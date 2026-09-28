#include "gtosd/preflop_blueprint/board_texture.hpp"
#include "hashing.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace gtosd::preflop_blueprint {
namespace ca = card_abstraction;
namespace {

using MapResult = Result<BoardTextureMap, TextureError>;

constexpr std::string_view format_line = "gtosd-board-texture-v1";
constexpr std::string_view river_key_turn = "turn";
// Reserved for river textures keyed by the river board (not supported yet).
constexpr std::string_view river_key_river_board = "river-board";
constexpr std::uint64_t maximum_file_bytes = 16ULL * 1024 * 1024;

// Relabels the classes by first appearance and returns their count.
std::uint32_t relabel(std::vector<std::uint32_t> &classes) {
  std::unordered_map<std::uint32_t, std::uint32_t> labels;
  labels.reserve(classes.size());
  for (auto &value : classes) {
    const auto next = static_cast<std::uint32_t>(labels.size());
    value = labels.try_emplace(value, next).first->second;
  }
  return static_cast<std::uint32_t>(labels.size());
}

bool is_identity_labels(const std::vector<std::uint32_t> &classes) {
  for (std::size_t index = 0; index < classes.size(); ++index) {
    if (classes[index] != index) {
      return false;
    }
  }
  return true;
}

void append_u32(std::string &payload, const std::uint32_t value) {
  for (unsigned shift = 0; shift < 32U; shift += 8U) {
    payload.push_back(static_cast<char>((value >> shift) & 0xFFU));
  }
}

bool valid_name(const std::string_view name) {
  return !name.empty() && name.find_first_of("\r\n") == std::string_view::npos;
}

// Lines of the file without their terminators (LF, or CRLF after a checkout
// that converted the line ends); one final terminator is not a line.
std::vector<std::string_view> split_lines(const std::string_view text) {
  std::vector<std::string_view> lines;
  std::size_t begin = 0U;
  while (begin < text.size()) {
    auto end = text.find('\n', begin);
    const bool terminated = end != std::string_view::npos;
    if (!terminated) {
      end = text.size();
    }
    auto line = text.substr(begin, end - begin);
    if (terminated && !line.empty() && line.back() == '\r') {
      line.remove_suffix(1U);
    }
    lines.push_back(line);
    begin = end + 1U;
  }
  return lines;
}

bool parse_number(const std::string_view text, std::uint64_t &value) {
  if (text.empty()) {
    return false;
  }
  const auto *const last = text.data() + text.size();
  const auto parsed = std::from_chars(text.data(), last, value, 10);
  return parsed.ec == std::errc{} && parsed.ptr == last;
}

// "<code> <class>" with one space between two decimal numbers.
bool parse_entry(const std::string_view line, std::uint64_t &code, std::uint64_t &board_class) {
  const auto space = line.find(' ');
  return space != std::string_view::npos && parse_number(line.substr(0, space), code) &&
         parse_number(line.substr(space + 1U), board_class);
}

// Reads one section: "<name> <count>" then count entries whose codes equal
// the catalog's, with dense class ids below 65,536.
Result<std::vector<std::uint32_t>, TextureError>
read_section(const std::vector<std::string_view> &lines, std::size_t &cursor,
             const std::string_view name, const std::vector<std::uint32_t> &codes) {
  using Section = Result<std::vector<std::uint32_t>, TextureError>;
  const auto header = std::string(name) + " " + std::to_string(codes.size());
  if (cursor >= lines.size() || lines[cursor] != header ||
      lines.size() - cursor - 1U < codes.size()) {
    return Section::failure(TextureError::BadSection);
  }
  ++cursor;
  std::vector<std::uint32_t> classes(codes.size());
  std::vector<std::uint8_t> used(static_cast<std::size_t>(BoardTextureMap::maximum_class_id) + 1U,
                                 std::uint8_t{0});
  std::uint64_t largest = 0U;
  std::uint64_t distinct = 0U;
  for (std::size_t index = 0; index < codes.size(); ++index, ++cursor) {
    std::uint64_t code = 0U;
    std::uint64_t board_class = 0U;
    if (!parse_entry(lines[cursor], code, board_class)) {
      return Section::failure(TextureError::BadSection);
    }
    if (code != codes[index]) {
      return Section::failure(TextureError::CodeMismatch);
    }
    if (board_class > BoardTextureMap::maximum_class_id) {
      return Section::failure(TextureError::BadClass);
    }
    classes[index] = static_cast<std::uint32_t>(board_class);
    largest = std::max(largest, board_class);
    if (used[board_class] == 0U) {
      used[board_class] = 1U;
      ++distinct;
    }
  }
  if (distinct != largest + 1U) {
    return Section::failure(TextureError::BadClass);
  }
  return Section::success(std::move(classes));
}

std::vector<std::uint32_t> flop_codes(const ca::BoardCatalog &catalog) {
  std::vector<std::uint32_t> codes;
  codes.reserve(catalog.flops().size());
  for (const auto &flop : catalog.flops()) {
    codes.push_back(flop.code);
  }
  return codes;
}

std::vector<std::uint32_t> flop_turn_codes(const ca::BoardCatalog &catalog) {
  std::vector<std::uint32_t> codes;
  codes.reserve(catalog.flop_turns().size());
  for (const auto &flop_turn : catalog.flop_turns()) {
    codes.push_back(flop_turn.code);
  }
  return codes;
}

bool catalog_complete(const ca::BoardCatalog &catalog) {
  return catalog.flops().size() == ca::canonical_flop_count &&
         catalog.flop_turns().size() == ca::canonical_flop_turn_count;
}

} // namespace

