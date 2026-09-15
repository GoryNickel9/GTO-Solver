#include "gtosd/preflop/hu_preflop.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <ranges>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace gtosd {
namespace {

constexpr std::uint64_t fnv_offset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
constexpr double probability_tolerance = 1.0e-12;

PotPercentage percentage(const std::int64_t basis_points) {
  return PotPercentage::from_basis_points(basis_points).value();
}

Money antes(const std::int64_t value) { return Money::from_antes(value).value(); }

bool add_count(std::uint64_t &target, const std::uint64_t value) {
  if (target > std::numeric_limits<std::uint64_t>::max() - value) {
    return false;
  }
  target += value;
  return true;
}

std::uint64_t fnv1a(std::string_view value, std::uint64_t hash = fnv_offset) {
  for (const auto character : value) {
    hash ^= static_cast<std::uint8_t>(character);
    hash *= fnv_prime;
  }
  return hash;
}

std::string hex64(std::uint64_t value) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result(16U, '0');
  for (std::size_t index = 0; index < result.size(); ++index) {
    result[result.size() - 1U - index] = digits[static_cast<std::size_t>(value & 0xFU)];
    value >>= 4U;
  }
  return result;
}

std::string blueprint_fingerprint(const HuPreflopBlueprint &blueprint) {
  std::ostringstream serialized;
  serialized << "gtosd.hu_preflop_blueprint.v1|" << blueprint.major << '|' << blueprint.minor << '|'
             << blueprint.tree_fingerprint << '|' << blueprint.algorithm << '|'
             << blueprint.iterations << '|'
             << std::setprecision(std::numeric_limits<double>::max_digits10);
  for (const auto &decision : blueprint.decisions) {
    serialized << decision.node_id << ':' << static_cast<unsigned>(decision.player) << ':'
               << static_cast<unsigned>(decision.action_count) << ':';
    for (const auto &row : decision.strategy) {
      for (std::size_t action = 0U; action < decision.action_count; ++action) {
        serialized << row[action] << ',';
      }
      serialized << ';';
    }
  }
  return "fnv1a64:" + hex64(fnv1a(serialized.str()));
}

ActionConfig passive_config(const HuPreflopConfig &config) {
  ActionConfig result;
  result.minimum_bet = config.ante;
  return result;
}

