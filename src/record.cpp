#include "portfabric/record.hpp"

#include "text.hpp"

namespace portfabric {

std::string_view to_string(AppliedOutcome outcome) noexcept {
  switch (outcome) {
    case AppliedOutcome::NotAttempted:
      return "NOT_ATTEMPTED";
    case AppliedOutcome::Applied:
      return "APPLIED";
    case AppliedOutcome::ApplyFailed:
      return "APPLY_FAILED";
    case AppliedOutcome::OutcomeUnknown:
      return "APPLY_OUTCOME_UNKNOWN";
    case AppliedOutcome::Verified:
      return "VERIFIED";
    case AppliedOutcome::Unsupported:
      return "UNSUPPORTED";
  }
  return "NOT_ATTEMPTED";
}

bool parse_applied_outcome(std::string_view text, AppliedOutcome& out) noexcept {
  const std::pair<std::string_view, AppliedOutcome> table[] = {
      {"NOT_ATTEMPTED", AppliedOutcome::NotAttempted},
      {"APPLIED", AppliedOutcome::Applied},
      {"APPLY_FAILED", AppliedOutcome::ApplyFailed},
      {"APPLY_OUTCOME_UNKNOWN", AppliedOutcome::OutcomeUnknown},
      {"VERIFIED", AppliedOutcome::Verified},
      {"UNSUPPORTED", AppliedOutcome::Unsupported}};
  for (const auto& [rendering, value] : table) {
    if (rendering == text) {
      out = value;
      return true;
    }
  }
  return false;
}

bool is_valid(AppliedOutcome outcome) noexcept {
  switch (outcome) {
    case AppliedOutcome::NotAttempted:
    case AppliedOutcome::Applied:
    case AppliedOutcome::ApplyFailed:
    case AppliedOutcome::OutcomeUnknown:
    case AppliedOutcome::Verified:
    case AppliedOutcome::Unsupported:
      return true;
  }
  return false;
}

std::string_view to_string(DriftState state) noexcept {
  switch (state) {
    case DriftState::Unknown:
      return "UNKNOWN";
    case DriftState::InSync:
      return "IN_SYNC";
    case DriftState::Drifted:
      return "DRIFTED";
    case DriftState::RevalidationRequired:
      return "REVALIDATION_REQUIRED";
    case DriftState::Conflicted:
      return "CONFLICTED";
  }
  return "UNKNOWN";
}

bool parse_drift_state(std::string_view text, DriftState& out) noexcept {
  const std::pair<std::string_view, DriftState> table[] = {{"UNKNOWN", DriftState::Unknown},
                                                           {"IN_SYNC", DriftState::InSync},
                                                           {"DRIFTED", DriftState::Drifted},
                                                           {"REVALIDATION_REQUIRED",
                                                            DriftState::RevalidationRequired},
                                                           {"CONFLICTED", DriftState::Conflicted}};
  for (const auto& [rendering, value] : table) {
    if (rendering == text) {
      out = value;
      return true;
    }
  }
  return false;
}

bool is_valid(DriftState state) noexcept {
  switch (state) {
    case DriftState::Unknown:
    case DriftState::InSync:
    case DriftState::Drifted:
    case DriftState::RevalidationRequired:
    case DriftState::Conflicted:
      return true;
  }
  return false;
}

std::string AppliedEvidence::to_string() const {
  std::string out(portfabric::to_string(outcome));
  out.append(" generation=");
  out.append(generation.valid() ? generation.to_string() : std::string("none"));
  out.append(" evidence_generation=");
  out.append(evidence_generation.valid() ? evidence_generation.to_string() : std::string("none"));
  out.append(" verified=");
  detail::append_bool(out, verified);
  if (!source.empty()) {
    out.append(" source=");
    out.append(source);
  }
  return out;
}

std::string GenerationVector::to_string() const {
  std::string out("configuration=");
  out.append(configuration.valid() ? configuration.to_string() : std::string("none"));
  out.append(" administrative=");
  out.append(administrative.valid() ? administrative.to_string() : std::string("none"));
  out.append(" ownership=");
  out.append(ownership.valid() ? ownership.to_string() : std::string("none"));
  out.append(" capability=");
  out.append(capability.valid() ? capability.to_string() : std::string("none"));
  out.append(" topology=");
  out.append(topology.valid() ? topology.to_string() : std::string("none"));
  out.append(" device=");
  out.append(device.valid() ? device.to_string() : std::string("none"));
  out.append(" lifecycle=");
  out.append(lifecycle.valid() ? lifecycle.to_string() : std::string("none"));
  out.append(" evidence=");
  out.append(evidence.valid() ? evidence.to_string() : std::string("none"));
  return out;
}

std::vector<std::pair<std::string, std::string>> GenerationVector::canonical_fields() const {
  std::vector<std::pair<std::string, std::string>> fields;
  fields.reserve(8);
  auto add = [&fields](std::string_view name, const auto& generation) {
    fields.emplace_back(std::string(name),
                        generation.valid() ? generation.to_string() : std::string());
  };
  add("configuration", configuration);
  add("administrative", administrative);
  add("ownership", ownership);
  add("capability", capability);
  add("topology", topology);
  add("device", device);
  add("lifecycle", lifecycle);
  add("evidence", evidence);
  return fields;
}

std::string LastMutationRecord::to_string() const {
  if (!valid()) {
    return "none";
  }
  std::string out = attempt.to_string();
  out.append(" payload=");
  detail::append_u64(out, payload_digest);
  out.append(" result_generation=");
  out.append(result_generation.valid() ? result_generation.to_string() : std::string("none"));
  return out;
}

bool PortRecord::validate(std::string& error) const {
  if (!port.valid()) {
    error = "the record has no canonical port identity";
    return false;
  }
  if (!parent_device.valid()) {
    error = "the record has no parent device identity";
    return false;
  }
  if (!is_valid(lifecycle)) {
    error = "the lifecycle state is not a legal value";
    return false;
  }
  if (!is_valid(drift)) {
    error = "the drift state is not a legal value";
    return false;
  }
  if (!is_valid(applied.outcome)) {
    error = "the applied outcome is not a legal value";
    return false;
  }
  if (!capability.valid()) {
    error = "the capability binding is inconsistent";
    return false;
  }
  if (ownership.has_value() && !ownership->valid()) {
    error = "the ownership record is inconsistent";
    return false;
  }
  if (configuration.has_value()) {
    if (!configuration->validate(error)) {
      return false;
    }
    if (configuration->port != port) {
      error = "the configuration is bound to a different port identity";
      return false;
    }
    if (configuration->parent_device != parent_device) {
      error = "the configuration is bound to a different parent device";
      return false;
    }
    if (generations.configuration.valid() &&
        configuration->generation != generations.configuration) {
      error = "the record generation does not match the configuration generation";
      return false;
    }
  } else if (lifecycle != PortLifecycle::Unconfigured && lifecycle != PortLifecycle::Retired &&
             lifecycle != PortLifecycle::Superseded) {
    error = "a live lifecycle state requires a committed configuration";
    return false;
  }
  if (!detail::validate_text(status_reason, max_name_length, error)) {
    error = "status reason: " + error;
    return false;
  }
  if (!detail::validate_text(status_detail, max_description_length, error)) {
    error = "status detail: " + error;
    return false;
  }
  return true;
}

std::string PortRecord::to_string() const {
  std::string out("port=");
  out.append(port.valid() ? port.to_string() : std::string("none"));
  out.append(" parent=");
  out.append(parent_device.valid() ? parent_device.to_string() : std::string("none"));
  out.append(" lifecycle=");
  out.append(portfabric::to_string(lifecycle));
  out.append(" drift=");
  out.append(portfabric::to_string(drift));
  out.append(" configuration=");
  out.append(configuration.has_value() ? configuration->to_string() : std::string("none"));
  out.append(" ownership=");
  out.append(ownership.has_value() ? ownership->to_string() : std::string("unowned"));
  out.append(" capability=");
  out.append(capability.to_string());
  out.append(" applied=");
  out.append(applied.to_string());
  out.append(" generations=");
  out.append(generations.to_string());
  out.append(" last_mutation=");
  out.append(last_mutation.to_string());
  if (!status_reason.empty()) {
    out.append(" status=");
    out.append(status_reason);
  }
  return out;
}

}  // namespace portfabric
