#pragma once

#include "gtosd/equity/evaluator.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace gtosd {

enum class SevenCardTableError : std::uint8_t {
  InvalidCards,
  MemoryFailure,
  IoFailure,
  IntegrityFailure,
  UnsupportedVersion
};

constexpr std::uint64_t seven_card_table_entry_count = 8'347'680U;
constexpr std::string_view seven_card_table_ruleset_fingerprint =
    "short_deck_36_flush_over_full_house_a6789_exact_v1";

[[nodiscard]] Result<std::uint64_t, SevenCardTableError>
seven_card_combination_index(const std::array<CardId, 7> &cards);

class SevenCardLookupTable final : public IHandEvaluator {
public:
  SevenCardLookupTable() = default;
  [[nodiscard]] static Result<SevenCardLookupTable, SevenCardTableError> build();
  [[nodiscard]] static Result<SevenCardLookupTable, SevenCardTableError>
  load(const std::filesystem::path &path);

  [[nodiscard]] Result<bool, SevenCardTableError> save(const std::filesystem::path &path) const;

  [[nodiscard]] Result<HandValue, EquityError>
  evaluate_seven(const std::array<CardId, 7> &cards) const override;
  [[nodiscard]] Result<std::vector<HandValue>, EquityError>
  evaluate_batch(std::span<const std::array<CardId, 7>> hands) const override;

  [[nodiscard]] std::uint64_t entry_count() const noexcept { return encoded_.size(); }
  [[nodiscard]] std::uint64_t payload_bytes() const noexcept {
    return encoded_.size() * sizeof(std::uint32_t);
  }
  [[nodiscard]] std::uint64_t checksum() const noexcept { return checksum_; }
  [[nodiscard]] std::string_view ruleset_fingerprint() const noexcept {
    return seven_card_table_ruleset_fingerprint;
  }

private:
  explicit SevenCardLookupTable(std::vector<std::uint32_t> encoded, std::uint64_t checksum)
      : encoded_(std::move(encoded)), checksum_(checksum) {}

  std::vector<std::uint32_t> encoded_;
  std::uint64_t checksum_{0U};
};

[[nodiscard]] const char *seven_card_table_error_name(SevenCardTableError error) noexcept;

} // namespace gtosd
