#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "portfabric/export.hpp"

namespace portfabric {

/// Port lifecycle: what Port Fabric knows about the port as a governed,
/// generation-bound infrastructure object.
///
/// The lifecycle deliberately contains no operational link condition. A port in
/// ACTIVE may be operationally DOWN, and a port in ADMIN_DISABLED may have an
/// observable carrier. Link condition belongs to Link State Fabric.
enum class PortLifecycle : std::uint8_t {
  /// The canonical port identity is known to Port Fabric and no configuration
  /// generation has been committed for it.
  Unconfigured = 0,
  /// A configuration generation is committed and authoritative.
  Configured,
  /// The committed configuration is active for traffic admission purposes.
  Active,
  /// The committed configuration is administratively disabled.
  AdminDisabled,
  /// The port is draining: it exists structurally, existing port-local
  /// obligations transition away and new dependent use must be prevented.
  Draining,
  /// The port is in an operator maintenance window.
  Maintenance,
  /// The committed configuration can no longer be trusted and must be
  /// revalidated against current capability, topology and device evidence.
  RevalidationRequired,
  /// The configuration generation has been superseded by a newer device,
  /// topology or replacement generation and is fenced from live mutation.
  Superseded,
  /// The port is retired. Retirement is terminal: a retired port is never
  /// resurrected, only explicitly re-registered as a new record by Fabric
  /// Registry and a fresh configuration.
  Retired,
  /// Two authorities disagree about this port and neither may proceed until the
  /// conflict is resolved explicitly.
  Conflicted,
};

/// A requested lifecycle change.
enum class LifecycleTransition : std::uint8_t {
  /// UNCONFIGURED -> CONFIGURED: commit the first configuration generation.
  Configure = 0,
  /// CONFIGURED/ADMIN_DISABLED/DRAINING/MAINTENANCE -> ACTIVE.
  Activate,
  /// live -> ADMIN_DISABLED.
  AdminDisable,
  /// ADMIN_DISABLED -> ACTIVE or CONFIGURED.
  AdminEnable,
  /// live -> DRAINING.
  BeginDrain,
  /// DRAINING -> MAINTENANCE.
  CompleteDrain,
  /// live -> MAINTENANCE (an operator window opened directly).
  EnterMaintenance,
  /// MAINTENANCE/DRAINING -> ACTIVE or CONFIGURED.
  ExitMaintenance,
  /// live -> REVALIDATION_REQUIRED.
  RequireRevalidation,
  /// non-retired -> SUPERSEDED.
  Supersede,
  /// non-retired -> RETIRED.
  Retire,
  /// non-retired -> CONFLICTED.
  Conflict,
  /// REVALIDATION_REQUIRED/SUPERSEDED/CONFLICTED -> CONFIGURED or ACTIVE.
  Reconcile,
};

/// Outcome of evaluating a transition against a lifecycle state.
struct PF_EXPORT TransitionEvaluation {
  bool allowed = false;
  /// The resulting lifecycle state when allowed; the unchanged state otherwise.
  PortLifecycle result = PortLifecycle::Unconfigured;
  /// Stable reason code, e.g. "TERMINAL_LIFECYCLE" or "TRANSITION_NOT_DEFINED".
  std::string_view reason;
};

/// Evaluates a lifecycle transition deterministically.
///
/// The transition relation is total: every (state, transition) pair evaluates to
/// either an allowed transition with an exact result or a rejection with a
/// stable reason. There are no implicit transitions.
PF_EXPORT TransitionEvaluation evaluate_transition(PortLifecycle current,
                                                   LifecycleTransition transition) noexcept;

/// Evaluates a state-to-state transition, used by adapters and recovery paths.
PF_EXPORT TransitionEvaluation evaluate_transition(PortLifecycle current,
                                                   PortLifecycle target) noexcept;

/// True when the state is a live governed state (a configuration generation is
/// committed and the port is neither superseded nor retired nor conflicted).
PF_EXPORT bool is_live(PortLifecycle state) noexcept;

/// True when the state is terminal for mutation purposes (RETIRED only).
PF_EXPORT bool is_terminal(PortLifecycle state) noexcept;

/// True when the state permits mutation without an explicit reconciliation.
PF_EXPORT bool is_mutable(PortLifecycle state) noexcept;

/// True when the state requires explicit reconciliation before further mutation.
PF_EXPORT bool requires_reconciliation(PortLifecycle state) noexcept;

PF_EXPORT std::string_view to_string(PortLifecycle state) noexcept;
PF_EXPORT std::string_view to_string(LifecycleTransition transition) noexcept;
PF_EXPORT bool parse_lifecycle(std::string_view text, PortLifecycle& out) noexcept;
PF_EXPORT bool parse_transition(std::string_view text, LifecycleTransition& out) noexcept;
PF_EXPORT bool is_valid(PortLifecycle state) noexcept;
PF_EXPORT bool is_valid(LifecycleTransition transition) noexcept;

}  // namespace portfabric
