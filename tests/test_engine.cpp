// Port lifecycle, configuration, ownership, administrative state, retirement,
// supersession, applied evidence and drift semantics.

#include <string>
#include <vector>

#include "pf_test.hpp"
#include "support.hpp"

using namespace portfabric;

PF_TEST(bind_port_creates_unconfigured_record) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PortBindingRequest binding;
  binding.port = port;
  binding.parent_device = fixture.device;
  binding.device_generation = DeviceGeneration::from_value(1);
  binding.epoch = engine.epoch();
  binding.attempt = MutationAttemptId::from_validated("bind-1");
  const MutationResult result = engine.bind_port(binding);
  PF_CHECK(result.accepted());
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::Unconfigured);
  PF_CHECK(!engine.configuration(port).has_value());
  PF_CHECK_EQ(engine.port_count(), static_cast<std::size_t>(1));

  // Identity exists, configuration does not: the two are separate.
  const MutationResult repeated = engine.bind_port(binding);
  PF_CHECK_EQ(repeated.outcome.code(), OutcomeCode::Idempotent);
  PF_CHECK_EQ(engine.port_count(), static_cast<std::size_t>(1));
}

PF_TEST(bind_port_rejects_unknown_device_and_malformed_request) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  PortBindingRequest binding;
  binding.port = fixture.port(1);
  binding.parent_device = fixture.device;
  binding.device_generation = DeviceGeneration::from_value(1);
  binding.epoch = engine.epoch();
  PF_CHECK_EQ(engine.bind_port(binding).outcome.code(), OutcomeCode::MalformedRequest);
  binding.attempt = MutationAttemptId::from_validated("bind-1");
  binding.epoch = CoordinatorEpoch::from_value(99);
  PF_CHECK_EQ(engine.bind_port(binding).outcome.code(), OutcomeCode::StaleCoordinatorEpoch);
  binding.epoch = engine.epoch();
  binding.port = PortId{};
  PF_CHECK_EQ(engine.bind_port(binding).outcome.code(), OutcomeCode::MalformedRequest);
}

PF_TEST(bind_port_rejects_conflicting_parent_device) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PortBindingRequest binding;
  binding.port = port;
  binding.parent_device = fixture.device;
  binding.device_generation = DeviceGeneration::from_value(1);
  binding.epoch = engine.epoch();
  binding.attempt = MutationAttemptId::from_validated("bind-1");
  PF_CHECK(engine.bind_port(binding).accepted());
  binding.parent_device = DeviceId::from_validated("other-device");
  binding.attempt = MutationAttemptId::from_validated("bind-2");
  PF_CHECK_EQ(engine.bind_port(binding).outcome.code(), OutcomeCode::ConflictDetected);
}

PF_TEST(configure_commits_and_advances_generations) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  const MutationResult result = pf_test::bind_claim_configure(engine, fixture, port);
  PF_REQUIRE(result.accepted());
  const auto configuration = engine.configuration(port);
  PF_REQUIRE(configuration.has_value());
  PF_CHECK_EQ(configuration->generation.value(), 1ull);
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::Configured);
  PF_CHECK_EQ(*engine.administrative_state(port), AdministrativeState::Enabled);
  PF_CHECK_EQ(configuration->speed.rate.to_string(), std::string("400G"));
  PF_CHECK(engine.generation().valid());
  const auto record = engine.record(port);
  PF_REQUIRE(record.has_value());
  PF_CHECK_EQ(record->generations.configuration.value(), 1ull);
  PF_CHECK_EQ(record->generations.administrative.value(), 1ull);
  PF_CHECK(record->generations.lifecycle.valid());
  // The synthetic adapter applied and verified the configuration.
  PF_CHECK_EQ(record->applied.outcome, AppliedOutcome::Verified);
  PF_CHECK(record->applied.verified);
}

PF_TEST(configure_requires_capability_evidence) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), nullptr, nullptr);
  const PortId port = fixture.port(1);
  const MutationResult result = pf_test::bind_claim_configure(engine, fixture, port);
  PF_CHECK_EQ(result.outcome.code(), OutcomeCode::CapabilityUnavailable);
  PF_CHECK(!engine.configuration(port).has_value());
}

