// Exact preflop-only solver for the first tree-building step of MonkerSolver:
// the preflop tree with an empty postflop. Every postflop entry is settled as
// a showdown over the full five-card runout with the exact preflop all-in
// table (the checkdown model), so the solved game has no card abstraction.
// Heads-up only. Vector DCFR over the 630 combos with full traversals and
// alternating updates, exact best response of the average profile, and charts
// in the MonkerSolver text format: one file per decision node, one row per
// hand class, the frequency of every action, named and ordered by chart_nodes
// (monker_chart_format.hpp) like every other chart writer, so a multiway tree
// gets MonkerSolver's fold rule. A configuration with rake
// settles the checkdown leaves and the preflop all-ins with the rake (the
// flop is dealt), and the preflop folds without it under no flop, no drop;
// the summary then reports both EVs and the expected rake, -(EV0 + EV1),
// checked against the rake of every terminal weighted by its reach.
#include "monker_chart_format.hpp"

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/game_model.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace ca = gtosd::card_abstraction;
namespace mc = gtosd::monker_charts;
namespace pb = gtosd::preflop_blueprint;

constexpr std::size_t hands = ca::combo_count;
constexpr double units_per_ante = static_cast<double>(gtosd::Money::units_per_ante);
// Mean own reach below which a hand class counts as outside the acting range
// (it prints as 0.000 in the chart format).
constexpr double out_of_range_reach = 5e-4;
// Opponent combos disjoint from a given combo: C(34, 2).
constexpr double opponents_per_hand = 561.0;

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::uint64_t parse_unsigned(const std::string_view text) {
  std::size_t used = 0;
  const auto value = std::stoull(std::string(text), &used);
  if (used != text.size()) {
    throw std::runtime_error("invalid unsigned value " + std::string(text));
  }
  return value;
}

double parse_double(const std::string_view text) {
  std::size_t used = 0;
  const auto value = std::stod(std::string(text), &used);
  if (used != text.size()) {
    throw std::runtime_error("invalid number " + std::string(text));
  }
  return value;
}

enum class Mode : std::uint8_t { Update, Average, BestResponse };

class CheckdownSolver {
public:
  CheckdownSolver(const pb::CompiledGame &game, const ca::AllInTable &table, const double alpha,
                  const double beta, const double gamma)
      : game_(game), alpha_(alpha), beta_(beta), gamma_(gamma) {
    const auto &combos = ca::combo_table();
    disjoint_.assign(hands * hands, 0.0);
    for (std::size_t hero = 0; hero < hands; ++hero) {
      for (std::size_t other = 0; other < hands; ++other) {
        if (hero != other && (combos.masks[hero] & combos.masks[other]) == 0U) {
          disjoint_[hero * hands + other] = 1.0;
        }
      }
    }
    const auto &nodes = game_.nodes();
    offsets_.assign(nodes.size(), 0U);
    std::size_t cells = 0;
    for (const auto &node : nodes) {
      if (node.kind == pb::NodeKind::Chance) {
        throw std::runtime_error("the checkdown tree still has a chance node");
      }
      if (node.street != gtosd::Street::Preflop) {
        throw std::runtime_error("the checkdown tree still has a postflop node");
      }
      if (node.kind == pb::NodeKind::Decision) {
        offsets_[node.id] = cells;
        cells += hands * node.action_count;
      }
      if (node.kind == pb::NodeKind::TerminalShowdown) {
        // Payoff matrix for each seat: sum over the win, tie and loss masses of
        // the pair times the net result of the seat in that outcome.
        for (std::uint8_t seat = 0; seat < 2U; ++seat) {
          const auto seat_bit = static_cast<std::uint8_t>(1U << seat);
          const auto other_bit = static_cast<std::uint8_t>(node.active_mask & ~seat_bit);
          const double win = static_cast<double>(game_.showdown_payoffs(node.id, seat_bit)[seat]) /
                             units_per_ante;
          const double tie =
              static_cast<double>(game_.showdown_payoffs(node.id, node.active_mask)[seat]) /
              units_per_ante;
          const double lose =
              static_cast<double>(game_.showdown_payoffs(node.id, other_bit)[seat]) /
              units_per_ante;
          std::vector<double> matrix(hands * hands, 0.0);
          for (std::size_t hero = 0; hero < hands; ++hero) {
            for (std::size_t other = 0; other < hands; ++other) {
              if (disjoint_[hero * hands + other] == 0.0) {
                continue;
              }
              const auto outcome = table.outcome(static_cast<std::uint16_t>(hero),
                                                 static_cast<std::uint16_t>(other));
              const auto total = static_cast<double>(outcome.total());
              if (total == 0.0) {
                throw std::runtime_error("all-in table has no runout for a disjoint pair");
              }
              matrix[hero * hands + other] = (static_cast<double>(outcome.wins) * win +
                                              static_cast<double>(outcome.ties) * tie +
                                              static_cast<double>(outcome.losses) * lose) /
                                             total;
            }
          }
          showdown_[{node.id, seat}] = std::move(matrix);
        }
      }
    }
    regret_.assign(cells, 0.0);
    strategy_sum_.assign(cells, 0.0);
  }

