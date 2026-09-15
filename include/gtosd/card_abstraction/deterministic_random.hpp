#pragma once

#include <array>
#include <cstdint>

// Platform-independent pseudo-random source for the blueprint solver.
//
// The standard library distributions are implementation-defined, so a
// sequence drawn through std::uniform_int_distribution differs between
// compilers. Every sampling decision of the new solver goes through this
// class (xoshiro256** seeded by splitmix64, bounded draws by Lemire's
// multiply-and-reject method) so that a seed identifies the same boards on
// any platform and the bit-identity tests keep their meaning.
namespace gtosd::card_abstraction {

class DeterministicRandom {
public:
  using State = std::array<std::uint64_t, 4>;

  explicit constexpr DeterministicRandom(std::uint64_t seed) noexcept { reseed(seed); }

  constexpr void reseed(std::uint64_t seed) noexcept {
    for (auto &word : state_) {
      word = splitmix64(seed);
    }
    if (state_[0] == 0U && state_[1] == 0U && state_[2] == 0U && state_[3] == 0U) {
      state_[0] = 0x9E37'79B9'7F4A'7C15ULL;
    }
  }

  [[nodiscard]] constexpr std::uint64_t next() noexcept {
    const std::uint64_t result = rotate_left(state_[1] * 5U, 7U) * 9U;
    const std::uint64_t t = state_[1] << 17U;
    state_[2] ^= state_[0];
    state_[3] ^= state_[1];
    state_[1] ^= state_[2];
    state_[0] ^= state_[3];
    state_[2] ^= t;
    state_[3] = rotate_left(state_[3], 45U);
    return result;
  }

  // Unbiased integer in [0, bound) for 1 <= bound <= 2^32 - 1.
  [[nodiscard]] constexpr std::uint32_t uniform_below(const std::uint32_t bound) noexcept {
    if (bound <= 1U) {
      return 0U;
    }
    std::uint32_t x = static_cast<std::uint32_t>(next() >> 32U);
    std::uint64_t m = static_cast<std::uint64_t>(x) * bound;
    auto l = static_cast<std::uint32_t>(m);
    if (l < bound) {
      const std::uint32_t threshold = (0U - bound) % bound;
      while (l < threshold) {
        x = static_cast<std::uint32_t>(next() >> 32U);
        m = static_cast<std::uint64_t>(x) * bound;
        l = static_cast<std::uint32_t>(m);
      }
    }
    return static_cast<std::uint32_t>(m >> 32U);
  }

  // Uniform double in [0, 1) with 53 random bits.
  [[nodiscard]] constexpr double uniform_unit() noexcept {
    constexpr double scale = 1.0 / 9007199254740992.0;
    return static_cast<double>(next() >> 11U) * scale;
  }

  [[nodiscard]] constexpr const State &state() const noexcept { return state_; }
  constexpr void restore(const State &state) noexcept { state_ = state; }

  [[nodiscard]] static constexpr std::uint64_t splitmix64(std::uint64_t &value) noexcept {
    value += 0x9E37'79B9'7F4A'7C15ULL;
    std::uint64_t z = value;
    z = (z ^ (z >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D0'49BB'1331'11EBULL;
    return z ^ (z >> 31U);
  }

private:
  [[nodiscard]] static constexpr std::uint64_t rotate_left(const std::uint64_t value,
                                                           const unsigned shift) noexcept {
    return (value << shift) | (value >> (64U - shift));
  }

  State state_{};
};

} // namespace gtosd::card_abstraction
