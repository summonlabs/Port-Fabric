#include "portfabric/server.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "codec.hpp"
#include "engine_internal.hpp"
#include "net.hpp"

namespace portfabric {
namespace {

/// Reads exactly length bytes, honouring the stop handle.
bool read_exact(detail::socket_handle socket, detail::stop_handle stop, std::size_t length,
                std::string& out, bool& stopped, std::string& error) {
  out.assign(length, '\0');
  std::size_t offset = 0;
  while (offset < length) {
    const int ready = detail::net_wait_readable(socket, stop, error);
    if (ready == 0) {
      stopped = true;
      return false;
    }
    if (ready < 0) {
      return false;
    }
    const int received = detail::net_receive(
        socket, reinterpret_cast<std::byte*>(out.data() + offset), length - offset, error);
    if (received < 0) {
      return false;
    }
    if (received == 0) {
      error = "the peer closed the connection";
      return false;
    }
    offset += static_cast<std::size_t>(received);
  }
  return true;
}

bool send_frame(detail::socket_handle socket, MessageType type, std::uint64_t sequence,
                const std::string& payload, std::string& error) {
  Frame frame;
  frame.type = type;
  frame.sequence = sequence;
  frame.payload = payload;
  std::string bytes;
  const Outcome encoded = encode_frame(frame, bytes);
  if (!encoded.ok()) {
    error = encoded.message();
    return false;
  }
  return detail::net_send_all(
      socket, std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()),
                                         bytes.size()),
      error);
}

}  // namespace

struct CoordinatorServer::Impl {
  ServerConfig config;
  CoordinatorService* service = nullptr;

  detail::socket_handle listener = detail::invalid_socket;
  detail::stop_handle stop = nullptr;
  std::uint16_t bound_port = 0;

  std::atomic<bool> running{false};
  std::atomic<bool> stop_requested{false};
  std::atomic<std::size_t> active_sessions{0};
  std::atomic<std::size_t> total_sessions{0};

  struct Session {
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> done;
  };

  mutable std::mutex session_mutex;
  std::vector<detail::socket_handle> session_sockets;
  std::vector<Session> sessions;
  std::thread accept_thread;

  mutable std::mutex error_mutex;
  std::string last_error;

  void set_error(std::string message) {
    std::lock_guard<std::mutex> lock(error_mutex);
    last_error = std::move(message);
  }

  void register_session(detail::socket_handle socket) {
    std::lock_guard<std::mutex> lock(session_mutex);
    session_sockets.push_back(socket);
  }

  void unregister_session(detail::socket_handle socket) {
    std::lock_guard<std::mutex> lock(session_mutex);
    for (auto it = session_sockets.begin(); it != session_sockets.end(); ++it) {
      if (*it == socket) {
        session_sockets.erase(it);
        break;
      }
    }
  }

  void shutdown_sessions() {
    std::lock_guard<std::mutex> lock(session_mutex);
    for (const detail::socket_handle socket : session_sockets) {
      detail::net_shutdown(socket);
    }
  }

  /// Joins and removes every session that has already finished. Called from the
  /// accept thread only, which is the only place that creates sessions.
  void prune_sessions() {
    // Finished sessions are moved out under the lock and joined after it is
    // released: a session thread takes the same lock while unregistering itself,
    // so joining under the lock would deadlock.
    std::vector<Session> finished;
    {
      std::lock_guard<std::mutex> lock(session_mutex);
      for (auto it = sessions.begin(); it != sessions.end();) {
        if (it->done->load()) {
          finished.push_back(std::move(*it));
          it = sessions.erase(it);
        } else {
          ++it;
        }
      }
    }
    for (Session& session : finished) {
      if (session.thread.joinable()) {
        session.thread.join();
      }
    }
  }

