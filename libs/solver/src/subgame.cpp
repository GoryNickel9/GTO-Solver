#include "gtosd/solver/subgame.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <set>
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
namespace {

constexpr std::uint64_t dense_boundary_record_bytes = 24U;
constexpr std::uint64_t dense_root_reach_bytes = 16U;
constexpr double reach_tolerance = 1.0e-12;
constexpr GameActionId gadget_terminate_action = std::numeric_limits<GameActionId>::max() - 1U;
constexpr GameActionId gadget_follow_action = std::numeric_limits<GameActionId>::max();

void hash_bytes(std::uint64_t &hash, const std::string_view bytes) {
  constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
  for (const unsigned char byte : bytes) {
    hash ^= byte;
    hash *= fnv_prime;
  }
}

std::string checksum(const std::string_view bytes) {
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  hash_bytes(hash, bytes);
  std::ostringstream output;
  output << std::hex << std::setw(16) << std::setfill('0') << hash;
  return output.str();
}

std::string wrap_file(const std::string_view marker, const std::string &payload) {
  std::ostringstream output;
  output << marker << ' ' << payload.size() << ' ' << checksum(payload) << '\n' << payload;
  return output.str();
}

Result<std::string, SolverError> unwrap_file(const std::string &serialized,
                                             const std::string_view expected_marker) {
  const auto line_end = serialized.find('\n');
  if (line_end == std::string::npos) {
    return Result<std::string, SolverError>::failure(SolverError::InvalidSubgame);
  }
  std::istringstream header(serialized.substr(0U, line_end));
  std::string marker;
  std::uint64_t payload_size = 0U;
  std::string expected_checksum;
  if (!(header >> marker >> payload_size >> expected_checksum) || marker != expected_marker) {
    return Result<std::string, SolverError>::failure(SolverError::InvalidSubgame);
  }
  header >> std::ws;
  if (!header.eof() || payload_size != serialized.size() - line_end - 1U) {
    return Result<std::string, SolverError>::failure(SolverError::InvalidSubgame);
  }
  std::string payload = serialized.substr(line_end + 1U);
  if (checksum(payload) != expected_checksum) {
    return Result<std::string, SolverError>::failure(SolverError::InvalidSubgame);
  }
  return Result<std::string, SolverError>::success(std::move(payload));
}

bool atomic_replace(const std::filesystem::path &temporary,
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

Result<bool, SolverError> save_wrapped_file(const std::string &path, const std::string &contents) {
  if (path.empty() ||
      contents.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
    return Result<bool, SolverError>::failure(SolverError::IoFailure);
  }
  const std::filesystem::path destination(path);
  std::filesystem::path temporary = destination;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output || !output.write(contents.data(), static_cast<std::streamsize>(contents.size())) ||
        !output.flush()) {
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      return Result<bool, SolverError>::failure(SolverError::IoFailure);
    }
  }
  if (!atomic_replace(temporary, destination)) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return Result<bool, SolverError>::failure(SolverError::IoFailure);
  }
  return Result<bool, SolverError>::success(true);
}

Result<std::string, SolverError> load_file(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Result<std::string, SolverError>::failure(SolverError::IoFailure);
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  if (!input.good() && !input.eof()) {
    return Result<std::string, SolverError>::failure(SolverError::IoFailure);
  }
  return Result<std::string, SolverError>::success(contents.str());
}

