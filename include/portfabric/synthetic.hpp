#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "portfabric/adapter.hpp"
#include "portfabric/capability.hpp"
#include "portfabric/export.hpp"
#include "portfabric/ids.hpp"

namespace portfabric {

/// Speed class of a synthetic port.
///
/// Every value here is SYNTHETIC. The runtime can model high speed switch
/// silicon, breakout hardware, transceivers and optical ports, but this host has
/// no such hardware: synthetic evidence is always labelled as synthetic and is
/// never presented as physical proof.
enum class SyntheticPortClass : std::uint8_t {
  Management1G = 0,
  Access1G,
  Server10G,
  Server25G,
  Uplink40G,
  Uplink50G,
  Uplink100G,
  Spine200G,
  Spine400G,
  Backbone800G,
  Optical400G,
  InfiniBandHdr200G,
};

PF_EXPORT std::string_view to_string(SyntheticPortClass port_class) noexcept;
PF_EXPORT bool parse_synthetic_class(std::string_view text, SyntheticPortClass& out) noexcept;

/// Injectable adapter fault.
enum class SyntheticFault : std::uint8_t {
  None = 0,
  PermissionDenied,
  DeviceDisappeared,
  DeviceReplaced,
  UnsupportedParameter,
  PartialApply,
  FailedVerification,
  StaleHandle,
  ConcurrentExternalChange,
  AdapterCrash,
  TransportFailure,
};

PF_EXPORT std::string_view to_string(SyntheticFault fault) noexcept;
PF_EXPORT bool parse_synthetic_fault(std::string_view text, SyntheticFault& out) noexcept;

/// One synthetic port.
struct PF_EXPORT SyntheticPortDefinition {
  PortId port;
  DeviceId device;
  PortEntityClass entity_class = PortEntityClass::SyntheticPort;
  PortCapabilities capabilities;
  /// True when the synthetic device models a transceiver.
  bool transceiver_present = true;

  std::string to_string() const;
};

/// Deterministic synthetic fabric inventory.
///
/// The inventory is a model, not a simulator of any vendor product: it exists so
/// that port-governance semantics that need switch-class hardware can be
/// exercised end to end on a workstation. Class models are vendor neutral.
class PF_EXPORT SyntheticFabric {
 public:
  explicit SyntheticFabric(std::string name);

  /// Adds a device with a contiguous range of ports of one class. Identities are
  /// generated deterministically from the label, so two runs of the same proof
  /// produce identical identities.
  DeviceId add_device(std::string_view device_label, SyntheticPortClass port_class,
                      std::uint32_t port_count, PortEntityClass entity_class);

  /// Adds a single port definition.
  void add_port(const SyntheticPortDefinition& definition);

  /// Adds breakout children for a parent port. Children are logical ports derived
  /// from the parent with a one based child index.
  std::vector<PortId> add_breakout_children(const PortId& parent, std::uint32_t children);

  /// The inventory, in the order definitions were added.
  const std::vector<SyntheticPortDefinition>& ports() const noexcept { return ports_; }
  std::size_t port_count() const noexcept { return ports_.size(); }
  std::size_t device_count() const noexcept { return devices_.size(); }

  /// Definition of one port, or nullopt when the port is unknown. Lookup is
  /// indexed, so a large inventory stays usable.
  const SyntheticPortDefinition* find(const PortId& port) const noexcept;

  /// Capability model of a class.
  static PortCapabilities capabilities_for(SyntheticPortClass port_class);

  /// The full capability binding for a port, or nullopt when the port is unknown.
  std::optional<CapabilityBinding> binding_for(const PortId& port) const;

  /// Faults.
  void set_fault(const PortId& port, SyntheticFault fault);
  void set_global_fault(SyntheticFault fault);
  void clear_faults();
  SyntheticFault fault_for(const PortId& port) const;

  /// Records an external change to the synthetic device state, used to exercise
  /// drift detection. Returns false when the port is unknown.
  bool set_observed_configuration(const PortId& port, PortConfiguration configuration);

  /// External state previously recorded for a port, when one was recorded.
  std::optional<PortConfiguration> observed_configuration_for(const PortId& port) const;

  /// Clears a recorded external state.
  void clear_observed_configuration(const PortId& port);

  const std::string& name() const noexcept { return name_; }

  /// Evidence generation the fabric currently reports.
  CapabilityBindingGeneration capability_generation() const noexcept {
    return capability_generation_;
  }

  /// Advances the capability generation, which is how the proofs model Fabric
  /// Capability Registry publishing new truth.
  CapabilityBindingGeneration advance_capability_generation();

  std::string to_string() const;

 private:
  std::string name_;
  std::vector<SyntheticPortDefinition> ports_;
  std::map<PortId, std::size_t> index_;
  std::map<PortId, SyntheticPortClass> classes_;
  std::map<DeviceId, std::size_t> devices_;
  std::map<PortId, SyntheticFault> faults_;
  std::map<PortId, PortConfiguration> observed_configurations_;
  SyntheticFault global_fault_ = SyntheticFault::None;
  CapabilityBindingGeneration capability_generation_ =
      CapabilityBindingGeneration::from_value(1);
};

/// Capability provider over a synthetic fabric.
class PF_EXPORT SyntheticCapabilityProvider : public CapabilityProvider {
 public:
  explicit SyntheticCapabilityProvider(SyntheticFabric& fabric);

  std::string_view label() const noexcept override;
  std::optional<CapabilityBinding> current(const PortId& port, std::string& error) override;

 private:
  SyntheticFabric& fabric_;
};

/// Adapter over a synthetic fabric.
///
/// The adapter keeps a model of "device state" per port, applies configurations
/// to that model and can inject every failure mode the runtime must represent,
/// including an ambiguous completion whose outcome is unknown.
class PF_EXPORT SyntheticAdapter : public PortAdapter {
 public:
  explicit SyntheticAdapter(SyntheticFabric& fabric);

  std::string_view label() const noexcept override;
  AdapterApplyResult apply(const PortId& port, const PortConfiguration& configuration) override;
  AdapterReadback readback(const PortId& port) override;

  /// Configuration the synthetic device currently holds.
  std::optional<PortConfiguration> observed(const PortId& port) const;

  /// Number of completed apply calls. Used by benchmarks and proofs to measure
  /// completed work rather than submitted work.
  std::uint64_t applies_completed() const;
  std::uint64_t readbacks_completed() const;

 private:
  SyntheticFabric& fabric_;
  mutable std::mutex mutex_;
  std::map<PortId, PortConfiguration> observed_;
  std::uint64_t applies_ = 0;
  std::uint64_t readbacks_ = 0;
};

}  // namespace portfabric
