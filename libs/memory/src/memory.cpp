#include "gtosd/memory/memory.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
// clang-format off
#include <Windows.h>
#include <Psapi.h>
// clang-format on
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace gtosd {
namespace {

constexpr std::uint64_t compact_public_node_bytes = 32U;
constexpr std::uint64_t compact_public_edge_bytes = 24U;
constexpr std::uint64_t sparse_infoset_index_bytes = 24U;
constexpr std::uint64_t action_descriptor_bytes = 4U;
constexpr std::uint64_t scalar_bytes = sizeof(double);
constexpr std::uint64_t best_response_entry_bytes = 16U;
constexpr std::uint64_t physical_flop_count = 7'140U;
constexpr std::array<std::uint64_t, 3> private_combos_by_street{528U, 496U, 465U};
constexpr std::array<char, 8> backing_magic{'G', 'T', 'S', 'D', 'M', 'E', 'M', '1'};

struct FlatInformationSet {
  std::string key;
  std::uint8_t player{0};
  std::vector<GameActionId> actions;
  std::vector<double> regrets;
  std::vector<double> strategy;
};

bool checked_add(std::uint64_t &target, const std::uint64_t value) {
  if (target > std::numeric_limits<std::uint64_t>::max() - value) {
    return false;
  }
  target += value;
  return true;
}

bool checked_multiply(const std::uint64_t left, const std::uint64_t right, std::uint64_t &result) {
  if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return false;
  }
  result = left * right;
  return true;
}

bool add_product(std::uint64_t &target, const std::uint64_t left, const std::uint64_t right) {
  std::uint64_t product = 0;
  return checked_multiply(left, right, product) && checked_add(target, product);
}

std::uint64_t fnv1a(const std::string &text) {
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  for (const char character : text) {
    hash ^= static_cast<std::uint8_t>(character);
    hash *= 1'099'511'628'211ULL;
  }
  return hash;
}

template <typename Value>
bool write_value(std::ofstream &output, const Value &value, std::uint64_t &bytes) {
  output.write(reinterpret_cast<const char *>(&value), sizeof(Value));
  return static_cast<bool>(output) && checked_add(bytes, sizeof(Value));
}

template <typename Value>
bool read_value(std::ifstream &input, Value &value, std::uint64_t &bytes) {
  input.read(reinterpret_cast<char *>(&value), sizeof(Value));
  return static_cast<bool>(input) && checked_add(bytes, sizeof(Value));
}

bool valid_options(const MemoryPrototypeOptions &options) {
  return options.page_size_bytes >= 4'096U &&
         options.page_size_bytes <= 16ULL * 1'024ULL * 1'024ULL &&
         std::has_single_bit(options.page_size_bytes) && options.resident_page_count > 0U &&
         options.resident_page_count <= 1'048'576U;
}

std::uint64_t measured_peak_rss() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  if (GetProcessMemoryInfo(GetCurrentProcess(),
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                           sizeof(counters)) == 0) {
    return 0;
  }
  return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
#else
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return 0;
  }
#ifdef __APPLE__
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1'024U;
#endif
#endif
}

class MappedBacking {
public:
  MappedBacking(const std::string &path, const std::uint64_t size) : size_(size) {
#ifdef _WIN32
    file_ = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_ == INVALID_HANDLE_VALUE) {
      return;
    }
    mapping_ = CreateFileMappingA(file_, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    if (mapping_ == nullptr) {
      return;
    }
    data_ = static_cast<char *>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, 0));
#else
    file_ = open(path.c_str(), O_RDWR);
    if (file_ < 0) {
      return;
    }
    void *const mapped = mmap(nullptr, static_cast<std::size_t>(size_), PROT_READ | PROT_WRITE,
                              MAP_SHARED, file_, 0);
    if (mapped != MAP_FAILED) {
      data_ = static_cast<char *>(mapped);
    }
#endif
  }

  MappedBacking(const MappedBacking &) = delete;
  MappedBacking &operator=(const MappedBacking &) = delete;

  ~MappedBacking() {
#ifdef _WIN32
    if (data_ != nullptr) {
      UnmapViewOfFile(data_);
    }
    if (mapping_ != nullptr) {
      CloseHandle(mapping_);
    }
    if (file_ != INVALID_HANDLE_VALUE) {
      CloseHandle(file_);
    }
#else
    if (data_ != nullptr) {
      munmap(data_, static_cast<std::size_t>(size_));
    }
    if (file_ >= 0) {
      close(file_);
    }
#endif
  }

  [[nodiscard]] bool valid() const noexcept { return data_ != nullptr; }
  [[nodiscard]] char *data() noexcept { return data_; }

  [[nodiscard]] bool flush(const std::uint64_t offset, const std::uint64_t size) {
#ifdef _WIN32
    return FlushViewOfFile(data_ + static_cast<std::size_t>(offset), static_cast<SIZE_T>(size)) !=
           0;
#else
    return msync(data_ + static_cast<std::size_t>(offset), static_cast<std::size_t>(size),
                 MS_SYNC) == 0;
#endif
  }

