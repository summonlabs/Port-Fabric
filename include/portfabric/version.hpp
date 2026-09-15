#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "portfabric/export.hpp"

/// Port Fabric: authoritative port-governance runtime of the Distributed
/// Fabric Infrastructure ("Fabric OS") stack.
///
/// The runtime answers one question:
///
///   What exact fabric port exists on this device generation, how is that port
///   configured, which capabilities and administrative state apply to it, who
///   owns or controls it, which configuration generation is authoritative, and
///   when must a port operation be rejected, fenced, superseded, retired, or
///   revalidated?
namespace portfabric {

inline constexpr std::uint32_t kVersionMajor = PF_VERSION_MAJOR;
inline constexpr std::uint32_t kVersionMinor = PF_VERSION_MINOR;
inline constexpr std::uint32_t kVersionPatch = PF_VERSION_PATCH;

/// Persisted state container format version understood by this build.
inline constexpr std::uint32_t kPersistenceFormatVersion = 1;

/// Distributed control protocol version understood by this build.
inline constexpr std::uint32_t kProtocolVersion = 1;

/// Version string of the form "major.minor.patch".
PF_EXPORT std::string version_string();

/// Stable product identifier used in diagnostics and persistable state.
PF_EXPORT std::string_view product_name() noexcept;

}  // namespace portfabric
