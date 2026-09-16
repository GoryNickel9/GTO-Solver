#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace ca = gtosd::card_abstraction;

std::uint64_t assertions = 0U;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

gtosd::CardId card(const std::uint8_t value) { return gtosd::CardId::from_index(value).value(); }

void test_binomials() {
  require(ca::binomial(36U, 2U) == 630U && ca::binomial(36U, 3U) == 7'140U &&
              ca::binomial(36U, 5U) == 376'992U && ca::binomial(36U, 7U) == 8'347'680U,
          "binomials of the 36-card deck");
  require(ca::binomial(31U, 2U) == 465U && ca::binomial(29U, 2U) == 406U &&
              ca::binomial(34U, 3U) == 5'984U && ca::binomial(32U, 5U) == 201'376U,
          "binomials of live hands and runouts");
  require(ca::binomial(0U, 0U) == 1U && ca::binomial(5U, 7U) == 0U && ca::binomial(37U, 1U) == 0U,
          "binomial edge cases");
}

void test_subset_round_trip(const std::uint32_t size) {
  const auto total = ca::binomial(36U, size);
  std::vector<std::uint8_t> subset(size);
  for (std::uint64_t index = 0U; index < total; ++index) {
    require(ca::subset_from_index(index, subset), "subset_from_index accepts every valid index");
    for (std::size_t position = 1U; position < subset.size(); ++position) {
      require(subset[position] > subset[position - 1U] && subset[position] < 36U,
              "subset is strictly increasing within the deck");
    }
    require(ca::subset_index(subset) == index, "subset_index inverts subset_from_index");
  }
  require(!ca::subset_from_index(total, subset), "index equal to the count is rejected");
}

void test_subset_indexing() {
  for (const auto size : {2U, 3U, 5U}) {
    test_subset_round_trip(size);
  }
  // Seven cards: sample the range densely rather than exhaustively.
  std::vector<std::uint8_t> seven(7U);
  const auto total = ca::binomial(36U, 7U);
  for (std::uint64_t index = 0U; index < total; index += 97U) {
    require(ca::subset_from_index(index, seven) && ca::subset_index(seven) == index,
            "seven-card subset round trip");
  }
  const std::array<std::uint8_t, 3> unsorted{5U, 3U, 9U};
  const std::array<std::uint8_t, 3> repeated{3U, 3U, 9U};
  const std::array<std::uint8_t, 2> out_of_deck{3U, 36U};
  require(ca::subset_index(unsorted) == ca::invalid_subset_index &&
              ca::subset_index(repeated) == ca::invalid_subset_index &&
              ca::subset_index(out_of_deck) == ca::invalid_subset_index,
          "invalid subsets are rejected");
}

void test_combo_index() {
  const auto combos = gtosd::all_combos();
  for (std::size_t index = 0U; index < combos.size(); ++index) {
    const auto id = static_cast<std::uint16_t>(index);
    require(ca::combo_index(combos[index].first, combos[index].second) == id &&
                ca::combo_index(combos[index].second, combos[index].first) == id,
            "combo_index reproduces all_combos order in either argument order");
    const auto back = ca::combo_from_index(id);
    require(back.first == combos[index].first && back.second == combos[index].second,
            "combo_from_index inverts combo_index");
  }
}

void test_permutations() {
  const auto &permutations = ca::all_suit_permutations();
  for (std::size_t i = 0U; i < permutations.size(); ++i) {
    std::array<bool, 4> seen{};
    for (const auto suit : permutations[i]) {
      require(suit < 4U && !seen[suit], "permutation is a bijection on suits");
      seen[suit] = true;
    }
    for (std::size_t j = i + 1U; j < permutations.size(); ++j) {
      require(permutations[i] != permutations[j], "permutations are distinct");
    }
    const auto inverse = ca::inverse_permutation(permutations[i]);
    for (std::uint8_t suit = 0U; suit < 4U; ++suit) {
      require(inverse[permutations[i][suit]] == suit, "inverse permutation");
    }
    for (std::uint8_t value = 0U; value < 36U; ++value) {
      const auto image = ca::permute_card(card(value), permutations[i]);
      require(image.rank() == card(value).rank(), "permutation preserves the rank");
      require(ca::permute_card(image, inverse) == card(value), "inverse restores the card");
    }
  }
  require(permutations[0] == ca::identity_permutation, "first permutation is the identity");
}

