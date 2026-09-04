#pragma once

#include "gtosd/core/ranges.hpp"
#include "gtosd/core/result.hpp"
#include "gtosd/tree/tree.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace gtosd {

enum class CanonicalLayoutError : std::uint8_t {
  InvalidConfiguration,
  TreeFailure,
  ArithmeticOverflow,
  NonIntegralShape
};

struct CanonicalStreetLayout {
  std::uint64_t physical_board_paths{0};
  std::uint64_t canonical_board_paths{0};
  std::uint64_t canonical_public_nodes{0};
  std::uint64_t canonical_decision_nodes{0};
  std::uint64_t canonical_chance_nodes{0};
  std::uint64_t canonical_terminal_nodes{0};
  std::uint64_t canonical_action_edges{0};
  std::uint64_t canonical_chance_edges{0};
  std::uint64_t information_sets{0};
  std::uint64_t action_entries{0};
};

struct CanonicalMemoryEstimate {
  std::uint8_t state_bytes_per_action{0};
  std::uint32_t worker_threads{0};
  std::uint64_t solver_state_bytes{0};
  std::uint64_t state_auxiliary_bytes{0};
  std::uint64_t topology_bytes{0};
  std::uint64_t board_mapping_bytes{0};
  std::uint64_t worker_scratch_bytes{0};
  std::uint64_t certification_bytes{0};
  std::uint64_t runtime_bytes{0};
  std::uint64_t reserve_bytes{0};
  std::uint64_t estimated_peak_bytes{0};
  std::optional<bool> meets_requested_budget;
};

enum class CanonicalLayoutBudgetSource : std::uint8_t { UserConfigured, Experiment };

struct CanonicalLayoutBudget {
  std::uint64_t bytes{0};
  CanonicalLayoutBudgetSource source{CanonicalLayoutBudgetSource::UserConfigured};
};

struct CanonicalLayoutOptions {
  std::vector<std::uint32_t> worker_threads{1U, 2U, 4U, 8U};
  std::vector<std::uint8_t> state_bytes_per_action{3U, 4U};
  std::optional<CanonicalLayoutBudget> requested_budget;
  std::optional<CanonicalLayoutBudget> requested_feature_cache_disk_budget;
  // Empty keeps the exact-only report unchanged.  Non-zero values request a
  // conservative production preflight for native Float64 CFR+ bucketing.
  std::vector<std::uint32_t> card_abstraction_buckets;
  std::uint64_t certification_bytes{120'000'000U};
  std::uint64_t runtime_bytes{120'000'000U};
  std::uint64_t reserve_bytes{100'000'000U};
};

struct CanonicalCardAbstractionCandidateEstimate {
  std::uint32_t buckets_per_partition{0};
  std::uint64_t abstract_information_sets_upper_bound{0};
  std::uint64_t abstract_action_entries_upper_bound{0};
  std::uint64_t bucket_mapping_bytes_upper_bound{0};
  std::uint64_t solver_state_bytes_upper_bound{0};
  std::uint64_t preparation_transient_bytes_estimate{0};
  std::uint64_t estimated_in_ram_peak_bytes{0};
  std::uint64_t estimated_out_of_core_peak_bytes{0};
  std::uint64_t out_of_core_backing_store_bytes{0};
  std::uint64_t estimated_disk_bytes_with_cache{0};
  std::optional<bool> in_ram_meets_requested_budget;
  std::optional<bool> out_of_core_meets_requested_budgets;
};

struct CanonicalCardAbstractionPreflight {
  std::uint32_t feature_cache_worker_count{0};
  std::uint32_t solver_thread_count{0};
  std::uint64_t partition_count{0};
  std::uint64_t observation_count{0};
  std::uint64_t maximum_partition_observations{0};
  std::uint64_t feature_dimensions{4};
  std::uint64_t feature_cache_logical_bytes{0};
  std::uint64_t feature_cache_serialized_bytes_upper_bound{0};
  std::uint64_t feature_cache_atomic_write_bytes_upper_bound{0};
  std::uint64_t feature_cache_build_peak_bytes_estimate{0};
  bool feature_cache_format_limit_ok{false};
  std::optional<bool> feature_cache_atomic_write_meets_requested_budget;
  std::vector<CanonicalCardAbstractionCandidateEstimate> candidates;
};

struct CanonicalLayoutReport {
  PublicTreeStats physical_public_tree{};
  std::array<CanonicalStreetLayout, 3> streets{};
  std::uint64_t preserving_suit_automorphisms{0};
  std::uint64_t canonical_public_nodes{0};
  std::uint64_t canonical_edges{0};
  std::uint64_t canonical_decision_nodes{0};
  std::uint64_t canonical_chance_nodes{0};
  std::uint64_t canonical_terminal_nodes{0};
  std::uint64_t canonical_action_edges{0};
  std::uint64_t canonical_chance_edges{0};
  std::uint64_t information_sets{0};
  std::uint64_t action_entries{0};
  std::uint64_t maximum_live_combos{0};
  std::uint64_t maximum_actions{0};
  std::vector<CanonicalMemoryEstimate> memory_estimates;
  std::optional<CanonicalCardAbstractionPreflight> card_abstraction_preflight;
};

// Counts a lossless orbit-representative chance tree without materializing the
// physical public tree or allocating solver state.  Ranges are checked
// independently for every retained suit automorphism; they need not be equal.
[[nodiscard]] Result<CanonicalLayoutReport, CanonicalLayoutError>
estimate_canonical_chance_layout(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                                 const CanonicalLayoutOptions &options = {});

[[nodiscard]] const char *canonical_layout_error_name(CanonicalLayoutError error) noexcept;

[[nodiscard]] const char *
canonical_layout_budget_source_name(CanonicalLayoutBudgetSource source) noexcept;

} // namespace gtosd
