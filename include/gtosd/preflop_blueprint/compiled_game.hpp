#pragma once

#include "gtosd/core/game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/game_model.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Compiled public game tree of the preflop blueprint solver.
//
// The whole game (preflop decisions, every postflop continuation after a
// called preflop sequence, the street transitions and the terminals) is one
// array of nodes in depth-first preorder: the subtree of node i occupies the
// contiguous id range [i, subtree_end(i)). Children follow the order of
// gtosd::legal_actions. Terminal payoffs are settled once with
// settle_terminal for every player and, at showdowns, for every non-empty
// subset of the active players that may win; the trainer only reads them.
// Nothing here depends on private cards or on the board: chance nodes are
// street transitions whose card is supplied by the board sampler of the
// trainer (public chance sampling).
namespace gtosd::preflop_blueprint {

enum class NodeKind : std::uint8_t { Decision, Chance, TerminalFold, TerminalShowdown };

inline constexpr std::uint32_t no_node = 0xFFFF'FFFFU;
inline constexpr std::uint16_t no_entry = 0xFFFFU;
inline constexpr std::uint8_t no_player = 0xFFU;
inline constexpr std::uint64_t no_offset = 0xFFFF'FFFF'FFFF'FFFFULL;
// Fold or check, call, up to four targets or three sizes, all-in.
inline constexpr std::size_t maximum_actions = 8U;

struct CompiledEdge {
  Action action{};
  std::uint32_t child{no_node};
};

struct CompiledNode {
  std::uint32_t id{0U};
  std::uint32_t parent{no_node};
  // Exclusive end of the preorder id range covered by this subtree.
  std::uint32_t subtree_end{0U};
  // First edge in CompiledGame::edges; Chance nodes have exactly one edge.
  std::uint32_t first_edge{0U};
  // First payoff entry in CompiledGame::payoffs; terminals only.
  std::uint32_t payoff_offset{0U};
  // Index into CompiledGame::postflop_entries for every node at or below a
  // postflop entry; no_entry for the preflop part of the tree.
  std::uint16_t postflop_entry{no_entry};
  std::uint16_t depth{0U};
  NodeKind kind{NodeKind::Decision};
  Street street{Street::Preflop};
  // Acting player of a Decision node, no_player otherwise.
  std::uint8_t actor{no_player};
  std::uint8_t action_count{0U};
  std::uint8_t active_mask{0U};
  // Board cards still to be dealt when the node is reached: 5 at a preflop
  // all-in, 2 and 1 at flop and turn all-in runouts, 0 at a river showdown.
  std::uint8_t remaining_board_cards{0U};
  // Aggressive actions already taken on the street when the node is reached.
  AggressionLevel level{0U};
};

struct CompiledGameStats {
  std::uint64_t node_count{0U};
  std::uint64_t edge_count{0U};
  std::uint64_t decision_nodes{0U};
  std::uint64_t chance_nodes{0U};
  std::uint64_t terminal_folds{0U};
  std::uint64_t terminal_showdowns{0U};
  // Preflop part: the nodes whose state is still on the preflop street,
  // including the postflop entries (street-complete chance nodes) and the
  // preflop all-in runouts.
  std::uint64_t preflop_nodes{0U};
  std::uint64_t preflop_decisions{0U};
  std::uint64_t preflop_terminal_folds{0U};
  std::uint64_t preflop_all_in_runouts{0U};
  std::uint64_t postflop_entries{0U};
  // Postflop part, counted as the legacy skeleton did: every entry node plus
  // its whole subtree, summed over the entries.
  std::uint64_t postflop_represented_nodes{0U};
  std::uint64_t postflop_action_edges{0U};
  std::uint64_t postflop_decisions{0U};
  std::array<std::uint64_t, 3> postflop_decisions_by_street{};
  std::uint64_t postflop_chance_frontiers{0U};
  std::uint64_t postflop_terminal_folds{0U};
  std::uint64_t postflop_terminal_showdowns{0U};
  std::uint64_t postflop_terminal_all_in_runouts{0U};
  std::uint32_t maximum_depth{0U};
  std::uint8_t maximum_raise_count{0U};
  double compile_seconds{0.0};
};

struct CompileOptions {
  // Stop at the postflop entries; used to inspect multiway preflop trees
  // before the multiway postflop is sized (roadmap P10).
  bool preflop_only{false};
  std::uint64_t maximum_nodes{50'000'000ULL};
};

class CompiledGame {
public:
  [[nodiscard]] const GameConfig &config() const noexcept { return config_; }
  [[nodiscard]] const std::vector<CompiledNode> &nodes() const noexcept { return nodes_; }
  [[nodiscard]] const std::vector<CompiledEdge> &edges() const noexcept { return edges_; }
  // Public state of every node, kept for verification, export and query.
  [[nodiscard]] const std::vector<PublicState> &states() const noexcept { return states_; }
  [[nodiscard]] const std::vector<std::uint32_t> &postflop_entries() const noexcept {
    return entries_;
  }
  [[nodiscard]] const CompiledGameStats &stats() const noexcept { return stats_; }
  [[nodiscard]] const std::string &fingerprint() const noexcept { return fingerprint_; }
  [[nodiscard]] std::uint32_t root() const noexcept { return 0U; }