  /// Serves one session until the peer closes, the runtime stops, or a protocol
  /// violation terminates the session.
  void serve(detail::socket_handle socket) {
    active_sessions.fetch_add(1);
    total_sessions.fetch_add(1);
    register_session(socket);
    bool hello_seen = false;
    std::uint64_t sequence = 0;
    std::string error;
    while (!stop_requested.load()) {
      std::string header;
      bool stopped = false;
      if (!read_exact(socket, stop, frame_header_size, header, stopped, error)) {
        if (!stopped && !error.empty() && !stop_requested.load()) {
          set_error(error);
        }
        break;
      }
      FrameHeader decoded_header;
      const Outcome header_outcome = decode_frame_header(header, decoded_header);
      if (!header_outcome.ok()) {
        // A malformed frame is answered once with an error and then the session
        // ends: the stream can no longer be trusted to be aligned.
        std::string payload;
        Outcome failure = Outcome::failure(OutcomeCode::TransportFailure, header_outcome.message());
        if (encode_outcome(failure, payload).ok()) {
          (void)send_frame(socket, MessageType::ErrorResponse, ++sequence, payload, error);
        }
        set_error(header_outcome.message());
        break;
      }
      std::string payload;
      if (decoded_header.payload_length > 0) {
        if (!read_exact(socket, stop, decoded_header.payload_length, payload, stopped, error)) {
          if (!stopped) {
            set_error(error);
          }
          break;
        }
        if (detail::crc32(payload) != decoded_header.payload_crc) {
          Outcome failure = Outcome::failure(OutcomeCode::TransportFailure,
                                             "the frame payload integrity check failed");
          std::string encoded;
          if (encode_outcome(failure, encoded).ok()) {
            (void)send_frame(socket, MessageType::ErrorResponse, ++sequence, encoded, error);
          }
          set_error("frame payload integrity check failed");
          break;
        }
      }
      ++sequence;
      const std::uint64_t response_sequence = decoded_header.sequence;
      switch (decoded_header.type) {
        case MessageType::Hello: {
          HelloMessage hello;
          const Outcome decoded = decode_hello(payload, hello);
          if (!decoded.ok()) {
            std::string encoded;
            if (encode_outcome(Outcome::failure(OutcomeCode::MalformedRequest, decoded.message()),
                               encoded)
                    .ok()) {
              (void)send_frame(socket, MessageType::ErrorResponse, response_sequence, encoded, error);
            }
            break;
          }
          CoordinatorEpoch epoch;
          const Outcome outcome = service->on_hello(hello, epoch);
          HelloAckMessage ack;
          ack.accepted = outcome.accepted();
          ack.epoch = epoch;
          ack.detail = outcome.message();
          std::string encoded;
          if (encode_hello_ack(ack, encoded).ok()) {
            (void)send_frame(socket, MessageType::HelloAck, response_sequence, encoded, error);
          }
          hello_seen = outcome.accepted();
          break;
        }
        case MessageType::RegisterPublisher: {
          RegisterPublisherMessage message;
          std::string encoded;
          if (!hello_seen) {
            if (encode_outcome(Outcome::failure(OutcomeCode::MalformedRequest,
                                                "the session has not completed its opening handshake"),
                               encoded)
                    .ok()) {
              (void)send_frame(socket, MessageType::ErrorResponse, response_sequence, encoded, error);
            }
            break;
          }
          const Outcome decoded = decode_register_publisher(payload, message);
          const Outcome outcome = decoded.ok()
                                      ? service->on_register_publisher(message)
                                      : Outcome::failure(OutcomeCode::MalformedRequest,
                                                         decoded.message());
          if (encode_outcome(outcome, encoded).ok()) {
            (void)send_frame(socket, MessageType::RegisterPublisherAck, response_sequence, encoded,
                             error);
          }
          break;
        }
        case MessageType::Mutation: {
          PortMutationMessage message;
          std::string encoded;
          if (!hello_seen) {
            if (encode_outcome(Outcome::failure(OutcomeCode::MalformedRequest,
                                                "the session has not completed its opening handshake"),
                               encoded)
                    .ok()) {
              (void)send_frame(socket, MessageType::ErrorResponse, response_sequence, encoded, error);
            }
            break;
          }
          const Outcome decoded = decode_mutation(payload, message);
          if (!decoded.ok()) {
            if (encode_outcome(Outcome::failure(OutcomeCode::MalformedRequest, decoded.message()),
                               encoded)
                    .ok()) {
              (void)send_frame(socket, MessageType::ErrorResponse, response_sequence, encoded, error);
            }
            break;
          }
          const MutationResult result = service->on_mutation(message);
          if (encode_outcome(result.outcome, encoded).ok()) {
            (void)send_frame(socket, MessageType::MutationAck, response_sequence, encoded, error);
          }
          break;
        }
        case MessageType::Query: {
          QueryMessage message;
          std::string encoded;
          if (!hello_seen) {
            if (encode_outcome(Outcome::failure(OutcomeCode::MalformedRequest,
                                                "the session has not completed its opening handshake"),
                               encoded)
                    .ok()) {
              (void)send_frame(socket, MessageType::ErrorResponse, response_sequence, encoded, error);
            }
            break;
          }
          const Outcome decoded = decode_query(payload, message);
          if (!decoded.ok()) {
            if (encode_outcome(Outcome::failure(OutcomeCode::MalformedRequest, decoded.message()),
                               encoded)
                    .ok()) {
              (void)send_frame(socket, MessageType::ErrorResponse, response_sequence, encoded, error);
            }
            break;
          }
          const QueryResponse response = service->on_query(message);
          if (encode_query_response(response, encoded).ok()) {
            (void)send_frame(socket, MessageType::QueryAck, response_sequence, encoded, error);
          } else {
            QueryResponse failure = response;
            failure.outcome = Outcome::failure(OutcomeCode::ResourceBoundExceeded,
                                               "the query response exceeds the frame bound");
            failure.records.clear();
            failure.ports.clear();
            failure.profiles.clear();
            failure.devices.clear();
            if (encode_query_response(failure, encoded).ok()) {
              (void)send_frame(socket, MessageType::QueryAck, response_sequence, encoded, error);
            }
          }
          break;
        }
        case MessageType::FencePublisher: {
          FenceMessage message;
          std::string encoded;
          const Outcome decoded = decode_fence(payload, message);
          const Outcome outcome = decoded.ok()
                                      ? service->on_fence(message)
                                      : Outcome::failure(OutcomeCode::MalformedRequest,
                                                         decoded.message());
          if (encode_outcome(outcome, encoded).ok()) {
            (void)send_frame(socket, MessageType::FencePublisherAck, response_sequence, encoded,
                             error);
          }
          break;
        }
        case MessageType::Heartbeat: {
          (void)send_frame(socket, MessageType::HeartbeatAck, response_sequence, std::string(),
                           error);
          break;
        }
        case MessageType::Shutdown: {
          // Remote shutdown is not part of the protocol: the coordinator is
          // stopped by its own control surface, never by a connected peer.
          std::string encoded;
          if (encode_outcome(Outcome::failure(OutcomeCode::MalformedRequest,
                                              "the coordinator does not accept a remote shutdown "
                                              "request"),
                             encoded)
                  .ok()) {
            (void)send_frame(socket, MessageType::ErrorResponse, response_sequence, encoded, error);
          }
          break;
        }
        case MessageType::HelloAck:
        case MessageType::RegisterPublisherAck:
        case MessageType::MutationAck:
        case MessageType::QueryAck:
        case MessageType::FencePublisherAck:
        case MessageType::HeartbeatAck:
        case MessageType::ErrorResponse: {
          // A client never sends a response message. Answering with an error and
          // closing keeps the protocol unambiguous.
          std::string encoded;
          if (encode_outcome(Outcome::failure(OutcomeCode::MalformedRequest,
                                              "the peer sent a response message to the coordinator"),
                             encoded)
                  .ok()) {
            (void)send_frame(socket, MessageType::ErrorResponse, response_sequence, encoded, error);
          }
          set_error("the peer sent a response message to the coordinator");
          unregister_session(socket);
          detail::net_shutdown(socket);
          detail::net_close(socket);
          active_sessions.fetch_sub(1);
          return;
        }
      }
    }
    unregister_session(socket);
    detail::net_shutdown(socket);
    detail::net_close(socket);
    active_sessions.fetch_sub(1);
  }

