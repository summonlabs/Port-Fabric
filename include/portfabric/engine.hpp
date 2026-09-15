#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "portfabric/adapter.hpp"
#include "portfabric/authority.hpp"
#include "portfabric/capability.hpp"
#include "portfabric/configuration.hpp"
#include "portfabric/diff.hpp"
#include "portfabric/digest.hpp"
#include "portfabric/explanation.hpp"
#include "portfabric/export.hpp"
#include "portfabric/generation.hpp"
#include "portfabric/ids.hpp"
#include "portfabric/outcome.hpp"
#include "portfabric/persistence.hpp"
#include "portfabric/profile.hpp"
#include "portfabric/record.hpp"
#include "portfabric/snapshot.hpp"

namespace portfabric {

/// Implementation type of the runtime. Defined inside the library; consumers
/// only ever hold a reference to the public type.
struct EngineImpl;

/// What the runtime does with an adapter when a configuration changes.
enum class AdapterPolicy : std::uint8_t {
  /// Commit authoritative desired state only. No device is touched.
  DesiredStateOnly = 0,
  /// Apply the change through the adapter and record the apply outcome.
  Apply,
  /// Apply the change and require a readback before reporting verified evidence.
  ApplyAndVerify,
};

/// How a publisher incarnation becomes known to the runtime.
enum class PublisherRegistration : std::uint8_t {
  /// An unknown publisher presenting a complete authority context is registered
  /// on first use. A publisher that is already known and presents a different
  /// worker boot must re-register explicitly, and a fenced incarnation is always
  /// rejected.
  Automatic = 0,
  /// Every publisher incarnation must be registered before it can mutate.
  Explicit,
};

PF_EXPORT std::string_view to_string(PublisherRegistration policy) noexcept;
PF_EXPORT bool parse_publisher_registration(std::string_view text,
                                            PublisherRegistration& out) noexcept;
PF_EXPORT bool is_valid(PublisherRegistration policy) noexcept;

/// When durable state is written.
enum class PersistenceMode : std::uint8_t {
  /// Never write automatically; save() must be called explicitly.
  Manual = 0,
  /// Write after every accepted mutation that advanced a generation.
  Automatic,
};

/// Runtime configuration.
struct PF_EXPORT EngineConfig {
  /// Identity of the authority domain this runtime governs.
  FabricId fabric;
  SiteId site;
  /// Stable identity of this coordinator process.
  PublisherId coordinator;
  /// Epoch of this process incarnation. Must be valid and must not be reused by
  /// a later incarnation of the same coordinator.
  CoordinatorEpoch epoch;

  std::string persistence_path;
  PersistenceMode persistence = PersistenceMode::Manual;
  AdapterPolicy adapter_policy = AdapterPolicy::DesiredStateOnly;

  /// When true, every port must carry capability evidence before a
  /// configuration may be committed. Port Fabric defaults to requiring proof.
  bool require_capability_evidence = true;
  /// When true, mutations require that the caller holds control ownership of the
  /// port.
  bool require_ownership = true;
  /// Publisher incarnation registration policy.
  PublisherRegistration publisher_registration = PublisherRegistration::Automatic;
  /// Bound on the number of ports this runtime will track.
  std::size_t port_bound = max_ports;
  /// Bound on the number of tracked devices.
  std::size_t device_bound = max_devices;

