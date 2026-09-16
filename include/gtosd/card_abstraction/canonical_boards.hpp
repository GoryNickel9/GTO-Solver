#pragma once

#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/core/cards.hpp"
#include "gtosd/core/result.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Lossless suit canonicalization of Short Deck boards and the catalogs of
// canonical boards with their physical multiplicities.
//
// Four objects are canonicalized under the 24 suit permutations:
// - the flop as an unordered 3-set;
// - the flop plus the turn card;
// - the complete board as an unordered 5-set (what a river bucket observes);
// - the board history flop -> turn -> river (what the strategies observe).
// Every lookup returns the catalog index and the permutation that maps the
// physical cards into the canonical frame, so private hands can be moved into
// the same frame before a table lookup.
namespace gtosd::card_abstraction {

// new_suit = permutation[old_suit]
using SuitPermutation = std::array<std::uint8_t, 4>;
inline constexpr std::size_t suit_permutation_count = 24U;
inline constexpr SuitPermutation identity_permutation{0U, 1U, 2U, 3U};

// Physical counts the catalogs must reproduce.
inline constexpr std::uint32_t physical_flops = 7'140U;
inline constexpr std::uint32_t physical_flop_turns = 235'620U;
inline constexpr std::uint32_t physical_river_boards = 376'992U;
inline constexpr std::uint32_t physical_board_histories = 7'539'840U;

// Canonical counts established by enumeration; the catalog verifies them.
inline constexpr std::uint32_t canonical_flop_count = 573U;
inline constexpr std::uint32_t canonical_flop_turn_count = 13'761U;
inline constexpr std::uint32_t canonical_river_board_count = 19'998U;
inline constexpr std::uint32_t canonical_board_history_count = 369'072U;

[[nodiscard]] const std::array<SuitPermutation, suit_permutation_count> &
all_suit_permutations() noexcept;
[[nodiscard]] SuitPermutation inverse_permutation(const SuitPermutation &permutation) noexcept;
[[nodiscard]] constexpr CardId permute_card(const CardId card,
                                            const SuitPermutation &permutation) noexcept {
  return CardId::from_parts(card.rank(),
                            static_cast<Suit>(permutation[static_cast<std::size_t>(card.suit())]));
}

struct BoardHistory {
  // Flop stored in increasing card order; turn and river as dealt.
  std::array<CardId, 3> flop{};
  CardId turn{};
  CardId river{};

  friend bool operator==(const BoardHistory &, const BoardHistory &) = default;
};

struct Canonicalization {
  // Packed code of the canonical representative (6 bits per card).
  std::uint32_t code{0U};
  // Lexicographically smallest permutation reaching the code.
  SuitPermutation permutation{identity_permutation};
  // Number of distinct physical images under the 24 permutations.
  std::uint32_t orbit_size{0U};
};

[[nodiscard]] Result<Canonicalization, CardError>
canonicalize_flop(const std::array<CardId, 3> &flop);
[[nodiscard]] Result<Canonicalization, CardError>
canonicalize_flop_turn(const std::array<CardId, 3> &flop, CardId turn);
[[nodiscard]] Result<Canonicalization, CardError>
canonicalize_river_board(const std::array<CardId, 5> &cards);
[[nodiscard]] Result<Canonicalization, CardError> canonicalize_history(const BoardHistory &history);

struct CanonicalFlop {
  std::array<CardId, 3> cards{};
  std::uint32_t multiplicity{0U};
  std::uint32_t code{0U};
};

struct CanonicalFlopTurn {
  std::array<CardId, 3> flop{};
  CardId turn{};
  std::uint32_t multiplicity{0U};
  std::uint32_t flop_index{0U};
  std::uint32_t code{0U};
};

struct CanonicalRiverBoard {
  std::array<CardId, 5> cards{};
  std::uint32_t multiplicity{0U};
  std::uint32_t code{0U};
};

struct CanonicalBoardHistory {
  BoardHistory history{};
  std::uint32_t multiplicity{0U};
  std::uint32_t flop_index{0U};
  std::uint32_t flop_turn_index{0U};
  std::uint32_t river_board_index{0U};
  std::uint32_t code{0U};
};

struct CanonicalLookup {
  std::uint32_t index{0U};
  SuitPermutation permutation{identity_permutation};
};

struct CatalogBuildTelemetry {
  double flop_seconds{0.0};
  double flop_turn_seconds{0.0};
  double river_board_seconds{0.0};
  double history_seconds{0.0};
  double cross_reference_seconds{0.0};
};

enum class CatalogError : std::uint8_t { IoFailure, IntegrityFailure, UnsupportedVersion };

class BoardCatalog {
public:
  static constexpr std::uint32_t format_version = 1U;

  [[nodiscard]] static BoardCatalog build(CatalogBuildTelemetry *telemetry = nullptr);

  [[nodiscard]] const std::vector<CanonicalFlop> &flops() const noexcept { return flops_; }
  [[nodiscard]] const std::vector<CanonicalFlopTurn> &flop_turns() const noexcept {
    return flop_turns_;
  }
  [[nodiscard]] const std::vector<CanonicalRiverBoard> &river_boards() const noexcept {
    return river_boards_;
  }
  [[nodiscard]] const std::vector<CanonicalBoardHistory> &histories() const noexcept {
    return histories_;
  }

  [[nodiscard]] Result<CanonicalLookup, CardError>
  lookup_flop(const std::array<CardId, 3> &flop) const;
  [[nodiscard]] Result<CanonicalLookup, CardError>
  lookup_flop_turn(const std::array<CardId, 3> &flop, CardId turn) const;
  [[nodiscard]] Result<CanonicalLookup, CardError>
  lookup_river_board(const std::array<CardId, 5> &cards) const;
  [[nodiscard]] Result<CanonicalLookup, CardError>
  lookup_history(const BoardHistory &history) const;

  // Uniform over the 7,539,840 physical histories.
  [[nodiscard]] BoardHistory sample_physical_history(DeterministicRandom &random) const;
  // Canonical index with probability proportional to multiplicity; equivalent
  // in distribution to sampling a physical history and canonicalizing it.
  [[nodiscard]] std::uint32_t sample_history_index(DeterministicRandom &random) const;
  // Deterministic core of sample_history_index for a quantile in
  // [0, physical_board_histories).
  [[nodiscard]] std::uint32_t history_index_from_quantile(std::uint32_t quantile) const noexcept;

  [[nodiscard]] const std::string &fingerprint() const noexcept { return fingerprint_; }
  [[nodiscard]] std::uint64_t byte_size() const noexcept;

  [[nodiscard]] Result<bool, CatalogError> save(const std::filesystem::path &path) const;
  [[nodiscard]] static Result<BoardCatalog, CatalogError> load(const std::filesystem::path &path);

  friend bool operator==(const BoardCatalog &left, const BoardCatalog &right);

private:
  void finalize();

  std::vector<CanonicalFlop> flops_;
  std::vector<CanonicalFlopTurn> flop_turns_;
  std::vector<CanonicalRiverBoard> river_boards_;
  std::vector<CanonicalBoardHistory> histories_;
  std::vector<std::uint32_t> flop_codes_;
  std::vector<std::uint32_t> flop_turn_codes_;
  std::vector<std::uint32_t> river_board_codes_;
  std::vector<std::uint32_t> history_codes_;
  std::vector<std::uint32_t> history_cumulative_multiplicity_;
  std::string fingerprint_;
};

[[nodiscard]] const char *catalog_error_name(CatalogError error) noexcept;

} // namespace gtosd::card_abstraction
