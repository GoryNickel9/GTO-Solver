#include "gtosd/core/cards.hpp"
#include "gtosd/core/game.hpp"
#include "gtosd/core/ranges.hpp"
#include "gtosd/isomorphism/isomorphism.hpp"
#include "gtosd/memory/memory.hpp"
#include "gtosd/postflop/postflop_solver.hpp"
#include "gtosd/solver/best_response.hpp"
#include "gtosd/solver/reference_games.hpp"
#include "gtosd/solver/solver.hpp"
#include "gtosd/tree/tree.hpp"
#include "gtosd/version.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::optional<std::uint64_t> parse_u64(const std::string_view text) {
  std::uint64_t value = 0;
  const auto *const begin = text.data();
  const auto *const end = begin + text.size();
  const auto parsed = std::from_chars(begin, end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end) {
    return std::nullopt;
  }
  return value;
}

std::optional<gtosd::PostflopBenchmark> parse_memory_benchmark(std::string_view text);

std::optional<gtosd::PostflopTreeConfig> load_postflop_config(const char *const path,
                                                              std::string &error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "cannot_open_config";
    return std::nullopt;
  }
  const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  const auto config = gtosd::parse_tree_config_json(json);
  if (!config) {
    error = gtosd::tree_config_error_name(config.error());
    return std::nullopt;
  }
  return config.value();
}

const char *postflop_stop_reason_name(const gtosd::PostflopStopReason reason) {
  switch (reason) {
  case gtosd::PostflopStopReason::Completed:
    return "completed";
  case gtosd::PostflopStopReason::Paused:
    return "paused";
  case gtosd::PostflopStopReason::Cancelled:
    return "cancelled";
  }
  return "unknown";
}

const char *action_type_name(const gtosd::ActionType type) {
  switch (type) {
  case gtosd::ActionType::Fold:
    return "fold";
  case gtosd::ActionType::Check:
    return "check";
  case gtosd::ActionType::Call:
    return "call";
  case gtosd::ActionType::Bet:
    return "bet";
  case gtosd::ActionType::Raise:
    return "raise";
  case gtosd::ActionType::AllIn:
    return "all-in";
  }
  return "unknown";
}

bool write_postflop_reports(const std::string &prefix, const gtosd::PostflopSolveResult &result,
                            const double elapsed_seconds, const std::uint64_t peak_rss_bytes,
                            const std::string_view backend) {
  if (prefix.empty()) {
    return false;
  }
  const auto &final = result.convergence.back();
  {
    std::ofstream json(prefix + ".json", std::ios::binary | std::ios::trunc);
    json << "{\n"
         << "  \"schema_version\": 1,\n"
         << "  \"status\": \"" << postflop_stop_reason_name(result.stop_reason) << "\",\n"
         << "  \"backend\": \"" << backend << "\",\n"
         << "  \"exact_outcomes\": true,\n"
         << "  \"uses_bucketing\": false,\n"
         << "  \"iterations\": " << result.checkpoint.completed_iterations << ",\n"
         << "  \"nodes\": " << result.public_tree.node_count << ",\n"
         << "  \"infosets\": " << result.information_sets << ",\n"
         << "  \"actions\": " << result.actions << ",\n"
         << "  \"ev_co_antes\": " << final.profile_value_antes[0] << ",\n"
         << "  \"ev_btn_antes\": " << final.profile_value_antes[1] << ",\n"
         << "  \"br_co_antes\": " << final.best_response_value_antes[0] << ",\n"
         << "  \"br_btn_antes\": " << final.best_response_value_antes[1] << ",\n"
         << "  \"nash_conv_antes\": " << final.nash_conv_antes << ",\n"
         << "  \"normalized_nash_conv\": " << final.normalized_nash_conv << ",\n"
         << "  \"maximum_normalization_error\": " << result.maximum_normalization_error << ",\n"
         << "  \"peak_rss_bytes\": " << peak_rss_bytes << ",\n"
         << "  \"elapsed_seconds\": " << elapsed_seconds << "\n"
         << "}\n";
    if (!json) {
      return false;
    }
  }
  {
    std::ofstream markdown(prefix + ".md", std::ios::binary | std::ios::trunc);
    markdown << "# GTOSD HU postflop solve report\n\n"
             << "| Metrica | Valore |\n"
             << "|---|---:|\n"
             << "| Stato | " << postflop_stop_reason_name(result.stop_reason) << " |\n"
             << "| Backend | " << backend << " |\n"
             << "| Iterazioni | " << result.checkpoint.completed_iterations << " |\n"
             << "| Nodi pubblici | " << result.public_tree.node_count << " |\n"
             << "| Infoset exact | " << result.information_sets << " |\n"
             << "| Azioni exact | " << result.actions << " |\n"
             << "| EV CO (ante) | " << final.profile_value_antes[0] << " |\n"
             << "| EV BTN (ante) | " << final.profile_value_antes[1] << " |\n"
             << "| BR CO (ante) | " << final.best_response_value_antes[0] << " |\n"
             << "| BR BTN (ante) | " << final.best_response_value_antes[1] << " |\n"
             << "| NashConv (ante) | " << final.nash_conv_antes << " |\n"
             << "| NashConv / pot | " << final.normalized_nash_conv << " |\n"
             << "| Errore massimo normalizzazione | " << result.maximum_normalization_error
             << " |\n"
             << "| Peak RSS (byte) | " << peak_rss_bytes << " |\n"
             << "| Tempo (s) | " << elapsed_seconds << " |\n\n"
             << "Turn e river sono enumerati esattamente. Nessun bucketing o sampling.\n";
    if (!markdown) {
      return false;
    }
  }
  return true;
}

struct PostflopRunArguments {
  const char *config_path;
  std::string_view iterations;
  const char *checkpoint_path;
  const char *report_prefix;
  std::string_view ram_gib;
  std::string_view disk_gib;
  std::string_view certification_interval;
  bool resume{false};
};

