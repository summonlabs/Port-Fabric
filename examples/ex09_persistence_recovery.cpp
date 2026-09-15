// Example 9: persistence and conservative recovery. Durable desired
// configuration survives a restart; live process authority and verified
// application evidence do not.
#include <cstdio>
#include <string>

#include "portfabric/engine.hpp"
#include "portfabric/platform.hpp"
#include "portfabric/synthetic.hpp"

namespace {

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

portfabric::EngineConfig config_for(const std::string& coordinator, const std::string& path,
                                    std::uint64_t epoch) {
  portfabric::EngineConfig config;
  config.fabric = portfabric::FabricId::from_validated("fabric-a");
  config.site = portfabric::SiteId::from_validated("site-a");
  config.coordinator = portfabric::PublisherId::from_validated(coordinator);
  config.epoch = portfabric::CoordinatorEpoch::from_value(epoch);
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

/// Binds, claims and configures one port. Returns true when every step was
/// accepted.
bool prepare_port(portfabric::PortFabricEngine& engine, const Fixture& fixture,
                  const portfabric::PortId& port, const std::string& publisher,
                  const std::string& boot, portfabric::DeviceGeneration device_generation,
                  std::uint64_t device_generation_value) {
  portfabric::PortBindingRequest binding;
  binding.port = port;
  binding.parent_device = fixture.device;
  binding.device_generation = device_generation;
  binding.epoch = engine.epoch();
  binding.attempt = portfabric::MutationAttemptId::from_validated("bind-" + std::string(boot));
  const auto binding_result = engine.bind_port(binding);
  std::printf("bind                        %s\n", binding_result.outcome.to_string().c_str());
  if (!binding_result.accepted()) {
    return false;
  }
  (void)device_generation_value;
  const auto capability = engine.capability_binding(port);
  portfabric::OwnershipRequest claim;
  claim.envelope = envelope_for(port, publisher, boot, "claim-" + boot, engine.epoch());
  claim.owner_kind = portfabric::PortOwnerKind::SwitchAgent;
  claim.owner = portfabric::OwnerId::from_validated("owner-" + publisher);
  const auto claim_result = engine.claim_ownership(claim);
  std::printf("claim                       %s\n", claim_result.outcome.to_string().c_str());
  if (!claim_result.accepted()) {
    return false;
  }
  const auto ownership = engine.ownership(port);
  portfabric::ConfigureRequest configure;
  configure.envelope = envelope_for(port, publisher, boot, "configure-" + boot, engine.epoch());
  configure.envelope.authority.ownership = ownership->id;
  configure.configuration = base_configuration(port, fixture.device, *capability, portfabric::SyntheticPortClass::Spine400G);
  const auto configure_result = engine.configure(configure);
  std::printf("configure                   %s\n", configure_result.outcome.to_string().c_str());
  return configure_result.accepted();
}

}  // namespace

int main() {
  const std::string path = "ex09-state.pfstate";
  (void)portfabric::remove_file(path);
  const portfabric::PortId port = portfabric::PortId::from_validated("syn-sw1-p01");
  std::string first_generation;

  {
    Fixture fixture(portfabric::SyntheticPortClass::Spine400G);
    portfabric::PortFabricEngine engine(config_for("coordinator", path, 1), &fixture.provider,
                                        &fixture.adapter);
    if (!prepare_port(engine, fixture, port, "agent-a", "boot-a",
                      portfabric::DeviceGeneration::from_value(1), 1)) {
      return 1;
    }
    first_generation = engine.configuration(port)->generation.to_string();
    std::printf("committed_generation=%s\n", first_generation.c_str());
    std::printf("applied=%s\n",
                std::string(portfabric::to_string(engine.applied_evidence(port)->outcome)).c_str());
  }

  {
    Fixture fixture(portfabric::SyntheticPortClass::Spine400G);
    portfabric::PortFabricEngine engine(config_for("coordinator", path, 1), &fixture.provider,
                                        &fixture.adapter);
    const portfabric::Outcome loaded = engine.load();
    std::printf("load=%s\n", loaded.to_string().c_str());
    if (!loaded.ok()) {
      return 1;
    }
    portfabric::CoordinatorEpoch assigned;
    (void)engine.begin_epoch(assigned);
    std::printf("epoch_after_restart=%s\n", assigned.to_string().c_str());
    std::printf("recovered_generation=%s\n", engine.configuration(port)->generation.to_string().c_str());
    std::printf("lifecycle_after_recovery=%s\n",
                std::string(portfabric::to_string(*engine.lifecycle(port))).c_str());
    std::printf("ownership_after_recovery=%s\n",
                engine.ownership(port).has_value() ? "held" : "released");
    std::printf("applied_after_recovery=%s\n",
                std::string(portfabric::to_string(engine.applied_evidence(port)->outcome)).c_str());
    std::printf("explanation=%s\n", engine.explain(port).to_string().c_str());
  }
  (void)portfabric::remove_file(path);
  return 0;
}
