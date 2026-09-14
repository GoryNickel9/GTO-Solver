#include "gtosd/preflop/hu_preflop_legacy.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>

namespace gtosd {
namespace {

struct PolicyDecisionContext {
  std::uint64_t public_history{0U};
  std::uint8_t player{0U};
  Street street{Street::Flop};

  friend bool operator==(const PolicyDecisionContext &, const PolicyDecisionContext &) = default;
};

struct PolicyPublicHistory {
  std::uint64_t public_history{0U};
  Street street{Street::Flop};

  friend bool operator==(const PolicyPublicHistory &, const PolicyPublicHistory &) = default;
};

std::size_t postflop_street_index(const Street street) {
  return static_cast<std::size_t>(street) - static_cast<std::size_t>(Street::Flop);
}

bool valid_validated_policy(const HuPreflopValidatedSampledPostflopPolicy &validated) {
  return validated.policy && !validated.policy_fingerprint.empty() &&
         validated.policy_fingerprint == validated.policy->fingerprint &&
         validated.tree_fingerprint == validated.policy->tree_fingerprint &&
         validated.iterations == validated.policy->iterations;
}

} // namespace

Result<HuPreflopSampledPolicyReuseCensus, HuPreflopError>
analyze_hu_preflop_sampled_policy_reuse(
    const HuPreflopValidatedSampledPostflopPolicy &validated) {
  if (!valid_validated_policy(validated)) {
    return Result<HuPreflopSampledPolicyReuseCensus, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }

  HuPreflopSampledPolicyReuseCensus result;
  result.policy_fingerprint = validated.policy_fingerprint;
  result.trained_information_sets = validated.policy->entries.size();
  if (validated.policy->entries.empty()) {
    return Result<HuPreflopSampledPolicyReuseCensus, HuPreflopError>::success(
        std::move(result));
  }

  std::vector<PolicyDecisionContext> contexts;
  std::vector<PolicyPublicHistory> histories;
  try {
    contexts.reserve(validated.policy->entries.size());
    histories.reserve(validated.policy->entries.size());
    for (const auto &entry : validated.policy->entries) {
      if (entry.key.player > 1U || entry.key.street < Street::Flop ||
          entry.key.street > Street::River) {
        return Result<HuPreflopSampledPolicyReuseCensus, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      const auto street = postflop_street_index(entry.key.street);
      ++result.trained_information_sets_by_street[street];
      ++result.trained_information_sets_by_street_player[street][entry.key.player];
      contexts.push_back({entry.key.public_history, entry.key.player, entry.key.street});
      histories.push_back({entry.key.public_history, entry.key.street});
    }
  } catch (const std::bad_alloc &) {
    return Result<HuPreflopSampledPolicyReuseCensus, HuPreflopError>::failure(
        HuPreflopError::MemoryFailure);
  }

  std::ranges::sort(contexts, [](const auto &left, const auto &right) {
    return std::tie(left.public_history, left.player, left.street) <
           std::tie(right.public_history, right.player, right.street);
  });
  std::ranges::sort(histories, [](const auto &left, const auto &right) {
    return std::tie(left.street, left.public_history) <
           std::tie(right.street, right.public_history);
  });

  result.minimum_information_sets_per_context = std::numeric_limits<std::uint64_t>::max();
  for (std::size_t first = 0U; first < contexts.size();) {
    auto last = first + 1U;
    while (last < contexts.size() && contexts[last] == contexts[first]) {
      ++last;
    }
    const auto count = static_cast<std::uint64_t>(last - first);
    const auto street = postflop_street_index(contexts[first].street);
    ++result.trained_decision_contexts;
    ++result.trained_decision_contexts_by_street[street];
    ++result.trained_decision_contexts_by_street_player[street][contexts[first].player];
    result.minimum_information_sets_per_context =
        std::min(result.minimum_information_sets_per_context, count);
    result.maximum_information_sets_per_context =
        std::max(result.maximum_information_sets_per_context, count);
    first = last;
  }
  for (std::size_t first = 0U; first < histories.size();) {
    auto last = first + 1U;
    while (last < histories.size() && histories[last] == histories[first]) {
      ++last;
    }
    ++result.distinct_public_histories_by_street[postflop_street_index(histories[first].street)];
    first = last;
  }
  result.mean_information_sets_per_context =
      static_cast<double>(result.trained_information_sets) /
      static_cast<double>(result.trained_decision_contexts);
  if (!std::isfinite(result.mean_information_sets_per_context)) {
    return Result<HuPreflopSampledPolicyReuseCensus, HuPreflopError>::failure(
        HuPreflopError::NumericalFailure);
  }
  return Result<HuPreflopSampledPolicyReuseCensus, HuPreflopError>::success(std::move(result));
}

} // namespace gtosd