  [[nodiscard]] std::span<const CompiledEdge> edges_of(const std::uint32_t node) const noexcept {
    const auto &entry = nodes_[node];
    return std::span<const CompiledEdge>(edges_.data() + entry.first_edge, entry.action_count);
  }
  // Net result of the hand for every seat, in money units, when the actor of
  // a TerminalFold node folded.
  [[nodiscard]] std::span<const std::int64_t> fold_payoffs(std::uint32_t node) const noexcept;
  // Net result for every seat when the players in winner_mask (a non-empty
  // subset of the node's active players) share the pot.
  [[nodiscard]] std::span<const std::int64_t> showdown_payoffs(std::uint32_t node,
                                                                std::uint8_t winner_mask) const noexcept;
  // Number of payoff rows of a showdown terminal: 2^k - 1 for k active players.
  [[nodiscard]] static std::uint32_t showdown_rows(std::uint8_t active_mask) noexcept;
  // Row of winner_mask among the non-empty subsets of active_mask in
  // increasing numeric order.
  [[nodiscard]] static std::uint32_t showdown_row(std::uint8_t active_mask,
                                                  std::uint8_t winner_mask) noexcept;

  [[nodiscard]] static Result<CompiledGame, GameModelError>
  compile(const GameConfig &config, const CompileOptions &options = {});
  // Compiles the subgame rooted at an arbitrary in-progress public state with
  // the postflop action abstraction of the configuration (tests and oracles).
  [[nodiscard]] static Result<CompiledGame, GameModelError>
  compile_subgame(const GameConfig &config, const PublicState &root,
                  const CompileOptions &options = {});

private:
  friend class GameCompiler;

  GameConfig config_{};
  std::vector<CompiledNode> nodes_;
  std::vector<CompiledEdge> edges_;
  std::vector<PublicState> states_;
  std::vector<std::int64_t> payoffs_;
  std::vector<std::uint32_t> entries_;
  CompiledGameStats stats_{};
  std::string fingerprint_;
};

// Contiguous layout of the regret and strategy-sum tables: one row per
// information class (81 preflop hand classes, the bucket capacity of the
// street postflop) and one column per action at every decision node.
struct StateLayout {
  std::uint16_t flop_capacity{0U};
  std::uint16_t turn_capacity{0U};
  std::uint16_t river_capacity{0U};
  // Offset of the first entry of every node; no_offset for non-decisions.
  std::vector<std::uint64_t> offsets;
  std::uint64_t entries{0U};
  std::array<std::uint64_t, 4> entries_by_street{};
  [[nodiscard]] std::uint64_t table_bytes() const noexcept { return entries * sizeof(double); }
  // Regrets and strategy sums together.
  [[nodiscard]] std::uint64_t state_bytes() const noexcept { return 2U * table_bytes(); }
  [[nodiscard]] static std::uint32_t rows_for(Street street, std::uint16_t flop,
                                              std::uint16_t turn, std::uint16_t river) noexcept;
};

[[nodiscard]] StateLayout layout_state(const CompiledGame &game, std::uint16_t flop_capacity,
                                       std::uint16_t turn_capacity, std::uint16_t river_capacity);

[[nodiscard]] const char *node_kind_name(NodeKind kind) noexcept;
[[nodiscard]] const char *street_name(Street street) noexcept;

} // namespace gtosd::preflop_blueprint
