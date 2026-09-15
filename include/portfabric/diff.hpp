#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "portfabric/configuration.hpp"
#include "portfabric/export.hpp"
#include "portfabric/record.hpp"

namespace portfabric {

/// Dimension compared by a configuration or record diff.
///
/// The enumerator order defines the rendering order of a diff: entries are
/// always emitted in ascending enumerator order, so two diffs over the same pair
/// of states render identically regardless of how the states were produced.
enum class DiffField : std::uint8_t {
  Lifecycle = 0,
  Administrative,
  PortMode,
  ProtocolFamily,
  Speed,
  LaneConfiguration,
  Mtu,
  Duplex,
  Fec,
  Autoneg,
  Pause,
  Breakout,
  LogicalDerivation,
  Aggregation,
  Profile,
  Role,
  Ownership,
  CapabilityBinding,
  DeviceGeneration,
  TopologyGeneration,
  AppliedEvidence,
  Drift,
};

PF_EXPORT std::string_view to_string(DiffField field) noexcept;
PF_EXPORT bool parse_diff_field(std::string_view text, DiffField& out) noexcept;

/// One changed dimension.
struct PF_EXPORT DiffEntry {
  DiffField field = DiffField::Lifecycle;
  std::string before;
  std::string after;

  std::string to_string() const;
};

/// Deterministic ordered set of changed dimensions.
class PF_EXPORT ConfigurationDiff {
 public:
  bool empty() const noexcept { return entries_.empty(); }
  std::size_t size() const noexcept { return entries_.size(); }
  const std::vector<DiffEntry>& entries() const noexcept { return entries_; }

  /// Stable multi-line rendering, one changed dimension per line.
  std::string to_string() const;

  /// Stable single-line rendering suitable for logs and tests.
  std::string to_single_line() const;

 private:
  std::vector<DiffEntry> entries_;

  friend PF_EXPORT ConfigurationDiff diff_configurations(const PortConfiguration& before,
                                                         const PortConfiguration& after);
  friend PF_EXPORT ConfigurationDiff diff_records(const PortRecord& before,
                                                  const PortRecord& after);
};

/// Diff of two committed configurations.
PF_EXPORT ConfigurationDiff diff_configurations(const PortConfiguration& before,
                                                const PortConfiguration& after);

/// Diff of two port records: configuration dimensions plus lifecycle, ownership,
/// capability binding, generations, applied evidence and drift.
PF_EXPORT ConfigurationDiff diff_records(const PortRecord& before, const PortRecord& after);

}  // namespace portfabric
