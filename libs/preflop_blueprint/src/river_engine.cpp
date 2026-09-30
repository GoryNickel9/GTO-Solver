#include "gtosd/preflop_blueprint/river_engine.hpp"

#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"

#include <algorithm>
#include <bit>
#include <emmintrin.h>

namespace gtosd::preflop_blueprint {
namespace {

namespace ca = card_abstraction;

constexpr double ante_scale = 1.0 / static_cast<double>(Money::units_per_ante);
constexpr std::size_t card_slots = ca::deck_cards;
// Sort key of the rank order: rank in the high half, hand index in the low.
constexpr unsigned rank_key_shift = 16U;
constexpr std::uint32_t rank_key_hand_mask = (std::uint32_t{1} << rank_key_shift) - 1U;
// Rows requested ahead of use while a node copies its policy rows: the rows of
// the live hands are scattered over the whole table, so each one is a cache
// and TLB miss otherwise. A prefetch never faults and changes no value.
constexpr std::size_t row_prefetch_distance = 16U;
// Hands scanned between two early-exit tests of the pruning check.
constexpr std::size_t alive_block = 16U;
// Lane bits: bit h stands for hero h.
constexpr unsigned both_lanes = 3U;

std::uint16_t canonical_combo(const std::array<std::uint8_t, 2> &cards,
                              const ca::SuitPermutation &permutation) {
  const auto first = ca::permute_card(CardId::from_index(cards[0]).value(), permutation);
  const auto second = ca::permute_card(CardId::from_index(cards[1]).value(), permutation);
  return ca::combo_index(first, second);
}

// The flop or turn row that BoardContext::build requires for a live hand.
bool street_row_exists(const ca::BucketTable &table, const ClassBucketRows *class_rows,
                       const std::uint8_t hand_class, const std::uint16_t bucket) {
  if (bucket == ca::no_bucket || bucket >= table.capacity()) {
    return false;
  }
  return class_rows == nullptr ||
         class_rows->row(table.street(), hand_class, bucket) != ca::no_bucket;
}

void prefetch_read(const void *const address) noexcept {
  _mm_prefetch(static_cast<const char *>(address), _MM_HINT_T0);
}

// All ones in lane h when bit h of `lanes` is set, zero in the other lanes: an
// AND with it keeps the selected lanes and writes +0.0 into the others.
__m128d lane_mask(const unsigned lanes) noexcept {
  const int low = (lanes & 1U) != 0U ? -1 : 0;
  const int high = (lanes & 2U) != 0U ? -1 : 0;
  return _mm_castsi128_pd(_mm_set_epi32(high, high, low, low));
}

__m128d load_pair(const double *values, const std::size_t hand) noexcept {
  return _mm_loadu_pd(values + hero_lanes * hand);
}

void store_pair(double *values, const std::size_t hand, const __m128d pair) noexcept {
  _mm_storeu_pd(values + hero_lanes * hand, pair);
}

// Bit h is set when lane h has an entry that compares unequal to 0.0: the
// all_zero test of ValueTraversal, per lane (-0.0 counts as zero).
unsigned alive_lanes(const double *reach) noexcept {
  const __m128d zero = _mm_setzero_pd();
  __m128d nonzero = zero;
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    nonzero = _mm_or_pd(nonzero, _mm_cmpneq_pd(load_pair(reach, hand), zero));
    if ((hand + 1U) % alive_block == 0U &&
        _mm_movemask_pd(nonzero) == static_cast<int>(both_lanes)) {
      return both_lanes;
    }
  }
  return static_cast<unsigned>(_mm_movemask_pd(nonzero));
}

} // namespace

