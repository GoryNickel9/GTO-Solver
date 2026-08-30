#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t hand_count = 630U;
constexpr std::size_t action_count = 3U;
constexpr std::size_t entry_count = hand_count * action_count;
constexpr float regret_capacity = 32767.0F;
constexpr float average_capacity = 65535.0F;
constexpr float positive_discount = 0.992F;
constexpr float strategy_weight = 6400.0F;

[[nodiscard]] constexpr std::size_t entry(const std::size_t action,
                                          const std::size_t hand) noexcept {
  return action * hand_count + hand;
}

struct FloatState {
  std::vector<float> regret = std::vector<float>(entry_count);
  std::vector<float> average = std::vector<float>(entry_count);
};

struct EncodedState {
  std::vector<std::uint16_t> regret = std::vector<std::uint16_t>(entry_count);
  std::vector<std::uint16_t> average = std::vector<std::uint16_t>(entry_count);
  std::vector<float> regret_scale;
  std::vector<float> average_scale;
};

struct BFloatState {
  std::vector<std::uint16_t> regret = std::vector<std::uint16_t>(entry_count);
  std::vector<std::uint16_t> average = std::vector<std::uint16_t>(entry_count);
};

struct SignedFloat24BFloatState {
  std::vector<std::uint8_t> regret = std::vector<std::uint8_t>(entry_count * 3U);
  std::vector<std::uint16_t> average = std::vector<std::uint16_t>(entry_count);
};

struct Workload {
  FloatState initial;
  std::vector<float> terminal = std::vector<float>(entry_count);
  std::vector<float> reach = std::vector<float>(hand_count);
};

struct PipelineScratch {
  std::vector<float> action_values = std::vector<float>(entry_count);
  std::vector<float> strategy = std::vector<float>(entry_count);
  std::vector<float> current = std::vector<float>(hand_count);
  FloatState updated;
  std::vector<float> parent = std::vector<float>(hand_count);
};

[[nodiscard]] Workload make_workload() {
  Workload workload;
  std::mt19937 generator(0x54494c45U);
  std::uniform_real_distribution<float> regret(-180.0F, 180.0F);
  std::uniform_real_distribution<float> average(0.0F, 2500.0F);
  std::uniform_real_distribution<float> terminal(-32.0F, 32.0F);
  std::uniform_real_distribution<float> reach(0.0F, 1.0F);
  for (std::size_t hand = 0U; hand < hand_count; ++hand) {
    workload.reach[hand] = reach(generator);
    for (std::size_t action = 0U; action < action_count; ++action) {
      const auto index = entry(action, hand);
      workload.initial.regret[index] = regret(generator);
      workload.initial.average[index] = average(generator);
      workload.terminal[index] = terminal(generator);
    }
  }
  // Deterministic pathological regret-matching cases.
  const std::array<std::array<float, action_count>, 5U> cases{{
      {-3.0F, -2.0F, -1.0F}, {0.00001F, -4.0F, -8.0F},
      {1.0F, std::nextafter(1.0F, 2.0F), -1.0F}, {-0.00001F, 0.00001F, 0.0F},
      {0.0001F, 1000.0F, 10.0F}}};
  for (std::size_t hand = 0U; hand < cases.size(); ++hand) {
    for (std::size_t action = 0U; action < action_count; ++action) {
      workload.initial.regret[entry(action, hand)] = cases[hand][action];
    }
  }
  return workload;
}

[[nodiscard]] std::uint16_t encode_regret(const float value, const float scale) noexcept {
  if (!(scale > 0.0F)) {
    return 0U;
  }
  const auto code = static_cast<std::int16_t>(std::clamp(
      std::nearbyint(static_cast<double>(value) / static_cast<double>(scale)),
      -static_cast<double>(regret_capacity), static_cast<double>(regret_capacity)));
  return static_cast<std::uint16_t>(code);
}

[[nodiscard]] std::uint16_t encode_average(const float value, const float scale) noexcept {
  if (!(scale > 0.0F)) {
    return 0U;
  }
  return static_cast<std::uint16_t>(std::clamp(
      std::nearbyint(static_cast<double>(value) / static_cast<double>(scale)), 0.0,
      static_cast<double>(average_capacity)));
}

