#include "portfabric/persistence.hpp"

#include <algorithm>
#include <cstring>

#include "codec.hpp"
#include "portfabric/platform.hpp"
#include "text.hpp"
#include "wire.hpp"

namespace portfabric {
namespace {

using detail::ByteReader;
using detail::ByteWriter;

constexpr char kMagic[8] = {'P', 'O', 'R', 'T', 'F', 'A', 'B', 'C'};
constexpr std::size_t kHeaderSize = 32;
constexpr std::size_t kPayloadLimit = 256u * 1024u * 1024u;

}  // namespace

std::string DeviceBinding::to_string() const {
  std::string out = device.valid() ? device.to_string() : std::string("none");
  out.append("#");
  out.append(generation.valid() ? generation.to_string() : std::string("none"));
  if (!source.empty()) {
    out.push_back('(');
    out.append(source);
    out.push_back(')');
  }
  return out;
}

std::string PublisherRecord::to_string() const {
  std::string out = publisher.valid() ? publisher.to_string() : std::string("none");
  out.append(" boot=");
  out.append(boot.valid() ? boot.to_string() : std::string("none"));
  out.append(" epoch=");
  out.append(epoch.valid() ? epoch.to_string() : std::string("none"));
  out.append(fenced ? " fenced" : " active");
  return out;
}

std::string_view persistence_magic() noexcept {
  return std::string_view(kMagic, sizeof(kMagic));
}

std::uint32_t persistence_format_version() noexcept { return kPersistenceFormatVersion; }

std::uint32_t persistence_checksum(std::string_view bytes) noexcept {
  return detail::crc32(bytes);
}

Outcome encode_persisted_state(const PersistedState& state, std::string& out) {
  out.clear();
  std::string payload;
  {
    ByteWriter writer(payload, kPayloadLimit);
    detail::write_id(writer, state.fabric.to_string());
    detail::write_id(writer, state.site.to_string());
    writer.u64(state.epoch.value());
    writer.u64(state.engine_generation.value());
    writer.u64(state.topology.value());
    if (state.records.size() > max_persisted_records) {
      return Outcome::failure(OutcomeCode::PersistenceBoundsExceeded,
                              "the record count exceeds the configured bound");
    }
    writer.u32(static_cast<std::uint32_t>(state.records.size()));
    for (const PortRecord& record : state.records) {
      std::string validation_error;
      if (!record.validate(validation_error)) {
        return Outcome::failure(OutcomeCode::InvalidConfiguration,
                                "a record is inconsistent: " + validation_error);
      }
      detail::write_record(writer, record);
    }
    if (state.profiles.size() > max_profiles) {
      return Outcome::failure(OutcomeCode::PersistenceBoundsExceeded,
                              "the profile count exceeds the configured bound");
    }
    writer.u32(static_cast<std::uint32_t>(state.profiles.size()));
    for (const PortProfile& profile : state.profiles) {
      std::string validation_error;
      if (!profile.validate(validation_error)) {
        return Outcome::failure(OutcomeCode::InvalidProfile,
                                "a profile is inconsistent: " + validation_error);
      }
      detail::write_profile(writer, profile);
    }
    if (state.devices.size() > max_devices) {
      return Outcome::failure(OutcomeCode::PersistenceBoundsExceeded,
                              "the device count exceeds the configured bound");
    }
    writer.u32(static_cast<std::uint32_t>(state.devices.size()));
    for (const DeviceBinding& device : state.devices) {
      detail::write_device(writer, device);
    }
    if (state.publishers.size() > max_publishers) {
      return Outcome::failure(OutcomeCode::PersistenceBoundsExceeded,
                              "the publisher count exceeds the configured bound");
    }
    writer.u32(static_cast<std::uint32_t>(state.publishers.size()));
    for (const PublisherRecord& publisher : state.publishers) {
      detail::write_publisher(writer, publisher);
    }
    if (!writer.ok()) {
      return Outcome::failure(OutcomeCode::PersistenceBoundsExceeded,
                              "the state does not fit in the configured container bound");
    }
  }

  std::string header;
  {
    ByteWriter writer(header, kHeaderSize);
    writer.raw(std::string_view(kMagic, sizeof(kMagic)));
    writer.u32(kPersistenceFormatVersion);
    writer.u32(static_cast<std::uint32_t>(kHeaderSize));
    writer.u64(static_cast<std::uint64_t>(payload.size()));
    writer.u32(detail::crc32(payload));
    if (!writer.ok()) {
      return Outcome::failure(OutcomeCode::InternalError,
                              "the container header could not be built");
    }
    writer.u32(detail::crc32(header));
    if (!writer.ok()) {
      return Outcome::failure(OutcomeCode::InternalError,
                              "the container header could not be built");
    }
  }

  out.reserve(header.size() + payload.size());
  out.append(header);
  out.append(payload);
  return Outcome::success("state encoded");
}

Outcome decode_persisted_state(std::string_view bytes, PersistedState& out) {
  out = PersistedState{};
  if (bytes.size() < kHeaderSize) {
    return Outcome::failure(OutcomeCode::PersistenceCorruption,
                            "the container is shorter than its header");
  }
  if (std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) {
    return Outcome::failure(OutcomeCode::PersistenceCorruption,
                            "the container magic does not match");
  }
  const std::string_view header = bytes.substr(0, kHeaderSize);
  ByteReader header_reader(header);
  std::string magic;
  if (!header_reader.raw(sizeof(kMagic), magic)) {
    return Outcome::failure(OutcomeCode::PersistenceCorruption, "the container header is truncated");
  }
  std::uint32_t version = 0;
  std::uint32_t header_size = 0;
  std::uint64_t payload_length = 0;
  std::uint32_t payload_crc = 0;
  std::uint32_t header_crc = 0;
  if (!header_reader.u32(version) || !header_reader.u32(header_size) ||
      !header_reader.u64(payload_length) || !header_reader.u32(payload_crc) ||
      !header_reader.u32(header_crc)) {
    return Outcome::failure(OutcomeCode::PersistenceCorruption, "the container header is truncated");
  }
  if (header_size != kHeaderSize) {
    return Outcome::failure(OutcomeCode::PersistenceCorruption,
                            "the container header size is not the expected size");
  }
  if (detail::crc32(header.substr(0, kHeaderSize - 4)) != header_crc) {
    return Outcome::failure(OutcomeCode::PersistenceCorruption,
                            "the container header integrity check failed");
  }
  if (version != kPersistenceFormatVersion) {
    return Outcome::failure(OutcomeCode::PersistenceVersionUnsupported,
                            "the container format version is not supported by this build");
  }
  if (payload_length > max_persistence_bytes) {
    return Outcome::failure(OutcomeCode::PersistenceBoundsExceeded,
                            "the container payload exceeds the configured bound");
  }
  if (payload_length != bytes.size() - kHeaderSize) {
    return Outcome::failure(OutcomeCode::PersistenceCorruption,
                            "the container payload length does not match the container size");
  }
  const std::string_view payload = bytes.substr(kHeaderSize);
  if (detail::crc32(payload) != payload_crc) {
    return Outcome::failure(OutcomeCode::PersistenceCorruption,
                            "the container payload integrity check failed");
  }

  ByteReader reader(payload);
  if (!detail::read_strong_id(reader, out.fabric)) {
    return Outcome::failure(OutcomeCode::PersistenceCorruption, "the fabric identity is malformed");
  }
  if (!detail::read_strong_id(reader, out.site)) {
    return Outcome::failure(OutcomeCode::PersistenceCorruption, "the site identity is malformed");
  }
  std::uint64_t value = 0;
  if (!reader.u64(value)) {
    return Outcome::failure(OutcomeCode::PersistenceCorruption, "the coordinator epoch is missing");
  }
  out.epoch = CoordinatorEpoch::from_value(value);
  if (!reader.u64(value)) {
    return Outcome::failure(OutcomeCode::PersistenceCorruption, "the engine generation is missing");
  }
  out.engine_generation = EngineGeneration::from_value(value);
  if (!reader.u64(value)) {
    return Outcome::failure(OutcomeCode::PersistenceCorruption, "the topology generation is missing");
  }
  out.topology = TopologyGeneration::from_value(value);

  std::uint32_t count = 0;
  if (!detail::read_bounded_count(reader, max_persisted_records, count)) {
    return Outcome::failure(OutcomeCode::PersistenceBoundsExceeded,
                            "the record count is out of bounds");
  }
  out.records.clear();
  out.records.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    PortRecord record;
    if (!detail::read_record(reader, record)) {
      return Outcome::failure(OutcomeCode::PersistenceCorruption,
                              "a port record could not be decoded");
    }
    for (const PortRecord& existing : out.records) {
      if (existing.port == record.port) {
        return Outcome::failure(OutcomeCode::DuplicateRecord,
                                "the container carries a duplicate port identity");
      }
    }
    out.records.push_back(std::move(record));
  }

