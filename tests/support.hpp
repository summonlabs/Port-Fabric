#pragma once

// Shared fixtures for the Port Fabric suite. Every helper builds state through
// the public API of the runtime: no test reaches into internal storage.

#include <cstdio>
#include <string>
#include <vector>

#include "portfabric/engine.hpp"
#include "portfabric/platform.hpp"
#include "portfabric/synthetic.hpp"

namespace pf_test {

/// Deterministic synthetic fabric.
///
/// Large inventories are split across devices, because one device carries at most
/// max_ports_per_device ports. Port identities stay deterministic: the first
/// device uses the plain label and later devices append their index.
struct Fixture {
  static constexpr std::uint32_t kPortsPerDevice = 4096;

  portfabric::SyntheticFabric fabric{"test-synthetic"};
  portfabric::SyntheticCapabilityProvider provider{fabric};
  portfabric::SyntheticAdapter adapter{fabric};
  std::vector<portfabric::DeviceId> devices;
  /// First device of the fixture.
  portfabric::DeviceId device;

  /// Class of the synthetic device built by this fixture.
  portfabric::SyntheticPortClass port_class = portfabric::SyntheticPortClass::Spine400G;

  explicit Fixture(portfabric::SyntheticPortClass port_class_value =
                       portfabric::SyntheticPortClass::Spine400G,
                   std::uint32_t port_count = 8, const std::string& label = "sw1")
      : port_class(port_class_value) {
    std::uint32_t remaining = port_count == 0 ? 1 : port_count;
    std::uint32_t device_index = 0;
    while (remaining > 0) {
      const std::uint32_t chunk =
          remaining > kPortsPerDevice ? kPortsPerDevice : remaining;
      std::string device_label = label;
      if (device_index > 0) {
        device_label.append("-").append(std::to_string(device_index));
      }
      const portfabric::DeviceId created = fabric.add_device(
          device_label, port_class, chunk, portfabric::PortEntityClass::SyntheticPort);
      if (created.valid()) {
        devices.push_back(created);
      }
      remaining -= chunk;
      ++device_index;
    }
    device = devices.empty() ? portfabric::DeviceId{} : devices.front();
  }

  /// Canonical configuration for one of this fixture's ports.
  portfabric::PortConfiguration configuration(const portfabric::PortId& port,
                                              const portfabric::CapabilityBinding& binding) const;

