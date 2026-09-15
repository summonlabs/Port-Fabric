#include "sha256.hpp"

#include <cstring>

namespace portfabric::detail {
namespace {

constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

constexpr std::uint32_t rotr(std::uint32_t value, unsigned count) noexcept {
  return (value >> count) | (value << (32u - count));
}

constexpr std::uint32_t big_sigma0(std::uint32_t value) noexcept {
  return rotr(value, 2) ^ rotr(value, 13) ^ rotr(value, 22);
}

constexpr std::uint32_t big_sigma1(std::uint32_t value) noexcept {
  return rotr(value, 6) ^ rotr(value, 11) ^ rotr(value, 25);
}

constexpr std::uint32_t small_sigma0(std::uint32_t value) noexcept {
  return rotr(value, 7) ^ rotr(value, 18) ^ (value >> 3);
}

constexpr std::uint32_t small_sigma1(std::uint32_t value) noexcept {
  return rotr(value, 17) ^ rotr(value, 19) ^ (value >> 10);
}

}  // namespace

Sha256::Sha256() noexcept
    : state_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu,
             0x1f83d9abu, 0x5be0cd19u} {}

void Sha256::compress(const std::uint8_t* block) noexcept {
  std::uint32_t schedule[64];
  for (std::size_t index = 0; index < 16; ++index) {
    schedule[index] = (static_cast<std::uint32_t>(block[index * 4]) << 24u) |
                      (static_cast<std::uint32_t>(block[index * 4 + 1]) << 16u) |
                      (static_cast<std::uint32_t>(block[index * 4 + 2]) << 8u) |
                      static_cast<std::uint32_t>(block[index * 4 + 3]);
  }
  for (std::size_t index = 16; index < 64; ++index) {
    schedule[index] = small_sigma1(schedule[index - 2]) + schedule[index - 7] +
                      small_sigma0(schedule[index - 15]) + schedule[index - 16];
  }
  std::uint32_t a = state_[0];
  std::uint32_t b = state_[1];
  std::uint32_t c = state_[2];
  std::uint32_t d = state_[3];
  std::uint32_t e = state_[4];
  std::uint32_t f = state_[5];
  std::uint32_t g = state_[6];
  std::uint32_t h = state_[7];
  for (std::size_t index = 0; index < 64; ++index) {
    const std::uint32_t temp1 =
        h + big_sigma1(e) + ((e & f) ^ (~e & g)) + kRoundConstants[index] + schedule[index];
    const std::uint32_t temp2 = big_sigma0(a) + ((a & b) ^ (a & c) ^ (b & c));
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

void Sha256::update(std::span<const std::byte> bytes) noexcept {
  if (finished_) {
    return;
  }
  std::size_t offset = 0;
  total_bytes_ += static_cast<std::uint64_t>(bytes.size());
  while (offset < bytes.size()) {
    const std::size_t space = 64 - buffer_size_;
    const std::size_t take = (bytes.size() - offset) < space ? (bytes.size() - offset) : space;
    std::memcpy(buffer_.data() + buffer_size_, bytes.data() + offset, take);
    buffer_size_ += take;
    offset += take;
    if (buffer_size_ == 64) {
      compress(buffer_.data());
      buffer_size_ = 0;
    }
  }
}

void Sha256::update(const std::string& text) noexcept {
  update(std::span<const std::byte>(reinterpret_cast<const std::byte*>(text.data()), text.size()));
}

std::array<std::byte, Sha256::digest_size> Sha256::finish() noexcept {
  if (!finished_) {
    const std::uint64_t bit_length = total_bytes_ * 8ull;
    const std::uint8_t padding = 0x80u;
    update(std::span<const std::byte>(reinterpret_cast<const std::byte*>(&padding), 1));
    const std::uint8_t zero = 0;
    while (buffer_size_ != 56) {
      update(std::span<const std::byte>(reinterpret_cast<const std::byte*>(&zero), 1));
    }
    std::uint8_t length_bytes[8];
    for (std::size_t index = 0; index < 8; ++index) {
      length_bytes[index] =
          static_cast<std::uint8_t>((bit_length >> (56u - 8u * static_cast<unsigned>(index))) & 0xffu);
    }
    update(std::span<const std::byte>(reinterpret_cast<const std::byte*>(length_bytes), 8));
    finished_ = true;
  }
  std::array<std::byte, digest_size> out{};
  for (std::size_t index = 0; index < 8; ++index) {
    out[index * 4] = static_cast<std::byte>((state_[index] >> 24u) & 0xffu);
    out[index * 4 + 1] = static_cast<std::byte>((state_[index] >> 16u) & 0xffu);
    out[index * 4 + 2] = static_cast<std::byte>((state_[index] >> 8u) & 0xffu);
    out[index * 4 + 3] = static_cast<std::byte>(state_[index] & 0xffu);
  }
  return out;
}

}  // namespace portfabric::detail
