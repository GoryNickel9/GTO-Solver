#include "gtosd/postflop/postflop_subgame.hpp"
#include "gtosd/solver/best_response.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {

using Json = nlohmann::json;

constexpr std::string_view corpus_schema = "gtosd.river_bucket_qualification_corpus.v1";
constexpr std::string_view report_schema = "gtosd.river_bucket_qualification_report.v1";
constexpr std::string_view exact_blocker_corpus_schema =
    "gtosd.river_exact_blocker_qualification_corpus.v2";
constexpr std::string_view exact_blocker_report_schema =
    "gtosd.river_exact_blocker_qualification_report.v2";
constexpr std::string_view showdown_distribution_corpus_schema =
    "gtosd.river_showdown_distribution_qualification_corpus.v3";
constexpr std::string_view showdown_distribution_report_schema =
    "gtosd.river_showdown_distribution_qualification_report.v3";
constexpr std::string_view equitable_feasibility_report_schema =
    "gtosd.river_joint_equitable_feasibility_report.v1";

struct Limits {
  std::uint64_t maximum_physical_deals{0};
  std::uint64_t maximum_bucket_pairs{0};
  std::uint64_t maximum_materialized_validation_nodes{0};
};

struct Thresholds {
  double maximum_exact_normalized_nash_conv{0.0};
  double maximum_bucket_normalized_nash_conv{0.0};
  double maximum_normalized_nash_conv_delta{0.0};
  double maximum_normalized_profile_value_delta{0.0};
  double maximum_normalized_best_response_delta{0.0};
  double minimum_operational_speedup{0.0};
  double minimum_node_reduction_ratio{0.0};
  std::uint64_t maximum_native_byte_model{0};
};

struct CorpusContract {
  std::uint64_t iterations{0};
  std::uint64_t repetitions{0};
  std::uint64_t solver_seed{0};
  gtosd::PostflopRiverBucketAbstraction abstraction{
      gtosd::PostflopRiverBucketAbstraction::MadeHandValueV1};
  std::uint16_t distribution_quantization_basis_points{500};
  Limits limits;
  Thresholds thresholds;
};

struct Fixture {
  std::string id;
  std::string stratum;
  std::string suite;
  gtosd::PostflopTreeConfig config;
  gtosd::PostflopRanges ranges;
};

[[noreturn]] void fail(const std::string &message);

struct PhaseMemorySample {
  std::uint64_t baseline_rss_bytes{0};
  std::uint64_t peak_rss_bytes{0};
  std::uint64_t peak_rss_delta_bytes{0};
};

