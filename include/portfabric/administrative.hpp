#pragma once

#include <cstdint>
#include <string_view>

#include "portfabric/export.hpp"
#include "portfabric/lifecycle.hpp"

namespace portfabric {

/// Administrative state of a port.
///
/// Administrative state is intent owned by Port Fabric. It is never derived from
/// observed link condition and never overwritten by it: a port may be ENABLED
/// while Link State Fabric reports DOWN, and DISABLED while the carrier remains
/// observable.
enum class AdministrativeState : std::uint8_t {
  /// No administrative intent established. Configuration safety that depends on
  /// the administrative intent fails closed in this state.
  Unknown = 0,
  /// The port is administratively enabled.
  Enabled,
  /// The port is administratively disabled. This is not a link failure.
  Disabled,
  /// The port is draining: structurally present, no new dependent use.
  Draining,
  /// The port is in an operator maintenance window.
  Maintenance,
  /// The administrative state cannot be trusted until the port is revalidated.
  RevalidationRequired,
};

/// Requested administrative operation.
enum class AdministrativeOp : std::uint8_t {
  Enable = 0,
  Disable,
  Drain,
  EnterMaintenance,
  /// Leave draining or maintenance and return to Enabled.
  Resume,
  RequireRevalidation,
};

PF_EXPORT std::string_view to_string(AdministrativeState state) noexcept;
PF_EXPORT std::string_view to_string(AdministrativeOp op) noexcept;
PF_EXPORT bool parse_administrative_state(std::string_view text, AdministrativeState& out) noexcept;
PF_EXPORT bool parse_administrative_op(std::string_view text, AdministrativeOp& out) noexcept;
PF_EXPORT bool is_valid(AdministrativeState state) noexcept;
PF_EXPORT bool is_valid(AdministrativeOp op) noexcept;

/// Administrative state a successful operation establishes.
PF_EXPORT AdministrativeState administrative_target(AdministrativeOp op) noexcept;

/// Lifecycle transition that carries the administrative change.
///
/// Administrative state and lifecycle are separate dimensions, but a single
/// mutation keeps them consistent. This mapping is the single place where the
/// two dimensions are related.
PF_EXPORT LifecycleTransition lifecycle_transition_for(AdministrativeOp op) noexcept;

/// True when the administrative transition is meaningful from the current state.
PF_EXPORT bool administrative_transition_allowed(AdministrativeState from,
                                                 AdministrativeState to) noexcept;

/// Administrative state implied by a lifecycle state, used by recovery and by
/// reconciliation. Returns Unknown when the lifecycle carries no intent.
PF_EXPORT AdministrativeState administrative_state_of(PortLifecycle lifecycle) noexcept;

}  // namespace portfabric
