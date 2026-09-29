// Preflop lock of the trainer (TrainerResources::preflop_lock) and its chart
// helper (benchmarks/monker_chart_lock.hpp): locked rows are the chart rows in
// the current and average strategy and in every export, bit for bit; reaches
// flow through the lock; no lock leaves the identity unchanged; a checkpoint
// cannot resume with another lock; invalid locks are refused.
#include "preflop_blueprint_test_support.hpp"

#include "../benchmarks/monker_chart_lock.hpp"

#include "gtosd/preflop_blueprint/policy_file.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace pb_test;
namespace mc = gtosd::monker_charts;

std::filesystem::path scratch_directory() {
  const auto directory =
      std::filesystem::temp_directory_path() / "gtosd_preflop_blueprint_lock_tests";
  std::filesystem::create_directories(directory);
  return directory;
}

pb::TrainerConfig lock_config(const Resources &resources, const pb::TableStorage storage) {
  auto config = resources.config();
  config.threads = 2U;
  config.batch_boards = 8U;
  config.batch_policy_refresh = true;
  config.scheme = pb::WeightingScheme::Dcfr;
  config.update_mode = pb::UpdateMode::Alternating;
  config.lazy_discount = true;
  config.storage = storage;
  return config;
}

std::vector<std::string> class_labels() {
  std::vector<std::string> labels(mc::class_count);
  for (const auto &[hand_class, label] : mc::hand_classes().label_by_class)
    labels.at(hand_class) = label;
  return labels;
}

// Chart of `node` with class-distinct 3-decimal rows summing to 1.000: two
// actions per class share the mass, some classes play one action only.
void write_distinct_chart(const pb::CompiledGame &game, const mc::ChartNode &chart,
                          const std::filesystem::path &directory) {
  const auto actions = static_cast<std::uint32_t>(chart.tokens.size());
  std::vector<std::string> labels;
  for (const auto &entry : mc::hand_classes().combos_by_label)
    labels.push_back(entry.first);
  std::map<std::string, std::vector<double>> rows;
  for (std::uint32_t index = 0; index < labels.size(); ++index) {
    std::vector<std::uint32_t> thousandths(actions, 0U);
    const auto primary = index % actions;
    const auto secondary = (index / actions + primary + 1U) % actions;
    const std::uint32_t moved = (index * 53U) % 1001U;
    thousandths[primary] += 1000U - moved;
    thousandths[secondary] += moved;
    std::vector<double> values(actions);
    for (std::uint32_t action = 0; action < actions; ++action)
      values[action] = static_cast<double>(thousandths[action]) / 1000.0;
    rows[labels[index]] = values;
  }
  const mc::ClassStrategy strategy =
      [&](const std::uint32_t node,
          const std::string &label) -> std::optional<std::vector<double>> {
    if (node != chart.node)
      return std::nullopt;
    return rows.at(label);
  };
  mc::write_charts(game, labels, strategy, directory);
}

std::vector<double> locked_frequencies(const pb::PreflopLock &lock, const std::uint32_t node,
                                       const std::uint8_t hand_class) {
  for (const auto &row : lock.rows)
    if (row.node == node && row.hand_class == hand_class)
      return row.frequencies;
  throw std::runtime_error("row not locked");
}

bool same_bits(const double *left, const double *right, const std::size_t count) {
  for (std::size_t index = 0; index < count; ++index)
    if (std::bit_cast<std::uint64_t>(left[index]) != std::bit_cast<std::uint64_t>(right[index]))
      return false;
  return true;
}

std::string file_bytes(const std::filesystem::path &path) { return read_file(path); }

const mc::ChartNode &root_chart(const std::vector<mc::ChartNode> &charts,
                                const pb::CompiledGame &game) {
  for (const auto &chart : charts)
    if (chart.node == game.root())
      return chart;
  throw std::runtime_error("root chart missing");
}

