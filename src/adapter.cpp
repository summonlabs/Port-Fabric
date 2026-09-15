#include "portfabric/adapter.hpp"

namespace portfabric {

std::string_view to_string(AdapterApplyStatus status) noexcept {
  switch (status) {
    case AdapterApplyStatus::Applied:
      return "APPLIED";
    case AdapterApplyStatus::Failed:
      return "FAILED";
    case AdapterApplyStatus::OutcomeUnknown:
      return "OUTCOME_UNKNOWN";
    case AdapterApplyStatus::Unsupported:
      return "UNSUPPORTED";
  }
  return "UNSUPPORTED";
}

bool parse_adapter_apply_status(std::string_view text, AdapterApplyStatus& out) noexcept {
  const std::pair<std::string_view, AdapterApplyStatus> table[] = {
      {"APPLIED", AdapterApplyStatus::Applied},
      {"FAILED", AdapterApplyStatus::Failed},
      {"OUTCOME_UNKNOWN", AdapterApplyStatus::OutcomeUnknown},
      {"UNSUPPORTED", AdapterApplyStatus::Unsupported}};
  for (const auto& [rendering, value] : table) {
    if (rendering == text) {
      out = value;
      return true;
    }
  }
  return false;
}

std::string_view to_string(AdapterReadbackStatus status) noexcept {
  switch (status) {
    case AdapterReadbackStatus::Verified:
      return "VERIFIED";
    case AdapterReadbackStatus::Mismatch:
      return "MISMATCH";
    case AdapterReadbackStatus::Unknown:
      return "UNKNOWN";
    case AdapterReadbackStatus::Unsupported:
      return "UNSUPPORTED";
  }
  return "UNSUPPORTED";
}

bool parse_adapter_readback_status(std::string_view text, AdapterReadbackStatus& out) noexcept {
  const std::pair<std::string_view, AdapterReadbackStatus> table[] = {
      {"VERIFIED", AdapterReadbackStatus::Verified},
      {"MISMATCH", AdapterReadbackStatus::Mismatch},
      {"UNKNOWN", AdapterReadbackStatus::Unknown},
      {"UNSUPPORTED", AdapterReadbackStatus::Unsupported}};
  for (const auto& [rendering, value] : table) {
    if (rendering == text) {
      out = value;
      return true;
    }
  }
  return false;
}

AdapterApplyResult AdapterApplyResult::applied(std::string detail) {
  return AdapterApplyResult{AdapterApplyStatus::Applied, std::move(detail)};
}

AdapterApplyResult AdapterApplyResult::failed(std::string detail) {
  return AdapterApplyResult{AdapterApplyStatus::Failed, std::move(detail)};
}

AdapterApplyResult AdapterApplyResult::unknown(std::string detail) {
  return AdapterApplyResult{AdapterApplyStatus::OutcomeUnknown, std::move(detail)};
}

AdapterApplyResult AdapterApplyResult::unsupported(std::string detail) {
  return AdapterApplyResult{AdapterApplyStatus::Unsupported, std::move(detail)};
}

AdapterReadback AdapterReadback::verified(PortConfiguration observed, std::string detail) {
  AdapterReadback result;
  result.status = AdapterReadbackStatus::Verified;
  result.observed = std::move(observed);
  result.detail = std::move(detail);
  return result;
}

AdapterReadback AdapterReadback::mismatch(PortConfiguration observed, std::string detail) {
  AdapterReadback result;
  result.status = AdapterReadbackStatus::Mismatch;
  result.observed = std::move(observed);
  result.detail = std::move(detail);
  return result;
}

AdapterReadback AdapterReadback::unknown(std::string detail) {
  AdapterReadback result;
  result.status = AdapterReadbackStatus::Unknown;
  result.detail = std::move(detail);
  return result;
}

AdapterReadback AdapterReadback::unsupported(std::string detail) {
  AdapterReadback result;
  result.status = AdapterReadbackStatus::Unsupported;
  result.detail = std::move(detail);
  return result;
}

PortAdapter::~PortAdapter() = default;

}  // namespace portfabric
