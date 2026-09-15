// Example 5: UNKNOWN capability fails closed; capability generation advance
// invalidates a configuration that depended on the previous truth.
#include <cstdio>
#include <string>

#include "portfabric/engine.hpp"
#include "portfabric/synthetic.hpp"

namespace {

/// Builds a deterministic synthetic fabric with one port of the given class.
struct Fixture {
  portfabric::SyntheticFabric fabric{"example-synthetic"};
  portfabric::SyntheticCapabilityProvider provider{fabric};
  portfabric::SyntheticAdapter adapter{fabric};
  portfabric::DeviceId device;

  explicit Fixture(portfabric::SyntheticPortClass port_class) {
    device = fabric.add_device("sw1", port_class, 4, portfabric::PortEntityClass::SyntheticPort);
  }

  portfabric::PortId port(std::uint32_t index) const {
    return portfabric::PortId::from_validated("syn-sw1-p0" + std::to_string(index));
  }
};

portfabric::EngineConfig config_for(const std::string& coordinator, const std::string& path) {
  portfabric::EngineConfig config;
  config.fabric = portfabric::FabricId::from_validated("fabric-a");
  config.site = portfabric::SiteId::from_validated("site-a");
  config.coordinator = portfabric::PublisherId::from_validated(coordinator);
  config.epoch = portfabric::CoordinatorEpoch::from_value(1);
  config.persistence_path = path;
  config.persistence = path.empty() ? portfabric::PersistenceMode::Manual
                                    : portfabric::PersistenceMode::Automatic;
  config.adapter_policy = portfabric::AdapterPolicy::ApplyAndVerify;
  return config;
}

portfabric::PortSpeed speed_for(portfabric::SyntheticPortClass port_class) {
  using portfabric::SyntheticPortClass;
  switch (port_class) {
    case SyntheticPortClass::Access1G:
    case SyntheticPortClass::Management1G:
      return *portfabric::PortSpeed::parse("1G");
    case SyntheticPortClass::Server10G:
      return *portfabric::PortSpeed::parse("10G");
    case SyntheticPortClass::Server25G:
      return *portfabric::PortSpeed::parse("25G");
    case SyntheticPortClass::Uplink40G:
      return *portfabric::PortSpeed::parse("40G");
    case SyntheticPortClass::Uplink50G:
      return *portfabric::PortSpeed::parse("50G");
    case SyntheticPortClass::Uplink100G:
      return *portfabric::PortSpeed::parse("100G");
    case SyntheticPortClass::Spine200G:
    case SyntheticPortClass::InfiniBandHdr200G:
      return *portfabric::PortSpeed::parse("200G");
    case SyntheticPortClass::Spine400G:
    case SyntheticPortClass::Optical400G:
      return *portfabric::PortSpeed::parse("400G");
    case SyntheticPortClass::Backbone800G:
      return *portfabric::PortSpeed::parse("800G");
  }
  return *portfabric::PortSpeed::parse("10G");
}

std::uint32_t lanes_for(portfabric::SyntheticPortClass port_class) {
  switch (port_class) {
    case portfabric::SyntheticPortClass::Uplink40G:
    case portfabric::SyntheticPortClass::Uplink100G:
    case portfabric::SyntheticPortClass::Spine200G:
    case portfabric::SyntheticPortClass::Optical400G:
      return 4;
    default:
      return 1;
  }
}

portfabric::FecMode fec_for(portfabric::SyntheticPortClass port_class) {
  switch (port_class) {
    case portfabric::SyntheticPortClass::Server25G:
    case portfabric::SyntheticPortClass::Uplink100G:
      return portfabric::FecMode::ReedSolomon528;
    case portfabric::SyntheticPortClass::Uplink50G:
    case portfabric::SyntheticPortClass::Spine200G:
      return portfabric::FecMode::ReedSolomon544;
    default:
      return portfabric::FecMode::None;
  }
}

portfabric::PortConfiguration base_configuration(const portfabric::PortId& port,
                                                 const portfabric::DeviceId& device,
                                                 const portfabric::CapabilityBinding& binding,
                                                 portfabric::SyntheticPortClass port_class) {
  portfabric::PortConfiguration configuration;
  configuration.port = port;
  configuration.parent_device = device;
  configuration.administrative = portfabric::AdministrativeState::Enabled;
  configuration.mode = portfabric::PortMode::Physical;
  configuration.protocol = portfabric::ProtocolFamily::Synthetic;
  configuration.speed.selection = portfabric::SpeedSelection::Forced;
  configuration.speed.rate = speed_for(port_class);
  configuration.duplex = portfabric::DuplexMode::Full;
  configuration.autoneg = portfabric::AutonegPolicy::Forced;
  configuration.fec = fec_for(port_class);
  configuration.pause = portfabric::PauseMode::Disabled;
  configuration.breakout = portfabric::BreakoutMode::None;
  configuration.mtu = *portfabric::Mtu::create(4096);
  configuration.lanes =
      *portfabric::LaneConfiguration::for_total(lanes_for(port_class), speed_for(port_class));
  configuration.capability_generation = binding.generation;
  return configuration;
}
portfabric::MutationEnvelope envelope_for(const portfabric::PortId& port,
                                          const std::string& publisher, const std::string& boot,
                                          const std::string& attempt,
                                          portfabric::CoordinatorEpoch epoch) {
  portfabric::MutationEnvelope envelope;
  envelope.port = port;
  envelope.authority.epoch = epoch;
  envelope.authority.publisher = portfabric::PublisherId::from_validated(publisher);
  envelope.authority.boot = portfabric::WorkerBootId::from_validated(boot);
  envelope.authority.attempt = portfabric::MutationAttemptId::from_validated(attempt);
  envelope.provenance_kind = portfabric::ProvenanceKind::SwitchAgent;
  envelope.provenance_source = publisher;
  return envelope;
}

int report(const char* step, const portfabric::MutationResult& result) {
  std::printf("%-28s %s\n", step, result.outcome.to_string().c_str());
  return result.accepted() ? 0 : 1;
}

}  // namespace

