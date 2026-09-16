#include "gtosd/preflop_blueprint/chart_export.hpp"

#include "gtosd/card_abstraction/card_abstraction.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/core/ranges.hpp"
#include "gtosd/preflop_blueprint/action_labels.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/policy_file.hpp"

#include "hashing.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace gtosd::preflop_blueprint {
namespace {

namespace ca = card_abstraction;
using Clock = std::chrono::steady_clock;
constexpr double ante_scale = 1.0 / static_cast<double>(Money::units_per_ante);

std::string number(const double value) {
  std::ostringstream stream;
  stream << std::setprecision(17) << value;
  return stream.str();
}

std::string json_quote(const std::string &value) {
  std::string out = "\"";
  for (const auto character : value) {
    if (character == '"' || character == '\\') {
      out.push_back('\\');
    }
    out.push_back(character);
  }
  out.push_back('"');
  return out;
}

constexpr const char *ev_scope_text =
    "EV in antes of the action for the actor, conditional on the hand class and the public "
    "history: opponent range from the average strategy (importance-weighted by its previous "
    "actions), both players following the average strategy after the action; averaged over the "
    "sampled flops with all their runouts; standard error over the sampled flops.";

// Strategy and action EV objects of one preflop node, keyed by class name.
struct NodeText {
  std::string strategy;
  std::string action_ev;
};

NodeText node_text(const CompiledGame &game, const BucketPolicy &policy,
                   const PreflopActionValues &values, const std::size_t slot,
                   const std::vector<std::string> &labels, const std::uint32_t samples) {
  const auto node = values.nodes[slot];
  NodeText text;
  text.strategy = "{";
  text.action_ev = "{";
  for (std::uint8_t hand_class = 0; hand_class < ca::preflop_hand_classes; ++hand_class) {
    const auto name = json_quote(class_name(hand_class));
    const auto row = policy.row(node, hand_class);
    std::string strategy_row = "{";
    std::string ev_row = "{";
    for (std::size_t action = 0; action < labels.size(); ++action) {
      if (action > 0U) {
        strategy_row += ", ";
        ev_row += ", ";
      }
      strategy_row += json_quote(labels[action]) + ": " + number(row[action]);
      ev_row += json_quote(labels[action]) + ": {\"ev_ante\": " +
                number(values.class_ev[slot][action][hand_class]) +
                ", \"standard_error_ante\": " + number(values.class_se[slot][action][hand_class]) +
                ", \"samples\": " + std::to_string(samples) + "}";
    }
    strategy_row += "}";
    ev_row += "}";
    if (hand_class > 0U) {
      text.strategy += ", ";
      text.action_ev += ", ";
    }
    text.strategy += name + ": " + strategy_row;
    text.action_ev += name + ": " + ev_row;
  }
  text.strategy += "}";
  text.action_ev += "}";
  static_cast<void>(game);
  return text;
}

} // namespace

Result<bool, ChartExportError> write_text_atomically(const std::filesystem::path &path,
                                                     const std::string &text) {
  using Outcome = Result<bool, ChartExportError>;
  const auto temporary = std::filesystem::path(path.string() + ".tmp");
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      return Outcome::failure(ChartExportError::IoFailure);
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    if (!output) {
      return Outcome::failure(ChartExportError::IoFailure);
    }
  }
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return Outcome::failure(ChartExportError::IoFailure);
  }
  return Outcome::success(true);
}

