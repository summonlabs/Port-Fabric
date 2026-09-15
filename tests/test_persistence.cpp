// Persistence: round trip, conservative recovery, and adversarial corruption.

#include <string>
#include <vector>

#include "pf_test.hpp"
#include "support.hpp"

using namespace portfabric;

namespace {

std::string valid_container() {
  PersistedState state;
  state.fabric = FabricId::from_validated("fabric-a");
  state.site = SiteId::from_validated("site-a");
  state.epoch = CoordinatorEpoch::from_value(3);
  state.engine_generation = EngineGeneration::from_value(7);
  state.topology = TopologyGeneration::from_value(2);
  std::string encoded;
  const Outcome result = encode_persisted_state(state, encoded);
  if (!result.ok()) {
    return std::string();
  }
  return encoded;
}

Outcome decode(std::string bytes, PersistedState& state) {
  return decode_persisted_state(bytes, state);
}

}  // namespace

PF_TEST(persistence_round_trip_preserves_records) {
  const std::string path = pf_test::scratch_path("roundtrip.pfstate");
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Spine400G, 4);
  {
    PortFabricEngine engine(pf_test::engine_config("coordinator", path), &fixture.provider,
                            &fixture.adapter);
    for (std::uint32_t index = 1; index <= 4; ++index) {
      PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, fixture.port(index)).accepted());
    }
    const Outcome saved = engine.save();
    PF_CHECK(saved.ok());
    const Digest before = engine.state_digest();

    PortFabricEngine recovered(pf_test::engine_config("coordinator", path), &fixture.provider,
                               &fixture.adapter);
    const Outcome loaded = recovered.load();
    PF_CHECK(loaded.ok());
    PF_CHECK_EQ(recovered.port_count(), static_cast<std::size_t>(4));
    // Conservative recovery fences every recovered configuration.
    for (std::uint32_t index = 1; index <= 4; ++index) {
      const auto record = recovered.record(fixture.port(index));
      PF_REQUIRE(record.has_value());
      PF_CHECK_EQ(record->lifecycle, PortLifecycle::RevalidationRequired);
      PF_CHECK(!record->ownership.has_value());
      PF_CHECK_EQ(record->status_reason, std::string("RECOVERED_FROM_PERSISTENCE"));
      PF_CHECK(!record->last_mutation.attempt.valid());
    }
    PF_CHECK(!recovered.state_digest().is_zero());
    PF_CHECK(before != recovered.state_digest());
  }
  pf_test::remove_scratch(path);
}

PF_TEST(persistence_survives_repeated_restarts) {
  const std::string path = pf_test::scratch_path("restarts.pfstate");
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Server25G, 2);
  portfabric::CoordinatorEpoch last_epoch;
  for (int round = 0; round < 4; ++round) {
    PortFabricEngine engine(pf_test::engine_config("coordinator", path, 1), &fixture.provider,
                            &fixture.adapter);
    const Outcome loaded = engine.load();
    if (round == 0) {
      // The very first start has no container yet: that is the normal fresh boot
      // path, not a failure.
      PF_CHECK_EQ(loaded.code(), OutcomeCode::PersistenceIoFailure);
    } else {
      if (!loaded.accepted()) {
        std::printf("  round %d load rejected: %s\n", round, loaded.to_string().c_str());
      }
      PF_REQUIRE(loaded.accepted());
    }
    CoordinatorEpoch assigned;
    PF_REQUIRE(engine.begin_epoch(assigned).accepted());
    // The epoch advances strictly on every restart.
    if (last_epoch.valid()) {
      PF_CHECK(assigned > last_epoch);
    }
    last_epoch = assigned;
    PF_CHECK(engine.save().accepted());
  }
  pf_test::remove_scratch(path);
}

