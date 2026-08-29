#include "gtosd/postflop/postflop_solver.hpp"
#include "gtosd/postflop/canonical_layout.hpp"

#include "gtosd/core/ranges.hpp"
#include "gtosd/equity/evaluator.hpp"
#include "gtosd/isomorphism/isomorphism.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <functional>
#include <iomanip>
#include <immintrin.h>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace gtosd {
namespace {

constexpr std::size_t combo_count = 630U;
constexpr std::size_t maximum_action_count = 8U;
constexpr std::size_t maximum_parallel_action_count = 3U;
constexpr std::size_t compact_combo_capacity = 36U;
constexpr std::size_t medium_combo_capacity = 256U;
constexpr std::size_t short_deck_range_capacity = 360U;
constexpr std::size_t compact_player_combo_capacity = 384U;
constexpr std::size_t large_range_capacity = 512U;
constexpr double units_per_ante = 10'000.0;
using DenseComboVector = std::array<double, combo_count>;
template <std::size_t Capacity>
using TraversalScalar = double;
template <std::size_t Capacity>
using TraversalComboVector = std::array<TraversalScalar<Capacity>, Capacity>;

class PagedActionFile;

struct ActionBuffers {
  double *regret{nullptr};
  double *strategy{nullptr};
  std::size_t count{0};
  PagedActionFile *paged{nullptr};
  float *regret_float32{nullptr};
  float *strategy_float32{nullptr};
  std::uint8_t *regret_float24{nullptr};
  std::uint16_t *strategy_float16{nullptr};
  std::uint8_t *compact_state{nullptr};
  // Reserved for the rejected aligned-codec experiment. No production
  // precision selects these pointers.
  std::uint16_t *compact_regret16{nullptr};
  std::uint16_t *compact_strategy16{nullptr};
  // Reserved experimental aligned state backend. Production compact solves
  // leave this null and retain the three-byte/action representation.
  std::uint8_t *aligned_compact_state{nullptr};
  // Action-major uint16 state with one floating-point scale per canonical
  // decision node. The generic *_at accessors expose raw codes because every
  // consumer outside the update path normalizes within one node, where the
  // common scale cancels exactly.
  std::uint16_t *scaled_regret{nullptr};
  std::uint16_t *scaled_strategy{nullptr};
  float *regret_node_scale{nullptr};
  float *strategy_node_scale{nullptr};
  std::size_t decision_node_count{0};
  bool action_major_compact{false};
  bool signed_scaled_regret{false};

  [[nodiscard]] double regret_at(std::size_t index) const;
  [[nodiscard]] double strategy_at(std::size_t index) const;
  void set_regret(std::size_t index, double value) const;
  void add_strategy(std::size_t index, double value) const;
  void scale_strategy(double factor) const;
};


[[nodiscard]] std::uint32_t compact_word(const std::uint8_t *const bytes) noexcept {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U);
}

void store_compact_word(std::uint8_t *const bytes, const std::uint32_t word) noexcept {
  bytes[0] = static_cast<std::uint8_t>(word);
  bytes[1] = static_cast<std::uint8_t>(word >> 8U);
  bytes[2] = static_cast<std::uint8_t>(word >> 16U);
}

[[nodiscard]] double decode_regret13(const std::uint16_t value) noexcept {
  return static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(value) << 18U));
}

[[nodiscard]] std::uint16_t encode_regret13(const double value) noexcept {
  std::uint32_t bits = std::bit_cast<std::uint32_t>(static_cast<float>(value)) & 0x7fffffffU;
  const std::uint32_t discarded = bits & 0x3ffffU;
  std::uint32_t packed = bits >> 18U;
  if (discarded > 0x20000U || (discarded == 0x20000U && (packed & 1U) != 0U)) {
    ++packed;
  }
  return static_cast<std::uint16_t>(std::min(packed, 0x1ffeU));
}

[[nodiscard]] double decode_strategy11(const std::uint16_t value) noexcept {
  const auto exponent = static_cast<std::uint64_t>(value >> 6U);
  const auto mantissa = static_cast<std::uint64_t>(value & 0x3fU);
  if (exponent == 0U) {
    // m * 2^-20.  Construct the exact binary64 value directly; strategy
    // subnormals in E5M6 are normal binary64 values except for zero.
    if (mantissa == 0U) {
      return 0.0;
    }
    const auto leading_zeroes = static_cast<unsigned>(std::countl_zero(mantissa) - 58U);
    const auto unbiased_exponent = -15 - static_cast<int>(leading_zeroes);
    const auto normalized = mantissa << (leading_zeroes + 1U);
    const auto fraction = (normalized & 0x3fU) << 46U;
    return std::bit_cast<double>(
        (static_cast<std::uint64_t>(unbiased_exponent + 1023) << 52U) | fraction);
  }
  return std::bit_cast<double>(((exponent + 1008U) << 52U) | (mantissa << 46U));
}

[[nodiscard]] std::uint16_t encode_strategy11(const double value) noexcept {
  if (!(value > 0.0)) {
    return 0U;
  }
  if (value < std::ldexp(1.0, -14)) {
    return static_cast<std::uint16_t>(
        std::clamp(std::nearbyint(std::ldexp(value, 20)), 0.0, 63.0));
  }
  const auto bits = std::bit_cast<std::uint64_t>(value);
  auto encoded_exponent = static_cast<int>((bits >> 52U) & 0x7ffU) - 1008;
  if (encoded_exponent > 31) {
    return 0x7ffU;
  }
  constexpr std::uint64_t discarded_mask = (std::uint64_t{1} << 46U) - 1U;
  constexpr std::uint64_t halfway = std::uint64_t{1} << 45U;
  const auto fraction = bits & ((std::uint64_t{1} << 52U) - 1U);
  auto encoded_mantissa = static_cast<std::uint32_t>(fraction >> 46U);
  const auto discarded = fraction & discarded_mask;
  if (discarded > halfway || (discarded == halfway && (encoded_mantissa & 1U) != 0U)) {
    ++encoded_mantissa;
  }
  if (encoded_mantissa == 64U) {
    encoded_mantissa = 0U;
    ++encoded_exponent;
  }
  if (encoded_exponent > 31) {
    return 0x7ffU;
  }
  return static_cast<std::uint16_t>((static_cast<std::uint32_t>(encoded_exponent) << 6U) |
                                    encoded_mantissa);
}

[[nodiscard]] float decode_float24(const std::uint8_t *const bytes) noexcept {
  const std::uint32_t packed = static_cast<std::uint32_t>(bytes[0]) |
                               (static_cast<std::uint32_t>(bytes[1]) << 8U) |
                               (static_cast<std::uint32_t>(bytes[2]) << 16U);
  return std::bit_cast<float>(packed << 7U);
}

[[nodiscard]] float decode_float24_overread(const std::uint8_t *const bytes) noexcept {
  std::uint32_t packed = 0U;
  std::memcpy(&packed, bytes, sizeof(packed));
  return std::bit_cast<float>((packed & 0x00ffffffU) << 7U);
}


[[nodiscard]] std::uint32_t encode_float24_bits(const double value) noexcept {
  std::uint32_t bits = std::bit_cast<std::uint32_t>(static_cast<float>(value));
  // CFR+ clips every stored regret to a non-negative value. Omit the always
  // zero sign bit and retain exponent(8)+fraction(16), then round the seven
  // discarded fraction bits to nearest, ties to even.
  bits &= 0x7fffffffU;
  const std::uint32_t discarded = bits & 0x7fU;
  std::uint32_t packed = bits >> 7U;
  if (discarded > 0x40U || (discarded == 0x40U && (packed & 1U) != 0U)) {
    ++packed;
  }
  return packed;
}

void encode_float24(std::uint8_t *const bytes, const double value) noexcept {
  const std::uint32_t packed = encode_float24_bits(value);
  bytes[0] = static_cast<std::uint8_t>(packed);
  bytes[1] = static_cast<std::uint8_t>(packed >> 8U);
  bytes[2] = static_cast<std::uint8_t>(packed >> 16U);
}

[[nodiscard]] float decode_float16(const std::uint16_t value) noexcept {
  return _mm_cvtss_f32(_mm_cvtph_ps(_mm_cvtsi32_si128(static_cast<int>(value))));
}

[[nodiscard]] std::uint16_t encode_float16(const double value) noexcept {
  return static_cast<std::uint16_t>(_mm_extract_epi16(
      _mm_cvtps_ph(_mm_set_ss(static_cast<float>(value)), _MM_FROUND_TO_NEAREST_INT), 0));
}

class PagedActionFile {
public:
  PagedActionFile(const std::string &path, const std::size_t action_count, const bool create)
      : path_(path), action_count_(action_count) {
    if (path.empty() || action_count == 0U ||
        action_count > std::numeric_limits<std::uint64_t>::max() / (2U * sizeof(double))) {
      return;
    }
    size_ = static_cast<std::uint64_t>(action_count) * 2U * sizeof(double);
    if (create) {
      std::ofstream output(path, std::ios::binary | std::ios::trunc);
      if (!output) {
        return;
      }
      output.seekp(static_cast<std::streamoff>(size_ - 1U));
      output.put('\0');
      output.flush();
      if (!output) {
        return;
      }
    } else {
      std::error_code size_error;
      if (std::filesystem::file_size(path, size_error) != size_ || size_error) {
        return;
      }
    }
    file_.open(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file_) {
      return;
    }
    pages_.resize(resident_page_count);
    for (auto &page : pages_) {
      page.bytes.resize(page_size_bytes);
    }
    valid_ = true;
  }

  PagedActionFile(const PagedActionFile &) = delete;
  PagedActionFile &operator=(const PagedActionFile &) = delete;

  ~PagedActionFile() { static_cast<void>(flush()); }

  [[nodiscard]] bool valid() const noexcept { return valid_; }
  [[nodiscard]] ActionBuffers buffers() noexcept { return {nullptr, nullptr, action_count_, this}; }
  [[nodiscard]] const std::string &path() const noexcept { return path_; }

  [[nodiscard]] double get(const std::size_t logical_index) {
    const auto byte_offset = static_cast<std::uint64_t>(logical_index) * sizeof(double);
    auto *const page = load_page(byte_offset / page_size_bytes);
    if (page == nullptr) {
      return std::numeric_limits<double>::quiet_NaN();
    }
    double value = 0.0;
    std::memcpy(&value, page->bytes.data() + byte_offset % page_size_bytes, sizeof(value));
    return value;
  }

  void set(const std::size_t logical_index, const double value) {
    const auto byte_offset = static_cast<std::uint64_t>(logical_index) * sizeof(double);
    auto *const page = load_page(byte_offset / page_size_bytes);
    if (page == nullptr) {
      return;
    }
    std::memcpy(page->bytes.data() + byte_offset % page_size_bytes, &value, sizeof(value));
    page->dirty = true;
  }

  [[nodiscard]] bool flush() {
    if (!valid_) {
      return false;
    }
    for (auto &page : pages_) {
      if (!flush_page(page)) {
        valid_ = false;
        return false;
      }
    }
    file_.flush();
    valid_ = static_cast<bool>(file_);
    return valid_;
  }

private:
  static constexpr std::uint64_t page_size_bytes = 64U * 1'024U;
  static constexpr std::size_t resident_page_count = 64U;
  static constexpr std::uint64_t invalid_page = std::numeric_limits<std::uint64_t>::max();

  struct Page {
    std::uint64_t index{invalid_page};
    std::uint64_t stamp{0};
    bool dirty{false};
    std::vector<char> bytes;
  };

  bool flush_page(Page &page) {
    if (page.index == invalid_page || !page.dirty) {
      return true;
    }
    const auto offset = page.index * page_size_bytes;
    const auto bytes = static_cast<std::streamsize>(std::min(page_size_bytes, size_ - offset));
    file_.clear();
    file_.seekp(static_cast<std::streamoff>(offset));
    file_.write(page.bytes.data(), bytes);
    if (!file_) {
      return false;
    }
    page.dirty = false;
    return true;
  }

  Page *load_page(const std::uint64_t page_index) {
    const auto found = page_lookup_.find(page_index);
    if (found != page_lookup_.end()) {
      auto &page = pages_[found->second];
      page.stamp = ++stamp_;
      return &page;
    }
    std::size_t slot = pages_.size();
    for (std::size_t index = 0; index < pages_.size(); ++index) {
      if (pages_[index].index == invalid_page) {
        slot = index;
        break;
      }
    }
    if (slot == pages_.size()) {
      slot = static_cast<std::size_t>(std::min_element(pages_.begin(), pages_.end(),
                                                       [](const Page &left, const Page &right) {
                                                         return left.stamp < right.stamp;
                                                       }) -
                                      pages_.begin());
    }
    auto &page = pages_[slot];
    if (!flush_page(page)) {
      valid_ = false;
      return nullptr;
    }
    if (page.index != invalid_page) {
      page_lookup_.erase(page.index);
    }
    std::fill(page.bytes.begin(), page.bytes.end(), '\0');
    const auto offset = page_index * page_size_bytes;
    const auto bytes = static_cast<std::streamsize>(std::min(page_size_bytes, size_ - offset));
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(offset));
    file_.read(page.bytes.data(), bytes);
    if (file_.gcount() != bytes) {
      valid_ = false;
      return nullptr;
    }
    page.index = page_index;
    page.stamp = ++stamp_;
    page.dirty = false;
    page_lookup_[page_index] = slot;
    return &page;
  }

  std::string path_;
  std::size_t action_count_{0};
  std::uint64_t size_{0};
  std::fstream file_;
  std::vector<Page> pages_;
  std::unordered_map<std::uint64_t, std::size_t> page_lookup_;
  std::uint64_t stamp_{0};
  bool valid_{false};
};

double ActionBuffers::regret_at(const std::size_t index) const {
  if (scaled_regret != nullptr) {
    return signed_scaled_regret
               ? static_cast<double>(static_cast<std::int16_t>(scaled_regret[index]))
               : static_cast<double>(scaled_regret[index]);
  }
  if (compact_state != nullptr) {
    return decode_regret13(static_cast<std::uint16_t>(
        compact_word(compact_state + index * 3U) & 0x1fffU));
  }
  if (paged != nullptr) {
    return paged->get(index);
  }
  if (regret_float24 != nullptr) {
    return static_cast<double>(decode_float24(regret_float24 + index * 3U));
  }
  return regret_float32 != nullptr ? static_cast<double>(regret_float32[index]) : regret[index];
}

double ActionBuffers::strategy_at(const std::size_t index) const {
  if (scaled_strategy != nullptr) {
    return static_cast<double>(scaled_strategy[index]);
  }
  if (compact_state != nullptr) {
    return decode_strategy11(static_cast<std::uint16_t>(
        compact_word(compact_state + index * 3U) >> 13U));
  }
  if (paged != nullptr) {
    return paged->get(count + index);
  }
  if (strategy_float16 != nullptr) {
    return static_cast<double>(decode_float16(strategy_float16[index]));
  }
  return strategy_float32 != nullptr ? static_cast<double>(strategy_float32[index])
                                     : strategy[index];
}

void ActionBuffers::set_regret(const std::size_t index, const double value) const {
  if (scaled_regret != nullptr) {
    if (signed_scaled_regret) {
      const auto encoded = static_cast<std::int16_t>(std::clamp(
          std::nearbyint(value), -32767.0, 32767.0));
      scaled_regret[index] = static_cast<std::uint16_t>(encoded);
    } else {
      scaled_regret[index] = static_cast<std::uint16_t>(
          std::clamp(std::nearbyint(value), 0.0, 65535.0));
    }
    return;
  }
  if (compact_state != nullptr) {
    auto *const bytes = compact_state + index * 3U;
    const auto word = compact_word(bytes);
    store_compact_word(bytes, (word & 0xffe000U) | encode_regret13(value));
  } else if (paged != nullptr) {
    paged->set(index, value);
  } else if (regret_float24 != nullptr) {
    encode_float24(regret_float24 + index * 3U, value);
  } else if (regret_float32 != nullptr) {
    regret_float32[index] = static_cast<float>(value);
  } else {
    regret[index] = value;
  }
}

void ActionBuffers::add_strategy(const std::size_t index, const double value) const {
  if (scaled_strategy != nullptr) {
    scaled_strategy[index] = static_cast<std::uint16_t>(std::clamp(
        std::nearbyint(static_cast<double>(scaled_strategy[index]) + value), 0.0, 65535.0));
    return;
  }
  if (compact_state != nullptr) {
    auto *const bytes = compact_state + index * 3U;
    const auto word = compact_word(bytes);
    const double updated = decode_strategy11(static_cast<std::uint16_t>(word >> 13U)) + value;
    store_compact_word(bytes, (word & 0x001fffU) |
                                  (static_cast<std::uint32_t>(encode_strategy11(updated)) << 13U));
  } else if (paged != nullptr) {
    paged->set(count + index, paged->get(count + index) + value);
  } else if (strategy_float16 != nullptr) {
    strategy_float16[index] =
        encode_float16(static_cast<double>(decode_float16(strategy_float16[index])) + value);
  } else if (strategy_float32 != nullptr) {
    strategy_float32[index] =
        static_cast<float>(static_cast<double>(strategy_float32[index]) + value);
  } else {
    strategy[index] += value;
  }
}

void ActionBuffers::scale_strategy(const double factor) const {
  if (scaled_strategy != nullptr) {
    for (std::size_t node = 0; node < decision_node_count; ++node) {
      strategy_node_scale[node] = static_cast<float>(
          static_cast<double>(strategy_node_scale[node]) * factor);
    }
    return;
  }
  for (std::size_t index = 0; index < count; ++index) {
    const double scaled = strategy_at(index) * factor;
    if (compact_state != nullptr) {
      auto *const bytes = compact_state + index * 3U;
      const auto word = compact_word(bytes);
      store_compact_word(bytes, (word & 0x001fffU) |
                                    (static_cast<std::uint32_t>(encode_strategy11(scaled)) << 13U));
    } else if (paged != nullptr) {
      paged->set(count + index, scaled);
    } else if (strategy_float16 != nullptr) {
      strategy_float16[index] = encode_float16(scaled);
    } else if (strategy_float32 != nullptr) {
      strategy_float32[index] = static_cast<float>(scaled);
    } else {
      strategy[index] = scaled;
    }
  }
}

using ComboPermutation = std::array<ComboId, combo_count>;

struct RangeAutomorphism {
  SuitPermutation suits{};
  ComboPermutation combos{};
};

struct DecisionLayout {
  std::uint64_t action_base{0};
  std::uint64_t physical_infoset_base{0};
  std::uint32_t board_index{0};
  std::uint32_t action_count : 4 {0};
  std::uint32_t player : 1 {0};
  std::uint32_t present : 1 {0};
  std::uint32_t terminal_child_mask : 8 {0};
  std::uint32_t paired_fold_action : 4 {maximum_action_count};
  std::uint32_t paired_showdown_action : 4 {maximum_action_count};
  std::uint32_t subtree_player_mask : 2 {0};
  std::uint32_t descendant_player_mask : 2 {0};
  std::uint32_t reserved : 6 {0};
};
static_assert(sizeof(DecisionLayout) == 24U);

struct CanonicalPublicOutcome {
  std::uint32_t child{0};
  CardId chance_card{};
  std::uint32_t physical_outcome_count{1};
  std::uint8_t physical_to_child_automorphism{0};
};

class CanonicalOutcomeList {
public:
  CanonicalOutcomeList() = default;

  CanonicalOutcomeList &operator=(std::vector<CanonicalPublicOutcome> &&values) {
    if (values.empty()) {
      storage_.template emplace<std::monostate>();
    } else if (values.size() == 1U) {
      storage_.template emplace<CanonicalPublicOutcome>(std::move(values.front()));
    } else {
      storage_.template emplace<std::vector<CanonicalPublicOutcome>>(
          std::move(values));
    }
    return *this;
  }

  void push_back(CanonicalPublicOutcome value) {
    if (std::holds_alternative<std::monostate>(storage_)) {
      storage_.template emplace<CanonicalPublicOutcome>(std::move(value));
      return;
    }
    if (auto *const inline_value =
            std::get_if<CanonicalPublicOutcome>(&storage_)) {
      std::vector<CanonicalPublicOutcome> values;
      values.reserve(2U);
      values.push_back(std::move(*inline_value));
      values.push_back(std::move(value));
      storage_.template emplace<std::vector<CanonicalPublicOutcome>>(
          std::move(values));
      return;
    }
    std::get<std::vector<CanonicalPublicOutcome>>(storage_)
        .push_back(std::move(value));
  }

  [[nodiscard]] bool empty() const noexcept { return size() == 0U; }

  [[nodiscard]] std::size_t size() const noexcept {
    if (std::holds_alternative<std::monostate>(storage_)) {
      return 0U;
    }
    if (std::holds_alternative<CanonicalPublicOutcome>(storage_)) {
      return 1U;
    }
    return std::get<std::vector<CanonicalPublicOutcome>>(storage_).size();
  }

  [[nodiscard]] CanonicalPublicOutcome &front() noexcept { return *data(); }
  [[nodiscard]] const CanonicalPublicOutcome &front() const noexcept {
    return *data();
  }
  [[nodiscard]] CanonicalPublicOutcome *begin() noexcept { return data(); }
  [[nodiscard]] CanonicalPublicOutcome *end() noexcept {
    auto *const first = data();
    return first == nullptr ? nullptr : first + size();
  }
  [[nodiscard]] const CanonicalPublicOutcome *begin() const noexcept {
    return data();
  }
  [[nodiscard]] const CanonicalPublicOutcome *end() const noexcept {
    const auto *const first = data();
    return first == nullptr ? nullptr : first + size();
  }

private:
  [[nodiscard]] CanonicalPublicOutcome *data() noexcept {
    if (auto *const inline_value =
            std::get_if<CanonicalPublicOutcome>(&storage_)) {
      return inline_value;
    }
    if (auto *const values =
            std::get_if<std::vector<CanonicalPublicOutcome>>(&storage_)) {
      return values->data();
    }
    return nullptr;
  }

  [[nodiscard]] const CanonicalPublicOutcome *data() const noexcept {
    if (const auto *const inline_value =
            std::get_if<CanonicalPublicOutcome>(&storage_)) {
      return inline_value;
    }
    if (const auto *const values =
            std::get_if<std::vector<CanonicalPublicOutcome>>(&storage_)) {
      return values->data();
    }
    return nullptr;
  }

  std::variant<std::monostate, CanonicalPublicOutcome,
               std::vector<CanonicalPublicOutcome>> storage_{};
};

struct CanonicalPublicEdge {
  Action action{};
  CanonicalOutcomeList outcomes;
};

struct CanonicalPublicNode {
  NodeId representative_node{0};
  PublicNodeKind kind{PublicNodeKind::Decision};
  std::uint32_t board_index{0};
  DecisionLayout decision{};
  std::uint32_t total_legal_outcome_count{0};
  std::uint32_t physical_path_multiplicity{1};
  std::vector<CanonicalPublicEdge> edges;
  std::uint32_t local_action_count{0};
  std::uint32_t state_scale_index{std::numeric_limits<std::uint32_t>::max()};
  std::array<double, 2> fold_payoff_antes{};
  std::array<std::array<double, 3>, 2> showdown_payoff_antes{};
};

struct CanonicalPublicAssignment {
  std::uint32_t node{0};
  std::uint8_t physical_to_canonical_automorphism{0};
};

struct CanonicalPublicGraph {
  std::uint32_t root{0};
  std::vector<CanonicalPublicNode> nodes;
  // Read-only bridge used by inspection APIs. Mutable CFR state remains
  // owned exclusively by nodes above; physical nodes never own or share it.
  std::vector<CanonicalPublicAssignment> physical_assignments;
};

struct TerminalComboData {
  static constexpr std::uint16_t invalid_slot = std::numeric_limits<std::uint16_t>::max();
  std::vector<std::uint16_t> own_slot;
  std::vector<std::uint16_t> opponent_slot;
  std::vector<std::uint16_t> opponent_local;
  std::vector<std::uint16_t> rank;
  std::vector<std::uint16_t> first_by_rank;
  std::vector<std::uint16_t> second_by_rank;
  std::vector<std::uint16_t> first_all;
  std::vector<std::uint16_t> second_all;
  // Unique cells of the rank-by-card accumulator touched by this combo set.
  // A showdown adds to two cells per combo, but many combos share the same
  // (rank, card) cell.  Resetting this compact list avoids replaying all
  // duplicate stores after every terminal evaluation.
  std::vector<std::uint16_t> touched_by_rank_card;
  std::vector<std::uint8_t> first_card;
  std::vector<std::uint8_t> second_card;

  void clear() {
    own_slot.clear();
    opponent_slot.clear();
    opponent_local.clear();
    rank.clear();
    first_by_rank.clear();
    second_by_rank.clear();
    first_all.clear();
    second_all.clear();
    touched_by_rank_card.clear();
    first_card.clear();
    second_card.clear();
  }

  void reserve(const std::size_t size) {
    own_slot.reserve(size);
    opponent_slot.reserve(size);
    opponent_local.reserve(size);
    rank.reserve(size);
    first_by_rank.reserve(size);
    second_by_rank.reserve(size);
    first_all.reserve(size);
    second_all.reserve(size);
    touched_by_rank_card.reserve(size * 2U);
    first_card.reserve(size);
    second_card.reserve(size);
  }

  [[nodiscard]] std::size_t size() const noexcept { return own_slot.size(); }

};

struct BoardData {
  std::uint64_t mask{0};
  std::array<std::int16_t, combo_count> local_index{};
  std::vector<ComboId> legal_combos;
  // Per-player live combos: only the acting player's own range combos are
  // stored as action slots at that player's decision nodes. This halves the
  // tree for asymmetric ranges without changing any reach-weighted result,
  // since combos outside the actor's range have zero actor reach.
  std::array<std::vector<ComboId>, 2> player_combos;
  // Slots of player_combos in the union-range traversal domain.  These are
  // immutable after layout construction and let AVX2 kernels gather dense
  // value/reach streams without repeating combo-id hash/table lookups.
  std::array<std::vector<std::uint16_t>, 2> player_active_slots;
  std::array<std::vector<std::uint16_t>, 2> player_flop_slots;
  std::array<std::array<std::vector<std::uint16_t>, 36U>, 2>
      chance_compatible_flop_slots;
  std::array<std::vector<std::uint16_t>, 2> player_opponent_flop_slots;
  std::array<std::array<std::int16_t, combo_count>, 2> player_local{};
  std::array<std::int16_t, combo_count> rank_index{};
  std::array<TerminalComboData, 2> terminal_combos;
  TerminalComboData terminal_active_combos;
  std::uint16_t rank_count{0};
  // Dense rank axis used by the per-player range-aware terminal kernels.
  // Ranks absent from both ranges have exactly zero mass and can be omitted
  // while preserving the ordering and equality of every reachable combo.
  std::uint16_t player_rank_count{0};
  bool ranks_ready{false};
};

struct PhysicalTerminalPayoff {
  // [player][0=win/fold, 1=tie, 2=loss]. Fold terminals use only column 0.
  std::array<std::array<double, 3>, 2> value_antes{};
};

struct DenseLayout {
  PublicTree tree;
  std::array<Combo, combo_count> combos{};
  std::array<std::uint64_t, combo_count> combo_masks{};
  std::array<DenseComboVector, 2> initial_reach{};
  std::array<double, 2> initial_pot_contribution_antes{};
  std::array<std::int16_t, combo_count> active_combo_index{};
  // Per-player flop-range combo spaces: the compact value/reach vectors used
  // by the physical (non-DAG) traversal are indexed by the updating player's
  // flop-range combos (player_flop_combos[player]), which are stable across
  // every deeper board (a combo is either still live, with the same slot, or
  // blocked with zero reach). player_flop_slot[player][combo] is the slot, or
  // -1 when the combo is outside the player's flop range.
  std::array<std::array<std::int16_t, combo_count>, 2> player_flop_slot{};
  std::array<std::vector<ComboId>, 2> player_flop_combos;
  std::array<std::size_t, 2> player_flop_count{};
  std::vector<ComboId> active_combos;
  std::array<std::vector<std::uint16_t>, 36U> active_slots_by_card;
  std::array<std::vector<ComboId>, 36U> active_combos_by_card;
  std::vector<std::vector<std::uint16_t>> active_automorphism_slots;
  std::vector<std::uint8_t> active_automorphism_is_identity;
  std::vector<BoardData> boards;
  std::vector<std::uint32_t> node_board;
  std::vector<std::uint32_t> node_terminal_payoff;
  std::vector<PhysicalTerminalPayoff> terminal_payoffs;
  std::vector<DecisionLayout> decisions;
  std::vector<std::uint32_t> physical_infoset_ids;
  std::vector<std::uint64_t> canonical_action_bases;
  std::vector<std::uint16_t> canonical_action_counts;
  std::vector<std::uint32_t> canonical_infoset_multiplicity;
  std::vector<RangeAutomorphism> automorphisms;
  CanonicalPublicGraph canonical_public_graph;
  std::uint64_t information_sets{0};
  std::uint64_t actions{0};
  std::uint64_t canonical_decision_nodes{0};
  double initial_normalization{0.0};
  bool uses_isomorphic_infosets{false};
  // True when the tree has only the identity automorphism (automorphisms.size()
  // <= 1): every physical infoset is unique, so each combo's action block is
  // laid out contiguously at decision.action_base + local * action_count and
  // decision_action_base can skip the two sparse canonical lookups.
  bool uses_direct_action_bases{false};
  bool uses_canonical_public_dag{false};
  bool uses_range_aware_physical_orbits{false};
  std::string fingerprint;
};

struct PhysicalOrbitOccurrence {
  NodeId node{};
  std::uint8_t physical_to_representative_automorphism{};
};
using PhysicalOrbitContext = std::vector<PhysicalOrbitOccurrence>;

struct NodeHistory {
  std::uint32_t betting_history{0};
  std::array<CardId, 2> chance_cards{};
  std::uint8_t chance_count{0};
};

struct ActionHistoryKey {
  std::uint32_t parent{0};
  std::int64_t amount_units{0};
  std::uint32_t requested_basis_points{0};
  std::uint8_t edge_kind{0};
  std::uint8_t action_type{0};
  std::uint8_t all_in_kind{0};

  friend bool operator==(const ActionHistoryKey &, const ActionHistoryKey &) = default;
};

struct ActionHistoryKeyHash {
  std::size_t operator()(const ActionHistoryKey &key) const noexcept {
    std::uint64_t hash = static_cast<std::uint64_t>(key.parent) << 32U;
    hash ^= static_cast<std::uint64_t>(key.amount_units);
    hash ^= static_cast<std::uint64_t>(key.requested_basis_points) << 8U;
    hash ^= static_cast<std::uint64_t>(key.edge_kind) << 56U;
    hash ^= static_cast<std::uint64_t>(key.action_type) << 48U;
    hash ^= static_cast<std::uint64_t>(key.all_in_kind) << 40U;
    hash ^= hash >> 33U;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33U;
    return static_cast<std::size_t>(hash);
  }
};

struct CanonicalInfosetKey {
  std::uint32_t public_history{0};
  std::uint64_t board_mask{0};
  std::uint16_t private_combo{0};
  std::uint16_t ordered_chance_cards{0};

  friend bool operator==(const CanonicalInfosetKey &, const CanonicalInfosetKey &) = default;
};

struct CanonicalInfosetKeyHash {
  std::size_t operator()(const CanonicalInfosetKey &key) const noexcept {
    std::uint64_t hash = key.board_mask ^ (static_cast<std::uint64_t>(key.public_history) << 32U);
    hash ^= static_cast<std::uint64_t>(key.private_combo) << 16U;
    hash ^= key.ordered_chance_cards;
    hash ^= hash >> 33U;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33U;
    return static_cast<std::size_t>(hash);
  }
};

struct CanonicalInfosetEntry {
  std::uint32_t id{0};
  NodeId representative_node{0};
};

struct CanonicalPublicKey {
  std::uint32_t public_history{0};
  std::uint64_t board_mask{0};
  std::uint16_t ordered_chance_cards{0};

  friend bool operator==(const CanonicalPublicKey &, const CanonicalPublicKey &) = default;
};

struct CanonicalPublicKeyHash {
  std::size_t operator()(const CanonicalPublicKey &key) const noexcept {
    std::uint64_t hash = key.board_mask ^ (static_cast<std::uint64_t>(key.public_history) << 32U);
    hash ^= key.ordered_chance_cards;
    hash ^= hash >> 33U;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33U;
    return static_cast<std::size_t>(hash);
  }
};

template <typename Value> bool finite_vector(const std::vector<Value> &values) {
  return std::all_of(values.begin(), values.end(),
                     [](const Value value) { return std::isfinite(value); });
}

bool finite_float24_vector(const std::vector<std::uint8_t> &values) {
  if (values.size() % 3U != 0U) {
    return false;
  }
  for (std::size_t offset = 0; offset < values.size(); offset += 3U) {
    if (!std::isfinite(decode_float24(values.data() + offset))) {
      return false;
    }
  }
  return true;
}

bool finite_compact_vector(const std::vector<std::uint8_t> &values) {
  if (values.size() % 3U != 0U) {
    return false;
  }
  for (std::size_t offset = 0; offset < values.size(); offset += 3U) {
    const auto word = compact_word(values.data() + offset);
    const auto regret = static_cast<std::uint16_t>(word & 0x1fffU);
    const auto strategy = static_cast<std::uint16_t>(word >> 13U);
    if (regret == 0x1fffU || !std::isfinite(decode_regret13(regret)) ||
        !std::isfinite(decode_strategy11(strategy))) {
      return false;
    }
  }
  return true;
}

std::string fingerprint_text(const std::string_view text) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char byte : text) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

std::string serialize_range_fingerprint(const PostflopRanges &ranges) {
  std::string result;
  result.reserve(2U * combo_count * 6U);
  for (const auto &range : ranges.players) {
    for (const auto weight : range) {
      result += std::to_string(weight.basis_points());
      result.push_back(',');
    }
    result.push_back('|');
  }
  return result;
}

bool uniform_full_ranges(const PostflopRanges &ranges) {
  return std::all_of(ranges.players.begin(), ranges.players.end(), [](const auto &range) {
    return std::all_of(range.begin(), range.end(),
                       [](const auto weight) { return weight.basis_points() == 10'000U; });
  });
}

using ComboLookup = std::array<std::array<std::int16_t, 36>, 36>;

ComboLookup make_combo_lookup(const std::array<Combo, combo_count> &combos) {
  ComboLookup lookup{};
  for (auto &row : lookup) {
    row.fill(-1);
  }
  for (std::size_t index = 0; index < combos.size(); ++index) {
    const auto first = std::min(combos[index].first.value(), combos[index].second.value());
    const auto second = std::max(combos[index].first.value(), combos[index].second.value());
    lookup[first][second] = static_cast<std::int16_t>(index);
  }
  return lookup;
}

Result<std::vector<RangeAutomorphism>, PostflopSolverError>
range_automorphisms(const std::array<Combo, combo_count> &combos, const PostflopRanges &ranges,
                    const std::uint64_t initial_board_mask) {
  const auto lookup = make_combo_lookup(combos);
  std::vector<RangeAutomorphism> automorphisms;
  for (const auto &permutation : all_suit_permutations()) {
    const auto transformed_board = transform_card_mask(initial_board_mask, permutation);
    if (!transformed_board || transformed_board.value() != initial_board_mask) {
      continue;
    }
    ComboPermutation mapping{};
    bool invariant = true;
    for (std::size_t combo = 0; combo < combos.size(); ++combo) {
      const auto transformed = transform_combo(combos[combo], permutation);
      if (!transformed) {
        return Result<std::vector<RangeAutomorphism>, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      const auto first =
          std::min(transformed.value().first.value(), transformed.value().second.value());
      const auto second =
          std::max(transformed.value().first.value(), transformed.value().second.value());
      const auto transformed_id = lookup[first][second];
      if (transformed_id < 0) {
        return Result<std::vector<RangeAutomorphism>, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      mapping[combo] = static_cast<ComboId>(transformed_id);
      for (std::size_t player = 0; player < ranges.players.size(); ++player) {
        if (ranges.players[player][combo] !=
            ranges.players[player][static_cast<std::size_t>(transformed_id)]) {
          invariant = false;
          break;
        }
      }
      if (!invariant) {
        break;
      }
    }
    if (invariant) {
      automorphisms.push_back(RangeAutomorphism{permutation, mapping});
    }
  }
  if (automorphisms.empty()) {
    return Result<std::vector<RangeAutomorphism>, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  return Result<std::vector<RangeAutomorphism>, PostflopSolverError>::success(
      std::move(automorphisms));
}

Result<std::vector<NodeHistory>, PostflopSolverError> build_node_histories(const PublicTree &tree) {
  std::vector<NodeHistory> histories(tree.nodes.size());
  std::vector<bool> assigned(tree.nodes.size(), false);
  std::unordered_map<ActionHistoryKey, std::uint32_t, ActionHistoryKeyHash> history_ids;
  history_ids.reserve(tree.nodes.size());
  assigned[static_cast<std::size_t>(tree.root)] = true;
  for (const auto &node : tree.nodes) {
    if (!assigned[static_cast<std::size_t>(node.id)]) {
      return Result<std::vector<NodeHistory>, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    for (const auto &edge : node.edges) {
      if (edge.child >= histories.size()) {
        return Result<std::vector<NodeHistory>, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      auto child = histories[static_cast<std::size_t>(node.id)];
      const ActionHistoryKey history_key{
          child.betting_history,
          edge.kind == PublicEdgeKind::Action ? edge.action.amount.units() : 0,
          edge.kind == PublicEdgeKind::Action ? edge.action.requested_basis_points : 0U,
          static_cast<std::uint8_t>(edge.kind),
          edge.kind == PublicEdgeKind::Action ? static_cast<std::uint8_t>(edge.action.type) : 0U,
          edge.kind == PublicEdgeKind::Action ? static_cast<std::uint8_t>(edge.action.all_in_kind)
                                              : 0U};
      const auto next_history_id = static_cast<std::uint32_t>(history_ids.size() + 1U);
      child.betting_history = history_ids.emplace(history_key, next_history_id).first->second;
      if (edge.kind == PublicEdgeKind::ChanceCard) {
        if (child.chance_count >= child.chance_cards.size()) {
          return Result<std::vector<NodeHistory>, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        child.chance_cards[child.chance_count++] = edge.chance_card;
      }
      histories[static_cast<std::size_t>(edge.child)] = std::move(child);
      assigned[static_cast<std::size_t>(edge.child)] = true;
    }
  }
  return Result<std::vector<NodeHistory>, PostflopSolverError>::success(std::move(histories));
}

std::vector<std::uint32_t> intern_public_histories(const PublicTree &tree,
                                                   const std::vector<NodeHistory> &histories) {
  std::unordered_map<std::string, std::uint32_t> ids;
  std::vector<std::uint32_t> result(tree.nodes.size());
  for (const auto &node : tree.nodes) {
    auto state = node.state;
    state.board_mask = 0U;
    auto signature = serialize_public_state(state);
    signature.push_back('|');
    signature += std::to_string(histories[static_cast<std::size_t>(node.id)].betting_history);
    const auto next_id = static_cast<std::uint32_t>(ids.size());
    result[static_cast<std::size_t>(node.id)] =
        ids.emplace(std::move(signature), next_id).first->second;
  }
  return result;
}

bool same_actions(const PublicTreeNode &left, const PublicTreeNode &right) {
  if (left.edges.size() != right.edges.size()) {
    return false;
  }
  for (std::size_t action = 0; action < left.edges.size(); ++action) {
    if (left.edges[action].kind != PublicEdgeKind::Action ||
        right.edges[action].kind != PublicEdgeKind::Action ||
        left.edges[action].action != right.edges[action].action) {
      return false;
    }
  }
  return true;
}

std::uint16_t transformed_ordered_chance_cards(const NodeHistory &history,
                                               const SuitPermutation &permutation) {
  std::uint16_t packed = static_cast<std::uint16_t>(history.chance_count) << 12U;
  for (std::size_t index = 0; index < history.chance_count; ++index) {
    const auto transformed = transform_card(history.chance_cards[index], permutation).value();
    packed |= static_cast<std::uint16_t>(transformed.value()) << (index == 0U ? 6U : 0U);
  }
  return packed;
}

Result<CanonicalPublicGraph, PostflopSolverError>
build_canonical_public_graph(const PublicTree &tree, const std::vector<NodeHistory> &histories,
                             const std::vector<std::uint32_t> &public_history_ids,
                             const std::vector<RangeAutomorphism> &automorphisms) {
  if (histories.size() != tree.nodes.size() || public_history_ids.size() != tree.nodes.size() ||
      automorphisms.empty() || automorphisms.size() > 24U) {
    return Result<CanonicalPublicGraph, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  std::size_t identity = automorphisms.size();
  const SuitPermutation identity_permutation{};
  for (std::size_t index = 0; index < automorphisms.size(); ++index) {
    if (automorphisms[index].suits == identity_permutation) {
      identity = index;
      break;
    }
  }
  if (identity == automorphisms.size()) {
    return Result<CanonicalPublicGraph, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }

  (void)histories;
  (void)public_history_ids;
  CanonicalPublicGraph graph;
  using Stabilizer = std::vector<std::uint8_t>;
  std::function<Result<std::uint32_t, PostflopSolverError>(NodeId, const Stabilizer &,
                                                           std::uint32_t)>
      build;
  build = [&](const NodeId physical_id, const Stabilizer &stabilizer,
              const std::uint32_t path_multiplicity)
      -> Result<std::uint32_t, PostflopSolverError> {
    if (physical_id >= tree.nodes.size() || stabilizer.empty()) {
      return Result<std::uint32_t, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    const auto &physical = tree.nodes[static_cast<std::size_t>(physical_id)];
    const auto node_id = static_cast<std::uint32_t>(graph.nodes.size());
    graph.nodes.emplace_back();
    graph.nodes[node_id].representative_node = physical_id;
    graph.nodes[node_id].kind = physical.kind;
    graph.nodes[node_id].physical_path_multiplicity = path_multiplicity;

    if (physical.kind == PublicNodeKind::Decision) {
      std::vector<CanonicalPublicEdge> edges;
      edges.reserve(physical.edges.size());
      for (const auto &edge : physical.edges) {
        if (edge.kind != PublicEdgeKind::Action) {
          return Result<std::uint32_t, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        auto child = build(edge.child, stabilizer, path_multiplicity);
        if (!child) {
          return child;
        }
        CanonicalPublicEdge canonical_edge;
        canonical_edge.action = edge.action;
        canonical_edge.outcomes.push_back(
            {child.value(), CardId{}, 1U, static_cast<std::uint8_t>(identity)});
        edges.push_back(std::move(canonical_edge));
      }
      graph.nodes[node_id].edges = std::move(edges);
    } else if (physical.kind == PublicNodeKind::Chance) {
      std::array<const PublicTreeEdge *, 36U> edge_by_card{};
      for (const auto &edge : physical.edges) {
        if (edge.kind != PublicEdgeKind::ChanceCard) {
          return Result<std::uint32_t, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        edge_by_card[edge.chance_card.value()] = &edge;
      }
      std::array<bool, 36U> consumed{};
      std::vector<CanonicalPublicEdge> edges;
      for (const auto &candidate_edge : physical.edges) {
        const auto candidate = candidate_edge.chance_card.value();
        if (consumed[candidate]) {
          continue;
        }
        std::uint8_t representative = candidate;
        for (const auto automorphism : stabilizer) {
          const auto transformed =
              transform_card(candidate_edge.chance_card, automorphisms[automorphism].suits);
          if (!transformed || edge_by_card[transformed.value().value()] == nullptr) {
            return Result<std::uint32_t, PostflopSolverError>::failure(
                PostflopSolverError::InvalidConfiguration);
          }
          representative = std::min(representative, transformed.value().value());
        }
        const auto *const representative_edge = edge_by_card[representative];
        if (representative_edge == nullptr) {
          return Result<std::uint32_t, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        Stabilizer child_stabilizer;
        for (const auto automorphism : stabilizer) {
          const auto transformed = transform_card(representative_edge->chance_card,
                                                  automorphisms[automorphism].suits);
          if (transformed && transformed.value() == representative_edge->chance_card) {
            child_stabilizer.push_back(automorphism);
          }
        }
        std::vector<CanonicalPublicOutcome> outcomes;
        std::uint32_t orbit_multiplicity = 0U;
        for (const auto &edge : physical.edges) {
          std::uint8_t maps_to_representative = static_cast<std::uint8_t>(identity);
          bool in_orbit = edge.chance_card == representative_edge->chance_card;
          if (!in_orbit) {
            for (const auto automorphism : stabilizer) {
              const auto transformed =
                  transform_card(edge.chance_card, automorphisms[automorphism].suits);
              if (transformed && transformed.value() == representative_edge->chance_card) {
                maps_to_representative = automorphism;
                in_orbit = true;
                break;
              }
            }
          }
          if (!in_orbit) {
            continue;
          }
          consumed[edge.chance_card.value()] = true;
          orbit_multiplicity += edge.physical_outcome_count;
          outcomes.push_back({0U, edge.chance_card, edge.physical_outcome_count,
                              maps_to_representative});
        }
        if (outcomes.empty() || orbit_multiplicity == 0U ||
            path_multiplicity > std::numeric_limits<std::uint32_t>::max() /
                                    orbit_multiplicity) {
          return Result<std::uint32_t, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        std::ranges::sort(outcomes, [&](const auto &left, const auto &right) {
          const auto left_key = std::pair{
              left.chance_card == representative_edge->chance_card ? 0U : 1U,
              static_cast<unsigned>(left.chance_card.value())};
          const auto right_key = std::pair{
              right.chance_card == representative_edge->chance_card ? 0U : 1U,
              static_cast<unsigned>(right.chance_card.value())};
          return left_key < right_key;
        });
        auto child = build(representative_edge->child, child_stabilizer,
                           path_multiplicity * orbit_multiplicity);
        if (!child) {
          return child;
        }
        for (auto &outcome : outcomes) {
          outcome.child = child.value();
        }
        CanonicalPublicEdge canonical_edge;
        canonical_edge.outcomes = std::move(outcomes);
        edges.push_back(std::move(canonical_edge));
      }
      graph.nodes[node_id].edges = std::move(edges);
    }
    return Result<std::uint32_t, PostflopSolverError>::success(node_id);
  };

  Stabilizer root_stabilizer;
  root_stabilizer.reserve(automorphisms.size());
  for (std::size_t index = 0; index < automorphisms.size(); ++index) {
    root_stabilizer.push_back(static_cast<std::uint8_t>(index));
  }
  auto root = build(tree.root, root_stabilizer, 1U);
  if (!root) {
    return Result<CanonicalPublicGraph, PostflopSolverError>::failure(root.error());
  }
  graph.root = root.value();
  const auto canonical_assignment = [&](const PublicTreeNode &physical) {
    CanonicalPublicKey canonical{};
    std::uint8_t selected = static_cast<std::uint8_t>(identity);
    bool initialized = false;
    for (std::size_t index = 0; index < automorphisms.size(); ++index) {
      const auto board = transform_card_mask(physical.state.board_mask,
                                             automorphisms[index].suits);
      if (!board) {
        continue;
      }
      const CanonicalPublicKey candidate{
          public_history_ids[static_cast<std::size_t>(physical.id)], board.value(),
          transformed_ordered_chance_cards(histories[static_cast<std::size_t>(physical.id)],
                                           automorphisms[index].suits)};
      if (!initialized ||
          std::tie(candidate.public_history, candidate.board_mask,
                   candidate.ordered_chance_cards) <
              std::tie(canonical.public_history, canonical.board_mask,
                       canonical.ordered_chance_cards)) {
        canonical = candidate;
        selected = static_cast<std::uint8_t>(index);
        initialized = true;
      }
    }
    return std::pair{canonical, selected};
  };
  std::unordered_map<CanonicalPublicKey, std::uint32_t, CanonicalPublicKeyHash> by_key;
  by_key.reserve(graph.nodes.size());
  for (std::uint32_t node = 0U; node < graph.nodes.size(); ++node) {
    const auto [key, ignored] = canonical_assignment(
        tree.nodes[static_cast<std::size_t>(graph.nodes[node].representative_node)]);
    (void)ignored;
    if (!by_key.emplace(key, node).second) {
      return Result<CanonicalPublicGraph, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
  }
  graph.physical_assignments.resize(tree.nodes.size());
  for (const auto &physical : tree.nodes) {
    const auto [key, automorphism] = canonical_assignment(physical);
    const auto found = by_key.find(key);
    if (found == by_key.end()) {
      return Result<CanonicalPublicGraph, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    graph.physical_assignments[static_cast<std::size_t>(physical.id)] = {
        found->second, automorphism};
  }
  return Result<CanonicalPublicGraph, PostflopSolverError>::success(std::move(graph));
}

Result<CanonicalPublicGraph, PostflopSolverError>
build_direct_canonical_public_graph(const PublicTree &tree,
                                    const std::vector<RangeAutomorphism> &automorphisms) {
  if (automorphisms.empty()) {
    return Result<CanonicalPublicGraph, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  std::size_t identity = automorphisms.size();
  const SuitPermutation identity_permutation{};
  for (std::size_t index = 0; index < automorphisms.size(); ++index) {
    if (automorphisms[index].suits == identity_permutation) {
      identity = index;
      break;
    }
  }
  if (identity == automorphisms.size()) {
    return Result<CanonicalPublicGraph, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  CanonicalPublicGraph graph;
  graph.root = static_cast<std::uint32_t>(tree.root);
  graph.nodes.resize(tree.nodes.size());
  graph.physical_assignments.resize(tree.nodes.size());
  for (const auto &source : tree.nodes) {
    if (source.id >= graph.nodes.size()) {
      return Result<CanonicalPublicGraph, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    auto &target = graph.nodes[static_cast<std::size_t>(source.id)];
    target.representative_node = source.id;
    target.kind = source.kind;
    target.edges.reserve(source.edges.size());
    graph.physical_assignments[static_cast<std::size_t>(source.id)] = {
        static_cast<std::uint32_t>(source.id), static_cast<std::uint8_t>(identity)};
    for (const auto &source_edge : source.edges) {
      CanonicalPublicEdge edge;
      edge.action = source_edge.action;
      if (source_edge.kind == PublicEdgeKind::Action) {
        edge.outcomes.push_back({static_cast<std::uint32_t>(source_edge.child), CardId{}, 1U,
                                 static_cast<std::uint8_t>(identity)});
      } else {
        if (source_edge.chance_outcome_count == 0U ||
            source_edge.chance_outcome_count > source_edge.chance_outcomes.size()) {
          return Result<CanonicalPublicGraph, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        for (std::size_t outcome = 0U; outcome < source_edge.chance_outcome_count; ++outcome) {
          const auto &mapping = source_edge.chance_outcomes[outcome];
          if (mapping.physical_to_representative_permutation >= automorphisms.size()) {
            return Result<CanonicalPublicGraph, PostflopSolverError>::failure(
                PostflopSolverError::InvalidConfiguration);
          }
          edge.outcomes.push_back({static_cast<std::uint32_t>(source_edge.child), mapping.card, 1U,
                                   mapping.physical_to_representative_permutation});
        }
      }
      target.edges.push_back(std::move(edge));
    }
  }
  return Result<CanonicalPublicGraph, PostflopSolverError>::success(std::move(graph));
}

std::uint64_t decision_action_base(const DenseLayout &layout, const DecisionLayout &decision,
                                   const std::int16_t local_combo) {
  if (layout.uses_direct_action_bases) {
    return decision.action_base + static_cast<std::uint64_t>(local_combo) * decision.action_count;
  }
  if (!layout.uses_isomorphic_infosets) {
    return decision.action_base + static_cast<std::uint64_t>(local_combo) * decision.action_count;
  }
  const auto infoset_id = layout.physical_infoset_ids[decision.physical_infoset_base +
                                                      static_cast<std::uint64_t>(local_combo)];
  return layout.canonical_action_bases[infoset_id];
}

std::uint32_t decision_infoset_id(const DenseLayout &layout, const DecisionLayout &decision,
                                  const std::int16_t local_combo) {
  return layout.physical_infoset_ids[decision.physical_infoset_base +
                                     static_cast<std::uint64_t>(local_combo)];
}

std::uint64_t canonical_action_base(const CanonicalPublicNode &canonical,
                                    const std::int16_t local_combo) {
  return canonical.decision.action_base +
         static_cast<std::uint64_t>(local_combo) * canonical.decision.action_count;
}

[[nodiscard]] std::size_t canonical_action_major_index(
    const CanonicalPublicNode &canonical, const std::size_t local_combo,
    const std::size_t action) noexcept {
  const auto action_count = static_cast<std::size_t>(canonical.decision.action_count);
  const auto local_count =
      static_cast<std::size_t>(canonical.local_action_count) / action_count;
  return static_cast<std::size_t>(canonical.decision.action_base) +
         action * local_count + local_combo;
}

std::vector<CardId> cards_from_mask(const std::uint64_t mask) {
  std::vector<CardId> cards;
  cards.reserve(static_cast<std::size_t>(std::popcount(mask)));
  for (std::uint8_t index = 0; index < 36U; ++index) {
    if ((mask & (std::uint64_t{1} << index)) != 0U) {
      cards.push_back(CardId::from_index(index).value());
    }
  }
  return cards;
}

Result<DenseLayout, PostflopSolverError>
build_layout(const PostflopTreeConfig &config, const PostflopRanges &ranges,
             const bool enable_lossless_isomorphism = true,
             const bool enable_canonical_public_dag = false,
             const bool preserve_physical_tree = false) {
  // Inspection APIs receive public node ids from build_public_tree(). Keep
  // their short-lived analysis layout on the physical tree so those stable
  // ids and paths remain valid. Solving/benchmark layouts use the direct
  // canonical tree and never materialize the physical chance expansion.
  const bool direct_canonical_tree = enable_canonical_public_dag && !preserve_physical_tree;
  const auto source_combos = all_combos();
  std::vector<RangeAutomorphism> direct_automorphisms;
  TreeBuildOptions options;
  options.maximum_nodes = std::numeric_limits<std::uint64_t>::max();
  PublicTreeStats physical_tree_stats{};
  if (direct_canonical_tree) {
    const auto initial_cards = configured_board(config);
    const auto initial_mask = card_mask(initial_cards);
    if (!initial_mask) {
      return Result<DenseLayout, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    auto exact_automorphisms =
        range_automorphisms(source_combos, ranges, initial_mask.value());
    if (!exact_automorphisms) {
      return Result<DenseLayout, PostflopSolverError>::failure(exact_automorphisms.error());
    }
    direct_automorphisms = std::move(exact_automorphisms.value());
    if (!enable_lossless_isomorphism) {
      const SuitPermutation identity_permutation{};
      const auto identity =
          std::ranges::find_if(direct_automorphisms, [&](const auto &automorphism) {
            return automorphism.suits == identity_permutation;
          });
      if (identity == direct_automorphisms.end()) {
        return Result<DenseLayout, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      auto identity_automorphism = *identity;
      direct_automorphisms.clear();
      direct_automorphisms.push_back(std::move(identity_automorphism));
    }
    const auto canonical_estimate = estimate_canonical_chance_layout(config, ranges);
    if (!canonical_estimate) {
      return Result<DenseLayout, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    physical_tree_stats = canonical_estimate.value().physical_public_tree;
    options.reserve_nodes = canonical_estimate.value().canonical_public_nodes;
    options.canonical_chance_permutations.reserve(direct_automorphisms.size());
    for (const auto &automorphism : direct_automorphisms) {
      std::array<std::uint8_t, 4> permutation{};
      for (std::size_t suit = 0U; suit < permutation.size(); ++suit) {
        permutation[suit] =
            static_cast<std::uint8_t>(automorphism.suits.forward[suit]);
      }
      options.canonical_chance_permutations.push_back(permutation);
    }
  }
  auto tree = build_public_tree(config, options);
  if (!tree) {
    return Result<DenseLayout, PostflopSolverError>::failure(PostflopSolverError::TreeFailure);
  }

  DenseLayout layout;
  layout.tree = std::move(tree.value());
  const auto &root_state =
      layout.tree.nodes[static_cast<std::size_t>(layout.tree.root)].state;
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    layout.initial_pot_contribution_antes[player] =
        static_cast<double>(root_state.initial_pot_contributions[player].units()) /
        units_per_ante;
  }
  constexpr auto invalid_terminal_payoff = std::numeric_limits<std::uint32_t>::max();
  layout.node_terminal_payoff.assign(layout.tree.nodes.size(), invalid_terminal_payoff);
  layout.terminal_payoffs.reserve(layout.tree.nodes.size() / 2U);
  for (const auto &node : layout.tree.nodes) {
    if (node.kind != PublicNodeKind::TerminalFold &&
        node.kind != PublicNodeKind::TerminalShowdown) {
      continue;
    }
    const auto payoff_index = static_cast<std::uint32_t>(layout.terminal_payoffs.size());
    layout.node_terminal_payoff[static_cast<std::size_t>(node.id)] = payoff_index;
    auto &payoff = layout.terminal_payoffs.emplace_back();
    if (node.kind == PublicNodeKind::TerminalFold) {
      const auto settlement = settle_terminal(node.state, layout.tree.config.rake);
      if (!settlement) {
        return Result<DenseLayout, PostflopSolverError>::failure(
            PostflopSolverError::SettlementFailure);
      }
      for (std::uint8_t player = 0; player < 2U; ++player) {
        payoff.value_antes[player][0] =
            static_cast<double>(settlement.value().payoff_units[player]) / units_per_ante;
      }
      continue;
    }
    const std::array<std::uint8_t, 3> winner_masks{0b01U, 0b11U, 0b10U};
    for (std::size_t outcome = 0; outcome < winner_masks.size(); ++outcome) {
      const auto settlement =
          settle_terminal(node.state, layout.tree.config.rake, winner_masks[outcome]);
      if (!settlement) {
        return Result<DenseLayout, PostflopSolverError>::failure(
            PostflopSolverError::SettlementFailure);
      }
      payoff.value_antes[0][outcome] =
          static_cast<double>(settlement.value().payoff_units[0]) / units_per_ante;
      payoff.value_antes[1][2U - outcome] =
          static_cast<double>(settlement.value().payoff_units[1]) / units_per_ante;
    }
  }
  layout.active_combo_index.fill(-1);  layout.combos = source_combos;
  for (std::size_t combo = 0; combo < combo_count; ++combo) {
    layout.combo_masks[combo] =
        layout.combos[combo].first.mask() | layout.combos[combo].second.mask();
  }
  layout.node_board.resize(layout.tree.nodes.size());
  layout.decisions.resize(layout.tree.nodes.size());
  std::unordered_map<std::uint64_t, std::uint32_t> board_lookup;

  for (const auto &node : layout.tree.nodes) {
    auto found = board_lookup.find(node.state.board_mask);
    if (found == board_lookup.end()) {
      BoardData board;
      board.mask = node.state.board_mask;
      board.local_index.fill(-1);
      for (auto &player_local : board.player_local) {
        player_local.fill(-1);
      }
      board.rank_index.fill(-1);
      for (std::size_t combo = 0; combo < combo_count; ++combo) {
        const bool present_in_source_range = ranges.players[0][combo].basis_points() != 0U ||
                                             ranges.players[1][combo].basis_points() != 0U;
        if (present_in_source_range && (layout.combo_masks[combo] & board.mask) == 0U) {
          board.local_index[combo] = static_cast<std::int16_t>(board.legal_combos.size());
          board.legal_combos.push_back(static_cast<ComboId>(combo));
        }
        for (std::size_t player = 0; player < 2U; ++player) {
          if (ranges.players[player][combo].basis_points() != 0U &&
              (layout.combo_masks[combo] & board.mask) == 0U) {
            board.player_local[player][combo] =
                static_cast<std::int16_t>(board.player_combos[player].size());
            board.player_combos[player].push_back(static_cast<ComboId>(combo));
          }
        }
      }
      const auto board_index = static_cast<std::uint32_t>(layout.boards.size());
      layout.boards.push_back(std::move(board));
      found = board_lookup.emplace(node.state.board_mask, board_index).first;
    }
    layout.node_board[static_cast<std::size_t>(node.id)] = found->second;
  }

  layout.uses_isomorphic_infosets = enable_lossless_isomorphism && !uniform_full_ranges(ranges);
  std::vector<NodeHistory> histories;
  std::vector<std::uint32_t> public_history_ids;
  std::vector<RangeAutomorphism> automorphisms = direct_automorphisms;
  std::unordered_map<CanonicalInfosetKey, CanonicalInfosetEntry, CanonicalInfosetKeyHash>
      canonical_infosets;
  if (!direct_canonical_tree &&
      (layout.uses_isomorphic_infosets || enable_canonical_public_dag)) {
    // Automorphisms first: the per-player direct-action-bases path (identity
    // automorphism group only) never uses the canonical infoset map or the
    // canonical arrays (decision_action_base computes the offset directly from
    // the local combo index), so skip the histories/interning/canonicalization
    // entirely for it. For th7d6s this avoids a ~2.5 GB transient
    // unordered_map (36.6M canonical infosets) and ~0.5 GB of unused arrays,
    // cutting the peak RSS from ~4.3 GB to ~1.5-2 GB.
    const auto initial_board =
        layout.tree.nodes[static_cast<std::size_t>(layout.tree.root)].state.board_mask;
    auto exact_automorphisms = range_automorphisms(layout.combos, ranges, initial_board);
    if (!exact_automorphisms) {
      return Result<DenseLayout, PostflopSolverError>::failure(exact_automorphisms.error());
    }
    automorphisms = std::move(exact_automorphisms.value());
    if (!enable_lossless_isomorphism) {
      const SuitPermutation identity_permutation{};
      const auto identity = std::ranges::find_if(automorphisms, [&](const auto &automorphism) {
        return automorphism.suits == identity_permutation;
      });
      if (identity == automorphisms.end()) {
        return Result<DenseLayout, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      auto identity_automorphism = *identity;
      automorphisms.clear();
      automorphisms.push_back(std::move(identity_automorphism));
    }
    layout.automorphisms = automorphisms;
    layout.uses_direct_action_bases = automorphisms.size() <= 1U;
    if (enable_canonical_public_dag ||
        (layout.uses_isomorphic_infosets && !layout.uses_direct_action_bases)) {
      auto built_histories = build_node_histories(layout.tree);
      if (!built_histories) {
        return Result<DenseLayout, PostflopSolverError>::failure(built_histories.error());
      }
      histories = std::move(built_histories.value());
      public_history_ids = intern_public_histories(layout.tree, histories);
      canonical_infosets.reserve(layout.tree.stats.decision_nodes * 8U);
    }
  } else if (direct_canonical_tree) {
    layout.automorphisms = automorphisms;
    layout.uses_direct_action_bases = true;
  }

  for (const auto &node : layout.tree.nodes) {
    if (node.kind != PublicNodeKind::Decision) {
      continue;
    }
    auto &decision = layout.decisions[static_cast<std::size_t>(node.id)];
    decision.board_index = layout.node_board[static_cast<std::size_t>(node.id)];
    decision.action_count = static_cast<std::uint16_t>(node.edges.size());
    decision.player = node.state.player_to_act;
    decision.present = true;
    const auto &board = layout.boards[decision.board_index];
    const auto legal_count = board.player_combos[decision.player].size();
    if (decision.action_count == 0U || decision.action_count > maximum_action_count) {
      return Result<DenseLayout, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    std::uint8_t first_fold = static_cast<std::uint8_t>(maximum_action_count);
    std::uint8_t first_showdown = static_cast<std::uint8_t>(maximum_action_count);
    for (std::size_t action = 0; action < node.edges.size(); ++action) {
      const auto child_kind =
          layout.tree.nodes[static_cast<std::size_t>(node.edges[action].child)].kind;
      if (child_kind == PublicNodeKind::TerminalFold ||
          child_kind == PublicNodeKind::TerminalShowdown) {
        decision.terminal_child_mask |= static_cast<std::uint8_t>(1U << action);
      }
      if (child_kind == PublicNodeKind::TerminalFold &&
          first_fold == maximum_action_count) {
        first_fold = static_cast<std::uint8_t>(action);
      } else if (child_kind == PublicNodeKind::TerminalShowdown &&
                 first_showdown == maximum_action_count) {
        first_showdown = static_cast<std::uint8_t>(action);
      }
    }
    if (first_fold != maximum_action_count && first_showdown != maximum_action_count) {
      decision.paired_fold_action = first_fold;
      decision.paired_showdown_action = first_showdown;
    }
    if (direct_canonical_tree) {
      continue;
    }
    if (!layout.uses_isomorphic_infosets || layout.uses_direct_action_bases) {
      // Simple per-combo action blocks: for the identity-only automorphism
      // group every combo is its own infoset and the canonical infoset
      // machinery (map, canonical arrays, physical_infoset_ids) is never read
      // by this path, so the counters match the canonical construction
      // (legal_count infosets, legal_count * action_count actions) without
      // building any of it.
      if (legal_count >
          (std::numeric_limits<std::uint64_t>::max() - layout.actions) / decision.action_count) {
        return Result<DenseLayout, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      decision.action_base = layout.actions;
      layout.information_sets += legal_count;
      layout.actions += legal_count * decision.action_count;
      continue;
    }

    decision.physical_infoset_base = layout.physical_infoset_ids.size();
    std::array<std::uint64_t, 24> transformed_board_masks{};
    std::array<std::uint16_t, 24> transformed_chance_histories{};
    for (std::size_t index = 0; index < automorphisms.size(); ++index) {
      const auto transformed_board = transform_card_mask(board.mask, automorphisms[index].suits);
      if (!transformed_board) {
        return Result<DenseLayout, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      transformed_board_masks[index] = transformed_board.value();
      transformed_chance_histories[index] = transformed_ordered_chance_cards(
          histories[static_cast<std::size_t>(node.id)], automorphisms[index].suits);
    }
    for (const ComboId combo : board.player_combos[decision.player]) {
      CanonicalInfosetKey canonical{};
      bool has_canonical = false;
      for (std::size_t index = 0; index < automorphisms.size(); ++index) {
        const CanonicalInfosetKey candidate{
            public_history_ids[static_cast<std::size_t>(node.id)], transformed_board_masks[index],
            automorphisms[index].combos[combo], transformed_chance_histories[index]};
        if (!has_canonical ||
            std::tie(candidate.public_history, candidate.board_mask, candidate.private_combo,
                     candidate.ordered_chance_cards) <
                std::tie(canonical.public_history, canonical.board_mask, canonical.private_combo,
                         canonical.ordered_chance_cards)) {
          canonical = candidate;
          has_canonical = true;
        }
      }
      const auto next_id = static_cast<std::uint32_t>(layout.canonical_action_bases.size());
      const auto [found, inserted] =
          canonical_infosets.emplace(canonical, CanonicalInfosetEntry{next_id, node.id});
      if (inserted) {
        if (layout.actions > std::numeric_limits<std::uint64_t>::max() - decision.action_count) {
          return Result<DenseLayout, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        layout.canonical_action_bases.push_back(layout.actions);
        layout.canonical_action_counts.push_back(decision.action_count);
        layout.canonical_infoset_multiplicity.push_back(0U);
        layout.actions += decision.action_count;
        ++layout.information_sets;
      } else if (!same_actions(node, layout.tree.nodes[static_cast<std::size_t>(
                                         found->second.representative_node)])) {
        return Result<DenseLayout, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      layout.physical_infoset_ids.push_back(found->second.id);
      ++layout.canonical_infoset_multiplicity[found->second.id];
    }
  }
  canonical_infosets.clear();
  canonical_infosets.rehash(0U);
  if (enable_canonical_public_dag && !automorphisms.empty()) {
    auto canonical_graph = direct_canonical_tree
                               ? build_direct_canonical_public_graph(layout.tree, automorphisms)
                               : build_canonical_public_graph(layout.tree, histories,
                                                              public_history_ids, automorphisms);
    if (!canonical_graph) {
      return Result<DenseLayout, PostflopSolverError>::failure(canonical_graph.error());
    }
    // The canonical chance tree owns one independent CFR block per decision
    // node. Chance-equivalent public outcomes share that subtree, while
    // unrelated histories can never alias the same mutable state.
    layout.uses_direct_action_bases = true;
    layout.information_sets = 0U;
    layout.actions = 0U;
    for (auto &canonical_node : canonical_graph.value().nodes) {
      const auto &representative =
          layout.tree.nodes[static_cast<std::size_t>(canonical_node.representative_node)];
      canonical_node.board_index =
          layout.node_board[static_cast<std::size_t>(canonical_node.representative_node)];
      if (!representative.edges.empty()) {
        canonical_node.total_legal_outcome_count =
            representative.edges.front().total_legal_outcome_count;
      }
      if (canonical_node.kind == PublicNodeKind::TerminalFold) {
        const auto payoff_index = layout.node_terminal_payoff[
            static_cast<std::size_t>(canonical_node.representative_node)];
        if (payoff_index >= layout.terminal_payoffs.size()) {
          return Result<DenseLayout, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        for (std::uint8_t player = 0; player < 2U; ++player) {
          canonical_node.fold_payoff_antes[player] =
              layout.terminal_payoffs[payoff_index].value_antes[player][0];
        }
      } else if (canonical_node.kind == PublicNodeKind::TerminalShowdown) {
        const auto payoff_index = layout.node_terminal_payoff[
            static_cast<std::size_t>(canonical_node.representative_node)];
        if (payoff_index >= layout.terminal_payoffs.size()) {
          return Result<DenseLayout, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        canonical_node.showdown_payoff_antes =
            layout.terminal_payoffs[payoff_index].value_antes;
      }
      if (canonical_node.kind != PublicNodeKind::Decision) {
        continue;
      }
      const auto &decision =
          layout.decisions[static_cast<std::size_t>(canonical_node.representative_node)];
      canonical_node.decision = decision;
      const auto &board = layout.boards[decision.board_index];
      const auto legal_count = board.player_combos[decision.player].size();
      if (legal_count >
          (std::numeric_limits<std::uint64_t>::max() - layout.actions) /
              decision.action_count) {
        return Result<DenseLayout, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      const auto node_action_begin = layout.actions;
      canonical_node.decision.action_base = node_action_begin;
      canonical_node.local_action_count =
          static_cast<std::uint32_t>(legal_count * decision.action_count);
      if (layout.canonical_decision_nodes >
          std::numeric_limits<std::uint32_t>::max()) {
        return Result<DenseLayout, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      canonical_node.state_scale_index =
          static_cast<std::uint32_t>(layout.canonical_decision_nodes++);
      layout.information_sets += legal_count;
      layout.actions += legal_count * decision.action_count;
    }
    // Canonical nodes are emitted depth-first with every child after its
    // parent.  Record which players can act below each node so CFR can prove
    // when an updating player's reach is dead for the remainder of a river
    // branch.  The two masks occupy previously reserved DecisionLayout bits.
    for (std::size_t index = canonical_graph.value().nodes.size(); index-- > 0U;) {
      auto &node = canonical_graph.value().nodes[index];
      std::uint8_t descendant_mask = 0U;
      for (const auto &edge : node.edges) {
        for (const auto &outcome : edge.outcomes) {
          if (outcome.child <= index ||
              outcome.child >= canonical_graph.value().nodes.size()) {
            return Result<DenseLayout, PostflopSolverError>::failure(
                PostflopSolverError::InvalidConfiguration);
          }
          descendant_mask |= static_cast<std::uint8_t>(
              canonical_graph.value().nodes[outcome.child]
                  .decision.subtree_player_mask);
        }
      }
      node.decision.descendant_player_mask = descendant_mask;
      node.decision.subtree_player_mask = static_cast<std::uint8_t>(
          descendant_mask |
          (node.kind == PublicNodeKind::Decision
               ? static_cast<std::uint8_t>(1U << node.decision.player)
               : 0U));
    }
    layout.physical_infoset_ids.clear();
    layout.canonical_action_bases.clear();
    layout.canonical_action_counts.clear();
    layout.canonical_infoset_multiplicity.clear();
    layout.canonical_public_graph = std::move(canonical_graph.value());
    layout.automorphisms = automorphisms;
    layout.uses_canonical_public_dag = true;
  }
  if (layout.actions > std::numeric_limits<std::size_t>::max()) {
    return Result<DenseLayout, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  // The legacy physical traversal does not aggregate public suit orbits. This
  // flag is retained for its specialized inspection path, not for the
  // node-owned chance tree selected above.
  layout.uses_range_aware_physical_orbits = false;

  const auto &flop_board = layout.boards[layout.node_board[layout.tree.root]];
  layout.active_combos = flop_board.legal_combos;
  for (std::size_t index = 0; index < layout.active_combos.size(); ++index) {
    layout.active_combo_index[layout.active_combos[index]] = static_cast<std::int16_t>(index);
    const auto &combo = layout.combos[layout.active_combos[index]];
    layout.active_slots_by_card[combo.first.value()].push_back(static_cast<std::uint16_t>(index));
    layout.active_slots_by_card[combo.second.value()].push_back(static_cast<std::uint16_t>(index));
    layout.active_combos_by_card[combo.first.value()].push_back(layout.active_combos[index]);
    layout.active_combos_by_card[combo.second.value()].push_back(layout.active_combos[index]);
  }
  layout.active_automorphism_slots.reserve(layout.automorphisms.size());
  layout.active_automorphism_is_identity.reserve(layout.automorphisms.size());
  for (const auto &automorphism : layout.automorphisms) {
    std::vector<std::uint16_t> slots;
    slots.reserve(layout.active_combos.size());
    bool is_identity = true;
    for (const ComboId combo : layout.active_combos) {
      const auto mapped = layout.active_combo_index[automorphism.combos[combo]];
      if (mapped < 0) {
        return Result<DenseLayout, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      slots.push_back(static_cast<std::uint16_t>(mapped));
      is_identity = is_identity &&
                    static_cast<std::size_t>(mapped) + 1U == slots.size();
    }
    layout.active_automorphism_slots.push_back(std::move(slots));
    layout.active_automorphism_is_identity.push_back(
        static_cast<std::uint8_t>(is_identity));
  }
  for (auto &board : layout.boards) {
    for (std::uint8_t player = 0U; player < 2U; ++player) {
      auto &slots = board.player_active_slots[player];
      slots.reserve(board.player_combos[player].size());
      for (const ComboId combo : board.player_combos[player]) {
        const auto slot = layout.active_combo_index[combo];
        if (slot < 0) {
          return Result<DenseLayout, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        slots.push_back(static_cast<std::uint16_t>(slot));
      }
    }
  }
  for (const ComboId first : flop_board.legal_combos) {
    layout.initial_reach[0][first] =
        static_cast<double>(ranges.players[0][first].basis_points()) / 10'000.0;
    layout.initial_reach[1][first] =
        static_cast<double>(ranges.players[1][first].basis_points()) / 10'000.0;
    for (const ComboId second : flop_board.legal_combos) {
      if ((layout.combo_masks[first] & layout.combo_masks[second]) == 0U) {
        layout.initial_normalization +=
            layout.initial_reach[0][first] *
            (static_cast<double>(ranges.players[1][second].basis_points()) / 10'000.0);
      }
    }
  }
  if (!(layout.initial_normalization > 0.0)) {
    return Result<DenseLayout, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  for (auto &player_slots : layout.player_flop_slot) {
    player_slots.fill(-1);
  }
  for (std::uint8_t player = 0; player < 2U; ++player) {
    layout.player_flop_combos[player] = flop_board.player_combos[player];
    layout.player_flop_count[player] = flop_board.player_combos[player].size();
    for (std::size_t index = 0; index < flop_board.player_combos[player].size(); ++index) {
      layout.player_flop_slot[player][flop_board.player_combos[player][index]] =
          static_cast<std::int16_t>(index);
    }
  }
  for (auto &board : layout.boards) {
    for (std::uint8_t player = 0; player < 2U; ++player) {
      auto &slots = board.player_flop_slots[player];
      auto &opponent_slots = board.player_opponent_flop_slots[player];
      slots.reserve(board.player_combos[player].size());
      opponent_slots.reserve(board.player_combos[player].size());
      for (const ComboId combo : board.player_combos[player]) {
        const auto slot = layout.player_flop_slot[player][combo];
        if (slot < 0) {
          return Result<DenseLayout, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        slots.push_back(static_cast<std::uint16_t>(slot));
        const auto opponent_slot = layout.player_flop_slot[1U - player][combo];
        opponent_slots.push_back(
            opponent_slot < 0 ? TerminalComboData::invalid_slot
                : static_cast<std::uint16_t>(opponent_slot));
      }
      for (std::size_t card = 0U; card < 36U; ++card) {
        auto &compatible = board.chance_compatible_flop_slots[player][card];
        compatible.reserve(board.player_combos[player].size());
        const auto card_mask = std::uint64_t{1} << card;
        for (std::size_t local = 0U;
             local < board.player_combos[player].size(); ++local) {
          const auto combo = board.player_combos[player][local];
          if ((layout.combo_masks[combo] & card_mask) == 0U) {
            compatible.push_back(board.player_flop_slots[player][local]);
          }
        }
      }
    }
  }
  const auto serialized_config = serialize_tree_config_json(config);
  std::string fingerprint_source = layout.tree.betting_tree_hash + "|" + serialized_config;
  if (!uniform_full_ranges(ranges)) {
    fingerprint_source +=
        "|ranges-v1|" + serialize_range_fingerprint(ranges) +
        (layout.uses_isomorphic_infosets ? "|iso-infosets-v1" : "|physical-infosets-v1");
  }
  layout.fingerprint = fingerprint_text(fingerprint_source);
  if (layout.uses_canonical_public_dag) {
    // The direct solver tree and the physical inspection tree are two lossless
    // storage representations of the same game. Their checkpoint identity
    // must therefore be based on the game/ranges and canonical layout version,
    // never on the representation-specific betting-tree hash.
    std::string canonical_fingerprint_source =
        "node-owned-chance-tree-v3|" + serialized_config;
    if (!uniform_full_ranges(ranges)) {
      canonical_fingerprint_source +=
          "|ranges-v1|" + serialize_range_fingerprint(ranges);
    }
    canonical_fingerprint_source +=
        enable_lossless_isomorphism ? "|iso-on" : "|iso-off";
    layout.fingerprint = fingerprint_text(canonical_fingerprint_source);
  }
  if (direct_canonical_tree) {
    layout.tree.stats = physical_tree_stats;
  }
  if (layout.uses_canonical_public_dag && !preserve_physical_tree) {
    layout.tree.nodes.clear();
    layout.tree.nodes.shrink_to_fit();
    layout.node_board.clear();
    layout.node_board.shrink_to_fit();
    layout.decisions.clear();
    layout.decisions.shrink_to_fit();
  }
  return Result<DenseLayout, PostflopSolverError>::success(std::move(layout));
}

Result<bool, PostflopSolverError> prepare_ranks(DenseLayout &layout,
                                                const std::uint32_t board_index) {
  auto &board = layout.boards[board_index];
  if (board.ranks_ready) {
    return Result<bool, PostflopSolverError>::success(true);
  }
  if (std::popcount(board.mask) != 5) {
    return Result<bool, PostflopSolverError>::failure(PostflopSolverError::EquityFailure);
  }
  const auto board_cards = cards_from_mask(board.mask);
  std::vector<std::pair<HandValue, ComboId>> ranked;
  ranked.reserve(board.legal_combos.size());
  for (const ComboId combo_id : board.legal_combos) {
    const auto &combo = layout.combos[combo_id];
    std::array<CardId, 7> cards{board_cards[0], board_cards[1], board_cards[2], board_cards[3],
                                board_cards[4], combo.first,    combo.second};
    const auto value = evaluate_seven(cards);
    if (!value) {
      return Result<bool, PostflopSolverError>::failure(PostflopSolverError::EquityFailure);
    }
    ranked.emplace_back(value.value(), combo_id);
  }
  std::sort(ranked.begin(), ranked.end(),
            [](const auto &left, const auto &right) { return left.first < right.first; });
  std::int16_t rank = -1;
  HandValue previous{};
  bool has_previous = false;
  for (const auto &[value, combo_id] : ranked) {
    if (!has_previous || value != previous) {
      ++rank;
      previous = value;
      has_previous = true;
    }
    board.rank_index[combo_id] = rank;
  }
  board.rank_count = static_cast<std::uint16_t>(rank + 1);
  const auto rank_count = static_cast<std::size_t>(board.rank_count);
  std::array<std::int16_t, combo_count> player_rank_map{};
  player_rank_map.fill(-1);
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    for (const ComboId combo_id : board.player_combos[player]) {
      const auto global_rank = board.rank_index[combo_id];
      if (global_rank < 0 ||
          static_cast<std::size_t>(global_rank) >= rank_count) {
        return Result<bool, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      player_rank_map[static_cast<std::size_t>(global_rank)] = 0;
    }
  }
  std::int16_t next_player_rank = 0;
  for (std::size_t global_rank = 0U; global_rank < rank_count; ++global_rank) {
    if (player_rank_map[global_rank] >= 0) {
      player_rank_map[global_rank] = next_player_rank++;
    }
  }
  board.player_rank_count = static_cast<std::uint16_t>(next_player_rank);
  const auto player_rank_count =
      static_cast<std::size_t>(board.player_rank_count);
  for (std::uint8_t player = 0; player < 2U; ++player) {
    auto &metadata = board.terminal_combos[player];
    metadata.clear();
    metadata.reserve(board.player_combos[player].size());
    for (const ComboId combo_id : board.player_combos[player]) {
      const auto global_rank = static_cast<std::size_t>(board.rank_index[combo_id]);
      if (global_rank >= rank_count || player_rank_map[global_rank] < 0) {
        return Result<bool, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      const auto combo_rank =
          static_cast<std::size_t>(player_rank_map[global_rank]);
      const auto first = static_cast<std::size_t>(layout.combos[combo_id].first.value());
      const auto second = static_cast<std::size_t>(layout.combos[combo_id].second.value());
      const auto own_slot = layout.player_flop_slot[player][combo_id];
      const auto opponent_slot = layout.player_flop_slot[1U - player][combo_id];
      if (own_slot < 0 || combo_rank >= player_rank_count ||
          combo_rank * 36U + first >
              std::numeric_limits<std::uint16_t>::max() ||
          combo_rank * 36U + second >
              std::numeric_limits<std::uint16_t>::max()) {
        return Result<bool, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      metadata.own_slot.push_back(static_cast<std::uint16_t>(own_slot));
      metadata.opponent_slot.push_back(
          opponent_slot < 0 ? TerminalComboData::invalid_slot
                            : static_cast<std::uint16_t>(opponent_slot));
      const auto opponent_local = board.player_local[1U - player][combo_id];
      metadata.opponent_local.push_back(
          opponent_local < 0 ? TerminalComboData::invalid_slot
                             : static_cast<std::uint16_t>(opponent_local));
      metadata.rank.push_back(static_cast<std::uint16_t>(combo_rank));
      metadata.first_by_rank.push_back(
          static_cast<std::uint16_t>(combo_rank * 36U + first));
      metadata.second_by_rank.push_back(
          static_cast<std::uint16_t>(combo_rank * 36U + second));
      metadata.first_all.push_back(
          static_cast<std::uint16_t>(rank_count * 36U + first));
      metadata.second_all.push_back(
          static_cast<std::uint16_t>(rank_count * 36U + second));
      metadata.first_card.push_back(static_cast<std::uint8_t>(first));
      metadata.second_card.push_back(static_cast<std::uint8_t>(second));
    }
    std::array<std::uint8_t, 36U * combo_count> touched{};
    for (std::size_t local = 0U; local < metadata.size(); ++local) {
      const std::array<std::uint16_t, 2> cells{
          metadata.first_by_rank[local], metadata.second_by_rank[local]};
      for (const auto cell : cells) {
        if (touched[cell] == 0U) {
          touched[cell] = 1U;
          metadata.touched_by_rank_card.push_back(cell);
        }
      }
    }
    std::ranges::sort(metadata.touched_by_rank_card);
  }
  auto &active_metadata = board.terminal_active_combos;
  active_metadata.clear();
  active_metadata.reserve(board.legal_combos.size());
  for (const ComboId combo_id : board.legal_combos) {
    const auto combo_rank = static_cast<std::size_t>(board.rank_index[combo_id]);
    const auto first = static_cast<std::size_t>(layout.combos[combo_id].first.value());
    const auto second = static_cast<std::size_t>(layout.combos[combo_id].second.value());
    const auto active_slot = layout.active_combo_index[combo_id];
    if (active_slot < 0 || combo_rank >= rank_count ||
        first * (rank_count + 1U) + rank_count >
            std::numeric_limits<std::uint16_t>::max() ||
        second * (rank_count + 1U) + rank_count >
            std::numeric_limits<std::uint16_t>::max()) {
      return Result<bool, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    active_metadata.own_slot.push_back(static_cast<std::uint16_t>(active_slot));
    active_metadata.opponent_slot.push_back(static_cast<std::uint16_t>(active_slot));
    active_metadata.rank.push_back(static_cast<std::uint16_t>(combo_rank));
    active_metadata.first_by_rank.push_back(
        static_cast<std::uint16_t>(combo_rank * 36U + first));
    active_metadata.second_by_rank.push_back(
        static_cast<std::uint16_t>(combo_rank * 36U + second));
    active_metadata.first_all.push_back(
        static_cast<std::uint16_t>(rank_count * 36U + first));
    active_metadata.second_all.push_back(
        static_cast<std::uint16_t>(rank_count * 36U + second));
    active_metadata.first_card.push_back(static_cast<std::uint8_t>(first));
    active_metadata.second_card.push_back(static_cast<std::uint8_t>(second));
  }
  {
    std::array<std::uint8_t, 36U * combo_count> touched{};
    for (std::size_t local = 0U; local < active_metadata.size(); ++local) {
      const std::array<std::uint16_t, 2> cells{
          active_metadata.first_by_rank[local],
          active_metadata.second_by_rank[local]};
      for (const auto cell : cells) {
        if (touched[cell] == 0U) {
          touched[cell] = 1U;
          active_metadata.touched_by_rank_card.push_back(cell);
        }
      }
    }
    std::ranges::sort(active_metadata.touched_by_rank_card);
  }
  board.ranks_ready = true;
  return Result<bool, PostflopSolverError>::success(true);
}

std::string root_action_label(const Action &action) {
  std::string label;
  switch (action.type) {
  case ActionType::Fold:
    label = "fold";
    break;
  case ActionType::Check:
    label = "check";
    break;
  case ActionType::Call:
    label = "call";
    break;
  case ActionType::Bet:
    label = "bet";
    break;
  case ActionType::Raise:
    label = "raise";
    break;
  case ActionType::AllIn:
    label = "all_in";
    break;
  }
  if (action.amount.units() > 0) {
    label += "_" + std::to_string(action.amount.units() / gtosd::Money::units_per_ante);
  }
  return label;
}

struct PreparedRootLock {
  std::string source_description;
  double source_dev_percent{0.0};
  std::vector<std::optional<std::array<double, maximum_action_count>>> by_local_combo;
};

Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>
prepare_root_lock(const DiagnosticRootLock &lock, const DenseLayout &layout) {
  // The root is read from the canonical public graph when the DAG is enabled
  // (the physical tree and decision table are not retained in that layout),
  // otherwise from the physical tree.
  std::vector<std::string> root_labels;
  std::uint16_t root_action_count = 0;
  std::uint32_t root_board_index = 0;
  std::uint8_t root_player = 0;
  bool root_is_decision = false;
  if (layout.uses_canonical_public_dag) {
    const auto &root_node =
        layout.canonical_public_graph.nodes[layout.canonical_public_graph.root];
    root_is_decision = root_node.kind == PublicNodeKind::Decision;
    root_player = static_cast<std::uint8_t>(root_node.decision.player);
    root_board_index = root_node.board_index;
    root_action_count = root_node.decision.action_count;
    for (const auto &edge : root_node.edges) {
      root_labels.push_back(root_action_label(edge.action));
    }
  } else {
    const auto &root_node = layout.tree.nodes[static_cast<std::size_t>(layout.tree.root)];
    root_is_decision = root_node.kind == PublicNodeKind::Decision;
    root_player = root_node.state.player_to_act;
    const auto &decision = layout.decisions[static_cast<std::size_t>(layout.tree.root)];
    root_board_index = decision.board_index;
    root_action_count = decision.action_count;
    for (const auto &edge : root_node.edges) {
      if (edge.kind == PublicEdgeKind::ChanceCard) {
        continue;
      }
      root_labels.push_back(root_action_label(edge.action));
    }
  }
  if (!root_is_decision || root_player != 0U || root_action_count == 0U) {
    return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  const auto &board = layout.boards[root_board_index];
  if (board.legal_combos.size() != lock.entries.size()) {
    return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  if (root_labels.size() != root_action_count) {
    return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  auto labels_sorted = root_labels;
  std::ranges::sort(labels_sorted);

  auto prepared = std::make_unique<PreparedRootLock>();
  prepared->source_description = lock.source_description;
  prepared->source_dev_percent = lock.source_dev_percent;
  prepared->by_local_combo.resize(board.legal_combos.size());

  std::vector<bool> covered(board.legal_combos.size(), false);
  for (const auto &entry : lock.entries) {
    if (entry.action_labels.size() != entry.probabilities.size() ||
        entry.probabilities.size() != root_action_count) {
      return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    const auto combo_it = std::ranges::find(layout.combos, entry.combo);
    if (combo_it == layout.combos.end()) {
      return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    const auto combo_id = static_cast<ComboId>(std::distance(layout.combos.begin(), combo_it));
    const auto local = board.local_index[combo_id];
    if (local < 0 || static_cast<std::size_t>(local) >= covered.size() ||
        covered[static_cast<std::size_t>(local)]) {
      return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    auto entry_labels = entry.action_labels;
    std::ranges::sort(entry_labels);
    if (entry_labels != labels_sorted) {
      return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    std::array<double, maximum_action_count> probabilities{};
    double sum = 0.0;
    for (std::size_t action = 0; action < root_action_count; ++action) {
      const auto it = std::ranges::find(entry.action_labels, root_labels[action]);
      if (it == entry.action_labels.end()) {
        return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      const auto probability =
          entry.probabilities[static_cast<std::size_t>(it - entry.action_labels.begin())];
      if (!std::isfinite(probability) || probability < 0.0) {
        return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      probabilities[action] = probability;
      sum += probability;
    }
    if (std::abs(sum - 1.0) > 1.0e-9) {
      return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    prepared->by_local_combo[static_cast<std::size_t>(local)] = probabilities;
    covered[static_cast<std::size_t>(local)] = true;
  }
  if (std::ranges::any_of(covered, [](const bool present) { return !present; })) {
    return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::success(
      std::move(prepared));
}

template <std::size_t Capacity, bool PlayerIndexed = false,
          typename ComputeScalar = TraversalScalar<Capacity>>
class DenseTraversal {
  using Scalar = ComputeScalar;
  using ComboVector = std::array<Scalar, Capacity>;
  using TraversalResult = Result<ComboVector, PostflopSolverError>;
  // Reach passed down the tree as two pointers (plan: zero opponent reach
  // copies): the actor's vector is a per-depth scratch (only its flop-range
  // prefix is written), the opponent's vector is shared from the parent.
  using ReachRef = std::array<const ComboVector *, 2>;

  static __m256d load_four_as_double(const Scalar *const source) noexcept {
    if constexpr (std::is_same_v<Scalar, float>) {
      return _mm256_cvtps_pd(_mm_loadu_ps(source));
    } else {
      return _mm256_loadu_pd(source);
    }
  }

  static void store_four_from_double(Scalar *const destination,
                                     const __m256d values) noexcept {
    if constexpr (std::is_same_v<Scalar, float>) {
      _mm_storeu_ps(destination, _mm256_cvtpd_ps(values));
    } else {
      _mm256_storeu_pd(destination, values);
    }
  }

  static __m256d gather_four_as_double(const Scalar *const source,
                                       const __m128i indices) noexcept {
    if constexpr (std::is_same_v<Scalar, float>) {
      return _mm256_cvtps_pd(_mm_i32gather_ps(source, indices, 4));
    } else {
      return _mm256_i32gather_pd(source, indices, 8);
    }
  }

  // Tag for constructing a pool worker: a full traversal that owns its own
  // deferred regret delta and worker thread but no nested workers.
  struct LeafWorkerTag {};

  // Pull-based shared task queue: all pool threads drain the same queue, so
  // tasks dispatched below the turn chance (river split) are picked up by any
  // idle worker and the pool self-balances. Teardown wakes every waiter
  // (notify_all) because the shared queue has one condition variable for all
  // workers; a single notify_one would leave the other workers blocked and
  // hang the destructor join.
  struct ParallelTaskQueue {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::packaged_task<TraversalResult(DenseTraversal &)>> tasks;
    std::atomic<std::uint64_t> completion_epoch{0U};
    std::atomic<std::size_t> active_tasks{0U};
    bool shutdown{false};
  };

  void run_worker_loop() {
#ifdef _WIN32
    if (pin_worker_threads_ && worker_logical_processor_ < 64U) {
      SetThreadAffinityMask(
          GetCurrentThread(),
          static_cast<DWORD_PTR>(1ULL << worker_logical_processor_));
    }
#endif
    ParallelTaskQueue *const queue = parallel_shared_.get();
    while (true) {
      std::optional<std::packaged_task<TraversalResult(DenseTraversal &)>> task;
      {
        std::unique_lock lock(queue->mutex);
        queue->ready.wait(lock, [&] { return queue->shutdown || !queue->tasks.empty(); });
        if (queue->shutdown && queue->tasks.empty()) {
          return;
        }
        task = std::move(queue->tasks.front());
        queue->tasks.pop_front();
      }
      const bool profile = hotpath_profiling_enabled();
      const auto task_started = profile ? std::chrono::steady_clock::now()
                                        : std::chrono::steady_clock::time_point{};
      queue->active_tasks.fetch_add(1U, std::memory_order_acq_rel);
      (*task)(*this);
      queue->active_tasks.fetch_sub(1U, std::memory_order_acq_rel);
      // A joining traversal may be sleeping because the queue became empty
      // while this task was still running. Completing the packaged task makes
      // its future ready but does not signal our queue condition variable, so
      // explicitly wake joiners instead of forcing every one through the
      // 1 ms timeout path.
      queue->completion_epoch.fetch_add(1U, std::memory_order_release);
      queue->completion_epoch.notify_all();
      if (profile) {
        ++prof_tasks_;
        prof_task_wall_seconds_ +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() - task_started)
                .count();
      }
    }
  }

public:
  DenseTraversal(DenseLayout &layout, const ActionBuffers buffers,
                 std::vector<double> *deferred_regret_delta = nullptr,
                 const std::uint8_t parallel_action_depth = 0U,
                 const PreparedRootLock *root_lock = nullptr,
                 const bool pin_worker_threads = false)
      : layout_(layout), buffers_(buffers), deferred_regret_delta_(deferred_regret_delta),
        root_lock_(root_lock), pin_worker_threads_(pin_worker_threads) {
    if (deferred_regret_delta_ != nullptr) {
      deferred_regret_touched_flags_.resize(deferred_regret_delta_->size(), 0U);
    }
    if (parallel_action_depth > 0U) {
      if (layout_.uses_canonical_public_dag) {
        // Node-owned canonical subtrees below distinct chance outcomes own
        // disjoint action ranges. The direct-action-base path therefore needs
        // no thread-local regret replicas or reduction: a shared worker pool
        // can update those subtrees in place without locks. Legacy canonical
        // layouts that still use a deferred delta retain the single-worker
        // decision split and its explicit merge.
        if (deferred_regret_delta_ == nullptr) {
          const std::size_t worker_count = static_cast<std::size_t>(parallel_action_depth);
          parallel_shared_ = std::make_shared<ParallelTaskQueue>();
          parallel_workers_.reserve(worker_count);
          for (std::size_t index = 0U; index < worker_count; ++index) {
            parallel_workers_.push_back(std::make_unique<DenseTraversal>(
                LeafWorkerTag{}, layout_, buffers_, nullptr, root_lock_, parallel_shared_,
                static_cast<std::uint8_t>(index + 1U), pin_worker_threads_));
          }
          return;
        }
        parallel_regret_delta_.resize(deferred_regret_delta_->size(), 0.0);
        parallel_shared_ = std::make_shared<ParallelTaskQueue>();
        parallel_worker_ =
            std::make_unique<DenseTraversal>(layout_, buffers_, &parallel_regret_delta_,
                                             static_cast<std::uint8_t>(parallel_action_depth - 1U),
                                             root_lock_, pin_worker_threads_);
        parallel_thread_ = std::jthread([this] { run_worker_loop(); });
      } else {
        // Physical tree: a pool of independent workers, each with its own
        // deferred regret delta when accumulating regrets (solve), or with a
        // null delta when evaluating a fixed profile (certification — the
        // policy traversal never touches regret state), used by the coarse
        // chance-node split so the per-card subtrees run concurrently.
        const std::size_t worker_count = static_cast<std::size_t>(parallel_action_depth);
        const bool has_deltas = deferred_regret_delta_ != nullptr;
        if (has_deltas) {
          parallel_worker_deltas_.resize(worker_count);
          for (auto &delta : parallel_worker_deltas_) {
            delta.resize(deferred_regret_delta_->size(), 0.0);
          }
        }
        parallel_shared_ = std::make_shared<ParallelTaskQueue>();
        parallel_workers_.reserve(worker_count);
        for (std::size_t index = 0; index < worker_count; ++index) {
          parallel_workers_.push_back(std::make_unique<DenseTraversal>(
              LeafWorkerTag{}, layout_, buffers_,
              has_deltas ? &parallel_worker_deltas_[index] : nullptr, root_lock_,
              parallel_shared_, static_cast<std::uint8_t>(index + 1U),
              pin_worker_threads_));
        }
      }
    }
  }

  // Pool worker constructor: owns a thread that drains the shared task queue,
  // but creates no nested workers.
  DenseTraversal(LeafWorkerTag, DenseLayout &layout, const ActionBuffers buffers,
                 std::vector<double> *deferred_regret_delta, const PreparedRootLock *root_lock,
                 std::shared_ptr<ParallelTaskQueue> shared_queue,
                 const std::uint8_t worker_logical_processor,
                 const bool pin_worker_threads)
      : layout_(layout), buffers_(buffers), deferred_regret_delta_(deferred_regret_delta),
        root_lock_(root_lock), parallel_shared_(std::move(shared_queue)),
        worker_logical_processor_(worker_logical_processor),
        pin_worker_threads_(pin_worker_threads) {
    if (deferred_regret_delta_ != nullptr) {
      deferred_regret_touched_flags_.resize(deferred_regret_delta_->size(), 0U);
    }
    parallel_thread_ = std::jthread([this] { run_worker_loop(); });
  }

  ~DenseTraversal() {
    if (parallel_thread_.joinable()) {
      ParallelTaskQueue *const queue = parallel_shared_.get();
      {
        std::scoped_lock lock(queue->mutex);
        queue->shutdown = true;
      }
      queue->ready.notify_all();
      // The scratch arenas are declared after the jthread and are therefore
      // destroyed before it by C++'s reverse member-destruction order. Join
      // explicitly here so a worker cannot still write into those arenas
      // while the implicit member destructors release them.
      parallel_thread_.join();
    }
  }

  // Per-node timing instrumentation (GTOSD_PROFILE_HOTPATH=1): accumulated
  // on every thread that runs cfr_decision, so totals are serial-equivalent.
  mutable std::uint64_t prof_decisions_ = 0;
  mutable std::uint64_t prof_actor_writes_ = 0;
  mutable std::uint64_t prof_strategy_entries_ = 0;
  mutable std::uint64_t prof_zero_strategy_entries_ = 0;
  mutable std::uint64_t prof_whole_zero_actions_ = 0;
  mutable std::uint64_t prof_average_only_calls_ = 0;
  mutable std::uint64_t prof_average_only_nodes_ = 0;
  mutable std::uint64_t prof_average_only_decisions_ = 0;
  mutable std::uint64_t prof_average_only_entries_ = 0;
  mutable std::uint64_t prof_chance_calls_ = 0;
  mutable std::uint64_t prof_chance_outcomes_ = 0;
  mutable std::uint64_t prof_tasks_ = 0;
  mutable double prof_strategy_seconds_ = 0.0;
  mutable double prof_copy_seconds_ = 0.0;
  mutable double prof_children_seconds_ = 0.0;
  mutable double prof_value_update_seconds_ = 0.0;
  mutable double prof_value_accumulate_seconds_ = 0.0;
  mutable double prof_regret_update_seconds_ = 0.0;
  mutable double prof_average_update_seconds_ = 0.0;
  mutable double prof_average_only_seconds_ = 0.0;
  mutable double prof_terminal_seconds_ = 0.0;
  mutable double prof_fold_seconds_ = 0.0;
  mutable double prof_showdown_seconds_ = 0.0;
  mutable double prof_showdown_accumulate_seconds_ = 0.0;
  mutable double prof_showdown_prefix_seconds_ = 0.0;
  mutable double prof_showdown_output_seconds_ = 0.0;
  mutable double prof_chance_seconds_ = 0.0;
  mutable double prof_chance_prepare_seconds_ = 0.0;
  mutable double prof_chance_accumulate_seconds_ = 0.0;
  mutable double prof_sync_seconds_ = 0.0;
  mutable double prof_wall_seconds_ = 0.0;
  mutable double prof_task_wall_seconds_ = 0.0;

  // Per-pass profile (GTOSD_PROFILE_HOTPATH=1): sums this traversal's and its
  // pool workers' counters (serial-equivalent), prints a per-pass breakdown
  // and resets all counters. The residual (pass wall minus the accounted
  // parts) is the pure recursion/dispatch overhead of the CFR walk.
  [[nodiscard]] static
#if defined(GTOSD_ENABLE_HOTPATH_PROFILE)
      bool
#else
      constexpr bool
#endif
  hotpath_profiling_enabled() noexcept {
#if defined(GTOSD_ENABLE_HOTPATH_PROFILE)
    static const bool enabled = [] {
#pragma warning(push)
#pragma warning(disable : 4996)
      return std::getenv("GTOSD_PROFILE_HOTPATH") != nullptr;
#pragma warning(pop)
    }();
    return enabled;
#else
    return false;
#endif
  }

  [[nodiscard]] static bool diagnostic_certify_current_strategy() noexcept {
#pragma warning(push)
#pragma warning(disable : 4996)
    static const bool enabled =
        std::getenv("GTOSD_DIAGNOSTIC_CERTIFY_CURRENT") != nullptr;
#pragma warning(pop)
    return enabled;
  }

  void dump_per_pass_profile(const double wall_seconds) {
    const bool profile = hotpath_profiling_enabled();
    if (!profile) {
      return;
    }
    double strategy = prof_strategy_seconds_;
    double copy = prof_copy_seconds_;
    double terminal = prof_terminal_seconds_;
    double fold = prof_fold_seconds_;
    double showdown = prof_showdown_seconds_;
    double showdown_accumulate = prof_showdown_accumulate_seconds_;
    double showdown_prefix = prof_showdown_prefix_seconds_;
    double showdown_output = prof_showdown_output_seconds_;
    double value_update = prof_value_update_seconds_;
    double value_accumulate = prof_value_accumulate_seconds_;
    double regret_update = prof_regret_update_seconds_;
    double average_update = prof_average_update_seconds_;
    double average_only = prof_average_only_seconds_;
    double chance = prof_chance_seconds_;
    double chance_prepare = prof_chance_prepare_seconds_;
    double chance_accumulate = prof_chance_accumulate_seconds_;
    double sync = prof_sync_seconds_;
    double wall = prof_wall_seconds_;
    std::uint64_t decisions = prof_decisions_;
    std::uint64_t actor_writes = prof_actor_writes_;
    std::uint64_t strategy_entries = prof_strategy_entries_;
    std::uint64_t zero_strategy_entries = prof_zero_strategy_entries_;
    std::uint64_t whole_zero_actions = prof_whole_zero_actions_;
    std::uint64_t average_only_calls = prof_average_only_calls_;
    std::uint64_t average_only_nodes = prof_average_only_nodes_;
    std::uint64_t average_only_decisions = prof_average_only_decisions_;
    std::uint64_t average_only_entries = prof_average_only_entries_;
    std::uint64_t chance_calls = prof_chance_calls_;
    std::uint64_t chance_outcomes = prof_chance_outcomes_;
    for (const auto &worker : parallel_workers_) {
      strategy += worker->prof_strategy_seconds_;
      copy += worker->prof_copy_seconds_;
      terminal += worker->prof_terminal_seconds_;
      fold += worker->prof_fold_seconds_;
      showdown += worker->prof_showdown_seconds_;
      showdown_accumulate += worker->prof_showdown_accumulate_seconds_;
      showdown_prefix += worker->prof_showdown_prefix_seconds_;
      showdown_output += worker->prof_showdown_output_seconds_;
      value_update += worker->prof_value_update_seconds_;
      value_accumulate += worker->prof_value_accumulate_seconds_;
      regret_update += worker->prof_regret_update_seconds_;
      average_update += worker->prof_average_update_seconds_;
      average_only += worker->prof_average_only_seconds_;
      chance += worker->prof_chance_seconds_;
      chance_prepare += worker->prof_chance_prepare_seconds_;
      chance_accumulate += worker->prof_chance_accumulate_seconds_;
      sync += worker->prof_sync_seconds_;
      wall += worker->prof_wall_seconds_;
      decisions += worker->prof_decisions_;
      actor_writes += worker->prof_actor_writes_;
      strategy_entries += worker->prof_strategy_entries_;
      zero_strategy_entries += worker->prof_zero_strategy_entries_;
      whole_zero_actions += worker->prof_whole_zero_actions_;
      average_only_calls += worker->prof_average_only_calls_;
      average_only_nodes += worker->prof_average_only_nodes_;
      average_only_decisions += worker->prof_average_only_decisions_;
      average_only_entries += worker->prof_average_only_entries_;
      chance_calls += worker->prof_chance_calls_;
      chance_outcomes += worker->prof_chance_outcomes_;
    }
    if (parallel_worker_ != nullptr) {
      strategy += parallel_worker_->prof_strategy_seconds_;
      copy += parallel_worker_->prof_copy_seconds_;
      terminal += parallel_worker_->prof_terminal_seconds_;
      fold += parallel_worker_->prof_fold_seconds_;
      showdown += parallel_worker_->prof_showdown_seconds_;
      showdown_accumulate += parallel_worker_->prof_showdown_accumulate_seconds_;
      showdown_prefix += parallel_worker_->prof_showdown_prefix_seconds_;
      showdown_output += parallel_worker_->prof_showdown_output_seconds_;
      value_update += parallel_worker_->prof_value_update_seconds_;
      value_accumulate += parallel_worker_->prof_value_accumulate_seconds_;
      regret_update += parallel_worker_->prof_regret_update_seconds_;
      average_update += parallel_worker_->prof_average_update_seconds_;
      average_only += parallel_worker_->prof_average_only_seconds_;
      chance += parallel_worker_->prof_chance_seconds_;
      chance_prepare += parallel_worker_->prof_chance_prepare_seconds_;
      chance_accumulate += parallel_worker_->prof_chance_accumulate_seconds_;
      sync += parallel_worker_->prof_sync_seconds_;
      wall += parallel_worker_->prof_wall_seconds_;
      decisions += parallel_worker_->prof_decisions_;
      actor_writes += parallel_worker_->prof_actor_writes_;
      strategy_entries += parallel_worker_->prof_strategy_entries_;
      zero_strategy_entries += parallel_worker_->prof_zero_strategy_entries_;
      whole_zero_actions += parallel_worker_->prof_whole_zero_actions_;
      average_only_calls += parallel_worker_->prof_average_only_calls_;
      average_only_nodes += parallel_worker_->prof_average_only_nodes_;
      average_only_decisions += parallel_worker_->prof_average_only_decisions_;
      average_only_entries += parallel_worker_->prof_average_only_entries_;
      chance_calls += parallel_worker_->prof_chance_calls_;
      chance_outcomes += parallel_worker_->prof_chance_outcomes_;
    }
    const double accounted = strategy + copy + terminal + value_update + chance + sync;
    std::fprintf(stderr,
                 "ITER-PROF wall=%.1fms decisions=%llu actor_writes=%llu serial-equiv-parts=%.1fms\n"
                 "  terminal showdown:        %8.1f ms\n"
                 "    fold terminal:          %8.1f ms\n"
                 "    showdown terminal:      %8.1f ms\n"
                 "    rank/card accumulate:   %8.1f ms\n"
                 "    prefix construction:    %8.1f ms\n"
                 "    value production:       %8.1f ms\n"
                 "  reach propagation:        %8.1f ms\n"
                 "  value + update:           %8.1f ms\n"
                 "    value accumulation:    %8.1f ms\n"
                 "    regret update:         %8.1f ms\n"
                 "    average update:        %8.1f ms\n"
                 "    average-only traversal:%8.1f ms (%llu calls, %llu nodes, %llu decisions, %llu entries)\n"
                 "  board/card filtering:     %8.1f ms\n"
                 "    chance preparation:    %8.1f ms\n"
                 "    chance accumulation:   %8.1f ms (%llu calls, %llu outcomes)\n"
                 "  synchronization:          %8.1f ms\n"
                 "  regret matching:          %8.1f ms\n"
                 "  strategy sparsity:        %llu/%llu zero entries, %llu whole-zero actions\n",
                 wall_seconds * 1000.0, static_cast<unsigned long long>(decisions),
                 static_cast<unsigned long long>(actor_writes), accounted * 1000.0,
                 terminal * 1000.0, fold * 1000.0, showdown * 1000.0,
                 showdown_accumulate * 1000.0,
                 showdown_prefix * 1000.0, showdown_output * 1000.0,
                 copy * 1000.0, value_update * 1000.0,
                 value_accumulate * 1000.0, regret_update * 1000.0,
                 average_update * 1000.0,
                 average_only * 1000.0,
                 static_cast<unsigned long long>(average_only_calls),
                 static_cast<unsigned long long>(average_only_nodes),
                 static_cast<unsigned long long>(average_only_decisions),
                 static_cast<unsigned long long>(average_only_entries),
                 chance * 1000.0, chance_prepare * 1000.0,
                 chance_accumulate * 1000.0,
                 static_cast<unsigned long long>(chance_calls),
                 static_cast<unsigned long long>(chance_outcomes),
                 sync * 1000.0, strategy * 1000.0,
                 static_cast<unsigned long long>(zero_strategy_entries),
                 static_cast<unsigned long long>(strategy_entries),
                 static_cast<unsigned long long>(whole_zero_actions));
    std::fprintf(stderr, "  task distribution: main=%llu/%.1fms",
                 static_cast<unsigned long long>(prof_tasks_),
                 prof_task_wall_seconds_ * 1000.0);
    for (std::size_t index = 0; index < parallel_workers_.size(); ++index) {
      std::fprintf(stderr, " w%zu=%llu/%.1fms", index,
                   static_cast<unsigned long long>(parallel_workers_[index]->prof_tasks_),
                   parallel_workers_[index]->prof_task_wall_seconds_ * 1000.0);
    }
    if (parallel_worker_ != nullptr) {
      std::fprintf(stderr, " chain=%llu/%.1fms",
                   static_cast<unsigned long long>(parallel_worker_->prof_tasks_),
                   parallel_worker_->prof_task_wall_seconds_ * 1000.0);
    }
    std::fputc('\n', stderr);
    prof_decisions_ = 0;
    prof_actor_writes_ = 0;
    prof_strategy_entries_ = 0;
    prof_zero_strategy_entries_ = 0;
    prof_whole_zero_actions_ = 0;
    prof_average_only_calls_ = 0;
    prof_average_only_nodes_ = 0;
    prof_average_only_decisions_ = 0;
    prof_average_only_entries_ = 0;
    prof_chance_calls_ = 0;
    prof_chance_outcomes_ = 0;
    prof_tasks_ = 0;
    prof_strategy_seconds_ = 0.0;
    prof_copy_seconds_ = 0.0;
    prof_children_seconds_ = 0.0;
    prof_value_update_seconds_ = 0.0;
    prof_value_accumulate_seconds_ = 0.0;
    prof_regret_update_seconds_ = 0.0;
    prof_average_update_seconds_ = 0.0;
    prof_average_only_seconds_ = 0.0;
    prof_terminal_seconds_ = 0.0;
    prof_fold_seconds_ = 0.0;
    prof_showdown_seconds_ = 0.0;
    prof_showdown_accumulate_seconds_ = 0.0;
    prof_showdown_prefix_seconds_ = 0.0;
    prof_showdown_output_seconds_ = 0.0;
    prof_chance_seconds_ = 0.0;
    prof_chance_prepare_seconds_ = 0.0;
    prof_chance_accumulate_seconds_ = 0.0;
    prof_sync_seconds_ = 0.0;
    prof_wall_seconds_ = 0.0;
    prof_task_wall_seconds_ = 0.0;
    for (const auto &worker : parallel_workers_) {
      worker->prof_decisions_ = 0;
      worker->prof_actor_writes_ = 0;
      worker->prof_strategy_entries_ = 0;
      worker->prof_zero_strategy_entries_ = 0;
      worker->prof_whole_zero_actions_ = 0;
      worker->prof_average_only_calls_ = 0;
      worker->prof_average_only_nodes_ = 0;
      worker->prof_average_only_decisions_ = 0;
      worker->prof_average_only_entries_ = 0;
      worker->prof_chance_calls_ = 0;
      worker->prof_chance_outcomes_ = 0;
      worker->prof_tasks_ = 0;
      worker->prof_strategy_seconds_ = 0.0;
      worker->prof_copy_seconds_ = 0.0;
      worker->prof_children_seconds_ = 0.0;
      worker->prof_value_update_seconds_ = 0.0;
      worker->prof_value_accumulate_seconds_ = 0.0;
      worker->prof_regret_update_seconds_ = 0.0;
      worker->prof_average_update_seconds_ = 0.0;
      worker->prof_average_only_seconds_ = 0.0;
      worker->prof_terminal_seconds_ = 0.0;
      worker->prof_fold_seconds_ = 0.0;
      worker->prof_showdown_seconds_ = 0.0;
      worker->prof_showdown_accumulate_seconds_ = 0.0;
      worker->prof_showdown_prefix_seconds_ = 0.0;
      worker->prof_showdown_output_seconds_ = 0.0;
      worker->prof_chance_seconds_ = 0.0;
      worker->prof_chance_prepare_seconds_ = 0.0;
      worker->prof_chance_accumulate_seconds_ = 0.0;
      worker->prof_sync_seconds_ = 0.0;
      worker->prof_wall_seconds_ = 0.0;
      worker->prof_task_wall_seconds_ = 0.0;
    }
    if (parallel_worker_ != nullptr) {
      parallel_worker_->prof_decisions_ = 0;
      parallel_worker_->prof_actor_writes_ = 0;
      parallel_worker_->prof_strategy_entries_ = 0;
      parallel_worker_->prof_zero_strategy_entries_ = 0;
      parallel_worker_->prof_whole_zero_actions_ = 0;
      parallel_worker_->prof_average_only_calls_ = 0;
      parallel_worker_->prof_average_only_nodes_ = 0;
      parallel_worker_->prof_average_only_decisions_ = 0;
      parallel_worker_->prof_average_only_entries_ = 0;
      parallel_worker_->prof_chance_calls_ = 0;
      parallel_worker_->prof_chance_outcomes_ = 0;
      parallel_worker_->prof_tasks_ = 0;
      parallel_worker_->prof_strategy_seconds_ = 0.0;
      parallel_worker_->prof_copy_seconds_ = 0.0;
      parallel_worker_->prof_children_seconds_ = 0.0;
      parallel_worker_->prof_value_update_seconds_ = 0.0;
      parallel_worker_->prof_value_accumulate_seconds_ = 0.0;
      parallel_worker_->prof_regret_update_seconds_ = 0.0;
      parallel_worker_->prof_average_update_seconds_ = 0.0;
      parallel_worker_->prof_average_only_seconds_ = 0.0;
      parallel_worker_->prof_terminal_seconds_ = 0.0;
      parallel_worker_->prof_fold_seconds_ = 0.0;
      parallel_worker_->prof_showdown_seconds_ = 0.0;
      parallel_worker_->prof_showdown_accumulate_seconds_ = 0.0;
      parallel_worker_->prof_showdown_prefix_seconds_ = 0.0;
      parallel_worker_->prof_showdown_output_seconds_ = 0.0;
      parallel_worker_->prof_chance_seconds_ = 0.0;
      parallel_worker_->prof_chance_prepare_seconds_ = 0.0;
      parallel_worker_->prof_chance_accumulate_seconds_ = 0.0;
      parallel_worker_->prof_sync_seconds_ = 0.0;
      parallel_worker_->prof_wall_seconds_ = 0.0;
      parallel_worker_->prof_task_wall_seconds_ = 0.0;
    }
  }

  Result<ComboVector, PostflopSolverError> cfr(const NodeId node_id,
                                               const std::uint8_t updating_player,
                                               const ReachRef &reach,
                                               const double strategy_weight,
                                               const double regret_update_weight,
                                               const double positive_regret_discount,
                                               const double negative_regret_discount) {
    regret_update_weight_ = regret_update_weight;
    strategy_weight_ = strategy_weight;
    positive_regret_discount_ = positive_regret_discount;
    negative_regret_discount_ = negative_regret_discount;
    for (auto &worker : parallel_workers_) {
      worker->regret_update_weight_ = regret_update_weight;
      worker->strategy_weight_ = strategy_weight;
      worker->positive_regret_discount_ = positive_regret_discount;
      worker->negative_regret_discount_ = negative_regret_discount;
    }
    if (parallel_worker_ != nullptr) {
      parallel_worker_->regret_update_weight_ = regret_update_weight;
      parallel_worker_->strategy_weight_ = strategy_weight;
      parallel_worker_->positive_regret_discount_ = positive_regret_discount;
      parallel_worker_->negative_regret_discount_ = negative_regret_discount;
    }
    if (layout_.uses_canonical_public_dag) {
      return cfr_canonical_parallel_entry(layout_.canonical_public_graph.root, updating_player,
                                          reach, strategy_weight);
    }
    // The physical tree is parallelized at chance nodes (coarse grained), not
    // at every decision node: the per-card subtrees are the big units of work.
    // Out-param adapter: the traversal writes into a local buffer and the
    // Result is only used for the error status (the runner discards the value).
    ComboVector result{};
    const auto error =
        cfr_physical(node_id, updating_player, reach, strategy_weight, true, 1.0, result);
    if (error) {
      return Result<ComboVector, PostflopSolverError>::failure(*error);
    }
    return Result<ComboVector, PostflopSolverError>::success(std::move(result));
  }

  std::optional<PostflopSolverError> cfr_simultaneous(
      const ReachRef &reach, const double strategy_weight,
      const double regret_update_weight, const double positive_regret_discount,
      const double negative_regret_discount) {
    if (!layout_.uses_canonical_public_dag) {
      return PostflopSolverError::InvalidConfiguration;
    }
    regret_update_weight_ = regret_update_weight;
    strategy_weight_ = strategy_weight;
    positive_regret_discount_ = positive_regret_discount;
    negative_regret_discount_ = negative_regret_discount;
    for (auto &worker : parallel_workers_) {
      worker->regret_update_weight_ = regret_update_weight;
      worker->strategy_weight_ = strategy_weight;
      worker->positive_regret_discount_ = positive_regret_discount;
      worker->negative_regret_discount_ = negative_regret_discount;
    }
    std::array<ComboVector, 2> values{};
    return cfr_canonical_simultaneous_into(
        layout_.canonical_public_graph.root, reach, strategy_weight, values);
  }

  Result<ComboVector, PostflopSolverError> policy(const NodeId node_id,
                                                  const std::uint8_t updating_player,
                                                  const ReachRef &reach,
                                                  const bool best_response) {
    if (layout_.uses_canonical_public_dag) {
      return policy_canonical(layout_.canonical_public_graph.root, updating_player, reach,
                              best_response);
    }
    return policy_physical(node_id, updating_player, reach, best_response);
  }

  Result<ComboVector, PostflopSolverError>
  policy_from_physical_node(const NodeId node_id, const std::uint8_t updating_player,
                            const ReachRef &reach) {
    if (layout_.uses_canonical_public_dag) {
      if (node_id >= layout_.canonical_public_graph.physical_assignments.size()) {
        return Result<ComboVector, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      const auto assignment = layout_.canonical_public_graph.physical_assignments[node_id];
      auto canonical_reach = transform_reach(
          {*reach[0], *reach[1]}, assignment.physical_to_canonical_automorphism);
      auto values = policy_canonical(assignment.node, updating_player,
                                     {&canonical_reach[0], &canonical_reach[1]}, false);
      if (!values) {
        return values;
      }
      return Result<ComboVector, PostflopSolverError>::success(transform_values_to_parent(
          values.value(), assignment.physical_to_canonical_automorphism, updating_player));
    }
    return policy_physical(node_id, updating_player, reach, false);
  }

private:
  void mark_deferred_regret_touched(const std::size_t index) {
    if (deferred_regret_touched_flags_[index] != 0U) {
      return;
    }
    deferred_regret_touched_flags_[index] = 1U;
    deferred_regret_touched_.push_back(index);
  }

  void clear_deferred_regret_touched(const std::size_t index) noexcept {
    deferred_regret_touched_flags_[index] = 0U;
  }

  void add_deferred_regret(const std::size_t index, const double delta) {
    mark_deferred_regret_touched(index);
    (*deferred_regret_delta_)[index] += delta;
  }

  void count_work_node(const PublicNodeKind kind,
                       const std::uint64_t chance_outcomes = 0U) noexcept {
    ++work_counters_.visited_nodes;
    switch (kind) {
    case PublicNodeKind::Decision:
      ++work_counters_.decision_node_evaluations;
      break;
    case PublicNodeKind::Chance:
      ++work_counters_.chance_node_evaluations;
      work_counters_.chance_outcome_evaluations += chance_outcomes;
      break;
    case PublicNodeKind::TerminalFold:
      ++work_counters_.terminal_evaluations;
      ++work_counters_.fold_terminal_evaluations;
      break;
    case PublicNodeKind::TerminalShowdown:
      ++work_counters_.terminal_evaluations;
      ++work_counters_.showdown_terminal_evaluations;
      break;
    }
  }

  static void add_work_counters(PostflopWorkCounters &target,
                                const PostflopWorkCounters &source) noexcept {
    target.visited_nodes += source.visited_nodes;
    target.decision_node_evaluations += source.decision_node_evaluations;
    target.chance_node_evaluations += source.chance_node_evaluations;
    target.chance_outcome_evaluations += source.chance_outcome_evaluations;
    target.terminal_evaluations += source.terminal_evaluations;
    target.fold_terminal_evaluations += source.fold_terminal_evaluations;
    target.showdown_terminal_evaluations += source.showdown_terminal_evaluations;
    target.regret_update_entries += source.regret_update_entries;
    target.strategy_update_entries += source.strategy_update_entries;
  }

  [[nodiscard]] std::size_t value_slot(const ComboId combo,
                                       [[maybe_unused]] const std::uint8_t player) const noexcept {
    if constexpr (PlayerIndexed) {
      return static_cast<std::size_t>(layout_.player_flop_slot[player][combo]);
    }
    if constexpr (Capacity == combo_count) {
      return static_cast<std::size_t>(combo);
    }
    return static_cast<std::size_t>(layout_.active_combo_index[combo]);
  }

  [[nodiscard]] std::size_t board_player_slot(const BoardData &board, const std::uint8_t player,
                                              const std::size_t local) const noexcept {
    if constexpr (PlayerIndexed) {
      // Physical traversal values/strategies are board-local: the live-combo
      // list is already a compact 0..N prefix.
      static_cast<void>(board);
      static_cast<void>(player);
      return local;
    }
    return value_slot(board.player_combos[player][local], player);
  }

  [[nodiscard]] std::size_t board_reach_slot(const BoardData &board,
                                             const std::uint8_t player,
                                             const std::size_t local) const noexcept {
    if constexpr (PlayerIndexed) {
      return static_cast<std::size_t>(board.player_flop_slots[player][local]);
    }
    return value_slot(board.player_combos[player][local], player);
  }

  [[nodiscard]] bool scaled_action_major_state() const noexcept {
    return buffers_.scaled_regret != nullptr && buffers_.scaled_strategy != nullptr &&
           buffers_.regret_node_scale != nullptr &&
           buffers_.strategy_node_scale != nullptr;
  }

  [[nodiscard]] bool action_major_compact_state() const noexcept {
    return buffers_.compact_state != nullptr && buffers_.action_major_compact;
  }

  [[nodiscard]] bool aligned_compact_state() const noexcept {
    return buffers_.compact_regret16 != nullptr &&
           buffers_.compact_strategy16 != nullptr;
  }

  void update_action_major_compact_average(
      const CanonicalPublicNode &canonical, const BoardData &board,
      const std::uint8_t player, const ReachRef &reach,
      const double strategy_weight,
      const std::array<ComboVector, maximum_action_count> &strategies,
      const bool local_indexed) {
    if (strategy_weight == 0.0) {
      return;
    }
    const auto action_count = static_cast<std::size_t>(canonical.decision.action_count);
    const auto &combos = board.player_combos[player];
    for (std::size_t action = 0; action < action_count; ++action) {
      for (std::size_t local = 0; local < combos.size(); ++local) {
        const auto slot = value_slot(combos[local], player);
        const auto strategy_slot = local_indexed ? local : slot;
        auto *const bytes = buffers_.compact_state +
            canonical_action_major_index(canonical, local, action) * 3U;
        const auto word = compact_word(bytes);
        const double updated =
            decode_strategy11(static_cast<std::uint16_t>(word >> 13U)) +
            strategy_weight * static_cast<double>((*reach[player])[slot]) *
                static_cast<double>(strategies[action][strategy_slot]);
        store_compact_word(
            bytes, (word & 0x1fffU) |
                       (static_cast<std::uint32_t>(encode_strategy11(updated)) << 13U));
      }
    }
  }

  void update_scaled_average(
      const CanonicalPublicNode &canonical, const BoardData &board,
      const std::uint8_t player, const ReachRef &reach,
      const double strategy_weight,
      std::array<ComboVector, maximum_action_count> &strategies,
      const bool local_indexed) {
    const bool unweighted_dcfr_average = buffers_.signed_scaled_regret;
    if (!unweighted_dcfr_average && strategy_weight == 0.0) {
      return;
    }
    const auto scale_index = static_cast<std::size_t>(canonical.state_scale_index);
    const auto action_count = static_cast<std::size_t>(canonical.decision.action_count);
    const auto &combos = board.player_combos[player];
    const double old_scale =
        static_cast<double>(buffers_.strategy_node_scale[scale_index]) *
        (unweighted_dcfr_average ? strategy_weight : 1.0);
    double maximum = 0.0;
    if constexpr (std::is_same_v<Scalar, float>) {
      const auto &slots = PlayerIndexed ? board.player_flop_slots[player]
                                        : board.player_active_slots[player];
      const __m256 old_scale_vector = _mm256_set1_ps(static_cast<float>(old_scale));
      const __m256 weight_vector = _mm256_set1_ps(static_cast<float>(strategy_weight));
      __m256 maximum_vector = _mm256_setzero_ps();
      for (std::size_t action = 0; action < action_count; ++action) {
        const auto *const source = buffers_.scaled_strategy +
                                   canonical_action_major_index(canonical, 0U, action);
        float *const destination = strategies[action].data();
        std::size_t local = 0U;
        for (; local + 8U <= combos.size(); local += 8U) {
          const __m256i indices = _mm256_cvtepu16_epi32(_mm_loadu_si128(
              reinterpret_cast<const __m128i *>(slots.data() + local)));
          const __m256 old_values = _mm256_mul_ps(
              _mm256_cvtepi32_ps(_mm256_cvtepu16_epi32(_mm_loadu_si128(
                  reinterpret_cast<const __m128i *>(source + local)))),
              old_scale_vector);
          const __m256 current_strategy = local_indexed
              ? _mm256_loadu_ps(destination + local)
              : _mm256_i32gather_ps(destination, indices, 4);
          const __m256 addition = unweighted_dcfr_average
                                      ? current_strategy
                                      : _mm256_mul_ps(
                                            _mm256_mul_ps(
                                                weight_vector,
                                                _mm256_i32gather_ps(
                                                    (*reach[player]).data(),
                                                    indices, 4)),
                                            current_strategy);
          const __m256 updated = _mm256_add_ps(old_values, addition);
          _mm256_storeu_ps(destination + local, updated);
          maximum_vector = _mm256_max_ps(maximum_vector, updated);
        }
        for (; local < combos.size(); ++local) {
          const auto slot = static_cast<std::size_t>(slots[local]);
          const auto strategy_slot = local_indexed ? local : slot;
          const double current = static_cast<double>(destination[strategy_slot]);
          const double updated = static_cast<double>(source[local]) * old_scale +
              (unweighted_dcfr_average
                   ? current
                   : strategy_weight *
                         static_cast<double>((*reach[player])[slot]) * current);
          destination[local] = static_cast<float>(updated);
          maximum = std::max(maximum, updated);
        }
      }
      alignas(32) float maxima[8];
      _mm256_store_ps(maxima, maximum_vector);
      for (const float value : maxima) {
        maximum = std::max(maximum, static_cast<double>(value));
      }
    } else {
      for (std::size_t local = 0; local < combos.size(); ++local) {
        const auto slot = value_slot(combos[local], player);
        const auto strategy_slot = local_indexed ? local : slot;
        const double reach_weight = unweighted_dcfr_average
                                        ? 1.0
                                        : strategy_weight *
                                              static_cast<double>((*reach[player])[slot]);
        for (std::size_t action = 0; action < action_count; ++action) {
          const auto index = canonical_action_major_index(canonical, local, action);
          const double updated =
              static_cast<double>(buffers_.scaled_strategy[index]) * old_scale +
              reach_weight * static_cast<double>(strategies[action][strategy_slot]);
          strategies[action][local] = static_cast<Scalar>(updated);
          maximum = std::max(maximum, updated);
        }
      }
    }
    const float encoded_scale =
        maximum > 0.0 ? static_cast<float>(maximum / 65535.0) : 0.0F;
    buffers_.strategy_node_scale[scale_index] = encoded_scale;
    if (!(encoded_scale > 0.0F)) {
      for (std::size_t action = 0; action < action_count; ++action) {
        auto *const destination =
            buffers_.scaled_strategy +
            canonical_action_major_index(canonical, 0U, action);
        std::fill_n(destination, combos.size(), std::uint16_t{0});
      }
      return;
    }
    const double inverse = 1.0 / static_cast<double>(encoded_scale);
    for (std::size_t action = 0; action < action_count; ++action) {
      auto *const destination =
          buffers_.scaled_strategy +
          canonical_action_major_index(canonical, 0U, action);
      std::size_t local = 0U;
      if constexpr (std::is_same_v<Scalar, float>) {
        const __m256 inverse_vector = _mm256_set1_ps(static_cast<float>(inverse));
        for (; local + 8U <= combos.size(); local += 8U) {
          const __m256 scaled = _mm256_min_ps(
              _mm256_set1_ps(65535.0F),
              _mm256_max_ps(_mm256_setzero_ps(),
                            _mm256_mul_ps(_mm256_loadu_ps(
                                strategies[action].data() + local), inverse_vector)));
          const __m256i encoded = _mm256_cvtps_epi32(scaled);
          const __m128i packed = _mm_packus_epi32(
              _mm256_castsi256_si128(encoded), _mm256_extracti128_si256(encoded, 1));
          _mm_storeu_si128(reinterpret_cast<__m128i *>(destination + local), packed);
        }
      }
      for (; local < combos.size(); ++local) {
        destination[local] = static_cast<std::uint16_t>(std::clamp(
            std::nearbyint(static_cast<double>(strategies[action][local]) * inverse),
            0.0, 65535.0));
      }
    }
  }

  template <bool BoardLocal = false, std::size_t FixedActionCount = 0U>
  void update_scaled_regrets(
      const CanonicalPublicNode &canonical, const BoardData &board,
      const std::uint8_t player, ComboVector &values,
      std::array<ComboVector, maximum_action_count> &action_values,
      std::array<ComboVector, maximum_action_count> &scratch,
      std::array<ComboVector, maximum_action_count> *const average_scratch = nullptr,
      const ReachRef *const average_reach = nullptr,
      const bool compute_values = false,
      const bool decode_current_strategy = false) {
    const auto scale_index = static_cast<std::size_t>(canonical.state_scale_index);
    const std::size_t action_count =
        FixedActionCount != 0U
            ? FixedActionCount
            : static_cast<std::size_t>(canonical.decision.action_count);
    const auto &combos = board.player_combos[player];
    const double old_scale =
        static_cast<double>(buffers_.regret_node_scale[scale_index]);
    if (buffers_.signed_scaled_regret) {
      if (average_scratch == nullptr || average_reach == nullptr) {
        return;
      }
      auto &average_values = *average_scratch;
      double maximum_magnitude = 0.0;
      double maximum_average = 0.0;
      const double old_average_scale =
          static_cast<double>(buffers_.strategy_node_scale[scale_index]);
      if constexpr (std::is_same_v<Scalar, float>) {
        const auto &slots = PlayerIndexed ? board.player_flop_slots[player]
                                          : board.player_active_slots[player];
        const auto slot_at = [&slots](const std::size_t local) {
          if constexpr (BoardLocal) {
            return local;
          } else {
            return static_cast<std::size_t>(slots[local]);
          }
        };
        const auto load_action = [](const float *const source,
                                    const __m256i indices,
                                    const std::size_t local) {
          (void)indices;
          (void)local;
          if constexpr (BoardLocal) {
            return _mm256_loadu_ps(source + local);
          } else {
            return _mm256_i32gather_ps(source, indices, 4);
          }
        };
        const auto load_reach = [](const float *const source,
                                   const __m256i indices,
                                   const std::size_t local) {
          (void)indices;
          (void)local;
          if constexpr (BoardLocal) {
            return _mm256_loadu_ps(source + local);
          } else {
            return _mm256_i32gather_ps(source, indices, 4);
          }
        };
        const auto store_values = [&slots](float *const destination,
                                           const __m256 value,
                                           const std::size_t local) {
          if constexpr (BoardLocal) {
            _mm256_storeu_ps(destination + local, value);
          } else {
            alignas(32) float lanes[8];
            _mm256_store_ps(lanes, value);
            for (std::size_t lane = 0U; lane < 8U; ++lane) {
              destination[slots[local + lane]] = lanes[lane];
            }
          }
        };
        const __m256 old_scale_vector = _mm256_set1_ps(static_cast<float>(old_scale));
        const __m256 old_average_scale_vector =
            _mm256_set1_ps(static_cast<float>(old_average_scale));
        const __m256 average_weight_vector =
            _mm256_set1_ps(static_cast<float>(strategy_weight_));
        const __m256 regret_weight =
            _mm256_set1_ps(static_cast<float>(regret_update_weight_));
        const __m256 positive_discount =
            _mm256_set1_ps(static_cast<float>(positive_regret_discount_));
        const __m256 negative_discount =
            _mm256_set1_ps(static_cast<float>(negative_regret_discount_));
        const __m256 zero = _mm256_setzero_ps();
        const __m256 sign_mask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff));
        __m256 maximum_vector = zero;
        __m256 maximum_average_vector = zero;
        if (compute_values && action_count == 2U) {
          const auto *const regret_source_0 = buffers_.scaled_regret +
              canonical_action_major_index(canonical, 0U, 0U);
          const auto *const regret_source_1 = buffers_.scaled_regret +
              canonical_action_major_index(canonical, 0U, 1U);
          const auto *const average_source_0 = buffers_.scaled_strategy +
              canonical_action_major_index(canonical, 0U, 0U);
          const auto *const average_source_1 = buffers_.scaled_strategy +
              canonical_action_major_index(canonical, 0U, 1U);
          std::size_t local = 0U;
          for (; local + 8U <= combos.size(); local += 8U) {
            const __m256i indices = _mm256_cvtepu16_epi32(_mm_loadu_si128(
                reinterpret_cast<const __m128i *>(slots.data() + local)));
            const __m256 action_0 =
                load_action(action_values[0].data(), indices, local);
            const __m256 action_1 =
                load_action(action_values[1].data(), indices, local);
            __m256 strategy_0;
            __m256 strategy_1;
            if (decode_current_strategy) {
              const __m256i positive_0 = _mm256_max_epi32(
                  _mm256_cvtepi16_epi32(_mm_loadu_si128(
                      reinterpret_cast<const __m128i *>(regret_source_0 + local))),
                  _mm256_setzero_si256());
              const __m256i positive_1 = _mm256_max_epi32(
                  _mm256_cvtepi16_epi32(_mm_loadu_si128(
                      reinterpret_cast<const __m128i *>(regret_source_1 + local))),
                  _mm256_setzero_si256());
              const __m256 sum = _mm256_cvtepi32_ps(
                  _mm256_add_epi32(positive_0, positive_1));
              const __m256 no_positive =
                  _mm256_cmp_ps(sum, zero, _CMP_LE_OQ);
              const __m256 safe_sum = _mm256_blendv_ps(
                  sum, _mm256_set1_ps(1.0F), no_positive);
              const __m256 estimate = _mm256_rcp_ps(safe_sum);
              const __m256 inverse = _mm256_mul_ps(
                  estimate,
                  _mm256_sub_ps(_mm256_set1_ps(2.0F),
                                _mm256_mul_ps(safe_sum, estimate)));
              strategy_0 = _mm256_blendv_ps(
                  _mm256_mul_ps(_mm256_cvtepi32_ps(positive_0), inverse),
                  _mm256_set1_ps(0.5F), no_positive);
              strategy_1 = _mm256_blendv_ps(
                  _mm256_mul_ps(_mm256_cvtepi32_ps(positive_1), inverse),
                  _mm256_set1_ps(0.5F), no_positive);
            } else {
              strategy_0 = _mm256_loadu_ps(scratch[0].data() + local);
              strategy_1 = _mm256_loadu_ps(scratch[1].data() + local);
            }
            __m256 current = _mm256_add_ps(
                zero, _mm256_mul_ps(strategy_0, action_0));
            current = _mm256_add_ps(
                current, _mm256_mul_ps(strategy_1, action_1));
            store_values(values.data(), current, local);
            const __m256 reach_vector = load_reach(
                (*average_reach)[player]->data(), indices, local);
            const auto update_action = [&](const std::size_t action,
                                           const std::uint16_t *const regret_source,
                                           const std::uint16_t *const average_source,
                                           const __m256 action_vector,
                                           const __m256 strategy) {
              const __m256i signed_codes = _mm256_cvtepi16_epi32(
                  _mm_loadu_si128(reinterpret_cast<const __m128i *>(
                      regret_source + local)));
              const __m256 old_values = _mm256_mul_ps(
                  _mm256_cvtepi32_ps(signed_codes), old_scale_vector);
              const __m256 discounts = _mm256_blendv_ps(
                  negative_discount, positive_discount,
                  _mm256_cmp_ps(old_values, zero, _CMP_GT_OQ));
              const __m256 updated = _mm256_add_ps(
                  _mm256_mul_ps(old_values, discounts),
                  _mm256_mul_ps(regret_weight,
                                _mm256_sub_ps(action_vector, current)));
              const __m256 updated_average = _mm256_add_ps(
                  _mm256_mul_ps(
                      _mm256_cvtepi32_ps(_mm256_cvtepu16_epi32(
                          _mm_loadu_si128(reinterpret_cast<const __m128i *>(
                              average_source + local)))),
                      old_average_scale_vector),
                  _mm256_mul_ps(average_weight_vector,
                                _mm256_mul_ps(reach_vector, strategy)));
              _mm256_storeu_ps(
                  average_values[action].data() + local, updated_average);
              maximum_average_vector = _mm256_max_ps(
                  maximum_average_vector, updated_average);
              _mm256_storeu_ps(scratch[action].data() + local, updated);
              maximum_vector = _mm256_max_ps(
                  maximum_vector, _mm256_and_ps(updated, sign_mask));
            };
            update_action(0U, regret_source_0, average_source_0,
                          action_0, strategy_0);
            update_action(1U, regret_source_1, average_source_1,
                          action_1, strategy_1);
          }
          for (; local < combos.size(); ++local) {
            const auto slot = slot_at(local);
            const auto raw_0 = static_cast<std::int16_t>(regret_source_0[local]);
            const auto raw_1 = static_cast<std::int16_t>(regret_source_1[local]);
            const auto positive_0 = std::max<std::int32_t>(0, raw_0);
            const auto positive_1 = std::max<std::int32_t>(0, raw_1);
            const auto positive_sum = positive_0 + positive_1;
            const Scalar strategy_0 = decode_current_strategy
                ? (positive_sum == 0
                       ? Scalar{0.5F}
                       : static_cast<Scalar>(positive_0) *
                             static_cast<Scalar>(1.0 / static_cast<double>(positive_sum)))
                : scratch[0][local];
            const Scalar strategy_1 = decode_current_strategy
                ? (positive_sum == 0
                       ? Scalar{0.5F}
                       : static_cast<Scalar>(positive_1) *
                             static_cast<Scalar>(1.0 / static_cast<double>(positive_sum)))
                : scratch[1][local];
            Scalar current = Scalar{0};
            current = static_cast<Scalar>(
                current + strategy_0 * action_values[0][slot]);
            current = static_cast<Scalar>(
                current + strategy_1 * action_values[1][slot]);
            values[slot] = current;
            const auto update_action = [&](const std::size_t action,
                                           const std::uint16_t *const regret_source,
                                           const std::uint16_t *const average_source) {
              const double old = static_cast<double>(
                                     static_cast<std::int16_t>(regret_source[local])) *
                                 old_scale;
              const double updated =
                  old * (old > 0.0 ? positive_regret_discount_
                                   : negative_regret_discount_) +
                  regret_update_weight_ *
                      (static_cast<double>(action_values[action][slot]) -
                       static_cast<double>(current));
              const double updated_average =
                  static_cast<double>(average_source[local]) * old_average_scale +
                  strategy_weight_ *
                      static_cast<double>((*(*average_reach)[player])[slot]) *
                      static_cast<double>(scratch[action][local]);
              average_values[action][local] =
                  static_cast<Scalar>(updated_average);
              maximum_average =
                  std::max(maximum_average, updated_average);
              scratch[action][local] = static_cast<Scalar>(updated);
              maximum_magnitude =
                  std::max(maximum_magnitude, std::abs(updated));
            };
            if (decode_current_strategy) {
              scratch[0][local] = strategy_0;
              scratch[1][local] = strategy_1;
            }
            update_action(0U, regret_source_0, average_source_0);
            update_action(1U, regret_source_1, average_source_1);
          }
        } else if (compute_values && action_count == 3U) {
          const auto *const regret_source_0 = buffers_.scaled_regret +
              canonical_action_major_index(canonical, 0U, 0U);
          const auto *const regret_source_1 = buffers_.scaled_regret +
              canonical_action_major_index(canonical, 0U, 1U);
          const auto *const regret_source_2 = buffers_.scaled_regret +
              canonical_action_major_index(canonical, 0U, 2U);
          const auto *const average_source_0 = buffers_.scaled_strategy +
              canonical_action_major_index(canonical, 0U, 0U);
          const auto *const average_source_1 = buffers_.scaled_strategy +
              canonical_action_major_index(canonical, 0U, 1U);
          const auto *const average_source_2 = buffers_.scaled_strategy +
              canonical_action_major_index(canonical, 0U, 2U);
          std::size_t local = 0U;
          for (; local + 8U <= combos.size(); local += 8U) {
            const __m256i indices = _mm256_cvtepu16_epi32(_mm_loadu_si128(
                reinterpret_cast<const __m128i *>(slots.data() + local)));
            const __m256 action_0 =
                load_action(action_values[0].data(), indices, local);
            const __m256 action_1 =
                load_action(action_values[1].data(), indices, local);
            const __m256 action_2 =
                load_action(action_values[2].data(), indices, local);
            __m256 strategy_0;
            __m256 strategy_1;
            __m256 strategy_2;
            if (decode_current_strategy) {
              const __m256i positive_0 = _mm256_max_epi32(
                  _mm256_cvtepi16_epi32(_mm_loadu_si128(
                      reinterpret_cast<const __m128i *>(regret_source_0 + local))),
                  _mm256_setzero_si256());
              const __m256i positive_1 = _mm256_max_epi32(
                  _mm256_cvtepi16_epi32(_mm_loadu_si128(
                      reinterpret_cast<const __m128i *>(regret_source_1 + local))),
                  _mm256_setzero_si256());
              const __m256i positive_2 = _mm256_max_epi32(
                  _mm256_cvtepi16_epi32(_mm_loadu_si128(
                      reinterpret_cast<const __m128i *>(regret_source_2 + local))),
                  _mm256_setzero_si256());
              const __m256 sum = _mm256_cvtepi32_ps(_mm256_add_epi32(
                  _mm256_add_epi32(positive_0, positive_1), positive_2));
              const __m256 no_positive =
                  _mm256_cmp_ps(sum, zero, _CMP_LE_OQ);
              const __m256 safe_sum = _mm256_blendv_ps(
                  sum, _mm256_set1_ps(1.0F), no_positive);
              const __m256 estimate = _mm256_rcp_ps(safe_sum);
              const __m256 inverse = _mm256_mul_ps(
                  estimate,
                  _mm256_sub_ps(_mm256_set1_ps(2.0F),
                                _mm256_mul_ps(safe_sum, estimate)));
              const __m256 uniform = _mm256_set1_ps(1.0F / 3.0F);
              strategy_0 = _mm256_blendv_ps(
                  _mm256_mul_ps(_mm256_cvtepi32_ps(positive_0), inverse),
                  uniform, no_positive);
              strategy_1 = _mm256_blendv_ps(
                  _mm256_mul_ps(_mm256_cvtepi32_ps(positive_1), inverse),
                  uniform, no_positive);
              strategy_2 = _mm256_blendv_ps(
                  _mm256_mul_ps(_mm256_cvtepi32_ps(positive_2), inverse),
                  uniform, no_positive);
            } else {
              strategy_0 = _mm256_loadu_ps(scratch[0].data() + local);
              strategy_1 = _mm256_loadu_ps(scratch[1].data() + local);
              strategy_2 = _mm256_loadu_ps(scratch[2].data() + local);
            }
            __m256 current = _mm256_add_ps(
                zero, _mm256_mul_ps(strategy_0, action_0));
            current = _mm256_add_ps(
                current, _mm256_mul_ps(strategy_1, action_1));
            current = _mm256_add_ps(
                current, _mm256_mul_ps(strategy_2, action_2));
            store_values(values.data(), current, local);
            const __m256 reach_vector = load_reach(
                (*average_reach)[player]->data(), indices, local);
            const auto update_action = [&](const std::size_t action,
                                           const std::uint16_t *const regret_source,
                                           const std::uint16_t *const average_source,
                                           const __m256 action_vector,
                                           const __m256 strategy) {
              const __m256i signed_codes = _mm256_cvtepi16_epi32(
                  _mm_loadu_si128(reinterpret_cast<const __m128i *>(
                      regret_source + local)));
              const __m256 old_values = _mm256_mul_ps(
                  _mm256_cvtepi32_ps(signed_codes), old_scale_vector);
              const __m256 discounts = _mm256_blendv_ps(
                  negative_discount, positive_discount,
                  _mm256_cmp_ps(old_values, zero, _CMP_GT_OQ));
              const __m256 updated = _mm256_add_ps(
                  _mm256_mul_ps(old_values, discounts),
                  _mm256_mul_ps(regret_weight,
                                _mm256_sub_ps(action_vector, current)));
              const __m256 updated_average = _mm256_add_ps(
                  _mm256_mul_ps(
                      _mm256_cvtepi32_ps(_mm256_cvtepu16_epi32(
                          _mm_loadu_si128(reinterpret_cast<const __m128i *>(
                              average_source + local)))),
                      old_average_scale_vector),
                  _mm256_mul_ps(average_weight_vector,
                                _mm256_mul_ps(reach_vector, strategy)));
              _mm256_storeu_ps(
                  average_values[action].data() + local, updated_average);
              maximum_average_vector = _mm256_max_ps(
                  maximum_average_vector, updated_average);
              _mm256_storeu_ps(scratch[action].data() + local, updated);
              maximum_vector = _mm256_max_ps(
                  maximum_vector, _mm256_and_ps(updated, sign_mask));
            };
            update_action(0U, regret_source_0, average_source_0,
                          action_0, strategy_0);
            update_action(1U, regret_source_1, average_source_1,
                          action_1, strategy_1);
            update_action(2U, regret_source_2, average_source_2,
                          action_2, strategy_2);
          }
          for (; local < combos.size(); ++local) {
            const auto slot = slot_at(local);
            const std::array<std::int32_t, 3> positive{
                std::max<std::int32_t>(
                    0, static_cast<std::int16_t>(regret_source_0[local])),
                std::max<std::int32_t>(
                    0, static_cast<std::int16_t>(regret_source_1[local])),
                std::max<std::int32_t>(
                    0, static_cast<std::int16_t>(regret_source_2[local]))};
            const auto positive_sum = positive[0] + positive[1] + positive[2];
            const Scalar uniform = static_cast<Scalar>(1.0F / 3.0F);
            const Scalar inverse = positive_sum == 0
                                       ? Scalar{0}
                                       : static_cast<Scalar>(
                                             1.0 / static_cast<double>(positive_sum));
            const std::array<Scalar, 3> decoded{
                positive_sum == 0 ? uniform
                                  : static_cast<Scalar>(positive[0]) * inverse,
                positive_sum == 0 ? uniform
                                  : static_cast<Scalar>(positive[1]) * inverse,
                positive_sum == 0 ? uniform
                                  : static_cast<Scalar>(positive[2]) * inverse};
            const Scalar strategy_0 =
                decode_current_strategy ? decoded[0] : scratch[0][local];
            const Scalar strategy_1 =
                decode_current_strategy ? decoded[1] : scratch[1][local];
            const Scalar strategy_2 =
                decode_current_strategy ? decoded[2] : scratch[2][local];
            Scalar current = Scalar{0};
            current = static_cast<Scalar>(
                current + strategy_0 * action_values[0][slot]);
            current = static_cast<Scalar>(
                current + strategy_1 * action_values[1][slot]);
            current = static_cast<Scalar>(
                current + strategy_2 * action_values[2][slot]);
            values[slot] = current;
            const auto update_action = [&](const std::size_t action,
                                           const std::uint16_t *const regret_source,
                                           const std::uint16_t *const average_source) {
              const double old = static_cast<double>(
                                     static_cast<std::int16_t>(regret_source[local])) *
                                 old_scale;
              const double updated =
                  old * (old > 0.0 ? positive_regret_discount_
                                   : negative_regret_discount_) +
                  regret_update_weight_ *
                      (static_cast<double>(action_values[action][slot]) -
                       static_cast<double>(current));
              const double updated_average =
                  static_cast<double>(average_source[local]) * old_average_scale +
                  strategy_weight_ *
                      static_cast<double>((*(*average_reach)[player])[slot]) *
                      static_cast<double>(scratch[action][local]);
              average_values[action][local] =
                  static_cast<Scalar>(updated_average);
              maximum_average =
                  std::max(maximum_average, updated_average);
              scratch[action][local] = static_cast<Scalar>(updated);
              maximum_magnitude =
                  std::max(maximum_magnitude, std::abs(updated));
            };
            if (decode_current_strategy) {
              scratch[0][local] = strategy_0;
              scratch[1][local] = strategy_1;
              scratch[2][local] = strategy_2;
            }
            update_action(0U, regret_source_0, average_source_0);
            update_action(1U, regret_source_1, average_source_1);
            update_action(2U, regret_source_2, average_source_2);
          }
        } else if (compute_values) {
          std::size_t local = 0U;
          for (; local + 8U <= combos.size(); local += 8U) {
            const __m256i indices = _mm256_cvtepu16_epi32(_mm_loadu_si128(
                reinterpret_cast<const __m128i *>(slots.data() + local)));
            std::array<__m256, maximum_action_count> action_vectors{};
            std::array<__m256, maximum_action_count> current_strategies{};
            if (decode_current_strategy) {
              std::array<__m256i, maximum_action_count> positive_codes{};
              __m256i integer_sum = _mm256_setzero_si256();
              for (std::size_t action = 0U; action < action_count; ++action) {
                const auto *const source = buffers_.scaled_regret +
                    canonical_action_major_index(canonical, 0U, action);
                positive_codes[action] = _mm256_max_epi32(
                    _mm256_cvtepi16_epi32(_mm_loadu_si128(
                        reinterpret_cast<const __m128i *>(source + local))),
                    _mm256_setzero_si256());
                integer_sum = _mm256_add_epi32(integer_sum,
                                               positive_codes[action]);
              }
              const __m256 sum = _mm256_cvtepi32_ps(integer_sum);
              const __m256 no_positive =
                  _mm256_cmp_ps(sum, zero, _CMP_LE_OQ);
              const __m256 safe_sum = _mm256_blendv_ps(
                  sum, _mm256_set1_ps(1.0F), no_positive);
              const __m256 estimate = _mm256_rcp_ps(safe_sum);
              const __m256 inverse = _mm256_mul_ps(
                  estimate,
                  _mm256_sub_ps(_mm256_set1_ps(2.0F),
                                _mm256_mul_ps(safe_sum, estimate)));
              const __m256 uniform = _mm256_set1_ps(
                  1.0F / static_cast<float>(action_count));
              for (std::size_t action = 0U; action < action_count; ++action) {
                current_strategies[action] = _mm256_blendv_ps(
                    _mm256_mul_ps(_mm256_cvtepi32_ps(positive_codes[action]),
                                  inverse),
                    uniform, no_positive);
              }
            } else {
              for (std::size_t action = 0U; action < action_count; ++action) {
                current_strategies[action] = _mm256_loadu_ps(
                    scratch[action].data() + local);
              }
            }
            __m256 current = zero;
            for (std::size_t action = 0U; action < action_count; ++action) {
              action_vectors[action] =
                  load_action(action_values[action].data(), indices, local);
              current = _mm256_add_ps(
                  current,
                  _mm256_mul_ps(current_strategies[action],
                                action_vectors[action]));
            }
            store_values(values.data(), current, local);
            const __m256 reach_vector = load_reach(
                (*average_reach)[player]->data(), indices, local);
            for (std::size_t action = 0U; action < action_count; ++action) {
              const auto *const source = buffers_.scaled_regret +
                  canonical_action_major_index(canonical, 0U, action);
              const auto *const average_source = buffers_.scaled_strategy +
                  canonical_action_major_index(canonical, 0U, action);
              float *const destination = scratch[action].data();
              float *const average_destination = average_values[action].data();
              const __m256 strategy = current_strategies[action];
              const __m256i signed_codes = _mm256_cvtepi16_epi32(
                  _mm_loadu_si128(reinterpret_cast<const __m128i *>(source + local)));
              const __m256 old_values = _mm256_mul_ps(
                  _mm256_cvtepi32_ps(signed_codes), old_scale_vector);
              const __m256 discounts = _mm256_blendv_ps(
                  negative_discount, positive_discount,
                  _mm256_cmp_ps(old_values, zero, _CMP_GT_OQ));
              const __m256 updated = _mm256_add_ps(
                  _mm256_mul_ps(old_values, discounts),
                  _mm256_mul_ps(regret_weight,
                                _mm256_sub_ps(action_vectors[action], current)));
              const __m256 updated_average = _mm256_add_ps(
                  _mm256_mul_ps(
                      _mm256_cvtepi32_ps(_mm256_cvtepu16_epi32(
                          _mm_loadu_si128(reinterpret_cast<const __m128i *>(
                              average_source + local)))),
                      old_average_scale_vector),
                  _mm256_mul_ps(average_weight_vector,
                                _mm256_mul_ps(reach_vector, strategy)));
              _mm256_storeu_ps(average_destination + local, updated_average);
              maximum_average_vector = _mm256_max_ps(
                  maximum_average_vector, updated_average);
              _mm256_storeu_ps(destination + local, updated);
              maximum_vector = _mm256_max_ps(
                  maximum_vector, _mm256_and_ps(updated, sign_mask));
            }
          }
          for (; local < combos.size(); ++local) {
            const auto slot = slot_at(local);
            std::array<Scalar, maximum_action_count> current_strategies{};
            if (decode_current_strategy) {
              std::array<std::int32_t, maximum_action_count> positive{};
              std::int32_t sum = 0;
              for (std::size_t action = 0U; action < action_count; ++action) {
                const auto *const source = buffers_.scaled_regret +
                    canonical_action_major_index(canonical, 0U, action);
                positive[action] = std::max<std::int32_t>(
                    0, static_cast<std::int16_t>(source[local]));
                sum += positive[action];
              }
              const Scalar inverse =
                  sum == 0
                      ? Scalar{0}
                      : static_cast<Scalar>(1.0 / static_cast<double>(sum));
              const Scalar uniform = static_cast<Scalar>(
                  1.0 / static_cast<double>(action_count));
              for (std::size_t action = 0U; action < action_count; ++action) {
                current_strategies[action] =
                    sum == 0 ? uniform
                             : static_cast<Scalar>(positive[action]) * inverse;
              }
            } else {
              for (std::size_t action = 0U; action < action_count; ++action) {
                current_strategies[action] = scratch[action][local];
              }
            }
            Scalar current = Scalar{0};
            for (std::size_t action = 0U; action < action_count; ++action) {
              current = static_cast<Scalar>(
                  current + current_strategies[action] *
                                action_values[action][slot]);
            }
            values[slot] = current;
            for (std::size_t action = 0U; action < action_count; ++action) {
              const auto *const source = buffers_.scaled_regret +
                  canonical_action_major_index(canonical, 0U, action);
              const auto *const average_source = buffers_.scaled_strategy +
                  canonical_action_major_index(canonical, 0U, action);
              const double old = static_cast<double>(
                                     static_cast<std::int16_t>(source[local])) *
                                 old_scale;
              const double updated =
                  old * (old > 0.0 ? positive_regret_discount_
                                   : negative_regret_discount_) +
                  regret_update_weight_ *
                      (static_cast<double>(action_values[action][slot]) -
                       static_cast<double>(current));
              const double updated_average =
                  static_cast<double>(average_source[local]) * old_average_scale +
                  strategy_weight_ *
                      static_cast<double>((*(*average_reach)[player])[slot]) *
                      static_cast<double>(current_strategies[action]);
              average_values[action][local] =
                  static_cast<Scalar>(updated_average);
              maximum_average =
                  std::max(maximum_average, updated_average);
              scratch[action][local] = static_cast<Scalar>(updated);
              maximum_magnitude = std::max(maximum_magnitude, std::abs(updated));
            }
          }
        } else for (std::size_t action = 0; action < action_count; ++action) {
          const auto *const source = buffers_.scaled_regret +
                                     canonical_action_major_index(canonical, 0U, action);
          const auto *const average_source = buffers_.scaled_strategy +
              canonical_action_major_index(canonical, 0U, action);
          float *const destination = scratch[action].data();
          float *const average_destination = average_values[action].data();
          std::size_t local = 0U;
          for (; local + 8U <= combos.size(); local += 8U) {
            const __m256i indices = _mm256_cvtepu16_epi32(_mm_loadu_si128(
                reinterpret_cast<const __m128i *>(slots.data() + local)));
            const __m256i signed_codes = _mm256_cvtepi16_epi32(
                _mm_loadu_si128(reinterpret_cast<const __m128i *>(source + local)));
            const __m256 old_values = _mm256_mul_ps(
                _mm256_cvtepi32_ps(signed_codes), old_scale_vector);
            const __m256 discounts = _mm256_blendv_ps(
                negative_discount, positive_discount,
                _mm256_cmp_ps(old_values, zero, _CMP_GT_OQ));
            const __m256 updated = _mm256_add_ps(
                _mm256_mul_ps(old_values, discounts),
                _mm256_mul_ps(
                    regret_weight,
                    _mm256_sub_ps(
                        _mm256_i32gather_ps(action_values[action].data(), indices, 4),
                        _mm256_i32gather_ps(values.data(), indices, 4))));
            const __m256 updated_average = _mm256_add_ps(
                _mm256_mul_ps(
                    _mm256_cvtepi32_ps(_mm256_cvtepu16_epi32(
                        _mm_loadu_si128(reinterpret_cast<const __m128i *>(
                            average_source + local)))),
                    old_average_scale_vector),
                _mm256_mul_ps(
                    average_weight_vector,
                    _mm256_mul_ps(
                        _mm256_i32gather_ps(
                            (*average_reach)[player]->data(), indices, 4),
                        _mm256_loadu_ps(destination + local))));
            _mm256_storeu_ps(average_destination + local, updated_average);
            maximum_average_vector = _mm256_max_ps(
                maximum_average_vector, updated_average);
            _mm256_storeu_ps(destination + local, updated);
            maximum_vector = _mm256_max_ps(
                maximum_vector, _mm256_and_ps(updated, sign_mask));
          }
          for (; local < combos.size(); ++local) {
            const auto slot = static_cast<std::size_t>(slots[local]);
            const double old = static_cast<double>(
                                   static_cast<std::int16_t>(source[local])) *
                               old_scale;
            const double updated =
                old * (old > 0.0 ? positive_regret_discount_
                                 : negative_regret_discount_) +
                regret_update_weight_ *
                    (static_cast<double>(action_values[action][slot]) -
                     static_cast<double>(values[slot]));
            const double updated_average =
                static_cast<double>(average_source[local]) *
                    old_average_scale +
                strategy_weight_ *
                    static_cast<double>((*(*average_reach)[player])[slot]) *
                    static_cast<double>(destination[local]);
            average_destination[local] = static_cast<float>(updated_average);
            maximum_average = std::max(maximum_average, updated_average);
            destination[local] = static_cast<float>(updated);
            maximum_magnitude = std::max(maximum_magnitude, std::abs(updated));
          }
        }
        alignas(32) float maxima[8];
        _mm256_store_ps(maxima, maximum_vector);
        for (const float value : maxima) {
          maximum_magnitude = std::max(maximum_magnitude,
                                       static_cast<double>(value));
        }
        _mm256_store_ps(maxima, maximum_average_vector);
        for (const float value : maxima) {
          maximum_average = std::max(maximum_average,
                                     static_cast<double>(value));
        }
      } else {
        for (std::size_t action = 0; action < action_count; ++action) {
          const auto *const source = buffers_.scaled_regret +
                                     canonical_action_major_index(canonical, 0U, action);
          const auto *const average_source = buffers_.scaled_strategy +
              canonical_action_major_index(canonical, 0U, action);
          for (std::size_t local = 0; local < combos.size(); ++local) {
            const auto slot = value_slot(combos[local], player);
            const double old = static_cast<double>(
                                   static_cast<std::int16_t>(source[local])) *
                               old_scale;
            const double updated =
                old * (old > 0.0 ? positive_regret_discount_
                                 : negative_regret_discount_) +
                regret_update_weight_ *
                    (static_cast<double>(action_values[action][slot]) -
                     static_cast<double>(values[slot]));
            const double updated_average =
                static_cast<double>(average_source[local]) *
                    old_average_scale +
                strategy_weight_ *
                    static_cast<double>((*(*average_reach)[player])[slot]) *
                    static_cast<double>(scratch[action][local]);
            average_values[action][local] =
                static_cast<Scalar>(updated_average);
            maximum_average = std::max(maximum_average, updated_average);
            scratch[action][local] = static_cast<Scalar>(updated);
            maximum_magnitude = std::max(maximum_magnitude, std::abs(updated));
          }
        }
      }
      const float encoded_scale = maximum_magnitude > 0.0
                                      ? static_cast<float>(maximum_magnitude / 32767.0)
                                      : 0.0F;
      const float average_scale = maximum_average > 0.0
                                      ? static_cast<float>(maximum_average / 65535.0)
                                      : 0.0F;
      buffers_.regret_node_scale[scale_index] = encoded_scale;
      buffers_.strategy_node_scale[scale_index] = average_scale;
      if (encoded_scale > 0.0F && average_scale > 0.0F) {
        const double regret_inverse =
            1.0 / static_cast<double>(encoded_scale);
        const double average_inverse =
            1.0 / static_cast<double>(average_scale);
        for (std::size_t action = 0U; action < action_count; ++action) {
          auto *const regret_destination = buffers_.scaled_regret +
              canonical_action_major_index(canonical, 0U, action);
          auto *const average_destination = buffers_.scaled_strategy +
              canonical_action_major_index(canonical, 0U, action);
          std::size_t local = 0U;
          if constexpr (std::is_same_v<Scalar, float>) {
            const __m256 regret_inverse_vector =
                _mm256_set1_ps(static_cast<float>(regret_inverse));
            const __m256 average_inverse_vector =
                _mm256_set1_ps(static_cast<float>(average_inverse));
            const __m256 regret_low = _mm256_set1_ps(-32767.0F);
            const __m256 regret_high = _mm256_set1_ps(32767.0F);
            const __m256 average_high = _mm256_set1_ps(65535.0F);
            const __m256 zero = _mm256_setzero_ps();
            for (; local + 8U <= combos.size(); local += 8U) {
              const __m256 regret_scaled = _mm256_min_ps(
                  regret_high,
                  _mm256_max_ps(
                      regret_low,
                      _mm256_mul_ps(
                          _mm256_loadu_ps(scratch[action].data() + local),
                          regret_inverse_vector)));
              const __m256 average_scaled = _mm256_min_ps(
                  average_high,
                  _mm256_max_ps(
                      zero,
                      _mm256_mul_ps(
                          _mm256_loadu_ps(average_values[action].data() + local),
                          average_inverse_vector)));
              const __m256i regret_encoded =
                  _mm256_cvtps_epi32(regret_scaled);
              const __m256i average_encoded =
                  _mm256_cvtps_epi32(average_scaled);
              _mm_storeu_si128(
                  reinterpret_cast<__m128i *>(regret_destination + local),
                  _mm_packs_epi32(
                      _mm256_castsi256_si128(regret_encoded),
                      _mm256_extracti128_si256(regret_encoded, 1)));
              _mm_storeu_si128(
                  reinterpret_cast<__m128i *>(average_destination + local),
                  _mm_packus_epi32(
                      _mm256_castsi256_si128(average_encoded),
                      _mm256_extracti128_si256(average_encoded, 1)));
            }
          }
          for (; local < combos.size(); ++local) {
            const auto regret_encoded = static_cast<std::int16_t>(std::clamp(
                std::nearbyint(static_cast<double>(scratch[action][local]) *
                               regret_inverse),
                -32767.0, 32767.0));
            regret_destination[local] =
                static_cast<std::uint16_t>(regret_encoded);
            average_destination[local] = static_cast<std::uint16_t>(std::clamp(
                std::nearbyint(
                    static_cast<double>(average_values[action][local]) *
                    average_inverse),
                0.0, 65535.0));
          }
        }
        return;
      }
      if (!(encoded_scale > 0.0F)) {
        for (std::size_t action = 0; action < action_count; ++action) {
          auto *const destination = buffers_.scaled_regret +
              canonical_action_major_index(canonical, 0U, action);
          std::fill_n(destination, combos.size(), std::uint16_t{0});
        }
      } else {
        const double inverse = 1.0 / static_cast<double>(encoded_scale);
        for (std::size_t action = 0; action < action_count; ++action) {
          auto *const destination = buffers_.scaled_regret +
              canonical_action_major_index(canonical, 0U, action);
          std::size_t local = 0U;
          if constexpr (std::is_same_v<Scalar, float>) {
            const __m256 inverse_vector = _mm256_set1_ps(static_cast<float>(inverse));
            const __m256 low = _mm256_set1_ps(-32767.0F);
            const __m256 high = _mm256_set1_ps(32767.0F);
            for (; local + 8U <= combos.size(); local += 8U) {
              const __m256 scaled = _mm256_min_ps(
                  high, _mm256_max_ps(
                            low, _mm256_mul_ps(
                                     _mm256_loadu_ps(scratch[action].data() + local),
                                     inverse_vector)));
              const __m256i encoded = _mm256_cvtps_epi32(scaled);
              const __m128i packed = _mm_packs_epi32(
                  _mm256_castsi256_si128(encoded),
                  _mm256_extracti128_si256(encoded, 1));
              _mm_storeu_si128(reinterpret_cast<__m128i *>(destination + local), packed);
            }
          }
          for (; local < combos.size(); ++local) {
            const auto encoded = static_cast<std::int16_t>(std::clamp(
                std::nearbyint(static_cast<double>(scratch[action][local]) * inverse),
                -32767.0, 32767.0));
            destination[local] = static_cast<std::uint16_t>(encoded);
          }
        }
      }
      if (!(average_scale > 0.0F)) {
        for (std::size_t action = 0; action < action_count; ++action) {
          auto *const destination = buffers_.scaled_strategy +
              canonical_action_major_index(canonical, 0U, action);
          std::fill_n(destination, combos.size(), std::uint16_t{0});
        }
      } else {
        const double inverse = 1.0 / static_cast<double>(average_scale);
        for (std::size_t action = 0; action < action_count; ++action) {
          auto *const destination = buffers_.scaled_strategy +
              canonical_action_major_index(canonical, 0U, action);
          std::size_t local = 0U;
          if constexpr (std::is_same_v<Scalar, float>) {
            const __m256 inverse_vector = _mm256_set1_ps(static_cast<float>(inverse));
            for (; local + 8U <= combos.size(); local += 8U) {
              const __m256 scaled = _mm256_min_ps(
                  _mm256_set1_ps(65535.0F),
                  _mm256_max_ps(
                      _mm256_setzero_ps(),
                      _mm256_mul_ps(
                          _mm256_loadu_ps(average_values[action].data() + local),
                          inverse_vector)));
              const __m256i encoded = _mm256_cvtps_epi32(scaled);
              const __m128i packed = _mm_packus_epi32(
                  _mm256_castsi256_si128(encoded),
                  _mm256_extracti128_si256(encoded, 1));
              _mm_storeu_si128(reinterpret_cast<__m128i *>(destination + local), packed);
            }
          }
          for (; local < combos.size(); ++local) {
            destination[local] = static_cast<std::uint16_t>(std::clamp(
                std::nearbyint(static_cast<double>(average_values[action][local]) * inverse),
                0.0, 65535.0));
          }
        }
      }
      return;
    }
    double maximum = 0.0;
    if constexpr (std::is_same_v<Scalar, float>) {
      const auto &slots = PlayerIndexed ? board.player_flop_slots[player]
                                        : board.player_active_slots[player];
      const __m256 old_scale_vector = _mm256_set1_ps(static_cast<float>(old_scale));
      const __m256 regret_weight =
          _mm256_set1_ps(static_cast<float>(regret_update_weight_));
      __m256 maximum_vector = _mm256_setzero_ps();
      for (std::size_t action = 0; action < action_count; ++action) {
        const auto *const source = buffers_.scaled_regret +
                                   canonical_action_major_index(canonical, 0U, action);
        float *const destination = scratch[action].data();
        std::size_t local = 0U;
        for (; local + 8U <= combos.size(); local += 8U) {
          const __m256i indices = _mm256_cvtepu16_epi32(_mm_loadu_si128(
              reinterpret_cast<const __m128i *>(slots.data() + local)));
          const __m256 old_values = _mm256_mul_ps(
              _mm256_cvtepi32_ps(_mm256_cvtepu16_epi32(_mm_loadu_si128(
                  reinterpret_cast<const __m128i *>(source + local)))),
              old_scale_vector);
          const __m256 updated = _mm256_max_ps(
              _mm256_setzero_ps(),
              _mm256_add_ps(
                  old_values,
                  _mm256_mul_ps(
                      regret_weight,
                      _mm256_sub_ps(
                          _mm256_i32gather_ps(action_values[action].data(), indices, 4),
                          _mm256_i32gather_ps(values.data(), indices, 4)))));
          _mm256_storeu_ps(destination + local, updated);
          maximum_vector = _mm256_max_ps(maximum_vector, updated);
        }
        for (; local < combos.size(); ++local) {
          const auto slot = static_cast<std::size_t>(slots[local]);
          const double updated = std::max(
              0.0, static_cast<double>(source[local]) * old_scale +
                       regret_update_weight_ *
                           (static_cast<double>(action_values[action][slot]) -
                            static_cast<double>(values[slot])));
          destination[local] = static_cast<float>(updated);
          maximum = std::max(maximum, updated);
        }
      }
      alignas(32) float maxima[8];
      _mm256_store_ps(maxima, maximum_vector);
      for (const float value : maxima) {
        maximum = std::max(maximum, static_cast<double>(value));
      }
    } else {
      for (std::size_t action = 0; action < action_count; ++action) {
        for (std::size_t local = 0; local < combos.size(); ++local) {
          const auto slot = value_slot(combos[local], player);
          const auto index = canonical_action_major_index(canonical, local, action);
          const double updated = std::max(
              0.0, static_cast<double>(buffers_.scaled_regret[index]) * old_scale +
                       regret_update_weight_ *
                           (static_cast<double>(action_values[action][slot]) -
                            static_cast<double>(values[slot])));
          scratch[action][local] = static_cast<Scalar>(updated);
          maximum = std::max(maximum, updated);
        }
      }
    }
    const float encoded_scale =
        maximum > 0.0 ? static_cast<float>(maximum / 65535.0) : 0.0F;
    buffers_.regret_node_scale[scale_index] = encoded_scale;
    if (!(encoded_scale > 0.0F)) {
      for (std::size_t action = 0; action < action_count; ++action) {
        auto *const destination =
            buffers_.scaled_regret +
            canonical_action_major_index(canonical, 0U, action);
        std::fill_n(destination, combos.size(), std::uint16_t{0});
      }
      return;
    }
    const double inverse = 1.0 / static_cast<double>(encoded_scale);
    for (std::size_t action = 0; action < action_count; ++action) {
      auto *const destination =
          buffers_.scaled_regret +
          canonical_action_major_index(canonical, 0U, action);
      std::size_t local = 0U;
      if constexpr (std::is_same_v<Scalar, float>) {
        const __m256 inverse_vector = _mm256_set1_ps(static_cast<float>(inverse));
        for (; local + 8U <= combos.size(); local += 8U) {
          const __m256 scaled = _mm256_min_ps(
              _mm256_set1_ps(65535.0F),
              _mm256_max_ps(_mm256_setzero_ps(),
                            _mm256_mul_ps(_mm256_loadu_ps(
                                scratch[action].data() + local), inverse_vector)));
          const __m256i encoded = _mm256_cvtps_epi32(scaled);
          const __m128i packed = _mm_packus_epi32(
              _mm256_castsi256_si128(encoded), _mm256_extracti128_si256(encoded, 1));
          _mm_storeu_si128(reinterpret_cast<__m128i *>(destination + local), packed);
        }
      }
      for (; local < combos.size(); ++local) {
        destination[local] = static_cast<std::uint16_t>(std::clamp(
            std::nearbyint(static_cast<double>(scratch[action][local]) * inverse),
            0.0, 65535.0));
      }
    }
  }

  [[nodiscard]] bool materialize_actor_reach(
      const BoardData &board, const std::uint8_t player,
      const ComboVector &parent_reach, const ComboVector &strategy,
      ComboVector &actor_reach, const std::size_t count,
      const bool profile_writes,
      const ComboVector *const board_local_parent = nullptr) {
    bool any_nonzero = false;
    std::size_t local = 0U;
    if constexpr (PlayerIndexed) {
      const auto *const slots = board.player_flop_slots[player].data();
      alignas(32) double child_values[4];
      for (; local + 4U <= count; local += 4U) {
        const __m128i packed_slots =
            _mm_loadl_epi64(reinterpret_cast<const __m128i *>(slots + local));
        const __m128i indices = _mm_cvtepu16_epi32(packed_slots);
        const __m256d parents =
            board_local_parent != nullptr
                ? load_four_as_double(board_local_parent->data() + local)
                : gather_four_as_double(parent_reach.data(), indices);
        const __m256d children =
            _mm256_mul_pd(parents, load_four_as_double(strategy.data() + local));
        _mm256_store_pd(child_values, children);
        for (std::size_t lane = 0U; lane < 4U; ++lane) {
          actor_reach[static_cast<std::size_t>(slots[local + lane])] =
              static_cast<Scalar>(child_values[lane]);
          any_nonzero = any_nonzero || child_values[lane] != 0.0;
        }
      }
    }
    for (; local < count; ++local) {
      const auto strategy_slot = board_player_slot(board, player, local);
      const auto reach_slot = board_reach_slot(board, player, local);
      const double parent = board_local_parent != nullptr
                                ? (*board_local_parent)[local]
                                : parent_reach[reach_slot];
      const double child = parent * strategy[strategy_slot];
      actor_reach[reach_slot] = static_cast<Scalar>(child);
      any_nonzero = any_nonzero || child != 0.0;
    }
    if (profile_writes && hotpath_profiling_enabled()) {
      prof_actor_writes_ += count;
    }
    return any_nonzero;
  }

  [[nodiscard]] std::array<bool, 2> materialize_actor_reach_pair(
      const BoardData &board, const std::uint8_t player,
      const ComboVector &parent_reach, const ComboVector &first_strategy,
      const ComboVector &second_strategy, ComboVector &first_reach,
      ComboVector &second_reach, const std::size_t count,
      const bool profile_writes,
      const ComboVector *const board_local_parent = nullptr) {
    std::array<bool, 2> any_nonzero{false, false};
    std::size_t local = 0U;
    if constexpr (PlayerIndexed) {
      const auto *const slots = board.player_flop_slots[player].data();
      alignas(32) double first_values[4];
      alignas(32) double second_values[4];
      for (; local + 4U <= count; local += 4U) {
        const __m128i packed_slots =
            _mm_loadl_epi64(reinterpret_cast<const __m128i *>(slots + local));
        const __m128i indices = _mm_cvtepu16_epi32(packed_slots);
        const __m256d parents =
            board_local_parent != nullptr
                ? load_four_as_double(board_local_parent->data() + local)
                : gather_four_as_double(parent_reach.data(), indices);
        const __m256d first_children = _mm256_mul_pd(
            parents, load_four_as_double(first_strategy.data() + local));
        const __m256d second_children = _mm256_mul_pd(
            parents, load_four_as_double(second_strategy.data() + local));
        _mm256_store_pd(first_values, first_children);
        _mm256_store_pd(second_values, second_children);
        for (std::size_t lane = 0U; lane < 4U; ++lane) {
          const auto slot = static_cast<std::size_t>(slots[local + lane]);
          first_reach[slot] = static_cast<Scalar>(first_values[lane]);
          second_reach[slot] = static_cast<Scalar>(second_values[lane]);
          any_nonzero[0] = any_nonzero[0] || first_values[lane] != 0.0;
          any_nonzero[1] = any_nonzero[1] || second_values[lane] != 0.0;
        }
      }
    }
    for (; local < count; ++local) {
      const auto strategy_slot = board_player_slot(board, player, local);
      const auto reach_slot = board_reach_slot(board, player, local);
      const double parent = board_local_parent != nullptr
                                ? (*board_local_parent)[local]
                                : parent_reach[reach_slot];
      const double first = parent * first_strategy[strategy_slot];
      const double second = parent * second_strategy[strategy_slot];
      first_reach[reach_slot] = static_cast<Scalar>(first);
      second_reach[reach_slot] = static_cast<Scalar>(second);
      any_nonzero[0] = any_nonzero[0] || first != 0.0;
      any_nonzero[1] = any_nonzero[1] || second != 0.0;
    }
    if (profile_writes && hotpath_profiling_enabled()) {
      prof_actor_writes_ += count;
    }
    return any_nonzero;
  }

  void add_strategy(const std::size_t index, const double value) const {
    // Linear averaging is disabled through averaging_delay.  Avoid touching
    // the (very large) strategy buffer for those zero-weight iterations: a
    // read/convert/write of +0 is semantically inert but streams hundreds of
    // megabytes through the cache on every player pass in TH7D6S.
    if (value == 0.0) {
      return;
    }
    if (buffers_.strategy_float32 != nullptr) {
      buffers_.strategy_float32[index] =
          static_cast<float>(static_cast<double>(buffers_.strategy_float32[index]) + value);
      return;
    }
    if (buffers_.strategy_float16 != nullptr) {
      buffers_.strategy_float16[index] = encode_float16(
          static_cast<double>(decode_float16(buffers_.strategy_float16[index])) + value);
      return;
    }
    buffers_.add_strategy(index, value);
  }

  void add_strategy_pair(const std::size_t index, const double first,
                         const double second) const {
    if (first == 0.0 && second == 0.0) {
      return;
    }
    if (buffers_.strategy_float32 != nullptr) {
      buffers_.strategy_float32[index] = static_cast<float>(
          static_cast<double>(buffers_.strategy_float32[index]) + first);
      buffers_.strategy_float32[index + 1U] = static_cast<float>(
          static_cast<double>(buffers_.strategy_float32[index + 1U]) + second);
      return;
    }
    if (buffers_.compact_state != nullptr) {
      auto *const first_bytes = buffers_.compact_state + index * 3U;
      auto *const second_bytes = first_bytes + 3U;
      const auto first_word = compact_word(first_bytes);
      const auto second_word = compact_word(second_bytes);
      const auto first_strategy = encode_strategy11(
          decode_strategy11(static_cast<std::uint16_t>(first_word >> 13U)) + first);
      const auto second_strategy = encode_strategy11(
          decode_strategy11(static_cast<std::uint16_t>(second_word >> 13U)) + second);
      store_compact_word(first_bytes, (first_word & 0x001fffU) |
                                          (static_cast<std::uint32_t>(first_strategy) << 13U));
      store_compact_word(second_bytes, (second_word & 0x001fffU) |
                                           (static_cast<std::uint32_t>(second_strategy) << 13U));
      return;
    }
    if (buffers_.strategy_float16 != nullptr) {
      auto *const packed = buffers_.strategy_float16 + index;
      const auto updated_first =
          static_cast<double>(decode_float16(packed[0])) + first;
      const auto updated_second =
          static_cast<double>(decode_float16(packed[1])) + second;
      const std::uint32_t encoded =
          static_cast<std::uint32_t>(encode_float16(updated_first)) |
          (static_cast<std::uint32_t>(encode_float16(updated_second)) << 16U);
      std::memcpy(packed, &encoded, sizeof(encoded));
      return;
    }
    buffers_.add_strategy(index, first);
    buffers_.add_strategy(index + 1U, second);
  }

  struct DecisionScratch {
    std::array<ComboVector, maximum_action_count> action_values{};
    std::array<ComboVector, maximum_action_count> opponent_action_values{};
    // Action-major strategy scratch: strategies[action][slot] keeps the slots
    // contiguous for a fixed action, so the value loop over the updating
    // player's combos is three contiguous streams (strategies, action_values,
    // values) and auto-vectorizes under /arch:AVX2. Slots are combo ids on the
    // non-per-player path, flop-range slots on the per-player path.
    std::array<ComboVector, maximum_action_count> strategies{};
    // Per-action actor reach scratch (plan: zero opponent reach copies): only
    // the actor's flop-range prefix is written per action; the opponent side
    // is shared from the parent, so no prefix copy is materialized per action.
    // Three buffers cover the largest root overlap currently enabled; the
    // sequential path reuses the first two for ordinary and paired terminals.
    std::array<ComboVector, 3> reach_actor{};
    ComboVector actor_parent_reach_local{};
    std::vector<double> local_regret_delta;
  };

  class DecisionScratchLease {
  public:
    explicit DecisionScratchLease(DenseTraversal &owner)
        : owner_(owner), scratch_(owner_.acquire_decision_scratch()) {}
    DecisionScratchLease(const DecisionScratchLease &) = delete;
    DecisionScratchLease &operator=(const DecisionScratchLease &) = delete;
    ~DecisionScratchLease() { --owner_.decision_scratch_depth_; }

    [[nodiscard]] DecisionScratch &get() const noexcept { return scratch_; }

  private:
    DenseTraversal &owner_;
    DecisionScratch &scratch_;
  };

  [[nodiscard]] DecisionScratch &acquire_decision_scratch() {
    if (decision_scratch_depth_ == decision_scratch_.size()) {
      decision_scratch_.push_back(std::make_unique<DecisionScratch>());
    }
    return *decision_scratch_[decision_scratch_depth_++];
  }


  void load_canonical_current_strategies(
      const CanonicalPublicNode &canonical, const BoardData &board,
      const std::uint8_t value_player,
      std::array<ComboVector, maximum_action_count> &strategies,
      const bool local_indexed = false,
      const bool average_strategy = false) {
    const auto &decision = canonical.decision;
    const auto action_count = static_cast<std::size_t>(decision.action_count);
    const bool locked_root = is_locked_root(canonical);
    if (!locked_root && action_major_compact_state()) {
      const auto &combos = board.player_combos[decision.player];
      for (std::size_t local = 0; local < combos.size(); ++local) {
        const auto slot = local_indexed ? local : value_slot(combos[local], value_player);
        double sum = 0.0;
        for (std::size_t action = 0; action < action_count; ++action) {
          const auto *const bytes = buffers_.compact_state +
              canonical_action_major_index(canonical, local, action) * 3U;
          const double regret = decode_regret13(static_cast<std::uint16_t>(
              compact_word(bytes) & 0x1fffU));
          strategies[action][slot] = static_cast<Scalar>(regret);
          sum += regret;
        }
        if (sum <= 0.0) {
          const auto uniform = static_cast<Scalar>(
              1.0 / static_cast<double>(action_count));
          for (std::size_t action = 0; action < action_count; ++action) {
            strategies[action][slot] = uniform;
          }
        } else {
          const auto inverse = static_cast<Scalar>(1.0 / sum);
          for (std::size_t action = 0; action < action_count; ++action) {
            strategies[action][slot] *= inverse;
          }
        }
      }
      return;
    }
    if (!locked_root && scaled_action_major_state()) {
      const auto &combos = board.player_combos[decision.player];
      const auto *const state_source = average_strategy
                                           ? buffers_.scaled_strategy
                                           : buffers_.scaled_regret;
      std::size_t local = 0U;
      if constexpr (std::is_same_v<Scalar, float>) {
        const __m256 zero = _mm256_setzero_ps();
        const __m256 one = _mm256_set1_ps(1.0F);
        const __m256 uniform =
            _mm256_set1_ps(1.0F / static_cast<float>(action_count));
        if (local_indexed && action_count == 2U) {
          const __m256 half = _mm256_set1_ps(0.5F);
          for (; local + 8U <= combos.size(); local += 8U) {
            const auto *const first_source = state_source +
                canonical_action_major_index(canonical, local, 0U);
            const auto *const second_source = state_source +
                canonical_action_major_index(canonical, local, 1U);
            const auto decode_positive = [&](const std::uint16_t *const source) {
              const __m256i raw = buffers_.signed_scaled_regret && !average_strategy
                  ? _mm256_cvtepi16_epi32(_mm_loadu_si128(
                        reinterpret_cast<const __m128i *>(source)))
                  : _mm256_cvtepu16_epi32(_mm_loadu_si128(
                        reinterpret_cast<const __m128i *>(source)));
              return buffers_.signed_scaled_regret && !average_strategy
                         ? _mm256_max_epi32(raw, _mm256_setzero_si256())
                         : raw;
            };
            const __m256i first_codes = decode_positive(first_source);
            const __m256i second_codes = decode_positive(second_source);
            const __m256 sum = _mm256_cvtepi32_ps(
                _mm256_add_epi32(first_codes, second_codes));
            const __m256 no_positive = _mm256_cmp_ps(sum, zero, _CMP_LE_OQ);
            const __m256 safe_sum = _mm256_blendv_ps(sum, one, no_positive);
            const __m256 estimate = _mm256_rcp_ps(safe_sum);
            const __m256 inverse = _mm256_mul_ps(
                estimate,
                _mm256_sub_ps(_mm256_set1_ps(2.0F),
                              _mm256_mul_ps(safe_sum, estimate)));
            _mm256_storeu_ps(
                strategies[0].data() + local,
                _mm256_blendv_ps(
                    _mm256_mul_ps(_mm256_cvtepi32_ps(first_codes), inverse),
                    half, no_positive));
            _mm256_storeu_ps(
                strategies[1].data() + local,
                _mm256_blendv_ps(
                    _mm256_mul_ps(_mm256_cvtepi32_ps(second_codes), inverse),
                    half, no_positive));
          }
        } else if (local_indexed && action_count == 3U) {
          const __m256 third = _mm256_set1_ps(1.0F / 3.0F);
          for (; local + 8U <= combos.size(); local += 8U) {
            std::array<__m256i, 3> codes{};
            __m256i integer_sum = _mm256_setzero_si256();
            for (std::size_t action = 0U; action < 3U; ++action) {
              const auto *const source = state_source +
                  canonical_action_major_index(canonical, local, action);
              const __m256i raw = buffers_.signed_scaled_regret && !average_strategy
                  ? _mm256_cvtepi16_epi32(_mm_loadu_si128(
                        reinterpret_cast<const __m128i *>(source)))
                  : _mm256_cvtepu16_epi32(_mm_loadu_si128(
                        reinterpret_cast<const __m128i *>(source)));
              codes[action] = buffers_.signed_scaled_regret && !average_strategy
                                  ? _mm256_max_epi32(raw, _mm256_setzero_si256())
                                  : raw;
              integer_sum = _mm256_add_epi32(integer_sum, codes[action]);
            }
            const __m256 sum = _mm256_cvtepi32_ps(integer_sum);
            const __m256 no_positive = _mm256_cmp_ps(sum, zero, _CMP_LE_OQ);
            const __m256 safe_sum = _mm256_blendv_ps(sum, one, no_positive);
            const __m256 estimate = _mm256_rcp_ps(safe_sum);
            const __m256 inverse = _mm256_mul_ps(
                estimate,
                _mm256_sub_ps(_mm256_set1_ps(2.0F),
                              _mm256_mul_ps(safe_sum, estimate)));
            for (std::size_t action = 0U; action < 3U; ++action) {
              _mm256_storeu_ps(
                  strategies[action].data() + local,
                  _mm256_blendv_ps(
                      _mm256_mul_ps(_mm256_cvtepi32_ps(codes[action]), inverse),
                      third, no_positive));
            }
          }
        }
        for (; local_indexed && local + 8U <= combos.size(); local += 8U) {
          std::array<__m256, maximum_action_count> regrets{};
          __m256i integer_sum = _mm256_setzero_si256();
          for (std::size_t action = 0U; action < action_count; ++action) {
            const auto *const source =
                state_source +
                canonical_action_major_index(canonical, local, action);
            const __m256i raw_codes = buffers_.signed_scaled_regret && !average_strategy
                ? _mm256_cvtepi16_epi32(
                      _mm_loadu_si128(reinterpret_cast<const __m128i *>(source)))
                : _mm256_cvtepu16_epi32(
                      _mm_loadu_si128(reinterpret_cast<const __m128i *>(source)));
            const __m256i codes = buffers_.signed_scaled_regret && !average_strategy
                                      ? _mm256_max_epi32(raw_codes,
                                                        _mm256_setzero_si256())
                                      : raw_codes;
            integer_sum = _mm256_add_epi32(integer_sum, codes);
            regrets[action] = _mm256_cvtepi32_ps(codes);
          }
          const __m256 sum = _mm256_cvtepi32_ps(integer_sum);
          const __m256 no_positive = _mm256_cmp_ps(sum, zero, _CMP_LE_OQ);
          // AVX2 has no packed integer divide and vdivps is a high-latency
          // bottleneck across hundreds of millions of infoset entries.  One
          // Newton-Raphson refinement of rcpps recovers essentially full
          // float32 reciprocal precision; zero-sum lanes use one as a safe
          // denominator and are replaced by the exact uniform strategy below.
          const __m256 safe_sum = _mm256_blendv_ps(sum, one, no_positive);
          const __m256 estimate = _mm256_rcp_ps(safe_sum);
          const __m256 inverse = _mm256_mul_ps(
              estimate,
              _mm256_sub_ps(_mm256_set1_ps(2.0F),
                            _mm256_mul_ps(safe_sum, estimate)));
          for (std::size_t action = 0U; action < action_count; ++action) {
            _mm256_storeu_ps(
                strategies[action].data() + local,
                _mm256_blendv_ps(_mm256_mul_ps(regrets[action], inverse),
                                 uniform, no_positive));
          }
        }
      }
      for (; local < combos.size(); ++local) {
        const auto slot = local_indexed ? local : value_slot(combos[local], value_player);
        std::uint32_t sum = 0U;
        for (std::size_t action = 0; action < action_count; ++action) {
          const auto raw_code = state_source[
              canonical_action_major_index(canonical, local, action)];
          const auto code = buffers_.signed_scaled_regret && !average_strategy
                                ? static_cast<std::uint16_t>(std::max<std::int32_t>(
                                      0, static_cast<std::int16_t>(raw_code)))
                                : raw_code;
          strategies[action][slot] = static_cast<Scalar>(code);
          sum += static_cast<std::uint32_t>(code);
        }
        if (sum == 0U) {
          const auto uniform = static_cast<Scalar>(
              1.0 / static_cast<double>(action_count));
          for (std::size_t action = 0; action < action_count; ++action) {
            strategies[action][slot] = uniform;
          }
        } else {
          const auto inverse = static_cast<Scalar>(1.0 / static_cast<double>(sum));
          for (std::size_t action = 0; action < action_count; ++action) {
            strategies[action][slot] *= inverse;
          }
        }
      }
      return;
    }
    if (!locked_root && aligned_compact_state()) {
      const auto &combos = board.player_combos[decision.player];
      const auto &slots = PlayerIndexed
                              ? board.player_flop_slots[decision.player]
                              : board.player_active_slots[decision.player];
      std::size_t local = 0U;
      if constexpr (std::is_same_v<Scalar, float>) {
        const __m256 zero = _mm256_setzero_ps();
        const __m256 one = _mm256_set1_ps(1.0F);
        const __m256 uniform =
            _mm256_set1_ps(1.0F / static_cast<float>(action_count));
        const auto *const state = buffers_.compact_regret16 +
                                  static_cast<std::size_t>(decision.action_base);
        // i32gather reads four bytes per lane although each code is uint16.
        // Keep one following code available; the final decision falls back to
        // the scalar tail instead of reading past the state allocation.
        for (; local + 8U <= combos.size() &&
               static_cast<std::size_t>(decision.action_base) +
                       (local + 7U) * action_count + action_count <
                   buffers_.count;
             local += 8U) {
          const __m256i offsets = _mm256_setr_epi32(
              static_cast<int>((local + 0U) * action_count),
              static_cast<int>((local + 1U) * action_count),
              static_cast<int>((local + 2U) * action_count),
              static_cast<int>((local + 3U) * action_count),
              static_cast<int>((local + 4U) * action_count),
              static_cast<int>((local + 5U) * action_count),
              static_cast<int>((local + 6U) * action_count),
              static_cast<int>((local + 7U) * action_count));
          std::array<__m256, maximum_action_count> regrets{};
          __m256 sum = zero;
          for (std::size_t action = 0U; action < action_count; ++action) {
            const __m256i codes = _mm256_and_si256(
                _mm256_i32gather_epi32(
                    reinterpret_cast<const int *>(state + action), offsets, 2),
                _mm256_set1_epi32(0x1fff));
            regrets[action] =
                _mm256_castsi256_ps(_mm256_slli_epi32(codes, 18));
            sum = _mm256_add_ps(sum, regrets[action]);
          }
          const __m256 no_positive =
              _mm256_cmp_ps(sum, zero, _CMP_LE_OQ);
          const __m256 inverse = _mm256_div_ps(one, sum);
          for (std::size_t action = 0U; action < action_count; ++action) {
            const __m256 strategy = _mm256_blendv_ps(
                _mm256_mul_ps(regrets[action], inverse), uniform, no_positive);
            if (local_indexed) {
              _mm256_storeu_ps(strategies[action].data() + local, strategy);
            } else {
              alignas(32) float lanes[8];
              _mm256_store_ps(lanes, strategy);
              for (std::size_t lane = 0U; lane < 8U; ++lane) {
                strategies[action][slots[local + lane]] = lanes[lane];
              }
            }
          }
        }
      }
      for (; local < combos.size(); ++local) {
        const auto output_slot =
            local_indexed ? local : static_cast<std::size_t>(slots[local]);
        const auto base = static_cast<std::size_t>(decision.action_base) +
                          local * action_count;
        double sum = 0.0;
        for (std::size_t action = 0U; action < action_count; ++action) {
          const double regret = decode_regret13(
              buffers_.compact_regret16[base + action]);
          strategies[action][output_slot] = static_cast<Scalar>(regret);
          sum += regret;
        }
        if (sum <= 0.0) {
          const auto uniform_value = static_cast<Scalar>(
              1.0 / static_cast<double>(action_count));
          for (std::size_t action = 0U; action < action_count; ++action) {
            strategies[action][output_slot] = uniform_value;
          }
        } else {
          const auto inverse = static_cast<Scalar>(1.0 / sum);
          for (std::size_t action = 0U; action < action_count; ++action) {
            strategies[action][output_slot] *= inverse;
          }
        }
      }
      return;
    }
    if (!locked_root && buffers_.compact_state != nullptr) {
      std::size_t local_index = 0U;
      {
        if (action_count == 2U) {
          const auto &slots = PlayerIndexed
                                  ? board.player_flop_slots[decision.player]
                                  : board.player_active_slots[decision.player];
          const __m128i expand_words =
              _mm_setr_epi8(0, 1, 2, -1, 3, 4, 5, -1,
                            6, 7, 8, -1, 9, 10, 11, -1);
          const __m128i regret_mask = _mm_set1_epi32(0x1fff);
          const __m256d half = _mm256_set1_pd(0.5);
          const __m256d zero = _mm256_setzero_pd();
          alignas(32) double first_values[4];
          alignas(32) double second_values[4];
          if constexpr (std::is_same_v<Scalar, float>) {
            const __m256 float_half = _mm256_set1_ps(0.5F);
            const __m256 float_zero = _mm256_setzero_ps();
            const __m256 float_one = _mm256_set1_ps(1.0F);
            for (; local_index + 8U <= slots.size(); local_index += 8U) {
              const auto *const packed =
                  buffers_.compact_state +
                  (static_cast<std::size_t>(decision.action_base) +
                   local_index * 2U) *
                      3U;
              const std::array<__m128, 4> groups{
                  _mm_castsi128_ps(_mm_slli_epi32(
                      _mm_and_si128(
                          _mm_shuffle_epi8(
                              _mm_loadu_si128(
                                  reinterpret_cast<const __m128i *>(packed)),
                              expand_words),
                          regret_mask),
                      18)),
                  _mm_castsi128_ps(_mm_slli_epi32(
                      _mm_and_si128(
                          _mm_shuffle_epi8(
                              _mm_loadu_si128(
                                  reinterpret_cast<const __m128i *>(packed + 12U)),
                              expand_words),
                          regret_mask),
                      18)),
                  _mm_castsi128_ps(_mm_slli_epi32(
                      _mm_and_si128(
                          _mm_shuffle_epi8(
                              _mm_loadu_si128(
                                  reinterpret_cast<const __m128i *>(packed + 24U)),
                              expand_words),
                          regret_mask),
                      18)),
                  _mm_castsi128_ps(_mm_slli_epi32(
                      _mm_and_si128(
                          _mm_shuffle_epi8(
                              _mm_loadu_si128(
                                  reinterpret_cast<const __m128i *>(packed + 36U)),
                              expand_words),
                          regret_mask),
                      18))};
              const __m128 first_low = _mm_shuffle_ps(
                  groups[0], groups[1], _MM_SHUFFLE(2, 0, 2, 0));
              const __m128 first_high = _mm_shuffle_ps(
                  groups[2], groups[3], _MM_SHUFFLE(2, 0, 2, 0));
              const __m128 second_low = _mm_shuffle_ps(
                  groups[0], groups[1], _MM_SHUFFLE(3, 1, 3, 1));
              const __m128 second_high = _mm_shuffle_ps(
                  groups[2], groups[3], _MM_SHUFFLE(3, 1, 3, 1));
              const __m256 first = _mm256_insertf128_ps(
                  _mm256_castps128_ps256(first_low), first_high, 1);
              const __m256 second = _mm256_insertf128_ps(
                  _mm256_castps128_ps256(second_low), second_high, 1);
              const __m256 sum = _mm256_add_ps(first, second);
              const __m256 use_uniform =
                  _mm256_cmp_ps(sum, float_zero, _CMP_LE_OQ);
              const __m256 inverse = _mm256_div_ps(float_one, sum);
              const __m256 normalized_first = _mm256_blendv_ps(
                  _mm256_mul_ps(first, inverse), float_half, use_uniform);
              const __m256 normalized_second = _mm256_blendv_ps(
                  _mm256_mul_ps(second, inverse), float_half, use_uniform);
              if (local_indexed) {
                _mm256_storeu_ps(strategies[0].data() + local_index,
                                 normalized_first);
                _mm256_storeu_ps(strategies[1].data() + local_index,
                                 normalized_second);
              } else {
                alignas(32) float first_lanes[8];
                alignas(32) float second_lanes[8];
                _mm256_store_ps(first_lanes, normalized_first);
                _mm256_store_ps(second_lanes, normalized_second);
                for (std::size_t lane = 0U; lane < 8U; ++lane) {
                  const auto slot =
                      static_cast<std::size_t>(slots[local_index + lane]);
                  strategies[0][slot] = first_lanes[lane];
                  strategies[1][slot] = second_lanes[lane];
                }
              }
            }
          }
          for (; local_index + 4U < slots.size(); local_index += 4U) {
            const auto *const packed =
                buffers_.compact_state +
                (static_cast<std::size_t>(decision.action_base) +
                 local_index * 2U) *
                    3U;
            const __m128i low_words = _mm_shuffle_epi8(
                _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed)),
                expand_words);
            const __m128i high_words = _mm_shuffle_epi8(
                _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed + 12U)),
                expand_words);
            const __m128 low_regrets = _mm_castsi128_ps(
                _mm_slli_epi32(_mm_and_si128(low_words, regret_mask), 18));
            const __m128 high_regrets = _mm_castsi128_ps(
                _mm_slli_epi32(_mm_and_si128(high_words, regret_mask), 18));
            const __m256d first = _mm256_cvtps_pd(
                _mm_shuffle_ps(low_regrets, high_regrets,
                               _MM_SHUFFLE(2, 0, 2, 0)));
            const __m256d second = _mm256_cvtps_pd(
                _mm_shuffle_ps(low_regrets, high_regrets,
                               _MM_SHUFFLE(3, 1, 3, 1)));
            const __m256d sum = _mm256_add_pd(first, second);
            const __m256d use_uniform =
                _mm256_cmp_pd(sum, zero, _CMP_LE_OQ);
            const __m256d inverse =
                _mm256_div_pd(_mm256_set1_pd(1.0), sum);
            _mm256_store_pd(first_values,
                            _mm256_blendv_pd(_mm256_mul_pd(first, inverse),
                                             half, use_uniform));
            _mm256_store_pd(second_values,
                            _mm256_blendv_pd(_mm256_mul_pd(second, inverse),
                                             half, use_uniform));
            if (local_indexed) {
              store_four_from_double(strategies[0].data() + local_index,
                               _mm256_load_pd(first_values));
              store_four_from_double(strategies[1].data() + local_index,
                               _mm256_load_pd(second_values));
            } else {
              for (std::size_t lane = 0U; lane < 4U; ++lane) {
                const auto slot = static_cast<std::size_t>(slots[local_index + lane]);
                strategies[0][slot] = static_cast<Scalar>(first_values[lane]);
                strategies[1][slot] = static_cast<Scalar>(second_values[lane]);
              }
            }
          }
        } else if (action_count == 3U) {
          const auto &slots = PlayerIndexed
                                  ? board.player_flop_slots[decision.player]
                                  : board.player_active_slots[decision.player];
          const __m128i expand_words =
              _mm_setr_epi8(0, 1, 2, -1, 3, 4, 5, -1,
                            6, 7, 8, -1, -1, -1, -1, -1);
          const __m128i regret_mask = _mm_set1_epi32(0x1fff);
          const __m256d third = _mm256_set1_pd(1.0 / 3.0);
          const __m256d zero = _mm256_setzero_pd();
          alignas(32) double normalized[3][4];
          if constexpr (std::is_same_v<Scalar, float>) {
            const __m256 uniform_float = _mm256_set1_ps(1.0F / 3.0F);
            const __m256 zero_float = _mm256_setzero_ps();
            const __m256 one_float = _mm256_set1_ps(1.0F);
            for (; local_index + 8U <= slots.size(); local_index += 8U) {
              const auto *const packed =
                  buffers_.compact_state +
                  (static_cast<std::size_t>(decision.action_base) +
                   local_index * 3U) *
                      3U;
              std::array<__m128, 8> rows{};
              for (std::size_t lane = 0U; lane < 8U; ++lane) {
                rows[lane] = _mm_castsi128_ps(_mm_slli_epi32(
                    _mm_and_si128(
                        _mm_shuffle_epi8(
                            _mm_loadu_si128(reinterpret_cast<const __m128i *>(
                                packed + lane * 9U)),
                            expand_words),
                        regret_mask),
                    18));
              }
              _MM_TRANSPOSE4_PS(rows[0], rows[1], rows[2], rows[3]);
              _MM_TRANSPOSE4_PS(rows[4], rows[5], rows[6], rows[7]);
              std::array<__m256, 3> regrets{};
              for (std::size_t action = 0U; action < 3U; ++action) {
                regrets[action] = _mm256_insertf128_ps(
                    _mm256_castps128_ps256(rows[action]), rows[action + 4U], 1);
              }
              const __m256 sum = _mm256_add_ps(
                  _mm256_add_ps(regrets[0], regrets[1]), regrets[2]);
              const __m256 use_uniform =
                  _mm256_cmp_ps(sum, zero_float, _CMP_LE_OQ);
              const __m256 inverse = _mm256_div_ps(one_float, sum);
              for (std::size_t action = 0U; action < 3U; ++action) {
                const __m256 strategy = _mm256_blendv_ps(
                    _mm256_mul_ps(regrets[action], inverse), uniform_float,
                    use_uniform);
                if (local_indexed) {
                  _mm256_storeu_ps(strategies[action].data() + local_index,
                                   strategy);
                } else {
                  alignas(32) float lanes[8];
                  _mm256_store_ps(lanes, strategy);
                  for (std::size_t lane = 0U; lane < 8U; ++lane) {
                    strategies[action][slots[local_index + lane]] = lanes[lane];
                  }
                }
              }
            }
          }
          for (; local_index + 4U < slots.size(); local_index += 4U) {
            const auto *const packed =
                buffers_.compact_state +
                (static_cast<std::size_t>(decision.action_base) +
                 local_index * 3U) *
                    3U;
            std::array<__m128, 4> regret_rows{
                _mm_castsi128_ps(_mm_slli_epi32(
                    _mm_and_si128(_mm_shuffle_epi8(
                                      _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed)),
                                      expand_words),
                                  regret_mask),
                    18)),
                _mm_castsi128_ps(_mm_slli_epi32(
                    _mm_and_si128(_mm_shuffle_epi8(
                                      _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed + 9U)),
                                      expand_words),
                                  regret_mask),
                    18)),
                _mm_castsi128_ps(_mm_slli_epi32(
                    _mm_and_si128(_mm_shuffle_epi8(
                                      _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed + 18U)),
                                      expand_words),
                                  regret_mask),
                    18)),
                _mm_castsi128_ps(_mm_slli_epi32(
                    _mm_and_si128(_mm_shuffle_epi8(
                                      _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed + 27U)),
                                      expand_words),
                                  regret_mask),
                    18))};
            _MM_TRANSPOSE4_PS(regret_rows[0], regret_rows[1],
                              regret_rows[2], regret_rows[3]);
            const std::array<__m256d, 3> regrets{
                _mm256_cvtps_pd(regret_rows[0]),
                _mm256_cvtps_pd(regret_rows[1]),
                _mm256_cvtps_pd(regret_rows[2])};
            const __m256d sum = _mm256_add_pd(
                _mm256_add_pd(regrets[0], regrets[1]), regrets[2]);
            const __m256d use_uniform =
                _mm256_cmp_pd(sum, zero, _CMP_LE_OQ);
            const __m256d inverse =
                _mm256_div_pd(_mm256_set1_pd(1.0), sum);
            for (std::size_t action = 0U; action < 3U; ++action) {
              _mm256_store_pd(
                  normalized[action],
                  _mm256_blendv_pd(_mm256_mul_pd(regrets[action], inverse),
                                   third, use_uniform));
            }
            if (local_indexed) {
              for (std::size_t action = 0U; action < 3U; ++action) {
                store_four_from_double(strategies[action].data() + local_index,
                                 _mm256_load_pd(normalized[action]));
              }
            } else {
              for (std::size_t lane = 0U; lane < 4U; ++lane) {
                const auto slot = static_cast<std::size_t>(slots[local_index + lane]);
                strategies[0][slot] = static_cast<Scalar>(normalized[0][lane]);
                strategies[1][slot] = static_cast<Scalar>(normalized[1][lane]);
                strategies[2][slot] = static_cast<Scalar>(normalized[2][lane]);
              }
            }
          }
        } else if (action_count == 4U) {
          const auto &slots = PlayerIndexed
                                  ? board.player_flop_slots[decision.player]
                                  : board.player_active_slots[decision.player];
          const __m128i expand_words =
              _mm_setr_epi8(0, 1, 2, -1, 3, 4, 5, -1,
                            6, 7, 8, -1, 9, 10, 11, -1);
          const __m128i regret_mask = _mm_set1_epi32(0x1fff);
          const __m256d uniform = _mm256_set1_pd(0.25);
          const __m256d zero = _mm256_setzero_pd();
          alignas(32) double normalized[4][4];
          if constexpr (std::is_same_v<Scalar, float>) {
            const __m256 uniform_float = _mm256_set1_ps(0.25F);
            const __m256 zero_float = _mm256_setzero_ps();
            const __m256 one_float = _mm256_set1_ps(1.0F);
            for (; local_index + 8U <= slots.size(); local_index += 8U) {
              const auto *const packed =
                  buffers_.compact_state +
                  (static_cast<std::size_t>(decision.action_base) +
                   local_index * 4U) *
                      3U;
              std::array<__m128, 8> rows{};
              for (std::size_t lane = 0U; lane < 8U; ++lane) {
                rows[lane] = _mm_castsi128_ps(_mm_slli_epi32(
                    _mm_and_si128(
                        _mm_shuffle_epi8(
                            _mm_loadu_si128(reinterpret_cast<const __m128i *>(
                                packed + lane * 12U)),
                            expand_words),
                        regret_mask),
                    18));
              }
              _MM_TRANSPOSE4_PS(rows[0], rows[1], rows[2], rows[3]);
              _MM_TRANSPOSE4_PS(rows[4], rows[5], rows[6], rows[7]);
              std::array<__m256, 4> regrets{};
              for (std::size_t action = 0U; action < 4U; ++action) {
                regrets[action] = _mm256_insertf128_ps(
                    _mm256_castps128_ps256(rows[action]), rows[action + 4U], 1);
              }
              const __m256 sum = _mm256_add_ps(
                  _mm256_add_ps(regrets[0], regrets[1]),
                  _mm256_add_ps(regrets[2], regrets[3]));
              const __m256 use_uniform =
                  _mm256_cmp_ps(sum, zero_float, _CMP_LE_OQ);
              const __m256 inverse = _mm256_div_ps(one_float, sum);
              for (std::size_t action = 0U; action < 4U; ++action) {
                const __m256 strategy = _mm256_blendv_ps(
                    _mm256_mul_ps(regrets[action], inverse), uniform_float,
                    use_uniform);
                if (local_indexed) {
                  _mm256_storeu_ps(strategies[action].data() + local_index,
                                   strategy);
                } else {
                  alignas(32) float lanes[8];
                  _mm256_store_ps(lanes, strategy);
                  for (std::size_t lane = 0U; lane < 8U; ++lane) {
                    strategies[action][slots[local_index + lane]] = lanes[lane];
                  }
                }
              }
            }
          }
          for (; local_index + 4U < slots.size(); local_index += 4U) {
            const auto *const block =
                buffers_.compact_state +
                (static_cast<std::size_t>(decision.action_base) +
                 local_index * 4U) * 3U;
            std::array<__m128, 4> regret_rows{
                _mm_castsi128_ps(_mm_slli_epi32(
                    _mm_and_si128(_mm_shuffle_epi8(
                                      _mm_loadu_si128(reinterpret_cast<const __m128i *>(block)),
                                      expand_words),
                                  regret_mask),
                    18)),
                _mm_castsi128_ps(_mm_slli_epi32(
                    _mm_and_si128(_mm_shuffle_epi8(
                                      _mm_loadu_si128(reinterpret_cast<const __m128i *>(block + 12U)),
                                      expand_words),
                                  regret_mask),
                    18)),
                _mm_castsi128_ps(_mm_slli_epi32(
                    _mm_and_si128(_mm_shuffle_epi8(
                                      _mm_loadu_si128(reinterpret_cast<const __m128i *>(block + 24U)),
                                      expand_words),
                                  regret_mask),
                    18)),
                _mm_castsi128_ps(_mm_slli_epi32(
                    _mm_and_si128(_mm_shuffle_epi8(
                                      _mm_loadu_si128(reinterpret_cast<const __m128i *>(block + 36U)),
                                      expand_words),
                                  regret_mask),
                    18))};
            _MM_TRANSPOSE4_PS(regret_rows[0], regret_rows[1],
                              regret_rows[2], regret_rows[3]);
            const std::array<__m256d, 4> regrets{
                _mm256_cvtps_pd(regret_rows[0]), _mm256_cvtps_pd(regret_rows[1]),
                _mm256_cvtps_pd(regret_rows[2]), _mm256_cvtps_pd(regret_rows[3])};
            const __m256d sum = _mm256_add_pd(
                _mm256_add_pd(regrets[0], regrets[1]),
                _mm256_add_pd(regrets[2], regrets[3]));
            const __m256d use_uniform = _mm256_cmp_pd(sum, zero, _CMP_LE_OQ);
            const __m256d inverse = _mm256_div_pd(_mm256_set1_pd(1.0), sum);
            for (std::size_t action = 0U; action < 4U; ++action) {
              _mm256_store_pd(
                  normalized[action],
                  _mm256_blendv_pd(_mm256_mul_pd(regrets[action], inverse),
                                   uniform, use_uniform));
            }
            if (local_indexed) {
              for (std::size_t action = 0U; action < 4U; ++action) {
                store_four_from_double(strategies[action].data() + local_index,
                                 _mm256_load_pd(normalized[action]));
              }
            } else {
              for (std::size_t lane = 0U; lane < 4U; ++lane) {
                const auto slot = static_cast<std::size_t>(slots[local_index + lane]);
                for (std::size_t action = 0U; action < 4U; ++action) {
                  strategies[action][slot] =
                      static_cast<Scalar>(normalized[action][lane]);
                }
              }
            }
          }
        }
      }
      for (; local_index < board.player_combos[decision.player].size(); ++local_index) {
        const auto combo = board.player_combos[decision.player][local_index];
        const auto local = board.player_local[decision.player][combo];
        const auto offset = canonical_action_base(canonical, local);
        const auto *const block =
            buffers_.compact_state + static_cast<std::size_t>(offset) * 3U;
        const auto slot = local_indexed ? local_index : value_slot(combo, value_player);
        double sum = 0.0;
        for (std::size_t action = 0U; action < action_count; ++action) {
          const double regret = decode_regret13(static_cast<std::uint16_t>(
              compact_word(block + action * 3U) & 0x1fffU));
          strategies[action][slot] = static_cast<Scalar>(regret);
          sum += regret;
        }
        if (sum <= 0.0) {
          const double uniform = 1.0 / static_cast<double>(action_count);
          for (std::size_t action = 0U; action < action_count; ++action) {
            strategies[action][slot] = static_cast<Scalar>(uniform);
          }
        } else {
          if (action_count == 2U) {
            const double inverse = 1.0 / sum;
            strategies[0][slot] = static_cast<Scalar>(strategies[0][slot] * inverse);
            strategies[1][slot] = static_cast<Scalar>(strategies[1][slot] * inverse);
          } else {
            for (std::size_t action = 0U; action < action_count; ++action) {
              strategies[action][slot] =
                  static_cast<Scalar>(strategies[action][slot] / sum);
            }
          }
        }
      }
      return;
    }
    if (!locked_root && action_count == 2U && buffers_.regret_float24 != nullptr) {
      for (const ComboId combo : board.player_combos[decision.player]) {
        const auto local = board.player_local[decision.player][combo];
        const auto offset = canonical_action_base(canonical, local);
        const auto *const packed =
            buffers_.regret_float24 + static_cast<std::size_t>(offset) * 3U;
        const double first = static_cast<double>(decode_float24(packed));
        const double second = static_cast<double>(decode_float24(packed + 3U));
        const double sum = first + second;
        const auto slot = local_indexed
                              ? static_cast<std::size_t>(board.player_local[decision.player][combo])
                              : value_slot(combo, value_player);
        if (sum <= 0.0) {
          strategies[0][slot] = static_cast<Scalar>(0.5);
          strategies[1][slot] = static_cast<Scalar>(0.5);
        } else {
          const double inverse = 1.0 / sum;
          strategies[0][slot] = static_cast<Scalar>(first * inverse);
          strategies[1][slot] = static_cast<Scalar>(second * inverse);
        }
      }
      return;
    }
    for (const ComboId combo : board.player_combos[decision.player]) {
      const auto local = board.player_local[decision.player][combo];
      const auto locked = locked_root ? locked_root_strategy(local) : std::nullopt;
      const auto strategy = locked ? *locked : current_strategy(canonical, local, false);
      const auto slot = local_indexed
                            ? static_cast<std::size_t>(board.player_local[decision.player][combo])
                            : value_slot(combo, value_player);
      for (std::size_t action = 0U; action < action_count; ++action) {
        strategies[action][slot] = static_cast<Scalar>(strategy[action]);
      }
    }
  }

  void merge_deferred_regrets_from(DenseTraversal &source, std::vector<double> &source_delta) {
    for (const std::size_t index : source.deferred_regret_touched_) {
      mark_deferred_regret_touched(index);
      (*deferred_regret_delta_)[index] += source_delta[index];
      source_delta[index] = 0.0;
      source.clear_deferred_regret_touched(index);
    }
    source.deferred_regret_touched_.clear();
  }

  void merge_parallel_deferred_regrets() {
    if (parallel_worker_ != nullptr) {
      merge_deferred_regrets_from(*parallel_worker_, parallel_regret_delta_);
    }
    for (std::size_t index = 0; index < parallel_workers_.size(); ++index) {
      merge_deferred_regrets_from(*parallel_workers_[index], parallel_worker_deltas_[index]);
    }
  }

  // Queues a task for the shared parallel queue. The FIFO queue is drained by
  // the worker threads; producers never block and pending tasks stay bounded
  // by the recursion depth of open parallel decisions, so a task is never
  // overwritten (which would break its future with "broken promise"). Called
  // only while the traversal is live (the queue shutdown flag is only set
  // during destruction, after all cfr calls have returned).
  void dispatch_parallel_task(std::packaged_task<TraversalResult(DenseTraversal &)> task) {
    ParallelTaskQueue *const queue = parallel_shared_.get();
    {
      std::unique_lock lock(queue->mutex);
      queue->tasks.push_back(std::move(task));
    }
    // notify_all: the shared queue is drained by several workers and the
    // join paths also wait on the same condition variable; a single
    // notify_one can wake the joining thread instead of a worker and starve
    // the workers indefinitely.
    queue->ready.notify_all();
  }

  void dispatch_parallel_tasks(
      std::vector<std::packaged_task<TraversalResult(DenseTraversal &)>> &tasks) {
    if (tasks.empty()) {
      return;
    }
    ParallelTaskQueue *const queue = parallel_shared_.get();
    {
      std::unique_lock lock(queue->mutex);
      for (auto &task : tasks) {
        queue->tasks.push_back(std::move(task));
      }
    }
    queue->ready.notify_all();
  }

  // Pull-based work-stealing: while a thread waits on futures dispatched to
  // the shared queue (river split below the turn), it executes other pending
  // tasks itself so the pool never stalls behind a single blocked dispatcher.
  // Returns whether a task was pulled (the caller blocks on the queue's
  // condition variable when false, instead of busy-spinning).
  bool try_pull_and_run() {
    ParallelTaskQueue *const queue = parallel_shared_.get();
    std::optional<std::packaged_task<TraversalResult(DenseTraversal &)>> task;
    {
      std::unique_lock lock(queue->mutex);
      if (!queue->tasks.empty()) {
        task = std::move(queue->tasks.front());
        queue->tasks.pop_front();
      }
    }
    if (!task) {
      return false;
    }
    const bool profile = hotpath_profiling_enabled();
    const auto task_started = profile ? std::chrono::steady_clock::now()
                                      : std::chrono::steady_clock::time_point{};
    queue->active_tasks.fetch_add(1U, std::memory_order_acq_rel);
    (*task)(*this);
    queue->active_tasks.fetch_sub(1U, std::memory_order_acq_rel);
    queue->completion_epoch.fetch_add(1U, std::memory_order_release);
    queue->completion_epoch.notify_all();
    if (profile) {
      ++prof_tasks_;
      prof_task_wall_seconds_ +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() - task_started).count();
    }
    return true;
  }

  void wait_for_parallel_future(std::future<TraversalResult> &future) {
    while (future.wait_for(std::chrono::seconds(0)) !=
           std::future_status::ready) {
      if (try_pull_and_run()) {
        continue;
      }
      ParallelTaskQueue *const queue = parallel_shared_.get();
      const auto epoch = queue->completion_epoch.load(std::memory_order_acquire);
      if (future.wait_for(std::chrono::seconds(0)) !=
          std::future_status::ready) {
        queue->completion_epoch.wait(epoch, std::memory_order_acquire);
      }
    }
  }

  [[nodiscard]] std::size_t idle_workers_if_queue_empty() const {
    if (parallel_shared_ == nullptr) {
      return 0U;
    }
    ParallelTaskQueue *const queue = parallel_shared_.get();
    std::scoped_lock lock(queue->mutex);
    if (!queue->tasks.empty()) {
      return 0U;
    }
    const auto active = queue->active_tasks.load(std::memory_order_acquire);
    const auto workers = parallel_workers_.empty()
                             ? parallel_pool_size_
                             : parallel_workers_.size();
    return active < workers ? workers - active : 0U;
  }

  Result<ComboVector, PostflopSolverError>
  cfr_canonical_parallel_entry(const std::uint32_t node_id, const std::uint8_t updating_player,
                               const ReachRef &reach,
                               const double strategy_weight) {
    if (parallel_worker_ != nullptr &&
        layout_.canonical_public_graph.nodes[node_id].kind == PublicNodeKind::Decision) {
      return cfr_canonical_parallel_decision(node_id, updating_player, reach, strategy_weight);
    }
    return cfr_canonical(node_id, updating_player, reach, strategy_weight);
  }

  Result<ComboVector, PostflopSolverError>
  cfr_canonical_parallel_decision(const std::uint32_t node_id, const std::uint8_t updating_player,
                                  const ReachRef &reach,
                                  const double strategy_weight) {
    const auto &canonical = layout_.canonical_public_graph.nodes[node_id];
    const auto action_count = canonical.edges.size();
    if (canonical.kind != PublicNodeKind::Decision || action_count < 2U ||
        action_count > maximum_parallel_action_count ||
        std::ranges::any_of(canonical.edges,
                            [](const auto &edge) { return edge.outcomes.size() != 1U; })) {
      return cfr_canonical(node_id, updating_player, reach, strategy_weight);
    }
    ++traversed_nodes_;
    count_work_node(PublicNodeKind::Decision);
    const auto &decision = canonical.decision;
    const auto &board = layout_.boards[decision.board_index];
    if (static_cast<std::size_t>(decision.action_count) != action_count) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    if (decision.player == updating_player) {
      const auto entries = static_cast<std::uint64_t>(
          board.player_combos[updating_player].size() * action_count);
      if (!is_locked_root(canonical)) {
        work_counters_.regret_update_entries += entries;
      }
      if (strategy_weight != 0.0) {
        work_counters_.strategy_update_entries += entries;
      }
    }
    DecisionScratchLease scratch_lease(*this);
    auto &action_values = scratch_lease.get().action_values;
    auto &strategies = scratch_lease.get().strategies;
    const bool locked_root = is_locked_root(canonical);
    load_canonical_current_strategies(canonical, board, decision.player, strategies);
    std::array<std::array<ComboVector, 2>, maximum_parallel_action_count> child_reaches{};
    std::array<bool, maximum_parallel_action_count> actor_reach_nonzero{};
    for (std::size_t action = 0; action < action_count; ++action) {
      child_reaches[action][0] = *reach[0];
      child_reaches[action][1] = *reach[1];
      for (const ComboId combo : board.player_combos[decision.player]) {
        const auto slot = value_slot(combo, decision.player);
        child_reaches[action][decision.player][slot] *= strategies[action][slot];
        actor_reach_nonzero[action] =
            actor_reach_nonzero[action] ||
            child_reaches[action][decision.player][slot] != 0.0;
      }
      child_reaches[action] =
          transform_reach(child_reaches[action],
                          canonical.edges[action].outcomes.front().physical_to_child_automorphism);
    }
    const auto &parallel_outcome = canonical.edges[0].outcomes.front();
    std::packaged_task<TraversalResult(DenseTraversal &)> first_task(
        [worker = parallel_worker_.get(), child = parallel_outcome.child, updating_player,
         child_reach = child_reaches[0], strategy_weight,
         skip_zero = decision.player != updating_player && !actor_reach_nonzero[0]](
            DenseTraversal &) {
          if (skip_zero) {
            return Result<ComboVector, PostflopSolverError>::success(ComboVector{});
          }
          return worker->cfr_canonical_parallel_entry(
              child, updating_player, {&child_reach[0], &child_reach[1]}, strategy_weight);
        });
    auto first = first_task.get_future();
    dispatch_parallel_task(std::move(first_task));
    std::optional<PostflopSolverError> serial_error;
    for (std::size_t action = 1U; action < action_count; ++action) {
      const auto &outcome = canonical.edges[action].outcomes.front();
      if (decision.player != updating_player && !actor_reach_nonzero[action]) {
        action_values[action] = zeroed_values(updating_player);
        continue;
      }
      auto child = cfr_canonical(outcome.child, updating_player,
                                 {&child_reaches[action][0], &child_reaches[action][1]},
                                 strategy_weight);
      if (!child) {
        serial_error = child.error();
        break;
      }
      action_values[action] =
        transform_values_to_parent(child.value(), outcome.physical_to_child_automorphism,
                                   updating_player);
    }
    auto first_values = first.get();
    if (!first_values || serial_error) {
      return Result<ComboVector, PostflopSolverError>::failure(first_values ? *serial_error
                                                                            : first_values.error());
    }
    action_values[0] = transform_values_to_parent(
        first_values.value(), parallel_outcome.physical_to_child_automorphism, updating_player);
    merge_parallel_deferred_regrets();

    auto values = zeroed_values(updating_player);
    if constexpr (PlayerIndexed) {
      // Per-player compact slots are contiguous 0..player_flop_count[player]-1,
      // so the value loop runs over the slot prefix directly and the common
      // two-action case is a fused SIMD accumulation (FMA chains, IEEE-identical).
      const std::size_t slot_count = layout_.player_flop_count[updating_player];
      if (decision.player == updating_player && action_count == 2U) {
        const Scalar *const s0 = strategies[0].data();
        const Scalar *const s1 = strategies[1].data();
        const Scalar *const a0 = action_values[0].data();
        const Scalar *const a1 = action_values[1].data();
        Scalar *const v = values.data();
        std::size_t i = 0;
        for (; i + 4U <= slot_count; i += 4U) {
          const __m256d acc = _mm256_add_pd(
              _mm256_mul_pd(load_four_as_double(s0 + i), load_four_as_double(a0 + i)),
              _mm256_mul_pd(load_four_as_double(s1 + i), load_four_as_double(a1 + i)));
          store_four_from_double(v + i, _mm256_add_pd(load_four_as_double(v + i), acc));
        }
        for (; i < slot_count; ++i) {
          v[i] = static_cast<Scalar>(v[i] + s0[i] * a0[i] + s1[i] * a1[i]);
        }
      } else if (decision.player == updating_player) {
        for (std::size_t action = 0; action < action_count; ++action) {
          for (std::size_t slot = 0; slot < slot_count; ++slot) {
            values[slot] += strategies[action][slot] * action_values[action][slot];
          }
        }
      } else {
        for (std::size_t action = 0; action < action_count; ++action) {
          for (std::size_t slot = 0; slot < slot_count; ++slot) {
            values[slot] += action_values[action][slot];
          }
        }
      }
      // Regret/strategy update: for the uniform per-player ranges guaranteed
      // in the PlayerIndexed path, player_combos[updating_player] equals
      // board.legal_combos, so iterating the actor's list matches the
      // combined scalar loop below exactly (same combos, same order).
      for (const ComboId combo : board.player_combos[updating_player]) {
        const auto slot = value_slot(combo, updating_player);
        if (decision.player == updating_player) {
          const auto local = board.player_local[decision.player][combo];
          const auto offset = canonical_action_base(canonical, local);
          if (action_count == 2U) {
            const auto index = static_cast<std::size_t>(offset);
            if (!locked_root) {
              for (std::size_t action = 0U; action < 2U; ++action) {
                add_deferred_regret(index + action,
                                    regret_update_weight_ *
                                        (action_values[action][slot] - values[slot]));
              }
            }
            const double weight = strategy_weight * (*reach[updating_player])[slot];
            add_strategy_pair(index, weight * strategies[0][slot],
                              weight * strategies[1][slot]);
            continue;
          }
          for (std::size_t action = 0; action < action_count; ++action) {
            const auto index = static_cast<std::size_t>(offset + action);
            if (!locked_root) {
              add_deferred_regret(index, regret_update_weight_ *
                                             (action_values[action][slot] - values[slot]));
            }
            add_strategy(index, strategy_weight * (*reach[updating_player])[slot] *
                                    strategies[action][slot]);
          }
        }
      }
    } else {
      for (const ComboId combo : board.player_combos[updating_player]) {
        const auto slot = value_slot(combo, updating_player);
        if (decision.player == updating_player) {
          for (std::size_t action = 0; action < action_count; ++action) {
            values[slot] += strategies[action][slot] * action_values[action][slot];
          }
        } else {
          for (std::size_t action = 0; action < action_count; ++action) {
            values[slot] += action_values[action][slot];
          }
        }
        if (decision.player == updating_player) {
          const auto local = board.player_local[decision.player][combo];
          const auto offset = canonical_action_base(canonical, local);
          if (action_count == 2U) {
            const auto index = static_cast<std::size_t>(offset);
            if (!locked_root) {
              for (std::size_t action = 0U; action < 2U; ++action) {
                add_deferred_regret(index + action,
                                    regret_update_weight_ *
                                        (action_values[action][slot] - values[slot]));
              }
            }
            const double weight = strategy_weight * (*reach[updating_player])[slot];
            add_strategy_pair(index, weight * strategies[0][slot],
                              weight * strategies[1][slot]);
            continue;
          }
          for (std::size_t action = 0; action < action_count; ++action) {
            const auto index = static_cast<std::size_t>(offset + action);
            if (!locked_root) {
              add_deferred_regret(index, regret_update_weight_ *
                                             (action_values[action][slot] - values[slot]));
            }
            add_strategy(index, strategy_weight * (*reach[updating_player])[slot] *
                                    strategies[action][slot]);
          }
        }
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(std::move(values));
  }

  // Physical-tree CFR traversal writing directly into `values_out` (no
  // per-node Result<ComboVector> return: the value vector is materialized in
  // the caller's buffer, eliminating the 5 KB move up the recursion).
  std::optional<PostflopSolverError>
  accumulate_average_only(const NodeId node_id, const std::uint8_t updating_player,
                          const ReachRef &reach, const double strategy_weight) {
    if (hotpath_profiling_enabled()) {
      ++prof_average_only_nodes_;
    }
    const auto &node = layout_.tree.nodes[static_cast<std::size_t>(node_id)];
    if (node.kind == PublicNodeKind::TerminalFold ||
        node.kind == PublicNodeKind::TerminalShowdown) {
      return std::nullopt;
    }
    if (node.kind == PublicNodeKind::Chance) {
      const auto &board =
          layout_.boards[layout_.node_board[static_cast<std::size_t>(node.id)]];
      const std::size_t worker_count =
          parallel_workers_.empty() ? parallel_pool_size_ : parallel_workers_.size();
      // An average-only river subtree writes strategy sums owned exclusively
      // by that river card. Fan those disjoint regions across the existing
      // traversal pool; the parent reach is read-only and remains alive until
      // every future is joined.
      if (std::popcount(board.mask) == 4U && worker_count > 0U &&
          node.edges.size() > 1U) {
        const std::size_t chunk_count =
            std::min(node.edges.size(), worker_count + 1U);
        std::vector<std::packaged_task<TraversalResult(DenseTraversal &)>> tasks;
        std::vector<std::future<TraversalResult>> futures;
        tasks.reserve(chunk_count - 1U);
        futures.reserve(chunk_count - 1U);
        for (std::size_t chunk = 1U; chunk < chunk_count; ++chunk) {
          const std::size_t begin = chunk * node.edges.size() / chunk_count;
          const std::size_t end = (chunk + 1U) * node.edges.size() / chunk_count;
          std::packaged_task<TraversalResult(DenseTraversal &)> task(
              [edges = &node.edges, begin, end, updating_player,
               reach_0 = reach[0], reach_1 = reach[1], strategy_weight](DenseTraversal &self) {
                for (std::size_t index = begin; index < end; ++index) {
                  if (const auto error = self.accumulate_average_only(
                          (*edges)[index].child, updating_player, {reach_0, reach_1},
                          strategy_weight)) {
                    return Result<ComboVector, PostflopSolverError>::failure(*error);
                  }
                }
                return Result<ComboVector, PostflopSolverError>::success(ComboVector{});
              });
          futures.push_back(task.get_future());
          tasks.push_back(std::move(task));
        }
        dispatch_parallel_tasks(tasks);
        std::optional<PostflopSolverError> main_error;
        const std::size_t main_end = node.edges.size() / chunk_count;
        for (std::size_t index = 0U; index < main_end && !main_error; ++index) {
          main_error = accumulate_average_only(
              node.edges[index].child, updating_player, reach, strategy_weight);
        }
        std::optional<PostflopSolverError> worker_error;
        for (auto &future : futures) {
          wait_for_parallel_future(future);
          auto result = future.get();
          if (!result && !worker_error) {
            worker_error = result.error();
          }
        }
        if (main_error) {
          return *main_error;
        }
        return worker_error;
      }
      for (const auto &edge : node.edges) {
        if (const auto error = accumulate_average_only(edge.child, updating_player, reach,
                                                       strategy_weight)) {
          return error;
        }
      }
      return std::nullopt;
    }
    if (node.kind != PublicNodeKind::Decision) {
      return PostflopSolverError::InvalidConfiguration;
    }
    const auto &decision = layout_.decisions[static_cast<std::size_t>(node.id)];
    if (hotpath_profiling_enabled()) {
      ++prof_average_only_decisions_;
    }
    if (decision.player != updating_player) {
      for (std::size_t action = 0; action < node.edges.size(); ++action) {
        if ((decision.terminal_child_mask & static_cast<std::uint8_t>(1U << action)) != 0U) {
          continue;
        }
        if (const auto error =
                accumulate_average_only(node.edges[action].child, updating_player, reach,
                                        strategy_weight)) {
          return error;
        }
      }
      return std::nullopt;
    }
    const auto &board = layout_.boards[decision.board_index];
    const auto action_count = static_cast<std::size_t>(decision.action_count);
    DecisionScratchLease scratch_lease(*this);
    auto &strategies = scratch_lease.get().strategies;
    auto &actor_reach = scratch_lease.get().reach_actor[0];
    const auto &actor_combos = board.player_combos[updating_player];
    if (hotpath_profiling_enabled()) {
      prof_average_only_entries_ += actor_combos.size() * action_count;
    }
    const bool locked_node = is_locked_root(node);
    if (!locked_node && layout_.uses_direct_action_bases &&
        buffers_.compact_state != nullptr) {
      const auto *const state =
          buffers_.compact_state +
          static_cast<std::size_t>(decision.action_base) * 3U;
      std::size_t local = 0U;
#if 0
      if constexpr (PlayerIndexed && std::is_same_v<Accumulator, float>) {
        const auto &updating_combos = board.terminal_combos[updating_player];
        const __m256 win = _mm256_set1_ps(static_cast<float>(win_payoff));
        const __m256 tie = _mm256_set1_ps(static_cast<float>(tie_payoff));
        const __m256 loss = _mm256_set1_ps(static_cast<float>(loss_payoff));
        const __m256 inverse_normalization = _mm256_set1_ps(
            static_cast<float>(1.0 / layout_.initial_normalization));
        const __m256 total = _mm256_set1_ps(prefix[rank_count]);
        const __m256 zero = _mm256_setzero_ps();
        std::size_t local = 0U;
        alignas(32) float own_reach_lanes[8];
        alignas(32) float result_lanes[8];
        for (; local + 8U <= updating_combos.size(); local += 8U) {
          const auto load_indices = [local](const std::vector<std::uint16_t> &source) {
            return _mm256_cvtepu16_epi32(_mm_loadu_si128(
                reinterpret_cast<const __m128i *>(source.data() + local)));
            };
            const __m256i opponent_indices =
                load_indices(BoardLocal ? updating_combos.opponent_local
                                        : updating_combos.opponent_slot);
          const __m256i invalid_slots = _mm256_cmpeq_epi32(
              opponent_indices,
              _mm256_set1_epi32(static_cast<int>(TerminalComboData::invalid_slot)));
          const __m256i safe_opponent_indices =
              _mm256_andnot_si256(invalid_slots, opponent_indices);
          const __m256 own_reaches = _mm256_blendv_ps(
              _mm256_i32gather_ps(opponent_reach.data(), safe_opponent_indices, 4),
              zero, _mm256_castsi256_ps(invalid_slots));
          const __m256i first_by_rank =
              load_indices(updating_combos.first_by_rank);
          const __m256i second_by_rank =
              load_indices(updating_combos.second_by_rank);
          const __m256 invalid_lower = _mm256_add_ps(
              _mm256_i32gather_ps(card_prefix.data(), first_by_rank, 4),
              _mm256_i32gather_ps(card_prefix.data(), second_by_rank, 4));
          const __m256 invalid_tie = _mm256_sub_ps(
              _mm256_add_ps(
                  _mm256_i32gather_ps(by_card.data(), first_by_rank, 4),
                  _mm256_i32gather_ps(by_card.data(), second_by_rank, 4)),
              own_reaches);
          const __m256i first_all = load_indices(updating_combos.first_all);
          const __m256i second_all = load_indices(updating_combos.second_all);
          const __m256 invalid_all = _mm256_sub_ps(
              _mm256_add_ps(
                  _mm256_i32gather_ps(card_prefix.data(), first_all, 4),
                  _mm256_i32gather_ps(card_prefix.data(), second_all, 4)),
              own_reaches);
          const __m256i ranks = load_indices(updating_combos.rank);
          const __m256 lower = _mm256_sub_ps(
              _mm256_i32gather_ps(prefix.data(), ranks, 4), invalid_lower);
          const __m256 equal = _mm256_sub_ps(
              _mm256_i32gather_ps(totals.data(), ranks, 4), invalid_tie);
          const __m256 invalid_higher = _mm256_sub_ps(
              _mm256_sub_ps(invalid_all, invalid_lower), invalid_tie);
          const __m256 higher = _mm256_sub_ps(
              _mm256_sub_ps(
                  total,
                  _mm256_i32gather_ps(
                      prefix.data(),
                      _mm256_add_epi32(ranks, _mm256_set1_epi32(1)), 4)),
              invalid_higher);
          __m256 numerator = _mm256_mul_ps(lower, win);
          if (tie_payoff != 0.0) {
            numerator = _mm256_add_ps(numerator, _mm256_mul_ps(equal, tie));
          }
          numerator = _mm256_add_ps(numerator, _mm256_mul_ps(higher, loss));
          _mm256_store_ps(result_lanes,
                          _mm256_mul_ps(numerator, inverse_normalization));
          _mm256_store_ps(own_reach_lanes, own_reaches);
          for (std::size_t lane = 0U; lane < 8U; ++lane) {
            const auto output_slot = updating_combos.own_slot[local + lane];
            values_out[output_slot] = result_lanes[lane];
            if (paired_fold_values_out != nullptr) {
              const auto opponent_slot =
                  updating_combos.opponent_slot[local + lane];
              const double fold_own_reach =
                  paired_fold_opponent_reach != nullptr &&
                          opponent_slot != TerminalComboData::invalid_slot
                      ? (*paired_fold_opponent_reach)[opponent_slot]
                      : static_cast<double>(own_reach_lanes[lane]);
              const double compatible =
                  paired_fold_total -
                  paired_fold_by_card[updating_combos.first_card[local + lane]] -
                  paired_fold_by_card[updating_combos.second_card[local + lane]] +
                  fold_own_reach;
              (*paired_fold_values_out)[output_slot] = static_cast<Scalar>(
                  compatible * paired_fold_payoff / layout_.initial_normalization);
            }
          }
        }
        for (; local < updating_combos.size(); ++local) {
          const auto opponent_slot = updating_combos.opponent_slot[local];
          const float own_reach = opponent_slot != TerminalComboData::invalid_slot
                                      ? opponent_reach[opponent_slot]
                                      : 0.0F;
          const float invalid_lower =
              card_prefix[updating_combos.first_by_rank[local]] +
              card_prefix[updating_combos.second_by_rank[local]];
          const float invalid_tie =
              by_card[updating_combos.first_by_rank[local]] +
              by_card[updating_combos.second_by_rank[local]] - own_reach;
          const float invalid_all =
              card_prefix[updating_combos.first_all[local]] +
              card_prefix[updating_combos.second_all[local]] - own_reach;
          const auto rank = updating_combos.rank[local];
          const float lower = prefix[rank] - invalid_lower;
          const float equal = totals[rank] - invalid_tie;
          const float higher =
              (prefix[rank_count] - prefix[rank + 1U]) -
              (invalid_all - invalid_lower - invalid_tie);
          const auto output_slot = updating_combos.own_slot[local];
          values_out[output_slot] =
              (lower * static_cast<float>(win_payoff) +
               equal * static_cast<float>(tie_payoff) +
               higher * static_cast<float>(loss_payoff)) *
              static_cast<float>(1.0 / layout_.initial_normalization);
          if (paired_fold_values_out != nullptr) {
            const double fold_own_reach =
                paired_fold_opponent_reach != nullptr &&
                        opponent_slot != TerminalComboData::invalid_slot
                    ? (*paired_fold_opponent_reach)[opponent_slot]
                    : static_cast<double>(own_reach);
            const double compatible =
                paired_fold_total -
                paired_fold_by_card[updating_combos.first_card[local]] -
                paired_fold_by_card[updating_combos.second_card[local]] +
                fold_own_reach;
            (*paired_fold_values_out)[output_slot] = static_cast<Scalar>(
                compatible * paired_fold_payoff / layout_.initial_normalization);
          }
        }
      }
#else
      if constexpr (PlayerIndexed) {
#endif
        if (action_count == 2U) {
          const __m128i expand_words =
              _mm_setr_epi8(0, 1, 2, -1, 3, 4, 5, -1,
                            6, 7, 8, -1, 9, 10, 11, -1);
          const __m128i regret_mask = _mm_set1_epi32(0x1fff);
          const __m256d zero = _mm256_setzero_pd();
          const __m256d half = _mm256_set1_pd(0.5);
          const __m256d one = _mm256_set1_pd(1.0);
          for (; local + 4U < actor_combos.size(); local += 4U) {
            const auto *const packed = state + local * 6U;
            const __m128i low_bytes = _mm_loadu_si128(
                reinterpret_cast<const __m128i *>(packed));
            const __m128i high_bytes = _mm_loadu_si128(
                reinterpret_cast<const __m128i *>(packed + 12U));
            const __m128 low_regrets = _mm_castsi128_ps(_mm_slli_epi32(
                _mm_and_si128(_mm_shuffle_epi8(low_bytes, expand_words),
                              regret_mask),
                18));
            const __m128 high_regrets = _mm_castsi128_ps(_mm_slli_epi32(
                _mm_and_si128(_mm_shuffle_epi8(high_bytes, expand_words),
                              regret_mask),
                18));
            const __m256d first = _mm256_cvtps_pd(_mm_shuffle_ps(
                low_regrets, high_regrets, _MM_SHUFFLE(2, 0, 2, 0)));
            const __m256d second = _mm256_cvtps_pd(_mm_shuffle_ps(
                low_regrets, high_regrets, _MM_SHUFFLE(3, 1, 3, 1)));
            const __m256d sums = _mm256_add_pd(first, second);
            const __m256d no_positive =
                _mm256_cmp_pd(sums, zero, _CMP_LE_OS);
            const __m256d inverse = _mm256_div_pd(one, sums);
            store_four_from_double(
                strategies[0].data() + local,
                _mm256_blendv_pd(_mm256_mul_pd(first, inverse), half,
                                 no_positive));
            store_four_from_double(
                strategies[1].data() + local,
                _mm256_blendv_pd(_mm256_mul_pd(second, inverse), half,
                                 no_positive));
          }
        } else if (action_count == 3U) {
          const __m128i expand_three_words =
              _mm_setr_epi8(0, 1, 2, -1, 3, 4, 5, -1,
                            6, 7, 8, -1, -1, -1, -1, -1);
          const __m128i regret_mask = _mm_set1_epi32(0x1fff);
          const __m256d zero = _mm256_setzero_pd();
          const __m256d uniform = _mm256_set1_pd(1.0 / 3.0);
          for (; local + 4U < actor_combos.size(); local += 4U) {
            const auto *const block = state + local * 9U;
            __m128 row0 = _mm_castsi128_ps(_mm_slli_epi32(
                _mm_and_si128(
                    _mm_shuffle_epi8(
                        _mm_loadu_si128(
                            reinterpret_cast<const __m128i *>(block)),
                        expand_three_words),
                    regret_mask),
                18));
            __m128 row1 = _mm_castsi128_ps(_mm_slli_epi32(
                _mm_and_si128(
                    _mm_shuffle_epi8(
                        _mm_loadu_si128(
                            reinterpret_cast<const __m128i *>(block + 9U)),
                        expand_three_words),
                    regret_mask),
                18));
            __m128 row2 = _mm_castsi128_ps(_mm_slli_epi32(
                _mm_and_si128(
                    _mm_shuffle_epi8(
                        _mm_loadu_si128(
                            reinterpret_cast<const __m128i *>(block + 18U)),
                        expand_three_words),
                    regret_mask),
                18));
            __m128 row3 = _mm_castsi128_ps(_mm_slli_epi32(
                _mm_and_si128(
                    _mm_shuffle_epi8(
                        _mm_loadu_si128(
                            reinterpret_cast<const __m128i *>(block + 27U)),
                        expand_three_words),
                    regret_mask),
                18));
            _MM_TRANSPOSE4_PS(row0, row1, row2, row3);
            const __m256d first = _mm256_cvtps_pd(row0);
            const __m256d second = _mm256_cvtps_pd(row1);
            const __m256d third = _mm256_cvtps_pd(row2);
            const __m256d sums =
                _mm256_add_pd(_mm256_add_pd(first, second), third);
            const __m256d no_positive =
                _mm256_cmp_pd(sums, zero, _CMP_LE_OS);
            store_four_from_double(
                strategies[0].data() + local,
                _mm256_blendv_pd(_mm256_div_pd(first, sums), uniform,
                                 no_positive));
            store_four_from_double(
                strategies[1].data() + local,
                _mm256_blendv_pd(_mm256_div_pd(second, sums), uniform,
                                 no_positive));
            store_four_from_double(
                strategies[2].data() + local,
                _mm256_blendv_pd(_mm256_div_pd(third, sums), uniform,
                                 no_positive));
          }
        }
      }
      for (; local < actor_combos.size(); ++local) {
        const auto *const block = state + local * action_count * 3U;
        double sum = 0.0;
        for (std::size_t action = 0U; action < action_count; ++action) {
          const double regret = decode_regret13(
              static_cast<std::uint16_t>(
                  compact_word(block + action * 3U) & 0x1fffU));
          strategies[action][local] = static_cast<Scalar>(regret);
          sum += regret;
        }
        if (sum <= 0.0) {
          const double uniform = 1.0 / static_cast<double>(action_count);
          for (std::size_t action = 0U; action < action_count; ++action) {
            strategies[action][local] = static_cast<Scalar>(uniform);
          }
        } else if (action_count == 2U) {
          const double inverse = 1.0 / sum;
          strategies[0][local] = static_cast<Scalar>(strategies[0][local] * inverse);
          strategies[1][local] = static_cast<Scalar>(strategies[1][local] * inverse);
        } else {
          for (std::size_t action = 0U; action < action_count; ++action) {
            strategies[action][local] =
                static_cast<Scalar>(strategies[action][local] / sum);
          }
        }
      }
    } else if (!locked_node && action_count == 2U && layout_.uses_direct_action_bases &&
        buffers_.regret_float24 != nullptr) {
      // This traversal visits exactly the branches whose opponent reach is
      // zero. They still contribute to the updating player's average
      // strategy, so retain exact regret matching but decode four contiguous
      // two-action blocks at once just like the full CFR path.
      const auto *const regrets =
          buffers_.regret_float24 + static_cast<std::size_t>(decision.action_base) * 3U;
      const __m256d zero = _mm256_setzero_pd();
      const __m256d half = _mm256_set1_pd(0.5);
      const __m256d one = _mm256_set1_pd(1.0);
      const __m128i expand_float24 =
          _mm_setr_epi8(0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8, -1, 9, 10, 11, -1);
      std::size_t local = 0U;
      for (; local + 4U < actor_combos.size(); local += 4U) {
        const auto *const packed = regrets + local * 6U;
        const __m128 first_four = _mm_castsi128_ps(_mm_slli_epi32(
            _mm_shuffle_epi8(
                _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed)), expand_float24),
            7));
        const __m128 second_four = _mm_castsi128_ps(_mm_slli_epi32(
            _mm_shuffle_epi8(
                _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed + 12U)),
                expand_float24),
            7));
        const __m256d first =
            _mm256_cvtps_pd(_mm_shuffle_ps(first_four, second_four, _MM_SHUFFLE(2, 0, 2, 0)));
        const __m256d second =
            _mm256_cvtps_pd(_mm_shuffle_ps(first_four, second_four, _MM_SHUFFLE(3, 1, 3, 1)));
        const __m256d sums = _mm256_add_pd(first, second);
        const __m256d no_positive = _mm256_cmp_pd(sums, zero, _CMP_LE_OS);
        const __m256d inverse = _mm256_div_pd(one, sums);
        store_four_from_double(strategies[0].data() + local,
                         _mm256_blendv_pd(_mm256_mul_pd(first, inverse), half,
                                          no_positive));
        store_four_from_double(strategies[1].data() + local,
                         _mm256_blendv_pd(_mm256_mul_pd(second, inverse), half,
                                          no_positive));
      }
      for (; local < actor_combos.size(); ++local) {
        const auto *const block = regrets + local * 6U;
        const double first = static_cast<double>(decode_float24(block));
        const double second = static_cast<double>(decode_float24(block + 3U));
        const double sum = first + second;
        if (sum <= 0.0) {
          strategies[0][local] = static_cast<Scalar>(0.5);
          strategies[1][local] = static_cast<Scalar>(0.5);
        } else {
          strategies[0][local] = static_cast<Scalar>(first / sum);
          strategies[1][local] = static_cast<Scalar>(second / sum);
        }
      }
    } else {
      for (std::size_t local = 0; local < actor_combos.size(); ++local) {
        const auto combo_id = actor_combos[local];
        const auto locked = locked_node ? locked_root_strategy(board.local_index[combo_id])
                                        : std::nullopt;
        const auto strategy =
            locked ? *locked
                   : current_strategy(decision, static_cast<std::int16_t>(local), false);
        for (std::size_t action = 0; action < action_count; ++action) {
          strategies[action][local] = static_cast<Scalar>(strategy[action]);
        }
      }
    }
    for (std::size_t action = 0; action < action_count; ++action) {
      const auto child = node.edges[action].child;
      if ((decision.terminal_child_mask & static_cast<std::uint8_t>(1U << action)) != 0U) {
        continue;
      }
      static_cast<void>(materialize_actor_reach(
          board, updating_player, *reach[updating_player], strategies[action],
          actor_reach, actor_combos.size(), false));
      const ReachRef child_reach =
          updating_player == 0U ? ReachRef{&actor_reach, reach[1]}
                                : ReachRef{reach[0], &actor_reach};
      if (const auto error =
              accumulate_average_only(child, updating_player, child_reach, strategy_weight)) {
        return error;
      }
    }
    if (action_count == 2U && layout_.uses_direct_action_bases &&
        buffers_.strategy_float16 != nullptr) {
      auto *const averages =
          buffers_.strategy_float16 + static_cast<std::size_t>(decision.action_base);
      std::size_t local = 0U;
      for (; local + 4U <= actor_combos.size(); local += 4U) {
        const __m128i encoded =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(averages + local * 2U));
        const __m256 current = _mm256_cvtph_ps(encoded);
        alignas(32) float current_values[8];
        alignas(32) double updated[8];
        _mm256_store_ps(current_values, current);
        for (std::size_t lane = 0; lane < 4U; ++lane) {
          const auto reach_slot = board_reach_slot(board, updating_player, local + lane);
          const double weight = strategy_weight * (*reach[updating_player])[reach_slot];
          updated[lane * 2U] = static_cast<double>(current_values[lane * 2U]) +
                               weight * strategies[0][local + lane];
          updated[lane * 2U + 1U] =
              static_cast<double>(current_values[lane * 2U + 1U]) +
              weight * strategies[1][local + lane];
        }
        const __m128 low = _mm256_cvtpd_ps(_mm256_load_pd(updated));
        const __m128 high = _mm256_cvtpd_ps(_mm256_load_pd(updated + 4U));
        __m256 as_float = _mm256_castps128_ps256(low);
        as_float = _mm256_insertf128_ps(as_float, high, 1);
        _mm_storeu_si128(
            reinterpret_cast<__m128i *>(averages + local * 2U),
            _mm256_cvtps_ph(as_float, _MM_FROUND_TO_NEAREST_INT));
      }
      for (; local < actor_combos.size(); ++local) {
        const auto reach_slot = board_reach_slot(board, updating_player, local);
        const double weight = strategy_weight * (*reach[updating_player])[reach_slot];
        add_strategy(static_cast<std::size_t>(decision.action_base) + local * 2U,
                     weight * strategies[0][local]);
        add_strategy(static_cast<std::size_t>(decision.action_base) + local * 2U + 1U,
                     weight * strategies[1][local]);
      }
    } else if (action_count == 2U && layout_.uses_direct_action_bases &&
               buffers_.compact_state != nullptr) {
      for (std::size_t local = 0; local < actor_combos.size(); ++local) {
        const auto reach_slot = board_reach_slot(board, updating_player, local);
        const double weight = strategy_weight * (*reach[updating_player])[reach_slot];
        const auto offset = static_cast<std::size_t>(decision.action_base) + local * 2U;
        add_strategy_pair(offset, weight * strategies[0][local],
                          weight * strategies[1][local]);
      }
    } else {
      for (std::size_t local = 0; local < actor_combos.size(); ++local) {
        const auto reach_slot = board_reach_slot(board, updating_player, local);
        const double weight = strategy_weight * (*reach[updating_player])[reach_slot];
        const auto offset = static_cast<std::size_t>(decision.action_base) +
                            local * action_count;
        for (std::size_t action = 0; action < action_count; ++action) {
          add_strategy(offset + action, weight * strategies[action][local]);
        }
      }
    }
    return std::nullopt;
  }

  std::optional<PostflopSolverError> cfr_physical(const NodeId node_id,
                                                  const std::uint8_t updating_player,
                                                  const ReachRef &reach,
                                                  const double strategy_weight,
                                                  const bool updating_reach_nonzero,
                                                  const double public_update_multiplicity,
                                                  ComboVector &values_out,
                                                  const PhysicalOrbitContext *orbit_context =
                                                      nullptr) {
    const bool profile = hotpath_profiling_enabled();
    struct WallGuard {
      DenseTraversal *owner;
      bool enabled;
      std::chrono::steady_clock::time_point start;
      ~WallGuard() {
        if (enabled) {
          owner->prof_wall_seconds_ +=
              std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        }
      }
    } wall_guard{this, profile,
                 profile ? std::chrono::steady_clock::now()
                         : std::chrono::steady_clock::time_point{}};
    const auto &node = layout_.tree.nodes[static_cast<std::size_t>(node_id)];
    switch (node.kind) {
    case PublicNodeKind::TerminalFold:
    case PublicNodeKind::TerminalShowdown: {
      const auto t_terminal = profile ? std::chrono::steady_clock::now()
                                      : std::chrono::steady_clock::time_point{};
      const auto error = node.kind == PublicNodeKind::TerminalFold
                             ? fold_values_into(node, updating_player,
                                                *reach[1U - updating_player], values_out)
                             : showdown_values_into(node, updating_player,
                                                    *reach[1U - updating_player], values_out);
      if (profile) {
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t_terminal).count();
        prof_terminal_seconds_ += elapsed;
        if (node.kind == PublicNodeKind::TerminalFold) {
          prof_fold_seconds_ += elapsed;
        } else {
          prof_showdown_seconds_ += elapsed;
        }
      }
      return error;
    }
    case PublicNodeKind::Chance:
      return cfr_chance(node, updating_player, reach, strategy_weight,
                        updating_reach_nonzero, public_update_multiplicity, values_out,
                        orbit_context);
    case PublicNodeKind::Decision:
      return cfr_decision(node, updating_player, reach, strategy_weight,
                          updating_reach_nonzero, public_update_multiplicity, values_out,
                          orbit_context);
    }
    return PostflopSolverError::InvalidConfiguration;
  }

  Result<ComboVector, PostflopSolverError> policy_physical(const NodeId node_id,
                                                           const std::uint8_t updating_player,
                                                           const ReachRef &reach,
                                                           const bool best_response) {
    ++traversed_nodes_;
    const auto &node = layout_.tree.nodes[static_cast<std::size_t>(node_id)];
    switch (node.kind) {
    case PublicNodeKind::TerminalFold:
      return fold_values(node, updating_player, *reach[1U - updating_player]);
    case PublicNodeKind::TerminalShowdown:
      return showdown_values(node, updating_player, *reach[1U - updating_player]);
    case PublicNodeKind::Chance:
      return policy_chance(node, updating_player, reach, best_response);
    case PublicNodeKind::Decision:
      return policy_decision(node, updating_player, reach, best_response);
    }
    return Result<ComboVector, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }

public:
  [[nodiscard]] std::uint64_t traversed_nodes() const noexcept {
    std::uint64_t total = traversed_nodes_;
    if (parallel_worker_ != nullptr) {
      total += parallel_worker_->traversed_nodes();
    }
    for (const auto &worker : parallel_workers_) {
      total += worker->traversed_nodes();
    }
    return total;
  }
  [[nodiscard]] PostflopWorkCounters work_counters() const noexcept {
    PostflopWorkCounters total = work_counters_;
    if (parallel_worker_ != nullptr) {
      add_work_counters(total, parallel_worker_->work_counters());
    }
    for (const auto &worker : parallel_workers_) {
      add_work_counters(total, worker->work_counters());
    }
    return total;
  }
  [[nodiscard]] double maximum_normalization_error() const noexcept {
    double maximum = maximum_normalization_error_;
    if (parallel_worker_ != nullptr) {
      maximum = std::max(maximum, parallel_worker_->maximum_normalization_error());
    }
    for (const auto &worker : parallel_workers_) {
      maximum = std::max(maximum, worker->maximum_normalization_error());
    }
    return maximum;
  }

  Result<bool, PostflopSolverError> apply_deferred_regrets() {
    if (deferred_regret_delta_ == nullptr) {
      return Result<bool, PostflopSolverError>::success(true);
    }
    // Pool workers merge their per-thread deferred deltas into our deferred
    // delta and apply their own (disjoint) action indices on their own
    // threads, so the merge+clip work overlaps with our own apply. The
    // disjointness is guaranteed by the physical tree: each pool task owns a
    // distinct subtree and, with trivial automorphisms, distinct action
    // bases. The canonical path has no pool workers and is unaffected.
    std::vector<std::future<TraversalResult>> worker_futures;
    worker_futures.reserve(parallel_workers_.size());
    for (const auto &worker : parallel_workers_) {
      auto *const target = deferred_regret_delta_;
      std::packaged_task<TraversalResult(DenseTraversal &)> task(
          [worker = worker.get(), target](DenseTraversal &) {
            return worker->apply_pool_regrets(*target);
          });
      worker_futures.push_back(task.get_future());
      worker->dispatch_parallel_task(std::move(task));
    }
    std::size_t touched = 0U;
    if (buffers_.compact_state != nullptr) {
      // Two-action blocks are appended as consecutive indices.  Apply both
      // compact regrets together and preserve their strategy fields, avoiding
      // two generic read/modify/write dispatches per infoset.
      for (; touched + 1U < deferred_regret_touched_.size(); touched += 2U) {
        const auto first_index = deferred_regret_touched_[touched];
        const auto second_index = deferred_regret_touched_[touched + 1U];
        if (second_index != first_index + 1U) {
          break;
        }
        auto *const first_bytes = buffers_.compact_state + first_index * 3U;
        auto *const second_bytes = first_bytes + 3U;
        const auto first_word = compact_word(first_bytes);
        const auto second_word = compact_word(second_bytes);
        const double first =
            decode_regret13(static_cast<std::uint16_t>(first_word & 0x1fffU)) +
            (*deferred_regret_delta_)[first_index];
        const double second =
            decode_regret13(static_cast<std::uint16_t>(second_word & 0x1fffU)) +
            (*deferred_regret_delta_)[second_index];
        if (!std::isfinite(first) || !std::isfinite(second)) {
          return Result<bool, PostflopSolverError>::failure(
              PostflopSolverError::NumericalFailure);
        }
        store_compact_word(first_bytes, (first_word & 0xffe000U) |
                                           encode_regret13(std::max(0.0, first)));
        store_compact_word(second_bytes, (second_word & 0xffe000U) |
                                            encode_regret13(std::max(0.0, second)));
        (*deferred_regret_delta_)[first_index] = 0.0;
        (*deferred_regret_delta_)[second_index] = 0.0;
        clear_deferred_regret_touched(first_index);
        clear_deferred_regret_touched(second_index);
      }
    } else if (buffers_.regret_float24 != nullptr) {
      // Two-action infosets dominate the exact postflop layouts and append
      // their two consecutive indices together. Decode and encode the packed
      // six-byte pair in one load/store while preserving the scalar update
      // order and float24 round-to-nearest behavior for each action.
      for (; touched + 1U < deferred_regret_touched_.size(); touched += 2U) {
        const auto first_index = deferred_regret_touched_[touched];
        const auto second_index = deferred_regret_touched_[touched + 1U];
        if (second_index != first_index + 1U) {
          break;
        }
        auto *const packed = buffers_.regret_float24 + first_index * 3U;
        std::uint64_t pair = 0U;
        std::memcpy(&pair, packed, 6U);
        const auto first_bits = static_cast<std::uint32_t>(pair & 0x00ffffffULL) << 7U;
        const auto second_bits =
            static_cast<std::uint32_t>((pair >> 24U) & 0x00ffffffULL) << 7U;
        const double first = static_cast<double>(std::bit_cast<float>(first_bits)) +
                             (*deferred_regret_delta_)[first_index];
        const double second = static_cast<double>(std::bit_cast<float>(second_bits)) +
                              (*deferred_regret_delta_)[second_index];
        if (!std::isfinite(first) || !std::isfinite(second)) {
          return Result<bool, PostflopSolverError>::failure(
              PostflopSolverError::NumericalFailure);
        }
        const std::uint64_t encoded =
            static_cast<std::uint64_t>(encode_float24_bits(std::max(0.0, first))) |
            (static_cast<std::uint64_t>(encode_float24_bits(std::max(0.0, second))) << 24U);
        std::memcpy(packed, &encoded, 6U);
        (*deferred_regret_delta_)[first_index] = 0.0;
        (*deferred_regret_delta_)[second_index] = 0.0;
        clear_deferred_regret_touched(first_index);
        clear_deferred_regret_touched(second_index);
      }
    }
    for (; touched < deferred_regret_touched_.size(); ++touched) {
      const auto index = deferred_regret_touched_[touched];
      const auto updated = buffers_.regret_at(index) + (*deferred_regret_delta_)[index];
      if (!std::isfinite(updated)) {
        return Result<bool, PostflopSolverError>::failure(PostflopSolverError::NumericalFailure);
      }
      buffers_.set_regret(index, std::max(0.0, updated));
      (*deferred_regret_delta_)[index] = 0.0;
      clear_deferred_regret_touched(index);
    }
    deferred_regret_touched_.clear();
    for (auto &future : worker_futures) {
      const auto applied = future.get();
      if (!applied) {
        return Result<bool, PostflopSolverError>::failure(applied.error());
      }
    }
    return Result<bool, PostflopSolverError>::success(true);
  }

  // Pool worker: merges this traversal's deferred regret deltas into the
  // shared target delta and applies the clipped regrets for its own action
  // indices (disjoint from every other thread's), then resets its own state.
  // Called on the worker thread via an apply task at the end of a player pass.
  Result<ComboVector, PostflopSolverError> apply_pool_regrets(std::vector<double> &target_delta) {
    for (const std::size_t index : deferred_regret_touched_) {
      target_delta[index] += (*deferred_regret_delta_)[index];
      (*deferred_regret_delta_)[index] = 0.0;
      const auto updated = buffers_.regret_at(index) + target_delta[index];
      if (!std::isfinite(updated)) {
        return Result<ComboVector, PostflopSolverError>::failure(
            PostflopSolverError::NumericalFailure);
      }
      buffers_.set_regret(index, std::max(0.0, updated));
      target_delta[index] = 0.0;
      clear_deferred_regret_touched(index);
    }
    deferred_regret_touched_.clear();
    return Result<ComboVector, PostflopSolverError>::success(ComboVector{});
  }

private:
  [[nodiscard]] bool
  identity_automorphism(const std::uint8_t automorphism_index) const noexcept {
    return layout_.automorphisms.size() == 1U ||
           (automorphism_index < layout_.active_automorphism_is_identity.size() &&
            layout_.active_automorphism_is_identity[automorphism_index] != 0U);
  }

  std::array<ComboVector, 2> transform_reach(const std::array<ComboVector, 2> &reach,
                                             const std::uint8_t automorphism_index) const {
    if (layout_.automorphisms.size() == 1U ||
        (automorphism_index < layout_.active_automorphism_is_identity.size() &&
         layout_.active_automorphism_is_identity[automorphism_index] != 0U)) {
      return reach;
    }
    std::array<ComboVector, 2> transformed{};
    if constexpr (PlayerIndexed) {
      const auto &mapping = layout_.automorphisms[automorphism_index].combos;
      for (std::uint8_t player = 0U; player < 2U; ++player) {
        for (std::size_t source_slot = 0U;
             source_slot < layout_.player_flop_combos[player].size(); ++source_slot) {
          const auto source_combo = layout_.player_flop_combos[player][source_slot];
          const auto target_slot = layout_.player_flop_slot[player][mapping[source_combo]];
          if (target_slot >= 0) {
            transformed[player][static_cast<std::size_t>(target_slot)] =
                reach[player][source_slot];
          }
        }
      }
    } else if constexpr (Capacity != combo_count) {
      const auto &mapping = layout_.active_automorphism_slots[automorphism_index];
      for (std::size_t slot = 0; slot < mapping.size(); ++slot) {
        transformed[0][mapping[slot]] = reach[0][slot];
        transformed[1][mapping[slot]] = reach[1][slot];
      }
    } else {
      const auto &mapping = layout_.automorphisms[automorphism_index].combos;
      for (const ComboId combo : layout_.active_combos) {
        transformed[0][mapping[combo]] = reach[0][combo];
        transformed[1][mapping[combo]] = reach[1][combo];
      }
    }
    return transformed;
  }

  ComboVector transform_values_to_parent(const ComboVector &child_values,
                                         const std::uint8_t automorphism_index,
                                         const std::uint8_t updating_player) const {
    if (identity_automorphism(automorphism_index)) {
      return child_values;
    }
    ComboVector parent_values{};
    if constexpr (PlayerIndexed) {
      const auto &mapping = layout_.automorphisms[automorphism_index].combos;
      for (std::size_t parent_slot = 0U;
           parent_slot < layout_.player_flop_combos[updating_player].size(); ++parent_slot) {
        const auto parent_combo = layout_.player_flop_combos[updating_player][parent_slot];
        const auto child_slot = layout_.player_flop_slot[updating_player][mapping[parent_combo]];
        if (child_slot >= 0) {
          parent_values[parent_slot] = child_values[static_cast<std::size_t>(child_slot)];
        }
      }
    } else if constexpr (Capacity != combo_count) {
      const auto &mapping = layout_.active_automorphism_slots[automorphism_index];
      for (std::size_t slot = 0; slot < mapping.size(); ++slot) {
        parent_values[slot] = child_values[mapping[slot]];
      }
    } else {
      const auto &mapping = layout_.automorphisms[automorphism_index].combos;
      for (const ComboId combo : layout_.active_combos) {
        parent_values[combo] = child_values[mapping[combo]];
      }
    }
    return parent_values;
  }

  void accumulate_transformed_values_to_parent(
      ComboVector &parent_values, const ComboVector &child_values,
      const std::uint8_t automorphism_index,
      const std::uint8_t updating_player, const BoardData &parent_board,
      const CardId chance_card, const double probability) const {
    const bool identity = identity_automorphism(automorphism_index);
    const auto *const mapping = identity
                                    ? nullptr
                                    : &layout_.automorphisms[automorphism_index].combos;
    if constexpr (PlayerIndexed) {
      const auto &compatible_slots =
          parent_board.chance_compatible_flop_slots[updating_player]
                                                    [chance_card.value()];
      for (const auto parent_slot_word : compatible_slots) {
        const auto parent_slot = static_cast<std::size_t>(parent_slot_word);
        std::size_t child_slot = parent_slot;
        if (!identity) {
          const auto combo =
              layout_.player_flop_combos[updating_player][parent_slot];
          const auto mapped_slot =
              layout_.player_flop_slot[updating_player][(*mapping)[combo]];
          if (mapped_slot < 0) {
            continue;
          }
          child_slot = static_cast<std::size_t>(mapped_slot);
        }
        parent_values[parent_slot] = static_cast<Scalar>(
            parent_values[parent_slot] + probability * child_values[child_slot]);
      }
    } else {
      for (const ComboId combo : parent_board.player_combos[updating_player]) {
        if ((layout_.combo_masks[combo] & chance_card.mask()) != 0U) {
          continue;
        }
        const auto parent_slot = value_slot(combo, updating_player);
        const auto child_slot = identity
                                    ? parent_slot
                                    : value_slot((*mapping)[combo],
                                                 updating_player);
        parent_values[parent_slot] = static_cast<Scalar>(
            parent_values[parent_slot] +
            probability * child_values[child_slot]);
      }
    }
  }

  std::optional<PostflopSolverError>
  accumulate_canonical_average_only(const std::uint32_t node_id,
                                    const std::uint8_t updating_player,
                                    const ReachRef &reach,
                                    const double strategy_weight) {
    const auto &canonical = layout_.canonical_public_graph.nodes[node_id];
    std::uint64_t chance_outcomes = 0U;
    if (canonical.kind == PublicNodeKind::Chance) {
      for (const auto &edge : canonical.edges) {
        chance_outcomes += edge.outcomes.size();
      }
    }
    count_work_node(canonical.kind, chance_outcomes);
    if (canonical.kind == PublicNodeKind::TerminalFold ||
        canonical.kind == PublicNodeKind::TerminalShowdown) {
      return std::nullopt;
    }
    if (canonical.kind == PublicNodeKind::Chance) {
      if (canonical.total_legal_outcome_count <= 4U) {
        return PostflopSolverError::InvalidConfiguration;
      }
      for (const auto &edge : canonical.edges) {
        if (edge.outcomes.empty()) {
          return PostflopSolverError::InvalidConfiguration;
        }
        const auto &representative = edge.outcomes.front();
        auto child_reach = transform_reach({*reach[0], *reach[1]},
                                           representative.physical_to_child_automorphism);
        if (const auto error = accumulate_canonical_average_only(
                representative.child, updating_player, {&child_reach[0], &child_reach[1]},
                strategy_weight)) {
          return error;
        }
      }
      return std::nullopt;
    }
    if (canonical.kind != PublicNodeKind::Decision) {
      return PostflopSolverError::InvalidConfiguration;
    }
    const auto &decision = canonical.decision;
    const auto &board = layout_.boards[decision.board_index];
    const auto action_count = static_cast<std::size_t>(decision.action_count);
    if (canonical.edges.size() != action_count) {
      return PostflopSolverError::InvalidConfiguration;
    }
    if (decision.player == updating_player && strategy_weight != 0.0) {
      work_counters_.strategy_update_entries += static_cast<std::uint64_t>(
          board.player_combos[updating_player].size() * action_count);
    }
    DecisionScratchLease scratch_lease(*this);
    auto &scratch = scratch_lease.get();
    auto &strategies = scratch.strategies;
    // Even when only the updating player's average is accumulated, both
    // players' reaches must follow the current profile. Omitting the
    // opponent's strategy here visits every opponent branch with full reach
    // and overweights all downstream strategy sums.
    load_canonical_current_strategies(canonical, board, decision.player, strategies, true);
    for (std::size_t action = 0; action < action_count; ++action) {
      if (canonical.edges[action].outcomes.size() != 1U) {
        return PostflopSolverError::InvalidConfiguration;
      }
      // Average-only actions are visited sequentially, so one reach buffer per
      // recursion level is sufficient even when the node has more than the
      // three action buffers reserved by the full traversal fast path.
      auto &actor_reach = scratch.reach_actor[0];
      actor_reach = *reach[decision.player];
      std::size_t actor_local = 0U;
      for (const ComboId combo : board.player_combos[decision.player]) {
        const auto slot = value_slot(combo, decision.player);
        actor_reach[slot] *= strategies[action][actor_local++];
      }
      const auto &outcome = canonical.edges[action].outcomes.front();
      const ReachRef direct_child_reach =
          decision.player == 0U ? ReachRef{&actor_reach, reach[1]}
                                : ReachRef{reach[0], &actor_reach};
      if (identity_automorphism(outcome.physical_to_child_automorphism)) {
        if (const auto error = accumulate_canonical_average_only(
                outcome.child, updating_player, direct_child_reach,
                strategy_weight)) {
          return error;
        }
      } else {
        auto transformed = transform_reach(
            {*direct_child_reach[0], *direct_child_reach[1]},
            outcome.physical_to_child_automorphism);
        if (const auto error = accumulate_canonical_average_only(
                outcome.child, updating_player,
                {&transformed[0], &transformed[1]}, strategy_weight)) {
          return error;
        }
      }
    }
    if (decision.player == updating_player) {
      if (action_major_compact_state()) {
        update_action_major_compact_average(
            canonical, board, updating_player, reach, strategy_weight,
            strategies, true);
      } else if (scaled_action_major_state()) {
        update_scaled_average(canonical, board, updating_player, reach,
                              strategy_weight, strategies, true);
      } else {
        for (const ComboId combo : board.player_combos[updating_player]) {
          const auto slot = value_slot(combo, updating_player);
          const auto local = board.player_local[updating_player][combo];
          const auto offset = canonical_action_base(canonical, local);
          for (std::size_t action = 0; action < action_count; ++action) {
            add_strategy(static_cast<std::size_t>(offset + action),
                         strategy_weight * (*reach[updating_player])[slot] *
                             strategies[action][static_cast<std::size_t>(local)]);
          }
        }
      }
    }
    return std::nullopt;
  }

#if 0
  // Rejected 2026-08-11: exact simultaneous DCFR+ converged too slowly on
  // GTP-AHKHQH-101 (1.37264% dEV after 200 iterations, 11.5957 s).
  std::optional<PostflopSolverError> cfr_canonical_simultaneous(
      const std::uint32_t node_id, const ReachRef &reach, const double strategy_weight,
      std::array<ComboVector, 2> &values_out) {
    ++traversed_nodes_;
    const auto &canonical = layout_.canonical_public_graph.nodes[node_id];
    if (canonical.kind == PublicNodeKind::TerminalFold ||
        canonical.kind == PublicNodeKind::TerminalShowdown) {
      for (std::uint8_t player = 0U; player < 2U; ++player) {
        const auto result = canonical.kind == PublicNodeKind::TerminalFold
                                ? fold_values_with_payoff(
                                      canonical.board_index, canonical.fold_payoff_antes[player],
                                      player, *reach[1U - player])
                                : showdown_values_with_payoffs(
                                      canonical.board_index,
                                      canonical.showdown_payoff_antes[player][2],
                                      canonical.showdown_payoff_antes[player][1],
                                      canonical.showdown_payoff_antes[player][0], player,
                                      *reach[1U - player]);
        if (!result) {
          return result.error();
        }
        values_out[player] = std::move(result.value());
      }
      return std::nullopt;
    }
    if (canonical.kind == PublicNodeKind::Chance) {
      if (canonical.total_legal_outcome_count <= 4U) {
        return PostflopSolverError::InvalidConfiguration;
      }
      const auto &board = layout_.boards[canonical.board_index];
      const double denominator =
          static_cast<double>(canonical.total_legal_outcome_count - 4U);
      values_out[0] = zeroed_values(0U);
      values_out[1] = zeroed_values(1U);
      for (const auto &edge : canonical.edges) {
        std::optional<std::array<ComboVector, 2>> representative_reach;
        std::optional<std::array<ComboVector, 2>> representative_values;
        for (const auto &outcome : edge.outcomes) {
          auto child_reach = transform_reach(block_card(reach, board, outcome.chance_card),
                                             outcome.physical_to_child_automorphism);
          const std::array<ComboVector, 2> *child_values = nullptr;
          std::optional<std::array<ComboVector, 2>> distinct_values;
          if (representative_reach && child_reach == *representative_reach) {
            child_values = &*representative_values;
          } else {
            std::array<ComboVector, 2> child;
            if (const auto error = cfr_canonical_simultaneous(
                    outcome.child, {&child_reach[0], &child_reach[1]}, strategy_weight, child)) {
              return error;
            }
            if (!representative_reach) {
              representative_reach = child_reach;
              representative_values = std::move(child);
              child_values = &*representative_values;
            } else {
              distinct_values = std::move(child);
              child_values = &*distinct_values;
            }
          }
          const double probability =
              static_cast<double>(outcome.physical_outcome_count) / denominator;
          for (std::uint8_t player = 0U; player < 2U; ++player) {
            const auto parent_values = transform_values_to_parent(
                (*child_values)[player], outcome.physical_to_child_automorphism);
            for (const ComboId combo : board.legal_combos) {
              if ((layout_.combo_masks[combo] & outcome.chance_card.mask()) == 0U) {
                const auto slot = value_slot(combo, player);
                values_out[player][slot] += probability * parent_values[slot];
              }
            }
          }
        }
      }
      return std::nullopt;
    }
    if (canonical.kind != PublicNodeKind::Decision) {
      return PostflopSolverError::InvalidConfiguration;
    }

    const auto &decision = canonical.decision;
    const auto &board = layout_.boards[decision.board_index];
    const auto actor = static_cast<std::uint8_t>(decision.player);
    const auto opponent = static_cast<std::uint8_t>(1U - actor);
    const auto action_count = static_cast<std::size_t>(decision.action_count);
    if (canonical.edges.size() != action_count) {
      return PostflopSolverError::InvalidConfiguration;
    }
    DecisionScratchLease scratch_lease(*this);
    auto &scratch = scratch_lease.get();
    auto &actor_action_values = scratch.action_values;
    auto &opponent_action_values = scratch.opponent_action_values;
    auto &strategies = scratch.strategies;
    const bool locked_root = is_locked_root(canonical);
    for (const ComboId combo : board.legal_combos) {
      const auto local = board.local_index[combo];
      const auto locked = locked_root ? locked_root_strategy(local) : std::nullopt;
      const auto strategy = locked ? *locked : current_strategy(canonical, local, false);
      const auto slot = value_slot(combo, actor);
      for (std::size_t action = 0U; action < action_count; ++action) {
        strategies[action][slot] = strategy[action];
      }
    }
    for (std::size_t action = 0U; action < action_count; ++action) {
      if (canonical.edges[action].outcomes.size() != 1U) {
        return PostflopSolverError::InvalidConfiguration;
      }
      std::array<ComboVector, 2> child_reach{*reach[0], *reach[1]};
      for (const ComboId combo : board.player_combos[decision.player]) {
        const auto slot = value_slot(combo, actor);
        child_reach[actor][slot] *= strategies[action][slot];
      }
      const auto &outcome = canonical.edges[action].outcomes.front();
      child_reach = transform_reach(child_reach, outcome.physical_to_child_automorphism);
      std::array<ComboVector, 2> child_values;
      if (const auto error = cfr_canonical_simultaneous(
              outcome.child, {&child_reach[0], &child_reach[1]}, strategy_weight,
              child_values)) {
        return error;
      }
      actor_action_values[action] = transform_values_to_parent(
          child_values[actor], outcome.physical_to_child_automorphism);
      opponent_action_values[action] = transform_values_to_parent(
          child_values[opponent], outcome.physical_to_child_automorphism);
    }

    values_out[actor] = zeroed_values(actor);
    values_out[opponent] = zeroed_values(opponent);
    for (const ComboId combo : board.legal_combos) {
      const auto actor_slot = value_slot(combo, actor);
      for (std::size_t action = 0U; action < action_count; ++action) {
        values_out[actor][actor_slot] +=
            strategies[action][actor_slot] * actor_action_values[action][actor_slot];
      }
      const auto opponent_slot = value_slot(combo, opponent);
      for (std::size_t action = 0U; action < action_count; ++action) {
        values_out[opponent][opponent_slot] +=
            opponent_action_values[action][opponent_slot];
      }
    }
    for (const ComboId combo : board.legal_combos) {
      const auto local = board.local_index[combo];
      const auto slot = value_slot(combo, actor);
      const auto offset = canonical_action_base(canonical, local);
      for (std::size_t action = 0U; action < action_count; ++action) {
        const auto index = static_cast<std::size_t>(offset + action);
        if (!locked_root) {
          const double delta = regret_update_weight_ *
                               (actor_action_values[action][slot] - values_out[actor][slot]);
          add_deferred_regret(index, delta);
        }
        add_strategy(index, strategy_weight * (*reach[actor])[slot] *
                                strategies[action][slot]);
      }
    }
    return std::nullopt;
  }

#endif

  std::optional<PostflopSolverError> cfr_canonical_simultaneous_into(
      const std::uint32_t node_id, const ReachRef &reach,
      const double strategy_weight,
      std::array<ComboVector, 2> &values_out) {
    ++traversed_nodes_;
    const auto &canonical = layout_.canonical_public_graph.nodes[node_id];
    if (canonical.kind == PublicNodeKind::TerminalFold ||
        canonical.kind == PublicNodeKind::TerminalShowdown) {
      for (std::uint8_t player = 0U; player < 2U; ++player) {
        const auto error =
            canonical.kind == PublicNodeKind::TerminalFold
                ? fold_values_with_payoff_into(
                      canonical.board_index,
                      canonical.fold_payoff_antes[player], player,
                      *reach[1U - player], values_out[player])
                : showdown_values_with_payoffs_into(
                      canonical.board_index,
                      canonical.showdown_payoff_antes[player][0],
                      canonical.showdown_payoff_antes[player][1],
                      canonical.showdown_payoff_antes[player][2], player,
                      *reach[1U - player], values_out[player], 0.0, nullptr);
        if (error) {
          return error;
        }
      }
      return std::nullopt;
    }
    if (canonical.kind == PublicNodeKind::Chance) {
      if (canonical.total_legal_outcome_count <= 4U) {
        return PostflopSolverError::InvalidConfiguration;
      }
      const auto &board = layout_.boards[canonical.board_index];
      const double denominator =
          static_cast<double>(canonical.total_legal_outcome_count - 4U);
      zero_values_into(0U, values_out[0]);
      zero_values_into(1U, values_out[1]);
      const auto edge_count = canonical.edges.size();
      auto representative_reaches =
          std::make_unique_for_overwrite<std::array<ComboVector, 2>[]>(edge_count);
      auto child_values =
          std::make_unique<std::array<ComboVector, 2>[]>(edge_count);
      for (std::size_t index = 0U; index < edge_count; ++index) {
        const auto &edge = canonical.edges[index];
        if (edge.outcomes.empty()) {
          return PostflopSolverError::InvalidConfiguration;
        }
        representative_reaches[index] = transform_reach(
            {*reach[0], *reach[1]},
            edge.outcomes.front().physical_to_child_automorphism);
      }
      const auto worker_count = parallel_workers_.size();
      if (std::popcount(board.mask) == 3U && worker_count > 0U &&
          edge_count > 1U) {
        const std::size_t chunk_count =
            std::min(edge_count, worker_count + 1U);
        std::vector<std::packaged_task<TraversalResult(DenseTraversal &)>> tasks;
        std::vector<std::future<TraversalResult>> futures;
        tasks.reserve(chunk_count - 1U);
        futures.reserve(chunk_count - 1U);
        for (std::size_t chunk = 1U; chunk < chunk_count; ++chunk) {
          const std::size_t begin = chunk * edge_count / chunk_count;
          const std::size_t end = (chunk + 1U) * edge_count / chunk_count;
          std::packaged_task<TraversalResult(DenseTraversal &)> task(
              [edges = &canonical.edges, reaches = representative_reaches.get(),
               results = child_values.get(), begin, end,
               strategy_weight](DenseTraversal &self) {
                for (std::size_t index = begin; index < end; ++index) {
                  if (const auto error = self.cfr_canonical_simultaneous_into(
                          (*edges)[index].outcomes.front().child,
                          {&reaches[index][0], &reaches[index][1]},
                          strategy_weight, results[index])) {
                    return Result<ComboVector, PostflopSolverError>::failure(*error);
                  }
                }
                return Result<ComboVector, PostflopSolverError>::success(ComboVector{});
              });
          futures.push_back(task.get_future());
          tasks.push_back(std::move(task));
        }
        dispatch_parallel_tasks(tasks);
        std::optional<PostflopSolverError> main_error;
        const std::size_t main_end = edge_count / chunk_count;
        for (std::size_t index = 0U; index < main_end && !main_error; ++index) {
          main_error = cfr_canonical_simultaneous_into(
              canonical.edges[index].outcomes.front().child,
              {&representative_reaches[index][0],
               &representative_reaches[index][1]},
              strategy_weight, child_values[index]);
        }
        std::optional<PostflopSolverError> worker_error;
        for (auto &future : futures) {
          wait_for_parallel_future(future);
          auto result = future.get();
          if (!result && !worker_error) {
            worker_error = result.error();
          }
        }
        if (main_error || worker_error) {
          return main_error ? main_error : worker_error;
        }
      } else {
        for (std::size_t index = 0U; index < edge_count; ++index) {
          if (const auto error = cfr_canonical_simultaneous_into(
                  canonical.edges[index].outcomes.front().child,
                  {&representative_reaches[index][0],
                   &representative_reaches[index][1]},
                  strategy_weight, child_values[index])) {
            return error;
          }
        }
      }
      for (std::size_t index = 0U; index < edge_count; ++index) {
        const auto &edge = canonical.edges[index];
        for (const auto &outcome : edge.outcomes) {
          const double probability =
              static_cast<double>(outcome.physical_outcome_count) / denominator;
          for (std::uint8_t player = 0U; player < 2U; ++player) {
            accumulate_transformed_values_to_parent(
                values_out[player], child_values[index][player],
                outcome.physical_to_child_automorphism, player, board,
                outcome.chance_card, probability);
          }
        }
      }
      return std::nullopt;
    }
    if (canonical.kind != PublicNodeKind::Decision) {
      return PostflopSolverError::InvalidConfiguration;
    }
    const auto &decision = canonical.decision;
    const auto &board = layout_.boards[decision.board_index];
    const auto actor = static_cast<std::uint8_t>(decision.player);
    const auto opponent = static_cast<std::uint8_t>(1U - actor);
    const auto action_count = static_cast<std::size_t>(decision.action_count);
    if (canonical.edges.size() != action_count) {
      return PostflopSolverError::InvalidConfiguration;
    }
    DecisionScratchLease scratch_lease(*this);
    auto &scratch = scratch_lease.get();
    auto &actor_action_values = scratch.action_values;
    auto &opponent_action_values = scratch.opponent_action_values;
    auto &strategies = scratch.strategies;
    load_canonical_current_strategies(canonical, board, actor, strategies, true);
    for (std::size_t action = 0U; action < action_count; ++action) {
      if (canonical.edges[action].outcomes.size() != 1U) {
        return PostflopSolverError::InvalidConfiguration;
      }
      auto &actor_reach = scratch.reach_actor[0];
      actor_reach = *reach[actor];
      for (std::size_t local = 0U;
           local < board.player_combos[actor].size(); ++local) {
        const auto slot = static_cast<std::size_t>(
            board.player_flop_slots[actor][local]);
        actor_reach[slot] *= strategies[action][local];
      }
      const ReachRef direct_child_reach =
          actor == 0U ? ReachRef{&actor_reach, reach[1]}
                      : ReachRef{reach[0], &actor_reach};
      const auto &outcome = canonical.edges[action].outcomes.front();
      std::array<ComboVector, 2> child;
      if (identity_automorphism(outcome.physical_to_child_automorphism)) {
        if (const auto error = cfr_canonical_simultaneous_into(
                outcome.child, direct_child_reach, strategy_weight, child)) {
          return error;
        }
      } else {
        auto transformed = transform_reach(
            {*direct_child_reach[0], *direct_child_reach[1]},
            outcome.physical_to_child_automorphism);
        if (const auto error = cfr_canonical_simultaneous_into(
                outcome.child, {&transformed[0], &transformed[1]},
                strategy_weight, child)) {
          return error;
        }
      }
      actor_action_values[action] = transform_values_to_parent(
          child[actor], outcome.physical_to_child_automorphism, actor);
      opponent_action_values[action] = transform_values_to_parent(
          child[opponent], outcome.physical_to_child_automorphism, opponent);
    }
    zero_values_into(actor, values_out[actor]);
    zero_values_into(opponent, values_out[opponent]);
    for (std::size_t local = 0U;
         local < board.player_combos[actor].size(); ++local) {
      const auto slot = static_cast<std::size_t>(
          board.player_flop_slots[actor][local]);
      for (std::size_t action = 0U; action < action_count; ++action) {
        values_out[actor][slot] = static_cast<Scalar>(
            values_out[actor][slot] +
            strategies[action][local] * actor_action_values[action][slot]);
      }
    }
    for (const ComboId combo : board.player_combos[opponent]) {
      const auto slot = value_slot(combo, opponent);
      for (std::size_t action = 0U; action < action_count; ++action) {
        values_out[opponent][slot] = static_cast<Scalar>(
            values_out[opponent][slot] + opponent_action_values[action][slot]);
      }
    }
    const bool locked_root = is_locked_root(canonical);
    for (std::size_t local = 0U;
         local < board.player_combos[actor].size(); ++local) {
      const auto slot = static_cast<std::size_t>(
          board.player_flop_slots[actor][local]);
      const auto offset = static_cast<std::size_t>(decision.action_base) +
                          local * action_count;
      const double reach_weight =
          strategy_weight * static_cast<double>((*reach[actor])[slot]);
      for (std::size_t action = 0U; action < action_count; ++action) {
        const auto index = offset + action;
        if (!locked_root) {
          buffers_.set_regret(
              index,
              std::max(0.0, buffers_.regret_at(index) +
                                regret_update_weight_ *
                                    (static_cast<double>(
                                         actor_action_values[action][slot]) -
                                     static_cast<double>(values_out[actor][slot]))));
        }
        if (reach_weight != 0.0) {
          add_strategy(index,
                       reach_weight *
                           static_cast<double>(strategies[action][local]));
        }
      }
    }
    return std::nullopt;
  }

  template <std::size_t ActionCount>
  std::optional<PostflopSolverError>
  cfr_canonical_river_decision_fixed_into(
      const CanonicalPublicNode &canonical,
      const std::uint8_t updating_player, const ReachRef &reach,
      const double strategy_weight, ComboVector &values) {
    const bool profile = hotpath_profiling_enabled();
    const auto strategy_started = profile ? std::chrono::steady_clock::now()
                                          : std::chrono::steady_clock::time_point{};
    if (profile) {
      ++prof_decisions_;
    }
    const auto &decision = canonical.decision;
    const auto &board = layout_.boards[decision.board_index];
    constexpr std::size_t action_count = ActionCount;
    if (decision.player == updating_player) {
      const auto entries = static_cast<std::uint64_t>(
          board.player_combos[updating_player].size() * action_count);
      if (!is_locked_root(canonical)) {
        work_counters_.regret_update_entries += entries;
      }
      if (strategy_weight != 0.0) {
        work_counters_.strategy_update_entries += entries;
      }
    }
    DecisionScratchLease scratch_lease(*this);
    auto &action_values = scratch_lease.get().action_values;
    auto &strategies = scratch_lease.get().strategies;
    auto &actor_reach = scratch_lease.get().reach_actor[0];
    const bool locked_root = is_locked_root(canonical);
    // If the updating player never acts again below this river node, its own
    // reach is dead for every child: counterfactual values consume only the
    // opponent reach, and there is no later own average-strategy update.
    // Decode regret matching directly in the fused value/regret/average
    // kernel instead of materializing and rereading strategy scratch.
    const bool decode_strategy_at_update =
        decision.player == updating_player && !locked_root &&
        (decision.descendant_player_mask &
         static_cast<std::uint8_t>(1U << updating_player)) == 0U;
    if (!decode_strategy_at_update) {
      load_canonical_current_strategies(canonical, board, updating_player,
                                        strategies, true);
    }
    if (profile) {
      prof_strategy_seconds_ += std::chrono::duration<double>(
          std::chrono::steady_clock::now() - strategy_started).count();
      if (!decode_strategy_at_update) {
        prof_strategy_entries_ +=
            board.player_combos[decision.player].size() * action_count;
      }
    }
    const auto paired_fold_action =
        static_cast<std::size_t>(decision.paired_fold_action);
    const auto paired_showdown_action =
        static_cast<std::size_t>(decision.paired_showdown_action);
    const auto actor_count = board.player_combos[decision.player].size();
    const auto materialize_actor_reach =
        [&](const std::size_t action, ComboVector &destination) {
          bool any_nonzero = false;
          std::size_t local = 0U;
          if constexpr (std::is_same_v<Scalar, float>) {
            const __m256 zero = _mm256_setzero_ps();
            __m256 nonzero = zero;
            for (; local + 8U <= actor_count; local += 8U) {
              const __m256 child = _mm256_mul_ps(
                  _mm256_loadu_ps(reach[decision.player]->data() + local),
                  _mm256_loadu_ps(strategies[action].data() + local));
              _mm256_storeu_ps(destination.data() + local, child);
              nonzero = _mm256_or_ps(
                  nonzero, _mm256_cmp_ps(child, zero, _CMP_NEQ_OQ));
            }
            any_nonzero = _mm256_movemask_ps(nonzero) != 0;
          }
          for (; local < actor_count; ++local) {
            destination[local] = static_cast<Scalar>(
                (*reach[decision.player])[local] * strategies[action][local]);
            any_nonzero = any_nonzero || destination[local] != Scalar{0};
          }
          if (profile) {
            prof_actor_writes_ += actor_count;
          }
          return any_nonzero;
        };
    const auto evaluate_direct_child =
        [&](const CanonicalPublicNode &child, const ReachRef &child_reach,
            ComboVector &child_values) -> std::optional<PostflopSolverError> {
          if (child.kind == PublicNodeKind::TerminalFold) {
            ++traversed_nodes_;
            count_work_node(PublicNodeKind::TerminalFold, 0U);
            return fold_values_with_payoff_into<true>(
                child.board_index, child.fold_payoff_antes[updating_player],
                updating_player, *child_reach[1U - updating_player],
                child_values);
          }
          if (child.kind == PublicNodeKind::TerminalShowdown) {
            ++traversed_nodes_;
            count_work_node(PublicNodeKind::TerminalShowdown, 0U);
            const auto &payoff = child.showdown_payoff_antes[updating_player];
            return showdown_values_with_payoffs_into<true>(
                child.board_index, payoff[0], payoff[1], payoff[2],
                updating_player, *child_reach[1U - updating_player],
                child_values, 0.0, nullptr);
          }
          if (child.kind == PublicNodeKind::Decision) {
            ++traversed_nodes_;
            count_work_node(PublicNodeKind::Decision, 0U);
            return cfr_canonical_river_decision_into(
                child, updating_player, child_reach, strategy_weight,
                child_values);
          }
          return PostflopSolverError::InvalidConfiguration;
        };
    for (std::size_t action = 0U; action < action_count; ++action) {
      if (action == paired_fold_action) {
        continue;
      }
      const auto &outcome = canonical.edges[action].outcomes.front();
      const auto &child = layout_.canonical_public_graph.nodes[outcome.child];
      const bool terminal = child.kind == PublicNodeKind::TerminalFold ||
                            child.kind == PublicNodeKind::TerminalShowdown;
      const bool actor_needed =
          decision.player != updating_player ||
          (!decode_strategy_at_update && !terminal && strategy_weight != 0.0);
      const bool actor_nonzero =
          actor_needed ? materialize_actor_reach(action, actor_reach) : true;
      const ReachRef child_reach =
          actor_needed
              ? (decision.player == 0U ? ReachRef{&actor_reach, reach[1]}
                                       : ReachRef{reach[0], &actor_reach})
              : reach;
      if (action == paired_showdown_action &&
          paired_fold_action < action_count) {
        const auto &fold_outcome =
            canonical.edges[paired_fold_action].outcomes.front();
        const auto &fold =
            layout_.canonical_public_graph.nodes[fold_outcome.child];
        if (child.kind != PublicNodeKind::TerminalShowdown ||
            fold.kind != PublicNodeKind::TerminalFold ||
            child.board_index != fold.board_index) {
          return PostflopSolverError::InvalidConfiguration;
        }
        auto &fold_actor_reach = scratch_lease.get().reach_actor[1];
        const ComboVector *fold_opponent_reach = reach[1U - updating_player];
        bool fold_nonzero = true;
        if (decision.player != updating_player) {
          fold_nonzero =
              materialize_actor_reach(paired_fold_action, fold_actor_reach);
          fold_opponent_reach = &fold_actor_reach;
        }
        if (!actor_nonzero) {
          std::fill_n(action_values[action].begin(),
                      board.player_combos[updating_player].size(), Scalar{0});
        }
        if (!fold_nonzero) {
          std::fill_n(action_values[paired_fold_action].begin(),
                      board.player_combos[updating_player].size(), Scalar{0});
        }
        if (actor_nonzero && fold_nonzero) {
          const auto &payoff = child.showdown_payoff_antes[updating_player];
          if (const auto error = showdown_values_with_payoffs_into<true, true>(
                  child.board_index, payoff[0], payoff[1], payoff[2],
                  updating_player, *child_reach[1U - updating_player],
                  action_values[action], fold.fold_payoff_antes[updating_player],
                  &action_values[paired_fold_action], fold_opponent_reach)) {
            return error;
          }
        } else if (actor_nonzero) {
          if (const auto error = evaluate_direct_child(
                  child, child_reach, action_values[action])) {
            return error;
          }
        } else if (fold_nonzero) {
          if (const auto error = fold_values_with_payoff_into<true>(
                  fold.board_index, fold.fold_payoff_antes[updating_player],
                  updating_player, *fold_opponent_reach,
                  action_values[paired_fold_action])) {
            return error;
          }
        }
        traversed_nodes_ += 2U;
        continue;
      }
      if (decision.player != updating_player && !actor_nonzero) {
        std::fill_n(action_values[action].begin(),
                    board.player_combos[updating_player].size(), Scalar{0});
        continue;
      }
      if (const auto error = evaluate_direct_child(
              child, child_reach, action_values[action])) {
        return error;
      }
    }

    const auto value_started = profile ? std::chrono::steady_clock::now()
                                       : std::chrono::steady_clock::time_point{};
    const auto updating_count = board.player_combos[updating_player].size();
    std::fill_n(values.begin(), updating_count, Scalar{0});
    if (decision.player != updating_player) {
      for (std::size_t action = 0U; action < action_count; ++action) {
        std::size_t local = 0U;
        if constexpr (std::is_same_v<Scalar, float>) {
          for (; local + 8U <= updating_count; local += 8U) {
            _mm256_storeu_ps(
                values.data() + local,
                _mm256_add_ps(_mm256_loadu_ps(values.data() + local),
                              _mm256_loadu_ps(action_values[action].data() + local)));
          }
        }
        for (; local < updating_count; ++local) {
          values[local] = static_cast<Scalar>(
              values[local] + action_values[action][local]);
        }
      }
    } else if (buffers_.signed_scaled_regret && !locked_root) {
      auto &average_scratch = scratch_lease.get().opponent_action_values;
      update_scaled_regrets<true, ActionCount>(
          canonical, board, updating_player, values, action_values, strategies,
          &average_scratch, &reach, true, decode_strategy_at_update);
    } else {
      return PostflopSolverError::InvalidConfiguration;
    }
    if (profile) {
      prof_value_update_seconds_ += std::chrono::duration<double>(
          std::chrono::steady_clock::now() - value_started).count();
    }
    return std::nullopt;
  }

  std::optional<PostflopSolverError>
  cfr_canonical_river_decision_into(
      const CanonicalPublicNode &canonical,
      const std::uint8_t updating_player, const ReachRef &reach,
      const double strategy_weight, ComboVector &values) {
    switch (canonical.decision.action_count) {
    case 1U:
      return cfr_canonical_river_decision_fixed_into<1U>(
          canonical, updating_player, reach, strategy_weight, values);
    case 2U:
      return cfr_canonical_river_decision_fixed_into<2U>(
          canonical, updating_player, reach, strategy_weight, values);
    case 3U:
      return cfr_canonical_river_decision_fixed_into<3U>(
          canonical, updating_player, reach, strategy_weight, values);
    case 4U:
      return cfr_canonical_river_decision_fixed_into<4U>(
          canonical, updating_player, reach, strategy_weight, values);
    case 5U:
      return cfr_canonical_river_decision_fixed_into<5U>(
          canonical, updating_player, reach, strategy_weight, values);
    default:
      return PostflopSolverError::InvalidConfiguration;
    }
  }

  std::optional<PostflopSolverError>
  cfr_canonical_river_into(const std::uint32_t node_id,
                           const std::uint8_t updating_player,
                           const ReachRef &reach,
                           const double strategy_weight,
                           ComboVector &values_out) {
    ++traversed_nodes_;
    const auto &canonical = layout_.canonical_public_graph.nodes[node_id];
    count_work_node(canonical.kind, 0U);
    switch (canonical.kind) {
    case PublicNodeKind::TerminalFold:
      return fold_values_with_payoff_into<true>(
          canonical.board_index, canonical.fold_payoff_antes[updating_player],
          updating_player, *reach[1U - updating_player], values_out);
    case PublicNodeKind::TerminalShowdown:
      return showdown_values_with_payoffs_into<true>(
          canonical.board_index,
          canonical.showdown_payoff_antes[updating_player][0],
          canonical.showdown_payoff_antes[updating_player][1],
          canonical.showdown_payoff_antes[updating_player][2],
          updating_player, *reach[1U - updating_player], values_out, 0.0,
          nullptr);
    case PublicNodeKind::Decision:
      return cfr_canonical_river_decision_into(
          canonical, updating_player, reach, strategy_weight, values_out);
    case PublicNodeKind::Chance:
      return PostflopSolverError::InvalidConfiguration;
    }
    return PostflopSolverError::InvalidConfiguration;
  }

  std::optional<PostflopSolverError>
  cfr_canonical_chance_child_into(
      const std::uint32_t node_id, const std::uint8_t updating_player,
      const ReachRef &flop_reach, const double strategy_weight,
      ComboVector &flop_values) {
    const auto &node = layout_.canonical_public_graph.nodes[node_id];
    const auto &board = layout_.boards[node.board_index];
    if (!buffers_.signed_scaled_regret || std::popcount(board.mask) != 5) {
      return cfr_canonical_into(node_id, updating_player, flop_reach,
                                strategy_weight, flop_values);
    }
    std::array<ComboVector, 2> local_reach{};
    for (std::uint8_t player = 0U; player < 2U; ++player) {
      const auto &slots = board.player_flop_slots[player];
      for (std::size_t local = 0U; local < slots.size(); ++local) {
        local_reach[player][local] = (*flop_reach[player])[slots[local]];
      }
    }
    ComboVector local_values{};
    if (const auto error = cfr_canonical_river_into(
            node_id, updating_player, {&local_reach[0], &local_reach[1]},
            strategy_weight, local_values)) {
      return error;
    }
    zero_values_into(updating_player, flop_values);
    const auto &slots = board.player_flop_slots[updating_player];
    for (std::size_t local = 0U; local < slots.size(); ++local) {
      flop_values[slots[local]] = local_values[local];
    }
    return std::nullopt;
  }

  std::optional<PostflopSolverError>
  cfr_canonical_into(const std::uint32_t node_id,
                     const std::uint8_t updating_player, const ReachRef &reach,
                     const double strategy_weight, ComboVector &values_out) {
    ++traversed_nodes_;
    const auto &canonical = layout_.canonical_public_graph.nodes[node_id];
    std::uint64_t chance_outcomes = 0U;
    if (canonical.kind == PublicNodeKind::Chance) {
      for (const auto &edge : canonical.edges) {
        chance_outcomes += edge.outcomes.size();
      }
    }
    count_work_node(canonical.kind, chance_outcomes);
    switch (canonical.kind) {
    case PublicNodeKind::TerminalFold:
      return fold_values_with_payoff_into(
          canonical.board_index, canonical.fold_payoff_antes[updating_player],
          updating_player, *reach[1U - updating_player], values_out);
    case PublicNodeKind::TerminalShowdown:
      return showdown_values_with_payoffs_into(
          canonical.board_index, canonical.showdown_payoff_antes[updating_player][0],
          canonical.showdown_payoff_antes[updating_player][1],
          canonical.showdown_payoff_antes[updating_player][2], updating_player,
          *reach[1U - updating_player], values_out, 0.0, nullptr);
    case PublicNodeKind::Chance:
      return cfr_canonical_chance_into(canonical, updating_player, reach,
                                       strategy_weight, values_out);
    case PublicNodeKind::Decision:
      return cfr_canonical_decision_into(canonical, updating_player, reach,
                                         strategy_weight, values_out);
    }
    return PostflopSolverError::InvalidConfiguration;
  }

  Result<ComboVector, PostflopSolverError> cfr_canonical(
      const std::uint32_t node_id, const std::uint8_t updating_player,
      const ReachRef &reach, const double strategy_weight) {
    ComboVector values{};
    if (const auto error = cfr_canonical_into(
            node_id, updating_player, reach, strategy_weight, values)) {
      return Result<ComboVector, PostflopSolverError>::failure(*error);
    }
    return Result<ComboVector, PostflopSolverError>::success(std::move(values));
  }

  Result<ComboVector, PostflopSolverError> policy_canonical(const std::uint32_t node_id,
                                                            const std::uint8_t updating_player,
                                                            const ReachRef &reach,
                                                            const bool best_response) {
    ++traversed_nodes_;
    const auto &canonical = layout_.canonical_public_graph.nodes[node_id];
    switch (canonical.kind) {
    case PublicNodeKind::TerminalFold:
      return fold_values_with_payoff(canonical.board_index,
                                     canonical.fold_payoff_antes[updating_player],
                                     updating_player, *reach[1U - updating_player]);
    case PublicNodeKind::TerminalShowdown:
      return showdown_values_with_payoffs(
          canonical.board_index, canonical.showdown_payoff_antes[updating_player][0],
          canonical.showdown_payoff_antes[updating_player][1],
          canonical.showdown_payoff_antes[updating_player][2], updating_player,
          *reach[1U - updating_player]);
    case PublicNodeKind::Chance:
      return policy_canonical_chance(canonical, updating_player, reach, best_response);
    case PublicNodeKind::Decision:
      return policy_canonical_decision(canonical, updating_player, reach, best_response);
    }
    return Result<ComboVector, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }

  std::optional<PostflopSolverError> cfr_canonical_chance_into(
      const CanonicalPublicNode &canonical, const std::uint8_t updating_player,
      const ReachRef &reach, const double strategy_weight,
      ComboVector &values) {
    const bool profile = hotpath_profiling_enabled();
    if (profile) {
      ++prof_chance_calls_;
      for (const auto &edge : canonical.edges) {
        prof_chance_outcomes_ += edge.outcomes.size();
      }
    }
    if (canonical.total_legal_outcome_count <= 4U) {
      return PostflopSolverError::InvalidConfiguration;
    }
    const auto &board = layout_.boards[canonical.board_index];
    const double denominator = static_cast<double>(canonical.total_legal_outcome_count - 4U);
    zero_values_into(updating_player, values);
    const std::size_t worker_count = parallel_workers_.size();
    const auto board_card_count = std::popcount(board.mask);
    // Flop representatives are always large enough to amortize dispatch.  On
    // the turn, split river representatives only when the shared queue has
    // drained and pool workers would otherwise sit idle.  This targets the
    // long tail without recursively flooding the queue at every river node.
    const std::size_t idle_river_workers =
        board_card_count == 4U ? idle_workers_if_queue_empty() : 0U;
    const std::size_t parallel_worker_budget =
        board_card_count == 3U ? worker_count : idle_river_workers;
    // The builder allocates every representative subtree independently, so
    // their action-state intervals are disjoint. Value buffers are
    // value-initialized above: PlayerIndexed terminal kernels intentionally
    // write only live combo slots, and workers must not expose stale blocked
    // slots when their results are transformed back to the parent.
    if ((board_card_count == 3U ||
         (board_card_count == 4U && idle_river_workers > 0U)) &&
        parallel_worker_budget > 0U && canonical.edges.size() > 1U) {
      const auto prepare_started = profile ? std::chrono::steady_clock::now()
                                           : std::chrono::steady_clock::time_point{};
      const auto edge_count = canonical.edges.size();
      auto representative_reaches =
          std::make_unique_for_overwrite<std::array<ComboVector, 2>[]>(edge_count);
      auto chance_results = std::make_unique<ComboVector[]>(edge_count);
      for (std::size_t index = 0U; index < edge_count; ++index) {
        const auto &edge = canonical.edges[index];
        if (edge.outcomes.empty()) {
          return PostflopSolverError::InvalidConfiguration;
        }
        representative_reaches[index] = transform_reach(
            {*reach[0], *reach[1]},
            edge.outcomes.front().physical_to_child_automorphism);
      }
      if (profile) {
        const auto elapsed = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - prepare_started)
                                 .count();
        prof_chance_prepare_seconds_ += elapsed;
        prof_chance_seconds_ += elapsed;
      }
      const std::size_t chunk_count =
          std::min(edge_count, parallel_worker_budget + 1U);
      // Reserve one representative for the caller before waking the pool.
      // Without this reservation the seven persistent workers can claim the
      // whole dynamic queue while the caller is still dispatching tasks,
      // leaving one of the configured eight solver threads idle.
      std::atomic<std::size_t> next_edge{1U};
      std::vector<std::packaged_task<TraversalResult(DenseTraversal &)>> tasks;
      std::vector<std::future<TraversalResult>> futures;
      tasks.reserve(chunk_count - 1U);
      futures.reserve(chunk_count - 1U);
      for (std::size_t chunk = 1U; chunk < chunk_count; ++chunk) {
        std::packaged_task<TraversalResult(DenseTraversal &)> task(
            [edges = &canonical.edges, reaches = representative_reaches.get(),
             results = chance_results.get(), next = &next_edge, edge_count,
             updating_player,
             strategy_weight](DenseTraversal &self) {
              while (true) {
                const std::size_t index =
                    next->fetch_add(1U, std::memory_order_relaxed);
                if (index >= edge_count) {
                  break;
                }
                if (const auto error = self.cfr_canonical_chance_child_into(
                        (*edges)[index].outcomes.front().child,
                        updating_player,
                        {&reaches[index][0], &reaches[index][1]},
                        strategy_weight, results[index])) {
                  return Result<ComboVector, PostflopSolverError>::failure(*error);
                }
              }
              return Result<ComboVector, PostflopSolverError>::success(ComboVector{});
            });
        futures.push_back(task.get_future());
        tasks.push_back(std::move(task));
      }
      dispatch_parallel_tasks(tasks);
      std::optional<PostflopSolverError> main_error;
      main_error = cfr_canonical_chance_child_into(
          canonical.edges[0U].outcomes.front().child, updating_player,
          {&representative_reaches[0U][0], &representative_reaches[0U][1]},
          strategy_weight, chance_results[0U]);
      while (!main_error) {
        const std::size_t index =
            next_edge.fetch_add(1U, std::memory_order_relaxed);
        if (index >= edge_count) {
          break;
        }
        main_error = cfr_canonical_chance_child_into(
            canonical.edges[index].outcomes.front().child, updating_player,
            {&representative_reaches[index][0], &representative_reaches[index][1]},
            strategy_weight, chance_results[index]);
      }
      std::optional<PostflopSolverError> worker_error;
      for (auto &future : futures) {
        wait_for_parallel_future(future);
        auto child = future.get();
        if (!child && !worker_error) {
          worker_error = child.error();
        }
      }
      if (main_error || worker_error) {
        return main_error ? *main_error : *worker_error;
      }
      const auto accumulate_started = profile ? std::chrono::steady_clock::now()
                                              : std::chrono::steady_clock::time_point{};
      if (board_card_count == 4U) {
        // Preserve the original turn-to-river accumulation order exactly.
        // Only independent child traversals are parallelized; their values are
        // folded into the parent in canonical edge/outcome order.
        for (std::size_t index = 0U; index < edge_count; ++index) {
          for (const auto &outcome : canonical.edges[index].outcomes) {
            const double probability =
                static_cast<double>(outcome.physical_outcome_count) /
                denominator;
            accumulate_transformed_values_to_parent(
                values, chance_results[index],
                outcome.physical_to_child_automorphism, updating_player, board,
                outcome.chance_card, probability);
          }
        }
        if (profile) {
          const auto elapsed = std::chrono::duration<double>(
                                   std::chrono::steady_clock::now() -
                                   accumulate_started)
                                   .count();
          prof_chance_accumulate_seconds_ += elapsed;
          prof_chance_seconds_ += elapsed;
        }
        return std::nullopt;
      }
      std::vector<std::packaged_task<TraversalResult(DenseTraversal &)>> accumulation_tasks;
      std::vector<std::future<TraversalResult>> accumulation_futures;
      accumulation_tasks.reserve(chunk_count - 1U);
      accumulation_futures.reserve(chunk_count - 1U);
      for (std::size_t chunk = 1U; chunk < chunk_count; ++chunk) {
        const std::size_t begin = chunk * edge_count / chunk_count;
        const std::size_t end = (chunk + 1U) * edge_count / chunk_count;
        std::packaged_task<TraversalResult(DenseTraversal &)> task(
            [edges = &canonical.edges, results = chance_results.get(),
             board_ptr = &board, begin, end, updating_player,
             denominator](DenseTraversal &self) {
              ComboVector partial{};
              for (std::size_t index = begin; index < end; ++index) {
                for (const auto &outcome : (*edges)[index].outcomes) {
                  const double probability =
                      static_cast<double>(outcome.physical_outcome_count) /
                      denominator;
                  self.accumulate_transformed_values_to_parent(
                      partial, results[index],
                      outcome.physical_to_child_automorphism,
                      updating_player, *board_ptr, outcome.chance_card,
                      probability);
                }
              }
              return Result<ComboVector, PostflopSolverError>::success(
                  std::move(partial));
            });
        accumulation_futures.push_back(task.get_future());
        accumulation_tasks.push_back(std::move(task));
      }
      dispatch_parallel_tasks(accumulation_tasks);
      const std::size_t main_accumulation_end = edge_count / chunk_count;
      for (std::size_t index = 0U; index < main_accumulation_end; ++index) {
        const auto &edge = canonical.edges[index];
        for (const auto &outcome : edge.outcomes) {
          const double probability =
              static_cast<double>(outcome.physical_outcome_count) / denominator;
          accumulate_transformed_values_to_parent(
              values, chance_results[index],
              outcome.physical_to_child_automorphism, updating_player, board,
              outcome.chance_card, probability);
        }
      }
      for (auto &future : accumulation_futures) {
        wait_for_parallel_future(future);
        auto partial = future.get();
        if (!partial) {
          return partial.error();
        }
        const std::size_t slot_count =
            layout_.player_flop_count[updating_player];
        std::size_t slot = 0U;
        if constexpr (std::is_same_v<Scalar, float>) {
          for (; slot + 8U <= slot_count; slot += 8U) {
            _mm256_storeu_ps(
                values.data() + slot,
                _mm256_add_ps(_mm256_loadu_ps(values.data() + slot),
                              _mm256_loadu_ps(partial.value().data() + slot)));
          }
        }
        for (; slot < slot_count; ++slot) {
          values[slot] = static_cast<Scalar>(values[slot] + partial.value()[slot]);
        }
      }
      if (profile) {
        const auto elapsed = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - accumulate_started)
                                 .count();
        prof_chance_accumulate_seconds_ += elapsed;
        prof_chance_seconds_ += elapsed;
      }
      return std::nullopt;
    }
    for (const auto &edge : canonical.edges) {
      if (edge.outcomes.empty()) {
        return PostflopSolverError::InvalidConfiguration;
      }
      const auto &representative = edge.outcomes.front();
      const auto prepare_started = profile ? std::chrono::steady_clock::now()
                                           : std::chrono::steady_clock::time_point{};
      auto representative_reach = transform_reach(
          {*reach[0], *reach[1]}, representative.physical_to_child_automorphism);
      if (profile) {
        const auto elapsed = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - prepare_started)
                                 .count();
        prof_chance_prepare_seconds_ += elapsed;
        prof_chance_seconds_ += elapsed;
      }
      ComboVector child{};
      if (const auto error = cfr_canonical_chance_child_into(
              representative.child, updating_player,
              {&representative_reach[0], &representative_reach[1]},
              strategy_weight, child)) {
        return error;
      }
      const auto accumulate_started = profile ? std::chrono::steady_clock::now()
                                              : std::chrono::steady_clock::time_point{};
      for (const auto &outcome : edge.outcomes) {
        const double probability =
            static_cast<double>(outcome.physical_outcome_count) / denominator;
        accumulate_transformed_values_to_parent(
            values, child, outcome.physical_to_child_automorphism,
            updating_player, board, outcome.chance_card, probability);
      }
      if (profile) {
        const auto elapsed = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - accumulate_started)
                                 .count();
        prof_chance_accumulate_seconds_ += elapsed;
        prof_chance_seconds_ += elapsed;
      }
    }
    return std::nullopt;
  }

  std::optional<PostflopSolverError> cfr_canonical_decision_into(
      const CanonicalPublicNode &canonical, const std::uint8_t updating_player,
      const ReachRef &reach, const double strategy_weight,
      ComboVector &values) {
    const bool profile = hotpath_profiling_enabled();
    const auto strategy_started = profile ? std::chrono::steady_clock::now()
                                          : std::chrono::steady_clock::time_point{};
    if (profile) {
      ++prof_decisions_;
    }
    const auto &decision = canonical.decision;
    const auto &board = layout_.boards[decision.board_index];
    const auto action_count = static_cast<std::size_t>(decision.action_count);
    if (canonical.edges.size() != action_count) {
      return PostflopSolverError::InvalidConfiguration;
    }
    if (decision.player == updating_player) {
      const auto entries = static_cast<std::uint64_t>(
          board.player_combos[updating_player].size() * action_count);
      if (!is_locked_root(canonical)) {
        work_counters_.regret_update_entries += entries;
      }
      if (strategy_weight != 0.0) {
        work_counters_.strategy_update_entries += entries;
      }
    }
    DecisionScratchLease scratch_lease(*this);
    auto &action_values = scratch_lease.get().action_values;
    auto &strategies = scratch_lease.get().strategies;
    auto &actor_reach = scratch_lease.get().reach_actor[0];
    const bool locked_root = is_locked_root(canonical);
    load_canonical_current_strategies(canonical, board, updating_player, strategies,
                                      true);
    if (profile) {
      prof_strategy_seconds_ +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        strategy_started)
              .count();
      prof_strategy_entries_ += board.player_combos[decision.player].size() *
                                action_count;
    }
    const auto paired_fold_action =
        static_cast<std::size_t>(decision.paired_fold_action);
    const auto paired_showdown_action =
        static_cast<std::size_t>(decision.paired_showdown_action);
    const auto &actor_combos = board.player_combos[decision.player];
    const auto &actor_slots =
        PlayerIndexed ? board.player_flop_slots[decision.player]
                      : board.player_active_slots[decision.player];
    const auto materialize_local_actor_reach =
        [&](const std::size_t action, ComboVector &destination) {
          const auto &parent_reach = *reach[decision.player];
          bool any_nonzero = false;
          std::size_t actor_local = 0U;
          if constexpr (std::is_same_v<Scalar, float>) {
            alignas(32) float child_lanes_float[8];
            for (; actor_local + 8U <= actor_slots.size(); actor_local += 8U) {
              const __m256i indices = _mm256_cvtepu16_epi32(_mm_loadu_si128(
                  reinterpret_cast<const __m128i *>(actor_slots.data() + actor_local)));
              const __m256 children = _mm256_mul_ps(
                  _mm256_i32gather_ps(parent_reach.data(), indices, 4),
                  _mm256_loadu_ps(strategies[action].data() + actor_local));
              _mm256_store_ps(child_lanes_float, children);
              for (std::size_t lane = 0U; lane < 8U; ++lane) {
                destination[actor_slots[actor_local + lane]] =
                    child_lanes_float[lane];
                any_nonzero = any_nonzero || child_lanes_float[lane] != 0.0F;
              }
            }
          }
          alignas(32) double child_lanes[4];
          for (; actor_local + 4U <= actor_slots.size(); actor_local += 4U) {
            const __m128i indices = _mm_cvtepu16_epi32(_mm_loadl_epi64(
                reinterpret_cast<const __m128i *>(actor_slots.data() + actor_local)));
            const __m256d children = _mm256_mul_pd(
                gather_four_as_double(parent_reach.data(), indices),
                load_four_as_double(strategies[action].data() + actor_local));
            _mm256_store_pd(child_lanes, children);
            for (std::size_t lane = 0U; lane < 4U; ++lane) {
              destination[actor_slots[actor_local + lane]] =
                  static_cast<Scalar>(child_lanes[lane]);
              any_nonzero = any_nonzero || child_lanes[lane] != 0.0;
            }
          }
          for (; actor_local < actor_combos.size(); ++actor_local) {
            const auto slot = value_slot(actor_combos[actor_local], decision.player);
            destination[slot] = static_cast<Scalar>(
                parent_reach[slot] * strategies[action][actor_local]);
            any_nonzero = any_nonzero || destination[slot] != 0.0;
          }
          if (profile) {
            prof_actor_writes_ += actor_combos.size();
          }
          return any_nonzero;
        };
    for (std::size_t action = 0; action < action_count; ++action) {
      if (action == paired_fold_action) {
        // The paired showdown branch below computes both terminal vectors in
        // one rank/card pass.
        continue;
      }
      if (canonical.edges[action].outcomes.size() != 1U) {
        return PostflopSolverError::InvalidConfiguration;
      }
      const auto &outcome = canonical.edges[action].outcomes.front();
      if (!identity_automorphism(outcome.physical_to_child_automorphism)) {
        return PostflopSolverError::InvalidConfiguration;
      }
      const auto copy_started = profile ? std::chrono::steady_clock::now()
                                        : std::chrono::steady_clock::time_point{};
      const auto child_node_id = outcome.child;
      const auto &child_node = layout_.canonical_public_graph.nodes[child_node_id];
      const bool terminal_child = child_node.kind == PublicNodeKind::TerminalFold ||
                                  child_node.kind == PublicNodeKind::TerminalShowdown;
      const bool actor_needed =
          decision.player != updating_player ||
          (!terminal_child && strategy_weight != 0.0);
      const bool actor_reach_nonzero =
          actor_needed ? materialize_local_actor_reach(action, actor_reach) : true;
      const ReachRef child_reach =
          actor_needed
              ? (decision.player == 0U ? ReachRef{&actor_reach, reach[1]}
                                       : ReachRef{reach[0], &actor_reach})
              : reach;
      if (profile) {
        prof_copy_seconds_ +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          copy_started)
                .count();
      }
      if (action == paired_showdown_action && paired_fold_action < action_count) {
        const auto &fold_outcome =
            canonical.edges[paired_fold_action].outcomes.front();
        const auto &fold_node =
            layout_.canonical_public_graph.nodes[fold_outcome.child];
        if (child_node.kind != PublicNodeKind::TerminalShowdown ||
            fold_node.kind != PublicNodeKind::TerminalFold ||
            child_node.board_index != fold_node.board_index) {
          return PostflopSolverError::InvalidConfiguration;
        }
        ComboVector &fold_actor_reach = scratch_lease.get().reach_actor[1];
        const ComboVector *fold_opponent_reach = reach[1U - updating_player];
        bool fold_reach_nonzero = true;
        if (decision.player != updating_player) {
          fold_reach_nonzero = materialize_local_actor_reach(
              paired_fold_action, fold_actor_reach);
          fold_opponent_reach = &fold_actor_reach;
        }
        if (!actor_reach_nonzero) {
          zero_values_into(updating_player, action_values[action]);
        }
        if (!fold_reach_nonzero) {
          zero_values_into(updating_player, action_values[paired_fold_action]);
        }
        if (actor_reach_nonzero && fold_reach_nonzero) {
          const auto &showdown_payoff =
              child_node.showdown_payoff_antes[updating_player];
          if (const auto error = showdown_values_with_payoffs_into<false, true>(
                  child_node.board_index, showdown_payoff[0], showdown_payoff[1],
                  showdown_payoff[2], updating_player,
                  *child_reach[1U - updating_player], action_values[action],
                  fold_node.fold_payoff_antes[updating_player],
                  &action_values[paired_fold_action], fold_opponent_reach)) {
            return error;
          }
        } else if (actor_reach_nonzero) {
          if (const auto error = cfr_canonical_into(
                  child_node_id, updating_player, child_reach, strategy_weight,
                  action_values[action])) {
            return error;
          }
        } else if (fold_reach_nonzero) {
          if (const auto error = fold_values_with_payoff_into(
                  fold_node.board_index,
                  fold_node.fold_payoff_antes[updating_player],
                  updating_player, *fold_opponent_reach,
                  action_values[paired_fold_action])) {
            return error;
          }
        }
        traversed_nodes_ += 2U;
        continue;
      }
      if (decision.player != updating_player && !actor_reach_nonzero) {
        // Standard partial pruning: counterfactual values and regret deltas in
        // this subtree are identically zero. The skipped average/discount step
        // changes only unreachable finite-iteration state; exact best-response
        // certification remains the acceptance authority.
        zero_values_into(updating_player, action_values[action]);
        continue;
      }
      if (const auto error = cfr_canonical_into(
              outcome.child, updating_player, child_reach, strategy_weight,
              action_values[action])) {
        return error;
      }
    }

    const auto value_started = profile ? std::chrono::steady_clock::now()
                                       : std::chrono::steady_clock::time_point{};
    zero_values_into(updating_player, values);
    const auto &updating_combos = board.player_combos[updating_player];
    std::size_t updating_local = 0U;
    std::size_t averaging_fused_until = 0U;
    if constexpr (PlayerIndexed && std::is_same_v<Scalar, float>) {
      if (decision.player != updating_player) {
        // Child values use the updating player's flop-slot prefix. Blocked
        // hands are zero by construction, so summing the complete prefix is
        // exact and avoids a gather plus scalar scatter for every live hand.
        const std::size_t slot_count = layout_.player_flop_count[updating_player];
        std::size_t slot = 0U;
        for (; slot + 8U <= slot_count; slot += 8U) {
          __m256 current = _mm256_setzero_ps();
          for (std::size_t action = 0U; action < action_count; ++action) {
            current = _mm256_add_ps(
                current, _mm256_loadu_ps(action_values[action].data() + slot));
          }
          _mm256_storeu_ps(values.data() + slot, current);
        }
        for (; slot < slot_count; ++slot) {
          Scalar current = Scalar{0};
          for (std::size_t action = 0U; action < action_count; ++action) {
            current = static_cast<Scalar>(current + action_values[action][slot]);
          }
          values[slot] = current;
        }
        updating_local = updating_combos.size();
      }
    }
    if (aligned_compact_state()) {
      const auto &slots = PlayerIndexed
                              ? board.player_flop_slots[updating_player]
                              : board.player_active_slots[updating_player];
      if constexpr (std::is_same_v<Scalar, float>) {
        alignas(32) float current_lanes[8];
        alignas(32) float action_lanes[maximum_action_count][8];
        for (; updating_local + 8U <= slots.size(); updating_local += 8U) {
          const __m256i indices = _mm256_cvtepu16_epi32(_mm_loadu_si128(
              reinterpret_cast<const __m128i *>(slots.data() + updating_local)));
          __m256 current = _mm256_setzero_ps();
          for (std::size_t action = 0U; action < action_count; ++action) {
            const __m256 action_vector =
                _mm256_i32gather_ps(action_values[action].data(), indices, 4);
            _mm256_store_ps(action_lanes[action], action_vector);
            current = decision.player == updating_player
                          ? _mm256_add_ps(
                                current,
                                _mm256_mul_ps(
                                    _mm256_loadu_ps(strategies[action].data() +
                                                    updating_local),
                                    action_vector))
                          : _mm256_add_ps(current, action_vector);
          }
          _mm256_store_ps(current_lanes, current);
          for (std::size_t lane = 0U; lane < 8U; ++lane) {
            values[slots[updating_local + lane]] = current_lanes[lane];
          }
          if (decision.player == updating_player) {
            for (std::size_t lane = 0U; lane < 8U; ++lane) {
              const auto state_base =
                  static_cast<std::size_t>(decision.action_base) +
                  (updating_local + lane) * action_count;
              const double reach_weight =
                  strategy_weight * static_cast<double>((*reach[updating_player])
                      [slots[updating_local + lane]]);
              for (std::size_t action = 0U; action < action_count; ++action) {
                const auto index = state_base + action;
                if (!locked_root) {
                  buffers_.compact_regret16[index] = encode_regret13(std::max(
                      0.0,
                      decode_regret13(buffers_.compact_regret16[index]) +
                          regret_update_weight_ *
                              static_cast<double>(action_lanes[action][lane] -
                                                  current_lanes[lane])));
                }
                if (reach_weight != 0.0) {
                  buffers_.compact_strategy16[index] = encode_strategy11(
                      decode_strategy11(buffers_.compact_strategy16[index]) +
                      reach_weight * static_cast<double>(
                                         strategies[action]
                                                   [updating_local + lane]));
                }
              }
            }
          }
        }
      }
      for (; updating_local < updating_combos.size(); ++updating_local) {
        const auto slot = static_cast<std::size_t>(slots[updating_local]);
        if (decision.player == updating_player) {
          for (std::size_t action = 0U; action < action_count; ++action) {
            values[slot] = static_cast<Scalar>(
                values[slot] +
                strategies[action][updating_local] * action_values[action][slot]);
          }
          const auto state_base =
              static_cast<std::size_t>(decision.action_base) +
              updating_local * action_count;
          const double reach_weight =
              strategy_weight * static_cast<double>((*reach[updating_player])[slot]);
          for (std::size_t action = 0U; action < action_count; ++action) {
            const auto index = state_base + action;
            if (!locked_root) {
              buffers_.compact_regret16[index] = encode_regret13(std::max(
                  0.0,
                  decode_regret13(buffers_.compact_regret16[index]) +
                      regret_update_weight_ *
                          (static_cast<double>(action_values[action][slot]) -
                           static_cast<double>(values[slot]))));
            }
            if (reach_weight != 0.0) {
              buffers_.compact_strategy16[index] = encode_strategy11(
                  decode_strategy11(buffers_.compact_strategy16[index]) +
                  reach_weight * static_cast<double>(
                                     strategies[action][updating_local]));
            }
          }
        } else {
          for (std::size_t action = 0U; action < action_count; ++action) {
            values[slot] = static_cast<Scalar>(
                values[slot] + action_values[action][slot]);
          }
        }
      }
      if (profile) {
        prof_value_update_seconds_ +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          value_started)
                .count();
      }
      return std::nullopt;
    }
    if (action_major_compact_state()) {
      if (decision.player == updating_player) {
        for (std::size_t local = 0; local < updating_combos.size(); ++local) {
          const auto slot = value_slot(updating_combos[local], updating_player);
          Scalar current = Scalar{0};
          for (std::size_t action = 0; action < action_count; ++action) {
            current = static_cast<Scalar>(
                current + strategies[action][local] * action_values[action][slot]);
          }
          values[slot] = current;
        }
        update_action_major_compact_average(
            canonical, board, updating_player, reach, strategy_weight,
            strategies, true);
        if (!locked_root) {
          for (std::size_t action = 0; action < action_count; ++action) {
            for (std::size_t local = 0; local < updating_combos.size(); ++local) {
              const auto slot = value_slot(updating_combos[local], updating_player);
              auto *const bytes = buffers_.compact_state +
                  canonical_action_major_index(canonical, local, action) * 3U;
              const auto word = compact_word(bytes);
              const double updated = std::max(
                  0.0,
                  decode_regret13(static_cast<std::uint16_t>(word & 0x1fffU)) +
                      regret_update_weight_ *
                          (static_cast<double>(action_values[action][slot]) -
                           static_cast<double>(values[slot])));
              store_compact_word(
                  bytes, (word & 0x00ffe000U) | encode_regret13(updated));
            }
          }
        }
      } else {
        for (const ComboId combo : updating_combos) {
          const auto slot = value_slot(combo, updating_player);
          Scalar current = Scalar{0};
          for (std::size_t action = 0; action < action_count; ++action) {
            current = static_cast<Scalar>(current + action_values[action][slot]);
          }
          values[slot] = current;
        }
      }
      if (profile) {
        prof_value_update_seconds_ +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          value_started)
                .count();
      }
      return std::nullopt;
    }
    if (scaled_action_major_state()) {
      if (decision.player == updating_player) {
        const auto scaled_value_started =
            profile ? std::chrono::steady_clock::now()
                    : std::chrono::steady_clock::time_point{};
        if (buffers_.signed_scaled_regret && !locked_root) {
          const auto scaled_regret_started =
              profile ? std::chrono::steady_clock::now()
                      : std::chrono::steady_clock::time_point{};
          auto &average_scratch = scratch_lease.get().opponent_action_values;
          update_scaled_regrets(canonical, board, updating_player, values,
                                action_values, strategies, &average_scratch,
                                &reach, true);
          if (profile) {
            prof_regret_update_seconds_ +=
                std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                              scaled_regret_started)
                    .count();
          }
        } else {
          for (std::size_t local = 0; local < updating_combos.size(); ++local) {
            const auto slot = value_slot(updating_combos[local], updating_player);
            Scalar current = Scalar{0};
            for (std::size_t action = 0; action < action_count; ++action) {
              current = static_cast<Scalar>(
                  current + strategies[action][local] * action_values[action][slot]);
            }
            values[slot] = current;
          }
          const auto scaled_regret_started =
              profile ? std::chrono::steady_clock::now()
                      : std::chrono::steady_clock::time_point{};
          if (profile) {
            prof_value_accumulate_seconds_ +=
                std::chrono::duration<double>(scaled_regret_started -
                                              scaled_value_started)
                    .count();
          }
          update_scaled_average(canonical, board, updating_player, reach,
                                strategy_weight, strategies, true);
          if (!locked_root) {
            update_scaled_regrets(canonical, board, updating_player, values,
                                  action_values, strategies);
          }
          if (profile) {
            prof_regret_update_seconds_ +=
                std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                              scaled_regret_started)
                    .count();
          }
        }
      } else {
        for (const ComboId combo : updating_combos) {
          const auto slot = value_slot(combo, updating_player);
          Scalar current = Scalar{0};
          for (std::size_t action = 0; action < action_count; ++action) {
            current = static_cast<Scalar>(current + action_values[action][slot]);
          }
          values[slot] = current;
        }
      }
      if (profile) {
        prof_value_update_seconds_ +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          value_started)
                .count();
      }
      return std::nullopt;
    }
    {
      if constexpr (std::is_same_v<Scalar, float>) {
        if (action_count >= 4U && buffers_.compact_state != nullptr) {
          const auto &slots = PlayerIndexed
                                  ? board.player_flop_slots[updating_player]
                                  : board.player_active_slots[updating_player];
          alignas(32) float current_lanes[8];
          alignas(32) float action_lanes[maximum_action_count][8];
          for (; updating_local + 8U <= slots.size(); updating_local += 8U) {
            const __m256i indices = _mm256_cvtepu16_epi32(_mm_loadu_si128(
                reinterpret_cast<const __m128i *>(slots.data() + updating_local)));
            __m256 current = _mm256_setzero_ps();
            for (std::size_t action = 0U; action < action_count; ++action) {
              const __m256 action_vector =
                  _mm256_i32gather_ps(action_values[action].data(), indices, 4);
              _mm256_store_ps(action_lanes[action], action_vector);
              if (decision.player == updating_player) {
                current = _mm256_add_ps(
                    current,
                    _mm256_mul_ps(
                        _mm256_loadu_ps(strategies[action].data() + updating_local),
                        action_vector));
              } else {
                current = _mm256_add_ps(current, action_vector);
              }
            }
            _mm256_store_ps(current_lanes, current);
            for (std::size_t lane = 0U; lane < 8U; ++lane) {
              values[slots[updating_local + lane]] = current_lanes[lane];
            }
            if (decision.player == updating_player && !locked_root) {
              for (std::size_t lane = 0U; lane < 8U; ++lane) {
                auto *const block =
                    buffers_.compact_state +
                    (static_cast<std::size_t>(decision.action_base) +
                     (updating_local + lane) * action_count) *
                        3U;
                for (std::size_t action = 0U; action < action_count; ++action) {
                  auto *const bytes = block + action * 3U;
                  const auto word = compact_word(bytes);
                  const double updated = std::max(
                      0.0,
                      decode_regret13(
                          static_cast<std::uint16_t>(word & 0x1fffU)) +
                          regret_update_weight_ *
                              static_cast<double>(action_lanes[action][lane] -
                                                  current_lanes[lane]));
                  const double strategy_delta =
                      strategy_weight == 0.0
                          ? 0.0
                          : strategy_weight *
                                static_cast<double>((*reach[updating_player])
                                                        [slots[updating_local + lane]]) *
                                static_cast<double>(
                                    strategies[action][updating_local + lane]);
                  const auto average =
                      strategy_delta == 0.0
                          ? static_cast<std::uint16_t>(word >> 13U)
                          : encode_strategy11(
                                decode_strategy11(static_cast<std::uint16_t>(
                                    word >> 13U)) +
                                strategy_delta);
                  store_compact_word(
                      bytes,
                      static_cast<std::uint32_t>(encode_regret13(updated)) |
                          (static_cast<std::uint32_t>(average) << 13U));
                }
              }
            }
          }
          if (decision.player == updating_player && !locked_root &&
              strategy_weight != 0.0) {
            averaging_fused_until = updating_local;
          }
        }
      }
#if 0
      // Rejected aligned four-byte/action A/B kernel. Kept excluded until the
      // compact-state cleanup below removes the experiment in one mechanical
      // pass; compiling it bloats the canonical decision hot path.
      if constexpr (std::is_same_v<Scalar, float>) {
        if (action_count == 2U &&
            buffers_.aligned_compact_state != nullptr) {
          const auto &slots = PlayerIndexed
                                  ? board.player_flop_slots[updating_player]
                                  : board.player_active_slots[updating_player];
          const __m256 zero = _mm256_setzero_ps();
          const __m256 regret_weight =
              _mm256_set1_ps(static_cast<float>(regret_update_weight_));
          const __m256i low_mask = _mm256_set1_epi32(0x0000ffff);
          const __m256i high_mask = _mm256_set1_epi32(0xffff0000);
          const __m256i even_lanes =
              _mm256_setr_epi32(0, 2, 4, 6, 0, 2, 4, 6);
          const __m256i odd_lanes =
              _mm256_setr_epi32(1, 3, 5, 7, 1, 3, 5, 7);
          const auto encode_regrets = [](const __m256 input) {
            const __m256i bits = _mm256_and_si256(
                _mm256_castps_si256(input), _mm256_set1_epi32(0x7fffffff));
            const __m256i discarded =
                _mm256_and_si256(bits, _mm256_set1_epi32(0x3ffff));
            __m256i packed = _mm256_srli_epi32(bits, 18);
            const __m256i greater =
                _mm256_cmpgt_epi32(discarded, _mm256_set1_epi32(0x20000));
            const __m256i equal =
                _mm256_cmpeq_epi32(discarded, _mm256_set1_epi32(0x20000));
            const __m256i odd = _mm256_cmpeq_epi32(
                _mm256_and_si256(packed, _mm256_set1_epi32(1)),
                _mm256_set1_epi32(1));
            packed = _mm256_add_epi32(
                packed,
                _mm256_and_si256(
                    _mm256_or_si256(greater, _mm256_and_si256(equal, odd)),
                    _mm256_set1_epi32(1)));
            return _mm256_min_epu32(packed, _mm256_set1_epi32(0x1ffe));
          };
          for (; updating_local + 8U <= slots.size(); updating_local += 8U) {
            const __m256i indices = _mm256_cvtepu16_epi32(_mm_loadu_si128(
                reinterpret_cast<const __m128i *>(slots.data() + updating_local)));
            const __m256 first_action =
                _mm256_i32gather_ps(action_values[0].data(), indices, 4);
            const __m256 second_action =
                _mm256_i32gather_ps(action_values[1].data(), indices, 4);
            const __m256 current =
                decision.player == updating_player
                    ? _mm256_add_ps(
                          _mm256_mul_ps(
                              _mm256_loadu_ps(strategies[0].data() + updating_local),
                              first_action),
                          _mm256_mul_ps(
                              _mm256_loadu_ps(strategies[1].data() + updating_local),
                              second_action))
                    : _mm256_add_ps(first_action, second_action);
            alignas(32) float current_lanes[8];
            _mm256_store_ps(current_lanes, current);
            for (std::size_t lane = 0U; lane < 8U; ++lane) {
              values[slots[updating_local + lane]] = current_lanes[lane];
            }
            if (decision.player != updating_player || locked_root) {
              continue;
            }
            auto *const records = reinterpret_cast<std::uint32_t *>(
                buffers_.aligned_compact_state +
                (static_cast<std::size_t>(decision.action_base) +
                 updating_local * 2U) *
                    4U);
            const __m256i low_records = _mm256_loadu_si256(
                reinterpret_cast<const __m256i *>(records));
            const __m256i high_records = _mm256_loadu_si256(
                reinterpret_cast<const __m256i *>(records + 8U));
            const __m256 low_regrets = _mm256_castsi256_ps(_mm256_slli_epi32(
                _mm256_and_si256(low_records, _mm256_set1_epi32(0x1fff)), 18));
            const __m256 high_regrets = _mm256_castsi256_ps(_mm256_slli_epi32(
                _mm256_and_si256(high_records, _mm256_set1_epi32(0x1fff)), 18));
            const __m128 first_low = _mm256_castps256_ps128(
                _mm256_permutevar8x32_ps(low_regrets, even_lanes));
            const __m128 first_high = _mm256_castps256_ps128(
                _mm256_permutevar8x32_ps(high_regrets, even_lanes));
            const __m128 second_low = _mm256_castps256_ps128(
                _mm256_permutevar8x32_ps(low_regrets, odd_lanes));
            const __m128 second_high = _mm256_castps256_ps128(
                _mm256_permutevar8x32_ps(high_regrets, odd_lanes));
            const __m256 first_regrets = _mm256_insertf128_ps(
                _mm256_castps128_ps256(first_low), first_high, 1);
            const __m256 second_regrets = _mm256_insertf128_ps(
                _mm256_castps128_ps256(second_low), second_high, 1);
            const __m256i encoded_first = encode_regrets(_mm256_max_ps(
                zero, _mm256_add_ps(
                          first_regrets,
                          _mm256_mul_ps(regret_weight,
                                        _mm256_sub_ps(first_action, current)))));
            const __m256i encoded_second = encode_regrets(_mm256_max_ps(
                zero, _mm256_add_ps(
                          second_regrets,
                          _mm256_mul_ps(regret_weight,
                                        _mm256_sub_ps(second_action, current)))));
            const std::array<__m128i, 2> first_halves{
                _mm256_castsi256_si128(encoded_first),
                _mm256_extracti128_si256(encoded_first, 1)};
            const std::array<__m128i, 2> second_halves{
                _mm256_castsi256_si128(encoded_second),
                _mm256_extracti128_si256(encoded_second, 1)};
            for (std::size_t half = 0U; half < 2U; ++half) {
              const __m256i interleaved = _mm256_inserti128_si256(
                  _mm256_castsi128_si256(_mm_unpacklo_epi32(
                      first_halves[half], second_halves[half])),
                  _mm_unpackhi_epi32(first_halves[half], second_halves[half]), 1);
              const __m256i old_records = half == 0U ? low_records : high_records;
              _mm256_storeu_si256(
                  reinterpret_cast<__m256i *>(records + half * 8U),
                  _mm256_or_si256(
                      _mm256_and_si256(old_records, high_mask),
                      _mm256_and_si256(interleaved, low_mask)));
            }
          }
        }
        if (action_count >= 3U &&
            buffers_.aligned_compact_state != nullptr) {
          const auto &slots = PlayerIndexed
                                  ? board.player_flop_slots[updating_player]
                                  : board.player_active_slots[updating_player];
          const __m256 zero = _mm256_setzero_ps();
          const __m256 regret_weight =
              _mm256_set1_ps(static_cast<float>(regret_update_weight_));
          const __m256i regret_mask = _mm256_set1_epi32(0x1fff);
          const __m256i record_indices = _mm256_setr_epi32(
              0, static_cast<int>(action_count),
              static_cast<int>(action_count * 2U),
              static_cast<int>(action_count * 3U),
              static_cast<int>(action_count * 4U),
              static_cast<int>(action_count * 5U),
              static_cast<int>(action_count * 6U),
              static_cast<int>(action_count * 7U));
          const auto encode_regrets = [](const __m256 input) {
            const __m256i bits = _mm256_and_si256(
                _mm256_castps_si256(input), _mm256_set1_epi32(0x7fffffff));
            const __m256i discarded =
                _mm256_and_si256(bits, _mm256_set1_epi32(0x3ffff));
            __m256i packed = _mm256_srli_epi32(bits, 18);
            const __m256i greater =
                _mm256_cmpgt_epi32(discarded, _mm256_set1_epi32(0x20000));
            const __m256i equal =
                _mm256_cmpeq_epi32(discarded, _mm256_set1_epi32(0x20000));
            const __m256i odd = _mm256_cmpeq_epi32(
                _mm256_and_si256(packed, _mm256_set1_epi32(1)),
                _mm256_set1_epi32(1));
            packed = _mm256_add_epi32(
                packed,
                _mm256_and_si256(
                    _mm256_or_si256(greater, _mm256_and_si256(equal, odd)),
                    _mm256_set1_epi32(1)));
            return _mm256_min_epu32(packed, _mm256_set1_epi32(0x1ffe));
          };
          for (; updating_local + 8U <= slots.size(); updating_local += 8U) {
            const __m256i value_indices = _mm256_cvtepu16_epi32(_mm_loadu_si128(
                reinterpret_cast<const __m128i *>(slots.data() + updating_local)));
            std::array<__m256, maximum_action_count> action_vectors{};
            __m256 current = _mm256_setzero_ps();
            for (std::size_t action = 0U; action < action_count; ++action) {
              action_vectors[action] =
                  _mm256_i32gather_ps(action_values[action].data(), value_indices, 4);
              current = decision.player == updating_player
                            ? _mm256_add_ps(
                                  current,
                                  _mm256_mul_ps(
                                      _mm256_loadu_ps(strategies[action].data() +
                                                      updating_local),
                                      action_vectors[action]))
                            : _mm256_add_ps(current, action_vectors[action]);
            }
            alignas(32) float current_lanes[8];
            _mm256_store_ps(current_lanes, current);
            for (std::size_t lane = 0U; lane < 8U; ++lane) {
              values[slots[updating_local + lane]] = current_lanes[lane];
            }
            if (decision.player != updating_player || locked_root) {
              continue;
            }
            auto *const records = reinterpret_cast<std::uint32_t *>(
                buffers_.aligned_compact_state +
                (static_cast<std::size_t>(decision.action_base) +
                 updating_local * action_count) *
                    4U);
            for (std::size_t action = 0U; action < action_count; ++action) {
              const __m256i old_records = _mm256_i32gather_epi32(
                  reinterpret_cast<const int *>(records + action),
                  record_indices, 4);
              const __m256 regrets = _mm256_castsi256_ps(_mm256_slli_epi32(
                  _mm256_and_si256(old_records, regret_mask), 18));
              const __m256i encoded = encode_regrets(_mm256_max_ps(
                  zero,
                  _mm256_add_ps(
                      regrets,
                      _mm256_mul_ps(
                          regret_weight,
                          _mm256_sub_ps(action_vectors[action], current)))));
              alignas(32) std::uint32_t encoded_lanes[8];
              _mm256_store_si256(reinterpret_cast<__m256i *>(encoded_lanes),
                                 encoded);
              for (std::size_t lane = 0U; lane < 8U; ++lane) {
                auto &record = records[lane * action_count + action];
                record = (record & 0xffff0000U) | encoded_lanes[lane];
              }
            }
          }
        }
      }
#endif
      if (action_count == 2U &&
          buffers_.compact_state != nullptr) {
        const auto &slots = PlayerIndexed
                                ? board.player_flop_slots[updating_player]
                                : board.player_active_slots[updating_player];
        const __m128i expand_words =
            _mm_setr_epi8(0, 1, 2, -1, 3, 4, 5, -1,
                          6, 7, 8, -1, 9, 10, 11, -1);
        const __m128i compact_words =
            _mm_setr_epi8(0, 1, 2, 4, 5, 6, 8, 9, 10,
                          12, 13, 14, -1, -1, -1, -1);
        const __m128i regret_mask = _mm_set1_epi32(0x1fff);
        const __m128i strategy_mask = _mm_set1_epi32(0x00ffe000);
        const __m256d zero = _mm256_setzero_pd();
        const __m256d regret_weight = _mm256_set1_pd(regret_update_weight_);
        const auto encode_four_regrets = [](const __m256d input) {
          const __m128i bits = _mm_and_si128(
              _mm_castps_si128(_mm256_cvtpd_ps(input)),
              _mm_set1_epi32(0x7fffffff));
          const __m128i discarded =
              _mm_and_si128(bits, _mm_set1_epi32(0x3ffff));
          __m128i packed = _mm_srli_epi32(bits, 18);
          const __m128i greater =
              _mm_cmpgt_epi32(discarded, _mm_set1_epi32(0x20000));
          const __m128i equal =
              _mm_cmpeq_epi32(discarded, _mm_set1_epi32(0x20000));
          const __m128i odd = _mm_cmpeq_epi32(
              _mm_and_si128(packed, _mm_set1_epi32(1)),
              _mm_set1_epi32(1));
          packed = _mm_add_epi32(
              packed,
              _mm_and_si128(_mm_or_si128(greater, _mm_and_si128(equal, odd)),
                            _mm_set1_epi32(1)));
          return _mm_min_epu32(packed, _mm_set1_epi32(0x1ffe));
        };
        const auto store_four_words = [compact_words](std::uint8_t *const destination,
                                                       const __m128i words) {
          const __m128i compact = _mm_shuffle_epi8(words, compact_words);
          _mm_storel_epi64(reinterpret_cast<__m128i *>(destination), compact);
          const std::uint32_t tail = static_cast<std::uint32_t>(
              _mm_cvtsi128_si32(_mm_srli_si128(compact, 8)));
          std::memcpy(destination + 8U, &tail, sizeof(tail));
        };
        alignas(32) double value_lanes[4];
        if constexpr (std::is_same_v<Scalar, float>) {
          if (decision.player == updating_player && !locked_root) {
            const __m256 zero_float = _mm256_setzero_ps();
            const __m256 regret_weight_float =
                _mm256_set1_ps(static_cast<float>(regret_update_weight_));
            const auto encode_eight_regrets = [](const __m256 input) {
              const __m256i bits = _mm256_and_si256(
                  _mm256_castps_si256(input), _mm256_set1_epi32(0x7fffffff));
              const __m256i discarded =
                  _mm256_and_si256(bits, _mm256_set1_epi32(0x3ffff));
              __m256i packed = _mm256_srli_epi32(bits, 18);
              const __m256i greater =
                  _mm256_cmpgt_epi32(discarded, _mm256_set1_epi32(0x20000));
              const __m256i equal =
                  _mm256_cmpeq_epi32(discarded, _mm256_set1_epi32(0x20000));
              const __m256i odd = _mm256_cmpeq_epi32(
                  _mm256_and_si256(packed, _mm256_set1_epi32(1)),
                  _mm256_set1_epi32(1));
              packed = _mm256_add_epi32(
                  packed,
                  _mm256_and_si256(
                      _mm256_or_si256(greater, _mm256_and_si256(equal, odd)),
                      _mm256_set1_epi32(1)));
              return _mm256_min_epu32(packed, _mm256_set1_epi32(0x1ffe));
            };
            for (; updating_local + 8U <= slots.size(); updating_local += 8U) {
              const __m256i indices = _mm256_cvtepu16_epi32(_mm_loadu_si128(
                  reinterpret_cast<const __m128i *>(slots.data() + updating_local)));
              const __m256 first_action =
                  _mm256_i32gather_ps(action_values[0].data(), indices, 4);
              const __m256 second_action =
                  _mm256_i32gather_ps(action_values[1].data(), indices, 4);
              const __m256 current = _mm256_add_ps(
                  _mm256_mul_ps(
                      _mm256_loadu_ps(strategies[0].data() + updating_local),
                      first_action),
                  _mm256_mul_ps(
                      _mm256_loadu_ps(strategies[1].data() + updating_local),
                      second_action));
              alignas(32) float current_lanes[8];
              _mm256_store_ps(current_lanes, current);
              for (std::size_t lane = 0U; lane < 8U; ++lane) {
                values[slots[updating_local + lane]] = current_lanes[lane];
              }

              auto *const packed =
                  buffers_.compact_state +
                  (static_cast<std::size_t>(decision.action_base) +
                   updating_local * 2U) *
                      3U;
              const std::array<__m128i, 4> old_words{
                  _mm_shuffle_epi8(
                      _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed)),
                      expand_words),
                  _mm_shuffle_epi8(
                      _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed + 12U)),
                      expand_words),
                  _mm_shuffle_epi8(
                      _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed + 24U)),
                      expand_words),
                  _mm_shuffle_epi8(
                      _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed + 36U)),
                      expand_words)};
              const auto decoded_regrets = [regret_mask](const __m128i low,
                                                          const __m128i high,
                                                          const int first_lane) {
                const __m128 low_regrets = _mm_castsi128_ps(
                    _mm_slli_epi32(_mm_and_si128(low, regret_mask), 18));
                const __m128 high_regrets = _mm_castsi128_ps(
                    _mm_slli_epi32(_mm_and_si128(high, regret_mask), 18));
                return first_lane == 0
                           ? _mm_shuffle_ps(low_regrets, high_regrets,
                                            _MM_SHUFFLE(2, 0, 2, 0))
                           : _mm_shuffle_ps(low_regrets, high_regrets,
                                            _MM_SHUFFLE(3, 1, 3, 1));
              };
              const __m128 first_low =
                  decoded_regrets(old_words[0], old_words[1], 0);
              const __m128 first_high =
                  decoded_regrets(old_words[2], old_words[3], 0);
              const __m128 second_low =
                  decoded_regrets(old_words[0], old_words[1], 1);
              const __m128 second_high =
                  decoded_regrets(old_words[2], old_words[3], 1);
              const __m256 first_regrets = _mm256_insertf128_ps(
                  _mm256_castps128_ps256(first_low), first_high, 1);
              const __m256 second_regrets = _mm256_insertf128_ps(
                  _mm256_castps128_ps256(second_low), second_high, 1);
              const __m256i encoded_first = encode_eight_regrets(
                  _mm256_max_ps(
                      zero_float,
                      _mm256_add_ps(
                          first_regrets,
                          _mm256_mul_ps(regret_weight_float,
                                        _mm256_sub_ps(first_action, current)))));
              const __m256i encoded_second = encode_eight_regrets(
                  _mm256_max_ps(
                      zero_float,
                      _mm256_add_ps(
                          second_regrets,
                          _mm256_mul_ps(regret_weight_float,
                                        _mm256_sub_ps(second_action, current)))));
              const std::array<__m128i, 2> first_halves{
                  _mm256_castsi256_si128(encoded_first),
                  _mm256_extracti128_si256(encoded_first, 1)};
              const std::array<__m128i, 2> second_halves{
                  _mm256_castsi256_si128(encoded_second),
                  _mm256_extracti128_si256(encoded_second, 1)};
              for (std::size_t half = 0U; half < 2U; ++half) {
                const auto old_index = half * 2U;
                store_four_words(
                    packed + half * 24U,
                    _mm_or_si128(
                        _mm_and_si128(old_words[old_index], strategy_mask),
                        _mm_unpacklo_epi32(first_halves[half],
                                           second_halves[half])));
                store_four_words(
                    packed + half * 24U + 12U,
                    _mm_or_si128(
                        _mm_and_si128(old_words[old_index + 1U], strategy_mask),
                        _mm_unpackhi_epi32(first_halves[half],
                                           second_halves[half])));
              }
            }
          } else if (decision.player != updating_player || locked_root) {
            for (; updating_local + 8U <= slots.size(); updating_local += 8U) {
              const __m256i indices = _mm256_cvtepu16_epi32(_mm_loadu_si128(
                  reinterpret_cast<const __m128i *>(slots.data() + updating_local)));
              const __m256 first_action =
                  _mm256_i32gather_ps(action_values[0].data(), indices, 4);
              const __m256 second_action =
                  _mm256_i32gather_ps(action_values[1].data(), indices, 4);
              const __m256 current = _mm256_add_ps(first_action, second_action);
              alignas(32) float current_lanes[8];
              _mm256_store_ps(current_lanes, current);
              for (std::size_t lane = 0U; lane < 8U; ++lane) {
                values[slots[updating_local + lane]] = current_lanes[lane];
              }
            }
          }
        }
        for (; updating_local + 4U < slots.size(); updating_local += 4U) {
          const __m128i indices = _mm_cvtepu16_epi32(_mm_loadl_epi64(
              reinterpret_cast<const __m128i *>(slots.data() + updating_local)));
          const __m256d first_action =
              gather_four_as_double(action_values[0].data(), indices);
          const __m256d second_action =
              gather_four_as_double(action_values[1].data(), indices);
          __m256d current;
          if (decision.player == updating_player) {
            current = _mm256_add_pd(
                _mm256_mul_pd(
                    load_four_as_double(strategies[0].data() + updating_local),
                    first_action),
                _mm256_mul_pd(
                    load_four_as_double(strategies[1].data() + updating_local),
                    second_action));
          } else {
            current = _mm256_add_pd(first_action, second_action);
          }
          _mm256_store_pd(value_lanes, current);
          for (std::size_t lane = 0U; lane < 4U; ++lane) {
            values[slots[updating_local + lane]] =
                static_cast<Scalar>(value_lanes[lane]);
          }
          if (decision.player != updating_player || locked_root) {
            continue;
          }
          auto *const packed =
              buffers_.compact_state +
              (static_cast<std::size_t>(decision.action_base) +
               updating_local * 2U) *
                  3U;
          const __m128i low_words = _mm_shuffle_epi8(
              _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed)),
              expand_words);
          const __m128i high_words = _mm_shuffle_epi8(
              _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed + 12U)),
              expand_words);
          const __m128 low_regrets = _mm_castsi128_ps(
              _mm_slli_epi32(_mm_and_si128(low_words, regret_mask), 18));
          const __m128 high_regrets = _mm_castsi128_ps(
              _mm_slli_epi32(_mm_and_si128(high_words, regret_mask), 18));
          const __m256d first_regrets = _mm256_cvtps_pd(
              _mm_shuffle_ps(low_regrets, high_regrets,
                             _MM_SHUFFLE(2, 0, 2, 0)));
          const __m256d second_regrets = _mm256_cvtps_pd(
              _mm_shuffle_ps(low_regrets, high_regrets,
                             _MM_SHUFFLE(3, 1, 3, 1)));
          const __m128i encoded_first = encode_four_regrets(_mm256_max_pd(
              zero, _mm256_add_pd(
                        first_regrets,
                        _mm256_mul_pd(regret_weight,
                                      _mm256_sub_pd(first_action, current)))));
          const __m128i encoded_second = encode_four_regrets(_mm256_max_pd(
              zero, _mm256_add_pd(
                        second_regrets,
                        _mm256_mul_pd(regret_weight,
                                      _mm256_sub_pd(second_action, current)))));
          store_four_words(
              packed,
              _mm_or_si128(_mm_and_si128(low_words, strategy_mask),
                           _mm_unpacklo_epi32(encoded_first, encoded_second)));
          store_four_words(
              packed + 12U,
              _mm_or_si128(_mm_and_si128(high_words, strategy_mask),
                           _mm_unpackhi_epi32(encoded_first, encoded_second)));
        }
      } else if (action_count == 3U &&
                 buffers_.compact_state != nullptr) {
        const auto &slots = PlayerIndexed
                                ? board.player_flop_slots[updating_player]
                                : board.player_active_slots[updating_player];
        const __m128i expand_words =
            _mm_setr_epi8(0, 1, 2, -1, 3, 4, 5, -1,
                          6, 7, 8, -1, -1, -1, -1, -1);
        const __m128i compact_words =
            _mm_setr_epi8(0, 1, 2, 4, 5, 6, 8, 9, 10,
                          -1, -1, -1, -1, -1, -1, -1);
        const __m128i regret_mask = _mm_set1_epi32(0x1fff);
        const __m128i strategy_mask = _mm_set1_epi32(0x00ffe000);
        const __m256d zero = _mm256_setzero_pd();
        const __m256d regret_weight = _mm256_set1_pd(regret_update_weight_);
        const auto encode_four_regrets = [](const __m256d input) {
          const __m128i bits = _mm_and_si128(
              _mm_castps_si128(_mm256_cvtpd_ps(input)),
              _mm_set1_epi32(0x7fffffff));
          const __m128i discarded =
              _mm_and_si128(bits, _mm_set1_epi32(0x3ffff));
          __m128i packed = _mm_srli_epi32(bits, 18);
          const __m128i greater =
              _mm_cmpgt_epi32(discarded, _mm_set1_epi32(0x20000));
          const __m128i equal =
              _mm_cmpeq_epi32(discarded, _mm_set1_epi32(0x20000));
          const __m128i odd = _mm_cmpeq_epi32(
              _mm_and_si128(packed, _mm_set1_epi32(1)),
              _mm_set1_epi32(1));
          packed = _mm_add_epi32(
              packed,
              _mm_and_si128(_mm_or_si128(greater, _mm_and_si128(equal, odd)),
                            _mm_set1_epi32(1)));
          return _mm_min_epu32(packed, _mm_set1_epi32(0x1ffe));
        };
        const auto store_three_words = [compact_words](std::uint8_t *const destination,
                                                        const __m128i words) {
          const __m128i compact = _mm_shuffle_epi8(words, compact_words);
          _mm_storel_epi64(reinterpret_cast<__m128i *>(destination), compact);
          destination[8] = static_cast<std::uint8_t>(
              _mm_cvtsi128_si32(_mm_srli_si128(compact, 8)));
        };
        alignas(32) double value_lanes[4];
        if constexpr (std::is_same_v<Scalar, float>) {
          if (decision.player == updating_player && !locked_root) {
            const __m256 zero_float = _mm256_setzero_ps();
            const __m256 regret_weight_float =
                _mm256_set1_ps(static_cast<float>(regret_update_weight_));
            const auto encode_eight_regrets = [](const __m256 input) {
              const __m256i bits = _mm256_and_si256(
                  _mm256_castps_si256(input), _mm256_set1_epi32(0x7fffffff));
              const __m256i discarded =
                  _mm256_and_si256(bits, _mm256_set1_epi32(0x3ffff));
              __m256i packed = _mm256_srli_epi32(bits, 18);
              const __m256i greater =
                  _mm256_cmpgt_epi32(discarded, _mm256_set1_epi32(0x20000));
              const __m256i equal =
                  _mm256_cmpeq_epi32(discarded, _mm256_set1_epi32(0x20000));
              const __m256i odd = _mm256_cmpeq_epi32(
                  _mm256_and_si256(packed, _mm256_set1_epi32(1)),
                  _mm256_set1_epi32(1));
              packed = _mm256_add_epi32(
                  packed,
                  _mm256_and_si256(
                      _mm256_or_si256(greater, _mm256_and_si256(equal, odd)),
                      _mm256_set1_epi32(1)));
              return _mm256_min_epu32(packed, _mm256_set1_epi32(0x1ffe));
            };
            for (; updating_local + 8U <= slots.size(); updating_local += 8U) {
              const __m256i indices = _mm256_cvtepu16_epi32(_mm_loadu_si128(
                  reinterpret_cast<const __m128i *>(slots.data() + updating_local)));
              const std::array<__m256, 3> action_vectors{
                  _mm256_i32gather_ps(action_values[0].data(), indices, 4),
                  _mm256_i32gather_ps(action_values[1].data(), indices, 4),
                  _mm256_i32gather_ps(action_values[2].data(), indices, 4)};
              __m256 current = _mm256_add_ps(
                  _mm256_mul_ps(
                      _mm256_loadu_ps(strategies[0].data() + updating_local),
                      action_vectors[0]),
                  _mm256_mul_ps(
                      _mm256_loadu_ps(strategies[1].data() + updating_local),
                      action_vectors[1]));
              current = _mm256_add_ps(
                  current,
                  _mm256_mul_ps(
                      _mm256_loadu_ps(strategies[2].data() + updating_local),
                      action_vectors[2]));
              alignas(32) float current_lanes[8];
              _mm256_store_ps(current_lanes, current);
              for (std::size_t lane = 0U; lane < 8U; ++lane) {
                values[slots[updating_local + lane]] = current_lanes[lane];
              }

              auto *const packed =
                  buffers_.compact_state +
                  (static_cast<std::size_t>(decision.action_base) +
                   updating_local * 3U) *
                      3U;
              std::array<__m128i, 8> old_words{};
              std::array<__m128, 8> regret_rows{};
              for (std::size_t lane = 0U; lane < 8U; ++lane) {
                old_words[lane] = _mm_shuffle_epi8(
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(
                        packed + lane * 9U)),
                    expand_words);
                regret_rows[lane] = _mm_castsi128_ps(_mm_slli_epi32(
                    _mm_and_si128(old_words[lane], regret_mask), 18));
              }
              _MM_TRANSPOSE4_PS(regret_rows[0], regret_rows[1],
                                regret_rows[2], regret_rows[3]);
              _MM_TRANSPOSE4_PS(regret_rows[4], regret_rows[5],
                                regret_rows[6], regret_rows[7]);
              std::array<__m256i, 3> encoded{};
              for (std::size_t action = 0U; action < 3U; ++action) {
                const __m256 regrets = _mm256_insertf128_ps(
                    _mm256_castps128_ps256(regret_rows[action]),
                    regret_rows[action + 4U], 1);
                encoded[action] = encode_eight_regrets(_mm256_max_ps(
                    zero_float,
                    _mm256_add_ps(
                        regrets,
                        _mm256_mul_ps(
                            regret_weight_float,
                            _mm256_sub_ps(action_vectors[action], current)))));
              }
              for (std::size_t half = 0U; half < 2U; ++half) {
                std::array<__m128, 4> encoded_rows{
                    _mm_castsi128_ps(
                        half == 0U ? _mm256_castsi256_si128(encoded[0])
                                   : _mm256_extracti128_si256(encoded[0], 1)),
                    _mm_castsi128_ps(
                        half == 0U ? _mm256_castsi256_si128(encoded[1])
                                   : _mm256_extracti128_si256(encoded[1], 1)),
                    _mm_castsi128_ps(
                        half == 0U ? _mm256_castsi256_si128(encoded[2])
                                   : _mm256_extracti128_si256(encoded[2], 1)),
                    _mm_setzero_ps()};
                _MM_TRANSPOSE4_PS(encoded_rows[0], encoded_rows[1],
                                  encoded_rows[2], encoded_rows[3]);
                for (std::size_t lane = 0U; lane < 4U; ++lane) {
                  const auto row = half * 4U + lane;
                  store_three_words(
                      packed + row * 9U,
                      _mm_or_si128(
                          _mm_and_si128(old_words[row], strategy_mask),
                          _mm_castps_si128(encoded_rows[lane])));
                }
              }
            }
          } else if (decision.player != updating_player || locked_root) {
            for (; updating_local + 8U <= slots.size(); updating_local += 8U) {
              const __m256i indices = _mm256_cvtepu16_epi32(_mm_loadu_si128(
                  reinterpret_cast<const __m128i *>(slots.data() + updating_local)));
              const __m256 current = _mm256_add_ps(
                  _mm256_add_ps(
                      _mm256_i32gather_ps(action_values[0].data(), indices, 4),
                      _mm256_i32gather_ps(action_values[1].data(), indices, 4)),
                  _mm256_i32gather_ps(action_values[2].data(), indices, 4));
              alignas(32) float current_lanes[8];
              _mm256_store_ps(current_lanes, current);
              for (std::size_t lane = 0U; lane < 8U; ++lane) {
                values[slots[updating_local + lane]] = current_lanes[lane];
              }
            }
          }
        }
        for (; updating_local + 4U < slots.size(); updating_local += 4U) {
          const __m128i indices = _mm_cvtepu16_epi32(_mm_loadl_epi64(
              reinterpret_cast<const __m128i *>(slots.data() + updating_local)));
          const std::array<__m256d, 3> action_vectors{
              gather_four_as_double(action_values[0].data(), indices),
              gather_four_as_double(action_values[1].data(), indices),
              gather_four_as_double(action_values[2].data(), indices)};
          __m256d current;
          if (decision.player == updating_player) {
            current = _mm256_add_pd(
                _mm256_mul_pd(
                    load_four_as_double(strategies[0].data() + updating_local),
                    action_vectors[0]),
                _mm256_mul_pd(
                    load_four_as_double(strategies[1].data() + updating_local),
                    action_vectors[1]));
            current = _mm256_add_pd(
                current,
                _mm256_mul_pd(
                    load_four_as_double(strategies[2].data() + updating_local),
                    action_vectors[2]));
          } else {
            current = _mm256_add_pd(
                _mm256_add_pd(action_vectors[0], action_vectors[1]),
                action_vectors[2]);
          }
          _mm256_store_pd(value_lanes, current);
          for (std::size_t lane = 0U; lane < 4U; ++lane) {
            values[slots[updating_local + lane]] =
                static_cast<Scalar>(value_lanes[lane]);
          }
          if (decision.player != updating_player || locked_root) {
            continue;
          }
          auto *const packed =
              buffers_.compact_state +
              (static_cast<std::size_t>(decision.action_base) +
               updating_local * 3U) *
                  3U;
          std::array<__m128i, 4> word_rows{
              _mm_shuffle_epi8(
                  _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed)),
                  expand_words),
              _mm_shuffle_epi8(
                  _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed + 9U)),
                  expand_words),
              _mm_shuffle_epi8(
                  _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed + 18U)),
                  expand_words),
              _mm_shuffle_epi8(
                  _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed + 27U)),
                  expand_words)};
          std::array<__m128, 4> regret_rows{
              _mm_castsi128_ps(_mm_slli_epi32(
                  _mm_and_si128(word_rows[0], regret_mask), 18)),
              _mm_castsi128_ps(_mm_slli_epi32(
                  _mm_and_si128(word_rows[1], regret_mask), 18)),
              _mm_castsi128_ps(_mm_slli_epi32(
                  _mm_and_si128(word_rows[2], regret_mask), 18)),
              _mm_castsi128_ps(_mm_slli_epi32(
                  _mm_and_si128(word_rows[3], regret_mask), 18))};
          _MM_TRANSPOSE4_PS(regret_rows[0], regret_rows[1],
                            regret_rows[2], regret_rows[3]);
          alignas(16) std::uint32_t encoded[3][4];
          for (std::size_t action = 0U; action < 3U; ++action) {
            const __m256d updated = _mm256_max_pd(
                zero,
                _mm256_add_pd(
                    _mm256_cvtps_pd(regret_rows[action]),
                    _mm256_mul_pd(
                        regret_weight,
                        _mm256_sub_pd(action_vectors[action], current))));
            _mm_store_si128(reinterpret_cast<__m128i *>(encoded[action]),
                            encode_four_regrets(updated));
          }
          alignas(16) std::uint32_t words[4][4];
          for (std::size_t lane = 0U; lane < 4U; ++lane) {
            _mm_store_si128(reinterpret_cast<__m128i *>(words[lane]),
                            word_rows[lane]);
            for (std::size_t action = 0U; action < 3U; ++action) {
              words[lane][action] =
                  encoded[action][lane] |
                  (words[lane][action] & 0x00ffe000U);
            }
            store_three_words(
                packed + lane * 9U,
                _mm_load_si128(reinterpret_cast<const __m128i *>(words[lane])));
          }
        }
      }
    }
    if (decision.player == updating_player && strategy_weight != 0.0 &&
        buffers_.compact_state != nullptr) {
      for (std::size_t averaged_local = averaging_fused_until;
           averaged_local < updating_local;
           ++averaged_local) {
        const ComboId combo = updating_combos[averaged_local];
        const auto slot = value_slot(combo, updating_player);
        const auto local = board.player_local[decision.player][combo];
        const auto offset = canonical_action_base(canonical, local);
        const double reach_weight =
            strategy_weight * static_cast<double>((*reach[updating_player])[slot]);
        if (action_count == 2U) {
          add_strategy_pair(
              static_cast<std::size_t>(offset),
              reach_weight *
                  static_cast<double>(strategies[0][averaged_local]),
              reach_weight *
                  static_cast<double>(strategies[1][averaged_local]));
          continue;
        }
        for (std::size_t action = 0U; action < action_count; ++action) {
          const double strategy_delta =
              reach_weight * static_cast<double>(strategies[action][averaged_local]);
          if (strategy_delta == 0.0) {
            continue;
          }
          auto *const bytes =
              buffers_.compact_state +
              static_cast<std::size_t>(offset + action) * 3U;
          const auto word = compact_word(bytes);
          const auto average = encode_strategy11(
              decode_strategy11(static_cast<std::uint16_t>(word >> 13U)) +
              strategy_delta);
          store_compact_word(
              bytes, (word & 0x1fffU) |
                         (static_cast<std::uint32_t>(average) << 13U));
        }
      }
    }
    for (; updating_local < updating_combos.size(); ++updating_local) {
      const ComboId combo = updating_combos[updating_local];
      const auto slot = value_slot(combo, updating_player);
      if (decision.player == updating_player) {
        for (std::size_t action = 0U; action < action_count; ++action) {
          values[slot] +=
              strategies[action][updating_local] * action_values[action][slot];
        }
      } else {
        for (std::size_t action = 0U; action < action_count; ++action) {
          values[slot] += action_values[action][slot];
        }
      }
      if (decision.player == updating_player) {
        const auto local = board.player_local[decision.player][combo];
        const auto offset = canonical_action_base(canonical, local);
        for (std::size_t action = 0; action < action_count; ++action) {
          const auto index = static_cast<std::size_t>(offset + action);
          const double strategy_delta =
              strategy_weight == 0.0
                  ? 0.0
                  : strategy_weight * (*reach[updating_player])[slot] *
                        strategies[action][updating_local];
          if (buffers_.compact_state != nullptr) {
            auto *const bytes = buffers_.compact_state + index * 3U;
            const auto word = compact_word(bytes);
            auto regret = static_cast<std::uint16_t>(word & 0x1fffU);
            auto average = static_cast<std::uint16_t>(word >> 13U);
            if (!locked_root) {
              regret = encode_regret13(std::max(
                  0.0, decode_regret13(regret) +
                           regret_update_weight_ *
                               (action_values[action][slot] - values[slot])));
            }
            if (strategy_delta != 0.0) {
              average = encode_strategy11(decode_strategy11(average) + strategy_delta);
            }
            store_compact_word(bytes, static_cast<std::uint32_t>(regret) |
                                          (static_cast<std::uint32_t>(average) << 13U));
          } else {
            if (!locked_root) {
              const double updated = std::max(
                  0.0, buffers_.regret_at(index) +
                           regret_update_weight_ *
                               (action_values[action][slot] - values[slot]));
              buffers_.set_regret(index, updated);
            }
            if (strategy_delta != 0.0) {
              add_strategy(index, strategy_delta);
            }
          }
        }
      }
    }
    if (profile) {
      prof_value_update_seconds_ +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        value_started)
              .count();
    }
    return std::nullopt;
  }

  Result<ComboVector, PostflopSolverError>
  policy_canonical_chance(const CanonicalPublicNode &canonical, const std::uint8_t updating_player,
                          const ReachRef &reach, const bool best_response) {
    if (canonical.total_legal_outcome_count <= 4U) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    const auto &board = layout_.boards[canonical.board_index];
    const double denominator = static_cast<double>(canonical.total_legal_outcome_count - 4U);
    auto values = zeroed_values(updating_player);
    const std::size_t edge_count = canonical.edges.size();
    auto representative_reaches =
        std::make_unique_for_overwrite<std::array<ComboVector, 2>[]>(edge_count);
    auto child_values = std::make_unique<ComboVector[]>(edge_count);
    for (std::size_t index = 0U; index < edge_count; ++index) {
      const auto &edge = canonical.edges[index];
      if (edge.outcomes.empty()) {
        return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
      }
      const auto &representative = edge.outcomes.front();
      representative_reaches[index] = transform_reach(
          {*reach[0], *reach[1]}, representative.physical_to_child_automorphism);
    }
    const std::size_t worker_count = parallel_workers_.size();
    if (std::popcount(board.mask) == 3U && worker_count > 0U && edge_count > 1U) {
      const std::size_t task_count = std::min(edge_count, worker_count + 1U);
      // Keep the caller productive as the eighth configured solver thread.
      // Reserving one item avoids losing the race to the persistent workers;
      // after it completes, the caller rejoins the shared dynamic queue.
      std::atomic<std::size_t> next_edge{1U};
      std::vector<std::packaged_task<TraversalResult(DenseTraversal &)>> tasks;
      std::vector<std::future<TraversalResult>> futures;
      tasks.reserve(task_count - 1U);
      futures.reserve(task_count - 1U);
      for (std::size_t task_index = 1U; task_index < task_count; ++task_index) {
        std::packaged_task<TraversalResult(DenseTraversal &)> task(
            [edges = &canonical.edges, reaches = representative_reaches.get(),
             results = child_values.get(), next = &next_edge, edge_count,
             updating_player, best_response](DenseTraversal &self) {
              while (true) {
                const std::size_t index =
                    next->fetch_add(1U, std::memory_order_relaxed);
                if (index >= edge_count) {
                  break;
                }
                auto child = self.policy_canonical(
                    (*edges)[index].outcomes.front().child, updating_player,
                    {&reaches[index][0], &reaches[index][1]}, best_response);
                if (!child) {
                  return Result<ComboVector, PostflopSolverError>::failure(
                      child.error());
                }
                results[index] = std::move(child.value());
              }
              return Result<ComboVector, PostflopSolverError>::success(
                  ComboVector{});
            });
        futures.push_back(task.get_future());
        tasks.push_back(std::move(task));
      }
      dispatch_parallel_tasks(tasks);
      std::optional<PostflopSolverError> main_error;
      auto first_child = policy_canonical(
          canonical.edges[0U].outcomes.front().child, updating_player,
          {&representative_reaches[0U][0], &representative_reaches[0U][1]},
          best_response);
      if (!first_child) {
        main_error = first_child.error();
      } else {
        child_values[0U] = std::move(first_child.value());
      }
      while (!main_error) {
        const std::size_t index =
            next_edge.fetch_add(1U, std::memory_order_relaxed);
        if (index >= edge_count) {
          break;
        }
        auto child = policy_canonical(
            canonical.edges[index].outcomes.front().child, updating_player,
            {&representative_reaches[index][0],
             &representative_reaches[index][1]},
            best_response);
        if (!child) {
          main_error = child.error();
        } else {
          child_values[index] = std::move(child.value());
        }
      }
      std::optional<PostflopSolverError> worker_error;
      for (auto &future : futures) {
        wait_for_parallel_future(future);
        auto result = future.get();
        if (!result && !worker_error) {
          worker_error = result.error();
        }
      }
      if (main_error || worker_error) {
        return Result<ComboVector, PostflopSolverError>::failure(
            main_error ? *main_error : *worker_error);
      }
    } else {
      for (std::size_t index = 0U; index < edge_count; ++index) {
        auto child = policy_canonical(
            canonical.edges[index].outcomes.front().child, updating_player,
            {&representative_reaches[index][0],
             &representative_reaches[index][1]},
            best_response);
        if (!child) {
          return child;
        }
        child_values[index] = std::move(child.value());
      }
    }
    for (std::size_t index = 0U; index < edge_count; ++index) {
      const auto &edge = canonical.edges[index];
      for (const auto &outcome : edge.outcomes) {
        const double probability =
            static_cast<double>(outcome.physical_outcome_count) / denominator;
        accumulate_transformed_values_to_parent(
            values, child_values[index],
            outcome.physical_to_child_automorphism, updating_player, board,
            outcome.chance_card, probability);
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(std::move(values));
  }

  Result<ComboVector, PostflopSolverError>
  policy_canonical_decision(const CanonicalPublicNode &canonical,
                            const std::uint8_t updating_player,
                            const ReachRef &reach, const bool best_response) {
    const auto &decision = canonical.decision;
    const auto &board = layout_.boards[decision.board_index];
    const auto action_count = static_cast<std::size_t>(decision.action_count);
    if (canonical.edges.size() != action_count) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    DecisionScratchLease scratch_lease(*this);
    auto &action_values = scratch_lease.get().action_values;
    auto &strategies = scratch_lease.get().strategies;
    auto &actor_reach = scratch_lease.get().reach_actor[0];
    const bool locked_root = is_locked_root(canonical);
    const bool average_policy = !diagnostic_certify_current_strategy();
    const bool local_scaled_average =
        average_policy && !locked_root && scaled_action_major_state();
    if (local_scaled_average) {
      load_canonical_current_strategies(canonical, board, decision.player,
                                        strategies, true, true);
    } else {
      for (const ComboId combo : board.player_combos[decision.player]) {
        const auto actor_local = board.player_local[decision.player][combo];
        const auto locked = locked_root ? locked_root_strategy(actor_local)
                                        : std::nullopt;
        const auto strategy =
            locked ? *locked
                   : current_strategy(canonical, actor_local, average_policy);
        const auto slot = value_slot(combo, decision.player);
        for (std::size_t action = 0; action < action_count; ++action) {
          strategies[action][slot] = static_cast<Scalar>(strategy[action]);
        }
      }
    }
    for (std::size_t action = 0; action < action_count; ++action) {
      if (canonical.edges[action].outcomes.size() != 1U) {
        return Result<ComboVector, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      const auto &outcome = canonical.edges[action].outcomes.front();
      if (!identity_automorphism(outcome.physical_to_child_automorphism)) {
        return Result<ComboVector, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      actor_reach = *reach[decision.player];
      if (!(best_response && decision.player == updating_player)) {
        const auto &actor_combos = board.player_combos[decision.player];
        for (std::size_t local = 0U; local < actor_combos.size(); ++local) {
          const auto slot = value_slot(actor_combos[local], decision.player);
          const auto strategy_slot = local_scaled_average ? local : slot;
          actor_reach[slot] *= strategies[action][strategy_slot];
        }
      }
      const ReachRef child_reach =
          decision.player == 0U ? ReachRef{&actor_reach, reach[1]}
                                : ReachRef{reach[0], &actor_reach};
      auto child = policy_canonical(outcome.child, updating_player, child_reach,
                                    best_response);
      if (!child) {
        return child;
      }
      action_values[action] = std::move(child.value());
    }
    auto values = zeroed_values(updating_player);
    const auto &updating_combos = board.player_combos[updating_player];
    if constexpr (PlayerIndexed) {
      const auto slot_count = layout_.player_flop_count[updating_player];
      if (best_response && decision.player == updating_player && !locked_root) {
        std::copy_n(action_values[0].begin(), slot_count, values.begin());
        for (std::size_t action = 1U; action < action_count; ++action) {
          for (std::size_t slot = 0U; slot < slot_count; ++slot) {
            values[slot] = std::max(values[slot], action_values[action][slot]);
          }
        }
        return Result<ComboVector, PostflopSolverError>::success(std::move(values));
      }
      if (decision.player != updating_player) {
        for (std::size_t action = 0U; action < action_count; ++action) {
          for (std::size_t slot = 0U; slot < slot_count; ++slot) {
            values[slot] += action_values[action][slot];
          }
        }
        return Result<ComboVector, PostflopSolverError>::success(std::move(values));
      }
    }
    for (std::size_t local = 0U; local < updating_combos.size(); ++local) {
      const auto slot = value_slot(updating_combos[local], updating_player);
      if (best_response && decision.player == updating_player && !is_locked_root(canonical)) {
        values[slot] = action_values[0][slot];
        for (std::size_t action = 1; action < action_count; ++action) {
          values[slot] = std::max(values[slot], action_values[action][slot]);
        }
      } else {
        if (decision.player == updating_player) {
          const auto strategy_slot = local_scaled_average ? local : slot;
          for (std::size_t action = 0; action < action_count; ++action) {
            values[slot] += strategies[action][strategy_slot] *
                            action_values[action][slot];
          }
        } else {
          for (std::size_t action = 0; action < action_count; ++action) {
            values[slot] += action_values[action][slot];
          }
        }
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(std::move(values));
  }

  template <bool Float32State>
  std::array<double, maximum_action_count>
  current_strategy_for_state(const DecisionLayout &decision, const std::int16_t local_combo,
                             const bool average) {
    const auto count = static_cast<std::size_t>(decision.action_count);
    const auto offset = decision_action_base(layout_, decision, local_combo);
    std::array<double, maximum_action_count> strategy{};
    const auto source_at = [this, average](const std::size_t index) {
      if constexpr (Float32State) {
        const double value = average ? static_cast<double>(buffers_.strategy_float32[index])
                                     : static_cast<double>(buffers_.regret_float32[index]);
        return average ? value : std::max(0.0, value);
      } else {
        return average ? buffers_.strategy_at(index) : std::max(0.0, buffers_.regret_at(index));
      }
    };
    if (count == 2U) {
      const double first = source_at(static_cast<std::size_t>(offset));
      const double second = source_at(static_cast<std::size_t>(offset + 1U));
      const double sum = first + second;
      if (sum <= 0.0) {
        strategy[0] = 0.5;
        strategy[1] = 0.5;
      } else {
        const double inverse = 1.0 / sum;
        strategy[0] = first * inverse;
        strategy[1] = second * inverse;
      }
      return strategy;
    }
    double sum = 0.0;
    for (std::size_t action = 0; action < count; ++action) {
      const double source = source_at(static_cast<std::size_t>(offset + action));
      strategy[action] = source;
      sum += source;
    }
    if (sum <= 0.0) {
      std::fill_n(strategy.begin(), count, 1.0 / static_cast<double>(count));
    } else {
      for (std::size_t action = 0; action < count; ++action) {
        strategy[action] /= sum;
      }
    }
    return strategy;
  }

  std::array<double, maximum_action_count> current_strategy(const DecisionLayout &decision,
                                                            const std::int16_t local_combo,
                                                            const bool average) {
    return buffers_.regret_float32 != nullptr
               ? current_strategy_for_state<true>(decision, local_combo, average)
               : current_strategy_for_state<false>(decision, local_combo, average);
  }

  std::array<double, maximum_action_count>
  current_strategy(const CanonicalPublicNode &canonical, const std::int16_t local_combo,
                   const bool average) {
    if (action_major_compact_state()) {
      const auto count = static_cast<std::size_t>(canonical.decision.action_count);
      const auto local = static_cast<std::size_t>(local_combo);
      std::array<double, maximum_action_count> strategy{};
      double sum = 0.0;
      for (std::size_t action = 0; action < count; ++action) {
        const auto *const bytes = buffers_.compact_state +
            canonical_action_major_index(canonical, local, action) * 3U;
        const auto word = compact_word(bytes);
        const double value =
            average ? decode_strategy11(static_cast<std::uint16_t>(word >> 13U))
                    : decode_regret13(static_cast<std::uint16_t>(word & 0x1fffU));
        strategy[action] = value;
        sum += value;
      }
      if (sum <= 0.0) {
        std::fill_n(strategy.begin(), count, 1.0 / static_cast<double>(count));
      } else {
        const double inverse = 1.0 / sum;
        for (std::size_t action = 0; action < count; ++action) {
          strategy[action] *= inverse;
        }
      }
      return strategy;
    }
    if (scaled_action_major_state()) {
      const auto count = static_cast<std::size_t>(canonical.decision.action_count);
      const auto local = static_cast<std::size_t>(local_combo);
      const auto *const source =
          average ? buffers_.scaled_strategy : buffers_.scaled_regret;
      std::array<double, maximum_action_count> strategy{};
      double sum = 0.0;
      for (std::size_t action = 0; action < count; ++action) {
        const auto code =
            source[canonical_action_major_index(canonical, local, action)];
        strategy[action] = static_cast<double>(code);
        sum += strategy[action];
      }
      if (sum <= 0.0) {
        std::fill_n(strategy.begin(), count, 1.0 / static_cast<double>(count));
      } else {
        const double inverse = 1.0 / sum;
        for (std::size_t action = 0; action < count; ++action) {
          strategy[action] *= inverse;
        }
      }
      return strategy;
    }
    auto direct = canonical.decision;
    direct.action_base = canonical_action_base(canonical, local_combo);
    return current_strategy(direct, 0, average);
  }

  [[nodiscard]] bool is_locked_root(const CanonicalPublicNode &canonical) const noexcept {
    return root_lock_ != nullptr &&
           std::addressof(canonical) == std::addressof(layout_.canonical_public_graph
                                                           .nodes[layout_.canonical_public_graph
                                                                     .root]);
  }

  [[nodiscard]] bool is_locked_root(const PublicTreeNode &node) const noexcept {
    return root_lock_ != nullptr && node.id == layout_.tree.root;
  }

  [[nodiscard]] std::optional<std::array<double, maximum_action_count>>
  locked_root_strategy(const std::int16_t local_combo) const {
    if (root_lock_ == nullptr || local_combo < 0) {
      return std::nullopt;
    }
    const auto local = static_cast<std::size_t>(local_combo);
    if (local >= root_lock_->by_local_combo.size()) {
      return std::nullopt;
    }
    return root_lock_->by_local_combo[local];
  }

  Result<ComboVector, PostflopSolverError> fold_values(const PublicTreeNode &node,
                                                       const std::uint8_t updating_player,
                                                       const ComboVector &opponent_reach) const {
    const auto payoff_index = layout_.node_terminal_payoff[static_cast<std::size_t>(node.id)];
    if (payoff_index >= layout_.terminal_payoffs.size()) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    return fold_values_with_payoff(
        layout_.node_board[static_cast<std::size_t>(node.id)],
        layout_.terminal_payoffs[payoff_index].value_antes[updating_player][0], updating_player,
        opponent_reach);
  }

  std::optional<PostflopSolverError>
  fold_values_into(const PublicTreeNode &node, const std::uint8_t updating_player,
                   const ComboVector &opponent_reach, ComboVector &values_out) const {
    const auto payoff_index = layout_.node_terminal_payoff[static_cast<std::size_t>(node.id)];
    if (payoff_index >= layout_.terminal_payoffs.size()) {
      return PostflopSolverError::InvalidConfiguration;
    }
    return fold_values_with_payoff_into(layout_.node_board[static_cast<std::size_t>(node.id)],
                                        layout_.terminal_payoffs[payoff_index]
                                            .value_antes[updating_player][0],
                                        updating_player, opponent_reach, values_out);
  }

  Result<ComboVector, PostflopSolverError> fold_values(const PublicState &state,
                                                       const std::uint32_t board_index,
                                                       const std::uint8_t updating_player,
                                                       const ComboVector &opponent_reach) const {
    const auto settlement = settle_terminal(state, layout_.tree.config.rake);
    if (!settlement) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::SettlementFailure);
    }
    const double payoff =
        static_cast<double>(settlement.value().payoff_units[updating_player]) / units_per_ante;
    return fold_values_with_payoff(board_index, payoff, updating_player, opponent_reach);
  }

  template <bool BoardLocal = false>
  std::optional<PostflopSolverError>
  fold_values_with_payoff_into(const std::uint32_t board_index, const double payoff,
                               const std::uint8_t updating_player,
                               const ComboVector &opponent_reach, ComboVector &values_out) const {
    double total = 0.0;
    std::array<double, 36> by_card{};
    const auto &board = layout_.boards[board_index];
    const auto opponent = static_cast<std::uint8_t>(1U - updating_player);
    if constexpr (PlayerIndexed) {
      // Compact per-player terminal: only the opponent's live combos carry
      // nonzero opponent reach, and only the updating player's live combos
      // are ever read by parents (reach of the updating player is zero
      // elsewhere), so iterate exactly those lists.
      const auto &opponent_combos = board.player_combos[opponent];
      for (std::size_t local = 0; local < opponent_combos.size(); ++local) {
        const auto combo_id = opponent_combos[local];
        const double weight = opponent_reach[
            BoardLocal ? local : board.player_flop_slots[opponent][local]];
        total += weight;
        by_card[layout_.combos[combo_id].first.value()] += weight;
        by_card[layout_.combos[combo_id].second.value()] += weight;
      }
    } else {
      for (const ComboId combo_id : board.legal_combos) {
        const double weight = opponent_reach[value_slot(combo_id, opponent)];
        total += weight;
        by_card[layout_.combos[combo_id].first.value()] += weight;
        by_card[layout_.combos[combo_id].second.value()] += weight;
      }
    }
    if constexpr (!PlayerIndexed) {
      zero_board_values_into(board, updating_player, values_out);
    }
    if constexpr (PlayerIndexed) {
      const auto &updating_combos = board.player_combos[updating_player];
      const auto &opponent_slots = board.player_opponent_flop_slots[updating_player];
      for (std::size_t local = 0; local < updating_combos.size(); ++local) {
        const auto &combo = layout_.combos[updating_combos[local]];
        const auto opponent_slot = BoardLocal
                                       ? board.terminal_combos[updating_player]
                                             .opponent_local[local]
                                       : opponent_slots[local];
        const double own_weight = opponent_slot != TerminalComboData::invalid_slot
                                      ? opponent_reach[opponent_slot]
                                      : 0.0;
        const double compatible = total - by_card[combo.first.value()] -
                                  by_card[combo.second.value()] + own_weight;
        const auto output_slot = BoardLocal
                                     ? local
                                     : (layout_.uses_canonical_public_dag
                                            ? static_cast<std::size_t>(
                                                  board.player_flop_slots[updating_player][local])
                                            : local);
        values_out[output_slot] =
            static_cast<Scalar>(compatible * payoff / layout_.initial_normalization);
      }
    } else {
      for (const ComboId combo_id : board.legal_combos) {
        const auto &combo = layout_.combos[combo_id];
        const double compatible = total - by_card[combo.first.value()] -
                                  by_card[combo.second.value()] +
                                  opponent_reach[value_slot(combo_id, updating_player)];
        values_out[value_slot(combo_id, updating_player)] =
            static_cast<Scalar>(compatible * payoff / layout_.initial_normalization);
      }
    }
    return std::nullopt;
  }

  Result<ComboVector, PostflopSolverError>
  fold_values_with_payoff(const std::uint32_t board_index, const double payoff,
                          const std::uint8_t updating_player,
                          const ComboVector &opponent_reach) const {
    ComboVector values{};
    if (const auto error = fold_values_with_payoff_into(board_index, payoff, updating_player,
                                                        opponent_reach, values)) {
      return Result<ComboVector, PostflopSolverError>::failure(*error);
    }
    return Result<ComboVector, PostflopSolverError>::success(std::move(values));
  }

  Result<ComboVector, PostflopSolverError> showdown_values(const PublicTreeNode &node,
                                                           const std::uint8_t updating_player,
                                                           const ComboVector &opponent_reach) {
    const auto payoff_index = layout_.node_terminal_payoff[static_cast<std::size_t>(node.id)];
    if (payoff_index >= layout_.terminal_payoffs.size()) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    const auto &payoff = layout_.terminal_payoffs[payoff_index].value_antes[updating_player];
    return showdown_values_with_payoffs(
        layout_.node_board[static_cast<std::size_t>(node.id)], payoff[0], payoff[1], payoff[2],
        updating_player, opponent_reach);
  }

  std::optional<PostflopSolverError>
  showdown_values_into(const PublicTreeNode &node, const std::uint8_t updating_player,
                       const ComboVector &opponent_reach, ComboVector &values_out) {
    const auto payoff_index = layout_.node_terminal_payoff[static_cast<std::size_t>(node.id)];
    if (payoff_index >= layout_.terminal_payoffs.size()) {
      return PostflopSolverError::InvalidConfiguration;
    }
    const auto &payoff = layout_.terminal_payoffs[payoff_index].value_antes[updating_player];
    return showdown_values_with_payoffs_into(layout_.node_board[static_cast<std::size_t>(node.id)],
                                             payoff[0], payoff[1], payoff[2], updating_player,
                                             opponent_reach, values_out, 0.0, nullptr);
  }

  std::optional<PostflopSolverError>
  showdown_and_fold_values_into(const PublicTreeNode &showdown_node,
                                const PublicTreeNode &fold_node,
                                const std::uint8_t updating_player,
                                const ComboVector &showdown_opponent_reach,
                                const ComboVector &fold_opponent_reach,
                                ComboVector &showdown_values_out,
                                ComboVector &fold_values_out) {
    const auto showdown_payoff_index =
        layout_.node_terminal_payoff[static_cast<std::size_t>(showdown_node.id)];
    const auto fold_payoff_index =
        layout_.node_terminal_payoff[static_cast<std::size_t>(fold_node.id)];
    const auto showdown_board = layout_.node_board[static_cast<std::size_t>(showdown_node.id)];
    if (showdown_payoff_index >= layout_.terminal_payoffs.size() ||
        fold_payoff_index >= layout_.terminal_payoffs.size() ||
        showdown_board != layout_.node_board[static_cast<std::size_t>(fold_node.id)]) {
      return PostflopSolverError::InvalidConfiguration;
    }
    const auto &showdown_payoff =
        layout_.terminal_payoffs[showdown_payoff_index].value_antes[updating_player];
    const double fold_payoff =
        layout_.terminal_payoffs[fold_payoff_index].value_antes[updating_player][0];
    return showdown_values_with_payoffs_into<false, true>(
        showdown_board, showdown_payoff[0], showdown_payoff[1], showdown_payoff[2],
        updating_player, showdown_opponent_reach, showdown_values_out, fold_payoff,
        &fold_values_out, &fold_opponent_reach);
  }

  Result<ComboVector, PostflopSolverError> showdown_values(const PublicState &state,
                                                           const std::uint32_t board_index,
                                                           const std::uint8_t updating_player,
                                                           const ComboVector &opponent_reach) {
    const auto own_win = settle_terminal(state, layout_.tree.config.rake,
                                         static_cast<std::uint8_t>(1U << updating_player));
    const auto tie = settle_terminal(state, layout_.tree.config.rake, 0b11U);
    const auto own_loss = settle_terminal(state, layout_.tree.config.rake,
                                          static_cast<std::uint8_t>(1U << (1U - updating_player)));
    if (!own_win || !tie || !own_loss) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::SettlementFailure);
    }
    const double win_payoff =
        static_cast<double>(own_win.value().payoff_units[updating_player]) / units_per_ante;
    const double tie_payoff =
        static_cast<double>(tie.value().payoff_units[updating_player]) / units_per_ante;
    const double loss_payoff =
        static_cast<double>(own_loss.value().payoff_units[updating_player]) / units_per_ante;
    return showdown_values_with_payoffs(board_index, win_payoff, tie_payoff, loss_payoff,
                                        updating_player, opponent_reach);
  }

  template <bool BoardLocal = false, bool PairedFold = false>
  std::optional<PostflopSolverError>
  showdown_values_with_payoffs_into(const std::uint32_t board_index, const double win_payoff,
                                    const double tie_payoff, const double loss_payoff,
                                    const std::uint8_t updating_player,
                                    const ComboVector &opponent_reach, ComboVector &values_out,
                                    const double paired_fold_payoff,
                                    ComboVector *const paired_fold_values_out,
                                    const ComboVector *const paired_fold_opponent_reach = nullptr) {
    const auto &board = layout_.boards[board_index];
    // solve_postflop_exact prepares every five-card board once, before any
    // worker starts. The previous hot-path call back into prepare_ranks()
    // rebuilt a Result and traversed a large cold function prologue at every
    // showdown even though ranks_ready can no longer change during solving.
    if (!board.ranks_ready) {
      return PostflopSolverError::InvalidConfiguration;
    }
    const auto rank_count = static_cast<std::size_t>(
        PlayerIndexed ? board.player_rank_count : board.rank_count);
    const auto opponent = static_cast<std::uint8_t>(1U - updating_player);
    if constexpr (PairedFold) {
      if (paired_fold_values_out == nullptr) {
        return PostflopSolverError::InvalidConfiguration;
      }
    }
    using PairedFoldAccumulator =
        std::conditional_t<std::is_same_v<Scalar, float>, float, double>;
    PairedFoldAccumulator paired_fold_total = PairedFoldAccumulator{0};
    std::array<PairedFoldAccumulator, 36U> paired_fold_by_card{};
    const bool paired_fold_shares_reach = PairedFold &&
        (paired_fold_opponent_reach == nullptr ||
         paired_fold_opponent_reach == &opponent_reach);
    const auto calculate = [&](auto &totals, auto &by_card, auto &prefix, auto &card_prefix) {
      using Accumulator = typename std::remove_reference_t<decltype(totals)>::value_type;
      const bool profile = hotpath_profiling_enabled();
      const auto t_accumulate = profile ? std::chrono::steady_clock::now()
                                        : std::chrono::steady_clock::time_point{};
      if constexpr (PlayerIndexed) {
        const auto &opponent_combos = board.terminal_combos[opponent];
        const auto accumulate_one = [&](const std::size_t local,
                                        const Accumulator weight) {
          totals[opponent_combos.rank[local]] += weight;
          by_card[opponent_combos.first_by_rank[local]] += weight;
          by_card[opponent_combos.second_by_rank[local]] += weight;
          if constexpr (PairedFold) {
            if (!paired_fold_shares_reach) {
            const PairedFoldAccumulator fold_weight =
                paired_fold_opponent_reach != nullptr
                    ? static_cast<PairedFoldAccumulator>(
                          (*paired_fold_opponent_reach)
                              [BoardLocal ? local
                                          : opponent_combos.own_slot[local]])
                    : static_cast<PairedFoldAccumulator>(weight);
            paired_fold_total += fold_weight;
            paired_fold_by_card[opponent_combos.first_card[local]] += fold_weight;
            paired_fold_by_card[opponent_combos.second_card[local]] += fold_weight;
            }
          }
        };
        std::size_t local = 0U;
        if constexpr ((BoardLocal && std::is_same_v<Accumulator, float>) ||
                      std::is_same_v<Accumulator, double>) {
          // Four independent reach loads can overlap before the dependent
          // rank/card scatter chains.  The updates still execute in original
          // combo order, preserving every per-cell floating-point addition.
          for (; local + 4U <= opponent_combos.size(); local += 4U) {
            const auto reach_slot = [&](const std::size_t offset) {
              return BoardLocal
                         ? local + offset
                         : static_cast<std::size_t>(
                               opponent_combos.own_slot[local + offset]);
            };
            const Accumulator weight_0 = opponent_reach[reach_slot(0U)];
            const Accumulator weight_1 = opponent_reach[reach_slot(1U)];
            const Accumulator weight_2 = opponent_reach[reach_slot(2U)];
            const Accumulator weight_3 = opponent_reach[reach_slot(3U)];
            accumulate_one(local, weight_0);
            accumulate_one(local + 1U, weight_1);
            accumulate_one(local + 2U, weight_2);
            accumulate_one(local + 3U, weight_3);
          }
        }
        for (; local < opponent_combos.size(); ++local) {
          const Accumulator weight = static_cast<Accumulator>(
              opponent_reach[BoardLocal ? local
                                        : opponent_combos.own_slot[local]]);
          accumulate_one(local, weight);
        }
      } else {
        const auto &opponent_combos = board.terminal_active_combos;
        for (std::size_t local = 0U; local < opponent_combos.size(); ++local) {
          const Accumulator weight = static_cast<Accumulator>(
              opponent_reach[opponent_combos.own_slot[local]]);
          totals[opponent_combos.rank[local]] += weight;
          by_card[opponent_combos.first_by_rank[local]] += weight;
          by_card[opponent_combos.second_by_rank[local]] += weight;
        }
      }
      const auto t_prefix = profile ? std::chrono::steady_clock::now()
                                    : std::chrono::steady_clock::time_point{};
      if (profile) {
        prof_showdown_accumulate_seconds_ +=
            std::chrono::duration<double>(t_prefix - t_accumulate).count();
      }
      for (std::size_t rank = 0; rank < rank_count; ++rank) {
        prefix[rank + 1U] = prefix[rank] + totals[rank];
        const auto source = rank * 36U;
        const auto destination = (rank + 1U) * 36U;
        if constexpr (std::is_same_v<Accumulator, float>) {
          std::size_t card = 0U;
          for (; card + 8U <= 36U; card += 8U) {
            _mm256_storeu_ps(
                card_prefix.data() + destination + card,
                _mm256_add_ps(
                    _mm256_loadu_ps(card_prefix.data() + source + card),
                    _mm256_loadu_ps(by_card.data() + source + card)));
          }
          for (; card < 36U; ++card) {
            card_prefix[destination + card] =
                card_prefix[source + card] + by_card[source + card];
          }
        } else {
          for (std::size_t card = 0; card < 36U; ++card) {
            card_prefix[destination + card] =
                card_prefix[source + card] + by_card[source + card];
          }
        }
      }
      auto &rank_base = [&]() -> auto & {
        if constexpr (std::is_same_v<Accumulator, float>) {
          return showdown_rank_base_float_;
        } else {
          return showdown_rank_base_;
        }
      }();
      const Accumulator total_reach = prefix[rank_count];
      for (std::size_t rank = 0; rank < rank_count; ++rank) {
        rank_base[rank] =
            prefix[rank] * static_cast<Accumulator>(win_payoff) +
            totals[rank] * static_cast<Accumulator>(tie_payoff) +
            (total_reach - prefix[rank + 1U]) *
                static_cast<Accumulator>(loss_payoff);
      }
      const auto t_output = profile ? std::chrono::steady_clock::now()
                                    : std::chrono::steady_clock::time_point{};
      if (profile) {
        prof_showdown_prefix_seconds_ +=
            std::chrono::duration<double>(t_output - t_prefix).count();
      }
      if constexpr (!PlayerIndexed) {
        zero_board_values_into(board, updating_player, values_out);
        if constexpr (PairedFold) {
          zero_board_values_into(board, updating_player, *paired_fold_values_out);
        }
      }
      if constexpr (PlayerIndexed && std::is_same_v<Accumulator, float>) {
        const auto &updating_combos = board.terminal_combos[updating_player];
        std::size_t local = 0U;
        {
          const __m256 loss = _mm256_set1_ps(static_cast<float>(loss_payoff));
          const __m256 inverse_normalization = _mm256_set1_ps(
              static_cast<float>(1.0 / layout_.initial_normalization));
          const __m256 zero = _mm256_setzero_ps();
          const __m256 lower_blocker_coefficient =
              _mm256_set1_ps(static_cast<float>(loss_payoff - win_payoff));
          const __m256 tie_blocker_coefficient =
              _mm256_set1_ps(static_cast<float>(loss_payoff - tie_payoff));
          const __m256 fold_scale = _mm256_set1_ps(
              static_cast<float>(paired_fold_payoff /
                                 layout_.initial_normalization));
          for (; local + 8U <= updating_combos.size(); local += 8U) {
            const auto load_indices =
                [local](const std::vector<std::uint16_t> &source) {
                  return _mm256_cvtepu16_epi32(_mm_loadu_si128(
                      reinterpret_cast<const __m128i *>(source.data() + local)));
                };
            const __m256i opponent_indices =
                load_indices(BoardLocal ? updating_combos.opponent_local
                                        : updating_combos.opponent_slot);
            const __m256i valid_slots = _mm256_cmpgt_epi32(
                _mm256_set1_epi32(
                    static_cast<int>(TerminalComboData::invalid_slot)),
                opponent_indices);
            const __m256 own_reaches = _mm256_mask_i32gather_ps(
                zero, opponent_reach.data(), opponent_indices,
                _mm256_castsi256_ps(valid_slots), 4);
            const __m256i first_by_rank =
                load_indices(updating_combos.first_by_rank);
            const __m256i second_by_rank =
                load_indices(updating_combos.second_by_rank);
            const __m256 invalid_lower = _mm256_add_ps(
                _mm256_i32gather_ps(card_prefix.data(), first_by_rank, 4),
                _mm256_i32gather_ps(card_prefix.data(), second_by_rank, 4));
            const __m256 invalid_tie = _mm256_sub_ps(
                _mm256_add_ps(
                    _mm256_i32gather_ps(by_card.data(), first_by_rank, 4),
                    _mm256_i32gather_ps(by_card.data(), second_by_rank, 4)),
                own_reaches);
            const __m256i ranks = load_indices(updating_combos.rank);
            const __m256i rank_offsets = _mm256_mullo_epi32(
                ranks, _mm256_set1_epi32(36));
            const __m256i first_cards =
                _mm256_sub_epi32(first_by_rank, rank_offsets);
            const __m256i second_cards =
                _mm256_sub_epi32(second_by_rank, rank_offsets);
            const auto *const all_by_card =
                card_prefix.data() + rank_count * 36U;
            const __m256 invalid_all = _mm256_sub_ps(
                _mm256_add_ps(
                    _mm256_i32gather_ps(all_by_card, first_cards, 4),
                    _mm256_i32gather_ps(all_by_card, second_cards, 4)),
                own_reaches);
            __m256 numerator = _mm256_i32gather_ps(
                rank_base.data(), ranks, 4);
            numerator = _mm256_add_ps(
                numerator,
                _mm256_mul_ps(invalid_lower, lower_blocker_coefficient));
            numerator = _mm256_add_ps(
                numerator,
                _mm256_mul_ps(invalid_tie, tie_blocker_coefficient));
            numerator = _mm256_sub_ps(
                numerator, _mm256_mul_ps(invalid_all, loss));
            const __m256 result =
                _mm256_mul_ps(numerator, inverse_normalization);
            if constexpr (BoardLocal) {
              _mm256_storeu_ps(
                  reinterpret_cast<float *>(values_out.data()) + local, result);
            }
            alignas(32) float result_lanes[8];
            if constexpr (!BoardLocal) {
              _mm256_store_ps(result_lanes, result);
            }
            __m256 fold_result{};
            if constexpr (PairedFold) {
              const __m256 fold_own_reaches =
                  paired_fold_opponent_reach != nullptr
                      ? _mm256_mask_i32gather_ps(
                            zero, paired_fold_opponent_reach->data(),
                            opponent_indices,
                            _mm256_castsi256_ps(valid_slots), 4)
                      : own_reaches;
              const float fold_total = paired_fold_shares_reach
                                           ? prefix[rank_count]
                                           : paired_fold_total;
              const float *const fold_by_card =
                  paired_fold_shares_reach
                      ? all_by_card
                      : reinterpret_cast<const float *>(paired_fold_by_card.data());
              const __m256 compatible = _mm256_add_ps(
                  _mm256_sub_ps(
                      _mm256_sub_ps(
                          _mm256_set1_ps(fold_total),
                          _mm256_i32gather_ps(fold_by_card, first_cards, 4)),
                      _mm256_i32gather_ps(fold_by_card, second_cards, 4)),
                  fold_own_reaches);
              fold_result = _mm256_mul_ps(compatible, fold_scale);
              if constexpr (BoardLocal) {
                _mm256_storeu_ps(
                    reinterpret_cast<float *>(paired_fold_values_out->data()) + local,
                    fold_result);
              }
            }
            if constexpr (!BoardLocal) {
              alignas(32) float fold_result_lanes[8];
              if constexpr (PairedFold) {
                _mm256_store_ps(fold_result_lanes, fold_result);
              }
              for (std::size_t lane = 0U; lane < 8U; ++lane) {
                const auto output_slot = static_cast<std::size_t>(
                    updating_combos.own_slot[local + lane]);
                values_out[output_slot] = result_lanes[lane];
                if constexpr (PairedFold) {
                  (*paired_fold_values_out)[output_slot] =
                      fold_result_lanes[lane];
                }
              }
            }
          }
        }
        for (; local < updating_combos.size(); ++local) {
          const auto opponent_slot = BoardLocal
                                         ? updating_combos.opponent_local[local]
                                         : updating_combos.opponent_slot[local];
          const float own_reach = opponent_slot != TerminalComboData::invalid_slot
                                      ? opponent_reach[opponent_slot]
                                      : 0.0F;
          const float invalid_lower =
              card_prefix[updating_combos.first_by_rank[local]] +
              card_prefix[updating_combos.second_by_rank[local]];
          const float invalid_tie =
              by_card[updating_combos.first_by_rank[local]] +
              by_card[updating_combos.second_by_rank[local]] - own_reach;
          const float invalid_all =
              card_prefix[rank_count * 36U +
                          updating_combos.first_card[local]] +
              card_prefix[rank_count * 36U +
                          updating_combos.second_card[local]] -
              own_reach;
          const auto rank = updating_combos.rank[local];
          const auto output_slot = BoardLocal
                                       ? local
                                       : static_cast<std::size_t>(
                                             updating_combos.own_slot[local]);
          values_out[output_slot] =
              (rank_base[rank] +
               invalid_lower * static_cast<float>(loss_payoff - win_payoff) +
               invalid_tie * static_cast<float>(loss_payoff - tie_payoff) -
               invalid_all * static_cast<float>(loss_payoff)) *
              static_cast<float>(1.0 / layout_.initial_normalization);
          if constexpr (PairedFold) {
            const float fold_own_reach =
                paired_fold_opponent_reach != nullptr &&
                        opponent_slot != TerminalComboData::invalid_slot
                    ? static_cast<float>((*paired_fold_opponent_reach)[opponent_slot])
                    : own_reach;
            const float fold_total = paired_fold_shares_reach
                                         ? prefix[rank_count]
                                         : paired_fold_total;
            const float *const fold_by_card =
                paired_fold_shares_reach
                    ? card_prefix.data() + rank_count * 36U
                    : reinterpret_cast<const float *>(paired_fold_by_card.data());
            const float compatible =
                fold_total -
                fold_by_card[updating_combos.first_card[local]] -
                fold_by_card[updating_combos.second_card[local]] +
                fold_own_reach;
            (*paired_fold_values_out)[output_slot] =
                compatible * static_cast<float>(paired_fold_payoff /
                                                layout_.initial_normalization);
          }
        }
      } else if constexpr (PlayerIndexed) {
        const auto &updating_combos = board.terminal_combos[updating_player];
        std::size_t local = 0U;
        for (; local + 4U <= updating_combos.size(); local += 4U) {
          const auto load_indices = [local](const std::vector<std::uint16_t> &source) {
            return _mm_cvtepu16_epi32(_mm_loadl_epi64(
                reinterpret_cast<const __m128i *>(source.data() + local)));
          };
          const __m128i opponent_indices = load_indices(
              BoardLocal ? updating_combos.opponent_local
                         : updating_combos.opponent_slot);
          const __m128i valid_slots = _mm_cmpgt_epi32(
              _mm_set1_epi32(static_cast<int>(TerminalComboData::invalid_slot)),
              opponent_indices);
          const __m256d own_reaches = _mm256_mask_i32gather_pd(
              _mm256_setzero_pd(), opponent_reach.data(), opponent_indices,
              _mm256_castsi256_pd(_mm256_cvtepi32_epi64(valid_slots)), 8);
          const __m128i first_by_rank =
              load_indices(updating_combos.first_by_rank);
          const __m128i second_by_rank =
              load_indices(updating_combos.second_by_rank);
          const __m256d invalid_lower = _mm256_add_pd(
              _mm256_i32gather_pd(card_prefix.data(), first_by_rank, 8),
              _mm256_i32gather_pd(card_prefix.data(), second_by_rank, 8));
          const __m256d invalid_tie = _mm256_sub_pd(
              _mm256_add_pd(
                  _mm256_i32gather_pd(by_card.data(), first_by_rank, 8),
                  _mm256_i32gather_pd(by_card.data(), second_by_rank, 8)),
              own_reaches);
          const __m128i ranks = load_indices(updating_combos.rank);
          const __m128i rank_offsets = _mm_mullo_epi32(
              ranks, _mm_set1_epi32(36));
          const __m128i first_cards =
              _mm_sub_epi32(first_by_rank, rank_offsets);
          const __m128i second_cards =
              _mm_sub_epi32(second_by_rank, rank_offsets);
          const auto *const all_by_card =
              card_prefix.data() + rank_count * 36U;
          const __m256d invalid_all = _mm256_sub_pd(
              _mm256_add_pd(
                  _mm256_i32gather_pd(all_by_card, first_cards, 8),
                  _mm256_i32gather_pd(all_by_card, second_cards, 8)),
              own_reaches);
          __m256d numerator =
              _mm256_i32gather_pd(rank_base.data(), ranks, 8);
          numerator = _mm256_add_pd(
              numerator,
              _mm256_mul_pd(invalid_lower,
                            _mm256_set1_pd(loss_payoff - win_payoff)));
          numerator = _mm256_add_pd(
              numerator,
              _mm256_mul_pd(invalid_tie,
                            _mm256_set1_pd(loss_payoff - tie_payoff)));
          numerator = _mm256_sub_pd(
              numerator,
              _mm256_mul_pd(invalid_all, _mm256_set1_pd(loss_payoff)));
          const __m256d result = _mm256_div_pd(
              numerator, _mm256_set1_pd(layout_.initial_normalization));
          if (layout_.uses_canonical_public_dag) {
            alignas(32) double result_lanes[4];
            _mm256_store_pd(result_lanes, result);
            for (std::size_t lane = 0U; lane < 4U; ++lane) {
              values_out[updating_combos.own_slot[local + lane]] =
                  static_cast<Scalar>(result_lanes[lane]);
            }
          } else {
            store_four_from_double(values_out.data() + local, result);
          }
          if constexpr (PairedFold) {
            const __m256d fold_own_reaches =
                paired_fold_opponent_reach != nullptr
                    ? _mm256_mask_i32gather_pd(
                          _mm256_setzero_pd(),
                          paired_fold_opponent_reach->data(), opponent_indices,
                          _mm256_castsi256_pd(
                              _mm256_cvtepi32_epi64(valid_slots)),
                          8)
                    : own_reaches;
            const double fold_total = paired_fold_shares_reach
                                          ? prefix[rank_count]
                                          : paired_fold_total;
            const double *const fold_by_card =
                paired_fold_shares_reach
                    ? all_by_card
                    : reinterpret_cast<const double *>(paired_fold_by_card.data());
            const __m256d compatible = _mm256_add_pd(
                _mm256_sub_pd(
                    _mm256_sub_pd(
                        _mm256_set1_pd(fold_total),
                        _mm256_i32gather_pd(fold_by_card, first_cards, 8)),
                    _mm256_i32gather_pd(fold_by_card, second_cards, 8)),
                fold_own_reaches);
            const __m256d fold_result = _mm256_div_pd(
                _mm256_mul_pd(compatible,
                              _mm256_set1_pd(paired_fold_payoff)),
                _mm256_set1_pd(layout_.initial_normalization));
            if (layout_.uses_canonical_public_dag) {
              alignas(32) double fold_lanes[4];
              _mm256_store_pd(fold_lanes, fold_result);
              for (std::size_t lane = 0U; lane < 4U; ++lane) {
                (*paired_fold_values_out)
                    [updating_combos.own_slot[local + lane]] =
                        static_cast<Scalar>(fold_lanes[lane]);
              }
            } else {
              store_four_from_double(
                  paired_fold_values_out->data() + local, fold_result);
            }
          }
        }
        for (; local < updating_combos.size(); ++local) {
          const auto opponent_slot = updating_combos.opponent_slot[local];
          const double own_reach = opponent_slot != TerminalComboData::invalid_slot
                                       ? opponent_reach[opponent_slot]
                                       : 0.0;
          const double invalid_lower =
              card_prefix[updating_combos.first_by_rank[local]] +
              card_prefix[updating_combos.second_by_rank[local]];
          const double invalid_tie =
              by_card[updating_combos.first_by_rank[local]] +
              by_card[updating_combos.second_by_rank[local]] - own_reach;
          const double invalid_all =
              card_prefix[rank_count * 36U +
                          updating_combos.first_card[local]] +
              card_prefix[rank_count * 36U +
                          updating_combos.second_card[local]] -
              own_reach;
          const auto combo_rank = updating_combos.rank[local];
          const double numerator =
              rank_base[combo_rank] +
              invalid_lower * (loss_payoff - win_payoff) +
              invalid_tie * (loss_payoff - tie_payoff) -
              invalid_all * loss_payoff;
          const auto output_slot = layout_.uses_canonical_public_dag
                                       ? static_cast<std::size_t>(
                                             updating_combos.own_slot[local])
                                       : local;
          values_out[output_slot] =
              static_cast<Scalar>(numerator / layout_.initial_normalization);
          if constexpr (PairedFold) {
            const double fold_own_reach =
                paired_fold_opponent_reach != nullptr &&
                        opponent_slot != TerminalComboData::invalid_slot
                    ? (*paired_fold_opponent_reach)[opponent_slot]
                    : own_reach;
            const double fold_total = paired_fold_shares_reach
                                          ? prefix[rank_count]
                                          : paired_fold_total;
            const double *const fold_by_card =
                paired_fold_shares_reach
                    ? card_prefix.data() + rank_count * 36U
                    : reinterpret_cast<const double *>(paired_fold_by_card.data());
            const double compatible =
                fold_total - fold_by_card[updating_combos.first_card[local]] -
                fold_by_card[updating_combos.second_card[local]] + fold_own_reach;
            (*paired_fold_values_out)[output_slot] = static_cast<Scalar>(
                compatible * paired_fold_payoff / layout_.initial_normalization);
          }
        }
      } else {
        const auto &updating_combos = board.terminal_active_combos;
        std::size_t local = 0U;
        if constexpr (std::is_same_v<Scalar, float>) {
          const __m256 win = _mm256_set1_ps(static_cast<float>(win_payoff));
          const __m256 tie = _mm256_set1_ps(static_cast<float>(tie_payoff));
          const __m256 loss = _mm256_set1_ps(static_cast<float>(loss_payoff));
          const __m256 inverse_normalization = _mm256_set1_ps(
              static_cast<float>(1.0 / layout_.initial_normalization));
          const __m256 total = _mm256_set1_ps(prefix[rank_count]);
          for (; local + 8U <= updating_combos.size(); local += 8U) {
            const auto load_indices = [local](const std::vector<std::uint16_t> &source) {
              return _mm256_cvtepu16_epi32(_mm_loadu_si128(
                  reinterpret_cast<const __m128i *>(source.data() + local)));
            };
            const __m256i slots = load_indices(updating_combos.own_slot);
            const __m256 own_reaches =
                _mm256_i32gather_ps(opponent_reach.data(), slots, 4);
            const __m256i first_by_rank =
                load_indices(updating_combos.first_by_rank);
            const __m256i second_by_rank =
                load_indices(updating_combos.second_by_rank);
            const __m256 invalid_lower = _mm256_add_ps(
                _mm256_i32gather_ps(card_prefix.data(), first_by_rank, 4),
                _mm256_i32gather_ps(card_prefix.data(), second_by_rank, 4));
            const __m256 invalid_tie = _mm256_sub_ps(
                _mm256_add_ps(
                    _mm256_i32gather_ps(by_card.data(), first_by_rank, 4),
                    _mm256_i32gather_ps(by_card.data(), second_by_rank, 4)),
                own_reaches);
            const __m256i first_all = load_indices(updating_combos.first_all);
            const __m256i second_all = load_indices(updating_combos.second_all);
            const __m256 invalid_all = _mm256_sub_ps(
                _mm256_add_ps(
                    _mm256_i32gather_ps(card_prefix.data(), first_all, 4),
                    _mm256_i32gather_ps(card_prefix.data(), second_all, 4)),
                own_reaches);
            const __m256i ranks = load_indices(updating_combos.rank);
            const __m256 lower = _mm256_sub_ps(
                _mm256_i32gather_ps(prefix.data(), ranks, 4), invalid_lower);
            const __m256 equal = _mm256_sub_ps(
                _mm256_i32gather_ps(totals.data(), ranks, 4), invalid_tie);
            const __m256 invalid_higher = _mm256_sub_ps(
                _mm256_sub_ps(invalid_all, invalid_lower), invalid_tie);
            const __m256 higher = _mm256_sub_ps(
                _mm256_sub_ps(
                    total,
                    _mm256_i32gather_ps(
                        prefix.data(),
                        _mm256_add_epi32(ranks, _mm256_set1_epi32(1)), 4)),
                invalid_higher);
            __m256 numerator = _mm256_mul_ps(lower, win);
            if (tie_payoff != 0.0) {
              numerator = _mm256_add_ps(numerator, _mm256_mul_ps(equal, tie));
            }
            numerator = _mm256_add_ps(numerator, _mm256_mul_ps(higher, loss));
            const __m256 result = _mm256_mul_ps(numerator, inverse_normalization);
            alignas(32) float result_lanes[8];
            _mm256_store_ps(result_lanes, result);
            for (std::size_t lane = 0U; lane < 8U; ++lane) {
              values_out[updating_combos.own_slot[local + lane]] = result_lanes[lane];
            }
          }
          for (; local < updating_combos.size(); ++local) {
            const auto slot = updating_combos.own_slot[local];
            const float own_reach = opponent_reach[slot];
            const float invalid_lower =
                card_prefix[updating_combos.first_by_rank[local]] +
                card_prefix[updating_combos.second_by_rank[local]];
            const float invalid_tie =
                by_card[updating_combos.first_by_rank[local]] +
                by_card[updating_combos.second_by_rank[local]] - own_reach;
            const float invalid_all =
                card_prefix[updating_combos.first_all[local]] +
                card_prefix[updating_combos.second_all[local]] - own_reach;
            const auto rank = updating_combos.rank[local];
            const float lower = prefix[rank] - invalid_lower;
            const float equal = totals[rank] - invalid_tie;
            const float higher = (prefix[rank_count] - prefix[rank + 1U]) -
                                 (invalid_all - invalid_lower - invalid_tie);
            values_out[slot] =
                (lower * static_cast<float>(win_payoff) +
                 equal * static_cast<float>(tie_payoff) +
                 higher * static_cast<float>(loss_payoff)) *
                static_cast<float>(1.0 / layout_.initial_normalization);
          }
        } else {
        alignas(32) double result_lanes[4];
        for (; local + 4U <= updating_combos.size(); local += 4U) {
          const auto load_indices = [local](const std::vector<std::uint16_t> &source) {
            return _mm_cvtepu16_epi32(_mm_loadl_epi64(
                reinterpret_cast<const __m128i *>(source.data() + local)));
          };
          const __m128i slots = load_indices(updating_combos.own_slot);
          const __m256d own_reaches =
              gather_four_as_double(opponent_reach.data(), slots);
          const __m128i first_by_rank =
              load_indices(updating_combos.first_by_rank);
          const __m128i second_by_rank =
              load_indices(updating_combos.second_by_rank);
          const __m256d invalid_lower = _mm256_add_pd(
              _mm256_i32gather_pd(card_prefix.data(), first_by_rank, 8),
              _mm256_i32gather_pd(card_prefix.data(), second_by_rank, 8));
          const __m256d invalid_tie = _mm256_sub_pd(
              _mm256_add_pd(
                  _mm256_i32gather_pd(by_card.data(), first_by_rank, 8),
                  _mm256_i32gather_pd(by_card.data(), second_by_rank, 8)),
              own_reaches);
          const __m128i first_all = load_indices(updating_combos.first_all);
          const __m128i second_all = load_indices(updating_combos.second_all);
          const __m256d invalid_all = _mm256_sub_pd(
              _mm256_add_pd(
                  _mm256_i32gather_pd(card_prefix.data(), first_all, 8),
                  _mm256_i32gather_pd(card_prefix.data(), second_all, 8)),
              own_reaches);
          const __m128i ranks = load_indices(updating_combos.rank);
          const __m256d lower = _mm256_sub_pd(
              _mm256_i32gather_pd(prefix.data(), ranks, 8), invalid_lower);
          const __m256d equal = _mm256_sub_pd(
              _mm256_i32gather_pd(totals.data(), ranks, 8), invalid_tie);
          const __m256d invalid_higher = _mm256_sub_pd(
              _mm256_sub_pd(invalid_all, invalid_lower), invalid_tie);
          const __m256d higher = _mm256_sub_pd(
              _mm256_sub_pd(
                  _mm256_set1_pd(prefix[rank_count]),
                  _mm256_i32gather_pd(
                      prefix.data(),
                      _mm_add_epi32(ranks, _mm_set1_epi32(1)), 8)),
              invalid_higher);
          __m256d numerator =
              _mm256_mul_pd(lower, _mm256_set1_pd(win_payoff));
          if (tie_payoff != 0.0) {
            numerator = _mm256_add_pd(
                numerator,
                _mm256_mul_pd(equal, _mm256_set1_pd(tie_payoff)));
          }
          numerator = _mm256_add_pd(
              numerator,
              _mm256_mul_pd(higher, _mm256_set1_pd(loss_payoff)));
          _mm256_store_pd(
              result_lanes,
              _mm256_div_pd(numerator,
                            _mm256_set1_pd(layout_.initial_normalization)));
          for (std::size_t lane = 0U; lane < 4U; ++lane) {
            values_out[updating_combos.own_slot[local + lane]] =
                static_cast<Scalar>(result_lanes[lane]);
          }
        }
        for (; local < updating_combos.size(); ++local) {
          const auto slot = updating_combos.own_slot[local];
          const double own_reach = opponent_reach[slot];
          const double invalid_lower =
              card_prefix[updating_combos.first_by_rank[local]] +
              card_prefix[updating_combos.second_by_rank[local]];
          const double invalid_tie =
              by_card[updating_combos.first_by_rank[local]] +
              by_card[updating_combos.second_by_rank[local]] - own_reach;
          const double invalid_all =
              card_prefix[updating_combos.first_all[local]] +
              card_prefix[updating_combos.second_all[local]] - own_reach;
          const auto rank = updating_combos.rank[local];
          const double lower = prefix[rank] - invalid_lower;
          const double equal = totals[rank] - invalid_tie;
          const double higher =
              (prefix[rank_count] - prefix[rank + 1U]) - (invalid_all - invalid_lower - invalid_tie);
          double numerator = lower * win_payoff;
          if (tie_payoff != 0.0) {
            numerator += equal * tie_payoff;
          }
          numerator += higher * loss_payoff;
          values_out[slot] =
              static_cast<Scalar>(numerator / layout_.initial_normalization);
        }
        }
      }
      if (profile) {
        prof_showdown_output_seconds_ +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t_output).count();
      }
    };
    // Reuse the member scratch arrays (sized for the maximum rank space, see
    // the member declarations): no per-node heap allocation (the old code
    // heap-allocated ~5.4 KB per showdown node, ~4.4 M allocations per run).
    auto &totals = [&]() -> auto & {
      if constexpr (std::is_same_v<Scalar, float>) {
        return showdown_totals_float_;
      } else {
        return showdown_totals_;
      }
    }();
    auto &by_card = [&]() -> auto & {
      if constexpr (std::is_same_v<Scalar, float>) {
        return showdown_by_card_float_;
      } else {
        return showdown_by_card_;
      }
    }();
    auto &prefix = [&]() -> auto & {
      if constexpr (std::is_same_v<Scalar, float>) {
        return showdown_prefix_float_;
      } else {
        return showdown_prefix_;
      }
    }();
    auto &card_prefix = [&]() -> auto & {
      if constexpr (std::is_same_v<Scalar, float>) {
        return showdown_card_prefix_float_;
      } else {
        return showdown_card_prefix_;
      }
    }();
    using ScratchScalar = typename std::remove_reference_t<decltype(totals)>::value_type;
    std::fill_n(totals.begin(), rank_count, ScratchScalar{0});
    // Every prefix entry after the base is overwritten by `calculate`; only
    // the scalar base and the 36-card base row need initialization. Clearing
    // the complete rank-by-card prefix here wrote thousands of dead doubles
    // at every showdown terminal.
    prefix[0] = ScratchScalar{0};
    std::fill_n(card_prefix.begin(), 36U, ScratchScalar{0});
    calculate(totals, by_card, prefix, card_prefix);
    // `showdown_by_card_` starts zeroed and only these cells are modified by
    // `calculate`. Restore each distinct cell once: replaying both cells for
    // every combo performs many duplicate stores on equal (rank, card) pairs.
    if constexpr (PlayerIndexed) {
      const auto &opponent_combos = board.terminal_combos[opponent];
      for (const auto cell : opponent_combos.touched_by_rank_card) {
        by_card[cell] = ScratchScalar{0};
      }
    } else {
      const auto &opponent_combos = board.terminal_active_combos;
      for (const auto cell : opponent_combos.touched_by_rank_card) {
        by_card[cell] = ScratchScalar{0};
      }
    }
    return std::nullopt;
  }

  Result<ComboVector, PostflopSolverError>
  showdown_values_with_payoffs(const std::uint32_t board_index, const double win_payoff,
                               const double tie_payoff, const double loss_payoff,
                               const std::uint8_t updating_player,
                               const ComboVector &opponent_reach) {
    ComboVector values{};
    if (const auto error = showdown_values_with_payoffs_into(
            board_index, win_payoff, tie_payoff, loss_payoff, updating_player, opponent_reach,
            values, 0.0, nullptr)) {
      return Result<ComboVector, PostflopSolverError>::failure(*error);
    }
    return Result<ComboVector, PostflopSolverError>::success(std::move(values));
  }

  // Returns a zero-initialized value vector. In the per-player path only the
  // updating player's flop-range prefix (slots 0..player_flop_count[player])
  // can ever be read, so only that prefix is zeroed; every reader in this
  // path iterates player_combos lists or bounds by player_flop_count.
#pragma warning(push)
#pragma warning(disable : 4701)
  [[nodiscard]] ComboVector zeroed_values(const std::uint8_t updating_player) const {
    ComboVector values;
    if constexpr (PlayerIndexed) {
      std::fill_n(values.begin(), layout_.player_flop_count[updating_player], Scalar{0});
    } else {
      values.fill(Scalar{0});
    }
    return values;
  }
#pragma warning(pop)

  // Zeroes the active prefix of an out-parameter value buffer (per-player
  // compact slots are 0..player_flop_count[player]-1 in the PlayerIndexed
  // path; the full array otherwise). Every callee that writes through an
  // out-param must zero its destination first so that slots not written
  // (blocked combos) read as zero in the parent's accumulation.
  void zero_values_into(const std::uint8_t updating_player, ComboVector &values) const {
    if constexpr (PlayerIndexed) {
      std::fill_n(values.begin(), layout_.player_flop_count[updating_player], Scalar{0});
    } else {
      values.fill(Scalar{0});
    }
  }

  void zero_board_values_into(const BoardData &board, const std::uint8_t updating_player,
                              ComboVector &values) const {
    if constexpr (PlayerIndexed) {
      std::fill_n(values.begin(), board.player_combos[updating_player].size(), Scalar{0});
    } else {
      values.fill(Scalar{0});
    }
  }

  [[nodiscard]] ComboVector zeroed_board_values(const BoardData &board,
                                                 const std::uint8_t updating_player) const {
    ComboVector values;
    zero_board_values_into(board, updating_player, values);
    return values;
  }

  std::array<ComboVector, 2> block_card(const ReachRef &reach,
                                        const BoardData &board, const CardId card) const {
    if constexpr (PlayerIndexed) {
      // Per-player compact spaces: slots 0..player_flop_count[p]-1 are the
      // player's flop-range combos (contiguous), so the copy is a pair of
      // contiguous prefix copies; then zero the slots blocked by the new card.
      // Slots beyond each prefix are never read in this path.
      std::array<ComboVector, 2> blocked;
      std::copy_n(reach[0]->begin(), layout_.player_flop_count[0], blocked[0].begin());
      std::copy_n(reach[1]->begin(), layout_.player_flop_count[1], blocked[1].begin());
      for (const ComboId combo : board.player_combos[0]) {
        if ((layout_.combo_masks[combo] & card.mask()) != 0U) {
          blocked[0][value_slot(combo, 0)] = 0.0;
        }
      }
      for (const ComboId combo : board.player_combos[1]) {
        if ((layout_.combo_masks[combo] & card.mask()) != 0U) {
          blocked[1][value_slot(combo, 1)] = 0.0;
        }
      }
      return blocked;
    }
    std::array<ComboVector, 2> blocked;
    blocked[0] = *reach[0];
    blocked[1] = *reach[1];
    static_cast<void>(board);
    if constexpr (Capacity != combo_count) {
      for (const std::uint16_t slot : layout_.active_slots_by_card[card.value()]) {
        blocked[0][slot] = 0.0;
        blocked[1][slot] = 0.0;
      }
    } else {
      for (const ComboId combo : layout_.active_combos_by_card[card.value()]) {
        blocked[0][combo] = 0.0;
        blocked[1][combo] = 0.0;
      }
    }
    return blocked;
  }

  // Copies the reach arrays. With per-player compact spaces (PlayerIndexed)
  // each player's live slots are a contiguous prefix, so the copy is two
  // contiguous prefix copies (~4.5 KB instead of ~10 KB); otherwise a full
  // copy preserves the zero-initialized state (identical to `auto copy =
  // reach;`).
  std::array<ComboVector, 2> copy_reach(const ReachRef &reach,
                                        const BoardData &board) const {
    static_cast<void>(board);
    if constexpr (PlayerIndexed) {
      std::array<ComboVector, 2> copy;
      std::copy_n(reach[0]->begin(), layout_.player_flop_count[0], copy[0].begin());
      std::copy_n(reach[1]->begin(), layout_.player_flop_count[1], copy[1].begin());
      return copy;
    }
    std::array<ComboVector, 2> copy;
    copy[0] = *reach[0];
    copy[1] = *reach[1];
    return copy;
  }

#if 0
  // Rejected experiment retained temporarily for historical comparison only.
  // It is excluded from production builds and will be removed after the
  // alternating traversal rewrite is complete.
  // Exact simultaneous CFR traversal for the physical, per-player layout.
  // Both counterfactual value vectors use the same regret-matched strategy
  // snapshot at every decision. Regret and average-strategy writes belong to
  // the acting player's disjoint action block, so no second public-tree walk
  // or copied state buffer is required.
  std::optional<PostflopSolverError> cfr_physical_simultaneous(
      const NodeId node_id, const ReachRef &reach, const double strategy_weight,
      std::array<ComboVector, 2> &values_out) {
    const auto &node = layout_.tree.nodes[static_cast<std::size_t>(node_id)];
    if (node.kind == PublicNodeKind::TerminalFold ||
        node.kind == PublicNodeKind::TerminalShowdown) {
      for (std::uint8_t player = 0; player < 2U; ++player) {
        const auto error =
            node.kind == PublicNodeKind::TerminalFold
                ? fold_values_into(node, player, *reach[1U - player], values_out[player])
                : showdown_values_into(node, player, *reach[1U - player], values_out[player]);
        if (error) {
          return error;
        }
      }
      return std::nullopt;
    }

    const auto &board = layout_.boards[layout_.node_board[static_cast<std::size_t>(node.id)]];
    if (node.kind == PublicNodeKind::Chance) {
      if (node.edges.empty() || node.edges.front().total_legal_outcome_count <= 4U) {
        return PostflopSolverError::InvalidConfiguration;
      }
      for (std::uint8_t player = 0; player < 2U; ++player) {
        zero_board_values_into(board, player, values_out[player]);
      }
      const double denominator =
          static_cast<double>(node.edges.front().total_legal_outcome_count - 4U);
      const auto accumulate = [&](const PublicTreeEdge &edge,
                                  const std::array<ComboVector, 2> &child) {
        const double probability =
            static_cast<double>(edge.physical_outcome_count) / denominator;
        const auto &child_board =
            layout_.boards[layout_.node_board[static_cast<std::size_t>(edge.child)]];
        for (std::uint8_t player = 0; player < 2U; ++player) {
          const auto &child_combos = child_board.player_combos[player];
          for (std::size_t child_slot = 0; child_slot < child_combos.size(); ++child_slot) {
            const auto parent_slot = board.player_local[player][child_combos[child_slot]];
            values_out[player][static_cast<std::size_t>(parent_slot)] +=
                probability * child[player][child_slot];
          }
        }
      };
      const std::size_t edge_count = node.edges.size();
      const std::size_t worker_count = parallel_workers_.size();
      const std::size_t split =
          std::popcount(board.mask) == 3U && worker_count > 0U
              ? edge_count - edge_count / (worker_count + 1U)
              : 0U;
      if (split == 0U) {
        std::array<ComboVector, 2> child;
        for (const auto &edge : node.edges) {
          if (const auto error =
                  cfr_physical_simultaneous(edge.child, reach, strategy_weight, child)) {
            return error;
          }
          accumulate(edge, child);
        }
        return std::nullopt;
      }
      std::vector<std::array<ComboVector, 2>> child_results(edge_count);
      std::vector<std::packaged_task<TraversalResult(DenseTraversal &)>> tasks;
      std::vector<std::future<TraversalResult>> futures;
      tasks.reserve(split);
      futures.reserve(split);
      for (std::size_t index = 0; index < split; ++index) {
        std::packaged_task<TraversalResult(DenseTraversal &)> task(
            [child = node.edges[index].child, reach, strategy_weight,
             result = &child_results[index]](DenseTraversal &self) {
              const auto error =
                  self.cfr_physical_simultaneous(child, reach, strategy_weight, *result);
              if (error) {
                return Result<ComboVector, PostflopSolverError>::failure(*error);
              }
              return Result<ComboVector, PostflopSolverError>::success(ComboVector{});
            });
        futures.push_back(task.get_future());
        tasks.push_back(std::move(task));
      }
      for (std::size_t index = 0; index < split; ++index) {
        parallel_workers_[index % worker_count]->dispatch_parallel_task(
            std::move(tasks[index]));
      }
      for (std::size_t index = split; index < edge_count; ++index) {
        if (const auto error = cfr_physical_simultaneous(
                node.edges[index].child, reach, strategy_weight, child_results[index])) {
          return error;
        }
      }
      for (auto &future : futures) {
        while (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
          if (!try_pull_and_run()) {
            ParallelTaskQueue *const queue = parallel_shared_.get();
            std::unique_lock lock(queue->mutex);
            queue->ready.wait(lock, [&] {
              return !queue->tasks.empty() ||
                     future.wait_for(std::chrono::seconds(0)) ==
                         std::future_status::ready;
            });
          }
        }
        const auto completed = future.get();
        if (!completed) {
          return completed.error();
        }
      }
      for (std::size_t index = 0; index < edge_count; ++index) {
        accumulate(node.edges[index], child_results[index]);
      }
      return std::nullopt;
    }

    if (node.kind != PublicNodeKind::Decision) {
      return PostflopSolverError::InvalidConfiguration;
    }
    const auto &decision = layout_.decisions[static_cast<std::size_t>(node.id)];
    const auto actor = decision.player;
    const auto action_count = static_cast<std::size_t>(decision.action_count);
    if (action_count == 0U || action_count > maximum_action_count) {
      return PostflopSolverError::InvalidConfiguration;
    }
    SimultaneousScratchLease scratch_lease(*this);
    auto &scratch = scratch_lease.get();
    const auto &actor_combos = board.player_combos[actor];
    const bool locked_node = is_locked_root(node);
    if (!locked_node && buffers_.regret_float24 != nullptr) {
      const auto *const regrets =
          buffers_.regret_float24 + static_cast<std::size_t>(decision.action_base) * 3U;
      for (std::size_t local = 0; local < actor_combos.size(); ++local) {
        const auto *const block = regrets + local * action_count * 3U;
        double sum = 0.0;
        for (std::size_t action = 0; action < action_count; ++action) {
          const double value = static_cast<double>(decode_float24(block + action * 3U));
          scratch.strategies[action][local] = value;
          sum += value;
        }
        if (sum <= 0.0) {
          const double uniform = 1.0 / static_cast<double>(action_count);
          for (std::size_t action = 0; action < action_count; ++action) {
            scratch.strategies[action][local] = uniform;
          }
        } else {
          const double inverse = 1.0 / sum;
          for (std::size_t action = 0; action < action_count; ++action) {
            scratch.strategies[action][local] *= inverse;
          }
        }
      }
    } else {
      for (std::size_t local = 0; local < actor_combos.size(); ++local) {
        const auto combo = actor_combos[local];
        const auto locked = locked_node ? locked_root_strategy(board.local_index[combo])
                                        : std::nullopt;
        const auto strategy = locked
                                  ? *locked
                                  : current_strategy(decision,
                                                     static_cast<std::int16_t>(local), false);
        for (std::size_t action = 0; action < action_count; ++action) {
          scratch.strategies[action][local] = strategy[action];
        }
      }
    }
    for (std::size_t action = 0; action < action_count; ++action) {
      auto &actor_reach = scratch.actor_reach[action];
      for (std::size_t local = 0; local < actor_combos.size(); ++local) {
        const auto reach_slot =
            static_cast<std::size_t>(board.player_flop_slots[actor][local]);
        actor_reach[reach_slot] =
            (*reach[actor])[reach_slot] * scratch.strategies[action][local];
      }
      const ReachRef child_reach =
          actor == 0U ? ReachRef{&actor_reach, reach[1]} : ReachRef{reach[0], &actor_reach};
      std::array<ComboVector, 2> child_values;
      if (const auto error = cfr_physical_simultaneous(
              node.edges[action].child, child_reach, strategy_weight, child_values)) {
        return error;
      }
      scratch.action_values[0][action] = std::move(child_values[0]);
      scratch.action_values[1][action] = std::move(child_values[1]);
    }

    for (std::uint8_t player = 0; player < 2U; ++player) {
      zero_board_values_into(board, player, values_out[player]);
      const auto combo_count_for_player = board.player_combos[player].size();
      for (std::size_t local = 0; local < combo_count_for_player; ++local) {
        for (std::size_t action = 0; action < action_count; ++action) {
          values_out[player][local] +=
              player == actor
                  ? scratch.strategies[action][local] *
                        scratch.action_values[player][action][local]
                  : scratch.action_values[player][action][local];
        }
      }
    }

    if (!locked_node) {
      if (action_count == 2U && buffers_.regret_float24 != nullptr) {
        auto *const regrets =
            buffers_.regret_float24 + static_cast<std::size_t>(decision.action_base) * 3U;
        for (std::size_t local = 0; local < actor_combos.size(); ++local) {
          auto *const packed = regrets + local * 6U;
          std::uint64_t pair = 0U;
          for (std::size_t byte = 0; byte < 6U; ++byte) {
            pair |= static_cast<std::uint64_t>(packed[byte]) << (byte * 8U);
          }
          const auto first_bits = static_cast<std::uint32_t>(pair & 0x00ffffffULL) << 7U;
          const auto second_bits =
              static_cast<std::uint32_t>((pair >> 24U) & 0x00ffffffULL) << 7U;
          const double first = static_cast<double>(std::bit_cast<float>(first_bits));
          const double second = static_cast<double>(std::bit_cast<float>(second_bits));
          const double first_delta =
              regret_update_weight_ * (scratch.action_values[actor][0][local] -
                                       values_out[actor][local]);
          const double second_delta =
              regret_update_weight_ * (scratch.action_values[actor][1][local] -
                                       values_out[actor][local]);
          const std::uint64_t encoded =
              static_cast<std::uint64_t>(
                  encode_float24_bits(std::max(0.0, first + first_delta))) |
              (static_cast<std::uint64_t>(
                   encode_float24_bits(std::max(0.0, second + second_delta)))
               << 24U);
          std::memcpy(packed, &encoded, 6U);
        }
      } else {
        for (std::size_t local = 0; local < actor_combos.size(); ++local) {
          const auto base = static_cast<std::size_t>(decision.action_base) + local * action_count;
          for (std::size_t action = 0; action < action_count; ++action) {
            const auto index = base + action;
            const double delta = regret_update_weight_ *
                                 (scratch.action_values[actor][action][local] -
                                  values_out[actor][local]);
            if (buffers_.regret_float24 != nullptr) {
              auto *const packed = buffers_.regret_float24 + index * 3U;
              encode_float24(
                  packed, std::max(0.0, static_cast<double>(decode_float24(packed)) + delta));
            } else {
              buffers_.set_regret(index, std::max(0.0, buffers_.regret_at(index) + delta));
            }
          }
        }
      }
    }
    if (strategy_weight != 0.0) {
      for (std::size_t local = 0; local < actor_combos.size(); ++local) {
        const auto base = static_cast<std::size_t>(decision.action_base) + local * action_count;
        const auto reach_slot =
            static_cast<std::size_t>(board.player_flop_slots[actor][local]);
        const double weight = strategy_weight * (*reach[actor])[reach_slot];
        for (std::size_t action = 0; action < action_count; ++action) {
          add_strategy(base + action, weight * scratch.strategies[action][local]);
        }
      }
    }
    return std::nullopt;
  }


#endif

  std::optional<PostflopSolverError> cfr_chance(const PublicTreeNode &node,
                                                const std::uint8_t updating_player,
                                                const ReachRef &reach,
                                                const double strategy_weight,
                                                const bool updating_reach_nonzero,
                                                const double public_update_multiplicity,
                                                ComboVector &values_out,
                                                const PhysicalOrbitContext *orbit_context) {
    const auto &board = layout_.boards[layout_.node_board[static_cast<std::size_t>(node.id)]];
    zero_board_values_into(board, updating_player, values_out);
    if (node.edges.empty() || node.edges.front().total_legal_outcome_count <= 4U) {
      return PostflopSolverError::InvalidConfiguration;
    }
    const double denominator =
        static_cast<double>(node.edges.front().total_legal_outcome_count - 4U);
    const auto accumulate = [&](const PublicTreeEdge &edge, const ComboVector &child) {
      const double probability = static_cast<double>(edge.physical_outcome_count) / denominator;
      if constexpr (PlayerIndexed) {
        const auto &child_board =
            layout_.boards[layout_.node_board[static_cast<std::size_t>(edge.child)]];
        const auto &child_combos = child_board.player_combos[updating_player];
        for (std::size_t child_slot = 0; child_slot < child_combos.size(); ++child_slot) {
          const auto parent_slot = board.player_local[updating_player][child_combos[child_slot]];
          const auto parent = static_cast<std::size_t>(parent_slot);
          values_out[parent] =
              static_cast<Scalar>(values_out[parent] + probability * child[child_slot]);
        }
      } else {
        const auto &combos = board.player_combos[updating_player];
        for (std::size_t local = 0; local < combos.size(); ++local) {
          const auto combo_id = combos[local];
          if ((layout_.combo_masks[combo_id] & edge.chance_card.mask()) == 0U) {
            const auto slot = board_player_slot(board, updating_player, local);
            values_out[slot] =
                static_cast<Scalar>(values_out[slot] + probability * child[slot]);
          }
        }
      }
    };
    if (orbit_context != nullptr) {
      ComboVector child_values;
      for (const auto &representative_edge : node.edges) {
        PhysicalOrbitContext child_context;
        child_context.reserve(orbit_context->size());
        for (const auto &occurrence : *orbit_context) {
          const auto &occurrence_node =
              layout_.tree.nodes[static_cast<std::size_t>(occurrence.node)];
          bool matched = false;
          for (const auto &occurrence_edge : occurrence_node.edges) {
            const auto transformed_card = transform_card(
                occurrence_edge.chance_card,
                layout_.automorphisms[occurrence.physical_to_representative_automorphism].suits);
            if (transformed_card &&
                transformed_card.value() == representative_edge.chance_card) {
              child_context.push_back(
                  {occurrence_edge.child,
                   occurrence.physical_to_representative_automorphism});
              matched = true;
              break;
            }
          }
          if (!matched) {
            return PostflopSolverError::InvalidConfiguration;
          }
        }
        const auto child_reach = block_card(reach, board, representative_edge.chance_card);
        if (const auto error = cfr_physical(
                representative_edge.child, updating_player,
                {&child_reach[0], &child_reach[1]}, strategy_weight,
                updating_reach_nonzero, public_update_multiplicity, child_values,
                &child_context)) {
          return error;
        }
        accumulate(representative_edge, child_values);
      }
      return std::nullopt;
    }
    if constexpr (!PlayerIndexed) {
      if (std::popcount(board.mask) == 3U && layout_.uses_range_aware_physical_orbits) {
        std::vector<bool> consumed(node.edges.size(), false);
        for (std::size_t representative_index = 0U;
             representative_index < node.edges.size(); ++representative_index) {
          if (consumed[representative_index]) {
            continue;
          }
          const auto &representative_edge = node.edges[representative_index];
          const auto representative_reach =
              block_card(reach, board, representative_edge.chance_card);
          std::vector<std::pair<std::size_t, std::uint8_t>> orbit;
          orbit.emplace_back(representative_index, std::uint8_t{0});
          consumed[representative_index] = true;
          for (std::size_t candidate_index = representative_index + 1U;
               candidate_index < node.edges.size(); ++candidate_index) {
            if (consumed[candidate_index]) {
              continue;
            }
            const auto &candidate_edge = node.edges[candidate_index];
            for (std::size_t automorphism_index = 0U;
                 automorphism_index < layout_.automorphisms.size(); ++automorphism_index) {
              const auto transformed_card = transform_card(
                  candidate_edge.chance_card,
                  layout_.automorphisms[automorphism_index].suits);
              if (!transformed_card ||
                  transformed_card.value() != representative_edge.chance_card) {
                continue;
              }
              const auto transformed_reach = transform_reach(
                  block_card(reach, board, candidate_edge.chance_card),
                  static_cast<std::uint8_t>(automorphism_index));
              if (transformed_reach == representative_reach) {
                orbit.emplace_back(candidate_index,
                                   static_cast<std::uint8_t>(automorphism_index));
                consumed[candidate_index] = true;
              }
              break;
            }
          }
          ComboVector representative_values;
          PhysicalOrbitContext grouped_context;
          grouped_context.reserve(orbit.size());
          for (const auto [edge_index, automorphism_index] : orbit) {
            grouped_context.push_back(
                {node.edges[edge_index].child, automorphism_index});
          }
          if (const auto error = cfr_physical(
                  representative_edge.child, updating_player,
                  {&representative_reach[0], &representative_reach[1]},
                  strategy_weight, updating_reach_nonzero,
                  public_update_multiplicity, representative_values,
                  &grouped_context)) {
            return error;
          }
          static bool emitted_orbit_value_diagnostic = false;
          if (!emitted_orbit_value_diagnostic && orbit.size() > 1U) {
            const auto [diagnostic_edge_index, diagnostic_automorphism_index] = orbit[1U];
            const auto diagnostic_reach =
                block_card(reach, board, node.edges[diagnostic_edge_index].chance_card);
            ComboVector diagnostic_values;
            if (const auto error = cfr_physical(
                    node.edges[diagnostic_edge_index].child, updating_player,
                    {&diagnostic_reach[0], &diagnostic_reach[1]}, 0.0,
                    updating_reach_nonzero, 0.0, diagnostic_values)) {
              return error;
            }
            const auto transformed_values = transform_values_to_parent(
                representative_values, diagnostic_automorphism_index, updating_player);
            double maximum_value_difference = 0.0;
            for (const ComboId combo : board.player_combos[updating_player]) {
              if ((layout_.combo_masks[combo] &
                   node.edges[diagnostic_edge_index].chance_card.mask()) != 0U) {
                continue;
              }
              maximum_value_difference =
                  std::max(maximum_value_difference,
                           std::abs(static_cast<double>(diagnostic_values[combo]) -
                                    static_cast<double>(transformed_values[combo])));
            }
            std::fprintf(stderr,
                         "ORBIT_VALUE_DIAGNOSTIC player=%u maximum_difference=%.17g\n",
                         static_cast<unsigned>(updating_player), maximum_value_difference);
            emitted_orbit_value_diagnostic = true;
          }
          for (const auto [edge_index, automorphism_index] : orbit) {
            if (edge_index == representative_index) {
              accumulate(node.edges[edge_index], representative_values);
            } else {
              const auto physical_values =
                  transform_values_to_parent(representative_values, automorphism_index,
                                             updating_player);
              accumulate(node.edges[edge_index], physical_values);
            }
          }
        }
        return std::nullopt;
      }
    }
    // Coarse-grained parallel split at the turn chance (the first chance layer
    // after the flop betting tree): the per-card subtrees are fanned out over
    // the worker pool while the current thread keeps its own share. A leaf
    // traversal exposes this layer only while it owns the explicitly queued
    // root-action subtree; ordinary chance tasks stay non-nested.
    const std::size_t edge_count = node.edges.size();
    const std::size_t worker_count =
        parallel_workers_.empty() ? parallel_pool_size_ : parallel_workers_.size();
    const std::size_t split = (std::popcount(board.mask) == 3U && worker_count > 0U)
                                  ? edge_count
                                  : 0U;
    const bool profile = hotpath_profiling_enabled();
    if (split == 0U) {
      ComboVector child;
      for (const auto &edge : node.edges) {
        if (const auto error =
                cfr_physical(edge.child, updating_player, reach, strategy_weight,
                             updating_reach_nonzero, public_update_multiplicity, child)) {
          return error;
        }
        const auto t_chance = profile ? std::chrono::steady_clock::now()
                                      : std::chrono::steady_clock::time_point{};
        accumulate(edge, child);
        if (profile) {
          prof_chance_seconds_ +=
              std::chrono::duration<double>(std::chrono::steady_clock::now() - t_chance).count();
        }
      }
      return std::nullopt;
    }
    std::vector<std::packaged_task<TraversalResult(DenseTraversal &)>> tasks;
    std::vector<std::future<TraversalResult>> futures;
    tasks.reserve(split);
    futures.reserve(split);
    // Pre-sized result vectors: every child writes directly into its own slot
    // (the workers write disjoint slots of the main thread's vector, which
    // stay valid until the join below).
    auto chance_results =
        std::make_unique_for_overwrite<ComboVector[]>(edge_count);
    for (std::size_t index = 0; index < split; ++index) {
      const auto &edge = node.edges[index];
      const ComboVector *child_reach_0 = reach[0];
      const ComboVector *child_reach_1 = reach[1];
      std::packaged_task<TraversalResult(DenseTraversal &)> task(
          [child = edge.child, updating_player, child_reach_0, child_reach_1, strategy_weight,
           updating_reach_nonzero, public_update_multiplicity,
           result = &chance_results[index]](DenseTraversal &self) {
            const auto error = self.cfr_physical(
                child, updating_player, {child_reach_0, child_reach_1}, strategy_weight,
                updating_reach_nonzero, public_update_multiplicity, *result);
            if (error) {
              return Result<ComboVector, PostflopSolverError>::failure(*error);
            }
            return Result<ComboVector, PostflopSolverError>::success(ComboVector{});
          });
      futures.push_back(task.get_future());
      tasks.push_back(std::move(task));
    }
    const auto t_sync = profile ? std::chrono::steady_clock::now()
                                : std::chrono::steady_clock::time_point{};
    dispatch_parallel_tasks(tasks);
    if (profile) {
      prof_sync_seconds_ +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() - t_sync).count();
    }
    // Both the worker subtrees and the main thread's share are joined, then accumulated in the original edge
    // order so the floating-point summation is bit-identical to the serial
    // traversal regardless of where the split boundary lies.
    for (std::size_t index = split; index < edge_count; ++index) {
      const auto &edge = node.edges[index];
      const auto error = cfr_physical(edge.child, updating_player, reach,
                                      strategy_weight, updating_reach_nonzero,
                                      public_update_multiplicity,
                                      chance_results[index]);
      if (error) {
        return error;
      }
    }
    for (std::size_t index = 0; index < split; ++index) {
      auto &future = futures[index];
      // Work-stealing wait: keep the pool busy with other pending subtrees
      // instead of blocking on this future. When the shared queue is empty,
      // block briefly on its condition variable instead of busy-spinning.
      wait_for_parallel_future(future);
      auto child = future.get();
      if (!child) {
        return child.error();
      }
    }
    const auto t_chance = profile ? std::chrono::steady_clock::now()
                                  : std::chrono::steady_clock::time_point{};
    for (std::size_t index = 0; index < edge_count; ++index) {
      accumulate(node.edges[index], chance_results[index]);
    }
    if (profile) {
      prof_chance_seconds_ +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() - t_chance).count();
    }
    return std::nullopt;
  }

  std::optional<PostflopSolverError> cfr_decision(const PublicTreeNode &node,
                                                   const std::uint8_t updating_player,
                                                   const ReachRef &reach,
                                                   const double strategy_weight,
                                                   const bool updating_reach_nonzero,
                                                   const double public_update_multiplicity,
                                                   ComboVector &values_out,
                                                   const PhysicalOrbitContext *orbit_context) {
    const auto &decision = layout_.decisions[static_cast<std::size_t>(node.id)];
    const auto &board = layout_.boards[decision.board_index];
    const auto action_count = static_cast<std::size_t>(decision.action_count);
    std::array<PhysicalOrbitContext, maximum_action_count> child_orbit_contexts;
    if (orbit_context != nullptr) {
      for (std::size_t action = 0U; action < action_count; ++action) {
        auto &child_context = child_orbit_contexts[action];
        child_context.reserve(orbit_context->size());
        for (const auto &occurrence : *orbit_context) {
          const auto &occurrence_node =
              layout_.tree.nodes[static_cast<std::size_t>(occurrence.node)];
          if (occurrence_node.kind != PublicNodeKind::Decision ||
              occurrence_node.edges.size() != node.edges.size()) {
            return PostflopSolverError::InvalidConfiguration;
          }
          child_context.push_back(
              {occurrence_node.edges[action].child,
               occurrence.physical_to_representative_automorphism});
        }
      }
    }
    DecisionScratchLease scratch_lease(*this);
    auto &action_values = scratch_lease.get().action_values;
    auto &strategies = scratch_lease.get().strategies;
    const double effective_regret_update_weight =
        regret_update_weight_ * public_update_multiplicity;
    const bool profile = hotpath_profiling_enabled();
    const auto t_begin = profile ? std::chrono::steady_clock::now()
                                 : std::chrono::steady_clock::time_point{};
    // Strategies are only meaningful for the acting player's own live combos
    // (board.player_combos[decision.player]); for every other combo the
    // actor's reach is zero, so its strategy never contributes. Only the
    // updating player's live combos (board.player_combos[updating_player])
    // carry nonzero updating-player reach, so value loops iterate that list.
    const auto &actor_combos = board.player_combos[decision.player];
    const auto actor_slot_count = actor_combos.size();
    const bool locked_node = is_locked_root(node);
    // A false flag is a proof that every updating-player reach entry is zero.
    // It is propagated conservatively (true may remain true after card
    // removal), so using it can only suppress average-strategy additions that
    // would multiply by zero; counterfactual values and regrets are unchanged.
    const bool accumulate_average = strategy_weight != 0.0 && updating_reach_nonzero;
    // SIMD batch for the common two-action case: consecutive actor combos
    // have contiguous action blocks (uses_direct_action_bases), so four
    // combos' regrets are eight contiguous floats; four scalar divisions
    // become one AVX2 division. The arithmetic (max, pair-add, 1/sum, mul)
    // is IEEE-identical to the scalar path, so the result is bit-exact.
    if (!locked_node && action_count == 2U && layout_.uses_direct_action_bases &&
        buffers_.regret_float32 != nullptr) {
      const float *const regrets = buffers_.regret_float32 + decision.action_base;
      const __m256d zero = _mm256_setzero_pd();
      const __m256d one = _mm256_set1_pd(1.0);
      const __m256d half = _mm256_set1_pd(0.5);
      std::size_t i = 0;
      for (; i + 4U <= actor_slot_count; i += 4U) {
        __m256d r01 = _mm256_cvtps_pd(_mm_loadu_ps(regrets + 2U * i));
        __m256d r23 = _mm256_cvtps_pd(_mm_loadu_ps(regrets + 2U * i + 4U));
        r01 = _mm256_max_pd(r01, zero);
        r23 = _mm256_max_pd(r23, zero);
        __m256d sums = _mm256_hadd_pd(r01, r23);
        // _mm256_hadd_pd is per-128-bit-lane: (a0+a1, b0+b1, a2+a3, b2+b3), so
        // reorder to (s0, s1, s2, s3) = (a0+a1, a2+a3, b0+b1, b2+b3).
        sums = _mm256_permute4x64_pd(sums, 0b11011000);
        __m256d inverse = _mm256_div_pd(one, sums);
        inverse = _mm256_blendv_pd(inverse, half, _mm256_cmp_pd(sums, zero, _CMP_LE_OS));
        __m256d s01 = _mm256_mul_pd(r01, _mm256_permute4x64_pd(inverse, 0b01010000));
        __m256d s23 = _mm256_mul_pd(r23, _mm256_permute4x64_pd(inverse, 0b11111010));
        // The scalar path substitutes the uniform strategy when sum <= 0;
        // the clamped regrets would otherwise multiply to zero.
        {
          const __m256d le_zero = _mm256_cmp_pd(sums, zero, _CMP_LE_OS);
          s01 = _mm256_blendv_pd(s01, half, _mm256_permute4x64_pd(le_zero, 0b01010000));
          s23 = _mm256_blendv_pd(s23, half, _mm256_permute4x64_pd(le_zero, 0b11111010));
        }
        alignas(32) double s01_arr[4];
        alignas(32) double s23_arr[4];
        _mm256_store_pd(s01_arr, s01);
        _mm256_store_pd(s23_arr, s23);
        for (std::size_t k = 0; k < 4U; ++k) {
          const auto slot = board_player_slot(board, decision.player, i + k);
          const double first = k < 2U ? s01_arr[2U * k] : s23_arr[2U * (k - 2U)];
          const double second = k < 2U ? s01_arr[2U * k + 1U] : s23_arr[2U * (k - 2U) + 1U];
          strategies[0][slot] = static_cast<Scalar>(first);
          strategies[1][slot] = static_cast<Scalar>(second);
        }
      }
      for (; i < actor_slot_count; ++i) {
        const auto strategy = current_strategy(decision, static_cast<std::int16_t>(i), false);
        const auto slot = board_player_slot(board, decision.player, i);
        for (std::size_t action = 0; action < action_count; ++action) {
          strategies[action][slot] = static_cast<Scalar>(strategy[action]);
        }
      }
    } else if (!locked_node && layout_.uses_direct_action_bases &&
               buffers_.compact_state != nullptr) {
      // The compact 13+11 state stores every action as one contiguous 24-bit
      // word.  Regret matching only needs the low 13 bits, so decode each
      // action block once instead of routing every entry through
      // current_strategy(), decision_action_base() and ActionBuffers.
      const auto *const state =
          buffers_.compact_state + static_cast<std::size_t>(decision.action_base) * 3U;
      std::size_t local = 0U;
      if constexpr (PlayerIndexed) {
        if (action_count == 2U) {
          const __m128i expand_words =
              _mm_setr_epi8(0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8, -1, 9, 10, 11, -1);
          const __m128i regret_mask = _mm_set1_epi32(0x1fff);
          const __m256d zero = _mm256_setzero_pd();
          const __m256d half = _mm256_set1_pd(0.5);
          const __m256d one = _mm256_set1_pd(1.0);
          for (; local + 4U < actor_slot_count; local += 4U) {
            const auto *const packed = state + local * 6U;
            const __m128 first_four = _mm_castsi128_ps(_mm_slli_epi32(
                _mm_and_si128(
                    _mm_shuffle_epi8(
                        _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed)),
                        expand_words),
                    regret_mask),
                18));
            const __m128 second_four = _mm_castsi128_ps(_mm_slli_epi32(
                _mm_and_si128(
                    _mm_shuffle_epi8(
                        _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed + 12U)),
                        expand_words),
                    regret_mask),
                18));
            const __m256d first = _mm256_cvtps_pd(
                _mm_shuffle_ps(first_four, second_four, _MM_SHUFFLE(2, 0, 2, 0)));
            const __m256d second = _mm256_cvtps_pd(
                _mm_shuffle_ps(first_four, second_four, _MM_SHUFFLE(3, 1, 3, 1)));
            const __m256d sums = _mm256_add_pd(first, second);
            const __m256d no_positive_regret =
                _mm256_cmp_pd(sums, zero, _CMP_LE_OS);
            const __m256d inverse = _mm256_div_pd(one, sums);
            store_four_from_double(
                strategies[0].data() + local,
                _mm256_blendv_pd(_mm256_mul_pd(first, inverse), half,
                                 no_positive_regret));
            store_four_from_double(
                strategies[1].data() + local,
                _mm256_blendv_pd(_mm256_mul_pd(second, inverse), half,
                                 no_positive_regret));
          }
        } else if (action_count == 3U) {
          const __m128i expand_three_words =
              _mm_setr_epi8(0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8, -1, -1, -1, -1, -1);
          const __m128i regret_mask = _mm_set1_epi32(0x1fff);
          const __m256d zero = _mm256_setzero_pd();
          const __m256d uniform = _mm256_set1_pd(1.0 / 3.0);
          for (; local + 4U < actor_slot_count; local += 4U) {
            const auto *const block = state + local * 9U;
            __m128 row0 = _mm_castsi128_ps(_mm_slli_epi32(
                _mm_and_si128(
                    _mm_shuffle_epi8(
                        _mm_loadu_si128(reinterpret_cast<const __m128i *>(block)),
                        expand_three_words),
                    regret_mask),
                18));
            __m128 row1 = _mm_castsi128_ps(_mm_slli_epi32(
                _mm_and_si128(
                    _mm_shuffle_epi8(
                        _mm_loadu_si128(reinterpret_cast<const __m128i *>(block + 9U)),
                        expand_three_words),
                    regret_mask),
                18));
            __m128 row2 = _mm_castsi128_ps(_mm_slli_epi32(
                _mm_and_si128(
                    _mm_shuffle_epi8(
                        _mm_loadu_si128(reinterpret_cast<const __m128i *>(block + 18U)),
                        expand_three_words),
                    regret_mask),
                18));
            __m128 row3 = _mm_castsi128_ps(_mm_slli_epi32(
                _mm_and_si128(
                    _mm_shuffle_epi8(
                        _mm_loadu_si128(reinterpret_cast<const __m128i *>(block + 27U)),
                        expand_three_words),
                    regret_mask),
                18));
            _MM_TRANSPOSE4_PS(row0, row1, row2, row3);
            const __m256d first = _mm256_cvtps_pd(row0);
            const __m256d second = _mm256_cvtps_pd(row1);
            const __m256d third = _mm256_cvtps_pd(row2);
            const __m256d sums =
                _mm256_add_pd(_mm256_add_pd(first, second), third);
            const __m256d no_positive_regret =
                _mm256_cmp_pd(sums, zero, _CMP_LE_OS);
            store_four_from_double(
                strategies[0].data() + local,
                _mm256_blendv_pd(_mm256_div_pd(first, sums), uniform,
                                 no_positive_regret));
            store_four_from_double(
                strategies[1].data() + local,
                _mm256_blendv_pd(_mm256_div_pd(second, sums), uniform,
                                 no_positive_regret));
            store_four_from_double(
                strategies[2].data() + local,
                _mm256_blendv_pd(_mm256_div_pd(third, sums), uniform,
                                 no_positive_regret));
          }
        }
      }
      for (; local < actor_slot_count; ++local) {
        const auto slot = board_player_slot(board, decision.player, local);
        const auto *const block = state + local * action_count * 3U;
        double sum = 0.0;
        for (std::size_t action = 0; action < action_count; ++action) {
          const double regret = decode_regret13(static_cast<std::uint16_t>(
              compact_word(block + action * 3U) & 0x1fffU));
          strategies[action][slot] = static_cast<Scalar>(regret);
          sum += regret;
        }
        if (sum <= 0.0) {
          const double uniform = 1.0 / static_cast<double>(action_count);
          for (std::size_t action = 0; action < action_count; ++action) {
            strategies[action][slot] = static_cast<Scalar>(uniform);
          }
        } else {
          if (action_count == 2U) {
            const double inverse = 1.0 / sum;
            strategies[0][slot] = static_cast<Scalar>(strategies[0][slot] * inverse);
            strategies[1][slot] = static_cast<Scalar>(strategies[1][slot] * inverse);
          } else {
            for (std::size_t action = 0; action < action_count; ++action) {
              strategies[action][slot] =
                  static_cast<Scalar>(strategies[action][slot] / sum);
            }
          }
        }
      }
    } else if (!locked_node && action_count == 2U && layout_.uses_direct_action_bases &&
               buffers_.regret_float24 != nullptr) {
      // Packed regrets are still laid out as contiguous two-action blocks.
      // Decode the block directly instead of routing every scalar through
      // ActionBuffers and decision_action_base; this preserves the exact
      // regret-matching arithmetic while removing two hot-path abstractions.
      const auto *regrets =
          buffers_.regret_float24 + static_cast<std::size_t>(decision.action_base) * 3U;
      std::size_t local = 0U;
      if constexpr (PlayerIndexed) {
        const __m256d zero = _mm256_setzero_pd();
        const __m256d half = _mm256_set1_pd(0.5);
        const __m256d one = _mm256_set1_pd(1.0);
        const __m128i expand_float24 =
            _mm_setr_epi8(0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8, -1, 9, 10, 11, -1);
        for (; local + 4U < actor_slot_count; local += 4U) {
          const auto *const packed = regrets + local * 6U;
          const __m128 first_four = _mm_castsi128_ps(_mm_slli_epi32(
              _mm_shuffle_epi8(
                  _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed)),
                  expand_float24),
              7));
          const __m128 second_four = _mm_castsi128_ps(_mm_slli_epi32(
              _mm_shuffle_epi8(
                  _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed + 12U)),
                  expand_float24),
              7));
          const __m256d first = _mm256_cvtps_pd(
              _mm_shuffle_ps(first_four, second_four, _MM_SHUFFLE(2, 0, 2, 0)));
          const __m256d second = _mm256_cvtps_pd(
              _mm_shuffle_ps(first_four, second_four, _MM_SHUFFLE(3, 1, 3, 1)));
          const __m256d sums = _mm256_add_pd(first, second);
          const __m256d no_positive_regret =
              _mm256_cmp_pd(sums, zero, _CMP_LE_OS);
          const __m256d inverse = _mm256_div_pd(one, sums);
          const __m256d first_strategy = _mm256_blendv_pd(
              _mm256_mul_pd(first, inverse), half, no_positive_regret);
          const __m256d second_strategy = _mm256_blendv_pd(
              _mm256_mul_pd(second, inverse), half, no_positive_regret);
          store_four_from_double(strategies[0].data() + local, first_strategy);
          store_four_from_double(strategies[1].data() + local, second_strategy);
        }
      }
      for (; local < actor_slot_count; ++local) {
        std::uint64_t packed_pair = 0U;
        if (local + 1U < actor_slot_count) {
          std::memcpy(&packed_pair, regrets + local * 6U, sizeof(packed_pair));
        } else {
          std::memcpy(&packed_pair, regrets + local * 6U, 6U);
        }
        const auto first_bits = static_cast<std::uint32_t>(packed_pair & 0x00ffffffULL) << 7U;
        const auto second_bits =
            static_cast<std::uint32_t>((packed_pair >> 24U) & 0x00ffffffULL) << 7U;
        const double first = static_cast<double>(std::bit_cast<float>(first_bits));
        const double second = static_cast<double>(std::bit_cast<float>(second_bits));
        const double sum = first + second;
        const auto slot = board_player_slot(board, decision.player, local);
        if (sum <= 0.0) {
          strategies[0][slot] = static_cast<Scalar>(0.5);
          strategies[1][slot] = static_cast<Scalar>(0.5);
        } else {
          const double inverse = 1.0 / sum;
          strategies[0][slot] = static_cast<Scalar>(first * inverse);
          strategies[1][slot] = static_cast<Scalar>(second * inverse);
        }
      }
    } else if (!locked_node && PlayerIndexed && action_count == 3U &&
               layout_.uses_direct_action_bases &&
               buffers_.regret_float24 != nullptr) {
      const auto *regrets =
          buffers_.regret_float24 + static_cast<std::size_t>(decision.action_base) * 3U;
      const __m256d zero = _mm256_setzero_pd();
      const __m256d uniform = _mm256_set1_pd(1.0 / 3.0);
      const __m128i expand_three_float24 =
          _mm_setr_epi8(0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8, -1, -1, -1, -1, -1);
      std::size_t local = 0U;
      for (; local + 4U < actor_slot_count; local += 4U) {
        const auto *const block = regrets + local * 9U;
        __m128 row0 = _mm_castsi128_ps(_mm_slli_epi32(
            _mm_shuffle_epi8(
                _mm_loadu_si128(reinterpret_cast<const __m128i *>(block)),
                expand_three_float24),
            7));
        __m128 row1 = _mm_castsi128_ps(_mm_slli_epi32(
            _mm_shuffle_epi8(
                _mm_loadu_si128(reinterpret_cast<const __m128i *>(block + 9U)),
                expand_three_float24),
            7));
        __m128 row2 = _mm_castsi128_ps(_mm_slli_epi32(
            _mm_shuffle_epi8(
                _mm_loadu_si128(reinterpret_cast<const __m128i *>(block + 18U)),
                expand_three_float24),
            7));
        __m128 row3 = _mm_castsi128_ps(_mm_slli_epi32(
            _mm_shuffle_epi8(
                _mm_loadu_si128(reinterpret_cast<const __m128i *>(block + 27U)),
                expand_three_float24),
            7));
        _MM_TRANSPOSE4_PS(row0, row1, row2, row3);
        const __m256d first = _mm256_cvtps_pd(row0);
        const __m256d second = _mm256_cvtps_pd(row1);
        const __m256d third = _mm256_cvtps_pd(row2);
        const __m256d sums =
            _mm256_add_pd(_mm256_add_pd(first, second), third);
        const __m256d no_positive_regret =
            _mm256_cmp_pd(sums, zero, _CMP_LE_OS);
        const __m256d first_strategy = _mm256_blendv_pd(
            _mm256_div_pd(first, sums), uniform, no_positive_regret);
        const __m256d second_strategy = _mm256_blendv_pd(
            _mm256_div_pd(second, sums), uniform, no_positive_regret);
        const __m256d third_strategy = _mm256_blendv_pd(
            _mm256_div_pd(third, sums), uniform, no_positive_regret);
        store_four_from_double(strategies[0].data() + local, first_strategy);
        store_four_from_double(strategies[1].data() + local, second_strategy);
        store_four_from_double(strategies[2].data() + local, third_strategy);
      }
      for (; local < actor_slot_count; ++local) {
        const auto *const block = regrets + local * 9U;
        const double first = static_cast<double>(decode_float24(block));
        const double second = static_cast<double>(decode_float24(block + 3U));
        const double third = static_cast<double>(decode_float24(block + 6U));
        const double sum = (first + second) + third;
        if (sum <= 0.0) {
          strategies[0][local] = static_cast<Scalar>(1.0 / 3.0);
          strategies[1][local] = static_cast<Scalar>(1.0 / 3.0);
          strategies[2][local] = static_cast<Scalar>(1.0 / 3.0);
        } else {
          strategies[0][local] = static_cast<Scalar>(first / sum);
          strategies[1][local] = static_cast<Scalar>(second / sum);
          strategies[2][local] = static_cast<Scalar>(third / sum);
        }
      }
    } else if (!locked_node && layout_.uses_direct_action_bases &&
               buffers_.regret_float24 != nullptr) {
      const auto *regrets =
          buffers_.regret_float24 + static_cast<std::size_t>(decision.action_base) * 3U;
      for (std::size_t local = 0; local < actor_slot_count; ++local) {
        const auto slot = board_player_slot(board, decision.player, local);
        const auto *block = regrets + local * action_count * 3U;
        double sum = 0.0;
        for (std::size_t action = 0; action < action_count; ++action) {
          const double value = static_cast<double>(decode_float24(block + action * 3U));
          strategies[action][slot] = static_cast<Scalar>(value);
          sum += value;
        }
        if (sum <= 0.0) {
          const double uniform = 1.0 / static_cast<double>(action_count);
          for (std::size_t action = 0; action < action_count; ++action) {
            strategies[action][slot] = static_cast<Scalar>(uniform);
          }
        } else {
          for (std::size_t action = 0; action < action_count; ++action) {
            strategies[action][slot] =
                static_cast<Scalar>(strategies[action][slot] / sum);
          }
        }
      }
    } else {
      for (std::size_t local = 0; local < actor_combos.size(); ++local) {
        const auto combo_id = actor_combos[local];
        const auto locked = locked_node ? locked_root_strategy(board.local_index[combo_id])
                                        : std::nullopt;
        const auto strategy =
            locked ? *locked
                   : current_strategy(decision, static_cast<std::int16_t>(local), false);
        const auto slot = board_player_slot(board, decision.player, local);
        for (std::size_t action = 0; action < action_count; ++action) {
          strategies[action][slot] = static_cast<Scalar>(strategy[action]);
        }
      }
    }

    if (profile) {
      prof_strategy_entries_ += actor_slot_count * action_count;
      for (std::size_t action = 0; action < action_count; ++action) {
        bool any_positive = false;
        for (std::size_t local = 0; local < actor_slot_count; ++local) {
          const auto slot = board_player_slot(board, decision.player, local);
          if (strategies[action][slot] == 0.0) {
            ++prof_zero_strategy_entries_;
          } else {
            any_positive = true;
          }
        }
        if (!any_positive) {
          ++prof_whole_zero_actions_;
        }
      }
    }
    const auto t_after_strategy = profile ? std::chrono::steady_clock::now()
                                          : std::chrono::steady_clock::time_point{};
    if (profile) {
      prof_strategy_seconds_ +=
          std::chrono::duration<double>(t_after_strategy - t_begin).count();
    }
    auto t_child_end = t_after_strategy;
    auto t_prev = t_after_strategy;
    const std::size_t paired_fold_action =
        PlayerIndexed ? decision.paired_fold_action : action_count;
    const std::size_t paired_showdown_action =
        PlayerIndexed ? decision.paired_showdown_action : action_count;
    auto &actor_parent_reach_local =
        scratch_lease.get().actor_parent_reach_local;
    const bool has_board_local_parent =
        PlayerIndexed && (decision.player != updating_player || accumulate_average);
    if constexpr (PlayerIndexed) {
      if (has_board_local_parent) {
        const auto *const slots = board.player_flop_slots[decision.player].data();
        std::size_t local = 0U;
        for (; local + 4U <= actor_slot_count; local += 4U) {
          const __m128i packed_slots = _mm_loadl_epi64(
              reinterpret_cast<const __m128i *>(slots + local));
          const __m128i indices = _mm_cvtepu16_epi32(packed_slots);
          store_four_from_double(
              actor_parent_reach_local.data() + local,
              gather_four_as_double((*reach[decision.player]).data(), indices));
        }
        for (; local < actor_slot_count; ++local) {
          actor_parent_reach_local[local] =
              (*reach[decision.player])[slots[local]];
        }
      }
    }
    const ComboVector *const board_local_parent =
        has_board_local_parent ? &actor_parent_reach_local : nullptr;
    const auto updating_parent_reach_at = [&](const std::size_t local) {
      if constexpr (PlayerIndexed) {
        return actor_parent_reach_local[local];
      }
      return (*reach[updating_player])[
          board_reach_slot(board, updating_player, local)];
    };
    // Root action subtrees in the physical flop tree own disjoint action
    // indices and result vectors. Queue every branch after the first and let
    // those root tasks expose their turn-card chance layer. Ordinary chance
    // tasks remain non-nested. Parent reductions keep their original order.
    const bool parallel_root_actions =
        PlayerIndexed && node.id == layout_.tree.root &&
        std::popcount(board.mask) == 3U && !locked_node &&
        (action_count == 2U || action_count == 3U) &&
        decision.terminal_child_mask == 0U &&
        paired_fold_action >= action_count && paired_showdown_action >= action_count &&
        layout_.uses_direct_action_bases &&
        (!parallel_workers_.empty() || parallel_pool_size_ > 0U);
    if (parallel_root_actions) {
      const bool actor_needed =
          decision.player != updating_player || accumulate_average;
      std::array<ReachRef, 3> child_refs{reach, reach, reach};
      std::array<bool, 3> actor_reach_nonzero{false, false, false};
      if (actor_needed) {
        for (std::size_t action = 0; action < action_count; ++action) {
          ComboVector &actor_reach = scratch_lease.get().reach_actor[action];
          actor_reach_nonzero[action] = materialize_actor_reach(
              board, decision.player, *reach[decision.player], strategies[action],
              actor_reach, actor_slot_count, true, board_local_parent);
          child_refs[action] =
              decision.player == 0U
                  ? ReachRef{&actor_reach, reach[1]}
                  : ReachRef{reach[0], &actor_reach};
        }
      }
      const auto t_copy_end = profile ? std::chrono::steady_clock::now()
                                      : std::chrono::steady_clock::time_point{};
      if (profile) {
        prof_copy_seconds_ +=
            std::chrono::duration<double>(t_copy_end - t_prev).count();
      }
      std::vector<std::packaged_task<TraversalResult(DenseTraversal &)>> tasks;
      std::vector<std::future<TraversalResult>> futures;
      tasks.reserve(action_count - 1U);
      futures.reserve(action_count - 1U);
      const std::size_t available_pool_size =
          parallel_workers_.empty() ? parallel_pool_size_ : parallel_workers_.size();
      for (std::size_t action = 1U; action < action_count; ++action) {
        const bool child_nonzero =
            decision.player == updating_player ? actor_reach_nonzero[action]
                                               : updating_reach_nonzero;
        std::packaged_task<TraversalResult(DenseTraversal &)> task(
            [child = node.edges[action].child, updating_player,
             child_reach_0 = child_refs[action][0],
             child_reach_1 = child_refs[action][1], strategy_weight,
             child_nonzero, public_update_multiplicity,
             root_pool_size = available_pool_size,
             result = &action_values[action]](DenseTraversal &self) {
              const std::size_t previous_pool_size = self.parallel_pool_size_;
              self.parallel_pool_size_ = root_pool_size;
              const auto error = self.cfr_physical(
                  child, updating_player, {child_reach_0, child_reach_1},
                  strategy_weight, child_nonzero, public_update_multiplicity, *result);
              self.parallel_pool_size_ = previous_pool_size;
              if (error) {
                return Result<ComboVector, PostflopSolverError>::failure(*error);
              }
              return Result<ComboVector, PostflopSolverError>::success(ComboVector{});
            });
        futures.push_back(task.get_future());
        tasks.push_back(std::move(task));
      }
      dispatch_parallel_tasks(tasks);
      const bool child_nonzero_0 =
          decision.player == updating_player ? actor_reach_nonzero[0]
                                             : updating_reach_nonzero;
      const auto main_error = cfr_physical(
          node.edges[0].child, updating_player, child_refs[0], strategy_weight,
          child_nonzero_0, public_update_multiplicity, action_values[0]);
      std::optional<PostflopSolverError> worker_error;
      for (auto &future : futures) {
        wait_for_parallel_future(future);
        auto worker_result = future.get();
        if (!worker_result && !worker_error) {
          worker_error = worker_result.error();
        }
      }
      t_child_end = profile ? std::chrono::steady_clock::now()
                            : std::chrono::steady_clock::time_point{};
      if (profile) {
        prof_children_seconds_ +=
            std::chrono::duration<double>(t_child_end - t_copy_end).count();
      }
      if (main_error) {
        return main_error;
      }
      if (worker_error) {
        return worker_error;
      }
    }
    for (std::size_t action = parallel_root_actions ? action_count : 0U;
         action < action_count; ++action) {
      if (action == paired_fold_action) {
        // The paired showdown action fills both result vectors from one
        // terminal pass. It may appear later in the action order; no parent
        // value is consumed until all children have completed.
        continue;
      }
      // The child node kind decides whether the actor reach is consumed at
      // all: a terminal (fold/showdown) reads only reach[1-updating_player].
      // When the actor is the updating player the freshly written actor reach
      // is never read, so its materialization is skipped and the parent's own
      // reach is passed instead (Phase C.3 fusion).
      const bool terminal_child =
          (decision.terminal_child_mask & static_cast<std::uint8_t>(1U << action)) != 0U;
      const PublicTreeNode *const child_node =
          terminal_child
              ? &layout_.tree.nodes[static_cast<std::size_t>(node.edges[action].child)]
              : nullptr;
      // The updating player's own reach only weights average-strategy
      // accumulation. Before averaging starts it is irrelevant to both
      // counterfactual values and regret deltas, so do not propagate it.
      const bool actor_needed =
          decision.player != updating_player || (!terminal_child && accumulate_average);
      ComboVector &actor_reach = scratch_lease.get().reach_actor[0];
      const bool paired_terminal_opponent =
          action == paired_showdown_action && decision.player != updating_player;
      ComboVector *const fold_actor_reach =
          paired_terminal_opponent ? &scratch_lease.get().reach_actor[1] : nullptr;
      bool actor_reach_nonzero = false;
      bool paired_fold_reach_nonzero = !paired_terminal_opponent;
      if (paired_terminal_opponent) {
        const auto nonzero = materialize_actor_reach_pair(
            board, decision.player, *reach[decision.player], strategies[action],
            strategies[paired_fold_action], actor_reach, *fold_actor_reach,
            actor_slot_count, true, board_local_parent);
        actor_reach_nonzero = nonzero[0];
        paired_fold_reach_nonzero = nonzero[1];
      } else if (actor_needed) {
        // Fused actor write: only the actor's flop-range slots are set; the
        // opponent side remains shared from the parent.
        actor_reach_nonzero = materialize_actor_reach(
            board, decision.player, *reach[decision.player], strategies[action],
            actor_reach, actor_slot_count, true, board_local_parent);
      }
      const auto t_copy_end = profile ? std::chrono::steady_clock::now()
                                      : std::chrono::steady_clock::time_point{};
      if (profile) {
        prof_copy_seconds_ +=
            std::chrono::duration<double>(t_copy_end - t_prev).count();
      }
      // The child writes its value vector directly into this action's slot of
      // the parent's scratch (no Result<ComboVector> return, no prefix copy).
      const ReachRef child_ref =
          actor_needed
              ? (decision.player == 0U ? ReachRef{&actor_reach, reach[1]}
                                       : ReachRef{reach[0], &actor_reach})
              : ReachRef{reach[0], reach[1]};
      const ComboVector *paired_fold_opponent_reach = child_ref[1U - updating_player];
      if (paired_terminal_opponent) {
        paired_fold_opponent_reach = fold_actor_reach;
      }
      std::optional<PostflopSolverError> error;
      const bool zero_paired_showdown =
          action == paired_showdown_action && decision.player != updating_player &&
          !actor_reach_nonzero;
      const bool zero_paired_fold =
          action == paired_showdown_action && decision.player != updating_player &&
          !paired_fold_reach_nonzero;
      const bool zero_opponent_branch =
          decision.player != updating_player && !actor_reach_nonzero &&
          (action != paired_showdown_action || zero_paired_fold);
      const bool average_only_allowed = terminal_child || strategy_weight == 0.0 ||
                                        std::popcount(board.mask) >= 4;
      const bool direct_physical_average =
          PlayerIndexed && layout_.uses_direct_action_bases;
      if (zero_opponent_branch && average_only_allowed && direct_physical_average) {
        if (!terminal_child && accumulate_average) {
          const auto average_only_begin =
              profile ? std::chrono::steady_clock::now()
                      : std::chrono::steady_clock::time_point{};
          if (profile) {
            ++prof_average_only_calls_;
          }
          error = accumulate_average_only(node.edges[action].child, updating_player,
                                          child_ref, strategy_weight);
          if (profile) {
            prof_average_only_seconds_ +=
                std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                              average_only_begin)
                    .count();
          }
        }
        if (!error) {
          zero_board_values_into(board, updating_player, action_values[action]);
          if (action == paired_showdown_action) {
            zero_board_values_into(board, updating_player,
                                   action_values[paired_fold_action]);
          }
        }
      } else if (zero_paired_showdown) {
        zero_board_values_into(board, updating_player, action_values[action]);
        error = fold_values_into(
            layout_.tree.nodes[static_cast<std::size_t>(
                node.edges[paired_fold_action].child)],
            updating_player, *paired_fold_opponent_reach,
            action_values[paired_fold_action]);
      } else if (zero_paired_fold) {
        error = showdown_values_into(*child_node, updating_player,
                                     *child_ref[1U - updating_player],
                                     action_values[action]);
        if (!error) {
          zero_board_values_into(board, updating_player,
                                 action_values[paired_fold_action]);
        }
      } else {
        const bool child_updating_reach_nonzero =
            decision.player == updating_player ? actor_reach_nonzero
                                               : updating_reach_nonzero;
        error =
            action == paired_showdown_action
                ? showdown_and_fold_values_into(
                      *child_node,
                      layout_.tree.nodes[static_cast<std::size_t>(
                          node.edges[paired_fold_action].child)],
                      updating_player, *child_ref[1U - updating_player],
                      *paired_fold_opponent_reach, action_values[action],
                      action_values[paired_fold_action])
                : cfr_physical(node.edges[action].child, updating_player, child_ref,
                               strategy_weight, child_updating_reach_nonzero,
                               public_update_multiplicity,
                               action_values[action],
                               orbit_context == nullptr ? nullptr
                                                        : &child_orbit_contexts[action]);
      }
      t_child_end = profile ? std::chrono::steady_clock::now()
                            : std::chrono::steady_clock::time_point{};
      if (profile) {
        prof_children_seconds_ +=
            std::chrono::duration<double>(t_child_end - t_copy_end).count();
      }
      t_prev = t_child_end;
      if (error) {
        return error;
      }
    }

    auto &values = values_out;
    zero_board_values_into(board, updating_player, values);
    const bool updating_actor = decision.player == updating_player;
    if (updating_actor) {
      if constexpr (PlayerIndexed) {
        // Action-outer, slot-inner value accumulation: for a fixed action the
        // three streams (strategies[action][slot], action_values[action][slot],
        // values[slot]) are contiguous over the updating player's flop-range
        // slots, so the loop auto-vectorizes under /arch:AVX2. Slots for
        // combos blocked on this board hold zero in action_values (children
        // keep them zero), so the full-prefix accumulation is exact. The
        // summation order differs from the strict combo-outer order by
        // construction; the dEV deviation is far below the 1e-6 gate.
        const auto updating_count = board.player_combos[updating_player].size();
        if (action_count == 2U) {
          // Fused two-action accumulation: four contiguous slots at a time
          // (per-player slots are contiguous). Compared with the scalar
          // action-outer loop this halves the values load/store traffic and
          // enables a single FMA chain per slot; IEEE-identical per slot.
          const Scalar *const s0 = strategies[0].data();
          const Scalar *const s1 = strategies[1].data();
          const Scalar *const a0 = action_values[0].data();
          const Scalar *const a1 = action_values[1].data();
          Scalar *const v = values.data();
          std::size_t i = 0;
          for (; i + 4U <= updating_count; i += 4U) {
            const __m256d acc = _mm256_add_pd(
                _mm256_mul_pd(load_four_as_double(s0 + i), load_four_as_double(a0 + i)),
                _mm256_mul_pd(load_four_as_double(s1 + i), load_four_as_double(a1 + i)));
            store_four_from_double(v + i, _mm256_add_pd(load_four_as_double(v + i), acc));
          }
          for (; i < updating_count; ++i) {
            v[i] = static_cast<Scalar>(v[i] + s0[i] * a0[i] + s1[i] * a1[i]);
          }
        } else {
          for (std::size_t action = 0; action < action_count; ++action) {
            for (std::size_t slot = 0; slot < updating_count; ++slot) {
              values[slot] += strategies[action][slot] * action_values[action][slot];
            }
          }
        }
      } else {
        // Non-per-player space: slots are combo ids, iterate the updating
        // player's live combos (combo-outer to keep the bit-exact order).
        for (const ComboId combo_id : board.player_combos[updating_player]) {
          const auto slot = value_slot(combo_id, updating_player);
          for (std::size_t action = 0; action < action_count; ++action) {
            values[slot] += strategies[action][slot] * action_values[action][slot];
          }
        }
      }
      const auto t_after_value = profile ? std::chrono::steady_clock::now()
                                         : std::chrono::steady_clock::time_point{};
      if (profile) {
        prof_value_accumulate_seconds_ +=
            std::chrono::duration<double>(t_after_value - t_child_end).count();
      }
      // Regret/strategy updates remain combo-outer: each combo's action block
      // is contiguous (offset..offset+action_count) and each index is touched
      // once, so the order is irrelevant to the result. (The F1 vectorized
      // update was measured and reverted: 175.5s vs 164.0s baseline — the
      // scattered value loads dominate and the contiguity check is pure
      // overhead; see journey §8.9.)
      const bool locked_root = is_locked_root(node);
      const auto updating_count = board.player_combos[updating_player].size();
      const bool batch_half_average =
          accumulate_average && action_count == 2U && layout_.uses_direct_action_bases &&
          buffers_.strategy_float16 != nullptr;
      const bool direct_compact_block_update =
          !locked_root && layout_.uses_direct_action_bases && deferred_regret_delta_ == nullptr &&
          buffers_.compact_state != nullptr;
      const bool direct_packed_pair_update =
          !locked_root && action_count == 2U && layout_.uses_direct_action_bases &&
          deferred_regret_delta_ == nullptr && buffers_.regret_float24 != nullptr &&
          buffers_.strategy_float16 != nullptr;
      const bool direct_packed_three_update =
          !locked_root && action_count == 3U && layout_.uses_direct_action_bases &&
          deferred_regret_delta_ == nullptr && buffers_.regret_float24 != nullptr &&
          buffers_.strategy_float16 != nullptr;
      const bool direct_packed_block_update =
          !locked_root && layout_.uses_direct_action_bases && deferred_regret_delta_ == nullptr &&
          buffers_.regret_float24 != nullptr && buffers_.strategy_float16 != nullptr;
      if (direct_compact_block_update) {
        auto *const state =
            buffers_.compact_state + static_cast<std::size_t>(decision.action_base) * 3U;
        std::size_t local = 0U;
        if (action_count == 2U) {
          if constexpr (PlayerIndexed) {
            const __m128i expand_words =
                _mm_setr_epi8(0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8, -1, 9, 10, 11, -1);
            const __m128i compact_words =
                _mm_setr_epi8(0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14, -1, -1, -1, -1);
            const __m128i regret_mask = _mm_set1_epi32(0x1fff);
            const auto encode_four_regrets = [](const __m256d input) {
              const __m128i bits = _mm_and_si128(
                  _mm_castps_si128(_mm256_cvtpd_ps(input)), _mm_set1_epi32(0x7fffffff));
              const __m128i discarded =
                  _mm_and_si128(bits, _mm_set1_epi32(0x3ffff));
              __m128i packed = _mm_srli_epi32(bits, 18);
              const __m128i greater =
                  _mm_cmpgt_epi32(discarded, _mm_set1_epi32(0x20000));
              const __m128i equal =
                  _mm_cmpeq_epi32(discarded, _mm_set1_epi32(0x20000));
              const __m128i odd = _mm_cmpeq_epi32(
                  _mm_and_si128(packed, _mm_set1_epi32(1)), _mm_set1_epi32(1));
              packed = _mm_add_epi32(
                  packed,
                  _mm_and_si128(_mm_or_si128(greater, _mm_and_si128(equal, odd)),
                                _mm_set1_epi32(1)));
              return _mm_min_epu32(packed, _mm_set1_epi32(0x1ffe));
            };
            const auto store_four_words = [compact_words](std::uint8_t *const destination,
                                                          const __m128i words) {
              const __m128i compact = _mm_shuffle_epi8(words, compact_words);
              _mm_storel_epi64(reinterpret_cast<__m128i *>(destination), compact);
              const std::uint32_t tail = static_cast<std::uint32_t>(
                  _mm_cvtsi128_si32(_mm_srli_si128(compact, 8)));
              std::memcpy(destination + 8U, &tail, sizeof(tail));
            };
            const __m256d zero = _mm256_setzero_pd();
            const __m256d regret_weight = _mm256_set1_pd(effective_regret_update_weight);
            const __m128i strategy_mask = _mm_set1_epi32(0x00ffe000);
            for (; local + 4U < updating_count; local += 4U) {
              auto *const packed = state + local * 6U;
              const __m128i low_words = _mm_shuffle_epi8(
                  _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed)), expand_words);
              const __m128i high_words = _mm_shuffle_epi8(
                  _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed + 12U)),
                  expand_words);
              const __m128 low_regrets = _mm_castsi128_ps(
                  _mm_slli_epi32(_mm_and_si128(low_words, regret_mask), 18));
              const __m128 high_regrets = _mm_castsi128_ps(
                  _mm_slli_epi32(_mm_and_si128(high_words, regret_mask), 18));
              const __m256d first = _mm256_cvtps_pd(
                  _mm_shuffle_ps(low_regrets, high_regrets, _MM_SHUFFLE(2, 0, 2, 0)));
              const __m256d second = _mm256_cvtps_pd(
                  _mm_shuffle_ps(low_regrets, high_regrets, _MM_SHUFFLE(3, 1, 3, 1)));
              const __m256d current = load_four_as_double(values.data() + local);
              const __m128i encoded_first = encode_four_regrets(_mm256_max_pd(
                      zero,
                      _mm256_add_pd(
                          first,
                          _mm256_mul_pd(
                              regret_weight,
                              _mm256_sub_pd(
                                  load_four_as_double(action_values[0].data() + local),
                                  current)))));
              const __m128i encoded_second = encode_four_regrets(_mm256_max_pd(
                      zero,
                      _mm256_add_pd(
                          second,
                          _mm256_mul_pd(
                              regret_weight,
                              _mm256_sub_pd(
                                  load_four_as_double(action_values[1].data() + local),
                                  current)))));
              if (!accumulate_average) {
                store_four_words(
                    packed,
                    _mm_or_si128(_mm_and_si128(low_words, strategy_mask),
                                 _mm_unpacklo_epi32(encoded_first, encoded_second)));
                store_four_words(
                    packed + 12U,
                    _mm_or_si128(_mm_and_si128(high_words, strategy_mask),
                                 _mm_unpackhi_epi32(encoded_first, encoded_second)));
                continue;
              }
              alignas(16) std::uint32_t encoded_first_values[4];
              alignas(16) std::uint32_t encoded_second_values[4];
              _mm_store_si128(reinterpret_cast<__m128i *>(encoded_first_values),
                              encoded_first);
              _mm_store_si128(reinterpret_cast<__m128i *>(encoded_second_values),
                              encoded_second);
              alignas(16) std::uint32_t words[8];
              _mm_store_si128(reinterpret_cast<__m128i *>(words), low_words);
              _mm_store_si128(reinterpret_cast<__m128i *>(words + 4U), high_words);
              for (std::size_t lane = 0U; lane < 4U; ++lane) {
                auto first_strategy = static_cast<std::uint16_t>(words[lane * 2U] >> 13U);
                auto second_strategy =
                    static_cast<std::uint16_t>(words[lane * 2U + 1U] >> 13U);
                const double average_weight =
                    strategy_weight * updating_parent_reach_at(local + lane);
                first_strategy = encode_strategy11(
                    decode_strategy11(first_strategy) +
                    average_weight * strategies[0][local + lane]);
                second_strategy = encode_strategy11(
                    decode_strategy11(second_strategy) +
                    average_weight * strategies[1][local + lane]);
                words[lane * 2U] = encoded_first_values[lane] |
                                         (static_cast<std::uint32_t>(first_strategy) << 13U);
                words[lane * 2U + 1U] = encoded_second_values[lane] |
                                              (static_cast<std::uint32_t>(second_strategy) << 13U);
              }
              store_four_words(packed,
                               _mm_load_si128(reinterpret_cast<const __m128i *>(words)));
              store_four_words(packed + 12U,
                               _mm_load_si128(reinterpret_cast<const __m128i *>(words + 4U)));
            }
          }
          for (; local < updating_count; ++local) {
            const auto slot = board_player_slot(board, updating_player, local);
            const double average_weight =
                accumulate_average ? strategy_weight * updating_parent_reach_at(local)
                                   : 0.0;
            auto *const block = state + local * 6U;
            std::uint64_t pair = 0U;
            std::memcpy(&pair, block, 6U);
            const auto first_word = static_cast<std::uint32_t>(pair & 0x00ffffffULL);
            const auto second_word = static_cast<std::uint32_t>((pair >> 24U) & 0x00ffffffULL);
            const auto first_regret = encode_regret13(std::max(
                0.0,
                decode_regret13(static_cast<std::uint16_t>(first_word & 0x1fffU)) +
                     effective_regret_update_weight *
                         (action_values[0][slot] - values[slot])));
            const auto second_regret = encode_regret13(std::max(
                0.0,
                decode_regret13(static_cast<std::uint16_t>(second_word & 0x1fffU)) +
                     effective_regret_update_weight *
                         (action_values[1][slot] - values[slot])));
            auto first_strategy = static_cast<std::uint16_t>(first_word >> 13U);
            auto second_strategy = static_cast<std::uint16_t>(second_word >> 13U);
            if (accumulate_average) {
              first_strategy = encode_strategy11(
                  decode_strategy11(first_strategy) + average_weight * strategies[0][slot]);
              second_strategy = encode_strategy11(
                  decode_strategy11(second_strategy) + average_weight * strategies[1][slot]);
            }
            pair = static_cast<std::uint64_t>(first_regret) |
                   (static_cast<std::uint64_t>(first_strategy) << 13U) |
                   (static_cast<std::uint64_t>(second_regret) << 24U) |
                   (static_cast<std::uint64_t>(second_strategy) << 37U);
            std::memcpy(block, &pair, 6U);
          }
        } else if (action_count == 3U) {
          if constexpr (PlayerIndexed) {
            const __m128i expand_three_words =
                _mm_setr_epi8(0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8, -1, -1, -1, -1, -1);
            const __m128i compact_three_words =
                _mm_setr_epi8(0, 1, 2, 4, 5, 6, 8, 9, 10, -1, -1, -1, -1, -1, -1, -1);
            const __m128i regret_mask = _mm_set1_epi32(0x1fff);
            const auto encode_four_regrets = [](const __m256d input) {
              const __m128i bits = _mm_and_si128(
                  _mm_castps_si128(_mm256_cvtpd_ps(input)), _mm_set1_epi32(0x7fffffff));
              const __m128i discarded =
                  _mm_and_si128(bits, _mm_set1_epi32(0x3ffff));
              __m128i packed = _mm_srli_epi32(bits, 18);
              const __m128i greater =
                  _mm_cmpgt_epi32(discarded, _mm_set1_epi32(0x20000));
              const __m128i equal =
                  _mm_cmpeq_epi32(discarded, _mm_set1_epi32(0x20000));
              const __m128i odd = _mm_cmpeq_epi32(
                  _mm_and_si128(packed, _mm_set1_epi32(1)), _mm_set1_epi32(1));
              packed = _mm_add_epi32(
                  packed,
                  _mm_and_si128(_mm_or_si128(greater, _mm_and_si128(equal, odd)),
                                _mm_set1_epi32(1)));
              return _mm_min_epu32(packed, _mm_set1_epi32(0x1ffe));
            };
            const auto store_three_words =
                [compact_three_words](std::uint8_t *const destination,
                                      const __m128i words) {
                  const __m128i compact =
                      _mm_shuffle_epi8(words, compact_three_words);
                  _mm_storel_epi64(reinterpret_cast<__m128i *>(destination), compact);
                  destination[8] = static_cast<std::uint8_t>(
                      _mm_cvtsi128_si32(_mm_srli_si128(compact, 8)));
            };
            const __m256d zero = _mm256_setzero_pd();
            const __m256d regret_weight = _mm256_set1_pd(effective_regret_update_weight);
            for (; local + 4U < updating_count; local += 4U) {
              auto *const block = state + local * 9U;
              std::array<__m128i, 4> word_rows{
                  _mm_shuffle_epi8(
                      _mm_loadu_si128(reinterpret_cast<const __m128i *>(block)),
                      expand_three_words),
                  _mm_shuffle_epi8(
                      _mm_loadu_si128(reinterpret_cast<const __m128i *>(block + 9U)),
                      expand_three_words),
                  _mm_shuffle_epi8(
                      _mm_loadu_si128(reinterpret_cast<const __m128i *>(block + 18U)),
                      expand_three_words),
                  _mm_shuffle_epi8(
                      _mm_loadu_si128(reinterpret_cast<const __m128i *>(block + 27U)),
                      expand_three_words)};
              __m128 first = _mm_castsi128_ps(
                  _mm_slli_epi32(_mm_and_si128(word_rows[0], regret_mask), 18));
              __m128 second = _mm_castsi128_ps(
                  _mm_slli_epi32(_mm_and_si128(word_rows[1], regret_mask), 18));
              __m128 third = _mm_castsi128_ps(
                  _mm_slli_epi32(_mm_and_si128(word_rows[2], regret_mask), 18));
              __m128 fourth = _mm_castsi128_ps(
                  _mm_slli_epi32(_mm_and_si128(word_rows[3], regret_mask), 18));
              _MM_TRANSPOSE4_PS(first, second, third, fourth);
              const __m256d current = load_four_as_double(values.data() + local);
              alignas(16) std::uint32_t encoded[3][4];
              const std::array<__m128, 3> regret_rows{first, second, third};
              for (std::size_t action = 0U; action < 3U; ++action) {
                const __m256d updated = _mm256_max_pd(
                    zero,
                    _mm256_add_pd(
                        _mm256_cvtps_pd(regret_rows[action]),
                        _mm256_mul_pd(
                            regret_weight,
                            _mm256_sub_pd(
                                load_four_as_double(action_values[action].data() + local),
                                current))));
                _mm_store_si128(reinterpret_cast<__m128i *>(encoded[action]),
                                encode_four_regrets(updated));
              }
              alignas(16) std::uint32_t words[4][4];
              for (std::size_t lane = 0U; lane < 4U; ++lane) {
                _mm_store_si128(reinterpret_cast<__m128i *>(words[lane]), word_rows[lane]);
                if (!accumulate_average) {
                  for (std::size_t action = 0U; action < 3U; ++action) {
                    words[lane][action] =
                        (words[lane][action] & 0x00ffe000U) | encoded[action][lane];
                  }
                  store_three_words(
                      block + lane * 9U,
                      _mm_load_si128(reinterpret_cast<const __m128i *>(words[lane])));
                  continue;
                }
                const double average_weight =
                    strategy_weight * updating_parent_reach_at(local + lane);
                for (std::size_t action = 0U; action < 3U; ++action) {
                  auto strategy = static_cast<std::uint16_t>(words[lane][action] >> 13U);
                  strategy = encode_strategy11(
                      decode_strategy11(strategy) +
                      average_weight * strategies[action][local + lane]);
                  words[lane][action] =
                      encoded[action][lane] |
                      (static_cast<std::uint32_t>(strategy) << 13U);
                }
                store_three_words(
                    block + lane * 9U,
                    _mm_load_si128(reinterpret_cast<const __m128i *>(words[lane])));
              }
            }
          }
        }
        for (; local < updating_count; ++local) {
          const auto slot = board_player_slot(board, updating_player, local);
          const double average_weight =
              accumulate_average ? strategy_weight * updating_parent_reach_at(local) : 0.0;
          auto *const block = state + local * action_count * 3U;
          for (std::size_t action = 0; action < action_count; ++action) {
            auto *const bytes = block + action * 3U;
            const auto word = compact_word(bytes);
            const double regret_delta =
                effective_regret_update_weight *
                (action_values[action][slot] - values[slot]);
            const auto regret = encode_regret13(std::max(
                0.0, decode_regret13(static_cast<std::uint16_t>(word & 0x1fffU)) +
                         regret_delta));
            auto strategy = static_cast<std::uint16_t>(word >> 13U);
            if (accumulate_average) {
              strategy = encode_strategy11(decode_strategy11(strategy) +
                                           average_weight * strategies[action][slot]);
            }
            store_compact_word(bytes, static_cast<std::uint32_t>(regret) |
                                          (static_cast<std::uint32_t>(strategy) << 13U));
          }
        }
      } else if (direct_packed_pair_update) {
        auto *const regrets =
            buffers_.regret_float24 + static_cast<std::size_t>(decision.action_base) * 3U;
        std::size_t local = 0U;
        const __m128i expand_float24 =
            _mm_setr_epi8(0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8, -1, 9, 10, 11, -1);
        const __m128i compact_float24 =
            _mm_setr_epi8(0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14, -1, -1, -1, -1);
        const auto encode_four = [](const __m256d values) {
          const __m128 as_float = _mm256_cvtpd_ps(values);
          const __m128i bits =
              _mm_and_si128(_mm_castps_si128(as_float), _mm_set1_epi32(0x7fffffff));
          const __m128i discarded = _mm_and_si128(bits, _mm_set1_epi32(0x7f));
          __m128i packed = _mm_srli_epi32(bits, 7);
          const __m128i greater = _mm_cmpgt_epi32(discarded, _mm_set1_epi32(0x40));
          const __m128i equal = _mm_cmpeq_epi32(discarded, _mm_set1_epi32(0x40));
          const __m128i odd =
              _mm_cmpeq_epi32(_mm_and_si128(packed, _mm_set1_epi32(1)),
                              _mm_set1_epi32(1));
          const __m128i increment =
              _mm_and_si128(_mm_or_si128(greater, _mm_and_si128(equal, odd)),
                            _mm_set1_epi32(1));
          packed = _mm_add_epi32(packed, increment);
          return packed;
        };
        const auto store_four = [&](std::uint8_t *const destination,
                                    const __m128i packed) {
          const __m128i compact = _mm_shuffle_epi8(packed, compact_float24);
          _mm_storel_epi64(reinterpret_cast<__m128i *>(destination), compact);
          const std::uint32_t tail =
              static_cast<std::uint32_t>(_mm_cvtsi128_si32(_mm_srli_si128(compact, 8)));
          std::memcpy(destination + 8U, &tail, sizeof(tail));
        };
        for (; local + 4U < updating_count; local += 4U) {
          auto *const packed = regrets + local * 6U;
          const __m128 first_four = _mm_castsi128_ps(_mm_slli_epi32(
              _mm_shuffle_epi8(
                  _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed)), expand_float24),
              7));
          const __m128 second_four = _mm_castsi128_ps(_mm_slli_epi32(
              _mm_shuffle_epi8(
                  _mm_loadu_si128(reinterpret_cast<const __m128i *>(packed + 12U)),
                  expand_float24),
              7));
          const __m256d first_regrets =
              _mm256_cvtps_pd(_mm_shuffle_ps(first_four, second_four, _MM_SHUFFLE(2, 0, 2, 0)));
          const __m256d second_regrets =
              _mm256_cvtps_pd(_mm_shuffle_ps(first_four, second_four, _MM_SHUFFLE(3, 1, 3, 1)));
          const __m256d current_values = load_four_as_double(values.data() + local);
          const __m256d regret_weight = _mm256_set1_pd(effective_regret_update_weight);
          const __m256d updated_first = _mm256_max_pd(
              _mm256_setzero_pd(),
              _mm256_add_pd(first_regrets,
                            _mm256_mul_pd(
                                regret_weight,
                                _mm256_sub_pd(
                                    load_four_as_double(action_values[0].data() + local),
                                    current_values))));
          const __m256d updated_second = _mm256_max_pd(
              _mm256_setzero_pd(),
              _mm256_add_pd(second_regrets,
                            _mm256_mul_pd(
                                regret_weight,
                                _mm256_sub_pd(
                                    load_four_as_double(action_values[1].data() + local),
                                    current_values))));
          const __m128i encoded_first = encode_four(updated_first);
          const __m128i encoded_second = encode_four(updated_second);
          store_four(packed, _mm_unpacklo_epi32(encoded_first, encoded_second));
          store_four(packed + 12U, _mm_unpackhi_epi32(encoded_first, encoded_second));
        }
        for (; local < updating_count; ++local) {
          const auto slot = board_player_slot(board, updating_player, local);
          auto *const packed = regrets + local * 6U;
          std::uint64_t packed_pair = 0U;
          if (local + 1U < updating_count) {
            std::memcpy(&packed_pair, packed, sizeof(packed_pair));
          } else {
            for (std::size_t byte = 0; byte < 6U; ++byte) {
              packed_pair |= static_cast<std::uint64_t>(packed[byte]) << (byte * 8U);
            }
          }
          const auto first_bits =
              static_cast<std::uint32_t>(packed_pair & 0x00ffffffULL) << 7U;
          const auto second_bits =
              static_cast<std::uint32_t>((packed_pair >> 24U) & 0x00ffffffULL) << 7U;
          const double first = static_cast<double>(std::bit_cast<float>(first_bits));
          const double second = static_cast<double>(std::bit_cast<float>(second_bits));
          const double first_delta =
              effective_regret_update_weight * (action_values[0][slot] - values[slot]);
          const double second_delta =
              effective_regret_update_weight * (action_values[1][slot] - values[slot]);
          const double updated_first = std::max(0.0, first + first_delta);
          const double updated_second = std::max(0.0, second + second_delta);
          const std::uint64_t encoded =
              static_cast<std::uint64_t>(encode_float24_bits(updated_first)) |
              (static_cast<std::uint64_t>(encode_float24_bits(updated_second)) << 24U);
          std::memcpy(packed, &encoded, 6U);
        }
      } else if (direct_packed_three_update) {
        auto *const regrets =
            buffers_.regret_float24 + static_cast<std::size_t>(decision.action_base) * 3U;
        const auto encode_four = [](const __m256d input) {
          const __m128 as_float = _mm256_cvtpd_ps(input);
          const __m128i bits =
              _mm_and_si128(_mm_castps_si128(as_float), _mm_set1_epi32(0x7fffffff));
          const __m128i discarded = _mm_and_si128(bits, _mm_set1_epi32(0x7f));
          __m128i packed = _mm_srli_epi32(bits, 7);
          const __m128i greater = _mm_cmpgt_epi32(discarded, _mm_set1_epi32(0x40));
          const __m128i equal = _mm_cmpeq_epi32(discarded, _mm_set1_epi32(0x40));
          const __m128i odd = _mm_cmpeq_epi32(
              _mm_and_si128(packed, _mm_set1_epi32(1)), _mm_set1_epi32(1));
          return _mm_add_epi32(
              packed, _mm_and_si128(_mm_or_si128(greater, _mm_and_si128(equal, odd)),
                                    _mm_set1_epi32(1)));
        };
        std::size_t local = 0U;
        for (; local + 4U < updating_count; local += 4U) {
          alignas(32) double decoded[3][4];
          for (std::size_t lane = 0; lane < 4U; ++lane) {
            const auto *const block = regrets + (local + lane) * 9U;
            for (std::size_t action = 0; action < 3U; ++action) {
              decoded[action][lane] =
                  static_cast<double>(decode_float24_overread(block + action * 3U));
            }
          }
          const __m256d current = load_four_as_double(values.data() + local);
          const __m256d weight = _mm256_set1_pd(effective_regret_update_weight);
          alignas(16) std::uint32_t encoded[3][4];
          for (std::size_t action = 0; action < 3U; ++action) {
            const __m256d updated = _mm256_max_pd(
                _mm256_setzero_pd(),
                _mm256_add_pd(
                    _mm256_load_pd(decoded[action]),
                    _mm256_mul_pd(
                        weight,
                        _mm256_sub_pd(
                            load_four_as_double(action_values[action].data() + local), current))));
            _mm_store_si128(reinterpret_cast<__m128i *>(encoded[action]),
                            encode_four(updated));
          }
          for (std::size_t lane = 0; lane < 4U; ++lane) {
            auto *const destination = regrets + (local + lane) * 9U;
            const std::uint64_t first_eight =
                static_cast<std::uint64_t>(encoded[0][lane]) |
                (static_cast<std::uint64_t>(encoded[1][lane]) << 24U) |
                (static_cast<std::uint64_t>(encoded[2][lane]) << 48U);
            std::memcpy(destination, &first_eight, sizeof(first_eight));
            destination[8] =
                static_cast<std::uint8_t>(encoded[2][lane] >> 16U);
          }
        }
        for (; local < updating_count; ++local) {
          auto *const tail_block = regrets + local * 9U;
          for (std::size_t action = 0; action < 3U; ++action) {
            const double delta = effective_regret_update_weight *
                                 (action_values[action][local] - values[local]);
            encode_float24(tail_block + action * 3U,
                           std::max(0.0,
                                    static_cast<double>(
                                        decode_float24(tail_block + action * 3U)) +
                                        delta));
          }
        }
        if (accumulate_average) {
          for (std::size_t local_index = 0; local_index < updating_count; ++local_index) {
            const double weight = strategy_weight *
                                  updating_parent_reach_at(local_index);
            const auto offset = static_cast<std::size_t>(decision.action_base) +
                                local_index * 3U;
            for (std::size_t action = 0; action < 3U; ++action) {
              add_strategy(offset + action,
                           weight * strategies[action][local_index]);
            }
          }
        }
      } else if (direct_packed_block_update) {
        auto *const regrets =
            buffers_.regret_float24 + static_cast<std::size_t>(decision.action_base) * 3U;
        for (std::size_t local = 0; local < updating_count; ++local) {
          const auto slot = board_player_slot(board, updating_player, local);
          auto *const block = regrets + local * action_count * 3U;
          for (std::size_t action = 0; action < action_count; ++action) {
            auto *const packed = block + action * 3U;
            const double regret_delta =
                effective_regret_update_weight *
                (action_values[action][slot] - values[slot]);
            encode_float24(
                packed,
                std::max(0.0, static_cast<double>(decode_float24(packed)) + regret_delta));
          }
        }
        if (accumulate_average) {
          for (std::size_t local = 0; local < updating_count; ++local) {
            const auto slot = board_player_slot(board, updating_player, local);
            const double weight = strategy_weight * updating_parent_reach_at(local);
            const auto offset = static_cast<std::size_t>(decision.action_base) +
                                local * static_cast<std::size_t>(action_count);
            for (std::size_t action = 0; action < action_count; ++action) {
              add_strategy(offset + action, weight * strategies[action][slot]);
            }
          }
        }
      } else {
        const auto update_occurrence =
            [&](const DecisionLayout &occurrence_decision,
                const BoardData &occurrence_board,
                const std::uint8_t physical_to_representative_automorphism) {
              const auto occurrence_count =
                  occurrence_board.player_combos[updating_player].size();
              for (std::size_t local = 0; local < occurrence_count; ++local) {
                const auto occurrence_combo =
                    occurrence_board.player_combos[updating_player][local];
                const auto representative_combo =
                    orbit_context == nullptr
                        ? occurrence_combo
                        : layout_.automorphisms[physical_to_representative_automorphism]
                              .combos[occurrence_combo];
                const auto representative_local =
                    board.player_local[updating_player][representative_combo];
                if (representative_local < 0) {
                  return false;
                }
                const auto slot = board_player_slot(
                    board, updating_player,
                    static_cast<std::size_t>(representative_local));
                const auto offset = decision_action_base(
                    layout_, occurrence_decision, static_cast<std::int16_t>(local));
                for (std::size_t action = 0; action < action_count; ++action) {
                  const auto index = static_cast<std::size_t>(offset + action);
                  if (!locked_root) {
                    const auto regret_delta =
                        effective_regret_update_weight *
                        (action_values[action][slot] - values[slot]);
                    if (deferred_regret_delta_ != nullptr) {
                      add_deferred_regret(index, regret_delta);
                    } else if (buffers_.regret_float24 != nullptr) {
                      auto *const packed = buffers_.regret_float24 + index * 3U;
                      encode_float24(
                          packed,
                          std::max(0.0,
                                   static_cast<double>(decode_float24(packed)) +
                                       regret_delta));
                    } else {
                      buffers_.set_regret(
                          index,
                          std::max(0.0, buffers_.regret_at(index) + regret_delta));
                    }
                  }
                  if (!batch_half_average && accumulate_average) {
                    add_strategy(
                        index,
                        strategy_weight *
                            updating_parent_reach_at(
                                static_cast<std::size_t>(representative_local)) *
                            strategies[action][slot]);
                  }
                }
              }
              return true;
            };
        if (orbit_context != nullptr) {
          for (const auto &occurrence : *orbit_context) {
            const auto &occurrence_decision =
                layout_.decisions[static_cast<std::size_t>(occurrence.node)];
            const auto &occurrence_board =
                layout_.boards[occurrence_decision.board_index];
            if (!update_occurrence(
                    occurrence_decision, occurrence_board,
                    occurrence.physical_to_representative_automorphism)) {
              return PostflopSolverError::InvalidConfiguration;
            }
          }
        } else {
          if (!update_occurrence(decision, board, std::uint8_t{0})) {
            return PostflopSolverError::InvalidConfiguration;
          }
        }
      }
      const auto t_after_regret = profile ? std::chrono::steady_clock::now()
                                          : std::chrono::steady_clock::time_point{};
      if (profile) {
        prof_regret_update_seconds_ +=
            std::chrono::duration<double>(t_after_regret - t_after_value).count();
      }
      if (batch_half_average) {
        auto *const averages =
            buffers_.strategy_float16 + static_cast<std::size_t>(decision.action_base);
        std::size_t local = 0U;
        for (; local + 4U <= updating_count; local += 4U) {
          const __m128i encoded =
              _mm_loadu_si128(reinterpret_cast<const __m128i *>(averages + local * 2U));
          const __m256 current = _mm256_cvtph_ps(encoded);
          alignas(32) float current_values[8];
          alignas(32) double updated[8];
          _mm256_store_ps(current_values, current);
          for (std::size_t lane = 0; lane < 4U; ++lane) {
            const auto value_slot = board_player_slot(board, updating_player, local + lane);
            const double weight = strategy_weight *
                                  updating_parent_reach_at(local + lane);
            updated[lane * 2U] = static_cast<double>(current_values[lane * 2U]) +
                                 weight * strategies[0][value_slot];
            updated[lane * 2U + 1U] =
                static_cast<double>(current_values[lane * 2U + 1U]) +
                weight * strategies[1][value_slot];
          }
          const __m128 low = _mm256_cvtpd_ps(_mm256_load_pd(updated));
          const __m128 high = _mm256_cvtpd_ps(_mm256_load_pd(updated + 4U));
          __m256 as_float = _mm256_castps128_ps256(low);
          as_float = _mm256_insertf128_ps(as_float, high, 1);
          const __m128i result = _mm256_cvtps_ph(as_float, _MM_FROUND_TO_NEAREST_INT);
          _mm_storeu_si128(reinterpret_cast<__m128i *>(averages + local * 2U), result);
        }
        for (; local < updating_count; ++local) {
          const auto slot = board_player_slot(board, updating_player, local);
          const double weight = strategy_weight * updating_parent_reach_at(local);
          add_strategy(static_cast<std::size_t>(decision.action_base) + local * 2U,
                       weight * strategies[0][slot]);
          add_strategy(static_cast<std::size_t>(decision.action_base) + local * 2U + 1U,
                       weight * strategies[1][slot]);
        }
      }
      if (profile) {
        prof_average_update_seconds_ +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t_after_regret)
                .count();
      }
    } else {
      const auto updating_count = board.player_combos[updating_player].size();
      if constexpr (PlayerIndexed) {
        for (std::size_t action = 0; action < action_count; ++action) {
          for (std::size_t slot = 0; slot < updating_count; ++slot) {
            values[slot] += action_values[action][slot];
          }
        }
      } else {
        for (std::size_t local = 0; local < updating_count; ++local) {
          const auto slot = board_player_slot(board, updating_player, local);
          for (std::size_t action = 0; action < action_count; ++action) {
            values[slot] += action_values[action][slot];
          }
        }
      }
    }
    if (profile) {
      prof_value_update_seconds_ +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() - t_child_end).count();
      ++prof_decisions_;
    }
    return std::nullopt;
  }

  Result<ComboVector, PostflopSolverError> policy_chance(const PublicTreeNode &node,
                                                         const std::uint8_t updating_player,
                                                         const ReachRef &reach,
                                                         const bool best_response) {
    const auto &board = layout_.boards[layout_.node_board[static_cast<std::size_t>(node.id)]];
    auto values = zeroed_board_values(board, updating_player);
    if (node.edges.empty() || node.edges.front().total_legal_outcome_count <= 4U) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    const double denominator =
        static_cast<double>(node.edges.front().total_legal_outcome_count - 4U);
    const auto accumulate = [&](const PublicTreeEdge &edge, const ComboVector &child) {
      const double probability = static_cast<double>(edge.physical_outcome_count) / denominator;
      if constexpr (PlayerIndexed) {
        const auto &child_board =
            layout_.boards[layout_.node_board[static_cast<std::size_t>(edge.child)]];
        const auto &child_combos = child_board.player_combos[updating_player];
        for (std::size_t child_slot = 0; child_slot < child_combos.size(); ++child_slot) {
          const auto parent_slot = board.player_local[updating_player][child_combos[child_slot]];
          const auto parent = static_cast<std::size_t>(parent_slot);
          values[parent] = static_cast<Scalar>(values[parent] + probability * child[child_slot]);
        }
      } else {
        const auto &combos = board.player_combos[updating_player];
        for (std::size_t local = 0; local < combos.size(); ++local) {
          const auto combo_id = combos[local];
          if ((layout_.combo_masks[combo_id] & edge.chance_card.mask()) == 0U) {
            const auto slot = board_player_slot(board, updating_player, local);
            values[slot] = static_cast<Scalar>(values[slot] + probability * child[slot]);
          }
        }
      }
    };
    // Same coarse-grained parallel split as cfr_chance: fan the per-card
    // subtrees out over the worker pool while the current
    // thread keeps its own share, then join.
    const std::size_t edge_count = node.edges.size();
    const std::size_t worker_count = parallel_workers_.size();
    const std::size_t split = (std::popcount(board.mask) == 3U && worker_count > 0U)
                                  ? edge_count - edge_count / (worker_count + 1U)
                                  : 0U;
    if (split == 0U) {
      for (const auto &edge : node.edges) {
        const auto child =
            policy_physical(edge.child, updating_player, reach, best_response);
        if (!child) {
          return child;
        }
        accumulate(edge, child.value());
      }
      return Result<ComboVector, PostflopSolverError>::success(std::move(values));
    }
    std::vector<std::packaged_task<TraversalResult(DenseTraversal &)>> tasks;
    std::vector<std::future<TraversalResult>> futures;
    tasks.reserve(split);
    futures.reserve(split);
    for (std::size_t index = 0; index < split; ++index) {
      const auto &edge = node.edges[index];
      const ComboVector *child_reach_0 = reach[0];
      const ComboVector *child_reach_1 = reach[1];
      std::packaged_task<TraversalResult(DenseTraversal &)> task(
          [child = edge.child, updating_player, child_reach_0, child_reach_1,
           best_response](DenseTraversal &self) {
            return self.policy_physical(child, updating_player,
                                        {child_reach_0, child_reach_1}, best_response);
          });
      futures.push_back(task.get_future());
      tasks.push_back(std::move(task));
    }
    dispatch_parallel_tasks(tasks);
    // Both the worker subtrees (0..split) and the main thread's share
    // (split..edge_count) are joined, then accumulated in the original edge
    // order so the floating-point summation is bit-identical to the serial
    // traversal: the certification is a single-shot evaluation and must stay
    // bit-exact against the reference profile. Result vectors reserve without
    // zero-init (every slot is assigned before reading).
    std::vector<ComboVector> worker_results;
    worker_results.reserve(split);
    std::vector<ComboVector> main_results;
    main_results.reserve(edge_count - split);
    for (std::size_t index = split; index < edge_count; ++index) {
      const auto &edge = node.edges[index];
      const auto child =
          policy_physical(edge.child, updating_player, reach, best_response);
      if (!child) {
        return child;
      }
      main_results.emplace_back(child.value());
    }
    for (std::size_t index = 0; index < split; ++index) {
      auto child = futures[index].get();
      if (!child) {
        return Result<ComboVector, PostflopSolverError>::failure(child.error());
      }
      worker_results.emplace_back(child.value());
    }
    for (std::size_t index = 0; index < split; ++index) {
      accumulate(node.edges[index], worker_results[index]);
    }
    for (std::size_t index = split; index < edge_count; ++index) {
      accumulate(node.edges[index], main_results[index - split]);
    }
    return Result<ComboVector, PostflopSolverError>::success(std::move(values));
  }

  Result<ComboVector, PostflopSolverError> policy_decision(const PublicTreeNode &node,
                                                           const std::uint8_t updating_player,
                                                           const ReachRef &reach,
                                                           const bool best_response) {
    const auto &decision = layout_.decisions[static_cast<std::size_t>(node.id)];
    const auto &board = layout_.boards[decision.board_index];
    const auto action_count = static_cast<std::size_t>(decision.action_count);
    DecisionScratchLease scratch_lease(*this);
    auto &action_values = scratch_lease.get().action_values;
    auto &strategies = scratch_lease.get().strategies;
    const auto &actor_combos = board.player_combos[decision.player];
    const bool locked_node = is_locked_root(node);
    if (PlayerIndexed && !locked_node && action_count == 2U &&
        layout_.uses_direct_action_bases &&
        buffers_.strategy_float16 != nullptr) {
      const auto *averages =
          buffers_.strategy_float16 + static_cast<std::size_t>(decision.action_base);
      const __m256d zero = _mm256_setzero_pd();
      const __m256d one = _mm256_set1_pd(1.0);
      const __m256d half = _mm256_set1_pd(0.5);
      std::size_t local = 0U;
      for (; local + 4U <= actor_combos.size(); local += 4U) {
        const __m256 decoded = _mm256_cvtph_ps(_mm_loadu_si128(
            reinterpret_cast<const __m128i *>(averages + local * 2U)));
        const __m128 low = _mm256_castps256_ps128(decoded);
        const __m128 high = _mm256_extractf128_ps(decoded, 1);
        const __m256d first =
            _mm256_cvtps_pd(_mm_shuffle_ps(low, high, _MM_SHUFFLE(2, 0, 2, 0)));
        const __m256d second =
            _mm256_cvtps_pd(_mm_shuffle_ps(low, high, _MM_SHUFFLE(3, 1, 3, 1)));
        const __m256d sums = _mm256_add_pd(first, second);
        const __m256d empty = _mm256_cmp_pd(sums, zero, _CMP_LE_OS);
        const __m256d inverse = _mm256_div_pd(one, _mm256_blendv_pd(sums, one, empty));
        const __m256d first_strategy =
            _mm256_blendv_pd(_mm256_mul_pd(first, inverse), half, empty);
        const __m256d second_strategy =
            _mm256_blendv_pd(_mm256_mul_pd(second, inverse), half, empty);
        if constexpr (std::is_same_v<Scalar, float>) {
          _mm_storeu_ps(strategies[0].data() + local,
                        _mm256_cvtpd_ps(first_strategy));
          _mm_storeu_ps(strategies[1].data() + local,
                        _mm256_cvtpd_ps(second_strategy));
        } else {
          store_four_from_double(strategies[0].data() + local, first_strategy);
          store_four_from_double(strategies[1].data() + local, second_strategy);
        }
      }
      for (; local < actor_combos.size(); ++local) {
        const auto slot = board_player_slot(board, decision.player, local);
        const double first = static_cast<double>(decode_float16(averages[local * 2U]));
        const double second = static_cast<double>(decode_float16(averages[local * 2U + 1U]));
        const double sum = first + second;
        if (sum <= 0.0) {
          strategies[0][slot] = static_cast<Scalar>(0.5);
          strategies[1][slot] = static_cast<Scalar>(0.5);
        } else {
          const double inverse = 1.0 / sum;
          strategies[0][slot] = static_cast<Scalar>(first * inverse);
          strategies[1][slot] = static_cast<Scalar>(second * inverse);
        }
      }
    } else if (!locked_node && layout_.uses_direct_action_bases &&
               buffers_.strategy_float16 != nullptr) {
      const auto *averages =
          buffers_.strategy_float16 + static_cast<std::size_t>(decision.action_base);
      for (std::size_t local = 0; local < actor_combos.size(); ++local) {
        const auto slot = board_player_slot(board, decision.player, local);
        const auto *block = averages + local * action_count;
        double sum = 0.0;
        for (std::size_t action = 0; action < action_count; ++action) {
          const double value = static_cast<double>(decode_float16(block[action]));
          strategies[action][slot] = static_cast<Scalar>(value);
          sum += value;
        }
        if (sum <= 0.0) {
          const double uniform = 1.0 / static_cast<double>(action_count);
          for (std::size_t action = 0; action < action_count; ++action) {
            strategies[action][slot] = static_cast<Scalar>(uniform);
          }
        } else {
          for (std::size_t action = 0; action < action_count; ++action) {
            strategies[action][slot] = static_cast<Scalar>(
                static_cast<double>(strategies[action][slot]) / sum);
          }
        }
      }
    } else {
      for (std::size_t local = 0; local < actor_combos.size(); ++local) {
        const auto combo_id = actor_combos[local];
        const auto locked = locked_node ? locked_root_strategy(board.local_index[combo_id])
                                        : std::nullopt;
        const auto strategy =
            locked ? *locked
                   : current_strategy(decision, static_cast<std::int16_t>(local), true);
        const auto slot = board_player_slot(board, decision.player, local);
        for (std::size_t action = 0; action < action_count; ++action) {
          strategies[action][slot] = static_cast<Scalar>(strategy[action]);
        }
      }
    }
    for (std::size_t action = 0; action < action_count; ++action) {
      if (decision.player == updating_player) {
        const auto child = policy_physical(node.edges[action].child, updating_player,
                                           reach, best_response);
        if (!child) {
          return child;
        }
        if constexpr (PlayerIndexed) {
          std::copy_n(child.value().begin(), board.player_combos[updating_player].size(),
                      action_values[action].begin());
        } else {
          action_values[action] = child.value();
        }
        continue;
      }
      auto &actor_reach = scratch_lease.get().reach_actor[0];
      for (std::size_t local = 0; local < actor_combos.size(); ++local) {
        const auto strategy_slot = board_player_slot(board, decision.player, local);
        const auto reach_slot = board_reach_slot(board, decision.player, local);
        actor_reach[reach_slot] = static_cast<Scalar>(
            static_cast<double>((*reach[decision.player])[reach_slot]) *
            static_cast<double>(strategies[action][strategy_slot]));
      }
      const ReachRef child_reach =
          decision.player == 0U ? ReachRef{&actor_reach, reach[1]}
                                : ReachRef{reach[0], &actor_reach};
      const auto child = policy_physical(node.edges[action].child, updating_player,
                                         child_reach, best_response);
      if (!child) {
        return child;
      }
      if constexpr (PlayerIndexed) {
        std::copy_n(child.value().begin(), board.player_combos[updating_player].size(),
                    action_values[action].begin());
      } else {
        action_values[action] = child.value();
      }
    }
    auto values = zeroed_board_values(board, updating_player);
    const bool updating_actor = decision.player == updating_player;
    if (best_response && updating_actor && !is_locked_root(node)) {
      // Best-response branch: max over actions, order-independent.
      const auto updating_count = board.player_combos[updating_player].size();
      if constexpr (PlayerIndexed) {
        std::copy_n(action_values[0].begin(), updating_count, values.begin());
        for (std::size_t action = 1; action < action_count; ++action) {
          std::size_t slot = 0U;
          if constexpr (std::is_same_v<Scalar, float>) {
            for (; slot + 8U <= updating_count; slot += 8U) {
              _mm256_storeu_ps(
                  values.data() + slot,
                  _mm256_max_ps(_mm256_loadu_ps(values.data() + slot),
                                _mm256_loadu_ps(action_values[action].data() + slot)));
            }
          } else {
            for (; slot + 4U <= updating_count; slot += 4U) {
              _mm256_storeu_pd(
                  values.data() + slot,
                  _mm256_max_pd(load_four_as_double(values.data() + slot),
                                load_four_as_double(action_values[action].data() + slot)));
            }
          }
          for (; slot < updating_count; ++slot) {
            values[slot] = std::max(values[slot], action_values[action][slot]);
          }
        }
      } else {
        for (std::size_t local = 0; local < updating_count; ++local) {
          const auto slot = board_player_slot(board, updating_player, local);
          values[slot] = action_values[0][slot];
          for (std::size_t action = 1; action < action_count; ++action) {
            values[slot] = std::max(values[slot], action_values[action][slot]);
          }
        }
      }
    } else if (updating_actor) {
      if constexpr (PlayerIndexed) {
        // Action-outer, slot-inner: contiguous streams, auto-vectorized (see
        // cfr_decision).
        const auto updating_count = board.player_combos[updating_player].size();
        for (std::size_t action = 0; action < action_count; ++action) {
          for (std::size_t slot = 0; slot < updating_count; ++slot) {
            values[slot] += strategies[action][slot] * action_values[action][slot];
          }
        }
      } else {
        for (const ComboId combo_id : board.player_combos[updating_player]) {
          const auto slot = value_slot(combo_id, updating_player);
          for (std::size_t action = 0; action < action_count; ++action) {
            values[slot] += strategies[action][slot] * action_values[action][slot];
          }
        }
      }
    } else {
      const auto updating_count = board.player_combos[updating_player].size();
      if constexpr (PlayerIndexed) {
        for (std::size_t action = 0; action < action_count; ++action) {
          for (std::size_t slot = 0; slot < updating_count; ++slot) {
            values[slot] += action_values[action][slot];
          }
        }
      } else {
        for (std::size_t local = 0; local < updating_count; ++local) {
          const auto slot = board_player_slot(board, updating_player, local);
          for (std::size_t action = 0; action < action_count; ++action) {
            values[slot] += action_values[action][slot];
          }
        }
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(std::move(values));
  }

  DenseLayout &layout_;
  ActionBuffers buffers_;
  std::vector<double> *deferred_regret_delta_{nullptr};
  const PreparedRootLock *root_lock_{nullptr};
  std::vector<std::size_t> deferred_regret_touched_;
  std::vector<std::uint8_t> deferred_regret_touched_flags_;
  std::vector<double> parallel_regret_delta_;
  std::unique_ptr<DenseTraversal> parallel_worker_;
  std::vector<std::vector<double>> parallel_worker_deltas_;
  std::vector<std::unique_ptr<DenseTraversal>> parallel_workers_;
  std::shared_ptr<ParallelTaskQueue> parallel_shared_;
  std::size_t parallel_pool_size_{0};
  std::jthread parallel_thread_;
  std::uint8_t worker_logical_processor_{0};
  bool pin_worker_threads_{false};
  std::vector<std::unique_ptr<DecisionScratch>> decision_scratch_;
  // Showdown rank scratch, sized for the maximum rank space: rank_count is
  // the number of distinct hand values among the board's legal combos, always
  // <= combo_count, so these members cover every board without per-node heap
  // allocation (the old code heap-allocated ~5.4 KB per showdown node).
  std::array<double, combo_count> showdown_totals_{};
  std::array<double, combo_count> showdown_rank_base_{};
  std::array<double, 36U * combo_count> showdown_by_card_{};
  std::array<double, combo_count + 1U> showdown_prefix_{};
  std::array<double, 36U * (combo_count + 1U)> showdown_card_prefix_{};
  std::array<float, combo_count> showdown_totals_float_{};
  std::array<float, combo_count> showdown_rank_base_float_{};
  std::array<float, 36U * combo_count> showdown_by_card_float_{};
  std::array<float, combo_count + 1U> showdown_prefix_float_{};
  std::array<float, 36U * (combo_count + 1U)> showdown_card_prefix_float_{};
  std::size_t decision_scratch_depth_{0};
  std::uint64_t traversed_nodes_{0};
  PostflopWorkCounters work_counters_{};
  double maximum_normalization_error_{0.0};
  double regret_update_weight_{1.0};
  double strategy_weight_{0.0};
  double positive_regret_discount_{1.0};
  double negative_regret_discount_{1.0};
};

template <std::size_t Capacity, bool PlayerIndexed,
          typename Scalar = TraversalScalar<Capacity>>
std::array<std::array<Scalar, Capacity>, 2> initial_reach(const DenseLayout &layout) {
  std::array<std::array<Scalar, Capacity>, 2> reach{};
  if constexpr (PlayerIndexed) {
    // Each player's reach lives in its own flop-range space (contiguous
    // prefix of size player_flop_count[player]); the same seeding serves both
    // player passes.
    for (std::uint8_t player = 0; player < 2U; ++player) {
      for (std::size_t slot = 0; slot < layout.player_flop_count[player]; ++slot) {
        const auto combo = layout.player_flop_combos[player][slot];
        reach[player][slot] = static_cast<Scalar>(
            layout.initial_reach[player][combo]);
      }
    }
  } else {
    for (const ComboId combo : layout.active_combos) {
      const auto slot = Capacity == combo_count
                            ? static_cast<std::size_t>(combo)
                            : static_cast<std::size_t>(layout.active_combo_index[combo]);
      reach[0][slot] = static_cast<Scalar>(layout.initial_reach[0][combo]);
      reach[1][slot] = static_cast<Scalar>(layout.initial_reach[1][combo]);
    }
  }
  return reach;
}

class DenseTraversalRunner {
public:
  virtual ~DenseTraversalRunner() = default;
  [[nodiscard]] virtual Result<bool, PostflopSolverError> cfr(std::uint8_t updating_player,
                                                              double strategy_weight,
                                                              double regret_update_weight,
                                                              double positive_regret_discount,
                                                              double negative_regret_discount) = 0;
  [[nodiscard]] virtual Result<bool, PostflopSolverError> cfr_simultaneous(
      double strategy_weight, double regret_update_weight,
      double positive_regret_discount, double negative_regret_discount) = 0;
  [[nodiscard]] virtual Result<bool, PostflopSolverError> apply_deferred_regrets() = 0;
  [[nodiscard]] virtual std::uint64_t traversed_nodes() const noexcept = 0;
  [[nodiscard]] virtual PostflopWorkCounters work_counters() const noexcept = 0;
  [[nodiscard]] virtual double maximum_normalization_error() const noexcept = 0;
};

template <std::size_t Capacity, bool PlayerIndexed,
          typename ComputeScalar = TraversalScalar<Capacity>>
class TypedDenseTraversalRunner final : public DenseTraversalRunner {
public:
  TypedDenseTraversalRunner(DenseLayout &layout, const ActionBuffers buffers,
                            std::vector<double> *deferred_regret_delta,
                            const std::uint8_t parallel_action_depth,
                            const PreparedRootLock *root_lock)
      : traversal_(layout, buffers, deferred_regret_delta, parallel_action_depth,
                   root_lock, false),
        reach_(initial_reach<Capacity, PlayerIndexed, ComputeScalar>(layout)),
        root_(layout.tree.root),
        physical_node_count_(layout.uses_canonical_public_dag ? 0U
                                                              : layout.tree.stats.node_count) {}

  Result<bool, PostflopSolverError> cfr(const std::uint8_t updating_player,
                                        const double strategy_weight,
                                        const double regret_update_weight,
                                        const double positive_regret_discount,
                                        const double negative_regret_discount) override {
    const auto t0 = std::chrono::steady_clock::now();
    const auto traversed = traversal_.cfr(root_, updating_player, {&reach_[0], &reach_[1]},
                                          strategy_weight, regret_update_weight,
                                          positive_regret_discount,
                                          negative_regret_discount);
    const double wall =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!traversed) {
      return Result<bool, PostflopSolverError>::failure(traversed.error());
    }
    if (physical_node_count_ != 0U) {
      ++physical_passes_;
    }
    traversal_.dump_per_pass_profile(wall);
    return Result<bool, PostflopSolverError>::success(true);
  }

  Result<bool, PostflopSolverError> cfr_simultaneous(
      const double strategy_weight,
      const double regret_update_weight,
      const double positive_regret_discount,
      const double negative_regret_discount) override {
    const auto traversed = traversal_.cfr_simultaneous(
        {&reach_[0], &reach_[1]}, strategy_weight, regret_update_weight,
        positive_regret_discount, negative_regret_discount);
    if (traversed) {
      return Result<bool, PostflopSolverError>::failure(*traversed);
    }
    return Result<bool, PostflopSolverError>::success(true);
  }

  Result<bool, PostflopSolverError> apply_deferred_regrets() override {
    return traversal_.apply_deferred_regrets();
  }
  [[nodiscard]] std::uint64_t traversed_nodes() const noexcept override {
    return physical_node_count_ != 0U ? physical_passes_ * physical_node_count_
                                      : traversal_.traversed_nodes();
  }
  [[nodiscard]] PostflopWorkCounters work_counters() const noexcept override {
    return traversal_.work_counters();
  }
  [[nodiscard]] double maximum_normalization_error() const noexcept override {
    return traversal_.maximum_normalization_error();
  }

private:
  DenseTraversal<Capacity, PlayerIndexed, ComputeScalar> traversal_;
  std::array<std::array<ComputeScalar, Capacity>, 2> reach_;
  NodeId root_{0};
  std::uint64_t physical_node_count_{0};
  std::uint64_t physical_passes_{0};
};

std::unique_ptr<DenseTraversalRunner>
make_dense_traversal_runner(DenseLayout &layout, const ActionBuffers buffers,
                            std::vector<double> *deferred_regret_delta,
                            const std::uint8_t parallel_action_depth,
                            const PreparedRootLock *root_lock) {
  if (layout.uses_canonical_public_dag) {
    const bool compact_compute = buffers.compact_state != nullptr ||
                                 buffers.scaled_regret != nullptr;
    const auto maximum_player_combos =
        std::max(layout.player_flop_count[0], layout.player_flop_count[1]);
    if (maximum_player_combos <= medium_combo_capacity) {
      if (compact_compute) {
        return std::make_unique<
            TypedDenseTraversalRunner<medium_combo_capacity, true, float>>(
            layout, buffers, deferred_regret_delta, parallel_action_depth, root_lock);
      }
      return std::make_unique<TypedDenseTraversalRunner<medium_combo_capacity, true>>(
          layout, buffers, deferred_regret_delta, parallel_action_depth, root_lock);
    }
    if (maximum_player_combos <= short_deck_range_capacity) {
      if (compact_compute) {
        return std::make_unique<
            TypedDenseTraversalRunner<short_deck_range_capacity, true, float>>(
            layout, buffers, deferred_regret_delta, parallel_action_depth, root_lock);
      }
      return std::make_unique<TypedDenseTraversalRunner<short_deck_range_capacity, true>>(
          layout, buffers, deferred_regret_delta, parallel_action_depth, root_lock);
    }
    if (maximum_player_combos <= compact_player_combo_capacity) {
      if (compact_compute) {
        return std::make_unique<
            TypedDenseTraversalRunner<compact_player_combo_capacity, true, float>>(
            layout, buffers, deferred_regret_delta, parallel_action_depth, root_lock);
      }
      return std::make_unique<TypedDenseTraversalRunner<compact_player_combo_capacity, true>>(
          layout, buffers, deferred_regret_delta, parallel_action_depth, root_lock);
    }
    if (maximum_player_combos <= large_range_capacity) {
      if (compact_compute) {
        return std::make_unique<
            TypedDenseTraversalRunner<large_range_capacity, true, float>>(
            layout, buffers, deferred_regret_delta, parallel_action_depth, root_lock);
      }
      return std::make_unique<TypedDenseTraversalRunner<large_range_capacity, true>>(
          layout, buffers, deferred_regret_delta, parallel_action_depth, root_lock);
    }
    if (compact_compute) {
      return std::make_unique<TypedDenseTraversalRunner<combo_count, true, float>>(
          layout, buffers, deferred_regret_delta, parallel_action_depth, root_lock);
    }
    return std::make_unique<TypedDenseTraversalRunner<combo_count, true>>(
        layout, buffers, deferred_regret_delta, parallel_action_depth, root_lock);
  }
  if (layout.uses_direct_action_bases) {
    // Per-player physical path (identity automorphisms only): every action
    // index is touched exactly once per player pass (each combo is its own
    // infoset and each node is visited once), so the deferred regret delta
    // (667 MB double + the end-of-pass apply) is unnecessary: the update
    // loop's immediate-apply branch (deferred_regret_delta_ == nullptr) is
    // bit-exact for single-touch indices and eliminates the delta, the
    // worker merge and the whole regret_application phase.
    const auto maximum_player_combos =
        std::max(layout.player_flop_count[0], layout.player_flop_count[1]);
    if (maximum_player_combos <= medium_combo_capacity) {
      return std::make_unique<TypedDenseTraversalRunner<medium_combo_capacity, true>>(
          layout, buffers, nullptr, parallel_action_depth, root_lock);
    }
    if (maximum_player_combos <= short_deck_range_capacity) {
      return std::make_unique<TypedDenseTraversalRunner<short_deck_range_capacity, true>>(
          layout, buffers, nullptr, parallel_action_depth, root_lock);
    }
    if (maximum_player_combos <= compact_player_combo_capacity) {
      return std::make_unique<TypedDenseTraversalRunner<compact_player_combo_capacity, true>>(
          layout, buffers, nullptr, parallel_action_depth, root_lock);
    }
    if (maximum_player_combos <= large_range_capacity) {
      return std::make_unique<TypedDenseTraversalRunner<large_range_capacity, true>>(
          layout, buffers, nullptr, parallel_action_depth, root_lock);
    }
    return std::make_unique<TypedDenseTraversalRunner<combo_count, true>>(
        layout, buffers, nullptr, parallel_action_depth, root_lock);
  }
  return std::make_unique<TypedDenseTraversalRunner<combo_count, false>>(
      layout, buffers, deferred_regret_delta, parallel_action_depth, root_lock);
}

Result<double, PostflopSolverError>
measure_canonical_normalization_error(const DenseLayout &layout, const ActionBuffers buffers) {
  if (!layout.uses_isomorphic_infosets) {
    return Result<double, PostflopSolverError>::success(0.0);
  }
  if (layout.canonical_action_bases.size() != layout.canonical_action_counts.size()) {
    return Result<double, PostflopSolverError>::failure(PostflopSolverError::InvalidConfiguration);
  }
  double maximum_error = 0.0;
  for (std::size_t infoset = 0; infoset < layout.canonical_action_bases.size(); ++infoset) {
    const auto base = static_cast<std::size_t>(layout.canonical_action_bases[infoset]);
    const auto count = static_cast<std::size_t>(layout.canonical_action_counts[infoset]);
    for (const bool average : {false, true}) {
      double source_sum = 0.0;
      for (std::size_t action = 0; action < count; ++action) {
        const double source = average ? buffers.strategy_at(base + action)
                                      : std::max(0.0, buffers.regret_at(base + action));
        if (!std::isfinite(source)) {
          return Result<double, PostflopSolverError>::failure(
              PostflopSolverError::NumericalFailure);
        }
        source_sum += source;
      }
      double normalized_sum = 0.0;
      if (source_sum <= 0.0) {
        normalized_sum = static_cast<double>(count) / static_cast<double>(count);
      } else {
        for (std::size_t action = 0; action < count; ++action) {
          const double source = average ? buffers.strategy_at(base + action)
                                        : std::max(0.0, buffers.regret_at(base + action));
          normalized_sum += source / source_sum;
        }
      }
      maximum_error = std::max(maximum_error, std::abs(normalized_sum - 1.0));
    }
  }
  return Result<double, PostflopSolverError>::success(maximum_error);
}

// Sums a per-combo value vector weighted by the player's initial reach, in the
// same convention as analyze_postflop_node: profile/BR values are expressed as
// per-hand EVs in antes. The certification evaluates at the tree root, where
// the public-reach probability is exactly 1.0 by construction of
// initial_normalization; the division is kept to mirror the analysis path.
template <std::size_t Capacity, bool PlayerIndexed>
double reach_weighted_sum(const DenseLayout &layout,
                          const std::array<TraversalComboVector<Capacity>, 2> &reach,
                          const TraversalComboVector<Capacity> &values, const std::uint8_t player) {
  double total = 0.0;
  if constexpr (PlayerIndexed) {
    for (std::size_t slot = 0; slot < layout.player_flop_count[player]; ++slot) {
      total += reach[player][slot] * values[slot];
    }
  } else {
    for (std::size_t slot = 0; slot < Capacity; ++slot) {
      total += reach[player][slot] * values[slot];
    }
  }
  return total;
}

template <std::size_t Capacity, bool PlayerIndexed>
double root_public_reach_probability(const DenseLayout &layout,
                                     const std::array<TraversalComboVector<Capacity>, 2> &reach) {
  // In DAG mode build_layout clears node_board/tree.nodes, so the root board
  // must come from the canonical graph instead of the physical node mapping.
  const auto root_board_index =
      layout.uses_canonical_public_dag
          ? layout.canonical_public_graph.nodes[layout.canonical_public_graph.root].board_index
          : layout.node_board[layout.tree.root];
  const auto &board = layout.boards[root_board_index];
  double compatible_pair_mass = 0.0;
  if constexpr (PlayerIndexed) {
    // Per-player spaces: reach[0] is in P0's flop-range space and reach[1] in
    // P1's; map each combo through its player's slot table (-1 when the combo
    // is outside that player's flop range).
    for (const ComboId first : board.legal_combos) {
      const auto slot0 = layout.player_flop_slot[0][first];
      if (slot0 < 0 || !(reach[0][slot0] > 0.0)) {
        continue;
      }
      for (const ComboId second : board.legal_combos) {
        if ((layout.combo_masks[first] & layout.combo_masks[second]) == 0U) {
          const auto slot1 = layout.player_flop_slot[1][second];
          if (slot1 >= 0) {
            compatible_pair_mass += reach[0][slot0] * reach[1][slot1];
          }
        }
      }
    }
  } else {
    const auto slot_of = [&layout](const ComboId combo) -> std::size_t {
      if constexpr (Capacity == combo_count) {
        return static_cast<std::size_t>(combo);
      }
      return static_cast<std::size_t>(layout.active_combo_index[combo]);
    };
    for (const ComboId first : board.legal_combos) {
      if (!(reach[0][slot_of(first)] > 0.0)) {
        continue;
      }
      for (const ComboId second : board.legal_combos) {
        if ((layout.combo_masks[first] & layout.combo_masks[second]) == 0U) {
          compatible_pair_mass += reach[0][slot_of(first)] * reach[1][slot_of(second)];
        }
      }
    }
  }
  return layout.initial_normalization > 0.0
             ? compatible_pair_mass / layout.initial_normalization
             : 0.0;
}

template <std::size_t Capacity, bool PlayerIndexed>
Result<PostflopCertification, PostflopSolverError>
certify_typed(DenseLayout &layout, const ActionBuffers buffers, const std::uint64_t iteration,
              const PreparedRootLock *root_lock) {
  const auto reach = initial_reach<Capacity, PlayerIndexed>(layout);
  const double public_reach_probability =
      root_public_reach_probability<Capacity, PlayerIndexed>(layout, reach);
  if (!(public_reach_probability > 0.0)) {
    return Result<PostflopCertification, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  const auto aggregate_value = [&](const std::uint8_t player,
                                   const TraversalComboVector<Capacity> &values) {
    return reach_weighted_sum<Capacity, PlayerIndexed>(layout, reach, values, player) /
           public_reach_probability;
  };
  for (std::uint32_t board_index = 0; board_index < layout.boards.size(); ++board_index) {
    if (std::popcount(layout.boards[board_index].mask) == 5) {
      const auto prepared = prepare_ranks(layout, board_index);
      if (!prepared) {
        return Result<PostflopCertification, PostflopSolverError>::failure(prepared.error());
      }
    }
  }
  if (!layout.uses_canonical_public_dag) {
    // Policy-only traversal: the pool works without a deferred regret delta
    // (the policy path never touches regret state), so each of the four
    // profile/BR evaluations runs on the worker pool at the turn chance.
    // PagedActionFile owns a mutable LRU page cache and cannot serve concurrent
    // policy reads safely. Keep out-of-core certification on one traversal;
    // the in-memory backend retains turn-chance parallelism.
    const std::uint8_t certification_parallel_depth = buffers.paged != nullptr ? 0U : 7U;
    DenseTraversal<Capacity, PlayerIndexed> traversal(layout, buffers, nullptr,
                                                       certification_parallel_depth, root_lock);
    PostflopCertification certification;
    certification.iteration = iteration;
    const auto evaluate = [&](const std::uint8_t player, const bool best_response)
        -> Result<double, PostflopSolverError> {
      const std::array<const TraversalComboVector<Capacity> *, 2> reach_ref{&reach[0], &reach[1]};
      const auto values =
          traversal.policy(layout.tree.root, player, reach_ref, best_response);
      if (!values) {
        return Result<double, PostflopSolverError>::failure(values.error());
      }
      return Result<double, PostflopSolverError>::success(
          aggregate_value(player, values.value()));
    };
    const auto profile_zero = evaluate(0U, false);
    const auto response_zero = evaluate(0U, true);
    const auto response_one = evaluate(1U, true);
    if (!profile_zero || !response_zero || !response_one) {
      return Result<PostflopCertification, PostflopSolverError>::failure(
          !profile_zero ? profile_zero.error()
                        : (!response_zero ? response_zero.error() : response_one.error()));
    }
    certification.profile_value_antes[0] = profile_zero.value();
    certification.best_response_value_antes[0] = response_zero.value();
    certification.best_response_value_antes[1] = response_one.value();
    if (!layout.tree.config.rake.enabled) {
      certification.profile_value_antes[1] = -certification.profile_value_antes[0];
    } else {
      const auto profile_one = evaluate(1U, false);
      if (!profile_one) {
        return Result<PostflopCertification, PostflopSolverError>::failure(profile_one.error());
      }
      certification.profile_value_antes[1] = profile_one.value();
    }
    certification.nash_conv_antes =
        (certification.best_response_value_antes[0] - certification.profile_value_antes[0]) +
        (certification.best_response_value_antes[1] - certification.profile_value_antes[1]);
    certification.expected_payoff_sum_antes =
        certification.profile_value_antes[0] + certification.profile_value_antes[1];
    const double initial_pot =
        static_cast<double>(layout.tree.config.initial_pot.units()) / units_per_ante;
    certification.normalized_nash_conv = initial_pot > 0.0
                                             ? certification.nash_conv_antes / initial_pot
                                             : std::numeric_limits<double>::quiet_NaN();
    if (!std::isfinite(certification.nash_conv_antes) ||
        !std::isfinite(certification.normalized_nash_conv)) {
      return Result<PostflopCertification, PostflopSolverError>::failure(
          PostflopSolverError::NumericalFailure);
    }
    return Result<PostflopCertification, PostflopSolverError>::success(certification);
  }
  const auto evaluate = [&layout, buffers, &reach, root_lock,
                         public_reach_probability](const std::uint8_t player,
                                                   const bool best_response,
                                                   const std::uint8_t worker_count) {
    const std::uint8_t certification_worker_count =
        buffers.paged != nullptr ? 0U : worker_count;
    auto traversal = std::make_unique<DenseTraversal<Capacity, PlayerIndexed>>(
        layout, buffers, nullptr, certification_worker_count, root_lock);
    const std::array<const TraversalComboVector<Capacity> *, 2> reach_ref{&reach[0], &reach[1]};
    const auto values = traversal->policy(layout.tree.root, player, reach_ref, best_response);
    if (!values) {
      return Result<double, PostflopSolverError>::failure(values.error());
    }
    return Result<double, PostflopSolverError>::success(
        reach_weighted_sum<Capacity, PlayerIndexed>(layout, reach, values.value(), player) /
        public_reach_probability);
  };
  const auto evaluated = [&]() -> std::array<Result<double, PostflopSolverError>, 4> {
    const auto mirror_profile = [](const Result<double, PostflopSolverError> &profile) {
      return profile
                 ? Result<double, PostflopSolverError>::success(-profile.value())
                 : Result<double, PostflopSolverError>::failure(profile.error());
    };
    if (buffers.paged != nullptr) {
      // PagedActionFile owns a mutable LRU page cache. Concurrent get() calls
      // would race on page_lookup_/pages_ and can return corrupt values or
      // crash. Keep exact certification sequential for this backend; in-RAM
      // buffers retain the four-way parallel evaluation below.
      auto profile_zero = evaluate(std::uint8_t{0}, false, 0U);
      auto response_zero = evaluate(std::uint8_t{0}, true, 0U);
      auto response_one = evaluate(std::uint8_t{1}, true, 0U);
      auto profile_one = layout.tree.config.rake.enabled
                             ? evaluate(std::uint8_t{1}, false, 0U)
                             : mirror_profile(profile_zero);
      return {std::move(profile_zero), std::move(response_zero),
              std::move(profile_one), std::move(response_one)};
    }
    if (!layout.tree.config.rake.enabled) {
      // Zero-rake heads-up poker is exactly zero-sum, so profile EV1 is the
      // negative of profile EV0.  Three independent traversals remain; assign
      // 2, 3 and 3 threads respectively (async caller included) for exactly
      // eight certification threads.
      auto profile_zero = std::async(std::launch::async, evaluate,
                                     std::uint8_t{0}, false, std::uint8_t{1});
      auto response_zero = std::async(std::launch::async, evaluate,
                                      std::uint8_t{0}, true, std::uint8_t{2});
      auto response_one = std::async(std::launch::async, evaluate,
                                     std::uint8_t{1}, true, std::uint8_t{2});
      auto profile_zero_result = profile_zero.get();
      auto profile_one_result = mirror_profile(profile_zero_result);
      return {std::move(profile_zero_result), response_zero.get(),
              std::move(profile_one_result), response_one.get()};
    }
    // With rake, all four profile/BR values are independent. One worker per
    // async traversal keeps the total at exactly eight threads.
    auto profile_zero = std::async(std::launch::async, evaluate,
                                   std::uint8_t{0}, false, std::uint8_t{1});
    auto response_zero = std::async(std::launch::async, evaluate,
                                    std::uint8_t{0}, true, std::uint8_t{1});
    auto profile_one = std::async(std::launch::async, evaluate,
                                  std::uint8_t{1}, false, std::uint8_t{1});
    auto response_one = std::async(std::launch::async, evaluate,
                                   std::uint8_t{1}, true, std::uint8_t{1});
    return {profile_zero.get(), response_zero.get(), profile_one.get(),
            response_one.get()};
  }();
  for (const auto &value : evaluated) {
    if (!value) {
      return Result<PostflopCertification, PostflopSolverError>::failure(value.error());
    }
  }
  PostflopCertification certification;
  certification.iteration = iteration;
  certification.profile_value_antes = {evaluated[0].value(), evaluated[2].value()};
  certification.best_response_value_antes = {evaluated[1].value(), evaluated[3].value()};
  certification.nash_conv_antes =
      (certification.best_response_value_antes[0] - certification.profile_value_antes[0]) +
      (certification.best_response_value_antes[1] - certification.profile_value_antes[1]);
  certification.expected_payoff_sum_antes =
      certification.profile_value_antes[0] + certification.profile_value_antes[1];
  const double initial_pot =
      static_cast<double>(layout.tree.config.initial_pot.units()) / units_per_ante;
  certification.normalized_nash_conv = initial_pot > 0.0
                                           ? certification.nash_conv_antes / initial_pot
                                           : std::numeric_limits<double>::quiet_NaN();
  if (!std::isfinite(certification.nash_conv_antes) ||
      !std::isfinite(certification.normalized_nash_conv)) {
    return Result<PostflopCertification, PostflopSolverError>::failure(
        PostflopSolverError::NumericalFailure);
  }
  return Result<PostflopCertification, PostflopSolverError>::success(certification);
}

Result<PostflopCertification, PostflopSolverError>
certify(DenseLayout &layout, const ActionBuffers buffers, const std::uint64_t iteration,
        const PreparedRootLock *root_lock) {
  if (layout.uses_canonical_public_dag) {
    const auto maximum_player_combos =
        std::max(layout.player_flop_count[0], layout.player_flop_count[1]);
    if (maximum_player_combos <= medium_combo_capacity) {
      return certify_typed<medium_combo_capacity, true>(layout, buffers, iteration, root_lock);
    }
    if (maximum_player_combos <= short_deck_range_capacity) {
      return certify_typed<short_deck_range_capacity, true>(layout, buffers, iteration,
                                                             root_lock);
    }
    if (maximum_player_combos <= compact_player_combo_capacity) {
      return certify_typed<compact_player_combo_capacity, true>(layout, buffers, iteration,
                                                                 root_lock);
    }
    if (maximum_player_combos <= large_range_capacity) {
      return certify_typed<large_range_capacity, true>(layout, buffers, iteration, root_lock);
    }
    return certify_typed<combo_count, true>(layout, buffers, iteration, root_lock);
  }
  if (layout.uses_direct_action_bases) {
    if (std::max(layout.player_flop_count[0], layout.player_flop_count[1]) <=
        compact_player_combo_capacity) {
      return certify_typed<compact_player_combo_capacity, true>(layout, buffers, iteration,
                                                                 root_lock);
    }
    return certify_typed<combo_count, true>(layout, buffers, iteration, root_lock);
  }
  return certify_typed<combo_count, false>(layout, buffers, iteration, root_lock);
}

template <std::size_t Capacity, bool PlayerIndexed>
Result<std::array<double, 2>, PostflopSolverError>
profile_root_values_typed(DenseLayout &layout, const ActionBuffers buffers) {
  const auto reach = initial_reach<Capacity, PlayerIndexed>(layout);
  const double public_reach_probability =
      root_public_reach_probability<Capacity, PlayerIndexed>(layout, reach);
  if (!(public_reach_probability > 0.0)) {
    return Result<std::array<double, 2>, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  auto traversal = std::make_unique<DenseTraversal<Capacity, PlayerIndexed>>(
      layout, buffers, nullptr, std::uint8_t{0}, nullptr);
  const std::array<const TraversalComboVector<Capacity> *, 2> reach_ref{
      &reach[0], &reach[1]};
  const auto evaluate = [&](const std::uint8_t player)
      -> Result<double, PostflopSolverError> {
    const auto values = traversal->policy(layout.tree.root, player, reach_ref, false);
    if (!values) {
      return Result<double, PostflopSolverError>::failure(values.error());
    }
    return Result<double, PostflopSolverError>::success(
        reach_weighted_sum<Capacity, PlayerIndexed>(layout, reach, values.value(), player) /
        public_reach_probability);
  };
  const auto profile_zero = evaluate(0U);
  if (!profile_zero) {
    return Result<std::array<double, 2>, PostflopSolverError>::failure(
        profile_zero.error());
  }
  std::array<double, 2> profile{profile_zero.value(), -profile_zero.value()};
  if (layout.tree.config.rake.enabled) {
    const auto profile_one = evaluate(1U);
    if (!profile_one) {
      return Result<std::array<double, 2>, PostflopSolverError>::failure(
          profile_one.error());
    }
    profile[1] = profile_one.value();
  }
  return Result<std::array<double, 2>, PostflopSolverError>::success(profile);
}

Result<std::array<double, 2>, PostflopSolverError>
profile_root_values(DenseLayout &layout, const ActionBuffers buffers) {
  if (layout.uses_canonical_public_dag) {
    const auto maximum_player_combos =
        std::max(layout.player_flop_count[0], layout.player_flop_count[1]);
    if (maximum_player_combos <= medium_combo_capacity) {
      return profile_root_values_typed<medium_combo_capacity, true>(layout, buffers);
    }
    if (maximum_player_combos <= short_deck_range_capacity) {
      return profile_root_values_typed<short_deck_range_capacity, true>(layout, buffers);
    }
    if (maximum_player_combos <= compact_player_combo_capacity) {
      return profile_root_values_typed<compact_player_combo_capacity, true>(layout, buffers);
    }
    if (maximum_player_combos <= large_range_capacity) {
      return profile_root_values_typed<large_range_capacity, true>(layout, buffers);
    }
    return profile_root_values_typed<combo_count, true>(layout, buffers);
  }
  return profile_root_values_typed<combo_count, false>(layout, buffers);
}

std::uint64_t double_bits(const double value) { return std::bit_cast<std::uint64_t>(value); }
double bits_double(const std::uint64_t value) { return std::bit_cast<double>(value); }

void checksum_bytes(std::uint64_t &checksum, const void *const data, const std::size_t size) {
  const auto *const bytes = static_cast<const unsigned char *>(data);
  for (std::size_t index = 0; index < size; ++index) {
    checksum ^= bytes[index];
    checksum *= 1099511628211ULL;
  }
}

template <typename Value>
bool write_binary(std::ofstream &output, const Value &value, std::uint64_t &checksum) {
  output.write(reinterpret_cast<const char *>(&value), sizeof(Value));
  checksum_bytes(checksum, &value, sizeof(Value));
  return static_cast<bool>(output);
}

template <typename Value>
bool read_binary(std::ifstream &input, Value &value, std::uint64_t &checksum) {
  input.read(reinterpret_cast<char *>(&value), sizeof(Value));
  if (!input) {
    return false;
  }
  checksum_bytes(checksum, &value, sizeof(Value));
  return true;
}

bool atomic_replace(const std::string &temporary, const std::filesystem::path &target) {
#ifdef _WIN32
  return MoveFileExA(temporary.c_str(), target.string().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  std::error_code rename_error;
  std::filesystem::rename(temporary, target, rename_error);
  return !rename_error;
#endif
}

Result<PostflopStrategyQuery, PostflopSolverError>
query_strategy_from_layout(const DenseLayout &layout, const ActionBuffers buffers,
                           const NodeId public_node, const ComboId combo) {
  if (combo >= combo_count) {
    return Result<PostflopStrategyQuery, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  const DecisionLayout *decision_ptr = nullptr;
  ComboId state_combo = combo;
  const CanonicalPublicNode *canonical = nullptr;
  if (layout.uses_canonical_public_dag) {
    if (public_node >= layout.canonical_public_graph.physical_assignments.size()) {
      return Result<PostflopStrategyQuery, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    const auto assignment =
        layout.canonical_public_graph.physical_assignments[static_cast<std::size_t>(public_node)];
    if (assignment.node >= layout.canonical_public_graph.nodes.size() ||
        assignment.physical_to_canonical_automorphism >= layout.automorphisms.size()) {
      return Result<PostflopStrategyQuery, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    canonical = &layout.canonical_public_graph.nodes[assignment.node];
    decision_ptr = &canonical->decision;
    state_combo = layout.automorphisms[assignment.physical_to_canonical_automorphism].combos[combo];
  } else {
    if (public_node >= layout.tree.nodes.size()) {
      return Result<PostflopStrategyQuery, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    decision_ptr = &layout.decisions[static_cast<std::size_t>(public_node)];
  }
  const auto &decision = *decision_ptr;
  if (!decision.present) {
    return Result<PostflopStrategyQuery, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  const auto &board = layout.boards[decision.board_index];
  // Decision state is allocated over the acting player's live range, not the
  // union of both ranges.  Using BoardData::local_index here happens to work
  // for identical ranges, but addresses the wrong action block (or runs past
  // the node's block) as soon as the two physical ranges are asymmetric.
  const auto local = board.player_local[decision.player][state_combo];
  if (local < 0) {
    return Result<PostflopStrategyQuery, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  const auto offset = canonical != nullptr ? canonical_action_base(*canonical, local)
                                           : decision_action_base(layout, decision, local);
  PostflopStrategyQuery query;
  query.public_node = public_node;
  query.combo = combo;
  query.actions.reserve(decision.action_count);
  query.probabilities.resize(decision.action_count);
  double sum = 0.0;
  for (std::size_t action = 0; action < decision.action_count; ++action) {
    query.actions.push_back(canonical != nullptr
                                ? canonical->edges[action].action
                                : layout.tree.nodes[static_cast<std::size_t>(public_node)]
                                      .edges[action]
                                      .action);
    const auto state_index =
        canonical != nullptr &&
                (buffers.scaled_strategy != nullptr || buffers.action_major_compact)
            ? canonical_action_major_index(*canonical, static_cast<std::size_t>(local), action)
            : static_cast<std::size_t>(offset + action);
    query.probabilities[action] = buffers.strategy_at(state_index);
    sum += query.probabilities[action];
  }
  if (sum <= 0.0) {
    std::fill(query.probabilities.begin(), query.probabilities.end(),
              1.0 / static_cast<double>(decision.action_count));
  } else {
    for (double &probability : query.probabilities) {
      probability /= sum;
    }
  }
  return Result<PostflopStrategyQuery, PostflopSolverError>::success(std::move(query));
}

Result<ActionBuffers, PostflopSolverError>
in_memory_checkpoint_buffers(const PostflopCheckpoint &checkpoint) {
  if (checkpoint.state_precision == PostflopStatePrecision::ScaledUint16RegretStrategy) {
    if (checkpoint.cumulative_regret_uint16.size() != checkpoint.action_count ||
        checkpoint.cumulative_strategy_uint16.size() != checkpoint.action_count ||
        checkpoint.regret_node_scale.size() != checkpoint.decision_node_count ||
        checkpoint.strategy_node_scale.size() != checkpoint.decision_node_count ||
        !checkpoint.cumulative_regret.empty() || !checkpoint.cumulative_strategy.empty() ||
        !checkpoint.cumulative_regret_float32.empty() ||
        !checkpoint.cumulative_strategy_float32.empty() ||
        !checkpoint.cumulative_regret_float24.empty() ||
        !checkpoint.cumulative_strategy_float16.empty() ||
        !checkpoint.cumulative_compact_state.empty()) {
      return Result<ActionBuffers, PostflopSolverError>::failure(
          PostflopSolverError::CheckpointMismatch);
    }
    ActionBuffers buffers;
    buffers.count = static_cast<std::size_t>(checkpoint.action_count);
    buffers.scaled_regret =
        const_cast<std::uint16_t *>(checkpoint.cumulative_regret_uint16.data());
    buffers.scaled_strategy =
        const_cast<std::uint16_t *>(checkpoint.cumulative_strategy_uint16.data());
    buffers.regret_node_scale =
        const_cast<float *>(checkpoint.regret_node_scale.data());
    buffers.strategy_node_scale =
        const_cast<float *>(checkpoint.strategy_node_scale.data());
    buffers.decision_node_count =
        static_cast<std::size_t>(checkpoint.decision_node_count);
    buffers.signed_scaled_regret =
        checkpoint.algorithm == PostflopAlgorithm::Dcfr ||
        checkpoint.algorithm == PostflopAlgorithm::HsDcfr30;
    return Result<ActionBuffers, PostflopSolverError>::success(buffers);
  }
  if (checkpoint.state_precision == PostflopStatePrecision::Float13RegretFloat11Strategy ||
      checkpoint.state_precision ==
          PostflopStatePrecision::ActionMajorFloat13RegretFloat11Strategy) {
    if (checkpoint.action_count > std::numeric_limits<std::size_t>::max() / 3U ||
        checkpoint.cumulative_compact_state.size() != checkpoint.action_count * 3U ||
        !checkpoint.cumulative_regret.empty() || !checkpoint.cumulative_strategy.empty() ||
        !checkpoint.cumulative_regret_float32.empty() ||
        !checkpoint.cumulative_strategy_float32.empty() ||
        !checkpoint.cumulative_regret_float24.empty() ||
        !checkpoint.cumulative_strategy_float16.empty()) {
      return Result<ActionBuffers, PostflopSolverError>::failure(
          PostflopSolverError::CheckpointMismatch);
    }
    ActionBuffers buffers;
    buffers.count = static_cast<std::size_t>(checkpoint.action_count);
    buffers.compact_state =
        const_cast<std::uint8_t *>(checkpoint.cumulative_compact_state.data());
    buffers.action_major_compact =
        checkpoint.state_precision ==
        PostflopStatePrecision::ActionMajorFloat13RegretFloat11Strategy;
    return Result<ActionBuffers, PostflopSolverError>::success(buffers);
  }
  if (checkpoint.state_precision == PostflopStatePrecision::Float24RegretFloat16Strategy) {
    if (checkpoint.action_count > std::numeric_limits<std::size_t>::max() / 3U ||
        checkpoint.cumulative_regret_float24.size() != checkpoint.action_count * 3U ||
        checkpoint.cumulative_strategy_float16.size() != checkpoint.action_count ||
        !checkpoint.cumulative_regret.empty() || !checkpoint.cumulative_strategy.empty() ||
        !checkpoint.cumulative_regret_float32.empty() ||
        !checkpoint.cumulative_strategy_float32.empty() ||
        !checkpoint.cumulative_compact_state.empty()) {
      return Result<ActionBuffers, PostflopSolverError>::failure(
          PostflopSolverError::CheckpointMismatch);
    }
    return Result<ActionBuffers, PostflopSolverError>::success(
        {nullptr, nullptr, static_cast<std::size_t>(checkpoint.action_count), nullptr, nullptr,
         nullptr, const_cast<std::uint8_t *>(checkpoint.cumulative_regret_float24.data()),
         const_cast<std::uint16_t *>(checkpoint.cumulative_strategy_float16.data())});
  }
  if (checkpoint.state_precision == PostflopStatePrecision::Float32) {
    if (checkpoint.cumulative_regret_float32.size() != checkpoint.action_count ||
        checkpoint.cumulative_strategy_float32.size() != checkpoint.action_count ||
        !checkpoint.cumulative_regret.empty() || !checkpoint.cumulative_strategy.empty() ||
        !checkpoint.cumulative_regret_float24.empty() ||
        !checkpoint.cumulative_strategy_float16.empty() ||
        !checkpoint.cumulative_compact_state.empty()) {
      return Result<ActionBuffers, PostflopSolverError>::failure(
          PostflopSolverError::CheckpointMismatch);
    }
    return Result<ActionBuffers, PostflopSolverError>::success(
        {nullptr, nullptr, checkpoint.cumulative_regret_float32.size(), nullptr,
         const_cast<float *>(checkpoint.cumulative_regret_float32.data()),
         const_cast<float *>(checkpoint.cumulative_strategy_float32.data())});
  }
  if (checkpoint.cumulative_regret.size() != checkpoint.action_count ||
      checkpoint.cumulative_strategy.size() != checkpoint.action_count ||
      !checkpoint.cumulative_regret_float32.empty() ||
      !checkpoint.cumulative_strategy_float32.empty() ||
      !checkpoint.cumulative_regret_float24.empty() ||
      !checkpoint.cumulative_strategy_float16.empty() ||
      !checkpoint.cumulative_compact_state.empty()) {
    return Result<ActionBuffers, PostflopSolverError>::failure(
        PostflopSolverError::CheckpointMismatch);
  }
  return Result<ActionBuffers, PostflopSolverError>::success(
      {const_cast<double *>(checkpoint.cumulative_regret.data()),
       const_cast<double *>(checkpoint.cumulative_strategy.data()),
       checkpoint.cumulative_regret.size()});
}

} // namespace

HsDcfrSchedulePoint hs_dcfr30_schedule(const std::uint64_t iteration) noexcept {
  const double t = static_cast<double>(iteration);
  return {
      .alpha = std::min(5.0, 1.0 + 0.003 * t),
      .beta = std::max(-5.0, -1.0 - 0.002 * t),
      .gamma = std::max(5.0, 30.0 - 0.005 * t),
  };
}

struct PostflopPreparedTree::Impl {
  PostflopTreeConfig config;
  PostflopRanges ranges;
  DenseLayout layout;
  std::optional<DenseLayout> analysis_layout;
  double preparation_seconds{0.0};
  bool lossless_isomorphism_enabled{true};
  bool canonical_public_dag_enabled{true};
};

PostflopPreparedTree::PostflopPreparedTree(std::unique_ptr<Impl> implementation)
    : implementation_(std::move(implementation)) {}

PostflopPreparedTree::~PostflopPreparedTree() = default;
PostflopPreparedTree::PostflopPreparedTree(PostflopPreparedTree &&) noexcept = default;
PostflopPreparedTree &PostflopPreparedTree::operator=(PostflopPreparedTree &&) noexcept = default;

Result<std::shared_ptr<PostflopPreparedTree>, PostflopSolverError>
prepare_postflop_tree(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                      const bool enable_lossless_isomorphism,
                      const bool enable_canonical_public_dag, const bool prepare_analysis) {
  if (!validate_postflop_ranges(config, ranges)) {
    return Result<std::shared_ptr<PostflopPreparedTree>, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  const auto started = std::chrono::steady_clock::now();
  auto layout =
      build_layout(config, ranges, enable_lossless_isomorphism, enable_canonical_public_dag);
  if (!layout) {
    return Result<std::shared_ptr<PostflopPreparedTree>, PostflopSolverError>::failure(
        layout.error());
  }
  auto implementation = std::make_unique<PostflopPreparedTree::Impl>();
  implementation->config = config;
  implementation->ranges = ranges;
  implementation->layout = std::move(layout.value());
  if (prepare_analysis && implementation->layout.tree.nodes.empty()) {
    auto analysis_layout =
        build_layout(config, ranges, enable_lossless_isomorphism,
                     enable_canonical_public_dag, true);
    if (!analysis_layout) {
      return Result<std::shared_ptr<PostflopPreparedTree>, PostflopSolverError>::failure(
          analysis_layout.error());
    }
    implementation->analysis_layout = std::move(analysis_layout.value());
  }
  implementation->lossless_isomorphism_enabled = enable_lossless_isomorphism;
  implementation->canonical_public_dag_enabled = enable_canonical_public_dag;
  implementation->preparation_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  return Result<std::shared_ptr<PostflopPreparedTree>, PostflopSolverError>::success(
      std::shared_ptr<PostflopPreparedTree>(new PostflopPreparedTree(std::move(implementation))));
}

PostflopLayoutEstimate prepared_postflop_layout_estimate(const PostflopPreparedTree &prepared) {
  const auto &layout = prepared.implementation_->layout;
  return {layout.tree.stats,
          static_cast<std::uint64_t>(layout.canonical_public_graph.nodes.size()),
          layout.information_sets,
          layout.actions,
          layout.actions * sizeof(double),
          layout.actions * sizeof(double)};
}

std::shared_ptr<const PublicTree>
prepared_postflop_public_tree(const std::shared_ptr<PostflopPreparedTree> &prepared) {
  if (!prepared || !prepared->implementation_) {
    return {};
  }
  const auto *tree = prepared->implementation_->analysis_layout
                         ? &prepared->implementation_->analysis_layout->tree
                         : &prepared->implementation_->layout.tree;
  if (tree->nodes.empty()) {
    return {};
  }
  return {prepared, tree};
}

PostflopRanges make_uniform_postflop_ranges() {
  PostflopRanges ranges;
  const auto full = RangeWeight::from_basis_points(10'000).value();
  for (auto &range : ranges.players) {
    range.fill(full);
  }
  return ranges;
}

Result<bool, PostflopSolverError> validate_postflop_ranges(const PostflopTreeConfig &config,
                                                           const PostflopRanges &ranges) {
  const auto valid_config = validate_tree_config(config);
  if (!valid_config) {
    return Result<bool, PostflopSolverError>::failure(PostflopSolverError::InvalidConfiguration);
  }
  const auto combos = all_combos();
  std::uint64_t board_mask = 0U;
  for (const auto card : configured_board(config)) {
    board_mask |= card.mask();
  }
  for (std::size_t first = 0; first < combos.size(); ++first) {
    const auto first_mask = combos[first].first.mask() | combos[first].second.mask();
    if ((first_mask & board_mask) != 0U || ranges.players[0][first].basis_points() == 0U) {
      continue;
    }
    for (std::size_t second = 0; second < combos.size(); ++second) {
      const auto second_mask = combos[second].first.mask() | combos[second].second.mask();
      if ((second_mask & board_mask) == 0U && (first_mask & second_mask) == 0U &&
          ranges.players[1][second].basis_points() != 0U) {
        return Result<bool, PostflopSolverError>::success(true);
      }
    }
  }
  return Result<bool, PostflopSolverError>::failure(PostflopSolverError::InvalidConfiguration);
}

Result<PostflopSolveResult, PostflopSolverError>
solve_postflop_exact(const PostflopTreeConfig &config, const PostflopSolveOptions &options,
                     const PostflopCheckpoint *resume_from) {
  return solve_postflop_exact(config, make_uniform_postflop_ranges(), options, resume_from);
}

Result<PostflopSolveResult, PostflopSolverError>
solve_postflop_exact(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                     const PostflopSolveOptions &options, const PostflopCheckpoint *resume_from) {
  const auto solve_started = std::chrono::steady_clock::now();
  auto prepared = prepare_postflop_tree(config, ranges, options.enable_lossless_isomorphism,
                                        options.enable_canonical_public_dag);
  if (!prepared) {
    return Result<PostflopSolveResult, PostflopSolverError>::failure(prepared.error());
  }
  auto result = solve_postflop_exact(*prepared.value(), options, resume_from);
  if (result) {
    const double total_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - solve_started).count();
    result.value().timings.layout_seconds =
        std::max(0.0, total_seconds - result.value().timings.run_solver_seconds);
    result.value().timings.total_seconds = total_seconds;
  }
  return result;
}

Result<PostflopSolveResult, PostflopSolverError>
solve_postflop_exact(PostflopPreparedTree &prepared, const PostflopSolveOptions &options,
                     const PostflopCheckpoint *resume_from) {
  const auto solve_started = std::chrono::steady_clock::now();
  const auto &config = prepared.implementation_->config;
  const auto &ranges = prepared.implementation_->ranges;
  const bool target_driven_without_iteration_limit = options.iterations == 0U;
  if (options.certification_interval == 0U ||
      (target_driven_without_iteration_limit && !options.target_normalized_nash_conv &&
       !options.target_normalized_max_deviation) ||
      (options.target_normalized_nash_conv &&
       (!std::isfinite(*options.target_normalized_nash_conv) ||
        *options.target_normalized_nash_conv < 0.0)) ||
      (options.target_normalized_max_deviation &&
       (!std::isfinite(*options.target_normalized_max_deviation) ||
        *options.target_normalized_max_deviation < 0.0)) ||
      (options.target_normalized_nash_conv && options.target_normalized_max_deviation) ||
      options.memory_backend == MemoryPrototype::StreetDecomposition ||
      (options.memory_backend == MemoryPrototype::OutOfCore &&
       options.state_precision != PostflopStatePrecision::Float64) ||
      ((options.algorithm == PostflopAlgorithm::Dcfr ||
        options.algorithm == PostflopAlgorithm::HsDcfr30) &&
       options.state_precision != PostflopStatePrecision::ScaledUint16RegretStrategy) ||
      (options.state_precision == PostflopStatePrecision::ScaledUint16RegretStrategy &&
       !options.enable_canonical_public_dag)) {
    return Result<PostflopSolveResult, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  if (!validate_postflop_ranges(config, ranges) ||
      !std::isfinite(options.dcfr_positive_regret_exponent) ||
      !std::isfinite(options.dcfr_average_exponent) ||
      options.dcfr_positive_regret_exponent < 0.0 ||
      options.dcfr_average_exponent < 0.0 ||
      options.enable_lossless_isomorphism !=
          prepared.implementation_->lossless_isomorphism_enabled ||
      options.enable_canonical_public_dag !=
          prepared.implementation_->canonical_public_dag_enabled) {
    return Result<PostflopSolveResult, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  struct PreparedLayoutReference {
    DenseLayout *pointer;
    [[nodiscard]] DenseLayout &value() const noexcept { return *pointer; }
  } layout{&prepared.implementation_->layout};
  const auto initialization_started = std::chrono::steady_clock::now();
  // Rank and terminal-index metadata are immutable during traversal. Build
  // them once on the caller thread so parallel workers only read BoardData
  // and never race on the lazy ranks_ready transition.
  std::fprintf(stderr, "solver_phase=prepare_ranks_start boards=%zu\n",
               layout.value().boards.size());
  for (std::uint32_t board_index = 0; board_index < layout.value().boards.size(); ++board_index) {
    if (std::popcount(layout.value().boards[board_index].mask) == 5) {
      const auto prepared_ranks = prepare_ranks(layout.value(), board_index);
      if (!prepared_ranks) {
        return Result<PostflopSolveResult, PostflopSolverError>::failure(prepared_ranks.error());
      }
    }
  }
  std::fprintf(stderr, "solver_phase=prepare_ranks_complete\n");
  if (layout.value().uses_canonical_public_dag) {
    const SuitPermutation identity{};
    std::uint64_t decision_edges = 0U;
    std::uint64_t identity_decision_edges = 0U;
    std::array<std::uint64_t, maximum_action_count + 1U> decision_nodes_by_actions{};
    std::array<std::uint64_t, maximum_action_count + 1U> state_entries_by_actions{};
    for (const auto &node : layout.value().canonical_public_graph.nodes) {
      if (node.kind != PublicNodeKind::Decision) {
        continue;
      }
      const auto action_count = static_cast<std::size_t>(node.decision.action_count);
      if (action_count <= maximum_action_count) {
        ++decision_nodes_by_actions[action_count];
        state_entries_by_actions[action_count] += node.local_action_count;
      }
      const bool river_decision =
          std::popcount(layout.value().boards[node.board_index].mask) == 5U;
      if (river_decision && node.edges.size() != action_count) {
        return Result<PostflopSolveResult, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      for (const auto &edge : node.edges) {
        if (river_decision && edge.outcomes.size() != 1U) {
          return Result<PostflopSolveResult, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        for (const auto &outcome : edge.outcomes) {
          ++decision_edges;
          const bool identity_outcome =
              outcome.physical_to_child_automorphism <
                  layout.value().automorphisms.size() &&
              layout.value()
                      .automorphisms[outcome.physical_to_child_automorphism]
                      .suits == identity;
          if (identity_outcome) {
            ++identity_decision_edges;
          }
          if (river_decision && !identity_outcome) {
            return Result<PostflopSolveResult, PostflopSolverError>::failure(
                PostflopSolverError::InvalidConfiguration);
          }
        }
      }
    }
    std::fprintf(stderr,
                 "solver_profile=canonical_automorphisms decision_edges=%llu "
                 "identity_decision_edges=%llu\n",
                 static_cast<unsigned long long>(decision_edges),
                 static_cast<unsigned long long>(identity_decision_edges));
    for (std::size_t action_count = 1U; action_count <= maximum_action_count;
         ++action_count) {
      if (decision_nodes_by_actions[action_count] != 0U) {
        std::fprintf(stderr,
                     "solver_profile=action_arity actions=%zu nodes=%llu state_entries=%llu\n",
                     action_count,
                     static_cast<unsigned long long>(decision_nodes_by_actions[action_count]),
                     static_cast<unsigned long long>(state_entries_by_actions[action_count]));
      }
    }
  }
  PostflopCheckpoint checkpoint;
  std::unique_ptr<PagedActionFile> mapped;
  ActionBuffers buffers;
  const bool out_of_core = options.memory_backend == MemoryPrototype::OutOfCore;
  if (resume_from != nullptr) {
    if (resume_from->game_fingerprint != layout.value().fingerprint ||
        resume_from->averaging_delay != options.averaging_delay ||
        resume_from->state_precision != options.state_precision ||
        resume_from->algorithm != options.algorithm ||
        resume_from->dcfr_positive_regret_exponent !=
            options.dcfr_positive_regret_exponent ||
        resume_from->dcfr_average_exponent != options.dcfr_average_exponent ||
        (!target_driven_without_iteration_limit &&
         resume_from->completed_iterations > options.iterations) ||
        resume_from->action_count != layout.value().actions ||
        (options.state_precision == PostflopStatePrecision::ScaledUint16RegretStrategy &&
         resume_from->decision_node_count != layout.value().canonical_decision_nodes)) {
      return Result<PostflopSolveResult, PostflopSolverError>::failure(
          PostflopSolverError::CheckpointMismatch);
    }
    checkpoint = *resume_from;
  } else {
    checkpoint.game_fingerprint = layout.value().fingerprint;
    checkpoint.averaging_delay = options.averaging_delay;
    checkpoint.action_count = layout.value().actions;
    checkpoint.decision_node_count =
        options.state_precision == PostflopStatePrecision::ScaledUint16RegretStrategy
            ? layout.value().canonical_decision_nodes
            : 0U;
    checkpoint.state_precision = options.state_precision;
    checkpoint.algorithm = options.algorithm;
    checkpoint.dcfr_positive_regret_exponent = options.dcfr_positive_regret_exponent;
    checkpoint.dcfr_average_exponent = options.dcfr_average_exponent;
  }
  if (out_of_core) {
    std::string backing_file =
        resume_from != nullptr ? checkpoint.external_buffer_file : options.backing_file;
    if (backing_file.empty() ||
        (resume_from != nullptr && checkpoint.external_buffer_file.empty())) {
      return Result<PostflopSolveResult, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    if (resume_from == nullptr) {
      std::error_code absolute_error;
      const auto absolute_path = std::filesystem::absolute(backing_file, absolute_error);
      if (absolute_error) {
        return Result<PostflopSolveResult, PostflopSolverError>::failure(
            PostflopSolverError::IoFailure);
      }
      backing_file = absolute_path.lexically_normal().string();
    }
    mapped = std::make_unique<PagedActionFile>(
        backing_file, static_cast<std::size_t>(layout.value().actions), resume_from == nullptr);
    if (!mapped->valid()) {
      return Result<PostflopSolveResult, PostflopSolverError>::failure(
          PostflopSolverError::IoFailure);
    }
    checkpoint.external_buffer_file = backing_file;
    checkpoint.cumulative_regret.clear();
    checkpoint.cumulative_strategy.clear();
    checkpoint.cumulative_regret_float32.clear();
    checkpoint.cumulative_strategy_float32.clear();
    checkpoint.cumulative_regret_float24.clear();
    checkpoint.cumulative_strategy_float16.clear();
    checkpoint.cumulative_compact_state.clear();
    checkpoint.cumulative_regret_uint16.clear();
    checkpoint.cumulative_strategy_uint16.clear();
    checkpoint.regret_node_scale.clear();
    checkpoint.strategy_node_scale.clear();
    buffers = mapped->buffers();
  } else {
    const bool mixed_state =
        options.state_precision == PostflopStatePrecision::Float24RegretFloat16Strategy;
    const bool action_major_compact_state =
        options.state_precision ==
        PostflopStatePrecision::ActionMajorFloat13RegretFloat11Strategy;
    const bool compact_state =
        options.state_precision == PostflopStatePrecision::Float13RegretFloat11Strategy ||
        action_major_compact_state;
    const bool scaled_uint16_state =
        options.state_precision == PostflopStatePrecision::ScaledUint16RegretStrategy;
    const bool float32_state = options.state_precision == PostflopStatePrecision::Float32;
    const bool checkpoint_size_matches =
        scaled_uint16_state
            ? checkpoint.cumulative_regret_uint16.size() == layout.value().actions &&
                  checkpoint.cumulative_strategy_uint16.size() == layout.value().actions &&
                  checkpoint.regret_node_scale.size() ==
                      layout.value().canonical_decision_nodes &&
                  checkpoint.strategy_node_scale.size() ==
                      layout.value().canonical_decision_nodes &&
                  checkpoint.cumulative_regret.empty() &&
                  checkpoint.cumulative_strategy.empty() &&
                  checkpoint.cumulative_regret_float32.empty() &&
                  checkpoint.cumulative_strategy_float32.empty() &&
                  checkpoint.cumulative_regret_float24.empty() &&
                  checkpoint.cumulative_strategy_float16.empty() &&
                  checkpoint.cumulative_compact_state.empty()
        : compact_state
            ? checkpoint.action_count <= std::numeric_limits<std::size_t>::max() / 3U &&
                  checkpoint.cumulative_compact_state.size() == layout.value().actions * 3U &&
                  checkpoint.cumulative_regret.empty() && checkpoint.cumulative_strategy.empty() &&
                  checkpoint.cumulative_regret_float32.empty() &&
                  checkpoint.cumulative_strategy_float32.empty() &&
                  checkpoint.cumulative_regret_float24.empty() &&
                  checkpoint.cumulative_strategy_float16.empty()
        : mixed_state
            ? checkpoint.action_count <= std::numeric_limits<std::size_t>::max() / 3U &&
                  checkpoint.cumulative_regret_float24.size() == layout.value().actions * 3U &&
                  checkpoint.cumulative_strategy_float16.size() == layout.value().actions &&
                  checkpoint.cumulative_regret.empty() && checkpoint.cumulative_strategy.empty() &&
                  checkpoint.cumulative_regret_float32.empty() &&
                  checkpoint.cumulative_strategy_float32.empty() &&
                  checkpoint.cumulative_compact_state.empty()
        : float32_state
            ? checkpoint.cumulative_regret_float32.size() == layout.value().actions &&
                  checkpoint.cumulative_strategy_float32.size() == layout.value().actions &&
                  checkpoint.cumulative_regret.empty() && checkpoint.cumulative_strategy.empty() &&
                  checkpoint.cumulative_regret_float24.empty() &&
                  checkpoint.cumulative_strategy_float16.empty() &&
                  checkpoint.cumulative_compact_state.empty()
            : checkpoint.cumulative_regret.size() == layout.value().actions &&
                  checkpoint.cumulative_strategy.size() == layout.value().actions &&
                  checkpoint.cumulative_regret_float32.empty() &&
                  checkpoint.cumulative_strategy_float32.empty() &&
                  checkpoint.cumulative_regret_float24.empty() &&
                  checkpoint.cumulative_strategy_float16.empty() &&
                  checkpoint.cumulative_compact_state.empty();
    if (resume_from != nullptr &&
        (!checkpoint_size_matches || !checkpoint.external_buffer_file.empty())) {
      return Result<PostflopSolveResult, PostflopSolverError>::failure(
          PostflopSolverError::CheckpointMismatch);
    }
    if (resume_from == nullptr) {
      if (scaled_uint16_state) {
        checkpoint.cumulative_regret_uint16.resize(
            static_cast<std::size_t>(layout.value().actions), std::uint16_t{0});
        checkpoint.cumulative_strategy_uint16.resize(
            static_cast<std::size_t>(layout.value().actions), std::uint16_t{0});
        checkpoint.regret_node_scale.resize(
            static_cast<std::size_t>(layout.value().canonical_decision_nodes), 0.0F);
        checkpoint.strategy_node_scale.resize(
            static_cast<std::size_t>(layout.value().canonical_decision_nodes), 0.0F);
      } else if (compact_state) {
        if (layout.value().actions > std::numeric_limits<std::size_t>::max() / 3U) {
          return Result<PostflopSolveResult, PostflopSolverError>::failure(
              PostflopSolverError::MemoryFailure);
        }
        std::fprintf(stderr, "solver_phase=compact_state_allocate_start bytes=%llu\n",
                     static_cast<unsigned long long>(layout.value().actions * 3U));
        checkpoint.cumulative_compact_state.resize(
            static_cast<std::size_t>(layout.value().actions) * 3U, std::uint8_t{0});
        std::fprintf(stderr, "solver_phase=compact_state_allocate_complete\n");
      } else if (mixed_state) {
        if (layout.value().actions > std::numeric_limits<std::size_t>::max() / 3U) {
          return Result<PostflopSolveResult, PostflopSolverError>::failure(
              PostflopSolverError::MemoryFailure);
        }
        checkpoint.cumulative_regret_float24.resize(
            static_cast<std::size_t>(layout.value().actions) * 3U, std::uint8_t{0});
        checkpoint.cumulative_strategy_float16.resize(
            static_cast<std::size_t>(layout.value().actions), std::uint16_t{0});
      } else if (float32_state) {
        checkpoint.cumulative_regret_float32.resize(
            static_cast<std::size_t>(layout.value().actions), 0.0F);
        checkpoint.cumulative_strategy_float32.resize(
            static_cast<std::size_t>(layout.value().actions), 0.0F);
      } else {
        checkpoint.cumulative_regret.resize(static_cast<std::size_t>(layout.value().actions), 0.0);
        checkpoint.cumulative_strategy.resize(static_cast<std::size_t>(layout.value().actions),
                                              0.0);
      }
    }
    if (scaled_uint16_state) {
      buffers.count = static_cast<std::size_t>(layout.value().actions);
      buffers.scaled_regret = checkpoint.cumulative_regret_uint16.data();
      buffers.scaled_strategy = checkpoint.cumulative_strategy_uint16.data();
      buffers.regret_node_scale = checkpoint.regret_node_scale.data();
      buffers.strategy_node_scale = checkpoint.strategy_node_scale.data();
      buffers.decision_node_count =
          static_cast<std::size_t>(layout.value().canonical_decision_nodes);
      buffers.signed_scaled_regret =
          options.algorithm == PostflopAlgorithm::Dcfr ||
          options.algorithm == PostflopAlgorithm::HsDcfr30;
    } else if (compact_state) {
      buffers = {nullptr,
                 nullptr,
                 static_cast<std::size_t>(layout.value().actions),
                 nullptr,
                 nullptr,
                 nullptr,
                 nullptr,
                 nullptr,
                 checkpoint.cumulative_compact_state.data()};
      buffers.action_major_compact = action_major_compact_state;
    } else if (mixed_state) {
      buffers = {nullptr,
                 nullptr,
                 static_cast<std::size_t>(layout.value().actions),
                 nullptr,
                 nullptr,
                 nullptr,
                 checkpoint.cumulative_regret_float24.data(),
                 checkpoint.cumulative_strategy_float16.data()};
    } else if (float32_state) {
      buffers = {nullptr,
                 nullptr,
                 checkpoint.cumulative_regret_float32.size(),
                 nullptr,
                 checkpoint.cumulative_regret_float32.data(),
                 checkpoint.cumulative_strategy_float32.data()};
    } else {
      buffers = {checkpoint.cumulative_regret.data(), checkpoint.cumulative_strategy.data(),
                 checkpoint.cumulative_regret.size()};
    }
  }
  PostflopSolveResult result;
  result.public_tree = layout.value().tree.stats;
  result.canonical_public_nodes = layout.value().canonical_public_graph.nodes.size();
  result.information_sets = layout.value().information_sets;
  result.actions = layout.value().actions;
  std::vector<double> deferred_regret_delta;
  // The deferred regret delta (8 B x actions = 667 MB for th7d6s) is only
  // needed when the runner's traversal will actually accumulate into it: the
  // canonical-DAG path and the non-direct-action-bases physical path use it,
  // but the per-player direct-action-bases path applies regrets immediately
  // (make_dense_traversal_runner passes nullptr there), so allocating it for
  // that path would waste 667 MB of peak RSS.
  const bool deferred_delta_needed =
      layout.value().uses_isomorphic_infosets && !layout.value().uses_direct_action_bases;
  if (deferred_delta_needed) {
    deferred_regret_delta.resize(static_cast<std::size_t>(layout.value().actions), 0.0);
  }
  std::unique_ptr<PreparedRootLock> prepared_root_lock;
  if (options.diagnostic_root_lock != nullptr) {
    auto prepared_lock = prepare_root_lock(*options.diagnostic_root_lock, layout.value());
    if (!prepared_lock) {
      return Result<PostflopSolveResult, PostflopSolverError>::failure(prepared_lock.error());
    }
    prepared_root_lock = std::move(prepared_lock.value());
  }
  auto traversal = make_dense_traversal_runner(
      layout.value(), buffers, deferred_delta_needed ? &deferred_regret_delta : nullptr,
      options.parallel_action_depth, prepared_root_lock.get());
  std::fprintf(stderr, "solver_phase=traversal_ready\n");
  result.timings.layout_seconds = 0.0;
  result.timings.initialization_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - initialization_started)
          .count();
  const auto attach_runtime_telemetry =
      [&](PostflopCertification point) {
        point.solver_elapsed_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          initialization_started)
                .count();
        point.traversal_elapsed_seconds = result.timings.traversal_seconds;
        point.certification_elapsed_seconds = result.timings.certification_seconds;
        point.traversed_nodes = traversal->traversed_nodes();
        point.work_counters = traversal->work_counters();
        return point;
      };
  if (!target_driven_without_iteration_limit &&
      checkpoint.completed_iterations == options.iterations) {
    const auto certification_started = std::chrono::steady_clock::now();
    const auto certification = certify(layout.value(), buffers, checkpoint.completed_iterations,
                                       prepared_root_lock.get());
    result.timings.certification_seconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - certification_started)
            .count();
    if (!certification) {
      return Result<PostflopSolveResult, PostflopSolverError>::failure(certification.error());
    }
    auto point = attach_runtime_telemetry(certification.value());
    result.convergence.push_back(point);
    if (options.progress_callback) {
      options.progress_callback(point);
    }
    if (mapped && !mapped->flush()) {
      return Result<PostflopSolveResult, PostflopSolverError>::failure(
          PostflopSolverError::IoFailure);
    }
    if (options.checkpoint_callback &&
        !options.checkpoint_callback(point, checkpoint)) {
      return Result<PostflopSolveResult, PostflopSolverError>::failure(
          PostflopSolverError::IoFailure);
    }
  }
  double dcfr_regret_scale = 1.0;
  if (options.algorithm == PostflopAlgorithm::DcfrPlus) {
    for (std::uint64_t iteration = 1U; iteration <= checkpoint.completed_iterations; ++iteration) {
      const double powered =
          std::pow(static_cast<double>(iteration), options.dcfr_positive_regret_exponent);
      dcfr_regret_scale *= powered / (powered + 1.0);
    }
  }
  // A finite DCFR run may use its final horizon as a harmless common scale on
  // all average-strategy weights. An unbounded run has no final horizon, so it
  // uses a deterministic power-of-two horizon and rescales the accumulated
  // strategy only when that horizon doubles. This preserves the exact t^gamma
  // relative weighting without tying the algorithm to a future stop count.
  std::uint64_t dcfr_strategy_horizon = 0U;
  if (target_driven_without_iteration_limit &&
      (options.algorithm == PostflopAlgorithm::DcfrPlus ||
       options.algorithm == PostflopAlgorithm::Dcfr)) {
    constexpr std::uint64_t minimum_horizon = 256U;
    const std::uint64_t required =
        std::max({minimum_horizon, options.certification_interval,
                  std::max<std::uint64_t>(1U, checkpoint.completed_iterations)});
    dcfr_strategy_horizon = std::bit_ceil(required);
  }
  std::uint64_t iteration = checkpoint.completed_iterations;
  std::optional<double> previous_certification_metric;
  std::uint64_t previous_certification_iteration = 0U;
  std::uint64_t adaptive_certification_iteration = 0U;
  const auto increment_saturated = [](const std::uint64_t value) {
    return value == std::numeric_limits<std::uint64_t>::max() ? value
                                                               : value + 1U;
  };
  const auto first_periodic_base =
      std::max(increment_saturated(checkpoint.completed_iterations),
               increment_saturated(options.averaging_delay));
  const auto periodic_remainder =
      first_periodic_base % options.certification_interval;
  const auto periodic_round_up =
      periodic_remainder == 0U ? 0U
                               : options.certification_interval - periodic_remainder;
  std::uint64_t next_periodic_certification_iteration =
      first_periodic_base <=
              std::numeric_limits<std::uint64_t>::max() - periodic_round_up
          ? first_periodic_base + periodic_round_up
          : std::numeric_limits<std::uint64_t>::max();
#pragma warning(push)
#pragma warning(disable : 4996)
  const bool diagnostic_simultaneous =
      std::getenv("GTOSD_DIAGNOSTIC_SIMULTANEOUS") != nullptr;
#pragma warning(pop)
  while (target_driven_without_iteration_limit || iteration < options.iterations) {
    if (iteration == std::numeric_limits<std::uint64_t>::max()) {
      return Result<PostflopSolveResult, PostflopSolverError>::failure(
          PostflopSolverError::NumericalFailure);
    }
    ++iteration;
    const auto traversal_started = std::chrono::steady_clock::now();
    const double effective_iteration =
        iteration > options.averaging_delay
            ? static_cast<double>(iteration - options.averaging_delay)
            : 0.0;
    double strategy_weight = effective_iteration;
    double regret_update_weight = 1.0;
    double positive_regret_discount = 1.0;
    double negative_regret_discount = 1.0;
    if (options.algorithm == PostflopAlgorithm::DcfrPlus) {
      if (target_driven_without_iteration_limit && iteration > dcfr_strategy_horizon) {
        if (dcfr_strategy_horizon > std::numeric_limits<std::uint64_t>::max() / 2U) {
          return Result<PostflopSolveResult, PostflopSolverError>::failure(
              PostflopSolverError::NumericalFailure);
        }
        const auto next_horizon = dcfr_strategy_horizon * 2U;
        const double rescale =
            std::pow(static_cast<double>(dcfr_strategy_horizon) /
                         static_cast<double>(next_horizon),
                     std::max(0.0, options.dcfr_average_exponent - 1.0));
        buffers.scale_strategy(rescale);
        dcfr_strategy_horizon = next_horizon;
      }
      const double powered =
          std::pow(static_cast<double>(iteration), options.dcfr_positive_regret_exponent);
      dcfr_regret_scale *= powered / (powered + 1.0);
      regret_update_weight = 1.0 / dcfr_regret_scale;
      const auto normalization_horizon = target_driven_without_iteration_limit
                                             ? dcfr_strategy_horizon
                                             : options.iterations;
      strategy_weight = effective_iteration == 0.0
                            ? 0.0
                            : std::pow(effective_iteration, options.dcfr_average_exponent) /
                                  std::pow(static_cast<double>(normalization_horizon),
                                          std::max(0.0,
                                                    options.dcfr_average_exponent - 1.0));
    } else if (options.algorithm == PostflopAlgorithm::Dcfr) {
      const double alpha_iteration =
          static_cast<double>(iteration > 0U ? iteration - 1U : 0U);
      const double powered =
          std::pow(alpha_iteration, options.dcfr_positive_regret_exponent);
      positive_regret_discount = powered / (powered + 1.0);
      negative_regret_discount = 0.5;
      strategy_weight =
          effective_iteration == 0.0
              ? 0.0
              : std::pow(effective_iteration,
                         options.dcfr_average_exponent);
    } else if (options.algorithm == PostflopAlgorithm::HsDcfr30) {
      const auto schedule = hs_dcfr30_schedule(iteration);
      const double discount_iteration = static_cast<double>(iteration - 1U);
      if (iteration == 1U) {
        // The previous regrets and average strategy are exactly zero, so the
        // limiting value chosen for the t=1 discounts is immaterial.
        positive_regret_discount = 0.0;
        negative_regret_discount = 0.0;
      } else {
        const double positive_power = std::pow(discount_iteration, schedule.alpha);
        const double negative_power = std::pow(discount_iteration, schedule.beta);
        positive_regret_discount = positive_power / (positive_power + 1.0);
        negative_regret_discount = negative_power / (negative_power + 1.0);
      }
      // Equation (4) of HS-DCFR discounts the previously accumulated average
      // before adding the current reach-weighted strategy. In the scaled
      // backend this touches only one float scale per decision node; the 16-bit
      // action payload remains unchanged and incurs no full-state pass.
      const double hs_average_discount =
          iteration == 1U
              ? 0.0
              : std::pow(discount_iteration / static_cast<double>(iteration),
                         schedule.gamma);
      buffers.scale_strategy(hs_average_discount);
      strategy_weight = effective_iteration == 0.0 ? 0.0 : 1.0;
    }
    if (!std::isfinite(strategy_weight) || !std::isfinite(regret_update_weight) ||
        !std::isfinite(positive_regret_discount) ||
        !std::isfinite(negative_regret_discount)) {
      return Result<PostflopSolveResult, PostflopSolverError>::failure(
          PostflopSolverError::NumericalFailure);
    }
    if (diagnostic_simultaneous) {
      const auto traversed =
          traversal->cfr_simultaneous(strategy_weight, regret_update_weight,
                                      positive_regret_discount,
                                      negative_regret_discount);
      if (!traversed) {
        return Result<PostflopSolveResult, PostflopSolverError>::failure(traversed.error());
      }
      const auto regret_application_started = std::chrono::steady_clock::now();
      const auto applied = traversal->apply_deferred_regrets();
      result.timings.regret_application_seconds +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        regret_application_started)
              .count();
      if (!applied) {
        return Result<PostflopSolveResult, PostflopSolverError>::failure(applied.error());
      }
    } else {
      for (std::uint8_t player = 0; player < 2U; ++player) {
        const auto traversed =
              traversal->cfr(player, strategy_weight, regret_update_weight,
                             positive_regret_discount,
                             negative_regret_discount);
        if (!traversed) {
          return Result<PostflopSolveResult, PostflopSolverError>::failure(
              traversed.error());
        }
        const auto regret_application_started = std::chrono::steady_clock::now();
        const auto applied = traversal->apply_deferred_regrets();
        result.timings.regret_application_seconds +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          regret_application_started)
                .count();
        if (!applied) {
          return Result<PostflopSolveResult, PostflopSolverError>::failure(
              applied.error());
        }
      }
    }
    result.timings.traversal_seconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - traversal_started).count();
    checkpoint.completed_iterations = iteration;
    if (iteration <= 5U || iteration % 20U == 0U) {
      std::fprintf(stderr,
                   "solver_phase=iteration_complete iteration=%llu traversal_seconds=%.6f "
                   "traversed_nodes=%llu\n",
                   static_cast<unsigned long long>(iteration),
                   std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                 traversal_started)
                       .count(),
                   static_cast<unsigned long long>(traversal->traversed_nodes()));
    }
    const auto control = options.control_callback ? options.control_callback(iteration)
                                                  : PostflopControlCommand::Continue;
    const bool stopping = control != PostflopControlCommand::Continue;
    // Before the averaging delay expires, cumulative_strategy contains no
    // samples and therefore cannot satisfy a convergence claim about the
    // average strategy. Skip only periodic certifications in that interval;
    // finite-run finalization and explicit pause/cancel still force one so
    // callers always receive an observable terminal checkpoint.
    const bool periodic_certification_due =
        iteration > options.averaging_delay &&
        iteration == next_periodic_certification_iteration;
    const bool adaptive_certification_due =
        adaptive_certification_iteration != 0U &&
        iteration == adaptive_certification_iteration;
    bool converged = false;
    if (periodic_certification_due || adaptive_certification_due ||
        (!target_driven_without_iteration_limit && iteration == options.iterations) ||
        stopping) {
      const auto certification_started = std::chrono::steady_clock::now();
      const auto certification = certify(layout.value(), buffers, checkpoint.completed_iterations,
                                       prepared_root_lock.get());
      result.timings.certification_seconds +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() - certification_started)
              .count();
      if (!certification) {
        return Result<PostflopSolveResult, PostflopSolverError>::failure(certification.error());
      }
      auto point = attach_runtime_telemetry(certification.value());
      result.convergence.push_back(point);
      std::optional<double> certification_metric;
      std::optional<double> certification_target;
      if (options.target_normalized_nash_conv) {
        certification_metric = certification.value().normalized_nash_conv;
        certification_target = *options.target_normalized_nash_conv;
        converged = options.strict_target
                        ? certification.value().normalized_nash_conv <
                              *options.target_normalized_nash_conv
                        : certification.value().normalized_nash_conv <=
                              *options.target_normalized_nash_conv;
      } else if (options.target_normalized_max_deviation) {
        const auto deviation =
            normalized_max_deviation_gain(certification.value(), config.initial_pot);
        if (!deviation) {
          return Result<PostflopSolveResult, PostflopSolverError>::failure(deviation.error());
        }
        certification_metric = deviation.value();
        certification_target = *options.target_normalized_max_deviation;
        converged = options.strict_target
                        ? deviation.value() < *options.target_normalized_max_deviation
                        : deviation.value() <= *options.target_normalized_max_deviation;
      }
      adaptive_certification_iteration = 0U;
      if (periodic_certification_due) {
        std::uint64_t periodic_step = options.certification_interval;
        // In an unbounded target-driven solve, one intermediate exact best
        // response can be omitted after two measurements both remain well
        // outside the target neighborhood. This never changes CFR state; it
        // only avoids an expensive observation that cannot stop the run.
        if (!converged && target_driven_without_iteration_limit &&
            certification_metric && certification_target &&
            previous_certification_metric &&
            *certification_metric > *certification_target * 1.25 &&
            *previous_certification_metric > *certification_target * 1.25 &&
            periodic_step <= std::numeric_limits<std::uint64_t>::max() / 2U) {
          periodic_step *= 2U;
        }
        next_periodic_certification_iteration =
            iteration <= std::numeric_limits<std::uint64_t>::max() - periodic_step
                ? iteration + periodic_step
                : std::numeric_limits<std::uint64_t>::max();
      }
      if (!converged && target_driven_without_iteration_limit && certification_metric &&
          certification_target && *certification_metric > 0.0 &&
          *certification_target > 0.0 &&
          *certification_metric <= *certification_target * 1.25 &&
          previous_certification_metric && previous_certification_iteration < iteration &&
          *previous_certification_metric > *certification_metric) {
        const double exponent =
            std::log(*previous_certification_metric / *certification_metric) /
            std::log(static_cast<double>(iteration) /
                     static_cast<double>(previous_certification_iteration));
        if (std::isfinite(exponent) && exponent > 0.0) {
          const double predicted = static_cast<double>(iteration) *
              std::pow(*certification_metric / *certification_target, 1.0 / exponent);
          const auto candidate = static_cast<std::uint64_t>(std::ceil(predicted));
          const auto next_periodic = next_periodic_certification_iteration;
          if (candidate > iteration && candidate < next_periodic) {
            adaptive_certification_iteration = candidate;
          }
        }
      }
      if (certification_metric) {
        previous_certification_metric = *certification_metric;
        previous_certification_iteration = iteration;
      }
      if (options.progress_callback) {
        options.progress_callback(point);
      }
      if (mapped && !mapped->flush()) {
        return Result<PostflopSolveResult, PostflopSolverError>::failure(
            PostflopSolverError::IoFailure);
      }
      if (options.checkpoint_callback &&
          !options.checkpoint_callback(point, checkpoint)) {
        return Result<PostflopSolveResult, PostflopSolverError>::failure(
            PostflopSolverError::IoFailure);
      }
    }
    if (converged) {
      result.stop_reason = PostflopStopReason::Converged;
      break;
    }
    if (stopping) {
      result.stop_reason = control == PostflopControlCommand::Pause ? PostflopStopReason::Paused
                                                                    : PostflopStopReason::Cancelled;
      break;
    }
  }
  const auto finalization_started = std::chrono::steady_clock::now();
  if (out_of_core) {
    for (std::size_t index = 0; index < buffers.count; ++index) {
      if (!std::isfinite(buffers.regret_at(index)) || !std::isfinite(buffers.strategy_at(index))) {
        return Result<PostflopSolveResult, PostflopSolverError>::failure(
            PostflopSolverError::NumericalFailure);
      }
    }
  } else if ((!checkpoint.cumulative_regret.empty() &&
              (!finite_vector(checkpoint.cumulative_regret) ||
               !finite_vector(checkpoint.cumulative_strategy))) ||
             (!checkpoint.cumulative_regret_float32.empty() &&
              (!finite_vector(checkpoint.cumulative_regret_float32) ||
               !finite_vector(checkpoint.cumulative_strategy_float32))) ||
             (!checkpoint.cumulative_regret_float24.empty() &&
              (!std::all_of(checkpoint.cumulative_strategy_float16.begin(),
                            checkpoint.cumulative_strategy_float16.end(),
                            [](const std::uint16_t value) {
                              return std::isfinite(decode_float16(value));
                            }) ||
               !finite_float24_vector(checkpoint.cumulative_regret_float24))) ||
             (!checkpoint.cumulative_compact_state.empty() &&
              !finite_compact_vector(checkpoint.cumulative_compact_state)) ||
             (!checkpoint.regret_node_scale.empty() &&
              (!finite_vector(checkpoint.regret_node_scale) ||
               !finite_vector(checkpoint.strategy_node_scale) ||
               std::ranges::any_of(checkpoint.regret_node_scale,
                                   [](const float value) { return value < 0.0F; }) ||
                 std::ranges::any_of(checkpoint.strategy_node_scale,
                                     [](const float value) { return value < 0.0F; })))) {
    return Result<PostflopSolveResult, PostflopSolverError>::failure(
        PostflopSolverError::NumericalFailure);
  }
  result.traversed_nodes = traversal->traversed_nodes();
  result.work_counters = traversal->work_counters();
  const auto normalization_error = measure_canonical_normalization_error(layout.value(), buffers);
  if (!normalization_error) {
    return Result<PostflopSolveResult, PostflopSolverError>::failure(normalization_error.error());
  }
  result.maximum_normalization_error =
      std::max(traversal->maximum_normalization_error(), normalization_error.value());
  result.checkpoint = std::move(checkpoint);
  result.timings.finalization_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - finalization_started)
          .count();
  result.timings.run_solver_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - initialization_started)
          .count();
  result.timings.total_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - solve_started).count();
  return Result<PostflopSolveResult, PostflopSolverError>::success(std::move(result));
}

Result<double, PostflopSolverError>
normalized_max_deviation_gain(const PostflopCertification &certification, const Money initial_pot) {
  const double initial_pot_antes = static_cast<double>(initial_pot.units()) / units_per_ante;
  if (!(initial_pot_antes > 0.0) || !std::isfinite(initial_pot_antes)) {
    return Result<double, PostflopSolverError>::failure(PostflopSolverError::InvalidConfiguration);
  }
  double maximum_gain = 0.0;
  for (std::size_t player = 0; player < certification.profile_value_antes.size(); ++player) {
    const double gain =
        certification.best_response_value_antes[player] - certification.profile_value_antes[player];
    if (!std::isfinite(gain) || gain < -1.0e-10) {
      return Result<double, PostflopSolverError>::failure(PostflopSolverError::NumericalFailure);
    }
    maximum_gain = std::max(maximum_gain, std::max(0.0, gain));
  }
  const double normalized = maximum_gain / initial_pot_antes;
  return std::isfinite(normalized)
             ? Result<double, PostflopSolverError>::success(normalized)
             : Result<double, PostflopSolverError>::failure(PostflopSolverError::NumericalFailure);
}

Result<PostflopCertification, PostflopSolverError>
certify_postflop_checkpoint(const PostflopTreeConfig &config,
                            const PostflopCheckpoint &checkpoint) {
  return certify_postflop_checkpoint(config, make_uniform_postflop_ranges(), checkpoint);
}

Result<PostflopCertification, PostflopSolverError>
certify_postflop_checkpoint(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                            const PostflopCheckpoint &checkpoint) {
  auto layout = build_layout(config, ranges, true, true);
  if (!layout) {
    return Result<PostflopCertification, PostflopSolverError>::failure(layout.error());
  }
  if (checkpoint.game_fingerprint != layout.value().fingerprint ||
      checkpoint.action_count != layout.value().actions) {
    return Result<PostflopCertification, PostflopSolverError>::failure(
        PostflopSolverError::CheckpointMismatch);
  }
  if (!checkpoint.external_buffer_file.empty()) {
    PagedActionFile mapped(checkpoint.external_buffer_file,
                           static_cast<std::size_t>(checkpoint.action_count), false);
    if (!mapped.valid()) {
      return Result<PostflopCertification, PostflopSolverError>::failure(
          PostflopSolverError::IoFailure);
    }
    return certify(layout.value(), mapped.buffers(), checkpoint.completed_iterations, nullptr);
  }
  const auto buffers = in_memory_checkpoint_buffers(checkpoint);
  if (!buffers) {
    return Result<PostflopCertification, PostflopSolverError>::failure(buffers.error());
  }
  return certify(layout.value(), buffers.value(), checkpoint.completed_iterations, nullptr);
}

Result<PostflopStrategyQuery, PostflopSolverError>
query_postflop_strategy(const PostflopTreeConfig &config, const PostflopCheckpoint &checkpoint,
                        const NodeId public_node, const ComboId combo) {
  return query_postflop_strategy(config, make_uniform_postflop_ranges(), checkpoint, public_node,
                                 combo);
}

Result<PostflopStrategyQuery, PostflopSolverError>
query_postflop_strategy(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                        const PostflopCheckpoint &checkpoint, const NodeId public_node,
                        const ComboId combo) {
  auto layout = build_layout(config, ranges, true, true, true);
  if (!layout || public_node >= layout.value().tree.nodes.size() || combo >= combo_count) {
    return Result<PostflopStrategyQuery, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  const auto &decision = layout.value().decisions[static_cast<std::size_t>(public_node)];
  if (!decision.present || checkpoint.game_fingerprint != layout.value().fingerprint ||
      checkpoint.action_count != layout.value().actions) {
    return Result<PostflopStrategyQuery, PostflopSolverError>::failure(
        PostflopSolverError::CheckpointMismatch);
  }
  std::unique_ptr<PagedActionFile> mapped;
  ActionBuffers query_buffers;
  if (!checkpoint.external_buffer_file.empty()) {
    mapped = std::make_unique<PagedActionFile>(
        checkpoint.external_buffer_file, static_cast<std::size_t>(checkpoint.action_count), false);
    if (!mapped->valid()) {
      return Result<PostflopStrategyQuery, PostflopSolverError>::failure(
          PostflopSolverError::IoFailure);
    }
    query_buffers = mapped->buffers();
  } else {
    const auto in_memory = in_memory_checkpoint_buffers(checkpoint);
    if (!in_memory) {
      return Result<PostflopStrategyQuery, PostflopSolverError>::failure(in_memory.error());
    }
    query_buffers = in_memory.value();
  }
  return query_strategy_from_layout(layout.value(), query_buffers, public_node, combo);
}

Result<std::vector<PostflopStrategyQuery>, PostflopSolverError>
query_postflop_strategies(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                          const PostflopCheckpoint &checkpoint, const NodeId public_node) {
  auto layout = build_layout(config, ranges, true, true, true);
  if (!layout || public_node >= layout.value().tree.nodes.size()) {
    return Result<std::vector<PostflopStrategyQuery>, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  const auto &decision = layout.value().decisions[static_cast<std::size_t>(public_node)];
  if (!decision.present || checkpoint.game_fingerprint != layout.value().fingerprint ||
      checkpoint.action_count != layout.value().actions) {
    return Result<std::vector<PostflopStrategyQuery>, PostflopSolverError>::failure(
        PostflopSolverError::CheckpointMismatch);
  }
  std::unique_ptr<PagedActionFile> mapped;
  ActionBuffers query_buffers;
  if (!checkpoint.external_buffer_file.empty()) {
    mapped = std::make_unique<PagedActionFile>(
        checkpoint.external_buffer_file, static_cast<std::size_t>(checkpoint.action_count), false);
    if (!mapped->valid()) {
      return Result<std::vector<PostflopStrategyQuery>, PostflopSolverError>::failure(
          PostflopSolverError::IoFailure);
    }
    query_buffers = mapped->buffers();
  } else {
    const auto in_memory = in_memory_checkpoint_buffers(checkpoint);
    if (!in_memory) {
      return Result<std::vector<PostflopStrategyQuery>, PostflopSolverError>::failure(
          in_memory.error());
    }
    query_buffers = in_memory.value();
  }
  std::vector<PostflopStrategyQuery> result;
  const auto &board = layout.value().boards[decision.board_index];
  result.reserve(board.player_combos[decision.player].size());
  for (const ComboId combo : board.player_combos[decision.player]) {
    auto query = query_strategy_from_layout(layout.value(), query_buffers, public_node, combo);
    if (!query) {
      return Result<std::vector<PostflopStrategyQuery>, PostflopSolverError>::failure(
          query.error());
    }
    result.push_back(std::move(query.value()));
  }
  return Result<std::vector<PostflopStrategyQuery>, PostflopSolverError>::success(
      std::move(result));
}

Result<PostflopLayoutEstimate, PostflopSolverError>
estimate_postflop_layout(const PostflopTreeConfig &config, const PostflopRanges &ranges) {
  if (!validate_postflop_ranges(config, ranges)) {
    return Result<PostflopLayoutEstimate, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  auto layout = build_layout(config, ranges, true, true);
  if (!layout) {
    return Result<PostflopLayoutEstimate, PostflopSolverError>::failure(layout.error());
  }
  PostflopLayoutEstimate estimate;
  estimate.physical_public_tree = layout.value().tree.stats;
  estimate.canonical_public_nodes = layout.value().canonical_public_graph.nodes.size();
  estimate.information_sets = layout.value().information_sets;
  estimate.actions = layout.value().actions;
  if (estimate.actions > std::numeric_limits<std::uint64_t>::max() / sizeof(double)) {
    return Result<PostflopLayoutEstimate, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  estimate.regret_bytes = estimate.actions * sizeof(double);
  estimate.strategy_bytes = estimate.actions * sizeof(double);
  return Result<PostflopLayoutEstimate, PostflopSolverError>::success(estimate);
}

static Result<PostflopNodeAnalysis, PostflopSolverError>
analyze_postflop_node_with_layout(DenseLayout &dense, const PostflopCheckpoint &checkpoint,
                                  const NodeId public_node) {
  const bool direct_canonical_root =
      dense.tree.nodes.empty() && dense.uses_canonical_public_dag &&
      public_node == dense.canonical_public_graph.root &&
      public_node < dense.canonical_public_graph.nodes.size();
  if (!direct_canonical_root && public_node >= dense.tree.nodes.size()) {
    return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  const auto *canonical_target =
      direct_canonical_root
          ? &dense.canonical_public_graph.nodes[static_cast<std::size_t>(public_node)]
          : nullptr;
  const auto *physical_target =
      direct_canonical_root ? nullptr
                            : &dense.tree.nodes[static_cast<std::size_t>(public_node)];
  const auto target_kind =
      canonical_target != nullptr ? canonical_target->kind : physical_target->kind;
  const auto target_player_to_act =
      canonical_target != nullptr ? canonical_target->decision.player
                                  : physical_target->state.player_to_act;
  if (target_kind != PublicNodeKind::Decision || checkpoint.game_fingerprint != dense.fingerprint ||
      checkpoint.action_count != dense.actions) {
    return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
        PostflopSolverError::CheckpointMismatch);
  }

  // The production traversal requires immutable showdown-rank metadata. Solve
  // and certification prepare it before starting their worker pools; node
  // analysis owns an independent layout and must establish the same invariant
  // before its policy traversals reach river terminals.
  for (std::uint32_t board_index = 0; board_index < dense.boards.size(); ++board_index) {
    if (std::popcount(dense.boards[board_index].mask) == 5) {
      const auto prepared = prepare_ranks(dense, board_index);
      if (!prepared) {
        return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(prepared.error());
      }
    }
  }

  std::unique_ptr<PagedActionFile> mapped;
  ActionBuffers buffers;
  if (!checkpoint.external_buffer_file.empty()) {
    mapped = std::make_unique<PagedActionFile>(
        checkpoint.external_buffer_file, static_cast<std::size_t>(checkpoint.action_count), false);
    if (!mapped->valid()) {
      return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
          PostflopSolverError::IoFailure);
    }
    buffers = mapped->buffers();
  } else {
    const auto in_memory = in_memory_checkpoint_buffers(checkpoint);
    if (!in_memory) {
      return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(in_memory.error());
    }
    buffers = in_memory.value();
  }

  // The direct canonical root must use the same capacity-dispatched policy
  // evaluator as certification. The generic combo_count traversal below is
  // retained for arbitrary physical nodes, but it is not interchangeable
  // with the compact active-combo kernels for packed four/five-action blocks.
  // Reusing certification here also makes the public root-analysis API and
  // the convergence gate report one authoritative profile EV.
  std::optional<std::array<double, 2>> root_profile_values;
  if (direct_canonical_root) {
    const auto profiled = profile_root_values(dense, buffers);
    if (!profiled) {
      return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(profiled.error());
    }
    root_profile_values = profiled.value();
  }

  struct ParentStep {
    NodeId parent{std::numeric_limits<NodeId>::max()};
    std::uint32_t edge{0};
  };
  std::vector<ParentStep> parents;
  std::vector<ParentStep> path;
  if (!direct_canonical_root) {
    parents.resize(dense.tree.nodes.size());
    for (const auto &node : dense.tree.nodes) {
      for (std::size_t edge = 0; edge < node.edges.size(); ++edge) {
        parents[static_cast<std::size_t>(node.edges[edge].child)] = {
            node.id, static_cast<std::uint32_t>(edge)};
      }
    }
    for (NodeId cursor = public_node; cursor != dense.tree.root;) {
      const auto step = parents[static_cast<std::size_t>(cursor)];
      if (step.parent == std::numeric_limits<NodeId>::max()) {
        return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      path.push_back(step);
      cursor = step.parent;
    }
    std::ranges::reverse(path);
  }

  auto reach = dense.initial_reach;
  for (const auto step : path) {
    const auto &parent = dense.tree.nodes[static_cast<std::size_t>(step.parent)];
    const auto &edge = parent.edges[step.edge];
    if (edge.kind == PublicEdgeKind::ChanceCard) {
      const auto card_mask = edge.chance_card.mask();
      for (std::size_t combo = 0; combo < combo_count; ++combo) {
        if ((dense.combo_masks[combo] & card_mask) != 0U) {
          reach[0][combo] = 0.0;
          reach[1][combo] = 0.0;
        }
      }
      continue;
    }
    const auto &decision = dense.decisions[static_cast<std::size_t>(parent.id)];
    const auto &board = dense.boards[decision.board_index];
    // Only the acting player's reach is conditioned by an action.  The union
    // also contains opponent-only combos, which have no strategy entry at
    // this decision under asymmetric ranges.
    for (const ComboId combo : board.player_combos[decision.player]) {
      const auto query = query_strategy_from_layout(dense, buffers, parent.id, combo);
      if (!query || step.edge >= query.value().probabilities.size()) {
        std::fprintf(stderr,
                     "node_analysis_failure phase=reach parent=%llu edge=%u combo=%u "
                     "error=%s\n",
                     static_cast<unsigned long long>(parent.id), step.edge,
                     static_cast<unsigned int>(combo),
                     query ? "edge_out_of_range"
                           : postflop_solver_error_name(query.error()));
        return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      reach[decision.player][combo] *= query.value().probabilities[step.edge];
    }
  }

  const auto actor = static_cast<std::size_t>(target_player_to_act);
  const auto opponent = 1U - actor;
  const auto &decision = canonical_target != nullptr
                             ? canonical_target->decision
                             : dense.decisions[static_cast<std::size_t>(public_node)];
  const auto &board = dense.boards[decision.board_index];
  const auto board_cards = cards_from_mask(board.mask);
  if (board_cards.size() < 3U || board_cards.size() > 5U) {
    return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }

  const auto evaluate_current =
      [&board_cards](const Combo &combo) -> Result<HandValue, PostflopSolverError> {
    std::vector<CardId> cards{combo.first, combo.second};
    cards.insert(cards.end(), board_cards.begin(), board_cards.end());
    if (cards.size() == 5U) {
      std::array<CardId, 5> five{};
      std::ranges::copy(cards, five.begin());
      const auto value = evaluate_five(five);
      return value ? Result<HandValue, PostflopSolverError>::success(value.value())
                   : Result<HandValue, PostflopSolverError>::failure(
                         PostflopSolverError::EquityFailure);
    }
    HandValue best{};
    bool initialized = false;
    const auto choose_five = [&](const auto &self, const std::size_t start,
                                 std::array<CardId, 5> &five,
                                 const std::size_t used) -> Result<bool, PostflopSolverError> {
      if (used == five.size()) {
        const auto value = evaluate_five(five);
        if (!value) {
          return Result<bool, PostflopSolverError>::failure(PostflopSolverError::EquityFailure);
        }
        if (!initialized || value.value() > best) {
          best = value.value();
          initialized = true;
        }
        return Result<bool, PostflopSolverError>::success(true);
      }
      for (std::size_t index = start; index <= cards.size() - (five.size() - used); ++index) {
        five[used] = cards[index];
        const auto result = self(self, index + 1U, five, used + 1U);
        if (!result) {
          return result;
        }
      }
      return Result<bool, PostflopSolverError>::success(true);
    };
    std::array<CardId, 5> five{};
    const auto selected = choose_five(choose_five, 0U, five, 0U);
    return selected && initialized ? Result<HandValue, PostflopSolverError>::success(best)
                                   : Result<HandValue, PostflopSolverError>::failure(
                                         PostflopSolverError::EquityFailure);
  };

  std::array<double, combo_count> equity_won{};
  std::array<double, combo_count> equity_total{};
  std::vector<CardId> public_candidates;
  for (std::uint8_t card = 0; card < 36U; ++card) {
    const auto candidate = CardId::from_index(card).value();
    if ((candidate.mask() & board.mask) == 0U) {
      public_candidates.push_back(candidate);
    }
  }
  const auto missing = 5U - board_cards.size();
  const auto runout_count = missing == 0U ? 1U : public_candidates.size();
  for (std::size_t first = 0; first < runout_count; ++first) {
    const auto second_begin = missing == 2U ? first + 1U : first;
    const auto second_end = missing == 2U ? public_candidates.size() : second_begin + 1U;
    for (std::size_t second = second_begin; second < second_end; ++second) {
      std::uint64_t runout_mask = 0U;
      if (missing >= 1U) {
        runout_mask |= public_candidates[first].mask();
      }
      if (missing == 2U) {
        runout_mask |= public_candidates[second].mask();
      }
      std::array<HandValue, combo_count> values{};
      std::array<bool, combo_count> valid{};
      for (const ComboId combo_id : board.legal_combos) {
        if ((dense.combo_masks[combo_id] & runout_mask) != 0U ||
            (!(reach[actor][combo_id] > 0.0) && !(reach[opponent][combo_id] > 0.0))) {
          continue;
        }
        const auto &combo = dense.combos[combo_id];
        std::array<CardId, 7> cards{combo.first,    combo.second, board_cards[0], board_cards[1],
                                    board_cards[2], CardId{},     CardId{}};
        cards[5] = board_cards.size() >= 4U ? board_cards[3] : public_candidates[first];
        cards[6] = board_cards.size() == 5U   ? board_cards[4]
                   : board_cards.size() == 4U ? public_candidates[first]
                                              : public_candidates[second];
        const auto value = evaluate_seven(cards);
        if (!value) {
          return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
              PostflopSolverError::EquityFailure);
        }
        values[combo_id] = value.value();
        valid[combo_id] = true;
      }
      for (const ComboId hero_id : board.legal_combos) {
        if (!valid[hero_id] || !(reach[actor][hero_id] > 0.0)) {
          continue;
        }
        for (const ComboId villain_id : board.legal_combos) {
          const double villain_weight = reach[opponent][villain_id];
          if (!valid[villain_id] || !(villain_weight > 0.0) ||
              (dense.combo_masks[hero_id] & dense.combo_masks[villain_id]) != 0U) {
            continue;
          }
          equity_total[hero_id] += villain_weight;
          if (values[hero_id] > values[villain_id]) {
            equity_won[hero_id] += villain_weight;
          } else if (values[hero_id] == values[villain_id]) {
            equity_won[hero_id] += villain_weight * 0.5;
          }
        }
      }
    }
  }

  PostflopNodeAnalysis analysis;
  analysis.public_node = public_node;
  analysis.player_to_act = static_cast<std::uint8_t>(target_player_to_act);
  double compatible_pair_mass = 0.0;
  for (const ComboId first : board.legal_combos) {
    if (!(reach[0][first] > 0.0)) {
      continue;
    }
    for (const ComboId second : board.legal_combos) {
      if ((dense.combo_masks[first] & dense.combo_masks[second]) == 0U) {
        compatible_pair_mass += reach[0][first] * reach[1][second];
      }
    }
  }
  const double public_reach_probability = compatible_pair_mass / dense.initial_normalization;
  if (!(public_reach_probability > 0.0) || !std::isfinite(public_reach_probability)) {
    return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
        PostflopSolverError::NumericalFailure);
  }
  for (std::uint8_t player = 0; player < 2U; ++player) {
    if (root_profile_values) {
      analysis.profile_value_antes[player] =
          (*root_profile_values)[player];
    } else {
      DenseTraversal<combo_count> traversal(dense, buffers);
      const auto values = traversal.policy_from_physical_node(public_node, player,
                                                              {&reach[0], &reach[1]});
      if (!values) {
        std::fprintf(stderr,
                     "node_analysis_failure phase=policy node=%llu player=%u error=%s\n",
                     static_cast<unsigned long long>(public_node),
                     static_cast<unsigned int>(player),
                     postflop_solver_error_name(values.error()));
        return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(values.error());
      }
      double weighted_value = 0.0;
      for (const ComboId combo : board.legal_combos) {
        weighted_value += values.value()[combo] * reach[player][combo];
      }
      analysis.profile_value_antes[player] = weighted_value / public_reach_probability;
    }
    analysis.gto_plus_ev_antes[player] =
        analysis.profile_value_antes[player] +
        (canonical_target != nullptr
             ? dense.initial_pot_contribution_antes[player]
             : static_cast<double>(physical_target->state.initial_pot_contributions[player].units()) /
                   units_per_ante);
    if (!std::isfinite(analysis.gto_plus_ev_antes[player])) {
      return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
          PostflopSolverError::NumericalFailure);
    }
  }
  if (canonical_target != nullptr) {
    for (const auto &edge : canonical_target->edges) {
      analysis.actions.push_back(edge.action);
    }
  } else {
    for (const auto &edge : physical_target->edges) {
      analysis.actions.push_back(edge.action);
    }
  }
  analysis.action_frequencies.assign(analysis.actions.size(), 0.0);
  double aggregate_weight = 0.0;
  for (const ComboId hero_id : board.legal_combos) {
    if (!(reach[actor][hero_id] > 0.0)) {
      continue;
    }
    const auto &hero = dense.combos[hero_id];
    const auto hero_mask = dense.combo_masks[hero_id];
    double compatible_opponent_weight = 0.0;
    for (const ComboId villain_id : board.legal_combos) {
      if ((hero_mask & dense.combo_masks[villain_id]) == 0U) {
        compatible_opponent_weight += reach[opponent][villain_id];
      }
    }
    const double combo_weight = reach[actor][hero_id] * compatible_opponent_weight;
    if (!(combo_weight > 0.0)) {
      continue;
    }
    const auto query = query_strategy_from_layout(dense, buffers, public_node, hero_id);
    const auto current_value = evaluate_current(hero);
    if (!query || !current_value) {
      return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
          PostflopSolverError::EquityFailure);
    }
    for (std::size_t action = 0; action < analysis.action_frequencies.size(); ++action) {
      analysis.action_frequencies[action] += combo_weight * query.value().probabilities[action];
    }
    aggregate_weight += combo_weight;

    analysis.combos.push_back(
        {hero_id, reach[actor][hero_id],
         equity_total[hero_id] > 0.0 ? equity_won[hero_id] / equity_total[hero_id] : 0.0,
         current_value.value().category, query.value().probabilities});
  }
  if (aggregate_weight > 0.0) {
    for (double &frequency : analysis.action_frequencies) {
      frequency /= aggregate_weight;
    }
  }
  return Result<PostflopNodeAnalysis, PostflopSolverError>::success(std::move(analysis));
}

Result<PostflopNodeAnalysis, PostflopSolverError>
analyze_postflop_node(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                      const PostflopCheckpoint &checkpoint, const NodeId public_node) {
  // Node zero is the public-tree root. It can be analyzed directly from the
  // canonical DAG; materializing the multi-gigabyte physical analysis tree is
  // only necessary for non-root navigation.
  auto layout = build_layout(config, ranges, true, true, public_node != NodeId{0});
  if (!layout) {
    return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(layout.error());
  }
  return analyze_postflop_node_with_layout(layout.value(), checkpoint, public_node);
}

Result<PostflopNodeAnalysis, PostflopSolverError>
analyze_postflop_node(PostflopPreparedTree &prepared, const PostflopCheckpoint &checkpoint,
                      const NodeId public_node) {
  auto &layout = prepared.implementation_->analysis_layout
                     ? *prepared.implementation_->analysis_layout
                     : prepared.implementation_->layout;
  return analyze_postflop_node_with_layout(layout, checkpoint, public_node);
}

Result<std::string, PostflopSolverError>
serialize_postflop_checkpoint(const PostflopCheckpoint &checkpoint) {
  if (checkpoint.major != PostflopCheckpoint::format_major ||
      checkpoint.minor != PostflopCheckpoint::format_minor || checkpoint.game_fingerprint.empty() ||
      checkpoint.algorithm != PostflopAlgorithm::CfrPlus ||
      checkpoint.game_fingerprint.size() > 1'024U || checkpoint.action_count == 0U ||
      checkpoint.action_count != checkpoint.cumulative_regret.size() ||
      checkpoint.cumulative_regret.size() != checkpoint.cumulative_strategy.size() ||
      !finite_vector(checkpoint.cumulative_regret) ||
      !finite_vector(checkpoint.cumulative_strategy)) {
    return Result<std::string, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  std::ostringstream output;
  output << "GTOSD_POSTFLOP_CHECKPOINT " << checkpoint.major << ' ' << checkpoint.minor << '\n'
         << checkpoint.game_fingerprint << '\n'
         << checkpoint.completed_iterations << ' ' << checkpoint.averaging_delay << ' '
         << checkpoint.cumulative_regret.size() << '\n';
  for (std::size_t index = 0; index < checkpoint.cumulative_regret.size(); ++index) {
    output << double_bits(checkpoint.cumulative_regret[index]) << ' '
           << double_bits(checkpoint.cumulative_strategy[index]) << '\n';
  }
  return Result<std::string, PostflopSolverError>::success(output.str());
}

Result<PostflopCheckpoint, PostflopSolverError>
deserialize_postflop_checkpoint(const std::string &serialized) {
  std::istringstream input(serialized);
  std::string magic;
  PostflopCheckpoint checkpoint;
  std::size_t count = 0;
  if (!(input >> magic >> checkpoint.major >> checkpoint.minor) ||
      magic != "GTOSD_POSTFLOP_CHECKPOINT") {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  if (checkpoint.major != PostflopCheckpoint::format_major ||
      checkpoint.minor != PostflopCheckpoint::format_minor) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::UnsupportedCheckpointVersion);
  }
  if (!(input >> checkpoint.game_fingerprint >> checkpoint.completed_iterations >>
        checkpoint.averaging_delay >> count) ||
      checkpoint.game_fingerprint.size() > 1'024U || count == 0U ||
      count > serialized.size() / 4U) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  checkpoint.cumulative_regret.resize(count);
  checkpoint.cumulative_strategy.resize(count);
  checkpoint.action_count = count;
  for (std::size_t index = 0; index < count; ++index) {
    std::uint64_t regret = 0;
    std::uint64_t strategy = 0;
    if (!(input >> regret >> strategy)) {
      return Result<PostflopCheckpoint, PostflopSolverError>::failure(
          PostflopSolverError::InvalidCheckpoint);
    }
    checkpoint.cumulative_regret[index] = bits_double(regret);
    checkpoint.cumulative_strategy[index] = bits_double(strategy);
  }
  input >> std::ws;
  if (!input.eof() || !finite_vector(checkpoint.cumulative_regret) ||
      !finite_vector(checkpoint.cumulative_strategy)) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  return Result<PostflopCheckpoint, PostflopSolverError>::success(std::move(checkpoint));
}

Result<bool, PostflopSolverError> save_postflop_checkpoint(const PostflopCheckpoint &checkpoint,
                                                           const std::string &path) {
  if (!checkpoint.external_buffer_file.empty()) {
    if (path.empty() || checkpoint.game_fingerprint.empty() || checkpoint.action_count == 0U ||
        checkpoint.game_fingerprint.size() > 1'024U ||
        checkpoint.major != PostflopCheckpoint::format_major ||
        checkpoint.minor != PostflopCheckpoint::format_minor ||
        checkpoint.algorithm != PostflopAlgorithm::CfrPlus ||
        !checkpoint.cumulative_regret.empty() || !checkpoint.cumulative_strategy.empty() ||
        checkpoint.action_count >
            std::numeric_limits<std::uint64_t>::max() / (2U * sizeof(double))) {
      return Result<bool, PostflopSolverError>::failure(PostflopSolverError::InvalidCheckpoint);
    }
    std::error_code size_error;
    const auto expected_size = checkpoint.action_count * 2U * sizeof(double);
    if (std::filesystem::file_size(checkpoint.external_buffer_file, size_error) != expected_size ||
        size_error) {
      return Result<bool, PostflopSolverError>::failure(PostflopSolverError::IoFailure);
    }
    const std::filesystem::path target(path);
    const auto temporary = target.string() + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << "GTOSD_POSTFLOP_EXTERNAL " << checkpoint.major << ' ' << checkpoint.minor << '\n'
           << checkpoint.game_fingerprint << '\n'
           << checkpoint.completed_iterations << ' ' << checkpoint.averaging_delay << ' '
           << checkpoint.action_count << '\n'
           << checkpoint.external_buffer_file << '\n';
    output.flush();
    output.close();
    if (!output || !atomic_replace(temporary, target)) {
      return Result<bool, PostflopSolverError>::failure(PostflopSolverError::IoFailure);
    }
    return Result<bool, PostflopSolverError>::success(true);
  }
  if (path.empty() || checkpoint.game_fingerprint.empty() ||
      checkpoint.game_fingerprint.size() > 1'024U || checkpoint.action_count == 0U ||
      checkpoint.action_count != checkpoint.cumulative_regret.size() ||
      checkpoint.cumulative_regret.size() != checkpoint.cumulative_strategy.size() ||
      !finite_vector(checkpoint.cumulative_regret) ||
      !finite_vector(checkpoint.cumulative_strategy)) {
    return Result<bool, PostflopSolverError>::failure(
        path.empty() ? PostflopSolverError::InvalidConfiguration
                     : PostflopSolverError::InvalidCheckpoint);
  }
  const std::filesystem::path target(path);
  const auto temporary = target.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    constexpr std::string_view magic = "GTOSD_POSTFLOP_BINARY_1";
    output.write(magic.data(), static_cast<std::streamsize>(magic.size()));
    std::uint64_t checksum = 1469598103934665603ULL;
    checksum_bytes(checksum, magic.data(), magic.size());
    const auto fingerprint_size = static_cast<std::uint32_t>(checkpoint.game_fingerprint.size());
    const auto value_count = static_cast<std::uint64_t>(checkpoint.cumulative_regret.size());
    if (!write_binary(output, checkpoint.major, checksum) ||
        !write_binary(output, checkpoint.minor, checksum) ||
        !write_binary(output, fingerprint_size, checksum)) {
      return Result<bool, PostflopSolverError>::failure(PostflopSolverError::IoFailure);
    }
    output.write(checkpoint.game_fingerprint.data(),
                 static_cast<std::streamsize>(checkpoint.game_fingerprint.size()));
    checksum_bytes(checksum, checkpoint.game_fingerprint.data(),
                   checkpoint.game_fingerprint.size());
    if (!write_binary(output, checkpoint.completed_iterations, checksum) ||
        !write_binary(output, checkpoint.averaging_delay, checksum) ||
        !write_binary(output, value_count, checksum)) {
      return Result<bool, PostflopSolverError>::failure(PostflopSolverError::IoFailure);
    }
    for (const double value : checkpoint.cumulative_regret) {
      if (!write_binary(output, value, checksum)) {
        return Result<bool, PostflopSolverError>::failure(PostflopSolverError::IoFailure);
      }
    }
    for (const double value : checkpoint.cumulative_strategy) {
      if (!write_binary(output, value, checksum)) {
        return Result<bool, PostflopSolverError>::failure(PostflopSolverError::IoFailure);
      }
    }
    output.write(reinterpret_cast<const char *>(&checksum), sizeof(checksum));
    output.flush();
    if (!output) {
      return Result<bool, PostflopSolverError>::failure(PostflopSolverError::IoFailure);
    }
  }
  if (!atomic_replace(temporary, target)) {
    return Result<bool, PostflopSolverError>::failure(PostflopSolverError::IoFailure);
  }
  return Result<bool, PostflopSolverError>::success(true);
}

Result<PostflopCheckpoint, PostflopSolverError> load_postflop_checkpoint(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(PostflopSolverError::IoFailure);
  }
  constexpr std::string_view external_magic = "GTOSD_POSTFLOP_EXTERNAL";
  std::string prefix(external_magic.size(), '\0');
  input.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
  input.clear();
  input.seekg(0);
  if (prefix == external_magic) {
    std::string magic;
    PostflopCheckpoint checkpoint;
    if (!(input >> magic >> checkpoint.major >> checkpoint.minor >> checkpoint.game_fingerprint >>
          checkpoint.completed_iterations >> checkpoint.averaging_delay >>
          checkpoint.action_count) ||
        magic != external_magic || checkpoint.major != PostflopCheckpoint::format_major ||
        checkpoint.minor != PostflopCheckpoint::format_minor ||
        checkpoint.game_fingerprint.size() > 1'024U || checkpoint.action_count == 0U) {
      return Result<PostflopCheckpoint, PostflopSolverError>::failure(
          PostflopSolverError::InvalidCheckpoint);
    }
    input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::getline(input, checkpoint.external_buffer_file);
    input >> std::ws;
    if (!input.eof() || checkpoint.external_buffer_file.empty() ||
        checkpoint.action_count >
            std::numeric_limits<std::uint64_t>::max() / (2U * sizeof(double))) {
      return Result<PostflopCheckpoint, PostflopSolverError>::failure(
          PostflopSolverError::InvalidCheckpoint);
    }
    std::error_code size_error;
    const auto expected_size = checkpoint.action_count * 2U * sizeof(double);
    if (std::filesystem::file_size(checkpoint.external_buffer_file, size_error) != expected_size ||
        size_error) {
      return Result<PostflopCheckpoint, PostflopSolverError>::failure(
          PostflopSolverError::InvalidCheckpoint);
    }
    return Result<PostflopCheckpoint, PostflopSolverError>::success(std::move(checkpoint));
  }
  constexpr std::string_view expected_magic = "GTOSD_POSTFLOP_BINARY_1";
  std::string magic(expected_magic.size(), '\0');
  input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  if (!input || magic != expected_magic) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  std::uint64_t checksum = 1469598103934665603ULL;
  checksum_bytes(checksum, magic.data(), magic.size());
  PostflopCheckpoint checkpoint;
  std::uint32_t fingerprint_size = 0;
  std::uint64_t value_count = 0;
  if (!read_binary(input, checkpoint.major, checksum) ||
      !read_binary(input, checkpoint.minor, checksum) ||
      !read_binary(input, fingerprint_size, checksum) || fingerprint_size == 0U ||
      fingerprint_size > 1'024U) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  if (checkpoint.major != PostflopCheckpoint::format_major ||
      checkpoint.minor != PostflopCheckpoint::format_minor) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::UnsupportedCheckpointVersion);
  }
  checkpoint.game_fingerprint.resize(fingerprint_size);
  input.read(checkpoint.game_fingerprint.data(),
             static_cast<std::streamsize>(checkpoint.game_fingerprint.size()));
  if (!input) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  checksum_bytes(checksum, checkpoint.game_fingerprint.data(), checkpoint.game_fingerprint.size());
  if (!read_binary(input, checkpoint.completed_iterations, checksum) ||
      !read_binary(input, checkpoint.averaging_delay, checksum) ||
      !read_binary(input, value_count, checksum) || value_count == 0U ||
      value_count > std::numeric_limits<std::size_t>::max() ||
      value_count > std::numeric_limits<std::uint64_t>::max() / (2U * sizeof(double))) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  const auto values_position = input.tellg();
  if (values_position == std::streampos(-1)) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  const auto values_offset =
      static_cast<std::uint64_t>(static_cast<std::streamoff>(values_position));
  std::error_code file_size_error;
  const auto file_size = std::filesystem::file_size(path, file_size_error);
  const auto payload_size = value_count * 2U * sizeof(double) + sizeof(std::uint64_t);
  if (file_size_error || values_offset > std::numeric_limits<std::uint64_t>::max() - payload_size ||
      file_size != values_offset + payload_size) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  checkpoint.cumulative_regret.resize(static_cast<std::size_t>(value_count));
  checkpoint.cumulative_strategy.resize(static_cast<std::size_t>(value_count));
  checkpoint.action_count = value_count;
  for (double &value : checkpoint.cumulative_regret) {
    if (!read_binary(input, value, checksum)) {
      return Result<PostflopCheckpoint, PostflopSolverError>::failure(
          PostflopSolverError::InvalidCheckpoint);
    }
  }
  for (double &value : checkpoint.cumulative_strategy) {
    if (!read_binary(input, value, checksum)) {
      return Result<PostflopCheckpoint, PostflopSolverError>::failure(
          PostflopSolverError::InvalidCheckpoint);
    }
  }
  std::uint64_t stored_checksum = 0;
  input.read(reinterpret_cast<char *>(&stored_checksum), sizeof(stored_checksum));
  input.peek();
  if (!input.eof() || stored_checksum != checksum || !finite_vector(checkpoint.cumulative_regret) ||
      !finite_vector(checkpoint.cumulative_strategy)) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  return Result<PostflopCheckpoint, PostflopSolverError>::success(std::move(checkpoint));
}

const char *postflop_solver_error_name(const PostflopSolverError error) noexcept {
  switch (error) {
  case PostflopSolverError::InvalidConfiguration:
    return "invalid_configuration";
  case PostflopSolverError::TreeFailure:
    return "tree_failure";
  case PostflopSolverError::MemoryFailure:
    return "memory_failure";
  case PostflopSolverError::EquityFailure:
    return "equity_failure";
  case PostflopSolverError::SettlementFailure:
    return "settlement_failure";
  case PostflopSolverError::NumericalFailure:
    return "numerical_failure";
  case PostflopSolverError::CheckpointMismatch:
    return "checkpoint_mismatch";
  case PostflopSolverError::InvalidCheckpoint:
    return "invalid_checkpoint";
  case PostflopSolverError::UnsupportedCheckpointVersion:
    return "unsupported_checkpoint_version";
  case PostflopSolverError::IoFailure:
    return "io_failure";
  }
  return "unknown";
}

} // namespace gtosd
