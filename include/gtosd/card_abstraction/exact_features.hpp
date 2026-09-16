#pragma once

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

// Exact private-observation features per street, keyed by canonical board and
// by combo id expressed in the canonical frame of that board.
//
// Flop and turn observations receive the histogram of the exact river equity
// (against every disjoint opponent hand) over all runouts. River observations
// receive the exact equity against every disjoint opponent and against each
// of eight groups of opponent preflop strength (OCHS). The features depend
// only on cards visible to the player; combos overlapping the board hold
// zeros and are never read.
namespace gtosd::card_abstraction {

inline constexpr std::uint32_t equity_histogram_bins = 16U;
inline constexpr std::uint32_t flop_runout_count = 465U;
inline constexpr std::uint32_t turn_runout_count = 30U;
inline constexpr std::uint32_t opponent_group_count = 8U;
inline constexpr std::uint32_t river_feature_count = 1U + opponent_group_count;
inline constexpr std::uint16_t equity_fixed_point_scale = 65'535U;

// Eight groups of the 81 preflop classes ordered by exact all-in equity against
// a random hand, with masses as equal as the class masses allow.
struct OpponentGroups {
  static constexpr std::uint32_t format_version = 1U;

  std::array<std::uint8_t, 81> group_of_class{};
  std::array<double, 81> class_equity{};
  std::array<std::uint16_t, opponent_group_count> group_mass{};
  std::string fingerprint;

  [[nodiscard]] static OpponentGroups build(const AllInTable &all_in);
  // Groups from an explicit strength ranking of the 81 classes (strongest
  // first) and their equities; `build` uses this with the all-in table, tests
  // may supply a synthetic ranking.
  [[nodiscard]] static OpponentGroups from_ranking(const std::array<std::uint8_t, 81> &ranking,
                                                   const std::array<double, 81> &class_equity,
                                                   const std::string &source_fingerprint);
  [[nodiscard]] static Result<OpponentGroups, ResourceError>
  load(const std::filesystem::path &path);
  [[nodiscard]] Result<bool, ResourceError> save(const std::filesystem::path &path) const;
};

class FlopFeatureTable {
public:
  static constexpr std::uint32_t format_version = 1U;

  [[nodiscard]] static Result<FlopFeatureTable, ResourceError>
  build(const BoardCatalog &catalog, const RankTable &ranks, unsigned threads);
  [[nodiscard]] static Result<FlopFeatureTable, ResourceError>
  load(const std::filesystem::path &path);
  [[nodiscard]] Result<bool, ResourceError> save(const std::filesystem::path &path) const;

  // Histogram counts (sum 465) for a live combo in the flop's canonical frame.
  [[nodiscard]] std::span<const std::uint16_t, equity_histogram_bins>
  histogram(std::uint32_t flop_index, std::uint16_t combo) const noexcept {
    return std::span<const std::uint16_t, equity_histogram_bins>(
        counts_.data() + (static_cast<std::size_t>(flop_index) * combo_count + combo) *
                             equity_histogram_bins,
        equity_histogram_bins);
  }
  [[nodiscard]] const std::string &fingerprint() const noexcept { return fingerprint_; }
  [[nodiscard]] const std::string &catalog_fingerprint() const noexcept {
    return catalog_fingerprint_;
  }
  [[nodiscard]] std::uint64_t payload_bytes() const noexcept {
    return counts_.size() * sizeof(std::uint16_t);
  }
  [[nodiscard]] const std::vector<std::uint16_t> &counts() const noexcept { return counts_; }

private:
  std::vector<std::uint16_t> counts_;
  std::string catalog_fingerprint_;
  std::string rank_fingerprint_;
  std::string fingerprint_;
};

class TurnFeatureTable {
public:
  static constexpr std::uint32_t format_version = 1U;

  [[nodiscard]] static Result<TurnFeatureTable, ResourceError>
  build(const BoardCatalog &catalog, const RankTable &ranks, unsigned threads);
  [[nodiscard]] static Result<TurnFeatureTable, ResourceError>
  load(const std::filesystem::path &path);
  [[nodiscard]] Result<bool, ResourceError> save(const std::filesystem::path &path) const;

  // Histogram counts (sum 30) for a live combo in the flop+turn canonical frame.
  [[nodiscard]] std::span<const std::uint8_t, equity_histogram_bins>
  histogram(std::uint32_t flop_turn_index, std::uint16_t combo) const noexcept {
    return std::span<const std::uint8_t, equity_histogram_bins>(
        counts_.data() + (static_cast<std::size_t>(flop_turn_index) * combo_count + combo) *
                             equity_histogram_bins,
        equity_histogram_bins);
  }
  [[nodiscard]] const std::string &fingerprint() const noexcept { return fingerprint_; }
  [[nodiscard]] std::uint64_t payload_bytes() const noexcept { return counts_.size(); }
  [[nodiscard]] const std::vector<std::uint8_t> &counts() const noexcept { return counts_; }

private:
  std::vector<std::uint8_t> counts_;
  std::string catalog_fingerprint_;
  std::string rank_fingerprint_;
  std::string fingerprint_;
};

class RiverFeatureTable {
public:
  static constexpr std::uint32_t format_version = 1U;

  [[nodiscard]] static Result<RiverFeatureTable, ResourceError>
  build(const BoardCatalog &catalog, const RankTable &ranks, const OpponentGroups &groups,
        unsigned threads);
  [[nodiscard]] static Result<RiverFeatureTable, ResourceError>
  load(const std::filesystem::path &path);
  [[nodiscard]] Result<bool, ResourceError> save(const std::filesystem::path &path) const;

  // Fixed-point equities: [0] against every disjoint opponent, [1..8] against
  // each opponent group (0.5 when no disjoint opponent belongs to the group).
  [[nodiscard]] std::span<const std::uint16_t, river_feature_count>
  features(std::uint32_t river_board_index, std::uint16_t combo) const noexcept {
    return std::span<const std::uint16_t, river_feature_count>(
        values_.data() + (static_cast<std::size_t>(river_board_index) * combo_count + combo) *
                             river_feature_count,
        river_feature_count);
  }
  [[nodiscard]] const std::string &fingerprint() const noexcept { return fingerprint_; }
  [[nodiscard]] const std::string &groups_fingerprint() const noexcept {
    return groups_fingerprint_;
  }
  [[nodiscard]] std::uint64_t payload_bytes() const noexcept {
    return values_.size() * sizeof(std::uint16_t);
  }
  [[nodiscard]] const std::vector<std::uint16_t> &values() const noexcept { return values_; }

private:
  std::vector<std::uint16_t> values_;
  std::string catalog_fingerprint_;
  std::string rank_fingerprint_;
  std::string groups_fingerprint_;
  std::string fingerprint_;
};

// Bin of an equity in [0, 1]: floor(equity * bins), with 1.0 in the last bin.
[[nodiscard]] constexpr std::uint32_t equity_bin(const double equity) noexcept {
  const auto bin = static_cast<std::uint32_t>(equity * equity_histogram_bins);
  return bin >= equity_histogram_bins ? equity_histogram_bins - 1U : bin;
}

[[nodiscard]] constexpr std::uint16_t equity_fixed_point(const double equity) noexcept {
  return static_cast<std::uint16_t>(equity * equity_fixed_point_scale + 0.5);
}

} // namespace gtosd::card_abstraction