PF_TEST(configure_rejects_second_configuration) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  // An exact replay is idempotent; a genuinely new configuration attempt against
  // an already configured port is an illegal lifecycle transition.
  PF_CHECK_EQ(pf_test::bind_claim_configure(engine, fixture, port).outcome.code(),
              OutcomeCode::Idempotent);
  const auto capability = engine.capability_binding(port);
  PF_REQUIRE(capability.has_value());
  ConfigureRequest second;
  second.envelope =
      pf_test::envelope(port, "agent-a", "boot-a", "configure-second", engine.epoch());
  second.envelope.authority.ownership = engine.ownership(port)->id;
  second.envelope.authority.expected_configuration = engine.configuration(port)->generation;
  second.configuration = fixture.configuration(port, *capability);
  PF_CHECK_EQ(engine.configure(second).outcome.code(), OutcomeCode::InvalidLifecycleTransition);
}

PF_TEST(exact_replay_is_idempotent_and_stale_is_not) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  const auto ownership = engine.ownership(port);
  PF_REQUIRE(ownership.has_value());
  const auto capability = engine.capability_binding(port);
  PF_REQUIRE(capability.has_value());

  PortConfiguration next = *engine.configuration(port);
  next.mtu = *Mtu::create(9000);
  ReconfigureRequest request;
  request.envelope =
      pf_test::envelope(port, "agent-a", "boot-a", "reconfigure-1", engine.epoch());
  request.envelope.authority.ownership = ownership->id;
  request.envelope.authority.expected_configuration = engine.configuration(port)->generation;
  request.configuration = next;
  const MutationResult committed = engine.reconfigure(request);
  PF_REQUIRE(committed.accepted());
  PF_CHECK_EQ(engine.configuration(port)->generation.value(), 2ull);

  // Exact replay of the committed attempt.
  const MutationResult replay = engine.reconfigure(request);
  PF_CHECK_EQ(replay.outcome.code(), OutcomeCode::Idempotent);
  PF_CHECK_EQ(engine.configuration(port)->generation.value(), 2ull);

  // Same attempt identity with a different payload is a conflict, not a replay.
  ReconfigureRequest conflicting = request;
  conflicting.configuration.mtu = *Mtu::create(9200);
  PF_CHECK_EQ(engine.reconfigure(conflicting).outcome.code(), OutcomeCode::ConflictDetected);

  // An older expectation is stale, not idempotent.
  ReconfigureRequest stale = request;
  stale.envelope.authority.attempt = MutationAttemptId::from_validated("reconfigure-2");
  stale.envelope.authority.expected_configuration = PortConfigurationGeneration::from_value(1);
  stale.configuration.mtu = *Mtu::create(9100);
  PF_CHECK_EQ(engine.reconfigure(stale).outcome.code(), OutcomeCode::StaleConfigurationGeneration);
  PF_CHECK_EQ(engine.configuration(port)->generation.value(), 2ull);
}

PF_TEST(administrative_state_is_independent_of_lifecycle_and_link) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());

  PF_CHECK(pf_test::administer(engine, port, AdministrativeOp::Disable, "admin-1").accepted());
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::AdminDisabled);
  PF_CHECK_EQ(*engine.administrative_state(port), AdministrativeState::Disabled);
  // The committed configuration still exists while administratively disabled.
  PF_CHECK(engine.configuration(port).has_value());

  PF_CHECK(pf_test::administer(engine, port, AdministrativeOp::Disable, "admin-2").accepted());
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::AdminDisabled);

  PF_CHECK(pf_test::administer(engine, port, AdministrativeOp::Enable, "admin-3").accepted());
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::Active);

  PF_CHECK(pf_test::administer(engine, port, AdministrativeOp::Drain, "admin-4").accepted());
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::Draining);

  PF_CHECK(
      pf_test::administer(engine, port, AdministrativeOp::EnterMaintenance, "admin-5").accepted());
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::Maintenance);

  PF_CHECK(pf_test::administer(engine, port, AdministrativeOp::Resume, "admin-6").accepted());
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::Active);
  PF_CHECK_EQ(*engine.administrative_state(port), AdministrativeState::Enabled);

  // Requiring revalidation fences the port without touching the configuration.
  const auto generation = engine.configuration(port)->generation;
  PF_CHECK(pf_test::administer(engine, port, AdministrativeOp::RequireRevalidation, "admin-7")
               .accepted());
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::RevalidationRequired);
  PF_CHECK_EQ(engine.configuration(port)->generation, generation);
  PF_CHECK_EQ(engine.ports_requiring_revalidation().size(), static_cast<std::size_t>(1));
}

