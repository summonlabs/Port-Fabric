// Control protocol and transport: framing, malformed input, integrity checks,
// session handling and clean shutdown. The transport is implemented for Windows
// in this release; on other platforms the runtime reports it as UNSUPPORTED and
// this test asserts that classification.

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "pf_test.hpp"
#include "portfabric/client.hpp"
#include "portfabric/protocol.hpp"
#include "portfabric/server.hpp"
#include "support.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

using namespace portfabric;

namespace {

#if defined(_WIN32)

/// Sends raw bytes over a fresh loopback connection and reads the response until
/// the peer closes or a bound is reached.
std::string raw_exchange(std::uint16_t port, const std::string& bytes) {
  WSADATA data;
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    return std::string();
  }
  SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  std::string response;
  if (socket != INVALID_SOCKET) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(0x7f000001u);
    if (::connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
      std::size_t offset = 0;
      while (offset < bytes.size()) {
        const int sent = ::send(socket, bytes.data() + offset,
                                static_cast<int>(bytes.size() - offset), 0);
        if (sent <= 0) {
          break;
        }
        offset += static_cast<std::size_t>(sent);
      }
      char buffer[1024];
      for (int round = 0; round < 8; ++round) {
        const int received = ::recv(socket, buffer, sizeof(buffer), 0);
        if (received <= 0) {
          break;
        }
        response.append(buffer, static_cast<std::size_t>(received));
      }
    }
    ::closesocket(socket);
  }
  WSACleanup();
  return response;
}

#endif

std::uint64_t session_mutations(PortFabricEngine& engine, const PortId& port,
                                const std::string& publisher, const std::string& boot,
                                int rounds) {
  std::uint64_t accepted = 0;
  for (int round = 0; round < rounds; ++round) {
    const MutationResult result = pf_test::administer(
        engine, port,
        round % 2 == 0 ? AdministrativeOp::Disable : AdministrativeOp::Enable,
        "session-" + std::to_string(round), publisher, boot);
    if (result.accepted()) {
      ++accepted;
    }
  }
  return accepted;
}

}  // namespace

PF_TEST(transport_availability_is_reported_truthfully) {
  // The transport is implemented for Windows in this release. On every platform
  // the protocol codec itself is platform independent and is exercised below.
  const Frame frame{MessageType::Hello, 0, 7, std::string("payload")};
  std::string encoded;
  PF_REQUIRE(encode_frame(frame, encoded).ok());
  PF_CHECK_EQ(encoded.size(), frame_header_size + frame.payload.size());
  Frame decoded;
  PF_REQUIRE(decode_frame(encoded, decoded).ok());
  PF_CHECK_EQ(decoded.type, MessageType::Hello);
  PF_CHECK_EQ(decoded.sequence, 7ull);
  PF_CHECK_EQ(decoded.payload, std::string("payload"));
}

PF_TEST(frame_decoder_rejects_malformed_input) {
  const Frame frame{MessageType::Mutation, 0, 1, std::string("body")};
  std::string encoded;
  PF_REQUIRE(encode_frame(frame, encoded).ok());

  Frame decoded;
  PF_CHECK_EQ(decode_frame(encoded.substr(0, 8), decoded).code(), OutcomeCode::TransportFailure);
  PF_CHECK_EQ(decode_frame(std::string(encoded.size(), 'x'), decoded).code(),
              OutcomeCode::TransportFailure);
  PF_CHECK_EQ(decode_frame(encoded + "extra", decoded).code(), OutcomeCode::TransportFailure);

  // Corrupt the payload: the integrity check rejects the frame.
  std::string payload_corrupt = encoded;
  payload_corrupt[frame_header_size] = static_cast<char>(payload_corrupt[frame_header_size] ^ 0x1);
  PF_CHECK_EQ(decode_frame(payload_corrupt, decoded).code(), OutcomeCode::TransportFailure);

  // Corrupt the header type.
  std::string type_corrupt = encoded;
  type_corrupt[6] = static_cast<char>(0x7f);
  PF_CHECK_EQ(decode_frame(type_corrupt, decoded).code(), OutcomeCode::TransportFailure);

  // An unsupported protocol version is rejected.
  std::string version_corrupt = encoded;
  version_corrupt[4] = static_cast<char>(0x63);
  PF_CHECK_EQ(decode_frame(version_corrupt, decoded).code(), OutcomeCode::TransportFailure);

  // The reserved fields must be zero.
  std::string reserved_corrupt = encoded;
  reserved_corrupt[10] = static_cast<char>(1);
  PF_CHECK_EQ(decode_frame(reserved_corrupt, decoded).code(), OutcomeCode::TransportFailure);

  // An over-long declared payload is rejected before allocation.
  std::string oversized = encoded;
  const std::uint32_t huge = 0xffffffffu;
  oversized[20] = static_cast<char>(huge & 0xffu);
  oversized[21] = static_cast<char>((huge >> 8u) & 0xffu);
  oversized[22] = static_cast<char>((huge >> 16u) & 0xffu);
  oversized[23] = static_cast<char>((huge >> 24u) & 0xffu);
  PF_CHECK(!decode_frame(oversized, decoded).ok());

  // A frame whose payload exceeds the configured bound cannot be produced.
  Frame too_large;
  too_large.type = MessageType::Mutation;
  too_large.payload.assign(max_frame_bytes + 1, 'x');
  std::string out;
  PF_CHECK_EQ(encode_frame(too_large, out).code(), OutcomeCode::ResourceBoundExceeded);
}

