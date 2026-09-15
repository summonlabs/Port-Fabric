#pragma once

#include <string>
#include <string_view>

#include "portfabric/export.hpp"
#include "portfabric/generation.hpp"
#include "portfabric/ids.hpp"
#include "portfabric/outcome.hpp"

namespace portfabric {

/// Authority context carried by every mutation request.
///
/// A mutation is only considered when the coordinator epoch, the publisher, the
/// worker boot, the ownership generation and every expected generation in the
/// context still match authoritative state. Authority is rejected before any
/// semantic validation happens, so a fenced process can never probe or alter
/// configuration.
struct PF_EXPORT AuthorityContext {
  CoordinatorEpoch epoch;
  PublisherId publisher;
  WorkerBootId boot;
  /// Expected current ownership identity. Empty means "the port must be
  /// unowned", which is only legal for an ownership claim.
  PortOwnershipId ownership;
  PortOwnershipGeneration ownership_generation;
  PortConfigurationGeneration expected_configuration;
  CapabilityBindingGeneration expected_capability;
  TopologyGeneration expected_topology;
  DeviceGeneration expected_device;
  /// Identity of this mutation attempt. Required for every mutation: it is what
  /// makes exact replay distinguishable from a stale request.
  MutationAttemptId attempt;

  bool valid() const noexcept {
    return epoch.valid() && publisher.valid() && boot.valid() && attempt.valid();
  }

  /// True when the context carries the configuration expectation.
  bool expects_configuration() const noexcept { return expected_configuration.valid(); }

  std::string to_string() const;
};

/// Stable rendering of a specific authority rejection.
struct PF_EXPORT AuthorityRejection {
  OutcomeCode code = OutcomeCode::Ok;
  std::string detail;

  bool rejected() const noexcept { return code != OutcomeCode::Ok; }
};

}  // namespace portfabric