class PhaseRssSampler {
public:
  PhaseRssSampler()
      : baseline_(gtosd::process_current_rss_bytes()), peak_(baseline_), worker_([this] {
          while (!stop_.load(std::memory_order_relaxed)) {
            observe();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
          observe();
        }) {}

  PhaseRssSampler(const PhaseRssSampler &) = delete;
  PhaseRssSampler &operator=(const PhaseRssSampler &) = delete;

  ~PhaseRssSampler() {
    if (worker_.joinable()) {
      stop_.store(true, std::memory_order_relaxed);
      worker_.join();
    }
  }

  PhaseMemorySample finish() {
    stop_.store(true, std::memory_order_relaxed);
    worker_.join();
    observe();
    const auto peak = peak_.load(std::memory_order_relaxed);
    return {baseline_, peak, peak > baseline_ ? peak - baseline_ : 0U};
  }

private:
  void observe() {
    const auto current = gtosd::process_current_rss_bytes();
    auto observed = peak_.load(std::memory_order_relaxed);
    while (current > observed &&
           !peak_.compare_exchange_weak(observed, current, std::memory_order_relaxed)) {
    }
  }

  std::uint64_t baseline_{0};
  std::atomic<std::uint64_t> peak_{0};
  std::atomic<bool> stop_{false};
  std::thread worker_;
};

struct InformationSetCfv {
  std::uint8_t player{0};
  std::vector<gtosd::GameActionId> actions;
  double counterfactual_reach{0.0};
  double strategy_value_numerator{0.0};
  std::vector<double> action_value_numerators;
};

struct InformationSetCfvProfile {
  std::map<std::string, InformationSetCfv> rows;
  std::array<double, 2> root_value{};
};

std::array<double, 2> evaluate_nodes(const gtosd::FiniteGame &game,
                                     const gtosd::StrategyProfile &profile,
                                     const gtosd::GameNodeId node_id,
                                     std::vector<std::optional<std::array<double, 2>>> &cache) {
  if (cache[node_id]) {
    return *cache[node_id];
  }
  const auto &node = game.nodes[node_id];
  if (node.kind == gtosd::GameNodeKind::Terminal) {
    cache[node_id] = node.payoff;
    return node.payoff;
  }

  std::array<double, 2> value{};
  if (node.kind == gtosd::GameNodeKind::Chance) {
    for (const auto &edge : node.edges) {
      const auto child = evaluate_nodes(game, profile, edge.child, cache);
      value[0] += edge.probability * child[0];
      value[1] += edge.probability * child[1];
    }
  } else {
    const auto strategy = profile.find(node.information_set);
    if (strategy == profile.end() || strategy->second.actions.size() != node.edges.size() ||
        strategy->second.probabilities.size() != node.edges.size()) {
      fail("missing strategy while evaluating counterfactual values");
    }
    for (std::size_t action = 0U; action < node.edges.size(); ++action) {
      if (strategy->second.actions[action] != node.edges[action].action.id) {
        fail("action mismatch while evaluating counterfactual values");
      }
      const auto child = evaluate_nodes(game, profile, node.edges[action].child, cache);
      value[0] += strategy->second.probabilities[action] * child[0];
      value[1] += strategy->second.probabilities[action] * child[1];
    }
  }
  cache[node_id] = value;
  return value;
}

void accumulate_counterfactual_values(
    const gtosd::FiniteGame &game, const gtosd::StrategyProfile &profile,
    const std::vector<std::optional<std::array<double, 2>>> &node_values,
    const gtosd::GameNodeId node_id, const double chance_reach,
    const std::array<double, 2> &player_reach, std::map<std::string, InformationSetCfv> &result) {
  const auto &node = game.nodes[node_id];
  if (node.kind == gtosd::GameNodeKind::Terminal) {
    return;
  }
  if (node.kind == gtosd::GameNodeKind::Chance) {
    for (const auto &edge : node.edges) {
      accumulate_counterfactual_values(game, profile, node_values, edge.child,
                                       chance_reach * edge.probability, player_reach, result);
    }
    return;
  }

  const auto strategy = profile.find(node.information_set);
  if (strategy == profile.end() || !node_values[node_id]) {
    fail("invalid decision while accumulating counterfactual values");
  }
  auto [entry, inserted] = result.try_emplace(node.information_set);
  auto &row = entry->second;
  if (inserted) {
    row.player = node.player;
    row.actions = strategy->second.actions;
    row.action_value_numerators.assign(node.edges.size(), 0.0);
  } else if (row.player != node.player || row.actions != strategy->second.actions) {
    fail("incompatible nodes share an information set in CFV diagnostic");
  }

  const double counterfactual_reach = chance_reach * player_reach[1U - node.player];
  row.counterfactual_reach += counterfactual_reach;
  row.strategy_value_numerator += counterfactual_reach * (*node_values[node_id])[node.player];
  for (std::size_t action = 0U; action < node.edges.size(); ++action) {
    const auto &child_value = node_values[node.edges[action].child];
    if (!child_value) {
      fail("missing child value in CFV diagnostic");
    }
    row.action_value_numerators[action] += counterfactual_reach * (*child_value)[node.player];
    auto next_reach = player_reach;
    next_reach[node.player] *= strategy->second.probabilities[action];
    accumulate_counterfactual_values(game, profile, node_values, node.edges[action].child,
                                     chance_reach, next_reach, result);
  }
}

InformationSetCfvProfile evaluate_information_set_cfvs(const gtosd::FiniteGame &game,
                                                       const gtosd::StrategyProfile &profile) {
  const auto valid = gtosd::validate_strategy_profile(game, profile);
  if (!valid) {
    fail("invalid strategy supplied to CFV diagnostic");
  }
  std::vector<std::optional<std::array<double, 2>>> node_values(game.nodes.size());
  InformationSetCfvProfile result;
  result.root_value = evaluate_nodes(game, profile, game.root, node_values);
  accumulate_counterfactual_values(game, profile, node_values, game.root, 1.0, {1.0, 1.0},
                                   result.rows);
  return result;
}

Json memory_samples_json(const std::vector<PhaseMemorySample> &samples) {
  Json result = Json::array();
  for (const auto &sample : samples) {
    result.push_back({{"baseline_rss_bytes", sample.baseline_rss_bytes},
                      {"peak_rss_bytes", sample.peak_rss_bytes},
                      {"peak_rss_delta_bytes", sample.peak_rss_delta_bytes}});
  }
  return result;
}

[[noreturn]] void fail(const std::string &message) { throw std::runtime_error(message); }

Json compare_root_information_sets(const gtosd::PostflopSubgameProjection &projection,
                                   const gtosd::StrategyProfile &exact_profile,
                                   const gtosd::StrategyProfile &bucket_profile,
                                   const double normalization_pot) {
  if (!(normalization_pot > 0.0) || !std::isfinite(normalization_pot)) {
    fail("invalid root diagnostic normalization pot");
  }
  const auto exact_cfvs = evaluate_information_set_cfvs(projection.game, exact_profile);
  const auto bucket_cfvs = evaluate_information_set_cfvs(projection.game, bucket_profile);

  double total_weight = 0.0;
  double weighted_strategy_tv = 0.0;
  double maximum_strategy_tv = 0.0;
  double weighted_cfv_error = 0.0;
  double maximum_cfv_error = 0.0;
  double weighted_action_cfv_error = 0.0;
  double maximum_action_cfv_error = 0.0;
  std::vector<double> exact_root_strategy;
  std::vector<double> bucket_root_strategy;
  std::vector<gtosd::GameActionId> root_actions;
  std::vector<Json> rows;
  std::set<std::string> seen;
  std::optional<std::uint8_t> root_player;
  double weighted_exact_root_value = 0.0;
  double weighted_bucket_root_value = 0.0;

  for (const auto &metadata : projection.information_sets) {
    if (metadata.source_public_node != projection.source_root ||
        !seen.insert(metadata.information_set).second) {
      continue;
    }
    const auto exact_row = exact_cfvs.rows.find(metadata.information_set);
    const auto bucket_row = bucket_cfvs.rows.find(metadata.information_set);
    const auto exact_strategy = exact_profile.find(metadata.information_set);
    const auto bucket_strategy = bucket_profile.find(metadata.information_set);
    if (exact_row == exact_cfvs.rows.end() || bucket_row == bucket_cfvs.rows.end() ||
        exact_strategy == exact_profile.end() || bucket_strategy == bucket_profile.end() ||
        exact_row->second.counterfactual_reach <= 0.0 ||
        bucket_row->second.counterfactual_reach <= 0.0 ||
        exact_strategy->second.actions != bucket_strategy->second.actions ||
        exact_strategy->second.probabilities.size() !=
            bucket_strategy->second.probabilities.size() ||
        exact_row->second.action_value_numerators.size() !=
            bucket_row->second.action_value_numerators.size()) {
      fail("incomplete root information-set diagnostic");
    }
    const auto &exact = exact_row->second;
    const auto &bucket = bucket_row->second;
    if (!root_player) {
      root_player = exact.player;
    } else if (*root_player != exact.player || exact.player != bucket.player) {
      fail("root information sets do not have one acting player");
    }
    if (std::abs(exact.counterfactual_reach - bucket.counterfactual_reach) > 1.0e-12) {
      fail("root counterfactual reach changed after lifting bucket strategy");
    }
    if (root_actions.empty()) {
      root_actions = exact_strategy->second.actions;
      exact_root_strategy.assign(root_actions.size(), 0.0);
      bucket_root_strategy.assign(root_actions.size(), 0.0);
    } else if (root_actions != exact_strategy->second.actions) {
      fail("root information sets do not share one action signature");
    }

    const double weight = exact.counterfactual_reach;
    const double exact_value = exact.strategy_value_numerator / weight;
    const double bucket_value = bucket.strategy_value_numerator / weight;
    const double cfv_error = std::abs(exact_value - bucket_value) / normalization_pot;
    double strategy_l1 = 0.0;
    double action_cfv_error = 0.0;
    for (std::size_t action = 0U; action < root_actions.size(); ++action) {
      const double exact_probability = exact_strategy->second.probabilities[action];
      const double bucket_probability = bucket_strategy->second.probabilities[action];
      strategy_l1 += std::abs(exact_probability - bucket_probability);
      exact_root_strategy[action] += weight * exact_probability;
      bucket_root_strategy[action] += weight * bucket_probability;
      const double exact_action_value = exact.action_value_numerators[action] / weight;
      const double bucket_action_value = bucket.action_value_numerators[action] / weight;
      action_cfv_error = std::max(
          action_cfv_error, std::abs(exact_action_value - bucket_action_value) / normalization_pot);
    }
    const double strategy_tv = strategy_l1 * 0.5;
    total_weight += weight;
    weighted_strategy_tv += weight * strategy_tv;
    maximum_strategy_tv = std::max(maximum_strategy_tv, strategy_tv);
    weighted_cfv_error += weight * cfv_error;
    weighted_exact_root_value += weight * exact_value;
    weighted_bucket_root_value += weight * bucket_value;
    maximum_cfv_error = std::max(maximum_cfv_error, cfv_error);
    weighted_action_cfv_error += weight * action_cfv_error;
    maximum_action_cfv_error = std::max(maximum_action_cfv_error, action_cfv_error);
    rows.push_back({{"information_set", metadata.information_set},
                    {"combo_id", metadata.combo},
                    {"counterfactual_reach", weight},
                    {"exact_cfv_antes", exact_value},
                    {"bucket_cfv_antes", bucket_value},
                    {"normalized_absolute_cfv_error", cfv_error},
                    {"strategy_total_variation", strategy_tv},
                    {"maximum_normalized_action_cfv_error", action_cfv_error},
                    {"exact_strategy", exact_strategy->second.probabilities},
                    {"bucket_strategy", bucket_strategy->second.probabilities}});
  }
  if (!(total_weight > 0.0) || root_actions.empty()) {
    fail("root diagnostic found no reachable information sets");
  }
  for (auto &probability : exact_root_strategy) {
    probability /= total_weight;
  }
  for (auto &probability : bucket_root_strategy) {
    probability /= total_weight;
  }
  const double exact_probability_sum =
      std::accumulate(exact_root_strategy.begin(), exact_root_strategy.end(), 0.0);
  const double bucket_probability_sum =
      std::accumulate(bucket_root_strategy.begin(), bucket_root_strategy.end(), 0.0);
  const double exact_root_value = weighted_exact_root_value / total_weight;
  const double bucket_root_value = weighted_bucket_root_value / total_weight;
  if (!root_player || std::abs(exact_probability_sum - 1.0) > 1.0e-10 ||
      std::abs(bucket_probability_sum - 1.0) > 1.0e-10 ||
      std::abs(exact_root_value - exact_cfvs.root_value[*root_player]) > 1.0e-10 ||
      std::abs(bucket_root_value - bucket_cfvs.root_value[*root_player]) > 1.0e-10) {
    fail("root CFV diagnostic does not reconstruct the profile value");
  }
  std::ranges::sort(rows, [](const Json &left, const Json &right) {
    return left.at("normalized_absolute_cfv_error").get<double>() >
           right.at("normalized_absolute_cfv_error").get<double>();
  });
  if (rows.size() > 10U) {
    rows.erase(rows.begin() + 10U, rows.end());
  }

  return {{"root_information_sets", seen.size()},
          {"counterfactual_reach_sum", total_weight},
          {"player", *root_player},
          {"action_ids", root_actions},
          {"exact_aggregate_strategy", exact_root_strategy},
          {"bucket_aggregate_strategy", bucket_root_strategy},
          {"weighted_mean_strategy_total_variation", weighted_strategy_tv / total_weight},
          {"maximum_strategy_total_variation", maximum_strategy_tv},
          {"weighted_mean_normalized_cfv_error", weighted_cfv_error / total_weight},
          {"maximum_normalized_cfv_error", maximum_cfv_error},
          {"weighted_mean_maximum_normalized_action_cfv_error",
           weighted_action_cfv_error / total_weight},
          {"maximum_normalized_action_cfv_error", maximum_action_cfv_error},
          {"exact_root_value_antes", exact_cfvs.root_value},
          {"bucket_root_value_antes", bucket_cfvs.root_value},
          {"largest_cfv_error_rows", std::move(rows)}};
}

std::string read_text(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    fail("cannot open manifest: " + path.string());
  }
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::uint64_t fnv1a64(const std::string_view text) {
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  for (const char byte : text) {
    hash ^= static_cast<std::uint8_t>(byte);
    hash *= 1'099'511'628'211ULL;
  }
  return hash;
}

std::string fingerprint(const std::string_view text) {
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << fnv1a64(text);
  return output.str();
}

std::uint64_t splitmix64(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

gtosd::Money money_from_antes(const std::int64_t value, const std::string_view field) {
  const auto result = gtosd::Money::from_antes(value);
  if (!result) {
    fail("invalid money field: " + std::string(field));
  }
  return result.value();
}

gtosd::RangeWeight range_weight(const std::int64_t basis_points) {
  const auto result = gtosd::RangeWeight::from_basis_points(basis_points);
  if (!result || basis_points == 0) {
    fail("invalid positive range weight");
  }
  return result.value();
}

gtosd::PotPercentage pot_percentage(const std::int64_t basis_points) {
  const auto result = gtosd::PotPercentage::from_basis_points(basis_points);
  if (!result || basis_points == 0) {
    fail("invalid positive pot percentage");
  }
  return result.value();
}

std::vector<std::int64_t> positive_integer_array(const Json &value, const std::string_view field) {
  if (!value.is_array() || value.empty()) {
    fail("invalid array field: " + std::string(field));
  }
  std::vector<std::int64_t> result;
  result.reserve(value.size());
  for (const auto &entry : value) {
    if (!entry.is_number_integer()) {
      fail("non-integer array entry: " + std::string(field));
    }
    const auto parsed = entry.get<std::int64_t>();
    if (parsed <= 0) {
      fail("non-positive array entry: " + std::string(field));
    }
    result.push_back(parsed);
  }
  return result;
}

gtosd::PostflopTreeConfig make_config(const Json &fixture) {
  const auto &board_json = fixture.at("board");
  if (!board_json.is_array() || board_json.size() != 5U) {
    fail("fixture board must contain five cards");
  }
  std::array<gtosd::CardId, 5> board{};
  std::uint64_t board_mask = 0U;
  for (std::size_t index = 0U; index < board.size(); ++index) {
    const auto parsed = gtosd::parse_card(board_json[index].get<std::string>());
    if (!parsed || (parsed.value().mask() & board_mask) != 0U) {
      fail("invalid or duplicate board card");
    }
    board[index] = parsed.value();
    board_mask |= parsed.value().mask();
  }

  gtosd::PostflopTreeConfig config;
  config.flop = {board[0], board[1], board[2]};
  config.turn = board[3];
  config.river = board[4];
  config.initial_pot =
      money_from_antes(fixture.at("initial_pot_antes").get<std::int64_t>(), "initial_pot");
  config.effective_stack =
      money_from_antes(fixture.at("effective_stack_antes").get<std::int64_t>(), "effective_stack");
  const auto rake_basis_points = fixture.at("rake_bp").get<std::int64_t>();
  config.rake.enabled = rake_basis_points != 0;
  const auto rake_percentage = gtosd::RangeWeight::from_basis_points(rake_basis_points);
  if (!rake_percentage) {
    fail("invalid rake basis points");
  }
  config.rake.percentage = rake_percentage.value();
  config.rake.cap = money_from_antes(fixture.at("rake_cap_antes").get<std::int64_t>(), "rake_cap");
  config.rake.no_flop_no_drop = true;

  const auto minimum_bet = money_from_antes(1, "minimum_bet");
  for (auto &street : config.streets) {
    for (auto &player : street.players) {
      for (auto &scenario : player) {
        scenario.minimum_bet = minimum_bet;
        scenario.all_in_mode = gtosd::AllInMode::Disabled;
      }
    }
  }
  const auto raise_depth_value = fixture.at("raise_depth").get<std::int64_t>();
  if (raise_depth_value < 0 || raise_depth_value > 4) {
    fail("invalid raise_depth");
  }
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    const auto key = player == 0U ? "player0_sizes_bp" : "player1_sizes_bp";
    const auto sizes = positive_integer_array(fixture.at(key), key);
    if (sizes.size() > 3U) {
      fail("too many river sizes");
    }
    for (auto &scenario : config.streets[2].players[player]) {
      scenario.raise_depth = static_cast<std::uint8_t>(raise_depth_value);
      for (const auto size : sizes) {
        scenario.aggressive_sizes.push_back(pot_percentage(size));
      }
    }
  }
  if (!gtosd::validate_tree_config(config)) {
    fail("invalid generated tree config");
  }
  return config;
}

gtosd::PostflopRanges make_ranges(const Json &fixture, const gtosd::PostflopTreeConfig &config) {
  const auto combo_count = fixture.at("combos_per_player").get<std::size_t>();
  const auto combos = gtosd::all_combos();
  if (combo_count == 0U || combo_count > combos.size()) {
    fail("invalid combos_per_player");
  }
  const auto seed = fixture.at("range_seed").get<std::uint64_t>();
  std::uint64_t board_mask = 0U;
  for (const auto card : gtosd::configured_board(config)) {
    board_mask |= card.mask();
  }
  gtosd::PostflopRanges ranges;
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    const auto weight_key = player == 0U ? "player0_weights_bp" : "player1_weights_bp";
    const auto weights = positive_integer_array(fixture.at(weight_key), weight_key);
    struct Candidate {
      gtosd::ComboId combo{0};
      std::uint64_t score{0};
    };
    std::vector<Candidate> candidates;
    candidates.reserve(combos.size());
    const auto player_salt = player == 0U ? 0x243f6a8885a308d3ULL : 0x13198a2e03707344ULL;
    for (std::size_t combo = 0U; combo < combos.size(); ++combo) {
      const auto mask = combos[combo].first.mask() | combos[combo].second.mask();
      if ((mask & board_mask) != 0U) {
        continue;
      }
      const auto id = static_cast<gtosd::ComboId>(combo);
      candidates.push_back(
          {id, splitmix64(seed ^ player_salt ^
                          (static_cast<std::uint64_t>(id) + 1U) * 0x9e3779b97f4a7c15ULL)});
    }
    std::ranges::sort(candidates, [](const Candidate &left, const Candidate &right) {
      return left.score < right.score || (left.score == right.score && left.combo < right.combo);
    });
    if (candidates.size() < combo_count) {
      fail("not enough legal combos for generated range");
    }
    for (std::size_t index = 0U; index < combo_count; ++index) {
      const auto combo = candidates[index].combo;
      const auto weight_index = static_cast<std::size_t>(
          splitmix64(seed ^ player_salt ^ static_cast<std::uint64_t>(combo) ^
                     0xa4093822299f31d0ULL) %
          weights.size());
      ranges.players[player][combo] = range_weight(weights[weight_index]);
    }
  }
  if (!gtosd::validate_postflop_ranges(config, ranges)) {
    fail("invalid generated ranges");
  }
  return ranges;
}

