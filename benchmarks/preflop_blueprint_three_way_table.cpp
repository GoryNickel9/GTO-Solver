// Builds and validates the exact three-player class table of the 3-way
// checkdown step 1 (phase 2a): entry (H, A, B) = the hero's result against A
// crossed with its result against B, for a representative combo of H, summed
// over the disjoint opponent pairs and the 142,506 runouts
// (card_abstraction/three_way_table.hpp).
//
// Build: --hero-classes all|NAME,... (a subset for timing probes and smokes),
// --threads, --board-symmetry on|off (one board per suit orbit of the hero,
// identical output), --compare-plain on also builds the subset without the
// symmetry and requires identical bytes. --input PATH validates a saved table
// instead of building (a subset table only with --allow-partial on); --output
// PATH saves and reloads it. A subset build is never written under the name of
// the complete table, preflop_three_way_v1.bin: the program stops before
// building.
//
// Validation (the phase 2a gate): the integer identities V1-V6 (V6 against
// preflop_all_in_v1.bin from --resources-dir, with the largest equity shift
// between folded cards dead and ignored as a diagnostic; without that file the
// program stops before building, unless --skip-v6 on leaves V6 out, which the
// report and stderr say); --invariance
// NAME,... rebuilds every other combo of the classes as hero and requires the
// representative's entries (V7); --brute-force N compares N class triples with
// every pair and runout through gtosd::evaluate_showdown (V8). --dump-path
// writes --dump-triples N random combo triples (exact per-winner-set counts
// from count_combo_triple) and --dump-class-triples K table entries with their
// combo pairs, for tools/three_way_webapp_check.py (V9, the user's
// equity-calculator-web-app DLL).
//
// The report is JSON on stdout, then PREFLOP_BLUEPRINT_THREE_WAY_TABLE=PASS or
// FAIL. For a subset build it extrapolates the full build time from the task
// seconds per class type (9 pairs, 36 suited, 36 offsuit classes).
#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/card_abstraction/three_way_table.hpp"
#include "gtosd/core/ranges.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <ctime>
#endif

namespace {

namespace ca = gtosd::card_abstraction;
using Clock = std::chrono::steady_clock;
using Json = nlohmann::ordered_json;

constexpr std::uint8_t class_count = static_cast<std::uint8_t>(ca::three_way_class_count);

double seconds_since(const Clock::time_point started) {
  return std::chrono::duration<double>(Clock::now() - started).count();
}

// CPU seconds of the whole process (all threads): the build cost without the
// time its threads waited for a core on a shared machine.
double process_cpu_seconds() {
#ifdef _WIN32
  FILETIME creation{};
  FILETIME exit{};
  FILETIME kernel{};
  FILETIME user{};
  if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) == 0) {
    return 0.0;
  }
  const auto ticks = [](const FILETIME &time) {
    return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32U) | time.dwLowDateTime;
  };
  return static_cast<double>(ticks(kernel) + ticks(user)) * 1e-7;
#else
  return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
#endif
}

std::uint64_t parse_unsigned(const std::string_view value) {
  std::size_t consumed = 0U;
  const auto parsed = std::stoull(std::string{value}, &consumed, 10);
  if (consumed != value.size()) {
    throw std::runtime_error("invalid number: " + std::string{value});
  }
  return parsed;
}

bool parse_switch(const std::string_view name, const std::string_view value) {
  if (value == "on") {
    return true;
  }
  if (value == "off") {
    return false;
  }
  throw std::runtime_error(std::string{name} + " takes on or off");
}

std::uint8_t parse_class(const std::string_view text) {
  for (std::uint8_t hand_class = 0U; hand_class < class_count; ++hand_class) {
    if (gtosd::class_name(hand_class) == text) {
      return hand_class;
    }
  }
  const auto value = parse_unsigned(text);
  if (value >= class_count) {
    throw std::runtime_error("unknown hand class " + std::string{text});
  }
  return static_cast<std::uint8_t>(value);
}

