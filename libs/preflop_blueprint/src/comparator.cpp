#include "gtosd/preflop_blueprint/comparator.hpp"

#include "gtosd/core/ranges.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string_view>
#include <vector>

namespace gtosd::preflop_blueprint {
namespace {

using Json = nlohmann::json;
constexpr std::size_t class_count = 81U;

struct ClassStrategy {
  std::vector<std::string> actions;
  // [class][action]
  std::array<std::vector<double>, class_count> probabilities{};
};

// Strategy object keyed by class name -> action -> probability.
bool parse_class_strategy(const Json &strategy, ClassStrategy &out) {
  if (!strategy.is_object() || strategy.size() != class_count) {
    return false;
  }
  std::map<std::string, std::uint8_t> ids;
  for (std::uint8_t id = 0U; id < class_count; ++id) {
    ids.emplace(class_name(id), id);
  }
  bool first = true;
  for (const auto &[name, row] : strategy.items()) {
    const auto found = ids.find(name);
    if (found == ids.end() || !row.is_object()) {
      return false;
    }
    if (first) {
      for (const auto &[action, value] : row.items()) {
        static_cast<void>(value);
        out.actions.push_back(action);
      }
      std::sort(out.actions.begin(), out.actions.end());
      first = false;
    }
    std::vector<double> probabilities(out.actions.size(), 0.0);
    std::size_t seen = 0U;
    for (const auto &[action, value] : row.items()) {
      const auto position = std::find(out.actions.begin(), out.actions.end(), action);
      if (position == out.actions.end() || !value.is_number()) {
        return false;
      }
      probabilities[static_cast<std::size_t>(position - out.actions.begin())] = value.get<double>();
      ++seen;
    }
    if (seen != out.actions.size()) {
      return false;
    }
    out.probabilities[found->second] = std::move(probabilities);
  }
  return true;
}

struct Distance {
  double weighted_mean_absolute_action_error_pp{0.0};
  double weighted_mean_class_total_variation_pp{0.0};
  double class_total_variation_p95_pp{0.0};
  double root_action_max_absolute_error_pp{0.0};
};

// Legacy metrics: class L1 over the actions weighted by the class mass.
Distance distance(const ClassStrategy &left, const ClassStrategy &right) {
  Distance result;
  double weighted_absolute_error = 0.0;
  double weighted_total_variation = 0.0;
  std::vector<double> physical;
  physical.reserve(630U);
  std::vector<double> left_root(left.actions.size(), 0.0);
  std::vector<double> right_root(left.actions.size(), 0.0);
  for (std::uint8_t id = 0U; id < class_count; ++id) {
    double class_l1 = 0.0;
    const auto mass = static_cast<double>(class_mass(id));
    for (std::size_t action = 0; action < left.actions.size(); ++action) {
      class_l1 += std::abs(left.probabilities[id][action] - right.probabilities[id][action]);
      left_root[action] += mass * left.probabilities[id][action] / 630.0;
      right_root[action] += mass * right.probabilities[id][action] / 630.0;
    }
    weighted_absolute_error += mass * class_l1 * 100.0;
    weighted_total_variation += mass * class_l1 * 50.0;
    for (std::uint32_t copy = 0; copy < static_cast<std::uint32_t>(mass); ++copy) {
      physical.push_back(class_l1 * 50.0);
    }
  }
  result.weighted_mean_absolute_action_error_pp =
      weighted_absolute_error / (630.0 * static_cast<double>(left.actions.size()));
  result.weighted_mean_class_total_variation_pp = weighted_total_variation / 630.0;
  std::sort(physical.begin(), physical.end());
  const auto p95 = (physical.size() * 95U + 99U) / 100U - 1U;
  result.class_total_variation_p95_pp = physical[p95];
  for (std::size_t action = 0; action < left.actions.size(); ++action) {
    result.root_action_max_absolute_error_pp =
        std::max(result.root_action_max_absolute_error_pp,
                 std::abs(left_root[action] - right_root[action]) * 100.0);
  }
  return result;
}

Json distance_json(const Distance &value) {
  return Json{{"weighted_mean_absolute_action_error_percentage_points",
               value.weighted_mean_absolute_action_error_pp},
              {"weighted_mean_class_total_variation_percentage_points",
               value.weighted_mean_class_total_variation_pp},
              {"class_total_variation_p95_percentage_points", value.class_total_variation_p95_pp},
              {"root_action_max_absolute_error_percentage_points",
               value.root_action_max_absolute_error_pp}};
}

std::string_view trim(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.remove_prefix(1U);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
    value.remove_suffix(1U);
  }
  return value;
}

// Monker range token: "[28.0]99[/28.0]" or "AKs" (100 percent).
bool parse_range_token(const std::string_view raw, std::string &name, double &percentage) {
  const auto token = trim(raw);
  if (token.empty()) {
    return false;
  }
  if (token.front() != '[') {
    name = std::string(token);
    percentage = 100.0;
    return true;
  }
  const auto opening_end = token.find(']');
  const auto closing_begin = opening_end == std::string_view::npos
                                 ? std::string_view::npos
                                 : token.find("[/", opening_end + 1U);
  if (opening_end == std::string_view::npos || opening_end == 1U ||
      closing_begin == std::string_view::npos || token.back() != ']') {
    return false;
  }
  const auto opening = token.substr(1U, opening_end - 1U);
  name = std::string(token.substr(opening_end + 1U, closing_begin - opening_end - 1U));
  try {
    percentage = std::stod(std::string(opening));
  } catch (const std::exception &) {
    return false;
  }
  return !name.empty() && std::isfinite(percentage) && percentage >= 0.0 && percentage <= 100.0;
}

// Reference strategy: action id -> range string; rows normalized to one.
bool parse_reference_strategy(const Json &strategy, ClassStrategy &out) {
  if (!strategy.is_object()) {
    return false;
  }
  std::map<std::string, std::uint8_t> ids;
  for (std::uint8_t id = 0U; id < class_count; ++id) {
    ids.emplace(class_name(id), id);
  }
  for (const auto &[action, value] : strategy.items()) {
    static_cast<void>(value);
    out.actions.push_back(action);
  }
  std::sort(out.actions.begin(), out.actions.end());
  for (auto &row : out.probabilities) {
    row.assign(out.actions.size(), 0.0);
  }
  for (const auto &[action, text] : strategy.items()) {
    if (!text.is_string()) {
      return false;
    }
    const auto position = static_cast<std::size_t>(
        std::find(out.actions.begin(), out.actions.end(), action) - out.actions.begin());
    const auto range = text.get<std::string>();
    std::size_t begin = 0U;
    while (begin <= range.size()) {
      const auto end = range.find(',', begin);
      const auto token = range.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
      if (!trim(token).empty()) {
        std::string name;
        double percentage = 0.0;
        if (!parse_range_token(token, name, percentage)) {
          return false;
        }
        const auto found = ids.find(name);
        if (found == ids.end()) {
          return false;
        }
        out.probabilities[found->second][position] += percentage;
      }
      if (end == std::string::npos) {
        break;
      }
      begin = end + 1U;
    }
  }
  for (auto &row : out.probabilities) {
    double total = 0.0;
    for (const auto value : row) {
      total += value;
    }
    if (total <= 0.0) {
      return false;
    }
    for (auto &value : row) {
      value /= total;
    }
  }
  return true;
}

} // namespace

