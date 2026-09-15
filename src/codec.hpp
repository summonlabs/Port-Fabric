#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace portfabric::detail {

/// Bounded little-endian byte writer.
///
/// Every write is checked against the caller supplied limit before the buffer
/// grows. Once a write fails the writer is poisoned: subsequent writes are
/// ignored and ok() stays false, so a partially encoded document can never be
/// mistaken for a complete one.
class ByteWriter {
 public:
  ByteWriter(std::string& out, std::size_t limit) noexcept : out_(out), limit_(limit) {}

  bool ok() const noexcept { return ok_; }

  /// Number of bytes written so far.
  std::size_t size() const noexcept { return out_.size(); }

  void u8(std::uint8_t value);
  void u16(std::uint16_t value);
  void u32(std::uint32_t value);
  void u64(std::uint64_t value);
  void boolean(bool value);
  void raw(std::string_view bytes);
  /// Length prefixed text with an explicit per-field bound.
  void text(std::string_view value, std::size_t max_length);
  /// Length prefixed byte string with an explicit bound.
  void blob(std::string_view value, std::size_t max_length);

 private:
  void require(std::size_t additional);

  std::string& out_;
  std::size_t limit_ = 0;
  bool ok_ = true;
};

/// Bounded little-endian byte reader.
class ByteReader {
 public:
  explicit ByteReader(std::string_view input) noexcept : input_(input) {}

  bool ok() const noexcept { return ok_; }
  std::size_t remaining() const noexcept { return input_.size() - offset_; }

  /// Number of bytes consumed so far.
  std::size_t consumed() const noexcept { return offset_; }

  bool u8(std::uint8_t& value);
  bool u16(std::uint16_t& value);
  bool u32(std::uint32_t& value);
  bool u64(std::uint64_t& value);
  bool boolean(bool& value);
  bool raw(std::size_t length, std::string& out);
  bool text(std::size_t max_length, std::string& out);
  bool blob(std::size_t max_length, std::string& out);

  /// True when every byte of the input has been consumed.
  bool exhausted() const noexcept { return offset_ == input_.size(); }

 private:
  bool fail() noexcept {
    ok_ = false;
    return false;
  }

  std::string_view input_;
  std::size_t offset_ = 0;
  bool ok_ = true;
};

/// Standard IEEE CRC-32 used for container and frame integrity checks.
std::uint32_t crc32(std::string_view bytes) noexcept;

}  // namespace portfabric::detail
