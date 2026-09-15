#include "portfabric/digest.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

#include "sha256.hpp"
#include "text.hpp"

namespace portfabric {
namespace {

void append_u32_le(std::string& out, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<char>((value >> shift) & 0xffu));
  }
}

void append_u64_le(std::string& out, std::uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8) {
    out.push_back(static_cast<char>((value >> shift) & 0xffu));
  }
}

}  // namespace

Digest Digest::zero() { return Digest{}; }

bool Digest::is_zero() const noexcept {
  for (const std::byte value : bytes_) {
    if (value != std::byte{0}) {
      return false;
    }
  }
  return true;
}

std::string Digest::to_hex() const {
  return detail::bytes_to_hex(bytes_.data(), bytes_.size());
}

DigestBuilder& DigestBuilder::add_field(std::string_view name, std::string_view value) {
  append_u32_le(buffer_, static_cast<std::uint32_t>(name.size()));
  buffer_.append(name);
  append_u64_le(buffer_, static_cast<std::uint64_t>(value.size()));
  buffer_.append(value);
  return *this;
}

DigestBuilder& DigestBuilder::add_u64(std::string_view name, std::uint64_t value) {
  return add_field(name, detail::format_u64(value));
}

DigestBuilder& DigestBuilder::add_bool(std::string_view name, bool value) {
  return add_field(name, value ? std::string_view("1") : std::string_view("0"));
}

Digest DigestBuilder::finish() const {
  detail::Sha256 sha;
  sha.update(buffer_);
  Digest digest;
  digest.bytes_ = sha.finish();
  return digest;
}

Digest digest_from_bytes(const std::array<std::byte, 32>& bytes) {
  Digest digest;
  digest.bytes_ = bytes;
  return digest;
}

Digest digest_bytes(std::string_view bytes) {
  detail::Sha256 sha;
  sha.update(std::string(bytes));
  Digest digest;
  digest.bytes_ = sha.finish();
  return digest;
}

Digest digest_configuration(const PortConfiguration& configuration) {
  DigestBuilder builder;
  for (const auto& [name, value] : configuration.canonical_fields()) {
    if (name == "provenance_attempt" || name == "evidence_generation") {
      // The attempt identity and the evidence generation record how the state was
      // reached and which evidence describes its application, not what the
      // configured state is. Two identical configurations must digest equally.
      continue;
    }
    builder.add_field(name, value);
  }
  return builder.finish();
}

Digest digest_record(const PortRecord& record) {
  DigestBuilder builder;
  builder.add_field("port", record.port.to_string());
  builder.add_field("parent_device", record.parent_device.to_string());
  builder.add_field("lifecycle", portfabric::to_string(record.lifecycle));
  builder.add_field("drift", portfabric::to_string(record.drift));
  builder.add_bool("configured", record.configuration.has_value());
  if (record.configuration.has_value()) {
    builder.add_field("configuration", digest_configuration(*record.configuration).to_hex());
  }
  builder.add_field("ownership",
                    record.ownership.has_value() ? record.ownership->to_string() : std::string());
  builder.add_field("capability_generation",
                    record.capability.generation.valid() ? record.capability.generation.to_string()
                                                         : std::string());
  builder.add_field("capability_source", record.capability.source);
  builder.add_field("applied", portfabric::to_string(record.applied.outcome));
  builder.add_bool("applied_verified", record.applied.verified);
  for (const auto& [name, value] : record.generations.canonical_fields()) {
    builder.add_field("generation." + name, value);
  }
  builder.add_field("last_mutation_attempt",
                    record.last_mutation.attempt.valid() ? record.last_mutation.attempt.to_string()
                                                         : std::string());
  builder.add_u64("last_mutation_payload", record.last_mutation.payload_digest);
  builder.add_field("status_reason", record.status_reason);
  return builder.finish();
}

Digest digest_records(const std::vector<PortRecord>& records) {
  std::vector<const PortRecord*> ordered;
  ordered.reserve(records.size());
  for (const PortRecord& record : records) {
    ordered.push_back(&record);
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const PortRecord* lhs, const PortRecord* rhs) { return lhs->port < rhs->port; });
  DigestBuilder builder;
  builder.add_u64("record_count", static_cast<std::uint64_t>(ordered.size()));
  for (const PortRecord* record : ordered) {
    builder.add_field(record->port.to_string(), digest_record(*record).to_hex());
  }
  return builder.finish();
}

}  // namespace portfabric