Result<bool, KernelError> RiverPrefix::assign(const std::array<CardId, 3> &flop,
                                              const CardId turn,
                                              const AbstractionTables &tables) {
  using Outcome = Result<bool, KernelError>;
  assigned_ = false;
  flop_ = flop;
  turn_ = turn;
  tables_ = tables;
  covered_.fill(std::uint8_t{0});
  cursors_.fill(RiverRowCursor{});
  if (tables.catalog == nullptr || tables.flop == nullptr || tables.turn == nullptr ||
      tables.river == nullptr) {
    return Outcome::failure(KernelError::MissingTable);
  }
  if ((tables.history_rows != nullptr ? 1 : 0) + (tables.class_rows != nullptr ? 1 : 0) +
          (tables.board_class_rows != nullptr ? 1 : 0) >
      1) {
    return Outcome::failure(KernelError::InvalidInput);
  }
  // The board-independent checks of BoardContext::build.
  const std::array<const ca::BucketTable *, 3> streets{tables.flop, tables.turn, tables.river};
  for (std::size_t index = 0; index < streets.size(); ++index) {
    const auto &table = *streets[index];
    if (table.street() != static_cast<ca::BucketStreet>(index) ||
        (tables.class_rows != nullptr && !tables.class_rows->matches(table)) ||
        (tables.history_rows != nullptr && !tables.history_rows->matches(table)) ||
        (tables.board_class_rows != nullptr && !tables.board_class_rows->matches(table))) {
      return Outcome::failure(KernelError::MissingTable);
    }
  }
  const auto flop_lookup = tables.catalog->lookup_flop(flop);
  const auto turn_lookup = tables.catalog->lookup_flop_turn(flop, turn);
  if (!flop_lookup || !turn_lookup) {
    return Outcome::failure(KernelError::MissingTable);
  }
  // Board class rows: every river of the turn uses the class of the turn,
  // the texture's river class of its canonical flop+turn index
  // (BoardContext::build); with river key "river-board" RiverBoard::assign
  // reads the class of each five-card board instead.
  turn_class_ = tables.board_class_rows != nullptr
                    ? tables.board_class_rows->river_class(turn_lookup.value().index)
                    : turn_lookup.value().index;
  const auto prefix_mask = flop[0].mask() | flop[1].mask() | flop[2].mask() | turn.mask();
  const auto &combos = ca::combo_table();
  for (std::uint16_t combo = 0; combo < ca::combo_count; ++combo) {
    if ((combos.masks[combo] & prefix_mask) != 0U) {
      continue;
    }
    const auto hand_class = combos.hand_class[combo];
    const auto flop_bucket =
        tables.flop->bucket(flop_lookup.value().index,
                            canonical_combo(combos.cards[combo], flop_lookup.value().permutation));
    const auto turn_bucket =
        tables.turn->bucket(turn_lookup.value().index,
                            canonical_combo(combos.cards[combo], turn_lookup.value().permutation));
    if (!street_row_exists(*tables.flop, tables.class_rows, hand_class, flop_bucket) ||
        !street_row_exists(*tables.turn, tables.class_rows, hand_class, turn_bucket)) {
      continue;
    }
    if (tables.history_rows != nullptr) {
      const auto &history = *tables.history_rows;
      if (history.row(Street::Flop, hand_class, flop_bucket, turn_bucket, ca::no_bucket) ==
              no_history_row ||
          history.row(Street::Turn, hand_class, flop_bucket, turn_bucket, ca::no_bucket) ==
              no_history_row) {
        continue;
      }
      cursors_[combo] = history.river_cursor(hand_class, flop_bucket, turn_bucket);
    }
    covered_[combo] = 1U;
  }
  assigned_ = true;
  return Outcome::success(true);
}

