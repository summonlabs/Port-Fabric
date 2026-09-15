#include "wire.hpp"

#include <cstring>

#include "portfabric/limits.hpp"

namespace portfabric::detail {

// ---------------------------------------------------------------------------
// Small field helpers
// ---------------------------------------------------------------------------

void write_id(ByteWriter& writer, const std::string& value) {
  writer.text(value, max_identifier_length);
}

bool read_id(ByteReader& reader, std::string& out) {
  return reader.text(max_identifier_length, out);
}

bool read_bounded_count(ByteReader& reader, std::size_t limit, std::uint32_t& count) {
  if (!reader.u32(count)) {
    return false;
  }
  if (count > limit) {
    return false;
  }
  // Every element occupies at least one byte, so a count larger than the
  // remaining input can never be satisfied. Rejecting here prevents a hostile
  // count from driving a large reservation before decoding fails.
  if (count > reader.remaining()) {
    return false;
  }
  return true;
}


// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

void write_provenance(ByteWriter& writer, const Provenance& provenance) {
  writer.u8(static_cast<std::uint8_t>(provenance.kind));
  writer.text(provenance.source, max_name_length);
  write_id(writer, provenance.publisher.to_string());
  write_id(writer, provenance.boot.to_string());
  writer.u64(provenance.epoch.value());
  write_id(writer, provenance.attempt.to_string());
}

bool read_provenance(ByteReader& reader, Provenance& provenance) {
  std::uint8_t kind = 0;
  if (!reader.u8(kind)) {
    return false;
  }
  if (!is_valid(static_cast<ProvenanceKind>(kind))) {
    return false;
  }
  provenance.kind = static_cast<ProvenanceKind>(kind);
  if (!reader.text(max_name_length, provenance.source)) {
    return false;
  }
  if (!read_strong_id(reader, provenance.publisher)) {
    return false;
  }
  if (!read_strong_id(reader, provenance.boot)) {
    return false;
  }
  std::uint64_t epoch = 0;
  if (!reader.u64(epoch)) {
    return false;
  }
  provenance.epoch = CoordinatorEpoch::from_value(epoch);
  if (!read_strong_id(reader, provenance.attempt)) {
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Capabilities
// ---------------------------------------------------------------------------

void write_capabilities(ByteWriter& writer, const PortCapabilities& capabilities) {
  writer.u8(static_cast<std::uint8_t>(capabilities.certainty));
  writer.u32(static_cast<std::uint32_t>(capabilities.supported_speeds.size()));
  for (const PortSpeed& speed : capabilities.supported_speeds) {
    writer.u64(speed.bits_per_second());
  }
  writer.u32(static_cast<std::uint32_t>(capabilities.lane_modes.size()));
  for (const std::uint32_t lanes : capabilities.lane_modes) {
    writer.u32(lanes);
  }
  writer.u32(capabilities.max_lanes);
  writer.boolean(capabilities.breakout_supported);
  writer.u32(capabilities.max_breakout_children);
  writer.u32(static_cast<std::uint32_t>(capabilities.fec_modes.size()));
  for (const FecMode mode : capabilities.fec_modes) {
    writer.u8(static_cast<std::uint8_t>(mode));
  }
  writer.u32(static_cast<std::uint32_t>(capabilities.autoneg_modes.size()));
  for (const AutonegPolicy policy : capabilities.autoneg_modes) {
    writer.u8(static_cast<std::uint8_t>(policy));
  }
  writer.u32(static_cast<std::uint32_t>(capabilities.pause_modes.size()));
  for (const PauseMode mode : capabilities.pause_modes) {
    writer.u8(static_cast<std::uint8_t>(mode));
  }
  writer.u32(static_cast<std::uint32_t>(capabilities.protocol_families.size()));
  for (const ProtocolFamily family : capabilities.protocol_families) {
    writer.u8(static_cast<std::uint8_t>(family));
  }
  writer.u32(capabilities.capability_mtu_min);
  writer.u32(capabilities.capability_mtu_max);
  writer.u8(static_cast<std::uint8_t>(capabilities.duplex));
  writer.boolean(capabilities.logical_interface_supported);
  writer.u32(capabilities.max_queue_count);
  writer.boolean(capabilities.transceiver_required);
  writer.text(capabilities.transceiver_class, max_name_length);
}

bool read_capabilities(ByteReader& reader, PortCapabilities& capabilities) {
  std::uint8_t certainty = 0;
  if (!reader.u8(certainty)) {
    return false;
  }
  if (!is_valid(static_cast<CapabilityCertainty>(certainty))) {
    return false;
  }
  capabilities.certainty = static_cast<CapabilityCertainty>(certainty);
  std::uint32_t count = 0;
  if (!read_bounded_count(reader, max_capability_entries, count)) {
    return false;
  }
  capabilities.supported_speeds.clear();
  capabilities.supported_speeds.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    std::uint64_t bits = 0;
    if (!reader.u64(bits)) {
      return false;
    }
    const auto speed = PortSpeed::from_bits_per_second(bits);
    if (!speed.has_value()) {
      return false;
    }
    capabilities.supported_speeds.push_back(*speed);
  }
  if (!read_bounded_count(reader, max_mode_entries, count)) {
    return false;
  }
  capabilities.lane_modes.clear();
  capabilities.lane_modes.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    std::uint32_t lanes = 0;
    if (!reader.u32(lanes)) {
      return false;
    }
    capabilities.lane_modes.push_back(lanes);
  }
  if (!reader.u32(capabilities.max_lanes)) {
    return false;
  }
  if (!reader.boolean(capabilities.breakout_supported)) {
    return false;
  }
  if (!reader.u32(capabilities.max_breakout_children)) {
    return false;
  }
  if (!read_bounded_count(reader, max_mode_entries, count)) {
    return false;
  }
  capabilities.fec_modes.clear();
  capabilities.fec_modes.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    std::uint8_t value = 0;
    if (!reader.u8(value) || !is_valid(static_cast<FecMode>(value))) {
      return false;
    }
    capabilities.fec_modes.push_back(static_cast<FecMode>(value));
  }
  if (!read_bounded_count(reader, max_mode_entries, count)) {
    return false;
  }
  capabilities.autoneg_modes.clear();
  capabilities.autoneg_modes.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    std::uint8_t value = 0;
    if (!reader.u8(value) || !is_valid(static_cast<AutonegPolicy>(value))) {
      return false;
    }
    capabilities.autoneg_modes.push_back(static_cast<AutonegPolicy>(value));
  }
  if (!read_bounded_count(reader, max_mode_entries, count)) {
    return false;
  }
  capabilities.pause_modes.clear();
  capabilities.pause_modes.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    std::uint8_t value = 0;
    if (!reader.u8(value) || !is_valid(static_cast<PauseMode>(value))) {
      return false;
    }
    capabilities.pause_modes.push_back(static_cast<PauseMode>(value));
  }
  if (!read_bounded_count(reader, max_mode_entries, count)) {
    return false;
  }
  capabilities.protocol_families.clear();
  capabilities.protocol_families.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    std::uint8_t value = 0;
    if (!reader.u8(value) || !is_valid(static_cast<ProtocolFamily>(value))) {
      return false;
    }
    capabilities.protocol_families.push_back(static_cast<ProtocolFamily>(value));
  }
  if (!reader.u32(capabilities.capability_mtu_min)) {
    return false;
  }
  if (!reader.u32(capabilities.capability_mtu_max)) {
    return false;
  }
  std::uint8_t duplex = 0;
  if (!reader.u8(duplex) || !is_valid(static_cast<DuplexMode>(duplex))) {
    return false;
  }
  capabilities.duplex = static_cast<DuplexMode>(duplex);
  if (!reader.boolean(capabilities.logical_interface_supported)) {
    return false;
  }
  if (!reader.u32(capabilities.max_queue_count)) {
    return false;
  }
  if (!reader.boolean(capabilities.transceiver_required)) {
    return false;
  }
  if (!reader.text(max_name_length, capabilities.transceiver_class)) {
    return false;
  }
  std::string error;
  return capabilities.validate(error);
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void write_configuration(ByteWriter& writer, const PortConfiguration& configuration) {
  write_id(writer, configuration.record.to_string());
  write_id(writer, configuration.port.to_string());
  write_id(writer, configuration.parent_device.to_string());
  writer.u64(configuration.device_generation.value());
  writer.u64(configuration.topology_generation.value());
  writer.u64(configuration.generation.value());
  writer.u8(static_cast<std::uint8_t>(configuration.administrative));
  writer.u8(static_cast<std::uint8_t>(configuration.mode));
  writer.u8(static_cast<std::uint8_t>(configuration.protocol));
  writer.u8(static_cast<std::uint8_t>(configuration.speed.selection));
  writer.u64(configuration.speed.rate.bits_per_second());
  writer.u8(static_cast<std::uint8_t>(configuration.duplex));
  writer.u32(configuration.mtu.value());
  writer.u32(configuration.lanes.lanes());
  writer.u64(configuration.lanes.per_lane().bits_per_second());
  writer.u8(static_cast<std::uint8_t>(configuration.breakout));
  writer.u8(static_cast<std::uint8_t>(configuration.autoneg));
  writer.u8(static_cast<std::uint8_t>(configuration.fec));
  writer.u8(static_cast<std::uint8_t>(configuration.pause));
  writer.boolean(configuration.aggregation.member);
  write_id(writer, configuration.aggregation.aggregate.to_string());
  writer.u64(configuration.aggregation.generation.value());
  writer.boolean(configuration.aggregation.administrative_participation);
  writer.boolean(configuration.profile.bound);
  write_id(writer, configuration.profile.id.to_string());
  writer.u64(configuration.profile.generation.value());
  writer.text(configuration.role, max_name_length);
  write_id(writer, configuration.logical_identity.to_string());
  write_id(writer, configuration.derived_from.to_string());
  writer.u32(configuration.child_index);
  write_provenance(writer, configuration.provenance);
  writer.u64(configuration.capability_generation.value());
  writer.u64(configuration.evidence_generation.value());
}