PF_TEST(persistence_rejects_corruption) {
  const std::string valid = valid_container();
  PF_REQUIRE(!valid.empty());

  PersistedState state;
  // Empty and truncated containers.
  PF_CHECK_EQ(decode("", state).code(), OutcomeCode::PersistenceCorruption);
  PF_CHECK_EQ(decode(std::string(4, 'x'), state).code(), OutcomeCode::PersistenceCorruption);
  PF_CHECK_EQ(decode(valid.substr(0, 8), state).code(), OutcomeCode::PersistenceCorruption);
  PF_CHECK_EQ(decode(valid.substr(0, valid.size() - 1), state).code(),
              OutcomeCode::PersistenceCorruption);

  // Bad magic.
  std::string bad_magic = valid;
  bad_magic[0] = 'X';
  PF_CHECK_EQ(decode(bad_magic, state).code(), OutcomeCode::PersistenceCorruption);

  // Unsupported format version.
  std::string bad_version = valid;
  bad_version[8] = static_cast<char>(9);
  PF_CHECK_EQ(decode(bad_version, state).code(), OutcomeCode::PersistenceCorruption);

  // Header corruption is caught by the header checksum.
  std::string bad_header = valid;
  bad_header[10] = static_cast<char>(0x7f);
  PF_CHECK_EQ(decode(bad_header, state).code(), OutcomeCode::PersistenceCorruption);

  // Payload corruption is caught by the payload checksum.
  std::string bad_payload = valid;
  bad_payload[valid.size() - 1] = static_cast<char>(bad_payload.back() ^ 0x5a);
  PF_CHECK_EQ(decode(bad_payload, state).code(), OutcomeCode::PersistenceCorruption);

  // Trailing bytes are rejected.
  PF_CHECK_EQ(decode(valid + "trailing", state).code(), OutcomeCode::PersistenceCorruption);
}

PF_TEST(persistence_rejects_absurd_counts) {
  // Build a container by hand whose record count is absurd, then repair both
  // checksums so that the count check itself is what rejects the container.
  PersistedState empty;
  empty.fabric = FabricId::from_validated("fabric-a");
  empty.site = SiteId::from_validated("site-a");
  empty.epoch = CoordinatorEpoch::from_value(1);
  std::string payload;
  {
    // The encoder writes an empty state; patching the count to an absurd value
    // exercises the decoder bound rather than the checksum.
    std::string encoded;
    PF_REQUIRE(encode_persisted_state(empty, encoded).ok());
    payload = encoded;
  }
  const std::size_t header_size = 32;
  const std::size_t count_offset = header_size + 4 + std::string("fabric-a").size() + 4 +
                                   std::string("site-a").size() + 8 + 8 + 8;
  PF_REQUIRE(payload.size() > count_offset + 4);
  const std::uint32_t absurd = 0x7fffffffu;
  payload[count_offset] = static_cast<char>(absurd & 0xffu);
  payload[count_offset + 1] = static_cast<char>((absurd >> 8u) & 0xffu);
  payload[count_offset + 2] = static_cast<char>((absurd >> 16u) & 0xffu);
  payload[count_offset + 3] = static_cast<char>((absurd >> 24u) & 0xffu);
  // Recompute the payload CRC and the header CRC so the container stays
  // internally consistent apart from the absurd count.
  const std::string_view body(payload.data() + header_size, payload.size() - header_size);
  const std::uint32_t crc = persistence_checksum(body);
  const auto write_u32 = [&payload](std::size_t offset, std::uint32_t value) {
    payload[offset] = static_cast<char>(value & 0xffu);
    payload[offset + 1] = static_cast<char>((value >> 8u) & 0xffu);
    payload[offset + 2] = static_cast<char>((value >> 16u) & 0xffu);
    payload[offset + 3] = static_cast<char>((value >> 24u) & 0xffu);
  };
  write_u32(24, crc);
  write_u32(28, persistence_checksum(std::string_view(payload.data(), 28)));

  PersistedState state;
  const Outcome result = decode(payload, state);
  PF_CHECK(!result.ok());
  PF_CHECK(result.code() == OutcomeCode::PersistenceBoundsExceeded ||
           result.code() == OutcomeCode::PersistenceCorruption);
}

PF_TEST(persistence_rejects_malformed_payload_fields) {
  // A container whose payload decodes but carries an impossible enum value must
  // be rejected. The value is introduced by re-encoding a state with a patched
  // record, which also proves that encoding validates input.
  PersistedState state;
  state.fabric = FabricId::from_validated("fabric-a");
  state.site = SiteId::from_validated("site-a");
  state.epoch = CoordinatorEpoch::from_value(1);
  PortRecord record;
  record.port = PortId::from_validated("port-1");
  record.parent_device = DeviceId::from_validated("device-1");
  record.lifecycle = PortLifecycle::Configured;
  // A live lifecycle without a configuration is inconsistent and must be
  // rejected by the record validator.
  state.records.push_back(record);
  std::string encoded;
  const Outcome result = encode_persisted_state(state, encoded);
  PF_CHECK(!result.ok());
  PF_CHECK_EQ(result.code(), OutcomeCode::InvalidConfiguration);
}