int main() {
  Fixture fixture(portfabric::SyntheticPortClass::Server10G);
  portfabric::PortFabricEngine engine(config_for("example-05", ""), &fixture.provider,
                                      &fixture.adapter);
  const portfabric::PortId port = fixture.port(1);
  portfabric::PortBindingRequest binding;
  binding.port = port;
  binding.parent_device = fixture.device;
  binding.device_generation = portfabric::DeviceGeneration::from_value(1);
  binding.epoch = engine.epoch();
  binding.attempt = portfabric::MutationAttemptId::from_validated("bind-1");
  (void)engine.bind_port(binding);
  const auto capability = engine.capability_binding(port);
  portfabric::OwnershipRequest claim;
  claim.envelope = envelope_for(port, "agent-a", "boot-a", "claim-1", engine.epoch());
  claim.owner_kind = portfabric::PortOwnerKind::SwitchAgent;
  claim.owner = portfabric::OwnerId::from_validated("owner-a");
  (void)engine.claim_ownership(claim);
  const auto ownership = engine.ownership(port);

  // A 400G request against a 10G class port.
  portfabric::ConfigureRequest configure;
  configure.envelope = envelope_for(port, "agent-a", "boot-a", "configure-400g", engine.epoch());
  configure.envelope.authority.ownership = ownership->id;
  configure.configuration = base_configuration(port, fixture.device, *capability,
                                               portfabric::SyntheticPortClass::Server10G);
  // Ask for a rate this port class does not advertise.
  configure.configuration.speed.rate = *portfabric::PortSpeed::parse("400G");
  configure.configuration.lanes =
      *portfabric::LaneConfiguration::for_total(8, *portfabric::PortSpeed::parse("400G"));
  const portfabric::MutationResult unsupported = engine.configure(configure);
  std::printf("unsupported_400g           %s\n", unsupported.outcome.to_string().c_str());
  std::printf("reason=%s\n", unsupported.explanation.to_string().c_str());

  // The supported rate is accepted.
  configure.envelope.authority.attempt = portfabric::MutationAttemptId::from_validated("configure-10g");
  configure.configuration.speed.rate = *portfabric::PortSpeed::parse("10G");
  configure.configuration.lanes = *portfabric::LaneConfiguration::for_total(1, *portfabric::PortSpeed::parse("10G"));
  configure.configuration.fec = portfabric::FecMode::None;
  if (report("configure_10g", engine.configure(configure)) != 0) {
    return 1;
  }

  // Capability truth advances and removes the committed rate.
  portfabric::SyntheticFabric& raw = fixture.fabric;
  (void)raw;
  portfabric::CapabilityRequest rebind;
  rebind.envelope = envelope_for(port, "agent-a", "boot-a", "rebind-caps", engine.epoch());
  rebind.envelope.authority.ownership = ownership->id;
  rebind.binding = *fixture.fabric.binding_for(port);
  rebind.binding.generation = portfabric::CapabilityBindingGeneration::from_value(
      capability->generation.value() + 1);
  rebind.binding.evidence_generation = portfabric::EvidenceGeneration::from_value(
      capability->evidence_generation.value() + 1);
  rebind.binding.capabilities.supported_speeds.clear();
  rebind.binding.capabilities.supported_speeds.push_back(*portfabric::PortSpeed::parse("100G"));
  if (report("bind_capabilities_v2", engine.bind_capabilities(rebind)) != 0) {
    return 1;
  }
  std::printf("lifecycle_after_capability_change=%s\n",
              std::string(portfabric::to_string(*engine.lifecycle(port))).c_str());
  std::printf("requires_revalidation=%zu\n", engine.ports_requiring_revalidation().size());
  return 0;
}
