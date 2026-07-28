#pragma once

#include "gtosd/solver/solver.hpp"
#include "gtosd/tree/tree.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace gtosd {

enum class MemoryPrototype : std::uint8_t { LazyInRam, StreetDecomposition, OutOfCore };

enum class PostflopBenchmark : std::uint8_t { PfF1, PfF2, PfF3 };

enum class MemoryError : std::uint8_t {
  InvalidConfiguration,
  TreeFailure,
  ArithmeticOverflow,
  InvalidCheckpoint,
  IoFailure,
  CorruptBackingStore
};

struct MemoryPrototypeOptions {
  std::uint64_t page_size_bytes{64U * 1'024U};
  std::uint64_t resident_page_count{64U};
};

struct MemoryBreakdown {
  std::uint64_t public_tree_bytes{0};
  std::uint64_t infoset_index_bytes{0};
  std::uint64_t action_bytes{0};
  std::uint64_t regret_bytes{0};
  std::uint64_t strategy_bytes{0};
  std::uint64_t reach_bytes{0};
  std::uint64_t best_response_bytes{0};
  std::uint64_t boundary_bytes{0};
  std::uint64_t checkpoint_staging_bytes{0};
  std::uint64_t gui_cache_bytes{0};
  std::uint64_t backing_store_bytes{0};
  std::uint64_t peak_resident_bytes{0};
};

struct MemoryPrototypeReport {
  MemoryPrototype prototype{MemoryPrototype::LazyInRam};
  PostflopBenchmark benchmark{PostflopBenchmark::PfF1};
  PublicTreeStats public_tree{};
  std::array<std::uint64_t, 3> information_sets_by_street{};
  std::array<std::uint64_t, 3> actions_by_street{};
  std::array<std::uint64_t, 3> range_state_slots_by_street{};
  std::uint64_t information_sets{0};
  std::uint64_t actions{0};
  std::uint64_t range_state_slots{0};
  MemoryBreakdown memory{};
  double bytes_per_public_node{0.0};
  double bytes_per_information_set{0.0};
  std::uint64_t preflop_full_projection_bytes{0};
  bool exact_outcomes{true};
  bool uses_bucketing{false};
};

struct MemoryRoundTripResult {
  SolverCheckpoint checkpoint{};
  std::uint64_t encoded_bytes{0};
  std::uint64_t resident_bytes{0};
  std::uint64_t page_reads{0};
  std::uint64_t page_writes{0};
};

struct MemoryResidencyProbe {
  std::uint64_t logical_backing_bytes{0};
  std::uint64_t touched_bytes{0};
  std::uint64_t measured_peak_rss_bytes{0};
  std::uint64_t page_reads{0};
  std::uint64_t page_writes{0};
};

[[nodiscard]] Result<PostflopTreeConfig, MemoryError>
make_postflop_benchmark_config(PostflopBenchmark benchmark);

[[nodiscard]] Result<MemoryPrototypeReport, MemoryError>
analyze_memory_prototype(PostflopBenchmark benchmark, MemoryPrototype prototype,
                         const MemoryPrototypeOptions &options = {});

[[nodiscard]] Result<MemoryRoundTripResult, MemoryError>
round_trip_checkpoint_memory(const SolverCheckpoint &checkpoint, MemoryPrototype prototype,
                             const std::string &backing_file = {},
                             const MemoryPrototypeOptions &options = {});

[[nodiscard]] Result<MemoryResidencyProbe, MemoryError>
probe_out_of_core_residency(const MemoryPrototypeReport &report, const std::string &backing_file,
                            const MemoryPrototypeOptions &options = {});

[[nodiscard]] const char *memory_prototype_name(MemoryPrototype prototype) noexcept;
[[nodiscard]] const char *postflop_benchmark_name(PostflopBenchmark benchmark) noexcept;
[[nodiscard]] const char *memory_error_name(MemoryError error) noexcept;

} // namespace gtosd