// T1: locked root rows equal the chart rows in every reader, for every storage.
void test_locked_rows_exact(const Resources &resources) {
  const auto game =
      pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "lock fixture compiles");
  const auto charts = mc::chart_nodes(game.value());
  const auto &root = root_chart(charts, game.value());
  const auto directory = scratch_directory() / "distinct";
  std::filesystem::remove_all(directory);
  write_distinct_chart(game.value(), root, directory);
  const auto chart_lock = mc::chart_lock(game.value(), directory, {root.relative});
  require(chart_lock.chart_rows == mc::class_count && chart_lock.outside_range_rows == 0U,
          "the root chart locks every class");
  const auto labels = class_labels();
  const auto file = mc::read_chart(directory / root.position / root.name);
  const auto actions = game.value().nodes()[root.node].action_count;
  for (std::uint8_t hand_class = 0; hand_class < mc::class_count; ++hand_class) {
    const auto expected = mc::chart_row(root, file, labels[hand_class], root.relative);
    require(expected.has_value() &&
                same_bits(expected->data(),
                          locked_frequencies(chart_lock.lock, root.node, hand_class).data(),
                          actions),
            "lock rows are the chart_row rows of their class id");
  }
  for (const auto storage :
       {pb::TableStorage::Double, pb::TableStorage::MixedFloatSums, pb::TableStorage::Float32}) {
    auto view = resources.view();
    view.preflop_lock = &chart_lock.lock;
    auto created = pb::Trainer::create(game.value(), view, lock_config(resources, storage));
    require(created.has_value(), "locked trainer creates");
    auto &trainer = *created.value();
    for (int iteration = 0; iteration < 6; ++iteration)
      require(trainer.iterate().has_value(), "locked iteration succeeds");
    const auto name = std::string(pb::table_storage_name(storage));
    // Snapshot before the save (the save materializes the pending discounts).
    const auto snapshot_path = scratch_directory() / ("snapshot_" + name + ".bin");
    const auto saved_path = scratch_directory() / ("saved_" + name + ".bin");
    std::vector<double> row(actions);
    for (std::uint8_t hand_class = 0; hand_class < mc::class_count; ++hand_class) {
      trainer.average_strategy_row(root.node, hand_class, row.data());
      require(same_bits(row.data(),
                        locked_frequencies(chart_lock.lock, root.node, hand_class).data(), actions),
              "average_strategy_row of a locked row is the chart row");
    }
    require(trainer.save_average_policy_snapshot(snapshot_path, "lock-test").has_value() &&
                trainer.save_average_policy(saved_path, "lock-test").has_value(),
            "locked policies save");
    require(file_bytes(snapshot_path) == file_bytes(saved_path),
            "the snapshot has the bytes of the saved policy");
    const auto average = trainer.average_policy();
    const auto current = trainer.current_policy();
    const auto loaded = pb::load_policy(saved_path, game.value());
    require(loaded.has_value(), "locked policy loads");
    for (std::uint8_t hand_class = 0; hand_class < mc::class_count; ++hand_class) {
      const auto expected = locked_frequencies(chart_lock.lock, root.node, hand_class);
      require(same_bits(average.row(root.node, hand_class).data(), expected.data(), actions) &&
                  same_bits(current.row(root.node, hand_class).data(), expected.data(), actions) &&
                  same_bits(loaded.value()->row(root.node, hand_class).data(), expected.data(),
                            actions),
              "average, current and saved rows of the locked root are the chart rows");
    }
    const auto base = trainer.layout().offsets[root.node];
    bool zero = true;
    for (std::uint64_t cell = base; cell < base + 81U * actions; ++cell)
      zero = zero && trainer.regret(cell) == 0.0 && trainer.strategy_sum(cell) == 0.0;
    require(zero, "locked rows never accumulate regrets or strategy sums");
    // Other nodes train: some regret outside the root is nonzero.
    bool trained = false;
    for (std::uint64_t cell = 0; cell < trainer.cell_count() && !trained; ++cell)
      trained = (cell < base || cell >= base + 81U * actions) && trainer.regret(cell) != 0.0;
    require(trained, "the unlocked nodes train");
    // The chart export of the locked node reproduces the input chart.
    const auto exported = scratch_directory() / ("exported_" + name);
    std::filesystem::remove_all(exported);
    mc::write_row_charts(
        game.value(),
        [&](const std::uint32_t node, const std::uint8_t hand_class) {
          std::vector<double> out(game.value().nodes()[node].action_count);
          trainer.average_strategy_row(node, hand_class, out.data());
          return out;
        },
        exported);
    const auto written = mc::read_chart(exported / root.position / root.name);
    require(written.columns == file.columns && written.rows == file.rows,
            "the exported chart of the locked root equals the input chart");
    std::cout << "locked rows exact (" << name << "): identity " << trainer.identity() << " lock "
              << trainer.preflop_lock_fingerprint() << '\n';
  }
}

