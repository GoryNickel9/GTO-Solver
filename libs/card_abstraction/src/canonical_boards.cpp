#include "gtosd/card_abstraction/canonical_boards.hpp"

#include "gtosd/card_abstraction/combinatorics.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string_view>
#include <utility>

namespace gtosd::card_abstraction {
namespace {

constexpr std::uint32_t card_bits = 6U;
constexpr std::uint32_t card_mask = (1U << card_bits) - 1U;
constexpr std::uint64_t fnv_offset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
constexpr std::string_view catalog_magic = "GTOSDCAT";
constexpr std::string_view fingerprint_domain = "gtosd.card_abstraction.board_catalog.v1";

using Clock = std::chrono::steady_clock;

double seconds_since(const Clock::time_point started) {
  return std::chrono::duration<double>(Clock::now() - started).count();
}

std::array<SuitPermutation, suit_permutation_count> make_permutations() noexcept {
  std::array<SuitPermutation, suit_permutation_count> permutations{};
  SuitPermutation current{0U, 1U, 2U, 3U};
  std::size_t index = 0U;
  do {
    permutations[index++] = current;
  } while (std::next_permutation(current.begin(), current.end()));
  return permutations;
}

const std::array<SuitPermutation, suit_permutation_count> permutations_table = make_permutations();

constexpr std::uint8_t permute_value(const std::uint8_t card,
                                     const SuitPermutation &permutation) noexcept {
  return static_cast<std::uint8_t>((card / 4U) * 4U + permutation[card % 4U]);
}

constexpr void sort3(std::array<std::uint8_t, 3> &values) noexcept {
  if (values[0] > values[1]) {
    std::swap(values[0], values[1]);
  }
  if (values[1] > values[2]) {
    std::swap(values[1], values[2]);
  }
  if (values[0] > values[1]) {
    std::swap(values[0], values[1]);
  }
}

constexpr void sort5(std::array<std::uint8_t, 5> &values) noexcept {
  for (std::size_t i = 1U; i < values.size(); ++i) {
    const auto value = values[i];
    std::size_t j = i;
    while (j > 0U && values[j - 1U] > value) {
      values[j] = values[j - 1U];
      --j;
    }
    values[j] = value;
  }
}

constexpr std::uint32_t pack_flop(const std::array<std::uint8_t, 3> &sorted) noexcept {
  return (static_cast<std::uint32_t>(sorted[0]) << (2U * card_bits)) |
         (static_cast<std::uint32_t>(sorted[1]) << card_bits) | sorted[2];
}

constexpr std::uint32_t pack_river(const std::array<std::uint8_t, 5> &sorted) noexcept {
  std::uint32_t code = 0U;
  for (const auto value : sorted) {
    code = (code << card_bits) | value;
  }
  return code;
}

constexpr std::uint32_t flop_code_image(const std::array<std::uint8_t, 3> &flop,
                                        const SuitPermutation &permutation) noexcept {
  std::array<std::uint8_t, 3> image{permute_value(flop[0], permutation),
                                    permute_value(flop[1], permutation),
                                    permute_value(flop[2], permutation)};
  sort3(image);
  return pack_flop(image);
}

constexpr std::uint32_t flop_turn_code_image(const std::array<std::uint8_t, 3> &flop,
                                             const std::uint8_t turn,
                                             const SuitPermutation &permutation) noexcept {
  return (flop_code_image(flop, permutation) << card_bits) | permute_value(turn, permutation);
}

constexpr std::uint32_t history_code_image(const std::array<std::uint8_t, 3> &flop,
                                           const std::uint8_t turn, const std::uint8_t river,
                                           const SuitPermutation &permutation) noexcept {
  return (flop_turn_code_image(flop, turn, permutation) << card_bits) |
         permute_value(river, permutation);
}

constexpr std::uint32_t river_code_image(const std::array<std::uint8_t, 5> &cards,
                                         const SuitPermutation &permutation) noexcept {
  std::array<std::uint8_t, 5> image{};
  for (std::size_t index = 0U; index < cards.size(); ++index) {
    image[index] = permute_value(cards[index], permutation);
  }
  sort5(image);
  return pack_river(image);
}

// Minimum code over the 24 permutations, the first permutation reaching it and
// the number of distinct images.
template <typename CodeOf> Canonicalization minimize(CodeOf &&code_of) noexcept {
  std::array<std::uint32_t, suit_permutation_count> images{};
  Canonicalization result;
  result.code = ~std::uint32_t{0};
  for (std::size_t index = 0U; index < suit_permutation_count; ++index) {
    const auto code = code_of(permutations_table[index]);
    images[index] = code;
    if (code < result.code) {
      result.code = code;
      result.permutation = permutations_table[index];
    }
  }
  std::sort(images.begin(), images.end());
  result.orbit_size = static_cast<std::uint32_t>(
      std::distance(images.begin(), std::unique(images.begin(), images.end())));
  return result;
}

bool distinct_cards(const std::uint8_t *values, const std::size_t count) noexcept {
  std::uint64_t mask = 0U;
  for (std::size_t index = 0U; index < count; ++index) {
    if (values[index] >= deck_cards) {
      return false;
    }
    const auto bit = std::uint64_t{1} << values[index];
    if ((mask & bit) != 0U) {
      return false;
    }
    mask |= bit;
  }
  return true;
}

std::array<std::uint8_t, 3> flop_values(const std::array<CardId, 3> &flop) noexcept {
  std::array<std::uint8_t, 3> values{flop[0].value(), flop[1].value(), flop[2].value()};
  sort3(values);
  return values;
}

CardId card_from_value(const std::uint8_t value) noexcept {
  return CardId::from_parts(static_cast<Rank>(value / 4U), static_cast<Suit>(value % 4U));
}

std::array<CardId, 3> unpack_flop(const std::uint32_t code) noexcept {
  return {card_from_value(static_cast<std::uint8_t>((code >> (2U * card_bits)) & card_mask)),
          card_from_value(static_cast<std::uint8_t>((code >> card_bits) & card_mask)),
          card_from_value(static_cast<std::uint8_t>(code & card_mask))};
}

// Sorts codes and collapses equal runs into (code, multiplicity) pairs.
template <typename Emit> void collapse_codes(std::vector<std::uint32_t> &codes, Emit &&emit) {
  std::sort(codes.begin(), codes.end());
  std::size_t begin = 0U;
  while (begin < codes.size()) {
    std::size_t end = begin + 1U;
    while (end < codes.size() && codes[end] == codes[begin]) {
      ++end;
    }
    emit(codes[begin], static_cast<std::uint32_t>(end - begin));
    begin = end;
  }
}

Result<CanonicalLookup, CardError> lookup_code(const std::vector<std::uint32_t> &codes,
                                                const Canonicalization &canonical) noexcept {
  const auto found = std::lower_bound(codes.begin(), codes.end(), canonical.code);
  if (found == codes.end() || *found != canonical.code) {
    return Result<CanonicalLookup, CardError>::failure(CardError::InvalidCard);
  }
  return Result<CanonicalLookup, CardError>::success(
      CanonicalLookup{static_cast<std::uint32_t>(std::distance(codes.begin(), found)),
                      canonical.permutation});
}

void mix_bytes(std::uint64_t &hash, const void *data, const std::size_t size) noexcept {
  const auto *bytes = static_cast<const std::uint8_t *>(data);
  for (std::size_t index = 0U; index < size; ++index) {
    hash ^= bytes[index];
    hash *= fnv_prime;
  }
}

template <typename Unsigned> void mix_unsigned(std::uint64_t &hash, Unsigned value) noexcept {
  for (std::size_t byte = 0U; byte < sizeof(Unsigned); ++byte) {
    hash ^= static_cast<std::uint8_t>(value & 0xFFU);
    hash *= fnv_prime;
    value >>= 8U;
  }
}

std::string hex64(std::uint64_t value) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result(16U, '0');
  for (std::size_t index = 0; index < result.size(); ++index) {
    result[result.size() - 1U - index] = digits[static_cast<std::size_t>(value & 0xFU)];
    value >>= 4U;
  }
  return result;
}

class ByteWriter {
public:
  template <typename Unsigned> void put(Unsigned value) {
    for (std::size_t byte = 0U; byte < sizeof(Unsigned); ++byte) {
      bytes_.push_back(static_cast<std::uint8_t>(value & 0xFFU));
      value >>= 8U;
    }
  }
  void put_byte(const std::uint8_t value) { bytes_.push_back(value); }
  [[nodiscard]] std::vector<std::uint8_t> &bytes() noexcept { return bytes_; }

private:
  std::vector<std::uint8_t> bytes_;
};

class ByteReader {
public:
  explicit ByteReader(const std::vector<std::uint8_t> &bytes) noexcept : bytes_(bytes) {}
  template <typename Unsigned> bool get(Unsigned &value) noexcept {
    if (position_ + sizeof(Unsigned) > bytes_.size()) {
      return false;
    }
    value = 0U;
    for (std::size_t byte = 0U; byte < sizeof(Unsigned); ++byte) {
      value |= static_cast<Unsigned>(static_cast<Unsigned>(bytes_[position_ + byte])
                                     << (8U * byte));
    }
    position_ += sizeof(Unsigned);
    return true;
  }
  bool skip(const std::size_t count) noexcept {
    if (position_ + count > bytes_.size()) {
      return false;
    }
    position_ += count;
    return true;
  }
  [[nodiscard]] std::size_t position() const noexcept { return position_; }

private:
  const std::vector<std::uint8_t> &bytes_;
  std::size_t position_{0U};
};

} // namespace