  // One DCFR iteration: seat 0 then seat 1 (alternating), then the discounts.
  void iterate(const std::uint64_t iteration) {
    const std::vector<double> ones(hands, 1.0);
    for (std::uint8_t hero = 0; hero < 2U; ++hero) {
      (void)traverse(game_.root(), hero, ones, ones, Mode::Update);
    }
    const double t = static_cast<double>(iteration);
    const double positive = std::pow(t, alpha_) / (std::pow(t, alpha_) + 1.0);
    const double negative = std::pow(t, beta_) / (std::pow(t, beta_) + 1.0);
    const double average = std::pow(t / (t + 1.0), gamma_);
    for (std::size_t cell = 0; cell < regret_.size(); ++cell) {
      regret_[cell] *= regret_[cell] > 0.0 ? positive : negative;
      strategy_sum_[cell] *= average;
    }
  }

  // Expected value of a seat, in antes, when the seat plays `mode` (average
  // strategy or best response) against the opponent's average strategy.
  double value(const std::uint8_t hero, const Mode mode) {
    const std::vector<double> ones(hands, 1.0);
    const auto values = traverse(game_.root(), hero, ones, ones, mode);
    double sum = 0.0;
    for (const auto entry : values) {
      sum += entry;
    }
    return sum / (static_cast<double>(hands) * opponents_per_hand);
  }

  // Average strategy of a decision node for one combo.
  std::vector<double> average_strategy(const std::uint32_t node, const std::size_t hand) const {
    return strategy(node, hand, true);
  }

  // Reach of each seat's own actions under the average profile, per node.
  std::vector<std::array<std::vector<double>, 2>> own_reach() const {
    std::vector<std::array<std::vector<double>, 2>> reach(game_.nodes().size());
    std::array<std::vector<double>, 2> start{std::vector<double>(hands, 1.0),
                                             std::vector<double>(hands, 1.0)};
    forward(game_.root(), start, reach);
    return reach;
  }

private:
  std::vector<double> strategy(const std::uint32_t node, const std::size_t hand,
                               const bool use_average) const {
    const auto actions = static_cast<std::size_t>(game_.nodes()[node].action_count);
    const auto base = offsets_[node] + hand * actions;
    const auto &source = use_average ? strategy_sum_ : regret_;
    std::vector<double> result(actions, 0.0);
    double total = 0.0;
    for (std::size_t action = 0; action < actions; ++action) {
      const double entry = use_average ? source[base + action] : std::max(source[base + action], 0.0);
      result[action] = entry;
      total += entry;
    }
    for (std::size_t action = 0; action < actions; ++action) {
      result[action] = total > 0.0 ? result[action] / total : 1.0 / static_cast<double>(actions);
    }
    return result;
  }

