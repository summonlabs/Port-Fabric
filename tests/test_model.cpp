// Identity, generation, characteristic, lifecycle, administrative and digest
// semantics.

#include <set>
#include <string>
#include <vector>

#include "portfabric/characteristics.hpp"
#include "portfabric/digest.hpp"
#include "portfabric/ids.hpp"
#include "portfabric/lifecycle.hpp"
#include "portfabric/outcome.hpp"
#include "pf_test.hpp"
#include "support.hpp"

using namespace portfabric;

PF_TEST(identifier_validation_rejects_malformed_encodings) {
  PF_CHECK(validate_identifier("port-01") == IdValidation::Ok);
  PF_CHECK(validate_identifier("") == IdValidation::Empty);
  PF_CHECK(validate_identifier("-port") == IdValidation::LeadingSeparator);
  PF_CHECK(validate_identifier("port-") == IdValidation::TrailingSeparator);
  PF_CHECK(validate_identifier("port..1") == IdValidation::DotDot);
  PF_CHECK(validate_identifier("port 1") == IdValidation::InvalidCharacter);
  PF_CHECK(validate_identifier("port\\1") == IdValidation::InvalidCharacter);
  PF_CHECK((validate_identifier(std::string(max_identifier_length + 1, 'a')) ==
            IdValidation::TooLong));
  PF_CHECK((validate_identifier(std::string(max_identifier_length, 'a')) == IdValidation::Ok));
  IdValidation reason = IdValidation::Ok;
  PF_CHECK(!PortId::parse("..", reason).has_value());
  PF_CHECK(reason == IdValidation::DotDot);
  PF_CHECK(!PortId::parse("trailing.").has_value());
  PF_CHECK(!PortId::from_validated("bad id").valid());
  PF_CHECK(!PortId{}.valid());
}

PF_TEST(identifier_domains_do_not_convert_implicitly) {
  const auto port = PortId::from_validated("port-01");
  const auto device = DeviceId::from_validated("port-01");
  // Same encoding, different domains: the values are distinct types and there is
  // no implicit conversion between them. This is enforced at compile time; here
  // the encodings and hashes are compared to document the intent.
  PF_CHECK_EQ(port.to_string(), device.to_string());
  PF_CHECK(port.hash() == device.hash());
  PF_CHECK(port.valid());
  PF_CHECK((!std::is_convertible_v<PortId, DeviceId>));
  PF_CHECK((!std::is_convertible_v<DeviceId, PortId>));
}

PF_TEST(entity_classes_round_trip) {
  for (std::uint8_t index = 0; index <= static_cast<std::uint8_t>(PortEntityClass::SyntheticPort);
       ++index) {
    const auto entity_class = static_cast<PortEntityClass>(index);
    PortEntityClass parsed = PortEntityClass::Unknown;
    PF_REQUIRE(parse_entity_class(to_string(entity_class), parsed));
    PF_CHECK(parsed == entity_class);
    PF_CHECK(is_valid(entity_class));
  }
}

PF_TEST(generations_are_monotonic_and_bounded) {
  const auto first = PortConfigurationGeneration::from_value(1);
  PF_CHECK(first.valid());
  PF_CHECK(first.next().value() == 2);
  PF_CHECK(first < first.next());
  PF_CHECK(!PortConfigurationGeneration{}.valid());
  const auto last = PortConfigurationGeneration::from_value(0xffffffffffffffffull);
  PF_CHECK(last.exhaustible());
  PF_CHECK(last.next() == last);
}

