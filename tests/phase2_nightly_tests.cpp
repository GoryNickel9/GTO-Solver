#include "phase2_oracle.hpp"

#include "gtosd/equity/evaluator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

constexpr std::uint64_t seed = 0xF2'5EED'2026ULL;
constexpr std::uint64_t deal_count = 1'000'000U;

std::uint64_t next_random(std::uint64_t &state) {
  state ^= state << 13U;
  state ^= state >> 7U;
  state ^= state << 17U;
  return state;
}

} // namespace

int main() {
  try {
    const auto canonical_deck = gtosd::short_deck();
    std::uint64_t random_state = seed;
    std::array<std::uint64_t, 9> categories{};

    for (std::uint64_t deal = 0; deal < deal_count; ++deal) {
      auto deck = canonical_deck;
      for (std::size_t index = 0; index < 7U; ++index) {
        const auto remaining = deck.size() - index;
        const auto selected =
            index + static_cast<std::size_t>(next_random(random_state) % remaining);
        std::swap(deck[index], deck[selected]);
      }
      const std::array<gtosd::CardId, 7> cards{deck[0], deck[1], deck[2], deck[3],
                                               deck[4], deck[5], deck[6]};
      const auto actual = gtosd::evaluate_seven(cards);
      if (!actual) {
        throw std::runtime_error("production evaluator rejected a valid deal");
      }
      const auto expected = phase2_oracle::evaluate(cards);
      if (actual.value() != expected) {
        throw std::runtime_error("million-deal oracle mismatch");
      }
      ++categories[static_cast<std::size_t>(actual.value().category)];
    }

    for (const auto count : categories) {
      if (count == 0U) {
        throw std::runtime_error("million-deal sample omitted a category");
      }
    }
    std::cout << "F2_NIGHTLY_ORACLE=PASS\n"
              << "seed=" << seed << '\n'
              << "deals=" << deal_count << '\n'
              << "mismatches=0\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "F2_NIGHTLY_ORACLE=FAIL: " << error.what() << '\n';
    return 1;
  }
}
