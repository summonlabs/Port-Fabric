// Real local host evidence, file system helpers and truthfulness of the
// REAL / SYNTHETIC / UNSUPPORTED classification.

#include <cstdio>
#include <string>
#include <vector>

#include "pf_test.hpp"
#include "support.hpp"

using namespace portfabric;

PF_TEST(host_platform_reports_its_backend_truthfully) {
  const std::string_view backend = HostPlatform::backend_label();
  PF_CHECK(!backend.empty());
#if defined(_WIN32)
  PF_CHECK(HostPlatform::available());
  PF_CHECK_EQ(backend, std::string_view("windows-mib-if"));
#else
  PF_CHECK(!HostPlatform::available());
#endif
  PF_CHECK(HostPlatform::processor_count() >= 1u);
  PF_CHECK(HostPlatform::current_process_id() != 0u);
}

PF_TEST(host_port_enumeration_is_read_only_and_classified) {
  std::string error;
  const std::vector<HostPortEvidence> ports = HostPlatform::enumerate_ports(error);
  if (!HostPlatform::available()) {
    PF_CHECK(!error.empty());
    PF_CHECK(ports.empty());
    return;
  }
  PF_CHECK(error.empty());
  std::printf("  real host ports observed: %zu\n", ports.size());
  for (const HostPortEvidence& port : ports) {
    // Evidence is real host evidence, never switch fabric truth: the entity
    // class of a host adapter is a NIC class.
    PF_CHECK_EQ(port.entity_class, PortEntityClass::NicPort);
    PF_CHECK(!port.interface_identity.empty() || !port.alias.empty());
    PF_CHECK(port.mtu <= max_mtu);
    PF_CHECK(port.speed_bits_per_second <= max_speed_bits_per_second);
    if (port.mtu != 0) {
      // Every observed MTU must be representable as port configuration.
      PF_CHECK(Mtu::create(port.mtu).has_value() || port.mtu < min_mtu);
    }
    std::printf("  %s\n", port.to_string().c_str());
  }
}

PF_TEST(file_helpers_are_bounded_and_safe) {
  const std::string path = pf_test::scratch_path("platform-state.bin");
  const std::string payload = "port fabric platform test";
  PF_CHECK(write_file_flushed(path, payload).ok());
  PF_CHECK(file_exists(path));

  std::string read_back;
  PF_CHECK(read_file_bounded(path, 4096, read_back).ok());
  PF_CHECK_EQ(read_back, payload);

  // The byte bound is enforced before reading.
  std::string too_small;
  PF_CHECK_EQ(read_file_bounded(path, 4, too_small).code(), OutcomeCode::PersistenceBoundsExceeded);

  // Path traversal and control characters are rejected.
  PF_CHECK(!validate_file_path("").ok());
  PF_CHECK(!validate_file_path("../escape").ok());
  PF_CHECK(!validate_file_path("bad\nname").ok());
  PF_CHECK(!validate_file_path("missing-directory/file").ok());
  PF_CHECK(validate_file_path(path).ok());

  // Atomic replacement and removal.
  const std::string replacement = pf_test::scratch_path("platform-replacement.bin");
  PF_CHECK(write_file_flushed(replacement, "second").ok());
  PF_CHECK(atomic_replace_file(replacement, path).ok());
  PF_CHECK(read_file_bounded(path, 4096, read_back).ok());
  PF_CHECK_EQ(read_back, std::string("second"));
  PF_CHECK(!file_exists(replacement));
  PF_CHECK(remove_file(path).ok());
  PF_CHECK(!file_exists(path));
  // Removing a file that does not exist is not an error.
  PF_CHECK(remove_file(path).ok());
}

