#include "gtosd/postflop/postflop_subgame.hpp"

#include "gtosd/equity/evaluator.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gtosd {

namespace detail {

struct PostflopRiverBucketRuntime {
  struct StateRow {
    std::string information_set;
    std::vector<GameActionId> actions;
    std::uint64_t action_begin{0};
    std::uint8_t player{0};
  };

  struct PublicNode {
    PublicNodeKind kind{PublicNodeKind::Decision};
    std::vector<NodeId> children;
    std::uint64_t state_row_begin{0};
    std::uint8_t player{0};
    std::array<std::array<double, 2>, 3> terminal_payoffs{};
  };

  NodeId root{0};
  std::uint64_t action_entries{0};
  std::vector<StateRow> state_rows;
  std::vector<PublicNode> public_nodes;
};

} // namespace detail

namespace {

constexpr double units_per_ante = static_cast<double>(Money::units_per_ante);

std::string action_label(const Action &action) {
  std::ostringstream output;
  output << static_cast<unsigned>(action.type) << ':' << action.amount.units() << ':'
         << action.requested_basis_points << ':' << static_cast<unsigned>(action.all_in_kind);
  return output.str();
}

std::string projected_information_set(const std::string &fingerprint, const NodeId public_node,
                                      const std::uint8_t player, const ComboId combo) {
  return "postflop-projection/1/" + fingerprint + "/node/" + std::to_string(public_node) + "/p/" +
         std::to_string(player) + "/combo/" + std::to_string(combo);
}

std::string made_hand_bucket(const NodeId source_public_node, const std::uint8_t player,
                             const HandValue &made_hand) {
  std::ostringstream output;
  output << "postflop-made-hand/1/node/" << source_public_node << "/p/"
         << static_cast<unsigned>(player) << "/value/" << static_cast<unsigned>(made_hand.category);
  for (const auto kicker : made_hand.kickers) {
    output << ':' << static_cast<unsigned>(kicker);
  }
  return output.str();
}

std::string made_hand_bucket(const PostflopProjectedInformationSet &information_set) {
  return made_hand_bucket(information_set.source_public_node, information_set.player,
                          information_set.made_hand);
}

std::string river_bucket_information_set(const NodeId source_public_node, const std::uint8_t player,
                                         const PostflopRiverBucket &bucket,
                                         const PostflopRiverBucketAbstraction abstraction) {
  if (abstraction == PostflopRiverBucketAbstraction::MadeHandValueV1) {
    return made_hand_bucket(source_public_node, player, bucket.made_hand);
  }
  const auto prefix = abstraction == PostflopRiverBucketAbstraction::ExactBlockerSignatureV2
                          ? "postflop-exact-blocker/2"
                          : "postflop-showdown-distribution/3";
  std::ostringstream output;
  output << prefix << "/node/" << source_public_node << "/p/" << static_cast<unsigned>(player)
         << "/bucket/" << bucket.index << "/value/"
         << static_cast<unsigned>(bucket.made_hand.category);
  for (const auto kicker : bucket.made_hand.kickers) {
    output << ':' << static_cast<unsigned>(kicker);
  }
  return output.str();
}

bool valid_river_bucket_abstraction(const PostflopRiverBucketAbstraction abstraction) {
  return abstraction == PostflopRiverBucketAbstraction::MadeHandValueV1 ||
         abstraction == PostflopRiverBucketAbstraction::ExactBlockerSignatureV2 ||
         abstraction == PostflopRiverBucketAbstraction::ShowdownDistributionV3;
}

std::uint16_t quantize_range_mass(const std::uint64_t mass_basis_points,
                                  const std::uint64_t total_basis_points,
                                  const std::uint16_t quantum_basis_points) {
  if (total_basis_points == 0U || quantum_basis_points == 0U) {
    return 0U;
  }
  const auto normalized_basis_points =
      (mass_basis_points * 10'000U + total_basis_points / 2U) / total_basis_points;
  return static_cast<std::uint16_t>((normalized_basis_points + quantum_basis_points / 2U) /
                                    quantum_basis_points);
}

Result<HandValue, SolverError> projected_hand_value(const Combo &combo,
                                                    const std::vector<CardId> &board) {
  if (board.size() != 5U) {
    return Result<HandValue, SolverError>::failure(SolverError::InvalidConfiguration);
  }
  const std::array<CardId, 7> cards{combo.first, combo.second, board[0], board[1],
                                    board[2],    board[3],     board[4]};
  const auto value = evaluate_seven(cards);
  return value ? Result<HandValue, SolverError>::success(value.value())
               : Result<HandValue, SolverError>::failure(SolverError::InvalidGame);
}

bool add_checked(const std::uint64_t left, const std::uint64_t right, std::uint64_t &result) {
  if (left > std::numeric_limits<std::uint64_t>::max() - right) {
    return false;
  }
  result = left + right;
  return true;
}

bool multiply_checked(const std::uint64_t left, const std::uint64_t right, std::uint64_t &result) {
  if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return false;
  }
  result = left * right;
  return true;
}

void hash_bytes(std::uint64_t &hash, const std::string_view bytes) {
  constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
  for (const unsigned char byte : bytes) {
    hash ^= byte;
    hash *= fnv_prime;
  }
}

template <typename T> void hash_integer(std::uint64_t &hash, const T value) {
  const auto bytes = std::bit_cast<std::array<std::uint8_t, sizeof(T)>>(value);
  hash_bytes(hash, std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
}

std::string hash_string(const std::uint64_t hash) {
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

bool atomic_replace_file(const std::filesystem::path &temporary,
                         const std::filesystem::path &destination) {
#if defined(_WIN32)
  return MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  std::error_code error;
  std::filesystem::rename(temporary, destination, error);
  return !error;
#endif
}

std::uint64_t string_bytes(const std::string &value) {
  return static_cast<std::uint64_t>(value.capacity());
}

PostflopSubgameProjectionByteModel
projection_byte_model(const FiniteGame &game, const StrategyProfile &blueprint,
                      const std::vector<PostflopProjectedPublicState> &public_states,
                      const std::vector<PostflopProjectedInformationSet> &information_sets) {
  PostflopSubgameProjectionByteModel model;
  model.finite_game_bytes = sizeof(FiniteGame) +
                            static_cast<std::uint64_t>(game.nodes.capacity()) * sizeof(GameNode) +
                            string_bytes(game.game_id);
  for (const auto &node : game.nodes) {
    model.finite_game_bytes += string_bytes(node.information_set) +
                               static_cast<std::uint64_t>(node.edges.capacity()) * sizeof(GameEdge);
    for (const auto &edge : node.edges) {
      model.finite_game_bytes += string_bytes(edge.action.label);
    }
  }

  model.blueprint_bytes = sizeof(StrategyProfile);
  for (const auto &[key, strategy] : blueprint) {
    model.blueprint_bytes +=
        sizeof(key) + sizeof(strategy) + 3U * sizeof(void *) + string_bytes(key) +
        static_cast<std::uint64_t>(strategy.actions.capacity()) * sizeof(GameActionId) +
        static_cast<std::uint64_t>(strategy.probabilities.capacity()) * sizeof(double);
  }

  model.metadata_bytes =
      static_cast<std::uint64_t>(public_states.capacity()) * sizeof(PostflopProjectedPublicState) +
      static_cast<std::uint64_t>(information_sets.capacity()) *
          sizeof(PostflopProjectedInformationSet);
  for (const auto &state : public_states) {
    model.metadata_bytes +=
        string_bytes(state.history) +
        static_cast<std::uint64_t>(state.actions.capacity()) * sizeof(Action) +
        static_cast<std::uint64_t>(state.child_public_nodes.capacity()) * sizeof(NodeId) +
        static_cast<std::uint64_t>(state.physical_roots.capacity()) * sizeof(GameNodeId);
  }
  for (const auto &information_set : information_sets) {
    model.metadata_bytes += string_bytes(information_set.information_set);
  }
  if (!add_checked(model.finite_game_bytes, model.blueprint_bytes, model.total_bytes) ||
      !add_checked(model.total_bytes, model.metadata_bytes, model.total_bytes)) {
    return {};
  }
  return model;
}

std::uint64_t finite_game_bytes(const FiniteGame &game) {
  std::uint64_t result = sizeof(FiniteGame) +
                         static_cast<std::uint64_t>(game.nodes.capacity()) * sizeof(GameNode) +
                         string_bytes(game.game_id);
  for (const auto &node : game.nodes) {
    result += string_bytes(node.information_set) +
              static_cast<std::uint64_t>(node.edges.capacity()) * sizeof(GameEdge);
    for (const auto &edge : node.edges) {
      result += string_bytes(edge.action.label);
    }
  }
  return result;
}

std::uint64_t river_runtime_bytes(const detail::PostflopRiverBucketRuntime &runtime) {
  std::uint64_t result = sizeof(runtime) +
                         static_cast<std::uint64_t>(runtime.state_rows.capacity()) *
                             sizeof(detail::PostflopRiverBucketRuntime::StateRow) +
                         static_cast<std::uint64_t>(runtime.public_nodes.capacity()) *
                             sizeof(detail::PostflopRiverBucketRuntime::PublicNode);
  for (const auto &row : runtime.state_rows) {
    result += string_bytes(row.information_set) +
              static_cast<std::uint64_t>(row.actions.capacity()) * sizeof(GameActionId);
  }
  for (const auto &node : runtime.public_nodes) {
    result += static_cast<std::uint64_t>(node.children.capacity()) * sizeof(NodeId);
  }
  return result;
}

bool compatible_river_bucket_resume(const SolverCheckpoint &checkpoint,
                                    const PostflopRiverBucketGame &bucket_game,
                                    const SolverConfig &config,
                                    const detail::PostflopRiverBucketRuntime &runtime) {
  if (checkpoint.major != SolverCheckpoint::format_major ||
      checkpoint.minor > SolverCheckpoint::format_minor ||
      checkpoint.game_fingerprint != bucket_game.game_fingerprint ||
      checkpoint.completed_iterations > config.iterations ||
      checkpoint.config.algorithm != config.algorithm || checkpoint.config.seed != config.seed ||
      checkpoint.config.thread_count != config.thread_count ||
      checkpoint.config.averaging_delay != config.averaging_delay ||
      checkpoint.config.dcfr.positive_regret_exponent != config.dcfr.positive_regret_exponent ||
      checkpoint.config.dcfr.negative_regret_exponent != config.dcfr.negative_regret_exponent ||
      checkpoint.config.dcfr.strategy_exponent != config.dcfr.strategy_exponent ||
      checkpoint.information_sets.size() != runtime.state_rows.size()) {
    return false;
  }
  for (const auto &row : runtime.state_rows) {
    const auto found = checkpoint.information_sets.find(row.information_set);
    if (found == checkpoint.information_sets.end() || found->second.player != row.player ||
        found->second.actions != row.actions ||
        found->second.cumulative_regret.size() != row.actions.size() ||
        found->second.cumulative_strategy.size() != row.actions.size() ||
        !std::ranges::all_of(found->second.cumulative_regret,
                             [](const double value) { return std::isfinite(value); }) ||
        !std::ranges::all_of(found->second.cumulative_strategy, [](const double value) {
          return std::isfinite(value) && value >= 0.0;
        })) {
      return false;
    }
  }
  return true;
}

bool valid_river_bucket_profile(const detail::PostflopRiverBucketRuntime &runtime,
                                const StrategyProfile &profile) {
  if (profile.size() != runtime.state_rows.size()) {
    return false;
  }
  for (const auto &row : runtime.state_rows) {
    const auto found = profile.find(row.information_set);
    if (found == profile.end() || found->second.player != row.player ||
        found->second.actions != row.actions ||
        found->second.probabilities.size() != row.actions.size()) {
      return false;
    }
    double sum = 0.0;
    for (const double probability : found->second.probabilities) {
      if (!std::isfinite(probability) || probability < 0.0 || probability > 1.0) {
        return false;
      }
      sum += probability;
    }
    if (std::abs(sum - 1.0) > 1.0e-12) {
      return false;
    }
  }
  return true;
}

void compute_river_bucket_policy(const detail::PostflopRiverBucketRuntime &runtime,
                                 const std::vector<double> &regret, std::vector<double> &policy) {
  for (const auto &row : runtime.state_rows) {
    double positive_sum = 0.0;
    for (std::size_t action = 0U; action < row.actions.size(); ++action) {
      positive_sum += std::max(0.0, regret[row.action_begin + action]);
    }
    if (positive_sum <= 0.0) {
      const double uniform = 1.0 / static_cast<double>(row.actions.size());
      for (std::size_t action = 0U; action < row.actions.size(); ++action) {
        policy[row.action_begin + action] = uniform;
      }
      continue;
    }
    for (std::size_t action = 0U; action < row.actions.size(); ++action) {
      policy[row.action_begin + action] =
          std::max(0.0, regret[row.action_begin + action]) / positive_sum;
    }
  }
}

double
traverse_river_bucket_pair(const detail::PostflopRiverBucketRuntime &runtime, const NodeId node_id,
                           const PostflopRiverBucketPair &pair, const std::uint8_t showdown_outcome,
                           const std::uint8_t updating_player, const std::array<double, 2> reach,
                           const double chance_reach, const std::vector<double> &policy,
                           std::vector<double> &regret_delta, std::vector<double> &strategy_delta,
                           std::uint64_t &traversed_nodes) {
  ++traversed_nodes;
  const auto &node = runtime.public_nodes[static_cast<std::size_t>(node_id)];
  if (node.kind == PublicNodeKind::TerminalFold || node.kind == PublicNodeKind::TerminalShowdown) {
    return node.terminal_payoffs[showdown_outcome][updating_player];
  }
  const auto private_bucket = node.player == 0U ? pair.first_bucket : pair.second_bucket;
  const auto &row = runtime.state_rows[node.state_row_begin + private_bucket];
  std::array<double, PublicTreeStats::decision_action_bucket_count> action_values{};
  double value = 0.0;
  for (std::size_t action = 0U; action < node.children.size(); ++action) {
    auto child_reach = reach;
    const double action_probability = policy[row.action_begin + action];
    child_reach[node.player] *= action_probability;
    action_values[action] = traverse_river_bucket_pair(
        runtime, node.children[action], pair, showdown_outcome, updating_player, child_reach,
        chance_reach, policy, regret_delta, strategy_delta, traversed_nodes);
    value += action_probability * action_values[action];
  }
  if (node.player != updating_player) {
    return value;
  }
  const auto opponent = static_cast<std::uint8_t>(1U - node.player);
  const double counterfactual_weight = reach[opponent] * chance_reach;
  const double average_weight = reach[node.player] * chance_reach;
  for (std::size_t action = 0U; action < node.children.size(); ++action) {
    regret_delta[row.action_begin + action] +=
        counterfactual_weight * (action_values[action] - value);
    strategy_delta[row.action_begin + action] += average_weight * policy[row.action_begin + action];
  }
  return value;
}

std::string river_source_fingerprint(const PostflopTreeConfig &config, const PublicTree &tree,
                                     const PostflopRanges &ranges) {
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  hash_bytes(hash, "GTOSD_FIXED_RIVER_BUCKET_SOURCE_1");
  hash_bytes(hash, serialize_tree_config_json(config));
  hash_bytes(hash, public_tree_hash(tree));
  for (const auto &range : ranges.players) {
    for (const auto weight : range) {
      hash_integer(hash, weight.basis_points());
    }
  }
  return hash_string(hash);
}

std::string
river_abstraction_fingerprint(const std::array<std::vector<PostflopRiverBucket>, 2> &buckets,
                              const std::vector<PostflopRiverBucketPair> &pairs,
                              const PostflopRiverBucketAbstraction abstraction,
                              const std::uint16_t distribution_quantization_basis_points) {
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  if (abstraction == PostflopRiverBucketAbstraction::MadeHandValueV1) {
    hash_bytes(hash, "GTOSD_FIXED_RIVER_BUCKET_ABSTRACTION_1");
  } else if (abstraction == PostflopRiverBucketAbstraction::ExactBlockerSignatureV2) {
    hash_bytes(hash, "GTOSD_FIXED_RIVER_BUCKET_ABSTRACTION_2");
    hash_integer(hash, static_cast<std::uint8_t>(abstraction));
  } else {
    hash_bytes(hash, "GTOSD_FIXED_RIVER_BUCKET_ABSTRACTION_3");
    hash_integer(hash, static_cast<std::uint8_t>(abstraction));
    hash_integer(hash, distribution_quantization_basis_points);
  }
  for (const auto &player : buckets) {
    hash_integer(hash, static_cast<std::uint64_t>(player.size()));
    for (const auto &bucket : player) {
      hash_integer(hash, bucket.index);
      hash_integer(hash, static_cast<std::uint8_t>(bucket.made_hand.category));
      for (const auto kicker : bucket.made_hand.kickers) {
        hash_integer(hash, kicker);
      }
      for (const auto &member : bucket.members) {
        hash_integer(hash, member.combo);
        hash_integer(hash, member.range_weight_basis_points);
      }
    }
  }
  for (const auto &pair : pairs) {
    hash_integer(hash, pair.first_bucket);
    hash_integer(hash, pair.second_bucket);
    hash_integer(hash, pair.physical_deals);
    hash_integer(hash, std::bit_cast<std::uint64_t>(pair.unnormalized_mass));
  }
  return hash_string(hash);
}

} // namespace

const char *
postflop_river_bucket_abstraction_name(const PostflopRiverBucketAbstraction abstraction) noexcept {
  switch (abstraction) {
  case PostflopRiverBucketAbstraction::MadeHandValueV1:
    return "made_hand_value_v1";
  case PostflopRiverBucketAbstraction::ExactBlockerSignatureV2:
    return "exact_blocker_signature_v2";
  case PostflopRiverBucketAbstraction::ShowdownDistributionV3:
    return "showdown_distribution_v3";
  }
  return "unknown";
}

Result<PostflopRiverEquitablePartitionAnalysis, SolverError>
analyze_fixed_river_equitable_partition(const PostflopTreeConfig &config,
                                        const PostflopRanges &ranges,
                                        const std::uint64_t maximum_physical_deals) {
  struct Vertex {
    ComboId combo{0};
    std::uint64_t mask{0};
    std::uint16_t weight_basis_points{0};
    HandValue made_hand{};
  };
  struct InitialSignature {
    std::uint8_t player{0};
    HandValue made_hand{};

    bool operator<(const InitialSignature &other) const {
      if (player != other.player) {
        return player < other.player;
      }
      return made_hand < other.made_hand;
    }
  };
  struct RefinementSignature {
    std::uint32_t current_color{0};
    std::vector<std::pair<std::uint32_t, std::uint64_t>> compatible_weight_sums;

    bool operator<(const RefinementSignature &other) const {
      if (current_color != other.current_color) {
        return current_color < other.current_color;
      }
      return compatible_weight_sums < other.compatible_weight_sums;
    }
  };

  const auto board = configured_board(config);
  if (board.size() != 5U || !config.river || maximum_physical_deals == 0U ||
      !validate_postflop_ranges(config, ranges)) {
    return Result<PostflopRiverEquitablePartitionAnalysis, SolverError>::failure(
        SolverError::InvalidConfiguration);
  }
  std::uint64_t board_mask = 0U;
  for (const auto card : board) {
    board_mask |= card.mask();
  }
  const auto combos = all_combos();
  std::array<std::vector<Vertex>, 2> vertices;
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    for (ComboId combo = 0U; combo < combos.size(); ++combo) {
      const auto mask = combos[combo].first.mask() | combos[combo].second.mask();
      const auto weight = ranges.players[player][combo].basis_points();
      if ((mask & board_mask) != 0U || weight == 0U) {
        continue;
      }
      const auto value = projected_hand_value(combos[combo], board);
      if (!value) {
        return Result<PostflopRiverEquitablePartitionAnalysis, SolverError>::failure(value.error());
      }
      vertices[player].push_back({combo, mask, weight, value.value()});
    }
    if (vertices[player].empty()) {
      return Result<PostflopRiverEquitablePartitionAnalysis, SolverError>::failure(
          SolverError::InvalidConfiguration);
    }
  }

  std::uint64_t physical_deals = 0U;
  for (const auto &first : vertices[0]) {
    for (const auto &second : vertices[1]) {
      if ((first.mask & second.mask) != 0U) {
        continue;
      }
      if (physical_deals == maximum_physical_deals) {
        return Result<PostflopRiverEquitablePartitionAnalysis, SolverError>::failure(
            SolverError::InvalidConfiguration);
      }
      ++physical_deals;
    }
  }
  if (physical_deals == 0U) {
    return Result<PostflopRiverEquitablePartitionAnalysis, SolverError>::failure(
        SolverError::InvalidConfiguration);
  }

  std::array<std::vector<std::uint32_t>, 2> colors;
  std::map<InitialSignature, std::uint32_t> initial_color_ids;
  std::uint32_t next_initial_color = 0U;
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    colors[player].reserve(vertices[player].size());
    for (const auto &vertex : vertices[player]) {
      const InitialSignature signature{player, vertex.made_hand};
      const auto [entry, inserted] = initial_color_ids.try_emplace(signature, next_initial_color);
      if (inserted) {
        ++next_initial_color;
      }
      colors[player].push_back(entry->second);
    }
  }

