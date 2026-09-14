#include "gtosd/solver/best_response.hpp"
#include "gtosd/solver/reference_games.hpp"
#include "gtosd/solver/solver.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using Json = nlohmann::json;

struct Arguments {
  std::uint64_t iterations{60'000U};
  std::uint64_t interval{20'000U};
  std::uint64_t seed{0x5232'4156'4552'4147ULL};
  std::string output;
};

std::uint64_t parse_unsigned(const std::string_view text, const std::string_view name) {
  std::size_t consumed = 0U;
  const auto value = std::stoull(std::string{text}, &consumed, 0);
  if (consumed != text.size()) {
    throw std::runtime_error("invalid value for " + std::string{name});
  }
  return value;
}

Arguments parse_arguments(const int argc, char **argv) {
  Arguments arguments;
  for (int index = 1; index < argc; ++index) {
    if (index + 1 >= argc) {
      throw std::runtime_error("missing argument value");
    }
    const std::string_view name = argv[index++];
    const std::string_view value = argv[index];
    if (name == "--iterations") {
      arguments.iterations = parse_unsigned(value, name);
    } else if (name == "--interval") {
      arguments.interval = parse_unsigned(value, name);
    } else if (name == "--seed") {
      arguments.seed = parse_unsigned(value, name);
    } else if (name == "--output") {
      arguments.output = value;
    } else {
      throw std::runtime_error("unknown argument " + std::string{name});
    }
  }
  if (arguments.interval == 0U || arguments.iterations < arguments.interval * 2U ||
      arguments.iterations % arguments.interval != 0U) {
    throw std::runtime_error(
        "iterations must be an exact multiple of interval and contain at least two checkpoints");
  }
  return arguments;
}

std::string checkpoint_fingerprint(const gtosd::SolverCheckpoint &checkpoint) {
  const auto serialized = gtosd::serialize_solver_checkpoint(checkpoint);
  if (!serialized) {
    throw std::runtime_error(std::string{"cannot serialize checkpoint: "} +
                             gtosd::solver_error_name(serialized.error()));
  }
  constexpr std::uint64_t offset = 14'695'981'039'346'656'037ULL;
  constexpr std::uint64_t prime = 1'099'511'628'211ULL;
  auto hash = offset;
  for (const auto byte : serialized.value()) {
    hash ^= static_cast<std::uint8_t>(byte);
    hash *= prime;
  }
  constexpr char digits[] = "0123456789abcdef";
  std::string result = "fnv1a64:";
  for (int shift = 60; shift >= 0; shift -= 4) {
    result.push_back(digits[(hash >> shift) & 0xFU]);
  }
  return result;
}

Json metrics_json(const gtosd::NashConvResult &metrics) {
  return {
      {"profile_ev", metrics.profile_value},
      {"best_response_ev", metrics.best_response_value},
      {"deviation_gain",
       {metrics.best_response_value[0] - metrics.profile_value[0],
        metrics.best_response_value[1] - metrics.profile_value[1]}},
      {"nashconv", metrics.nash_conv},
      {"normalized_nashconv", metrics.normalized_nash_conv},
      {"zero_sum_exploitability", metrics.zero_sum_exploitability},
      {"expected_payoff_sum", metrics.expected_payoff_sum},
  };
}

void write_result(const std::string &path, const std::string &payload) {
  if (path.empty()) {
    std::cout << payload << '\n';
    return;
  }
  const auto destination = std::filesystem::path(path);
  auto temporary = destination;
  temporary += ".tmp";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      throw std::runtime_error("cannot open output temporary file");
    }
    stream << payload << '\n';
    stream.flush();
    if (!stream) {
      throw std::runtime_error("cannot write output temporary file");
    }
  }
  std::error_code error;
  std::filesystem::remove(destination, error);
  error.clear();
  std::filesystem::rename(temporary, destination, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    throw std::runtime_error("cannot atomically replace output file");
  }
}

} // namespace