CorpusContract parse_contract(const Json &manifest) {
  const auto schema = manifest.value("schema", std::string{});
  const bool made_hand_v1 = schema == corpus_schema;
  const bool exact_blocker_v2 = schema == exact_blocker_corpus_schema;
  const bool showdown_distribution_v3 = schema == showdown_distribution_corpus_schema;
  if (!made_hand_v1 && !exact_blocker_v2 && !showdown_distribution_v3) {
    fail("unsupported corpus schema");
  }
  if (exact_blocker_v2 &&
      manifest.at("abstraction").value("mode", std::string{}) != "exact_blocker_signature_v2") {
    fail("invalid exact blocker abstraction contract");
  }
  if (showdown_distribution_v3 &&
      manifest.at("abstraction").value("mode", std::string{}) != "showdown_distribution_v3") {
    fail("invalid showdown distribution abstraction contract");
  }
  const auto &selection = manifest.at("selection");
  if (selection.value("solver_outputs_used", true) || selection.value("startup_included", true)) {
    fail("corpus must be selected without solver output and exclude startup");
  }
  const auto &solver = manifest.at("solver");
  if (solver.value("algorithm", std::string{}) != "ProductionDcfr" ||
      solver.at("positive_regret_exponent").get<double>() != 1.5 ||
      solver.at("negative_regret_exponent").get<double>() != 0.0 ||
      solver.at("strategy_exponent").get<double>() != 3.0) {
    fail("unsupported solver contract");
  }
  CorpusContract result;
  result.abstraction = made_hand_v1 ? gtosd::PostflopRiverBucketAbstraction::MadeHandValueV1
                       : exact_blocker_v2
                           ? gtosd::PostflopRiverBucketAbstraction::ExactBlockerSignatureV2
                           : gtosd::PostflopRiverBucketAbstraction::ShowdownDistributionV3;
  if (showdown_distribution_v3) {
    const auto quantum = manifest.at("abstraction")
                             .at("distribution_quantization_basis_points")
                             .get<std::uint64_t>();
    if (quantum == 0U || quantum > 10'000U) {
      fail("invalid showdown distribution quantization");
    }
    result.distribution_quantization_basis_points = static_cast<std::uint16_t>(quantum);
  }
  result.iterations = solver.at("iterations").get<std::uint64_t>();
  result.repetitions = solver.at("timing_repetitions").get<std::uint64_t>();
  result.solver_seed = solver.at("seed").get<std::uint64_t>();
  if (result.iterations == 0U || result.repetitions < 3U || result.repetitions > 11U ||
      result.repetitions % 2U == 0U) {
    fail("iterations and repetitions violate the qualification contract");
  }
  const auto &limits = manifest.at("limits");
  result.limits.maximum_physical_deals = limits.at("maximum_physical_deals").get<std::uint64_t>();
  result.limits.maximum_bucket_pairs = limits.at("maximum_bucket_pairs").get<std::uint64_t>();
  result.limits.maximum_materialized_validation_nodes =
      limits.at("maximum_materialized_validation_nodes").get<std::uint64_t>();
  const auto &thresholds = manifest.at("thresholds");
  result.thresholds.maximum_exact_normalized_nash_conv =
      thresholds.at("maximum_exact_normalized_nash_conv").get<double>();
  result.thresholds.maximum_bucket_normalized_nash_conv =
      thresholds.at("maximum_bucket_normalized_nash_conv").get<double>();
  result.thresholds.maximum_normalized_nash_conv_delta =
      thresholds.at("maximum_normalized_nash_conv_delta").get<double>();
  result.thresholds.maximum_normalized_profile_value_delta =
      thresholds.at("maximum_normalized_profile_value_delta").get<double>();
  result.thresholds.maximum_normalized_best_response_delta =
      thresholds.at("maximum_normalized_best_response_delta").get<double>();
  result.thresholds.minimum_operational_speedup =
      thresholds.at("minimum_operational_speedup").get<double>();
  result.thresholds.minimum_node_reduction_ratio =
      thresholds.at("minimum_node_reduction_ratio").get<double>();
  result.thresholds.maximum_native_byte_model =
      thresholds.at("maximum_native_byte_model").get<std::uint64_t>();
  const std::array positive_thresholds{result.thresholds.maximum_exact_normalized_nash_conv,
                                       result.thresholds.maximum_bucket_normalized_nash_conv,
                                       result.thresholds.maximum_normalized_nash_conv_delta,
                                       result.thresholds.maximum_normalized_profile_value_delta,
                                       result.thresholds.maximum_normalized_best_response_delta,
                                       result.thresholds.minimum_operational_speedup,
                                       result.thresholds.minimum_node_reduction_ratio};
  if (result.limits.maximum_physical_deals == 0U || result.limits.maximum_bucket_pairs == 0U ||
      result.limits.maximum_materialized_validation_nodes == 0U ||
      result.thresholds.maximum_native_byte_model == 0U ||
      !std::ranges::all_of(positive_thresholds, [](const double value) {
        return std::isfinite(value) && value > 0.0;
      })) {
    fail("invalid qualification limits or thresholds");
  }
  return result;
}