Result<bool, KernelError> RiverBoard::assign(const ca::BoardHistory &history,
                                             const ca::RankTable &ranks,
                                             const RiverPrefix *prefix) {
  using Outcome = Result<bool, KernelError>;
  history_ = history;
  rows_.fill(no_history_row);
  maximum_row_ = no_history_row;
  const std::array<CardId, 5> board{history.flop[0], history.flop[1], history.flop[2],
                                    history.turn, history.river};
  std::array<std::uint8_t, 5> board_indices{};
  std::uint64_t board_mask = 0U;
  for (std::size_t index = 0; index < board.size(); ++index) {
    board_indices[index] = board[index].value();
    board_mask |= board[index].mask();
  }
  if (std::popcount(board_mask) != 5 || (board_mask >> card_slots) != 0U ||
      !(history.flop[0] < history.flop[1] && history.flop[1] < history.flop[2])) {
    return Outcome::failure(KernelError::InvalidBoard);
  }
  const auto &combos = ca::combo_table();
  std::size_t count = 0U;
  for (std::uint16_t combo = 0; combo < combos.masks.size(); ++combo) {
    if ((combos.masks[combo] & board_mask) != 0U) {
      continue;
    }
    if (count >= live_hand_count) {
      return Outcome::failure(KernelError::InvalidBoard);
    }
    combo_ids_[count] = combo;
    cards_[count] = combos.cards[combo];
    ++count;
  }
  if (count != live_hand_count) {
    return Outcome::failure(KernelError::InvalidBoard);
  }

  // Sorting (rank, hand) keys gives the unique order by rank with ties in
  // increasing hand index, which is what the stable sort of BoardContext
  // produces from the identity permutation.
  std::array<std::uint32_t, live_hand_count> keys{};
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    ranks_[hand] = ranks.rank_of(cards_[hand], board_indices);
    keys[hand] = (static_cast<std::uint32_t>(ranks_[hand]) << rank_key_shift) |
                 static_cast<std::uint32_t>(hand);
  }
  std::sort(keys.begin(), keys.end());
  rank_groups_ = 0U;
  for (std::size_t position = 0; position < live_hand_count; ++position) {
    order_[position] = static_cast<std::uint16_t>(keys[position] & rank_key_hand_mask);
    if (position == 0U ||
        (keys[position] >> rank_key_shift) != (keys[position - 1U] >> rank_key_shift)) {
      group_starts_[rank_groups_] = static_cast<std::uint16_t>(position);
      ++rank_groups_;
    }
  }
  group_starts_[rank_groups_] = static_cast<std::uint16_t>(live_hand_count);
  if (prefix == nullptr) {
    return Outcome::success(true);
  }

  if (!prefix->assigned() || prefix->flop() != history.flop || prefix->turn() != history.turn) {
    return Outcome::failure(KernelError::InvalidInput);
  }
  const auto &tables = prefix->tables();
  const auto lookup = tables.catalog->lookup_river_board(board);
  if (!lookup) {
    return Outcome::failure(KernelError::MissingTable);
  }
  const auto &table = *tables.river;
  // Board class rows: the class of the turn, or with river key "river-board"
  // the class of this five-card board (BoardContext::build).
  const auto river_class =
      tables.board_class_rows != nullptr &&
              tables.board_class_rows->river_key() == RiverKey::RiverBoard
          ? tables.board_class_rows->river_class(0U, lookup.value().index)
          : prefix->turn_class();
  std::uint32_t maximum = 0U;
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    const auto combo = combo_ids_[hand];
    const auto bucket = table.bucket(lookup.value().index,
                                     canonical_combo(cards_[hand], lookup.value().permutation));
    if (bucket == ca::no_bucket || bucket >= table.capacity() || !prefix->covers(combo)) {
      return Outcome::failure(KernelError::InvalidInput);
    }
    std::uint32_t row = bucket;
    if (tables.class_rows != nullptr) {
      const auto mapped =
          tables.class_rows->row(ca::BucketStreet::River, combos.hand_class[combo], bucket);
      if (mapped == ca::no_bucket) {
        return Outcome::failure(KernelError::InvalidInput);
      }
      row = mapped;
    } else if (tables.history_rows != nullptr) {
      row = tables.history_rows->river_row(prefix->cursor(combo), bucket);
      if (row == no_history_row) {
        return Outcome::failure(KernelError::InvalidInput);
      }
    } else if (tables.board_class_rows != nullptr) {
      row = tables.board_class_rows->row(ca::BucketStreet::River, river_class, bucket);
    }
    rows_[hand] = row;
    maximum = std::max(maximum, row);
  }
  maximum_row_ = maximum;
  return Outcome::success(true);
}

