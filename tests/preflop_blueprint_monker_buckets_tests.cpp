// Exact-flop bucket tables of gtosd_preflop_blueprint_monker_buckets
// (--flop-exact): on named physical flops (rainbow, two-tone, monotone,
// paired, trips) two live hands share their id, looked up through the
// catalog as BoardContext does, exactly when a suit permutation that fixes
// the flop maps one hand onto the other (checked pair by pair); on every
// canonical flop the ids are dense and in bijection with the suit orbits; the
// capacity is the largest orbit count; optionally the turn and river tables
// keep the fingerprints of the non-exact run with the same settings, and the
// board class rows of a texture map take the table capacities.
// A non-exact flop table (the default builder path, or per-street
// --flop-levels/--turn-levels/--river-levels) is checked by fingerprint only
// (--expect-flop is then required): the default and per-street runs must stay
// byte-identical to the tables of the builder before per-street settings.
// --street-settings present|absent checks the builder report of the directory
// (the key is written only when a street differs from --levels/--tiers).
// Exact turn and river tables (--turn-exact, --river-exact, recognized by
// their recipe) get the same checks on named physical boards and on every
// canonical flop+turn (flop as a set, turn fixed) or five-card board.
#include "preflop_blueprint_test_support.hpp"

#include "gtosd/preflop_blueprint/board_class_rows.hpp"
#include "gtosd/preflop_blueprint/board_texture.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace pb_test;

using Flop = std::array<gtosd::CardId, 3>;
constexpr std::uint32_t flop_live_combos = 528U;

// Suit permutations (the identity included) mapping the flop onto itself as
// a card set, found from the cards (not from the builder's maps).
std::vector<ca::SuitPermutation> flop_stabilizer(const Flop &flop) {
  auto sorted = flop;
  std::sort(sorted.begin(), sorted.end());
  std::vector<ca::SuitPermutation> stabilizer;
  for (const auto &permutation : ca::all_suit_permutations()) {
    Flop image{};
    for (std::size_t card = 0U; card < image.size(); ++card) {
      image[card] = ca::permute_card(flop[card], permutation);
    }
    std::sort(image.begin(), image.end());
    if (image == sorted) {
      stabilizer.push_back(permutation);
    }
  }
  return stabilizer;
}

std::uint64_t flop_mask(const Flop &flop) {
  std::uint64_t mask = 0U;
  for (const auto card : flop) {
    mask |= card.mask();
  }
  return mask;
}

// Live hands of a flop as card pairs in combo order.
std::vector<std::array<gtosd::CardId, 2>> live_hands(const Flop &flop) {
  const auto mask = flop_mask(flop);
  std::vector<std::array<gtosd::CardId, 2>> hands;
  for (std::uint8_t first = 0U; first < ca::deck_cards; ++first) {
    for (auto second = static_cast<std::uint8_t>(first + 1U); second < ca::deck_cards; ++second) {
      const auto a = gtosd::CardId::from_index(first).value();
      const auto b = gtosd::CardId::from_index(second).value();
      if (((a.mask() | b.mask()) & mask) == 0U) {
        hands.push_back({a, b});
      }
    }
  }
  return hands;
}

std::string flop_name(const Flop &flop) {
  return gtosd::format_card(flop[0]) + gtosd::format_card(flop[1]) + gtosd::format_card(flop[2]);
}

struct FlopSummary {
  std::uint32_t stabilizer{0U};
  std::uint32_t distinct_ids{0U};
  std::uint64_t isomorphic_pairs{0U};
};

