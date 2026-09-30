// Three-player class table (phase 2a): pair counts, the reference combo-triple
// counter, a subset build against the plain board enumeration, per-combo
// invariance, the brute force through the exact evaluator, the integer
// identities (V6 always: the heads-up table is built when the resources lack
// it), persistence (a subset loads only on request and never takes the name
// of the complete table), thread determinism, and, when the resources hold
// the complete table, its rows against the subset.
#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/card_abstraction/three_way_table.hpp"
#include "gtosd/core/ranges.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace ca = gtosd::card_abstraction;
using Clock = std::chrono::steady_clock;

std::uint64_t assertions = 0U;
constexpr unsigned build_threads = 2U;
constexpr std::uint8_t class_count = static_cast<std::uint8_t>(ca::three_way_class_count);

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

double seconds_since(const Clock::time_point started) {
  return std::chrono::duration<double>(Clock::now() - started).count();
}

std::uint8_t class_of(const std::string_view name) {
  for (std::uint8_t hand_class = 0U; hand_class < class_count; ++hand_class) {
    if (gtosd::class_name(hand_class) == name) {
      return hand_class;
    }
  }
  throw std::runtime_error("unknown class " + std::string{name});
}

std::uint16_t combo_of(const gtosd::Rank first_rank, const gtosd::Suit first_suit,
                       const gtosd::Rank second_rank, const gtosd::Suit second_suit) {
  return ca::combo_index(gtosd::CardId::from_parts(first_rank, first_suit),
                         gtosd::CardId::from_parts(second_rank, second_suit));
}

std::vector<std::uint16_t> members_of(const std::uint8_t hand_class) {
  std::vector<std::uint16_t> members;
  for (std::uint16_t combo = 0U; combo < ca::combo_count; ++combo) {
    if (ca::combo_table().hand_class[combo] == hand_class) {
      members.push_back(combo);
    }
  }
  return members;
}

bool disjoint(const std::uint16_t a, const std::uint16_t b) {
  return (ca::combo_table().masks[a] & ca::combo_table().masks[b]) == 0U;
}

void test_pair_counts() {
  const auto aa = class_of("AA");
  const auto kk = class_of("KK");
  const auto aks = class_of("AKs");
  const auto ako = class_of("AKo");
  for (std::uint8_t hand_class = 0U; hand_class < class_count; ++hand_class) {
    require(ca::ThreeWayTable::representative_of(hand_class) == members_of(hand_class).front(),
            "the representative is the lowest combo id of its class");
  }
  const auto hero_aa = ca::ThreeWayTable::representative_of(aa);
  require(ca::three_way_pair_count(hero_aa, aa, aa) == 0U, "AA against two more AA: no deal");
  require(ca::three_way_pair_count(hero_aa, aa, kk) == 6U, "AA vs AA vs KK: 1 x 6 pairs");
  require(ca::three_way_pair_count(hero_aa, kk, kk) == 6U, "AA vs KK vs KK: 6 x 1 pairs");
  require(ca::three_way_pair_count(ca::ThreeWayTable::representative_of(aks), aks, aks) == 6U,
          "AKs vs AKs vs AKs: 3 x 2 pairs");
  require(ca::three_way_pair_count(ca::ThreeWayTable::representative_of(ako), aa, kk) == 3U * 3U,
          "AKo vs AA vs KK: 3 x 3 pairs");
  // Every hero combo sees C(34,2) * C(32,2) ordered disjoint pairs in total.
  std::uint64_t total = 0U;
  for (std::uint8_t first = 0U; first < class_count; ++first) {
    for (std::uint8_t second = 0U; second < class_count; ++second) {
      total += ca::three_way_pair_count(hero_aa, first, second);
    }
  }
  require(total == 561U * 496U, "ordered disjoint pairs of one hero = 561 x 496");
}