template <typename T> void hash_integer(std::uint64_t &hash, const T value) {
  const auto bytes = std::bit_cast<std::array<std::uint8_t, sizeof(T)>>(value);
  hash_bytes(hash, std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
}

const char *reach_status_token(const BoundaryReachStatus status) noexcept {
  switch (status) {
  case BoundaryReachStatus::Positive:
    return "positive";
  case BoundaryReachStatus::Zero:
    return "zero";
  }
  return "unknown";
}

Result<BoundaryReachStatus, SolverError> parse_reach_status(const std::string &token) {
  if (token == "positive") {
    return Result<BoundaryReachStatus, SolverError>::success(BoundaryReachStatus::Positive);
  }
  if (token == "zero") {
    return Result<BoundaryReachStatus, SolverError>::success(BoundaryReachStatus::Zero);
  }
  return Result<BoundaryReachStatus, SolverError>::failure(SolverError::InvalidSubgame);
}

bool supported_version(const std::uint32_t major, const std::uint32_t minor) {
  return major == PublicSubgameDefinition::format_major &&
         minor <= PublicSubgameDefinition::format_minor;
}

Result<std::set<std::string>, SolverError>
boundary_information_sets(const FiniteGame &game, const PublicSubgameDefinition &definition) {
  std::set<std::string> result;
  for (const auto root : definition.roots) {
    if (root >= game.nodes.size() || game.nodes[root].kind != GameNodeKind::Decision) {
      return Result<std::set<std::string>, SolverError>::failure(SolverError::InvalidSubgame);
    }
    result.insert(game.nodes[root].information_set);
  }
  return Result<std::set<std::string>, SolverError>::success(std::move(result));
}

Result<std::array<double, 2>, SolverError>
evaluate_node(const FiniteGame &game, const StrategyProfile &profile, const GameNodeId node_id,
              std::vector<std::optional<std::array<double, 2>>> &memo) {
  if (memo[node_id]) {
    return Result<std::array<double, 2>, SolverError>::success(*memo[node_id]);
  }
  const auto &node = game.nodes[node_id];
  if (node.kind == GameNodeKind::Terminal) {
    memo[node_id] = node.payoff;
    return Result<std::array<double, 2>, SolverError>::success(node.payoff);
  }

  std::array<double, 2> value{0.0, 0.0};
  for (std::size_t edge_index = 0U; edge_index < node.edges.size(); ++edge_index) {
    const auto child = evaluate_node(game, profile, node.edges[edge_index].child, memo);
    if (!child) {
      return Result<std::array<double, 2>, SolverError>::failure(child.error());
    }
    double probability = node.edges[edge_index].probability;
    if (node.kind == GameNodeKind::Decision) {
      const auto strategy = profile.find(node.information_set);
      if (strategy == profile.end() || edge_index >= strategy->second.probabilities.size()) {
        return Result<std::array<double, 2>, SolverError>::failure(SolverError::InvalidStrategy);
      }
      probability = strategy->second.probabilities[edge_index];
    }
    value[0] += probability * child.value()[0];
    value[1] += probability * child.value()[1];
  }
  if (!std::isfinite(value[0]) || !std::isfinite(value[1])) {
    return Result<std::array<double, 2>, SolverError>::failure(SolverError::NumericalFailure);
  }
  memo[node_id] = value;
  return Result<std::array<double, 2>, SolverError>::success(value);
}

} // namespace

Result<PublicSubgameSummary, SolverError>
validate_public_subgame(const FiniteGame &game, const PublicSubgameDefinition &definition) {
  const auto game_validation = validate_finite_game(game);
  if (!game_validation) {
    return Result<PublicSubgameSummary, SolverError>::failure(game_validation.error());
  }
  if (!supported_version(definition.major, definition.minor)) {
    return Result<PublicSubgameSummary, SolverError>::failure(
        SolverError::UnsupportedSubgameVersion);
  }
  if (definition.public_state_id.empty() || definition.entry_history.empty() ||
      definition.resolving_player > 1U || definition.roots.empty()) {
    return Result<PublicSubgameSummary, SolverError>::failure(SolverError::InvalidSubgame);
  }

  const auto opponent = static_cast<std::uint8_t>(1U - definition.resolving_player);
  std::set<GameNodeId> roots;
  std::set<std::string> root_information_sets;
  for (const auto root : definition.roots) {
    if (root >= game.nodes.size() || !roots.insert(root).second) {
      return Result<PublicSubgameSummary, SolverError>::failure(SolverError::InvalidSubgame);
    }
    const auto &node = game.nodes[root];
    if (node.kind != GameNodeKind::Decision || node.player != opponent ||
        node.information_set.empty()) {
      return Result<PublicSubgameSummary, SolverError>::failure(SolverError::InvalidSubgame);
    }
    root_information_sets.insert(node.information_set);
  }

  for (GameNodeId node_id = 0U; node_id < game.nodes.size(); ++node_id) {
    const auto &node = game.nodes[node_id];
    if (node.kind == GameNodeKind::Decision &&
        root_information_sets.contains(node.information_set) && !roots.contains(node_id)) {
      return Result<PublicSubgameSummary, SolverError>::failure(SolverError::InvalidSubgame);
    }
  }

  std::vector<std::int64_t> owner(game.nodes.size(), -1);
  std::set<std::string> included_information_sets;
  for (std::size_t root_index = 0U; root_index < definition.roots.size(); ++root_index) {
    std::vector<GameNodeId> pending{definition.roots[root_index]};
    while (!pending.empty()) {
      const auto node_id = pending.back();
      pending.pop_back();
      if (owner[node_id] >= 0) {
        if (owner[node_id] != static_cast<std::int64_t>(root_index)) {
          return Result<PublicSubgameSummary, SolverError>::failure(SolverError::InvalidSubgame);
        }
        continue;
      }
      owner[node_id] = static_cast<std::int64_t>(root_index);
      const auto &node = game.nodes[node_id];
      if (node.kind == GameNodeKind::Decision) {
        included_information_sets.insert(node.information_set);
      }
      for (const auto &edge : node.edges) {
        pending.push_back(edge.child);
      }
    }
  }

  for (GameNodeId node_id = 0U; node_id < game.nodes.size(); ++node_id) {
    const auto &node = game.nodes[node_id];
    if (node.kind == GameNodeKind::Decision &&
        included_information_sets.contains(node.information_set) && owner[node_id] < 0) {
      return Result<PublicSubgameSummary, SolverError>::failure(SolverError::InvalidSubgame);
    }
  }

  PublicSubgameSummary summary;
  summary.root_nodes = roots.size();
  summary.subtree_nodes = static_cast<std::uint64_t>(
      std::ranges::count_if(owner, [](const auto value) { return value >= 0; }));
  summary.boundary_information_sets = root_information_sets.size();
  summary.opponent = opponent;
  summary.information_set_closed = true;
  return Result<PublicSubgameSummary, SolverError>::success(summary);
}