ActionConfig all_in_config(const HuPreflopConfig &config) {
  auto result = passive_config(config);
  if (config.include_all_in) {
    result.all_in_mode = AllInMode::Add;
    result.all_in_threshold = percentage(100'000);
  }
  return result;
}

Result<ActionConfig, HuPreflopError> target_config(const HuPreflopConfig &config,
                                                   const PublicState &state,
                                                   const std::vector<Money> &targets,
                                                   const bool allow_incomplete) {
  auto result = all_in_config(config);
  result.raise_depth = maximum_core_raise_depth;
  result.allow_incomplete_non_all_in_raise = allow_incomplete;
  result.aggressive_targets = targets;
  for (const auto target : result.aggressive_targets) {
    if (target <= state.current_bet) {
      return Result<ActionConfig, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
    }
  }
  return Result<ActionConfig, HuPreflopError>::success(std::move(result));
}

Result<ActionConfig, HuPreflopError> stage_config(const HuPreflopConfig &config,
                                                  const PublicState &state,
                                                  const HuPreflopDecisionStage stage) {
  switch (stage) {
  case HuPreflopDecisionStage::Root:
  case HuPreflopDecisionStage::LimpOption:
    return target_config(config, state, {config.open_targets[0], config.open_targets[1]}, false);
  case HuPreflopDecisionStage::FacingSmallOpen:
    return target_config(config, state, {config.response_targets[0]},
                         config.allow_configured_incomplete_raise);
  case HuPreflopDecisionStage::FacingLargeOpen:
    return target_config(config, state, {config.response_targets[1]},
                         config.allow_configured_incomplete_raise);
  case HuPreflopDecisionStage::FinalResponse:
    return Result<ActionConfig, HuPreflopError>::success(all_in_config(config));
  case HuPreflopDecisionStage::FacingAllIn:
    return Result<ActionConfig, HuPreflopError>::success(passive_config(config));
  }
  return Result<ActionConfig, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
}

HuPreflopDecisionStage next_stage(const HuPreflopConfig &config, const HuPreflopDecisionStage stage,
                                  const Action &action, const PublicState &next) {
  if (next.status != HandStatus::InProgress) {
    return stage;
  }
  if (action.type == ActionType::AllIn) {
    return HuPreflopDecisionStage::FacingAllIn;
  }
  if (stage == HuPreflopDecisionStage::Root && action.type == ActionType::Call) {
    return HuPreflopDecisionStage::LimpOption;
  }
  if ((stage == HuPreflopDecisionStage::Root || stage == HuPreflopDecisionStage::LimpOption) &&
      (action.type == ActionType::Raise || action.type == ActionType::Bet)) {
    return next.current_bet == config.open_targets[0] ? HuPreflopDecisionStage::FacingSmallOpen
                                                      : HuPreflopDecisionStage::FacingLargeOpen;
  }
  if ((stage == HuPreflopDecisionStage::FacingSmallOpen ||
       stage == HuPreflopDecisionStage::FacingLargeOpen) &&
      action.type == ActionType::Raise) {
    return HuPreflopDecisionStage::FinalResponse;
  }
  return stage;
}

class PreflopTreeBuilder {
public:
  explicit PreflopTreeBuilder(const HuPreflopConfig &config) { tree_.config = config; }

  Result<HuPreflopTree, HuPreflopError> build() {
    const auto root_state = make_hu_preflop_state(tree_.config.effective_stack, tree_.config.ante);
    if (!root_state) {
      return Result<HuPreflopTree, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    const auto root = expand(root_state.value(), HuPreflopDecisionStage::Root, 0U);
    if (!root) {
      return Result<HuPreflopTree, HuPreflopError>::failure(root.error());
    }
    tree_.root = root.value();
    std::uint64_t hash = fnv1a("gtosd.hu_preflop_tree.v2");
    hash = fnv1a(std::to_string(tree_.config.rake.enabled) + ":" +
                     std::to_string(tree_.config.rake.percentage.basis_points()) + ":" +
                     std::to_string(tree_.config.rake.cap.units()) + ":" +
                     std::to_string(tree_.config.rake.no_flop_no_drop) + ":" +
                     std::to_string(tree_.config.rake.minimum_pot.units()) + ";",
                 hash);
    for (const auto &node : tree_.nodes) {
      hash = fnv1a(serialize_public_state(node.state), hash);
      for (const auto &edge : node.edges) {
        hash = fnv1a(std::to_string(static_cast<unsigned>(edge.action.type)) + ":" +
                         std::to_string(edge.action.amount.units()) + ";",
                     hash);
      }
    }
    tree_.fingerprint = "fnv1a64:" + hex64(hash);
    return Result<HuPreflopTree, HuPreflopError>::success(std::move(tree_));
  }

private:
  Result<std::uint32_t, HuPreflopError>
  expand(const PublicState &state, const HuPreflopDecisionStage stage, const std::uint32_t depth) {
    if (tree_.nodes.size() >= std::numeric_limits<std::uint32_t>::max()) {
      return Result<std::uint32_t, HuPreflopError>::failure(HuPreflopError::NodeOverflow);
    }
    HuPreflopNodeKind kind = HuPreflopNodeKind::Decision;
    if (state.status == HandStatus::Folded) {
      kind = HuPreflopNodeKind::TerminalFold;
    } else if (state.status == HandStatus::AllInRunout) {
      kind = HuPreflopNodeKind::TerminalAllIn;
    } else if (state.status == HandStatus::StreetComplete) {
      kind = HuPreflopNodeKind::PostflopEntry;
    } else if (state.status != HandStatus::InProgress) {
      return Result<std::uint32_t, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }

    const auto id = static_cast<std::uint32_t>(tree_.nodes.size());
    tree_.nodes.push_back(HuPreflopNode{id, kind, stage, state, {}});
    ++tree_.stats.node_count;
    tree_.stats.maximum_depth = std::max(tree_.stats.maximum_depth, depth);
    switch (kind) {
    case HuPreflopNodeKind::Decision:
      ++tree_.stats.decision_nodes;
      break;
    case HuPreflopNodeKind::PostflopEntry:
      ++tree_.stats.postflop_entries;
      return Result<std::uint32_t, HuPreflopError>::success(id);
    case HuPreflopNodeKind::TerminalFold:
      ++tree_.stats.terminal_folds;
      return Result<std::uint32_t, HuPreflopError>::success(id);
    case HuPreflopNodeKind::TerminalAllIn:
      ++tree_.stats.terminal_all_ins;
      return Result<std::uint32_t, HuPreflopError>::success(id);
    }

    const auto action_config = stage_config(tree_.config, state, stage);
    if (!action_config) {
      return Result<std::uint32_t, HuPreflopError>::failure(action_config.error());
    }
    const auto actions = legal_actions(state, action_config.value());
    if (!actions) {
      return Result<std::uint32_t, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    std::vector<HuPreflopEdge> edges;
    edges.reserve(actions.value().size());
    for (const auto &action : actions.value()) {
      const auto child_state = apply_action(state, action, action_config.value());
      if (!child_state) {
        return Result<std::uint32_t, HuPreflopError>::failure(HuPreflopError::GameFailure);
      }
      const auto child_stage = next_stage(tree_.config, stage, action, child_state.value());
      const auto child = expand(child_state.value(), child_stage, depth + 1U);
      if (!child) {
        return Result<std::uint32_t, HuPreflopError>::failure(child.error());
      }
      edges.push_back(HuPreflopEdge{action, child.value()});
      ++tree_.stats.edge_count;
    }
    tree_.nodes[id].edges = std::move(edges);
    return Result<std::uint32_t, HuPreflopError>::success(id);
  }

  HuPreflopTree tree_{};
};

struct SubtreeStats {
  std::uint64_t represented_nodes{0};
  std::uint64_t action_edges{0};
  std::uint64_t decision_nodes{0};
  std::array<std::uint64_t, 3> decision_nodes_by_street{};
  std::uint64_t chance_frontiers{0};
  std::uint64_t terminal_folds{0};
  std::uint64_t terminal_showdowns{0};
  std::uint64_t terminal_all_in_runouts{0};
  std::uint32_t maximum_depth{0};
  std::uint8_t maximum_raise_count{0};
  bool safety_limit_reached{false};
};

bool merge(SubtreeStats &target, const SubtreeStats &source) {
  for (std::size_t street = 0U; street < target.decision_nodes_by_street.size(); ++street) {
    if (!add_count(target.decision_nodes_by_street[street],
                   source.decision_nodes_by_street[street])) {
      return false;
    }
  }
  return add_count(target.represented_nodes, source.represented_nodes) &&
         add_count(target.action_edges, source.action_edges) &&
         add_count(target.decision_nodes, source.decision_nodes) &&
         add_count(target.chance_frontiers, source.chance_frontiers) &&
         add_count(target.terminal_folds, source.terminal_folds) &&
         add_count(target.terminal_showdowns, source.terminal_showdowns) &&
         add_count(target.terminal_all_in_runouts, source.terminal_all_in_runouts);
}

class PostflopSkeletonAnalyzer {
public:
  explicit PostflopSkeletonAnalyzer(const HuPreflopConfig &config) {
    action_config_.aggressive_sizes.assign(config.postflop_sizes.begin(),
                                           config.postflop_sizes.end());
    action_config_.raise_depth = maximum_core_raise_depth;
    action_config_.minimum_bet = config.postflop_minimum_bet;
    if (config.include_all_in) {
      action_config_.all_in_mode = AllInMode::Add;
      action_config_.all_in_threshold = percentage(100'000);
    }
  }

  Result<SubtreeStats, HuPreflopError> analyze(const PublicState &state) {
    const auto key = serialize_public_state(state);
    if (const auto found = memo_.find(key); found != memo_.end()) {
      return Result<SubtreeStats, HuPreflopError>::success(found->second);
    }

    SubtreeStats result;
    result.represented_nodes = 1U;
    result.maximum_raise_count = state.raise_count_this_street;
    result.safety_limit_reached = state.raise_count_this_street == maximum_core_raise_depth;

    if (state.status == HandStatus::Folded) {
      result.terminal_folds = 1U;
    } else if (state.status == HandStatus::Showdown) {
      result.terminal_showdowns = 1U;
    } else if (state.status == HandStatus::AllInRunout) {
      result.terminal_all_in_runouts = 1U;
    } else if (state.status == HandStatus::StreetComplete) {
      ++result.chance_frontiers;
      const auto next = advance_street(state);
      if (!next) {
        return Result<SubtreeStats, HuPreflopError>::failure(HuPreflopError::GameFailure);
      }
      const auto child = analyze(next.value());
      if (!child || !merge(result, child.value())) {
        return Result<SubtreeStats, HuPreflopError>::failure(child ? HuPreflopError::CountOverflow
                                                                   : child.error());
      }
      result.maximum_depth = child.value().maximum_depth + 1U;
      result.maximum_raise_count =
          std::max(result.maximum_raise_count, child.value().maximum_raise_count);
      result.safety_limit_reached =
          result.safety_limit_reached || child.value().safety_limit_reached;
    } else if (state.status == HandStatus::InProgress) {
      ++result.decision_nodes;
      if (state.street < Street::Flop || state.street > Street::River) {
        return Result<SubtreeStats, HuPreflopError>::failure(HuPreflopError::GameFailure);
      }
      ++result.decision_nodes_by_street[static_cast<std::size_t>(state.street) -
                                        static_cast<std::size_t>(Street::Flop)];
      const auto actions = legal_actions(state, action_config_);
      if (!actions || actions.value().empty()) {
        return Result<SubtreeStats, HuPreflopError>::failure(HuPreflopError::GameFailure);
      }
      std::uint32_t maximum_child_depth = 0;
      for (const auto &action : actions.value()) {
        const auto next = apply_action(state, action, action_config_);
        if (!next) {
          return Result<SubtreeStats, HuPreflopError>::failure(HuPreflopError::GameFailure);
        }
        const auto child = analyze(next.value());
        if (!child || !add_count(result.action_edges, 1U) || !merge(result, child.value())) {
          return Result<SubtreeStats, HuPreflopError>::failure(child ? HuPreflopError::CountOverflow
                                                                     : child.error());
        }
        maximum_child_depth = std::max(maximum_child_depth, child.value().maximum_depth);
        result.maximum_raise_count =
            std::max(result.maximum_raise_count, child.value().maximum_raise_count);
        result.safety_limit_reached =
            result.safety_limit_reached || child.value().safety_limit_reached;
      }
      result.maximum_depth = maximum_child_depth + 1U;
    } else {
      return Result<SubtreeStats, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }

    memo_.emplace(key, result);
    return Result<SubtreeStats, HuPreflopError>::success(result);
  }

  [[nodiscard]] std::uint64_t memoized_states() const noexcept { return memo_.size(); }

private:
  ActionConfig action_config_{};
  std::unordered_map<std::string, SubtreeStats> memo_;
};

} // namespace

std::string fingerprint_hu_preflop_blueprint(const HuPreflopBlueprint &blueprint) {
  return blueprint_fingerprint(blueprint);
}

Result<HuPreflopBlueprint, HuPreflopError>
make_uniform_hu_preflop_blueprint(const HuPreflopTree &tree) {
  if (!validate_hu_preflop_config(tree.config) || tree.nodes.empty() ||
      tree.root >= tree.nodes.size()) {
    return Result<HuPreflopBlueprint, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  HuPreflopBlueprint blueprint;
  blueprint.tree_fingerprint = tree.fingerprint;
  blueprint.algorithm = "uniform_policy_test_oracle";
  for (const auto &node : tree.nodes) {
    if (node.kind != HuPreflopNodeKind::Decision || node.edges.empty() ||
        node.edges.size() > hu_preflop_maximum_actions) {
      continue;
    }
    HuPreflopBlueprintDecision decision;
    decision.node_id = node.id;
    decision.player = node.state.player_to_act;
    decision.action_count = static_cast<std::uint8_t>(node.edges.size());
    const auto probability = 1.0 / static_cast<double>(decision.action_count);
    for (auto &row : decision.strategy) {
      std::fill_n(row.begin(), decision.action_count, probability);
    }
    blueprint.decisions.push_back(std::move(decision));
  }
  blueprint.fingerprint = blueprint_fingerprint(blueprint);
  const auto valid = validate_hu_preflop_blueprint(tree, blueprint);
  return valid ? Result<HuPreflopBlueprint, HuPreflopError>::success(std::move(blueprint))
               : Result<HuPreflopBlueprint, HuPreflopError>::failure(valid.error());
}

Result<bool, HuPreflopError> validate_hu_preflop_blueprint(const HuPreflopTree &tree,
                                                           const HuPreflopBlueprint &blueprint) {
  if (blueprint.major != HuPreflopBlueprint::format_major ||
      blueprint.minor != HuPreflopBlueprint::format_minor ||
      blueprint.tree_fingerprint != tree.fingerprint || blueprint.algorithm.empty() ||
      blueprint.fingerprint.empty() || blueprint.fingerprint != blueprint_fingerprint(blueprint)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  std::map<std::uint32_t, const HuPreflopBlueprintDecision *> decisions;
  for (const auto &decision : blueprint.decisions) {
    if (decision.node_id >= tree.nodes.size() || decision.player > 1U ||
        decision.action_count == 0U || decision.action_count > hu_preflop_maximum_actions ||
        !decisions.emplace(decision.node_id, &decision).second) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
    }
    const auto &node = tree.nodes[decision.node_id];
    if (node.kind != HuPreflopNodeKind::Decision || node.state.player_to_act != decision.player ||
        node.edges.size() != decision.action_count) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
    }
    for (const auto &row : decision.strategy) {
      double total = 0.0;
      for (std::size_t action = 0U; action < row.size(); ++action) {
        if (!std::isfinite(row[action]) || row[action] < 0.0 ||
            (action >= decision.action_count && row[action] != 0.0)) {
          return Result<bool, HuPreflopError>::failure(HuPreflopError::NumericalFailure);
        }
        if (action < decision.action_count) {
          total += row[action];
        }
      }
      if (std::abs(total - 1.0) > probability_tolerance) {
        return Result<bool, HuPreflopError>::failure(HuPreflopError::NumericalFailure);
      }
    }
  }
  for (const auto &node : tree.nodes) {
    if (node.kind == HuPreflopNodeKind::Decision && !decisions.contains(node.id)) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
    }
  }
  return Result<bool, HuPreflopError>::success(true);
}

HuPreflopConfig make_hu_co40_benchmark_config() {
  HuPreflopConfig config;
  config.effective_stack = antes(40);
  config.ante = antes(1);
  config.open_targets = {antes(6), antes(10)};
  config.response_targets = {Money::from_units(105'000).value(),
                             Money::from_units(145'000).value()};
  config.postflop_sizes = {percentage(3'300), percentage(6'600), percentage(12'000)};
  config.postflop_minimum_bet = antes(1);
  config.include_all_in = true;
  config.allow_configured_incomplete_raise = true;
  return config;
}

Result<bool, HuPreflopError> validate_hu_preflop_config(const HuPreflopConfig &config) {
  if (config.effective_stack.units() <= 0 || config.ante.units() <= 0 ||
      config.postflop_minimum_bet.units() <= 0 || !config.include_all_in ||
      config.open_targets[0] <= config.ante || config.open_targets[1] <= config.open_targets[0] ||
      config.response_targets[0] <= config.open_targets[0] ||
      config.response_targets[1] <= config.open_targets[1] ||
      config.response_targets[0] >= config.effective_stack ||
      config.response_targets[1] >= config.effective_stack ||
      (config.rake.enabled &&
       (config.rake.percentage.basis_points() == 0U || config.rake.cap.units() <= 0))) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  const auto root = make_hu_preflop_state(config.effective_stack, config.ante);
  if (!root) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  return Result<bool, HuPreflopError>::success(true);
}

Result<HuPreflopTree, HuPreflopError> build_hu_preflop_tree(const HuPreflopConfig &config) {
  if (!validate_hu_preflop_config(config)) {
    return Result<HuPreflopTree, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  return PreflopTreeBuilder(config).build();
}

Result<HuPostflopPublicStats, HuPreflopError>
analyze_hu_postflop_public_skeleton(const HuPreflopTree &tree) {
  if (!validate_hu_preflop_config(tree.config)) {
    return Result<HuPostflopPublicStats, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  PostflopSkeletonAnalyzer analyzer(tree.config);
  SubtreeStats aggregate;
  for (const auto &node : tree.nodes) {
    if (node.kind != HuPreflopNodeKind::PostflopEntry) {
      continue;
    }
    const auto subtree = analyzer.analyze(node.state);
    if (!subtree || !merge(aggregate, subtree.value())) {
      return Result<HuPostflopPublicStats, HuPreflopError>::failure(
          subtree ? HuPreflopError::CountOverflow : subtree.error());
    }
    aggregate.maximum_depth = std::max(aggregate.maximum_depth, subtree.value().maximum_depth);
    aggregate.maximum_raise_count =
        std::max(aggregate.maximum_raise_count, subtree.value().maximum_raise_count);
    aggregate.safety_limit_reached =
        aggregate.safety_limit_reached || subtree.value().safety_limit_reached;
  }

  HuPostflopPublicStats result;
  result.represented_nodes = aggregate.represented_nodes;
  result.action_edges = aggregate.action_edges;
  result.decision_nodes = aggregate.decision_nodes;
  result.decision_nodes_by_street = aggregate.decision_nodes_by_street;
  result.chance_frontiers = aggregate.chance_frontiers;
  result.terminal_folds = aggregate.terminal_folds;
  result.terminal_showdowns = aggregate.terminal_showdowns;
  result.terminal_all_in_runouts = aggregate.terminal_all_in_runouts;
  result.memoized_states = analyzer.memoized_states();
  result.maximum_subtree_depth = aggregate.maximum_depth;
  result.maximum_observed_raise_count = aggregate.maximum_raise_count;
  result.core_raise_safety_limit_reached = aggregate.safety_limit_reached;
  const auto maximum_full_raises =
      tree.config.effective_stack.units() / tree.config.postflop_minimum_bet.units();
  result.natural_stack_termination_proven =
      maximum_full_raises <= maximum_core_raise_depth && !aggregate.safety_limit_reached;
  return Result<HuPostflopPublicStats, HuPreflopError>::success(result);
}

const char *hu_preflop_error_name(const HuPreflopError error) noexcept {
  switch (error) {
  case HuPreflopError::InvalidConfiguration:
    return "invalid_configuration";
  case HuPreflopError::GameFailure:
    return "game_failure";
  case HuPreflopError::NodeOverflow:
    return "node_overflow";
  case HuPreflopError::CountOverflow:
    return "count_overflow";
  case HuPreflopError::EquityFailure:
    return "equity_failure";
  case HuPreflopError::NumericalFailure:
    return "numerical_failure";
  case HuPreflopError::MemoryFailure:
    return "memory_failure";
  case HuPreflopError::IoFailure:
    return "io_failure";
  case HuPreflopError::IntegrityFailure:
    return "integrity_failure";
  case HuPreflopError::UnsupportedVersion:
    return "unsupported_version";
  }
  return "unknown";
}

} // namespace gtosd
