#include "engine_internal.hpp"

#include <algorithm>

#include "portfabric/platform.hpp"
#include "text.hpp"

namespace portfabric {

std::string_view to_string(PublisherRegistration policy) noexcept {
  switch (policy) {
    case PublisherRegistration::Automatic:
      return "AUTOMATIC";
    case PublisherRegistration::Explicit:
      return "EXPLICIT";
  }
  return "AUTOMATIC";
}

bool parse_publisher_registration(std::string_view text, PublisherRegistration& out) noexcept {
  if (text == "AUTOMATIC") {
    out = PublisherRegistration::Automatic;
    return true;
  }
  if (text == "EXPLICIT") {
    out = PublisherRegistration::Explicit;
    return true;
  }
  return false;
}

bool is_valid(PublisherRegistration policy) noexcept {
  return policy == PublisherRegistration::Automatic || policy == PublisherRegistration::Explicit;
}

std::string compose_identifier(std::string_view prefix, std::string_view identity,
                               std::uint64_t generation) {
  std::string generation_text = detail::format_u64(generation);
  std::string candidate;
  candidate.reserve(prefix.size() + identity.size() + generation_text.size() + 2);
  candidate.append(prefix);
  candidate.push_back('-');
  candidate.append(identity);
  candidate.push_back('-');
  candidate.append(generation_text);
  if (candidate.size() <= max_identifier_length) {
    return candidate;
  }
  // The composed identifier would exceed the grammar bound. Fold the identity
  // into a deterministic hash so the identifier stays bounded, stable and
  // collision resistant for the generation it names.
  std::string digest = detail::format_u64(detail::fnv1a(identity));
  std::string suffix;
  suffix.reserve(digest.size() + generation_text.size() + 2);
  suffix.push_back('-');
  suffix.append(digest);
  suffix.push_back('-');
  suffix.append(generation_text);
  const std::size_t budget =
      max_identifier_length - prefix.size() - 1 - suffix.size();
  candidate.assign(prefix);
  candidate.push_back('-');
  candidate.append(identity.substr(0, budget));
  candidate.append(suffix);
  // The trailing generation digits keep the identifier alphanumeric at the end.
  if (candidate.size() > max_identifier_length) {
    candidate.resize(max_identifier_length);
  }
  return candidate;
}

std::uint64_t payload_digest_of(const Digest& digest) {
  const std::string hex = digest.to_hex();
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < 16 && index < hex.size(); ++index) {
    const char ch = hex[index];
    const std::uint64_t digit = (ch >= '0' && ch <= '9') ? static_cast<std::uint64_t>(ch - '0')
                                                         : static_cast<std::uint64_t>(ch - 'a' + 10);
    value = (value << 4u) | digit;
  }
  return value == 0 ? 1ull : value;
}

std::string render_port_summary(const PortRecord& record) {
  std::string out;
  out.append(record.port.valid() ? record.port.to_string() : std::string("none"));
  out.append(" lifecycle=");
  out.append(portfabric::to_string(record.lifecycle));
  out.append(" drift=");
  out.append(portfabric::to_string(record.drift));
  if (record.configuration.has_value()) {
    out.append(" configuration_generation=");
    out.append(record.configuration->generation.to_string());
  }
  return out;
}

// ---------------------------------------------------------------------------
// Index maintenance
// ---------------------------------------------------------------------------

void EngineImpl::index_add(const PortRecord& record) {
  by_device[record.parent_device].insert(record.port);
  by_lifecycle[record.lifecycle].insert(record.port);
  if (record.configuration.has_value()) {
    by_administrative[record.configuration->administrative].insert(record.port);
    if (record.configuration->profile.bound) {
      by_profile[record.configuration->profile.id].insert(record.port);
    }
    if (record.configuration->aggregation.member) {
      by_aggregate[record.configuration->aggregation.aggregate].insert(record.port);
    }
    if (record.configuration->derived_from.valid()) {
      derived_from_index[record.configuration->derived_from].insert(record.port);
    }
  }
  if (record.ownership.has_value()) {
    by_owner[record.ownership->owner].insert(record.port);
  }
  if (record.lifecycle == PortLifecycle::RevalidationRequired) {
    revalidation_required.insert(record.port);
  }
}

