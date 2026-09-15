#include "portfabric/synthetic.hpp"

#include <algorithm>

#include "text.hpp"

namespace portfabric {
namespace {

PortSpeed speed(std::uint64_t bits_per_second) {
  const auto value = PortSpeed::from_bits_per_second(bits_per_second);
  return value.has_value() ? *value : PortSpeed{};
}

struct ClassModel {
  SyntheticPortClass port_class;
  std::uint64_t rate;
  std::uint32_t lanes;
  FecMode fec;
  bool breakout;
  bool infiniBand;
  bool optical;
};

const ClassModel kModels[] = {
    {SyntheticPortClass::Management1G, 1000000000ull, 1, FecMode::None, false, false, false},
    {SyntheticPortClass::Access1G, 1000000000ull, 1, FecMode::None, false, false, false},
    {SyntheticPortClass::Server10G, 10000000000ull, 1, FecMode::None, false, false, false},
    {SyntheticPortClass::Server25G, 25000000000ull, 1, FecMode::ReedSolomon528, false, false, false},
    {SyntheticPortClass::Uplink40G, 40000000000ull, 4, FecMode::None, true, false, false},
    {SyntheticPortClass::Uplink50G, 50000000000ull, 2, FecMode::ReedSolomon544, true, false, false},
    {SyntheticPortClass::Uplink100G, 100000000000ull, 4, FecMode::ReedSolomon528, true, false, false},
    {SyntheticPortClass::Spine200G, 200000000000ull, 4, FecMode::ReedSolomon544, true, false, false},
    {SyntheticPortClass::Spine400G, 400000000000ull, 8, FecMode::ReedSolomon544Interleaved, true,
     false, false},
    {SyntheticPortClass::Backbone800G, 800000000000ull, 8, FecMode::ReedSolomon544Interleaved, true,
     false, false},
    {SyntheticPortClass::Optical400G, 400000000000ull, 4, FecMode::ReedSolomon544, false, false,
     true},
    {SyntheticPortClass::InfiniBandHdr200G, 200000000000ull, 4, FecMode::ReedSolomon544, false, true,
     false},
};

std::string compose_synthetic_identity(const std::string& device_text, std::uint32_t index) {
  std::string identity = device_text;
  identity.append("-p");
  const std::string digits = std::to_string(index);
  if (digits.size() < 2) {
    identity.push_back('0');
  }
  identity.append(digits);
  if (identity.size() > max_identifier_length) {
    identity.resize(max_identifier_length);
  }
  return identity;
}

const ClassModel* model_for(SyntheticPortClass port_class) {
  for (const ClassModel& model : kModels) {
    if (model.port_class == port_class) {
      return &model;
    }
  }
  return nullptr;
}

}  // namespace

std::string_view to_string(SyntheticPortClass port_class) noexcept {
  switch (port_class) {
    case SyntheticPortClass::Management1G:
      return "MANAGEMENT_1G";
    case SyntheticPortClass::Access1G:
      return "ACCESS_1G";
    case SyntheticPortClass::Server10G:
      return "SERVER_10G";
    case SyntheticPortClass::Server25G:
      return "SERVER_25G";
    case SyntheticPortClass::Uplink40G:
      return "UPLINK_40G";
    case SyntheticPortClass::Uplink50G:
      return "UPLINK_50G";
    case SyntheticPortClass::Uplink100G:
      return "UPLINK_100G";
    case SyntheticPortClass::Spine200G:
      return "SPINE_200G";
    case SyntheticPortClass::Spine400G:
      return "SPINE_400G";
    case SyntheticPortClass::Backbone800G:
      return "BACKBONE_800G";
    case SyntheticPortClass::Optical400G:
      return "OPTICAL_400G";
    case SyntheticPortClass::InfiniBandHdr200G:
      return "INFINIBAND_HDR_200G";
  }
  return "UNKNOWN";
}

bool parse_synthetic_class(std::string_view text, SyntheticPortClass& out) noexcept {
  for (const ClassModel& model : kModels) {
    if (portfabric::to_string(model.port_class) == text) {
      out = model.port_class;
      return true;
    }
  }
  return false;
}

std::string_view to_string(SyntheticFault fault) noexcept {
  switch (fault) {
    case SyntheticFault::None:
      return "NONE";
    case SyntheticFault::PermissionDenied:
      return "PERMISSION_DENIED";
    case SyntheticFault::DeviceDisappeared:
      return "DEVICE_DISAPPEARED";
    case SyntheticFault::DeviceReplaced:
      return "DEVICE_REPLACED";
    case SyntheticFault::UnsupportedParameter:
      return "UNSUPPORTED_PARAMETER";
    case SyntheticFault::PartialApply:
      return "PARTIAL_APPLY";
    case SyntheticFault::FailedVerification:
      return "FAILED_VERIFICATION";
    case SyntheticFault::StaleHandle:
      return "STALE_HANDLE";
    case SyntheticFault::ConcurrentExternalChange:
      return "CONCURRENT_EXTERNAL_CHANGE";
    case SyntheticFault::AdapterCrash:
      return "ADAPTER_CRASH";
    case SyntheticFault::TransportFailure:
      return "TRANSPORT_FAILURE";
  }
  return "NONE";
}

bool parse_synthetic_fault(std::string_view text, SyntheticFault& out) noexcept {
  for (std::uint8_t index = 0; index <= static_cast<std::uint8_t>(SyntheticFault::TransportFailure);
       ++index) {
    const auto fault = static_cast<SyntheticFault>(index);
    if (portfabric::to_string(fault) == text) {
      out = fault;
      return true;
    }
  }
  return false;
}

std::string SyntheticPortDefinition::to_string() const {
  std::string out("port=");
  out.append(port.valid() ? port.to_string() : std::string("none"));
  out.append(" device=");
  out.append(device.valid() ? device.to_string() : std::string("none"));
  out.append(" class=");
  out.append(portfabric::to_string(entity_class));
  out.append(" capabilities=[");
  out.append(capabilities.to_string());
  out.push_back(']');
  return out;
}

SyntheticFabric::SyntheticFabric(std::string name) : name_(std::move(name)) {}

PortCapabilities SyntheticFabric::capabilities_for(SyntheticPortClass port_class) {
  PortCapabilities capabilities;
  const ClassModel* model = model_for(port_class);
  if (model == nullptr) {
    return capabilities;
  }
  capabilities.certainty = CapabilityCertainty::Known;
  capabilities.supported_speeds.push_back(speed(model->rate));
  capabilities.supported_speeds.push_back(speed(model->rate / 2));
  capabilities.max_lanes = max_lanes;
  capabilities.lane_modes.push_back(1);
  capabilities.lane_modes.push_back(2);
  capabilities.lane_modes.push_back(4);
  capabilities.lane_modes.push_back(8);
  capabilities.breakout_supported = model->breakout;
  capabilities.max_breakout_children = model->breakout ? 4 : 1;
  capabilities.fec_modes.push_back(FecMode::None);
  capabilities.fec_modes.push_back(FecMode::Firecode);
  capabilities.fec_modes.push_back(FecMode::ReedSolomon528);
  capabilities.fec_modes.push_back(FecMode::ReedSolomon544);
  capabilities.fec_modes.push_back(FecMode::ReedSolomon544Interleaved);
  capabilities.autoneg_modes.push_back(AutonegPolicy::Forced);
  capabilities.autoneg_modes.push_back(AutonegPolicy::Preferred);
  capabilities.autoneg_modes.push_back(AutonegPolicy::Negotiated);
  capabilities.pause_modes.push_back(PauseMode::Disabled);
  capabilities.pause_modes.push_back(PauseMode::Transmit);
  capabilities.pause_modes.push_back(PauseMode::Receive);
  capabilities.pause_modes.push_back(PauseMode::Bidirectional);
  capabilities.protocol_families.push_back(model->infiniBand ? ProtocolFamily::InfiniBand
                                                             : ProtocolFamily::Ethernet);
  if (model->optical) {
    capabilities.protocol_families.push_back(ProtocolFamily::OpticalTransport);
  }
  capabilities.protocol_families.push_back(ProtocolFamily::Synthetic);
  capabilities.capability_mtu_min = 1280;
  capabilities.capability_mtu_max = model->infiniBand ? 4096 : 9216;
  capabilities.duplex = DuplexMode::Full;
  capabilities.logical_interface_supported = true;
  capabilities.max_queue_count = 256;
  capabilities.transceiver_required = model->rate >= 25000000000ull;
  capabilities.transceiver_class = capabilities.transceiver_required ? "synthetic-optics" : "";
  return capabilities;
}

DeviceId SyntheticFabric::add_device(std::string_view device_label, SyntheticPortClass port_class,
                                     std::uint32_t port_count, PortEntityClass entity_class) {
  if (port_count == 0 || port_count > max_ports_per_device) {
    port_count = 1;
  }
  const std::string device_text = "syn-" + std::string(device_label);
  const DeviceId device = DeviceId::from_validated(device_text);
  if (!device.valid()) {
    return DeviceId{};
  }
  devices_[device] = port_count;
  const PortCapabilities capabilities = capabilities_for(port_class);
  for (std::uint32_t index = 1; index <= port_count; ++index) {
    SyntheticPortDefinition definition;
    definition.port = PortId::from_validated(compose_synthetic_identity(device_text, index));
    if (!definition.port.valid()) {
      continue;
    }
    definition.device = device;
    definition.entity_class = entity_class;
    definition.capabilities = capabilities;
    // add_port maintains the identity index, so every generated port is
    // reachable without scanning the inventory.
    add_port(definition);
    classes_[definition.port] = port_class;
  }
  return device;
}

void SyntheticFabric::add_port(const SyntheticPortDefinition& definition) {
  if (!definition.port.valid()) {
    return;
  }
  const auto existing = index_.find(definition.port);
  if (existing != index_.end()) {
    ports_[existing->second] = definition;
    return;
  }
  index_.emplace(definition.port, ports_.size());
  ports_.push_back(definition);
  devices_[definition.device] = devices_[definition.device] + 1;
}

const SyntheticPortDefinition* SyntheticFabric::find(const PortId& port) const noexcept {
  const auto existing = index_.find(port);
  if (existing == index_.end()) {
    return nullptr;
  }
  return &ports_[existing->second];
}

std::vector<PortId> SyntheticFabric::add_breakout_children(const PortId& parent,
                                                           std::uint32_t children) {
  std::vector<PortId> created;
  const SyntheticPortDefinition* parent_definition = find(parent);
  if (parent_definition == nullptr || children < 2 || children > max_breakout_children) {
    return created;
  }
  // The parent definition is copied before the inventory grows: the inventory is
  // a vector, so adding children may reallocate it and invalidate the pointer the
  // search loop produced.
  const SyntheticPortDefinition parent_copy = *parent_definition;
  for (std::uint32_t index = 1; index <= children; ++index) {
    SyntheticPortDefinition child = parent_copy;
    child.port = PortId::from_validated(parent.value() + "-b" + std::to_string(index));
    child.entity_class = PortEntityClass::LogicalPort;
    if (!child.port.valid()) {
      continue;
    }
    add_port(child);
    created.push_back(child.port);
  }
  return created;
}

std::optional<CapabilityBinding> SyntheticFabric::binding_for(const PortId& port) const {
  const SyntheticPortDefinition* definition = find(port);
  if (definition == nullptr) {
    return std::nullopt;
  }
  CapabilityBinding binding;
  binding.present = true;
  binding.capabilities = definition->capabilities;
  binding.generation = capability_generation_;
  binding.evidence_generation = EvidenceGeneration::from_value(capability_generation_.value());
  binding.source = name_;
  binding.evidence = EvidenceId::from_validated("syn-evidence-" + port.value());
  return binding;
}

void SyntheticFabric::set_fault(const PortId& port, SyntheticFault fault) {
  faults_[port] = fault;
}

void SyntheticFabric::set_global_fault(SyntheticFault fault) { global_fault_ = fault; }

void SyntheticFabric::clear_faults() {
  faults_.clear();
  global_fault_ = SyntheticFault::None;
}

SyntheticFault SyntheticFabric::fault_for(const PortId& port) const {
  const auto found = faults_.find(port);
  if (found != faults_.end()) {
    return found->second;
  }
  return global_fault_;
}

bool SyntheticFabric::set_observed_configuration(const PortId& port,
                                                 PortConfiguration configuration) {
  for (const SyntheticPortDefinition& definition : ports_) {
    if (definition.port == port) {
      observed_configurations_[port] = std::move(configuration);
      return true;
    }
  }
  return false;
}

std::optional<PortConfiguration> SyntheticFabric::observed_configuration_for(
    const PortId& port) const {
  const auto found = observed_configurations_.find(port);
  if (found == observed_configurations_.end()) {
    return std::nullopt;
  }
  return found->second;
}

void SyntheticFabric::clear_observed_configuration(const PortId& port) {
  observed_configurations_.erase(port);
}

CapabilityBindingGeneration SyntheticFabric::advance_capability_generation() {
  capability_generation_ = capability_generation_.next();
  return capability_generation_;
}

std::string SyntheticFabric::to_string() const {
  std::string out("synthetic fabric ");
  out.append(name_);
  out.append(" devices=");
  detail::append_u64(out, static_cast<std::uint64_t>(devices_.size()));
  out.append(" ports=");
  detail::append_u64(out, static_cast<std::uint64_t>(ports_.size()));
  out.append(" capability_generation=");
  out.append(capability_generation_.to_string());
  return out;
}

SyntheticCapabilityProvider::SyntheticCapabilityProvider(SyntheticFabric& fabric)
    : fabric_(fabric) {}

std::string_view SyntheticCapabilityProvider::label() const noexcept { return "synthetic"; }

std::optional<CapabilityBinding> SyntheticCapabilityProvider::current(const PortId& port,
                                                                     std::string& error) {
  error.clear();
  std::optional<CapabilityBinding> binding = fabric_.binding_for(port);
  if (!binding.has_value()) {
    error = "the synthetic fabric does not model this port";
    return std::nullopt;
  }
  return binding;
}

SyntheticAdapter::SyntheticAdapter(SyntheticFabric& fabric) : fabric_(fabric) {}

std::string_view SyntheticAdapter::label() const noexcept { return "synthetic"; }

AdapterApplyResult SyntheticAdapter::apply(const PortId& port,
                                           const PortConfiguration& configuration) {
  const SyntheticFault fault = fabric_.fault_for(port);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++applies_;
    switch (fault) {
      case SyntheticFault::None:
        break;
      case SyntheticFault::PermissionDenied:
        return AdapterApplyResult::failed("permission denied by the synthetic device");
      case SyntheticFault::DeviceDisappeared:
        return AdapterApplyResult::failed("the synthetic device disappeared");
      case SyntheticFault::DeviceReplaced:
        return AdapterApplyResult::unknown(
            "the synthetic device was replaced while the request was in flight");
      case SyntheticFault::UnsupportedParameter:
        return AdapterApplyResult::failed(
            "the synthetic device rejected an unsupported parameter");
      case SyntheticFault::PartialApply:
        return AdapterApplyResult::failed(
            "the synthetic device applied part of the configuration only");
      case SyntheticFault::FailedVerification:
        return AdapterApplyResult::applied(
            "the synthetic device accepted the configuration but verification failed");
      case SyntheticFault::StaleHandle:
        return AdapterApplyResult::failed("the synthetic device handle is stale");
      case SyntheticFault::ConcurrentExternalChange:
        return AdapterApplyResult::unknown(
            "a concurrent external change made the apply outcome ambiguous");
      case SyntheticFault::AdapterCrash:
        return AdapterApplyResult::unknown("the synthetic adapter crashed mid apply");
      case SyntheticFault::TransportFailure:
        return AdapterApplyResult::unknown(
            "the synthetic control transport failed before completion");
    }
    observed_[port] = configuration;
  }
  return AdapterApplyResult::applied("the synthetic device accepted the configuration");
}