bool read_configuration(ByteReader& reader, PortConfiguration& configuration) {
  if (!read_strong_id(reader, configuration.record)) {
    return false;
  }
  if (!read_strong_id(reader, configuration.port)) {
    return false;
  }
  if (!read_strong_id(reader, configuration.parent_device)) {
    return false;
  }
  std::uint64_t value = 0;
  if (!reader.u64(value)) {
    return false;
  }
  configuration.device_generation = DeviceGeneration::from_value(value);
  if (!reader.u64(value)) {
    return false;
  }
  configuration.topology_generation = TopologyGeneration::from_value(value);
  if (!reader.u64(value)) {
    return false;
  }
  configuration.generation = PortConfigurationGeneration::from_value(value);
  std::uint8_t byte = 0;
  if (!reader.u8(byte) || !is_valid(static_cast<AdministrativeState>(byte))) {
    return false;
  }
  configuration.administrative = static_cast<AdministrativeState>(byte);
  if (!reader.u8(byte) || !is_valid(static_cast<PortMode>(byte))) {
    return false;
  }
  configuration.mode = static_cast<PortMode>(byte);
  if (!reader.u8(byte) || !is_valid(static_cast<ProtocolFamily>(byte))) {
    return false;
  }
  configuration.protocol = static_cast<ProtocolFamily>(byte);
  if (!reader.u8(byte) || !is_valid(static_cast<SpeedSelection>(byte))) {
    return false;
  }
  configuration.speed.selection = static_cast<SpeedSelection>(byte);
  if (!reader.u64(value)) {
    return false;
  }
  if (value != 0) {
    const auto rate = PortSpeed::from_bits_per_second(value);
    if (!rate.has_value()) {
      return false;
    }
    configuration.speed.rate = *rate;
  }
  if (!reader.u8(byte) || !is_valid(static_cast<DuplexMode>(byte))) {
    return false;
  }
  configuration.duplex = static_cast<DuplexMode>(byte);
  std::uint32_t mtu_value = 0;
  if (!reader.u32(mtu_value)) {
    return false;
  }
  const auto mtu = Mtu::create(mtu_value);
  if (!mtu.has_value()) {
    return false;
  }
  configuration.mtu = *mtu;
  std::uint32_t lanes = 0;
  if (!reader.u32(lanes)) {
    return false;
  }
  std::uint64_t per_lane_bits = 0;
  if (!reader.u64(per_lane_bits)) {
    return false;
  }
  const auto per_lane = PortSpeed::from_bits_per_second(per_lane_bits);
  if (!per_lane.has_value()) {
    return false;
  }
  const auto lane_configuration = LaneConfiguration::create(lanes, *per_lane);
  if (!lane_configuration.has_value()) {
    return false;
  }
  configuration.lanes = *lane_configuration;
  if (!reader.u8(byte) || !is_valid(static_cast<BreakoutMode>(byte))) {
    return false;
  }
  configuration.breakout = static_cast<BreakoutMode>(byte);
  if (!reader.u8(byte) || !is_valid(static_cast<AutonegPolicy>(byte))) {
    return false;
  }
  configuration.autoneg = static_cast<AutonegPolicy>(byte);
  if (!reader.u8(byte) || !is_valid(static_cast<FecMode>(byte))) {
    return false;
  }
  configuration.fec = static_cast<FecMode>(byte);
  if (!reader.u8(byte) || !is_valid(static_cast<PauseMode>(byte))) {
    return false;
  }
  configuration.pause = static_cast<PauseMode>(byte);
  if (!reader.boolean(configuration.aggregation.member)) {
    return false;
  }
  if (!read_strong_id(reader, configuration.aggregation.aggregate)) {
    return false;
  }
  if (!reader.u64(value)) {
    return false;
  }
  configuration.aggregation.generation = MembershipGeneration::from_value(value);
  if (!reader.boolean(configuration.aggregation.administrative_participation)) {
    return false;
  }
  if (!reader.boolean(configuration.profile.bound)) {
    return false;
  }
  if (!read_strong_id(reader, configuration.profile.id)) {
    return false;
  }
  if (!reader.u64(value)) {
    return false;
  }
  configuration.profile.generation = PortProfileGeneration::from_value(value);
  if (!reader.text(max_name_length, configuration.role)) {
    return false;
  }
  if (!read_strong_id(reader, configuration.logical_identity)) {
    return false;
  }
  if (!read_strong_id(reader, configuration.derived_from)) {
    return false;
  }
  if (!reader.u32(configuration.child_index)) {
    return false;
  }
  if (!read_provenance(reader, configuration.provenance)) {
    return false;
  }
  if (!reader.u64(value)) {
    return false;
  }
  configuration.capability_generation = CapabilityBindingGeneration::from_value(value);
  if (!reader.u64(value)) {
    return false;
  }
  configuration.evidence_generation = EvidenceGeneration::from_value(value);
  // A configuration payload is a proposal: identity, generation, provenance and
  // the derived topology binding are supplied by the runtime that accepts it, so
  // full semantic validation happens there. Every persisted configuration is
  // still validated, because the record codec validates the whole record.
  return true;
}

