#include "gtosd/postflop/canonical_layout.hpp"

#include "gtosd/isomorphism/isomorphism.hpp"
#include "gtosd/postflop/postflop_solver.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <iterator>
#include <limits>
#include <utility>

namespace gtosd {
namespace {

constexpr std::uint64_t compact_node_bytes = 24U;
constexpr std::size_t canonical_combo_count = 630U;
constexpr std::uint64_t compact_action_edge_bytes = 16U;
constexpr std::uint64_t compact_chance_edge_bytes = 8U;
constexpr std::uint64_t board_header_bytes = 64U;
constexpr std::uint64_t player_local_map_bytes = 2U * canonical_combo_count * sizeof(std::int16_t);
constexpr std::uint64_t minimum_worker_arena_bytes = 4U * 1'024U * 1'024U;

bool checked_add(std::uint64_t &target, const std::uint64_t value) {
  if (target > std::numeric_limits<std::uint64_t>::max() - value) {
    return false;
  }
  target += value;
  return true;
}

bool checked_product(const std::uint64_t left, const std::uint64_t right, std::uint64_t &result) {
  if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return false;
  }
  result = left * right;
  return true;
}

bool add_product(std::uint64_t &target, const std::uint64_t left, const std::uint64_t right) {
  std::uint64_t product = 0U;
  return checked_product(left, right, product) && checked_add(target, product);
}

std::uint64_t mask_of(const std::vector<CardId> &cards) {
  std::uint64_t result = 0U;
  for (const CardId card : cards) {
    result |= card.mask();
  }
  return result;
}

std::vector<SuitPermutation> preserving_automorphisms(const PostflopTreeConfig &config,
                                                      const PostflopRanges &ranges) {
  const auto combos = all_combos();
  std::array<std::array<ComboId, 36U>, 36U> combo_id{};
  for (auto &row : combo_id) {
    row.fill(std::numeric_limits<ComboId>::max());
  }
  for (ComboId id = 0; id < canonical_combo_count; ++id) {
    const auto first = std::min(combos[id].first.value(), combos[id].second.value());
    const auto second = std::max(combos[id].first.value(), combos[id].second.value());
    combo_id[first][second] = id;
  }

  const auto initial_mask = mask_of(configured_board(config));
  std::vector<SuitPermutation> result;
  for (const auto &permutation : all_suit_permutations()) {
    const auto transformed_board = transform_card_mask(initial_mask, permutation);
    if (!transformed_board || transformed_board.value() != initial_mask) {
      continue;
    }
    bool preserves_both_ranges = true;
    for (std::size_t player = 0; player < 2U && preserves_both_ranges; ++player) {
      for (ComboId id = 0; id < canonical_combo_count; ++id) {
        const auto transformed = transform_combo(combos[id], permutation);
        if (!transformed) {
          preserves_both_ranges = false;
          break;
        }
        const auto first =
            std::min(transformed.value().first.value(), transformed.value().second.value());
        const auto second =
            std::max(transformed.value().first.value(), transformed.value().second.value());
        const ComboId mapped = combo_id[first][second];
        if (mapped == std::numeric_limits<ComboId>::max() ||
            ranges.players[player][id] != ranges.players[player][mapped]) {
          preserves_both_ranges = false;
          break;
        }
      }
    }
    if (preserves_both_ranges) {
      result.push_back(permutation);
    }
  }
  return result;
}

struct CanonicalBoard {
  std::uint64_t mask{0};
  std::vector<SuitPermutation> stabilizer;
};

std::vector<CanonicalBoard> canonical_children(const CanonicalBoard &parent) {
  std::array<bool, 36U> consumed{};
  std::vector<CanonicalBoard> result;
  for (std::uint8_t card_index = 0; card_index < 36U; ++card_index) {
    const auto card = CardId::from_index(card_index).value();
    if ((parent.mask & card.mask()) != 0U || consumed[card_index]) {
      continue;
    }
    std::uint8_t representative = card_index;
    std::array<bool, 36U> orbit{};
    for (const auto &permutation : parent.stabilizer) {
      const auto transformed = transform_card(card, permutation).value().value();
      orbit[transformed] = true;
      representative = std::min(representative, transformed);
    }
    if (representative != card_index) {
      continue;
    }
    for (std::size_t index = 0; index < orbit.size(); ++index) {
      consumed[index] = consumed[index] || orbit[index];
    }
    CanonicalBoard child;
    child.mask = parent.mask | card.mask();
    for (const auto &permutation : parent.stabilizer) {
      if (transform_card(card, permutation).value() == card) {
        child.stabilizer.push_back(permutation);
      }
    }
    result.push_back(std::move(child));
  }
  return result;
}

std::uint64_t live_combo_count(const PostflopRange &range,
                               const std::array<Combo, canonical_combo_count> &combos,
                               const std::uint64_t board_mask) {
  std::uint64_t count = 0U;
  for (ComboId combo = 0; combo < canonical_combo_count; ++combo) {
    if (range[combo].basis_points() != 0U &&
        ((combos[combo].first.mask() | combos[combo].second.mask()) & board_mask) == 0U) {
      ++count;
    }
  }
  return count;
}

std::array<std::uint64_t, 3> physical_board_paths(const PostflopTreeConfig &config) {
  const auto board_size = configured_board(config).size();
  if (board_size == 3U) {
    return {1U, 33U, 33U * 32U};
  }
  if (board_size == 4U) {
    return {0U, 1U, 32U};
  }
  return {0U, 0U, 1U};
}

} // namespace

