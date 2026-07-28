#pragma once

#include "gtosd/core/game.hpp"
#include "gtosd/core/ranges.hpp"
#include "gtosd/core/result.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gtosd {

struct SuitPermutation {
  std::array<Suit, 4> forward{Suit::Clubs, Suit::Diamonds, Suit::Hearts, Suit::Spades};

  friend bool operator==(const SuitPermutation &, const SuitPermutation &) = default;
};

struct WeightedCombo {
  Combo combo{};
  RangeWeight weight{};

  friend bool operator==(const WeightedCombo &, const WeightedCombo &) = default;
};

using WeightedRange = std::vector<WeightedCombo>;

struct NodelockEntry {
  std::uint64_t board_mask{0};
  Combo private_hand{};
  std::uint8_t player{0};
  std::string betting_history;
  std::vector<RangeWeight> action_weights;

  friend bool operator==(const NodelockEntry &, const NodelockEntry &) = default;
};

struct CanonicalStateInput {
  std::uint32_t game_version{1};
  PublicState public_state{};
  std::string betting_history;
  std::array<WeightedRange, 2> ranges{};
  std::vector<Combo> private_deal;
  std::vector<CardId> dead_cards;
  std::vector<CardId> future_cards;
  std::vector<NodelockEntry> nodelocks;

  friend bool operator==(const CanonicalStateInput &, const CanonicalStateInput &) = default;
};

enum class IsomorphismError : std::uint8_t {
  InvalidPermutation,
  InvalidBoard,
  InvalidPhysicalCards,
  InvalidRange,
  InvalidNodelock,
  EmptyChanceOutcomes,
  IllegalChanceCard
};

struct CanonicalState {
  std::string key;
  CanonicalStateInput state;
  SuitPermutation physical_to_canonical{};
  SuitPermutation canonical_to_physical{};
};

struct CanonicalCacheStats {
  std::uint64_t queries{0};
  std::uint64_t hits{0};
  std::uint64_t misses{0};
  std::uint64_t hash_collisions{0};
};

class CanonicalKeyCache {
public:
  [[nodiscard]] Result<CanonicalState, IsomorphismError>
  canonicalize(const CanonicalStateInput &input);
  [[nodiscard]] const CanonicalCacheStats &stats() const noexcept { return stats_; }
  [[nodiscard]] double hit_rate() const noexcept;
  void clear();

private:
  struct Entry {
    std::string physical_key;
    CanonicalState canonical;
  };

  std::unordered_map<std::uint64_t, std::vector<Entry>> entries_;
  CanonicalCacheStats stats_{};
};

struct CanonicalChanceEdge {
  std::string canonical_key;
  CanonicalState child;
  CardId representative_card{};
  std::vector<CardId> physical_cards;
  std::uint32_t physical_outcome_count{0};
  std::uint32_t total_legal_outcome_count{0};
};

struct OrbitEntry {
  SuitPermutation permutation{};
  std::string serialized_state;
  bool is_canonical{false};
};

[[nodiscard]] const std::array<SuitPermutation, 24> &all_suit_permutations() noexcept;
[[nodiscard]] Result<SuitPermutation, IsomorphismError>
inverse_permutation(const SuitPermutation &permutation);

[[nodiscard]] Result<CardId, IsomorphismError> transform_card(CardId card,
                                                              const SuitPermutation &permutation);
[[nodiscard]] Result<std::uint64_t, IsomorphismError>
transform_card_mask(std::uint64_t mask, const SuitPermutation &permutation);
[[nodiscard]] Result<Combo, IsomorphismError> transform_combo(Combo combo,
                                                              const SuitPermutation &permutation);
[[nodiscard]] Result<WeightedRange, IsomorphismError>
transform_range(const WeightedRange &range, const SuitPermutation &permutation);
[[nodiscard]] Result<NodelockEntry, IsomorphismError>
transform_nodelock(const NodelockEntry &nodelock, const SuitPermutation &permutation);
[[nodiscard]] Result<CanonicalStateInput, IsomorphismError>
transform_state(const CanonicalStateInput &input, const SuitPermutation &permutation);

[[nodiscard]] Result<CanonicalState, IsomorphismError>
canonicalize_state(const CanonicalStateInput &input);
[[nodiscard]] Result<std::vector<OrbitEntry>, IsomorphismError>
audit_orbit(const CanonicalStateInput &input);
[[nodiscard]] Result<std::vector<CanonicalChanceEdge>, IsomorphismError>
aggregate_chance_outcomes(const CanonicalStateInput &parent,
                          const std::vector<CardId> &legal_cards);

[[nodiscard]] std::string serialize_isomorphic_state(const CanonicalStateInput &input);
[[nodiscard]] std::string permutation_name(const SuitPermutation &permutation);
[[nodiscard]] const char *isomorphism_error_name(IsomorphismError error) noexcept;

} // namespace gtosd