void EngineImpl::index_remove(const PortRecord& record) {
  auto erase_from = [&record](auto& container, const auto& key) {
    const auto found = container.find(key);
    if (found == container.end()) {
      return;
    }
    found->second.erase(record.port);
    if (found->second.empty()) {
      container.erase(found);
    }
  };
  erase_from(by_device, record.parent_device);
  erase_from(by_lifecycle, record.lifecycle);
  if (record.configuration.has_value()) {
    erase_from(by_administrative, record.configuration->administrative);
    if (record.configuration->profile.bound) {
      erase_from(by_profile, record.configuration->profile.id);
    }
    if (record.configuration->aggregation.member) {
      erase_from(by_aggregate, record.configuration->aggregation.aggregate);
    }
    if (record.configuration->derived_from.valid()) {
      erase_from(derived_from_index, record.configuration->derived_from);
    }
  }
  if (record.ownership.has_value()) {
    erase_from(by_owner, record.ownership->owner);
  }
  revalidation_required.erase(record.port);
}

void EngineImpl::index_update(const PortRecord& before, const PortRecord& after) {
  index_remove(before);
  index_add(after);
}

void EngineImpl::reindex_all() {
  by_device.clear();
  by_lifecycle.clear();
  by_administrative.clear();
  by_owner.clear();
  by_profile.clear();
  by_aggregate.clear();
  derived_from_index.clear();
  revalidation_required.clear();
  for (const auto& [port, record] : records) {
    (void)port;
    index_add(record);
  }
}

// ---------------------------------------------------------------------------
// Lifecycle of the engine object
// ---------------------------------------------------------------------------

bool EngineConfig::valid(std::string& error) const {
  if (!coordinator.valid()) {
    error = "the coordinator identity is missing";
    return false;
  }
  if (!epoch.valid()) {
    error = "the coordinator epoch is missing";
    return false;
  }
  if (persistence == PersistenceMode::Automatic && persistence_path.empty()) {
    error = "automatic persistence requires a container path";
    return false;
  }
  if (adapter_policy != AdapterPolicy::DesiredStateOnly && persistence_path.empty()) {
    // Not an error: applying to hardware without a durable container is legal but
    // the operator must have chosen it explicitly by leaving the path empty.
  }
  if (port_bound == 0 || port_bound > max_ports) {
    error = "the port bound must be between 1 and the configured maximum";
    return false;
  }
  if (device_bound == 0 || device_bound > max_devices) {
    error = "the device bound must be between 1 and the configured maximum";
    return false;
  }
  return true;
}

PortFabricEngine::PortFabricEngine(const EngineConfig& config, CapabilityProvider* capability_provider,
                                   PortAdapter* adapter)
    : impl_(std::make_unique<EngineImpl>(config)) {
  impl_->capability_provider = capability_provider;
  impl_->adapter = adapter;
  std::string error;
  if (!config.valid(error)) {
    // An invalid configuration is a programming error, not a runtime condition:
    // the runtime keeps running with a rejected configuration recorded so that
    // every mutation fails closed rather than proceeding with undefined bounds.
    impl_->config.port_bound = 0;
    impl_->config.device_bound = 0;
  }
}

PortFabricEngine::~PortFabricEngine() {
  if (impl_ != nullptr) {
    impl_->shutting_down.store(true);
  }
}

bool PortFabricEngine::shutting_down() const { return impl_->shutting_down.load(); }

void PortFabricEngine::shutdown() { impl_->shutting_down.store(true); }

// ---------------------------------------------------------------------------
// Authority registry
// ---------------------------------------------------------------------------

Outcome PortFabricEngine::begin_epoch(CoordinatorEpoch& assigned) {
  std::lock_guard<std::mutex> lock(impl_->authority_mutex);
  if (!impl_->epoch.valid()) {
    return Outcome::failure(OutcomeCode::InternalError, "the coordinator epoch is not established");
  }
  if (impl_->epoch.exhaustible()) {
    return Outcome::failure(OutcomeCode::ResourceBoundExceeded,
                            "the coordinator epoch can no longer advance");
  }
  impl_->epoch = impl_->epoch.next();
  assigned = impl_->epoch;
  // Every publisher incarnation registered under an earlier epoch is fenced: a
  // process that survived the restart must not keep mutation authority.
  for (auto& [publisher, record] : impl_->publishers) {
    (void)publisher;
    if (record.epoch < impl_->epoch) {
      record.fenced = true;
    }
  }
  // Ownership granted under an earlier epoch does not survive the epoch advance.
  // A restarted coordinator never restores live process authority.
  impl_->release_ownership_before_epoch(impl_->epoch);
  return Outcome::success("coordinator epoch advanced");
}