private:
  std::uint64_t size_{0};
  char *data_{nullptr};
#ifdef _WIN32
  HANDLE file_{INVALID_HANDLE_VALUE};
  HANDLE mapping_{nullptr};
#else
  int file_{-1};
#endif
};

Result<std::vector<FlatInformationSet>, MemoryError>
flatten_checkpoint(const SolverCheckpoint &checkpoint) {
  if (checkpoint.information_sets.empty() || checkpoint.game_fingerprint.empty()) {
    return Result<std::vector<FlatInformationSet>, MemoryError>::failure(
        MemoryError::InvalidCheckpoint);
  }
  std::vector<FlatInformationSet> result;
  result.reserve(checkpoint.information_sets.size());
  for (const auto &[key, buffer] : checkpoint.information_sets) {
    if (key.empty() || buffer.actions.empty() ||
        buffer.actions.size() != buffer.cumulative_regret.size() ||
        buffer.actions.size() != buffer.cumulative_strategy.size()) {
      return Result<std::vector<FlatInformationSet>, MemoryError>::failure(
          MemoryError::InvalidCheckpoint);
    }
    result.push_back(
        {key, buffer.player, buffer.actions, buffer.cumulative_regret, buffer.cumulative_strategy});
  }
  return Result<std::vector<FlatInformationSet>, MemoryError>::success(std::move(result));
}

Result<SolverCheckpoint, MemoryError>
rebuild_checkpoint(const SolverCheckpoint &source,
                   const std::vector<FlatInformationSet> &information_sets) {
  SolverCheckpoint rebuilt = source;
  rebuilt.information_sets.clear();
  for (const auto &entry : information_sets) {
    InformationSetBuffer buffer;
    buffer.player = entry.player;
    buffer.actions = entry.actions;
    buffer.cumulative_regret = entry.regrets;
    buffer.cumulative_strategy = entry.strategy;
    if (!rebuilt.information_sets.emplace(entry.key, std::move(buffer)).second) {
      return Result<SolverCheckpoint, MemoryError>::failure(MemoryError::InvalidCheckpoint);
    }
  }
  const auto valid = serialize_solver_checkpoint(rebuilt);
  if (!valid) {
    return Result<SolverCheckpoint, MemoryError>::failure(MemoryError::InvalidCheckpoint);
  }
  return Result<SolverCheckpoint, MemoryError>::success(std::move(rebuilt));
}

Result<MemoryRoundTripResult, MemoryError> round_trip_in_memory(const SolverCheckpoint &checkpoint,
                                                                const MemoryPrototype prototype) {
  const auto flat = flatten_checkpoint(checkpoint);
  if (!flat) {
    return Result<MemoryRoundTripResult, MemoryError>::failure(flat.error());
  }

  std::array<std::vector<FlatInformationSet>, 3> partitions;
  std::uint64_t encoded_bytes = 0;
  std::uint64_t peak_partition_bytes = 0;
  for (const auto &entry : flat.value()) {
    const auto partition =
        prototype == MemoryPrototype::StreetDecomposition ? fnv1a(entry.key) % 3U : 0U;
    std::uint64_t entry_bytes = sparse_infoset_index_bytes + entry.key.size();
    if (!add_product(entry_bytes, entry.actions.size(),
                     action_descriptor_bytes + 2U * scalar_bytes)) {
      return Result<MemoryRoundTripResult, MemoryError>::failure(MemoryError::ArithmeticOverflow);
    }
    if (!checked_add(encoded_bytes, entry_bytes)) {
      return Result<MemoryRoundTripResult, MemoryError>::failure(MemoryError::ArithmeticOverflow);
    }
    partitions[static_cast<std::size_t>(partition)].push_back(entry);
  }

  std::vector<FlatInformationSet> restored;
  restored.reserve(flat.value().size());
  for (const auto &partition : partitions) {
    std::uint64_t partition_bytes = 0;
    for (const auto &entry : partition) {
      std::uint64_t entry_bytes = sparse_infoset_index_bytes + entry.key.size();
      if (!add_product(entry_bytes, entry.actions.size(),
                       action_descriptor_bytes + 2U * scalar_bytes)) {
        return Result<MemoryRoundTripResult, MemoryError>::failure(MemoryError::ArithmeticOverflow);
      }
      if (!checked_add(partition_bytes, entry_bytes)) {
        return Result<MemoryRoundTripResult, MemoryError>::failure(MemoryError::ArithmeticOverflow);
      }
      restored.push_back(entry);
    }
    peak_partition_bytes = std::max(peak_partition_bytes, partition_bytes);
  }
  std::ranges::sort(restored,
                    [](const auto &left, const auto &right) { return left.key < right.key; });
  const auto rebuilt = rebuild_checkpoint(checkpoint, restored);
  if (!rebuilt) {
    return Result<MemoryRoundTripResult, MemoryError>::failure(rebuilt.error());
  }
  MemoryRoundTripResult result;
  result.checkpoint = rebuilt.value();
  result.encoded_bytes = encoded_bytes;
  result.resident_bytes =
      prototype == MemoryPrototype::StreetDecomposition ? peak_partition_bytes : encoded_bytes;
  return Result<MemoryRoundTripResult, MemoryError>::success(std::move(result));
}