int run_postflop_solve(const PostflopRunArguments &arguments) {
  const auto iterations = parse_u64(arguments.iterations);
  const auto ram_gib = parse_u64(arguments.ram_gib);
  const auto disk_gib = parse_u64(arguments.disk_gib);
  const auto certification_interval = arguments.certification_interval.empty()
                                          ? std::optional<std::uint64_t>{1U}
                                          : parse_u64(arguments.certification_interval);
  constexpr std::uint64_t gib = 1ULL << 30U;
  if (!iterations || *iterations == 0U || !ram_gib || *ram_gib == 0U || !disk_gib ||
      !certification_interval || *certification_interval == 0U ||
      *ram_gib > std::numeric_limits<std::uint64_t>::max() / gib ||
      *disk_gib > std::numeric_limits<std::uint64_t>::max() / gib) {
    std::cerr << "postflop " << (arguments.resume ? "resume" : "solve")
              << " failed: invalid_argument\n";
    return 2;
  }
  std::string config_error;
  const auto config = load_postflop_config(arguments.config_path, config_error);
  if (!config) {
    std::cerr << "postflop solve failed: " << config_error << '\n';
    return 1;
  }
  const auto lazy = gtosd::analyze_postflop_config(*config, gtosd::MemoryPrototype::LazyInRam);
  const auto out_of_core =
      gtosd::analyze_postflop_config(*config, gtosd::MemoryPrototype::OutOfCore);
  if (!lazy || !out_of_core) {
    std::cerr << "postflop solve failed: preflight_failure\n";
    return 1;
  }
  const std::uint64_t ram_bytes = *ram_gib * gib;
  const std::uint64_t disk_bytes = *disk_gib * gib;
  const bool lazy_fits = lazy.value().memory.peak_resident_bytes <= ram_bytes;
  const bool out_of_core_fits = out_of_core.value().memory.peak_resident_bytes <= ram_bytes &&
                                out_of_core.value().memory.backing_store_bytes <= disk_bytes;
  if (!lazy_fits && !out_of_core_fits) {
    std::cerr << "postflop solve failed: insufficient_ram_or_disk\n";
    return 3;
  }

  std::optional<gtosd::PostflopCheckpoint> checkpoint;
  if (arguments.resume) {
    const auto loaded = gtosd::load_postflop_checkpoint(arguments.checkpoint_path);
    if (!loaded) {
      std::cerr << "postflop resume failed: " << gtosd::postflop_solver_error_name(loaded.error())
                << '\n';
      return 1;
    }
    checkpoint = loaded.value();
  }
  gtosd::MemoryPrototype backend =
      lazy_fits ? gtosd::MemoryPrototype::LazyInRam : gtosd::MemoryPrototype::OutOfCore;
  if (checkpoint && !checkpoint->external_buffer_file.empty()) {
    if (!out_of_core_fits) {
      std::cerr << "postflop resume failed: insufficient_ram_or_disk\n";
      return 3;
    }
    backend = gtosd::MemoryPrototype::OutOfCore;
  } else if (checkpoint && backend == gtosd::MemoryPrototype::OutOfCore) {
    std::cerr << "postflop resume failed: checkpoint_backend_mismatch\n";
    return 3;
  }
  const std::string_view backend_name =
      backend == gtosd::MemoryPrototype::LazyInRam ? "lazy-in-ram" : "out-of-core";
  const std::filesystem::path control_path = std::string(arguments.checkpoint_path) + ".control";
  std::error_code stale_control_error;
  std::filesystem::remove(control_path, stale_control_error);

  gtosd::PostflopSolveOptions options;
  options.iterations = *iterations;
  options.averaging_delay =
      checkpoint ? checkpoint->averaging_delay : std::min<std::uint64_t>(100U, *iterations / 10U);
  options.certification_interval = *certification_interval;
  options.memory_backend = backend;
  options.backing_file = std::string(arguments.checkpoint_path) + ".buffers";
  options.progress_callback = [](const gtosd::PostflopCertification &point) {
    std::cout << "progress iteration=" << point.iteration
              << " ev_co_antes=" << point.profile_value_antes[0]
              << " ev_btn_antes=" << point.profile_value_antes[1]
              << " br_co_antes=" << point.best_response_value_antes[0]
              << " br_btn_antes=" << point.best_response_value_antes[1]
              << " nash_conv_antes=" << point.nash_conv_antes
              << " normalized_nash_conv=" << point.normalized_nash_conv << std::endl;
  };
  options.checkpoint_callback =
      [path = std::string(arguments.checkpoint_path)](const gtosd::PostflopCertification &,
                                                      const gtosd::PostflopCheckpoint &current) {
        return gtosd::save_postflop_checkpoint(current, path).has_value();
      };
  options.control_callback = [&control_path](const std::uint64_t) {
    std::ifstream control(control_path, std::ios::binary);
    if (!control) {
      return gtosd::PostflopControlCommand::Continue;
    }
    std::string command;
    control >> command;
    control.close();
    std::error_code remove_error;
    std::filesystem::remove(control_path, remove_error);
    if (command == "pause") {
      return gtosd::PostflopControlCommand::Pause;
    }
    if (command == "cancel") {
      return gtosd::PostflopControlCommand::Cancel;
    }
    return gtosd::PostflopControlCommand::Continue;
  };

  std::cout << "GTOSD_POSTFLOP_SOLVE_1\n"
            << "backend=" << backend_name << " exact_outcomes=true bucketing=false"
            << " target_iterations=" << *iterations
            << " certification_interval=" << *certification_interval << std::endl;
  const auto started = std::chrono::steady_clock::now();
  const auto solved =
      gtosd::solve_postflop_exact(*config, options, checkpoint ? &*checkpoint : nullptr);
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  if (!solved) {
    std::cerr << "postflop solve failed: " << gtosd::postflop_solver_error_name(solved.error())
              << '\n';
    return 1;
  }
  const auto saved =
      gtosd::save_postflop_checkpoint(solved.value().checkpoint, arguments.checkpoint_path);
  if (!saved) {
    std::cerr << "postflop solve failed: " << gtosd::postflop_solver_error_name(saved.error())
              << '\n';
    return 1;
  }
  const auto peak_rss_bytes = gtosd::process_peak_rss_bytes();
  if (solved.value().convergence.empty() ||
      !write_postflop_reports(arguments.report_prefix, solved.value(), elapsed, peak_rss_bytes,
                              backend_name)) {
    std::cerr << "postflop solve failed: report_io_failure\n";
    return 1;
  }
  std::cout << "status=" << postflop_stop_reason_name(solved.value().stop_reason)
            << " completed_iterations=" << solved.value().checkpoint.completed_iterations
            << " checkpoint=" << arguments.checkpoint_path
            << " report_json=" << arguments.report_prefix << ".json"
            << " report_markdown=" << arguments.report_prefix << ".md"
            << " peak_rss_bytes=" << peak_rss_bytes << " elapsed_seconds=" << elapsed << std::endl;
  return 0;
}

