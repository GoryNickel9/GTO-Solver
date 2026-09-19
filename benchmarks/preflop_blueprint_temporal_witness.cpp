#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "preflop_blueprint_temporal_distance.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>

namespace {
namespace ca = gtosd::card_abstraction;
namespace research = gtosd::research;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
struct Features {
  std::vector<research::EquityHistogram> turns;
  research::EquityHistogram marginal{};
};
using StateKey = std::pair<std::uint32_t, std::uint16_t>;
StateKey state_key(const Json &observation) {
  const auto flop = observation.at("flop_index").get<std::int64_t>();
  const auto combo = observation.at("combo").get<std::int64_t>();
  if (flop < 0 || flop >= ca::canonical_flop_count || combo < 0 || combo >= ca::combo_count) {
    throw std::runtime_error("invalid observation indices");
  }
  return {static_cast<std::uint32_t>(flop), static_cast<std::uint16_t>(combo)};
}
Features features(const Json &observation, const ca::BoardCatalog &catalog,
                  const ca::FlopFeatureTable &flop, const ca::TurnFeatureTable &turn) {
  const auto [flop_id, combo] = state_key(observation);
  const auto &board = catalog.flops()[flop_id].cards;
  const auto raw = ca::combo_table().cards[combo];
  const std::array<gtosd::CardId, 2> hand{gtosd::CardId::from_index(raw[0]).value(),
                                          gtosd::CardId::from_index(raw[1]).value()};
  const auto board_mask = board[0].mask() | board[1].mask() | board[2].mask();
  if ((ca::combo_table().masks[combo] & board_mask) != 0) {
    throw std::runtime_error("blocked observation");
  }
  const auto mask = board_mask | hand[0].mask() | hand[1].mask();
  const auto flop_lookup = catalog.lookup_flop(board);
  if (!flop_lookup) {
    throw std::runtime_error("flop lookup failed");
  }
  const auto canonical_hand =
      ca::combo_index(ca::permute_card(hand[0], flop_lookup.value().permutation),
                      ca::permute_card(hand[1], flop_lookup.value().permutation));
  Features result;
  const auto marginal = flop.histogram(flop_lookup.value().index, canonical_hand);
  std::copy(marginal.begin(), marginal.end(), result.marginal.begin());
  std::array<std::uint32_t, ca::equity_histogram_bins> sum{};
  for (std::uint8_t card = 0; card < 36; ++card) {
    const auto next = gtosd::CardId::from_index(card).value();
    if ((mask & next.mask()) != 0) {
      continue;
    }
    const auto lookup = catalog.lookup_flop_turn(board, next);
    if (!lookup) {
      throw std::runtime_error("turn lookup failed");
    }
    const auto transformed = ca::combo_index(ca::permute_card(hand[0], lookup.value().permutation),
                                             ca::permute_card(hand[1], lookup.value().permutation));
    const auto histogram = turn.histogram(lookup.value().index, transformed);
    research::EquityHistogram counts{};
    std::copy(histogram.begin(), histogram.end(), counts.begin());
    if (std::accumulate(counts.begin(), counts.end(), 0U) != 30U) {
      throw std::runtime_error("turn histogram does not sum to 30");
    }
    for (std::size_t bin = 0; bin < sum.size(); ++bin) {
      sum[bin] += counts[bin];
    }
    result.turns.push_back(counts);
  }
  if (result.turns.size() != 31) {
    throw std::runtime_error("expected 31 legal turn cards");
  }
  for (std::size_t bin = 0; bin < sum.size(); ++bin) {
    if (sum[bin] != 2U * result.marginal[bin]) {
      throw std::runtime_error(
          "conditional histograms do not reconstruct the existing flop marginal");
    }
  }
  return result;
}
} // namespace