PF_TEST(administrative_transition_from_unconfigured_rejects) {
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
  const MutationResult result =
      pf_test::administer(engine, port, AdministrativeOp::Enable, "admin-1");
  PF_CHECK_EQ(result.outcome.code(), OutcomeCode::InvalidLifecycleTransition);
}

PF_TEST(ownership_is_mutation_authority) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());

  // Another publisher cannot mutate an exclusively owned port.
  const MutationResult intruder =
      pf_test::administer(engine, port, AdministrativeOp::Disable, "intruder-1", "agent-b",
                          "boot-b");
  PF_CHECK_EQ(intruder.outcome.code(), OutcomeCode::UnauthorizedOwner);

  // Ownership is delegated together with the process incarnation that receives
  // it: authority is bound to an incarnation, not to a name.
  PF_REQUIRE(engine
                 .register_publisher(PublisherId::from_validated("controller-1"),
                                     WorkerBootId::from_validated("controller-boot"),
                                     engine.epoch())
                 .accepted());
  OwnershipRequest transfer;
  transfer.envelope =
      pf_test::envelope(port, "agent-a", "boot-a", "transfer-1", engine.epoch());
  transfer.envelope.authority.ownership = engine.ownership(port)->id;
  transfer.owner_kind = PortOwnerKind::NetworkController;
  transfer.owner = OwnerId::from_validated("controller-1");
  transfer.delegate_publisher = PublisherId::from_validated("controller-1");
  transfer.delegate_boot = WorkerBootId::from_validated("controller-boot");
  const MutationResult transferred = engine.transfer_ownership(transfer);
  PF_REQUIRE(transferred.accepted());
  PF_CHECK_EQ(engine.ownership(port)->owner.to_string(), std::string("controller-1"));
  PF_CHECK_EQ(engine.ownership(port)->publisher.to_string(), std::string("controller-1"));
  PF_CHECK_EQ(engine.ports_of_owner(OwnerId::from_validated("controller-1")).size(),
              static_cast<std::size_t>(1));
  PF_CHECK(engine.ports_of_owner(OwnerId::from_validated("owner-agent-a")).empty());

  // The previous holder no longer has authority.
  PF_CHECK_EQ(pf_test::administer(engine, port, AdministrativeOp::Disable, "old-owner-1")
                  .outcome.code(),
              OutcomeCode::UnauthorizedOwner);
  // The new holder does.
  PF_CHECK(pf_test::administer(engine, port, AdministrativeOp::Disable, "new-owner-1",
                               "controller-1", "controller-boot")
               .accepted());
  PF_CHECK(pf_test::administer(engine, port, AdministrativeOp::Enable, "new-owner-2",
                               "controller-1", "controller-boot")
               .accepted());

  // A second authority cannot claim an exclusively owned port.
  const MutationResult contested =
      pf_test::claim(engine, port, "agent-c", "boot-c", "claim-c", OwnerId::from_validated("c"));
  PF_CHECK_EQ(contested.outcome.code(), OutcomeCode::OwnershipConflict);

  // Releasing ownership removes authority, and a later claim succeeds.
  OwnershipRequest release;
  release.envelope = pf_test::envelope(port, "controller-1", "controller-boot", "release-1",
                                       engine.epoch());
  release.envelope.authority.ownership = engine.ownership(port)->id;
  release.release = true;
  PF_CHECK(engine.release_ownership(release).accepted());
  PF_CHECK(!engine.ownership(port).has_value());
  PF_CHECK_EQ(engine.port_count(), static_cast<std::size_t>(1));
}

