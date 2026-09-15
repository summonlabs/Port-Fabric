// Port Fabric coordinator process.
//
// The coordinator owns the authoritative engine, hosts the control server and
// writes durable state. On a fresh start it advances the coordinator epoch,
// recovers durable configuration conservatively and fences every publisher
// incarnation from the previous process life.
//
// The process is deliberately killable: nothing about its correctness depends on
// a graceful shutdown. A hard kill followed by a restart is a supported and
// proven path.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "portfabric/engine.hpp"
#include "portfabric/persistence.hpp"
#include "portfabric/platform.hpp"
#include "portfabric/server.hpp"
#include "portfabric/synthetic.hpp"
#include "portfabric/version.hpp"

namespace {

struct Options {
  std::string address = "127.0.0.1";
  std::uint16_t port = 0;
  std::string state_path;
  std::string stop_file;
  std::string fabric = "fabric";
  std::string site = "site";
  std::string coordinator = "coordinator";
  std::uint64_t epoch = 1;
  std::string synthetic_spec = "SPINE_400G:8";
  std::string synthetic_device = "coordinator-device";
  bool verbose = false;
};

void print_usage() {
  std::printf(
      "pf-coordinator - Port Fabric coordinator %s\n"
      "usage: pf-coordinator [options]\n"
      "  --address <ip>            listen address (default 127.0.0.1)\n"
      "  --port <n>                listen port, 0 selects an ephemeral port\n"
      "  --state <path>            persistence container path\n"
      "  --stop-file <path>        exit gracefully when this file appears\n"
      "  --epoch <n>               starting coordinator epoch\n"
      "  --coordinator <id>        coordinator identity\n"
      "  --fabric <id> --site <id> authority domain identities\n"
      "  --synthetic <CLASS:COUNT> synthetic device inventory\n"
      "  --verbose                 print committed mutations\n",
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
    } else if (argument == "--port") {
      std::string value;
      next(value);
      options.port = static_cast<std::uint16_t>(std::strtoul(value.c_str(), nullptr, 10));
    } else if (argument == "--state") {
      next(options.state_path);
    } else if (argument == "--stop-file") {
      next(options.stop_file);
    } else if (argument == "--epoch") {
      std::string value;
      next(value);
      options.epoch = std::strtoull(value.c_str(), nullptr, 10);
    } else if (argument == "--coordinator") {
      next(options.coordinator);
    } else if (argument == "--fabric") {
      next(options.fabric);
    } else if (argument == "--site") {
      next(options.site);
    } else if (argument == "--synthetic") {
      next(options.synthetic_spec);
    } else if (argument == "--verbose") {
      options.verbose = true;
    } else if (argument == "--help" || argument == "-h") {
      print_usage();
      return false;
    } else {
      std::fprintf(stderr, "unknown option: %s\n", argument.c_str());
      return false;
    }
  }
  return true;
}

void add_synthetic(portfabric::SyntheticFabric& fabric, const std::string& spec,
                   const std::string& device_label) {
  std::string class_name = spec;
  std::uint32_t count = 8;
  const std::size_t separator = spec.find(':');
  if (separator != std::string::npos) {
    class_name = spec.substr(0, separator);
    count = static_cast<std::uint32_t>(std::strtoul(spec.c_str() + separator + 1, nullptr, 10));
  }
  portfabric::SyntheticPortClass port_class = portfabric::SyntheticPortClass::Spine400G;
  (void)portfabric::parse_synthetic_class(class_name, port_class);
  (void)fabric.add_device(device_label, port_class, count,
                          portfabric::PortEntityClass::SyntheticPort);
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parse(argc, argv, options)) {
    print_usage();
    return 2;
  }

  portfabric::SyntheticFabric fabric("coordinator-synthetic");
  add_synthetic(fabric, options.synthetic_spec, options.synthetic_device);
  portfabric::SyntheticCapabilityProvider provider(fabric);
  portfabric::SyntheticAdapter adapter(fabric);

  portfabric::EngineConfig config;
  config.fabric = portfabric::FabricId::from_validated(options.fabric);
  config.site = portfabric::SiteId::from_validated(options.site);
  config.coordinator = portfabric::PublisherId::from_validated(options.coordinator);
  config.epoch = portfabric::CoordinatorEpoch::from_value(options.epoch);
  config.persistence_path = options.state_path;
  config.persistence = options.state_path.empty() ? portfabric::PersistenceMode::Manual
                                                  : portfabric::PersistenceMode::Automatic;
  config.adapter_policy = portfabric::AdapterPolicy::ApplyAndVerify;

  portfabric::PortFabricEngine engine(config, &provider, &adapter);

  std::string startup = "pf-coordinator starting version=" +
                        std::string(portfabric::version_string());
  std::printf("%s\n", startup.c_str());
  std::fflush(stdout);

  if (!options.state_path.empty()) {
    const portfabric::Outcome loaded = engine.load();
    std::printf("recovery=%s\n", loaded.to_string().c_str());
    if (!loaded.ok() && loaded.code() != portfabric::OutcomeCode::PersistenceIoFailure) {
      std::fflush(stdout);
      return 1;
    }
  }
  portfabric::CoordinatorEpoch assigned;
  const portfabric::Outcome epoch_outcome = engine.begin_epoch(assigned);
  if (!epoch_outcome.ok()) {
    std::printf("epoch=%s\n", epoch_outcome.to_string().c_str());
    std::fflush(stdout);
    return 1;
  }
  std::printf("epoch=%s\n", assigned.to_string().c_str());

  portfabric::ServerConfig server_config;
  server_config.address = options.address;
  server_config.port = options.port;
  portfabric::EngineService service(engine);
  portfabric::CoordinatorServer server(server_config, service);
  const portfabric::Outcome started = server.start();
  std::printf("listen=%s\n", started.to_string().c_str());
  if (!started.ok()) {
    std::fflush(stdout);
    return 1;
  }
  std::printf("port=%u\n", static_cast<unsigned>(server.bound_port()));
  std::printf("coordinator=%s\n", options.coordinator.c_str());
  std::printf("ready=true\n");
  std::fflush(stdout);

  std::atomic<bool> stop{false};
  while (!stop.load()) {
    if (!options.stop_file.empty() && portfabric::file_exists(options.stop_file)) {
      stop.store(true);
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }

  const portfabric::Outcome joined = server.join();
  std::printf("shutdown=%s\n", joined.to_string().c_str());
  if (!options.state_path.empty()) {
    const portfabric::Outcome saved = engine.save();
    std::printf("persisted=%s\n", saved.to_string().c_str());
  }
  std::printf("stopped=true\n");
  std::fflush(stdout);
  return 0;
}
