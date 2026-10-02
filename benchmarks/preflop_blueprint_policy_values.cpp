// Part A of phase 3b (PHASE3_SPEC_2026-09-30 section 6.2): the values of a
// fixed policy, 2 or 3 seats, through the trainer's own traversal
// (Trainer::evaluate_policy_values, policy-only initialization): per seat the
// EV, the counterfactual value of every action at every preflop decision,
// the other seats' reach there, the best preflop response (per class and per
// combo), chart sets played inside our game (monker_chart_values.hpp), and
// the expected rake computed independently at the terminals, which the sum
// of the seats' EVs must equal with the opposite sign.
//
// Boards: --flops K samples K physical flops with all their runouts (standard
// errors over the flops, the i.i.d. units); --all-flops enumerates the 573
// canonical flops with their orbit weights (exact; --flop-limit stops early,
// a partial pass); --boards-file / --canonical-river-boards (with --checkdown)
// evaluate an explicit exact list in blocks of --chunk-boards. --state FILE
// makes the pass resumable: the running sums are written after every chunk
// and a rerun with the same command continues (--stop-after-chunks N stops
// early on purpose).
//
// Writes JSON with --out (schema gtosd.preflop_blueprint_policy_values.v1,
// the layout of gtosd_preflop_blueprint_monker_values, heroes of length N),
// read by tools/monker_compare/monker_in_our_game.py and part_a_values.py.
// --values per_class (default, the spec's choice for sampled lists) gives
// every combo of a class the pooled class estimate; per_combo gives the
// per-combo means value_sum / live_sum. The two agree only on a suit-closed
// list; the canonical lists (--all-flops, --canonical-river-boards) are not
// suit-closed, and there the pooled class values are the exact per-combo
// values (the heads-up evaluator averages every suit image of a canonical
// flop), so per_combo is meant for physical lists.
#include "monker_chart_values.hpp"

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/card_abstraction/three_way_table.hpp"
#include "gtosd/core/cards.hpp"
#include "gtosd/preflop_blueprint/action_labels.hpp"
#include "gtosd/preflop_blueprint/board_class_rows.hpp"
#include "gtosd/preflop_blueprint/board_texture.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/game_model.hpp"
#include "gtosd/preflop_blueprint/policy_file.hpp"
#include "gtosd/preflop_blueprint/trainer.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

namespace ca = gtosd::card_abstraction;
namespace pb = gtosd::preflop_blueprint;
namespace mc = gtosd::monker_charts;
using Clock = std::chrono::steady_clock;
using Json = nlohmann::ordered_json;

constexpr double ante_scale = 1.0 / static_cast<double>(gtosd::Money::units_per_ante);
constexpr double check_tolerance = 1e-9;
constexpr std::size_t class_count = mc::class_count;
constexpr std::size_t combos = mc::hero_combos;

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::uint64_t parse_unsigned(const std::string_view text) {
  std::size_t consumed = 0U;
  const auto value = std::stoull(std::string(text), &consumed, 0);
  if (consumed != text.size()) {
    throw std::runtime_error("invalid number: " + std::string(text));
  }
  return value;
}

double parse_decimal(const std::string_view text) {
  std::size_t consumed = 0U;
  const auto value = std::stod(std::string(text), &consumed);
  if (consumed != text.size() || !std::isfinite(value)) {
    throw std::runtime_error("invalid decimal: " + std::string(text));
  }
  return value;
}

Json vector_json(const std::vector<double> &values) {
  Json array = Json::array();
  for (const auto value : values) {
    array.push_back(value);
  }
  return array;
}

Json peaks_json() {
  const auto peaks = pb::process_memory_peaks();
  return Json{{"working_set_bytes", peaks.working_set_bytes},
              {"peak_working_set_bytes", peaks.peak_working_set_bytes},
              {"private_commit_bytes", peaks.private_commit_bytes},
              {"peak_private_commit_bytes", peaks.peak_private_commit_bytes}};
}

// An exact weighted board list from a text file (the format of the trainer
// CLI): one board per line, flop, turn and river, an optional weight.
pb::TrainingBoards read_boards_file(const std::filesystem::path &path) {
  std::istringstream input(read_file(path));
  pb::TrainingBoards boards;
  boards.sample = false;
  std::string line;
  std::size_t number = 0U;
  while (std::getline(input, line)) {
    ++number;
    if (const auto hash = line.find('#'); hash != std::string::npos)
      line.erase(hash);
    std::istringstream fields(line);
    std::vector<std::string> tokens;
    for (std::string token; fields >> token;)
      tokens.push_back(token);
    if (tokens.empty())
      continue;
    const auto where = path.string() + ":" + std::to_string(number);
    if (tokens.size() != 5U && tokens.size() != 6U)
      throw std::runtime_error(where + ": a board line has 5 cards and an optional weight");
    std::array<gtosd::CardId, 5> cards{};
    std::uint64_t mask = 0U;
    for (std::size_t index = 0; index < 5U; ++index) {
      const auto card = gtosd::parse_card(tokens[index]);
      if (!card)
        throw std::runtime_error(where + ": invalid card " + tokens[index]);
      cards[index] = card.value();
      const auto bit = std::uint64_t{1} << card.value().value();
      if ((mask & bit) != 0U)
        throw std::runtime_error(where + ": a card appears twice");
      mask |= bit;
    }
    const double weight = tokens.size() == 6U ? parse_decimal(tokens[5]) : 1.0;
    if (!(weight > 0.0))
      throw std::runtime_error(where + ": the weight must be positive");
    ca::BoardHistory history;
    history.flop = {cards[0], cards[1], cards[2]};
    std::sort(history.flop.begin(), history.flop.end());
    history.turn = cards[3];
    history.river = cards[4];
    boards.histories.push_back(history);
    boards.weights.push_back(weight);
  }
  if (boards.histories.empty())
    throw std::runtime_error("no board in " + path.string());
  return boards;
}

// The 19,998 suit-canonical 5-card boards weighted by their orbit sizes
// (total 376,992): exact for a checkdown tree.
pb::TrainingBoards canonical_river_board_list(const ca::BoardCatalog &catalog) {
  pb::TrainingBoards boards;
  boards.sample = false;
  for (const auto &board : catalog.river_boards()) {
    auto cards = board.cards;
    std::sort(cards.begin(), cards.end());
    ca::BoardHistory history;
    history.flop = {cards[0], cards[1], cards[2]};
    history.turn = cards[3];
    history.river = cards[4];
    boards.histories.push_back(history);
    boards.weights.push_back(static_cast<double>(board.multiplicity));
  }
  return boards;
}

struct ChartSet {
  std::string name;
  std::filesystem::path directory;
};

enum class Convention : std::uint8_t { PerClass, PerCombo };

