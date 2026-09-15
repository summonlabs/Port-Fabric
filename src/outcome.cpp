#include "portfabric/outcome.hpp"

#include <algorithm>

#include "text.hpp"

namespace portfabric {

std::string_view to_string(OutcomeCode code) noexcept {
  switch (code) {
    case OutcomeCode::Ok:
      return "OK";
    case OutcomeCode::Idempotent:
      return "IDEMPOTENT";
    case OutcomeCode::AlreadyCurrent:
      return "ALREADY_CURRENT";
    case OutcomeCode::MalformedRequest:
      return "MALFORMED_REQUEST";
    case OutcomeCode::UnknownPort:
      return "UNKNOWN_PORT";
    case OutcomeCode::UnknownDevice:
      return "UNKNOWN_DEVICE";
    case OutcomeCode::UnknownProfile:
      return "UNKNOWN_PROFILE";
    case OutcomeCode::UnknownPublisher:
      return "UNKNOWN_PUBLISHER";
    case OutcomeCode::UnknownOwnership:
      return "UNKNOWN_OWNERSHIP";
    case OutcomeCode::UnknownAggregate:
      return "UNKNOWN_AGGREGATE";
    case OutcomeCode::WrongEntityClass:
      return "WRONG_ENTITY_CLASS";
    case OutcomeCode::DuplicateRecord:
      return "DUPLICATE_RECORD";
    case OutcomeCode::ResourceBoundExceeded:
      return "RESOURCE_BOUND_EXCEEDED";
    case OutcomeCode::InvalidText:
      return "INVALID_TEXT";
    case OutcomeCode::InvalidEnumValue:
      return "INVALID_ENUM_VALUE";
    case OutcomeCode::StaleDeviceGeneration:
      return "STALE_DEVICE_GENERATION";
    case OutcomeCode::StaleTopologyGeneration:
      return "STALE_TOPOLOGY_GENERATION";
    case OutcomeCode::StaleConfigurationGeneration:
      return "STALE_CONFIGURATION_GENERATION";
    case OutcomeCode::StaleCapabilityGeneration:
      return "STALE_CAPABILITY_GENERATION";
    case OutcomeCode::StaleWorkerBoot:
      return "STALE_WORKER_BOOT";
    case OutcomeCode::StaleCoordinatorEpoch:
      return "STALE_COORDINATOR_EPOCH";
    case OutcomeCode::StaleOwnershipGeneration:
      return "STALE_OWNERSHIP_GENERATION";
    case OutcomeCode::UnauthorizedOwner:
      return "UNAUTHORIZED_OWNER";
    case OutcomeCode::OwnershipConflict:
      return "OWNERSHIP_CONFLICT";
    case OutcomeCode::PublisherFenced:
      return "PUBLISHER_FENCED";
    case OutcomeCode::FencedByRestart:
      return "FENCED_BY_RESTART";
    case OutcomeCode::UnsupportedConfiguration:
      return "UNSUPPORTED_CONFIGURATION";
    case OutcomeCode::CapabilityUnknown:
      return "CAPABILITY_UNKNOWN";
    case OutcomeCode::CapabilityUnavailable:
      return "CAPABILITY_UNAVAILABLE";
    case OutcomeCode::InvalidLifecycleTransition:
      return "INVALID_LIFECYCLE_TRANSITION";
    case OutcomeCode::InvalidConfiguration:
      return "INVALID_CONFIGURATION";
    case OutcomeCode::InvalidSpeed:
      return "INVALID_SPEED";
    case OutcomeCode::InvalidMtu:
      return "INVALID_MTU";
    case OutcomeCode::InvalidLaneConfiguration:
      return "INVALID_LANE_CONFIGURATION";
    case OutcomeCode::InvalidBreakout:
      return "INVALID_BREAKOUT";
    case OutcomeCode::InvalidProfile:
      return "INVALID_PROFILE";
    case OutcomeCode::PortRetired:
      return "PORT_RETIRED";
    case OutcomeCode::PortSuperseded:
      return "PORT_SUPERSEDED";
    case OutcomeCode::RevalidationRequired:
      return "REVALIDATION_REQUIRED";
    case OutcomeCode::DriftDetected:
      return "DRIFT_DETECTED";
    case OutcomeCode::ConflictDetected:
      return "CONFLICT_DETECTED";
    case OutcomeCode::TransceiverUnknown:
      return "TRANSCEIVER_UNKNOWN";
    case OutcomeCode::ApplyFailed:
      return "APPLY_FAILED";
    case OutcomeCode::ApplyOutcomeUnknown:
      return "APPLY_OUTCOME_UNKNOWN";
    case OutcomeCode::ReadbackMismatch:
      return "READBACK_MISMATCH";
    case OutcomeCode::AdapterUnavailable:
      return "ADAPTER_UNAVAILABLE";
    case OutcomeCode::PersistenceCorruption:
      return "PERSISTENCE_CORRUPTION";
    case OutcomeCode::PersistenceVersionUnsupported:
      return "PERSISTENCE_VERSION_UNSUPPORTED";
    case OutcomeCode::PersistenceIoFailure:
      return "PERSISTENCE_IO_FAILURE";
    case OutcomeCode::PersistenceBoundsExceeded:
      return "PERSISTENCE_BOUNDS_EXCEEDED";
    case OutcomeCode::TransportFailure:
      return "TRANSPORT_FAILURE";
    case OutcomeCode::InternalError:
      return "INTERNAL_ERROR";
    case OutcomeCode::ShuttingDown:
      return "SHUTTING_DOWN";
  }
  return "INTERNAL_ERROR";
}

bool is_accepted(OutcomeCode code) noexcept {
  return code == OutcomeCode::Ok || code == OutcomeCode::Idempotent ||
         code == OutcomeCode::AlreadyCurrent;
}

bool advances_generation(OutcomeCode code) noexcept { return code == OutcomeCode::Ok; }

Outcome Outcome::success() { return Outcome{}; }

Outcome Outcome::success(std::string message) {
  Outcome result;
  result.message_ = std::move(message);
  return result;
}

Outcome Outcome::idempotent(std::string message) {
  Outcome result;
  result.code_ = OutcomeCode::Idempotent;
  result.message_ = std::move(message);
  return result;
}

Outcome Outcome::current(std::string message) {
  Outcome result;
  result.code_ = OutcomeCode::AlreadyCurrent;
  result.message_ = std::move(message);
  return result;
}

Outcome Outcome::failure(OutcomeCode code, std::string message) {
  Outcome result;
  result.code_ = code;
  result.message_ = std::move(message);
  return result;
}

Outcome& Outcome::with_message(std::string message) {
  message_ = detail::single_line(message);
  if (message_.size() > max_description_length) {
    message_.resize(max_description_length);
  }
  return *this;
}

Outcome& Outcome::with(std::string key, std::string value) {
  if (details_.size() >= max_outcome_details) {
    return *this;
  }
  std::string bounded_key = detail::single_line(key);
  if (bounded_key.size() > max_name_length) {
    bounded_key.resize(max_name_length);
  }
  std::string bounded_value = detail::single_line(value);
  if (bounded_value.size() > max_description_length) {
    bounded_value.resize(max_description_length);
  }
  details_.emplace_back(std::move(bounded_key), std::move(bounded_value));
  return *this;
}

std::string Outcome::to_string() const {
  std::string out(portfabric::to_string(code_));
  out.append(": ");
  out.append(message_.empty() ? std::string_view("no detail") : std::string_view(message_));
  for (const auto& [key, value] : details_) {
    out.push_back(' ');
    out.append(key);
    out.push_back('=');
    out.append(value);
  }
  return out;
}

}  // namespace portfabric