  bool valid(std::string& error) const;
};

/// Binds a canonical port identity to this runtime.
struct PF_EXPORT PortBindingRequest {
  PortId port;
  DeviceId parent_device;
  PortEntityClass entity_class = PortEntityClass::Unknown;
  DeviceGeneration device_generation;
  ProvenanceKind provenance_kind = ProvenanceKind::Operator;
  std::string provenance_source;
  CoordinatorEpoch epoch;
  MutationAttemptId attempt;
};

/// Common envelope of every mutation.
struct PF_EXPORT MutationEnvelope {
  AuthorityContext authority;
  PortId port;
  ProvenanceKind provenance_kind = ProvenanceKind::Operator;
  std::string provenance_source;
};

/// Commits the first configuration generation of an UNCONFIGURED port.
struct PF_EXPORT ConfigureRequest {
  MutationEnvelope envelope;
  PortConfiguration configuration;
};

/// Replaces the committed configuration of a live port.
struct PF_EXPORT ReconfigureRequest {
  MutationEnvelope envelope;
  PortConfiguration configuration;
};

/// Changes the administrative state of a live port.
struct PF_EXPORT AdministrativeRequest {
  MutationEnvelope envelope;
  AdministrativeOp op = AdministrativeOp::Enable;
};

/// Assigns or releases a profile binding.
struct PF_EXPORT ProfileRequest {
  MutationEnvelope envelope;
  PortProfileId profile;
  PortProfileGeneration generation;
  /// When true the profile binding is released instead of assigned.
  bool release = false;
};

/// Binds fresh capability evidence to a port.
struct PF_EXPORT CapabilityRequest {
  MutationEnvelope envelope;
  CapabilityBinding binding;
};

/// Rebinds a port to the current device, topology and capability generations.
struct PF_EXPORT ReconcileRequest {
  MutationEnvelope envelope;
  std::string detail;
};

/// Fences the current configuration generation (supersede) or ends the port's
/// governed life (retire).
struct PF_EXPORT FenceRequest {
  MutationEnvelope envelope;
  std::string detail;
};

/// Claims, transfers or releases control ownership.
struct PF_EXPORT OwnershipRequest {
  MutationEnvelope envelope;
  PortOwnerKind owner_kind = PortOwnerKind::Unknown;
  OwnerId owner;
  bool exclusive = true;
  /// When true the caller releases its own ownership.
  bool release = false;
  /// Process incarnation that will hold the ownership after a transfer.
  ///
  /// Ownership authority is bound to a process incarnation, so delegating to a
  /// controller identity requires naming the incarnation that receives it. When
  /// both are empty the caller keeps holding the port and only the owner identity
  /// is re-designated. A partial pair is rejected.
  PublisherId delegate_publisher;
  WorkerBootId delegate_boot;
};

/// Records evidence about physical application of a configuration generation.
struct PF_EXPORT EvidenceRequest {
  MutationEnvelope envelope;
  AppliedEvidence evidence;
};

/// Records an observation of device state that may differ from desired state.
struct PF_EXPORT DriftRequest {
  MutationEnvelope envelope;
  DriftState observed = DriftState::Unknown;
  std::string detail;
};

/// Result of a mutation, including the deterministic explanation.
struct PF_EXPORT MutationResult {
  Outcome outcome;
  Explanation explanation;
  PortLifecycle lifecycle = PortLifecycle::Unconfigured;
  PortConfigurationGeneration configuration_generation;
  PortOwnershipGeneration ownership_generation;
  Digest record_digest;

  bool accepted() const noexcept { return outcome.accepted(); }
  bool advanced() const noexcept { return outcome.advanced(); }

  std::string to_string() const;
};

/// Authoritative port-governance runtime.
///
/// Thread safety: all public methods are safe to call concurrently from multiple
/// threads. Queries return values, never references into internal storage.
/// Adapter and capability provider callbacks are invoked with no engine lock
/// held.
///
/// Generation semantics: every accepted mutation that changes authoritative
/// semantic state advances the relevant generation and the global engine
/// generation. Exact replay of an already committed mutation returns IDEMPOTENT
/// and advances nothing.
///
/// Lifetime: the runtime outlives nothing it is given. The capability provider
/// and the adapter must outlive the engine and must be destroyed after it.
class PF_EXPORT PortFabricEngine {
 public:
  PortFabricEngine(const EngineConfig& config, CapabilityProvider* capability_provider,
                   PortAdapter* adapter);
  ~PortFabricEngine();

  PortFabricEngine(const PortFabricEngine&) = delete;
  PortFabricEngine& operator=(const PortFabricEngine&) = delete;
  PortFabricEngine(PortFabricEngine&&) = delete;
  PortFabricEngine& operator=(PortFabricEngine&&) = delete;

  // ---- identity binding ---------------------------------------------------

  /// Binds a canonical port identity. This never creates identity: it records
  /// that this runtime knows the identity supplied by Fabric Registry.
  MutationResult bind_port(const PortBindingRequest& request);

  // ---- mutations ----------------------------------------------------------

  MutationResult configure(const ConfigureRequest& request);
  MutationResult reconfigure(const ReconfigureRequest& request);
  MutationResult administrative(const AdministrativeRequest& request);
  MutationResult assign_profile(const ProfileRequest& request);
  MutationResult bind_capabilities(const CapabilityRequest& request);
  MutationResult reconcile(const ReconcileRequest& request);
  MutationResult supersede(const FenceRequest& request);
  MutationResult retire(const FenceRequest& request);
  MutationResult claim_ownership(const OwnershipRequest& request);
  MutationResult transfer_ownership(const OwnershipRequest& request);
  MutationResult release_ownership(const OwnershipRequest& request);
  MutationResult record_applied_evidence(const EvidenceRequest& request);
  MutationResult observe_external_state(const DriftRequest& request);

  // ---- profiles -----------------------------------------------------------