  const auto class_count = [](const std::vector<std::uint32_t> &values) {
    auto unique = values;
    std::ranges::sort(unique);
    const auto last = std::ranges::unique(unique);
    unique.erase(last.begin(), last.end());
    return static_cast<std::uint64_t>(unique.size());
  };
  const auto neighbor_weight_sums =
      [&](const std::uint8_t player, const std::size_t vertex_index,
          const std::array<std::vector<std::uint32_t>, 2> &partition) {
        const auto opponent = static_cast<std::uint8_t>(1U - player);
        std::map<std::uint32_t, std::uint64_t> weight_sums;
        for (std::size_t index = 0U; index < vertices[opponent].size(); ++index) {
          if ((vertices[player][vertex_index].mask & vertices[opponent][index].mask) == 0U) {
            weight_sums[partition[opponent][index]] +=
                vertices[opponent][index].weight_basis_points;
          }
        }
        return std::vector<std::pair<std::uint32_t, std::uint64_t>>(weight_sums.begin(),
                                                                    weight_sums.end());
      };

  PostflopRiverEquitablePartitionAnalysis analysis;
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    analysis.active_combos[player] = vertices[player].size();
    analysis.initial_classes[player] = class_count(colors[player]);
  }
  std::uint64_t previous_class_count = analysis.initial_classes[0] + analysis.initial_classes[1];
  for (;;) {
    std::array<std::vector<std::uint32_t>, 2> refined;
    std::map<RefinementSignature, std::uint32_t> refined_color_ids;
    std::uint32_t next_refined_color = 0U;
    for (std::uint8_t player = 0U; player < 2U; ++player) {
      refined[player].reserve(vertices[player].size());
      for (std::size_t index = 0U; index < vertices[player].size(); ++index) {
        RefinementSignature signature{colors[player][index],
                                      neighbor_weight_sums(player, index, colors)};
        const auto [entry, inserted] =
            refined_color_ids.try_emplace(std::move(signature), next_refined_color);
        if (inserted) {
          ++next_refined_color;
        }
        refined[player].push_back(entry->second);
      }
    }
    ++analysis.refinement_rounds;
    const auto refined_class_count = class_count(refined[0]) + class_count(refined[1]);
    colors = std::move(refined);
    if (refined_class_count == previous_class_count) {
      break;
    }
    previous_class_count = refined_class_count;
  }

  analysis.partition_is_equitable = true;
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    analysis.stable_classes[player] = class_count(colors[player]);
    std::map<std::uint32_t, std::uint64_t> class_sizes;
    std::map<std::uint32_t, std::vector<std::pair<std::uint32_t, std::uint64_t>>> reference_counts;
    for (std::size_t index = 0U; index < vertices[player].size(); ++index) {
      const auto color = colors[player][index];
      analysis.maximum_stable_class_size[player] =
          std::max(analysis.maximum_stable_class_size[player], ++class_sizes[color]);
      const auto counts = neighbor_weight_sums(player, index, colors);
      const auto [entry, inserted] = reference_counts.try_emplace(color, counts);
      if (!inserted && entry->second != counts) {
        analysis.partition_is_equitable = false;
      }
    }
  }

  std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint64_t> compatible_pairs;
  for (std::size_t first = 0U; first < vertices[0].size(); ++first) {
    for (std::size_t second = 0U; second < vertices[1].size(); ++second) {
      if ((vertices[0][first].mask & vertices[1][second].mask) == 0U) {
        ++compatible_pairs[{colors[0][first], colors[1][second]}];
      }
    }
  }
  analysis.physical_deals = physical_deals;
  analysis.compatible_class_pairs = compatible_pairs.size();
  analysis.strategic_row_reduction =
      static_cast<double>(analysis.active_combos[0] + analysis.active_combos[1]) /
      static_cast<double>(analysis.stable_classes[0] + analysis.stable_classes[1]);
  analysis.deal_pair_reduction = static_cast<double>(analysis.physical_deals) /
                                 static_cast<double>(analysis.compatible_class_pairs);
  return analysis.partition_is_equitable
             ? Result<PostflopRiverEquitablePartitionAnalysis, SolverError>::success(analysis)
             : Result<PostflopRiverEquitablePartitionAnalysis, SolverError>::failure(
                   SolverError::InvalidGame);
}