[[nodiscard]] float power_of_two_scale(const float maximum, const float capacity) noexcept {
  if (!(maximum > 0.0F)) {
    return 0.0F;
  }
  const float exact = maximum / capacity;
  return std::ldexp(1.0F, static_cast<int>(std::ceil(std::log2(exact))));
}

[[nodiscard]] std::uint16_t encode_bfloat16(const float value) noexcept {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  const auto rounded = bits + 0x7fffU + ((bits >> 16U) & 1U);
  return static_cast<std::uint16_t>(rounded >> 16U);
}

[[nodiscard]] float decode_bfloat16(const std::uint16_t value) noexcept {
  return std::bit_cast<float>(static_cast<std::uint32_t>(value) << 16U);
}

[[nodiscard]] BFloatState encode_bfloat_state(const FloatState &source) {
  BFloatState output;
  for (std::size_t index = 0U; index < entry_count; ++index) {
    output.regret[index] = encode_bfloat16(source.regret[index]);
    output.average[index] = encode_bfloat16(source.average[index]);
  }
  return output;
}

[[nodiscard]] FloatState decode_bfloat_state(const BFloatState &source) {
  FloatState output;
  for (std::size_t index = 0U; index < entry_count; ++index) {
    output.regret[index] = decode_bfloat16(source.regret[index]);
    output.average[index] = decode_bfloat16(source.average[index]);
  }
  return output;
}

void encode_signed_float24(std::uint8_t *const destination, const float value) noexcept {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  const auto rounded = bits + 0x7fU + ((bits >> 8U) & 1U);
  const auto encoded = rounded >> 8U;
  destination[0] = static_cast<std::uint8_t>(encoded);
  destination[1] = static_cast<std::uint8_t>(encoded >> 8U);
  destination[2] = static_cast<std::uint8_t>(encoded >> 16U);
}

[[nodiscard]] float decode_signed_float24(const std::uint8_t *const source) noexcept {
  const auto encoded = static_cast<std::uint32_t>(source[0]) |
                       (static_cast<std::uint32_t>(source[1]) << 8U) |
                       (static_cast<std::uint32_t>(source[2]) << 16U);
  return std::bit_cast<float>(encoded << 8U);
}

[[nodiscard]] SignedFloat24BFloatState encode_float24_bfloat_state(const FloatState &source) {
  SignedFloat24BFloatState output;
  for (std::size_t index = 0U; index < entry_count; ++index) {
    encode_signed_float24(output.regret.data() + index * 3U, source.regret[index]);
    output.average[index] = encode_bfloat16(source.average[index]);
  }
  return output;
}

[[nodiscard]] FloatState decode_float24_bfloat_state(const SignedFloat24BFloatState &source) {
  FloatState output;
  for (std::size_t index = 0U; index < entry_count; ++index) {
    output.regret[index] = decode_signed_float24(source.regret.data() + index * 3U);
    output.average[index] = decode_bfloat16(source.average[index]);
  }
  return output;
}

template <std::size_t TileSize>
[[nodiscard]] EncodedState encode_tiles(const FloatState &source, const bool power_of_two) {
  const auto tile_count = (hand_count + TileSize - 1U) / TileSize;
  EncodedState output;
  output.regret_scale.resize(tile_count);
  output.average_scale.resize(tile_count);
  for (std::size_t tile = 0U; tile < tile_count; ++tile) {
    const auto begin = tile * TileSize;
    const auto end = std::min(begin + TileSize, hand_count);
    float maximum_regret = 0.0F;
    float maximum_average = 0.0F;
    for (std::size_t action = 0U; action < action_count; ++action) {
      for (std::size_t hand = begin; hand < end; ++hand) {
        maximum_regret = std::max(maximum_regret, std::abs(source.regret[entry(action, hand)]));
        maximum_average = std::max(maximum_average, source.average[entry(action, hand)]);
      }
    }
    const float regret_scale =
        power_of_two ? power_of_two_scale(maximum_regret, regret_capacity)
                     : maximum_regret / regret_capacity;
    const float average_scale =
        power_of_two ? power_of_two_scale(maximum_average, average_capacity)
                     : maximum_average / average_capacity;
    output.regret_scale[tile] = regret_scale;
    output.average_scale[tile] = average_scale;
    for (std::size_t action = 0U; action < action_count; ++action) {
      for (std::size_t hand = begin; hand < end; ++hand) {
        const auto index = entry(action, hand);
        output.regret[index] = encode_regret(source.regret[index], regret_scale);
        output.average[index] = encode_average(source.average[index], average_scale);
      }
    }
  }
  return output;
}

