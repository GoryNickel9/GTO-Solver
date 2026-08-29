#include <benchmark/benchmark.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <random>
#include <vector>

namespace {

constexpr std::size_t codec_entry_count = 1U << 20U;

std::uint32_t load24(const std::uint8_t *bytes) noexcept {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U);
}

void store24(std::uint8_t *bytes, const std::uint32_t word) noexcept {
  bytes[0] = static_cast<std::uint8_t>(word);
  bytes[1] = static_cast<std::uint8_t>(word >> 8U);
  bytes[2] = static_cast<std::uint8_t>(word >> 16U);
}

std::uint16_t encode_signed13(const float value) noexcept {
  const std::uint32_t raw = std::bit_cast<std::uint32_t>(value);
  const std::uint16_t sign = (raw >> 31U) != 0U ? 0x1000U : 0U;
  const std::uint32_t magnitude = raw & 0x7fffffffU;
  const std::uint32_t discarded = magnitude & 0x7ffffU;
  std::uint32_t packed = magnitude >> 19U;
  if (discarded > 0x40000U || (discarded == 0x40000U && (packed & 1U) != 0U)) {
    ++packed;
  }
  return static_cast<std::uint16_t>(sign | std::min(packed, 0xffeU));
}

float decode_signed13(const std::uint16_t code) noexcept {
  const std::uint32_t sign = (code & 0x1000U) != 0U ? 0x80000000U : 0U;
  return std::bit_cast<float>(sign |
      (static_cast<std::uint32_t>(code & 0x0fffU) << 19U));
}

std::uint16_t encode_strategy11(const float value) noexcept {
  if (!(value > 0.0F)) {
    return 0U;
  }
  const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
  const auto exponent = static_cast<int>((bits >> 23U) & 0xffU) - 112;
  if (exponent <= 0) {
    return static_cast<std::uint16_t>(std::clamp(
        std::nearbyint(std::ldexp(static_cast<double>(value), 20)), 0.0, 63.0));
  }
  if (exponent >= 31) {
    return 0x7ffU;
  }
  std::uint32_t mantissa = (bits & 0x7fffffU) >> 17U;
  const std::uint32_t discarded = bits & 0x1ffffU;
  if (discarded > 0x10000U || (discarded == 0x10000U && (mantissa & 1U) != 0U)) {
    ++mantissa;
    if (mantissa == 64U) {
      mantissa = 0U;
      return static_cast<std::uint16_t>(std::min(exponent + 1, 31) << 6U);
    }
  }
  return static_cast<std::uint16_t>((exponent << 6U) | mantissa);
}

float decode_strategy11(const std::uint16_t code) noexcept {
  const std::uint32_t exponent = code >> 6U;
  const std::uint32_t mantissa = code & 0x3fU;
  if (exponent == 0U) {
    return std::ldexp(static_cast<float>(mantissa), -20);
  }
  return std::bit_cast<float>(((exponent + 112U) << 23U) | (mantissa << 17U));
}

struct Fixture {
  std::vector<float> regrets = std::vector<float>(codec_entry_count);
  std::vector<float> averages = std::vector<float>(codec_entry_count);
  std::vector<std::uint16_t> scaled_regrets =
      std::vector<std::uint16_t>(codec_entry_count);
  std::vector<std::uint16_t> scaled_averages =
      std::vector<std::uint16_t>(codec_entry_count);
  std::vector<std::uint8_t> packed = std::vector<std::uint8_t>(codec_entry_count * 3U);
  std::vector<std::uint32_t> random_indices =
      std::vector<std::uint32_t>(codec_entry_count);
  float regret_scale{1.0F / 512.0F};
  float average_scale{1.0F / 1024.0F};

  Fixture() {
    std::mt19937 generator(0x47544f53U);
    std::uniform_real_distribution<float> regret_distribution(-48.0F, 48.0F);
    std::uniform_real_distribution<float> average_distribution(0.0F, 64.0F);
    for (std::size_t index = 0U; index < codec_entry_count; ++index) {
      regrets[index] = regret_distribution(generator);
      averages[index] = average_distribution(generator);
      scaled_regrets[index] = static_cast<std::uint16_t>(static_cast<std::int16_t>(
          std::clamp(std::nearbyint(regrets[index] / regret_scale), -32767.0F, 32767.0F)));
      scaled_averages[index] = static_cast<std::uint16_t>(
          std::clamp(std::nearbyint(averages[index] / average_scale), 0.0F, 65535.0F));
      store24(packed.data() + index * 3U,
              encode_signed13(regrets[index]) |
                  (static_cast<std::uint32_t>(encode_strategy11(averages[index])) << 13U));
      random_indices[index] = static_cast<std::uint32_t>(index);
    }
    std::shuffle(random_indices.begin(), random_indices.end(), generator);
  }
};