PF_TEST(ownership_generation_advances_on_change) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  const auto first = engine.ownership(port);
  PF_REQUIRE(first.has_value());
  PF_CHECK_EQ(first->generation.value(), 1ull);

  OwnershipRequest release;
  release.envelope = pf_test::envelope(port, "agent-a", "boot-a", "release-1", engine.epoch());
  release.release = true;
  PF_REQUIRE(engine.release_ownership(release).accepted());
  PF_CHECK_EQ(engine.record(port)->generations.ownership.value(), 2ull);

  const MutationResult second =
      pf_test::claim(engine, port, "agent-b", "boot-b", "claim-b", OwnerId::from_validated("b"));
  PF_REQUIRE(second.accepted());
  PF_CHECK_EQ(engine.ownership(port)->generation.value(), 3ull);
  PF_CHECK(engine.ownership(port)->id != first->id);
}

PF_TEST(retirement_is_terminal) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  FenceRequest retire;
  retire.envelope = pf_test::envelope(port, "agent-a", "boot-a", "retire-1", engine.epoch());
  retire.envelope.authority.ownership = engine.ownership(port)->id;
  retire.detail = "decommissioned";
  const MutationResult retired = engine.retire(retire);
  PF_REQUIRE(retired.accepted());
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::Retired);
  PF_CHECK(!engine.ownership(port).has_value());

  // Every later mutation is rejected, including reconciliation and configuration.
  FenceRequest again = retire;
  again.envelope.authority.attempt = MutationAttemptId::from_validated("retire-2");
  PF_CHECK_EQ(engine.retire(again).outcome.code(), OutcomeCode::AlreadyCurrent);
  PF_CHECK_EQ(pf_test::bind_claim_configure(engine, fixture, port).outcome.code(),
              OutcomeCode::PortRetired);
  ReconcileRequest reconcile;
  reconcile.envelope = pf_test::envelope(port, "agent-a", "boot-a", "reconcile-1", engine.epoch());
  PF_CHECK_EQ(engine.reconcile(reconcile).outcome.code(), OutcomeCode::PortRetired);
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::Retired);
}

PF_TEST(supersession_fences_and_reconciliation_rebinds) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  const auto ownership = engine.ownership(port);
  const auto generation = engine.configuration(port)->generation;

  FenceRequest supersede;
  supersede.envelope = pf_test::envelope(port, "agent-a", "boot-a", "supersede-1", engine.epoch());
  supersede.envelope.authority.ownership = ownership->id;
  supersede.detail = "device generation superseded";
  PF_REQUIRE(engine.supersede(supersede).accepted());
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::Superseded);
  // The exact replay is idempotent; a new supersession attempt on an already
  // superseded port is already current.
  PF_CHECK_EQ(engine.supersede(supersede).outcome.code(), OutcomeCode::Idempotent);
  FenceRequest repeated = supersede;
  repeated.envelope.authority.attempt = MutationAttemptId::from_validated("supersede-2");
  PF_CHECK_EQ(engine.supersede(repeated).outcome.code(), OutcomeCode::AlreadyCurrent);

  AdministrativeRequest blocked;
  blocked.envelope =
      pf_test::envelope(port, "agent-a", "boot-a", "blocked-1", engine.epoch());
  blocked.envelope.authority.ownership = ownership->id;
  blocked.envelope.authority.expected_configuration = generation;
  blocked.op = AdministrativeOp::Disable;
  PF_CHECK_EQ(engine.administrative(blocked).outcome.code(), OutcomeCode::PortSuperseded);

  ReconcileRequest reconcile;
  reconcile.envelope = pf_test::envelope(port, "agent-a", "boot-a", "reconcile-1", engine.epoch());
  reconcile.envelope.authority.ownership = engine.ownership(port)->id;
  reconcile.envelope.authority.expected_configuration = generation;
  reconcile.detail = "explicit reconciliation";
  const MutationResult reconciled = engine.reconcile(reconcile);
  PF_REQUIRE(reconciled.accepted());
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::Configured);
  PF_CHECK(engine.configuration(port)->generation > generation);
}