[[nodiscard]] EncodedState encode_node(const FloatState &source) {
  return encode_tiles<hand_count>(source, false);
}

void regret_matching(const std::array<float, action_count> &regrets,
                     std::array<float, action_count> &strategy) noexcept {
  float sum = 0.0F;
  for (std::size_t action = 0U; action < action_count; ++action) {
    strategy[action] = std::max(0.0F, regrets[action]);
    sum += strategy[action];
  }
  if (sum > 0.0F) {
    for (auto &probability : strategy) {
      probability /= sum;
    }
  } else {
    strategy.fill(1.0F / static_cast<float>(action_count));
  }
}

[[nodiscard]] float produce_action_value(const Workload &workload, const std::size_t action,
                                         const std::size_t hand) noexcept {
  const float terminal = workload.terminal[entry(action, hand)];
  const float adjacent = workload.terminal[entry(action, (hand + 1U) % hand_count)];
  return std::fma(adjacent, 0.125F,
                  std::fma(workload.reach[hand], static_cast<float>(action + 1U), terminal));
}

void reference_pipeline(const Workload &workload, PipelineScratch &scratch) {
  for (std::size_t hand = 0U; hand < hand_count; ++hand) {
    std::array<float, action_count> regrets{};
    std::array<float, action_count> strategy{};
    for (std::size_t action = 0U; action < action_count; ++action) {
      regrets[action] = workload.initial.regret[entry(action, hand)];
    }
    regret_matching(regrets, strategy);
    float current = 0.0F;
    for (std::size_t action = 0U; action < action_count; ++action) {
      const auto index = entry(action, hand);
      const float value = produce_action_value(workload, action, hand);
      scratch.action_values[index] = value;
      scratch.strategy[index] = strategy[action];
      current = std::fma(strategy[action], value, current);
    }
    scratch.current[hand] = current;
    scratch.parent[hand] = current;
    for (std::size_t action = 0U; action < action_count; ++action) {
      const auto index = entry(action, hand);
      const float old_regret = workload.initial.regret[index];
      scratch.updated.regret[index] =
          std::fma(old_regret, old_regret > 0.0F ? positive_discount : 0.0F,
                   scratch.action_values[index] - current);
      scratch.updated.average[index] =
          std::fma(strategy_weight * workload.reach[hand], strategy[action],
                   workload.initial.average[index]);
    }
  }
}

void legacy_pipeline(const Workload &workload, const EncodedState &input, EncodedState &output,
                     PipelineScratch &scratch) {
  const float regret_scale = input.regret_scale.front();
  const float average_scale = input.average_scale.front();
  for (std::size_t hand = 0U; hand < hand_count; ++hand) {
    std::array<float, action_count> regrets{};
    std::array<float, action_count> strategy{};
    for (std::size_t action = 0U; action < action_count; ++action) {
      regrets[action] = static_cast<float>(static_cast<std::int16_t>(
                            input.regret[entry(action, hand)])) *
                        regret_scale;
    }
    regret_matching(regrets, strategy);
    float current = 0.0F;
    for (std::size_t action = 0U; action < action_count; ++action) {
      const auto index = entry(action, hand);
      scratch.strategy[index] = strategy[action];
      scratch.action_values[index] = produce_action_value(workload, action, hand);
      current = std::fma(strategy[action], scratch.action_values[index], current);
    }
    scratch.current[hand] = current;
    scratch.parent[hand] = current;
  }
  float maximum_regret = 0.0F;
  float maximum_average = 0.0F;
  for (std::size_t index = 0U; index < entry_count; ++index) {
    const auto hand = index % hand_count;
    const float old_regret =
        static_cast<float>(static_cast<std::int16_t>(input.regret[index])) * regret_scale;
    const float old_average = static_cast<float>(input.average[index]) * average_scale;
    const float updated_regret =
        std::fma(old_regret, old_regret > 0.0F ? positive_discount : 0.0F,
                 scratch.action_values[index] - scratch.current[hand]);
    const float updated_average =
        std::fma(strategy_weight * workload.reach[hand], scratch.strategy[index], old_average);
    scratch.updated.regret[index] = updated_regret;
    scratch.updated.average[index] = updated_average;
    maximum_regret = std::max(maximum_regret, std::abs(updated_regret));
    maximum_average = std::max(maximum_average, updated_average);
  }
  output.regret_scale[0] = maximum_regret / regret_capacity;
  output.average_scale[0] = maximum_average / average_capacity;
  for (std::size_t index = 0U; index < entry_count; ++index) {
    output.regret[index] = encode_regret(scratch.updated.regret[index], output.regret_scale[0]);
    output.average[index] = encode_average(scratch.updated.average[index], output.average_scale[0]);
  }
}