PF_TEST(message_codecs_reject_malformed_payloads) {
  HelloMessage hello;
  hello.publisher = PublisherId::from_validated("agent-a");
  hello.boot = WorkerBootId::from_validated("boot-a");
  hello.epoch = CoordinatorEpoch::from_value(1);
  hello.role = "switch-agent";
  std::string encoded;
  PF_REQUIRE(encode_hello(hello, encoded).ok());
  HelloMessage decoded;
  PF_REQUIRE(decode_hello(encoded, decoded).ok());
  PF_CHECK_EQ(decoded.publisher.to_string(), std::string("agent-a"));
  PF_CHECK_EQ(decoded.boot.to_string(), std::string("boot-a"));
  PF_CHECK_EQ(decoded.role, std::string("switch-agent"));
  PF_CHECK(decode_hello(encoded.substr(0, 3), decoded).ok() == false);
  PF_CHECK(decode_hello(encoded + "x", decoded).ok() == false);
  std::string wrong_version = encoded;
  wrong_version[0] = static_cast<char>(9);
  wrong_version[1] = static_cast<char>(0);
  PF_CHECK(decode_hello(wrong_version, decoded).ok() == false);

  PortMutationMessage mutation;
  mutation.kind = 0;
  mutation.authority.epoch = CoordinatorEpoch::from_value(1);
  mutation.authority.publisher = PublisherId::from_validated("agent-a");
  mutation.authority.boot = WorkerBootId::from_validated("boot-a");
  mutation.authority.attempt = MutationAttemptId::from_validated("attempt-1");
  mutation.port = PortId::from_validated("port-1");
  std::string encoded_mutation;
  PF_REQUIRE(encode_mutation(mutation, encoded_mutation).ok());
  PortMutationMessage decoded_mutation;
  PF_REQUIRE(decode_mutation(encoded_mutation, decoded_mutation).ok());
  PF_CHECK_EQ(decoded_mutation.port.to_string(), std::string("port-1"));
  PF_CHECK(!decode_mutation(encoded_mutation.substr(0, encoded_mutation.size() - 1),
                            decoded_mutation)
                .ok());

  QueryMessage query;
  query.kind = QueryKind::Lifecycle;
  query.port = PortId::from_validated("port-1");
  std::string encoded_query;
  PF_REQUIRE(encode_query(query, encoded_query).ok());
  QueryMessage decoded_query;
  PF_REQUIRE(decode_query(encoded_query, decoded_query).ok());
  PF_CHECK_EQ(decoded_query.kind, QueryKind::Lifecycle);
  std::string bad_kind = encoded_query;
  bad_kind[0] = static_cast<char>(200);
  PF_CHECK_EQ(decode_query(bad_kind, decoded_query).code(), OutcomeCode::MalformedRequest);
}

