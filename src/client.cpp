#include "portfabric/client.hpp"

#include <mutex>
#include <string>

#include "codec.hpp"
#include "net.hpp"

namespace portfabric {

struct FabricClient::Impl {
  ClientConfig config;
  detail::socket_handle socket = detail::invalid_socket;
  std::uint64_t sequence = 0;
  CoordinatorEpoch epoch;
  mutable std::mutex mutex;
  std::string last_error;
};

namespace {

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

/// Reads one complete frame. Returns false on failure and sets closed when the
/// peer closed the connection cleanly.
bool receive_frame(detail::socket_handle socket, MessageType expected, Frame& out, std::string& error,
                   bool& closed) {
  std::string header(frame_header_size, '\0');
  std::size_t offset = 0;
  while (offset < frame_header_size) {
    const int received = detail::net_receive(
        socket, reinterpret_cast<std::byte*>(header.data() + offset), frame_header_size - offset,
        error);
    if (received < 0) {
      return false;
    }
    if (received == 0) {
      error = "the coordinator closed the connection";
      closed = true;
      return false;
    }
    offset += static_cast<std::size_t>(received);
  }
  FrameHeader decoded_header;
  const Outcome header_outcome = decode_frame_header(header, decoded_header);
  if (!header_outcome.ok()) {
    error = header_outcome.message();
    return false;
  }
  std::string payload;
  if (decoded_header.payload_length > 0) {
    payload.assign(decoded_header.payload_length, '\0');
    offset = 0;
    while (offset < payload.size()) {
      const int received = detail::net_receive(
          socket, reinterpret_cast<std::byte*>(payload.data() + offset), payload.size() - offset,
          error);
      if (received < 0) {
        return false;
      }
      if (received == 0) {
        error = "the coordinator closed the connection";
        closed = true;
        return false;
      }
      offset += static_cast<std::size_t>(received);
    }
    if (detail::crc32(payload) != decoded_header.payload_crc) {
      error = "the frame payload integrity check failed";
      return false;
    }
  }
  if (expected != MessageType::Heartbeat && decoded_header.type == MessageType::ErrorResponse) {
    Outcome failure;
    if (decode_outcome(payload, failure).ok()) {
      error = failure.to_string();
      return false;
    }
    error = "the coordinator rejected the request";
    return false;
  }
  out.type = decoded_header.type;
  out.flags = decoded_header.flags;
  out.sequence = decoded_header.sequence;
  out.payload = std::move(payload);
  return true;
}

}  // namespace

FabricClient::FabricClient(const ClientConfig& config) : impl_(std::make_unique<Impl>()) {
  impl_->config = config;
}

FabricClient::~FabricClient() { close(); }

bool FabricClient::connected() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->socket != detail::invalid_socket;
}

CoordinatorEpoch FabricClient::epoch() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->epoch;
}

const std::string& FabricClient::last_error() const { return impl_->last_error; }

void FabricClient::close_locked() {
  if (impl_->socket != detail::invalid_socket) {
    detail::net_shutdown(impl_->socket);
    detail::net_close(impl_->socket);
    impl_->socket = detail::invalid_socket;
  }
}

void FabricClient::close() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  close_locked();
}

Outcome FabricClient::connect() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->socket != detail::invalid_socket) {
    return Outcome::current("the client is already connected");
  }
  if (!detail::net_available()) {
    return Outcome::failure(OutcomeCode::TransportFailure,
                            "the socket runtime is unavailable on this host");
  }
  if (!impl_->config.publisher.valid() || !impl_->config.boot.valid()) {
    return Outcome::failure(OutcomeCode::MalformedRequest,
                            "the client configuration does not identify the process");
  }
  std::string error;
  impl_->socket = detail::net_connect(impl_->config.address, impl_->config.port, error);
  if (impl_->socket == detail::invalid_socket) {
    impl_->last_error = error;
    return Outcome::failure(OutcomeCode::TransportFailure, error);
  }
  HelloMessage hello;
  hello.protocol_version = kProtocolVersion;
  hello.publisher = impl_->config.publisher;
  hello.boot = impl_->config.boot;
  hello.epoch = CoordinatorEpoch{};
  hello.role = impl_->config.role;
  std::string payload;
  if (const Outcome encoded = encode_hello(hello, payload); !encoded.ok()) {
    close_locked();
    return encoded;
  }
  const std::uint64_t sequence = ++impl_->sequence;
  if (!send_frame(impl_->socket, MessageType::Hello, sequence, payload, error)) {
    close_locked();
    impl_->last_error = error;
    return Outcome::failure(OutcomeCode::TransportFailure, error);
  }
  Frame response;
  bool closed = false;
  if (!receive_frame(impl_->socket, MessageType::HelloAck, response, error, closed)) {
    close_locked();
    impl_->last_error = error;
    return Outcome::failure(OutcomeCode::TransportFailure, error);
  }
  if (response.type != MessageType::HelloAck) {
    close_locked();
    return Outcome::failure(OutcomeCode::TransportFailure,
                            "the coordinator answered the opening message with an unexpected type");
  }
  HelloAckMessage ack;
  if (const Outcome decoded = decode_hello_ack(response.payload, ack); !decoded.ok()) {
    close_locked();
    return decoded;
  }
  if (!ack.accepted) {
    close_locked();
    return Outcome::failure(OutcomeCode::UnauthorizedOwner, ack.detail);
  }
  impl_->epoch = ack.epoch;
  return Outcome::success("session established");
}