void direct_bfloat_pipeline(const Workload &workload, const BFloatState &input,
                            BFloatState &output, PipelineScratch &scratch) {
  for (std::size_t hand = 0U; hand < hand_count; ++hand) {
    std::array<float, action_count> regrets{};
    std::array<float, action_count> averages{};
    std::array<float, action_count> strategy{};
    std::array<float, action_count> values{};
    for (std::size_t action = 0U; action < action_count; ++action) {
      const auto index = entry(action, hand);
      regrets[action] = decode_bfloat16(input.regret[index]);
      averages[action] = decode_bfloat16(input.average[index]);
    }
    regret_matching(regrets, strategy);
    float current = 0.0F;
    for (std::size_t action = 0U; action < action_count; ++action) {
      values[action] = produce_action_value(workload, action, hand);
      current = std::fma(strategy[action], values[action], current);
    }
    scratch.parent[hand] = current;
    for (std::size_t action = 0U; action < action_count; ++action) {
      const auto index = entry(action, hand);
      const float updated_regret =
          std::fma(regrets[action], regrets[action] > 0.0F ? positive_discount : 0.0F,
                   values[action] - current);
      const float updated_average =
          std::fma(strategy_weight * workload.reach[hand], strategy[action], averages[action]);
      output.regret[index] = encode_bfloat16(updated_regret);
      output.average[index] = encode_bfloat16(updated_average);
    }
  }
}

void direct_float24_bfloat_pipeline(const Workload &workload,
                                    const SignedFloat24BFloatState &input,
                                    SignedFloat24BFloatState &output,
                                    PipelineScratch &scratch) {
  for (std::size_t hand = 0U; hand < hand_count; ++hand) {
    std::array<float, action_count> regrets{};
    std::array<float, action_count> averages{};
    std::array<float, action_count> strategy{};
    std::array<float, action_count> values{};
    for (std::size_t action = 0U; action < action_count; ++action) {
      const auto index = entry(action, hand);
      regrets[action] = decode_signed_float24(input.regret.data() + index * 3U);
      averages[action] = decode_bfloat16(input.average[index]);
    }
    regret_matching(regrets, strategy);
    float current = 0.0F;
    for (std::size_t action = 0U; action < action_count; ++action) {
      values[action] = produce_action_value(workload, action, hand);
      current = std::fma(strategy[action], values[action], current);
    }
    scratch.parent[hand] = current;
    for (std::size_t action = 0U; action < action_count; ++action) {
      const auto index = entry(action, hand);
      const float updated_regret =
          std::fma(regrets[action], regrets[action] > 0.0F ? positive_discount : 0.0F,
                   values[action] - current);
      const float updated_average =
          std::fma(strategy_weight * workload.reach[hand], strategy[action], averages[action]);
      encode_signed_float24(output.regret.data() + index * 3U, updated_regret);
      output.average[index] = encode_bfloat16(updated_average);
    }
  }
}

