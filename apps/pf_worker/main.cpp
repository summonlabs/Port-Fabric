// Port Fabric port agent.
//
// The agent is a real, separately killable process. It connects to a
// coordinator, registers one publisher incarnation, claims control ownership of
// a port and (optionally) commits a configuration. Two modes exist:
//
//   agent  - claim, configure and then stay alive until a stop file appears or
//            the process is killed outright.
//   replay - attempt one mutation with a caller supplied (publisher, boot) pair
//            and print the deterministic outcome code. This is how a stale
//            incarnation is proven to be fenced after a real process death.
//
// Every meaningful line is "key=value" so the proofs can parse it.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "portfabric/client.hpp"
#include "portfabric/platform.hpp"
#include "portfabric/synthetic.hpp"
#include "portfabric/version.hpp"

namespace {

struct Options {
  std::string address = "127.0.0.1";
  /// Coordinator control port.
  std::uint16_t port_number = 0;
  std::string publisher = "agent-a";
  std::string boot = "boot-a";
  std::string role = "switch-agent";
  std::string owner = "owner-a";
  std::string owner_kind = "SWITCH_AGENT";
  std::string port;
  std::string device = "coordinator-device";
  std::string stop_file;
  std::string mode = "agent";
  bool claim = true;
  bool configure = true;
  std::string speed = "400G";
  std::uint32_t mtu = 4096;
  std::uint64_t config_generation = 0;
};

void print_usage() {
  std::printf(
      "pf-worker - Port Fabric port agent %s\n"
      "usage: pf-worker --fabric-port <id> [options]\n"
      "  --address <ip> --port-number <n>   coordinator endpoint\n"
      "  --publisher <id> --boot <id>       process incarnation identity\n"
      "  --owner <id> --owner-kind <kind>   control ownership identity\n"
      "  --fabric-port <id>                 canonical port to govern\n"
      "  --mode agent|replay                operating mode\n"
      "  --no-claim --no-configure          skip ownership or configuration\n"
      "  --speed <rate> --mtu <n>           configuration payload\n"
      "  --stop-file <path>                 exit gracefully when the file appears\n",
      std::string(portfabric::version_string()).c_str());
}

bool parse(int argc, char** argv, Options& options) {
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    auto next = [&](std::string& target) {
      if (index + 1 < argc) {
        target = argv[++index];
      }
    };
    if (argument == "--address") {
      next(options.address);
    } else if (argument == "--port-number") {
      std::string value;
      next(value);
      options.port_number = static_cast<std::uint16_t>(std::strtoul(value.c_str(), nullptr, 10));
    } else if (argument == "--publisher") {
      next(options.publisher);
    } else if (argument == "--boot") {
      next(options.boot);
    } else if (argument == "--role") {
      next(options.role);
    } else if (argument == "--owner") {
      next(options.owner);
    } else if (argument == "--owner-kind") {
      next(options.owner_kind);
    } else if (argument == "--fabric-port") {
      next(options.port);
    } else if (argument == "--device") {
      next(options.device);
    } else if (argument == "--stop-file") {
      next(options.stop_file);
    } else if (argument == "--mode") {
      next(options.mode);
    } else if (argument == "--no-claim") {
      options.claim = false;
    } else if (argument == "--no-configure") {
      options.configure = false;
    } else if (argument == "--speed") {
      next(options.speed);
    } else if (argument == "--mtu") {
      std::string value;
      next(value);
      options.mtu = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
    } else if (argument == "--generation") {
      std::string value;
      next(value);
      options.config_generation = std::strtoull(value.c_str(), nullptr, 10);
    } else if (argument == "--help" || argument == "-h") {
      print_usage();
      return false;
    } else {
      std::fprintf(stderr, "unknown option: %s\n", argument.c_str());
      return false;
    }
  }
  if (options.port.empty()) {
    std::fprintf(stderr, "--fabric-port <canonical port identity> is required\n");
    return false;
  }
  return true;
}

