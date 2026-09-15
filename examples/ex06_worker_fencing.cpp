// Example 6: process incarnation fencing. A worker boot is a process life, not a
// name: a fenced boot can never mutate again, and a restart requires a new boot
// identity.
#include <thread>
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
  configure.configuration = base_configuration(port, fixture.device, *capability, portfabric::SyntheticPortClass::Server25G);
  const auto configure_result = engine.configure(configure);
  std::printf("configure                   %s\n", configure_result.outcome.to_string().c_str());
  return configure_result.accepted();
}

}  // namespace

int main() {
  Fixture fixture(portfabric::SyntheticPortClass::Server25G);
  portfabric::PortFabricEngine engine(config_for("coordinator", "", 1), &fixture.provider,
                                      &fixture.adapter);
  const portfabric::PortId port = fixture.port(1);
  if (!prepare_port(engine, fixture, port, "agent-a", "boot-a",
                    portfabric::DeviceGeneration::from_value(1), 1)) {
    return 1;
  }
  const auto generation = engine.configuration(port)->generation;
  const auto ownership = engine.ownership(port);

  // The worker process dies. The coordinator fences its incarnation.
  std::printf("fence_publisher             %s\n",
              engine.fence_publisher(ownership->publisher, ownership->boot).to_string().c_str());

  portfabric::AdministrativeRequest replay;
  replay.envelope = envelope_for(port, "agent-a", "boot-a", "replay-after-death", engine.epoch());
  replay.envelope.authority.ownership = ownership->id;
  replay.envelope.authority.expected_configuration = generation;
  replay.op = portfabric::AdministrativeOp::Disable;
  const portfabric::MutationResult fenced = engine.administrative(replay);
  std::printf("old_boot_replay             %s\n", fenced.outcome.to_string().c_str());

  // A restart must present a fresh boot identity.
  std::printf("register_new_boot           %s\n",
              engine.register_publisher(portfabric::PublisherId::from_validated("agent-a"),
                                        portfabric::WorkerBootId::from_validated("boot-b"),
                                        engine.epoch())
                  .to_string()
                  .c_str());
  // Fencing released the ownership the dead incarnation held, so the fresh boot
  // has to claim the port before it can mutate it.
  portfabric::AdministrativeRequest fresh = replay;
  fresh.envelope = envelope_for(port, "agent-a", "boot-b", "fresh-boot", engine.epoch());
  fresh.envelope.authority.ownership = ownership->id;
  fresh.envelope.authority.expected_configuration = generation;
  const portfabric::MutationResult without_claim = engine.administrative(fresh);
  std::printf("fresh_boot_without_claim    %s\n", without_claim.outcome.to_string().c_str());

  portfabric::OwnershipRequest reclaim;
  reclaim.envelope = envelope_for(port, "agent-a", "boot-b", "reclaim", engine.epoch());
  reclaim.owner_kind = portfabric::PortOwnerKind::SwitchAgent;
  reclaim.owner = portfabric::OwnerId::from_validated("owner-agent-a");
  const portfabric::MutationResult reclaimed = engine.claim_ownership(reclaim);
  std::printf("fresh_boot_claim            %s\n", reclaimed.outcome.to_string().c_str());

  fresh.envelope.authority.ownership = engine.ownership(port)->id;
  const portfabric::MutationResult after_claim = engine.administrative(fresh);
  std::printf("fresh_boot_attempt          %s\n", after_claim.outcome.to_string().c_str());
  std::printf("durable_configuration_generation=%s\n",
              engine.configuration(port)->generation.to_string().c_str());
  return 0;
}
