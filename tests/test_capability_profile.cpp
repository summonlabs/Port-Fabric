// Capability binding, fail-closed UNKNOWN handling, profile generations and
// breakout semantics.

#include <string>
#include <vector>

#include "pf_test.hpp"
#include "support.hpp"

using namespace portfabric;

PF_TEST(capability_validation_is_fail_closed_on_unknown) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PortBindingRequest binding;
  binding.port = port;
  binding.parent_device = fixture.device;
  binding.device_generation = DeviceGeneration::from_value(1);
  binding.epoch = engine.epoch();
  binding.attempt = MutationAttemptId::from_validated("bind-1");
  PF_REQUIRE(engine.bind_port(binding).accepted());
  PF_REQUIRE(pf_test::claim(engine, port, "agent-a", "boot-a", "claim-1",
                            OwnerId::from_validated("owner-a"))
                 .accepted());
  const auto capability = engine.capability_binding(port);
  PF_REQUIRE(capability.has_value());

  PortConfiguration configuration = fixture.configuration(port, *capability);
  CapabilityBinding unknown = *capability;
  unknown.capabilities.certainty = CapabilityCertainty::Unknown;
  const CapabilityValidation rejected = validate_against_capabilities(configuration, unknown);
  PF_CHECK(!rejected.accepted);
  PF_CHECK_EQ(rejected.code, OutcomeCode::CapabilityUnknown);

  // An unbound capability binding rejects everything that depends on it.
  CapabilityBinding unbound;
  const CapabilityValidation unbound_result =
      validate_against_capabilities(configuration, unbound);
  PF_CHECK(!unbound_result.accepted);
  PF_CHECK_EQ(unbound_result.code, OutcomeCode::CapabilityUnavailable);
}

PF_TEST(capability_dimensions_are_validated) {
  pf_test::Fixture fixture;
  const PortId port = fixture.port(1);
  std::string error;
  const auto binding = fixture.provider.current(port, error);
  PF_REQUIRE(binding.has_value());
  PortConfiguration configuration = fixture.configuration(port, *binding);

  PF_CHECK(validate_against_capabilities(configuration, *binding).accepted);

  struct Case {
    const char* name;
    PortConfiguration configuration;
    OutcomeCode expected;
    const char* reason;
  };
  std::vector<Case> cases;

  PortConfiguration unsupported_speed = configuration;
  unsupported_speed.speed.rate = *PortSpeed::parse("800G");
  unsupported_speed.lanes = *LaneConfiguration::for_total(8, *PortSpeed::parse("800G"));
  cases.push_back({"speed", unsupported_speed, OutcomeCode::UnsupportedConfiguration,
                   "SPEED_UNSUPPORTED"});

  PortConfiguration bad_mtu = configuration;
  bad_mtu.mtu = *Mtu::create(65000);
  cases.push_back({"mtu", bad_mtu, OutcomeCode::UnsupportedConfiguration, "MTU_OUT_OF_RANGE"});

  // The synthetic spine model advertises lane counts 1, 2, 4 and 8 only.
  PortConfiguration bad_lanes = configuration;
  bad_lanes.lanes = *LaneConfiguration::for_total(16, *PortSpeed::parse("400G"));
  bad_lanes.speed.rate = *PortSpeed::parse("400G");
  cases.push_back({"lanes", bad_lanes, OutcomeCode::UnsupportedConfiguration,
                   "LANE_COUNT_UNSUPPORTED"});

  PortConfiguration bad_fec = configuration;
  bad_fec.fec = FecMode::ReedSolomon272Interleaved;
  cases.push_back({"fec", bad_fec, OutcomeCode::UnsupportedConfiguration, "FEC_UNSUPPORTED"});

  PortConfiguration bad_breakout = configuration;
  bad_breakout.breakout = BreakoutMode::X8;
  bad_breakout.lanes = *LaneConfiguration::for_total(8, *PortSpeed::parse("400G"));
  cases.push_back({"breakout", bad_breakout, OutcomeCode::UnsupportedConfiguration,
                   "BREAKOUT_FANOUT_UNSUPPORTED"});

  PortConfiguration bad_protocol = configuration;
  bad_protocol.protocol = ProtocolFamily::InfiniBand;
  cases.push_back({"protocol", bad_protocol, OutcomeCode::UnsupportedConfiguration,
                   "PROTOCOL_UNSUPPORTED"});

  PortConfiguration unknown_protocol = configuration;
  unknown_protocol.protocol = ProtocolFamily::Unknown;
  cases.push_back({"protocol_unknown", unknown_protocol, OutcomeCode::CapabilityUnknown,
                   "PROTOCOL_UNKNOWN"});

  for (const Case& item : cases) {
    const CapabilityValidation validation =
        validate_against_capabilities(item.configuration, *binding);
    PF_CHECK(!validation.accepted);
    PF_CHECK_EQ(validation.code, item.expected);
    if (validation.reason != item.reason) {
      std::printf("  case %s reason: %s\n", item.name, validation.reason.c_str());
      PF_CHECK(false);
    }
  }
}