  void accept_loop() {
    while (!stop_requested.load()) {
      prune_sessions();
      bool stopped = false;
      std::string error;
      const detail::socket_handle socket = detail::net_accept(listener, stop, stopped, error);
      if (stopped) {
        break;
      }
      if (socket == detail::invalid_socket) {
        if (!error.empty()) {
          set_error(error);
        }
        continue;
      }
      {
        std::lock_guard<std::mutex> lock(session_mutex);
        if (sessions.size() >= config.max_sessions) {
          detail::net_close(socket);
          set_error("the session bound was reached and an inbound connection was refused");
          continue;
        }
        Session session;
        session.done = std::make_shared<std::atomic<bool>>(false);
        session.thread = std::thread([this, socket, done = session.done]() {
          serve(socket);
          done->store(true);
        });
        sessions.push_back(std::move(session));
      }
    }
    running.store(false);
  }
};

CoordinatorService::~CoordinatorService() = default;

EngineService::EngineService(PortFabricEngine& engine) : engine_(engine) {}

Outcome EngineService::on_hello(const HelloMessage& message, CoordinatorEpoch& epoch) {
  epoch = engine_.epoch();
  if (!message.publisher.valid() || !message.boot.valid()) {
    return Outcome::failure(OutcomeCode::MalformedRequest,
                            "the opening message does not identify the connecting process");
  }
  if (message.epoch.valid() && message.epoch > epoch) {
    return Outcome::failure(OutcomeCode::StaleCoordinatorEpoch,
                            "the client claims a coordinator epoch newer than the coordinator");
  }
  return Outcome::success("session accepted");
}

