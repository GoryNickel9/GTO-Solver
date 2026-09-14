#include "gtosd/solver/card_abstraction.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace gtosd {
namespace {

struct InformationSetDefinition {
  std::uint8_t player{0};
  std::vector<GameActionId> actions;
};

using Definitions = std::map<std::string, InformationSetDefinition>;

Result<Definitions, SolverError> collect_definitions(const FiniteGame &game) {
  const auto validation = validate_finite_game(game);
  if (!validation) {
    return Result<Definitions, SolverError>::failure(validation.error());
  }
  Definitions definitions;
  for (const auto &node : game.nodes) {
    if (node.kind != GameNodeKind::Decision || definitions.contains(node.information_set)) {
      continue;
    }
    InformationSetDefinition definition;
    definition.player = node.player;
    for (const auto &edge : node.edges) {
      definition.actions.push_back(edge.action.id);
    }
    definitions.emplace(node.information_set, std::move(definition));
  }
  return Result<Definitions, SolverError>::success(std::move(definitions));
}

std::map<std::string, const CardAbstractionEntry *>
index_entries(const CardAbstractionPolicy &policy) {
  std::map<std::string, const CardAbstractionEntry *> entries;
  for (const auto &entry : policy.entries) {
    entries.emplace(entry.original_information_set, &entry);
  }
  return entries;
}

bool checked_add(std::uint64_t &target, const std::uint64_t value) {
  if (value > std::numeric_limits<std::uint64_t>::max() - target) {
    return false;
  }
  target += value;
  return true;
}

bool checked_multiply(const std::uint64_t left, const std::uint64_t right, std::uint64_t &result) {
  if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return false;
  }
  result = left * right;
  return true;
}

void hash_bytes(std::uint64_t &hash, const std::string_view bytes) {
  constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
  for (const unsigned char byte : bytes) {
    hash ^= byte;
    hash *= fnv_prime;
  }
}

const char *perfect_recall_token(const PerfectRecallClaim claim) noexcept {
  switch (claim) {
  case PerfectRecallClaim::IdentityVerified:
    return "identity_verified";
  case PerfectRecallClaim::NotClaimed:
    return "not_claimed";
  }
  return "unknown";
}

Result<PerfectRecallClaim, SolverError> parse_perfect_recall(const std::string &value) {
  if (value == "identity_verified") {
    return Result<PerfectRecallClaim, SolverError>::success(PerfectRecallClaim::IdentityVerified);
  }
  if (value == "not_claimed") {
    return Result<PerfectRecallClaim, SolverError>::success(PerfectRecallClaim::NotClaimed);
  }
  return Result<PerfectRecallClaim, SolverError>::failure(SolverError::InvalidAbstraction);
}

} // namespace

Result<CardAbstractionPolicy, SolverError> make_identity_card_abstraction(const FiniteGame &game,
                                                                          std::string policy_id) {
  const auto definitions = collect_definitions(game);
  if (!definitions || policy_id.empty()) {
    return Result<CardAbstractionPolicy, SolverError>::failure(
        definitions ? SolverError::InvalidAbstraction : definitions.error());
  }
  CardAbstractionPolicy policy;
  policy.policy_id = std::move(policy_id);
  policy.similarity_metric = "identity";
  policy.street_scope = "all";
  policy.perfect_recall = PerfectRecallClaim::IdentityVerified;
  policy.entries.reserve(definitions.value().size());
  for (const auto &[information_set, definition] : definitions.value()) {
    (void)definition;
    policy.entries.push_back({information_set, information_set, 1.0});
  }
  return Result<CardAbstractionPolicy, SolverError>::success(std::move(policy));
}

