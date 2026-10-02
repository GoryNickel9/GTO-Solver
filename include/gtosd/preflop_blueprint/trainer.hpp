#pragma once

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/preflop_blueprint/board_context.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/kernels.hpp"
#include "gtosd/preflop_blueprint/traversal.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Vector CFR trainer with public chance sampling (roadmap P6).
//
// One iteration applies the weighting scheme once and updates both players.
// Simultaneous updates share one batch of complete boards. Sampled alternating
// updates draw an independent batch for each player: the second player's
// counterfactual values must be unbiased conditional on the updated opponent
// policy. Exact alternating traversals reuse the complete weighted board list.
// Within a pass the current strategy is read from a snapshot taken from the
// regrets at the start of the pass (Simultaneous: one snapshot per iteration
// for both players, the order of the FiniteGame oracle; Alternating: a fresh
// snapshot before the second player). Regret and strategy-sum cells are
// written directly with the iteration weight: every cell has exactly one
// writer because threads split disjoint subtrees of the public tree and
// boards are processed in order, so the result is bit-identical for any
// thread count.
//
// Storage (milestone "RAM e tempi", 2026-09-21). Two dense tables persist:
// regrets and strategy sums. The current regret-matched policy is no longer a
// third dense table: before every pass the rows touched by the boards of the
// batch are materialized once into a compact per-batch table indexed by
// (decision node, slot), where the slot of a hand on a board is the index of
// its dense row among the distinct rows the batch needs on that street. The
// values equal regret matching on the same regrets at the same moment, so the
// trajectory is bit-identical to the dense snapshot. A fixed-policy evaluator
// (abstract best response) uses the dense layout as its slots.
//
// Contracts (roadmap section 5), per board B of weight w_B and hero hand h:
//   R[n][b(h)][a] += w_B P(h) P(o|h) (v_a[h] - v[h])   v from the P5 kernels
//   S[n][b(h)][a] += w_B P(h) reach_hero[h] sigma(a|n,b(h))
// with P(h) = 1/(live hero hands) and P(o|h) = 1/(live opponent hands
// disjoint from h); in the full game 1/465 and 1/406. Linear multiplies the
// increments of iteration t by t; DCFR alpha/beta/gamma discounts the
// accumulated state once per iteration before the increments.
//
// Preflop lock (TrainerResources::preflop_lock): a locked (preflop node, hand
// class) row plays the given frequencies in every pass. Its row of the compact
// policy is overwritten after regret matching, its regret and strategy-sum
// cells are never written (they stay zero, so no discount changes them) and
// every export (average, current, snapshots, charts) returns the locked row.
// Reaches of both players flow through the locked strategy normally. The lock
// is part of the identity, so a checkpoint cannot resume with another lock.
//
// Three seats (phase 3, PHASE3_SPEC_2026-09-30 sections 3.1-3.3, 3.9, 3.12).
// A 3-player game runs through a separate set of member functions (pass3,
// top_down_reach3, traverse3, terminal3); the heads-up path above is left
// byte-for-byte as it was, so a later fix in one path must be checked against
// the other (spec section 8.4, risk 2). Per board B and hero hand h, with the
// other seats' hands o1 (lower seat) and o2 (higher seat) mutually disjoint
// and disjoint from B:
//   R[n][b(h)][a] += w_B P(h) P(o1,o2|h) (v_a[h] - v[h]),  P(h) = 1/465,
//                    P(o1,o2|h) = 1/(406 * 351)
//   v[h] = sum over ordered pairs of r_lower(o1) r_higher(o2) u_hero(h,o1,o2,B)
// Every iteration makes one pass per hero in seat order (0, 1, 2); in
// alternating mode every hero after the first draws a fresh batch and every
// hero refreshes the compact policy. Each unit and traversal level carries
// three reach vectors; a non-hero decision splits only its actor's vector, so
// a folded seat keeps its frozen reach (its cards are dead). Once the hero
// folds, its value is the constant folded payoff times the joint disjoint
// mass D3 of the other two seats: the shortcut is taken at the hero's own
// decision node before any unit lookup, so the units of hero-inactive
// subtrees are neither prepared nor run (unit_skipped). Preflop terminals come
// from the per-pass class cache (exact over the runouts, scaled to the
// board-conditioned weight) or, in the validation-only board_kernels mode,
// from the multiway kernels on the listed board.
namespace gtosd::card_abstraction {
class ThreeWayTable;
}