PF_TEST(synthetic_capability_models_cover_the_speed_classes) {
  // The synthetic models are labelled SYNTHETIC and must be able to express the
  // speed classes the runtime claims to govern, without any claim that this host
  // owns such hardware.
  const SyntheticPortClass classes[] = {
      SyntheticPortClass::Access1G,        SyntheticPortClass::Server10G,
      SyntheticPortClass::Server25G,       SyntheticPortClass::Uplink40G,
      SyntheticPortClass::Uplink100G,      SyntheticPortClass::Spine200G,
      SyntheticPortClass::Spine400G,       SyntheticPortClass::Backbone800G,
      SyntheticPortClass::Optical400G,     SyntheticPortClass::InfiniBandHdr200G};
  for (const SyntheticPortClass port_class : classes) {
    const PortCapabilities capabilities = SyntheticFabric::capabilities_for(port_class);
    std::string error;
    PF_CHECK(capabilities.validate(error));
    PF_CHECK_EQ(capabilities.certainty, CapabilityCertainty::Known);
    PF_CHECK(!capabilities.supported_speeds.empty());
    PF_CHECK(!capabilities.protocol_families.empty());
    std::printf("  %-24s %s\n", std::string(to_string(port_class)).c_str(),
                capabilities.to_string().c_str());
  }
  SyntheticFabric fabric("classes");
  const DeviceId device = fabric.add_device("cls", SyntheticPortClass::Backbone800G, 2,
                                            PortEntityClass::SyntheticPort);
  PF_CHECK(device.valid());
  PF_CHECK_EQ(fabric.port_count(), static_cast<std::size_t>(2));
  PF_CHECK(fabric.binding_for(fabric.ports().front().port)->source == "classes");
}

PF_TEST(synthetic_adapter_exercises_failure_modes) {
  pf_test::Fixture fixture;
  const PortId port = fixture.port(1);
  std::string error;
  const auto binding = fixture.provider.current(port, error);
  PF_REQUIRE(binding.has_value());
  const PortConfiguration configuration =
      fixture.configuration(port, *binding);

  PF_CHECK_EQ(fixture.adapter.apply(port, configuration).status, AdapterApplyStatus::Applied);
  PF_CHECK_EQ(fixture.adapter.readback(port).status, AdapterReadbackStatus::Verified);

  struct Case {
    SyntheticFault fault;
    AdapterApplyStatus status;
  };
  const Case cases[] = {
      {SyntheticFault::PermissionDenied, AdapterApplyStatus::Failed},
      {SyntheticFault::DeviceDisappeared, AdapterApplyStatus::Failed},
      {SyntheticFault::DeviceReplaced, AdapterApplyStatus::OutcomeUnknown},
      {SyntheticFault::UnsupportedParameter, AdapterApplyStatus::Failed},
      {SyntheticFault::PartialApply, AdapterApplyStatus::Failed},
      {SyntheticFault::StaleHandle, AdapterApplyStatus::Failed},
      {SyntheticFault::ConcurrentExternalChange, AdapterApplyStatus::OutcomeUnknown},
      {SyntheticFault::AdapterCrash, AdapterApplyStatus::OutcomeUnknown},
      {SyntheticFault::TransportFailure, AdapterApplyStatus::OutcomeUnknown},
  };
  for (const Case& item : cases) {
    fixture.fabric.set_fault(port, item.fault);
    const AdapterApplyResult result = fixture.adapter.apply(port, configuration);
    if (result.status != item.status) {
      std::printf("  fault %s produced %s\n", std::string(to_string(item.fault)).c_str(),
                  std::string(to_string(result.status)).c_str());
    }
    PF_CHECK_EQ(result.status, item.status);
  }
  fixture.fabric.clear_faults();
  PF_CHECK_EQ(fixture.adapter.readback(port).status, AdapterReadbackStatus::Verified);
}

PF_TEST(unavailable_hardware_classes_are_not_claimed_as_real) {
  // Port Fabric never claims REAL for switch, optical, InfiniBand, SmartNIC or
  // breakout hardware from local host evidence. The host adapter reports NIC
  // class entities only, and the synthetic fabric labels its evidence synthetic.
  std::string error;
  const std::vector<HostPortEvidence> ports = HostPlatform::enumerate_ports(error);
  for (const HostPortEvidence& port : ports) {
    PF_CHECK(port.entity_class == PortEntityClass::NicPort);
  }
  SyntheticFabric fabric("label");
  (void)fabric.add_device("label", SyntheticPortClass::Optical400G, 1,
                          PortEntityClass::SyntheticPort);
  std::string provider_error;
  const auto binding = fabric.binding_for(fabric.ports().front().port);
  PF_REQUIRE(binding.has_value());
  PF_CHECK_EQ(binding->source, std::string("label"));
  PF_CHECK(binding->capabilities.supports_protocol(ProtocolFamily::OpticalTransport));
  (void)provider_error;
}