Result<PostflopRiverBucketGame, SolverError>
build_fixed_river_bucket_game(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                              const PostflopRiverBucketBuildOptions &options) {
  const auto started = std::chrono::steady_clock::now();
  const auto board = configured_board(config);
  if (board.size() != 5U || !config.river || options.maximum_physical_deals == 0U ||
      options.maximum_bucket_pairs == 0U || options.maximum_materialized_validation_nodes == 0U ||
      !valid_river_bucket_abstraction(options.abstraction) ||
      (options.abstraction == PostflopRiverBucketAbstraction::ShowdownDistributionV3 &&
       (options.distribution_quantization_basis_points == 0U ||
        options.distribution_quantization_basis_points > 10'000U)) ||
      !validate_postflop_ranges(config, ranges)) {
    return Result<PostflopRiverBucketGame, SolverError>::failure(SolverError::InvalidConfiguration);
  }
  const auto built_tree = build_public_tree(config);
  if (!built_tree || built_tree.value().nodes.empty()) {
    return Result<PostflopRiverBucketGame, SolverError>::failure(SolverError::InvalidGame);
  }
  const auto &tree = built_tree.value();
  std::uint64_t public_action_entries = 0U;
  for (const auto &node : tree.nodes) {
    if (node.kind == PublicNodeKind::Chance) {
      return Result<PostflopRiverBucketGame, SolverError>::failure(
          SolverError::InvalidConfiguration);
    }
    if (node.kind == PublicNodeKind::Decision &&
        !add_checked(public_action_entries, node.edges.size(), public_action_entries)) {
      return Result<PostflopRiverBucketGame, SolverError>::failure(
          SolverError::InvalidConfiguration);
    }
  }

  struct WeightedCombo {
    ComboId id{0};
    std::uint64_t mask{0};
    std::uint16_t bucket{0};
    std::uint16_t weight_basis_points{0};
  };
  struct ActiveCombo {
    ComboId id{0};
    std::uint64_t mask{0};
    std::uint16_t weight_basis_points{0};
    HandValue made_hand{};
  };
  struct ExactBlockerSignature {
    HandValue made_hand{};
    std::vector<std::uint64_t> compatible_opponent_words;

    bool operator<(const ExactBlockerSignature &other) const {
      if (made_hand < other.made_hand) {
        return true;
      }
      if (other.made_hand < made_hand) {
        return false;
      }
      return compatible_opponent_words < other.compatible_opponent_words;
    }
  };
  struct ShowdownDistributionSignature {
    HandValue made_hand{};
    // Nine opponent made-hand categories followed by hero loss/tie/win mass.
    std::array<std::uint16_t, 12> quantized_mass{};

    bool operator<(const ShowdownDistributionSignature &other) const {
      if (made_hand < other.made_hand) {
        return true;
      }
      if (other.made_hand < made_hand) {
        return false;
      }
      return quantized_mass < other.quantized_mass;
    }
  };
  std::array<std::vector<ActiveCombo>, 2> active_combos;
  std::array<std::vector<WeightedCombo>, 2> weighted_combos;
  std::array<std::vector<PostflopRiverBucket>, 2> buckets;
  const auto combos = all_combos();
  std::uint64_t board_mask = 0U;
  for (const auto card : board) {
    board_mask |= card.mask();
  }
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    for (ComboId combo = 0U; combo < combos.size(); ++combo) {
      const auto mask = combos[combo].first.mask() | combos[combo].second.mask();
      const auto weight = ranges.players[player][combo].basis_points();
      if ((mask & board_mask) != 0U || weight == 0U) {
        continue;
      }
      const auto value = projected_hand_value(combos[combo], board);
      if (!value) {
        return Result<PostflopRiverBucketGame, SolverError>::failure(value.error());
      }
      active_combos[player].push_back({combo, mask, weight, value.value()});
    }
    if (active_combos[player].empty()) {
      return Result<PostflopRiverBucketGame, SolverError>::failure(
          SolverError::InvalidConfiguration);
    }
  }
  const auto append_bucket = [&](const std::uint8_t player, const HandValue &made_hand,
                                 std::vector<PostflopRiverBucketMember> members) {
    const auto index = static_cast<std::uint16_t>(buckets[player].size());
    double total_weight = 0.0;
    for (const auto &member : members) {
      total_weight += static_cast<double>(member.range_weight_basis_points) / 10'000.0;
    }
    buckets[player].push_back({index, made_hand, total_weight, std::move(members)});
  };
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    if (options.abstraction == PostflopRiverBucketAbstraction::MadeHandValueV1) {
      std::map<HandValue, std::vector<PostflopRiverBucketMember>> grouped;
      for (const auto &combo : active_combos[player]) {
        grouped[combo.made_hand].push_back({combo.id, combo.weight_basis_points});
      }
      for (auto &[made_hand, members] : grouped) {
        append_bucket(player, made_hand, std::move(members));
      }
    } else if (options.abstraction == PostflopRiverBucketAbstraction::ExactBlockerSignatureV2) {
      const auto opponent = static_cast<std::uint8_t>(1U - player);
      const auto signature_words = (active_combos[opponent].size() + 63U) / 64U;
      std::map<ExactBlockerSignature, std::vector<PostflopRiverBucketMember>> grouped;
      for (const auto &combo : active_combos[player]) {
        ExactBlockerSignature signature;
        signature.made_hand = combo.made_hand;
        signature.compatible_opponent_words.assign(signature_words, 0U);
        for (std::size_t index = 0U; index < active_combos[opponent].size(); ++index) {
          if ((combo.mask & active_combos[opponent][index].mask) == 0U) {
            signature.compatible_opponent_words[index / 64U] |= 1ULL << (index % 64U);
          }
        }
        grouped[std::move(signature)].push_back({combo.id, combo.weight_basis_points});
      }
      for (auto &[signature, members] : grouped) {
        append_bucket(player, signature.made_hand, std::move(members));
      }
    } else {
      constexpr std::size_t hand_category_count = 9U;
      const auto opponent = static_cast<std::uint8_t>(1U - player);
      const auto total_opponent_weight = std::accumulate(
          active_combos[opponent].begin(), active_combos[opponent].end(), std::uint64_t{0},
          [](const std::uint64_t total, const ActiveCombo &combo) {
            return total + combo.weight_basis_points;
          });
      if (total_opponent_weight == 0U) {
        return Result<PostflopRiverBucketGame, SolverError>::failure(
            SolverError::InvalidConfiguration);
      }
      std::map<ShowdownDistributionSignature, std::vector<PostflopRiverBucketMember>> grouped;
      for (const auto &combo : active_combos[player]) {
        std::array<std::uint64_t, 12> masses{};
        for (const auto &opponent_combo : active_combos[opponent]) {
          if ((combo.mask & opponent_combo.mask) != 0U) {
            continue;
          }
          const auto category = static_cast<std::size_t>(opponent_combo.made_hand.category);
          if (category >= hand_category_count) {
            return Result<PostflopRiverBucketGame, SolverError>::failure(SolverError::InvalidGame);
          }
          masses[category] += opponent_combo.weight_basis_points;
          const auto outcome = combo.made_hand < opponent_combo.made_hand   ? 0U
                               : opponent_combo.made_hand < combo.made_hand ? 2U
                                                                            : 1U;
          masses[hand_category_count + outcome] += opponent_combo.weight_basis_points;
        }
        ShowdownDistributionSignature signature;
        signature.made_hand = combo.made_hand;
        for (std::size_t feature = 0U; feature < masses.size(); ++feature) {
          signature.quantized_mass[feature] =
              quantize_range_mass(masses[feature], total_opponent_weight,
                                  options.distribution_quantization_basis_points);
        }
        grouped[std::move(signature)].push_back({combo.id, combo.weight_basis_points});
      }
      for (auto &[signature, members] : grouped) {
        append_bucket(player, signature.made_hand, std::move(members));
      }
    }
    if (buckets[player].empty() ||
        buckets[player].size() > std::numeric_limits<std::uint16_t>::max()) {
      return Result<PostflopRiverBucketGame, SolverError>::failure(
          SolverError::InvalidConfiguration);
    }
    for (const auto &bucket : buckets[player]) {
      for (const auto &member : bucket.members) {
        const auto &combo = combos[member.combo];
        weighted_combos[player].push_back({member.combo, combo.first.mask() | combo.second.mask(),
                                           bucket.index, member.range_weight_basis_points});
      }
    }
  }

  struct PairAccumulator {
    std::uint64_t physical_deals{0};
    long double mass{0.0L};
  };
  std::map<std::pair<std::uint16_t, std::uint16_t>, PairAccumulator> accumulated;
  std::uint64_t physical_deals = 0U;
  long double total_mass = 0.0L;
  for (const auto &first : weighted_combos[0]) {
    for (const auto &second : weighted_combos[1]) {
      if ((first.mask & second.mask) != 0U) {
        continue;
      }
      if (physical_deals == options.maximum_physical_deals) {
        return Result<PostflopRiverBucketGame, SolverError>::failure(
            SolverError::InvalidConfiguration);
      }
      ++physical_deals;
      const long double mass = static_cast<long double>(first.weight_basis_points) *
                               static_cast<long double>(second.weight_basis_points);
      auto &pair = accumulated[{first.bucket, second.bucket}];
      ++pair.physical_deals;
      pair.mass += mass;
      total_mass += mass;
    }
  }
  if (physical_deals == 0U || accumulated.empty() || !(total_mass > 0.0L) ||
      accumulated.size() > options.maximum_bucket_pairs) {
    return Result<PostflopRiverBucketGame, SolverError>::failure(SolverError::InvalidConfiguration);
  }

  std::vector<PostflopRiverBucketPair> pairs;
  pairs.reserve(accumulated.size());
  double probability_prefix = 0.0;
  for (auto iterator = accumulated.begin(); iterator != accumulated.end(); ++iterator) {
    const bool last = std::next(iterator) == accumulated.end();
    const double probability =
        last ? 1.0 - probability_prefix : static_cast<double>(iterator->second.mass / total_mass);
    if (!std::isfinite(probability) || probability <= 0.0) {
      return Result<PostflopRiverBucketGame, SolverError>::failure(
          SolverError::InvalidChanceProbabilities);
    }
    probability_prefix += probability;
    pairs.push_back({iterator->first.first, iterator->first.second, iterator->second.physical_deals,
                     static_cast<double>(iterator->second.mass), probability});
  }

  const auto source_fingerprint = river_source_fingerprint(config, tree, ranges);
  const auto abstraction_fingerprint = river_abstraction_fingerprint(
      buckets, pairs, options.abstraction, options.distribution_quantization_basis_points);
  std::uint64_t bucket_game_hash = 14'695'981'039'346'656'037ULL;
  hash_bytes(bucket_game_hash, "GTOSD_FIXED_RIVER_BUCKET_GAME_1");
  hash_bytes(bucket_game_hash, source_fingerprint);
  hash_bytes(bucket_game_hash, abstraction_fingerprint);
  const auto game_fingerprint = hash_string(bucket_game_hash);
  std::optional<FiniteGame> validation_game;
  if (options.materialize_validation_game) {
    std::uint64_t product_nodes = 0U;
    if (!multiply_checked(pairs.size(), tree.nodes.size(), product_nodes) ||
        !add_checked(product_nodes, 1U, product_nodes) ||
        product_nodes > options.maximum_materialized_validation_nodes ||
        product_nodes > std::numeric_limits<GameNodeId>::max()) {
      return Result<PostflopRiverBucketGame, SolverError>::failure(
          SolverError::InvalidConfiguration);
    }
    std::vector<GameNode> projected_nodes;
    projected_nodes.reserve(static_cast<std::size_t>(product_nodes));
    std::vector<GameEdge> chance_edges;
    chance_edges.reserve(pairs.size());
    for (std::size_t pair_index = 0U; pair_index < pairs.size(); ++pair_index) {
      const auto &pair = pairs[pair_index];
      const auto &first_value = buckets[0][pair.first_bucket].made_hand;
      const auto &second_value = buckets[1][pair.second_bucket].made_hand;
      const std::uint8_t winner_mask = first_value > second_value   ? 0b01U
                                       : second_value > first_value ? 0b10U
                                                                    : 0b11U;
      std::vector<std::optional<GameNodeId>> memo(tree.nodes.size());
      std::function<Result<GameNodeId, SolverError>(NodeId)> clone;
      clone = [&](const NodeId public_node) -> Result<GameNodeId, SolverError> {
        if (public_node >= tree.nodes.size()) {
          return Result<GameNodeId, SolverError>::failure(SolverError::InvalidGame);
        }
        auto &known = memo[static_cast<std::size_t>(public_node)];
        if (known) {
          return Result<GameNodeId, SolverError>::success(*known);
        }
        const auto &source = tree.nodes[static_cast<std::size_t>(public_node)];
        GameNode node;
        if (source.kind == PublicNodeKind::TerminalFold ||
            source.kind == PublicNodeKind::TerminalShowdown) {
          const auto settlement = source.kind == PublicNodeKind::TerminalFold
                                      ? settle_terminal(source.state, config.rake)
                                      : settle_terminal(source.state, config.rake, winner_mask);
          if (!settlement) {
            return Result<GameNodeId, SolverError>::failure(SolverError::InvalidGame);
          }
          node.kind = GameNodeKind::Terminal;
          for (std::uint8_t player = 0U; player < 2U; ++player) {
            node.payoff[player] =
                static_cast<double>(settlement.value().payoff_units[player]) / units_per_ante;
          }
        } else if (source.kind == PublicNodeKind::Decision) {
          node.kind = GameNodeKind::Decision;
          node.player = source.state.player_to_act;
          const auto bucket = node.player == 0U ? pair.first_bucket : pair.second_bucket;
          node.information_set = river_bucket_information_set(
              public_node, node.player, buckets[node.player][bucket], options.abstraction);
          node.edges.reserve(source.edges.size());
          for (std::size_t edge_index = 0U; edge_index < source.edges.size(); ++edge_index) {
            if (source.edges[edge_index].kind != PublicEdgeKind::Action) {
              return Result<GameNodeId, SolverError>::failure(SolverError::InvalidGame);
            }
            const auto child = clone(source.edges[edge_index].child);
            if (!child) {
              return child;
            }
            node.edges.push_back({{static_cast<GameActionId>(edge_index),
                                   action_label(source.edges[edge_index].action)},
                                  child.value(),
                                  0.0});
          }
        } else {
          return Result<GameNodeId, SolverError>::failure(SolverError::InvalidConfiguration);
        }
        const auto id = static_cast<GameNodeId>(projected_nodes.size());
        projected_nodes.push_back(std::move(node));
        known = id;
        return Result<GameNodeId, SolverError>::success(id);
      };
      const auto root = clone(tree.root);
      if (!root) {
        return Result<PostflopRiverBucketGame, SolverError>::failure(root.error());
      }
      chance_edges.push_back({{static_cast<GameActionId>(pair_index),
                               "bucket_pair_" + std::to_string(pair.first_bucket) + "_" +
                                   std::to_string(pair.second_bucket)},
                              root.value(),
                              pair.probability});
    }
    GameNode chance_root;
    chance_root.kind = GameNodeKind::Chance;
    chance_root.edges = std::move(chance_edges);
    const auto root = static_cast<GameNodeId>(projected_nodes.size());
    projected_nodes.push_back(std::move(chance_root));
    validation_game.emplace(
        FiniteGame{"postflop-fixed-river-bucket-native/1/" + source_fingerprint + "/" +
                       abstraction_fingerprint,
                   root, std::move(projected_nodes),
                   static_cast<double>(config.initial_pot.units()) / units_per_ante});
    const auto validation = validate_finite_game(*validation_game);
    if (!validation) {
      return Result<PostflopRiverBucketGame, SolverError>::failure(validation.error());
    }
  }

  auto runtime = std::make_shared<detail::PostflopRiverBucketRuntime>();
  runtime->root = tree.root;
  runtime->public_nodes.resize(tree.nodes.size());
  constexpr std::array<std::uint8_t, 3> showdown_winner_masks{0b01U, 0b10U, 0b11U};
  for (std::size_t node_index = 0U; node_index < tree.nodes.size(); ++node_index) {
    const auto &source = tree.nodes[node_index];
    if (source.id != node_index) {
      return Result<PostflopRiverBucketGame, SolverError>::failure(SolverError::InvalidGame);
    }
    auto &compact = runtime->public_nodes[node_index];
    compact.kind = source.kind;
    if (source.kind == PublicNodeKind::Decision) {
      if (source.edges.size() > PublicTreeStats::decision_action_bucket_count) {
        return Result<PostflopRiverBucketGame, SolverError>::failure(
            SolverError::InvalidConfiguration);
      }
      compact.player = source.state.player_to_act;
      compact.state_row_begin = runtime->state_rows.size();
      compact.children.reserve(source.edges.size());
      std::vector<GameActionId> actions;
      actions.reserve(source.edges.size());
      for (std::size_t action = 0U; action < source.edges.size(); ++action) {
        compact.children.push_back(source.edges[action].child);
        actions.push_back(static_cast<GameActionId>(action));
      }
      for (const auto &bucket : buckets[compact.player]) {
        detail::PostflopRiverBucketRuntime::StateRow row;
        row.information_set =
            river_bucket_information_set(source.id, compact.player, bucket, options.abstraction);
        row.actions = actions;
        row.action_begin = runtime->action_entries;
        row.player = compact.player;
        if (!add_checked(runtime->action_entries, actions.size(), runtime->action_entries)) {
          return Result<PostflopRiverBucketGame, SolverError>::failure(
              SolverError::InvalidConfiguration);
        }
        runtime->state_rows.push_back(std::move(row));
      }
      continue;
    }
    if (source.kind != PublicNodeKind::TerminalFold &&
        source.kind != PublicNodeKind::TerminalShowdown) {
      return Result<PostflopRiverBucketGame, SolverError>::failure(
          SolverError::InvalidConfiguration);
    }
    for (std::size_t outcome = 0U; outcome < showdown_winner_masks.size(); ++outcome) {
      const auto settlement =
          source.kind == PublicNodeKind::TerminalFold
              ? settle_terminal(source.state, config.rake)
              : settle_terminal(source.state, config.rake, showdown_winner_masks[outcome]);
      if (!settlement) {
        return Result<PostflopRiverBucketGame, SolverError>::failure(SolverError::InvalidGame);
      }
      for (std::uint8_t player = 0U; player < 2U; ++player) {
        compact.terminal_payoffs[outcome][player] =
            static_cast<double>(settlement.value().payoff_units[player]) / units_per_ante;
      }
    }
  }

  PostflopRiverBucketGame result;
  result.validation_game = std::move(validation_game);
  result.game_fingerprint = game_fingerprint;
  result.source_game_fingerprint = source_fingerprint;
  result.abstraction_fingerprint = abstraction_fingerprint;
  result.abstraction = options.abstraction;
  result.source_root = tree.root;
  result.player_buckets = std::move(buckets);
  result.bucket_pairs = std::move(pairs);
  result.runtime = runtime;
  for (const auto &node : tree.nodes) {
    if (node.kind != PublicNodeKind::Decision) {
      continue;
    }
    PostflopRiverPublicDecision decision;
    decision.source_public_node = node.id;
    decision.player_to_act = node.state.player_to_act;
    decision.actions.reserve(node.edges.size());
    for (const auto &edge : node.edges) {
      decision.actions.push_back(edge.action);
    }
    result.public_decisions.push_back(std::move(decision));
  }
  result.work_model.physical_deals_preprocessed = physical_deals;
  result.work_model.bucket_pairs = result.bucket_pairs.size();
  result.work_model.public_nodes_per_pair = tree.nodes.size();
  result.work_model.abstract_information_sets = runtime->state_rows.size();
  result.work_model.abstract_action_entries = runtime->action_entries;
  if (!multiply_checked(physical_deals, tree.nodes.size(),
                        result.work_model.physical_node_instances_per_player_pass) ||
      !multiply_checked(result.bucket_pairs.size(), tree.nodes.size(),
                        result.work_model.bucket_node_instances_per_player_pass) ||
      !multiply_checked(physical_deals, public_action_entries,
                        result.work_model.physical_action_entries_per_player_pass) ||
      !multiply_checked(result.bucket_pairs.size(), public_action_entries,
                        result.work_model.bucket_action_entries_per_player_pass)) {
    return Result<PostflopRiverBucketGame, SolverError>::failure(SolverError::InvalidConfiguration);
  }
  result.byte_model.finite_game_bytes =
      result.validation_game ? finite_game_bytes(*result.validation_game) : 0U;
  result.byte_model.bucket_metadata_bytes =
      static_cast<std::uint64_t>(result.bucket_pairs.capacity()) * sizeof(PostflopRiverBucketPair);
  for (const auto &player : result.player_buckets) {
    result.byte_model.bucket_metadata_bytes +=
        static_cast<std::uint64_t>(player.capacity()) * sizeof(PostflopRiverBucket);
    for (const auto &bucket : player) {
      result.byte_model.bucket_metadata_bytes +=
          static_cast<std::uint64_t>(bucket.members.capacity()) * sizeof(PostflopRiverBucketMember);
    }
  }
  result.byte_model.bucket_metadata_bytes +=
      static_cast<std::uint64_t>(result.public_decisions.capacity()) *
      sizeof(PostflopRiverPublicDecision);
  for (const auto &decision : result.public_decisions) {
    result.byte_model.bucket_metadata_bytes +=
        static_cast<std::uint64_t>(decision.actions.capacity()) * sizeof(Action);
  }
  result.byte_model.runtime_bytes = river_runtime_bytes(*runtime);
  if (!multiply_checked(runtime->action_entries, 2U * sizeof(double),
                        result.byte_model.solver_state_payload_bytes) ||
      !multiply_checked(runtime->action_entries, 2U * sizeof(double),
                        result.byte_model.checkpoint_mirror_payload_bytes) ||
      !multiply_checked(runtime->action_entries, 3U * sizeof(double),
                        result.byte_model.traversal_scratch_payload_bytes)) {
    return Result<PostflopRiverBucketGame, SolverError>::failure(SolverError::InvalidConfiguration);
  }
  if (!add_checked(result.byte_model.finite_game_bytes, result.byte_model.bucket_metadata_bytes,
                   result.byte_model.total_bytes) ||
      !add_checked(result.byte_model.total_bytes, result.byte_model.runtime_bytes,
                   result.byte_model.total_bytes) ||
      !add_checked(result.byte_model.total_bytes, result.byte_model.solver_state_payload_bytes,
                   result.byte_model.total_bytes) ||
      !add_checked(result.byte_model.total_bytes, result.byte_model.checkpoint_mirror_payload_bytes,
                   result.byte_model.total_bytes) ||
      !add_checked(result.byte_model.total_bytes, result.byte_model.traversal_scratch_payload_bytes,
                   result.byte_model.total_bytes)) {
    return Result<PostflopRiverBucketGame, SolverError>::failure(SolverError::InvalidConfiguration);
  }
  result.preparation_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  return Result<PostflopRiverBucketGame, SolverError>::success(std::move(result));
}

