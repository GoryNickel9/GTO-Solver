#pragma once

#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/preflop_blueprint/board_texture.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <utility>

// MonkerSolver-style information rows (step 2 of its tree building): the
// strategy of a postflop hand is keyed by the class of the board and by the
// hand's bucket on that board, where the bucket tables group the hands board
// by board (strength levels times potential tiers). The board classes come
// from a texture map (board_texture.hpp). Without one (the identity) flop
// classes are the 573 canonical flops and turn classes the 13,761 canonical
// flop+turn boards, so every flop and every turn has its own rows; a texture
// merges turns into classes that share their rows. The flop classes are
// always the canonical flops in the maps used so far. The river shares the
// class of its turn (all rivers of a turn use the same rows). This river
// rule is ours: MonkerSolver merges turns and rivers into texture classes
// with an undocumented rule. Row of a street: class * groups + bucket, with
// groups the capacity of that street's bucket table; the bucket stays the
// one of the canonical board (the texture changes only the class). Imperfect
// recall: the row keeps neither the preflop class nor the buckets of earlier
// streets.
namespace gtosd::preflop_blueprint {

class BoardClassRows {
public:
  BoardClassRows(const std::uint32_t flop_groups, const std::uint32_t turn_groups,
                 const std::uint32_t river_groups,
                 BoardTextureMap texture = BoardTextureMap::identity())
      : flop_groups_(flop_groups), turn_groups_(turn_groups), river_groups_(river_groups),
        texture_(std::move(texture)),
        // The identity keeps the historical string (trainer identities,
        // checkpoints and policy sources of the runs without a texture).
        fingerprint_(texture_.is_identity()
                         ? "board-class-rows-v1|flop=" + std::to_string(flop_groups) +
                               "|turn=" + std::to_string(turn_groups) +
                               "|river=turn-class*" + std::to_string(river_groups)
                         : "board-class-rows-v2|flop=" + std::to_string(flop_groups) +
                               "|turn=" + std::to_string(turn_groups) +
                               "|river=" + std::to_string(river_groups) + "|" +
                               texture_.fingerprint()) {}

  [[nodiscard]] std::uint32_t groups(const card_abstraction::BucketStreet street) const noexcept {
    switch (street) {
    case card_abstraction::BucketStreet::Flop:
      return flop_groups_;
    case card_abstraction::BucketStreet::Turn:
      return turn_groups_;
    case card_abstraction::BucketStreet::River:
      return river_groups_;
    }
    return 0U;
  }
  // Number of board classes of a street (the river uses the turn classes).
  [[nodiscard]] std::uint32_t classes(const card_abstraction::BucketStreet street) const noexcept {
    return texture_.classes(street);
  }
  [[nodiscard]] std::uint32_t count(const card_abstraction::BucketStreet street) const noexcept {
    return classes(street) * groups(street);
  }
  [[nodiscard]] bool matches(const card_abstraction::BucketTable &table) const noexcept {
    return groups(table.street()) != 0U && table.capacity() == groups(table.street());
  }
  [[nodiscard]] std::uint32_t flop_class(const std::uint32_t flop_index) const noexcept {
    return texture_.flop_class(flop_index);
  }
  [[nodiscard]] std::uint32_t turn_class(const std::uint32_t flop_turn_index) const noexcept {
    return texture_.turn_class(flop_turn_index);
  }
  [[nodiscard]] std::uint32_t river_class(const std::uint32_t flop_turn_index) const noexcept {
    return texture_.river_class(flop_turn_index);
  }
  // Class of a canonical board: the canonical flop index on the flop, the
  // canonical flop+turn index on the turn and on the river.
  [[nodiscard]] std::uint32_t board_class(const card_abstraction::BucketStreet street,
                                          const std::uint32_t canonical_index) const noexcept {
    switch (street) {
    case card_abstraction::BucketStreet::Flop:
      return flop_class(canonical_index);
    case card_abstraction::BucketStreet::Turn:
      return turn_class(canonical_index);
    case card_abstraction::BucketStreet::River:
      return river_class(canonical_index);
    }
    return 0U;
  }
  [[nodiscard]] std::uint32_t row(const card_abstraction::BucketStreet street,
                                  const std::uint32_t board_class,
                                  const std::uint16_t bucket) const noexcept {
    assert(board_class < classes(street) && bucket < groups(street));
    return board_class * groups(street) + bucket;
  }
  [[nodiscard]] const BoardTextureMap &texture() const noexcept { return texture_; }
  [[nodiscard]] const std::string &fingerprint() const noexcept { return fingerprint_; }

private:
  std::uint32_t flop_groups_;
  std::uint32_t turn_groups_;
  std::uint32_t river_groups_;
  BoardTextureMap texture_;
  std::string fingerprint_;
};

} // namespace gtosd::preflop_blueprint
