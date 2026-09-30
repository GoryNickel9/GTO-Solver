#pragma once

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Exact three-player showdown outcomes at the granularity of the 81 preflop
// classes, for the 3-way checkdown (step 1) and 3-way preflop all-ins.
//
// Entry (H, A, B) is computed for a fixed representative hero combo h of class
// H (its lowest combo id). It sums, over every ordered pair (a in A, b in B)
// with h, a and b mutually disjoint and over the C(30,5) = 142,506 runouts of
// the remaining cards, the hero's result against a crossed with the hero's
// result against b. The checkdown game is invariant under the 24 suit
// permutations, so with class-constant opponent reach every combo of H has the
// same entry and the class table is exact, not an abstraction. A 2-way
// showdown after a fold reads entry (H, opponent, folded) with the folded
// hand's cards dead; the heads-up approximation (folded cards ignored) is kept
// as a switch.
namespace gtosd::card_abstraction {

inline constexpr std::uint32_t three_way_class_count = 81U;
inline constexpr std::uint32_t three_way_entry_count =
    three_way_class_count * three_way_class_count * three_way_class_count;
// C(30,5): runouts of one mutually disjoint combo triple.
inline constexpr std::uint32_t three_way_runout_count = 142'506U;
// Representative of a hero class that a subset build left out.
inline constexpr std::uint16_t three_way_unbuilt_hero = 0xFFFFU;
// File name of the complete table in a resources directory. A subset build is
// never saved under this name (compared without ASCII case).
inline constexpr std::string_view three_way_table_file_name = "preflop_three_way_v1.bin";

// Whether ThreeWayTable::load accepts a table that a subset build left
// incomplete, whose unbuilt hero rows read as zero.
enum class ThreeWayLoad : std::uint8_t {
  CompleteOnly, // every one of the 81 hero classes built (the default)
  AllowPartial  // subset builds: timing probes, smokes, tests
};

// The hero's result against one opponent on one runout.
enum class Versus : std::uint8_t { Better = 0U, Tie = 1U, Worse = 2U };

struct ThreeWayEntry {
  // N(H, A, B): ordered pairs (a, b) mutually disjoint with h.
  std::uint32_t pairs{0U};
  // cells[3 * versus_first + versus_second], summed over pairs and runouts.
  std::array<std::uint32_t, 9> cells{};

  [[nodiscard]] constexpr std::uint32_t cell(const Versus first,
                                             const Versus second) const noexcept {
    return cells[3U * static_cast<std::uint32_t>(first) + static_cast<std::uint32_t>(second)];
  }
  // Sum over the second opponent: the hero's result against the first one.
  [[nodiscard]] constexpr std::uint64_t against_first(const Versus first) const noexcept {
    const auto row = 3U * static_cast<std::uint32_t>(first);
    return static_cast<std::uint64_t>(cells[row]) + cells[row + 1U] + cells[row + 2U];
  }
  [[nodiscard]] constexpr std::uint64_t total() const noexcept {
    std::uint64_t sum = 0U;
    for (const auto value : cells) {
      sum += value;
    }
    return sum;
  }
  friend constexpr bool operator==(const ThreeWayEntry &, const ThreeWayEntry &) = default;
};

// Hero-centric masses of a 3-active showdown, in runout-weighted pairs: each
// field is the cell count divided by the runouts, so the five sum to N.
struct ThreeWayMass {
  double hero_alone{0.0};  // winners {hero}
  double with_first{0.0};  // winners {hero, first}
  double with_second{0.0}; // winners {hero, second}
  double all_three{0.0};   // winners {hero, first, second}
  double hero_loses{0.0};  // hero not among the winners
};

// 2-way showdown after a fold, summed over the disjoint (opponent, folded)
// pairs in runout-weighted pairs: wins + ties + losses = N(H, O, F).
struct TwoWayMass {
  double wins{0.0};
  double ties{0.0};
  double losses{0.0};
};

// Who owns the folded hand's cards at a 2-way showdown after a fold.
enum class FoldedCards : std::uint8_t {
  Dead,   // removed from the deck: exact, read from the 3-way entry (default)
  Ignored // heads-up table, the folded hand only blocks the deal
};

// Outcome counts of one combo deal over its 142,506 runouts. Seats: 0 the
// hero, 1 the first opponent, 2 the second one.
struct ComboTripleCounts {
  // by_winners[mask - 1] for the winner masks 1..7.
  std::array<std::uint32_t, 7> by_winners{};
  // Hero-centric cells in the layout of ThreeWayEntry::cells.
  std::array<std::uint32_t, 9> cells{};
  friend constexpr bool operator==(const ComboTripleCounts &, const ComboTripleCounts &) = default;
};

struct ThreeWayBuildOptions {
  unsigned threads{1U};
  // Enumerate one board per orbit of the suit permutations that fix the hero
  // combo and weight it by the orbit size: identical output, about 3x less work.
  bool board_symmetry{true};
  // Hero classes to build; empty builds all 81.
  std::vector<std::uint8_t> hero_classes;
};

// Per-hero build cost, summed over the worker tasks of that hero.
struct ThreeWayBuildTiming {
  std::array<double, three_way_class_count> hero_task_seconds{};
  std::array<std::uint64_t, three_way_class_count> hero_boards{};
  double wall_seconds{0.0};
};

class ThreeWayTable {
public:
  static constexpr std::uint32_t format_version = 1U;

