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
  PublicEdgeKind kind{PublicEdgeKind::Action};
  NodeId child{0};
  Action action{};
  CardId chance_card{};
  std::uint32_t physical_outcome_count{0};
  std::uint32_t total_legal_outcome_count{0};
};

struct PublicTreeNode {
  NodeId id{0};
  PublicNodeKind kind{PublicNodeKind::Decision};
  PublicState state{};
  std::uint32_t depth{0};
  std::vector<PublicTreeEdge> edges;
};

struct PublicTreeStats {
  std::uint64_t node_count{0};
  std::uint64_t edge_count{0};
  std::uint64_t decision_nodes{0};
  std::uint64_t chance_nodes{0};
  std::uint64_t terminal_fold_nodes{0};
  std::uint64_t terminal_showdown_nodes{0};
  std::uint64_t chance_edges{0};
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
