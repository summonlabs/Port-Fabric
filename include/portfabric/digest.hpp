#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "portfabric/export.hpp"
#include "portfabric/configuration.hpp"
#include "portfabric/record.hpp"

namespace portfabric {

/// Deterministic 256-bit content digest.
class PF_EXPORT Digest {
 public:
  static Digest zero();

  bool is_zero() const noexcept;
  std::string to_hex() const;

  friend bool operator==(const Digest& lhs, const Digest& rhs) noexcept {
    return lhs.bytes_ == rhs.bytes_;
  }
  friend bool operator!=(const Digest& lhs, const Digest& rhs) noexcept { return !(lhs == rhs); }
  friend bool operator<(const Digest& lhs, const Digest& rhs) noexcept {
    return lhs.bytes_ < rhs.bytes_;
  }

 private:
  std::array<std::byte, 32> bytes_{};

  friend class DigestBuilder;
  friend PF_EXPORT Digest digest_bytes(std::string_view bytes);
  friend PF_EXPORT Digest digest_from_bytes(const std::array<std::byte, 32>& bytes);
};

/// Builds a canonical digest over length-prefixed named fields.
///
/// Encoding: for every field, a 32-bit little-endian name length, the name, a
/// 64-bit little-endian value length and the value. Length prefixing makes the
/// encoding unambiguous: no combination of field names and values can collide
/// with a different combination.
class PF_EXPORT DigestBuilder {
 public:
  DigestBuilder() = default;

  DigestBuilder& add_field(std::string_view name, std::string_view value);
  DigestBuilder& add_u64(std::string_view name, std::uint64_t value);
  DigestBuilder& add_bool(std::string_view name, bool value);

  Digest finish() const;

 private:
  std::string buffer_;
};

/// Digest of an arbitrary byte range.
PF_EXPORT Digest digest_bytes(std::string_view bytes);

/// Builds a digest value from its raw 32 byte representation. Used by decoders
/// that must reconstruct a digest transported as bytes.
PF_EXPORT Digest digest_from_bytes(const std::array<std::byte, 32>& bytes);

/// Semantic digest of a committed configuration.
///
/// Process-local bookkeeping (the mutation attempt identity) is excluded: it
/// describes how a state was reached, not what the state is. Provenance kind and
/// source are included because authority semantics depend on them.
PF_EXPORT Digest digest_configuration(const PortConfiguration& configuration);

/// Digest of a port record, including lifecycle, ownership, capability binding
/// and applied evidence.
PF_EXPORT Digest digest_record(const PortRecord& record);

/// Digest over a set of records. Records are sorted by PortId before digesting,
/// so the result is independent of insertion order.
PF_EXPORT Digest digest_records(const std::vector<PortRecord>& records);

}  // namespace portfabric