Result<SolveResult, SolverError>
solve_fixed_river_bucket_game(const PostflopRiverBucketGame &bucket_game,
                              const SolverConfig &config,
                              const SolverCheckpoint *const resume_from) {
  if (bucket_game.major != PostflopRiverBucketGame::format_major ||
      bucket_game.minor != PostflopRiverBucketGame::format_minor ||
      bucket_game.source_game_fingerprint.empty() || bucket_game.abstraction_fingerprint.empty() ||
      bucket_game.game_fingerprint.empty() || bucket_game.bucket_pairs.empty() ||
      !valid_river_bucket_abstraction(bucket_game.abstraction) || !bucket_game.runtime ||
      config.iterations == 0U || config.algorithm != SolverAlgorithm::ProductionDcfr ||
      config.thread_count != 1U || config.averaging_delay != 0U ||
      config.dcfr.positive_regret_exponent != 1.5 || config.dcfr.negative_regret_exponent != 0.0 ||
      config.dcfr.strategy_exponent != 3.0) {
    return Result<SolveResult, SolverError>::failure(SolverError::InvalidConfiguration);
  }
  const auto &runtime = *bucket_game.runtime;
  SolverCheckpoint checkpoint;
  if (resume_from != nullptr) {
    if (!compatible_river_bucket_resume(*resume_from, bucket_game, config, runtime)) {
      return Result<SolveResult, SolverError>::failure(SolverError::GameMismatch);
    }
    checkpoint = *resume_from;
    checkpoint.config.iterations = config.iterations;
  } else {
    checkpoint.game_fingerprint = bucket_game.game_fingerprint;
    checkpoint.config = config;
    checkpoint.rng_state = config.seed;
    for (const auto &row : runtime.state_rows) {
      InformationSetBuffer buffer;
      buffer.player = row.player;
      buffer.actions = row.actions;
      buffer.cumulative_regret.resize(row.actions.size(), 0.0);
      buffer.cumulative_strategy.resize(row.actions.size(), 0.0);
      if (!checkpoint.information_sets.emplace(row.information_set, std::move(buffer)).second) {
        return Result<SolveResult, SolverError>::failure(SolverError::InvalidInformationSet);
      }
    }
  }

  const auto action_count = static_cast<std::size_t>(runtime.action_entries);
  std::vector<double> regret(action_count, 0.0);
  std::vector<double> average(action_count, 0.0);
  std::vector<double> policy(action_count, 0.0);
  std::vector<double> regret_delta(action_count, 0.0);
  std::vector<double> strategy_delta(action_count, 0.0);
  for (const auto &row : runtime.state_rows) {
    const auto &buffer = checkpoint.information_sets.at(row.information_set);
    std::copy(buffer.cumulative_regret.begin(), buffer.cumulative_regret.end(),
              regret.begin() + static_cast<std::ptrdiff_t>(row.action_begin));
    std::copy(buffer.cumulative_strategy.begin(), buffer.cumulative_strategy.end(),
              average.begin() + static_cast<std::ptrdiff_t>(row.action_begin));
  }

  std::uint64_t traversed_nodes = 0U;
  for (std::uint64_t iteration = checkpoint.completed_iterations + 1U;
       iteration <= config.iterations; ++iteration) {
    const auto schedule = production_dcfr_schedule(iteration);
    if (schedule.reset_average_strategy) {
      std::ranges::fill(average, 0.0);
    }
    for (std::uint8_t updating_player = 0U; updating_player < 2U; ++updating_player) {
      compute_river_bucket_policy(runtime, regret, policy);
      std::ranges::fill(regret_delta, 0.0);
      std::ranges::fill(strategy_delta, 0.0);
      ++traversed_nodes;
      for (const auto &pair : bucket_game.bucket_pairs) {
        const auto &first_value = bucket_game.player_buckets[0][pair.first_bucket].made_hand;
        const auto &second_value = bucket_game.player_buckets[1][pair.second_bucket].made_hand;
        const std::uint8_t showdown_outcome = first_value > second_value   ? 0U
                                              : second_value > first_value ? 1U
                                                                           : 2U;
        static_cast<void>(traverse_river_bucket_pair(
            runtime, runtime.root, pair, showdown_outcome, updating_player, {1.0, 1.0},
            pair.probability, policy, regret_delta, strategy_delta, traversed_nodes));
      }
      const double regret_iteration = static_cast<double>(schedule.regret_discount_iteration);
      const double powered = std::pow(regret_iteration, 1.5);
      const double positive_discount = powered / (powered + 1.0);
      constexpr double negative_discount = 0.5;
      for (const auto &row : runtime.state_rows) {
        if (row.player != updating_player) {
          continue;
        }
        for (std::size_t action = 0U; action < row.actions.size(); ++action) {
          const auto index = static_cast<std::size_t>(row.action_begin) + action;
          regret[index] *= regret[index] > 0.0 ? positive_discount : negative_discount;
          regret[index] += regret_delta[index];
          average[index] += schedule.average_strategy_weight * strategy_delta[index];
        }
      }
    }
    checkpoint.completed_iterations = iteration;
  }

  if (!std::ranges::all_of(regret, [](const double value) { return std::isfinite(value); }) ||
      !std::ranges::all_of(
          average, [](const double value) { return std::isfinite(value) && value >= 0.0; })) {
    return Result<SolveResult, SolverError>::failure(SolverError::NumericalFailure);
  }
  compute_river_bucket_policy(runtime, regret, policy);
  SolveResult result;
  result.traversed_nodes = traversed_nodes;
  for (const auto &row : runtime.state_rows) {
    auto &buffer = checkpoint.information_sets.at(row.information_set);
    std::copy_n(regret.begin() + static_cast<std::ptrdiff_t>(row.action_begin), row.actions.size(),
                buffer.cumulative_regret.begin());
    std::copy_n(average.begin() + static_cast<std::ptrdiff_t>(row.action_begin), row.actions.size(),
                buffer.cumulative_strategy.begin());
    InformationSetStrategy strategy;
    strategy.player = row.player;
    strategy.actions = row.actions;
    strategy.probabilities.resize(row.actions.size(), 0.0);
    double positive_sum = 0.0;
    for (std::size_t action = 0U; action < row.actions.size(); ++action) {
      positive_sum += std::max(0.0, average[row.action_begin + action]);
    }
    for (std::size_t action = 0U; action < row.actions.size(); ++action) {
      strategy.probabilities[action] =
          positive_sum > 0.0 ? std::max(0.0, average[row.action_begin + action]) / positive_sum
                             : policy[row.action_begin + action];
    }
    double normalization = 0.0;
    for (const double probability : strategy.probabilities) {
      normalization += probability;
    }
    result.maximum_normalization_error =
        std::max(result.maximum_normalization_error, std::abs(normalization - 1.0));
    result.average_strategy.emplace(row.information_set, std::move(strategy));
  }
  result.checkpoint = std::move(checkpoint);
  return valid_river_bucket_profile(runtime, result.average_strategy)
             ? Result<SolveResult, SolverError>::success(std::move(result))
             : Result<SolveResult, SolverError>::failure(SolverError::InvalidStrategy);
}

