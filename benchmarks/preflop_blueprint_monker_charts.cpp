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

#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace pb = gtosd::preflop_blueprint;
namespace mc = gtosd::monker_charts;

constexpr double out_of_range_reach = 5e-4;

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::vector<double> normalized(std::span<const double> row) {
  std::vector<double> result(row.begin(), row.end());
  double total = 0.0;
  for (const auto value : result) {
    total += value;
  }
  for (auto &value : result) {
    value = total > 0.0 ? value / total : 1.0 / static_cast<double>(result.size());
  }
  return result;
}

// Own reach of each seat per preflop node and hand class under the policy.
void forward(const pb::CompiledGame &game, const pb::BucketPolicy &policy,
             const std::uint32_t node_id, const std::array<std::vector<double>, 2> &reach,
             std::map<std::uint32_t, std::array<std::vector<double>, 2>> &output) {
  const auto &node = game.nodes()[node_id];
  if (node.kind != pb::NodeKind::Decision || node.street != gtosd::Street::Preflop) {
    return;
  }
  output[node_id] = reach;
  const auto edges = game.edges_of(node_id);
  for (std::size_t action = 0; action < edges.size(); ++action) {
    auto next = reach;
    for (std::size_t hand_class = 0; hand_class < next[node.actor].size(); ++hand_class) {
      next[node.actor][hand_class] *=
          normalized(policy.row(node_id, static_cast<std::uint32_t>(hand_class)))[action];
    }
    forward(game, policy, edges[action].child, next, output);
  }
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
  if (config.value().player_count != 2U) {
    throw std::runtime_error("the chart export is heads-up only");
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
  const auto classes = mc::hand_classes();
  std::map<std::string, std::uint8_t> class_of_label;
  for (const auto &[hand_class, label] : classes.label_by_class) {
    class_of_label[label] = hand_class;
  }
  std::vector<std::string> labels;
  for (const auto &entry : classes.combos_by_label) {
    labels.push_back(entry.first);
  }
  const auto class_count = classes.label_by_class.size();
  std::map<std::uint32_t, std::array<std::vector<double>, 2>> reach;
  forward(game, *policy.value(), game.root(),
          {std::vector<double>(class_count, 1.0), std::vector<double>(class_count, 1.0)}, reach);
  const mc::ClassStrategy strategy =
      [&](const std::uint32_t node, const std::string &label) -> std::optional<std::vector<double>> {
    const auto hand_class = class_of_label.at(label);
    const auto actor = game.nodes()[node].actor;
    if (reach.at(node)[actor][hand_class] < out_of_range_reach) {
      return std::nullopt;
    }
    return normalized(policy.value()->row(node, hand_class));
  };
  const auto written = mc::write_charts(game, labels, strategy, output_dir);
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
