#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "portfabric/capability.hpp"
#include "portfabric/configuration.hpp"
#include "portfabric/export.hpp"
#include "portfabric/generation.hpp"
#include "portfabric/ids.hpp"
#include "portfabric/lifecycle.hpp"
#include "portfabric/ownership.hpp"

namespace portfabric {

/// What is known about physical application of the committed configuration.
enum class AppliedOutcome : std::uint8_t {
  /// No application attempt has been made for the current generation.
  NotAttempted = 0,
  /// An adapter reported a completed application. Without independent readback
  /// this is an apply acknowledgement, not verified hardware truth.
  Applied,
  /// An adapter reported failure.
  ApplyFailed,
  /// The apply attempt's completion is unknown: the outcome must be reconciled
  /// by readback before authoritative completion.
  OutcomeUnknown,
  /// A readback confirmed the device matches the committed configuration.
  Verified,
  /// No adapter exists for this port class; the runtime holds desired state only.
  Unsupported,
};

PF_EXPORT std::string_view to_string(AppliedOutcome outcome) noexcept;
PF_EXPORT bool parse_applied_outcome(std::string_view text, AppliedOutcome& out) noexcept;
PF_EXPORT bool is_valid(AppliedOutcome outcome) noexcept;

/// Evidence about physical application of a configuration generation.
struct PF_EXPORT AppliedEvidence {
  AppliedOutcome outcome = AppliedOutcome::NotAttempted;
  /// Configuration generation this evidence describes.
  PortConfigurationGeneration generation;
  EvidenceGeneration evidence_generation;
  /// Bounded label of the component that produced the evidence.
  std::string source;
  /// True only when a readback confirmed the device state for this generation.
  bool verified = false;

  /// True when the evidence may be treated as current hardware truth.
  ///
  /// Persisted evidence never satisfies this test after a restart: recovery
  /// downgrades verified evidence to unverified so that a restart cannot turn
  /// durable state into fresh hardware truth.
  bool current_for(const PortConfigurationGeneration& expected_generation) const noexcept {
    return verified && expected_generation.valid() && generation == expected_generation;
  }

  std::string to_string() const;
};

/// Drift between authoritative desired state and observed device state.
enum class DriftState : std::uint8_t {
  Unknown = 0,
  InSync,
  Drifted,
  RevalidationRequired,
  Conflicted,
};

PF_EXPORT std::string_view to_string(DriftState state) noexcept;
PF_EXPORT bool parse_drift_state(std::string_view text, DriftState& out) noexcept;
PF_EXPORT bool is_valid(DriftState state) noexcept;

/// All generations that apply to one port record.
struct PF_EXPORT GenerationVector {
  PortConfigurationGeneration configuration;
  AdministrativeGeneration administrative;
  PortOwnershipGeneration ownership;
  CapabilityBindingGeneration capability;
  TopologyGeneration topology;
  DeviceGeneration device;
  LifecycleGeneration lifecycle;
  EvidenceGeneration evidence;

  std::string to_string() const;
  std::vector<std::pair<std::string, std::string>> canonical_fields() const;
};

/// Identity and result of the last committed mutation of a port.
///
/// This is what makes exact replay distinguishable from a stale request: replay
/// of the same attempt with the same semantic payload is IDEMPOTENT, whereas an
/// older expectation than the current generation is STALE.
struct PF_EXPORT LastMutationRecord {
  MutationAttemptId attempt;
  /// Semantic payload digest of the committed mutation.
  std::uint64_t payload_digest = 0;
  PortConfigurationGeneration result_generation;

  bool valid() const noexcept { return attempt.valid() && payload_digest != 0; }

  std::string to_string() const;
};

/// Authoritative record of one governed port.
class PF_EXPORT PortRecord {
 public:
  /// Canonical identity from Fabric Registry.
  PortId port;
  /// Parent device identity as recorded by Fabric Registry.
  DeviceId parent_device;
  /// Lifecycle state. Never derived from operational link condition.
  PortLifecycle lifecycle = PortLifecycle::Unconfigured;
  /// Committed configuration. Absent while the port is UNCONFIGURED.
  std::optional<PortConfiguration> configuration;
  /// Current control ownership. Absent when the port is unowned.
  std::optional<PortOwnership> ownership;
  /// Bound capability evidence.
  CapabilityBinding capability;
  /// Evidence about physical application.
  AppliedEvidence applied;
  /// Drift between desired and observed state.
  DriftState drift = DriftState::Unknown;
  /// Generations that apply to this record.
  GenerationVector generations;
  /// Provenance of the last authoritative change.
  Provenance provenance;
  /// Last committed mutation identity and payload digest.
  LastMutationRecord last_mutation;
  /// Stable reason code describing why the record is in its current lifecycle.
  std::string status_reason;
  /// Bounded human readable detail for the status reason.
  std::string status_detail;

  bool validate(std::string& error) const;

  /// True when the record carries an authoritative configuration generation.
  bool configured() const noexcept { return configuration.has_value(); }

  std::string to_string() const;
};

}  // namespace portfabric