Result<bool, SolverError> validate_subgame_boundary(const FiniteGame &game,
                                                    const PublicSubgameDefinition &definition,
                                                    const SubgameBoundary &boundary) {
  const auto subgame = validate_public_subgame(game, definition);
  if (!subgame) {
    return Result<bool, SolverError>::failure(subgame.error());
  }
  if (boundary.major != SubgameBoundary::format_major ||
      boundary.minor != SubgameBoundary::format_minor) {
    return Result<bool, SolverError>::failure(SolverError::UnsupportedSubgameVersion);
  }
  if (boundary.game_fingerprint != finite_game_fingerprint(game) ||
      boundary.blueprint_fingerprint.empty() ||
      boundary.public_state_id != definition.public_state_id ||
      boundary.algorithm != "ProductionDcfr" || boundary.blueprint_iterations == 0U ||
      boundary.opponent != subgame.value().opponent) {
    return Result<bool, SolverError>::failure(SolverError::InvalidSubgame);
  }

  const auto expected = boundary_information_sets(game, definition);
  if (!expected || boundary.values.size() != expected.value().size()) {
    return Result<bool, SolverError>::failure(expected ? SolverError::InvalidSubgame
                                                       : expected.error());
  }
  std::set<std::string> observed;
  bool has_positive_reach = false;
  for (const auto &value : boundary.values) {
    if (!expected.value().contains(value.opponent_information_set) ||
        !observed.insert(value.opponent_information_set).second ||
        !std::isfinite(value.counterfactual_reach) || value.counterfactual_reach < 0.0 ||
        !std::isfinite(value.blueprint_counterfactual_value)) {
      return Result<bool, SolverError>::failure(SolverError::InvalidSubgame);
    }
    const bool positive = value.counterfactual_reach > 0.0;
    if ((positive && value.reach_status != BoundaryReachStatus::Positive) ||
        (!positive && value.reach_status != BoundaryReachStatus::Zero)) {
      return Result<bool, SolverError>::failure(SolverError::InvalidSubgame);
    }
    has_positive_reach = has_positive_reach || positive;
  }
  if (!has_positive_reach) {
    return Result<bool, SolverError>::failure(SolverError::InvalidSubgame);
  }

  if (boundary.root_reaches.size() != definition.roots.size()) {
    return Result<bool, SolverError>::failure(SolverError::InvalidSubgame);
  }
  std::set<GameNodeId> expected_roots(definition.roots.begin(), definition.roots.end());
  std::set<GameNodeId> observed_roots;
  std::map<std::string, double> reach_by_information_set;
  for (const auto &root_reach : boundary.root_reaches) {
    if (!expected_roots.contains(root_reach.root) ||
        !observed_roots.insert(root_reach.root).second ||
        game.nodes[root_reach.root].information_set != root_reach.opponent_information_set ||
        !std::isfinite(root_reach.counterfactual_reach) || root_reach.counterfactual_reach < 0.0) {
      return Result<bool, SolverError>::failure(SolverError::InvalidSubgame);
    }
    reach_by_information_set[root_reach.opponent_information_set] +=
        root_reach.counterfactual_reach;
  }
  for (const auto &value : boundary.values) {
    const double scale = std::max(1.0, std::abs(value.counterfactual_reach));
    if (std::abs(reach_by_information_set[value.opponent_information_set] -
                 value.counterfactual_reach) > reach_tolerance * scale) {
      return Result<bool, SolverError>::failure(SolverError::InvalidSubgame);
    }
  }
  return Result<bool, SolverError>::success(true);
}

