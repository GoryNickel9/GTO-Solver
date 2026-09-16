#pragma once

#include "gtosd/preflop_blueprint/best_response.hpp"
#include "gtosd/preflop_blueprint/certifier.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/traversal.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

// Chart export of a bucket policy (roadmap P8.3), schema
// gtosd.preflop_blueprint_chart.v1: for every preflop decision node the 81
// hand classes with the frequencies of the average strategy, the EV of every
// action in antes with its standard error (estimated on sampled flops with
// all their runouts) and the number of samples; the four fingerprints
// (rules, tree, abstraction, policy) and a checksum; a status block with the
// badge ESTIMATED (sampled exploitability of the same flops) or
// CERTIFIED_EXACT (a P7 certificate of the same policy and tree is attached).
// The node layout (preflop_nodes with history, strategy and action_ev keyed
// by class name) is the one read by the chart viewer generator.
//
// EV scope: the EV of an action for a class is the mean over the combos of
// the class, weighted by the probability that the opponent reaches the node
// with a hand disjoint from the combo, of the counterfactual value of the
// action divided by that probability: the value of the action given the
// class and the public history, the opponent playing the average strategy
// before and after, both players following the average strategy after the
// action. The root EV is the game value of the actor of the root.
namespace gtosd::preflop_blueprint {

enum class ChartExportError : std::uint8_t {
  InvalidConfiguration,
  MissingResource,
  EvaluationFailure,
  IoFailure
};

struct ChartExportOptions {
  unsigned threads{1U};
  // Sampled flops (all runouts each) for the action EVs and the estimate.
  std::uint32_t flops{20U};
  std::uint64_t seed{0x4348'4152'5445'5850ULL};
  std::string algorithm{"vector_cfr_public_chance_sampling"};
  std::string abstraction;
  std::uint64_t iterations{0U};
  std::string policy_source;
  // Optional exact certificate of the same policy (badge CERTIFIED_EXACT).
  const Certificate *certificate{nullptr};
};

struct ChartExport {
  std::string json;
  std::string badge;
  std::string policy_fingerprint;
  std::string checksum;
  std::uint32_t nodes{0U};
  BestResponseReport estimate;
  std::array<double, 2> root_ev{};
  double seconds{0.0};
};

[[nodiscard]] Result<ChartExport, ChartExportError>
export_chart(const CompiledGame &game, const BucketPolicy &policy,
             const BestResponseResources &resources, const ChartExportOptions &options);

// Atomic write of the JSON text (temporary file then rename).
[[nodiscard]] Result<bool, ChartExportError> write_text_atomically(const std::filesystem::path &path,
                                                                   const std::string &text);

[[nodiscard]] const char *chart_export_error_name(ChartExportError error) noexcept;

} // namespace gtosd::preflop_blueprint