pb::PreflopLock pure_root_lock(const pb::CompiledGame &game, const gtosd::ActionType type) {
  const auto root = game.root();
  const auto edges = game.edges_of(root);
  std::vector<double> frequencies(edges.size(), 0.0);
  bool found = false;
  for (std::size_t edge = 0; edge < edges.size(); ++edge) {
    if (edges[edge].action.type == type) {
      frequencies[edge] = 1.0;
      found = true;
      break;
    }
  }
  require(found, "root action found");
  pb::PreflopLock lock;
  for (std::uint8_t hand_class = 0; hand_class < mc::class_count; ++hand_class)
    lock.rows.push_back({root, hand_class, frequencies});
  return lock;
}

// T3: the reach of both players flows through the lock. A root locked to Fold
// ends every hand: no cell of either player is ever written.
void test_reach_flows_through_lock(const Resources &resources) {
  const auto game =
      pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "lock fixture compiles");
  const auto lock = pure_root_lock(game.value(), gtosd::ActionType::Fold);
  auto view = resources.view();
  view.preflop_lock = &lock;
  auto created =
      pb::Trainer::create(game.value(), view, lock_config(resources, pb::TableStorage::Double));
  require(created.has_value(), "fold-locked trainer creates");
  for (int iteration = 0; iteration < 3; ++iteration)
    require(created.value()->iterate().has_value(), "fold-locked iteration succeeds");
  // The opponent's regrets are weighted by the locked player's reach (zero behind
  // the fold); the locked player's own later nodes have zero own reach, so no
  // strategy sum. (Its later regrets are weighted by the opponent's reach and train.)
  const auto &trainer = *created.value();
  const auto &nodes = game.value().nodes();
  const auto locked_actor = nodes[game.value().root()].actor;
  bool opponent_regrets_zero = true;
  bool own_sums_zero = true;
  bool own_regrets_trained = false;
  for (const auto &node : nodes) {
    if (node.kind != pb::NodeKind::Decision)
      continue;
    const auto rows =
        pb::StateLayout::rows_for(node.street, trainer.config().flop_capacity,
                                  trainer.config().turn_capacity, trainer.config().river_capacity);
    const auto base = trainer.layout().offsets[node.id];
    for (std::uint64_t cell = base; cell < base + std::uint64_t{rows} * node.action_count; ++cell) {
      if (node.actor != locked_actor)
        opponent_regrets_zero = opponent_regrets_zero && trainer.regret(cell) == 0.0;
      else
        own_sums_zero = own_sums_zero && trainer.strategy_sum(cell) == 0.0;
      if (node.actor == locked_actor && node.id != game.value().root())
        own_regrets_trained = own_regrets_trained || trainer.regret(cell) != 0.0;
    }
  }
  require(opponent_regrets_zero, "behind a root locked to fold the opponent never updates regrets");
  require(own_sums_zero, "behind a root locked to fold the locked player accumulates nothing");
  require(own_regrets_trained, "the locked player's later nodes still update regrets");
}

