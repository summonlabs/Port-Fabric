#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "portfabric/export.hpp"
#include "portfabric/limits.hpp"

namespace portfabric {

/// Result of validating an encoded identifier.
enum class IdValidation : std::uint8_t {
  Ok = 0,
  Empty,
  TooLong,
  InvalidCharacter,
  LeadingSeparator,
  TrailingSeparator,
  DotDot,
};

/// Stable diagnostic rendering of an identifier validation result.
PF_EXPORT std::string_view to_string(IdValidation validation) noexcept;

/// Validates an encoded identifier.
///
/// Accepted form: 1..max_identifier_length bytes over [A-Za-z0-9], where the
/// first and last byte are alphanumeric and the interior may additionally use
/// '-', '_', '.', ':' and '/'. The two byte sequence ".." is always rejected so
/// that identifiers can never be interpreted as relative path traversal.
PF_EXPORT IdValidation validate_identifier(std::string_view value) noexcept;

/// Strongly typed identifier.
///
/// An identifier carries a domain tag, so identifiers from different domains
/// never convert into each other implicitly. A default constructed identifier is
/// the null identifier: it is not valid, compares equal only to other null
/// identifiers, and is rejected by every mutating request.
template <class Tag>
class StrongId {
 public:
  using tag_type = Tag;

  constexpr StrongId() noexcept = default;

  /// Builds an identifier from an already validated encoding. Invalid input
  /// yields the null identifier rather than a partially valid identifier.
  static StrongId from_validated(std::string value) {
    if (validate_identifier(value) != IdValidation::Ok) {
      return StrongId{};
    }
    StrongId result;
    result.hash_ = hash_of(value);
    result.value_ = std::move(value);
    return result;
  }

  /// Parses an encoded identifier, returning nullopt when malformed.
  static std::optional<StrongId> parse(std::string_view value) {
    if (validate_identifier(value) != IdValidation::Ok) {
      return std::nullopt;
    }
    return from_validated(std::string(value));
  }

  /// Parses an encoded identifier, reporting why malformed input was rejected.
  static std::optional<StrongId> parse(std::string_view value, IdValidation& reason) {
    reason = validate_identifier(value);
    if (reason != IdValidation::Ok) {
      return std::nullopt;
    }
    return from_validated(std::string(value));
  }

  bool valid() const noexcept { return !value_.empty(); }
  explicit operator bool() const noexcept { return valid(); }

  const std::string& value() const noexcept { return value_; }
  const char* c_str() const noexcept { return value_.c_str(); }
  std::string to_string() const { return value_; }
  std::uint64_t hash() const noexcept { return hash_; }

  friend bool operator==(const StrongId& lhs, const StrongId& rhs) noexcept {
    return lhs.value_ == rhs.value_;
  }
  friend bool operator!=(const StrongId& lhs, const StrongId& rhs) noexcept { return !(lhs == rhs); }
  friend bool operator<(const StrongId& lhs, const StrongId& rhs) noexcept {
    return lhs.value_ < rhs.value_;
  }
  friend bool operator>(const StrongId& lhs, const StrongId& rhs) noexcept { return rhs < lhs; }
  friend bool operator<=(const StrongId& lhs, const StrongId& rhs) noexcept { return !(rhs < lhs); }
  friend bool operator>=(const StrongId& lhs, const StrongId& rhs) noexcept { return !(lhs < rhs); }

 private:
  static std::uint64_t hash_of(const std::string& value) noexcept {
    // FNV-1a over the validated encoding: deterministic and stable across
    // processes, unlike salted standard library hash implementations.
    std::uint64_t hash = 1469598103934665603ull;
    for (const char ch : value) {
      hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ch));
      hash *= 1099511628211ull;
    }
    return hash == 0 ? 1ull : hash;
  }

  std::string value_;
  std::uint64_t hash_ = 0;
};

namespace tags {
struct FabricTag {};
struct SiteTag {};
struct DeviceTag {};
struct SwitchTag {};
struct RouterTag {};
struct NicTag {};
struct SmartNicTag {};
struct DpuTag {};
struct PortTag {};
struct LogicalPortTag {};
struct ControlParticipantTag {};
struct PortConfigurationTag {};
struct PortProfileTag {};
struct PortOwnershipTag {};
struct OwnerTag {};
struct MutationAttemptTag {};
struct PublicationTag {};
struct PublisherTag {};
struct WorkerBootTag {};
struct SnapshotTag {};
struct EvidenceTag {};
struct AggregateTag {};
struct TransceiverTag {};
struct SessionTag {};
}  // namespace tags

using FabricId = StrongId<tags::FabricTag>;
using SiteId = StrongId<tags::SiteTag>;
using DeviceId = StrongId<tags::DeviceTag>;
using SwitchId = StrongId<tags::SwitchTag>;
using RouterId = StrongId<tags::RouterTag>;
using NicId = StrongId<tags::NicTag>;
using SmartNicId = StrongId<tags::SmartNicTag>;
using DpuId = StrongId<tags::DpuTag>;
using PortId = StrongId<tags::PortTag>;
using LogicalPortId = StrongId<tags::LogicalPortTag>;
using ControlParticipantId = StrongId<tags::ControlParticipantTag>;
using PortConfigurationId = StrongId<tags::PortConfigurationTag>;
using PortProfileId = StrongId<tags::PortProfileTag>;
using PortOwnershipId = StrongId<tags::PortOwnershipTag>;
using OwnerId = StrongId<tags::OwnerTag>;
using MutationAttemptId = StrongId<tags::MutationAttemptTag>;
using PublicationId = StrongId<tags::PublicationTag>;
using PublisherId = StrongId<tags::PublisherTag>;
using WorkerBootId = StrongId<tags::WorkerBootTag>;
using SnapshotId = StrongId<tags::SnapshotTag>;
using EvidenceId = StrongId<tags::EvidenceTag>;
using AggregateId = StrongId<tags::AggregateTag>;
using TransceiverId = StrongId<tags::TransceiverTag>;
using SessionId = StrongId<tags::SessionTag>;

/// Structural class of the canonical port entity.
///
/// Port Fabric consumes the entity class from Fabric Registry and rejects a
/// request that addresses a port through the wrong class.
enum class PortEntityClass : std::uint8_t {
  Unknown = 0,
  SwitchPort,
  RouterPort,
  NicPort,
  SmartNicPort,
  DpuPort,
  LogicalPort,
  SyntheticPort,
};

PF_EXPORT std::string_view to_string(PortEntityClass entity_class) noexcept;
PF_EXPORT bool parse_entity_class(std::string_view text, PortEntityClass& out) noexcept;
PF_EXPORT bool is_valid(PortEntityClass entity_class) noexcept;

/// Identity of a port exactly as Fabric Registry defines it.
///
/// Port Fabric never invents port identity. It consumes the canonical PortId and
/// records its own configuration record identity separately, so that "the port
/// exists" and "a configuration record exists for the port" stay distinguishable.
struct CanonicalPortReference {
  PortId port;
  DeviceId parent;
  PortEntityClass entity_class = PortEntityClass::Unknown;

  friend bool operator==(const CanonicalPortReference& lhs,
                         const CanonicalPortReference& rhs) noexcept {
    return lhs.port == rhs.port && lhs.parent == rhs.parent &&
           lhs.entity_class == rhs.entity_class;
  }
};

}  // namespace portfabric

namespace std {

template <class Tag>
struct hash<portfabric::StrongId<Tag>> {
  std::size_t operator()(const portfabric::StrongId<Tag>& id) const noexcept {
    return static_cast<std::size_t>(id.hash());
  }
};

}  // namespace std
