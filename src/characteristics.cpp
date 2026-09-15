#include "portfabric/characteristics.hpp"

#include <array>
#include <cstdio>

#include "text.hpp"

namespace portfabric {
namespace {

struct EnumText {
  std::string_view text;
};

}  // namespace

std::uint32_t breakout_children(BreakoutMode mode) noexcept {
  switch (mode) {
    case BreakoutMode::None:
      return 1;
    case BreakoutMode::X2:
      return 2;
    case BreakoutMode::X4:
      return 4;
    case BreakoutMode::X8:
      return 8;
  }
  return 0;
}

std::optional<BreakoutMode> breakout_for_children(std::uint32_t children) noexcept {
  switch (children) {
    case 1:
      return BreakoutMode::None;
    case 2:
      return BreakoutMode::X2;
    case 4:
      return BreakoutMode::X4;
    case 8:
      return BreakoutMode::X8;
    default:
      return std::nullopt;
  }
}

bool breakout_lanes_are_divisible(std::uint32_t lanes, BreakoutMode mode) noexcept {
  const std::uint32_t children = breakout_children(mode);
  if (children == 0 || lanes == 0) {
    return false;
  }
  return lanes % children == 0;
}

std::string_view to_string(PortMode mode) noexcept {
  switch (mode) {
    case PortMode::Physical:
      return "PHYSICAL";
    case PortMode::Logical:
      return "LOGICAL";
  }
  return "UNKNOWN";
}

std::string_view to_string(ProtocolFamily family) noexcept {
  switch (family) {
    case ProtocolFamily::Unknown:
      return "UNKNOWN";
    case ProtocolFamily::Ethernet:
      return "ETHERNET";
    case ProtocolFamily::InfiniBand:
      return "INFINIBAND";
    case ProtocolFamily::FibreChannel:
      return "FIBRECHANNEL";
    case ProtocolFamily::OpticalTransport:
      return "OPTICAL_TRANSPORT";
    case ProtocolFamily::Overlay:
      return "OVERLAY";
    case ProtocolFamily::Synthetic:
      return "SYNTHETIC";
  }
  return "UNKNOWN";
}

std::string_view to_string(FecMode mode) noexcept {
  switch (mode) {
    case FecMode::Unknown:
      return "UNKNOWN";
    case FecMode::Unsupported:
      return "UNSUPPORTED";
    case FecMode::None:
      return "NONE";
    case FecMode::Firecode:
      return "FIRECODE";
    case FecMode::ReedSolomon528:
      return "RS528";
    case FecMode::ReedSolomon544:
      return "RS544";
    case FecMode::ReedSolomon544Interleaved:
      return "RS544_INTERLEAVED";
    case FecMode::ReedSolomon272Interleaved:
      return "RS272_INTERLEAVED";
  }
  return "UNKNOWN";
}

std::string_view to_string(DuplexMode mode) noexcept {
  switch (mode) {
    case DuplexMode::Unknown:
      return "UNKNOWN";
    case DuplexMode::Unsupported:
      return "UNSUPPORTED";
    case DuplexMode::Half:
      return "HALF";
    case DuplexMode::Full:
      return "FULL";
  }
  return "UNKNOWN";
}

std::string_view to_string(AutonegPolicy policy) noexcept {
  switch (policy) {
    case AutonegPolicy::Unknown:
      return "UNKNOWN";
    case AutonegPolicy::Unsupported:
      return "UNSUPPORTED";
    case AutonegPolicy::Forced:
      return "FORCED";
    case AutonegPolicy::Preferred:
      return "PREFERRED";
    case AutonegPolicy::Negotiated:
      return "NEGOTIATED";
  }
  return "UNKNOWN";
}

std::string_view to_string(PauseMode mode) noexcept {
  switch (mode) {
    case PauseMode::Unknown:
      return "UNKNOWN";
    case PauseMode::Unsupported:
      return "UNSUPPORTED";
    case PauseMode::Disabled:
      return "DISABLED";
    case PauseMode::Transmit:
      return "TRANSMIT";
    case PauseMode::Receive:
      return "RECEIVE";
    case PauseMode::Bidirectional:
      return "BIDIRECTIONAL";
  }
  return "UNKNOWN";
}

std::string_view to_string(BreakoutMode mode) noexcept {
  switch (mode) {
    case BreakoutMode::None:
      return "NONE";
    case BreakoutMode::X2:
      return "X2";
    case BreakoutMode::X4:
      return "X4";
    case BreakoutMode::X8:
      return "X8";
  }
  return "NONE";
}

std::string_view to_string(SpeedSelection selection) noexcept {
  switch (selection) {
    case SpeedSelection::Auto:
      return "AUTO";
    case SpeedSelection::Forced:
      return "FORCED";
  }
  return "AUTO";
}

namespace {

template <class Enum, std::size_t N>
bool parse_from_table(std::string_view text, const std::array<std::pair<std::string_view, Enum>, N>& table,
                      Enum& out) noexcept {
  for (const auto& [rendering, value] : table) {
    if (rendering == text) {
      out = value;
      return true;
    }
  }
  return false;
}

}  // namespace

bool parse_port_mode(std::string_view text, PortMode& out) noexcept {
  static const std::array<std::pair<std::string_view, PortMode>, 2> table{{{"PHYSICAL", PortMode::Physical},
                                                                         {"LOGICAL", PortMode::Logical}}};
  return parse_from_table(text, table, out);
}

bool parse_protocol_family(std::string_view text, ProtocolFamily& out) noexcept {
  static const std::array<std::pair<std::string_view, ProtocolFamily>, 7> table{{
      {"UNKNOWN", ProtocolFamily::Unknown},
      {"ETHERNET", ProtocolFamily::Ethernet},
      {"INFINIBAND", ProtocolFamily::InfiniBand},
      {"FIBRECHANNEL", ProtocolFamily::FibreChannel},
      {"OPTICAL_TRANSPORT", ProtocolFamily::OpticalTransport},
      {"OVERLAY", ProtocolFamily::Overlay},
      {"SYNTHETIC", ProtocolFamily::Synthetic}}};
  return parse_from_table(text, table, out);
}

bool parse_fec_mode(std::string_view text, FecMode& out) noexcept {
  static const std::array<std::pair<std::string_view, FecMode>, 8> table{{
      {"UNKNOWN", FecMode::Unknown},
      {"UNSUPPORTED", FecMode::Unsupported},
      {"NONE", FecMode::None},
      {"FIRECODE", FecMode::Firecode},
      {"RS528", FecMode::ReedSolomon528},
      {"RS544", FecMode::ReedSolomon544},
      {"RS544_INTERLEAVED", FecMode::ReedSolomon544Interleaved},
      {"RS272_INTERLEAVED", FecMode::ReedSolomon272Interleaved}}};
  return parse_from_table(text, table, out);
}

bool parse_duplex_mode(std::string_view text, DuplexMode& out) noexcept {
  static const std::array<std::pair<std::string_view, DuplexMode>, 4> table{{{"UNKNOWN", DuplexMode::Unknown},
                                                                            {"UNSUPPORTED", DuplexMode::Unsupported},
                                                                            {"HALF", DuplexMode::Half},
                                                                            {"FULL", DuplexMode::Full}}};
  return parse_from_table(text, table, out);
}

bool parse_autoneg_policy(std::string_view text, AutonegPolicy& out) noexcept {
  static const std::array<std::pair<std::string_view, AutonegPolicy>, 5> table{{
      {"UNKNOWN", AutonegPolicy::Unknown},
      {"UNSUPPORTED", AutonegPolicy::Unsupported},
      {"FORCED", AutonegPolicy::Forced},
      {"PREFERRED", AutonegPolicy::Preferred},
      {"NEGOTIATED", AutonegPolicy::Negotiated}}};
  return parse_from_table(text, table, out);
}

bool parse_pause_mode(std::string_view text, PauseMode& out) noexcept {
  static const std::array<std::pair<std::string_view, PauseMode>, 6> table{{{"UNKNOWN", PauseMode::Unknown},
                                                                           {"UNSUPPORTED", PauseMode::Unsupported},
                                                                           {"DISABLED", PauseMode::Disabled},
                                                                           {"TRANSMIT", PauseMode::Transmit},
                                                                           {"RECEIVE", PauseMode::Receive},
                                                                           {"BIDIRECTIONAL", PauseMode::Bidirectional}}};
  return parse_from_table(text, table, out);
}

bool parse_breakout_mode(std::string_view text, BreakoutMode& out) noexcept {
  static const std::array<std::pair<std::string_view, BreakoutMode>, 4> table{{{"NONE", BreakoutMode::None},
                                                                              {"X2", BreakoutMode::X2},
                                                                              {"X4", BreakoutMode::X4},
                                                                              {"X8", BreakoutMode::X8}}};
  return parse_from_table(text, table, out);
}

bool parse_speed_selection(std::string_view text, SpeedSelection& out) noexcept {
  static const std::array<std::pair<std::string_view, SpeedSelection>, 2> table{
      {{"AUTO", SpeedSelection::Auto}, {"FORCED", SpeedSelection::Forced}}};
  return parse_from_table(text, table, out);
}

bool is_valid(PortMode mode) noexcept {
  return mode == PortMode::Physical || mode == PortMode::Logical;
}

bool is_valid(ProtocolFamily family) noexcept {
  switch (family) {
    case ProtocolFamily::Unknown:
    case ProtocolFamily::Ethernet:
    case ProtocolFamily::InfiniBand:
    case ProtocolFamily::FibreChannel:
    case ProtocolFamily::OpticalTransport:
    case ProtocolFamily::Overlay:
    case ProtocolFamily::Synthetic:
      return true;
  }
  return false;
}

bool is_valid(FecMode mode) noexcept {
  switch (mode) {
    case FecMode::Unknown:
    case FecMode::Unsupported:
    case FecMode::None:
    case FecMode::Firecode:
    case FecMode::ReedSolomon528:
    case FecMode::ReedSolomon544:
    case FecMode::ReedSolomon544Interleaved:
    case FecMode::ReedSolomon272Interleaved:
      return true;
  }
  return false;
}

bool is_valid(DuplexMode mode) noexcept {
  switch (mode) {
    case DuplexMode::Unknown:
    case DuplexMode::Unsupported:
    case DuplexMode::Half:
    case DuplexMode::Full:
      return true;
  }
  return false;
}

bool is_valid(AutonegPolicy policy) noexcept {
  switch (policy) {
    case AutonegPolicy::Unknown:
    case AutonegPolicy::Unsupported:
    case AutonegPolicy::Forced:
    case AutonegPolicy::Preferred:
    case AutonegPolicy::Negotiated:
      return true;
  }
  return false;
}

bool is_valid(PauseMode mode) noexcept {
  switch (mode) {
    case PauseMode::Unknown:
    case PauseMode::Unsupported:
    case PauseMode::Disabled:
    case PauseMode::Transmit:
    case PauseMode::Receive:
    case PauseMode::Bidirectional:
      return true;
  }
  return false;
}

bool is_valid(BreakoutMode mode) noexcept {
  switch (mode) {
    case BreakoutMode::None:
    case BreakoutMode::X2:
    case BreakoutMode::X4:
    case BreakoutMode::X8:
      return true;
  }
  return false;
}

bool is_valid(SpeedSelection selection) noexcept {
  return selection == SpeedSelection::Auto || selection == SpeedSelection::Forced;
}

std::optional<PortSpeed> PortSpeed::from_bits_per_second(std::uint64_t bits_per_second) noexcept {
  if (bits_per_second < min_speed_bits_per_second ||
      bits_per_second > max_speed_bits_per_second) {
    return std::nullopt;
  }
  PortSpeed speed;
  speed.bits_per_second_ = bits_per_second;
  return speed;
}

std::optional<PortSpeed> PortSpeed::parse(std::string_view text) noexcept {
  if (text.empty() || text.size() > 32) {
    return std::nullopt;
  }
  std::uint64_t multiplier = 1;
  std::string_view digits = text;
  const char last = text.back();
  if (last == 'G' || last == 'g') {
    multiplier = 1000000000ull;
    digits = text.substr(0, text.size() - 1);
  } else if (last == 'M' || last == 'm') {
    multiplier = 1000000ull;
    digits = text.substr(0, text.size() - 1);
  } else if (last == 'K' || last == 'k') {
    multiplier = 1000ull;
    digits = text.substr(0, text.size() - 1);
  }
  if (digits.empty() || digits.size() > 20) {
    return std::nullopt;
  }
  std::uint64_t whole = 0;
  std::size_t index = 0;
  while (index < digits.size() && digits[index] >= '0' && digits[index] <= '9') {
    const std::uint64_t digit = static_cast<std::uint64_t>(digits[index] - '0');
    if (whole > (0xffffffffffffffffull - digit) / 10ull) {
      return std::nullopt;
    }
    whole = whole * 10ull + digit;
    ++index;
  }
  std::uint64_t fraction = 0;
  std::uint64_t fraction_scale = 1;
  if (index < digits.size()) {
    if (digits[index] != '.') {
      return std::nullopt;
    }
    ++index;
    std::size_t fraction_digits = 0;
    while (index < digits.size() && digits[index] >= '0' && digits[index] <= '9') {
      if (fraction_digits >= 3) {
        return std::nullopt;
      }
      fraction = fraction * 10ull + static_cast<std::uint64_t>(digits[index] - '0');
      fraction_scale *= 10ull;
      ++fraction_digits;
      ++index;
    }
    if (fraction_digits == 0) {
      return std::nullopt;
    }
  }
  if (index != digits.size()) {
    return std::nullopt;
  }
  std::uint64_t total = 0;
  if (!detail::checked_multiply(whole, multiplier, total)) {
    return std::nullopt;
  }
  // The fractional part is only meaningful in units of the multiplier; for a
  // plain bit rate the fraction is simply the decimal expansion.
  std::uint64_t fractional_value = 0;
  if (multiplier == 1) {
    fractional_value = 0;
    if (fraction_scale != 1) {
      return std::nullopt;
    }
  } else {
    const std::uint64_t unit = multiplier / fraction_scale;
    if (unit == 0 || !detail::checked_multiply(fraction, unit, fractional_value)) {
      return std::nullopt;
    }
  }
  std::uint64_t bits = 0;
  if (!detail::checked_add(total, fractional_value, bits)) {
    return std::nullopt;
  }
  return from_bits_per_second(bits);
}

std::string PortSpeed::to_string() const {
  if (!valid()) {
    return "UNSPECIFIED";
  }
  const std::uint64_t bps = bits_per_second_;
  std::string out;
  if (bps % 1000000000ull == 0) {
    detail::append_u64(out, bps / 1000000000ull);
    out.push_back('G');
    return out;
  }
  if (bps % 1000000ull == 0 && bps > 1000000000ull) {
    const std::uint64_t whole = bps / 1000000000ull;
    std::uint64_t remainder = (bps % 1000000000ull) / 1000000ull;
    detail::append_u64(out, whole);
    out.push_back('.');
    // The fraction is rendered without trailing zeros, so 2.5G stays 2.5G and
    // 1.25G stays 1.25G: the rendering is canonical and reversible.
    std::uint64_t scale = 100ull;
    while (scale > 1ull && remainder % 10ull == 0ull) {
      remainder /= 10ull;
      scale /= 10ull;
    }
    while (scale > 1ull && remainder < scale) {
      out.push_back('0');
      scale /= 10ull;
    }
    detail::append_u64(out, remainder);
    out.push_back('G');
    return out;
  }
  if (bps % 1000000ull == 0) {
    detail::append_u64(out, bps / 1000000ull);
    out.push_back('M');
    return out;
  }
  if (bps % 1000ull == 0) {
    detail::append_u64(out, bps / 1000ull);
    out.push_back('K');
    return out;
  }
  detail::append_u64(out, bps);
  return out;
}

std::optional<LaneConfiguration> LaneConfiguration::create(std::uint32_t lanes,
                                                           PortSpeed per_lane) noexcept {
  if (lanes == 0 || lanes > max_lanes || !per_lane.valid()) {
    return std::nullopt;
  }
  LaneConfiguration configuration;
  configuration.lanes_ = lanes;
  configuration.per_lane_ = per_lane;
  PortSpeed total;
  if (!configuration.total(total)) {
    return std::nullopt;
  }
  return configuration;
}

std::optional<LaneConfiguration> LaneConfiguration::for_total(std::uint32_t lanes,
                                                              PortSpeed total) noexcept {
  if (lanes == 0 || lanes > max_lanes || !total.valid()) {
    return std::nullopt;
  }
  if (total.bits_per_second() % lanes != 0) {
    return std::nullopt;
  }
  const auto per_lane = PortSpeed::from_bits_per_second(total.bits_per_second() / lanes);
  if (!per_lane.has_value()) {
    return std::nullopt;
  }
  return create(lanes, *per_lane);
}

bool LaneConfiguration::total(PortSpeed& out) const noexcept {
  if (!valid()) {
    return false;
  }
  std::uint64_t bits = 0;
  if (!detail::checked_multiply(per_lane_.bits_per_second(), lanes_, bits)) {
    return false;
  }
  const auto speed = PortSpeed::from_bits_per_second(bits);
  if (!speed.has_value()) {
    return false;
  }
  out = *speed;
  return true;
}

std::string LaneConfiguration::to_string() const {
  if (!valid()) {
    return "UNSPECIFIED";
  }
  PortSpeed total_speed;
  std::string out;
  out.append(std::to_string(lanes_));
  out.append("x");
  out.append(per_lane_.to_string());
  if (total(total_speed)) {
    out.append("=");
    out.append(total_speed.to_string());
  }
  return out;
}

std::optional<Mtu> Mtu::create(std::uint32_t value) noexcept {
  if (value < min_mtu || value > max_mtu) {
    return std::nullopt;
  }
  Mtu mtu;
  mtu.value_ = value;
  return mtu;
}

std::string Mtu::to_string() const {
  if (!valid()) {
    return "UNSPECIFIED";
  }
  return std::to_string(value_);
}

std::string SpeedSetting::to_string() const {
  std::string out(portfabric::to_string(selection));
  if (selection == SpeedSelection::Forced) {
    out.push_back('(');
    out.append(rate.to_string());
    out.push_back(')');
  }
  return out;
}

std::string AggregationMembership::to_string() const {
  if (!member) {
    return "NONE";
  }
  std::string out;
  out.append(aggregate.to_string());
  out.push_back('@');
  out.append(generation.to_string());
  out.append(administrative_participation ? " participating" : " passive");
  return out;
}

}  // namespace portfabric
