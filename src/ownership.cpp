#include "portfabric/ownership.hpp"

#include <array>

namespace portfabric {

std::string_view to_string(PortOwnerKind kind) noexcept {
  switch (kind) {
    case PortOwnerKind::Unknown:
      return "UNKNOWN";
    case PortOwnerKind::SwitchAgent:
      return "SWITCH_AGENT";
    case PortOwnerKind::HostAgent:
      return "HOST_AGENT";
    case PortOwnerKind::NetworkController:
      return "NETWORK_CONTROLLER";
    case PortOwnerKind::AdministrativeControlPlane:
      return "ADMINISTRATIVE_CONTROL_PLANE";
    case PortOwnerKind::DelegatedSubsystem:
      return "DELEGATED_SUBSYSTEM";
  }
  return "UNKNOWN";
}

bool parse_owner_kind(std::string_view text, PortOwnerKind& out) noexcept {
  static const std::array<std::pair<std::string_view, PortOwnerKind>, 6> table{{
      {"UNKNOWN", PortOwnerKind::Unknown},
      {"SWITCH_AGENT", PortOwnerKind::SwitchAgent},
      {"HOST_AGENT", PortOwnerKind::HostAgent},
      {"NETWORK_CONTROLLER", PortOwnerKind::NetworkController},
      {"ADMINISTRATIVE_CONTROL_PLANE", PortOwnerKind::AdministrativeControlPlane},
      {"DELEGATED_SUBSYSTEM", PortOwnerKind::DelegatedSubsystem}}};
  for (const auto& [rendering, value] : table) {
    if (rendering == text) {
      out = value;
      return true;
    }
  }
  return false;
}

bool is_valid(PortOwnerKind kind) noexcept {
  switch (kind) {
    case PortOwnerKind::Unknown:
    case PortOwnerKind::SwitchAgent:
    case PortOwnerKind::HostAgent:
    case PortOwnerKind::NetworkController:
    case PortOwnerKind::AdministrativeControlPlane:
    case PortOwnerKind::DelegatedSubsystem:
      return true;
  }
  return false;
}

std::string PortOwnership::to_string() const {
  if (!valid()) {
    return "unowned";
  }
  std::string out(portfabric::to_string(kind));
  out.push_back(':');
  out.append(owner.to_string());
  out.append("#");
  out.append(generation.to_string());
  out.append(" publisher=");
  out.append(publisher.to_string());
  out.append(" boot=");
  out.append(boot.to_string());
  out.append(" epoch=");
  out.append(epoch.to_string());
  out.append(exclusive ? " exclusive" : " shared");
  return out;
}

}  // namespace portfabric
