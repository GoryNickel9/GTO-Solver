#include "gtosd/card_abstraction/exact_features.hpp"

#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/core/ranges.hpp"
#include "resource_file.hpp"

#include <algorithm>
#include <new>
#include <numeric>
#include <string_view>
#include <system_error>
#include <thread>

namespace gtosd::card_abstraction {
namespace {

constexpr std::string_view groups_kind = "opponent_groups";
constexpr std::string_view flop_kind = "flop_equity_histograms";
constexpr std::string_view turn_kind = "turn_equity_histograms";
constexpr std::string_view river_kind = "river_group_equities";

template <typename Body>
Result<bool, ResourceError> parallel_ranges(const std::uint64_t count, const unsigned threads,
                                            Body &&body) {
  if (threads == 0U || threads > 64U) {
    return Result<bool, ResourceError>::failure(ResourceError::InvalidInput);
  }
  try {
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (unsigned worker = 0U; worker < threads; ++worker) {
      const auto first = count * worker / threads;
      const auto last = count * (worker + 1U) / threads;
      workers.emplace_back([first, last, &body] { body(first, last); });
    }
    for (auto &worker : workers) {
      worker.join();
    }
  } catch (const std::system_error &) {
    return Result<bool, ResourceError>::failure(ResourceError::InvalidInput);
  }
  return Result<bool, ResourceError>::success(true);
}

std::array<std::uint8_t, 5> board_values(const std::array<CardId, 3> &flop, const std::uint8_t turn,
                                         const std::uint8_t river) noexcept {
  std::array<std::uint8_t, 5> board{flop[0].value(), flop[1].value(), flop[2].value(), turn,
                                    river};
  std::sort(board.begin(), board.end());
  return board;
}

std::uint64_t mask_of(const std::array<std::uint8_t, 5> &board) noexcept {
  std::uint64_t mask = 0U;
  for (const auto card : board) {
    mask |= std::uint64_t{1} << card;
  }
  return mask;
}

struct BoardScratch {
  LiveHands live;
  std::vector<std::uint16_t> ranks;
  std::vector<HandOutcomeCounts> outcomes;

  BoardScratch() {
    live.combo_ids.reserve(465U);
    live.cards.reserve(465U);
    ranks.reserve(465U);
    outcomes.reserve(465U);
  }

