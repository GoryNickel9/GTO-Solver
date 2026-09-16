// Comparator of preflop blueprint chart exports (roadmap P8.4):
//   --candidate chart.json [--baseline other.json] [--reference monker.json]
//   [--max-gain 0.1] --output report.json
// Verdict on the declared physical exploitability (decision D2); baseline
// distances on the common preflop nodes; Monker distances descriptive only.
#include "gtosd/preflop_blueprint/comparator.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace pb = gtosd::preflop_blueprint;

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path candidate_path;
    std::filesystem::path baseline_path;
    std::filesystem::path reference_path;
    std::filesystem::path output_path;
    pb::ComparisonThresholds thresholds;
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string_view name = argv[index];
      const std::string_view value = argv[index + 1];
      if (name == "--candidate") {
        candidate_path = std::filesystem::path(value);
      } else if (name == "--baseline") {
        baseline_path = std::filesystem::path(value);
      } else if (name == "--reference") {
        reference_path = std::filesystem::path(value);
      } else if (name == "--output") {
        output_path = std::filesystem::path(value);
      } else if (name == "--max-gain") {
        thresholds.max_gain_antes = std::stod(std::string(value));
      } else {
        throw std::runtime_error("unknown argument " + std::string{name});
      }
    }
    if (candidate_path.empty()) {
      throw std::runtime_error("--candidate is required");
    }
    const auto candidate = read_file(candidate_path);
    std::optional<std::string> baseline;
    std::optional<std::string> reference;
    if (!baseline_path.empty()) {
      baseline = read_file(baseline_path);
    }
    if (!reference_path.empty()) {
      reference = read_file(reference_path);
    }
    const auto report = pb::compare_charts(candidate, baseline ? &*baseline : nullptr,
                                           reference ? &*reference : nullptr, thresholds);
    if (!report) {
      throw std::runtime_error(std::string("comparison failed: ") +
                               pb::comparison_error_name(report.error()));
    }
    if (!output_path.empty()) {
      std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
      if (!output) {
        throw std::runtime_error("cannot write " + output_path.string());
      }
      output << report.value().json;
    }
    std::cout << report.value().json;
    std::cout << "PREFLOP_BLUEPRINT_COMPARE=" << report.value().status << '\n';
    return report.value().status == "REJECTED" ? 2 : 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_COMPARE=FAIL " << error.what() << '\n';
    return 1;
  }
}
