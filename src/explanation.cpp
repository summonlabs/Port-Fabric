#include "portfabric/explanation.hpp"

#include "text.hpp"

namespace portfabric {

std::string_view to_string(ExplanationCode code) noexcept {
  switch (code) {
    case ExplanationCode::Accepted:
      return "ACCEPTED";
    case ExplanationCode::IdempotentReplay:
      return "IDEMPOTENT_REPLAY";
    case ExplanationCode::AlreadyCurrent:
      return "ALREADY_CURRENT";
    case ExplanationCode::RejectedMalformedRequest:
      return "REJECTED_MALFORMED_REQUEST";
    case ExplanationCode::RejectedUnknownPort:
      return "REJECTED_UNKNOWN_PORT";
    case ExplanationCode::RejectedWrongEntityClass:
      return "REJECTED_WRONG_ENTITY_CLASS";
    case ExplanationCode::RejectedStaleConfigurationGeneration:
      return "REJECTED_STALE_CONFIGURATION_GENERATION";
    case ExplanationCode::RejectedStaleCapabilityGeneration:
      return "REJECTED_STALE_CAPABILITY_GENERATION";
    case ExplanationCode::RejectedStaleTopologyGeneration:
      return "REJECTED_STALE_TOPOLOGY_GENERATION";
    case ExplanationCode::RejectedStaleDeviceGeneration:
      return "REJECTED_STALE_DEVICE_GENERATION";
    case ExplanationCode::RejectedStaleWorkerBoot:
      return "REJECTED_STALE_WORKER_BOOT";
    case ExplanationCode::RejectedStaleCoordinatorEpoch:
      return "REJECTED_STALE_COORDINATOR_EPOCH";
    case ExplanationCode::RejectedStaleOwnershipGeneration:
      return "REJECTED_STALE_OWNERSHIP_GENERATION";
    case ExplanationCode::RejectedUnauthorizedOwner:
      return "REJECTED_UNAUTHORIZED_OWNER";
    case ExplanationCode::RejectedOwnershipConflict:
      return "REJECTED_OWNERSHIP_CONFLICT";
    case ExplanationCode::RejectedPublisherFenced:
      return "REJECTED_PUBLISHER_FENCED";
    case ExplanationCode::RejectedFencedByRestart:
      return "REJECTED_FENCED_BY_RESTART";
    case ExplanationCode::RejectedUnsupportedConfiguration:
      return "REJECTED_UNSUPPORTED_CONFIGURATION";
    case ExplanationCode::RejectedCapabilityUnknown:
      return "REJECTED_CAPABILITY_UNKNOWN";
    case ExplanationCode::RejectedCapabilityUnavailable:
      return "REJECTED_CAPABILITY_UNAVAILABLE";
    case ExplanationCode::RejectedTransceiverUnknown:
      return "REJECTED_TRANSCEIVER_UNKNOWN";
    case ExplanationCode::RejectedInvalidLifecycleTransition:
      return "REJECTED_INVALID_LIFECYCLE_TRANSITION";
    case ExplanationCode::RejectedInvalidConfiguration:
      return "REJECTED_INVALID_CONFIGURATION";
    case ExplanationCode::RejectedInvalidSpeed:
      return "REJECTED_INVALID_SPEED";
    case ExplanationCode::RejectedInvalidMtu:
      return "REJECTED_INVALID_MTU";
    case ExplanationCode::RejectedInvalidLaneConfiguration:
      return "REJECTED_INVALID_LANE_CONFIGURATION";
    case ExplanationCode::RejectedInvalidBreakout:
      return "REJECTED_INVALID_BREAKOUT";
    case ExplanationCode::RejectedInvalidProfile:
      return "REJECTED_INVALID_PROFILE";
    case ExplanationCode::RejectedPortRetired:
      return "REJECTED_PORT_RETIRED";
    case ExplanationCode::RejectedPortSuperseded:
      return "REJECTED_PORT_SUPERSEDED";
    case ExplanationCode::RejectedRevalidationRequired:
      return "REJECTED_REVALIDATION_REQUIRED";
    case ExplanationCode::RejectedDriftDetected:
      return "REJECTED_DRIFT_DETECTED";
    case ExplanationCode::RejectedConflictDetected:
      return "REJECTED_CONFLICT_DETECTED";
    case ExplanationCode::RejectedApplyFailed:
      return "REJECTED_APPLY_FAILED";
    case ExplanationCode::RejectedApplyOutcomeUnknown:
      return "REJECTED_APPLY_OUTCOME_UNKNOWN";
    case ExplanationCode::RejectedReadbackMismatch:
      return "REJECTED_READBACK_MISMATCH";
    case ExplanationCode::RejectedAdapterUnavailable:
      return "REJECTED_ADAPTER_UNAVAILABLE";
    case ExplanationCode::RejectedUnknownProfile:
      return "REJECTED_UNKNOWN_PROFILE";
    case ExplanationCode::RejectedDuplicateRecord:
      return "REJECTED_DUPLICATE_RECORD";
    case ExplanationCode::RejectedResourceBoundExceeded:
      return "REJECTED_RESOURCE_BOUND_EXCEEDED";
    case ExplanationCode::RejectedPersistenceCorruption:
      return "REJECTED_PERSISTENCE_CORRUPTION";
    case ExplanationCode::RejectedPersistenceVersionUnsupported:
      return "REJECTED_PERSISTENCE_VERSION_UNSUPPORTED";
    case ExplanationCode::RejectedPersistenceIoFailure:
      return "REJECTED_PERSISTENCE_IO_FAILURE";
    case ExplanationCode::RejectedPersistenceBoundsExceeded:
      return "REJECTED_PERSISTENCE_BOUNDS_EXCEEDED";
    case ExplanationCode::RejectedTransportFailure:
      return "REJECTED_TRANSPORT_FAILURE";
    case ExplanationCode::RejectedInternalError:
      return "REJECTED_INTERNAL_ERROR";
    case ExplanationCode::RejectedShuttingDown:
      return "REJECTED_SHUTTING_DOWN";
    case ExplanationCode::LifecycleUnconfigured:
      return "LIFECYCLE_UNCONFIGURED";
    case ExplanationCode::LifecycleSuperseded:
      return "LIFECYCLE_SUPERSEDED";
    case ExplanationCode::LifecycleRetired:
      return "LIFECYCLE_RETIRED";
    case ExplanationCode::LifecycleRevalidationRequired:
      return "LIFECYCLE_REVALIDATION_REQUIRED";
    case ExplanationCode::LifecycleConflicted:
      return "LIFECYCLE_CONFLICTED";
    case ExplanationCode::DriftObserved:
      return "DRIFT_OBSERVED";
    case ExplanationCode::AppliedEvidenceStale:
      return "APPLIED_EVIDENCE_STALE";
    case ExplanationCode::AppliedOutcomeUnknownPending:
      return "APPLIED_OUTCOME_UNKNOWN_PENDING";
    case ExplanationCode::CapabilityGenerationAdvanced:
      return "CAPABILITY_GENERATION_ADVANCED";
    case ExplanationCode::TopologyGenerationAdvanced:
      return "TOPOLOGY_GENERATION_ADVANCED";
    case ExplanationCode::DeviceGenerationAdvanced:
      return "DEVICE_GENERATION_ADVANCED";
    case ExplanationCode::RecoveredFromPersistence:
      return "RECOVERED_FROM_PERSISTENCE";
    case ExplanationCode::PublisherFenced:
      return "PUBLISHER_FENCED";
    case ExplanationCode::OwnershipHeld:
      return "OWNERSHIP_HELD";
    case ExplanationCode::ProfileBound:
      return "PROFILE_BOUND";
    case ExplanationCode::ConfigurationCommitted:
      return "CONFIGURATION_COMMITTED";
  }
  return "REJECTED_INTERNAL_ERROR";
}

bool parse_explanation_code(std::string_view text, ExplanationCode& out) noexcept {
  for (std::uint16_t index = 0; index <= static_cast<std::uint16_t>(ExplanationCode::ConfigurationCommitted);
       ++index) {
    const auto code = static_cast<ExplanationCode>(index);
    if (portfabric::to_string(code) == text) {
      out = code;
      return true;
    }
  }
  return false;
}

ExplanationCode explain_code_for(OutcomeCode code) noexcept {
  switch (code) {
    case OutcomeCode::Ok:
      return ExplanationCode::Accepted;
    case OutcomeCode::Idempotent:
      return ExplanationCode::IdempotentReplay;
    case OutcomeCode::AlreadyCurrent:
      return ExplanationCode::AlreadyCurrent;
    case OutcomeCode::MalformedRequest:
      return ExplanationCode::RejectedMalformedRequest;
    case OutcomeCode::UnknownPort:
      return ExplanationCode::RejectedUnknownPort;
    case OutcomeCode::UnknownDevice:
      return ExplanationCode::RejectedUnknownPort;
    case OutcomeCode::UnknownProfile:
      return ExplanationCode::RejectedUnknownProfile;
    case OutcomeCode::UnknownPublisher:
      return ExplanationCode::RejectedUnauthorizedOwner;
    case OutcomeCode::UnknownOwnership:
      return ExplanationCode::RejectedUnauthorizedOwner;
    case OutcomeCode::UnknownAggregate:
      return ExplanationCode::RejectedInvalidConfiguration;
    case OutcomeCode::WrongEntityClass:
      return ExplanationCode::RejectedWrongEntityClass;
    case OutcomeCode::DuplicateRecord:
      return ExplanationCode::RejectedDuplicateRecord;
    case OutcomeCode::ResourceBoundExceeded:
      return ExplanationCode::RejectedResourceBoundExceeded;
    case OutcomeCode::InvalidText:
      return ExplanationCode::RejectedMalformedRequest;
    case OutcomeCode::InvalidEnumValue:
      return ExplanationCode::RejectedMalformedRequest;
    case OutcomeCode::StaleDeviceGeneration:
      return ExplanationCode::RejectedStaleDeviceGeneration;
    case OutcomeCode::StaleTopologyGeneration:
      return ExplanationCode::RejectedStaleTopologyGeneration;
    case OutcomeCode::StaleConfigurationGeneration:
      return ExplanationCode::RejectedStaleConfigurationGeneration;
    case OutcomeCode::StaleCapabilityGeneration:
      return ExplanationCode::RejectedStaleCapabilityGeneration;
    case OutcomeCode::StaleWorkerBoot:
      return ExplanationCode::RejectedStaleWorkerBoot;
    case OutcomeCode::StaleCoordinatorEpoch:
      return ExplanationCode::RejectedStaleCoordinatorEpoch;
    case OutcomeCode::StaleOwnershipGeneration:
      return ExplanationCode::RejectedStaleOwnershipGeneration;
    case OutcomeCode::UnauthorizedOwner:
      return ExplanationCode::RejectedUnauthorizedOwner;
    case OutcomeCode::OwnershipConflict:
      return ExplanationCode::RejectedOwnershipConflict;
    case OutcomeCode::PublisherFenced:
      return ExplanationCode::RejectedPublisherFenced;
    case OutcomeCode::FencedByRestart:
      return ExplanationCode::RejectedFencedByRestart;
    case OutcomeCode::UnsupportedConfiguration:
      return ExplanationCode::RejectedUnsupportedConfiguration;
    case OutcomeCode::CapabilityUnknown:
      return ExplanationCode::RejectedCapabilityUnknown;
    case OutcomeCode::CapabilityUnavailable:
      return ExplanationCode::RejectedCapabilityUnavailable;
    case OutcomeCode::InvalidLifecycleTransition:
      return ExplanationCode::RejectedInvalidLifecycleTransition;
    case OutcomeCode::InvalidConfiguration:
      return ExplanationCode::RejectedInvalidConfiguration;
    case OutcomeCode::InvalidSpeed:
      return ExplanationCode::RejectedInvalidSpeed;
    case OutcomeCode::InvalidMtu:
      return ExplanationCode::RejectedInvalidMtu;
    case OutcomeCode::InvalidLaneConfiguration:
      return ExplanationCode::RejectedInvalidLaneConfiguration;
    case OutcomeCode::InvalidBreakout:
      return ExplanationCode::RejectedInvalidBreakout;
    case OutcomeCode::InvalidProfile:
      return ExplanationCode::RejectedInvalidProfile;
    case OutcomeCode::PortRetired:
      return ExplanationCode::RejectedPortRetired;
    case OutcomeCode::PortSuperseded:
      return ExplanationCode::RejectedPortSuperseded;
    case OutcomeCode::RevalidationRequired:
      return ExplanationCode::RejectedRevalidationRequired;
    case OutcomeCode::DriftDetected:
      return ExplanationCode::RejectedDriftDetected;
    case OutcomeCode::ConflictDetected:
      return ExplanationCode::RejectedConflictDetected;
    case OutcomeCode::TransceiverUnknown:
      return ExplanationCode::RejectedTransceiverUnknown;
    case OutcomeCode::ApplyFailed:
      return ExplanationCode::RejectedApplyFailed;
    case OutcomeCode::ApplyOutcomeUnknown:
      return ExplanationCode::RejectedApplyOutcomeUnknown;
    case OutcomeCode::ReadbackMismatch:
      return ExplanationCode::RejectedReadbackMismatch;
    case OutcomeCode::AdapterUnavailable:
      return ExplanationCode::RejectedAdapterUnavailable;
    case OutcomeCode::PersistenceCorruption:
      return ExplanationCode::RejectedPersistenceCorruption;
    case OutcomeCode::PersistenceVersionUnsupported:
      return ExplanationCode::RejectedPersistenceVersionUnsupported;
    case OutcomeCode::PersistenceIoFailure:
      return ExplanationCode::RejectedPersistenceIoFailure;
    case OutcomeCode::PersistenceBoundsExceeded:
      return ExplanationCode::RejectedPersistenceBoundsExceeded;
    case OutcomeCode::TransportFailure:
      return ExplanationCode::RejectedTransportFailure;
    case OutcomeCode::InternalError:
      return ExplanationCode::RejectedInternalError;
    case OutcomeCode::ShuttingDown:
      return ExplanationCode::RejectedShuttingDown;
  }
  return ExplanationCode::RejectedInternalError;
}

std::string ExplanationReason::to_string() const {
  std::string out(portfabric::to_string(code));
  if (!detail.empty()) {
    out.append(": ");
    out.append(detail);
  }
  return out;
}

Explanation& Explanation::add(ExplanationCode code, std::string detail) {
  if (reasons_.size() >= max_explanation_reasons) {
    return *this;
  }
  ExplanationReason reason;
  reason.code = code;
  reason.detail = detail::single_line(detail);
  if (reason.detail.size() > max_description_length) {
    reason.detail.resize(max_description_length);
  }
  reasons_.push_back(std::move(reason));
  return *this;
}

bool Explanation::has(ExplanationCode code) const noexcept {
  for (const ExplanationReason& reason : reasons_) {
    if (reason.code == code) {
      return true;
    }
  }
  return false;
}

std::string Explanation::to_string() const {
  std::string out(accepted ? "ACCEPTED" : "REJECTED");
  out.append(" [");
  out.append(portfabric::to_string(outcome));
  out.append("]");
  if (!summary.empty()) {
    out.append(" ");
    out.append(summary);
  }
  for (const ExplanationReason& reason : reasons_) {
    out.append("\n  ");
    out.append(reason.to_string());
  }
  return out;
}

}  // namespace portfabric