  std::vector<double> traverse(const std::uint32_t node_id, const std::uint8_t hero,
                               const std::vector<double> &hero_reach,
                               const std::vector<double> &opponent_reach, const Mode mode) {
    const auto &node = game_.nodes()[node_id];
    std::vector<double> values(hands, 0.0);
    if (node.kind == pb::NodeKind::TerminalFold) {
      const double payoff = static_cast<double>(game_.fold_payoffs(node_id)[hero]) / units_per_ante;
      for (std::size_t hand = 0; hand < hands; ++hand) {
        const double *row = disjoint_.data() + hand * hands;
        double mass = 0.0;
        for (std::size_t other = 0; other < hands; ++other) {
          mass += row[other] * opponent_reach[other];
        }
        values[hand] = payoff * mass;
      }
      return values;
    }
    if (node.kind == pb::NodeKind::TerminalShowdown) {
      const auto &matrix = showdown_.at({node_id, hero});
      for (std::size_t hand = 0; hand < hands; ++hand) {
        const double *row = matrix.data() + hand * hands;
        double sum = 0.0;
        for (std::size_t other = 0; other < hands; ++other) {
          sum += row[other] * opponent_reach[other];
        }
        values[hand] = sum;
      }
      return values;
    }
    const auto edges = game_.edges_of(node_id);
    const auto actions = edges.size();
    const bool use_average = mode != Mode::Update;
    std::vector<std::vector<double>> strategies(hands);
    for (std::size_t hand = 0; hand < hands; ++hand) {
      strategies[hand] = strategy(node_id, hand, use_average);
    }
    if (node.actor != hero) {
      std::vector<double> reach(hands, 0.0);
      for (std::size_t action = 0; action < actions; ++action) {
        for (std::size_t hand = 0; hand < hands; ++hand) {
          reach[hand] = opponent_reach[hand] * strategies[hand][action];
        }
        const auto child = traverse(edges[action].child, hero, hero_reach, reach, mode);
        for (std::size_t hand = 0; hand < hands; ++hand) {
          values[hand] += child[hand];
        }
      }
      return values;
    }
    std::vector<std::vector<double>> children(actions);
    std::vector<double> reach(hands, 0.0);
    for (std::size_t action = 0; action < actions; ++action) {
      for (std::size_t hand = 0; hand < hands; ++hand) {
        reach[hand] = hero_reach[hand] * strategies[hand][action];
      }
      children[action] = traverse(edges[action].child, hero, reach, opponent_reach, mode);
    }
    for (std::size_t hand = 0; hand < hands; ++hand) {
      if (mode == Mode::BestResponse) {
        double best = children[0][hand];
        for (std::size_t action = 1; action < actions; ++action) {
          best = std::max(best, children[action][hand]);
        }
        values[hand] = best;
        continue;
      }
      double node_value = 0.0;
      for (std::size_t action = 0; action < actions; ++action) {
        node_value += strategies[hand][action] * children[action][hand];
      }
      values[hand] = node_value;
      if (mode == Mode::Update) {
        const auto base = offsets_[node_id] + hand * actions;
        for (std::size_t action = 0; action < actions; ++action) {
          regret_[base + action] += children[action][hand] - node_value;
          strategy_sum_[base + action] += hero_reach[hand] * strategies[hand][action];
        }
      }
    }
    return values;
  }

  void forward(const std::uint32_t node_id, const std::array<std::vector<double>, 2> &reach,
               std::vector<std::array<std::vector<double>, 2>> &output) const {
    output[node_id] = reach;
    const auto &node = game_.nodes()[node_id];
    if (node.kind != pb::NodeKind::Decision) {
      return;
    }
    const auto edges = game_.edges_of(node_id);
    for (std::size_t action = 0; action < edges.size(); ++action) {
      auto next = reach;
      for (std::size_t hand = 0; hand < hands; ++hand) {
        next[node.actor][hand] *= strategy(node_id, hand, true)[action];
      }
      forward(edges[action].child, next, output);
    }
  }

