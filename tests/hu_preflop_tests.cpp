#include "gtosd/preflop/hu_preflop_legacy.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::size_t assertions = 0;
std::string_view test_filter;

void run_test(const std::string_view name, void (*test)()) {
  if (!test_filter.empty() && name.find(test_filter) == std::string_view::npos) {
    return;
  }
  if (!test_filter.empty()) {
    std::cout << "HU_PREFLOP_TEST_BEGIN name=" << name << '\n' << std::flush;
  }
  test();
  if (!test_filter.empty()) {
    std::cout << "HU_PREFLOP_TEST_END name=" << name << " assertions=" << assertions << '\n'
              << std::flush;
  }
}

void require(const bool condition, const std::string &message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(message);
  }
}

const gtosd::HuPreflopNode &node_at(const gtosd::HuPreflopTree &tree, const std::uint32_t id) {
  require(id < tree.nodes.size(), "edge child is in bounds");
  return tree.nodes[id];
}

gtosd::HuPreflopSampledPostflopPolicy
make_uniform_sampled_postflop_policy(const gtosd::HuPreflopTree &tree,
                                     const std::uint64_t iterations) {
  gtosd::HuPreflopSampledPostflopPolicy policy;
  policy.minor = gtosd::HuPreflopSampledPostflopPolicy::minimum_supported_minor;
  policy.tree_fingerprint = tree.fingerprint;
  policy.algorithm = "test_uniform_sampled_policy_v1";
  policy.abstraction_id = "preflop_exact81_postflop_physical_lossless_suit_isomorphism_v2";
  policy.iterations = iterations;
  policy.seed = 0x554E'4946'4F52'4D01ULL;
  policy.representation = gtosd::HuPreflopPostflopRepresentation::ExactPhysical;
  policy.fingerprint = gtosd::fingerprint_hu_preflop_sampled_postflop_policy(policy);
  return policy;
}

const gtosd::HuPreflopNode *find_stage(const gtosd::HuPreflopTree &tree,
                                       const gtosd::HuPreflopDecisionStage stage,
                                       const std::int64_t current_bet_units) {
  const auto found = std::ranges::find_if(tree.nodes, [&](const auto &node) {
    return node.kind == gtosd::HuPreflopNodeKind::Decision && node.stage == stage &&
           node.state.current_bet.units() == current_bet_units;
  });
  return found == tree.nodes.end() ? nullptr : &*found;
}

bool has_target(const gtosd::HuPreflopNode &node, const gtosd::ActionType type,
                const std::int64_t target_units) {
  return std::ranges::any_of(node.edges, [&](const auto &edge) {
    const auto actor = node.state.player_to_act;
    return edge.action.type == type &&
           node.state.committed_this_street[actor].units() + edge.action.amount.units() ==
               target_units;
  });
}

std::string ledger_action_name(const gtosd::HuPreflopNode &node,
                               const gtosd::Action &action) {
  switch (action.type) {
  case gtosd::ActionType::Fold:
    return "fold";
  case gtosd::ActionType::Call:
    return "call";
  case gtosd::ActionType::Check:
    return "check";
  case gtosd::ActionType::AllIn:
    return "all_in";
  case gtosd::ActionType::Bet:
  case gtosd::ActionType::Raise: {
    const auto actor = node.state.player_to_act;
    const auto target = node.state.committed_this_street[actor].units() + action.amount.units();
    if (target == 60'000) {
      return "raise_6";
    }
    if (target == 100'000) {
      return "raise_10";
    }
    if (target == 105'000) {
      return "raise_10_5";
    }
    if (target == 145'000) {
      return "raise_14_5";
    }
    throw std::runtime_error("unexpected live preflop target in monetary ledger");
  }
  }
  throw std::runtime_error("unknown preflop action in monetary ledger");
}

gtosd::Result<gtosd::HuPreflopWholeGameCoverageAccumulator, gtosd::HuPreflopError>
make_complete_zero_profile_coverage(const gtosd::HuPreflopTree &tree,
                                    const gtosd::HuPreflopDecompositionPlan &plan,
                                    const std::uint64_t blueprint_iterations) {
  auto coverage = gtosd::make_hu_preflop_whole_game_coverage_accumulator(tree, plan, 0.01);
  const auto tasks = gtosd::enumerate_hu_preflop_canonical_flop_tasks(plan);
  if (!coverage || !tasks || blueprint_iterations == 0U) {
    return gtosd::Result<gtosd::HuPreflopWholeGameCoverageAccumulator, gtosd::HuPreflopError>::
        failure(!coverage ? coverage.error()
                          : (!tasks ? tasks.error() : gtosd::HuPreflopError::InvalidConfiguration));
  }
  gtosd::HuPreflopComboReach zero_cfvs{};
  const auto streamed = gtosd::stream_hu_preflop_whole_game_boundaries(
      tree, plan, coverage.value(), [&](const gtosd::HuPreflopWholeGameBoundaryRequest &request) {
        const auto conditioned = gtosd::condition_hu_preflop_ranges_on_flop(
            plan, request.task.entry_node, request.task.flop);
        if (!conditioned) {
          return gtosd::Result<std::vector<gtosd::HuPreflopFlopBoundary>,
                               gtosd::HuPreflopError>::failure(conditioned.error());
        }
        std::vector<gtosd::HuPreflopFlopBoundary> boundaries;
        for (std::uint8_t resolver = 0U; resolver < 2U; ++resolver) {
          if ((request.missing_resolver_mask & static_cast<std::uint8_t>(1U << resolver)) == 0U) {
            continue;
          }
          auto boundary = gtosd::build_hu_preflop_flop_boundary(
              tree, plan, conditioned.value(), resolver, zero_cfvs,
              "continuation:zero-profile-coverage:v1", blueprint_iterations);
          if (!boundary) {
            return gtosd::Result<std::vector<gtosd::HuPreflopFlopBoundary>,
                                 gtosd::HuPreflopError>::failure(boundary.error());
          }
          boundaries.push_back(std::move(boundary.value()));
        }
        return gtosd::Result<std::vector<gtosd::HuPreflopFlopBoundary>,
                             gtosd::HuPreflopError>::success(std::move(boundaries));
      });
  if (!streamed || streamed.value() != tasks.value().size()) {
    return gtosd::Result<gtosd::HuPreflopWholeGameCoverageAccumulator, gtosd::HuPreflopError>::
        failure(streamed ? gtosd::HuPreflopError::IntegrityFailure : streamed.error());
  }
  return coverage;
}