// ---------------------------------------------------------------------------
// Ownership, capabilities binding, records
// ---------------------------------------------------------------------------

void write_ownership(ByteWriter& writer, const PortOwnership& ownership) {
  write_id(writer, ownership.id.to_string());
  writer.u8(static_cast<std::uint8_t>(ownership.kind));
  write_id(writer, ownership.owner.to_string());
  writer.u64(ownership.generation.value());
  write_id(writer, ownership.publisher.to_string());
  write_id(writer, ownership.boot.to_string());
  writer.u64(ownership.epoch.value());
  writer.boolean(ownership.exclusive);
}

bool read_ownership(ByteReader& reader, PortOwnership& ownership) {
  if (!read_strong_id(reader, ownership.id)) {
    return false;
  }
  std::uint8_t kind = 0;
  if (!reader.u8(kind) || !is_valid(static_cast<PortOwnerKind>(kind))) {
    return false;
  }
  ownership.kind = static_cast<PortOwnerKind>(kind);
  if (!read_strong_id(reader, ownership.owner)) {
    return false;
  }
  std::uint64_t value = 0;
  if (!reader.u64(value)) {
    return false;
  }
  ownership.generation = PortOwnershipGeneration::from_value(value);
  if (!read_strong_id(reader, ownership.publisher)) {
    return false;
  }
  if (!read_strong_id(reader, ownership.boot)) {
    return false;
  }
  if (!reader.u64(value)) {
    return false;
  }
  ownership.epoch = CoordinatorEpoch::from_value(value);
  if (!reader.boolean(ownership.exclusive)) {
    return false;
  }
  return ownership.valid();
}

