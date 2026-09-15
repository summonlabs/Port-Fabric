// Adversarial hardening: malformed input, stale authority, replayed identities,
// exhausted bounds, injected adapter failures and ambiguous completion.

#include <string>
#include <thread>
#include <vector>

#include "pf_test.hpp"
#include "support.hpp"

using namespace portfabric;

namespace {

/// Adapter whose behaviour is scripted per call.
class ScriptedAdapter : public PortAdapter {
 public:
  std::string_view label() const noexcept override { return "scripted"; }

  AdapterApplyResult apply(const PortId& port, const PortConfiguration& configuration) override {
    (void)port;
    (void)configuration;
    std::lock_guard<std::mutex> lock(mutex);
    calls.fetch_add(1);
    if (!script.empty()) {
      const AdapterApplyResult result = script.front();
      script.erase(script.begin());
      return result;
    }
    return AdapterApplyResult::applied("scripted apply");
  }

  AdapterReadback readback(const PortId& port) override {
    (void)port;
    std::lock_guard<std::mutex> lock(mutex);
    readbacks.fetch_add(1);
    if (!readback_script.empty()) {
      const AdapterReadback result = readback_script.front();
      readback_script.erase(readback_script.begin());
      return result;
    }
    return AdapterReadback::verified(PortConfiguration{}, "scripted readback");
  }

  /// Queues one scripted apply result.
  void queue(AdapterApplyResult result) {
    std::lock_guard<std::mutex> lock(mutex);
    script.push_back(std::move(result));
  }

  /// Queues one scripted readback result.
  void queue(AdapterReadback result) {
    std::lock_guard<std::mutex> lock(mutex);
    readback_script.push_back(std::move(result));
  }

  std::mutex mutex;
  std::vector<AdapterApplyResult> script;
  std::vector<AdapterReadback> readback_script;
  std::atomic<std::size_t> calls{0};
  std::atomic<std::size_t> readbacks{0};
};

}  // namespace

PF_TEST(malformed_requests_are_rejected_without_side_effects) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  const Digest before = engine.state_digest();

  AdministrativeRequest request;
  request.envelope.port = PortId{};
  request.envelope.authority.epoch = engine.epoch();
  request.envelope.authority.publisher = PublisherId::from_validated("agent-a");
  request.envelope.authority.boot = WorkerBootId::from_validated("boot-a");
  request.envelope.authority.attempt = MutationAttemptId::from_validated("malformed-1");
  PF_CHECK_EQ(engine.administrative(request).outcome.code(), OutcomeCode::MalformedRequest);

  request.envelope.port = port;
  request.envelope.authority.attempt = MutationAttemptId{};
  PF_CHECK_EQ(engine.administrative(request).outcome.code(), OutcomeCode::MalformedRequest);

  request.envelope.authority.attempt = MutationAttemptId::from_validated("malformed-2");
  request.envelope.authority.publisher = PublisherId{};
  PF_CHECK_EQ(engine.administrative(request).outcome.code(), OutcomeCode::MalformedRequest);

  request.envelope.authority.publisher = PublisherId::from_validated("agent-a");
  request.op = static_cast<AdministrativeOp>(200);
  PF_CHECK_EQ(engine.administrative(request).outcome.code(), OutcomeCode::MalformedRequest);

  PF_CHECK_EQ(engine.state_digest(), before);
}

PF_TEST(unknown_port_and_wrong_entity_class_are_distinguished) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId unknown = PortId::from_validated("never-bound");
  PF_CHECK_EQ(pf_test::administer(engine, unknown, AdministrativeOp::Enable, "a1").outcome.code(),
              OutcomeCode::UnknownPort);
  PF_CHECK(!engine.record(unknown).has_value());
  PF_CHECK_EQ(engine.explain(unknown).outcome, OutcomeCode::UnknownPort);

  const PortId port = fixture.port(1);
  PortBindingRequest binding;
  binding.port = port;
  binding.parent_device = fixture.device;
  binding.device_generation = DeviceGeneration::from_value(1);
  binding.epoch = engine.epoch();
  binding.attempt = MutationAttemptId::from_validated("bind-1");
  binding.entity_class = static_cast<PortEntityClass>(200);
  PF_CHECK_EQ(engine.bind_port(binding).outcome.code(), OutcomeCode::WrongEntityClass);
}

