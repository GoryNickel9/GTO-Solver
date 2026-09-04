#pragma once

#include "gtosd/core/ranges.hpp"
#include "gtosd/core/result.hpp"
#include "gtosd/solver/finite_game.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gtosd {

// Serialization and in-memory validation share this hard safety bound.  The
// canonical preflight publishes whether a requested game can be represented
// before feature enumeration starts.
inline constexpr std::uint64_t maximum_card_abstraction_feature_cache_observations = 2'000'000U;
inline constexpr std::uint32_t production_card_abstraction_feature_workers = 8U;

enum class CardAbstractionKind : std::uint8_t { ExactIdentity, EquityFeatureKMeans };

enum class CardAbstractionError : std::uint8_t {
  InvalidConfiguration,
  InvalidObservation,
  DuplicateInformationSet,
  IncompatiblePartition,
  IncompatibleGame,
  InvalidStrategy,
  NumericalFailure,
  InvalidSerializedData,
  UnsupportedVersion,
  IoFailure
};

struct CardAbstractionConfig {
  static constexpr std::uint32_t format_major = 1;
  static constexpr std::uint32_t format_minor = 0;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  CardAbstractionKind kind{CardAbstractionKind::ExactIdentity};
  std::uint32_t buckets_per_partition{0};
  std::uint32_t maximum_iterations{100};
  std::string feature_schema_id{"equity-features-l2-v1"};

  friend bool operator==(const CardAbstractionConfig &, const CardAbstractionConfig &) = default;
};

// One observation is one exact private-card information set. `partition`
// identifies a public state/history and action schema; observations are never
// clustered across partitions. Omission means absent/blocked. A present state
// may have reach_weight == 0 and remains distinguishable from omission.
struct CardAbstractionObservation {
  std::string information_set;
  std::string partition;
  std::uint8_t player{0};
  ComboId combo{0};
  std::uint64_t public_card_mask{0};
  double reach_weight{0.0};
  std::vector<double> equity_features;

  friend bool operator==(const CardAbstractionObservation &,
                         const CardAbstractionObservation &) = default;
};

struct CardAbstractionAssignment {
  std::string information_set;
  std::string abstract_information_set;
  std::string partition;
  std::uint8_t player{0};
  ComboId combo{0};
  std::uint64_t public_card_mask{0};
  std::uint32_t bucket{0};
  double reach_weight{0.0};
  double distance_to_centroid{0.0};
  std::vector<double> equity_features;

  friend bool operator==(const CardAbstractionAssignment &,
                         const CardAbstractionAssignment &) = default;
};

struct CardAbstractionBucket {
  std::string partition;
  std::uint8_t player{0};
  std::uint32_t bucket{0};
  double total_reach_weight{0.0};
  std::vector<double> centroid;
  std::vector<std::string> members;

  friend bool operator==(const CardAbstractionBucket &, const CardAbstractionBucket &) = default;
};

struct CardAbstractionMetrics {
  std::uint64_t exact_information_sets{0};
  std::uint64_t abstract_information_sets{0};
  double compression_ratio{1.0};
  double weighted_mean_squared_error{0.0};
  double maximum_l2_error{0.0};
  bool uses_lossy_bucketing{false};

  friend bool operator==(const CardAbstractionMetrics &, const CardAbstractionMetrics &) = default;
};

struct CardAbstraction {
  CardAbstractionConfig config{};
  std::string fingerprint;
  std::vector<CardAbstractionAssignment> assignments;
  std::vector<CardAbstractionBucket> buckets;
  CardAbstractionMetrics metrics{};

  friend bool operator==(const CardAbstraction &, const CardAbstraction &) = default;
};

// Reusable exact-feature artifact. It deliberately excludes clustering
// parameters so one expensive flop/turn enumeration can feed multiple
// bucket-granularity candidates. `source_fingerprint` binds the observations
// to the exact game/ranges that produced them; `fingerprint` authenticates the
// canonicalized manifest content.
struct CardAbstractionFeatureCache {
  static constexpr std::uint32_t format_major = 1;
  static constexpr std::uint32_t format_minor = 0;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string feature_schema_id{"equity-features-l2-v1"};
  std::string source_fingerprint;
  std::string fingerprint;
  std::uint64_t partition_count{0};
  std::vector<CardAbstractionObservation> observations;

  friend bool operator==(const CardAbstractionFeatureCache &,
                         const CardAbstractionFeatureCache &) = default;
};

// Exact postflop W/T/L + equity feature generation against the configured
// weighted opponent range. Flop and turn runouts are fully enumerated; river
// uses the single complete board. This is preparation work, not chance
// sampling inside CFR.
[[nodiscard]] Result<std::vector<CardAbstractionObservation>, CardAbstractionError>
build_exact_postflop_equity_observations(const std::vector<CardId> &board,
                                         const PostflopRanges &ranges, std::uint8_t player,
                                         const std::string &partition,
                                         const std::string &information_set_prefix);

[[nodiscard]] Result<CardAbstractionFeatureCache, CardAbstractionError>
build_card_abstraction_feature_cache(const std::vector<CardAbstractionObservation> &observations,
                                     const std::string &feature_schema_id,
                                     const std::string &source_fingerprint);

[[nodiscard]] Result<bool, CardAbstractionError>
validate_card_abstraction_feature_cache(const CardAbstractionFeatureCache &cache);

[[nodiscard]] Result<std::string, CardAbstractionError>
serialize_card_abstraction_feature_cache(const CardAbstractionFeatureCache &cache);

[[nodiscard]] Result<CardAbstractionFeatureCache, CardAbstractionError>
deserialize_card_abstraction_feature_cache(const std::string &serialized);

[[nodiscard]] Result<bool, CardAbstractionError>
save_card_abstraction_feature_cache(const CardAbstractionFeatureCache &cache,
                                    const std::string &path);

[[nodiscard]] Result<CardAbstractionFeatureCache, CardAbstractionError>
load_card_abstraction_feature_cache(const std::string &path);

[[nodiscard]] Result<CardAbstraction, CardAbstractionError>
build_card_abstraction(const std::vector<CardAbstractionObservation> &observations,
                       const CardAbstractionConfig &config);

// Rewrites only information sets named by the abstraction. Finite-game
// validation rejects buckets that mix players or action schemas.
[[nodiscard]] Result<FiniteGame, CardAbstractionError>
apply_card_abstraction(const FiniteGame &exact_game, const CardAbstraction &abstraction);

// Expands an abstract policy over the original information-set names so the
// exact/no-bucket game can remain the oracle for EV and NashConv measurement.
[[nodiscard]] Result<StrategyProfile, CardAbstractionError>
lift_card_abstraction_strategy(const FiniteGame &exact_game, const CardAbstraction &abstraction,
                               const StrategyProfile &abstract_profile);

[[nodiscard]] Result<std::string, CardAbstractionError>
serialize_card_abstraction(const CardAbstraction &abstraction);

[[nodiscard]] Result<CardAbstraction, CardAbstractionError>
deserialize_card_abstraction(const std::string &serialized);

[[nodiscard]] const char *card_abstraction_kind_name(CardAbstractionKind kind) noexcept;
[[nodiscard]] const char *card_abstraction_error_name(CardAbstractionError error) noexcept;

} // namespace gtosd
