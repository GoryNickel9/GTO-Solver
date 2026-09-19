#include "../benchmarks/preflop_blueprint_temporal_distance.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include <cmath>
#include <iostream>

namespace research = gtosd::research;
void require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
int main() {
  try {
    const std::array<research::EquityHistogram, 2> unresolved{{{1, 1}, {1, 1}}};
    const std::array<research::EquityHistogram, 2> revealed{{{2, 0}, {0, 2}}};
    const auto witness = research::temporal_distance(unresolved, revealed);
    require(witness.marginal == 0 && witness.potential == 0.5,
            "identical river marginals can hide different turn information");
    gtosd::card_abstraction::DeterministicRandom random(20260919);
    std::size_t cases = 0;
    for (std::size_t n = 1; n <= 6; ++n) {
      for (std::size_t trial = 0; trial < 16; ++trial) {
        std::vector<research::EquityHistogram> left(n), right(n);
        for (auto *side : {&left, &right}) {
          for (auto &histogram : *side) {
            for (int draw = 0; draw < 4; ++draw) {
              ++histogram[random.uniform_below(16)];
            }
          }
        }
        const auto actual = research::temporal_distance(left, right);
        std::vector<std::size_t> permutation(n);
        std::iota(permutation.begin(), permutation.end(), 0U);
        auto expected = std::numeric_limits<std::int64_t>::max();
        do {
          std::int64_t sum = 0;
          for (std::size_t i = 0; i < n; ++i) {
            sum += research::histogram_distance(left[i], right[permutation[i]]);
          }
          expected = std::min(expected, sum);
        } while (std::next_permutation(permutation.begin(), permutation.end()));
        require(actual.assignment_cost == expected,
                "assignment equals exhaustive permutation enumeration");
        require(actual.potential + 1e-12 >= actual.marginal,
                "conditional transport dominates marginal transport");
        const auto inverse = research::temporal_distance(right, left);
        require(actual.potential == inverse.potential, "distance symmetry");
        std::reverse(right.begin(), right.end());
        require(actual.potential == research::temporal_distance(left, right).potential,
                "future-state order does not matter");
        require(research::temporal_distance(left, left).potential == 0,
                "identical distributions have zero distance");
        ++cases;
      }
    }
    bool rejected = false;
    try {
      research::temporal_distance(std::span<const research::EquityHistogram>{}, revealed);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    require(rejected, "empty collections rejected");
    std::cout << "TEMPORAL_DISTANCE_TESTS=PASS exhaustive_cases=" << cases
              << " hidden-turn-witness=0_vs_0.5\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "TEMPORAL_DISTANCE_TESTS=FAIL " << error.what() << '\n';
    return 1;
  }
}