const std::array<SuitPermutation, suit_permutation_count> &all_suit_permutations() noexcept {
  return permutations_table;
}

SuitPermutation inverse_permutation(const SuitPermutation &permutation) noexcept {
  SuitPermutation inverse{};
  for (std::size_t suit = 0U; suit < inverse.size(); ++suit) {
    inverse[permutation[suit]] = static_cast<std::uint8_t>(suit);
  }
  return inverse;
}

Result<Canonicalization, CardError> canonicalize_flop(const std::array<CardId, 3> &flop) {
  const auto values = flop_values(flop);
  if (!distinct_cards(values.data(), values.size())) {
    return Result<Canonicalization, CardError>::failure(CardError::DuplicateCard);
  }
  return Result<Canonicalization, CardError>::success(
      minimize([&](const SuitPermutation &p) { return flop_code_image(values, p); }));
}

Result<Canonicalization, CardError> canonicalize_flop_turn(const std::array<CardId, 3> &flop,
                                                           const CardId turn) {
  const auto values = flop_values(flop);
  const std::array<std::uint8_t, 4> all{values[0], values[1], values[2], turn.value()};
  if (!distinct_cards(all.data(), all.size())) {
    return Result<Canonicalization, CardError>::failure(CardError::DuplicateCard);
  }
  return Result<Canonicalization, CardError>::success(minimize(
      [&](const SuitPermutation &p) { return flop_turn_code_image(values, turn.value(), p); }));
}

