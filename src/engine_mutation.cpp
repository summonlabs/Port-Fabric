#include "engine_internal.hpp"

#include <algorithm>
#include <functional>

#include "text.hpp"

namespace portfabric {
namespace {

using Builder = MutationBuilder;

Provenance make_provenance(const MutationEnvelope& envelope, ProvenanceKind fallback) {
  Provenance provenance;
  provenance.kind = envelope.provenance_kind == ProvenanceKind::Unknown ? fallback
                                                                        : envelope.provenance_kind;
  provenance.source = envelope.provenance_source;
  provenance.publisher = envelope.authority.publisher;
  provenance.boot = envelope.authority.boot;
  provenance.epoch = envelope.authority.epoch;
  provenance.attempt = envelope.authority.attempt;
  return provenance;
}

std::string canonical_payload(const PortConfiguration& configuration) {
  std::string out;
  for (const auto& [name, value] : configuration.canonical_fields()) {
    if (name == "provenance_attempt" || name == "provenance_epoch" ||
        name == "evidence_generation") {
      // Bookkeeping that must not make two identical configurations look
      // different, so that an identical resubmission is recognised as such.
      continue;
    }
    out.append(name);
    out.push_back('=');
    out.append(value);
    out.push_back(';');
  }
  return out;
}

bool configuration_semantically_equal(const PortConfiguration& lhs,
                                      const PortConfiguration& rhs) {
  return canonical_payload(lhs) == canonical_payload(rhs);
}

void reject(MutationPlan& plan, OutcomeCode code, std::string detail,
            ExplanationCode explanation) {
  plan.failure = code;
  plan.failure_detail = std::move(detail);
  plan.failure_explanation = explanation;
}

void capture_expected(const PortRecord& record, MutationPlan& plan) {
  plan.expected.configuration = record.generations.configuration;
  plan.expected.lifecycle = record.generations.lifecycle;
  plan.expected.administrative = record.generations.administrative;
  plan.expected.ownership = record.generations.ownership;
  plan.expected.evidence = record.generations.evidence;
  plan.expected.last_attempt = record.last_mutation.attempt;
}

void advance_lifecycle_generation(PortRecord& record) {
  record.generations.lifecycle = record.generations.lifecycle.valid()
                                     ? record.generations.lifecycle.next()
                                     : LifecycleGeneration::from_value(1);
}

void advance_administrative_generation(PortRecord& record) {
  record.generations.administrative = record.generations.administrative.valid()
                                          ? record.generations.administrative.next()
                                          : AdministrativeGeneration::from_value(1);
}

void advance_configuration_generation(PortRecord& record) {
  record.generations.configuration = record.generations.configuration.valid()
                                         ? record.generations.configuration.next()
                                         : PortConfigurationGeneration::from_value(1);
}

void advance_ownership_generation(PortRecord& record) {
  record.generations.ownership = record.generations.ownership.valid()
                                     ? record.generations.ownership.next()
                                     : PortOwnershipGeneration::from_value(1);
}

void advance_evidence_generation(PortRecord& record) {
  record.generations.evidence = record.generations.evidence.valid()
                                    ? record.generations.evidence.next()
                                    : EvidenceGeneration::from_value(1);
}

/// Lifecycle states from which configuration mutation is possible without an
/// explicit reconciliation first.
OutcomeCode rejection_for_fenced_state(PortLifecycle lifecycle) {
  switch (lifecycle) {
    case PortLifecycle::Retired:
      return OutcomeCode::PortRetired;
    case PortLifecycle::Superseded:
      return OutcomeCode::PortSuperseded;
    case PortLifecycle::RevalidationRequired:
      return OutcomeCode::RevalidationRequired;
    case PortLifecycle::Conflicted:
      return OutcomeCode::ConflictDetected;
    default:
      return OutcomeCode::InvalidLifecycleTransition;
  }
}

ExplanationCode explanation_for_fenced_state(PortLifecycle lifecycle) {
  switch (lifecycle) {
    case PortLifecycle::Retired:
      return ExplanationCode::RejectedPortRetired;
    case PortLifecycle::Superseded:
      return ExplanationCode::RejectedPortSuperseded;
    case PortLifecycle::RevalidationRequired:
      return ExplanationCode::RejectedRevalidationRequired;
    case PortLifecycle::Conflicted:
      return ExplanationCode::RejectedConflictDetected;
    default:
      return ExplanationCode::RejectedInvalidLifecycleTransition;
  }
}

void build_configure(const MutationBuildInput& input, const ConfigureRequest& request,
                     MutationPlan& plan) {
  const PortRecord& current = input.current;
  if (current.lifecycle != PortLifecycle::Unconfigured) {
    reject(plan, rejection_for_fenced_state(current.lifecycle),
           "the port already carries a committed configuration generation",
           explanation_for_fenced_state(current.lifecycle));
    return;
  }
  if (!input.parent_device_generation.has_value()) {
    reject(plan, OutcomeCode::UnknownDevice,
           "the parent device generation is not recorded by this runtime",
           ExplanationCode::RejectedUnknownPort);
    return;
  }
  if (!input.capability.present) {
    reject(plan, OutcomeCode::CapabilityUnavailable,
           "no capability evidence is available for this port",
           ExplanationCode::RejectedCapabilityUnavailable);
    return;
  }
  PortConfiguration configuration = request.configuration;
  if (configuration.port.valid() && configuration.port != current.port) {
    reject(plan, OutcomeCode::WrongEntityClass,
           "the configuration addresses a different port identity",
           ExplanationCode::RejectedWrongEntityClass);
    return;
  }
  if (configuration.parent_device.valid() && configuration.parent_device != current.parent_device) {
    reject(plan, OutcomeCode::WrongEntityClass,
           "the configuration names a different parent device",
           ExplanationCode::RejectedWrongEntityClass);
    return;
  }
  configuration.port = current.port;
  configuration.parent_device = current.parent_device;
  configuration.device_generation = *input.parent_device_generation;
  configuration.topology_generation = input.topology;
  configuration.generation = PortConfigurationGeneration::from_value(1);
  configuration.record = configuration.record.valid()
                             ? configuration.record
                             : PortConfigurationId::from_validated(compose_identifier(
                                   "cfg", current.port.value(), 1));
  if (!configuration.record.valid()) {
    reject(plan, OutcomeCode::MalformedRequest, "the configuration record identity is malformed",
           ExplanationCode::RejectedMalformedRequest);
    return;
  }
  if (configuration.administrative == AdministrativeState::Unknown) {
    configuration.administrative = AdministrativeState::Enabled;
  }
  if (configuration.administrative == AdministrativeState::RevalidationRequired) {
    reject(plan, OutcomeCode::InvalidConfiguration,
           "a first configuration generation cannot be committed in REVALIDATION_REQUIRED state",
           ExplanationCode::RejectedInvalidConfiguration);
    return;
  }
  if (configuration.profile.bound &&
      !input.profiles.get(configuration.profile.id, configuration.profile.generation)
           .has_value()) {
    reject(plan, OutcomeCode::UnknownProfile,
           "the configuration binds a profile generation that is not defined",
           ExplanationCode::RejectedUnknownProfile);
    return;
  }
  configuration.capability_generation = input.capability.generation;
  configuration.evidence_generation = current.generations.evidence;
  configuration.provenance = make_provenance(request.envelope, ProvenanceKind::Operator);
  std::string error;
  if (!configuration.validate(error)) {
    reject(plan, OutcomeCode::InvalidConfiguration, error,
           ExplanationCode::RejectedInvalidConfiguration);
    return;
  }
  capture_expected(current, plan);
  plan.next = current;
  plan.next.configuration = configuration;
  plan.next.lifecycle = PortLifecycle::Configured;
  advance_configuration_generation(plan.next);
  advance_administrative_generation(plan.next);
  advance_lifecycle_generation(plan.next);
  plan.next.status_reason = "CONFIGURED";
  plan.next.status_detail.clear();
  plan.next.drift = DriftState::Unknown;
  plan.next.applied = AppliedEvidence{};
  plan.next.applied.generation = configuration.generation;
  plan.configuration_changed = true;
  plan.requires_capability_validation = true;
  plan.requires_apply = true;
  plan.capability = input.capability;
  plan.apply_configuration = configuration;
}

void build_reconfigure(const MutationBuildInput& input, const ReconfigureRequest& request,
                       MutationPlan& plan) {
  const PortRecord& current = input.current;
  if (!current.configuration.has_value()) {
    reject(plan, OutcomeCode::InvalidLifecycleTransition,
           "the port has no committed configuration generation to replace",
           ExplanationCode::RejectedInvalidLifecycleTransition);
    return;
  }
  if (!is_live(current.lifecycle)) {
    reject(plan, rejection_for_fenced_state(current.lifecycle),
           "the port is fenced and must be reconciled before further configuration",
           explanation_for_fenced_state(current.lifecycle));
    return;
  }
  if (!input.capability.present) {
    reject(plan, OutcomeCode::CapabilityUnavailable,
           "no capability evidence is available for this port",
           ExplanationCode::RejectedCapabilityUnavailable);
    return;
  }
  PortConfiguration configuration = request.configuration;
  if (configuration.port.valid() && configuration.port != current.port) {
    reject(plan, OutcomeCode::WrongEntityClass,
           "the configuration addresses a different port identity",
           ExplanationCode::RejectedWrongEntityClass);
    return;
  }
  configuration.port = current.port;
  configuration.parent_device = current.parent_device;
  configuration.device_generation = *input.parent_device_generation;
  configuration.topology_generation = input.topology;
  configuration.record = current.configuration->record;
  configuration.generation = current.generations.configuration;
  if (configuration.administrative == AdministrativeState::Unknown) {
    configuration.administrative = current.configuration->administrative;
  }
  if (configuration.administrative != current.configuration->administrative) {
    reject(plan, OutcomeCode::InvalidConfiguration,
           "administrative state is changed through the administrative operation, not through "
           "reconfiguration",
           ExplanationCode::RejectedInvalidConfiguration);
    return;
  }
  if (configuration.profile.bound &&
      !input.profiles.get(configuration.profile.id, configuration.profile.generation).has_value()) {
    reject(plan, OutcomeCode::UnknownProfile,
           "the configuration binds a profile generation that is not defined",
           ExplanationCode::RejectedUnknownProfile);
    return;
  }
  configuration.capability_generation = input.capability.generation;
  configuration.evidence_generation = current.generations.evidence;
  configuration.provenance = make_provenance(request.envelope, ProvenanceKind::Operator);
  std::string error;
  if (!configuration.validate(error)) {
    reject(plan, OutcomeCode::InvalidConfiguration, error,
           ExplanationCode::RejectedInvalidConfiguration);
    return;
  }
  if (configuration_semantically_equal(*current.configuration, configuration) &&
      configuration.profile == current.configuration->profile) {
    reject(plan, OutcomeCode::AlreadyCurrent,
           "the submitted configuration is semantically identical to the committed generation",
           ExplanationCode::AlreadyCurrent);
    return;
  }
  capture_expected(current, plan);
  plan.next = current;
  advance_configuration_generation(plan.next);
  configuration.generation = plan.next.generations.configuration;
  plan.next.configuration = configuration;
  advance_lifecycle_generation(plan.next);
  plan.next.status_reason = "RECONFIGURED";
  plan.next.status_detail.clear();
  plan.next.applied = AppliedEvidence{};
  plan.next.applied.generation = configuration.generation;
  plan.configuration_changed = true;
  plan.requires_capability_validation = true;
  plan.requires_apply = true;
  plan.capability = input.capability;
  plan.apply_configuration = configuration;
}

void build_administrative(const MutationBuildInput& input, const AdministrativeRequest& request,
                          MutationPlan& plan) {
  const PortRecord& current = input.current;
  if (!is_valid(request.op)) {
    reject(plan, OutcomeCode::MalformedRequest, "the administrative operation is not a legal value",
           ExplanationCode::RejectedMalformedRequest);
    return;
  }
  if (!current.configuration.has_value()) {
    reject(plan, OutcomeCode::InvalidLifecycleTransition,
           "administrative state requires a committed configuration generation",
           ExplanationCode::RejectedInvalidLifecycleTransition);
    return;
  }
  if (!is_live(current.lifecycle)) {
    reject(plan, rejection_for_fenced_state(current.lifecycle),
           "the port is fenced and must be reconciled before its administrative state changes",
           explanation_for_fenced_state(current.lifecycle));
    return;
  }
  const LifecycleTransition transition = lifecycle_transition_for(request.op);
  const TransitionEvaluation evaluation = evaluate_transition(current.lifecycle, transition);
  if (!evaluation.allowed) {
    reject(plan, OutcomeCode::InvalidLifecycleTransition,
           std::string("the lifecycle transition is not defined: ") + std::string(evaluation.reason),
           ExplanationCode::RejectedInvalidLifecycleTransition);
    return;
  }
  const AdministrativeState target = administrative_target(request.op);
  if (current.configuration->administrative == target && current.lifecycle == evaluation.result) {
    reject(plan, OutcomeCode::AlreadyCurrent,
           "the requested administrative state already holds",
           ExplanationCode::AlreadyCurrent);
    return;
  }
  capture_expected(current, plan);
  plan.next = current;
  PortConfiguration configuration = *current.configuration;
  configuration.administrative = target;
  configuration.provenance = make_provenance(request.envelope, ProvenanceKind::Operator);
  plan.next.configuration = configuration;
  plan.next.lifecycle = evaluation.result;
  advance_administrative_generation(plan.next);
  advance_lifecycle_generation(plan.next);
  plan.next.status_reason = std::string("ADMINISTRATIVE_") + std::string(portfabric::to_string(request.op));
  plan.next.status_detail.clear();
  if (target == AdministrativeState::RevalidationRequired) {
    plan.next.drift = DriftState::RevalidationRequired;
  }
  plan.requires_apply = request.op != AdministrativeOp::RequireRevalidation;
  plan.apply_configuration = configuration;
}

void build_assign_profile(const MutationBuildInput& input, const ProfileRequest& request,
                          MutationPlan& plan) {
  const PortRecord& current = input.current;
  if (!current.configuration.has_value()) {
    reject(plan, OutcomeCode::InvalidLifecycleTransition,
           "a profile can only be assigned to a configured port",
           ExplanationCode::RejectedInvalidLifecycleTransition);
    return;
  }
  if (!is_live(current.lifecycle)) {
    reject(plan, rejection_for_fenced_state(current.lifecycle),
           "the port is fenced and must be reconciled before profiles are assigned",
           explanation_for_fenced_state(current.lifecycle));
    return;
  }
  PortConfiguration configuration = *current.configuration;
  if (request.release) {
    if (!configuration.profile.bound) {
      reject(plan, OutcomeCode::AlreadyCurrent, "no profile is bound to this port",
             ExplanationCode::AlreadyCurrent);
      return;
    }
    configuration.profile = ProfileBinding{};
  } else {
    const auto profile = input.profiles.get(request.profile, request.generation);
    if (!profile.has_value()) {
      reject(plan, OutcomeCode::UnknownProfile,
             "the requested profile generation is not defined",
             ExplanationCode::RejectedUnknownProfile);
      return;
    }
    apply_profile(profile->configuration, configuration);
    configuration.profile.bound = true;
    configuration.profile.id = request.profile;
    configuration.profile.generation = request.generation;
  }
  configuration.port = current.port;
  configuration.parent_device = current.parent_device;
  configuration.device_generation = *input.parent_device_generation;
  configuration.topology_generation = input.topology;
  configuration.capability_generation = input.capability.present
                                            ? input.capability.generation
                                            : current.generations.capability;
  configuration.provenance = make_provenance(request.envelope, ProvenanceKind::Operator);
  std::string error;
  if (!configuration.validate(error)) {
    reject(plan, OutcomeCode::InvalidProfile, error, ExplanationCode::RejectedInvalidProfile);
    return;
  }
  if (configuration_semantically_equal(*current.configuration, configuration)) {
    reject(plan, OutcomeCode::AlreadyCurrent,
           "the profile assignment does not change the committed configuration",
           ExplanationCode::AlreadyCurrent);
    return;
  }
  capture_expected(current, plan);
  plan.next = current;
  advance_configuration_generation(plan.next);
  configuration.generation = plan.next.generations.configuration;
  plan.next.configuration = configuration;
  advance_lifecycle_generation(plan.next);
  plan.next.status_reason = request.release ? "PROFILE_RELEASED" : "PROFILE_ASSIGNED";
  plan.next.status_detail.clear();
  plan.next.applied = AppliedEvidence{};
  plan.next.applied.generation = configuration.generation;
  plan.configuration_changed = true;
  plan.requires_capability_validation = true;
  plan.requires_apply = true;
  plan.capability = input.capability;
  plan.apply_configuration = configuration;
}

void build_bind_capabilities(const MutationBuildInput& input, const CapabilityRequest& request,
                             MutationPlan& plan) {
  const PortRecord& current = input.current;
  const CapabilityBinding& binding = request.binding;
  if (!binding.present || !binding.generation.valid() || !binding.evidence_generation.valid()) {
    reject(plan, OutcomeCode::MalformedRequest,
           "the capability binding is incomplete (generation and evidence generation are required)",
           ExplanationCode::RejectedMalformedRequest);
    return;
  }
  std::string error;
  if (!binding.capabilities.validate(error)) {
    reject(plan, OutcomeCode::CapabilityUnavailable, error,
           ExplanationCode::RejectedCapabilityUnavailable);
    return;
  }
  if (current.capability.present) {
    if (binding.generation < current.capability.generation) {
      reject(plan, OutcomeCode::StaleCapabilityGeneration,
             "the supplied capability generation is older than the bound generation",
             ExplanationCode::RejectedStaleCapabilityGeneration);
      return;
    }
    if (binding.generation == current.capability.generation) {
      if (digest_bytes(binding.capabilities.to_string()) ==
          digest_bytes(current.capability.capabilities.to_string())) {
        reject(plan, OutcomeCode::AlreadyCurrent,
               "the capability generation is already bound with identical evidence",
               ExplanationCode::AlreadyCurrent);
        return;
      }
      reject(plan, OutcomeCode::ConflictDetected,
             "the capability generation is already bound with different evidence",
             ExplanationCode::RejectedConflictDetected);
      return;
    }
  }
  capture_expected(current, plan);
  plan.next = current;
  plan.next.capability = binding;
  plan.next.generations.capability = binding.generation;
  plan.next.generations.evidence = binding.evidence_generation;
  plan.next.status_reason = "CAPABILITY_BOUND";
  plan.next.status_detail.clear();
  if (plan.next.configuration.has_value()) {
    const CapabilityValidation validation =
        validate_against_capabilities(*plan.next.configuration, binding);
    if (!validation.accepted) {
      // Capability truth advanced and the committed configuration is no longer
      // supported by it. The configuration is not silently grandfathered: the
      // port is fenced into REVALIDATION_REQUIRED with the exact reason.
      plan.next.lifecycle = PortLifecycle::RevalidationRequired;
      plan.next.drift = DriftState::RevalidationRequired;
      plan.next.status_reason = "CAPABILITY_INVALIDATED";
      plan.next.status_detail = validation.reason + ": " + validation.detail;
      advance_lifecycle_generation(plan.next);
    } else {
      plan.next.drift = DriftState::Unknown;
    }
  }
}

void build_reconcile(const MutationBuildInput& input, const ReconcileRequest& request,
                     MutationPlan& plan) {
  const PortRecord& current = input.current;
  if (!current.configuration.has_value()) {
    reject(plan, OutcomeCode::InvalidLifecycleTransition,
           "there is no configuration generation to reconcile",
           ExplanationCode::RejectedInvalidLifecycleTransition);
    return;
  }
  if (!input.parent_device_generation.has_value()) {
    reject(plan, OutcomeCode::UnknownDevice,
           "the parent device generation is not recorded by this runtime",
           ExplanationCode::RejectedUnknownPort);
    return;
  }
  if (!input.capability.present) {
    reject(plan, OutcomeCode::CapabilityUnavailable,
           "no capability evidence is available for this port",
           ExplanationCode::RejectedCapabilityUnavailable);
    return;
  }
  PortConfiguration configuration = *current.configuration;
  configuration.device_generation = *input.parent_device_generation;
  configuration.topology_generation = input.topology;
  configuration.capability_generation = input.capability.generation;
  configuration.provenance = make_provenance(request.envelope, ProvenanceKind::Reconciliation);
  if (configuration.profile.bound &&
      !input.profiles.get(configuration.profile.id, configuration.profile.generation).has_value()) {
    reject(plan, OutcomeCode::UnknownProfile,
           "the bound profile generation is no longer defined",
           ExplanationCode::RejectedUnknownProfile);
    return;
  }
  std::string error;
  if (!configuration.validate(error)) {
    reject(plan, OutcomeCode::InvalidConfiguration, error,
           ExplanationCode::RejectedInvalidConfiguration);
    return;
  }
  const bool rebinding = configuration.device_generation != current.configuration->device_generation ||
                         configuration.topology_generation !=
                             current.configuration->topology_generation ||
                         configuration.capability_generation !=
                             current.configuration->capability_generation;
  if (!requires_reconciliation(current.lifecycle) && current.drift != DriftState::Drifted &&
      current.drift != DriftState::RevalidationRequired && !rebinding) {
    reject(plan, OutcomeCode::AlreadyCurrent,
           "the port is already bound to the current generations",
           ExplanationCode::AlreadyCurrent);
    return;
  }
  capture_expected(current, plan);
  plan.next = current;
  advance_configuration_generation(plan.next);
  configuration.generation = plan.next.generations.configuration;
  configuration.evidence_generation = plan.next.generations.evidence;
  plan.next.configuration = configuration;
  plan.next.lifecycle = PortLifecycle::Configured;
  advance_lifecycle_generation(plan.next);
  plan.next.drift = DriftState::Unknown;
  plan.next.status_reason = "RECONCILED";
  plan.next.status_detail = request.detail.empty() ? std::string() : request.detail;
  plan.next.applied = AppliedEvidence{};
  plan.next.applied.generation = configuration.generation;
  plan.configuration_changed = true;
  plan.requires_capability_validation = true;
  plan.requires_apply = true;
  plan.capability = input.capability;
  plan.apply_configuration = configuration;
}

void build_fence(const MutationBuildInput& input, const FenceRequest& request, bool retire,
                 MutationPlan& plan) {
  const PortRecord& current = input.current;
  const LifecycleTransition transition =
      retire ? LifecycleTransition::Retire : LifecycleTransition::Supersede;
  const TransitionEvaluation evaluation = evaluate_transition(current.lifecycle, transition);
  if (!evaluation.allowed) {
    if (std::string_view(evaluation.reason) == "ALREADY_SUPERSEDED") {
      reject(plan, OutcomeCode::AlreadyCurrent, "the port configuration is already superseded",
             ExplanationCode::AlreadyCurrent);
      return;
    }
    reject(plan, rejection_for_fenced_state(current.lifecycle),
           std::string("the lifecycle transition is not defined: ") + std::string(evaluation.reason),
           explanation_for_fenced_state(current.lifecycle));
    return;
  }
  capture_expected(current, plan);
  plan.next = current;
  plan.next.lifecycle = evaluation.result;
  advance_lifecycle_generation(plan.next);
  plan.next.status_reason = retire ? "RETIRED" : "SUPERSEDED";
  plan.next.status_detail = request.detail;
  plan.next.applied.verified = false;
  if (retire) {
    if (plan.next.ownership.has_value()) {
      advance_ownership_generation(plan.next);
      plan.next.ownership.reset();
    }
  }
}

void build_ownership(const MutationBuildInput& input, const OwnershipRequest& request,
                     PortMutationKind kind, MutationPlan& plan) {
  const PortRecord& current = input.current;
  if (kind == PortMutationKind::ClaimOwnership) {
    if (current.ownership.has_value()) {
      if (current.ownership->held_by(request.envelope.authority.publisher,
                                     request.envelope.authority.boot,
                                     request.envelope.authority.epoch)) {
        reject(plan, OutcomeCode::AlreadyCurrent,
               "this process incarnation already holds ownership of the port",
               ExplanationCode::AlreadyCurrent);
        return;
      }
      if (current.ownership->exclusive) {
        reject(plan, OutcomeCode::OwnershipConflict,
               "the port is owned exclusively by another authority",
               ExplanationCode::RejectedOwnershipConflict);
        return;
      }
    }
    if (!is_valid(request.owner_kind) || request.owner_kind == PortOwnerKind::Unknown ||
        !request.owner.valid()) {
      reject(plan, OutcomeCode::MalformedRequest,
             "an ownership claim requires an owner kind and an owner identity",
             ExplanationCode::RejectedMalformedRequest);
      return;
    }
  } else {
    if (!current.ownership.has_value()) {
      if (kind == PortMutationKind::ReleaseOwnership) {
        reject(plan, OutcomeCode::AlreadyCurrent, "the port is not owned", ExplanationCode::AlreadyCurrent);
        return;
      }
      reject(plan, OutcomeCode::UnknownOwnership,
             "the port is not owned and cannot be transferred",
             ExplanationCode::RejectedUnauthorizedOwner);
      return;
    }
    if (!current.ownership->held_by(request.envelope.authority.publisher,
                                    request.envelope.authority.boot,
                                    request.envelope.authority.epoch)) {
      reject(plan, OutcomeCode::UnauthorizedOwner,
             "the caller does not hold ownership of the port",
             ExplanationCode::RejectedUnauthorizedOwner);
      return;
    }
    if (kind == PortMutationKind::TransferOwnership &&
        (!is_valid(request.owner_kind) || request.owner_kind == PortOwnerKind::Unknown ||
         !request.owner.valid())) {
      reject(plan, OutcomeCode::MalformedRequest,
             "an ownership transfer requires an owner kind and an owner identity",
             ExplanationCode::RejectedMalformedRequest);
      return;
    }
  }
  capture_expected(current, plan);
  plan.next = current;
  advance_ownership_generation(plan.next);
  if (kind == PortMutationKind::ReleaseOwnership) {
    plan.next.ownership.reset();
    plan.next.status_reason = "OWNERSHIP_RELEASED";
    plan.next.status_detail = request.envelope.provenance_source;
    return;
  }
  const bool delegates = request.delegate_publisher.valid() || request.delegate_boot.valid();
  if (delegates && (!request.delegate_publisher.valid() || !request.delegate_boot.valid())) {
    reject(plan, OutcomeCode::MalformedRequest,
           "a delegated transfer requires both the receiving publisher and its worker boot",
           ExplanationCode::RejectedMalformedRequest);
    return;
  }
  if (delegates && kind != PortMutationKind::TransferOwnership) {
    reject(plan, OutcomeCode::MalformedRequest,
           "only an ownership transfer can name a receiving process incarnation",
           ExplanationCode::RejectedMalformedRequest);
    return;
  }
  PortOwnership ownership;
  ownership.generation = plan.next.generations.ownership;
  ownership.id = PortOwnershipId::from_validated(compose_identifier(
      "own", current.port.value(), ownership.generation.value()));
  ownership.kind = request.owner_kind;
  ownership.owner = request.owner;
  ownership.publisher =
      delegates ? request.delegate_publisher : request.envelope.authority.publisher;
  ownership.boot = delegates ? request.delegate_boot : request.envelope.authority.boot;
  ownership.epoch = request.envelope.authority.epoch;
  ownership.exclusive = request.exclusive;
  plan.next.ownership = ownership;
  plan.next.status_reason = kind == PortMutationKind::TransferOwnership ? "OWNERSHIP_TRANSFERRED"
                                                                        : "OWNERSHIP_CLAIMED";
  plan.next.status_detail = ownership.owner.to_string();
}

void build_applied_evidence(const MutationBuildInput& input, const EvidenceRequest& request,
                            MutationPlan& plan) {
  const PortRecord& current = input.current;
  const AppliedEvidence& evidence = request.evidence;
  if (!is_valid(evidence.outcome)) {
    reject(plan, OutcomeCode::MalformedRequest, "the applied outcome is not a legal value",
           ExplanationCode::RejectedMalformedRequest);
    return;
  }
  if (!current.configuration.has_value()) {
    reject(plan, OutcomeCode::InvalidLifecycleTransition,
           "applied evidence requires a committed configuration generation",
           ExplanationCode::RejectedInvalidLifecycleTransition);
    return;
  }
  if (evidence.generation != current.generations.configuration) {
    reject(plan, OutcomeCode::StaleConfigurationGeneration,
           "the applied evidence describes a configuration generation that is not current",
           ExplanationCode::RejectedStaleConfigurationGeneration);
    return;
  }
  if (current.applied.outcome == evidence.outcome &&
      current.applied.verified == evidence.verified &&
      current.applied.generation == evidence.generation) {
    reject(plan, OutcomeCode::AlreadyCurrent, "the applied evidence is already recorded",
           ExplanationCode::AlreadyCurrent);
    return;
  }
  capture_expected(current, plan);
  plan.next = current;
  plan.next.applied = evidence;
  advance_evidence_generation(plan.next);
  plan.next.applied.evidence_generation = plan.next.generations.evidence;
  if (evidence.outcome == AppliedOutcome::Verified) {
    plan.next.drift = DriftState::InSync;
    plan.next.status_reason = "APPLIED_VERIFIED";
  } else if (evidence.outcome == AppliedOutcome::ApplyFailed) {
    plan.next.drift = DriftState::Drifted;
    plan.next.status_reason = "APPLY_FAILED";
  } else if (evidence.outcome == AppliedOutcome::OutcomeUnknown) {
    plan.next.drift = DriftState::RevalidationRequired;
    if (is_live(plan.next.lifecycle)) {
      plan.next.lifecycle = PortLifecycle::RevalidationRequired;
      advance_lifecycle_generation(plan.next);
    }
    plan.next.status_reason = "APPLY_OUTCOME_UNKNOWN";
  } else if (evidence.outcome == AppliedOutcome::Applied) {
    plan.next.drift = DriftState::Unknown;
    plan.next.status_reason = "APPLIED";
  } else {
    plan.next.drift = DriftState::Unknown;
    plan.next.status_reason = "APPLIED_NOT_ATTEMPTED";
  }
  plan.next.status_detail = evidence.source;
}

void build_drift(const MutationBuildInput& input, const DriftRequest& request, MutationPlan& plan) {
  const PortRecord& current = input.current;
  if (!is_valid(request.observed)) {
    reject(plan, OutcomeCode::MalformedRequest, "the observed drift state is not a legal value",
           ExplanationCode::RejectedMalformedRequest);
    return;
  }
  if (current.drift == request.observed) {
    reject(plan, OutcomeCode::AlreadyCurrent, "the observed drift state is already recorded",
           ExplanationCode::AlreadyCurrent);
    return;
  }
  capture_expected(current, plan);
  plan.next = current;
  plan.next.drift = request.observed;
  plan.next.status_reason = std::string("DRIFT_") + std::string(portfabric::to_string(request.observed));
  plan.next.status_detail = request.detail;
  if (request.observed == DriftState::RevalidationRequired &&
      plan.next.lifecycle != PortLifecycle::Retired) {
    if (is_live(plan.next.lifecycle)) {
      plan.next.lifecycle = PortLifecycle::RevalidationRequired;
      advance_lifecycle_generation(plan.next);
    }
  } else if (request.observed == DriftState::Conflicted &&
             plan.next.lifecycle != PortLifecycle::Retired) {
    const TransitionEvaluation evaluation =
        evaluate_transition(plan.next.lifecycle, LifecycleTransition::Conflict);
    if (evaluation.allowed) {
      plan.next.lifecycle = evaluation.result;
      advance_lifecycle_generation(plan.next);
    }
  }
  if (request.observed == DriftState::InSync) {
    plan.next.applied.verified = true;
  }
}

}  // namespace

MutationResult execute_mutation(EngineImpl& impl, const MutationEnvelope& envelope,
                                 PortMutationKind kind, std::string payload,
                                 const Builder& builder, ProvenanceKind fallback_provenance) {
  MutationResult result;
  if (impl.shutting_down.load()) {
    result.outcome = Outcome::failure(OutcomeCode::ShuttingDown,
                                      "the runtime is shutting down and rejects mutations");
    result.explanation.accepted = false;
    result.explanation.outcome = result.outcome.code();
    result.explanation.add(ExplanationCode::RejectedShuttingDown, envelope.port.to_string());
    return result;
  }
  if (!envelope.port.valid()) {
    result.outcome = Outcome::failure(OutcomeCode::MalformedRequest,
                                      "the request does not carry a valid port identity");
    result.explanation.accepted = false;
    result.explanation.outcome = result.outcome.code();
    result.explanation.add(ExplanationCode::RejectedMalformedRequest, "port identity");
    return result;
  }
  if (!envelope.authority.valid()) {
    result.outcome = Outcome::failure(
        OutcomeCode::MalformedRequest,
        "the request does not carry a complete authority context");
    result.explanation.accepted = false;
    result.explanation.outcome = result.outcome.code();
    result.explanation.add(ExplanationCode::RejectedMalformedRequest, "authority context");
    return result;
  }
  const Outcome authority = impl.check_authority(envelope.authority);
  if (!authority.ok()) {
    result.outcome = authority;
    result.explanation.accepted = false;
    result.explanation.outcome = authority.code();
    result.explanation.summary = authority.message();
    result.explanation.add(explain_code_for(authority.code()), authority.to_string());
    return result;
  }

  const Digest payload_digest = digest_bytes(payload);
  PortRecord snapshot;
  CapabilityBinding capability;
  std::optional<DeviceGeneration> device_generation;
  bool needs_configuration_expectation = false;
  {
    std::lock_guard<std::mutex> profiles_lock(impl.profiles_mutex);
    std::shared_lock<std::shared_mutex> records_lock(impl.records_mutex);
    const auto found = impl.records.find(envelope.port);
    if (found == impl.records.end()) {
      result.outcome = Outcome::failure(OutcomeCode::UnknownPort,
                                        "the port identity is not bound to this runtime");
      result.explanation.accepted = false;
      result.explanation.outcome = result.outcome.code();
      result.explanation.add(ExplanationCode::RejectedUnknownPort, envelope.port.to_string());
      return result;
    }
    snapshot = found->second;
  }

  if (snapshot.lifecycle == PortLifecycle::Retired && kind != PortMutationKind::Retire) {
    result.outcome = Outcome::failure(OutcomeCode::PortRetired,
                                      "the port is retired and can never be resurrected");
    result.explanation.accepted = false;
    result.explanation.outcome = result.outcome.code();
    result.explanation.add(ExplanationCode::RejectedPortRetired, envelope.port.to_string());
    return result;
  }

  // A repeated retirement of an already retired port changes nothing: report the
  // current state instead of demanding ownership that retirement already ended.
  if (snapshot.lifecycle == PortLifecycle::Retired && kind == PortMutationKind::Retire) {
    result.outcome = Outcome::current("the port is already retired");
    result.lifecycle = snapshot.lifecycle;
    result.configuration_generation = snapshot.generations.configuration;
    result.ownership_generation = snapshot.generations.ownership;
    result.record_digest = digest_record(snapshot);
    result.explanation.accepted = true;
    result.explanation.outcome = OutcomeCode::AlreadyCurrent;
    result.explanation.summary = result.outcome.message();
    result.explanation.add(ExplanationCode::AlreadyCurrent, envelope.port.to_string());
    return result;
  }

  if (snapshot.last_mutation.valid() &&
      snapshot.last_mutation.attempt == envelope.authority.attempt) {
    if (snapshot.last_mutation.payload_digest == payload_digest_of(payload_digest)) {
      result.outcome = Outcome::idempotent(
          "the exact mutation attempt with an identical payload is already committed");
      result.lifecycle = snapshot.lifecycle;
      result.configuration_generation = snapshot.generations.configuration;
      result.ownership_generation = snapshot.generations.ownership;
      result.record_digest = digest_record(snapshot);
      result.explanation.accepted = true;
      result.explanation.outcome = OutcomeCode::Idempotent;
      result.explanation.summary = result.outcome.message();
      result.explanation.add(ExplanationCode::IdempotentReplay,
                             "attempt " + envelope.authority.attempt.to_string() +
                                 " already committed generation " +
                                 snapshot.generations.configuration.to_string());
      return result;
    }
    result.outcome = Outcome::failure(
        OutcomeCode::ConflictDetected,
        "the mutation attempt identity was already used with a different payload");
    result.explanation.accepted = false;
    result.explanation.outcome = result.outcome.code();
    result.explanation.add(ExplanationCode::RejectedConflictDetected,
                           "attempt " + envelope.authority.attempt.to_string() + " reused");
    return result;
  }

  // Generation expectations carried by the request.

  if (envelope.authority.expected_topology.valid()) {
    TopologyGeneration current_topology;
    {
      std::lock_guard<std::mutex> authority_lock(impl.authority_mutex);
      current_topology = impl.topology;
    }
    if (envelope.authority.expected_topology != current_topology) {
      result.outcome = Outcome::failure(OutcomeCode::StaleTopologyGeneration,
                                        "the request expects a different topology generation");
      result.explanation.accepted = false;
      result.explanation.outcome = result.outcome.code();
      result.explanation.add(ExplanationCode::RejectedStaleTopologyGeneration,
                             "expected " + envelope.authority.expected_topology.to_string() +
                                 " observed " + current_topology.to_string());
      return result;
    }
  }
  if (envelope.authority.expected_capability.valid() &&
      envelope.authority.expected_capability != snapshot.generations.capability) {
    result.outcome = Outcome::failure(OutcomeCode::StaleCapabilityGeneration,
                                      "the request expects a different capability generation");
    result.explanation.accepted = false;
    result.explanation.outcome = result.outcome.code();
    result.explanation.add(ExplanationCode::RejectedStaleCapabilityGeneration,
                           "expected " + envelope.authority.expected_capability.to_string() +
                               " observed " + snapshot.generations.capability.to_string());
    return result;
  }
  switch (kind) {
    case PortMutationKind::Configure:
    case PortMutationKind::Reconfigure:
    case PortMutationKind::AssignProfile:
    case PortMutationKind::Reconcile:
      needs_configuration_expectation = true;
      break;
    default:
      needs_configuration_expectation = false;
      break;
  }
  // A configuration expectation is required by every mutation that produces a new
  // configuration generation, and it is always enforced when the caller supplies
  // one, whatever the kind: a caller that states what it believes must be right.
  const bool expectation_stated = envelope.authority.expected_configuration.valid();
  if ((needs_configuration_expectation || expectation_stated) &&
      envelope.authority.expected_configuration != snapshot.generations.configuration) {
    result.outcome = Outcome::failure(
        OutcomeCode::StaleConfigurationGeneration,
        "the request expects a configuration generation that is not current");
    result.outcome.with("expected", envelope.authority.expected_configuration.valid()
                                        ? envelope.authority.expected_configuration.to_string()
                                        : std::string("none"))
        .with("current", snapshot.generations.configuration.valid()
                             ? snapshot.generations.configuration.to_string()
                             : std::string("none"));
    result.explanation.accepted = false;
    result.explanation.outcome = result.outcome.code();
    result.explanation.add(ExplanationCode::RejectedStaleConfigurationGeneration,
                           result.outcome.message());
    return result;
  }

  // Ownership gate. Ownership means mutation authority: a port owned by a live
  // process incarnation rejects mutations from any other authority.
  const bool ownership_exempt = kind == PortMutationKind::ClaimOwnership;
  if (impl.config.require_ownership && !ownership_exempt) {
    if (!snapshot.ownership.has_value()) {
      if (snapshot.configuration.has_value()) {
        result.outcome = Outcome::failure(OutcomeCode::UnauthorizedOwner,
                                          "the port has no owner and therefore no mutation "
                                          "authority is delegated");
        result.explanation.accepted = false;
        result.explanation.outcome = result.outcome.code();
        result.explanation.add(ExplanationCode::RejectedUnauthorizedOwner,
                               "ownership is required for mutation");
        return result;
      }
    } else {
      if (envelope.authority.ownership.valid() &&
          envelope.authority.ownership != snapshot.ownership->id) {
        result.outcome = Outcome::failure(OutcomeCode::StaleOwnershipGeneration,
                                          "the request expects a different ownership identity");
        result.explanation.accepted = false;
        result.explanation.outcome = result.outcome.code();
        result.explanation.add(ExplanationCode::RejectedStaleOwnershipGeneration,
                               "expected " + envelope.authority.ownership.to_string() +
                                   " observed " + snapshot.ownership->id.to_string());
        return result;
      }
      if (envelope.authority.ownership_generation.valid() &&
          envelope.authority.ownership_generation != snapshot.generations.ownership) {
        result.outcome = Outcome::failure(OutcomeCode::StaleOwnershipGeneration,
                                          "the request expects a different ownership generation");
        result.explanation.accepted = false;
        result.explanation.outcome = result.outcome.code();
        result.explanation.add(ExplanationCode::RejectedStaleOwnershipGeneration,
                               "expected " +
                                   envelope.authority.ownership_generation.to_string() +
                                   " observed " + snapshot.generations.ownership.to_string());
        return result;
      }
      if (kind != PortMutationKind::TransferOwnership &&
          kind != PortMutationKind::ReleaseOwnership &&
          !snapshot.ownership->held_by(envelope.authority.publisher, envelope.authority.boot,
                                       envelope.authority.epoch)) {
        result.outcome = Outcome::failure(
            OutcomeCode::UnauthorizedOwner,
            "the caller does not hold control ownership of this port");
        result.outcome.with("owner", snapshot.ownership->owner.to_string())
            .with("publisher", snapshot.ownership->publisher.to_string())
            .with("boot", snapshot.ownership->boot.to_string());
        result.explanation.accepted = false;
        result.explanation.outcome = result.outcome.code();
        result.explanation.add(ExplanationCode::RejectedUnauthorizedOwner,
                               result.outcome.message());
        return result;
      }
    }
  } else if (impl.config.require_ownership && ownership_exempt) {
    // A claim still requires the caller to be a registered live incarnation,
    // which check_authority already established.
  }

  // Resolve the exact generations and evidence the builder may consult.
  CoordinatorEpoch current_epoch;
  TopologyGeneration current_topology;
  {
    std::lock_guard<std::mutex> authority_lock(impl.authority_mutex);
    current_epoch = impl.epoch;
    current_topology = impl.topology;
    const auto device = impl.devices.find(snapshot.parent_device);
    if (device != impl.devices.end()) {
      device_generation = device->second.generation;
    }
  }

  // The parent device may have been replaced since the configuration was bound.
  // A configuration bound to a superseded device generation is fenced until it is
  // explicitly reconciled; it never silently becomes authoritative for the
  // replacement device.
  const bool reconcile_kind = kind == PortMutationKind::Reconcile ||
                              kind == PortMutationKind::Supersede ||
                              kind == PortMutationKind::Retire ||
                              kind == PortMutationKind::BindPort;
  if (!reconcile_kind && device_generation.has_value()) {
    if (envelope.authority.expected_device.valid() &&
        envelope.authority.expected_device != *device_generation) {
      result.outcome = Outcome::failure(OutcomeCode::StaleDeviceGeneration,
                                        "the request expects a device generation that was replaced");
      result.outcome.with("expected", envelope.authority.expected_device.to_string())
          .with("current", device_generation->to_string());
      result.explanation.accepted = false;
      result.explanation.outcome = result.outcome.code();
      result.explanation.add(ExplanationCode::RejectedStaleDeviceGeneration,
                             result.outcome.message());
      return result;
    }
    if (*device_generation != snapshot.generations.device) {
      result.outcome = Outcome::failure(
          OutcomeCode::StaleDeviceGeneration,
          "the configuration is bound to a device generation that has been replaced");
      result.outcome.with("bound", snapshot.generations.device.valid()
                                       ? snapshot.generations.device.to_string()
                                       : std::string("none"))
          .with("current", device_generation->to_string());
      result.explanation.accepted = false;
      result.explanation.outcome = result.outcome.code();
      result.explanation.summary = result.outcome.message();
      result.explanation.add(ExplanationCode::RejectedStaleDeviceGeneration,
                             "explicit reconciliation is required");
      return result;
    }
  }
  if (kind == PortMutationKind::Configure || kind == PortMutationKind::Reconfigure ||
      kind == PortMutationKind::AssignProfile || kind == PortMutationKind::Reconcile) {
    Outcome capability_failure;
    capability = impl.resolve_capabilities(snapshot, capability_failure);
    if (!capability_failure.ok()) {
      result.outcome = capability_failure;
      result.explanation.accepted = false;
      result.explanation.outcome = capability_failure.code();
      result.explanation.summary = capability_failure.message();
      result.explanation.add(explain_code_for(capability_failure.code()),
                             capability_failure.to_string());
      return result;
    }
  }

  MutationPlan plan;
  {
    // The plan is built from the record the request was validated against, not
    // from a second, later read. Building from a later read would let two
    // requests that raced on the same generation both pass the compare-and-commit
    // check, which is exactly the corruption the check exists to prevent.
    std::lock_guard<std::mutex> profiles_lock(impl.profiles_mutex);
    MutationBuildInput input{snapshot, current_epoch, current_topology, device_generation,
                             capability, kind, impl.profiles, impl.config};
    builder(input, plan);
  }

  if (plan.failure == OutcomeCode::AlreadyCurrent) {
    result.outcome = Outcome::current(plan.failure_detail);
    result.lifecycle = snapshot.lifecycle;
    result.configuration_generation = snapshot.generations.configuration;
    result.ownership_generation = snapshot.generations.ownership;
    result.record_digest = digest_record(snapshot);
    result.explanation.accepted = true;
    result.explanation.outcome = OutcomeCode::AlreadyCurrent;
    result.explanation.summary = plan.failure_detail;
    result.explanation.add(ExplanationCode::AlreadyCurrent, plan.failure_detail);
    return result;
  }
  if (plan.failure != OutcomeCode::Ok) {
    result.outcome = Outcome::failure(plan.failure, plan.failure_detail);
    result.lifecycle = snapshot.lifecycle;
    result.configuration_generation = snapshot.generations.configuration;
    result.ownership_generation = snapshot.generations.ownership;
    result.record_digest = digest_record(snapshot);
    result.explanation.accepted = false;
    result.explanation.outcome = plan.failure;
    result.explanation.summary = plan.failure_detail;
    result.explanation.add(plan.failure_explanation, plan.failure_detail);
    return result;
  }

  std::string validation_error;
  if (!plan.next.validate(validation_error)) {
    result.outcome = Outcome::failure(OutcomeCode::InvalidConfiguration, validation_error);
    result.explanation.accepted = false;
    result.explanation.outcome = result.outcome.code();
    result.explanation.add(ExplanationCode::RejectedInvalidConfiguration, validation_error);
    return result;
  }
  if (plan.requires_capability_validation) {
    if (!plan.next.configuration.has_value()) {
      result.outcome = Outcome::failure(OutcomeCode::InternalError,
                                        "a capability validated plan carries no configuration");
      result.explanation.accepted = false;
      result.explanation.outcome = result.outcome.code();
      return result;
    }
    const CapabilityValidation validation =
        validate_against_capabilities(*plan.next.configuration, plan.capability);
    if (!validation.accepted) {
      result.outcome = Outcome::failure(validation.code, validation.detail);
      result.outcome.with("reason", validation.reason);
      result.lifecycle = snapshot.lifecycle;
      result.configuration_generation = snapshot.generations.configuration;
      result.explanation.accepted = false;
      result.explanation.outcome = validation.code;
      result.explanation.summary = validation.detail;
      result.explanation.add(explain_code_for(validation.code), validation.reason);
      return result;
    }
  }

  // Every accepted mutation records its attempt identity and payload digest so
  // that exact replay is distinguishable from a stale request.
  plan.next.last_mutation.attempt = envelope.authority.attempt;
  plan.next.last_mutation.payload_digest = payload_digest_of(payload_digest);
  plan.next.last_mutation.result_generation = plan.next.generations.configuration;
  plan.next.provenance = make_provenance(envelope, fallback_provenance);

  AppliedEvidence applied = plan.next.applied;
  bool applied_settled = false;
  if (plan.requires_apply && impl.config.adapter_policy != AdapterPolicy::DesiredStateOnly) {
    if (impl.adapter == nullptr) {
      applied.outcome = AppliedOutcome::Unsupported;
      applied.generation = plan.next.generations.configuration;
      applied.verified = false;
      applied.source = "no-adapter";
      applied_settled = true;
    } else {
      const AdapterApplyResult apply_result =
          impl.adapter->apply(envelope.port, plan.apply_configuration);
      applied.source = std::string(impl.adapter->label());
      applied.generation = plan.next.generations.configuration;
      applied.verified = false;
      switch (apply_result.status) {
        case AdapterApplyStatus::Applied:
          applied.outcome = AppliedOutcome::Applied;
          applied_settled = true;
          break;
        case AdapterApplyStatus::Unsupported:
          applied.outcome = AppliedOutcome::Unsupported;
          applied_settled = true;
          break;
        case AdapterApplyStatus::Failed: {
          result.outcome = Outcome::failure(OutcomeCode::ApplyFailed,
                                            "the device adapter reported a failed apply");
          result.outcome.with("detail", apply_result.detail)
              .with("adapter", std::string(impl.adapter->label()));
          result.lifecycle = snapshot.lifecycle;
          result.configuration_generation = snapshot.generations.configuration;
          result.explanation.accepted = false;
          result.explanation.outcome = result.outcome.code();
          result.explanation.summary = result.outcome.message();
          result.explanation.add(ExplanationCode::RejectedApplyFailed, apply_result.detail);
          return result;
        }
        case AdapterApplyStatus::OutcomeUnknown:
          return impl.record_apply_uncertainty(
              envelope.port, envelope.authority, plan.next.generations.configuration,
              AppliedOutcome::OutcomeUnknown,
              apply_result.detail.empty()
                  ? std::string("the apply outcome is unknown and requires readback")
                  : apply_result.detail,
              OutcomeCode::ApplyOutcomeUnknown);
      }
      if (applied_settled && impl.config.adapter_policy == AdapterPolicy::ApplyAndVerify &&
          apply_result.status == AdapterApplyStatus::Applied) {
        const AdapterReadback readback = impl.adapter->readback(envelope.port);
        switch (readback.status) {
          case AdapterReadbackStatus::Verified:
            applied.outcome = AppliedOutcome::Verified;
            applied.verified = true;
            break;
          case AdapterReadbackStatus::Mismatch:
            result.outcome = Outcome::failure(
                OutcomeCode::ReadbackMismatch,
                "the device does not match the configuration that was applied");
            result.outcome.with("detail", readback.detail);
            result.explanation.accepted = false;
            result.explanation.outcome = result.outcome.code();
            result.explanation.summary = result.outcome.message();
            result.explanation.add(ExplanationCode::RejectedReadbackMismatch, readback.detail);
            (void)impl.record_apply_uncertainty(
                envelope.port, envelope.authority, plan.next.generations.configuration,
                AppliedOutcome::OutcomeUnknown,
                "readback reported a mismatch after a successful apply",
                OutcomeCode::ReadbackMismatch);
            return result;
          case AdapterReadbackStatus::Unknown:
            return impl.record_apply_uncertainty(
                envelope.port, envelope.authority, plan.next.generations.configuration,
                AppliedOutcome::OutcomeUnknown, "the readback could not determine device state",
                OutcomeCode::ApplyOutcomeUnknown);
          case AdapterReadbackStatus::Unsupported:
            applied.outcome = AppliedOutcome::Applied;
            applied.verified = false;
            break;
        }
      }
    }
    if (applied_settled) {
      advance_evidence_generation(plan.next);
      applied.evidence_generation = plan.next.generations.evidence;
      // Drift follows the evidence the adapter actually produced: a verified
      // readback means the device matches desired state, a reported failure means
      // it does not.
      if (applied.outcome == AppliedOutcome::Verified) {
        plan.next.drift = DriftState::InSync;
      } else if (applied.outcome == AppliedOutcome::ApplyFailed) {
        plan.next.drift = DriftState::Drifted;
      } else if (applied.outcome == AppliedOutcome::OutcomeUnknown) {
        plan.next.drift = DriftState::RevalidationRequired;
      }
    }
  } else if (plan.requires_apply) {
    applied.outcome = AppliedOutcome::NotAttempted;
    applied.generation = plan.next.generations.configuration;
    applied.verified = false;
    applied.source = "desired-state-only";
    applied_settled = true;
  }

  MutationResult committed =
      impl.commit(envelope.port, plan.expected, plan, plan.next.provenance, applied, applied_settled);
  if (committed.accepted()) {
    committed.explanation.add(ExplanationCode::ConfigurationCommitted,
                              std::string(portfabric::to_string(kind)) + " committed generation " +
                                  committed.configuration_generation.to_string());
    if (kind == PortMutationKind::Configure || kind == PortMutationKind::Reconfigure ||
        kind == PortMutationKind::AssignProfile) {
      if (plan.next.configuration.has_value() && plan.next.configuration->profile.bound) {
        committed.explanation.add(ExplanationCode::ProfileBound,
                                  plan.next.configuration->profile.to_string());
      }
    }
    if (kind == PortMutationKind::Reconcile) {
      committed.explanation.add(ExplanationCode::DeviceGenerationAdvanced,
                                plan.next.generations.device.to_string());
      committed.explanation.add(ExplanationCode::TopologyGenerationAdvanced,
                                plan.next.generations.topology.to_string());
    }
  }
  return committed;
}

MutationResult PortFabricEngine::bind_port(const PortBindingRequest& request) {
  EngineImpl& impl = *impl_;
  MutationResult result;
  if (impl.shutting_down.load()) {
    result.outcome =
        Outcome::failure(OutcomeCode::ShuttingDown, "the runtime is shutting down");
    result.explanation.accepted = false;
    result.explanation.outcome = result.outcome.code();
    result.explanation.add(ExplanationCode::RejectedShuttingDown, request.port.to_string());
    return result;
  }
  if (!request.port.valid() || !request.parent_device.valid() ||
      !request.device_generation.valid() || !request.attempt.valid()) {
    result.outcome = Outcome::failure(
        OutcomeCode::MalformedRequest,
        "binding requires a port identity, a parent device, a device generation and an attempt "
        "identity");
    result.explanation.accepted = false;
    result.explanation.outcome = result.outcome.code();
    result.explanation.add(ExplanationCode::RejectedMalformedRequest, "binding request");
    return result;
  }
  if (request.entity_class != PortEntityClass::Unknown &&
      !is_valid(request.entity_class)) {
    result.outcome = Outcome::failure(OutcomeCode::WrongEntityClass,
                                      "the entity class is not a legal value");
    result.explanation.accepted = false;
    result.explanation.outcome = result.outcome.code();
    result.explanation.add(ExplanationCode::RejectedWrongEntityClass, "entity class");
    return result;
  }
  {
    std::lock_guard<std::mutex> authority_lock(impl.authority_mutex);
    if (request.epoch != impl.epoch) {
      result.outcome = Outcome::failure(OutcomeCode::StaleCoordinatorEpoch,
                                        "the binding carries a coordinator epoch that is not current");
      result.explanation.accepted = false;
      result.explanation.outcome = result.outcome.code();
      result.explanation.add(ExplanationCode::RejectedStaleCoordinatorEpoch,
                             request.epoch.to_string());
      return result;
    }
  }

  // Resolve capability evidence before taking any lock, so a provider call can
  // never run while the runtime holds its state lock.
  CapabilityBinding provider_evidence;
  bool have_provider_evidence = false;
  {
    std::shared_lock<std::shared_mutex> records_lock(impl.records_mutex);
    const auto found = impl.records.find(request.port);
    if (found == impl.records.end()) {
      PortRecord probe;
      probe.port = request.port;
      probe.parent_device = request.parent_device;
      Outcome failure;
      const CapabilityBinding binding = impl.resolve_capabilities(probe, failure);
      if (failure.ok() && binding.present) {
        provider_evidence = binding;
        have_provider_evidence = true;
      }
    }
  }

  PortRecord record;
  {
    std::unique_lock<std::shared_mutex> records_lock(impl.records_mutex);
    const auto found = impl.records.find(request.port);
    if (found != impl.records.end()) {
      const PortRecord& existing = found->second;
      if (existing.parent_device != request.parent_device) {
        result.outcome = Outcome::failure(
            OutcomeCode::ConflictDetected,
            "the canonical port identity is already bound to a different parent device");
        result.outcome.with("bound_parent", existing.parent_device.to_string())
            .with("requested_parent", request.parent_device.to_string());
        result.explanation.accepted = false;
        result.explanation.outcome = result.outcome.code();
        result.explanation.add(ExplanationCode::RejectedConflictDetected,
                               result.outcome.message());
        return result;
      }
      if (existing.last_mutation.valid() && existing.last_mutation.attempt == request.attempt) {
        result.outcome = Outcome::idempotent("the binding attempt is already committed");
        result.lifecycle = existing.lifecycle;
        result.configuration_generation = existing.generations.configuration;
        result.record_digest = digest_record(existing);
        result.explanation.accepted = true;
        result.explanation.outcome = OutcomeCode::Idempotent;
        result.explanation.add(ExplanationCode::IdempotentReplay, request.port.to_string());
        return result;
      }
      result.outcome = Outcome::current("the canonical port identity is already bound");
      result.lifecycle = existing.lifecycle;
      result.configuration_generation = existing.generations.configuration;
      result.record_digest = digest_record(existing);
      result.explanation.accepted = true;
      result.explanation.outcome = OutcomeCode::AlreadyCurrent;
      result.explanation.add(ExplanationCode::AlreadyCurrent, request.port.to_string());
      return result;
    }
    if (impl.records.size() >= impl.config.port_bound) {
      result.outcome = Outcome::failure(OutcomeCode::ResourceBoundExceeded,
                                        "the runtime is at its configured port bound");
      result.explanation.accepted = false;
      result.explanation.outcome = result.outcome.code();
      result.explanation.add(ExplanationCode::RejectedResourceBoundExceeded,
                             "port bound reached");
      return result;
    }
    record.port = request.port;
    record.parent_device = request.parent_device;
    record.lifecycle = PortLifecycle::Unconfigured;
    record.drift = DriftState::Unknown;
    record.generations.device = request.device_generation;
    record.generations.lifecycle = LifecycleGeneration::from_value(1);
    record.applied.outcome = AppliedOutcome::NotAttempted;
    if (have_provider_evidence) {
      record.capability = provider_evidence;
      record.generations.capability = provider_evidence.generation;
      record.generations.evidence = provider_evidence.evidence_generation;
    }
    record.provenance.kind = request.provenance_kind == ProvenanceKind::Unknown
                                 ? ProvenanceKind::Operator
                                 : request.provenance_kind;
    record.provenance.source = request.provenance_source;
    record.provenance.epoch = request.epoch;
    record.provenance.attempt = request.attempt;
    record.last_mutation.attempt = request.attempt;
    record.last_mutation.payload_digest =
        payload_digest_of(digest_bytes(request.port.to_string() + "|" +
                                       request.parent_device.to_string() + "|" + "BIND_PORT"));
    record.last_mutation.result_generation = PortConfigurationGeneration{};
    record.status_reason = "BOUND";
    std::string validation_error;
    if (!record.validate(validation_error)) {
      result.outcome = Outcome::failure(OutcomeCode::InvalidConfiguration, validation_error);
      result.explanation.accepted = false;
      result.explanation.outcome = result.outcome.code();
      result.explanation.add(ExplanationCode::RejectedInvalidConfiguration, validation_error);
      return result;
    }
    impl.records.emplace(request.port, record);
    impl.index_add(record);
    impl.generation =
        impl.generation.valid() ? impl.generation.next() : EngineGeneration::from_value(1);
  }
  {
    std::lock_guard<std::mutex> authority_lock(impl.authority_mutex);
    const auto existing = impl.devices.find(request.parent_device);
    if (existing == impl.devices.end()) {
      if (impl.devices.size() >= impl.config.device_bound) {
        result.outcome = Outcome::failure(OutcomeCode::ResourceBoundExceeded,
                                          "the device table is at its configured bound");
        result.explanation.accepted = false;
        result.explanation.outcome = result.outcome.code();
        return result;
      }
      DeviceBinding binding;
      binding.device = request.parent_device;
      binding.generation = request.device_generation;
      binding.source = request.provenance_source;
      impl.devices.emplace(request.parent_device, std::move(binding));
    } else if (existing->second.generation < request.device_generation) {
      existing->second.generation = request.device_generation;
      existing->second.source = request.provenance_source;
    } else if (existing->second.generation > request.device_generation) {
      std::unique_lock<std::shared_mutex> records_lock(impl.records_mutex);
      impl.records.erase(request.port);
      impl.index_remove(record);
      result.outcome = Outcome::failure(OutcomeCode::StaleDeviceGeneration,
                                        "the device has been replaced by a newer generation");
      result.explanation.accepted = false;
      result.explanation.outcome = result.outcome.code();
      result.explanation.add(ExplanationCode::RejectedStaleDeviceGeneration,
                             request.device_generation.to_string());
      return result;
    }
  }
  (void)impl.persist_locked_state();
  result.lifecycle = PortLifecycle::Unconfigured;
  result.configuration_generation = record.generations.configuration;
  result.record_digest = digest_record(record);
  result.outcome = Outcome::success("canonical port identity bound");
  result.explanation.accepted = true;
  result.explanation.outcome = OutcomeCode::Ok;
  result.explanation.summary = result.outcome.message();
  result.explanation.add(ExplanationCode::Accepted, request.port.to_string());
  return result;
}

MutationResult PortFabricEngine::configure(const ConfigureRequest& request) {
  return execute_mutation(
      *impl_, request.envelope, PortMutationKind::Configure,
      canonical_payload(request.configuration),
      [&request](const MutationBuildInput& input, MutationPlan& plan) {
        build_configure(input, request, plan);
      },
      ProvenanceKind::Operator);
}

MutationResult PortFabricEngine::reconfigure(const ReconfigureRequest& request) {
  return execute_mutation(
      *impl_, request.envelope, PortMutationKind::Reconfigure,
      canonical_payload(request.configuration),
      [&request](const MutationBuildInput& input, MutationPlan& plan) {
        build_reconfigure(input, request, plan);
      },
      ProvenanceKind::Operator);
}

MutationResult PortFabricEngine::administrative(const AdministrativeRequest& request) {
  return execute_mutation(
      *impl_, request.envelope, PortMutationKind::Administrative,
      std::string("ADMINISTRATIVE|") + std::string(portfabric::to_string(request.op)),
      [&request](const MutationBuildInput& input, MutationPlan& plan) {
        build_administrative(input, request, plan);
      },
      ProvenanceKind::Operator);
}

MutationResult PortFabricEngine::assign_profile(const ProfileRequest& request) {
  const std::string payload =
      std::string("ASSIGN_PROFILE|") + (request.release ? "RELEASE" : request.profile.to_string() +
                                                                      "@" +
                                                                      request.generation.to_string());
  return execute_mutation(
      *impl_, request.envelope, PortMutationKind::AssignProfile, payload,
      [&request](const MutationBuildInput& input, MutationPlan& plan) {
        build_assign_profile(input, request, plan);
      },
      ProvenanceKind::Operator);
}

MutationResult PortFabricEngine::bind_capabilities(const CapabilityRequest& request) {
  const std::string payload =
      std::string("BIND_CAPABILITIES|") + request.binding.generation.to_string() + "|" +
      request.binding.capabilities.to_string();
  return execute_mutation(
      *impl_, request.envelope, PortMutationKind::BindCapabilities, payload,
      [&request](const MutationBuildInput& input, MutationPlan& plan) {
        build_bind_capabilities(input, request, plan);
      },
      ProvenanceKind::Operator);
}

MutationResult PortFabricEngine::reconcile(const ReconcileRequest& request) {
  return execute_mutation(
      *impl_, request.envelope, PortMutationKind::Reconcile,
      std::string("RECONCILE|") + request.detail,
      [&request](const MutationBuildInput& input, MutationPlan& plan) {
        build_reconcile(input, request, plan);
      },
      ProvenanceKind::Reconciliation);
}

MutationResult PortFabricEngine::supersede(const FenceRequest& request) {
  return execute_mutation(
      *impl_, request.envelope, PortMutationKind::Supersede,
      std::string("SUPERSEDE|") + request.detail,
      [&request](const MutationBuildInput& input, MutationPlan& plan) {
        build_fence(input, request, false, plan);
      },
      ProvenanceKind::Operator);
}

MutationResult PortFabricEngine::retire(const FenceRequest& request) {
  return execute_mutation(
      *impl_, request.envelope, PortMutationKind::Retire, std::string("RETIRE|") + request.detail,
      [&request](const MutationBuildInput& input, MutationPlan& plan) {
        build_fence(input, request, true, plan);
      },
      ProvenanceKind::Operator);
}

MutationResult PortFabricEngine::claim_ownership(const OwnershipRequest& request) {
  const std::string payload = std::string("CLAIM_OWNERSHIP|") +
                              std::string(portfabric::to_string(request.owner_kind)) + "|" +
                              request.owner.to_string();
  return execute_mutation(
      *impl_, request.envelope, PortMutationKind::ClaimOwnership, payload,
      [&request](const MutationBuildInput& input, MutationPlan& plan) {
        build_ownership(input, request, PortMutationKind::ClaimOwnership, plan);
      },
      ProvenanceKind::Operator);
}

MutationResult PortFabricEngine::transfer_ownership(const OwnershipRequest& request) {
  const std::string payload = std::string("TRANSFER_OWNERSHIP|") +
                              std::string(portfabric::to_string(request.owner_kind)) + "|" +
                              request.owner.to_string();
  return execute_mutation(
      *impl_, request.envelope, PortMutationKind::TransferOwnership, payload,
      [&request](const MutationBuildInput& input, MutationPlan& plan) {
        build_ownership(input, request, PortMutationKind::TransferOwnership, plan);
      },
      ProvenanceKind::Operator);
}

MutationResult PortFabricEngine::release_ownership(const OwnershipRequest& request) {
  return execute_mutation(
      *impl_, request.envelope, PortMutationKind::ReleaseOwnership, "RELEASE_OWNERSHIP",
      [&request](const MutationBuildInput& input, MutationPlan& plan) {
        build_ownership(input, request, PortMutationKind::ReleaseOwnership, plan);
      },
      ProvenanceKind::Operator);
}

MutationResult PortFabricEngine::record_applied_evidence(const EvidenceRequest& request) {
  const std::string payload = std::string("RECORD_APPLIED_EVIDENCE|") +
                              std::string(portfabric::to_string(request.evidence.outcome)) + "|" +
                              request.evidence.generation.to_string();
  return execute_mutation(
      *impl_, request.envelope, PortMutationKind::RecordAppliedEvidence, payload,
      [&request](const MutationBuildInput& input, MutationPlan& plan) {
        build_applied_evidence(input, request, plan);
      },
      ProvenanceKind::HostAdapter);
}

MutationResult PortFabricEngine::observe_external_state(const DriftRequest& request) {
  const std::string payload = std::string("OBSERVE_EXTERNAL_STATE|") +
                              std::string(portfabric::to_string(request.observed)) + "|" + request.detail;
  return execute_mutation(
      *impl_, request.envelope, PortMutationKind::ObserveExternalState, payload,
      [&request](const MutationBuildInput& input, MutationPlan& plan) {
        build_drift(input, request, plan);
      },
      ProvenanceKind::HostAdapter);
}

}  // namespace portfabric
