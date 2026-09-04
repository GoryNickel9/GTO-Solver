#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace gtosd {

enum class SolverMemoryPhase : std::uint8_t {
  ProcessStart,
  TreePreparation,
  SolverStateReady,
  Traversal,
  Certification,
  Finalization,
  CheckpointMaterialization,
  SolutionReady,
};

enum class SolverMemoryBacking : std::uint8_t { Heap, Arena, Mapped };
enum class SolverMemoryLifetime : std::uint8_t {
  Persistent,
  Scratch,
  Certification,
  Finalization,
};

enum class SolverMemoryLedgerError : std::uint8_t {
  None,
  EmptyCategory,
  InvalidAllocation,
  MetadataMismatch,
  Underflow,
  Overflow,
  PhaseRegression,
};

struct SolverMemoryCategoryMetadata {
  SolverMemoryBacking backing{SolverMemoryBacking::Heap};
  SolverMemoryLifetime lifetime{SolverMemoryLifetime::Persistent};
  bool included_in_candidate{true};
  std::string candidate_reason;

  bool operator==(const SolverMemoryCategoryMetadata &) const = default;
};

struct SolverMemoryCategorySnapshot {
  std::string name;
  SolverMemoryCategoryMetadata metadata;
  std::uint64_t current_logical_bytes{0};
  std::uint64_t maximum_logical_bytes{0};
  std::uint64_t current_allocated_bytes{0};
  std::uint64_t maximum_allocated_bytes{0};
  std::optional<SolverMemoryPhase> first_allocation_phase;
  std::optional<SolverMemoryPhase> release_phase;
};

struct SolverMemoryLedgerSnapshot {
  std::vector<SolverMemoryCategorySnapshot> categories;
  std::uint64_t current_logical_bytes{0};
  std::uint64_t maximum_logical_bytes{0};
  std::uint64_t current_allocated_bytes{0};
  std::uint64_t maximum_allocated_bytes{0};
  std::uint64_t candidate_current_logical_bytes{0};
  std::uint64_t candidate_maximum_logical_bytes{0};
  std::uint64_t candidate_current_allocated_bytes{0};
  std::uint64_t candidate_maximum_allocated_bytes{0};
  SolverMemoryPhase phase{SolverMemoryPhase::ProcessStart};
};

// Solver-owned accounting only. This ledger deliberately does not inspect RSS,
// commit charge, allocator internals, or page residency. Callers update it at
// ownership/capacity changes, so paging and working-set trimming cannot change
// the logical or allocated totals.
class SolverMemoryLedger final {
public:
  [[nodiscard]] SolverMemoryLedgerError set(
      std::string name, SolverMemoryCategoryMetadata metadata, SolverMemoryPhase phase,
      std::uint64_t logical_bytes, std::uint64_t allocated_bytes);
  [[nodiscard]] SolverMemoryLedgerError release(const std::string &name,
                                                 SolverMemoryPhase phase) noexcept;
  [[nodiscard]] SolverMemoryLedgerError transition(SolverMemoryPhase phase) noexcept;
  [[nodiscard]] SolverMemoryLedgerSnapshot snapshot() const;

private:
  struct Record {
    SolverMemoryCategoryMetadata metadata;
    std::uint64_t current_logical_bytes{0};
    std::uint64_t maximum_logical_bytes{0};
    std::uint64_t current_allocated_bytes{0};
    std::uint64_t maximum_allocated_bytes{0};
    std::optional<SolverMemoryPhase> first_allocation_phase;
    std::optional<SolverMemoryPhase> release_phase;
  };

  [[nodiscard]] SolverMemoryLedgerError update_totals_locked() noexcept;

  mutable std::mutex mutex_;
  std::map<std::string, Record> records_;
  SolverMemoryPhase phase_{SolverMemoryPhase::ProcessStart};
  std::uint64_t maximum_logical_bytes_{0};
  std::uint64_t maximum_allocated_bytes_{0};
  std::uint64_t candidate_maximum_logical_bytes_{0};
  std::uint64_t candidate_maximum_allocated_bytes_{0};
};

[[nodiscard]] const char *solver_memory_phase_name(SolverMemoryPhase phase) noexcept;
[[nodiscard]] const char *solver_memory_backing_name(SolverMemoryBacking backing) noexcept;
[[nodiscard]] const char *solver_memory_lifetime_name(SolverMemoryLifetime lifetime) noexcept;

} // namespace gtosd