void write_capability_binding(ByteWriter& writer, const CapabilityBinding& binding) {
  writer.boolean(binding.present);
  writer.u64(binding.generation.value());
  writer.u64(binding.evidence_generation.value());
  write_id(writer, binding.evidence.to_string());
  writer.text(binding.source, max_name_length);
  write_capabilities(writer, binding.capabilities);
}

bool read_capability_binding(ByteReader& reader, CapabilityBinding& binding) {
  if (!reader.boolean(binding.present)) {
    return false;
  }
  std::uint64_t value = 0;
  if (!reader.u64(value)) {
    return false;
  }
  binding.generation = CapabilityBindingGeneration::from_value(value);
  if (!reader.u64(value)) {
    return false;
  }
  binding.evidence_generation = EvidenceGeneration::from_value(value);
  if (!read_strong_id(reader, binding.evidence)) {
    return false;
  }
  if (!reader.text(max_name_length, binding.source)) {
    return false;
  }
  if (!read_capabilities(reader, binding.capabilities)) {
    return false;
  }
  return binding.valid();
}

void write_generations(ByteWriter& writer, const GenerationVector& generations) {
  writer.u64(generations.configuration.value());
  writer.u64(generations.administrative.value());
  writer.u64(generations.ownership.value());
  writer.u64(generations.capability.value());
  writer.u64(generations.topology.value());
  writer.u64(generations.device.value());
  writer.u64(generations.lifecycle.value());
  writer.u64(generations.evidence.value());
}

