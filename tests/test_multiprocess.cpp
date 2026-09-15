// Real process death proofs.
//
// This suite runs the actual coordinator and port agent executables as separate
// operating system processes, kills them with uncatchable termination and proves
// that the runtime fences the dead incarnation, rejects its replay, requires a
// fresh incarnation, preserves durable desired configuration and keeps unrelated
// ports unaffected.
//
// On platforms without child process control the proof is reported as
// UNSUPPORTED rather than being emulated in memory.

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "pf_test.hpp"
#include "portfabric/client.hpp"
#include "portfabric/platform.hpp"
#include "process.hpp"
#include "support.hpp"

#ifndef PF_COORDINATOR_EXE
#define PF_COORDINATOR_EXE "pf-coordinator"
#endif
#ifndef PF_WORKER_EXE
#define PF_WORKER_EXE "pf-worker"
#endif

using namespace portfabric;

namespace {

std::string quote(const std::string& value) { return "\"" + value + "\""; }

struct Coordinator {
  pf_process::ChildProcess process;
  std::uint16_t port = 0;
  std::string state_path;
  std::string stop_path;
};

bool start_coordinator(Coordinator& coordinator, const std::string& state_path,
                       const std::string& label) {
  coordinator.state_path = state_path;
  coordinator.stop_path = "pf-scratch-stop-" + label + ".flag";
  (void)remove_file(coordinator.stop_path);
  const std::string command = quote(PF_COORDINATOR_EXE) + " --port 0 --state " +
                              quote(coordinator.state_path) + " --stop-file " +
                              quote(coordinator.stop_path) + " --coordinator coordinator";
  std::string error;
  coordinator.process = pf_process::spawn(command, error);
  if (!coordinator.process.valid()) {
    std::printf("  coordinator spawn failed: %s\n", error.c_str());
    return false;
  }
  std::string line;
  if (!pf_process::wait_for_line(coordinator.process, "port=", line, error)) {
    std::printf("  coordinator did not report its port: %s\n", error.c_str());
    return false;
  }
  coordinator.port = static_cast<std::uint16_t>(std::stoul(line.substr(5)));
  if (!pf_process::wait_for_line(coordinator.process, "ready=true", line, error)) {
    std::printf("  coordinator did not become ready: %s\n", error.c_str());
    return false;
  }
  return true;
}

bool stop_coordinator(Coordinator& coordinator) {
  (void)write_file_flushed(coordinator.stop_path, "stop");
  unsigned long exit_code = 0;
  std::string error;
  if (!pf_process::wait(coordinator.process, exit_code, error)) {
    std::printf("  coordinator wait failed: %s\n", error.c_str());
    return false;
  }
  (void)remove_file(coordinator.stop_path);
  return exit_code == 0;
}

/// Connects and registers a control client with the coordinator.
bool connect_client(const Coordinator& coordinator, const std::string& publisher,
                    const std::string& boot, std::unique_ptr<FabricClient>& client) {
  ClientConfig config;
  config.port = coordinator.port;
  config.publisher = PublisherId::from_validated(publisher);
  config.boot = WorkerBootId::from_validated(boot);
  config.role = "proof";
  client = std::make_unique<FabricClient>(config);
  const Outcome connected = client->connect();
  if (!connected.accepted()) {
    std::printf("  client connect failed: %s\n", connected.to_string().c_str());
    return false;
  }
  const Outcome registered = client->register_publisher();
  if (!registered.accepted()) {
    std::printf("  publisher registration failed: %s\n", registered.to_string().c_str());
    return false;
  }
  return true;
}

PortMutationMessage base_message(std::uint8_t kind, const PortId& port,
                                 const std::string& publisher, const std::string& boot,
                                 CoordinatorEpoch epoch, const std::string& attempt) {
  PortMutationMessage message;
  message.kind = kind;
  message.port = port;
  message.authority.epoch = epoch;
  message.authority.publisher = PublisherId::from_validated(publisher);
  message.authority.boot = WorkerBootId::from_validated(boot);
  message.authority.attempt = MutationAttemptId::from_validated(attempt);
  message.provenance_kind = static_cast<std::uint8_t>(ProvenanceKind::SwitchAgent);
  message.provenance_source = publisher;
  return message;
}

std::optional<PortRecord> fetch_record(FabricClient& client, const PortId& port) {
  QueryMessage query;
  query.kind = QueryKind::Port;
  query.port = port;
  const QueryResponse response = client.query(query);
  if (!response.outcome.accepted() || response.records.empty()) {
    return std::nullopt;
  }
  return response.records.front();
}

/// Spawns the port agent as a real process and waits until it reports readiness,
/// capturing the ownership identity it obtained.
bool start_agent(pf_process::ChildProcess& process, const PortId& port,
                 const std::string& publisher, const std::string& boot,
                 std::uint16_t coordinator_port, const std::string& stop_path,
                 std::string& ownership_id, bool configure = true) {
  const std::string command =
      quote(PF_WORKER_EXE) + " --address 127.0.0.1 --port-number " +
      std::to_string(coordinator_port) + " --publisher " + publisher + " --boot " + boot +
      " --owner owner-" + publisher + " --owner-kind SWITCH_AGENT --fabric-port " +
      port.to_string() + " --device syn-coordinator-device --stop-file " + quote(stop_path) +
      (configure ? std::string() : std::string(" --no-configure"));
  std::string error;
  process = pf_process::spawn(command, error);
  if (!process.valid()) {
    std::printf("  agent spawn failed: %s\n", error.c_str());
    return false;
  }
  std::string line;
  bool ready = false;
  std::string transcript;
  while (pf_process::read_line(process, line, error)) {
    transcript.append("    agent: ").append(line).append("\n");
    if (line.rfind("ownership=", 0) == 0) {
      ownership_id = line.substr(std::string("ownership=").size());
    }
    if (line == "ready=true") {
      ready = true;
      break;
    }
  }
  if (!ready) {
    std::printf("  agent did not become ready: %s\n%s", error.c_str(), transcript.c_str());
    return false;
  }
  return true;
}

/// Kills an agent with uncatchable termination and reaps the process.
void kill_agent(pf_process::ChildProcess& process) {
  pf_process::terminate(process);
  unsigned long exit_code = 0;
  std::string error;
  (void)pf_process::wait(process, exit_code, error);
}

bool stop_agent(pf_process::ChildProcess& process, const std::string& stop_path) {
  (void)write_file_flushed(stop_path, "stop");
  unsigned long exit_code = 0;
  std::string error;
  if (!pf_process::wait(process, exit_code, error)) {
    return false;
  }
  (void)remove_file(stop_path);
  return exit_code == 0;
}

/// Binds a canonical port identity over the control protocol.
bool bind_port_over_wire(FabricClient& client, const PortId& port, const std::string& attempt) {
  PortMutationMessage bind = base_message(0, port, "operator", "operator-boot", client.epoch(),
                                          attempt);
  bind.parent_device = DeviceId::from_validated("syn-coordinator-device");
  bind.device_generation = DeviceGeneration::from_value(1);
  return client.mutate(bind).accepted();
}

}  // namespace