AdapterReadback SyntheticAdapter::readback(const PortId& port) {
  const SyntheticFault fault = fabric_.fault_for(port);
  std::lock_guard<std::mutex> lock(mutex_);
  ++readbacks_;
  switch (fault) {
    case SyntheticFault::DeviceDisappeared:
    case SyntheticFault::StaleHandle:
    case SyntheticFault::AdapterCrash:
    case SyntheticFault::TransportFailure:
    case SyntheticFault::ConcurrentExternalChange:
    case SyntheticFault::DeviceReplaced:
      return AdapterReadback::unknown("the synthetic device could not be read back");
    case SyntheticFault::FailedVerification:
    case SyntheticFault::PartialApply:
      return AdapterReadback::mismatch(PortConfiguration{},
                                       "the synthetic device state does not match the request");
    default:
      break;
  }
  const auto found = observed_.find(port);
  if (found == observed_.end()) {
    return AdapterReadback::unknown("the synthetic device holds no configuration for this port");
  }
  const auto external = fabric_.observed_configuration_for(port);
  if (external.has_value()) {
    return AdapterReadback::mismatch(*external,
                                     "the synthetic device state differs from the request");
  }
  return AdapterReadback::verified(found->second, "the synthetic device matches the request");
}

std::optional<PortConfiguration> SyntheticAdapter::observed(const PortId& port) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = observed_.find(port);
  if (found == observed_.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::uint64_t SyntheticAdapter::applies_completed() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return applies_;
}

std::uint64_t SyntheticAdapter::readbacks_completed() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return readbacks_;
}

}  // namespace portfabric