Outcome EngineService::on_register_publisher(const RegisterPublisherMessage& message) {
  return engine_.register_publisher(message.publisher, message.boot, message.epoch);
}

Outcome EngineService::on_fence(const FenceMessage& message) {
  return engine_.fence_publisher(message.publisher, message.boot);
}

MutationResult EngineService::on_mutation(const PortMutationMessage& message) {
  return dispatch(engine_, message);
}

MutationResult EngineService::dispatch(PortFabricEngine& engine,
                                       const PortMutationMessage& message) {
  if (message.kind > static_cast<std::uint8_t>(PortMutationKind::ObserveExternalState)) {
    MutationResult result;
    result.outcome = Outcome::failure(OutcomeCode::MalformedRequest,
                                      "the mutation kind is not a legal value");
    result.explanation.accepted = false;
    result.explanation.outcome = result.outcome.code();
    result.explanation.add(ExplanationCode::RejectedMalformedRequest, "mutation kind");
    return result;
  }
  const auto kind = static_cast<PortMutationKind>(message.kind);
  MutationEnvelope envelope;
  envelope.authority = message.authority;
  envelope.port = message.port;
  envelope.provenance_kind = static_cast<ProvenanceKind>(message.provenance_kind);
  envelope.provenance_source = message.provenance_source;
  switch (kind) {
    case PortMutationKind::BindPort: {
      PortBindingRequest request;
      request.port = message.port;
      request.parent_device = message.parent_device;
      request.device_generation = message.device_generation;
      request.provenance_kind = envelope.provenance_kind;
      request.provenance_source = envelope.provenance_source;
      request.epoch = message.authority.epoch;
      request.attempt = message.authority.attempt;
      return engine.bind_port(request);
    }
    case PortMutationKind::Configure: {
      if (!message.has_configuration) {
        MutationResult result;
        result.outcome = Outcome::failure(OutcomeCode::MalformedRequest,
                                          "a configuration mutation requires a configuration "
                                          "payload");
        result.explanation.accepted = false;
        result.explanation.outcome = result.outcome.code();
        return result;
      }
      ConfigureRequest request;
      request.envelope = envelope;
      request.configuration = message.configuration;
      return engine.configure(request);
    }
    case PortMutationKind::Reconfigure: {
      if (!message.has_configuration) {
        MutationResult result;
        result.outcome = Outcome::failure(OutcomeCode::MalformedRequest,
                                          "a reconfiguration requires a configuration payload");
        result.explanation.accepted = false;
        result.explanation.outcome = result.outcome.code();
        return result;
      }
      ReconfigureRequest request;
      request.envelope = envelope;
      request.configuration = message.configuration;
      return engine.reconfigure(request);
    }
    case PortMutationKind::Administrative: {
      AdministrativeRequest request;
      request.envelope = envelope;
      if (!is_valid(static_cast<AdministrativeOp>(message.administrative_op))) {
        MutationResult result;
        result.outcome = Outcome::failure(OutcomeCode::MalformedRequest,
                                          "the administrative operation is not a legal value");
        result.explanation.accepted = false;
        result.explanation.outcome = result.outcome.code();
        return result;
      }
      request.op = static_cast<AdministrativeOp>(message.administrative_op);
      return engine.administrative(request);
    }
    case PortMutationKind::AssignProfile: {
      // A profile assignment carries no configuration payload: the runtime reads
      // the committed generation and applies the profile to it.
      ProfileRequest request;
      request.envelope = envelope;
      request.profile = message.profile;
      request.generation = message.profile_generation;
      request.release = message.release;
      return engine.assign_profile(request);
    }
    case PortMutationKind::BindCapabilities: {
      CapabilityRequest request;
      request.envelope = envelope;
      request.binding = message.capability;
      return engine.bind_capabilities(request);
    }
    case PortMutationKind::Reconcile: {
      ReconcileRequest request;
      request.envelope = envelope;
      request.detail = message.detail;
      return engine.reconcile(request);
    }
    case PortMutationKind::Supersede: {
      FenceRequest request;
      request.envelope = envelope;
      request.detail = message.detail;
      return engine.supersede(request);
    }
    case PortMutationKind::Retire: {
      FenceRequest request;
      request.envelope = envelope;
      request.detail = message.detail;
      return engine.retire(request);
    }
    case PortMutationKind::ClaimOwnership:
    case PortMutationKind::TransferOwnership:
    case PortMutationKind::ReleaseOwnership: {
      OwnershipRequest request;
      request.envelope = envelope;
      request.owner_kind = static_cast<PortOwnerKind>(message.owner_kind);
      request.owner = message.owner;
      request.exclusive = message.exclusive;
      request.release = kind == PortMutationKind::ReleaseOwnership;
      if (kind == PortMutationKind::ClaimOwnership) {
        return engine.claim_ownership(request);
      }
      if (kind == PortMutationKind::TransferOwnership) {
        return engine.transfer_ownership(request);
      }
      return engine.release_ownership(request);
    }
    case PortMutationKind::RecordAppliedEvidence: {
      EvidenceRequest request;
      request.envelope = envelope;
      request.evidence = message.evidence;
      return engine.record_applied_evidence(request);
    }
    case PortMutationKind::ObserveExternalState: {
      DriftRequest request;
      request.envelope = envelope;
      if (!is_valid(static_cast<DriftState>(message.drift))) {
        MutationResult result;
        result.outcome = Outcome::failure(OutcomeCode::MalformedRequest,
                                          "the observed drift state is not a legal value");
        result.explanation.accepted = false;
        result.explanation.outcome = result.outcome.code();
        return result;
      }
      request.observed = static_cast<DriftState>(message.drift);
      request.detail = message.detail;
      return engine.observe_external_state(request);
    }
  }
  MutationResult result;
  result.outcome = Outcome::failure(OutcomeCode::MalformedRequest,
                                    "the mutation kind is not implemented");
  result.explanation.accepted = false;
  result.explanation.outcome = result.outcome.code();
  return result;
}

