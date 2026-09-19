// Preregistered small mechanism experiment. No production policy is trained.
#include "finite_game_audit_json.hpp"
#include "gtosd/solver/enumerated_best_response.hpp"
#include "gtosd/solver/reference_games.hpp"
#include "recall_witness.hpp"
#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace {
using namespace gtosd;
using Json = nlohmann::json;

FiniteGame make_game(const std::string &partition, const double scale) {
  auto game = research::forgotten_type(partition == "lossless", -3);
  game.game_id = "recall-mechanism." + partition;
  if (partition == "blind") {
    game.nodes[1].information_set = "before";
    game.nodes[6].information_set = "before";
  }
  for (auto &node : game.nodes) {
    for (auto &payoff : node.payoff) {
      payoff *= scale;
    }
  }
  return game;
}

StrategyProfile lift(const StrategyProfile &profile, const std::string &partition,
                     const FiniteGame &physical) {
  auto result = uniform_strategy_profile(physical).value();
  for (auto &[key, strategy] : result) {
    auto source = key;
    if (partition != "lossless" && (key == "L_after" || key == "R_after")) {
      source = "merged";
    } else if (partition == "blind" && (key == "L_before" || key == "R_before")) {
      source = "before";
    }
    strategy = profile.at(source);
  }
  return result;
}

Json preflight(const FiniteGame &game) {
  Json result{{"game", game.game_id}, {"nodes", game.nodes.size()}};
  result["players"] = Json::array();
  for (const std::uint8_t player : {std::uint8_t{0}, std::uint8_t{1}}) {
    const auto count = estimate_best_response_enumeration(game, player);
    const auto recall = has_perfect_recall(game, player);
    if (!count || !recall) {
      throw std::runtime_error("invalid reference game preflight");
    }
    result["players"].push_back({{"player", player},
                                 {"information_sets", count.value().information_sets},
                                 {"policies", count.value().policies},
                                 {"node_evaluations", count.value().node_evaluations},
                                 {"overflow", count.value().overflow},
                                 {"perfect_recall", recall.value()}});
  }
  return result;
}
} // namespace

int main(const int argc, char **argv) {
  try {
    if (argc != 2) {
      throw std::runtime_error("usage: gtosd_constrained_br_audit OUTPUT.json");
    }
    const auto start = std::chrono::steady_clock::now();
    Json output{{"schema", "gtosd.research.constrained_br_audit.v1"},
                {"scope", "analytic_mechanism_only_not_co40"},
                {"precision", "float64"},
                {"threads", 1},
                {"chance", "exact_2_types_each_probability_0.5"},
                {"sampled_metric", false},
                {"checkpoints", {1000, 10000, 100000}},
                {"sampling_seeds", {101, 202, 303}},
                {"payoff_scales", {1, 10, 40}}};
    output["preflight"] = {preflight(make_kuhn_poker_game()),
                           preflight(make_short_deck_river_toy_game().value()),
                           preflight(make_short_deck_four_street_toy_game().value())};
    output["runs"] = Json::array();
    output["oracle_cases"] = Json::array();
    for (const auto &game : {research::forgotten_type(false), research::forgotten_type(true),
                             make_kuhn_poker_game(), make_short_deck_river_toy_game().value()}) {
      auto profile = uniform_strategy_profile(game).value();
      for (auto &[key, strategy] : profile) {
        static_cast<void>(key);
        if (strategy.probabilities.size() == 2) {
          strategy.probabilities = {0.23, 0.77};
        }
      }
      Json expected = Json::array();
      for (const std::uint8_t player : {std::uint8_t{0}, std::uint8_t{1}}) {
        const auto response = enumerated_best_response(game, profile, player);
        if (!response) {
          throw std::runtime_error("oracle export enumeration failed");
        }
        expected.push_back(response.value().response.value);
      }
      output["oracle_cases"].push_back({{"game", research::finite_game_json(game)},
                                        {"profile", research::profile_json(profile)},
                                        {"expected_br", expected}});
    }
    for (const std::string partition : {"lossless", "forget_later", "blind"}) {
      for (const auto algorithm : {SolverAlgorithm::LinearCfr, SolverAlgorithm::LinearMccfr}) {
        for (const std::uint64_t seed : {101U, 202U, 303U}) {
          if (algorithm == SolverAlgorithm::LinearCfr && seed != 101) {
            continue;
          }
          for (const double scale : {1.0, 10.0, 40.0}) {
            const auto game = make_game(partition, scale);
            const auto physical = make_game("lossless", scale);
            std::optional<SolverCheckpoint> checkpoint;
            for (const std::uint64_t iterations : {1000U, 10000U, 100000U}) {
              SolverConfig config;
              config.algorithm = algorithm;
              config.seed = seed;
              config.iterations = iterations;
              const auto solved =
                  solve_finite_game(game, config, checkpoint ? &*checkpoint : nullptr);
              if (!solved) {
                throw std::runtime_error(solver_error_name(solved.error()));
              }
              checkpoint = solved.value().checkpoint;
              const auto &profile = solved.value().average_strategy;
              const auto value = evaluate_strategy_profile(game, profile).value()[0];
              const auto constrained = enumerated_best_response(game, profile, 0);
              const auto physical_profile = lift(profile, partition, physical);
              const auto unrestricted = enumerated_best_response(physical, physical_profile, 0);
              if (!constrained || !unrestricted) {
                throw std::runtime_error("exhaustive BR did not complete");
              }
              const double expected = partition == "blind" ? 0 : scale;
              if (std::abs(constrained.value().response.value - expected) > scale * 1e-12 ||
                  std::abs(unrestricted.value().response.value - scale) > scale * 1e-12 ||
                  std::abs(evaluate_strategy_profile(physical, physical_profile).value()[0] -
                           value) > scale * 1e-12) {
                throw std::runtime_error("analytic optimum or policy lifting mismatch");
              }
              const double internal = constrained.value().response.value - value;
              const double information =
                  unrestricted.value().response.value - constrained.value().response.value;
              output["runs"].push_back(
                  {{"partition", partition},
                   {"algorithm", solver_algorithm_name(algorithm)},
                   {"seed", seed},
                   {"iterations", iterations},
                   {"payoff_scale", scale},
                   {"game_fingerprint", finite_game_fingerprint(game)},
                   {"ev", value},
                   {"constrained_br", constrained.value().response.value},
                   {"physical_br", unrestricted.value().response.value},
                   {"internal_gain", internal},
                   {"information_gap", information},
                   {"physical_gain", internal + information},
                   {"normalized_internal_gain", internal / scale},
                   {"enumerated_policies", constrained.value().work.policies}});
            }
            std::cout << partition << ' ' << solver_algorithm_name(algorithm) << " seed=" << seed
                      << " scale=" << scale << " complete\n"
                      << std::flush;
          }
        }
      }
    }
    output["seconds"] =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::ofstream file(argv[1]);
    file << output.dump(2) << '\n';
    file.close();
    if (!file) {
      throw std::runtime_error("cannot write report");
    }
    std::cout << "CONSTRAINED_BR_AUDIT=PASS rows=" << output["runs"].size()
              << " seconds=" << output["seconds"] << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "CONSTRAINED_BR_AUDIT=FAIL " << error.what() << '\n';
    return 1;
  }
}