Result<bool, SolverError>
save_fixed_river_bucket_checkpoint(const PostflopRiverBucketGame &bucket_game,
                                   const SolverCheckpoint &checkpoint, const std::string &path) {
  if (!bucket_game.runtime || path.empty() ||
      !compatible_river_bucket_resume(checkpoint, bucket_game, checkpoint.config,
                                      *bucket_game.runtime)) {
    return Result<bool, SolverError>::failure(SolverError::InvalidCheckpoint);
  }
  const auto serialized = serialize_solver_checkpoint(checkpoint);
  if (!serialized || serialized.value().size() > 256U * 1024U * 1024U ||
      serialized.value().size() >
          static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
    return Result<bool, SolverError>::failure(serialized ? SolverError::InvalidCheckpoint
                                                         : serialized.error());
  }
  std::uint64_t payload_hash = 14'695'981'039'346'656'037ULL;
  hash_bytes(payload_hash, "GTOSD_RIVER_BUCKET_CHECKPOINT_PAYLOAD_1");
  hash_bytes(payload_hash, serialized.value());
  const auto payload_checksum = hash_string(payload_hash);
  const std::filesystem::path destination(path);
  auto temporary = destination;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << "GTOSD_RIVER_BUCKET_CHECKPOINT " << PostflopRiverBucketGame::format_major << ' '
           << PostflopRiverBucketGame::format_minor << '\n'
           << std::quoted(bucket_game.source_game_fingerprint) << '\n'
           << std::quoted(bucket_game.abstraction_fingerprint) << '\n'
           << serialized.value().size() << '\n'
           << std::quoted(payload_checksum) << '\n';
    output.write(serialized.value().data(),
                 static_cast<std::streamsize>(serialized.value().size()));
    output.flush();
    if (!output) {
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      return Result<bool, SolverError>::failure(SolverError::IoFailure);
    }
  }
  if (!atomic_replace_file(temporary, destination)) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return Result<bool, SolverError>::failure(SolverError::IoFailure);
  }
  return Result<bool, SolverError>::success(true);
}