int write_postflop_control(const char *const checkpoint_path, const std::string_view command) {
  const std::string control_path = std::string(checkpoint_path) + ".control";
  std::ofstream output(control_path, std::ios::binary | std::ios::trunc);
  output << command << '\n';
  output.flush();
  if (!output) {
    std::cerr << "postflop " << command << " failed: control_io_failure\n";
    return 1;
  }
  std::cout << "postflop " << command << " requested control_file=" << control_path << '\n';
  return 0;
}

int run_postflop_query(const char *const config_path, const char *const checkpoint_path,
                       const std::string_view node_text, const std::string_view combo_text) {
  const auto node = parse_u64(node_text);
  const auto combo = parse_u64(combo_text);
  std::string error;
  const auto config = load_postflop_config(config_path, error);
  const auto checkpoint = gtosd::load_postflop_checkpoint(checkpoint_path);
  if (!config || !checkpoint || !node || !combo || *combo >= 630U) {
    std::cerr << "postflop query failed: invalid_argument_or_checkpoint\n";
    return 1;
  }
  const auto query = gtosd::query_postflop_strategy(*config, checkpoint.value(), *node,
                                                    static_cast<gtosd::ComboId>(*combo));
  if (!query) {
    std::cerr << "postflop query failed: " << gtosd::postflop_solver_error_name(query.error())
              << '\n';
    return 1;
  }
  std::cout << "GTOSD_POSTFLOP_STRATEGY_1\n"
            << "public_node=" << *node << " combo=" << *combo << '\n';
  for (std::size_t action = 0; action < query.value().actions.size(); ++action) {
    std::cout << "action=" << action_type_name(query.value().actions[action].type)
              << " amount_units=" << query.value().actions[action].amount.units()
              << " probability=" << query.value().probabilities[action] << '\n';
  }
  return 0;
}

int run_postflop_certify(const char *const config_path, const char *const checkpoint_path) {
  std::string error;
  const auto config = load_postflop_config(config_path, error);
  const auto checkpoint = gtosd::load_postflop_checkpoint(checkpoint_path);
  if (!config || !checkpoint) {
    std::cerr << "postflop certify failed: invalid_config_or_checkpoint\n";
    return 1;
  }
  const auto certified = gtosd::certify_postflop_checkpoint(*config, checkpoint.value());
  if (!certified) {
    std::cerr << "postflop certify failed: " << gtosd::postflop_solver_error_name(certified.error())
              << '\n';
    return 1;
  }
  std::cout << "GTOSD_POSTFLOP_CERTIFICATION_1\n"
            << "game_fingerprint=" << checkpoint.value().game_fingerprint
            << " iteration=" << certified.value().iteration
            << " ev_co_antes=" << certified.value().profile_value_antes[0]
            << " ev_btn_antes=" << certified.value().profile_value_antes[1]
            << " payoff_sum_antes=" << certified.value().expected_payoff_sum_antes << '\n'
            << "br_co_antes=" << certified.value().best_response_value_antes[0]
            << " br_btn_antes=" << certified.value().best_response_value_antes[1]
            << " nash_conv_antes=" << certified.value().nash_conv_antes
            << " normalized_nash_conv=" << certified.value().normalized_nash_conv << '\n'
            << "gate_below_one_percent="
            << (certified.value().normalized_nash_conv < 0.01 ? "pass" : "fail") << '\n';
  return certified.value().normalized_nash_conv < 0.01 ? 0 : 4;
}

int compare_gto_plus_reference(const char *const config_path, const char *const checkpoint_path,
                               const char *const reference_path) {
  std::string error;
  const auto config = load_postflop_config(config_path, error);
  const auto checkpoint = gtosd::load_postflop_checkpoint(checkpoint_path);
  if (!config || !checkpoint) {
    std::cerr << "postflop compare-gto-plus failed: invalid_config_or_checkpoint\n";
    return 1;
  }
  const auto certified = gtosd::certify_postflop_checkpoint(*config, checkpoint.value());
  if (!certified) {
    std::cerr << "postflop compare-gto-plus failed: "
              << gtosd::postflop_solver_error_name(certified.error()) << '\n';
    return 1;
  }
  std::ifstream input(reference_path, std::ios::binary);
  nlohmann::json reference;
  double reference_co = 0.0;
  double reference_btn = 0.0;
  try {
    input >> reference;
    if (!input || !reference.is_object() || reference.value("schema_version", 0) != 1 ||
        reference.value("source", std::string{}) != "GTO+" ||
        reference.value("game_fingerprint", std::string{}) != checkpoint.value().game_fingerprint ||
        !reference.contains("ev_co_antes") || !reference["ev_co_antes"].is_number() ||
        !reference.contains("ev_btn_antes") || !reference["ev_btn_antes"].is_number()) {
      std::cerr << "postflop compare-gto-plus failed: reference_mismatch\n";
      return 1;
    }
    reference_co = reference["ev_co_antes"].get<double>();
    reference_btn = reference["ev_btn_antes"].get<double>();
  } catch (const nlohmann::json::exception &) {
    std::cerr << "postflop compare-gto-plus failed: invalid_reference_json\n";
    return 1;
  }
  if (!std::isfinite(reference_co) || !std::isfinite(reference_btn)) {
    std::cerr << "postflop compare-gto-plus failed: invalid_reference_value\n";
    return 1;
  }
  std::cout << "GTOSD_GTO_PLUS_COMPARISON_1\n"
            << "iteration=" << certified.value().iteration
            << " game_fingerprint=" << checkpoint.value().game_fingerprint << '\n'
            << "gtosd_ev_co_antes=" << certified.value().profile_value_antes[0]
            << " gto_plus_ev_co_antes=" << reference_co
            << " delta_ev_co_antes=" << certified.value().profile_value_antes[0] - reference_co
            << '\n'
            << "gtosd_ev_btn_antes=" << certified.value().profile_value_antes[1]
            << " gto_plus_ev_btn_antes=" << reference_btn
            << " delta_ev_btn_antes=" << certified.value().profile_value_antes[1] - reference_btn
            << '\n'
            << "gtosd_normalized_nash_conv=" << certified.value().normalized_nash_conv << '\n';
  return 0;
}

