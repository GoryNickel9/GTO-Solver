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
// (test_board_texture_rows). River key "river-board" (the river class of the
// unordered five-card board): its file, fingerprint, rows in BoardContext and
// in the joint river engine, and a short training run. Both river keys against
// the FiniteGame oracle (test_river_key_oracle): the trainer computes the CFR
// iterates of the abstract game whose rows pool the rivers of a turn, the
// turns of a class or the orders of five cards, and its physical best
// response is the brute-force NashConv of that policy; the sampled alternating
// update on key-"turn" rows is unbiased (test_river_key_alternating); gain_lower
// under a locked preflop is the brute-force best response from the flop on
// (test_river_key_gain_lower).
#include "preflop_blueprint_test_support.hpp"

#include "gtosd/card_abstraction/card_abstraction.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/preflop_blueprint/board_class_rows.hpp"
#include "gtosd/preflop_blueprint/board_context.hpp"
#include "gtosd/preflop_blueprint/board_texture.hpp"
#include "gtosd/preflop_blueprint/river_engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
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
  // River key "river-board" is read, but its river section lists the 19,998
  // canonical five-card boards: a turn-style river section is refused.
  expect("river_key_river_board", edited([](auto &copy) { copy[2] = "river-key river-board"; }),
         pb::TextureError::BadSection);
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

