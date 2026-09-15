#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "portfabric/export.hpp"
#include "portfabric/generation.hpp"
#include "portfabric/ids.hpp"
#include "portfabric/limits.hpp"

namespace portfabric {

/// Structural class of a port. Physical ports map to hardware; logical ports are
/// derived from hardware or from a software construct and always bind the
/// provenance that produced them.
enum class PortMode : std::uint8_t {
  Physical = 0,
  Logical = 1,
};

/// Protocol family a port is configured to carry. UNKNOWN is a real value: the
/// runtime never guesses a protocol family.
enum class ProtocolFamily : std::uint8_t {
  Unknown = 0,
  Ethernet,
  InfiniBand,
  FibreChannel,
  OpticalTransport,
  Overlay,
  Synthetic,
};

/// Forward error correction mode.
enum class FecMode : std::uint8_t {
  Unknown = 0,
  Unsupported,
  None,
  Firecode,
  ReedSolomon528,
  ReedSolomon544,
  ReedSolomon544Interleaved,
  ReedSolomon272Interleaved,
};

/// Duplex mode, for port classes where duplex is meaningful.
enum class DuplexMode : std::uint8_t {
  Unknown = 0,
  Unsupported,
  Half,
  Full,
};

/// Autonegotiation policy. FORCED means the configured rate is asserted without
/// negotiation; PREFERRED means negotiate but keep the configured rate as the
/// admission target; NEGOTIATED means the rate follows the negotiation result.
enum class AutonegPolicy : std::uint8_t {
  Unknown = 0,
  Unsupported,
  Forced,
  Preferred,
  Negotiated,
};

/// Link level pause / flow control configuration, local to the port.
enum class PauseMode : std::uint8_t {
  Unknown = 0,
  Unsupported,
  Disabled,
  Transmit,
  Receive,
  Bidirectional,
};

/// Breakout mode: how many independent children one physical port is split into.
enum class BreakoutMode : std::uint8_t {
  None = 0,
  X2,
  X4,
  X8,
};

/// Number of children produced by a breakout mode (1 for None).
PF_EXPORT std::uint32_t breakout_children(BreakoutMode mode) noexcept;

/// Breakout mode for a child count, or nullopt when the count is not a legal
/// breakout fan-out.
PF_EXPORT std::optional<BreakoutMode> breakout_for_children(std::uint32_t children) noexcept;

/// True when the lane count divides evenly across the breakout fan-out.
PF_EXPORT bool breakout_lanes_are_divisible(std::uint32_t lanes, BreakoutMode mode) noexcept;

/// Stable renderings. Every renderer is part of the observable contract.
PF_EXPORT std::string_view to_string(PortMode mode) noexcept;
PF_EXPORT std::string_view to_string(ProtocolFamily family) noexcept;
PF_EXPORT std::string_view to_string(FecMode mode) noexcept;
PF_EXPORT std::string_view to_string(DuplexMode mode) noexcept;
PF_EXPORT std::string_view to_string(AutonegPolicy policy) noexcept;
PF_EXPORT std::string_view to_string(PauseMode mode) noexcept;
PF_EXPORT std::string_view to_string(BreakoutMode mode) noexcept;

/// Parses a stable rendering back into an enum value. Returns false and leaves
/// the output untouched when the text is not a legal rendering.
PF_EXPORT bool parse_port_mode(std::string_view text, PortMode& out) noexcept;
PF_EXPORT bool parse_protocol_family(std::string_view text, ProtocolFamily& out) noexcept;
PF_EXPORT bool parse_fec_mode(std::string_view text, FecMode& out) noexcept;
PF_EXPORT bool parse_duplex_mode(std::string_view text, DuplexMode& out) noexcept;
PF_EXPORT bool parse_autoneg_policy(std::string_view text, AutonegPolicy& out) noexcept;
PF_EXPORT bool parse_pause_mode(std::string_view text, PauseMode& out) noexcept;
PF_EXPORT bool parse_breakout_mode(std::string_view text, BreakoutMode& out) noexcept;

/// True when the enumerator is a legal value of the domain. Used by the decoders
/// to reject out-of-range values before they reach the model.
PF_EXPORT bool is_valid(PortMode mode) noexcept;
PF_EXPORT bool is_valid(ProtocolFamily family) noexcept;
PF_EXPORT bool is_valid(FecMode mode) noexcept;
PF_EXPORT bool is_valid(DuplexMode mode) noexcept;
PF_EXPORT bool is_valid(AutonegPolicy policy) noexcept;
PF_EXPORT bool is_valid(PauseMode mode) noexcept;
PF_EXPORT bool is_valid(BreakoutMode mode) noexcept;

/// A signalling rate in bits per second.
///
/// Speeds are never stored as display strings. Storage and arithmetic use an
/// integer bit rate; the canonical rendering is derived and reversible.
class PF_EXPORT PortSpeed {
 public:
  constexpr PortSpeed() noexcept = default;

  static std::optional<PortSpeed> from_bits_per_second(std::uint64_t bits_per_second) noexcept;

  /// Parses the canonical rendering ("400G", "2.5G", "100M", "1250K") or a plain
  /// decimal bit rate in bits per second. Rejects zero and out-of-range values.
  static std::optional<PortSpeed> parse(std::string_view text) noexcept;

  constexpr bool valid() const noexcept { return bits_per_second_ != 0; }
  explicit constexpr operator bool() const noexcept { return valid(); }
  constexpr std::uint64_t bits_per_second() const noexcept { return bits_per_second_; }

  std::string to_string() const;