PF_TEST(stale_worker_boot_and_epoch_are_rejected_before_semantics) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());

  // Under explicit registration an unknown publisher cannot mutate. A publisher
  // that is known but presents a different boot is always rejected.
  AdministrativeRequest request;
  request.envelope = pf_test::envelope(port, "ghost", "boot-a", "ghost-1", engine.epoch());
  request.op = AdministrativeOp::Disable;
  {
    EngineConfig strict_config = pf_test::engine_config();
    strict_config.publisher_registration = PublisherRegistration::Explicit;
    PortFabricEngine strict(strict_config, &fixture.provider, &fixture.adapter);
    PF_CHECK_EQ(strict.administrative(request).outcome.code(), OutcomeCode::UnknownPublisher);
    PF_CHECK_EQ(strict.port_count(), static_cast<std::size_t>(0));
  }
  // Under automatic registration the first use of an incarnation registers it,
  // so the rejection that follows is about ownership rather than identity.
  PF_CHECK_EQ(engine.administrative(request).outcome.code(), OutcomeCode::UnauthorizedOwner);

  // A registered publisher presenting a different boot.
  PF_REQUIRE(engine
                 .register_publisher(PublisherId::from_validated("agent-b"),
                                     WorkerBootId::from_validated("boot-b"), engine.epoch())
                 .accepted());
  request.envelope = pf_test::envelope(port, "agent-b", "boot-other", "ghost-2", engine.epoch());
  PF_CHECK_EQ(engine.administrative(request).outcome.code(), OutcomeCode::StaleWorkerBoot);

  // A stale coordinator epoch.
  request.envelope = pf_test::envelope(port, "agent-b", "boot-b", "ghost-3",
                                       CoordinatorEpoch::from_value(999));
  PF_CHECK_EQ(engine.administrative(request).outcome.code(), OutcomeCode::StaleCoordinatorEpoch);

  // Advancing the epoch fences every earlier incarnation.
  CoordinatorEpoch assigned;
  PF_REQUIRE(engine.begin_epoch(assigned).accepted());
  PF_CHECK(assigned.value() >= 2ull);
  request.envelope = pf_test::envelope(port, "agent-b", "boot-b", "ghost-4",
                                       CoordinatorEpoch::from_value(1));
  PF_CHECK_EQ(engine.administrative(request).outcome.code(), OutcomeCode::StaleCoordinatorEpoch);
  // An epoch advance fences every incarnation registered under the earlier epoch,
  // including the same boot identity. Regaining authority requires a genuinely
  // fresh process incarnation, never the survival of an old one.
  PF_CHECK_EQ(engine
                  .register_publisher(PublisherId::from_validated("agent-b"),
                                      WorkerBootId::from_validated("boot-b"), assigned)
                  .code(),
              OutcomeCode::PublisherFenced);
  PF_REQUIRE(engine
                 .register_publisher(PublisherId::from_validated("agent-b"),
                                     WorkerBootId::from_validated("boot-b2"), assigned)
                 .accepted());
  request.envelope = pf_test::envelope(port, "agent-a", "boot-a", "ghost-5", assigned);
  PF_CHECK_EQ(engine.administrative(request).outcome.code(), OutcomeCode::PublisherFenced);
}

PF_TEST(attempt_identity_reuse_is_a_conflict) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  const auto ownership = engine.ownership(port);
  const auto capability = engine.capability_binding(port);

  ReconfigureRequest request;
  request.envelope = pf_test::envelope(port, "agent-a", "boot-a", "reuse-1", engine.epoch());
  request.envelope.authority.ownership = ownership->id;
  request.envelope.authority.expected_configuration = engine.configuration(port)->generation;
  request.configuration = fixture.configuration(port, *capability);
  request.configuration.mtu = *Mtu::create(9000);
  PF_REQUIRE(engine.reconfigure(request).accepted());
  PF_CHECK_EQ(engine.reconfigure(request).outcome.code(), OutcomeCode::Idempotent);

  request.configuration.mtu = *Mtu::create(9200);
  PF_CHECK_EQ(engine.reconfigure(request).outcome.code(), OutcomeCode::ConflictDetected);
}

PF_TEST(text_bounds_are_enforced) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  const auto ownership = engine.ownership(port);
  const auto capability = engine.capability_binding(port);

  ReconfigureRequest request;
  request.envelope = pf_test::envelope(port, "agent-a", "boot-a", "text-1", engine.epoch());
  request.envelope.authority.ownership = ownership->id;
  request.envelope.authority.expected_configuration = engine.configuration(port)->generation;
  request.configuration = fixture.configuration(port, *capability);
  request.configuration.role = std::string(max_name_length + 1, 'x');
  PF_CHECK_EQ(engine.reconfigure(request).outcome.code(), OutcomeCode::InvalidConfiguration);

  request.envelope.authority.attempt = MutationAttemptId::from_validated("text-2");
  request.configuration.role = std::string("ok") + '\n' + "control";
  PF_CHECK_EQ(engine.reconfigure(request).outcome.code(), OutcomeCode::InvalidConfiguration);

  request.envelope.authority.attempt = MutationAttemptId::from_validated("text-3");
  request.configuration.role = std::string("valid-role");
  PF_CHECK(engine.reconfigure(request).accepted());
}

