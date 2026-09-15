#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace portfabric::detail {

/// Minimal SHA-256 implementation used for deterministic content digests.
///
/// The implementation is self contained: Port Fabric has no third-party
/// dependencies. It is used for identity of canonical byte streams, not for
/// secrecy.
class Sha256 {
 public:
  static constexpr std::size_t digest_size = 32;

  Sha256() noexcept;

  void update(std::span<const std::byte> bytes) noexcept;
  void update(const std::string& text) noexcept;
  std::array<std::byte, digest_size> finish() noexcept;

 private:
  void compress(const std::uint8_t* block) noexcept;

  std::array<std::uint32_t, 8> state_;
  std::array<std::uint8_t, 64> buffer_{};
  std::uint64_t total_bytes_ = 0;
  std::size_t buffer_size_ = 0;
  bool finished_ = false;
};

}  // namespace portfabric::detail
