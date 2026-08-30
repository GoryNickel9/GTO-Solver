#include <benchmark/benchmark.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <span>
#include <vector>

namespace {

constexpr std::size_t hand_count = 630U;
constexpr std::size_t action_count = 3U;
constexpr std::size_t entry_count = hand_count * action_count;

struct Input {
  std::vector<std::uint16_t> regret;
  std::vector<std::uint16_t> average;
  std::vector<float> immediate;
  std::vector<float> average_addition;
  float regret_scale{0.03125F};
  float average_scale{0.015625F};
  float positive_discount{0.992F};
  float negative_discount{0.0F};
  float regret_weight{1.0F};
};

struct Output {
  std::vector<std::uint16_t> regret;
  std::vector<std::uint16_t> average;
  float regret_scale{0.0F};
  float average_scale{0.0F};

  friend bool operator==(const Output &, const Output &) = default;
};

[[nodiscard]] Input make_input() {
  Input input;
  input.regret.resize(entry_count);
  input.average.resize(entry_count);
  input.immediate.resize(entry_count);
  input.average_addition.resize(entry_count);
  std::mt19937 generator(0x5eed1234U);
  std::uniform_int_distribution<int> regret_code(-32767, 32767);
  std::uniform_int_distribution<unsigned> average_code(0U, 65535U);
  std::uniform_real_distribution<float> immediate(-24.0F, 24.0F);
  std::uniform_real_distribution<float> addition(0.0F, 12.0F);
  for (std::size_t index = 0U; index < entry_count; ++index) {
    input.regret[index] = static_cast<std::uint16_t>(static_cast<std::int16_t>(regret_code(generator)));
    input.average[index] = static_cast<std::uint16_t>(average_code(generator));
    input.immediate[index] = immediate(generator);
    input.average_addition[index] = addition(generator);
  }
  return input;
}

[[nodiscard]] float regret_value(const Input &input, const std::size_t index) noexcept {
  const float old = static_cast<float>(static_cast<std::int16_t>(input.regret[index])) *
                    input.regret_scale;
  const float discounted = old * (old > 0.0F ? input.positive_discount : input.negative_discount);
  return discounted + input.regret_weight * input.immediate[index];
}

[[nodiscard]] float average_value(const Input &input, const std::size_t index) noexcept {
  return static_cast<float>(input.average[index]) * input.average_scale +
         input.average_addition[index];
}

[[nodiscard]] std::uint16_t encode_regret(const float value, const float scale,
                                          const std::size_t local) noexcept {
  if (!(scale > 0.0F)) {
    return 0U;
  }
  const double inverse = 1.0 / static_cast<double>(scale);
  const double normalized = local < hand_count - hand_count % 8U
                                ? static_cast<double>(value * static_cast<float>(inverse))
                                : static_cast<double>(value) * inverse;
  const auto code = static_cast<std::int16_t>(
      std::clamp(std::nearbyint(normalized), -32767.0, 32767.0));
  return static_cast<std::uint16_t>(code);
}

[[nodiscard]] std::uint16_t encode_average(const float value, const float scale,
                                           const std::size_t local) noexcept {
  if (!(scale > 0.0F)) {
    return 0U;
  }
  const double inverse = 1.0 / static_cast<double>(scale);
  const double normalized = local < hand_count - hand_count % 8U
                                ? static_cast<double>(value * static_cast<float>(inverse))
                                : static_cast<double>(value) * inverse;
  return static_cast<std::uint16_t>(
      std::clamp(std::nearbyint(normalized), 0.0, 65535.0));
}

template <typename RegretJournal, typename AverageJournal, typename Store>
[[nodiscard]] Output retain_update(const Input &input, RegretJournal &regret_journal,
                                   AverageJournal &average_journal, Store store) {
  float maximum_regret = 0.0F;
  float maximum_average = 0.0F;
  for (std::size_t index = 0U; index < entry_count; ++index) {
    const float regret = regret_value(input, index);
    const float average = average_value(input, index);
    store(regret_journal[index], regret);
    store(average_journal[index], average);
    maximum_regret = std::max(maximum_regret, std::abs(regret));
    maximum_average = std::max(maximum_average, average);
  }
  Output output;
  output.regret.resize(entry_count);
  output.average.resize(entry_count);
  output.regret_scale = maximum_regret > 0.0F ? static_cast<float>(maximum_regret / 32767.0F) : 0.0F;
  output.average_scale = maximum_average > 0.0F ? static_cast<float>(maximum_average / 65535.0F) : 0.0F;
  for (std::size_t index = 0U; index < entry_count; ++index) {
    const auto local = index % hand_count;
    const float regret = std::bit_cast<float>(static_cast<std::uint32_t>(regret_journal[index]));
    const float average = std::bit_cast<float>(static_cast<std::uint32_t>(average_journal[index]));
    output.regret[index] = encode_regret(regret, output.regret_scale, local);
    output.average[index] = encode_average(average, output.average_scale, local);
  }
  return output;
}

[[nodiscard]] Output legacy_update(const Input &input) {
  std::vector<std::uint32_t> regrets(entry_count);
  std::vector<std::uint32_t> averages(entry_count);
  return retain_update(input, regrets, averages, [](std::uint32_t &destination, const float value) {
    destination = std::bit_cast<std::uint32_t>(value);
  });
}

[[nodiscard]] Output exponent_residual_update(const Input &input) {
  // Sign+exponent and mantissa are logically separated, but packed back into
  // 32 bits.  Dropping any mantissa bit is tested adversarially below.
  std::vector<std::uint32_t> regrets(entry_count);
  std::vector<std::uint32_t> averages(entry_count);
  return retain_update(input, regrets, averages, [](std::uint32_t &destination, const float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    const auto sign_exponent = bits & 0xff800000U;
    const auto residual = bits & 0x007fffffU;
    destination = sign_exponent | residual;
  });
}

[[nodiscard]] Output recompute_update(const Input &input) {
  float maximum_regret = 0.0F;
  float maximum_average = 0.0F;
  for (std::size_t index = 0U; index < entry_count; ++index) {
    maximum_regret = std::max(maximum_regret, std::abs(regret_value(input, index)));
    maximum_average = std::max(maximum_average, average_value(input, index));
  }
  Output output;
  output.regret.resize(entry_count);
  output.average.resize(entry_count);
  output.regret_scale = maximum_regret > 0.0F ? static_cast<float>(maximum_regret / 32767.0F) : 0.0F;
  output.average_scale = maximum_average > 0.0F ? static_cast<float>(maximum_average / 65535.0F) : 0.0F;
  for (std::size_t index = 0U; index < entry_count; ++index) {
    const auto local = index % hand_count;
    output.regret[index] = encode_regret(regret_value(input, index), output.regret_scale, local);
    output.average[index] = encode_average(average_value(input, index), output.average_scale, local);
  }
  return output;
}

[[nodiscard]] Output mixed_update(const Input &input) {
  std::vector<std::uint32_t> retained_average(entry_count);
  float maximum_regret = 0.0F;
  float maximum_average = 0.0F;
  for (std::size_t index = 0U; index < entry_count; ++index) {
    maximum_regret = std::max(maximum_regret, std::abs(regret_value(input, index)));
    const float average = average_value(input, index);
    retained_average[index] = std::bit_cast<std::uint32_t>(average);
    maximum_average = std::max(maximum_average, average);
  }
  Output output;
  output.regret.resize(entry_count);
  output.average.resize(entry_count);
  output.regret_scale = maximum_regret > 0.0F ? static_cast<float>(maximum_regret / 32767.0F) : 0.0F;
  output.average_scale = maximum_average > 0.0F ? static_cast<float>(maximum_average / 65535.0F) : 0.0F;
  for (std::size_t index = 0U; index < entry_count; ++index) {
    const auto local = index % hand_count;
    output.regret[index] = encode_regret(regret_value(input, index), output.regret_scale, local);
    output.average[index] = encode_average(std::bit_cast<float>(retained_average[index]),
                                           output.average_scale, local);
  }
  return output;
}

[[nodiscard]] bool collision_proof_tests() {
  // Any summary dropping the low mantissa bit aliases adjacent floats.  Pick a
  // future scale whose half-integer boundary lies strictly between them.
  const float x = 1.0F;
  const float y = std::nextafter(x, 2.0F);
  const auto truncated = [](const float value) { return std::bit_cast<std::uint32_t>(value) >> 1U; };
  if (x == y || truncated(x) != truncated(y)) {
    return false;
  }
  const double boundary = (static_cast<double>(x) + static_cast<double>(y)) * 0.5;
  const float future_scale = static_cast<float>(boundary / 1000.5);
  if (encode_average(x, future_scale, hand_count - 1U) ==
      encode_average(y, future_scale, hand_count - 1U)) {
    return false;
  }

  // Exhaustive reduced domain: retained bits and recomputation must preserve
  // both code arrays and float scale bits over signs, zero and scale ratios.
  for (int code = -3; code <= 3; ++code) {
    Input input = make_input();
    input.regret_scale = code == 0 ? 0.0F : std::ldexp(1.0F, code - 4);
    input.average_scale = std::ldexp(1.0F, code - 5);
    for (std::size_t index = 0U; index < entry_count; ++index) {
      const auto signed_code =
          static_cast<std::int16_t>(static_cast<int>(index % 7U) - 3);
      input.regret[index] = static_cast<std::uint16_t>(signed_code);
      input.average[index] = static_cast<std::uint16_t>(index % 8U);
      input.immediate[index] = static_cast<float>(static_cast<int>(index % 9U) - 4) * 0.125F;
      input.average_addition[index] = static_cast<float>(index % 5U) * 0.0625F;
    }
    const auto oracle = legacy_update(input);
    if (!(oracle == exponent_residual_update(input)) || !(oracle == recompute_update(input)) ||
        !(oracle == mixed_update(input))) {
      return false;
    }
  }
  return true;
}

Input benchmark_input = make_input();

void BM_Legacy(benchmark::State &state) {
  for (auto _ : state) {
    const auto output = legacy_update(benchmark_input);
    benchmark::DoNotOptimize(output.regret.data());
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * entry_count * 24U));
}