void test_flop_canonicalization(const ca::BoardCatalog &catalog) {
  std::uint64_t multiplicity_sum = 0U;
  std::vector<std::uint32_t> counted(catalog.flops().size(), 0U);
  for (std::uint8_t a = 0U; a < 36U; ++a) {
    for (std::uint8_t b = static_cast<std::uint8_t>(a + 1U); b < 36U; ++b) {
      for (std::uint8_t c = static_cast<std::uint8_t>(b + 1U); c < 36U; ++c) {
        const std::array<gtosd::CardId, 3> flop{card(a), card(b), card(c)};
        const auto canonical = ca::canonicalize_flop(flop).value();
        const auto lookup = catalog.lookup_flop(flop).value();
        require(catalog.flops()[lookup.index].code == canonical.code,
                "lookup returns the entry with the canonical code");
        require(canonical.orbit_size == catalog.flops()[lookup.index].multiplicity,
                "orbit size equals the enumerated multiplicity");
        ++counted[lookup.index];
        std::array<gtosd::CardId, 3> image{ca::permute_card(flop[0], lookup.permutation),
                                           ca::permute_card(flop[1], lookup.permutation),
                                           ca::permute_card(flop[2], lookup.permutation)};
        std::sort(image.begin(), image.end());
        require(image == catalog.flops()[lookup.index].cards,
                "returned permutation maps the flop onto the canonical cards");
        for (const auto &permutation : ca::all_suit_permutations()) {
          const std::array<gtosd::CardId, 3> permuted{ca::permute_card(flop[0], permutation),
                                                      ca::permute_card(flop[1], permutation),
                                                      ca::permute_card(flop[2], permutation)};
          require(ca::canonicalize_flop(permuted).value().code == canonical.code,
                  "canonical flop code is invariant under suit permutations");
        }
      }
    }
  }
  for (std::size_t index = 0U; index < counted.size(); ++index) {
    require(counted[index] == catalog.flops()[index].multiplicity,
            "multiplicity counts the physical flops in the class");
    multiplicity_sum += counted[index];
  }
  require(multiplicity_sum == ca::physical_flops, "flop multiplicities sum to 7,140");
  const std::array<gtosd::CardId, 3> duplicate{card(1U), card(1U), card(5U)};
  require(!ca::canonicalize_flop(duplicate).has_value(), "duplicate flop cards rejected");
}

