#include "portfabric/capability.hpp"

#include <algorithm>

#include "text.hpp"

namespace portfabric {
namespace {

void append_list(std::string& out, const std::vector<PortSpeed>& values) {
  bool first = true;
  for (const PortSpeed& value : values) {
    if (!first) {
      out.push_back(',');
    }
    first = false;
    out.append(value.to_string());
  }
  if (first) {
    out.append("none");
  }
}

template <class T, class Render>
void append_rendered(std::string& out, const std::vector<T>& values, Render render) {
  bool first = true;
  for (const T& value : values) {
    if (!first) {
      out.push_back(',');
    }
    first = false;
    out.append(render(value));
  }
  if (first) {
    out.append("none");
  }
}

}  // namespace

std::string_view to_string(CapabilityCertainty certainty) noexcept {
  switch (certainty) {
    case CapabilityCertainty::Unknown:
      return "UNKNOWN";
    case CapabilityCertainty::Known:
      return "KNOWN";
    case CapabilityCertainty::Partial:
      return "PARTIAL";
  }
  return "UNKNOWN";
}

bool parse_capability_certainty(std::string_view text, CapabilityCertainty& out) noexcept {
  if (text == "UNKNOWN") {
    out = CapabilityCertainty::Unknown;
    return true;
  }
  if (text == "KNOWN") {
    out = CapabilityCertainty::Known;
    return true;
  }
  if (text == "PARTIAL") {
    out = CapabilityCertainty::Partial;
    return true;
  }
  return false;
}

bool is_valid(CapabilityCertainty certainty) noexcept {
  switch (certainty) {
    case CapabilityCertainty::Unknown:
    case CapabilityCertainty::Known:
    case CapabilityCertainty::Partial:
      return true;
  }
  return false;
}

bool PortCapabilities::validate(std::string& error) const {
  if (!is_valid(certainty)) {
    error = "capability certainty is not a legal value";
    return false;
  }
  if (supported_speeds.size() > max_capability_entries) {
    error = "supported speed list exceeds the configured bound";
    return false;
  }
  if (lane_modes.size() > max_mode_entries) {
    error = "lane mode list exceeds the configured bound";
    return false;
  }
  if (fec_modes.size() > max_mode_entries) {
    error = "FEC mode list exceeds the configured bound";
    return false;
  }
  if (autoneg_modes.size() > max_mode_entries) {
    error = "autonegotiation mode list exceeds the configured bound";
    return false;
  }
  if (pause_modes.size() > max_mode_entries) {
    error = "pause mode list exceeds the configured bound";
    return false;
  }
  if (protocol_families.size() > max_mode_entries) {
    error = "protocol family list exceeds the configured bound";
    return false;
  }
  if (max_lanes > portfabric::max_lanes) {
    error = "lane bound exceeds the configured bound";
    return false;
  }
  for (const PortSpeed& speed : supported_speeds) {
    if (!speed.valid()) {
      error = "a supported speed entry is not a valid rate";
      return false;
    }
  }
  for (const std::uint32_t lanes : lane_modes) {
    if (lanes == 0 || lanes > portfabric::max_lanes) {
      error = "a lane mode entry is outside the legal lane range";
      return false;
    }
  }
  for (const FecMode mode : fec_modes) {
    if (!is_valid(mode)) {
      error = "a FEC mode entry is not a legal value";
      return false;
    }
  }
  for (const AutonegPolicy policy : autoneg_modes) {
    if (!is_valid(policy)) {
      error = "an autonegotiation entry is not a legal value";
      return false;
    }
  }
  for (const PauseMode mode : pause_modes) {
    if (!is_valid(mode)) {
      error = "a pause mode entry is not a legal value";
      return false;
    }
  }
  for (const ProtocolFamily family : protocol_families) {
    if (!is_valid(family)) {
      error = "a protocol family entry is not a legal value";
      return false;
    }
  }
  if (!is_valid(duplex)) {
    error = "duplex evidence is not a legal value";
    return false;
  }
  if (capability_mtu_min > capability_mtu_max) {
    error = "capability MTU bounds are inverted";
    return false;
  }
  if (capability_mtu_max > portfabric::max_mtu) {
    error = "capability MTU bound exceeds the runtime bound";
    return false;
  }
  if (max_breakout_children > portfabric::max_breakout_children) {
    error = "breakout bound exceeds the configured bound";
    return false;
  }
  if (max_queue_count > portfabric::max_queue_count) {
    error = "queue count bound exceeds the configured bound";
    return false;
  }
  if (!detail::validate_text(transceiver_class, max_name_length, error)) {
    error = "transceiver class: " + error;
    return false;
  }
  return true;
}

bool PortCapabilities::supports_speed(PortSpeed speed) const noexcept {
  if (!speed.valid() || certainty == CapabilityCertainty::Unknown) {
    return false;
  }
  return std::find(supported_speeds.begin(), supported_speeds.end(), speed) !=
         supported_speeds.end();
}

bool PortCapabilities::supports_lanes(std::uint32_t lanes) const noexcept {
  if (lanes == 0 || certainty == CapabilityCertainty::Unknown) {
    return false;
  }
  if (max_lanes != 0 && lanes > max_lanes) {
    return false;
  }
  if (lane_modes.empty()) {
    // The port exposes no lane enumeration: only the bound is meaningful.
    return max_lanes == 0 && lanes == 1;
  }
  return std::find(lane_modes.begin(), lane_modes.end(), lanes) != lane_modes.end();
}

bool PortCapabilities::supports_fec(FecMode mode) const noexcept {
  if (certainty == CapabilityCertainty::Unknown) {
    return false;
  }
  if (mode == FecMode::Unknown) {
    return false;
  }
  return std::find(fec_modes.begin(), fec_modes.end(), mode) != fec_modes.end();
}

bool PortCapabilities::supports_autoneg(AutonegPolicy policy) const noexcept {
  if (certainty == CapabilityCertainty::Unknown) {
    return false;
  }
  if (policy == AutonegPolicy::Unknown) {
    return false;
  }
  return std::find(autoneg_modes.begin(), autoneg_modes.end(), policy) != autoneg_modes.end();
}

bool PortCapabilities::supports_pause(PauseMode mode) const noexcept {
  if (certainty == CapabilityCertainty::Unknown) {
    return false;
  }
  if (mode == PauseMode::Unknown) {
    return false;
  }
  return std::find(pause_modes.begin(), pause_modes.end(), mode) != pause_modes.end();
}

bool PortCapabilities::supports_protocol(ProtocolFamily family) const noexcept {
  if (certainty == CapabilityCertainty::Unknown || family == ProtocolFamily::Unknown) {
    return false;
  }
  return std::find(protocol_families.begin(), protocol_families.end(), family) !=
         protocol_families.end();
}

std::string PortCapabilities::to_string() const {
  std::string out("certainty=");
  out.append(portfabric::to_string(certainty));
  out.append(" speeds=");
  append_list(out, supported_speeds);
  out.append(" lanes=");
  append_rendered(out, lane_modes, [](std::uint32_t value) { return std::to_string(value); });
  out.append(" max_lanes=");
  detail::append_u64(out, max_lanes);
  out.append(" breakout=");
  detail::append_bool(out, breakout_supported);
  out.append(" fec=");
  append_rendered(out, fec_modes,
                  [](FecMode value) { return std::string(portfabric::to_string(value)); });
  out.append(" mtu=");
  detail::append_u64(out, capability_mtu_min);
  out.push_back('-');
  detail::append_u64(out, capability_mtu_max);
  out.append(" protocols=");
  append_rendered(out, protocol_families,
                  [](ProtocolFamily value) { return std::string(portfabric::to_string(value)); });
  return out;
}

std::string CapabilityBinding::to_string() const {
  if (!present) {
    return "unbound";
  }
  std::string out("generation=");
  out.append(generation.to_string());
  out.append(" evidence_generation=");
  out.append(evidence_generation.to_string());
  out.append(" source=");
  out.append(source.empty() ? "unspecified" : source);
  out.append(" ");
  out.append(capabilities.to_string());
  return out;
}

CapabilityProvider::~CapabilityProvider() = default;

CapabilityValidation CapabilityValidation::accept() { return CapabilityValidation{}; }

CapabilityValidation CapabilityValidation::reject(OutcomeCode code, std::string reason,
                                                  std::string detail) {
  CapabilityValidation validation;
  validation.accepted = false;
  validation.code = code;
  validation.reason = std::move(reason);
  validation.detail = std::move(detail);
  return validation;
}

std::string_view to_string(CapabilityValidation validation) noexcept {
  return validation.accepted ? std::string_view("ACCEPTED")
                             : std::string_view(portfabric::to_string(validation.code));
}

}  // namespace portfabric