int run_postflop_validate(const char *const path) {
  std::string error;
  const auto config = load_postflop_config(path, error);
  if (!config) {
    std::cerr << "postflop validate failed: " << error << '\n';
    return 1;
  }
  const auto estimate = gtosd::estimate_public_tree(*config);
  if (!estimate) {
    std::cerr << "postflop validate failed: " << gtosd::tree_error_name(estimate.error()) << '\n';
    return 1;
  }
  std::cout << "GTOSD_POSTFLOP_VALIDATE_1\n"
            << "status=valid exact_outcomes=true bucketing=false\n"
            << "nodes=" << estimate.value().node_count << " edges=" << estimate.value().edge_count
            << " decision_nodes=" << estimate.value().decision_nodes
            << " chance_edges=" << estimate.value().chance_edges << '\n';
  return 0;
}

int run_postflop_estimate(const char *const path, const std::string_view ram_gib_text,
                          const std::string_view disk_gib_text) {
  const auto ram_gib = parse_u64(ram_gib_text);
  const auto disk_gib = parse_u64(disk_gib_text);
  constexpr std::uint64_t gib = 1ULL << 30U;
  if (!ram_gib || !disk_gib || *ram_gib == 0U ||
      *ram_gib > std::numeric_limits<std::uint64_t>::max() / gib ||
      *disk_gib > std::numeric_limits<std::uint64_t>::max() / gib) {
    std::cerr << "postflop estimate failed: invalid_resource_budget\n";
    return 2;
  }
  std::string error;
  const auto config = load_postflop_config(path, error);
  if (!config) {
    std::cerr << "postflop estimate failed: " << error << '\n';
    return 1;
  }
  const auto lazy = gtosd::analyze_postflop_config(*config, gtosd::MemoryPrototype::LazyInRam);
  const auto out_of_core =
      gtosd::analyze_postflop_config(*config, gtosd::MemoryPrototype::OutOfCore);
  if (!lazy || !out_of_core) {
    const auto failure = lazy ? out_of_core.error() : lazy.error();
    std::cerr << "postflop estimate failed: " << gtosd::memory_error_name(failure) << '\n';
    return 1;
  }
  const std::uint64_t ram_bytes = *ram_gib * gib;
  const std::uint64_t disk_bytes = *disk_gib * gib;
  const bool lazy_fits = lazy.value().memory.peak_resident_bytes <= ram_bytes;
  const bool out_of_core_fits = out_of_core.value().memory.peak_resident_bytes <= ram_bytes &&
                                out_of_core.value().memory.backing_store_bytes <= disk_bytes;
  const char *const selected =
      lazy_fits ? "lazy-in-ram" : (out_of_core_fits ? "out-of-core" : "rejected");
  std::cout << "GTOSD_POSTFLOP_ESTIMATE_1\n"
            << "exact_outcomes=true bucketing=false\n"
            << "nodes=" << lazy.value().public_tree.node_count
            << " infosets=" << lazy.value().information_sets << " actions=" << lazy.value().actions
            << '\n'
            << "lazy_peak_bytes=" << lazy.value().memory.peak_resident_bytes
            << " out_of_core_peak_bytes=" << out_of_core.value().memory.peak_resident_bytes
            << " out_of_core_backing_bytes=" << out_of_core.value().memory.backing_store_bytes
            << '\n'
            << "ram_budget_bytes=" << ram_bytes << " disk_budget_bytes=" << disk_bytes
            << " selected_backend=" << selected << '\n';
  if (!lazy_fits && !out_of_core_fits) {
    std::cerr << "postflop estimate rejected: insufficient_ram_or_disk\n";
    return 3;
  }
  return 0;
}

int write_postflop_benchmark_config(const std::string_view benchmark_text, const char *const path) {
  const auto benchmark = parse_memory_benchmark(benchmark_text);
  if (!benchmark) {
    std::cerr << "postflop benchmark-config failed: invalid_benchmark\n";
    return 2;
  }
  const auto config = gtosd::make_postflop_benchmark_config(*benchmark);
  if (!config) {
    std::cerr << "postflop benchmark-config failed: " << gtosd::memory_error_name(config.error())
              << '\n';
    return 1;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << gtosd::serialize_tree_config_json(config.value());
  output.flush();
  if (!output) {
    std::cerr << "postflop benchmark-config failed: io_failure\n";
    return 1;
  }
  std::cout << "postflop benchmark config written benchmark="
            << gtosd::postflop_benchmark_name(*benchmark) << " path=" << path << '\n';
  return 0;
}

int inspect_tree(const char *const path, const std::string_view maximum_nodes_text) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    std::cerr << "tree-inspect failed: cannot_open_config\n";
    return 1;
  }
  const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  const auto config = gtosd::parse_tree_config_json(json);
  if (!config) {
    std::cerr << "tree-inspect failed: " << gtosd::tree_config_error_name(config.error()) << '\n';
    return 1;
  }

  gtosd::TreeBuildOptions options;
  if (!maximum_nodes_text.empty()) {
    const auto *const begin = maximum_nodes_text.data();
    const auto *const end = begin + maximum_nodes_text.size();
    const auto parsed = std::from_chars(begin, end, options.maximum_nodes);
    if (parsed.ec != std::errc{} || parsed.ptr != end || options.maximum_nodes == 0U) {
      std::cerr << "tree-inspect failed: invalid_maximum_nodes\n";
      return 1;
    }
  }

  const auto estimate = gtosd::estimate_public_tree(config.value(), options);
  if (!estimate) {
    std::cerr << "tree-inspect preflight failed: " << gtosd::tree_error_name(estimate.error())
              << '\n';
    return 1;
  }
  const auto tree = gtosd::build_public_tree(config.value(), options);
  if (!tree) {
    std::cerr << "tree-inspect failed: " << gtosd::tree_error_name(tree.error()) << '\n';
    return 1;
  }
  const auto &stats = tree.value().stats;
  std::cout << "GTOSD_PUBLIC_TREE_1\n"
            << "preflight_nodes=" << estimate.value().node_count
            << " preflight_eager_bytes=" << estimate.value().estimated_eager_bytes << '\n'
            << "nodes=" << stats.node_count << " edges=" << stats.edge_count
            << " max_depth=" << stats.maximum_depth << '\n'
            << "decision_nodes=" << stats.decision_nodes << " chance_nodes=" << stats.chance_nodes
            << " terminal_fold_nodes=" << stats.terminal_fold_nodes
            << " terminal_showdown_nodes=" << stats.terminal_showdown_nodes << '\n'
            << "chance_edges=" << stats.chance_edges
            << " estimated_eager_bytes=" << stats.estimated_eager_bytes << '\n'
            << "betting_tree_hash=" << tree.value().betting_tree_hash << '\n';
  return 0;
}