QueryResponse EngineService::on_query(const QueryMessage& message) {
  QueryResponse response;
  response.kind = message.kind;
  response.generation = engine_.generation();
  response.epoch = engine_.epoch();
  response.digest = engine_.state_digest();
  switch (message.kind) {
    case QueryKind::Port: {
      const auto record = engine_.record(message.port);
      if (!record.has_value()) {
        response.outcome = Outcome::failure(OutcomeCode::UnknownPort,
                                            "the port identity is not bound to this runtime");
        return response;
      }
      response.records.push_back(*record);
      response.text = record->to_string();
      break;
    }
    case QueryKind::Configuration: {
      const auto configuration = engine_.configuration(message.port);
      if (!configuration.has_value()) {
        response.outcome = Outcome::failure(OutcomeCode::UnknownPort,
                                            "no committed configuration exists for this port");
        return response;
      }
      response.text = configuration->to_string();
      break;
    }
    case QueryKind::Lifecycle: {
      const auto lifecycle = engine_.lifecycle(message.port);
      if (!lifecycle.has_value()) {
        response.outcome =
            Outcome::failure(OutcomeCode::UnknownPort, "the port is not bound to this runtime");
        return response;
      }
      response.text = std::string(portfabric::to_string(*lifecycle));
      break;
    }
    case QueryKind::AdministrativeState: {
      const auto state = engine_.administrative_state(message.port);
      if (!state.has_value()) {
        response.outcome = Outcome::failure(OutcomeCode::UnknownPort,
                                            "the port carries no administrative state");
        return response;
      }
      response.text = std::string(portfabric::to_string(*state));
      break;
    }
    case QueryKind::Ownership: {
      const auto ownership = engine_.ownership(message.port);
      if (!ownership.has_value()) {
        response.outcome = Outcome::failure(OutcomeCode::UnknownOwnership,
                                            "the port is not owned");
        return response;
      }
      response.text = ownership->to_string();
      break;
    }
    case QueryKind::CapabilityBinding: {
      const auto binding = engine_.capability_binding(message.port);
      if (!binding.has_value()) {
        response.outcome =
            Outcome::failure(OutcomeCode::UnknownPort, "the port is not bound to this runtime");
        return response;
      }
      response.text = binding->to_string();
      break;
    }
    case QueryKind::AppliedEvidence: {
      const auto applied = engine_.applied_evidence(message.port);
      if (!applied.has_value()) {
        response.outcome =
            Outcome::failure(OutcomeCode::UnknownPort, "the port is not bound to this runtime");
        return response;
      }
      response.text = applied->to_string();
      break;
    }
    case QueryKind::PortsOfDevice:
      response.ports = engine_.ports_of_device(message.device);
      break;
    case QueryKind::PortsWithLifecycle:
      response.ports = engine_.ports_with_lifecycle(message.lifecycle);
      break;
    case QueryKind::PortsRequiringRevalidation:
      response.ports = engine_.ports_requiring_revalidation();
      break;
    case QueryKind::PortsOfOwner:
      response.ports = engine_.ports_of_owner(message.owner);
      break;
    case QueryKind::PortsByProfile:
      response.ports = engine_.ports_by_profile(message.profile);
      break;
    case QueryKind::DerivedPorts:
      response.ports = engine_.derived_ports(message.port);
      break;
    case QueryKind::StaleProfileBindings:
      response.ports = engine_.ports_with_stale_profile_binding();
      break;
    case QueryKind::Explain:
      response.text = engine_.explain(message.port).to_string();
      break;
    case QueryKind::Snapshot: {
      const Snapshot snapshot = engine_.snapshot();
      response.records = snapshot.records;
      response.text = snapshot.to_string();
      break;
    }
    case QueryKind::EngineInfo:
      response.text = "ports=" + std::to_string(engine_.port_count()) +
                      " profiles=" + std::to_string(engine_.profile_count()) +
                      " generation=" + (response.generation.valid()
                                            ? response.generation.to_string()
                                            : std::string("none")) +
                      " epoch=" +
                      (response.epoch.valid() ? response.epoch.to_string() : std::string("none")) +
                      " digest=" + response.digest.to_hex();
      break;
    case QueryKind::Profiles:
      response.profiles = engine_.profiles();
      break;
    case QueryKind::Devices:
      response.devices = engine_.devices();
      break;
  }
  return response;
}