bool read_generations(ByteReader& reader, GenerationVector& generations) {
  std::uint64_t value = 0;
  if (!reader.u64(value)) {
    return false;
  }
  generations.configuration = PortConfigurationGeneration::from_value(value);
  if (!reader.u64(value)) {
    return false;
  }
  generations.administrative = AdministrativeGeneration::from_value(value);
  if (!reader.u64(value)) {
    return false;
  }
  generations.ownership = PortOwnershipGeneration::from_value(value);
  if (!reader.u64(value)) {
    return false;
  }
  generations.capability = CapabilityBindingGeneration::from_value(value);
  if (!reader.u64(value)) {
    return false;
  }
  generations.topology = TopologyGeneration::from_value(value);
  if (!reader.u64(value)) {
    return false;
  }
  generations.device = DeviceGeneration::from_value(value);
  if (!reader.u64(value)) {
    return false;
  }
  generations.lifecycle = LifecycleGeneration::from_value(value);
  if (!reader.u64(value)) {
    return false;
  }
  generations.evidence = EvidenceGeneration::from_value(value);
  return true;
}

void write_record(ByteWriter& writer, const PortRecord& record) {
  write_id(writer, record.port.to_string());
  write_id(writer, record.parent_device.to_string());
  writer.u8(static_cast<std::uint8_t>(record.lifecycle));
  writer.boolean(record.configuration.has_value());
  if (record.configuration.has_value()) {
    write_configuration(writer, *record.configuration);
  }
  writer.boolean(record.ownership.has_value());
  if (record.ownership.has_value()) {
    write_ownership(writer, *record.ownership);
  }
  write_capability_binding(writer, record.capability);
  writer.u8(static_cast<std::uint8_t>(record.applied.outcome));
  writer.u64(record.applied.generation.value());
  writer.u64(record.applied.evidence_generation.value());
  writer.text(record.applied.source, max_name_length);
  writer.boolean(record.applied.verified);
  writer.u8(static_cast<std::uint8_t>(record.drift));
  write_generations(writer, record.generations);
  write_provenance(writer, record.provenance);
  write_id(writer, record.last_mutation.attempt.to_string());
  writer.u64(record.last_mutation.payload_digest);
  writer.u64(record.last_mutation.result_generation.value());
  writer.text(record.status_reason, max_name_length);
  writer.text(record.status_detail, max_description_length);
}