PF_TEST(invalid_enum_values_from_persistence_are_rejected) {
  // A persisted record carrying an impossible lifecycle value cannot be
  // constructed through the public model, which is the point: the decoders
  // reject out-of-range values before the model sees them. This test pins the
  // model level guarantee that a record with an unknown lifecycle never
  // validates.
  PortRecord record;
  record.port = PortId::from_validated("port-1");
  record.parent_device = DeviceId::from_validated("device-1");
  record.lifecycle = static_cast<PortLifecycle>(200);
  std::string error;
  PF_CHECK(!record.validate(error));
  PF_CHECK(!error.empty());
}

PF_TEST(resource_bounds_are_enforced_before_allocation) {
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Spine400G, 8);
  EngineConfig config = pf_test::engine_config();
  config.port_bound = 2;
  PortFabricEngine engine(config, &fixture.provider, &fixture.adapter);
  for (std::uint32_t index = 1; index <= 2; ++index) {
    PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, fixture.port(index)).accepted());
  }
  PortBindingRequest binding;
  binding.port = fixture.port(3);
  binding.parent_device = fixture.device;
  binding.device_generation = DeviceGeneration::from_value(1);
  binding.epoch = engine.epoch();
  binding.attempt = MutationAttemptId::from_validated("bind-3");
  PF_CHECK_EQ(engine.bind_port(binding).outcome.code(), OutcomeCode::ResourceBoundExceeded);
  PF_CHECK_EQ(engine.port_count(), static_cast<std::size_t>(2));
}

PF_TEST(device_bound_is_enforced) {
  pf_test::Fixture fixture;
  EngineConfig config = pf_test::engine_config();
  config.device_bound = 1;
  PortFabricEngine engine(config, &fixture.provider, &fixture.adapter);
  PF_REQUIRE(engine.register_device(fixture.device, DeviceGeneration::from_value(1), "a").accepted());
  PF_CHECK_EQ(engine.register_device(DeviceId::from_validated("second"), DeviceGeneration::from_value(1),
                                     "a")
                  .code(),
              OutcomeCode::ResourceBoundExceeded);
}

PF_TEST(adapter_failures_never_commit_configuration) {
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Server10G, 1);
  ScriptedAdapter adapter;
  EngineConfig config = pf_test::engine_config();
  config.adapter_policy = AdapterPolicy::ApplyAndVerify;
  PortFabricEngine engine(config, &fixture.provider, &adapter);
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

  adapter.queue(AdapterApplyResult::failed("permission denied"));
  ConfigureRequest configure;
  configure.envelope = pf_test::envelope(port, "agent-a", "boot-a", "configure-1", engine.epoch());
  configure.envelope.authority.ownership = engine.ownership(port)->id;
  configure.configuration = fixture.configuration(port, *capability);
  PF_CHECK_EQ(engine.configure(configure).outcome.code(), OutcomeCode::ApplyFailed);
  PF_CHECK(!engine.configuration(port).has_value());
  PF_CHECK_EQ(engine.port_count(), static_cast<std::size_t>(1));

  adapter.queue(AdapterApplyResult::unknown("connection lost"));
  configure.envelope.authority.attempt = MutationAttemptId::from_validated("configure-2");
  PF_CHECK_EQ(engine.configure(configure).outcome.code(), OutcomeCode::ApplyOutcomeUnknown);
  PF_CHECK(!engine.configuration(port).has_value());
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::RevalidationRequired);

  // The port must be reconciled before it can be configured again.
  configure.envelope.authority.attempt = MutationAttemptId::from_validated("configure-3");
  PF_CHECK_EQ(engine.configure(configure).outcome.code(), OutcomeCode::RevalidationRequired);
}

