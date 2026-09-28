// Preflop charts of a trained policy in the MonkerSolver text format, to
// compare our solutions with MonkerSolver charts node by node. The preflop
// rows of the policy are the hand classes, so every chart row is read
// directly; a class whose own reach at the node is below 5e-4 is written as an
// all-zero row (outside the acting range), as MonkerSolver does.
#include "monker_chart_format.hpp"

#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/game_model.hpp"
#include "gtosd/preflop_blueprint/policy_file.hpp"
#include "gtosd/preflop_blueprint/traversal.hpp"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace pb = gtosd::preflop_blueprint;
namespace mc = gtosd::monker_charts;

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

int run(const int argc, char **argv) {
  std::filesystem::path config_path;
  std::filesystem::path policy_path;
  std::filesystem::path output_dir;
  for (int index = 1; index < argc; ++index) {
    const std::string_view name(argv[index]);
    if (index + 1 >= argc) {
      throw std::runtime_error("missing value for " + std::string(name));
    }
    const std::string_view value(argv[++index]);
    if (name == "--config") {
      config_path = value;
    } else if (name == "--policy") {
      policy_path = value;
    } else if (name == "--output-dir") {
      output_dir = value;
    } else {
      throw std::runtime_error("unknown argument " + std::string(name));
    }
  }
  if (config_path.empty() || policy_path.empty() || output_dir.empty()) {
    throw std::runtime_error("--config, --policy and --output-dir are required");
  }
  const auto config = pb::parse_game_config_json(read_file(config_path));
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
  auto policy = pb::load_policy(policy_path, game);
  if (!policy) {
    throw std::runtime_error(std::string("policy rejected: ") +
                             pb::policy_file_error_name(policy.error()));
  }
  const auto &loaded = *policy.value();
  const mc::RowStrategy row = [&](const std::uint32_t node, const std::uint8_t hand_class) {
    const auto values = loaded.row(node, hand_class);
    std::vector<double> result(values.begin(), values.end());
    double total = 0.0;
    for (const auto value : result) {
      total += value;
    }
    for (auto &value : result) {
      value = total > 0.0 ? value / total : 1.0 / static_cast<double>(result.size());
    }
    return result;
  };
  const auto written = mc::write_row_charts(game, row, output_dir);
  std::cout << "PREFLOP_BLUEPRINT_MONKER_CHARTS=PASS charts=" << written.size() << '\n';
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_MONKER_CHARTS=FAIL " << error.what() << '\n';
    return 1;
  }
}
