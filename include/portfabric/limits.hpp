#pragma once

#include <cstddef>
#include <cstdint>

#include "portfabric/export.hpp"

namespace portfabric {

// Every externally reachable container, string and count in Port Fabric is
// bounded here. Bounds are checked before memory is reserved or a container is
// grown, so a hostile or corrupt input can never drive an unbounded allocation.

/// Maximum length of an encoded strong identifier.
inline constexpr std::size_t max_identifier_length = 128;

/// Maximum length of a human readable name (profile name, role, source label).
inline constexpr std::size_t max_name_length = 128;

/// Maximum length of a description or free text field.
inline constexpr std::size_t max_description_length = 1024;

/// Maximum number of entries in a single capability list.
inline constexpr std::size_t max_capability_entries = 64;

/// Maximum number of enumerated modes (lane counts, FEC, autonegotiation).
inline constexpr std::size_t max_mode_entries = 32;

/// Maximum number of ports bound to one parent device.
inline constexpr std::size_t max_ports_per_device = 4096;

/// Maximum number of ports carried by a single snapshot.
inline constexpr std::size_t max_snapshot_ports = 1000000;

/// Maximum number of registered profiles.
inline constexpr std::size_t max_profiles = 65536;

/// Maximum number of profile generations retained for one profile identifier.
inline constexpr std::size_t max_profile_generations = 64;

/// Maximum number of tracked publishers.
inline constexpr std::size_t max_publishers = 65536;

/// Maximum number of records in a persisted state document.
inline constexpr std::size_t max_persisted_records = 1000000;

/// Maximum number of distinct devices tracked by the runtime.
inline constexpr std::size_t max_devices = 262144;

/// Maximum number of distinct aggregate identities.
inline constexpr std::size_t max_aggregates = 262144;

/// Maximum number of mutations accepted in one batch request.
inline constexpr std::size_t max_batch_mutations = 1024;

/// Maximum number of structured detail fields attached to an outcome.
inline constexpr std::size_t max_outcome_details = 32;

/// Maximum number of reasons attached to one explanation.
inline constexpr std::size_t max_explanation_reasons = 64;

/// Maximum number of queue entries reported by a capability binding.
inline constexpr std::uint32_t max_queue_count = 4096;

/// Maximum encoded size of a single port configuration payload.
inline constexpr std::size_t max_configuration_bytes = 8192;

/// Maximum size of the diagnostic text carried by a query response.
///
/// A full record rendering is larger than a description field: the bound is
/// explicit and still small enough that a response always fits a frame.
inline constexpr std::size_t max_query_text_bytes = 8192;

/// Maximum encoded size of a control protocol frame payload.
inline constexpr std::size_t max_frame_bytes = 1u << 20;

/// Maximum number of concurrently tracked connector sessions.
inline constexpr std::size_t max_sessions = 1024;

/// Maximum number of socket bytes buffered for one logical frame header.
inline constexpr std::size_t max_frame_header_bytes = 32;

/// Largest port speed representable by this runtime, in bits per second.
inline constexpr std::uint64_t max_speed_bits_per_second = 1600000000000ull;

/// Smallest port speed representable by this runtime, in bits per second.
inline constexpr std::uint64_t min_speed_bits_per_second = 1000000ull;

/// Largest number of lanes in one lane configuration.
inline constexpr std::uint32_t max_lanes = 16;

/// Largest port MTU representable by this runtime, in bytes.
inline constexpr std::uint32_t max_mtu = 65536;

/// Smallest port MTU representable by this runtime, in bytes.
inline constexpr std::uint32_t min_mtu = 68;

/// Largest number of children produced by one breakout.
inline constexpr std::uint32_t max_breakout_children = 8;

/// Maximum number of ports addressable by one runtime instance.
inline constexpr std::size_t max_ports = 1000000;

/// Maximum persistence file size accepted for decoding.
inline constexpr std::uint64_t max_persistence_bytes = 512ull * 1024ull * 1024ull;

/// Maximum decoded string length inside a persistence or protocol document.
inline constexpr std::size_t max_decoded_string_bytes = 4096;

}  // namespace portfabric