const char *texture_error_name(const TextureError error) noexcept {
  switch (error) {
  case TextureError::IoFailure:
    return "io_failure";
  case TextureError::BadHeader:
    return "bad_header";
  case TextureError::BadSection:
    return "bad_section";
  case TextureError::CodeMismatch:
    return "code_mismatch";
  case TextureError::BadClass:
    return "bad_class";
  case TextureError::RiverMismatch:
    return "river_mismatch";
  case TextureError::Unsupported:
    return "unsupported";
  }
  return "unknown";
}

std::uint32_t BoardTextureMap::classes(const ca::BucketStreet street) const noexcept {
  return street == ca::BucketStreet::Flop ? flop_classes_ : turn_classes_;
}

Result<BoardTextureMap, TextureError>
BoardTextureMap::from_partition(std::vector<std::uint32_t> flop, std::vector<std::uint32_t> turn,
                                std::string name) {
  if (flop.size() != ca::canonical_flop_count || turn.size() != ca::canonical_flop_turn_count) {
    return MapResult::failure(TextureError::BadSection);
  }
  if (!valid_name(name)) {
    return MapResult::failure(TextureError::BadHeader);
  }
  BoardTextureMap map;
  map.name_ = std::move(name);
  const auto flop_classes = relabel(flop);
  const auto turn_classes = relabel(turn);
  if (is_identity_labels(flop) && is_identity_labels(turn)) {
    return MapResult::success(std::move(map));
  }
  // The fingerprint hashes the relabelled partition, not the name.
  std::string payload = "gtosd-board-texture-v1|river-key=turn|";
  payload.reserve(payload.size() + 4U * (2U + flop.size() + turn.size()));
  append_u32(payload, flop_classes);
  for (const auto value : flop) {
    append_u32(payload, value);
  }
  append_u32(payload, turn_classes);
  for (const auto value : turn) {
    append_u32(payload, value);
  }
  map.fingerprint_ = "texture=fnv1a64:" + detail::hex64_text(detail::fnv1a_text(payload)) +
                     "|classes=" + std::to_string(flop_classes) + "/" +
                     std::to_string(turn_classes) + "/" + std::to_string(turn_classes) +
                     "|river-key=turn";
  map.flop_ = std::move(flop);
  map.turn_ = std::move(turn);
  map.flop_classes_ = flop_classes;
  map.turn_classes_ = turn_classes;
  return MapResult::success(std::move(map));
}

