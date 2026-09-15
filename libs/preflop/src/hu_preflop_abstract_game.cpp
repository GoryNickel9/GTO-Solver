#include "gtosd/preflop/hu_preflop_abstract_game.hpp"

#include "gtosd/equity/showdown.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <new>
#include <numeric>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace gtosd {
namespace {

constexpr std::uint64_t fnv_offset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;

void mix_byte(std::uint64_t &hash, const std::uint8_t value) noexcept {
  hash ^= value;
  hash *= fnv_prime;
}

template <typename Unsigned> void mix_unsigned(std::uint64_t &hash, Unsigned value) noexcept {
  static_assert(std::is_unsigned_v<Unsigned>);
  for (std::size_t byte = 0U; byte < sizeof(Unsigned); ++byte) {
    mix_byte(hash, static_cast<std::uint8_t>(value & 0xFFU));
    value >>= 8U;
  }
}

void mix_string(std::uint64_t &hash, const std::string &value) noexcept {
  mix_unsigned(hash, static_cast<std::uint64_t>(value.size()));
  for (const auto character : value) {
    mix_byte(hash, static_cast<std::uint8_t>(character));
  }
}

std::string finish_fingerprint(const std::uint64_t hash) {
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

std::uint64_t splitmix64(std::uint64_t value) noexcept {
  value += 0x9E37'79B9'7F4A'7C15ULL;
  value = (value ^ (value >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D0'49BB'1331'11EBULL;
  return value ^ (value >> 31U);
}

std::string action_label(const Action &action) {
  std::string label;
  switch (action.type) {
  case ActionType::Fold:
    label = "fold";
    break;
  case ActionType::Check:
    label = "check";
    break;
  case ActionType::Call:
    label = "call";
    break;
  case ActionType::Bet:
    label = "bet";
    break;
  case ActionType::Raise:
    label = "raise";
    break;
  case ActionType::AllIn:
    label = "all_in";
    break;
  }
  return label + "_" + std::to_string(action.amount.units()) + "u_" +
         std::to_string(action.requested_basis_points) + "bp";
}

class BoundedGameBuilder {
public:
  explicit BoundedGameBuilder(const std::uint64_t maximum_nodes) : maximum_nodes_(maximum_nodes) {}

  Result<GameNodeId, HuPreflopError> append(GameNode node) {
    if (nodes_.size() >= maximum_nodes_ ||
        nodes_.size() >= std::numeric_limits<GameNodeId>::max()) {
      return Result<GameNodeId, HuPreflopError>::failure(HuPreflopError::NodeOverflow);
    }
    const auto id = static_cast<GameNodeId>(nodes_.size());
    nodes_.push_back(std::move(node));
    return Result<GameNodeId, HuPreflopError>::success(id);
  }

  std::vector<GameNode> finish() { return std::move(nodes_); }

private:
  std::uint64_t maximum_nodes_{0U};
  std::vector<GameNode> nodes_;
};

class AbstractFiniteGameCompiler {
public:
  AbstractFiniteGameCompiler(const HuPreflopTree &tree,
                             const HuPreflopAbstractGameDefinition &definition,
                             const HuPreflopAbstractChanceCorpus &corpus,
                             const std::uint64_t maximum_nodes)
      : tree_(tree), definition_(definition), corpus_(corpus), builder_(maximum_nodes),
        combos_(all_combos()) {
    postflop_actions_.aggressive_sizes.assign(tree.config.postflop_sizes.begin(),
                                              tree.config.postflop_sizes.end());
    postflop_actions_.raise_depth = maximum_core_raise_depth;
    postflop_actions_.minimum_bet = tree.config.postflop_minimum_bet;
    if (tree.config.include_all_in) {
      postflop_actions_.all_in_mode = AllInMode::Add;
      postflop_actions_.all_in_threshold = PotPercentage::from_basis_points(100'000).value();
    }
    policy_contract_.minor = HuPreflopSampledPostflopPolicy::format_minor;
    policy_contract_.tree_fingerprint = tree.fingerprint;
    policy_contract_.algorithm = "finite_abstract_game_compiler_v1";
    policy_contract_.abstraction_id = definition.abstraction_id;
    policy_contract_.iterations = 1U;
    policy_contract_.seed = corpus.seed;
    policy_contract_.partition_seed = definition.partition_seed;
    policy_contract_.equity_samples_per_bucket = definition.equity_samples_per_bucket;
    policy_contract_.distributional_bucket_capacities = definition.distributional_bucket_capacities;
    policy_contract_.representation = definition.representation;
  }

  Result<FiniteGame, HuPreflopError> compile() {
    std::vector<GameEdge> deal_edges;
    try {
      deal_edges.reserve(corpus_.outcomes.size());
    } catch (const std::bad_alloc &) {
      return Result<FiniteGame, HuPreflopError>::failure(HuPreflopError::MemoryFailure);
    }
    for (std::size_t outcome = 0U; outcome < corpus_.outcomes.size(); ++outcome) {
      const auto child = build_preflop(tree_.root, corpus_.outcomes[outcome].deal);
      if (!child) {
        return Result<FiniteGame, HuPreflopError>::failure(child.error());
      }
      deal_edges.push_back({{static_cast<GameActionId>(outcome), "deal_" + std::to_string(outcome)},
                            child.value(),
                            corpus_.outcomes[outcome].probability});
    }
    GameNode root;
    root.kind = GameNodeKind::Chance;
    root.edges = std::move(deal_edges);
    const auto root_id = builder_.append(std::move(root));
    if (!root_id) {
      return Result<FiniteGame, HuPreflopError>::failure(root_id.error());
    }
    const auto initial_pot = static_cast<double>(tree_.nodes[tree_.root].state.pot.units()) /
                             static_cast<double>(Money::units_per_ante);
    FiniteGame game{"hu_preflop_v23_empirical_chance|" + definition_.fingerprint + "|" +
                        corpus_.fingerprint,
                    root_id.value(), builder_.finish(), initial_pot};
    return Result<FiniteGame, HuPreflopError>::success(std::move(game));
  }

private:
  ComboId combo_id(const std::array<CardId, 2> &hole) const {
    const auto mask = hole[0].mask() | hole[1].mask();
    for (std::size_t index = 0U; index < combos_.size(); ++index) {
      if ((combos_[index].first.mask() | combos_[index].second.mask()) == mask) {
        return static_cast<ComboId>(index);
      }
    }
    return static_cast<ComboId>(combos_.size());
  }

  Result<GameNodeId, HuPreflopError> terminal(const PublicState &state,
                                              const HuPreflopRootConditionalDeal &deal) {
    std::uint8_t winner_mask = 0U;
    if (state.status != HandStatus::Folded) {
      const std::vector<std::array<CardId, 2>> holes{deal.holes[0], deal.holes[1]};
      const std::vector<CardId> board(deal.board.begin(), deal.board.end());
      const auto showdown = evaluate_showdown(holes, board);
      if (!showdown) {
        return Result<GameNodeId, HuPreflopError>::failure(HuPreflopError::EquityFailure);
      }
      winner_mask = showdown.value().winner_mask;
    }
    const auto settlement = settle_terminal(state, tree_.config.rake, winner_mask);
    if (!settlement) {
      return Result<GameNodeId, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    GameNode node;
    node.kind = GameNodeKind::Terminal;
    for (std::size_t player = 0U; player < node.payoff.size(); ++player) {
      node.payoff[player] = static_cast<double>(settlement.value().payoff_units[player]) /
                            static_cast<double>(Money::units_per_ante);
    }
    return builder_.append(std::move(node));
  }

  Result<PublicState, HuPreflopError>
  advance_with_board(const PublicState &state, const HuPreflopRootConditionalDeal &deal) const {
    const auto advanced = advance_street(state);
    if (!advanced) {
      return Result<PublicState, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    auto next = advanced.value();
    if (next.street == Street::Flop) {
      next.board_mask |= deal.board[0].mask() | deal.board[1].mask() | deal.board[2].mask();
    } else if (next.street == Street::Turn) {
      next.board_mask |= deal.board[3].mask();
    } else if (next.street == Street::River) {
      next.board_mask |= deal.board[4].mask();
    } else {
      return Result<PublicState, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    return validate_state(next)
               ? Result<PublicState, HuPreflopError>::success(next)
               : Result<PublicState, HuPreflopError>::failure(HuPreflopError::GameFailure);
  }

  std::string postflop_history_descriptor(const std::uint32_t entry_node, const PublicState &state,
                                          const std::vector<Action> &history) const {
    std::string output =
        std::to_string(entry_node) + ":s" + std::to_string(static_cast<std::uint8_t>(state.street));
    for (const auto &action : history) {
      output += ":" + action_label(action);
    }
    return output;
  }

  Result<std::string, HuPreflopError>
  postflop_information_set(const std::uint32_t entry_node, const PublicState &state,
                           const HuPreflopRootConditionalDeal &deal,
                           const std::vector<Action> &history) {
    const auto decision = derive_hu_preflop_sampled_postflop_public_decision(
        tree_, entry_node, deal.board, state, std::span<const Action>(history));
    if (!decision) {
      return Result<std::string, HuPreflopError>::failure(decision.error());
    }
    const auto descriptor = postflop_history_descriptor(entry_node, state, history);
    const auto [found, inserted] =
        public_history_descriptors_.emplace(decision.value().public_history, descriptor);
    if (!inserted && found->second != descriptor) {
      return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    const auto combo = combo_id(deal.holes[state.player_to_act]);
    if (combo >= combos_.size()) {
      return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    const auto key = derive_hu_preflop_sampled_postflop_policy_key(
        policy_contract_, decision.value(), deal.board, combo);
    if (!key) {
      return Result<std::string, HuPreflopError>::failure(key.error());
    }
    std::ostringstream output;
    output << "v23:p" << static_cast<unsigned>(key.value().player) << ":s"
           << static_cast<unsigned>(key.value().street) << ":h" << key.value().public_history
           << ":c" << static_cast<unsigned>(key.value().preflop_class) << ":b";
    for (const auto bucket : key.value().bucket_history) {
      output << bucket << ',';
    }
    return Result<std::string, HuPreflopError>::success(output.str());
  }

  Result<GameNodeId, HuPreflopError> build_postflop(const std::uint32_t entry_node,
                                                    const PublicState &state,
                                                    const HuPreflopRootConditionalDeal &deal,
                                                    std::vector<Action> &history) {
    if (state.status == HandStatus::Folded || state.status == HandStatus::AllInRunout ||
        state.status == HandStatus::Showdown) {
      return terminal(state, deal);
    }
    if (state.status == HandStatus::StreetComplete) {
      const auto next = advance_with_board(state, deal);
      if (!next) {
        return Result<GameNodeId, HuPreflopError>::failure(next.error());
      }
      const auto child = build_postflop(entry_node, next.value(), deal, history);
      if (!child) {
        return Result<GameNodeId, HuPreflopError>::failure(child.error());
      }
      GameNode chance;
      chance.kind = GameNodeKind::Chance;
      chance.edges.push_back(
          {{0U, "reveal_" + std::to_string(static_cast<std::uint8_t>(next.value().street))},
           child.value(),
           1.0});
      return builder_.append(std::move(chance));
    }
    if (state.status != HandStatus::InProgress) {
      return Result<GameNodeId, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    const auto information_set = postflop_information_set(entry_node, state, deal, history);
    const auto actions = legal_actions(state, postflop_actions_);
    if (!information_set || !actions || actions.value().empty() ||
        actions.value().size() > hu_preflop_sampled_postflop_maximum_actions) {
      return Result<GameNodeId, HuPreflopError>::failure(
          !information_set ? information_set.error() : HuPreflopError::GameFailure);
    }
    std::vector<GameEdge> edges;
    edges.reserve(actions.value().size());
    for (std::size_t action = 0U; action < actions.value().size(); ++action) {
      const auto next = apply_action(state, actions.value()[action], postflop_actions_);
      if (!next) {
        return Result<GameNodeId, HuPreflopError>::failure(HuPreflopError::GameFailure);
      }
      history.push_back(actions.value()[action]);
      const auto child = build_postflop(entry_node, next.value(), deal, history);
      history.pop_back();
      if (!child) {
        return Result<GameNodeId, HuPreflopError>::failure(child.error());
      }
      edges.push_back({{static_cast<GameActionId>(action), action_label(actions.value()[action])},
                       child.value(),
                       0.0});
    }
    GameNode decision;
    decision.kind = GameNodeKind::Decision;
    decision.player = state.player_to_act;
    decision.information_set = information_set.value();
    decision.edges = std::move(edges);
    return builder_.append(std::move(decision));
  }

  Result<GameNodeId, HuPreflopError> build_preflop(const std::uint32_t node_id,
                                                   const HuPreflopRootConditionalDeal &deal) {
    if (node_id >= tree_.nodes.size()) {
      return Result<GameNodeId, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    const auto &node = tree_.nodes[node_id];
    if (node.kind == HuPreflopNodeKind::TerminalFold ||
        node.kind == HuPreflopNodeKind::TerminalAllIn) {
      return terminal(node.state, deal);
    }
    if (node.kind == HuPreflopNodeKind::PostflopEntry) {
      std::vector<Action> history;
      return build_postflop(node.id, node.state, deal, history);
    }
    if (node.kind != HuPreflopNodeKind::Decision || node.edges.empty()) {
      return Result<GameNodeId, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    std::vector<GameEdge> edges;
    edges.reserve(node.edges.size());
    for (std::size_t action = 0U; action < node.edges.size(); ++action) {
      const auto child = build_preflop(node.edges[action].child, deal);
      if (!child) {
        return Result<GameNodeId, HuPreflopError>::failure(child.error());
      }
      edges.push_back({{static_cast<GameActionId>(action), action_label(node.edges[action].action)},
                       child.value(),
                       0.0});
    }
    const auto actor = node.state.player_to_act;
    const auto actor_class = hand_class(Combo{deal.holes[actor][0], deal.holes[actor][1]});
    GameNode decision;
    decision.kind = GameNodeKind::Decision;
    decision.player = actor;
    decision.information_set = "v23:pre:n" + std::to_string(node.id) + ":p" +
                               std::to_string(actor) + ":c" + std::to_string(actor_class);
    decision.edges = std::move(edges);
    return builder_.append(std::move(decision));
  }

  const HuPreflopTree &tree_;
  const HuPreflopAbstractGameDefinition &definition_;
  const HuPreflopAbstractChanceCorpus &corpus_;
  BoundedGameBuilder builder_;
  std::array<Combo, 630> combos_{};
  ActionConfig postflop_actions_{};
  HuPreflopSampledPostflopPolicy policy_contract_{};
  std::unordered_map<std::uint64_t, std::string> public_history_descriptors_;
};

} // namespace

std::string
fingerprint_hu_preflop_abstract_chance_corpus(const HuPreflopAbstractChanceCorpus &corpus) {
  auto hash = fnv_offset;
  mix_string(hash, "gtosd.hu_preflop_abstract_chance_corpus.v1");
  mix_unsigned(hash, corpus.major);
  mix_unsigned(hash, corpus.minor);
  mix_string(hash, corpus.abstract_game_definition_fingerprint);
  mix_unsigned(hash, corpus.seed);
  mix_unsigned(hash, corpus.deals_per_co_class);
  mix_unsigned(hash, static_cast<std::uint64_t>(corpus.outcomes.size()));
  for (const auto &outcome : corpus.outcomes) {
    for (const auto &hole : outcome.deal.holes) {
      for (const auto card : hole) {
        mix_unsigned(hash, card.value());
      }
    }
    for (const auto card : outcome.deal.board) {
      mix_unsigned(hash, card.value());
    }
    mix_unsigned(hash, std::bit_cast<std::uint64_t>(outcome.probability));
  }
  return finish_fingerprint(hash);
}

Result<HuPreflopCompiledAbstractGame, HuPreflopError>
compile_hu_preflop_abstract_game(const HuPreflopTree &tree, const HuPreflopSolveOptions &options,
                                 const std::uint32_t deals_per_co_class,
                                 const std::uint64_t corpus_seed,
                                 const std::uint64_t maximum_nodes) {
  if (deals_per_co_class == 0U || corpus_seed == 0U || maximum_nodes == 0U) {
    return Result<HuPreflopCompiledAbstractGame, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto definition = make_hu_preflop_abstract_game_definition(tree, options);
  if (!definition) {
    return Result<HuPreflopCompiledAbstractGame, HuPreflopError>::failure(definition.error());
  }
  const auto outcome_count =
      static_cast<std::uint64_t>(hu_preflop_hand_class_count) * deals_per_co_class;
  const auto public_nodes_per_deal = tree.stats.node_count - tree.stats.postflop_entries +
                                     definition.value().postflop_public_tree.represented_nodes;
  if (outcome_count > std::numeric_limits<GameNodeId>::max() ||
      public_nodes_per_deal > (maximum_nodes - 1U) / outcome_count) {
    return Result<HuPreflopCompiledAbstractGame, HuPreflopError>::failure(
        HuPreflopError::NodeOverflow);
  }

  HuPreflopAbstractChanceCorpus corpus;
  corpus.abstract_game_definition_fingerprint = definition.value().fingerprint;
  corpus.seed = corpus_seed;
  corpus.deals_per_co_class = deals_per_co_class;
  try {
    corpus.outcomes.reserve(static_cast<std::size_t>(outcome_count));
    for (HandClassId hand_class_id = 0U; hand_class_id < hu_preflop_hand_class_count;
         ++hand_class_id) {
      auto class_seed = splitmix64(corpus_seed ^ static_cast<std::uint64_t>(hand_class_id));
      if (class_seed == 0U) {
        class_seed = 1U;
      }
      const auto deals = sample_hu_preflop_root_conditional_deals(hand_class_id, 0U,
                                                                  deals_per_co_class, class_seed);
      if (!deals) {
        return Result<HuPreflopCompiledAbstractGame, HuPreflopError>::failure(deals.error());
      }
      const auto probability = static_cast<double>(class_mass(hand_class_id)) /
                               (630.0 * static_cast<double>(deals_per_co_class));
      for (const auto &deal : deals.value()) {
        corpus.outcomes.push_back({deal, probability});
      }
    }
  } catch (const std::bad_alloc &) {
    return Result<HuPreflopCompiledAbstractGame, HuPreflopError>::failure(
        HuPreflopError::MemoryFailure);
  }
  const auto probability_sum = std::accumulate(
      corpus.outcomes.begin(), corpus.outcomes.end(), 0.0,
      [](const double total, const auto &outcome) { return total + outcome.probability; });
  if (!std::isfinite(probability_sum) || std::abs(probability_sum - 1.0) > 1.0e-12) {
    return Result<HuPreflopCompiledAbstractGame, HuPreflopError>::failure(
        HuPreflopError::NumericalFailure);
  }
  corpus.fingerprint = fingerprint_hu_preflop_abstract_chance_corpus(corpus);

  try {
    AbstractFiniteGameCompiler compiler(tree, definition.value(), corpus, maximum_nodes);
    auto game = compiler.compile();
    if (!game) {
      return Result<HuPreflopCompiledAbstractGame, HuPreflopError>::failure(game.error());
    }
    const auto summary = validate_finite_game(game.value());
    if (!summary) {
      return Result<HuPreflopCompiledAbstractGame, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    HuPreflopCompiledAbstractGame result;
    result.definition = definition.value();
    result.chance_corpus = std::move(corpus);
    result.game = std::move(game.value());
    result.summary = summary.value();
    return Result<HuPreflopCompiledAbstractGame, HuPreflopError>::success(std::move(result));
  } catch (const std::bad_alloc &) {
    return Result<HuPreflopCompiledAbstractGame, HuPreflopError>::failure(
        HuPreflopError::MemoryFailure);
  }
}

} // namespace gtosd