void fold_mass_pair(const RiverBoard &board, const ConstPairSpan reach,
                    const PairSpan disjoint_mass) noexcept {
  // fold_mass per lane: D[h] = S - C[h1] - C[h2] + r[h], sums in hand order.
  const auto cards = board.cards();
  const double *in = reach.data();
  // Value-initialized SIMD values hold +0.0 in both lanes.
  __m128d total = _mm_setzero_pd();
  __m128d per_card[card_slots] = {};
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    const __m128d mass = load_pair(in, hand);
    total = _mm_add_pd(total, mass);
    per_card[cards[hand][0]] = _mm_add_pd(per_card[cards[hand][0]], mass);
    per_card[cards[hand][1]] = _mm_add_pd(per_card[cards[hand][1]], mass);
  }
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    const __m128d outside =
        _mm_sub_pd(_mm_sub_pd(total, per_card[cards[hand][0]]), per_card[cards[hand][1]]);
    store_pair(disjoint_mass.data(), hand, _mm_add_pd(outside, load_pair(in, hand)));
  }
}

void showdown_masses_pair(const RiverBoard &board, const ConstPairSpan reach,
                          const PairSpan worse, const PairSpan tied,
                          const PairSpan better) noexcept {
  // showdown_masses per lane, with the same groups, visiting order and
  // association: the rank groups are precomputed by the board instead of
  // being found by comparing ranks, which does not touch the arithmetic.
  const auto cards = board.cards();
  const auto order = board.order_by_rank();
  const auto starts = board.group_starts();
  const std::size_t groups = board.rank_groups();
  const double *in = reach.data();
  const __m128d zero = _mm_setzero_pd();

  // Ascending pass: mass of the strictly weaker hands, corrected for blockers;
  // a group joins the running sums only after every member has read them.
  __m128d below_total = zero;
  __m128d below_card[card_slots] = {};
  __m128d group_card[card_slots] = {};
  for (std::size_t group = 0; group < groups; ++group) {
    const std::size_t start = starts[group];
    const std::size_t end = starts[group + 1U];
    __m128d group_total = zero;
    for (std::size_t position = start; position < end; ++position) {
      const std::size_t hand = order[position];
      const __m128d mass = load_pair(in, hand);
      group_total = _mm_add_pd(group_total, mass);
      group_card[cards[hand][0]] = _mm_add_pd(group_card[cards[hand][0]], mass);
      group_card[cards[hand][1]] = _mm_add_pd(group_card[cards[hand][1]], mass);
    }
    for (std::size_t position = start; position < end; ++position) {
      const std::size_t hand = order[position];
      const auto first = cards[hand][0];
      const auto second = cards[hand][1];
      store_pair(worse.data(), hand,
                 _mm_sub_pd(_mm_sub_pd(below_total, below_card[first]), below_card[second]));
      store_pair(tied.data(), hand,
                 _mm_add_pd(_mm_sub_pd(_mm_sub_pd(group_total, group_card[first]),
                                       group_card[second]),
                            load_pair(in, hand)));
    }
    for (std::size_t position = start; position < end; ++position) {
      const std::size_t hand = order[position];
      const __m128d mass = load_pair(in, hand);
      below_total = _mm_add_pd(below_total, mass);
      below_card[cards[hand][0]] = _mm_add_pd(below_card[cards[hand][0]], mass);
      below_card[cards[hand][1]] = _mm_add_pd(below_card[cards[hand][1]], mass);
      group_card[cards[hand][0]] = zero;
      group_card[cards[hand][1]] = zero;
    }
  }

  // Descending pass: mass of the strictly stronger hands.
  __m128d above_total = zero;
  __m128d above_card[card_slots] = {};
  for (std::size_t group = groups; group-- > 0U;) {
    const std::size_t start = starts[group];
    const std::size_t end = starts[group + 1U];
    for (std::size_t position = start; position < end; ++position) {
      const std::size_t hand = order[position];
      store_pair(better.data(), hand,
                 _mm_sub_pd(_mm_sub_pd(above_total, above_card[cards[hand][0]]),
                            above_card[cards[hand][1]]));
    }
    for (std::size_t position = start; position < end; ++position) {
      const std::size_t hand = order[position];
      const __m128d mass = load_pair(in, hand);
      above_total = _mm_add_pd(above_total, mass);
      above_card[cards[hand][0]] = _mm_add_pd(above_card[cards[hand][0]], mass);
      above_card[cards[hand][1]] = _mm_add_pd(above_card[cards[hand][1]], mass);
    }
  }
}