Result<BoardTextureMap, TextureError> BoardTextureMap::load(const std::filesystem::path &path,
                                                            const ca::BoardCatalog &catalog) {
  if (!catalog_complete(catalog)) {
    return MapResult::failure(TextureError::BadSection);
  }
  std::error_code error;
  const auto bytes = std::filesystem::file_size(path, error);
  if (error || bytes > maximum_file_bytes) {
    return MapResult::failure(TextureError::IoFailure);
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return MapResult::failure(TextureError::IoFailure);
  }
  const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (input.bad()) {
    return MapResult::failure(TextureError::IoFailure);
  }
  const auto lines = split_lines(text);
  constexpr std::string_view name_prefix = "name ";
  constexpr std::string_view river_key_prefix = "river-key ";
  if (lines.size() < 3U || lines[0] != format_line || !lines[1].starts_with(name_prefix) ||
      !valid_name(lines[1].substr(name_prefix.size())) ||
      !lines[2].starts_with(river_key_prefix)) {
    return MapResult::failure(TextureError::BadHeader);
  }
  const auto river_key = lines[2].substr(river_key_prefix.size());
  if (river_key == river_key_river_board) {
    return MapResult::failure(TextureError::Unsupported);
  }
  if (river_key != river_key_turn) {
    return MapResult::failure(TextureError::BadHeader);
  }
  const auto turn_codes = flop_turn_codes(catalog);
  std::size_t cursor = 3U;
  auto flop = read_section(lines, cursor, "flop", flop_codes(catalog));
  if (!flop) {
    return MapResult::failure(flop.error());
  }
  auto turn = read_section(lines, cursor, "turn", turn_codes);
  if (!turn) {
    return MapResult::failure(turn.error());
  }
  const auto river = read_section(lines, cursor, "river", turn_codes);
  if (!river) {
    return MapResult::failure(river.error());
  }
  if (river.value() != turn.value()) {
    return MapResult::failure(TextureError::RiverMismatch);
  }
  if (cursor != lines.size()) {
    return MapResult::failure(TextureError::BadSection);
  }
  return from_partition(std::move(flop.value()), std::move(turn.value()),
                        std::string(lines[1].substr(name_prefix.size())));
}

Result<bool, TextureError> BoardTextureMap::save(const std::filesystem::path &path,
                                                 const ca::BoardCatalog &catalog) const {
  using Outcome = Result<bool, TextureError>;
  if (!catalog_complete(catalog)) {
    return Outcome::failure(TextureError::BadSection);
  }
  if (!valid_name(name_)) {
    return Outcome::failure(TextureError::BadHeader);
  }
  std::string text;
  text.reserve(512U * 1024U);
  text.append(format_line).append("\nname ").append(name_).append("\nriver-key ");
  text.append(river_key_turn).append("\n");
  const auto append_section = [&](const std::string_view section,
                                  const std::vector<std::uint32_t> &codes, const auto &class_of) {
    text.append(section).append(" ").append(std::to_string(codes.size())).append("\n");
    for (std::size_t index = 0; index < codes.size(); ++index) {
      text.append(std::to_string(codes[index]))
          .append(" ")
          .append(std::to_string(class_of(static_cast<std::uint32_t>(index))))
          .append("\n");
    }
  };
  const auto turn_codes = flop_turn_codes(catalog);
  append_section("flop", flop_codes(catalog),
                 [this](const std::uint32_t index) { return flop_class(index); });
  append_section("turn", turn_codes,
                 [this](const std::uint32_t index) { return turn_class(index); });
  append_section("river", turn_codes,
                 [this](const std::uint32_t index) { return river_class(index); });
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return Outcome::failure(TextureError::IoFailure);
  }
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  output.close();
  if (!output) {
    return Outcome::failure(TextureError::IoFailure);
  }
  return Outcome::success(true);
}

} // namespace gtosd::preflop_blueprint
