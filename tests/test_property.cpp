// Seeded property tests.
//
// Every case prints its seed on failure so that a failing run is reproducible.

#include <cstdint>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "pf_test.hpp"
#include "support.hpp"

using namespace portfabric;

namespace {

struct Rng {
  explicit Rng(std::uint64_t seed) : engine(seed), seed(seed) {}
  std::mt19937_64 engine;
  std::uint64_t seed;
  std::uint32_t next(std::uint32_t bound) { return static_cast<std::uint32_t>(engine() % bound); }
  bool coin() { return (engine() & 1ull) != 0ull; }
};

constexpr std::uint64_t kSeeds[] = {0x5eed0001ull, 0x5eed0002ull, 0x5eed0003ull};

}  // namespace

PF_TEST(property_generation_monotonicity_and_stale_rejection) {
  for (const std::uint64_t seed : kSeeds) {
    Rng rng(seed);
    pf_test::Fixture fixture(portfabric::SyntheticPortClass::Spine400G, 4);
    PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
    const PortId port = fixture.port(1);
    const MutationResult initial = pf_test::bind_claim_configure(engine, fixture, port);
    if (!initial.accepted()) {
      std::printf("seed=%llu initial configure rejected\n",
                  static_cast<unsigned long long>(seed));
      PF_CHECK(false);
      continue;
    }
    std::vector<PortConfigurationGeneration> observed;
    observed.push_back(engine.configuration(port)->generation);
    for (int round = 0; round < 12; ++round) {
      const auto ownership = engine.ownership(port);
      const auto current = engine.configuration(port);
      if (!ownership.has_value() || !current.has_value()) {
        PF_CHECK(false);
        break;
      }
      ReconfigureRequest request;
      request.envelope = pf_test::envelope(port, "agent-a", "boot-a",
                                           "prop-" + std::to_string(round), engine.epoch());
      request.envelope.authority.ownership = ownership->id;
      request.envelope.authority.expected_configuration =
          rng.coin() ? current->generation : PortConfigurationGeneration::from_value(1);
      request.configuration = *current;
      request.configuration.mtu = *Mtu::create(static_cast<std::uint32_t>(9000 + rng.next(100)));
      const MutationResult result = engine.reconfigure(request);
      if (request.envelope.authority.expected_configuration == current->generation) {
        if (!result.accepted()) {
          std::printf("seed=%llu round=%d unexpected %s\n",
                      static_cast<unsigned long long>(seed), round,
                      result.outcome.to_string().c_str());
          PF_CHECK(false);
          break;
        }
        const auto next = engine.configuration(port)->generation;
        if (!(next > observed.back())) {
          std::printf("seed=%llu generation did not advance\n",
                      static_cast<unsigned long long>(seed));
          PF_CHECK(false);
        }
        observed.push_back(next);
      } else {
        if (result.outcome.code() != OutcomeCode::StaleConfigurationGeneration) {
          std::printf("seed=%llu round=%d expected stale, got %s\n",
                      static_cast<unsigned long long>(seed), round,
                      result.outcome.to_string().c_str());
          PF_CHECK(false);
        }
        PF_CHECK_EQ(engine.configuration(port)->generation, observed.back());
      }
    }
  }
}

PF_TEST(property_retired_ports_never_become_live_again) {
  for (const std::uint64_t seed : kSeeds) {
    Rng rng(seed);
    pf_test::Fixture fixture(portfabric::SyntheticPortClass::Server25G, 4);
    PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
    const PortId port = fixture.port(1);
    if (!pf_test::bind_claim_configure(engine, fixture, port,
                                       portfabric::SyntheticPortClass::Server25G)
             .accepted()) {
      PF_CHECK(false);
      continue;
    }
    FenceRequest retire;
    retire.envelope = pf_test::envelope(port, "agent-a", "boot-a", "retire", engine.epoch());
    retire.envelope.authority.ownership = engine.ownership(port)->id;
    if (!engine.retire(retire).accepted()) {
      PF_CHECK(false);
      continue;
    }
    for (int round = 0; round < 8; ++round) {
      switch (rng.next(4)) {
        case 0:
          (void)pf_test::administer(engine, port, AdministrativeOp::Enable,
                                    "p-enable-" + std::to_string(round));
          break;
        case 1: {
          ReconcileRequest reconcile;
          reconcile.envelope = pf_test::envelope(port, "agent-a", "boot-a",
                                                 "p-reconcile-" + std::to_string(round),
                                                 engine.epoch());
          (void)engine.reconcile(reconcile);
          break;
        }
        case 2: {
          FenceRequest supersede;
          supersede.envelope = pf_test::envelope(port, "agent-a", "boot-a",
                                                 "p-supersede-" + std::to_string(round),
                                                 engine.epoch());
          (void)engine.supersede(supersede);
          break;
        }
        default: {
          const MutationResult result = pf_test::bind_claim_configure(engine, fixture, port);
          (void)result;
          break;
        }
      }
      const auto lifecycle = engine.lifecycle(port);
      if (!lifecycle.has_value() || *lifecycle != PortLifecycle::Retired) {
        std::printf("seed=%llu round=%d lifecycle escaped retirement\n",
                    static_cast<unsigned long long>(seed), round);
        PF_CHECK(false);
        break;
      }
    }
  }
}

