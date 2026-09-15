#include "portfabric/diff.hpp"

#include <algorithm>

namespace portfabric {
namespace {

constexpr std::size_t kFieldCount = 22;

void add_if_changed(std::vector<DiffEntry>& entries, DiffField field, std::string before,
                    std::string after) {
  if (before == after) {
    return;
  }
  DiffEntry entry;
  entry.field = field;
  entry.before = std::move(before);
  entry.after = std::move(after);
  entries.push_back(std::move(entry));
}

void sort_entries(std::vector<DiffEntry>& entries) {
  std::stable_sort(entries.begin(), entries.end(), [](const DiffEntry& lhs, const DiffEntry& rhs) {
    return static_cast<std::uint8_t>(lhs.field) < static_cast<std::uint8_t>(rhs.field);
  });
}

std::string render_configuration_field(const PortConfiguration& configuration, DiffField field) {
  switch (field) {
    case DiffField::Administrative:
      return std::string(portfabric::to_string(configuration.administrative));
    case DiffField::PortMode:
      return std::string(portfabric::to_string(configuration.mode));
    case DiffField::ProtocolFamily:
      return std::string(portfabric::to_string(configuration.protocol));
    case DiffField::Speed:
      return configuration.speed.to_string();
    case DiffField::LaneConfiguration:
      return configuration.lanes.to_string();
    case DiffField::Mtu:
      return configuration.mtu.to_string();
    case DiffField::Duplex:
      return std::string(portfabric::to_string(configuration.duplex));
    case DiffField::Fec:
      return std::string(portfabric::to_string(configuration.fec));
    case DiffField::Autoneg:
      return std::string(portfabric::to_string(configuration.autoneg));
    case DiffField::Pause:
      return std::string(portfabric::to_string(configuration.pause));
    case DiffField::Breakout:
      return std::string(portfabric::to_string(configuration.breakout));
    case DiffField::LogicalDerivation: {
      std::string out = configuration.logical_identity.valid()
                            ? configuration.logical_identity.to_string()
                            : std::string("none");
      out.append("->");
      out.append(configuration.derived_from.valid() ? configuration.derived_from.to_string()
                                                    : std::string("none"));
      out.append("#");
      out.append(std::to_string(configuration.child_index));
      return out;
    }
    case DiffField::Aggregation:
      return configuration.aggregation.to_string();
    case DiffField::Profile:
      return configuration.profile.to_string();
    case DiffField::Role:
      return configuration.role;
    case DiffField::CapabilityBinding:
      return configuration.capability_generation.valid()
                 ? configuration.capability_generation.to_string()
                 : std::string("none");
    case DiffField::DeviceGeneration:
      return configuration.device_generation.valid() ? configuration.device_generation.to_string()
                                                     : std::string("none");
    case DiffField::TopologyGeneration:
      return configuration.topology_generation.valid()
                 ? configuration.topology_generation.to_string()
                 : std::string("none");
    case DiffField::Lifecycle:
    case DiffField::Ownership:
    case DiffField::AppliedEvidence:
    case DiffField::Drift:
      return std::string();
  }
  return std::string();
}

constexpr DiffField kConfigurationFields[kFieldCount] = {
    DiffField::Lifecycle,        DiffField::Administrative,      DiffField::PortMode,
    DiffField::ProtocolFamily,   DiffField::Speed,               DiffField::LaneConfiguration,
    DiffField::Mtu,              DiffField::Duplex,              DiffField::Fec,
    DiffField::Autoneg,          DiffField::Pause,               DiffField::Breakout,
    DiffField::LogicalDerivation, DiffField::Aggregation,        DiffField::Profile,
    DiffField::Role,             DiffField::Ownership,           DiffField::CapabilityBinding,
    DiffField::DeviceGeneration, DiffField::TopologyGeneration,  DiffField::AppliedEvidence,
    DiffField::Drift};

}  // namespace

std::string_view to_string(DiffField field) noexcept {
  switch (field) {
    case DiffField::Lifecycle:
      return "lifecycle";
    case DiffField::Administrative:
      return "administrative";
    case DiffField::PortMode:
      return "port_mode";
    case DiffField::ProtocolFamily:
      return "protocol_family";
    case DiffField::Speed:
      return "speed";
    case DiffField::LaneConfiguration:
      return "lane_configuration";
    case DiffField::Mtu:
      return "mtu";
    case DiffField::Duplex:
      return "duplex";
    case DiffField::Fec:
      return "fec";
    case DiffField::Autoneg:
      return "autoneg";
    case DiffField::Pause:
      return "pause";
    case DiffField::Breakout:
      return "breakout";
    case DiffField::LogicalDerivation:
      return "logical_derivation";
    case DiffField::Aggregation:
      return "aggregation";
    case DiffField::Profile:
      return "profile";
    case DiffField::Role:
      return "role";
    case DiffField::Ownership:
      return "ownership";
    case DiffField::CapabilityBinding:
      return "capability_binding";
    case DiffField::DeviceGeneration:
      return "device_generation";
    case DiffField::TopologyGeneration:
      return "topology_generation";
    case DiffField::AppliedEvidence:
      return "applied_evidence";
    case DiffField::Drift:
      return "drift";
  }
  return "unknown";
}

bool parse_diff_field(std::string_view text, DiffField& out) noexcept {
  for (std::uint8_t index = 0; index < static_cast<std::uint8_t>(kFieldCount); ++index) {
    const auto field = static_cast<DiffField>(index);
    if (portfabric::to_string(field) == text) {
      out = field;
      return true;
    }
  }
  return false;
}

std::string DiffEntry::to_string() const {
  std::string out(portfabric::to_string(field));
  out.append(": ");
  out.append(before.empty() ? "none" : before);
  out.append(" -> ");
  out.append(after.empty() ? "none" : after);
  return out;
}

std::string ConfigurationDiff::to_string() const {
  if (entries_.empty()) {
    return "no differences";
  }
  std::string out;
  for (const DiffEntry& entry : entries_) {
    out.append(entry.to_string());
    out.push_back('\n');
  }
  if (!out.empty()) {
    out.pop_back();
  }
  return out;
}

std::string ConfigurationDiff::to_single_line() const {
  if (entries_.empty()) {
    return "no differences";
  }
  std::string out;
  bool first = true;
  for (const DiffEntry& entry : entries_) {
    if (!first) {
      out.append("; ");
    }
    first = false;
    out.append(entry.to_string());
  }
  return out;
}

ConfigurationDiff diff_configurations(const PortConfiguration& before,
                                      const PortConfiguration& after) {
  ConfigurationDiff diff;
  for (const DiffField field : kConfigurationFields) {
    if (field == DiffField::Lifecycle || field == DiffField::Ownership ||
        field == DiffField::AppliedEvidence || field == DiffField::Drift) {
      continue;
    }
    add_if_changed(diff.entries_, field, render_configuration_field(before, field),
                   render_configuration_field(after, field));
  }
  sort_entries(diff.entries_);
  return diff;
}

ConfigurationDiff diff_records(const PortRecord& before, const PortRecord& after) {
  ConfigurationDiff diff;
  if (before.configuration.has_value() && after.configuration.has_value()) {
    diff = diff_configurations(*before.configuration, *after.configuration);
  } else if (!before.configuration.has_value() && after.configuration.has_value()) {
    for (const DiffField field : kConfigurationFields) {
      if (field == DiffField::Lifecycle || field == DiffField::Ownership ||
          field == DiffField::AppliedEvidence || field == DiffField::Drift) {
        continue;
      }
      add_if_changed(diff.entries_, field, "absent", render_configuration_field(*after.configuration, field));
    }
  } else if (before.configuration.has_value() && !after.configuration.has_value()) {
    for (const DiffField field : kConfigurationFields) {
      if (field == DiffField::Lifecycle || field == DiffField::Ownership ||
          field == DiffField::AppliedEvidence || field == DiffField::Drift) {
        continue;
      }
      add_if_changed(diff.entries_, field, render_configuration_field(*before.configuration, field),
                     "absent");
    }
  }
  add_if_changed(diff.entries_, DiffField::Lifecycle, std::string(portfabric::to_string(before.lifecycle)),
                 std::string(portfabric::to_string(after.lifecycle)));
  add_if_changed(diff.entries_, DiffField::Ownership,
                 before.ownership.has_value() ? before.ownership->to_string() : std::string("unowned"),
                 after.ownership.has_value() ? after.ownership->to_string() : std::string("unowned"));
  add_if_changed(diff.entries_, DiffField::AppliedEvidence, before.applied.to_string(),
                 after.applied.to_string());
  add_if_changed(diff.entries_, DiffField::Drift, std::string(portfabric::to_string(before.drift)),
                 std::string(portfabric::to_string(after.drift)));
  add_if_changed(diff.entries_, DiffField::CapabilityBinding,
                 before.capability.generation.valid() ? before.capability.generation.to_string()
                                                      : std::string("none"),
                 after.capability.generation.valid() ? after.capability.generation.to_string()
                                                     : std::string("none"));
  sort_entries(diff.entries_);
  return diff;
}

}  // namespace portfabric
