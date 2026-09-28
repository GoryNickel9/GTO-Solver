// MonkerSolver's preflop charts played inside our game: the EV a player loses
// when its preflop decisions follow a chart set (MonkerSolver text format, one
// row per hand class) while everything else stays our average strategy, in
// the physical game with the lifted policy (--board-class-rows for a step-2
// policy trained with them). One stage-one pass of the physical best response
// gives the action values of both players at their preflop nodes
// (BestResponseEvaluator::preflop_action_values); every chart set is then
// valued exactly on them (monker_chart_values.hpp), with the loss split by
// node and hand class. The best preflop response to the same values is
// reported as the scale of the losses: our own preflop should be close to it.
//
// Flops: --flops N samples N physical flops with all their runouts (standard
// errors over the flops); --all-flops enumerates the 573 canonical flops with
// their suit orbits, which is exact (--flop-limit K stops after K flops, a
// partial pass for smoke tests). Writes JSON with --out (schema
// gtosd.preflop_blueprint_monker_values.v1), read by
// tools/monker_compare/monker_in_our_game.py.
#include "monker_chart_values.hpp"

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/preflop_blueprint/action_labels.hpp"
#include "gtosd/preflop_blueprint/best_response.hpp"
#include "gtosd/preflop_blueprint/board_class_rows.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/game_model.hpp"
#include "gtosd/preflop_blueprint/policy_file.hpp"
#include "gtosd/preflop_blueprint/trainer.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

namespace ca = gtosd::card_abstraction;
namespace pb = gtosd::preflop_blueprint;
namespace mc = gtosd::monker_charts;
using Clock = std::chrono::steady_clock;
using Json = nlohmann::ordered_json;

constexpr double ante_scale = 1.0 / static_cast<double>(gtosd::Money::units_per_ante);
// Allowance of the self-checks between sums of the same values taken in
// another order.
constexpr double check_tolerance = 1e-9;

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::uint64_t parse_unsigned(const std::string_view text) {
  std::size_t consumed = 0U;
  const auto value = std::stoull(std::string(text), &consumed, 0);
  if (consumed != text.size()) {
    throw std::runtime_error("invalid number: " + std::string(text));
  }
  return value;
}

double parse_decimal(const std::string_view text) {
  std::size_t consumed = 0U;
  const auto value = std::stod(std::string(text), &consumed);
  if (consumed != text.size() || !std::isfinite(value)) {
    throw std::runtime_error("invalid decimal: " + std::string(text));
  }
  return value;
}

template <typename Function>
void run_parallel(const unsigned threads, const std::size_t count, Function &&function) {
  const auto workers = static_cast<std::size_t>(std::max(1U, threads));
  std::atomic<std::size_t> next{0U};
  const auto worker = [&] {
    for (std::size_t index = next.fetch_add(1U); index < count; index = next.fetch_add(1U)) {
      function(index);
    }
  };
  std::vector<std::thread> pool;
  const auto spawned = count == 0U ? 0U : std::min(workers, count) - 1U;
  for (std::size_t thread = 0; thread < spawned; ++thread) {
    pool.emplace_back(worker);
  }
  worker();
  for (auto &thread : pool) {
    thread.join();
  }
}

Json peaks_json() {
  const auto peaks = pb::process_memory_peaks();
  return Json{{"working_set_bytes", peaks.working_set_bytes},
              {"peak_working_set_bytes", peaks.peak_working_set_bytes},
              {"private_commit_bytes", peaks.private_commit_bytes},
              {"peak_private_commit_bytes", peaks.peak_private_commit_bytes}};
}

struct ChartSet {
  std::string name;
  std::filesystem::path directory;
};

struct Options {
  std::filesystem::path config_path;
  std::filesystem::path resources_dir;
  std::filesystem::path buckets_dir;
  std::filesystem::path policy_path;
  std::filesystem::path output_path;
  bool board_class_rows{false};
  bool all_flops{false};
  std::uint32_t flops{64U};
  std::uint32_t flop_limit{0U};
  // Chart export default (chart_export.hpp).
  std::uint64_t seed{0x4348415254455850ULL};
  unsigned threads{1U};
  pb::RiverEngine river_engine{pb::RiverEngine::Joint};
  bool combo_values{true};
  double target_pot_percent{1.0};
  std::vector<ChartSet> charts;
};