void test_catalog_counts(const ca::BoardCatalog &catalog) {
  require(catalog.flops().size() == ca::canonical_flop_count, "573 canonical flops");
  require(catalog.river_boards().size() == ca::canonical_river_board_count,
          "19,998 canonical five-card boards");
  require(catalog.histories().size() == ca::canonical_board_history_count,
          "369,072 canonical board histories");
  require(catalog.flop_turns().size() >= ca::physical_flop_turns / 24U,
          "canonical flop+turn count respects the orbit lower bound");
  require(catalog.flop_turns().size() == ca::canonical_flop_turn_count,
          "13,761 canonical flop+turn boards");
  std::uint64_t flop_turn_sum = 0U;
  for (const auto &entry : catalog.flop_turns()) {
    flop_turn_sum += entry.multiplicity;
    require(entry.flop_index < catalog.flops().size(), "flop+turn cross reference in range");
    require(catalog.lookup_flop(entry.flop).value().index == entry.flop_index,
            "flop+turn cross reference points to the flop class");
  }
  require(flop_turn_sum == ca::physical_flop_turns, "flop+turn multiplicities sum to 235,620");
  std::uint64_t river_sum = 0U;
  for (const auto &entry : catalog.river_boards()) {
    river_sum += entry.multiplicity;
    require(ca::canonicalize_river_board(entry.cards).value().orbit_size == entry.multiplicity,
            "river board orbit size equals its multiplicity");
  }
  require(river_sum == ca::physical_river_boards, "river multiplicities sum to 376,992");
  std::uint64_t history_sum = 0U;
  for (const auto &entry : catalog.histories()) {
    history_sum += entry.multiplicity;
    const auto canonical = ca::canonicalize_history(entry.history).value();
    require(canonical.code == entry.code && canonical.orbit_size == entry.multiplicity,
            "history representative is canonical with orbit size equal to multiplicity");
    require(catalog.lookup_flop(entry.history.flop).value().index == entry.flop_index &&
                catalog.lookup_flop_turn(entry.history.flop, entry.history.turn).value().index ==
                    entry.flop_turn_index,
            "history cross references point to the flop and flop+turn classes");
    const std::array<gtosd::CardId, 5> all{entry.history.flop[0], entry.history.flop[1],
                                           entry.history.flop[2], entry.history.turn,
                                           entry.history.river};
    require(catalog.lookup_river_board(all).value().index == entry.river_board_index,
            "history cross reference points to the five-card board class");
  }
  require(history_sum == ca::physical_board_histories,
          "history multiplicities sum to 7,539,840");
}