  if (!detail::read_bounded_count(reader, max_profiles, count)) {
    return Outcome::failure(OutcomeCode::PersistenceBoundsExceeded,
                            "the profile count is out of bounds");
  }
  out.profiles.clear();
  out.profiles.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    PortProfile profile;
    if (!detail::read_profile(reader, profile)) {
      return Outcome::failure(OutcomeCode::PersistenceCorruption,
                              "a profile record could not be decoded");
    }
    for (const PortProfile& existing : out.profiles) {
      if (existing.id == profile.id && existing.generation == profile.generation) {
        return Outcome::failure(OutcomeCode::DuplicateRecord,
                                "the container carries a duplicate profile generation");
      }
    }
    out.profiles.push_back(std::move(profile));
  }

  if (!detail::read_bounded_count(reader, max_devices, count)) {
    return Outcome::failure(OutcomeCode::PersistenceBoundsExceeded,
                            "the device count is out of bounds");
  }
  out.devices.clear();
  out.devices.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    DeviceBinding device;
    if (!detail::read_device(reader, device)) {
      return Outcome::failure(OutcomeCode::PersistenceCorruption,
                              "a device binding could not be decoded");
    }
    for (const DeviceBinding& existing : out.devices) {
      if (existing.device == device.device) {
        return Outcome::failure(OutcomeCode::DuplicateRecord,
                                "the container carries a duplicate device binding");
      }
    }
    out.devices.push_back(std::move(device));
  }

  if (!detail::read_bounded_count(reader, max_publishers, count)) {
    return Outcome::failure(OutcomeCode::PersistenceBoundsExceeded,
                            "the publisher count is out of bounds");
  }
  out.publishers.clear();
  out.publishers.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    PublisherRecord publisher;
    if (!detail::read_publisher(reader, publisher)) {
      return Outcome::failure(OutcomeCode::PersistenceCorruption,
                              "a publisher record could not be decoded");
    }
    for (const PublisherRecord& existing : out.publishers) {
      if (existing.publisher == publisher.publisher && existing.boot == publisher.boot) {
        return Outcome::failure(OutcomeCode::DuplicateRecord,
                                "the container carries a duplicate publisher incarnation");
      }
    }
    out.publishers.push_back(std::move(publisher));
  }

  if (!reader.exhausted()) {
    return Outcome::failure(OutcomeCode::PersistenceCorruption,
                            "the container carries trailing bytes");
  }
  return Outcome::success("state decoded");
}

