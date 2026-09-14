#pragma once

#include "gtosd/solver/finite_game.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gtosd {

enum class PerfectRecallClaim : std::uint8_t {
  IdentityVerified,
  NotClaimed,
};

struct CardAbstractionEntry {
  std::string original_information_set;
  std::string abstract_information_set;
  double aggregation_weight{1.0};
  friend bool operator==(const CardAbstractionEntry &, const CardAbstractionEntry &) = default;
};

struct CardAbstractionPolicy {
  static constexpr std::uint32_t format_major = 1;
  static constexpr std::uint32_t format_minor = 0;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string policy_id;
  std::string similarity_metric;
  std::string street_scope;
  PerfectRecallClaim perfect_recall{PerfectRecallClaim::NotClaimed};
  std::vector<CardAbstractionEntry> entries;
};

struct CardAbstractionSummary {
  std::uint64_t original_information_sets{0};
  std::uint64_t abstract_information_sets{0};
  std::uint64_t original_action_entries{0};
  std::uint64_t abstract_action_entries{0};
  bool identity{false};
  bool exact_chance_outcomes{true};
  bool perfect_recall_verified{false};
  std::string policy_fingerprint;
};

struct CardAbstractionByteModel {
  std::uint64_t dense_mapping_bytes{0};
  std::uint64_t aggregation_weight_bytes{0};
  std::uint64_t bucket_offset_bytes{0};
  std::uint64_t serialized_policy_bytes{0};
  std::uint64_t minimum_runtime_bytes{0};
};

[[nodiscard]] Result<CardAbstractionPolicy, SolverError>
make_identity_card_abstraction(const FiniteGame &game, std::string policy_id);

[[nodiscard]] Result<CardAbstractionSummary, SolverError>
validate_card_abstraction_policy(const FiniteGame &game, const CardAbstractionPolicy &policy);

[[nodiscard]] Result<FiniteGame, SolverError>
apply_card_abstraction(const FiniteGame &game, const CardAbstractionPolicy &policy);

[[nodiscard]] Result<StrategyProfile, SolverError>
aggregate_strategy_profile(const FiniteGame &original_game, const CardAbstractionPolicy &policy,
                           const StrategyProfile &original_profile);

[[nodiscard]] Result<StrategyProfile, SolverError>
lift_strategy_profile(const FiniteGame &original_game, const CardAbstractionPolicy &policy,
                      const StrategyProfile &abstract_profile);

[[nodiscard]] Result<std::string, SolverError>
serialize_card_abstraction_policy(const CardAbstractionPolicy &policy);

[[nodiscard]] Result<CardAbstractionPolicy, SolverError>
deserialize_card_abstraction_policy(const std::string &serialized);

[[nodiscard]] std::string card_abstraction_policy_fingerprint(const CardAbstractionPolicy &policy);

[[nodiscard]] Result<CardAbstractionByteModel, SolverError>
estimate_card_abstraction_bytes(const FiniteGame &game, const CardAbstractionPolicy &policy);

[[nodiscard]] const char *perfect_recall_claim_name(PerfectRecallClaim claim) noexcept;

} // namespace gtosd
