#pragma once

#include "gtosd/core/ranges.hpp"
#include "gtosd/core/result.hpp"
#include "gtosd/tree/tree.hpp"

#include <array>
#include <cstdint>
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
  bool meets_engineering_target{false};
  bool meets_absolute_gate{false};
};

struct CanonicalLayoutOptions {
  std::vector<std::uint32_t> worker_threads{1U, 2U, 4U, 8U};
  std::vector<std::uint8_t> state_bytes_per_action{3U, 4U};
  std::uint64_t engineering_target_bytes{1'800'000'000U};
  std::uint64_t absolute_gate_bytes{2'000'000'000U};
  std::uint64_t certification_bytes{120'000'000U};
  std::uint64_t runtime_bytes{120'000'000U};
  std::uint64_t reserve_bytes{100'000'000U};
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
};

// Counts a lossless orbit-representative chance tree without materializing the
// physical public tree or allocating solver state.  Ranges are checked
// independently for every retained suit automorphism; they need not be equal.
[[nodiscard]] Result<CanonicalLayoutReport, CanonicalLayoutError>
estimate_canonical_chance_layout(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                                 const CanonicalLayoutOptions &options = {});

[[nodiscard]] const char *canonical_layout_error_name(CanonicalLayoutError error) noexcept;

} // namespace gtosd
