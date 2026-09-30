#pragma once

#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/core/result.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Board textures of the MonkerSolver-style rows (BoardClassRows): a partition
// of the canonical boards into classes that share their information rows.
// The flop classes partition the 573 canonical flops, the turn classes the
// 13,761 canonical flop+turn boards; a river uses the class of its turn
// (river key "turn") or, with river key "river-board", the class of its
// unordered five-card board among the 19,998 canonical river boards. The
// identity partition (river key "turn", every canonical board its own class)
// is the historical layout of BoardClassRows.
//
// Classes are relabelled by first appearance in catalog order, so two
// partitions that differ only by their labels are the same map (same
// fingerprint, same rows). The map is computed outside the library and
// loaded from a text file (format gtosd-board-texture-v1):
//
//   gtosd-board-texture-v1
//   name <name>
//   river-key turn
//   flop 573
//   <canonical flop code> <class>            573 lines, catalog order
//   turn 13761
//   <canonical flop+turn code> <class>       13,761 lines, catalog order
//   river 13761
//   <canonical flop+turn code> <class>       the same codes and classes as the turn
//
// or, with "river-key river-board" on the third line, a river section of the
// canonical five-card boards:
//
//   river 19998
//   <canonical river board code> <class>     19,998 lines, catalog order
//
// A river-board key forgets the order of the board cards (which card came on
// the river): it is lossless only in games without decisions before the river
// on the postflop streets (the correctness games of 30 September 2026, flop
// and turn checked through).
//
// Every code must equal the catalog's code at that position (a generator that
// orders the boards differently is rejected), the class ids of a section
// must be dense (every id below the class count used) and below 65,536.
namespace gtosd::preflop_blueprint {

// What the river class is keyed by.
enum class RiverKey : std::uint8_t { Turn, RiverBoard };

enum class TextureError : std::uint8_t {
  IoFailure,
  BadHeader,
  BadSection,
  CodeMismatch,
  BadClass,
  RiverMismatch,
  Unsupported
};

[[nodiscard]] const char *texture_error_name(TextureError error) noexcept;

class BoardTextureMap {
public:
  static constexpr std::uint32_t maximum_class_id = 65'535U;

  // The identity: every canonical board its own class.
  BoardTextureMap() = default;
  [[nodiscard]] static BoardTextureMap identity() { return BoardTextureMap{}; }
  // Flop classes of the 573 canonical flops and turn classes of the 13,761
  // canonical flop+turn boards, in catalog order (any labels; they are
  // relabelled by first appearance). The river follows the turn. A partition
  // equal to the identity is stored as the identity (the name is kept).
  [[nodiscard]] static Result<BoardTextureMap, TextureError>
  from_partition(std::vector<std::uint32_t> flop, std::vector<std::uint32_t> turn,
                 std::string name);
  // River key "river-board": flop and turn classes as in from_partition and
  // the classes of the 19,998 canonical five-card boards, in catalog order
  // (relabelled by first appearance). Never the identity, even when every
  // section is (its rows differ from the turn-keyed rows).
  [[nodiscard]] static Result<BoardTextureMap, TextureError>
  from_river_board_partition(std::vector<std::uint32_t> flop, std::vector<std::uint32_t> turn,
                             std::vector<std::uint32_t> river, std::string name);
  [[nodiscard]] static Result<BoardTextureMap, TextureError>
  load(const std::filesystem::path &path, const card_abstraction::BoardCatalog &catalog);
  [[nodiscard]] Result<bool, TextureError> save(const std::filesystem::path &path,
                                                const card_abstraction::BoardCatalog &catalog) const;

  [[nodiscard]] bool is_identity() const noexcept {
    return flop_.empty() && turn_.empty() && river_key_ == RiverKey::Turn;
  }
  [[nodiscard]] RiverKey river_key() const noexcept { return river_key_; }
  // Identity: 573 / 13,761 / 13,761; river key "river-board": the river
  // classes of the five-card boards (19,998 for the identity partition).
  [[nodiscard]] std::uint32_t classes(card_abstraction::BucketStreet street) const noexcept;
  [[nodiscard]] std::uint32_t flop_class(const std::uint32_t flop_index) const noexcept {
    return flop_.empty() ? flop_index : flop_[flop_index];
  }
  [[nodiscard]] std::uint32_t turn_class(const std::uint32_t flop_turn_index) const noexcept {
    return turn_.empty() ? flop_turn_index : turn_[flop_turn_index];
  }
  // River key "turn": a river shares the class of its turn; the argument is
  // the canonical flop+turn index of the river's history.
  [[nodiscard]] std::uint32_t river_class(const std::uint32_t flop_turn_index) const noexcept {
    return turn_class(flop_turn_index);
  }
  // River key "river-board": the class of a canonical five-card board.
  [[nodiscard]] std::uint32_t river_board_class(const std::uint32_t river_board_index) const noexcept {
    return river_.empty() ? river_board_index : river_[river_board_index];
  }
  // The river class of a history under either key, given its canonical
  // flop+turn index and its canonical five-card board index.
  [[nodiscard]] std::uint32_t river_class(const std::uint32_t flop_turn_index,
                                          const std::uint32_t river_board_index) const noexcept {
    return river_key_ == RiverKey::RiverBoard ? river_board_class(river_board_index)
                                              : river_class(flop_turn_index);
  }
  [[nodiscard]] const std::string &name() const noexcept { return name_; }
  // Identifies the partition (not the file or the name); empty for the identity.
  [[nodiscard]] const std::string &fingerprint() const noexcept { return fingerprint_; }

private:
  // Relabelled classes; empty for an identity section (all of them for the
  // identity map).
  std::vector<std::uint32_t> flop_;
  std::vector<std::uint32_t> turn_;
  // River key "river-board" only.
  std::vector<std::uint32_t> river_;
  RiverKey river_key_{RiverKey::Turn};
  std::uint32_t flop_classes_{card_abstraction::canonical_flop_count};
  std::uint32_t turn_classes_{card_abstraction::canonical_flop_turn_count};
  std::uint32_t river_board_classes_{card_abstraction::canonical_river_board_count};
  std::string name_{"identity"};
  std::string fingerprint_;
};

} // namespace gtosd::preflop_blueprint
