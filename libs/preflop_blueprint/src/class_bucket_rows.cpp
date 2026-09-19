#include "gtosd/card_abstraction/card_abstraction.hpp"
#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"

#include "gtosd/preflop_blueprint/board_context.hpp"
#include <utility>

namespace gtosd::preflop_blueprint {
namespace ca = card_abstraction;

Result<ClassBucketRows, KernelError> ClassBucketRows::build(const ca::BucketTable &flop,
                                                            const ca::BucketTable &turn,
                                                            const ca::BucketTable &river) {
  using Outcome = Result<ClassBucketRows, KernelError>;
  ClassBucketRows result;
  const std::array<const ca::BucketTable *, 3> tables{&flop, &turn, &river};
  const auto &combos = ca::combo_table();
  for (std::size_t index = 0; index < tables.size(); ++index) {
    const auto &table = *tables[index];
    if (static_cast<std::size_t>(table.street()) != index || table.capacity() == 0U) {
      return Outcome::failure(KernelError::InvalidInput);
    }
    result.capacities_[index] = table.capacity();
    result.fingerprints_[index] = table.fingerprint();
    auto &rows = result.rows_[index];
    rows.assign(static_cast<std::size_t>(ca::preflop_hand_classes) * table.capacity(),
                ca::no_bucket);
    for (std::uint32_t board = 0; board < table.rows(); ++board) {
      for (std::uint16_t combo = 0; combo < ca::combo_count; ++combo) {
        const auto bucket = table.bucket(board, combo);
        if (bucket == ca::no_bucket) {
          continue;
        }
        if (bucket >= table.capacity()) {
          return Outcome::failure(KernelError::InvalidInput);
        }
        rows[static_cast<std::size_t>(combos.hand_class[combo]) * table.capacity() + bucket] = 0U;
      }
    }
    for (auto &row : rows) {
      if (row != ca::no_bucket) {
        if (result.counts_[index] >= ca::no_bucket) {
          return Outcome::failure(KernelError::InvalidInput);
        }
        row = static_cast<std::uint16_t>(result.counts_[index]++);
      }
    }
  }
  return Outcome::success(std::move(result));
}

std::uint16_t ClassBucketRows::row(const ca::BucketStreet street, const std::uint8_t hand_class,
                                   const std::uint16_t bucket) const noexcept {
  const auto index = static_cast<std::size_t>(street);
  if (index >= rows_.size() || hand_class >= ca::preflop_hand_classes ||
      bucket >= capacities_[index]) {
    return ca::no_bucket;
  }
  return rows_[index][static_cast<std::size_t>(hand_class) * capacities_[index] + bucket];
}

bool ClassBucketRows::matches(const ca::BucketTable &table) const noexcept {
  const auto index = static_cast<std::size_t>(table.street());
  return index < fingerprints_.size() && fingerprints_[index] == table.fingerprint();
}
} // namespace gtosd::preflop_blueprint