PF_TEST(speed_parsing_is_canonical_and_bounded) {
  PF_CHECK_EQ(PortSpeed::parse("400G")->bits_per_second(), 400000000000ull);
  PF_CHECK_EQ(PortSpeed::parse("2.5G")->bits_per_second(), 2500000000ull);
  PF_CHECK_EQ(PortSpeed::parse("100M")->bits_per_second(), 100000000ull);
  PF_CHECK_EQ(PortSpeed::parse("1000000000")->bits_per_second(), 1000000000ull);
  PF_CHECK_EQ(PortSpeed::parse("400G")->to_string(), std::string("400G"));
  PF_CHECK_EQ(PortSpeed::parse("2.5G")->to_string(), std::string("2.5G"));
  PF_CHECK_EQ(PortSpeed::parse("100M")->to_string(), std::string("100M"));
  PF_CHECK(!PortSpeed::parse("").has_value());
  PF_CHECK(!PortSpeed::parse("0G").has_value());
  PF_CHECK(!PortSpeed::parse("400").has_value());
  PF_CHECK(!PortSpeed::parse("400T").has_value());
  PF_CHECK(!PortSpeed::parse("-400G").has_value());
  PF_CHECK(!PortSpeed::parse("400G.5").has_value());
  PF_CHECK(!PortSpeed::parse("99999999999999999999G").has_value());
  PF_CHECK(!PortSpeed::parse("1.2345G").has_value());
  PF_CHECK(!PortSpeed::from_bits_per_second(0).has_value());
  PF_CHECK(!PortSpeed::from_bits_per_second(1).has_value());
  PF_CHECK(!PortSpeed::from_bits_per_second(max_speed_bits_per_second + 1).has_value());
}

PF_TEST(lane_configuration_is_consistent) {
  const auto lanes = LaneConfiguration::for_total(4, *PortSpeed::parse("400G"));
  PF_REQUIRE(lanes.has_value());
  PF_CHECK_EQ(lanes->lanes(), 4u);
  PortSpeed total;
  PF_CHECK(lanes->total(total));
  PF_CHECK_EQ(total.bits_per_second(), 400000000000ull);
  PF_CHECK(!LaneConfiguration::create(0, *PortSpeed::parse("100G")).has_value());
  PF_CHECK(!LaneConfiguration::create(max_lanes + 1, *PortSpeed::parse("100G")).has_value());
  // 100G cannot be divided into three equal lanes at a representable rate.
  PF_CHECK(!LaneConfiguration::for_total(3, *PortSpeed::parse("100G")).has_value());
  PF_CHECK(!LaneConfiguration{}.valid());
}

PF_TEST(mtu_is_bounded) {
  PF_CHECK(Mtu::create(min_mtu - 1).has_value() == false);
  PF_CHECK(Mtu::create(min_mtu).has_value());
  PF_CHECK(Mtu::create(max_mtu).has_value());
  PF_CHECK(!Mtu::create(max_mtu + 1).has_value());
  PF_CHECK_EQ(Mtu::create(9000)->to_string(), std::string("9000"));
}

PF_TEST(lifecycle_transition_relation_is_total) {
  const PortLifecycle states[] = {
      PortLifecycle::Unconfigured, PortLifecycle::Configured,  PortLifecycle::Active,
      PortLifecycle::AdminDisabled, PortLifecycle::Draining,  PortLifecycle::Maintenance,
      PortLifecycle::RevalidationRequired, PortLifecycle::Superseded, PortLifecycle::Retired,
      PortLifecycle::Conflicted};
  const LifecycleTransition transitions[] = {
      LifecycleTransition::Configure,         LifecycleTransition::Activate,
      LifecycleTransition::AdminDisable,      LifecycleTransition::AdminEnable,
      LifecycleTransition::BeginDrain,        LifecycleTransition::CompleteDrain,
      LifecycleTransition::EnterMaintenance,  LifecycleTransition::ExitMaintenance,
      LifecycleTransition::RequireRevalidation, LifecycleTransition::Supersede,
      LifecycleTransition::Retire,            LifecycleTransition::Conflict,
      LifecycleTransition::Reconcile};
  for (const PortLifecycle state : states) {
    PF_REQUIRE(is_valid(state));
    for (const LifecycleTransition transition : transitions) {
      const TransitionEvaluation evaluation = evaluate_transition(state, transition);
      PF_CHECK(!evaluation.reason.empty());
      if (evaluation.allowed) {
        PF_CHECK(is_valid(evaluation.result));
      } else {
        PF_CHECK(evaluation.result == state);
      }
    }
  }
}