  /// Canonical identity of the index-th port (one based), matching the identity
  /// the synthetic fabric generated.
  portfabric::PortId port(std::uint32_t index, const std::string& label = "sw1") const {
    const std::uint32_t zero_based = index == 0 ? 0 : index - 1;
    const std::uint32_t device_index = zero_based / kPortsPerDevice;
    const std::uint32_t local = zero_based % kPortsPerDevice + 1;
    std::string device_label = label;
    if (device_index > 0) {
      device_label.append("-").append(std::to_string(device_index));
    }
    const std::string digits =
        local < 10 ? "0" + std::to_string(local) : std::to_string(local);
    return portfabric::PortId::from_validated("syn-" + device_label + "-p" + digits);
  }
};

inline portfabric::EngineConfig engine_config(const std::string& coordinator = "coordinator",
                                              const std::string& path = "",
                                              std::uint64_t epoch = 1) {
  portfabric::EngineConfig config;
  config.fabric = portfabric::FabricId::from_validated("fabric-a");
  config.site = portfabric::SiteId::from_validated("site-a");
  config.coordinator = portfabric::PublisherId::from_validated(coordinator);
  config.epoch = portfabric::CoordinatorEpoch::from_value(epoch);
  config.persistence_path = path;
  config.persistence = path.empty() ? portfabric::PersistenceMode::Manual
                                    : portfabric::PersistenceMode::Automatic;
  // The fixture always supplies a synthetic adapter, so the tests exercise the
  // real apply and readback path unless a case opts out explicitly.
  config.adapter_policy = portfabric::AdapterPolicy::ApplyAndVerify;
  return config;
}

/// Canonical forced rate of a synthetic class, matching the class model.
inline portfabric::PortSpeed class_speed(portfabric::SyntheticPortClass port_class) {
  using portfabric::SyntheticPortClass;
  switch (port_class) {
    case SyntheticPortClass::Management1G:
    case SyntheticPortClass::Access1G:
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

inline std::uint32_t class_lanes(portfabric::SyntheticPortClass port_class) {
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

inline portfabric::FecMode class_fec(portfabric::SyntheticPortClass port_class) {
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

inline portfabric::PortConfiguration make_configuration(
    const Fixture& fixture, const portfabric::PortId& port,
    const portfabric::CapabilityBinding& binding,
    portfabric::SyntheticPortClass port_class = portfabric::SyntheticPortClass::Spine400G) {
  portfabric::PortConfiguration configuration;
  configuration.port = port;
  configuration.parent_device = fixture.device;
  configuration.administrative = portfabric::AdministrativeState::Enabled;
  configuration.mode = portfabric::PortMode::Physical;
  configuration.protocol = portfabric::ProtocolFamily::Synthetic;
  configuration.speed.selection = portfabric::SpeedSelection::Forced;
  configuration.speed.rate = class_speed(port_class);
  configuration.duplex = portfabric::DuplexMode::Full;
  configuration.autoneg = portfabric::AutonegPolicy::Forced;
  configuration.fec = class_fec(port_class);
  configuration.pause = portfabric::PauseMode::Disabled;
  configuration.breakout = portfabric::BreakoutMode::None;
  configuration.mtu = *portfabric::Mtu::create(4096);
  configuration.lanes = *portfabric::LaneConfiguration::for_total(
      class_lanes(port_class), class_speed(port_class));
  configuration.capability_generation = binding.generation;
  return configuration;
}

inline portfabric::PortConfiguration Fixture::configuration(
    const portfabric::PortId& port, const portfabric::CapabilityBinding& binding) const {
  return make_configuration(*this, port, binding, port_class);
}

inline portfabric::MutationEnvelope envelope(const portfabric::PortId& port,
                                             const std::string& publisher,
                                             const std::string& boot, const std::string& attempt,
                                             portfabric::CoordinatorEpoch epoch) {
  portfabric::MutationEnvelope result;
  result.port = port;
  result.authority.epoch = epoch;
  result.authority.publisher = portfabric::PublisherId::from_validated(publisher);
  result.authority.boot = portfabric::WorkerBootId::from_validated(boot);
  result.authority.attempt = portfabric::MutationAttemptId::from_validated(attempt);
  result.provenance_kind = portfabric::ProvenanceKind::SwitchAgent;
  result.provenance_source = publisher;
  return result;
}

/// Binds, claims ownership of and configures one port.
///
/// Returns the outcome of the configuration step so a test can assert on it.
inline portfabric::MutationResult bind_claim_configure(
    portfabric::PortFabricEngine& engine, const Fixture& fixture, const portfabric::PortId& port,
    const std::string& publisher = "agent-a", const std::string& boot = "boot-a",
    portfabric::SyntheticPortClass port_class = portfabric::SyntheticPortClass::Spine400G) {
  portfabric::PortBindingRequest binding;
  binding.port = port;
  binding.parent_device = fixture.device;
  binding.device_generation = portfabric::DeviceGeneration::from_value(1);
  binding.epoch = engine.epoch();
  binding.attempt = portfabric::MutationAttemptId::from_validated("bind-" + port.value());
  (void)engine.bind_port(binding);

  portfabric::OwnershipRequest claim;
  claim.envelope = envelope(port, publisher, boot, "claim-" + port.value(), engine.epoch());
  claim.owner_kind = portfabric::PortOwnerKind::SwitchAgent;
  claim.owner = portfabric::OwnerId::from_validated("owner-" + publisher);
  (void)engine.claim_ownership(claim);

  const auto ownership = engine.ownership(port);
  const auto capability = engine.capability_binding(port);
  portfabric::ConfigureRequest configure;
  configure.envelope =
      envelope(port, publisher, boot, "configure-" + port.value(), engine.epoch());
  if (ownership.has_value()) {
    configure.envelope.authority.ownership = ownership->id;
  }
  if (capability.has_value()) {
    configure.configuration = make_configuration(fixture, port, *capability, port_class);
  } else {
    portfabric::CapabilityBinding empty;
    configure.configuration = make_configuration(fixture, port, empty, port_class);
  }
  const portfabric::MutationResult result = engine.configure(configure);
  if (!result.accepted()) {
    // A helper failure is almost always a test bug: print why.
    std::printf("  bind_claim_configure(%s) rejected: %s\n", port.value().c_str(),
                result.outcome.to_string().c_str());
    std::fflush(stdout);
  }
  return result;
}

/// Binds, claims ownership of and configures a port of the given synthetic class.
inline portfabric::MutationResult bind_claim_configure(
    portfabric::PortFabricEngine& engine, const Fixture& fixture, const portfabric::PortId& port,
    portfabric::SyntheticPortClass port_class) {
  return bind_claim_configure(engine, fixture, port, "agent-a", "boot-a", port_class);
}

/// Claims ownership with an explicit attempt identity.
inline portfabric::MutationResult claim(portfabric::PortFabricEngine& engine,
                                        const portfabric::PortId& port,
                                        const std::string& publisher, const std::string& boot,
                                        const std::string& attempt,
                                        portfabric::OwnerId owner) {
  portfabric::OwnershipRequest request;
  request.envelope = envelope(port, publisher, boot, attempt, engine.epoch());
  request.owner_kind = portfabric::PortOwnerKind::SwitchAgent;
  request.owner = owner;
  return engine.claim_ownership(request);
}

inline portfabric::MutationResult administer(portfabric::PortFabricEngine& engine,
                                             const portfabric::PortId& port,
                                             portfabric::AdministrativeOp op,
                                             const std::string& attempt,
                                             const std::string& publisher = "agent-a",
                                             const std::string& boot = "boot-a") {
  portfabric::AdministrativeRequest request;
  request.envelope = envelope(port, publisher, boot, attempt, engine.epoch());
  const auto ownership = engine.ownership(port);
  if (ownership.has_value()) {
    request.envelope.authority.ownership = ownership->id;
  }
  const auto configuration = engine.configuration(port);
  if (configuration.has_value()) {
    request.envelope.authority.expected_configuration = configuration->generation;
  }
  request.op = op;
  return engine.administrative(request);
}

/// Creates a unique temporary file path for a test and removes any stale file,
/// including the temporary container a failed save may have left behind.
inline std::string scratch_path(const std::string& name) {
  const std::string path = "pf-scratch-" + name;
  (void)portfabric::remove_file(path);
  (void)portfabric::remove_file(path + ".pftmp");
  return path;
}

/// Removes a scratch container and its temporary counterpart.
inline void remove_scratch(const std::string& path) {
  (void)portfabric::remove_file(path);
  (void)portfabric::remove_file(path + ".pftmp");
}

}  // namespace pf_test
