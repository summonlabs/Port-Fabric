#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "portfabric/export.hpp"
#include "portfabric/limits.hpp"

namespace portfabric {

/// Precise, non-collapsing result code for every Port Fabric operation.
///
/// Port Fabric never reports a generic "port configuration failed". Every
/// rejection names the exact dimension that rejected it so an operator can act
/// on the result without re-deriving state.
enum class OutcomeCode : std::uint8_t {
  /// The operation completed and changed authoritative state.
  Ok = 0,
  /// The exact same semantic mutation with the same attempt identity was already
  /// committed: no generation advanced and the original result is reported.
  Idempotent,
  /// The requested state already holds: nothing to do, no generation advanced.
  AlreadyCurrent,

  // ---- request shape -------------------------------------------------------
  MalformedRequest,
  UnknownPort,
  UnknownDevice,
  UnknownProfile,
  UnknownPublisher,
  UnknownOwnership,
  UnknownAggregate,
  WrongEntityClass,
  DuplicateRecord,
  ResourceBoundExceeded,
  InvalidText,
  InvalidEnumValue,

  // ---- authority and generations ------------------------------------------
  StaleDeviceGeneration,
  StaleTopologyGeneration,
  StaleConfigurationGeneration,
  StaleCapabilityGeneration,
  StaleWorkerBoot,
  StaleCoordinatorEpoch,
  StaleOwnershipGeneration,
  UnauthorizedOwner,
  OwnershipConflict,
  PublisherFenced,
  FencedByRestart,

  // ---- configuration semantics --------------------------------------------
  UnsupportedConfiguration,
  CapabilityUnknown,
  CapabilityUnavailable,
  InvalidLifecycleTransition,
  InvalidConfiguration,
  InvalidSpeed,
  InvalidMtu,
  InvalidLaneConfiguration,
  InvalidBreakout,
  InvalidProfile,
  PortRetired,
  PortSuperseded,
  RevalidationRequired,
  DriftDetected,
  ConflictDetected,
  TransceiverUnknown,

  // ---- application to hardware --------------------------------------------
  ApplyFailed,
  ApplyOutcomeUnknown,
  ReadbackMismatch,
  AdapterUnavailable,

  // ---- persistence and transport ------------------------------------------
  PersistenceCorruption,
  PersistenceVersionUnsupported,
  PersistenceIoFailure,
  PersistenceBoundsExceeded,
  TransportFailure,

  // ---- runtime -------------------------------------------------------------
  InternalError,
  ShuttingDown,
};

/// Stable upper-case rendering of an outcome code. The rendering is part of the
/// observable contract: it is used by the CLI, the protocol and the tests.
PF_EXPORT std::string_view to_string(OutcomeCode code) noexcept;

/// True when the code represents an accepted, authoritative result.
PF_EXPORT bool is_accepted(OutcomeCode code) noexcept;

/// True when the code represents an accepted result that advanced a generation.
PF_EXPORT bool advances_generation(OutcomeCode code) noexcept;

/// Structured outcome of an operation.
///
/// An outcome carries a precise code, a single-line human readable message and a
/// bounded set of structured key/value details. The rendering is deterministic:
/// details are rendered in insertion order and never contain newlines.
class PF_EXPORT Outcome {
 public:
  Outcome() = default;

  static Outcome success();
  static Outcome success(std::string message);
  static Outcome idempotent(std::string message);
  static Outcome current(std::string message);
  static Outcome failure(OutcomeCode code, std::string message);

  OutcomeCode code() const noexcept { return code_; }
  bool ok() const noexcept { return code_ == OutcomeCode::Ok; }
  bool accepted() const noexcept { return is_accepted(code_); }
  bool advanced() const noexcept { return advances_generation(code_); }
  const std::string& message() const noexcept { return message_; }

  /// Replaces the message. The message must be single-line and bounded.
  Outcome& with_message(std::string message);

  /// Attaches a structured detail. At most max_outcome_details details are kept;
  /// surplus details are ignored rather than growing without bound.
  Outcome& with(std::string key, std::string value);

  const std::vector<std::pair<std::string, std::string>>& details() const noexcept {
    return details_;
  }

  /// Deterministic single-line rendering: "CODE: message key=value ...".
  std::string to_string() const;

  friend bool operator==(const Outcome& lhs, const Outcome& rhs) noexcept {
    return lhs.code_ == rhs.code_ && lhs.message_ == rhs.message_;
  }

 private:
  OutcomeCode code_ = OutcomeCode::Ok;
  std::string message_;
  std::vector<std::pair<std::string, std::string>> details_;
};

}  // namespace portfabric