Fixture &fixture() {
  static Fixture value;
  return value;
}

void BM_ScaledI16U16SequentialUpdate(benchmark::State &state) {
  auto &data = fixture();
  for (auto _ : state) {
    static_cast<void>(_);
    for (std::size_t index = 0U; index < codec_entry_count; ++index) {
      const float regret = static_cast<std::int16_t>(data.scaled_regrets[index]) *
                               data.regret_scale + 0.015625F;
      const float average = data.scaled_averages[index] * data.average_scale + 0.0078125F;
      data.scaled_regrets[index] = static_cast<std::uint16_t>(static_cast<std::int16_t>(
          std::clamp(std::nearbyint(regret / data.regret_scale), -32767.0F, 32767.0F)));
      data.scaled_averages[index] = static_cast<std::uint16_t>(
          std::clamp(std::nearbyint(average / data.average_scale), 0.0F, 65535.0F));
    }
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(codec_entry_count * 8U));
  state.counters["state_bytes_per_entry"] = 4.0;
}

void BM_Signed13Strategy11SequentialUpdate(benchmark::State &state) {
  auto &data = fixture();
  for (auto _ : state) {
    static_cast<void>(_);
    for (std::size_t index = 0U; index < codec_entry_count; ++index) {
      auto *const bytes = data.packed.data() + index * 3U;
      const std::uint32_t word = load24(bytes);
      const float regret = decode_signed13(static_cast<std::uint16_t>(word & 0x1fffU)) +
                           0.015625F;
      const float average = decode_strategy11(static_cast<std::uint16_t>(word >> 13U)) +
                            0.0078125F;
      store24(bytes, encode_signed13(regret) |
                         (static_cast<std::uint32_t>(encode_strategy11(average)) << 13U));
    }
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(codec_entry_count * 6U));
  state.counters["state_bytes_per_entry"] = 3.0;
}

void BM_ScaledI16U16RandomDecode(benchmark::State &state) {
  auto &data = fixture();
  double sum = 0.0;
  for (auto _ : state) {
    static_cast<void>(_);
    for (const auto index : data.random_indices) {
      sum += static_cast<std::int16_t>(data.scaled_regrets[index]) * data.regret_scale;
      sum += data.scaled_averages[index] * data.average_scale;
    }
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(codec_entry_count));
}

void BM_Signed13Strategy11RandomDecode(benchmark::State &state) {
  auto &data = fixture();
  double sum = 0.0;
  for (auto _ : state) {
    static_cast<void>(_);
    for (const auto index : data.random_indices) {
      const std::uint32_t word = load24(data.packed.data() + index * 3U);
      sum += decode_signed13(static_cast<std::uint16_t>(word & 0x1fffU));
      sum += decode_strategy11(static_cast<std::uint16_t>(word >> 13U));
    }
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(codec_entry_count));
}

void BM_QuantizationError(benchmark::State &state) {
  const auto &data = fixture();
  double scaled_sum = 0.0;
  double packed_sum = 0.0;
  double scaled_max = 0.0;
  double packed_max = 0.0;
  for (std::size_t index = 0U; index < codec_entry_count; ++index) {
    const auto scaled_regret = static_cast<std::int16_t>(std::clamp(
        std::nearbyint(data.regrets[index] / data.regret_scale), -32767.0F, 32767.0F));
    const double scaled_error = std::abs(
        static_cast<double>(scaled_regret * data.regret_scale - data.regrets[index]));
    const double packed_error = std::abs(static_cast<double>(
        decode_signed13(encode_signed13(data.regrets[index])) - data.regrets[index]));
    scaled_sum += scaled_error;
    packed_sum += packed_error;
    scaled_max = std::max(scaled_max, scaled_error);
    packed_max = std::max(packed_max, packed_error);
  }
  for (auto _ : state) {
    benchmark::DoNotOptimize(_);
    benchmark::DoNotOptimize(scaled_sum);
    benchmark::DoNotOptimize(packed_sum);
  }
  state.counters["scaled_regret_mean_abs_error"] = scaled_sum / codec_entry_count;
  state.counters["scaled_regret_max_abs_error"] = scaled_max;
  state.counters["signed13_regret_mean_abs_error"] = packed_sum / codec_entry_count;
  state.counters["signed13_regret_max_abs_error"] = packed_max;
}

BENCHMARK(BM_ScaledI16U16SequentialUpdate);
BENCHMARK(BM_Signed13Strategy11SequentialUpdate);
BENCHMARK(BM_ScaledI16U16RandomDecode);
BENCHMARK(BM_Signed13Strategy11RandomDecode);
BENCHMARK(BM_QuantizationError)->Iterations(1);

} // namespace
