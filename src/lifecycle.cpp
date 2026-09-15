#include "portfabric/lifecycle.hpp"

#include <array>

namespace portfabric {
namespace {

TransitionEvaluation reject(PortLifecycle current, std::string_view reason) noexcept {
  TransitionEvaluation evaluation;
  evaluation.allowed = false;
  evaluation.result = current;
  evaluation.reason = reason;
  return evaluation;
}

TransitionEvaluation allow(PortLifecycle result) noexcept {
  TransitionEvaluation evaluation;
  evaluation.allowed = true;
  evaluation.result = result;
  evaluation.reason = "OK";
  return evaluation;
}

/// The lifecycle state a port lands in when a configuration generation is
/// committed while the administrative state is Enabled.
constexpr PortLifecycle kEnabledLanding = PortLifecycle::Configured;

}  // namespace

bool is_live(PortLifecycle state) noexcept {
  switch (state) {
    case PortLifecycle::Configured:
    case PortLifecycle::Active:
    case PortLifecycle::AdminDisabled:
    case PortLifecycle::Draining:
    case PortLifecycle::Maintenance:
      return true;
    case PortLifecycle::Unconfigured:
    case PortLifecycle::RevalidationRequired:
    case PortLifecycle::Superseded:
    case PortLifecycle::Retired:
    case PortLifecycle::Conflicted:
      return false;
  }
  return false;
}

bool is_terminal(PortLifecycle state) noexcept { return state == PortLifecycle::Retired; }

bool is_mutable(PortLifecycle state) noexcept {
  return is_live(state) || state == PortLifecycle::Unconfigured;
}

bool requires_reconciliation(PortLifecycle state) noexcept {
  return state == PortLifecycle::RevalidationRequired || state == PortLifecycle::Superseded ||
         state == PortLifecycle::Conflicted;
}

TransitionEvaluation evaluate_transition(PortLifecycle current,
                                         LifecycleTransition transition) noexcept {
  if (!is_valid(current)) {
    return reject(current, "INVALID_LIFECYCLE_STATE");
  }
  if (!is_valid(transition)) {
    return reject(current, "INVALID_LIFECYCLE_TRANSITION");
  }
  if (current == PortLifecycle::Retired) {
    return reject(current, "TERMINAL_LIFECYCLE");
  }
  switch (transition) {
    case LifecycleTransition::Configure:
      if (current == PortLifecycle::Unconfigured) {
        return allow(kEnabledLanding);
      }
      return reject(current, "CONFIGURATION_ALREADY_EXISTS");
    case LifecycleTransition::Activate:
      switch (current) {
        case PortLifecycle::Configured:
        case PortLifecycle::AdminDisabled:
        case PortLifecycle::Draining:
        case PortLifecycle::Maintenance:
          return allow(PortLifecycle::Active);
        case PortLifecycle::Active:
          return allow(PortLifecycle::Active);
        default:
          return reject(current, "ACTIVATION_REQUIRES_CONFIGURATION");
      }
    case LifecycleTransition::AdminDisable:
      if (is_live(current)) {
        return allow(PortLifecycle::AdminDisabled);
      }
      return reject(current, "ADMIN_DISABLE_REQUIRES_LIVE_CONFIGURATION");
    case LifecycleTransition::AdminEnable:
      if (current == PortLifecycle::AdminDisabled) {
        return allow(PortLifecycle::Configured);
      }
      return reject(current, "ENABLE_REQUIRES_DISABLED_STATE");
    case LifecycleTransition::BeginDrain:
      if (is_live(current)) {
        return allow(PortLifecycle::Draining);
      }
      return reject(current, "DRAIN_REQUIRES_LIVE_CONFIGURATION");
    case LifecycleTransition::CompleteDrain:
      if (current == PortLifecycle::Draining) {
        return allow(PortLifecycle::Maintenance);
      }
      return reject(current, "DRAIN_COMPLETION_REQUIRES_DRAINING_STATE");
    case LifecycleTransition::EnterMaintenance:
      if (is_live(current)) {
        return allow(PortLifecycle::Maintenance);
      }
      return reject(current, "MAINTENANCE_REQUIRES_LIVE_CONFIGURATION");
    case LifecycleTransition::ExitMaintenance:
      if (current == PortLifecycle::Maintenance || current == PortLifecycle::Draining) {
        return allow(kEnabledLanding);
      }
      return reject(current, "EXIT_MAINTENANCE_REQUIRES_MAINTENANCE_STATE");
    case LifecycleTransition::RequireRevalidation:
      if (is_live(current) || current == PortLifecycle::Unconfigured) {
        return allow(PortLifecycle::RevalidationRequired);
      }
      if (current == PortLifecycle::RevalidationRequired) {
        return allow(PortLifecycle::RevalidationRequired);
      }
      return reject(current, "REVALIDATION_NOT_APPLICABLE");
    case LifecycleTransition::Supersede:
      if (current == PortLifecycle::Superseded) {
        return reject(current, "ALREADY_SUPERSEDED");
      }
      return allow(PortLifecycle::Superseded);
    case LifecycleTransition::Retire:
      return allow(PortLifecycle::Retired);
    case LifecycleTransition::Conflict:
      if (current == PortLifecycle::Conflicted) {
        return reject(current, "ALREADY_CONFLICTED");
      }
      return allow(PortLifecycle::Conflicted);
    case LifecycleTransition::Reconcile:
      if (requires_reconciliation(current)) {
        return allow(PortLifecycle::Configured);
      }
      return reject(current, "RECONCILIATION_NOT_REQUIRED");
  }
  return reject(current, "TRANSITION_NOT_DEFINED");
}

TransitionEvaluation evaluate_transition(PortLifecycle current, PortLifecycle target) noexcept {
  if (!is_valid(target)) {
    return reject(current, "INVALID_LIFECYCLE_STATE");
  }
  if (current == target) {
    // A self transition is not a transition: it never advances a generation.
    return reject(current, "ALREADY_IN_STATE");
  }
  switch (target) {
    case PortLifecycle::Configured:
      if (current == PortLifecycle::Unconfigured) {
        return allow(target);
      }
      if (current == PortLifecycle::AdminDisabled || current == PortLifecycle::Draining ||
          current == PortLifecycle::Maintenance) {
        return allow(target);
      }
      if (requires_reconciliation(current)) {
        return allow(target);
      }
      return reject(current, "TRANSITION_NOT_DEFINED");
    case PortLifecycle::Active:
      return evaluate_transition(current, LifecycleTransition::Activate);
    case PortLifecycle::AdminDisabled:
      return evaluate_transition(current, LifecycleTransition::AdminDisable);
    case PortLifecycle::Draining:
      return evaluate_transition(current, LifecycleTransition::BeginDrain);
    case PortLifecycle::Maintenance:
      return evaluate_transition(current, LifecycleTransition::EnterMaintenance);
    case PortLifecycle::RevalidationRequired:
      return evaluate_transition(current, LifecycleTransition::RequireRevalidation);
    case PortLifecycle::Superseded:
      return evaluate_transition(current, LifecycleTransition::Supersede);
    case PortLifecycle::Retired:
      return evaluate_transition(current, LifecycleTransition::Retire);
    case PortLifecycle::Conflicted:
      return evaluate_transition(current, LifecycleTransition::Conflict);
    case PortLifecycle::Unconfigured:
      return reject(current, "TRANSITION_TO_UNCONFIGURED_NOT_DEFINED");
  }
  return reject(current, "TRANSITION_NOT_DEFINED");
}

std::string_view to_string(PortLifecycle state) noexcept {
  switch (state) {
    case PortLifecycle::Unconfigured:
      return "UNCONFIGURED";
    case PortLifecycle::Configured:
      return "CONFIGURED";
    case PortLifecycle::Active:
      return "ACTIVE";
    case PortLifecycle::AdminDisabled:
      return "ADMIN_DISABLED";
    case PortLifecycle::Draining:
      return "DRAINING";
    case PortLifecycle::Maintenance:
      return "MAINTENANCE";
    case PortLifecycle::RevalidationRequired:
      return "REVALIDATION_REQUIRED";
    case PortLifecycle::Superseded:
      return "SUPERSEDED";
    case PortLifecycle::Retired:
      return "RETIRED";
    case PortLifecycle::Conflicted:
      return "CONFLICTED";
  }
  return "UNKNOWN";
}

std::string_view to_string(LifecycleTransition transition) noexcept {
  switch (transition) {
    case LifecycleTransition::Configure:
      return "CONFIGURE";
    case LifecycleTransition::Activate:
      return "ACTIVATE";
    case LifecycleTransition::AdminDisable:
      return "ADMIN_DISABLE";
    case LifecycleTransition::AdminEnable:
      return "ADMIN_ENABLE";
    case LifecycleTransition::BeginDrain:
      return "BEGIN_DRAIN";
    case LifecycleTransition::CompleteDrain:
      return "COMPLETE_DRAIN";
    case LifecycleTransition::EnterMaintenance:
      return "ENTER_MAINTENANCE";
    case LifecycleTransition::ExitMaintenance:
      return "EXIT_MAINTENANCE";
    case LifecycleTransition::RequireRevalidation:
      return "REQUIRE_REVALIDATION";
    case LifecycleTransition::Supersede:
      return "SUPERSEDE";
    case LifecycleTransition::Retire:
      return "RETIRE";
    case LifecycleTransition::Conflict:
      return "CONFLICT";
    case LifecycleTransition::Reconcile:
      return "RECONCILE";
  }
  return "UNKNOWN";
}

bool parse_lifecycle(std::string_view text, PortLifecycle& out) noexcept {
  static const std::array<std::pair<std::string_view, PortLifecycle>, 10> table{{
      {"UNCONFIGURED", PortLifecycle::Unconfigured},
      {"CONFIGURED", PortLifecycle::Configured},
      {"ACTIVE", PortLifecycle::Active},
      {"ADMIN_DISABLED", PortLifecycle::AdminDisabled},
      {"DRAINING", PortLifecycle::Draining},
      {"MAINTENANCE", PortLifecycle::Maintenance},
      {"REVALIDATION_REQUIRED", PortLifecycle::RevalidationRequired},
      {"SUPERSEDED", PortLifecycle::Superseded},
      {"RETIRED", PortLifecycle::Retired},
      {"CONFLICTED", PortLifecycle::Conflicted}}};
  for (const auto& [rendering, value] : table) {
    if (rendering == text) {
      out = value;
      return true;
    }
  }
  return false;
}

bool parse_transition(std::string_view text, LifecycleTransition& out) noexcept {
  static const std::array<std::pair<std::string_view, LifecycleTransition>, 13> table{{
      {"CONFIGURE", LifecycleTransition::Configure},
      {"ACTIVATE", LifecycleTransition::Activate},
      {"ADMIN_DISABLE", LifecycleTransition::AdminDisable},
      {"ADMIN_ENABLE", LifecycleTransition::AdminEnable},
      {"BEGIN_DRAIN", LifecycleTransition::BeginDrain},
      {"COMPLETE_DRAIN", LifecycleTransition::CompleteDrain},
      {"ENTER_MAINTENANCE", LifecycleTransition::EnterMaintenance},
      {"EXIT_MAINTENANCE", LifecycleTransition::ExitMaintenance},
      {"REQUIRE_REVALIDATION", LifecycleTransition::RequireRevalidation},
      {"SUPERSEDE", LifecycleTransition::Supersede},
      {"RETIRE", LifecycleTransition::Retire},
      {"CONFLICT", LifecycleTransition::Conflict},
      {"RECONCILE", LifecycleTransition::Reconcile}}};
  for (const auto& [rendering, value] : table) {
    if (rendering == text) {
      out = value;
      return true;
    }
  }
  return false;
}

bool is_valid(PortLifecycle state) noexcept {
  switch (state) {
    case PortLifecycle::Unconfigured:
    case PortLifecycle::Configured:
    case PortLifecycle::Active:
    case PortLifecycle::AdminDisabled:
    case PortLifecycle::Draining:
    case PortLifecycle::Maintenance:
    case PortLifecycle::RevalidationRequired:
    case PortLifecycle::Superseded:
    case PortLifecycle::Retired:
    case PortLifecycle::Conflicted:
      return true;
  }
  return false;
}

bool is_valid(LifecycleTransition transition) noexcept {
  switch (transition) {
    case LifecycleTransition::Configure:
    case LifecycleTransition::Activate:
    case LifecycleTransition::AdminDisable:
    case LifecycleTransition::AdminEnable:
    case LifecycleTransition::BeginDrain:
    case LifecycleTransition::CompleteDrain:
    case LifecycleTransition::EnterMaintenance:
    case LifecycleTransition::ExitMaintenance:
    case LifecycleTransition::RequireRevalidation:
    case LifecycleTransition::Supersede:
    case LifecycleTransition::Retire:
    case LifecycleTransition::Conflict:
    case LifecycleTransition::Reconcile:
      return true;
  }
  return false;
}

}  // namespace portfabric