Result<std::string, SolverError> strategy_profile_fingerprint(const FiniteGame &game,
                                                              const StrategyProfile &profile) {
  const auto validation = validate_strategy_profile(game, profile);
  if (!validation) {
    return Result<std::string, SolverError>::failure(validation.error());
  }
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  hash_bytes(hash, "GTOSD_STRATEGY_PROFILE_1");
  hash_bytes(hash, finite_game_fingerprint(game));
  for (const auto &[information_set, strategy] : profile) {
    hash_bytes(hash, information_set);
    hash_integer(hash, strategy.player);
    hash_integer(hash, static_cast<std::uint64_t>(strategy.actions.size()));
    for (std::size_t index = 0U; index < strategy.actions.size(); ++index) {
      hash_integer(hash, strategy.actions[index]);
      hash_integer(hash, std::bit_cast<std::uint64_t>(strategy.probabilities[index]));
    }
  }
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
  return Result<std::string, SolverError>::success(output.str());
}

Result<SubgameBoundary, SolverError>
derive_subgame_boundary(const FiniteGame &game, const PublicSubgameDefinition &definition,
                        const StrategyProfile &blueprint,
                        const std::uint64_t blueprint_iterations) {
  const auto public_state = validate_public_subgame(game, definition);
  const auto profile_validation = validate_strategy_profile(game, blueprint);
  const auto profile_fingerprint = strategy_profile_fingerprint(game, blueprint);
  if (!public_state || !profile_validation || !profile_fingerprint || blueprint_iterations == 0U) {
    return Result<SubgameBoundary, SolverError>::failure(
        !public_state          ? public_state.error()
        : !profile_validation  ? profile_validation.error()
        : !profile_fingerprint ? profile_fingerprint.error()
                               : SolverError::InvalidSubgame);
  }

  struct BoundaryAccumulator {
    double reach{0.0};
    double weighted_value{0.0};
  };
  std::map<std::string, BoundaryAccumulator> accumulators;
  std::vector<SubgameRootReach> root_reaches;
  std::set<GameNodeId> roots(definition.roots.begin(), definition.roots.end());
  for (const auto root : definition.roots) {
    accumulators.try_emplace(game.nodes[root].information_set);
  }

  std::vector<std::optional<std::array<double, 2>>> memo(game.nodes.size());
  struct PendingNode {
    GameNodeId node{0};
    double opponent_counterfactual_reach{0.0};
  };
  std::vector<PendingNode> pending{{game.root, 1.0}};
  const auto opponent = public_state.value().opponent;
  while (!pending.empty()) {
    const auto current = pending.back();
    pending.pop_back();
    if (roots.contains(current.node)) {
      const auto node_value = evaluate_node(game, blueprint, current.node, memo);
      if (!node_value) {
        return Result<SubgameBoundary, SolverError>::failure(node_value.error());
      }
      auto &accumulator = accumulators.at(game.nodes[current.node].information_set);
      accumulator.reach += current.opponent_counterfactual_reach;
      accumulator.weighted_value +=
          current.opponent_counterfactual_reach * node_value.value()[opponent];
      root_reaches.push_back({current.node, game.nodes[current.node].information_set,
                              current.opponent_counterfactual_reach});
      continue;
    }
    const auto &node = game.nodes[current.node];
    if (node.kind == GameNodeKind::Terminal) {
      continue;
    }
    for (std::size_t edge_index = 0U; edge_index < node.edges.size(); ++edge_index) {
      double multiplier = node.edges[edge_index].probability;
      if (node.kind == GameNodeKind::Decision) {
        if (node.player == opponent) {
          multiplier = 1.0;
        } else {
          const auto strategy = blueprint.find(node.information_set);
          if (strategy == blueprint.end() || edge_index >= strategy->second.probabilities.size()) {
            return Result<SubgameBoundary, SolverError>::failure(SolverError::InvalidStrategy);
          }
          multiplier = strategy->second.probabilities[edge_index];
        }
      }
      pending.push_back(
          {node.edges[edge_index].child, current.opponent_counterfactual_reach * multiplier});
    }
  }

  SubgameBoundary boundary;
  boundary.game_fingerprint = finite_game_fingerprint(game);
  boundary.blueprint_fingerprint = profile_fingerprint.value();
  boundary.public_state_id = definition.public_state_id;
  boundary.algorithm = "ProductionDcfr";
  boundary.blueprint_iterations = blueprint_iterations;
  boundary.opponent = opponent;
  std::ranges::sort(root_reaches, {}, &SubgameRootReach::root);
  boundary.root_reaches = std::move(root_reaches);
  for (const auto &[information_set, accumulator] : accumulators) {
    SubgameBoundaryValue value;
    value.opponent_information_set = information_set;
    value.counterfactual_reach = accumulator.reach;
    if (accumulator.reach > 0.0) {
      value.blueprint_counterfactual_value = accumulator.weighted_value / accumulator.reach;
      value.reach_status = BoundaryReachStatus::Positive;
    } else {
      value.blueprint_counterfactual_value = 0.0;
      value.reach_status = BoundaryReachStatus::Zero;
    }
    boundary.values.push_back(std::move(value));
  }
  const auto boundary_validation = validate_subgame_boundary(game, definition, boundary);
  return boundary_validation
             ? Result<SubgameBoundary, SolverError>::success(std::move(boundary))
             : Result<SubgameBoundary, SolverError>::failure(boundary_validation.error());
}