std::string format_board(const std::uint64_t board_mask) {
  std::ostringstream output;
  bool first = true;
  for (std::uint8_t index = 0; index < 36U; ++index) {
    if ((board_mask & (std::uint64_t{1} << index)) == 0U) {
      continue;
    }
    if (!first) {
      output << ',';
    }
    first = false;
    output << gtosd::format_card(gtosd::CardId::from_index(index).value());
  }
  return output.str();
}

int audit_isomorphism(const char *const path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    std::cerr << "isomorphism-audit failed: cannot_open_config\n";
    return 1;
  }
  const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  const auto config = gtosd::parse_tree_config_json(json);
  if (!config) {
    std::cerr << "isomorphism-audit failed: " << gtosd::tree_config_error_name(config.error())
              << '\n';
    return 1;
  }
  const std::vector<gtosd::CardId> flop(config.value().flop.begin(), config.value().flop.end());
  const auto board_mask = gtosd::card_mask(flop);
  if (!board_mask) {
    std::cerr << "isomorphism-audit failed: invalid_board\n";
    return 1;
  }

  gtosd::CanonicalStateInput state;
  state.game_version = gtosd::PostflopTreeConfig::current_version;
  const auto public_state =
      gtosd::make_hu_postflop_state(gtosd::Street::Flop, config.value().initial_pot,
                                    config.value().effective_stack, board_mask.value());
  if (!public_state) {
    std::cerr << "isomorphism-audit failed: invalid_public_state\n";
    return 1;
  }
  state.public_state = public_state.value();

  gtosd::CanonicalKeyCache cache;
  const auto canonical = cache.canonicalize(state);
  const auto cached = cache.canonicalize(state);
  const auto orbit = gtosd::audit_orbit(state);
  if (!canonical || !cached || !orbit) {
    const auto error = canonical ? (cached ? orbit.error() : cached.error()) : canonical.error();
    std::cerr << "isomorphism-audit failed: " << gtosd::isomorphism_error_name(error) << '\n';
    return 1;
  }

  std::set<std::string> distinct;
  std::size_t canonical_representatives = 0;
  std::cout << "GTOSD_ISOMORPHISM_AUDIT_1\n"
            << "input_board=" << format_board(state.public_state.board_mask) << '\n'
            << "permutations=" << orbit.value().size() << '\n'
            << "physical_to_canonical="
            << gtosd::permutation_name(canonical.value().physical_to_canonical) << '\n'
            << "canonical_to_physical="
            << gtosd::permutation_name(canonical.value().canonical_to_physical) << '\n';
  for (std::size_t index = 0; index < orbit.value().size(); ++index) {
    const auto &entry = orbit.value()[index];
    const auto transformed =
        gtosd::transform_card_mask(state.public_state.board_mask, entry.permutation).value();
    distinct.insert(entry.serialized_state);
    canonical_representatives += entry.is_canonical ? 1U : 0U;
    std::cout << "orbit[" << index << "]=" << gtosd::permutation_name(entry.permutation)
              << " board=" << format_board(transformed)
              << " canonical=" << (entry.is_canonical ? "yes" : "no") << '\n';
  }
  std::cout << "distinct_physical_representations=" << distinct.size() << '\n'
            << "canonical_representatives=" << canonical_representatives << '\n'
            << "cache_queries=" << cache.stats().queries << " cache_hits=" << cache.stats().hits
            << " cache_misses=" << cache.stats().misses
            << " hash_collisions=" << cache.stats().hash_collisions
            << " hit_rate=" << cache.hit_rate() << '\n';
  return 0;
}

std::optional<gtosd::SolverAlgorithm> parse_algorithm(const std::string_view text) {
  if (text == "cfr") {
    return gtosd::SolverAlgorithm::VanillaCfr;
  }
  if (text == "cfr+") {
    return gtosd::SolverAlgorithm::CfrPlus;
  }
  if (text == "linear") {
    return gtosd::SolverAlgorithm::LinearCfr;
  }
  if (text == "dcfr") {
    return gtosd::SolverAlgorithm::Dcfr;
  }
  if (text == "mccfr") {
    return gtosd::SolverAlgorithm::ExternalSamplingMccfr;
  }
  return std::nullopt;
}

std::optional<gtosd::PostflopBenchmark> parse_memory_benchmark(const std::string_view text) {
  if (text == "pf-f1") {
    return gtosd::PostflopBenchmark::PfF1;
  }
  if (text == "pf-f2") {
    return gtosd::PostflopBenchmark::PfF2;
  }
  if (text == "pf-f3") {
    return gtosd::PostflopBenchmark::PfF3;
  }
  return std::nullopt;
}

std::optional<gtosd::MemoryPrototype> parse_memory_prototype(const std::string_view text) {
  if (text == "lazy") {
    return gtosd::MemoryPrototype::LazyInRam;
  }
  if (text == "street") {
    return gtosd::MemoryPrototype::StreetDecomposition;
  }
  if (text == "out-of-core") {
    return gtosd::MemoryPrototype::OutOfCore;
  }
  return std::nullopt;
}

struct MemoryLabArguments {
  std::string_view benchmark;
  std::string_view prototype;
  std::string_view resident_pages;
};

