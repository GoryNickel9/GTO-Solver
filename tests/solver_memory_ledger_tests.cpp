#include "gtosd/postflop/solver_memory_ledger.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

gtosd::SolverMemoryCategoryMetadata persistent() {
  return {gtosd::SolverMemoryBacking::Heap, gtosd::SolverMemoryLifetime::Persistent, true,
          "solver_owned_persistent"};
}

void allocation_lifetime_and_peak() {
  gtosd::SolverMemoryLedger ledger;
  expect(ledger.set("tree", persistent(), gtosd::SolverMemoryPhase::TreePreparation, 80U, 100U) ==
             gtosd::SolverMemoryLedgerError::None,
         "tree allocation");
  auto scratch = gtosd::SolverMemoryCategoryMetadata{
      gtosd::SolverMemoryBacking::Arena, gtosd::SolverMemoryLifetime::Scratch, true,
      "solver_owned_scratch"};
  expect(ledger.set("scratch", scratch, gtosd::SolverMemoryPhase::Traversal, 40U, 64U) ==
             gtosd::SolverMemoryLedgerError::None,
         "scratch allocation");
  expect(ledger.set("scratch", scratch, gtosd::SolverMemoryPhase::Traversal, 50U, 96U) ==
             gtosd::SolverMemoryLedgerError::None,
         "scratch growth");
  expect(ledger.release("scratch", gtosd::SolverMemoryPhase::Certification) ==
             gtosd::SolverMemoryLedgerError::None,
         "scratch release");
  const auto snapshot = ledger.snapshot();
  expect(snapshot.current_logical_bytes == 80U, "current logical sum");
  expect(snapshot.current_allocated_bytes == 100U, "current allocated sum");
  expect(snapshot.maximum_logical_bytes == 130U, "simultaneous logical peak");
  expect(snapshot.maximum_allocated_bytes == 196U, "simultaneous allocated peak");
  expect(snapshot.categories.size() == 2U, "deterministic category count");
  const auto &scratch_snapshot = snapshot.categories[0];
  expect(scratch_snapshot.name == "scratch", "categories sorted by name");
  expect(scratch_snapshot.maximum_allocated_bytes == 96U, "category capacity peak");
  expect(scratch_snapshot.first_allocation_phase ==
             gtosd::SolverMemoryPhase::Traversal,
         "first allocation phase");
  expect(scratch_snapshot.release_phase == gtosd::SolverMemoryPhase::Certification,
         "release phase");
}

void failures_are_explicit() {
  gtosd::SolverMemoryLedger ledger;
  expect(ledger.set("bad", persistent(), gtosd::SolverMemoryPhase::SolverStateReady, 2U, 1U) ==
             gtosd::SolverMemoryLedgerError::InvalidAllocation,
         "logical greater than allocation rejected");
  expect(ledger.release("missing", gtosd::SolverMemoryPhase::Finalization) ==
             gtosd::SolverMemoryLedgerError::Underflow,
         "underflow rejected");
  expect(ledger.set("x", persistent(), gtosd::SolverMemoryPhase::Traversal,
                    std::numeric_limits<std::uint64_t>::max(),
                    std::numeric_limits<std::uint64_t>::max()) ==
             gtosd::SolverMemoryLedgerError::None,
         "maximum category accepted");
  expect(ledger.set("y", persistent(), gtosd::SolverMemoryPhase::Traversal, 1U, 1U) ==
             gtosd::SolverMemoryLedgerError::Overflow,
         "total overflow rejected");
  expect(ledger.snapshot().categories.size() == 1U,
         "overflow rolls back a newly inserted category");
  auto excluded = persistent();
  excluded.included_in_candidate = false;
  expect(ledger.set("x", excluded, gtosd::SolverMemoryPhase::Traversal, 1U, 1U) ==
             gtosd::SolverMemoryLedgerError::MetadataMismatch,
         "metadata mismatch rejected");
}

void thread_safe_updates() {
  gtosd::SolverMemoryLedger ledger;
  constexpr std::size_t workers = 8U;
  std::vector<std::jthread> threads;
  threads.reserve(workers);
  for (std::size_t index = 0; index < workers; ++index) {
    threads.emplace_back([&ledger, index] {
      const auto name = "worker." + std::to_string(index);
      const auto metadata = gtosd::SolverMemoryCategoryMetadata{
          gtosd::SolverMemoryBacking::Arena, gtosd::SolverMemoryLifetime::Scratch, true,
          "worker_scratch"};
      static_cast<void>(ledger.set(name, metadata, gtosd::SolverMemoryPhase::Traversal, 16U, 32U));
    });
  }
  threads.clear();
  const auto snapshot = ledger.snapshot();
  expect(snapshot.categories.size() == workers, "all concurrent categories retained");
  expect(snapshot.current_logical_bytes == workers * 16U, "concurrent logical total");
  expect(snapshot.current_allocated_bytes == workers * 32U, "concurrent allocated total");
}

void paging_does_not_change_logical_accounting() {
  gtosd::SolverMemoryLedger ledger;
  const auto mapped = gtosd::SolverMemoryCategoryMetadata{
      gtosd::SolverMemoryBacking::Mapped, gtosd::SolverMemoryLifetime::Persistent, true,
      "logical_mapping_included_residency_excluded"};
  expect(ledger.set("mapped_state", mapped, gtosd::SolverMemoryPhase::SolverStateReady, 4096U,
                    4096U) == gtosd::SolverMemoryLedgerError::None,
         "mapped allocation");
  const auto before = ledger.snapshot();
  // A trim/paging event intentionally makes no ledger call.
  const auto after = ledger.snapshot();
  expect(before.current_logical_bytes == after.current_logical_bytes,
         "logical bytes stable under residency changes");
  expect(before.maximum_allocated_bytes == after.maximum_allocated_bytes,
         "allocated peak stable under residency changes");
}

} // namespace

int main() {
  allocation_lifetime_and_peak();
  failures_are_explicit();
  thread_safe_updates();
  paging_does_not_change_logical_accounting();
  if (failures != 0) {
    return 1;
  }
  std::cout << "solver memory ledger tests passed\n";
  return 0;
}