PF_TEST(lifecycle_illegal_transitions_reject_deterministically) {
  PF_CHECK(!evaluate_transition(PortLifecycle::Unconfigured, LifecycleTransition::Activate).allowed);
  PF_CHECK(!evaluate_transition(PortLifecycle::Configured, LifecycleTransition::Configure).allowed);
  PF_CHECK(!evaluate_transition(PortLifecycle::Retired, LifecycleTransition::Activate).allowed);
  PF_CHECK(!evaluate_transition(PortLifecycle::Retired, LifecycleTransition::Reconcile).allowed);
  PF_CHECK(!evaluate_transition(PortLifecycle::Retired, LifecycleTransition::Supersede).allowed);
  PF_CHECK(evaluate_transition(PortLifecycle::Configured, LifecycleTransition::Activate).allowed);
  PF_CHECK(evaluate_transition(PortLifecycle::Active, LifecycleTransition::BeginDrain).allowed);
  PF_CHECK(evaluate_transition(PortLifecycle::Draining, LifecycleTransition::CompleteDrain).allowed);
  PF_CHECK(evaluate_transition(PortLifecycle::Maintenance, LifecycleTransition::Activate).allowed);
  PF_CHECK(evaluate_transition(PortLifecycle::AdminDisabled, LifecycleTransition::Activate).allowed);
  PF_CHECK(evaluate_transition(PortLifecycle::Active, LifecycleTransition::RequireRevalidation)
               .allowed);
  PF_CHECK(evaluate_transition(PortLifecycle::Active, LifecycleTransition::Supersede).allowed);
  PF_CHECK(evaluate_transition(PortLifecycle::Active, LifecycleTransition::Retire).allowed);
  PF_CHECK(evaluate_transition(PortLifecycle::RevalidationRequired, LifecycleTransition::Reconcile)
               .allowed);
  PF_CHECK(evaluate_transition(PortLifecycle::Superseded, LifecycleTransition::Reconcile).allowed);
  PF_CHECK(!evaluate_transition(PortLifecycle::Active, LifecycleTransition::Reconcile).allowed);
  PF_CHECK(is_live(PortLifecycle::Active));
  PF_CHECK(!is_live(PortLifecycle::RevalidationRequired));
  PF_CHECK(is_terminal(PortLifecycle::Retired));
  PF_CHECK(requires_reconciliation(PortLifecycle::Superseded));
}

PF_TEST(administrative_state_mapping_is_explicit) {
  PF_CHECK_EQ(administrative_target(AdministrativeOp::Enable), AdministrativeState::Enabled);
  PF_CHECK_EQ(administrative_target(AdministrativeOp::Disable), AdministrativeState::Disabled);
  PF_CHECK_EQ(administrative_target(AdministrativeOp::Drain), AdministrativeState::Draining);
  PF_CHECK_EQ(administrative_target(AdministrativeOp::EnterMaintenance),
              AdministrativeState::Maintenance);
  PF_CHECK_EQ(administrative_target(AdministrativeOp::Resume), AdministrativeState::Enabled);
  PF_CHECK_EQ(lifecycle_transition_for(AdministrativeOp::Resume), LifecycleTransition::Activate);
  PF_CHECK_EQ(administrative_state_of(PortLifecycle::Active), AdministrativeState::Enabled);
  PF_CHECK_EQ(administrative_state_of(PortLifecycle::AdminDisabled), AdministrativeState::Disabled);
  PF_CHECK_EQ(administrative_state_of(PortLifecycle::Unconfigured), AdministrativeState::Unknown);
}