  [[nodiscard]] static Result<ThreeWayTable, ResourceError>
  build(const RankTable &ranks, const ThreeWayBuildOptions &options,
        ThreeWayBuildTiming *timing = nullptr);
  // The 81 x 81 entries (A-major) of any hero combo, not only a representative.
  [[nodiscard]] static Result<std::vector<ThreeWayEntry>, ResourceError>
  build_hero_rows(const RankTable &ranks, std::uint16_t hero_combo, unsigned threads,
                  bool board_symmetry);
  // An incomplete table fails with InvalidInput unless `partial` is
  // AllowPartial.
  [[nodiscard]] static Result<ThreeWayTable, ResourceError>
  load(const std::filesystem::path &path, ThreeWayLoad partial = ThreeWayLoad::CompleteOnly);
  // An incomplete table under three_way_table_file_name fails with
  // InvalidInput and writes nothing.
  [[nodiscard]] Result<bool, ResourceError> save(const std::filesystem::path &path) const;
  // Whether the file name of `path` is three_way_table_file_name.
  [[nodiscard]] static bool canonical_file_name(const std::filesystem::path &path);

  // Lowest combo id of a class.
  [[nodiscard]] static std::uint16_t representative_of(std::uint8_t hand_class) noexcept;
  [[nodiscard]] static constexpr std::size_t entry_index(const std::uint8_t hero,
                                                         const std::uint8_t first,
                                                         const std::uint8_t second) noexcept {
    return (static_cast<std::size_t>(hero) * three_way_class_count + first) *
               three_way_class_count +
           second;
  }

  [[nodiscard]] const ThreeWayEntry &entry(std::uint8_t hero, std::uint8_t first,
                                           std::uint8_t second) const noexcept {
    return entries_[entry_index(hero, first, second)];
  }
  [[nodiscard]] ThreeWayMass three_way_mass(std::uint8_t hero, std::uint8_t first,
                                            std::uint8_t second) const noexcept;
  // Folded cards dead: entry (hero, opponent, folded), rows summed.
  [[nodiscard]] TwoWayMass two_way_mass(std::uint8_t hero, std::uint8_t opponent,
                                        std::uint8_t folded) const noexcept;