void test_combo_triple_counter(const ca::RankTable &ranks) {
  using gtosd::Rank;
  using gtosd::Suit;
  const auto hero = combo_of(Rank::Ace, Suit::Clubs, Rank::King, Suit::Clubs);
  const auto first = combo_of(Rank::Queen, Suit::Clubs, Rank::Jack, Suit::Clubs);
  const auto second = combo_of(Rank::Ten, Suit::Hearts, Rank::Ten, Suit::Spades);
  const auto counts = ca::count_combo_triple(ranks, hero, first, second);
  require(counts.has_value(), "combo triple counts");
  std::uint64_t by_winners = 0U;
  std::uint64_t cells = 0U;
  for (const auto count : counts.value().by_winners) {
    by_winners += count;
  }
  for (const auto count : counts.value().cells) {
    cells += count;
  }
  require(by_winners == ca::three_way_runout_count && cells == ca::three_way_runout_count,
          "a combo triple covers C(30,5) runouts");
  const auto &value = counts.value();
  require(value.by_winners[0] == value.cells[0] && value.by_winners[2] == value.cells[3] &&
              value.by_winners[4] == value.cells[1] && value.by_winners[6] == value.cells[4],
          "winner masks {hero}, {hero, first}, {hero, second}, {all} match the W/T cells");
  // Permuting the seats permutes the winner masks.
  const auto rotated = ca::count_combo_triple(ranks, first, second, hero);
  require(rotated.has_value(), "rotated combo triple counts");
  for (std::uint32_t mask = 1U; mask < 8U; ++mask) {
    const auto rotated_mask = ((mask >> 1U) & 3U) | ((mask & 1U) << 2U);
    require(value.by_winners[mask - 1U] == rotated.value().by_winners[rotated_mask - 1U],
            "seat rotation permutes the winner masks");
  }
  require(!ca::count_combo_triple(ranks, hero, hero, second).has_value(),
          "overlapping combos are rejected");
}

// Entry of a representative hero as the sum of the reference counter.
ca::ThreeWayEntry entry_by_combo_triples(const ca::RankTable &ranks, const std::uint16_t hero,
                                         const std::uint8_t first, const std::uint8_t second) {
  ca::ThreeWayEntry entry;
  for (const auto a : members_of(first)) {
    for (const auto b : members_of(second)) {
      if (!disjoint(hero, a) || !disjoint(hero, b) || !disjoint(a, b)) {
        continue;
      }
      const auto counts = ca::count_combo_triple(ranks, hero, a, b);
      require(counts.has_value(), "reference combo triple counts");
      ++entry.pairs;
      for (std::size_t cell = 0U; cell < entry.cells.size(); ++cell) {
        entry.cells[cell] += counts.value().cells[cell];
      }
    }
  }
  return entry;
}

bool rows_equal(const ca::ThreeWayTable &table, const std::uint8_t hero,
                const std::vector<ca::ThreeWayEntry> &rows) {
  for (std::uint8_t first = 0U; first < class_count; ++first) {
    for (std::uint8_t second = 0U; second < class_count; ++second) {
      if (rows[static_cast<std::size_t>(first) * class_count + second] !=
          table.entry(hero, first, second)) {
        return false;
      }
    }
  }
  return true;
}

