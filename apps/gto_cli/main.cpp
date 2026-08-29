#include "gtosd/core/cards.hpp"
#include "gtosd/core/game.hpp"
#include "gtosd/core/ranges.hpp"
#include "gtosd/isomorphism/isomorphism.hpp"
#include "gtosd/memory/memory.hpp"
#include "gtosd/postflop/canonical_layout.hpp"
#include "gtosd/postflop/postflop_solver.hpp"
#include "gtosd/solver/best_response.hpp"
#include "gtosd/solver/reference_games.hpp"
#include "gtosd/solver/solver.hpp"
#include "gtosd/storage/storage.hpp"
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
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
LONG WINAPI report_unhandled_exception(EXCEPTION_POINTERS *exception) {
  void *frames[64]{};
  const auto frame_count = CaptureStackBackTrace(0U, 64U, frames, nullptr);
  const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
  const auto address = reinterpret_cast<std::uintptr_t>(
      exception != nullptr && exception->ExceptionRecord != nullptr
          ? exception->ExceptionRecord->ExceptionAddress
          : nullptr);
  const auto code = exception != nullptr && exception->ExceptionRecord != nullptr
                        ? exception->ExceptionRecord->ExceptionCode
                        : 0U;
  std::fprintf(stderr,
               "gto_cli unhandled_windows_exception code=0x%08lx address=0x%llx "
               "module_base=0x%llx module_offset=0x%llx frames=%u\n",
               static_cast<unsigned long>(code), static_cast<unsigned long long>(address),
               static_cast<unsigned long long>(base),
               static_cast<unsigned long long>(address >= base ? address - base : 0U),
               static_cast<unsigned>(frame_count));
  for (USHORT index = 0U; index < frame_count; ++index) {
    const auto frame = reinterpret_cast<std::uintptr_t>(frames[index]);
    std::fprintf(stderr, "gto_cli stack[%u]=0x%llx module_offset=0x%llx\n",
                 static_cast<unsigned>(index), static_cast<unsigned long long>(frame),
                 static_cast<unsigned long long>(frame >= base ? frame - base : 0U));
  }
  std::fflush(stderr);
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif

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

std::optional<std::string> environment_value(const char *const name) {
#pragma warning(push)
#pragma warning(disable : 4996)
  const char *const value = std::getenv(name);
#pragma warning(pop)
  return value == nullptr ? std::nullopt
                          : std::optional<std::string>{value};
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
  case gtosd::PostflopStopReason::Converged:
    return "converged";
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

gtosd::PostflopTreeConfig make_gto_plus_parity_config() {
  auto config = gtosd::make_postflop_benchmark_config(gtosd::PostflopBenchmark::PfF1).value();
  config.flop = {gtosd::parse_card("Ah").value(), gtosd::parse_card("Kh").value(),
                 gtosd::parse_card("Qh").value()};
  config.initial_pot = gtosd::Money::from_antes(40).value();
  config.effective_stack = gtosd::Money::from_antes(100).value();
  config.rake.enabled = true;
  config.rake.percentage = gtosd::RangeWeight::from_basis_points(0).value();
  config.rake.cap = gtosd::Money{};
  const auto half_pot = gtosd::PotPercentage::from_basis_points(5'000).value();
  for (auto &street : config.streets) {
    for (auto &player : street.players) {
      player[static_cast<std::size_t>(gtosd::BettingScenario::Lead)].aggressive_sizes = {half_pot};
      player[static_cast<std::size_t>(gtosd::BettingScenario::AfterCheck)].aggressive_sizes = {
          half_pot};
      player[static_cast<std::size_t>(gtosd::BettingScenario::FacingBet)].aggressive_sizes = {
          half_pot};
      for (auto &scenario : player) {
        scenario.raise_depth = 0U;
        // GTO+ tree builder (dichiarazione utente 2026-08-05): stesse size del
        // flop su turn/river; all-in solo quando la bet size supera lo stack
        // rimanente (regola naturale del motore). La soglia 150% era una
        // inferenza non confermata (interpretazione A) ed è stata rimossa:
        // con la regola naturale lo stato (6,7 MB) si avvicina ancora di piu'
        // agli 8 MB dichiarati da GTO+.
        scenario.all_in_mode = gtosd::AllInMode::Disabled;
      }
      player[static_cast<std::size_t>(gtosd::BettingScenario::FacingBet)].raise_depth = 1U;
    }
  }
  return config;
}

gtosd::PostflopRanges make_gto_plus_parity_ranges() {
  gtosd::PostflopRanges ranges;
  const auto zero = gtosd::RangeWeight::from_basis_points(0).value();
  const auto full = gtosd::RangeWeight::from_basis_points(10'000).value();
  for (auto &range : ranges.players) {
    range.fill(zero);
  }
  constexpr std::array<std::string_view, 9> selected_classes{"AA",  "KK",  "QQ",  "AKs", "AQs",
                                                             "KQs", "AKo", "AQo", "KQo"};
  const auto combos = gtosd::all_combos();
  for (std::size_t combo = 0; combo < combos.size(); ++combo) {
    const auto name = gtosd::class_name(gtosd::hand_class(combos[combo]));
    if (std::ranges::find(selected_classes, name) != selected_classes.end()) {
      ranges.players[0][combo] = full;
      ranges.players[1][combo] = full;
    }
  }
  return ranges;
}

const char *build_configuration_name() noexcept {
#ifdef NDEBUG
  return "Release";
#else
  return "Debug";
#endif
}

nlohmann::json compiler_identity() {
#ifdef _MSC_VER
  return {{"id", "MSVC"}, {"version", std::to_string(_MSC_VER)}};
#elif defined(__clang__)
  return {{"id", "Clang"}, {"version", __clang_version__}};
#elif defined(__GNUC__)
  return {{"id", "GCC"}, {"version", __VERSION__}};
#else
  return {{"id", "unknown"}, {"version", "unknown"}};
#endif
}

std::string action_label(const gtosd::Action &action) {
  std::string label = action_type_name(action.type);
  if (action.amount.units() > 0) {
    label += "_" + std::to_string(action.amount.units() / gtosd::Money::units_per_ante);
  }
  return label;
}

bool matches_benchmark_id_pattern(const std::string &id) {
  if (!id.starts_with("GTP-")) {
    return false;
  }
  const auto last_dash = id.rfind('-');
  if (last_dash == std::string::npos || last_dash < 5U) {
    return false;
  }
  const auto suffix = id.substr(last_dash + 1);
  if (suffix.size() != 3U) {
    return false;
  }
  for (const char character : suffix) {
    if (character < '0' || character > '9') {
      return false;
    }
  }
  const auto middle = id.substr(4U, last_dash - 4U);
  if (middle.size() < 2U) {
    return false;
  }
  for (const char character : middle) {
    const bool upper_alnum =
        (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9');
    if (!upper_alnum) {
      return false;
    }
  }
  return true;
}

struct ParsedAllInSpec {
  bool valid{false};
  gtosd::AllInMode mode{gtosd::AllInMode::Disabled};
  std::uint32_t threshold_basis_points{0};
};

ParsedAllInSpec parse_all_in_spec(const std::string_view text) {
  if (text == "disabled") {
    return {true, gtosd::AllInMode::Disabled, 0U};
  }
  constexpr std::string_view add_prefix{"add_if_push_below_"};
  constexpr std::string_view go_prefix{"go_if_push_below_"};
  constexpr std::string_view suffix{"_percent_pot_after_call"};
  const auto mode = text.starts_with(add_prefix)
                        ? gtosd::AllInMode::Add
                        : (text.starts_with(go_prefix) ? gtosd::AllInMode::Go
                                                       : gtosd::AllInMode::Disabled);
  const auto prefix = mode == gtosd::AllInMode::Add ? add_prefix : go_prefix;
  if (mode == gtosd::AllInMode::Disabled || !text.ends_with(suffix)) {
    return {};
  }
  const auto number_text = text.substr(prefix.size(), text.size() - prefix.size() - suffix.size());
  std::uint32_t percent = 0;
  const auto *const begin = number_text.data();
  const auto *const end = begin + number_text.size();
  const auto parsed = std::from_chars(begin, end, percent);
  if (parsed.ec != std::errc{} || parsed.ptr != end || percent == 0U || percent > 1000U) {
    return {};
  }
  return {true, mode, percent * 100U};
}

std::optional<gtosd::PostflopRange> parse_hand_class_range(const std::string_view text) {
  const auto class_id_of = [](const std::string_view name) -> std::optional<std::uint8_t> {
    for (std::uint8_t id = 0; id < 81U; ++id) {
      if (gtosd::class_name(id) == name) {
        return id;
      }
    }
    return std::nullopt;
  };
  const auto category = [](const std::uint8_t id) { return id < 9U ? 0U : (id < 45U ? 1U : 2U); };
  gtosd::PostflopRange range{};
  const auto zero = gtosd::RangeWeight::from_basis_points(0).value();
  range.fill(zero);
  const auto full = gtosd::RangeWeight::from_basis_points(10'000).value();
  const auto combos = gtosd::all_combos();
  const auto apply = [&](const std::uint8_t first_id, const std::uint8_t last_id,
                         const gtosd::RangeWeight weight) {
    for (std::uint8_t id = first_id; id <= last_id; ++id) {
      for (std::size_t combo = 0; combo < combos.size(); ++combo) {
        if (gtosd::hand_class(combos[combo]) == id) {
          range[combo] = weight;
        }
      }
    }
  };
  // GTO+ range exports weight individual hand classes, e.g. "[73.0]77[/73.0]"
  // (73.0 percent, basis points 7300). Plain GTOSD tokens ("AA-QQ,AKs,KQs")
  // mean full weight. A zero-weight class is explicitly kept at zero.
  const auto parse_percent = [](const std::string_view number) -> std::optional<double> {
    // MSVC's floating-point from_chars requires a null-terminated buffer, so
    // copy the view into a std::string first.
    const std::string terminated(number);
    double value = 0.0;
    const auto parsed =
        std::from_chars(terminated.data(), terminated.data() + terminated.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != terminated.data() + terminated.size()) {
      return std::nullopt;
    }
    return value;
  };
  const auto parse_weighted_class = [&](const std::string_view token)
      -> std::optional<std::pair<std::uint8_t, gtosd::RangeWeight>> {
    const auto close = token.find(']');
    if (close == std::string_view::npos || close == 1U) {
      return std::nullopt;
    }
    const auto reopen = token.rfind('[');
    if (reopen == std::string_view::npos || reopen <= close + 1U || !token.ends_with(']')) {
      return std::nullopt;
    }
    const auto leading = parse_percent(token.substr(1U, close - 1U));
    // The trailing bracket is "[/<percent>]" — skip the leading slash.
    auto trailing_text = token.substr(reopen + 1U, token.size() - reopen - 2U);
    if (!trailing_text.empty() && trailing_text.front() == '/') {
      trailing_text.remove_prefix(1U);
    }
    const auto trailing = parse_percent(trailing_text);
    if (!leading || !trailing || *leading != *trailing) {
      return std::nullopt;
    }
    const auto id = class_id_of(token.substr(close + 1U, reopen - close - 1U));
    if (!id) {
      return std::nullopt;
    }
    const auto basis_points = static_cast<std::int64_t>(std::llround(*leading * 100.0));
    if (basis_points < 0 || basis_points > 10'000) {
      return std::nullopt;
    }
    const auto weight = gtosd::RangeWeight::from_basis_points(basis_points);
    if (!weight) {
      return std::nullopt;
    }
    return std::pair{*id, weight.value()};
  };
  std::string_view remaining = text;
  while (!remaining.empty()) {
    const auto comma = remaining.find(',');
    const auto token = remaining.substr(0, comma);
    if (token.empty()) {
      return std::nullopt;
    }
    if (token.front() == '[') {
      const auto weighted = parse_weighted_class(token);
      if (!weighted) {
        return std::nullopt;
      }
      apply(weighted->first, weighted->first, weighted->second);
    } else {
      const auto dash = token.find('-');
      if (dash == std::string_view::npos) {
        const auto id = class_id_of(token);
        if (!id) {
          return std::nullopt;
        }
        apply(*id, *id, full);
      } else {
        const auto first = class_id_of(token.substr(0, dash));
        const auto last = class_id_of(token.substr(dash + 1));
        if (!first || !last || category(*first) != category(*last)) {
          return std::nullopt;
        }
        apply(std::min(*first, *last), std::max(*first, *last), full);
      }
    }
    if (comma == std::string_view::npos) {
      break;
    }
    remaining.remove_prefix(comma + 1);
  }
  return range;
}

std::optional<std::vector<gtosd::PotPercentage>>
parse_size_list(const nlohmann::json &value, const std::int64_t minimum,
                const std::int64_t maximum) {
  std::vector<gtosd::PotPercentage> sizes;
  const auto append = [&](const nlohmann::json &entry) -> bool {
    if (!entry.is_number_integer()) {
      return false;
    }
    const auto percent = entry.get<std::int64_t>();
    if (percent < minimum || percent > maximum) {
      return false;
    }
    const auto size = gtosd::PotPercentage::from_basis_points(
        static_cast<std::uint32_t>(percent * 100));
    if (!size) {
      return false;
    }
    sizes.push_back(size.value());
    return true;
  };

  if (value.is_number_integer()) {
    if (!append(value)) {
      return std::nullopt;
    }
  } else if (value.is_array() && !value.empty()) {
    for (const auto &entry : value) {
      if (!append(entry)) {
        return std::nullopt;
      }
    }
  } else {
    return std::nullopt;
  }
  return sizes;
}

std::optional<std::vector<std::vector<gtosd::PotPercentage>>>
parse_size_schedule(const nlohmann::json &value, const std::size_t expected_depth,
                    const std::int64_t minimum, const std::int64_t maximum) {
  if (!value.is_array() || value.size() != expected_depth) {
    return std::nullopt;
  }
  std::vector<std::vector<gtosd::PotPercentage>> schedule;
  schedule.reserve(value.size());
  for (const auto &entry : value) {
    const auto sizes = parse_size_list(entry, minimum, maximum);
    if (!sizes) {
      return std::nullopt;
    }
    schedule.push_back(*sizes);
  }
  return schedule;
}

struct ParsedRoundingPolicy {
  gtosd::MoneyRoundingMode mode{gtosd::MoneyRoundingMode::Nearest};
  std::vector<gtosd::ActionConfig::RoundingBand> bands;
};

std::optional<ParsedRoundingPolicy> parse_rounding_policy(const nlohmann::json &value) {
  if (!value.is_object() || !value.contains("mode") || !value["mode"].is_string() ||
      !value.contains("bands") || !value["bands"].is_array() || value["bands"].empty()) {
    return std::nullopt;
  }
  ParsedRoundingPolicy result;
  const auto mode = value["mode"].get<std::string>();
  if (mode == "nearest") {
    result.mode = gtosd::MoneyRoundingMode::Nearest;
  } else if (mode == "down") {
    result.mode = gtosd::MoneyRoundingMode::Down;
  } else if (mode == "up") {
    result.mode = gtosd::MoneyRoundingMode::Up;
  } else {
    return std::nullopt;
  }
  for (const auto &band : value["bands"]) {
    if (!band.is_object() || !band.contains("upper_bound_exclusive_units") ||
        !band["upper_bound_exclusive_units"].is_number_integer() ||
        !band.contains("quantum_units") || !band["quantum_units"].is_number_integer()) {
      return std::nullopt;
    }
    const auto upper =
        gtosd::Money::from_units(band["upper_bound_exclusive_units"].get<std::int64_t>());
    const auto quantum = gtosd::Money::from_units(band["quantum_units"].get<std::int64_t>());
    if (!upper || !quantum || quantum.value().units() == 0) {
      return std::nullopt;
    }
    result.bands.push_back({upper.value(), quantum.value()});
  }
  return result;
}

gtosd::PostflopTreeConfig make_convergence_config(const nlohmann::json &fixture,
                                                  const ParsedAllInSpec &all_in) {
  gtosd::PostflopTreeConfig config;
  config.flop = {gtosd::parse_card(fixture["flop"][0].get_ref<const std::string &>()).value(),
                 gtosd::parse_card(fixture["flop"][1].get_ref<const std::string &>()).value(),
                 gtosd::parse_card(fixture["flop"][2].get_ref<const std::string &>()).value()};
  config.initial_pot =
      gtosd::Money::from_antes(fixture["initial_pot_antes"].get<std::int64_t>()).value();
  config.effective_stack =
      gtosd::Money::from_antes(fixture["effective_stack_antes"].get<std::int64_t>()).value();
  config.rake.enabled = true;
  config.rake.percentage = gtosd::RangeWeight::from_basis_points(
                               static_cast<std::uint32_t>(
                                   fixture["rake_percent"].get<std::int64_t>() * 100))
                               .value();
  config.rake.cap = gtosd::Money{};
  const auto bet_sizes = parse_size_list(fixture["bet_size_percent_pot"], 1, 100).value();
  const auto raise_sizes = parse_size_list(fixture["raise_size_percent_pot"], 1, 200).value();
  const auto raise_depth =
      static_cast<std::uint8_t>(fixture["maximum_raises_per_street"].get<std::int64_t>());
  std::vector<std::vector<gtosd::PotPercentage>> raise_size_schedule;
  if (fixture.contains("raise_size_percent_pot_by_raise_count")) {
    raise_size_schedule =
        parse_size_schedule(fixture["raise_size_percent_pot_by_raise_count"], raise_depth, 1, 200)
            .value();
  }
  ParsedRoundingPolicy rounding;
  if (fixture.contains("aggressive_target_rounding")) {
    rounding = parse_rounding_policy(fixture["aggressive_target_rounding"]).value();
  }
  const auto minimum_bet = gtosd::Money::from_antes(1).value();
  const auto threshold =
      gtosd::PotPercentage::from_basis_points(all_in.threshold_basis_points).value();
  for (auto &street : config.streets) {
    for (auto &player : street.players) {
      player[static_cast<std::size_t>(gtosd::BettingScenario::Lead)].aggressive_sizes = bet_sizes;
      player[static_cast<std::size_t>(gtosd::BettingScenario::AfterCheck)].aggressive_sizes =
          bet_sizes;
      player[static_cast<std::size_t>(gtosd::BettingScenario::FacingBet)].aggressive_sizes =
          raise_sizes;
      for (auto &scenario : player) {
        scenario.raise_depth = 0U;
        scenario.all_in_mode = all_in.mode;
        scenario.all_in_threshold = threshold;
        scenario.all_in_strict_boundary =
            fixture["automatic_all_in_strict_boundary"].get<bool>();
        scenario.minimum_bet = minimum_bet;
        scenario.aggressive_target_rounding = rounding.bands;
        scenario.aggressive_target_rounding_mode = rounding.mode;
      }
      player[static_cast<std::size_t>(gtosd::BettingScenario::FacingBet)].raise_depth =
          raise_depth;
      player[static_cast<std::size_t>(gtosd::BettingScenario::FacingBet)]
          .aggressive_sizes_by_raise_count = raise_size_schedule;
    }
  }
  return config;
}

gtosd::PostflopRanges make_convergence_ranges(const gtosd::PostflopRange &co_range,
                                              const gtosd::PostflopRange &btn_range) {
  gtosd::PostflopRanges ranges;
  ranges.players[0] = co_range;
  ranges.players[1] = btn_range;
  return ranges;
}

struct ConvergenceReferenceNode {
  std::string id;
  std::vector<std::string> path;
  std::uint8_t player{0};
  std::optional<double> ev_antes;
  std::map<std::string, double> actions;
};

struct ConvergenceBenchmarkSpec {
  std::string benchmark_id;
  std::string gate_node_id;
  double target_percent{0.0};
  double reference_seconds{0.0};
  std::uint64_t reference_memory_bytes{0};
  std::string target_definition;
  double ev_tolerance{0.0};
  double frequency_tolerance{0.0};
  double display_precision_percent{0.0};
  std::uint64_t certification_interval{0};
  std::uint64_t averaging_delay{0};
  std::uint64_t diagnostic_iteration_limit{0};
  bool diagnostic_fixed_iterations{false};
  std::uint8_t parallel_action_depth{0};
  std::uint8_t maximum_solver_threads{0};
  bool enable_lossless_isomorphism{true};
  bool enable_canonical_public_dag{true};
  gtosd::PostflopStatePrecision state_precision{gtosd::PostflopStatePrecision::Float32};
  gtosd::PostflopAlgorithm algorithm{gtosd::PostflopAlgorithm::CfrPlus};
  double dcfr_positive_regret_exponent{1.5};
  double dcfr_average_exponent{2.0};
  gtosd::PostflopTreeConfig config{};
  gtosd::PostflopRanges ranges{};
  std::vector<ConvergenceReferenceNode> reference_nodes;
  std::string expected_game_fingerprint;
  std::uint64_t expected_physical_public_nodes{0};
  std::uint64_t expected_canonical_public_nodes{0};
  std::uint64_t expected_decision_node_scales{0};
  std::uint64_t expected_information_sets{0};
  std::uint64_t expected_actions{0};
  std::uint64_t expected_solver_state_bytes{0};
};

std::optional<gtosd::NodeId> resolve_reference_node(const gtosd::PublicTree &tree,
                                                    const std::vector<std::string> &path) {
  gtosd::NodeId current = tree.root;
  for (const auto &label : path) {
    const auto &node = tree.nodes[static_cast<std::size_t>(current)];
    if (node.kind != gtosd::PublicNodeKind::Decision) {
      return std::nullopt;
    }
    bool found = false;
    for (const auto &edge : node.edges) {
      if (edge.kind != gtosd::PublicEdgeKind::Action) {
        continue;
      }
      if (action_label(edge.action) == label) {
        current = edge.child;
        found = true;
        break;
      }
    }
    if (!found) {
      return std::nullopt;
    }
  }
  return current;
}

nlohmann::json initial_street_action_catalog(const gtosd::PublicTree &tree) {
  struct PendingNode {
    gtosd::NodeId id{0};
    std::vector<std::string> history;
  };

  nlohmann::json catalog = nlohmann::json::array();
  const auto root_street = tree.nodes[static_cast<std::size_t>(tree.root)].state.street;
  std::vector<PendingNode> pending{{tree.root, {}}};
  while (!pending.empty()) {
    PendingNode current = std::move(pending.back());
    pending.pop_back();
    const auto &node = tree.nodes[static_cast<std::size_t>(current.id)];
    if (node.kind != gtosd::PublicNodeKind::Decision || node.state.street != root_street) {
      continue;
    }

    nlohmann::json actions = nlohmann::json::array();
    std::vector<PendingNode> children;
    for (const auto &edge : node.edges) {
      if (edge.kind != gtosd::PublicEdgeKind::Action) {
        continue;
      }
      const auto label = action_label(edge.action);
      actions.push_back({{"label", label},
                         {"type", action_type_name(edge.action.type)},
                         {"amount_units", edge.action.amount.units()},
                         {"amount_antes",
                          static_cast<double>(edge.action.amount.units()) /
                              static_cast<double>(gtosd::Money::units_per_ante)},
                         {"requested_basis_points", edge.action.requested_basis_points},
                         {"all_in_kind", static_cast<std::uint8_t>(edge.action.all_in_kind)}});
      const auto &child = tree.nodes[static_cast<std::size_t>(edge.child)];
      if (child.kind == gtosd::PublicNodeKind::Decision && child.state.street == root_street) {
        auto child_history = current.history;
        child_history.push_back(label);
        children.push_back({edge.child, std::move(child_history)});
      }
    }
    for (auto child = children.rbegin(); child != children.rend(); ++child) {
      pending.push_back(std::move(*child));
    }

    const auto &state = node.state;
    catalog.push_back(
        {{"history", std::move(current.history)},
         {"player_to_act", state.player_to_act},
         {"pot_units", state.pot.units()},
         {"pot_antes", static_cast<double>(state.pot.units()) /
                           static_cast<double>(gtosd::Money::units_per_ante)},
         {"remaining_stack_units",
          {state.remaining_stacks[0].units(), state.remaining_stacks[1].units()}},
         {"committed_this_street_units",
          {state.committed_this_street[0].units(), state.committed_this_street[1].units()}},
         {"actions", std::move(actions)}});
  }
  return catalog;
}

int run_convergence_benchmark_core(const ConvergenceBenchmarkSpec &spec,
                                   const char *const report_path) {
  const std::filesystem::path destination(report_path);
  if (!destination.parent_path().empty()) {
    std::error_code directory_error;
    std::filesystem::create_directories(destination.parent_path(), directory_error);
    if (directory_error) {
      std::cerr << "postflop benchmark-gto-plus failed: io_failure\n";
      return 1;
    }
  }

  const auto &config = spec.config;
  struct RuntimeSample {
    std::uint64_t current_rss_bytes{0};
    std::uint64_t peak_rss_bytes{0};
    double process_cpu_seconds{0.0};
  };
  std::map<std::uint64_t, RuntimeSample> runtime_samples;
  const bool rbp_read_only_enabled =
      environment_value("GTOSD_RBP_READ_ONLY_TELEMETRY").has_value();
  gtosd::RbpReadOnlyTelemetry rbp_telemetry;
  std::vector<gtosd::RbpReadOnlySnapshot> rbp_snapshots;
  double solver_cpu_started = 0.0;
  gtosd::PostflopSolveOptions options;
  options.iterations = spec.diagnostic_iteration_limit;
  options.averaging_delay = spec.averaging_delay;
  options.certification_interval = spec.certification_interval;
  if (!spec.diagnostic_fixed_iterations) {
    options.target_normalized_max_deviation = spec.target_percent / 100.0;
  }
  options.strict_target = true;
  options.state_precision = spec.state_precision;
  options.algorithm = spec.algorithm;
  options.dcfr_positive_regret_exponent = spec.dcfr_positive_regret_exponent;
  options.dcfr_average_exponent = spec.dcfr_average_exponent;
  options.parallel_action_depth = spec.parallel_action_depth;
  options.enable_lossless_isomorphism = spec.enable_lossless_isomorphism;
  options.enable_canonical_public_dag = spec.enable_canonical_public_dag;
  options.progress_callback = [&config, &runtime_samples,
                               &solver_cpu_started](const gtosd::PostflopCertification &point) {
    runtime_samples[point.iteration] =
        {gtosd::process_current_rss_bytes(), gtosd::process_peak_rss_bytes(),
         std::max(0.0, gtosd::process_cpu_seconds() - solver_cpu_started)};
    const auto deviation = gtosd::normalized_max_deviation_gain(point, config.initial_pot);
    if (deviation) {
      std::cerr << "certification iteration=" << point.iteration
                << " target_dev_percent=" << deviation.value() * 100.0 << '\n';
    }
  };

  const auto started = std::chrono::steady_clock::now();
  const bool requires_physical_analysis_tree =
      std::ranges::any_of(spec.reference_nodes,
                          [](const auto &node) { return !node.path.empty(); });
  std::cerr << "benchmark_phase=prepare_start peak_rss_bytes="
            << gtosd::process_peak_rss_bytes() << '\n';
  const auto prepared = gtosd::prepare_postflop_tree(
      config, spec.ranges, spec.enable_lossless_isomorphism,
      spec.enable_canonical_public_dag, requires_physical_analysis_tree);
  if (!prepared) {
    std::cerr << "postflop benchmark-gto-plus failed: "
              << gtosd::postflop_solver_error_name(prepared.error()) << '\n';
    return 1;
  }
  if (rbp_read_only_enabled) {
    options.checkpoint_callback =
        [&rbp_telemetry, &rbp_snapshots,
         &prepared](const gtosd::PostflopCertification &,
                    const gtosd::PostflopCheckpoint &checkpoint) {
          auto observed = rbp_telemetry.observe(*prepared.value(), checkpoint);
          if (!observed) {
            return false;
          }
          rbp_snapshots.push_back(std::move(observed.value()));
          return true;
        };
  }
  std::cerr << "benchmark_phase=prepare_complete peak_rss_bytes="
            << gtosd::process_peak_rss_bytes() << '\n';
  solver_cpu_started = gtosd::process_cpu_seconds();
  const auto solved = gtosd::solve_postflop_exact(*prepared.value(), options);
  const double wall_elapsed_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  if (!solved || solved.value().convergence.empty()) {
    std::cerr << "postflop benchmark-gto-plus failed: "
              << (solved ? "missing_certification"
                         : gtosd::postflop_solver_error_name(solved.error()))
              << '\n';
    return 1;
  }

  const auto &result = solved.value();
  const double elapsed_seconds = result.timings.run_solver_seconds;
  const auto &final = result.convergence.back();
  const auto final_deviation = gtosd::normalized_max_deviation_gain(final, config.initial_pot);
  if (!final_deviation) {
    std::cerr << "postflop benchmark-gto-plus failed: numerical_failure\n";
    return 1;
  }
  const std::uint64_t solver_state_bytes =
      spec.state_precision == gtosd::PostflopStatePrecision::ScaledUint16RegretStrategy
          ? result.actions * 2U * sizeof(std::uint16_t) +
                result.checkpoint.decision_node_count * 2U * sizeof(float)
      : (spec.state_precision == gtosd::PostflopStatePrecision::Float13RegretFloat11Strategy ||
         spec.state_precision ==
             gtosd::PostflopStatePrecision::ActionMajorFloat13RegretFloat11Strategy)
          ? result.actions * 3U
      : spec.state_precision == gtosd::PostflopStatePrecision::Float24RegretFloat16Strategy
          ? result.actions * 5U
          : result.actions * 2U * sizeof(float);
  const bool layout_matches =
      result.public_tree.node_count == spec.expected_physical_public_nodes &&
      result.canonical_public_nodes == spec.expected_canonical_public_nodes &&
      (spec.expected_decision_node_scales == 0U ||
       result.checkpoint.decision_node_count ==
           spec.expected_decision_node_scales) &&
      result.information_sets == spec.expected_information_sets &&
      result.actions == spec.expected_actions && solver_state_bytes == spec.expected_solver_state_bytes &&
      result.checkpoint.game_fingerprint == spec.expected_game_fingerprint;
  const bool converged =
      final_deviation.value() < spec.target_percent / 100.0 &&
      (result.stop_reason == gtosd::PostflopStopReason::Converged ||
       spec.diagnostic_fixed_iterations);
  const auto public_tree = gtosd::prepared_postflop_public_tree(prepared.value());
  if (requires_physical_analysis_tree && !public_tree) {
    std::cerr << "postflop benchmark-gto-plus failed: missing_analysis_tree\n";
    return 1;
  }

  std::vector<gtosd::PostflopNodeAnalysis> analyses;
  analyses.reserve(spec.reference_nodes.size());
  for (const auto &reference_node : spec.reference_nodes) {
    const auto node = reference_node.path.empty()
                          ? std::optional<gtosd::NodeId>{0U}
                          : resolve_reference_node(*public_tree, reference_node.path);
    if (!node) {
      std::cerr << "postflop benchmark-gto-plus failed: reference_node_not_found id="
                << reference_node.id << '\n';
      return 1;
    }
    const auto analysis = gtosd::analyze_postflop_node(*prepared.value(), result.checkpoint, *node);
    if (!analysis) {
      std::cerr << "postflop benchmark-gto-plus failed: node_ev_analysis_failure id="
                << reference_node.id
                << " error=" << gtosd::postflop_solver_error_name(analysis.error()) << '\n';
      return 1;
    }
    if (analysis.value().player_to_act != reference_node.player) {
      std::cerr << "postflop benchmark-gto-plus failed: reference_node_player_mismatch id="
                << reference_node.id << '\n';
      return 1;
    }
    analyses.push_back(std::move(analysis.value()));
  }

  nlohmann::json action_catalog = nlohmann::json::array();
  if (public_tree) {
    action_catalog = initial_street_action_catalog(*public_tree);
  } else if (!analyses.empty()) {
    nlohmann::json actions = nlohmann::json::array();
    for (const auto &action : analyses.front().actions) {
      actions.push_back({{"label", action_label(action)},
                         {"type", action_type_name(action.type)},
                         {"amount_units", action.amount.units()},
                         {"amount_antes",
                          static_cast<double>(action.amount.units()) /
                              static_cast<double>(gtosd::Money::units_per_ante)},
                         {"requested_basis_points", action.requested_basis_points},
                         {"all_in_kind", static_cast<std::uint8_t>(action.all_in_kind)}});
    }
    action_catalog.push_back(
        {{"history", nlohmann::json::array()},
         {"player_to_act", analyses.front().player_to_act},
         {"pot_units", config.initial_pot.units()},
         {"pot_antes", static_cast<double>(config.initial_pot.units()) /
                            static_cast<double>(gtosd::Money::units_per_ante)},
         {"remaining_stack_units",
          {config.effective_stack.units(), config.effective_stack.units()}},
         {"committed_this_street_units", {0, 0}},
         {"actions", std::move(actions)}});
  }

  const auto ev_check = [ev_tolerance = spec.ev_tolerance](const double measured,
                                                           const double expected_value) {
    const double delta = measured - expected_value;
    return nlohmann::json{{"reference_antes", expected_value},
                          {"measured_antes", measured},
                          {"delta_antes", delta},
                          {"absolute_tolerance_antes", ev_tolerance},
                          {"passed", std::isfinite(measured) && std::abs(delta) <= ev_tolerance}};
  };
  nlohmann::json ev_checks = nlohmann::json::object();
  for (std::size_t index = 0; index < spec.reference_nodes.size(); ++index) {
    const auto &reference_node = spec.reference_nodes[index];
    if (!reference_node.ev_antes) {
      continue;
    }
    ev_checks[reference_node.id] =
        ev_check(analyses[index].gto_plus_ev_antes[reference_node.player],
                 reference_node.ev_antes.value());
  }
  const auto action_frequencies_json = [](const gtosd::PostflopNodeAnalysis &analysis) {
    nlohmann::json frequencies = nlohmann::json::object();
    for (std::size_t action = 0; action < analysis.actions.size(); ++action) {
      frequencies[action_label(analysis.actions[action])] = analysis.action_frequencies[action];
    }
    return frequencies;
  };
  nlohmann::json reference_node_frequencies = nlohmann::json::object();
  for (std::size_t index = 0; index < spec.reference_nodes.size(); ++index) {
    reference_node_frequencies[spec.reference_nodes[index].id] =
        action_frequencies_json(analyses[index]);
  }
  const auto frequency_check = [frequency_tolerance = spec.frequency_tolerance](
                                   const nlohmann::json &measured_node,
                                   const std::string_view action, const double expected_value) {
    const double measured =
        measured_node.value(std::string(action), std::numeric_limits<double>::quiet_NaN());
    const double delta = measured - expected_value;
    return nlohmann::json{
        {"reference_fraction", expected_value},
        {"measured_fraction", measured},
        {"delta_fraction", delta},
        {"absolute_tolerance_fraction", frequency_tolerance},
        {"passed", std::isfinite(measured) && std::abs(delta) <= frequency_tolerance}};
  };
  nlohmann::json frequency_checks = nlohmann::json::object();
  for (std::size_t index = 0; index < spec.reference_nodes.size(); ++index) {
    const auto &reference_node = spec.reference_nodes[index];
    if (reference_node.actions.empty()) {
      continue;
    }
    const auto &measured_node = reference_node_frequencies[reference_node.id];
    nlohmann::json node_checks = nlohmann::json::object();
    for (const auto &[label, expected_fraction] : reference_node.actions) {
      node_checks[label] = frequency_check(measured_node, label, expected_fraction);
    }
    frequency_checks[reference_node.id] = std::move(node_checks);
  }
  const bool action_frequency_correctness_passed =
      std::ranges::all_of(frequency_checks.items(), [](const auto &node) {
        return std::ranges::all_of(node.value().items(), [](const auto &action) {
          return action.value().value("passed", false);
        });
      });
  // Aggregate check: unconditional (frequency-weighted) EV of the opponent
  // after the gate node's actions. The per-node conditional EV checks are
  // knife-edge when the root action frequencies differ (both solutions can be
  // within the dEV target with a different bet/check mix); weighting each side
  // with its own posteriors (measured_frequency x measured_ev vs reference
  // frequency x reference_ev) yields the unconditional EV, which is robust and
  // consistent with the zero-sum root gate. Published as
  // gto_plus_unconditional_ev_checks and used as the EV correctness criterion;
  // falls back to the per-node checks when no child reference nodes exist.
  nlohmann::json unconditional_ev_checks = nlohmann::json::object();
  {
    const auto gate_it = std::find_if(
        spec.reference_nodes.begin(), spec.reference_nodes.end(),
        [&](const ConvergenceReferenceNode &node) { return node.id == spec.gate_node_id; });
    if (gate_it != spec.reference_nodes.end()) {
      const bool has_children = std::any_of(
          spec.reference_nodes.begin(), spec.reference_nodes.end(),
          [&](const ConvergenceReferenceNode &node) {
            return node.path.size() == 1U && node.player != gate_it->player && node.ev_antes;
          });
      if (has_children) {
        const auto &gate_measured =
            reference_node_frequencies.value(spec.gate_node_id, nlohmann::json::object());
        double weighted_measured = 0.0;
        double weighted_reference = 0.0;
        double display_rounding_budget = 0.0;
        nlohmann::json components = nlohmann::json::array();
        // The reference components are rounded to GTO+'s display precision
        // (EV at display_precision_percent of the initial pot, frequencies at
        // 0.001), so the weighted reference sum carries a rounding budget:
        // the aggregate tolerance is the per-node tolerance plus that budget
        // (worst case over all display-rounded products).
        const double display_ev_step =
            (spec.display_precision_percent / 100.0) *
            static_cast<double>(config.initial_pot.units()) /
            static_cast<double>(gtosd::Money::units_per_ante);
        constexpr double kDisplayFrequencyStep = 0.001;
        for (std::size_t index = 0; index < spec.reference_nodes.size(); ++index) {
          const auto &child = spec.reference_nodes[index];
          if (child.path.size() != 1U || !child.ev_antes || child.player == gate_it->player) {
            continue;
          }
          const auto &label = child.path.front();
          const double reference_frequency =
              gate_it->actions.contains(label)
                  ? gate_it->actions.at(label)
                  : std::numeric_limits<double>::quiet_NaN();
          const double measured_frequency =
              gate_measured.value(label, std::numeric_limits<double>::quiet_NaN());
          const double measured_ev = analyses[index].gto_plus_ev_antes[child.player];
          if (!std::isfinite(reference_frequency) || !std::isfinite(measured_frequency) ||
              !std::isfinite(measured_ev)) {
            continue;
          }
          weighted_measured += measured_frequency * measured_ev;
          weighted_reference += reference_frequency * child.ev_antes.value();
          display_rounding_budget +=
              std::abs(reference_frequency) * display_ev_step +
              std::abs(child.ev_antes.value()) * kDisplayFrequencyStep;
          components.push_back(nlohmann::json{{"id", child.id},
                                              {"path", child.path},
                                              {"reference_frequency", reference_frequency},
                                              {"measured_frequency", measured_frequency},
                                              {"reference_ev_antes", child.ev_antes.value()},
                                              {"measured_ev_antes", measured_ev}});
        }
        const double delta = weighted_measured - weighted_reference;
        const double aggregate_tolerance = spec.ev_tolerance + display_rounding_budget;
        unconditional_ev_checks[spec.gate_node_id] = nlohmann::json{
            {"weighted_measured_antes", weighted_measured},
            {"weighted_reference_antes", weighted_reference},
            {"delta_antes", delta},
            {"base_absolute_tolerance_antes", spec.ev_tolerance},
            {"display_rounding_budget_antes", display_rounding_budget},
            {"absolute_tolerance_antes", aggregate_tolerance},
            {"components", std::move(components)},
            {"passed", std::isfinite(delta) && std::abs(delta) <= aggregate_tolerance}};
      }
    }
  }
  // The EV correctness criterion is the unconditional (frequency-weighted)
  // opponent EV after the gate node's actions (see above). The per-node BTN
  // EV and action frequency checks remain published as diagnostics: per the
  // 2026-08-02 analysis they cannot prove a different game unless the root
  // posteriors are identical.
  const bool ev_correctness_passed =
      unconditional_ev_checks.empty()
          ? std::ranges::all_of(ev_checks.items(), [](const auto &entry) {
              return entry.value().value("passed", false);
            })
          : unconditional_ev_checks[spec.gate_node_id].value("passed", false);
  // The correctness gate is the GTO+ EV of the gate reference node (the tree
  // root, resolved by spec.gate_node_id). It stays the structural gate: the
  // aggregate weighted check above is the EV correctness criterion.
  const bool gate_ev_passed =
      ev_checks.contains(spec.gate_node_id) &&
      ev_checks[spec.gate_node_id].value("passed", false);
  const bool correctness_passed = layout_matches && result.maximum_normalization_error <= 1.0e-11 &&
                                  std::isfinite(final.normalized_nash_conv) &&
                                  std::abs(final.expected_payoff_sum_antes) <= 1.0e-11 &&
                                  converged && gate_ev_passed;

  nlohmann::json convergence = nlohmann::json::array();
  double best_deviation_percent = std::numeric_limits<double>::infinity();
  for (const auto &point : result.convergence) {
    const auto deviation = gtosd::normalized_max_deviation_gain(point, config.initial_pot);
    if (!deviation) {
      std::cerr << "postflop benchmark-gto-plus failed: numerical_failure\n";
      return 1;
    }
    const double deviation_percent = deviation.value() * 100.0;
    best_deviation_percent = std::min(best_deviation_percent, deviation_percent);
    const auto runtime = runtime_samples.find(point.iteration);
    const RuntimeSample sample = runtime != runtime_samples.end()
                                     ? runtime->second
                                     : RuntimeSample{};
    const double traversal_iterations_per_second =
        point.traversal_elapsed_seconds > 0.0
            ? static_cast<double>(point.iteration) / point.traversal_elapsed_seconds
            : 0.0;
    const double solver_iterations_per_second =
        point.solver_elapsed_seconds > 0.0
            ? static_cast<double>(point.iteration) / point.solver_elapsed_seconds
            : 0.0;
    const double nodes_per_second =
        point.traversal_elapsed_seconds > 0.0
            ? static_cast<double>(point.work_counters.visited_nodes) /
                  point.traversal_elapsed_seconds
            : 0.0;
    convergence.push_back({{"iteration", point.iteration},
                           {"solver_elapsed_seconds", point.solver_elapsed_seconds},
                           {"traversal_elapsed_seconds", point.traversal_elapsed_seconds},
                           {"certification_elapsed_seconds",
                            point.certification_elapsed_seconds},
                           {"process_cpu_seconds", sample.process_cpu_seconds},
                           {"current_rss_bytes", sample.current_rss_bytes},
                           {"peak_rss_bytes", sample.peak_rss_bytes},
                           {"solver_iterations_per_second", solver_iterations_per_second},
                           {"traversal_iterations_per_second",
                            traversal_iterations_per_second},
                           {"average_traversal_seconds_per_iteration",
                            point.iteration > 0U
                                ? point.traversal_elapsed_seconds /
                                      static_cast<double>(point.iteration)
                                : 0.0},
                           {"visited_nodes_per_second", nodes_per_second},
                           {"gto_plus_dev_fraction", deviation.value()},
                           {"gto_plus_dev_percent", deviation_percent},
                           {"best_gto_plus_dev_percent_so_far", best_deviation_percent},
                           {"normalized_nash_conv", point.normalized_nash_conv},
                           {"profile_value_co_antes", point.profile_value_antes[0]},
                           {"profile_value_btn_antes", point.profile_value_antes[1]},
                           {"best_response_value_co_antes",
                            point.best_response_value_antes[0]},
                           {"best_response_value_btn_antes",
                            point.best_response_value_antes[1]},
                           {"expected_payoff_sum_antes", point.expected_payoff_sum_antes},
                           {"deviation_gain_co_antes",
                            point.best_response_value_antes[0] - point.profile_value_antes[0]},
                           {"deviation_gain_btn_antes",
                            point.best_response_value_antes[1] - point.profile_value_antes[1]},
                           {"work_counters",
                            {{"visited_nodes", point.work_counters.visited_nodes},
                             {"decision_node_evaluations",
                              point.work_counters.decision_node_evaluations},
                             {"chance_node_evaluations",
                              point.work_counters.chance_node_evaluations},
                             {"chance_outcome_evaluations",
                              point.work_counters.chance_outcome_evaluations},
                             {"terminal_evaluations",
                              point.work_counters.terminal_evaluations},
                             {"fold_terminal_evaluations",
                              point.work_counters.fold_terminal_evaluations},
                             {"showdown_terminal_evaluations",
                              point.work_counters.showdown_terminal_evaluations},
                             {"regret_update_entries",
                              point.work_counters.regret_update_entries},
                             {"strategy_update_entries",
                              point.work_counters.strategy_update_entries}}}});
  }

  // GTO+ reports the memory consumed by the complete solve, not only the
  // persistent regret/strategy payload. Keep solver_state_bytes as a separate
  // structural metric, but gate parity on the process peak RSS so tree,
  // traversal scratch and per-worker deltas cannot be hidden.
  const std::uint64_t peak_rss_bytes = gtosd::process_peak_rss_bytes();
  const std::uint64_t current_rss_bytes = gtosd::process_current_rss_bytes();
  const double process_cpu_seconds =
      std::max(0.0, gtosd::process_cpu_seconds() - solver_cpu_started);
  const double normalized_cpu_utilization =
      elapsed_seconds > 0.0 && spec.maximum_solver_threads > 0U
          ? process_cpu_seconds /
                (elapsed_seconds * static_cast<double>(spec.maximum_solver_threads))
          : 0.0;
  const double traversal_iterations_per_second =
      result.timings.traversal_seconds > 0.0
          ? static_cast<double>(result.checkpoint.completed_iterations) /
                result.timings.traversal_seconds
          : 0.0;
  const double visited_nodes_per_second =
      result.timings.traversal_seconds > 0.0
          ? static_cast<double>(result.work_counters.visited_nodes) /
                result.timings.traversal_seconds
          : 0.0;

  const bool diagnostic_simultaneous =
      environment_value("GTOSD_DIAGNOSTIC_SIMULTANEOUS").has_value();
  const nlohmann::json iteration_limit =
      spec.diagnostic_iteration_limit == 0U
          ? nlohmann::json(nullptr)
          : nlohmann::json(spec.diagnostic_iteration_limit);
  const auto rbp_work_json = [](const gtosd::RbpReadOnlyWorkEstimate &work) {
    return nlohmann::json{{"public_nodes", work.public_nodes},
                          {"decision_nodes", work.decision_nodes},
                          {"chance_nodes", work.chance_nodes},
                          {"chance_outcomes", work.chance_outcomes},
                          {"fold_terminals", work.fold_terminals},
                          {"showdown_terminals", work.showdown_terminals},
                          {"regret_entries", work.regret_entries},
                          {"strategy_entries", work.strategy_entries}};
  };
  nlohmann::json rbp_observations = nlohmann::json::array();
  for (const auto &snapshot : rbp_snapshots) {
    nlohmann::json players = nlohmann::json::array();
    for (const auto &player : snapshot.players) {
      players.push_back(
          {{"decisions", player.decisions},
           {"actions", player.actions},
           {"zero_policy_actions", player.zero_policy_actions},
           {"negative_regret_actions", player.negative_regret_actions},
           {"original_formula_candidates", player.original_formula_candidates},
           {"candidate_fraction",
            player.actions == 0U
                ? 0.0
                : static_cast<double>(player.original_formula_candidates) / player.actions},
           {"decisions_with_candidates", player.decisions_with_candidates},
           {"decisions_with_all_but_one_candidate",
            player.decisions_with_all_but_one_candidate},
           {"persistent_candidates", player.persistent_candidates},
           {"new_candidates", player.new_candidates},
           {"reactivated_candidates", player.reactivated_candidates},
           {"candidates_by_street", player.candidates_by_street},
           {"candidates_by_decision_action_count",
            player.candidates_by_decision_action_count},
           {"candidate_regret_antes",
            {{"minimum", player.minimum_candidate_regret_antes},
             {"mean", player.mean_candidate_regret_antes},
             {"maximum", player.maximum_candidate_regret_antes}}},
           {"threshold_multiple",
            {{"minimum", player.minimum_threshold_multiple},
             {"mean", player.mean_threshold_multiple},
             {"maximum", player.maximum_threshold_multiple}}},
           {"negative_regret_threshold_multiple",
            {{"mean", player.mean_negative_regret_threshold_multiple},
             {"maximum", player.maximum_negative_regret_threshold_multiple},
             {"distance_of_closest_to_one",
              1.0 - player.maximum_negative_regret_threshold_multiple}}},
           {"structural_upper_bound", rbp_work_json(player.structural_upper_bound)}});
    }
    rbp_observations.push_back(
        {{"iteration", snapshot.iteration},
         {"players", std::move(players)},
         {"structurally_unreachable_action_entries",
          snapshot.structurally_unreachable_action_entries},
         {"exact_zero_counterfactual_reach_available",
          snapshot.exact_zero_counterfactual_reach_available},
         {"exact_zero_counterfactual_reach_actions",
          snapshot.exact_zero_counterfactual_reach_actions},
         {"metadata_bytes", snapshot.metadata_bytes},
         {"metadata_bytes_per_action", snapshot.metadata_bytes_per_action},
         {"metadata_bytes_per_decision", snapshot.metadata_bytes_per_decision}});
  }
  nlohmann::json report = {
      {"schema", "gtosd.gto_plus_convergence_run.v1"},
      {"benchmark_id", spec.benchmark_id},
      {"game_fingerprint", result.checkpoint.game_fingerprint},
      {"build",
       {{"configuration", build_configuration_name()},
        {"compiler", compiler_identity()},
        {"api_version", std::string(gtosd::api_version_string)}}},
      {"algorithm", spec.algorithm == gtosd::PostflopAlgorithm::HsDcfr30
                        ? "exact_hs_dcfr_30"
                    : spec.algorithm == gtosd::PostflopAlgorithm::Dcfr
                        ? "exact_dcfr"
                        : (spec.algorithm == gtosd::PostflopAlgorithm::DcfrPlus
                               ? "exact_dcfr_plus"
                               : "exact_cfr_plus")},
      {"dcfr_parameters",
       {{"alpha", spec.dcfr_positive_regret_exponent},
        {"beta", 0.0},
        {"gamma", spec.dcfr_average_exponent}}},
      {"update_mode", diagnostic_simultaneous ? "simultaneous" : "alternating"},
      {"precision",
       spec.state_precision == gtosd::PostflopStatePrecision::ScaledUint16RegretStrategy
           ? "action_major_scaled_uint16_regret_strategy_float32_compute"
       : spec.state_precision ==
                 gtosd::PostflopStatePrecision::ActionMajorFloat13RegretFloat11Strategy
           ? "action_major_float13_regret_float11_strategy_float32_compute"
       : spec.state_precision == gtosd::PostflopStatePrecision::Float13RegretFloat11Strategy
           ? "float13_regret_float11_strategy_float32_compute"
       : spec.state_precision == gtosd::PostflopStatePrecision::Float24RegretFloat16Strategy
           ? "float24_regret_float16_strategy_float64_compute"
           : "float32_state_float64_compute"},
      {"exact_outcomes", true},
      {"sampling", false},
      {"bucketing", false},
      {"lossless_isomorphism", spec.enable_lossless_isomorphism},
      {"canonical_public_dag", spec.enable_canonical_public_dag},
      {"parallel_action_depth", spec.parallel_action_depth},
      {"maximum_solver_threads", spec.maximum_solver_threads},
      {"convergence_metric",
       {{"name", "maximum_unilateral_best_response_gain_over_initial_pot"},
        {"gto_plus_name", "Target dEV"},
        {"target_fraction", spec.target_percent / 100.0},
        {"target_percent", spec.target_percent}}},
      {"timer_scope",
       {{"tree_layout", false},
        {"initialization", true},
        {"cfr_plus", true},
        {"averaging", true},
        {"exact_best_response_certifications", true},
        {"process_startup", false}}},
      {"elapsed_seconds", elapsed_seconds},
      {"process_cpu_seconds", process_cpu_seconds},
      {"normalized_cpu_utilization_fraction", normalized_cpu_utilization},
      {"time_gate",
       {{"measured_seconds", elapsed_seconds},
        {"reference_seconds", spec.reference_seconds},
        {"passed", elapsed_seconds <= spec.reference_seconds}}},
      {"wall_elapsed_seconds_including_tree_preparation", wall_elapsed_seconds},
      {"phase_seconds",
       {{"layout", result.timings.layout_seconds},
        {"initialization", result.timings.initialization_seconds},
        {"traversal", result.timings.traversal_seconds},
        {"regret_application", result.timings.regret_application_seconds},
        {"certification", result.timings.certification_seconds},
        {"finalization", result.timings.finalization_seconds},
        {"run_solver", result.timings.run_solver_seconds},
        {"solver_total", result.timings.total_seconds}}},
      {"completed_iterations", result.checkpoint.completed_iterations},
      {"throughput",
       {{"traversal_iterations_per_second", traversal_iterations_per_second},
        {"average_traversal_seconds_per_iteration",
         result.checkpoint.completed_iterations > 0U
             ? result.timings.traversal_seconds /
                   static_cast<double>(result.checkpoint.completed_iterations)
             : 0.0},
        {"visited_nodes_per_second", visited_nodes_per_second}}},
      {"work_counters",
       {{"visited_nodes", result.work_counters.visited_nodes},
        {"decision_node_evaluations",
         result.work_counters.decision_node_evaluations},
        {"chance_node_evaluations", result.work_counters.chance_node_evaluations},
        {"chance_outcome_evaluations",
         result.work_counters.chance_outcome_evaluations},
        {"terminal_evaluations", result.work_counters.terminal_evaluations},
        {"fold_terminal_evaluations",
         result.work_counters.fold_terminal_evaluations},
        {"showdown_terminal_evaluations",
         result.work_counters.showdown_terminal_evaluations},
        {"regret_update_entries", result.work_counters.regret_update_entries},
        {"strategy_update_entries", result.work_counters.strategy_update_entries}}},
      {"iteration_limit", iteration_limit},
      {"fixed_iteration_diagnostic", spec.diagnostic_fixed_iterations},
      {"certification_interval", spec.certification_interval},
      {"averaging_delay", spec.averaging_delay},
      {"stop_reason", postflop_stop_reason_name(result.stop_reason)},
      {"converged", converged},
      {"layout_matches_fixture", layout_matches},
      {"correctness_passed", correctness_passed},
      {"correctness_gate",
       {{"metric", "gto_plus_ev_antes"},
        {"node_id", spec.gate_node_id},
        {"passed", gate_ev_passed}}},
      {"ev_correctness_passed", ev_correctness_passed},
      {"action_frequency_correctness_passed", action_frequency_correctness_passed},
      {"gto_plus_ev_checks", std::move(ev_checks)},
      {"gto_plus_unconditional_ev_checks", std::move(unconditional_ev_checks)},
      {"gto_plus_action_frequency_checks", std::move(frequency_checks)},
      {"reference_node_action_frequencies", reference_node_frequencies},
      {"initial_street_action_catalog", std::move(action_catalog)},
      {"maximum_normalization_error", result.maximum_normalization_error},
      {"physical_public_nodes", result.public_tree.node_count},
      {"canonical_public_nodes", result.canonical_public_nodes},
      {"decision_node_scales", result.checkpoint.decision_node_count},
      {"information_sets", result.information_sets},
      {"actions", result.actions},
      {"solver_state_bytes", solver_state_bytes},
      {"solver_state_gate",
       {{"metric", "solver_state_bytes"},
        {"measured_bytes", solver_state_bytes},
        {"reference_bytes", spec.reference_memory_bytes},
        {"passed", solver_state_bytes <= spec.reference_memory_bytes}}},
      {"memory_gate",
       {{"metric", "peak_rss_bytes"},
        {"measured_bytes", peak_rss_bytes},
        {"reference_bytes", spec.reference_memory_bytes},
        {"passed", peak_rss_bytes <= spec.reference_memory_bytes}}},
      {"transient_regret_delta_bytes",
       result.actions * sizeof(double) * static_cast<std::uint64_t>(spec.parallel_action_depth + 1U)},
      {"peak_rss_bytes", peak_rss_bytes},
      {"current_rss_bytes", current_rss_bytes},
      {"final_gto_plus_dev_fraction", final_deviation.value()},
      {"final_gto_plus_dev_percent", final_deviation.value() * 100.0},
      {"final_normalized_nash_conv", final.normalized_nash_conv},
      {"convergence", std::move(convergence)}};
  report["rbp_read_only_audit"] =
      {{"enabled", rbp_read_only_enabled},
       {"mutates_solver_state", false},
       {"changes_traversal_control_flow", false},
       {"original_cfr_formula_is_sound_for_production_dcfr", false},
       {"observation_scope", "exact certification checkpoints only"},
       {"observations", std::move(rbp_observations)}};

  const auto temporary = destination.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << report.dump(2) << '\n';
    output.flush();
    if (!output) {
      std::cerr << "postflop benchmark-gto-plus failed: io_failure\n";
      return 1;
    }
  }
  std::error_code filesystem_error;
  std::filesystem::remove(destination, filesystem_error);
  filesystem_error.clear();
  std::filesystem::rename(temporary, destination, filesystem_error);
  if (filesystem_error) {
    std::cerr << "postflop benchmark-gto-plus failed: io_failure\n";
    return 1;
  }
  std::cout << "GTOSD_GTO_PLUS_CONVERGENCE_RUN_1\n"
            << "benchmark_id=" << spec.benchmark_id
            << " iteration=" << result.checkpoint.completed_iterations
            << " target_dev_percent=" << spec.target_percent
            << " final_dev_percent=" << final_deviation.value() * 100.0
            << " normalized_nash_conv=" << final.normalized_nash_conv
            << " elapsed_seconds=" << elapsed_seconds
            << " correctness=" << (correctness_passed ? "pass" : "fail") << '\n';
  return correctness_passed ? 0 : 4;
}

int run_gto_plus_convergence_benchmark_v1(const char *const specification_path,
                                          const char *const report_path) {
  constexpr std::string_view benchmark_id{"GTP-AHKHQH-003"};
  nlohmann::json specification;
  try {
    std::ifstream input(specification_path, std::ios::binary);
    input >> specification;
    if (!input || !specification.is_object() ||
        specification.value("schema", std::string{}) != "gtosd.gto_plus_convergence_benchmark.v1" ||
        specification.value("benchmark_id", std::string{}) != benchmark_id ||
        specification.value("source", std::string{}) != "GTO+" ||
        !specification.contains("gto_plus_reference") ||
        !specification["gto_plus_reference"].is_object() || !specification.contains("gtosd_run") ||
        !specification["gto_plus_reference"].contains("flop_ev_antes") ||
        !specification["gto_plus_reference"]["flop_ev_antes"].is_object() ||
        !specification["gto_plus_reference"].contains("flop_action_frequencies") ||
        !specification["gto_plus_reference"]["flop_action_frequencies"].is_object() ||
        !specification["gtosd_run"].is_object() || !specification.contains("expected_layout") ||
        !specification["expected_layout"].is_object()) {
      std::cerr << "postflop benchmark-gto-plus failed: specification_mismatch\n";
      return 2;
    }
  } catch (const nlohmann::json::exception &) {
    std::cerr << "postflop benchmark-gto-plus failed: invalid_specification_json\n";
    return 2;
  }

  const auto &reference = specification["gto_plus_reference"];
  const auto &run = specification["gtosd_run"];
  const auto &expected = specification["expected_layout"];
  const auto &fixture = specification["fixture"];
  const double target_percent = reference.value("target_dev_percent", -1.0);
  const double reference_seconds = reference.value("elapsed_seconds", -1.0);
  const std::uint64_t reference_memory_bytes =
      reference.value("solver_memory_bytes", std::uint64_t{0});
  const auto &reference_ev = reference["flop_ev_antes"];
  const auto &reference_frequencies = reference["flop_action_frequencies"];
  const double ev_tolerance = reference_ev.value("absolute_tolerance_antes", -1.0);
  const std::uint64_t certification_interval =
      run.value("certification_interval", std::uint64_t{0});
  const std::uint64_t averaging_delay = run.value("averaging_delay", std::uint64_t{0});
  const std::uint8_t parallel_action_depth = run.value("parallel_action_depth", std::uint8_t{0});
  const std::uint8_t maximum_solver_threads = run.value("maximum_solver_threads", std::uint8_t{0});
  const bool immutable_fixture_matches =
      fixture.is_object() && fixture.value("variant", std::string{}) == "short_deck_hu_postflop" &&
      fixture.value("deck", std::string{}) == "36_cards_6_to_ace" &&
      fixture.value("initial_pot_antes", 0) == 40 &&
      fixture.value("effective_stack_antes", 0) == 100 &&
      fixture.value("bet_size_percent_pot", 0) == 50 &&
      fixture.value("raise_size_percent_pot", 0) == 50 &&
      fixture.value("maximum_raises_per_street", 0) == 1 &&
      fixture.value("physical_combos_after_blockers_per_player", 0) == 36 &&
      fixture.value("raises", std::string{}) == "half_pot" &&
      fixture.value("automatic_all_in", std::string{}) == "disabled" &&
      fixture.value("automatic_all_in_strict_boundary", false) &&
      fixture.value("final_bet_smoothing", std::string{}) == "disabled" &&
      fixture.value("rake_percent", -1) == 0 && fixture.contains("flop") &&
      fixture["flop"] == nlohmann::json::array({"Ah", "Kh", "Qh"}) &&
      fixture.value("range_co", std::string{}) == "AA-QQ,AKs-AQs,KQs,AKo-AQo,KQo" &&
      fixture.value("range_btn", std::string{}) == "AA-QQ,AKs-AQs,KQs,AKo-AQo,KQo";
  if (!std::isfinite(target_percent) || std::abs(target_percent - 1.0) > 1.0e-12 ||
      std::abs(reference_seconds - 1.71) > 1.0e-12 || reference_memory_bytes != 8'000'000U ||
      !reference_ev.is_object() || std::abs(ev_tolerance - 0.05) > 1.0e-12 ||
      std::abs(reference_ev.value("co_root", -1.0) - 19.15) > 1.0e-12 ||
      std::abs(reference_ev.value("btn_after_co_check", -1.0) - 21.65) > 1.0e-12 ||
      std::abs(reference_ev.value("btn_after_co_bet_20", -1.0) - 17.51) > 1.0e-12 ||
      std::abs(reference_frequencies.value("absolute_tolerance_fraction", -1.0) - 0.01) > 1.0e-12 ||
      std::abs(reference_frequencies.value("display_precision_percent", -1.0) - 0.1) > 1.0e-12 ||
      reference_frequencies.value("flop_co_root", nlohmann::json{}) !=
          nlohmann::json{{"check", 0.803}, {"bet_20", 0.197}} ||
      reference_frequencies.value("flop_btn_after_co_bet_20", nlohmann::json{}) !=
          nlohmann::json{{"fold", 0.374}, {"call_20", 0.626}, {"raise_60", 0.0}} ||
      run.contains("maximum_iterations") || certification_interval != 20U ||
      averaging_delay != 20U ||
      parallel_action_depth != 5U || maximum_solver_threads != 6U ||
      reference.value("target_definition", std::string{}) !=
          "maximum unilateral best-response gain divided by the initial pot" ||
      !immutable_fixture_matches) {
    std::cerr << "postflop benchmark-gto-plus failed: invalid_specification_value\n";
    return 2;
  }

  ConvergenceBenchmarkSpec spec;
  spec.benchmark_id = benchmark_id;
  spec.gate_node_id = "flop_co_root";
  spec.target_percent = target_percent;
  spec.reference_seconds = reference_seconds;
  spec.reference_memory_bytes = reference_memory_bytes;
  spec.target_definition = reference.value("target_definition", std::string{});
  spec.ev_tolerance = ev_tolerance;
  spec.frequency_tolerance = reference_frequencies.value("absolute_tolerance_fraction", -1.0);
  spec.display_precision_percent = reference_frequencies.value("display_precision_percent", -1.0);
  spec.certification_interval = certification_interval;
  spec.averaging_delay = averaging_delay;
  spec.parallel_action_depth = parallel_action_depth;
  spec.maximum_solver_threads = maximum_solver_threads;
  spec.config = make_gto_plus_parity_config();
  spec.ranges = make_gto_plus_parity_ranges();
  spec.reference_nodes = {
      {"flop_co_root", {}, 0, 19.15, {{"check", 0.803}, {"bet_20", 0.197}}},
      {"flop_btn_after_co_check", {"check"}, 1, 21.65, {}},
      {"flop_btn_after_co_bet_20", {"bet_20"}, 1, 17.51,
       {{"fold", 0.374}, {"call_20", 0.626}, {"raise_60", 0.0}}}};
  spec.expected_game_fingerprint = expected.value("game_fingerprint", std::string{});
  spec.expected_physical_public_nodes = expected.value("physical_public_nodes", std::uint64_t{0});
  spec.expected_canonical_public_nodes = expected.value("canonical_public_nodes", std::uint64_t{0});
  spec.expected_decision_node_scales =
      expected.value("decision_node_scales", std::uint64_t{0});
  spec.expected_information_sets = expected.value("information_sets", std::uint64_t{0});
  spec.expected_actions = expected.value("actions", std::uint64_t{0});
  spec.expected_solver_state_bytes = expected.value("solver_state_bytes", std::uint64_t{0});
  return run_convergence_benchmark_core(spec, report_path);
}

int run_gto_plus_convergence_benchmark_v2(const char *const specification_path,
                                          const char *const report_path) {
  nlohmann::json specification;
  try {
    std::ifstream input(specification_path, std::ios::binary);
    input >> specification;
    if (!input || !specification.is_object() ||
        specification.value("schema", std::string{}) !=
            "gtosd.gto_plus_convergence_benchmark.v2" ||
        specification.value("source", std::string{}) != "GTO+" ||
        !specification.contains("benchmark_id") || !specification["benchmark_id"].is_string() ||
        !matches_benchmark_id_pattern(
            specification["benchmark_id"].get_ref<const std::string &>()) ||
        !specification.contains("fixture") || !specification["fixture"].is_object() ||
        !specification.contains("gto_plus_reference") ||
        !specification["gto_plus_reference"].is_object() ||
        !specification.contains("gtosd_run") || !specification["gtosd_run"].is_object() ||
        !specification.contains("expected_layout") ||
        !specification["expected_layout"].is_object()) {
      std::cerr << "postflop benchmark-gto-plus failed: specification_mismatch\n";
      return 2;
    }
  } catch (const nlohmann::json::exception &) {
    std::cerr << "postflop benchmark-gto-plus failed: invalid_specification_json\n";
    return 2;
  }

  const auto &fixture = specification["fixture"];
  const auto &reference = specification["gto_plus_reference"];
  const auto &run = specification["gtosd_run"];
  const auto &expected = specification["expected_layout"];

  ConvergenceBenchmarkSpec spec;
  spec.benchmark_id = specification["benchmark_id"].get<std::string>();
  spec.target_percent = reference.value("target_dev_percent", -1.0);
  spec.reference_seconds = reference.value("elapsed_seconds", -1.0);
  spec.reference_memory_bytes = reference.value("solver_memory_bytes", std::uint64_t{0});
  spec.target_definition = reference.value("target_definition", std::string{});
  spec.ev_tolerance = reference.value("ev_absolute_tolerance_antes", -1.0);
  spec.frequency_tolerance = reference.value("action_frequency_absolute_tolerance_fraction", -1.0);
  spec.display_precision_percent = reference.value("display_precision_percent", -1.0);
  spec.certification_interval = run.value("certification_interval", std::uint64_t{0});
  spec.averaging_delay = run.value("averaging_delay", std::uint64_t{0});
  spec.diagnostic_iteration_limit =
      run.value("diagnostic_iteration_limit", std::uint64_t{0});
  spec.parallel_action_depth = run.value("parallel_action_depth", std::uint8_t{0});
  spec.maximum_solver_threads = run.value("maximum_solver_threads", std::uint8_t{0});
  spec.enable_lossless_isomorphism =
      run.value("enable_lossless_isomorphism", true);
  spec.enable_canonical_public_dag =
      run.value("enable_canonical_public_dag", true);
  const auto diagnostic_iteration_override =
      environment_value("GTOSD_DIAGNOSTIC_ITERATION_LIMIT");
  if (diagnostic_iteration_override) {
    const auto parsed = parse_u64(*diagnostic_iteration_override);
    if (!parsed || *parsed == 0U) {
      std::cerr << "postflop benchmark-gto-plus failed: "
                   "invalid_diagnostic_iteration_limit\n";
      return 2;
    }
    spec.diagnostic_iteration_limit = *parsed;
  }
  const auto diagnostic_certification_override =
      environment_value("GTOSD_DIAGNOSTIC_CERTIFICATION_INTERVAL");
  if (diagnostic_certification_override) {
    const auto parsed = parse_u64(*diagnostic_certification_override);
    if (!parsed || *parsed == 0U) {
      std::cerr << "postflop benchmark-gto-plus failed: "
                   "invalid_diagnostic_certification_interval\n";
      return 2;
    }
    spec.certification_interval = *parsed;
  }
  spec.diagnostic_fixed_iterations =
      environment_value("GTOSD_DIAGNOSTIC_FIXED_ITERATIONS").has_value();
  if (spec.diagnostic_fixed_iterations &&
      spec.diagnostic_iteration_limit == 0U) {
    std::cerr << "postflop benchmark-gto-plus failed: "
                 "fixed_iterations_without_limit\n";
    return 2;
  }
  const auto algorithm = run.value("algorithm", std::string{"cfr_plus"});
  if (algorithm == "dcfr_plus") {
    spec.algorithm = gtosd::PostflopAlgorithm::DcfrPlus;
  } else if (algorithm == "dcfr") {
    spec.algorithm = gtosd::PostflopAlgorithm::Dcfr;
  } else if (algorithm == "hs_dcfr_30") {
    spec.algorithm = gtosd::PostflopAlgorithm::HsDcfr30;
  } else if (algorithm != "cfr_plus") {
    std::cerr << "postflop benchmark-gto-plus failed: invalid_algorithm\n";
    return 2;
  }
  spec.dcfr_positive_regret_exponent =
      run.value("dcfr_positive_regret_exponent", 1.5);
  spec.dcfr_average_exponent = run.value("dcfr_average_exponent", 2.0);
  const auto state_precision = run.value("state_precision", std::string{"float32"});
  if (state_precision == "float24_regret_float16_strategy") {
    spec.state_precision = gtosd::PostflopStatePrecision::Float24RegretFloat16Strategy;
  } else if (state_precision == "float13_regret_float11_strategy") {
    spec.state_precision = gtosd::PostflopStatePrecision::Float13RegretFloat11Strategy;
  } else if (state_precision == "scaled_uint16_regret_strategy") {
    spec.state_precision = gtosd::PostflopStatePrecision::ScaledUint16RegretStrategy;
  } else if (state_precision == "action_major_float13_regret_float11_strategy") {
    spec.state_precision =
        gtosd::PostflopStatePrecision::ActionMajorFloat13RegretFloat11Strategy;
  } else if (state_precision != "float32") {
    std::cerr << "postflop benchmark-gto-plus failed: invalid_state_precision\n";
    return 2;
  }
  spec.expected_game_fingerprint = expected.value("game_fingerprint", std::string{});
  spec.expected_physical_public_nodes = expected.value("physical_public_nodes", std::uint64_t{0});
  spec.expected_canonical_public_nodes = expected.value("canonical_public_nodes", std::uint64_t{0});
  spec.expected_decision_node_scales =
      expected.value("decision_node_scales", std::uint64_t{0});
  spec.expected_information_sets = expected.value("information_sets", std::uint64_t{0});
  spec.expected_actions = expected.value("actions", std::uint64_t{0});
  spec.expected_solver_state_bytes = expected.value("solver_state_bytes", std::uint64_t{0});

  const auto valid_convergence_trace = [&]() {
    if (!reference.contains("convergence_trace")) {
      return !reference.contains("first_strictly_below_target");
    }
    const auto &trace = reference["convergence_trace"];
    if (!trace.is_array() || trace.empty() ||
        !reference.contains("first_strictly_below_target") ||
        !reference["first_strictly_below_target"].is_object() ||
        !fixture.contains("initial_pot_antes") ||
        !fixture["initial_pot_antes"].is_number()) {
      return false;
    }
    const double initial_pot = fixture["initial_pot_antes"].get<double>();
    if (!std::isfinite(initial_pot) || initial_pot <= 0.0) {
      return false;
    }
    double previous_time = -1.0;
    const nlohmann::json *first_below = nullptr;
    for (const auto &point : trace) {
      if (!point.is_object() || !point.contains("elapsed_seconds") ||
          !point["elapsed_seconds"].is_number() || !point.contains("dev_percent") ||
          !point["dev_percent"].is_number() || !point.contains("dev_antes") ||
          !point["dev_antes"].is_number()) {
        return false;
      }
      const double elapsed = point["elapsed_seconds"].get<double>();
      const double displayed_percent = point["dev_percent"].get<double>();
      const double deviation_antes = point["dev_antes"].get<double>();
      const double normalized_percent = 100.0 * deviation_antes / initial_pot;
      if (!std::isfinite(elapsed) || elapsed <= previous_time ||
          !std::isfinite(displayed_percent) || displayed_percent < 0.0 ||
          !std::isfinite(deviation_antes) || deviation_antes < 0.0 ||
          std::abs(displayed_percent - normalized_percent) > 0.051) {
        return false;
      }
      previous_time = elapsed;
      if (first_below == nullptr && normalized_percent < spec.target_percent) {
        first_below = &point;
      }
    }
    if (first_below == nullptr) {
      return false;
    }
    const auto &declared = reference["first_strictly_below_target"];
    return declared.contains("elapsed_seconds") && declared["elapsed_seconds"].is_number() &&
           declared.contains("dev_percent") && declared["dev_percent"].is_number() &&
           declared.contains("dev_antes") && declared["dev_antes"].is_number() &&
           std::abs(declared["elapsed_seconds"].get<double>() -
                    (*first_below)["elapsed_seconds"].get<double>()) < 1.0e-12 &&
           std::abs(declared["dev_percent"].get<double>() -
                    (*first_below)["dev_percent"].get<double>()) < 1.0e-12 &&
           std::abs(declared["dev_antes"].get<double>() -
                    (*first_below)["dev_antes"].get<double>()) < 1.0e-12 &&
           std::abs(spec.reference_seconds -
                    (*first_below)["elapsed_seconds"].get<double>()) < 1.0e-12;
  }();
  if (!valid_convergence_trace) {
    std::cerr << "postflop benchmark-gto-plus failed: invalid_reference_convergence_trace\n";
    return 2;
  }

  if (run.contains("maximum_iterations") || spec.certification_interval == 0 ||
      spec.maximum_solver_threads == 0 ||
      static_cast<std::uint16_t>(spec.parallel_action_depth) + 1U >
          static_cast<std::uint16_t>(spec.maximum_solver_threads) ||
      !std::isfinite(spec.target_percent) ||
      std::abs(spec.target_percent - 1.0) > 1.0e-12 ||
      !std::isfinite(spec.reference_seconds) || spec.reference_seconds <= 0.0 ||
      spec.reference_memory_bytes == 0 || spec.target_definition.empty() ||
      !std::isfinite(spec.ev_tolerance) || spec.ev_tolerance <= 0.0 ||
      !std::isfinite(spec.frequency_tolerance) || spec.frequency_tolerance <= 0.0 ||
      !std::isfinite(spec.display_precision_percent) || spec.display_precision_percent <= 0.0) {
    std::cerr << "postflop benchmark-gto-plus failed: invalid_run_parameters\n";
    return 2;
  }
  if (!std::isfinite(spec.dcfr_positive_regret_exponent) ||
      spec.dcfr_positive_regret_exponent < 0.0 ||
      !std::isfinite(spec.dcfr_average_exponent) || spec.dcfr_average_exponent < 0.0) {
    std::cerr << "postflop benchmark-gto-plus failed: invalid_dcfr_parameters\n";
    return 2;
  }

  const auto valid_card_list = [&fixture] {
    if (!fixture["flop"].is_array() || fixture["flop"].size() != 3U) {
      return false;
    }
    std::array<gtosd::CardId, 3> cards{};
    for (std::size_t index = 0; index < 3U; ++index) {
      const auto &entry = fixture["flop"][index];
      if (!entry.is_string()) {
        return false;
      }
      const auto card = gtosd::parse_card(entry.get_ref<const std::string &>());
      if (!card) {
        return false;
      }
      cards[index] = card.value();
    }
    return cards[0] != cards[1] && cards[0] != cards[2] && cards[1] != cards[2];
  }();
  const auto all_in = parse_all_in_spec(fixture.value("automatic_all_in", std::string{}));
  const bool valid_rounding = !fixture.contains("aggressive_target_rounding") ||
                              parse_rounding_policy(fixture["aggressive_target_rounding"])
                                  .has_value();
  const auto range_co = parse_hand_class_range(fixture.value("range_co", std::string{}));
  const auto range_btn = parse_hand_class_range(fixture.value("range_btn", std::string{}));
  const auto integer_in_range = [&fixture](const char *const key, const std::int64_t minimum,
                                           const std::int64_t maximum) {
    const auto &value = fixture[key];
    return value.is_number_integer() && value.get<std::int64_t>() >= minimum &&
           value.get<std::int64_t>() <= maximum;
  };
  // Bet/raise sizes accept a single integer (legacy) or an array of integers,
  // each within [minimum, maximum] percent of the pot.
  const auto size_list_ok = [&fixture](const char *const key, const std::int64_t minimum,
                                       const std::int64_t maximum) {
    const auto &value = fixture[key];
    if (value.is_number_integer()) {
      const auto size = value.get<std::int64_t>();
      return size >= minimum && size <= maximum;
    }
    if (!value.is_array() || value.empty()) {
      return false;
    }
    return std::ranges::all_of(value, [&](const auto &entry) {
      return entry.is_number_integer() && entry.template get<std::int64_t>() >= minimum &&
             entry.template get<std::int64_t>() <= maximum;
    });
  };
  const auto smoothing = fixture.value("final_bet_smoothing", std::string{});
  const bool valid_raise_depth = integer_in_range("maximum_raises_per_street", 0, 4);
  const bool valid_raise_schedule =
      !fixture.contains("raise_size_percent_pot_by_raise_count") ||
      (valid_raise_depth &&
       parse_size_schedule(
           fixture["raise_size_percent_pot_by_raise_count"],
           static_cast<std::size_t>(fixture["maximum_raises_per_street"].get<std::int64_t>()), 1,
           200)
           .has_value());
  const bool fixture_ok =
      fixture.value("variant", std::string{}) == "short_deck_hu_postflop" &&
      fixture.value("deck", std::string{}) == "36_cards_6_to_ace" &&
      integer_in_range("initial_pot_antes", 2, 1'000'000) &&
      integer_in_range("effective_stack_antes", 1, 1'000'000) &&
      size_list_ok("bet_size_percent_pot", 1, 100) &&
      size_list_ok("raise_size_percent_pot", 1, 200) &&
      valid_raise_depth &&
      integer_in_range("rake_percent", 0, 100) && valid_card_list && range_co.has_value() &&
      range_btn.has_value() && all_in.valid && valid_raise_schedule && valid_rounding &&
      fixture["automatic_all_in_strict_boundary"].is_boolean() &&
      (smoothing == "disabled" || smoothing == "enabled");
  if (!fixture_ok) {
    std::cerr << "postflop benchmark-gto-plus failed: invalid_fixture\n";
    return 2;
  }

  if (!reference["reference_nodes"].is_array() || reference["reference_nodes"].empty()) {
    std::cerr << "postflop benchmark-gto-plus failed: invalid_reference_nodes\n";
    return 2;
  }
  const auto valid_reference_id = [](const std::string &id) {
    return !id.empty() && std::ranges::all_of(id, [](const char character) {
             return (character >= 'a' && character <= 'z') ||
                    (character >= '0' && character <= '9') || character == '_';
           });
  };
  std::set<std::string> reference_ids;
  for (const auto &entry : reference["reference_nodes"]) {
    if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string() ||
        !entry.contains("path") || !entry["path"].is_array() || !entry.contains("player") ||
        !entry["player"].is_number_integer()) {
      std::cerr << "postflop benchmark-gto-plus failed: invalid_reference_nodes\n";
      return 2;
    }
    const auto id = entry["id"].get<std::string>();
    const auto player = entry["player"].get<std::int64_t>();
    if (!valid_reference_id(id) || !reference_ids.insert(id).second ||
        (player != 0 && player != 1)) {
      std::cerr << "postflop benchmark-gto-plus failed: invalid_reference_nodes\n";
      return 2;
    }
    ConvergenceReferenceNode node;
    node.id = id;
    node.player = static_cast<std::uint8_t>(player);
    for (const auto &label : entry["path"]) {
      if (!label.is_string() || label.get_ref<const std::string &>().empty()) {
        std::cerr << "postflop benchmark-gto-plus failed: invalid_reference_nodes\n";
        return 2;
      }
      node.path.push_back(label.get<std::string>());
    }
    const bool has_ev = entry.contains("ev_antes") && entry["ev_antes"].is_number();
    const bool has_actions = entry.contains("actions") && entry["actions"].is_object();
    if (!has_ev && !has_actions) {
      std::cerr << "postflop benchmark-gto-plus failed: invalid_reference_nodes\n";
      return 2;
    }
    if (has_ev) {
      const auto ev = entry["ev_antes"].get<double>();
      if (!std::isfinite(ev)) {
        std::cerr << "postflop benchmark-gto-plus failed: invalid_reference_nodes\n";
        return 2;
      }
      node.ev_antes = ev;
    }
    if (has_actions) {
      for (const auto &[label, fraction] : entry["actions"].items()) {
        if (!fraction.is_number()) {
          std::cerr << "postflop benchmark-gto-plus failed: invalid_reference_nodes\n";
          return 2;
        }
        const auto value = fraction.get<double>();
        if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
          std::cerr << "postflop benchmark-gto-plus failed: invalid_reference_nodes\n";
          return 2;
        }
        node.actions[label] = value;
      }
    }
    spec.reference_nodes.push_back(std::move(node));
  }

  spec.config = make_convergence_config(fixture, all_in);
  spec.ranges = make_convergence_ranges(range_co.value(), range_btn.value());

  // The correctness gate is the EV of one reference node: explicit gate_node,
  // or the reference node with an empty path (the tree root) by default.
  spec.gate_node_id = reference.value("gate_node", std::string{});
  if (spec.gate_node_id.empty()) {
    const auto root_node = std::ranges::find_if(
        spec.reference_nodes, [](const auto &node) { return node.path.empty(); });
    if (root_node == spec.reference_nodes.end() || !root_node->ev_antes) {
      std::cerr << "postflop benchmark-gto-plus failed: invalid_gate_node\n";
      return 2;
    }
    spec.gate_node_id = root_node->id;
  } else {
    const auto gate = std::ranges::find_if(spec.reference_nodes, [&](const auto &node) {
      return node.id == spec.gate_node_id;
    });
    if (gate == spec.reference_nodes.end() || !gate->ev_antes) {
      std::cerr << "postflop benchmark-gto-plus failed: invalid_gate_node\n";
      return 2;
    }
  }
  return run_convergence_benchmark_core(spec, report_path);
}

int run_gto_plus_convergence_benchmark(const char *const specification_path,
                                       const char *const report_path) {
  std::string schema;
  try {
    std::ifstream input(specification_path, std::ios::binary);
    nlohmann::json specification;
    input >> specification;
    if (!input || !specification.is_object()) {
      std::cerr << "postflop benchmark-gto-plus failed: invalid_specification_json\n";
      return 2;
    }
    schema = specification.value("schema", std::string{});
  } catch (const nlohmann::json::exception &) {
    std::cerr << "postflop benchmark-gto-plus failed: invalid_specification_json\n";
    return 2;
  }
  if (schema == "gtosd.gto_plus_convergence_benchmark.v1") {
    return run_gto_plus_convergence_benchmark_v1(specification_path, report_path);
  }
  if (schema == "gtosd.gto_plus_convergence_benchmark.v2") {
    return run_gto_plus_convergence_benchmark_v2(specification_path, report_path);
  }
  std::cerr << "postflop benchmark-gto-plus failed: specification_mismatch\n";
  return 2;
}

int run_gto_plus_layout_preflight(const char *const specification_path,
                                  const char *const report_path) {
  nlohmann::json specification;
  try {
    std::ifstream input(specification_path, std::ios::binary);
    input >> specification;
    if (!input || !specification.is_object() ||
        specification.value("schema", std::string{}) != "gtosd.gto_plus_convergence_benchmark.v2" ||
        !specification.contains("fixture") || !specification["fixture"].is_object()) {
      std::cerr << "postflop layout-gto-plus failed: specification_mismatch\n";
      return 2;
    }
  } catch (const nlohmann::json::exception &) {
    std::cerr << "postflop layout-gto-plus failed: invalid_specification_json\n";
    return 2;
  }

  const auto &fixture = specification["fixture"];
  const auto all_in = parse_all_in_spec(fixture.value("automatic_all_in", std::string{}));
  const auto range_co = parse_hand_class_range(fixture.value("range_co", std::string{}));
  const auto range_btn = parse_hand_class_range(fixture.value("range_btn", std::string{}));
  if (!all_in.valid || !range_co || !range_btn || !fixture.contains("flop") ||
      !fixture["flop"].is_array() || fixture["flop"].size() != 3U ||
      !fixture.contains("initial_pot_antes") || !fixture.contains("effective_stack_antes") ||
      !fixture.contains("rake_percent") || !fixture.contains("bet_size_percent_pot") ||
      !fixture.contains("raise_size_percent_pot") ||
      !fixture.contains("maximum_raises_per_street") ||
      !fixture.contains("automatic_all_in_strict_boundary")) {
    std::cerr << "postflop layout-gto-plus failed: invalid_fixture\n";
    return 2;
  }

  gtosd::PostflopTreeConfig config;
  try {
    config = make_convergence_config(fixture, all_in);
  } catch (const std::exception &) {
    std::cerr << "postflop layout-gto-plus failed: invalid_fixture\n";
    return 2;
  }
  const auto ranges = make_convergence_ranges(*range_co, *range_btn);
  const auto started = std::chrono::steady_clock::now();
  const auto layout = gtosd::estimate_canonical_chance_layout(config, ranges);
  const double elapsed_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  if (!layout) {
    std::cerr << "postflop layout-gto-plus failed: "
              << gtosd::canonical_layout_error_name(layout.error()) << '\n';
    return 1;
  }

  nlohmann::json streets = nlohmann::json::array();
  constexpr std::array<const char *, 3> street_names{"flop", "turn", "river"};
  for (std::size_t street = 0; street < layout.value().streets.size(); ++street) {
    const auto &value = layout.value().streets[street];
    streets.push_back({{"street", street_names[street]},
                       {"physical_board_paths", value.physical_board_paths},
                       {"canonical_board_paths", value.canonical_board_paths},
                       {"canonical_public_nodes", value.canonical_public_nodes},
                       {"canonical_decision_nodes", value.canonical_decision_nodes},
                       {"canonical_chance_nodes", value.canonical_chance_nodes},
                       {"canonical_terminal_nodes", value.canonical_terminal_nodes},
                       {"canonical_action_edges", value.canonical_action_edges},
                       {"canonical_chance_edges", value.canonical_chance_edges},
                       {"information_sets", value.information_sets},
                       {"action_entries", value.action_entries}});
  }
  nlohmann::json memory = nlohmann::json::array();
  for (const auto &value : layout.value().memory_estimates) {
    memory.push_back({{"state_bytes_per_action", value.state_bytes_per_action},
                      {"worker_threads", value.worker_threads},
                      {"solver_state_bytes", value.solver_state_bytes},
                      {"state_auxiliary_bytes", value.state_auxiliary_bytes},
                      {"topology_bytes", value.topology_bytes},
                      {"board_mapping_bytes", value.board_mapping_bytes},
                      {"worker_scratch_bytes", value.worker_scratch_bytes},
                      {"certification_bytes", value.certification_bytes},
                      {"runtime_bytes", value.runtime_bytes},
                      {"reserve_bytes", value.reserve_bytes},
                      {"estimated_peak_bytes", value.estimated_peak_bytes},
                      {"meets_engineering_target", value.meets_engineering_target},
                      {"meets_absolute_gate", value.meets_absolute_gate}});
  }
  const auto expected_physical_nodes =
      specification.value("expected_layout", nlohmann::json::object())
          .value("physical_public_nodes", std::uint64_t{0});
  const auto &physical = layout.value().physical_public_tree;
  const auto physical_action_edges =
      std::accumulate(physical.action_edges_by_street.begin(),
                      physical.action_edges_by_street.end(), std::uint64_t{0});
  const nlohmann::json output{
      {"schema", "gtosd.canonical_chance_layout.v1"},
      {"benchmark_id", specification.value("benchmark_id", std::string{})},
      {"architecture", "canonical_chance_tree_player_local"},
      {"exact_outcomes", true},
      {"uses_bucketing", false},
      {"materializes_physical_tree", false},
      {"preserving_suit_automorphisms", layout.value().preserving_suit_automorphisms},
      {"physical_public_nodes", physical.node_count},
      {"physical_public_tree",
       {{"decision_nodes", physical.decision_nodes},
        {"chance_nodes", physical.chance_nodes},
        {"terminal_nodes", physical.terminal_fold_nodes + physical.terminal_showdown_nodes},
        {"terminal_fold_nodes", physical.terminal_fold_nodes},
        {"terminal_showdown_nodes", physical.terminal_showdown_nodes},
        {"edges", physical.edge_count},
        {"action_edges", physical_action_edges},
        {"chance_edges", physical.chance_edges},
        {"estimated_eager_bytes", physical.estimated_eager_bytes}}},
      {"physical_layout_matches_fixture",
       expected_physical_nodes == 0U ||
           expected_physical_nodes == layout.value().physical_public_tree.node_count},
      {"canonical_public_nodes", layout.value().canonical_public_nodes},
      {"canonical_edges", layout.value().canonical_edges},
      {"canonical_decision_nodes", layout.value().canonical_decision_nodes},
      {"canonical_chance_nodes", layout.value().canonical_chance_nodes},
      {"canonical_terminal_nodes", layout.value().canonical_terminal_nodes},
      {"canonical_action_edges", layout.value().canonical_action_edges},
      {"canonical_chance_edges", layout.value().canonical_chance_edges},
      {"information_sets", layout.value().information_sets},
      {"action_entries", layout.value().action_entries},
      {"maximum_live_combos", layout.value().maximum_live_combos},
      {"maximum_actions", layout.value().maximum_actions},
      {"maximum_depth", layout.value().physical_public_tree.maximum_depth},
      {"streets", std::move(streets)},
      {"memory_model",
       {{"name", "canonical_layout_budget_v1"},
        {"note", "Engineering preflight, not measured process RSS; includes explicit runtime, "
                 "certification and reserve budgets."},
        {"profiles", std::move(memory)}}},
      {"preflight_elapsed_seconds", elapsed_seconds},
      {"preflight_peak_rss_bytes", gtosd::process_peak_rss_bytes()}};

  std::ofstream report(report_path, std::ios::binary | std::ios::trunc);
  report << output.dump(2) << '\n';
  if (!report) {
    std::cerr << "postflop layout-gto-plus failed: report_io_failure\n";
    return 1;
  }
  std::cout << "GTOSD_CANONICAL_LAYOUT_1"
            << " benchmark_id=" << specification.value("benchmark_id", std::string{})
            << " physical_nodes=" << layout.value().physical_public_tree.node_count
            << " canonical_nodes=" << layout.value().canonical_public_nodes
            << " infosets=" << layout.value().information_sets
            << " actions=" << layout.value().action_entries
            << " elapsed_seconds=" << elapsed_seconds << '\n';
  return 0;
}

int run_root_lock_diagnostic(const char *const config_path, const char *const lock_path,
                             const char *const iterations_text, const char *const report_path) {
  std::string config_error;
  const auto config = load_postflop_config(config_path, config_error);
  if (!config) {
    std::cerr << "postflop root-lock-diagnostic failed: " << config_error << '\n';
    return 1;
  }
  const auto iterations = parse_u64(iterations_text);
  if (!iterations || *iterations == 0U) {
    std::cerr << "postflop root-lock-diagnostic failed: invalid_iterations\n";
    return 2;
  }

  nlohmann::json lock_json;
  try {
    std::ifstream input(lock_path, std::ios::binary);
    input >> lock_json;
    if (!input || !lock_json.is_object() ||
        lock_json.value("path", std::string{}) != "diagnostic_external_root_lock" ||
        !lock_json.contains("entries") || !lock_json["entries"].is_array() ||
        lock_json["entries"].empty() || !lock_json.contains("range_co") ||
        !lock_json["range_co"].is_string() || !lock_json.contains("range_btn") ||
        !lock_json["range_btn"].is_string()) {
      std::cerr << "postflop root-lock-diagnostic failed: invalid_lock_file\n";
      return 2;
    }
  } catch (const nlohmann::json::exception &) {
    std::cerr << "postflop root-lock-diagnostic failed: invalid_lock_file\n";
    return 2;
  }

  const auto range_co = parse_hand_class_range(lock_json["range_co"].get_ref<const std::string &>());
  const auto range_btn =
      parse_hand_class_range(lock_json["range_btn"].get_ref<const std::string &>());
  if (!range_co || !range_btn) {
    std::cerr << "postflop root-lock-diagnostic failed: invalid_lock_ranges\n";
    return 2;
  }
  const auto ranges = make_convergence_ranges(*range_co, *range_btn);

  gtosd::DiagnosticRootLock lock;
  lock.source_description = lock_json.value("source_description", std::string{});
  lock.source_dev_percent = lock_json.value("source_dev_percent", -1.0);
  if (!std::isfinite(lock.source_dev_percent) || lock.source_dev_percent < 0.0 ||
      lock.source_dev_percent > 100.0) {
    std::cerr << "postflop root-lock-diagnostic failed: invalid_source_dev\n";
    return 2;
  }
  for (const auto &entry_json : lock_json["entries"]) {
    if (!entry_json.is_object() || !entry_json.contains("combo") ||
        !entry_json["combo"].is_string()) {
      std::cerr << "postflop root-lock-diagnostic failed: invalid_lock_entry\n";
      return 2;
    }
    const auto combo_text = entry_json["combo"].get_ref<const std::string &>();
    if (combo_text.size() != 4U) {
      std::cerr << "postflop root-lock-diagnostic failed: invalid_lock_entry\n";
      return 2;
    }
    const auto first = gtosd::parse_card(combo_text.substr(0, 2));
    const auto second = gtosd::parse_card(combo_text.substr(2, 2));
    if (!first || !second || first.value() == second.value()) {
      std::cerr << "postflop root-lock-diagnostic failed: invalid_lock_entry\n";
      return 2;
    }
    gtosd::DiagnosticRootLockEntry entry;
    entry.combo = {std::min(first.value(), second.value()),
                   std::max(first.value(), second.value())};
    for (const auto &[label, probability] : entry_json.items()) {
      if (label == "combo") {
        continue;
      }
      if (!probability.is_number()) {
        std::cerr << "postflop root-lock-diagnostic failed: invalid_lock_entry\n";
        return 2;
      }
      entry.action_labels.push_back(label);
      entry.probabilities.push_back(probability.get<double>());
    }
    if (entry.action_labels.empty()) {
      std::cerr << "postflop root-lock-diagnostic failed: invalid_lock_entry\n";
      return 2;
    }
    lock.entries.push_back(std::move(entry));
  }

  gtosd::PostflopSolveOptions options;
  options.iterations = *iterations;
  options.averaging_delay = std::min<std::uint64_t>(100U, *iterations / 10U);
  options.certification_interval = 20U;
  options.state_precision = gtosd::PostflopStatePrecision::Float32;
  options.parallel_action_depth = 5U;
  options.diagnostic_root_lock = &lock;

  const auto started = std::chrono::steady_clock::now();
  const auto solved = gtosd::solve_postflop_exact(*config, ranges, options);
  const double wall_elapsed_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  if (!solved || solved.value().convergence.empty()) {
    std::cerr << "postflop root-lock-diagnostic failed: "
              << (solved ? "missing_certification"
                         : gtosd::postflop_solver_error_name(solved.error()))
              << '\n';
    return 1;
  }
  const auto &result = solved.value();
  const auto &final = result.convergence.back();
  const auto final_deviation = gtosd::normalized_max_deviation_gain(final, config->initial_pot);
  if (!final_deviation) {
    std::cerr << "postflop root-lock-diagnostic failed: numerical_failure\n";
    return 1;
  }
  // Original-game exploitability: standard best-response certification where
  // the locked root is part of the profile (its deviation is included).
  // Distinguishes the constrained game's convergence from the original
  // game's exploitability (F10.4).
  const auto original_certification =
      gtosd::certify_postflop_checkpoint(*config, ranges, result.checkpoint);
  double original_game_exploitability_percent = std::numeric_limits<double>::quiet_NaN();
  if (original_certification) {
    const auto gain =
        gtosd::normalized_max_deviation_gain(original_certification.value(), config->initial_pot);
    if (gain) {
      original_game_exploitability_percent = gain.value() * 100.0;
    }
  }

  const auto prepared = gtosd::prepare_postflop_tree(*config, ranges, true, true, true);
  if (!prepared) {
    std::cerr << "postflop root-lock-diagnostic failed: "
              << gtosd::postflop_solver_error_name(prepared.error()) << '\n';
    return 1;
  }
  const auto public_tree = gtosd::prepared_postflop_public_tree(prepared.value());
  if (!public_tree) {
    std::cerr << "postflop root-lock-diagnostic failed: missing_analysis_tree\n";
    return 1;
  }

  struct ReferenceNode {
    const char *id;
    std::vector<std::string> path;
    std::uint8_t player;
  };
  const std::vector<ReferenceNode> reference_nodes = {
      {"flop_co_root", {}, 0},
      {"flop_btn_after_co_check", {"check"}, 1},
      {"flop_btn_after_co_bet_20", {"bet_20"}, 1},
      {"flop_co_after_check_btn_bet_20", {"check", "bet_20"}, 0}};

  const auto &reference_ev = lock_json["gto_plus_reference_ev_antes"];
  const auto root_node = resolve_reference_node(*public_tree, {});
  if (!root_node) {
    std::cerr << "postflop root-lock-diagnostic failed: reference_node_not_found id=flop_co_root\n";
    return 1;
  }
  const auto root_analysis = gtosd::analyze_postflop_node(*prepared.value(), result.checkpoint, *root_node);
  if (!root_analysis) {
    std::cerr << "postflop root-lock-diagnostic failed: node_ev_analysis_failure\n";
    return 1;
  }
  nlohmann::json reference_json = nlohmann::json::array();
  for (const auto &node : reference_nodes) {
    const auto resolved = resolve_reference_node(*public_tree, node.path);
    if (!resolved) {
      std::cerr << "postflop root-lock-diagnostic failed: reference_node_not_found id=" << node.id
                << '\n';
      return 1;
    }
    const auto analysis = node.path.empty()
                              ? root_analysis
                              : gtosd::analyze_postflop_node(*prepared.value(), result.checkpoint,
                                                             *resolved);
    if (!analysis) {
      std::cerr << "postflop root-lock-diagnostic failed: node_ev_analysis_failure\n";
      return 1;
    }
    const auto &value = analysis.value();
    const double gto_plus_reference =
        reference_ev.contains(node.id) ? reference_ev[node.id].get<double>()
                                       : std::numeric_limits<double>::quiet_NaN();
    const double measured = value.gto_plus_ev_antes[node.player];
    nlohmann::json frequencies = nlohmann::json::object();
    for (std::size_t action = 0; action < value.actions.size(); ++action) {
      frequencies[action_label(value.actions[action])] = value.action_frequencies[action];
    }
    reference_json.push_back({{"id", node.id},
                              {"path", node.path},
                              {"player", node.player},
                              {"measured_ev_antes", measured},
                              {"gto_plus_reference_ev_antes", gto_plus_reference},
                              {"delta_ev_antes", measured - gto_plus_reference},
                              {"within_0_05_ante_tolerance",
                               std::isfinite(gto_plus_reference) &&
                                   std::abs(measured - gto_plus_reference) <= 0.05},
                              {"action_frequencies", std::move(frequencies)}});
  }

  // F10.4 validation point 1: exact combo coverage and locked-probability
  // reproduction at the tree root under the constrained solve.
  const auto combos = gtosd::all_combos();
  const auto &root_actions = root_analysis.value().actions;
  nlohmann::json locked_verification = nlohmann::json::array();
  double max_absolute_probability_delta = 0.0;
  for (const auto &entry : lock.entries) {
    const auto combo_it = std::ranges::find(combos, entry.combo);
    if (combo_it == combos.end()) {
      std::cerr << "postflop root-lock-diagnostic failed: invalid_lock_entry\n";
      return 1;
    }
    const auto combo_id = static_cast<gtosd::ComboId>(std::distance(combos.begin(), combo_it));
    const auto combo_analysis = std::ranges::find_if(
        root_analysis.value().combos,
        [combo_id](const auto &combo) { return combo.combo == combo_id; });
    if (combo_analysis == root_analysis.value().combos.end()) {
      std::cerr << "postflop root-lock-diagnostic failed: strategy_query_failure\n";
      return 1;
    }
    nlohmann::json measured_probabilities = nlohmann::json::object();
    for (std::size_t action = 0; action < root_actions.size(); ++action) {
      const auto label = action_label(root_actions[action]);
      measured_probabilities[label] = combo_analysis->action_probabilities[action];
      const auto it = std::ranges::find(entry.action_labels, label);
      if (it != entry.action_labels.end()) {
        const auto delta = std::abs(combo_analysis->action_probabilities[action] -
                                    entry.probabilities[static_cast<std::size_t>(
                                        it - entry.action_labels.begin())]);
        max_absolute_probability_delta = std::max(max_absolute_probability_delta, delta);
      }
    }
    locked_verification.push_back({{"combo", gtosd::format_card(entry.combo.first) +
                                                 gtosd::format_card(entry.combo.second)},
                                   {"measured_probabilities", std::move(measured_probabilities)}});
  }
  const bool locked_probabilities_reproduced = max_absolute_probability_delta <= 1.0e-6;

  const auto destination = std::filesystem::path(report_path);
  if (!destination.parent_path().empty()) {
    std::error_code directory_error;
    std::filesystem::create_directories(destination.parent_path(), directory_error);
    if (directory_error) {
      std::cerr << "postflop root-lock-diagnostic failed: io_failure\n";
      return 1;
    }
  }
  nlohmann::json report = {
      {"schema", "gtosd.diagnostic_external_root_lock.v1"},
      {"path", "diagnostic_external_root_lock"},
      {"source_description", lock.source_description},
      {"source_dev_percent", lock.source_dev_percent},
      {"game_fingerprint", result.checkpoint.game_fingerprint},
      {"requested_iterations", *iterations},
      {"completed_iterations", result.checkpoint.completed_iterations},
      {"stop_reason", postflop_stop_reason_name(result.stop_reason)},
      {"converged", result.stop_reason == gtosd::PostflopStopReason::Converged},
      {"constrained_game_final_dev_percent", final_deviation.value() * 100.0},
      {"constrained_game_final_normalized_nash_conv", final.normalized_nash_conv},
      {"constrained_profile_value_antes", final.profile_value_antes},
      {"constrained_best_response_value_antes", final.best_response_value_antes},
      {"constrained_nash_conv_antes", final.nash_conv_antes},
      {"constrained_convergence_trajectory",
       [&result, &config] {
         nlohmann::json trajectory = nlohmann::json::array();
         for (const auto &point : result.convergence) {
           const auto deviation =
               gtosd::normalized_max_deviation_gain(point, config->initial_pot);
           trajectory.push_back({{"iteration", point.iteration},
                                 {"profile_value_antes", point.profile_value_antes},
                                 {"best_response_value_antes", point.best_response_value_antes},
                                 {"nash_conv_antes", point.nash_conv_antes},
                                 {"normalized_nash_conv", point.normalized_nash_conv},
                                 {"deviation_percent", deviation ? deviation.value() * 100.0
                                                                 : std::numeric_limits<double>::quiet_NaN()}});
         }
         return trajectory;
       }()},
      {"original_game_exploitability_percent", original_game_exploitability_percent},
      {"locked_root_entries", lock.entries.size()},
      {"locked_probabilities_reproduced", locked_probabilities_reproduced},
      {"max_absolute_probability_delta", max_absolute_probability_delta},
      {"locked_root_verification", std::move(locked_verification)},
      {"reference_nodes", std::move(reference_json)},
      {"wall_elapsed_seconds_including_tree_preparation", wall_elapsed_seconds}};
  const auto temporary = destination.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << report.dump(2) << '\n';
    output.flush();
    if (!output) {
      std::cerr << "postflop root-lock-diagnostic failed: io_failure\n";
      return 1;
    }
  }
  std::error_code filesystem_error;
  std::filesystem::remove(destination, filesystem_error);
  filesystem_error.clear();
  std::filesystem::rename(temporary, destination, filesystem_error);
  if (filesystem_error) {
    std::cerr << "postflop root-lock-diagnostic failed: io_failure\n";
    return 1;
  }
  std::cout << "GTOSD_ROOT_LOCK_DIAGNOSTIC_1\n"
            << "fingerprint=" << result.checkpoint.game_fingerprint
            << " iterations=" << result.checkpoint.completed_iterations
            << " constrained_dev_percent=" << final_deviation.value() * 100.0 << '\n';
  return 0;
}

std::optional<gtosd::StorageKey> parse_storage_key(const std::string_view text,
                                                   const char *const operation) {
  const auto key = gtosd::storage_key_from_hex(text);
  if (!key) {
    std::cerr << operation << " failed: " << gtosd::storage_error_name(key.error()) << '\n';
    return std::nullopt;
  }
  return key.value();
}

void print_storage_metrics(const gtosd::StorageMetrics &metrics) {
  std::cout << "file_bytes=" << metrics.file_size << " raw_bytes=" << metrics.raw_size
            << " compressed_bytes=" << metrics.compressed_size
            << " encrypted_bytes=" << metrics.encrypted_size
            << " compression_ratio=" << metrics.compression_ratio
            << " peak_open_bytes=" << metrics.peak_open_bytes << " chunks=" << metrics.chunk_count
            << '\n';
}

int run_storage_pack(const char *const config_path, const char *const checkpoint_path,
                     const char *const solution_path, const std::string_view key_text) {
  const auto key = parse_storage_key(key_text, "storage pack");
  std::string config_error;
  const auto config = load_postflop_config(config_path, config_error);
  const auto checkpoint = gtosd::load_postflop_checkpoint(checkpoint_path);
  if (!key || !config || !checkpoint) {
    std::cerr << "storage pack failed: invalid_config_key_or_checkpoint\n";
    return 1;
  }
  const auto certification = gtosd::certify_postflop_checkpoint(*config, checkpoint.value());
  if (!certification) {
    std::cerr << "storage pack failed: " << gtosd::postflop_solver_error_name(certification.error())
              << '\n';
    return 1;
  }
  const auto archive =
      gtosd::make_postflop_solution(*config, checkpoint.value(), certification.value());
  if (!archive) {
    std::cerr << "storage pack failed: " << gtosd::storage_error_name(archive.error()) << '\n';
    return 1;
  }
  auto solution = archive.value();
  const auto dictionary = gtosd::train_solution_dictionary(solution, 32U * 1024U);
  if (!dictionary) {
    std::cerr << "storage pack failed: " << gtosd::storage_error_name(dictionary.error()) << '\n';
    return 1;
  }
  solution.chunks.push_back({gtosd::SolutionChunkType::Dictionary, dictionary.value()});
  const auto saved = gtosd::save_solution(solution_path, solution, *key);
  if (!saved) {
    std::cerr << "storage pack failed: " << gtosd::storage_error_name(saved.error()) << '\n';
    return 1;
  }
  const auto verified = gtosd::verify_solution(solution_path, *key);
  if (!verified) {
    std::cerr << "storage pack failed: " << gtosd::storage_error_name(verified.error()) << '\n';
    return 1;
  }
  std::cout << "GTOSD_SOLUTION_1\n"
            << "status=packed path=" << solution_path
            << " game_fingerprint=" << checkpoint.value().game_fingerprint
            << " iteration=" << certification.value().iteration
            << " normalized_nash_conv=" << certification.value().normalized_nash_conv << '\n';
  print_storage_metrics(verified.value().metrics);
  return 0;
}

int run_storage_verify(const char *const solution_path, const std::string_view key_text) {
  const auto key = parse_storage_key(key_text, "storage verify");
  if (!key) {
    return 2;
  }
  const auto verified = gtosd::verify_solution(solution_path, *key);
  if (!verified) {
    std::cerr << "storage verify failed: " << gtosd::storage_error_name(verified.error()) << '\n';
    return 1;
  }
  std::cout << "GTOSD_STORAGE_VERIFICATION_1\n"
            << "index_authenticated=" << std::boolalpha << verified.value().index_authenticated
            << " all_chunks_authenticated=" << verified.value().all_chunks_authenticated
            << " all_chunks_decompressed=" << verified.value().all_chunks_decompressed << '\n';
  print_storage_metrics(verified.value().metrics);
  return 0;
}

int run_storage_query(const char *const solution_path, const std::string_view key_text,
                      const std::string_view node_text, const std::string_view combo_text) {
  const auto key = parse_storage_key(key_text, "storage query");
  const auto node = parse_u64(node_text);
  const auto combo = parse_u64(combo_text);
  if (!key || !node || !combo || *combo >= 630U) {
    std::cerr << "storage query failed: invalid_argument\n";
    return 2;
  }
  const auto reader = gtosd::open_solution(solution_path, *key);
  if (!reader) {
    std::cerr << "storage query failed: " << gtosd::storage_error_name(reader.error()) << '\n';
    return 1;
  }
  const auto restored = gtosd::restore_postflop_solution(reader.value());
  if (!restored) {
    std::cerr << "storage query failed: " << gtosd::storage_error_name(restored.error()) << '\n';
    return 1;
  }
  const auto query = gtosd::query_postflop_strategy(
      restored.value().config, restored.value().ranges, restored.value().checkpoint, *node,
      static_cast<gtosd::ComboId>(*combo));
  if (!query) {
    std::cerr << "storage query failed: " << gtosd::postflop_solver_error_name(query.error())
              << '\n';
    return 1;
  }
  std::cout << "GTOSD_STORAGE_QUERY_1\n"
            << "public_node=" << *node << " combo=" << *combo << '\n';
  for (std::size_t action = 0; action < query.value().actions.size(); ++action) {
    std::cout << "action=" << action_type_name(query.value().actions[action].type)
              << " amount_units=" << query.value().actions[action].amount.units()
              << " probability=" << query.value().probabilities[action] << '\n';
  }
  return 0;
}

int run_storage_migrate(const char *const source, const char *const destination,
                        const std::string_view key_text) {
  const auto key = parse_storage_key(key_text, "storage migrate");
  if (!key) {
    return 2;
  }
  const auto migrated = gtosd::migrate_solution(source, destination, *key);
  if (!migrated) {
    std::cerr << "storage migrate failed: " << gtosd::storage_error_name(migrated.error()) << '\n';
    return 1;
  }
  std::cout << "GTOSD_STORAGE_MIGRATION_1\n"
            << "status=migrated source=" << source << " destination=" << destination << '\n';
  return 0;
}

int run_storage_catalog_add(const char *const database_path, const char *const solution_path,
                            const std::string_view key_text) {
  const auto key = parse_storage_key(key_text, "storage catalog-add");
  if (!key) {
    return 2;
  }
  const auto reader = gtosd::open_solution(solution_path, *key);
  if (!reader) {
    std::cerr << "storage catalog-add failed: " << gtosd::storage_error_name(reader.error())
              << '\n';
    return 1;
  }
  const auto restored = gtosd::restore_postflop_solution(reader.value());
  if (!restored) {
    std::cerr << "storage catalog-add failed: " << gtosd::storage_error_name(restored.error())
              << '\n';
    return 1;
  }
  gtosd::CatalogEntry entry;
  entry.path = std::filesystem::absolute(solution_path).string();
  entry.game_fingerprint = restored.value().checkpoint.game_fingerprint;
  entry.file_size = reader.value().metrics().file_size;
  entry.modified_unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
  entry.normalized_nash_conv = restored.value().certification.normalized_nash_conv;
  const auto added = gtosd::upsert_solution_catalog(database_path, entry);
  if (!added) {
    std::cerr << "storage catalog-add failed: " << gtosd::storage_error_name(added.error()) << '\n';
    return 1;
  }
  std::cout << "GTOSD_STORAGE_CATALOG_1\n"
            << "status=upserted database=" << database_path << " path=" << entry.path << '\n';
  return 0;
}

int run_storage_catalog_list(const char *const database_path) {
  const auto entries = gtosd::list_solution_catalog(database_path);
  if (!entries) {
    std::cerr << "storage catalog-list failed: " << gtosd::storage_error_name(entries.error())
              << '\n';
    return 1;
  }
  std::cout << "GTOSD_STORAGE_CATALOG_1\n"
            << "entries=" << entries.value().size() << '\n';
  for (const auto &entry : entries.value()) {
    std::cout << "path=" << entry.path << " game_fingerprint=" << entry.game_fingerprint
              << " file_bytes=" << entry.file_size << " modified_unix_ms=" << entry.modified_unix_ms
              << " normalized_nash_conv=" << entry.normalized_nash_conv << '\n';
  }
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
            << "  gto_cli postflop benchmark-gto-plus <specification.json> <report.json>\n"
            << "  gto_cli postflop layout-gto-plus <specification.json> <report.json>\n"
            << "  gto_cli postflop benchmark-config <pf-f1|pf-f2|pf-f3> <output.json>\n"
            << "  gto_cli postflop root-lock-diagnostic <config.json> <lock.json> "
               "<iterations> <report.json>\n"
            << "  gto_cli storage keygen\n"
            << "  gto_cli storage pack <config.json> <checkpoint> <solution.gtsd> <key_hex>\n"
            << "  gto_cli storage verify <solution.gtsd> <key_hex>\n"
            << "  gto_cli storage query <solution.gtsd> <key_hex> <node> <combo_id>\n"
            << "  gto_cli storage migrate <source.gtsd> <destination.gtsd> <key_hex>\n"
            << "  gto_cli storage catalog-add <catalog.gtsddb> <solution.gtsd> <key_hex>\n"
            << "  gto_cli storage catalog-list <catalog.gtsddb>\n"
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
      std::string_view(argv[2]) == "benchmark-gto-plus") {
    return run_gto_plus_convergence_benchmark(argv[3], argv[4]);
  }
  if (argc == 5 && std::string_view(argv[1]) == "postflop" &&
      std::string_view(argv[2]) == "layout-gto-plus") {
    return run_gto_plus_layout_preflight(argv[3], argv[4]);
  }
  if (argc == 5 && std::string_view(argv[1]) == "postflop" &&
      std::string_view(argv[2]) == "benchmark-config") {
    return write_postflop_benchmark_config(argv[3], argv[4]);
  }
  if (argc == 7 && std::string_view(argv[1]) == "postflop" &&
      std::string_view(argv[2]) == "root-lock-diagnostic") {
    return run_root_lock_diagnostic(argv[3], argv[4], argv[5], argv[6]);
  }
  if (argc == 3 && std::string_view(argv[1]) == "storage" &&
      std::string_view(argv[2]) == "keygen") {
    std::cout << gtosd::storage_key_to_hex(gtosd::generate_storage_key()) << '\n';
    return 0;
  }
  if (argc == 7 && std::string_view(argv[1]) == "storage" && std::string_view(argv[2]) == "pack") {
    return run_storage_pack(argv[3], argv[4], argv[5], argv[6]);
  }
  if (argc == 5 && std::string_view(argv[1]) == "storage" &&
      std::string_view(argv[2]) == "verify") {
    return run_storage_verify(argv[3], argv[4]);
  }
  if (argc == 7 && std::string_view(argv[1]) == "storage" && std::string_view(argv[2]) == "query") {
    return run_storage_query(argv[3], argv[4], argv[5], argv[6]);
  }
  if (argc == 6 && std::string_view(argv[1]) == "storage" &&
      std::string_view(argv[2]) == "migrate") {
    return run_storage_migrate(argv[3], argv[4], argv[5]);
  }
  if (argc == 6 && std::string_view(argv[1]) == "storage" &&
      std::string_view(argv[2]) == "catalog-add") {
    return run_storage_catalog_add(argv[3], argv[4], argv[5]);
  }
  if (argc == 4 && std::string_view(argv[1]) == "storage" &&
      std::string_view(argv[2]) == "catalog-list") {
    return run_storage_catalog_list(argv[3]);
  }
  print_usage();
  return argc == 1 ? 0 : 2;
}

int main(const int argc, const char *const argv[]) {
#ifdef _WIN32
  SetUnhandledExceptionFilter(report_unhandled_exception);
#endif
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