PF_TEST(device_replacement_does_not_inherit_authority) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(engine
                 .register_device(fixture.device, DeviceGeneration::from_value(1), "registry")
                 .accepted());
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  const auto ownership = engine.ownership(port);
  const auto generation = engine.configuration(port)->generation;

  PF_REQUIRE(engine
                 .register_device(fixture.device, DeviceGeneration::from_value(2), "registry")
                 .accepted());
  // Stale device generation is rejected before any semantic work.
  AdministrativeRequest stale;
  stale.envelope = pf_test::envelope(port, "agent-a", "boot-a", "stale-device", engine.epoch());
  stale.envelope.authority.ownership = ownership->id;
  stale.envelope.authority.expected_configuration = generation;
  stale.envelope.authority.expected_device = DeviceGeneration::from_value(1);
  stale.op = AdministrativeOp::Disable;
  PF_CHECK_EQ(engine.administrative(stale).outcome.code(), OutcomeCode::StaleDeviceGeneration);

  ReconcileRequest reconcile;
  reconcile.envelope = pf_test::envelope(port, "agent-a", "boot-a", "reconcile-device", engine.epoch());
  reconcile.envelope.authority.ownership = ownership->id;
  reconcile.envelope.authority.expected_configuration = generation;
  const MutationResult reconciled = engine.reconcile(reconcile);
  PF_REQUIRE(reconciled.accepted());
  PF_CHECK_EQ(engine.configuration(port)->device_generation.value(), 2ull);
}

PF_TEST(topology_generation_change_is_visible_and_fences_stale_expectations) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(engine.set_topology_generation(TopologyGeneration::from_value(10)).accepted());
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  const auto ownership = engine.ownership(port);
  const auto generation = engine.configuration(port)->generation;
  PF_CHECK_EQ(engine.configuration(port)->topology_generation.value(), 10ull);
  PF_REQUIRE(engine.set_topology_generation(TopologyGeneration::from_value(11)).accepted());
  PF_CHECK_EQ(engine.set_topology_generation(TopologyGeneration::from_value(10)).code(),
              OutcomeCode::StaleTopologyGeneration);

  AdministrativeRequest stale;
  stale.envelope = pf_test::envelope(port, "agent-a", "boot-a", "stale-topology", engine.epoch());
  stale.envelope.authority.ownership = ownership->id;
  stale.envelope.authority.expected_configuration = generation;
  stale.envelope.authority.expected_topology = TopologyGeneration::from_value(10);
  stale.op = AdministrativeOp::Disable;
  PF_CHECK_EQ(engine.administrative(stale).outcome.code(), OutcomeCode::StaleTopologyGeneration);

  // The explanation names the divergence.
  const Explanation explanation = engine.explain(port);
  PF_CHECK(explanation.has(ExplanationCode::TopologyGenerationAdvanced));
}