bool read_record(ByteReader& reader, PortRecord& record) {
  if (!read_strong_id(reader, record.port)) {
    return false;
  }
  if (!read_strong_id(reader, record.parent_device)) {
    return false;
  }
  std::uint8_t byte = 0;
  if (!reader.u8(byte) || !is_valid(static_cast<PortLifecycle>(byte))) {
    return false;
  }
  record.lifecycle = static_cast<PortLifecycle>(byte);
  bool has_configuration = false;
  if (!reader.boolean(has_configuration)) {
    return false;
  }
  if (has_configuration) {
    PortConfiguration configuration;
    if (!read_configuration(reader, configuration)) {
      return false;
    }
    record.configuration = configuration;
  }
  bool has_ownership = false;
  if (!reader.boolean(has_ownership)) {
    return false;
  }
  if (has_ownership) {
    PortOwnership ownership;
    if (!read_ownership(reader, ownership)) {
      return false;
    }
    record.ownership = ownership;
  }
  if (!read_capability_binding(reader, record.capability)) {
    return false;
  }
  std::uint8_t applied = 0;
  if (!reader.u8(applied) || !is_valid(static_cast<AppliedOutcome>(applied))) {
    return false;
  }
  record.applied.outcome = static_cast<AppliedOutcome>(applied);
  std::uint64_t value = 0;
  if (!reader.u64(value)) {
    return false;
  }
  record.applied.generation = PortConfigurationGeneration::from_value(value);
  if (!reader.u64(value)) {
    return false;
  }
  record.applied.evidence_generation = EvidenceGeneration::from_value(value);
  if (!reader.text(max_name_length, record.applied.source)) {
    return false;
  }
  if (!reader.boolean(record.applied.verified)) {
    return false;
  }
  if (!reader.u8(byte) || !is_valid(static_cast<DriftState>(byte))) {
    return false;
  }
  record.drift = static_cast<DriftState>(byte);
  if (!read_generations(reader, record.generations)) {
    return false;
  }
  if (!read_provenance(reader, record.provenance)) {
    return false;
  }
  if (!read_strong_id(reader, record.last_mutation.attempt)) {
    return false;
  }
  if (!reader.u64(record.last_mutation.payload_digest)) {
    return false;
  }
  if (!reader.u64(value)) {
    return false;
  }
  record.last_mutation.result_generation = PortConfigurationGeneration::from_value(value);
  if (!reader.text(max_name_length, record.status_reason)) {
    return false;
  }
  if (!reader.text(max_description_length, record.status_detail)) {
    return false;
  }
  std::string error;
  return record.validate(error);
}

// ---------------------------------------------------------------------------
// Profiles
// ---------------------------------------------------------------------------

void write_profile_configuration(ByteWriter& writer, const ProfileConfiguration& configuration) {
  writer.boolean(configuration.specifies_speed);
  writer.u8(static_cast<std::uint8_t>(configuration.speed.selection));
  writer.u64(configuration.speed.rate.bits_per_second());
  writer.boolean(configuration.specifies_mtu);
  writer.u32(configuration.mtu.value());
  writer.boolean(configuration.specifies_duplex);
  writer.u8(static_cast<std::uint8_t>(configuration.duplex));
  writer.boolean(configuration.specifies_autoneg);
  writer.u8(static_cast<std::uint8_t>(configuration.autoneg));
  writer.boolean(configuration.specifies_fec);
  writer.u8(static_cast<std::uint8_t>(configuration.fec));
  writer.boolean(configuration.specifies_pause);
  writer.u8(static_cast<std::uint8_t>(configuration.pause));
  writer.boolean(configuration.specifies_breakout);
  writer.u8(static_cast<std::uint8_t>(configuration.breakout));
  writer.boolean(configuration.specifies_mode);
  writer.u8(static_cast<std::uint8_t>(configuration.mode));
  writer.boolean(configuration.specifies_protocol);
  writer.u8(static_cast<std::uint8_t>(configuration.protocol));
  writer.boolean(configuration.specifies_aggregation);
  writer.boolean(configuration.aggregate_participation);
  writer.boolean(configuration.specifies_role);
  writer.text(configuration.role, max_name_length);
}