void test_subset_table(const ca::RankTable &ranks, const ca::ThreeWayTable &table,
                       const ca::AllInTable &heads_up) {
  const auto aa = class_of("AA");
  const auto kk = class_of("KK");
  const auto aks = class_of("AKs");
  const auto ako = class_of("AKo");
  require(!table.complete() && table.hero_built(aa) && table.hero_built(aks) &&
              table.hero_built(ako) && !table.hero_built(kk),
          "subset build marks exactly its hero classes");
  require(table.representatives()[kk] == ca::three_way_unbuilt_hero,
          "unbuilt hero classes have no representative");
  require(table.entry(aa, aa, aa) == ca::ThreeWayEntry{}, "AA vs AA vs AA is empty");
  require(table.entry(kk, aa, aa) == ca::ThreeWayEntry{}, "unbuilt rows stay zero");

  // Board symmetry against the plain enumeration: suited (6 symmetries) and
  // pair (4) heroes; the offsuit hero (2) is covered by the thread check below.
  for (const auto hero : {aks, aa}) {
    const auto plain = ca::ThreeWayTable::build_hero_rows(ranks, table.representatives()[hero],
                                                          build_threads, false);
    require(plain.has_value() && rows_equal(table, hero, plain.value()),
            "orbit-weighted boards equal the plain board enumeration");
  }

  // Reference counter (rank table, one runout at a time) on small entries.
  for (const auto &triple :
       {std::array<std::uint8_t, 3>{aa, aa, kk}, std::array<std::uint8_t, 3>{aks, aks, aks},
        std::array<std::uint8_t, 3>{ako, aa, kk}}) {
    require(entry_by_combo_triples(ranks, table.representatives()[triple[0]], triple[1],
                                   triple[2]) == table.entry(triple[0], triple[1], triple[2]),
            "builder entry equals the sum of the reference combo-triple counts");
  }

  // V8: brute force through gtosd::evaluate_showdown.
  for (const auto &triple :
       {std::array<std::uint8_t, 3>{aa, aa, kk}, std::array<std::uint8_t, 3>{aks, aks, aks}}) {
    const auto brute = ca::evaluate_entry_by_showdown(table.representatives()[triple[0]], triple[1],
                                                      triple[2], build_threads);
    require(brute.has_value() && brute.value() == table.entry(triple[0], triple[1], triple[2]),
            "builder entry equals the exact evaluator brute force");
  }

  // V7: another combo of the class gives the same entries.
  using gtosd::Rank;
  using gtosd::Suit;
  for (const auto &[hero, combo] :
       {std::pair<std::uint8_t, std::uint16_t>{
            ako, combo_of(Rank::Ace, Suit::Hearts, Rank::King, Suit::Spades)},
        std::pair<std::uint8_t, std::uint16_t>{
            aa, combo_of(Rank::Ace, Suit::Diamonds, Rank::Ace, Suit::Spades)}}) {
    require(combo != table.representatives()[hero], "invariance uses a non-representative");
    const auto rows = ca::ThreeWayTable::build_hero_rows(ranks, combo, build_threads, true);
    require(rows.has_value() && rows_equal(table, hero, rows.value()),
            "every combo of a class has the representative's entries");
  }

  // V1-V6.
  const auto report = ca::check_three_way_identities(table, &heads_up);
  for (std::size_t identity = 0U; identity < report.checks.size(); ++identity) {
    require(report.checks[identity] != 0U, "every table identity is exercised");
  }
  require(report.passed(), "integer identities V1-V6 hold on the subset");
  require(report.checks[5] == 3U * 81U * 4U, "V6 checks every (hero, opponent) of the subset");
  // Up to 0.195 on this subset: AKo against 66 with 66 folded, no six left.
  require(report.max_folded_equity_shift > 0.0 && report.max_folded_equity_shift < 0.25,
          "dead folded cards shift the 2-way equity");
  require(ca::check_three_way_identities(table, nullptr).checks[5] == 0U,
          "without the heads-up table V6 has no check (the gate program refuses to run so)");

  // Masses read by the trainer sum to N.
  for (std::uint8_t first = 0U; first < class_count;
       first = static_cast<std::uint8_t>(first + 7U)) {
    for (std::uint8_t second = 0U; second < class_count;
         second = static_cast<std::uint8_t>(second + 5U)) {
      const auto pairs = static_cast<double>(table.entry(ako, first, second).pairs);
      const auto three = table.three_way_mass(ako, first, second);
      require(std::abs(three.hero_alone + three.with_first + three.with_second + three.all_three +
                       three.hero_loses - pairs) < 1e-9,
              "3-active masses sum to N");
      const auto dead = table.two_way_mass(ako, first, second);
      require(std::abs(dead.wins + dead.ties + dead.losses - pairs) < 1e-9,
              "2-active masses with dead folded cards sum to N");
      const auto ignored =
          ca::two_way_mass(table, heads_up, ca::FoldedCards::Ignored, ako, first, second);
      require(std::abs(ignored.wins + ignored.ties + ignored.losses - pairs) < 1e-9,
              "2-active masses with ignored folded cards sum to N");
    }
  }
}