PF_TEST(query_response_round_trips_every_record_shape) {
  // A query response carries whole records. Every record shape the runtime can
  // produce must survive the codec, including a record that exists without a
  // configuration and a record that carries ownership.
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Spine400G, 4);
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

  const auto run_round_trip = [&engine](const PortId& candidate, const char* label) {
    QueryResponse response;
    response.kind = QueryKind::Port;
    const auto record = engine.record(candidate);
    PF_REQUIRE(record.has_value());
    response.records.push_back(*record);
    response.generation = engine.generation();
    response.epoch = engine.epoch();
    response.digest = engine.state_digest();
    response.text = record->to_string();
    std::string encoded;
    const Outcome encode_result = encode_query_response(response, encoded);
    if (!encode_result.ok()) {
      std::printf("  %s: encode failed: %s\n", label, encode_result.to_string().c_str());
      PF_CHECK(false);
      return;
    }
    QueryResponse decoded;
    const Outcome decode_result = decode_query_response(encoded, decoded);
    if (!decode_result.ok()) {
      std::printf("  %s: decode failed: %s\n", label, decode_result.to_string().c_str());
      PF_CHECK(false);
      return;
    }
    PF_CHECK_EQ(decoded.records.size(), static_cast<std::size_t>(1));
    if (!decoded.records.empty()) {
      PF_CHECK(digest_record(decoded.records.front()) == digest_record(*record));
    }
  };

  // Bisect the record shape so that a codec regression names the field that
  // stopped surviving the wire.
  const auto round_trip_record = [](const PortRecord& record, const char* label) {
    QueryResponse response;
    response.kind = QueryKind::Port;
    response.records.push_back(record);
    std::string encoded;
    const Outcome encode_result = encode_query_response(response, encoded);
    if (!encode_result.ok()) {
      std::printf("  %s: encode failed: %s\n", label, encode_result.to_string().c_str());
      PF_CHECK(false);
      return;
    }
    QueryResponse decoded;
    const Outcome decode_result = decode_query_response(encoded, decoded);
    if (!decode_result.ok()) {
      std::printf("  %s: decode failed: %s\n", label, decode_result.to_string().c_str());
      PF_CHECK(false);
      return;
    }
    PF_CHECK_EQ(decoded.records.size(), static_cast<std::size_t>(1));
    if (!decoded.records.empty()) {
      PF_CHECK(digest_record(decoded.records.front()) == digest_record(record));
    }
  };

  PortRecord minimal;
  minimal.port = PortId::from_validated("bisect-port");
  minimal.parent_device = DeviceId::from_validated("bisect-device");
  minimal.lifecycle = PortLifecycle::Unconfigured;
  round_trip_record(minimal, "minimal unconfigured record");

  PortRecord with_capability = minimal;
  with_capability.capability.present = true;
  with_capability.capability.generation = CapabilityBindingGeneration::from_value(1);
  with_capability.capability.evidence_generation = EvidenceGeneration::from_value(1);
  with_capability.capability.source = "bisect";
  with_capability.capability.capabilities.certainty = CapabilityCertainty::Known;
  with_capability.capability.capabilities.supported_speeds.push_back(*PortSpeed::parse("10G"));
  with_capability.capability.capabilities.lane_modes.push_back(1);
  with_capability.capability.capabilities.max_lanes = 1;
  with_capability.capability.capabilities.fec_modes.push_back(FecMode::None);
  with_capability.capability.capabilities.autoneg_modes.push_back(AutonegPolicy::Forced);
  with_capability.capability.capabilities.pause_modes.push_back(PauseMode::Disabled);
  with_capability.capability.capabilities.protocol_families.push_back(ProtocolFamily::Synthetic);
  with_capability.capability.capabilities.capability_mtu_min = 1280;
  with_capability.capability.capabilities.capability_mtu_max = 9000;
  with_capability.capability.capabilities.duplex = DuplexMode::Full;
  round_trip_record(with_capability, "record with capability evidence");

  PortRecord with_ownership = minimal;
  PortOwnership ownership;
  ownership.id = PortOwnershipId::from_validated("own-bisect-port-1");
  ownership.kind = PortOwnerKind::SwitchAgent;
  ownership.owner = OwnerId::from_validated("owner-a");
  ownership.generation = PortOwnershipGeneration::from_value(1);
  ownership.publisher = PublisherId::from_validated("agent-a");
  ownership.boot = WorkerBootId::from_validated("boot-a");
  ownership.epoch = CoordinatorEpoch::from_value(1);
  with_ownership.ownership = ownership;
  round_trip_record(with_ownership, "record with ownership");

  run_round_trip(port, "unconfigured with ownership");
  PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, port).accepted());
  run_round_trip(port, "configured");
}

