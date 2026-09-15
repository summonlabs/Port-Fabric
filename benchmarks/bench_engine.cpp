// Port Fabric benchmarks.
//
// Every measurement counts completed work: a mutation is measured only after the
// runtime returned an authoritative outcome. Nothing here benchmarks submitted
// or asynchronous work as if it had completed.

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "portfabric/engine.hpp"
#include "portfabric/platform.hpp"
#include "portfabric/synthetic.hpp"

namespace {

using Clock = std::chrono::steady_clock;

double milliseconds_since(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void report(const char* name, std::size_t operations, double milliseconds) {
  const double per_operation =
      operations == 0 ? 0.0 : (milliseconds * 1000.0) / static_cast<double>(operations);
  std::printf("%-44s operations=%-8zu total_ms=%-10.3f per_operation_us=%.3f\n", name, operations,
              milliseconds, per_operation);
}

struct Harness {
  static constexpr std::uint32_t kPortsPerDevice = 4096;

  portfabric::SyntheticFabric fabric{"bench-synthetic"};
  portfabric::SyntheticCapabilityProvider provider{fabric};
  portfabric::SyntheticAdapter adapter{fabric};
  std::vector<portfabric::DeviceId> devices;
  /// First device of the inventory.
  portfabric::DeviceId device;

  explicit Harness(std::uint32_t port_count) {
    // One device carries at most max_ports_per_device ports, so a large inventory
    // is split across devices exactly as a real fleet would be.
    std::uint32_t remaining = port_count == 0 ? 1 : port_count;
    std::uint32_t device_index = 0;
    while (remaining > 0) {
      const std::uint32_t chunk = remaining > kPortsPerDevice ? kPortsPerDevice : remaining;
      std::string label = "bench";
      if (device_index > 0) {
        label.append("-").append(std::to_string(device_index));
      }
      const portfabric::DeviceId created = fabric.add_device(
          label, portfabric::SyntheticPortClass::Spine400G, chunk,
          portfabric::PortEntityClass::SyntheticPort);
      if (created.valid()) {
        devices.push_back(created);
      }
      remaining -= chunk;
      ++device_index;
    }
    device = devices.empty() ? portfabric::DeviceId{} : devices.front();
  }

  /// Device that carries the index-th port (one based).
  portfabric::DeviceId device_for(std::uint32_t index) const {
    const std::uint32_t zero_based = index == 0 ? 0 : index - 1;
    const std::uint32_t device_index = zero_based / kPortsPerDevice;
    if (device_index < devices.size()) {
      return devices[device_index];
    }
    return device;
  }

  /// Identity of the index-th port (one based), shared with every device chunk.
  portfabric::PortId port(std::uint32_t index) const {
    const std::uint32_t zero_based = index == 0 ? 0 : index - 1;
    const std::uint32_t device_index = zero_based / kPortsPerDevice;
    const std::uint32_t local = zero_based % kPortsPerDevice + 1;
    std::string label = "bench";
    if (device_index > 0) {
      label.append("-").append(std::to_string(device_index));
    }
    const std::string digits = local < 10 ? "0" + std::to_string(local) : std::to_string(local);
    return portfabric::PortId::from_validated("syn-" + label + "-p" + digits);
  }
};

portfabric::EngineConfig bench_config() {
  portfabric::EngineConfig config;
  config.fabric = portfabric::FabricId::from_validated("bench-fabric");
  config.site = portfabric::SiteId::from_validated("bench-site");
  config.coordinator = portfabric::PublisherId::from_validated("bench-coordinator");
  config.epoch = portfabric::CoordinatorEpoch::from_value(1);
  config.require_ownership = true;
  return config;
}

void run_scale(std::uint32_t port_count) {
  Harness harness(port_count);
  portfabric::PortFabricEngine engine(bench_config(), &harness.provider, &harness.adapter);
  std::printf("\n--- scale: %u ports ---\n", port_count);

  std::vector<portfabric::PortId> ports;
  ports.reserve(port_count);

  Clock::time_point start = Clock::now();
  for (std::uint32_t index = 1; index <= port_count; ++index) {
    const portfabric::PortId port = harness.port(index);
    portfabric::PortBindingRequest binding;
    binding.port = port;
    binding.parent_device = harness.device_for(index);
    binding.device_generation = portfabric::DeviceGeneration::from_value(1);
    binding.epoch = engine.epoch();
    binding.attempt = portfabric::MutationAttemptId::from_validated("bind-" + std::to_string(index));
    const portfabric::MutationResult result = engine.bind_port(binding);
    if (!result.accepted()) {
      std::printf("binding failed: %s\n", result.outcome.to_string().c_str());
      return;
    }
    ports.push_back(port);
  }
  report("port binding (completed)", ports.size(), milliseconds_since(start));

  start = Clock::now();
  for (std::size_t index = 0; index < ports.size(); ++index) {
    const portfabric::PortId& port = ports[index];
    portfabric::OwnershipRequest claim;
    claim.envelope.port = port;
    claim.envelope.authority.epoch = engine.epoch();
    claim.envelope.authority.publisher = portfabric::PublisherId::from_validated("bench-agent");
    claim.envelope.authority.boot = portfabric::WorkerBootId::from_validated("bench-boot");
    claim.envelope.authority.attempt = portfabric::MutationAttemptId::from_validated(
        "claim-" + std::to_string(index));
    claim.owner_kind = portfabric::PortOwnerKind::SwitchAgent;
    claim.owner = portfabric::OwnerId::from_validated("bench-owner");
    if (!engine.claim_ownership(claim).accepted()) {
      std::printf("ownership claim failed\n");
      return;
    }
  }
  report("ownership claim (completed)", ports.size(), milliseconds_since(start));

  start = Clock::now();
  for (std::size_t index = 0; index < ports.size(); ++index) {
    const portfabric::PortId& port = ports[index];
    const auto capability = engine.capability_binding(port);
    const auto ownership = engine.ownership(port);
    portfabric::ConfigureRequest configure;
    configure.envelope.port = port;
    configure.envelope.authority.epoch = engine.epoch();
    configure.envelope.authority.publisher = portfabric::PublisherId::from_validated("bench-agent");
    configure.envelope.authority.boot = portfabric::WorkerBootId::from_validated("bench-boot");
    configure.envelope.authority.ownership = ownership->id;
    configure.envelope.authority.attempt = portfabric::MutationAttemptId::from_validated(
        "configure-" + std::to_string(index));
    configure.configuration.port = port;
    // The configure loop is zero based while the inventory helpers are one based.
    configure.configuration.parent_device =
        harness.device_for(static_cast<std::uint32_t>(index) + 1);
    configure.configuration.administrative = portfabric::AdministrativeState::Enabled;
    configure.configuration.mode = portfabric::PortMode::Physical;
    configure.configuration.protocol = portfabric::ProtocolFamily::Synthetic;
    configure.configuration.speed.selection = portfabric::SpeedSelection::Forced;
    configure.configuration.speed.rate = *portfabric::PortSpeed::parse("400G");
    configure.configuration.duplex = portfabric::DuplexMode::Full;
    configure.configuration.autoneg = portfabric::AutonegPolicy::Forced;
    configure.configuration.fec = portfabric::FecMode::None;
    configure.configuration.pause = portfabric::PauseMode::Disabled;
    configure.configuration.mtu = *portfabric::Mtu::create(4096);
    configure.configuration.lanes =
        *portfabric::LaneConfiguration::for_total(8, *portfabric::PortSpeed::parse("400G"));
    configure.configuration.capability_generation = capability->generation;
    const portfabric::MutationResult configured = engine.configure(configure);
    if (!configured.accepted()) {
      std::printf("configuration failed at port %s: %s\n", port.to_string().c_str(),
                  configured.outcome.to_string().c_str());
      return;
    }
  }
  report("configuration commit (completed)", ports.size(), milliseconds_since(start));

  start = Clock::now();
  std::size_t lookups = 0;
  for (int round = 0; round < 10; ++round) {
    for (const portfabric::PortId& port : ports) {
      if (engine.configuration(port).has_value()) {
        ++lookups;
      }
    }
  }
  report("configuration lookup (completed)", lookups, milliseconds_since(start));

  start = Clock::now();
  for (int round = 0; round < 10; ++round) {
    (void)engine.ports_requiring_revalidation();
    (void)engine.ports_with_lifecycle(portfabric::PortLifecycle::Configured);
  }
  report("indexed queries (completed)", static_cast<std::size_t>(20), milliseconds_since(start));

  start = Clock::now();
  const std::size_t snapshots = 10;
  for (std::size_t index = 0; index < snapshots; ++index) {
    (void)engine.snapshot();
  }
  report("snapshot construction (completed)", snapshots, milliseconds_since(start));

  start = Clock::now();
  const std::size_t threads = 4;
  std::vector<std::thread> readers;
  std::atomic<std::size_t> reads{0};
  for (std::size_t index = 0; index < threads; ++index) {
    readers.emplace_back([&engine, &ports, &reads]() {
      for (const portfabric::PortId& port : ports) {
        if (engine.lifecycle(port).has_value()) {
          reads.fetch_add(1);
        }
      }
    });
  }
  for (std::thread& thread : readers) {
    thread.join();
  }
  report("concurrent reads (completed)", reads.load(), milliseconds_since(start));
}

}  // namespace

int main() {
  std::printf("Port Fabric benchmarks\n");
  for (const std::uint32_t count : {1000u, 10000u, 100000u}) {
    run_scale(count);
  }

  // Persistence round trip at 10k ports.
  {
    const std::uint32_t port_count = 10000;
    Harness harness(port_count);
    portfabric::EngineConfig config = bench_config();
    config.persistence_path = "bench-state.pfstate";
    config.persistence = portfabric::PersistenceMode::Manual;
    portfabric::PortFabricEngine engine(config, &harness.provider, &harness.adapter);
    for (std::uint32_t index = 1; index <= port_count; ++index) {
      const portfabric::PortId port = harness.port(index);
      portfabric::PortBindingRequest binding;
      binding.port = port;
      binding.parent_device = harness.device_for(index);
      binding.device_generation = portfabric::DeviceGeneration::from_value(1);
      binding.epoch = engine.epoch();
      binding.attempt = portfabric::MutationAttemptId::from_validated("bind-" + std::to_string(index));
      (void)engine.bind_port(binding);
    }
    Clock::time_point start = Clock::now();
    const portfabric::Outcome saved = engine.save();
    report("persistence save (completed, 10k ports)", saved.ok() ? 1 : 0, milliseconds_since(start));
    start = Clock::now();
    const portfabric::Outcome loaded = engine.load();
    report("persistence load (completed, 10k ports)", loaded.ok() ? 1 : 0,
           milliseconds_since(start));
    std::printf("persistence result: save=%s load=%s\n", saved.to_string().c_str(),
                loaded.to_string().c_str());
    (void)portfabric::remove_file("bench-state.pfstate");
  }
  return 0;
}
