#pragma once

#include <string>
#include <string_view>

#include "portfabric/capability.hpp"
#include "portfabric/configuration.hpp"
#include "portfabric/export.hpp"
#include "portfabric/ids.hpp"

namespace portfabric {

/// Result of asking an adapter to apply a configuration to a device.
enum class AdapterApplyStatus : std::uint8_t {
  /// The adapter completed the change and the device acknowledged it.
  Applied = 0,
  /// The adapter reported a definite failure: the device did not change.
  Failed,
  /// The completion of the request is unknown: the connection dropped, the
  /// device disappeared, or an acknowledgement was lost. The runtime must not
  /// retry blindly and must not claim success.
  OutcomeUnknown,
  /// The adapter does not implement application for this port class.
  Unsupported,
};

PF_EXPORT std::string_view to_string(AdapterApplyStatus status) noexcept;
PF_EXPORT bool parse_adapter_apply_status(std::string_view text, AdapterApplyStatus& out) noexcept;

/// Result of an adapter apply request.
struct PF_EXPORT AdapterApplyResult {
  AdapterApplyStatus status = AdapterApplyStatus::Unsupported;
  /// Bounded single-line detail from the adapter.
  std::string detail;

  static AdapterApplyResult applied(std::string detail);
  static AdapterApplyResult failed(std::string detail);
  static AdapterApplyResult unknown(std::string detail);
  static AdapterApplyResult unsupported(std::string detail);
};

/// Result of reading back a device's actual configuration.
enum class AdapterReadbackStatus : std::uint8_t {
  /// The device matches the requested configuration.
  Verified = 0,
  /// The device is reachable but does not match.
  Mismatch,
  /// The device state could not be determined.
  Unknown,
  /// The adapter does not implement readback for this port class.
  Unsupported,
};

PF_EXPORT std::string_view to_string(AdapterReadbackStatus status) noexcept;
PF_EXPORT bool parse_adapter_readback_status(std::string_view text, AdapterReadbackStatus& out) noexcept;

/// Result of an adapter readback.
struct PF_EXPORT AdapterReadback {
  AdapterReadbackStatus status = AdapterReadbackStatus::Unsupported;
  /// Observed configuration when the adapter can report it. Meaningful only when
  /// status is Mismatch or Verified.
  PortConfiguration observed;
  std::string detail;

  static AdapterReadback verified(PortConfiguration observed, std::string detail);
  static AdapterReadback mismatch(PortConfiguration observed, std::string detail);
  static AdapterReadback unknown(std::string detail);
  static AdapterReadback unsupported(std::string detail);
};

/// Boundary between port semantics and hardware-specific control.
///
/// An adapter performs real device changes. The core runtime never contains
/// vendor-specific knowledge: everything device specific lives behind this
/// interface. Adapter calls are always made with no engine lock held, so an
/// adapter may block without stalling unrelated ports.
class PF_EXPORT PortAdapter {
 public:
  PortAdapter() = default;
  virtual ~PortAdapter();

  PortAdapter(const PortAdapter&) = delete;
  PortAdapter& operator=(const PortAdapter&) = delete;

  /// Bounded, single-line adapter label used as evidence provenance.
  virtual std::string_view label() const noexcept = 0;

  /// Applies a configuration generation to the device.
  virtual AdapterApplyResult apply(const PortId& port, const PortConfiguration& configuration) = 0;

  /// Reads the device's actual configuration back.
  virtual AdapterReadback readback(const PortId& port) = 0;
};

}  // namespace portfabric
