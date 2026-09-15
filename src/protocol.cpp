#include "portfabric/protocol.hpp"

#include <array>
#include <cstdio>
#include <cstring>

#include "codec.hpp"
#include "text.hpp"
#include "wire.hpp"

namespace portfabric {
namespace {

using detail::ByteReader;
using detail::ByteWriter;

constexpr char kMagic[4] = {'P', 'F', 'R', '1'};
constexpr std::size_t kPayloadLimit = max_frame_bytes;

void write_outcome(ByteWriter& writer, const Outcome& outcome) {
  writer.u8(static_cast<std::uint8_t>(outcome.code()));
  writer.text(outcome.message(), max_description_length);
  writer.u32(static_cast<std::uint32_t>(outcome.details().size()));
  for (const auto& [key, value] : outcome.details()) {
    writer.text(key, max_name_length);
    writer.text(value, max_description_length);
  }
}

bool read_outcome(ByteReader& reader, Outcome& outcome) {
  std::uint8_t code = 0;
  if (!reader.u8(code)) {
    return false;
  }
  outcome = Outcome::failure(static_cast<OutcomeCode>(code), std::string());
  std::string message;
  if (!reader.text(max_description_length, message)) {
    return false;
  }
  outcome.with_message(message);
  std::uint32_t count = 0;
  if (!detail::read_bounded_count(reader, max_outcome_details, count)) {
    return false;
  }
  for (std::uint32_t index = 0; index < count; ++index) {
    std::string key;
    std::string value;
    if (!reader.text(max_name_length, key) || !reader.text(max_description_length, value)) {
      return false;
    }
    outcome.with(std::move(key), std::move(value));
  }
  return true;
}

}  // namespace

std::string_view to_string(PortMutationKind kind) noexcept {
  switch (kind) {
    case PortMutationKind::BindPort:
      return "BIND_PORT";
    case PortMutationKind::Configure:
      return "CONFIGURE";
    case PortMutationKind::Reconfigure:
      return "RECONFIGURE";
    case PortMutationKind::Administrative:
      return "ADMINISTRATIVE";
    case PortMutationKind::AssignProfile:
      return "ASSIGN_PROFILE";
    case PortMutationKind::BindCapabilities:
      return "BIND_CAPABILITIES";
    case PortMutationKind::Reconcile:
      return "RECONCILE";
    case PortMutationKind::Supersede:
      return "SUPERSEDE";
    case PortMutationKind::Retire:
      return "RETIRE";
    case PortMutationKind::ClaimOwnership:
      return "CLAIM_OWNERSHIP";
    case PortMutationKind::TransferOwnership:
      return "TRANSFER_OWNERSHIP";
    case PortMutationKind::ReleaseOwnership:
      return "RELEASE_OWNERSHIP";
    case PortMutationKind::RecordAppliedEvidence:
      return "RECORD_APPLIED_EVIDENCE";
    case PortMutationKind::ObserveExternalState:
      return "OBSERVE_EXTERNAL_STATE";
  }
  return "UNKNOWN";
}

bool is_valid(PortMutationKind kind) noexcept {
  return static_cast<std::uint8_t>(kind) <=
         static_cast<std::uint8_t>(PortMutationKind::ObserveExternalState);
}

std::string_view to_string(MessageType type) noexcept {
  switch (type) {
    case MessageType::Hello:
      return "HELLO";
    case MessageType::HelloAck:
      return "HELLO_ACK";
    case MessageType::RegisterPublisher:
      return "REGISTER_PUBLISHER";
    case MessageType::RegisterPublisherAck:
      return "REGISTER_PUBLISHER_ACK";
    case MessageType::Mutation:
      return "MUTATION";
    case MessageType::MutationAck:
      return "MUTATION_ACK";
    case MessageType::Query:
      return "QUERY";
    case MessageType::QueryAck:
      return "QUERY_ACK";
    case MessageType::FencePublisher:
      return "FENCE_PUBLISHER";
    case MessageType::FencePublisherAck:
      return "FENCE_PUBLISHER_ACK";
    case MessageType::Heartbeat:
      return "HEARTBEAT";
    case MessageType::HeartbeatAck:
      return "HEARTBEAT_ACK";
    case MessageType::Shutdown:
      return "SHUTDOWN";
    case MessageType::ErrorResponse:
      return "ERROR_RESPONSE";
  }
  return "UNKNOWN";
}

bool is_valid(MessageType type) noexcept {
  switch (type) {
    case MessageType::Hello:
    case MessageType::HelloAck:
    case MessageType::RegisterPublisher:
    case MessageType::RegisterPublisherAck:
    case MessageType::Mutation:
    case MessageType::MutationAck:
    case MessageType::Query:
    case MessageType::QueryAck:
    case MessageType::FencePublisher:
    case MessageType::FencePublisherAck:
    case MessageType::Heartbeat:
    case MessageType::HeartbeatAck:
    case MessageType::Shutdown:
    case MessageType::ErrorResponse:
      return true;
  }
  return false;
}

std::string_view frame_magic() noexcept { return std::string_view(kMagic, sizeof(kMagic)); }

Outcome encode_frame(const Frame& frame, std::string& out) {
  out.clear();
  if (!is_valid(frame.type)) {
    return Outcome::failure(OutcomeCode::MalformedRequest,
                            "the message type is not a legal protocol message type");
  }
  if (frame.payload.size() > kPayloadLimit) {
    return Outcome::failure(OutcomeCode::ResourceBoundExceeded,
                            "the frame payload exceeds the configured frame bound");
  }
  std::string header;
  {
    ByteWriter writer(header, frame_header_size);
    writer.raw(std::string_view(kMagic, sizeof(kMagic)));
    writer.u16(static_cast<std::uint16_t>(kProtocolVersion));
    writer.u16(static_cast<std::uint16_t>(frame.type));
    writer.u16(frame.flags);
    writer.u16(0);
    writer.u64(frame.sequence);
    writer.u32(static_cast<std::uint32_t>(frame.payload.size()));
    writer.u32(detail::crc32(frame.payload));
    if (!writer.ok()) {
      return Outcome::failure(OutcomeCode::InternalError, "the frame header could not be built");
    }
    writer.u32(detail::crc32(header));
    if (!writer.ok()) {
      return Outcome::failure(OutcomeCode::InternalError, "the frame header could not be built");
    }
  }
  out.reserve(header.size() + frame.payload.size());
  out.append(header);
  out.append(frame.payload);
  return Outcome::success("frame encoded");
}

Outcome decode_frame_header(std::string_view header, FrameHeader& out) {
  if (header.size() != frame_header_size) {
    return Outcome::failure(OutcomeCode::TransportFailure,
                            "the frame header is not the expected size");
  }
  if (std::memcmp(header.data(), kMagic, sizeof(kMagic)) != 0) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the frame magic does not match");
  }
  ByteReader reader(header);
  std::string magic;
  std::uint16_t version = 0;
  std::uint16_t type = 0;
  std::uint16_t flags = 0;
  std::uint16_t reserved = 0;
  std::uint64_t sequence = 0;
  std::uint32_t payload_length = 0;
  std::uint32_t payload_crc = 0;
  std::uint32_t header_crc = 0;
  if (!reader.raw(sizeof(kMagic), magic) || !reader.u16(version) || !reader.u16(type) ||
      !reader.u16(flags) || !reader.u16(reserved) || !reader.u64(sequence) ||
      !reader.u32(payload_length) || !reader.u32(payload_crc) || !reader.u32(header_crc)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the frame header is truncated");
  }
  if (detail::crc32(header.substr(0, frame_header_size - 4)) != header_crc) {
    return Outcome::failure(OutcomeCode::TransportFailure,
                            "the frame header integrity check failed");
  }
  if (version != kProtocolVersion) {
    return Outcome::failure(OutcomeCode::TransportFailure,
                            "the frame protocol version is not supported by this build");
  }
  if (!is_valid(static_cast<MessageType>(type))) {
    return Outcome::failure(OutcomeCode::TransportFailure,
                            "the frame carries an unknown message type");
  }
  if (reserved != 0) {
    return Outcome::failure(OutcomeCode::TransportFailure,
                            "the frame carries non-zero reserved bits");
  }
  if (payload_length > kPayloadLimit) {
    return Outcome::failure(OutcomeCode::ResourceBoundExceeded,
                            "the frame payload length exceeds the configured frame bound");
  }
  out.version = version;
  out.type = static_cast<MessageType>(type);
  out.flags = flags;
  out.sequence = sequence;
  out.payload_length = payload_length;
  out.payload_crc = payload_crc;
  return Outcome::success("frame header accepted");
}