bool read_profile_configuration(ByteReader& reader, ProfileConfiguration& configuration) {
  std::uint64_t value = 0;
  std::uint8_t byte = 0;
  if (!reader.boolean(configuration.specifies_speed)) {
    return false;
  }
  if (!reader.u8(byte) || !is_valid(static_cast<SpeedSelection>(byte))) {
    return false;
  }
  configuration.speed.selection = static_cast<SpeedSelection>(byte);
  if (!reader.u64(value)) {
    return false;
  }
  if (value != 0) {
    const auto rate = PortSpeed::from_bits_per_second(value);
    if (!rate.has_value()) {
      return false;
    }
    configuration.speed.rate = *rate;
  }
  if (!reader.boolean(configuration.specifies_mtu)) {
    return false;
  }
  std::uint32_t mtu_value = 0;
  if (!reader.u32(mtu_value)) {
    return false;
  }
  if (configuration.specifies_mtu) {
    const auto mtu = Mtu::create(mtu_value);
    if (!mtu.has_value()) {
      return false;
    }
    configuration.mtu = *mtu;
  } else if (mtu_value != 0) {
    return false;
  }
  if (!reader.boolean(configuration.specifies_duplex)) {
    return false;
  }
  if (!reader.u8(byte) || !is_valid(static_cast<DuplexMode>(byte))) {
    return false;
  }
  configuration.duplex = static_cast<DuplexMode>(byte);
  if (!reader.boolean(configuration.specifies_autoneg)) {
    return false;
  }
  if (!reader.u8(byte) || !is_valid(static_cast<AutonegPolicy>(byte))) {
    return false;
  }
  configuration.autoneg = static_cast<AutonegPolicy>(byte);
  if (!reader.boolean(configuration.specifies_fec)) {
    return false;
  }
  if (!reader.u8(byte) || !is_valid(static_cast<FecMode>(byte))) {
    return false;
  }
  configuration.fec = static_cast<FecMode>(byte);
  if (!reader.boolean(configuration.specifies_pause)) {
    return false;
  }
  if (!reader.u8(byte) || !is_valid(static_cast<PauseMode>(byte))) {
    return false;
  }
  configuration.pause = static_cast<PauseMode>(byte);
  if (!reader.boolean(configuration.specifies_breakout)) {
    return false;
  }
  if (!reader.u8(byte) || !is_valid(static_cast<BreakoutMode>(byte))) {
    return false;
  }
  configuration.breakout = static_cast<BreakoutMode>(byte);
  if (!reader.boolean(configuration.specifies_mode)) {
    return false;
  }
  if (!reader.u8(byte) || !is_valid(static_cast<PortMode>(byte))) {
    return false;
  }
  configuration.mode = static_cast<PortMode>(byte);
  if (!reader.boolean(configuration.specifies_protocol)) {
    return false;
  }
  if (!reader.u8(byte) || !is_valid(static_cast<ProtocolFamily>(byte))) {
    return false;
  }
  configuration.protocol = static_cast<ProtocolFamily>(byte);
  if (!reader.boolean(configuration.specifies_aggregation)) {
    return false;
  }
  if (!reader.boolean(configuration.aggregate_participation)) {
    return false;
  }
  if (!reader.boolean(configuration.specifies_role)) {
    return false;
  }
  if (!reader.text(max_name_length, configuration.role)) {
    return false;
  }
  std::string error;
  return configuration.validate(error);
}

void write_profile(ByteWriter& writer, const PortProfile& profile) {
  write_id(writer, profile.id.to_string());
  writer.u64(profile.generation.value());
  writer.text(profile.name, max_name_length);
  writer.text(profile.description, max_description_length);
  write_profile_configuration(writer, profile.configuration);
  write_provenance(writer, profile.provenance);
}

bool read_profile(ByteReader& reader, PortProfile& profile) {
  if (!read_strong_id(reader, profile.id)) {
    return false;
  }
  std::uint64_t value = 0;
  if (!reader.u64(value)) {
    return false;
  }
  profile.generation = PortProfileGeneration::from_value(value);
  if (!reader.text(max_name_length, profile.name)) {
    return false;
  }
  if (!reader.text(max_description_length, profile.description)) {
    return false;
  }
  if (!read_profile_configuration(reader, profile.configuration)) {
    return false;
  }
  if (!read_provenance(reader, profile.provenance)) {
    return false;
  }
  std::string error;
  return profile.validate(error);
}

// ---------------------------------------------------------------------------
// Devices and publishers
// ---------------------------------------------------------------------------

void write_device(ByteWriter& writer, const DeviceBinding& device) {
  write_id(writer, device.device.to_string());
  writer.u64(device.generation.value());
  writer.text(device.source, max_name_length);
}

