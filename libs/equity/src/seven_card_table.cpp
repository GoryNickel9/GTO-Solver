#include "gtosd/equity/seven_card_table.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <new>
#include <system_error>
#include <type_traits>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace gtosd {
namespace {

constexpr std::array<char, 8> magic{'G', 'T', '7', 'T', 'A', 'B', '1', '\0'};
constexpr std::uint32_t format_major = 1U;
constexpr std::uint32_t format_minor = 0U;
constexpr std::uint64_t fnv_offset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;

constexpr std::uint64_t choose(const std::uint64_t n, const std::uint64_t k) noexcept {
  if (k > n) {
    return 0U;
  }
  std::uint64_t result = 1U;
  for (std::uint64_t index = 1U; index <= k; ++index) {
    result = result * (n - k + index) / index;
  }
  return result;
}

static_assert(choose(36U, 7U) == seven_card_table_entry_count);

std::uint32_t encode(const HandValue &value) noexcept {
  std::uint32_t encoded = static_cast<std::uint32_t>(value.category) << 20U;
  for (std::size_t index = 0U; index < value.kickers.size(); ++index) {
    encoded |= static_cast<std::uint32_t>(value.kickers[index]) << (16U - 4U * index);
  }
  return encoded;
}

HandValue decode(const std::uint32_t encoded) noexcept {
  HandValue value;
  value.category = static_cast<HandCategory>((encoded >> 20U) & 0xFU);
  for (std::size_t index = 0U; index < value.kickers.size(); ++index) {
    value.kickers[index] = static_cast<std::uint8_t>((encoded >> (16U - 4U * index)) & 0xFU);
  }
  return value;
}

std::uint64_t payload_checksum(const std::span<const std::uint32_t> encoded) noexcept {
  auto hash = fnv_offset;
  for (const auto value : encoded) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
      hash ^= static_cast<std::uint8_t>((value >> shift) & 0xFFU);
      hash *= fnv_prime;
    }
  }
  return hash;
}

template <typename Unsigned> bool write_little(std::ostream &output, Unsigned value) {
  static_assert(std::is_unsigned_v<Unsigned>);
  for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
    output.put(static_cast<char>(value & 0xFFU));
    value >>= 8U;
  }
  return static_cast<bool>(output);
}

template <typename Unsigned> bool read_little(std::istream &input, Unsigned &value) {
  static_assert(std::is_unsigned_v<Unsigned>);
  value = 0U;
  for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
    const auto byte = input.get();
    if (byte == std::char_traits<char>::eof()) {
      return false;
    }
    value |= static_cast<Unsigned>(static_cast<std::uint8_t>(byte)) << (8U * index);
  }
  return true;
}

bool atomic_replace(const std::filesystem::path &temporary,
                    const std::filesystem::path &destination) {
#ifdef _WIN32
  return MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_WRITE_THROUGH | MOVEFILE_REPLACE_EXISTING) != 0;
#else
  std::error_code error;
  std::filesystem::rename(temporary, destination, error);
  return !error;
#endif
}

EquityError equity_error(const SevenCardTableError error) noexcept {
  return error == SevenCardTableError::InvalidCards ? EquityError::DuplicateCard
                                                    : EquityError::InternalEvaluatorFailure;
}

} // namespace

Result<std::uint64_t, SevenCardTableError>
seven_card_combination_index(const std::array<CardId, 7> &cards) {
  std::array<std::uint8_t, 7> sorted{};
  std::transform(cards.begin(), cards.end(), sorted.begin(),
                 [](const CardId card) { return card.value(); });
  std::ranges::sort(sorted);
  for (std::size_t index = 1U; index < sorted.size(); ++index) {
    if (sorted[index] >= 36U || sorted[index] == sorted[index - 1U]) {
      return Result<std::uint64_t, SevenCardTableError>::failure(SevenCardTableError::InvalidCards);
    }
  }
  std::uint64_t result = 0U;
  for (std::size_t index = 0U; index < sorted.size(); ++index) {
    result += choose(sorted[index], index + 1U);
  }
  return result < seven_card_table_entry_count
             ? Result<std::uint64_t, SevenCardTableError>::success(result)
             : Result<std::uint64_t, SevenCardTableError>::failure(
                   SevenCardTableError::InvalidCards);
}

