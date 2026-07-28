#include "phase2_oracle.hpp"

#include "gtosd/core/game.hpp"
#include "gtosd/equity/evaluator.hpp"
#include "gtosd/equity/showdown.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::uint64_t assertions = 0;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

gtosd::CardId card(const std::string_view text) {
  const auto parsed = gtosd::parse_card(text);
  require(parsed.has_value(), "fixture card parse");
  return parsed.value();
}

std::array<gtosd::CardId, 5> cards_five(const std::array<std::string_view, 5> &texts) {
  return {card(texts[0]), card(texts[1]), card(texts[2]), card(texts[3]), card(texts[4])};
}

std::array<gtosd::CardId, 7> cards_seven(const std::array<std::string_view, 7> &texts) {
  return {card(texts[0]), card(texts[1]), card(texts[2]), card(texts[3]),
          card(texts[4]), card(texts[5]), card(texts[6])};
}

void test_exhaustive_five_card_oracle_and_suit_invariance() {
  const auto deck = gtosd::short_deck();
  std::array<std::uint64_t, 9> category_counts{};
  std::array<std::uint8_t, 4> permutation{0, 1, 2, 3};
  std::uint64_t combinations = 0;

  for (std::size_t a = 0; a < deck.size() - 4U; ++a) {
    for (std::size_t b = a + 1U; b < deck.size() - 3U; ++b) {
      for (std::size_t c = b + 1U; c < deck.size() - 2U; ++c) {
        for (std::size_t d = c + 1U; d < deck.size() - 1U; ++d) {
          for (std::size_t e = d + 1U; e < deck.size(); ++e) {
            const std::array<gtosd::CardId, 5> hand{deck[a], deck[b], deck[c], deck[d], deck[e]};
            const auto expected = phase2_oracle::evaluate(hand);
            const auto actual_result = gtosd::evaluate_five(hand);
            require(actual_result.has_value(), "valid five-card hand accepted");
            const auto actual = actual_result.value();
            require(actual == expected, "five-card oracle mismatch");
            ++category_counts[static_cast<std::size_t>(actual.category)];

            permutation = {0, 1, 2, 3};
            do {
              std::array<gtosd::CardId, 5> permuted{};
              std::ranges::transform(hand, permuted.begin(), [&permutation](const auto value) {
                return phase2_oracle::permute_suit(value, permutation);
              });
              const auto permuted_value = gtosd::evaluate_five(permuted);
              require(permuted_value.has_value() && permuted_value.value() == actual,
                      "global suit permutation changed five-card value");
            } while (std::ranges::next_permutation(permutation).found);
            ++combinations;
          }
        }
      }
    }
  }

  require(combinations == 376'992U, "all C(36,5) hands enumerated");
  for (const auto count : category_counts) {
    require(count > 0U, "every Short Deck category represented");
  }
}

void test_ranking_and_best_five_regressions() {
  const auto wheel = gtosd::evaluate_five(cards_five({"As", "6s", "7s", "8s", "9s"})).value();
  require(wheel == gtosd::HandValue{gtosd::HandCategory::StraightFlush, {3, 0, 0, 0, 0}},
          "A-6-7-8-9 suited is nine-high straight flush");

  const auto flush = gtosd::evaluate_five(cards_five({"As", "Qs", "Js", "8s", "6s"})).value();
  const auto full = gtosd::evaluate_five(cards_five({"Ah", "Ad", "Ac", "Kh", "Kd"})).value();
  const auto quads = gtosd::evaluate_five(cards_five({"Ah", "Ad", "Ac", "As", "Kd"})).value();
  require(quads > flush && flush > full, "Short Deck quads flush full ordering");

  auto duplicate_five = cards_five({"As", "Ks", "Qs", "Js", "Ts"});
  duplicate_five[4] = duplicate_five[0];
  const auto duplicate_five_result = gtosd::evaluate_five(duplicate_five);
  require(!duplicate_five_result &&
              duplicate_five_result.error() == gtosd::EquityError::DuplicateCard,
          "duplicate five-card input rejected explicitly");

  const auto double_trips = cards_seven({"As", "Ad", "Ac", "Ks", "Kd", "Kc", "Qh"});
  const auto double_trips_value = gtosd::evaluate_seven(double_trips);
  require(double_trips_value.has_value(), "double trips accepted");
  require(double_trips_value.value() ==
              gtosd::HandValue{gtosd::HandCategory::FullHouse, {8, 7, 0, 0, 0}},
          "double trips selects highest full house");

  const auto six_flush = cards_seven({"As", "Ks", "Qs", "9s", "8s", "6s", "7d"});
  const auto six_flush_value = gtosd::evaluate_seven(six_flush);
  require(six_flush_value.has_value(), "six-card flush accepted");
  require(six_flush_value.value() == gtosd::HandValue{gtosd::HandCategory::Flush, {8, 7, 6, 3, 2}},
          "six-card flush selects best five ranks");

  const auto three_pairs = cards_seven({"As", "Ad", "Ks", "Kd", "Qs", "Qd", "Jh"});
  const auto three_pairs_value = gtosd::evaluate_seven(three_pairs);
  require(three_pairs_value.has_value(), "three pairs accepted");
  require(three_pairs_value.value() ==
              gtosd::HandValue{gtosd::HandCategory::TwoPair, {8, 7, 6, 0, 0}},
          "three pairs selects top two and kicker");

  auto duplicate = double_trips;
  duplicate[6] = duplicate[0];
  const auto duplicate_result = gtosd::evaluate_seven(duplicate);
  require(!duplicate_result && duplicate_result.error() == gtosd::EquityError::DuplicateCard,
          "duplicate seven-card input rejected explicitly");
}

class ThrowingEvaluator final : public gtosd::IHandEvaluator {
public:
  [[nodiscard]] gtosd::Result<gtosd::HandValue, gtosd::EquityError>
  evaluate_seven(const std::array<gtosd::CardId, 7> &) const override {
    throw std::runtime_error("oracle failure");
  }

  [[nodiscard]] gtosd::Result<std::vector<gtosd::HandValue>, gtosd::EquityError>
  evaluate_batch(std::span<const std::array<gtosd::CardId, 7>>) const override {
    return gtosd::Result<std::vector<gtosd::HandValue>, gtosd::EquityError>::failure(
        gtosd::EquityError::InternalEvaluatorFailure);
  }
};

void test_showdown_validation_and_winner_masks() {
  const std::vector<gtosd::CardId> unique_winner_board{card("6c"), card("7d"), card("8h"),
                                                       card("9s"), card("Kc")};
  const std::vector<std::array<gtosd::CardId, 2>> winner_fixtures{
      {card("Tc"), card("Ad")}, {card("As"), card("Ah")}, {card("Qs"), card("Qd")},
      {card("Js"), card("Jd")}, {card("Ks"), card("Kd")}, {card("Qc"), card("Qh")}};
  for (std::size_t player_count = 2U; player_count <= 6U; ++player_count) {
    const std::vector<std::array<gtosd::CardId, 2>> players(
        winner_fixtures.begin(),
        winner_fixtures.begin() + static_cast<std::ptrdiff_t>(player_count));
    const auto showdown = gtosd::evaluate_showdown(players, unique_winner_board);
    require(showdown.has_value() && showdown.value().winner_mask == 0b1U,
            "winner mask exact for 2-6 players");
  }

  const std::vector<gtosd::CardId> straight_board{card("6c"), card("7d"), card("8h"), card("9s"),
                                                  card("Tc")};
  const std::vector<std::array<gtosd::CardId, 2>> heads_up{{card("As"), card("Ad")},
                                                           {card("Ks"), card("Kd")}};
  const auto split = gtosd::evaluate_showdown(heads_up, straight_board);
  require(split.has_value() && split.value().winner_mask == 0b11U, "board plays split HU");

  const std::vector<gtosd::CardId> royal_board{card("As"), card("Ks"), card("Qs"), card("Js"),
                                               card("Ts")};
  const std::vector<std::array<gtosd::CardId, 2>> six_players{
      {card("6c"), card("6d")}, {card("7c"), card("7d")}, {card("8c"), card("8d")},
      {card("9c"), card("9d")}, {card("Ah"), card("Ad")}, {card("Kh"), card("Kd")}};
  for (std::size_t player_count = 2U; player_count <= 6U; ++player_count) {
    const std::vector<std::array<gtosd::CardId, 2>> players(
        six_players.begin(), six_players.begin() + static_cast<std::ptrdiff_t>(player_count));
    const auto tied = gtosd::evaluate_showdown(players, royal_board);
    const auto expected_mask = static_cast<std::uint8_t>((1U << player_count) - 1U);
    require(tied.has_value() && tied.value().winner_mask == expected_mask,
            "complete tie mask exact for 2-6 players");
  }
  const auto six_way = gtosd::evaluate_showdown(six_players, royal_board);
  require(six_way.has_value() && six_way.value().winner_mask == 0b11'1111U,
          "six-player complete winner mask");
  require(six_way.value().values.size() == 6U, "one exact value per player");

  const auto payout = gtosd::split_pot(gtosd::Money::from_units(17).value(), {},
                                       gtosd::WinnerMask{six_way.value().winner_mask}, 6U);
  require(payout.has_value(), "six-way tie split accepted");
  std::int64_t paid = 0;
  for (const auto amount : payout.value()) {
    paid += amount.units();
  }
  require(paid == 17, "six-way tie loses no fixed-point unit");

  const std::vector<std::vector<gtosd::CardId>> malformed_hole_cards{{card("6c")},
                                                                     {card("7c"), card("7d")}};
  const auto malformed = gtosd::evaluate_showdown(malformed_hole_cards, royal_board);
  require(!malformed && malformed.error() == gtosd::EquityError::WrongHoleCardCount,
          "wrong hole-card count rejected");

  const auto incomplete_board =
      gtosd::evaluate_showdown(heads_up, std::vector<gtosd::CardId>{card("6c")});
  require(!incomplete_board && incomplete_board.error() == gtosd::EquityError::WrongBoardSize,
          "incomplete board rejected");

  const std::vector<std::array<gtosd::CardId, 2>> overlap{{card("As"), card("6c")},
                                                          {card("Ks"), card("Kd")}};
  const auto overlap_result = gtosd::evaluate_showdown(overlap, royal_board);
  require(!overlap_result && overlap_result.error() == gtosd::EquityError::DuplicateCard,
          "board and hole-card overlap rejected");

  const std::vector<std::array<gtosd::CardId, 2>> one_player{{card("6c"), card("6d")}};
  const auto unsupported = gtosd::evaluate_showdown(one_player, royal_board);
  require(!unsupported && unsupported.error() == gtosd::EquityError::UnsupportedPlayerCount,
          "unsupported player count rejected");

  const ThrowingEvaluator throwing_evaluator;
  const auto internal = gtosd::evaluate_showdown(heads_up, straight_board, throwing_evaluator);
  require(!internal && internal.error() == gtosd::EquityError::InternalEvaluatorFailure,
          "evaluator exception translated to explicit error");
}

void test_batch_adapter() {
  const std::array<std::array<gtosd::CardId, 7>, 3> hands{
      cards_seven({"As", "Ad", "Ac", "Ks", "Kd", "6c", "7d"}),
      cards_seven({"As", "Ks", "Qs", "9s", "8s", "6s", "7d"}),
      cards_seven({"6c", "7d", "8h", "9s", "Tc", "Jc", "Qd"})};

  const auto batch = gtosd::evaluate_seven_batch(hands);
  require(batch.has_value() && batch.value().size() == hands.size(), "batch evaluates every hand");
  for (std::size_t index = 0; index < hands.size(); ++index) {
    const auto scalar = gtosd::evaluate_seven(hands[index]);
    require(scalar.has_value() && scalar.value() == batch.value()[index], "batch equals scalar");
  }

  auto invalid_hands = hands;
  invalid_hands[1][6] = invalid_hands[1][0];
  const auto invalid_batch = gtosd::evaluate_seven_batch(invalid_hands);
  require(!invalid_batch && invalid_batch.error() == gtosd::EquityError::DuplicateCard,
          "batch fails explicitly rather than returning partial values");
}

} // namespace

int main() {
  try {
    test_exhaustive_five_card_oracle_and_suit_invariance();
    test_ranking_and_best_five_regressions();
    test_showdown_validation_and_winner_masks();
    test_batch_adapter();
    std::cout << "F2_EQUITY_TESTS=PASS\n"
              << "assertions=" << assertions << '\n'
              << "five_card_combinations=376992\n"
              << "suit_permutations_per_hand=24\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "F2_EQUITY_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  }
}