PF_TEST(outcome_codes_render_stably) {
  PF_CHECK_EQ(std::string(to_string(OutcomeCode::StaleConfigurationGeneration)),
              std::string("STALE_CONFIGURATION_GENERATION"));
  PF_CHECK_EQ(std::string(to_string(OutcomeCode::CapabilityUnknown)),
              std::string("CAPABILITY_UNKNOWN"));
  PF_CHECK_EQ(std::string(to_string(OutcomeCode::ApplyOutcomeUnknown)),
              std::string("APPLY_OUTCOME_UNKNOWN"));
  PF_CHECK(is_accepted(OutcomeCode::Idempotent));
  PF_CHECK(is_accepted(OutcomeCode::AlreadyCurrent));
  PF_CHECK(!is_accepted(OutcomeCode::StaleWorkerBoot));
  PF_CHECK(advances_generation(OutcomeCode::Ok));
  PF_CHECK(!advances_generation(OutcomeCode::Idempotent));
  const Outcome outcome = Outcome::failure(OutcomeCode::ApplyFailed, "detail")
                               .with("adapter", "synthetic")
                               .with("port", "p1");
  PF_CHECK_EQ(outcome.to_string(), std::string("APPLY_FAILED: detail adapter=synthetic port=p1"));
}

PF_TEST(configuration_digest_is_order_independent) {
  pf_test::Fixture fixture;
  portfabric::PortFabricEngine engine(pf_test::engine_config(), &fixture.provider,
                                      &fixture.adapter);
  const PortId first = fixture.port(1);
  const PortId second = fixture.port(2);
  std::string provider_error;
  const auto capability = fixture.provider.current(first, provider_error);
  PF_REQUIRE(capability.has_value());

  const PortConfiguration configuration =
      fixture.configuration(first, *capability);
  PortConfiguration same = configuration;
  same.mtu = configuration.mtu;
  same.speed = configuration.speed;
  PF_CHECK(digest_configuration(configuration) == digest_configuration(same));

  PortConfiguration different = configuration;
  different.mtu = *Mtu::create(9000);
  PF_CHECK(digest_configuration(configuration) != digest_configuration(different));

  // A different port identity must produce a different digest.
  PortConfiguration other_port = configuration;
  other_port.port = second;
  PF_CHECK(digest_configuration(configuration) != digest_configuration(other_port));

  // The mutation attempt identity is bookkeeping, not semantic state.
  PortConfiguration with_attempt = configuration;
  with_attempt.provenance.attempt = MutationAttemptId::from_validated("attempt-1");
  PF_CHECK(digest_configuration(configuration) == digest_configuration(with_attempt));
  (void)engine;
}

PF_TEST(record_digest_is_stable_and_complete) {
  pf_test::Fixture fixture;
  portfabric::PortFabricEngine engine(pf_test::engine_config(), &fixture.provider,
                                      &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  const auto record = engine.record(port);
  PF_REQUIRE(record.has_value());
  const Digest first = digest_record(*record);
  const Digest second = digest_record(*record);
  PF_CHECK(first == second);
  PF_CHECK(!first.is_zero());
  PF_CHECK_EQ(first.to_hex().size(), static_cast<std::size_t>(64));

  PortRecord modified = *record;
  modified.status_reason = "DIFFERENT";
  PF_CHECK(digest_record(modified) != first);
}

PF_TEST(record_set_digest_ignores_insertion_order) {
  pf_test::Fixture fixture;
  portfabric::PortFabricEngine engine(pf_test::engine_config(), &fixture.provider,
                                      &fixture.adapter);
  for (std::uint32_t index = 1; index <= 4; ++index) {
    PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, fixture.port(index)).accepted());
  }
  const Snapshot snapshot = engine.snapshot();
  std::vector<PortRecord> forward = snapshot.records;
  std::vector<PortRecord> reversed(forward.rbegin(), forward.rend());
  PF_CHECK(digest_records(forward) == digest_records(reversed));
  PF_CHECK_EQ(snapshot.port_count(), static_cast<std::size_t>(4));
}