Result<FiniteGame, SolverError>
build_safe_resolving_gadget(const FiniteGame &game, const PublicSubgameDefinition &definition,
                            const SubgameBoundary &boundary) {
  const auto validation = validate_subgame_boundary(game, definition, boundary);
  if (!validation) {
    return Result<FiniteGame, SolverError>::failure(validation.error());
  }
  for (const auto &node : game.nodes) {
    if (node.kind == GameNodeKind::Terminal &&
        std::abs(node.payoff[0] + node.payoff[1]) > reach_tolerance) {
      return Result<FiniteGame, SolverError>::failure(SolverError::InvalidSubgame);
    }
  }

  std::map<std::string, double> opt_out_values;
  for (const auto &value : boundary.values) {
    opt_out_values.emplace(value.opponent_information_set, value.blueprint_counterfactual_value);
  }
  std::map<GameNodeId, double> root_reaches;
  double total_reach = 0.0;
  for (const auto &root_reach : boundary.root_reaches) {
    root_reaches.emplace(root_reach.root, root_reach.counterfactual_reach);
    total_reach += root_reach.counterfactual_reach;
  }
  if (!std::isfinite(total_reach) || total_reach <= 0.0) {
    return Result<FiniteGame, SolverError>::failure(SolverError::InvalidSubgame);
  }

  std::vector<GameNode> nodes;
  std::map<GameNodeId, GameNodeId> clones;
  const std::string resolved_prefix = "resolved:" + definition.public_state_id + ":";
  std::function<GameNodeId(GameNodeId)> clone_subtree = [&](const GameNodeId source_id) {
    const auto existing = clones.find(source_id);
    if (existing != clones.end()) {
      return existing->second;
    }
    GameNode clone = game.nodes[source_id];
    if (clone.kind == GameNodeKind::Decision) {
      clone.information_set = resolved_prefix + clone.information_set;
    }
    for (auto &edge : clone.edges) {
      edge.child = clone_subtree(edge.child);
    }
    const auto clone_id = static_cast<GameNodeId>(nodes.size());
    nodes.push_back(std::move(clone));
    clones.emplace(source_id, clone_id);
    return clone_id;
  };

  std::vector<GameEdge> chance_edges;
  for (const auto root : definition.roots) {
    const double root_reach = root_reaches.at(root);
    if (root_reach == 0.0) {
      continue;
    }
    const auto cloned_root = clone_subtree(root);
    const auto &original_root = game.nodes[root];
    const double opponent_value = opt_out_values.at(original_root.information_set);

    GameNode terminate;
    terminate.kind = GameNodeKind::Terminal;
    terminate.payoff[boundary.opponent] = opponent_value;
    terminate.payoff[definition.resolving_player] = -opponent_value;
    const auto terminate_id = static_cast<GameNodeId>(nodes.size());
    nodes.push_back(std::move(terminate));

    GameNode choice;
    choice.kind = GameNodeKind::Decision;
    choice.player = boundary.opponent;
    choice.information_set =
        "gadget:" + definition.public_state_id + ":" + original_root.information_set;
    choice.edges = {{{gadget_terminate_action, "take_blueprint"}, terminate_id, 0.0},
                    {{gadget_follow_action, "follow"}, cloned_root, 0.0}};
    const auto choice_id = static_cast<GameNodeId>(nodes.size());
    nodes.push_back(std::move(choice));
    chance_edges.push_back(
        {{root, "root_" + std::to_string(root)}, choice_id, root_reach / total_reach});
  }
  if (chance_edges.empty()) {
    return Result<FiniteGame, SolverError>::failure(SolverError::InvalidSubgame);
  }
  GameNode chance;
  chance.kind = GameNodeKind::Chance;
  chance.edges = std::move(chance_edges);
  const auto root = static_cast<GameNodeId>(nodes.size());
  nodes.push_back(std::move(chance));

  FiniteGame gadget{"safe_resolving_gadget:" + definition.public_state_id + ":" +
                        boundary.blueprint_fingerprint,
                    root, std::move(nodes), game.initial_pot};
  const auto gadget_validation = validate_finite_game(gadget);
  return gadget_validation ? Result<FiniteGame, SolverError>::success(std::move(gadget))
                           : Result<FiniteGame, SolverError>::failure(gadget_validation.error());
}