Result<ChartExport, ChartExportError> export_chart(const CompiledGame &game,
                                                   const BucketPolicy &policy,
                                                   const BestResponseResources &resources,
                                                   const ChartExportOptions &options) {
  using Outcome = Result<ChartExport, ChartExportError>;
  const auto started = Clock::now();
  if (options.flops == 0U || game.config().player_count != 2U) {
    return Outcome::failure(ChartExportError::InvalidConfiguration);
  }
  if (resources.ranks == nullptr || resources.catalog == nullptr || resources.flop == nullptr ||
      resources.turn == nullptr || resources.river == nullptr) {
    return Outcome::failure(ChartExportError::MissingResource);
  }
  auto evaluator = BestResponseEvaluator::create(game, policy, resources);
  if (!evaluator) {
    return Outcome::failure(ChartExportError::EvaluationFailure);
  }
  ca::DeterministicRandom random(options.seed);
  std::vector<FlopGroup> groups;
  groups.reserve(options.flops);
  for (std::uint32_t draw = 0; draw < options.flops; ++draw) {
    groups.push_back(full_runouts(resources.catalog->sample_physical_history(random).flop));
  }
  const auto values = evaluate_flops(evaluator.value(), groups, options.threads);
  if (!values) {
    return Outcome::failure(ChartExportError::EvaluationFailure);
  }
  std::vector<const FlopValues *> pointers;
  for (const auto &entry : values.value()) {
    pointers.push_back(&entry);
  }
  const auto estimate = evaluator.value().aggregate(pointers, false);
  if (!estimate) {
    return Outcome::failure(ChartExportError::EvaluationFailure);
  }
  std::array<PreflopActionValues, 2> actions{};
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    auto computed = evaluator.value().preflop_action_values(pointers, hero);
    if (!computed) {
      return Outcome::failure(ChartExportError::EvaluationFailure);
    }
    actions[hero] = std::move(computed.value());
  }

  ChartExport result;
  result.estimate = estimate.value();
  result.root_ev = estimate.value().ev;
  result.policy_fingerprint = policy_fingerprint(policy);
  const auto &config = game.config();
  const bool certified = options.certificate != nullptr && options.certificate->exact &&
                         options.certificate->policy_fingerprint == result.policy_fingerprint &&
                         options.certificate->tree_fingerprint == game.fingerprint();
  result.badge = certified ? "CERTIFIED_EXACT" : "ESTIMATED";
  const double initial_pot =
      static_cast<double>(config.ante.units() * config.player_count + config.button_blind.units()) *
      ante_scale;

  std::string json = "{\n";
  json += "  \"schema\": \"gtosd.preflop_blueprint_chart.v1\",\n";
  json += "  \"config_id\": " + json_quote(config.id) + ",\n";
  json += "  \"tree_fingerprint\": " + json_quote(game.fingerprint()) + ",\n";
  json += "  \"fingerprints\": {\"rules\": " + json_quote(game_config_fingerprint(config)) +
          ", \"tree\": " + json_quote(game.fingerprint()) +
          ", \"catalog\": " + json_quote(resources.catalog->fingerprint()) +
          ", \"flop_table\": " + json_quote(resources.flop->fingerprint()) +
          ", \"turn_table\": " + json_quote(resources.turn->fingerprint()) +
          ", \"river_table\": " + json_quote(resources.river->fingerprint()) +
          ", \"policy\": " + json_quote(result.policy_fingerprint) + "},\n";
  json += "  \"game\": {\"positions\": [";
  for (std::size_t index = 0; index < config.positions.size(); ++index) {
    json += (index > 0U ? ", " : "") + json_quote(config.positions[index]);
  }
  json += "], \"effective_stack_antes\": " +
          number(static_cast<double>(config.effective_stack.units()) * ante_scale) +
          ", \"ante_antes\": " + number(static_cast<double>(config.ante.units()) * ante_scale) +
          ", \"button_blind_antes\": " +
          number(static_cast<double>(config.button_blind.units()) * ante_scale) +
          ", \"initial_pot_antes\": " + number(initial_pot) + "},\n";
  json += "  \"capacities\": [" + std::to_string(policy.layout().flop_capacity) + ", " +
          std::to_string(policy.layout().turn_capacity) + ", " +
          std::to_string(policy.layout().river_capacity) + "],\n";
  json += "  \"algorithm\": " + json_quote(options.algorithm) + ",\n";
  json += "  \"abstraction\": " + json_quote(options.abstraction) + ",\n";
  json += "  \"iterations\": " + std::to_string(options.iterations) + ",\n";
  json += "  \"policy_source\": " + json_quote(options.policy_source) + ",\n";
  json += "  \"has_current_strategy\": false,\n";
  const auto &report = estimate.value();
  json += "  \"status\": {\"badge\": " + json_quote(result.badge) +
          ", \"exploitability\": {\"exact\": " + (certified ? "true" : "false");
  if (certified) {
    const auto &certificate = *options.certificate;
    json += ", \"max_gain_antes\": " + number(certificate.report.max_gain) +
            ", \"max_gain_lower_antes\": " + number(certificate.report.max_gain_lower) +
            ", \"max_gain_half_width_antes\": 0" +
            ", \"nashconv_antes\": " + number(certificate.report.nashconv) +
            ", \"gain_antes\": [" + number(certificate.report.gain[0]) + ", " +
            number(certificate.report.gain[1]) + "]" +
            ", \"flops\": " + std::to_string(certificate.flops) +
            ", \"physical_flops\": " + std::to_string(certificate.physical_flops) +
            ", \"boards\": " + std::to_string(certificate.boards) +
            ", \"certificate_policy_fingerprint\": " + json_quote(certificate.policy_fingerprint);
  } else {
    json += ", \"max_gain_antes\": " + number(report.max_gain) +
            ", \"max_gain_lower_antes\": " + number(report.max_gain_lower) +
            ", \"max_gain_half_width_antes\": " + number(report.max_gain_half_width) +
            ", \"nashconv_antes\": " + number(report.nashconv) +
            ", \"gain_antes\": [" + number(report.gain[0]) + ", " + number(report.gain[1]) + "]" +
            ", \"flops\": " + std::to_string(report.flops) +
            ", \"physical_flops\": " + std::to_string(report.flops) +
            ", \"boards\": " + std::to_string(report.boards);
    if (options.certificate != nullptr) {
      json += ", \"certificate_ignored\": \"policy or tree fingerprint mismatch\"";
    }
  }
  json += ", \"normalized_dev\": " +
          number(initial_pot > 0.0 ? (certified ? options.certificate->report.max_gain
                                                : report.max_gain) /
                                         initial_pot
                                   : 0.0) +
          "}},\n";
  json += "  \"evaluation\": {\"flops\": " + std::to_string(report.flops) +
          ", \"boards\": " + std::to_string(report.boards) +
          ", \"seed\": " + std::to_string(options.seed) +
          ", \"ev_scope\": " + json_quote(ev_scope_text) + "},\n";
  json += "  \"root_ev_ante\": " + number(report.ev[0]) + ",\n";
  json += "  \"root_ev_standard_error_ante\": " + number(report.ev_standard_error[0]) + ",\n";
  json += "  \"root_action_ev_scope\": " + json_quote(ev_scope_text) + ",\n";

  // Nodes in preorder; the root text is repeated at the top level for the
  // viewer's fallback fields.
  std::string nodes_json;
  std::string root_strategy;
  std::string root_action_ev;
  const auto preflop_nodes = preflop_decision_nodes(game);
  for (const auto node : preflop_nodes) {
    const auto &entry = game.nodes()[node];
    const auto hero = entry.actor;
    const auto &per_hero = actions[hero];
    std::size_t slot = per_hero.nodes.size();
    for (std::size_t index = 0; index < per_hero.nodes.size(); ++index) {
      if (per_hero.nodes[index] == node) {
        slot = index;
        break;
      }
    }
    if (slot == per_hero.nodes.size()) {
      return Outcome::failure(ChartExportError::EvaluationFailure);
    }
    const auto labels = edge_labels(game, node);
    const auto text = node_text(game, policy, per_hero, slot, labels, per_hero.groups);
    const auto id = node_path_id(game, node);
    std::string history = "[";
    const auto steps = history_steps(game, node);
    for (std::size_t index = 0; index < steps.size(); ++index) {
      history += (index > 0U ? ", " : "") + std::string("{\"player\": ") + json_quote(steps[index].player) +
                 ", \"action\": " + json_quote(steps[index].action) + "}";
    }
    history += "]";
    if (!nodes_json.empty()) {
      nodes_json += ",\n";
    }
    nodes_json += "    " + json_quote(id) + ": {\"id\": " + json_quote(id) +
                  ", \"player\": " + json_quote(position_name(game, hero)) +
                  ", \"node\": " + std::to_string(node) + ", \"history\": " + history +
                  ", \"actions\": [";
    for (std::size_t index = 0; index < labels.size(); ++index) {
      nodes_json += (index > 0U ? ", " : "") + json_quote(labels[index]);
    }
    nodes_json += "], \"strategy\": " + text.strategy + ", \"action_ev\": " + text.action_ev +
                  ", \"ev_scope\": " + json_quote(ev_scope_text) + "}";
    if (node == game.root()) {
      root_strategy = text.strategy;
      root_action_ev = text.action_ev;
    }
    ++result.nodes;
  }
  json += "  \"strategy\": " + root_strategy + ",\n";
  json += "  \"root_action_ev\": " + root_action_ev + ",\n";
  json += "  \"preflop_nodes\": {\n" + nodes_json + "\n  },\n";
  result.checksum = "fnv1a64:" + detail::hex64_text(detail::fnv1a_text(json));
  json += "  \"checksum\": " + json_quote(result.checksum) + "\n}\n";
  result.json = std::move(json);
  result.seconds = std::chrono::duration<double>(Clock::now() - started).count();
  return Outcome::success(std::move(result));
}

const char *chart_export_error_name(const ChartExportError error) noexcept {
  switch (error) {
  case ChartExportError::InvalidConfiguration:
    return "invalid_configuration";
  case ChartExportError::MissingResource:
    return "missing_resource";
  case ChartExportError::EvaluationFailure:
    return "evaluation_failure";
  case ChartExportError::IoFailure:
    return "io_failure";
  }
  return "unknown";
}

} // namespace gtosd::preflop_blueprint