Outcome PortFabricEngine::register_publisher(const PublisherId& publisher, const WorkerBootId& boot,
                                             CoordinatorEpoch epoch) {
  if (!publisher.valid() || !boot.valid() || !epoch.valid()) {
    return Outcome::failure(OutcomeCode::MalformedRequest,
                            "the publisher registration is incomplete");
  }
  std::lock_guard<std::mutex> lock(impl_->authority_mutex);
  if (epoch != impl_->epoch) {
    return Outcome::failure(OutcomeCode::StaleCoordinatorEpoch,
                            "the publisher registered under a stale coordinator epoch");
  }
  const auto existing = impl_->publishers.find(publisher);
  if (existing != impl_->publishers.end()) {
    if (existing->second.boot == boot) {
      if (existing->second.fenced) {
        return Outcome::failure(OutcomeCode::PublisherFenced,
                                "the publisher incarnation is fenced");
      }
      return Outcome::current("the publisher incarnation is already registered");
    }
    if (existing->second.boot != boot && existing->second.epoch == epoch) {
      // A new incarnation of the same stable identity is a reincarnation: the
      // previous boot is fenced permanently.
      existing->second.boot = boot;
      existing->second.epoch = epoch;
      existing->second.fenced = false;
      return Outcome::success("publisher reincarnated under a fresh boot identity");
    }
    if (existing->second.fenced) {
      existing->second.boot = boot;
      existing->second.epoch = epoch;
      existing->second.fenced = false;
      return Outcome::success("publisher reincarnated under a fresh boot identity");
    }
  }
  if (impl_->publishers.size() >= max_publishers) {
    return Outcome::failure(OutcomeCode::ResourceBoundExceeded,
                            "the publisher table is at its configured bound");
  }
  PublisherRecord record;
  record.publisher = publisher;
  record.boot = boot;
  record.epoch = epoch;
  record.fenced = false;
  impl_->publishers[publisher] = record;
  return Outcome::success("publisher registered");
}

Outcome PortFabricEngine::fence_publisher(const PublisherId& publisher, const WorkerBootId& boot) {
  if (!publisher.valid()) {
    return Outcome::failure(OutcomeCode::MalformedRequest, "the publisher identity is missing");
  }
  std::lock_guard<std::mutex> lock(impl_->authority_mutex);
  const auto existing = impl_->publishers.find(publisher);
  if (existing == impl_->publishers.end()) {
    return Outcome::failure(OutcomeCode::UnknownPublisher, "the publisher is not registered");
  }
  if (boot.valid() && existing->second.boot != boot) {
    return Outcome::failure(OutcomeCode::StaleWorkerBoot,
                            "the publisher is running a different worker boot");
  }
  if (existing->second.fenced) {
    return Outcome::current("the publisher incarnation is already fenced");
  }
  existing->second.fenced = true;
  // Ownership held by the fenced incarnation is released: mutation authority must
  // never outlive the process that held it, and a fresh incarnation of the same
  // agent must be able to claim the port again.
  impl_->release_ownership_of(existing->second.publisher, existing->second.boot);
  return Outcome::success("publisher incarnation fenced");
}

bool PortFabricEngine::is_publisher_fenced(const PublisherId& publisher,
                                           const WorkerBootId& boot) const {
  std::lock_guard<std::mutex> lock(impl_->authority_mutex);
  const auto existing = impl_->publishers.find(publisher);
  if (existing == impl_->publishers.end()) {
    return false;
  }
  if (boot.valid() && existing->second.boot != boot) {
    return false;
  }
  return existing->second.fenced;
}

std::size_t PortFabricEngine::publisher_count() const {
  std::lock_guard<std::mutex> lock(impl_->authority_mutex);
  return impl_->publishers.size();
}

