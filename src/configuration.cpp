#include "portfabric/configuration.hpp"

#include <vector>

#include "text.hpp"

namespace portfabric {
namespace {

void append_optional_speed(std::string& out, const std::optional<PortSpeed>& speed) {
  out.append(speed.has_value() ? speed->to_string() : std::string("none"));
}

}  // namespace

std::string_view to_string(ProvenanceKind kind) noexcept {
  switch (kind) {
    case ProvenanceKind::Unknown:
      return "UNKNOWN";
    case ProvenanceKind::Operator:
      return "OPERATOR";
    case ProvenanceKind::NetworkController:
      return "NETWORK_CONTROLLER";
    case ProvenanceKind::SwitchAgent:
      return "SWITCH_AGENT";
    case ProvenanceKind::HostAgent:
      return "HOST_AGENT";
    case ProvenanceKind::DelegatedSubsystem:
      return "DELEGATED_SUBSYSTEM";
    case ProvenanceKind::SyntheticBackend:
      return "SYNTHETIC_BACKEND";
    case ProvenanceKind::HostAdapter:
      return "HOST_ADAPTER";
    case ProvenanceKind::Recovery:
      return "RECOVERY";
    case ProvenanceKind::Reconciliation:
      return "RECONCILIATION";
  }
  return "UNKNOWN";
}

bool parse_provenance_kind(std::string_view text, ProvenanceKind& out) noexcept {
  const std::pair<std::string_view, ProvenanceKind> table[] = {
      {"UNKNOWN", ProvenanceKind::Unknown},
      {"OPERATOR", ProvenanceKind::Operator},
      {"NETWORK_CONTROLLER", ProvenanceKind::NetworkController},
      {"SWITCH_AGENT", ProvenanceKind::SwitchAgent},
      {"HOST_AGENT", ProvenanceKind::HostAgent},
      {"DELEGATED_SUBSYSTEM", ProvenanceKind::DelegatedSubsystem},
      {"SYNTHETIC_BACKEND", ProvenanceKind::SyntheticBackend},
      {"HOST_ADAPTER", ProvenanceKind::HostAdapter},
      {"RECOVERY", ProvenanceKind::Recovery},
      {"RECONCILIATION", ProvenanceKind::Reconciliation}};
  for (const auto& [rendering, value] : table) {
    if (rendering == text) {
      out = value;
      return true;
    }
  }
  return false;
}

bool is_valid(ProvenanceKind kind) noexcept {
  switch (kind) {
    case ProvenanceKind::Unknown:
    case ProvenanceKind::Operator:
    case ProvenanceKind::NetworkController:
    case ProvenanceKind::SwitchAgent:
    case ProvenanceKind::HostAgent:
    case ProvenanceKind::DelegatedSubsystem:
    case ProvenanceKind::SyntheticBackend:
    case ProvenanceKind::HostAdapter:
    case ProvenanceKind::Recovery:
    case ProvenanceKind::Reconciliation:
      return true;
  }
  return false;
}

std::string Provenance::to_string() const {
  std::string out(portfabric::to_string(kind));
  if (!source.empty()) {
    out.push_back('(');
    out.append(source);
    out.push_back(')');
  }
  if (publisher.valid()) {
    out.append(" publisher=");
    out.append(publisher.to_string());
  }
  if (boot.valid()) {
    out.append(" boot=");
    out.append(boot.to_string());
  }
  if (epoch.valid()) {
    out.append(" epoch=");
    out.append(epoch.to_string());
  }
  if (attempt.valid()) {
    out.append(" attempt=");
    out.append(attempt.to_string());
  }
  return out;
}

std::string ProfileBinding::to_string() const {
  if (!bound) {
    return "none";
  }
  std::string out = id.to_string();
  out.push_back('@');
  out.append(generation.to_string());
  return out;
}

bool PortConfiguration::validate(std::string& error) const {
  if (!record.valid() || !port.valid()) {
    error = "configuration record or port identity is missing";
    return false;
  }
  if (!parent_device.valid()) {
    error = "parent device identity is missing";
    return false;
  }
  if (!device_generation.valid()) {
    error = "device generation is missing";
    return false;
  }
  if (!topology_generation.valid()) {
    error = "topology generation is missing";
    return false;
  }
  if (!generation.valid()) {
    error = "configuration generation is missing";
    return false;
  }
  if (!is_valid(administrative)) {
    error = "administrative state is not a legal value";
    return false;
  }
  if (!is_valid(mode)) {
    error = "port mode is not a legal value";
    return false;
  }
  if (!is_valid(protocol)) {
    error = "protocol family is not a legal value";
    return false;
  }
  if (!speed.valid()) {
    error = "speed setting is inconsistent (selection and rate disagree)";
    return false;
  }
  if (!is_valid(duplex)) {
    error = "duplex mode is not a legal value";
    return false;
  }
  if (!mtu.valid()) {
    error = "MTU is missing or outside the legal range";
    return false;
  }
  if (!lanes.valid()) {
    error = "lane configuration is missing or inconsistent";
    return false;
  }
  if (!is_valid(breakout)) {
    error = "breakout mode is not a legal value";
    return false;
  }
  if (!is_valid(autoneg)) {
    error = "autonegotiation policy is not a legal value";
    return false;
  }
  if (!is_valid(fec)) {
    error = "FEC mode is not a legal value";
    return false;
  }
  if (!is_valid(pause)) {
    error = "pause mode is not a legal value";
    return false;
  }
  if (!aggregation.valid()) {
    error = "aggregation membership is inconsistent";
    return false;
  }
  if (!profile.valid()) {
    error = "profile binding is inconsistent";
    return false;
  }
  if (!provenance.valid()) {
    error = "configuration provenance is incomplete";
    return false;
  }
  if (!detail::validate_text(role, max_name_length, error)) {
    error = "role: " + error;
    return false;
  }
  PortSpeed lane_total;
  if (lanes.total(lane_total)) {
    if (speed.selection == SpeedSelection::Forced && speed.rate != lane_total) {
      error = "the forced rate and the lane configuration disagree";
      return false;
    }
    if (speed.selection == SpeedSelection::Auto && speed.rate.valid()) {
      error = "an automatic speed selection must not carry a rate";
      return false;
    }
  }
  if (breakout != BreakoutMode::None && !breakout_lanes_are_divisible(lanes.lanes(), breakout)) {
    error = "the lane count is not divisible by the breakout fan-out";
    return false;
  }
  if (mode == PortMode::Physical) {
    if (logical_identity.valid() || derived_from.valid() || child_index != 0) {
      error = "a physical port must not carry a logical derivation";
      return false;
    }
  } else {
    if (!logical_identity.valid() || !derived_from.valid()) {
      error = "a logical port must carry a logical identity and its derivation";
      return false;
    }
    if (child_index != 0 && child_index > portfabric::max_breakout_children) {
      error = "the breakout child index is outside the legal position range";
      return false;
    }
  }
  return true;
}

std::optional<PortSpeed> PortConfiguration::total_speed() const {
  PortSpeed total;
  if (!lanes.total(total)) {
    return std::nullopt;
  }
  return total;
}

std::string PortConfiguration::to_string() const {
  std::string out;
  auto& fields = out;
  fields.append("record=");
  fields.append(record.valid() ? record.to_string() : std::string("none"));
  fields.append(" port=");
  fields.append(port.valid() ? port.to_string() : std::string("none"));
  fields.append(" parent=");
  fields.append(parent_device.valid() ? parent_device.to_string() : std::string("none"));
  fields.append(" device_generation=");
  fields.append(device_generation.valid() ? device_generation.to_string() : std::string("none"));
  fields.append(" topology_generation=");
  fields.append(topology_generation.valid() ? topology_generation.to_string() : std::string("none"));
  fields.append(" generation=");
  fields.append(generation.valid() ? generation.to_string() : std::string("none"));
  fields.append(" administrative=");
  fields.append(portfabric::to_string(administrative));
  fields.append(" mode=");
  fields.append(portfabric::to_string(mode));
  fields.append(" protocol=");
  fields.append(portfabric::to_string(protocol));
  fields.append(" speed=");
  fields.append(speed.to_string());
  fields.append(" duplex=");
  fields.append(portfabric::to_string(duplex));
  fields.append(" mtu=");
  fields.append(mtu.to_string());
  fields.append(" lanes=");
  fields.append(lanes.to_string());
  fields.append(" breakout=");
  fields.append(portfabric::to_string(breakout));
  fields.append(" autoneg=");
  fields.append(portfabric::to_string(autoneg));
  fields.append(" fec=");
  fields.append(portfabric::to_string(fec));
  fields.append(" pause=");
  fields.append(portfabric::to_string(pause));
  fields.append(" aggregation=");
  fields.append(aggregation.to_string());
  fields.append(" profile=");
  fields.append(profile.to_string());
  fields.append(" role=");
  fields.append(role.empty() ? "none" : role);
  fields.append(" logical=");
  fields.append(logical_identity.valid() ? logical_identity.to_string() : std::string("none"));
  fields.append(" derived_from=");
  fields.append(derived_from.valid() ? derived_from.to_string() : std::string("none"));
  fields.append(" child_index=");
  detail::append_u64(fields, child_index);
  fields.append(" capability_generation=");
  fields.append(capability_generation.valid() ? capability_generation.to_string()
                                              : std::string("none"));
  fields.append(" evidence_generation=");
  fields.append(evidence_generation.valid() ? evidence_generation.to_string() : std::string("none"));
  fields.append(" provenance=");
  fields.append(provenance.to_string());
  return out;
}

std::vector<std::pair<std::string, std::string>> PortConfiguration::canonical_fields() const {
  std::vector<std::pair<std::string, std::string>> fields;
  fields.reserve(32);
  auto add = [&fields](std::string_view name, std::string value) {
    fields.emplace_back(std::string(name), std::move(value));
  };
  add("record", record.valid() ? record.to_string() : std::string());
  add("port", port.valid() ? port.to_string() : std::string());
  add("parent_device", parent_device.valid() ? parent_device.to_string() : std::string());
  add("device_generation", device_generation.valid() ? device_generation.to_string() : std::string());
  add("topology_generation",
      topology_generation.valid() ? topology_generation.to_string() : std::string());
  add("generation", generation.valid() ? generation.to_string() : std::string());
  add("administrative", std::string(portfabric::to_string(administrative)));
  add("mode", std::string(portfabric::to_string(mode)));
  add("protocol", std::string(portfabric::to_string(protocol)));
  add("speed_selection", std::string(portfabric::to_string(speed.selection)));
  add("speed_rate", speed.rate.valid() ? speed.rate.to_string() : std::string());
  add("duplex", std::string(portfabric::to_string(duplex)));
  add("mtu", mtu.valid() ? mtu.to_string() : std::string());
  add("lanes", lanes.to_string());
  add("breakout", std::string(portfabric::to_string(breakout)));
  add("autoneg", std::string(portfabric::to_string(autoneg)));
  add("fec", std::string(portfabric::to_string(fec)));
  add("pause", std::string(portfabric::to_string(pause)));
  add("aggregation", aggregation.to_string());
  add("profile", profile.to_string());
  add("role", role);
  add("logical_identity", logical_identity.valid() ? logical_identity.to_string() : std::string());
  add("derived_from", derived_from.valid() ? derived_from.to_string() : std::string());
  add("child_index", std::to_string(child_index));
  add("capability_generation",
      capability_generation.valid() ? capability_generation.to_string() : std::string());
  add("evidence_generation",
      evidence_generation.valid() ? evidence_generation.to_string() : std::string());
  add("provenance_kind", std::string(portfabric::to_string(provenance.kind)));
  add("provenance_source", provenance.source);
  add("provenance_publisher",
      provenance.publisher.valid() ? provenance.publisher.to_string() : std::string());
  add("provenance_epoch",
      provenance.epoch.valid() ? provenance.epoch.to_string() : std::string());
  add("provenance_attempt",
      provenance.attempt.valid() ? provenance.attempt.to_string() : std::string());
  return fields;
}

CapabilityValidation validate_against_capabilities(const PortConfiguration& configuration,
                                                   const CapabilityBinding& binding) {
  if (!binding.present || !binding.generation.valid()) {
    return CapabilityValidation::reject(OutcomeCode::CapabilityUnavailable, "CAPABILITY_UNBOUND",
                                        "no capability evidence is bound to this port");
  }
  const PortCapabilities& capabilities = binding.capabilities;
  std::string error;
  if (!capabilities.validate(error)) {
    return CapabilityValidation::reject(OutcomeCode::CapabilityUnavailable,
                                        "CAPABILITY_EVIDENCE_INVALID", error);
  }
  if (capabilities.certainty == CapabilityCertainty::Unknown) {
    return CapabilityValidation::reject(OutcomeCode::CapabilityUnknown, "CAPABILITY_UNKNOWN",
                                        "capability evidence for this port is UNKNOWN");
  }

  PortSpeed port_total;
  const bool have_total = configuration.lanes.total(port_total);
  if (!have_total) {
    return CapabilityValidation::reject(OutcomeCode::InvalidLaneConfiguration,
                                        "LANE_TOTAL_OVERFLOW",
                                        "the lane configuration does not yield a representable rate");
  }
  if (configuration.speed.selection == SpeedSelection::Auto) {
    // Automatic selection requires the port to advertise at least one rate that
    // the lane configuration can realise.
    bool any = false;
    for (const PortSpeed& candidate : capabilities.supported_speeds) {
      if (candidate == port_total) {
        any = true;
        break;
      }
    }
    if (!any) {
      return CapabilityValidation::reject(
          OutcomeCode::UnsupportedConfiguration, "SPEED_AUTO_UNSUPPORTED",
          "no advertised rate matches the derived lane rate " + port_total.to_string());
    }
  } else {
    if (!capabilities.supports_speed(configuration.speed.rate)) {
      return CapabilityValidation::reject(
          OutcomeCode::UnsupportedConfiguration, "SPEED_UNSUPPORTED",
          "the port does not advertise " + configuration.speed.rate.to_string());
    }
    if (configuration.speed.rate != port_total) {
      return CapabilityValidation::reject(OutcomeCode::InvalidLaneConfiguration,
                                          "LANE_RATE_MISMATCH",
                                          "the lane configuration does not realise the forced rate");
    }
  }
  if (!capabilities.supports_lanes(configuration.lanes.lanes())) {
    return CapabilityValidation::reject(
        OutcomeCode::UnsupportedConfiguration, "LANE_COUNT_UNSUPPORTED",
        "the port does not support " + std::to_string(configuration.lanes.lanes()) + " lanes");
  }
  if (configuration.breakout != BreakoutMode::None) {
    if (!capabilities.breakout_supported) {
      return CapabilityValidation::reject(OutcomeCode::UnsupportedConfiguration,
                                          "BREAKOUT_UNSUPPORTED",
                                          "the port does not advertise breakout support");
    }
    const std::uint32_t children = breakout_children(configuration.breakout);
    if (children > capabilities.max_breakout_children) {
      return CapabilityValidation::reject(
          OutcomeCode::UnsupportedConfiguration, "BREAKOUT_FANOUT_UNSUPPORTED",
          "the port supports at most " + std::to_string(capabilities.max_breakout_children) +
              " breakout children");
    }
  }
  if (configuration.protocol != ProtocolFamily::Unknown) {
    if (!capabilities.supports_protocol(configuration.protocol)) {
      return CapabilityValidation::reject(
          OutcomeCode::UnsupportedConfiguration, "PROTOCOL_UNSUPPORTED",
          "the port does not advertise protocol family " +
              std::string(portfabric::to_string(configuration.protocol)));
    }
  } else {
    return CapabilityValidation::reject(OutcomeCode::CapabilityUnknown, "PROTOCOL_UNKNOWN",
                                        "the configuration does not name a protocol family");
  }
  if (configuration.fec != FecMode::Unknown && configuration.fec != FecMode::Unsupported) {
    if (!capabilities.supports_fec(configuration.fec)) {
      return CapabilityValidation::reject(OutcomeCode::UnsupportedConfiguration,
                                          "FEC_UNSUPPORTED",
                                          "the port does not advertise FEC mode " +
                                              std::string(portfabric::to_string(configuration.fec)));
    }
  } else if (configuration.fec == FecMode::Unknown) {
    return CapabilityValidation::reject(OutcomeCode::CapabilityUnknown, "FEC_UNKNOWN",
                                        "the configuration does not name a FEC mode");
  }
  if (configuration.autoneg != AutonegPolicy::Unknown &&
      configuration.autoneg != AutonegPolicy::Unsupported) {
    if (!capabilities.supports_autoneg(configuration.autoneg)) {
      return CapabilityValidation::reject(OutcomeCode::UnsupportedConfiguration,
                                          "AUTONEG_UNSUPPORTED",
                                          "the port does not advertise autonegotiation policy " +
                                              std::string(portfabric::to_string(configuration.autoneg)));
    }
  } else if (configuration.autoneg == AutonegPolicy::Unknown) {
    return CapabilityValidation::reject(OutcomeCode::CapabilityUnknown, "AUTONEG_UNKNOWN",
                                        "the configuration does not name an autonegotiation policy");
  }
  if (configuration.pause != PauseMode::Unknown && configuration.pause != PauseMode::Unsupported) {
    if (!capabilities.supports_pause(configuration.pause)) {
      return CapabilityValidation::reject(OutcomeCode::UnsupportedConfiguration,
                                          "PAUSE_UNSUPPORTED",
                                          "the port does not advertise pause mode " +
                                              std::string(portfabric::to_string(configuration.pause)));
    }
  } else if (configuration.pause == PauseMode::Unknown) {
    return CapabilityValidation::reject(OutcomeCode::CapabilityUnknown, "PAUSE_UNKNOWN",
                                        "the configuration does not name a pause mode");
  }
  if (configuration.duplex != DuplexMode::Unknown &&
      configuration.duplex != DuplexMode::Unsupported) {
    if (capabilities.duplex != DuplexMode::Unknown &&
        capabilities.duplex != configuration.duplex) {
      return CapabilityValidation::reject(OutcomeCode::UnsupportedConfiguration,
                                          "DUPLEX_UNSUPPORTED",
                                          "the port does not advertise duplex mode " +
                                              std::string(portfabric::to_string(configuration.duplex)));
    }
  } else if (configuration.duplex == DuplexMode::Unknown) {
    return CapabilityValidation::reject(OutcomeCode::CapabilityUnknown, "DUPLEX_UNKNOWN",
                                        "the configuration does not name a duplex mode");
  }
  if (capabilities.capability_mtu_max != 0) {
    if (configuration.mtu.value() < capabilities.capability_mtu_min ||
        configuration.mtu.value() > capabilities.capability_mtu_max) {
      return CapabilityValidation::reject(
          OutcomeCode::UnsupportedConfiguration, "MTU_OUT_OF_RANGE",
          "the port supports MTU " + std::to_string(capabilities.capability_mtu_min) + ".." +
              std::to_string(capabilities.capability_mtu_max));
    }
  } else {
    return CapabilityValidation::reject(OutcomeCode::CapabilityUnknown, "MTU_UNKNOWN",
                                        "the port does not advertise MTU bounds");
  }
  if (configuration.mode == PortMode::Logical && !capabilities.logical_interface_supported) {
    return CapabilityValidation::reject(OutcomeCode::UnsupportedConfiguration,
                                        "LOGICAL_INTERFACE_UNSUPPORTED",
                                        "the port does not advertise logical interface support");
  }
  if (capabilities.transceiver_required) {
    if (capabilities.transceiver_class.empty()) {
      return CapabilityValidation::reject(OutcomeCode::TransceiverUnknown, "TRANSCEIVER_UNKNOWN",
                                          "the port requires a transceiver of unknown class");
    }
  }
  return CapabilityValidation::accept();
}

}  // namespace portfabric