PF_TEST(readback_mismatch_does_not_commit_and_marks_revalidation) {
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Server10G, 1);
  ScriptedAdapter adapter;
  EngineConfig config = pf_test::engine_config();
  config.adapter_policy = AdapterPolicy::ApplyAndVerify;
  PortFabricEngine engine(config, &fixture.provider, &adapter);
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

  adapter.queue(AdapterApplyResult::applied("accepted"));
  adapter.queue(AdapterReadback::mismatch(PortConfiguration{}, "state differs"));
  ConfigureRequest configure;
  configure.envelope = pf_test::envelope(port, "agent-a", "boot-a", "configure-1", engine.epoch());
  configure.envelope.authority.ownership = engine.ownership(port)->id;
  configure.configuration = fixture.configuration(port, *capability);
  const MutationResult result = engine.configure(configure);
  PF_CHECK_EQ(result.outcome.code(), OutcomeCode::ReadbackMismatch);
  PF_CHECK(!engine.configuration(port).has_value());
  PF_CHECK_EQ(*engine.lifecycle(port), PortLifecycle::RevalidationRequired);
  PF_CHECK_EQ(engine.record(port)->applied.outcome, AppliedOutcome::OutcomeUnknown);
}

PF_TEST(verified_readback_records_verified_evidence) {
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Server10G, 1);
  ScriptedAdapter adapter;
  EngineConfig config = pf_test::engine_config();
  config.adapter_policy = AdapterPolicy::ApplyAndVerify;
  PortFabricEngine engine(config, &fixture.provider, &adapter);
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
  adapter.queue(AdapterApplyResult::applied("accepted"));
  adapter.queue(AdapterReadback::verified(PortConfiguration{}, "matches"));

  ConfigureRequest configure;
  configure.envelope = pf_test::envelope(port, "agent-a", "boot-a", "configure-1", engine.epoch());
  configure.envelope.authority.ownership = engine.ownership(port)->id;
  configure.configuration = fixture.configuration(port, *capability);
  PF_REQUIRE(engine.configure(configure).accepted());
  const auto applied = engine.applied_evidence(port);
  PF_REQUIRE(applied.has_value());
  PF_CHECK_EQ(applied->outcome, AppliedOutcome::Verified);
  PF_CHECK(applied->verified);
  PF_CHECK_EQ(engine.record(port)->drift, DriftState::InSync);
}

PF_TEST(repeated_publisher_start_stop_is_bounded_and_idempotent) {
  pf_test::Fixture fixture;
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  for (int round = 0; round < 16; ++round) {
    const std::string boot = "boot-" + std::to_string(round);
    PF_REQUIRE(engine
                   .register_publisher(PublisherId::from_validated("agent-a"),
                                       WorkerBootId::from_validated(boot), engine.epoch())
                   .accepted());
    const Outcome fenced = engine.fence_publisher(PublisherId::from_validated("agent-a"),
                                                 WorkerBootId::from_validated(boot));
    PF_CHECK(fenced.accepted());
    PF_CHECK(engine.fence_publisher(PublisherId::from_validated("agent-a"),
                                    WorkerBootId::from_validated(boot))
                  .accepted());
  }
  PF_CHECK_EQ(engine.publisher_count(), static_cast<std::size_t>(1));
  PF_CHECK(engine.is_publisher_fenced(PublisherId::from_validated("agent-a"),
                                      WorkerBootId::from_validated("boot-15")));
}

PF_TEST(concurrent_mutation_storm_keeps_indexes_consistent) {
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Spine400G, 24);
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  for (std::uint32_t index = 1; index <= 24; ++index) {
    PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, fixture.port(index)).accepted());
  }
  std::vector<std::thread> threads;
  for (std::uint32_t index = 1; index <= 24; ++index) {
    threads.emplace_back([&engine, &fixture, index]() {
      const PortId port = fixture.port(index);
      for (int round = 0; round < 8; ++round) {
        const AdministrativeOp op =
            round % 2 == 0 ? AdministrativeOp::Disable : AdministrativeOp::Enable;
        (void)pf_test::administer(engine, port, op,
                                  "storm-" + std::to_string(index) + "-" + std::to_string(round));
      }
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
  const std::size_t configured =
      engine.ports_with_lifecycle(PortLifecycle::Configured).size() +
      engine.ports_with_lifecycle(PortLifecycle::Active).size() +
      engine.ports_with_lifecycle(PortLifecycle::AdminDisabled).size();
  PF_CHECK_EQ(configured, static_cast<std::size_t>(24));
  PF_CHECK_EQ(engine.port_count(), static_cast<std::size_t>(24));
  const std::size_t by_device = engine.ports_of_device(fixture.device).size();
  PF_CHECK_EQ(by_device, static_cast<std::size_t>(24));
  PF_CHECK_EQ(engine.snapshot().port_count(), static_cast<std::size_t>(24));
}
