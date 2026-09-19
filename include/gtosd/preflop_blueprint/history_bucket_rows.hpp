#pragma once

#include "gtosd/preflop_blueprint/board_context.hpp"

#include <filesystem>
#include <limits>

namespace gtosd::preflop_blueprint {

inline constexpr std::uint32_t no_history_row = std::numeric_limits<std::uint32_t>::max();

struct HistoryObservation {
  std::uint64_t key{0}; // ((class * F + flop) * T + turn) * R + river
  std::uint64_t weight{0};
};

struct HistoryClusteringReport {
  std::uint64_t support{0};
  std::uint64_t weight{0};
  double weighted_squared_distance{0};
  double maximum_squared_distance{0};
  std::uint32_t maximum_iterations{0};
};

// Preserve class and every original flop/turn bucket. Only river observations
// with the same complete turn-history parent may be clustered together.
// This preserves recall of abstract observations; it does not bound physical
// exploitability or restore distinctions already lost in the base buckets.
class HistoryBucketRows {
public:
  [[nodiscard]] static Result<HistoryBucketRows, KernelError>
  build(const card_abstraction::BucketTable &flop, const card_abstraction::BucketTable &turn,
        const card_abstraction::BucketTable &river, std::vector<HistoryObservation> observations,
        std::uint32_t maximum_children, HistoryClusteringReport *report = nullptr);

  [[nodiscard]] std::uint32_t row(Street street, std::uint8_t hand_class, std::uint16_t flop,
                                  std::uint16_t turn, std::uint16_t river) const noexcept;
  [[nodiscard]] std::uint32_t count(card_abstraction::BucketStreet street) const noexcept {
    return counts_[static_cast<std::size_t>(street)];
  }
  [[nodiscard]] bool matches(const card_abstraction::BucketTable &table) const noexcept;
  [[nodiscard]] const std::string &fingerprint() const noexcept { return fingerprint_; }
  [[nodiscard]] std::uint64_t byte_size() const noexcept;
  [[nodiscard]] Result<bool, KernelError> save(const std::filesystem::path &path) const;
  [[nodiscard]] static Result<HistoryBucketRows, KernelError>
  load(const std::filesystem::path &path);

private:
  [[nodiscard]] std::string payload() const;
  std::array<std::uint32_t, 3> capacities_{};
  std::array<std::uint32_t, 3> counts_{};
  std::array<std::string, 3> tables_;
  std::vector<std::uint32_t> flop_rows_;
  std::vector<std::uint64_t> turn_keys_;
  std::vector<std::uint64_t> river_keys_;
  std::vector<std::uint32_t> river_rows_;
  std::string fingerprint_;
};

} // namespace gtosd::preflop_blueprint