struct Options {
  std::filesystem::path config_path;
  std::filesystem::path resources_dir;
  std::filesystem::path buckets_dir;
  std::filesystem::path policy_path;
  std::filesystem::path output_path;
  std::filesystem::path series_path;
  std::filesystem::path state_path;
  std::filesystem::path three_way_table_path;
  std::filesystem::path boards_file;
  std::filesystem::path board_texture_path;
  bool board_class_rows{false};
  bool class_rows{false};
  bool all_flops{false};
  bool canonical_river_boards{false};
  bool checkdown{false};
  bool validation{false};
  bool combo_values{true};
  bool check{false};
  std::optional<pb::PreflopTerminals> preflop_terminals;
  std::optional<bool> hero_folded_shortcut;
  std::uint32_t flops{64U};
  std::uint32_t flop_limit{0U};
  std::uint32_t chunk_boards{1'056U};
  std::uint32_t stop_after_chunks{0U};
  std::uint32_t partition_target{64U};
  std::uint64_t seed{0x5041'5254'4120'4121ULL};
  unsigned threads{1U};
  Convention convention{Convention::PerClass};
  double target_pot_percent{1.0};
  std::vector<ChartSet> charts;
};

Options parse_options(const int argc, char **argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view name(argv[index]);
    if (name == "--board-class-rows") {
      options.board_class_rows = true;
      continue;
    }
    if (name == "--class-rows") {
      options.class_rows = true;
      continue;
    }
    if (name == "--all-flops") {
      options.all_flops = true;
      continue;
    }
    if (name == "--canonical-river-boards") {
      options.canonical_river_boards = true;
      continue;
    }
    if (name == "--checkdown") {
      options.checkdown = true;
      continue;
    }
    if (name == "--validation") {
      options.validation = true;
      continue;
    }
    if (name == "--no-combo-values") {
      options.combo_values = false;
      continue;
    }
    if (name == "--check") {
      options.check = true;
      continue;
    }
    if (index + 1 >= argc) {
      throw std::runtime_error("missing value for " + std::string(name));
    }
    const std::string_view value(argv[++index]);
    if (name == "--config") {
      options.config_path = value;
    } else if (name == "--resources-dir") {
      options.resources_dir = value;
    } else if (name == "--buckets-dir") {
      options.buckets_dir = value;
    } else if (name == "--policy") {
      options.policy_path = value;
    } else if (name == "--out") {
      options.output_path = value;
    } else if (name == "--series-out") {
      options.series_path = value;
    } else if (name == "--state") {
      options.state_path = value;
    } else if (name == "--three-way-table") {
      options.three_way_table_path = value;
    } else if (name == "--boards-file") {
      options.boards_file = value;
    } else if (name == "--board-texture-map") {
      options.board_texture_path = value;
    } else if (name == "--flops") {
      options.flops = static_cast<std::uint32_t>(parse_unsigned(value));
    } else if (name == "--flop-limit") {
      options.flop_limit = static_cast<std::uint32_t>(parse_unsigned(value));
    } else if (name == "--chunk-boards") {
      options.chunk_boards = static_cast<std::uint32_t>(parse_unsigned(value));
    } else if (name == "--stop-after-chunks") {
      options.stop_after_chunks = static_cast<std::uint32_t>(parse_unsigned(value));
    } else if (name == "--partition-target") {
      options.partition_target = static_cast<std::uint32_t>(parse_unsigned(value));
    } else if (name == "--seed") {
      options.seed = parse_unsigned(value);
    } else if (name == "--threads") {
      options.threads = static_cast<unsigned>(parse_unsigned(value));
    } else if (name == "--target-pot-percent") {
      options.target_pot_percent = parse_decimal(value);
    } else if (name == "--values") {
      if (value == "per_class") {
        options.convention = Convention::PerClass;
      } else if (value == "per_combo") {
        options.convention = Convention::PerCombo;
      } else {
        throw std::runtime_error("--values takes per_class or per_combo");
      }
    } else if (name == "--preflop-terminals") {
      if (value == "class_cache") {
        options.preflop_terminals = pb::PreflopTerminals::ClassCache;
      } else if (value == "board_kernels") {
        options.preflop_terminals = pb::PreflopTerminals::BoardKernels;
      } else {
        throw std::runtime_error("--preflop-terminals takes class_cache or board_kernels");
      }
    } else if (name == "--hero-folded-shortcut") {
      if (value == "on") {
        options.hero_folded_shortcut = true;
      } else if (value == "off") {
        options.hero_folded_shortcut = false;
      } else {
        throw std::runtime_error("--hero-folded-shortcut takes on or off");
      }
    } else if (name == "--folded-cards") {
      if (value != "dead") {
        throw std::runtime_error("--folded-cards " + std::string(value) +
                                 " is not supported: folded cards are dead");
      }
    } else if (name == "--charts") {
      const auto equals = value.find('=');
      if (equals == std::string_view::npos || equals == 0U || equals + 1U >= value.size()) {
        throw std::runtime_error("--charts takes NAME=DIRECTORY");
      }
      options.charts.push_back({std::string(value.substr(0U, equals)),
                                std::filesystem::path(value.substr(equals + 1U))});
    } else {
      throw std::runtime_error("unknown argument " + std::string(name));
    }
  }
  if (options.config_path.empty() || options.resources_dir.empty() ||
      options.buckets_dir.empty() || options.policy_path.empty()) {
    throw std::runtime_error("--config, --resources-dir, --buckets-dir and --policy are required");
  }
  if (!options.board_texture_path.empty() && !options.board_class_rows) {
    throw std::runtime_error("--board-texture-map requires --board-class-rows");
  }
  if (options.board_class_rows && options.class_rows) {
    throw std::runtime_error("choose --board-class-rows or --class-rows");
  }
  const int kinds = (options.all_flops ? 1 : 0) + (options.boards_file.empty() ? 0 : 1) +
                    (options.canonical_river_boards ? 1 : 0);
  if (kinds > 1) {
    throw std::runtime_error("choose one of --all-flops, --boards-file, --canonical-river-boards");
  }
  if (kinds == 0 && options.flops == 0U) {
    throw std::runtime_error("--flops must be positive");
  }
  if (!options.all_flops && options.flop_limit > 0U) {
    throw std::runtime_error("--flop-limit goes with --all-flops");
  }
  if (options.canonical_river_boards && !options.checkdown) {
    throw std::runtime_error("--canonical-river-boards requires --checkdown");
  }
  if (options.chunk_boards == 0U) {
    throw std::runtime_error("--chunk-boards must be positive");
  }
  if (options.preflop_terminals == pb::PreflopTerminals::BoardKernels &&
      (!options.validation || kinds == 0 || options.all_flops)) {
    throw std::runtime_error("--preflop-terminals board_kernels requires --validation and an exact "
                             "list (--boards-file or --canonical-river-boards)");
  }
  if (options.hero_folded_shortcut == false && !options.validation) {
    throw std::runtime_error("--hero-folded-shortcut off requires --validation");
  }
  if (!(options.target_pot_percent > 0.0 && options.target_pot_percent <= 100.0)) {
    throw std::runtime_error("--target-pot-percent must be in (0, 100]");
  }
  return options;
}

// The per-class sums of one chunk of a hero, read as [slot][action][class].
struct ChunkView {
  const pb::PolicyValuesHero *hero{nullptr};
  const pb::PolicyValuesHeroChunk *chunk{nullptr};
  [[nodiscard]] double value(const std::size_t slot, const std::size_t action,
                             const std::size_t hand_class) const {
    return chunk->values[hero->class_offset[slot] + action * class_count + hand_class];
  }
};

// Sum over the seat's own preflop nodes of a strategy's value against the
// chunk's sums: V_x(m, c) = sum_a x(m, c, a) [S(m, a, c) + sum over the next
// own nodes of (V_x - V_ours)], V_ours(m, c) = sum_a ours(m, c, a) S(m, a, c).
// Returns, per class, root(c) + sum over the top nodes of (V_x - V_ours): the
// pooled EV of x is then sum_c (n_c / 630) of that divided by wt[c].
std::vector<double> strategy_class_values(const mc::HeroTree &tree, const ChunkView &view,
                                          const mc::PreflopStrategy &ours,
                                          const mc::PreflopStrategy &strategy) {
  const auto slots = tree.nodes.size();
  std::vector<std::vector<double>> value_ours(slots, std::vector<double>(class_count, 0.0));
  std::vector<std::vector<double>> value_x(slots, std::vector<double>(class_count, 0.0));
  for (std::size_t slot = 0; slot < slots; ++slot) {
    const auto actions = ours[slot][0].size();
    for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
      double total = 0.0;
      for (std::size_t action = 0; action < actions; ++action) {
        total += ours[slot][hand_class][action] * view.value(slot, action, hand_class);
      }
      value_ours[slot][hand_class] = total;
    }
  }
  for (auto position = tree.order.rbegin(); position != tree.order.rend(); ++position) {
    const auto slot = *position;
    const auto actions = ours[slot][0].size();
    for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
      double total = 0.0;
      for (std::size_t action = 0; action < actions; ++action) {
        double q = view.value(slot, action, hand_class);
        for (const auto child : tree.next[slot][action]) {
          q += value_x[child][hand_class] - value_ours[child][hand_class];
        }
        total += strategy[slot][hand_class][action] * q;
      }
      value_x[slot][hand_class] = total;
    }
  }
  std::vector<double> result(view.chunk->root);
  for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
    for (const auto slot : tree.top) {
      result[hand_class] += value_x[slot][hand_class] - value_ours[slot][hand_class];
    }
  }
  return result;
}

