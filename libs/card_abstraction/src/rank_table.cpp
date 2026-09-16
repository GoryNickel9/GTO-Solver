#include "gtosd/card_abstraction/rank_table.hpp"

#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/equity/evaluator.hpp"
#include "gtosd/equity/seven_card_table.hpp"
#include "resource_file.hpp"

#include <algorithm>
#include <new>
#include <string_view>

namespace gtosd::card_abstraction {
namespace {

constexpr std::string_view resource_kind = "rank_table_ordinal";

// Order-preserving packing of a HandValue: category above the five kickers.
constexpr std::uint32_t order_key(const HandValue &value) noexcept {
  std::uint32_t key = static_cast<std::uint32_t>(value.category) << 20U;
  for (std::size_t index = 0U; index < value.kickers.size(); ++index) {
    key |= static_cast<std::uint32_t>(value.kickers[index]) << (16U - 4U * index);
  }
  return key;
}

constexpr std::uint64_t colex5(const std::uint8_t a, const std::uint8_t b, const std::uint8_t c,
                               const std::uint8_t d, const std::uint8_t e) noexcept {
  return binomial(a, 1U) + binomial(b, 2U) + binomial(c, 3U) + binomial(d, 4U) + binomial(e, 5U);
}

constexpr std::uint64_t colex7(const std::array<std::uint8_t, 7> &s) noexcept {
  return binomial(s[0], 1U) + binomial(s[1], 2U) + binomial(s[2], 3U) + binomial(s[3], 4U) +
         binomial(s[4], 5U) + binomial(s[5], 6U) + binomial(s[6], 7U);
}

std::string make_fingerprint(const std::uint64_t checksum, const std::uint16_t distinct) {
  auto hash = detail::fnv1a_text("gtosd.card_abstraction.rank_table_ordinal.v1|");
  hash = detail::fnv1a_text(seven_card_table_ruleset_fingerprint, hash);
  hash = detail::fnv1a_text("|", hash);
  hash = detail::fnv1a_text(std::to_string(distinct), hash);
  hash = detail::fnv1a_text("|", hash);
  hash = detail::fnv1a_text(detail::hex64(checksum), hash);
  return "fnv1a64:" + detail::hex64(hash);
}

} // namespace

Result<RankTable, ResourceError> RankTable::build() {
  using Built = Result<RankTable, ResourceError>;
  try {
    RankTable table;
    std::vector<std::uint32_t> five_keys(static_cast<std::size_t>(five_card_set_count), 0U);
    const auto deck = short_deck();
    for (std::uint8_t a = 0U; a < 32U; ++a) {
      for (std::uint8_t b = static_cast<std::uint8_t>(a + 1U); b < 33U; ++b) {
        for (std::uint8_t c = static_cast<std::uint8_t>(b + 1U); c < 34U; ++c) {
          for (std::uint8_t d = static_cast<std::uint8_t>(c + 1U); d < 35U; ++d) {
            for (std::uint8_t e = static_cast<std::uint8_t>(d + 1U); e < 36U; ++e) {
              const std::array<CardId, 5> cards{deck[a], deck[b], deck[c], deck[d], deck[e]};
              const auto value = evaluate_five(cards);
              if (!value) {
                return Built::failure(ResourceError::InvalidInput);
              }
              five_keys[static_cast<std::size_t>(colex5(a, b, c, d, e))] = order_key(value.value());
            }
          }
        }
      }
    }
    std::vector<std::uint32_t> distinct(five_keys);
    std::sort(distinct.begin(), distinct.end());
    distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
    if (distinct.size() >= 65'535U) {
      return Built::failure(ResourceError::IntegrityFailure);
    }
    table.distinct_ = static_cast<std::uint16_t>(distinct.size());
    table.five_.resize(five_keys.size());
    for (std::size_t index = 0U; index < five_keys.size(); ++index) {
      const auto found = std::lower_bound(distinct.begin(), distinct.end(), five_keys[index]);
      table.five_[index] = static_cast<std::uint16_t>(std::distance(distinct.begin(), found));
    }

    table.seven_.assign(static_cast<std::size_t>(seven_card_set_count), 0U);
    std::array<std::uint8_t, 7> s{};
    for (s[0] = 0U; s[0] < 30U; ++s[0]) {
      for (s[1] = static_cast<std::uint8_t>(s[0] + 1U); s[1] < 31U; ++s[1]) {
        for (s[2] = static_cast<std::uint8_t>(s[1] + 1U); s[2] < 32U; ++s[2]) {
          for (s[3] = static_cast<std::uint8_t>(s[2] + 1U); s[3] < 33U; ++s[3]) {
            for (s[4] = static_cast<std::uint8_t>(s[3] + 1U); s[4] < 34U; ++s[4]) {
              for (s[5] = static_cast<std::uint8_t>(s[4] + 1U); s[5] < 35U; ++s[5]) {
                for (s[6] = static_cast<std::uint8_t>(s[5] + 1U); s[6] < 36U; ++s[6]) {
                  std::uint16_t best = 0U;
                  for (std::size_t i = 0U; i < 7U; ++i) {
                    for (std::size_t j = i + 1U; j < 7U; ++j) {
                      std::array<std::uint8_t, 5> five{};
                      std::size_t output = 0U;
                      for (std::size_t k = 0U; k < 7U; ++k) {
                        if (k != i && k != j) {
                          five[output++] = s[k];
                        }
                      }
                      const auto rank = table.five_[static_cast<std::size_t>(
                          colex5(five[0], five[1], five[2], five[3], five[4]))];
                      best = std::max(best, rank);
                    }
                  }
                  table.seven_[static_cast<std::size_t>(colex7(s))] = best;
                }
              }
            }
          }
        }
      }
    }
    std::vector<std::uint8_t> payload;
    payload.reserve(table.payload_bytes() + 2U);
    detail::append_little(payload, table.distinct_);
    detail::append_vector(payload, table.five_);
    detail::append_vector(payload, table.seven_);
    table.checksum_ = detail::fnv1a(payload);
    table.fingerprint_ = make_fingerprint(table.checksum_, table.distinct_);
    return Built::success(std::move(table));
  } catch (const std::bad_alloc &) {
    return Built::failure(ResourceError::MemoryFailure);
  }
}

std::uint16_t RankTable::rank_of_sorted(const std::array<std::uint8_t, 7> &sorted) const {
  return seven_[static_cast<std::size_t>(colex7(sorted))];
}

std::uint16_t RankTable::rank_of(const std::array<std::uint8_t, 2> &hand,
                                 const std::array<std::uint8_t, 5> &board) const {
  std::array<std::uint8_t, 7> cards{hand[0], hand[1], board[0], board[1], board[2], board[3],
                                    board[4]};
  std::sort(cards.begin(), cards.end());
  return rank_of_sorted(cards);
}

Result<bool, ResourceError> RankTable::save(const std::filesystem::path &path) const {
  std::vector<std::uint8_t> payload;
  payload.reserve(payload_bytes() + 32U);
  detail::append_little(payload, distinct_);
  detail::append_vector(payload, five_);
  detail::append_vector(payload, seven_);
  if (detail::fnv1a(payload) != checksum_) {
    return Result<bool, ResourceError>::failure(ResourceError::IntegrityFailure);
  }
  return detail::write_resource(path, resource_kind, format_version, fingerprint_, payload);
}

Result<RankTable, ResourceError> RankTable::load(const std::filesystem::path &path) {
  using Loaded = Result<RankTable, ResourceError>;
  const auto resource = detail::read_resource(path, resource_kind, format_version);
  if (!resource) {
    return Loaded::failure(resource.error());
  }
  RankTable table;
  std::size_t position = 0U;
  const std::span<const std::uint8_t> bytes(resource.value().bytes);
  if (!detail::read_little(bytes, position, table.distinct_) ||
      !detail::read_vector(bytes, position, table.five_, five_card_set_count) ||
      !detail::read_vector(bytes, position, table.seven_, seven_card_set_count) ||
      position != bytes.size()) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  for (const auto rank : table.seven_) {
    if (rank >= table.distinct_) {
      return Loaded::failure(ResourceError::IntegrityFailure);
    }
  }
  table.checksum_ = detail::fnv1a(bytes);
  table.fingerprint_ = make_fingerprint(table.checksum_, table.distinct_);
  if (table.fingerprint_ != resource.value().fingerprint) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  return Loaded::success(std::move(table));
}

const char *resource_error_name(const ResourceError error) noexcept {
  switch (error) {
  case ResourceError::InvalidInput:
    return "invalid_input";
  case ResourceError::IoFailure:
    return "io_failure";
  case ResourceError::IntegrityFailure:
    return "integrity_failure";
  case ResourceError::UnsupportedVersion:
    return "unsupported_version";
  case ResourceError::MemoryFailure:
    return "memory_failure";
  }
  return "unknown";
}

} // namespace gtosd::card_abstraction