Outcome decode_frame(std::string_view bytes, Frame& out) {
  if (bytes.size() < frame_header_size) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the frame is shorter than its header");
  }
  FrameHeader header;
  if (const Outcome decoded = decode_frame_header(bytes.substr(0, frame_header_size), header);
      !decoded.ok()) {
    return decoded;
  }
  if (bytes.size() != frame_header_size + header.payload_length) {
    return Outcome::failure(OutcomeCode::TransportFailure,
                            "the frame length does not match the declared payload length");
  }
  const std::string_view payload = bytes.substr(frame_header_size);
  if (detail::crc32(payload) != header.payload_crc) {
    return Outcome::failure(OutcomeCode::TransportFailure,
                            "the frame payload integrity check failed");
  }
  out.type = header.type;
  out.flags = header.flags;
  out.sequence = header.sequence;
  out.payload.assign(payload);
  return Outcome::success("frame decoded");
}

std::string PortMutationMessage::to_string() const {
  std::string out("kind=");
  detail::append_u64(out, kind);
  out.append(" port=");
  out.append(port.valid() ? port.to_string() : std::string("none"));
  out.append(" authority=[");
  out.append(authority.to_string());
  out.push_back(']');
  return out;
}

std::string_view to_string(QueryKind kind) noexcept {
  switch (kind) {
    case QueryKind::Port:
      return "PORT";
    case QueryKind::Configuration:
      return "CONFIGURATION";
    case QueryKind::Lifecycle:
      return "LIFECYCLE";
    case QueryKind::AdministrativeState:
      return "ADMINISTRATIVE_STATE";
    case QueryKind::Ownership:
      return "OWNERSHIP";
    case QueryKind::CapabilityBinding:
      return "CAPABILITY_BINDING";
    case QueryKind::AppliedEvidence:
      return "APPLIED_EVIDENCE";
    case QueryKind::PortsOfDevice:
      return "PORTS_OF_DEVICE";
    case QueryKind::PortsWithLifecycle:
      return "PORTS_WITH_LIFECYCLE";
    case QueryKind::PortsRequiringRevalidation:
      return "PORTS_REQUIRING_REVALIDATION";
    case QueryKind::PortsOfOwner:
      return "PORTS_OF_OWNER";
    case QueryKind::PortsByProfile:
      return "PORTS_BY_PROFILE";
    case QueryKind::DerivedPorts:
      return "DERIVED_PORTS";
    case QueryKind::StaleProfileBindings:
      return "STALE_PROFILE_BINDINGS";
    case QueryKind::Explain:
      return "EXPLAIN";
    case QueryKind::Snapshot:
      return "SNAPSHOT";
    case QueryKind::EngineInfo:
      return "ENGINE_INFO";
    case QueryKind::Profiles:
      return "PROFILES";
    case QueryKind::Devices:
      return "DEVICES";
  }
  return "UNKNOWN";
}