Result<SevenCardLookupTable, SevenCardTableError> SevenCardLookupTable::build() {
  try {
    std::vector<std::uint32_t> encoded(seven_card_table_entry_count);
    const auto deck = short_deck();
    for (std::uint8_t a = 0U; a < 30U; ++a) {
      for (std::uint8_t b = static_cast<std::uint8_t>(a + 1U); b < 31U; ++b) {
        for (std::uint8_t c = static_cast<std::uint8_t>(b + 1U); c < 32U; ++c) {
          for (std::uint8_t d = static_cast<std::uint8_t>(c + 1U); d < 33U; ++d) {
            for (std::uint8_t e = static_cast<std::uint8_t>(d + 1U); e < 34U; ++e) {
              for (std::uint8_t f = static_cast<std::uint8_t>(e + 1U); f < 35U; ++f) {
                for (std::uint8_t g = static_cast<std::uint8_t>(f + 1U); g < 36U; ++g) {
                  const std::array<CardId, 7> cards{deck[a], deck[b], deck[c], deck[d],
                                                    deck[e], deck[f], deck[g]};
                  const auto value = gtosd::evaluate_seven(cards);
                  const auto index = seven_card_combination_index(cards);
                  if (!value || !index) {
                    return Result<SevenCardLookupTable, SevenCardTableError>::failure(
                        SevenCardTableError::IntegrityFailure);
                  }
                  encoded[index.value()] = encode(value.value());
                }
              }
            }
          }
        }
      }
    }
    const auto hash = payload_checksum(encoded);
    return Result<SevenCardLookupTable, SevenCardTableError>::success(
        SevenCardLookupTable(std::move(encoded), hash));
  } catch (const std::bad_alloc &) {
    return Result<SevenCardLookupTable, SevenCardTableError>::failure(
        SevenCardTableError::MemoryFailure);
  }
}

Result<bool, SevenCardTableError>
SevenCardLookupTable::save(const std::filesystem::path &path) const {
  if (path.empty() || encoded_.size() != seven_card_table_entry_count ||
      payload_checksum(encoded_) != checksum_) {
    return Result<bool, SevenCardTableError>::failure(SevenCardTableError::IntegrityFailure);
  }
  const auto temporary = path.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.write(magic.data(), static_cast<std::streamsize>(magic.size()));
    const auto fingerprint_size =
        static_cast<std::uint32_t>(seven_card_table_ruleset_fingerprint.size());
    if (!output || !write_little(output, format_major) || !write_little(output, format_minor) ||
        !write_little(output, seven_card_table_entry_count) || !write_little(output, checksum_) ||
        !write_little(output, fingerprint_size)) {
      return Result<bool, SevenCardTableError>::failure(SevenCardTableError::IoFailure);
    }
    output.write(seven_card_table_ruleset_fingerprint.data(), fingerprint_size);
    for (const auto value : encoded_) {
      if (!write_little(output, value)) {
        return Result<bool, SevenCardTableError>::failure(SevenCardTableError::IoFailure);
      }
    }
    output.flush();
    if (!output) {
      return Result<bool, SevenCardTableError>::failure(SevenCardTableError::IoFailure);
    }
  }
  if (!atomic_replace(temporary, path)) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return Result<bool, SevenCardTableError>::failure(SevenCardTableError::IoFailure);
  }
  return Result<bool, SevenCardTableError>::success(true);
}