void BM_ExponentResidual(benchmark::State &state) {
  for (auto _ : state) {
    const auto output = exponent_residual_update(benchmark_input);
    benchmark::DoNotOptimize(output.regret.data());
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * entry_count * 24U));
}

void BM_Recompute(benchmark::State &state) {
  for (auto _ : state) {
    const auto output = recompute_update(benchmark_input);
    benchmark::DoNotOptimize(output.regret.data());
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * entry_count * 16U));
}

void BM_Mixed(benchmark::State &state) {
  for (auto _ : state) {
    const auto output = mixed_update(benchmark_input);
    benchmark::DoNotOptimize(output.regret.data());
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * entry_count * 20U));
}

BENCHMARK(BM_Legacy);
BENCHMARK(BM_ExponentResidual);
BENCHMARK(BM_Recompute);
BENCHMARK(BM_Mixed);

} // namespace

int main(int argc, char **argv) {
  if (!collision_proof_tests()) {
    std::cerr << "EXACT_STATE_ORACLE=FAIL\n";
    return EXIT_FAILURE;
  }
  const auto oracle = legacy_update(benchmark_input);
  const bool exact = oracle == exponent_residual_update(benchmark_input) &&
                     oracle == recompute_update(benchmark_input) &&
                     oracle == mixed_update(benchmark_input);
  std::cout << "EXACT_STATE_ORACLE=" << (exact ? "PASS" : "FAIL")
            << " entries=" << entry_count
            << " retained_bytes_per_value=4 exponent_residual_bytes_per_value=4"
               " recompute_bytes_per_value=0 mixed_bytes_per_value=2\n";
  if (!exact) {
    return EXIT_FAILURE;
  }
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return EXIT_SUCCESS;
}
