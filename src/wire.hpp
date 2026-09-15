#pragma once

#include <cstdint>
#include <string>

#include "codec.hpp"
#include "portfabric/authority.hpp"
#include "portfabric/capability.hpp"
#include "portfabric/configuration.hpp"
#include "portfabric/persistence.hpp"
#include "portfabric/record.hpp"

namespace portfabric::detail {

// Canonical field codecs shared by the persistence container and the control
// protocol. Both use exactly the same encoding, so a value never changes shape
// depending on the transport it travelled over.

void write_id(ByteWriter& writer, const std::string& value);
bool read_id(ByteReader& reader, std::string& out);

/// Reads a strongly typed identifier from its encoded rendering, accepting an
/// empty encoding as the null identifier of that domain.
template <class Id>
bool read_strong_id(ByteReader& reader, Id& out) {
  std::string text;
  if (!read_id(reader, text)) {
    return false;
  }
  if (text.empty()) {
    out = Id{};
    return true;
  }
  const auto parsed = Id::parse(text);
  if (!parsed.has_value()) {
    return false;
  }
  out = *parsed;
  return true;
}

/// Reads a bounded element count, rejecting a count that cannot possibly be
/// satisfied by the remaining input. Public within the library because both the
/// container decoder and the protocol decoder rely on it.
bool read_bounded_count(ByteReader& reader, std::size_t limit, std::uint32_t& count);

void write_provenance(ByteWriter& writer, const Provenance& provenance);
bool read_provenance(ByteReader& reader, Provenance& provenance);

void write_capabilities(ByteWriter& writer, const PortCapabilities& capabilities);
bool read_capabilities(ByteReader& reader, PortCapabilities& capabilities);

void write_capability_binding(ByteWriter& writer, const CapabilityBinding& binding);
bool read_capability_binding(ByteReader& reader, CapabilityBinding& binding);

void write_configuration(ByteWriter& writer, const PortConfiguration& configuration);
bool read_configuration(ByteReader& reader, PortConfiguration& configuration);

void write_ownership(ByteWriter& writer, const PortOwnership& ownership);
bool read_ownership(ByteReader& reader, PortOwnership& ownership);

void write_authority(ByteWriter& writer, const AuthorityContext& authority);
bool read_authority(ByteReader& reader, AuthorityContext& authority);

void write_applied(ByteWriter& writer, const AppliedEvidence& evidence);
bool read_applied(ByteReader& reader, AppliedEvidence& evidence);

void write_generations(ByteWriter& writer, const GenerationVector& generations);
bool read_generations(ByteReader& reader, GenerationVector& generations);

void write_record(ByteWriter& writer, const PortRecord& record);
bool read_record(ByteReader& reader, PortRecord& record);

void write_profile_configuration(ByteWriter& writer, const ProfileConfiguration& configuration);
bool read_profile_configuration(ByteReader& reader, ProfileConfiguration& configuration);

void write_profile(ByteWriter& writer, const PortProfile& profile);
bool read_profile(ByteReader& reader, PortProfile& profile);

void write_device(ByteWriter& writer, const DeviceBinding& device);
bool read_device(ByteReader& reader, DeviceBinding& device);

void write_publisher(ByteWriter& writer, const PublisherRecord& publisher);
bool read_publisher(ByteReader& reader, PublisherRecord& publisher);

}  // namespace portfabric::detail
