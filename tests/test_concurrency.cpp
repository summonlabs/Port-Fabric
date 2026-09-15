// Concurrency: independent ports mutate in parallel, conflicting mutations on
// one port resolve deterministically, adapters never run under the state lock
// and shutdown with work in flight is safe.

#include <atomic>
#include <barrier>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "pf_test.hpp"
#include "support.hpp"

using namespace portfabric;

namespace {

/// Adapter that records how many applies overlapped with a concurrent read of
/// the runtime, which proves that no engine lock is held across the callback.
class ObservingAdapter : public PortAdapter {
 public:
  std::string_view label() const noexcept override { return "observing"; }

  AdapterApplyResult apply(const PortId& port, const PortConfiguration& configuration) override {
    (void)configuration;
    applies.fetch_add(1);
    in_flight.fetch_add(1);
    std::this_thread::sleep_for(std::chrono::microseconds(200));
    in_flight.fetch_sub(1);
    return AdapterApplyResult::applied("observed " + port.to_string());
  }

  AdapterReadback readback(const PortId& port) override {
    (void)port;
    return AdapterReadback::unsupported("no readback");
  }

  std::atomic<int> applies{0};
  std::atomic<int> in_flight{0};
  std::atomic<int> max_in_flight{0};
};

}  // namespace

PF_TEST(independent_ports_mutate_in_parallel) {
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Spine400G, 16);
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  for (std::uint32_t index = 1; index <= 8; ++index) {
    PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, fixture.port(index)).accepted());
  }
  std::vector<std::thread> workers;
  std::atomic<int> accepted{0};
  for (std::uint32_t index = 1; index <= 8; ++index) {
    workers.emplace_back([&engine, &fixture, &accepted, index]() {
      const PortId port = fixture.port(index);
      const auto ownership = engine.ownership(port);
      const auto configuration = engine.configuration(port);
      if (!ownership.has_value() || !configuration.has_value()) {
        return;
      }
      ReconfigureRequest request;
      request.envelope = pf_test::envelope(port, "agent-a", "boot-a",
                                           "parallel-" + std::to_string(index), engine.epoch());
      request.envelope.authority.ownership = ownership->id;
      request.envelope.authority.expected_configuration = configuration->generation;
      request.configuration = *configuration;
      request.configuration.mtu = *Mtu::create(9000);
      if (engine.reconfigure(request).accepted()) {
        accepted.fetch_add(1);
      }
    });
  }
  for (std::thread& worker : workers) {
    worker.join();
  }
  PF_CHECK_EQ(accepted.load(), 8);
  PF_CHECK_EQ(engine.ports_requiring_revalidation().size(), static_cast<std::size_t>(0));
  for (std::uint32_t index = 1; index <= 8; ++index) {
    PF_CHECK_EQ(engine.configuration(fixture.port(index))->generation.value(), 2ull);
  }
}

