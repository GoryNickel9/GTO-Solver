#include "gtosd/card_abstraction/all_in_table.hpp"

#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "resource_file.hpp"

#include <algorithm>
#include <new>
#include <string_view>
#include <system_error>
#include <thread>

namespace gtosd::card_abstraction {
namespace {

constexpr std::string_view resource_kind = "preflop_all_in_pairs";

void accumulate_boards(const RankTable &ranks, const std::uint64_t first_board,
                       const std::uint64_t last_board, std::vector<PairOutcome> &counters) {
  LiveHands live;
  live.combo_ids.reserve(465U);
  live.cards.reserve(465U);
  std::vector<std::uint16_t> hand_ranks;
  std::array<std::uint8_t, 5> board{};
  for (std::uint64_t index = first_board; index < last_board; ++index) {
    static_cast<void>(subset_from_index(index, board));
    std::uint64_t board_mask = 0U;
    for (const auto card : board) {
      board_mask |= std::uint64_t{1} << card;
    }
    collect_live_hands(board_mask, live);
    hand_ranks.resize(live.cards.size());
    for (std::size_t hand = 0U; hand < live.cards.size(); ++hand) {
      hand_ranks[hand] = ranks.rank_of(live.cards[hand], board);
    }
    const auto count = live.cards.size();
    for (std::size_t i = 0U; i + 1U < count; ++i) {
      const auto &a = live.cards[i];
      const auto rank_a = hand_ranks[i];
      for (std::size_t j = i + 1U; j < count; ++j) {
        const auto &b = live.cards[j];
        if (b[0] == a[0] || b[0] == a[1] || b[1] == a[0] || b[1] == a[1]) {
          continue;
        }
        auto &entry = counters[combo_pair_index(live.combo_ids[i], live.combo_ids[j])];
        if (rank_a > hand_ranks[j]) {
          ++entry.wins;
        } else if (rank_a == hand_ranks[j]) {
          ++entry.ties;
        } else {
          ++entry.losses;
        }
      }
    }
  }
}

} // namespace

Result<AllInTable, ResourceError> AllInTable::build(const RankTable &ranks,
                                                    const unsigned threads) {
  using Built = Result<AllInTable, ResourceError>;
  if (threads == 0U || threads > 64U) {
    return Built::failure(ResourceError::InvalidInput);
  }
  try {
    const auto board_count = binomial(deck_cards, 5U);
    std::vector<std::vector<PairOutcome>> partial(
        threads, std::vector<PairOutcome>(combo_pair_count, PairOutcome{}));
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (unsigned worker = 0U; worker < threads; ++worker) {
      const auto first = board_count * worker / threads;
      const auto last = board_count * (worker + 1U) / threads;
      workers.emplace_back([&ranks, first, last, &counters = partial[worker]] {
        accumulate_boards(ranks, first, last, counters);
      });
    }
    for (auto &worker : workers) {
      worker.join();
    }
    AllInTable table;
    table.entries_.assign(combo_pair_count, PairOutcome{});
    for (const auto &counters : partial) {
      for (std::size_t index = 0U; index < counters.size(); ++index) {
        table.entries_[index].wins += counters[index].wins;
        table.entries_[index].ties += counters[index].ties;
        table.entries_[index].losses += counters[index].losses;
      }
    }
    table.finalize(ranks.fingerprint());
    return Built::success(std::move(table));
  } catch (const std::bad_alloc &) {
    return Built::failure(ResourceError::MemoryFailure);
  } catch (const std::system_error &) {
    return Built::failure(ResourceError::InvalidInput);
  }
}

void AllInTable::finalize(const std::string &rank_fingerprint) {
  auto hash = detail::fnv1a_text("gtosd.card_abstraction.preflop_all_in_pairs.v1|");
  hash = detail::fnv1a_text(rank_fingerprint, hash);
  std::vector<std::uint8_t> bytes;
  bytes.reserve(entries_.size() * 12U);
  for (const auto &entry : entries_) {
    detail::append_little(bytes, entry.wins);
    detail::append_little(bytes, entry.ties);
    detail::append_little(bytes, entry.losses);
  }
  hash = detail::fnv1a(bytes, hash);
  fingerprint_ = "fnv1a64:" + detail::hex64(hash);
}

PairOutcome AllInTable::outcome(const std::uint16_t hero,
                                const std::uint16_t opponent) const noexcept {
  if (hero == opponent) {
    return PairOutcome{};
  }
  const auto &entry = entries_[combo_pair_index(hero, opponent)];
  if (hero < opponent) {
    return entry;
  }
  return PairOutcome{entry.losses, entry.ties, entry.wins};
}

Result<bool, ResourceError> AllInTable::save(const std::filesystem::path &path) const {
  std::vector<std::uint8_t> payload;
  payload.reserve(entries_.size() * 12U + 8U);
  detail::append_little(payload, static_cast<std::uint64_t>(entries_.size()));
  for (const auto &entry : entries_) {
    detail::append_little(payload, entry.wins);
    detail::append_little(payload, entry.ties);
    detail::append_little(payload, entry.losses);
  }
  return detail::write_resource(path, resource_kind, format_version, fingerprint_, payload);
}

Result<AllInTable, ResourceError> AllInTable::load(const std::filesystem::path &path) {
  using Loaded = Result<AllInTable, ResourceError>;
  const auto resource = detail::read_resource(path, resource_kind, format_version);
  if (!resource) {
    return Loaded::failure(resource.error());
  }
  const std::span<const std::uint8_t> bytes(resource.value().bytes);
  std::size_t position = 0U;
  std::uint64_t count = 0U;
  if (!detail::read_little(bytes, position, count) || count != combo_pair_count) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  AllInTable table;
  table.entries_.resize(static_cast<std::size_t>(count));
  for (auto &entry : table.entries_) {
    if (!detail::read_little(bytes, position, entry.wins) ||
        !detail::read_little(bytes, position, entry.ties) ||
        !detail::read_little(bytes, position, entry.losses)) {
      return Loaded::failure(ResourceError::IntegrityFailure);
    }
  }
  if (position != bytes.size()) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  table.fingerprint_ = resource.value().fingerprint;
  return Loaded::success(std::move(table));
}

} // namespace gtosd::card_abstraction