Result<StrategyProfile, SolverError>
splice_resolved_subgame_strategy(const FiniteGame &game, const PublicSubgameDefinition &definition,
                                 const SubgameBoundary &boundary, const StrategyProfile &blueprint,
                                 const StrategyProfile &gadget_strategy) {
  const auto blueprint_validation = validate_strategy_profile(game, blueprint);
  const auto gadget = build_safe_resolving_gadget(game, definition, boundary);
  if (!blueprint_validation || !gadget) {
    return Result<StrategyProfile, SolverError>::failure(
        !blueprint_validation ? blueprint_validation.error() : gadget.error());
  }
  const auto gadget_validation = validate_strategy_profile(gadget.value(), gadget_strategy);
  if (!gadget_validation) {
    return Result<StrategyProfile, SolverError>::failure(gadget_validation.error());
  }

  std::set<std::string> resolving_information_sets;
  std::vector<GameNodeId> pending(definition.roots.begin(), definition.roots.end());
  std::set<GameNodeId> visited;
  while (!pending.empty()) {
    const auto node_id = pending.back();
    pending.pop_back();
    if (!visited.insert(node_id).second) {
      continue;
    }
    const auto &node = game.nodes[node_id];
    if (node.kind == GameNodeKind::Decision && node.player == definition.resolving_player) {
      resolving_information_sets.insert(node.information_set);
    }
    for (const auto &edge : node.edges) {
      pending.push_back(edge.child);
    }
  }

  StrategyProfile result = blueprint;
  const std::string resolved_prefix = "resolved:" + definition.public_state_id + ":";
  for (const auto &information_set : resolving_information_sets) {
    const auto resolved = gadget_strategy.find(resolved_prefix + information_set);
    if (resolved == gadget_strategy.end()) {
      return Result<StrategyProfile, SolverError>::failure(SolverError::InvalidStrategy);
    }
    result[information_set] = resolved->second;
  }
  const auto result_validation = validate_strategy_profile(game, result);
  return result_validation
             ? Result<StrategyProfile, SolverError>::success(std::move(result))
             : Result<StrategyProfile, SolverError>::failure(result_validation.error());
}

Result<std::string, SolverError>
serialize_public_subgame_definition(const PublicSubgameDefinition &definition) {
  if (!supported_version(definition.major, definition.minor) ||
      definition.public_state_id.empty() || definition.entry_history.empty() ||
      definition.resolving_player > 1U || definition.roots.empty()) {
    return Result<std::string, SolverError>::failure(SolverError::InvalidSubgame);
  }
  std::ostringstream output;
  output << "GTOSD_PUBLIC_SUBGAME " << definition.major << ' ' << definition.minor << '\n'
         << std::quoted(definition.public_state_id) << '\n'
         << std::quoted(definition.entry_history) << '\n'
         << static_cast<unsigned int>(definition.resolving_player) << '\n'
         << definition.roots.size() << '\n';
  for (const auto root : definition.roots) {
    output << root << '\n';
  }
  return Result<std::string, SolverError>::success(output.str());
}

Result<PublicSubgameDefinition, SolverError>
deserialize_public_subgame_definition(const std::string &serialized) {
  std::istringstream input(serialized);
  PublicSubgameDefinition definition;
  std::string marker;
  unsigned int resolving_player = 0U;
  std::uint64_t root_count = 0U;
  if (!(input >> marker >> definition.major >> definition.minor) ||
      marker != "GTOSD_PUBLIC_SUBGAME") {
    return Result<PublicSubgameDefinition, SolverError>::failure(SolverError::InvalidSubgame);
  }
  if (!supported_version(definition.major, definition.minor)) {
    return Result<PublicSubgameDefinition, SolverError>::failure(
        SolverError::UnsupportedSubgameVersion);
  }
  if (!(input >> std::quoted(definition.public_state_id) >> std::quoted(definition.entry_history) >>
        resolving_player >> root_count) ||
      resolving_player > 1U || root_count == 0U ||
      root_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return Result<PublicSubgameDefinition, SolverError>::failure(SolverError::InvalidSubgame);
  }
  definition.resolving_player = static_cast<std::uint8_t>(resolving_player);
  definition.roots.reserve(static_cast<std::size_t>(root_count));
  for (std::uint64_t index = 0U; index < root_count; ++index) {
    std::uint64_t root = 0U;
    if (!(input >> root) || root > std::numeric_limits<GameNodeId>::max()) {
      return Result<PublicSubgameDefinition, SolverError>::failure(SolverError::InvalidSubgame);
    }
    definition.roots.push_back(static_cast<GameNodeId>(root));
  }
  input >> std::ws;
  if (!input.eof()) {
    return Result<PublicSubgameDefinition, SolverError>::failure(SolverError::InvalidSubgame);
  }
  return Result<PublicSubgameDefinition, SolverError>::success(std::move(definition));
}