template <std::size_t TileSize, bool PowerOfTwo>
void tile_pipeline(const Workload &workload, const EncodedState &input, EncodedState &output,
                   PipelineScratch &scratch) {
  constexpr std::size_t tile_entries = TileSize * action_count;
  std::array<float, tile_entries> values{};
  std::array<float, tile_entries> updated_regret{};
  std::array<float, tile_entries> updated_average{};
  const auto tile_count = (hand_count + TileSize - 1U) / TileSize;
  for (std::size_t tile = 0U; tile < tile_count; ++tile) {
    const auto begin = tile * TileSize;
    const auto end = std::min(begin + TileSize, hand_count);
    const auto count = end - begin;
    const float old_regret_scale = input.regret_scale[tile];
    const float old_average_scale = input.average_scale[tile];
    float maximum_regret = 0.0F;
    float maximum_average = 0.0F;
    for (std::size_t local = 0U; local < count; ++local) {
      const auto hand = begin + local;
      std::array<float, action_count> regrets{};
      std::array<float, action_count> strategy{};
      for (std::size_t action = 0U; action < action_count; ++action) {
        regrets[action] = static_cast<float>(static_cast<std::int16_t>(
                              input.regret[entry(action, hand)])) *
                          old_regret_scale;
      }
      regret_matching(regrets, strategy);
      float current = 0.0F;
      for (std::size_t action = 0U; action < action_count; ++action) {
        const auto local_entry = action * TileSize + local;
        values[local_entry] = produce_action_value(workload, action, hand);
        current = std::fma(strategy[action], values[local_entry], current);
      }
      scratch.parent[hand] = current;
      for (std::size_t action = 0U; action < action_count; ++action) {
        const auto index = entry(action, hand);
        const auto local_entry = action * TileSize + local;
        const float old_regret =
            static_cast<float>(static_cast<std::int16_t>(input.regret[index])) * old_regret_scale;
        const float old_average = static_cast<float>(input.average[index]) * old_average_scale;
        updated_regret[local_entry] =
            std::fma(old_regret, old_regret > 0.0F ? positive_discount : 0.0F,
                     values[local_entry] - current);
        updated_average[local_entry] =
            std::fma(strategy_weight * workload.reach[hand], strategy[action], old_average);
        maximum_regret = std::max(maximum_regret, std::abs(updated_regret[local_entry]));
        maximum_average = std::max(maximum_average, updated_average[local_entry]);
      }
    }
    const float regret_scale =
        PowerOfTwo ? power_of_two_scale(maximum_regret, regret_capacity)
                   : maximum_regret / regret_capacity;
    const float average_scale =
        PowerOfTwo ? power_of_two_scale(maximum_average, average_capacity)
                   : maximum_average / average_capacity;
    output.regret_scale[tile] = regret_scale;
    output.average_scale[tile] = average_scale;
    for (std::size_t action = 0U; action < action_count; ++action) {
      for (std::size_t local = 0U; local < count; ++local) {
        const auto index = entry(action, begin + local);
        const auto local_entry = action * TileSize + local;
        output.regret[index] = encode_regret(updated_regret[local_entry], regret_scale);
        output.average[index] = encode_average(updated_average[local_entry], average_scale);
      }
    }
  }
}

template <std::size_t TileSize>
[[nodiscard]] FloatState decode_tiles(const EncodedState &state) {
  FloatState decoded;
  for (std::size_t hand = 0U; hand < hand_count; ++hand) {
    const auto tile = hand / TileSize;
    for (std::size_t action = 0U; action < action_count; ++action) {
      const auto index = entry(action, hand);
      decoded.regret[index] =
          static_cast<float>(static_cast<std::int16_t>(state.regret[index])) *
          state.regret_scale[tile];
      decoded.average[index] = static_cast<float>(state.average[index]) * state.average_scale[tile];
    }
  }
  return decoded;
}

struct ErrorSummary {
  double mean{0.0};
  double p95{0.0};
  double p99{0.0};
  double maximum{0.0};
};

[[nodiscard]] ErrorSummary summarize(std::vector<double> values) {
  std::ranges::sort(values);
  const auto percentile = [&values](const double fraction) {
    const auto index = static_cast<std::size_t>(
        std::ceil(fraction * static_cast<double>(values.size())) - 1.0);
    return values[std::min(index, values.size() - 1U)];
  };
  return {std::accumulate(values.begin(), values.end(), 0.0) /
              static_cast<double>(values.size()),
          percentile(0.95), percentile(0.99), values.back()};
}

