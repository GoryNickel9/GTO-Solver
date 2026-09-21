// Frozen-continuation diagnostics. This command does not train a policy and
// does not compute a bucket-constrained best response or a Nash certificate.
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/exact_features.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/preflop_blueprint/action_labels.hpp"
#include "gtosd/preflop_blueprint/best_response.hpp"
#include "gtosd/preflop_blueprint/decision_gap.hpp"
#include "gtosd/preflop_blueprint/history_bucket_rows.hpp"
#include "gtosd/preflop_blueprint/policy_file.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {
namespace pb = gtosd::preflop_blueprint;
namespace ca = gtosd::card_abstraction;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
std::string read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot read " + path.string());
  }
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
struct Observation {
  pb::BucketDecisionSample sample;
  double hero_reach{0.0};
  std::uint32_t flop{0};
  std::uint16_t combo{0};
  std::uint16_t bucket{ca::no_bucket};
  std::uint16_t comparison_bucket{ca::no_bucket};
  std::uint8_t hand_class{0U};
  std::array<std::uint16_t, ca::equity_histogram_bins> feature{};
};
Json witness(const Observation &observation, const ca::BoardCatalog &catalog,
             const std::size_t actions) {
  Json board = Json::array();
  for (const auto card : catalog.flops()[observation.flop].cards) {
    board.push_back(gtosd::format_card(card));
  }
  Json hand = Json::array();
  for (const auto card : ca::combo_table().cards[observation.combo]) {
    hand.push_back(gtosd::format_card(gtosd::CardId::from_index(card).value()));
  }
  Json ev = Json::array();
  for (std::size_t action = 0; action < actions; ++action) {
    ev.push_back(observation.sample.action_values[action] /
                 observation.sample.opponent_probability);
  }
  return {{"flop_index", observation.flop},
          {"board", board},
          {"combo", observation.combo},
          {"hand", hand},
          {"hand_class", observation.hand_class},
          {"bucket", observation.bucket},
          {"comparison_bucket", observation.comparison_bucket},
          {"feature", observation.feature},
          {"action_ev", ev},
          {"opponent_probability", observation.sample.opponent_probability},
          {"hero_reach", observation.hero_reach}};
}
} // namespace