PF_TEST(applied_evidence_distinguishes_desired_from_applied) {
  pf_test::Fixture fixture;
  EngineConfig config = pf_test::engine_config();
  config.adapter_policy = AdapterPolicy::DesiredStateOnly;
  PortFabricEngine engine(config, &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  const auto applied = engine.applied_evidence(port);
  PF_REQUIRE(applied.has_value());
  PF_CHECK_EQ(applied->outcome, AppliedOutcome::NotAttempted);
  PF_CHECK(!applied->current_for(engine.configuration(port)->generation));
  // Desired state is authoritative even though nothing was applied.
  PF_CHECK(engine.configuration(port).has_value());

  // Recording verified evidence for the current generation marks the port in sync.
  EvidenceRequest evidence;
  evidence.envelope =
      pf_test::envelope(port, "agent-a", "boot-a", "evidence-1", engine.epoch());
  evidence.envelope.authority.ownership = engine.ownership(port)->id;
  evidence.evidence.outcome = AppliedOutcome::Verified;
  evidence.evidence.generation = engine.configuration(port)->generation;
  evidence.evidence.source = "test-adapter";
  evidence.evidence.verified = true;
  PF_REQUIRE(engine.record_applied_evidence(evidence).accepted());
  const auto record = engine.record(port);
  PF_REQUIRE(record.has_value());
  PF_CHECK_EQ(record->drift, DriftState::InSync);
  PF_CHECK(record->applied.current_for(record->generations.configuration));

  // Evidence for an older generation is stale.
  EvidenceRequest stale = evidence;
  stale.envelope.authority.attempt = MutationAttemptId::from_validated("evidence-2");
  stale.evidence.generation = PortConfigurationGeneration::from_value(99);
  PF_CHECK_EQ(engine.record_applied_evidence(stale).outcome.code(),
              OutcomeCode::StaleConfigurationGeneration);
}

PF_TEST(ambiguous_application_requires_reconciliation) {
  pf_test::Fixture fixture;
  EngineConfig config = pf_test::engine_config();
  config.adapter_policy = AdapterPolicy::ApplyAndVerify;
  PortFabricEngine engine(config, &fixture.provider, &fixture.adapter);
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

  // The device disappears mid apply: the outcome is unknown.
  fixture.fabric.set_fault(port, SyntheticFault::DeviceDisappeared);
  ConfigureRequest configure;
  configure.envelope =
      pf_test::envelope(port, "agent-a", "boot-a", "configure-1", engine.epoch());
  configure.envelope.authority.ownership = engine.ownership(port)->id;
  configure.configuration = fixture.configuration(port, *capability);
  const MutationResult uncertain = engine.configure(configure);
  PF_CHECK_EQ(uncertain.outcome.code(), OutcomeCode::ApplyFailed);
  PF_CHECK(!engine.configuration(port).has_value());

  fixture.fabric.set_fault(port, SyntheticFault::TransportFailure);
  configure.envelope.authority.attempt = MutationAttemptId::from_validated("configure-2");
  const MutationResult unknown = engine.configure(configure);
  PF_CHECK_EQ(unknown.outcome.code(), OutcomeCode::ApplyOutcomeUnknown);
  PF_CHECK(!engine.configuration(port).has_value());
  PF_CHECK_EQ(engine.record(port)->applied.outcome, AppliedOutcome::OutcomeUnknown);
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::RevalidationRequired);
  PF_CHECK_EQ(engine.record(port)->drift, DriftState::RevalidationRequired);
}

PF_TEST(drift_observation_is_recorded_without_overwriting_desired_state) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  const auto before = *engine.configuration(port);

  DriftRequest drift;
  drift.envelope = pf_test::envelope(port, "agent-a", "boot-a", "drift-1", engine.epoch());
  drift.envelope.authority.ownership = engine.ownership(port)->id;
  drift.observed = DriftState::Drifted;
  drift.detail = "external change detected by readback";
  PF_REQUIRE(engine.observe_external_state(drift).accepted());
  PF_CHECK_EQ(engine.record(port)->drift, DriftState::Drifted);
  PF_CHECK_EQ(engine.configuration(port)->mtu.value(), before.mtu.value());
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::Configured);

  drift.envelope.authority.attempt = MutationAttemptId::from_validated("drift-2");
  drift.observed = DriftState::Conflicted;
  PF_REQUIRE(engine.observe_external_state(drift).accepted());
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::Conflicted);
  PF_CHECK_EQ(pf_test::administer(engine, port, AdministrativeOp::Disable, "admin-1").outcome.code(),
              OutcomeCode::ConflictDetected);
}

PF_TEST(reconfiguration_rejects_administrative_change) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  ReconfigureRequest request;
  request.envelope =
      pf_test::envelope(port, "agent-a", "boot-a", "reconfigure-1", engine.epoch());
  request.envelope.authority.ownership = engine.ownership(port)->id;
  request.envelope.authority.expected_configuration = engine.configuration(port)->generation;
  request.configuration = *engine.configuration(port);
  request.configuration.administrative = AdministrativeState::Disabled;
  PF_CHECK_EQ(engine.reconfigure(request).outcome.code(), OutcomeCode::InvalidConfiguration);
}

