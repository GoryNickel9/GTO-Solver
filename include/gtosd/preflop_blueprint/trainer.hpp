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
// One iteration draws a batch of complete boards, runs for every board one
// pass that updates player 0 and one that updates player 1, and applies the
// weighting scheme once. Within a pass the current strategy is read from a
// snapshot taken from the regrets at the start of the pass (Simultaneous: one
// snapshot per iteration for both players, the order of the FiniteGame
// oracle; Alternating: a fresh snapshot before the second player). Regret and
// strategy-sum cells are written directly with the iteration weight: every
// cell has exactly one writer because threads split disjoint subtrees of the
// public tree and boards are processed in order, so the result is
// bit-identical for any thread count.
//
// Contracts (roadmap section 5), per board B of weight w_B and hero hand h:
//   R[n][b(h)][a] += w_B P(h) P(o|h) (v_a[h] - v[h])   v from the P5 kernels
//   S[n][b(h)][a] += w_B P(h) reach_hero[h] sigma(a|n,b(h))
// with P(h) = 1/(live hero hands) and P(o|h) = 1/(live opponent hands
// disjoint from h); in the full game 1/465 and 1/406. Linear multiplies the
// increments of iteration t by t; DCFR alpha/beta/gamma discounts the
// accumulated state once per iteration before the increments.
namespace gtosd::preflop_blueprint {

enum class TrainerError : std::uint8_t {
  InvalidConfiguration,
  MissingResource,
  BoardFailure,
  IoFailure,
  IntegrityFailure,
  UnsupportedVersion
};

enum class WeightingScheme : std::uint8_t { Linear, Dcfr };
enum class UpdateMode : std::uint8_t { Simultaneous, Alternating };

struct TrainerConfig {
  std::uint16_t flop_capacity{200U};
  std::uint16_t turn_capacity{500U};
  std::uint16_t river_capacity{1'000U};
  std::uint32_t batch_boards{32U};
  unsigned threads{1U};
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
};

// Exact-mode hook: explicit boards with weights. With sample = false every
// iteration processes all the boards (weights normalized to one); with
// sample = true batches are drawn from the list with probability proportional
// to the weights. An empty list samples physical histories from the catalog.
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
  // Decision D3: estimate plus half-width within one per cent of the initial pot.
  [[nodiscard]] bool meets_stop_rule(const double initial_pot_antes) const noexcept {
    return max_gain + max_gain_half_width <= 0.01 * initial_pot_antes;
  }
};

struct IterationTelemetry {
  std::uint64_t iteration{0U};
  std::uint32_t boards{0U};
  double seconds{0.0};
  double seconds_per_board{0.0};
  std::uint64_t nodes_visited{0U};
  std::uint64_t process_bytes{0U};
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
  // sampling trainer on a list draws `flops` boards from it instead.
  [[nodiscard]] Result<ExploitabilityEstimate, TrainerError>
  estimate_exploitability(std::uint32_t flops, bool exact_on_list = false);

  [[nodiscard]] BucketPolicy average_policy() const;
  [[nodiscard]] BucketPolicy current_policy() const;
  [[nodiscard]] const std::vector<double> &regrets() const noexcept { return regrets_; }
  [[nodiscard]] const std::vector<double> &strategy_sums() const noexcept {
    return strategy_sums_;
  }
  [[nodiscard]] const StateLayout &layout() const noexcept { return layout_; }
  [[nodiscard]] const TrainerConfig &config() const noexcept { return config_; }
  [[nodiscard]] const SubtreePartition &partition() const noexcept { return partition_; }
  [[nodiscard]] std::uint64_t iteration() const noexcept { return iteration_; }
  [[nodiscard]] std::uint64_t boards_processed() const noexcept { return boards_processed_; }
  [[nodiscard]] double initial_pot_antes() const noexcept { return initial_pot_antes_; }
  [[nodiscard]] double effective_stack_antes() const noexcept { return stack_antes_; }
  // FNV-1a over iteration, RNG states and the two tables: equal fingerprints
  // mean bit-identical state.
  [[nodiscard]] std::string state_fingerprint() const;
  [[nodiscard]] std::uint64_t state_bytes() const noexcept {
    return (regrets_.size() + strategy_sums_.size() + policy_.size()) * sizeof(double);
  }
  [[nodiscard]] const std::string &identity() const noexcept { return identity_; }

  // Atomic checkpoint (temporary file then rename) with checksum and identity.
  [[nodiscard]] Result<bool, TrainerError> save_checkpoint(const std::filesystem::path &path) const;
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

  Trainer(const CompiledGame &game, const TrainerResources &resources, const TrainerConfig &config);
  Result<bool, TrainerError> initialize(const TrainingBoards *boards, const HandSubsets *subsets);
  Result<bool, TrainerError> prepare_board(const card_abstraction::BoardHistory &history,
                                           double weight, BoardWork &work) const;
  void refresh_policy();
  void discount_state(std::uint64_t iteration);
  void pass(const BoardWork &board, std::uint8_t hero, double iteration_weight);
  void top_down_reach(std::uint32_t node, const double *hero_reach, const double *opponent_reach,
                      std::uint8_t hero, const BoardWork &board, Workspace &workspace,
                      std::uint32_t depth);
  void traverse(std::uint32_t node, std::uint32_t depth, const double *hero_reach,
                const double *opponent_reach, double *values, std::uint8_t hero,
                const BoardWork &board, Workspace &workspace, bool top_phase);
  void terminal(const CompiledNode &node, const double *opponent_reach, double *values,
                double *scratch, std::uint8_t hero, const BoardWork &board) const;
  [[nodiscard]] const double *policy_row(std::uint32_t node, std::uint16_t hand,
                                         const BoardContext &context) const noexcept;
  [[nodiscard]] std::uint64_t cell_offset(std::uint32_t node, std::uint16_t hand,
                                          const BoardContext &context) const noexcept;
  card_abstraction::BoardHistory sample_history(card_abstraction::DeterministicRandom &random,
                                                double &weight) const;

  const CompiledGame *game_;
  TrainerResources resources_;
  TrainerConfig config_;
  StateLayout layout_;
  SubtreePartition partition_;
  std::vector<double> regrets_;
  std::vector<double> strategy_sums_;
  std::vector<double> policy_;
  std::vector<std::uint32_t> unit_of_node_;
  std::vector<Unit> units_;
  std::vector<std::unique_ptr<Workspace>> workspaces_;
  std::vector<card_abstraction::BoardHistory> board_list_;
  std::vector<double> board_weights_;
  std::vector<double> board_cumulative_;
  bool sample_boards_{true};
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
[[nodiscard]] const char *trainer_error_name(TrainerError error) noexcept;
[[nodiscard]] const char *weighting_scheme_name(WeightingScheme scheme) noexcept;
[[nodiscard]] const char *update_mode_name(UpdateMode mode) noexcept;

} // namespace gtosd::preflop_blueprint