PF_TEST(capability_generation_advance_invalidates_configuration) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1, "sw1");
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port,
                                           portfabric::SyntheticPortClass::Spine400G)
                 .accepted());
  const auto capability = engine.capability_binding(port);
  PF_REQUIRE(capability.has_value());
  const auto generation = engine.configuration(port)->generation;

  CapabilityRequest request;
  request.envelope = pf_test::envelope(port, "agent-a", "boot-a", "caps-2", engine.epoch());
  request.envelope.authority.ownership = engine.ownership(port)->id;
  request.binding = *capability;
  request.binding.generation = capability->generation.next();
  request.binding.evidence_generation = capability->evidence_generation.next();
  request.binding.capabilities.supported_speeds.clear();
  request.binding.capabilities.supported_speeds.push_back(*PortSpeed::parse("100G"));
  const MutationResult rebound = engine.bind_capabilities(request);
  PF_REQUIRE(rebound.accepted());
  PF_CHECK_EQ(engine.record(port)->generations.capability, request.binding.generation);
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::RevalidationRequired);
  const auto invalidated = engine.record(port);
  PF_REQUIRE(invalidated.has_value());
  PF_CHECK_EQ(invalidated->status_reason, std::string("CAPABILITY_INVALIDATED"));
  PF_CHECK_EQ(invalidated->drift, DriftState::RevalidationRequired);
  PF_CHECK_EQ(engine.configuration(port)->generation, generation);

  // The port is fenced until it is reconciled against the new evidence.
  PF_CHECK_EQ(pf_test::administer(engine, port, AdministrativeOp::Disable, "d1").outcome.code(),
              OutcomeCode::RevalidationRequired);
}

PF_TEST(capability_generation_cannot_go_backwards) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  CapabilityRequest request;
  request.envelope = pf_test::envelope(port, "agent-a", "boot-a", "caps-old", engine.epoch());
  request.envelope.authority.ownership = engine.ownership(port)->id;
  request.binding = *engine.capability_binding(port);
  request.binding.generation = CapabilityBindingGeneration::from_value(0);
  PF_CHECK_EQ(engine.bind_capabilities(request).outcome.code(), OutcomeCode::MalformedRequest);
  request.binding.generation = engine.capability_binding(port)->generation;
  request.binding.evidence_generation = engine.capability_binding(port)->evidence_generation;
  PF_CHECK_EQ(engine.bind_capabilities(request).outcome.code(), OutcomeCode::AlreadyCurrent);
}

PF_TEST(profile_generations_are_immutable) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  PortProfile profile;
  profile.id = PortProfileId::from_validated("uplink");
  profile.generation = PortProfileGeneration::from_value(1);
  profile.name = "uplink";
  profile.description = "fabric uplink";
  profile.configuration.specifies_mtu = true;
  profile.configuration.mtu = *Mtu::create(9216);
  profile.provenance.kind = ProvenanceKind::Operator;
  profile.provenance.epoch = engine.epoch();
  profile.provenance.attempt = MutationAttemptId::from_validated("p1");
  PF_REQUIRE(engine.define_profile(profile).accepted());

  // Identical redefinition is already current.
  PF_CHECK_EQ(engine.define_profile(profile).code(), OutcomeCode::AlreadyCurrent);
  // Different content for the same generation is rejected.
  PortProfile conflicting = profile;
  conflicting.configuration.mtu = *Mtu::create(4096);
  PF_CHECK_EQ(engine.define_profile(conflicting).code(), OutcomeCode::DuplicateRecord);
  // An older generation cannot be defined after a newer one.
  PortProfile newer = profile;
  newer.generation = PortProfileGeneration::from_value(2);
  newer.configuration.mtu = *Mtu::create(4096);
  PF_REQUIRE(engine.define_profile(newer).accepted());
  // A generation that is already defined can never be rewritten, not even by a
  // later attempt that names an older generation with different content.
  PortProfile older = profile;
  older.generation = PortProfileGeneration::from_value(1);
  older.configuration.mtu = *Mtu::create(1500);
  PF_CHECK_EQ(engine.define_profile(older).code(), OutcomeCode::InvalidProfile);
  PF_CHECK(!engine.profile(profile.id, PortProfileGeneration::from_value(3)).has_value());
  PF_CHECK(engine.latest_profile(profile.id)->generation.value() == 2ull);
  PF_CHECK_EQ(engine.profile_count(), static_cast<std::size_t>(2));
}

