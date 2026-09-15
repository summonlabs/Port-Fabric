#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "portfabric/export.hpp"
#include "portfabric/limits.hpp"
#include "portfabric/outcome.hpp"

namespace portfabric {

/// Stable reason vocabulary for deterministic explanations.
///
/// Every important decision the runtime makes is explainable through this
/// vocabulary. Codes are stable strings and are part of the observable contract.
enum class ExplanationCode : std::uint8_t {
  Accepted = 0,
  IdempotentReplay,
  AlreadyCurrent,
  RejectedMalformedRequest,
  RejectedUnknownPort,
  RejectedWrongEntityClass,
  RejectedStaleConfigurationGeneration,
  RejectedStaleCapabilityGeneration,
  RejectedStaleTopologyGeneration,
  RejectedStaleDeviceGeneration,
  RejectedStaleWorkerBoot,
  RejectedStaleCoordinatorEpoch,
  RejectedStaleOwnershipGeneration,
  RejectedUnauthorizedOwner,
  RejectedOwnershipConflict,
  RejectedPublisherFenced,
  RejectedFencedByRestart,
  RejectedUnsupportedConfiguration,
  RejectedCapabilityUnknown,
  RejectedCapabilityUnavailable,
  RejectedTransceiverUnknown,
  RejectedInvalidLifecycleTransition,
  RejectedInvalidConfiguration,
  RejectedInvalidSpeed,
  RejectedInvalidMtu,
  RejectedInvalidLaneConfiguration,
  RejectedInvalidBreakout,
  RejectedInvalidProfile,
  RejectedPortRetired,
  RejectedPortSuperseded,
  RejectedRevalidationRequired,
  RejectedDriftDetected,
  RejectedConflictDetected,
  RejectedApplyFailed,
  RejectedApplyOutcomeUnknown,
  RejectedReadbackMismatch,
  RejectedAdapterUnavailable,
  RejectedUnknownProfile,
  RejectedDuplicateRecord,
  RejectedResourceBoundExceeded,
  RejectedPersistenceCorruption,
  RejectedPersistenceVersionUnsupported,
  RejectedPersistenceIoFailure,
  RejectedPersistenceBoundsExceeded,
  RejectedTransportFailure,
  RejectedInternalError,
  RejectedShuttingDown,
  LifecycleUnconfigured,
  LifecycleSuperseded,
  LifecycleRetired,
  LifecycleRevalidationRequired,
  LifecycleConflicted,
  DriftObserved,
  AppliedEvidenceStale,
  AppliedOutcomeUnknownPending,
  CapabilityGenerationAdvanced,
  TopologyGenerationAdvanced,
  DeviceGenerationAdvanced,
  RecoveredFromPersistence,
  PublisherFenced,
  OwnershipHeld,
  ProfileBound,
  ConfigurationCommitted,
};

PF_EXPORT std::string_view to_string(ExplanationCode code) noexcept;
PF_EXPORT bool parse_explanation_code(std::string_view text, ExplanationCode& out) noexcept;

/// Explanation code that corresponds to an outcome code.
PF_EXPORT ExplanationCode explain_code_for(OutcomeCode code) noexcept;

/// One reason in an explanation.
struct PF_EXPORT ExplanationReason {
  ExplanationCode code = ExplanationCode::Accepted;
  /// Bounded, single-line detail naming the exact field, generation or process.
  std::string detail;

  std::string to_string() const;
};

/// Deterministic explanation of a decision or of a record's current state.
class PF_EXPORT Explanation {
 public:
  bool accepted = false;
  OutcomeCode outcome = OutcomeCode::Ok;
  /// Bounded single-line summary.
  std::string summary;

  Explanation& add(ExplanationCode code, std::string detail);

  const std::vector<ExplanationReason>& reasons() const noexcept { return reasons_; }

  /// True when the explanation contains the given reason code.
  bool has(ExplanationCode code) const noexcept;

  /// Stable rendering: summary line followed by one line per reason, in the
  /// order the reasons were added.
  std::string to_string() const;

 private:
  std::vector<ExplanationReason> reasons_;
};

}  // namespace portfabric
