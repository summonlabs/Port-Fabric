#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "portfabric/export.hpp"
#include "portfabric/generation.hpp"
#include "portfabric/ids.hpp"
#include "portfabric/outcome.hpp"
#include "portfabric/profile.hpp"
#include "portfabric/record.hpp"
#include "portfabric/version.hpp"

namespace portfabric {

/// Device generation known to the runtime.
///
/// Port Fabric does not own device identity or device generations: Fabric
/// Registry and Fabric Topology do. This is the generation Port Fabric has been
/// told about, used to fence configurations bound to an older generation.
struct PF_EXPORT DeviceBinding {
  DeviceId device;
  DeviceGeneration generation;
  /// Bounded label of the component that reported the generation.
  std::string source;

  std::string to_string() const;
  friend bool operator==(const DeviceBinding& lhs, const DeviceBinding& rhs) noexcept {
    return lhs.device == rhs.device && lhs.generation == rhs.generation;
  }
};

/// Publisher known to the coordinator.
///
/// A publisher is a stable process identity; a boot is one incarnation of that
/// process. Every restart produces a new WorkerBootId, and an old boot stays
/// fenced forever.
struct PF_EXPORT PublisherRecord {
  PublisherId publisher;
  WorkerBootId boot;
  CoordinatorEpoch epoch;
  /// True when this publisher incarnation has been fenced.
  bool fenced = false;

  std::string to_string() const;
};

/// Durable state written to and read from the persistence container.
struct PF_EXPORT PersistedState {
  FabricId fabric;
  SiteId site;
  /// Epoch of the process incarnation that wrote this state.
  CoordinatorEpoch epoch;
  EngineGeneration engine_generation;
  TopologyGeneration topology;
  std::vector<PortRecord> records;
  std::vector<PortProfile> profiles;
  std::vector<DeviceBinding> devices;
  std::vector<PublisherRecord> publishers;
};

/// Versioned, integrity-checked persistence container.
///
/// Layout:
///   header: magic "PORTFABC" (8 bytes), format version (u32),
///           header size (u32), payload length (u64), payload CRC32 (u32),
///           header CRC32 (u32)
///   payload: canonical bounded encoding of PersistedState.
///
/// The header CRC covers the header up to and including the payload CRC field.
/// The payload CRC covers exactly payload_length bytes. Decoding is bounded at
/// every step: lengths are checked against the remaining input and against the
/// configured bounds before anything is allocated.
class PF_EXPORT PersistenceStore {
 public:
  /// Writes state atomically.
  ///
  /// The container is written to a temporary file in the same directory, flushed
  /// and closed, re-read and verified, then atomically replaced over the target
  /// path. A failure at any step leaves the previous container intact and
  /// removes the temporary file.
  static Outcome save(const std::string& path, const PersistedState& state);

  /// Reads and validates a container.
  static Outcome load(const std::string& path, PersistedState& state);

  /// Validates a persistence path without touching the file system beyond
  /// resolving the parent directory.
  static Outcome validate_path(const std::string& path);
};

/// Encodes a state into the container byte layout.
PF_EXPORT Outcome encode_persisted_state(const PersistedState& state, std::string& out);

/// Decodes a container byte layout.
PF_EXPORT Outcome decode_persisted_state(std::string_view bytes, PersistedState& out);

/// Magic and versions exposed so that tooling and tests can construct and attack
/// containers without duplicating the constants.
PF_EXPORT std::string_view persistence_magic() noexcept;
PF_EXPORT std::uint32_t persistence_format_version() noexcept;

/// CRC-32 used by the container and frame integrity checks.
///
/// Exposed so that tooling and tests can construct, repair and attack containers
/// without duplicating the integrity implementation.
PF_EXPORT std::uint32_t persistence_checksum(std::string_view bytes) noexcept;

}  // namespace portfabric
