#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "portfabric/characteristics.hpp"
#include "portfabric/configuration.hpp"
#include "portfabric/digest.hpp"
#include "portfabric/export.hpp"
#include "portfabric/generation.hpp"
#include "portfabric/ids.hpp"
#include "portfabric/outcome.hpp"

namespace portfabric {

/// The subset of port configuration a profile can supply.
///
/// A flag marks whether the profile expresses an opinion about a dimension.
/// A dimension the profile does not mention is left untouched when the profile
/// is applied, so a profile never silently resets unrelated configuration.
struct PF_EXPORT ProfileConfiguration {
  bool specifies_speed = false;
  SpeedSetting speed;

  bool specifies_mtu = false;
  Mtu mtu;

  bool specifies_duplex = false;
  DuplexMode duplex = DuplexMode::Unknown;

  bool specifies_autoneg = false;
  AutonegPolicy autoneg = AutonegPolicy::Unknown;

  bool specifies_fec = false;
  FecMode fec = FecMode::Unknown;

  bool specifies_pause = false;
  PauseMode pause = PauseMode::Unknown;

  bool specifies_breakout = false;
  BreakoutMode breakout = BreakoutMode::None;

  bool specifies_mode = false;
  PortMode mode = PortMode::Physical;

  bool specifies_protocol = false;
  ProtocolFamily protocol = ProtocolFamily::Unknown;

  bool specifies_aggregation = false;
  bool aggregate_participation = false;

  bool specifies_role = false;
  std::string role;

  bool validate(std::string& error) const;
  bool unspecified() const noexcept;
  std::vector<std::pair<std::string, std::string>> canonical_fields() const;
  std::string to_string() const;
};

/// A reusable, generation-bound configuration bundle.
///
/// A profile generation is immutable. Defining a different bundle for the same
/// identifier requires a strictly greater generation, and existing port
/// configurations keep the generation they were committed against until an
/// explicit re-evaluation assigns the newer generation.
class PF_EXPORT PortProfile {
 public:
  PortProfileId id;
  PortProfileGeneration generation;
  /// Bounded, validated name.
  std::string name;
  /// Bounded, validated description.
  std::string description;
  ProfileConfiguration configuration;
  Provenance provenance;

  bool validate(std::string& error) const;
  Digest digest() const;
  std::string to_string() const;
};

/// In-memory registry of profile generations.
///
/// The registry is not internally synchronized: the owning engine serializes
/// access, and no callback is ever invoked while a registry lock would be held.
class PF_EXPORT ProfileRegistry {
 public:
  /// Defines a profile generation.
  ///
  /// Re-defining an existing (identifier, generation) pair with identical
  /// content returns ALREADY_CURRENT; with different content it is rejected as
  /// DUPLICATE_RECORD. A generation that does not advance beyond the latest
  /// generation of that identifier is rejected.
  Outcome define(const PortProfile& profile);

  std::optional<PortProfile> get(const PortProfileId& id, PortProfileGeneration generation) const;

  /// Latest defined generation of an identifier.
  std::optional<PortProfile> latest(const PortProfileId& id) const;

  /// True when the identifier is known at any generation.
  bool contains(const PortProfileId& id) const;

  std::vector<PortProfile> list() const;

  /// Number of distinct profile identifiers.
  std::size_t size() const noexcept { return profiles_.size(); }

  /// Number of defined profile generations.
  std::size_t generation_count() const noexcept { return generation_count_; }

  void clear();

 private:
  std::map<PortProfileId, std::vector<PortProfile>> profiles_;
  std::size_t generation_count_ = 0;
};

/// Applies the dimensions a profile specifies onto a configuration.
///
/// Dimensions the profile does not mention are preserved. The caller must
/// re-validate the resulting configuration against capability evidence; this
/// function performs no capability checks.
PF_EXPORT void apply_profile(const ProfileConfiguration& profile, PortConfiguration& target);

}  // namespace portfabric