PF_TEST(property_committed_configuration_always_satisfies_bound_capabilities) {
  for (const std::uint64_t seed : kSeeds) {
    Rng rng(seed);
    pf_test::Fixture fixture(portfabric::SyntheticPortClass::Spine400G, 4);
    PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
    const PortId port = fixture.port(1);
    if (!pf_test::bind_claim_configure(engine, fixture, port).accepted()) {
      PF_CHECK(false);
      continue;
    }
    for (int round = 0; round < 12; ++round) {
      const auto ownership = engine.ownership(port);
      const auto current = engine.configuration(port);
      if (!ownership.has_value() || !current.has_value()) {
        break;
      }
      ReconfigureRequest request;
      request.envelope = pf_test::envelope(port, "agent-a", "boot-a",
                                           "caps-" + std::to_string(round), engine.epoch());
      request.envelope.authority.ownership = ownership->id;
      request.envelope.authority.expected_configuration = current->generation;
      request.configuration = *current;
      static const char* kCandidates[] = {"1G",  "10G",  "25G",  "40G",
                                          "50G", "100G", "200G", "400G"};
      const std::string candidate = kCandidates[rng.next(8)];
      const auto speed = PortSpeed::parse(candidate);
      const std::uint32_t lanes = 1u << rng.next(3);
      if (speed.has_value() && (speed->bits_per_second() % lanes) == 0) {
        const auto lane_configuration = LaneConfiguration::for_total(lanes, *speed);
        if (lane_configuration.has_value()) {
          request.configuration.lanes = *lane_configuration;
          request.configuration.speed.selection = SpeedSelection::Forced;
          request.configuration.speed.rate = *speed;
        }
      }
      (void)engine.reconfigure(request);
      const auto committed = engine.configuration(port);
      const auto capability = engine.capability_binding(port);
      if (!committed.has_value() || !capability.has_value()) {
        PF_CHECK(false);
        break;
      }
      const CapabilityValidation validation =
          validate_against_capabilities(*committed, *capability);
      if (!validation.accepted) {
        std::printf("seed=%llu round=%d committed configuration violates capabilities: %s\n",
                    static_cast<unsigned long long>(seed), round, validation.reason.c_str());
        PF_CHECK(false);
        break;
      }
      const auto record = engine.record(port);
      if (!record.has_value() || record->generations.configuration != committed->generation) {
        PF_CHECK(false);
        break;
      }
    }
  }
}