// Pair by pair on a physical flop: equal ids exactly for isomorphic hands.
FlopSummary check_physical_flop(const ca::BoardCatalog &catalog, const ca::BucketTable &table,
                                const Flop &flop) {
  const auto lookup = catalog.lookup_flop(flop);
  require(lookup.has_value(), "physical flop found in the catalog");
  const auto stabilizer = flop_stabilizer(flop);
  const auto hands = live_hands(flop);
  require(hands.size() == flop_live_combos, "528 live hands on a flop");
  std::vector<std::uint16_t> ids(hands.size());
  std::vector<std::uint8_t> seen(table.capacity(), 0U);
  FlopSummary summary;
  summary.stabilizer = static_cast<std::uint32_t>(stabilizer.size());
  for (std::size_t hand = 0U; hand < hands.size(); ++hand) {
    const auto canonical =
        ca::combo_index(ca::permute_card(hands[hand][0], lookup.value().permutation),
                        ca::permute_card(hands[hand][1], lookup.value().permutation));
    ids[hand] = table.bucket(lookup.value().index, canonical);
    require(ids[hand] != ca::no_bucket && ids[hand] < table.capacity(),
            "live hand holds an id below the capacity");
    if (seen[ids[hand]] == 0U) {
      seen[ids[hand]] = 1U;
      ++summary.distinct_ids;
    }
  }
  for (std::size_t left = 0U; left < hands.size(); ++left) {
    for (std::size_t right = left + 1U; right < hands.size(); ++right) {
      bool isomorphic = false;
      for (const auto &permutation : stabilizer) {
        const auto first = ca::permute_card(hands[left][0], permutation);
        const auto second = ca::permute_card(hands[left][1], permutation);
        if ((first == hands[right][0] && second == hands[right][1]) ||
            (first == hands[right][1] && second == hands[right][0])) {
          isomorphic = true;
          break;
        }
      }
      summary.isomorphic_pairs += isomorphic ? 1U : 0U;
      require((ids[left] == ids[right]) == isomorphic,
              "exact flop: equal ids exactly for suit-isomorphic hands on " + flop_name(flop));
    }
  }
  return summary;
}

void test_named_flops(const ca::BoardCatalog &catalog, const ca::BucketTable &table) {
  struct Named {
    std::string_view kind;
    std::array<std::string_view, 3> cards;
    std::uint32_t stabilizer;
  };
  // Stabilizer sizes: the suits outside the flop permute freely, a paired
  // flop also swaps the suits of its pair when the kicker allows it.
  const std::array<Named, 9> named{{{"rainbow", {"As", "Kd", "9c"}, 1U},
                                    {"rainbow low", {"8h", "7d", "6c"}, 1U},
                                    {"two-tone", {"Ah", "Kh", "9c"}, 2U},
                                    {"monotone", {"Ah", "Kh", "9h"}, 6U},
                                    {"monotone connected", {"Ts", "9s", "8s"}, 6U},
                                    {"paired rainbow", {"As", "Ad", "9c"}, 2U},
                                    {"paired two-tone", {"As", "Ad", "9s"}, 2U},
                                    {"paired low", {"6h", "6c", "Qd"}, 2U},
                                    {"trips", {"7s", "7h", "7d"}, 6U}}};
  for (const auto &entry : named) {
    const Flop flop{card(entry.cards[0]), card(entry.cards[1]), card(entry.cards[2])};
    const auto summary = check_physical_flop(catalog, table, flop);
    require(summary.stabilizer == entry.stabilizer,
            "stabilizer size of " + std::string(entry.kind) + " flop");
    if (entry.stabilizer == 1U) {
      require(summary.distinct_ids == flop_live_combos && summary.isomorphic_pairs == 0U,
              "a flop without symmetry gives every live hand its own id");
    } else {
      require(summary.distinct_ids < flop_live_combos && summary.isomorphic_pairs > 0U,
              "a symmetric flop merges its isomorphic hands");
    }
    std::cout << "exact flop " << entry.kind << " " << flop_name(flop) << ": stabilizer "
              << summary.stabilizer << ", distinct ids " << summary.distinct_ids
              << ", isomorphic pairs " << summary.isomorphic_pairs << ", pairs checked "
              << flop_live_combos * (flop_live_combos - 1U) / 2U << '\n';
  }
}

