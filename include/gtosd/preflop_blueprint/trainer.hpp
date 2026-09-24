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
namespace gtosd::preflop_blueprint {

class ParallelExecutor;

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
           rank_table_bytes + catalog_bytes + all_in_table_bytes;
  }
};

struct ProcessMemoryPeaks {
  std::uint64_t working_set_bytes{0U};
  std::uint64_t peak_working_set_bytes{0U};
  std::uint64_t private_commit_bytes{0U};
  std::uint64_t peak_private_commit_bytes{0U};
  std::uint64_t page_faults{0U};
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
  [[nodiscard]] double initial_pot_antes() const noexcept { return initial_pot_antes_; }
  [[nodiscard]] double effective_stack_antes() const noexcept { return stack_antes_; }
  // FNV-1a over iteration, RNG states and the two tables: equal fingerprints
  // mean bit-identical state. The bytes hashed are the table bytes in their
  // storage format.
  [[nodiscard]] std::string state_fingerprint();
  [[nodiscard]] std::uint64_t state_bytes() const noexcept;
  [[nodiscard]] MemoryBreakdown memory_breakdown() const noexcept;
  [[nodiscard]] const std::string &identity() const noexcept { return identity_; }

  // Atomic checkpoint (temporary file then rename) with checksum and identity.
  [[nodiscard]] Result<bool, TrainerError> save_checkpoint(const std::filesystem::path &path);
  // Restores iteration, RNG states and tables into a trainer created with the
  // same game, resources, configuration and hooks.
  [[nodiscard]] Result<bool, TrainerError> load_checkpoint(const std::filesystem::path &path);
  // Restarts the evaluation RNG from a seed, for example to re-evaluate a
  // restored checkpoint on fresh flops; training is unaffected.
  void reseed_evaluation(const std::uint64_t seed) noexcept { evaluation_random_.reseed(seed); }

  ~Trainer();
  Trainer(const Trainer &) = delete;
  Trainer &operator=(const Trainer &) = delete;

private:
  friend class TrainerAccess;
  struct BoardWork;
  struct Workspace;
  struct Unit;
  using ActiveRows = std::array<std::vector<std::uint32_t>, 4>;

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
  template <typename Function> void for_each_row(Function &&function) const;
  void average_row(std::uint64_t offset, std::uint8_t actions, double *out) const noexcept;
  void current_row(std::uint64_t offset, std::uint8_t actions, double *out) const noexcept;
  void fill_average_policy(std::vector<double> &table);
  void fill_current_policy(std::vector<double> &table);
  void discount_state(std::uint64_t iteration);
  void prepare_discount_factors(std::uint64_t iteration);
  void materialize_row(std::uint32_t node, std::uint32_t row, std::uint64_t iteration);
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
  std::vector<std::uint32_t> discount_iterations_;
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
  HeadsUpShowdownKernel kernel_;
};

[[nodiscard]] std::uint64_t process_working_set_bytes() noexcept;
[[nodiscard]] ProcessMemoryPeaks process_memory_peaks() noexcept;
[[nodiscard]] const char *trainer_error_name(TrainerError error) noexcept;
[[nodiscard]] const char *weighting_scheme_name(WeightingScheme scheme) noexcept;
[[nodiscard]] const char *update_mode_name(UpdateMode mode) noexcept;
[[nodiscard]] const char *table_storage_name(TableStorage storage) noexcept;

} // namespace gtosd::preflop_blueprint
