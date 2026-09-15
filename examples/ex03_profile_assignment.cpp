// Example 3: generation bound profiles. A newer profile generation never
// silently rewrites an already committed port configuration.
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
  Fixture fixture(portfabric::SyntheticPortClass::Spine400G);
  portfabric::PortFabricEngine engine(config_for("example-03", ""), &fixture.provider,
                                      &fixture.adapter);
  const portfabric::PortId port = fixture.port(1);

  portfabric::PortProfile profile;
  profile.id = portfabric::PortProfileId::from_validated("fabric-uplink");
  profile.generation = portfabric::PortProfileGeneration::from_value(1);
  profile.name = "fabric-uplink";
  profile.description = "high speed fabric uplink";
  profile.configuration.specifies_speed = true;
  profile.configuration.speed.selection = portfabric::SpeedSelection::Forced;
  profile.configuration.speed.rate = *portfabric::PortSpeed::parse("400G");
  profile.configuration.specifies_mtu = true;
  profile.configuration.mtu = *portfabric::Mtu::create(9216);
  profile.configuration.specifies_fec = true;
  profile.configuration.fec = portfabric::FecMode::ReedSolomon544Interleaved;
  profile.configuration.specifies_role = true;
  profile.configuration.role = "uplink";
  profile.provenance.kind = portfabric::ProvenanceKind::Operator;
  profile.provenance.epoch = engine.epoch();
  profile.provenance.attempt = portfabric::MutationAttemptId::from_validated("profile-1");
  std::printf("define_profile_v1            %s\n", engine.define_profile(profile).to_string().c_str());

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
  portfabric::ConfigureRequest configure;
  configure.envelope = envelope_for(port, "agent-a", "boot-a", "configure-1", engine.epoch());
  configure.envelope.authority.ownership = ownership->id;
  configure.configuration = base_configuration(port, fixture.device, *capability, portfabric::SyntheticPortClass::Spine400G);
  if (report("configure", engine.configure(configure)) != 0) {
    return 1;
  }

  portfabric::ProfileRequest assign;
  assign.envelope = envelope_for(port, "agent-a", "boot-a", "assign-1", engine.epoch());
  assign.envelope.authority.ownership = ownership->id;
  assign.envelope.authority.expected_configuration = engine.configuration(port)->generation;
  assign.profile = profile.id;
  assign.generation = portfabric::PortProfileGeneration::from_value(1);
  if (report("assign_profile_v1", engine.assign_profile(assign)) != 0) {
    return 1;
  }
  std::printf("mtu_after_v1=%u\n", engine.configuration(port)->mtu.value());

  profile.generation = portfabric::PortProfileGeneration::from_value(2);
  profile.configuration.mtu = *portfabric::Mtu::create(4096);
  profile.provenance.attempt = portfabric::MutationAttemptId::from_validated("profile-2");
  std::printf("define_profile_v2            %s\n", engine.define_profile(profile).to_string().c_str());
  std::printf("mtu_after_v2=%u\n", engine.configuration(port)->mtu.value());
  const std::vector<portfabric::PortId> stale = engine.ports_with_stale_profile_binding();
  std::printf("stale_profile_bindings=%zu\n", stale.size());

  assign.envelope.authority.attempt = portfabric::MutationAttemptId::from_validated("assign-2");
  assign.envelope.authority.expected_configuration = engine.configuration(port)->generation;
  assign.generation = portfabric::PortProfileGeneration::from_value(2);
  if (report("assign_profile_v2", engine.assign_profile(assign)) != 0) {
    return 1;
  }
  std::printf("mtu_after_explicit_v2=%u\n", engine.configuration(port)->mtu.value());
  return 0;
}
