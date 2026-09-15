#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "portfabric/export.hpp"
#include "portfabric/generation.hpp"
#include "portfabric/ids.hpp"

namespace portfabric {

/// Who controls a port.
///
/// Ownership is mutation authority over the port configuration, not physical or
/// commercial ownership. Physical ownership is out of scope for this runtime.
enum class PortOwnerKind : std::uint8_t {
  Unknown = 0,
  /// The agent running on the switch or device that hosts the port.
  SwitchAgent,
  /// The agent running on the host that drives the port.
  HostAgent,
  /// A network control plane process.
  NetworkController,
  /// The administrative control plane of the fabric.
  AdministrativeControlPlane,
  /// A subsystem the administrative control plane delegated authority to.
  DelegatedSubsystem,
};

PF_EXPORT std::string_view to_string(PortOwnerKind kind) noexcept;
PF_EXPORT bool parse_owner_kind(std::string_view text, PortOwnerKind& out) noexcept;
PF_EXPORT bool is_valid(PortOwnerKind kind) noexcept;

/// Control ownership of one port, bound to a process incarnation.
///
/// Ownership carries the publisher and worker boot that currently hold it. A
/// process restart produces a new WorkerBootId, so a restarted process never
/// silently retains mutation authority merely because its stable identity is
/// unchanged.
struct PF_EXPORT PortOwnership {
  PortOwnershipId id;
  PortOwnerKind kind = PortOwnerKind::Unknown;
  OwnerId owner;
  PortOwnershipGeneration generation;
  PublisherId publisher;
  WorkerBootId boot;
  CoordinatorEpoch epoch;
  /// When true no other authority may hold this port concurrently.
  bool exclusive = true;

  bool valid() const noexcept {
    return id.valid() && is_valid(kind) && owner.valid() && generation.valid() &&
           publisher.valid() && boot.valid() && epoch.valid();
  }

  /// True when the given process incarnation holds this ownership.
  bool held_by(const PublisherId& candidate_publisher, const WorkerBootId& candidate_boot,
               CoordinatorEpoch candidate_epoch) const noexcept {
    return valid() && publisher == candidate_publisher && boot == candidate_boot &&
           epoch == candidate_epoch;
  }

  std::string to_string() const;
};

}  // namespace portfabric