std::vector<std::uint8_t> parse_classes(const std::string_view text) {
  std::vector<std::uint8_t> classes;
  if (text == "all") {
    for (std::uint8_t hand_class = 0U; hand_class < class_count; ++hand_class) {
      classes.push_back(hand_class);
    }
    return classes;
  }
  std::size_t begin = 0U;
  while (begin <= text.size()) {
    const auto end = std::min(text.find(',', begin), text.size());
    classes.push_back(parse_class(text.substr(begin, end - begin)));
    begin = end + 1U;
  }
  std::sort(classes.begin(), classes.end());
  classes.erase(std::unique(classes.begin(), classes.end()), classes.end());
  return classes;
}

const char *class_type(const std::uint8_t hand_class) {
  return hand_class < 9U ? "pair" : (hand_class < 45U ? "suited" : "offsuit");
}

Json card_json(const std::uint8_t card) { return Json::array({card / 4U, card % 4U}); }

Json combo_json(const std::uint16_t combo) {
  const auto &cards = ca::combo_table().cards[combo];
  return Json::array({card_json(cards[0]), card_json(cards[1])});
}

Json entry_json(const ca::ThreeWayEntry &entry) {
  return Json{{"pairs", entry.pairs}, {"cells", entry.cells}};
}

struct Options {
  std::filesystem::path resources_dir;
  std::vector<std::uint8_t> hero_classes = parse_classes("all");
  unsigned threads = std::min(8U, std::max(1U, std::thread::hardware_concurrency()));
  bool board_symmetry = true;
  bool compare_plain = false;
  std::filesystem::path input;
  bool allow_partial = false;
  std::filesystem::path output;
  bool skip_v6 = false;
  std::vector<std::uint8_t> invariance_classes;
  std::uint64_t brute_force = 0U;
  std::uint64_t dump_triples = 0U;
  std::uint64_t dump_class_triples = 0U;
  std::filesystem::path dump_path;
  std::uint64_t seed = 0x3357'4159'5441'424CULL;
};

Options parse_options(const int argc, char **argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view name = argv[index];
    if (index + 1 >= argc) {
      throw std::runtime_error("missing value for " + std::string{name});
    }
    const std::string_view value = argv[++index];
    if (name == "--resources-dir") {
      options.resources_dir = value;
    } else if (name == "--hero-classes") {
      options.hero_classes = parse_classes(value);
    } else if (name == "--threads") {
      options.threads = static_cast<unsigned>(parse_unsigned(value));
    } else if (name == "--board-symmetry") {
      options.board_symmetry = parse_switch(name, value);
    } else if (name == "--compare-plain") {
      options.compare_plain = parse_switch(name, value);
    } else if (name == "--input") {
      options.input = value;
    } else if (name == "--allow-partial") {
      options.allow_partial = parse_switch(name, value);
    } else if (name == "--output") {
      options.output = value;
    } else if (name == "--skip-v6") {
      options.skip_v6 = parse_switch(name, value);
    } else if (name == "--invariance") {
      options.invariance_classes = parse_classes(value);
    } else if (name == "--brute-force") {
      options.brute_force = parse_unsigned(value);
    } else if (name == "--dump-triples") {
      options.dump_triples = parse_unsigned(value);
    } else if (name == "--dump-class-triples") {
      options.dump_class_triples = parse_unsigned(value);
    } else if (name == "--dump-path") {
      options.dump_path = value;
    } else if (name == "--seed") {
      options.seed = parse_unsigned(value);
    } else {
      throw std::runtime_error("unknown argument " + std::string{name});
    }
  }
  if ((options.dump_triples != 0U || options.dump_class_triples != 0U) &&
      options.dump_path.empty()) {
    throw std::runtime_error("--dump-triples and --dump-class-triples need --dump-path");
  }
  // Checked before a build of hours, not by the save at its end.
  if (options.input.empty() && options.hero_classes.size() != class_count &&
      !options.output.empty() && ca::ThreeWayTable::canonical_file_name(options.output)) {
    throw std::runtime_error("a subset build (--hero-classes) is not saved as " +
                             std::string{ca::three_way_table_file_name} +
                             ", the name of the complete table: choose another --output");
  }
  return options;
}