CoordinatorServer::CoordinatorServer(const ServerConfig& config, CoordinatorService& service)
    : impl_(std::make_unique<Impl>()) {
  impl_->config = config;
  impl_->service = &service;
}

CoordinatorServer::~CoordinatorServer() { (void)join(); }

Outcome CoordinatorServer::start() {
  if (impl_->running.load()) {
    return Outcome::current("the server is already running");
  }
  if (!detail::net_available()) {
    return Outcome::failure(OutcomeCode::TransportFailure,
                            "the socket runtime is unavailable on this host");
  }
  std::string error;
  impl_->stop = detail::net_create_stop_handle();
  impl_->listener = detail::net_listen(impl_->config.address, impl_->config.port, error,
                                       impl_->bound_port);
  if (impl_->listener == detail::invalid_socket) {
    detail::net_destroy_stop_handle(impl_->stop);
    impl_->stop = nullptr;
    return Outcome::failure(OutcomeCode::TransportFailure, error);
  }
  impl_->stop_requested.store(false);
  impl_->running.store(true);
  impl_->accept_thread = std::thread([this]() { impl_->accept_loop(); });
  return Outcome::success("the coordinator is listening");
}

void CoordinatorServer::stop() {
  impl_->stop_requested.store(true);
  detail::net_signal_stop(impl_->stop);
  impl_->shutdown_sessions();
}