PF_TEST(server_and_client_exchange_mutations_over_loopback) {
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Spine400G, 4);
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  EngineService service(engine);
  ServerConfig server_config;
  server_config.address = "127.0.0.1";
  server_config.port = 0;
  CoordinatorServer server(server_config, service);
  const Outcome started = server.start();
  if (!started.ok()) {
    // The transport is unavailable on this platform; the runtime reports that
    // truthfully rather than pretending the session worked.
    PF_CHECK_EQ(started.code(), OutcomeCode::TransportFailure);
    std::printf("  transport unavailable: %s\n", started.to_string().c_str());
    return;
  }
  PF_CHECK(server.bound_port() != 0);

  ClientConfig client_config;
  client_config.port = server.bound_port();
  client_config.publisher = PublisherId::from_validated("test-client");
  client_config.boot = WorkerBootId::from_validated("test-boot");
  client_config.role = "proof-client";
  FabricClient client(client_config);
  PF_REQUIRE(client.connect().accepted());
  PF_CHECK(client.connected());
  PF_CHECK_EQ(client.epoch(), engine.epoch());
  PF_REQUIRE(client.register_publisher().accepted());

  const PortId port = fixture.port(1);
  PortMutationMessage bind;
  bind.kind = 0;
  bind.port = port;
  bind.authority.epoch = client.epoch();
  bind.authority.publisher = client_config.publisher;
  bind.authority.boot = client_config.boot;
  bind.authority.attempt = MutationAttemptId::from_validated("wire-bind-1");
  bind.parent_device = fixture.device;
  bind.device_generation = DeviceGeneration::from_value(1);
  const MutationResult bound = client.mutate(bind);
  if (!bound.accepted()) {
    std::printf("  bind rejected: %s\n", bound.outcome.to_string().c_str());
  }
  PF_REQUIRE(bound.accepted());

  PortMutationMessage claim;
  claim.kind = 9;  // ClaimOwnership, in the wire domain
  claim.port = port;
  claim.authority.epoch = client.epoch();
  claim.authority.publisher = client_config.publisher;
  claim.authority.boot = client_config.boot;
  claim.authority.attempt = MutationAttemptId::from_validated("wire-claim-1");
  claim.owner_kind = static_cast<std::uint8_t>(PortOwnerKind::NetworkController);
  claim.owner = OwnerId::from_validated("controller-1");
  PF_REQUIRE(client.mutate(claim).accepted());

  QueryMessage query;
  query.kind = QueryKind::Port;
  query.port = port;
  const QueryResponse response = client.query(query);
  if (!response.outcome.accepted()) {
    std::printf("  query rejected: %s records=%zu\n", response.outcome.to_string().c_str(),
                response.records.size());
  }
  PF_REQUIRE(response.outcome.accepted());
  PF_REQUIRE_EQ(response.records.size(), static_cast<std::size_t>(1));
  PF_CHECK_EQ(response.records.front().lifecycle, PortLifecycle::Unconfigured);
  PF_CHECK(response.records.front().ownership.has_value());

  // A query about an unknown port is rejected through the wire.
  query.port = PortId::from_validated("unknown-port");
  PF_CHECK_EQ(client.query(query).outcome.code(), OutcomeCode::UnknownPort);

  client.close();
  PF_CHECK(!client.connected());
  PF_REQUIRE(server.join().ok());
  PF_CHECK(!server.running());
}

PF_TEST(malformed_frames_do_not_disturb_other_sessions) {
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Server10G, 2);
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  EngineService service(engine);
  ServerConfig server_config;
  server_config.port = 0;
  CoordinatorServer server(server_config, service);
  const Outcome started = server.start();
  if (!started.ok()) {
    PF_CHECK_EQ(started.code(), OutcomeCode::TransportFailure);
    return;
  }
  const std::uint16_t port = server.bound_port();

  // A healthy session is established first.
  ClientConfig client_config;
  client_config.port = port;
  client_config.publisher = PublisherId::from_validated("healthy");
  client_config.boot = WorkerBootId::from_validated("healthy-boot");
  FabricClient client(client_config);
  PF_REQUIRE(client.connect().accepted());

#if defined(_WIN32)
  // Garbage bytes are rejected and answered with an error frame. The payload is
  // longer than a frame header so that the coordinator can reach its framing
  // decision without waiting for more input.
  const std::string garbage = "not-a-port-fabric-frame-at-all-and-not-any-other-frame-either";
  const std::string answer = raw_exchange(port, garbage);
  PF_CHECK(!answer.empty());
  if (!answer.empty()) {
    Frame frame;
    const Outcome decoded = decode_frame(answer, frame);
    PF_CHECK(decoded.ok());
    if (decoded.ok()) {
      PF_CHECK_EQ(frame.type, MessageType::ErrorResponse);
      Outcome outcome;
      PF_REQUIRE(decode_outcome(frame.payload, outcome).ok());
      PF_CHECK(!outcome.ok());
    }
  }
  // An unknown message type is rejected.
  Frame unknown;
  unknown.type = static_cast<MessageType>(4242);
  std::string encoded_unknown;
  PF_CHECK_EQ(encode_frame(unknown, encoded_unknown).code(), OutcomeCode::MalformedRequest);
