#include "portfabric/profile.hpp"

#include <algorithm>

#include "text.hpp"

namespace portfabric {
namespace {

void add_flag_field(std::vector<std::pair<std::string, std::string>>& fields, std::string name,
                    bool present) {
  fields.emplace_back(std::move(name), present ? std::string("specified") : std::string("absent"));
}

}  // namespace

bool ProfileConfiguration::validate(std::string& error) const {
  if (specifies_speed && !speed.valid()) {
    error = "profile speed specification is inconsistent";
    return false;
  }
  if (specifies_mtu && !mtu.valid()) {
    error = "profile MTU specification is outside the legal range";
    return false;
  }
  if (specifies_duplex && !is_valid(duplex)) {
    error = "profile duplex specification is not a legal value";
    return false;
  }
  if (specifies_autoneg && !is_valid(autoneg)) {
    error = "profile autonegotiation specification is not a legal value";
    return false;
  }
  if (specifies_fec && !is_valid(fec)) {
    error = "profile FEC specification is not a legal value";
    return false;
  }
  if (specifies_pause && !is_valid(pause)) {
    error = "profile pause specification is not a legal value";
    return false;
  }
  if (specifies_breakout && !is_valid(breakout)) {
    error = "profile breakout specification is not a legal value";
    return false;
  }
  if (specifies_mode && !is_valid(mode)) {
    error = "profile port mode specification is not a legal value";
    return false;
  }
  if (specifies_protocol && !is_valid(protocol)) {
    error = "profile protocol specification is not a legal value";
    return false;
  }
  if (!detail::validate_text(role, max_name_length, error)) {
    error = "profile role: " + error;
    return false;
  }
  if (!specifies_role && !role.empty()) {
    error = "profile carries a role without declaring it specified";
    return false;
  }
  return true;
}

bool ProfileConfiguration::unspecified() const noexcept {
  return !specifies_speed && !specifies_mtu && !specifies_duplex && !specifies_autoneg &&
         !specifies_fec && !specifies_pause && !specifies_breakout && !specifies_mode &&
         !specifies_protocol && !specifies_aggregation && !specifies_role;
}

std::vector<std::pair<std::string, std::string>> ProfileConfiguration::canonical_fields() const {
  std::vector<std::pair<std::string, std::string>> fields;
  fields.reserve(24);
  add_flag_field(fields, "speed", specifies_speed);
  fields.emplace_back("speed_value", speed.to_string());
  add_flag_field(fields, "mtu", specifies_mtu);
  fields.emplace_back("mtu_value", mtu.to_string());
  add_flag_field(fields, "duplex", specifies_duplex);
  fields.emplace_back("duplex_value", std::string(portfabric::to_string(duplex)));
  add_flag_field(fields, "autoneg", specifies_autoneg);
  fields.emplace_back("autoneg_value", std::string(portfabric::to_string(autoneg)));
  add_flag_field(fields, "fec", specifies_fec);
  fields.emplace_back("fec_value", std::string(portfabric::to_string(fec)));
  add_flag_field(fields, "pause", specifies_pause);
  fields.emplace_back("pause_value", std::string(portfabric::to_string(pause)));
  add_flag_field(fields, "breakout", specifies_breakout);
  fields.emplace_back("breakout_value", std::string(portfabric::to_string(breakout)));
  add_flag_field(fields, "mode", specifies_mode);
  fields.emplace_back("mode_value", std::string(portfabric::to_string(mode)));
  add_flag_field(fields, "protocol", specifies_protocol);
  fields.emplace_back("protocol_value", std::string(portfabric::to_string(protocol)));
  add_flag_field(fields, "aggregation", specifies_aggregation);
  fields.emplace_back("aggregation_participation",
                      aggregate_participation ? std::string("participating") : std::string("passive"));
  add_flag_field(fields, "role", specifies_role);
  fields.emplace_back("role_value", role);
  return fields;
}

std::string ProfileConfiguration::to_string() const {
  std::string out;
  bool first = true;
  for (const auto& [name, value] : canonical_fields()) {
    if (!first) {
      out.push_back(' ');
    }
    first = false;
    out.append(name);
    out.push_back('=');
    out.append(value.empty() ? "none" : value);
  }
  return out;
}

bool PortProfile::validate(std::string& error) const {
  if (!id.valid() || !generation.valid()) {
    error = "the profile identity or generation is missing";
    return false;
  }
  if (!detail::validate_text(name, max_name_length, error)) {
    error = "profile name: " + error;
    return false;
  }
  if (name.empty()) {
    error = "the profile name is empty";
    return false;
  }
  if (!detail::validate_text(description, max_description_length, error)) {
    error = "profile description: " + error;
    return false;
  }
  if (!configuration.validate(error)) {
    return false;
  }
  if (!is_valid(provenance.kind) || !provenance.epoch.valid()) {
    error = "profile provenance is incomplete";
    return false;
  }
  return true;
}

Digest PortProfile::digest() const {
  DigestBuilder builder;
  builder.add_field("id", id.to_string());
  builder.add_field("generation", generation.to_string());
  builder.add_field("name", name);
  builder.add_field("description", description);
  for (const auto& [field_name, value] : configuration.canonical_fields()) {
    builder.add_field(field_name, value);
  }
  return builder.finish();
}

std::string PortProfile::to_string() const {
  std::string out("profile=");
  out.append(id.valid() ? id.to_string() : std::string("none"));
  out.append("@");
  out.append(generation.valid() ? generation.to_string() : std::string("none"));
  out.append(" name=");
  out.append(name.empty() ? "none" : name);
  out.append(" configuration=[");
  out.append(configuration.to_string());
  out.push_back(']');
  return out;
}

Outcome ProfileRegistry::define(const PortProfile& profile) {
  std::string error;
  if (!profile.validate(error)) {
    return Outcome::failure(OutcomeCode::InvalidProfile, error);
  }
  auto existing = profiles_.find(profile.id);
  if (existing == profiles_.end()) {
    if (profiles_.size() >= max_profiles) {
      return Outcome::failure(OutcomeCode::ResourceBoundExceeded,
                              "the profile registry is at its configured bound");
    }
    if (profile.generation.value() != 1) {
      return Outcome::failure(OutcomeCode::InvalidProfile,
                              "the first generation of a profile must be generation 1");
    }
    std::vector<PortProfile> generations;
    generations.push_back(profile);
    profiles_.emplace(profile.id, std::move(generations));
    ++generation_count_;
    return Outcome::success("profile generation defined");
  }
  std::vector<PortProfile>& generations = existing->second;
  const PortProfile& latest = generations.back();
  if (profile.generation == latest.generation) {
    if (profile.digest() == latest.digest()) {
      return Outcome::current("the profile generation is already defined with identical content");
    }
    return Outcome::failure(OutcomeCode::DuplicateRecord,
                            "the profile generation is already defined with different content");
  }
  if (profile.generation < latest.generation) {
    return Outcome::failure(OutcomeCode::InvalidProfile,
                            "the profile generation is older than the latest defined generation");
  }
  if (generations.size() >= max_profile_generations) {
    return Outcome::failure(OutcomeCode::ResourceBoundExceeded,
                            "the profile identifier reached its generation bound");
  }
  generations.push_back(profile);
  ++generation_count_;
  return Outcome::success("profile generation defined");
}

std::optional<PortProfile> ProfileRegistry::get(const PortProfileId& id,
                                                PortProfileGeneration generation) const {
  const auto existing = profiles_.find(id);
  if (existing == profiles_.end()) {
    return std::nullopt;
  }
  for (const PortProfile& profile : existing->second) {
    if (profile.generation == generation) {
      return profile;
    }
  }
  return std::nullopt;
}

std::optional<PortProfile> ProfileRegistry::latest(const PortProfileId& id) const {
  const auto existing = profiles_.find(id);
  if (existing == profiles_.end() || existing->second.empty()) {
    return std::nullopt;
  }
  return existing->second.back();
}

bool ProfileRegistry::contains(const PortProfileId& id) const {
  return profiles_.find(id) != profiles_.end();
}

std::vector<PortProfile> ProfileRegistry::list() const {
  std::vector<PortProfile> out;
  out.reserve(generation_count_);
  for (const auto& [id, generations] : profiles_) {
    for (const PortProfile& profile : generations) {
      out.push_back(profile);
    }
  }
  return out;
}

void ProfileRegistry::clear() {
  profiles_.clear();
  generation_count_ = 0;
}

void apply_profile(const ProfileConfiguration& profile, PortConfiguration& target) {
  if (profile.specifies_speed) {
    target.speed = profile.speed;
  }
  if (profile.specifies_mtu) {
    target.mtu = profile.mtu;
  }
  if (profile.specifies_duplex) {
    target.duplex = profile.duplex;
  }
  if (profile.specifies_autoneg) {
    target.autoneg = profile.autoneg;
  }
  if (profile.specifies_fec) {
    target.fec = profile.fec;
  }
  if (profile.specifies_pause) {
    target.pause = profile.pause;
  }
  if (profile.specifies_breakout) {
    target.breakout = profile.breakout;
  }
  if (profile.specifies_mode) {
    target.mode = profile.mode;
  }
  if (profile.specifies_protocol) {
    target.protocol = profile.protocol;
  }
  if (profile.specifies_aggregation) {
    target.aggregation.administrative_participation = profile.aggregate_participation;
  }
  if (profile.specifies_role) {
    target.role = profile.role;
  }
}

}  // namespace portfabric