Result<CardAbstractionSummary, SolverError>
validate_card_abstraction_policy(const FiniteGame &game, const CardAbstractionPolicy &policy) {
  if (policy.major != CardAbstractionPolicy::format_major ||
      policy.minor > CardAbstractionPolicy::format_minor) {
    return Result<CardAbstractionSummary, SolverError>::failure(
        SolverError::UnsupportedAbstractionVersion);
  }
  if (policy.policy_id.empty() || policy.similarity_metric.empty() || policy.street_scope.empty()) {
    return Result<CardAbstractionSummary, SolverError>::failure(SolverError::InvalidAbstraction);
  }
  const auto definitions = collect_definitions(game);
  if (!definitions) {
    return Result<CardAbstractionSummary, SolverError>::failure(definitions.error());
  }
  if (policy.entries.size() != definitions.value().size()) {
    return Result<CardAbstractionSummary, SolverError>::failure(SolverError::InvalidAbstraction);
  }

  std::set<std::string> originals;
  std::map<std::string, InformationSetDefinition> buckets;
  bool identity = true;
  std::uint64_t original_action_entries = 0U;
  for (const auto &entry : policy.entries) {
    if (entry.original_information_set.empty() || entry.abstract_information_set.empty() ||
        !std::isfinite(entry.aggregation_weight) || entry.aggregation_weight <= 0.0 ||
        !originals.insert(entry.original_information_set).second) {
      return Result<CardAbstractionSummary, SolverError>::failure(SolverError::InvalidAbstraction);
    }
    const auto original = definitions.value().find(entry.original_information_set);
    if (original == definitions.value().end()) {
      return Result<CardAbstractionSummary, SolverError>::failure(SolverError::InvalidAbstraction);
    }
    if (!checked_add(original_action_entries, original->second.actions.size())) {
      return Result<CardAbstractionSummary, SolverError>::failure(SolverError::InvalidAbstraction);
    }
    const auto [bucket, inserted] =
        buckets.emplace(entry.abstract_information_set, original->second);
    if (!inserted && (bucket->second.player != original->second.player ||
                      bucket->second.actions != original->second.actions)) {
      return Result<CardAbstractionSummary, SolverError>::failure(SolverError::InvalidAbstraction);
    }
    identity = identity && entry.original_information_set == entry.abstract_information_set &&
               entry.aggregation_weight == 1.0;
  }

  if (policy.perfect_recall == PerfectRecallClaim::IdentityVerified && !identity) {
    return Result<CardAbstractionSummary, SolverError>::failure(SolverError::InvalidAbstraction);
  }

  std::uint64_t abstract_action_entries = 0U;
  for (const auto &[name, definition] : buckets) {
    (void)name;
    if (!checked_add(abstract_action_entries, definition.actions.size())) {
      return Result<CardAbstractionSummary, SolverError>::failure(SolverError::InvalidAbstraction);
    }
  }

  CardAbstractionSummary summary;
  summary.original_information_sets = definitions.value().size();
  summary.abstract_information_sets = buckets.size();
  summary.original_action_entries = original_action_entries;
  summary.abstract_action_entries = abstract_action_entries;
  summary.identity = identity;
  summary.perfect_recall_verified = policy.perfect_recall == PerfectRecallClaim::IdentityVerified;
  summary.policy_fingerprint = card_abstraction_policy_fingerprint(policy);
  return Result<CardAbstractionSummary, SolverError>::success(std::move(summary));
}

Result<FiniteGame, SolverError> apply_card_abstraction(const FiniteGame &game,
                                                       const CardAbstractionPolicy &policy) {
  const auto validation = validate_card_abstraction_policy(game, policy);
  if (!validation) {
    return Result<FiniteGame, SolverError>::failure(validation.error());
  }
  const auto entries = index_entries(policy);
  FiniteGame abstract_game = game;
  abstract_game.game_id =
      game.game_id + ":card_abstraction:" + validation.value().policy_fingerprint;
  for (auto &node : abstract_game.nodes) {
    if (node.kind == GameNodeKind::Decision) {
      node.information_set = entries.at(node.information_set)->abstract_information_set;
    }
  }
  return Result<FiniteGame, SolverError>::success(std::move(abstract_game));
}