  Outcome define_profile(const PortProfile& profile);
  std::optional<PortProfile> profile(const PortProfileId& id,
                                     PortProfileGeneration generation) const;
  std::optional<PortProfile> latest_profile(const PortProfileId& id) const;
  std::vector<PortProfile> profiles() const;
  std::size_t profile_count() const;
  /// Ports whose committed profile binding is older than the latest defined
  /// generation of that profile. Such ports are not silently re-evaluated.
  std::vector<PortId> ports_with_stale_profile_binding() const;

  // ---- queries ------------------------------------------------------------

  std::size_t port_count() const;
  bool contains(const PortId& port) const;
  std::optional<PortRecord> record(const PortId& port) const;
  std::optional<PortConfiguration> configuration(const PortId& port) const;
  std::optional<PortLifecycle> lifecycle(const PortId& port) const;
  std::optional<AdministrativeState> administrative_state(const PortId& port) const;
  std::optional<PortOwnership> ownership(const PortId& port) const;
  std::optional<CapabilityBinding> capability_binding(const PortId& port) const;
  std::optional<AppliedEvidence> applied_evidence(const PortId& port) const;
  std::optional<DeviceId> parent_device(const PortId& port) const;

  std::vector<PortId> ports_of_device(const DeviceId& device) const;
  std::vector<PortId> ports_with_lifecycle(PortLifecycle lifecycle) const;
  std::vector<PortId> ports_with_administrative_state(AdministrativeState state) const;
  std::vector<PortId> ports_requiring_revalidation() const;
  std::vector<PortId> ports_of_owner(const OwnerId& owner) const;
  std::vector<PortId> ports_by_profile(const PortProfileId& profile) const;
  std::vector<PortId> ports_of_aggregate(const AggregateId& aggregate) const;
  /// Children derived from a parent port through breakout or logical derivation.
  std::vector<PortId> derived_ports(const PortId& parent) const;

  EngineGeneration generation() const;
  CoordinatorEpoch epoch() const;
  TopologyGeneration topology_generation() const;
  std::optional<DeviceGeneration> device_generation(const DeviceId& device) const;
  std::vector<DeviceBinding> devices() const;
  Digest state_digest() const;

  // ---- explanation, diff, snapshot ---------------------------------------

  /// Explains the current state of one port: why it is in this lifecycle, why it
  /// requires revalidation, whether applied evidence is current.
  Explanation explain(const PortId& port) const;

  /// Diff between the committed configuration and a proposed configuration.
  ConfigurationDiff diff(const PortId& port, const PortConfiguration& proposed) const;

  /// Immutable snapshot of the current state.
  Snapshot snapshot() const;

  /// Snapshot of a single port, or of nothing when the port is unknown.
  Snapshot snapshot_of(const PortId& port) const;

  // ---- authority ----------------------------------------------------------

  /// Advances the coordinator epoch for a fresh process incarnation and returns
  /// the new epoch. The previous epoch is fenced: every request carrying it is
  /// rejected.
  Outcome begin_epoch(CoordinatorEpoch& assigned);

  /// Registers a publisher incarnation with the coordinator.
  Outcome register_publisher(const PublisherId& publisher, const WorkerBootId& boot,
                             CoordinatorEpoch epoch);

  /// Fences one publisher incarnation permanently.
  Outcome fence_publisher(const PublisherId& publisher, const WorkerBootId& boot);

  bool is_publisher_fenced(const PublisherId& publisher, const WorkerBootId& boot) const;
  std::size_t publisher_count() const;

  /// Records the device generation reported by Fabric Registry or Fabric
  /// Topology. Advancing a device generation fences configurations bound to an
  /// older generation of that device.
  Outcome register_device(const DeviceId& device, DeviceGeneration generation,
                          std::string_view source);

  /// Records the current topology generation. Advancing it fences configurations
  /// bound to an older structural context.
  Outcome set_topology_generation(TopologyGeneration generation);

  // ---- persistence --------------------------------------------------------

  /// Writes durable state. Never restores live process authority.
  Outcome save();

  /// Loads durable state with conservative recovery.
  ///
  /// Durable desired configuration survives. Process-local authority does not:
  /// publisher incarnations are marked inactive, verified applied evidence is
  /// downgraded to unverified, ownership held by a previous incarnation is
  /// released, and every recovered configuration is placed in
  /// REVALIDATION_REQUIRED until it is reconciled against current generations.
  Outcome load();

  /// True when the runtime is shutting down: further mutations are rejected.
  bool shutting_down() const;

  /// Rejects further mutations. Idempotent.
  void shutdown();

 private:
  std::unique_ptr<EngineImpl> impl_;
};

}  // namespace portfabric