// T4 and T5: identity without a lock, resume refusals, resume equivalence.
void test_identity_and_resume(const Resources &resources) {
  const auto game =
      pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "lock fixture compiles");
  const auto config = lock_config(resources, pb::TableStorage::Double);
  const pb::PreflopLock empty;
  // Every class plays the root uniformly (both players keep training below it).
  pb::PreflopLock first;
  {
    const auto actions = game.value().nodes()[game.value().root()].action_count;
    std::vector<double> uniform(actions, 1.0 / actions);
    uniform.back() = 1.0 - (actions - 1U) * (1.0 / actions);
    for (std::uint8_t hand_class = 0; hand_class < mc::class_count; ++hand_class)
      first.rows.push_back({game.value().root(), hand_class, uniform});
  }
  auto second = first;
  second.rows[5].frequencies.assign(second.rows[5].frequencies.size(), 0.0);
  second.rows[5].frequencies[0] = 1.0;
  const auto make = [&](const pb::PreflopLock *lock) {
    auto view = resources.view();
    view.preflop_lock = lock;
    auto created = pb::Trainer::create(game.value(), view, config);
    require(created.has_value(), "identity trainer creates");
    return std::move(created.value());
  };
  auto plain = make(nullptr);
  auto with_empty = make(&empty);
  auto locked = make(&first);
  require(plain->identity() == with_empty->identity() &&
              plain->preflop_lock_fingerprint().empty() &&
              with_empty->preflop_lock_fingerprint().empty(),
          "an empty lock is no lock");
  require(locked->identity() != plain->identity() && !locked->preflop_lock_fingerprint().empty() &&
              locked->preflop_locked_rows() == mc::class_count,
          "a lock changes the identity");
  for (int iteration = 0; iteration < 3; ++iteration)
    require(plain->iterate().has_value() && with_empty->iterate().has_value() &&
                locked->iterate().has_value(),
            "identity iterations succeed");
  require(plain->state_fingerprint() == with_empty->state_fingerprint(),
          "no lock and an empty lock train bit-identically");
  const auto locked_path = scratch_directory() / "locked.ckpt";
  const auto plain_path = scratch_directory() / "plain.ckpt";
  require(locked->save_checkpoint(locked_path).has_value() &&
              plain->save_checkpoint(plain_path).has_value(),
          "lock checkpoints save");
  for (const auto *lock : {static_cast<const pb::PreflopLock *>(&second),
                           static_cast<const pb::PreflopLock *>(nullptr)}) {
    auto other = make(lock);
    const auto loaded = other->load_checkpoint(locked_path);
    require(!loaded.has_value() && loaded.error() == pb::TrainerError::IntegrityFailure,
            "a locked checkpoint does not resume with another lock or without one");
  }
  {
    auto other = make(&first);
    const auto loaded = other->load_checkpoint(plain_path);
    require(!loaded.has_value() && loaded.error() == pb::TrainerError::IntegrityFailure,
            "an unlocked checkpoint does not resume with a lock");
  }
  auto resumed = make(&first);
  require(resumed->load_checkpoint(locked_path).has_value(), "same lock resumes");
  // The continuous run also saves at iteration 3: a save materializes the lazy
  // discounts, which changes the rounding of the later discount products.
  auto continuous = make(&first);
  for (int iteration = 0; iteration < 6; ++iteration) {
    require(continuous->iterate().has_value(), "continuous iteration succeeds");
    if (iteration == 2)
      require(continuous->save_checkpoint(scratch_directory() / "continuous.ckpt").has_value(),
              "continuous checkpoint saves");
  }
  for (int iteration = 0; iteration < 3; ++iteration)
    require(resumed->iterate().has_value(), "resumed iteration succeeds");
  require(resumed->state_fingerprint() == continuous->state_fingerprint(),
          "3 + 3 locked iterations with a resume equal 6 continuous ones");
}