  const pb::CompiledGame &game_;
  double alpha_;
  double beta_;
  double gamma_;
  std::vector<double> disjoint_;
  std::map<std::pair<std::uint32_t, std::uint8_t>, std::vector<double>> showdown_;
  std::vector<std::size_t> offsets_;
  std::vector<double> regret_;
  std::vector<double> strategy_sum_;
};

// One chart per preflop decision node through mc::write_charts, which names
// the files and orders the columns by chart_nodes. The row of a hand class is
// the average strategy of its combos weighted by their own reach.
std::vector<std::string> write_charts(const pb::CompiledGame &game, const CheckdownSolver &solver,
                                      const std::filesystem::path &directory) {
  const auto classes = mc::hand_classes();
  std::vector<std::string> labels;
  for (const auto &entry : classes.combos_by_label) {
    labels.push_back(entry.first);
  }
  const auto reach = solver.own_reach();
  const mc::ClassStrategy strategy =
      [&](const std::uint32_t node_id,
          const std::string &label) -> std::optional<std::vector<double>> {
    const auto &members = classes.combos_by_label.at(label);
    const auto &own = reach[node_id][game.nodes()[node_id].actor];
    double weight = 0.0;
    for (const auto hand : members) {
      weight += own[hand];
    }
    // Like MonkerSolver, a class outside the acting range at this node is
    // written as an all-zero row. DCFR averaging from a uniform start never
    // gives an exactly zero reach, hence the threshold on the mean reach.
    if (weight / static_cast<double>(members.size()) < out_of_range_reach) {
      return std::nullopt;
    }
    std::vector<double> frequency(game.edges_of(node_id).size(), 0.0);
    for (const auto hand : members) {
      const auto average = solver.average_strategy(node_id, hand);
      const double share = own[hand] / weight;
      for (std::size_t action = 0; action < average.size(); ++action) {
        frequency[action] += share * average[action];
      }
    }
    return frequency;
  };
  return mc::write_charts(game, labels, strategy, directory);
}