int run_memory_lab(const MemoryLabArguments arguments) {
  const auto benchmark = parse_memory_benchmark(arguments.benchmark);
  const auto prototype = parse_memory_prototype(arguments.prototype);
  gtosd::MemoryPrototypeOptions options;
  if (!arguments.resident_pages.empty()) {
    const auto resident_pages = parse_u64(arguments.resident_pages);
    if (!resident_pages) {
      std::cerr << "memory-lab failed: invalid_resident_pages\n";
      return 2;
    }
    options.resident_page_count = *resident_pages;
  }
  if (!benchmark || !prototype) {
    std::cerr << "memory-lab failed: invalid_argument\n";
    return 2;
  }
  const auto started = std::chrono::steady_clock::now();
  const auto report = gtosd::analyze_memory_prototype(*benchmark, *prototype, options);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  if (!report) {
    std::cerr << "memory-lab failed: " << gtosd::memory_error_name(report.error()) << '\n';
    return 1;
  }

  const auto &value = report.value();
  std::cout << "GTOSD_MEMORY_LAB_1\n"
            << "benchmark=" << gtosd::postflop_benchmark_name(value.benchmark)
            << " prototype=" << gtosd::memory_prototype_name(value.prototype) << '\n'
            << "exact_outcomes=" << (value.exact_outcomes ? "yes" : "no")
            << " bucketing=" << (value.uses_bucketing ? "yes" : "no") << '\n'
            << "nodes=" << value.public_tree.node_count << " edges=" << value.public_tree.edge_count
            << " infosets=" << value.information_sets << " actions=" << value.actions
            << " range_state_slots=" << value.range_state_slots << '\n';
  constexpr std::array<std::string_view, 3> street_names{"flop", "turn", "river"};
  for (std::size_t street = 0; street < street_names.size(); ++street) {
    std::cout << "street=" << street_names[street]
              << " nodes=" << value.public_tree.node_count_by_street[street]
              << " decisions=" << value.public_tree.decision_nodes_by_street[street]
              << " infosets=" << value.information_sets_by_street[street]
              << " actions=" << value.actions_by_street[street]
              << " range_state_slots=" << value.range_state_slots_by_street[street] << '\n';
  }
  const auto &memory = value.memory;
  std::cout << "public_tree_bytes=" << memory.public_tree_bytes
            << " infoset_index_bytes=" << memory.infoset_index_bytes
            << " action_bytes=" << memory.action_bytes << '\n'
            << "regret_bytes=" << memory.regret_bytes << " strategy_bytes=" << memory.strategy_bytes
            << " reach_bytes=" << memory.reach_bytes
            << " best_response_bytes=" << memory.best_response_bytes << '\n'
            << "boundary_bytes=" << memory.boundary_bytes
            << " checkpoint_staging_bytes=" << memory.checkpoint_staging_bytes
            << " gui_cache_bytes=" << memory.gui_cache_bytes << '\n'
            << "backing_store_bytes=" << memory.backing_store_bytes
            << " peak_resident_bytes=" << memory.peak_resident_bytes << '\n'
            << "bytes_per_node=" << value.bytes_per_public_node
            << " bytes_per_infoset=" << value.bytes_per_information_set << '\n'
            << "preflop_full_projection_bytes=" << value.preflop_full_projection_bytes << '\n'
            << "gate_pf_f1_12gib=";
  if (value.benchmark != gtosd::PostflopBenchmark::PfF1) {
    std::cout << "not_applicable\n";
  } else {
    std::cout << (memory.peak_resident_bytes <= 12ULL * 1'024ULL * 1'024ULL * 1'024ULL ? "pass"
                                                                                       : "fail")
              << '\n';
  }
  std::cout << "elapsed_seconds=" << std::chrono::duration<double>(elapsed).count() << '\n';
  return 0;
}

int run_memory_probe(const std::string_view benchmark_text, const char *const backing_file) {
  const auto benchmark = parse_memory_benchmark(benchmark_text);
  if (!benchmark) {
    std::cerr << "memory-probe failed: invalid_argument\n";
    return 2;
  }
  const auto report =
      gtosd::analyze_memory_prototype(*benchmark, gtosd::MemoryPrototype::OutOfCore);
  if (!report) {
    std::cerr << "memory-probe failed: " << gtosd::memory_error_name(report.error()) << '\n';
    return 1;
  }
  const auto started = std::chrono::steady_clock::now();
  const auto probe = gtosd::probe_out_of_core_residency(report.value(), backing_file);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  if (!probe) {
    std::cerr << "memory-probe failed: " << gtosd::memory_error_name(probe.error()) << '\n';
    return 1;
  }
  std::cout << "GTOSD_MEMORY_PROBE_1\n"
            << "benchmark=" << gtosd::postflop_benchmark_name(*benchmark)
            << " prototype=out_of_core\n"
            << "backing_file=" << backing_file << '\n'
            << "logical_backing_bytes=" << probe.value().logical_backing_bytes
            << " touched_bytes=" << probe.value().touched_bytes
            << " measured_peak_rss_bytes=" << probe.value().measured_peak_rss_bytes << '\n'
            << "page_reads=" << probe.value().page_reads
            << " page_writes=" << probe.value().page_writes << '\n'
            << "elapsed_seconds=" << std::chrono::duration<double>(elapsed).count() << '\n';
  return 0;
}

std::optional<gtosd::FiniteGame> make_lab_game(const std::string_view name) {
  if (name == "matching") {
    return gtosd::make_matching_pennies_game();
  }
  if (name == "kuhn") {
    return gtosd::make_kuhn_poker_game();
  }
  if (name == "leduc") {
    return gtosd::make_leduc_poker_game();
  }
  if (name == "short-deck-toy") {
    const auto game = gtosd::make_short_deck_river_toy_game();
    return game ? std::optional<gtosd::FiniteGame>{game.value()} : std::nullopt;
  }
  if (name == "short-deck-rake-toy") {
    const auto game = gtosd::make_short_deck_river_toy_game(0.05);
    return game ? std::optional<gtosd::FiniteGame>{game.value()} : std::nullopt;
  }
  return std::nullopt;
}

struct SolverLabArguments {
  std::string_view game;
  std::string_view algorithm;
  std::string_view iterations;
  std::string_view seed;
  std::string_view threads;
};