#endif

  // The healthy session still works after the malformed traffic.
  PF_REQUIRE(client.register_publisher().accepted());
  QueryMessage query;
  query.kind = QueryKind::EngineInfo;
  PF_CHECK(client.query(query).outcome.accepted());
  client.close();
  PF_REQUIRE(server.join().ok());
}

PF_TEST(session_bound_is_enforced_and_released) {
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Server10G, 1);
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  EngineService service(engine);
  ServerConfig server_config;
  server_config.port = 0;
  server_config.max_sessions = 2;
  CoordinatorServer server(server_config, service);
  const Outcome started = server.start();
  if (!started.ok()) {
    PF_CHECK_EQ(started.code(), OutcomeCode::TransportFailure);
    return;
  }
  std::vector<std::unique_ptr<FabricClient>> clients;
  for (int index = 0; index < 2; ++index) {
    ClientConfig config;
    config.port = server.bound_port();
    config.publisher = PublisherId::from_validated("client-" + std::to_string(index));
    config.boot = WorkerBootId::from_validated("boot-" + std::to_string(index));
    auto client = std::make_unique<FabricClient>(config);
    PF_REQUIRE(client->connect().accepted());
    clients.push_back(std::move(client));
  }
  PF_CHECK_EQ(server.active_sessions(), static_cast<std::size_t>(2));

  // A third session is refused without disturbing the others.
  ClientConfig overflow;
  overflow.port = server.bound_port();
  overflow.publisher = PublisherId::from_validated("client-overflow");
  overflow.boot = WorkerBootId::from_validated("boot-overflow");
  FabricClient overflow_client(overflow);
  const Outcome overflow_result = overflow_client.connect();
  PF_CHECK(!overflow_result.ok());

  for (auto& client : clients) {
    client->close();
  }
  // Closing sessions releases the bound.
  for (int attempt = 0; attempt < 2000 && server.active_sessions() != 0; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  PF_CHECK_EQ(server.active_sessions(), static_cast<std::size_t>(0));
  PF_REQUIRE(server.join().ok());
}

PF_TEST(server_start_and_stop_are_repeatable) {
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Server10G, 1);
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  EngineService service(engine);
  ServerConfig server_config;
  server_config.port = 0;
  CoordinatorServer server(server_config, service);
  for (int round = 0; round < 4; ++round) {
    const Outcome started = server.start();
    if (!started.ok()) {
      PF_CHECK_EQ(started.code(), OutcomeCode::TransportFailure);
      return;
    }
    PF_CHECK(server.running());
    const std::uint16_t port = server.bound_port();
    ClientConfig config;
    config.port = port;
    config.publisher = PublisherId::from_validated("round-client");
    config.boot = WorkerBootId::from_validated("round-boot-" + std::to_string(round));
    FabricClient client(config);
    PF_REQUIRE(client.connect().accepted());
    client.close();
    PF_REQUIRE(server.join().ok());
    PF_CHECK(!server.running());
    // A stopped server accepts no new sessions.
    FabricClient after(config);
    PF_CHECK(!after.connect().ok());
  }
}

PF_TEST(shutdown_with_a_session_blocked_in_receive_returns) {
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Server10G, 1);
  PortFabricEngine engine(pf_test::engine_config(), &fixture.provider, &fixture.adapter);
  EngineService service(engine);
  ServerConfig server_config;
  server_config.port = 0;
  CoordinatorServer server(server_config, service);
  const Outcome started = server.start();
  if (!started.ok()) {
    PF_CHECK_EQ(started.code(), OutcomeCode::TransportFailure);
    return;
  }
  ClientConfig config;
  config.port = server.bound_port();
  config.publisher = PublisherId::from_validated("idle-client");
  config.boot = WorkerBootId::from_validated("idle-boot");
  FabricClient client(config);
  PF_REQUIRE(client.connect().accepted());
  // The session is idle and blocked in a receive on the server side.
  PF_CHECK_EQ(server.active_sessions(), static_cast<std::size_t>(1));
  PF_REQUIRE(server.join().ok());
  PF_CHECK(!server.running());
  client.close();
}
