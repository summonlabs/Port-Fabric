// Port Fabric inspection tool.
//
// The tool is a read-only view over authoritative state. It either loads a
// persistence container or builds a deterministic synthetic fabric, then renders
// the requested view. Output is deterministic and script friendly: one record
// per line, no timestamps, no locale dependent formatting.

#include <cstdio>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "portfabric/engine.hpp"
#include "portfabric/persistence.hpp"
#include "portfabric/platform.hpp"
#include "portfabric/synthetic.hpp"
#include "portfabric/version.hpp"

namespace {

struct Options {
  std::string state_path;
  std::string synthetic_spec;
  std::string device_label = "inspect-device";
  bool help = false;
};

void print_usage() {
  std::printf(
      "pf-inspect - Port Fabric inspection tool %s\n"
      "\n"
      "usage: pf-inspect [--state <path>] [--synthetic <CLASS:COUNT>] <command> [args]\n"
      "\n"
      "commands:\n"
      "  version                             print the runtime version\n"
      "  list-ports [--lifecycle S] [--administrative S] [--device D] [--profile P] [--revalidation]\n"
      "  show-port <port>                    full record rendering\n"
      "  show-config <port>                  committed configuration\n"
      "  show-lifecycle <port>               lifecycle state\n"
      "  show-admin <port>                   administrative state\n"
      "  show-owner <port>                   control ownership\n"
      "  show-profile <port>                 profile binding\n"
      "  show-capability <port>              bound capability evidence\n"
      "  show-applied <port>                 applied evidence\n"
      "  show-devices                        recorded device generations\n"
      "  show-profiles                       defined profile generations\n"
      "  explain <port>                      deterministic explanation\n"
      "  diff --set key=value ... <port>     deterministic configuration diff\n"
      "  validate <port>                     capability validation of committed state\n"
      "  snapshot                            immutable state snapshot summary\n"
      "  inspect-persistence                 container header and record summary\n"
      "  host-ports                          real local host port evidence\n"
      "\n"
      "with no --state and no --synthetic, a deterministic synthetic fabric is used.\n",
      std::string(portfabric::version_string()).c_str());
}

bool parse_options(int argc, char** argv, Options& options, std::vector<std::string>& rest) {
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--state" && index + 1 < argc) {
      options.state_path = argv[++index];
    } else if (argument == "--synthetic" && index + 1 < argc) {
      options.synthetic_spec = argv[++index];
    } else if (argument == "--device" && index + 1 < argc) {
      options.device_label = argv[++index];
    } else if (argument == "--help" || argument == "-h") {
      options.help = true;
    } else {
      rest.push_back(argument);
    }
  }
  return true;
}

portfabric::EngineConfig base_config(const std::string& state_path) {
  portfabric::EngineConfig config;
  config.fabric = portfabric::FabricId::from_validated("inspect-fabric");
  config.site = portfabric::SiteId::from_validated("inspect-site");
  config.coordinator = portfabric::PublisherId::from_validated("pf-inspect");
  config.epoch = portfabric::CoordinatorEpoch::from_value(1);
  config.persistence_path = state_path;
  config.persistence = portfabric::PersistenceMode::Manual;
  return config;
}

void add_synthetic_fabric(portfabric::SyntheticFabric& fabric, const std::string& spec,
                          const std::string& device_label) {
  std::string class_name = "SPINE_400G";
  std::uint32_t count = 8;
  if (!spec.empty()) {
    const std::size_t separator = spec.find(':');
    if (separator == std::string::npos) {
      class_name = spec;
    } else {
      class_name = spec.substr(0, separator);
      const std::string count_text = spec.substr(separator + 1);
      count = static_cast<std::uint32_t>(std::strtoul(count_text.c_str(), nullptr, 10));
    }
  }
  portfabric::SyntheticPortClass port_class = portfabric::SyntheticPortClass::Spine400G;
  (void)portfabric::parse_synthetic_class(class_name, port_class);
  (void)fabric.add_device(device_label, port_class, count,
                          portfabric::PortEntityClass::SyntheticPort);
}