void print_decoded_metrics(const std::string_view name, const FloatState &decoded,
                           const FloatState &reference,
                           const std::vector<std::uint16_t> *const scaled_codes) {
  std::vector<double> regret_error;
  std::vector<double> average_error;
  std::vector<double> current_l1;
  std::vector<double> current_linf;
  std::vector<double> average_l1;
  std::vector<double> average_linf;
  regret_error.reserve(entry_count);
  average_error.reserve(entry_count);
  std::uint64_t sign_flips = 0U;
  std::uint64_t positive_changes = 0U;
  std::uint64_t saturation = 0U;
  std::uint64_t near_zero = 0U;
  double utilization_sum = 0.0;
  for (std::size_t index = 0U; index < entry_count; ++index) {
    regret_error.push_back(std::abs(static_cast<double>(decoded.regret[index]) -
                                    static_cast<double>(reference.regret[index])));
    average_error.push_back(std::abs(static_cast<double>(decoded.average[index]) -
                                     static_cast<double>(reference.average[index])));
    sign_flips += std::signbit(decoded.regret[index]) != std::signbit(reference.regret[index]) &&
                          decoded.regret[index] != 0.0F && reference.regret[index] != 0.0F
                      ? 1U
                      : 0U;
    positive_changes += (decoded.regret[index] > 0.0F) != (reference.regret[index] > 0.0F) ? 1U
                                                                                           : 0U;
    if (scaled_codes != nullptr) {
      const auto magnitude =
          std::abs(static_cast<int>(static_cast<std::int16_t>((*scaled_codes)[index])));
      saturation += magnitude == static_cast<int>(regret_capacity) ? 1U : 0U;
      near_zero += magnitude <= 1 ? 1U : 0U;
      utilization_sum += static_cast<double>(magnitude) / regret_capacity;
    }
  }
  for (std::size_t hand = 0U; hand < hand_count; ++hand) {
    std::array<float, action_count> reference_regrets{};
    std::array<float, action_count> candidate_regrets{};
    std::array<float, action_count> reference_policy{};
    std::array<float, action_count> candidate_policy{};
    float reference_average_sum = 0.0F;
    float candidate_average_sum = 0.0F;
    for (std::size_t action = 0U; action < action_count; ++action) {
      const auto index = entry(action, hand);
      reference_regrets[action] = reference.regret[index];
      candidate_regrets[action] = decoded.regret[index];
      reference_average_sum += reference.average[index];
      candidate_average_sum += decoded.average[index];
    }
    regret_matching(reference_regrets, reference_policy);
    regret_matching(candidate_regrets, candidate_policy);
    double hand_current_l1 = 0.0;
    double hand_current_linf = 0.0;
    double hand_average_l1 = 0.0;
    double hand_average_linf = 0.0;
    for (std::size_t action = 0U; action < action_count; ++action) {
      const auto index = entry(action, hand);
      const double current_delta =
          std::abs(static_cast<double>(candidate_policy[action] - reference_policy[action]));
      hand_current_l1 += current_delta;
      hand_current_linf = std::max(hand_current_linf, current_delta);
      const double reference_probability =
          reference_average_sum > 0.0F ? reference.average[index] / reference_average_sum
                                       : 1.0 / static_cast<double>(action_count);
      const double candidate_probability =
          candidate_average_sum > 0.0F ? decoded.average[index] / candidate_average_sum
                                       : 1.0 / static_cast<double>(action_count);
      const double average_delta = std::abs(candidate_probability - reference_probability);
      hand_average_l1 += average_delta;
      hand_average_linf = std::max(hand_average_linf, average_delta);
    }
    current_l1.push_back(hand_current_l1);
    current_linf.push_back(hand_current_linf);
    average_l1.push_back(hand_average_l1);
    average_linf.push_back(hand_average_linf);
  }
  const auto regret = summarize(std::move(regret_error));
  const auto average = summarize(std::move(average_error));
  const auto policy_l1 = summarize(std::move(current_l1));
  const auto policy_linf = summarize(std::move(current_linf));
  const auto avg_l1 = summarize(std::move(average_l1));
  const auto avg_linf = summarize(std::move(average_linf));
  std::cout << "candidate=" << name << " regret_abs_mean=" << regret.mean
            << " regret_abs_p95=" << regret.p95 << " regret_abs_p99=" << regret.p99
            << " regret_abs_max=" << regret.maximum << " average_abs_mean=" << average.mean
            << " average_abs_p95=" << average.p95 << " average_abs_p99=" << average.p99
            << " average_abs_max=" << average.maximum << " sign_flips=" << sign_flips
            << " positive_class_changes=" << positive_changes
            << " current_policy_l1_mean=" << policy_l1.mean
            << " current_policy_l1_p99=" << policy_l1.p99
            << " current_policy_linf_max=" << policy_linf.maximum
            << " average_policy_l1_mean=" << avg_l1.mean
            << " average_policy_l1_p99=" << avg_l1.p99
            << " average_policy_linf_max=" << avg_linf.maximum
            << " regret_code_mean_utilization="
            << (scaled_codes != nullptr ? utilization_sum / entry_count : 0.0)
            << " regret_near_zero_fraction="
            << (scaled_codes != nullptr ? static_cast<double>(near_zero) / entry_count : 0.0)
            << " regret_saturation_fraction="
            << (scaled_codes != nullptr ? static_cast<double>(saturation) / entry_count : 0.0)
            << '\n';
}