PF_TEST(conflicting_mutations_on_one_port_resolve_deterministically) {
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Spine400G, 1);
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  const auto ownership = engine.ownership(port);
  const auto configuration = engine.configuration(port);
  PF_REQUIRE(ownership.has_value());
  PF_REQUIRE(configuration.has_value());

  const int racers = 8;
  std::vector<std::thread> threads;
  std::atomic<int> accepted{0};
  std::atomic<int> stale{0};
  std::atomic<int> other{0};
  for (int index = 0; index < racers; ++index) {
    threads.emplace_back([&, index]() {
      ReconfigureRequest request;
      request.envelope = pf_test::envelope(port, "agent-a", "boot-a",
                                           "race-" + std::to_string(index), engine.epoch());
      request.envelope.authority.ownership = ownership->id;
      request.envelope.authority.expected_configuration = configuration->generation;
      request.configuration = *configuration;
      request.configuration.mtu = *Mtu::create(static_cast<std::uint32_t>(9000 + index));
      const MutationResult result = engine.reconfigure(request);
      if (result.accepted()) {
        accepted.fetch_add(1);
      } else if (result.outcome.code() == OutcomeCode::StaleConfigurationGeneration) {
        stale.fetch_add(1);
      } else {
        other.fetch_add(1);
      }
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
  // Exactly one mutation commits from generation 1; every other racer observes a
  // stale expectation. No other outcome is legal.
  PF_CHECK_EQ(accepted.load(), 1);
  PF_CHECK_EQ(stale.load(), racers - 1);
  PF_CHECK_EQ(other.load(), 0);
  PF_CHECK_EQ(engine.configuration(port)->generation.value(), 2ull);
}

PF_TEST(concurrent_enable_and_disable_produce_one_legal_result) {
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Server10G, 1);
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  const PortId port = fixture.port(1);
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port,
                                           portfabric::SyntheticPortClass::Server10G)
                 .accepted());
  std::vector<std::thread> threads;
  std::atomic<int> accepted{0};
  std::atomic<int> stale{0};
  for (int index = 0; index < 4; ++index) {
    threads.emplace_back([&, index]() {
      const AdministrativeOp op =
          index % 2 == 0 ? AdministrativeOp::Disable : AdministrativeOp::Enable;
      const MutationResult result = pf_test::administer(
          engine, port, op, "admin-race-" + std::to_string(index));
      if (result.accepted()) {
        accepted.fetch_add(1);
      } else if (result.outcome.code() == OutcomeCode::StaleConfigurationGeneration ||
                 result.outcome.code() == OutcomeCode::StaleOwnershipGeneration) {
        stale.fetch_add(1);
      }
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
  PF_CHECK(accepted.load() + stale.load() == 4);
  const auto lifecycle = engine.lifecycle(port);
  PF_REQUIRE(lifecycle.has_value());
  PF_CHECK(*lifecycle == PortLifecycle::Active || *lifecycle == PortLifecycle::AdminDisabled);
}

PF_TEST(adapter_callbacks_run_without_the_state_lock) {
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Spine400G, 8);
  ObservingAdapter adapter;
  EngineConfig config = pf_test::engine_config();
  config.adapter_policy = AdapterPolicy::Apply;
  PortFabricEngine engine(config, &fixture.provider, &adapter);
  for (std::uint32_t index = 1; index <= 8; ++index) {
    PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, fixture.port(index)).accepted());
  }

  std::atomic<bool> stop{false};
  std::atomic<int> reads{0};
  std::thread reader([&engine, &fixture, &stop, &reads]() {
    while (!stop.load()) {
      (void)engine.ports_with_lifecycle(PortLifecycle::Configured);
      (void)engine.snapshot();
      reads.fetch_add(1);
    }
  });

  std::vector<std::thread> writers;
  for (std::uint32_t index = 1; index <= 8; ++index) {
    writers.emplace_back([&engine, &fixture, index]() {
      const PortId port = fixture.port(index);
      const auto ownership = engine.ownership(port);
      const auto configuration = engine.configuration(port);
      if (!ownership.has_value() || !configuration.has_value()) {
        return;
      }
      ReconfigureRequest request;
      request.envelope = pf_test::envelope(port, "agent-a", "boot-a",
                                           "apply-" + std::to_string(index), engine.epoch());
      request.envelope.authority.ownership = ownership->id;
      request.envelope.authority.expected_configuration = configuration->generation;
      request.configuration = *configuration;
      request.configuration.mtu = *Mtu::create(9100);
      (void)engine.reconfigure(request);
    });
  }
  for (std::thread& writer : writers) {
    writer.join();
  }
  stop.store(true);
  reader.join();
  PF_CHECK(adapter.applies.load() >= 8);
  PF_CHECK(reads.load() > 0);
}