Result<CanonicalLayoutReport, CanonicalLayoutError>
estimate_canonical_chance_layout(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                                 const CanonicalLayoutOptions &options) {
  if (!validate_postflop_ranges(config, ranges) || options.worker_threads.empty() ||
      options.state_bytes_per_action.empty() ||
      (options.requested_budget && options.requested_budget->bytes == 0U) ||
      std::ranges::any_of(
          options.worker_threads, [](const auto value) { return value == 0U; }) ||
      std::ranges::any_of(
          options.state_bytes_per_action, [](const auto value) { return value == 0U; })) {
    return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
        CanonicalLayoutError::InvalidConfiguration);
  }

  TreeBuildOptions tree_options;
  tree_options.maximum_nodes = std::numeric_limits<std::uint64_t>::max();
  const auto physical = estimate_public_tree(config, tree_options);
  if (!physical) {
    return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
        CanonicalLayoutError::TreeFailure);
  }
  const auto automorphisms = preserving_automorphisms(config, ranges);
  if (automorphisms.empty()) {
    return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
        CanonicalLayoutError::InvalidConfiguration);
  }

  std::array<std::vector<CanonicalBoard>, 3> boards;
  const auto configured = configured_board(config);
  const auto start_street = static_cast<std::size_t>(configured_starting_street(config)) -
                            static_cast<std::size_t>(Street::Flop);
  boards[start_street].push_back(CanonicalBoard{mask_of(configured), automorphisms});
  for (std::size_t street = start_street; street < 2U; ++street) {
    for (const auto &board : boards[street]) {
      auto children = canonical_children(board);
      boards[street + 1U].insert(boards[street + 1U].end(),
                                 std::make_move_iterator(children.begin()),
                                 std::make_move_iterator(children.end()));
    }
  }

  CanonicalLayoutReport report;
  report.physical_public_tree = physical.value();
  report.preserving_suit_automorphisms = automorphisms.size();
  const auto physical_paths = physical_board_paths(config);
  const auto combos = all_combos();
  std::uint64_t canonical_action_edges = 0U;
  std::uint64_t canonical_chance_edges = 0U;
  std::uint64_t total_live_combo_slots = 0U;

  for (std::size_t street = 0; street < 3U; ++street) {
    auto &street_report = report.streets[street];
    street_report.physical_board_paths = physical_paths[street];
    street_report.canonical_board_paths = boards[street].size();
    if (physical_paths[street] == 0U) {
      if (!boards[street].empty() || physical.value().node_count_by_street[street] != 0U) {
        return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
            CanonicalLayoutError::NonIntegralShape);
      }
      continue;
    }
    const auto require_divisible = [divisor = physical_paths[street]](const std::uint64_t value) {
      return value % divisor == 0U;
    };
    if (!require_divisible(physical.value().node_count_by_street[street]) ||
        !require_divisible(physical.value().decision_nodes_by_street[street]) ||
        !require_divisible(physical.value().action_edges_by_street[street]) ||
        !require_divisible(physical.value().chance_nodes_by_street[street])) {
      return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
          CanonicalLayoutError::NonIntegralShape);
    }
    const auto canonical_board_count = static_cast<std::uint64_t>(boards[street].size());
    const auto nodes_per_board =
        physical.value().node_count_by_street[street] / physical_paths[street];
    const auto decisions_per_board =
        physical.value().decision_nodes_by_street[street] / physical_paths[street];
    const auto chance_nodes_per_board =
        physical.value().chance_nodes_by_street[street] / physical_paths[street];
    const auto action_edges_per_board =
        physical.value().action_edges_by_street[street] / physical_paths[street];
    if (!checked_product(nodes_per_board, canonical_board_count,
                         street_report.canonical_public_nodes) ||
        !checked_product(decisions_per_board, canonical_board_count,
                         street_report.canonical_decision_nodes) ||
        !checked_product(chance_nodes_per_board, canonical_board_count,
                         street_report.canonical_chance_nodes) ||
        !checked_product(action_edges_per_board, canonical_board_count,
                         street_report.canonical_action_edges) ||
        !checked_add(report.canonical_public_nodes, street_report.canonical_public_nodes) ||
        !checked_add(report.canonical_decision_nodes, street_report.canonical_decision_nodes) ||
        !checked_add(report.canonical_chance_nodes,
                     street_report.canonical_chance_nodes) ||
        !checked_add(canonical_action_edges, street_report.canonical_action_edges)) {
      return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
          CanonicalLayoutError::ArithmeticOverflow);
    }
    if (street_report.canonical_decision_nodes + street_report.canonical_chance_nodes >
        street_report.canonical_public_nodes) {
      return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
          CanonicalLayoutError::NonIntegralShape);
    }
    street_report.canonical_terminal_nodes =
        street_report.canonical_public_nodes - street_report.canonical_decision_nodes -
        street_report.canonical_chance_nodes;
    if (!checked_add(report.canonical_terminal_nodes,
                     street_report.canonical_terminal_nodes)) {
      return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
          CanonicalLayoutError::ArithmeticOverflow);
    }

    for (const auto &board : boards[street]) {
      std::array<std::uint64_t, 2> live{};
      for (std::size_t player = 0; player < 2U; ++player) {
        live[player] = live_combo_count(ranges.players[player], combos, board.mask);
        report.maximum_live_combos = std::max(report.maximum_live_combos, live[player]);
        if (!checked_add(total_live_combo_slots, live[player])) {
          return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
              CanonicalLayoutError::ArithmeticOverflow);
        }
      }
      for (std::size_t player = 0; player < 2U; ++player) {
        for (std::size_t action_count = 1U;
             action_count < PublicTreeStats::decision_action_bucket_count; ++action_count) {
          const auto physical_decisions =
              physical.value().decision_nodes_by_street_player_action[street][player][action_count];
          if (!require_divisible(physical_decisions)) {
            return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
                CanonicalLayoutError::NonIntegralShape);
          }
          const auto decisions_per_board_shape = physical_decisions / physical_paths[street];
          std::uint64_t infosets = 0U;
          std::uint64_t actions = 0U;
          if (!checked_product(decisions_per_board_shape, live[player], infosets) ||
              !checked_product(infosets, action_count, actions) ||
              !checked_add(street_report.information_sets, infosets) ||
              !checked_add(street_report.action_entries, actions)) {
            return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
                CanonicalLayoutError::ArithmeticOverflow);
          }
          if (physical_decisions != 0U) {
            report.maximum_actions =
                std::max(report.maximum_actions, static_cast<std::uint64_t>(action_count));
          }
        }
      }
    }
    if (!checked_add(report.information_sets, street_report.information_sets) ||
        !checked_add(report.action_entries, street_report.action_entries)) {
      return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
          CanonicalLayoutError::ArithmeticOverflow);
    }
    if (street < 2U) {
      if (!checked_product(chance_nodes_per_board,
                           static_cast<std::uint64_t>(boards[street + 1U].size()),
                           street_report.canonical_chance_edges) ||
          !checked_add(canonical_chance_edges,
                       street_report.canonical_chance_edges)) {
        return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
            CanonicalLayoutError::ArithmeticOverflow);
      }
    }
  }
  report.canonical_edges = canonical_action_edges;
  if (!checked_add(report.canonical_edges, canonical_chance_edges)) {
    return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
        CanonicalLayoutError::ArithmeticOverflow);
  }
  report.canonical_action_edges = canonical_action_edges;
  report.canonical_chance_edges = canonical_chance_edges;

  std::uint64_t topology_bytes = 0U;
  std::uint64_t board_mapping_bytes = 0U;
  const auto board_count =
      static_cast<std::uint64_t>(boards[0].size() + boards[1].size() + boards[2].size());
  if (!add_product(topology_bytes, report.canonical_public_nodes, compact_node_bytes) ||
      !add_product(topology_bytes, canonical_action_edges, compact_action_edge_bytes) ||
      !add_product(topology_bytes, canonical_chance_edges, compact_chance_edge_bytes) ||
      !add_product(board_mapping_bytes, board_count, board_header_bytes + player_local_map_bytes) ||
      !add_product(board_mapping_bytes, total_live_combo_slots, sizeof(ComboId))) {
    return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
        CanonicalLayoutError::ArithmeticOverflow);
  }

  for (const auto state_bytes_per_action : options.state_bytes_per_action) {
    for (const auto worker_threads : options.worker_threads) {
      CanonicalMemoryEstimate memory;
      memory.state_bytes_per_action = state_bytes_per_action;
      memory.worker_threads = worker_threads;
      memory.topology_bytes = topology_bytes;
      memory.board_mapping_bytes = board_mapping_bytes;
      memory.certification_bytes = options.certification_bytes;
      memory.runtime_bytes = options.runtime_bytes;
      memory.reserve_bytes = options.reserve_bytes;
      if (!checked_product(report.action_entries, state_bytes_per_action,
                           memory.solver_state_bytes)) {
        return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
            CanonicalLayoutError::ArithmeticOverflow);
      }
      // The 4-byte i16/u16 profile uses one regret and one strategy scale per
      // canonical public decision node.  The joint 3-byte codec has no scale.
      if (state_bytes_per_action == 4U &&
          !checked_product(report.canonical_decision_nodes, 2U * sizeof(float),
                           memory.state_auxiliary_bytes)) {
        return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
            CanonicalLayoutError::ArithmeticOverflow);
      }
      std::uint64_t scratch_per_worker = 0U;
      std::uint64_t frame_values = 0U;
      if (!checked_product(report.maximum_live_combos, report.maximum_actions + 4U, frame_values) ||
          !checked_product(frame_values,
                           static_cast<std::uint64_t>(physical.value().maximum_depth) + 1U,
                           frame_values) ||
          !checked_product(frame_values, sizeof(double), scratch_per_worker)) {
        return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
            CanonicalLayoutError::ArithmeticOverflow);
      }
      scratch_per_worker = std::max(scratch_per_worker, minimum_worker_arena_bytes);
      if (!checked_product(scratch_per_worker, worker_threads, memory.worker_scratch_bytes)) {
        return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
            CanonicalLayoutError::ArithmeticOverflow);
      }
      memory.estimated_peak_bytes = memory.solver_state_bytes;
      if (!checked_add(memory.estimated_peak_bytes, memory.state_auxiliary_bytes) ||
          !checked_add(memory.estimated_peak_bytes, memory.topology_bytes) ||
          !checked_add(memory.estimated_peak_bytes, memory.board_mapping_bytes) ||
          !checked_add(memory.estimated_peak_bytes, memory.worker_scratch_bytes) ||
          !checked_add(memory.estimated_peak_bytes, memory.certification_bytes) ||
          !checked_add(memory.estimated_peak_bytes, memory.runtime_bytes) ||
          !checked_add(memory.estimated_peak_bytes, memory.reserve_bytes)) {
        return Result<CanonicalLayoutReport, CanonicalLayoutError>::failure(
            CanonicalLayoutError::ArithmeticOverflow);
      }
      if (options.requested_budget) {
        memory.meets_requested_budget =
            memory.estimated_peak_bytes <= options.requested_budget->bytes;
      }
      report.memory_estimates.push_back(memory);
    }
  }
  return Result<CanonicalLayoutReport, CanonicalLayoutError>::success(std::move(report));
}

const char *canonical_layout_error_name(const CanonicalLayoutError error) noexcept {
  switch (error) {
  case CanonicalLayoutError::InvalidConfiguration:
    return "invalid_configuration";
  case CanonicalLayoutError::TreeFailure:
    return "tree_failure";
  case CanonicalLayoutError::ArithmeticOverflow:
    return "arithmetic_overflow";
  case CanonicalLayoutError::NonIntegralShape:
    return "non_integral_shape";
  }
  return "unknown";
}

const char *
canonical_layout_budget_source_name(const CanonicalLayoutBudgetSource source) noexcept {
  switch (source) {
  case CanonicalLayoutBudgetSource::UserConfigured:
    return "user_configured";
  case CanonicalLayoutBudgetSource::Experiment:
    return "experiment";
  }
  return "unknown";
}

} // namespace gtosd
