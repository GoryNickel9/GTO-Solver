#pragma once

#include "gtosd/card_abstraction/rank_table.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Exact preflop all-in outcomes for every unordered pair of disjoint hole-card
// combos over the C(32,5) = 201,376 runouts. Pairs are indexed triangularly
// over the 630 combos; entries of overlapping pairs stay zero.
namespace gtosd::card_abstraction {

struct PairOutcome {
  std::uint32_t wins{0U};
  std::uint32_t ties{0U};
  std::uint32_t losses{0U};

  [[nodiscard]] constexpr std::uint32_t total() const noexcept { return wins + ties + losses; }
  [[nodiscard]] constexpr double equity() const noexcept {
    const auto count = total();
    return count == 0U ? 0.5 : (static_cast<double>(wins) + 0.5 * ties) / count;
  }
  friend constexpr bool operator==(const PairOutcome &, const PairOutcome &) = default;
};

inline constexpr std::uint32_t combo_count = 630U;
inline constexpr std::uint32_t combo_pair_count = combo_count * (combo_count - 1U) / 2U;
// Unordered pairs of disjoint combos: C(36,2) * C(34,2) / 2.
inline constexpr std::uint32_t disjoint_combo_pair_count = 176'715U;
inline constexpr std::uint32_t all_in_runout_count = 201'376U;

// Triangular index of the unordered pair {a, b}, a != b.
[[nodiscard]] constexpr std::uint32_t combo_pair_index(std::uint16_t a, std::uint16_t b) noexcept {
  if (a > b) {
    const auto swap = a;
    a = b;
    b = swap;
  }
  return static_cast<std::uint32_t>(a) * (2U * combo_count - static_cast<std::uint32_t>(a) - 1U) /
             2U +
         static_cast<std::uint32_t>(b - a - 1U);
}

class AllInTable {
public:
  static constexpr std::uint32_t format_version = 1U;

  [[nodiscard]] static Result<AllInTable, ResourceError> build(const RankTable &ranks,
                                                               unsigned threads);
  [[nodiscard]] static Result<AllInTable, ResourceError> load(const std::filesystem::path &path);
  [[nodiscard]] Result<bool, ResourceError> save(const std::filesystem::path &path) const;

  // Outcome for `hero` against `opponent`; both are combo ids.
  [[nodiscard]] PairOutcome outcome(std::uint16_t hero, std::uint16_t opponent) const noexcept;
  [[nodiscard]] const std::vector<PairOutcome> &entries() const noexcept { return entries_; }
  [[nodiscard]] const std::string &fingerprint() const noexcept { return fingerprint_; }
  [[nodiscard]] std::uint64_t payload_bytes() const noexcept {
    return entries_.size() * sizeof(PairOutcome);
  }

  friend bool operator==(const AllInTable &left, const AllInTable &right) {
    return left.entries_ == right.entries_ && left.fingerprint_ == right.fingerprint_;
  }

private:
  void finalize(const std::string &rank_fingerprint);

  // Stored for the lower combo id of each pair.
  std::vector<PairOutcome> entries_;
  std::string fingerprint_;
};

} // namespace gtosd::card_abstraction