PF_TEST(real_worker_death_fences_the_incarnation) {
  if (!pf_process::available()) {
    std::printf("  real process proofs are UNSUPPORTED on this platform\n");
    return;
  }
  const std::string state_path = pf_test::scratch_path("worker-death.pfstate");
  Coordinator coordinator;
  PF_REQUIRE(start_coordinator(coordinator, state_path, "worker-death"));

  std::unique_ptr<FabricClient> operator_client;
  PF_REQUIRE(connect_client(coordinator, "operator", "operator-boot", operator_client));
  const PortId port = PortId::from_validated("syn-coordinator-device-p01");
  PF_REQUIRE(bind_port_over_wire(*operator_client, port, "bind-1"));

  const std::string agent_stop = "pf-scratch-stop-agent-a.flag";
  (void)remove_file(agent_stop);
  pf_process::ChildProcess agent_a;
  std::string ownership_a;
  PF_REQUIRE(start_agent(agent_a, port, "agent-a", "boot-a", coordinator.port, agent_stop,
                         ownership_a));
  std::printf("  agent-a ownership=%s\n", ownership_a.c_str());

  const auto after_configure = fetch_record(*operator_client, port);
  PF_REQUIRE(after_configure.has_value());
  PF_CHECK_EQ(after_configure->lifecycle, PortLifecycle::Configured);
  PF_CHECK(after_configure->configuration.has_value());
  PF_CHECK_EQ(after_configure->generations.configuration.value(), 1ull);
  PF_CHECK(after_configure->ownership.has_value());
  PF_CHECK_EQ(after_configure->ownership->boot.to_string(), std::string("boot-a"));
  PF_CHECK_EQ(after_configure->applied.outcome, AppliedOutcome::Verified);

  // Real process death: uncatchable termination, no shutdown path runs.
  kill_agent(agent_a);
  std::printf("  agent-a killed with uncatchable termination\n");

  // Loss is detected and fenced through the real control path.
  const Outcome fenced = operator_client->fence_publisher(
      PublisherId::from_validated("agent-a"), WorkerBootId::from_validated("boot-a"),
      "process death detected");
  PF_REQUIRE(fenced.accepted());

  const auto after_fence = fetch_record(*operator_client, port);
  PF_REQUIRE(after_fence.has_value());
  PF_CHECK(!after_fence->ownership.has_value());
  PF_CHECK_EQ(after_fence->status_reason, std::string("OWNERSHIP_FENCED"));

  // Replay from the dead boot rejects through the real control plane.
  const std::string replay_command =
      quote(PF_WORKER_EXE) + " --address 127.0.0.1 --port-number " +
      std::to_string(coordinator.port) + " --publisher agent-a --boot boot-a --mode replay" +
      " --fabric-port " + port.to_string() + " --generation 1";
  std::string error;
  pf_process::ChildProcess replay = pf_process::spawn(replay_command, error);
  PF_REQUIRE(replay.valid());
  std::string line;
  PF_REQUIRE(pf_process::wait_for_line(replay, "result=", line, error));
  std::printf("  old-boot replay: %s\n", line.c_str());
  PF_CHECK(line == "result=STALE_WORKER_BOOT" || line == "result=PUBLISHER_FENCED");
  unsigned long replay_code = 0;
  PF_REQUIRE(pf_process::wait(replay, replay_code, error));
  PF_CHECK(replay_code != 0);

  // A fresh incarnation of the same agent presents a new boot identity.
  std::unique_ptr<FabricClient> fresh_client;
  PF_REQUIRE(connect_client(coordinator, "agent-a", "boot-b", fresh_client));
  PortMutationMessage claim = base_message(9, port, "agent-a", "boot-b", fresh_client->epoch(),
                                           "claim-boot-b");
  claim.owner_kind = static_cast<std::uint8_t>(PortOwnerKind::SwitchAgent);
  claim.owner = OwnerId::from_validated("owner-agent-a");
  claim.exclusive = true;
  PF_REQUIRE(fresh_client->mutate(claim).accepted());
  const auto after_claim = fetch_record(*fresh_client, port);
  PF_REQUIRE(after_claim.has_value());
  PF_REQUIRE(after_claim->ownership.has_value());
  PF_CHECK_EQ(after_claim->ownership->boot.to_string(), std::string("boot-b"));
  // Durable desired configuration was reconciled, not duplicated.
  PF_CHECK_EQ(after_claim->generations.configuration.value(), 1ull);
  PF_CHECK(after_claim->configuration.has_value());

  // A fresh valid mutation commits the next generation without touching
  // unrelated ports.
  PortMutationMessage reconfigure =
      base_message(2, port, "agent-a", "boot-b", fresh_client->epoch(), "reconfigure-boot-b");
  reconfigure.authority.ownership = after_claim->ownership->id;
  reconfigure.authority.expected_configuration = after_claim->generations.configuration;
  reconfigure.parent_device = DeviceId::from_validated("syn-coordinator-device");
  reconfigure.device_generation = DeviceGeneration::from_value(1);
  reconfigure.has_configuration = true;
  reconfigure.configuration = *after_claim->configuration;
  reconfigure.configuration.mtu = *Mtu::create(8192);
  PF_REQUIRE(fresh_client->mutate(reconfigure).accepted());
  const auto after_reconfigure = fetch_record(*fresh_client, port);
  PF_REQUIRE(after_reconfigure.has_value());
  PF_CHECK_EQ(after_reconfigure->generations.configuration.value(), 2ull);
  PF_CHECK_EQ(after_reconfigure->configuration->mtu.value(), 8192u);

  // An unrelated agent and port are unaffected by the fencing.
  const PortId other_port = PortId::from_validated("syn-coordinator-device-p02");
  PF_REQUIRE(bind_port_over_wire(*operator_client, other_port, "bind-2"));
  pf_process::ChildProcess agent_c;
  std::string ownership_c;
  const std::string agent_c_stop = "pf-scratch-stop-agent-c.flag";
  (void)remove_file(agent_c_stop);
  PF_REQUIRE(start_agent(agent_c, other_port, "agent-b", "boot-b2", coordinator.port,
                         agent_c_stop, ownership_c));
  const auto other_record = fetch_record(*operator_client, other_port);
  PF_REQUIRE(other_record.has_value());
  PF_CHECK_EQ(other_record->lifecycle, PortLifecycle::Configured);
  PF_REQUIRE(other_record->ownership.has_value());
  PF_CHECK_EQ(other_record->ownership->publisher.to_string(), std::string("agent-b"));
  PF_CHECK(stop_agent(agent_c, agent_c_stop));

  fresh_client->close();
  operator_client->close();
  PF_CHECK(stop_coordinator(coordinator));
  (void)remove_file(state_path);
}

