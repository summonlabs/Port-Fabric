#include "engine_internal.hpp"

#include <algorithm>

#include "text.hpp"

namespace portfabric {

Outcome EngineImpl::check_authority(const AuthorityContext& authority) {
  if (!authority.valid()) {
    return Outcome::failure(OutcomeCode::MalformedRequest,
                            "the authority context is incomplete (epoch, publisher, boot and "
                            "attempt identity are all required)");
  }
  std::lock_guard<std::mutex> lock(authority_mutex);
  if (authority.epoch != epoch) {
    return Outcome::failure(OutcomeCode::StaleCoordinatorEpoch,
                            "the request carries a coordinator epoch that is not current")
        .with("current_epoch", epoch.valid() ? epoch.to_string() : std::string("none"))
        .with("request_epoch", authority.epoch.to_string());
  }
  const auto found = publishers.find(authority.publisher);
  if (found == publishers.end()) {
    if (config.publisher_registration == PublisherRegistration::Explicit) {
      return Outcome::failure(OutcomeCode::UnknownPublisher, "the publisher is not registered")
          .with("publisher", authority.publisher.to_string());
    }
    if (publishers.size() >= max_publishers) {
      return Outcome::failure(OutcomeCode::ResourceBoundExceeded,
                              "the publisher table is at its configured bound");
    }
    PublisherRecord record;
    record.publisher = authority.publisher;
    record.boot = authority.boot;
    record.epoch = authority.epoch;
    record.fenced = false;
    publishers.emplace(authority.publisher, std::move(record));
    return Outcome::success();
  }
  if (found->second.boot != authority.boot) {
    return Outcome::failure(OutcomeCode::StaleWorkerBoot,
                            "the request carries a worker boot that is not the current "
                            "incarnation of this publisher")
        .with("current_boot", found->second.boot.to_string())
        .with("request_boot", authority.boot.to_string());
  }
  if (found->second.fenced) {
    return Outcome::failure(OutcomeCode::PublisherFenced,
                            "the publisher incarnation is fenced and can never mutate again")
        .with("publisher", authority.publisher.to_string())
        .with("boot", authority.boot.to_string());
  }
  return Outcome::success();
}

CapabilityBinding EngineImpl::resolve_capabilities(const PortRecord& record,
                                                              Outcome& failure) {
  failure = Outcome::success();
  if (record.capability.present) {
    return record.capability;
  }
  if (capability_provider == nullptr) {
    failure = Outcome::failure(OutcomeCode::CapabilityUnavailable,
                               "no capability evidence is bound and no provider is configured");
    return CapabilityBinding{};
  }
  std::string error;
  std::optional<CapabilityBinding> binding = capability_provider->current(record.port, error);
  if (!binding.has_value()) {
    failure = Outcome::failure(
        OutcomeCode::CapabilityUnavailable,
        error.empty() ? std::string("the capability provider returned no evidence") : error);
    return CapabilityBinding{};
  }
  if (!binding->present) {
    failure = Outcome::failure(OutcomeCode::CapabilityUnknown,
                               "the capability provider reported UNKNOWN evidence");
    return CapabilityBinding{};
  }
  std::string validation_error;
  if (!binding->capabilities.validate(validation_error)) {
    failure = Outcome::failure(OutcomeCode::CapabilityUnavailable, validation_error);
    return CapabilityBinding{};
  }
  return *binding;
}

void EngineImpl::release_ownership_of(const PublisherId& publisher, const WorkerBootId& boot) {
  std::unique_lock<std::shared_mutex> lock(records_mutex);
  bool changed = false;
  for (auto& [port, record] : records) {
    (void)port;
    if (!record.ownership.has_value()) {
      continue;
    }
    if (record.ownership->publisher != publisher || record.ownership->boot != boot) {
      continue;
    }
    PortRecord before = record;
    record.generations.ownership = record.generations.ownership.valid()
                                       ? record.generations.ownership.next()
                                       : PortOwnershipGeneration::from_value(1);
    record.ownership.reset();
    record.status_reason = "OWNERSHIP_FENCED";
    record.status_detail = "ownership released when the holding incarnation was fenced";
    index_update(before, record);
    changed = true;
  }
  if (changed) {
    generation = generation.valid() ? generation.next() : EngineGeneration::from_value(1);
  }
}

void EngineImpl::release_ownership_before_epoch(CoordinatorEpoch current_epoch) {
  std::unique_lock<std::shared_mutex> lock(records_mutex);
  bool changed = false;
  for (auto& [port, record] : records) {
    (void)port;
    if (!record.ownership.has_value()) {
      continue;
    }
    if (!(record.ownership->epoch < current_epoch)) {
      continue;
    }
    PortRecord before = record;
    record.generations.ownership = record.generations.ownership.valid()
                                       ? record.generations.ownership.next()
                                       : PortOwnershipGeneration::from_value(1);
    record.ownership.reset();
    record.status_reason = "OWNERSHIP_RELEASED_ON_RESTART";
    record.status_detail = "ownership granted under an earlier coordinator epoch does not survive";
    index_update(before, record);
    changed = true;
  }
  if (changed) {
    generation = generation.valid() ? generation.next() : EngineGeneration::from_value(1);
  }
}

Outcome EngineImpl::persist_locked_state() {
  PersistenceMode mode = PersistenceMode::Manual;
  std::string path;
  {
    std::lock_guard<std::mutex> config_lock(authority_mutex);
    mode = config.persistence;
    path = config.persistence_path;
  }
  if (mode != PersistenceMode::Automatic) {
    return Outcome::success("persistence is manual");
  }
  if (path.empty()) {
    return Outcome::failure(OutcomeCode::PersistenceIoFailure,
                            "automatic persistence is enabled without a container path");
  }
  PersistedState state;
  {
    std::lock_guard<std::mutex> authority_lock(authority_mutex);
    state.fabric = config.fabric;
    state.site = config.site;
    state.epoch = epoch;
    state.topology = topology;
    state.publishers.reserve(publishers.size());
    for (const auto& [publisher, record] : publishers) {
      (void)publisher;
      state.publishers.push_back(record);
    }
    state.devices.reserve(devices.size());
    for (const auto& [device, binding] : devices) {
      (void)device;
      state.devices.push_back(binding);
    }
  }
  {
    std::lock_guard<std::mutex> profile_lock(profiles_mutex);
    state.profiles = profiles.list();
  }
  {
    std::shared_lock<std::shared_mutex> records_lock(records_mutex);
    state.engine_generation = generation;
    state.records.reserve(records.size());
    for (const auto& [port, record] : records) {
      (void)port;
      state.records.push_back(record);
    }
  }
  std::lock_guard<std::mutex> persistence_lock(persistence_mutex);
  const Outcome written = PersistenceStore::save(path, state);
  durability_dirty = !written.ok();
  return written;
}

void EngineImpl::mark_revalidation(const PortId& port, std::string reason,
                                               std::string detail) {
  {
    std::unique_lock<std::shared_mutex> lock(records_mutex);
    const auto found = records.find(port);
    if (found == records.end()) {
      return;
    }
    PortRecord before = found->second;
    PortRecord after = before;
    if (is_live(after.lifecycle) || after.lifecycle == PortLifecycle::Unconfigured) {
      after.lifecycle = PortLifecycle::RevalidationRequired;
      after.generations.lifecycle = after.generations.lifecycle.valid()
                                        ? after.generations.lifecycle.next()
                                        : LifecycleGeneration::from_value(1);
    }
    after.drift = DriftState::RevalidationRequired;
    after.status_reason = std::move(reason);
    after.status_detail = std::move(detail);
    index_update(before, after);
    found->second = std::move(after);
    generation = generation.valid() ? generation.next() : EngineGeneration::from_value(1);
  }
  (void)persist_locked_state();
}

MutationResult EngineImpl::record_apply_uncertainty(
    const PortId& port, const AuthorityContext& authority,
    const PortConfigurationGeneration& configuration_generation, AppliedOutcome outcome,
    std::string detail, OutcomeCode code) {
  MutationResult result;
  result.outcome = Outcome::failure(code, detail);
  {
    std::unique_lock<std::shared_mutex> lock(records_mutex);
    const auto found = records.find(port);
    if (found == records.end()) {
      return result;
    }
    PortRecord before = found->second;
    PortRecord after = before;
    after.applied.outcome = outcome;
    after.applied.generation = configuration_generation;
    after.applied.verified = false;
    after.applied.source = adapter != nullptr ? std::string(adapter->label()) : std::string("adapter");
    after.generations.evidence = after.generations.evidence.valid()
                                     ? after.generations.evidence.next()
                                     : EvidenceGeneration::from_value(1);
    after.applied.evidence_generation = after.generations.evidence;
    if (outcome == AppliedOutcome::OutcomeUnknown) {
      after.drift = DriftState::RevalidationRequired;
      // The device may hold a configuration this runtime never committed, so the
      // port is fenced for reconciliation even when no configuration generation
      // was committed yet. Retired and superseded ports keep their fencing.
      if (after.lifecycle != PortLifecycle::Retired &&
          after.lifecycle != PortLifecycle::Superseded) {
        after.lifecycle = PortLifecycle::RevalidationRequired;
        after.generations.lifecycle = after.generations.lifecycle.valid()
                                          ? after.generations.lifecycle.next()
                                          : LifecycleGeneration::from_value(1);
      }
      after.status_reason = "APPLY_OUTCOME_UNKNOWN";
    } else {
      after.drift = DriftState::Drifted;
      after.status_reason = "APPLY_INTERRUPTED";
    }
    after.status_detail = detail;
    after.provenance.kind = ProvenanceKind::Recovery;
    after.provenance.epoch = authority.epoch;
    after.provenance.attempt = authority.attempt;
    index_update(before, after);
    found->second = std::move(after);
    generation = generation.next();
  }
  (void)persist_locked_state();
  result.explanation.accepted = false;
  result.explanation.outcome = code;
  result.explanation.summary = detail;
  result.explanation.add(explain_code_for(code), detail);
  return result;
}

std::string ExpectedState::to_string() const {
  std::string out("configuration=");
  out.append(configuration.valid() ? configuration.to_string() : std::string("none"));
  out.append(" lifecycle=");
  out.append(lifecycle.valid() ? lifecycle.to_string() : std::string("none"));
  out.append(" administrative=");
  out.append(administrative.valid() ? administrative.to_string() : std::string("none"));
  out.append(" ownership=");
  out.append(ownership.valid() ? ownership.to_string() : std::string("none"));
  out.append(" evidence=");
  out.append(evidence.valid() ? evidence.to_string() : std::string("none"));
  out.append(" last_attempt=");
  out.append(last_attempt.valid() ? last_attempt.to_string() : std::string("none"));
  return out;
}

MutationResult EngineImpl::commit(const PortId& port, const ExpectedState& expected,
                                              const MutationPlan& plan,
                                              const Provenance& provenance,
                                              const AppliedEvidence& applied,
                                              bool applied_settled) {
  MutationResult result;
  PortRecord final_record;
  {
    std::unique_lock<std::shared_mutex> lock(records_mutex);
    const auto found = records.find(port);
    if (found == records.end()) {
      result.outcome = Outcome::failure(OutcomeCode::UnknownPort,
                                        "the port identity is not bound to this runtime");
      result.explanation.accepted = false;
      result.explanation.outcome = result.outcome.code();
      result.explanation.add(ExplanationCode::RejectedUnknownPort, port.to_string());
      return result;
    }
    PortRecord& current = found->second;
    if (current.last_mutation.attempt.valid() &&
        current.last_mutation.attempt == plan.next.last_mutation.attempt) {
      result.outcome = Outcome::idempotent(
          "the mutation attempt was already committed by a concurrent request");
      result.lifecycle = current.lifecycle;
      result.configuration_generation = current.generations.configuration;
      result.ownership_generation = current.generations.ownership;
      result.record_digest = digest_record(current);
      result.explanation.accepted = true;
      result.explanation.outcome = OutcomeCode::Idempotent;
      result.explanation.summary = result.outcome.message();
      result.explanation.add(ExplanationCode::IdempotentReplay, port.to_string());
      return result;
    }
    if (!expected.matches(current)) {
      result.outcome = Outcome::failure(
          OutcomeCode::StaleConfigurationGeneration,
          "authoritative state changed while this mutation was prepared");
      result.outcome.with("expected", expected.to_string())
          .with("current", render_port_summary(current));
      result.lifecycle = current.lifecycle;
      result.configuration_generation = current.generations.configuration;
      result.ownership_generation = current.generations.ownership;
      result.record_digest = digest_record(current);
      result.explanation.accepted = false;
      result.explanation.outcome = result.outcome.code();
      result.explanation.summary = result.outcome.message();
      result.explanation.add(ExplanationCode::RejectedStaleConfigurationGeneration,
                             "compare-and-commit rejected the mutation");
      return result;
    }
    if (current.lifecycle == PortLifecycle::Retired &&
        plan.next.lifecycle != PortLifecycle::Retired) {
      result.outcome = Outcome::failure(OutcomeCode::PortRetired,
                                        "a retired port can never be resurrected");
      result.lifecycle = current.lifecycle;
      result.explanation.accepted = false;
      result.explanation.outcome = result.outcome.code();
      result.explanation.add(ExplanationCode::RejectedPortRetired, port.to_string());
      return result;
    }
    if (generation.exhaustible()) {
      result.outcome = Outcome::failure(OutcomeCode::ResourceBoundExceeded,
                                        "the engine generation can no longer advance");
      result.explanation.accepted = false;
      result.explanation.outcome = result.outcome.code();
      return result;
    }
    PortRecord updated = plan.next;
    updated.provenance = provenance;
    if (applied_settled) {
      updated.applied = applied;
    }
    index_update(current, updated);
    current = std::move(updated);
    generation = generation.next();
    final_record = current;
  }

  const Outcome persisted = persist_locked_state();
  result.lifecycle = final_record.lifecycle;
  result.configuration_generation = final_record.generations.configuration;
  result.ownership_generation = final_record.generations.ownership;
  result.record_digest = digest_record(final_record);
  if (!persisted.ok()) {
    result.outcome = Outcome::failure(
        OutcomeCode::PersistenceIoFailure,
        "the mutation was committed in memory but durable state could not be written");
    result.outcome.with("persistence", persisted.to_string());
    result.explanation.accepted = false;
    result.explanation.outcome = result.outcome.code();
    result.explanation.summary = result.outcome.message();
    result.explanation.add(ExplanationCode::RejectedPersistenceIoFailure, persisted.message());
    return result;
  }
  result.outcome = Outcome::success("mutation committed");
  result.explanation.accepted = true;
  result.explanation.outcome = OutcomeCode::Ok;
  result.explanation.summary = result.outcome.message();
  return result;
}

}  // namespace portfabric
