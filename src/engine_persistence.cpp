#include "engine_internal.hpp"

#include <algorithm>

namespace portfabric {
namespace {

PersistedState gather_state(const EngineImpl& impl) {
  PersistedState state;
  {
    std::lock_guard<std::mutex> authority_lock(impl.authority_mutex);
    state.fabric = impl.config.fabric;
    state.site = impl.config.site;
    state.epoch = impl.epoch;
    state.topology = impl.topology;
    state.publishers.reserve(impl.publishers.size());
    for (const auto& [publisher, record] : impl.publishers) {
      (void)publisher;
      state.publishers.push_back(record);
    }
    state.devices.reserve(impl.devices.size());
    for (const auto& [device, binding] : impl.devices) {
      (void)device;
      state.devices.push_back(binding);
    }
  }
  {
    std::lock_guard<std::mutex> profile_lock(impl.profiles_mutex);
    state.profiles = impl.profiles.list();
  }
  {
    std::shared_lock<std::shared_mutex> records_lock(impl.records_mutex);
    state.engine_generation = impl.generation;
    state.records.reserve(impl.records.size());
    for (const auto& [port, record] : impl.records) {
      (void)port;
      state.records.push_back(record);
    }
  }
  return state;
}

}  // namespace

Outcome PortFabricEngine::save() {
  const std::string path = impl_->config.persistence_path;
  if (path.empty()) {
    return Outcome::failure(OutcomeCode::PersistenceIoFailure,
                            "no persistence container path is configured");
  }
  const PersistedState state = gather_state(*impl_);
  std::lock_guard<std::mutex> persistence_lock(impl_->persistence_mutex);
  const Outcome written = PersistenceStore::save(path, state);
  impl_->durability_dirty = !written.ok();
  return written;
}

Outcome PortFabricEngine::load() {
  const std::string path = impl_->config.persistence_path;
  if (path.empty()) {
    return Outcome::failure(OutcomeCode::PersistenceIoFailure,
                            "no persistence container path is configured");
  }
  PersistedState state;
  {
    std::lock_guard<std::mutex> persistence_lock(impl_->persistence_mutex);
    const Outcome read = PersistenceStore::load(path, state);
    if (!read.ok()) {
      return read;
    }
  }
  if (state.records.size() > impl_->config.port_bound) {
    return Outcome::failure(OutcomeCode::PersistenceBoundsExceeded,
                            "the container carries more records than the configured port bound");
  }
  if (impl_->config.fabric.valid() && state.fabric.valid() && impl_->config.fabric != state.fabric) {
    return Outcome::failure(OutcomeCode::ConflictDetected,
                            "the container belongs to a different fabric authority domain");
  }
  if (impl_->config.site.valid() && state.site.valid() && impl_->config.site != state.site) {
    return Outcome::failure(OutcomeCode::ConflictDetected,
                            "the container belongs to a different site authority domain");
  }

  // Profiles first: the profile registry validates generation ordering itself.
  {
    std::lock_guard<std::mutex> profile_lock(impl_->profiles_mutex);
    impl_->profiles.clear();
    std::vector<PortProfile> ordered = state.profiles;
    std::sort(ordered.begin(), ordered.end(),
              [](const PortProfile& lhs, const PortProfile& rhs) {
                if (lhs.id != rhs.id) {
                  return lhs.id < rhs.id;
                }
                return lhs.generation < rhs.generation;
              });
    for (const PortProfile& profile : ordered) {
      const Outcome defined = impl_->profiles.define(profile);
      if (!defined.accepted()) {
        return Outcome::failure(OutcomeCode::PersistenceCorruption,
                                "a recovered profile could not be restored: " +
                                    defined.to_string());
      }
    }
  }

  // Authority state, then records: the documented lock order is
  // authority -> records.
  {
    std::lock_guard<std::mutex> authority_lock(impl_->authority_mutex);
    if (state.epoch >= impl_->epoch) {
      if (state.epoch.exhaustible()) {
        return Outcome::failure(OutcomeCode::ResourceBoundExceeded,
                                "the persisted coordinator epoch can no longer advance");
      }
      impl_->epoch = state.epoch.next();
    }
    impl_->topology = state.topology.valid() && state.topology > impl_->topology
                          ? state.topology
                          : impl_->topology;
    impl_->devices.clear();
    for (const DeviceBinding& binding : state.devices) {
      impl_->devices[binding.device] = binding;
    }
    impl_->publishers.clear();
    for (const PublisherRecord& publisher : state.publishers) {
      PublisherRecord fenced = publisher;
      // Every publisher incarnation from a previous process life is fenced: a
      // restart never restores live process authority.
      fenced.fenced = true;
      impl_->publishers[publisher.publisher] = fenced;
    }
  }

  {
    std::unique_lock<std::shared_mutex> records_lock(impl_->records_mutex);
    impl_->records.clear();
    for (PortRecord record : state.records) {
      // Conservative recovery. Durable desired configuration survives; live
      // process authority and hardware application freshness do not.
      if (record.ownership.has_value()) {
        record.generations.ownership = record.generations.ownership.valid()
                                           ? record.generations.ownership.next()
                                           : PortOwnershipGeneration::from_value(1);
        record.ownership.reset();
        record.status_reason = "OWNERSHIP_RELEASED_ON_RECOVERY";
        record.status_detail =
            "ownership held by a previous process incarnation does not survive restart";
      }
      if (record.applied.outcome == AppliedOutcome::Applied ||
          record.applied.outcome == AppliedOutcome::Verified) {
        record.applied.outcome = AppliedOutcome::OutcomeUnknown;
        record.applied.verified = false;
        record.applied.source = "recovered";
        record.generations.evidence =
            record.generations.evidence.valid() ? record.generations.evidence.next()
                                                : EvidenceGeneration::from_value(1);
        record.applied.evidence_generation = record.generations.evidence;
      }
      if (record.configuration.has_value() && record.lifecycle != PortLifecycle::Retired) {
        record.lifecycle = PortLifecycle::RevalidationRequired;
        record.generations.lifecycle = record.generations.lifecycle.valid()
                                           ? record.generations.lifecycle.next()
                                           : LifecycleGeneration::from_value(1);
        record.drift = DriftState::RevalidationRequired;
        record.status_reason = "RECOVERED_FROM_PERSISTENCE";
        record.status_detail =
            "durable desired configuration recovered; revalidation against current device, "
            "topology and capability generations is required before it is trusted again";
      }
      // A recovered record must not answer an old replay as IDEMPOTENT.
      record.last_mutation = LastMutationRecord{};
      record.provenance.kind = ProvenanceKind::Recovery;
      std::string validation_error;
      if (!record.validate(validation_error)) {
        return Outcome::failure(OutcomeCode::PersistenceCorruption,
                                "a recovered record is inconsistent: " + validation_error);
      }
      impl_->records.emplace(record.port, std::move(record));
    }
    impl_->reindex_all();
    if (state.engine_generation.valid()) {
      if (state.engine_generation.exhaustible()) {
        return Outcome::failure(OutcomeCode::ResourceBoundExceeded,
                                "the persisted engine generation can no longer advance");
      }
      impl_->generation = state.engine_generation.next();
    } else {
      impl_->generation = EngineGeneration::from_value(1);
    }
  }
  return Outcome::success("durable state recovered conservatively");
}

}  // namespace portfabric
