// Research-only finite corpus. Reuses the independent test tree builder;
// no production policy, table or game configuration is changed.
#include "../tests/preflop_blueprint_test_support.hpp"
#include "finite_game_audit_json.hpp"
#include "gtosd/preflop_blueprint/policy_file.hpp"
#include "gtosd/solver/enumerated_best_response.hpp"

#include <fstream>
#include <optional>
#include <tuple>

namespace {
using namespace pb_test;
using Json = nlohmann::json;

gtosd::FiniteGame retain_history(gtosd::FiniteGame game) {
  using Key = std::tuple<std::uint8_t, std::size_t, std::string, gtosd::GameActionId>;
  std::map<Key, std::size_t> interned;
  struct Pending {
    gtosd::GameNodeId node;
    std::array<std::size_t, 2> history;
  };
  std::vector<Pending> pending{{game.root, {0, 0}}};
  while (!pending.empty()) {
    const auto current = pending.back();
    pending.pop_back();
    auto &node = game.nodes[current.node];
    const auto original = node.information_set;
    if (node.kind == gtosd::GameNodeKind::Decision) {
      node.information_set += "|memory=" + std::to_string(current.history[node.player]);
    }
    for (const auto &edge : node.edges) {
      auto next = current.history;
      if (node.kind == gtosd::GameNodeKind::Decision) {
        const auto key = Key{node.player, next[node.player], original, edge.action.id};
        next[node.player] = interned.emplace(key, interned.size() + 1).first->second;
      }
      pending.push_back({edge.child, next});
    }
  }
  return game;
}

std::map<std::string, std::string> lift_keys(const gtosd::FiniteGame &physical,
                                             const gtosd::FiniteGame &abstract) {
  require(physical.nodes.size() == abstract.nodes.size(), "same physical tree size");
  std::map<std::string, std::string> result;
  for (std::size_t i = 0; i < physical.nodes.size(); ++i) {
    const auto &p = physical.nodes[i];
    const auto &a = abstract.nodes[i];
    require(p.kind == a.kind && p.edges == a.edges && p.payoff == a.payoff && p.player == a.player,
            "partition changes only information set keys");
    if (p.kind == gtosd::GameNodeKind::Decision) {
      const auto [found, inserted] = result.emplace(p.information_set, a.information_set);
      require(inserted || found->second == a.information_set,
              "abstract key depends only on physical information, never future cards");
    }
  }
  return result;
}

Resources required_resources(const std::filesystem::path &resource_dir,
                             const std::filesystem::path &bucket_dir) {
  Resources resources;
  auto ranks = ca::RankTable::load(resource_dir / "rank_table_v1.bin");
  auto all_in = ca::AllInTable::load(resource_dir / "preflop_all_in_v1.bin");
  auto flop = ca::BucketTable::load(bucket_dir / "flop_buckets_v1.bin");
  auto turn = ca::BucketTable::load(bucket_dir / "turn_buckets_v1.bin");
  auto river = ca::BucketTable::load(bucket_dir / "river_buckets_v1.bin");
  require(ranks && all_in && flop && turn && river,
          "all real resources load; no synthetic fallback");
  resources.ranks.emplace(std::move(ranks.value()));
  resources.all_in.emplace(std::move(all_in.value()));
  resources.flop.emplace(std::move(flop.value()));
  resources.turn.emplace(std::move(turn.value()));
  resources.river.emplace(std::move(river.value()));
  resources.catalog.emplace(ca::BoardCatalog::build());
  resources.loaded = resources.buckets_loaded = true;
  return resources;
}
} // namespace