void append_fixtures(const Json &fixtures, const std::string_view suite,
                     std::vector<Fixture> &result) {
  if (!fixtures.is_array() || fixtures.size() < 5U) {
    fail("qualification corpus requires at least five fixtures");
  }
  for (const auto &source : fixtures) {
    Fixture fixture;
    fixture.id = source.at("id").get<std::string>();
    fixture.stratum = source.at("stratum").get<std::string>();
    fixture.suite = suite;
    if (fixture.id.empty() || fixture.stratum.empty() ||
        std::ranges::any_of(result, [&](const Fixture &existing) {
          return existing.id == fixture.id || existing.stratum == fixture.stratum;
        })) {
      fail("duplicate or empty fixture identity");
    }
    fixture.config = make_config(source);
    fixture.ranges = make_ranges(source, fixture.config);
    result.push_back(std::move(fixture));
  }
}

std::vector<Fixture> parse_fixtures(const Json &manifest,
                                    const std::filesystem::path &manifest_path) {
  std::vector<Fixture> result;
  if (manifest.value("schema", std::string{}) == corpus_schema) {
    append_fixtures(manifest.at("fixtures"), "v1_corpus", result);
    return result;
  }
  const auto &regression_source = manifest.at("regression_source");
  const auto regression_path =
      manifest_path.parent_path() / regression_source.at("path").get<std::string>();
  const auto regression_text = read_text(regression_path);
  if (fingerprint(regression_text) != regression_source.at("fingerprint").get<std::string>()) {
    fail("frozen v1 regression manifest fingerprint mismatch");
  }
  const auto regression = Json::parse(regression_text);
  if (regression.value("schema", std::string{}) != corpus_schema) {
    fail("invalid frozen v1 regression manifest schema");
  }
  append_fixtures(regression.at("fixtures"), "frozen_v1_regression", result);
  const auto suite = manifest.value("schema", std::string{}) == exact_blocker_corpus_schema
                         ? "independent_v2_holdout"
                         : "independent_v3_holdout";
  append_fixtures(manifest.at("fixtures"), suite, result);
  return result;
}

