#include "gtosd/equity/showdown.hpp"
#include "gtosd/isomorphism/isomorphism.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::uint64_t assertions = 0;
std::uint64_t exhaustive_flops = 0;
std::uint64_t permuted_flops = 0;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

gtosd::CardId card(const std::string_view text) {
  const auto parsed = gtosd::parse_card(text);
  require(parsed.has_value(), "card parses");
  return parsed.value();
}

gtosd::RangeWeight weight(const std::int64_t basis_points) {
  const auto parsed = gtosd::RangeWeight::from_basis_points(basis_points);
  require(parsed.has_value(), "range weight parses");
  return parsed.value();
}

std::uint64_t mask(std::initializer_list<gtosd::CardId> cards) {
  const std::vector<gtosd::CardId> values(cards);
  return gtosd::card_mask(values).value();
}

gtosd::CanonicalStateInput golden_input() {
  gtosd::CanonicalStateInput input;
  input.game_version = 4;
  input.public_state =
      gtosd::make_hu_postflop_state(gtosd::Street::Flop, gtosd::Money::from_antes(10).value(),
                                    gtosd::Money::from_antes(100).value(),
                                    mask({card("As"), card("Qd"), card("7c")}))
          .value();
  input.betting_history = "CO:X|BTN:B5000|CO:C";
  input.ranges[0] = {{{card("Ks"), card("Js")}, weight(10'000)},
                     {{card("Ac"), card("Kc")}, weight(4'000)}};
  input.ranges[1] = {{{card("9h"), card("8h")}, weight(7'500)}};
  input.private_deal = {{card("Ts"), card("9s")}, {card("6c"), card("6d")}};
  input.dead_cards = {card("6h")};
  input.future_cards = {card("8c")};
  input.nodelocks = {{input.public_state.board_mask,
                      {card("Ks"), card("Js")},
                      0,
                      "CO:X|BTN:B5000",
                      {weight(2'500), weight(7'500)}}};
  return input;
}

gtosd::SuitPermutation golden_permutation() {
  return {{gtosd::Suit::Diamonds, gtosd::Suit::Clubs, gtosd::Suit::Spades, gtosd::Suit::Hearts}};
}

void test_all_permutations_and_inverse_mapping() {
  const auto &permutations = gtosd::all_suit_permutations();
  require(permutations.size() == 24U, "exactly 24 suit permutations");
  std::set<std::string> names;
  for (const auto &permutation : permutations) {
    names.insert(gtosd::permutation_name(permutation));
    const auto inverse = gtosd::inverse_permutation(permutation);
    require(inverse.has_value(), "every generated permutation has inverse");
    for (const auto physical : gtosd::short_deck()) {
      const auto canonical = gtosd::transform_card(physical, permutation);
      require(canonical.has_value(), "card transforms");
      const auto round_trip = gtosd::transform_card(canonical.value(), inverse.value());
      require(round_trip.has_value() && round_trip.value() == physical,
              "card permutation round trip exact");
    }
  }
  require(names.size() == 24U, "all permutation names unique");

  const auto input = golden_input();
  const auto transformed = gtosd::transform_state(input, golden_permutation());
  require(transformed.has_value(), "global state transforms");
  require((transformed.value().public_state.board_mask & card("Ah").mask()) != 0U,
          "board transformed with global permutation");
  require(std::ranges::any_of(transformed.value().ranges[0],
                              [](const auto &entry) {
                                return entry.combo.first == card("Kh") ||
                                       entry.combo.second == card("Kh");
                              }),
          "range transformed with global permutation");
  require(transformed.value().private_deal[0].first == card("Th"),
          "private deal transformed without changing player order");
  require(transformed.value().dead_cards.front() == card("6s"), "dead card transformed");
  require(transformed.value().future_cards.front() == card("8d"), "future card transformed");
  require(transformed.value().nodelocks.front().private_hand.first == card("Kh"),
          "nodelock private hand transformed");

  const auto inverse = gtosd::inverse_permutation(golden_permutation()).value();
  const auto round_trip = gtosd::transform_state(transformed.value(), inverse);
  require(round_trip.has_value(), "global inverse transform succeeds");
  require(round_trip.value() == input, "global state round trip is lossless and GUI-ready");
}

void test_golden_global_key_and_counterexample() {
  const auto original = golden_input();
  const auto mapped = gtosd::transform_state(original, golden_permutation());
  require(mapped.has_value(), "golden mapped state created");
  const auto first = gtosd::canonicalize_state(original);
  const auto second = gtosd::canonicalize_state(mapped.value());
  require(first.has_value() && second.has_value(), "golden states canonicalize");
  require(first.value().key == second.value().key,
          "globally mapped board ranges private cards and nodelocks share key");

  auto board_only_mapping = mapped.value();
  board_only_mapping.ranges = original.ranges;
  board_only_mapping.private_deal = original.private_deal;
  board_only_mapping.dead_cards = original.dead_cards;
  board_only_mapping.future_cards = original.future_cards;
  board_only_mapping.nodelocks = original.nodelocks;
  const auto broken = gtosd::canonicalize_state(board_only_mapping);
  require(broken.has_value(), "board-only counterexample remains a valid input");
  require(first.value().key != broken.value().key,
          "board-only canonicalization cannot erase blocker differences");
}

void test_every_physical_flop_under_every_permutation() {
  const auto deck = gtosd::short_deck();
  std::set<std::string> canonical_flops;
  for (std::size_t first = 0; first < deck.size(); ++first) {
    for (std::size_t second = first + 1U; second < deck.size(); ++second) {
      for (std::size_t third = second + 1U; third < deck.size(); ++third) {
        gtosd::CanonicalStateInput input;
        input.public_state.board_mask =
            deck[first].mask() | deck[second].mask() | deck[third].mask();
        const auto baseline = gtosd::canonicalize_state(input);
        require(baseline.has_value(), "physical flop canonicalizes");
        canonical_flops.insert(baseline.value().key);
        ++exhaustive_flops;
        for (const auto &permutation : gtosd::all_suit_permutations()) {
          const auto transformed = gtosd::transform_state(input, permutation);
          require(transformed.has_value(), "permuted flop state transforms");
          const auto canonical = gtosd::canonicalize_state(transformed.value());
          require(canonical.has_value() && canonical.value().key == baseline.value().key,
                  "all 24 variants of physical flop have identical canonical key");
          ++permuted_flops;
        }
      }
    }
  }
  require(exhaustive_flops == 7'140U, "all choose(36,3) physical flops visited");
  require(permuted_flops == 171'360U, "all physical flop permutation pairs visited");
  require(canonical_flops.size() == 573U, "Short Deck physical flops reduce to 573 suit orbits");
}

void test_chance_aggregation_and_multiplicity() {
  gtosd::CanonicalStateInput parent;
  parent.public_state.board_mask = mask({card("As"), card("Qs"), card("7s")});
  std::vector<gtosd::CardId> legal;
  for (const auto deck_card : gtosd::short_deck()) {
    if ((deck_card.mask() & parent.public_state.board_mask) == 0U) {
      legal.push_back(deck_card);
    }
  }
  const auto aggregated = gtosd::aggregate_chance_outcomes(parent, legal);
  require(aggregated.has_value(), "physical turns aggregate");
  require(aggregated.value().size() == 15U, "monotone flop has 15 canonical turn children");
  std::uint32_t physical_total = 0;
  bool saw_three = false;
  for (const auto &edge : aggregated.value()) {
    require(edge.physical_outcome_count == edge.physical_cards.size(),
            "edge multiplicity equals retained physical outcomes");
    require(edge.total_legal_outcome_count == 33U, "chance denominator remains physical");
    require(edge.physical_outcome_count == 1U || edge.physical_outcome_count == 3U,
            "monotone orbit multiplicity is exact");
    physical_total += edge.physical_outcome_count;
    saw_three = saw_three || edge.physical_outcome_count == 3U;
  }
  require(saw_three, "canonical chance edge represents multiple physical cards");
  require(physical_total == legal.size(), "no physical chance outcome lost");

  parent.ranges[0] = {{{card("Kc"), card("Jc")}, weight(10'000)}};
  const auto blocker_sensitive = gtosd::aggregate_chance_outcomes(parent, legal);
  require(blocker_sensitive.has_value(), "range-aware chance aggregation succeeds");
  require(blocker_sensitive.value().size() > aggregated.value().size(),
          "range suits break otherwise valid chance symmetries");
  physical_total = 0;
  for (const auto &edge : blocker_sensitive.value()) {
    physical_total += edge.physical_outcome_count;
  }
  require(physical_total == legal.size(), "range-aware aggregation remains lossless");
}

void test_showdown_ev_and_strategy_invariance() {
  const std::vector<std::array<gtosd::CardId, 2>> hands{{card("Ks"), card("Js")},
                                                        {card("Ac"), card("Kc")}};
  const std::vector<gtosd::CardId> board{card("As"), card("Qd"), card("7c"), card("Th"),
                                         card("9d")};
  const auto baseline = gtosd::evaluate_showdown(hands, board);
  require(baseline.has_value(), "baseline showdown evaluates");
  for (const auto &permutation : gtosd::all_suit_permutations()) {
    std::vector<std::array<gtosd::CardId, 2>> mapped_hands = hands;
    for (auto &hand : mapped_hands) {
      hand[0] = gtosd::transform_card(hand[0], permutation).value();
      hand[1] = gtosd::transform_card(hand[1], permutation).value();
    }
    std::vector<gtosd::CardId> mapped_board = board;
    for (auto &board_card : mapped_board) {
      board_card = gtosd::transform_card(board_card, permutation).value();
    }
    const auto mapped = gtosd::evaluate_showdown(mapped_hands, mapped_board);
    require(mapped.has_value(), "mapped showdown evaluates");
    require(mapped.value().winner_mask == baseline.value().winner_mask,
            "winner and therefore terminal EV invariant");
    require(mapped.value().values == baseline.value().values,
            "exact hand values invariant under suits");
  }

  const auto canonical = gtosd::canonicalize_state(golden_input()).value();
  const auto restored =
      gtosd::transform_state(canonical.state, canonical.canonical_to_physical).value();
  require(restored.nodelocks.front().action_weights ==
              golden_input().nodelocks.front().action_weights,
          "strategy weights preserved through inverse mapping");
  require(restored.nodelocks.front().private_hand == golden_input().nodelocks.front().private_hand,
          "nodelock infoset restored to physical suits");
}

void test_cache_orbit_and_errors() {
  gtosd::CanonicalKeyCache cache;
  const auto input = golden_input();
  const auto first = cache.canonicalize(input);
  const auto second = cache.canonicalize(input);
  require(first.has_value() && second.has_value() && first.value().key == second.value().key,
          "cache returns exact canonical state");
  require(cache.stats().queries == 2U && cache.stats().hits == 1U && cache.stats().misses == 1U,
          "cache statistics measured");
  require(cache.hit_rate() == 0.5, "cache hit rate exact");
  require(cache.stats().hash_collisions == 0U, "no hash collision in fixture");

  const auto orbit = gtosd::audit_orbit(input);
  require(orbit.has_value() && orbit.value().size() == 24U, "audit returns complete orbit");
  require(std::ranges::count_if(orbit.value(),
                                [](const auto &entry) { return entry.is_canonical; }) == 1,
          "asymmetric global fixture has one canonical representative");

  auto duplicate_range = input;
  duplicate_range.ranges[0].push_back(duplicate_range.ranges[0].front());
  const auto invalid_range = gtosd::canonicalize_state(duplicate_range);
  require(!invalid_range && invalid_range.error() == gtosd::IsomorphismError::InvalidRange,
          "duplicate combo rejected explicitly");
  const auto invalid_range_transform =
      gtosd::transform_range(duplicate_range.ranges[0], gtosd::all_suit_permutations().front());
  require(!invalid_range_transform &&
              invalid_range_transform.error() == gtosd::IsomorphismError::InvalidRange,
          "standalone range transform enforces uniqueness");

  auto blocked_range = input;
  blocked_range.ranges[1] = {{{card("As"), card("Kh")}, weight(10'000)}};
  const auto invalid_blocked_range = gtosd::canonicalize_state(blocked_range);
  require(!invalid_blocked_range &&
              invalid_blocked_range.error() == gtosd::IsomorphismError::InvalidRange,
          "range combo blocked by public board rejected explicitly");

  auto invalid_nodelock = input;
  invalid_nodelock.nodelocks.front().action_weights = {weight(4'000), weight(4'000)};
  const auto invalid_strategy = gtosd::canonicalize_state(invalid_nodelock);
  require(!invalid_strategy && invalid_strategy.error() == gtosd::IsomorphismError::InvalidNodelock,
          "nodelock strategy must retain a complete probability mass");
  const auto invalid_nodelock_transform = gtosd::transform_nodelock(
      invalid_nodelock.nodelocks.front(), gtosd::all_suit_permutations().front());
  require(!invalid_nodelock_transform &&
              invalid_nodelock_transform.error() == gtosd::IsomorphismError::InvalidNodelock,
          "standalone nodelock transform validates probability mass");

  auto overlapping = input;
  overlapping.dead_cards = {card("As")};
  const auto invalid_physical = gtosd::canonicalize_state(overlapping);
  require(!invalid_physical &&
              invalid_physical.error() == gtosd::IsomorphismError::InvalidPhysicalCards,
          "overlapping physical cards rejected explicitly");

  const auto empty_chance = gtosd::aggregate_chance_outcomes(input, {});
  require(!empty_chance && empty_chance.error() == gtosd::IsomorphismError::EmptyChanceOutcomes,
          "empty chance enumeration cannot silently approximate");
}

} // namespace

int main(const int argc, const char *const argv[]) {
  try {
    const bool focused = argc == 2 && std::string_view(argv[1]) == "--focused";
    if (argc > 2 || (argc == 2 && !focused)) {
      std::cerr << "Usage: gtosd_phase4_tests [--focused]\n";
      return 2;
    }
    test_all_permutations_and_inverse_mapping();
    test_golden_global_key_and_counterexample();
    if (!focused) {
      test_every_physical_flop_under_every_permutation();
    }
    test_chance_aggregation_and_multiplicity();
    test_showdown_ev_and_strategy_invariance();
    test_cache_orbit_and_errors();
    std::cout << "F4_ISOMORPHISM_TESTS=PASS\n"
              << "assertions=" << assertions << '\n'
              << "physical_flops=" << exhaustive_flops << '\n'
              << "permuted_flops=" << permuted_flops << '\n'
              << "canonical_flop_orbits=" << (focused ? 0U : 573U) << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "F4_ISOMORPHISM_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "F4_ISOMORPHISM_TESTS=FAIL: unknown exception\n";
    return 1;
  }
}