Result<SolverCheckpoint, SolverError>
load_fixed_river_bucket_checkpoint(const PostflopRiverBucketGame &bucket_game,
                                   const std::string &path) {
  if (!bucket_game.runtime || path.empty()) {
    return Result<SolverCheckpoint, SolverError>::failure(SolverError::InvalidCheckpoint);
  }
  std::ifstream input(path, std::ios::binary);
  std::string marker;
  std::uint32_t major = 0U;
  std::uint32_t minor = 0U;
  std::string source_fingerprint;
  std::string abstraction_fingerprint;
  std::string payload_checksum;
  std::uint64_t payload_size = 0U;
  if (!input ||
      !(input >> marker >> major >> minor >> std::quoted(source_fingerprint) >>
        std::quoted(abstraction_fingerprint) >> payload_size >> std::quoted(payload_checksum)) ||
      marker != "GTOSD_RIVER_BUCKET_CHECKPOINT" || major != PostflopRiverBucketGame::format_major ||
      minor > PostflopRiverBucketGame::format_minor ||
      source_fingerprint != bucket_game.source_game_fingerprint ||
      abstraction_fingerprint != bucket_game.abstraction_fingerprint || payload_size == 0U ||
      payload_size > 256U * 1024U * 1024U || input.get() != '\n') {
    return Result<SolverCheckpoint, SolverError>::failure(SolverError::InvalidCheckpoint);
  }
  std::string payload(static_cast<std::size_t>(payload_size), '\0');
  input.read(payload.data(), static_cast<std::streamsize>(payload.size()));
  if (!input || input.peek() != std::char_traits<char>::eof()) {
    return Result<SolverCheckpoint, SolverError>::failure(SolverError::InvalidCheckpoint);
  }
  std::uint64_t payload_hash = 14'695'981'039'346'656'037ULL;
  hash_bytes(payload_hash, "GTOSD_RIVER_BUCKET_CHECKPOINT_PAYLOAD_1");
  hash_bytes(payload_hash, payload);
  if (payload_checksum != hash_string(payload_hash)) {
    return Result<SolverCheckpoint, SolverError>::failure(SolverError::InvalidCheckpoint);
  }
  const auto checkpoint = deserialize_solver_checkpoint(payload);
  if (!checkpoint ||
      !compatible_river_bucket_resume(checkpoint.value(), bucket_game, checkpoint.value().config,
                                      *bucket_game.runtime)) {
    return Result<SolverCheckpoint, SolverError>::failure(checkpoint ? SolverError::GameMismatch
                                                                     : checkpoint.error());
  }
  return checkpoint;
}