Result<StrategyProfile, SolverError>
aggregate_strategy_profile(const FiniteGame &original_game, const CardAbstractionPolicy &policy,
                           const StrategyProfile &original_profile) {
  const auto policy_validation = validate_card_abstraction_policy(original_game, policy);
  const auto strategy_validation = validate_strategy_profile(original_game, original_profile);
  if (!policy_validation || !strategy_validation) {
    return Result<StrategyProfile, SolverError>::failure(
        !policy_validation ? policy_validation.error() : strategy_validation.error());
  }

  struct Accumulator {
    InformationSetStrategy strategy;
    double weight{0.0};
  };
  std::map<std::string, Accumulator> accumulated;
  for (const auto &entry : policy.entries) {
    const auto &source = original_profile.at(entry.original_information_set);
    auto [found, inserted] = accumulated.try_emplace(entry.abstract_information_set);
    if (inserted) {
      found->second.strategy.player = source.player;
      found->second.strategy.actions = source.actions;
      found->second.strategy.probabilities.assign(source.probabilities.size(), 0.0);
    }
    for (std::size_t index = 0; index < source.probabilities.size(); ++index) {
      found->second.strategy.probabilities[index] +=
          entry.aggregation_weight * source.probabilities[index];
    }
    found->second.weight += entry.aggregation_weight;
  }

  StrategyProfile result;
  for (auto &[bucket, accumulator] : accumulated) {
    if (!std::isfinite(accumulator.weight) || accumulator.weight <= 0.0) {
      return Result<StrategyProfile, SolverError>::failure(SolverError::InvalidAbstraction);
    }
    for (auto &probability : accumulator.strategy.probabilities) {
      probability /= accumulator.weight;
    }
    result.emplace(std::move(bucket), std::move(accumulator.strategy));
  }
  const auto abstract_game = apply_card_abstraction(original_game, policy);
  if (!abstract_game) {
    return Result<StrategyProfile, SolverError>::failure(abstract_game.error());
  }
  const auto validation = validate_strategy_profile(abstract_game.value(), result);
  return validation ? Result<StrategyProfile, SolverError>::success(std::move(result))
                    : Result<StrategyProfile, SolverError>::failure(validation.error());
}

Result<StrategyProfile, SolverError>
lift_strategy_profile(const FiniteGame &original_game, const CardAbstractionPolicy &policy,
                      const StrategyProfile &abstract_profile) {
  const auto abstract_game = apply_card_abstraction(original_game, policy);
  if (!abstract_game) {
    return Result<StrategyProfile, SolverError>::failure(abstract_game.error());
  }
  const auto abstract_validation =
      validate_strategy_profile(abstract_game.value(), abstract_profile);
  if (!abstract_validation) {
    return Result<StrategyProfile, SolverError>::failure(abstract_validation.error());
  }

  StrategyProfile result;
  for (const auto &entry : policy.entries) {
    const auto source = abstract_profile.find(entry.abstract_information_set);
    if (source == abstract_profile.end()) {
      return Result<StrategyProfile, SolverError>::failure(SolverError::InvalidStrategy);
    }
    result.emplace(entry.original_information_set, source->second);
  }
  const auto validation = validate_strategy_profile(original_game, result);
  return validation ? Result<StrategyProfile, SolverError>::success(std::move(result))
                    : Result<StrategyProfile, SolverError>::failure(validation.error());
}

Result<std::string, SolverError>
serialize_card_abstraction_policy(const CardAbstractionPolicy &policy) {
  if (policy.major != CardAbstractionPolicy::format_major ||
      policy.minor > CardAbstractionPolicy::format_minor || policy.policy_id.empty() ||
      policy.similarity_metric.empty() || policy.street_scope.empty()) {
    return Result<std::string, SolverError>::failure(SolverError::InvalidAbstraction);
  }
  std::ostringstream output;
  output << "GTOSD_CARD_ABSTRACTION " << policy.major << ' ' << policy.minor << '\n'
         << std::quoted(policy.policy_id) << '\n'
         << std::quoted(policy.similarity_metric) << '\n'
         << std::quoted(policy.street_scope) << '\n'
         << perfect_recall_token(policy.perfect_recall) << '\n'
         << policy.entries.size() << '\n'
         << std::setprecision(std::numeric_limits<double>::max_digits10);
  for (const auto &entry : policy.entries) {
    output << std::quoted(entry.original_information_set) << ' '
           << std::quoted(entry.abstract_information_set) << ' ' << entry.aggregation_weight
           << '\n';
  }
  return Result<std::string, SolverError>::success(output.str());
}

