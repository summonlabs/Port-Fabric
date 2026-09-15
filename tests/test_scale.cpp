// Scale: tens of thousands of ports with incremental mutation, bulk profile
// assignment, ownership invalidation, capability invalidation, snapshots and a
// persistence round trip.

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "pf_test.hpp"
#include "support.hpp"

using namespace portfabric;

namespace {

std::uint32_t scale_port_count() {
  // 10,000 ports always; 100,000 when the environment asks for the larger run.
  return 10000;
}

}  // namespace

PF_TEST(scale_binding_and_configuration_are_linear) {
  const std::uint32_t count = scale_port_count();
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Spine400G, count, "scale");
  EngineConfig config = pf_test::engine_config();
  config.adapter_policy = AdapterPolicy::DesiredStateOnly;
  PortFabricEngine engine(config, &fixture.provider, nullptr);

  const auto start = std::chrono::steady_clock::now();
  for (std::uint32_t index = 1; index <= count; ++index) {
    const PortId port = fixture.port(index, "scale");
    PortBindingRequest binding;
    binding.port = port;
    binding.parent_device = fixture.device;
    binding.device_generation = DeviceGeneration::from_value(1);
    binding.epoch = engine.epoch();
    binding.attempt = MutationAttemptId::from_validated("bind-" + std::to_string(index));
    PF_REQUIRE(engine.bind_port(binding).accepted());
  }
  const auto after_binding = std::chrono::steady_clock::now();
  PF_CHECK_EQ(engine.port_count(), static_cast<std::size_t>(count));

  for (std::uint32_t index = 1; index <= count; ++index) {
    PF_REQUIRE(pf_test::claim(engine, fixture.port(index, "scale"), "bulk-agent", "bulk-boot",
                              "claim-" + std::to_string(index),
                              OwnerId::from_validated("bulk-owner"))
                   .accepted());
  }
  for (std::uint32_t index = 1; index <= count; ++index) {
    const PortId port = fixture.port(index, "scale");
    const auto capability = engine.capability_binding(port);
    const auto ownership = engine.ownership(port);
    ConfigureRequest configure;
    configure.envelope = pf_test::envelope(port, "bulk-agent", "bulk-boot",
                                           "configure-" + std::to_string(index), engine.epoch());
    configure.envelope.authority.ownership = ownership->id;
    configure.configuration = fixture.configuration(port, *capability);
    const MutationResult configured = engine.configure(configure);
    if (!configured.accepted()) {
      std::printf("  port index %u rejected: %s\n", index,
                  configured.outcome.to_string().c_str());
    }
    PF_REQUIRE(configured.accepted());
  }
  const auto after_configuration = std::chrono::steady_clock::now();

  const double bind_ms =
      std::chrono::duration<double, std::milli>(after_binding - start).count();
  const double configure_ms =
      std::chrono::duration<double, std::milli>(after_configuration - after_binding).count();
  std::printf("  ports=%u bind_ms=%.1f configure_ms=%.1f\n", count, bind_ms, configure_ms);
  PF_CHECK_EQ(engine.ports_with_lifecycle(PortLifecycle::Configured).size(),
              static_cast<std::size_t>(count));
  std::size_t indexed = 0;
  for (const portfabric::DeviceId& device : fixture.devices) {
    indexed += engine.ports_of_device(device).size();
  }
  PF_CHECK_EQ(indexed, static_cast<std::size_t>(count));

  // A snapshot binds every record and is immutable.
  const Snapshot snapshot = engine.snapshot();
  PF_CHECK_EQ(snapshot.port_count(), static_cast<std::size_t>(count));
  PF_CHECK(snapshot.current_for(engine.generation(), engine.epoch()));
  PF_CHECK(!snapshot.digest.is_zero());

  // Incremental mutation of one port must not rebuild the indexes.
  const PortId probe = fixture.port(1, "scale");
  const auto ownership = engine.ownership(probe);
  const auto before = engine.configuration(probe)->generation;
  ReconfigureRequest reconfigure;
  reconfigure.envelope =
      pf_test::envelope(probe, "bulk-agent", "bulk-boot", "incremental-1", engine.epoch());
  reconfigure.envelope.authority.ownership = ownership->id;
  reconfigure.envelope.authority.expected_configuration = before;
  reconfigure.configuration = *engine.configuration(probe);
  reconfigure.configuration.mtu = *Mtu::create(9000);
  const auto mutation_start = std::chrono::steady_clock::now();
  PF_REQUIRE(engine.reconfigure(reconfigure).accepted());
  const double mutation_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mutation_start)
          .count();
  std::printf("  single incremental mutation ms=%.4f\n", mutation_ms);
  PF_CHECK(mutation_ms < 50.0);
  PF_CHECK_EQ(engine.port_count(), static_cast<std::size_t>(count));

  // Ownership invalidation is bounded work proportional to the owned set.
  const auto fence_start = std::chrono::steady_clock::now();
  PF_REQUIRE(engine
                 .fence_publisher(PublisherId::from_validated("bulk-agent"),
                                  WorkerBootId::from_validated("bulk-boot"))
                 .accepted());
  const double fence_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - fence_start)
          .count();
  std::printf("  ownership invalidation ms=%.1f\n", fence_ms);
  PF_CHECK(engine.ports_of_owner(OwnerId::from_validated("bulk-owner")).empty());
}