// T6: invalid locks.
void test_invalid_locks(const Resources &resources) {
  const auto game =
      pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "lock fixture compiles");
  const auto config = lock_config(resources, pb::TableStorage::Double);
  const auto root = game.value().root();
  const auto actions = game.value().nodes()[root].action_count;
  std::uint32_t postflop = 0U;
  for (const auto &node : game.value().nodes())
    if (node.kind == pb::NodeKind::Decision && node.street != gtosd::Street::Preflop) {
      postflop = node.id;
      break;
    }
  require(postflop != 0U, "a postflop decision exists");
  std::vector<double> uniform(actions, 1.0 / actions);
  uniform.back() = 1.0 - (actions - 1U) * (1.0 / actions);
  std::vector<pb::PreflopLock> invalid(7U);
  invalid[0].rows.push_back(
      {postflop, 0U, std::vector<double>(game.value().nodes()[postflop].action_count, 0.0)});
  invalid[0].rows[0].frequencies[0] = 1.0;
  invalid[1].rows.push_back({root, 81U, uniform});
  invalid[2].rows.push_back({root, 0U, std::vector<double>(actions + 1U, 1.0 / (actions + 1U))});
  auto negative = uniform;
  negative[0] = -0.1;
  negative[1] += 0.1;
  invalid[3].rows.push_back({root, 0U, negative});
  auto short_sum = uniform;
  short_sum[0] -= 0.1;
  invalid[4].rows.push_back({root, 0U, short_sum});
  auto not_a_number = uniform;
  not_a_number[0] = std::numeric_limits<double>::quiet_NaN();
  invalid[5].rows.push_back({root, 0U, not_a_number});
  invalid[6].rows.push_back({root, 3U, uniform});
  invalid[6].rows.push_back({root, 3U, uniform});
  for (const auto &lock : invalid) {
    auto view = resources.view();
    view.preflop_lock = &lock;
    const auto created = pb::Trainer::create(game.value(), view, config);
    require(!created.has_value() && created.error() == pb::TrainerError::InvalidConfiguration,
            "an invalid lock is refused");
  }
}

