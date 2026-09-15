#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "portfabric/engine.hpp"
#include "portfabric/persistence.hpp"
#include "portfabric/protocol.hpp"

namespace portfabric {

/// Optimistic concurrency expectation captured by a builder from the record it
/// observed. The commit phase compares every generation before accepting.
struct ExpectedState {
  PortConfigurationGeneration configuration;
  LifecycleGeneration lifecycle;
  AdministrativeGeneration administrative;
  PortOwnershipGeneration ownership;
  EvidenceGeneration evidence;
  MutationAttemptId last_attempt;

  bool matches(const PortRecord& record) const noexcept {
    return record.generations.configuration == configuration &&
           record.generations.lifecycle == lifecycle &&
           record.generations.administrative == administrative &&
           record.generations.ownership == ownership &&
           record.generations.evidence == evidence &&
           record.last_mutation.attempt == last_attempt;
  }

  std::string to_string() const;
};

/// Read-only inputs a mutation builder may consult.
struct MutationBuildInput {
  const PortRecord& current;
  CoordinatorEpoch epoch;
  TopologyGeneration topology;
  std::optional<DeviceGeneration> parent_device_generation;
  CapabilityBinding capability;
  PortMutationKind kind;
  const ProfileRegistry& profiles;
  const EngineConfig& config;
};

/// Accumulated result of a mutation builder.
struct MutationPlan {
  /// Optimistic expectation captured from the record the builder observed.
  ExpectedState expected;
  /// Capability evidence the plan is validated against.
  CapabilityBinding capability;
  /// Proposed record after the mutation.
  PortRecord next;
  /// True when the configuration payload changed semantically.
  bool configuration_changed = false;
  /// True when the builder requires capability validation of the result.
  bool requires_capability_validation = false;
  /// True when the change must be applied through the adapter.
  bool requires_apply = false;
  /// Configuration that must be applied to the device.
  PortConfiguration apply_configuration;
  /// Self describing payload of this mutation, used for replay classification.
  std::string payload;
  /// Set when the builder rejected the mutation.
  OutcomeCode failure = OutcomeCode::Ok;
  std::string failure_detail;
  ExplanationCode failure_explanation = ExplanationCode::RejectedInvalidConfiguration;
};

/// Engine implementation. Declared in the public header and defined here.
struct EngineImpl {
  EngineConfig config;
  CapabilityProvider* capability_provider = nullptr;
  PortAdapter* adapter = nullptr;

  // ---- port records and indexes -------------------------------------------
  mutable std::shared_mutex records_mutex;
  std::unordered_map<PortId, PortRecord> records;
  std::map<DeviceId, std::set<PortId>> by_device;
  std::map<PortLifecycle, std::set<PortId>> by_lifecycle;
  std::map<AdministrativeState, std::set<PortId>> by_administrative;
  std::map<OwnerId, std::set<PortId>> by_owner;
  std::map<PortProfileId, std::set<PortId>> by_profile;
  std::map<AggregateId, std::set<PortId>> by_aggregate;
  std::map<PortId, std::set<PortId>> derived_from_index;
  std::set<PortId> revalidation_required;
  EngineGeneration generation;

  // ---- profiles -----------------------------------------------------------
  mutable std::mutex profiles_mutex;
  ProfileRegistry profiles;

  // ---- authority ----------------------------------------------------------
  mutable std::mutex authority_mutex;
  CoordinatorEpoch epoch;
  TopologyGeneration topology;
  std::map<PublisherId, PublisherRecord> publishers;
  std::map<DeviceId, DeviceBinding> devices;

  // ---- persistence --------------------------------------------------------
  mutable std::mutex persistence_mutex;
  bool durability_dirty = false;

  std::atomic<bool> shutting_down{false};

  explicit EngineImpl(const EngineConfig& engine_config)
      : config(engine_config),
        epoch(engine_config.epoch),
        // The runtime always knows a structural context. Fabric Topology owns the
        // authoritative generation; this baseline records that the runtime has
        // been told generation 1 and nothing newer yet.
        topology(TopologyGeneration::from_value(1)) {}

  // ---- index maintenance (records_mutex must be held) ---------------------
  void index_add(const PortRecord& record);
  void index_remove(const PortRecord& record);
  void index_update(const PortRecord& before, const PortRecord& after);
  void reindex_all();

  /// Authority gate. Returns an outcome that is not ok when the context is
  /// rejected. Never called with any lock held.
  Outcome check_authority(const AuthorityContext& authority);

  /// Resolves capability evidence for a port: bound evidence when present,
  /// otherwise a fresh reading from the provider. Never called with a lock held.
  CapabilityBinding resolve_capabilities(const PortRecord& record, Outcome& failure);

  /// Commits a plan under the records write lock, with compare-and-commit
  /// against the generation expectations. Never called with a lock held.
  MutationResult commit(const PortId& port, const ExpectedState& expected, const MutationPlan& plan,
                        const Provenance& provenance, const AppliedEvidence& applied,
                        bool applied_settled);

  /// Records adapter uncertainty without committing the proposed configuration.
  MutationResult record_apply_uncertainty(const PortId& port, const AuthorityContext& authority,
                                          const PortConfigurationGeneration& generation,
                                          AppliedOutcome outcome, std::string detail,
                                          OutcomeCode code);

  /// Marks a port as requiring revalidation and persists if configured.
  void mark_revalidation(const PortId& port, std::string reason, std::string detail);

  /// Releases every ownership held by one process incarnation. Called with the
  /// authority lock held, which is the documented authority -> records order.
  void release_ownership_of(const PublisherId& publisher, const WorkerBootId& boot);

  /// Releases every ownership granted under an epoch older than the given one.
  void release_ownership_before_epoch(CoordinatorEpoch epoch);

  Outcome persist_locked_state();
};

/// Alias of the mutation builder callback type used by execute_mutation.
using MutationBuilder =
    std::function<void(const MutationBuildInput&, MutationPlan&)>;

/// Runs the full mutation pipeline: authority gate, generation expectations,
/// ownership, locked planning, capability validation, adapter application and
/// compare-and-commit. Never called with a lock held.
MutationResult execute_mutation(EngineImpl& impl, const MutationEnvelope& envelope, PortMutationKind kind,
                                std::string payload, const MutationBuilder& builder,
                                ProvenanceKind fallback_provenance);

/// Composes a bounded identifier from a prefix, an identity and a generation.
/// The result always satisfies the identifier grammar.
std::string compose_identifier(std::string_view prefix, std::string_view identity,
                               std::uint64_t generation);

/// Derives a 64-bit payload digest from a canonical digest.
std::uint64_t payload_digest_of(const Digest& digest);

/// Stable rendering of a drift state for diagnostics.
std::string render_port_summary(const PortRecord& record);

}  // namespace portfabric
