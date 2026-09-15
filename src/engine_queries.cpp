#include "engine_internal.hpp"

#include <algorithm>

namespace portfabric {
namespace {

template <class Container>
std::vector<PortId> collect(const Container& container, const typename Container::key_type& key) {
  std::vector<PortId> out;
  const auto found = container.find(key);
  if (found == container.end()) {
    return out;
  }
  out.assign(found->second.begin(), found->second.end());
  return out;
}

}  // namespace

std::size_t PortFabricEngine::port_count() const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  return impl_->records.size();
}

bool PortFabricEngine::contains(const PortId& port) const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  return impl_->records.find(port) != impl_->records.end();
}

std::optional<PortRecord> PortFabricEngine::record(const PortId& port) const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  const auto found = impl_->records.find(port);
  if (found == impl_->records.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::optional<PortConfiguration> PortFabricEngine::configuration(const PortId& port) const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  const auto found = impl_->records.find(port);
  if (found == impl_->records.end() || !found->second.configuration.has_value()) {
    return std::nullopt;
  }
  return found->second.configuration;
}

std::optional<PortLifecycle> PortFabricEngine::lifecycle(const PortId& port) const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  const auto found = impl_->records.find(port);
  if (found == impl_->records.end()) {
    return std::nullopt;
  }
  return found->second.lifecycle;
}

std::optional<AdministrativeState> PortFabricEngine::administrative_state(
    const PortId& port) const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  const auto found = impl_->records.find(port);
  if (found == impl_->records.end() || !found->second.configuration.has_value()) {
    return std::nullopt;
  }
  return found->second.configuration->administrative;
}

std::optional<PortOwnership> PortFabricEngine::ownership(const PortId& port) const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  const auto found = impl_->records.find(port);
  if (found == impl_->records.end()) {
    return std::nullopt;
  }
  return found->second.ownership;
}

std::optional<CapabilityBinding> PortFabricEngine::capability_binding(const PortId& port) const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  const auto found = impl_->records.find(port);
  if (found == impl_->records.end()) {
    return std::nullopt;
  }
  return found->second.capability;
}

std::optional<AppliedEvidence> PortFabricEngine::applied_evidence(const PortId& port) const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  const auto found = impl_->records.find(port);
  if (found == impl_->records.end()) {
    return std::nullopt;
  }
  return found->second.applied;
}

std::optional<DeviceId> PortFabricEngine::parent_device(const PortId& port) const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  const auto found = impl_->records.find(port);
  if (found == impl_->records.end()) {
    return std::nullopt;
  }
  return found->second.parent_device;
}

std::vector<PortId> PortFabricEngine::ports_of_device(const DeviceId& device) const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  return collect(impl_->by_device, device);
}

std::vector<PortId> PortFabricEngine::ports_with_lifecycle(PortLifecycle state) const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  return collect(impl_->by_lifecycle, state);
}

std::vector<PortId> PortFabricEngine::ports_with_administrative_state(
    AdministrativeState state) const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  return collect(impl_->by_administrative, state);
}

std::vector<PortId> PortFabricEngine::ports_requiring_revalidation() const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  std::vector<PortId> out(impl_->revalidation_required.begin(), impl_->revalidation_required.end());
  for (const auto& [port, record] : impl_->records) {
    (void)port;
    if (record.drift == DriftState::RevalidationRequired &&
        impl_->revalidation_required.find(record.port) == impl_->revalidation_required.end()) {
      out.push_back(record.port);
    }
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::vector<PortId> PortFabricEngine::ports_of_owner(const OwnerId& owner) const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  return collect(impl_->by_owner, owner);
}

std::vector<PortId> PortFabricEngine::ports_by_profile(const PortProfileId& profile) const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  return collect(impl_->by_profile, profile);
}

std::vector<PortId> PortFabricEngine::ports_of_aggregate(const AggregateId& aggregate) const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  return collect(impl_->by_aggregate, aggregate);
}

std::vector<PortId> PortFabricEngine::derived_ports(const PortId& parent) const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  return collect(impl_->derived_from_index, parent);
}

std::vector<PortId> PortFabricEngine::ports_with_stale_profile_binding() const {
  std::lock_guard<std::mutex> profiles_lock(impl_->profiles_mutex);
  std::shared_lock<std::shared_mutex> records_lock(impl_->records_mutex);
  std::vector<PortId> out;
  for (const auto& [port, record] : impl_->records) {
    (void)port;
    if (!record.configuration.has_value() || !record.configuration->profile.bound) {
      continue;
    }
    const auto latest = impl_->profiles.latest(record.configuration->profile.id);
    if (!latest.has_value()) {
      out.push_back(record.port);
      continue;
    }
    if (latest->generation != record.configuration->profile.generation) {
      out.push_back(record.port);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

EngineGeneration PortFabricEngine::generation() const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  return impl_->generation;
}

CoordinatorEpoch PortFabricEngine::epoch() const {
  std::lock_guard<std::mutex> lock(impl_->authority_mutex);
  return impl_->epoch;
}

TopologyGeneration PortFabricEngine::topology_generation() const {
  std::lock_guard<std::mutex> lock(impl_->authority_mutex);
  return impl_->topology;
}

Digest PortFabricEngine::state_digest() const {
  std::shared_lock<std::shared_mutex> lock(impl_->records_mutex);
  std::vector<PortRecord> copy;
  copy.reserve(impl_->records.size());
  for (const auto& [port, record] : impl_->records) {
    (void)port;
    copy.push_back(record);
  }
  return digest_records(copy);
}

}  // namespace portfabric