Outcome CoordinatorServer::join() {
  if (impl_->listener == detail::invalid_socket && impl_->stop == nullptr) {
    return Outcome::success("the server is not running");
  }
  stop();
  if (impl_->accept_thread.joinable()) {
    impl_->accept_thread.join();
  }
  {
    // Sessions are detached from the registry under the lock and joined after it
    // is released, so a session that is unregistering itself cannot deadlock the
    // shutdown path.
    std::vector<Impl::Session> sessions;
    {
      std::lock_guard<std::mutex> lock(impl_->session_mutex);
      sessions.swap(impl_->sessions);
    }
    for (Impl::Session& session : sessions) {
      if (session.thread.joinable()) {
        session.thread.join();
      }
    }
  }
  if (impl_->listener != detail::invalid_socket) {
    detail::net_close(impl_->listener);
    impl_->listener = detail::invalid_socket;
  }
  if (impl_->stop != nullptr) {
    detail::net_destroy_stop_handle(impl_->stop);
    impl_->stop = nullptr;
  }
  impl_->running.store(false);
  return Outcome::success("the coordinator stopped");
}

std::uint16_t CoordinatorServer::bound_port() const { return impl_->bound_port; }

std::size_t CoordinatorServer::active_sessions() const {
  return impl_->active_sessions.load();
}

std::size_t CoordinatorServer::total_sessions() const { return impl_->total_sessions.load(); }

bool CoordinatorServer::running() const { return impl_->running.load(); }

std::string CoordinatorServer::last_error() const {
  std::lock_guard<std::mutex> lock(impl_->error_mutex);
  return impl_->last_error;
}

}  // namespace portfabric