Result<CardAbstractionPolicy, SolverError>
deserialize_card_abstraction_policy(const std::string &serialized) {
  std::istringstream input(serialized);
  std::string marker;
  CardAbstractionPolicy policy;
  if (!(input >> marker >> policy.major >> policy.minor) || marker != "GTOSD_CARD_ABSTRACTION") {
    return Result<CardAbstractionPolicy, SolverError>::failure(SolverError::InvalidAbstraction);
  }
  if (policy.major != CardAbstractionPolicy::format_major ||
      policy.minor > CardAbstractionPolicy::format_minor) {
    return Result<CardAbstractionPolicy, SolverError>::failure(
        SolverError::UnsupportedAbstractionVersion);
  }
  std::string perfect_recall;
  std::uint64_t entry_count = 0U;
  if (!(input >> std::quoted(policy.policy_id) >> std::quoted(policy.similarity_metric) >>
        std::quoted(policy.street_scope) >> perfect_recall >> entry_count) ||
      entry_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return Result<CardAbstractionPolicy, SolverError>::failure(SolverError::InvalidAbstraction);
  }
  const auto parsed_recall = parse_perfect_recall(perfect_recall);
  if (!parsed_recall) {
    return Result<CardAbstractionPolicy, SolverError>::failure(parsed_recall.error());
  }
  policy.perfect_recall = parsed_recall.value();
  policy.entries.reserve(static_cast<std::size_t>(entry_count));
  for (std::uint64_t index = 0; index < entry_count; ++index) {
    CardAbstractionEntry entry;
    if (!(input >> std::quoted(entry.original_information_set) >>
          std::quoted(entry.abstract_information_set) >> entry.aggregation_weight)) {
      return Result<CardAbstractionPolicy, SolverError>::failure(SolverError::InvalidAbstraction);
    }
    policy.entries.push_back(std::move(entry));
  }
  input >> std::ws;
  if (!input.eof()) {
    return Result<CardAbstractionPolicy, SolverError>::failure(SolverError::InvalidAbstraction);
  }
  return Result<CardAbstractionPolicy, SolverError>::success(std::move(policy));
}

std::string card_abstraction_policy_fingerprint(const CardAbstractionPolicy &policy) {
  const auto serialized = serialize_card_abstraction_policy(policy);
  if (!serialized) {
    return "invalid";
  }
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  hash_bytes(hash, serialized.value());
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
  return output.str();
}

Result<CardAbstractionByteModel, SolverError>
estimate_card_abstraction_bytes(const FiniteGame &game, const CardAbstractionPolicy &policy) {
  const auto validation = validate_card_abstraction_policy(game, policy);
  const auto serialized = serialize_card_abstraction_policy(policy);
  if (!validation || !serialized) {
    return Result<CardAbstractionByteModel, SolverError>::failure(!validation ? validation.error()
                                                                              : serialized.error());
  }
  CardAbstractionByteModel model;
  if (!checked_multiply(validation.value().original_information_sets, sizeof(std::uint32_t),
                        model.dense_mapping_bytes) ||
      !checked_multiply(validation.value().original_information_sets, sizeof(double),
                        model.aggregation_weight_bytes) ||
      !checked_multiply(validation.value().abstract_information_sets + 1U, sizeof(std::uint32_t),
                        model.bucket_offset_bytes)) {
    return Result<CardAbstractionByteModel, SolverError>::failure(SolverError::InvalidAbstraction);
  }
  model.serialized_policy_bytes = serialized.value().size();
  model.minimum_runtime_bytes = model.dense_mapping_bytes;
  if (!checked_add(model.minimum_runtime_bytes, model.aggregation_weight_bytes) ||
      !checked_add(model.minimum_runtime_bytes, model.bucket_offset_bytes)) {
    return Result<CardAbstractionByteModel, SolverError>::failure(SolverError::InvalidAbstraction);
  }
  return Result<CardAbstractionByteModel, SolverError>::success(model);
}

const char *perfect_recall_claim_name(const PerfectRecallClaim claim) noexcept {
  return perfect_recall_token(claim);
}

} // namespace gtosd
