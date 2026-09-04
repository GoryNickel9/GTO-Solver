#include "gtosd/postflop/solver_memory_ledger.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace gtosd {
namespace {

bool add_checked(std::uint64_t &total, const std::uint64_t value) noexcept {
  if (value > std::numeric_limits<std::uint64_t>::max() - total) {
    return false;
  }
  total += value;
  return true;
}

} // namespace

SolverMemoryLedgerError SolverMemoryLedger::set(
    std::string name, SolverMemoryCategoryMetadata metadata, const SolverMemoryPhase phase,
    const std::uint64_t logical_bytes, const std::uint64_t allocated_bytes) {
  if (name.empty()) {
    return SolverMemoryLedgerError::EmptyCategory;
  }
  if (logical_bytes > allocated_bytes) {
    return SolverMemoryLedgerError::InvalidAllocation;
  }
  std::scoped_lock lock(mutex_);
  auto [entry, inserted] = records_.try_emplace(std::move(name));
  if (!inserted && entry->second.metadata != metadata) {
    return SolverMemoryLedgerError::MetadataMismatch;
  }
  auto previous = entry->second;
  entry->second.metadata = std::move(metadata);
  entry->second.current_logical_bytes = logical_bytes;
  entry->second.current_allocated_bytes = allocated_bytes;
  entry->second.maximum_logical_bytes =
      std::max(entry->second.maximum_logical_bytes, logical_bytes);
  entry->second.maximum_allocated_bytes =
      std::max(entry->second.maximum_allocated_bytes, allocated_bytes);
  if (allocated_bytes != 0U && !entry->second.first_allocation_phase) {
    entry->second.first_allocation_phase = phase;
  }
  if (allocated_bytes != 0U) {
    entry->second.release_phase.reset();
  }
  const auto old_phase = phase_;
  phase_ = phase;
  const auto error = update_totals_locked();
  if (error != SolverMemoryLedgerError::None) {
    if (inserted) {
      records_.erase(entry);
    } else {
      entry->second = std::move(previous);
    }
    phase_ = old_phase;
  }
  return error;
}

SolverMemoryLedgerError SolverMemoryLedger::release(const std::string &name,
                                                     const SolverMemoryPhase phase) noexcept {
  std::scoped_lock lock(mutex_);
  const auto entry = records_.find(name);
  if (entry == records_.end() || entry->second.current_allocated_bytes == 0U) {
    return SolverMemoryLedgerError::Underflow;
  }
  entry->second.current_logical_bytes = 0U;
  entry->second.current_allocated_bytes = 0U;
  entry->second.release_phase = phase;
  phase_ = phase;
  return update_totals_locked();
}

SolverMemoryLedgerError SolverMemoryLedger::transition(const SolverMemoryPhase phase) noexcept {
  std::scoped_lock lock(mutex_);
  phase_ = phase;
  return SolverMemoryLedgerError::None;
}

SolverMemoryLedgerError SolverMemoryLedger::update_totals_locked() noexcept {
  std::uint64_t logical = 0U;
  std::uint64_t allocated = 0U;
  std::uint64_t candidate_logical = 0U;
  std::uint64_t candidate_allocated = 0U;
  for (const auto &[name, record] : records_) {
    static_cast<void>(name);
    if (!add_checked(logical, record.current_logical_bytes) ||
        !add_checked(allocated, record.current_allocated_bytes)) {
      return SolverMemoryLedgerError::Overflow;
    }
    if (record.metadata.included_in_candidate &&
        (!add_checked(candidate_logical, record.current_logical_bytes) ||
         !add_checked(candidate_allocated, record.current_allocated_bytes))) {
      return SolverMemoryLedgerError::Overflow;
    }
  }
  maximum_logical_bytes_ = std::max(maximum_logical_bytes_, logical);
  maximum_allocated_bytes_ = std::max(maximum_allocated_bytes_, allocated);
  candidate_maximum_logical_bytes_ =
      std::max(candidate_maximum_logical_bytes_, candidate_logical);
  candidate_maximum_allocated_bytes_ =
      std::max(candidate_maximum_allocated_bytes_, candidate_allocated);
  return SolverMemoryLedgerError::None;
}

SolverMemoryLedgerSnapshot SolverMemoryLedger::snapshot() const {
  std::scoped_lock lock(mutex_);
  SolverMemoryLedgerSnapshot result;
  result.phase = phase_;
  result.maximum_logical_bytes = maximum_logical_bytes_;
  result.maximum_allocated_bytes = maximum_allocated_bytes_;
  result.candidate_maximum_logical_bytes = candidate_maximum_logical_bytes_;
  result.candidate_maximum_allocated_bytes = candidate_maximum_allocated_bytes_;
  result.categories.reserve(records_.size());
  for (const auto &[name, record] : records_) {
    result.categories.push_back({name,
                                 record.metadata,
                                 record.current_logical_bytes,
                                 record.maximum_logical_bytes,
                                 record.current_allocated_bytes,
                                 record.maximum_allocated_bytes,
                                 record.first_allocation_phase,
                                 record.release_phase});
    add_checked(result.current_logical_bytes, record.current_logical_bytes);
    add_checked(result.current_allocated_bytes, record.current_allocated_bytes);
    if (record.metadata.included_in_candidate) {
      add_checked(result.candidate_current_logical_bytes, record.current_logical_bytes);
      add_checked(result.candidate_current_allocated_bytes, record.current_allocated_bytes);
    }
  }
  return result;
}

const char *solver_memory_phase_name(const SolverMemoryPhase phase) noexcept {
  switch (phase) {
  case SolverMemoryPhase::ProcessStart: return "process_start";
  case SolverMemoryPhase::TreePreparation: return "tree_preparation";
  case SolverMemoryPhase::SolverStateReady: return "solver_state_ready";
  case SolverMemoryPhase::Traversal: return "traversal";
  case SolverMemoryPhase::Certification: return "certification";
  case SolverMemoryPhase::Finalization: return "finalization";
  case SolverMemoryPhase::CheckpointMaterialization: return "checkpoint_materialization";
  case SolverMemoryPhase::SolutionReady: return "solution_ready";
  }
  return "unknown";
}

const char *solver_memory_backing_name(const SolverMemoryBacking backing) noexcept {
  switch (backing) {
  case SolverMemoryBacking::Heap: return "heap";
  case SolverMemoryBacking::Arena: return "arena";
  case SolverMemoryBacking::Mapped: return "mapped";
  }
  return "unknown";
}

const char *solver_memory_lifetime_name(const SolverMemoryLifetime lifetime) noexcept {
  switch (lifetime) {
  case SolverMemoryLifetime::Persistent: return "persistent";
  case SolverMemoryLifetime::Scratch: return "scratch";
  case SolverMemoryLifetime::Certification: return "certification";
  case SolverMemoryLifetime::Finalization: return "finalization";
  }
  return "unknown";
}

} // namespace gtosd