  friend constexpr bool operator==(PortSpeed lhs, PortSpeed rhs) noexcept {
    return lhs.bits_per_second_ == rhs.bits_per_second_;
  }
  friend constexpr bool operator!=(PortSpeed lhs, PortSpeed rhs) noexcept {
    return !(lhs == rhs);
  }
  friend constexpr bool operator<(PortSpeed lhs, PortSpeed rhs) noexcept {
    return lhs.bits_per_second_ < rhs.bits_per_second_;
  }
  friend constexpr bool operator>(PortSpeed lhs, PortSpeed rhs) noexcept { return rhs < lhs; }
  friend constexpr bool operator<=(PortSpeed lhs, PortSpeed rhs) noexcept { return !(rhs < lhs); }
  friend constexpr bool operator>=(PortSpeed lhs, PortSpeed rhs) noexcept { return !(lhs < rhs); }

 private:
  std::uint64_t bits_per_second_ = 0;
};

/// Lane configuration: a lane count and the signalling rate of each lane.
///
/// The total port rate is derived with checked arithmetic and must not overflow
/// the representable rate range. A lane configuration is therefore either wholly
/// consistent or absent; the runtime never stores a lane count and a total that
/// disagree.
class PF_EXPORT LaneConfiguration {
 public:
  constexpr LaneConfiguration() noexcept = default;

  static std::optional<LaneConfiguration> create(std::uint32_t lanes,
                                                 PortSpeed per_lane) noexcept;

  /// Creates a configuration whose total rate equals total, using lanes lanes.
  /// Fails when the total is not divisible by the lane count.
  static std::optional<LaneConfiguration> for_total(std::uint32_t lanes,
                                                    PortSpeed total) noexcept;

  constexpr bool valid() const noexcept { return lanes_ != 0 && per_lane_.valid(); }
  explicit constexpr operator bool() const noexcept { return valid(); }
  constexpr std::uint32_t lanes() const noexcept { return lanes_; }
  constexpr PortSpeed per_lane() const noexcept { return per_lane_; }

  /// Total rate of the lane configuration. False when the product overflows.
  bool total(PortSpeed& out) const noexcept;

  std::string to_string() const;

  friend constexpr bool operator==(LaneConfiguration lhs, LaneConfiguration rhs) noexcept {
    return lhs.lanes_ == rhs.lanes_ && lhs.per_lane_ == rhs.per_lane_;
  }
  friend constexpr bool operator!=(LaneConfiguration lhs, LaneConfiguration rhs) noexcept {
    return !(lhs == rhs);
  }

 private:
  std::uint32_t lanes_ = 0;
  PortSpeed per_lane_;
};

/// Port MTU, bounded to a legal range.
class PF_EXPORT Mtu {
 public:
  constexpr Mtu() noexcept = default;

  static std::optional<Mtu> create(std::uint32_t value) noexcept;

  constexpr bool valid() const noexcept { return value_ != 0; }
  explicit constexpr operator bool() const noexcept { return valid(); }
  constexpr std::uint32_t value() const noexcept { return value_; }

  std::string to_string() const;

  friend constexpr bool operator==(Mtu lhs, Mtu rhs) noexcept { return lhs.value_ == rhs.value_; }
  friend constexpr bool operator!=(Mtu lhs, Mtu rhs) noexcept { return !(lhs == rhs); }
  friend constexpr bool operator<(Mtu lhs, Mtu rhs) noexcept { return lhs.value_ < rhs.value_; }

 private:
  std::uint32_t value_ = 0;
};

/// How the port rate is decided.
enum class SpeedSelection : std::uint8_t {
  /// The runtime admits the highest rate the capability evidence supports.
  Auto = 0,
  /// The configured rate is authoritative and must be supported.
  Forced = 1,
};

PF_EXPORT std::string_view to_string(SpeedSelection selection) noexcept;
PF_EXPORT bool parse_speed_selection(std::string_view text, SpeedSelection& out) noexcept;
PF_EXPORT bool is_valid(SpeedSelection selection) noexcept;

/// Configured speed intent.
struct PF_EXPORT SpeedSetting {
  SpeedSelection selection = SpeedSelection::Auto;
  PortSpeed rate;  // required when selection == Forced, absent when Auto

  bool valid() const noexcept {
    if (!is_valid(selection)) {
      return false;
    }
    return selection == SpeedSelection::Auto ? !rate.valid() : rate.valid();
  }

  std::string to_string() const;

  friend bool operator==(const SpeedSetting& lhs, const SpeedSetting& rhs) noexcept {
    return lhs.selection == rhs.selection && lhs.rate == rhs.rate;
  }
  friend bool operator!=(const SpeedSetting& lhs, const SpeedSetting& rhs) noexcept {
    return !(lhs == rhs);
  }
};

/// Aggregation membership held at the port layer.
///
/// Port Fabric records membership intent, the local aggregate identity and the
/// membership generation. It does not execute a link aggregation protocol and
/// does not own aggregate-level forwarding semantics.
struct PF_EXPORT AggregationMembership {
  bool member = false;
  AggregateId aggregate;
  MembershipGeneration generation;
  bool administrative_participation = false;

  bool valid() const noexcept {
    if (!member) {
      return !aggregate.valid() && !generation.valid() && !administrative_participation;
    }
    return aggregate.valid() && generation.valid();
  }

  std::string to_string() const;

  friend bool operator==(const AggregationMembership& lhs,
                         const AggregationMembership& rhs) noexcept {
    return lhs.member == rhs.member && lhs.aggregate == rhs.aggregate &&
           lhs.generation == rhs.generation &&
           lhs.administrative_participation == rhs.administrative_participation;
  }
  friend bool operator!=(const AggregationMembership& lhs,
                         const AggregationMembership& rhs) noexcept {
    return !(lhs == rhs);
  }
};

}  // namespace portfabric