// Every canonical flop: ids dense in 0..orbits-1 and in bijection with the
// orbits (orbit representative = smallest combo index of the image set).
void test_canonical_flops(const ca::BoardCatalog &catalog, const ca::BucketTable &table) {
  std::uint32_t largest = 0U;
  std::uint32_t smallest = flop_live_combos;
  // Distinct ids per flop by suit pattern (distinct suits) and rank pattern.
  std::map<std::string, std::pair<std::uint32_t, std::uint32_t>> patterns;
  for (std::uint32_t index = 0U; index < catalog.flops().size(); ++index) {
    const auto &flop = catalog.flops()[index].cards;
    const auto stabilizer = flop_stabilizer(flop);
    const auto hands = live_hands(flop);
    std::vector<std::uint16_t> id_of_orbit(ca::combo_count, ca::no_bucket);
    std::vector<std::uint16_t> orbit_of_id(table.capacity(), ca::no_bucket);
    std::uint32_t orbits = 0U;
    for (const auto &hand : hands) {
      const auto combo = ca::combo_index(hand[0], hand[1]);
      auto representative = combo;
      for (const auto &permutation : stabilizer) {
        representative =
            std::min(representative, ca::combo_index(ca::permute_card(hand[0], permutation),
                                                     ca::permute_card(hand[1], permutation)));
      }
      const auto id = table.bucket(index, combo);
      require(id != ca::no_bucket && id < table.capacity(), "canonical live combo holds an id");
      if (id_of_orbit[representative] == ca::no_bucket) {
        id_of_orbit[representative] = id;
        ++orbits;
      }
      require(id_of_orbit[representative] == id, "an orbit holds one id");
      if (orbit_of_id[id] == ca::no_bucket) {
        orbit_of_id[id] = representative;
      }
      require(orbit_of_id[id] == representative, "an id holds one orbit");
    }
    for (std::uint32_t id = 0U; id < orbits; ++id) {
      require(orbit_of_id[id] != ca::no_bucket, "ids are dense below the orbit count");
    }
    largest = std::max(largest, orbits);
    smallest = std::min(smallest, orbits);
    std::array<std::uint8_t, 4> suits{};
    std::array<std::uint8_t, 9> ranks{};
    for (const auto card : flop) {
      suits[static_cast<std::size_t>(card.suit())] = 1U;
      ++ranks[static_cast<std::size_t>(card.rank())];
    }
    const int distinct_suits = suits[0] + suits[1] + suits[2] + suits[3];
    const int top = *std::max_element(ranks.begin(), ranks.end());
    const std::string pattern = std::string(distinct_suits == 3   ? "rainbow"
                                            : distinct_suits == 2 ? "two-tone"
                                                                  : "monotone") +
                                (top == 3   ? " trips"
                                 : top == 2 ? " paired"
                                            : "");
    auto &[count, ids] = patterns[pattern];
    // Every flop of a pattern has the same stabilizer shape, so the same
    // orbit count.
    require(count == 0U || orbits == ids, "one orbit count per suit and rank pattern: " + pattern);
    ++count;
    ids = orbits;
  }
  require(table.capacity() == largest, "exact flop capacity is the largest orbit count");
  require(largest == flop_live_combos, "a flop without symmetry has 528 orbits");
  std::cout << "exact flop canonical flops " << catalog.flops().size() << ": orbits min "
            << smallest << ", max " << largest << ", capacity " << table.capacity() << '\n';
  for (const auto &[pattern, entry] : patterns) {
    std::cout << "  pattern " << pattern << ": flops " << entry.first << ", ids per flop "
              << entry.second << '\n';
  }
}

// Exact turn and river tables (--turn-exact, --river-exact). A board is a set
// of cards plus cards fixed one by one (the turn of a flop+turn board, as the
// catalog canonicalizes it); its stabilizer is found from the cards.
std::vector<ca::SuitPermutation> board_stabilizer(const std::vector<gtosd::CardId> &set_cards,
                                                  const std::vector<gtosd::CardId> &fixed_cards) {
  auto sorted = set_cards;
  std::sort(sorted.begin(), sorted.end());
  std::vector<ca::SuitPermutation> stabilizer;
  for (const auto &permutation : ca::all_suit_permutations()) {
    auto image = set_cards;
    for (auto &value : image) {
      value = ca::permute_card(value, permutation);
    }
    std::sort(image.begin(), image.end());
    bool fixes = image == sorted;
    for (const auto fixed : fixed_cards) {
      fixes = fixes && ca::permute_card(fixed, permutation) == fixed;
    }
    if (fixes) {
      stabilizer.push_back(permutation);
    }
  }
  return stabilizer;
}

