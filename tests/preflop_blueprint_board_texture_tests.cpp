// Board texture maps of the board class rows (board_texture.hpp,
// BoardClassRows): the identity map reproduces the rows, fingerprint and
// training state of the rows without a map bit for bit; partitions are
// relabelled by first appearance (labels do not matter, the partition does);
// the file format round-trips and malformed or mismatched files are refused;
// merged boards share their rows and nothing else; the rows keep the suit
// symmetry through BoardContext; the repository maps
// (benchmarks/monker/textures) cover every canonical turn with a valid class
// and match the documented class counts and examples; a trainer with a merged
// map has its own identity and refuses the checkpoints of other partitions.
// The best response with a texture is covered by the certifier tests
// (test_board_texture_rows).
#include "preflop_blueprint_test_support.hpp"

#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/preflop_blueprint/board_class_rows.hpp"
#include "gtosd/preflop_blueprint/board_context.hpp"
#include "gtosd/preflop_blueprint/board_texture.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace pb_test;

constexpr std::uint32_t flop_count = ca::canonical_flop_count;
constexpr std::uint32_t turn_count = ca::canonical_flop_turn_count;
constexpr std::array<ca::BucketStreet, 3> bucket_streets{
    ca::BucketStreet::Flop, ca::BucketStreet::Turn, ca::BucketStreet::River};
constexpr std::array<gtosd::Street, 3> postflop_streets{gtosd::Street::Flop, gtosd::Street::Turn,
                                                        gtosd::Street::River};

std::filesystem::path texture_directory() {
  return std::filesystem::path(GTOSD_SOURCE_DIR) / "benchmarks" / "monker" / "textures";
}

// Per-board bucket table with the buckets of `source` taken modulo
// `capacity` (as in the certifier tests): a function of the canonical combo
// like the source, small enough for a board-class layout of HU10.
ca::BucketTable folded_table(const Resources &resources, const ca::BucketTable &source,
                             const std::uint16_t capacity) {
  std::vector<std::uint16_t> buckets = source.buckets();
  for (auto &bucket : buckets) {
    if (bucket != ca::no_bucket) {
      bucket = static_cast<std::uint16_t>(bucket % capacity);
    }
  }
  ca::ClusteringParameters parameters;
  parameters.capacity = capacity;
  std::vector<std::uint16_t> centroids(
      static_cast<std::size_t>(capacity) * source.centroid_width(), std::uint16_t{0});
  auto table = ca::BucketTable::from_assignment(
      source.street(), *resources.catalog, parameters, std::move(buckets), std::move(centroids),
      "texture-test-folded-" + std::to_string(capacity));
  require(table.has_value(), "folded bucket table builds");
  return std::move(table.value());
}

struct FoldedTables {
  ca::BucketTable flop;
  ca::BucketTable turn;
  ca::BucketTable river;
};

std::vector<std::uint32_t> iota_labels(const std::uint32_t count, const std::uint32_t offset = 0U) {
  std::vector<std::uint32_t> labels(count);
  std::iota(labels.begin(), labels.end(), offset);
  return labels;
}

// Every turn of a flop in one class: 573 turn classes.
std::vector<std::uint32_t> turn_as_flop_labels(const ca::BoardCatalog &catalog) {
  std::vector<std::uint32_t> labels;
  labels.reserve(catalog.flop_turns().size());
  for (const auto &entry : catalog.flop_turns()) {
    labels.push_back(entry.flop_index);
  }
  return labels;
}

pb::BoardTextureMap make_partition(std::vector<std::uint32_t> flop, std::vector<std::uint32_t> turn,
                              std::string name) {
  auto map = pb::BoardTextureMap::from_partition(std::move(flop), std::move(turn), std::move(name));
  require(map.has_value(), "partition is accepted");
  return std::move(map.value());
}

pb::BoardTextureMap turn_as_flop(const ca::BoardCatalog &catalog) {
  return make_partition(iota_labels(flop_count), turn_as_flop_labels(catalog), "turn_as_flop");
}

pb::BoardTextureMap load_map(const std::filesystem::path &path, const ca::BoardCatalog &catalog) {
  auto map = pb::BoardTextureMap::load(path, catalog);
  require(map.has_value(), "texture map loads: " + path.string());
  return std::move(map.value());
}

bool same_partition(const pb::BoardTextureMap &left, const pb::BoardTextureMap &right) {
  for (const auto street : bucket_streets) {
    if (left.classes(street) != right.classes(street)) {
      return false;
    }
  }
  for (std::uint32_t index = 0; index < flop_count; ++index) {
    if (left.flop_class(index) != right.flop_class(index)) {
      return false;
    }
  }
  for (std::uint32_t index = 0; index < turn_count; ++index) {
    if (left.turn_class(index) != right.turn_class(index) ||
        left.river_class(index) != right.river_class(index)) {
      return false;
    }
  }
  return left.fingerprint() == right.fingerprint() && left.is_identity() == right.is_identity();
}

pb::AbstractionTables abstraction_tables(const Resources &resources, const FoldedTables &tables,
                                         const pb::BoardClassRows &rows) {
  pb::AbstractionTables view;
  view.catalog = &*resources.catalog;
  view.flop = &tables.flop;
  view.turn = &tables.turn;
  view.river = &tables.river;
  view.board_class_rows = &rows;
  return view;
}

// Physical history with the suits permuted (flop re-sorted).
ca::BoardHistory permuted_history(const ca::BoardHistory &history,
                                  const ca::SuitPermutation &permutation) {
  ca::BoardHistory permuted;
  for (std::size_t index = 0; index < 3U; ++index) {
    permuted.flop[index] = ca::permute_card(history.flop[index], permutation);
  }
  std::sort(permuted.flop.begin(), permuted.flop.end());
  permuted.turn = ca::permute_card(history.turn, permutation);
  permuted.river = ca::permute_card(history.river, permutation);
  return permuted;
}

std::string read_text(const std::filesystem::path &path) { return read_file(path); }