Result<std::string, SolverError> serialize_subgame_boundary(const SubgameBoundary &boundary) {
  if (boundary.major != SubgameBoundary::format_major ||
      boundary.minor != SubgameBoundary::format_minor || boundary.game_fingerprint.empty() ||
      boundary.blueprint_fingerprint.empty() || boundary.public_state_id.empty() ||
      boundary.algorithm.empty() || boundary.blueprint_iterations == 0U || boundary.opponent > 1U ||
      boundary.values.empty() || boundary.root_reaches.empty()) {
    return Result<std::string, SolverError>::failure(SolverError::InvalidSubgame);
  }
  std::ostringstream output;
  output << "GTOSD_SUBGAME_BOUNDARY " << boundary.major << ' ' << boundary.minor << '\n'
         << std::quoted(boundary.game_fingerprint) << '\n'
         << std::quoted(boundary.blueprint_fingerprint) << '\n'
         << std::quoted(boundary.public_state_id) << '\n'
         << std::quoted(boundary.algorithm) << '\n'
         << boundary.blueprint_iterations << '\n'
         << static_cast<unsigned int>(boundary.opponent) << '\n'
         << boundary.values.size() << '\n'
         << std::setprecision(std::numeric_limits<double>::max_digits10);
  for (const auto &value : boundary.values) {
    output << std::quoted(value.opponent_information_set) << ' ' << value.counterfactual_reach
           << ' ' << value.blueprint_counterfactual_value << ' '
           << reach_status_token(value.reach_status) << '\n';
  }
  output << boundary.root_reaches.size() << '\n';
  for (const auto &root_reach : boundary.root_reaches) {
    output << root_reach.root << ' ' << std::quoted(root_reach.opponent_information_set) << ' '
           << root_reach.counterfactual_reach << '\n';
  }
  return Result<std::string, SolverError>::success(output.str());
}

Result<SubgameBoundary, SolverError> deserialize_subgame_boundary(const std::string &serialized) {
  std::istringstream input(serialized);
  SubgameBoundary boundary;
  std::string marker;
  unsigned int opponent = 0U;
  std::uint64_t value_count = 0U;
  if (!(input >> marker >> boundary.major >> boundary.minor) ||
      marker != "GTOSD_SUBGAME_BOUNDARY") {
    return Result<SubgameBoundary, SolverError>::failure(SolverError::InvalidSubgame);
  }
  if (boundary.major != SubgameBoundary::format_major ||
      boundary.minor != SubgameBoundary::format_minor) {
    return Result<SubgameBoundary, SolverError>::failure(SolverError::UnsupportedSubgameVersion);
  }
  if (!(input >> std::quoted(boundary.game_fingerprint) >>
        std::quoted(boundary.blueprint_fingerprint) >> std::quoted(boundary.public_state_id) >>
        std::quoted(boundary.algorithm) >> boundary.blueprint_iterations >> opponent >>
        value_count) ||
      opponent > 1U || value_count == 0U ||
      value_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return Result<SubgameBoundary, SolverError>::failure(SolverError::InvalidSubgame);
  }
  boundary.opponent = static_cast<std::uint8_t>(opponent);
  boundary.values.reserve(static_cast<std::size_t>(value_count));
  for (std::uint64_t index = 0U; index < value_count; ++index) {
    SubgameBoundaryValue value;
    std::string status;
    if (!(input >> std::quoted(value.opponent_information_set) >> value.counterfactual_reach >>
          value.blueprint_counterfactual_value >> status)) {
      return Result<SubgameBoundary, SolverError>::failure(SolverError::InvalidSubgame);
    }
    const auto parsed_status = parse_reach_status(status);
    if (!parsed_status) {
      return Result<SubgameBoundary, SolverError>::failure(parsed_status.error());
    }
    value.reach_status = parsed_status.value();
    boundary.values.push_back(std::move(value));
  }
  std::uint64_t root_count = 0U;
  if (!(input >> root_count) || root_count == 0U ||
      root_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return Result<SubgameBoundary, SolverError>::failure(SolverError::InvalidSubgame);
  }
  boundary.root_reaches.reserve(static_cast<std::size_t>(root_count));
  for (std::uint64_t index = 0U; index < root_count; ++index) {
    std::uint64_t root = 0U;
    SubgameRootReach root_reach;
    if (!(input >> root >> std::quoted(root_reach.opponent_information_set) >>
          root_reach.counterfactual_reach) ||
        root > std::numeric_limits<GameNodeId>::max()) {
      return Result<SubgameBoundary, SolverError>::failure(SolverError::InvalidSubgame);
    }
    root_reach.root = static_cast<GameNodeId>(root);
    boundary.root_reaches.push_back(std::move(root_reach));
  }
  input >> std::ws;
  if (!input.eof()) {
    return Result<SubgameBoundary, SolverError>::failure(SolverError::InvalidSubgame);
  }
  return Result<SubgameBoundary, SolverError>::success(std::move(boundary));
}