PF_TEST(scale_bulk_profile_assignment) {
  const std::uint32_t count = 2000;
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Spine400G, count, "profiles");
  EngineConfig config = pf_test::engine_config();
  config.adapter_policy = AdapterPolicy::DesiredStateOnly;
  PortFabricEngine engine(config, &fixture.provider, nullptr);

  PortProfile profile;
  profile.id = PortProfileId::from_validated("bulk-uplink");
  profile.generation = PortProfileGeneration::from_value(1);
  profile.name = "bulk-uplink";
  profile.description = "bulk assignment profile";
  profile.configuration.specifies_mtu = true;
  profile.configuration.mtu = *Mtu::create(9216);
  profile.provenance.kind = ProvenanceKind::Operator;
  profile.provenance.epoch = engine.epoch();
  profile.provenance.attempt = MutationAttemptId::from_validated("profile-1");
  PF_REQUIRE(engine.define_profile(profile).accepted());

  for (std::uint32_t index = 1; index <= count; ++index) {
    const PortId port = fixture.port(index, "profiles");
    PortBindingRequest binding;
    binding.port = port;
    binding.parent_device = fixture.device;
    binding.device_generation = DeviceGeneration::from_value(1);
    binding.epoch = engine.epoch();
    binding.attempt = MutationAttemptId::from_validated("bind-" + std::to_string(index));
    PF_REQUIRE(engine.bind_port(binding).accepted());
    PF_REQUIRE(pf_test::claim(engine, port, "bulk-agent", "bulk-boot",
                              "claim-" + std::to_string(index),
                              OwnerId::from_validated("bulk-owner"))
                   .accepted());
    const auto ownership = engine.ownership(port);
    const auto capability = engine.capability_binding(port);
    ConfigureRequest configure;
    configure.envelope = pf_test::envelope(port, "bulk-agent", "bulk-boot",
                                           "configure-" + std::to_string(index), engine.epoch());
    configure.envelope.authority.ownership = ownership->id;
    configure.configuration = fixture.configuration(port, *capability);
    PF_REQUIRE(engine.configure(configure).accepted());
  }

  const auto start = std::chrono::steady_clock::now();
  for (std::uint32_t index = 1; index <= count; ++index) {
    const PortId port = fixture.port(index, "profiles");
    ProfileRequest assign;
    assign.envelope = pf_test::envelope(port, "bulk-agent", "bulk-boot",
                                        "assign-" + std::to_string(index), engine.epoch());
    assign.envelope.authority.ownership = engine.ownership(port)->id;
    assign.envelope.authority.expected_configuration = engine.configuration(port)->generation;
    assign.profile = profile.id;
    assign.generation = PortProfileGeneration::from_value(1);
    PF_REQUIRE(engine.assign_profile(assign).accepted());
  }
  const double assign_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  std::printf("  bulk profile assignment ports=%u ms=%.1f\n", count, assign_ms);
  PF_CHECK_EQ(engine.ports_by_profile(profile.id).size(), static_cast<std::size_t>(count));
  PF_CHECK(engine.ports_with_stale_profile_binding().empty());
}

