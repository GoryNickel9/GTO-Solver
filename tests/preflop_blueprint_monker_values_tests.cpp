// Chart sets played inside our game (benchmarks/monker_chart_values.hpp and
// the chart reader of monker_chart_format.hpp): the loss of a chart set
// computed from the evaluator's preflop action values equals the EV change
// of the policy whose preflop rows of that player are replaced by the charts,
// evaluated from scratch; the recursion equals the node sum; our own charts
// lose exactly zero; the preflop response reproduces the aggregate's
// gain_preflop; the chart files read back as written.
//
// The best response against a chart set (monker_chart_exploitation.hpp):
// writing a policy's own preflop rows back into it leaves every number of
// the report unchanged, full and street-restricted; with random chart rows
// the chart player's EV moves by the chart loss while its own best response
// does not move, the per-class best-response choices match the mix, and the
// replaced rows restore the table bit for bit.
#include "preflop_blueprint_test_support.hpp"

#include "../benchmarks/monker_chart_exploitation.hpp"
#include "../benchmarks/monker_chart_values.hpp"

#include "gtosd/card_abstraction/deterministic_random.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace pb_test;
namespace mc = gtosd::monker_charts;

pb::BucketPolicy random_policy(const pb::CompiledGame &game, const Resources &resources,
                               const std::uint64_t seed) {
  const auto layout = pb::layout_state(game, resources.flop->capacity(),
                                       resources.turn->capacity(), resources.river->capacity());
  pb::BucketPolicy policy(game, layout);
  ca::DeterministicRandom random(seed);
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::Decision) {
      continue;
    }
    const auto rows = pb::StateLayout::rows_for(node.street, layout.flop_capacity,
                                                layout.turn_capacity, layout.river_capacity);
    for (std::uint32_t row = 0; row < rows; ++row) {
      auto probabilities = policy.row(node.id, row);
      double total = 0.0;
      for (auto &probability : probabilities) {
        probability = 0.05 + random.uniform_unit();
        total += probability;
      }
      for (auto &probability : probabilities) {
        probability /= total;
      }
    }
  }
  return policy;
}

pb::BestResponseResources response_resources(const Resources &resources) {
  pb::BestResponseResources view;
  view.ranks = &resources.ranks.value();
  view.all_in = &resources.all_in.value();
  view.catalog = &resources.catalog.value();
  view.flop = &resources.flop.value();
  view.turn = &resources.turn.value();
  view.river = &resources.river.value();
  return view;
}

// Our preflop rows of a hero, as the tool reads them.
mc::PreflopStrategy policy_rows(const pb::BucketPolicy &policy,
                                const pb::PreflopActionValues &values) {
  mc::PreflopStrategy rows(values.nodes.size());
  for (std::size_t slot = 0; slot < values.nodes.size(); ++slot) {
    rows[slot].resize(mc::class_count);
    for (std::size_t hand_class = 0; hand_class < mc::class_count; ++hand_class) {
      const auto row = policy.row(values.nodes[slot], static_cast<std::uint32_t>(hand_class));
      rows[slot][hand_class].assign(row.begin(), row.end());
    }
  }
  return rows;
}

struct Evaluation {
  std::vector<pb::FlopValues> values;
  std::vector<const pb::FlopValues *> pointers;
};

// Stage one on the flops, the groups standing for their physical flop
// (identity image) or for their suit orbit.
Evaluation evaluate(const pb::BestResponseEvaluator &evaluator,
                    const std::vector<pb::FlopGroup> &groups, const bool orbits) {
  auto values = pb::evaluate_flops(evaluator, groups, 2U);
  require(values.has_value(), "flop evaluation succeeds");
  Evaluation evaluation;
  evaluation.values = std::move(values.value());
  for (auto &entry : evaluation.values) {
    if (orbits) {
      entry.images = pb::flop_images(entry.flop);
    }
  }
  for (const auto &entry : evaluation.values) {
    evaluation.pointers.push_back(&entry);
  }
  return evaluation;
}

