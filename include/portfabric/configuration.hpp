#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "portfabric/administrative.hpp"
#include "portfabric/authority.hpp"
#include "portfabric/capability.hpp"
#include "portfabric/characteristics.hpp"
#include "portfabric/export.hpp"
#include "portfabric/generation.hpp"
#include "portfabric/ids.hpp"

namespace portfabric {

/// Where a configuration decision came from.
enum class ProvenanceKind : std::uint8_t {
  Unknown = 0,
  Operator,
  NetworkController,
  SwitchAgent,
  HostAgent,
  DelegatedSubsystem,
  /// A synthetic hardware backend supplied the decision.
  SyntheticBackend,
  /// A local host adapter supplied the decision.
  HostAdapter,
  /// The runtime itself decided, during restart recovery.
  Recovery,
  /// The runtime bound current generations during explicit reconciliation.
  Reconciliation,
};

PF_EXPORT std::string_view to_string(ProvenanceKind kind) noexcept;
PF_EXPORT bool parse_provenance_kind(std::string_view text, ProvenanceKind& out) noexcept;
PF_EXPORT bool is_valid(ProvenanceKind kind) noexcept;

/// Provenance of the committed configuration generation.
struct PF_EXPORT Provenance {
  ProvenanceKind kind = ProvenanceKind::Unknown;
  /// Bounded, single-line label of the deciding component.
  std::string source;
  PublisherId publisher;
  WorkerBootId boot;
  CoordinatorEpoch epoch;
  MutationAttemptId attempt;

  bool valid() const noexcept {
    return is_valid(kind) && epoch.valid() && attempt.valid();
  }

  std::string to_string() const;
};

/// Profile assignment of a port configuration.
struct PF_EXPORT ProfileBinding {
  bool bound = false;
  PortProfileId id;
  PortProfileGeneration generation;

  bool valid() const noexcept {
    return bound ? (id.valid() && generation.valid()) : (!id.valid() && !generation.valid());
  }

  std::string to_string() const;

  friend bool operator==(const ProfileBinding& lhs, const ProfileBinding& rhs) noexcept {
    return lhs.bound == rhs.bound && lhs.id == rhs.id && lhs.generation == rhs.generation;
  }
  friend bool operator!=(const ProfileBinding& lhs, const ProfileBinding& rhs) noexcept {
    return !(lhs == rhs);
  }
};

/// Authoritative committed configuration of one port.
///
/// This is the requested, validated and committed configuration. It is not a
/// claim that the hardware has been changed: applied evidence is tracked
/// separately by the port record.
class PF_EXPORT PortConfiguration {
 public:
  /// Port Fabric record identity for this configuration. The canonical identity
  /// of the port itself stays in PortId.
  PortConfigurationId record;
  PortId port;
  DeviceId parent_device;
  DeviceGeneration device_generation;
  TopologyGeneration topology_generation;
  PortConfigurationGeneration generation;

  AdministrativeState administrative = AdministrativeState::Unknown;

  PortMode mode = PortMode::Physical;
  ProtocolFamily protocol = ProtocolFamily::Unknown;

  SpeedSetting speed;
  DuplexMode duplex = DuplexMode::Unknown;
  Mtu mtu;
  LaneConfiguration lanes;
  BreakoutMode breakout = BreakoutMode::None;
  AutonegPolicy autoneg = AutonegPolicy::Unknown;
  FecMode fec = FecMode::Unknown;
  PauseMode pause = PauseMode::Unknown;
  AggregationMembership aggregation;
  ProfileBinding profile;

  /// Local role label. Vendor neutral free text, bounded and validated.
  std::string role;

  /// Logical derivation. Empty for physical ports.
  LogicalPortId logical_identity;
  PortId derived_from;
  /// One-based child index inside a breakout; zero for a non-child port.
  std::uint32_t child_index = 0;

  Provenance provenance;
  CapabilityBindingGeneration capability_generation;
  EvidenceGeneration evidence_generation;

  /// Structural and semantic validation. Returns false with a bounded
  /// explanation when any dimension is inconsistent.
  bool validate(std::string& error) const;

  /// Total configured rate derived from the lane configuration, when valid.
  std::optional<PortSpeed> total_speed() const;

  /// Deterministic single-line rendering. Field order is part of the contract.
  std::string to_string() const;

  /// Ordered canonical field list used by digesting and diffing.
  std::vector<std::pair<std::string, std::string>> canonical_fields() const;
};

/// Validates a configuration against bound capability evidence.
///
/// The rule is fail-closed: a dimension that the evidence does not positively
/// support is rejected. UNKNOWN evidence therefore rejects every configuration
/// that depends on it instead of assuming support.
PF_EXPORT CapabilityValidation validate_against_capabilities(const PortConfiguration& configuration,
                                                             const CapabilityBinding& binding);

}  // namespace portfabric