int main(const int argc, char **argv) {
  try {
    const auto arguments = parse_arguments(argc, argv);
    const auto game = gtosd::make_short_deck_four_street_toy_game(0.0);
    if (!game) {
      throw std::runtime_error(std::string{"cannot build fixture: "} +
                               gtosd::solver_error_name(game.error()));
    }
    const auto summary = gtosd::validate_finite_game(game.value());
    if (!summary) {
      throw std::runtime_error(std::string{"invalid fixture: "} +
                               gtosd::solver_error_name(summary.error()));
    }

    Json checkpoints = Json::array();
    std::optional<gtosd::SolverCheckpoint> resume;
    double maximum_normalization_error = 0.0;
    std::uint64_t traversed_nodes = 0U;
    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t target = arguments.interval; target <= arguments.iterations;
         target += arguments.interval) {
      gtosd::SolverConfig config;
      config.algorithm = gtosd::SolverAlgorithm::LinearMccfr;
      config.iterations = target;
      config.seed = arguments.seed;
      const auto solved =
          gtosd::solve_finite_game(game.value(), config, resume ? &resume.value() : nullptr);
      if (!solved) {
        throw std::runtime_error(std::string{"MCCFR solve failed: "} +
                                 gtosd::solver_error_name(solved.error()));
      }
      const auto metrics =
          gtosd::calculate_nash_conv(game.value(), solved.value().average_strategy);
      if (!metrics) {
        throw std::runtime_error(std::string{"NashConv failed: "} +
                                 gtosd::solver_error_name(metrics.error()));
      }
      for (std::size_t player = 0U; player < 2U; ++player) {
        if (metrics.value().best_response_value[player] + 1.0e-12 <
            metrics.value().profile_value[player]) {
          throw std::runtime_error("best response underperforms the frozen profile");
        }
      }
      maximum_normalization_error =
          std::max(maximum_normalization_error, solved.value().maximum_normalization_error);
      traversed_nodes += solved.value().traversed_nodes;
      Json point = metrics_json(metrics.value());
      point["iteration"] = target;
      point["checkpoint_fingerprint"] = checkpoint_fingerprint(solved.value().checkpoint);
      checkpoints.push_back(std::move(point));
      resume = solved.value().checkpoint;
    }
    const auto elapsed_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const auto first_nashconv = checkpoints.front().at("nashconv").get<double>();
    const auto final_nashconv = checkpoints.back().at("nashconv").get<double>();
    const bool passes = final_nashconv < first_nashconv && maximum_normalization_error <= 1.0e-12;

    const Json result{
        {"schema", "gtosd.mccfr_nashconv_validation.v1"},
        {"status", passes ? "PASS" : "FAIL"},
        {"scope", "exact_enumerated_reduced_four_street_short_deck"},
        {"nashconv_certified", true},
        {"game",
         {{"id", game.value().game_id},
          {"fingerprint", summary.value().fingerprint},
          {"nodes", summary.value().nodes},
          {"information_sets", summary.value().information_sets},
          {"chance_nodes", summary.value().chance_nodes},
          {"decision_nodes", summary.value().decision_nodes},
          {"initial_pot_antes", game.value().initial_pot}}},
        {"solver",
         {{"algorithm", gtosd::solver_algorithm_name(gtosd::SolverAlgorithm::LinearMccfr)},
          {"iterations", arguments.iterations},
          {"certification_interval", arguments.interval},
          {"seed", arguments.seed},
          {"strategy", "average"}}},
        {"checkpoints", std::move(checkpoints)},
        {"maximum_strategy_normalization_error", maximum_normalization_error},
        {"traversed_nodes", traversed_nodes},
        {"elapsed_seconds", elapsed_seconds},
        {"limitations",
         {"This certifies the reduced calibration game only.",
          "It does not certify a V17, V18, V20 or V21 CO40 checkpoint."}},
    };
    write_result(arguments.output, result.dump(2));
    return passes ? 0 : 2;
  } catch (const std::exception &error) {
    std::cerr << "MCCFR_NASHCONV_VALIDATION=FAIL reason=" << error.what() << '\n';
    return 1;
  }
}