Result<Canonicalization, CardError> canonicalize_river_board(const std::array<CardId, 5> &cards) {
  std::array<std::uint8_t, 5> values{};
  for (std::size_t index = 0U; index < cards.size(); ++index) {
    values[index] = cards[index].value();
  }
  if (!distinct_cards(values.data(), values.size())) {
    return Result<Canonicalization, CardError>::failure(CardError::DuplicateCard);
  }
  sort5(values);
  return Result<Canonicalization, CardError>::success(
      minimize([&](const SuitPermutation &p) { return river_code_image(values, p); }));
}

Result<Canonicalization, CardError> canonicalize_history(const BoardHistory &history) {
  const auto values = flop_values(history.flop);
  const std::array<std::uint8_t, 5> all{values[0], values[1], values[2], history.turn.value(),
                                        history.river.value()};
  if (!distinct_cards(all.data(), all.size())) {
    return Result<Canonicalization, CardError>::failure(CardError::DuplicateCard);
  }
  return Result<Canonicalization, CardError>::success(minimize([&](const SuitPermutation &p) {
    return history_code_image(values, history.turn.value(), history.river.value(), p);
  }));
}

BoardCatalog BoardCatalog::build(CatalogBuildTelemetry *telemetry) {
  BoardCatalog catalog;
  CatalogBuildTelemetry local_telemetry;

  auto started = Clock::now();
  {
    std::vector<std::uint32_t> codes;
    codes.reserve(physical_flops);
    for (std::uint8_t a = 0U; a < deck_cards; ++a) {
      for (std::uint8_t b = static_cast<std::uint8_t>(a + 1U); b < deck_cards; ++b) {
        for (std::uint8_t c = static_cast<std::uint8_t>(b + 1U); c < deck_cards; ++c) {
          const std::array<std::uint8_t, 3> flop{a, b, c};
          codes.push_back(
              minimize([&](const SuitPermutation &p) { return flop_code_image(flop, p); }).code);
        }
      }
    }
    collapse_codes(codes, [&](const std::uint32_t code, const std::uint32_t multiplicity) {
      catalog.flops_.push_back(CanonicalFlop{unpack_flop(code), multiplicity, code});
      catalog.flop_codes_.push_back(code);
    });
  }
  local_telemetry.flop_seconds = seconds_since(started);

  started = Clock::now();
  {
    std::vector<std::uint32_t> codes;
    codes.reserve(physical_flop_turns);
    for (std::uint8_t a = 0U; a < deck_cards; ++a) {
      for (std::uint8_t b = static_cast<std::uint8_t>(a + 1U); b < deck_cards; ++b) {
        for (std::uint8_t c = static_cast<std::uint8_t>(b + 1U); c < deck_cards; ++c) {
          const std::array<std::uint8_t, 3> flop{a, b, c};
          for (std::uint8_t turn = 0U; turn < deck_cards; ++turn) {
            if (turn == a || turn == b || turn == c) {
              continue;
            }
            codes.push_back(minimize([&](const SuitPermutation &p) {
                              return flop_turn_code_image(flop, turn, p);
                            }).code);
          }
        }
      }
    }
    collapse_codes(codes, [&](const std::uint32_t code, const std::uint32_t multiplicity) {
      CanonicalFlopTurn entry;
      entry.flop = unpack_flop(code >> card_bits);
      entry.turn = card_from_value(static_cast<std::uint8_t>(code & card_mask));
      entry.multiplicity = multiplicity;
      entry.code = code;
      catalog.flop_turns_.push_back(entry);
      catalog.flop_turn_codes_.push_back(code);
    });
  }
  local_telemetry.flop_turn_seconds = seconds_since(started);

  started = Clock::now();
  {
    std::vector<std::uint32_t> codes;
    codes.reserve(physical_river_boards);
    std::array<std::uint8_t, 5> cards{};
    for (cards[0] = 0U; cards[0] < deck_cards; ++cards[0]) {
      for (cards[1] = static_cast<std::uint8_t>(cards[0] + 1U); cards[1] < deck_cards;
           ++cards[1]) {
        for (cards[2] = static_cast<std::uint8_t>(cards[1] + 1U); cards[2] < deck_cards;
             ++cards[2]) {
          for (cards[3] = static_cast<std::uint8_t>(cards[2] + 1U); cards[3] < deck_cards;
               ++cards[3]) {
            for (cards[4] = static_cast<std::uint8_t>(cards[3] + 1U); cards[4] < deck_cards;
                 ++cards[4]) {
              codes.push_back(
                  minimize([&](const SuitPermutation &p) { return river_code_image(cards, p); })
                      .code);
            }
          }
        }
      }
    }
    collapse_codes(codes, [&](const std::uint32_t code, const std::uint32_t multiplicity) {
      CanonicalRiverBoard entry;
      for (std::size_t index = 0U; index < entry.cards.size(); ++index) {
        const auto shift = card_bits * static_cast<std::uint32_t>(entry.cards.size() - 1U - index);
        entry.cards[index] = card_from_value(static_cast<std::uint8_t>((code >> shift) & card_mask));
      }
      entry.multiplicity = multiplicity;
      entry.code = code;
      catalog.river_boards_.push_back(entry);
      catalog.river_board_codes_.push_back(code);
    });
  }
  local_telemetry.river_board_seconds = seconds_since(started);

  started = Clock::now();
  {
    std::vector<std::uint32_t> codes;
    codes.reserve(physical_board_histories);
    for (std::uint8_t a = 0U; a < deck_cards; ++a) {
      for (std::uint8_t b = static_cast<std::uint8_t>(a + 1U); b < deck_cards; ++b) {
        for (std::uint8_t c = static_cast<std::uint8_t>(b + 1U); c < deck_cards; ++c) {
          const std::array<std::uint8_t, 3> flop{a, b, c};
          const std::uint64_t flop_mask = (std::uint64_t{1} << a) | (std::uint64_t{1} << b) |
                                          (std::uint64_t{1} << c);
          for (std::uint8_t turn = 0U; turn < deck_cards; ++turn) {
            if ((flop_mask & (std::uint64_t{1} << turn)) != 0U) {
              continue;
            }
            const std::uint64_t turn_mask = flop_mask | (std::uint64_t{1} << turn);
            for (std::uint8_t river = 0U; river < deck_cards; ++river) {
              if ((turn_mask & (std::uint64_t{1} << river)) != 0U) {
                continue;
              }
              codes.push_back(minimize([&](const SuitPermutation &p) {
                                return history_code_image(flop, turn, river, p);
                              }).code);
            }
          }
        }
      }
    }
    collapse_codes(codes, [&](const std::uint32_t code, const std::uint32_t multiplicity) {
      CanonicalBoardHistory entry;
      entry.history.flop = unpack_flop(code >> (2U * card_bits));
      entry.history.turn = card_from_value(static_cast<std::uint8_t>((code >> card_bits) & card_mask));
      entry.history.river = card_from_value(static_cast<std::uint8_t>(code & card_mask));
      entry.multiplicity = multiplicity;
      entry.code = code;
      catalog.histories_.push_back(entry);
      catalog.history_codes_.push_back(code);
    });
  }
  local_telemetry.history_seconds = seconds_since(started);

  started = Clock::now();
  for (auto &entry : catalog.flop_turns_) {
    entry.flop_index = catalog.lookup_flop(entry.flop).value().index;
  }
  for (auto &entry : catalog.histories_) {
    entry.flop_index = catalog.lookup_flop(entry.history.flop).value().index;
    entry.flop_turn_index =
        catalog.lookup_flop_turn(entry.history.flop, entry.history.turn).value().index;
    const std::array<CardId, 5> all{entry.history.flop[0], entry.history.flop[1],
                                    entry.history.flop[2], entry.history.turn,
                                    entry.history.river};
    entry.river_board_index = catalog.lookup_river_board(all).value().index;
  }
  catalog.finalize();
  local_telemetry.cross_reference_seconds = seconds_since(started);

  if (telemetry != nullptr) {
    *telemetry = local_telemetry;
  }
  return catalog;
}

