// Independent consumer of the installed Port Fabric package.
//
// It exercises the published surface only: create a runtime, bind a canonical
// port, commit a synthetic configuration, read it back, take a snapshot and shut
// down. No source-tree header is reachable from here.

#include <cstdio>
#include <string>

#include <portfabric/engine.hpp>
#include <portfabric/platform.hpp>
#include <portfabric/synthetic.hpp>
#include <portfabric/version.hpp>

int main() {
  std::printf("Port Fabric consumer, runtime version %s\n",
              std::string(portfabric::version_string()).c_str());

  portfabric::SyntheticFabric fabric("consumer-synthetic");
  const portfabric::DeviceId device = fabric.add_device(
      "consumer", portfabric::SyntheticPortClass::Spine400G, 2,
      portfabric::PortEntityClass::SyntheticPort);
  portfabric::SyntheticCapabilityProvider provider(fabric);
  portfabric::SyntheticAdapter adapter(fabric);

  portfabric::EngineConfig config;
  config.fabric = portfabric::FabricId::from_validated("consumer-fabric");
  config.site = portfabric::SiteId::from_validated("consumer-site");
  config.coordinator = portfabric::PublisherId::from_validated("consumer");
  config.epoch = portfabric::CoordinatorEpoch::from_value(1);
  config.adapter_policy = portfabric::AdapterPolicy::ApplyAndVerify;

  portfabric::PortFabricEngine engine(config, &provider, &adapter);
  const portfabric::PortId port = portfabric::PortId::from_validated("syn-consumer-p01");

  portfabric::PortBindingRequest binding;
  binding.port = port;
  binding.parent_device = device;
  binding.device_generation = portfabric::DeviceGeneration::from_value(1);
  binding.epoch = engine.epoch();
  binding.attempt = portfabric::MutationAttemptId::from_validated("bind-1");
  const portfabric::MutationResult bound = engine.bind_port(binding);
  if (!bound.accepted()) {
    std::printf("bind failed: %s\n", bound.outcome.to_string().c_str());
    return 1;
  }

  portfabric::OwnershipRequest claim;
  claim.envelope.port = port;
  claim.envelope.authority.epoch = engine.epoch();
  claim.envelope.authority.publisher = portfabric::PublisherId::from_validated("consumer-agent");
  claim.envelope.authority.boot = portfabric::WorkerBootId::from_validated("consumer-boot");
  claim.envelope.authority.attempt = portfabric::MutationAttemptId::from_validated("claim-1");
  claim.owner_kind = portfabric::PortOwnerKind::HostAgent;
  claim.owner = portfabric::OwnerId::from_validated("consumer-owner");
  const portfabric::MutationResult claimed = engine.claim_ownership(claim);
  if (!claimed.accepted()) {
    std::printf("claim failed: %s\n", claimed.outcome.to_string().c_str());
    return 1;
  }

  const auto capability = engine.capability_binding(port);
  if (!capability.has_value()) {
    std::printf("no capability evidence\n");
    return 1;
  }
  portfabric::PortConfiguration configuration;
  configuration.port = port;
  configuration.parent_device = device;
  configuration.administrative = portfabric::AdministrativeState::Enabled;
  configuration.mode = portfabric::PortMode::Physical;
  configuration.protocol = portfabric::ProtocolFamily::Synthetic;
  configuration.speed.selection = portfabric::SpeedSelection::Forced;
  configuration.speed.rate = *portfabric::PortSpeed::parse("400G");
  configuration.duplex = portfabric::DuplexMode::Full;
  configuration.autoneg = portfabric::AutonegPolicy::Forced;
  configuration.fec = portfabric::FecMode::None;
  configuration.pause = portfabric::PauseMode::Disabled;
  configuration.mtu = *portfabric::Mtu::create(4096);
  configuration.lanes =
      *portfabric::LaneConfiguration::for_total(8, *portfabric::PortSpeed::parse("400G"));
  configuration.capability_generation = capability->generation;

  portfabric::ConfigureRequest configure;
  configure.envelope.port = port;
  configure.envelope.authority.epoch = engine.epoch();
  configure.envelope.authority.publisher = portfabric::PublisherId::from_validated("consumer-agent");
  configure.envelope.authority.boot = portfabric::WorkerBootId::from_validated("consumer-boot");
  configure.envelope.authority.ownership = engine.ownership(port)->id;
  configure.envelope.authority.attempt = portfabric::MutationAttemptId::from_validated("configure-1");
  configure.configuration = configuration;
  const portfabric::MutationResult configured = engine.configure(configure);
  if (!configured.accepted()) {
    std::printf("configure failed: %s\n", configured.outcome.to_string().c_str());
    return 1;
  }

  const auto committed = engine.configuration(port);
  const auto lifecycle = engine.lifecycle(port);
  if (!committed.has_value() || !lifecycle.has_value()) {
    std::printf("query failed\n");
    return 1;
  }
  std::printf("lifecycle=%s generation=%s speed=%s mtu=%u applied=%s\n",
              std::string(portfabric::to_string(*lifecycle)).c_str(),
              committed->generation.to_string().c_str(), committed->speed.rate.to_string().c_str(),
              committed->mtu.value(),
              std::string(portfabric::to_string(engine.applied_evidence(port)->outcome)).c_str());

  const portfabric::Snapshot snapshot = engine.snapshot();
  std::printf("snapshot=%s ports=%zu\n", snapshot.to_string().c_str(), snapshot.port_count());
  std::printf("consumer ok\n");
  return 0;
}