Options parse_options(const int argc, char **argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view name(argv[index]);
    if (name == "--board-class-rows") {
      options.board_class_rows = true;
      continue;
    }
    if (name == "--all-flops") {
      options.all_flops = true;
      continue;
    }
    if (name == "--no-combo-values") {
      options.combo_values = false;
      continue;
    }
    if (index + 1 >= argc) {
      throw std::runtime_error("missing value for " + std::string(name));
    }
    const std::string_view value(argv[++index]);
    if (name == "--config") {
      options.config_path = value;
    } else if (name == "--resources-dir") {
      options.resources_dir = value;
    } else if (name == "--buckets-dir") {
      options.buckets_dir = value;
    } else if (name == "--policy") {
      options.policy_path = value;
    } else if (name == "--out") {
      options.output_path = value;
    } else if (name == "--flops") {
      options.flops = static_cast<std::uint32_t>(parse_unsigned(value));
    } else if (name == "--flop-limit") {
      options.flop_limit = static_cast<std::uint32_t>(parse_unsigned(value));
    } else if (name == "--seed") {
      options.seed = parse_unsigned(value);
    } else if (name == "--threads") {
      options.threads = static_cast<unsigned>(parse_unsigned(value));
    } else if (name == "--target-pot-percent") {
      options.target_pot_percent = parse_decimal(value);
    } else if (name == "--river-engine") {
      if (value == "joint") {
        options.river_engine = pb::RiverEngine::Joint;
      } else if (value == "reference") {
        options.river_engine = pb::RiverEngine::Reference;
      } else {
        throw std::runtime_error("--river-engine must be joint or reference");
      }
    } else if (name == "--charts") {
      const auto equals = value.find('=');
      if (equals == std::string_view::npos || equals == 0U || equals + 1U == value.size()) {
        throw std::runtime_error("--charts takes NAME=DIRECTORY");
      }
      options.charts.push_back(
          {std::string(value.substr(0, equals)), std::filesystem::path(value.substr(equals + 1U))});
    } else {
      throw std::runtime_error("unknown argument " + std::string(name));
    }
  }
  if (options.config_path.empty() || options.resources_dir.empty() ||
      options.buckets_dir.empty() || options.policy_path.empty()) {
    throw std::runtime_error("--config, --resources-dir, --buckets-dir and --policy are required");
  }
  if (options.charts.empty()) {
    throw std::runtime_error("at least one --charts NAME=DIRECTORY is required");
  }
  if (!options.all_flops && options.flops == 0U) {
    throw std::runtime_error("--flops must be positive");
  }
  if (!options.all_flops && options.flop_limit > 0U) {
    throw std::runtime_error("--flop-limit goes with --all-flops");
  }
  if (!(options.target_pot_percent > 0.0 && options.target_pot_percent <= 100.0)) {
    throw std::runtime_error("--target-pot-percent must be in (0, 100]");
  }
  return options;
}

Json vector_json(const std::vector<double> &values) {
  Json array = Json::array();
  for (const auto value : values) {
    array.push_back(value);
  }
  return array;
}

