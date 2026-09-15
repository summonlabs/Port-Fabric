#include "engine_internal.hpp"

#include <algorithm>

namespace portfabric {

Explanation PortFabricEngine::explain(const PortId& port) const {
  Explanation explanation;
  std::optional<PortRecord> record_copy;
  EngineGeneration current_generation;
  CoordinatorEpoch current_epoch;
  TopologyGeneration current_topology;
  {
    std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
    const auto found = impl_->records.find(port);
    if (found != impl_->records.end()) {
      record_copy = found->second;
    }
    current_generation = impl_->generation;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->authority_mutex);
    current_epoch = impl_->epoch;
    current_topology = impl_->topology;
  }
  if (!record_copy.has_value()) {
    explanation.accepted = false;
    explanation.outcome = OutcomeCode::UnknownPort;
    explanation.summary = "the port identity is not bound to this runtime";
    explanation.add(ExplanationCode::RejectedUnknownPort, port.to_string());
    return explanation;
  }
  const PortRecord& record = *record_copy;
  explanation.accepted = true;
  explanation.outcome = OutcomeCode::Ok;
  explanation.summary = render_port_summary(record);
  explanation.add(ExplanationCode::Accepted, "record is bound and readable");
  switch (record.lifecycle) {
    case PortLifecycle::Unconfigured:
      explanation.add(ExplanationCode::LifecycleUnconfigured,
                      "no configuration generation has been committed for this port");
      break;
    case PortLifecycle::Superseded:
      explanation.add(ExplanationCode::LifecycleSuperseded,
                      record.status_detail.empty() ? "the configuration generation is fenced"
                                                   : record.status_detail);
      break;
    case PortLifecycle::Retired:
      explanation.add(ExplanationCode::LifecycleRetired,
                      record.status_detail.empty() ? "the port is retired" : record.status_detail);
      break;
    case PortLifecycle::RevalidationRequired:
      explanation.add(ExplanationCode::LifecycleRevalidationRequired,
                      record.status_detail.empty() ? "the committed configuration must be "
                                                     "revalidated against current evidence"
                                                   : record.status_detail);
      break;
    case PortLifecycle::Conflicted:
      explanation.add(ExplanationCode::LifecycleConflicted,
                      record.status_detail.empty() ? "two authorities disagree about this port"
                                                   : record.status_detail);
      break;
    default:
      break;
  }
  if (record.ownership.has_value()) {
    explanation.add(ExplanationCode::OwnershipHeld, record.ownership->to_string());
  }
  if (record.configuration.has_value()) {
    const PortConfiguration& configuration = *record.configuration;
    if (configuration.profile.bound) {
      explanation.add(ExplanationCode::ProfileBound, configuration.profile.to_string());
    }
    if (configuration.device_generation != record.generations.device) {
      explanation.add(ExplanationCode::DeviceGenerationAdvanced,
                      "the committed configuration is bound to device generation " +
                          configuration.device_generation.to_string() + " while the runtime "
                          "records generation " + record.generations.device.to_string());
    }
    if (configuration.topology_generation != current_topology) {
      explanation.add(ExplanationCode::TopologyGenerationAdvanced,
                      "the committed configuration is bound to topology generation " +
                          configuration.topology_generation.to_string() +
                          " while the runtime records generation " + current_topology.to_string());
    }
    if (configuration.capability_generation != record.generations.capability) {
      explanation.add(ExplanationCode::CapabilityGenerationAdvanced,
                      "the committed configuration is bound to capability generation " +
                          configuration.capability_generation.to_string() +
                          " while the bound evidence generation is " +
                          record.generations.capability.to_string());
    }
    const bool applied_current = record.applied.current_for(configuration.generation);
    if (!applied_current) {
      explanation.add(ExplanationCode::AppliedEvidenceStale,
                      "physical application evidence for generation " +
                          configuration.generation.to_string() + " is " +
                          std::string(portfabric::to_string(record.applied.outcome)) +
                          (record.applied.verified ? " (verified)"
                                                   : " (not independently verified)"));
    }
    if (record.applied.outcome == AppliedOutcome::OutcomeUnknown) {
      explanation.add(ExplanationCode::AppliedOutcomeUnknownPending,
                      "the last apply attempt has an unknown outcome and requires readback");
    }
  }
  switch (record.drift) {
    case DriftState::Drifted:
      explanation.add(ExplanationCode::DriftObserved,
                      record.status_detail.empty() ? "observed device state differs from desired "
                                                     "state"
                                                   : record.status_detail);
      break;
    case DriftState::InSync:
      explanation.add(ExplanationCode::DriftObserved, "observed device state matches desired state");
      break;
    case DriftState::RevalidationRequired:
      explanation.add(ExplanationCode::DriftObserved,
                      "drift requires revalidation before the configuration is trusted again");
      break;
    case DriftState::Conflicted:
      explanation.add(ExplanationCode::DriftObserved,
                      "the device reports a configuration that conflicts with authoritative state");
      break;
    case DriftState::Unknown:
      break;
  }
  if (record.provenance.kind == ProvenanceKind::Recovery) {
    explanation.add(ExplanationCode::RecoveredFromPersistence,
                    "the record was recovered from durable state and requires reconciliation");
  }
  explanation.add(ExplanationCode::ConfigurationCommitted,
                  "engine generation " +
                      (current_generation.valid() ? current_generation.to_string()
                                                  : std::string("none")) +
                      " coordinator epoch " +
                      (current_epoch.valid() ? current_epoch.to_string() : std::string("none")));
  return explanation;
}

ConfigurationDiff PortFabricEngine::diff(const PortId& port,
                                         const PortConfiguration& proposed) const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  const auto found = impl_->records.find(port);
  if (found == impl_->records.end()) {
    return ConfigurationDiff{};
  }
  PortRecord candidate = found->second;
  candidate.configuration = proposed;
  return diff_records(found->second, candidate);
}

Snapshot PortFabricEngine::snapshot() const {
  Snapshot snapshot_value;
  {
    std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
    snapshot_value.records.reserve(impl_->records.size());
    for (const auto& [port, record] : impl_->records) {
      (void)port;
      snapshot_value.records.push_back(record);
    }
    snapshot_value.generation = impl_->generation;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->authority_mutex);
    snapshot_value.epoch = impl_->epoch;
    snapshot_value.topology = impl_->topology;
  }
  std::sort(snapshot_value.records.begin(), snapshot_value.records.end(),
            [](const PortRecord& lhs, const PortRecord& rhs) { return lhs.port < rhs.port; });
  snapshot_value.digest = digest_records(snapshot_value.records);
  snapshot_value.id = SnapshotId::from_validated(compose_identifier(
      "snap", impl_->config.coordinator.value(), snapshot_value.generation.value()));
  return snapshot_value;
}

Snapshot PortFabricEngine::snapshot_of(const PortId& port) const {
  Snapshot snapshot_value;
  std::optional<PortRecord> record_copy;
  {
    std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
    const auto found = impl_->records.find(port);
    if (found != impl_->records.end()) {
      record_copy = found->second;
    }
    snapshot_value.generation = impl_->generation;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->authority_mutex);
    snapshot_value.epoch = impl_->epoch;
    snapshot_value.topology = impl_->topology;
  }
  if (record_copy.has_value()) {
    snapshot_value.records.push_back(*record_copy);
  }
  snapshot_value.digest = digest_records(snapshot_value.records);
  snapshot_value.id = SnapshotId::from_validated(compose_identifier(
      "snap", impl_->config.coordinator.value(), snapshot_value.generation.value()));
  return snapshot_value;
}

}  // namespace portfabric