JointRiverTraversal::JointRiverTraversal(const CompiledGame &game, const BucketPolicy &policy)
    : game_(&game), policy_(&policy),
      levels_(static_cast<std::size_t>(game.stats().maximum_depth) + 2U),
      worse_(pair_values, 0.0), tied_(pair_values, 0.0), better_(pair_values, 0.0) {}

Result<bool, KernelError> JointRiverTraversal::evaluate(const std::uint32_t node,
                                                        const RiverBoard &board,
                                                        const ConstPairSpan reach,
                                                        const PairSpan response,
                                                        const PairSpan average) {
  using Outcome = Result<bool, KernelError>;
  if (node >= game_->nodes().size() ||
      static_cast<std::size_t>(game_->config().player_count) != hero_lanes ||
      game_->nodes()[node].street != Street::River) {
    return Outcome::failure(KernelError::InvalidInput);
  }
  board_ = &board;
  error_ = KernelError::MissingTable;
  if (!traverse(node, 0U, reach.data(), response.data(), average.data())) {
    return Outcome::failure(error_);
  }
  return Outcome::success(true);
}

bool JointRiverTraversal::traverse(const std::uint32_t node_id, const std::size_t level,
                                   const double *reach, double *response, double *average) {
  const auto &node = game_->nodes()[node_id];
  const auto alive = alive_lanes(reach);
  if (alive == 0U) {
    std::fill_n(response, pair_values, 0.0);
    std::fill_n(average, pair_values, 0.0);
    return true;
  }
  switch (node.kind) {
  case NodeKind::TerminalFold:
    fold(node, alive, reach, response, average);
    return true;
  case NodeKind::TerminalShowdown:
    showdown(node, alive, reach, response, average);
    return true;
  case NodeKind::Chance:
    // A river subtree ends in terminals; a chance node would deal a street
    // that the river rows of the board do not describe.
    error_ = KernelError::InvalidInput;
    return false;
  case NodeKind::Decision:
    break;
  }
  return decision(node, level, alive, reach, response, average);
}