void test_persistence(const ca::RankTable &ranks, const ca::ThreeWayTable &table) {
  const auto directory = std::filesystem::temp_directory_path() / "gtosd_three_way_tests";
  std::filesystem::create_directories(directory);
  const auto path = directory / "preflop_three_way_subset.bin";
  require(table.save(path).has_value(), "subset table saves");
  const auto refused = ca::ThreeWayTable::load(path);
  require(!refused.has_value() && refused.error() == ca::ResourceError::InvalidInput,
          "a subset table is rejected by default (its unbuilt rows would read as zero)");
  const auto loaded = ca::ThreeWayTable::load(path, ca::ThreeWayLoad::AllowPartial);
  require(loaded.has_value() && loaded.value() == table, "subset table round trip on request");
  // Never under the name of the complete table, in any ASCII case.
  require(ca::ThreeWayTable::canonical_file_name(directory / ca::three_way_table_file_name) &&
              ca::ThreeWayTable::canonical_file_name("PREFLOP_Three_Way_V1.BIN") &&
              !ca::ThreeWayTable::canonical_file_name(path) &&
              !ca::ThreeWayTable::canonical_file_name("preflop_three_way_v1.bin.tmp") &&
              !ca::ThreeWayTable::canonical_file_name(
                  std::filesystem::path(ca::three_way_table_file_name) / "x.bin"),
          "canonical file name: the file name only, ASCII case ignored");
  for (const auto &name : {std::string{ca::three_way_table_file_name},
                           std::string{"PREFLOP_THREE_WAY_V1.bin"}}) {
    const auto canonical = directory / name;
    const auto saved = table.save(canonical);
    require(!saved.has_value() && saved.error() == ca::ResourceError::InvalidInput &&
                !std::filesystem::exists(canonical),
            "a subset is not written under the name of the complete table");
  }
  const auto header = 8U + 4U + 4U + 25U + 4U + table.fingerprint().size() + 8U;
  require(std::filesystem::file_size(path) == header + ca::ThreeWayTable::payload_bytes() + 8U,
          "file size = header + 21,257,810 payload bytes + checksum");
  {
    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
    file.seekp(static_cast<std::streamoff>(header + 1000U));
    const char flipped = 0x5A;
    file.write(&flipped, 1);
  }
  const auto corrupted = ca::ThreeWayTable::load(path, ca::ThreeWayLoad::AllowPartial);
  require(!corrupted.has_value() && corrupted.error() == ca::ResourceError::IntegrityFailure,
          "a corrupted table is rejected");
  std::filesystem::remove_all(directory);

  // Thread count and chunking do not change a byte.
  const ca::ThreeWayBuildOptions one_thread{1U, true, {class_of("AKo")}};
  const ca::ThreeWayBuildOptions two_threads{build_threads, true, {class_of("AKo")}};
  const auto single = ca::ThreeWayTable::build(ranks, one_thread);
  const auto parallel = ca::ThreeWayTable::build(ranks, two_threads);
  require(single.has_value() && parallel.has_value() && single.value() == parallel.value(),
          "one and two threads give the same table and fingerprint");
  require(
      rows_equal(table, class_of("AKo"),
                 std::vector<ca::ThreeWayEntry>(
                     single.value().entries().begin() +
                         static_cast<std::ptrdiff_t>(
                             ca::ThreeWayTable::entry_index(class_of("AKo"), 0U, 0U)),
                     single.value().entries().begin() +
                         static_cast<std::ptrdiff_t>(
                             ca::ThreeWayTable::entry_index(class_of("AKo"), 0U, 0U) + 81U * 81U))),
      "a one-class build equals the same rows of a larger subset");
  require(single.value().fingerprint() != table.fingerprint(), "the fingerprint covers the subset");

  const ca::ThreeWayBuildOptions invalid{0U, true, {}};
  require(!ca::ThreeWayTable::build(ranks, invalid).has_value(), "zero threads are rejected");
  const ca::ThreeWayBuildOptions duplicate{1U, true, {3U, 3U}};
  require(!ca::ThreeWayTable::build(ranks, duplicate).has_value(),
          "duplicate hero classes are rejected");
}