// Class triples for V8: textures first (shared ranks and ties, same-suit
// flushes over full houses, the A-6-7-8-9 straight, trips against straights),
// then random ones; only triples whose hero class is built.
std::vector<std::array<std::uint8_t, 3>> brute_force_triples(const ca::ThreeWayTable &table,
                                                             const std::uint64_t count,
                                                             ca::DeterministicRandom &random) {
  constexpr std::array<std::array<std::string_view, 3>, 8> textures{{{"AA", "AA", "KK"},
                                                                     {"AKs", "AKs", "AKs"},
                                                                     {"AKs", "QJs", "T9s"},
                                                                     {"A6s", "98s", "77"},
                                                                     {"TT", "J9o", "87s"},
                                                                     {"KQo", "KQo", "KQs"},
                                                                     {"AKo", "A6s", "66"},
                                                                     {"99", "T8s", "76o"}}};
  std::vector<std::array<std::uint8_t, 3>> triples;
  for (const auto &names : textures) {
    const std::array<std::uint8_t, 3> triple{parse_class(names[0]), parse_class(names[1]),
                                             parse_class(names[2])};
    if (triples.size() < count && table.hero_built(triple[0])) {
      triples.push_back(triple);
    }
  }
  std::vector<std::uint8_t> heroes;
  for (std::uint8_t hand_class = 0U; hand_class < class_count; ++hand_class) {
    if (table.hero_built(hand_class)) {
      heroes.push_back(hand_class);
    }
  }
  while (triples.size() < count && !heroes.empty()) {
    const auto hero = heroes[random.uniform_below(static_cast<std::uint32_t>(heroes.size()))];
    const auto first = static_cast<std::uint8_t>(random.uniform_below(class_count));
    const auto second = static_cast<std::uint8_t>(random.uniform_below(class_count));
    if (table.entry(hero, first, second).pairs != 0U) {
      triples.push_back({hero, first, second});
    }
  }
  return triples;
}

// Random disjoint combo triple.
std::array<std::uint16_t, 3> random_combo_triple(ca::DeterministicRandom &random) {
  const auto &masks = ca::combo_table().masks;
  while (true) {
    std::array<std::uint16_t, 3> combos{};
    for (auto &combo : combos) {
      combo = static_cast<std::uint16_t>(random.uniform_below(ca::combo_count));
    }
    if ((masks[combos[0]] & masks[combos[1]]) == 0U &&
        (masks[combos[0]] & masks[combos[2]]) == 0U &&
        (masks[combos[1]] & masks[combos[2]]) == 0U) {
      return combos;
    }
  }
}

Json dump_for_webapp(const ca::RankTable &ranks, const ca::ThreeWayTable &table,
                     const Options &options, ca::DeterministicRandom &random) {
  Json triples = Json::array();
  for (std::uint64_t index = 0U; index < options.dump_triples; ++index) {
    const auto combos = random_combo_triple(random);
    const auto counts = ca::count_combo_triple(ranks, combos[0], combos[1], combos[2]);
    if (!counts) {
      throw std::runtime_error("combo triple count failed");
    }
    triples.push_back(Json{{"combos", Json::array({combo_json(combos[0]), combo_json(combos[1]),
                                                   combo_json(combos[2])})},
                           {"by_winners", counts.value().by_winners}});
  }
  // Class triples with at most 36 pairs keep the DLL pass short.
  Json class_triples = Json::array();
  const auto &combos = ca::combo_table();
  const auto candidates = brute_force_triples(table, 64U, random);
  for (const auto &triple : candidates) {
    if (class_triples.size() >= options.dump_class_triples) {
      break;
    }
    const auto &entry = table.entry(triple[0], triple[1], triple[2]);
    if (entry.pairs == 0U || entry.pairs > 36U) {
      continue;
    }
    const auto hero = table.representatives()[triple[0]];
    Json pairs = Json::array();
    for (std::uint16_t a = 0U; a < ca::combo_count; ++a) {
      if (combos.hand_class[a] != triple[1] || (combos.masks[a] & combos.masks[hero]) != 0U) {
        continue;
      }
      for (std::uint16_t b = 0U; b < ca::combo_count; ++b) {
        if (combos.hand_class[b] == triple[2] &&
            (combos.masks[b] & (combos.masks[hero] | combos.masks[a])) == 0U) {
          pairs.push_back(Json::array({combo_json(a), combo_json(b)}));
        }
      }
    }
    class_triples.push_back(
        Json{{"classes", Json::array({gtosd::class_name(triple[0]), gtosd::class_name(triple[1]),
                                      gtosd::class_name(triple[2])})},
             {"hero", combo_json(hero)},
             {"pairs", pairs},
             {"entry", entry_json(entry)}});
  }
  return Json{{"schema", "gtosd.preflop_three_way_webapp_dump.v1"},
              {"card_encoding", "[rank 0=six..8=ace, suit 0..3]"},
              {"runouts", ca::three_way_runout_count},
              {"table_fingerprint", table.fingerprint()},
              {"combo_triples", triples},
              {"class_triples", class_triples}};
}

} // namespace

