#include "codec.hpp"

#include <array>

namespace portfabric::detail {
namespace {

const std::array<std::uint32_t, 256>& crc_table() {
  static const std::array<std::uint32_t, 256> table = [] {
    std::array<std::uint32_t, 256> values{};
    // The table is filled by iterating the elements themselves, so no index is
    // ever computed and no bounds assumption has to be proven by an analyzer.
    std::uint32_t index = 0;
    for (std::uint32_t& slot : values) {
      std::uint32_t value = index++;
      for (int bit = 0; bit < 8; ++bit) {
        value = (value & 1u) != 0u ? (0xedb88320u ^ (value >> 1u)) : (value >> 1u);
      }
      slot = value;
    }
    return values;
  }();
  return table;
}

}  // namespace

void ByteWriter::require(std::size_t additional) {
  if (!ok_) {
    return;
  }
  if (additional > limit_ || out_.size() > limit_ - additional) {
    ok_ = false;
  }
}

void ByteWriter::u8(std::uint8_t value) {
  require(1);
  if (!ok_) {
    return;
  }
  out_.push_back(static_cast<char>(value));
}

void ByteWriter::u16(std::uint16_t value) {
  require(2);
  if (!ok_) {
    return;
  }
  out_.push_back(static_cast<char>(value & 0xffu));
  out_.push_back(static_cast<char>((value >> 8u) & 0xffu));
}

void ByteWriter::u32(std::uint32_t value) {
  require(4);
  if (!ok_) {
    return;
  }
  for (unsigned shift = 0; shift < 32; shift += 8) {
    out_.push_back(static_cast<char>((value >> shift) & 0xffu));
  }
}

void ByteWriter::u64(std::uint64_t value) {
  require(8);
  if (!ok_) {
    return;
  }
  for (unsigned shift = 0; shift < 64; shift += 8) {
    out_.push_back(static_cast<char>((value >> shift) & 0xffu));
  }
}

void ByteWriter::boolean(bool value) { u8(value ? 1u : 0u); }

void ByteWriter::raw(std::string_view bytes) {
  require(bytes.size());
  if (!ok_) {
    return;
  }
  out_.append(bytes);
}

void ByteWriter::text(std::string_view value, std::size_t max_length) {
  if (value.size() > max_length) {
    ok_ = false;
    return;
  }
  u32(static_cast<std::uint32_t>(value.size()));
  raw(value);
}

void ByteWriter::blob(std::string_view value, std::size_t max_length) {
  if (value.size() > max_length) {
    ok_ = false;
    return;
  }
  u32(static_cast<std::uint32_t>(value.size()));
  raw(value);
}

bool ByteReader::u8(std::uint8_t& value) {
  if (!ok_ || remaining() < 1) {
    return fail();
  }
  value = static_cast<std::uint8_t>(input_[offset_]);
  ++offset_;
  return true;
}

bool ByteReader::u16(std::uint16_t& value) {
  if (!ok_ || remaining() < 2) {
    return fail();
  }
  value = static_cast<std::uint16_t>(static_cast<unsigned char>(input_[offset_])) |
          static_cast<std::uint16_t>(static_cast<unsigned char>(input_[offset_ + 1]) << 8u);
  offset_ += 2;
  return true;
}

bool ByteReader::u32(std::uint32_t& value) {
  if (!ok_ || remaining() < 4) {
    return fail();
  }
  std::uint32_t result = 0;
  for (unsigned index = 0; index < 4; ++index) {
    result |= static_cast<std::uint32_t>(static_cast<unsigned char>(input_[offset_ + index]))
              << (8u * index);
  }
  offset_ += 4;
  value = result;
  return true;
}

bool ByteReader::u64(std::uint64_t& value) {
  if (!ok_ || remaining() < 8) {
    return fail();
  }
  std::uint64_t result = 0;
  for (unsigned index = 0; index < 8; ++index) {
    result |= static_cast<std::uint64_t>(static_cast<unsigned char>(input_[offset_ + index]))
              << (8u * index);
  }
  offset_ += 8;
  value = result;
  return true;
}

bool ByteReader::boolean(bool& value) {
  std::uint8_t raw_value = 0;
  if (!u8(raw_value)) {
    return false;
  }
  if (raw_value > 1u) {
    return fail();
  }
  value = raw_value == 1u;
  return true;
}

bool ByteReader::raw(std::size_t length, std::string& out) {
  if (!ok_ || remaining() < length) {
    return fail();
  }
  out.assign(input_.substr(offset_, length));
  offset_ += length;
  return true;
}

bool ByteReader::text(std::size_t max_length, std::string& out) {
  std::uint32_t length = 0;
  if (!u32(length)) {
    return false;
  }
  if (length > max_length) {
    return fail();
  }
  return raw(length, out);
}

bool ByteReader::blob(std::size_t max_length, std::string& out) { return text(max_length, out); }

std::uint32_t crc32(std::string_view bytes) noexcept {
  const std::array<std::uint32_t, 256>& table = crc_table();
  std::uint32_t crc = 0xffffffffu;
  for (const char ch : bytes) {
    crc = table[(crc ^ static_cast<std::uint8_t>(ch)) & 0xffu] ^ (crc >> 8u);
  }
  return crc ^ 0xffffffffu;
}

}  // namespace portfabric::detail