void BoardCatalog::finalize() {
  history_cumulative_multiplicity_.clear();
  history_cumulative_multiplicity_.reserve(histories_.size());
  std::uint32_t total = 0U;
  for (const auto &entry : histories_) {
    total += entry.multiplicity;
    history_cumulative_multiplicity_.push_back(total);
  }

  auto hash = fnv_offset;
  mix_bytes(hash, fingerprint_domain.data(), fingerprint_domain.size());
  mix_unsigned(hash, static_cast<std::uint32_t>(flops_.size()));
  for (const auto &entry : flops_) {
    mix_unsigned(hash, entry.code);
    mix_unsigned(hash, entry.multiplicity);
  }
  mix_unsigned(hash, static_cast<std::uint32_t>(flop_turns_.size()));
  for (const auto &entry : flop_turns_) {
    mix_unsigned(hash, entry.code);
    mix_unsigned(hash, entry.multiplicity);
    mix_unsigned(hash, entry.flop_index);
  }
  mix_unsigned(hash, static_cast<std::uint32_t>(river_boards_.size()));
  for (const auto &entry : river_boards_) {
    mix_unsigned(hash, entry.code);
    mix_unsigned(hash, entry.multiplicity);
  }
  mix_unsigned(hash, static_cast<std::uint32_t>(histories_.size()));
  for (const auto &entry : histories_) {
    mix_unsigned(hash, entry.code);
    mix_unsigned(hash, entry.multiplicity);
    mix_unsigned(hash, entry.flop_index);
    mix_unsigned(hash, entry.flop_turn_index);
    mix_unsigned(hash, entry.river_board_index);
  }
  fingerprint_ = "fnv1a64:" + hex64(hash);
}