int main(const int argc, char **argv) {
  try {
    const auto started = Clock::now();
    std::map<std::string, std::string> args;
    for (int i = 1; i < argc; i += 2) {
      if (i + 1 >= argc) {
        throw std::runtime_error("missing argument value");
      }
      const std::string name = argv[i];
      if (name != "--config" && name != "--resources-dir" && name != "--buckets-dir" &&
          name != "--policy" && name != "--reference-certificate" && name != "--rows" &&
          name != "--history-rows" && name != "--comparison-buckets-dir" && name != "--flops" &&
          name != "--seed" && name != "--threads" && name != "--nodes" && name != "--output" &&
          name != "--export-observations") {
        throw std::runtime_error("unknown option " + name);
      }
      if (!args.emplace(name, argv[i + 1]).second) {
        throw std::runtime_error("duplicate option " + name);
      }
    }
    const auto required = [&](const std::string &key) -> const std::string & {
      if (!args.contains(key)) {
        throw std::runtime_error("required " + key);
      }
      return args.at(key);
    };
    const auto value = [&](const std::string &key, const std::string &fallback) {
      return args.contains(key) ? args.at(key) : fallback;
    };
    const auto requested_flops = std::stoul(value("--flops", "16"));
    const auto threads = std::stoul(value("--threads", "8"));
    const auto seed = std::stoull(value("--seed", "20260919"));
    const auto export_value = value("--export-observations", "false");
    if (export_value != "true" && export_value != "false") {
      throw std::runtime_error("export-observations must be true or false");
    }
    const bool export_observations = export_value == "true";
    if (requested_flops == 0 || requested_flops > ca::canonical_flop_count || threads == 0 ||
        threads > 64) {
      throw std::runtime_error("invalid flop/thread limit");
    }
    const auto config = pb::parse_game_config_json(read(required("--config")));
    if (!config) {
      throw std::runtime_error("invalid game config");
    }
    const auto game = pb::CompiledGame::compile(config.value());
    if (!game) {
      throw std::runtime_error("game compilation failed");
    }
    const std::filesystem::path resource_dir = required("--resources-dir"),
                                bucket_dir = required("--buckets-dir");
    auto ranks = ca::RankTable::load(resource_dir / "rank_table_v1.bin");
    auto all_in = ca::AllInTable::load(resource_dir / "preflop_all_in_v1.bin");
    auto flop = ca::BucketTable::load(bucket_dir / "flop_buckets_v1.bin");
    auto turn = ca::BucketTable::load(bucket_dir / "turn_buckets_v1.bin");
    auto river = ca::BucketTable::load(bucket_dir / "river_buckets_v1.bin");
    if (!ranks || !all_in || !flop || !turn || !river) {
      throw std::runtime_error("missing/invalid resources");
    }
    auto flop_features = ca::FlopFeatureTable::load(resource_dir / "flop_features_v1.bin");
    if (!flop_features ||
        flop_features.value().fingerprint() != flop.value().feature_fingerprint()) {
      throw std::runtime_error("missing/incompatible flop features");
    }
    std::optional<ca::BucketTable> comparison_flop;
    if (args.contains("--comparison-buckets-dir")) {
      auto loaded = ca::BucketTable::load(
          std::filesystem::path(args.at("--comparison-buckets-dir")) / "flop_buckets_v1.bin");
      if (!loaded || loaded.value().feature_fingerprint() != flop.value().feature_fingerprint() ||
          loaded.value().catalog_fingerprint() != flop.value().catalog_fingerprint()) {
        throw std::runtime_error("incompatible comparison flop buckets");
      }
      comparison_flop.emplace(std::move(loaded.value()));
    }
    const auto catalog = ca::BoardCatalog::build();
    const auto policy = pb::load_policy(required("--policy"), game.value());
    if (!policy) {
      throw std::runtime_error("policy rejected");
    }
    const auto reference = Json::parse(read(required("--reference-certificate")));
    const auto &layout = policy.value()->layout();
    const std::array<std::uint32_t, 3> counts{layout.flop_capacity, layout.turn_capacity,
                                              layout.river_capacity};
    if (reference.at("tree_fingerprint") != game.value().fingerprint() ||
        reference.at("rules_fingerprint") != pb::game_config_fingerprint(config.value()) ||
        reference.at("policy_fingerprint") != pb::policy_fingerprint(*policy.value()) ||
        reference.at("catalog_fingerprint") != catalog.fingerprint() ||
        reference.at("flop_table_fingerprint") != flop.value().fingerprint() ||
        reference.at("turn_table_fingerprint") != turn.value().fingerprint() ||
        reference.at("river_table_fingerprint") != river.value().fingerprint() ||
        reference.at("capacities") != Json(counts)) {
      throw std::runtime_error("reference fingerprint/capacity mismatch");
    }
    std::optional<pb::ClassBucketRows> class_rows;
    std::optional<pb::HistoryBucketRows> history_rows;
    const auto row_kind = value("--rows", "class");
    if (row_kind == "class") {
      auto built = pb::ClassBucketRows::build(flop.value(), turn.value(), river.value());
      if (!built) {
        throw std::runtime_error("class mapping failed");
      }
      class_rows.emplace(std::move(built.value()));
    } else if (row_kind == "history") {
      if (!args.contains("--history-rows")) {
        throw std::runtime_error("history rows path required");
      }
      auto loaded = pb::HistoryBucketRows::load(args.at("--history-rows"));
      if (!loaded) {
        throw std::runtime_error("history mapping failed");
      }
      history_rows.emplace(std::move(loaded.value()));
      if (!reference.contains("history_map_fingerprint") ||
          reference.at("history_map_fingerprint") != history_rows->fingerprint() ||
          layout.flop_capacity != history_rows->count(ca::BucketStreet::Flop) ||
          layout.turn_capacity != history_rows->count(ca::BucketStreet::Turn) ||
          layout.river_capacity != history_rows->count(ca::BucketStreet::River)) {
        throw std::runtime_error("reference/history mapping mismatch");
      }
    } else if (row_kind != "base") {
      throw std::runtime_error("rows must be class, history or base");
    }
    pb::BestResponseResources resources{&ranks.value(),
                                        &all_in.value(),
                                        &catalog,
                                        &flop.value(),
                                        &turn.value(),
                                        &river.value(),
                                        class_rows ? &*class_rows : nullptr,
                                        history_rows ? &*history_rows : nullptr};
    const auto evaluator =
        pb::BestResponseEvaluator::create(game.value(), *policy.value(), resources);
    if (!evaluator) {
      throw std::runtime_error("evaluator rejects policy/mapping");
    }
    std::vector<std::uint32_t> nodes;
    if (args.contains("--nodes")) {
      std::istringstream input(args.at("--nodes"));
      std::string item;
      while (std::getline(input, item, ',')) {
        nodes.push_back(static_cast<std::uint32_t>(std::stoul(item)));
      }
    } else {
      for (const auto entry : game.value().postflop_entries()) {
        const auto first = game.value().edges_of(entry).front().child;
        nodes.push_back(first);
        for (const auto &edge : game.value().edges_of(first)) {
          if (game.value().nodes()[edge.child].kind == pb::NodeKind::Decision) {
            nodes.push_back(edge.child);
          }
        }
      }
    }
    if (nodes.empty() || nodes.size() > 32) {
      throw std::runtime_error("need 1..32 nodes");
    }
    std::sort(nodes.begin(), nodes.end());
    if (std::adjacent_find(nodes.begin(), nodes.end()) != nodes.end()) {
      throw std::runtime_error("duplicate node");
    }
    for (const auto node : nodes) {
      if (node >= game.value().nodes().size() ||
          game.value().nodes()[node].kind != pb::NodeKind::Decision ||
          game.value().nodes()[node].street != gtosd::Street::Flop) {
        throw std::runtime_error("diagnostic accepts flop decisions only");
      }
    }
    std::vector<std::uint32_t> selected(catalog.flops().size());
    std::iota(selected.begin(), selected.end(), 0U);
    ca::DeterministicRandom random(seed);
    for (std::size_t i = 0; i < selected.size(); ++i) {
      std::swap(
          selected[i],
          selected[i + random.uniform_below(static_cast<std::uint32_t>(selected.size() - i))]);
    }
    selected.resize(requested_flops);
    std::vector<std::vector<pb::NodeProbe>> probes(selected.size(),
                                                   std::vector<pb::NodeProbe>(nodes.size()));
    std::atomic<std::size_t> next{0}, done{0};
    std::atomic<bool> failed{false};
    std::mutex mutex;
    std::exception_ptr failure;
    std::vector<std::thread> workers;
    for (std::size_t worker = 0; worker < std::min<std::size_t>(threads, selected.size());
         ++worker) {
      workers.emplace_back([&] {
        try {
          while (!failed) {
            const auto i = next.fetch_add(1);
            if (i >= selected.size()) {
              break;
            }
            const auto group = pb::full_runouts(catalog.flops()[selected[i]].cards);
            for (std::size_t j = 0; j < nodes.size(); ++j) {
              auto probe = evaluator.value().probe_node(group, nodes[j],
                                                        game.value().nodes()[nodes[j]].actor);
              if (!probe || !probe.value().found) {
                throw std::runtime_error("node probe failed");
              }
              probes[i][j] = std::move(probe.value());
            }
            const auto count = done.fetch_add(1) + 1;
            std::lock_guard lock(mutex);
            std::cout << Json({{"event", "flop_done"},
                               {"done", count},
                               {"total", selected.size()},
                               {"seconds",
                                std::chrono::duration<double>(Clock::now() - started).count()}})
                             .dump()
                      << std::endl;
          }
        } catch (...) {
          std::lock_guard lock(mutex);
          if (!failure) {
            failure = std::current_exception();
          }
          failed = true;
        }
      });
    }
    for (auto &worker : workers) {
      worker.join();
    }
    if (failure) {
      std::rethrow_exception(failure);
    }
    Json report = {{"schema", "gtosd.bucket_decision_diagnostics.v1"},
                   {"rows", row_kind},
                   {"tree_fingerprint", game.value().fingerprint()},
                   {"policy_fingerprint", pb::policy_fingerprint(*policy.value())},
                   {"flop_feature_fingerprint", flop_features.value().fingerprint()},
                   {"comparison_flop_table_fingerprint",
                    comparison_flop ? Json(comparison_flop->fingerprint()) : Json(nullptr)},
                   {"reference_certificate", required("--reference-certificate")},
                   {"flop_indices", selected},
                   {"seed", seed},
                   {"full_flop_coverage", selected.size() == catalog.flops().size()},
                   {"exact_future_runouts", true},
                   {"interpretation",
                    "local frozen-continuation gains; not NashConv or an abstract best response"},
                   {"nodes", Json::array()}};
    const auto &combos = ca::combo_table();
    for (std::size_t j = 0; j < nodes.size(); ++j) {
      const auto node = nodes[j];
      const auto &target = game.value().nodes()[node];
      std::map<std::uint32_t, std::vector<Observation>> buckets;
      for (std::size_t i = 0; i < selected.size(); ++i) {
        const auto &board = catalog.flops()[selected[i]];
        const auto &probe = probes[i][j];
        const auto mask = board.cards[0].mask() | board.cards[1].mask() | board.cards[2].mask();
        for (std::uint16_t combo = 0; combo < ca::combo_count; ++combo) {
          if ((combos.masks[combo] & mask) != 0U) {
            continue;
          }
          const std::array<gtosd::CardId, 2> hand{
              gtosd::CardId::from_index(combos.cards[combo][0]).value(),
              gtosd::CardId::from_index(combos.cards[combo][1]).value()};
          const auto lookup = ca::lookup_flop_bucket(catalog, flop.value(), board.cards, hand);
          if (!lookup) {
            throw std::runtime_error("flop lookup failed");
          }
          const auto row =
              history_rows ? history_rows->row(gtosd::Street::Flop, combos.hand_class[combo],
                                               lookup.value().bucket, ca::no_bucket, ca::no_bucket)
              : class_rows ? class_rows->row(ca::BucketStreet::Flop, combos.hand_class[combo],
                                             lookup.value().bucket)
                           : lookup.value().bucket;
          if (row == pb::no_history_row) {
            throw std::runtime_error("history row lookup failed");
          }
          Observation observation;
          observation.flop = selected[i];
          observation.combo = combo;
          observation.bucket = lookup.value().bucket;
          observation.comparison_bucket =
              comparison_flop
                  ? comparison_flop->bucket(lookup.value().row_index, lookup.value().combo)
                  : ca::no_bucket;
          observation.hand_class = combos.hand_class[combo];
          std::copy(
              flop_features.value()
                  .histogram(lookup.value().row_index, lookup.value().combo)
                  .begin(),
              flop_features.value().histogram(lookup.value().row_index, lookup.value().combo).end(),
              observation.feature.begin());
          observation.sample.weight = board.multiplicity;
          observation.sample.opponent_probability = probe.opponent_mass[combo];
          for (std::size_t action = 0; action < target.action_count; ++action) {
            observation.sample.action_values[action] = probe.action_values[action][combo];
          }
          observation.hero_reach = 1.0;
          auto cursor = game.value().root();
          for (const auto action : pb::path_edges(game.value(), node)) {
            const auto &ancestor = game.value().nodes()[cursor];
            if (ancestor.kind == pb::NodeKind::Decision && ancestor.actor == target.actor) {
              const auto ancestor_row =
                  ancestor.street == gtosd::Street::Preflop ? combos.hand_class[combo] : row;
              observation.hero_reach *= policy.value()->row(cursor, ancestor_row)[action];
            }
            cursor = game.value().edges_of(cursor)[action].child;
          }
          buckets[row].push_back(observation);
        }
      }
      Json node_report = {{"node", node},
                          {"path", pb::node_path_id(game.value(), node)},
                          {"hero", target.actor},
                          {"actions", pb::edge_labels(game.value(), node)},
                          {"observed_rows", buckets.size()}};
      if (export_observations) {
        node_report["observations"] = Json::array();
        for (const auto &[row, observations] : buckets) {
          for (const auto &observation : observations) {
            if (observation.sample.opponent_probability <= 0) {
              continue;
            }
            auto record = witness(observation, catalog, target.action_count);
            record["row"] = row;
            record["board_multiplicity"] = observation.sample.weight;
            node_report["observations"].push_back(std::move(record));
          }
        }
      }
      for (const bool force_prefix : {false, true}) {
        double mass = 0, shared = 0, separated = 0;
        std::vector<std::pair<double, Json>> top;
        for (const auto &[row, observations] : buckets) {
          std::vector<pb::BucketDecisionSample> samples;
          samples.reserve(observations.size());
          for (const auto &observation : observations) {
            auto sample = observation.sample;
            if (!force_prefix) {
              sample.weight *= observation.hero_reach;
            }
            samples.push_back(sample);
          }
          const auto gap = pb::bucket_decision_gap(samples, policy.value()->row(node, row));
          if (!gap) {
            throw std::runtime_error("invalid decision-gap input");
          }
          mass += gap.value().mass;
          shared += gap.value().shared_strategy_gain;
          separated += gap.value().separation_gain;
          if (gap.value().mass <= 0) {
            continue;
          }
          Json examples = Json::array();
          std::array<bool, pb::maximum_actions> seen{};
          for (std::size_t k = 0; k < observations.size(); ++k) {
            if (samples[k].weight == 0 || samples[k].opponent_probability <= 0) {
              continue;
            }
            const auto first = samples[k].action_values.begin();
            const auto best = static_cast<std::size_t>(
                std::max_element(first, first + target.action_count) - first);
            if (!seen[best]) {
              seen[best] = true;
              examples.push_back(witness(observations[k], catalog, target.action_count));
            }
          }
          const auto strategy = policy.value()->row(node, row);
          Json record = {
              {"row", row},
              {"observations", observations.size()},
              {"mass", gap.value().mass},
              {"shared_strategy_gain", gap.value().shared_strategy_gain / gap.value().mass},
              {"separation_gain", gap.value().separation_gain / gap.value().mass},
              {"shared_action", gap.value().shared_action},
              {"strategy", std::vector<double>(strategy.begin(), strategy.end())},
              {"examples", examples}};
          top.emplace_back(gap.value().separation_gain, std::move(record));
        }
        std::stable_sort(top.begin(), top.end(),
                         [](const auto &a, const auto &b) { return a.first > b.first; });
        Json top_rows = Json::array();
        for (std::size_t k = 0; k < std::min<std::size_t>(5, top.size()); ++k) {
          top_rows.push_back(top[k].second);
        }
        node_report[force_prefix ? "forced_hero_prefix" : "self_reach"] = {
            {"mass", mass},
            {"shared_strategy_gain", mass > 0 ? Json(shared / mass) : Json(nullptr)},
            {"separation_gain", mass > 0 ? Json(separated / mass) : Json(nullptr)},
            {"top_rows", top_rows}};
      }
      report["nodes"].push_back(std::move(node_report));
    }
    report["seconds"] = std::chrono::duration<double>(Clock::now() - started).count();
    report["threads"] = threads;
    std::ofstream output(required("--output"));
    if (!output) {
      throw std::runtime_error("cannot create output");
    }
    output << report.dump(2) << '\n';
    output.flush();
    if (!output) {
      throw std::runtime_error("output write failed");
    }
    std::cout << "BUCKET_DIAGNOSTICS=PASS seconds=" << report["seconds"] << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "BUCKET_DIAGNOSTICS=FAIL " << error.what() << '\n';
    return 1;
  }
}
