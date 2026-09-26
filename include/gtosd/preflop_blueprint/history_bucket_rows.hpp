#pragma once

#include "gtosd/preflop_blueprint/board_context.hpp"

#include <filesystem>
#include <limits>

namespace gtosd::preflop_blueprint {

inline constexpr std::uint32_t no_history_row = std::numeric_limits<std::uint32_t>::max();

// Position of one turn history among the river rows of a map, made by
// HistoryBucketRows::river_cursor and valid only for that map.
struct RiverRowCursor {
  // Key of river bucket 0 below the turn history.
  std::uint64_t first_key{0};
  // history-v1: index range of the keys of the turn history in the sorted
  // key list (the river keys of one parent are contiguous).
  std::uint32_t begin{0};
  std::uint32_t end{0};
  bool valid{false};
};

struct HistoryObservation {
  std::uint64_t key{0}; // ((class * F + flop) * T + turn) * R + river
  std::uint64_t weight{0};
};

struct HistoryClusteringReport {
  std::uint64_t support{0};
  std::uint64_t weight{0};
  // Legacy field names from the river-only HR1 builder. In the turn report
  // of HR2 these contain the street-native CDF-L1 distance, as identified by
  // the report schema; river continues to use squared L2.
  double weighted_squared_distance{0};
  double maximum_squared_distance{0};
  std::uint32_t maximum_iterations{0};
};

struct HistoryHierarchyReport {
  HistoryClusteringReport turn;
  HistoryClusteringReport river;
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

  // Build a frozen two-level hierarchy from an exact transition census. Turn
  // observations may merge only below the same flop-history parent; river
  // observations may merge only below the same compressed turn-history row.
  // Lookup never uses a future-street observation.
  [[nodiscard]] static Result<HistoryBucketRows, KernelError>
  build_hierarchy(const card_abstraction::BucketTable &flop,
                  const card_abstraction::BucketTable &turn,
                  const card_abstraction::BucketTable &river,
                  std::vector<HistoryObservation> observations,
                  std::uint32_t maximum_turn_children,
                  std::uint32_t maximum_river_children,
                  HistoryHierarchyReport *report = nullptr);

  [[nodiscard]] std::uint32_t row(Street street, std::uint8_t hand_class, std::uint16_t flop,
                                  std::uint16_t turn, std::uint16_t river) const noexcept;
  // Unique row of the immediately preceding street. History rows preserve
  // this parent by construction; Preflop and invalid rows return
  // no_history_row. These links are derived from the persisted keys and do
  // not change the map format or fingerprint.
  [[nodiscard]] std::uint32_t parent_row(Street street, std::uint32_t row) const noexcept;
  // River rows of one turn history for callers that visit every river of a
  // turn (joint river engine): river_row(river_cursor(c, f, t), r) equals
  // row(Street::River, c, f, t, r) for every input, but reads one dense
  // entry (v2) or searches only the keys of the turn history (v1) instead of
  // the whole map.
  [[nodiscard]] RiverRowCursor river_cursor(std::uint8_t hand_class, std::uint16_t flop,
                                            std::uint16_t turn) const noexcept;
  [[nodiscard]] std::uint32_t river_row(const RiverRowCursor &cursor,
                                        std::uint16_t river) const noexcept;
  [[nodiscard]] std::uint32_t count(card_abstraction::BucketStreet street) const noexcept {
    return counts_[static_cast<std::size_t>(street)];
  }
  [[nodiscard]] bool matches(const card_abstraction::BucketTable &table) const noexcept;
  [[nodiscard]] const char *format_name() const noexcept {
    return format_version_ == 1 ? "history-v1" : "history-hierarchy-v2";
  }
  [[nodiscard]] const std::string &fingerprint() const noexcept { return fingerprint_; }
  [[nodiscard]] std::uint64_t byte_size() const noexcept;
  [[nodiscard]] std::uint64_t resident_byte_size() const noexcept {
    return byte_size() + 4ULL * (turn_lookup_.size() + river_lookup_.size());
  }
  [[nodiscard]] Result<bool, KernelError> save(const std::filesystem::path &path) const;
  [[nodiscard]] static Result<HistoryBucketRows, KernelError>
  load(const std::filesystem::path &path);

private:
  [[nodiscard]] std::string payload() const;
  [[nodiscard]] bool build_parent_rows() noexcept;
  [[nodiscard]] bool build_lookup_rows();
  std::array<std::uint32_t, 3> capacities_{};
  std::array<std::uint32_t, 3> counts_{};
  std::uint32_t format_version_{1};
  std::array<std::string, 3> tables_;
  std::vector<std::uint32_t> flop_rows_;
  std::vector<std::uint64_t> turn_keys_;
  std::vector<std::uint32_t> turn_rows_;
  std::vector<std::uint64_t> river_keys_;
  std::vector<std::uint32_t> river_rows_;
  std::vector<std::uint32_t> turn_lookup_;
  std::vector<std::uint32_t> river_lookup_;
  std::array<std::vector<std::uint32_t>, 3> parent_rows_;
  std::string fingerprint_;
};

} // namespace gtosd::preflop_blueprint
