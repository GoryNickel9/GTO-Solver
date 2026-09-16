#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Little-endian serialization helpers shared by the checkpoint, the policy
// file and the certifier state (private to the library).
namespace gtosd::preflop_blueprint::binary_io {

inline void append_little(std::string &buffer, const std::uint64_t value) {
  for (unsigned shift = 0; shift < 64U; shift += 8U) {
    buffer.push_back(static_cast<char>((value >> shift) & 0xFFU));
  }
}

inline void append_little32(std::string &buffer, const std::uint32_t value) {
  for (unsigned shift = 0; shift < 32U; shift += 8U) {
    buffer.push_back(static_cast<char>((value >> shift) & 0xFFU));
  }
}

inline void append_string(std::string &buffer, const std::string &value) {
  append_little32(buffer, static_cast<std::uint32_t>(value.size()));
  buffer += value;
}

inline void append_doubles(std::string &buffer, const std::vector<double> &values) {
  const auto offset = buffer.size();
  buffer.resize(offset + values.size() * sizeof(double));
  if (!values.empty()) {
    std::memcpy(buffer.data() + offset, values.data(), values.size() * sizeof(double));
  }
}

class Reader {
public:
  explicit Reader(const std::string &data) : data_(data) {}
  bool read_little(std::uint64_t &value) {
    if (position_ + 8U > data_.size()) {
      return false;
    }
    value = 0U;
    for (unsigned shift = 0; shift < 64U; shift += 8U) {
      value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(data_[position_++])) << shift;
    }
    return true;
  }
  bool read_little32(std::uint32_t &value) {
    if (position_ + 4U > data_.size()) {
      return false;
    }
    value = 0U;
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
      value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(data_[position_++])) << shift;
    }
    return true;
  }
  bool read_doubles(std::vector<double> &values, const std::size_t count) {
    if (position_ + count * sizeof(double) > data_.size()) {
      return false;
    }
    values.resize(count);
    if (count > 0U) {
      std::memcpy(values.data(), data_.data() + position_, count * sizeof(double));
    }
    position_ += count * sizeof(double);
    return true;
  }
  bool read_bytes(std::vector<std::uint8_t> &values, const std::size_t count) {
    if (position_ + count > data_.size()) {
      return false;
    }
    values.resize(count);
    if (count > 0U) {
      std::memcpy(values.data(), data_.data() + position_, count);
    }
    position_ += count;
    return true;
  }
  bool read_string(std::string &value) {
    std::uint32_t length = 0U;
    if (!read_little32(length) || position_ + length > data_.size()) {
      return false;
    }
    value.assign(data_.data() + position_, length);
    position_ += length;
    return true;
  }
  [[nodiscard]] std::size_t position() const noexcept { return position_; }
  [[nodiscard]] bool at_end() const noexcept { return position_ == data_.size(); }

private:
  const std::string &data_;
  std::size_t position_{0U};
};

} // namespace gtosd::preflop_blueprint::binary_io
