#include "portfabric/ids.hpp"

#include <utility>

namespace portfabric {
namespace {

bool is_alphanumeric(char ch) noexcept {
  return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool is_interior(char ch) noexcept {
  return is_alphanumeric(ch) || ch == '-' || ch == '_' || ch == '.' || ch == ':' || ch == '/';
}

}  // namespace

std::string_view to_string(IdValidation validation) noexcept {
  switch (validation) {
    case IdValidation::Ok:
      return "OK";
    case IdValidation::Empty:
      return "EMPTY";
    case IdValidation::TooLong:
      return "TOO_LONG";
    case IdValidation::InvalidCharacter:
      return "INVALID_CHARACTER";
    case IdValidation::LeadingSeparator:
      return "LEADING_SEPARATOR";
    case IdValidation::TrailingSeparator:
      return "TRAILING_SEPARATOR";
    case IdValidation::DotDot:
      return "DOT_DOT";
  }
  return "INVALID_CHARACTER";
}

std::string_view to_string(PortEntityClass entity_class) noexcept {
  switch (entity_class) {
    case PortEntityClass::Unknown:
      return "UNKNOWN";
    case PortEntityClass::SwitchPort:
      return "SWITCH_PORT";
    case PortEntityClass::RouterPort:
      return "ROUTER_PORT";
    case PortEntityClass::NicPort:
      return "NIC_PORT";
    case PortEntityClass::SmartNicPort:
      return "SMARTNIC_PORT";
    case PortEntityClass::DpuPort:
      return "DPU_PORT";
    case PortEntityClass::LogicalPort:
      return "LOGICAL_PORT";
    case PortEntityClass::SyntheticPort:
      return "SYNTHETIC_PORT";
  }
  return "UNKNOWN";
}

bool parse_entity_class(std::string_view text, PortEntityClass& out) noexcept {
  const std::pair<std::string_view, PortEntityClass> table[] = {
      {"UNKNOWN", PortEntityClass::Unknown},
      {"SWITCH_PORT", PortEntityClass::SwitchPort},
      {"ROUTER_PORT", PortEntityClass::RouterPort},
      {"NIC_PORT", PortEntityClass::NicPort},
      {"SMARTNIC_PORT", PortEntityClass::SmartNicPort},
      {"DPU_PORT", PortEntityClass::DpuPort},
      {"LOGICAL_PORT", PortEntityClass::LogicalPort},
      {"SYNTHETIC_PORT", PortEntityClass::SyntheticPort}};
  for (const auto& [rendering, value] : table) {
    if (rendering == text) {
      out = value;
      return true;
    }
  }
  return false;
}

bool is_valid(PortEntityClass entity_class) noexcept {
  switch (entity_class) {
    case PortEntityClass::Unknown:
    case PortEntityClass::SwitchPort:
    case PortEntityClass::RouterPort:
    case PortEntityClass::NicPort:
    case PortEntityClass::SmartNicPort:
    case PortEntityClass::DpuPort:
    case PortEntityClass::LogicalPort:
    case PortEntityClass::SyntheticPort:
      return true;
  }
  return false;
}

IdValidation validate_identifier(std::string_view value) noexcept {
  if (value.empty()) {
    return IdValidation::Empty;
  }
  if (value.size() > max_identifier_length) {
    return IdValidation::TooLong;
  }
  // The traversal sequence is reported ahead of the separator rules: an encoded
  // identifier can never be interpreted as a relative path, and ".." must be
  // named as traversal rather than as a separator problem.
  for (std::size_t index = 0; index + 1 < value.size(); ++index) {
    if (value[index] == '.' && value[index + 1] == '.') {
      return IdValidation::DotDot;
    }
  }
  if (!is_alphanumeric(value.front())) {
    return IdValidation::LeadingSeparator;
  }
  if (!is_alphanumeric(value.back())) {
    return IdValidation::TrailingSeparator;
  }
  for (const char ch : value) {
    if (!is_interior(ch)) {
      return IdValidation::InvalidCharacter;
    }
  }
  return IdValidation::Ok;
}

}  // namespace portfabric
