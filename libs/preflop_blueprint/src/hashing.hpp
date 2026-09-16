#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// FNV-1a helpers shared by the fingerprints of the preflop blueprint library.
namespace gtosd::preflop_blueprint::detail {

inline constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
inline constexpr std::uint64_t fnv_prime_64 = 1'099'511'628'211ULL;

[[nodiscard]] inline std::uint64_t fnv1a_text(const std::string_view text,
                                              std::uint64_t hash = fnv_offset_basis) noexcept {
  for (const auto character : text) {
    hash ^= static_cast<std::uint8_t>(character);
    hash *= fnv_prime_64;
  }
  return hash;
}

[[nodiscard]] inline std::string hex64_text(std::uint64_t value) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result(16U, '0');
  for (std::size_t index = 0; index < result.size(); ++index) {
    result[result.size() - 1U - index] = digits[static_cast<std::size_t>(value & 0xFU)];
    value >>= 4U;
  }
  return result;
}

} // namespace gtosd::preflop_blueprint::detail