Outcome PersistenceStore::validate_path(const std::string& path) {
  return validate_file_path(path);
}

Outcome PersistenceStore::save(const std::string& path, const PersistedState& state) {
  if (const Outcome check = validate_file_path(path); !check.ok()) {
    return check;
  }
  std::string bytes;
  if (const Outcome encoded = encode_persisted_state(state, bytes); !encoded.ok()) {
    return encoded;
  }
  const std::string temporary = path + ".pftmp";
  (void)remove_file(temporary);
  if (const Outcome written = write_file_flushed(temporary, bytes); !written.ok()) {
    (void)remove_file(temporary);
    return written;
  }
  std::string verify;
  const Outcome read = read_file_bounded(temporary, max_persistence_bytes, verify);
  if (!read.ok() || verify != bytes) {
    (void)remove_file(temporary);
    return Outcome::failure(OutcomeCode::PersistenceIoFailure,
                            "the temporary container failed verification");
  }
  if (const Outcome replaced = atomic_replace_file(temporary, path); !replaced.ok()) {
    (void)remove_file(temporary);
    return replaced;
  }
  return Outcome::success("durable state written");
}

Outcome PersistenceStore::load(const std::string& path, PersistedState& state) {
  state = PersistedState{};
  if (const Outcome check = validate_file_path(path); !check.ok()) {
    return check;
  }
  if (!file_exists(path)) {
    return Outcome::failure(OutcomeCode::PersistenceIoFailure,
                            "the container does not exist at the configured path");
  }
  std::string bytes;
  if (const Outcome read = read_file_bounded(path, max_persistence_bytes, bytes); !read.ok()) {
    return read;
  }
  return decode_persisted_state(bytes, state);
}

}  // namespace portfabric