void print_ports(const std::vector<portfabric::PortId>& ports) {
  for (const portfabric::PortId& port : ports) {
    std::printf("%s\n", port.to_string().c_str());
  }
  std::printf("total=%zu\n", ports.size());
}

bool parse_state(const std::string& text, portfabric::AdministrativeState& out) {
  return portfabric::parse_administrative_state(text, out);
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  std::vector<std::string> arguments;
  parse_options(argc, argv, options, arguments);
  if (options.help || arguments.empty()) {
    print_usage();
    return options.help ? 0 : 2;
  }
  const std::string command = arguments.front();

  portfabric::SyntheticFabric fabric("inspect-synthetic");
  add_synthetic_fabric(fabric, options.synthetic_spec, options.device_label);
  portfabric::SyntheticCapabilityProvider provider(fabric);
  portfabric::SyntheticAdapter adapter(fabric);

  portfabric::EngineConfig config = base_config(options.state_path);
  portfabric::PortFabricEngine engine(config, &provider, &adapter);
  if (!options.state_path.empty()) {
    const portfabric::Outcome loaded = engine.load();
    if (!loaded.ok()) {
      std::fprintf(stderr, "load failed: %s\n", loaded.to_string().c_str());
      return 1;
    }
  }

  if (command == "version") {
    std::printf("Port Fabric %s\n", std::string(portfabric::version_string()).c_str());
    std::printf("persistence_format=%u\n", portfabric::persistence_format_version());
    std::printf("protocol=%u\n", portfabric::kProtocolVersion);
    std::printf("host_platform=%s\n", std::string(portfabric::HostPlatform::backend_label()).c_str());
    return 0;
  }
  if (command == "host-ports") {
    std::string error;
    const std::vector<portfabric::HostPortEvidence> evidence =
        portfabric::HostPlatform::enumerate_ports(error);
    if (!error.empty()) {
      std::fprintf(stderr, "host enumeration error: %s\n", error.c_str());
      return 1;
    }
    for (const portfabric::HostPortEvidence& entry : evidence) {
      std::printf("%s\n", entry.to_string().c_str());
    }
    std::printf("total=%zu\n", evidence.size());
    return 0;
  }
  if (command == "list-ports") {
    portfabric::PortLifecycle lifecycle = portfabric::PortLifecycle::Unconfigured;
    portfabric::AdministrativeState administrative = portfabric::AdministrativeState::Unknown;
    portfabric::DeviceId device;
    portfabric::PortProfileId profile;
    bool filter_lifecycle = false;
    bool filter_administrative = false;
    bool filter_revalidation = false;
    for (std::size_t index = 1; index < arguments.size(); ++index) {
      const std::string& argument = arguments[index];
      if (argument == "--lifecycle" && index + 1 < arguments.size()) {
        filter_lifecycle =
            portfabric::parse_lifecycle(arguments[++index], lifecycle);
      } else if (argument == "--administrative" && index + 1 < arguments.size()) {
        filter_administrative =
            parse_state(arguments[++index], administrative);
      } else if (argument == "--device" && index + 1 < arguments.size()) {
        device = portfabric::DeviceId::from_validated(arguments[++index]);
      } else if (argument == "--profile" && index + 1 < arguments.size()) {
        profile = portfabric::PortProfileId::from_validated(arguments[++index]);
      } else if (argument == "--revalidation") {
        filter_revalidation = true;
      }
    }
    std::vector<portfabric::PortId> ports;
    if (filter_revalidation) {
      ports = engine.ports_requiring_revalidation();
    } else if (filter_lifecycle) {
      ports = engine.ports_with_lifecycle(lifecycle);
    } else if (filter_administrative) {
      ports = engine.ports_with_administrative_state(administrative);
    } else if (device.valid()) {
      ports = engine.ports_of_device(device);
    } else if (profile.valid()) {
      ports = engine.ports_by_profile(profile);
    } else {
      for (const portfabric::PortRecord& record : engine.snapshot().records) {
        ports.push_back(record.port);
      }
    }
    print_ports(ports);
    return 0;
  }
  if (command == "snapshot") {
    const portfabric::Snapshot snapshot = engine.snapshot();
    std::printf("%s\n", snapshot.to_string().c_str());
    std::printf("ports=%zu\n", snapshot.port_count());
    return 0;
  }
  if (command == "inspect-persistence") {
    if (options.state_path.empty()) {
      std::fprintf(stderr, "inspect-persistence requires --state <path>\n");
      return 2;
    }
    portfabric::PersistedState state;
    const portfabric::Outcome loaded = portfabric::PersistenceStore::load(options.state_path, state);
    std::printf("result=%s\n", loaded.to_string().c_str());
    if (!loaded.ok()) {
      return 1;
    }
    std::printf("magic=%s\n", std::string(portfabric::persistence_magic()).c_str());
    std::printf("format=%u\n", portfabric::persistence_format_version());
    std::printf("fabric=%s\n", state.fabric.to_string().c_str());
    std::printf("site=%s\n", state.site.to_string().c_str());
    std::printf("epoch=%s\n", state.epoch.to_string().c_str());
    std::printf("engine_generation=%s\n", state.engine_generation.to_string().c_str());
    std::printf("records=%zu\n", state.records.size());
    std::printf("profiles=%zu\n", state.profiles.size());
    std::printf("devices=%zu\n", state.devices.size());
    std::printf("publishers=%zu\n", state.publishers.size());
    return 0;
  }
  if (command == "show-devices") {
    for (const portfabric::DeviceBinding& binding : engine.devices()) {
      std::printf("%s\n", binding.to_string().c_str());
    }
    return 0;
  }
  if (command == "show-profiles") {
    for (const portfabric::PortProfile& profile : engine.profiles()) {
      std::printf("%s\n", profile.to_string().c_str());
    }
    return 0;
  }

  if (arguments.size() < 2) {
    std::fprintf(stderr, "command %s requires a port identity\n", command.c_str());
    return 2;
  }
  std::string port_text = arguments.back();
  portfabric::IdValidation validation = portfabric::IdValidation::Ok;
  const auto port = portfabric::PortId::parse(port_text, validation);
  if (!port.has_value()) {
    std::fprintf(stderr, "malformed port identity: %s\n",
                 std::string(portfabric::to_string(validation)).c_str());
    return 2;
  }
  const portfabric::PortRecord* record = nullptr;
  const portfabric::Snapshot snapshot = engine.snapshot_of(*port);
  if (!snapshot.records.empty()) {
    record = &snapshot.records.front();
  }
  if (record == nullptr) {
    std::fprintf(stderr, "unknown port: %s\n", port_text.c_str());
    return 1;
  }

  if (command == "show-port") {
    std::printf("%s\n", record->to_string().c_str());
    return 0;
  }
  if (command == "show-config") {
    if (!record->configuration.has_value()) {
      std::printf("configuration=none\n");
      return 0;
    }
    std::printf("%s\n", record->configuration->to_string().c_str());
    return 0;
  }
  if (command == "show-lifecycle") {
    std::printf("%s\n", std::string(portfabric::to_string(record->lifecycle)).c_str());
    return 0;
  }
  if (command == "show-admin") {
    if (!record->configuration.has_value()) {
      std::printf("administrative=UNKNOWN\n");
      return 0;
    }
    std::printf("%s\n",
                std::string(portfabric::to_string(record->configuration->administrative)).c_str());
    return 0;
  }
  if (command == "show-owner") {
    std::printf("%s\n",
                record->ownership.has_value() ? record->ownership->to_string().c_str() : "unowned");
    return 0;
  }
  if (command == "show-profile") {
    std::printf("%s\n", record->configuration.has_value()
                             ? record->configuration->profile.to_string().c_str()
                             : "none");
    return 0;
  }
  if (command == "show-capability") {
    std::printf("%s\n", record->capability.to_string().c_str());
    return 0;
  }
  if (command == "show-applied") {
    std::printf("%s\n", record->applied.to_string().c_str());
    return 0;
  }
  if (command == "explain") {
    std::printf("%s\n", engine.explain(*port).to_string().c_str());
    return 0;
  }
  if (command == "validate") {
    if (!record->configuration.has_value()) {
      std::printf("result=NO_CONFIGURATION\n");
      return 0;
    }
    const portfabric::CapabilityValidation validation_result =
        portfabric::validate_against_capabilities(*record->configuration, record->capability);
    std::printf("result=%s\n", std::string(portfabric::to_string(validation_result)).c_str());
    std::printf("accepted=%s\n", validation_result.accepted ? "true" : "false");
    if (!validation_result.accepted) {
      std::printf("reason=%s\n", validation_result.reason.c_str());
      std::printf("detail=%s\n", validation_result.detail.c_str());
    }
    return 0;
  }
  if (command == "diff") {
    if (!record->configuration.has_value()) {
      std::fprintf(stderr, "the port carries no committed configuration\n");
      return 1;
    }
    portfabric::PortConfiguration candidate = *record->configuration;
    for (std::size_t index = 1; index + 1 < arguments.size(); ++index) {
      if (arguments[index] != "--set") {
        continue;
      }
      const std::string& assignment = arguments[++index];
      const std::size_t separator = assignment.find('=');
      if (separator == std::string::npos) {
        std::fprintf(stderr, "malformed assignment: %s\n", assignment.c_str());
        return 2;
      }
      const std::string key = assignment.substr(0, separator);
      const std::string value = assignment.substr(separator + 1);
      if (key == "speed") {
        const auto speed = portfabric::PortSpeed::parse(value);
        if (!speed.has_value()) {
          std::fprintf(stderr, "malformed speed: %s\n", value.c_str());
          return 2;
        }
        candidate.speed.selection = portfabric::SpeedSelection::Forced;
        candidate.speed.rate = *speed;
        const auto lanes =
            portfabric::LaneConfiguration::for_total(candidate.lanes.lanes(), *speed);
        if (lanes.has_value()) {
          candidate.lanes = *lanes;
        }
      } else if (key == "mtu") {
        const auto mtu = portfabric::Mtu::create(
            static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 10)));
        if (!mtu.has_value()) {
          std::fprintf(stderr, "malformed mtu: %s\n", value.c_str());
          return 2;
        }
        candidate.mtu = *mtu;
      } else if (key == "admin") {
        portfabric::AdministrativeState state = portfabric::AdministrativeState::Unknown;
        if (!portfabric::parse_administrative_state(value, state)) {
          std::fprintf(stderr, "malformed administrative state: %s\n", value.c_str());
          return 2;
        }
        candidate.administrative = state;
      } else if (key == "fec") {
        portfabric::FecMode mode = portfabric::FecMode::Unknown;
        if (!portfabric::parse_fec_mode(value, mode)) {
          std::fprintf(stderr, "malformed fec mode: %s\n", value.c_str());
          return 2;
        }
        candidate.fec = mode;
      } else if (key == "pause") {
        portfabric::PauseMode mode = portfabric::PauseMode::Unknown;
        if (!portfabric::parse_pause_mode(value, mode)) {
          std::fprintf(stderr, "malformed pause mode: %s\n", value.c_str());
          return 2;
        }
        candidate.pause = mode;
      } else if (key == "breakout") {
        portfabric::BreakoutMode mode = portfabric::BreakoutMode::None;
        if (!portfabric::parse_breakout_mode(value, mode)) {
          std::fprintf(stderr, "malformed breakout mode: %s\n", value.c_str());
          return 2;
        }
        candidate.breakout = mode;
      } else {
        std::fprintf(stderr, "unknown field: %s\n", key.c_str());
        return 2;
      }
    }
    const portfabric::ConfigurationDiff diff = engine.diff(*port, candidate);
    std::printf("%s\n", diff.to_string().c_str());
    return 0;
  }

  std::fprintf(stderr, "unknown command: %s\n", command.c_str());
  print_usage();
  return 2;
}