template <std::size_t TileSize>
void print_metrics(const std::string_view name, const EncodedState &candidate,
                   const FloatState &reference) {
  print_decoded_metrics(name, decode_tiles<TileSize>(candidate), reference, &candidate.regret);
}

[[nodiscard]] bool shared_scale_oracle() {
  const std::array<std::int16_t, action_count> codes{3, 9, -2};
  std::array<float, action_count> first{};
  std::array<float, action_count> second{};
  std::array<float, action_count> first_policy{};
  std::array<float, action_count> second_policy{};
  for (std::size_t action = 0U; action < action_count; ++action) {
    first[action] = static_cast<float>(codes[action]) * 0.25F;
    second[action] = static_cast<float>(codes[action]) * 64.0F;
  }
  regret_matching(first, first_policy);
  regret_matching(second, second_policy);
  if (first_policy != second_policy) {
    return false;
  }
  // Independent action scales do not cancel and must not be treated as equivalent.
  second[0] = static_cast<float>(codes[0]) * 0.25F;
  regret_matching(second, second_policy);
  return first_policy != second_policy;
}

Workload workload = make_workload();
EncodedState node_input = encode_node(workload.initial);
EncodedState node_output = encode_node(workload.initial);
EncodedState hand_input = encode_tiles<1U>(workload.initial, false);
EncodedState hand_output = encode_tiles<1U>(workload.initial, false);
EncodedState tile8_input = encode_tiles<8U>(workload.initial, false);
EncodedState tile8_output = encode_tiles<8U>(workload.initial, false);
EncodedState tile16_input = encode_tiles<16U>(workload.initial, false);
EncodedState tile16_output = encode_tiles<16U>(workload.initial, false);
EncodedState tile32_input = encode_tiles<32U>(workload.initial, false);
EncodedState tile32_output = encode_tiles<32U>(workload.initial, false);
EncodedState tile64_input = encode_tiles<64U>(workload.initial, false);
EncodedState tile64_output = encode_tiles<64U>(workload.initial, false);
EncodedState power2_input = encode_tiles<32U>(workload.initial, true);
EncodedState power2_output = encode_tiles<32U>(workload.initial, true);
BFloatState bfloat_input = encode_bfloat_state(workload.initial);
BFloatState bfloat_output = encode_bfloat_state(workload.initial);
SignedFloat24BFloatState float24_bfloat_input = encode_float24_bfloat_state(workload.initial);
SignedFloat24BFloatState float24_bfloat_output = encode_float24_bfloat_state(workload.initial);
PipelineScratch scratch;

void BM_LegacyWholeNode(benchmark::State &state) {
  for (auto _ : state) {
    legacy_pipeline(workload, node_input, node_output, scratch);
    benchmark::DoNotOptimize(node_output.regret.data());
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * entry_count * 44U));
}

