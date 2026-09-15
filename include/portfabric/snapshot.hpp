#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "portfabric/digest.hpp"
#include "portfabric/export.hpp"
#include "portfabric/generation.hpp"
#include "portfabric/ids.hpp"
#include "portfabric/record.hpp"

namespace portfabric {

/// Immutable view of the runtime state at one engine generation.
///
/// A snapshot is a value: it is never mutated afterwards and it can always be
/// inspected. Its currentness is explicit: current_for() compares the engine
/// generation and coordinator epoch the snapshot was taken at against the
/// runtime's current values. An old snapshot never masquerades as current.
class PF_EXPORT Snapshot {
 public:
  Snapshot() = default;

  SnapshotId id;
  EngineGeneration generation;
  CoordinatorEpoch epoch;
  TopologyGeneration topology;
  Digest digest;
  std::vector<PortRecord> records;

  std::size_t port_count() const noexcept { return records.size(); }
  bool empty() const noexcept { return records.empty(); }

  /// True when this snapshot still describes the current state.
  bool current_for(EngineGeneration current_generation, CoordinatorEpoch current_epoch) const
      noexcept {
    return generation.valid() && generation == current_generation && epoch == current_epoch;
  }

  /// Looks a record up inside the snapshot.
  const PortRecord* find(const PortId& port) const noexcept;

  std::string to_string() const;
};

}  // namespace portfabric