int main(int argc, char **argv) {
  try {
    const auto started = Clock::now();
    std::map<std::string, std::string> args;
    for (int i = 1; i < argc; i += 2) {
      if (i + 1 >= argc) {
        throw std::runtime_error("missing argument value");
      }
      const std::string key = argv[i];
      if (key != "--resources-dir" && key != "--buckets-dir" && key != "--diagnostics" &&
          key != "--output" && key != "--mode") {
        throw std::runtime_error("unknown argument");
      }
      if (!args.emplace(key, argv[i + 1]).second) {
        throw std::runtime_error("duplicate argument");
      }
    }
    const std::filesystem::path resources = args.at("--resources-dir"),
                                buckets = args.at("--buckets-dir");
    const auto flop = ca::FlopFeatureTable::load(resources / "flop_features_v1.bin");
    const auto turn = ca::TurnFeatureTable::load(resources / "turn_features_v1.bin");
    const auto flop_buckets = ca::BucketTable::load(buckets / "flop_buckets_v1.bin");
    const auto turn_buckets = ca::BucketTable::load(buckets / "turn_buckets_v1.bin");
    if (!flop || !turn || !flop_buckets || !turn_buckets) {
      throw std::runtime_error("feature/bucket resources missing or corrupt");
    }
    if (flop_buckets.value().feature_fingerprint() != flop.value().fingerprint() ||
        turn_buckets.value().feature_fingerprint() != turn.value().fingerprint()) {
      throw std::runtime_error("feature fingerprints do not match bucket sources");
    }
    const auto catalog = ca::BoardCatalog::build();
    std::ifstream input(args.at("--diagnostics"));
    if (!input) {
      throw std::runtime_error("cannot open diagnostics");
    }
    const auto source = Json::parse(input);
    Json report = {
        {"schema", "gtosd.temporal_witness.v1"},
        {"diagnostics", args.at("--diagnostics")},
        {"flop_feature_fingerprint", flop.value().fingerprint()},
        {"turn_feature_fingerprint", turn.value().fingerprint()},
        {"units", "equity-bin units; probabilities normalized"},
        {"method",
         "exact 31-by-31 assignment; ground cost is EMD between conditional river histograms"},
        {"interpretation", "feature discrimination only; no equilibrium or solving result"},
        {"pairs", Json::array()}};
    std::map<std::pair<std::uint32_t, std::uint16_t>, Features> cache;
    const auto get = [&](const Json &observation) -> const Features & {
      const auto key = state_key(observation);
      if (!cache.contains(key)) {
        cache.emplace(key, features(observation, catalog, flop.value(), turn.value()));
      }
      return cache.at(key);
    };
    double distance_seconds = 0;
    const auto mode = args.contains("--mode") ? args.at("--mode") : "pairs";
    if (mode != "pairs" && mode != "controls") {
      throw std::runtime_error("mode must be pairs or controls");
    }
    if (mode == "controls") {
      report.erase("pairs");
      report["schema"] = "gtosd.temporal_controls.v1";
      report["method"] = "leave-one-flop-out nearest neighbor within each existing row; "
                         "average all tied nearest states and their tied best actions";
      report["interpretation"] =
          "frozen-policy local prediction loss; no clustering or Nash result";
      report["nodes"] = Json::array();
      using State = StateKey;
      std::map<std::pair<State, State>, std::array<double, 2>> distances;
      const auto state = [](const Json &observation) -> State { return state_key(observation); };
      for (const auto &node : source.at("nodes")) {
        std::map<std::uint16_t, std::vector<const Json *>> rows;
        for (const auto &observation : node.at("observations")) {
          (void)state_key(observation);
          const auto ev = observation.at("action_ev").get<std::vector<double>>();
          const auto opponent = observation.at("opponent_probability").get<double>();
          const auto hero = observation.at("hero_reach").get<double>();
          const auto weight = observation.at("board_multiplicity").get<double>();
          if (ev.empty() ||
              !std::all_of(ev.begin(), ev.end(),
                           [](const double value) { return std::isfinite(value); }) ||
              !std::isfinite(opponent) || opponent <= 0 || opponent > 1.0 + 1e-12 ||
              !std::isfinite(hero) || hero < 0 || hero > 1.0 + 1e-12 || !std::isfinite(weight) ||
              weight <= 0) {
            throw std::runtime_error("invalid action values or observation weights");
          }
          rows[observation.at("row").get<std::uint16_t>()].push_back(&observation);
        }
        Json node_report = {{"node", node.at("node")}, {"flops", Json::array()}};
        // Each vector holds covered mass, flat loss, temporal loss, total mass.
        std::map<std::uint32_t, std::array<std::array<double, 4>, 2>> sums;
        std::size_t supported = 0, unsupported = 0;
        for (const auto &[row, observations] : rows) {
          (void)row;
          for (const auto *query : observations) {
            const auto query_state = state(*query);
            const auto query_ev = query->at("action_ev").get<std::vector<double>>();
            const auto query_best = *std::max_element(query_ev.begin(), query_ev.end());
            std::array<double, 2> nearest{std::numeric_limits<double>::infinity(),
                                          std::numeric_limits<double>::infinity()};
            std::array<double, 2> losses{};
            std::array<std::size_t, 2> ties{};
            for (const auto *neighbor : observations) {
              auto neighbor_state = state(*neighbor);
              if (query_state.first == neighbor_state.first) {
                continue;
              }
              const auto key = std::minmax(query_state, neighbor_state);
              if (!distances.contains(key)) {
                const auto &left = get(*query);
                const auto &right = get(*neighbor);
                const auto before = Clock::now();
                const auto measured = research::temporal_distance(left.turns, right.turns);
                distance_seconds += std::chrono::duration<double>(Clock::now() - before).count();
                distances.emplace(key,
                                  std::array<double, 2>{measured.marginal, measured.potential});
              }
              const auto &distance_values = distances.at(key);
              const auto neighbor_ev = neighbor->at("action_ev").get<std::vector<double>>();
              if (neighbor_ev.size() != query_ev.size()) {
                throw std::runtime_error("incompatible action counts in row");
              }
              const auto neighbor_best = *std::max_element(neighbor_ev.begin(), neighbor_ev.end());
              double transfer_loss = 0;
              std::size_t best_count = 0;
              for (std::size_t action = 0; action < neighbor_ev.size(); ++action) {
                if (std::abs(neighbor_ev[action] - neighbor_best) <= 1e-12) {
                  transfer_loss += query_best - query_ev[action];
                  ++best_count;
                }
              }
              transfer_loss /= static_cast<double>(best_count);
              for (std::size_t metric = 0; metric < 2; ++metric) {
                if (distance_values[metric] < nearest[metric] - 1e-12) {
                  nearest[metric] = distance_values[metric];
                  losses[metric] = 0;
                  ties[metric] = 0;
                }
                if (std::abs(distance_values[metric] - nearest[metric]) <= 1e-12) {
                  losses[metric] += transfer_loss;
                  ++ties[metric];
                }
              }
            }
            const bool covered = ties[0] != 0;
            if (covered != (ties[1] != 0)) {
              throw std::runtime_error("metric coverage differs");
            }
            covered ? ++supported : ++unsupported;
            for (std::size_t view = 0; view < 2; ++view) {
              const auto weight = query->at("board_multiplicity").get<double>() *
                                  query->at("opponent_probability").get<double>() *
                                  (view == 0 ? query->at("hero_reach").get<double>() : 1.0);
              auto &sum = sums[query_state.first][view];
              sum[3] += weight;
              if (covered) {
                sum[0] += weight;
                for (std::size_t metric = 0; metric < 2; ++metric) {
                  sum[metric + 1] += weight * losses[metric] / static_cast<double>(ties[metric]);
                }
              }
            }
          }
        }
        std::array<std::array<double, 4>, 2> total{};
        for (const auto &[flop_id, views] : sums) {
          node_report["flops"].push_back({{"flop_index", flop_id}, {"sums", views}});
          for (std::size_t view = 0; view < 2; ++view) {
            for (std::size_t field = 0; field < 4; ++field) {
              total[view][field] += views[view][field];
            }
          }
        }
        for (std::size_t view = 0; view < 2; ++view) {
          const auto &sum = total[view];
          node_report[view == 0 ? "self_reach" : "forced_hero_prefix"] = {
              {"covered_mass", sum[0]},
              {"total_mass", sum[3]},
              {"coverage", sum[3] > 0 ? Json(sum[0] / sum[3]) : Json(nullptr)},
              {"flat_loss", sum[0] > 0 ? Json(sum[1] / sum[0]) : Json(nullptr)},
              {"temporal_loss", sum[0] > 0 ? Json(sum[2] / sum[0]) : Json(nullptr)}};
        }
        node_report["supported_observations"] = supported;
        node_report["unsupported_observations"] = unsupported;
        report["nodes"].push_back(std::move(node_report));
      }
      report["unique_pair_distances"] = distances.size();
    }
    if (mode == "pairs") {
      for (const auto &node : source.at("nodes")) {
        for (const auto view : {"self_reach", "forced_hero_prefix"}) {
          for (const auto &row : node.at(view).at("top_rows")) {
            const auto &examples = row.at("examples");
            for (std::size_t i = 0; i < examples.size(); ++i) {
              for (std::size_t j = i + 1; j < examples.size(); ++j) {
                const auto &left = get(examples[i]);
                const auto &right = get(examples[j]);
                const auto before = Clock::now();
                const auto distance = research::temporal_distance(left.turns, right.turns);
                distance_seconds += std::chrono::duration<double>(Clock::now() - before).count();
                const auto flat = static_cast<double>(
                                      research::histogram_distance(left.marginal, right.marginal)) /
                                  465.0;
                if (std::abs(flat - distance.marginal) > 1e-12) {
                  throw std::runtime_error("marginal distance mismatch");
                }
                report["pairs"].push_back({{"node", node.at("node")},
                                           {"row", row.at("row")},
                                           {"view", view},
                                           {"left", examples[i]},
                                           {"right", examples[j]},
                                           {"marginal_distance", flat},
                                           {"potential_distance", distance.potential},
                                           {"extra_separation", distance.potential - flat}});
              }
            }
          }
        }
      }
    }
    report["unique_states"] = cache.size();
    report["distance_seconds"] = distance_seconds;
    report["seconds"] = std::chrono::duration<double>(Clock::now() - started).count();
    std::ofstream output(args.at("--output"));
    if (!output) {
      throw std::runtime_error("cannot create report");
    }
    output << report.dump(2) << '\n';
    output.flush();
    if (!output) {
      throw std::runtime_error("report write failed");
    }
    std::cout << "TEMPORAL_WITNESS=PASS states=" << cache.size() << " mode=" << mode
              << " distance_seconds=" << distance_seconds << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "TEMPORAL_WITNESS=FAIL " << error.what() << '\n';
    return 1;
  }
}
