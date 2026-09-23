#pragma once

#include "binary_io.hpp"
#include "hashing.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <istream>
#include <ostream>
#include <span>
#include <string_view>

// Same bytes and FNV checksum as the existing v1 policy/v2 checkpoint files,
// with bounded temporary storage instead of copies of the numeric arrays.
namespace gtosd::preflop_blueprint::stream_io {

// Header, then every array as raw bytes, then the FNV-1a checksum of all the
// preceding bytes. Arrays may have different element types (checkpoint
// storage formats); the byte sequence is what is hashed.
inline bool write_raw(std::ostream &output, const std::string &header,
                      const std::span<const std::string_view> arrays) {
  auto hash = detail::fnv1a_text(header);
  output.write(header.data(), static_cast<std::streamsize>(header.size()));
  for (const auto bytes : arrays) {
    hash = detail::fnv1a_text(bytes, hash);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  std::string checksum;
  binary_io::append_little(checksum, hash);
  output.write(checksum.data(), static_cast<std::streamsize>(checksum.size()));
  output.flush();
  return static_cast<bool>(output);
}

inline bool write(std::ostream &output, const std::string &header,
                  const std::span<const std::span<const double>> arrays) {
  std::array<std::string_view, 8> views{};
  std::size_t count = 0U;
  for (const auto values : arrays) {
    if (count >= views.size())
      return false;
    views[count++] = std::string_view(reinterpret_cast<const char *>(values.data()),
                                      values.size_bytes());
  }
  return write_raw(output, header, std::span<const std::string_view>(views.data(), count));
}

class Reader {
public:
  explicit Reader(std::istream &input) : input_(input) {
    input_.clear();
    input_.seekg(0, std::ios::end);
    const auto length = input_.tellg();
    if (length >= 8)
      remaining_ = static_cast<std::uint64_t>(length) - 8;
    input_.seekg(0);
  }
  bool bytes(char *destination, const std::size_t size) {
    if (size > remaining_)
      return false;
    input_.read(destination, static_cast<std::streamsize>(size));
    if (!input_)
      return false;
    hash_ = detail::fnv1a_text(std::string_view(destination, size), hash_);
    remaining_ -= size;
    return true;
  }
  bool u32(std::uint32_t &value) {
    std::array<char, 4> data{};
    if (!bytes(data.data(), data.size()))
      return false;
    value = 0;
    for (unsigned i = 0; i < data.size(); ++i)
      value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[i])) << (i * 8);
    return true;
  }
  bool u64(std::uint64_t &value) {
    std::array<char, 8> data{};
    if (!bytes(data.data(), data.size()))
      return false;
    value = 0;
    for (unsigned i = 0; i < data.size(); ++i)
      value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(data[i])) << (i * 8);
    return true;
  }
  bool string(std::string &value) {
    std::uint32_t length = 0;
    if (!u32(length) || length > remaining_ || length > 1024U * 1024U)
      return false;
    value.resize(length);
    return bytes(value.data(), value.size());
  }
  template <typename Number>
  bool numbers(const std::span<Number> values, const bool nonnegative = false) {
    if (!bytes(reinterpret_cast<char *>(values.data()), values.size_bytes()))
      return false;
    for (const auto value : values)
      if (!std::isfinite(value) || (nonnegative && value < 0))
        return false;
    return true;
  }
  bool doubles(const std::span<double> values, const bool nonnegative = false) {
    return numbers(values, nonnegative);
  }
  bool floats(const std::span<float> values, const bool nonnegative = false) {
    return numbers(values, nonnegative);
  }
  template <typename Number>
  bool scan_numbers(std::uint64_t count, const bool nonnegative = false,
                    std::uint64_t *array_hash = nullptr) {
    if (count > remaining_ / sizeof(Number))
      return false;
    std::array<Number, 8192> buffer{};
    auto hash = detail::fnv_offset_basis;
    while (count > 0) {
      const auto size = static_cast<std::size_t>(std::min<std::uint64_t>(count, buffer.size()));
      const auto part = std::span<Number>(buffer.data(), size);
      if (!numbers(part, nonnegative))
        return false;
      if (array_hash)
        hash = detail::fnv1a_text(
            std::string_view(reinterpret_cast<const char *>(part.data()), part.size_bytes()), hash);
      count -= size;
    }
    if (array_hash)
      *array_hash = hash;
    return true;
  }
  bool scan_doubles(const std::uint64_t count, const bool nonnegative = false,
                    std::uint64_t *array_hash = nullptr) {
    return scan_numbers<double>(count, nonnegative, array_hash);
  }
  bool scan_floats(const std::uint64_t count, const bool nonnegative = false,
                   std::uint64_t *array_hash = nullptr) {
    return scan_numbers<float>(count, nonnegative, array_hash);
  }
  [[nodiscard]] std::uint64_t remaining() const noexcept { return remaining_; }
  bool finish() {
    if (remaining_ != 0)
      return false;
    std::string checksum(8, '\0');
    input_.read(checksum.data(), 8);
    if (!input_ || input_.peek() != std::char_traits<char>::eof())
      return false;
    binary_io::Reader reader(checksum);
    std::uint64_t stored = 0;
    return reader.read_little(stored) && stored == hash_;
  }

private:
  std::istream &input_;
  std::uint64_t remaining_{0};
  std::uint64_t hash_{detail::fnv_offset_basis};
};
} // namespace gtosd::preflop_blueprint::stream_io