std::vector<std::array<gtosd::CardId, 2>> live_hands_of(const std::uint64_t mask) {
  std::vector<std::array<gtosd::CardId, 2>> hands;
  for (std::uint8_t first = 0U; first < ca::deck_cards; ++first) {
    for (auto second = static_cast<std::uint8_t>(first + 1U); second < ca::deck_cards; ++second) {
      const auto a = gtosd::CardId::from_index(first).value();
      const auto b = gtosd::CardId::from_index(second).value();
      if (((a.mask() | b.mask()) & mask) == 0U) {
        hands.push_back({a, b});
      }
    }
  }
  return hands;
}

std::uint64_t mask_of_cards(const std::vector<gtosd::CardId> &cards) {
  std::uint64_t mask = 0U;
  for (const auto value : cards) {
    mask |= value.mask();
  }
  return mask;
}

std::uint16_t image_combo(const std::array<gtosd::CardId, 2> &hand,
                          const ca::SuitPermutation &permutation) {
  return ca::combo_index(ca::permute_card(hand[0], permutation),
                         ca::permute_card(hand[1], permutation));
}

// Pair by pair on a physical turn or river board looked up through the
// catalog as BoardContext does: equal ids exactly for isomorphic hands.
FlopSummary check_physical_board(const ca::BucketTable &table,
                                 const ca::CanonicalLookup &lookup,
                                 const std::vector<gtosd::CardId> &set_cards,
                                 const std::vector<gtosd::CardId> &fixed_cards,
                                 const std::string &name, const std::size_t live_count) {
  const auto stabilizer = board_stabilizer(set_cards, fixed_cards);
  auto all_cards = set_cards;
  all_cards.insert(all_cards.end(), fixed_cards.begin(), fixed_cards.end());
  const auto hands = live_hands_of(mask_of_cards(all_cards));
  require(hands.size() == live_count, "live hands of " + name);
  std::vector<std::uint16_t> ids(hands.size());
  std::vector<std::uint8_t> seen(table.capacity(), 0U);
  FlopSummary summary;
  summary.stabilizer = static_cast<std::uint32_t>(stabilizer.size());
  for (std::size_t hand = 0U; hand < hands.size(); ++hand) {
    ids[hand] = table.bucket(lookup.index, image_combo(hands[hand], lookup.permutation));
    require(ids[hand] != ca::no_bucket && ids[hand] < table.capacity(),
            "live hand holds an id below the capacity on " + name);
    if (seen[ids[hand]] == 0U) {
      seen[ids[hand]] = 1U;
      ++summary.distinct_ids;
    }
  }
  for (std::size_t left = 0U; left < hands.size(); ++left) {
    for (std::size_t right = left + 1U; right < hands.size(); ++right) {
      const auto target = ca::combo_index(hands[right][0], hands[right][1]);
      bool isomorphic = false;
      for (const auto &permutation : stabilizer) {
        if (image_combo(hands[left], permutation) == target) {
          isomorphic = true;
          break;
        }
      }
      summary.isomorphic_pairs += isomorphic ? 1U : 0U;
      require((ids[left] == ids[right]) == isomorphic,
              "exact ids: equal exactly for suit-isomorphic hands on " + name);
    }
  }
  return summary;
}

std::string board_name(const std::vector<gtosd::CardId> &cards) {
  std::string name;
  for (const auto value : cards) {
    name += gtosd::format_card(value);
  }
  return name;
}