bool JointRiverTraversal::decision(const CompiledNode &node, const std::size_t level_index,
                                   const unsigned alive, const double *reach, double *response,
                                   double *average) {
  const std::size_t actor = node.actor;
  if (level_index >= levels_.size() || actor >= hero_lanes) {
    error_ = KernelError::InvalidInput;
    return false;
  }
  auto &level = levels_[level_index];
  if (level.policy.empty()) {
    level.child_reach.assign(pair_values, 0.0);
    level.child_response.assign(pair_values, 0.0);
    level.child_average.assign(pair_values, 0.0);
    level.policy.assign(maximum_actions * live_hand_count, 0.0);
  }
  const std::size_t other = hero_lanes - 1U - actor;
  // The reference reads the rows of every hand at a decision of its hero
  // (average mode) and of the hands with nonzero reach at a decision of the
  // opponent, so a live acting lane needs all rows.
  const bool actor_alive = ((alive >> actor) & 1U) != 0U;
  if (!gather(node, actor_alive, other, reach, level.policy.data())) {
    error_ = KernelError::MissingTable;
    return false;
  }

  // The acting lane (the hero of the node) passes its reach to every child and
  // takes the maximum or the policy average of the child values; the other
  // lane splits its reach by the policy and sums the child values.
  const __m128d zero = _mm_setzero_pd();
  const __m128d acting = lane_mask(1U << actor);
  const __m128d other_one = _mm_andnot_pd(acting, _mm_set1_pd(1.0));
  const __m128d acting_negative_zero = _mm_and_pd(acting, _mm_set1_pd(-0.0));
  const auto edges = game_->edges_of(node.id);
  double *child_reach = level.child_reach.data();
  double *child_response = level.child_response.data();
  double *child_average = level.child_average.data();
  for (std::size_t action = 0; action < node.action_count; ++action) {
    const double *probability = level.policy.data() + action * live_hand_count;
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      const __m128d mass = load_pair(reach, hand);
      // reach == 0.0 ? 0.0 : reach * p, without a branch.
      const __m128d split = _mm_and_pd(_mm_cmpneq_pd(mass, zero),
                                       _mm_mul_pd(mass, _mm_set1_pd(probability[hand])));
      store_pair(child_reach, hand,
                 _mm_or_pd(_mm_and_pd(acting, mass), _mm_andnot_pd(acting, split)));
    }
    if (!traverse(edges[action].child, level_index + 1U, child_reach, child_response,
                  child_average)) {
      return false;
    }
    // Average mode: the acting lane adds p * child to a sum started at 0.0;
    // the other lane adds 1.0 * child, which is the child value exactly.
    if (action == 0U) {
      for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
        const __m128d scale =
            _mm_or_pd(_mm_and_pd(acting, _mm_set1_pd(probability[hand])), other_one);
        // Response mode: the acting lane starts from the first child value
        // (x + -0.0 is x for every x), the other lane from 0.0 + child.
        store_pair(response, hand,
                   _mm_add_pd(load_pair(child_response, hand), acting_negative_zero));
        store_pair(average, hand,
                   _mm_add_pd(zero, _mm_mul_pd(scale, load_pair(child_average, hand))));
      }
      continue;
    }
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      const __m128d scale =
          _mm_or_pd(_mm_and_pd(acting, _mm_set1_pd(probability[hand])), other_one);
      const __m128d value = load_pair(child_response, hand);
      const __m128d best = load_pair(response, hand);
      // Acting lane: std::max(best, value) = (best < value) ? value : best,
      // which MAXPD computes with the operands in this order; other lane:
      // best + value.
      store_pair(response, hand,
                 _mm_or_pd(_mm_and_pd(acting, _mm_max_pd(value, best)),
                           _mm_andnot_pd(acting, _mm_add_pd(best, value))));
      store_pair(average, hand,
                 _mm_add_pd(load_pair(average, hand),
                            _mm_mul_pd(scale, load_pair(child_average, hand))));
    }
  }
  if (alive != both_lanes) {
    // The reference fills a pruned lane with +0.0 whatever its children gave.
    const __m128d keep = lane_mask(alive);
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      store_pair(response, hand, _mm_and_pd(keep, load_pair(response, hand)));
      store_pair(average, hand, _mm_and_pd(keep, load_pair(average, hand)));
    }
  }
  return true;
}

bool JointRiverTraversal::gather(const CompiledNode &node, const bool all_hands,
                                 const std::size_t other, const double *reach, double *policy) {
  std::size_t count = 0U;
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    if (all_hands || reach[hero_lanes * hand + other] != 0.0) {
      gathered_[count] = static_cast<std::uint16_t>(hand);
      ++count;
    }
  }
  const auto &layout = policy_->layout();
  const std::size_t actions = node.action_count;
  const auto capacity = StateLayout::rows_for(node.street, layout.flop_capacity,
                                              layout.turn_capacity, layout.river_capacity);
  const double *base = policy_->table().data() + layout.offsets[node.id];
  const auto rows = board_->rows();
  const auto row_at = [&](const std::size_t index) {
    return base + static_cast<std::size_t>(rows[gathered_[index]]) * actions;
  };
  // A row outside the layout makes BucketPolicy::probabilities() return null
  // and fails the reference. In the evaluator every row is inside, so the
  // per-row check is skipped and the rows are prefetched instead.
  const bool inside = board_->maximum_row() < capacity;
  if (inside) {
    for (std::size_t index = 0; index < std::min(count, row_prefetch_distance); ++index) {
      prefetch_read(row_at(index));
      prefetch_read(row_at(index) + actions - 1U);
    }
  }
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t hand = gathered_[index];
    if (!inside) {
      if (rows[hand] >= capacity) {
        return false;
      }
    } else if (index + row_prefetch_distance < count) {
      const double *ahead = row_at(index + row_prefetch_distance);
      prefetch_read(ahead);
      prefetch_read(ahead + actions - 1U);
    }
    const double *row = row_at(index);
    for (std::size_t action = 0; action < actions; ++action) {
      policy[action * live_hand_count + hand] = row[action];
    }
  }
  return true;
}