Result<CanonicalLookup, CardError>
BoardCatalog::lookup_flop(const std::array<CardId, 3> &flop) const {
  const auto canonical = canonicalize_flop(flop);
  if (!canonical) {
    return Result<CanonicalLookup, CardError>::failure(canonical.error());
  }
  return lookup_code(flop_codes_, canonical.value());
}

Result<CanonicalLookup, CardError>
BoardCatalog::lookup_flop_turn(const std::array<CardId, 3> &flop, const CardId turn) const {
  const auto canonical = canonicalize_flop_turn(flop, turn);
  if (!canonical) {
    return Result<CanonicalLookup, CardError>::failure(canonical.error());
  }
  return lookup_code(flop_turn_codes_, canonical.value());
}

Result<CanonicalLookup, CardError>
BoardCatalog::lookup_river_board(const std::array<CardId, 5> &cards) const {
  const auto canonical = canonicalize_river_board(cards);
  if (!canonical) {
    return Result<CanonicalLookup, CardError>::failure(canonical.error());
  }
  return lookup_code(river_board_codes_, canonical.value());
}

Result<CanonicalLookup, CardError>
BoardCatalog::lookup_history(const BoardHistory &history) const {
  const auto canonical = canonicalize_history(history);
  if (!canonical) {
    return Result<CanonicalLookup, CardError>::failure(canonical.error());
  }
  return lookup_code(history_codes_, canonical.value());
}