void write_text(const std::filesystem::path &path, const std::string_view text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  require(static_cast<bool>(output), "scratch file writes");
}

std::vector<std::string> split_lines(const std::string &text) {
  std::vector<std::string> lines;
  std::size_t begin = 0U;
  while (begin < text.size()) {
    const auto end = text.find('\n', begin);
    lines.push_back(text.substr(begin, end - begin));
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1U;
  }
  return lines;
}

std::string join_lines(const std::vector<std::string> &lines) {
  std::string text;
  for (const auto &line : lines) {
    text += line;
    text += '\n';
  }
  return text;
}

// "<code> <class>" with another class.
std::string with_class(const std::string &line, const std::uint64_t board_class) {
  return line.substr(0, line.find(' ')) + " " + std::to_string(board_class);
}

std::uint64_t class_of_line(const std::string &line) {
  return std::stoull(line.substr(line.find(' ') + 1U));
}

// T1: the identity map is bit-identical to the rows without a map.
void test_identity(const Resources &resources, const FoldedTables &tables,
                   const std::filesystem::path &scratch) {
  const auto started = Clock::now();
  const auto &catalog = *resources.catalog;
  const pb::BoardClassRows legacy(120U, 120U, 30U);
  require(legacy.fingerprint() == "board-class-rows-v1|flop=120|turn=120|river=turn-class*30",
          "rows without a map keep the historical fingerprint");
  require(legacy.texture().is_identity() && legacy.texture().fingerprint().empty() &&
              legacy.texture().name() == "identity",
          "rows without a map use the identity texture");
  require(legacy.classes(ca::BucketStreet::Flop) == flop_count &&
              legacy.classes(ca::BucketStreet::Turn) == turn_count &&
              legacy.classes(ca::BucketStreet::River) == turn_count &&
              legacy.count(ca::BucketStreet::Flop) == 68'760U &&
              legacy.count(ca::BucketStreet::Turn) == 1'651'320U &&
              legacy.count(ca::BucketStreet::River) == 412'830U,
          "identity classes and HU50 step 2 capacities");

  // The identity partition, with any labels in the same order.
  const auto identity = make_partition(iota_labels(flop_count), iota_labels(turn_count), "identity");
  const auto shifted = make_partition(iota_labels(flop_count, 1'000U), iota_labels(turn_count, 7U),
                                 "identity_shifted_labels");
  require(identity.is_identity() && identity.fingerprint().empty() && shifted.is_identity() &&
              shifted.name() == "identity_shifted_labels",
          "an identity partition is stored as the identity and keeps its name");
  const auto path = scratch / "identity_roundtrip.txt";
  require(identity.save(path, catalog).has_value(), "identity map saves");
  const auto reloaded = load_map(path, catalog);
  const auto repository = load_map(texture_directory() / "identity_texture_map.txt", catalog);
  require(read_text(path) == read_text(texture_directory() / "identity_texture_map.txt"),
          "the saved identity map equals the repository file byte for byte");
  for (const auto *map : {&identity, &shifted, &reloaded, &repository}) {
    require(map->is_identity() && same_partition(*map, legacy.texture()),
            "identity maps are the identity partition");
    const pb::BoardClassRows rows(120U, 120U, 30U, *map);
    require(rows.fingerprint() == legacy.fingerprint(),
            "the identity map keeps the historical fingerprint");
    for (const auto street : bucket_streets) {
      require(rows.count(street) == legacy.count(street), "identity map keeps the capacities");
    }
  }

  // Rows of the BoardContext: canonical index * groups + bucket, with and
  // without the identity map.
  const pb::BoardClassRows plain_rows(4U, 5U, 6U);
  const pb::BoardClassRows identity_rows(4U, 5U, 6U, repository);
  const auto plain_view = abstraction_tables(resources, tables, plain_rows);
  const auto identity_view = abstraction_tables(resources, tables, identity_rows);
  ca::DeterministicRandom random(0x5445'5854'0001ULL);
  std::uint64_t compared = 0U;
  constexpr int histories = 300;
  for (int draw = 0; draw < histories; ++draw) {
    const auto history = catalog.sample_physical_history(random);
    const auto plain = pb::BoardContext::build(history, *resources.ranks, &plain_view);
    const auto mapped = pb::BoardContext::build(history, *resources.ranks, &identity_view);
    require(plain.has_value() && mapped.has_value(), "contexts with board class rows build");
    const auto flop_index = catalog.lookup_flop(history.flop).value().index;
    const auto turn_index = catalog.lookup_flop_turn(history.flop, history.turn).value().index;
    const std::array<std::uint32_t, 3> classes{flop_index, turn_index, turn_index};
    for (std::size_t street = 0; street < 3U; ++street) {
      const auto buckets = plain.value().buckets(postflop_streets[street]);
      const auto groups = plain_rows.groups(bucket_streets[street]);
      for (std::uint16_t hand = 0; hand < pb::live_hand_count; ++hand) {
        const auto expected = classes[street] * groups + buckets[hand];
        require(plain.value().row(postflop_streets[street], hand) == expected,
                "rows without a map are canonical index * groups + bucket");
        require(mapped.value().row(postflop_streets[street], hand) == expected,
                "rows with the identity map are canonical index * groups + bucket");
        ++compared;
      }
    }
  }
  std::cout << "identity: fingerprint " << legacy.fingerprint()
            << " with and without the map, file round trip byte-identical, " << compared
            << " context rows equal on " << histories << " histories, "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

// T2: partitions and relabelling.
void test_partition(const Resources &resources) {
  const auto &catalog = *resources.catalog;
  const auto merged = turn_as_flop(catalog);
  require(!merged.is_identity() && merged.classes(ca::BucketStreet::Flop) == flop_count &&
              merged.classes(ca::BucketStreet::Turn) == flop_count &&
              merged.classes(ca::BucketStreet::River) == flop_count,
          "turn-as-flop has 573/573/573 classes");
  require(merged.fingerprint().starts_with("texture=fnv1a64:") &&
              merged.fingerprint().ends_with("|classes=573/573/573|river-key=turn"),
          "texture fingerprint format");
  std::vector<std::uint8_t> used(flop_count, 0U);
  for (std::uint32_t index = 0; index < turn_count; ++index) {
    const auto board_class = merged.turn_class(index);
    require(board_class == catalog.flop_turns()[index].flop_index &&
                merged.river_class(index) == board_class,
            "turn-as-flop classes are the flop indices (first appearance) and the river follows");
    used[board_class] = 1U;
  }
  for (std::uint32_t index = 0; index < flop_count; ++index) {
    require(merged.flop_class(index) == index, "flop classes stay the canonical flops");
  }
  require(std::all_of(used.begin(), used.end(), [](const auto value) { return value != 0U; }),
          "every turn class is used");

  // The same partition under other labels, flop labels permuted too.
  std::vector<std::uint32_t> relabel_flop(flop_count);
  std::vector<std::uint32_t> relabel_turn(turn_count);
  for (std::uint32_t index = 0; index < flop_count; ++index) {
    relabel_flop[index] = 50'000U + 2U * (flop_count - 1U - index);
  }
  for (std::uint32_t index = 0; index < turn_count; ++index) {
    relabel_turn[index] = 65'000U - 3U * catalog.flop_turns()[index].flop_index;
  }
  const auto relabelled = make_partition(relabel_flop, relabel_turn, "turn_as_flop_relabelled");
  require(same_partition(relabelled, merged) && relabelled.name() != merged.name(),
          "labels do not matter: same classes and fingerprint, the name is not hashed");
  const pb::BoardClassRows rows(4U, 5U, 6U, merged);
  const pb::BoardClassRows relabelled_rows(4U, 5U, 6U, relabelled);
  require(rows.fingerprint() == relabelled_rows.fingerprint() &&
              rows.fingerprint() == "board-class-rows-v2|flop=4|turn=5|river=6|" +
                                        merged.fingerprint(),
          "rows fingerprint of a texture");
  for (const auto street : bucket_streets) {
    const auto count = street == ca::BucketStreet::Flop ? flop_count : turn_count;
    for (std::uint32_t index = 0; index < count; ++index) {
      for (std::uint16_t bucket = 0; bucket < rows.groups(street); ++bucket) {
        require(rows.row(street, rows.board_class(street, index), bucket) ==
                    relabelled_rows.row(street, relabelled_rows.board_class(street, index), bucket),
                "relabelled partition gives the same rows");
      }
    }
  }

  // One turn moved to another class: same counts, another partition.
  auto moved_labels = turn_as_flop_labels(catalog);
  std::uint32_t moved = 0U;
  while (catalog.flop_turns()[moved + 1U].flop_index == 0U) {
    ++moved;
  }
  moved_labels[moved] = 1U; // the last turn of flop 0 joins the turns of flop 1
  const auto other = make_partition(iota_labels(flop_count), moved_labels, "moved_one_turn");
  require(other.classes(ca::BucketStreet::Turn) == flop_count &&
              other.fingerprint() != merged.fingerprint(),
          "moving one turn keeps the counts and changes the fingerprint");
  require(pb::BoardClassRows(4U, 5U, 6U, other).fingerprint() != rows.fingerprint(),
          "moving one turn changes the rows fingerprint");

  // Rejected partitions.
  auto short_turn = turn_as_flop_labels(catalog);
  short_turn.pop_back();
  const auto wrong_size = pb::BoardTextureMap::from_partition(iota_labels(flop_count),
                                                              std::move(short_turn), "short");
  require(!wrong_size.has_value() && wrong_size.error() == pb::TextureError::BadSection,
          "a partition of the wrong size is rejected");
  const auto no_name = pb::BoardTextureMap::from_partition(iota_labels(flop_count),
                                                           turn_as_flop_labels(catalog), "");
  require(!no_name.has_value() && no_name.error() == pb::TextureError::BadHeader,
          "a partition without a name is rejected");
  const auto newline_name = pb::BoardTextureMap::from_partition(
      iota_labels(flop_count), turn_as_flop_labels(catalog), "two\nlines");
  require(!newline_name.has_value() && newline_name.error() == pb::TextureError::BadHeader,
          "a name with a line break is rejected");
  std::cout << "partition: turn-as-flop 573/573/573 classes (" << merged.fingerprint()
            << "), relabelling invariant, one moved turn changes the fingerprint\n";
}

// T3: file format, round trip and rejections.
void test_file_format(const Resources &resources, const std::filesystem::path &scratch) {
  const auto &catalog = *resources.catalog;
  const auto merged = turn_as_flop(catalog);
  const auto path = scratch / "turn_as_flop.txt";
  require(merged.save(path, catalog).has_value(), "turn-as-flop map saves");
  const auto reloaded = load_map(path, catalog);
  require(same_partition(reloaded, merged) && reloaded.name() == "turn_as_flop",
          "turn-as-flop round trip");
  const auto resaved = scratch / "turn_as_flop_resaved.txt";
  require(reloaded.save(resaved, catalog).has_value() && read_text(resaved) == read_text(path),
          "save of a loaded map is byte-identical");
  const auto text = read_text(path);
  const auto lines = split_lines(text);
  // Layout: header, name, river key, "flop 573" (3), flop lines 4..576,
  // "turn 13761" (577), turn lines 578..14338, "river 13761" (14339), river
  // lines 14340..28100.
  constexpr std::size_t flop_header = 3U;
  constexpr std::size_t turn_header = flop_header + 1U + flop_count;
  constexpr std::size_t river_header = turn_header + 1U + turn_count;
  require(lines.size() == river_header + 1U + turn_count && lines[0] == "gtosd-board-texture-v1" &&
              lines[1] == "name turn_as_flop" && lines[2] == "river-key turn" &&
              lines[flop_header] == "flop 573" && lines[turn_header] == "turn 13761" &&
              lines[river_header] == "river 13761",
          "file layout");
  {
    std::string crlf;
    for (const auto &line : lines) {
      crlf += line + "\r\n";
    }
    const auto crlf_path = scratch / "turn_as_flop_crlf.txt";
    write_text(crlf_path, crlf);
    require(same_partition(load_map(crlf_path, catalog), merged),
            "CRLF line ends (a checkout conversion) load the same map");
  }

  std::uint32_t rejected = 0U;
  const auto expect = [&](const std::string &case_name, const std::string &body,
                          const pb::TextureError error) {
    const auto bad_path = scratch / ("bad_" + case_name + ".txt");
    write_text(bad_path, body);
    const auto loaded = pb::BoardTextureMap::load(bad_path, catalog);
    require(!loaded.has_value(), "malformed map is rejected: " + case_name);
    require(loaded.error() == error, "malformed map error " + case_name + ": got " +
                                         pb::texture_error_name(loaded.error()) + ", expected " +
                                         pb::texture_error_name(error));
    ++rejected;
  };
  const auto edited = [&](const auto &edit) {
    auto copy = lines;
    edit(copy);
    return join_lines(copy);
  };
  {
    const auto missing = pb::BoardTextureMap::load(scratch / "no_such_map.txt", catalog);
    require(!missing.has_value() && missing.error() == pb::TextureError::IoFailure,
            "a missing file is an I/O failure");
    ++rejected;
  }
  expect("empty", "", pb::TextureError::BadHeader);
  expect("header", edited([](auto &copy) { copy[0] = "gtosd-board-texture-v2"; }),
         pb::TextureError::BadHeader);
  expect("no_name", edited([](auto &copy) { copy[1] = "name "; }), pb::TextureError::BadHeader);
  expect("river_key_unknown", edited([](auto &copy) { copy[2] = "river-key flop"; }),
         pb::TextureError::BadHeader);
  expect("river_key_river_board", edited([](auto &copy) { copy[2] = "river-key river-board"; }),
         pb::TextureError::Unsupported);
  expect("section_size", edited([&](auto &copy) {
           copy[flop_header] = "flop 572";
           copy.erase(copy.begin() + static_cast<std::ptrdiff_t>(flop_header + 1U));
         }),
         pb::TextureError::BadSection);
  expect("section_name", edited([&](auto &copy) { copy[turn_header] = "turns 13761"; }),
         pb::TextureError::BadSection);
  expect("section_order", edited([&](auto &copy) {
           // The turn and river headers swapped: the sections come in the wrong order.
           std::swap(copy[turn_header], copy[river_header]);
         }),
         pb::TextureError::BadSection);
  expect("truncated", edited([&](auto &copy) { copy.resize(river_header + 10U); }),
         pb::TextureError::BadSection);
  expect("malformed_entry", edited([&](auto &copy) { copy[turn_header + 5U] = "12x 3"; }),
         pb::TextureError::BadSection);
  expect("two_spaces", edited([&](auto &copy) {
           copy[turn_header + 5U] =
               copy[turn_header + 5U].substr(0, copy[turn_header + 5U].find(' ')) + "  4";
         }),
         pb::TextureError::BadSection);
  expect("swapped_codes", edited([&](auto &copy) {
           std::swap(copy[turn_header + 100U], copy[turn_header + 101U]);
           std::swap(copy[river_header + 100U], copy[river_header + 101U]);
         }),
         pb::TextureError::CodeMismatch);
  expect("swapped_flop_codes",
         edited([&](auto &copy) { std::swap(copy[flop_header + 1U], copy[flop_header + 2U]); }),
         pb::TextureError::CodeMismatch);
  expect("class_too_large", edited([&](auto &copy) {
           copy[turn_header + 1U] = with_class(copy[turn_header + 1U], 65'536U);
           copy[river_header + 1U] = with_class(copy[river_header + 1U], 65'536U);
         }),
         pb::TextureError::BadClass);
  expect("class_gap", edited([&](auto &copy) {
           // Class 572 renamed 573 in both sections: ids 0..571 and 573.
           for (const auto header : {turn_header, river_header}) {
             for (std::size_t line = header + 1U; line <= header + turn_count; ++line) {
               if (class_of_line(copy[line]) == 572U) {
                 copy[line] = with_class(copy[line], 573U);
               }
             }
           }
         }),
         pb::TextureError::BadClass);
  expect("flop_class_gap", edited([&](auto &copy) {
           copy[flop_header + 1U] = with_class(copy[flop_header + 1U], 600U);
         }),
         pb::TextureError::BadClass);
  expect("river_mismatch", edited([&](auto &copy) {
           // The first river moved from class 0 to class 1: both sections dense.
           copy[river_header + 1U] = with_class(copy[river_header + 1U], 1U);
         }),
         pb::TextureError::RiverMismatch);
  expect("trailing_line", text + "extra\n", pb::TextureError::BadSection);
  expect("trailing_empty_line", text + "\n", pb::TextureError::BadSection);
  {
    // A map written for another catalog order: every code of a valid file
    // shifted by one position is a mismatch at the first line.
    auto copy = lines;
    std::rotate(copy.begin() + static_cast<std::ptrdiff_t>(turn_header + 1U),
                copy.begin() + static_cast<std::ptrdiff_t>(turn_header + 2U),
                copy.begin() + static_cast<std::ptrdiff_t>(turn_header + 1U + turn_count));
    expect("rotated_codes", join_lines(copy), pb::TextureError::CodeMismatch);
  }
  std::cout << "file format: round trip byte-identical, CRLF accepted, " << rejected
            << " malformed files rejected with their error\n";
}

// T4: merged boards share their rows, different classes never do.
void test_shared_rows(const Resources &resources, const FoldedTables &tables) {
  const auto &catalog = *resources.catalog;
  const auto merged = turn_as_flop(catalog);
  const pb::BoardClassRows rows(4U, 5U, 6U, merged);
  require(rows.count(ca::BucketStreet::Flop) == flop_count * 4U &&
              rows.count(ca::BucketStreet::Turn) == flop_count * 5U &&
              rows.count(ca::BucketStreet::River) == flop_count * 6U,
          "turn-as-flop capacities are 573 * groups");
  for (const auto street : {ca::BucketStreet::Turn, ca::BucketStreet::River}) {
    std::vector<std::int64_t> owner(rows.count(street), -1);
    for (std::uint32_t index = 0; index < turn_count; ++index) {
      const auto board_class = rows.board_class(street, index);
      for (std::uint16_t bucket = 0; bucket < rows.groups(street); ++bucket) {
        const auto row = rows.row(street, board_class, bucket);
        require(row < rows.count(street), "every row is below the capacity");
        require(owner[row] == -1 || owner[row] == static_cast<std::int64_t>(board_class),
                "rows of different classes are disjoint");
        owner[row] = static_cast<std::int64_t>(board_class);
      }
    }
    require(std::none_of(owner.begin(), owner.end(), [](const auto value) { return value < 0; }),
            "every row of the capacity belongs to a class");
  }

  // Two histories on the same physical flop with different turns: a hand
  // with the same bucket has the same turn and river row under turn-as-flop,
  // and different rows without a texture (different canonical turns).
  const pb::BoardClassRows plain_rows(4U, 5U, 6U);
  const auto merged_view = abstraction_tables(resources, tables, rows);
  const auto plain_view = abstraction_tables(resources, tables, plain_rows);
  ca::DeterministicRandom random(0x5445'5854'0004ULL);
  std::uint64_t shared = 0U;
  std::uint64_t separated = 0U;
  for (int draw = 0; draw < 100; ++draw) {
    const auto first = catalog.sample_physical_history(random);
    auto second = first;
    const auto used = [&](const gtosd::CardId card) {
      return card == first.flop[0] || card == first.flop[1] || card == first.flop[2] ||
             card == first.turn;
    };
    do {
      second.turn = gtosd::CardId::from_index(static_cast<std::uint8_t>(random.uniform_below(36U)))
                        .value();
    } while (used(second.turn));
    do {
      second.river =
          gtosd::CardId::from_index(static_cast<std::uint8_t>(random.uniform_below(36U))).value();
    } while (used(second.river) || second.river == second.turn);
    const auto same_turn = catalog.lookup_flop_turn(first.flop, first.turn).value().index ==
                           catalog.lookup_flop_turn(second.flop, second.turn).value().index;
    const auto merged_first = pb::BoardContext::build(first, *resources.ranks, &merged_view);
    const auto merged_second = pb::BoardContext::build(second, *resources.ranks, &merged_view);
    const auto plain_first = pb::BoardContext::build(first, *resources.ranks, &plain_view);
    const auto plain_second = pb::BoardContext::build(second, *resources.ranks, &plain_view);
    require(merged_first && merged_second && plain_first && plain_second, "contexts build");
    for (std::uint16_t hand = 0; hand < pb::live_hand_count; ++hand) {
      const auto combo = merged_first.value().combo_ids()[hand];
      const auto other = merged_second.value().hand_index(combo);
      if (other == pb::no_hand) {
        continue;
      }
      for (const auto street : {gtosd::Street::Turn, gtosd::Street::River}) {
        const auto bucket = merged_first.value().buckets(street)[hand];
        if (bucket != merged_second.value().buckets(street)[other]) {
          continue;
        }
        require(merged_first.value().row(street, hand) == merged_second.value().row(street, other),
                "turns of one class share the rows of a bucket");
        ++shared;
        if (!same_turn) {
          require(plain_first.value().row(street, hand) != plain_second.value().row(street, other),
                  "without a texture different canonical turns have different rows");
          ++separated;
        }
      }
    }
  }
  require(shared > 0U && separated > 0U, "shared rows were compared");
  std::cout << "shared rows: turn and river rows disjoint across classes and cover the "
               "capacity, "
            << shared << " equal-bucket rows shared across turns of one flop (" << separated
            << " separate without a texture)\n";
}

// T5: suit symmetry of the rows through BoardContext.
// Rows of a map whose turn classes span several flops (TX2): the flop row uses
// the flop's class, the turn and river rows the class of the flop+turn board
// (river-key turn), each times its street's groups plus the bucket of the
// board. A hook reading the flop's class on the turn or river fails here,
// while the turn-as-flop partition of the evaluator test cannot tell them apart.
void test_texture_row_formula(const Resources &resources, const FoldedTables &tables,
                              const pb::BoardTextureMap &map) {
  const auto started = Clock::now();
  const auto &catalog = *resources.catalog;
  const pb::BoardClassRows plain_rows(4U, 5U, 6U);
  const pb::BoardClassRows rows(4U, 5U, 6U, map);
  const auto plain_view = abstraction_tables(resources, tables, plain_rows);
  const auto view = abstraction_tables(resources, tables, rows);
  ca::DeterministicRandom random(0x5445'5854'0006ULL);
  std::uint64_t compared = 0U;
  std::uint64_t turn_differs_from_flop = 0U;
  constexpr int histories = 300;
  for (int draw = 0; draw < histories; ++draw) {
    const auto history = catalog.sample_physical_history(random);
    const auto plain = pb::BoardContext::build(history, *resources.ranks, &plain_view);
    const auto mapped = pb::BoardContext::build(history, *resources.ranks, &view);
    require(plain.has_value() && mapped.has_value(), "contexts with a texture map build");
    const auto flop_index = catalog.lookup_flop(history.flop).value().index;
    const auto turn_index = catalog.lookup_flop_turn(history.flop, history.turn).value().index;
    const std::array<std::uint32_t, 3> classes{map.flop_class(flop_index),
                                               map.turn_class(turn_index),
                                               map.river_class(turn_index)};
    require(classes[2] == classes[1], "the river uses the class of its turn");
    if (classes[1] != classes[0])
      ++turn_differs_from_flop;
    for (std::size_t street = 0; street < 3U; ++street) {
      const auto buckets = plain.value().buckets(postflop_streets[street]);
      const auto groups = rows.groups(bucket_streets[street]);
      for (std::uint16_t hand = 0; hand < pb::live_hand_count; ++hand) {
        require(mapped.value().row(postflop_streets[street], hand) ==
                    classes[street] * groups + buckets[hand],
                "rows with a texture map are the street's class * groups + bucket");
        ++compared;
      }
    }
  }
  require(turn_differs_from_flop > 0U, "the map separates turn classes from flop classes");
  std::cout << "row formula (" << map.name() << "): " << compared << " context rows on "
            << histories << " histories (" << turn_differs_from_flop
            << " with a turn class different from the flop class), "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

void test_suit_symmetry(const Resources &resources, const FoldedTables &tables,
                        const std::vector<const pb::BoardTextureMap *> &maps) {
  const auto started = Clock::now();
  const auto &catalog = *resources.catalog;
  std::uint64_t compared = 0U;
  for (const auto *map : maps) {
    const pb::BoardClassRows rows(4U, 5U, 6U, *map);
    const auto view = abstraction_tables(resources, tables, rows);
    ca::DeterministicRandom random(0x5445'5854'0005ULL);
    for (int draw = 0; draw < 200; ++draw) {
      const auto history = catalog.sample_physical_history(random);
      const auto context = pb::BoardContext::build(history, *resources.ranks, &view);
      require(context.has_value(), "context builds");
      for (const auto &permutation : ca::all_suit_permutations()) {
        const auto image = pb::BoardContext::build(permuted_history(history, permutation),
                                                   *resources.ranks, &view);
        require(image.has_value(), "permuted context builds");
        for (std::uint16_t hand = 0; hand < pb::live_hand_count; ++hand) {
          const auto &cards = context.value().cards()[hand];
          const auto first =
              ca::permute_card(gtosd::CardId::from_index(cards[0]).value(), permutation);
          const auto second =
              ca::permute_card(gtosd::CardId::from_index(cards[1]).value(), permutation);
          const auto permuted_hand = image.value().hand_index(ca::combo_index(first, second));
          require(permuted_hand != pb::no_hand, "permuted hand is live on the permuted board");
          for (const auto street : postflop_streets) {
            if (context.value().row(street, hand) != image.value().row(street, permuted_hand)) {
              require(false, "rows are invariant under suit permutations: " + map->name());
            }
          }
          compared += 3U;
        }
      }
    }
  }
  std::cout << "suit symmetry: " << compared << " rows equal under the 24 permutations ("
            << maps.size() << " maps x 200 histories), "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

struct RepositoryMap {
  std::string file;
  std::string name;
  std::uint32_t turn_classes;
  // Canonical turns in the largest class.
  std::uint32_t largest_class;
  // Turn classes among the canonical turns of the example flops.
  std::array<std::uint32_t, 4> example_classes;
};

// Distinct turn classes among the canonical turns of one flop.
std::uint32_t classes_of_flop(const ca::BoardCatalog &catalog, const pb::BoardTextureMap &map,
                              const std::array<std::string_view, 3> &cards) {
  std::array<gtosd::CardId, 3> flop{card(cards[0]), card(cards[1]), card(cards[2])};
  std::sort(flop.begin(), flop.end());
  const auto flop_index = catalog.lookup_flop(flop).value().index;
  std::set<std::uint32_t> classes;
  for (std::uint32_t index = 0; index < turn_count; ++index) {
    if (catalog.flop_turns()[index].flop_index == flop_index) {
      classes.insert(map.turn_class(index));
    }
  }
  return static_cast<std::uint32_t>(classes.size());
}

// The maps of benchmarks/monker/textures: valid classes for every canonical
// turn, the documented counts and examples, suit-permuted physical boards in
// the same class.
std::vector<pb::BoardTextureMap> test_repository_maps(const Resources &resources) {
  const auto &catalog = *resources.catalog;
  // Examples (research brief of 28 September 2026, section 1.5): KsKh6d
  // (paired), Ks9h6d (rainbow), Ks9s6d (two-tone), Th9h8d (wet two-tone).
  const std::array<std::array<std::string_view, 3>, 4> example_flops{
      {{"Ks", "Kh", "6d"}, {"Ks", "9h", "6d"}, {"Ks", "9s", "6d"}, {"Th", "9h", "8d"}}};
  const std::vector<RepositoryMap> expected{
      {"identity_texture_map.txt", "identity", turn_count, 1U, {25U, 33U, 24U, 24U}},
      {"texture_map_TX0_suit_only.txt", "TX0_suit_only", 8'217U, 3U, {18U, 18U, 24U, 24U}},
      {"texture_map_TX1_fallback.txt", "TX1_fallback", 6'768U, 9U, {10U, 16U, 21U, 21U}},
      {"texture_map_TX2_recommended.txt", "TX2_recommended", 4'482U, 18U, {5U, 8U, 14U, 14U}},
      {"texture_map_TX3_aggressive.txt", "TX3_aggressive", 2'680U, 36U, {3U, 4U, 6U, 10U}}};
  std::vector<pb::BoardTextureMap> maps;
  std::set<std::string> fingerprints;
  for (const auto &entry : expected) {
    auto map = load_map(texture_directory() / entry.file, catalog);
    require(map.name() == entry.name, "map name: " + entry.file);
    require(map.is_identity() == (entry.name == "identity") &&
                map.classes(ca::BucketStreet::Flop) == flop_count &&
                map.classes(ca::BucketStreet::Turn) == entry.turn_classes &&
                map.classes(ca::BucketStreet::River) == entry.turn_classes,
            "map class counts: " + entry.file);
    bool flop_identity = true;
    for (std::uint32_t index = 0; index < flop_count; ++index) {
      flop_identity = flop_identity && map.flop_class(index) == index;
    }
    require(flop_identity, "flop section is the identity: " + entry.file);
    std::vector<std::uint32_t> sizes(entry.turn_classes, 0U);
    bool valid = true;
    for (std::uint32_t index = 0; index < turn_count && valid; ++index) {
      const auto board_class = map.turn_class(index);
      valid = board_class < entry.turn_classes && map.river_class(index) == board_class;
      if (valid) {
        ++sizes[board_class];
      }
    }
    require(valid, "every canonical turn has a valid class and the river follows it: " +
                       entry.file);
    require(std::none_of(sizes.begin(), sizes.end(), [](const auto size) { return size == 0U; }) &&
                *std::max_element(sizes.begin(), sizes.end()) == entry.largest_class,
            "every class is used and the largest class has the documented size: " + entry.file);
    for (std::size_t example = 0; example < example_flops.size(); ++example) {
      require(classes_of_flop(catalog, map, example_flops[example]) ==
                  entry.example_classes[example],
              "documented example class count: " + entry.file);
    }
    require(map.is_identity() || fingerprints.insert(map.fingerprint()).second,
            "the maps are different partitions");
    const pb::BoardClassRows rows(120U, 120U, 30U, map);
    std::cout << "  " << entry.name << ": " << entry.turn_classes
              << " turn classes (largest " << entry.largest_class << "), capacities "
              << rows.count(ca::BucketStreet::Flop) << " / " << rows.count(ca::BucketStreet::Turn)
              << " / " << rows.count(ca::BucketStreet::River) << " with 120/120/30 groups, "
              << (map.is_identity() ? rows.fingerprint() : map.fingerprint()) << '\n';
    maps.push_back(std::move(map));
  }
  // TX2: the capacities of the brief and its multi-flop classes (41 % of the
  // physical turn boards in 1,094 classes spanning 2-3 canonical flops).
  const auto &tx2 = maps[3];
  const pb::BoardClassRows tx2_rows(120U, 120U, 30U, tx2);
  require(tx2_rows.count(ca::BucketStreet::Flop) == 68'760U &&
              tx2_rows.count(ca::BucketStreet::Turn) == 537'840U &&
              tx2_rows.count(ca::BucketStreet::River) == 134'460U,
          "TX2 capacities 68,760 / 537,840 / 134,460");
  {
    std::vector<std::set<std::uint32_t>> flops_of(tx2.classes(ca::BucketStreet::Turn));
    for (std::uint32_t index = 0; index < turn_count; ++index) {
      flops_of[tx2.turn_class(index)].insert(catalog.flop_turns()[index].flop_index);
    }
    std::uint32_t multi_flop = 0U;
    std::size_t widest = 0U;
    for (const auto &flops : flops_of) {
      multi_flop += flops.size() > 1U ? 1U : 0U;
      widest = std::max(widest, flops.size());
    }
    require(multi_flop == 1'094U && widest == 3U, "TX2 classes spanning several canonical flops");
  }
  // Suit-permuted physical flop+turn boards have the same class.
  ca::DeterministicRandom random(0x5445'5854'0006ULL);
  std::uint64_t compared = 0U;
  for (int draw = 0; draw < 1'000; ++draw) {
    const auto history = catalog.sample_physical_history(random);
    const auto index = catalog.lookup_flop_turn(history.flop, history.turn).value().index;
    for (const auto &permutation : ca::all_suit_permutations()) {
      const auto image = permuted_history(history, permutation);
      const auto image_index = catalog.lookup_flop_turn(image.flop, image.turn).value().index;
      for (const auto &map : maps) {
        require(map.turn_class(image_index) == map.turn_class(index) &&
                    map.flop_class(catalog.lookup_flop(image.flop).value().index) ==
                        map.flop_class(catalog.lookup_flop(history.flop).value().index),
                "suit-permuted physical boards have the same class");
        ++compared;
      }
    }
  }
  std::cout << "repository maps: 5 maps valid on every canonical turn, documented counts and "
               "examples, "
            << compared << " suit-permuted physical boards in the same class\n";
  return maps;
}

pb::TrainerConfig texture_config(const Resources &resources, const pb::BoardClassRows &rows) {
  auto config = resources.config();
  config.flop_capacity = rows.count(ca::BucketStreet::Flop);
  config.turn_capacity = rows.count(ca::BucketStreet::Turn);
  config.river_capacity = rows.count(ca::BucketStreet::River);
  config.batch_boards = 4U;
  config.threads = 2U;
  config.scheme = pb::WeightingScheme::Dcfr;
  config.update_mode = pb::UpdateMode::Alternating;
  config.lazy_discount = true;
  return config;
}

std::unique_ptr<pb::Trainer> trained(const pb::CompiledGame &game, const Resources &resources,
                                     const FoldedTables &tables, const pb::BoardClassRows &rows,
                                     const int iterations) {
  auto view = resources.view();
  view.flop = &tables.flop;
  view.turn = &tables.turn;
  view.river = &tables.river;
  view.board_class_rows = &rows;
  auto trainer = pb::Trainer::create(game, view, texture_config(resources, rows));
  require(trainer.has_value(), "board-class trainer creates");
  for (int iteration = 0; iteration < iterations; ++iteration) {
    require(trainer.value()->iterate().has_value(), "board-class iteration succeeds");
  }
  return std::move(trainer.value());
}

// T6 and T7: the trainer with the identity map and with merged maps.
void test_trainer(const Resources &resources, const FoldedTables &tables,
                  const std::filesystem::path &scratch) {
  const auto started = Clock::now();
  const auto &catalog = *resources.catalog;
  const auto game =
      pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  const pb::BoardClassRows legacy_rows(4U, 5U, 6U);
  const pb::BoardClassRows identity_rows(
      4U, 5U, 6U, load_map(texture_directory() / "identity_texture_map.txt", catalog));
  const auto legacy = trained(game.value(), resources, tables, legacy_rows, 4);
  const auto identity = trained(game.value(), resources, tables, identity_rows, 4);
  // state_fingerprint() materializes the lazy discounts of both runs before
  // the tables are compared.
  const auto legacy_state = legacy->state_fingerprint();
  const auto identity_state = identity->state_fingerprint();
  require(identity->identity() == legacy->identity(),
          "the identity map keeps the trainer identity");
  require(identity_state == legacy_state && identity->regrets() == legacy->regrets() &&
              identity->strategy_sums() == legacy->strategy_sums(),
          "the identity map trains bit for bit like the rows without a map");

  const auto merged = turn_as_flop(catalog);
  const pb::BoardClassRows merged_rows(4U, 5U, 6U, merged);
  const auto merged_config = texture_config(resources, merged_rows);
  require(merged_config.flop_capacity == flop_count * 4U &&
              merged_config.turn_capacity == flop_count * 5U &&
              merged_config.river_capacity == flop_count * 6U,
          "turn-as-flop trainer capacities are 573 * groups");
  const auto merged_trainer = trained(game.value(), resources, tables, merged_rows, 4);
  require(merged_trainer->identity() != legacy->identity(),
          "a texture changes the trainer identity");
  require(merged_trainer->state_bytes() < legacy->state_bytes() &&
              merged_trainer->regrets().size() < legacy->regrets().size(),
          "merged rows make a smaller state");

  const auto legacy_checkpoint = scratch / "legacy_rows.ckpt";
  const auto merged_checkpoint = scratch / "turn_as_flop.ckpt";
  require(legacy->save_checkpoint(legacy_checkpoint).has_value() &&
              merged_trainer->save_checkpoint(merged_checkpoint).has_value(),
          "checkpoints save");
  const auto merged_state = merged_trainer->state_fingerprint();
  {
    const auto fresh = trained(game.value(), resources, tables, merged_rows, 0);
    const auto refused = fresh->load_checkpoint(legacy_checkpoint);
    require(!refused.has_value() && refused.error() == pb::TrainerError::IntegrityFailure,
            "a texture run refuses the checkpoint of the identity run");
  }
  {
    const auto fresh = trained(game.value(), resources, tables, identity_rows, 0);
    const auto refused = fresh->load_checkpoint(merged_checkpoint);
    require(!refused.has_value() && refused.error() == pb::TrainerError::IntegrityFailure,
            "the identity run refuses the checkpoint of a texture run");
    require(fresh->load_checkpoint(legacy_checkpoint).has_value() &&
                fresh->state_fingerprint() == legacy_state,
            "the identity map resumes the checkpoint of the rows without a map");
  }
  {
    // The same partition under other labels resumes it.
    std::vector<std::uint32_t> relabelled(turn_count);
    for (std::uint32_t index = 0; index < turn_count; ++index) {
      relabelled[index] = 9'999U - catalog.flop_turns()[index].flop_index;
    }
    const pb::BoardClassRows relabelled_rows(
        4U, 5U, 6U, make_partition(iota_labels(flop_count), relabelled, "relabelled"));
    const auto fresh = trained(game.value(), resources, tables, relabelled_rows, 0);
    require(fresh->load_checkpoint(merged_checkpoint).has_value() &&
                fresh->state_fingerprint() == merged_state,
            "a relabelled partition resumes the checkpoint");
  }
  {
    // Another partition with the same class counts refuses it.
    auto moved_labels = turn_as_flop_labels(catalog);
    moved_labels[0] = 1U; // the first turn of flop 0 joins flop 1 (flop 0 keeps others)
    const pb::BoardClassRows moved_rows(
        4U, 5U, 6U, make_partition(iota_labels(flop_count), moved_labels, "moved_one_turn"));
    require(moved_rows.count(ca::BucketStreet::Turn) == merged_rows.count(ca::BucketStreet::Turn),
            "the moved partition has the same capacities");
    const auto fresh = trained(game.value(), resources, tables, moved_rows, 0);
    const auto refused = fresh->load_checkpoint(merged_checkpoint);
    require(!refused.has_value() && refused.error() == pb::TrainerError::IntegrityFailure,
            "another partition with equal counts refuses the checkpoint");
  }
  std::cout << "trainer: identity map identity " << identity->identity() << " and state "
            << legacy_state << " equal to the rows without a map; turn-as-flop identity "
            << merged_trainer->identity() << ", state " << merged_trainer->state_bytes()
            << " bytes (identity " << legacy->state_bytes()
            << "), checkpoints of other partitions refused, "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path resources_dir;
    std::filesystem::path buckets_dir;
    std::filesystem::path scratch_dir =
        std::filesystem::temp_directory_path() / "gtosd_board_texture_tests";
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string_view name = argv[index];
      const std::string_view value = argv[index + 1];
      if (name == "--resources-dir") {
        resources_dir = std::filesystem::path(value);
      } else if (name == "--buckets-dir") {
        buckets_dir = std::filesystem::path(value);
      } else if (name == "--scratch-dir") {
        scratch_dir = std::filesystem::path(value);
      } else {
        throw std::runtime_error("unknown argument " + std::string(name));
      }
    }
    std::filesystem::create_directories(scratch_dir);
    const auto resources = load_resources(resources_dir, buckets_dir);
    std::cout << "resources " << (resources.loaded ? "loaded" : "built") << ", bucket tables "
              << (resources.buckets_loaded ? "loaded" : "synthetic") << " ("
              << resources.flop->capacity() << "/" << resources.turn->capacity() << "/"
              << resources.river->capacity() << ")\n";
    const FoldedTables tables{folded_table(resources, *resources.flop, 4U),
                              folded_table(resources, *resources.turn, 5U),
                              folded_table(resources, *resources.river, 6U)};
    test_identity(resources, tables, scratch_dir);
    test_partition(resources);
    test_file_format(resources, scratch_dir);
    test_shared_rows(resources, tables);
    const auto maps = test_repository_maps(resources);
    const auto merged = turn_as_flop(*resources.catalog);
    test_suit_symmetry(resources, tables, {&maps[0], &merged, &maps[3]});
    test_texture_row_formula(resources, tables, maps[3]);
    test_trainer(resources, tables, scratch_dir);
    std::cout << "PREFLOP_BLUEPRINT_BOARD_TEXTURE_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_BOARD_TEXTURE_TESTS=FAIL " << error.what() << " after "
              << assertions << " assertions\n";
    return 1;
  }
}