std::string outcome_text(const portfabric::MutationResult& result) {
  return std::string(portfabric::to_string(result.outcome.code()));
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parse(argc, argv, options)) {
    print_usage();
    return 2;
  }

  portfabric::SyntheticFabric fabric("worker-synthetic");
  portfabric::SyntheticCapabilityProvider provider(fabric);

  portfabric::ClientConfig client_config;
  client_config.address = options.address;
  client_config.port = options.port_number;
  client_config.publisher = portfabric::PublisherId::from_validated(options.publisher);
  client_config.boot = portfabric::WorkerBootId::from_validated(options.boot);
  client_config.role = options.role;

  portfabric::FabricClient client(client_config);
  const portfabric::Outcome connected = client.connect();
  std::printf("connect=%s\n", connected.to_string().c_str());
  if (!connected.ok()) {
    std::fflush(stdout);
    return 1;
  }
  std::printf("epoch=%s\n", client.epoch().to_string().c_str());
  const portfabric::Outcome registered = client.register_publisher();
  std::printf("register=%s\n", registered.to_string().c_str());
  // In replay mode a refused registration is part of the proof: the mutation is
  // still attempted so that the deterministic rejection is observable.
  if (!registered.accepted() && options.mode != "replay") {
    std::fflush(stdout);
    return 1;
  }
  const portfabric::PortId port = portfabric::PortId::from_validated(options.port);
  if (!port.valid()) {
    std::printf("result=MALFORMED_REQUEST\n");
    std::fflush(stdout);
    return 2;
  }

  if (options.mode == "replay") {
    // Attempt one mutation with the caller supplied incarnation. A fenced
    // incarnation must be rejected before any semantic validation happens.
    portfabric::PortMutationMessage message;
    message.kind = static_cast<std::uint8_t>(portfabric::PortMutationKind::Administrative);
    message.port = port;
    message.authority.epoch = client.epoch();
    message.authority.publisher = client_config.publisher;
    message.authority.boot = client_config.boot;
    message.authority.expected_configuration =
        portfabric::PortConfigurationGeneration::from_value(options.config_generation);
    message.authority.attempt = portfabric::MutationAttemptId::from_validated(
        "replay-" + options.publisher + "-" + options.boot);
    message.administrative_op = static_cast<std::uint8_t>(portfabric::AdministrativeOp::Enable);
    message.provenance_kind = static_cast<std::uint8_t>(portfabric::ProvenanceKind::SwitchAgent);
    message.provenance_source = options.publisher;
    const portfabric::MutationResult result = client.mutate(message);
    std::printf("result=%s\n", outcome_text(result).c_str());
    std::printf("message=%s\n", result.outcome.message().c_str());
    std::fflush(stdout);
    client.close();
    return result.accepted() ? 0 : 3;
  }

  portfabric::PortOwnerKind owner_kind = portfabric::PortOwnerKind::SwitchAgent;
  (void)portfabric::parse_owner_kind(options.owner_kind, owner_kind);
  const portfabric::OwnerId owner = portfabric::OwnerId::from_validated(options.owner);

  portfabric::PortOwnershipId ownership_id;
  if (options.claim) {
    portfabric::PortMutationMessage claim;
    claim.kind = static_cast<std::uint8_t>(portfabric::PortMutationKind::ClaimOwnership);
    claim.port = port;
    claim.authority.epoch = client.epoch();
    claim.authority.publisher = client_config.publisher;
    claim.authority.boot = client_config.boot;
    claim.authority.expected_configuration =
        portfabric::PortConfigurationGeneration::from_value(options.config_generation);
    claim.authority.attempt = portfabric::MutationAttemptId::from_validated(
        "claim-" + options.publisher + "-" + options.boot);
    claim.owner_kind = static_cast<std::uint8_t>(owner_kind);
    claim.owner = owner;
    claim.exclusive = true;
    claim.provenance_kind = static_cast<std::uint8_t>(portfabric::ProvenanceKind::SwitchAgent);
    claim.provenance_source = options.publisher;
    const portfabric::MutationResult result = client.mutate(claim);
    std::printf("claim=%s\n", outcome_text(result).c_str());
    if (!result.accepted()) {
      std::printf("message=%s\n", result.outcome.message().c_str());
      std::fflush(stdout);
      client.close();
      return 1;
    }
    portfabric::QueryMessage query;
    query.kind = portfabric::QueryKind::Port;
    query.port = port;
    const portfabric::QueryResponse response = client.query(query);
    if (!response.records.empty() && response.records.front().ownership.has_value()) {
      ownership_id = response.records.front().ownership->id;
    }
  }

  if (options.configure) {
    const auto speed = portfabric::PortSpeed::parse(options.speed);
    if (!speed.has_value()) {
      std::printf("result=INVALID_SPEED\n");
      std::fflush(stdout);
      return 2;
    }
    portfabric::PortMutationMessage configure;
    configure.kind = static_cast<std::uint8_t>(portfabric::PortMutationKind::Configure);
    configure.port = port;
    configure.authority.epoch = client.epoch();
    configure.authority.publisher = client_config.publisher;
    configure.authority.boot = client_config.boot;
    configure.authority.ownership = ownership_id;
    configure.authority.expected_configuration =
        portfabric::PortConfigurationGeneration::from_value(options.config_generation);
    configure.authority.attempt = portfabric::MutationAttemptId::from_validated(
        "configure-" + options.publisher + "-" + options.boot);
    configure.has_configuration = true;
    configure.parent_device = portfabric::DeviceId::from_validated(options.device);
    configure.device_generation = portfabric::DeviceGeneration::from_value(1);
    configure.configuration.port = port;
    configure.configuration.parent_device =
        portfabric::DeviceId::from_validated(options.device);
    configure.configuration.administrative = portfabric::AdministrativeState::Enabled;
    configure.configuration.mode = portfabric::PortMode::Physical;
    configure.configuration.protocol = portfabric::ProtocolFamily::Synthetic;
    configure.configuration.speed.selection = portfabric::SpeedSelection::Forced;
    configure.configuration.speed.rate = *speed;
    configure.configuration.duplex = portfabric::DuplexMode::Full;
    configure.configuration.autoneg = portfabric::AutonegPolicy::Forced;
    configure.configuration.fec = portfabric::FecMode::None;
    configure.configuration.pause = portfabric::PauseMode::Disabled;
    configure.configuration.breakout = portfabric::BreakoutMode::None;
    const auto mtu = portfabric::Mtu::create(options.mtu);
    if (!mtu.has_value()) {
      std::printf("result=INVALID_MTU\n");
      std::fflush(stdout);
      return 2;
    }
    configure.configuration.mtu = *mtu;
    const auto lanes = portfabric::LaneConfiguration::for_total(8, *speed);
    if (!lanes.has_value()) {
      std::printf("result=INVALID_LANE_CONFIGURATION\n");
      std::fflush(stdout);
      return 2;
    }
    configure.configuration.lanes = *lanes;
    configure.provenance_kind = static_cast<std::uint8_t>(portfabric::ProvenanceKind::SwitchAgent);
    configure.provenance_source = options.publisher;
    const portfabric::MutationResult result = client.mutate(configure);
    std::printf("configure=%s\n", outcome_text(result).c_str());
    if (!result.accepted()) {
      std::printf("message=%s\n", result.outcome.message().c_str());
      std::fflush(stdout);
      client.close();
      return 1;
    }
  }

  std::printf("publisher=%s\n", options.publisher.c_str());
  std::printf("boot=%s\n", options.boot.c_str());
  std::printf("fabric-port=%s\n", options.port.c_str());
  if (ownership_id.valid()) {
    std::printf("ownership=%s\n", ownership_id.to_string().c_str());
  }
  std::printf("ready=true\n");
  std::fflush(stdout);

  const bool wait_for_stop = !options.stop_file.empty();
  while (wait_for_stop) {
    if (portfabric::file_exists(options.stop_file)) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }

  client.close();
  std::printf("stopped=true\n");
  std::fflush(stdout);
  return 0;
}