int run(const int argc, char **argv) {
  std::filesystem::path config_path;
  std::filesystem::path resources_dir;
  std::filesystem::path output_dir;
  std::uint64_t iterations = 20'000U;
  std::uint64_t report_every = 1'000U;
  double alpha = 1.5;
  double beta = 0.0;
  double gamma = 2.0;
  for (int index = 1; index < argc; ++index) {
    const std::string_view name(argv[index]);
    if (index + 1 >= argc) {
      throw std::runtime_error("missing value for " + std::string(name));
    }
    const std::string_view value(argv[++index]);
    if (name == "--config") {
      config_path = value;
    } else if (name == "--resources-dir") {
      resources_dir = value;
    } else if (name == "--output-dir") {
      output_dir = value;
    } else if (name == "--iterations") {
      iterations = parse_unsigned(value);
    } else if (name == "--report-every") {
      report_every = std::max<std::uint64_t>(1U, parse_unsigned(value));
    } else if (name == "--alpha") {
      alpha = parse_double(value);
    } else if (name == "--beta") {
      beta = parse_double(value);
    } else if (name == "--gamma") {
      gamma = parse_double(value);
    } else {
      throw std::runtime_error("unknown argument " + std::string(name));
    }
  }
  if (config_path.empty() || resources_dir.empty() || output_dir.empty()) {
    throw std::runtime_error("--config, --resources-dir and --output-dir are required");
  }
  const auto started = std::chrono::steady_clock::now();
  const auto config = pb::parse_game_config_json(read_file(config_path));
  if (!config) {
    throw std::runtime_error(std::string("configuration rejected: ") +
                             pb::config_error_name(config.error()));
  }
  if (config.value().player_count != 2U) {
    throw std::runtime_error("the checkdown solver is heads-up only");
  }
  pb::CompileOptions options;
  options.checkdown_at_flop = true;
  const auto compiled = pb::CompiledGame::compile(config.value(), options);
  if (!compiled) {
    throw std::runtime_error(std::string("compile failed: ") +
                             pb::game_model_error_name(compiled.error()));
  }
  const auto &game = compiled.value();
  if (!game.postflop_entries().empty()) {
    throw std::runtime_error("the checkdown tree still has postflop entries");
  }
  // The payoffs of every terminal sum to minus its rake (zero without rake).
  // Every node of this tree is preflop: a fold ends the hand before the flop,
  // while an all-in runout and a checkdown leaf see the flop.
  const auto &rake = config.value().rake;
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::TerminalFold && node.kind != pb::NodeKind::TerminalShowdown) {
      continue;
    }
    const auto expected = gtosd::calculate_rake(rake, game.states()[node.id].pot,
                                                node.kind == pb::NodeKind::TerminalShowdown);
    if (!expected) {
      throw std::runtime_error("rake computation failed");
    }
    const auto sum = -expected.value().units();
    if (node.kind == pb::NodeKind::TerminalFold) {
      const auto payoffs = game.fold_payoffs(node.id);
      if (payoffs[0] + payoffs[1] != sum) {
        throw std::runtime_error("fold payoffs do not sum to minus the rake");
      }
    } else {
      for (const std::uint8_t mask : {std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3}}) {
        const auto payoffs = game.showdown_payoffs(node.id, mask);
        if (payoffs[0] + payoffs[1] != sum) {
          throw std::runtime_error("showdown payoffs do not sum to minus the rake");
        }
      }
    }
  }
  auto table = ca::AllInTable::load(resources_dir / "preflop_all_in_v1.bin");
  if (!table) {
    throw std::runtime_error("cannot load preflop_all_in_v1.bin");
  }
  CheckdownSolver solver(game, table.value(), alpha, beta, gamma);
  const double initial_pot =
      static_cast<double>(config.value().ante.units() * config.value().player_count +
                          config.value().button_blind.units()) /
      units_per_ante;
  std::ostringstream trajectory;
  bool first_report = true;
  double last_gain_0 = 0.0;
  double last_gain_1 = 0.0;
  double last_ev_0 = 0.0;
  double last_ev_1 = 0.0;
  for (std::uint64_t iteration = 1; iteration <= iterations; ++iteration) {
    solver.iterate(iteration);
    if (iteration % report_every == 0U || iteration == iterations) {
      const double ev_0 = solver.value(0U, Mode::Average);
      const double ev_1 = solver.value(1U, Mode::Average);
      const double gain_0 = solver.value(0U, Mode::BestResponse) - ev_0;
      const double gain_1 = solver.value(1U, Mode::BestResponse) - ev_1;
      const double seconds =
          std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
      std::cerr << "iteration " << iteration << " gain " << gain_0 << ' ' << gain_1 << " a ("
                << 100.0 * std::max(gain_0, gain_1) / initial_pot << " % of the pot) ev "
                << ev_0 << ' ' << ev_1 << " seconds " << seconds << '\n';
      trajectory << (first_report ? "    " : ",\n    ") << "{\"iteration\": " << iteration
                 << ", \"gain\": [" << std::setprecision(10) << gain_0 << ", " << gain_1
                 << "], \"seconds\": " << seconds << "}";
      first_report = false;
      last_gain_0 = gain_0;
      last_gain_1 = gain_1;
      last_ev_0 = ev_0;
      last_ev_1 = ev_1;
    }
  }
  // With rake, the expected rake computed directly: the rake of every terminal
  // times the probability that the average profile reaches it (both seats'
  // reach over disjoint hands), independent of the payoffs and of the value
  // traversal. The traversal is exact, so it must equal -(EV0 + EV1).
  double direct_rake = 0.0;
  if (rake.enabled) {
    const auto reach = solver.own_reach();
    const auto &masks = ca::combo_table().masks;
    for (const auto &node : game.nodes()) {
      if (node.kind != pb::NodeKind::TerminalFold && node.kind != pb::NodeKind::TerminalShowdown) {
        continue;
      }
      const auto amount = gtosd::calculate_rake(rake, game.states()[node.id].pot,
                                                node.kind == pb::NodeKind::TerminalShowdown);
      if (!amount || amount.value().units() == 0) {
        continue;
      }
      double mass = 0.0;
      for (std::size_t hand = 0; hand < hands; ++hand) {
        double opponents = 0.0;
        for (std::size_t other = 0; other < hands; ++other) {
          if (hand != other && (masks[hand] & masks[other]) == 0U) {
            opponents += reach[node.id][1][other];
          }
        }
        mass += reach[node.id][0][hand] * opponents;
      }
      direct_rake += static_cast<double>(amount.value().units()) / units_per_ante * mass /
                     (static_cast<double>(hands) * opponents_per_hand);
    }
    if (std::abs(direct_rake + last_ev_0 + last_ev_1) > 1e-9) {
      throw std::runtime_error("the expected rake by reach differs from -(EV0 + EV1)");
    }
  }
  const auto written = write_charts(game, solver, output_dir / "charts");
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  std::ofstream summary(output_dir / "summary.json", std::ios::binary);
  summary << std::setprecision(12) << "{\n"
          << "  \"schema\": \"gtosd.preflop_blueprint_checkdown.v1\",\n"
          << "  \"config_id\": \"" << config.value().id << "\",\n"
          << "  \"tree_fingerprint\": \"" << game.fingerprint() << "\",\n"
          << "  \"all_in_table_fingerprint\": \"" << table.value().fingerprint() << "\",\n"
          << "  \"nodes\": " << game.nodes().size() << ",\n"
          << "  \"iterations\": " << iterations << ",\n"
          << "  \"dcfr\": [" << alpha << ", " << beta << ", " << gamma << "],\n"
          << "  \"initial_pot_antes\": " << initial_pot << ",\n";
  // Without rake the game is zero-sum and the second EV is written as the
  // negated first, as before. With rake both are computed, and since the
  // traversal is exact, -(EV0 + EV1) is the expected rake per hand.
  if (!rake.enabled) {
    summary << "  \"ev_antes\": [" << last_ev_0 << ", " << -last_ev_0 << "],\n";
  } else {
    summary << "  \"ev_antes\": [" << last_ev_0 << ", " << last_ev_1 << "],\n"
            << "  \"rake\": {\"basis_points\": " << rake.percentage.basis_points()
            << ", \"cap_antes\": " << static_cast<double>(rake.cap.units()) / units_per_ante
            << ", \"no_flop_no_drop\": " << (rake.no_flop_no_drop ? "true" : "false")
            << ", \"minimum_pot_antes\": "
            << static_cast<double>(rake.minimum_pot.units()) / units_per_ante << "},\n"
            << "  \"expected_rake_antes\": " << -(last_ev_0 + last_ev_1) << ",\n"
            << "  \"expected_rake_by_reach_antes\": " << direct_rake << ",\n";
  }
  summary << "  \"gain_antes\": [" << last_gain_0 << ", " << last_gain_1 << "],\n"
          << "  \"max_gain_pot_percent\": "
          << 100.0 * std::max(last_gain_0, last_gain_1) / initial_pot << ",\n"
          << "  \"seconds\": " << seconds << ",\n"
          << "  \"charts\": " << written.size() << ",\n"
          << "  \"trajectory\": [\n"
          << trajectory.str() << "\n  ]\n}\n";
  if (rake.enabled) {
    std::cout << "rake: EV [" << last_ev_0 << ", " << last_ev_1 << "], expected rake -(EV0 + EV1) "
              << -(last_ev_0 + last_ev_1) << ", by reach " << direct_rake << " antes\n";
  }
  std::cout << "PREFLOP_BLUEPRINT_CHECKDOWN=PASS charts=" << written.size()
            << " max_gain_pot_percent=" << 100.0 * std::max(last_gain_0, last_gain_1) / initial_pot
            << '\n';
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_CHECKDOWN=FAIL " << error.what() << '\n';
    return 1;
  }
}
