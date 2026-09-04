#pragma once

#include "gtosd/core/ranges.hpp"
#include "gtosd/core/result.hpp"
#include "gtosd/solver/finite_game.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gtosd {

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
  UnsupportedVersion
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

// Exact postflop W/T/L + equity feature generation against the configured
// weighted opponent range. Flop and turn runouts are fully enumerated; river
// uses the single complete board. This is preparation work, not chance
// sampling inside CFR.
[[nodiscard]] Result<std::vector<CardAbstractionObservation>, CardAbstractionError>
build_exact_postflop_equity_observations(const std::vector<CardId> &board,
                                         const PostflopRanges &ranges, std::uint8_t player,
                                         const std::string &partition,
                                         const std::string &information_set_prefix);

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