PF_TEST(profile_assignment_requires_explicit_reevaluation) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());

  PortProfile profile;
  profile.id = PortProfileId::from_validated("uplink");
  profile.generation = PortProfileGeneration::from_value(1);
  profile.name = "uplink";
  profile.description = "fabric uplink";
  profile.configuration.specifies_mtu = true;
  profile.configuration.mtu = *Mtu::create(9216);
  profile.provenance.kind = ProvenanceKind::Operator;
  profile.provenance.epoch = engine.epoch();
  profile.provenance.attempt = MutationAttemptId::from_validated("p1");
  PF_REQUIRE(engine.define_profile(profile).accepted());

  ProfileRequest assign;
  assign.envelope = pf_test::envelope(port, "agent-a", "boot-a", "assign-1", engine.epoch());
  assign.envelope.authority.ownership = engine.ownership(port)->id;
  assign.envelope.authority.expected_configuration = engine.configuration(port)->generation;
  assign.profile = profile.id;
  assign.generation = PortProfileGeneration::from_value(1);
  PF_REQUIRE(engine.assign_profile(assign).accepted());
  PF_CHECK_EQ(engine.configuration(port)->mtu.value(), 9216u);
  PF_CHECK_EQ(engine.configuration(port)->profile.generation.value(), 1ull);
  PF_CHECK_EQ(engine.ports_by_profile(profile.id).size(), static_cast<std::size_t>(1));

  // A newer generation does not silently rewrite the committed configuration.
  profile.generation = PortProfileGeneration::from_value(2);
  profile.configuration.mtu = *Mtu::create(4096);
  profile.provenance.attempt = MutationAttemptId::from_validated("p2");
  PF_REQUIRE(engine.define_profile(profile).accepted());
  PF_CHECK_EQ(engine.configuration(port)->mtu.value(), 9216u);
  PF_CHECK_EQ(engine.configuration(port)->profile.generation.value(), 1ull);
  PF_CHECK_EQ(engine.ports_with_stale_profile_binding().size(), static_cast<std::size_t>(1));

  // Explicit re-evaluation binds the newer generation.
  assign.envelope.authority.attempt = MutationAttemptId::from_validated("assign-2");
  assign.envelope.authority.expected_configuration = engine.configuration(port)->generation;
  assign.generation = PortProfileGeneration::from_value(2);
  PF_REQUIRE(engine.assign_profile(assign).accepted());
  PF_CHECK_EQ(engine.configuration(port)->mtu.value(), 4096u);
  PF_CHECK_EQ(engine.ports_with_stale_profile_binding().size(), static_cast<std::size_t>(0));

  // Releasing the profile binding keeps the configuration but drops the binding.
  assign.envelope.authority.attempt = MutationAttemptId::from_validated("release-profile");
  assign.envelope.authority.expected_configuration = engine.configuration(port)->generation;
  assign.release = true;
  PF_REQUIRE(engine.assign_profile(assign).accepted());
  PF_CHECK(!engine.configuration(port)->profile.bound);
  PF_CHECK(engine.ports_by_profile(profile.id).empty());
}

PF_TEST(unknown_profile_is_rejected) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  ProfileRequest assign;
  assign.envelope = pf_test::envelope(port, "agent-a", "boot-a", "assign-unknown", engine.epoch());
  assign.envelope.authority.ownership = engine.ownership(port)->id;
  assign.envelope.authority.expected_configuration = engine.configuration(port)->generation;
  assign.profile = PortProfileId::from_validated("does-not-exist");
  assign.generation = PortProfileGeneration::from_value(1);
  PF_CHECK_EQ(engine.assign_profile(assign).outcome.code(), OutcomeCode::UnknownProfile);
}