// T7 and T8: the helper on charts exported by the trainer itself, and locking
// the root to the trainer's own average changes the rest of the strategy little.
void test_self_charts(const Resources &resources) {
  const auto game =
      pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "lock fixture compiles");
  const auto config = lock_config(resources, pb::TableStorage::Double);
  constexpr int iterations = 8;
  auto source = pb::Trainer::create(game.value(), resources.view(), config);
  require(source.has_value(), "source trainer creates");
  for (int iteration = 0; iteration < iterations; ++iteration)
    require(source.value()->iterate().has_value(), "source iteration succeeds");
  const auto directory = scratch_directory() / "self";
  std::filesystem::remove_all(directory);
  const auto row_of = [&](const pb::Trainer &trainer) {
    return [&trainer, &game](const std::uint32_t node, const std::uint8_t hand_class) {
      std::vector<double> out(game.value().nodes()[node].action_count);
      trainer.average_strategy_row(node, hand_class, out.data());
      return out;
    };
  };
  mc::write_row_charts(game.value(), row_of(*source.value()), directory);
  const auto charts = mc::chart_nodes(game.value());
  const auto &root = root_chart(charts, game.value());
  bool unknown = false;
  try {
    (void)mc::chart_lock(game.value(), directory, {"XX/XX_strategy.txt"});
  } catch (const std::runtime_error &) {
    unknown = true;
  }
  require(unknown, "an unknown chart name is refused");
  const auto all = mc::chart_lock(game.value(), directory, {"all"});
  require(all.files.size() == charts.size() &&
              all.chart_rows + all.outside_range_rows == charts.size() * mc::class_count,
          "every chart row is locked or outside the range");
  std::cout << "self charts: " << charts.size() << " nodes, " << all.chart_rows << " chart rows, "
            << all.outside_range_rows << " outside the range\n";
  const auto lock = mc::chart_lock(game.value(), directory, {root.relative});
  auto view = resources.view();
  view.preflop_lock = &lock.lock;
  auto locked = pb::Trainer::create(game.value(), view, config);
  require(locked.has_value(), "self-locked trainer creates");
  for (int iteration = 0; iteration < iterations; ++iteration)
    require(locked.value()->iterate().has_value(), "self-locked iteration succeeds");
  const auto file = mc::read_chart(directory / root.position / root.name);
  const auto labels = class_labels();
  const auto actions = game.value().nodes()[root.node].action_count;
  std::vector<double> row(actions);
  for (std::uint8_t hand_class = 0; hand_class < mc::class_count; ++hand_class) {
    const auto expected = mc::chart_row(root, file, labels[hand_class], root.relative);
    locked.value()->average_strategy_row(root.node, hand_class, row.data());
    require(expected.has_value() && same_bits(row.data(), expected->data(), actions),
            "the self-locked root is the chart row");
  }
  // Mean and maximum |difference| of the other preflop rows from the unlocked source.
  const auto difference = [&](const pb::Trainer &other) {
    double maximum = 0.0;
    double total = 0.0;
    std::size_t count = 0U;
    for (const auto &chart : charts) {
      if (chart.node == root.node)
        continue;
      const auto node_actions = game.value().nodes()[chart.node].action_count;
      std::vector<double> left(node_actions);
      std::vector<double> right(node_actions);
      for (std::uint8_t hand_class = 0; hand_class < mc::class_count; ++hand_class) {
        source.value()->average_strategy_row(chart.node, hand_class, left.data());
        other.average_strategy_row(chart.node, hand_class, right.data());
        for (std::size_t action = 0; action < node_actions; ++action) {
          const double gap = std::abs(left[action] - right[action]);
          maximum = std::max(maximum, gap);
          total += gap;
          ++count;
        }
      }
    }
    return std::array<double, 2>{count > 0U ? total / static_cast<double>(count) : 0.0, maximum};
  };
  // Reference: the root locked to the uniform row for every class.
  pb::PreflopLock uniform_lock;
  std::vector<double> uniform(actions, 1.0 / actions);
  uniform.back() = 1.0 - (actions - 1U) * (1.0 / actions);
  for (std::uint8_t hand_class = 0; hand_class < mc::class_count; ++hand_class)
    uniform_lock.rows.push_back({root.node, hand_class, uniform});
  auto uniform_view = resources.view();
  uniform_view.preflop_lock = &uniform_lock;
  auto reference = pb::Trainer::create(game.value(), uniform_view, config);
  require(reference.has_value(), "uniform-locked trainer creates");
  for (int iteration = 0; iteration < iterations; ++iteration)
    require(reference.value()->iterate().has_value(), "uniform-locked iteration succeeds");
  const auto self = difference(*locked.value());
  const auto other = difference(*reference.value());
  std::cout << "after " << iterations << " iterations, other preflop rows vs the unlocked run: "
            << "self lock mean |difference| " << self[0] << " max " << self[1]
            << "; uniform root lock mean " << other[0] << " max " << other[1] << '\n';
  require(self[0] < 0.2 && self[0] < other[0],
          "locking the root to the trainer's own charts changes little");
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path resources_dir;
    std::filesystem::path buckets_dir;
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string_view name = argv[index];
      if (name == "--resources-dir")
        resources_dir = argv[index + 1];
      else if (name == "--buckets-dir")
        buckets_dir = argv[index + 1];
      else
        throw std::runtime_error("unknown argument " + std::string{name});
    }
    const auto resources = load_resources(resources_dir, buckets_dir);
    test_invalid_locks(resources);
    test_identity_and_resume(resources);
    test_reach_flows_through_lock(resources);
    test_locked_rows_exact(resources);
    test_self_charts(resources);
    std::cout << "PREFLOP_LOCK_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_LOCK_TESTS=FAIL " << error.what() << '\n';
    return 1;
  }
}
