#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "portfabric/characteristics.hpp"
#include "portfabric/export.hpp"
#include "portfabric/generation.hpp"
#include "portfabric/ids.hpp"
#include "portfabric/limits.hpp"
#include "portfabric/outcome.hpp"

namespace portfabric {

/// How much is actually known about a port's capabilities.
enum class CapabilityCertainty : std::uint8_t {
  /// No usable capability evidence exists. Every configuration that depends on
  /// capability proof fails closed against UNKNOWN.
  Unknown = 0,
  /// Complete capability evidence exists for the dimensions the port exposes.
  Known,
  /// Some dimensions are known and others are not. A dimension that is not
  /// present in the evidence is treated as UNKNOWN, never as supported.
  Partial,
};

PF_EXPORT std::string_view to_string(CapabilityCertainty certainty) noexcept;
PF_EXPORT bool parse_capability_certainty(std::string_view text, CapabilityCertainty& out) noexcept;
PF_EXPORT bool is_valid(CapabilityCertainty certainty) noexcept;

/// Capability evidence for one port.
///
/// Port Fabric does not own broader canonical capability truth: Fabric Capability
/// Registry does. A PortCapabilities value is the evidence this runtime is
/// currently allowed to bind a configuration against, together with the
/// generation that evidence belongs to.
class PF_EXPORT PortCapabilities {
 public:
  CapabilityCertainty certainty = CapabilityCertainty::Unknown;

  std::vector<PortSpeed> supported_speeds;
  /// Lane counts the port can be configured with.
  std::vector<std::uint32_t> lane_modes;
  std::uint32_t max_lanes = 0;
  bool breakout_supported = false;
  std::uint32_t max_breakout_children = 1;
  std::vector<FecMode> fec_modes;
  std::vector<AutonegPolicy> autoneg_modes;
  std::vector<PauseMode> pause_modes;
  std::vector<ProtocolFamily> protocol_families;
  std::uint32_t capability_mtu_min = 0;
  std::uint32_t capability_mtu_max = 0;
  DuplexMode duplex = DuplexMode::Unknown;
  bool logical_interface_supported = false;
  std::uint32_t max_queue_count = 0;
  bool transceiver_required = false;
  /// Vendor neutral transceiver class label. Empty means the port class does not
  /// expose a transceiver concept.
  std::string transceiver_class;

  /// Validates bounds and internal consistency. Never allocates beyond limits.
  bool validate(std::string& error) const;

  /// True when the evidence claims support for the given rate. UNKNOWN evidence
  /// never claims support.
  bool supports_speed(PortSpeed speed) const noexcept;
  bool supports_lanes(std::uint32_t lanes) const noexcept;
  bool supports_fec(FecMode mode) const noexcept;
  bool supports_autoneg(AutonegPolicy policy) const noexcept;
  bool supports_pause(PauseMode mode) const noexcept;
  bool supports_protocol(ProtocolFamily family) const noexcept;

  std::string to_string() const;
};

/// Capability evidence bound to one port, with the generation it belongs to.
struct PF_EXPORT CapabilityBinding {
  PortCapabilities capabilities;
  /// Generation of the bound capability evidence. Zero means nothing is bound.
  CapabilityBindingGeneration generation;
  EvidenceGeneration evidence_generation;
  EvidenceId evidence;
  /// Label of the provider that produced the evidence, e.g. "synthetic" or
  /// "windows-nic". Bounded, single-line text.
  std::string source;
  bool present = false;

  bool valid() const noexcept {
    return !present || (generation.valid() && evidence_generation.valid());
  }

  std::string to_string() const;
};

/// A source of capability evidence for a port.
///
/// Implementations must be safe to call concurrently and must never block
/// indefinitely. The returned binding is a value: Port Fabric never retains a
/// reference into provider owned storage.
class PF_EXPORT CapabilityProvider {
 public:
  CapabilityProvider() = default;
  virtual ~CapabilityProvider();

  CapabilityProvider(const CapabilityProvider&) = delete;
  CapabilityProvider& operator=(const CapabilityProvider&) = delete;

  /// Human readable provider label used as evidence provenance.
  virtual std::string_view label() const noexcept = 0;

  /// Returns the current capability evidence for a port. Returning nullopt means
  /// the provider cannot supply evidence; error carries the reason when set.
  virtual std::optional<CapabilityBinding> current(const PortId& port, std::string& error) = 0;
};

/// Result of validating a configuration against capability evidence.
struct PF_EXPORT CapabilityValidation {
  bool accepted = true;
  OutcomeCode code = OutcomeCode::Ok;
  /// Stable machine readable reason, e.g. "SPEED_UNSUPPORTED" or "BREAKOUT_UNKNOWN".
  std::string reason;
  /// Human readable detail naming the exact dimension that failed.
  std::string detail;

  static CapabilityValidation accept();
  static CapabilityValidation reject(OutcomeCode code, std::string reason, std::string detail);
};

PF_EXPORT std::string_view to_string(CapabilityValidation validation) noexcept;

}  // namespace portfabric