gtosd::PostflopRiverBucketBuildOptions build_options(const CorpusContract &contract,
                                                     const bool materialize) {
  return {contract.limits.maximum_physical_deals,
          contract.limits.maximum_bucket_pairs,
          contract.limits.maximum_materialized_validation_nodes,
          materialize,
          contract.abstraction,
          contract.distribution_quantization_basis_points};
}

gtosd::SolverConfig solver_config(const CorpusContract &contract) {
  gtosd::SolverConfig config;
  config.algorithm = gtosd::SolverAlgorithm::ProductionDcfr;
  config.iterations = contract.iterations;
  config.seed = contract.solver_seed;
  config.thread_count = 1U;
  config.averaging_delay = 0U;
  config.dcfr = {1.5, 0.0, 3.0};
  return config;
}

double median(std::vector<double> values) {
  if (values.empty()) {
    fail("cannot calculate an empty median");
  }
  std::ranges::sort(values);
  return values[values.size() / 2U];
}

Json preflight_fixture(const Fixture &fixture, const CorpusContract &contract) {
  const auto bucket = gtosd::build_fixed_river_bucket_game(fixture.config, fixture.ranges,
                                                           build_options(contract, false));
  if (!bucket) {
    fail("bucket preflight failed for " + fixture.id);
  }
  const auto physical_nodes = bucket.value().work_model.physical_node_instances_per_player_pass;
  if (physical_nodes == std::numeric_limits<std::uint64_t>::max() ||
      physical_nodes + 1U > contract.limits.maximum_materialized_validation_nodes) {
    fail("physical oracle exceeds node cap for " + fixture.id);
  }
  std::cout << "RIVER_BUCKET_PREFLIGHT fixture=" << fixture.id
            << " deals=" << bucket.value().work_model.physical_deals_preprocessed
            << " bucket_pairs=" << bucket.value().work_model.bucket_pairs
            << " physical_nodes=" << physical_nodes
            << " bucket_nodes=" << bucket.value().work_model.bucket_node_instances_per_player_pass
            << '\n';
  return {
      {"id", fixture.id},
      {"stratum", fixture.stratum},
      {"suite", fixture.suite},
      {"physical_deals", bucket.value().work_model.physical_deals_preprocessed},
      {"bucket_pairs", bucket.value().work_model.bucket_pairs},
      {"physical_nodes_per_pass", physical_nodes},
      {"bucket_nodes_per_pass", bucket.value().work_model.bucket_node_instances_per_player_pass},
      {"native_byte_model", bucket.value().byte_model.total_bytes}};
}