namespace gtosd::preflop_blueprint {

class ParallelExecutor;
class PreflopClassCache;

enum class TrainerError : std::uint8_t {
  InvalidConfiguration,
  MissingResource,
  BoardFailure,
  IoFailure,
  IntegrityFailure,
  UnsupportedVersion,
  UnsupportedBoardPrior
};

enum class WeightingScheme : std::uint8_t { Linear, Dcfr };
enum class UpdateMode : std::uint8_t { Simultaneous, Alternating };

// Element type of the two persistent tables. Increments are always computed
// in double; a narrower storage rounds only when the cell is written
// (storage error), never inside a reduction.
enum class TableStorage : std::uint8_t { Double, MixedFloatSums, Float32 };

// Source of the preflop terminal values of the 3-seat path (spec 3.9).
//  - ClassCache (production): every preflop all-in, preflop fold and hero-folded
//    preflop child takes its value from the per-pass class cache, exact over all
//    runouts with folded cards dead, scaled by C(30,5)/C(34,5) so that it is
//    unbiased under the board-conditioned weight.
//  - BoardKernels (validation only, needs TrainerConfig::validation and an
//    exact board list): every preflop showdown (all-ins and checkdown leaves)
//    is evaluated on the listed board, which is its runout, with the multiway
//    kernels; preflop folds use the board-restricted D3.
enum class PreflopTerminals : std::uint8_t { ClassCache, BoardKernels };

struct TrainerConfig {
  std::uint32_t flop_capacity{200U};
  std::uint32_t turn_capacity{500U};
  std::uint32_t river_capacity{1'000U};
  std::uint32_t batch_boards{32U};
  unsigned threads{1U};
  // Physical best response parallelism can differ from the memory-bandwidth
  // limited CFR traversal. Zero reuses `threads`. Excluded from identity.
  unsigned evaluation_threads{0U};
  // Accepted for compatibility with the reference protocol: the policy is
  // always materialized for the rows of the batch only. Excluded from identity.
  bool batch_policy_refresh{true};
  // Software prefetch distances (0 = off): active rows ahead in the policy refresh,
  // hands ahead in the regret update. Prefetches change no value.
  std::uint32_t prefetch_refresh_rows{4U};
  std::uint32_t prefetch_update_hands{8U};
  // Benchmark-only counters and coarse timers. Excluded from identity and
  // disabled in production because even thread-local counters perturb the hot path.
  bool detailed_profile{false};
  // Removed experimental option: the per-batch policy is always rebuilt before
  // a pass, so there is nothing to reuse. Rejected when set.
  bool reuse_discount_invariant_policy{false};
  // Apply DCFR discounts when a row is next read instead of scanning both
  // dense tables every iteration. Checkpoints and exports materialize every
  // row first. This changes only floating-point association, so it is part of
  // the trainer identity and must be validated against eager discounting.
  bool lazy_discount{false};
  // The lazy-discount timestamps are 16-bit slots relative to an epoch base. When the
  // discount target reaches base + lazy_discount_epoch, every touched row is
  // materialized to the target and the base moves there. Runs shorter than the epoch
  // never rebase and are bit-identical to unbounded timestamps; a rebase changes only
  // the association of the discount products (rounding). Range 1..65535. Excluded from
  // the identity: checkpoints are fully materialized, so a resume is valid with any epoch.
  std::uint32_t lazy_discount_epoch{65'535U};
  TableStorage storage{TableStorage::Double};
  WeightingScheme scheme{WeightingScheme::Linear};
  UpdateMode update_mode{UpdateMode::Simultaneous};
  double dcfr_alpha{1.5};
  double dcfr_beta{0.0};
  double dcfr_gamma{2.0};
  std::uint64_t training_seed{0x5452'4149'4e49'4e47ULL};
  std::uint64_t evaluation_seed{0x4556'414c'5541'5445ULL};
  // Nodes per parallel work unit; 0 selects max(256, nodes / 128). The
  // partition never depends on the thread count.
  std::uint32_t partition_target_nodes{0U};

  // ---- 3-seat path (phase 3). A heads-up game ignores these fields unless
  // three_seat_harness is set; with their defaults its identity is unchanged.
  // Preflop terminal source of a 3-player game (see PreflopTerminals). Part of
  // the identity when it is BoardKernels; must stay ClassCache for 2 seats.
  PreflopTerminals preflop_terminals{PreflopTerminals::ClassCache};
  // Diagnostic configurations (BoardKernels, the shortcut switched off) are
  // refused without it. Part of the identity when set.
  bool validation{false};
  // Debug: before each hero pass the value buffers of the units that hero
  // skips are filled with NaN, so a read of a skipped unit becomes visible in
  // the values. Cannot change a correct result; excluded from the identity.
  bool poison_skipped_units{false};
  // Validation only (V6): false walks the subtrees below a hero fold like any
  // other (every unit is prepared and run, and terminals where the hero is
  // inactive pay its folded payoff times D3) instead of the shortcut. Part of
  // the identity when false; needs `validation` and, for 3 players,
  // PreflopTerminals::BoardKernels (in class-cache mode the shortcut of a
  // preflop fold is a board-free class value, equal to the walk only in
  // expectation).
  bool hero_folded_shortcut{true};
  // Diagnostic of V7 ("3-way with a seat that never plays" = heads-up):
  // allowed only on a heads-up game. The game runs through the 3-seat code
  // path with a virtual third seat (seat 2) that never acts, has reach 1 on
  // every live hand, is inactive everywhere (folded cards dead) and never is a
  // hero; the pair weight is 1/(406 * 351). Preflop all-ins use the heads-up
  // pair table (times 351), preflop folds the board D3. Part of the identity.
  bool three_seat_harness{false};
};

// One locked preflop row: the frequencies of the node's edges (edge order, sum
// one) played by every combo of the class. An empty lock locks nothing.
struct PreflopLockRow {
  std::uint32_t node{0U};
  std::uint8_t hand_class{0U};
  std::vector<double> frequencies;
};

struct PreflopLock {
  std::vector<PreflopLockRow> rows;
};

struct TrainerResources {
  const card_abstraction::RankTable *ranks{nullptr};
  const card_abstraction::AllInTable *all_in{nullptr};
  const card_abstraction::BoardCatalog *catalog{nullptr};
  const card_abstraction::BucketTable *flop{nullptr};
  const card_abstraction::BucketTable *turn{nullptr};
  const card_abstraction::BucketTable *river{nullptr};
  // Optional immutable (preflop class, street bucket) map. Must outlive trainer.
  const ClassBucketRows *class_rows{nullptr};
  const HistoryBucketRows *history_rows{nullptr};
  // MonkerSolver-style (board class, per-board bucket) rows. Must outlive trainer.
  const BoardClassRows *board_class_rows{nullptr};
  // Optional fixed preflop rows (see the contract above). Must outlive trainer.
  const PreflopLock *preflop_lock{nullptr};
  // Three-player class table (preflop_three_way_v1.bin, complete): required by a
  // 3-player game with preflop showdowns in PreflopTerminals::ClassCache mode,
  // where the trainer builds its class cache from it at creation. Need not
  // outlive create().
  const card_abstraction::ThreeWayTable *three_way{nullptr};
};

// Exact-mode hook: explicit boards with weights. With sample = false every
// iteration processes all the boards (weights normalized to one); with
// sample = true batches are drawn from the list with probability proportional
// to the weights. An empty list samples physical histories from the catalog.
// Exact exploitability on a restricted list currently requires hand subsets
// whose combos remain live on every board and have a constant number of
// compatible opposing combos. Otherwise the evaluator's fixed preflop deal
// differs from the board-conditioned deal used by this diagnostic trainer;
// estimate_exploitability returns UnsupportedBoardPrior.
struct TrainingBoards {
  std::vector<card_abstraction::BoardHistory> histories;
  std::vector<double> weights;
  bool sample{false};
};

// Exact-mode hook: uniform hand subsets per player (combo ids); empty = all.
struct HandSubsets {
  std::array<std::vector<std::uint16_t>, 2> combos{};
};

struct SubtreePartition {
  std::vector<std::uint32_t> unit_roots;
  // 1 for nodes above the unit roots (processed serially), 0 otherwise.
  std::vector<std::uint8_t> is_top;
  std::uint32_t top_nodes{0U};
  std::uint32_t largest_unit_nodes{0U};
  [[nodiscard]] static SubtreePartition build(const CompiledGame &game,
                                              std::uint32_t target_nodes);
};

// Physical best response with the information structure of the physical game
// (see best_response.hpp): the responder's decisions aggregate over the
// cards still to come. Sampled evaluation draws flops and enumerates their
// runouts.
struct ExploitabilityEstimate {
  std::uint32_t flops{0U};
  std::uint32_t boards{0U};
  bool exact{false};
  // Mean over boards, in antes per hand.
  std::array<double, 2> ev{};
  std::array<double, 2> best_response{};
  std::array<double, 2> gain{};
  // Gain of the average preflop strategy with an exact best response from
  // the flop on (best_response.hpp): unbiased lower bound of the physical
  // gain; gain also selects the preflop choice on the sampled flops and is
  // biased upwards. The stop rule uses the upper one.
  std::array<double, 2> gain_lower{};
  std::array<double, 2> gain_standard_error{};
  double max_gain{0.0};
  double max_gain_lower{0.0};
  // 1.96 standard errors of the player with the maximum gain; 0 when exact.
  double max_gain_half_width{0.0};
  double nashconv{0.0};
  double normalized_dev{0.0};
  double normalized_stack{0.0};
  double seconds{0.0};
  // Estimated upper bound compared with a user-facing percentage of the
  // initial pot. One per cent remains the default product target.
  [[nodiscard]] bool meets_stop_rule(const double initial_pot_antes,
                                     const double target_pot_percent = 1.0) const noexcept {
    return max_gain + max_gain_half_width <= 0.01 * target_pot_percent * initial_pot_antes;
  }
};

struct IterationTelemetry {
  std::uint64_t iteration{0U};
  std::uint32_t boards{0U};
  // Boards of this iteration whose context had already been built in an earlier batch of
  // this run (identical rebuilds, see boards_repeated()).
  std::uint32_t boards_repeated{0U};
  double seconds{0.0};
  double seconds_per_board{0.0};
  double discount_seconds{0.0};
  double policy_refresh_seconds{0.0};
  // Parts of the refresh: active-row collection and slot assignment, then the parallel
  // materialization (lazy discount and regret matching) of the compact policy.
  double policy_refresh_collect_seconds{0.0};
  double policy_refresh_materialize_seconds{0.0};
  double board_prepare_seconds{0.0};
  double board_context_cpu_seconds{0.0};
  double all_in_cache_cpu_seconds{0.0};
  double reach_setup_cpu_seconds{0.0};
  double traversal_seconds{0.0};
  double traversal_weight_setup_seconds{0.0};
  double traversal_top_down_seconds{0.0};
  double traversal_parallel_seconds{0.0};
  double traversal_top_reduce_seconds{0.0};
  double sampled_hero_reach_seconds{0.0};
  double sampled_hero_update_seconds{0.0};
  double sampled_opponent_reach_seconds{0.0};
  double sampled_opponent_accumulate_seconds{0.0};
  double sampled_fold_terminal_seconds{0.0};
  double sampled_preflop_all_in_seconds{0.0};
  double sampled_postflop_showdown_seconds{0.0};
  std::uint64_t nodes_visited{0U};
  std::uint64_t decision_nodes_visited{0U};
  std::uint64_t hero_decision_nodes{0U};
  std::uint64_t opponent_decision_nodes{0U};
  std::uint64_t chance_nodes_visited{0U};
  std::uint64_t fold_terminals_visited{0U};
  std::uint64_t preflop_all_in_terminals_visited{0U};
  std::uint64_t postflop_showdown_terminals_visited{0U};
  std::uint64_t zero_reach_prunes{0U};
  std::uint64_t policy_rows_read{0U};
  std::uint64_t regret_cells_written{0U};
  std::uint64_t strategy_cells_written{0U};
  // 3-seat path with detailed_profile, per hero, summed over the boards of the
  // iteration. Every terminal of the tree is, in each board pass of a hero,
  // visited, inside a subtree pruned for zero reach, or inside a subtree the
  // hero-folded shortcut skips: census x boards = visited + pruned + skipped.
  std::array<std::uint64_t, 3> terminals_visited_by_hero{};
  std::array<std::uint64_t, 3> terminals_pruned_by_hero{};
  std::array<std::uint64_t, 3> terminals_shortcut_by_hero{};
  // Units each hero ran and skipped (hero-folded shortcut), summed over boards.
  std::array<std::uint64_t, 3> units_run_by_hero{};
  std::array<std::uint64_t, 3> units_skipped_by_hero{};
  // Seconds spent computing the preflop class values of the 3-seat path (class
  // reach walk and cache contraction), once per hero pass.
  double class_cache_seconds{0.0};
  // Per-batch policy accounting (always on: computed per pass, not per hand).
  // Rows and cells of the compact table built for the passes of this
  // iteration, by street; hand-row lookups count every (board, hand, street)
  // pair the batch maps, so lookups - distinct rows = reuses of a row.
  std::array<std::uint64_t, 4> policy_rows_materialized{};
  std::array<std::uint64_t, 4> policy_cells_materialized{};
  std::array<std::uint64_t, 4> policy_hand_lookups{};
  std::uint64_t compact_policy_bytes{0U};
  std::uint64_t process_bytes{0U};
};

// Bytes actually reserved by the trainer, component by component (capacity of
// the containers, not theoretical cell counts). Nothing here is measured from
// the operating system; see process_memory_peaks for the process view.
struct MemoryBreakdown {
  std::uint64_t regret_bytes{0U};
  std::uint64_t strategy_sum_bytes{0U};
  std::uint64_t compact_policy_capacity_bytes{0U};
  std::uint64_t compact_policy_offsets_bytes{0U};
  std::uint64_t discount_timestamp_bytes{0U};
  std::uint64_t discount_factor_bytes{0U};
  std::uint64_t discount_offset_bytes{0U};
  std::uint64_t all_in_dense_bytes{0U};
  std::uint64_t board_batch_bytes{0U};
  std::uint64_t workspace_bytes{0U};
  std::uint64_t unit_bytes{0U};
  std::uint64_t layout_offset_bytes{0U};
  std::uint64_t partition_bytes{0U};
  std::uint64_t board_list_bytes{0U};
  std::uint64_t hand_mask_bytes{0U};
  std::uint64_t tree_bytes{0U};
  std::uint64_t history_map_resident_bytes{0U};
  std::uint64_t bucket_table_bytes{0U};
  std::uint64_t rank_table_bytes{0U};
  std::uint64_t catalog_bytes{0U};
  std::uint64_t all_in_table_bytes{0U};
  // 3-seat path: the preflop class cache (tensors, class reach and per-pass
  // values), and the trainer's per-node tables of the path (folded payoffs,
  // terminal counts, preflop indices, zero-reach flags); 0 for 2 seats.
  std::uint64_t class_cache_bytes{0U};
  std::uint64_t class_values_bytes{0U};
  std::array<std::uint64_t, 4> cells_by_street{};
  std::array<std::uint64_t, 4> rows_by_street{};
  std::uint32_t regret_bytes_per_cell{8U};
  std::uint32_t strategy_sum_bytes_per_cell{8U};
  [[nodiscard]] std::uint64_t total() const noexcept {
    return regret_bytes + strategy_sum_bytes + compact_policy_capacity_bytes +
           compact_policy_offsets_bytes + discount_timestamp_bytes + discount_factor_bytes +
           discount_offset_bytes + all_in_dense_bytes + board_batch_bytes + workspace_bytes +
           unit_bytes + layout_offset_bytes + partition_bytes + board_list_bytes +
           hand_mask_bytes + tree_bytes + history_map_resident_bytes + bucket_table_bytes +
           rank_table_bytes + catalog_bytes + all_in_table_bytes + class_cache_bytes +
           class_values_bytes;
  }
};

struct ProcessMemoryPeaks {
  std::uint64_t working_set_bytes{0U};
  std::uint64_t peak_working_set_bytes{0U};
  std::uint64_t private_commit_bytes{0U};
  std::uint64_t peak_private_commit_bytes{0U};
  std::uint64_t page_faults{0U};
};

// ---- Part A of phase 3b (PHASE3_SPEC_2026-09-30 section 6.2): the values of
// a fixed policy through the trainer's own traversal, with no regret or
// strategy-sum table (policy-only initialization). For every seat s, board B
// of weight w_B (the weights of the list sum to one) and live hero hand h, the
// traversal gives v_a[h] at s's preflop decisions, v[h] at the root and the
// joint disjoint mass D[h] of the other seats at every node, all with the
// other seats' reach inside and unnormalized (3 seats: ordered disjoint
// pairs; heads-up: disjoint hands). With P = P(o1,o2|h) = 1/(406 * 351) (heads-up
// P(o|h) = 1/406) and P(h) = 1/465 the sums accumulated per combo are
//   value_sum[node][a][h] += w_B P v_a[h]      reach_sum[node][h] += w_B P D[h]
//   root_sum[h] += w_B P v[h]                  live_sum[h] += w_B [h live on B]
//   ev_direct += w_B sum_h P(h) P v[h]         rake_sum += w_B E[rake | B]
// E[rake | B] = sum over the terminals t of rake(t) sum_h P(h) P r_s(h) D_t[h],
// each terminal counted once, in the pass of its lowest active seat. The
// chunk series (one record per flop, or per block of an explicit list) holds
// the same sums per hand class for the standard errors over i.i.d. flops. A
// per-combo mean is value_sum / live_sum; the class estimate pools the combos
// of a class (the spec's choice for sampled lists). The two agree on a
// suit-closed list only: on a canonical list with orbit weights the per-combo
// means are not suit-symmetric and the pooled class values are the exact
// per-combo values (a preflop class is one suit orbit of combos).
enum class PolicyValuesBoardKind : std::uint8_t {
  // The 573 canonical flops with their orbit weights, every turn and river:
  // the exact list (605,088 boards, 573 chunks).
  CanonicalFlops,
  // `flops` physical flops drawn uniformly with `seed` (the draw of
  // BoardCatalog::sample_physical_history, as the heads-up evaluator), every
  // turn and river, weight 1 / flops each: one chunk per flop.
  PhysicalFlops,
  // An explicit exact list (sample = false), in blocks of chunk_boards.
  List
};

struct PolicyValuesProgress {
  std::uint32_t chunks_done{0U};
  std::uint32_t chunks_total{0U};
  std::uint64_t boards_done{0U};
  double seconds{0.0};
};

struct PolicyValuesOptions {
  PolicyValuesBoardKind boards{PolicyValuesBoardKind::PhysicalFlops};
  // PhysicalFlops: the number of flops; CanonicalFlops: a limit on the
  // canonical flops taken in catalog order (0 = all 573, a partial pass
  // otherwise, for smoke tests).
  std::uint32_t flops{64U};
  std::uint64_t seed{0x5041'5254'4120'4121ULL};
  const TrainingBoards *list{nullptr};
  std::uint32_t chunk_boards{1'056U};
  // Resumable state, rewritten atomically after every chunk (empty: none).
  // A state file of another policy, game, configuration or board list is
  // refused (IntegrityFailure).
  std::filesystem::path state_path;
  // Stop after this many chunks of this call (0 = run to the end); the result
  // is then partial (complete = false) and the state file holds the rest.
  std::uint32_t stop_after_chunks{0U};
  std::function<void(const PolicyValuesProgress &)> progress;
};

struct PolicyValuesHeroChunk {
  double ev_direct{0.0};
  // [class]
  std::vector<double> root;
  // [slot][class]
  std::vector<double> reach;
  // [slot][action][class], slot s at PolicyValuesHero::class_offset[s].
  std::vector<double> values;
};

struct PolicyValuesChunk {
  std::uint32_t index{0U};
  std::uint32_t boards{0U};
  double weight{0.0};
  double rake{0.0};
  // [class]: sum of w_B times the live combos of the class.
  std::vector<double> live;
  std::vector<PolicyValuesHeroChunk> heroes;
};

struct PolicyValuesHero {
  std::uint8_t hero{0U};
  // The seat's preflop decision nodes in preorder (slots).
  std::vector<std::uint32_t> nodes;
  std::vector<std::uint8_t> actions;
  // Offset of slot s in value_sum ([action][630]) and in a chunk's values
  // ([action][81]).
  std::vector<std::uint64_t> value_offset;
  std::vector<std::uint64_t> class_offset;
  std::vector<double> value_sum;
  // [slot][630]
  std::vector<double> reach_sum;
  // [630]
  std::vector<double> root_sum;
  double ev_direct{0.0};
};

struct PolicyValues {
  std::uint8_t seats{0U};
  std::uint32_t chunks_total{0U};
  std::uint32_t chunks_done{0U};
  std::uint64_t boards{0U};
  bool complete{false};
  bool resumed{false};
  // Sum of the weights of the chunks done (1 when complete).
  double weight{0.0};
  // [630]
  std::vector<double> live_sum;
  double rake_sum{0.0};
  std::vector<PolicyValuesHero> heroes;
  std::vector<PolicyValuesChunk> series;
  double seconds{0.0};
  std::string identity;
};

class Trainer {
public:
  [[nodiscard]] static Result<std::unique_ptr<Trainer>, TrainerError>
  create(const CompiledGame &game, const TrainerResources &resources, const TrainerConfig &config,
         const TrainingBoards *boards = nullptr, const HandSubsets *subsets = nullptr);

  [[nodiscard]] Result<IterationTelemetry, TrainerError> iterate();
  // Sampled estimate on `flops` independently drawn flops with all their
  // runouts. The evaluation is exact (all listed boards with their weights)
  // when the trainer runs on an explicit non-sampled board list, or when
  // exact_on_list is requested for a trainer that samples from a list; a
  // sampling trainer on a list draws `flops` boards from it instead. The
  // evaluator needs the average strategy as a dense table: it is allocated
  // for the duration of the call and released afterwards.
  [[nodiscard]] Result<ExploitabilityEstimate, TrainerError>
  estimate_exploitability(std::uint32_t flops, bool exact_on_list = false);

  // Dense average / current strategy (allocates one table of layout().entries).
  [[nodiscard]] BucketPolicy average_policy();
  [[nodiscard]] BucketPolicy current_policy();
  // Write the average / current strategy as a GTOSDPOL file row by row through
  // a bounded buffer, without a dense table; same bytes and checksum as
  // save_policy on the dense export. Returns the policy fingerprint.
  [[nodiscard]] Result<std::string, TrainerError>
  save_average_policy(const std::filesystem::path &path, const std::string &source);
  [[nodiscard]] Result<std::string, TrainerError>
  save_current_policy(const std::filesystem::path &path, const std::string &source);
  // Same file as save_average_policy, byte for byte in every storage and discount mode,
  // without modifying the state: a row with a pending lazy discount is discounted into
  // local copies rounded through the storage type exactly as the materialization would
  // store it, so training then continues bit-identically. Used for the policy snapshots
  // of the trainer CLI; call between iterations.
  [[nodiscard]] Result<std::string, TrainerError>
  save_average_policy_snapshot(const std::filesystem::path &path, const std::string &source) const;
  // Average strategy of one row, read without touching the state (training then
  // continues bit-identically). A pending lazy discount multiplies every strategy
  // sum of a row by one factor and every positive regret by another, so the
  // normalized row equals the exported one up to storage rounding (exactly up to
  // double rounding with double storage). Used for chart snapshots. Preconditions:
  // a usable trainer, a decision node, row < its row count; call between iterations.
  void average_strategy_row(std::uint32_t node, std::uint32_t row, double *out) const noexcept;

  // Cell accessors of the two persistent tables (any storage format).
  [[nodiscard]] double regret(std::uint64_t cell) const noexcept;
  [[nodiscard]] double strategy_sum(std::uint64_t cell) const noexcept;
  [[nodiscard]] std::uint64_t cell_count() const noexcept { return layout_.entries; }
  // Copies of the tables as double (tests and diagnostics; allocates).
  [[nodiscard]] std::vector<double> regrets() const;
  [[nodiscard]] std::vector<double> strategy_sums() const;
  [[nodiscard]] const StateLayout &layout() const noexcept { return layout_; }
  [[nodiscard]] const TrainerConfig &config() const noexcept { return config_; }
  [[nodiscard]] const SubtreePartition &partition() const noexcept { return partition_; }
  [[nodiscard]] std::uint64_t iteration() const noexcept { return iteration_; }
  [[nodiscard]] std::uint64_t boards_processed() const noexcept { return boards_processed_; }
  [[nodiscard]] std::uint64_t boards_distinct() const noexcept { return boards_distinct_; }
  [[nodiscard]] std::uint64_t boards_repeated() const noexcept { return boards_repeated_; }
  // Applies every pending lazy discount now, so later saves are read-only and can be
  // overlapped by the caller.
  void materialize_discounts();
  // Positive-regret discount over the iterations in (last, iteration], as the lazy
  // path applies it (ratio of prefix products); exposed for the tests.
  [[nodiscard]] double positive_discount_ratio(std::uint64_t last, std::uint64_t iteration);
  // Epoch base of the 16-bit lazy-discount timestamps and the decoded iteration of one
  // row's last materialization (0 when the row was never touched); for the tests.
  [[nodiscard]] std::uint64_t discount_epoch_base() const noexcept { return discount_epoch_base_; }
  [[nodiscard]] std::uint64_t discount_last_iteration(std::uint32_t node,
                                                      std::uint32_t row) const noexcept;
  // Rows the lazy discount has materialized at least once (rows a batch touched) per
  // street, and the 4 KiB pages of the regret table those rows span (pages counted from
  // the start of the table; a page shared by two nodes belongs to the street of the first
  // node that reaches it): the coverage a page-granular sparse allocation could exploit.
  // Full materializations skip the untouched rows, so the coverage survives saves and
  // evaluations; a checkpoint load marks every row.
  struct RowCoverage {
    std::array<std::uint64_t, 4> rows_total{};
    std::array<std::uint64_t, 4> rows_touched{};
    std::array<std::uint64_t, 4> regret_pages_total{};
    std::array<std::uint64_t, 4> regret_pages_touched{};
  };
  [[nodiscard]] RowCoverage row_coverage() const;
  [[nodiscard]] double initial_pot_antes() const noexcept { return initial_pot_antes_; }
  [[nodiscard]] double effective_stack_antes() const noexcept { return stack_antes_; }
  // FNV-1a over iteration, RNG states and the two tables: equal fingerprints
  // mean bit-identical state. The bytes hashed are the table bytes in their
  // storage format.
  [[nodiscard]] std::string state_fingerprint();
  [[nodiscard]] std::uint64_t state_bytes() const noexcept;
  [[nodiscard]] MemoryBreakdown memory_breakdown() const noexcept;
  [[nodiscard]] const std::string &identity() const noexcept { return identity_; }
  // FNV-1a of the locked rows (node, class, bits of every frequency); empty without a lock.
  [[nodiscard]] const std::string &preflop_lock_fingerprint() const noexcept {
    return lock_fingerprint_;
  }
  [[nodiscard]] std::uint32_t preflop_locked_rows() const noexcept { return lock_row_count_; }
  // Reach vectors of the traversal: 3 for a 3-player game and for the V7
  // harness, 2 on the heads-up path.
  [[nodiscard]] std::uint8_t traversal_seats() const noexcept { return traversal_seats_; }
  // Heroes updated per iteration (the game's players, in seat order).
  [[nodiscard]] std::uint8_t heroes() const noexcept { return heroes_; }
  // Units whose root is inactive for the hero (skipped by the hero-folded
  // shortcut); fixed by the tree and the partition. 0 on the heads-up path.
  [[nodiscard]] std::uint32_t skipped_unit_count(const std::uint8_t hero) const noexcept {
    return hero < 3U ? static_cast<std::uint32_t>(unit_skipped_[hero].size()) : 0U;
  }

  // Atomic checkpoint (temporary file then rename) with checksum and identity.
  [[nodiscard]] Result<bool, TrainerError> save_checkpoint(const std::filesystem::path &path);
  // Restores iteration, RNG states and tables into a trainer created with the
  // same game, resources, configuration and hooks.
  [[nodiscard]] Result<bool, TrainerError> load_checkpoint(const std::filesystem::path &path);
  // Restarts the evaluation RNG from a seed, for example to re-evaluate a
  // restored checkpoint on fresh flops; training is unaffected.
  void reseed_evaluation(const std::uint64_t seed) noexcept { evaluation_random_.reseed(seed); }

  // Part A (see PolicyValues): the values of `policy` (its table is moved into
  // the evaluator: no copy) over the boards of `options`, with the game's
  // player count (2 or 3), the abstraction of `resources` and the trainer
  // settings of `config` that matter here: threads, capacities, partition
  // target, preflop_terminals and validation (BoardKernels needs a List and
  // validation, as in create), hero_folded_shortcut. The regret and
  // strategy-sum tables are never allocated; the harness, hand subsets, a
  // preflop lock and lazy discount are refused (InvalidConfiguration).
  [[nodiscard]] static Result<PolicyValues, TrainerError>
  evaluate_policy_values(const CompiledGame &game, const TrainerResources &resources,
                         const TrainerConfig &config, BucketPolicy policy,
                         const PolicyValuesOptions &options);

  ~Trainer();
  Trainer(const Trainer &) = delete;
  Trainer &operator=(const Trainer &) = delete;

private:
  friend class TrainerAccess;
  struct BoardWork;
  struct Workspace;
  struct Unit;
  // 3-seat path: a unit with three reach vectors, and the test trace (defined
  // in trainer.cpp; null in production).
  struct Unit3;
  struct Trace3;
  // Part A: the per-pass buffers of evaluate_policy_values (defined in
  // trainer.cpp; null in training).
  struct ValueCollector;
  using ActiveRows = std::array<std::vector<std::uint32_t>, 4>;
  // Reach vector of every seat (seat order) at a node of the 3-seat traversal.
  using SeatReach = std::array<const double *, 3>;

  Trainer(const CompiledGame &game, const TrainerResources &resources, const TrainerConfig &config);
  Result<bool, TrainerError> initialize(const TrainingBoards *boards, const HandSubsets *subsets,
                                        std::vector<double> *fixed_policy = nullptr);
  Result<bool, TrainerError> prepare_board(const card_abstraction::BoardHistory &history,
                                           double weight, BoardWork &work) const;
  [[nodiscard]] ActiveRows collect_active_rows(const std::vector<BoardWork> &batch) const;
  void assign_slots(std::vector<BoardWork> &batch, const ActiveRows &active) const;
  // Materialize the regret-matched policy of the rows of the batch into the
  // compact table and map every (board, street, hand) to its slot.
  void refresh_policy(std::vector<BoardWork> &batch, IterationTelemetry *telemetry);
  // Calls function(node, row, offset, actions) for every row of the layout.
  template <typename Function> void for_each_row(Function &&function) const;
  // Locked frequencies of (node, row), or nullptr (unlocked node or class, postflop row).
  [[nodiscard]] const double *locked_row(std::uint32_t node, std::uint32_t row) const noexcept;
  // Per-class lock flags (81 bytes) of a locked node, or nullptr.
  [[nodiscard]] const std::uint8_t *locked_classes(std::uint32_t node) const noexcept;
  Result<bool, TrainerError> initialize_lock();
  // Overwrites the locked rows of the compact per-batch policy.
  void apply_lock_to_policy();
  void average_row(std::uint64_t offset, std::uint8_t actions, double *out) const noexcept;
  // Average row as materialize_all_discounts() would leave it: a pending lazy discount
  // (slot older than discount_target_) is applied to copies of the cells, each rounded
  // through the storage type as apply_row_discount stores it. Reads only.
  void materialized_average_row(std::uint64_t offset, std::uint8_t actions, std::uint16_t slot,
                                double *out) const noexcept;
  void current_row(std::uint64_t offset, std::uint8_t actions, double *out) const noexcept;
  void fill_average_policy(std::vector<double> &table);
  void fill_current_policy(std::vector<double> &table);
  // Streams the average rows; rows with a pending lazy discount are read through
  // materialized_average_row, so the state is never modified.
  [[nodiscard]] Result<std::string, TrainerError>
  write_average_policy(const std::filesystem::path &path, const std::string &source) const;
  void discount_state(std::uint64_t iteration);
  void prepare_discount_factors(std::uint64_t iteration);
  void materialize_row(std::uint32_t node, std::uint32_t row, std::uint64_t iteration);
  // Discount of one row over the iterations in (last, iteration] (no timestamp update).
  void apply_row_discount(std::uint32_t node, std::uint32_t row, std::uint64_t last,
                          std::uint64_t iteration);
  // Materializes every touched row to `target` and makes it the epoch base.
  void rebase_discount_epoch(std::uint64_t target);
  void materialize_all_discounts();
  void pass(const BoardWork &board, std::uint8_t hero, double iteration_weight,
            IterationTelemetry *telemetry = nullptr);
  void top_down_reach(std::uint32_t node, const double *hero_reach, const double *opponent_reach,
                      std::uint8_t hero, const BoardWork &board, Workspace &workspace,
                      std::uint32_t depth);
  void traverse(std::uint32_t node, std::uint32_t depth, const double *hero_reach,
                const double *opponent_reach, double *values, std::uint8_t hero,
                const BoardWork &board, Workspace &workspace, bool top_phase);
  void terminal(const CompiledNode &node, const double *opponent_reach, double *values,
                double *scratch, std::uint8_t hero, const BoardWork &board) const;
  void all_in_masses(const BoardContext &context, const double *reach, double *win, double *tie,
                     double *lose) const noexcept;
  [[nodiscard]] const double *policy_row(std::uint32_t node, std::uint16_t hand,
                                         const BoardWork &board) const noexcept;

  // ---- 3-seat path (separate from the heads-up functions above).
  // Seats, partition skips, folded payoffs and creation checks of a 3-seat
  // traversal; called by initialize() after the partition is built.
  Result<bool, TrainerError> initialize_three_seat();
  // Class-cache mode: class reach of every seat at every preflop node from the
  // compact policy's preflop rows (lock applied), the zero-reach flags, and the
  // hero's class values (cache contraction). Call after refresh_policy, once
  // per hero pass.
  void prepare_preflop_classes3(std::uint8_t hero);
  // Fills the value buffers of the units the hero skips with NaN (debug).
  void poison_skipped_units3(std::uint8_t hero);
  void pass3(const BoardWork &board, std::uint8_t hero, double iteration_weight,
             IterationTelemetry *telemetry = nullptr);
  void top_down_reach3(std::uint32_t node, const SeatReach &reach, std::uint8_t hero,
                       const BoardWork &board, Workspace &workspace, std::uint32_t depth);
  void traverse3(std::uint32_t node, std::uint32_t depth, const SeatReach &reach, double *values,
                 std::uint8_t hero, const BoardWork &board, Workspace &workspace, bool top_phase);
  // Hero values at a terminal the hero reaches (active, or inactive with the
  // shortcut off); others_zero has been ruled out by the caller.
  void terminal3(const CompiledNode &node, const SeatReach &reach, double *values, double *scratch,
                 std::uint8_t hero, const BoardWork &board, Workspace &workspace) const;
  // D3[h]: joint disjoint mass of the two other seats at a node (board kernel,
  // or the scaled class D3 of a preflop node in class-cache mode).
  void deal_mass3(const CompiledNode &node, const SeatReach &reach, double *out, std::uint8_t hero,
                  const BoardWork &board, Workspace &workspace) const;
  // Class-cache mode: the hero's 81 class values at a preflop node (a preflop
  // terminal, or the node right after a preflop fold of the hero), or nullptr.
  [[nodiscard]] const double *class_values3(std::uint32_t node, std::uint8_t hero) const noexcept;
  // Either other seat's reach is zero, so every hero value at the node is 0:
  // class-level at preflop decisions and terminals in class-cache mode (the
  // cache values are board-free), board-level elsewhere.
  [[nodiscard]] bool others_zero3(const CompiledNode &node, const SeatReach &reach,
                                  std::uint8_t hero) const noexcept;
  [[nodiscard]] bool class_cache_active() const noexcept;
  // Part A hooks, active only while collector_ is set: the hero's action
  // values and the other seats' disjoint mass at a preflop decision of the
  // hero (heads-up path and 3-seat path), and the reach mass of a terminal
  // times its rake (heads-up path; the 3-seat path collects inside terminal3).
  void collect_hero_values(std::uint32_t node, const double *child_values,
                           const double *opponent_reach, const BoardWork &board) const;
  void collect_hero_values3(std::uint32_t node, const double *child_values,
                            const SeatReach &reach, std::uint8_t hero, const BoardWork &board,
                            Workspace &workspace) const;
  void collect_terminal_rake(const CompiledNode &node, const double *hero_reach,
                             const double *scratch, std::uint8_t hero,
                             const BoardWork &board) const;
  // Adds the collector's buffers of one pass of `hero` on `board` (weight w_B)
  // to the chunk sums (per combo).
  void harvest_pass(const PolicyValuesHero &hero_values, std::vector<double> &chunk_value_sum,
                    std::vector<double> &chunk_reach_sum, std::vector<double> &chunk_root_sum,
                    double &chunk_ev, double &chunk_rake, std::uint8_t hero,
                    const BoardWork &board, double weight) const;
  card_abstraction::BoardHistory sample_history(card_abstraction::DeterministicRandom &random,
                                                double &weight,
                                                std::size_t *index_out = nullptr) const;
  void note_board_sample(const card_abstraction::BoardHistory &history, std::size_t list_index,
                         IterationTelemetry &telemetry);
  [[nodiscard]] std::uint64_t regret_table_bytes() const noexcept;
  [[nodiscard]] std::uint64_t strategy_table_bytes() const noexcept;
  [[nodiscard]] const char *regret_table_data() const noexcept;
  [[nodiscard]] const char *strategy_table_data() const noexcept;
  void add_regret(std::uint64_t cell, double increment) noexcept;

  const CompiledGame *game_;
  bool usable_{true};
  TrainerResources resources_;
  TrainerConfig config_;
  std::unique_ptr<ParallelExecutor> executor_;
  StateLayout layout_;
  SubtreePartition partition_;
  // Persistent tables; one pair is used according to config_.storage.
  std::vector<double> regrets_;
  std::vector<double> strategy_sums_;
  std::vector<float> regrets_f32_;
  std::vector<float> strategy_sums_f32_;
  // Per-batch policy: compact_offsets_[node] is the first entry of the node's
  // (slot, action) block. In fixed-policy mode the block is the dense layout.
  std::vector<double> compact_policy_;
  std::vector<std::uint64_t> compact_offsets_;
  bool fixed_policy_evaluation_{false};
  std::vector<std::uint64_t> discount_offsets_;
  // Per row: 0 = never materialized (all cells zero), otherwise the iteration of the last
  // materialization is discount_epoch_base_ + slot - 1. iterate() rebases the epoch
  // (rebase_discount_epoch) before a slot could overflow.
  std::vector<std::uint16_t> discount_iterations_;
  std::uint64_t discount_epoch_base_{0U};
  // Positive regrets retain the original per-step multiplication order because
  // their rounded value feeds the next CFR update. Strategy sums do not feed
  // training, so their prefix products safely collapse a skipped interval.
  // Prefix products of the DCFR positive-regret factors t^a / (t^a + 1): the factor
  // over any skipped range is one ratio (the prefix stays within [0.1, 1]).
  std::vector<double> positive_discount_prefix_{1.0};
  // True while every row is materialized to discount_target_: saves skip the scan and
  // may run concurrently (read-only) once it is set.
  bool discounts_materialized_{false};
  std::vector<double> strategy_discount_prefix_{1.0};
  // Dense, oriented 630 x 630 probabilities of the exact preflop all-in
  // outcomes, prepared once; boards gather rows through their live combo ids
  // instead of copying a 465 x 465 block per board.
  std::vector<double> all_in_win_probability_;
  std::vector<double> all_in_tie_probability_;
  bool all_in_available_{false};
  std::uint64_t discount_target_{0U};
  std::vector<std::uint32_t> unit_of_node_;
  std::vector<Unit> units_;
  std::vector<std::unique_ptr<Workspace>> workspaces_;
  std::vector<BoardWork> board_batch_;
  std::vector<card_abstraction::BoardHistory> board_list_;
  std::vector<double> board_weights_;
  std::vector<double> board_cumulative_;
  bool sample_boards_{true};
  // One bit per possible board (list index, or flop combination x turn x river when the
  // catalog samples): counts identical rebuilds of board contexts. Not persisted.
  std::vector<std::uint64_t> board_seen_bits_;
  std::uint64_t boards_distinct_{0U};
  std::uint64_t boards_repeated_{0U};
  std::array<std::vector<std::uint8_t>, 2> hand_masks_{};
  bool subsets_{false};
  card_abstraction::DeterministicRandom training_random_;
  card_abstraction::DeterministicRandom evaluation_random_;
  std::uint64_t iteration_{0U};
  std::uint64_t boards_processed_{0U};
  double initial_pot_antes_{0.0};
  double stack_antes_{0.0};
  std::string identity_;
  // Preflop lock: lock_block_[node] indexes the node's block (no lock = all ones);
  // block b has 81 class flags at lock_classes_[81 b] and 81 x actions frequencies
  // at lock_values_[lock_value_offsets_[b]] (zero for unlocked classes).
  std::vector<std::uint32_t> lock_block_;
  std::vector<std::uint32_t> locked_nodes_;
  std::vector<std::uint8_t> lock_classes_;
  std::vector<std::uint64_t> lock_value_offsets_;
  std::vector<double> lock_values_;
  std::uint32_t lock_row_count_{0U};
  std::string lock_fingerprint_;
  HeadsUpShowdownKernel kernel_;

  // ---- 3-seat path (traversal_seats_ == 3).
  std::uint8_t traversal_seats_{2U};
  std::uint8_t heroes_{2U};
  bool harness_{false};
  std::vector<Unit3> units3_;
  // Per hero: the units to run (in partition order) and the units it skips.
  std::array<std::vector<std::uint32_t>, 3> unit_work_{};
  std::array<std::vector<std::uint32_t>, 3> unit_skipped_{};
  // folded_payoff_[3 node + seat]: the seat's payoff (antes) at every terminal
  // below a node where it is inactive, checked constant at creation; NaN where
  // the seat is active.
  std::vector<double> folded_payoff_;
  // Terminals in the subtree of every node, and [3 node + hero] at a top node
  // those below it that no unit run by the hero counts (telemetry of prunes
  // and skips: census = visited + pruned + shortcut-skipped, per board pass).
  std::vector<std::uint32_t> terminal_count_;
  std::vector<std::uint32_t> top_uncounted_terminals_;
  // Preflop decisions and terminals: index among them (no_unit otherwise).
  std::vector<std::uint32_t> preflop_index_;
  std::vector<std::uint32_t> preflop_nodes_;
  // Class-cache mode, per pass: the all-zero flag of each seat's class reach
  // [preflop index][seat], and the hero whose class values the cache holds.
  std::vector<std::uint8_t> class_zero3_;
  std::uint8_t class_values_hero_{0xFFU};
  std::unique_ptr<PreflopClassCache> class_cache_;
  // Kernel arithmetic of the 3-seat path (multiway_kernels.hpp), e.g.
  // "multiway-kernel-v1/avx2": part of the identity, since the scalar and AVX2
  // paths agree only to rounding.
  std::string multiway_kernel_tag_;
  // Reach 1 on every live hand: the initial reach of every seat (and the
  // reach of the V7 harness's virtual seat throughout).
  std::vector<double> ones3_;
  // Hero values at the root of a 3-seat pass (diagnostics).
  std::vector<double> root_values3_;
  Trace3 *trace3_{nullptr};
  // Part A: no regret or strategy-sum table (set before initialize by
  // evaluate_policy_values), and the collector of the current pass.
  bool policy_only_{false};
  ValueCollector *collector_{nullptr};
};

[[nodiscard]] std::uint64_t process_working_set_bytes() noexcept;
[[nodiscard]] ProcessMemoryPeaks process_memory_peaks() noexcept;
[[nodiscard]] const char *trainer_error_name(TrainerError error) noexcept;
[[nodiscard]] const char *weighting_scheme_name(WeightingScheme scheme) noexcept;
[[nodiscard]] const char *update_mode_name(UpdateMode mode) noexcept;
[[nodiscard]] const char *table_storage_name(TableStorage storage) noexcept;

} // namespace gtosd::preflop_blueprint