Outcome PortFabricEngine::register_device(const DeviceId& device, DeviceGeneration generation,
                                          std::string_view source) {
  if (!device.valid() || !generation.valid()) {
    return Outcome::failure(OutcomeCode::MalformedRequest,
                            "the device generation registration is incomplete");
  }
  std::string bounded_source(source);
  std::string text_error;
  if (!detail::validate_text(bounded_source, max_name_length, text_error)) {
    return Outcome::failure(OutcomeCode::InvalidText, text_error);
  }
  std::lock_guard<std::mutex> lock(impl_->authority_mutex);
  const auto existing = impl_->devices.find(device);
  if (existing != impl_->devices.end()) {
    if (generation < existing->second.generation) {
      return Outcome::failure(OutcomeCode::StaleDeviceGeneration,
                              "the device generation is older than the recorded generation");
    }
    if (generation == existing->second.generation) {
      return Outcome::current("the device generation is already recorded");
    }
    if (impl_->devices.size() > impl_->config.device_bound) {
      return Outcome::failure(OutcomeCode::ResourceBoundExceeded,
                              "the device table is at its configured bound");
    }
    existing->second.generation = generation;
    existing->second.source = bounded_source;
    return Outcome::success("device generation advanced");
  }
  if (impl_->devices.size() >= impl_->config.device_bound) {
    return Outcome::failure(OutcomeCode::ResourceBoundExceeded,
                            "the device table is at its configured bound");
  }
  DeviceBinding binding;
  binding.device = device;
  binding.generation = generation;
  binding.source = bounded_source;
  impl_->devices.emplace(device, std::move(binding));
  return Outcome::success("device generation recorded");
}

Outcome PortFabricEngine::set_topology_generation(TopologyGeneration generation) {
  if (!generation.valid()) {
    return Outcome::failure(OutcomeCode::MalformedRequest, "the topology generation is missing");
  }
  std::lock_guard<std::mutex> lock(impl_->authority_mutex);
  if (generation < impl_->topology) {
    return Outcome::failure(OutcomeCode::StaleTopologyGeneration,
                            "the topology generation is older than the recorded generation");
  }
  if (generation == impl_->topology) {
    return Outcome::current("the topology generation is already recorded");
  }
  impl_->topology = generation;
  return Outcome::success("topology generation advanced");
}

std::optional<DeviceGeneration> PortFabricEngine::device_generation(const DeviceId& device) const {
  std::lock_guard<std::mutex> lock(impl_->authority_mutex);
  const auto existing = impl_->devices.find(device);
  if (existing == impl_->devices.end()) {
    return std::nullopt;
  }
  return existing->second.generation;
}

std::vector<DeviceBinding> PortFabricEngine::devices() const {
  std::lock_guard<std::mutex> lock(impl_->authority_mutex);
  std::vector<DeviceBinding> out;
  out.reserve(impl_->devices.size());
  for (const auto& [device, binding] : impl_->devices) {
    (void)device;
    out.push_back(binding);
  }
  return out;
}

// ---------------------------------------------------------------------------
// Profiles
// ---------------------------------------------------------------------------

Outcome PortFabricEngine::define_profile(const PortProfile& profile) {
  std::lock_guard<std::mutex> lock(impl_->profiles_mutex);
  return impl_->profiles.define(profile);
}

std::optional<PortProfile> PortFabricEngine::profile(const PortProfileId& id,
                                                     PortProfileGeneration generation) const {
  std::lock_guard<std::mutex> lock(impl_->profiles_mutex);
  return impl_->profiles.get(id, generation);
}

std::optional<PortProfile> PortFabricEngine::latest_profile(const PortProfileId& id) const {
  std::lock_guard<std::mutex> lock(impl_->profiles_mutex);
  return impl_->profiles.latest(id);
}

std::vector<PortProfile> PortFabricEngine::profiles() const {
  std::lock_guard<std::mutex> lock(impl_->profiles_mutex);
  return impl_->profiles.list();
}

std::size_t PortFabricEngine::profile_count() const {
  std::lock_guard<std::mutex> lock(impl_->profiles_mutex);
  return impl_->profiles.generation_count();
}

}  // namespace portfabric