PF_TEST(property_indexes_match_records) {
  for (const std::uint64_t seed : kSeeds) {
    Rng rng(seed);
    pf_test::Fixture fixture(portfabric::SyntheticPortClass::Spine400G, 12);
    PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
    std::set<std::string> live;
    for (std::uint32_t index = 1; index <= 12; ++index) {
      const auto result = pf_test::bind_claim_configure(engine, fixture, fixture.port(index));
      if (result.accepted()) {
        live.insert(fixture.port(index).to_string());
      }
    }
    for (int round = 0; round < 24; ++round) {
      const std::uint32_t index = 1 + rng.next(12);
      const PortId port = fixture.port(index);
      if (live.find(port.to_string()) == live.end()) {
        continue;
      }
      switch (rng.next(4)) {
        case 0:
          (void)pf_test::administer(engine, port, AdministrativeOp::Disable,
                                    "idx-disable-" + std::to_string(round));
          break;
        case 1:
          (void)pf_test::administer(engine, port, AdministrativeOp::Enable,
                                    "idx-enable-" + std::to_string(round));
          break;
        case 2:
          (void)pf_test::administer(engine, port, AdministrativeOp::Drain,
                                    "idx-drain-" + std::to_string(round));
          break;
        default: {
          FenceRequest retire;
          retire.envelope = pf_test::envelope(port, "agent-a", "boot-a",
                                              "idx-retire-" + std::to_string(round),
                                              engine.epoch());
          retire.envelope.authority.ownership = engine.ownership(port)->id;
          if (engine.retire(retire).accepted()) {
            live.erase(port.to_string());
          }
          break;
        }
      }
      // Index driven counts must equal a full scan of authoritative records.
      const Snapshot snapshot = engine.snapshot();
      std::size_t retired = 0;
      std::size_t configured = 0;
      std::size_t disabled = 0;
      for (const PortRecord& record : snapshot.records) {
        if (record.lifecycle == PortLifecycle::Retired) {
          ++retired;
        }
        if (record.lifecycle == PortLifecycle::Configured ||
            record.lifecycle == PortLifecycle::Active) {
          ++configured;
        }
        if (record.configuration.has_value() &&
            record.configuration->administrative == AdministrativeState::Disabled) {
          ++disabled;
        }
      }
      if (engine.ports_with_lifecycle(PortLifecycle::Retired).size() != retired ||
          engine.ports_with_administrative_state(AdministrativeState::Disabled).size() !=
              disabled) {
        std::printf("seed=%llu round=%d index divergence\n",
                    static_cast<unsigned long long>(seed), round);
        PF_CHECK(false);
        break;
      }
      (void)configured;
      if (engine.port_count() != static_cast<std::size_t>(12)) {
        PF_CHECK(false);
        break;
      }
    }
  }
}

PF_TEST(property_persistence_round_trip_is_deterministic) {
  for (const std::uint64_t seed : kSeeds) {
    Rng rng(seed);
    pf_test::Fixture fixture(portfabric::SyntheticPortClass::Server25G, 6);
    const std::string path = pf_test::scratch_path("prop-" + std::to_string(seed) + ".pfstate");
    {
      PortFabricEngine engine(pf_test::engine_config("coordinator", path), &fixture.provider,
                              &fixture.adapter);
      for (std::uint32_t index = 1; index <= 6; ++index) {
        (void)pf_test::bind_claim_configure(engine, fixture, fixture.port(index),
                                            portfabric::SyntheticPortClass::Server25G);
      }
      for (int round = 0; round < 4; ++round) {
        const std::uint32_t index = 1 + rng.next(6);
        (void)pf_test::administer(engine, fixture.port(index),
                                  rng.coin() ? AdministrativeOp::Disable
                                             : AdministrativeOp::Enable,
                                  "persist-" + std::to_string(round));
      }
      const Outcome saved = engine.save();
      if (!saved.ok()) {
        std::printf("seed=%llu save failed: %s\n", static_cast<unsigned long long>(seed),
                    saved.to_string().c_str());
        PF_CHECK(false);
      }
    }
    PersistedState first;
    PersistedState second;
    const Outcome first_load = PersistenceStore::load(path, first);
    const Outcome second_load = PersistenceStore::load(path, second);
    PF_CHECK(first_load.ok());
    PF_CHECK(second_load.ok());
    std::string first_bytes;
    std::string second_bytes;
    PF_CHECK(encode_persisted_state(first, first_bytes).ok());
    PF_CHECK(encode_persisted_state(second, second_bytes).ok());
    if (first_bytes != second_bytes) {
      std::printf("seed=%llu encoding is not deterministic\n",
                  static_cast<unsigned long long>(seed));
      PF_CHECK(false);
    }
    PF_CHECK_EQ(first.records.size(), second.records.size());
    pf_test::remove_scratch(path);
  }
}

PF_TEST(property_digest_is_stable_across_process_state) {
  // The digest of a record must not depend on unrelated runtime state: mutating
  // another port must never change this port's record digest.
  for (const std::uint64_t seed : kSeeds) {
    pf_test::Fixture fixture(portfabric::SyntheticPortClass::Spine400G, 4);
    PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
    (void)pf_test::bind_claim_configure(engine, fixture, fixture.port(1));
    const Digest before = digest_record(*engine.record(fixture.port(1)));
    for (int round = 0; round < 6; ++round) {
      const std::uint32_t index = 2 + (static_cast<std::uint32_t>(seed + round) % 3);
      (void)pf_test::bind_claim_configure(engine, fixture, fixture.port(index));
    }
    const Digest after = digest_record(*engine.record(fixture.port(1)));
    PF_CHECK(before == after);
  }
}