bool parse_query_kind(std::string_view text, QueryKind& out) noexcept {
  for (std::uint8_t index = 0; index <= static_cast<std::uint8_t>(QueryKind::Devices); ++index) {
    const auto kind = static_cast<QueryKind>(index);
    if (portfabric::to_string(kind) == text) {
      out = kind;
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Message codecs
// ---------------------------------------------------------------------------

Outcome encode_hello(const HelloMessage& message, std::string& out) {
  out.clear();
  ByteWriter writer(out, kPayloadLimit);
  writer.u16(message.protocol_version);
  detail::write_id(writer, message.publisher.to_string());
  detail::write_id(writer, message.boot.to_string());
  writer.u64(message.epoch.value());
  writer.text(message.role, max_name_length);
  if (!writer.ok()) {
    return Outcome::failure(OutcomeCode::MalformedRequest, "the hello message could not be encoded");
  }
  return Outcome::success("encoded");
}

Outcome decode_hello(std::string_view bytes, HelloMessage& out) {
  ByteReader reader(bytes);
  if (!reader.u16(out.protocol_version)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the hello message is malformed");
  }
  if (out.protocol_version != kProtocolVersion) {
    return Outcome::failure(OutcomeCode::TransportFailure,
                            "the peer speaks an unsupported protocol version");
  }
  if (!detail::read_strong_id(reader, out.publisher) ||
      !detail::read_strong_id(reader, out.boot)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the hello message is malformed");
  }
  std::uint64_t epoch = 0;
  if (!reader.u64(epoch) || !reader.text(max_name_length, out.role)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the hello message is malformed");
  }
  out.epoch = CoordinatorEpoch::from_value(epoch);
  if (!reader.exhausted()) {
    return Outcome::failure(OutcomeCode::TransportFailure,
                            "the hello message carries trailing bytes");
  }
  return Outcome::success("decoded");
}

Outcome encode_hello_ack(const HelloAckMessage& message, std::string& out) {
  out.clear();
  ByteWriter writer(out, kPayloadLimit);
  writer.boolean(message.accepted);
  writer.u64(message.epoch.value());
  writer.text(message.detail, max_description_length);
  if (!writer.ok()) {
    return Outcome::failure(OutcomeCode::MalformedRequest, "the hello acknowledgement is too large");
  }
  return Outcome::success("encoded");
}

Outcome decode_hello_ack(std::string_view bytes, HelloAckMessage& out) {
  ByteReader reader(bytes);
  std::uint64_t epoch = 0;
  if (!reader.boolean(out.accepted) || !reader.u64(epoch) ||
      !reader.text(max_description_length, out.detail)) {
    return Outcome::failure(OutcomeCode::TransportFailure,
                            "the hello acknowledgement is malformed");
  }
  out.epoch = CoordinatorEpoch::from_value(epoch);
  return Outcome::success("decoded");
}

Outcome encode_register_publisher(const RegisterPublisherMessage& message, std::string& out) {
  out.clear();
  ByteWriter writer(out, kPayloadLimit);
  detail::write_id(writer, message.publisher.to_string());
  detail::write_id(writer, message.boot.to_string());
  writer.u64(message.epoch.value());
  if (!writer.ok()) {
    return Outcome::failure(OutcomeCode::MalformedRequest,
                            "the publisher registration could not be encoded");
  }
  return Outcome::success("encoded");
}

Outcome decode_register_publisher(std::string_view bytes, RegisterPublisherMessage& out) {
  ByteReader reader(bytes);
  std::uint64_t epoch = 0;
  if (!detail::read_strong_id(reader, out.publisher) ||
      !detail::read_strong_id(reader, out.boot) || !reader.u64(epoch)) {
    return Outcome::failure(OutcomeCode::TransportFailure,
                            "the publisher registration is malformed");
  }
  out.epoch = CoordinatorEpoch::from_value(epoch);
  return Outcome::success("decoded");
}

Outcome encode_fence(const FenceMessage& message, std::string& out) {
  out.clear();
  ByteWriter writer(out, kPayloadLimit);
  detail::write_id(writer, message.publisher.to_string());
  detail::write_id(writer, message.boot.to_string());
  writer.text(message.detail, max_description_length);
  if (!writer.ok()) {
    return Outcome::failure(OutcomeCode::MalformedRequest, "the fence message could not be encoded");
  }
  return Outcome::success("encoded");
}

Outcome decode_fence(std::string_view bytes, FenceMessage& out) {
  ByteReader reader(bytes);
  if (!detail::read_strong_id(reader, out.publisher) ||
      !detail::read_strong_id(reader, out.boot) ||
      !reader.text(max_description_length, out.detail)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the fence message is malformed");
  }
  return Outcome::success("decoded");
}

Outcome encode_mutation(const PortMutationMessage& message, std::string& out) {
  out.clear();
  ByteWriter writer(out, kPayloadLimit);
  writer.u8(message.kind);
  detail::write_authority(writer, message.authority);
  detail::write_id(writer, message.port.to_string());
  detail::write_id(writer, message.parent_device.to_string());
  writer.u64(message.device_generation.value());
  writer.boolean(message.has_configuration);
  if (message.has_configuration) {
    detail::write_configuration(writer, message.configuration);
  }
  writer.u8(message.administrative_op);
  detail::write_id(writer, message.profile.to_string());
  writer.u64(message.profile_generation.value());
  writer.boolean(message.release);
  detail::write_capability_binding(writer, message.capability);
  writer.text(message.detail, max_description_length);
  writer.u8(message.owner_kind);
  detail::write_id(writer, message.owner.to_string());
  writer.boolean(message.exclusive);
  detail::write_applied(writer, message.evidence);
  writer.u8(message.drift);
  writer.u8(message.provenance_kind);
  writer.text(message.provenance_source, max_name_length);
  if (!writer.ok()) {
    return Outcome::failure(OutcomeCode::MalformedRequest,
                            "the mutation message exceeds the frame bound");
  }
  return Outcome::success("encoded");
}

Outcome decode_mutation(std::string_view bytes, PortMutationMessage& out) {
  ByteReader reader(bytes);
  if (!reader.u8(out.kind)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the mutation message is malformed");
  }
  if (!detail::read_authority(reader, out.authority)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the authority context is malformed");
  }
  if (!detail::read_strong_id(reader, out.port)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the port identity is malformed");
  }
  if (!detail::read_strong_id(reader, out.parent_device)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the parent device is malformed");
  }
  std::uint64_t value = 0;
  if (!reader.u64(value)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the mutation message is malformed");
  }
  out.device_generation = DeviceGeneration::from_value(value);
  if (!reader.boolean(out.has_configuration)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the mutation message is malformed");
  }
  if (out.has_configuration) {
    if (!detail::read_configuration(reader, out.configuration)) {
      return Outcome::failure(OutcomeCode::TransportFailure, "the configuration is malformed");
    }
  }
  if (!reader.u8(out.administrative_op)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the mutation message is malformed");
  }
  if (!detail::read_strong_id(reader, out.profile)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the profile identity is malformed");
  }
  if (!reader.u64(value)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the mutation message is malformed");
  }
  out.profile_generation = PortProfileGeneration::from_value(value);
  if (!reader.boolean(out.release)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the mutation message is malformed");
  }
  if (!detail::read_capability_binding(reader, out.capability)) {
    return Outcome::failure(OutcomeCode::TransportFailure,
                            "the capability binding is malformed");
  }
  if (!reader.text(max_description_length, out.detail)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the mutation message is malformed");
  }
  if (!reader.u8(out.owner_kind)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the mutation message is malformed");
  }
  if (!detail::read_strong_id(reader, out.owner)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the owner identity is malformed");
  }
  if (!reader.boolean(out.exclusive)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the mutation message is malformed");
  }
  if (!detail::read_applied(reader, out.evidence)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the applied evidence is malformed");
  }
  if (!reader.u8(out.drift)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the mutation message is malformed");
  }
  if (!reader.u8(out.provenance_kind) ||
      !reader.text(max_name_length, out.provenance_source)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the mutation message is malformed");
  }
  if (!reader.exhausted()) {
    return Outcome::failure(OutcomeCode::TransportFailure,
                            "the mutation message carries trailing bytes");
  }
  return Outcome::success("decoded");
}

Outcome encode_query(const QueryMessage& message, std::string& out) {
  out.clear();
  ByteWriter writer(out, kPayloadLimit);
  writer.u8(static_cast<std::uint8_t>(message.kind));
  detail::write_id(writer, message.port.to_string());
  detail::write_id(writer, message.device.to_string());
  detail::write_id(writer, message.owner.to_string());
  detail::write_id(writer, message.profile.to_string());
  writer.u8(static_cast<std::uint8_t>(message.lifecycle));
  writer.u8(static_cast<std::uint8_t>(message.administrative));
  if (!writer.ok()) {
    return Outcome::failure(OutcomeCode::MalformedRequest, "the query message could not be encoded");
  }
  return Outcome::success("encoded");
}

Outcome decode_query(std::string_view bytes, QueryMessage& out) {
  ByteReader reader(bytes);
  std::uint8_t kind = 0;
  if (!reader.u8(kind)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the query message is malformed");
  }
  if (kind > static_cast<std::uint8_t>(QueryKind::Devices)) {
    return Outcome::failure(OutcomeCode::MalformedRequest, "the query selector is not a legal value");
  }
  out.kind = static_cast<QueryKind>(kind);
  if (!detail::read_strong_id(reader, out.port) || !detail::read_strong_id(reader, out.device) ||
      !detail::read_strong_id(reader, out.owner) || !detail::read_strong_id(reader, out.profile)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the query message is malformed");
  }
  std::uint8_t lifecycle = 0;
  std::uint8_t administrative = 0;
  if (!reader.u8(lifecycle) || !reader.u8(administrative)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the query message is malformed");
  }
  if (!is_valid(static_cast<PortLifecycle>(lifecycle)) ||
      !is_valid(static_cast<AdministrativeState>(administrative))) {
    return Outcome::failure(OutcomeCode::MalformedRequest,
                            "the query carries an invalid lifecycle or administrative state");
  }
  out.lifecycle = static_cast<PortLifecycle>(lifecycle);
  out.administrative = static_cast<AdministrativeState>(administrative);
  return Outcome::success("decoded");
}

Outcome encode_query_response(const QueryResponse& message, std::string& out) {
  out.clear();
  ByteWriter writer(out, kPayloadLimit);
  write_outcome(writer, message.outcome);
  writer.u8(static_cast<std::uint8_t>(message.kind));
  if (message.ports.size() > max_snapshot_ports) {
    return Outcome::failure(OutcomeCode::ResourceBoundExceeded,
                            "the query response exceeds the configured port bound");
  }
  writer.u32(static_cast<std::uint32_t>(message.ports.size()));
  for (const PortId& port : message.ports) {
    detail::write_id(writer, port.to_string());
  }
  writer.u32(static_cast<std::uint32_t>(message.records.size()));
  for (const PortRecord& record : message.records) {
    detail::write_record(writer, record);
  }
  writer.u32(static_cast<std::uint32_t>(message.profiles.size()));
  for (const PortProfile& profile : message.profiles) {
    detail::write_profile(writer, profile);
  }
  writer.u32(static_cast<std::uint32_t>(message.devices.size()));
  for (const DeviceBinding& device : message.devices) {
    detail::write_device(writer, device);
  }
  writer.text(message.text, max_query_text_bytes);
  writer.u64(message.generation.value());
  writer.u64(message.epoch.value());
  writer.text(message.digest.to_hex(), 64);
  if (!writer.ok()) {
    return Outcome::failure(OutcomeCode::ResourceBoundExceeded,
                            "the query response exceeds the configured frame bound");
  }
  return Outcome::success("encoded");
}

Outcome decode_query_response(std::string_view bytes, QueryResponse& out) {
  ByteReader reader(bytes);
  if (!read_outcome(reader, out.outcome)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the query response outcome is malformed");
  }
  std::uint8_t kind = 0;
  if (!reader.u8(kind) || kind > static_cast<std::uint8_t>(QueryKind::Devices)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the query response outcome is malformed");
  }
  out.kind = static_cast<QueryKind>(kind);
  std::uint32_t count = 0;
  if (!detail::read_bounded_count(reader, max_snapshot_ports, count)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the query response outcome is malformed");
  }
  out.ports.clear();
  out.ports.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    PortId port;
    if (!detail::read_strong_id(reader, port)) {
      return Outcome::failure(OutcomeCode::TransportFailure, "a port identity is malformed");
    }
    out.ports.push_back(port);
  }
  if (!detail::read_bounded_count(reader, max_snapshot_ports, count)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the query response outcome is malformed");
  }
  out.records.clear();
  out.records.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    PortRecord record;
    if (!detail::read_record(reader, record)) {
      return Outcome::failure(OutcomeCode::TransportFailure, "a port record is malformed");
    }
    out.records.push_back(std::move(record));
  }
  if (!detail::read_bounded_count(reader, max_profiles, count)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the query response outcome is malformed");
  }
  out.profiles.clear();
  out.profiles.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    PortProfile profile;
    if (!detail::read_profile(reader, profile)) {
      return Outcome::failure(OutcomeCode::TransportFailure, "a profile is malformed");
    }
    out.profiles.push_back(std::move(profile));
  }
  if (!detail::read_bounded_count(reader, max_devices, count)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the query response outcome is malformed");
  }
  out.devices.clear();
  out.devices.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    DeviceBinding device;
    if (!detail::read_device(reader, device)) {
      return Outcome::failure(OutcomeCode::TransportFailure, "a device binding is malformed");
    }
    out.devices.push_back(std::move(device));
  }
  if (!reader.text(max_query_text_bytes, out.text)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the query response outcome is malformed");
  }
  std::uint64_t value = 0;
  if (!reader.u64(value)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the query response outcome is malformed");
  }
  out.generation = EngineGeneration::from_value(value);
  if (!reader.u64(value)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the query response outcome is malformed");
  }
  out.epoch = CoordinatorEpoch::from_value(value);
  std::string digest_text;
  if (!reader.text(64, digest_text)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the query response outcome is malformed");
  }
  if (digest_text.size() != 64) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the response digest is malformed");
  }
  std::array<std::byte, 32> digest_bytes_value{};
  for (std::size_t index = 0; index < 32; ++index) {
    const auto hex_value = [](char ch) -> int {
      if (ch >= '0' && ch <= '9') {
        return ch - '0';
      }
      if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
      }
      return -1;
    };
    const int high = hex_value(digest_text[index * 2]);
    const int low = hex_value(digest_text[index * 2 + 1]);
    if (high < 0 || low < 0) {
      return Outcome::failure(OutcomeCode::TransportFailure, "the response digest is malformed");
    }
    digest_bytes_value[index] = static_cast<std::byte>((high << 4) | low);
  }
  out.digest = digest_from_bytes(digest_bytes_value);
  if (!reader.exhausted()) {
    return Outcome::failure(OutcomeCode::TransportFailure,
                            "the query response carries trailing bytes");
  }
  return Outcome::success("decoded");
}

Outcome encode_outcome(const Outcome& outcome, std::string& out) {
  out.clear();
  ByteWriter writer(out, kPayloadLimit);
  write_outcome(writer, outcome);
  if (!writer.ok()) {
    return Outcome::failure(OutcomeCode::MalformedRequest, "the outcome could not be encoded");
  }
  return Outcome::success("encoded");
}

Outcome decode_outcome(std::string_view bytes, Outcome& out) {
  ByteReader reader(bytes);
  if (!read_outcome(reader, out)) {
    return Outcome::failure(OutcomeCode::TransportFailure, "the outcome is malformed");
  }
  return Outcome::success("decoded");
}

}  // namespace portfabric

