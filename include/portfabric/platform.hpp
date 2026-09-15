#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "portfabric/export.hpp"
#include "portfabric/ids.hpp"
#include "portfabric/outcome.hpp"

namespace portfabric {

/// Real, read-only evidence about one port of the local host.
///
/// This is host-visible evidence, not switch-fabric truth. Port Fabric records
/// the evidence class it came from and never presents host adapter evidence as
/// switch port proof.
struct PF_EXPORT HostPortEvidence {
  /// Canonical interface identity on this host (the interface GUID on Windows).
  std::string interface_identity;
  /// Operator visible alias.
  std::string alias;
  /// Driver description.
  std::string description;
  /// PnP instance identity reported by the operating system, when available.
  std::string pnp_identity;
  PortEntityClass entity_class = PortEntityClass::NicPort;
  /// Administrative status reported by the operating system.
  bool admin_enabled = false;
  /// Operational status reported by the operating system. Never authoritative
  /// for Port Fabric: Link State Fabric owns link condition.
  bool operational_up = false;
  /// Link speed reported by the operating system, in bits per second.
  std::uint64_t speed_bits_per_second = 0;
  /// Interface MTU reported by the operating system.
  std::uint32_t mtu = 0;
  /// Media type label reported by the operating system.
  std::string media_type;

  std::string to_string() const;
};

/// Local platform services.
///
/// Everything here is read-only with respect to host configuration: Port Fabric
/// never changes the host's network configuration while gathering evidence.
class PF_EXPORT HostPlatform {
 public:
  /// True when real host port enumeration is available on this platform.
  static bool available() noexcept;

  /// Label of the enumeration backend, e.g. "windows-mib-if" or "unsupported".
  static std::string_view backend_label() noexcept;

  /// Enumerates host ports. Returns an empty vector and sets error when the
  /// platform cannot enumerate; error is empty when enumeration succeeds and the
  /// host simply has no ports.
  static std::vector<HostPortEvidence> enumerate_ports(std::string& error);

  /// Number of logical processors visible to the runtime.
  static std::uint32_t processor_count() noexcept;

  /// Identifier of the current process.
  static std::uint32_t current_process_id() noexcept;
};

/// Replaces a target file with a source file atomically.
///
/// On Windows this is MoveFileEx with MOVEFILE_REPLACE_EXISTING and
/// MOVEFILE_WRITE_THROUGH. On other platforms it is a rename over the target.
PF_EXPORT Outcome atomic_replace_file(const std::string& source, const std::string& target);

/// Reads a whole file, refusing to exceed the byte bound.
PF_EXPORT Outcome read_file_bounded(const std::string& path, std::uint64_t max_bytes,
                                    std::string& bytes);

/// Writes a file, flushes it to stable storage and closes it.
PF_EXPORT Outcome write_file_flushed(const std::string& path, std::string_view bytes);

/// Removes a file. Missing files are not an error.
PF_EXPORT Outcome remove_file(const std::string& path);

/// True when the path exists and is a regular file.
PF_EXPORT bool file_exists(const std::string& path);

/// True when the path exists and is a directory.
PF_EXPORT bool directory_exists(const std::string& path);

/// Validates a caller supplied file path: bounded length, no traversal
/// component, no control characters, and an existing parent directory.
PF_EXPORT Outcome validate_file_path(const std::string& path);

}  // namespace portfabric