// The complete table of the resources, when it exists: it loads by default,
// every hero class is built, and the rows of the subset's hero classes are
// the subset's rows. Returns whether the file was there.
bool test_canonical_table(const std::filesystem::path &resources_dir,
                          const ca::ThreeWayTable &subset) {
  const auto path = resources_dir / ca::three_way_table_file_name;
  if (resources_dir.empty() || !std::filesystem::exists(path)) {
    return false;
  }
  const auto loaded = ca::ThreeWayTable::load(path);
  require(loaded.has_value() && loaded.value().complete(),
          "the complete table loads by default and has every hero class");
  const auto &table = loaded.value();
  for (std::uint8_t hero = 0U; hero < class_count; ++hero) {
    require(table.representatives()[hero] == ca::ThreeWayTable::representative_of(hero),
            "the complete table uses the class representatives");
    if (!subset.hero_built(hero)) {
      continue;
    }
    for (std::uint8_t first = 0U; first < class_count; ++first) {
      for (std::uint8_t second = 0U; second < class_count; ++second) {
        require(table.entry(hero, first, second) == subset.entry(hero, first, second),
                "the complete table has the subset's rows");
      }
    }
  }
  return true;
}

} // namespace

int main(const int argc, char **argv) {
  try {
    const auto started = Clock::now();
    std::filesystem::path resources_dir;
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string_view name = argv[index];
      if (name == "--resources-dir") {
        resources_dir = argv[index + 1];
      } else {
        throw std::runtime_error("unknown argument " + std::string{name});
      }
    }
    std::optional<ca::RankTable> ranks;
    std::optional<ca::AllInTable> heads_up;
    if (!resources_dir.empty()) {
      auto loaded = ca::RankTable::load(resources_dir / "rank_table_v1.bin");
      if (loaded) {
        ranks.emplace(std::move(loaded.value()));
      }
      auto loaded_heads_up = ca::AllInTable::load(resources_dir / "preflop_all_in_v1.bin");
      if (loaded_heads_up) {
        heads_up.emplace(std::move(loaded_heads_up.value()));
      }
    }
    const bool ranks_loaded = ranks.has_value();
    if (!ranks) {
      auto built = ca::RankTable::build();
      require(built.has_value(), "rank table builds");
      ranks.emplace(std::move(built.value()));
    }
    // V6 always runs: without the resource the heads-up table is built.
    const bool heads_up_loaded = heads_up.has_value();
    if (!heads_up) {
      auto built = ca::AllInTable::build(ranks.value(), build_threads);
      require(built.has_value(), "heads-up all-in table builds");
      heads_up.emplace(std::move(built.value()));
    }
    std::cout << "rank table " << (ranks_loaded ? "loaded" : "built") << ", heads-up table "
              << (heads_up_loaded ? "loaded" : "built") << '\n';

    test_pair_counts();
    test_combo_triple_counter(ranks.value());

    auto phase = Clock::now();
    const ca::ThreeWayBuildOptions subset{
        build_threads, true, {class_of("AA"), class_of("AKs"), class_of("AKo")}};
    ca::ThreeWayBuildTiming timing;
    const auto table = ca::ThreeWayTable::build(ranks.value(), subset, &timing);
    require(table.has_value(), "subset table builds");
    const auto build_seconds = seconds_since(phase);

    phase = Clock::now();
    test_subset_table(ranks.value(), table.value(), heads_up.value());
    const auto check_seconds = seconds_since(phase);
    test_persistence(ranks.value(), table.value());
    const bool canonical = test_canonical_table(resources_dir, table.value());
    std::cout << "complete table " << ca::three_way_table_file_name << ": "
              << (canonical ? "loaded, subset rows equal" : "absent from the resources, not checked")
              << '\n';

    std::cout << "CARD_ABSTRACTION_THREE_WAY=PASS assertions=" << assertions
              << " subset_build_seconds=" << build_seconds
              << " subset_task_seconds_AA=" << timing.hero_task_seconds[class_of("AA")]
              << " AKs=" << timing.hero_task_seconds[class_of("AKs")]
              << " AKo=" << timing.hero_task_seconds[class_of("AKo")]
              << " check_seconds=" << check_seconds << " total_seconds=" << seconds_since(started)
              << " subset_fingerprint=" << table.value().fingerprint() << '\n';
  } catch (const std::exception &error) {
    std::cerr << "CARD_ABSTRACTION_THREE_WAY=FAIL " << error.what() << '\n';
    return 1;
  }
  return 0;
}
