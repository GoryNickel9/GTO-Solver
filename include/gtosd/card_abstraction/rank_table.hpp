#pragma once

#include "gtosd/core/cards.hpp"
#include "gtosd/core/result.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Ordinal hand ranks for every five- and seven-card set of the Short Deck.
//
// The ranks are derived from the existing exact five-card evaluator: every
// five-card set receives the ordinal position of its HandValue among the
// distinct values, and every seven-card set receives the maximum ordinal of
// its 21 five-card subsets. Comparing two ordinals is equivalent to comparing
// the HandValues returned by gtosd::evaluate_seven; the table is a derived
// resource, not a second evaluator. Sets are indexed by the colex rank of
// their sorted card indices, the same index used by SevenCardLookupTable.
namespace gtosd::card_abstraction {

enum class ResourceError : std::uint8_t {
  InvalidInput,
  IoFailure,
  IntegrityFailure,
  UnsupportedVersion,
  MemoryFailure
};

inline constexpr std::uint64_t five_card_set_count = 376'992U;
inline constexpr std::uint64_t seven_card_set_count = 8'347'680U;

class RankTable {
public:
  static constexpr std::uint32_t format_version = 1U;

  [[nodiscard]] static Result<RankTable, ResourceError> build();
  [[nodiscard]] static Result<RankTable, ResourceError> load(const std::filesystem::path &path);
  [[nodiscard]] Result<bool, ResourceError> save(const std::filesystem::path &path) const;

  // Sorted, distinct card indices.
  [[nodiscard]] std::uint16_t rank_of_sorted(const std::array<std::uint8_t, 7> &sorted) const;
  // Any order; sorts internally. Undefined for duplicate cards.
  [[nodiscard]] std::uint16_t rank_of(const std::array<std::uint8_t, 2> &hand,
                                      const std::array<std::uint8_t, 5> &board) const;
  [[nodiscard]] std::uint16_t rank_at(const std::uint64_t colex_index) const noexcept {
    return seven_[colex_index];
  }
  [[nodiscard]] std::uint16_t five_card_rank_at(const std::uint64_t colex_index) const noexcept {
    return five_[colex_index];
  }

  [[nodiscard]] std::uint16_t distinct_ranks() const noexcept { return distinct_; }
  [[nodiscard]] std::uint64_t checksum() const noexcept { return checksum_; }
  [[nodiscard]] const std::string &fingerprint() const noexcept { return fingerprint_; }
  [[nodiscard]] std::uint64_t payload_bytes() const noexcept {
    return (seven_.size() + five_.size()) * sizeof(std::uint16_t);
  }

  friend bool operator==(const RankTable &left, const RankTable &right) {
    return left.seven_ == right.seven_ && left.five_ == right.five_ &&
           left.distinct_ == right.distinct_ && left.checksum_ == right.checksum_;
  }

private:
  std::vector<std::uint16_t> seven_;
  std::vector<std::uint16_t> five_;
  std::uint16_t distinct_{0U};
  std::uint64_t checksum_{0U};
  std::string fingerprint_;
};

[[nodiscard]] const char *resource_error_name(ResourceError error) noexcept;

} // namespace gtosd::card_abstraction