bool read_device(ByteReader& reader, DeviceBinding& device) {
  if (!read_strong_id(reader, device.device)) {
    return false;
  }
  std::uint64_t value = 0;
  if (!reader.u64(value)) {
    return false;
  }
  device.generation = DeviceGeneration::from_value(value);
  if (!reader.text(max_name_length, device.source)) {
    return false;
  }
  return device.device.valid() && device.generation.valid();
}

void write_publisher(ByteWriter& writer, const PublisherRecord& publisher) {
  write_id(writer, publisher.publisher.to_string());
  write_id(writer, publisher.boot.to_string());
  writer.u64(publisher.epoch.value());
  writer.boolean(publisher.fenced);
}

bool read_publisher(ByteReader& reader, PublisherRecord& publisher) {
  if (!read_strong_id(reader, publisher.publisher)) {
    return false;
  }
  if (!read_strong_id(reader, publisher.boot)) {
    return false;
  }
  std::uint64_t value = 0;
  if (!reader.u64(value)) {
    return false;
  }
  publisher.epoch = CoordinatorEpoch::from_value(value);
  if (!reader.boolean(publisher.fenced)) {
    return false;
  }
  return publisher.publisher.valid() && publisher.boot.valid();
}

// ---------------------------------------------------------------------------
// Authority context and applied evidence
// ---------------------------------------------------------------------------

void write_authority(ByteWriter& writer, const AuthorityContext& authority) {
  writer.u64(authority.epoch.value());
  write_id(writer, authority.publisher.to_string());
  write_id(writer, authority.boot.to_string());
  write_id(writer, authority.ownership.to_string());
  writer.u64(authority.ownership_generation.value());
  writer.u64(authority.expected_configuration.value());
  writer.u64(authority.expected_capability.value());
  writer.u64(authority.expected_topology.value());
  writer.u64(authority.expected_device.value());
  write_id(writer, authority.attempt.to_string());
}

bool read_authority(ByteReader& reader, AuthorityContext& authority) {
  std::uint64_t value = 0;
  if (!reader.u64(value)) {
    return false;
  }
  authority.epoch = CoordinatorEpoch::from_value(value);
  if (!read_strong_id(reader, authority.publisher)) {
    return false;
  }
  if (!read_strong_id(reader, authority.boot)) {
    return false;
  }
  if (!read_strong_id(reader, authority.ownership)) {
    return false;
  }
  if (!reader.u64(value)) {
    return false;
  }
  authority.ownership_generation = PortOwnershipGeneration::from_value(value);
  if (!reader.u64(value)) {
    return false;
  }
  authority.expected_configuration = PortConfigurationGeneration::from_value(value);
  if (!reader.u64(value)) {
    return false;
  }
  authority.expected_capability = CapabilityBindingGeneration::from_value(value);
  if (!reader.u64(value)) {
    return false;
  }
  authority.expected_topology = TopologyGeneration::from_value(value);
  if (!reader.u64(value)) {
    return false;
  }
  authority.expected_device = DeviceGeneration::from_value(value);
  if (!read_strong_id(reader, authority.attempt)) {
    return false;
  }
  return true;
}

void write_applied(ByteWriter& writer, const AppliedEvidence& evidence) {
  writer.u8(static_cast<std::uint8_t>(evidence.outcome));
  writer.u64(evidence.generation.value());
  writer.u64(evidence.evidence_generation.value());
  writer.text(evidence.source, max_name_length);
  writer.boolean(evidence.verified);
}

bool read_applied(ByteReader& reader, AppliedEvidence& evidence) {
  std::uint8_t outcome = 0;
  if (!reader.u8(outcome) || !is_valid(static_cast<AppliedOutcome>(outcome))) {
    return false;
  }
  evidence.outcome = static_cast<AppliedOutcome>(outcome);
  std::uint64_t value = 0;
  if (!reader.u64(value)) {
    return false;
  }
  evidence.generation = PortConfigurationGeneration::from_value(value);
  if (!reader.u64(value)) {
    return false;
  }
  evidence.evidence_generation = EvidenceGeneration::from_value(value);
  if (!reader.text(max_name_length, evidence.source)) {
    return false;
  }
  return reader.boolean(evidence.verified);
}

}  // namespace portfabric::detail



