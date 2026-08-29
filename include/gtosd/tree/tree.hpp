#pragma once

#include "gtosd/equity/showdown.hpp"
#include "gtosd/tree/config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gtosd {

using NodeId = std::uint64_t;

enum class PublicNodeKind : std::uint8_t { Decision, Chance, TerminalFold, TerminalShowdown };

enum class PublicEdgeKind : std::uint8_t { Action, ChanceCard };

enum class TreeError : std::uint8_t {
  InvalidConfiguration,
  InvalidBoard,
  BuildLimitExceeded,
  NodeOverflow,
  GameFailure,
  InvalidNode,
  NotChanceNode,
  NotShowdownTerminal,
  InvalidPrivateCards,
  EquityFailure,
  SettlementFailure
};

struct PublicTreeEdge {
  struct ChanceOutcomeMapping {
    CardId card{};
    std::uint8_t physical_to_representative_permutation{0};
  };

  PublicEdgeKind kind{PublicEdgeKind::Action};
  NodeId child{0};
  Action action{};
  CardId chance_card{};
  std::uint32_t physical_outcome_count{0};
  std::uint32_t total_legal_outcome_count{0};
  std::array<ChanceOutcomeMapping, 4> chance_outcomes{};
  std::uint8_t chance_outcome_count{0};
};

struct PublicTreeNode {
  NodeId id{0};
  PublicNodeKind kind{PublicNodeKind::Decision};
  PublicState state{};
  std::uint32_t depth{0};
  std::vector<PublicTreeEdge> edges;
};

struct PublicTreeStats {
  static constexpr std::size_t decision_action_bucket_count = 16U;

  std::uint64_t node_count{0};
  std::array<std::uint64_t, 3> node_count_by_street{};
  std::uint64_t edge_count{0};
  std::array<std::uint64_t, 3> edge_count_by_street{};
  std::uint64_t decision_nodes{0};
  std::array<std::uint64_t, 3> decision_nodes_by_street{};
  std::uint64_t chance_nodes{0};
  std::array<std::uint64_t, 3> chance_nodes_by_street{};
  std::uint64_t terminal_fold_nodes{0};
  std::uint64_t terminal_showdown_nodes{0};
  std::uint64_t chance_edges{0};
  std::array<std::uint64_t, 3> chance_edges_by_street{};
  std::array<std::uint64_t, 3> action_edges_by_street{};
  // Exact decision-shape histogram used by layout-only compilers.  The last
  // index is the legal action count; bucket zero is intentionally unused.
  std::array<std::array<std::array<std::uint64_t, decision_action_bucket_count>, 2>, 3>
      decision_nodes_by_street_player_action{};
  std::uint32_t maximum_depth{0};
  std::uint64_t estimated_eager_bytes{0};
};

struct PublicTree {
  static constexpr std::uint32_t format_major = 1;
  static constexpr std::uint32_t format_minor = 0;

  PostflopTreeConfig config{};
  NodeId root{0};
  std::vector<PublicTreeNode> nodes;
  PublicTreeStats stats{};
  std::string betting_tree_hash;
};

struct TreeBuildOptions {
  std::uint64_t maximum_nodes{20'000'000};
  std::uint64_t reserve_nodes{0};
  // Optional range-preserving suit permutations. Empty keeps the physical
  // chance tree. Non-empty builds only orbit representatives and records all
  // omitted physical cards on each chance edge.
  std::vector<std::array<std::uint8_t, 4>> canonical_chance_permutations;
};

struct ConditionedChanceEdge {
  NodeId child{0};
  CardId card{};
  std::uint32_t physical_outcome_count{1};
  std::uint32_t total_legal_outcome_count{0};
};

struct TerminalResolution {
  ShowdownWinners showdown{};
  Settlement settlement{};
};

[[nodiscard]] Result<PublicTree, TreeError> build_public_tree(const PostflopTreeConfig &config,
                                                              const TreeBuildOptions &options = {});

[[nodiscard]] Result<PublicTreeStats, TreeError>
estimate_public_tree(const PostflopTreeConfig &config, const TreeBuildOptions &options = {});

[[nodiscard]] Result<std::vector<ConditionedChanceEdge>, TreeError>
condition_chance_edges(const PublicTree &tree, NodeId chance_node,
                       const std::vector<CardId> &private_or_dead_cards);

[[nodiscard]] Result<TerminalResolution, TreeError>
resolve_showdown_terminal(const PublicTree &tree, NodeId terminal_node,
                          const std::vector<std::array<CardId, 2>> &hole_cards);

[[nodiscard]] std::string public_tree_hash(const PublicTree &tree);
[[nodiscard]] const char *tree_error_name(TreeError error) noexcept;

} // namespace gtosd