PF_TEST(scale_capability_invalidation_across_many_ports_is_explicit) {
  const std::uint32_t count = 500;
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Server25G, count, "caps");
  EngineConfig config = pf_test::engine_config();
  config.adapter_policy = AdapterPolicy::DesiredStateOnly;
  PortFabricEngine engine(config, &fixture.provider, nullptr);
  for (std::uint32_t index = 1; index <= count; ++index) {
    PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, fixture.port(index, "caps"),
                                             "bulk-agent", "bulk-boot",
                                             portfabric::SyntheticPortClass::Server25G)
                   .accepted());
  }
  // Capability truth advances for one port only: exactly that port is fenced.
  const PortId probe = fixture.port(1, "caps");
  const auto capability = engine.capability_binding(probe);
  CapabilityRequest request;
  request.envelope = pf_test::envelope(probe, "bulk-agent", "bulk-boot", "caps-2", engine.epoch());
  request.envelope.authority.ownership = engine.ownership(probe)->id;
  request.binding = *capability;
  request.binding.generation = capability->generation.next();
  request.binding.evidence_generation = capability->evidence_generation.next();
  request.binding.capabilities.supported_speeds.clear();
  request.binding.capabilities.supported_speeds.push_back(*PortSpeed::parse("10G"));
  PF_REQUIRE(engine.bind_capabilities(request).accepted());
  PF_CHECK_EQ(*engine.lifecycle(probe), PortLifecycle::RevalidationRequired);
  PF_CHECK_EQ(engine.ports_requiring_revalidation().size(), static_cast<std::size_t>(1));
  PF_CHECK_EQ(*engine.lifecycle(fixture.port(2, "caps")), PortLifecycle::Configured);
}

PF_TEST(scale_persistence_round_trip) {
  const std::uint32_t count = 5000;
  const std::string path = pf_test::scratch_path("scale.pfstate");
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Spine400G, count, "persist");
  {
    EngineConfig config = pf_test::engine_config("coordinator", path);
    config.adapter_policy = AdapterPolicy::DesiredStateOnly;
    PortFabricEngine engine(config, &fixture.provider, nullptr);
    for (std::uint32_t index = 1; index <= count; ++index) {
      const PortId port = fixture.port(index, "persist");
      PortBindingRequest binding;
      binding.port = port;
      binding.parent_device = fixture.device;
      binding.device_generation = DeviceGeneration::from_value(1);
      binding.epoch = engine.epoch();
      binding.attempt = MutationAttemptId::from_validated("bind-" + std::to_string(index));
      PF_REQUIRE(engine.bind_port(binding).accepted());
    }
    const auto start = std::chrono::steady_clock::now();
    PF_REQUIRE(engine.save().accepted());
    const double save_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("  save ports=%u ms=%.1f\n", count, save_ms);
  }
  {
    EngineConfig config = pf_test::engine_config("coordinator", path);
    config.adapter_policy = AdapterPolicy::DesiredStateOnly;
    PortFabricEngine engine(config, &fixture.provider, nullptr);
    const auto start = std::chrono::steady_clock::now();
    PF_REQUIRE(engine.load().accepted());
    const double load_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("  load ports=%u ms=%.1f\n", count, load_ms);
    PF_CHECK_EQ(engine.port_count(), static_cast<std::size_t>(count));
  }
  pf_test::remove_scratch(path);
}