Json equitable_feasibility_fixture(const Fixture &fixture, const CorpusContract &contract) {
  const auto analysis = gtosd::analyze_fixed_river_equitable_partition(
      fixture.config, fixture.ranges, contract.limits.maximum_physical_deals);
  if (!analysis || !analysis.value().partition_is_equitable) {
    fail("equitable River analysis failed for " + fixture.id);
  }
  const bool passes_structural_gate =
      analysis.value().deal_pair_reduction >= contract.thresholds.minimum_node_reduction_ratio;
  std::cout << "RIVER_EQUITABLE_PREFLIGHT fixture=" << fixture.id
            << " active0=" << analysis.value().active_combos[0]
            << " active1=" << analysis.value().active_combos[1]
            << " initial0=" << analysis.value().initial_classes[0]
            << " initial1=" << analysis.value().initial_classes[1]
            << " stable0=" << analysis.value().stable_classes[0]
            << " stable1=" << analysis.value().stable_classes[1]
            << " class_pairs=" << analysis.value().compatible_class_pairs
            << " deals=" << analysis.value().physical_deals
            << " row_reduction=" << analysis.value().strategic_row_reduction
            << " pair_reduction=" << analysis.value().deal_pair_reduction
            << " gate=" << (passes_structural_gate ? "PASS" : "FAIL") << '\n';
  return {{"id", fixture.id},
          {"stratum", fixture.stratum},
          {"suite", fixture.suite},
          {"active_combos", analysis.value().active_combos},
          {"initial_classes", analysis.value().initial_classes},
          {"stable_classes", analysis.value().stable_classes},
          {"maximum_stable_class_size", analysis.value().maximum_stable_class_size},
          {"physical_deals", analysis.value().physical_deals},
          {"compatible_class_pairs", analysis.value().compatible_class_pairs},
          {"refinement_rounds", analysis.value().refinement_rounds},
          {"strategic_row_reduction", analysis.value().strategic_row_reduction},
          {"deal_pair_reduction", analysis.value().deal_pair_reduction},
          {"partition_is_equitable", analysis.value().partition_is_equitable},
          {"minimum_deal_pair_reduction", contract.thresholds.minimum_node_reduction_ratio},
          {"structural_gate", passes_structural_gate}};
}

