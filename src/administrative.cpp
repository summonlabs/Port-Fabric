#include "portfabric/administrative.hpp"

#include <array>

namespace portfabric {

std::string_view to_string(AdministrativeState state) noexcept {
  switch (state) {
    case AdministrativeState::Unknown:
      return "UNKNOWN";
    case AdministrativeState::Enabled:
      return "ENABLED";
    case AdministrativeState::Disabled:
      return "DISABLED";
    case AdministrativeState::Draining:
      return "DRAINING";
    case AdministrativeState::Maintenance:
      return "MAINTENANCE";
    case AdministrativeState::RevalidationRequired:
      return "REVALIDATION_REQUIRED";
  }
  return "UNKNOWN";
}

std::string_view to_string(AdministrativeOp op) noexcept {
  switch (op) {
    case AdministrativeOp::Enable:
      return "ENABLE";
    case AdministrativeOp::Disable:
      return "DISABLE";
    case AdministrativeOp::Drain:
      return "DRAIN";
    case AdministrativeOp::EnterMaintenance:
      return "ENTER_MAINTENANCE";
    case AdministrativeOp::Resume:
      return "RESUME";
    case AdministrativeOp::RequireRevalidation:
      return "REQUIRE_REVALIDATION";
  }
  return "UNKNOWN";
}

bool parse_administrative_state(std::string_view text, AdministrativeState& out) noexcept {
  static const std::array<std::pair<std::string_view, AdministrativeState>, 6> table{{
      {"UNKNOWN", AdministrativeState::Unknown},
      {"ENABLED", AdministrativeState::Enabled},
      {"DISABLED", AdministrativeState::Disabled},
      {"DRAINING", AdministrativeState::Draining},
      {"MAINTENANCE", AdministrativeState::Maintenance},
      {"REVALIDATION_REQUIRED", AdministrativeState::RevalidationRequired}}};
  for (const auto& [rendering, value] : table) {
    if (rendering == text) {
      out = value;
      return true;
    }
  }
  return false;
}

bool parse_administrative_op(std::string_view text, AdministrativeOp& out) noexcept {
  static const std::array<std::pair<std::string_view, AdministrativeOp>, 6> table{{
      {"ENABLE", AdministrativeOp::Enable},
      {"DISABLE", AdministrativeOp::Disable},
      {"DRAIN", AdministrativeOp::Drain},
      {"ENTER_MAINTENANCE", AdministrativeOp::EnterMaintenance},
      {"RESUME", AdministrativeOp::Resume},
      {"REQUIRE_REVALIDATION", AdministrativeOp::RequireRevalidation}}};
  for (const auto& [rendering, value] : table) {
    if (rendering == text) {
      out = value;
      return true;
    }
  }
  return false;
}

bool is_valid(AdministrativeState state) noexcept {
  switch (state) {
    case AdministrativeState::Unknown:
    case AdministrativeState::Enabled:
    case AdministrativeState::Disabled:
    case AdministrativeState::Draining:
    case AdministrativeState::Maintenance:
    case AdministrativeState::RevalidationRequired:
      return true;
  }
  return false;
}

bool is_valid(AdministrativeOp op) noexcept {
  switch (op) {
    case AdministrativeOp::Enable:
    case AdministrativeOp::Disable:
    case AdministrativeOp::Drain:
    case AdministrativeOp::EnterMaintenance:
    case AdministrativeOp::Resume:
    case AdministrativeOp::RequireRevalidation:
      return true;
  }
  return false;
}

AdministrativeState administrative_target(AdministrativeOp op) noexcept {
  switch (op) {
    case AdministrativeOp::Enable:
      return AdministrativeState::Enabled;
    case AdministrativeOp::Disable:
      return AdministrativeState::Disabled;
    case AdministrativeOp::Drain:
      return AdministrativeState::Draining;
    case AdministrativeOp::EnterMaintenance:
      return AdministrativeState::Maintenance;
    case AdministrativeOp::Resume:
      return AdministrativeState::Enabled;
    case AdministrativeOp::RequireRevalidation:
      return AdministrativeState::RevalidationRequired;
  }
  return AdministrativeState::Unknown;
}

LifecycleTransition lifecycle_transition_for(AdministrativeOp op) noexcept {
  switch (op) {
    case AdministrativeOp::Enable:
      return LifecycleTransition::Activate;
    case AdministrativeOp::Disable:
      return LifecycleTransition::AdminDisable;
    case AdministrativeOp::Drain:
      return LifecycleTransition::BeginDrain;
    case AdministrativeOp::EnterMaintenance:
      return LifecycleTransition::EnterMaintenance;
    case AdministrativeOp::Resume:
      // Leaving draining or maintenance returns the port to active service.
      return LifecycleTransition::Activate;
    case AdministrativeOp::RequireRevalidation:
      return LifecycleTransition::RequireRevalidation;
  }
  return LifecycleTransition::RequireRevalidation;
}

bool administrative_transition_allowed(AdministrativeState from,
                                       AdministrativeState to) noexcept {
  if (!is_valid(from) || !is_valid(to)) {
    return false;
  }
  if (from == to) {
    return true;
  }
  switch (to) {
    case AdministrativeState::Enabled:
      return from != AdministrativeState::Unknown;
    case AdministrativeState::Disabled:
      return from != AdministrativeState::Unknown;
    case AdministrativeState::Draining:
      return from == AdministrativeState::Enabled || from == AdministrativeState::Maintenance;
    case AdministrativeState::Maintenance:
      return from == AdministrativeState::Enabled || from == AdministrativeState::Draining ||
             from == AdministrativeState::Disabled;
    case AdministrativeState::RevalidationRequired:
      return true;
    case AdministrativeState::Unknown:
      return false;
  }
  return false;
}

AdministrativeState administrative_state_of(PortLifecycle lifecycle) noexcept {
  switch (lifecycle) {
    case PortLifecycle::Configured:
    case PortLifecycle::Active:
      return AdministrativeState::Enabled;
    case PortLifecycle::AdminDisabled:
      return AdministrativeState::Disabled;
    case PortLifecycle::Draining:
      return AdministrativeState::Draining;
    case PortLifecycle::Maintenance:
      return AdministrativeState::Maintenance;
    case PortLifecycle::RevalidationRequired:
      return AdministrativeState::RevalidationRequired;
    case PortLifecycle::Unconfigured:
    case PortLifecycle::Superseded:
    case PortLifecycle::Retired:
    case PortLifecycle::Conflicted:
      return AdministrativeState::Unknown;
  }
  return AdministrativeState::Unknown;
}

}  // namespace portfabric