BoardHistory BoardCatalog::sample_physical_history(DeterministicRandom &random) const {
  std::array<std::uint8_t, deck_cards> deck{};
  for (std::uint8_t index = 0U; index < deck_cards; ++index) {
    deck[index] = index;
  }
  for (std::uint32_t drawn = 0U; drawn < 5U; ++drawn) {
    const auto offset = random.uniform_below(deck_cards - drawn);
    std::swap(deck[drawn], deck[drawn + offset]);
  }
  std::array<std::uint8_t, 3> flop{deck[0], deck[1], deck[2]};
  sort3(flop);
  BoardHistory history;
  history.flop = {card_from_value(flop[0]), card_from_value(flop[1]), card_from_value(flop[2])};
  history.turn = card_from_value(deck[3]);
  history.river = card_from_value(deck[4]);
  return history;
}

std::uint32_t BoardCatalog::history_index_from_quantile(const std::uint32_t quantile) const noexcept {
  const auto found = std::upper_bound(history_cumulative_multiplicity_.begin(),
                                      history_cumulative_multiplicity_.end(), quantile);
  const auto index = static_cast<std::size_t>(
      std::distance(history_cumulative_multiplicity_.begin(), found));
  return static_cast<std::uint32_t>(
      index < histories_.size() ? index : (histories_.empty() ? 0U : histories_.size() - 1U));
}

std::uint32_t BoardCatalog::sample_history_index(DeterministicRandom &random) const {
  return history_index_from_quantile(random.uniform_below(physical_board_histories));
}

std::uint64_t BoardCatalog::byte_size() const noexcept {
  return flops_.size() * sizeof(CanonicalFlop) + flop_turns_.size() * sizeof(CanonicalFlopTurn) +
         river_boards_.size() * sizeof(CanonicalRiverBoard) +
         histories_.size() * sizeof(CanonicalBoardHistory) +
         (flop_codes_.size() + flop_turn_codes_.size() + river_board_codes_.size() +
          history_codes_.size() + history_cumulative_multiplicity_.size()) *
             sizeof(std::uint32_t);
}