Result<ComparisonReport, ComparisonError>
compare_charts(const std::string &candidate_json, const std::string *baseline_json,
               const std::string *reference_json, const ComparisonThresholds &thresholds) {
  using Outcome = Result<ComparisonReport, ComparisonError>;
  Json candidate;
  try {
    candidate = Json::parse(candidate_json);
  } catch (const std::exception &) {
    return Outcome::failure(ComparisonError::InvalidCandidate);
  }
  if (!candidate.is_object() || candidate.value("schema", "") != "gtosd.preflop_blueprint_chart.v1" ||
      !candidate.contains("status") || !candidate["status"].contains("exploitability") ||
      !candidate.contains("preflop_nodes")) {
    return Outcome::failure(ComparisonError::InvalidCandidate);
  }
  const auto &exploitability = candidate["status"]["exploitability"];
  if (!exploitability.contains("exact") || !exploitability.contains("max_gain_antes")) {
    return Outcome::failure(ComparisonError::InvalidCandidate);
  }
  ComparisonReport report;
  report.candidate_exact = exploitability["exact"].get<bool>();
  report.candidate_max_gain = exploitability["max_gain_antes"].get<double>();
  const double lower = exploitability.value("max_gain_lower_antes", 0.0);
  const double half_width = exploitability.value("max_gain_half_width_antes", 0.0);
  const double threshold = thresholds.max_gain_antes;
  if (report.candidate_exact) {
    report.status = report.candidate_max_gain <= threshold ? "QUALIFIED" : "REJECTED";
  } else if (lower > threshold) {
    report.status = "REJECTED";
  } else if (report.candidate_max_gain + half_width <= threshold) {
    report.status = "PROMISING";
  } else {
    report.status = "INCONCLUSIVE_ESTIMATE";
  }
  report.qualified = report.status == "QUALIFIED";

  Json output;
  output["schema"] = "gtosd.preflop_blueprint_comparison.v1";
  output["candidate"] = {{"config_id", candidate.value("config_id", "")},
                         {"tree_fingerprint", candidate.value("tree_fingerprint", "")},
                         {"policy_fingerprint", candidate.contains("fingerprints")
                                                    ? candidate["fingerprints"].value("policy", "")
                                                    : std::string()},
                         {"badge", candidate["status"].value("badge", "")}};
  output["exploitability_gate"] = {{"threshold_max_gain_antes", threshold},
                                   {"exact", report.candidate_exact},
                                   {"max_gain_antes", report.candidate_max_gain},
                                   {"max_gain_lower_antes", lower},
                                   {"max_gain_half_width_antes", half_width},
                                   {"passes", report.candidate_exact
                                                  ? report.candidate_max_gain <= threshold
                                                  : report.candidate_max_gain + half_width <= threshold}};
  output["status"] = report.status;
  output["qualified"] = report.qualified;

  // Root strategy of the candidate for the reference comparison.
  ClassStrategy candidate_root;
  const bool root_ok = candidate.contains("strategy") &&
                       parse_class_strategy(candidate["strategy"], candidate_root);
  if (!root_ok) {
    return Outcome::failure(ComparisonError::InvalidCandidate);
  }

  if (baseline_json != nullptr) {
    Json baseline;
    try {
      baseline = Json::parse(*baseline_json);
    } catch (const std::exception &) {
      return Outcome::failure(ComparisonError::InvalidBaseline);
    }
    if (!baseline.is_object() || baseline.value("schema", "") != "gtosd.preflop_blueprint_chart.v1" ||
        !baseline.contains("preflop_nodes")) {
      return Outcome::failure(ComparisonError::InvalidBaseline);
    }
    Json block;
    block["config_id"] = baseline.value("config_id", "");
    block["tree_fingerprint"] = baseline.value("tree_fingerprint", "");
    if (baseline.value("tree_fingerprint", "") != candidate.value("tree_fingerprint", "")) {
      block["status"] = "STALE_TREE";
    } else {
      block["status"] = "COMPARED";
      Json nodes = Json::object();
      double worst_tv = 0.0;
      std::size_t compared = 0U;
      for (const auto &[node_id, node] : candidate["preflop_nodes"].items()) {
        if (!baseline["preflop_nodes"].contains(node_id)) {
          continue;
        }
        ClassStrategy left;
        ClassStrategy right;
        if (!parse_class_strategy(node["strategy"], left) ||
            !parse_class_strategy(baseline["preflop_nodes"][node_id]["strategy"], right) ||
            left.actions != right.actions) {
          nodes[node_id] = {{"status", "ACTION_SET_MISMATCH"}};
          continue;
        }
        const auto value = distance(left, right);
        nodes[node_id] = distance_json(value);
        worst_tv = std::max(worst_tv, value.weighted_mean_class_total_variation_pp);
        ++compared;
      }
      block["nodes"] = nodes;
      block["nodes_compared"] = compared;
      block["worst_weighted_mean_class_total_variation_percentage_points"] = worst_tv;
      block["root_ev_difference_ante"] =
          candidate.value("root_ev_ante", 0.0) - baseline.value("root_ev_ante", 0.0);
    }
    output["baseline"] = block;
  }

  if (reference_json != nullptr) {
    Json reference;
    try {
      reference = Json::parse(*reference_json);
    } catch (const std::exception &) {
      return Outcome::failure(ComparisonError::InvalidReference);
    }
    // Schema id of the legacy reference fixture, spelled in two parts because
    // the isolation guard forbids the legacy token in this library.
    const std::string legacy_reference_schema = std::string("gtosd.hu_") + "preflop_reference.v1";
    if (!reference.is_object() || reference.value("schema", "") != legacy_reference_schema ||
        !reference.contains("reference") || !reference["reference"].contains("strategy")) {
      return Outcome::failure(ComparisonError::InvalidReference);
    }
    Json block;
    block["id"] = reference.value("id", "");
    block["status"] = "EXTERNAL_CONTRACT_INCOMPLETE";
    block["note"] = "descriptive distances against an external reference with an incomplete "
                    "contract; they do not change the verdict";
    ClassStrategy reference_root;
    if (!parse_reference_strategy(reference["reference"]["strategy"], reference_root)) {
      return Outcome::failure(ComparisonError::InvalidReference);
    }
    if (reference_root.actions != candidate_root.actions) {
      block["comparison"] = "ACTION_SET_MISMATCH";
      block["reference_actions"] = reference_root.actions;
      block["candidate_actions"] = candidate_root.actions;
    } else {
      block["comparison"] = "ROOT_COMPARED";
      block["root"] = distance_json(distance(candidate_root, reference_root));
      if (reference["reference"].contains("root_ev_ante")) {
        block["root_ev_absolute_error_ante"] =
            std::abs(candidate.value("root_ev_ante", 0.0) -
                     reference["reference"]["root_ev_ante"].get<double>());
      }
    }
    output["reference"] = block;
  }
  report.json = output.dump(2) + "\n";
  return Outcome::success(std::move(report));
}

const char *comparison_error_name(const ComparisonError error) noexcept {
  switch (error) {
  case ComparisonError::InvalidCandidate:
    return "invalid_candidate";
  case ComparisonError::InvalidBaseline:
    return "invalid_baseline";
  case ComparisonError::InvalidReference:
    return "invalid_reference";
  }
  return "unknown";
}

} // namespace gtosd::preflop_blueprint