PF_TEST(real_coordinator_restart_preserves_configuration_only) {
  if (!pf_process::available()) {
    std::printf("  real process proofs are UNSUPPORTED on this platform\n");
    return;
  }
  const std::string state_path = pf_test::scratch_path("coordinator-restart.pfstate");
  Coordinator coordinator;
  PF_REQUIRE(start_coordinator(coordinator, state_path, "coordinator-restart"));

  const PortId port = PortId::from_validated("syn-coordinator-device-p01");
  std::unique_ptr<FabricClient> operator_client;
  PF_REQUIRE(connect_client(coordinator, "operator", "operator-boot", operator_client));
  const CoordinatorEpoch first_epoch = operator_client->epoch();
  PF_CHECK(first_epoch.valid());
  PF_REQUIRE(bind_port_over_wire(*operator_client, port, "bind-1"));

  const std::string agent_stop = "pf-scratch-stop-restart-agent.flag";
  (void)remove_file(agent_stop);
  pf_process::ChildProcess agent;
  std::string ownership;
  PF_REQUIRE(start_agent(agent, port, "agent-a", "boot-a", coordinator.port, agent_stop,
                         ownership));
  const auto before = fetch_record(*operator_client, port);
  PF_REQUIRE(before.has_value());
  const std::string before_configuration = before->configuration->to_string();
  const auto before_generation = before->generations.configuration;
  PF_REQUIRE(stop_agent(agent, agent_stop));

  // Kill the coordinator outright: no graceful shutdown and no final save.
  pf_process::terminate(coordinator.process);
  unsigned long coordinator_code = 0;
  std::string error;
  PF_REQUIRE(pf_process::wait(coordinator.process, coordinator_code, error));
  std::printf("  coordinator killed with exit code %lu\n", coordinator_code);
  PF_CHECK(coordinator_code != 0);
  operator_client->close();

  Coordinator restarted;
  PF_REQUIRE(start_coordinator(restarted, state_path, "coordinator-restart-2"));

  std::unique_ptr<FabricClient> fresh;
  PF_REQUIRE(connect_client(restarted, "operator-2", "operator-boot-2", fresh));
  PF_CHECK(fresh->epoch() > first_epoch);
  std::printf("  epoch after restart=%s\n", fresh->epoch().to_string().c_str());

  // Old-epoch traffic is rejected.
  PortMutationMessage stale = base_message(3, port, "operator-2", "operator-boot-2",
                                           first_epoch, "stale-epoch-1");
  stale.administrative_op = static_cast<std::uint8_t>(AdministrativeOp::Disable);
  stale.authority.expected_configuration = before_generation;
  PF_CHECK_EQ(fresh->mutate(stale).outcome.code(), OutcomeCode::StaleCoordinatorEpoch);

  // Stale worker incarnations from the previous process life are rejected.
  PortMutationMessage stale_boot = base_message(3, port, "agent-a", "boot-a", fresh->epoch(),
                                                "stale-boot-1");
  stale_boot.administrative_op = static_cast<std::uint8_t>(AdministrativeOp::Disable);
  stale_boot.authority.expected_configuration = before_generation;
  const OutcomeCode boot_code = fresh->mutate(stale_boot).outcome.code();
  PF_CHECK(boot_code == OutcomeCode::StaleWorkerBoot || boot_code == OutcomeCode::PublisherFenced ||
           boot_code == OutcomeCode::UnknownPublisher);

  // Durable desired configuration survived. Live authority and applied freshness
  // did not: the port requires revalidation.
  const auto recovered = fetch_record(*fresh, port);
  PF_REQUIRE(recovered.has_value());
  PF_REQUIRE(recovered->configuration.has_value());
  PF_CHECK_EQ(recovered->configuration->to_string(), before_configuration);
  PF_CHECK_EQ(recovered->generations.configuration, before_generation);
  PF_CHECK_EQ(recovered->lifecycle, PortLifecycle::RevalidationRequired);
  PF_CHECK(!recovered->ownership.has_value());
  PF_CHECK_EQ(recovered->applied.outcome, AppliedOutcome::OutcomeUnknown);
  PF_CHECK(!recovered->applied.verified);
  PF_CHECK_EQ(recovered->status_reason, std::string("RECOVERED_FROM_PERSISTENCE"));

  // A fresh incarnation re-establishes authority and reconciles the port.
  std::unique_ptr<FabricClient> agent_client;
  PF_REQUIRE(connect_client(restarted, "agent-a", "boot-c", agent_client));
  PortMutationMessage claim = base_message(9, port, "agent-a", "boot-c", agent_client->epoch(),
                                           "claim-boot-c");
  claim.owner_kind = static_cast<std::uint8_t>(PortOwnerKind::SwitchAgent);
  claim.owner = OwnerId::from_validated("owner-agent-a");
  PF_REQUIRE(agent_client->mutate(claim).accepted());
  const auto claimed = fetch_record(*agent_client, port);
  PF_REQUIRE(claimed.has_value());
  PF_REQUIRE(claimed->ownership.has_value());

  PortMutationMessage reconcile = base_message(6, port, "agent-a", "boot-c",
                                               agent_client->epoch(), "reconcile-1");
  reconcile.authority.ownership = claimed->ownership->id;
  reconcile.authority.expected_configuration = claimed->generations.configuration;
  reconcile.detail = "restart reconciliation";
  PF_REQUIRE(agent_client->mutate(reconcile).accepted());
  const auto after_reconcile = fetch_record(*agent_client, port);
  PF_REQUIRE(after_reconcile.has_value());
  PF_CHECK_EQ(after_reconcile->lifecycle, PortLifecycle::Configured);
  PF_CHECK(after_reconcile->generations.configuration > before_generation);

  // A new generation commits successfully after the restart.
  PortMutationMessage reconfigure = base_message(2, port, "agent-a", "boot-c",
                                                 agent_client->epoch(), "reconfigure-restart");
  reconfigure.authority.ownership = after_reconcile->ownership->id;
  reconfigure.authority.expected_configuration = after_reconcile->generations.configuration;
  reconfigure.parent_device = DeviceId::from_validated("syn-coordinator-device");
  reconfigure.device_generation = DeviceGeneration::from_value(1);
  reconfigure.has_configuration = true;
  reconfigure.configuration = *after_reconcile->configuration;
  reconfigure.configuration.mtu = *Mtu::create(8000);
  PF_REQUIRE(agent_client->mutate(reconfigure).accepted());
  const auto final_record = fetch_record(*agent_client, port);
  PF_REQUIRE(final_record.has_value());
  PF_CHECK_EQ(final_record->configuration->mtu.value(), 8000u);

  agent_client->close();
  fresh->close();
  PF_CHECK(stop_coordinator(restarted));
  (void)remove_file(state_path);
}

PF_TEST(repeated_coordinator_start_stop_leaves_no_orphans) {
  if (!pf_process::available()) {
    std::printf("  real process proofs are UNSUPPORTED on this platform\n");
    return;
  }
  const std::string state_path = pf_test::scratch_path("repeat.pfstate");
  for (int round = 0; round < 3; ++round) {
    Coordinator coordinator;
    PF_REQUIRE(start_coordinator(coordinator, state_path, "repeat-" + std::to_string(round)));
    std::unique_ptr<FabricClient> client;
    // Every round is a fresh process life, so it presents a fresh boot identity.
    PF_REQUIRE(connect_client(coordinator, "operator",
                              "operator-boot-" + std::to_string(round), client));
    QueryMessage query;
    query.kind = QueryKind::EngineInfo;
    PF_CHECK(client->query(query).outcome.accepted());
    client->close();
    PF_CHECK(stop_coordinator(coordinator));
  }
  (void)remove_file(state_path);
}
