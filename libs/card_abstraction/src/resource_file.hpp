#pragma once

#include "gtosd/card_abstraction/rank_table.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Internal helper shared by the precomputed resources: a versioned container
// with kind, fingerprint and FNV-1a checksum, written atomically.
namespace gtosd::card_abstraction::detail {

inline constexpr std::uint64_t fnv_offset = 14'695'981'039'346'656'037ULL;
inline constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;

[[nodiscard]] std::uint64_t fnv1a(std::span<const std::uint8_t> bytes,
                                  std::uint64_t hash = fnv_offset) noexcept;
[[nodiscard]] std::uint64_t fnv1a_text(std::string_view text,
                                       std::uint64_t hash = fnv_offset) noexcept;
[[nodiscard]] std::string hex64(std::uint64_t value);

struct ResourcePayload {
  std::string kind;
  std::uint32_t version{0U};
  std::string fingerprint;
  std::vector<std::uint8_t> bytes;
};

[[nodiscard]] Result<bool, ResourceError> write_resource(const std::filesystem::path &path,
                                                         std::string_view kind,
                                                         std::uint32_t version,
                                                         std::string_view fingerprint,
                                                         std::span<const std::uint8_t> payload);

[[nodiscard]] Result<ResourcePayload, ResourceError>
read_resource(const std::filesystem::path &path, std::string_view expected_kind,
              std::uint32_t expected_version);

template <typename Unsigned> void append_little(std::vector<std::uint8_t> &bytes, Unsigned value) {
  for (std::size_t byte = 0U; byte < sizeof(Unsigned); ++byte) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    value >>= 8U;
  }
}

template <typename Unsigned>
[[nodiscard]] bool read_little(std::span<const std::uint8_t> bytes, std::size_t &position,
                               Unsigned &value) noexcept {
  if (position + sizeof(Unsigned) > bytes.size()) {
    return false;
  }
  value = 0U;
  for (std::size_t byte = 0U; byte < sizeof(Unsigned); ++byte) {
    value |= static_cast<Unsigned>(static_cast<Unsigned>(bytes[position + byte]) << (8U * byte));
  }
  position += sizeof(Unsigned);
  return true;
}

template <typename Unsigned>
void append_vector(std::vector<std::uint8_t> &bytes, const std::vector<Unsigned> &values) {
  append_little(bytes, static_cast<std::uint64_t>(values.size()));
  for (const auto value : values) {
    append_little(bytes, value);
  }
}

template <typename Unsigned>
[[nodiscard]] bool read_vector(std::span<const std::uint8_t> bytes, std::size_t &position,
                               std::vector<Unsigned> &values, const std::uint64_t expected_size) {
  std::uint64_t size = 0U;
  if (!read_little(bytes, position, size) || size != expected_size ||
      position + size * sizeof(Unsigned) > bytes.size()) {
    return false;
  }
  values.resize(static_cast<std::size_t>(size));
  for (auto &value : values) {
    if (!read_little(bytes, position, value)) {
      return false;
    }
  }
  return true;
}

} // namespace gtosd::card_abstraction::detail
