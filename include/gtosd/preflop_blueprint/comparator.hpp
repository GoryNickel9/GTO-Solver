#pragma once

#include "gtosd/core/result.hpp"

#include <cstdint>
#include <string>

// Comparator of chart exports (roadmap P8.4). The verdict depends only on
// the physical exploitability declared by the candidate (decision D2: the
// maximum physical gain must not exceed the threshold, 0.1 antes per hand by
// default) and on the certification status:
//
//   QUALIFIED             exact certificate, max gain within the threshold
//   REJECTED              exact certificate above the threshold, or a sampled
//                         lower bound above it
//   PROMISING             sampled estimate plus half-width within the
//                         threshold, no exact certificate yet
//   INCONCLUSIVE_ESTIMATE sampled estimate above the threshold but lower
//                         bound within it (selection bias possible)
//
// A baseline chart (another blueprint) is compared node by node on the
// common preflop nodes (class total variation and weighted mean absolute
// action error, class masses 6/4/12); a different tree is reported as
// STALE_TREE and skipped. A Monker reference (the legacy CO40 reference
// fixture format) is compared at the root with the legacy metrics; the
// distances are
// descriptive only, with status EXTERNAL_CONTRACT_INCOMPLETE, and never
// change the verdict.
namespace gtosd::preflop_blueprint {

enum class ComparisonError : std::uint8_t {
  InvalidCandidate,
  InvalidBaseline,
  InvalidReference
};

struct ComparisonThresholds {
  double max_gain_antes{0.1};
};

struct ComparisonReport {
  std::string status;
  bool qualified{false};
  double candidate_max_gain{0.0};
  bool candidate_exact{false};
  std::string json;
};

[[nodiscard]] Result<ComparisonReport, ComparisonError>
compare_charts(const std::string &candidate_json, const std::string *baseline_json,
               const std::string *reference_json, const ComparisonThresholds &thresholds);

[[nodiscard]] const char *comparison_error_name(ComparisonError error) noexcept;

} // namespace gtosd::preflop_blueprint