Result<MemoryRoundTripResult, MemoryError>
round_trip_out_of_core(const SolverCheckpoint &checkpoint, const std::string &backing_file,
                       const MemoryPrototypeOptions &options) {
  if (backing_file.empty()) {
    return Result<MemoryRoundTripResult, MemoryError>::failure(MemoryError::InvalidConfiguration);
  }
  const auto flat = flatten_checkpoint(checkpoint);
  if (!flat) {
    return Result<MemoryRoundTripResult, MemoryError>::failure(flat.error());
  }

  std::ofstream output(backing_file, std::ios::binary | std::ios::trunc);
  if (!output) {
    return Result<MemoryRoundTripResult, MemoryError>::failure(MemoryError::IoFailure);
  }
  std::uint64_t bytes_written = 0;
  output.write(backing_magic.data(), static_cast<std::streamsize>(backing_magic.size()));
  if (!output || !checked_add(bytes_written, backing_magic.size())) {
    return Result<MemoryRoundTripResult, MemoryError>::failure(MemoryError::IoFailure);
  }
  const auto count = static_cast<std::uint64_t>(flat.value().size());
  if (!write_value(output, count, bytes_written)) {
    return Result<MemoryRoundTripResult, MemoryError>::failure(MemoryError::IoFailure);
  }
  for (const auto &entry : flat.value()) {
    const auto key_size = static_cast<std::uint64_t>(entry.key.size());
    const auto action_count = static_cast<std::uint64_t>(entry.actions.size());
    if (!write_value(output, key_size, bytes_written) ||
        !write_value(output, entry.player, bytes_written) ||
        !write_value(output, action_count, bytes_written)) {
      return Result<MemoryRoundTripResult, MemoryError>::failure(MemoryError::IoFailure);
    }
    output.write(entry.key.data(), static_cast<std::streamsize>(entry.key.size()));
    if (!output || !checked_add(bytes_written, entry.key.size())) {
      return Result<MemoryRoundTripResult, MemoryError>::failure(MemoryError::IoFailure);
    }
    for (std::size_t index = 0; index < entry.actions.size(); ++index) {
      if (!write_value(output, entry.actions[index], bytes_written) ||
          !write_value(output, entry.regrets[index], bytes_written) ||
          !write_value(output, entry.strategy[index], bytes_written)) {
        return Result<MemoryRoundTripResult, MemoryError>::failure(MemoryError::IoFailure);
      }
    }
  }
  output.flush();
  if (!output) {
    return Result<MemoryRoundTripResult, MemoryError>::failure(MemoryError::IoFailure);
  }
  output.close();

  std::ifstream input(backing_file, std::ios::binary);
  if (!input) {
    return Result<MemoryRoundTripResult, MemoryError>::failure(MemoryError::IoFailure);
  }
  std::array<char, backing_magic.size()> magic{};
  input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  std::uint64_t bytes_read = magic.size();
  std::uint64_t stored_count = 0;
  if (!input || magic != backing_magic || !read_value(input, stored_count, bytes_read) ||
      stored_count != count) {
    return Result<MemoryRoundTripResult, MemoryError>::failure(MemoryError::CorruptBackingStore);
  }
  std::vector<FlatInformationSet> restored;
  restored.reserve(static_cast<std::size_t>(stored_count));
  for (std::uint64_t record = 0; record < stored_count; ++record) {
    std::uint64_t key_size = 0;
    std::uint8_t player = 0;
    std::uint64_t action_count = 0;
    if (!read_value(input, key_size, bytes_read) || !read_value(input, player, bytes_read) ||
        !read_value(input, action_count, bytes_read) || key_size == 0U || key_size > 1'048'576U ||
        action_count == 0U || action_count > 1'024U || player > 1U) {
      return Result<MemoryRoundTripResult, MemoryError>::failure(MemoryError::CorruptBackingStore);
    }
    FlatInformationSet entry;
    entry.key.resize(static_cast<std::size_t>(key_size));
    input.read(entry.key.data(), static_cast<std::streamsize>(key_size));
    if (!input || !checked_add(bytes_read, key_size)) {
      return Result<MemoryRoundTripResult, MemoryError>::failure(MemoryError::CorruptBackingStore);
    }
    entry.player = player;
    entry.actions.resize(static_cast<std::size_t>(action_count));
    entry.regrets.resize(static_cast<std::size_t>(action_count));
    entry.strategy.resize(static_cast<std::size_t>(action_count));
    for (std::size_t index = 0; index < entry.actions.size(); ++index) {
      if (!read_value(input, entry.actions[index], bytes_read) ||
          !read_value(input, entry.regrets[index], bytes_read) ||
          !read_value(input, entry.strategy[index], bytes_read)) {
        return Result<MemoryRoundTripResult, MemoryError>::failure(
            MemoryError::CorruptBackingStore);
      }
    }
    restored.push_back(std::move(entry));
  }
  input.peek();
  if (!input.eof() || bytes_read != bytes_written) {
    return Result<MemoryRoundTripResult, MemoryError>::failure(MemoryError::CorruptBackingStore);
  }
  const auto rebuilt = rebuild_checkpoint(checkpoint, restored);
  if (!rebuilt) {
    return Result<MemoryRoundTripResult, MemoryError>::failure(rebuilt.error());
  }

  MemoryRoundTripResult result;
  result.checkpoint = rebuilt.value();
  result.encoded_bytes = bytes_written;
  if (!checked_multiply(options.page_size_bytes, options.resident_page_count,
                        result.resident_bytes)) {
    return Result<MemoryRoundTripResult, MemoryError>::failure(MemoryError::ArithmeticOverflow);
  }
  result.page_writes = (bytes_written + options.page_size_bytes - 1U) / options.page_size_bytes;
  result.page_reads = (bytes_read + options.page_size_bytes - 1U) / options.page_size_bytes;
  return Result<MemoryRoundTripResult, MemoryError>::success(std::move(result));
}