Result<bool, CatalogError> BoardCatalog::save(const std::filesystem::path &path) const {
  ByteWriter writer;
  for (const auto character : catalog_magic) {
    writer.put_byte(static_cast<std::uint8_t>(character));
  }
  writer.put(format_version);
  writer.put(static_cast<std::uint32_t>(flops_.size()));
  writer.put(static_cast<std::uint32_t>(flop_turns_.size()));
  writer.put(static_cast<std::uint32_t>(river_boards_.size()));
  writer.put(static_cast<std::uint32_t>(histories_.size()));
  for (const auto &entry : flops_) {
    writer.put(entry.code);
    writer.put(entry.multiplicity);
  }
  for (const auto &entry : flop_turns_) {
    writer.put(entry.code);
    writer.put(entry.multiplicity);
    writer.put(entry.flop_index);
  }
  for (const auto &entry : river_boards_) {
    writer.put(entry.code);
    writer.put(entry.multiplicity);
  }
  for (const auto &entry : histories_) {
    writer.put(entry.code);
    writer.put(entry.multiplicity);
    writer.put(entry.flop_index);
    writer.put(entry.flop_turn_index);
    writer.put(entry.river_board_index);
  }
  auto checksum = fnv_offset;
  mix_bytes(checksum, writer.bytes().data(), writer.bytes().size());
  writer.put(checksum);

  const auto temporary = path.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      return Result<bool, CatalogError>::failure(CatalogError::IoFailure);
    }
    output.write(reinterpret_cast<const char *>(writer.bytes().data()),
                 static_cast<std::streamsize>(writer.bytes().size()));
    if (!output) {
      return Result<bool, CatalogError>::failure(CatalogError::IoFailure);
    }
  }
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return Result<bool, CatalogError>::failure(CatalogError::IoFailure);
  }
  return Result<bool, CatalogError>::success(true);
}

Result<BoardCatalog, CatalogError> BoardCatalog::load(const std::filesystem::path &path) {
  using Loaded = Result<BoardCatalog, CatalogError>;
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Loaded::failure(CatalogError::IoFailure);
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                  std::istreambuf_iterator<char>());
  if (bytes.size() < catalog_magic.size() + sizeof(std::uint32_t) * 5U + sizeof(std::uint64_t)) {
    return Loaded::failure(CatalogError::IntegrityFailure);
  }
  const auto payload_size = bytes.size() - sizeof(std::uint64_t);
  auto checksum = fnv_offset;
  mix_bytes(checksum, bytes.data(), payload_size);
  std::uint64_t stored_checksum = 0U;
  for (std::size_t byte = 0U; byte < sizeof(std::uint64_t); ++byte) {
    stored_checksum |= static_cast<std::uint64_t>(bytes[payload_size + byte]) << (8U * byte);
  }
  if (stored_checksum != checksum) {
    return Loaded::failure(CatalogError::IntegrityFailure);
  }
  for (std::size_t index = 0U; index < catalog_magic.size(); ++index) {
    if (bytes[index] != static_cast<std::uint8_t>(catalog_magic[index])) {
      return Loaded::failure(CatalogError::IntegrityFailure);
    }
  }
  ByteReader reader(bytes);
  std::uint32_t version = 0U;
  std::uint32_t flop_count = 0U;
  std::uint32_t flop_turn_count = 0U;
  std::uint32_t river_count = 0U;
  std::uint32_t history_count = 0U;
  if (!reader.skip(catalog_magic.size()) || !reader.get(version) || !reader.get(flop_count) ||
      !reader.get(flop_turn_count) || !reader.get(river_count) || !reader.get(history_count)) {
    return Loaded::failure(CatalogError::IntegrityFailure);
  }
  if (version != format_version) {
    return Loaded::failure(CatalogError::UnsupportedVersion);
  }
  if (flop_count != canonical_flop_count || flop_turn_count != canonical_flop_turn_count ||
      river_count != canonical_river_board_count ||
      history_count != canonical_board_history_count) {
    return Loaded::failure(CatalogError::IntegrityFailure);
  }

  BoardCatalog catalog;
  catalog.flops_.reserve(flop_count);
  catalog.flop_turns_.reserve(flop_turn_count);
  catalog.river_boards_.reserve(river_count);
  catalog.histories_.reserve(history_count);
  for (std::uint32_t index = 0U; index < flop_count; ++index) {
    CanonicalFlop entry;
    if (!reader.get(entry.code) || !reader.get(entry.multiplicity)) {
      return Loaded::failure(CatalogError::IntegrityFailure);
    }
    entry.cards = unpack_flop(entry.code);
    catalog.flops_.push_back(entry);
    catalog.flop_codes_.push_back(entry.code);
  }
  for (std::uint32_t index = 0U; index < flop_turn_count; ++index) {
    CanonicalFlopTurn entry;
    if (!reader.get(entry.code) || !reader.get(entry.multiplicity) ||
        !reader.get(entry.flop_index) || entry.flop_index >= flop_count) {
      return Loaded::failure(CatalogError::IntegrityFailure);
    }
    entry.flop = unpack_flop(entry.code >> card_bits);
    entry.turn = card_from_value(static_cast<std::uint8_t>(entry.code & card_mask));
    catalog.flop_turns_.push_back(entry);
    catalog.flop_turn_codes_.push_back(entry.code);
  }
  for (std::uint32_t index = 0U; index < river_count; ++index) {
    CanonicalRiverBoard entry;
    if (!reader.get(entry.code) || !reader.get(entry.multiplicity)) {
      return Loaded::failure(CatalogError::IntegrityFailure);
    }
    for (std::size_t card = 0U; card < entry.cards.size(); ++card) {
      const auto shift = card_bits * static_cast<std::uint32_t>(entry.cards.size() - 1U - card);
      entry.cards[card] =
          card_from_value(static_cast<std::uint8_t>((entry.code >> shift) & card_mask));
    }
    catalog.river_boards_.push_back(entry);
    catalog.river_board_codes_.push_back(entry.code);
  }
  for (std::uint32_t index = 0U; index < history_count; ++index) {
    CanonicalBoardHistory entry;
    if (!reader.get(entry.code) || !reader.get(entry.multiplicity) ||
        !reader.get(entry.flop_index) || !reader.get(entry.flop_turn_index) ||
        !reader.get(entry.river_board_index) || entry.flop_index >= flop_count ||
        entry.flop_turn_index >= flop_turn_count || entry.river_board_index >= river_count) {
      return Loaded::failure(CatalogError::IntegrityFailure);
    }
    entry.history.flop = unpack_flop(entry.code >> (2U * card_bits));
    entry.history.turn =
        card_from_value(static_cast<std::uint8_t>((entry.code >> card_bits) & card_mask));
    entry.history.river = card_from_value(static_cast<std::uint8_t>(entry.code & card_mask));
    catalog.histories_.push_back(entry);
    catalog.history_codes_.push_back(entry.code);
  }
  if (reader.position() != payload_size) {
    return Loaded::failure(CatalogError::IntegrityFailure);
  }
  for (const auto *codes : {&catalog.flop_codes_, &catalog.flop_turn_codes_,
                            &catalog.river_board_codes_, &catalog.history_codes_}) {
    if (!std::is_sorted(codes->begin(), codes->end()) ||
        std::adjacent_find(codes->begin(), codes->end()) != codes->end()) {
      return Loaded::failure(CatalogError::IntegrityFailure);
    }
  }
  catalog.finalize();
  return Loaded::success(std::move(catalog));
}