int main(const int argc, char **argv) {
  try {
    const auto options = parse_options(argc, argv);
    const auto started = Clock::now();
    Json report{{"schema", "gtosd.preflop_blueprint_three_way_table_report.v1"},
                {"threads", options.threads},
                {"board_symmetry", options.board_symmetry}};
    bool ok = true;

    std::optional<ca::RankTable> ranks;
    std::optional<ca::AllInTable> heads_up;
    std::string heads_up_missing = "no --resources-dir";
    if (!options.resources_dir.empty()) {
      auto loaded = ca::RankTable::load(options.resources_dir / "rank_table_v1.bin");
      if (loaded) {
        ranks.emplace(std::move(loaded.value()));
      }
      if (!options.skip_v6) {
        auto loaded_heads_up =
            ca::AllInTable::load(options.resources_dir / "preflop_all_in_v1.bin");
        if (loaded_heads_up) {
          heads_up.emplace(std::move(loaded_heads_up.value()));
        } else {
          heads_up_missing = ca::resource_error_name(loaded_heads_up.error());
        }
      }
    }
    // V6 is part of the gate: without its table the program stops here,
    // before the build, unless --skip-v6 on says to leave it out.
    if (!heads_up && !options.skip_v6) {
      throw std::runtime_error("V6 needs the heads-up table preflop_all_in_v1.bin in "
                               "--resources-dir (" +
                               heads_up_missing + "); --skip-v6 on leaves V6 out");
    }
    if (options.skip_v6) {
      std::cerr << "warning: V6 (heads-up consistency) skipped by --skip-v6 on\n";
    }
    report["rank_table"] = ranks ? "loaded" : "built";
    if (!ranks) {
      auto built = ca::RankTable::build();
      if (!built) {
        throw std::runtime_error("rank table build failed");
      }
      ranks.emplace(std::move(built.value()));
    }
    report["heads_up_table"] =
        heads_up ? heads_up->fingerprint() : std::string{"skipped (--skip-v6 on)"};

    // Build or load.
    std::optional<ca::ThreeWayTable> table;
    if (!options.input.empty()) {
      auto phase = Clock::now();
      auto loaded = ca::ThreeWayTable::load(options.input, options.allow_partial
                                                               ? ca::ThreeWayLoad::AllowPartial
                                                               : ca::ThreeWayLoad::CompleteOnly);
      if (!loaded) {
        throw std::runtime_error(
            std::string{"cannot load "} + options.input.string() + ": " +
            ca::resource_error_name(loaded.error()) +
            (!options.allow_partial && loaded.error() == ca::ResourceError::InvalidInput
                 ? " (an incomplete table, a subset build, needs --allow-partial on)"
                 : ""));
      }
      table.emplace(std::move(loaded.value()));
      report["load_seconds"] = seconds_since(phase);
    } else {
      ca::ThreeWayBuildOptions build{options.threads, options.board_symmetry, options.hero_classes};
      ca::ThreeWayBuildTiming timing;
      const auto cpu_before = process_cpu_seconds();
      auto built = ca::ThreeWayTable::build(ranks.value(), build, &timing);
      const auto cpu_seconds = process_cpu_seconds() - cpu_before;
      if (!built) {
        throw std::runtime_error(std::string{"build failed: "} +
                                 ca::resource_error_name(built.error()));
      }
      table.emplace(std::move(built.value()));
      Json heroes = Json::array();
      std::array<double, 3> type_seconds{};
      std::array<std::uint32_t, 3> type_count{};
      double task_seconds = 0.0;
      for (const auto hand_class : options.hero_classes) {
        const auto type = hand_class < 9U ? 0U : (hand_class < 45U ? 1U : 2U);
        type_seconds[type] += timing.hero_task_seconds[hand_class];
        ++type_count[type];
        task_seconds += timing.hero_task_seconds[hand_class];
        heroes.push_back(Json{{"class", gtosd::class_name(hand_class)},
                              {"type", class_type(hand_class)},
                              {"task_seconds", timing.hero_task_seconds[hand_class]},
                              {"boards", timing.hero_boards[hand_class]}});
      }
      Json build_report{{"hero_classes", options.hero_classes.size()},
                        {"wall_seconds", timing.wall_seconds},
                        {"task_seconds", task_seconds},
                        {"process_cpu_seconds", cpu_seconds},
                        {"effective_threads",
                         timing.wall_seconds > 0.0 ? task_seconds / timing.wall_seconds : 0.0},
                        {"heroes", heroes}};
      if (type_count[0] != 0U && type_count[1] != 0U && type_count[2] != 0U) {
        const auto full_task_seconds = 9.0 * type_seconds[0] / type_count[0] +
                                       36.0 * type_seconds[1] / type_count[1] +
                                       36.0 * type_seconds[2] / type_count[2];
        build_report["full_build_task_seconds_estimate"] = full_task_seconds;
        build_report["full_build_cpu_seconds_estimate"] =
            cpu_seconds * full_task_seconds / task_seconds;
        build_report["full_build_wall_seconds_estimate_at_effective_threads"] =
            full_task_seconds / (task_seconds / timing.wall_seconds);
      }
      report["build"] = build_report;

      if (options.compare_plain && options.board_symmetry) {
        build.board_symmetry = false;
        ca::ThreeWayBuildTiming plain_timing;
        const auto plain = ca::ThreeWayTable::build(ranks.value(), build, &plain_timing);
        const bool identical = plain && plain.value() == table.value();
        report["compare_plain"] = Json{{"identical", identical},
                                       {"wall_seconds", plain_timing.wall_seconds},
                                       {"fingerprint", plain ? plain.value().fingerprint() : ""}};
        ok = ok && identical;
      }
    }
    report["fingerprint"] = table->fingerprint();
    report["complete"] = table->complete();
    report["payload_bytes"] = ca::ThreeWayTable::payload_bytes();

    if (!options.output.empty()) {
      auto phase = Clock::now();
      if (!options.output.parent_path().empty()) {
        std::filesystem::create_directories(options.output.parent_path());
      }
      const auto saved = table->save(options.output);
      bool verified = false;
      if (saved) {
        const auto reloaded = ca::ThreeWayTable::load(
            options.output, table->complete() ? ca::ThreeWayLoad::CompleteOnly
                                              : ca::ThreeWayLoad::AllowPartial);
        verified = reloaded && reloaded.value() == table.value();
      }
      report["output"] = Json{
          {"path", options.output.generic_string()},
          {"saved", saved ? "yes" : ca::resource_error_name(saved.error())},
          {"file_bytes", saved ? std::filesystem::file_size(options.output) : std::uintmax_t{0}},
          {"reload_verified", verified},
          {"seconds", seconds_since(phase)}};
      ok = ok && verified;
    }

    // V1-V6.
    auto phase = Clock::now();
    const auto identities =
        ca::check_three_way_identities(table.value(), heads_up ? &heads_up.value() : nullptr);
    const std::array<const char *, 6> identity_names{"v1_cell_total", "v2_pair_counts",
                                                     "v3_transpose",  "v4_hero_first_swap",
                                                     "v5_pot_shares", "v6_heads_up"};
    Json identity_report{{"passed", identities.passed()}, {"seconds", seconds_since(phase)}};
    for (std::size_t identity = 0U; identity < identity_names.size(); ++identity) {
      identity_report[identity_names[identity]] = Json{{"checks", identities.checks[identity]},
                                                       {"failures", identities.failures[identity]}};
    }
    if (heads_up) {
      const auto &where = identities.max_folded_shift_classes;
      identity_report["max_folded_equity_shift"] = identities.max_folded_equity_shift;
      identity_report["max_folded_equity_shift_at"] = Json::array(
          {gtosd::class_name(where[0]), gtosd::class_name(where[1]), gtosd::class_name(where[2])});
    } else {
      identity_report["v6_heads_up"]["skipped"] = "--skip-v6 on";
    }
    report["identities"] = identity_report;
    ok = ok && identities.passed();

    // V7: every combo of a class gives the representative's entries.
    if (!options.invariance_classes.empty()) {
      phase = Clock::now();
      Json classes = Json::array();
      std::uint64_t mismatches = 0U;
      const auto &combos = ca::combo_table();
      for (const auto hand_class : options.invariance_classes) {
        if (!table->hero_built(hand_class)) {
          throw std::runtime_error("--invariance class " + gtosd::class_name(hand_class) +
                                   " is not built");
        }
        std::uint32_t checked = 0U;
        std::uint64_t class_mismatches = 0U;
        for (std::uint16_t combo = 0U; combo < ca::combo_count; ++combo) {
          if (combos.hand_class[combo] != hand_class ||
              combo == table->representatives()[hand_class]) {
            continue;
          }
          const auto rows = ca::ThreeWayTable::build_hero_rows(
              ranks.value(), combo, options.threads, options.board_symmetry);
          if (!rows) {
            throw std::runtime_error("hero rows build failed");
          }
          for (std::uint8_t first = 0U; first < class_count; ++first) {
            for (std::uint8_t second = 0U; second < class_count; ++second) {
              class_mismatches += static_cast<std::uint64_t>(
                  rows.value()[static_cast<std::size_t>(first) * class_count + second] !=
                  table->entry(hand_class, first, second));
            }
          }
          ++checked;
        }
        mismatches += class_mismatches;
        classes.push_back(Json{{"class", gtosd::class_name(hand_class)},
                               {"other_combos", checked},
                               {"mismatched_entries", class_mismatches}});
      }
      report["invariance"] = Json{{"classes", classes},
                                  {"mismatched_entries", mismatches},
                                  {"seconds", seconds_since(phase)}};
      ok = ok && mismatches == 0U;
    }

    ca::DeterministicRandom random(options.seed);
    // V8: independent brute force through the exact evaluator.
    if (options.brute_force != 0U) {
      phase = Clock::now();
      Json triples = Json::array();
      std::uint64_t mismatches = 0U;
      for (const auto &triple : brute_force_triples(table.value(), options.brute_force, random)) {
        const auto triple_started = Clock::now();
        const auto &expected = table->entry(triple[0], triple[1], triple[2]);
        const auto brute = ca::evaluate_entry_by_showdown(table->representatives()[triple[0]],
                                                          triple[1], triple[2], options.threads);
        const bool equal = brute && brute.value() == expected;
        mismatches += static_cast<std::uint64_t>(!equal);
        triples.push_back(Json{
            {"classes", Json::array({gtosd::class_name(triple[0]), gtosd::class_name(triple[1]),
                                     gtosd::class_name(triple[2])})},
            {"pairs", expected.pairs},
            {"equal", equal},
            {"seconds", seconds_since(triple_started)}});
      }
      report["brute_force"] =
          Json{{"triples", triples}, {"mismatches", mismatches}, {"seconds", seconds_since(phase)}};
      ok = ok && mismatches == 0U;
    }

    // V9 input for tools/three_way_webapp_check.py.
    if (!options.dump_path.empty()) {
      phase = Clock::now();
      const auto dump = dump_for_webapp(ranks.value(), table.value(), options, random);
      std::ofstream output(options.dump_path);
      output << dump.dump(1) << '\n';
      if (!output) {
        throw std::runtime_error("cannot write " + options.dump_path.string());
      }
      report["dump"] = Json{{"path", options.dump_path.generic_string()},
                            {"combo_triples", dump["combo_triples"].size()},
                            {"class_triples", dump["class_triples"].size()},
                            {"seconds", seconds_since(phase)}};
    }

    report["total_seconds"] = seconds_since(started);
    std::cout << report.dump(2) << '\n';
    std::cout << "PREFLOP_BLUEPRINT_THREE_WAY_TABLE=" << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_THREE_WAY_TABLE=FAIL " << error.what() << '\n';
    return 1;
  }
}
