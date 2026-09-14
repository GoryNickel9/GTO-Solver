#pragma once

#include "gtosd/postflop/postflop_solver.hpp"
#include "gtosd/solver/card_abstraction.hpp"
#include "gtosd/solver/solver.hpp"
#include "gtosd/solver/subgame.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gtosd {

namespace detail {
struct PostflopRiverBucketRuntime;
}

struct PostflopSubgameProjectionOptions {
  std::uint64_t maximum_physical_deals{4'096};
  std::uint64_t maximum_projected_nodes{1'000'000};
};

struct PostflopProjectedPublicState {
  NodeId source_public_node{0};
  std::string history;
  std::uint8_t player_to_act{0};
  std::vector<Action> actions;
  std::vector<NodeId> child_public_nodes;
  std::vector<GameNodeId> physical_roots;
};

struct PostflopProjectedInformationSet {
  std::string information_set;
  NodeId source_public_node{0};
  std::uint8_t player{0};
  ComboId combo{0};
  std::uint16_t range_weight_basis_points{0};
  HandValue made_hand{};
};

struct PostflopSubgameProjectionByteModel {
  std::uint64_t finite_game_bytes{0};
  std::uint64_t blueprint_bytes{0};
  std::uint64_t metadata_bytes{0};
  std::uint64_t total_bytes{0};
  bool allocator_overhead_included{false};
};

struct PostflopSubgameProjection {
  FiniteGame game;
  StrategyProfile blueprint;
  std::string source_game_fingerprint;
  std::uint64_t source_iterations{0};
  NodeId source_root{0};
  std::uint64_t physical_deals{0};
  double projection_seconds{0.0};
  PostflopSubgameProjectionByteModel byte_model;
  std::vector<PostflopProjectedPublicState> public_states;
  std::vector<PostflopProjectedInformationSet> information_sets;
};

enum class PostflopRiverBucketAbstraction : std::uint8_t {
  MadeHandValueV1,
  ExactBlockerSignatureV2,
  ShowdownDistributionV3,
};

struct PostflopRiverBucketBuildOptions {
  std::uint64_t maximum_physical_deals{500'000};
  std::uint64_t maximum_bucket_pairs{65'536};
  std::uint64_t maximum_materialized_validation_nodes{5'000'000};
  bool materialize_validation_game{false};
  PostflopRiverBucketAbstraction abstraction{PostflopRiverBucketAbstraction::MadeHandValueV1};
  // V3 rounds each opponent-range mass feature to this many normalized basis
  // points. The default is fixed before qualification and is part of the
  // abstraction fingerprint through the resulting partition.
  std::uint16_t distribution_quantization_basis_points{500};
};

// Coarsest equitable refinement used as a lossless River feasibility gate.
// Initial colors preserve player and final HandValue; refinement preserves
// compatible opponent range mass in every opposing color. A combo's own range
// weight only scales its otherwise-identical counterfactual updates.
struct PostflopRiverEquitablePartitionAnalysis {
  std::array<std::uint64_t, 2> active_combos{};
  std::array<std::uint64_t, 2> initial_classes{};
  std::array<std::uint64_t, 2> stable_classes{};
  std::array<std::uint64_t, 2> maximum_stable_class_size{};
  std::uint64_t physical_deals{0};
  std::uint64_t compatible_class_pairs{0};
  std::uint64_t refinement_rounds{0};
  double strategic_row_reduction{0.0};
  double deal_pair_reduction{0.0};
  bool partition_is_equitable{false};
};

[[nodiscard]] Result<PostflopRiverEquitablePartitionAnalysis, SolverError>
analyze_fixed_river_equitable_partition(const PostflopTreeConfig &config,
                                        const PostflopRanges &ranges,
                                        std::uint64_t maximum_physical_deals = 500'000);

struct PostflopRiverBucketMember {
  ComboId combo{0};
  std::uint16_t range_weight_basis_points{0};

  friend bool operator==(const PostflopRiverBucketMember &,
                         const PostflopRiverBucketMember &) = default;
};

struct PostflopRiverBucket {
  std::uint16_t index{0};
  HandValue made_hand{};
  double total_range_weight{0.0};
  std::vector<PostflopRiverBucketMember> members;
};

struct PostflopRiverBucketPair {
  std::uint16_t first_bucket{0};
  std::uint16_t second_bucket{0};
  std::uint64_t physical_deals{0};
  double unnormalized_mass{0.0};
  double probability{0.0};
};

struct PostflopRiverBucketWorkModel {
  std::uint64_t physical_deals_preprocessed{0};
  std::uint64_t bucket_pairs{0};
  std::uint64_t public_nodes_per_pair{0};
  std::uint64_t physical_node_instances_per_player_pass{0};
  std::uint64_t bucket_node_instances_per_player_pass{0};
  std::uint64_t physical_action_entries_per_player_pass{0};
  std::uint64_t bucket_action_entries_per_player_pass{0};
  std::uint64_t abstract_information_sets{0};
  std::uint64_t abstract_action_entries{0};
};

struct PostflopRiverBucketByteModel {
  std::uint64_t finite_game_bytes{0};
  std::uint64_t bucket_metadata_bytes{0};
  std::uint64_t runtime_bytes{0};
  std::uint64_t solver_state_payload_bytes{0};
  std::uint64_t checkpoint_mirror_payload_bytes{0};
  std::uint64_t traversal_scratch_payload_bytes{0};
  std::uint64_t total_bytes{0};
  bool allocator_overhead_included{false};
};

struct PostflopRiverPublicDecision {
  NodeId source_public_node{0};
  std::uint8_t player_to_act{0};
  std::vector<Action> actions;
};

struct PostflopRiverBucketGame {
  static constexpr std::uint32_t format_major = 1;
  static constexpr std::uint32_t format_minor = 0;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::optional<FiniteGame> validation_game;
  std::string game_fingerprint;
  std::string source_game_fingerprint;
  std::string abstraction_fingerprint;
  PostflopRiverBucketAbstraction abstraction{PostflopRiverBucketAbstraction::MadeHandValueV1};
  NodeId source_root{0};
  std::array<std::vector<PostflopRiverBucket>, 2> player_buckets;
  std::vector<PostflopRiverBucketPair> bucket_pairs;
  std::vector<PostflopRiverPublicDecision> public_decisions;
  PostflopRiverBucketWorkModel work_model;
  PostflopRiverBucketByteModel byte_model;
  double preparation_seconds{0.0};
  std::shared_ptr<const detail::PostflopRiverBucketRuntime> runtime;
};

// Experimental fixed-river representation. Physical combos are enumerated
// once to preserve range weights and card removal, then compatible deal mass
// is aggregated by the selected versioned abstraction. ProductionDcfr
// traverses the resulting bucket-pair game and never visits physical deals in
// its hot path.
[[nodiscard]] Result<PostflopRiverBucketGame, SolverError>
build_fixed_river_bucket_game(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                              const PostflopRiverBucketBuildOptions &options = {});

[[nodiscard]] const char *
postflop_river_bucket_abstraction_name(PostflopRiverBucketAbstraction abstraction) noexcept;

// The experimental kernel deliberately accepts only the qualified 1.5/0/3
// single-thread ProductionDcfr contract. Its checkpoint fingerprint is
// distinct from every exact postflop checkpoint.
[[nodiscard]] Result<SolveResult, SolverError>
solve_fixed_river_bucket_game(const PostflopRiverBucketGame &bucket_game,
                              const SolverConfig &config,
                              const SolverCheckpoint *resume_from = nullptr);

[[nodiscard]] Result<bool, SolverError>
save_fixed_river_bucket_checkpoint(const PostflopRiverBucketGame &bucket_game,
                                   const SolverCheckpoint &checkpoint, const std::string &path);

[[nodiscard]] Result<SolverCheckpoint, SolverError>
load_fixed_river_bucket_checkpoint(const PostflopRiverBucketGame &bucket_game,
                                   const std::string &path);

// Expands one bucket strategy row to a physical combo at a public decision.
// The returned probabilities are shared by every member of that bucket.
[[nodiscard]] Result<PostflopStrategyQuery, SolverError>
query_fixed_river_bucket_strategy(const PostflopRiverBucketGame &bucket_game,
                                  const StrategyProfile &profile, NodeId public_node,
                                  ComboId combo);

// Validation-only bridge. The exact projection remains the original-game
// oracle; this function only lifts the already-solved bucket profile onto its
// physical infosets so best response and NashConv can be calculated there.
[[nodiscard]] Result<StrategyProfile, SolverError>
lift_fixed_river_bucket_strategy(const PostflopSubgameProjection &exact_projection,
                                 const PostflopRiverBucketGame &bucket_game,
                                 const StrategyProfile &bucket_profile);

// Exact, bounded bridge used by R2-S. It materializes every compatible private
// deal for a fixed-river game and copies the average strategy from a
// ProductionDcfr exact checkpoint. It rejects larger games instead of silently
// sampling or dropping chance outcomes.
[[nodiscard]] Result<PostflopSubgameProjection, SolverError>
project_fixed_river_postflop_game(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                                  const PostflopCheckpoint &checkpoint,
                                  const PostflopSubgameProjectionOptions &options = {});

[[nodiscard]] Result<PublicSubgameDefinition, SolverError>
define_projected_postflop_subgame(const PostflopSubgameProjection &projection,
                                  NodeId source_public_node, std::uint8_t resolving_player);

// Builds the same made-hand-value strategy-tying policy used by the postflop
// bucket feasibility traversal, but over the bounded exact finite projection.
// This enables an original-game BR check of the bucket + subgame composition.
[[nodiscard]] Result<CardAbstractionPolicy, SolverError>
make_projected_postflop_card_abstraction(const PostflopSubgameProjection &projection);

} // namespace gtosd