  void evaluate(const RankTable &table, const std::array<std::uint8_t, 5> &board) {
    collect_live_hands(mask_of(board), live);
    ranks.resize(live.cards.size());
    outcomes.assign(live.cards.size(), HandOutcomeCounts{});
    for (std::size_t hand = 0U; hand < live.cards.size(); ++hand) {
      ranks[hand] = table.rank_of(live.cards[hand], board);
    }
    count_showdown_outcomes(live.cards, ranks, outcomes);
  }
};

std::string table_fingerprint(const std::string_view domain, const std::string &catalog,
                              const std::string &ranks, const std::string &extra,
                              const std::span<const std::uint8_t> payload) {
  auto hash = detail::fnv1a_text(domain);
  hash = detail::fnv1a_text("|", hash);
  hash = detail::fnv1a_text(catalog, hash);
  hash = detail::fnv1a_text("|", hash);
  hash = detail::fnv1a_text(ranks, hash);
  hash = detail::fnv1a_text("|", hash);
  hash = detail::fnv1a_text(extra, hash);
  hash = detail::fnv1a_text("|", hash);
  hash = detail::fnv1a(payload, hash);
  return "fnv1a64:" + detail::hex64(hash);
}

template <typename Unsigned>
std::vector<std::uint8_t> payload_of(const std::string &catalog, const std::string &ranks,
                                     const std::string &extra,
                                     const std::vector<Unsigned> &values) {
  std::vector<std::uint8_t> payload;
  payload.reserve(values.size() * sizeof(Unsigned) + catalog.size() + ranks.size() + 64U);
  detail::append_little(payload, static_cast<std::uint32_t>(catalog.size()));
  for (const auto character : catalog) {
    payload.push_back(static_cast<std::uint8_t>(character));
  }
  detail::append_little(payload, static_cast<std::uint32_t>(ranks.size()));
  for (const auto character : ranks) {
    payload.push_back(static_cast<std::uint8_t>(character));
  }
  detail::append_little(payload, static_cast<std::uint32_t>(extra.size()));
  for (const auto character : extra) {
    payload.push_back(static_cast<std::uint8_t>(character));
  }
  detail::append_vector(payload, values);
  return payload;
}

template <typename Unsigned>
bool parse_payload(const std::span<const std::uint8_t> bytes, std::string &catalog,
                   std::string &ranks, std::string &extra, std::vector<Unsigned> &values,
                   const std::uint64_t expected_values) {
  std::size_t position = 0U;
  const auto read_text = [&](std::string &target) {
    std::uint32_t size = 0U;
    if (!detail::read_little(bytes, position, size) || position + size > bytes.size()) {
      return false;
    }
    target.assign(reinterpret_cast<const char *>(bytes.data() + position), size);
    position += size;
    return true;
  };
  return read_text(catalog) && read_text(ranks) && read_text(extra) &&
         detail::read_vector(bytes, position, values, expected_values) &&
         position == bytes.size();
}

} // namespace

OpponentGroups OpponentGroups::build(const AllInTable &all_in) {
  OpponentGroups groups;
  const auto &combos = combo_table();
  std::array<double, 81> equity_sum{};
  std::array<double, 81> weight{};
  for (std::uint16_t hero = 0U; hero < combo_count; ++hero) {
    for (std::uint16_t opponent = 0U; opponent < combo_count; ++opponent) {
      if (hero == opponent || (combos.masks[hero] & combos.masks[opponent]) != 0U) {
        continue;
      }
      const auto outcome = all_in.outcome(hero, opponent);
      equity_sum[combos.hand_class[hero]] += outcome.equity();
      weight[combos.hand_class[hero]] += 1.0;
    }
  }
  std::array<std::uint8_t, 81> order{};
  for (std::uint8_t index = 0U; index < order.size(); ++index) {
    order[index] = index;
    groups.class_equity[index] = weight[index] > 0.0 ? equity_sum[index] / weight[index] : 0.5;
  }
  std::stable_sort(order.begin(), order.end(), [&](const std::uint8_t left, const std::uint8_t right) {
    return groups.class_equity[left] > groups.class_equity[right];
  });
  // Greedy cut into eight groups of similar combo mass (630 / 8 = 78.75).
  const double target = static_cast<double>(combo_count) / opponent_group_count;
  std::uint8_t group = 0U;
  double cumulative = 0.0;
  for (const auto hand_class_id : order) {
    const auto mass = static_cast<double>(class_mass(hand_class_id));
    if (group + 1U < opponent_group_count &&
        cumulative + mass / 2.0 >= target * static_cast<double>(group + 1U)) {
      ++group;
    }
    groups.group_of_class[hand_class_id] = group;
    groups.group_mass[group] = static_cast<std::uint16_t>(groups.group_mass[group] + mass);
    cumulative += mass;
  }
  std::vector<std::uint8_t> payload;
  for (const auto value : groups.group_of_class) {
    payload.push_back(value);
  }
  auto hash = detail::fnv1a_text("gtosd.card_abstraction.opponent_groups.v1|");
  hash = detail::fnv1a_text(all_in.fingerprint(), hash);
  hash = detail::fnv1a(payload, hash);
  groups.fingerprint = "fnv1a64:" + detail::hex64(hash);
  return groups;
}

Result<bool, ResourceError> OpponentGroups::save(const std::filesystem::path &path) const {
  std::vector<std::uint8_t> payload;
  for (const auto value : group_of_class) {
    payload.push_back(value);
  }
  for (const auto value : class_equity) {
    std::uint64_t bits = 0U;
    static_assert(sizeof(bits) == sizeof(value));
    std::copy_n(reinterpret_cast<const std::uint8_t *>(&value), sizeof(value),
                reinterpret_cast<std::uint8_t *>(&bits));
    detail::append_little(payload, bits);
  }
  for (const auto value : group_mass) {
    detail::append_little(payload, value);
  }
  return detail::write_resource(path, groups_kind, format_version, fingerprint, payload);
}

Result<OpponentGroups, ResourceError> OpponentGroups::load(const std::filesystem::path &path) {
  using Loaded = Result<OpponentGroups, ResourceError>;
  const auto resource = detail::read_resource(path, groups_kind, format_version);
  if (!resource) {
    return Loaded::failure(resource.error());
  }
  const std::span<const std::uint8_t> bytes(resource.value().bytes);
  OpponentGroups groups;
  std::size_t position = 0U;
  if (bytes.size() < groups.group_of_class.size()) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  for (auto &value : groups.group_of_class) {
    value = bytes[position++];
    if (value >= opponent_group_count) {
      return Loaded::failure(ResourceError::IntegrityFailure);
    }
  }
  for (auto &value : groups.class_equity) {
    std::uint64_t bits = 0U;
    if (!detail::read_little(bytes, position, bits)) {
      return Loaded::failure(ResourceError::IntegrityFailure);
    }
    std::copy_n(reinterpret_cast<const std::uint8_t *>(&bits), sizeof(bits),
                reinterpret_cast<std::uint8_t *>(&value));
  }
  for (auto &value : groups.group_mass) {
    if (!detail::read_little(bytes, position, value)) {
      return Loaded::failure(ResourceError::IntegrityFailure);
    }
  }
  if (position != bytes.size()) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  groups.fingerprint = resource.value().fingerprint;
  return Loaded::success(std::move(groups));
}

Result<FlopFeatureTable, ResourceError> FlopFeatureTable::build(const BoardCatalog &catalog,
                                                                const RankTable &ranks,
                                                                const unsigned threads) {
  using Built = Result<FlopFeatureTable, ResourceError>;
  try {
    FlopFeatureTable table;
    table.counts_.assign(static_cast<std::size_t>(catalog.flops().size()) * combo_count *
                             equity_histogram_bins,
                         0U);
    const auto parallel = parallel_ranges(
        catalog.flops().size(), threads, [&](const std::uint64_t first, const std::uint64_t last) {
          BoardScratch scratch;
          for (std::uint64_t flop_index = first; flop_index < last; ++flop_index) {
            const auto &flop = catalog.flops()[static_cast<std::size_t>(flop_index)].cards;
            const std::uint64_t flop_mask = flop[0].mask() | flop[1].mask() | flop[2].mask();
            auto *rows = table.counts_.data() +
                         static_cast<std::size_t>(flop_index) * combo_count * equity_histogram_bins;
            for (std::uint8_t turn = 0U; turn < 36U; ++turn) {
              if ((flop_mask & (std::uint64_t{1} << turn)) != 0U) {
                continue;
              }
              for (std::uint8_t river = static_cast<std::uint8_t>(turn + 1U); river < 36U; ++river) {
                if ((flop_mask & (std::uint64_t{1} << river)) != 0U) {
                  continue;
                }
                scratch.evaluate(ranks, board_values(flop, turn, river));
                for (std::size_t hand = 0U; hand < scratch.live.cards.size(); ++hand) {
                  const auto bin = equity_bin(scratch.outcomes[hand].equity());
                  ++rows[static_cast<std::size_t>(scratch.live.combo_ids[hand]) *
                             equity_histogram_bins +
                         bin];
                }
              }
            }
          }
        });
    if (!parallel) {
      return Built::failure(parallel.error());
    }
    table.catalog_fingerprint_ = catalog.fingerprint();
    table.rank_fingerprint_ = ranks.fingerprint();
    table.fingerprint_ = table_fingerprint(
        "gtosd.card_abstraction.flop_equity_histograms.v1", table.catalog_fingerprint_,
        table.rank_fingerprint_, "bins=16;runouts=465",
        payload_of(table.catalog_fingerprint_, table.rank_fingerprint_, "bins=16;runouts=465",
                   table.counts_));
    return Built::success(std::move(table));
  } catch (const std::bad_alloc &) {
    return Built::failure(ResourceError::MemoryFailure);
  }
}

Result<bool, ResourceError> FlopFeatureTable::save(const std::filesystem::path &path) const {
  return detail::write_resource(
      path, flop_kind, format_version, fingerprint_,
      payload_of(catalog_fingerprint_, rank_fingerprint_, "bins=16;runouts=465", counts_));
}

Result<FlopFeatureTable, ResourceError> FlopFeatureTable::load(const std::filesystem::path &path) {
  using Loaded = Result<FlopFeatureTable, ResourceError>;
  const auto resource = detail::read_resource(path, flop_kind, format_version);
  if (!resource) {
    return Loaded::failure(resource.error());
  }
  FlopFeatureTable table;
  std::string extra;
  if (!parse_payload(std::span<const std::uint8_t>(resource.value().bytes),
                     table.catalog_fingerprint_, table.rank_fingerprint_, extra, table.counts_,
                     static_cast<std::uint64_t>(canonical_flop_count) * combo_count *
                         equity_histogram_bins) ||
      extra != "bins=16;runouts=465") {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  table.fingerprint_ = table_fingerprint(
      "gtosd.card_abstraction.flop_equity_histograms.v1", table.catalog_fingerprint_,
      table.rank_fingerprint_, extra, std::span<const std::uint8_t>(resource.value().bytes));
  if (table.fingerprint_ != resource.value().fingerprint) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  return Loaded::success(std::move(table));
}

Result<TurnFeatureTable, ResourceError> TurnFeatureTable::build(const BoardCatalog &catalog,
                                                                const RankTable &ranks,
                                                                const unsigned threads) {
  using Built = Result<TurnFeatureTable, ResourceError>;
  try {
    TurnFeatureTable table;
    table.counts_.assign(static_cast<std::size_t>(catalog.flop_turns().size()) * combo_count *
                             equity_histogram_bins,
                         0U);
    const auto parallel = parallel_ranges(
        catalog.flop_turns().size(), threads,
        [&](const std::uint64_t first, const std::uint64_t last) {
          BoardScratch scratch;
          for (std::uint64_t index = first; index < last; ++index) {
            const auto &entry = catalog.flop_turns()[static_cast<std::size_t>(index)];
            const std::uint64_t dead_mask = entry.flop[0].mask() | entry.flop[1].mask() |
                                            entry.flop[2].mask() | entry.turn.mask();
            auto *rows = table.counts_.data() +
                         static_cast<std::size_t>(index) * combo_count * equity_histogram_bins;
            for (std::uint8_t river = 0U; river < 36U; ++river) {
              if ((dead_mask & (std::uint64_t{1} << river)) != 0U) {
                continue;
              }
              scratch.evaluate(ranks, board_values(entry.flop, entry.turn.value(), river));
              for (std::size_t hand = 0U; hand < scratch.live.cards.size(); ++hand) {
                const auto bin = equity_bin(scratch.outcomes[hand].equity());
                ++rows[static_cast<std::size_t>(scratch.live.combo_ids[hand]) *
                           equity_histogram_bins +
                       bin];
              }
            }
          }
        });
    if (!parallel) {
      return Built::failure(parallel.error());
    }
    table.catalog_fingerprint_ = catalog.fingerprint();
    table.rank_fingerprint_ = ranks.fingerprint();
    table.fingerprint_ = table_fingerprint(
        "gtosd.card_abstraction.turn_equity_histograms.v1", table.catalog_fingerprint_,
        table.rank_fingerprint_, "bins=16;runouts=30",
        payload_of(table.catalog_fingerprint_, table.rank_fingerprint_, "bins=16;runouts=30",
                   table.counts_));
    return Built::success(std::move(table));
  } catch (const std::bad_alloc &) {
    return Built::failure(ResourceError::MemoryFailure);
  }
}

Result<bool, ResourceError> TurnFeatureTable::save(const std::filesystem::path &path) const {
  return detail::write_resource(
      path, turn_kind, format_version, fingerprint_,
      payload_of(catalog_fingerprint_, rank_fingerprint_, "bins=16;runouts=30", counts_));
}

Result<TurnFeatureTable, ResourceError> TurnFeatureTable::load(const std::filesystem::path &path) {
  using Loaded = Result<TurnFeatureTable, ResourceError>;
  const auto resource = detail::read_resource(path, turn_kind, format_version);
  if (!resource) {
    return Loaded::failure(resource.error());
  }
  TurnFeatureTable table;
  std::string extra;
  if (!parse_payload(std::span<const std::uint8_t>(resource.value().bytes),
                     table.catalog_fingerprint_, table.rank_fingerprint_, extra, table.counts_,
                     static_cast<std::uint64_t>(canonical_flop_turn_count) * combo_count *
                         equity_histogram_bins) ||
      extra != "bins=16;runouts=30") {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  table.fingerprint_ = table_fingerprint(
      "gtosd.card_abstraction.turn_equity_histograms.v1", table.catalog_fingerprint_,
      table.rank_fingerprint_, extra, std::span<const std::uint8_t>(resource.value().bytes));
  if (table.fingerprint_ != resource.value().fingerprint) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  return Loaded::success(std::move(table));
}

Result<RiverFeatureTable, ResourceError>
RiverFeatureTable::build(const BoardCatalog &catalog, const RankTable &ranks,
                         const OpponentGroups &groups, const unsigned threads) {
  using Built = Result<RiverFeatureTable, ResourceError>;
  try {
    RiverFeatureTable table;
    table.values_.assign(static_cast<std::size_t>(catalog.river_boards().size()) * combo_count *
                             river_feature_count,
                         0U);
    const auto &combos = combo_table();
    const auto parallel = parallel_ranges(
        catalog.river_boards().size(), threads,
        [&](const std::uint64_t first, const std::uint64_t last) {
          LiveHands live;
          std::vector<std::uint16_t> hand_ranks;
          std::array<std::uint8_t, 5> board{};
          for (std::uint64_t index = first; index < last; ++index) {
            const auto &entry = catalog.river_boards()[static_cast<std::size_t>(index)];
            for (std::size_t card = 0U; card < board.size(); ++card) {
              board[card] = entry.cards[card].value();
            }
            collect_live_hands(mask_of(board), live);
            hand_ranks.resize(live.cards.size());
            for (std::size_t hand = 0U; hand < live.cards.size(); ++hand) {
              hand_ranks[hand] = ranks.rank_of(live.cards[hand], board);
            }
            auto *rows = table.values_.data() +
                         static_cast<std::size_t>(index) * combo_count * river_feature_count;
            for (std::size_t hero = 0U; hero < live.cards.size(); ++hero) {
              const auto &h = live.cards[hero];
              std::array<HandOutcomeCounts, river_feature_count> counts{};
              for (std::size_t opponent = 0U; opponent < live.cards.size(); ++opponent) {
                const auto &o = live.cards[opponent];
                if (opponent == hero || o[0] == h[0] || o[0] == h[1] || o[1] == h[0] ||
                    o[1] == h[1]) {
                  continue;
                }
                const auto group =
                    1U + groups.group_of_class[combos.hand_class[live.combo_ids[opponent]]];
                if (hand_ranks[hero] > hand_ranks[opponent]) {
                  ++counts[0].wins;
                  ++counts[group].wins;
                } else if (hand_ranks[hero] == hand_ranks[opponent]) {
                  ++counts[0].ties;
                  ++counts[group].ties;
                } else {
                  ++counts[0].losses;
                  ++counts[group].losses;
                }
              }
              auto *row = rows + static_cast<std::size_t>(live.combo_ids[hero]) * river_feature_count;
              for (std::size_t feature = 0U; feature < river_feature_count; ++feature) {
                row[feature] = equity_fixed_point(counts[feature].equity());
              }
            }
          }
        });
    if (!parallel) {
      return Built::failure(parallel.error());
    }
    table.catalog_fingerprint_ = catalog.fingerprint();
    table.rank_fingerprint_ = ranks.fingerprint();
    table.groups_fingerprint_ = groups.fingerprint;
    table.fingerprint_ = table_fingerprint(
        "gtosd.card_abstraction.river_group_equities.v1", table.catalog_fingerprint_,
        table.rank_fingerprint_, table.groups_fingerprint_,
        payload_of(table.catalog_fingerprint_, table.rank_fingerprint_, table.groups_fingerprint_,
                   table.values_));
    return Built::success(std::move(table));
  } catch (const std::bad_alloc &) {
    return Built::failure(ResourceError::MemoryFailure);
  }
}

Result<bool, ResourceError> RiverFeatureTable::save(const std::filesystem::path &path) const {
  return detail::write_resource(
      path, river_kind, format_version, fingerprint_,
      payload_of(catalog_fingerprint_, rank_fingerprint_, groups_fingerprint_, values_));
}

Result<RiverFeatureTable, ResourceError>
RiverFeatureTable::load(const std::filesystem::path &path) {
  using Loaded = Result<RiverFeatureTable, ResourceError>;
  const auto resource = detail::read_resource(path, river_kind, format_version);
  if (!resource) {
    return Loaded::failure(resource.error());
  }
  RiverFeatureTable table;
  if (!parse_payload(std::span<const std::uint8_t>(resource.value().bytes),
                     table.catalog_fingerprint_, table.rank_fingerprint_,
                     table.groups_fingerprint_, table.values_,
                     static_cast<std::uint64_t>(canonical_river_board_count) * combo_count *
                         river_feature_count)) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  table.fingerprint_ = table_fingerprint(
      "gtosd.card_abstraction.river_group_equities.v1", table.catalog_fingerprint_,
      table.rank_fingerprint_, table.groups_fingerprint_,
      std::span<const std::uint8_t>(resource.value().bytes));
  if (table.fingerprint_ != resource.value().fingerprint) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  return Loaded::success(std::move(table));
}

} // namespace gtosd::card_abstraction