void JointRiverTraversal::fold(const CompiledNode &node, const unsigned alive,
                               const double *reach, double *response, double *average) {
  fold_mass_pair(*board_, ConstPairSpan(reach, pair_values), PairSpan(worse_.data(), pair_values));
  // ValueTraversal::terminal: payoff * disjoint mass, the same in both modes.
  const auto payoffs = game_->fold_payoffs(node.id);
  const __m128d payoff = _mm_set_pd(static_cast<double>(payoffs[1]) * ante_scale,
                                    static_cast<double>(payoffs[0]) * ante_scale);
  const __m128d keep = lane_mask(alive);
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    const __m128d value = _mm_and_pd(keep, _mm_mul_pd(payoff, load_pair(worse_.data(), hand)));
    store_pair(response, hand, value);
    store_pair(average, hand, value);
  }
}

void JointRiverTraversal::showdown(const CompiledNode &node, const unsigned alive,
                                   const double *reach, double *response, double *average) {
  showdown_masses_pair(*board_, ConstPairSpan(reach, pair_values),
                       PairSpan(worse_.data(), pair_values), PairSpan(tied_.data(), pair_values),
                       PairSpan(better_.data(), pair_values));
  // Payoffs of ValueTraversal::terminal per hero; HeadsUpShowdownKernel gives
  // zero masses to a hero without exactly one live opponent.
  std::array<double, hero_lanes> win{};
  std::array<double, hero_lanes> tie{};
  std::array<double, hero_lanes> lose{};
  unsigned heads_up = 0U;
  for (std::size_t hero = 0; hero < hero_lanes; ++hero) {
    const auto hero_bit = static_cast<std::uint8_t>(std::uint8_t{1} << hero);
    const auto opponents = static_cast<std::uint8_t>(node.active_mask & ~hero_bit);
    const auto opponent_seat = static_cast<std::size_t>(std::countr_zero(opponents));
    if (std::popcount(opponents) == 1 &&
        opponent_seat < static_cast<std::size_t>(game_->config().player_count)) {
      heads_up |= 1U << hero;
    }
    win[hero] =
        static_cast<double>(game_->showdown_payoffs(node.id, hero_bit)[hero]) * ante_scale;
    tie[hero] =
        static_cast<double>(game_->showdown_payoffs(node.id, node.active_mask)[hero]) * ante_scale;
    lose[hero] =
        static_cast<double>(game_->showdown_payoffs(node.id, opponents)[hero]) * ante_scale;
  }
  const __m128d masses = lane_mask(heads_up);
  const __m128d keep = lane_mask(alive);
  const __m128d win_pair = _mm_set_pd(win[1], win[0]);
  const __m128d tie_pair = _mm_set_pd(tie[1], tie[0]);
  const __m128d lose_pair = _mm_set_pd(lose[1], lose[0]);
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    const __m128d first = _mm_and_pd(masses, load_pair(worse_.data(), hand));
    const __m128d second = _mm_and_pd(masses, load_pair(tied_.data(), hand));
    const __m128d third = _mm_and_pd(masses, load_pair(better_.data(), hand));
    // win * first + tie * second + lose * third, left to right.
    const __m128d value =
        _mm_add_pd(_mm_add_pd(_mm_mul_pd(win_pair, first), _mm_mul_pd(tie_pair, second)),
                   _mm_mul_pd(lose_pair, third));
    store_pair(response, hand, _mm_and_pd(keep, value));
    store_pair(average, hand, _mm_and_pd(keep, value));
  }
}

} // namespace gtosd::preflop_blueprint
