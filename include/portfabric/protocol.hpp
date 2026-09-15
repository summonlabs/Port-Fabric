#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "portfabric/adapter.hpp"
#include "portfabric/authority.hpp"
#include "portfabric/capability.hpp"
#include "portfabric/configuration.hpp"
#include "portfabric/digest.hpp"
#include "portfabric/export.hpp"
#include "portfabric/ids.hpp"
#include "portfabric/limits.hpp"
#include "portfabric/outcome.hpp"
#include "portfabric/persistence.hpp"
#include "portfabric/profile.hpp"
#include "portfabric/record.hpp"
#include "portfabric/version.hpp"

namespace portfabric {

/// Mutation discriminator carried by a mutation message.
///
/// The value is the wire discriminator and selects which request members are
/// meaningful, exactly as the typed in-process request types do.
enum class PortMutationKind : std::uint8_t {
  BindPort = 0,
  Configure,
  Reconfigure,
  Administrative,
  AssignProfile,
  BindCapabilities,
  Reconcile,
  Supersede,
  Retire,
  ClaimOwnership,
  TransferOwnership,
  ReleaseOwnership,
  RecordAppliedEvidence,
  ObserveExternalState,
};

PF_EXPORT std::string_view to_string(PortMutationKind kind) noexcept;
PF_EXPORT bool is_valid(PortMutationKind kind) noexcept;

/// Control protocol message types.
///
/// The wire form is a fixed 32 byte frame header followed by a bounded payload.
/// Every frame carries its protocol version, its type, a sequence number and a
/// CRC-32 over both the header and the payload, so a malformed, truncated or
/// corrupted frame is rejected before any decoding of message content happens.
enum class MessageType : std::uint16_t {
  Hello = 1,
  HelloAck = 2,
  RegisterPublisher = 3,
  RegisterPublisherAck = 4,
  Mutation = 5,
  MutationAck = 6,
  Query = 7,
  QueryAck = 8,
  FencePublisher = 9,
  FencePublisherAck = 10,
  Heartbeat = 11,
  HeartbeatAck = 12,
  Shutdown = 13,
  ErrorResponse = 14,
};

PF_EXPORT std::string_view to_string(MessageType type) noexcept;
PF_EXPORT bool is_valid(MessageType type) noexcept;

/// Size of the fixed frame header in bytes.
inline constexpr std::size_t frame_header_size = 32;

/// Decoded frame header.
struct PF_EXPORT FrameHeader {
  std::uint16_t version = 0;
  MessageType type = MessageType::Hello;
  std::uint16_t flags = 0;
  std::uint64_t sequence = 0;
  std::uint32_t payload_length = 0;
  std::uint32_t payload_crc = 0;
};

/// A complete protocol frame.
struct PF_EXPORT Frame {
  MessageType type = MessageType::Hello;
  std::uint16_t flags = 0;
  std::uint64_t sequence = 0;
  std::string payload;
};

/// Encodes a frame header and payload into its wire form.
PF_EXPORT Outcome encode_frame(const Frame& frame, std::string& out);

/// Decodes a complete frame, validating magic, version, type, length and both
/// integrity checks.
PF_EXPORT Outcome decode_frame(std::string_view bytes, Frame& out);

/// Decodes and validates a frame header without touching the payload.
PF_EXPORT Outcome decode_frame_header(std::string_view header, FrameHeader& out);

/// The four byte frame magic.
PF_EXPORT std::string_view frame_magic() noexcept;

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------

/// Opening message of a session.
struct PF_EXPORT HelloMessage {
  std::uint16_t protocol_version = kProtocolVersion;
  PublisherId publisher;
  WorkerBootId boot;
  CoordinatorEpoch epoch;
  /// Bounded role label of the connecting process.
  std::string role;
};

struct PF_EXPORT HelloAckMessage {
  bool accepted = false;
  CoordinatorEpoch epoch;
  std::string detail;
};

struct PF_EXPORT RegisterPublisherMessage {
  PublisherId publisher;
  WorkerBootId boot;
  CoordinatorEpoch epoch;
};

struct PF_EXPORT FenceMessage {
  PublisherId publisher;
  WorkerBootId boot;
  std::string detail;
};

/// One mutation request. The kind selects which payload members are meaningful,
/// exactly as the in-process request types do.
struct PF_EXPORT PortMutationMessage {
  std::uint8_t kind = 0;
  AuthorityContext authority;
  PortId port;
  /// Parent device and device generation used by an identity binding request.
  DeviceId parent_device;
  DeviceGeneration device_generation;
  /// True when the message carries a complete configuration payload, which is
  /// required by configuration and profile mutations.
  bool has_configuration = false;
  PortConfiguration configuration;
  std::uint8_t administrative_op = 0;
  PortProfileId profile;
  PortProfileGeneration profile_generation;
  bool release = false;
  CapabilityBinding capability;
  std::string detail;
  std::uint8_t owner_kind = 0;
  OwnerId owner;
  bool exclusive = true;
  AppliedEvidence evidence;
  std::uint8_t drift = 0;
  std::uint8_t provenance_kind = 0;
  std::string provenance_source;

