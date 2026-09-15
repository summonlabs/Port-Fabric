#pragma once

#include <cstdint>
#include <string>

#include "portfabric/export.hpp"

namespace portfabric {

/// Monotonically advancing generation counter bounded to one semantic domain.
///
/// Generation zero is the null generation: it means "no generation has been
/// established yet" and is never a legal expectation in a mutation request.
/// Generations are strictly increasing within a domain and never wrap: the
/// runtime rejects a mutation that would exhaust the counter instead of
/// silently reusing a generation.
template <class Tag>
class Generation {
 public:
  using tag_type = Tag;

  constexpr Generation() noexcept = default;

  static constexpr Generation from_value(std::uint64_t value) noexcept {
    Generation result;
    result.value_ = value;
    return result;
  }

  constexpr bool valid() const noexcept { return value_ != 0; }
  explicit constexpr operator bool() const noexcept { return valid(); }
  constexpr std::uint64_t value() const noexcept { return value_; }

  /// The next generation in the domain. The last representable value has no
  /// successor; callers must test exhaustible() before advancing.
  constexpr Generation next() const noexcept {
    return value_ == 0xffffffffffffffffull ? *this : from_value(value_ + 1);
  }

  /// True when the counter can no longer advance.
  constexpr bool exhaustible() const noexcept { return value_ == 0xffffffffffffffffull; }

  friend constexpr bool operator==(Generation lhs, Generation rhs) noexcept {
    return lhs.value_ == rhs.value_;
  }
  friend constexpr bool operator!=(Generation lhs, Generation rhs) noexcept {
    return lhs.value_ != rhs.value_;
  }
  friend constexpr bool operator<(Generation lhs, Generation rhs) noexcept {
    return lhs.value_ < rhs.value_;
  }
  friend constexpr bool operator>(Generation lhs, Generation rhs) noexcept {
    return rhs.value_ < lhs.value_;
  }
  friend constexpr bool operator<=(Generation lhs, Generation rhs) noexcept {
    return !(rhs.value_ < lhs.value_);
  }
  friend constexpr bool operator>=(Generation lhs, Generation rhs) noexcept {
    return !(lhs.value_ < rhs.value_);
  }

  std::string to_string() const { return std::to_string(value_); }

 private:
  std::uint64_t value_ = 0;
};

namespace tags {
struct ConfigurationGenerationTag {};
struct AdministrativeGenerationTag {};
struct OwnershipGenerationTag {};
struct CapabilityGenerationTag {};
struct TopologyGenerationTag {};
struct DeviceGenerationTag {};
struct EvidenceGenerationTag {};
struct EngineGenerationTag {};
struct LifecycleGenerationTag {};
struct ProfileGenerationTag {};
struct CoordinatorEpochTag {};
struct MembershipGenerationTag {};
}  // namespace tags

using PortConfigurationGeneration = Generation<tags::ConfigurationGenerationTag>;
using AdministrativeGeneration = Generation<tags::AdministrativeGenerationTag>;
using PortOwnershipGeneration = Generation<tags::OwnershipGenerationTag>;
using CapabilityBindingGeneration = Generation<tags::CapabilityGenerationTag>;
using TopologyGeneration = Generation<tags::TopologyGenerationTag>;
using DeviceGeneration = Generation<tags::DeviceGenerationTag>;
using EvidenceGeneration = Generation<tags::EvidenceGenerationTag>;
using EngineGeneration = Generation<tags::EngineGenerationTag>;
using LifecycleGeneration = Generation<tags::LifecycleGenerationTag>;
using PortProfileGeneration = Generation<tags::ProfileGenerationTag>;
using CoordinatorEpoch = Generation<tags::CoordinatorEpochTag>;
using MembershipGeneration = Generation<tags::MembershipGenerationTag>;

}  // namespace portfabric