Result<PostflopStrategyQuery, SolverError>
query_fixed_river_bucket_strategy(const PostflopRiverBucketGame &bucket_game,
                                  const StrategyProfile &profile, const NodeId public_node,
                                  const ComboId combo) {
  if (!bucket_game.runtime || !valid_river_bucket_profile(*bucket_game.runtime, profile)) {
    return Result<PostflopStrategyQuery, SolverError>::failure(SolverError::InvalidStrategy);
  }
  const auto decision =
      std::ranges::find_if(bucket_game.public_decisions, [&](const auto &candidate) {
        return candidate.source_public_node == public_node;
      });
  if (decision == bucket_game.public_decisions.end() || decision->player_to_act > 1U) {
    return Result<PostflopStrategyQuery, SolverError>::failure(SolverError::InvalidNode);
  }
  const auto &player_buckets = bucket_game.player_buckets[decision->player_to_act];
  const auto bucket = std::ranges::find_if(player_buckets, [&](const auto &candidate) {
    return std::ranges::any_of(candidate.members,
                               [&](const auto &member) { return member.combo == combo; });
  });
  if (bucket == player_buckets.end()) {
    return Result<PostflopStrategyQuery, SolverError>::failure(SolverError::InvalidInformationSet);
  }
  const auto key = river_bucket_information_set(public_node, decision->player_to_act, *bucket,
                                                bucket_game.abstraction);
  const auto strategy = profile.find(key);
  if (strategy == profile.end() || strategy->second.player != decision->player_to_act ||
      strategy->second.probabilities.size() != decision->actions.size()) {
    return Result<PostflopStrategyQuery, SolverError>::failure(SolverError::InvalidStrategy);
  }
  return Result<PostflopStrategyQuery, SolverError>::success(
      {public_node, combo, decision->actions, strategy->second.probabilities});
}