Outcome FabricClient::register_publisher() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->socket == detail::invalid_socket) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the client is not connected");
  }
  RegisterPublisherMessage message;
  message.publisher = impl_->config.publisher;
  message.boot = impl_->config.boot;
  message.epoch = impl_->epoch;
  std::string payload;
  if (const Outcome encoded = encode_register_publisher(message, payload); !encoded.ok()) {
    return encoded;
  }
  std::string error;
  const std::uint64_t sequence = ++impl_->sequence;
  if (!send_frame(impl_->socket, MessageType::RegisterPublisher, sequence, payload, error)) {
    impl_->last_error = error;
    return Outcome::failure(OutcomeCode::TransportFailure, error);
  }
  Frame response;
  bool closed = false;
  if (!receive_frame(impl_->socket, MessageType::RegisterPublisherAck, response, error, closed)) {
    impl_->last_error = error;
    return Outcome::failure(OutcomeCode::TransportFailure, error);
  }
  Outcome outcome;
  if (const Outcome decoded = decode_outcome(response.payload, outcome); !decoded.ok()) {
    return decoded;
  }
  return outcome;
}

MutationResult FabricClient::mutate(const PortMutationMessage& message) {
  MutationResult result;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->socket == detail::invalid_socket) {
    result.outcome = Outcome::failure(OutcomeCode::TransportFailure, "the client is not connected");
    result.explanation.accepted = false;
    result.explanation.outcome = result.outcome.code();
    return result;
  }
  std::string payload;
  if (const Outcome encoded = encode_mutation(message, payload); !encoded.ok()) {
    result.outcome = encoded;
    result.explanation.accepted = false;
    result.explanation.outcome = encoded.code();
    return result;
  }
  std::string error;
  const std::uint64_t sequence = ++impl_->sequence;
  if (!send_frame(impl_->socket, MessageType::Mutation, sequence, payload, error)) {
    impl_->last_error = error;
    result.outcome = Outcome::failure(OutcomeCode::TransportFailure, error);
    result.explanation.accepted = false;
    result.explanation.outcome = result.outcome.code();
    return result;
  }
  Frame response;
  bool closed = false;
  if (!receive_frame(impl_->socket, MessageType::MutationAck, response, error, closed)) {
    impl_->last_error = error;
    result.outcome = Outcome::failure(OutcomeCode::TransportFailure, error);
    result.explanation.accepted = false;
    result.explanation.outcome = result.outcome.code();
    return result;
  }
  if (const Outcome decoded = decode_outcome(response.payload, result.outcome); !decoded.ok()) {
    result.outcome = decoded;
    result.explanation.accepted = false;
    result.explanation.outcome = decoded.code();
    return result;
  }
  result.explanation.accepted = result.outcome.accepted();
  result.explanation.outcome = result.outcome.code();
  result.explanation.summary = result.outcome.message();
  return result;
}

QueryResponse FabricClient::query(const QueryMessage& message) {
  QueryResponse response;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->socket == detail::invalid_socket) {
    response.outcome = Outcome::failure(OutcomeCode::TransportFailure, "the client is not connected");
    return response;
  }
  std::string payload;
  if (const Outcome encoded = encode_query(message, payload); !encoded.ok()) {
    response.outcome = encoded;
    return response;
  }
  std::string error;
  const std::uint64_t sequence = ++impl_->sequence;
  if (!send_frame(impl_->socket, MessageType::Query, sequence, payload, error)) {
    impl_->last_error = error;
    response.outcome = Outcome::failure(OutcomeCode::TransportFailure, error);
    return response;
  }
  Frame frame;
  bool closed = false;
  if (!receive_frame(impl_->socket, MessageType::QueryAck, frame, error, closed)) {
    impl_->last_error = error;
    response.outcome = Outcome::failure(OutcomeCode::TransportFailure, error);
    return response;
  }
  if (const Outcome decoded = decode_query_response(frame.payload, response); !decoded.ok()) {
    response.outcome = decoded;
    return response;
  }
  return response;
}

Outcome FabricClient::fence_publisher(const PublisherId& publisher, const WorkerBootId& boot,
                                      std::string detail) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->socket == detail::invalid_socket) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the client is not connected");
  }
  FenceMessage message;
  message.publisher = publisher;
  message.boot = boot;
  message.detail = std::move(detail);
  std::string payload;
  if (const Outcome encoded = encode_fence(message, payload); !encoded.ok()) {
    return encoded;
  }
  std::string error;
  const std::uint64_t sequence = ++impl_->sequence;
  if (!send_frame(impl_->socket, MessageType::FencePublisher, sequence, payload, error)) {
    impl_->last_error = error;
    return Outcome::failure(OutcomeCode::TransportFailure, error);
  }
  Frame response;
  bool closed = false;
  if (!receive_frame(impl_->socket, MessageType::FencePublisherAck, response, error, closed)) {
    impl_->last_error = error;
    return Outcome::failure(OutcomeCode::TransportFailure, error);
  }
  Outcome outcome;
  if (const Outcome decoded = decode_outcome(response.payload, outcome); !decoded.ok()) {
    return decoded;
  }
  return outcome;
}

}  // namespace portfabric