// The pooled EV contribution of a chunk: sum_c (n_c / 630) class_value[c] / wt[c].
double pooled_chunk_ev(const std::vector<double> &class_values,
                       const std::vector<double> &class_weight,
                       const std::vector<double> &class_combos) {
  double total = 0.0;
  for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
    if (class_weight[hand_class] > 0.0) {
      total += class_combos[hand_class] / static_cast<double>(combos) * class_values[hand_class] /
               class_weight[hand_class];
    }
  }
  return total;
}

// Standard error of the sum of a series of K chunk contributions of equal
// weight 1 / K (the series is the per-flop estimate: K times the contribution).
double series_standard_error(const std::vector<double> &contributions) {
  if (contributions.size() < 2U) {
    return 0.0;
  }
  std::vector<double> series;
  series.reserve(contributions.size());
  const double scale = static_cast<double>(contributions.size());
  for (const auto value : contributions) {
    series.push_back(scale * value);
  }
  return mc::standard_error(series);
}

// Standard error (delta method) of a pooled ratio estimate sum_c (n_c / 630)
// X_c / W_c over K i.i.d. flops, X_c = sum_k X_{c,k} and W_c = sum_k w_{c,k} the
// class's live weight: the linearized per-flop contribution is
// sum_c (n_c / 630) (X_{c,k} - R_c w_{c,k}) / W_c with R_c = X_c / W_c; the
// contributions have mean zero and K times them is the series. Without the
// R_c w_{c,k} term the variability of the live weights would be ignored.
double pooled_standard_error(const std::vector<std::vector<double>> &chunk_class_values,
                             const std::vector<const std::vector<double> *> &chunk_live,
                             const std::vector<double> &class_weight,
                             const std::vector<double> &class_combos) {
  const auto count = chunk_class_values.size();
  if (count < 2U) {
    return 0.0;
  }
  std::vector<double> ratio(class_count, 0.0);
  for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
    double total = 0.0;
    for (const auto &values : chunk_class_values) {
      total += values[hand_class];
    }
    ratio[hand_class] = class_weight[hand_class] > 0.0 ? total / class_weight[hand_class] : 0.0;
  }
  std::vector<double> series;
  series.reserve(count);
  for (std::size_t chunk = 0; chunk < count; ++chunk) {
    double contribution = 0.0;
    for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
      if (class_weight[hand_class] > 0.0) {
        contribution += class_combos[hand_class] / static_cast<double>(combos) *
                        (chunk_class_values[chunk][hand_class] -
                         ratio[hand_class] * (*chunk_live[chunk])[hand_class]) /
                        class_weight[hand_class];
      }
    }
    series.push_back(static_cast<double>(count) * contribution);
  }
  return mc::standard_error(series);
}

mc::PreflopStrategy one_hot(const mc::PreflopStrategy &ours,
                            const std::vector<std::vector<std::size_t>> &choice) {
  mc::PreflopStrategy strategy = ours;
  for (std::size_t slot = 0; slot < strategy.size(); ++slot) {
    for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
      std::fill(strategy[slot][hand_class].begin(), strategy[slot][hand_class].end(), 0.0);
      strategy[slot][hand_class][choice[slot][hand_class]] = 1.0;
    }
  }
  return strategy;
}

bool write_output(const std::filesystem::path &path, const std::string &text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << text;
  output.flush();
  return static_cast<bool>(output);
}