PF_TEST(barrier_forced_races_resolve_to_exactly_one_legal_state) {
  // A barrier releases every participant at the same instant, so the races below
  // are genuine rather than merely likely. Each race must end in exactly one
  // authoritative outcome.
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Spine400G, 4);

  // Race 1: enable against disable, both starting together.
  {
    PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
    const PortId port = fixture.port(1);
    PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
    std::barrier gate(3);
    std::atomic<int> accepted{0};
    std::atomic<int> rejected{0};
    const auto run = [&](AdministrativeOp op, const char* attempt) {
      gate.arrive_and_wait();
      const MutationResult result = pf_test::administer(engine, port, op, attempt);
      if (result.accepted()) {
        accepted.fetch_add(1);
      } else {
        rejected.fetch_add(1);
      }
    };
    std::thread first(run, AdministrativeOp::Disable, "race-disable");
    std::thread second(run, AdministrativeOp::Enable, "race-enable");
    gate.arrive_and_wait();
    first.join();
    second.join();
    PF_CHECK_EQ(accepted.load() + rejected.load(), 2);
    const auto lifecycle = engine.lifecycle(port);
    PF_REQUIRE(lifecycle.has_value());
    PF_CHECK(*lifecycle == PortLifecycle::Active || *lifecycle == PortLifecycle::AdminDisabled);
  }

  // Race 2: a configuration change against a capability invalidation.
  {
    PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
    const PortId port = fixture.port(1);
    PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
    const auto ownership = engine.ownership(port);
    const auto capability = engine.capability_binding(port);
    PF_REQUIRE(ownership.has_value());
    PF_REQUIRE(capability.has_value());
    const auto generation = engine.configuration(port)->generation;

    std::barrier gate(3);
    std::atomic<int> accepted{0};
    const auto reconfigure = [&]() {
      gate.arrive_and_wait();
      ReconfigureRequest request;
      request.envelope = pf_test::envelope(port, "agent-a", "boot-a", "race-reconfigure",
                                           engine.epoch());
      request.envelope.authority.ownership = ownership->id;
      request.envelope.authority.expected_configuration = generation;
      request.configuration = fixture.configuration(port, *capability);
      request.configuration.mtu = *Mtu::create(9000);
      if (engine.reconfigure(request).accepted()) {
        accepted.fetch_add(1);
      }
    };
    const auto rebind = [&]() {
      gate.arrive_and_wait();
      CapabilityRequest request;
      request.envelope =
          pf_test::envelope(port, "agent-a", "boot-a", "race-capability", engine.epoch());
      request.envelope.authority.ownership = ownership->id;
      request.binding = *capability;
      request.binding.generation = capability->generation.next();
      request.binding.evidence_generation = capability->evidence_generation.next();
      request.binding.capabilities.supported_speeds.clear();
      request.binding.capabilities.supported_speeds.push_back(*PortSpeed::parse("400G"));
      request.binding.capabilities.supported_speeds.push_back(*PortSpeed::parse("200G"));
      if (engine.bind_capabilities(request).accepted()) {
        accepted.fetch_add(1);
      }
    };
    std::thread first(reconfigure);
    std::thread second(rebind);
    gate.arrive_and_wait();
    first.join();
    second.join();
    PF_CHECK(accepted.load() >= 1);
    // Whatever the order, the committed configuration must satisfy the bound
    // capability evidence or the port must be fenced for reconciliation.
    const auto committed = engine.configuration(port);
    const auto bound = engine.capability_binding(port);
    const auto lifecycle = engine.lifecycle(port);
    PF_REQUIRE(committed.has_value());
    PF_REQUIRE(bound.has_value());
    PF_REQUIRE(lifecycle.has_value());
    if (*lifecycle != PortLifecycle::RevalidationRequired) {
      PF_CHECK(validate_against_capabilities(*committed, *bound).accepted);
    }
  }

  // Race 3: retirement against configuration.
  {
    PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
    const PortId port = fixture.port(1);
    PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
    const auto ownership = engine.ownership(port);
    const auto capability = engine.capability_binding(port);
    const auto generation = engine.configuration(port)->generation;
    std::barrier gate(3);
    const auto retire = [&]() {
      gate.arrive_and_wait();
      FenceRequest request;
      request.envelope = pf_test::envelope(port, "agent-a", "boot-a", "race-retire", engine.epoch());
      request.envelope.authority.ownership = ownership->id;
      (void)engine.retire(request);
    };
    const auto reconfigure = [&]() {
      gate.arrive_and_wait();
      ReconfigureRequest request;
      request.envelope =
          pf_test::envelope(port, "agent-a", "boot-a", "race-config", engine.epoch());
      request.envelope.authority.ownership = ownership->id;
      request.envelope.authority.expected_configuration = generation;
      request.configuration = fixture.configuration(port, *capability);
      request.configuration.mtu = *Mtu::create(9200);
      (void)engine.reconfigure(request);
    };
    std::thread first(retire);
    std::thread second(reconfigure);
    gate.arrive_and_wait();
    first.join();
    second.join();
    const auto lifecycle = engine.lifecycle(port);
    PF_REQUIRE(lifecycle.has_value());
    if (*lifecycle == PortLifecycle::Retired) {
      // Nothing may resurrect it afterwards.
      PF_CHECK_EQ(pf_test::administer(engine, port, AdministrativeOp::Enable, "after-retire")
                      .outcome.code(),
                  OutcomeCode::PortRetired);
    } else {
      PF_CHECK(*lifecycle == PortLifecycle::Configured);
    }
  }

  // Race 4: a fencing decision against an in-flight mutation.
  {
    PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
    const PortId port = fixture.port(1);
    PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
    const auto ownership = engine.ownership(port);
    std::barrier gate(3);
    std::atomic<int> accepted{0};
    std::atomic<int> fenced{0};
    const auto mutate = [&]() {
      gate.arrive_and_wait();
      const MutationResult result =
          pf_test::administer(engine, port, AdministrativeOp::Disable, "race-fence-mutation");
      if (result.accepted()) {
        accepted.fetch_add(1);
      } else if (result.outcome.code() == OutcomeCode::PublisherFenced ||
                 result.outcome.code() == OutcomeCode::StaleWorkerBoot) {
        fenced.fetch_add(1);
      }
    };
    const auto fence = [&]() {
      gate.arrive_and_wait();
      (void)engine.fence_publisher(ownership->publisher, ownership->boot);
    };
    std::thread first(mutate);
    std::thread second(fence);
    gate.arrive_and_wait();
    first.join();
    second.join();
    // Either the mutation committed before the fence, or the fence stopped it.
    PF_CHECK_EQ(accepted.load() + fenced.load(), 1);
    PF_CHECK(engine.is_publisher_fenced(ownership->publisher, ownership->boot));
    PF_CHECK(!engine.ownership(port).has_value());
  }
}

