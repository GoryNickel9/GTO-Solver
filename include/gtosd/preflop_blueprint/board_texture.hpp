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
// (river key "turn"). The identity partition, every canonical board its own
// class, is the historical layout of BoardClassRows.
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
// Every code must equal the catalog's code at that position (a generator that
// orders the boards differently is rejected), the class ids of a section
// must be dense (every id below the class count used) and below 65,536.
namespace gtosd::preflop_blueprint {

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
  [[nodiscard]] static Result<BoardTextureMap, TextureError>
  load(const std::filesystem::path &path, const card_abstraction::BoardCatalog &catalog);
  [[nodiscard]] Result<bool, TextureError> save(const std::filesystem::path &path,
                                                const card_abstraction::BoardCatalog &catalog) const;

  [[nodiscard]] bool is_identity() const noexcept { return flop_.empty(); }
  // Identity: 573 / 13,761 / 13,761.
  [[nodiscard]] std::uint32_t classes(card_abstraction::BucketStreet street) const noexcept;
  [[nodiscard]] std::uint32_t flop_class(const std::uint32_t flop_index) const noexcept {
    return flop_.empty() ? flop_index : flop_[flop_index];
  }
  [[nodiscard]] std::uint32_t turn_class(const std::uint32_t flop_turn_index) const noexcept {
    return turn_.empty() ? flop_turn_index : turn_[flop_turn_index];
  }
  // A river shares the class of its turn: the argument is the canonical
  // flop+turn index of the river's history.
  [[nodiscard]] std::uint32_t river_class(const std::uint32_t flop_turn_index) const noexcept {
    return turn_class(flop_turn_index);
  }
  [[nodiscard]] const std::string &name() const noexcept { return name_; }
  // Identifies the partition (not the file or the name); empty for the identity.
  [[nodiscard]] const std::string &fingerprint() const noexcept { return fingerprint_; }

private:
  // Relabelled classes; both empty for the identity.
  std::vector<std::uint32_t> flop_;
  std::vector<std::uint32_t> turn_;
  std::uint32_t flop_classes_{card_abstraction::canonical_flop_count};
  std::uint32_t turn_classes_{card_abstraction::canonical_flop_turn_count};
  std::string name_{"identity"};
  std::string fingerprint_;
};

} // namespace gtosd::preflop_blueprint
