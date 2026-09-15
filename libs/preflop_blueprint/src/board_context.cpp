#include "gtosd/preflop_blueprint/board_context.hpp"

#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"

#include <algorithm>
#include <bit>
#include <numeric>

namespace gtosd::preflop_blueprint {
namespace {

using ContextResult = Result<BoardContext, KernelError>;

std::uint16_t canonical_combo(const std::array<std::uint8_t, 2> &cards,
                              const card_abstraction::SuitPermutation &permutation) {
  const auto first = card_abstraction::permute_card(CardId::from_index(cards[0]).value(), permutation);
  const auto second =
      card_abstraction::permute_card(CardId::from_index(cards[1]).value(), permutation);
  return card_abstraction::combo_index(first, second);
}

} // namespace

Result<BoardContext, KernelError> BoardContext::build(const card_abstraction::BoardHistory &history,
                                                      const card_abstraction::RankTable &ranks,
                                                      const AbstractionTables *tables) {
  BoardContext context;
  context.history_ = history;
  context.board_ = {history.flop[0], history.flop[1], history.flop[2], history.turn,
                    history.river};
  std::array<std::uint8_t, 5> board_indices{};
  for (std::size_t index = 0; index < 5U; ++index) {
    board_indices[index] = context.board_[index].value();
    context.board_mask_ |= context.board_[index].mask();
  }
  if (std::popcount(context.board_mask_) != 5 || (context.board_mask_ >> 36U) != 0U ||
      !(history.flop[0] < history.flop[1] && history.flop[1] < history.flop[2])) {
    return ContextResult::failure(KernelError::InvalidBoard);
  }

  const auto &combos = card_abstraction::combo_table();
  context.hand_index_.fill(no_hand);
  std::uint16_t count = 0U;
  for (std::uint16_t combo = 0; combo < combos.masks.size(); ++combo) {
    if ((combos.masks[combo] & context.board_mask_) != 0U) {
      continue;
    }
    if (count >= live_hand_count) {
      return ContextResult::failure(KernelError::InvalidBoard);
    }
    context.combo_ids_[count] = combo;
    context.cards_[count] = combos.cards[combo];
    context.hand_class_[count] = combos.hand_class[combo];
    context.hand_index_[combo] = count;
    ++count;
  }
  if (count != live_hand_count) {
    return ContextResult::failure(KernelError::InvalidBoard);
  }

  for (std::uint16_t hand = 0; hand < live_hand_count; ++hand) {
    context.ranks_[hand] = ranks.rank_of(context.cards_[hand], board_indices);
  }
  std::iota(context.order_.begin(), context.order_.end(), std::uint16_t{0});
  std::stable_sort(context.order_.begin(), context.order_.end(),
                   [&](const std::uint16_t left, const std::uint16_t right) {
                     return context.ranks_[left] < context.ranks_[right];
                   });
  context.rank_groups_ = 0U;
  for (std::size_t position = 0; position < live_hand_count; ++position) {
    if (position == 0U || context.ranks_[context.order_[position]] !=
                              context.ranks_[context.order_[position - 1U]]) {
      ++context.rank_groups_;
    }
  }

  std::array<std::uint8_t, 36> incidence{};
  for (std::uint16_t hand = 0; hand < live_hand_count; ++hand) {
    for (const auto card : context.cards_[hand]) {
      if (incidence[card] >= hands_per_card) {
        return ContextResult::failure(KernelError::InvalidBoard);
      }
      context.hands_with_card_[card][incidence[card]++] = hand;
    }
  }
  for (std::uint8_t card = 0; card < 36U; ++card) {
    context.card_is_live_[card] = ((context.board_mask_ >> card) & 1U) == 0U;
    if (context.card_is_live_[card] != (incidence[card] == hands_per_card)) {
      return ContextResult::failure(KernelError::InvalidBoard);
    }
  }

  for (auto &street : context.buckets_) {
    street.fill(card_abstraction::no_bucket);
  }
  if (tables != nullptr && tables->catalog != nullptr) {
    struct StreetTable {
      const card_abstraction::BucketTable *table;
      card_abstraction::BucketStreet street;
      Result<card_abstraction::CanonicalLookup, CardError> lookup;
    };
    const std::array<StreetTable, 3> streets{
        StreetTable{tables->flop, card_abstraction::BucketStreet::Flop,
                    tables->catalog->lookup_flop(history.flop)},
        StreetTable{tables->turn, card_abstraction::BucketStreet::Turn,
                    tables->catalog->lookup_flop_turn(history.flop, history.turn)},
        StreetTable{tables->river, card_abstraction::BucketStreet::River,
                    tables->catalog->lookup_river_board(context.board_)}};
    for (std::size_t index = 0; index < streets.size(); ++index) {
      const auto &entry = streets[index];
      if (entry.table == nullptr) {
        continue;
      }
      if (entry.table->street() != entry.street || !entry.lookup) {
        return ContextResult::failure(KernelError::MissingTable);
      }
      const auto row = entry.lookup.value().index;
      const auto &permutation = entry.lookup.value().permutation;
      for (std::uint16_t hand = 0; hand < live_hand_count; ++hand) {
        const auto bucket =
            entry.table->bucket(row, canonical_combo(context.cards_[hand], permutation));
        if (bucket == card_abstraction::no_bucket || bucket >= entry.table->capacity()) {
          return ContextResult::failure(KernelError::InvalidInput);
        }
        context.buckets_[index][hand] = bucket;
      }
      context.has_buckets_[index] = true;
    }
  } else if (tables != nullptr &&
             (tables->flop != nullptr || tables->turn != nullptr || tables->river != nullptr)) {
    return ContextResult::failure(KernelError::MissingTable);
  }
  return ContextResult::success(std::move(context));
}

const char *kernel_error_name(const KernelError error) noexcept {
  switch (error) {
  case KernelError::InvalidBoard:
    return "invalid_board";
  case KernelError::MissingTable:
    return "missing_table";
  case KernelError::InvalidInput:
    return "invalid_input";
  }
  return "unknown";
}

} // namespace gtosd::preflop_blueprint