Json qualify_fixture(const Fixture &fixture, const CorpusContract &contract) {
  gtosd::PostflopProductionSolveRequest request;
  request.iterations = contract.iterations;
  const auto exact_options = gtosd::resolve_postflop_production_options(request);
  if (!exact_options) {
    fail("cannot resolve exact ProductionDcfr options");
  }
  const auto native_config = solver_config(contract);
  std::vector<double> exact_samples;
  std::vector<double> native_samples;
  std::vector<PhaseMemorySample> exact_memory_samples;
  std::vector<PhaseMemorySample> native_memory_samples;
  std::optional<gtosd::PostflopSolveResult> exact_reference;
  std::optional<gtosd::PostflopRiverBucketGame> bucket_reference;
  std::optional<gtosd::SolveResult> native_reference;

  const auto run_exact = [&](const std::uint64_t repetition) {
    PhaseRssSampler memory_sampler;
    const auto started = std::chrono::steady_clock::now();
    auto solved =
        gtosd::solve_postflop_exact(fixture.config, fixture.ranges, exact_options.value());
    exact_samples.push_back(
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
    exact_memory_samples.push_back(memory_sampler.finish());
    if (!solved || solved.value().convergence.empty()) {
      fail("exact solve failed for " + fixture.id);
    }
    if (repetition + 1U == contract.repetitions) {
      exact_reference.emplace(std::move(solved.value()));
    }
  };
  const auto run_native = [&](const std::uint64_t repetition) {
    PhaseRssSampler memory_sampler;
    const auto started = std::chrono::steady_clock::now();
    auto bucket = gtosd::build_fixed_river_bucket_game(fixture.config, fixture.ranges,
                                                       build_options(contract, false));
    if (!bucket) {
      fail("bucket build failed for " + fixture.id);
    }
    auto solved = gtosd::solve_fixed_river_bucket_game(bucket.value(), native_config);
    native_samples.push_back(
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
    native_memory_samples.push_back(memory_sampler.finish());
    if (!solved) {
      fail("bucket solve failed for " + fixture.id);
    }
    if (repetition + 1U == contract.repetitions) {
      bucket_reference.emplace(std::move(bucket.value()));
      native_reference.emplace(std::move(solved.value()));
    }
  };

  for (std::uint64_t repetition = 0U; repetition < contract.repetitions; ++repetition) {
    if (repetition % 2U == 0U) {
      run_exact(repetition);
      run_native(repetition);
    } else {
      run_native(repetition);
      run_exact(repetition);
    }
  }
  if (!exact_reference || !bucket_reference || !native_reference) {
    fail("missing qualification reference result");
  }

  PhaseRssSampler validation_memory_sampler;
  const auto projection = gtosd::project_fixed_river_postflop_game(
      fixture.config, fixture.ranges, exact_reference->checkpoint,
      {contract.limits.maximum_physical_deals,
       contract.limits.maximum_materialized_validation_nodes});
  if (!projection) {
    fail("physical projection failed for " + fixture.id);
  }
  const auto lifted = gtosd::lift_fixed_river_bucket_strategy(projection.value(), *bucket_reference,
                                                              native_reference->average_strategy);
  const auto bucket_certification =
      lifted ? gtosd::calculate_nash_conv(projection.value().game, lifted.value())
             : gtosd::Result<gtosd::NashConvResult, gtosd::SolverError>::failure(
                   gtosd::SolverError::InvalidStrategy);
  if (!bucket_certification) {
    fail("original-game bucket certification failed for " + fixture.id);
  }

  const auto &exact_certification = exact_reference->convergence.back();
  const auto &bucket_cert = bucket_certification.value();
  const double pot_antes = static_cast<double>(fixture.config.initial_pot.units()) /
                           static_cast<double>(gtosd::Money::units_per_ante);
  if (!(pot_antes > 0.0)) {
    fail("invalid normalization pot");
  }
  const auto root_diagnostic = compare_root_information_sets(
      projection.value(), projection.value().blueprint, lifted.value(), pot_antes);
  const auto validation_memory = validation_memory_sampler.finish();
  double maximum_profile_delta = 0.0;
  double maximum_best_response_delta = 0.0;
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    maximum_profile_delta =
        std::max(maximum_profile_delta, std::abs(bucket_cert.profile_value[player] -
                                                 exact_certification.profile_value_antes[player]) /
                                            pot_antes);
    maximum_best_response_delta =
        std::max(maximum_best_response_delta,
                 std::abs(bucket_cert.best_response_value[player] -
                          exact_certification.best_response_value_antes[player]) /
                     pot_antes);
  }
  const double nash_conv_delta =
      std::abs(bucket_cert.normalized_nash_conv - exact_certification.normalized_nash_conv);
  const double exact_median = median(exact_samples);
  const double native_median = median(native_samples);
  const double speedup = exact_median / native_median;
  const double node_reduction =
      static_cast<double>(bucket_reference->work_model.physical_node_instances_per_player_pass) /
      static_cast<double>(bucket_reference->work_model.bucket_node_instances_per_player_pass);

  const auto &thresholds = contract.thresholds;
  Json gates{{"exact_nash_conv", exact_certification.normalized_nash_conv <=
                                     thresholds.maximum_exact_normalized_nash_conv},
             {"bucket_nash_conv",
              bucket_cert.normalized_nash_conv <= thresholds.maximum_bucket_normalized_nash_conv},
             {"nash_conv_delta", nash_conv_delta <= thresholds.maximum_normalized_nash_conv_delta},
             {"profile_value_delta",
              maximum_profile_delta <= thresholds.maximum_normalized_profile_value_delta},
             {"best_response_delta",
              maximum_best_response_delta <= thresholds.maximum_normalized_best_response_delta},
             {"operational_speedup", speedup >= thresholds.minimum_operational_speedup},
             {"node_reduction", node_reduction >= thresholds.minimum_node_reduction_ratio},
             {"native_byte_model",
              bucket_reference->byte_model.total_bytes <= thresholds.maximum_native_byte_model}};
  bool passed = true;
  for (const auto &[name, value] : gates.items()) {
    static_cast<void>(name);
    passed = passed && value.get<bool>();
  }

  std::cout << "RIVER_BUCKET_QUALIFICATION fixture=" << fixture.id
            << " status=" << (passed ? "PASS" : "FAIL") << " exact_median_seconds=" << exact_median
            << " native_median_seconds=" << native_median << " speedup=" << speedup
            << " exact_nash_conv=" << exact_certification.normalized_nash_conv
            << " bucket_nash_conv=" << bucket_cert.normalized_nash_conv << '\n';

  return {
      {"id", fixture.id},
      {"stratum", fixture.stratum},
      {"suite", fixture.suite},
      {"status", passed ? "PASS" : "FAIL"},
      {"source_game_fingerprint", bucket_reference->source_game_fingerprint},
      {"abstraction_fingerprint", bucket_reference->abstraction_fingerprint},
      {"bucket_game_fingerprint", bucket_reference->game_fingerprint},
      {"physical_deals", bucket_reference->work_model.physical_deals_preprocessed},
      {"bucket_pairs", bucket_reference->work_model.bucket_pairs},
      {"player0_buckets", bucket_reference->player_buckets[0].size()},
      {"player1_buckets", bucket_reference->player_buckets[1].size()},
      {"public_nodes", bucket_reference->work_model.public_nodes_per_pair},
      {"physical_nodes_per_pass",
       bucket_reference->work_model.physical_node_instances_per_player_pass},
      {"bucket_nodes_per_pass", bucket_reference->work_model.bucket_node_instances_per_player_pass},
      {"node_reduction_ratio", node_reduction},
      {"physical_projection_bytes", projection.value().byte_model.total_bytes},
      {"native_byte_model", bucket_reference->byte_model.total_bytes},
      {"memory_scope",
       {{"metric", "process_current_rss_sampled_every_1ms"},
        {"phase_delta_is_not_solver_owned_memory", true},
        {"allocator_retention_may_affect_later_phase_baselines", true}}},
      {"exact_phase_memory_samples", memory_samples_json(exact_memory_samples)},
      {"bucket_phase_memory_samples", memory_samples_json(native_memory_samples)},
      {"validation_phase_memory", memory_samples_json({validation_memory})[0]},
      {"process_peak_rss_bytes_at_fixture_end", gtosd::process_peak_rss_bytes()},
      {"exact_operational_seconds_samples", exact_samples},
      {"native_operational_seconds_samples", native_samples},
      {"exact_operational_seconds_median", exact_median},
      {"native_operational_seconds_median", native_median},
      {"operational_speedup", speedup},
      {"exact_normalized_nash_conv", exact_certification.normalized_nash_conv},
      {"bucket_original_normalized_nash_conv", bucket_cert.normalized_nash_conv},
      {"normalized_nash_conv_delta", nash_conv_delta},
      {"maximum_normalized_profile_value_delta", maximum_profile_delta},
      {"maximum_normalized_best_response_delta", maximum_best_response_delta},
      {"exact_profile_value_antes", exact_certification.profile_value_antes},
      {"bucket_profile_value_antes", bucket_cert.profile_value},
      {"exact_best_response_value_antes", exact_certification.best_response_value_antes},
      {"bucket_best_response_value_antes", bucket_cert.best_response_value},
      {"root_information_set_diagnostic", root_diagnostic},
      {"gates", std::move(gates)}};
}

void write_report(const std::filesystem::path &destination, const Json &report) {
  if (destination.empty()) {
    fail("empty report path");
  }
  if (!destination.parent_path().empty()) {
    std::error_code directory_error;
    std::filesystem::create_directories(destination.parent_path(), directory_error);
    if (directory_error) {
      fail("cannot create report directory");
    }
  }
  auto temporary = destination;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << std::setw(2) << report << '\n';
    output.flush();
    if (!output) {
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      fail("cannot write qualification report");
    }
  }
  bool replaced = false;
#ifdef _WIN32
  replaced = ::MoveFileExW(temporary.c_str(), destination.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  std::error_code rename_error;
  std::filesystem::rename(temporary, destination, rename_error);
  replaced = !rename_error;
#endif
  if (!replaced) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    fail("cannot atomically replace qualification report");
  }
}