void test_history_invariance(const ca::BoardCatalog &catalog) {
  ca::DeterministicRandom random(0x5031'4849'5354'4F52ULL);
  for (std::uint32_t sample = 0U; sample < 5'000U; ++sample) {
    const auto history = catalog.sample_physical_history(random);
    const auto lookup = catalog.lookup_history(history).value();
    const auto &entry = catalog.histories()[lookup.index];
    ca::BoardHistory image;
    image.flop = {ca::permute_card(history.flop[0], lookup.permutation),
                  ca::permute_card(history.flop[1], lookup.permutation),
                  ca::permute_card(history.flop[2], lookup.permutation)};
    std::sort(image.flop.begin(), image.flop.end());
    image.turn = ca::permute_card(history.turn, lookup.permutation);
    image.river = ca::permute_card(history.river, lookup.permutation);
    require(image == entry.history, "history permutation maps onto the canonical history");
    for (const auto &permutation : ca::all_suit_permutations()) {
      ca::BoardHistory permuted;
      permuted.flop = {ca::permute_card(history.flop[0], permutation),
                       ca::permute_card(history.flop[1], permutation),
                       ca::permute_card(history.flop[2], permutation)};
      std::sort(permuted.flop.begin(), permuted.flop.end());
      permuted.turn = ca::permute_card(history.turn, permutation);
      permuted.river = ca::permute_card(history.river, permutation);
      require(catalog.lookup_history(permuted).value().index == lookup.index,
              "history index is invariant under suit permutations");
    }
  }
}

void test_sampling(const ca::BoardCatalog &catalog) {
  ca::DeterministicRandom first(42U);
  ca::DeterministicRandom second(42U);
  for (std::uint32_t draw = 0U; draw < 1'000U; ++draw) {
    require(first.next() == second.next(), "same seed, same sequence");
  }
  ca::DeterministicRandom random(7U);
  for (std::uint32_t draw = 0U; draw < 100'000U; ++draw) {
    const auto history = catalog.sample_physical_history(random);
    require(history.flop[0] < history.flop[1] && history.flop[1] < history.flop[2],
            "sampled flop is sorted");
    const std::array<gtosd::CardId, 5> all{history.flop[0], history.flop[1], history.flop[2],
                                           history.turn, history.river};
    require(ca::canonicalize_river_board(all).has_value(), "sampled cards are distinct");
  }
  const auto &histories = catalog.histories();
  require(catalog.history_index_from_quantile(0U) == 0U, "quantile zero maps to the first class");
  require(catalog.history_index_from_quantile(histories[0].multiplicity - 1U) == 0U,
          "last quantile of the first class stays in the first class");
  require(catalog.history_index_from_quantile(histories[0].multiplicity) == 1U,
          "next quantile moves to the second class");
  require(catalog.history_index_from_quantile(ca::physical_board_histories - 1U) ==
              static_cast<std::uint32_t>(histories.size() - 1U),
          "last quantile maps to the last class");
  std::vector<std::uint32_t> hits(catalog.flops().size(), 0U);
  ca::DeterministicRandom sampler(11U);
  constexpr std::uint32_t draws = 2'000'000U;
  for (std::uint32_t draw = 0U; draw < draws; ++draw) {
    ++hits[histories[catalog.sample_history_index(sampler)].flop_index];
  }
  for (std::size_t index = 0U; index < hits.size(); ++index) {
    const auto expected = static_cast<double>(catalog.flops()[index].multiplicity) /
                          static_cast<double>(ca::physical_flops) * draws;
    require(std::abs(static_cast<double>(hits[index]) - expected) < 6.0 * std::sqrt(expected) + 5.0,
            "canonical sampler frequency per flop class is within six sigma of its multiplicity");
  }
  for (std::uint32_t bound : {1U, 2U, 7U, 36U, 465U, ca::physical_board_histories}) {
    for (std::uint32_t draw = 0U; draw < 1'000U; ++draw) {
      require(random.uniform_below(bound) < bound, "bounded draw stays below its bound");
    }
  }
}

void test_persistence(const ca::BoardCatalog &catalog) {
  const auto directory = std::filesystem::temp_directory_path() / "gtosd_card_abstraction_tests";
  std::filesystem::create_directories(directory);
  const auto path = directory / "board_catalog_v1.bin";
  require(catalog.save(path).has_value(), "catalog saves");
  const auto loaded = ca::BoardCatalog::load(path);
  require(loaded.has_value(), "catalog loads");
  require(loaded.value() == catalog, "loaded catalog equals the built catalog");
  require(loaded.value().fingerprint() == catalog.fingerprint(), "fingerprint survives the round trip");
  require(loaded.value().lookup_history(catalog.histories()[1234].history).value().index == 1234U,
          "loaded catalog answers lookups");

  std::vector<char> bytes;
  {
    std::ifstream input(path, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  }
  bytes[bytes.size() / 2U] = static_cast<char>(bytes[bytes.size() / 2U] ^ 0x5A);
  const auto corrupted = directory / "board_catalog_corrupted.bin";
  {
    std::ofstream output(corrupted, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  const auto rejected = ca::BoardCatalog::load(corrupted);
  require(!rejected.has_value() && rejected.error() == ca::CatalogError::IntegrityFailure,
          "corrupted catalog is rejected");
  require(!ca::BoardCatalog::load(directory / "missing.bin").has_value(),
          "missing catalog file is an io failure");
}

} // namespace

int main() {
  try {
    const auto started = std::chrono::steady_clock::now();
    test_binomials();
    test_subset_indexing();
    test_combo_index();
    test_permutations();
    ca::CatalogBuildTelemetry telemetry;
    const auto catalog = ca::BoardCatalog::build(&telemetry);
    test_flop_canonicalization(catalog);
    test_catalog_counts(catalog);
    test_history_invariance(catalog);
    test_sampling(catalog);
    test_persistence(catalog);
    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::cout << "CARD_ABSTRACTION_CANONICAL=PASS assertions=" << assertions
              << " flop_turns=" << catalog.flop_turns().size()
              << " fingerprint=" << catalog.fingerprint() << " build_seconds="
              << telemetry.flop_seconds + telemetry.flop_turn_seconds +
                     telemetry.river_board_seconds + telemetry.history_seconds +
                     telemetry.cross_reference_seconds
              << " total_seconds=" << elapsed << '\n';
  } catch (const std::exception &error) {
    std::cerr << "CARD_ABSTRACTION_CANONICAL=FAIL " << error.what() << '\n';
    return 1;
  }
  return 0;
}