int main(const int argc, char **argv) {
  try {
    require(argc >= 4,
            "usage: short_deck_constrained_audit RESOURCES BUCKETS OUTPUT.json [options]");
    bool conflict = false, long_sampling = false;
    std::filesystem::path saved_path, reference_path;
    for (int i = 4; i < argc; ++i) {
      const std::string_view option = argv[i];
      if (option == "--conflict-witness") {
        conflict = true;
      } else if (option == "--long-sampling") {
        long_sampling = true;
      } else if (option == "--saved-policy" && i + 1 < argc) {
        saved_path = argv[++i];
      } else if (option == "--reference-certificate" && i + 1 < argc) {
        reference_path = argv[++i];
      } else {
        throw std::runtime_error("unknown option or missing value");
      }
    }
    require(saved_path.empty() == reference_path.empty(),
            "saved policy requires reference certificate");
    require(!long_sampling || (conflict && saved_path.empty()),
            "long sampling extends only the fixed conflict corpus");
    const std::array<std::uint64_t, 3> checkpoints =
        long_sampling ? std::array<std::uint64_t, 3>{2500, 10000, 100000}
                      : std::array<std::uint64_t, 3>{25, 250, 2500};
    const auto start = Clock::now();
    const auto resources = required_resources(argv[1], argv[2]);
    const auto rows =
        pb::ClassBucketRows::build(*resources.flop, *resources.turn, *resources.river);
    require(rows.has_value(), "real class row mapping builds");
    auto subsets = oracle_subsets();
    for (auto &combos : subsets.combos) {
      combos = {combos.front(), combos.back()};
    }
    if (conflict) {
      subsets.combos[0] = {ca::combo_index(card("Ac"), card("Ad")),
                           ca::combo_index(card("Ad"), card("Ah"))};
      subsets.combos[1] = {ca::combo_index(card("Ks"), card("Kh")),
                           ca::combo_index(card("Qs"), card("Qh"))};
    }
    pb::TrainingBoards boards;
    Json board_records = Json::array();
    using Flops = std::array<std::array<std::string_view, 3>, 2>;
    const Flops flops = conflict ? Flops{{{"6c", "7d", "Qc"}, {"9c", "9d", "Qc"}}}
                                 : Flops{{{"6s", "7d", "8c"}, {"6d", "7h", "8s"}}};
    const std::array<std::string_view, 2> turns = conflict
                                                      ? std::array<std::string_view, 2>{"8c", "Kd"}
                                                      : std::array<std::string_view, 2>{"9h", "9c"};
    const std::array<std::string_view, 2> rivers =
        conflict ? std::array<std::string_view, 2>{"Jc", "Qd"}
                 : std::array<std::string_view, 2>{"As", "Ad"};
    for (const auto &flop : flops) {
      for (const auto turn : turns) {
        for (const auto river : rivers) {
          boards.histories.push_back(make_history({flop[0], flop[1], flop[2], turn, river}));
          boards.weights.push_back(static_cast<double>(boards.weights.size() + 1));
          board_records.push_back({{"cards", {flop[0], flop[1], flop[2], turn, river}},
                                   {"weight", boards.weights.back()}});
        }
      }
    }
    boards.sample = false;
    if (conflict) {
      pb::AbstractionTables tables{&*resources.catalog, &*resources.flop, &*resources.turn,
                                   &*resources.river, &rows.value()};
      for (std::size_t i = 0; i < 2; ++i) {
        const auto context =
            pb::BoardContext::build(boards.histories[i * 4], *resources.ranks, &tables);
        require(context &&
                    context.value().row(gtosd::Street::Flop,
                                        context.value().hand_index(subsets.combos[0][i])) == 28,
                "both real CO40 witnesses map to historical class row 28");
      }
    }
    Json output{
        {"schema", "gtosd.research.short_deck_constrained_corpus.v1"},
        {"scope", "8_board_2_hands_per_player_diagnostic_not_full_co40"},
        {"corpus", conflict ? "co40_node4_self_reach_row28_witness" : "existing_trainer_oracle"},
        {"boards", board_records},
        {"hand_subsets", subsets.combos},
        {"iterations", checkpoints},
        {"sampling_seeds", {101, 202, 303}},
        {"flop_fingerprint", resources.flop->fingerprint()},
        {"turn_fingerprint", resources.turn->fingerprint()},
        {"river_fingerprint", resources.river->fingerprint()}};
    output["games"] = Json::array();
    for (const int stack : {20, 40}) {
      if ((!saved_path.empty() || long_sampling) && stack != 40) {
        continue;
      }
      auto config = load_fixture("preflop_blueprint_co40_test_v1.json");
      config.effective_stack =
          gtosd::Money::from_units(static_cast<std::int64_t>(stack) * gtosd::Money::units_per_ante)
              .value();
      const auto game = pb::CompiledGame::compile(config);
      require(game.has_value(), "matched-stack public game compiles");
      const auto physical = FiniteGameBuilder(game.value(), resources, true).build(boards, subsets);
      const auto bucketed =
          FiniteGameBuilder(game.value(), resources, false, nullptr, &rows.value())
              .build(boards, subsets);
      gtosd::StrategyProfile saved_physical;
      if (!saved_path.empty()) {
        const auto saved = pb::load_policy(saved_path, game.value());
        require(saved.has_value(), "saved policy matches the compiled tree");
        std::ifstream reference_file(reference_path);
        const auto reference = Json::parse(reference_file);
        const std::array<std::uint32_t, 3> capacities{rows.value().count(ca::BucketStreet::Flop),
                                                      rows.value().count(ca::BucketStreet::Turn),
                                                      rows.value().count(ca::BucketStreet::River)};
        const auto &layout = saved.value()->layout();
        require(reference.at("tree_fingerprint") == game.value().fingerprint() &&
                    reference.at("policy_fingerprint") == pb::policy_fingerprint(*saved.value()) &&
                    reference.at("flop_table_fingerprint") == resources.flop->fingerprint() &&
                    reference.at("turn_table_fingerprint") == resources.turn->fingerprint() &&
                    reference.at("river_table_fingerprint") == resources.river->fingerprint() &&
                    reference.at("capacities") == Json(capacities) &&
                    capacities == std::array<std::uint32_t, 3>{layout.flop_capacity,
                                                               layout.turn_capacity,
                                                               layout.river_capacity},
                "saved class identity matches");
        output["saved_policy_fingerprint"] = pb::policy_fingerprint(*saved.value());
        FiniteGameBuilder source(game.value(), resources, true, saved.value().get(), &rows.value());
        const auto source_game = source.build(boards, subsets);
        require(source_game.nodes.size() == physical.nodes.size(), "saved physical tree matches");
        saved_physical = source.profile();
      }
      for (const std::string partition : {"class", "history", "lossless"}) {
        auto finite = partition == "class"     ? bucketed
                      : partition == "history" ? retain_history(bucketed)
                                               : physical;
        finite.game_id += "." + partition + ".stack" + std::to_string(stack);
        const auto mapping = lift_keys(physical, finite);
        require(partition == "class" || (gtosd::has_perfect_recall(finite, 0).value() &&
                                         gtosd::has_perfect_recall(finite, 1).value()),
                "history and lossless partitions retain perfect recall");
        Json record{{"stack", stack},
                    {"partition", partition},
                    {"game", gtosd::research::finite_game_json(finite)},
                    {"public_tree_fingerprint", game.value().fingerprint()},
                    {"recall",
                     {gtosd::has_perfect_recall(finite, 0).value(),
                      gtosd::has_perfect_recall(finite, 1).value()}}};
        record["runs"] = Json::array();
        if (!saved_path.empty()) {
          gtosd::StrategyProfile lifted;
          for (const auto &[physical_key, abstract_key] : mapping) {
            const auto &strategy = saved_physical.at(physical_key);
            const auto [found, inserted] = lifted.emplace(abstract_key, strategy);
            require(inserted || found->second == strategy,
                    "same saved policy across the partition");
          }
          const auto evaluation = gtosd::calculate_nash_conv(physical, saved_physical);
          const auto value = gtosd::evaluate_strategy_profile(finite, lifted);
          require(evaluation && value &&
                      close(value.value()[0], evaluation.value().profile_value[0], 1e-10) &&
                      close(value.value()[1], evaluation.value().profile_value[1], 1e-10),
                  "saved-policy lift preserves both values");
          record["runs"].push_back({{"algorithm", "saved_co40_class"},
                                    {"seed", nullptr},
                                    {"iterations", nullptr},
                                    {"profile", gtosd::research::profile_json(lifted)},
                                    {"ev", evaluation.value().profile_value},
                                    {"physical_br", evaluation.value().best_response_value},
                                    {"physical_nashconv", evaluation.value().nash_conv}});
          output["games"].push_back(std::move(record));
          continue;
        }
        for (const auto algorithm :
             {gtosd::SolverAlgorithm::LinearCfr, gtosd::SolverAlgorithm::LinearMccfr}) {
          if (long_sampling && algorithm == gtosd::SolverAlgorithm::LinearCfr) {
            continue;
          }
          for (const std::uint64_t seed : {101U, 202U, 303U}) {
            if (algorithm == gtosd::SolverAlgorithm::LinearCfr && seed != 101) {
              continue;
            }
            std::optional<gtosd::SolverCheckpoint> checkpoint;
            for (const std::uint64_t iterations : checkpoints) {
              gtosd::SolverConfig solve_config;
              solve_config.algorithm = algorithm;
              solve_config.seed = seed;
              solve_config.iterations = iterations;
              const auto segment_start = Clock::now();
              const auto solved = gtosd::solve_finite_game(finite, solve_config,
                                                           checkpoint ? &*checkpoint : nullptr);
              require(solved.has_value(), "reduced training completes");
              checkpoint = solved.value().checkpoint;
              const double segment_seconds =
                  std::chrono::duration<double>(Clock::now() - segment_start).count();
              if (long_sampling) {
                const auto checkpoint_path = std::string(argv[3]) + "." + partition + "." +
                                             std::to_string(seed) + ".checkpoint.json";
                require(gtosd::save_solver_checkpoint(*checkpoint, checkpoint_path).has_value(),
                        "long trajectory checkpoint persists");
              }
              const auto &profile = solved.value().average_strategy;
              gtosd::StrategyProfile lifted;
              for (const auto &[key, source] : mapping) {
                lifted.emplace(key, profile.at(source));
              }
              const auto evaluation = gtosd::calculate_nash_conv(physical, lifted);
              const auto abstract_ev = gtosd::evaluate_strategy_profile(finite, profile);
              require(evaluation && abstract_ev, "physical certificate and abstract EV compute");
              for (std::size_t player = 0; player < 2; ++player) {
                require(close(abstract_ev.value()[player], evaluation.value().profile_value[player],
                              1e-10),
                        "policy lift preserves EV");
              }
              record["runs"].push_back({{"algorithm", gtosd::solver_algorithm_name(algorithm)},
                                        {"seed", seed},
                                        {"iterations", iterations},
                                        {"segment_seconds", segment_seconds},
                                        {"segment_traversed_nodes", solved.value().traversed_nodes},
                                        {"profile", gtosd::research::profile_json(profile)},
                                        {"ev", evaluation.value().profile_value},
                                        {"physical_br", evaluation.value().best_response_value},
                                        {"physical_nashconv", evaluation.value().nash_conv}});
              if (long_sampling) {
                const auto current = gtosd::current_strategy_profile(*checkpoint);
                require(current.has_value(), "current strategy extracts for averaging diagnostic");
                gtosd::StrategyProfile current_lifted;
                for (const auto &[key, source] : mapping) {
                  current_lifted.emplace(key, current.value().at(source));
                }
                const auto current_metric = gtosd::calculate_nash_conv(physical, current_lifted);
                require(current_metric.has_value(), "current policy physical BR computes");
                auto &last = record["runs"].back();
                last["current_ev"] = current_metric.value().profile_value;
                last["current_physical_br"] = current_metric.value().best_response_value;
                last["current_physical_nashconv"] = current_metric.value().nash_conv;
              }
            }
            std::cout << "stack=" << stack << " partition=" << partition
                      << " algorithm=" << gtosd::solver_algorithm_name(algorithm)
                      << " seed=" << seed << " complete\n"
                      << std::flush;
          }
        }
        output["games"].push_back(std::move(record));
      }
    }
    output["seconds"] = std::chrono::duration<double>(Clock::now() - start).count();
    std::ofstream file(argv[3]);
    file << output.dump() << '\n';
    file.close();
    require(static_cast<bool>(file), "corpus writes");
    std::cout << "SHORT_DECK_CONSTRAINED_AUDIT=PASS seconds=" << output["seconds"] << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SHORT_DECK_CONSTRAINED_AUDIT=FAIL " << error.what() << '\n';
    return 1;
  }
}