bool operator==(const BoardCatalog &left, const BoardCatalog &right) {
  if (left.fingerprint_ != right.fingerprint_ || left.flops_.size() != right.flops_.size() ||
      left.flop_turns_.size() != right.flop_turns_.size() ||
      left.river_boards_.size() != right.river_boards_.size() ||
      left.histories_.size() != right.histories_.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < left.flops_.size(); ++index) {
    if (left.flops_[index].code != right.flops_[index].code ||
        left.flops_[index].multiplicity != right.flops_[index].multiplicity ||
        left.flops_[index].cards != right.flops_[index].cards) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < left.histories_.size(); ++index) {
    const auto &a = left.histories_[index];
    const auto &b = right.histories_[index];
    if (a.code != b.code || a.multiplicity != b.multiplicity || a.flop_index != b.flop_index ||
        a.flop_turn_index != b.flop_turn_index || a.river_board_index != b.river_board_index ||
        !(a.history == b.history)) {
      return false;
    }
  }
  return true;
}

const char *catalog_error_name(const CatalogError error) noexcept {
  switch (error) {
  case CatalogError::IoFailure:
    return "io_failure";
  case CatalogError::IntegrityFailure:
    return "integrity_failure";
  case CatalogError::UnsupportedVersion:
    return "unsupported_version";
  }
  return "unknown";
}

} // namespace gtosd::card_abstraction