  std::string to_string() const;
};

/// Query selector.
enum class QueryKind : std::uint8_t {
  Port = 0,
  Configuration,
  Lifecycle,
  AdministrativeState,
  Ownership,
  CapabilityBinding,
  AppliedEvidence,
  PortsOfDevice,
  PortsWithLifecycle,
  PortsRequiringRevalidation,
  PortsOfOwner,
  PortsByProfile,
  DerivedPorts,
  StaleProfileBindings,
  Explain,
  Snapshot,
  EngineInfo,
  Profiles,
  Devices,
};

PF_EXPORT std::string_view to_string(QueryKind kind) noexcept;
PF_EXPORT bool parse_query_kind(std::string_view text, QueryKind& out) noexcept;

struct PF_EXPORT QueryMessage {
  QueryKind kind = QueryKind::Port;
  PortId port;
  DeviceId device;
  OwnerId owner;
  PortProfileId profile;
  PortLifecycle lifecycle = PortLifecycle::Unconfigured;
  AdministrativeState administrative = AdministrativeState::Unknown;
};

struct PF_EXPORT QueryResponse {
  Outcome outcome;
  QueryKind kind = QueryKind::Port;
  std::vector<PortId> ports;
  std::vector<PortRecord> records;
  std::vector<PortProfile> profiles;
  std::vector<DeviceBinding> devices;
  std::string text;
  EngineGeneration generation;
  CoordinatorEpoch epoch;
  Digest digest;
};

// ---------------------------------------------------------------------------
// Message codecs
// ---------------------------------------------------------------------------

PF_EXPORT Outcome encode_hello(const HelloMessage& message, std::string& out);
PF_EXPORT Outcome decode_hello(std::string_view bytes, HelloMessage& out);
PF_EXPORT Outcome encode_hello_ack(const HelloAckMessage& message, std::string& out);
PF_EXPORT Outcome decode_hello_ack(std::string_view bytes, HelloAckMessage& out);
PF_EXPORT Outcome encode_register_publisher(const RegisterPublisherMessage& message,
                                            std::string& out);
PF_EXPORT Outcome decode_register_publisher(std::string_view bytes,
                                            RegisterPublisherMessage& out);
PF_EXPORT Outcome encode_fence(const FenceMessage& message, std::string& out);
PF_EXPORT Outcome decode_fence(std::string_view bytes, FenceMessage& out);
PF_EXPORT Outcome encode_mutation(const PortMutationMessage& message, std::string& out);
PF_EXPORT Outcome decode_mutation(std::string_view bytes, PortMutationMessage& out);
PF_EXPORT Outcome encode_query(const QueryMessage& message, std::string& out);
PF_EXPORT Outcome decode_query(std::string_view bytes, QueryMessage& out);
PF_EXPORT Outcome encode_query_response(const QueryResponse& message, std::string& out);
PF_EXPORT Outcome decode_query_response(std::string_view bytes, QueryResponse& out);
PF_EXPORT Outcome encode_outcome(const Outcome& outcome, std::string& out);
PF_EXPORT Outcome decode_outcome(std::string_view bytes, Outcome& out);

}  // namespace portfabric