// The loss of random chart sets equals the EV change of the policy whose
// preflop rows of the hero are replaced by the charts, on sampled flops
// (identity images) and on canonical flops standing for their orbits.
void test_loss_identity(const Resources &resources) {
  const auto started = Clock::now();
  const auto game =
      pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  const auto view = response_resources(resources);
  const auto policy = random_policy(game.value(), resources, 0x4D56'0001ULL);
  const auto evaluator = pb::BestResponseEvaluator::create(game.value(), policy, view);
  require(evaluator.has_value(), "evaluator creates");
  const auto &flops = resources.catalog->flops();
  std::vector<pb::FlopGroup> groups;
  for (const std::size_t index : {std::size_t{3}, flops.size() / 3U}) {
    auto cards = flops[index].cards;
    std::sort(cards.begin(), cards.end());
    groups.push_back(pb::full_runouts(cards));
  }
  ca::DeterministicRandom random(0x4D56'0002ULL);
  std::uint32_t fallbacks = 0U;
  std::uint32_t outside = 0U;
  double largest_loss = 0.0;
  for (const bool orbits : {false, true}) {
    const auto base = evaluate(evaluator.value(), groups, orbits);
    const auto report = evaluator.value().aggregate(base.pointers, false);
    require(report.has_value(), "aggregation succeeds");
    for (std::uint8_t hero = 0; hero < 2U; ++hero) {
      const auto values = evaluator.value().preflop_action_values(base.pointers, hero);
      require(values.has_value(), "preflop action values");
      const auto &pav = values.value();
      double root_mean = 0.0;
      for (const auto value : pav.root_values) {
        root_mean += value;
      }
      require(close(root_mean / static_cast<double>(pav.root_values.size()), report.value().ev[hero],
                    1e-12),
              "the mean root value is the aggregate EV");
      const auto tree = mc::hero_tree(game.value(), pav.nodes, hero);
      const auto ours = policy_rows(policy, pav);

      // Our own rows as a chart set lose exactly nothing.
      const auto self = mc::chart_strategy(
          tree, ours, [&](const std::size_t slot, const std::size_t hand_class) {
            return std::optional<std::vector<double>>(ours[slot][hand_class]);
          });
      const auto self_loss = mc::chart_loss(tree, pav, ours, self);
      require(self_loss.loss == 0.0 && self_loss.loss_recursive == 0.0,
              "our own rows as charts lose exactly zero");

      const auto response = mc::preflop_response(tree, pav, ours);
      require(close(response.gain_per_combo, report.value().gain_preflop[hero], 1e-10),
              "the per-combo preflop response reproduces gain_preflop");
      require(response.gain_per_combo >= response.gain_per_class - 1e-12,
              "a per-class response never beats the per-combo one");
      if (!orbits) {
        const auto series = mc::group_series(
            base.pointers, hero, mc::entry_reach(game.value(), tree, hero, ours));
        require(close(mc::standard_error(series), report.value().ev_standard_error[hero], 1e-10),
                "the entry series reproduce the EV standard error");
      } else {
        require(mc::group_series(base.pointers, hero,
                                 mc::entry_reach(game.value(), tree, hero, ours))
                    .empty(),
                "orbit groups have no standard-error series");
      }

      // Random chart rows with exact zeros, some classes without a row.
      const auto charts = mc::chart_strategy(
          tree, ours,
          [&](const std::size_t slot,
              const std::size_t hand_class) -> std::optional<std::vector<double>> {
            if (random.uniform_below(6U) == 0U) {
              return std::nullopt;
            }
            std::vector<double> row(ours[slot][hand_class].size(), 0.0);
            double total = 0.0;
            for (auto &value : row) {
              value = random.uniform_below(3U) == 0U ? 0.0 : random.uniform_unit();
              total += value;
            }
            if (total == 0.0) {
              row.front() = 1.0;
              total = 1.0;
            }
            for (auto &value : row) {
              value /= total;
            }
            return row;
          });
      for (const auto &per_slot : charts.source) {
        for (const auto source : per_slot) {
          fallbacks += source == mc::RowSource::Fallback ? 1U : 0U;
          outside += source == mc::RowSource::OutsideRange ? 1U : 0U;
        }
      }
      const auto loss = mc::chart_loss(tree, pav, ours, charts);
      require(close(loss.loss, loss.loss_recursive, 1e-12),
              "the loss by nodes equals the recursion");
      require(close(loss.loss, loss.positive + loss.negative, 1e-12),
              "positive and negative terms add up to the loss");
      require(loss.loss >= -report.value().gain_preflop[hero] - 1e-12,
              "no chart set beats the best preflop response");

      // The policy with the hero's preflop rows replaced, from scratch.
      auto replaced = policy;
      for (std::size_t slot = 0; slot < pav.nodes.size(); ++slot) {
        for (std::size_t hand_class = 0; hand_class < mc::class_count; ++hand_class) {
          auto row = replaced.row(pav.nodes[slot], static_cast<std::uint32_t>(hand_class));
          std::copy(charts.rows[slot][hand_class].begin(), charts.rows[slot][hand_class].end(),
                    row.begin());
        }
      }
      const auto replaced_evaluator =
          pb::BestResponseEvaluator::create(game.value(), replaced, view);
      require(replaced_evaluator.has_value(), "replaced evaluator creates");
      const auto replaced_values = evaluate(replaced_evaluator.value(), groups, orbits);
      const auto replaced_report =
          replaced_evaluator.value().aggregate(replaced_values.pointers, false);
      require(replaced_report.has_value(), "replaced aggregation succeeds");
      require(close(replaced_report.value().ev[hero], report.value().ev[hero] - loss.loss, 1e-10),
              "EV with the charts equals our EV minus the chart loss");
      // The hero's entry values do not depend on its own preflop rows.
      for (std::size_t group = 0; group < groups.size(); ++group) {
        require(base.values[group].entry_values[hero][pb::average_mode] ==
                    replaced_values.values[group].entry_values[hero][pb::average_mode],
                "the hero's entry values ignore its own preflop rows");
      }
      largest_loss = std::max(largest_loss, std::abs(loss.loss));
    }
  }
  require(fallbacks > 0U && outside > 0U, "the random charts exercise both missing-row cases");
  std::cout << "loss identity: sampled and orbit groups, both heroes, " << fallbacks
            << " fallback rows, " << outside << " rows outside the range, largest loss "
            << largest_loss << " antes, "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

// The chart reader: CRLF lines, columns in another order than the edges,
// rows normalized by their total, all-zero rows outside the range, token
// mismatches and missing classes rejected; charts written by write_row_charts
// read back to the policy rows (three decimals) at the nodes chart_nodes
// names, in its order.
void test_chart_files(const Resources &resources, const std::filesystem::path &scratch) {
  const auto directory = scratch / "chart_files";
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  const auto path = directory / "crlf_strategy.txt";
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "Combination\tCall\tAllIn\tFold\tTotal\r\n"
           << "66\t0.500\t0.250\t0.250\t1.000\r\n"
           << "77\t0.000\t0.000\t0.000\t0.000\r\n"
           << "88\t0.333\t0.333\t0.333\t0.999\r\n";
  }
  const auto chart = mc::read_chart(path);
  require(chart.columns == std::vector<std::string>{"Call", "AllIn", "Fold"},
          "chart columns without Total");
  mc::ChartNode node;
  node.tokens = {"AllIn", "Call", "Fold"};
  const auto pair = mc::chart_row(node, chart, "66", path);
  require(pair.has_value() && (*pair)[0] == 0.25 && (*pair)[1] == 0.5 && (*pair)[2] == 0.25,
          "columns are matched to the edges by token");
  require(!mc::chart_row(node, chart, "77", path).has_value(), "an all-zero row is outside");
  const auto rounded = mc::chart_row(node, chart, "88", path);
  require(rounded.has_value() && close((*rounded)[0], 1.0 / 3.0, 1e-15),
          "rows are normalized by their total");
  bool rejected = false;
  try {
    static_cast<void>(mc::chart_row(node, chart, "99", path));
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "a missing class is rejected");
  rejected = false;
  node.tokens = {"AllIn", "Call", "Check"};
  try {
    static_cast<void>(mc::chart_row(node, chart, "66", path));
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "charts with other actions are rejected");

  const auto game =
      pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  const auto policy = random_policy(game.value(), resources, 0x4D56'0003ULL);
  const mc::RowStrategy row = [&](const std::uint32_t node_id, const std::uint8_t hand_class) {
    const auto values = policy.row(node_id, hand_class);
    return std::vector<double>(values.begin(), values.end());
  };
  const auto written = mc::write_row_charts(game.value(), row, directory / "policy");
  const auto nodes = mc::chart_nodes(game.value());
  require(written.size() == nodes.size(), "one chart per preflop decision node");
  const auto classes = mc::hand_classes();
  std::size_t rows_compared = 0U;
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    require(written[index] == nodes[index].relative, "chart_nodes follows write_charts");
    const auto chart_path = directory / "policy" / nodes[index].position / nodes[index].name;
    const auto file = mc::read_chart(chart_path);
    for (const auto &[hand_class, label] : classes.label_by_class) {
      const auto read = mc::chart_row(nodes[index], file, label, chart_path);
      if (!read) {
        continue;
      }
      const auto expected = policy.row(nodes[index].node, hand_class);
      for (std::size_t action = 0; action < expected.size(); ++action) {
        require(std::abs((*read)[action] - expected[action]) <= 1e-3,
                "written charts read back to the policy rows");
      }
      ++rows_compared;
    }
  }
  require(rows_compared > 0U, "some chart rows are in range");
  std::cout << "chart files: CRLF, token order, normalization, rejections, " << nodes.size()
            << " written charts read back (" << rows_compared << " rows) PASS\n";
}

// Largest absolute difference over every number of two best-response
// reports; infinity when their structure differs (list sizes, nodes, heroes,
// paths, restriction, the presence of a conditional gain or of the per-class
// choices, or a per-class choice).
double largest_report_difference(const pb::BestResponseReport &left,
                                 const pb::BestResponseReport &right) {
  constexpr double different = std::numeric_limits<double>::infinity();
  double largest = 0.0;
  const auto compare = [&](const double a, const double b) {
    largest = std::max(largest, std::abs(a - b));
  };
  const auto pair = [&](const std::array<double, 2> &a, const std::array<double, 2> &b) {
    compare(a[0], b[0]);
    compare(a[1], b[1]);
  };
  if (left.flops != right.flops || left.boards != right.boards ||
      left.deviation_from != right.deviation_from ||
      left.best_response_preflop_mix.size() != right.best_response_preflop_mix.size() ||
      left.postflop_entry_loss.size() != right.postflop_entry_loss.size() ||
      left.postflop_entry_route.size() != right.postflop_entry_route.size()) {
    return different;
  }
  pair(left.ev, right.ev);
  pair(left.best_response, right.best_response);
  pair(left.gain, right.gain);
  pair(left.best_response_lower, right.best_response_lower);
  pair(left.gain_lower, right.gain_lower);
  pair(left.best_response_preflop, right.best_response_preflop);
  pair(left.gain_preflop, right.gain_preflop);
  pair(left.best_response_route_average_value, right.best_response_route_average_value);
  pair(left.best_response_standard_error, right.best_response_standard_error);
  pair(left.ev_standard_error, right.ev_standard_error);
  compare(left.max_gain, right.max_gain);
  compare(left.max_gain_lower, right.max_gain_lower);
  compare(left.max_gain_half_width, right.max_gain_half_width);
  compare(left.nashconv, right.nashconv);
  for (std::size_t index = 0; index < left.best_response_preflop_mix.size(); ++index) {
    const auto &a = left.best_response_preflop_mix[index];
    const auto &b = right.best_response_preflop_mix[index];
    if (a.node != b.node || a.hero != b.hero || a.action_count != b.action_count ||
        a.split_classes != b.split_classes || a.class_action != b.class_action) {
      return different;
    }
    for (std::size_t action = 0; action < a.frequency.size(); ++action) {
      compare(a.frequency[action], b.frequency[action]);
    }
  }
  for (std::size_t index = 0; index < left.postflop_entry_loss.size(); ++index) {
    const auto &a = left.postflop_entry_loss[index];
    const auto &b = right.postflop_entry_loss[index];
    if (a.node != b.node || a.hero != b.hero ||
        a.conditional_gain.has_value() != b.conditional_gain.has_value()) {
      return different;
    }
    compare(a.mean_gain, b.mean_gain);
    compare(a.opponent_reach, b.opponent_reach);
    compare(a.entry_probability, b.entry_probability);
    if (a.conditional_gain) {
      compare(*a.conditional_gain, *b.conditional_gain);
    }
  }
  for (std::size_t index = 0; index < left.postflop_entry_route.size(); ++index) {
    const auto &a = left.postflop_entry_route[index];
    const auto &b = right.postflop_entry_route[index];
    if (a.node != b.node || a.hero != b.hero || a.path != b.path) {
      return different;
    }
    compare(a.average_probability, b.average_probability);
    compare(a.response_probability, b.response_probability);
    compare(a.postflop_gain_on_response_route, b.postflop_gain_on_response_route);
  }
  return largest;
}

// The per-class choices of every unrestricted mix give its frequencies; a
// restricted report has none.
void require_class_actions(const pb::BestResponseReport &report) {
  for (const auto &mix : report.best_response_preflop_mix) {
    if (report.deviation_from != pb::DeviationStreet::Preflop) {
      require(mix.class_action.empty(), "a restricted mix has no per-class choices");
      continue;
    }
    require(mix.class_action.size() == mc::class_count, "one choice per hand class");
    std::array<double, pb::maximum_actions> counts{};
    double seen = 0.0;
    for (const auto action : mix.class_action) {
      require(action >= -1 && action < static_cast<int>(mix.action_count),
              "per-class choices are actions of the node");
      if (action >= 0) {
        counts[static_cast<std::size_t>(action)] += 1.0;
        seen += 1.0;
      }
    }
    for (std::size_t action = 0; action < mix.action_count; ++action) {
      require(close(mix.frequency[action], seen > 0.0 ? counts[action] / seen : 0.0, 1e-15),
              "per-class choices reproduce the mix frequencies");
    }
  }
}

void test_exploitation(const Resources &resources) {
  const auto started = Clock::now();
  const auto game =
      pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  const auto view = response_resources(resources);
  const auto policy = random_policy(game.value(), resources, 0x4D56'0004ULL);
  const auto evaluator = pb::BestResponseEvaluator::create(game.value(), policy, view);
  require(evaluator.has_value(), "evaluator creates");
  const auto &flops = resources.catalog->flops();
  std::vector<pb::FlopGroup> groups;
  for (const std::size_t index : {std::size_t{7}, flops.size() / 2U}) {
    auto cards = flops[index].cards;
    std::sort(cards.begin(), cards.end());
    groups.push_back(pb::full_runouts(cards));
  }
  const auto evaluate_with = [&](const pb::BucketPolicy &rows, const bool orbits,
                                 const pb::DeviationStreet street) {
    std::size_t calls = 0U;
    auto evaluation = mc::evaluate_policy(
        game.value(), rows, view, groups, orbits, false, 2U, pb::RiverEngine::Joint, street,
        [&](const std::size_t done, const std::size_t total, const double) {
          require(done >= 1U && done <= total && total == groups.size(), "progress in range");
          ++calls;
        });
    require(calls == groups.size(), "one progress call per flop group");
    require(evaluation.boards > 0U && evaluation.physical_flops >= groups.size(),
            "the evaluation counts its boards and physical flops");
    return evaluation;
  };
  ca::DeterministicRandom random(0x4D56'0005ULL);
  double largest_self = 0.0;
  double largest_extra = 0.0;
  std::uint32_t changed_choices = 0U;
  for (const bool orbits : {false, true}) {
    // The helper reproduces the stage one and aggregate of the evaluator.
    const auto base = evaluate(evaluator.value(), groups, orbits);
    const auto direct = evaluator.value().aggregate(base.pointers, false);
    require(direct.has_value(), "aggregation succeeds");
    const auto ours = evaluate_with(policy, orbits, pb::DeviationStreet::Preflop);
    require(largest_report_difference(ours.report, direct.value()) == 0.0,
            "evaluate_policy reproduces the evaluator's report bit for bit");
    require_class_actions(ours.report);
    const auto ours_river = evaluate_with(policy, orbits, pb::DeviationStreet::River);
    require_class_actions(ours_river.report);

    for (std::uint8_t hero = 0; hero < 2U; ++hero) {
      const auto exploiter = static_cast<std::uint8_t>(1U - hero);
      const auto values = evaluator.value().preflop_action_values(base.pointers, hero);
      require(values.has_value(), "preflop action values");
      const auto &pav = values.value();
      const auto tree = mc::hero_tree(game.value(), pav.nodes, hero);
      const auto own = policy_rows(policy, pav);

      // Our own rows through the chart path: every number unchanged.
      const auto self =
          mc::chart_strategy(tree, own, [&](const std::size_t slot, const std::size_t hand_class) {
            return std::optional<std::vector<double>>(own[slot][hand_class]);
          });
      auto replaced = policy;
      const auto previous = mc::replace_preflop_rows(replaced, pav.nodes, self.rows);
      require(previous == own, "the replaced rows are the policy's own");
      require(replaced.table() == policy.table(), "own rows leave the table bit for bit");
      const auto self_full = evaluate_with(replaced, orbits, pb::DeviationStreet::Preflop);
      const auto self_river = evaluate_with(replaced, orbits, pb::DeviationStreet::River);
      const double difference =
          std::max(largest_report_difference(self_full.report, ours.report),
                   largest_report_difference(self_river.report, ours_river.report));
      require(difference == 0.0, "own rows as charts leave every number unchanged");
      largest_self = std::max(largest_self, difference);

      // Random chart rows, some classes without a row.
      const auto charts = mc::chart_strategy(
          tree, own,
          [&](const std::size_t slot,
              const std::size_t hand_class) -> std::optional<std::vector<double>> {
            if (random.uniform_below(5U) == 0U) {
              return std::nullopt;
            }
            std::vector<double> row(own[slot][hand_class].size(), 0.0);
            double total = 0.0;
            for (auto &value : row) {
              value = random.uniform_below(3U) == 0U ? 0.0 : random.uniform_unit();
              total += value;
            }
            if (total == 0.0) {
              row.back() = 1.0;
              total = 1.0;
            }
            for (auto &value : row) {
              value /= total;
            }
            return row;
          });
      const auto loss = mc::chart_loss(tree, pav, own, charts);
      const auto written = mc::replace_preflop_rows(replaced, pav.nodes, charts.rows);
      require(written == own, "the rows replaced are our rows");
      const auto chart_full = evaluate_with(replaced, orbits, pb::DeviationStreet::Preflop);
      const auto &report = chart_full.report;
      require(close(report.ev[hero], ours.report.ev[hero] - loss.loss, 1e-10),
              "the chart player's EV moves by the chart loss");
      require(close(report.best_response[hero], ours.report.best_response[hero], 1e-12),
              "the chart player's best response ignores its own preflop rows");
      require(report.gain[exploiter] >= -1e-12 && report.gain_lower[exploiter] >= -1e-12 &&
                  report.gain_preflop[exploiter] >= -1e-12,
              "the exploiter's gains are not negative");
      require(report.gain[exploiter] >= report.gain_lower[exploiter] - 1e-12 &&
                  report.gain[exploiter] >= report.gain_preflop[exploiter] - 1e-12,
              "the full best response dominates the partial ones");
      require(close(report.nashconv, report.gain[0] + report.gain[1], 1e-12),
              "nashconv adds the gains");
      require_class_actions(report);
      require(largest_report_difference(report, ours.report) > 0.0,
              "random charts change the report");
      for (std::size_t index = 0; index < report.best_response_preflop_mix.size(); ++index) {
        const auto &mix = report.best_response_preflop_mix[index];
        const auto &baseline = ours.report.best_response_preflop_mix[index];
        require(mix.node == baseline.node && mix.hero == baseline.hero,
                "the mixes of both passes match node by node");
        if (mix.hero == hero) {
          require(mix.class_action == baseline.class_action,
                  "the chart player's best-response choices do not move");
        } else {
          for (std::size_t hand_class = 0; hand_class < mix.class_action.size(); ++hand_class) {
            changed_choices +=
                mix.class_action[hand_class] != baseline.class_action[hand_class] ? 1U : 0U;
          }
        }
      }
      largest_extra =
          std::max(largest_extra, std::abs(report.gain[exploiter] - ours.report.gain[exploiter]));
      const auto chart_river = evaluate_with(replaced, orbits, pb::DeviationStreet::River);
      require_class_actions(chart_river.report);
      require(chart_river.report.gain[exploiter] <= report.gain[exploiter] + 1e-12,
              "a river-restricted response never gains more than the full one");

      // Writing the replaced rows back restores the table bit for bit.
      const auto restored = mc::replace_preflop_rows(replaced, pav.nodes, written);
      require(restored == charts.rows, "the rows written back replace the chart rows");
      require(replaced.table() == policy.table(), "the table is restored bit for bit");
    }
  }
  bool rejected = false;
  try {
    auto copy = policy;
    static_cast<void>(mc::replace_preflop_rows(copy, {0U}, mc::PreflopStrategy{}));
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "rows for another number of nodes are rejected");
  require(changed_choices > 0U, "random charts change some best-response choices");
  std::cout << "exploitation: sampled and orbit groups, both chart players, own rows change "
            << largest_self << ", random charts move the exploiter's gain by up to "
            << largest_extra << " antes and " << changed_choices << " class choices, "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path resources_dir;
    std::filesystem::path buckets_dir;
    std::filesystem::path scratch_dir =
        std::filesystem::temp_directory_path() / "gtosd_monker_values_tests";
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string_view name = argv[index];
      const std::string_view value = argv[index + 1];
      if (name == "--resources-dir") {
        resources_dir = std::filesystem::path(value);
      } else if (name == "--buckets-dir") {
        buckets_dir = std::filesystem::path(value);
      } else if (name == "--scratch-dir") {
        scratch_dir = std::filesystem::path(value);
      }
    }
    std::filesystem::create_directories(scratch_dir);
    const auto resources = load_resources(resources_dir, buckets_dir);
    std::cout << "resources " << (resources.loaded ? "loaded" : "built") << ", bucket tables "
              << (resources.buckets_loaded ? "loaded" : "synthetic") << "\n";
    test_chart_files(resources, scratch_dir);
    test_loss_identity(resources);
    test_exploitation(resources);
    std::cout << "PREFLOP_BLUEPRINT_MONKER_VALUES_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_MONKER_VALUES_TESTS=FAIL " << error.what() << '\n';
    return 1;
  }
}