Result<bool, SolverError> save_public_subgame_definition(const PublicSubgameDefinition &definition,
                                                         const std::string &path) {
  const auto serialized = serialize_public_subgame_definition(definition);
  return serialized
             ? save_wrapped_file(path, wrap_file("GTOSD_PUBLIC_SUBGAME_FILE", serialized.value()))
             : Result<bool, SolverError>::failure(serialized.error());
}

Result<PublicSubgameDefinition, SolverError>
load_public_subgame_definition(const std::string &path) {
  const auto file = load_file(path);
  if (!file) {
    return Result<PublicSubgameDefinition, SolverError>::failure(file.error());
  }
  const auto payload = unwrap_file(file.value(), "GTOSD_PUBLIC_SUBGAME_FILE");
  return payload ? deserialize_public_subgame_definition(payload.value())
                 : Result<PublicSubgameDefinition, SolverError>::failure(payload.error());
}

Result<bool, SolverError> save_subgame_boundary(const SubgameBoundary &boundary,
                                                const std::string &path) {
  const auto serialized = serialize_subgame_boundary(boundary);
  return serialized
             ? save_wrapped_file(path, wrap_file("GTOSD_SUBGAME_BOUNDARY_FILE", serialized.value()))
             : Result<bool, SolverError>::failure(serialized.error());
}

Result<SubgameBoundary, SolverError> load_subgame_boundary(const std::string &path) {
  const auto file = load_file(path);
  if (!file) {
    return Result<SubgameBoundary, SolverError>::failure(file.error());
  }
  const auto payload = unwrap_file(file.value(), "GTOSD_SUBGAME_BOUNDARY_FILE");
  return payload ? deserialize_subgame_boundary(payload.value())
                 : Result<SubgameBoundary, SolverError>::failure(payload.error());
}

Result<SubgameBoundaryByteModel, SolverError>
estimate_subgame_boundary_bytes(const FiniteGame &game, const PublicSubgameDefinition &definition,
                                const SubgameBoundary &boundary) {
  const auto validation = validate_subgame_boundary(game, definition, boundary);
  const auto serialized = serialize_subgame_boundary(boundary);
  if (!validation || !serialized ||
      boundary.values.size() >
          std::numeric_limits<std::uint64_t>::max() / dense_boundary_record_bytes ||
      boundary.root_reaches.size() >
          std::numeric_limits<std::uint64_t>::max() / dense_root_reach_bytes) {
    return Result<SubgameBoundaryByteModel, SolverError>::failure(
        !validation ? validation.error()
                    : (!serialized ? serialized.error() : SolverError::InvalidSubgame));
  }
  SubgameBoundaryByteModel model;
  model.dense_record_bytes =
      static_cast<std::uint64_t>(boundary.values.size()) * dense_boundary_record_bytes;
  const auto root_bytes =
      static_cast<std::uint64_t>(boundary.root_reaches.size()) * dense_root_reach_bytes;
  if (root_bytes > std::numeric_limits<std::uint64_t>::max() - model.dense_record_bytes) {
    return Result<SubgameBoundaryByteModel, SolverError>::failure(SolverError::InvalidSubgame);
  }
  model.dense_record_bytes += root_bytes;
  model.serialized_boundary_bytes = serialized.value().size();
  model.minimum_runtime_bytes = model.dense_record_bytes;
  return Result<SubgameBoundaryByteModel, SolverError>::success(model);
}

const char *boundary_reach_status_name(const BoundaryReachStatus status) noexcept {
  return reach_status_token(status);
}

} // namespace gtosd