int run(const int argc, char **argv) {
  if (argc != 4 && argc != 5) {
    std::cerr << "usage: gtosd_river_bucket_qualification --manifest FILE "
                 "--preflight-only\n"
                 "   or: gtosd_river_bucket_qualification --manifest FILE "
                 "--diagnostic-smoke-only\n"
                 "   or: gtosd_river_bucket_qualification --manifest FILE --output FILE\n"
                 "   or: gtosd_river_bucket_qualification --manifest FILE "
                 "--equitable-preflight-only\n"
                 "   or: gtosd_river_bucket_qualification --manifest FILE "
                 "--equitable-output FILE\n";
    return 3;
  }
  if (std::string_view(argv[1]) != "--manifest") {
    std::cerr << "missing --manifest\n";
    return 3;
  }
  const std::filesystem::path manifest_path(argv[2]);
  const bool preflight_only = argc == 4 && std::string_view(argv[3]) == "--preflight-only";
  const bool diagnostic_smoke_only =
      argc == 4 && std::string_view(argv[3]) == "--diagnostic-smoke-only";
  const bool qualification = argc == 5 && std::string_view(argv[3]) == "--output";
  const bool equitable_preflight_only =
      argc == 4 && std::string_view(argv[3]) == "--equitable-preflight-only";
  const bool equitable_report = argc == 5 && std::string_view(argv[3]) == "--equitable-output";
  if (!preflight_only && !diagnostic_smoke_only && !qualification && !equitable_preflight_only &&
      !equitable_report) {
    std::cerr << "invalid arguments\n";
    return 3;
  }

  const auto manifest_text = read_text(manifest_path);
  const auto manifest = Json::parse(manifest_text);
  const auto contract = parse_contract(manifest);
  const auto fixtures = parse_fixtures(manifest, manifest_path);
  if (equitable_preflight_only || equitable_report) {
    Json results = Json::array();
    bool all_structural_gates_pass = true;
    for (const auto &fixture : fixtures) {
      auto result = equitable_feasibility_fixture(fixture, contract);
      all_structural_gates_pass =
          all_structural_gates_pass && result.at("structural_gate").get<bool>();
      results.push_back(std::move(result));
    }
    if (equitable_preflight_only) {
      std::cout << "RIVER_EQUITABLE_FEASIBILITY_PREFLIGHT=PASS fixtures=" << fixtures.size()
                << " all_structural_gates=" << (all_structural_gates_pass ? "PASS" : "FAIL")
                << " manifest_fingerprint=" << fingerprint(manifest_text) << '\n';
      return 0;
    }
    Json report{{"schema", std::string(equitable_feasibility_report_schema)},
                {"manifest", manifest_path.generic_string()},
                {"manifest_fingerprint", fingerprint(manifest_text)},
                {"method", "coarsest_equitable_refinement_v1"},
                {"status", all_structural_gates_pass ? "PROMISING" : "BLOCKED"},
                {"decision_scope", "minimum deal-pair reduction on every fixture"},
                {"minimum_deal_pair_reduction", contract.thresholds.minimum_node_reduction_ratio},
                {"fixtures", std::move(results)}};
    write_report(argv[4], report);
    std::cout << "RIVER_EQUITABLE_FEASIBILITY="
              << (all_structural_gates_pass ? "PROMISING" : "BLOCKED") << " report=" << argv[4]
              << '\n';
    return all_structural_gates_pass ? 0 : 2;
  }
  Json preflight = Json::array();
  for (const auto &fixture : fixtures) {
    preflight.push_back(preflight_fixture(fixture, contract));
  }
  if (preflight_only) {
    std::cout << "RIVER_BUCKET_QUALIFICATION_PREFLIGHT=PASS fixtures=" << fixtures.size()
              << " manifest_fingerprint=" << fingerprint(manifest_text) << '\n';
    return 0;
  }
  if (diagnostic_smoke_only) {
    const auto result = qualify_fixture(fixtures.front(), contract);
    const auto &diagnostic = result.at("root_information_set_diagnostic");
    const auto root_information_sets = diagnostic.at("root_information_sets").get<std::size_t>();
    const auto cfv_error = diagnostic.at("weighted_mean_normalized_cfv_error").get<double>();
    const auto strategy_error =
        diagnostic.at("weighted_mean_strategy_total_variation").get<double>();
    if (root_information_sets == 0U || !std::isfinite(cfv_error) || cfv_error < 0.0 ||
        !std::isfinite(strategy_error) || strategy_error < 0.0 ||
        result.at("exact_phase_memory_samples").empty() ||
        result.at("bucket_phase_memory_samples").empty()) {
      fail("invalid root CFV, strategy, or RSS diagnostic");
    }
    std::cout << "RIVER_BUCKET_DIAGNOSTIC_SMOKE=PASS fixture=" << result.at("id").get<std::string>()
              << " root_infosets=" << root_information_sets << " weighted_cfv_error=" << cfv_error
              << " weighted_strategy_tv=" << strategy_error
              << " peak_rss_bytes=" << gtosd::process_peak_rss_bytes() << '\n';
    return 0;
  }

  Json results = Json::array();
  bool overall_pass = true;
  for (const auto &fixture : fixtures) {
    auto result = qualify_fixture(fixture, contract);
    overall_pass = overall_pass && result.at("status") == "PASS";
    results.push_back(std::move(result));
  }
  const auto selected_report_schema =
      contract.abstraction == gtosd::PostflopRiverBucketAbstraction::MadeHandValueV1 ? report_schema
      : contract.abstraction == gtosd::PostflopRiverBucketAbstraction::ExactBlockerSignatureV2
          ? exact_blocker_report_schema
          : showdown_distribution_report_schema;
  Json report{{"schema", std::string(selected_report_schema)},
              {"manifest", manifest_path.generic_string()},
              {"manifest_fingerprint", fingerprint(manifest_text)},
              {"abstraction", gtosd::postflop_river_bucket_abstraction_name(contract.abstraction)},
              {"status", overall_pass ? "QUALIFIED" : "REJECTED"},
              {"decision_scope", "all thresholds on every fixture"},
              {"solver", manifest.at("solver")},
              {"limits", manifest.at("limits")},
              {"thresholds", manifest.at("thresholds")},
              {"process_peak_rss_bytes", gtosd::process_peak_rss_bytes()},
              {"preflight", std::move(preflight)},
              {"fixtures", std::move(results)}};
  write_report(argv[4], report);
  std::cout << "RIVER_BUCKET_QUALIFICATION=" << (overall_pass ? "QUALIFIED" : "REJECTED")
            << " report=" << argv[4] << '\n';
  return overall_pass ? 0 : 2;
}

} // namespace

int main(const int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "RIVER_BUCKET_QUALIFICATION_ERROR=" << error.what() << '\n';
    return 3;
  }
}