template <std::size_t TileSize>
void BM_TileFused(benchmark::State &state) {
  auto *input = &tile32_input;
  auto *output = &tile32_output;
  if constexpr (TileSize == 1U) {
    input = &hand_input;
    output = &hand_output;
  } else if constexpr (TileSize == 8U) {
    input = &tile8_input;
    output = &tile8_output;
  } else if constexpr (TileSize == 16U) {
    input = &tile16_input;
    output = &tile16_output;
  } else if constexpr (TileSize == 64U) {
    input = &tile64_input;
    output = &tile64_output;
  }
  for (auto _ : state) {
    tile_pipeline<TileSize, false>(workload, *input, *output, scratch);
    benchmark::DoNotOptimize(output->regret.data());
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * entry_count * 24U));
}

void BM_TilePowerOfTwo32(benchmark::State &state) {
  for (auto _ : state) {
    tile_pipeline<32U, true>(workload, power2_input, power2_output, scratch);
    benchmark::DoNotOptimize(power2_output.regret.data());
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * entry_count * 24U));
}

void BM_DirectBFloat16(benchmark::State &state) {
  for (auto _ : state) {
    direct_bfloat_pipeline(workload, bfloat_input, bfloat_output, scratch);
    benchmark::DoNotOptimize(bfloat_output.regret.data());
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * entry_count * 20U));
}

void BM_DirectSignedFloat24BFloat16(benchmark::State &state) {
  for (auto _ : state) {
    direct_float24_bfloat_pipeline(workload, float24_bfloat_input, float24_bfloat_output, scratch);
    benchmark::DoNotOptimize(float24_bfloat_output.regret.data());
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * entry_count * 22U));
}

BENCHMARK(BM_LegacyWholeNode);
BENCHMARK_TEMPLATE(BM_TileFused, 1U);
BENCHMARK_TEMPLATE(BM_TileFused, 8U);
BENCHMARK_TEMPLATE(BM_TileFused, 16U);
BENCHMARK_TEMPLATE(BM_TileFused, 32U);
BENCHMARK_TEMPLATE(BM_TileFused, 64U);
BENCHMARK(BM_TilePowerOfTwo32);
BENCHMARK(BM_DirectBFloat16);
BENCHMARK(BM_DirectSignedFloat24BFloat16);

} // namespace

int main(int argc, char **argv) {
  if (!shared_scale_oracle()) {
    std::cerr << "TILE_STATE_ORACLE=FAIL\n";
    return EXIT_FAILURE;
  }
  PipelineScratch reference_scratch;
  reference_pipeline(workload, reference_scratch);
  tile_pipeline<1U, false>(workload, hand_input, hand_output, scratch);
  tile_pipeline<8U, false>(workload, tile8_input, tile8_output, scratch);
  tile_pipeline<16U, false>(workload, tile16_input, tile16_output, scratch);
  tile_pipeline<32U, false>(workload, tile32_input, tile32_output, scratch);
  tile_pipeline<64U, false>(workload, tile64_input, tile64_output, scratch);
  tile_pipeline<32U, true>(workload, power2_input, power2_output, scratch);
  direct_bfloat_pipeline(workload, bfloat_input, bfloat_output, scratch);
  direct_float24_bfloat_pipeline(workload, float24_bfloat_input, float24_bfloat_output, scratch);
  std::cout << std::setprecision(9) << "TILE_STATE_ORACLE=PASS hands=" << hand_count
            << " actions=" << action_count
            << " legacy_transient_bytes_per_entry=20 tile_transient_bytes_per_entry=0"
               " modeled_legacy_total_bytes_per_entry=44 modeled_tile_total_bytes_per_entry=24\n";
  print_metrics<1U>("per_hand_float_scale", hand_output, reference_scratch.updated);
  print_metrics<8U>("tile_float_k8", tile8_output, reference_scratch.updated);
  print_metrics<16U>("tile_float_k16", tile16_output, reference_scratch.updated);
  print_metrics<32U>("tile_float_k32", tile32_output, reference_scratch.updated);
  print_metrics<64U>("tile_float_k64", tile64_output, reference_scratch.updated);
  print_metrics<32U>("tile_pow2_k32", power2_output, reference_scratch.updated);
  print_decoded_metrics("direct_bfloat16", decode_bfloat_state(bfloat_output),
                        reference_scratch.updated, nullptr);
  print_decoded_metrics("direct_signed_float24_bfloat16",
                        decode_float24_bfloat_state(float24_bfloat_output),
                        reference_scratch.updated, nullptr);
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return EXIT_SUCCESS;
}