// Named physical turn and river boards with trivial and non-trivial
// stabilizers.
void test_named_exact_boards(const ca::BoardCatalog &catalog, const ca::BucketTable &table) {
  struct Named {
    std::string_view kind;
    std::vector<std::string_view> cards;
    std::uint32_t stabilizer;
  };
  const bool turn = table.street() == ca::BucketStreet::Turn;
  const std::size_t live_count = turn ? 496U : 465U;
  const std::vector<Named> named =
      turn ? std::vector<Named>{{"rainbow + fourth suit", {"As", "Kd", "9c", "7h"}, 1U},
                                {"two-tone + flop suit", {"Ah", "Kh", "9c", "7c"}, 2U},
                                {"monotone + same suit", {"Ah", "Kh", "9h", "7h"}, 6U},
                                {"paired + other suit", {"As", "Ad", "9c", "7h"}, 2U},
                                {"trips + quads", {"7s", "7h", "7d", "7c"}, 6U}}
           : std::vector<Named>{{"rainbow straight", {"6c", "7d", "8h", "9s", "Ts"}, 1U},
                                {"five of a suit", {"Ah", "Kh", "9h", "7h", "6h"}, 6U},
                                {"two pairs", {"As", "Ad", "9c", "9h", "7s"}, 2U},
                                {"full house", {"Ks", "Kh", "Kd", "6c", "6s"}, 2U}};
  for (const auto &entry : named) {
    std::vector<gtosd::CardId> cards;
    for (const auto text : entry.cards) {
      cards.push_back(card(text));
    }
    FlopSummary summary;
    if (turn) {
      std::array<gtosd::CardId, 3> flop{cards[0], cards[1], cards[2]};
      std::sort(flop.begin(), flop.end());
      const auto lookup = catalog.lookup_flop_turn(flop, cards[3]);
      require(lookup.has_value(), "physical turn board found in the catalog");
      summary = check_physical_board(table, lookup.value(), {cards[0], cards[1], cards[2]},
                                     {cards[3]}, board_name(cards), live_count);
    } else {
      const std::array<gtosd::CardId, 5> board{cards[0], cards[1], cards[2], cards[3], cards[4]};
      const auto lookup = catalog.lookup_river_board(board);
      require(lookup.has_value(), "physical river board found in the catalog");
      summary = check_physical_board(table, lookup.value(), cards, {}, board_name(cards),
                                     live_count);
    }
    require(summary.stabilizer == entry.stabilizer,
            "stabilizer size of the " + std::string(entry.kind) + " board");
    require(entry.stabilizer == 1U
                ? summary.distinct_ids == live_count && summary.isomorphic_pairs == 0U
                : summary.distinct_ids < live_count && summary.isomorphic_pairs > 0U,
            "exact ids: one per hand without symmetry, merged isomorphic hands otherwise");
    std::cout << "exact " << ca::bucket_street_name(table.street()) << " " << entry.kind << " "
              << board_name(cards) << ": stabilizer " << summary.stabilizer << ", distinct ids "
              << summary.distinct_ids << ", isomorphic pairs " << summary.isomorphic_pairs
              << '\n';
  }
}

// Every canonical flop+turn (flop set, turn fixed) or five-card board: ids
// dense in 0..orbits-1 and in bijection with the orbits; the capacity is the
// largest orbit count, which a board without symmetry reaches (496 / 465).
void test_canonical_exact_boards(const ca::BoardCatalog &catalog, const ca::BucketTable &table) {
  const bool turn = table.street() == ca::BucketStreet::Turn;
  const auto rows = turn ? catalog.flop_turns().size() : catalog.river_boards().size();
  const std::uint32_t live_count = turn ? 496U : 465U;
  std::uint32_t largest = 0U;
  std::uint32_t smallest = live_count;
  std::uint64_t symmetric = 0U;
  for (std::uint32_t index = 0U; index < rows; ++index) {
    std::vector<gtosd::CardId> set_cards;
    std::vector<gtosd::CardId> fixed_cards;
    if (turn) {
      const auto &entry = catalog.flop_turns()[index];
      set_cards.assign(entry.flop.begin(), entry.flop.end());
      fixed_cards.push_back(entry.turn);
    } else {
      const auto &entry = catalog.river_boards()[index];
      set_cards.assign(entry.cards.begin(), entry.cards.end());
    }
    const auto stabilizer = board_stabilizer(set_cards, fixed_cards);
    symmetric += stabilizer.size() > 1U ? 1U : 0U;
    auto all_cards = set_cards;
    all_cards.insert(all_cards.end(), fixed_cards.begin(), fixed_cards.end());
    std::vector<std::uint16_t> id_of_orbit(ca::combo_count, ca::no_bucket);
    std::vector<std::uint16_t> orbit_of_id(table.capacity(), ca::no_bucket);
    std::uint32_t orbits = 0U;
    for (const auto &hand : live_hands_of(mask_of_cards(all_cards))) {
      const auto combo = ca::combo_index(hand[0], hand[1]);
      auto representative = combo;
      for (const auto &permutation : stabilizer) {
        representative = std::min(representative, image_combo(hand, permutation));
      }
      const auto id = table.bucket(index, combo);
      require(id != ca::no_bucket && id < table.capacity(), "canonical live combo holds an id");
      if (id_of_orbit[representative] == ca::no_bucket) {
        id_of_orbit[representative] = id;
        ++orbits;
      }
      require(id_of_orbit[representative] == id, "an orbit holds one id");
      if (orbit_of_id[id] == ca::no_bucket) {
        orbit_of_id[id] = representative;
      }
      require(orbit_of_id[id] == representative, "an id holds one orbit");
    }
    for (std::uint32_t id = 0U; id < orbits; ++id) {
      require(orbit_of_id[id] != ca::no_bucket, "ids are dense below the orbit count");
    }
    largest = std::max(largest, orbits);
    smallest = std::min(smallest, orbits);
  }
  require(table.capacity() == largest && largest == live_count,
          "exact capacity is the largest orbit count, reached without symmetry");
  std::cout << "exact " << ca::bucket_street_name(table.street()) << " canonical boards " << rows
            << " (" << symmetric << " symmetric): orbits min " << smallest << ", max "
            << largest << ", capacity " << table.capacity() << '\n';
}