  [[nodiscard]] bool hero_built(const std::uint8_t hero) const noexcept {
    return representatives_[hero] != three_way_unbuilt_hero;
  }
  [[nodiscard]] bool complete() const noexcept;
  [[nodiscard]] const std::array<std::uint16_t, three_way_class_count> &
  representatives() const noexcept {
    return representatives_;
  }
  [[nodiscard]] const std::vector<ThreeWayEntry> &entries() const noexcept { return entries_; }
  [[nodiscard]] const std::string &fingerprint() const noexcept { return fingerprint_; }
  [[nodiscard]] static constexpr std::uint64_t payload_bytes() noexcept {
    return sizeof(std::uint64_t) + three_way_class_count * sizeof(std::uint16_t) +
           std::uint64_t{three_way_entry_count} * 10U * sizeof(std::uint32_t);
  }

  friend bool operator==(const ThreeWayTable &left, const ThreeWayTable &right) {
    return left.representatives_ == right.representatives_ && left.entries_ == right.entries_ &&
           left.fingerprint_ == right.fingerprint_;
  }

private:
  [[nodiscard]] std::vector<std::uint8_t> payload() const;
  void finalize(const std::string &rank_fingerprint);

  std::array<std::uint16_t, three_way_class_count> representatives_{};
  std::vector<ThreeWayEntry> entries_;
  std::string fingerprint_;
};

// Heads-up approximation of a 2-way showdown after a fold: the folded hand
// blocks the deal (the pair weights are those of N) but its cards return to
// the deck for the runout.
[[nodiscard]] TwoWayMass two_way_mass_folded_ignored(const AllInTable &heads_up, std::uint8_t hero,
                                                     std::uint8_t opponent,
                                                     std::uint8_t folded) noexcept;
[[nodiscard]] TwoWayMass two_way_mass(const ThreeWayTable &table, const AllInTable &heads_up,
                                      FoldedCards folded_cards, std::uint8_t hero,
                                      std::uint8_t opponent, std::uint8_t folded) noexcept;

// N for any hero combo: ordered (a in first, b in second) disjoint with it.
[[nodiscard]] std::uint32_t three_way_pair_count(std::uint16_t hero_combo, std::uint8_t first,
                                                 std::uint8_t second) noexcept;

// Reference counter for one combo deal through the rank table, one runout at a
// time (no class reduction, no board symmetry). Combos must be disjoint.
[[nodiscard]] Result<ComboTripleCounts, ResourceError> count_combo_triple(const RankTable &ranks,
                                                                          std::uint16_t hero,
                                                                          std::uint16_t first,
                                                                          std::uint16_t second);

// Independent brute force for one entry of any hero combo: every pair and every
// runout through gtosd::evaluate_showdown (the exact evaluator, not the rank
// table).
[[nodiscard]] Result<ThreeWayEntry, ResourceError>
evaluate_entry_by_showdown(std::uint16_t hero_combo, std::uint8_t first, std::uint8_t second,
                           unsigned threads);

// Exact integer identities of the table (phase 2a gate, V1-V6). Checks that
// involve a hero class left out of a subset build are skipped.
struct ThreeWayIdentityReport {
  static constexpr std::size_t identity_count = 6U;
  // V1 cell total, V2 pair counts, V3 transpose, V4 hero/first swap, V5 pot
  // shares, V6 heads-up consistency (needs the heads-up table).
  std::array<std::uint64_t, identity_count> checks{};
  std::array<std::uint64_t, identity_count> failures{};
  // V6 diagnostic: largest |equity(folded cards dead) - equity(folded cards
  // ignored)| over the (H, A, F) with N > 0, and where it occurs.
  double max_folded_equity_shift{0.0};
  std::array<std::uint8_t, 3> max_folded_shift_classes{};

  [[nodiscard]] bool passed() const noexcept {
    for (const auto failure : failures) {
      if (failure != 0U) {
        return false;
      }
    }
    return true;
  }
};

[[nodiscard]] ThreeWayIdentityReport check_three_way_identities(const ThreeWayTable &table,
                                                                const AllInTable *heads_up);

} // namespace gtosd::card_abstraction