Result<std::uint64_t, MemoryError> sum_memory(const MemoryBreakdown &memory,
                                              const bool include_backing_store) {
  std::uint64_t total = 0;
  const std::array values{memory.public_tree_bytes,
                          memory.infoset_index_bytes,
                          memory.action_bytes,
                          memory.regret_bytes,
                          memory.strategy_bytes,
                          memory.reach_bytes,
                          memory.best_response_bytes,
                          memory.boundary_bytes,
                          memory.checkpoint_staging_bytes,
                          memory.gui_cache_bytes,
                          include_backing_store ? memory.backing_store_bytes : 0U};
  for (const auto value : values) {
    if (!checked_add(total, value)) {
      return Result<std::uint64_t, MemoryError>::failure(MemoryError::ArithmeticOverflow);
    }
  }
  return Result<std::uint64_t, MemoryError>::success(total);
}

} // namespace

Result<PostflopTreeConfig, MemoryError>
make_postflop_benchmark_config(const PostflopBenchmark benchmark) {
  PostflopTreeConfig config;
  const auto ace_spades = parse_card("As");
  const auto queen_diamonds = parse_card("Qd");
  const auto seven_clubs = parse_card("7c");
  const auto pot = Money::from_antes(10);
  const auto minimum_bet = Money::from_antes(1);
  const auto stack = Money::from_antes(benchmark == PostflopBenchmark::PfF1 ? 20 : 100);
  if (!ace_spades || !queen_diamonds || !seven_clubs || !pot || !minimum_bet || !stack) {
    return Result<PostflopTreeConfig, MemoryError>::failure(MemoryError::InvalidConfiguration);
  }
  config.flop = {ace_spades.value(), queen_diamonds.value(), seven_clubs.value()};
  config.initial_pot = pot.value();
  config.effective_stack = stack.value();

  std::vector<PotPercentage> sizes;
  std::uint8_t raise_depth = 0;
  switch (benchmark) {
  case PostflopBenchmark::PfF1:
    sizes = {PotPercentage::from_basis_points(5'000).value()};
    raise_depth = 1;
    break;
  case PostflopBenchmark::PfF2:
    sizes = {PotPercentage::from_basis_points(3'300).value(),
             PotPercentage::from_basis_points(7'500).value()};
    raise_depth = 2;
    break;
  case PostflopBenchmark::PfF3:
    sizes = {PotPercentage::from_basis_points(2'500).value(),
             PotPercentage::from_basis_points(5'000).value(),
             PotPercentage::from_basis_points(10'000).value()};
    raise_depth = 4;
    break;
  }
  for (auto &street : config.streets) {
    for (auto &player : street.players) {
      for (auto &scenario : player) {
        scenario.aggressive_sizes = sizes;
        scenario.raise_depth = raise_depth;
        scenario.minimum_bet = minimum_bet.value();
      }
    }
  }
  if (!validate_tree_config(config)) {
    return Result<PostflopTreeConfig, MemoryError>::failure(MemoryError::InvalidConfiguration);
  }
  return Result<PostflopTreeConfig, MemoryError>::success(std::move(config));
}

Result<MemoryPrototypeReport, MemoryError>
analyze_postflop_config(const PostflopTreeConfig &config, const MemoryPrototype prototype,
                        const MemoryPrototypeOptions &options) {
  if (!valid_options(options)) {
    return Result<MemoryPrototypeReport, MemoryError>::failure(MemoryError::InvalidConfiguration);
  }
  if (!validate_tree_config(config)) {
    return Result<MemoryPrototypeReport, MemoryError>::failure(MemoryError::InvalidConfiguration);
  }
  TreeBuildOptions tree_options;
  tree_options.maximum_nodes = std::numeric_limits<std::uint64_t>::max();
  const auto tree = estimate_public_tree(config, tree_options);
  if (!tree) {
    return Result<MemoryPrototypeReport, MemoryError>::failure(
        tree.error() == TreeError::NodeOverflow ? MemoryError::ArithmeticOverflow
                                                : MemoryError::TreeFailure);
  }

  MemoryPrototypeReport report;
  report.prototype = prototype;
  report.public_tree = tree.value();
  for (std::size_t street = 0; street < 3U; ++street) {
    if (!checked_multiply(tree.value().decision_nodes_by_street[street],
                          private_combos_by_street[street],
                          report.information_sets_by_street[street]) ||
        !checked_multiply(tree.value().action_edges_by_street[street],
                          private_combos_by_street[street], report.actions_by_street[street]) ||
        !checked_multiply(tree.value().node_count_by_street[street],
                          private_combos_by_street[street],
                          report.range_state_slots_by_street[street]) ||
        !checked_add(report.information_sets, report.information_sets_by_street[street]) ||
        !checked_add(report.actions, report.actions_by_street[street]) ||
        !checked_add(report.range_state_slots, report.range_state_slots_by_street[street])) {
      return Result<MemoryPrototypeReport, MemoryError>::failure(MemoryError::ArithmeticOverflow);
    }
  }

  auto &memory = report.memory;
  if (prototype == MemoryPrototype::LazyInRam) {
    if (!add_product(memory.public_tree_bytes, tree.value().node_count,
                     compact_public_node_bytes) ||
        !add_product(memory.public_tree_bytes, tree.value().edge_count,
                     compact_public_edge_bytes) ||
        !checked_multiply(report.information_sets, sparse_infoset_index_bytes,
                          memory.infoset_index_bytes) ||
        !checked_multiply(report.actions, action_descriptor_bytes, memory.action_bytes) ||
        !checked_multiply(report.actions, scalar_bytes, memory.regret_bytes) ||
        !checked_multiply(report.actions, scalar_bytes, memory.strategy_bytes) ||
        !checked_multiply(report.range_state_slots, 2U * scalar_bytes, memory.reach_bytes) ||
        !checked_multiply(report.range_state_slots, best_response_entry_bytes,
                          memory.best_response_bytes)) {
      return Result<MemoryPrototypeReport, MemoryError>::failure(MemoryError::ArithmeticOverflow);
    }
    if (!checked_add(memory.checkpoint_staging_bytes, memory.regret_bytes) ||
        !checked_add(memory.checkpoint_staging_bytes, memory.strategy_bytes)) {
      return Result<MemoryPrototypeReport, MemoryError>::failure(MemoryError::ArithmeticOverflow);
    }
    const auto peak = sum_memory(memory, false);
    if (!peak) {
      return Result<MemoryPrototypeReport, MemoryError>::failure(peak.error());
    }
    memory.peak_resident_bytes = peak.value();
  } else if (prototype == MemoryPrototype::StreetDecomposition) {
    std::uint64_t peak = 0;
    std::uint64_t total_backing = 0;
    for (std::size_t street = 0; street < 3U; ++street) {
      MemoryBreakdown partition;
      if (!add_product(partition.public_tree_bytes, tree.value().node_count_by_street[street],
                       compact_public_node_bytes) ||
          !add_product(partition.public_tree_bytes, tree.value().edge_count_by_street[street],
                       compact_public_edge_bytes) ||
          !checked_multiply(report.information_sets_by_street[street], sparse_infoset_index_bytes,
                            partition.infoset_index_bytes) ||
          !checked_multiply(report.actions_by_street[street], action_descriptor_bytes,
                            partition.action_bytes) ||
          !checked_multiply(report.actions_by_street[street], scalar_bytes,
                            partition.regret_bytes) ||
          !checked_multiply(report.actions_by_street[street], scalar_bytes,
                            partition.strategy_bytes) ||
          !checked_multiply(report.range_state_slots_by_street[street], 2U * scalar_bytes,
                            partition.reach_bytes) ||
          !checked_multiply(report.range_state_slots_by_street[street], best_response_entry_bytes,
                            partition.best_response_bytes)) {
        return Result<MemoryPrototypeReport, MemoryError>::failure(MemoryError::ArithmeticOverflow);
      }
      if (!checked_add(partition.checkpoint_staging_bytes, partition.regret_bytes) ||
          !checked_add(partition.checkpoint_staging_bytes, partition.strategy_bytes)) {
        return Result<MemoryPrototypeReport, MemoryError>::failure(MemoryError::ArithmeticOverflow);
      }
      const auto resident = sum_memory(partition, false);
      if (!resident) {
        return Result<MemoryPrototypeReport, MemoryError>::failure(resident.error());
      }
      peak = std::max(peak, resident.value());
      if (!checked_add(memory.public_tree_bytes, partition.public_tree_bytes) ||
          !checked_add(memory.infoset_index_bytes, partition.infoset_index_bytes) ||
          !checked_add(memory.action_bytes, partition.action_bytes) ||
          !checked_add(memory.regret_bytes, partition.regret_bytes) ||
          !checked_add(memory.strategy_bytes, partition.strategy_bytes) ||
          !checked_add(memory.reach_bytes, partition.reach_bytes) ||
          !checked_add(memory.best_response_bytes, partition.best_response_bytes)) {
        return Result<MemoryPrototypeReport, MemoryError>::failure(MemoryError::ArithmeticOverflow);
      }
      if (!checked_add(memory.checkpoint_staging_bytes, partition.checkpoint_staging_bytes)) {
        return Result<MemoryPrototypeReport, MemoryError>::failure(MemoryError::ArithmeticOverflow);
      }
    }
    if (!add_product(memory.boundary_bytes, tree.value().chance_edges_by_street[0],
                     private_combos_by_street[1] * 2U * scalar_bytes) ||
        !add_product(memory.boundary_bytes, tree.value().chance_edges_by_street[1],
                     private_combos_by_street[2] * 2U * scalar_bytes)) {
      return Result<MemoryPrototypeReport, MemoryError>::failure(MemoryError::ArithmeticOverflow);
    }
    const auto backing = sum_memory(memory, false);
    std::uint64_t transient_bytes = memory.checkpoint_staging_bytes;
    if (!checked_add(transient_bytes, memory.boundary_bytes) || !backing ||
        backing.value() < transient_bytes ||
        !checked_add(total_backing, backing.value() - transient_bytes) ||
        !checked_add(peak, memory.boundary_bytes)) {
      return Result<MemoryPrototypeReport, MemoryError>::failure(MemoryError::ArithmeticOverflow);
    }
    memory.backing_store_bytes = total_backing;
    memory.peak_resident_bytes = peak;
  } else {
    if (!add_product(memory.public_tree_bytes, tree.value().node_count,
                     compact_public_node_bytes) ||
        !add_product(memory.public_tree_bytes, tree.value().edge_count,
                     compact_public_edge_bytes) ||
        !checked_multiply(report.information_sets, sparse_infoset_index_bytes,
                          memory.infoset_index_bytes) ||
        !checked_multiply(report.actions, action_descriptor_bytes, memory.action_bytes) ||
        !checked_multiply(report.actions, scalar_bytes, memory.regret_bytes) ||
        !checked_multiply(report.actions, scalar_bytes, memory.strategy_bytes) ||
        !checked_multiply(report.range_state_slots, 2U * scalar_bytes, memory.reach_bytes) ||
        !checked_multiply(report.range_state_slots, best_response_entry_bytes,
                          memory.best_response_bytes)) {
      return Result<MemoryPrototypeReport, MemoryError>::failure(MemoryError::ArithmeticOverflow);
    }
    const auto backing = sum_memory(memory, false);
    if (!backing || !checked_multiply(options.page_size_bytes, options.resident_page_count,
                                      memory.peak_resident_bytes)) {
      return Result<MemoryPrototypeReport, MemoryError>::failure(MemoryError::ArithmeticOverflow);
    }
    memory.backing_store_bytes = backing.value();
    memory.checkpoint_staging_bytes = options.page_size_bytes;
    if (!checked_add(memory.peak_resident_bytes, memory.checkpoint_staging_bytes)) {
      return Result<MemoryPrototypeReport, MemoryError>::failure(MemoryError::ArithmeticOverflow);
    }
  }

  report.bytes_per_public_node = report.public_tree.node_count == 0U
                                     ? 0.0
                                     : static_cast<double>(memory.public_tree_bytes) /
                                           static_cast<double>(report.public_tree.node_count);
  MemoryBreakdown solver_memory = memory;
  solver_memory.public_tree_bytes = 0;
  solver_memory.boundary_bytes = 0;
  solver_memory.gui_cache_bytes = 0;
  solver_memory.backing_store_bytes = 0;
  const auto solver_bytes = sum_memory(solver_memory, false);
  if (!solver_bytes) {
    return Result<MemoryPrototypeReport, MemoryError>::failure(solver_bytes.error());
  }
  report.bytes_per_information_set = report.information_sets == 0U
                                         ? 0.0
                                         : static_cast<double>(solver_bytes.value()) /
                                               static_cast<double>(report.information_sets);
  if (!checked_multiply(memory.backing_store_bytes > 0U ? memory.backing_store_bytes
                                                        : memory.peak_resident_bytes,
                        physical_flop_count, report.preflop_full_projection_bytes)) {
    return Result<MemoryPrototypeReport, MemoryError>::failure(MemoryError::ArithmeticOverflow);
  }
  return Result<MemoryPrototypeReport, MemoryError>::success(report);
}

Result<MemoryPrototypeReport, MemoryError>
analyze_memory_prototype(const PostflopBenchmark benchmark, const MemoryPrototype prototype,
                         const MemoryPrototypeOptions &options) {
  const auto config = make_postflop_benchmark_config(benchmark);
  if (!config) {
    return Result<MemoryPrototypeReport, MemoryError>::failure(config.error());
  }
  auto report = analyze_postflop_config(config.value(), prototype, options);
  if (report) {
    report.value().benchmark = benchmark;
  }
  return report;
}

Result<MemoryRoundTripResult, MemoryError>
round_trip_checkpoint_memory(const SolverCheckpoint &checkpoint, const MemoryPrototype prototype,
                             const std::string &backing_file,
                             const MemoryPrototypeOptions &options) {
  if (!valid_options(options)) {
    return Result<MemoryRoundTripResult, MemoryError>::failure(MemoryError::InvalidConfiguration);
  }
  if (prototype == MemoryPrototype::OutOfCore) {
    return round_trip_out_of_core(checkpoint, backing_file, options);
  }
  return round_trip_in_memory(checkpoint, prototype);
}

Result<MemoryResidencyProbe, MemoryError>
probe_out_of_core_residency(const MemoryPrototypeReport &report, const std::string &backing_file,
                            const MemoryPrototypeOptions &options) {
  if (report.prototype != MemoryPrototype::OutOfCore || backing_file.empty() ||
      report.memory.backing_store_bytes == 0U || !valid_options(options)) {
    return Result<MemoryResidencyProbe, MemoryError>::failure(MemoryError::InvalidConfiguration);
  }
  std::uint64_t cache_bytes = 0;
  if (!checked_multiply(options.page_size_bytes, options.resident_page_count, cache_bytes) ||
      cache_bytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
      report.memory.backing_store_bytes < options.page_size_bytes) {
    return Result<MemoryResidencyProbe, MemoryError>::failure(MemoryError::ArithmeticOverflow);
  }

  std::error_code filesystem_error;
  if (std::filesystem::exists(backing_file, filesystem_error) || filesystem_error) {
    return Result<MemoryResidencyProbe, MemoryError>::failure(MemoryError::IoFailure);
  }
  std::ofstream create(backing_file, std::ios::binary | std::ios::trunc);
  if (!create || report.memory.backing_store_bytes == 0U) {
    return Result<MemoryResidencyProbe, MemoryError>::failure(MemoryError::IoFailure);
  }
  create.seekp(static_cast<std::streamoff>(report.memory.backing_store_bytes - 1U));
  create.put('\0');
  create.flush();
  if (!create) {
    return Result<MemoryResidencyProbe, MemoryError>::failure(MemoryError::IoFailure);
  }
  create.close();

  std::vector<char> cache(static_cast<std::size_t>(cache_bytes), '\0');
  MappedBacking backing(backing_file, report.memory.backing_store_bytes);
  if (!backing.valid()) {
    return Result<MemoryResidencyProbe, MemoryError>::failure(MemoryError::IoFailure);
  }
  const auto total_pages = report.memory.backing_store_bytes / options.page_size_bytes;
  const auto access_count = std::min(total_pages, options.resident_page_count * 2U);
  std::vector<std::uint64_t> slot_pages(static_cast<std::size_t>(options.resident_page_count),
                                        std::numeric_limits<std::uint64_t>::max());
  std::vector<std::uint64_t> slot_ages(static_cast<std::size_t>(options.resident_page_count), 0U);
  std::vector<bool> slot_dirty(static_cast<std::size_t>(options.resident_page_count), false);
  std::uint64_t age = 0;
  std::uint64_t page_reads = 0;
  std::uint64_t page_writes = 0;

  const auto flush_slot = [&](const std::size_t slot) {
    if (!slot_dirty[slot]) {
      return true;
    }
    const auto offset = slot_pages[slot] * options.page_size_bytes;
    const auto *const slot_data =
        cache.data() + static_cast<std::size_t>(slot * options.page_size_bytes);
    std::memcpy(backing.data() + static_cast<std::size_t>(offset), slot_data,
                static_cast<std::size_t>(options.page_size_bytes));
    if (!backing.flush(offset, options.page_size_bytes)) {
      return false;
    }
    slot_dirty[slot] = false;
    ++page_writes;
    return true;
  };

  const auto acquire_page = [&](const std::uint64_t page) -> char * {
    ++age;
    for (std::size_t slot = 0; slot < slot_pages.size(); ++slot) {
      if (slot_pages[slot] == page) {
        slot_ages[slot] = age;
        return cache.data() + static_cast<std::size_t>(slot * options.page_size_bytes);
      }
    }
    std::size_t victim = 0;
    for (std::size_t slot = 0; slot < slot_pages.size(); ++slot) {
      if (slot_pages[slot] == std::numeric_limits<std::uint64_t>::max()) {
        victim = slot;
        break;
      }
      if (slot_ages[slot] < slot_ages[victim]) {
        victim = slot;
      }
    }
    if (!flush_slot(victim)) {
      return nullptr;
    }
    auto *const slot_data =
        cache.data() + static_cast<std::size_t>(victim * options.page_size_bytes);
    const auto offset = page * options.page_size_bytes;
    std::memcpy(slot_data, backing.data() + static_cast<std::size_t>(offset),
                static_cast<std::size_t>(options.page_size_bytes));
    slot_pages[victim] = page;
    slot_ages[victim] = age;
    ++page_reads;
    return slot_data;
  };

  const auto logical_page = [&](const std::uint64_t access) {
    return access_count == 1U ? 0U : ((total_pages - 1U) / (access_count - 1U)) * access;
  };
  for (std::uint64_t access = 0; access < access_count; ++access) {
    char *const page_data = acquire_page(logical_page(access));
    if (page_data == nullptr) {
      return Result<MemoryResidencyProbe, MemoryError>::failure(MemoryError::IoFailure);
    }
    page_data[0] = static_cast<char>(access & 0x7FU);
    const auto found = std::ranges::find(slot_pages, logical_page(access));
    slot_dirty[static_cast<std::size_t>(std::distance(slot_pages.begin(), found))] = true;
  }
  for (std::size_t slot = 0; slot < slot_pages.size(); ++slot) {
    if (!flush_slot(slot)) {
      return Result<MemoryResidencyProbe, MemoryError>::failure(MemoryError::IoFailure);
    }
  }
  for (std::uint64_t access = access_count; access > 0U; --access) {
    if (acquire_page(logical_page(access - 1U)) == nullptr) {
      return Result<MemoryResidencyProbe, MemoryError>::failure(MemoryError::IoFailure);
    }
  }

  MemoryResidencyProbe probe;
  probe.logical_backing_bytes = report.memory.backing_store_bytes;
  if (!checked_multiply(access_count, options.page_size_bytes, probe.touched_bytes)) {
    return Result<MemoryResidencyProbe, MemoryError>::failure(MemoryError::ArithmeticOverflow);
  }
  probe.measured_peak_rss_bytes = measured_peak_rss();
  probe.page_reads = page_reads;
  probe.page_writes = page_writes;
  if (probe.measured_peak_rss_bytes == 0U) {
    return Result<MemoryResidencyProbe, MemoryError>::failure(MemoryError::IoFailure);
  }
  return Result<MemoryResidencyProbe, MemoryError>::success(probe);
}

const char *memory_prototype_name(const MemoryPrototype prototype) noexcept {
  switch (prototype) {
  case MemoryPrototype::LazyInRam:
    return "lazy_in_ram";
  case MemoryPrototype::StreetDecomposition:
    return "street_decomposition";
  case MemoryPrototype::OutOfCore:
    return "out_of_core";
  }
  return "unknown";
}

const char *postflop_benchmark_name(const PostflopBenchmark benchmark) noexcept {
  switch (benchmark) {
  case PostflopBenchmark::PfF1:
    return "PF-F1";
  case PostflopBenchmark::PfF2:
    return "PF-F2";
  case PostflopBenchmark::PfF3:
    return "PF-F3";
  }
  return "unknown";
}

const char *memory_error_name(const MemoryError error) noexcept {
  switch (error) {
  case MemoryError::InvalidConfiguration:
    return "invalid_configuration";
  case MemoryError::TreeFailure:
    return "tree_failure";
  case MemoryError::ArithmeticOverflow:
    return "arithmetic_overflow";
  case MemoryError::InvalidCheckpoint:
    return "invalid_checkpoint";
  case MemoryError::IoFailure:
    return "io_failure";
  case MemoryError::CorruptBackingStore:
    return "corrupt_backing_store";
  }
  return "unknown";
}

std::uint64_t process_peak_rss_bytes() noexcept { return measured_peak_rss(); }

} // namespace gtosd
