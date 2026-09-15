#pragma once

#include "gtosd/preflop/hu_preflop.hpp"
#include "gtosd/solver/finite_game.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gtosd {

struct HuPreflopAbstractChanceOutcome {
  HuPreflopRootConditionalDeal deal{};
  double probability{0.0};

  friend bool operator==(const HuPreflopAbstractChanceOutcome &,
                         const HuPreflopAbstractChanceOutcome &) = default;
};

// Frozen empirical chance distribution. CO hand-class mass is exact; BTN
// holes and the complete public runout are sampled conditionally and then
// treated as immutable chance outcomes by both trainer and certifier.
struct HuPreflopAbstractChanceCorpus {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 0U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string abstract_game_definition_fingerprint;
  std::uint64_t seed{0U};
  std::uint32_t deals_per_co_class{0U};
  std::vector<HuPreflopAbstractChanceOutcome> outcomes;
  std::string fingerprint;
};

struct HuPreflopCompiledAbstractGame {
  HuPreflopAbstractGameDefinition definition{};
  HuPreflopAbstractChanceCorpus chance_corpus{};
  FiniteGame game{};
  GameSummary summary{};
};

[[nodiscard]] std::string
fingerprint_hu_preflop_abstract_chance_corpus(const HuPreflopAbstractChanceCorpus &corpus);

// Materializes the exact finite empirical game. maximum_nodes is a hard
// pre-allocation/runtime guard, not an estimate.
[[nodiscard]] Result<HuPreflopCompiledAbstractGame, HuPreflopError>
compile_hu_preflop_abstract_game(const HuPreflopTree &tree, const HuPreflopSolveOptions &options,
                                 std::uint32_t deals_per_co_class, std::uint64_t corpus_seed,
                                 std::uint64_t maximum_nodes);

} // namespace gtosd
