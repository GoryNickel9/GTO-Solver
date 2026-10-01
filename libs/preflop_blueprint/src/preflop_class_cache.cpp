#include "gtosd/preflop_blueprint/preflop_class_cache.hpp"

#include "hashing.hpp"

#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <thread>
#include <utility>

namespace gtosd::preflop_blueprint {

namespace {

namespace ca = card_abstraction;

using Built = Result<PreflopClassCache, PreflopClassCacheError>;

constexpr std::size_t classes = PreflopClassCache::class_count;
constexpr std::size_t entries = classes * classes * classes;
constexpr double units_per_ante = static_cast<double>(Money::units_per_ante);

// The lower and the higher seat other than `seat` among 3.
constexpr std::array<std::uint8_t, 2> other_seats(const std::uint8_t seat) noexcept {
  return {static_cast<std::uint8_t>(seat == 0U ? 1U : 0U),
          static_cast<std::uint8_t>(seat == 2U ? 1U : 2U)};
}

// Payoff of a seat for a winner set, in antes, as step 1 computes it.
double payoff_antes(const CompiledGame &game, const std::uint32_t node, const std::uint8_t winners,
                    const std::uint8_t seat) {
  return static_cast<double>(game.showdown_payoffs(node, winners)[seat]) / units_per_ante;
}

// Hand class of the combo of two distinct cards.
const std::array<std::array<std::uint8_t, ca::deck_cards>, ca::deck_cards> &card_pair_classes() {
  static const auto table = [] {
    std::array<std::array<std::uint8_t, ca::deck_cards>, ca::deck_cards> pairs{};
    const auto &combos = ca::combo_table();
    for (std::size_t combo = 0; combo < ca::combo_count; ++combo) {
      const auto &cards = combos.cards[combo];
      pairs[cards[0]][cards[1]] = combos.hand_class[combo];
      pairs[cards[1]][cards[0]] = combos.hand_class[combo];
    }
    return pairs;
  }();
  return table;
}

// The single payoff of `seat` at every terminal of the subtree of `root`, or
// false when it has more than one (a folded seat loses its commitment
// whatever happens below).
bool constant_payoff(const CompiledGame &game, const std::uint32_t root, const std::uint8_t seat,
                     std::int64_t &payoff) {
  const auto &nodes = game.nodes();
  bool found = false;
  for (std::uint32_t id = root; id < nodes[root].subtree_end; ++id) {
    const auto &node = nodes[id];
    if (node.kind == NodeKind::TerminalFold) {
      const auto value = game.fold_payoffs(id)[seat];
      if (found && value != payoff) {
        return false;
      }
      payoff = value;
      found = true;
    } else if (node.kind == NodeKind::TerminalShowdown) {
      for (std::uint8_t winners = 1U; winners <= node.active_mask; ++winners) {
        if ((winners & static_cast<std::uint8_t>(~node.active_mask)) != 0U) {
          continue;
        }
        const auto value = game.showdown_payoffs(id, winners)[seat];
        if (found && value != payoff) {
          return false;
        }
        payoff = value;
        found = true;
      }
    }
  }
  return found;
}

} // namespace

const char *preflop_class_cache_error_name(const PreflopClassCacheError error) noexcept {
  switch (error) {
  case PreflopClassCacheError::NotThreePlayers:
    return "not_three_players";
  case PreflopClassCacheError::IncompleteTable:
    return "incomplete_three_way_table";
  case PreflopClassCacheError::UnexpectedTerminal:
    return "unexpected_preflop_terminal";
  case PreflopClassCacheError::PayoffMismatch:
    return "payoff_mismatch";
  case PreflopClassCacheError::CountMismatch:
    return "count_mismatch";
  }
  return "unknown";
}

Result<PreflopClassCache, PreflopClassCacheError>
PreflopClassCache::build(const CompiledGame &game, const ca::ThreeWayTable &table) {
  if (game.config().player_count != 3U) {
    return Built::failure(PreflopClassCacheError::NotThreePlayers);
  }
  if (!table.complete() || table.entries().size() != entries) {
    return Built::failure(PreflopClassCacheError::IncompleteTable);
  }
  const auto &nodes = game.nodes();
  PreflopClassCache cache;
  cache.game_ = &game;
  cache.terms_.assign(nodes.size() * seat_count, PreflopClassTerm{});
  cache.value_slot_.assign(nodes.size() * seat_count, no_slot);
  cache.preflop_index_.assign(nodes.size(), no_slot);

  // Masses of every entry (H, first, second), as three_way_terminals builds
  // them: the five winner sets of a 3-active showdown, then win, tie and loss
  // against the first opponent with the second one folded (cards dead).
  bool showdowns = false;
  for (const auto &node : nodes) {
    showdowns = showdowns ||
                (node.kind == NodeKind::TerminalShowdown && node.street == Street::Preflop);
  }
  std::array<std::vector<double>, 5> three;
  std::array<std::vector<double>, 3> two;
  if (showdowns) {
    for (auto &masses : three) {
      masses.assign(entries, 0.0);
    }
    for (auto &masses : two) {
      masses.assign(entries, 0.0);
    }
    for (std::uint8_t hero = 0U; hero < classes; ++hero) {
      for (std::uint8_t first = 0U; first < classes; ++first) {
        for (std::uint8_t second = 0U; second < classes; ++second) {
          const auto index = ca::ThreeWayTable::entry_index(hero, first, second);
          const auto mass = table.three_way_mass(hero, first, second);
          three[0][index] = mass.hero_alone;
          three[1][index] = mass.with_first;
          three[2][index] = mass.with_second;
          three[3][index] = mass.all_three;
          three[4][index] = mass.hero_loses;
          const auto versus = table.two_way_mass(hero, first, second);
          two[0][index] = versus.wins;
          two[1][index] = versus.ties;
          two[2][index] = versus.losses;
        }
      }
    }
  }

  const auto set_deal = [&cache](const std::uint32_t node, const std::uint8_t seat,
                                 const double payoff) {
    cache.terms_[node * seat_count + seat] = {PreflopTermKind::Deal, 0U, payoff};
  };
  const auto add_tensor = [&cache](const std::uint32_t node, const std::uint8_t seat,
                                   std::vector<double> tensor) {
    cache.terms_[node * seat_count + seat] = {
        PreflopTermKind::Tensor, static_cast<std::uint32_t>(cache.tensors_.size()), 0.0};
    cache.tensors_.push_back(std::move(tensor));
  };

  for (const auto &node : nodes) {
    if (node.street != Street::Preflop) {
      continue;
    }
    cache.preflop_index_[node.id] = static_cast<std::uint32_t>(cache.preflop_nodes_.size());
    cache.preflop_nodes_.push_back(node.id);
    std::uint8_t edge = 0U;
    if (node.parent != no_node) {
      const auto siblings = game.edges_of(node.parent);
      const auto found = std::find_if(siblings.begin(), siblings.end(), [&node](const auto &e) {
        return e.child == node.id;
      });
      if (found == siblings.end() || nodes[node.parent].kind != NodeKind::Decision) {
        return Built::failure(PreflopClassCacheError::UnexpectedTerminal);
      }
      edge = static_cast<std::uint8_t>(found - siblings.begin());
    }
    cache.parent_edge_.push_back(edge);

    if (node.kind == NodeKind::TerminalFold) {
      const auto payoffs = game.fold_payoffs(node.id);
      for (std::uint8_t seat = 0U; seat < seat_count; ++seat) {
        set_deal(node.id, seat, static_cast<double>(payoffs[seat]) / units_per_ante);
      }
      ++cache.counts_.fold_terminals;
      continue;
    }
    if (node.kind == NodeKind::TerminalShowdown) {
      const auto status = game.states()[node.id].status;
      if (node.remaining_board_cards != 5U ||
          (status != HandStatus::AllInRunout && status != HandStatus::StreetComplete)) {
        return Built::failure(PreflopClassCacheError::UnexpectedTerminal);
      }
      if (status == HandStatus::AllInRunout) {
        ++cache.counts_.all_in_terminals;
      } else {
        ++cache.counts_.checkdown_leaves;
      }
      if (node.active_mask == 7U) {
        for (std::uint8_t seat = 0U; seat < seat_count; ++seat) {
          const auto seats = other_seats(seat);
          const auto seat_bit = static_cast<std::uint8_t>(1U << seat);
          const auto lower_bit = static_cast<std::uint8_t>(1U << seats[0]);
          const auto higher_bit = static_cast<std::uint8_t>(1U << seats[1]);
          const double lose = payoff_antes(game, node.id, lower_bit, seat);
          if (payoff_antes(game, node.id, higher_bit, seat) != lose ||
              payoff_antes(game, node.id, static_cast<std::uint8_t>(lower_bit | higher_bit),
                           seat) != lose) {
            return Built::failure(PreflopClassCacheError::PayoffMismatch);
          }
          const std::array<double, 5> payoffs{
              payoff_antes(game, node.id, seat_bit, seat),
              payoff_antes(game, node.id, static_cast<std::uint8_t>(seat_bit | lower_bit), seat),
              payoff_antes(game, node.id, static_cast<std::uint8_t>(seat_bit | higher_bit), seat),
              payoff_antes(game, node.id, 7U, seat), lose};
          std::vector<double> tensor(entries, 0.0);
          for (std::size_t index = 0; index < entries; ++index) {
            double value = 0.0;
            for (std::size_t outcome = 0; outcome < payoffs.size(); ++outcome) {
              value += three[outcome][index] * payoffs[outcome];
            }
            tensor[index] = value;
          }
          add_tensor(node.id, seat, std::move(tensor));
        }
        ++cache.counts_.three_active_showdowns;
        continue;
      }
      if (std::popcount(node.active_mask) != 2 || node.active_mask >= 8U) {
        return Built::failure(PreflopClassCacheError::UnexpectedTerminal);
      }
      const auto folded = static_cast<std::uint8_t>(
          std::countr_zero(static_cast<unsigned>(~node.active_mask & 7U)));
      const double folded_payoff = payoff_antes(game, node.id, node.active_mask, folded);
      for (std::uint8_t winners = 1U; winners <= node.active_mask; ++winners) {
        if ((winners & static_cast<std::uint8_t>(~node.active_mask)) == 0U &&
            payoff_antes(game, node.id, winners, folded) != folded_payoff) {
          return Built::failure(PreflopClassCacheError::PayoffMismatch);
        }
      }
      set_deal(node.id, folded, folded_payoff);
      for (std::uint8_t seat = 0U; seat < seat_count; ++seat) {
        if (seat == folded) {
          continue;
        }
        const auto seat_bit = static_cast<std::uint8_t>(1U << seat);
        const auto opponent_bit = static_cast<std::uint8_t>(node.active_mask & ~seat_bit);
        const auto opponent = static_cast<std::uint8_t>(std::countr_zero(opponent_bit));
        const std::array<double, 3> payoffs{payoff_antes(game, node.id, seat_bit, seat),
                                            payoff_antes(game, node.id, node.active_mask, seat),
                                            payoff_antes(game, node.id, opponent_bit, seat)};
        // The mass of (H, opponent, folder) sits at [H][opponent][folder] when the
        // opponent is the lower other seat; transposed when the folder is.
        const bool transposed = folded < opponent;
        std::vector<double> tensor(entries, 0.0);
        for (std::uint8_t hero = 0U; hero < classes; ++hero) {
          for (std::uint8_t lower = 0U; lower < classes; ++lower) {
            for (std::uint8_t higher = 0U; higher < classes; ++higher) {
              const auto source = transposed ? ca::ThreeWayTable::entry_index(hero, higher, lower)
                                             : ca::ThreeWayTable::entry_index(hero, lower, higher);
              double value = 0.0;
              for (std::size_t outcome = 0; outcome < payoffs.size(); ++outcome) {
                value += two[outcome][source] * payoffs[outcome];
              }
              tensor[ca::ThreeWayTable::entry_index(hero, lower, higher)] = value;
            }
          }
        }
        add_tensor(node.id, seat, std::move(tensor));
      }
      ++cache.counts_.two_active_showdowns;
      continue;
    }
    // A decision or a flop entry right after a fold: the seat that folded is
    // out, and its payoff is the same at every terminal below.
    if (node.parent == no_node) {
      continue;
    }
    const auto actor = nodes[node.parent].actor;
    if (actor >= seat_count ||
        (node.active_mask & static_cast<std::uint8_t>(1U << actor)) != 0U) {
      continue;
    }
    std::int64_t payoff = 0;
    if (!constant_payoff(game, node.id, actor, payoff)) {
      return Built::failure(PreflopClassCacheError::PayoffMismatch);
    }
    set_deal(node.id, actor, static_cast<double>(payoff) / units_per_ante);
    ++cache.counts_.folded_children;
  }

  // The terms cover exactly the preflop terminals the compiler counted (it
  // counts the checkdown leaves as preflop all-in runouts).
  const auto &stats = game.stats();
  if (cache.counts_.fold_terminals != stats.preflop_terminal_folds ||
      cache.counts_.all_in_terminals + cache.counts_.checkdown_leaves !=
          stats.preflop_all_in_runouts ||
      cache.counts_.three_active_showdowns + cache.counts_.two_active_showdowns !=
          cache.counts_.all_in_terminals + cache.counts_.checkdown_leaves) {
    return Built::failure(PreflopClassCacheError::CountMismatch);
  }
  cache.counts_.tensors = static_cast<std::uint32_t>(cache.tensors_.size());

  // Value slots and the per-seat entries in increasing node id.
  std::uint32_t slots = 0U;
  for (const auto id : cache.preflop_nodes_) {
    for (std::uint8_t seat = 0U; seat < seat_count; ++seat) {
      if (cache.terms_[id * seat_count + seat].kind == PreflopTermKind::None) {
        continue;
      }
      cache.value_slot_[id * seat_count + seat] = slots;
      cache.entries_[seat].push_back({id, slots});
      ++cache.counts_.terms_by_seat[seat];
      ++slots;
    }
  }
  cache.values_.assign(static_cast<std::size_t>(slots) * classes, 0.0);
  cache.reach_.assign(cache.preflop_nodes_.size() * seat_count * classes, 1.0);

  std::string identity = "gtosd.preflop_blueprint.preflop_class_cache.v1|folded=dead|scale=";
  identity += "142506/278256|three-way-table=" + table.fingerprint() + "|game=" +
              game.fingerprint() + "|all-in=" + std::to_string(cache.counts_.all_in_terminals) +
              "|checkdown=" + std::to_string(cache.counts_.checkdown_leaves) +
              "|fold=" + std::to_string(cache.counts_.fold_terminals) +
              "|folded-children=" + std::to_string(cache.counts_.folded_children) +
              "|tensors=" + std::to_string(cache.counts_.tensors);
  cache.fingerprint_ = "fnv1a64:" + detail::hex64_text(detail::fnv1a_text(identity));
  return Built::success(std::move(cache));
}

void PreflopClassCache::compute_reach(const std::span<const double> policy,
                                      const std::span<const std::uint64_t> node_offsets) {
  const auto &nodes = game_->nodes();
  constexpr std::size_t stride = seat_count * classes;
  for (std::size_t index = 0; index < preflop_nodes_.size(); ++index) {
    const auto id = preflop_nodes_[index];
    double *target = reach_.data() + index * stride;
    const auto parent = nodes[id].parent;
    if (parent == no_node) {
      std::fill_n(target, stride, 1.0);
      continue;
    }
    // Preorder: the parent's reach is final.
    const double *source = reach_.data() + static_cast<std::size_t>(preflop_index_[parent]) * stride;
    std::copy_n(source, stride, target);
    const auto &decision = nodes[parent];
    const auto actions = static_cast<std::size_t>(decision.action_count);
    const double *rows = policy.data() + node_offsets[parent];
    double *actor = target + static_cast<std::size_t>(decision.actor) * classes;
    const auto edge = static_cast<std::size_t>(parent_edge_[index]);
    for (std::size_t hand_class = 0; hand_class < classes; ++hand_class) {
      actor[hand_class] *= rows[hand_class * actions + edge];
    }
  }
}

void PreflopClassCache::class_deal_values(const double *lower, const double *higher,
                                          double *out) {
  // values[H] = S_1 S_2 - sum_{c not in h} C_1(c) C_2(c) + P (step 1,
  // three_way_deal_values), with h the representative of H.
  const auto &combos = ca::combo_table();
  const auto &pair_class = card_pair_classes();
  std::array<double, ca::deck_cards> first_by_card{};
  std::array<double, ca::deck_cards> second_by_card{};
  std::array<double, ca::deck_cards> both_by_card{};
  double first_total = 0.0;
  double second_total = 0.0;
  double both_total = 0.0;
  for (std::size_t combo = 0; combo < ca::combo_count; ++combo) {
    const auto hand_class = combos.hand_class[combo];
    const double a = lower[hand_class];
    const double b = higher[hand_class];
    first_total += a;
    second_total += b;
    both_total += a * b;
    for (const auto card : combos.cards[combo]) {
      first_by_card[card] += a;
      second_by_card[card] += b;
      both_by_card[card] += a * b;
    }
  }
  for (std::size_t hand_class = 0; hand_class < classes; ++hand_class) {
    const auto &cards =
        combos.cards[ca::ThreeWayTable::representative_of(static_cast<std::uint8_t>(hand_class))];
    const auto x = cards[0];
    const auto y = cards[1];
    const double a_h = lower[hand_class];
    const double b_h = higher[hand_class];
    const double first_live = first_total - first_by_card[x] - first_by_card[y] + a_h;
    const double second_live = second_total - second_by_card[x] - second_by_card[y] + b_h;
    const double both_live = both_total - both_by_card[x] - both_by_card[y] + a_h * b_h;
    double shared = 0.0;
    for (std::uint8_t card = 0U; card < ca::deck_cards; ++card) {
      if (card == x || card == y) {
        continue;
      }
      const auto with_x = pair_class[card][x];
      const auto with_y = pair_class[card][y];
      shared += (first_by_card[card] - lower[with_x] - lower[with_y]) *
                (second_by_card[card] - higher[with_x] - higher[with_y]);
    }
    out[hand_class] = first_live * second_live - shared + both_live;
  }
}

void PreflopClassCache::contract_tensor(const std::span<const double> tensor, const double scale,
                                        const double *lower, const double *higher, double *outer,
                                        double *out) {
  constexpr std::size_t stride = classes * classes;
  // The outer product in the layout of step 1's others_outer: 1 * lower[A],
  // then times higher[B].
  for (std::size_t first = 0; first < classes; ++first) {
    const double a = 1.0 * lower[first];
    for (std::size_t second = 0; second < classes; ++second) {
      outer[first * classes + second] = a * higher[second];
    }
  }
  for (std::size_t hand_class = 0; hand_class < classes; ++hand_class) {
    const double *row = tensor.data() + hand_class * stride;
    std::array<double, 4> sums{};
    std::size_t index = 0;
    for (; index + 4U <= stride; index += 4U) {
      sums[0] += row[index] * outer[index];
      sums[1] += row[index + 1U] * outer[index + 1U];
      sums[2] += row[index + 2U] * outer[index + 2U];
      sums[3] += row[index + 3U] * outer[index + 3U];
    }
    for (; index < stride; ++index) {
      sums[0] += row[index] * outer[index];
    }
    out[hand_class] = scale * ((sums[0] + sums[1]) + (sums[2] + sums[3]));
  }
}

void PreflopClassCache::deal_values(const std::uint32_t node, const std::uint8_t hero,
                                    double *out) const {
  const auto seats = other_seats(hero);
  class_deal_values(reach(node, seats[0]), reach(node, seats[1]), out);
}

void PreflopClassCache::contract(const std::uint8_t hero, const unsigned threads) {
  const auto &list = entries_[hero];
  const auto seats = other_seats(hero);
  const auto compute = [&](const Entry &entry, double *outer) {
    const auto &term = terms_[entry.node * seat_count + hero];
    double *out = values_.data() + static_cast<std::size_t>(entry.slot) * classes;
    const double *lower = reach(entry.node, seats[0]);
    const double *higher = reach(entry.node, seats[1]);
    if (term.kind == PreflopTermKind::Tensor) {
      contract_tensor(tensors_[term.tensor], board_scale, lower, higher, outer, out);
      return;
    }
    class_deal_values(lower, higher, out);
    const double scale = term.payoff * board_scale;
    for (std::size_t hand_class = 0; hand_class < classes; ++hand_class) {
      out[hand_class] *= scale;
    }
  };
  const auto workers = std::max(1U, std::min<unsigned>(threads, static_cast<unsigned>(list.size())));
  std::vector<std::vector<double>> scratch(workers, std::vector<double>(classes * classes, 0.0));
  if (workers == 1U) {
    for (const auto &entry : list) {
      compute(entry, scratch[0].data());
    }
    return;
  }
  std::atomic<std::size_t> next{0U};
  const auto work = [&](const unsigned worker) {
    while (true) {
      const auto index = next.fetch_add(1U, std::memory_order_relaxed);
      if (index >= list.size()) {
        return;
      }
      compute(list[index], scratch[worker].data());
    }
  };
  {
    std::vector<std::jthread> pool;
    pool.reserve(workers - 1U);
    for (unsigned worker = 1U; worker < workers; ++worker) {
      pool.emplace_back(work, worker);
    }
    work(0U);
  }
}

const double *PreflopClassCache::values(const std::uint32_t node,
                                        const std::uint8_t seat) const noexcept {
  if (node >= preflop_index_.size() || seat >= seat_count) {
    return nullptr;
  }
  const auto slot = value_slot_[node * seat_count + seat];
  return slot == no_slot ? nullptr : values_.data() + static_cast<std::size_t>(slot) * classes;
}

const double *PreflopClassCache::reach(const std::uint32_t node,
                                       const std::uint8_t seat) const noexcept {
  if (node >= preflop_index_.size() || seat >= seat_count) {
    return nullptr;
  }
  const auto index = preflop_index_[node];
  return index == no_slot
             ? nullptr
             : reach_.data() + (static_cast<std::size_t>(index) * seat_count + seat) * classes;
}

const PreflopClassTerm &PreflopClassCache::term(const std::uint32_t node,
                                                const std::uint8_t seat) const noexcept {
  static const PreflopClassTerm none{};
  if (node >= preflop_index_.size() || seat >= seat_count) {
    return none;
  }
  return terms_[node * seat_count + seat];
}

std::uint64_t PreflopClassCache::tensor_bytes() const noexcept {
  std::uint64_t bytes = 0U;
  for (const auto &tensor : tensors_) {
    bytes += tensor.capacity() * sizeof(double);
  }
  return bytes;
}

std::uint64_t PreflopClassCache::memory_bytes() const noexcept {
  std::uint64_t bytes = tensor_bytes();
  bytes += tensors_.capacity() * sizeof(std::vector<double>);
  bytes += terms_.capacity() * sizeof(PreflopClassTerm);
  bytes += (value_slot_.capacity() + preflop_index_.capacity() + preflop_nodes_.capacity()) *
           sizeof(std::uint32_t);
  bytes += parent_edge_.capacity();
  bytes += (reach_.capacity() + values_.capacity()) * sizeof(double);
  for (const auto &list : entries_) {
    bytes += list.capacity() * sizeof(Entry);
  }
  return bytes;
}

} // namespace gtosd::preflop_blueprint