PF_TEST(semantically_identical_reconfiguration_is_already_current) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  const auto generation = engine.configuration(port)->generation;
  ReconfigureRequest request;
  request.envelope =
      pf_test::envelope(port, "agent-a", "boot-a", "reconfigure-identical", engine.epoch());
  request.envelope.authority.ownership = engine.ownership(port)->id;
  request.envelope.authority.expected_configuration = generation;
  request.configuration = *engine.configuration(port);
  PF_CHECK_EQ(engine.reconfigure(request).outcome.code(), OutcomeCode::AlreadyCurrent);
  PF_CHECK_EQ(engine.configuration(port)->generation, generation);
}

PF_TEST(indexes_stay_consistent_with_records) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  for (std::uint32_t index = 1; index <= 6; ++index) {
    const auto result = pf_test::bind_claim_configure(engine, fixture, fixture.port(index));
    PF_REQUIRE(result.accepted());
    (void)result;
  }
  PF_CHECK_EQ(engine.ports_of_device(fixture.device).size(), static_cast<std::size_t>(6));
  PF_CHECK_EQ(engine.ports_with_lifecycle(PortLifecycle::Configured).size(),
              static_cast<std::size_t>(6));
  PF_CHECK_EQ(engine.ports_with_administrative_state(AdministrativeState::Enabled).size(),
              static_cast<std::size_t>(6));

  PF_REQUIRE(pf_test::administer(engine, fixture.port(1), AdministrativeOp::Disable, "d1").accepted());
  PF_CHECK_EQ(engine.ports_with_lifecycle(PortLifecycle::Configured).size(),
              static_cast<std::size_t>(5));
  PF_CHECK_EQ(engine.ports_with_lifecycle(PortLifecycle::AdminDisabled).size(),
              static_cast<std::size_t>(1));
  PF_CHECK_EQ(engine.ports_with_administrative_state(AdministrativeState::Disabled).size(),
              static_cast<std::size_t>(1));

  FenceRequest retire;
  retire.envelope = pf_test::envelope(fixture.port(2), "agent-a", "boot-a", "retire-1", engine.epoch());
  retire.envelope.authority.ownership = engine.ownership(fixture.port(2))->id;
  PF_REQUIRE(engine.retire(retire).accepted());
  PF_CHECK_EQ(engine.ports_with_lifecycle(PortLifecycle::Retired).size(), static_cast<std::size_t>(1));
  PF_CHECK_EQ(engine.ports_with_administrative_state(AdministrativeState::Disabled).size(),
              static_cast<std::size_t>(1));
}

PF_TEST(snapshot_currentness_is_explicit) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  const Snapshot before = engine.snapshot();
  PF_CHECK(before.current_for(engine.generation(), engine.epoch()));
  PF_CHECK(!before.digest.is_zero());
  PF_CHECK(before.find(port) != nullptr);

  PF_REQUIRE(pf_test::administer(engine, port, AdministrativeOp::Disable, "d1").accepted());
  PF_CHECK(!before.current_for(engine.generation(), engine.epoch()));
  const Snapshot after = engine.snapshot();
  PF_CHECK(after.current_for(engine.generation(), engine.epoch()));
  PF_CHECK(after.digest != before.digest);
  // The old snapshot remains inspectable.
  PF_CHECK_EQ(before.port_count(), static_cast<std::size_t>(1));
  PF_CHECK(before.find(port) != nullptr);
}

PF_TEST(query_does_not_expose_mutable_state) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  const Digest before = digest_record(*engine.record(port));
  // Mutating the returned copy must not change authoritative state.
  {
    auto copy = engine.record(port);
    PF_REQUIRE(copy.has_value());
    copy->lifecycle = PortLifecycle::Retired;
    copy->status_reason = "tampered";
  }
  PF_CHECK_EQ(digest_record(*engine.record(port)), before);
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::Configured);
}

PF_TEST(shutdown_rejects_further_mutation) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  PF_CHECK(!engine.shutting_down());
  engine.shutdown();
  PF_CHECK(engine.shutting_down());
  PF_CHECK_EQ(pf_test::administer(engine, port, AdministrativeOp::Disable, "d1").outcome.code(),
              OutcomeCode::ShuttingDown);
  engine.shutdown();
}