int run(const int argc, char **argv) {
  const auto options = parse_options(argc, argv);
  const auto started = Clock::now();

  const auto config = pb::parse_game_config_json(read_file(options.config_path));
  if (!config) {
    throw std::runtime_error(std::string("configuration rejected: ") +
                             pb::config_error_name(config.error()));
  }
  const auto compiled = pb::CompiledGame::compile(config.value());
  if (!compiled) {
    throw std::runtime_error(std::string("compile failed: ") +
                             pb::game_model_error_name(compiled.error()));
  }
  const auto &game = compiled.value();
  if (game.config().player_count != 2U) {
    throw std::runtime_error("the chart values are heads-up only");
  }
  auto ranks = ca::RankTable::load(options.resources_dir / "rank_table_v1.bin");
  auto all_in = ca::AllInTable::load(options.resources_dir / "preflop_all_in_v1.bin");
  auto flop = ca::BucketTable::load(options.buckets_dir / "flop_buckets_v1.bin");
  auto turn = ca::BucketTable::load(options.buckets_dir / "turn_buckets_v1.bin");
  auto river = ca::BucketTable::load(options.buckets_dir / "river_buckets_v1.bin");
  if (!ranks || !all_in || !flop || !turn || !river) {
    throw std::runtime_error("resources or bucket tables missing");
  }
  const auto catalog = ca::BoardCatalog::build();
  pb::BestResponseResources resources;
  resources.ranks = &ranks.value();
  resources.all_in = &all_in.value();
  resources.catalog = &catalog;
  resources.flop = &flop.value();
  resources.turn = &turn.value();
  resources.river = &river.value();
  std::optional<pb::BoardClassRows> board_class_rows;
  std::string abstraction = "buckets";
  if (options.board_class_rows) {
    board_class_rows.emplace(flop.value().capacity(), turn.value().capacity(),
                             river.value().capacity());
    resources.board_class_rows = &*board_class_rows;
    abstraction = board_class_rows->fingerprint();
  }

  pb::PolicyFileInfo info;
  auto loaded = pb::load_policy(options.policy_path, game, &info);
  if (!loaded) {
    throw std::runtime_error(std::string("policy rejected: ") +
                             pb::policy_file_error_name(loaded.error()));
  }
  const std::unique_ptr<pb::BucketPolicy> policy_holder = std::move(loaded.value());
  const pb::BucketPolicy &policy = *policy_holder;
  // The policy must have been trained with these rows and tables (the
  // source text the trainer CLI writes).
  {
    const auto &layout = policy.layout();
    if (board_class_rows) {
      const auto suffix = "|abstraction=" + board_class_rows->fingerprint() +
                          "|flop=" + flop.value().fingerprint() + "|turn=" +
                          turn.value().fingerprint() + "|river=" + river.value().fingerprint();
      if (!info.source.ends_with(suffix) ||
          layout.flop_capacity != board_class_rows->count(ca::BucketStreet::Flop) ||
          layout.turn_capacity != board_class_rows->count(ca::BucketStreet::Turn) ||
          layout.river_capacity != board_class_rows->count(ca::BucketStreet::River)) {
        throw std::runtime_error("the policy was not trained with these board class rows and "
                                 "bucket tables (source " +
                                 info.source + ")");
      }
    } else if (info.source.find("|abstraction=") != std::string::npos) {
      throw std::runtime_error("the policy was trained with another abstraction (source " +
                               info.source + "); pass --board-class-rows for a step-2 policy");
    }
  }
  auto created = pb::BestResponseEvaluator::create(game, policy, resources);
  if (!created) {
    throw std::runtime_error(std::string("evaluator rejected the policy: ") +
                             pb::kernel_error_name(created.error()));
  }
  auto evaluator = created.value();
  evaluator.set_river_engine(options.river_engine);
  const double load_seconds = std::chrono::duration<double>(Clock::now() - started).count();
  const auto load_peaks = peaks_json();

  // Flop groups: sampled physical flops, or the canonical flops with their
  // suit orbits.
  const auto &canonical = catalog.flops();
  std::vector<pb::FlopGroup> groups;
  std::vector<std::uint32_t> multiplicity;
  std::string mode = "sampled";
  if (options.all_flops) {
    const auto total = options.flop_limit > 0U
                           ? std::min<std::size_t>(options.flop_limit, canonical.size())
                           : canonical.size();
    for (std::size_t index = 0; index < total; ++index) {
      auto cards = canonical[index].cards;
      std::sort(cards.begin(), cards.end());
      groups.push_back(pb::full_runouts(cards));
      multiplicity.push_back(canonical[index].multiplicity);
    }
    mode = total == canonical.size() ? "exact" : "partial";
  } else {
    ca::DeterministicRandom random(options.seed);
    for (std::uint32_t draw = 0; draw < options.flops; ++draw) {
      groups.push_back(pb::full_runouts(catalog.sample_physical_history(random).flop));
    }
  }
  std::cerr << "{\"event\": \"start\", \"mode\": \"" << mode << "\", \"flops\": " << groups.size()
            << ", \"threads\": " << options.threads << ", \"policy_entries\": "
            << policy.table().size() << ", \"load_seconds\": " << load_seconds << "}\n"
            << std::flush;

  // Stage one.
  const auto stage_started = Clock::now();
  std::vector<pb::FlopValues> values(groups.size());
  std::atomic<bool> failed{false};
  std::atomic<std::size_t> done{0U};
  std::mutex progress_mutex;
  const auto progress_every = std::max<std::size_t>(1U, groups.size() / 20U);
  run_parallel(options.threads, groups.size(), [&](const std::size_t index) {
    if (failed.load()) {
      return;
    }
    auto result = evaluator.evaluate_flop(groups[index]);
    if (!result) {
      failed.store(true);
      return;
    }
    if (options.all_flops) {
      result.value().images = pb::flop_images(groups[index].flop);
      if (result.value().images.size() != multiplicity[index]) {
        failed.store(true);
        return;
      }
    }
    values[index] = std::move(result.value());
    const auto finished = done.fetch_add(1U) + 1U;
    if (finished % progress_every == 0U || finished == groups.size()) {
      const std::lock_guard<std::mutex> lock(progress_mutex);
      const double elapsed = std::chrono::duration<double>(Clock::now() - stage_started).count();
      const double rate = elapsed / static_cast<double>(finished);
      std::cerr << "{\"event\": \"progress\", \"flops_done\": " << finished
                << ", \"flops_total\": " << groups.size() << ", \"elapsed_seconds\": " << elapsed
                << ", \"seconds_per_flop\": " << rate << ", \"eta_seconds\": "
                << rate * static_cast<double>(groups.size() - finished) << "}\n"
                << std::flush;
    }
  });
  if (failed.load()) {
    throw std::runtime_error("flop evaluation failed");
  }
  const double stage_seconds = std::chrono::duration<double>(Clock::now() - stage_started).count();
  std::vector<const pb::FlopValues *> pointers;
  std::uint64_t boards = 0U;
  std::uint64_t physical_flops = 0U;
  for (const auto &entry : values) {
    pointers.push_back(&entry);
    boards += entry.boards;
    physical_flops += entry.images.size();
  }

  const auto aggregate_started = Clock::now();
  const auto aggregated = evaluator.aggregate(pointers, mode == "exact");
  if (!aggregated) {
    throw std::runtime_error("aggregation failed");
  }
  const auto &report = aggregated.value();
  std::array<pb::PreflopActionValues, 2> action_values{};
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    auto computed = evaluator.preflop_action_values(pointers, hero);
    if (!computed) {
      throw std::runtime_error("preflop action values failed");
    }
    action_values[hero] = std::move(computed.value());
  }
  const double aggregate_seconds =
      std::chrono::duration<double>(Clock::now() - aggregate_started).count();

  const auto values_started = Clock::now();
  const auto &game_config = game.config();
  const double initial_pot =
      static_cast<double>(game_config.ante.units() * game_config.player_count +
                          game_config.button_blind.units()) *
      ante_scale;
  const double target_antes = 0.01 * options.target_pot_percent * initial_pot;
  const auto percent = [&](const double antes) {
    return initial_pot > 0.0 ? 100.0 * antes / initial_pot : 0.0;
  };
  const auto classes = mc::hand_classes();
  std::vector<std::string> class_labels(mc::class_count);
  for (const auto &[hand_class, label] : classes.label_by_class) {
    class_labels.at(hand_class) = label;
  }
  const auto class_combos = mc::class_combos();
  const auto &combo_table = ca::combo_table();
  std::map<std::uint32_t, mc::ChartNode> chart_of;
  for (auto &chart : mc::chart_nodes(game)) {
    chart_of.emplace(chart.node, std::move(chart));
  }

  std::vector<std::string> failures;
  const auto check = [&](const bool condition, const std::string &what) {
    if (!condition) {
      failures.push_back(what);
    }
  };

  Json json;
  json["schema"] = "gtosd.preflop_blueprint_monker_values.v1";
  json["config_id"] = game_config.id;
  json["tree_fingerprint"] = game.fingerprint();
  json["policy_fingerprint"] = info.policy_fingerprint;
  json["policy_source"] = info.source;
  json["abstraction"] = abstraction;
  json["fingerprints"] = {{"catalog", catalog.fingerprint()},
                          {"flop_table", flop.value().fingerprint()},
                          {"turn_table", turn.value().fingerprint()},
                          {"river_table", river.value().fingerprint()}};
  json["capacities"] = {policy.layout().flop_capacity, policy.layout().turn_capacity,
                        policy.layout().river_capacity};
  json["initial_pot_antes"] = initial_pot;
  json["target_pot_percent"] = options.target_pot_percent;
  json["target_antes"] = target_antes;
  json["value_scope"] =
      "antes per hand of the hero (net result of the hand, posted antes included), full "
      "uniform hand ranges; entry values averaged over the evaluated flops with all their "
      "runouts; counterfactual values carry the opponent reach and chance";
  json["evaluation"] = {{"mode", mode},
                        {"flops", groups.size()},
                        {"physical_flops", physical_flops},
                        {"boards", boards},
                        {"seed", options.all_flops ? Json(nullptr) : Json(options.seed)},
                        {"threads", options.threads},
                        {"river_engine", pb::river_engine_name(options.river_engine)}};
  json["estimate"] = {
      {"ev_antes", {report.ev[0], report.ev[1]}},
      {"ev_standard_error_antes", {report.ev_standard_error[0], report.ev_standard_error[1]}},
      {"best_response_antes", {report.best_response[0], report.best_response[1]}},
      {"gain_antes", {report.gain[0], report.gain[1]}},
      {"gain_lower_antes", {report.gain_lower[0], report.gain_lower[1]}},
      {"best_response_preflop_antes",
       {report.best_response_preflop[0], report.best_response_preflop[1]}},
      {"gain_preflop_antes", {report.gain_preflop[0], report.gain_preflop[1]}},
      {"max_gain_antes", report.max_gain},
      {"nashconv_antes", report.nashconv}};
  {
    Json labels = Json::array();
    Json class_ids = Json::array();
    for (std::size_t combo = 0; combo < mc::hero_combos; ++combo) {
      labels.push_back(class_labels[combo_table.hand_class[combo]]);
      class_ids.push_back(combo_table.hand_class[combo]);
    }
    json["combos"] = {{"labels", labels}, {"class", class_ids}};
    json["classes"] = class_labels;
  }

  // Our preflop rows per hero, exactly what the evaluator multiplies.
  std::array<mc::HeroTree, 2> trees;
  std::array<mc::PreflopStrategy, 2> ours;
  Json heroes = Json::array();
  Json responses = Json::array();
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    const auto &pav = action_values[hero];
    trees[hero] = mc::hero_tree(game, pav.nodes, hero);
    const auto &tree = trees[hero];
    ours[hero].resize(pav.nodes.size());
    for (std::size_t slot = 0; slot < pav.nodes.size(); ++slot) {
      ours[hero][slot].resize(mc::class_count);
      for (std::size_t hand_class = 0; hand_class < mc::class_count; ++hand_class) {
        const auto row = policy.row(pav.nodes[slot], static_cast<std::uint32_t>(hand_class));
        ours[hero][slot][hand_class].assign(row.begin(), row.end());
      }
    }
    double root_mean = 0.0;
    for (const auto value : pav.root_values) {
      root_mean += value;
    }
    root_mean /= static_cast<double>(pav.root_values.size());
    check(std::abs(root_mean - report.ev[hero]) <= check_tolerance,
          "mean root value equals the aggregate EV of hero " + std::to_string(hero));

    const auto chart_name = [&](const std::size_t slot) {
      return chart_of.at(pav.nodes[slot]).relative;
    };
    Json hero_json;
    hero_json["hero"] = hero;
    hero_json["position"] = pb::position_name(game, hero);
    hero_json["ev_antes"] = report.ev[hero];
    hero_json["root_value_mean_antes"] = root_mean;
    Json top = Json::array();
    for (const auto slot : tree.top) {
      top.push_back(chart_name(slot));
    }
    hero_json["top"] = top;
    if (options.combo_values) {
      hero_json["root_values"] = vector_json(pav.root_values);
    }
    Json nodes = Json::array();
    for (std::size_t slot = 0; slot < pav.nodes.size(); ++slot) {
      const auto &chart = chart_of.at(pav.nodes[slot]);
      Json node;
      node["chart"] = chart.relative;
      node["node"] = pav.nodes[slot];
      node["path_id"] = pb::node_path_id(game, pav.nodes[slot]);
      node["tokens"] = chart.tokens;
      Json columns = Json::array();
      for (const auto index : chart.columns) {
        columns.push_back(chart.tokens[index]);
      }
      node["columns"] = columns;
      if (tree.parent[slot] == pav.nodes.size()) {
        node["parent"] = nullptr;
      } else {
        const auto &parent = chart_of.at(pav.nodes[tree.parent[slot]]);
        node["parent"] = {{"chart", parent.relative},
                          {"action", parent.tokens[tree.parent_action[slot]]}};
      }
      Json next = Json::object();
      for (std::size_t action = 0; action < chart.tokens.size(); ++action) {
        Json children = Json::array();
        for (const auto child : tree.next[slot][action]) {
          children.push_back(chart_name(child));
        }
        next[chart.tokens[action]] = children;
      }
      node["next"] = next;
      if (options.combo_values) {
        node["opponent_reach"] = vector_json(pav.opponent_reach[slot]);
        Json combo_values = Json::object();
        for (std::size_t action = 0; action < chart.tokens.size(); ++action) {
          combo_values[chart.tokens[action]] = vector_json(pav.combo_values[slot][action]);
        }
        node["combo_values"] = combo_values;
      }
      Json strategy = Json::object();
      Json class_ev = Json::object();
      Json class_weight = Json::object();
      for (std::size_t hand_class = 0; hand_class < mc::class_count; ++hand_class) {
        const auto &label = class_labels[hand_class];
        Json row = Json::object();
        Json ev = Json::object();
        for (std::size_t action = 0; action < chart.tokens.size(); ++action) {
          row[chart.tokens[action]] = ours[hero][slot][hand_class][action];
          // The spread over the groups is a standard error only for sampled
          // flops: an exact or partial pass has unequal orbit weights and no
          // sampling error.
          ev[chart.tokens[action]] = {
              {"ev", pav.class_ev[slot][action][hand_class]},
              {"se", mode == "sampled" ? Json(pav.class_se[slot][action][hand_class]) : Json()}};
        }
        strategy[label] = row;
        class_ev[label] = ev;
        class_weight[label] = pav.class_weight[slot][hand_class];
      }
      node["strategy"] = strategy;
      node["class_ev"] = class_ev;
      node["class_weight"] = class_weight;
      nodes.push_back(node);
    }
    hero_json["nodes"] = nodes;
    heroes.push_back(hero_json);

    // Best preflop response to the same values.
    const auto response = mc::preflop_response(tree, pav, ours[hero]);
    check(std::abs(response.gain_per_combo - report.gain_preflop[hero]) <= check_tolerance,
          "per-combo preflop response equals the aggregate's gain_preflop of hero " +
              std::to_string(hero));
    if (mode == "exact") {
      check(std::abs(response.gain_per_class - response.gain_per_combo) <= check_tolerance,
            "per-class and per-combo preflop responses agree on the exact pass for hero " +
                std::to_string(hero));
    }
    Json choices = Json::object();
    for (std::size_t slot = 0; slot < pav.nodes.size(); ++slot) {
      const auto &chart = chart_of.at(pav.nodes[slot]);
      Json per_class = Json::object();
      for (std::size_t hand_class = 0; hand_class < mc::class_count; ++hand_class) {
        per_class[class_labels[hand_class]] = chart.tokens[response.choice[slot][hand_class]];
      }
      choices[chart.relative] = per_class;
    }
    responses.push_back({{"hero", hero},
                         {"position", pb::position_name(game, hero)},
                         {"gain_per_class_antes", response.gain_per_class},
                         {"gain_per_combo_antes", response.gain_per_combo},
                         {"aggregate_gain_preflop_antes", report.gain_preflop[hero]},
                         {"pot_percent", percent(response.gain_per_class)},
                         {"choices", choices}});
    std::cout << "preflop response " << pb::position_name(game, hero) << ": gain per class "
              << response.gain_per_class << " antes (" << percent(response.gain_per_class)
              << " % of the pot), per combo " << response.gain_per_combo << " (aggregate "
              << report.gain_preflop[hero] << ")\n";

    // Standard error of our own EV from the entry series: the aggregate's.
    if (mode == "sampled" && groups.size() > 1U) {
      const auto series =
          mc::group_series(pointers, hero, mc::entry_reach(game, tree, hero, ours[hero]));
      const double se = mc::standard_error(series);
      check(std::abs(se - report.ev_standard_error[hero]) <=
                check_tolerance * std::max(1.0, report.ev_standard_error[hero]),
            "entry series reproduce the EV standard error of hero " + std::to_string(hero));
    }
  }
  json["heroes"] = heroes;
  json["preflop_response"] = responses;

  // Every chart set, for both players.
  Json chart_sets = Json::array();
  for (const auto &set : options.charts) {
    Json players = Json::array();
    for (std::uint8_t hero = 0; hero < 2U; ++hero) {
      const auto &pav = action_values[hero];
      const auto &tree = trees[hero];
      std::vector<std::optional<mc::ChartFile>> files(pav.nodes.size());
      const mc::ChartLookup lookup = [&](const std::size_t slot, const std::size_t hand_class) {
        const auto &chart = chart_of.at(pav.nodes[slot]);
        const auto path = set.directory / chart.position / chart.name;
        if (!files[slot]) {
          files[slot] = mc::read_chart(path);
        }
        return mc::chart_row(chart, *files[slot], class_labels[hand_class], path);
      };
      const auto charts = mc::chart_strategy(tree, ours[hero], lookup);
      const auto loss = mc::chart_loss(tree, pav, ours[hero], charts);
      check(std::abs(loss.loss - loss.loss_recursive) <= check_tolerance,
            "chart set " + set.name + ": loss by nodes equals the recursion for hero " +
                std::to_string(hero));
      check(loss.loss >= -report.gain_preflop[hero] - check_tolerance,
            "chart set " + set.name + ": no preflop strategy beats the best preflop response "
                                      "for hero " +
                std::to_string(hero));
      double standard_error = 0.0;
      if (mode == "sampled" && groups.size() > 1U) {
        auto reach = mc::entry_reach(game, tree, hero, ours[hero]);
        const auto charts_reach = mc::entry_reach(game, tree, hero, charts.rows);
        for (std::size_t entry = 0; entry < reach.size(); ++entry) {
          for (std::size_t hand_class = 0; hand_class < mc::class_count; ++hand_class) {
            reach[entry][hand_class] -= charts_reach[entry][hand_class];
          }
        }
        standard_error = mc::standard_error(mc::group_series(pointers, hero, reach));
      }
      Json nodes = Json::array();
      for (std::size_t slot = 0; slot < pav.nodes.size(); ++slot) {
        const auto &chart = chart_of.at(pav.nodes[slot]);
        const auto &term = loss.nodes[slot];
        Json columns = Json::array();
        for (const auto index : chart.columns) {
          columns.push_back(chart.tokens[index]);
        }
        std::vector<std::size_t> ranked(mc::class_count);
        for (std::size_t index = 0; index < ranked.size(); ++index) {
          ranked[index] = index;
        }
        std::stable_sort(ranked.begin(), ranked.end(), [&](const std::size_t a, const std::size_t b) {
          return term.classes[a].loss > term.classes[b].loss;
        });
        Json largest = Json::array();
        for (std::size_t index = 0; index < 10U; ++index) {
          largest.push_back({class_labels[ranked[index]], term.classes[ranked[index]].loss});
        }
        Json largest_negative = Json::array();
        for (std::size_t index = 0; index < 5U; ++index) {
          const auto hand_class = ranked[ranked.size() - 1U - index];
          largest_negative.push_back({class_labels[hand_class], term.classes[hand_class].loss});
        }
        Json per_class = Json::object();
        for (std::size_t hand_class = 0; hand_class < mc::class_count; ++hand_class) {
          const auto &entry = term.classes[hand_class];
          Json strategy_ours = Json::object();
          Json strategy_charts = Json::object();
          for (std::size_t action = 0; action < chart.tokens.size(); ++action) {
            strategy_ours[chart.tokens[action]] = ours[hero][slot][hand_class][action];
            strategy_charts[chart.tokens[action]] = charts.rows[slot][hand_class][action];
          }
          per_class[class_labels[hand_class]] = {
              {"combos", class_combos[hand_class]},
              {"reach_ours", entry.reach_ours},
              {"reach_charts", entry.reach_charts},
              {"opponent_reach", entry.opponent_reach},
              {"row", mc::row_source_name(entry.source)},
              {"strategy_ours", strategy_ours},
              {"strategy_charts", strategy_charts},
              {"best_action", chart.tokens[entry.best_action]},
              {"local_loss_antes", entry.local_loss},
              {"regret_ours_antes", entry.regret_ours},
              {"loss_antes", entry.loss}};
        }
        nodes.push_back({{"chart", chart.relative},
                         {"node", pav.nodes[slot]},
                         {"path_id", pb::node_path_id(game, pav.nodes[slot])},
                         {"columns", columns},
                         {"loss_antes", term.loss},
                         {"loss_pot_percent", percent(term.loss)},
                         {"reach_combos_ours", term.reach_combos_ours},
                         {"reach_combos_charts", term.reach_combos_charts},
                         {"largest", largest},
                         {"largest_negative", largest_negative},
                         {"classes", per_class}});
      }
      players.push_back({{"hero", hero},
                         {"position", pb::position_name(game, hero)},
                         {"ev_ours_antes", report.ev[hero]},
                         {"ev_charts_antes", report.ev[hero] - loss.loss},
                         {"loss_antes", loss.loss},
                         {"loss_pot_percent", percent(loss.loss)},
                         {"loss_standard_error_antes", standard_error},
                         {"loss_recursive_antes", loss.loss_recursive},
                         {"positive_loss_antes", loss.positive},
                         {"negative_loss_antes", loss.negative},
                         {"fallback_reach_combos", loss.fallback_reach_combos},
                         {"within_target", loss.loss <= target_antes},
                         {"nodes", nodes}});
      std::cout << "chart set " << set.name << " " << pb::position_name(game, hero) << ": EV ours "
                << report.ev[hero] << ", charts " << report.ev[hero] - loss.loss << ", loss "
                << loss.loss << " antes (" << percent(loss.loss) << " % of the pot)";
      if (standard_error > 0.0) {
        std::cout << " +- " << standard_error;
      }
      std::cout << ", recursive " << loss.loss_recursive << ", positive " << loss.positive
                << ", negative " << loss.negative << ", fallback reach "
                << loss.fallback_reach_combos << " combos\n";
    }
    chart_sets.push_back(
        {{"name", set.name}, {"directory", set.directory.generic_string()}, {"players", players}});
  }
  json["chart_sets"] = chart_sets;
  const double values_seconds = std::chrono::duration<double>(Clock::now() - values_started).count();
  json["evaluation"]["seconds"] = {{"load", load_seconds},
                                   {"stage_one", stage_seconds},
                                   {"aggregate", aggregate_seconds},
                                   {"values", values_seconds}};
  json["evaluation"]["process_after_load"] = load_peaks;
  json["evaluation"]["process_peaks"] = peaks_json();
  json["self_checks"] = {{"passed", failures.empty()}, {"failures", failures}};

  if (!options.output_path.empty()) {
    std::ofstream output(options.output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
      throw std::runtime_error("cannot write " + options.output_path.string());
    }
    output << json.dump(1) << '\n';
    if (!output) {
      throw std::runtime_error("cannot write " + options.output_path.string());
    }
  }
  std::cout << "evaluation: " << mode << ", " << groups.size() << " flops (" << physical_flops
            << " physical, " << boards << " boards), EV [" << report.ev[0] << ", " << report.ev[1]
            << "], stage one " << stage_seconds << " s\n";
  if (!failures.empty()) {
    for (const auto &failure : failures) {
      std::cerr << "self-check failed: " << failure << '\n';
    }
    std::cout << "PREFLOP_BLUEPRINT_MONKER_VALUES=FAIL self-checks " << failures.size() << '\n';
    return 1;
  }
  std::cout << "PREFLOP_BLUEPRINT_MONKER_VALUES=PASS mode=" << mode << '\n';
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_MONKER_VALUES=FAIL " << error.what() << '\n';
    return 1;
  }
}