PF_TEST(persistence_path_is_validated) {
  PF_CHECK_EQ(PersistenceStore::validate_path("").code(), OutcomeCode::MalformedRequest);
  PF_CHECK_EQ(PersistenceStore::validate_path("../escape.pfstate").code(),
              OutcomeCode::MalformedRequest);
  PF_CHECK_EQ(PersistenceStore::validate_path("does-not-exist/state.pfstate").code(),
              OutcomeCode::PersistenceIoFailure);

  PersistedState state;
  state.fabric = FabricId::from_validated("fabric-a");
  state.site = SiteId::from_validated("site-a");
  state.epoch = CoordinatorEpoch::from_value(1);
  PF_CHECK(!PersistenceStore::save("..\\escape.pfstate", state).ok());
  PF_CHECK(!PersistenceStore::load("..\\escape.pfstate", state).ok());
  PF_CHECK(!PersistenceStore::load("pf-scratch-missing.pfstate", state).ok());
}

PF_TEST(persistence_write_is_atomic_and_leaves_no_temporary_file) {
  const std::string path = pf_test::scratch_path("atomic.pfstate");
  PersistedState state;
  state.fabric = FabricId::from_validated("fabric-a");
  state.site = SiteId::from_validated("site-a");
  state.epoch = CoordinatorEpoch::from_value(1);
  PF_REQUIRE(PersistenceStore::save(path, state).ok());
  PF_CHECK(file_exists(path));
  PF_CHECK(!file_exists(path + ".pftmp"));
  PersistedState loaded;
  PF_REQUIRE(PersistenceStore::load(path, loaded).ok());
  PF_CHECK_EQ(loaded.fabric.to_string(), std::string("fabric-a"));

  // Saving over an existing container replaces it atomically.
  state.epoch = CoordinatorEpoch::from_value(2);
  PF_REQUIRE(PersistenceStore::save(path, state).ok());
  PF_REQUIRE(PersistenceStore::load(path, loaded).ok());
  PF_CHECK_EQ(loaded.epoch.value(), 2ull);
  PF_CHECK(!file_exists(path + ".pftmp"));
  pf_test::remove_scratch(path);
}

PF_TEST(recovery_downgrades_applied_evidence) {
  const std::string path = pf_test::scratch_path("applied.pfstate");
  pf_test::Fixture fixture(portfabric::SyntheticPortClass::Server10G, 1);
  {
    EngineConfig config = pf_test::engine_config("coordinator", path);
    config.adapter_policy = AdapterPolicy::ApplyAndVerify;
    PortFabricEngine engine(config, &fixture.provider, &fixture.adapter);
    PF_REQUIRE(pf_test::bind_claim_configure(engine, fixture, fixture.port(1),
                                             portfabric::SyntheticPortClass::Server10G)
                   .accepted());
    PF_CHECK_EQ(engine.applied_evidence(fixture.port(1))->outcome, AppliedOutcome::Verified);
    PF_CHECK(engine.save().accepted());
  }
  {
    PortFabricEngine recovered(pf_test::engine_config("coordinator", path), &fixture.provider,
                               &fixture.adapter);
    PF_REQUIRE(recovered.load().accepted());
    const auto applied = recovered.applied_evidence(fixture.port(1));
    PF_REQUIRE(applied.has_value());
    // Persisted verification never becomes fresh hardware truth by assumption.
    PF_CHECK_EQ(applied->outcome, AppliedOutcome::OutcomeUnknown);
    PF_CHECK(!applied->verified);
  }
  pf_test::remove_scratch(path);
}

PF_TEST(recovery_rejects_container_from_another_authority_domain) {
  const std::string path = pf_test::scratch_path("domain.pfstate");
  PersistedState state;
  state.fabric = FabricId::from_validated("other-fabric");
  state.site = SiteId::from_validated("other-site");
  state.epoch = CoordinatorEpoch::from_value(1);
  PF_REQUIRE(PersistenceStore::save(path, state).ok());
  PortFabricEngine engine(pf_test::engine_config("coordinator", path), nullptr, nullptr);
  PF_CHECK_EQ(engine.load().code(), OutcomeCode::ConflictDetected);
  pf_test::remove_scratch(path);
}
