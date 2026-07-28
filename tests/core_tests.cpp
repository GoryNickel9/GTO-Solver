#include "gtosd/core/cards.hpp"
#include "gtosd/core/game.hpp"
#include "gtosd/core/ranges.hpp"
#include "gtosd/equity/evaluator.hpp"
#include "gtosd/equity/showdown.hpp"

#include <bit>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

gtosd::CardId card(const std::string_view text) {
  const auto parsed = gtosd::parse_card(text);
  require(parsed.has_value(), "card parse");
  return parsed.value();
}

gtosd::PotPercentage pct(const std::int64_t basis_points) {
  return gtosd::PotPercentage::from_basis_points(basis_points).value();
}

gtosd::RangeWeight weight(const std::int64_t basis_points) {
  return gtosd::RangeWeight::from_basis_points(basis_points).value();
}

std::array<gtosd::CardId, 5> five(const std::array<std::string_view, 5> &texts) {
  return {card(texts[0]), card(texts[1]), card(texts[2]), card(texts[3]), card(texts[4])};
}

void test_cards() {
  const auto deck = gtosd::short_deck();
  require(deck.size() == 36U, "36 cards");
  require(gtosd::format_card(card("As")) == "As", "card round trip");
  require(!gtosd::parse_card("2s"), "rank 2 rejected");
  for (const auto value : deck) {
    require(gtosd::parse_card(gtosd::format_card(value)).value() == value, "deck round trip");
  }
  std::vector<gtosd::CardId> seven{deck.begin(), deck.begin() + 7};
  require(std::popcount(gtosd::card_mask(seven).value()) == 7, "deck mask");
  seven.push_back(deck.front());
  require(!gtosd::card_mask(seven), "duplicate rejected");
}

void test_money_and_rake() {
  const auto one_hundred = gtosd::Money::from_antes(100).value();
  require(gtosd::percent_of(one_hundred, 5'000, 100'000).value() ==
              gtosd::Money::from_antes(50).value(),
          "50 percent bet");
  const auto three = gtosd::Money::from_antes(3).value();
  require(gtosd::percent_of(three, 3'333, 100'000).value().units() == 9'999,
          "fixed point rounding");
  require(!gtosd::percent_of(one_hundred, 100'001, 100'000), "overbet rejected");

  const gtosd::RakeConfig rake{true, weight(500), gtosd::Money::from_antes(3).value(), true, {}};
  require(gtosd::calculate_rake(rake, one_hundred, true).value() ==
              gtosd::Money::from_antes(3).value(),
          "rake cap");
  require(gtosd::calculate_rake(rake, gtosd::Money::from_antes(20).value(), true).value() ==
              gtosd::Money::from_antes(1).value(),
          "rake percentage");
  require(gtosd::calculate_rake(rake, one_hundred, false).value().units() == 0, "no flop no drop");
}

void test_preflop_and_actions() {
  const auto ante = gtosd::Money::from_antes(1).value();
  auto state = gtosd::make_hu_preflop_state(gtosd::Money::from_antes(40).value(), ante).value();
  require(state.pot == gtosd::Money::from_antes(3).value(), "root pot 3");
  require(state.player_to_act == 0U, "CO first");
  require(state.committed_total[0] == ante, "CO ante");
  require(state.committed_total[1] == gtosd::Money::from_antes(2).value(), "BTN posts blind");
  require(gtosd::amount_to_call(state, 0) == ante, "CO calls one");

  const gtosd::ActionConfig config{{pct(5'000)}, 2, gtosd::AllInMode::Disabled, pct(0), ante};
  const auto actions = gtosd::legal_actions(state, config).value();
  require(actions[0].type == gtosd::ActionType::Fold, "fold facing bet");
  require(actions[1].type == gtosd::ActionType::Call, "call facing bet");
  require(actions[2].type == gtosd::ActionType::Raise, "raise facing button blind");
  require(actions[2].amount == gtosd::Money::from_units(30'000).value(),
          "raise uses pot after call");

  const auto called = gtosd::apply_action(state, actions[1], config).value();
  require(called.pot == gtosd::Money::from_antes(4).value(), "call adds to pot");
  require(called.remaining_stacks[0] == gtosd::Money::from_antes(38).value(), "stack accounting");

  const gtosd::ActionConfig no_raises{{pct(5'000)}, 0, gtosd::AllInMode::Disabled, pct(0), ante};
  const auto root_no_raises =
      gtosd::legal_actions(
          gtosd::make_hu_preflop_state(gtosd::Money::from_antes(40).value(), ante).value(),
          no_raises)
          .value();
  require(root_no_raises.size() == 2U, "raise depth zero removes raise facing blind");
}

void test_ranges() {
  const auto combos = gtosd::all_combos();
  require(combos.size() == 630U, "630 combos");
  std::array<int, 81> counts{};
  for (const auto combo : combos) {
    ++counts[gtosd::hand_class(combo)];
  }
  for (std::uint8_t index = 0; index < 81U; ++index) {
    require(counts[index] == gtosd::class_mass(index), "class mass");
  }
  require(gtosd::class_name(0) == "AA", "AA label");
  require(gtosd::class_name(9) == "AKs", "AKs label");
  require(gtosd::class_name(45) == "AKo", "AKo label");
}

void test_evaluator() {
  const auto wheel = gtosd::evaluate_five(five({"As", "6s", "7s", "8s", "9s"})).value();
  require(wheel.category == gtosd::HandCategory::StraightFlush && wheel.kickers[0] == 3U,
          "A6789 straight flush");
  const auto flush = gtosd::evaluate_five(five({"As", "Qs", "Js", "8s", "6s"})).value();
  const auto full = gtosd::evaluate_five(five({"Ah", "Ad", "Ac", "Kh", "Kd"})).value();
  require(flush > full, "flush beats full");
  const auto quads = gtosd::evaluate_five(five({"Ah", "Ad", "Ac", "As", "Kd"})).value();
  require(quads > flush, "quads beats flush");
  const auto straight = gtosd::evaluate_five(five({"6s", "7d", "8c", "9h", "Ts"})).value();
  const auto trips = gtosd::evaluate_five(five({"As", "Ad", "Ac", "Kh", "Qd"})).value();
  require(straight > trips, "straight beats trips");

  const std::vector<std::array<gtosd::CardId, 2>> hands{{card("As"), card("Ad")},
                                                        {card("Ks"), card("Kd")}};
  const std::vector<gtosd::CardId> board{card("6c"), card("7d"), card("8h"), card("9s"),
                                         card("Tc")};
  const auto showdown = gtosd::evaluate_showdown(hands, board).value();
  require(showdown.winner_mask == 0b11U, "board plays split");
  require(!gtosd::evaluate_showdown(hands, {card("6c")}), "wrong board rejected");
}

void test_split() {
  const auto pot = gtosd::Money::from_units(10).value();
  const auto payout = gtosd::split_pot(pot, {}, gtosd::WinnerMask{0b111}, 3).value();
  require(payout[0].units() == 4 && payout[1].units() == 3 && payout[2].units() == 3,
          "deterministic remainder");
}

} // namespace

int main() {
  try {
    test_cards();
    test_money_and_rake();
    test_preflop_and_actions();
    test_ranges();
    test_evaluator();
    test_split();
    std::cout << "All GTOSD core tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
}