int run_solver_lab(const SolverLabArguments arguments) {
  const auto game = make_lab_game(arguments.game);
  const auto algorithm = parse_algorithm(arguments.algorithm);
  const auto iterations = parse_u64(arguments.iterations);
  const auto seed = arguments.seed.empty() ? std::optional<std::uint64_t>{0x47544f5344463541ULL}
                                           : parse_u64(arguments.seed);
  const auto threads =
      arguments.threads.empty() ? std::optional<std::uint64_t>{1U} : parse_u64(arguments.threads);
  if (!game || !algorithm || !iterations || *iterations == 0U || !seed || !threads ||
      *threads > 8U) {
    std::cerr << "solver-lab failed: invalid_argument\n";
    return 2;
  }

  gtosd::SolverConfig config;
  config.algorithm = *algorithm;
  config.iterations = *iterations;
  config.seed = *seed;
  config.thread_count = static_cast<std::uint32_t>(*threads);
  config.averaging_delay = *algorithm == gtosd::SolverAlgorithm::CfrPlus
                               ? std::min<std::uint64_t>(100, *iterations / 10)
                               : 0U;
  const auto summary = gtosd::validate_finite_game(*game);
  if (!summary) {
    std::cerr << "solver-lab failed: " << gtosd::solver_error_name(summary.error()) << '\n';
    return 1;
  }

  const auto started = std::chrono::steady_clock::now();
  const auto certified = gtosd::solve_with_certification(
      *game, config, std::max<std::uint64_t>(1U, *iterations / 10U));
  const auto elapsed = std::chrono::steady_clock::now() - started;
  if (!certified) {
    std::cerr << "solver-lab failed: " << gtosd::solver_error_name(certified.error()) << '\n';
    return 1;
  }
  const auto metrics = gtosd::calculate_nash_conv(*game, certified.value().solve.average_strategy);
  const auto checkpoint = gtosd::serialize_solver_checkpoint(certified.value().solve.checkpoint);
  if (!metrics || !checkpoint) {
    const auto error = metrics ? checkpoint.error() : metrics.error();
    std::cerr << "solver-lab failed: " << gtosd::solver_error_name(error) << '\n';
    return 1;
  }
  const double elapsed_seconds = std::chrono::duration<double>(elapsed).count();
  const double iterations_per_second =
      elapsed_seconds > 0.0 ? static_cast<double>(*iterations) / elapsed_seconds : 0.0;

  std::cout << "GTOSD_SOLVER_LAB_1\n"
            << "game=" << game->game_id << " fingerprint=" << summary.value().fingerprint << '\n'
            << "nodes=" << summary.value().nodes << " infosets=" << summary.value().information_sets
            << " max_depth=" << summary.value().maximum_depth << '\n'
            << "algorithm=" << gtosd::solver_algorithm_name(*algorithm) << " seed=" << *seed
            << " threads=" << *threads << " iterations=" << *iterations << '\n';
  for (const auto &point : certified.value().convergence) {
    std::cout << "curve iteration=" << point.iteration << " ev_co=" << point.profile_value[0]
              << " ev_btn=" << point.profile_value[1] << " nash_conv=" << point.nash_conv << '\n';
  }
  std::cout << "final_ev_co=" << metrics.value().profile_value[0]
            << " final_ev_btn=" << metrics.value().profile_value[1]
            << " payoff_sum=" << metrics.value().expected_payoff_sum << '\n'
            << "br_co=" << metrics.value().best_response_value[0]
            << " br_btn=" << metrics.value().best_response_value[1]
            << " nash_conv=" << metrics.value().nash_conv
            << " normalized_nash_conv=" << metrics.value().normalized_nash_conv << '\n'
            << "zero_sum_exploitability=";
  if (std::isnan(metrics.value().zero_sum_exploitability)) {
    std::cout << "not_applicable_general_sum\n";
  } else {
    std::cout << metrics.value().zero_sum_exploitability << '\n';
  }
  std::cout << "elapsed_seconds=" << elapsed_seconds
            << " iterations_per_second=" << iterations_per_second
            << " traversed_nodes=" << certified.value().solve.traversed_nodes
            << " checkpoint_bytes=" << checkpoint.value().size()
            << " normalization_error=" << certified.value().solve.maximum_normalization_error
            << '\n';
  return 0;
}

struct DcfrSweepArguments {
  std::string_view game;
  std::string_view iterations;
};

int run_dcfr_sweep(const DcfrSweepArguments arguments) {
  const auto game = make_lab_game(arguments.game);
  const auto iterations = parse_u64(arguments.iterations);
  if (!game || !iterations || *iterations == 0U) {
    std::cerr << "dcfr-sweep failed: invalid_argument\n";
    return 2;
  }
  constexpr std::array<gtosd::DcfrParameters, 6> candidates{{
      {1.0, 0.0, 1.0},
      {1.5, 0.0, 2.0},
      {2.0, 0.0, 2.0},
      {1.5, -0.5, 2.0},
      {1.5, 0.5, 2.0},
      {1.5, 0.0, 3.0},
  }};

  std::cout << "GTOSD_DCFR_SWEEP_1\n"
            << "game=" << game->game_id << " iterations=" << *iterations
            << " candidates=" << candidates.size() << '\n';
  for (const auto &parameters : candidates) {
    gtosd::SolverConfig config;
    config.algorithm = gtosd::SolverAlgorithm::Dcfr;
    config.iterations = *iterations;
    config.dcfr = parameters;
    const auto started = std::chrono::steady_clock::now();
    const auto solved = gtosd::solve_finite_game(*game, config);
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (!solved) {
      std::cerr << "dcfr-sweep failed: " << gtosd::solver_error_name(solved.error()) << '\n';
      return 1;
    }
    const auto metrics = gtosd::calculate_nash_conv(*game, solved.value().average_strategy);
    if (!metrics) {
      std::cerr << "dcfr-sweep failed: " << gtosd::solver_error_name(metrics.error()) << '\n';
      return 1;
    }
    std::cout << "alpha=" << parameters.positive_regret_exponent
              << " beta=" << parameters.negative_regret_exponent
              << " gamma=" << parameters.strategy_exponent
              << " ev_co=" << metrics.value().profile_value[0]
              << " nash_conv=" << metrics.value().nash_conv << " elapsed_seconds=" << elapsed
              << '\n';
  }
  return 0;
}