void test_co40_tree_contract() {
  const auto config = gtosd::make_hu_co40_benchmark_config();
  require(gtosd::validate_hu_preflop_config(config).has_value(),
          "CO40 benchmark configuration validates");
  const auto built = gtosd::build_hu_preflop_tree(config);
  require(built.has_value(), "CO40 preflop tree builds");
  const auto &tree = built.value();
  const auto &root = node_at(tree, tree.root);
  require(root.edges.size() == 5U, "root has fold, call, two raises and all-in");
  require(has_target(root, gtosd::ActionType::Raise, 60'000), "root has raise-to 6a");
  require(has_target(root, gtosd::ActionType::Raise, 100'000), "root has raise-to 10a");
  require(has_target(root, gtosd::ActionType::AllIn, 390'000),
          "root all-in reaches 39a live plus the dead ante");

  const auto *small = find_stage(tree, gtosd::HuPreflopDecisionStage::FacingSmallOpen, 60'000);
  require(small != nullptr, "BTN response to raise-to 6a exists");
  require(has_target(*small, gtosd::ActionType::Raise, 105'000),
          "BTN has configured raise-to 10.5a");
  const auto small_incomplete_edge = std::ranges::find_if(
      small->edges, [](const auto &edge) { return edge.action.type == gtosd::ActionType::Raise; });
  require(small_incomplete_edge != small->edges.end(), "small-open incomplete edge found");
  const auto &after_small_incomplete = node_at(tree, small_incomplete_edge->child);
  require(after_small_incomplete.state.last_full_raise_increment.units() == 50'000,
          "raise-to 10.5a preserves the prior 5a full-raise increment");

  const auto *large = find_stage(tree, gtosd::HuPreflopDecisionStage::FacingLargeOpen, 100'000);
  require(large != nullptr, "BTN response to raise-to 10a exists");
  require(has_target(*large, gtosd::ActionType::Raise, 145'000),
          "explicit benchmark override admits raise-to 14.5a");
  const auto incomplete_edge = std::ranges::find_if(
      large->edges, [](const auto &edge) { return edge.action.type == gtosd::ActionType::Raise; });
  require(incomplete_edge != large->edges.end(), "incomplete benchmark edge found");
  const auto &after_incomplete = node_at(tree, incomplete_edge->child);
  require(after_incomplete.state.last_full_raise_increment.units() == 90'000,
          "raise-to 14.5a preserves the prior 9a full-raise increment");
  require(after_incomplete.edges.size() == 3U,
          "after configured re-raise only fold, call and all-in remain");

  const auto call_edge = std::ranges::find_if(
      root.edges, [](const auto &edge) { return edge.action.type == gtosd::ActionType::Call; });
  require(call_edge != root.edges.end(), "root call edge exists");
  const auto &limp_option = node_at(tree, call_edge->child);
  require(limp_option.stage == gtosd::HuPreflopDecisionStage::LimpOption &&
              limp_option.edges.size() == 4U,
          "BTN limp option has check, two raises and all-in");
  require(has_target(limp_option, gtosd::ActionType::Bet, 60'000), "limp branch mirrors target 6a");
  require(has_target(limp_option, gtosd::ActionType::Bet, 100'000),
          "limp branch mirrors target 10a");

  const auto rebuilt = gtosd::build_hu_preflop_tree(config);
  require(rebuilt.has_value() && rebuilt.value().fingerprint == tree.fingerprint,
          "preflop tree fingerprint is deterministic");
  require(tree.stats.postflop_entries > 0U && tree.stats.terminal_folds > 0U &&
              tree.stats.terminal_all_ins > 0U,
          "preflop tree reaches every terminal class");
}

void test_co40_monetary_ledger() {
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config()).value();
  constexpr auto no_fold = std::numeric_limits<std::int64_t>::max();
  struct ExpectedNode {
    std::string_view path;
    std::uint8_t actor;
    std::int64_t co_live;
    std::int64_t btn_live;
    std::int64_t fold_payoff;
  };
  constexpr std::array expected{
      ExpectedNode{"root", 0U, 0, 10'000, -10'000},
      ExpectedNode{"CO:all_in", 1U, 390'000, 10'000, -20'000},
      ExpectedNode{"CO:call", 1U, 10'000, 10'000, no_fold},
      ExpectedNode{"CO:raise_10", 1U, 100'000, 10'000, -20'000},
      ExpectedNode{"CO:raise_6", 1U, 60'000, 10'000, -20'000},
      ExpectedNode{"CO:call/BTN:all_in", 0U, 10'000, 390'000, -20'000},
      ExpectedNode{"CO:call/BTN:raise_10", 0U, 10'000, 100'000, -20'000},
      ExpectedNode{"CO:call/BTN:raise_6", 0U, 10'000, 60'000, -20'000},
      ExpectedNode{"CO:raise_10/BTN:all_in", 0U, 100'000, 390'000, -110'000},
      ExpectedNode{"CO:raise_10/BTN:raise_14_5", 0U, 100'000, 145'000, -110'000},
      ExpectedNode{"CO:raise_6/BTN:all_in", 0U, 60'000, 390'000, -70'000},
      ExpectedNode{"CO:raise_6/BTN:raise_10_5", 0U, 60'000, 105'000, -70'000},
      ExpectedNode{"CO:call/BTN:raise_10/CO:all_in", 1U, 390'000, 100'000, -110'000},
      ExpectedNode{"CO:call/BTN:raise_10/CO:raise_14_5", 1U, 145'000, 100'000, -110'000},
      ExpectedNode{"CO:call/BTN:raise_6/CO:all_in", 1U, 390'000, 60'000, -70'000},
      ExpectedNode{"CO:call/BTN:raise_6/CO:raise_10_5", 1U, 105'000, 60'000, -70'000},
      ExpectedNode{"CO:raise_10/BTN:raise_14_5/CO:all_in", 1U, 390'000, 145'000, -155'000},
      ExpectedNode{"CO:raise_6/BTN:raise_10_5/CO:all_in", 1U, 390'000, 105'000, -115'000},
      ExpectedNode{"CO:call/BTN:raise_10/CO:raise_14_5/BTN:all_in", 0U, 145'000, 390'000,
                   -155'000},
      ExpectedNode{"CO:call/BTN:raise_6/CO:raise_10_5/BTN:all_in", 0U, 105'000, 390'000,
                   -115'000},
  };

  std::map<std::string, const gtosd::HuPreflopNode *> decisions;
  std::function<void(std::uint32_t, const std::string &)> visit;
  visit = [&](const std::uint32_t node_id, const std::string &path) {
    const auto &node = node_at(tree, node_id);
    if (node.kind != gtosd::HuPreflopNodeKind::Decision) {
      return;
    }
    require(decisions.emplace(path, &node).second, "monetary ledger path is unique");
    for (const auto &edge : node.edges) {
      const auto player = node.state.player_to_act == 0U ? "CO" : "BTN";
      const auto step = std::string(player) + ":" + ledger_action_name(node, edge.action);
      visit(edge.child, path == "root" ? step : path + "/" + step);
    }
  };
  visit(tree.root, "root");
  if (decisions.size() != expected.size()) {
    for (const auto &[path, node] : decisions) {
      std::cerr << "HU_PREFLOP_LEDGER_PATH path=" << path
                << " actor=" << static_cast<unsigned>(node->state.player_to_act)
                << " co_live=" << node->state.committed_this_street[0].units()
                << " btn_live=" << node->state.committed_this_street[1].units() << '\n';
    }
  }
  require(decisions.size() == expected.size(), "CO40 tree exposes exactly 20 decision ledgers");

  for (const auto &entry : expected) {
    const auto found = decisions.find(std::string(entry.path));
    require(found != decisions.end(), "expected monetary ledger path exists");
    const auto &node = *found->second;
    const auto &state = node.state;
    require(state.player_to_act == entry.actor, "ledger actor matches frozen path");
    require(state.initial_pot.units() == 20'000 &&
                state.initial_pot_contributions[0].units() == 10'000 &&
                state.initial_pot_contributions[1].units() == 10'000,
            "dead antes remain separate at every preflop decision");
    require(state.committed_this_street[0].units() == entry.co_live &&
                state.committed_this_street[1].units() == entry.btn_live &&
                state.committed_total == state.committed_this_street,
            "live commitments match the frozen monetary ledger");
    require(state.pot.units() == 20'000 + entry.co_live + entry.btn_live,
            "pot equals dead antes plus live commitments");
    require(state.remaining_stacks[0].units() + entry.co_live + 10'000 == 400'000 &&
                state.remaining_stacks[1].units() + entry.btn_live + 10'000 == 400'000,
            "each player preserves the 40a total stack ledger");

    const auto fold = std::ranges::find_if(
        node.edges, [](const auto &edge) { return edge.action.type == gtosd::ActionType::Fold; });
    if (entry.fold_payoff == no_fold) {
      require(fold == node.edges.end(), "check option has no artificial fold action");
      continue;
    }
    require(fold != node.edges.end(), "facing action exposes fold");
    const auto &terminal = node_at(tree, fold->child);
    const auto settlement = gtosd::settle_terminal(terminal.state, tree.config.rake, 0U);
    require(settlement.has_value(), "fold terminal settles");
    require(settlement.value().payoff_units[entry.actor] == entry.fold_payoff,
            "fold EV equals negative dead ante plus live commitment");
    require(settlement.value().payoff_units[0] + settlement.value().payoff_units[1] == 0,
            "rake-free fold settlement remains zero-sum");
  }
}

void test_declarative_config() {
  std::ifstream input(std::string(GTOSD_SOURCE_DIR) +
                          "/benchmarks/fixtures/hu_preflop_co40_game_v1.json",
                      std::ios::binary);
  require(static_cast<bool>(input), "declarative CO40 configuration opens");
  const std::string serialized{std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>()};
  const auto parsed = gtosd::deserialize_hu_preflop_config_json(serialized);
  require(parsed.has_value(), "declarative CO40 configuration parses");
  const auto expected = gtosd::make_hu_co40_benchmark_config();
  require(parsed.value().effective_stack == expected.effective_stack &&
              parsed.value().open_targets == expected.open_targets &&
              parsed.value().response_targets == expected.response_targets &&
              parsed.value().postflop_sizes == expected.postflop_sizes &&
              !parsed.value().rake.enabled && parsed.value().allow_configured_incomplete_raise,
          "declarative configuration matches the benchmark contract");
  auto stale_contract = serialized;
  const auto revision = stale_contract.find("\"monetary_contract_revision\": 2");
  require(revision != std::string::npos, "declarative fixture exposes monetary revision");
  stale_contract.replace(revision, std::string("\"monetary_contract_revision\": 2").size(),
                         "\"monetary_contract_revision\": 1");
  require(!gtosd::deserialize_hu_preflop_config_json(stale_contract),
          "preflop configuration rejects the stale monetary contract");
  std::ifstream rake_input(
      std::string(GTOSD_SOURCE_DIR) +
          "/benchmarks/fixtures/hu_preflop_co40_rake5_cap3_sensitivity_v2.json",
      std::ios::binary);
  const std::string rake_serialized{std::istreambuf_iterator<char>(rake_input),
                                    std::istreambuf_iterator<char>()};
  const auto rake_config = gtosd::deserialize_hu_preflop_config_json(rake_serialized);
  const auto rake_tree = rake_config
                             ? gtosd::build_hu_preflop_tree(rake_config.value())
                             : gtosd::Result<gtosd::HuPreflopTree, gtosd::HuPreflopError>::failure(
                                   gtosd::HuPreflopError::InvalidConfiguration);
  require(rake_config.has_value() && rake_tree.has_value() && rake_config.value().rake.enabled &&
              rake_config.value().rake.percentage.basis_points() == 500U &&
              rake_config.value().rake.cap.units() == 30'000 &&
              rake_tree.value().fingerprint !=
                  gtosd::build_hu_preflop_tree(expected).value().fingerprint,
          "versioned HU configuration binds explicit rake into the game fingerprint");
  const auto all_in = std::ranges::find_if(rake_tree.value().nodes, [](const auto &node) {
    return node.kind == gtosd::HuPreflopNodeKind::TerminalAllIn;
  });
  const auto settlement = all_in != rake_tree.value().nodes.end()
                              ? gtosd::settle_terminal(all_in->state, rake_config.value().rake, 1U)
                              : gtosd::Result<gtosd::Settlement, gtosd::GameError>::failure(
                                    gtosd::GameError::NotTerminal);
  require(settlement.has_value() && settlement.value().rake.units() > 0,
          "no-flop-no-drop applies rake when a preflop all-in runs out a board");
  require(!gtosd::deserialize_hu_preflop_config_json("{}"),
          "invalid declarative configuration fails closed");
}

void test_postflop_natural_termination() {
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config()).value();
  const auto analyzed = gtosd::analyze_hu_postflop_public_skeleton(tree);
  require(analyzed.has_value(), "postflop public skeleton analyzes");
  const auto &stats = analyzed.value();
  require(stats.represented_nodes > stats.decision_nodes && stats.action_edges > 0U,
          "postflop skeleton contains decisions and terminals");
  require(stats.maximum_observed_raise_count < gtosd::maximum_core_raise_depth,
          "stack exhausts before the core safety limit");
  require(!stats.core_raise_safety_limit_reached && stats.natural_stack_termination_proven,
          "natural all-in termination is proven for stack 40a");
}

void test_flop_decomposition_contract_and_resources() {
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config()).value();
  const auto blueprint = gtosd::make_uniform_hu_preflop_blueprint(tree);
  require(blueprint.has_value() &&
              gtosd::validate_hu_preflop_blueprint(tree, blueprint.value()).has_value(),
          "uniform dense preflop blueprint validates");
  const auto plan = gtosd::derive_hu_preflop_decomposition_plan(tree, blueprint.value());
  require(plan.has_value(), "public-Flop decomposition plan derives");
  require(plan.value().entries.size() == 9U && plan.value().physical_flops == 7'140U &&
              plan.value().canonical_flops == 573U &&
              plan.value().canonical_flop_catalog.size() == 573U &&
              plan.value().canonical_public_flop_roots == 9U * plan.value().canonical_flops &&
              plan.value().public_flop_roots == 64'260U &&
              plan.value().physical_private_deals == 353'430U &&
              plan.value().physical_deal_flop_histories == 15'777'115'200ULL,
          "plan counts every entry, physical deal and public Flop root");
  const auto catalog_mass = std::accumulate(
      plan.value().canonical_flop_catalog.begin(), plan.value().canonical_flop_catalog.end(),
      std::uint64_t{0}, [](const std::uint64_t total, const auto &flop_entry) {
        return total + flop_entry.physical_outcome_count;
      });
  require(catalog_mass == 7'140U && std::ranges::all_of(plan.value().canonical_flop_catalog,
                                                        [](const auto &entry) {
                                                          return entry.physical_outcome_count > 0U;
                                                        }),
          "canonical Flop catalog retains every physical chance outcome exactly once");
  const auto tasks = gtosd::enumerate_hu_preflop_canonical_flop_tasks(plan.value());
  require(tasks.has_value() && tasks.value().size() == 5'157U,
          "entry by canonical-Flop work catalog is complete");
  const auto task_probability = std::accumulate(
      tasks.value().begin(), tasks.value().end(), 0.0,
      [](const double total, const auto &task) { return total + task.joint_probability; });
  require(std::abs(task_probability - plan.value().postflop_entry_probability) <= 1.0e-10 &&
              std::ranges::all_of(tasks.value(),
                                  [](const auto &task) {
                                    return task.physical_outcome_count > 0U &&
                                           task.joint_probability > 0.0 &&
                                           !task.fingerprint.empty();
                                  }),
          "canonical tasks preserve physical postflop reach probability");
  require(std::abs(plan.value().total_probability - 1.0) <= 1.0e-10 &&
              plan.value().postflop_entry_probability > 0.0 &&
              plan.value().terminal_fold_probability > 0.0 &&
              plan.value().terminal_all_in_probability > 0.0,
          "postflop, fold and all-in leaves partition total preflop probability");
  require(plan.value().bytes.reach_template_bytes == 90'720U &&
              plan.value().bytes.one_flop_conditioned_range_bytes == 10'080U &&
              plan.value().bytes.one_resolver_boundary_bytes == 12'672U &&
              plan.value().bytes.all_resolver_boundaries_bytes == 814'302'720U &&
              plan.value().bytes.both_players_boundary_bytes == 1'628'605'440ULL &&
              plan.value().bytes.canonical_all_resolver_boundaries_bytes == 65'349'504U &&
              plan.value().bytes.canonical_both_players_boundary_bytes == 130'699'008U &&
              plan.value().bytes.fully_materialized_range_bytes == 647'740'800U &&
              plan.value().bytes.canonical_fully_materialized_range_bytes == 51'982'560U &&
              plan.value().bytes.whole_game_coverage_mask_bytes == 5'157U &&
              plan.value().bytes.whole_game_coverage_probability_bytes == 41'256U &&
              plan.value().bytes.whole_game_profile_utility_bytes == 82'512U &&
              plan.value().bytes.whole_game_local_continuation_identity_bytes == 41'256U &&
              plan.value().bytes.whole_game_coverage_payload_bytes == 170'181U &&
              plan.value().bytes.streaming_certification_live_payload_bytes == 365'706U,
          "byte model separates streaming working set from full materialization");

  const std::array flop{gtosd::parse_card("6c").value(), gtosd::parse_card("7d").value(),
                        gtosd::parse_card("Ah").value()};
  const auto entry_node = plan.value().entries.front().entry_node;
  const auto conditioned =
      gtosd::condition_hu_preflop_ranges_on_flop(plan.value(), entry_node, flop);
  require(conditioned.has_value() && conditioned.value().live_positive_combo_count[0] == 528U &&
              conditioned.value().live_positive_combo_count[1] == 528U &&
              conditioned.value().compatible_joint_reach_mass > 0.0,
          "Flop conditioning removes exactly the blocked private combos");
  const auto quantized = gtosd::quantize_hu_preflop_conditioned_ranges(conditioned.value());
  require(quantized.has_value() && quantized.value().maximum_absolute_probability_error <= 1.0e-12,
          "uniform relative reaches cross the legacy basis-point adapter without error");

  gtosd::HuPreflopComboReach cfvs{};
  for (std::size_t combo = 0U; combo < cfvs.size(); ++combo) {
    cfvs[combo] = static_cast<double>(combo) / 1'000.0;
  }
  const auto boundary = gtosd::build_hu_preflop_flop_boundary(
      tree, plan.value(), conditioned.value(), 0U, cfvs, "continuation:test:v1", 100U);
  require(boundary.has_value() && boundary.value().values.size() == 528U &&
              gtosd::validate_hu_preflop_flop_boundary(tree, plan.value(), boundary.value())
                  .has_value(),
          "complete opponent CFV boundary validates for one public Flop root");
  require(std::ranges::all_of(boundary.value().values,
                              [](const auto &value) {
                                return value.positive_reach && value.counterfactual_reach > 0.0;
                              }),
          "counterfactual reach includes chance and resolver reach for every live opponent combo");

  auto coverage = gtosd::make_hu_preflop_whole_game_coverage_accumulator(tree, plan.value(), 0.01);
  require(coverage.has_value() && coverage.value().task_side_coverage.size() == 5'157U &&
              coverage.value().task_joint_probabilities.size() == 5'157U &&
              coverage.value().task_side_profile_utility_antes.size() == 10'314U &&
              coverage.value().task_local_continuation_identity_hashes.size() == 5'157U &&
              coverage.value().blueprint_iterations == 0U &&
              coverage.value().continuation_checkpoint_fingerprint.empty() &&
              coverage.value().validated_boundary_count == 0U &&
              gtosd::validate_hu_preflop_whole_game_coverage_accumulator(tree, plan.value(),
                                                                         coverage.value()),
          "streaming whole-game coverage starts empty and validates against the task catalog");
  require(gtosd::accumulate_hu_preflop_whole_game_boundary(tree, plan.value(), coverage.value(),
                                                           boundary.value())
                  .has_value() &&
              coverage.value().continuation_checkpoint_fingerprint == "continuation:test:v1",
          "first resolver boundary enters the streaming coverage ledger");
  const auto fingerprint_after_first_side = coverage.value().fingerprint;
  const auto continuation_after_first_side = coverage.value().continuation_checkpoint_fingerprint;
  require(!gtosd::accumulate_hu_preflop_whole_game_boundary(tree, plan.value(), coverage.value(),
                                                            boundary.value()) &&
              coverage.value().validated_boundary_count == 1U &&
              coverage.value().fingerprint == fingerprint_after_first_side,
          "duplicate resolver boundary is rejected without mutating coverage");

  const auto mixed_checkpoint_boundary = gtosd::build_hu_preflop_flop_boundary(
      tree, plan.value(), conditioned.value(), 1U, cfvs, "continuation:different:v1", 100U);
  require(
      mixed_checkpoint_boundary.has_value() &&
          !gtosd::accumulate_hu_preflop_whole_game_boundary(tree, plan.value(), coverage.value(),
                                                            mixed_checkpoint_boundary.value()) &&
          coverage.value().validated_boundary_count == 1U &&
          coverage.value().continuation_checkpoint_fingerprint == continuation_after_first_side &&
          coverage.value().fingerprint == fingerprint_after_first_side,
      "boundaries from different continuation checkpoints cannot be mixed");

  const auto mixed_local_boundary = gtosd::build_hu_preflop_flop_boundary(
      tree, plan.value(), conditioned.value(), 1U, cfvs, "continuation:different-local-task:v1",
      100U, "continuation:test:v1");
  require(mixed_local_boundary.has_value() &&
              !gtosd::accumulate_hu_preflop_whole_game_boundary(
                  tree, plan.value(), coverage.value(), mixed_local_boundary.value()) &&
              coverage.value().validated_boundary_count == 1U &&
              coverage.value().fingerprint == fingerprint_after_first_side,
          "two sides of one task cannot use different local continuation identities");

  const auto first_task_conditioned = gtosd::condition_hu_preflop_ranges_on_flop(
      plan.value(), tasks.value().front().entry_node, tasks.value().front().flop);
  const auto first_task_side_zero =
      first_task_conditioned
          ? gtosd::build_hu_preflop_flop_boundary(tree, plan.value(),
                                                  first_task_conditioned.value(), 0U, cfvs,
                                                  "continuation:streaming-test:v1", 100U)
          : gtosd::Result<gtosd::HuPreflopFlopBoundary, gtosd::HuPreflopError>::failure(
                first_task_conditioned.error());
  const auto first_task_side_one =
      first_task_conditioned
          ? gtosd::build_hu_preflop_flop_boundary(tree, plan.value(),
                                                  first_task_conditioned.value(), 1U, cfvs,
                                                  "continuation:streaming-test:v1", 100U)
          : gtosd::Result<gtosd::HuPreflopFlopBoundary, gtosd::HuPreflopError>::failure(
                first_task_conditioned.error());
  auto wrong_provider_coverage =
      gtosd::make_hu_preflop_whole_game_coverage_accumulator(tree, plan.value(), 0.01);
  const auto wrong_provider_initial_fingerprint = wrong_provider_coverage.value().fingerprint;
  const auto wrong_provider_result = gtosd::stream_hu_preflop_whole_game_boundaries(
      tree, plan.value(), wrong_provider_coverage.value(),
      [&](const gtosd::HuPreflopWholeGameBoundaryRequest &) {
        std::vector<gtosd::HuPreflopFlopBoundary> incomplete;
        incomplete.push_back(first_task_side_zero.value());
        return gtosd::Result<std::vector<gtosd::HuPreflopFlopBoundary>,
                             gtosd::HuPreflopError>::success(std::move(incomplete));
      });
  require(first_task_conditioned.has_value() && first_task_side_zero.has_value() &&
              first_task_side_one.has_value() && wrong_provider_coverage.has_value() &&
              !wrong_provider_result &&
              wrong_provider_result.error() == gtosd::HuPreflopError::IntegrityFailure &&
              wrong_provider_coverage.value().validated_boundary_count == 0U &&
              wrong_provider_coverage.value().fingerprint == wrong_provider_initial_fingerprint,
          "streaming rejects an incomplete provider response without mutation");

  auto rejected_checkpoint_coverage =
      gtosd::make_hu_preflop_whole_game_coverage_accumulator(tree, plan.value(), 0.01);
  const auto rejected_checkpoint_initial_fingerprint =
      rejected_checkpoint_coverage.value().fingerprint;
  const auto rejected_checkpoint_result = gtosd::stream_hu_preflop_whole_game_boundaries(
      tree, plan.value(), rejected_checkpoint_coverage.value(),
      [&](const gtosd::HuPreflopWholeGameBoundaryRequest &) {
        std::vector<gtosd::HuPreflopFlopBoundary> complete_task{first_task_side_one.value(),
                                                                first_task_side_zero.value()};
        return gtosd::Result<std::vector<gtosd::HuPreflopFlopBoundary>,
                             gtosd::HuPreflopError>::success(std::move(complete_task));
      },
      [](const gtosd::HuPreflopWholeGameCoverageAccumulator &) {
        return gtosd::Result<bool, gtosd::HuPreflopError>::failure(
            gtosd::HuPreflopError::IoFailure);
      });
  require(rejected_checkpoint_coverage.has_value() && !rejected_checkpoint_result &&
              rejected_checkpoint_result.error() == gtosd::HuPreflopError::IoFailure &&
              rejected_checkpoint_coverage.value().validated_boundary_count == 0U &&
              rejected_checkpoint_coverage.value().fingerprint ==
                  rejected_checkpoint_initial_fingerprint,
          "a failed checkpoint prevents the candidate task from being committed");

  auto resumed_coverage =
      gtosd::make_hu_preflop_whole_game_coverage_accumulator(tree, plan.value(), 0.01);
  require(resumed_coverage.has_value() &&
              gtosd::accumulate_hu_preflop_whole_game_boundary(
                  tree, plan.value(), resumed_coverage.value(), first_task_side_zero.value()),
          "streaming resume fixture starts with exactly one resolver side");
  std::uint64_t provider_calls = 0U;
  std::uint64_t checkpoint_calls = 0U;
  bool resumed_missing_side_only = false;
  bool checkpoint_saw_complete_task = false;
  const auto resumed_result = gtosd::stream_hu_preflop_whole_game_boundaries(
      tree, plan.value(), resumed_coverage.value(),
      [&](const gtosd::HuPreflopWholeGameBoundaryRequest &request)
          -> gtosd::Result<std::vector<gtosd::HuPreflopFlopBoundary>, gtosd::HuPreflopError> {
        ++provider_calls;
        if (provider_calls == 1U) {
          resumed_missing_side_only =
              request.task_index == 0U && request.missing_resolver_mask == 0x2U;
          std::vector<gtosd::HuPreflopFlopBoundary> missing_side;
          missing_side.push_back(first_task_side_one.value());
          return gtosd::Result<std::vector<gtosd::HuPreflopFlopBoundary>,
                               gtosd::HuPreflopError>::success(std::move(missing_side));
        }
        return gtosd::Result<std::vector<gtosd::HuPreflopFlopBoundary>,
                             gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::GameFailure);
      },
      [&](const gtosd::HuPreflopWholeGameCoverageAccumulator &candidate) {
        ++checkpoint_calls;
        checkpoint_saw_complete_task = candidate.task_side_coverage.front() == 0x3U &&
                                       candidate.validated_boundary_count == 2U;
        return gtosd::Result<bool, gtosd::HuPreflopError>::success(true);
      });
  require(!resumed_result && resumed_result.error() == gtosd::HuPreflopError::GameFailure &&
              provider_calls == 2U && checkpoint_calls == 1U && resumed_missing_side_only &&
              checkpoint_saw_complete_task &&
              resumed_coverage.value().task_side_coverage.front() == 0x3U &&
              resumed_coverage.value().validated_boundary_count == 2U &&
              resumed_coverage.value().fully_covered_task_count == 1U,
          "streaming resumes only the missing side and retains the last checkpointed task after a "
          "later provider failure");

  const auto opposite_boundary = gtosd::build_hu_preflop_flop_boundary(
      tree, plan.value(), conditioned.value(), 1U, cfvs, "continuation:test:v1", 100U);
  require(opposite_boundary.has_value() &&
              gtosd::accumulate_hu_preflop_whole_game_boundary(tree, plan.value(), coverage.value(),
                                                               opposite_boundary.value()) &&
              coverage.value().validated_boundary_count == 2U &&
              coverage.value().fully_covered_task_count == 1U &&
              coverage.value().blueprint_iterations == 100U &&
              gtosd::validate_hu_preflop_whole_game_coverage_accumulator(tree, plan.value(),
                                                                         coverage.value()),
          "second resolver boundary completes exactly one canonical Flop task");

  auto distinct_local_identity_coverage =
      gtosd::make_hu_preflop_whole_game_coverage_accumulator(tree, plan.value(), 0.01);
  const auto second_task_conditioned = gtosd::condition_hu_preflop_ranges_on_flop(
      plan.value(), tasks.value()[1U].entry_node, tasks.value()[1U].flop);
  std::array<gtosd::Result<gtosd::HuPreflopFlopBoundary, gtosd::HuPreflopError>, 4U>
      distinct_local_boundaries{
          gtosd::build_hu_preflop_flop_boundary(tree, plan.value(), first_task_conditioned.value(),
                                                0U, cfvs, "continuation:local-task-0:v1", 100U,
                                                "continuation:shared-checkpoint:v1"),
          gtosd::build_hu_preflop_flop_boundary(tree, plan.value(), first_task_conditioned.value(),
                                                1U, cfvs, "continuation:local-task-0:v1", 100U,
                                                "continuation:shared-checkpoint:v1"),
          gtosd::build_hu_preflop_flop_boundary(tree, plan.value(), second_task_conditioned.value(),
                                                0U, cfvs, "continuation:local-task-1:v1", 100U,
                                                "continuation:shared-checkpoint:v1"),
          gtosd::build_hu_preflop_flop_boundary(tree, plan.value(), second_task_conditioned.value(),
                                                1U, cfvs, "continuation:local-task-1:v1", 100U,
                                                "continuation:shared-checkpoint:v1")};
  bool distinct_local_identities_accepted =
      distinct_local_identity_coverage.has_value() && second_task_conditioned.has_value();
  for (const auto &candidate : distinct_local_boundaries) {
    distinct_local_identities_accepted =
        distinct_local_identities_accepted && candidate.has_value() &&
        gtosd::accumulate_hu_preflop_whole_game_boundary(
            tree, plan.value(), distinct_local_identity_coverage.value(), candidate.value())
            .has_value();
  }
  require(distinct_local_identities_accepted &&
              distinct_local_identity_coverage.value().fully_covered_task_count == 2U &&
              distinct_local_identity_coverage.value().continuation_checkpoint_fingerprint ==
                  "continuation:shared-checkpoint:v1" &&
              distinct_local_boundaries[0].value().continuation_fingerprint !=
                  distinct_local_boundaries[2].value().continuation_fingerprint,
          "whole-game coverage accepts task-local identities only under one shared checkpoint");
  const auto completed_task = std::ranges::find_if(tasks.value(), [&](const auto &task) {
    return task.entry_node == entry_node && task.flop == boundary.value().flop;
  });
  require(completed_task != tasks.value().end() &&
              std::abs(coverage.value().covered_postflop_probability -
                       completed_task->joint_probability) <= 1.0e-12,
          "streaming coverage adds a task probability only after both resolver sides exist");
  const auto completed_task_index =
      static_cast<std::size_t>(std::distance(tasks.value().begin(), completed_task));
  gtosd::HuPreflopComboReach unit_cfvs{};
  unit_cfvs.fill(1.0);
  auto unit_coverage =
      gtosd::make_hu_preflop_whole_game_coverage_accumulator(tree, plan.value(), 0.01);
  const auto unit_boundary = gtosd::build_hu_preflop_flop_boundary(
      tree, plan.value(), conditioned.value(), 0U, unit_cfvs, "continuation:unit-profile:v1", 100U);
  const auto opposite_unit_boundary = gtosd::build_hu_preflop_flop_boundary(
      tree, plan.value(), conditioned.value(), 1U, unit_cfvs, "continuation:unit-profile:v1", 100U);
  require(
      unit_coverage.has_value() && unit_boundary.has_value() &&
          opposite_unit_boundary.has_value() &&
          gtosd::accumulate_hu_preflop_whole_game_boundary(
              tree, plan.value(), unit_coverage.value(), unit_boundary.value()) &&
          gtosd::accumulate_hu_preflop_whole_game_boundary(
              tree, plan.value(), unit_coverage.value(), opposite_unit_boundary.value()) &&
          std::abs(
              unit_coverage.value().task_side_profile_utility_antes[completed_task_index * 2U] -
              completed_task->joint_probability) <= 1.0e-12 &&
          std::abs(unit_coverage.value()
                       .task_side_profile_utility_antes[completed_task_index * 2U + 1U] -
                   completed_task->joint_probability) <= 1.0e-12,
      "a unit continuation value reconstructs the exact joint probability on both resolver sides");
  auto reverse_coverage =
      gtosd::make_hu_preflop_whole_game_coverage_accumulator(tree, plan.value(), 0.01);
  require(reverse_coverage.has_value() &&
              gtosd::accumulate_hu_preflop_whole_game_boundary(
                  tree, plan.value(), reverse_coverage.value(), opposite_boundary.value()) &&
              gtosd::accumulate_hu_preflop_whole_game_boundary(
                  tree, plan.value(), reverse_coverage.value(), boundary.value()) &&
              gtosd::fingerprint_hu_preflop_continuation_profile(reverse_coverage.value()) ==
                  gtosd::fingerprint_hu_preflop_continuation_profile(coverage.value()) &&
              reverse_coverage.value().task_side_profile_utility_antes ==
                  coverage.value().task_side_profile_utility_antes,
          "continuation identity and profile utility are stable across boundary arrival order");
  const auto streamed_incomplete = gtosd::finalize_hu_preflop_whole_game_certification(
      tree, plan.value(), coverage.value(), nullptr);
  require(streamed_incomplete.has_value() && !streamed_incomplete.value().certified &&
              streamed_incomplete.value().validated_boundary_count == 2U &&
              streamed_incomplete.value().fully_covered_task_count == 1U &&
              streamed_incomplete.value().status == "INCOMPLETE_BOUNDARY_COVERAGE",
          "streaming finalizer preserves the whole-game certification gate");

  const auto incomplete =
      gtosd::certify_hu_preflop_whole_game(tree, plan.value(), {boundary.value()}, nullptr, 0.01);
  require(incomplete.has_value() && !incomplete.value().certified &&
              incomplete.value().expected_boundary_count == 10'314U &&
              incomplete.value().validated_boundary_count == 1U &&
              incomplete.value().status == "INCOMPLETE_BOUNDARY_COVERAGE",
          "whole-game certification reports exact missing boundary coverage");

  gtosd::HuPreflopGlobalBestResponseEvidence sampled_br;
  sampled_br.tree_fingerprint = tree.fingerprint;
  sampled_br.blueprint_fingerprint = plan.value().blueprint_fingerprint;
  auto one_side_coverage =
      gtosd::make_hu_preflop_whole_game_coverage_accumulator(tree, plan.value(), 0.01);
  require(one_side_coverage.has_value() &&
              gtosd::accumulate_hu_preflop_whole_game_boundary(
                  tree, plan.value(), one_side_coverage.value(), boundary.value()),
          "one-side continuation profile derives for global BR identity test");
  sampled_br.continuation_profile_fingerprint =
      gtosd::fingerprint_hu_preflop_continuation_profile(one_side_coverage.value());
  sampled_br.method = "sampled_response_lower_bound";
  sampled_br.exact_best_response = false;
  sampled_br.fingerprint = gtosd::fingerprint_hu_preflop_global_best_response(sampled_br);
  const auto still_incomplete = gtosd::certify_hu_preflop_whole_game(
      tree, plan.value(), {boundary.value()}, &sampled_br, 0.01);
  require(still_incomplete.has_value() && !still_incomplete.value().certified &&
              still_incomplete.value().global_best_response_present &&
              !still_incomplete.value().global_best_response_exact &&
              still_incomplete.value().status == "INCOMPLETE_BOUNDARY_COVERAGE",
          "sampled response evidence is recorded but cannot bypass missing boundaries");
  auto corrupted = boundary.value();
  corrupted.values.pop_back();
  corrupted.fingerprint = "fnv1a64:corrupt";
  require(!gtosd::validate_hu_preflop_flop_boundary(tree, plan.value(), corrupted),
          "incomplete or fingerprint-mismatched Flop boundary is rejected");
  auto wrong_reach = boundary.value();
  wrong_reach.values.front().counterfactual_reach *= 2.0;
  wrong_reach.fingerprint = "fnv1a64:tampered-reach";
  require(!gtosd::validate_hu_preflop_flop_boundary(tree, plan.value(), wrong_reach),
          "a boundary with altered counterfactual reach is rejected");

  const auto blueprint_bytes = gtosd::serialize_hu_preflop_blueprint(blueprint.value());
  const auto plan_bytes = gtosd::serialize_hu_preflop_decomposition_plan(plan.value());
  const auto boundary_bytes = gtosd::serialize_hu_preflop_flop_boundary(boundary.value());
  auto legacy_boundary_bytes = boundary_bytes.value();
  legacy_boundary_bytes.replace(legacy_boundary_bytes.find("hu_preflop_flop_boundary.v2"),
                                std::string("hu_preflop_flop_boundary.v2").size(),
                                "hu_preflop_flop_boundary.v1");
  const auto coverage_bytes =
      gtosd::serialize_hu_preflop_whole_game_coverage_accumulator(coverage.value());
  auto legacy_coverage_bytes = coverage_bytes.value();
  const auto legacy_coverage_minor = legacy_coverage_bytes.find("\"minor\":3");
  require(legacy_coverage_minor != std::string::npos,
          "coverage payload exposes its current minor version");
  legacy_coverage_bytes.replace(legacy_coverage_minor, std::string("\"minor\":3").size(),
                                "\"minor\":2");
  require(blueprint_bytes.has_value() && plan_bytes.has_value() && boundary_bytes.has_value() &&
              coverage_bytes.has_value(),
          "blueprint, decomposition plan, boundary and coverage checkpoint serialize");
  const auto restored_blueprint = gtosd::deserialize_hu_preflop_blueprint(blueprint_bytes.value());
  const auto restored_plan = gtosd::deserialize_hu_preflop_decomposition_plan(plan_bytes.value());
  const auto restored_boundary =
      gtosd::deserialize_hu_preflop_flop_boundary(boundary_bytes.value());
  const auto restored_coverage =
      gtosd::deserialize_hu_preflop_whole_game_coverage_accumulator(coverage_bytes.value());
  require(restored_blueprint.has_value() && restored_blueprint.value() == blueprint.value() &&
              restored_plan.has_value() &&
              restored_plan.value().fingerprint == plan.value().fingerprint &&
              restored_boundary.has_value() &&
              restored_boundary.value().fingerprint == boundary.value().fingerprint &&
              restored_boundary.value().continuation_checkpoint_fingerprint ==
                  boundary.value().continuation_checkpoint_fingerprint &&
              restored_coverage.has_value() &&
              restored_coverage.value().fingerprint == coverage.value().fingerprint &&
              restored_coverage.value().blueprint_iterations ==
                  coverage.value().blueprint_iterations &&
              restored_coverage.value().continuation_checkpoint_fingerprint ==
                  coverage.value().continuation_checkpoint_fingerprint &&
              restored_coverage.value().task_side_profile_utility_antes ==
                  coverage.value().task_side_profile_utility_antes &&
              restored_coverage.value().task_local_continuation_identity_hashes ==
                  coverage.value().task_local_continuation_identity_hashes &&
              restored_coverage.value().profile_utility_state_hash ==
                  coverage.value().profile_utility_state_hash &&
              restored_coverage.value().local_continuation_identity_state_hash ==
                  coverage.value().local_continuation_identity_state_hash &&
              gtosd::validate_hu_preflop_whole_game_coverage_accumulator(tree, plan.value(),
                                                                         restored_coverage.value()),
          "all HU preflop decomposition artifacts round-trip with stable identity");
  require(!gtosd::deserialize_hu_preflop_flop_boundary(boundary_bytes.value() + "corrupt"),
          "boundary parser rejects trailing corruption");
  require(!gtosd::deserialize_hu_preflop_flop_boundary(legacy_boundary_bytes),
          "boundary parser rejects the legacy payload without checkpoint identity");
  require(!gtosd::deserialize_hu_preflop_whole_game_coverage_accumulator(coverage_bytes.value() +
                                                                         "corrupt"),
          "coverage checkpoint parser rejects trailing corruption");
  require(!gtosd::deserialize_hu_preflop_whole_game_coverage_accumulator(legacy_coverage_bytes),
          "coverage checkpoint rejects the legacy payload without task-local identity");

  const auto base = std::filesystem::current_path();
  const auto blueprint_path = base / "hu_preflop_blueprint_test.bin";
  const auto plan_path = base / "hu_preflop_plan_test.bin";
  const auto boundary_path = base / "hu_preflop_boundary_test.bin";
  const auto coverage_path = base / "hu_preflop_coverage_test.bin";
  std::error_code ignored;
  std::filesystem::remove(blueprint_path, ignored);
  std::filesystem::remove(plan_path, ignored);
  std::filesystem::remove(boundary_path, ignored);
  std::filesystem::remove(coverage_path, ignored);
  require(gtosd::save_hu_preflop_blueprint(blueprint.value(), blueprint_path.string()) &&
              gtosd::save_hu_preflop_decomposition_plan(plan.value(), plan_path.string()) &&
              gtosd::save_hu_preflop_flop_boundary(boundary.value(), boundary_path.string()) &&
              gtosd::save_hu_preflop_whole_game_coverage_accumulator(coverage.value(),
                                                                     coverage_path.string()),
          "HU preflop decomposition artifacts save atomically with checksums");
  require(gtosd::load_hu_preflop_blueprint(blueprint_path.string()) &&
              gtosd::load_hu_preflop_decomposition_plan(plan_path.string()) &&
              gtosd::load_hu_preflop_flop_boundary(boundary_path.string()) &&
              gtosd::load_hu_preflop_whole_game_coverage_accumulator(coverage_path.string()),
          "checksummed HU preflop decomposition artifacts reload");
  {
    std::ofstream corrupt_file(boundary_path, std::ios::binary | std::ios::trunc);
    corrupt_file << "GTOSD_HU_PREFLOP_BOUNDARY_FILE 4 deadbeefdeadbeef\nBAD!";
  }
  require(!gtosd::load_hu_preflop_flop_boundary(boundary_path.string()),
          "a corrupted persisted boundary fails closed");
  std::filesystem::remove(blueprint_path, ignored);
  std::filesystem::remove(plan_path, ignored);
  std::filesystem::remove(boundary_path, ignored);
  std::filesystem::remove(coverage_path, ignored);

  const auto postflop = gtosd::make_hu_preflop_postflop_config(tree, entry_node, flop);
  require(
      postflop.has_value() && postflop.value().initial_pot == tree.nodes[entry_node].state.pot &&
          postflop.value().effective_stack == tree.nodes[entry_node].state.remaining_stacks[0] &&
          gtosd::validate_tree_config(postflop.value()).has_value(),
      "preflop entry maps to a valid exact postflop configuration");
}

void test_decomposition_preserves_zero_reach_by_private_class() {
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config()).value();
  auto blueprint = gtosd::make_uniform_hu_preflop_blueprint(tree).value();
  auto &root = *std::ranges::find(blueprint.decisions, tree.root,
                                  &gtosd::HuPreflopBlueprintDecision::node_id);
  const auto fold = static_cast<std::size_t>(
      std::distance(tree.nodes[tree.root].edges.begin(),
                    std::ranges::find_if(tree.nodes[tree.root].edges, [](const auto &edge) {
                      return edge.action.type == gtosd::ActionType::Fold;
                    })));
  root.strategy[0].fill(0.0);
  root.strategy[0][fold] = 1.0;
  blueprint.fingerprint = gtosd::fingerprint_hu_preflop_blueprint(blueprint);
  const auto plan = gtosd::derive_hu_preflop_decomposition_plan(tree, blueprint);
  require(plan.has_value(), "class-selective blueprint decomposition derives");
  const auto combos = gtosd::all_combos();
  for (const auto &entry : plan.value().entries) {
    for (std::size_t combo = 0U; combo < combos.size(); ++combo) {
      if (gtosd::hand_class(combos[combo]) == 0U) {
        require(entry.own_sequence_reach[0][combo] == 0.0,
                "a folded private class remains explicit zero reach at every Flop entry");
      }
    }
  }
}

void test_whole_game_streaming_coverage_closes_every_public_root() {
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config()).value();
  const auto blueprint = gtosd::make_uniform_hu_preflop_blueprint(tree).value();
  const auto plan = gtosd::derive_hu_preflop_decomposition_plan(tree, blueprint).value();
  const auto tasks = gtosd::enumerate_hu_preflop_canonical_flop_tasks(plan).value();
  auto coverage = gtosd::make_hu_preflop_whole_game_coverage_accumulator(tree, plan, 0.01).value();
  const auto combos = gtosd::all_combos();
  const auto chance_denominator =
      static_cast<double>(plan.physical_deal_flop_histories / plan.entries.size());

  bool accumulated_all = true;
  for (const auto &task : tasks) {
    const auto entry = std::ranges::find(plan.entries, task.entry_node,
                                         &gtosd::HuPreflopPostflopEntryReach::entry_node);
    if (entry == plan.entries.end()) {
      accumulated_all = false;
      break;
    }
    const auto board_mask = task.flop[0].mask() | task.flop[1].mask() | task.flop[2].mask();
    for (std::uint8_t resolver = 0U; resolver < 2U; ++resolver) {
      std::array<double, 36U> resolving_mass_by_card{};
      double resolving_total = 0.0;
      for (std::size_t combo = 0U; combo < combos.size(); ++combo) {
        const auto combo_mask = combos[combo].first.mask() | combos[combo].second.mask();
        if ((combo_mask & board_mask) != 0U) {
          continue;
        }
        const auto reach = entry->own_sequence_reach[resolver][combo];
        resolving_total += reach;
        resolving_mass_by_card[combos[combo].first.value()] += reach;
        resolving_mass_by_card[combos[combo].second.value()] += reach;
      }
      gtosd::HuPreflopFlopBoundary boundary;
      boundary.tree_fingerprint = tree.fingerprint;
      boundary.blueprint_fingerprint = plan.blueprint_fingerprint;
      boundary.continuation_fingerprint = "continuation:whole-game-coverage-test:v1";
      boundary.continuation_checkpoint_fingerprint = "continuation:whole-game-coverage-test:v1";
      boundary.blueprint_iterations = 1U;
      boundary.entry_node = task.entry_node;
      boundary.flop = task.flop;
      boundary.resolving_player = resolver;
      boundary.opponent = static_cast<std::uint8_t>(1U - resolver);
      boundary.values.reserve(528U);
      for (std::size_t combo = 0U; combo < combos.size(); ++combo) {
        const auto combo_mask = combos[combo].first.mask() | combos[combo].second.mask();
        if ((combo_mask & board_mask) != 0U) {
          continue;
        }
        const auto counterfactual_reach =
            (resolving_total - resolving_mass_by_card[combos[combo].first.value()] -
             resolving_mass_by_card[combos[combo].second.value()] +
             entry->own_sequence_reach[resolver][combo]) /
            chance_denominator;
        boundary.values.push_back({static_cast<gtosd::ComboId>(combo), counterfactual_reach, 0.0,
                                   counterfactual_reach > 0.0});
      }
      boundary.fingerprint = gtosd::fingerprint_hu_preflop_flop_boundary(boundary);
      if (!gtosd::accumulate_hu_preflop_whole_game_boundary(tree, plan, coverage, boundary)) {
        accumulated_all = false;
        break;
      }
    }
    if (!accumulated_all) {
      break;
    }
  }
  require(accumulated_all && coverage.validated_boundary_count == 10'314U &&
              coverage.fully_covered_task_count == 5'157U &&
              std::abs(coverage.covered_postflop_probability - plan.postflop_entry_probability) <=
                  1.0e-12 &&
              gtosd::validate_hu_preflop_whole_game_coverage_accumulator(tree, plan, coverage),
          "streaming ledger covers all 5,157 canonical Flop tasks and both resolver sides");

  const auto without_br =
      gtosd::finalize_hu_preflop_whole_game_certification(tree, plan, coverage, nullptr);
  require(without_br.has_value() && without_br.value().boundary_coverage_complete &&
              !without_br.value().certified &&
              without_br.value().status == "GLOBAL_BEST_RESPONSE_MISSING",
          "complete public-root coverage cannot certify without global best response evidence");

  gtosd::HuPreflopGlobalBestResponseEvidence sampled_br;
  sampled_br.tree_fingerprint = tree.fingerprint;
  sampled_br.blueprint_fingerprint = plan.blueprint_fingerprint;
  sampled_br.continuation_profile_fingerprint =
      gtosd::fingerprint_hu_preflop_continuation_profile(coverage);
  sampled_br.method = "sampled_response_test_evidence";
  sampled_br.exact_best_response = false;
  sampled_br.fingerprint = gtosd::fingerprint_hu_preflop_global_best_response(sampled_br);
  const auto sampled =
      gtosd::finalize_hu_preflop_whole_game_certification(tree, plan, coverage, &sampled_br);
  require(sampled.has_value() && sampled.value().boundary_coverage_complete &&
              !sampled.value().certified &&
              sampled.value().status == "GLOBAL_BEST_RESPONSE_NOT_EXACT",
          "sampled best response cannot certify even with complete public-root coverage");
  auto exact_br = sampled_br;
  exact_br.method = "exact_best_response_test_oracle";
  exact_br.exact_best_response = true;
  exact_br.profile_evaluation_fingerprint = "fnv1a64:unit_exact_profile_evaluation";
  exact_br.blueprint_iterations = 1U;
  exact_br.chance_outcome_count = plan.physical_private_deals * 4'027'520U;
  exact_br.best_response_values_antes = {0.4, 0.4};
  exact_br.profile_values_antes = {0.0, 0.0};
  exact_br.deviation_gains_antes = {0.4, 0.4};
  exact_br.nashconv_antes = 0.8;
  exact_br.normalized_nashconv = 0.02;
  exact_br.fingerprint = gtosd::fingerprint_hu_preflop_global_best_response(exact_br);
  const auto above_target =
      gtosd::finalize_hu_preflop_whole_game_certification(tree, plan, coverage, &exact_br);
  require(above_target.has_value() && !above_target.value().certified &&
              above_target.value().status == "NASHCONV_TARGET_NOT_MET",
          "exact best response above the declared NashConv target cannot certify");
  exact_br.best_response_values_antes = {0.1, 0.1};
  exact_br.deviation_gains_antes = {0.1, 0.1};
  exact_br.nashconv_antes = 0.2;
  exact_br.normalized_nashconv = 0.005;
  exact_br.fingerprint = gtosd::fingerprint_hu_preflop_global_best_response(exact_br);
  const auto below_target =
      gtosd::finalize_hu_preflop_whole_game_certification(tree, plan, coverage, &exact_br);
  require(below_target.has_value() && below_target.value().certified &&
              below_target.value().status == "CERTIFIED",
          "complete coverage and matching exact BR below target close the certification gate");
  auto wrong_profile_br = sampled_br;
  wrong_profile_br.continuation_profile_fingerprint = "fnv1a64:different-continuations";
  wrong_profile_br.fingerprint =
      gtosd::fingerprint_hu_preflop_global_best_response(wrong_profile_br);
  require(
      !gtosd::finalize_hu_preflop_whole_game_certification(tree, plan, coverage, &wrong_profile_br),
      "global best response evidence for different continuations is rejected");
}

void test_nested_public_boundary_shapes_preserve_history() {
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config()).value();
  const auto turn = gtosd::enumerate_hu_postflop_betting_root_shapes(tree, gtosd::Street::Turn);
  const auto river = gtosd::enumerate_hu_postflop_betting_root_shapes(tree, gtosd::Street::River);
  const auto terminals = gtosd::enumerate_hu_postflop_upper_street_terminal_shapes(tree);
  require(turn.has_value() && river.has_value() && !turn.value().empty() &&
              river.value().size() > turn.value().size() && terminals.has_value() &&
              !terminals.value().empty(),
          "Turn, River and upper-street terminal histories enumerate");
  std::set<std::pair<std::uint32_t, std::string>> identities;
  for (const auto &root : river.value()) {
    const auto replayed = gtosd::replay_hu_postflop_betting_root_shape(tree, root);
    require(root.street == gtosd::Street::River && root.state.street == gtosd::Street::River &&
                root.state.status == gtosd::HandStatus::InProgress && root.state.board_mask == 0U &&
                !root.action_history.empty() && !root.history_fingerprint.empty() &&
                replayed.has_value() && replayed.value() == root.state,
            "River shape carries a replayable action history without inventing cards");
    require(identities.emplace(root.entry_node, root.history_fingerprint).second,
            "distinct public betting histories keep distinct River boundary identities");
  }
  auto tampered_shape = river.value().front();
  tampered_shape.action_history.pop_back();
  require(!gtosd::replay_hu_postflop_betting_root_shape(tree, tampered_shape),
          "replay rejects a betting-history path that no longer reaches its public root");
  std::array<std::uint64_t, 2> terminal_count_by_street{};
  std::array<std::uint64_t, 2> terminal_count_by_status{};
  std::set<std::pair<std::uint32_t, std::string>> terminal_identities;
  for (const auto &terminal : terminals.value()) {
    const auto replayed = gtosd::replay_hu_postflop_upper_street_terminal_shape(tree, terminal);
    require(
        replayed.has_value() && replayed.value() == terminal.state &&
            terminal_identities.emplace(terminal.entry_node, terminal.history_fingerprint).second,
        "every upper-street terminal has one replayable stable identity");
    ++terminal_count_by_street[terminal.street == gtosd::Street::Flop ? 0U : 1U];
    ++terminal_count_by_status[terminal.state.status == gtosd::HandStatus::Folded ? 0U : 1U];
  }
  require(
      std::ranges::all_of(terminal_count_by_street, [](const auto count) { return count > 0U; }) &&
          std::ranges::all_of(terminal_count_by_status,
                              [](const auto count) { return count > 0U; }),
      "terminal manifest covers Flop, Turn, folds and all-in runouts");
  auto tampered_terminal = terminals.value().front();
  tampered_terminal.action_history.pop_back();
  require(!gtosd::replay_hu_postflop_upper_street_terminal_shape(tree, tampered_terminal),
          "terminal replay rejects a truncated action history");
  const auto blueprint = gtosd::make_uniform_hu_preflop_blueprint(tree).value();
  const auto plan = gtosd::derive_hu_preflop_decomposition_plan(tree, blueprint).value();
  const auto work = gtosd::estimate_hu_preflop_river_work(tree, plan);
  require(work.has_value() && work.value().physical_ordered_runouts_per_flop == 1'056U &&
              work.value().physical_public_board_histories == 7'539'840U &&
              work.value().canonical_public_board_histories == 369'072U &&
              work.value().minimum_canonical_runouts_per_flop == 291U &&
              work.value().maximum_canonical_runouts_per_flop == 1'056U &&
              work.value().river_betting_histories == river.value().size() &&
              work.value().physical_resolver_roots == 6'582'280'320U &&
              work.value().canonical_resolver_roots == 322'199'856U &&
              work.value().canonical_roots_for_both_players == 644'399'712U &&
              work.value().boundary_values_per_resolver_root == 528U &&
              work.value().boundary_bytes_per_resolver_root == 12'672U &&
              work.value().fully_materialized_boundary_bytes == 8'165'833'150'464ULL &&
              work.value().maximum_best_response_turn_components_per_root == 3U &&
              work.value().best_response_boundary_bytes_per_resolver_root == 50'688U &&
              work.value().fully_materialized_best_response_boundary_bytes ==
                  32'663'332'601'856ULL &&
              work.value().maximum_canonical_river_roots_per_physical_turn_leaf == 32U &&
              work.value().best_response_leaf_accumulator_value_bytes == 20'160U &&
              work.value().maximum_best_response_leaf_manifest_bytes == 256U &&
              work.value().maximum_best_response_leaf_live_payload_bytes == 71'104U &&
              work.value().best_response_task_value_bytes == 21'120U &&
              work.value().best_response_entry_accumulator_value_bytes == 2'592U &&
              work.value().best_response_entry_reduction_live_payload_bytes == 23'712U &&
              work.value().all_in_canonical_complete_boards == 19'998U &&
              work.value().all_in_board_catalog_payload_bytes == 239'976U &&
              work.value().all_in_matchup_table_payload_bytes == 157'464U &&
              work.value().all_in_one_board_scratch_payload_bytes == 328'728U &&
              work.value().all_in_streaming_live_payload_bytes == 726'168U &&
              !work.value().fingerprint.empty(),
          "work estimator separates River, entry and exact all-in BR payloads");
}

void test_upper_terminal_contributions_and_flop_assembler() {
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config()).value();
  const auto blueprint = gtosd::make_uniform_hu_preflop_blueprint(tree).value();
  const auto plan = gtosd::derive_hu_preflop_decomposition_plan(tree, blueprint).value();
  const auto terminals = gtosd::enumerate_hu_postflop_upper_street_terminal_shapes(tree).value();
  const auto &entry = plan.entries.front();
  const auto &flop_entry = plan.canonical_flop_catalog.front();
  const auto flop_mask =
      flop_entry.cards[0].mask() | flop_entry.cards[1].mask() | flop_entry.cards[2].mask();
  const auto combos = gtosd::all_combos();
  std::array<gtosd::ComboId, 2> selected{};
  bool selected_pair = false;
  for (std::size_t first = 0U; first < combos.size() && !selected_pair; ++first) {
    const auto first_mask = combos[first].first.mask() | combos[first].second.mask();
    if ((first_mask & flop_mask) != 0U) {
      continue;
    }
    for (std::size_t second = 0U; second < combos.size(); ++second) {
      const auto second_mask = combos[second].first.mask() | combos[second].second.mask();
      if ((second_mask & flop_mask) == 0U && (first_mask & second_mask) == 0U) {
        selected = {static_cast<gtosd::ComboId>(first), static_cast<gtosd::ComboId>(second)};
        selected_pair = true;
        break;
      }
    }
  }
  require(selected_pair, "terminal oracle finds one compatible physical private deal");

  std::vector<const gtosd::HuPostflopUpperStreetTerminalShape *> entry_terminals;
  std::array<std::uint64_t, 4> terminal_ordinals{
      std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint64_t>::max(),
      std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint64_t>::max()};
  for (const auto &terminal : terminals) {
    if (terminal.entry_node != entry.entry_node) {
      continue;
    }
    const auto ordinal = static_cast<std::uint64_t>(entry_terminals.size());
    entry_terminals.push_back(&terminal);
    const auto street_offset = terminal.street == gtosd::Street::Flop ? 0U : 2U;
    const auto status_offset = terminal.state.status == gtosd::HandStatus::Folded ? 0U : 1U;
    auto &slot = terminal_ordinals[street_offset + status_offset];
    if (slot == std::numeric_limits<std::uint64_t>::max()) {
      slot = ordinal;
    }
  }
  require(std::ranges::all_of(terminal_ordinals,
                              [](const auto ordinal) {
                                return ordinal != std::numeric_limits<std::uint64_t>::max();
                              }),
          "one entry exposes fold and all-in terminals on both Flop and Turn");

  const auto uniform_sampled_policy = make_uniform_sampled_postflop_policy(tree, 11U);
  const auto sampled_terminal =
      gtosd::evaluate_hu_preflop_upper_street_terminal_contribution_from_sampled_policy(
          tree, plan, 0U, flop_entry.cards, terminal_ordinals[0U],
          *entry_terminals[terminal_ordinals[0U]], uniform_sampled_policy);
  auto corrupt_sampled_policy = uniform_sampled_policy;
  corrupt_sampled_policy.fingerprint = "fnv1a64:corrupt";
  require(sampled_terminal.has_value() &&
              sampled_terminal.value().blueprint_iterations == uniform_sampled_policy.iterations &&
              sampled_terminal.value().continuation_checkpoint_fingerprint ==
                  uniform_sampled_policy.fingerprint &&
              gtosd::validate_hu_preflop_upper_street_terminal_contribution(
                  tree, plan, sampled_terminal.value()) &&
              !gtosd::evaluate_hu_preflop_upper_street_terminal_contribution_from_sampled_policy(
                  tree, plan, 0U, flop_entry.cards, terminal_ordinals[0U],
                  *entry_terminals[terminal_ordinals[0U]], corrupt_sampled_policy),
          "sampled policy bridge binds an exact upper-street terminal to one validated snapshot");

  const gtosd::HuPostflopActionProbabilityProvider sparse_provider =
      [&](const gtosd::PublicState &state, const std::span<const gtosd::Action>,
          const gtosd::Action &, const gtosd::ComboId combo) {
        return gtosd::Result<double, gtosd::HuPreflopError>::success(
            combo == selected[state.player_to_act] ? 1.0 : 0.0);
      };
  bool standalone_terminal_validation_exercised = false;
  for (const auto ordinal : terminal_ordinals) {
    const auto contribution = gtosd::evaluate_hu_preflop_upper_street_terminal_contribution(
        tree, plan, 0U, flop_entry.cards, ordinal, *entry_terminals[ordinal], sparse_provider, 11U,
        "continuation:test:average:v1");
    const auto standalone_validation =
        standalone_terminal_validation_exercised || !contribution
            ? contribution.has_value()
            : gtosd::validate_hu_preflop_upper_street_terminal_contribution(tree, plan,
                                                                            contribution.value())
                  .has_value();
    standalone_terminal_validation_exercised = true;
    require(contribution.has_value() && standalone_validation,
            "exact terminal contribution evaluates and validates against its manifest slot");
    for (std::uint8_t resolver = 0U; resolver < 2U; ++resolver) {
      const auto opponent = static_cast<std::uint8_t>(1U - resolver);
      const auto expected = entry.own_sequence_reach[resolver][selected[resolver]] *
                            static_cast<double>(flop_entry.physical_outcome_count) * 812.0;
      const auto found =
          std::ranges::find(contribution.value().resolver_values[resolver], selected[opponent],
                            &gtosd::HuPreflopRiverTaskAggregateValue::opponent_combo);
      require(found != contribution.value().resolver_values[resolver].end() &&
                  std::abs(found->weighted_counterfactual_reach - expected) <=
                      1.0e-10 * std::max(1.0, std::abs(expected)) &&
                  std::isfinite(found->weighted_counterfactual_utility_antes),
              "terminal fold/all-in enumeration preserves 29x28 runouts per private deal");
    }
  }

  gtosd::HuPreflopRiverTaskAggregate empty_river;
  empty_river.tree_fingerprint = tree.fingerprint;
  empty_river.blueprint_fingerprint = plan.blueprint_fingerprint;
  empty_river.accumulator_fingerprint = "fnv1a64:synthetic-empty-river";
  empty_river.continuation_checkpoint_fingerprint = "continuation:test:average:v1";
  empty_river.blueprint_iterations = 11U;
  empty_river.task_span_index = 0U;
  empty_river.entry_node = entry.entry_node;
  empty_river.flop = flop_entry.cards;
  for (auto &resolver : empty_river.resolver_values) {
    for (std::size_t combo = 0U; combo < combos.size(); ++combo) {
      const auto mask = combos[combo].first.mask() | combos[combo].second.mask();
      if ((mask & flop_mask) == 0U) {
        resolver.push_back({static_cast<gtosd::ComboId>(combo), 0.0, 0.0, 0.0, false});
      }
    }
  }
  empty_river.fingerprint = gtosd::fingerprint_hu_preflop_river_task_aggregate(empty_river);
  require(gtosd::validate_hu_preflop_river_task_aggregate(empty_river).has_value(),
          "synthetic zero River aggregate is a valid assembler fixture");
  auto accumulator = gtosd::make_hu_preflop_flop_task_accumulator(tree, plan, empty_river).value();
  require(!accumulator.complete &&
              accumulator.expected_terminal_fingerprints.size() == entry_terminals.size() &&
              !gtosd::finalize_hu_preflop_flop_task_accumulator(tree, plan, accumulator),
          "Flop assembler refuses to finalize before every terminal manifest slot arrives");

  const gtosd::HuPostflopActionProbabilityProvider deterministic_fold_provider =
      [](const gtosd::PublicState &state, const std::span<const gtosd::Action>,
         const gtosd::Action &action, const gtosd::ComboId) {
        const auto selected = gtosd::amount_to_call(state, state.player_to_act).units() > 0
                                  ? gtosd::ActionType::Fold
                                  : gtosd::ActionType::AllIn;
        return gtosd::Result<double, gtosd::HuPreflopError>::success(action.type == selected ? 1.0
                                                                                             : 0.0);
      };
  const auto mismatched_terminal = gtosd::evaluate_hu_preflop_upper_street_terminal_contribution(
      tree, plan, 0U, flop_entry.cards, 0U, *entry_terminals[0U], deterministic_fold_provider, 11U,
      "continuation:test:different-checkpoint:v1");
  const auto before_mismatched_terminal = accumulator.fingerprint;
  require(mismatched_terminal.has_value() &&
              !gtosd::accumulate_hu_preflop_upper_street_terminal_contribution(
                  tree, plan, accumulator, mismatched_terminal.value()) &&
              accumulator.fingerprint == before_mismatched_terminal,
          "Flop assembler rejects a terminal from another checkpoint without mutation");
  if (entry_terminals.size() > 1U) {
    const auto original_fingerprint = accumulator.fingerprint;
    const auto out_of_order = gtosd::evaluate_hu_preflop_upper_street_terminal_contribution(
        tree, plan, 0U, flop_entry.cards, 1U, *entry_terminals[1U], deterministic_fold_provider,
        11U, "continuation:test:average:v1");
    require(out_of_order.has_value() &&
                !gtosd::accumulate_hu_preflop_upper_street_terminal_contribution(
                    tree, plan, accumulator, out_of_order.value()) &&
                accumulator.fingerprint == original_fingerprint,
            "Flop assembler rejects an out-of-order terminal without mutation");
  }
  std::array<double, 2> expected_cfv{std::numeric_limits<double>::quiet_NaN(),
                                     std::numeric_limits<double>::quiet_NaN()};
  std::uint64_t positive_terminal_count = 0U;
  for (std::uint64_t ordinal = 0U; ordinal < entry_terminals.size(); ++ordinal) {
    const auto contribution = gtosd::evaluate_hu_preflop_upper_street_terminal_contribution(
        tree, plan, 0U, flop_entry.cards, ordinal, *entry_terminals[ordinal],
        deterministic_fold_provider, 11U, "continuation:test:average:v1");
    require(contribution.has_value() &&
                gtosd::accumulate_hu_preflop_upper_street_terminal_contribution(
                    tree, plan, accumulator, contribution.value())
                    .has_value(),
            "Flop assembler consumes each terminal exactly once in manifest order");
    const auto positive =
        std::ranges::any_of(contribution.value().resolver_values[0],
                            [](const auto &value) { return value.positive_reach; });
    if (positive) {
      ++positive_terminal_count;
      for (std::uint8_t resolver = 0U; resolver < 2U; ++resolver) {
        const auto found =
            std::ranges::find_if(contribution.value().resolver_values[resolver],
                                 [](const auto &value) { return value.positive_reach; });
        require(found != contribution.value().resolver_values[resolver].end(),
                "deterministic terminal reaches both counterfactual resolver views");
        expected_cfv[resolver] = found->conditional_value_antes;
      }
    }
  }
  const auto boundaries = gtosd::finalize_hu_preflop_flop_task_accumulator(tree, plan, accumulator);
  require(accumulator.complete && boundaries.has_value() &&
              boundaries.value()[0].continuation_fingerprint ==
                  boundaries.value()[1].continuation_fingerprint &&
              boundaries.value()[0].continuation_checkpoint_fingerprint ==
                  "continuation:test:average:v1" &&
              boundaries.value()[1].continuation_checkpoint_fingerprint ==
                  boundaries.value()[0].continuation_checkpoint_fingerprint &&
              positive_terminal_count == 1U,
          "one deterministic fold path plus zero River mass emits both Flop boundary sides");
  for (std::uint8_t resolver = 0U; resolver < 2U; ++resolver) {
    require(std::ranges::all_of(boundaries.value()[resolver].values,
                                [&](const auto &value) {
                                  return value.positive_reach &&
                                         std::abs(value.blueprint_counterfactual_value_antes -
                                                  expected_cfv[resolver]) <= 1.0e-12;
                                }),
            "assembled Flop boundary carries the exact deterministic continuation CFV");
  }
}

void test_best_response_action_reducer() {
  const std::array flop{gtosd::parse_card("6c").value(), gtosd::parse_card("7d").value(),
                        gtosd::parse_card("8h").value()};
  const auto combos = gtosd::all_combos();
  const auto make_values = [&](const auto &reach_for, const auto &conditional_for) {
    std::vector<gtosd::HuPreflopBestResponseComboValue> values;
    values.reserve(528U);
    for (std::size_t combo = 0U; combo < combos.size(); ++combo) {
      const auto blocked = std::ranges::any_of(flop, [&](const auto card) {
        return combos[combo].first == card || combos[combo].second == card;
      });
      if (blocked) {
        continue;
      }
      const auto reach = reach_for(combo);
      const auto conditional = conditional_for(combo);
      values.push_back({static_cast<gtosd::ComboId>(combo), reach, reach * conditional,
                        reach > 0.0 ? conditional : 0.0, reach > 0.0});
    }
    return values;
  };

  std::array responder_children{
      make_values([](const auto) { return 2.0; },
                  [](const auto combo) { return combo % 2U == 0U ? 3.0 : 1.0; }),
      make_values([](const auto) { return 2.0; },
                  [](const auto combo) { return combo % 2U == 0U ? 1.0 : 3.0; })};
  std::ranges::reverse(responder_children[1]);
  const auto responder =
      gtosd::reduce_hu_preflop_best_response_action_values(responder_children, flop, true);
  require(responder.has_value() && responder.value().size() == 528U &&
              std::ranges::all_of(responder.value(),
                                  [](const auto &value) {
                                    return value.weighted_counterfactual_reach == 2.0 &&
                                           value.weighted_counterfactual_utility_antes == 6.0 &&
                                           value.conditional_value_antes == 3.0 &&
                                           value.positive_reach;
                                  }),
          "BR responder reduction takes the per-combo maximum independent of row order");

  std::array opponent_children{
      make_values([](const auto) { return 0.25; },
                  [](const auto combo) { return combo % 2U == 0U ? 4.0 : -2.0; }),
      make_values([](const auto) { return 0.75; }, [](const auto) { return 0.0; })};
  std::ranges::reverse(opponent_children[1]);
  const auto opponent =
      gtosd::reduce_hu_preflop_best_response_action_values(opponent_children, flop, false);
  const auto opponent_is_exact =
      opponent.has_value() && opponent.value().size() == 528U &&
      std::ranges::all_of(opponent.value(), [](const auto &value) {
        const auto expected = value.responding_combo % 2U == 0U ? 1.0 : -0.5;
        return value.weighted_counterfactual_reach == 1.0 &&
               value.weighted_counterfactual_utility_antes == expected &&
               value.conditional_value_antes == expected && value.positive_reach;
      });
  require(opponent_is_exact,
          "BR opponent reduction sums action-weighted reach and utility per combo");

  auto inconsistent_reach = responder_children;
  inconsistent_reach[1].front().weighted_counterfactual_reach = 2.25;
  inconsistent_reach[1].front().weighted_counterfactual_utility_antes =
      2.25 * inconsistent_reach[1].front().conditional_value_antes;
  require(!gtosd::reduce_hu_preflop_best_response_action_values(inconsistent_reach, flop, true),
          "BR responder reduction rejects action-dependent counterfactual reach");

  auto duplicate_combo = responder_children;
  duplicate_combo[0][1].responding_combo = duplicate_combo[0][0].responding_combo;
  auto nonfinite = responder_children;
  nonfinite[0][0].weighted_counterfactual_utility_antes = std::numeric_limits<double>::infinity();
  const std::span<const std::vector<gtosd::HuPreflopBestResponseComboValue>> no_children;
  const std::array invalid_flop{flop[0], flop[0], flop[2]};
  require(!gtosd::reduce_hu_preflop_best_response_action_values(duplicate_combo, flop, true) &&
              !gtosd::reduce_hu_preflop_best_response_action_values(nonfinite, flop, true) &&
              !gtosd::reduce_hu_preflop_best_response_action_values(no_children, flop, true) &&
              !gtosd::reduce_hu_preflop_best_response_action_values(responder_children,
                                                                    invalid_flop, true),
          "BR action reduction rejects corrupt rows, non-finite values and invalid inputs");
}

void test_best_response_task_recursion() {
  auto config = gtosd::make_hu_co40_benchmark_config();
  config.effective_stack = gtosd::Money::from_antes(12).value();
  config.response_targets = {gtosd::Money::from_units(105'000).value(),
                             gtosd::Money::from_units(107'500).value()};
  const auto tree_result = gtosd::build_hu_preflop_tree(config);
  require(tree_result.has_value(), "short-stack BR recursion fixture builds");
  const auto &tree = tree_result.value();
  const auto blueprint = gtosd::make_uniform_hu_preflop_blueprint(tree);
  require(blueprint.has_value(), "short-stack BR recursion blueprint builds");
  const auto plan_result = gtosd::derive_hu_preflop_decomposition_plan(tree, blueprint.value());
  require(plan_result.has_value(), "short-stack BR decomposition plan builds");
  const auto &plan = plan_result.value();

  const auto shallow_entry = std::ranges::min_element(plan.entries, {}, [&](const auto &entry) {
    return tree.nodes[entry.entry_node].state.remaining_stacks[0].units();
  });
  require(shallow_entry != plan.entries.end() &&
              tree.nodes[shallow_entry->entry_node].state.remaining_stacks[0].units() > 0 &&
              tree.nodes[shallow_entry->entry_node].state.remaining_stacks[0] <=
                  gtosd::Money::from_antes(1).value(),
          "BR recursion fixture selects a postflop stack no deeper than one ante");
  const auto entry_index =
      static_cast<std::uint64_t>(std::distance(plan.entries.begin(), shallow_entry));
  const auto task_span_index = entry_index * plan.canonical_flops;
  const auto combos = gtosd::all_combos();
  std::array<bool, 36U> observed_turns{};
  std::array<std::uint64_t, 2> leaf_kinds{};

  const gtosd::HuPreflopBestResponseLeafProvider leaf_provider =
      [&](const gtosd::HuPreflopBestResponseLeafQuery &query) {
        auto state = tree.nodes[query.entry_node].state;
        for (const auto card : query.flop) {
          state.board_mask |= card.mask();
        }
        gtosd::ActionConfig action_config;
        action_config.aggressive_sizes.assign(config.postflop_sizes.begin(),
                                              config.postflop_sizes.end());
        action_config.raise_depth = 4U;
        action_config.minimum_bet = config.postflop_minimum_bet;
        action_config.all_in_mode = gtosd::AllInMode::Add;
        action_config.all_in_threshold = gtosd::PotPercentage::from_basis_points(100'000).value();
        double opponent_action_reach = 1.0;
        for (const auto &action : query.action_history) {
          while (state.status == gtosd::HandStatus::StreetComplete) {
            const auto advanced = gtosd::advance_street(state);
            if (!advanced) {
              return gtosd::Result<
                  std::vector<gtosd::HuPreflopBestResponseComboValue>,
                  gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::GameFailure);
            }
            state = advanced.value();
            if (state.street == gtosd::Street::Turn) {
              state.board_mask |= query.turn.mask();
            }
          }
          const auto legal = gtosd::legal_actions(state, action_config);
          if (!legal || std::ranges::find(legal.value(), action) == legal.value().end()) {
            return gtosd::Result<
                std::vector<gtosd::HuPreflopBestResponseComboValue>,
                gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
          }
          if (state.player_to_act != query.responding_player) {
            opponent_action_reach /= static_cast<double>(legal.value().size());
          }
          const auto next = gtosd::apply_action(state, action, action_config);
          if (!next) {
            return gtosd::Result<
                std::vector<gtosd::HuPreflopBestResponseComboValue>,
                gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::GameFailure);
          }
          state = next.value();
        }
        if (query.kind == gtosd::HuPreflopBestResponseLeafKind::RiverContinuation &&
            state.status == gtosd::HandStatus::StreetComplete) {
          const auto advanced = gtosd::advance_street(state);
          if (!advanced) {
            return gtosd::Result<
                std::vector<gtosd::HuPreflopBestResponseComboValue>,
                gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::GameFailure);
          }
          state = advanced.value();
        }
        if (state != query.state) {
          return gtosd::Result<
              std::vector<gtosd::HuPreflopBestResponseComboValue>,
              gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
        }
        ++leaf_kinds[static_cast<std::size_t>(query.kind)];
        if (query.has_turn) {
          observed_turns[query.turn.value()] = true;
        }
        std::vector<gtosd::HuPreflopBestResponseComboValue> values;
        values.reserve(528U);
        for (std::size_t combo = 0U; combo < combos.size(); ++combo) {
          const auto blocked_by_flop = std::ranges::any_of(query.flop, [&](const auto card) {
            return combos[combo].first == card || combos[combo].second == card;
          });
          if (blocked_by_flop) {
            continue;
          }
          const auto blocked_by_turn = query.has_turn && (combos[combo].first == query.turn ||
                                                          combos[combo].second == query.turn);
          const auto chance_weight = query.has_turn ? 30.0 : 930.0;
          const auto reach = blocked_by_turn ? 0.0 : chance_weight * opponent_action_reach;
          values.push_back({static_cast<gtosd::ComboId>(combo), reach, reach,
                            reach > 0.0 ? 1.0 : 0.0, reach > 0.0});
        }
        return gtosd::Result<std::vector<gtosd::HuPreflopBestResponseComboValue>,
                             gtosd::HuPreflopError>::success(std::move(values));
      };

  const auto evaluated = gtosd::evaluate_hu_preflop_best_response_task(
      tree, plan, task_span_index, leaf_provider, "unit_uniform_leaf_profile_v1", 0U, 17U);
  const auto exact_runout_mass =
      evaluated.has_value() && std::ranges::all_of(evaluated.value().values, [](const auto &value) {
        return std::abs(value.weighted_counterfactual_reach - 930.0) < 1.0e-9 &&
               std::abs(value.weighted_counterfactual_utility_antes - 930.0) < 1.0e-9 &&
               value.conditional_value_antes == 1.0 && value.positive_reach;
      });
  require(
      evaluated.has_value() && exact_runout_mass &&
          evaluated.value().physical_turn_branches_visited >= 33U &&
          evaluated.value().physical_turn_branches_visited % 33U == 0U &&
          leaf_kinds[static_cast<std::size_t>(
              gtosd::HuPreflopBestResponseLeafKind::UpperStreetTerminal)] > 0U &&
          leaf_kinds[static_cast<std::size_t>(
              gtosd::HuPreflopBestResponseLeafKind::RiverContinuation)] > 0U &&
          gtosd::validate_hu_preflop_best_response_task_evaluation(tree, plan, evaluated.value())
              .has_value(),
      "BR task recursion preserves exact 31x30 runout mass through max and sum");
  const auto first_flop = plan.canonical_flop_catalog.front().cards;
  std::array<bool, 36U> flop_cards{};
  for (const auto card : first_flop) {
    flop_cards[card.value()] = true;
  }
  bool exact_turn_coverage = true;
  for (std::size_t card = 0U; card < observed_turns.size(); ++card) {
    exact_turn_coverage = exact_turn_coverage && observed_turns[card] == !flop_cards[card];
  }
  require(exact_turn_coverage, "BR task recursion visits every and only physical Turn observation");

  const auto serialized_task =
      gtosd::serialize_hu_preflop_best_response_task_evaluation(evaluated.value());
  const auto restored_task =
      serialized_task
          ? gtosd::deserialize_hu_preflop_best_response_task_evaluation(serialized_task.value())
          : gtosd::Result<gtosd::HuPreflopBestResponseTaskEvaluation,
                          gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
  require(restored_task.has_value() &&
              restored_task.value().fingerprint == evaluated.value().fingerprint &&
              gtosd::validate_hu_preflop_best_response_task_evaluation(tree, plan,
                                                                       restored_task.value()) &&
              !gtosd::deserialize_hu_preflop_best_response_task_evaluation(serialized_task.value() +
                                                                           "corrupt"),
          "BR task evaluation round-trips and rejects trailing corruption");
  const auto task_path = std::filesystem::current_path() / "hu_preflop_best_response_task_test.bin";
  std::error_code task_ignored;
  std::filesystem::remove(task_path, task_ignored);
  require(
      gtosd::save_hu_preflop_best_response_task_evaluation(evaluated.value(), task_path.string()) &&
          gtosd::load_hu_preflop_best_response_task_evaluation(task_path.string()),
      "BR task evaluation saves atomically with checksum");
  std::filesystem::remove(task_path, task_ignored);

  auto tampered = evaluated.value();
  ++tampered.physical_turn_branches_visited;
  tampered.fingerprint = gtosd::fingerprint_hu_preflop_best_response_task_evaluation(tampered);
  require(!gtosd::validate_hu_preflop_best_response_task_evaluation(tree, plan, tampered) &&
              !gtosd::evaluate_hu_preflop_best_response_task(
                  tree, plan, plan.canonical_public_flop_roots, leaf_provider,
                  "unit_uniform_leaf_profile_v1", 0U, 17U) &&
              !gtosd::evaluate_hu_preflop_best_response_task(tree, plan, task_span_index,
                                                             leaf_provider, "", 0U, 17U),
          "BR task validation rejects semantic tampering and invalid identities");
}

void test_best_response_entry_streaming_reduction() {
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config()).value();
  const auto blueprint = gtosd::make_uniform_hu_preflop_blueprint(tree).value();
  const auto plan = gtosd::derive_hu_preflop_decomposition_plan(tree, blueprint).value();
  constexpr std::string_view continuation = "unit_best_response_entry_profile_v1";
  constexpr std::uint64_t blueprint_iterations = 19U;
  const auto combos = gtosd::all_combos();

  const auto make_task_evaluation = [&](const std::uint64_t task_span_index) {
    gtosd::HuPreflopBestResponseTaskEvaluation evaluation;
    const auto flop_index = static_cast<std::size_t>(task_span_index % plan.canonical_flops);
    const auto &flop = plan.canonical_flop_catalog[flop_index];
    const auto board_mask = flop.cards[0].mask() | flop.cards[1].mask() | flop.cards[2].mask();
    evaluation.tree_fingerprint = tree.fingerprint;
    evaluation.blueprint_fingerprint = plan.blueprint_fingerprint;
    evaluation.continuation_fingerprint = continuation;
    evaluation.blueprint_iterations = blueprint_iterations;
    evaluation.task_span_index = task_span_index;
    evaluation.entry_node = plan.entries.front().entry_node;
    evaluation.flop = flop.cards;
    evaluation.responding_player = 0U;
    evaluation.decision_nodes_visited = 1U;
    evaluation.responder_decision_nodes_visited = 1U;
    evaluation.physical_turn_branches_visited = 33U;
    evaluation.upper_terminal_leaves_visited = 1U;
    evaluation.river_continuation_leaves_visited = 1U;
    evaluation.maximum_recursion_depth = 1U;
    evaluation.values.reserve(528U);
    for (std::size_t combo = 0U; combo < combos.size(); ++combo) {
      const auto combo_mask = combos[combo].first.mask() | combos[combo].second.mask();
      if ((combo_mask & board_mask) == 0U) {
        const auto weight = static_cast<double>(flop.physical_outcome_count);
        evaluation.values.push_back(
            {static_cast<gtosd::ComboId>(combo), weight, weight, 1.0, true});
      }
    }
    evaluation.fingerprint =
        gtosd::fingerprint_hu_preflop_best_response_task_evaluation(evaluation);
    return evaluation;
  };

  auto accumulator = gtosd::make_hu_preflop_best_response_entry_accumulator(
      tree, plan, 0U, std::string(continuation), 0U, blueprint_iterations);
  require(accumulator.has_value() && !accumulator.value().complete &&
              accumulator.value().task_count == 573U &&
              accumulator.value().first_task_span_index == 0U,
          "BR entry accumulator starts at one exact contiguous 573-Flop task span");
  const auto out_of_order = make_task_evaluation(1U);
  require(!gtosd::accumulate_hu_preflop_best_response_task_evaluation(
              tree, plan, accumulator.value(), out_of_order),
          "BR entry accumulator rejects a skipped canonical Flop task");

  const auto first_task = make_task_evaluation(0U);
  const auto first_accumulated = gtosd::accumulate_hu_preflop_best_response_task_evaluation(
      tree, plan, accumulator.value(), first_task);
  bool accumulated = first_accumulated.has_value();
  const auto serialized_partial_entry =
      accumulated ? gtosd::serialize_hu_preflop_best_response_entry_accumulator(accumulator.value())
                  : gtosd::Result<std::string, gtosd::HuPreflopError>::failure(
                        gtosd::HuPreflopError::IntegrityFailure);
  const auto restored_partial_entry =
      serialized_partial_entry
          ? gtosd::deserialize_hu_preflop_best_response_entry_accumulator(
                serialized_partial_entry.value())
          : gtosd::Result<gtosd::HuPreflopBestResponseEntryAccumulator,
                          gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
  const auto entry_accumulator_path =
      std::filesystem::current_path() / "hu_preflop_best_response_entry_accumulator_test.bin";
  std::error_code entry_accumulator_ignored;
  std::filesystem::remove(entry_accumulator_path, entry_accumulator_ignored);
  require(
      restored_partial_entry.has_value() && restored_partial_entry.value().next_task_offset == 1U &&
          gtosd::validate_hu_preflop_best_response_entry_accumulator(
              tree, plan, restored_partial_entry.value()) &&
          !gtosd::deserialize_hu_preflop_best_response_entry_accumulator(
              serialized_partial_entry.value() + "corrupt") &&
          gtosd::save_hu_preflop_best_response_entry_accumulator(accumulator.value(),
                                                                 entry_accumulator_path.string()) &&
          gtosd::load_hu_preflop_best_response_entry_accumulator(entry_accumulator_path.string()),
      "partial BR entry accumulator persists one committed Flop task atomically");
  std::filesystem::remove(entry_accumulator_path, entry_accumulator_ignored);
  for (std::uint64_t task = 1U; task < plan.canonical_flops; ++task) {
    const auto evaluation = make_task_evaluation(task);
    if (!gtosd::accumulate_hu_preflop_best_response_task_evaluation(tree, plan, accumulator.value(),
                                                                    evaluation)) {
      accumulated = false;
      break;
    }
  }
  const auto result =
      accumulated
          ? gtosd::finalize_hu_preflop_best_response_entry_accumulator(tree, plan,
                                                                       accumulator.value())
          : gtosd::Result<gtosd::HuPreflopBestResponseEntryEvaluation,
                          gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
  std::array<std::uint64_t, gtosd::hu_preflop_hand_class_count> combo_count_by_class{};
  for (const auto &combo : combos) {
    ++combo_count_by_class[gtosd::hand_class(combo)];
  }
  constexpr double physical_flops_per_private_combo = 5'984.0;
  bool exact_flop_orbit_mass = result.has_value();
  if (exact_flop_orbit_mass) {
    for (const auto &value : result.value().values) {
      const auto expected = physical_flops_per_private_combo *
                            static_cast<double>(combo_count_by_class[value.responding_class]);
      if (value.weighted_counterfactual_reach != expected ||
          value.weighted_counterfactual_utility_antes != expected ||
          value.conditional_value_antes != 1.0 || !value.positive_reach) {
        exact_flop_orbit_mass = false;
        break;
      }
    }
  }
  require(accumulated && accumulator.value().complete && result.has_value() &&
              exact_flop_orbit_mass &&
              gtosd::validate_hu_preflop_best_response_entry_evaluation(tree, plan, result.value()),
          "BR entry reduction reconstructs every physical Flop per exact preflop hand class");

  const auto serialized_entry =
      gtosd::serialize_hu_preflop_best_response_entry_evaluation(result.value());
  const auto restored_entry =
      serialized_entry
          ? gtosd::deserialize_hu_preflop_best_response_entry_evaluation(serialized_entry.value())
          : gtosd::Result<gtosd::HuPreflopBestResponseEntryEvaluation,
                          gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
  const auto entry_path =
      std::filesystem::current_path() / "hu_preflop_best_response_entry_test.bin";
  std::error_code entry_ignored;
  std::filesystem::remove(entry_path, entry_ignored);
  require(restored_entry.has_value() &&
              restored_entry.value().fingerprint == result.value().fingerprint &&
              gtosd::validate_hu_preflop_best_response_entry_evaluation(tree, plan,
                                                                        restored_entry.value()) &&
              !gtosd::deserialize_hu_preflop_best_response_entry_evaluation(
                  serialized_entry.value() + "corrupt") &&
              gtosd::save_hu_preflop_best_response_entry_evaluation(result.value(),
                                                                    entry_path.string()) &&
              gtosd::load_hu_preflop_best_response_entry_evaluation(entry_path.string()),
          "complete BR entry evaluation round-trips and saves atomically with checksum");
  std::filesystem::remove(entry_path, entry_ignored);

  auto tampered = accumulator.value();
  tampered.next_task_offset = 0U;
  tampered.complete = false;
  tampered.fingerprint = gtosd::fingerprint_hu_preflop_best_response_entry_accumulator(tampered);
  require(!gtosd::validate_hu_preflop_best_response_entry_accumulator(tree, plan, tampered),
          "BR entry validation rejects a fingerprint-consistent rewind with accumulated values");
}

void test_best_response_preflop_recursion() {
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config()).value();
  const auto blueprint = gtosd::make_uniform_hu_preflop_blueprint(tree).value();
  const auto plan = gtosd::derive_hu_preflop_decomposition_plan(tree, blueprint).value();

  for (std::uint8_t responding_player = 0U; responding_player < 2U; ++responding_player) {
    std::vector<double> opponent_path_reach(tree.nodes.size(),
                                            std::numeric_limits<double>::quiet_NaN());
    std::function<void(std::uint32_t, double)> assign_reach;
    assign_reach = [&](const std::uint32_t node_id, const double reach) {
      opponent_path_reach[node_id] = reach;
      const auto &node = tree.nodes[node_id];
      if (node.kind != gtosd::HuPreflopNodeKind::Decision) {
        return;
      }
      const auto action_probability = node.state.player_to_act == responding_player
                                          ? 1.0
                                          : 1.0 / static_cast<double>(node.edges.size());
      for (const auto &edge : node.edges) {
        assign_reach(edge.child, reach * action_probability);
      }
    };
    assign_reach(tree.root, 1.0);

    const gtosd::HuPreflopBestResponsePreflopLeafProvider leaf_provider =
        [&](const gtosd::HuPreflopBestResponsePreflopLeafQuery &query) {
          using Values = std::vector<gtosd::HuPreflopBestResponseHandClassValue>;
          if (query.node_id >= tree.nodes.size() || query.responding_player != responding_player ||
              !std::isfinite(opponent_path_reach[query.node_id])) {
            return gtosd::Result<Values, gtosd::HuPreflopError>::failure(
                gtosd::HuPreflopError::IntegrityFailure);
          }
          const auto &node = tree.nodes[query.node_id];
          const auto expected_kind =
              node.kind == gtosd::HuPreflopNodeKind::PostflopEntry
                  ? gtosd::HuPreflopBestResponsePreflopLeafKind::PostflopEntry
              : node.kind == gtosd::HuPreflopNodeKind::TerminalFold
                  ? gtosd::HuPreflopBestResponsePreflopLeafKind::TerminalFold
                  : gtosd::HuPreflopBestResponsePreflopLeafKind::TerminalAllIn;
          if (node.kind == gtosd::HuPreflopNodeKind::Decision || query.kind != expected_kind) {
            return gtosd::Result<Values, gtosd::HuPreflopError>::failure(
                gtosd::HuPreflopError::IntegrityFailure);
          }
          Values values;
          values.reserve(gtosd::hu_preflop_hand_class_count);
          const auto reach = opponent_path_reach[query.node_id];
          for (std::size_t class_id = 0U; class_id < gtosd::hu_preflop_hand_class_count;
               ++class_id) {
            values.push_back({static_cast<gtosd::HandClassId>(class_id), reach, reach,
                              reach > 0.0 ? 1.0 : 0.0, reach > 0.0});
          }
          return gtosd::Result<Values, gtosd::HuPreflopError>::success(std::move(values));
        };
    const auto evaluated = gtosd::evaluate_hu_preflop_best_response_preflop(
        tree, plan, leaf_provider, "unit_preflop_leaf_profile_v1", responding_player, 23U);
    const auto unit_root_value =
        evaluated.has_value() &&
        std::ranges::all_of(evaluated.value().values, [](const auto &value) {
          return std::abs(value.weighted_counterfactual_reach - 1.0) < 1.0e-12 &&
                 std::abs(value.weighted_counterfactual_utility_antes - 1.0) < 1.0e-12 &&
                 value.conditional_value_antes == 1.0 && value.positive_reach;
        });
    require(evaluated.has_value() && unit_root_value &&
                evaluated.value().decision_nodes_visited == 20U &&
                evaluated.value().postflop_entry_leaves_visited == 9U &&
                evaluated.value().terminal_fold_leaves_visited == 19U &&
                evaluated.value().terminal_all_in_leaves_visited == 10U &&
                gtosd::validate_hu_preflop_best_response_preflop_evaluation(tree, plan,
                                                                            evaluated.value()),
            "preflop BR recursion applies max for the responder and sum for the opponent");

    auto tampered = evaluated.value();
    ++tampered.terminal_all_in_leaves_visited;
    tampered.fingerprint = gtosd::fingerprint_hu_preflop_best_response_preflop_evaluation(tampered);
    require(!gtosd::validate_hu_preflop_best_response_preflop_evaluation(tree, plan, tampered),
            "preflop BR validation rejects semantic leaf-coverage tampering");
  }
}

void test_exact_preflop_fold_terminal_values() {
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config()).value();
  const auto blueprint = gtosd::make_uniform_hu_preflop_blueprint(tree).value();
  const auto combos = gtosd::all_combos();
  std::array<std::uint64_t, gtosd::hu_preflop_hand_class_count> combo_count_by_class{};
  for (const auto &combo : combos) {
    ++combo_count_by_class[gtosd::hand_class(combo)];
  }
  constexpr double compatible_opponent_combos = 561.0;
  constexpr double ordered_public_runouts = 4'027'520.0;
  constexpr std::string_view continuation = "unit_exact_preflop_terminal_profile_v1";
  constexpr std::uint64_t blueprint_iterations = 31U;

  for (std::uint8_t responding_player = 0U; responding_player < 2U; ++responding_player) {
    std::vector<double> opponent_path_reach(tree.nodes.size(),
                                            std::numeric_limits<double>::quiet_NaN());
    std::function<void(std::uint32_t, double)> assign_reach;
    assign_reach = [&](const std::uint32_t node_id, const double reach) {
      opponent_path_reach[node_id] = reach;
      const auto &node = tree.nodes[node_id];
      if (node.kind != gtosd::HuPreflopNodeKind::Decision) {
        return;
      }
      const auto action_probability = node.state.player_to_act == responding_player
                                          ? 1.0
                                          : 1.0 / static_cast<double>(node.edges.size());
      for (const auto &edge : node.edges) {
        assign_reach(edge.child, reach * action_probability);
      }
    };
    assign_reach(tree.root, 1.0);

    std::uint64_t evaluated_fold_count = 0U;
    for (const auto &node : tree.nodes) {
      if (node.kind != gtosd::HuPreflopNodeKind::TerminalFold) {
        continue;
      }
      const auto evaluation = gtosd::evaluate_hu_preflop_best_response_fold_terminal(
          tree, blueprint, node.id, std::string(continuation), responding_player,
          blueprint_iterations);
      const auto settlement = gtosd::settle_terminal(node.state, tree.config.rake, 0U);
      const auto payoff =
          settlement.has_value()
              ? static_cast<double>(settlement.value().payoff_units[responding_player]) /
                    static_cast<double>(gtosd::Money::units_per_ante)
              : std::numeric_limits<double>::quiet_NaN();
      bool exact_values = evaluation.has_value() && settlement.has_value();
      if (exact_values) {
        for (const auto &value : evaluation.value().values) {
          const auto expected_reach =
              static_cast<double>(combo_count_by_class[value.responding_class]) *
              compatible_opponent_combos * opponent_path_reach[node.id] * ordered_public_runouts;
          const auto expected_utility = expected_reach * payoff;
          const auto reach_tolerance = 1.0e-12 * std::max(1.0, std::abs(expected_reach));
          const auto utility_tolerance = 1.0e-12 * std::max(1.0, std::abs(expected_utility));
          if (std::abs(value.weighted_counterfactual_reach - expected_reach) > reach_tolerance ||
              std::abs(value.weighted_counterfactual_utility_antes - expected_utility) >
                  utility_tolerance ||
              std::abs(value.conditional_value_antes - payoff) > 1.0e-12 || !value.positive_reach) {
            exact_values = false;
            break;
          }
        }
      }
      gtosd::HuPreflopBestResponsePreflopLeafQuery query;
      query.kind = gtosd::HuPreflopBestResponsePreflopLeafKind::TerminalFold;
      query.node_id = node.id;
      query.responding_player = responding_player;
      const auto selected =
          evaluation.has_value()
              ? gtosd::select_hu_preflop_best_response_preflop_terminal_evaluation(
                    tree, blueprint, query, evaluation.value(), std::string(continuation),
                    blueprint_iterations)
              : gtosd::Result<
                    std::vector<gtosd::HuPreflopBestResponseHandClassValue>,
                    gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
      const auto identical_selection =
          selected.has_value() && evaluation.has_value() &&
          selected.value().size() == evaluation.value().values.size() &&
          std::ranges::equal(selected.value(), evaluation.value().values,
                             [](const auto &first, const auto &second) {
                               return first.responding_class == second.responding_class &&
                                      first.weighted_counterfactual_reach ==
                                          second.weighted_counterfactual_reach &&
                                      first.weighted_counterfactual_utility_antes ==
                                          second.weighted_counterfactual_utility_antes &&
                                      first.conditional_value_antes ==
                                          second.conditional_value_antes &&
                                      first.positive_reach == second.positive_reach;
                             });
      require(exact_values && identical_selection,
              "exact preflop fold leaf preserves opponent reach, all runouts and terminal payoff");
      ++evaluated_fold_count;
    }
    require(evaluated_fold_count == tree.stats.terminal_folds,
            "exact preflop fold evaluation covers every fold terminal for both responders");
  }

  const auto fold = std::ranges::find_if(tree.nodes, [](const auto &node) {
    return node.kind == gtosd::HuPreflopNodeKind::TerminalFold;
  });
  const auto valid = gtosd::evaluate_hu_preflop_best_response_fold_terminal(
      tree, blueprint, fold->id, std::string(continuation), 0U, blueprint_iterations);
  auto tampered = valid.value();
  tampered.continuation_fingerprint = "different_profile";
  tampered.fingerprint =
      gtosd::fingerprint_hu_preflop_best_response_preflop_terminal_evaluation(tampered);
  gtosd::HuPreflopBestResponsePreflopLeafQuery query;
  query.kind = gtosd::HuPreflopBestResponsePreflopLeafKind::TerminalFold;
  query.node_id = fold->id;
  query.responding_player = 0U;
  require(!gtosd::select_hu_preflop_best_response_preflop_terminal_evaluation(
              tree, blueprint, query, tampered, std::string(continuation), blueprint_iterations),
          "preflop terminal selector rejects a fingerprint-consistent continuation mismatch");
}

void test_preflop_all_in_equity_streaming_contract() {
  const auto catalog = gtosd::build_hu_preflop_all_in_board_catalog();
  require(catalog.has_value() && catalog.value().physical_unordered_boards == 376'992U &&
              catalog.value().ordered_histories_per_unordered_board == 20U &&
              !catalog.value().boards.empty() &&
              catalog.value().boards.size() < catalog.value().physical_unordered_boards &&
              gtosd::validate_hu_preflop_all_in_board_catalog(catalog.value()),
          "preflop all-in catalog losslessly canonicalizes every five-card public board");

  auto accumulator = gtosd::make_hu_preflop_all_in_equity_accumulator(catalog.value());
  require(accumulator.has_value() && accumulator.value().next_board_ordinal == 0U &&
              accumulator.value().matchups.size() == 81U * 81U &&
              gtosd::validate_hu_preflop_all_in_equity_accumulator(catalog.value(),
                                                                   accumulator.value()),
          "preflop all-in equity accumulator starts empty and fingerprinted");
  const auto first_board_weight =
      static_cast<std::uint64_t>(catalog.value().boards.front().physical_board_count) * 465U *
      406U * 20U;
  const auto accumulated =
      gtosd::accumulate_hu_preflop_all_in_equity_board(catalog.value(), accumulator.value());
  require(accumulated.has_value() && accumulator.value().next_board_ordinal == 1U &&
              accumulator.value().matchup_outcome_count == first_board_weight &&
              gtosd::validate_hu_preflop_all_in_equity_accumulator(catalog.value(),
                                                                   accumulator.value()),
          "one all-in board orbit contributes exact integer showdown mass");

  auto rewound = accumulator.value();
  rewound.next_board_ordinal = 0U;
  rewound.complete = false;
  rewound.fingerprint = gtosd::fingerprint_hu_preflop_all_in_equity_accumulator(rewound);
  require(!gtosd::validate_hu_preflop_all_in_equity_accumulator(catalog.value(), rewound),
          "all-in accumulator rejects a fingerprint-consistent board rewind");
}

void test_preflop_all_in_terminal_consumes_exact_table_contract() {
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config()).value();
  const auto blueprint = gtosd::make_uniform_hu_preflop_blueprint(tree).value();
  const auto plan = gtosd::derive_hu_preflop_decomposition_plan(tree, blueprint).value();
  const auto catalog = gtosd::build_hu_preflop_all_in_board_catalog().value();
  const auto combos = gtosd::all_combos();
  std::array<std::uint64_t, gtosd::hu_preflop_hand_class_count> combo_count_by_class{};
  std::array<std::uint64_t, gtosd::hu_preflop_hand_class_count * gtosd::hu_preflop_hand_class_count>
      compatible_pairs{};
  for (std::size_t first = 0U; first < combos.size(); ++first) {
    ++combo_count_by_class[gtosd::hand_class(combos[first])];
    const auto first_mask = combos[first].first.mask() | combos[first].second.mask();
    for (std::size_t second = 0U; second < combos.size(); ++second) {
      const auto second_mask = combos[second].first.mask() | combos[second].second.mask();
      if ((first_mask & second_mask) == 0U) {
        ++compatible_pairs[static_cast<std::size_t>(gtosd::hand_class(combos[first])) *
                               gtosd::hu_preflop_hand_class_count +
                           static_cast<std::size_t>(gtosd::hand_class(combos[second]))];
      }
    }
  }
  constexpr std::uint64_t ordered_public_runouts = 4'027'520U;
  gtosd::HuPreflopAllInEquityTable tie_oracle;
  tie_oracle.board_catalog_fingerprint = catalog.fingerprint;
  tie_oracle.evaluator_contract = catalog.evaluator_contract;
  tie_oracle.canonical_board_count = catalog.boards.size();
  tie_oracle.physical_unordered_boards = catalog.physical_unordered_boards;
  tie_oracle.ordered_public_runouts_per_private_deal = ordered_public_runouts;
  tie_oracle.matchups.resize(gtosd::hu_preflop_hand_class_count *
                             gtosd::hu_preflop_hand_class_count);
  for (std::size_t index = 0U; index < tie_oracle.matchups.size(); ++index) {
    tie_oracle.matchups[index].ties = compatible_pairs[index] * ordered_public_runouts;
    tie_oracle.matchup_outcome_count += tie_oracle.matchups[index].ties;
  }
  tie_oracle.accumulator_fingerprint = "unit_all_tie_oracle";
  tie_oracle.fingerprint = gtosd::fingerprint_hu_preflop_all_in_equity_table(tie_oracle);
  const auto training_oracle = gtosd::make_hu_preflop_all_in_training_oracle(tie_oracle);
  const auto training_oracle_is_all_ties =
      training_oracle.has_value() &&
      std::ranges::all_of(training_oracle.value().matchups, [](const auto &matchup) {
        return matchup.win_probability == 0.0 && matchup.tie_probability == 1.0;
      });
  require(gtosd::validate_hu_preflop_all_in_equity_table(catalog, tie_oracle).has_value(),
          "all-in table validator proves every class-pair chance mass and transpose symmetry");
  require(training_oracle_is_all_ties &&
              gtosd::validate_hu_preflop_all_in_training_oracle(training_oracle.value()),
          "all-in equity table converts losslessly into the bounded training oracle");

  const auto all_in = std::ranges::find_if(tree.nodes, [](const auto &node) {
    return node.kind == gtosd::HuPreflopNodeKind::TerminalAllIn;
  });
  const auto coverage = make_complete_zero_profile_coverage(tree, plan, 37U);
  require(coverage.has_value(), "complete zero-CFV profile coverage builds for exact evaluation");
  const auto continuation = gtosd::fingerprint_hu_preflop_continuation_profile(coverage.value());
  std::array<gtosd::HuPreflopBestResponsePreflopEvaluation, 2> exact_root_evaluations{};
  for (std::uint8_t responding_player = 0U; responding_player < 2U; ++responding_player) {
    std::vector<double> opponent_path_reach(tree.nodes.size(),
                                            std::numeric_limits<double>::quiet_NaN());
    std::function<void(std::uint32_t, double)> assign_reach;
    assign_reach = [&](const std::uint32_t node_id, const double reach) {
      opponent_path_reach[node_id] = reach;
      const auto &node = tree.nodes[node_id];
      if (node.kind != gtosd::HuPreflopNodeKind::Decision) {
        return;
      }
      const auto action_probability = node.state.player_to_act == responding_player
                                          ? 1.0
                                          : 1.0 / static_cast<double>(node.edges.size());
      for (const auto &edge : node.edges) {
        assign_reach(edge.child, reach * action_probability);
      }
    };
    assign_reach(tree.root, 1.0);
    const auto evaluation = gtosd::evaluate_hu_preflop_best_response_all_in_terminal(
        tree, blueprint, catalog, tie_oracle, all_in->id, std::string(continuation),
        responding_player, 37U);
    const auto settlement = gtosd::settle_terminal(all_in->state, tree.config.rake, 0b11U);
    const auto tie_payoff =
        settlement.has_value()
            ? static_cast<double>(settlement.value().payoff_units[responding_player]) /
                  static_cast<double>(gtosd::Money::units_per_ante)
            : std::numeric_limits<double>::quiet_NaN();
    bool exact = evaluation.has_value() && settlement.has_value();
    if (exact) {
      for (const auto &value : evaluation.value().values) {
        const auto expected_reach =
            static_cast<double>(combo_count_by_class[value.responding_class]) * 561.0 *
            opponent_path_reach[all_in->id] * static_cast<double>(ordered_public_runouts);
        const auto reach_tolerance = 1.0e-12 * std::max(1.0, std::abs(expected_reach));
        if (std::abs(value.weighted_counterfactual_reach - expected_reach) > reach_tolerance ||
            std::abs(value.conditional_value_antes - tie_payoff) > 1.0e-12) {
          exact = false;
          break;
        }
      }
    }
    require(exact, "preflop all-in leaf applies opponent reach and exact class-pair table mass");

    std::vector<gtosd::HuPreflopBestResponseEntryEvaluation> entry_evaluations;
    entry_evaluations.reserve(plan.entries.size());
    for (std::size_t entry_index = 0U; entry_index < plan.entries.size(); ++entry_index) {
      gtosd::HuPreflopBestResponseEntryEvaluation entry;
      entry.tree_fingerprint = tree.fingerprint;
      entry.blueprint_fingerprint = blueprint.fingerprint;
      entry.continuation_fingerprint = std::string(continuation);
      entry.accumulator_fingerprint = "unit_entry_" + std::to_string(entry_index);
      entry.blueprint_iterations = 37U;
      entry.entry_index = entry_index;
      entry.entry_node = plan.entries[entry_index].entry_node;
      entry.responding_player = responding_player;
      entry.task_count = plan.canonical_flops;
      entry.values.reserve(gtosd::hu_preflop_hand_class_count);
      for (std::size_t class_id = 0U; class_id < gtosd::hu_preflop_hand_class_count; ++class_id) {
        const auto reach = static_cast<double>(combo_count_by_class[class_id]) * 561.0 *
                           opponent_path_reach[entry.entry_node] *
                           static_cast<double>(ordered_public_runouts);
        entry.values.push_back(
            {static_cast<gtosd::HandClassId>(class_id), reach, 0.0, 0.0, reach > 0.0});
      }
      entry.fingerprint = gtosd::fingerprint_hu_preflop_best_response_entry_evaluation(entry);
      entry_evaluations.push_back(std::move(entry));
    }

    std::vector<gtosd::HuPreflopBestResponsePreflopTerminalEvaluation> terminal_evaluations;
    terminal_evaluations.reserve(tree.stats.terminal_folds + tree.stats.terminal_all_ins);
    bool complete_terminals = true;
    for (const auto &node : tree.nodes) {
      if (node.kind != gtosd::HuPreflopNodeKind::TerminalFold &&
          node.kind != gtosd::HuPreflopNodeKind::TerminalAllIn) {
        continue;
      }
      const auto terminal =
          node.kind == gtosd::HuPreflopNodeKind::TerminalFold
              ? gtosd::evaluate_hu_preflop_best_response_fold_terminal(
                    tree, blueprint, node.id, std::string(continuation), responding_player, 37U)
              : gtosd::evaluate_hu_preflop_best_response_all_in_terminal(
                    tree, blueprint, catalog, tie_oracle, node.id, std::string(continuation),
                    responding_player, 37U);
      if (!terminal) {
        complete_terminals = false;
        break;
      }
      terminal_evaluations.push_back(terminal.value());
    }
    const auto artifacts_valid =
        std::ranges::all_of(entry_evaluations,
                            [&](const auto &entry) {
                              return gtosd::validate_hu_preflop_best_response_entry_evaluation(
                                         tree, plan, entry)
                                  .has_value();
                            }) &&
        std::ranges::all_of(terminal_evaluations, [&](const auto &terminal) {
          return gtosd::validate_hu_preflop_best_response_preflop_terminal_evaluation(
                     tree, blueprint, terminal)
              .has_value();
        });
    require(complete_terminals && artifacts_valid,
            "whole-game BR leaf artifacts validate independently before assembly");
    const auto whole_game =
        complete_terminals
            ? gtosd::evaluate_hu_preflop_best_response_whole_game(
                  tree, blueprint, plan, entry_evaluations, terminal_evaluations,
                  std::string(continuation), responding_player, 37U)
            : gtosd::Result<gtosd::HuPreflopBestResponsePreflopEvaluation, gtosd::HuPreflopError>::
                  failure(gtosd::HuPreflopError::IntegrityFailure);
    bool root_mass_exact = whole_game.has_value();
    if (root_mass_exact) {
      for (const auto &value : whole_game.value().values) {
        const auto expected = static_cast<double>(combo_count_by_class[value.responding_class]) *
                              561.0 * static_cast<double>(ordered_public_runouts);
        const auto tolerance = 1.0e-12 * std::max(1.0, std::abs(expected));
        if (std::abs(value.weighted_counterfactual_reach - expected) > tolerance) {
          root_mass_exact = false;
          break;
        }
      }
    }
    require(complete_terminals && root_mass_exact,
            std::string(
                "whole-game BR assembly consumes all 9 entries and 29 exact preflop terminals: ") +
                (whole_game.has_value() ? "root_mass_mismatch"
                                        : gtosd::hu_preflop_error_name(whole_game.error())));
    exact_root_evaluations[responding_player] = whole_game.value();
    terminal_evaluations.pop_back();
    require(!gtosd::evaluate_hu_preflop_best_response_whole_game(
                tree, blueprint, plan, entry_evaluations, terminal_evaluations,
                std::string(continuation), responding_player, 37U),
            "whole-game BR assembly rejects one missing terminal leaf");
  }

  const auto exact_profile = gtosd::evaluate_hu_preflop_exact_profile(
      tree, blueprint, plan, coverage.value(), catalog, tie_oracle);
  const auto evidence =
      exact_profile
          ? gtosd::build_hu_preflop_global_best_response_evidence(
                tree, blueprint, plan, coverage.value(), catalog, tie_oracle,
                exact_root_evaluations, exact_profile.value())
          : gtosd::Result<gtosd::HuPreflopGlobalBestResponseEvidence,
                          gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
  require(
      exact_profile.has_value() && evidence.has_value() && evidence.value().exact_best_response &&
          evidence.value().blueprint_iterations == 37U &&
          evidence.value().chance_outcome_count == 353'430ULL * ordered_public_runouts &&
          evidence.value().profile_evaluation_fingerprint == exact_profile.value().fingerprint &&
          std::abs(evidence.value().nashconv_antes - (evidence.value().deviation_gains_antes[0] +
                                                      evidence.value().deviation_gains_antes[1])) <
              1.0e-12 &&
          std::abs(evidence.value().normalized_nashconv - evidence.value().nashconv_antes / 40.0) <
              1.0e-12,
      "global BR evidence derives NashConv from exact root responses and the exact profile "
      "artifact");
  const auto certification =
      evidence
          ? gtosd::finalize_hu_preflop_whole_game_certification(tree, plan, coverage.value(),
                                                                &evidence.value())
          : gtosd::Result<gtosd::HuPreflopWholeGameCertification, gtosd::HuPreflopError>::failure(
                gtosd::HuPreflopError::IntegrityFailure);
  require(certification.has_value() && certification.value().global_best_response_exact &&
              certification.value().normalized_nashconv == evidence.value().normalized_nashconv &&
              certification.value().certified == (evidence.value().normalized_nashconv <=
                                                  coverage.value().target_normalized_nashconv),
          "the exact profile and both root responses feed the whole-game certification gate end to "
          "end");
  auto duplicate_responder = exact_root_evaluations;
  duplicate_responder[1] = duplicate_responder[0];
  require(!gtosd::build_hu_preflop_global_best_response_evidence(
              tree, blueprint, plan, coverage.value(), catalog, tie_oracle, duplicate_responder,
              exact_profile.value()),
          "global BR evidence rejects duplicate responder evaluations");
  auto impossible_profile = exact_profile.value();
  impossible_profile.total_values_antes[0] = evidence.value().best_response_values_antes[0] + 1.0;
  impossible_profile.preflop_terminal_values_antes[0] =
      impossible_profile.total_values_antes[0] - impossible_profile.postflop_values_antes[0];
  impossible_profile.fingerprint =
      gtosd::fingerprint_hu_preflop_exact_profile_evaluation(impossible_profile);
  require(!gtosd::build_hu_preflop_global_best_response_evidence(
              tree, blueprint, plan, coverage.value(), catalog, tie_oracle, exact_root_evaluations,
              impossible_profile),
          "global BR evidence recomputes and rejects tampered profile terminal values");
}

void test_river_batch_scheduler_contract() {
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config()).value();
  const auto blueprint = gtosd::make_uniform_hu_preflop_blueprint(tree).value();
  const auto plan = gtosd::derive_hu_preflop_decomposition_plan(tree, blueprint).value();
  const auto work = gtosd::estimate_hu_preflop_river_work(tree, plan).value();
  const auto catalog = gtosd::build_hu_preflop_river_root_catalog(tree, plan, work);
  constexpr std::uint64_t batch_payload = 64ULL * 1'024ULL * 1'024ULL;
  const auto batches =
      catalog ? gtosd::derive_hu_preflop_river_batch_plan(tree, plan, work, catalog.value(),
                                                          batch_payload)
              : gtosd::Result<gtosd::HuPreflopRiverBatchPlan, gtosd::HuPreflopError>::failure(
                    gtosd::HuPreflopError::InvalidConfiguration);
  require(batches.has_value() && batches.value().roots_per_batch == 5'294U &&
              batches.value().batch_count == 125'271U &&
              batches.value().final_batch_root_count == 754U &&
              batches.value().maximum_batch_payload_bytes == 67'085'568U &&
              batches.value().fully_materialized_boundary_bytes ==
                  work.fully_materialized_boundary_bytes &&
              batches.value().resolver_root_ordering ==
                  "entry/flop_lex/river_shape_dfs/canonical_ordered_runout/resolver_pair_v3" &&
              batches.value().reduce_to_upper_street_accumulator &&
              !batches.value().allow_full_boundary_materialization &&
              !batches.value().fingerprint.empty(),
          "64 MiB River scheduler plan is bounded and requires streaming reduction");
  require(!gtosd::derive_hu_preflop_river_batch_plan(tree, plan, work, catalog.value(),
                                                     work.boundary_bytes_per_resolver_root - 1U) &&
              !gtosd::derive_hu_preflop_river_batch_plan(tree, plan, work, catalog.value(),
                                                         work.fully_materialized_boundary_bytes),
          "scheduler rejects a sub-record budget and full boundary materialization");

  const auto first = gtosd::hu_preflop_river_batch_at(batches.value(), 0U);
  const auto last =
      gtosd::hu_preflop_river_batch_at(batches.value(), batches.value().batch_count - 1U);
  require(first.has_value() && first.value().first_resolver_root == 0U &&
              first.value().resolver_root_count == 5'294U &&
              first.value().maximum_boundary_payload_bytes == 67'085'568U && last.has_value() &&
              last.value().first_resolver_root + last.value().resolver_root_count ==
                  batches.value().resolver_roots &&
              last.value().task_span_index + 1U == 5'157U &&
              last.value().maximum_boundary_payload_bytes ==
                  last.value().resolver_root_count * work.boundary_bytes_per_resolver_root,
          "first and final River batches cover exact non-overlapping ordinal ranges");
  bool batches_are_task_aligned = true;
  for (std::size_t index = 0U; index < batches.value().batch_count; ++index) {
    const auto task_index = batches.value().batch_task_span_indices[index];
    if (task_index >= catalog.value().task_spans.size()) {
      batches_are_task_aligned = false;
      break;
    }
    const auto &span = catalog.value().task_spans[task_index];
    const auto first_root = batches.value().batch_first_resolver_roots[index];
    const auto count = batches.value().batch_resolver_root_counts[index];
    batches_are_task_aligned =
        first_root % 2U == 0U && count % 2U == 0U && first_root >= span.first_resolver_root &&
        first_root + count <= span.first_resolver_root + span.resolver_root_count;
    if (!batches_are_task_aligned) {
      break;
    }
  }
  require(batches_are_task_aligned,
          "every bounded River batch contains whole resolver pairs in one Flop task span");

  require(catalog.has_value() && catalog.value().river_shapes.size() == 873U &&
              catalog.value().canonical_boards.size() == 369'072U &&
              catalog.value().task_spans.size() == 5'157U &&
              catalog.value().physical_public_board_histories == 7'539'840U &&
              std::accumulate(catalog.value().canonical_boards.begin(),
                              catalog.value().canonical_boards.end(), std::uint64_t{0},
                              [](const std::uint64_t sum, const auto &board) {
                                return sum + board.physical_outcome_count;
                              }) == 7'539'840U &&
              catalog.value().resolver_roots == batches.value().resolver_roots &&
              !catalog.value().fingerprint.empty(),
          "reusable River root catalog materializes only shapes and canonical boards");
  const auto turn_groups = gtosd::enumerate_hu_preflop_river_turn_groups(catalog.value(), 0U);
  const auto &catalog_first_span = catalog.value().task_spans.front();
  std::uint64_t grouped_boards = 0U;
  std::uint64_t grouped_physical_outcomes = 0U;
  bool turn_groups_are_exact = turn_groups.has_value() && turn_groups.value().size() > 1U;
  if (turn_groups) {
    for (const auto &group : turn_groups.value()) {
      if (group.root_catalog_fingerprint != catalog.value().fingerprint ||
          group.task_span_index != 0U || group.entry_node != catalog_first_span.entry_node ||
          group.flop !=
              catalog.value().canonical_boards[catalog_first_span.first_board_index].flop ||
          group.first_board_offset != grouped_boards || group.canonical_board_count == 0U ||
          group.physical_public_outcome_count == 0U || group.fingerprint.empty()) {
        turn_groups_are_exact = false;
        break;
      }
      std::uint64_t group_physical_outcomes = 0U;
      for (std::uint64_t offset = 0U; offset < group.canonical_board_count; ++offset) {
        const auto &board = catalog.value().canonical_boards[catalog_first_span.first_board_index +
                                                             group.first_board_offset + offset];
        if (board.turn != group.turn) {
          turn_groups_are_exact = false;
          break;
        }
        group_physical_outcomes += board.physical_outcome_count;
      }
      if (!turn_groups_are_exact ||
          group_physical_outcomes != group.physical_public_outcome_count) {
        turn_groups_are_exact = false;
        break;
      }
      grouped_boards += group.canonical_board_count;
      grouped_physical_outcomes += group.physical_public_outcome_count;
    }
  }
  require(turn_groups_are_exact && grouped_boards == catalog_first_span.board_count &&
              grouped_physical_outcomes ==
                  static_cast<std::uint64_t>(
                      plan.canonical_flop_catalog[catalog_first_span.flop_catalog_index]
                          .physical_outcome_count) *
                      33U * 32U,
          "task-local Turn groups partition canonical River boards before BR maximization");
  std::vector<std::uint8_t> turn_major_root_coverage(
      static_cast<std::size_t>(catalog_first_span.resolver_root_count), 0U);
  bool turn_major_spans_are_exact = turn_groups.has_value();
  if (turn_groups) {
    for (const auto &group : turn_groups.value()) {
      const auto spans =
          gtosd::enumerate_hu_preflop_river_turn_group_resolver_spans(catalog.value(), group);
      if (!spans || spans.value().size() != catalog_first_span.shape_count) {
        turn_major_spans_are_exact = false;
        break;
      }
      for (const auto &span : spans.value()) {
        if (span.root_catalog_fingerprint != catalog.value().fingerprint ||
            span.turn_group_fingerprint != group.fingerprint || span.task_span_index != 0U ||
            span.resolver_root_count % 2U != 0U || span.fingerprint.empty() ||
            span.first_resolver_root < catalog_first_span.first_resolver_root ||
            span.first_resolver_root + span.resolver_root_count >
                catalog_first_span.first_resolver_root + catalog_first_span.resolver_root_count) {
          turn_major_spans_are_exact = false;
          break;
        }
        for (std::uint64_t root = span.first_resolver_root;
             root < span.first_resolver_root + span.resolver_root_count; ++root) {
          auto &coverage = turn_major_root_coverage[static_cast<std::size_t>(
              root - catalog_first_span.first_resolver_root)];
          if (coverage != 0U) {
            turn_major_spans_are_exact = false;
            break;
          }
          coverage = 1U;
        }
        if (!turn_major_spans_are_exact) {
          break;
        }
      }
      if (!turn_major_spans_are_exact) {
        break;
      }
    }
  }
  require(turn_major_spans_are_exact &&
              std::ranges::all_of(turn_major_root_coverage,
                                  [](const auto count) { return count == 1U; }),
          "Turn-major resolver spans cover every task root exactly once without materialization");
  auto invalid_turn_group_catalog = catalog.value();
  invalid_turn_group_catalog.fingerprint.clear();
  require(!gtosd::enumerate_hu_preflop_river_turn_groups(invalid_turn_group_catalog, 0U) &&
              !gtosd::enumerate_hu_preflop_river_turn_groups(catalog.value(),
                                                             catalog.value().task_spans.size()),
          "Turn grouping rejects a corrupt catalog and an out-of-range task");
  auto invalid_turn_group = turn_groups.value().front();
  invalid_turn_group.fingerprint.clear();
  require(!gtosd::enumerate_hu_preflop_river_turn_group_resolver_spans(catalog.value(),
                                                                       invalid_turn_group),
          "Turn-major resolver scheduling rejects a tampered group identity");
  const auto best_response_terminals =
      gtosd::enumerate_hu_postflop_upper_street_terminal_shapes(tree);

  require(best_response_terminals.has_value(),
          "upper-street terminal catalog is available for BR leaves");
  const auto flop_fold_terminal =
      std::ranges::find_if(best_response_terminals.value(), [&](const auto &shape) {
        return shape.entry_node == catalog_first_span.entry_node &&
               shape.street == gtosd::Street::Flop &&
               shape.state.status == gtosd::HandStatus::Folded;
      });
  const auto turn_fold_terminal =
      std::ranges::find_if(best_response_terminals.value(), [&](const auto &shape) {
        return shape.entry_node == catalog_first_span.entry_node &&
               shape.street == gtosd::Street::Turn &&
               shape.state.status == gtosd::HandStatus::Folded;
      });
  require(flop_fold_terminal != best_response_terminals.value().end() &&
              turn_fold_terminal != best_response_terminals.value().end(),
          "BR terminal fixture finds folded leaves on Flop and Turn");
  std::array<std::uint64_t, 2> br_probability_calls{};
  const gtosd::HuPostflopActionProbabilityProvider br_probability_provider =
      [&](const gtosd::PublicState &state, const std::span<const gtosd::Action>,
          const gtosd::Action &, const gtosd::ComboId) {
        ++br_probability_calls[state.player_to_act];
        return gtosd::Result<double, gtosd::HuPreflopError>::success(1.0);
      };
  const auto flop_br_terminal =
      gtosd::evaluate_hu_preflop_upper_street_best_response_terminal_contribution(
          tree, plan, catalog.value(), 0U, *flop_fold_terminal, nullptr, br_probability_provider,
          0U, 13U, "continuation:test:best-response:v1");
  require(flop_br_terminal.has_value() && flop_br_terminal.value().responding_player == 0U &&
              !flop_br_terminal.value().has_turn &&
              flop_br_terminal.value().continuation_fingerprint ==
                  "continuation:test:best-response:v1" &&
              flop_br_terminal.value().values.size() == 528U && br_probability_calls[0] == 0U &&
              br_probability_calls[1] > 0U &&
              gtosd::validate_hu_preflop_upper_street_best_response_terminal_contribution(
                  tree, plan, catalog.value(), flop_br_terminal.value())
                  .has_value(),
          "Flop BR terminal follows only opponent reach and validates independently");
  const auto sampled_policy = make_uniform_sampled_postflop_policy(tree, 13U);
  const auto sampled_flop_br_terminal = gtosd::
      evaluate_hu_preflop_upper_street_best_response_terminal_contribution_from_sampled_policy(
          tree, plan, catalog.value(), 0U, *flop_fold_terminal, nullptr, sampled_policy, 0U);
  require(sampled_flop_br_terminal.has_value() &&
              sampled_flop_br_terminal.value().continuation_fingerprint ==
                  sampled_policy.fingerprint &&
              sampled_flop_br_terminal.value().blueprint_iterations == sampled_policy.iterations &&
              gtosd::validate_hu_preflop_upper_street_best_response_terminal_contribution(
                  tree, plan, catalog.value(), sampled_flop_br_terminal.value()),
          "sampled policy bridge fixes only the opponent at an upper-street BR terminal");
  br_probability_calls.fill(0U);
  const auto turn_br_terminal =
      gtosd::evaluate_hu_preflop_upper_street_best_response_terminal_contribution(
          tree, plan, catalog.value(), 0U, *turn_fold_terminal, &turn_groups.value().front(),
          br_probability_provider, 0U, 13U, "continuation:test:best-response:v1");
  require(turn_br_terminal.has_value() && turn_br_terminal.value().has_turn &&
              turn_br_terminal.value().continuation_fingerprint ==
                  flop_br_terminal.value().continuation_fingerprint &&
              turn_br_terminal.value().turn == turn_groups.value().front().turn &&
              turn_br_terminal.value().turn_group_fingerprint ==
                  turn_groups.value().front().fingerprint &&
              !turn_br_terminal.value().turn_components.empty() &&
              turn_br_terminal.value().values.size() == 528U &&
              std::ranges::any_of(turn_br_terminal.value().values,
                                  [](const auto &value) { return value.positive_reach; }) &&
              br_probability_calls[0] == 0U && br_probability_calls[1] > 0U &&
              gtosd::validate_hu_preflop_upper_street_best_response_terminal_contribution(
                  tree, plan, catalog.value(), turn_br_terminal.value())
                  .has_value(),
          "Turn BR terminal remains separated by observed canonical Turn");
  const auto same_br_rows = [](const auto &first, const auto &second) {
    return first.size() == second.size() &&
           std::equal(
               first.begin(), first.end(), second.begin(), [](const auto &left, const auto &right) {
                 return left.responding_combo == right.responding_combo &&
                        left.weighted_counterfactual_reach == right.weighted_counterfactual_reach &&
                        left.weighted_counterfactual_utility_antes ==
                            right.weighted_counterfactual_utility_antes &&
                        left.conditional_value_antes == right.conditional_value_antes &&
                        left.positive_reach == right.positive_reach;
               });
  };
  gtosd::HuPreflopBestResponseLeafQuery flop_leaf_query;
  flop_leaf_query.kind = gtosd::HuPreflopBestResponseLeafKind::UpperStreetTerminal;
  flop_leaf_query.task_span_index = 0U;
  flop_leaf_query.entry_node = catalog_first_span.entry_node;
  flop_leaf_query.flop = flop_br_terminal.value().flop;
  flop_leaf_query.responding_player = 0U;
  flop_leaf_query.state = flop_fold_terminal->state;
  for (const auto card : flop_leaf_query.flop) {
    flop_leaf_query.state.board_mask |= card.mask();
  }
  flop_leaf_query.action_history = flop_fold_terminal->action_history;
  flop_leaf_query.history_fingerprint = flop_fold_terminal->history_fingerprint;
  const auto selected_flop_leaf = gtosd::select_hu_preflop_upper_street_best_response_leaf(
      tree, plan, catalog.value(), flop_br_terminal.value(), flop_leaf_query);

  auto turn_leaf_query = flop_leaf_query;
  turn_leaf_query.flop = turn_br_terminal.value().flop;
  turn_leaf_query.turn = turn_br_terminal.value().turn_components.front().turn;
  turn_leaf_query.has_turn = true;
  turn_leaf_query.state = turn_fold_terminal->state;
  for (const auto card : turn_leaf_query.flop) {
    turn_leaf_query.state.board_mask |= card.mask();
  }
  turn_leaf_query.state.board_mask |= turn_leaf_query.turn.mask();
  turn_leaf_query.action_history = turn_fold_terminal->action_history;
  turn_leaf_query.history_fingerprint = turn_fold_terminal->history_fingerprint;
  const auto selected_turn_leaf = gtosd::select_hu_preflop_upper_street_best_response_leaf(
      tree, plan, catalog.value(), turn_br_terminal.value(), turn_leaf_query);
  require(selected_flop_leaf.has_value() && selected_turn_leaf.has_value() &&
              same_br_rows(selected_flop_leaf.value(), flop_br_terminal.value().values) &&
              same_br_rows(selected_turn_leaf.value(),
                           turn_br_terminal.value().turn_components.front().values),
          "BR leaf selector maps recursive physical observations to exact terminal rows");
  turn_leaf_query.history_fingerprint.clear();
  require(!gtosd::select_hu_preflop_upper_street_best_response_leaf(
              tree, plan, catalog.value(), turn_br_terminal.value(), turn_leaf_query),
          "BR leaf selector rejects a mismatched recursive history");
  const gtosd::HuPostflopActionProbabilityProvider unit_probability_provider =
      [](const gtosd::PublicState &, const std::span<const gtosd::Action>, const gtosd::Action &,
         const gtosd::ComboId) {
        return gtosd::Result<double, gtosd::HuPreflopError>::success(1.0);
      };
  const auto physical_turn_terminal = gtosd::evaluate_hu_preflop_upper_street_terminal_contribution(
      tree, plan, 0U, catalog.value().canonical_boards[catalog_first_span.first_board_index].flop,
      turn_fold_terminal->terminal_ordinal, *turn_fold_terminal, unit_probability_provider, 13U,
      "continuation:test:average:v1");
  require(physical_turn_terminal.has_value(), "physical all-Turn terminal oracle evaluates");
  std::array<double, 630U> grouped_turn_reach{};
  std::array<double, 630U> grouped_turn_utility{};
  std::array<std::uint32_t, 36U> physical_turn_coverage{};
  bool every_turn_group_evaluated = true;
  for (const auto &group : turn_groups.value()) {
    const auto contribution =
        gtosd::evaluate_hu_preflop_upper_street_best_response_terminal_contribution(
            tree, plan, catalog.value(), 0U, *turn_fold_terminal, &group, unit_probability_provider,
            0U, 13U, "continuation:test:best-response:v1");
    if (!contribution) {
      every_turn_group_evaluated = false;
      break;
    }
    for (const auto &component : contribution.value().turn_components) {
      if (component.turn.value() >= physical_turn_coverage.size()) {
        every_turn_group_evaluated = false;
        break;
      }
      ++physical_turn_coverage[component.turn.value()];
    }
    if (!every_turn_group_evaluated) {
      break;
    }
    for (const auto &value : contribution.value().values) {
      grouped_turn_reach[value.responding_combo] += value.weighted_counterfactual_reach;
      grouped_turn_utility[value.responding_combo] += value.weighted_counterfactual_utility_antes;
    }
  }
  const auto first_task_flop =
      catalog.value().canonical_boards[catalog_first_span.first_board_index].flop;
  std::array<bool, 36U> flop_cards{};
  for (const auto card : first_task_flop) {
    flop_cards[card.value()] = true;
  }
  bool physical_turns_partitioned = every_turn_group_evaluated;
  for (std::size_t card = 0U; card < physical_turn_coverage.size(); ++card) {
    const auto expected = flop_cards[card] ? 0U : 1U;
    if (physical_turn_coverage[card] != expected) {
      physical_turns_partitioned = false;
      break;
    }
  }
  require(physical_turns_partitioned, "Turn BR components cover every physical Turn exactly once");
  bool turn_partition_recomposes = every_turn_group_evaluated;
  for (const auto &physical : physical_turn_terminal.value().resolver_values[1U]) {
    const auto reach_tolerance =
        1.0e-10 * std::max(1.0, std::abs(physical.weighted_counterfactual_reach));
    const auto utility_tolerance =
        1.0e-10 * std::max(1.0, std::abs(physical.weighted_counterfactual_utility_antes));
    if (std::abs(grouped_turn_reach[physical.opponent_combo] -
                 physical.weighted_counterfactual_reach) > reach_tolerance ||
        std::abs(grouped_turn_utility[physical.opponent_combo] -
                 physical.weighted_counterfactual_utility_antes) > utility_tolerance) {
      turn_partition_recomposes = false;
      break;
    }
  }
  require(turn_partition_recomposes,
          "canonical Turn BR leaves recompose the physical all-Turn oracle per combo");
  auto inconsistent_br_terminal = turn_br_terminal.value();
  auto inconsistent_row =
      std::ranges::find_if(inconsistent_br_terminal.turn_components.front().values,
                           [](const auto &value) { return value.positive_reach; });
  require(inconsistent_row != inconsistent_br_terminal.turn_components.front().values.end(),
          "Turn BR component exposes a positive-reach row for integrity testing");
  inconsistent_row->weighted_counterfactual_utility_antes += 1.0;
  inconsistent_row->conditional_value_antes =
      inconsistent_row->weighted_counterfactual_utility_antes /
      inconsistent_row->weighted_counterfactual_reach;
  inconsistent_br_terminal.fingerprint =
      gtosd::fingerprint_hu_preflop_upper_street_best_response_terminal_contribution(
          inconsistent_br_terminal);
  require(!gtosd::validate_hu_preflop_upper_street_best_response_terminal_contribution(
              tree, plan, catalog.value(), inconsistent_br_terminal),
          "Turn BR validation rejects a fingerprint-consistent component/aggregate mismatch");
  auto tampered_br_terminal = turn_br_terminal.value();
  tampered_br_terminal.turn_group_fingerprint.clear();
  require(!gtosd::validate_hu_preflop_upper_street_best_response_terminal_contribution(
              tree, plan, catalog.value(), tampered_br_terminal) &&
              !gtosd::evaluate_hu_preflop_upper_street_best_response_terminal_contribution(
                  tree, plan, catalog.value(), 0U, *turn_fold_terminal, nullptr,
                  br_probability_provider, 0U, 13U, "continuation:test:best-response:v1"),
          "BR terminal rejects a missing Turn partition and a tampered identity");
  const auto first_batch_roots = gtosd::enumerate_hu_preflop_river_batch_roots(
      catalog.value(), batches.value(), first.value());
  require(first_batch_roots.has_value() &&
              first_batch_roots.value().size() == first.value().resolver_root_count &&
              first_batch_roots.value().front().ordinal == first.value().first_resolver_root &&
              first_batch_roots.value().back().ordinal + 1U ==
                  first.value().first_resolver_root + first.value().resolver_root_count &&
              first_batch_roots.value().front().entry_node ==
                  first_batch_roots.value().back().entry_node &&
              first_batch_roots.value().front().flop == first_batch_roots.value().back().flop,
          "one bounded batch enumerates a contiguous task-local resolver-root range");
  const auto root_zero =
      gtosd::hu_preflop_river_resolver_root_at(catalog.value(), batches.value(), 0U);
  const auto root_one =
      gtosd::hu_preflop_river_resolver_root_at(catalog.value(), batches.value(), 1U);
  const auto root_two =
      gtosd::hu_preflop_river_resolver_root_at(catalog.value(), batches.value(), 2U);
  const auto root_last = gtosd::hu_preflop_river_resolver_root_at(
      catalog.value(), batches.value(), batches.value().resolver_roots - 1U);
  require(root_zero.has_value() && root_one.has_value() && root_two.has_value() &&
              root_last.has_value() && root_zero.value().resolving_player == 0U &&
              root_one.value().resolving_player == 1U &&
              root_zero.value().entry_node == root_one.value().entry_node &&
              root_zero.value().flop == root_one.value().flop &&
              root_zero.value().turn == root_one.value().turn &&
              root_zero.value().river == root_one.value().river &&
              root_zero.value().physical_public_outcome_count ==
                  root_one.value().physical_public_outcome_count &&
              root_zero.value().action_history == root_one.value().action_history &&
              !root_zero.value().action_history.empty() &&
              root_zero.value().task_span_index == 0U &&
              root_zero.value().task_first_resolver_root == 0U &&
              root_zero.value().task_resolver_root_count ==
                  catalog.value().task_spans.front().resolver_root_count &&
              root_zero.value().physical_public_outcome_count > 0U &&
              root_two.value().resolving_player == 0U &&
              (root_two.value().turn != root_zero.value().turn ||
               root_two.value().river != root_zero.value().river) &&
              std::popcount(root_zero.value().state.board_mask) == 5 &&
              root_last.value().ordinal + 1U == batches.value().resolver_roots &&
              root_last.value().task_span_index + 1U == catalog.value().task_spans.size() &&
              root_last.value().resolving_player == 1U && !root_last.value().fingerprint.empty(),
          "River root ordinals resolve deterministically to cards, history and player side");
  const auto river_config = gtosd::make_hu_preflop_river_postflop_config(tree, root_zero.value());
  require(river_config.has_value() && river_config.value().turn == root_zero.value().turn &&
              river_config.value().river == root_zero.value().river &&
              river_config.value().initial_pot == root_zero.value().state.pot &&
              river_config.value().effective_stack == root_zero.value().state.remaining_stacks[0] &&
              gtosd::validate_tree_config(river_config.value()),
          "resolved River root maps to an exact postflop configuration");

  gtosd::PostflopRootCounterfactualValues postflop_values;
  postflop_values.game_fingerprint = "fnv1a64:river-continuation-test";
  postflop_values.blueprint_iterations = 7U;
  const auto combos = gtosd::all_combos();
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    for (std::size_t combo = 0U; combo < combos.size(); ++combo) {
      const auto mask = combos[combo].first.mask() | combos[combo].second.mask();
      if ((mask & root_zero.value().state.board_mask) == 0U) {
        postflop_values.players[player].push_back(
            {static_cast<gtosd::ComboId>(combo), 1.0, 1.0,
             static_cast<double>(player) + static_cast<double>(combo + 1U) / 1'000.0, true});
      }
    }
  }
  gtosd::HuPreflopComboReach exact_sequence_reach;
  exact_sequence_reach.fill(1.0);
  std::array<gtosd::HuPreflopComboReach, 2> unit_postflop_action_reach;
  unit_postflop_action_reach[0].fill(1.0);
  unit_postflop_action_reach[1].fill(1.0);
  const auto conditioned_river = gtosd::condition_hu_preflop_ranges_on_river_root(
      plan, root_zero.value(), unit_postflop_action_reach);
  const auto root_entry = std::ranges::find_if(plan.entries, [&](const auto &entry) {
    return entry.entry_node == root_zero.value().entry_node;
  });
  require(conditioned_river.has_value() && root_entry != plan.entries.end() &&
              conditioned_river.value().live_positive_combo_count ==
                  std::array<std::uint64_t, 2>{465U, 465U} &&
              conditioned_river.value().compatible_joint_reach_mass > 0.0,
          "River conditioning composes preflop and postflop sequence reach");
  for (std::size_t combo = 0U; combo < combos.size(); ++combo) {
    const auto mask = combos[combo].first.mask() | combos[combo].second.mask();
    const auto expected = (mask & root_zero.value().state.board_mask) == 0U
                              ? root_entry->own_sequence_reach[0][combo]
                              : 0.0;
    require(conditioned_river.value().exact_sequence_reach[0][combo] == expected,
            "River conditioning preserves exact physical combo reach and blockers");
  }
  const auto selected_combo = static_cast<std::size_t>(
      std::distance(combos.begin(), std::ranges::find_if(combos, [&](const auto &combo) {
                      return ((combo.first.mask() | combo.second.mask()) &
                              root_zero.value().state.board_mask) == 0U;
                    })));
  std::array<std::uint64_t, 2> selected_combo_action_count{};
  bool provider_context_valid = true;
  const gtosd::HuPostflopActionProbabilityProvider probability_provider =
      [&](const gtosd::PublicState &state, const std::span<const gtosd::Action> prefix,
          const gtosd::Action &, const gtosd::ComboId combo) {
        provider_context_valid =
            provider_context_valid && state.player_to_act < 2U &&
            (std::popcount(state.board_mask) == 3 || std::popcount(state.board_mask) == 4) &&
            prefix.size() < root_zero.value().action_history.size();
        if (combo == selected_combo) {
          ++selected_combo_action_count[state.player_to_act];
          return gtosd::Result<double, gtosd::HuPreflopError>::success(
              state.player_to_act == 0U ? 0.5 : 0.25);
        }
        return gtosd::Result<double, gtosd::HuPreflopError>::success(1.0);
      };
  const auto derived_reach = gtosd::derive_hu_preflop_river_conditioned_reach(
      tree, plan, root_zero.value(), probability_provider);
  require(selected_combo < combos.size() && derived_reach.has_value() && provider_context_valid &&
              selected_combo_action_count[0] > 0U && selected_combo_action_count[1] > 0U &&
              derived_reach.value().exact_sequence_reach[0][selected_combo] ==
                  root_entry->own_sequence_reach[0][selected_combo] *
                      std::pow(0.5, static_cast<double>(selected_combo_action_count[0])) &&
              derived_reach.value().exact_sequence_reach[1][selected_combo] ==
                  root_entry->own_sequence_reach[1][selected_combo] *
                      std::pow(0.25, static_cast<double>(selected_combo_action_count[1])) &&
              derived_reach.value().postflop_action_sequence_reach[0][selected_combo] ==
                  std::pow(0.5, static_cast<double>(selected_combo_action_count[0])) &&
              derived_reach.value().postflop_action_sequence_reach[1][selected_combo] ==
                  std::pow(0.25, static_cast<double>(selected_combo_action_count[1])),
          "replayed Flop/Turn policy multiplies only the acting player's exact combo reach");
  const auto sampled_reach_policy = make_uniform_sampled_postflop_policy(tree, 7U);
  const auto sampled_derived_reach =
      gtosd::derive_hu_preflop_river_conditioned_reach_from_sampled_policy(
          tree, plan, root_zero.value(), sampled_reach_policy);
  auto corrupt_sampled_reach_policy = sampled_reach_policy;
  corrupt_sampled_reach_policy.fingerprint = "fnv1a64:corrupt";
  require(sampled_derived_reach.has_value() &&
              sampled_derived_reach.value().live_positive_combo_count ==
                  std::array<std::uint64_t, 2>{465U, 465U} &&
              sampled_derived_reach.value().compatible_joint_reach_mass > 0.0 &&
              !gtosd::derive_hu_preflop_river_conditioned_reach_from_sampled_policy(
                  tree, plan, root_zero.value(), corrupt_sampled_reach_policy),
          "sampled policy bridge propagates exact physical River reaches and rejects corruption");

  const gtosd::HuPostflopActionProbabilityProvider invalid_probability_provider =
      [](const gtosd::PublicState &, const std::span<const gtosd::Action>, const gtosd::Action &,
         const gtosd::ComboId) {
        return gtosd::Result<double, gtosd::HuPreflopError>::success(1.01);
      };
  require(!gtosd::derive_hu_preflop_river_conditioned_reach(tree, plan, root_zero.value(),
                                                            invalid_probability_provider),
          "River reach propagation rejects action probabilities outside the simplex");
  const auto boundary_zero = gtosd::build_hu_preflop_river_root_boundary(
      tree, plan, root_zero.value(), postflop_values, exact_sequence_reach,
      unit_postflop_action_reach[1], postflop_values.game_fingerprint);
  const auto boundary_one = gtosd::build_hu_preflop_river_root_boundary(
      tree, plan, root_one.value(), postflop_values, exact_sequence_reach,
      unit_postflop_action_reach[0], postflop_values.game_fingerprint);
  const auto boundary_zero_reach_sum =
      boundary_zero ? std::accumulate(boundary_zero.value().values.begin(),
                                      boundary_zero.value().values.end(), 0.0,
                                      [](const double sum, const auto &value) {
                                        return sum + value.counterfactual_reach;
                                      })
                    : 0.0;
  require(boundary_zero.has_value(), "CO River resolver boundary builds after orbit lifting");
  require(boundary_one.has_value(), "BTN River resolver boundary builds after orbit lifting");
  require(boundary_zero.value().values.size() == 528U,
          "orbit-lifted River boundary covers every Flop-live opponent combo");
  require(boundary_zero.value().best_response_turn_components.empty() &&
              boundary_one.value().best_response_turn_components.empty(),
          "average-policy River boundaries do not retain BR-only Turn payloads");
  require(boundary_zero.value().resolving_player == 0U && boundary_zero.value().opponent == 1U &&
              boundary_one.value().resolving_player == 1U && boundary_one.value().opponent == 0U,
          "orbit-lifted River boundaries preserve resolver sides");
  require(boundary_zero_reach_sum ==
              static_cast<double>(root_zero.value().physical_public_outcome_count) * 465.0 * 406.0,
          "one canonical River boundary preserves the full physical-orbit reach mass");
  gtosd::HuPreflopComboReach half_opponent_action_reach;
  half_opponent_action_reach.fill(0.5);
  const auto half_boundary = gtosd::build_hu_preflop_river_root_boundary(
      tree, plan, root_zero.value(), postflop_values, exact_sequence_reach,
      half_opponent_action_reach, postflop_values.game_fingerprint);
  const auto half_boundary_reach_sum =
      half_boundary ? std::accumulate(half_boundary.value().values.begin(),
                                      half_boundary.value().values.end(), 0.0,
                                      [](const double sum, const auto &value) {
                                        return sum + value.counterfactual_reach;
                                      })
                    : 0.0;
  require(half_boundary.has_value() && half_boundary_reach_sum == boundary_zero_reach_sum * 0.5,
          "River aggregation includes the target hand's own Flop/Turn action reach");
  half_opponent_action_reach.front() = 1.01;
  require(!gtosd::build_hu_preflop_river_root_boundary(
              tree, plan, root_zero.value(), postflop_values, exact_sequence_reach,
              half_opponent_action_reach, postflop_values.game_fingerprint),
          "River boundary rejects invalid target-player continuation probabilities");
  auto best_response_values = postflop_values;
  best_response_values.mode = gtosd::PostflopRootValueMode::ExactBestResponse;
  const auto best_response_boundary_zero = gtosd::build_hu_preflop_river_root_boundary(
      tree, plan, root_zero.value(), best_response_values, exact_sequence_reach,
      unit_postflop_action_reach[1], best_response_values.game_fingerprint);
  const auto best_response_boundary_one = gtosd::build_hu_preflop_river_root_boundary(
      tree, plan, root_one.value(), best_response_values, exact_sequence_reach,
      unit_postflop_action_reach[0], best_response_values.game_fingerprint);
  require(best_response_boundary_zero.has_value() && best_response_boundary_one.has_value(),
          "exact best-response River boundaries build with physical Turn components");
  require(best_response_boundary_zero.value().value_mode ==
                  gtosd::HuPreflopContinuationValueMode::ExactBestResponse &&
              !best_response_boundary_zero.value().best_response_turn_components.empty() &&
              !best_response_boundary_one.value().best_response_turn_components.empty() &&
              best_response_boundary_zero.value().fingerprint != boundary_zero.value().fingerprint,
          "exact best-response values retain physical Turn components in a distinct channel");

  const auto &br_leaf_span = catalog.value().task_spans.front();
  const auto &br_leaf_shape = catalog.value().river_shapes[br_leaf_span.first_shape_index];
  gtosd::HuPreflopBestResponseLeafQuery br_leaf_query;
  br_leaf_query.kind = gtosd::HuPreflopBestResponseLeafKind::RiverContinuation;
  br_leaf_query.task_span_index = 0U;
  br_leaf_query.entry_node = br_leaf_span.entry_node;
  br_leaf_query.flop = catalog.value().canonical_boards[br_leaf_span.first_board_index].flop;
  br_leaf_query.turn =
      best_response_boundary_one.value().best_response_turn_components.front().turn;
  br_leaf_query.has_turn = true;
  br_leaf_query.responding_player = 0U;
  br_leaf_query.state = br_leaf_shape.state;
  br_leaf_query.state.board_mask = br_leaf_query.flop[0].mask() | br_leaf_query.flop[1].mask() |
                                   br_leaf_query.flop[2].mask() | br_leaf_query.turn.mask();
  br_leaf_query.action_history = br_leaf_shape.action_history;
  br_leaf_query.history_fingerprint = br_leaf_shape.history_fingerprint;
  auto br_leaf_accumulator = gtosd::make_hu_preflop_river_best_response_leaf_accumulator(
      tree, plan, catalog.value(), batches.value(), br_leaf_query,
      best_response_values.game_fingerprint, best_response_values.blueprint_iterations);
  require(br_leaf_accumulator.has_value() && !br_leaf_accumulator.value().complete &&
              !br_leaf_accumulator.value().expected_resolver_root_ordinals.empty() &&
              br_leaf_accumulator.value().expected_resolver_root_ordinals.size() <= 32U,
          "physical-Turn River BR leaf accumulator derives a bounded exact root manifest");

  const auto serialized_partial_br_leaf =
      gtosd::serialize_hu_preflop_river_best_response_leaf_accumulator(br_leaf_accumulator.value());
  const auto restored_partial_br_leaf =
      serialized_partial_br_leaf
          ? gtosd::deserialize_hu_preflop_river_best_response_leaf_accumulator(
                serialized_partial_br_leaf.value())
          : gtosd::Result<gtosd::HuPreflopRiverBestResponseLeafAccumulator,
                          gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
  auto legacy_partial_br_leaf = serialized_partial_br_leaf.value();
  legacy_partial_br_leaf.replace(
      legacy_partial_br_leaf.find("river_best_response_leaf_accumulator.v1"),
      std::string("river_best_response_leaf_accumulator.v1").size(),
      "river_best_response_leaf_accumulator.v0");
  require(restored_partial_br_leaf.has_value() && !restored_partial_br_leaf.value().complete &&
              restored_partial_br_leaf.value().fingerprint ==
                  br_leaf_accumulator.value().fingerprint &&
              gtosd::validate_hu_preflop_river_best_response_leaf_accumulator(
                  tree, plan, catalog.value(), batches.value(), restored_partial_br_leaf.value()) &&
              !gtosd::deserialize_hu_preflop_river_best_response_leaf_accumulator(
                  legacy_partial_br_leaf) &&
              !gtosd::deserialize_hu_preflop_river_best_response_leaf_accumulator(
                  serialized_partial_br_leaf.value() + "corrupt"),
          "partial River BR leaf accumulator round-trips and rejects legacy or trailing data");

  const auto br_leaf_path =
      std::filesystem::current_path() / "hu_preflop_river_br_leaf_accumulator_test.bin";
  std::error_code br_leaf_ignored;
  std::filesystem::remove(br_leaf_path, br_leaf_ignored);
  const auto saved_partial_br_leaf = gtosd::save_hu_preflop_river_best_response_leaf_accumulator(
      br_leaf_accumulator.value(), br_leaf_path.string());
  const auto loaded_partial_br_leaf =
      saved_partial_br_leaf
          ? gtosd::load_hu_preflop_river_best_response_leaf_accumulator(br_leaf_path.string())
          : gtosd::Result<gtosd::HuPreflopRiverBestResponseLeafAccumulator,
                          gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IoFailure);
  require(saved_partial_br_leaf && loaded_partial_br_leaf.has_value() &&
              loaded_partial_br_leaf.value().fingerprint == br_leaf_accumulator.value().fingerprint,
          "partial River BR leaf accumulator saves atomically with checksum");
  if (saved_partial_br_leaf) {
    std::ofstream corrupt_br_leaf(br_leaf_path, std::ios::binary | std::ios::app);
    corrupt_br_leaf << 'x';
  }
  require(!gtosd::load_hu_preflop_river_best_response_leaf_accumulator(br_leaf_path.string()),
          "River BR leaf checkpoint rejects file corruption");
  std::filesystem::remove(br_leaf_path, br_leaf_ignored);

  const auto make_unit_best_response_boundary =
      [&](const gtosd::HuPreflopBestResponseLeafQuery &query, const std::uint64_t ordinal)
      -> gtosd::Result<gtosd::HuPreflopRiverRootBoundary, gtosd::HuPreflopError> {
    const auto root =
        gtosd::hu_preflop_river_resolver_root_at(catalog.value(), batches.value(), ordinal);
    if (!root) {
      return gtosd::Result<gtosd::HuPreflopRiverRootBoundary, gtosd::HuPreflopError>::failure(
          root.error());
    }
    gtosd::PostflopRootCounterfactualValues unit_values;
    unit_values.game_fingerprint = best_response_values.game_fingerprint;
    unit_values.blueprint_iterations = best_response_values.blueprint_iterations;
    unit_values.mode = gtosd::PostflopRootValueMode::ExactBestResponse;
    for (std::uint8_t player = 0U; player < 2U; ++player) {
      for (std::size_t combo = 0U; combo < combos.size(); ++combo) {
        const auto mask = combos[combo].first.mask() | combos[combo].second.mask();
        if ((mask & root.value().state.board_mask) == 0U) {
          unit_values.players[player].push_back(
              {static_cast<gtosd::ComboId>(combo), 1.0, 1.0, 1.0, true});
        }
      }
    }
    return gtosd::build_hu_preflop_river_root_boundary(
        tree, plan, root.value(), unit_values, exact_sequence_reach,
        unit_postflop_action_reach[query.responding_player], unit_values.game_fingerprint);
  };

  bool br_leaf_reduction_ok = br_leaf_accumulator.has_value();
  bool duplicate_rejected = false;
  if (br_leaf_reduction_ok) {
    for (const auto ordinal : br_leaf_accumulator.value().expected_resolver_root_ordinals) {
      const auto boundary = make_unit_best_response_boundary(br_leaf_query, ordinal);
      if (!boundary ||
          !gtosd::accumulate_hu_preflop_river_best_response_leaf_boundary(
              catalog.value(), batches.value(), br_leaf_accumulator.value(), boundary.value())) {
        br_leaf_reduction_ok = false;
        break;
      }
      if (!duplicate_rejected) {
        auto duplicate_accumulator = br_leaf_accumulator.value();
        duplicate_rejected = !gtosd::accumulate_hu_preflop_river_best_response_leaf_boundary(
            catalog.value(), batches.value(), duplicate_accumulator, boundary.value());
      }
    }
  }
  const auto br_leaf_values =
      br_leaf_reduction_ok
          ? gtosd::finalize_hu_preflop_river_best_response_leaf_accumulator(
                tree, plan, catalog.value(), batches.value(), br_leaf_accumulator.value())
          : gtosd::Result<std::vector<gtosd::HuPreflopBestResponseComboValue>,
                          gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
  const auto br_flop_catalog = std::ranges::find(plan.canonical_flop_catalog, br_leaf_query.flop,
                                                 &gtosd::HuPreflopCanonicalFlop::cards);
  bool exact_physical_turn_leaf =
      br_leaf_values.has_value() && br_flop_catalog != plan.canonical_flop_catalog.end();
  if (exact_physical_turn_leaf) {
    const auto expected_reach =
        30.0 * 406.0 * static_cast<double>(br_flop_catalog->physical_outcome_count);
    for (const auto &value : br_leaf_values.value()) {
      const auto combo = combos[value.responding_combo];
      const auto blocked = combo.first == br_leaf_query.turn || combo.second == br_leaf_query.turn;
      const auto expected = blocked ? 0.0 : expected_reach;
      if (value.weighted_counterfactual_reach != expected ||
          value.weighted_counterfactual_utility_antes != expected ||
          value.conditional_value_antes != (blocked ? 0.0 : 1.0) ||
          value.positive_reach == blocked) {
        exact_physical_turn_leaf = false;
        break;
      }
    }
  }
  require(br_leaf_reduction_ok && br_leaf_accumulator.value().complete && duplicate_rejected &&
              exact_physical_turn_leaf,
          "River BR leaf streaming reduction preserves 30 physical Rivers per combo");

  const auto serialized_complete_br_leaf =
      gtosd::serialize_hu_preflop_river_best_response_leaf_accumulator(br_leaf_accumulator.value());
  const auto restored_complete_br_leaf =
      serialized_complete_br_leaf
          ? gtosd::deserialize_hu_preflop_river_best_response_leaf_accumulator(
                serialized_complete_br_leaf.value())
          : gtosd::Result<gtosd::HuPreflopRiverBestResponseLeafAccumulator,
                          gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
  require(restored_complete_br_leaf.has_value() && restored_complete_br_leaf.value().complete &&
              restored_complete_br_leaf.value().next_boundary_index ==
                  br_leaf_accumulator.value().next_boundary_index &&
              restored_complete_br_leaf.value().fingerprint ==
                  br_leaf_accumulator.value().fingerprint &&
              gtosd::validate_hu_preflop_river_best_response_leaf_accumulator(
                  tree, plan, catalog.value(), batches.value(), restored_complete_br_leaf.value()),
          "complete River BR leaf accumulator preserves exact resume state");

  const auto dispatch_flop_br_terminal =
      gtosd::evaluate_hu_preflop_upper_street_best_response_terminal_contribution(
          tree, plan, catalog.value(), 0U, *flop_fold_terminal, nullptr, br_probability_provider,
          0U, best_response_values.blueprint_iterations, best_response_values.game_fingerprint);
  require(dispatch_flop_br_terminal.has_value(),
          "BR dispatcher fixture binds terminal and River leaves to one blueprint iteration");
  std::uint64_t terminal_provider_calls = 0U;
  std::uint64_t river_boundary_provider_calls = 0U;
  const gtosd::HuPreflopBestResponseTerminalContributionProvider terminal_contribution_provider =
      [&](const gtosd::HuPreflopBestResponseLeafQuery &query)
      -> gtosd::Result<gtosd::HuPreflopUpperStreetBestResponseTerminalContribution,
                       gtosd::HuPreflopError> {
    ++terminal_provider_calls;
    if (query.history_fingerprint != flop_leaf_query.history_fingerprint) {
      return gtosd::Result<gtosd::HuPreflopUpperStreetBestResponseTerminalContribution,
                           gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
    }
    return gtosd::Result<gtosd::HuPreflopUpperStreetBestResponseTerminalContribution,
                         gtosd::HuPreflopError>::success(dispatch_flop_br_terminal.value());
  };
  const gtosd::HuPreflopBestResponseRiverBoundaryProvider river_boundary_provider =
      [&](const gtosd::HuPreflopBestResponseLeafQuery &query, const std::uint64_t ordinal) {
        ++river_boundary_provider_calls;
        return make_unit_best_response_boundary(query, ordinal);
      };
  const auto dispatched_terminal = gtosd::resolve_hu_preflop_best_response_leaf_streaming(
      tree, plan, catalog.value(), batches.value(), flop_leaf_query, terminal_contribution_provider,
      river_boundary_provider, best_response_values.game_fingerprint,
      best_response_values.blueprint_iterations);
  const auto dispatched_river = gtosd::resolve_hu_preflop_best_response_leaf_streaming(
      tree, plan, catalog.value(), batches.value(), br_leaf_query, terminal_contribution_provider,
      river_boundary_provider, best_response_values.game_fingerprint,
      best_response_values.blueprint_iterations);
  require(dispatched_terminal.has_value() && dispatched_river.has_value() &&
              same_br_rows(dispatched_terminal.value(), selected_flop_leaf.value()) &&
              same_br_rows(dispatched_river.value(), br_leaf_values.value()) &&
              terminal_provider_calls == 1U &&
              river_boundary_provider_calls ==
                  br_leaf_accumulator.value().expected_resolver_root_ordinals.size(),
          "bounded BR leaf dispatcher selects one terminal or streams its exact River manifest");

  std::filesystem::remove(br_leaf_path, br_leaf_ignored);
  std::uint64_t interrupted_boundary_calls = 0U;
  const gtosd::HuPreflopBestResponseRiverBoundaryProvider interrupted_boundary_provider =
      [&](const gtosd::HuPreflopBestResponseLeafQuery &query, const std::uint64_t ordinal) {
        ++interrupted_boundary_calls;
        return interrupted_boundary_calls == 1U
                   ? make_unit_best_response_boundary(query, ordinal)
                   : gtosd::Result<gtosd::HuPreflopRiverRootBoundary, gtosd::HuPreflopError>::
                         failure(gtosd::HuPreflopError::IoFailure);
      };
  const gtosd::HuPreflopRiverBestResponseLeafCheckpointSink br_leaf_checkpoint_sink =
      [&](const gtosd::HuPreflopRiverBestResponseLeafAccumulator &checkpoint) {
        return gtosd::save_hu_preflop_river_best_response_leaf_accumulator(checkpoint,
                                                                           br_leaf_path.string());
      };
  const auto interrupted_br_leaf = gtosd::resolve_hu_preflop_best_response_leaf_streaming(
      tree, plan, catalog.value(), batches.value(), br_leaf_query, terminal_contribution_provider,
      interrupted_boundary_provider, best_response_values.game_fingerprint,
      best_response_values.blueprint_iterations, nullptr, br_leaf_checkpoint_sink);
  const auto persisted_br_leaf =
      gtosd::load_hu_preflop_river_best_response_leaf_accumulator(br_leaf_path.string());
  std::uint64_t resumed_boundary_calls = 0U;
  const gtosd::HuPreflopBestResponseRiverBoundaryProvider resumed_boundary_provider =
      [&](const gtosd::HuPreflopBestResponseLeafQuery &query, const std::uint64_t ordinal) {
        ++resumed_boundary_calls;
        return make_unit_best_response_boundary(query, ordinal);
      };
  const auto resumed_br_leaf =
      persisted_br_leaf
          ? gtosd::resolve_hu_preflop_best_response_leaf_streaming(
                tree, plan, catalog.value(), batches.value(), br_leaf_query,
                terminal_contribution_provider, resumed_boundary_provider,
                best_response_values.game_fingerprint, best_response_values.blueprint_iterations,
                &persisted_br_leaf.value(), br_leaf_checkpoint_sink)
          : gtosd::Result<std::vector<gtosd::HuPreflopBestResponseComboValue>,
                          gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IoFailure);
  require(!interrupted_br_leaf && persisted_br_leaf.has_value() &&
              persisted_br_leaf.value().next_boundary_index == 1U && resumed_br_leaf.has_value() &&
              same_br_rows(resumed_br_leaf.value(), br_leaf_values.value()) &&
              resumed_boundary_calls + persisted_br_leaf.value().next_boundary_index ==
                  br_leaf_accumulator.value().expected_resolver_root_ordinals.size(),
          "River BR leaf resumes from the last atomically persisted boundary without replay");
  std::filesystem::remove(br_leaf_path, br_leaf_ignored);

  auto mixed_checkpoint_terminal = dispatch_flop_br_terminal.value();
  mixed_checkpoint_terminal.continuation_fingerprint =
      "fnv1a64:different-best-response-continuation";
  mixed_checkpoint_terminal.fingerprint =
      gtosd::fingerprint_hu_preflop_upper_street_best_response_terminal_contribution(
          mixed_checkpoint_terminal);
  const gtosd::HuPreflopBestResponseTerminalContributionProvider
      mixed_checkpoint_terminal_provider = [&](const gtosd::HuPreflopBestResponseLeafQuery &) {
        return gtosd::Result<gtosd::HuPreflopUpperStreetBestResponseTerminalContribution,
                             gtosd::HuPreflopError>::success(mixed_checkpoint_terminal);
      };
  require(!gtosd::resolve_hu_preflop_best_response_leaf_streaming(
              tree, plan, catalog.value(), batches.value(), flop_leaf_query,
              mixed_checkpoint_terminal_provider, river_boundary_provider,
              best_response_values.game_fingerprint, best_response_values.blueprint_iterations),
          "bounded BR dispatcher rejects an upper-street terminal from another continuation");

  const gtosd::HuPreflopBestResponseRiverBoundaryProvider corrupt_boundary_provider =
      [&](const gtosd::HuPreflopBestResponseLeafQuery &query, const std::uint64_t ordinal) {
        auto boundary = make_unit_best_response_boundary(query, ordinal);
        if (boundary) {
          boundary.value().fingerprint.clear();
        }
        return boundary;
      };
  const gtosd::HuPreflopBestResponseRiverBoundaryProvider missing_boundary_provider;
  require(!gtosd::resolve_hu_preflop_best_response_leaf_streaming(
              tree, plan, catalog.value(), batches.value(), br_leaf_query,
              terminal_contribution_provider, corrupt_boundary_provider,
              best_response_values.game_fingerprint, best_response_values.blueprint_iterations) &&
              !gtosd::evaluate_hu_preflop_best_response_task_streaming(
                  tree, plan, catalog.value(), batches.value(), 0U, terminal_contribution_provider,
                  missing_boundary_provider, best_response_values.game_fingerprint, 0U,
                  best_response_values.blueprint_iterations),
          "bounded BR dispatcher rejects a corrupt boundary and a missing task source");
  auto tampered_br_leaf_accumulator = br_leaf_accumulator.value();
  ++tampered_br_leaf_accumulator.expected_resolver_root_ordinals.front();
  tampered_br_leaf_accumulator.fingerprint =
      gtosd::fingerprint_hu_preflop_river_best_response_leaf_accumulator(
          tampered_br_leaf_accumulator);
  require(!gtosd::validate_hu_preflop_river_best_response_leaf_accumulator(
              tree, plan, catalog.value(), batches.value(), tampered_br_leaf_accumulator),
          "River BR leaf validation rejects a fingerprint-consistent root manifest change");

  // With unit reach and unit utility, a fixed opponent combo that is live on
  // the Flop has 31*30 compatible ordered public runouts. Every such River
  // board leaves C(29,2)=406 compatible resolving-player combos. This checks
  // the result per physical combo, which a scalar orbit multiplicity cannot do.
  std::array<double, 630U> orbit_lifted_reach{};
  const auto &first_span = catalog.value().task_spans.front();
  bool orbit_lift_ok = true;
  for (std::uint64_t board = 0U; board < first_span.board_count && orbit_lift_ok; ++board) {
    const auto root_offset =
        first_span.first_resolver_root + board * 2U - first.value().first_resolver_root;
    if (root_offset >= first_batch_roots.value().size()) {
      orbit_lift_ok = false;
      break;
    }
    const auto &root = first_batch_roots.value()[root_offset];
    gtosd::PostflopRootCounterfactualValues unit_values;
    unit_values.game_fingerprint = "fnv1a64:river-orbit-lift-oracle";
    unit_values.blueprint_iterations = 7U;
    for (std::uint8_t player = 0U; player < 2U; ++player) {
      for (std::size_t combo = 0U; combo < combos.size(); ++combo) {
        const auto mask = combos[combo].first.mask() | combos[combo].second.mask();
        if ((mask & root.state.board_mask) == 0U) {
          unit_values.players[player].push_back(
              {static_cast<gtosd::ComboId>(combo), 1.0, 1.0, 1.0, true});
        }
      }
    }
    const auto lifted = gtosd::build_hu_preflop_river_root_boundary(
        tree, plan, root, unit_values, exact_sequence_reach,
        unit_postflop_action_reach[1U - root.resolving_player], unit_values.game_fingerprint);
    if (!lifted || lifted.value().values.size() != 528U) {
      orbit_lift_ok = false;
      break;
    }
    for (const auto &value : lifted.value().values) {
      orbit_lifted_reach[value.opponent_combo] += value.counterfactual_reach;
    }
  }
  const auto first_flop_mask = root_zero.value().flop[0].mask() | root_zero.value().flop[1].mask() |
                               root_zero.value().flop[2].mask();
  const auto canonical_flop_entry = std::ranges::find(
      plan.canonical_flop_catalog, root_zero.value().flop, &gtosd::HuPreflopCanonicalFlop::cards);
  require(canonical_flop_entry != plan.canonical_flop_catalog.end(),
          "River task Flop belongs to the canonical decomposition catalog");
  const auto expected_combo_reach =
      static_cast<double>(canonical_flop_entry->physical_outcome_count) * 31.0 * 30.0 * 406.0;
  for (std::size_t combo = 0U; combo < combos.size() && orbit_lift_ok; ++combo) {
    const auto mask = combos[combo].first.mask() | combos[combo].second.mask();
    const auto expected = (mask & first_flop_mask) == 0U ? expected_combo_reach : 0.0;
    orbit_lift_ok = orbit_lifted_reach[combo] == expected;
  }
  require(orbit_lift_ok,
          "canonical River orbit lift preserves the exact blocker mass of every physical combo");
  const std::function<void()> verify_task_accumulators = [&]() {
    auto accumulator = gtosd::make_hu_preflop_river_task_accumulator(
        catalog.value(), batches.value(), 0U, postflop_values.blueprint_iterations);
    require(accumulator.has_value(), "task-local River accumulator binds catalog and blueprint");
    auto best_response_accumulator = gtosd::make_hu_preflop_river_task_accumulator(
        catalog.value(), batches.value(), 0U, postflop_values.blueprint_iterations,
        gtosd::HuPreflopContinuationValueMode::ExactBestResponse);
    require(best_response_accumulator.has_value() &&
                best_response_accumulator.value().fingerprint != accumulator.value().fingerprint,
            "best-response River accumulator has a mode-specific identity");
    auto numerical_accumulator = accumulator.value();
    numerical_accumulator.resolver_root_count = 2U;
    numerical_accumulator.fingerprint =
        gtosd::fingerprint_hu_preflop_river_task_accumulator(numerical_accumulator);
    auto numerical_best_response_accumulator = best_response_accumulator.value();
    numerical_best_response_accumulator.resolver_root_count = 2U;
    numerical_best_response_accumulator.fingerprint =
        gtosd::fingerprint_hu_preflop_river_task_accumulator(numerical_best_response_accumulator);
    auto inconsistent_best_response_boundary = best_response_boundary_zero.value();
    auto inconsistent_component_value = std::ranges::find_if(
        inconsistent_best_response_boundary.best_response_turn_components.front().values,
        [](const auto &value) { return value.positive_reach; });
    require(
        inconsistent_component_value !=
            inconsistent_best_response_boundary.best_response_turn_components.front().values.end(),
        "River BR Turn component exposes positive reach for integrity testing");
    inconsistent_component_value->blueprint_counterfactual_value_antes += 1.0;
    inconsistent_best_response_boundary.fingerprint =
        gtosd::fingerprint_hu_preflop_river_root_boundary(inconsistent_best_response_boundary);
    auto inconsistent_best_response_accumulator = numerical_best_response_accumulator;
    require(!gtosd::accumulate_hu_preflop_river_root_boundary(
                inconsistent_best_response_accumulator, inconsistent_best_response_boundary),
            "River BR reduction rejects a fingerprint-consistent component/aggregate mismatch");
    auto mismatched_accumulator = numerical_accumulator;
    require(!gtosd::accumulate_hu_preflop_river_root_boundary(mismatched_accumulator,
                                                              best_response_boundary_zero.value()),
            "average-policy accumulator rejects a best-response River boundary");
    require(!gtosd::accumulate_hu_preflop_river_root_boundary(numerical_accumulator,
                                                              boundary_one.value()),
            "River accumulator rejects an out-of-order resolver side");
    auto mixed_checkpoint_accumulator = numerical_accumulator;
    require(gtosd::accumulate_hu_preflop_river_root_boundary(mixed_checkpoint_accumulator,
                                                             boundary_zero.value())
                .has_value(),
            "River accumulator binds the first continuation checkpoint");
    const auto before_mixed_checkpoint =
        gtosd::fingerprint_hu_preflop_river_task_accumulator(mixed_checkpoint_accumulator);
    auto mismatched_checkpoint_boundary = boundary_one.value();
    mismatched_checkpoint_boundary.continuation_fingerprint =
        "fnv1a64:different-continuation-checkpoint";
    mismatched_checkpoint_boundary.fingerprint =
        gtosd::fingerprint_hu_preflop_river_root_boundary(mismatched_checkpoint_boundary);
    require(!gtosd::accumulate_hu_preflop_river_root_boundary(mixed_checkpoint_accumulator,
                                                              mismatched_checkpoint_boundary) &&
                gtosd::fingerprint_hu_preflop_river_task_accumulator(
                    mixed_checkpoint_accumulator) == before_mixed_checkpoint,
            "River accumulator rejects a mixed continuation checkpoint without mutation");
    require(gtosd::accumulate_hu_preflop_river_root_boundary(numerical_accumulator,
                                                             boundary_zero.value()) &&
                gtosd::accumulate_hu_preflop_river_root_boundary(numerical_accumulator,
                                                                 boundary_one.value()) &&
                numerical_accumulator.complete &&
                numerical_accumulator.accumulated_roots_by_resolver ==
                    std::array<std::uint64_t, 2>{1U, 1U} &&
                gtosd::seal_hu_preflop_river_task_accumulator(numerical_accumulator),
            "task-local River accumulator consumes both sides in deterministic order");
    const auto aggregate = gtosd::finalize_hu_preflop_river_task_accumulator(numerical_accumulator);
    require(aggregate.has_value() && aggregate.value().resolver_values[0].size() == 528U &&
                aggregate.value().resolver_values[1].size() == 528U &&
                aggregate.value().continuation_checkpoint_fingerprint ==
                    boundary_zero.value().continuation_fingerprint &&
                !aggregate.value().fingerprint.empty(),
            "sealed River continuation aggregate covers every Flop-live opponent combo");
    require(gtosd::accumulate_hu_preflop_river_root_boundary(numerical_best_response_accumulator,
                                                             best_response_boundary_zero.value()) &&
                gtosd::accumulate_hu_preflop_river_root_boundary(
                    numerical_best_response_accumulator, best_response_boundary_one.value()) &&
                gtosd::seal_hu_preflop_river_task_accumulator(numerical_best_response_accumulator),
            "best-response accumulator consumes only best-response River boundaries");
    const auto best_response_aggregate =
        gtosd::finalize_hu_preflop_river_task_accumulator(numerical_best_response_accumulator);
    require(
        best_response_aggregate.has_value() &&
            best_response_aggregate.value().value_mode ==
                gtosd::HuPreflopContinuationValueMode::ExactBestResponse &&
            !gtosd::make_hu_preflop_flop_task_accumulator(tree, plan,
                                                          best_response_aggregate.value()),
        "best-response River aggregate remains distinct from the average-policy Flop assembler");
    const auto serialized_best_response_accumulator =
        gtosd::serialize_hu_preflop_river_task_accumulator(numerical_best_response_accumulator);
    const auto serialized_best_response_aggregate =
        gtosd::serialize_hu_preflop_river_task_aggregate(best_response_aggregate.value());
    const auto restored_best_response_accumulator =
        serialized_best_response_accumulator
            ? gtosd::deserialize_hu_preflop_river_task_accumulator(
                  serialized_best_response_accumulator.value())
            : gtosd::Result<gtosd::HuPreflopRiverTaskAccumulator, gtosd::HuPreflopError>::failure(
                  gtosd::HuPreflopError::IntegrityFailure);
    const auto restored_best_response_aggregate =
        serialized_best_response_aggregate
            ? gtosd::deserialize_hu_preflop_river_task_aggregate(
                  serialized_best_response_aggregate.value())
            : gtosd::Result<gtosd::HuPreflopRiverTaskAggregate, gtosd::HuPreflopError>::failure(
                  gtosd::HuPreflopError::IntegrityFailure);
    require(restored_best_response_accumulator.has_value() &&
                restored_best_response_accumulator.value().value_mode ==
                    gtosd::HuPreflopContinuationValueMode::ExactBestResponse &&
                restored_best_response_aggregate.has_value() &&
                restored_best_response_aggregate.value().value_mode ==
                    gtosd::HuPreflopContinuationValueMode::ExactBestResponse,
            "best-response River accumulator and aggregate preserve their mode on round-trip");
    const auto first_live_value = std::ranges::find_if(
        boundary_zero.value().values, [](const auto &value) { return value.positive_reach; });
    require(first_live_value != boundary_zero.value().values.end(),
            "orbit-lifted River boundary has positive physical reach");
    const auto first_live = first_live_value->opponent_combo;
    const auto aggregated_value =
        std::ranges::find_if(aggregate.value().resolver_values[0],
                             [&](const auto &value) { return value.opponent_combo == first_live; });
    require(aggregated_value != aggregate.value().resolver_values[0].end() &&
                aggregated_value->weighted_counterfactual_reach ==
                    first_live_value->counterfactual_reach &&
                std::abs(aggregated_value->conditional_value_antes -
                         first_live_value->blueprint_counterfactual_value_antes) < 1.0e-12,
            "River reduction consumes an orbit-lifted boundary exactly once");
    const auto serialized_aggregate =
        gtosd::serialize_hu_preflop_river_task_aggregate(aggregate.value());
    auto legacy_aggregate = serialized_aggregate.value();
    legacy_aggregate.replace(legacy_aggregate.find("river_task_aggregate.v5"),
                             std::string("river_task_aggregate.v5").size(),
                             "river_task_aggregate.v4");
    const auto restored_aggregate =
        serialized_aggregate
            ? gtosd::deserialize_hu_preflop_river_task_aggregate(serialized_aggregate.value())
            : gtosd::Result<gtosd::HuPreflopRiverTaskAggregate, gtosd::HuPreflopError>::failure(
                  gtosd::HuPreflopError::IntegrityFailure);
    require(gtosd::validate_hu_preflop_river_task_aggregate(aggregate.value()) &&
                restored_aggregate.has_value() &&
                restored_aggregate.value().fingerprint == aggregate.value().fingerprint &&
                restored_aggregate.value().continuation_checkpoint_fingerprint ==
                    aggregate.value().continuation_checkpoint_fingerprint &&
                !gtosd::deserialize_hu_preflop_river_task_aggregate(legacy_aggregate) &&
                !gtosd::deserialize_hu_preflop_river_task_aggregate(serialized_aggregate.value() +
                                                                    "corrupt"),
            "River aggregate round-trips and rejects legacy semantics or corruption");
    const auto aggregate_path =
        std::filesystem::current_path() / "hu_preflop_river_aggregate_test.bin";
    std::error_code aggregate_ignored;
    std::filesystem::remove(aggregate_path, aggregate_ignored);
    require(
        gtosd::save_hu_preflop_river_task_aggregate(aggregate.value(), aggregate_path.string()) &&
            gtosd::load_hu_preflop_river_task_aggregate(aggregate_path.string()),
        "River continuation aggregate saves atomically with checksum");
    std::filesystem::remove(aggregate_path, aggregate_ignored);
    const auto serialized_accumulator =
        gtosd::serialize_hu_preflop_river_task_accumulator(numerical_accumulator);
    auto legacy_accumulator = serialized_accumulator.value();
    legacy_accumulator.replace(legacy_accumulator.find("river_task_accumulator.v5"),
                               std::string("river_task_accumulator.v5").size(),
                               "river_task_accumulator.v4");
    const auto restored_accumulator =
        serialized_accumulator
            ? gtosd::deserialize_hu_preflop_river_task_accumulator(serialized_accumulator.value())
            : gtosd::Result<gtosd::HuPreflopRiverTaskAccumulator, gtosd::HuPreflopError>::failure(
                  gtosd::HuPreflopError::IntegrityFailure);
    require(restored_accumulator.has_value() &&
                restored_accumulator.value().fingerprint == numerical_accumulator.fingerprint &&
                restored_accumulator.value().continuation_checkpoint_fingerprint ==
                    numerical_accumulator.continuation_checkpoint_fingerprint &&
                !gtosd::deserialize_hu_preflop_river_task_accumulator(legacy_accumulator) &&
                !gtosd::deserialize_hu_preflop_river_task_accumulator(
                    serialized_accumulator.value() + "corrupt"),
            "sealed River accumulator rejects legacy semantics and trailing corruption");
    const auto accumulator_path =
        std::filesystem::current_path() / "hu_preflop_river_accumulator_test.bin";
    std::error_code accumulator_ignored;
    std::filesystem::remove(accumulator_path, accumulator_ignored);
    require(gtosd::save_hu_preflop_river_task_accumulator(numerical_accumulator,
                                                          accumulator_path.string()) &&
                gtosd::load_hu_preflop_river_task_accumulator(accumulator_path.string()),
            "sealed River accumulator saves atomically with checksum");
    std::filesystem::remove(accumulator_path, accumulator_ignored);
  };
  verify_task_accumulators();
  require(!gtosd::hu_preflop_river_resolver_root_at(catalog.value(), batches.value(),
                                                    batches.value().resolver_roots),
          "River root resolver rejects the first ordinal outside exact coverage");
  const auto initial =
      gtosd::make_hu_preflop_river_scheduler_checkpoint(batches.value(), "fnv1a64:upper-empty");
  require(initial.has_value() && initial.value().completed_batch_count == 0U &&
              initial.value().completed_resolver_root_count == 0U && !initial.value().complete,
          "scheduler checkpoint starts before the first deterministic batch");
  require(!gtosd::advance_hu_preflop_river_scheduler_checkpoint(
              batches.value(), initial.value(), last.value(), "fnv1a64:out-of-order"),
          "scheduler checkpoint rejects skipped or reordered batches");
  const auto after_first = gtosd::advance_hu_preflop_river_scheduler_checkpoint(
      batches.value(), initial.value(), first.value(), "fnv1a64:upper-after-first");
  require(after_first.has_value() && after_first.value().completed_batch_count == 1U &&
              after_first.value().completed_resolver_root_count == 5'294U &&
              !after_first.value().complete,
          "scheduler advances only after binding the reduced upper-street accumulator");

  auto penultimate = initial.value();
  penultimate.completed_batch_count = batches.value().batch_count - 1U;
  penultimate.completed_resolver_root_count = last.value().first_resolver_root;
  penultimate.upper_street_accumulator_fingerprint = "fnv1a64:upper-before-last";
  penultimate.fingerprint = gtosd::fingerprint_hu_preflop_river_scheduler_checkpoint(penultimate);
  const auto complete = gtosd::advance_hu_preflop_river_scheduler_checkpoint(
      batches.value(), penultimate, last.value(), "fnv1a64:upper-complete");
  require(complete.has_value() && complete.value().complete &&
              complete.value().completed_batch_count == batches.value().batch_count &&
              complete.value().completed_resolver_root_count == batches.value().resolver_roots,
          "final batch closes exact resolver-root coverage without materializing all boundaries");

  const auto serialized =
      gtosd::serialize_hu_preflop_river_scheduler_checkpoint(after_first.value());
  const auto restored =
      serialized
          ? gtosd::deserialize_hu_preflop_river_scheduler_checkpoint(serialized.value())
          : gtosd::Result<gtosd::HuPreflopRiverSchedulerCheckpoint, gtosd::HuPreflopError>::failure(
                gtosd::HuPreflopError::IntegrityFailure);
  require(
      restored.has_value() && restored.value().fingerprint == after_first.value().fingerprint &&
          !gtosd::deserialize_hu_preflop_river_scheduler_checkpoint(serialized.value() + "corrupt"),
      "River scheduler checkpoint round-trips and rejects trailing corruption");

  const auto path = std::filesystem::current_path() / "hu_preflop_river_scheduler_test.bin";
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  require(gtosd::save_hu_preflop_river_scheduler_checkpoint(after_first.value(), path.string()) &&
              gtosd::load_hu_preflop_river_scheduler_checkpoint(path.string()),
          "River scheduler checkpoint saves atomically with checksum");
  std::filesystem::remove(path, ignored);
}

void test_sampled_river_policy_evaluator() {
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config()).value();
  const auto blueprint = gtosd::make_uniform_hu_preflop_blueprint(tree).value();
  const auto plan = gtosd::derive_hu_preflop_decomposition_plan(tree, blueprint).value();
  const auto work = gtosd::estimate_hu_preflop_river_work(tree, plan).value();
  const auto catalog = gtosd::build_hu_preflop_river_root_catalog(tree, plan, work).value();
  constexpr std::uint64_t batch_payload = 64ULL * 1'024ULL * 1'024ULL;
  const auto batches =
      gtosd::derive_hu_preflop_river_batch_plan(tree, plan, work, catalog, batch_payload).value();
  const auto &task = catalog.task_spans.front();
  const auto shape_begin =
      catalog.river_shapes.begin() + static_cast<std::ptrdiff_t>(task.first_shape_index);
  const auto shape_end = shape_begin + static_cast<std::ptrdiff_t>(task.shape_count);
  const auto shape =
      std::ranges::min_element(shape_begin, shape_end, {}, [](const auto &candidate) {
        return candidate.state.remaining_stacks[0].units();
      });
  require(shape != shape_end, "sampled River evaluator fixture has a public shape");
  const auto shape_offset = static_cast<std::uint64_t>(std::distance(shape_begin, shape));
  const auto root_ordinal = task.first_resolver_root + shape_offset * task.board_count * 2U;
  const auto root = gtosd::hu_preflop_river_resolver_root_at(catalog, batches, root_ordinal);
  require(root.has_value() &&
              root.value().state.remaining_stacks[0] == shape->state.remaining_stacks[0],
          "sampled River evaluator fixture selects a valid shallow public root");

  const auto policy = make_uniform_sampled_postflop_policy(tree, 7U);
  const auto empty_validated_policy =
      gtosd::make_hu_preflop_validated_sampled_postflop_policy(tree, policy);
  const auto empty_census =
      empty_validated_policy
          ? gtosd::analyze_hu_preflop_sampled_policy_reuse(empty_validated_policy.value())
          : gtosd::Result<gtosd::HuPreflopSampledPolicyReuseCensus,
                          gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
  auto census_policy = policy;
  gtosd::HuPreflopSampledPostflopPolicyEntry first_census_entry;
  first_census_entry.key.public_history = 10U;
  first_census_entry.key.physical_cards = 1U;
  first_census_entry.key.player = 0U;
  first_census_entry.key.street = gtosd::Street::Flop;
  first_census_entry.action_count = 2U;
  first_census_entry.probabilities[0] = 0.25;
  first_census_entry.probabilities[1] = 0.75;
  auto second_census_entry = first_census_entry;
  second_census_entry.key.physical_cards = 2U;
  auto third_census_entry = first_census_entry;
  third_census_entry.key.public_history = 11U;
  third_census_entry.key.physical_cards = 3U;
  third_census_entry.key.player = 1U;
  third_census_entry.key.street = gtosd::Street::Turn;
  census_policy.entries = {first_census_entry, second_census_entry, third_census_entry};
  census_policy.fingerprint =
      gtosd::fingerprint_hu_preflop_sampled_postflop_policy(census_policy);
  const auto validated_census_policy =
      gtosd::make_hu_preflop_validated_sampled_postflop_policy(tree, census_policy);
  const auto census =
      validated_census_policy
          ? gtosd::analyze_hu_preflop_sampled_policy_reuse(validated_census_policy.value())
          : gtosd::Result<gtosd::HuPreflopSampledPolicyReuseCensus,
                          gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
  require(empty_census.has_value() && empty_census.value().trained_information_sets == 0U &&
              census.has_value() && census.value().trained_information_sets == 3U &&
              census.value().trained_decision_contexts == 2U &&
              census.value().trained_information_sets_by_street[0] == 2U &&
              census.value().trained_information_sets_by_street_player[1][1] == 1U &&
              census.value().distinct_public_histories_by_street[0] == 1U &&
              census.value().distinct_public_histories_by_street[1] == 1U &&
              census.value().minimum_information_sets_per_context == 1U &&
              census.value().maximum_information_sets_per_context == 2U &&
              std::abs(census.value().mean_information_sets_per_context - 1.5) <= 1.0e-12,
          "policy reuse census separates trained infosets from public decision contexts");
  const auto reach = gtosd::derive_hu_preflop_river_conditioned_reach_from_sampled_policy(
      tree, plan, root.value(), policy);
  const auto profile = gtosd::evaluate_hu_preflop_sampled_policy_river_root(
      tree, plan, root.value(), policy, gtosd::PostflopRootValueMode::AverageStrategy);
  const auto best_response = gtosd::evaluate_hu_preflop_sampled_policy_river_root(
      tree, plan, root.value(), policy, gtosd::PostflopRootValueMode::ExactBestResponse);
  const auto validated_policy =
      gtosd::make_hu_preflop_validated_sampled_postflop_policy(tree, policy);
  const auto validated_reach =
      validated_policy
          ? gtosd::derive_hu_preflop_river_conditioned_reach_from_sampled_policy(
                tree, plan, root.value(), validated_policy.value())
          : gtosd::Result<gtosd::HuPreflopRiverConditionedReach,
                          gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
  const auto validated_best_response =
      validated_policy
          ? gtosd::evaluate_hu_preflop_sampled_policy_river_root(
                tree, plan, root.value(), validated_policy.value(),
                gtosd::PostflopRootValueMode::ExactBestResponse)
          : gtosd::Result<gtosd::PostflopRootCounterfactualValues,
                          gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
  const auto vectorized_profile =
      validated_policy
          ? gtosd::evaluate_hu_preflop_sampled_policy_river_root_vectorized(
                tree, plan, root.value(), validated_policy.value(),
                gtosd::PostflopRootValueMode::AverageStrategy)
          : gtosd::Result<gtosd::PostflopRootCounterfactualValues,
                          gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
  const auto vectorized_best_response =
      validated_policy
          ? gtosd::evaluate_hu_preflop_sampled_policy_river_root_vectorized(
                tree, plan, root.value(), validated_policy.value(),
                gtosd::PostflopRootValueMode::ExactBestResponse)
          : gtosd::Result<gtosd::PostflopRootCounterfactualValues,
                          gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
  const auto vectorized_pair =
      validated_policy
          ? gtosd::evaluate_hu_preflop_sampled_policy_river_root_vectorized_pair(
                tree, plan, root.value(), validated_policy.value())
          : gtosd::Result<gtosd::HuPreflopRiverProfileBestResponseEvaluation,
                          gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
  const auto adjacent_shape_offset = (shape_offset + 1U) % task.shape_count;
  const auto enumerated_board_roots =
      gtosd::enumerate_hu_preflop_river_board_roots(catalog, batches, 0U, 0U, 0U);
  const auto *adjacent_root =
      enumerated_board_roots && adjacent_shape_offset < enumerated_board_roots.value().size()
          ? &enumerated_board_roots.value()[static_cast<std::size_t>(adjacent_shape_offset)]
          : nullptr;
  std::vector<gtosd::HuPreflopRiverResolverRoot> board_batch_roots;
  if (root && adjacent_root != nullptr) {
    board_batch_roots = {root.value(), *adjacent_root};
  }
  const auto vectorized_board_batch =
      validated_policy && board_batch_roots.size() == 2U
          ? gtosd::evaluate_hu_preflop_sampled_policy_river_board_batch_vectorized_pair(
                tree, plan, board_batch_roots, validated_policy.value())
          : gtosd::Result<gtosd::HuPreflopRiverBoardBatchEvaluation,
                          gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
  const auto adjacent_pair =
      validated_policy && adjacent_root != nullptr
          ? gtosd::evaluate_hu_preflop_sampled_policy_river_root_vectorized_pair(
                tree, plan, *adjacent_root, validated_policy.value())
          : gtosd::Result<gtosd::HuPreflopRiverProfileBestResponseEvaluation,
                          gtosd::HuPreflopError>::failure(gtosd::HuPreflopError::IntegrityFailure);
  require(reach.has_value() && profile.has_value() && best_response.has_value(),
          "sampled River evaluator produces exact profile and best-response values");
  require(validated_policy.has_value() && validated_reach.has_value() &&
              validated_best_response.has_value() &&
              validated_policy.value().policy_fingerprint == policy.fingerprint &&
              validated_reach.value().compatible_joint_reach_mass ==
                  reach.value().compatible_joint_reach_mass &&
              validated_best_response.value().recomposed_value_antes ==
                  best_response.value().recomposed_value_antes,
          "immutable sampled-policy handle validates once and preserves River BR values");
  const auto same_root_values = [](const auto &scalar, const auto &vectorized) {
    for (std::size_t player = 0U; player < 2U; ++player) {
      if (std::abs(scalar.recomposed_value_antes[player] -
                   vectorized.recomposed_value_antes[player]) > 1.0e-10 ||
          scalar.players[player].size() != vectorized.players[player].size()) {
        return false;
      }
      for (std::size_t row = 0U; row < scalar.players[player].size(); ++row) {
        const auto &expected = scalar.players[player][row];
        const auto &actual = vectorized.players[player][row];
        if (expected.combo != actual.combo || expected.positive_reach != actual.positive_reach ||
            std::abs(expected.source_range_weight - actual.source_range_weight) > 1.0e-12 ||
            std::abs(expected.counterfactual_reach - actual.counterfactual_reach) > 1.0e-10 ||
            std::abs(expected.conditional_value_antes - actual.conditional_value_antes) > 1.0e-10) {
          return false;
        }
      }
    }
    return true;
  };
  require(vectorized_profile.has_value() && vectorized_best_response.has_value() &&
              same_root_values(profile.value(), vectorized_profile.value()) &&
              same_root_values(best_response.value(), vectorized_best_response.value()),
          "vectorized River profile and BR match the scalar oracle within 1e-10");
  require(vectorized_pair.has_value() &&
              same_root_values(profile.value(), vectorized_pair.value().profile) &&
              same_root_values(best_response.value(), vectorized_pair.value().best_response),
          "paired vectorized River profile and BR match the scalar oracle within 1e-10");
  require(enumerated_board_roots.has_value() &&
              enumerated_board_roots.value().size() == task.shape_count &&
              enumerated_board_roots.value()[static_cast<std::size_t>(shape_offset)].fingerprint ==
                  root.value().fingerprint &&
              vectorized_board_batch.has_value() && adjacent_pair.has_value() &&
              vectorized_board_batch.value().roots.size() == 2U &&
              same_root_values(vectorized_pair.value().profile,
                               vectorized_board_batch.value().roots[0].profile) &&
              same_root_values(vectorized_pair.value().best_response,
                               vectorized_board_batch.value().roots[0].best_response) &&
              same_root_values(adjacent_pair.value().profile,
                               vectorized_board_batch.value().roots[1].profile) &&
              same_root_values(adjacent_pair.value().best_response,
                               vectorized_board_batch.value().roots[1].best_response),
          "board-batched River roots match independent paired evaluation within 1e-10");
  const auto strict_policy = gtosd::make_hu_preflop_validated_sampled_postflop_policy(
      tree, policy, gtosd::HuPreflopSampledPolicyLookupMode::RequireTrainedInfoset);
  require(strict_policy.has_value() &&
              !gtosd::evaluate_hu_preflop_sampled_policy_river_root(
                  tree, plan, root.value(), strict_policy.value(),
                  gtosd::PostflopRootValueMode::ExactBestResponse),
          "strict coverage lookup rejects an infoset absent from the sparse policy");
  if (validated_policy) {
    auto tampered_validation = validated_policy.value();
    tampered_validation.policy_fingerprint = "fnv1a64:tampered";
    require(!gtosd::evaluate_hu_preflop_sampled_policy_river_root(
                tree, plan, root.value(), tampered_validation,
                gtosd::PostflopRootValueMode::ExactBestResponse),
            "River evaluator rejects a tampered validated-policy identity");
  }
  require(profile.value().game_fingerprint == policy.fingerprint &&
              profile.value().blueprint_iterations == policy.iterations &&
              profile.value().mode == gtosd::PostflopRootValueMode::AverageStrategy &&
              best_response.value().mode == gtosd::PostflopRootValueMode::ExactBestResponse &&
              profile.value().players[0].size() == 465U &&
              profile.value().players[1].size() == 465U &&
              best_response.value().players[0].size() == 465U &&
              best_response.value().players[1].size() == 465U &&
              profile.value().maximum_recomposition_error_antes <= 1.0e-9 &&
              best_response.value().maximum_recomposition_error_antes <= 1.0e-9 &&
              std::abs(profile.value().recomposed_value_antes[0] +
                       profile.value().recomposed_value_antes[1]) <= 1.0e-9 &&
              best_response.value().recomposed_value_antes[0] + 1.0e-12 >=
                  profile.value().recomposed_value_antes[0] &&
              best_response.value().recomposed_value_antes[1] + 1.0e-12 >=
                  profile.value().recomposed_value_antes[1],
          "River profile is zero-sum and each exact best response weakly dominates it");
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    std::array<bool, 630U> observed{};
    for (const auto &row : profile.value().players[player]) {
      require(row.combo < observed.size() && !observed[row.combo] && row.positive_reach &&
                  std::isfinite(row.conditional_value_antes) &&
                  row.source_range_weight == reach.value().exact_sequence_reach[player][row.combo],
              "River profile rows preserve exact physical reach and unique combo identity");
      observed[row.combo] = true;
    }
  }

  const auto resolver = root.value().resolving_player;
  const auto opponent = static_cast<std::uint8_t>(1U - resolver);
  const auto profile_boundary = gtosd::build_hu_preflop_river_root_boundary(
      tree, plan, root.value(), profile.value(), reach.value().exact_sequence_reach[resolver],
      reach.value().postflop_action_sequence_reach[opponent], policy.fingerprint);
  const auto best_response_boundary = gtosd::build_hu_preflop_river_root_boundary(
      tree, plan, root.value(), best_response.value(), reach.value().exact_sequence_reach[resolver],
      reach.value().postflop_action_sequence_reach[opponent], policy.fingerprint);
  auto corrupt_policy = policy;
  corrupt_policy.fingerprint = "fnv1a64:corrupt";
  require(profile_boundary.has_value() && best_response_boundary.has_value() &&
              profile_boundary.value().value_mode ==
                  gtosd::HuPreflopContinuationValueMode::AverageStrategy &&
              profile_boundary.value().best_response_turn_components.empty() &&
              best_response_boundary.value().value_mode ==
                  gtosd::HuPreflopContinuationValueMode::ExactBestResponse &&
              !best_response_boundary.value().best_response_turn_components.empty() &&
              profile_boundary.value().continuation_fingerprint == policy.fingerprint &&
              best_response_boundary.value().continuation_fingerprint == policy.fingerprint &&
              !gtosd::evaluate_hu_preflop_sampled_policy_river_root(
                  tree, plan, root.value(), corrupt_policy,
                  gtosd::PostflopRootValueMode::AverageStrategy),
          "exact sampled River values feed profile and BR boundary channels with one identity");
}

void test_exact_postflop_all_in_runout_enumeration() {
  const std::array<std::array<gtosd::CardId, 2>, 2> holes{{
      {gtosd::parse_card("As").value(), gtosd::parse_card("Ah").value()},
      {gtosd::parse_card("Kc").value(), gtosd::parse_card("Kd").value()},
  }};
  const std::array board{gtosd::parse_card("Qs").value(),
                         gtosd::parse_card("Js").value(),
                         gtosd::parse_card("Ts").value(),
                         gtosd::parse_card("6d").value(),
                         gtosd::parse_card("7h").value()};
  const auto flop = gtosd::enumerate_hu_preflop_postflop_all_in_equity(
      holes, board, gtosd::Street::Flop);
  const auto turn = gtosd::enumerate_hu_preflop_postflop_all_in_equity(
      holes, board, gtosd::Street::Turn);
  const auto river = gtosd::enumerate_hu_preflop_postflop_all_in_equity(
      holes, board, gtosd::Street::River);
  require(flop.has_value() && flop.value().runouts() == 406U && turn.has_value() &&
              turn.value().runouts() == 28U && river.has_value() &&
              river.value().runouts() == 1U && river.value().wins == 1U,
          "exact postflop all-in enumeration covers 406/28/1 legal runouts");

  auto swapped_holes = holes;
  std::swap(swapped_holes[0], swapped_holes[1]);
  const auto swapped_flop = gtosd::enumerate_hu_preflop_postflop_all_in_equity(
      swapped_holes, board, gtosd::Street::Flop);
  require(swapped_flop.has_value() && swapped_flop.value().wins == flop.value().losses &&
              swapped_flop.value().ties == flop.value().ties &&
              swapped_flop.value().losses == flop.value().wins,
          "swapping players complements exact postflop all-in outcomes");

  const std::array<std::array<gtosd::CardId, 2>, 2> permuted_holes{{
      {gtosd::parse_card("Ac").value(), gtosd::parse_card("As").value()},
      {gtosd::parse_card("Kd").value(), gtosd::parse_card("Kh").value()},
  }};
  const std::array permuted_board{gtosd::parse_card("Qc").value(),
                                  gtosd::parse_card("Jc").value(),
                                  gtosd::parse_card("Tc").value(),
                                  gtosd::parse_card("6h").value(),
                                  gtosd::parse_card("7s").value()};
  const auto permuted_flop = gtosd::enumerate_hu_preflop_postflop_all_in_equity(
      permuted_holes, permuted_board, gtosd::Street::Flop);
  require(permuted_flop.has_value() && permuted_flop.value() == flop.value(),
          "global suit permutations preserve exact postflop all-in outcomes");

  std::uint64_t visible_mask = 0U;
  for (const auto &hole : holes) {
    visible_mask |= hole[0].mask() | hole[1].mask();
  }
  visible_mask |= board[0].mask() | board[1].mask() | board[2].mask();
  gtosd::HuPreflopPostflopAllInEquity summed_turns;
  std::size_t turn_cards = 0U;
  for (const auto card : gtosd::short_deck()) {
    if ((visible_mask & card.mask()) != 0U) {
      continue;
    }
    auto conditioned_board = board;
    conditioned_board[3] = card;
    const auto conditioned = gtosd::enumerate_hu_preflop_postflop_all_in_equity(
        holes, conditioned_board, gtosd::Street::Turn);
    require(conditioned.has_value(), "each legal Turn has an exact River enumeration");
    summed_turns.wins += conditioned.value().wins;
    summed_turns.ties += conditioned.value().ties;
    summed_turns.losses += conditioned.value().losses;
    ++turn_cards;
  }
  require(turn_cards == 29U && summed_turns.wins == 2U * flop.value().wins &&
              summed_turns.ties == 2U * flop.value().ties &&
              summed_turns.losses == 2U * flop.value().losses,
          "Flop enumeration equals the order-normalized sum of every conditioned Turn");

  auto duplicate_board = board;
  duplicate_board[0] = holes[0][0];
  require(!gtosd::enumerate_hu_preflop_postflop_all_in_equity(
              holes, duplicate_board, gtosd::Street::Flop) &&
              !gtosd::enumerate_hu_preflop_postflop_all_in_equity(
                  holes, board, gtosd::Street::Preflop),
          "exact postflop all-in enumeration rejects duplicate and preflop inputs");
}

void test_exact_postflop_all_in_cache_is_transparent() {
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config()).value();
  gtosd::HuPreflopSolveOptions options;
  options.iterations = 4U;
  options.evaluation_deals = 8U;
  options.best_response_iterations = 2U;
  options.best_response_evaluation_deals = 8U;
  options.equity_samples_per_bucket = 1U;
  options.seed = 0x4558'4143'5443'4143ULL;
  options.evaluation_seed = 0x4558'4143'5445'5641ULL;
  options.postflop_all_in_expectation_mode =
      gtosd::HuPreflopPostflopAllInExpectationMode::ExactFlopAndTurn;
  options.maximum_exact_postflop_all_in_cache_entries = 0U;
  const auto uncached = gtosd::solve_hu_preflop_sampled(tree, options);

  options.maximum_exact_postflop_all_in_cache_entries = 10'000U;
  const auto cached = gtosd::solve_hu_preflop_sampled(tree, options);
  const auto uncached_evaluations =
      uncached ? uncached.value().exact_postflop_all_in_evaluations[0] +
                     uncached.value().exact_postflop_all_in_evaluations[1]
               : 0U;
  const auto cached_evaluations =
      cached ? cached.value().exact_postflop_all_in_evaluations[0] +
                   cached.value().exact_postflop_all_in_evaluations[1]
             : 0U;
  require(uncached.has_value() && cached.has_value() && uncached_evaluations > 0U &&
              cached_evaluations == uncached_evaluations &&
              uncached.value().root_strategy == cached.value().root_strategy &&
              uncached.value().root_action_ev_ante == cached.value().root_action_ev_ante &&
              uncached.value().root_ev_ante == cached.value().root_ev_ante &&
              uncached.value().exact_postflop_all_in_cache_hits == 0U &&
              uncached.value().exact_postflop_all_in_cache_misses == uncached_evaluations &&
              cached.value().exact_postflop_all_in_cache_hits > 0U &&
              cached.value().exact_postflop_all_in_cache_hits +
                      cached.value().exact_postflop_all_in_cache_misses ==
                  cached_evaluations &&
              cached.value().exact_postflop_all_in_cache_peak_entries > 0U &&
              cached.value().exact_postflop_all_in_cache_peak_entries <=
                  options.maximum_exact_postflop_all_in_cache_entries,
          "bounded exact all-in cache preserves deterministic solve outputs and reports reuse");
}

void test_sampled_solver_smoke() {
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config()).value();
  gtosd::HuPreflopSolveOptions options;
  options.iterations = 32U;
  options.evaluation_deals = 128U;
  options.best_response_iterations = 16U;
  options.best_response_evaluation_deals = 128U;
  options.equity_samples_per_bucket = 2U;
  options.seed = 0x534D'4F4B'4500'0001ULL;
  options.export_postflop_policy = true;
  options.evaluate_preflop_decisions = true;
  auto training_oracle = std::make_shared<gtosd::HuPreflopAllInTrainingOracle>();
  training_oracle->source_equity_table_fingerprint = "unit_all_tie_training_oracle";
  for (auto &matchup : training_oracle->matchups) {
    matchup.tie_probability = 1.0;
  }
  options.preflop_all_in_training_oracle = training_oracle;
  const auto solved = gtosd::solve_hu_preflop_sampled(tree, options);
  require(solved.has_value(), "sampled HU preflop smoke solve completes");
  require(solved.value().information_sets > 0U &&
              solved.value().best_response_information_sets > 0U,
          "sampled solve materializes sparse blueprint and response infosets");
  require(solved.value().bytes_per_information_set_payload > 0U &&
              solved.value().minimum_blueprint_payload_bytes ==
                  solved.value().information_sets *
                      solved.value().bytes_per_information_set_payload &&
              !solved.value().allocator_overhead_included,
          "sampled solve reports a minimum payload without allocator overhead");
  require(std::isfinite(solved.value().root_ev_ante) &&
              std::isfinite(solved.value().normalized_abstract_nashconv) &&
              solved.value().exact_preflop_all_in_expectation &&
              solved.value().preflop_all_in_equity_table_fingerprint ==
                  training_oracle->source_equity_table_fingerprint &&
              solved.value().algorithm_id.ends_with("_exact_preflop_all_in_expectation_v1"),
          "sampled solve metrics are finite");
  auto invalid_options = options;
  auto invalid_oracle = std::make_shared<gtosd::HuPreflopAllInTrainingOracle>(*training_oracle);
  invalid_oracle->matchups.front().win_probability = 1.1;
  invalid_options.preflop_all_in_training_oracle = invalid_oracle;
  require(!gtosd::solve_hu_preflop_sampled(tree, invalid_options),
          "sampled solve rejects a non-probabilistic all-in training oracle");
  require(
      gtosd::validate_hu_preflop_blueprint(tree, solved.value().preflop_blueprint).has_value() &&
          gtosd::derive_hu_preflop_decomposition_plan(tree, solved.value().preflop_blueprint)
              .has_value(),
      "sampled solve exports a complete preflop policy for decomposition");
  require(solved.value().preflop_decision_training_diagnostics.size() ==
              tree.stats.decision_nodes,
          "full-chart evaluation exports one final training diagnostic per preflop node");
  for (const auto &diagnostic : solved.value().preflop_decision_training_diagnostics) {
    require(diagnostic.node_id < tree.nodes.size() &&
                diagnostic.action_count == tree.nodes[diagnostic.node_id].edges.size(),
            "preflop training diagnostics retain their exact node and action shape");
    for (std::size_t hand = 0U; hand < gtosd::hu_preflop_hand_class_count; ++hand) {
      const auto strategy_total = std::accumulate(
          diagnostic.current_strategy[hand].begin(),
          diagnostic.current_strategy[hand].begin() + diagnostic.action_count, 0.0);
      require(std::isfinite(strategy_total) && std::abs(strategy_total - 1.0) <= 1.0e-9 &&
                  diagnostic.last_iteration[hand] <= options.iterations,
              "each final preflop current policy is normalized and iteration-bounded");
      for (std::size_t action = 0U; action < diagnostic.action_count; ++action) {
        require(std::isfinite(diagnostic.cumulative_weighted_regret[hand][action]) &&
                    std::isfinite(diagnostic.cumulative_average_weight[hand][action]) &&
                    diagnostic.cumulative_average_weight[hand][action] >= 0.0,
                "preflop training state contains finite regrets and non-negative average weights");
      }
    }
  }
  const auto &postflop_policy = solved.value().postflop_policy;
  require(
      !postflop_policy.entries.empty() &&
          gtosd::validate_hu_preflop_sampled_postflop_policy(tree, postflop_policy).has_value() &&
          solved.value().minimum_exported_postflop_policy_payload_bytes ==
              postflop_policy.entries.size() * sizeof(gtosd::HuPreflopSampledPostflopPolicyEntry),
      "sampled solve optionally exports a validated versioned postflop average policy");
  const auto serialized_policy =
      gtosd::serialize_hu_preflop_sampled_postflop_policy(postflop_policy);
  const auto restored_policy =
      serialized_policy
          ? gtosd::deserialize_hu_preflop_sampled_postflop_policy(tree, serialized_policy.value(),
                                                                  serialized_policy.value().size())
          : gtosd::Result<gtosd::HuPreflopSampledPostflopPolicy, gtosd::HuPreflopError>::failure(
                gtosd::HuPreflopError::IntegrityFailure);
  require(
      serialized_policy.has_value() && restored_policy.has_value() &&
          restored_policy.value().fingerprint == postflop_policy.fingerprint &&
          restored_policy.value().entries == postflop_policy.entries &&
          !gtosd::deserialize_hu_preflop_sampled_postflop_policy(
              tree, serialized_policy.value() + "corrupt", serialized_policy.value().size() + 7U) &&
          !gtosd::deserialize_hu_preflop_sampled_postflop_policy(tree, serialized_policy.value(),
                                                                 1U),
      "sampled postflop policy serialization round-trips and enforces integrity and size");

  const auto policy_path =
      std::filesystem::current_path() / "hu_preflop_sampled_postflop_policy_test.bin";
  std::error_code policy_cleanup_error;
  std::filesystem::remove(policy_path, policy_cleanup_error);
  const auto saved_policy =
      gtosd::save_hu_preflop_sampled_postflop_policy(postflop_policy, policy_path.string());
  const auto loaded_policy =
      saved_policy
          ? gtosd::load_hu_preflop_sampled_postflop_policy(tree, policy_path.string(),
                                                           serialized_policy.value().size())
          : gtosd::Result<gtosd::HuPreflopSampledPostflopPolicy, gtosd::HuPreflopError>::failure(
                gtosd::HuPreflopError::IntegrityFailure);
  require(saved_policy.has_value() && loaded_policy.has_value() &&
              loaded_policy.value().fingerprint == postflop_policy.fingerprint &&
              !gtosd::load_hu_preflop_sampled_postflop_policy(tree, policy_path.string(), 1U),
          "sampled postflop policy checkpoint saves atomically and rejects oversized input");
  std::filesystem::remove(policy_path, policy_cleanup_error);
  const auto &first_policy_entry = postflop_policy.entries.front();
  const auto queried_policy = gtosd::query_hu_preflop_sampled_postflop_policy(
      postflop_policy, first_policy_entry.key, first_policy_entry.action_count);
  require(queried_policy.has_value() && queried_policy.value() == first_policy_entry.probabilities,
          "sampled postflop policy lookup reproduces a visited infoset exactly");
  auto fallback_policy = postflop_policy;
  fallback_policy.entries.clear();
  fallback_policy.fingerprint =
      gtosd::fingerprint_hu_preflop_sampled_postflop_policy(fallback_policy);
  const auto fallback = gtosd::query_hu_preflop_sampled_postflop_policy(
      fallback_policy, first_policy_entry.key, first_policy_entry.action_count);
  const auto expected_fallback = 1.0 / static_cast<double>(first_policy_entry.action_count);
  require(
      gtosd::validate_hu_preflop_sampled_postflop_policy(tree, fallback_policy).has_value() &&
          fallback.has_value() &&
          std::all_of(fallback.value().begin(),
                      fallback.value().begin() + first_policy_entry.action_count,
                      [&](const double probability) { return probability == expected_fallback; }),
      "unvisited sampled postflop infosets use the declared uniform fallback");
  const auto postflop_entry = std::ranges::find_if(tree.nodes, [](const auto &node) {
    return node.kind == gtosd::HuPreflopNodeKind::PostflopEntry;
  });
  require(postflop_entry != tree.nodes.end(), "sampled policy fixture finds a postflop entry");
  const std::array sampled_board{gtosd::parse_card("6c").value(), gtosd::parse_card("7d").value(),
                                 gtosd::parse_card("Ah").value(), gtosd::parse_card("8s").value(),
                                 gtosd::parse_card("9c").value()};
  auto sampled_state = gtosd::advance_street(postflop_entry->state);
  require(sampled_state.has_value(), "sampled policy fixture advances to the Flop");
  sampled_state.value().board_mask =
      sampled_board[0].mask() | sampled_board[1].mask() | sampled_board[2].mask();
  gtosd::ActionConfig sampled_action_config;
  sampled_action_config.aggressive_sizes.assign(tree.config.postflop_sizes.begin(),
                                                tree.config.postflop_sizes.end());
  sampled_action_config.raise_depth = gtosd::maximum_core_raise_depth;
  sampled_action_config.minimum_bet = tree.config.postflop_minimum_bet;
  sampled_action_config.all_in_mode = gtosd::AllInMode::Add;
  sampled_action_config.all_in_threshold = gtosd::PotPercentage::from_basis_points(100'000).value();
  const auto sampled_actions = gtosd::legal_actions(sampled_state.value(), sampled_action_config);
  require(sampled_actions.has_value() && !sampled_actions.value().empty(),
          "sampled policy fixture has a legal Flop action");
  const auto sampled_combos = gtosd::all_combos();
  const auto sampled_combo = std::ranges::find_if(sampled_combos, [&](const auto &combo) {
    return ((combo.first.mask() | combo.second.mask()) & sampled_state.value().board_mask) == 0U;
  });
  require(sampled_combo != sampled_combos.end(), "sampled policy fixture has a live combo");
  const std::array sampled_hole{sampled_combo->first, sampled_combo->second};
  const auto sampled_bucket = gtosd::compute_hu_preflop_category_equity_bucket(
      sampled_hole, sampled_board, gtosd::Street::Flop, fallback_policy.equity_samples_per_bucket,
      fallback_policy.partition_seed);
  require(sampled_bucket.has_value(), "sampled policy fixture computes its Flop bucket");
  const auto mix_sampled_history = [](std::uint64_t history, const std::uint64_t value) {
    history ^= value + 0x9E37'79B9'7F4A'7C15ULL + (history << 6U) + (history >> 2U);
    history ^= history >> 30U;
    history *= 0xBF58'476D'1CE4'E5B9ULL;
    history ^= history >> 27U;
    return history;
  };
  auto semantic_policy = fallback_policy;
  gtosd::HuPreflopSampledPostflopPolicyEntry semantic_entry;
  semantic_entry.key.public_history =
      mix_sampled_history(0x5052'4546'4C4F'5001ULL, postflop_entry->id);
  semantic_entry.key.bucket_history[0] = sampled_bucket.value();
  semantic_entry.key.preflop_class = gtosd::hand_class(*sampled_combo);
  semantic_entry.key.player = sampled_state.value().player_to_act;
  semantic_entry.key.street = gtosd::Street::Flop;
  semantic_entry.action_count = static_cast<std::uint8_t>(sampled_actions.value().size());
  double semantic_weight_total = 0.0;
  for (std::size_t action = 0U; action < semantic_entry.action_count; ++action) {
    semantic_weight_total += static_cast<double>(action + 1U);
  }
  for (std::size_t action = 0U; action < semantic_entry.action_count; ++action) {
    semantic_entry.probabilities[action] = static_cast<double>(action + 1U) / semantic_weight_total;
  }
  semantic_policy.entries.push_back(semantic_entry);
  semantic_policy.fingerprint =
      gtosd::fingerprint_hu_preflop_sampled_postflop_policy(semantic_policy);
  const auto semantic_probability = gtosd::query_hu_preflop_sampled_postflop_action_probability(
      tree, semantic_policy, postflop_entry->id, sampled_board, sampled_state.value(), {},
      sampled_actions.value().front(),
      static_cast<gtosd::ComboId>(sampled_combo - sampled_combos.begin()));
  require(gtosd::validate_hu_preflop_sampled_postflop_policy(tree, semantic_policy).has_value() &&
              semantic_probability.has_value() &&
              semantic_probability.value() == semantic_entry.probabilities.front(),
          "sampled policy replay selects a non-uniform visited Flop strategy row");
  auto corrupt_policy = postflop_policy;
  corrupt_policy.entries.front().probabilities.front() += 0.25;
  require(!gtosd::validate_hu_preflop_sampled_postflop_policy(tree, corrupt_policy),
          "sampled postflop policy validation rejects modified strategy bytes");
  auto bounded_export_options = options;
  bounded_export_options.iterations = 4U;
  bounded_export_options.evaluation_deals = 4U;
  bounded_export_options.best_response_iterations = 1U;
  bounded_export_options.best_response_evaluation_deals = 4U;
  bounded_export_options.equity_samples_per_bucket = 1U;
  bounded_export_options.maximum_exported_postflop_policy_payload_bytes = 1U;
  const auto bounded_export = gtosd::solve_hu_preflop_sampled(tree, bounded_export_options);
  require(!bounded_export && bounded_export.error() == gtosd::HuPreflopError::MemoryFailure,
          "sampled postflop policy export fails before exceeding its explicit payload budget");
  for (const auto &row : solved.value().root_strategy) {
    const auto total = std::accumulate(row.begin(), row.end(), 0.0);
    require(std::abs(total - 1.0) <= 1.0e-9, "every exact preflop class strategy sums to one");
  }

  options.iterations = 16U;
  options.evaluation_deals = 64U;
  options.best_response_iterations = 8U;
  options.best_response_evaluation_deals = 64U;
  options.export_postflop_policy = false;
  options.postflop_representation =
      gtosd::HuPreflopPostflopRepresentation::CategoryEquityMonteCarloCurrentStreet;
  const auto current_street = gtosd::solve_hu_preflop_sampled(tree, options);
  require(current_street.has_value() &&
              current_street.value().abstraction_id ==
                  "preflop_exact81_postflop_category_equity_mc2_"
                  "current_street_imperfect_recall_suit_invariant_v2" &&
              current_street.value().information_sets > 0U,
          "current-street postflop abstraction is explicit and operational");

  options.preflop_refinement_iterations = 4U;
  const auto refined = gtosd::solve_hu_preflop_sampled(tree, options);
  require(refined.has_value() && refined.value().iterations == 20U &&
              refined.value().postflop_training_iterations == 16U &&
              refined.value().preflop_refinement_iterations == 4U &&
              refined.value().preflop_blueprint.iterations == 20U &&
              refined.value().algorithm_id ==
                  "external_sampling_dcfr_1.5_0_3_alternating_v2_opponent_pass_average_"
                  "exact_preflop_all_in_expectation_v1_"
                  "frozen_postflop_rollout_refinement_v1",
          "preflop refinement freezes postflop rollouts and reports both training phases");
  options.preflop_refinement_iterations = 0U;

  options.postflop_representation =
      gtosd::HuPreflopPostflopRepresentation::CategoryEquityMonteCarloMemoryless;
  const auto memoryless = gtosd::solve_hu_preflop_sampled(tree, options);
  require(memoryless.has_value() &&
              memoryless.value().abstraction_id ==
                  "preflop_exact81_postflop_category_equity_mc2_"
                  "memoryless_imperfect_recall_suit_invariant_v2" &&
              memoryless.value().information_sets > 0U,
          "memoryless postflop abstraction is explicit and operational");

  options.iterations = 16U;
  options.evaluation_deals = 64U;
  options.best_response_iterations = 8U;
  options.best_response_evaluation_deals = 64U;
  options.equity_samples_per_bucket = 0U;
  options.export_postflop_policy = true;
  options.postflop_representation = gtosd::HuPreflopPostflopRepresentation::ExactPhysical;
  const auto exact = gtosd::solve_hu_preflop_sampled(tree, options);
  require(exact.has_value(), "physical postflop external-sampling smoke solve completes");
  require(exact.value().abstraction_id ==
                  "preflop_exact81_postflop_physical_lossless_suit_isomorphism_v2" &&
              exact.value().information_sets > 0U && std::isfinite(exact.value().root_ev_ante),
          "physical mode is explicit, finite and does not require equity buckets");
  require(gtosd::validate_hu_preflop_blueprint(tree, exact.value().preflop_blueprint).has_value(),
          "physical sampled solve exports a valid dense preflop blueprint");
  require(gtosd::validate_hu_preflop_sampled_postflop_policy(tree, exact.value().postflop_policy)
                  .has_value() &&
              exact.value().postflop_policy.equity_samples_per_bucket == 0U,
          "physical sampled policy exports exact visible-card keys without bucket parameters");
}

void test_sampled_bucket_is_globally_suit_isomorphic() {
  const std::array original_hole{gtosd::parse_card("Ah").value(), gtosd::parse_card("Kh").value()};
  const std::array original_board{gtosd::parse_card("Qh").value(), gtosd::parse_card("9c").value(),
                                  gtosd::parse_card("7d").value(), gtosd::parse_card("8s").value(),
                                  gtosd::parse_card("6c").value()};
  const std::array permuted_hole{gtosd::parse_card("As").value(), gtosd::parse_card("Ks").value()};
  const std::array permuted_board{gtosd::parse_card("Qs").value(), gtosd::parse_card("9d").value(),
                                  gtosd::parse_card("7h").value(), gtosd::parse_card("8c").value(),
                                  gtosd::parse_card("6d").value()};
  for (const auto street : {gtosd::Street::Flop, gtosd::Street::Turn, gtosd::Street::River}) {
    const auto original = gtosd::compute_hu_preflop_category_equity_bucket(
        original_hole, original_board, street, 64U, 0x4953'4F4D'4F52'5048ULL);
    const auto permuted = gtosd::compute_hu_preflop_category_equity_bucket(
        permuted_hole, permuted_board, street, 64U, 0x4953'4F4D'4F52'5048ULL);
    require(original.has_value() && permuted.has_value() && original.value() == permuted.value(),
            "category/equity buckets preserve global suit isomorphism on every street");
  }
  require(!gtosd::compute_hu_preflop_category_equity_bucket(original_hole, original_board,
                                                            gtosd::Street::Flop, 0U, 1U),
          "category/equity bucket rejects an empty Monte Carlo sample budget");
}

} // namespace

int main(const int argc, const char *const argv[]) {
  try {
    if (argc == 2) {
      test_filter = argv[1];
    } else if (argc != 1) {
      throw std::runtime_error("usage: gtosd_hu_preflop_tests [test-name-filter]");
    }
    run_test("co40_tree_contract", test_co40_tree_contract);
    run_test("co40_monetary_ledger", test_co40_monetary_ledger);
    run_test("declarative_config", test_declarative_config);
    run_test("postflop_natural_termination", test_postflop_natural_termination);
    run_test("flop_decomposition_contract_and_resources",
             test_flop_decomposition_contract_and_resources);
    run_test("decomposition_preserves_zero_reach_by_private_class",
             test_decomposition_preserves_zero_reach_by_private_class);
    run_test("whole_game_streaming_coverage_closes_every_public_root",
             test_whole_game_streaming_coverage_closes_every_public_root);
    run_test("nested_public_boundary_shapes_preserve_history",
             test_nested_public_boundary_shapes_preserve_history);
    run_test("upper_terminal_contributions_and_flop_assembler",
             test_upper_terminal_contributions_and_flop_assembler);
    run_test("best_response_action_reducer", test_best_response_action_reducer);
    run_test("best_response_task_recursion", test_best_response_task_recursion);
    run_test("best_response_entry_streaming_reduction",
             test_best_response_entry_streaming_reduction);
    run_test("exact_preflop_fold_terminal_values", test_exact_preflop_fold_terminal_values);
    run_test("preflop_all_in_equity_streaming_contract",
             test_preflop_all_in_equity_streaming_contract);
    run_test("preflop_all_in_terminal_consumes_exact_table_contract",
             test_preflop_all_in_terminal_consumes_exact_table_contract);
    run_test("best_response_preflop_recursion", test_best_response_preflop_recursion);
    run_test("river_batch_scheduler_contract", test_river_batch_scheduler_contract);
    run_test("sampled_river_policy_evaluator", test_sampled_river_policy_evaluator);
    run_test("sampled_bucket_is_globally_suit_isomorphic",
             test_sampled_bucket_is_globally_suit_isomorphic);
    run_test("exact_postflop_all_in_runout_enumeration",
             test_exact_postflop_all_in_runout_enumeration);
    run_test("exact_postflop_all_in_cache_is_transparent",
             test_exact_postflop_all_in_cache_is_transparent);
    run_test("sampled_solver_smoke", test_sampled_solver_smoke);
    std::cout << "HU_PREFLOP_TREE_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "HU_PREFLOP_TREE_TESTS=FAIL assertion=" << assertions << " reason=" << error.what()
              << '\n';
    return 1;
  }
}