Result<StrategyProfile, SolverError>
lift_fixed_river_bucket_strategy(const PostflopSubgameProjection &exact_projection,
                                 const PostflopRiverBucketGame &bucket_game,
                                 const StrategyProfile &bucket_profile) {
  if (!bucket_game.runtime || !valid_river_bucket_abstraction(bucket_game.abstraction) ||
      !valid_river_bucket_profile(*bucket_game.runtime, bucket_profile)) {
    return Result<StrategyProfile, SolverError>::failure(SolverError::InvalidStrategy);
  }
  CardAbstractionPolicy policy;
  policy.policy_id = "postflop-projection-river-bucket/" +
                     std::string(postflop_river_bucket_abstraction_name(bucket_game.abstraction)) +
                     "/" + exact_projection.source_game_fingerprint;
  policy.similarity_metric = postflop_river_bucket_abstraction_name(bucket_game.abstraction);
  policy.street_scope = "river";
  policy.perfect_recall = PerfectRecallClaim::NotClaimed;
  policy.entries.reserve(exact_projection.information_sets.size());
  for (const auto &information_set : exact_projection.information_sets) {
    if (information_set.player > 1U || information_set.range_weight_basis_points == 0U) {
      return Result<StrategyProfile, SolverError>::failure(SolverError::InvalidAbstraction);
    }
    const auto &buckets = bucket_game.player_buckets[information_set.player];
    const auto bucket = std::ranges::find_if(buckets, [&](const auto &candidate) {
      return std::ranges::any_of(candidate.members, [&](const auto &member) {
        return member.combo == information_set.combo;
      });
    });
    if (bucket == buckets.end()) {
      return Result<StrategyProfile, SolverError>::failure(SolverError::InvalidAbstraction);
    }
    policy.entries.push_back(
        {information_set.information_set,
         river_bucket_information_set(information_set.source_public_node, information_set.player,
                                      *bucket, bucket_game.abstraction),
         static_cast<double>(information_set.range_weight_basis_points) / 10'000.0});
  }
  const auto validation = validate_card_abstraction_policy(exact_projection.game, policy);
  if (!validation) {
    return Result<StrategyProfile, SolverError>::failure(validation.error());
  }
  return lift_strategy_profile(exact_projection.game, policy, bucket_profile);
}

Result<PostflopSubgameProjection, SolverError>
project_fixed_river_postflop_game(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                                  const PostflopCheckpoint &checkpoint,
                                  const PostflopSubgameProjectionOptions &options) {
  const auto started = std::chrono::steady_clock::now();
  const auto board = configured_board(config);
  if (board.size() != 5U || !config.river || options.maximum_physical_deals == 0U ||
      options.maximum_projected_nodes == 0U ||
      checkpoint.major != PostflopCheckpoint::format_major ||
      checkpoint.minor != PostflopCheckpoint::format_minor ||
      checkpoint.algorithm != PostflopAlgorithm::ProductionDcfr ||
      checkpoint.completed_iterations == 0U || !validate_postflop_ranges(config, ranges)) {
    return Result<PostflopSubgameProjection, SolverError>::failure(
        SolverError::InvalidConfiguration);
  }

  const auto prepared = prepare_postflop_tree(config, ranges, true, true, true);
  if (!prepared) {
    return Result<PostflopSubgameProjection, SolverError>::failure(SolverError::InvalidGame);
  }
  const auto tree = prepared_postflop_public_tree(prepared.value());
  if (!tree || tree->nodes.empty()) {
    return Result<PostflopSubgameProjection, SolverError>::failure(SolverError::InvalidGame);
  }
  for (const auto &node : tree->nodes) {
    if (node.kind == PublicNodeKind::Chance) {
      return Result<PostflopSubgameProjection, SolverError>::failure(
          SolverError::InvalidConfiguration);
    }
  }

  struct WeightedCombo {
    ComboId id{0};
    double weight{0.0};
  };
  std::array<std::vector<WeightedCombo>, 2> player_combos;
  const auto combos = all_combos();
  std::uint64_t board_mask = 0U;
  for (const auto card : board) {
    board_mask |= card.mask();
  }
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    for (ComboId combo = 0U; combo < combos.size(); ++combo) {
      const auto mask = combos[combo].first.mask() | combos[combo].second.mask();
      const auto weight = ranges.players[player][combo].basis_points();
      if ((mask & board_mask) == 0U && weight != 0U) {
        player_combos[player].push_back({combo, static_cast<double>(weight) / 10'000.0});
      }
    }
  }

  struct Deal {
    ComboId first{0};
    ComboId second{0};
    double mass{0.0};
  };
  std::vector<Deal> deals;
  double total_mass = 0.0;
  for (const auto &first : player_combos[0]) {
    const auto first_mask = combos[first.id].first.mask() | combos[first.id].second.mask();
    for (const auto &second : player_combos[1]) {
      const auto second_mask = combos[second.id].first.mask() | combos[second.id].second.mask();
      if ((first_mask & second_mask) != 0U) {
        continue;
      }
      if (deals.size() >= options.maximum_physical_deals) {
        return Result<PostflopSubgameProjection, SolverError>::failure(
            SolverError::InvalidConfiguration);
      }
      const double mass = first.weight * second.weight;
      deals.push_back({first.id, second.id, mass});
      total_mass += mass;
    }
  }
  if (deals.empty() || !(total_mass > 0.0) || !std::isfinite(total_mass) ||
      deals.size() > (std::numeric_limits<std::uint64_t>::max() - 1U) / tree->nodes.size()) {
    return Result<PostflopSubgameProjection, SolverError>::failure(
        SolverError::InvalidConfiguration);
  }
  const auto maximum_nodes = static_cast<std::uint64_t>(deals.size()) * tree->nodes.size() + 1U;
  if (maximum_nodes > options.maximum_projected_nodes ||
      maximum_nodes > std::numeric_limits<GameNodeId>::max()) {
    return Result<PostflopSubgameProjection, SolverError>::failure(
        SolverError::InvalidConfiguration);
  }

  std::vector<std::optional<std::string>> histories(tree->nodes.size());
  histories[static_cast<std::size_t>(tree->root)] = "root";
  std::vector<NodeId> pending{tree->root};
  while (!pending.empty()) {
    const auto public_node = pending.back();
    pending.pop_back();
    const auto &node = tree->nodes[static_cast<std::size_t>(public_node)];
    for (const auto &edge : node.edges) {
      if (edge.kind != PublicEdgeKind::Action || edge.child >= tree->nodes.size()) {
        return Result<PostflopSubgameProjection, SolverError>::failure(SolverError::InvalidGame);
      }
      const auto history =
          *histories[static_cast<std::size_t>(public_node)] + "/" + action_label(edge.action);
      auto &child_history = histories[static_cast<std::size_t>(edge.child)];
      if (child_history && *child_history != history) {
        return Result<PostflopSubgameProjection, SolverError>::failure(SolverError::InvalidGame);
      }
      if (!child_history) {
        child_history = history;
        pending.push_back(edge.child);
      }
    }
  }

  std::vector<GameNode> projected_nodes;
  projected_nodes.reserve(static_cast<std::size_t>(maximum_nodes));
  std::map<NodeId, std::vector<GameNodeId>> occurrences;
  std::map<std::string, PostflopProjectedInformationSet> projected_information_sets;
  std::vector<GameEdge> deal_edges;
  deal_edges.reserve(deals.size());
  for (std::size_t deal_index = 0U; deal_index < deals.size(); ++deal_index) {
    const auto &deal = deals[deal_index];
    std::vector<std::optional<GameNodeId>> memo(tree->nodes.size());
    std::function<Result<GameNodeId, SolverError>(NodeId)> clone;
    clone = [&](const NodeId public_node) -> Result<GameNodeId, SolverError> {
      if (public_node >= tree->nodes.size()) {
        return Result<GameNodeId, SolverError>::failure(SolverError::InvalidGame);
      }
      auto &known = memo[static_cast<std::size_t>(public_node)];
      if (known) {
        return Result<GameNodeId, SolverError>::success(*known);
      }
      const auto &source = tree->nodes[static_cast<std::size_t>(public_node)];
      GameNode projected;
      if (source.kind == PublicNodeKind::TerminalFold ||
          source.kind == PublicNodeKind::TerminalShowdown) {
        Result<Settlement, TreeError> settlement =
            Result<Settlement, TreeError>::failure(TreeError::SettlementFailure);
        if (source.kind == PublicNodeKind::TerminalFold) {
          const auto folded = settle_terminal(source.state, config.rake);
          if (folded) {
            settlement = Result<Settlement, TreeError>::success(folded.value());
          }
        } else {
          const std::vector<std::array<CardId, 2>> holes{
              {combos[deal.first].first, combos[deal.first].second},
              {combos[deal.second].first, combos[deal.second].second}};
          const auto showdown = resolve_showdown_terminal(*tree, public_node, holes);
          if (showdown) {
            settlement = Result<Settlement, TreeError>::success(showdown.value().settlement);
          }
        }
        if (!settlement) {
          return Result<GameNodeId, SolverError>::failure(SolverError::InvalidGame);
        }
        projected.kind = GameNodeKind::Terminal;
        for (std::uint8_t player = 0U; player < 2U; ++player) {
          projected.payoff[player] =
              static_cast<double>(settlement.value().payoff_units[player]) / units_per_ante;
        }
      } else if (source.kind == PublicNodeKind::Decision) {
        projected.kind = GameNodeKind::Decision;
        projected.player = source.state.player_to_act;
        const ComboId private_combo = projected.player == 0U ? deal.first : deal.second;
        projected.information_set = projected_information_set(
            checkpoint.game_fingerprint, public_node, projected.player, private_combo);
        projected.edges.reserve(source.edges.size());
        for (std::size_t edge_index = 0U; edge_index < source.edges.size(); ++edge_index) {
          const auto child = clone(source.edges[edge_index].child);
          if (!child) {
            return child;
          }
          projected.edges.push_back({{static_cast<GameActionId>(edge_index),
                                      action_label(source.edges[edge_index].action)},
                                     child.value(),
                                     0.0});
        }
        if (!projected_information_sets.contains(projected.information_set)) {
          const auto made_hand = projected_hand_value(combos[private_combo], board);
          if (!made_hand) {
            return Result<GameNodeId, SolverError>::failure(made_hand.error());
          }
          projected_information_sets.emplace(
              projected.information_set,
              PostflopProjectedInformationSet{
                  projected.information_set, public_node, projected.player, private_combo,
                  ranges.players[projected.player][private_combo].basis_points(),
                  made_hand.value()});
        }
      } else {
        return Result<GameNodeId, SolverError>::failure(SolverError::InvalidConfiguration);
      }
      if (projected_nodes.size() >= std::numeric_limits<GameNodeId>::max()) {
        return Result<GameNodeId, SolverError>::failure(SolverError::InvalidConfiguration);
      }
      const auto projected_id = static_cast<GameNodeId>(projected_nodes.size());
      projected_nodes.push_back(std::move(projected));
      known = projected_id;
      occurrences[public_node].push_back(projected_id);
      return Result<GameNodeId, SolverError>::success(projected_id);
    };
    const auto deal_root = clone(tree->root);
    if (!deal_root) {
      return Result<PostflopSubgameProjection, SolverError>::failure(deal_root.error());
    }
    deal_edges.push_back(
        {{static_cast<GameActionId>(deal_index),
          "deal_" + std::to_string(deal.first) + "_" + std::to_string(deal.second)},
         deal_root.value(),
         deal.mass / total_mass});
  }

  GameNode chance_root;
  chance_root.kind = GameNodeKind::Chance;
  chance_root.edges = std::move(deal_edges);
  const auto projected_root = static_cast<GameNodeId>(projected_nodes.size());
  projected_nodes.push_back(std::move(chance_root));
  FiniteGame game{"postflop-fixed-river-projection/1/" + checkpoint.game_fingerprint,
                  projected_root, std::move(projected_nodes),
                  static_cast<double>(config.initial_pot.units()) / units_per_ante};
  if (!validate_finite_game(game)) {
    return Result<PostflopSubgameProjection, SolverError>::failure(SolverError::InvalidGame);
  }

  StrategyProfile blueprint;
  std::map<NodeId, std::map<ComboId, PostflopStrategyQuery>> strategies_by_node;
  for (const auto &[key, information_set] : projected_information_sets) {
    auto found_node = strategies_by_node.find(information_set.source_public_node);
    if (found_node == strategies_by_node.end()) {
      const auto queries =
          query_postflop_strategies(config, ranges, checkpoint, information_set.source_public_node);
      if (!queries) {
        return Result<PostflopSubgameProjection, SolverError>::failure(
            SolverError::InvalidCheckpoint);
      }
      std::map<ComboId, PostflopStrategyQuery> by_combo;
      for (const auto &query : queries.value()) {
        by_combo.emplace(query.combo, query);
      }
      found_node =
          strategies_by_node.emplace(information_set.source_public_node, std::move(by_combo)).first;
    }
    const auto query = found_node->second.find(information_set.combo);
    if (query == found_node->second.end() ||
        query->second.actions.size() != query->second.probabilities.size()) {
      return Result<PostflopSubgameProjection, SolverError>::failure(
          SolverError::InvalidCheckpoint);
    }
    InformationSetStrategy strategy;
    strategy.player = information_set.player;
    strategy.probabilities = query->second.probabilities;
    strategy.actions.reserve(strategy.probabilities.size());
    for (std::size_t action = 0U; action < strategy.probabilities.size(); ++action) {
      strategy.actions.push_back(static_cast<GameActionId>(action));
    }
    blueprint.emplace(key, std::move(strategy));
  }
  if (!validate_strategy_profile(game, blueprint)) {
    return Result<PostflopSubgameProjection, SolverError>::failure(SolverError::InvalidStrategy);
  }

  std::vector<PostflopProjectedPublicState> public_states;
  for (const auto &source : tree->nodes) {
    if (source.kind != PublicNodeKind::Decision) {
      continue;
    }
    PostflopProjectedPublicState state;
    state.source_public_node = source.id;
    state.history = histories[static_cast<std::size_t>(source.id)].value_or("unreachable");
    state.player_to_act = source.state.player_to_act;
    state.physical_roots = occurrences[source.id];
    state.actions.reserve(source.edges.size());
    state.child_public_nodes.reserve(source.edges.size());
    for (const auto &edge : source.edges) {
      state.actions.push_back(edge.action);
      state.child_public_nodes.push_back(edge.child);
    }
    public_states.push_back(std::move(state));
  }
  std::vector<PostflopProjectedInformationSet> information_sets;
  information_sets.reserve(projected_information_sets.size());
  for (auto &[key, information_set] : projected_information_sets) {
    static_cast<void>(key);
    information_sets.push_back(std::move(information_set));
  }

  PostflopSubgameProjection projection;
  projection.game = std::move(game);
  projection.blueprint = std::move(blueprint);
  projection.source_game_fingerprint = checkpoint.game_fingerprint;
  projection.source_iterations = checkpoint.completed_iterations;
  projection.source_root = tree->root;
  projection.physical_deals = deals.size();
  projection.public_states = std::move(public_states);
  projection.information_sets = std::move(information_sets);
  projection.byte_model = projection_byte_model(
      projection.game, projection.blueprint, projection.public_states, projection.information_sets);
  if (projection.byte_model.total_bytes == 0U) {
    return Result<PostflopSubgameProjection, SolverError>::failure(
        SolverError::InvalidConfiguration);
  }
  projection.projection_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  return Result<PostflopSubgameProjection, SolverError>::success(std::move(projection));
}

Result<PublicSubgameDefinition, SolverError>
define_projected_postflop_subgame(const PostflopSubgameProjection &projection,
                                  const NodeId source_public_node,
                                  const std::uint8_t resolving_player) {
  if (resolving_player > 1U || projection.source_game_fingerprint.empty() ||
      projection.source_iterations == 0U) {
    return Result<PublicSubgameDefinition, SolverError>::failure(SolverError::InvalidSubgame);
  }
  const auto found = std::ranges::find_if(projection.public_states, [&](const auto &state) {
    return state.source_public_node == source_public_node;
  });
  const auto opponent = static_cast<std::uint8_t>(1U - resolving_player);
  if (found == projection.public_states.end() || found->player_to_act != opponent ||
      found->physical_roots.empty()) {
    return Result<PublicSubgameDefinition, SolverError>::failure(SolverError::InvalidSubgame);
  }
  for (const auto &node : projection.game.nodes) {
    if (node.kind == GameNodeKind::Terminal &&
        std::abs(node.payoff[0] + node.payoff[1]) > 1.0e-12) {
      return Result<PublicSubgameDefinition, SolverError>::failure(
          SolverError::UnsupportedAlgorithm);
    }
  }
  PublicSubgameDefinition definition;
  definition.public_state_id = "postflop-public-state/1/" + projection.source_game_fingerprint +
                               "/node/" + std::to_string(source_public_node);
  definition.entry_history = found->history;
  definition.resolving_player = resolving_player;
  definition.roots = found->physical_roots;
  const auto validation = validate_public_subgame(projection.game, definition);
  return validation ? Result<PublicSubgameDefinition, SolverError>::success(std::move(definition))
                    : Result<PublicSubgameDefinition, SolverError>::failure(validation.error());
}

Result<CardAbstractionPolicy, SolverError>
make_projected_postflop_card_abstraction(const PostflopSubgameProjection &projection) {
  if (!validate_finite_game(projection.game) || projection.information_sets.empty() ||
      projection.source_game_fingerprint.empty()) {
    return Result<CardAbstractionPolicy, SolverError>::failure(SolverError::InvalidAbstraction);
  }
  CardAbstractionPolicy policy;
  policy.policy_id = "postflop-projection-made-hand-value/1/" + projection.source_game_fingerprint;
  policy.similarity_metric = "made_hand_value";
  policy.street_scope = "river";
  policy.perfect_recall = PerfectRecallClaim::NotClaimed;
  policy.entries.reserve(projection.information_sets.size());
  for (const auto &information_set : projection.information_sets) {
    if (information_set.range_weight_basis_points == 0U) {
      return Result<CardAbstractionPolicy, SolverError>::failure(SolverError::InvalidAbstraction);
    }
    policy.entries.push_back(
        {information_set.information_set, made_hand_bucket(information_set),
         static_cast<double>(information_set.range_weight_basis_points) / 10'000.0});
  }
  const auto validation = validate_card_abstraction_policy(projection.game, policy);
  return validation ? Result<CardAbstractionPolicy, SolverError>::success(std::move(policy))
                    : Result<CardAbstractionPolicy, SolverError>::failure(validation.error());
}

} // namespace gtosd