void test_board_class_rows(const ca::BoardCatalog &catalog,
                           const std::array<const ca::BucketTable *, 3> &tables,
                           const std::filesystem::path &texture_path) {
  const auto texture = pb::BoardTextureMap::load(texture_path, catalog);
  require(texture.has_value(), "texture map loads");
  const pb::BoardClassRows rows(tables[0]->capacity(), tables[1]->capacity(), tables[2]->capacity(),
                                texture.value());
  constexpr std::array<ca::BucketStreet, 3> streets{ca::BucketStreet::Flop, ca::BucketStreet::Turn,
                                                    ca::BucketStreet::River};
  std::cout << "board class rows " << texture.value().name() << ":";
  for (std::size_t street = 0U; street < streets.size(); ++street) {
    require(rows.matches(*tables[street]), "board class rows match the table capacity");
    require(rows.count(streets[street]) ==
                texture.value().classes(streets[street]) * tables[street]->capacity(),
            "rows per street = classes * capacity");
    std::cout << " " << ca::bucket_street_name(streets[street]) << " "
              << texture.value().classes(streets[street]) << " x " << tables[street]->capacity()
              << " = " << rows.count(streets[street]);
  }
  std::cout << '\n';
  // The largest flop row stays inside the flop count.
  const auto last_class = texture.value().classes(ca::BucketStreet::Flop) - 1U;
  require(rows.row(ca::BucketStreet::Flop, last_class,
                   static_cast<std::uint16_t>(tables[0]->capacity() - 1U)) ==
              rows.count(ca::BucketStreet::Flop) - 1U,
          "last flop row");
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path buckets_dir;
    std::filesystem::path reference_dir;
    std::filesystem::path texture_path;
    std::string expect_flop;
    std::string expect_turn;
    std::string expect_river;
    std::string street_settings;
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string_view name = argv[index];
      const std::string_view value = argv[index + 1];
      if (name == "--buckets-dir") {
        buckets_dir = std::filesystem::path(value);
      } else if (name == "--reference-dir") {
        reference_dir = std::filesystem::path(value);
      } else if (name == "--texture-map") {
        texture_path = std::filesystem::path(value);
      } else if (name == "--expect-flop") {
        expect_flop = value;
      } else if (name == "--expect-turn") {
        expect_turn = value;
      } else if (name == "--expect-river") {
        expect_river = value;
      } else if (name == "--street-settings") {
        street_settings = value;
      } else {
        throw std::runtime_error("unknown argument " + std::string(name));
      }
    }
    require(!buckets_dir.empty(), "--buckets-dir is required");
    const auto flop = ca::BucketTable::load(buckets_dir / "flop_buckets_v1.bin");
    const auto turn = ca::BucketTable::load(buckets_dir / "turn_buckets_v1.bin");
    const auto river = ca::BucketTable::load(buckets_dir / "river_buckets_v1.bin");
    require(flop.has_value() && turn.has_value() && river.has_value(), "bucket tables load");
    require(flop.value().street() == ca::BucketStreet::Flop &&
                turn.value().street() == ca::BucketStreet::Turn &&
                river.value().street() == ca::BucketStreet::River,
            "tables of the three streets");
    const bool exact =
        flop.value().feature_fingerprint().find("|ids=suit_orbits|") != std::string::npos;
    require(exact || !expect_flop.empty(), "a non-exact flop table is checked by --expect-flop");
    require(street_settings.empty() || street_settings == "present" || street_settings == "absent",
            "--street-settings takes present or absent");
    const auto catalog = ca::BoardCatalog::build();
    require(flop.value().catalog_fingerprint() == catalog.fingerprint(), "catalog fingerprint");
    std::cout << "tables " << buckets_dir.generic_string() << ": "
              << (exact ? "exact" : "levels x tiers") << " flop, capacities "
              << flop.value().capacity() << "/" << turn.value().capacity() << "/"
              << river.value().capacity() << ", fingerprints " << flop.value().fingerprint() << " "
              << turn.value().fingerprint() << " " << river.value().fingerprint() << '\n';
    if (exact) {
      test_named_flops(catalog, flop.value());
      test_canonical_flops(catalog, flop.value());
    }
    for (const auto *table : {&turn.value(), &river.value()}) {
      if (table->feature_fingerprint().find("|ids=suit_orbits|") != std::string::npos) {
        test_named_exact_boards(catalog, *table);
        test_canonical_exact_boards(catalog, *table);
      }
    }
    if (!expect_flop.empty()) {
      require(flop.value().fingerprint() == expect_flop, "flop fingerprint unchanged");
    }
    if (!expect_turn.empty()) {
      require(turn.value().fingerprint() == expect_turn, "turn fingerprint unchanged");
    }
    if (!expect_river.empty()) {
      require(river.value().fingerprint() == expect_river, "river fingerprint unchanged");
    }
    if (!reference_dir.empty()) {
      const auto reference_flop = ca::BucketTable::load(reference_dir / "flop_buckets_v1.bin");
      const auto reference_turn = ca::BucketTable::load(reference_dir / "turn_buckets_v1.bin");
      const auto reference_river = ca::BucketTable::load(reference_dir / "river_buckets_v1.bin");
      require(reference_flop.has_value() && reference_turn.has_value() &&
                  reference_river.has_value(),
              "reference tables load");
      require(reference_turn.value() == turn.value() && reference_river.value() == river.value(),
              "turn and river equal the reference tables");
      if (exact) {
        require(reference_flop.value().fingerprint() != flop.value().fingerprint(),
                "the exact flop differs from the reference flop");
      } else {
        require(reference_flop.value() == flop.value(), "the flop equals the reference table");
      }
      std::cout << "reference " << reference_dir.generic_string()
                << ": turn and river identical, flop " << reference_flop.value().fingerprint()
                << " (capacity " << reference_flop.value().capacity() << ")\n";
    }
    if (!texture_path.empty()) {
      test_board_class_rows(catalog, {&flop.value(), &turn.value(), &river.value()}, texture_path);
    }
    if (!street_settings.empty()) {
      std::ifstream input(buckets_dir / "monker_buckets_report.json", std::ios::binary);
      require(static_cast<bool>(input), "builder report opens");
      const std::string report((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
      require(report.find("\"total_seconds\"") != std::string::npos, "builder report is complete");
      const bool present = report.find("\"street_settings\"") != std::string::npos;
      require(present == (street_settings == "present"),
              "street_settings in the builder report exactly when a street differs");
      std::cout << "builder report: street_settings " << (present ? "present" : "absent") << '\n';
    }
    std::cout << "PREFLOP_BLUEPRINT_MONKER_BUCKETS_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_MONKER_BUCKETS_TESTS=FAIL " << error.what() << " after "
              << assertions << " assertions\n";
    return 1;
  }
}