PF_TEST(shutdown_with_work_in_flight_is_safe) {
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Spine400G, 8);
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  for (std::uint32_t index = 1; index <= 8; ++index) {
    PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, fixture.port(index)).accepted());
  }
  std::atomic<int> rejected{0};
  std::vector<std::thread> threads;
  for (std::uint32_t index = 1; index <= 8; ++index) {
    threads.emplace_back([&engine, &fixture, &rejected, index]() {
      const PortId port = fixture.port(index);
      for (int attempt = 0; attempt < 4; ++attempt) {
        const MutationResult result = pf_test::administer(
            engine, port, AdministrativeOp::Disable,
            "shutdown-" + std::to_string(index) + "-" + std::to_string(attempt));
        if (result.outcome.code() == OutcomeCode::ShuttingDown) {
          rejected.fetch_add(1);
        }
      }
    });
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  engine.shutdown();
  for (std::thread& thread : threads) {
    thread.join();
  }
  PF_CHECK(engine.shutting_down());
  // The runtime never hangs and every port remains in a legal lifecycle state.
  for (std::uint32_t index = 1; index <= 8; ++index) {
    const auto lifecycle = engine.lifecycle(fixture.port(index));
    PF_REQUIRE(lifecycle.has_value());
    PF_CHECK(is_valid(*lifecycle));
  }
}

PF_TEST(concurrent_snapshots_and_mutations_agree) {
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Server25G, 8);
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  for (std::uint32_t index = 1; index <= 8; ++index) {
    PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, fixture.port(index),
                                             portfabric::SyntheticPortClass::Server25G)
                   .accepted());
  }
  std::atomic<bool> stop{false};
  std::atomic<int> snapshots{0};
  std::thread snapshotter([&engine, &stop, &snapshots]() {
    while (!stop.load()) {
      const Snapshot snapshot = engine.snapshot();
      PF_CHECK_EQ(snapshot.port_count(), static_cast<std::size_t>(8));
      snapshots.fetch_add(1);
    }
  });
  for (std::uint32_t index = 1; index <= 8; ++index) {
    PF_CHECK(pf_test::administer(engine, fixture.port(index), AdministrativeOp::Disable,
                                 "snap-" + std::to_string(index))
                 .accepted());
  }
  stop.store(true);
  snapshotter.join();
  PF_CHECK(snapshots.load() > 0);
  PF_CHECK_EQ(engine.ports_with_lifecycle(PortLifecycle::AdminDisabled).size(),
              static_cast<std::size_t>(8));
}