int run(const int argc, char **argv) {
  const auto started = Clock::now();
  const auto options = parse_options(argc, argv);

  const auto config = pb::parse_game_config_json(read_file(options.config_path));
  if (!config) {
    throw std::runtime_error(std::string("configuration rejected: ") +
                             pb::config_error_name(config.error()));
  }
  pb::CompileOptions compile_options;
  compile_options.checkdown_at_flop = options.checkdown;
  const auto compiled = pb::CompiledGame::compile(config.value(), compile_options);
  if (!compiled) {
    throw std::runtime_error(std::string("compile failed: ") +
                             pb::game_model_error_name(compiled.error()));
  }
  const auto &game = compiled.value();
  const auto players = static_cast<std::size_t>(game.config().player_count);
  if (players != 2U && players != 3U) {
    throw std::runtime_error("2 or 3 players");
  }
  const auto &rake = game.config().rake;

  auto ranks = ca::RankTable::load(options.resources_dir / "rank_table_v1.bin");
  auto all_in = ca::AllInTable::load(options.resources_dir / "preflop_all_in_v1.bin");
  auto flop = ca::BucketTable::load(options.buckets_dir / "flop_buckets_v1.bin");
  auto turn = ca::BucketTable::load(options.buckets_dir / "turn_buckets_v1.bin");
  auto river = ca::BucketTable::load(options.buckets_dir / "river_buckets_v1.bin");
  if (!ranks || !all_in || !flop || !turn || !river) {
    throw std::runtime_error("resources or bucket tables missing");
  }
  const auto catalog = ca::BoardCatalog::build();
  pb::TrainerResources resources;
  resources.ranks = &ranks.value();
  resources.all_in = &all_in.value();
  resources.catalog = &catalog;
  resources.flop = &flop.value();
  resources.turn = &turn.value();
  resources.river = &river.value();
  pb::TrainerConfig trainer_config;
  trainer_config.flop_capacity = flop.value().capacity();
  trainer_config.turn_capacity = turn.value().capacity();
  trainer_config.river_capacity = river.value().capacity();
  trainer_config.threads = std::max(1U, options.threads);
  trainer_config.partition_target_nodes = options.partition_target;
  trainer_config.validation = options.validation;
  if (options.preflop_terminals) {
    trainer_config.preflop_terminals = *options.preflop_terminals;
  }
  if (options.hero_folded_shortcut) {
    trainer_config.hero_folded_shortcut = *options.hero_folded_shortcut;
  }
  std::optional<pb::ClassBucketRows> class_rows;
  std::optional<pb::BoardClassRows> board_class_rows;
  std::string abstraction = "buckets";
  Json board_texture_json = nullptr;
  if (options.class_rows) {
    auto built = pb::ClassBucketRows::build(flop.value(), turn.value(), river.value());
    if (!built) {
      throw std::runtime_error("class row mapping failed");
    }
    class_rows.emplace(std::move(built.value()));
    resources.class_rows = &*class_rows;
    trainer_config.flop_capacity = class_rows->count(ca::BucketStreet::Flop);
    trainer_config.turn_capacity = class_rows->count(ca::BucketStreet::Turn);
    trainer_config.river_capacity = class_rows->count(ca::BucketStreet::River);
    abstraction = "class-major-v1";
  }
  if (options.board_class_rows) {
    auto texture = pb::BoardTextureMap::identity();
    if (!options.board_texture_path.empty()) {
      auto loaded_texture = pb::BoardTextureMap::load(options.board_texture_path, catalog);
      if (!loaded_texture) {
        throw std::runtime_error("board texture map " + options.board_texture_path.string() +
                                 " rejected: " + pb::texture_error_name(loaded_texture.error()));
      }
      texture = std::move(loaded_texture.value());
    }
    board_class_rows.emplace(flop.value().capacity(), turn.value().capacity(),
                             river.value().capacity(), std::move(texture));
    resources.board_class_rows = &*board_class_rows;
    trainer_config.flop_capacity = board_class_rows->count(ca::BucketStreet::Flop);
    trainer_config.turn_capacity = board_class_rows->count(ca::BucketStreet::Turn);
    trainer_config.river_capacity = board_class_rows->count(ca::BucketStreet::River);
    abstraction = board_class_rows->fingerprint();
    const auto &board_texture = board_class_rows->texture();
    board_texture_json = {{"name", board_texture.name()},
                          {"fingerprint", board_texture.fingerprint()},
                          {"classes", Json::array({board_texture.classes(ca::BucketStreet::Flop),
                                                   board_texture.classes(ca::BucketStreet::Turn),
                                                   board_texture.classes(ca::BucketStreet::River)})}};
  }
  // The three-player class table of the class cache (3 players, class-cache
  // mode): read by the evaluator's creation only.
  std::optional<ca::ThreeWayTable> three_way;
  std::string three_way_fingerprint;
  if (players == 3U && trainer_config.preflop_terminals == pb::PreflopTerminals::ClassCache) {
    const auto source = options.three_way_table_path.empty()
                            ? options.resources_dir / std::string(ca::three_way_table_file_name)
                            : options.three_way_table_path;
    auto loaded = ca::ThreeWayTable::load(source, ca::ThreeWayLoad::CompleteOnly);
    if (!loaded) {
      throw std::runtime_error("three-player class table " + source.string() + " rejected: " +
                               ca::resource_error_name(loaded.error()));
    }
    three_way.emplace(std::move(loaded.value()));
    resources.three_way = &*three_way;
    three_way_fingerprint = three_way->fingerprint();
  }

  pb::PolicyFileInfo info;
  auto loaded = pb::load_policy(options.policy_path, game, &info);
  if (!loaded) {
    throw std::runtime_error(std::string("policy rejected: ") +
                             pb::policy_file_error_name(loaded.error()));
  }
  std::unique_ptr<pb::BucketPolicy> policy_holder = std::move(loaded.value());
  {
    const auto &layout = policy_holder->layout();
    if (board_class_rows) {
      const auto suffix = "|abstraction=" + board_class_rows->fingerprint() +
                          "|flop=" + flop.value().fingerprint() + "|turn=" +
                          turn.value().fingerprint() + "|river=" + river.value().fingerprint();
      if (!info.source.ends_with(suffix) ||
          layout.flop_capacity != board_class_rows->count(ca::BucketStreet::Flop) ||
          layout.turn_capacity != board_class_rows->count(ca::BucketStreet::Turn) ||
          layout.river_capacity != board_class_rows->count(ca::BucketStreet::River)) {
        throw std::runtime_error("the policy was not trained with these board class rows, board "
                                 "texture and bucket tables (source " +
                                 info.source + ")");
      }
    } else if (!class_rows && info.source.find("|abstraction=") != std::string::npos) {
      throw std::runtime_error("the policy was trained with another abstraction (source " +
                               info.source + "); pass --board-class-rows for a step-2 policy");
    }
    if (layout.flop_capacity != trainer_config.flop_capacity ||
        layout.turn_capacity != trainer_config.turn_capacity ||
        layout.river_capacity != trainer_config.river_capacity) {
      throw std::runtime_error("the policy's capacities do not match the abstraction");
    }
  }

  // Chart nodes, our preflop rows (read before the table moves into the
  // evaluator) and the hero trees.
  std::map<std::uint32_t, mc::ChartNode> chart_of;
  for (auto &chart : mc::chart_nodes(game)) {
    chart_of.emplace(chart.node, std::move(chart));
  }
  std::vector<std::vector<std::uint32_t>> hero_nodes(players);
  for (const auto &node : game.nodes()) {
    if (node.kind == pb::NodeKind::Decision && node.street == gtosd::Street::Preflop) {
      hero_nodes[node.actor].push_back(node.id);
    }
  }
  std::vector<mc::HeroTree> trees(players);
  std::vector<mc::PreflopStrategy> ours(players);
  for (std::size_t hero = 0; hero < players; ++hero) {
    trees[hero] = mc::hero_tree(game, hero_nodes[hero], static_cast<std::uint8_t>(hero));
    ours[hero].resize(hero_nodes[hero].size());
    for (std::size_t slot = 0; slot < hero_nodes[hero].size(); ++slot) {
      ours[hero][slot].resize(class_count);
      for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
        const auto row =
            policy_holder->row(hero_nodes[hero][slot], static_cast<std::uint32_t>(hand_class));
        ours[hero][slot][hand_class].assign(row.begin(), row.end());
      }
    }
  }

  // Boards.
  pb::PolicyValuesOptions values_options;
  std::optional<pb::TrainingBoards> list;
  std::string mode = "sampled";
  std::string board_source;
  if (options.all_flops) {
    values_options.boards = pb::PolicyValuesBoardKind::CanonicalFlops;
    values_options.flops = options.flop_limit;
    mode = options.flop_limit == 0U || options.flop_limit >= catalog.flops().size() ? "exact"
                                                                                     : "partial";
    board_source = "canonical-flops";
  } else if (!options.boards_file.empty()) {
    values_options.boards = pb::PolicyValuesBoardKind::List;
    list.emplace(read_boards_file(options.boards_file));
    mode = "list";
    board_source = "file:" + options.boards_file.generic_string();
  } else if (options.canonical_river_boards) {
    values_options.boards = pb::PolicyValuesBoardKind::List;
    list.emplace(canonical_river_board_list(catalog));
    mode = "list";
    board_source = "canonical-river-boards";
  } else {
    values_options.boards = pb::PolicyValuesBoardKind::PhysicalFlops;
    values_options.flops = options.flops;
    values_options.seed = options.seed;
    board_source = "physical-flops";
  }
  if (list) {
    values_options.list = &*list;
  }
  values_options.chunk_boards = options.chunk_boards;
  values_options.state_path = options.state_path;
  values_options.stop_after_chunks = options.stop_after_chunks;
  values_options.progress = [&](const pb::PolicyValuesProgress &progress) {
    std::cerr << "{\"event\": \"progress\", \"chunks_done\": " << progress.chunks_done
              << ", \"chunks_total\": " << progress.chunks_total
              << ", \"boards_done\": " << progress.boards_done
              << ", \"elapsed_seconds\": " << progress.seconds << ", \"eta_seconds\": "
              << (progress.chunks_done > 0U
                      ? progress.seconds / progress.chunks_done *
                            static_cast<double>(progress.chunks_total - progress.chunks_done)
                      : 0.0)
              << ", \"process_bytes\": " << pb::process_working_set_bytes() << "}\n"
              << std::flush;
  };
  const double load_seconds = std::chrono::duration<double>(Clock::now() - started).count();
  std::cerr << "{\"event\": \"start\", \"mode\": \"" << mode << "\", \"players\": " << players
            << ", \"threads\": " << trainer_config.threads
            << ", \"policy_entries\": " << policy_holder->table().size()
            << ", \"load_seconds\": " << load_seconds << "}\n"
            << std::flush;

  auto evaluated = pb::Trainer::evaluate_policy_values(game, resources, trainer_config,
                                                       std::move(*policy_holder), values_options);
  policy_holder.reset();
  three_way.reset();
  if (!evaluated) {
    throw std::runtime_error(std::string("evaluation failed: ") +
                             pb::trainer_error_name(evaluated.error()));
  }
  const auto &values = evaluated.value();
  const auto peaks = peaks_json();
  if (!values.complete) {
    std::cerr << "{\"event\": \"stopped\", \"chunks_done\": " << values.chunks_done
              << ", \"chunks_total\": " << values.chunks_total << ", \"state\": \""
              << options.state_path.generic_string() << "\"}\n";
    std::cout << "PREFLOP_BLUEPRINT_POLICY_VALUES=PARTIAL chunks " << values.chunks_done << "/"
              << values.chunks_total << '\n';
    return 0;
  }

  // Estimates from the sums.
  const auto &combo_table = ca::combo_table();
  const auto class_combos = mc::class_combos();
  const auto classes = mc::hand_classes();
  std::vector<std::string> class_labels(class_count);
  for (const auto &[hand_class, label] : classes.label_by_class) {
    class_labels.at(hand_class) = label;
  }
  const bool sampled = mode == "sampled";
  const bool per_class = options.convention == Convention::PerClass;
  std::vector<double> class_weight(class_count, 0.0);
  for (std::size_t combo = 0; combo < combos; ++combo) {
    class_weight[combo_table.hand_class[combo]] += values.live_sum[combo];
  }
  const auto class_mean = [&](const std::vector<double> &sums, const std::size_t offset) {
    // Per combo: sums[offset + combo] pooled per class over the live weight.
    std::vector<double> numerator(class_count, 0.0);
    for (std::size_t combo = 0; combo < combos; ++combo) {
      numerator[combo_table.hand_class[combo]] += sums[offset + combo];
    }
    std::vector<double> result(combos, 0.0);
    for (std::size_t combo = 0; combo < combos; ++combo) {
      const auto hand_class = combo_table.hand_class[combo];
      if (per_class) {
        result[combo] = class_weight[hand_class] > 0.0 ? numerator[hand_class] / class_weight[hand_class]
                                                       : 0.0;
      } else {
        result[combo] =
            values.live_sum[combo] > 0.0 ? sums[offset + combo] / values.live_sum[combo] : 0.0;
      }
    }
    return result;
  };

  const auto &game_config = game.config();
  const double initial_pot =
      static_cast<double>(game_config.ante.units() * game_config.player_count +
                          game_config.button_blind.units()) *
      ante_scale;
  const double target_antes = 0.01 * options.target_pot_percent * initial_pot;
  const auto percent = [&](const double antes) {
    return initial_pot > 0.0 ? 100.0 * antes / initial_pot : 0.0;
  };
  std::vector<std::string> failures;
  const auto check = [&](const bool condition, const std::string &what) {
    if (!condition) {
      failures.push_back(what);
    }
  };

  std::vector<pb::PreflopActionValues> action_values(players);
  std::vector<double> ev(players, 0.0);
  std::vector<double> ev_direct(players, 0.0);
  std::vector<double> ev_se(players, 0.0);
  std::vector<double> ev_direct_se(players, 0.0);
  std::vector<mc::PreflopResponse> responses(players);
  std::vector<double> gain_se(players, 0.0);
  const double weight = values.weight > 0.0 ? values.weight : 1.0;
  for (std::size_t hero = 0; hero < players; ++hero) {
    const auto &totals = values.heroes[hero];
    auto &pav = action_values[hero];
    pav.hero = static_cast<std::uint8_t>(hero);
    pav.groups = values.chunks_done;
    pav.nodes = totals.nodes;
    const auto slots = totals.nodes.size();
    pav.combo_values.resize(slots);
    pav.opponent_reach.resize(slots);
    pav.class_ev.resize(slots);
    pav.class_se.resize(slots);
    pav.class_weight.resize(slots);
    for (std::size_t slot = 0; slot < slots; ++slot) {
      const auto actions = totals.actions[slot];
      pav.opponent_reach[slot] = class_mean(totals.reach_sum, slot * combos);
      pav.class_weight[slot].assign(class_count, 0.0);
      // Opponent-reach weight of the class: the sum of its combos' reach.
      for (std::size_t combo = 0; combo < combos; ++combo) {
        pav.class_weight[slot][combo_table.hand_class[combo]] += pav.opponent_reach[slot][combo];
      }
      pav.combo_values[slot].resize(actions);
      pav.class_ev[slot].assign(actions, std::vector<double>(class_count, 0.0));
      pav.class_se[slot].assign(actions, std::vector<double>(class_count, 0.0));
      for (std::size_t action = 0; action < actions; ++action) {
        pav.combo_values[slot][action] =
            class_mean(totals.value_sum, totals.value_offset[slot] + action * combos);
        // Conditional EV of the action given the class and the history:
        // the combo values over the opponent reach, in the same convention.
        std::vector<double> numerator(class_count, 0.0);
        for (std::size_t combo = 0; combo < combos; ++combo) {
          numerator[combo_table.hand_class[combo]] += pav.combo_values[slot][action][combo];
        }
        for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
          pav.class_ev[slot][action][hand_class] =
              pav.class_weight[slot][hand_class] > 0.0
                  ? numerator[hand_class] / pav.class_weight[slot][hand_class]
                  : 0.0;
        }
        if (sampled && values.series.size() > 1U) {
          // Ratio of sums over i.i.d. flops: the linearized standard error.
          const double count = static_cast<double>(values.series.size());
          for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
            double numerator_total = 0.0;
            double denominator_total = 0.0;
            for (const auto &chunk : values.series) {
              const ChunkView view{&totals, &chunk.heroes[hero]};
              numerator_total += view.value(slot, action, hand_class);
              denominator_total += chunk.heroes[hero].reach[slot * class_count + hand_class];
            }
            if (!(denominator_total > 0.0)) {
              continue;
            }
            const double ratio = numerator_total / denominator_total;
            double squares = 0.0;
            for (const auto &chunk : values.series) {
              const ChunkView view{&totals, &chunk.heroes[hero]};
              const double residual =
                  count * (view.value(slot, action, hand_class) -
                           ratio * chunk.heroes[hero].reach[slot * class_count + hand_class]);
              squares += residual * residual;
            }
            // sqrt(s^2 / K) / mean(denominator): s^2 the sample variance of
            // the residual series (K times the chunk residuals, mean zero)
            // and mean(denominator) = total / K.
            pav.class_se[slot][action][hand_class] =
                std::sqrt(squares / (count - 1.0) / count) / denominator_total;
          }
        }
      }
    }
    pav.root_values = class_mean(totals.root_sum, 0U);
    double root_mean = 0.0;
    for (const auto value : pav.root_values) {
      root_mean += value;
    }
    ev[hero] = root_mean / static_cast<double>(combos);
    ev_direct[hero] = totals.ev_direct / weight;
    responses[hero] = mc::preflop_response(trees[hero], pav, ours[hero]);
    if (sampled && values.series.size() > 1U) {
      std::vector<std::vector<double>> pooled;
      std::vector<std::vector<double>> gains;
      std::vector<const std::vector<double> *> live;
      std::vector<double> direct;
      const auto response = one_hot(ours[hero], responses[hero].choice);
      for (const auto &chunk : values.series) {
        const ChunkView view{&totals, &chunk.heroes[hero]};
        auto class_ours = strategy_class_values(trees[hero], view, ours[hero], ours[hero]);
        auto class_response = strategy_class_values(trees[hero], view, ours[hero], response);
        for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
          class_response[hand_class] -= class_ours[hand_class];
        }
        pooled.push_back(std::move(class_ours));
        gains.push_back(std::move(class_response));
        live.push_back(&chunk.live);
        direct.push_back(chunk.heroes[hero].ev_direct);
      }
      ev_se[hero] = pooled_standard_error(pooled, live, class_weight, class_combos);
      ev_direct_se[hero] = series_standard_error(direct);
      gain_se[hero] = pooled_standard_error(gains, live, class_weight, class_combos);
    }
  }
  const double expected_rake = values.rake_sum / weight;
  double ev_sum = 0.0;
  double ev_direct_sum = 0.0;
  for (std::size_t hero = 0; hero < players; ++hero) {
    ev_sum += ev[hero];
    ev_direct_sum += ev_direct[hero];
  }
  // The rake identity. It is exact per board when every value is
  // board-restricted (the heads-up path, and 3 seats in board_kernels mode):
  // the direct EVs then sum to minus the expected rake on every list. In
  // class_cache mode (3 seats) the preflop terminals carry the cache's
  // board-free class values (spec 3.9), so the identity holds on an exact list
  // (tower property) and only in expectation on a sampled or partial one; the
  // residual is then reported with its standard error over the flops (02/10,
  // T4: one random 3WAY50 policy gave per-flop residuals from -1.15 to +0.89 a,
  // mean -0.14 +- 0.29 a over 6 flops, and 1e-14 in board_kernels mode on the
  // same flop's runouts). The pooled EVs satisfy it on an exact list only.
  const bool exact_list = mode == "exact" || board_source == "canonical-river-boards";
  const bool identity_exact =
      exact_list || players == 2U ||
      trainer_config.preflop_terminals == pb::PreflopTerminals::BoardKernels;
  double identity_se = 0.0;
  if (sampled && values.series.size() > 1U) {
    std::vector<double> residuals;
    for (const auto &chunk : values.series) {
      double residual = chunk.rake;
      for (const auto &hero_chunk : chunk.heroes)
        residual += hero_chunk.ev_direct;
      residuals.push_back(residual);
    }
    identity_se = series_standard_error(residuals);
  }
  if (identity_exact) {
    check(std::abs(ev_direct_sum + expected_rake) <= check_tolerance,
          "the direct EVs sum to minus the expected rake");
  }
  if (exact_list) {
    check(std::abs(ev_sum + expected_rake) <= check_tolerance,
          "the pooled EVs sum to minus the expected rake on an exact list");
  }
  if (rake.enabled) {
    const double cap_antes = static_cast<double>(rake.cap.units()) * ante_scale;
    check(expected_rake >= -check_tolerance && expected_rake <= cap_antes + check_tolerance,
          "the expected rake lies between zero and the cap");
  } else {
    check(std::abs(expected_rake) <= check_tolerance, "no rake: the expected rake is zero");
  }

  Json json;
  json["schema"] = "gtosd.preflop_blueprint_policy_values.v1";
  json["config_id"] = game_config.id;
  if (rake.enabled) {
    json["rake"] = {{"basis_points", rake.percentage.basis_points()},
                    {"cap_antes", static_cast<double>(rake.cap.units()) * ante_scale},
                    {"no_flop_no_drop", rake.no_flop_no_drop},
                    {"minimum_pot_antes", static_cast<double>(rake.minimum_pot.units()) * ante_scale}};
  }
  json["tree_fingerprint"] = game.fingerprint();
  json["policy_fingerprint"] = info.policy_fingerprint;
  json["policy_source"] = info.source;
  json["abstraction"] = abstraction;
  json["board_texture"] = board_texture_json;
  json["fingerprints"] = {{"catalog", catalog.fingerprint()},
                          {"flop_table", flop.value().fingerprint()},
                          {"turn_table", turn.value().fingerprint()},
                          {"river_table", river.value().fingerprint()},
                          {"three_way_table", three_way_fingerprint.empty()
                                                  ? Json(nullptr)
                                                  : Json(three_way_fingerprint)}};
  json["capacities"] = {trainer_config.flop_capacity, trainer_config.turn_capacity,
                        trainer_config.river_capacity};
  json["players"] = players;
  Json positions = Json::array();
  for (const auto &position : game_config.positions) {
    positions.push_back(position);
  }
  json["positions"] = positions;
  json["initial_pot_antes"] = initial_pot;
  json["target_pot_percent"] = options.target_pot_percent;
  json["target_antes"] = target_antes;
  json["value_scope"] =
      "antes per hand of the hero (net result of the hand, posted antes included), full "
      "uniform hand ranges; counterfactual values carry the other seats' reach and chance; "
      + std::string(per_class ? "every combo of a class carries the pooled class estimate"
                              : "per-combo means over the boards where the combo is live");
  std::uint64_t physical_flops = 0U;
  if (values_options.boards == pb::PolicyValuesBoardKind::CanonicalFlops) {
    const auto &canonical = catalog.flops();
    const auto total = options.flop_limit == 0U
                           ? canonical.size()
                           : std::min<std::size_t>(options.flop_limit, canonical.size());
    for (std::size_t index = 0; index < total; ++index) {
      physical_flops += canonical[index].multiplicity;
    }
  } else if (values_options.boards == pb::PolicyValuesBoardKind::PhysicalFlops) {
    physical_flops = options.flops;
  }
  json["evaluation"] = {
      {"mode", mode},
      {"board_source", board_source},
      {"flops", values_options.boards == pb::PolicyValuesBoardKind::List ? Json(nullptr)
                                                                          : Json(values.chunks_total)},
      {"physical_flops", values_options.boards == pb::PolicyValuesBoardKind::List
                             ? Json(nullptr)
                             : Json(physical_flops)},
      {"boards", values.boards},
      {"chunks", values.chunks_total},
      {"seed", values_options.boards == pb::PolicyValuesBoardKind::PhysicalFlops
                   ? Json(options.seed)
                   : Json(nullptr)},
      {"threads", trainer_config.threads},
      {"convention", per_class ? "per_class" : "per_combo"},
      {"folded_cards", "dead"},
      {"checkdown", options.checkdown},
      {"preflop_terminals",
       trainer_config.preflop_terminals == pb::PreflopTerminals::BoardKernels ? "board_kernels"
                                                                              : "class_cache"},
      {"validation", trainer_config.validation},
      {"hero_folded_shortcut", trainer_config.hero_folded_shortcut},
      {"resumed", values.resumed},
      {"state", options.state_path.empty() ? Json(nullptr)
                                           : Json(options.state_path.generic_string())},
      {"identity", values.identity},
      {"seconds", values.seconds},
      {"process_peaks", peaks}};
  {
    Json ev_json = Json::array();
    Json ev_se_json = Json::array();
    Json ev_direct_json = Json::array();
    Json ev_direct_se_json = Json::array();
    Json gain_class_json = Json::array();
    Json gain_combo_json = Json::array();
    Json gain_se_json = Json::array();
    Json gain_percent_json = Json::array();
    double max_gain = 0.0;
    for (std::size_t hero = 0; hero < players; ++hero) {
      ev_json.push_back(ev[hero]);
      ev_se_json.push_back(sampled ? Json(ev_se[hero]) : Json(nullptr));
      ev_direct_json.push_back(ev_direct[hero]);
      ev_direct_se_json.push_back(sampled ? Json(ev_direct_se[hero]) : Json(nullptr));
      gain_class_json.push_back(responses[hero].gain_per_class);
      gain_combo_json.push_back(responses[hero].gain_per_combo);
      gain_se_json.push_back(sampled ? Json(gain_se[hero]) : Json(nullptr));
      gain_percent_json.push_back(percent(responses[hero].gain_per_class));
      max_gain = std::max(max_gain, responses[hero].gain_per_class);
    }
    json["estimate"] = {{"ev_antes", ev_json},
                        {"ev_standard_error_antes", ev_se_json},
                        {"ev_direct_antes", ev_direct_json},
                        {"ev_direct_standard_error_antes", ev_direct_se_json},
                        {"ev_sum_antes", ev_sum},
                        {"ev_direct_sum_antes", ev_direct_sum},
                        {"expected_rake_antes", expected_rake},
                        {"rake_identity_residual_antes", ev_direct_sum + expected_rake},
                        {"rake_identity_exact", identity_exact},
                        {"rake_identity_standard_error_antes",
                         sampled ? Json(identity_se) : Json(nullptr)},
                        {"gain_preflop_antes", gain_combo_json},
                        {"gain_preflop_class_antes", gain_class_json},
                        {"gain_preflop_standard_error_antes", gain_se_json},
                        {"gain_preflop_pot_percent", gain_percent_json},
                        {"max_gain_preflop_antes", max_gain},
                        {"passes_target", max_gain <= target_antes}};
  }
  {
    Json labels = Json::array();
    Json class_ids = Json::array();
    for (std::size_t combo = 0; combo < combos; ++combo) {
      labels.push_back(class_labels[combo_table.hand_class[combo]]);
      class_ids.push_back(combo_table.hand_class[combo]);
    }
    json["combos"] = {{"labels", labels}, {"class", class_ids}};
    json["classes"] = class_labels;
  }

  Json heroes = Json::array();
  Json responses_json = Json::array();
  for (std::size_t hero = 0; hero < players; ++hero) {
    const auto &pav = action_values[hero];
    const auto &tree = trees[hero];
    const auto chart_name = [&](const std::size_t slot) {
      return chart_of.at(pav.nodes[slot]).relative;
    };
    // The root identity (monker_in_our_game.py): root(h) = sum over the top
    // nodes of V_ours(m, h) + c P(the hand ends before the hero acts | h), c
    // the hero's payoff of the terminal the other seats' folds reach before
    // it (the fold of everybody before it), 0 when the hero always acts.
    double ended_payoff = 0.0;
    {
      std::uint32_t node_id = game.root();
      while (true) {
        const auto &node = game.nodes()[node_id];
        if (node.kind == pb::NodeKind::TerminalFold) {
          ended_payoff = static_cast<double>(game.fold_payoffs(node.id)[hero]) * ante_scale;
          break;
        }
        if (node.kind != pb::NodeKind::Decision || node.actor == hero) {
          break;
        }
        // The fold edge of the other seat (the first edge).
        node_id = game.edges_of(node_id)[0].child;
      }
    }
    double root_identity = 0.0;
    for (std::size_t combo = 0; combo < combos; ++combo) {
      const auto hand_class = combo_table.hand_class[combo];
      double residual = pav.root_values[combo];
      double ended = 1.0;
      for (const auto slot : tree.top) {
        double value = 0.0;
        for (std::size_t action = 0; action < pav.combo_values[slot].size(); ++action) {
          value += ours[hero][slot][hand_class][action] * pav.combo_values[slot][action][combo];
        }
        residual -= value;
        ended -= pav.opponent_reach[slot][combo];
      }
      root_identity = std::max(root_identity, std::abs(residual - ended_payoff * ended));
    }
    check(root_identity <= check_tolerance,
          "root identity of " + pb::position_name(game, static_cast<std::uint8_t>(hero)) +
              " (residual " + std::to_string(root_identity) + ")");

    Json hero_json;
    hero_json["hero"] = hero;
    hero_json["position"] = pb::position_name(game, static_cast<std::uint8_t>(hero));
    hero_json["ev_antes"] = ev[hero];
    hero_json["ev_direct_antes"] = ev_direct[hero];
    hero_json["ev_standard_error_antes"] = sampled ? Json(ev_se[hero]) : Json(nullptr);
    hero_json["root_value_mean_antes"] = ev[hero];
    hero_json["root_identity_residual_antes"] = root_identity;
    hero_json["ended_before_acting_payoff_antes"] = ended_payoff;
    Json top = Json::array();
    for (const auto slot : tree.top) {
      top.push_back(chart_name(slot));
    }
    hero_json["top"] = top;
    if (options.combo_values) {
      hero_json["root_values"] = vector_json(pav.root_values);
    }
    Json nodes = Json::array();
    for (std::size_t slot = 0; slot < pav.nodes.size(); ++slot) {
      const auto &chart = chart_of.at(pav.nodes[slot]);
      Json node;
      node["chart"] = chart.relative;
      node["node"] = pav.nodes[slot];
      node["path_id"] = pb::node_path_id(game, pav.nodes[slot]);
      node["tokens"] = chart.tokens;
      Json columns = Json::array();
      for (const auto index : chart.columns) {
        columns.push_back(chart.tokens[index]);
      }
      node["columns"] = columns;
      if (tree.parent[slot] == pav.nodes.size()) {
        node["parent"] = nullptr;
      } else {
        const auto &parent = chart_of.at(pav.nodes[tree.parent[slot]]);
        node["parent"] = {{"chart", parent.relative},
                          {"action", parent.tokens[tree.parent_action[slot]]}};
      }
      Json next = Json::object();
      for (std::size_t action = 0; action < chart.tokens.size(); ++action) {
        Json children = Json::array();
        for (const auto child : tree.next[slot][action]) {
          children.push_back(chart_name(child));
        }
        next[chart.tokens[action]] = children;
      }
      node["next"] = next;
      if (options.combo_values) {
        node["opponent_reach"] = vector_json(pav.opponent_reach[slot]);
        Json combo_values = Json::object();
        for (std::size_t action = 0; action < chart.tokens.size(); ++action) {
          combo_values[chart.tokens[action]] = vector_json(pav.combo_values[slot][action]);
        }
        node["combo_values"] = combo_values;
      }
      Json strategy = Json::object();
      Json class_ev = Json::object();
      Json class_weight_json = Json::object();
      for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
        const auto &label = class_labels[hand_class];
        Json row = Json::object();
        Json ev_row = Json::object();
        for (std::size_t action = 0; action < chart.tokens.size(); ++action) {
          row[chart.tokens[action]] = ours[hero][slot][hand_class][action];
          ev_row[chart.tokens[action]] = {
              {"ev", pav.class_ev[slot][action][hand_class]},
              {"se", sampled ? Json(pav.class_se[slot][action][hand_class]) : Json()}};
        }
        strategy[label] = row;
        class_ev[label] = ev_row;
        class_weight_json[label] = pav.class_weight[slot][hand_class];
      }
      node["strategy"] = strategy;
      node["class_ev"] = class_ev;
      node["class_weight"] = class_weight_json;
      nodes.push_back(node);
    }
    hero_json["nodes"] = nodes;
    heroes.push_back(hero_json);

    const auto &response = responses[hero];
    Json choices = Json::object();
    for (std::size_t slot = 0; slot < pav.nodes.size(); ++slot) {
      const auto &chart = chart_of.at(pav.nodes[slot]);
      Json per_class_json = Json::object();
      for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
        per_class_json[class_labels[hand_class]] = chart.tokens[response.choice[slot][hand_class]];
      }
      choices[chart.relative] = per_class_json;
    }
    responses_json.push_back({{"hero", hero},
                              {"position", pb::position_name(game, static_cast<std::uint8_t>(hero))},
                              {"gain_per_class_antes", response.gain_per_class},
                              {"gain_per_combo_antes", response.gain_per_combo},
                              {"gain_standard_error_antes", sampled ? Json(gain_se[hero]) : Json(nullptr)},
                              {"pot_percent", percent(response.gain_per_class)},
                              {"choices", choices}});
    std::cout << "preflop response " << pb::position_name(game, static_cast<std::uint8_t>(hero))
              << ": EV " << ev[hero] << " antes" << (sampled ? " +- " + std::to_string(ev_se[hero]) : "")
              << ", gain per class " << response.gain_per_class << " antes ("
              << percent(response.gain_per_class) << " % of the pot), per combo "
              << response.gain_per_combo
              << (sampled ? ", se " + std::to_string(gain_se[hero]) : "") << '\n';
  }
  json["heroes"] = heroes;
  json["preflop_response"] = responses_json;
  std::cout << "expected rake " << expected_rake << " antes; sum of the direct EVs "
            << ev_direct_sum << " (residual " << ev_direct_sum + expected_rake
            << (identity_exact ? ", exact per board"
                               : ", in expectation only (class_cache, sampled), se " +
                                     std::to_string(identity_se))
            << "), pooled " << ev_sum << '\n';

  // Chart sets played inside our game, every seat.
  Json chart_sets = Json::array();
  for (const auto &set : options.charts) {
    Json players_json = Json::array();
    for (std::size_t hero = 0; hero < players; ++hero) {
      const auto &pav = action_values[hero];
      const auto &tree = trees[hero];
      std::vector<std::optional<mc::ChartFile>> files(pav.nodes.size());
      const mc::ChartLookup lookup = [&](const std::size_t slot, const std::size_t hand_class) {
        const auto &chart = chart_of.at(pav.nodes[slot]);
        const auto path = set.directory / chart.position / chart.name;
        if (!files[slot]) {
          files[slot] = mc::read_chart(path);
        }
        return mc::chart_row(chart, *files[slot], class_labels[hand_class], path);
      };
      const auto charts = mc::chart_strategy(tree, ours[hero], lookup);
      const auto loss = mc::chart_loss(tree, pav, ours[hero], charts);
      check(std::abs(loss.loss - loss.loss_recursive) <= check_tolerance,
            "chart set " + set.name + ": loss by nodes equals the recursion for hero " +
                std::to_string(hero));
      double loss_se = 0.0;
      if (sampled && values.series.size() > 1U) {
        std::vector<std::vector<double>> losses;
        std::vector<const std::vector<double> *> live;
        for (const auto &chunk : values.series) {
          const ChunkView view{&values.heroes[hero], &chunk.heroes[hero]};
          auto class_ours = strategy_class_values(tree, view, ours[hero], ours[hero]);
          const auto class_charts = strategy_class_values(tree, view, ours[hero], charts.rows);
          for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
            class_ours[hand_class] -= class_charts[hand_class];
          }
          losses.push_back(std::move(class_ours));
          live.push_back(&chunk.live);
        }
        loss_se = pooled_standard_error(losses, live, class_weight, class_combos);
      }
      Json nodes = Json::array();
      Json fallback_rows = Json::array();
      for (std::size_t slot = 0; slot < pav.nodes.size(); ++slot) {
        const auto &term = loss.nodes[slot];
        Json largest = Json::array();
        std::vector<std::size_t> order(class_count);
        for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
          order[hand_class] = hand_class;
          if (term.classes[hand_class].source == mc::RowSource::Fallback) {
            fallback_rows.push_back(chart_of.at(pav.nodes[slot]).relative + ":" +
                                    class_labels[hand_class]);
          }
        }
        std::sort(order.begin(), order.end(), [&](const std::size_t left, const std::size_t right) {
          return term.classes[left].loss > term.classes[right].loss;
        });
        for (std::size_t rank = 0; rank < std::min<std::size_t>(8U, order.size()); ++rank) {
          const auto &entry = term.classes[order[rank]];
          if (entry.loss <= 0.0) {
            break;
          }
          largest.push_back({{"class", class_labels[order[rank]]},
                             {"loss_antes", entry.loss},
                             {"local_loss_antes", entry.local_loss},
                             {"reach_ours", entry.reach_ours},
                             {"reach_charts", entry.reach_charts},
                             {"source", mc::row_source_name(entry.source)}});
        }
        nodes.push_back({{"chart", chart_of.at(pav.nodes[slot]).relative},
                         {"loss_antes", term.loss},
                         {"reach_combos_ours", term.reach_combos_ours},
                         {"reach_combos_charts", term.reach_combos_charts},
                         {"largest", largest}});
      }
      players_json.push_back({{"hero", hero},
                              {"position", pb::position_name(game, static_cast<std::uint8_t>(hero))},
                              {"loss_antes", loss.loss},
                              {"loss_pot_percent", percent(loss.loss)},
                              {"loss_standard_error_antes", sampled ? Json(loss_se) : Json(nullptr)},
                              {"loss_recursive_antes", loss.loss_recursive},
                              {"loss_positive_antes", loss.positive},
                              {"loss_negative_antes", loss.negative},
                              {"within_target", loss.loss <= target_antes},
                              {"fallback_reach_combos", loss.fallback_reach_combos},
                              {"fallback_rows", fallback_rows},
                              {"nodes", nodes}});
      std::cout << "charts " << set.name << " played by "
                << pb::position_name(game, static_cast<std::uint8_t>(hero)) << ": loss "
                << loss.loss << " antes (" << percent(loss.loss) << " % of the pot)"
                << (sampled ? " +- " + std::to_string(loss_se) : "") << ", fallback reach "
                << loss.fallback_reach_combos << " combos\n";
    }
    chart_sets.push_back({{"name", set.name},
                          {"directory", set.directory.generic_string()},
                          {"players", players_json}});
  }
  json["chart_sets"] = chart_sets;
  json["checks_failed"] = failures;
  json["total_seconds"] = std::chrono::duration<double>(Clock::now() - started).count();

  bool written = true;
  if (!options.output_path.empty()) {
    written = write_output(options.output_path, json.dump(1) + "\n") && written;
  }
  if (!options.series_path.empty()) {
    // The chunk series per class (the standard-error inputs), for the Python side.
    Json series = Json::array();
    for (const auto &chunk : values.series) {
      Json record{{"index", chunk.index},
                  {"boards", chunk.boards},
                  {"weight", chunk.weight},
                  {"rake", chunk.rake},
                  {"live", vector_json(chunk.live)}};
      Json chunk_heroes = Json::array();
      for (std::size_t hero = 0; hero < players; ++hero) {
        const auto &entry = chunk.heroes[hero];
        const auto &totals = values.heroes[hero];
        Json slots = Json::array();
        for (std::size_t slot = 0; slot < totals.nodes.size(); ++slot) {
          Json per_action = Json::array();
          for (std::size_t action = 0; action < totals.actions[slot]; ++action) {
            std::vector<double> row(entry.values.begin() + static_cast<std::ptrdiff_t>(
                                                               totals.class_offset[slot] +
                                                               action * class_count),
                                    entry.values.begin() + static_cast<std::ptrdiff_t>(
                                                               totals.class_offset[slot] +
                                                               (action + 1U) * class_count));
            per_action.push_back(vector_json(row));
          }
          std::vector<double> reach(entry.reach.begin() + static_cast<std::ptrdiff_t>(slot * class_count),
                                    entry.reach.begin() +
                                        static_cast<std::ptrdiff_t>((slot + 1U) * class_count));
          slots.push_back({{"chart", chart_of.at(totals.nodes[slot]).relative},
                           {"values", per_action},
                           {"reach", vector_json(reach)}});
        }
        chunk_heroes.push_back({{"hero", hero},
                                {"ev_direct", entry.ev_direct},
                                {"root", vector_json(entry.root)},
                                {"nodes", slots}});
      }
      record["heroes"] = chunk_heroes;
      series.push_back(record);
    }
    Json series_json{{"schema", "gtosd.preflop_blueprint_policy_values_series.v1"},
                     {"identity", values.identity},
                     {"classes", class_labels},
                     {"chunks", series}};
    written = write_output(options.series_path, series_json.dump() + "\n") && written;
  }
  for (const auto &failure : failures) {
    std::cerr << "CHECK FAILED: " << failure << '\n';
  }
  if (!written) {
    throw std::runtime_error("an output file could not be written");
  }
  if (options.check && !failures.empty()) {
    std::cout << "PREFLOP_BLUEPRINT_POLICY_VALUES=FAIL checks " << failures.size() << '\n';
    return 1;
  }
  std::cout << "PREFLOP_BLUEPRINT_POLICY_VALUES=" << (failures.empty() ? "PASS" : "WARN")
            << " boards " << values.boards << " seconds " << values.seconds << '\n';
  return 0;
}

} // namespace

int main(const int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_POLICY_VALUES=FAIL " << error.what() << '\n';
    return 1;
  }
}