// River key "river-board" (correctness tests of 30 September 2026): the river
// class is that of the unordered five-card board. The identity river-board
// map (benchmarks/monker/textures/identity_river_board_texture_map.txt,
// written by generate_texture_maps.py) is the save of the identity partition,
// round-trips, has its own fingerprint and trainer identity, gives the river
// rows class * groups + bucket with the canonical five-card board as the class
// in BoardContext and in the joint river engine alike, gives two histories
// with the same five cards the same river rows, and trains.
void test_river_board_key(const Resources &resources, const FoldedTables &tables,
                          const std::filesystem::path &scratch) {
  const auto started = Clock::now();
  const auto &catalog = *resources.catalog;
  constexpr std::uint32_t river_count = ca::canonical_river_board_count;
  auto built = pb::BoardTextureMap::from_river_board_partition(
      iota_labels(flop_count), iota_labels(turn_count), iota_labels(river_count),
      "identity_river_board");
  require(built.has_value(), "the identity river-board partition is accepted");
  const auto &map = built.value();
  require(!map.is_identity() && map.river_key() == pb::RiverKey::RiverBoard &&
              map.classes(ca::BucketStreet::Flop) == flop_count &&
              map.classes(ca::BucketStreet::Turn) == turn_count &&
              map.classes(ca::BucketStreet::River) == river_count,
          "river-board map: its key, 573 / 13,761 / 19,998 classes, never the identity");
  require(map.fingerprint().starts_with("texture=fnv1a64:") &&
              map.fingerprint().ends_with("|classes=573/13761/19998|river-key=river-board"),
          "the river-board fingerprint names the class counts and the key: " + map.fingerprint());
  bool identity_classes = true;
  for (std::uint32_t index = 0; index < river_count; ++index) {
    identity_classes = identity_classes && map.river_board_class(index) == index &&
                       map.river_class(index % turn_count, index) == index;
  }
  for (std::uint32_t index = 0; index < turn_count; ++index) {
    identity_classes = identity_classes && map.turn_class(index) == index;
  }
  require(identity_classes, "identity river-board classes are the canonical indices");
  {
    const auto wrong = pb::BoardTextureMap::from_river_board_partition(
        iota_labels(flop_count), iota_labels(turn_count), iota_labels(turn_count), "wrong");
    require(!wrong.has_value() && wrong.error() == pb::TextureError::BadSection,
            "a river-board partition needs one class per canonical five-card board");
  }
  // A merged river partition: its own class count and fingerprint.
  std::vector<std::uint32_t> halves(river_count);
  for (std::uint32_t index = 0; index < river_count; ++index) {
    halves[index] = index / 2U;
  }
  const auto merged = pb::BoardTextureMap::from_river_board_partition(
      iota_labels(flop_count), iota_labels(turn_count), halves, "river_halves");
  require(merged.has_value() && merged.value().classes(ca::BucketStreet::River) == 9'999U &&
              merged.value().river_board_class(3U) == 1U &&
              merged.value().fingerprint() != map.fingerprint(),
          "a merged river-board partition has its classes and its fingerprint");

  // File: the repository map is the save of the identity partition.
  const auto repository = texture_directory() / "identity_river_board_texture_map.txt";
  const auto path = scratch / "identity_river_board.txt";
  require(map.save(path, catalog).has_value(), "river-board map saves");
  require(read_text(path) == read_text(repository),
          "the repository river-board map is the identity partition (generate_texture_maps.py)");
  const auto loaded = load_map(repository, catalog);
  require(loaded.fingerprint() == map.fingerprint() &&
              loaded.river_key() == pb::RiverKey::RiverBoard &&
              loaded.name() == "identity_river_board" &&
              loaded.classes(ca::BucketStreet::River) == river_count,
          "the repository river-board map loads");
  const auto merged_path = scratch / "river_halves.txt";
  require(merged.value().save(merged_path, catalog).has_value(), "merged river-board map saves");
  const auto merged_loaded = load_map(merged_path, catalog);
  require(merged_loaded.fingerprint() == merged.value().fingerprint() &&
              merged_loaded.river_board_class(19'997U) == 9'998U,
          "a merged river-board map round-trips");
  const auto lines = split_lines(read_text(path));
  constexpr std::size_t river_header = 3U + 1U + flop_count + 1U + turn_count;
  require(lines.size() == river_header + 1U + river_count &&
              lines[2] == "river-key river-board" && lines[river_header] == "river 19998" &&
              lines[river_header + 1U] ==
                  std::to_string(catalog.river_boards()[0].code) + " 0",
          "river-board file layout: 19,998 river lines of canonical five-card board codes");
  const auto expect = [&](const std::string &case_name, std::vector<std::string> copy,
                          const pb::TextureError error) {
    const auto bad_path = scratch / ("bad_river_board_" + case_name + ".txt");
    write_text(bad_path, join_lines(copy));
    const auto rejected = pb::BoardTextureMap::load(bad_path, catalog);
    require(!rejected.has_value() && rejected.error() == error,
            "malformed river-board map rejected: " + case_name);
  };
  {
    auto copy = lines;
    std::swap(copy[river_header + 10U], copy[river_header + 11U]);
    expect("swapped_codes", copy, pb::TextureError::CodeMismatch);
  }
  {
    auto copy = lines;
    copy.resize(river_header + 100U);
    expect("truncated", copy, pb::TextureError::BadSection);
  }
  {
    auto copy = lines;
    copy[2] = "river-key turn";
    expect("turn_key", copy, pb::TextureError::BadSection);
  }

  // Rows: flop and turn as without a map, the river from the five-card board,
  // equal in BoardContext and in the joint river engine; the same five cards
  // in another order give the same river rows.
  const pb::BoardClassRows legacy_rows(4U, 5U, 6U);
  const pb::BoardClassRows rows(4U, 5U, 6U, map);
  require(rows.fingerprint() != legacy_rows.fingerprint() &&
              rows.fingerprint().starts_with("board-class-rows-v2|") &&
              rows.river_key() == pb::RiverKey::RiverBoard &&
              rows.count(ca::BucketStreet::River) == river_count * 6U &&
              rows.count(ca::BucketStreet::Turn) == legacy_rows.count(ca::BucketStreet::Turn) &&
              rows.count(ca::BucketStreet::Flop) == legacy_rows.count(ca::BucketStreet::Flop),
          "river-board rows: own fingerprint, river capacity 19,998 * groups");
  const auto legacy_view = abstraction_tables(resources, tables, legacy_rows);
  const auto view = abstraction_tables(resources, tables, rows);
  auto prefix = std::make_unique<pb::RiverPrefix>();
  auto river_board = std::make_unique<pb::RiverBoard>();
  ca::DeterministicRandom random(0x5445'5854'0007ULL);
  constexpr int histories = 300;
  std::uint64_t compared = 0U;
  std::uint64_t turn_key_differs = 0U;
  for (int draw = 0; draw < histories; ++draw) {
    const auto history = catalog.sample_physical_history(random);
    const std::array<gtosd::CardId, 5> board{history.flop[0], history.flop[1], history.flop[2],
                                             history.turn, history.river};
    const auto river_index = catalog.lookup_river_board(board).value().index;
    const auto legacy = pb::BoardContext::build(history, *resources.ranks, &legacy_view);
    const auto context = pb::BoardContext::build(history, *resources.ranks, &view);
    auto swapped_history = history;
    std::swap(swapped_history.turn, swapped_history.river);
    const auto swapped = pb::BoardContext::build(swapped_history, *resources.ranks, &view);
    const auto swapped_legacy =
        pb::BoardContext::build(swapped_history, *resources.ranks, &legacy_view);
    require(legacy.has_value() && context.has_value() && swapped.has_value() &&
                swapped_legacy.has_value(),
            "river-board contexts build");
    require(prefix->assign(history.flop, history.turn, view).has_value() &&
                river_board->assign(history, *resources.ranks, prefix.get()).has_value(),
            "the joint river engine accepts the river-board rows");
    const auto river_buckets = context.value().buckets(gtosd::Street::River);
    bool legacy_same = true;
    for (std::uint16_t hand = 0; hand < pb::live_hand_count; ++hand) {
      require(context.value().row(gtosd::Street::Flop, hand) ==
                      legacy.value().row(gtosd::Street::Flop, hand) &&
                  context.value().row(gtosd::Street::Turn, hand) ==
                      legacy.value().row(gtosd::Street::Turn, hand),
              "flop and turn rows do not depend on the river key");
      const auto river_row = context.value().row(gtosd::Street::River, hand);
      require(river_row == river_index * 6U + river_buckets[hand],
              "river-board river row = five-card board class * groups + bucket");
      require(river_board->rows()[hand] == river_row,
              "the joint river engine reads the same river-board rows as BoardContext");
      // Same live hands in the same combo order on the same five cards.
      require(swapped.value().combo_ids()[hand] == context.value().combo_ids()[hand] &&
                  swapped.value().row(gtosd::Street::River, hand) == river_row,
              "the same five cards in another order share the river-board rows");
      legacy_same = legacy_same && swapped_legacy.value().row(gtosd::Street::River, hand) ==
                                       legacy.value().row(gtosd::Street::River, hand);
      ++compared;
    }
    turn_key_differs += legacy_same ? 0U : 1U;
  }
  require(turn_key_differs > 0U,
          "with river key turn the swapped histories have other river rows (the test sees "
          "the key)");

  // Trainer: its own identity, river capacity of the five-card boards, and a
  // checkpoint of the turn-keyed rows refused.
  const auto game =
      pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  const auto legacy_trainer = trained(game.value(), resources, tables, legacy_rows, 2);
  const auto trainer = trained(game.value(), resources, tables, rows, 2);
  require(trainer->identity() != legacy_trainer->identity(),
          "the river-board key changes the trainer identity");
  require(texture_config(resources, rows).river_capacity == river_count * 6U,
          "river-board trainer river capacity 19,998 * groups");
  const auto checkpoint = scratch / "river_board_legacy.ckpt";
  require(legacy_trainer->save_checkpoint(checkpoint).has_value(), "legacy checkpoint saves");
  {
    const auto fresh = trained(game.value(), resources, tables, rows, 0);
    const auto refused = fresh->load_checkpoint(checkpoint);
    require(!refused.has_value() && refused.error() == pb::TrainerError::IntegrityFailure,
            "a river-board run refuses the checkpoint of the turn-keyed rows");
  }
  std::cout << "river-board key: " << map.fingerprint() << ", rows " << rows.fingerprint()
            << ", " << compared << " river rows on " << histories
            << " histories equal in BoardContext, the joint river engine and the swapped "
               "history (turn key differs on "
            << turn_key_differs << "), trainer identity " << trainer->identity() << ", "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

// Key "p<player>|n<node>|r<row>" of FiniteGameBuilder::information_set (the
// parser of the trainer tests).
struct ParsedKey {
  std::uint8_t player;
  std::uint32_t node;
  std::uint32_t row;
};

ParsedKey parse_key(const std::string &key) {
  const auto node_position = key.find("|n");
  const auto row_position = key.find("|r");
  require(key.size() > 2U && key[0] == 'p' && node_position != std::string::npos &&
              row_position != std::string::npos,
          "information set key parses");
  ParsedKey parsed;
  parsed.player = static_cast<std::uint8_t>(std::stoul(key.substr(1, node_position - 1)));
  parsed.node = static_cast<std::uint32_t>(
      std::stoul(key.substr(node_position + 2, row_position - node_position - 2)));
  parsed.row = static_cast<std::uint32_t>(std::stoul(key.substr(row_position + 2)));
  return parsed;
}

// Boards of the river-key oracle, with distinct weights and no sampling. On
// the flop 6s7d8c: the turn 9h with the rivers As, Ad and 6h, the turn 9c
// with As and Ad (9h and 9c are two canonical turns in one TX2 class), a
// suit-permuted copy of the first board (6s7c8d 9h As: the same canonical
// turn and five-card board), the first board with turn and river swapped
// (6s7d8c As 9h: another canonical turn, the same five cards); then 9h Ac on
// the two-tone flops 6d7d8c and 6d7c8d (two canonical flops whose turns share
// a TX2 class). The test checks these relations on the catalog before using
// them. No ten to king, so the oracle subsets are live and uniform on every
// board.
pb::TrainingBoards river_key_boards() {
  const std::array<std::array<std::string_view, 5>, 9> cards{{{"6s", "7d", "8c", "9h", "As"},
                                                              {"6s", "7d", "8c", "9h", "Ad"},
                                                              {"6s", "7d", "8c", "9h", "6h"},
                                                              {"6s", "7d", "8c", "9c", "As"},
                                                              {"6s", "7d", "8c", "9c", "Ad"},
                                                              {"6s", "7c", "8d", "9h", "As"},
                                                              {"6s", "7d", "8c", "As", "9h"},
                                                              {"6d", "7d", "8c", "9h", "Ac"},
                                                              {"6d", "7c", "8d", "9h", "Ac"}}};
  pb::TrainingBoards boards;
  for (const auto &texts : cards) {
    boards.histories.push_back(make_history(texts));
    boards.weights.push_back(static_cast<double>(boards.weights.size() + 1U));
  }
  boards.sample = false;
  return boards;
}

// Canonical flop, flop+turn and five-card board indices of a history.
using CanonicalBoard = std::array<std::uint32_t, 3>;

CanonicalBoard canonical_board(const ca::BoardCatalog &catalog, const ca::BoardHistory &history) {
  const std::array<gtosd::CardId, 5> five{history.flop[0], history.flop[1], history.flop[2],
                                          history.turn, history.river};
  return {catalog.lookup_flop(history.flop).value().index,
          catalog.lookup_flop_turn(history.flop, history.turn).value().index,
          catalog.lookup_river_board(five).value().index};
}

// Information sets of the abstract oracle game, (decision node, row of a
// subset hand of the actor), with the canonical boards on which they occur.
struct PoolingCounts {
  std::uint64_t information_sets{0U};
  std::array<std::uint64_t, 4> by_street{};
  // River information sets spanning two canonical five-card boards of one
  // canonical turn, two canonical turns, two canonical flops, two canonical
  // five-card boards; turn information sets spanning two canonical turns.
  std::uint64_t river_rivers_of_one_turn{0U};
  std::uint64_t river_turns{0U};
  std::uint64_t river_flops{0U};
  std::uint64_t river_five_card_boards{0U};
  std::uint64_t turn_turns{0U};
};

PoolingCounts pooling_counts(const pb::CompiledGame &game, const Resources &resources,
                             const pb::AbstractionTables &view, const pb::TrainingBoards &boards,
                             const pb::HandSubsets &subsets) {
  std::map<std::uint64_t, std::set<CanonicalBoard>> boards_of;
  for (const auto &history : boards.histories) {
    const auto context = pb::BoardContext::build(history, *resources.ranks, &view);
    require(context.has_value(), "river-key oracle context builds");
    const auto canonical = canonical_board(*resources.catalog, history);
    for (const auto &node : game.nodes()) {
      if (node.kind != pb::NodeKind::Decision) {
        continue;
      }
      for (const auto combo : subsets.combos[node.actor]) {
        const auto hand = context.value().hand_index(combo);
        require(hand != pb::no_hand, "subset hand is live on the oracle board");
        const auto row = context.value().row(node.street, hand);
        boards_of[(static_cast<std::uint64_t>(node.id) << 32U) | row].insert(canonical);
      }
    }
  }
  PoolingCounts counts;
  for (const auto &[key, spanned] : boards_of) {
    const auto street = game.nodes()[static_cast<std::uint32_t>(key >> 32U)].street;
    ++counts.information_sets;
    ++counts.by_street[static_cast<std::size_t>(street)];
    std::array<bool, 3> differ{};
    bool rivers_of_one_turn = false;
    for (const auto &left : spanned) {
      for (const auto &right : spanned) {
        for (std::size_t part = 0; part < 3U; ++part) {
          differ[part] = differ[part] || left[part] != right[part];
        }
        rivers_of_one_turn = rivers_of_one_turn || (left[1] == right[1] && left[2] != right[2]);
      }
    }
    if (street == gtosd::Street::River) {
      counts.river_rivers_of_one_turn += rivers_of_one_turn ? 1U : 0U;
      counts.river_flops += differ[0] ? 1U : 0U;
      counts.river_turns += differ[1] ? 1U : 0U;
      counts.river_five_card_boards += differ[2] ? 1U : 0U;
    } else if (street == gtosd::Street::Turn) {
      counts.turn_turns += differ[1] ? 1U : 0U;
    }
  }
  return counts;
}

struct RiverKeyVariant {
  std::string name;
  const pb::CompiledGame *game{nullptr};
  const pb::BoardTextureMap *map{nullptr};
  // Folded river table of river_groups groups (flop and turn: 4 and 5).
  const ca::BucketTable *river{nullptr};
  std::uint32_t river_groups{0U};
  pb::HandSubsets subsets;
};

// One variant of the river-key oracle: A1 and A2 (see test_river_key_oracle);
// returns the pooling counts of A3 after checking that they cover exactly the
// oracle's information sets.
PoolingCounts river_key_oracle(const Resources &resources, const FoldedTables &tables,
                               const pb::TrainingBoards &boards, const RiverKeyVariant &variant) {
  const auto started = Clock::now();
  constexpr std::uint64_t iterations = 25U;
  const auto &game = *variant.game;
  const pb::BoardClassRows rows(4U, 5U, variant.river_groups, *variant.map);
  const StreetTables street_tables{&tables.flop, &tables.turn, variant.river};
  pb::AbstractionTables view;
  view.catalog = &*resources.catalog;
  view.flop = &tables.flop;
  view.turn = &tables.turn;
  view.river = variant.river;
  view.board_class_rows = &rows;
  const auto counts = pooling_counts(game, resources, view, boards, variant.subsets);

  // The abstract game keyed by the BoardContext rows, solved by the
  // independent scalar CFR.
  FiniteGameBuilder builder(game, resources, false, nullptr, nullptr, nullptr, &rows,
                            street_tables);
  const auto finite = builder.build(boards, variant.subsets);
  const auto summary = gtosd::validate_finite_game(finite);
  require(summary.has_value(), "river-key oracle game validates: " + variant.name + " " +
                                   (summary ? "" : gtosd::solver_error_name(summary.error())));
  require(counts.information_sets == summary.value().information_sets,
          "the pooling counts cover exactly the oracle's information sets: " + variant.name);
  gtosd::SolverConfig solver_config;
  solver_config.algorithm = gtosd::SolverAlgorithm::LinearCfr;
  solver_config.iterations = iterations;
  solver_config.thread_count = 1U;
  const auto solved = gtosd::solve_finite_game(finite, solver_config);
  require(solved.has_value(), "river-key oracle game solves: " + variant.name);

  // The trainer in exact mode on the same boards and hand subsets.
  auto config = resources.config();
  config.flop_capacity = rows.count(ca::BucketStreet::Flop);
  config.turn_capacity = rows.count(ca::BucketStreet::Turn);
  config.river_capacity = rows.count(ca::BucketStreet::River);
  config.threads = 2U;
  config.scheme = pb::WeightingScheme::Linear;
  config.update_mode = pb::UpdateMode::Simultaneous;
  auto training_resources = resources.view();
  training_resources.flop = &tables.flop;
  training_resources.turn = &tables.turn;
  training_resources.river = variant.river;
  training_resources.board_class_rows = &rows;
  auto trainer =
      pb::Trainer::create(game, training_resources, config, &boards, &variant.subsets);
  require(trainer.has_value(), "river-key oracle trainer creates: " + variant.name + " " +
                                   (trainer ? "" : pb::trainer_error_name(trainer.error())));
  for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
    require(trainer.value()->iterate().has_value(), "river-key oracle iteration succeeds");
  }
  // The physical evaluation first: it holds its own dense copy of the average
  // policy only for the duration of the call.
  const auto exact = trainer.value()->estimate_exploitability(0U);
  require(exact.has_value() && exact.value().exact,
          "exact physical evaluation on the listed boards: " + variant.name + " " +
              (exact ? "" : pb::trainer_error_name(exact.error())));
  const auto &trained_state = *trainer.value();
  const auto &layout = trained_state.layout();
  const auto average = trainer.value()->average_policy();

  // A1: regrets, strategy sums and average of the trainer are the oracle's.
  std::vector<std::uint8_t> covered(trained_state.cell_count(), 0U);
  double maximum_regret_error = 0.0;
  double maximum_strategy_error = 0.0;
  std::uint64_t compared = 0U;
  for (const auto &[key, buffer] : solved.value().checkpoint.information_sets) {
    const auto parsed = parse_key(key);
    const auto &node = game.nodes()[parsed.node];
    require(node.kind == pb::NodeKind::Decision && node.actor == parsed.player &&
                buffer.actions.size() == node.action_count,
            "oracle information set maps onto a compiled decision");
    const auto offset =
        layout.offsets[parsed.node] + static_cast<std::uint64_t>(parsed.row) * node.action_count;
    const auto average_row = average.row(parsed.node, parsed.row);
    const auto &oracle_average = solved.value().average_strategy.at(key);
    for (std::size_t action = 0; action < node.action_count; ++action) {
      const auto cell = offset + action;
      const double regret = trained_state.regret(cell);
      const double strategy_sum = trained_state.strategy_sum(cell);
      covered[cell] = 1U;
      maximum_regret_error =
          std::max(maximum_regret_error, std::abs(regret - buffer.cumulative_regret[action]));
      maximum_strategy_error = std::max(
          maximum_strategy_error, std::abs(strategy_sum - buffer.cumulative_strategy[action]));
      if (!close(regret, buffer.cumulative_regret[action], 1e-9)) {
        std::cout << "first mismatch " << variant.name << " " << key << " action=" << action
                  << " regret=" << regret << " reference=" << buffer.cumulative_regret[action]
                  << std::endl;
      }
      require(close(regret, buffer.cumulative_regret[action], 1e-9),
              "board-class cumulative regret equals the FiniteGame oracle within 1e-9");
      require(close(strategy_sum, buffer.cumulative_strategy[action], 1e-9),
              "board-class cumulative strategy equals the FiniteGame oracle within 1e-9");
      require(close(average_row[action], oracle_average.probabilities[action], 1e-9),
              "board-class average strategy equals the FiniteGame oracle within 1e-9");
      ++compared;
    }
  }
  for (std::uint64_t cell = 0; cell < covered.size(); ++cell) {
    if (covered[cell] == 0U) {
      require(trained_state.regret(cell) == 0.0 && trained_state.strategy_sum(cell) == 0.0,
              "board-class cells outside the reduced game stay untouched");
    }
  }

  // A2: the trainer's physical best response (joint river engine) is the
  // brute-force NashConv of the lifted average on the lossless physical game.
  const auto bucket_value =
      gtosd::evaluate_strategy_profile(finite, solved.value().average_strategy);
  require(bucket_value.has_value(), "board-class bucket profile evaluates");
  FiniteGameBuilder physical_builder(game, resources, true, &average, nullptr, nullptr, &rows,
                                     street_tables);
  const auto physical_game = physical_builder.build(boards, variant.subsets);
  const auto nash_conv = gtosd::calculate_nash_conv(physical_game, physical_builder.profile());
  require(nash_conv.has_value(), "board-class lossless FiniteGame NashConv computes");
  require(close(bucket_value.value()[0], nash_conv.value().profile_value[0], 1e-9) &&
              close(bucket_value.value()[1], nash_conv.value().profile_value[1], 1e-9),
          "lifting the board-class policy preserves both profile values");
  require(close(exact.value().ev[0], nash_conv.value().profile_value[0], 1e-9) &&
              close(exact.value().ev[1], nash_conv.value().profile_value[1], 1e-9),
          "board-class exact profile values equal calculate_nash_conv within 1e-9: " +
              variant.name);
  require(
      close(exact.value().best_response[0], nash_conv.value().best_response_value[0], 1e-9) &&
          close(exact.value().best_response[1], nash_conv.value().best_response_value[1], 1e-9) &&
          close(exact.value().nashconv, nash_conv.value().nash_conv, 1e-9),
      "board-class physical best responses and NashConv match the lossless oracle: " +
          variant.name);
  std::cout << "  " << variant.name << ": finite game " << summary.value().nodes << " nodes, "
            << summary.value().information_sets << " information sets (river "
            << counts.by_street[3] << ": " << counts.river_rivers_of_one_turn
            << " span rivers of one turn, " << counts.river_turns << " turns, "
            << counts.river_flops << " flops, " << counts.river_five_card_boards
            << " five-card boards; turn " << counts.by_street[2] << ": " << counts.turn_turns
            << " span turns), " << compared << " cells compared of " << trained_state.cell_count()
            << ", max regret error " << maximum_regret_error << ", max strategy error "
            << maximum_strategy_error << ", nashconv " << exact.value().nashconv << " (oracle "
            << nash_conv.value().nash_conv << "), EV [" << exact.value().ev[0] << ", "
            << exact.value().ev[1] << "], "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
  return counts;
}

// River keys against the FiniteGame oracle (correctness tests of 30 September
// 2026, gap (a) of HU50's key "turn", whose river rows pool every river of a
// turn and, with a texture, the turns of a class). The existing oracle
// (test_finite_game_oracle, trainer tests) with board class rows:
//  A1  25 exact Linear Simultaneous iterations on a board list with hand
//      subsets give the regrets, strategy sums and average of
//      gtosd::solve_finite_game (LinearCfr) on the abstract game keyed by the
//      BoardContext rows, within 1e-9, and leave every other cell at zero;
//  A2  the trainer's exact physical best response (the joint river engine,
//      which computes the river rows of either key independently of
//      BoardContext) equals calculate_nash_conv of the lifted average on the
//      lossless physical game within 1e-9 (EVs, best responses, NashConv).
//      This is the first validation of the in-training evaluation with
//      board class rows (Trainer::estimate_exploitability ->
//      evaluate_best_response on a board list), which the trainer CLI
//      refuses because it had not been validated
//      (benchmarks/preflop_blueprint_train.cpp:490-493). It is not the
//      monker_values path: no canonical flops, flop images, policy file or
//      source check;
//  A3  the rows pool what the key says (non-vacuity): with one river group
//      key "turn" puts two rivers of one turn in one river information set,
//      turn-as-flop and TX2 two canonical turns, TX2 the turns of two
//      canonical flops; the identity never spans two canonical turns, the
//      identity and turn-as-flop never two canonical flops; "river-board"
//      never spans two canonical five-card boards and pools the same five
//      cards after another turn; with one river group the two keys have
//      different river information-set counts.
// Variants: HU10 reduced with key "turn" under the identity, turn-as-flop and
// TX2 maps and key "river-board" (identity map), each with 6 and 1 river
// groups (flop 4, turn 5 groups), on the nine boards of river_key_boards and
// the oracle subsets (T, J against Q, K). CO40-test (40 antes: non-all-in bets
// and bet-call lines reach the river) with turn-as-flop only, 2 x 2 hands:
// its tree (604 public nodes in the ctest log of 30 September; the older
// 637-node tree of the CO40 trainer start logs had 1,110,048 cells at
// 200/500/1000 rows: 9,702,552 state bytes in float32 storage and 18,582,936
// in double, 8 bytes per cell apart), so turn-as-flop (2,292 / 2,865 / 3,438
// rows, at most 11.5 times as many) stays under 12.7 M cells (0.2 GB of
// state), while the identity (68,805 turn rows, 138 times) could reach 153 M
// cells (2.4 GB) and TX2 (22,410 turn rows) 50 M.
void test_river_key_oracle(const Resources &resources, const FoldedTables &tables,
                           const pb::BoardTextureMap &tx2) {
  const auto started = Clock::now();
  const auto &catalog = *resources.catalog;
  const auto boards = river_key_boards();
  std::vector<CanonicalBoard> canonical;
  for (const auto &history : boards.histories) {
    canonical.push_back(canonical_board(catalog, history));
  }
  require(tx2.name() == "TX2_recommended", "the oracle's merged map is TX2");
  require(canonical[3][0] == canonical[0][0] && canonical[3][1] != canonical[0][1] &&
              tx2.turn_class(canonical[3][1]) == tx2.turn_class(canonical[0][1]),
          "9h and 9c are two canonical turns of 6s7d8c in one TX2 class");
  require(canonical[5] == canonical[0],
          "the suit-permuted copy has the canonical boards of the first board");
  require(canonical[6][0] == canonical[0][0] && canonical[6][1] != canonical[0][1] &&
              canonical[6][2] == canonical[0][2],
          "turn and river swapped: another canonical turn, the same five-card board");
  require(canonical[7][0] != canonical[8][0] &&
              tx2.turn_class(canonical[7][1]) == tx2.turn_class(canonical[8][1]),
          "9h on two canonical two-tone flops in one TX2 class");

  const auto identity = pb::BoardTextureMap::identity();
  const auto merged = turn_as_flop(catalog);
  const auto river_board =
      load_map(texture_directory() / "identity_river_board_texture_map.txt", catalog);
  require(river_board.river_key() == pb::RiverKey::RiverBoard, "the river-board map has its key");
  const auto river_one = folded_table(resources, *resources.river, 1U);
  const auto hu10 =
      pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  const auto co40 = pb::CompiledGame::compile(load_fixture("preflop_blueprint_co40_test_v1.json"));
  require(hu10.has_value() && co40.has_value(), "HU10 reduced and CO40-test compile");
  auto co40_subsets = oracle_subsets();
  for (auto &combos : co40_subsets.combos) {
    combos = {combos.front(), combos.back()};
  }

  struct MapCase {
    const char *name;
    const pb::BoardTextureMap *map;
    bool co40_game;
  };
  const std::array<MapCase, 5> cases{{{"identity", &identity, false},
                                      {"turn_as_flop", &merged, false},
                                      {"TX2", &tx2, false},
                                      {"identity_river_board", &river_board, false},
                                      {"turn_as_flop", &merged, true}}};
  std::array<std::uint64_t, 2> identity_river_sets{};
  std::array<std::uint64_t, 2> river_board_river_sets{};
  std::uint32_t variants = 0U;
  for (const auto &entry : cases) {
    const bool river_board_key = entry.map->river_key() == pb::RiverKey::RiverBoard;
    for (const std::uint32_t groups : {6U, 1U}) {
      RiverKeyVariant variant;
      variant.name = std::string(entry.co40_game ? "CO40-test " : "HU10 reduced ") +
                     entry.name + (river_board_key ? " key=river-board" : " key=turn") +
                     " river_groups=" + std::to_string(groups);
      variant.game = entry.co40_game ? &co40.value() : &hu10.value();
      variant.map = entry.map;
      variant.river = groups == 1U ? &river_one : &tables.river;
      variant.river_groups = groups;
      variant.subsets = entry.co40_game ? co40_subsets : oracle_subsets();
      const auto counts = river_key_oracle(resources, tables, boards, variant);
      ++variants;

      // A3: non-vacuity of the pooling.
      require(counts.by_street[3] > 0U, "the oracle game has river information sets");
      if (river_board_key) {
        require(counts.river_five_card_boards == 0U,
                "river key river-board: no river information set spans two canonical "
                "five-card boards");
        require(counts.river_turns > 0U,
                "river key river-board: the same five cards after another turn share the "
                "river information sets");
      } else {
        if (groups == 1U) {
          require(counts.river_rivers_of_one_turn > 0U,
                  "river key turn: a river information set spans two rivers of one turn");
        }
        if (entry.map->is_identity()) {
          require(counts.river_turns == 0U && counts.turn_turns == 0U,
                  "identity: no information set spans two canonical turns");
        } else if (groups == 1U) {
          require(counts.river_turns > 0U,
                  "a merged texture puts two canonical turns in one river information set");
        }
        if (entry.map == &tx2) {
          if (groups == 1U) {
            require(counts.river_flops > 0U,
                    "TX2 puts turns of two canonical flops in one river information set");
          }
        } else {
          require(counts.river_flops == 0U,
                  "identity and turn-as-flop never span two canonical flops");
        }
      }
      if (!entry.co40_game && entry.map == &identity) {
        identity_river_sets[groups == 1U ? 1U : 0U] = counts.by_street[3];
      }
      if (!entry.co40_game && entry.map == &river_board) {
        river_board_river_sets[groups == 1U ? 1U : 0U] = counts.by_street[3];
      }
    }
  }
  // One river group: a river row per class, 5 canonical turns against 7
  // canonical five-card boards per river decision.
  require(identity_river_sets[1] != river_board_river_sets[1],
          "the two river keys give different river information-set counts");
  std::cout << "river-key oracle: " << variants
            << " variants, trainer = FiniteGame LinearCfr and physical best response = "
               "calculate_nash_conv within 1e-9, river information sets key turn / river-board "
            << identity_river_sets[0] << " / " << river_board_river_sets[0] << " (6 groups), "
            << identity_river_sets[1] << " / " << river_board_river_sets[1] << " (1 group), "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

// A4 (critic R3): HU50's update path, boards sampled from the list with
// alternating updates, on key-"turn" rows. As in
// test_alternating_conditional_expectation (trainer tests), conditional on
// player 0 having drawn board A: the expectation over player 1's independent
// draw (A or B, one half each) of player 1's update equals the exact update
// of the FiniteGame oracle against player 0's updated policy, within 1e-10.
// A and B share flop and turn (6s7d8c 6c, rivers Tc and Kc), so with one river
// group both boards have the same river rows: player 0's pooled rows carry
// what it learned on A into B. Variants: CO40-test with
// turn-as-flop (1 and 6 river groups; the identity rows of CO40 are too large,
// see test_river_key_oracle), HU10 reduced with the identity (1 group) and
// TX2 (6 groups); hands TsTh, JsJh against QsQh, KsKh.
void test_river_key_alternating(const Resources &resources, const FoldedTables &tables,
                                const pb::BoardTextureMap &tx2) {
  const auto started = Clock::now();
  const auto &catalog = *resources.catalog;
  pb::TrainingBoards boards;
  boards.histories = {make_history({"6s", "7d", "8c", "6c", "Tc"}),
                      make_history({"6s", "7d", "8c", "6c", "Kc"})};
  boards.weights = {1.0, 1.0};
  auto first_board = boards;
  first_board.histories.resize(1U);
  first_board.weights.resize(1U);
  auto subsets = oracle_subsets();
  for (auto &combos : subsets.combos) {
    combos = {combos.front(), combos.back()};
  }
  const auto identity = pb::BoardTextureMap::identity();
  const auto merged = turn_as_flop(catalog);
  const auto river_one = folded_table(resources, *resources.river, 1U);
  const auto hu10 =
      pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  const auto co40 = pb::CompiledGame::compile(load_fixture("preflop_blueprint_co40_test_v1.json"));
  require(hu10.has_value() && co40.has_value(), "HU10 reduced and CO40-test compile");

  struct AlternatingCase {
    const char *name;
    const pb::CompiledGame *game;
    const pb::BoardTextureMap *map;
    std::uint32_t river_groups;
  };
  const std::array<AlternatingCase, 4> cases{
      {{"CO40-test turn_as_flop", &co40.value(), &merged, 1U},
       {"CO40-test turn_as_flop", &co40.value(), &merged, 6U},
       {"HU10 reduced identity", &hu10.value(), &identity, 1U},
       {"HU10 reduced TX2", &hu10.value(), &tx2, 6U}}};
  for (const auto &entry : cases) {
    const auto case_started = Clock::now();
    const auto &game = *entry.game;
    const auto name = std::string(entry.name) + " key=turn river_groups=" +
                      std::to_string(entry.river_groups);
    const auto *river_table = entry.river_groups == 1U ? &river_one : &tables.river;
    const pb::BoardClassRows rows(4U, 5U, entry.river_groups, *entry.map);
    const StreetTables street_tables{&tables.flop, &tables.turn, river_table};
    pb::AbstractionTables view;
    view.catalog = &catalog;
    view.flop = &tables.flop;
    view.turn = &tables.turn;
    view.river = river_table;
    view.board_class_rows = &rows;
    const auto counts = pooling_counts(game, resources, view, boards, subsets);
    if (entry.river_groups == 1U) {
      require(counts.river_rivers_of_one_turn > 0U,
              "the two rivers of the turn share river information sets: " + name);
    }

    // Exact reference: player 0's update on board A alone, then player 1's
    // update on both boards against it.
    gtosd::SolverConfig reference_config;
    reference_config.algorithm = gtosd::SolverAlgorithm::LinearCfr;
    reference_config.iterations = 1U;
    FiniteGameBuilder first_builder(game, resources, false, nullptr, nullptr, nullptr, &rows,
                                    street_tables);
    const auto first_finite = first_builder.build(first_board, subsets);
    const auto first = gtosd::solve_finite_game(first_finite, reference_config);
    require(first.has_value(), "first player's conditional reference solves: " + name);
    FiniteGameBuilder full_builder(game, resources, false, nullptr, nullptr, nullptr, &rows,
                                   street_tables);
    const auto full_finite = full_builder.build(boards, subsets);
    const auto initial = gtosd::solve_finite_game(full_finite, reference_config);
    require(initial.has_value(), "full conditional reference initializes: " + name);
    auto checkpoint = initial.value().checkpoint;
    checkpoint.completed_iterations = 0U;
    for (auto &[key, buffer] : checkpoint.information_sets) {
      std::fill(buffer.cumulative_strategy.begin(), buffer.cumulative_strategy.end(), 0.0);
      std::fill(buffer.cumulative_regret.begin(), buffer.cumulative_regret.end(), 0.0);
      const auto found = first.value().checkpoint.information_sets.find(key);
      if (buffer.player == 0U && found != first.value().checkpoint.information_sets.end()) {
        buffer.cumulative_regret = found->second.cumulative_regret;
      }
    }
    const auto expected = gtosd::solve_finite_game(full_finite, reference_config, &checkpoint);
    require(expected.has_value(), "conditional exact second-player update computes: " + name);

    // Sampled alternating trainer: player 0 draws A, player 1 draws A, then B.
    auto sampled_boards = boards;
    sampled_boards.sample = true;
    std::vector<double> mean_regret;
    std::vector<double> mean_sum;
    for (std::size_t second = 0U; second < 2U; ++second) {
      std::uint64_t seed = 0U;
      for (;; ++seed) {
        ca::DeterministicRandom random(seed);
        const bool first_is_a = random.uniform_unit() < 0.5;
        const bool second_is_a = random.uniform_unit() < 0.5;
        if (first_is_a && second_is_a == (second == 0U)) {
          break;
        }
        require(seed < 1000U, "conditional seed found within a bounded search");
      }
      auto config = resources.config();
      config.flop_capacity = rows.count(ca::BucketStreet::Flop);
      config.turn_capacity = rows.count(ca::BucketStreet::Turn);
      config.river_capacity = rows.count(ca::BucketStreet::River);
      config.batch_boards = 1U;
      config.training_seed = seed;
      config.scheme = pb::WeightingScheme::Linear;
      config.update_mode = pb::UpdateMode::Alternating;
      auto training_resources = resources.view();
      training_resources.flop = &tables.flop;
      training_resources.turn = &tables.turn;
      training_resources.river = river_table;
      training_resources.board_class_rows = &rows;
      auto trainer =
          pb::Trainer::create(game, training_resources, config, &sampled_boards, &subsets);
      require(trainer.has_value(), "conditional sampled board-class trainer creates: " + name);
      const auto telemetry = trainer.value()->iterate();
      require(telemetry.has_value(), "conditional sampled iteration succeeds");
      require(telemetry.value().boards == 2U && trainer.value()->boards_processed() == 2U,
              "telemetry counts the two independently drawn boards");
      const auto &layout = trainer.value()->layout();
      std::size_t index = 0U;
      for (const auto &[key, buffer] : expected.value().checkpoint.information_sets) {
        if (buffer.player != 1U) {
          continue;
        }
        const auto parsed = parse_key(key);
        const auto &node = game.nodes()[parsed.node];
        require(node.kind == pb::NodeKind::Decision && node.actor == 1U &&
                    buffer.actions.size() == node.action_count,
                "oracle information set maps onto a compiled decision of player 1");
        const auto offset = layout.offsets[parsed.node] +
                            static_cast<std::uint64_t>(parsed.row) * node.action_count;
        for (std::size_t action = 0U; action < node.action_count; ++action, ++index) {
          if (second == 0U) {
            mean_regret.push_back(0.0);
            mean_sum.push_back(0.0);
          }
          mean_regret[index] += 0.5 * trainer.value()->regret(offset + action);
          mean_sum[index] += 0.5 * trainer.value()->strategy_sum(offset + action);
        }
      }
      require(index == mean_regret.size(), "both runs compare the same cells");
    }
    double max_error = 0.0;
    double max_sum_error = 0.0;
    double largest_update = 0.0;
    std::size_t index = 0U;
    for (const auto &[key, buffer] : expected.value().checkpoint.information_sets) {
      static_cast<void>(key);
      if (buffer.player != 1U) {
        continue;
      }
      for (std::size_t action = 0U; action < buffer.actions.size(); ++action, ++index) {
        max_error = std::max(max_error, std::abs(mean_regret[index] -
                                                 buffer.cumulative_regret[action]));
        max_sum_error = std::max(max_sum_error, std::abs(mean_sum[index] -
                                                         buffer.cumulative_strategy[action]));
        largest_update = std::max(largest_update, std::abs(buffer.cumulative_regret[action]));
      }
    }
    std::cout << "  alternating conditional expectation " << name << ": " << index
              << " player-1 cells, max regret error " << max_error
              << ", max strategy-sum error " << max_sum_error << ", largest regret "
              << largest_update << ", river information sets spanning rivers of one turn "
              << counts.river_rivers_of_one_turn << ", "
              << std::chrono::duration<double>(Clock::now() - case_started).count() << " s\n";
    require(index > 0U && largest_update > 0.0,
            "the conditional update of player 1 is not empty: " + name);
    require(max_error < 1e-10,
            "board-class conditional regret expectation equals the exact traversal: " + name);
    require(max_sum_error < 1e-10,
            "board-class conditional strategy-sum expectation equals the exact traversal: " +
                name);
  }
  std::cout << "river-key alternating: sampled alternating updates on key-turn rows are "
               "unbiased (4 cases), "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

// Mixed preflop lock of every preflop decision and hand class: frequencies
// proportional to 1 + (7 class + 3 node + 5 action) mod 11, all positive.
pb::PreflopLock mixed_preflop_lock(const pb::CompiledGame &game) {
  pb::PreflopLock lock;
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::Decision || node.street != gtosd::Street::Preflop) {
      continue;
    }
    for (std::uint32_t hand_class = 0U; hand_class < ca::preflop_hand_classes; ++hand_class) {
      pb::PreflopLockRow locked;
      locked.node = node.id;
      locked.hand_class = static_cast<std::uint8_t>(hand_class);
      double total = 0.0;
      for (std::uint32_t action = 0U; action < node.action_count; ++action) {
        locked.frequencies.push_back(
            1.0 + static_cast<double>((7U * hand_class + 3U * node.id + 5U * action) % 11U));
        total += locked.frequencies.back();
      }
      for (auto &frequency : locked.frequencies) {
        frequency /= total;
      }
      lock.rows.push_back(std::move(locked));
    }
  }
  return lock;
}

// A5 (critic M3): gain_lower, the pass metric of the locked correctness runs
// (V1L, V2L and the proposed locked runs), against brute force. gain_lower is
// the gain of a hero who follows the average preflop and best-responds from
// the flop on (best_response.hpp, best_response_lower). With both players'
// preflop locked to mixed class-shaped charts the average preflop is the
// lock, so gain_lower of each player is its best-response gain in the
// physical game whose preflop decisions are chance nodes with the lock's
// frequencies (FiniteGameBuilder with preflop_chance): calculate_nash_conv of
// the lifted postflop average on that game, within 1e-9, per player and
// summed; the EVs agree too. The same locked policy is also checked as in A2
// (unrestricted physical best response = calculate_nash_conv of the lossless
// game with preflop decisions), gain_lower never exceeds gain, and the
// exported average preflop rows are the lock. 5 exact Linear iterations, so
// the gains are large. Cases: HU10 reduced with TX2 (key "turn", HU50's
// path) and with the identity river-board map (key "river-board", V2L's
// path), CO40-test with turn-as-flop (several postflop entries: opened and
// 3-bet pots), 6 river groups.
void test_river_key_gain_lower(const Resources &resources, const FoldedTables &tables,
                               const pb::BoardTextureMap &tx2) {
  const auto started = Clock::now();
  const auto &catalog = *resources.catalog;
  const auto boards = river_key_boards();
  const auto merged = turn_as_flop(catalog);
  const auto river_board =
      load_map(texture_directory() / "identity_river_board_texture_map.txt", catalog);
  const auto hu10 =
      pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  const auto co40 = pb::CompiledGame::compile(load_fixture("preflop_blueprint_co40_test_v1.json"));
  require(hu10.has_value() && co40.has_value(), "HU10 reduced and CO40-test compile");
  auto co40_subsets = oracle_subsets();
  for (auto &combos : co40_subsets.combos) {
    combos = {combos.front(), combos.back()};
  }
  struct LockCase {
    const char *name;
    const pb::CompiledGame *game;
    const pb::BoardTextureMap *map;
    const pb::HandSubsets *subsets;
  };
  const auto hu10_subsets = oracle_subsets();
  const std::array<LockCase, 3> cases{
      {{"HU10 reduced TX2 key=turn", &hu10.value(), &tx2, &hu10_subsets},
       {"HU10 reduced identity_river_board key=river-board", &hu10.value(), &river_board,
        &hu10_subsets},
       {"CO40-test turn_as_flop key=turn", &co40.value(), &merged, &co40_subsets}}};
  constexpr std::uint64_t iterations = 5U;
  for (const auto &entry : cases) {
    const auto case_started = Clock::now();
    const auto &game = *entry.game;
    const std::string name = entry.name;
    const auto lock = mixed_preflop_lock(game);
    require(!lock.rows.empty(), "the lock covers the preflop decisions");
    const pb::BoardClassRows rows(4U, 5U, 6U, *entry.map);
    const StreetTables street_tables{&tables.flop, &tables.turn, &tables.river};
    auto config = resources.config();
    config.flop_capacity = rows.count(ca::BucketStreet::Flop);
    config.turn_capacity = rows.count(ca::BucketStreet::Turn);
    config.river_capacity = rows.count(ca::BucketStreet::River);
    config.threads = 2U;
    config.scheme = pb::WeightingScheme::Linear;
    config.update_mode = pb::UpdateMode::Simultaneous;
    auto training_resources = resources.view();
    training_resources.flop = &tables.flop;
    training_resources.turn = &tables.turn;
    training_resources.river = &tables.river;
    training_resources.board_class_rows = &rows;
    training_resources.preflop_lock = &lock;
    auto trainer = pb::Trainer::create(game, training_resources, config, &boards, entry.subsets);
    require(trainer.has_value(), "locked board-class trainer creates: " + name + " " +
                                     (trainer ? "" : pb::trainer_error_name(trainer.error())));
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
      require(trainer.value()->iterate().has_value(), "locked iteration succeeds");
    }
    const auto exact = trainer.value()->estimate_exploitability(0U);
    require(exact.has_value() && exact.value().exact,
            "exact locked evaluation on the listed boards: " + name + " " +
                (exact ? "" : pb::trainer_error_name(exact.error())));
    const auto average = trainer.value()->average_policy();
    for (const auto &locked : lock.rows) {
      const auto exported = average.row(locked.node, locked.hand_class);
      for (std::size_t action = 0U; action < locked.frequencies.size(); ++action) {
        require(close(exported[action], locked.frequencies[action], 1e-12),
                "the exported average preflop rows are the lock");
      }
    }

    // The preflop as chance: the hero follows the lock, then best-responds.
    FiniteGameBuilder restricted_builder(game, resources, true, &average, nullptr, nullptr, &rows,
                                         street_tables, &lock);
    const auto restricted_game = restricted_builder.build(boards, *entry.subsets);
    const auto restricted_summary = gtosd::validate_finite_game(restricted_game);
    require(restricted_summary.has_value(),
            "preflop-as-chance game validates: " + name + " " +
                (restricted_summary ? ""
                                    : gtosd::solver_error_name(restricted_summary.error())));
    const auto restricted =
        gtosd::calculate_nash_conv(restricted_game, restricted_builder.profile());
    require(restricted.has_value(), "preflop-as-chance NashConv computes: " + name);
    std::array<double, 2> oracle_gain{};
    for (const std::uint8_t player : {std::uint8_t{0}, std::uint8_t{1}}) {
      oracle_gain[player] = restricted.value().best_response_value[player] -
                            restricted.value().profile_value[player];
      require(close(exact.value().ev[player], restricted.value().profile_value[player], 1e-9),
              "locked EV equals the value of the preflop-as-chance game: " + name);
      require(close(exact.value().gain_lower[player], oracle_gain[player], 1e-9),
              "gain_lower equals the brute-force best response from the flop on: " + name);
      require(exact.value().gain_lower[player] <= exact.value().gain[player] + 1e-9,
              "gain_lower never exceeds gain");
    }
    require(close(exact.value().gain_lower[0] + exact.value().gain_lower[1],
                  restricted.value().nash_conv, 1e-9),
            "gain_lower[0] + gain_lower[1] equals the preflop-as-chance NashConv: " + name);
    require(restricted.value().nash_conv > 1e-6,
            "the locked policy is exploitable from the flop on (the check is not vacuous)");

    // The same locked policy with the unrestricted physical best response.
    FiniteGameBuilder physical_builder(game, resources, true, &average, nullptr, nullptr, &rows,
                                       street_tables);
    const auto physical_game = physical_builder.build(boards, *entry.subsets);
    const auto nash_conv = gtosd::calculate_nash_conv(physical_game, physical_builder.profile());
    require(nash_conv.has_value(), "locked lossless FiniteGame NashConv computes: " + name);
    require(close(exact.value().ev[0], nash_conv.value().profile_value[0], 1e-9) &&
                close(exact.value().ev[1], nash_conv.value().profile_value[1], 1e-9) &&
                close(exact.value().best_response[0], nash_conv.value().best_response_value[0],
                      1e-9) &&
                close(exact.value().best_response[1], nash_conv.value().best_response_value[1],
                      1e-9) &&
                close(exact.value().nashconv, nash_conv.value().nash_conv, 1e-9),
            "locked physical best responses and NashConv match the lossless oracle: " + name);
    std::cout << "  gain_lower " << name << ": [" << exact.value().gain_lower[0] << ", "
              << exact.value().gain_lower[1] << "] (oracle [" << oracle_gain[0] << ", "
              << oracle_gain[1] << "]), gain [" << exact.value().gain[0] << ", "
              << exact.value().gain[1] << "], nashconv " << exact.value().nashconv
              << " (oracle " << nash_conv.value().nash_conv << "), preflop-as-chance game "
              << restricted_summary.value().nodes << " nodes, " << lock.rows.size()
              << " locked rows, "
              << std::chrono::duration<double>(Clock::now() - case_started).count() << " s\n";
  }
  std::cout << "river-key gain_lower: gain_lower = brute-force best response from the flop on "
               "under a locked preflop (3 cases), "
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
    test_river_board_key(resources, tables, scratch_dir);
    test_river_key_oracle(resources, tables, maps[3]);
    test_river_key_alternating(resources, tables, maps[3]);
    test_river_key_gain_lower(resources, tables, maps[3]);
    {
      // Suit symmetry of the river-board rows through BoardContext.
      const auto river_board = load_map(
          texture_directory() / "identity_river_board_texture_map.txt", *resources.catalog);
      test_suit_symmetry(resources, tables, {&river_board});
    }
    std::cout << "PREFLOP_BLUEPRINT_BOARD_TEXTURE_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_BOARD_TEXTURE_TESTS=FAIL " << error.what() << " after "
              << assertions << " assertions\n";
    return 1;
  }
}