PF_TEST(breakout_geometry_is_validated) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PortBindingRequest binding;
  binding.port = port;
  binding.parent_device = fixture.device;
  binding.device_generation = DeviceGeneration::from_value(1);
  binding.epoch = engine.epoch();
  binding.attempt = MutationAttemptId::from_validated("bind-1");
  PF_REQUIRE(engine.bind_port(binding).accepted());
  PF_REQUIRE(pf_test::claim(engine, port, "agent-a", "boot-a", "claim-1",
                            OwnerId::from_validated("owner-a"))
                 .accepted());
  const auto capability = engine.capability_binding(port);
  PF_REQUIRE(capability.has_value());

  // Lanes that do not divide across the breakout fan-out are rejected.
  ConfigureRequest configure;
  configure.envelope =
      pf_test::envelope(port, "agent-a", "boot-a", "configure-1", engine.epoch());
  configure.envelope.authority.ownership = engine.ownership(port)->id;
  configure.configuration = fixture.configuration(port, *capability);
  configure.configuration.breakout = BreakoutMode::X4;
  configure.configuration.lanes = *LaneConfiguration::for_total(2, *PortSpeed::parse("200G"));
  configure.configuration.speed.rate = *PortSpeed::parse("400G");
  PF_CHECK_EQ(engine.configure(configure).outcome.code(), OutcomeCode::InvalidConfiguration);

  // A physical port must not carry logical derivation fields.
  configure.envelope.authority.attempt = MutationAttemptId::from_validated("configure-2");
  configure.configuration = fixture.configuration(port, *capability);
  configure.configuration.logical_identity = LogicalPortId::from_validated("lgl-1");
  configure.configuration.derived_from = port;
  PF_CHECK_EQ(engine.configure(configure).outcome.code(), OutcomeCode::InvalidConfiguration);

  // A legal breakout configuration is accepted.
  configure.envelope.authority.attempt = MutationAttemptId::from_validated("configure-3");
  configure.configuration = fixture.configuration(port, *capability);
  configure.configuration.breakout = BreakoutMode::X4;
  configure.configuration.lanes = *LaneConfiguration::for_total(8, *PortSpeed::parse("400G"));
  PF_CHECK(engine.configure(configure).accepted());
}

PF_TEST(breakout_children_are_independently_governed) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId parent = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, parent).accepted());
  const std::vector<PortId> children = fixture.fabric.add_breakout_children(parent, 4);
  PF_REQUIRE_EQ(children.size(), static_cast<std::size_t>(4));
  for (std::size_t index = 0; index < children.size(); ++index) {
    const PortId& child = children[index];
    PortBindingRequest binding;
    binding.port = child;
    binding.parent_device = fixture.device;
    binding.device_generation = DeviceGeneration::from_value(1);
    binding.epoch = engine.epoch();
    binding.attempt = MutationAttemptId::from_validated("bind-child-" + std::to_string(index));
    PF_REQUIRE(engine.bind_port(binding).accepted());
    PF_REQUIRE(pf_test::claim(engine, child, "agent-a", "boot-a",
                              "claim-child-" + std::to_string(index),
                              OwnerId::from_validated("owner-agent-a"))
                   .accepted());
    ConfigureRequest configure;
    configure.envelope.port = child;
    configure.envelope.authority.epoch = engine.epoch();
    configure.envelope.authority.publisher = PublisherId::from_validated("agent-a");
    configure.envelope.authority.boot = WorkerBootId::from_validated("boot-a");
    configure.envelope.authority.ownership = engine.ownership(child)->id;
    configure.envelope.authority.attempt = MutationAttemptId::from_validated(
        "configure-child-" + std::to_string(index));
    const auto child_capability = engine.capability_binding(child);
    PF_REQUIRE(child_capability.has_value());
    configure.configuration = fixture.configuration(child, *child_capability);
    configure.configuration.mode = PortMode::Logical;
    configure.configuration.logical_identity =
        LogicalPortId::from_validated("lgl-" + child.value());
    configure.configuration.derived_from = parent;
    configure.configuration.child_index = static_cast<std::uint32_t>(index + 1);
    // A parent split four ways: four lanes, each child realising its own share of
    // the parent rate. The lane count must divide across the breakout fan-out.
    configure.configuration.breakout = BreakoutMode::X4;
    configure.configuration.lanes = *LaneConfiguration::for_total(4, *PortSpeed::parse("200G"));
    configure.configuration.speed.rate = *PortSpeed::parse("200G");
    const MutationResult configured = engine.configure(configure);
    if (!configured.accepted()) {
      std::printf("  child %zu configure rejected: %s\n", index,
                  configured.outcome.to_string().c_str());
    }
    PF_CHECK(configured.accepted());
  }
  PF_CHECK_EQ(engine.derived_ports(parent).size(), static_cast<std::size_t>(4));
  // Retiring one child leaves the others unaffected.
  FenceRequest retire;
  retire.envelope = pf_test::envelope(children[0], "agent-a", "boot-a", "retire-child",
                                      engine.epoch());
  retire.envelope.authority.ownership = engine.ownership(children[0])->id;
  PF_REQUIRE(engine.retire(retire).accepted());
  PF_CHECK_EQ(*engine.lifecycle(children[1]), PortLifecycle::Configured);
  PF_CHECK_EQ(engine.derived_ports(parent).size(), static_cast<std::size_t>(4));
}