Result<SevenCardLookupTable, SevenCardTableError>
SevenCardLookupTable::load(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Result<SevenCardLookupTable, SevenCardTableError>::failure(
        SevenCardTableError::IoFailure);
  }
  std::array<char, 8> observed_magic{};
  input.read(observed_magic.data(), static_cast<std::streamsize>(observed_magic.size()));
  std::uint32_t major = 0U;
  std::uint32_t minor = 0U;
  std::uint64_t count = 0U;
  std::uint64_t expected_checksum = 0U;
  std::uint32_t fingerprint_size = 0U;
  if (!input || observed_magic != magic || !read_little(input, major) ||
      !read_little(input, minor) || !read_little(input, count) ||
      !read_little(input, expected_checksum) || !read_little(input, fingerprint_size)) {
    return Result<SevenCardLookupTable, SevenCardTableError>::failure(
        SevenCardTableError::IntegrityFailure);
  }
  if (major != format_major || minor != format_minor) {
    return Result<SevenCardLookupTable, SevenCardTableError>::failure(
        SevenCardTableError::UnsupportedVersion);
  }
  if (count != seven_card_table_entry_count ||
      fingerprint_size != seven_card_table_ruleset_fingerprint.size()) {
    return Result<SevenCardLookupTable, SevenCardTableError>::failure(
        SevenCardTableError::IntegrityFailure);
  }
  std::string fingerprint(fingerprint_size, '\0');
  input.read(fingerprint.data(), fingerprint.size());
  if (!input || fingerprint != seven_card_table_ruleset_fingerprint) {
    return Result<SevenCardLookupTable, SevenCardTableError>::failure(
        SevenCardTableError::IntegrityFailure);
  }
  try {
    std::vector<std::uint32_t> encoded(count);
    for (auto &value : encoded) {
      if (!read_little(input, value)) {
        return Result<SevenCardLookupTable, SevenCardTableError>::failure(
            SevenCardTableError::IntegrityFailure);
      }
    }
    if (input.peek() != std::char_traits<char>::eof() ||
        payload_checksum(encoded) != expected_checksum) {
      return Result<SevenCardLookupTable, SevenCardTableError>::failure(
          SevenCardTableError::IntegrityFailure);
    }
    return Result<SevenCardLookupTable, SevenCardTableError>::success(
        SevenCardLookupTable(std::move(encoded), expected_checksum));
  } catch (const std::bad_alloc &) {
    return Result<SevenCardLookupTable, SevenCardTableError>::failure(
        SevenCardTableError::MemoryFailure);
  }
}

Result<HandValue, EquityError>
SevenCardLookupTable::evaluate_seven(const std::array<CardId, 7> &cards) const {
  const auto index = seven_card_combination_index(cards);
  if (!index || index.value() >= encoded_.size()) {
    return Result<HandValue, EquityError>::failure(index ? EquityError::InternalEvaluatorFailure
                                                         : equity_error(index.error()));
  }
  return Result<HandValue, EquityError>::success(decode(encoded_[index.value()]));
}

Result<std::vector<HandValue>, EquityError>
SevenCardLookupTable::evaluate_batch(const std::span<const std::array<CardId, 7>> hands) const {
  try {
    std::vector<HandValue> values;
    values.reserve(hands.size());
    for (const auto &hand : hands) {
      const auto value = evaluate_seven(hand);
      if (!value) {
        return Result<std::vector<HandValue>, EquityError>::failure(value.error());
      }
      values.push_back(value.value());
    }
    return Result<std::vector<HandValue>, EquityError>::success(std::move(values));
  } catch (const std::bad_alloc &) {
    return Result<std::vector<HandValue>, EquityError>::failure(
        EquityError::InternalEvaluatorFailure);
  }
}

const char *seven_card_table_error_name(const SevenCardTableError error) noexcept {
  switch (error) {
  case SevenCardTableError::InvalidCards:
    return "invalid_cards";
  case SevenCardTableError::MemoryFailure:
    return "memory_failure";
  case SevenCardTableError::IoFailure:
    return "io_failure";
  case SevenCardTableError::IntegrityFailure:
    return "integrity_failure";
  case SevenCardTableError::UnsupportedVersion:
    return "unsupported_version";
  }
  return "unknown";
}

} // namespace gtosd