void print_usage() {
  std::cout << "Usage:\n"
            << "  gto_cli self-check\n"
            << "  gto_cli tree-inspect <config.json> [maximum_nodes]\n"
            << "  gto_cli isomorphism-audit <config.json>\n"
            << "  gto_cli solver-lab <game> <algorithm> <iterations> [seed] [threads]\n"
            << "  gto_cli dcfr-sweep <game> <iterations>\n"
            << "  gto_cli memory-lab <pf-f1|pf-f2|pf-f3> "
               "<lazy|street|out-of-core> [resident_pages]\n"
            << "  gto_cli memory-probe <pf-f1|pf-f2|pf-f3> <backing_file>\n"
            << "  gto_cli postflop validate <config.json>\n"
            << "  gto_cli postflop estimate <config.json> <ram_gib> <disk_gib>\n"
            << "  gto_cli postflop solve <config.json> <iterations> <checkpoint> "
               "<report_prefix> <ram_gib> <disk_gib> [cert_interval]\n"
            << "  gto_cli postflop resume <config.json> <iterations> <checkpoint> "
               "<report_prefix> <ram_gib> <disk_gib> [cert_interval]\n"
            << "  gto_cli postflop pause|cancel <checkpoint>\n"
            << "  gto_cli postflop query <config.json> <checkpoint> <node> <combo_id>\n"
            << "  gto_cli postflop certify <config.json> <checkpoint>\n"
            << "  gto_cli postflop compare-gto-plus <config.json> <checkpoint> "
               "<reference.json>\n"
            << "  gto_cli postflop benchmark-config <pf-f1|pf-f2|pf-f3> <output.json>\n"
            << "    game: matching|kuhn|leduc|short-deck-toy|short-deck-rake-toy\n"
            << "    algorithm: cfr|cfr+|linear|dcfr|mccfr\n";
}

} // namespace

int run_cli(const int argc, const char *const argv[]) {
  if (argc == 2 && std::string_view(argv[1]) == "self-check") {
    const auto ante = gtosd::Money::from_antes(1).value();
    const auto stack = gtosd::Money::from_antes(40).value();
    const auto state = gtosd::make_hu_preflop_state(stack, ante);
    if (!state) {
      std::cerr << "self-check failed\n";
      return 1;
    }
    std::cout << "GTOSD " << gtosd::api_version_string << '\n'
              << "deck_cards=36 combos=" << gtosd::all_combos().size() << " hand_classes=81\n"
              << "positions=CO,BTN actor=CO root_pot_units=" << state.value().pot.units()
              << " call_units=" << gtosd::amount_to_call(state.value(), 0).units() << '\n';
    return 0;
  }
  if ((argc == 3 || argc == 4) && std::string_view(argv[1]) == "tree-inspect") {
    return inspect_tree(argv[2], argc == 4 ? std::string_view(argv[3]) : std::string_view{});
  }
  if (argc == 3 && std::string_view(argv[1]) == "isomorphism-audit") {
    return audit_isomorphism(argv[2]);
  }
  if (argc >= 5 && argc <= 7 && std::string_view(argv[1]) == "solver-lab") {
    return run_solver_lab({argv[2], argv[3], argv[4],
                           argc >= 6 ? std::string_view(argv[5]) : std::string_view{},
                           argc == 7 ? std::string_view(argv[6]) : std::string_view{}});
  }
  if (argc == 4 && std::string_view(argv[1]) == "dcfr-sweep") {
    return run_dcfr_sweep({argv[2], argv[3]});
  }
  if ((argc == 4 || argc == 5) && std::string_view(argv[1]) == "memory-lab") {
    return run_memory_lab(
        {argv[2], argv[3], argc == 5 ? std::string_view(argv[4]) : std::string_view{}});
  }
  if (argc == 4 && std::string_view(argv[1]) == "memory-probe") {
    return run_memory_probe(argv[2], argv[3]);
  }
  if (argc == 4 && std::string_view(argv[1]) == "postflop" &&
      std::string_view(argv[2]) == "validate") {
    return run_postflop_validate(argv[3]);
  }
  if (argc == 6 && std::string_view(argv[1]) == "postflop" &&
      std::string_view(argv[2]) == "estimate") {
    return run_postflop_estimate(argv[3], argv[4], argv[5]);
  }
  if ((argc == 9 || argc == 10) && std::string_view(argv[1]) == "postflop" &&
      (std::string_view(argv[2]) == "solve" || std::string_view(argv[2]) == "resume")) {
    return run_postflop_solve({argv[3], argv[4], argv[5], argv[6], argv[7], argv[8],
                               argc == 10 ? std::string_view(argv[9]) : std::string_view{},
                               std::string_view(argv[2]) == "resume"});
  }
  if (argc == 4 && std::string_view(argv[1]) == "postflop" &&
      (std::string_view(argv[2]) == "pause" || std::string_view(argv[2]) == "cancel")) {
    return write_postflop_control(argv[3], argv[2]);
  }
  if (argc == 7 && std::string_view(argv[1]) == "postflop" &&
      std::string_view(argv[2]) == "query") {
    return run_postflop_query(argv[3], argv[4], argv[5], argv[6]);
  }
  if (argc == 5 && std::string_view(argv[1]) == "postflop" &&
      std::string_view(argv[2]) == "certify") {
    return run_postflop_certify(argv[3], argv[4]);
  }
  if (argc == 6 && std::string_view(argv[1]) == "postflop" &&
      std::string_view(argv[2]) == "compare-gto-plus") {
    return compare_gto_plus_reference(argv[3], argv[4], argv[5]);
  }
  if (argc == 5 && std::string_view(argv[1]) == "postflop" &&
      std::string_view(argv[2]) == "benchmark-config") {
    return write_postflop_benchmark_config(argv[3], argv[4]);
  }
  print_usage();
  return argc == 1 ? 0 : 2;
}

int main(const int argc, const char *const argv[]) {
  try {
    return run_cli(argc, argv);
  } catch (const std::exception &error) {
    std::fprintf(stderr, "gto_cli failed: internal_exception: %s\n", error.what());
    return 1;
  } catch (...) {
    std::fputs("gto_cli failed: unknown_internal_exception\n", stderr);
    return 1;
  }
}
