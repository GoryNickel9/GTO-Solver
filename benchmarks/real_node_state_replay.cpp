#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using Json = nlohmann::json;

struct Sample {
  std::uint64_t iteration{};
  std::uint64_t signature{};
  std::uint8_t street{};
  std::uint8_t action_count{};
  std::uint16_t hand_count{};
  double regret_weight{};
  double strategy_weight{};
  double positive_discount{};
  double negative_discount{};
  float old_regret_scale{};
  float old_strategy_scale{};
  float regret_scale{};
  float strategy_scale{};
  std::vector<std::string> producers;
  std::vector<std::uint16_t> old_regret;
  std::vector<std::uint16_t> old_strategy;
  std::vector<float> policy;
  std::vector<float> reach;
  std::vector<float> action_values;
  std::vector<float> current_values;
  std::vector<float> average_contribution;
  std::vector<float> result_regret;
  std::vector<float> result_strategy;
  std::vector<std::uint16_t> result_regret_codes;
  std::vector<std::uint16_t> result_strategy_codes;
  std::vector<float> parent_values;
};

template <typename T> std::vector<T> vector_field(const Json &value, const char *name) {
  return value.at(name).get<std::vector<T>>();
}

Sample parse_sample(const Json &value) {
  Sample sample;
  sample.iteration = value.at("iteration").get<std::uint64_t>();
  sample.signature = value.at("structural_signature").get<std::uint64_t>();
  sample.street = value.at("street").get<std::uint8_t>();
  sample.action_count = value.at("action_count").get<std::uint8_t>();
  sample.hand_count = value.at("hand_count").get<std::uint16_t>();
  sample.regret_weight = value.at("regret_update_weight").get<double>();
  sample.strategy_weight = value.at("strategy_weight").get<double>();
  sample.positive_discount = value.at("positive_regret_discount").get<double>();
  sample.negative_discount = value.at("negative_regret_discount").get<double>();
  sample.old_regret_scale = value.at("old_regret_scale").get<float>();
  sample.old_strategy_scale = value.at("old_strategy_scale").get<float>();
  sample.regret_scale = value.at("resulting_regret_scale").get<float>();
  sample.strategy_scale = value.at("resulting_strategy_scale").get<float>();
  sample.producers = vector_field<std::string>(value, "producers");
  sample.old_regret = vector_field<std::uint16_t>(value, "old_regret_codes");
  sample.old_strategy = vector_field<std::uint16_t>(value, "old_strategy_codes");
  sample.policy = vector_field<float>(value, "current_policy");
  sample.reach = vector_field<float>(value, "actor_reach");
  sample.action_values = vector_field<float>(value, "action_values");
  sample.current_values = vector_field<float>(value, "current_values");
  sample.average_contribution = vector_field<float>(value, "average_contribution");
  sample.result_regret = vector_field<float>(value, "resulting_regret_values");
  sample.result_strategy = vector_field<float>(value, "resulting_strategy_values");
  sample.result_regret_codes = vector_field<std::uint16_t>(value, "resulting_regret_codes");
  sample.result_strategy_codes = vector_field<std::uint16_t>(value, "resulting_strategy_codes");
  sample.parent_values = vector_field<float>(value, "parent_returned_values");
  return sample;
}

float quantize_float(const float value, const unsigned total_bits) noexcept {
  if (total_bits >= 32U || !std::isfinite(value) || value == 0.0F) {
    return value;
  }
  const unsigned fraction_bits = total_bits > 9U ? total_bits - 9U : 0U;
  const unsigned discarded = 23U - std::min(23U, fraction_bits);
  if (discarded == 0U) {
    return value;
  }
  std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
  const std::uint32_t mask = (std::uint32_t{1} << discarded) - 1U;
  const std::uint32_t halfway = std::uint32_t{1} << (discarded - 1U);
  const std::uint32_t retained_lsb = (bits >> discarded) & 1U;
  const std::uint32_t remainder = bits & mask;
  bits &= ~mask;
  if (remainder > halfway || (remainder == halfway && retained_lsb != 0U)) {
    bits += std::uint32_t{1} << discarded;
  }
  return std::bit_cast<float>(bits);
}

std::vector<float> policy_from_regret(const std::vector<float> &regret, const std::size_t actions,
                                      const std::size_t hands) {
  std::vector<float> policy(regret.size());
  for (std::size_t hand = 0U; hand < hands; ++hand) {
    float sum = 0.0F;
    for (std::size_t action = 0U; action < actions; ++action) {
      sum += std::max(0.0F, regret[action * hands + hand]);
    }
    for (std::size_t action = 0U; action < actions; ++action) {
      policy[action * hands + hand] =
          sum > 0.0F ? std::max(0.0F, regret[action * hands + hand]) / sum
                     : 1.0F / static_cast<float>(actions);
    }
  }
  return policy;
}

std::vector<float> normalized_average(const std::vector<float> &average,
                                      const std::size_t actions, const std::size_t hands) {
  std::vector<float> policy(average.size());
  for (std::size_t hand = 0U; hand < hands; ++hand) {
    float sum = 0.0F;
    for (std::size_t action = 0U; action < actions; ++action) {
      sum += std::max(0.0F, average[action * hands + hand]);
    }
    for (std::size_t action = 0U; action < actions; ++action) {
      policy[action * hands + hand] =
          sum > 0.0F ? std::max(0.0F, average[action * hands + hand]) / sum
                     : 1.0F / static_cast<float>(actions);
    }
  }
  return policy;
}

double percentile(std::vector<double> values, const double fraction) {
  if (values.empty()) {
    return 0.0;
  }
  std::ranges::sort(values);
  const auto index = static_cast<std::size_t>(
      std::llround(fraction * static_cast<double>(values.size() - 1U)));
  return values[index];
}

Json quantiles(const std::vector<double> &values) {
  return {{"p10", percentile(values, 0.10)}, {"p25", percentile(values, 0.25)},
          {"p50", percentile(values, 0.50)}, {"p75", percentile(values, 0.75)},
          {"p90", percentile(values, 0.90)}, {"p95", percentile(values, 0.95)},
          {"p99", percentile(values, 0.99)}};
}

std::uint16_t encode_signed(const float value, const float scale, const std::size_t local,
                            const std::size_t vectorized) {
  if (!(scale > 0.0F)) {
    return 0U;
  }
  const double inverse = 1.0 / static_cast<double>(scale);
  const double normalized = local < vectorized
                                ? static_cast<double>(value * static_cast<float>(inverse))
                                : static_cast<double>(value) * inverse;
  return static_cast<std::uint16_t>(static_cast<std::int16_t>(
      std::clamp(std::nearbyint(normalized), -32767.0, 32767.0)));
}

std::uint16_t encode_unsigned(const float value, const float scale, const std::size_t local,
                              const std::size_t vectorized) {
  if (!(scale > 0.0F)) {
    return 0U;
  }
  const double inverse = 1.0 / static_cast<double>(scale);
  const double normalized = local < vectorized
                                ? static_cast<double>(value * static_cast<float>(inverse))
                                : static_cast<double>(value) * inverse;
  return static_cast<std::uint16_t>(
      std::clamp(std::nearbyint(normalized), 0.0, 65535.0));
}

Json fidelity(const std::vector<Sample> &samples) {
  std::uint64_t regret_mismatch = 0U;
  std::uint64_t strategy_mismatch = 0U;
  std::uint64_t parent_mismatch = 0U;
  std::uint64_t entries = 0U;
  std::vector<double> regret_formula_error;
  std::vector<double> strategy_formula_error;
  for (const auto &sample : samples) {
    const auto hands = static_cast<std::size_t>(sample.hand_count);
    const auto actions = static_cast<std::size_t>(sample.action_count);
    const auto vectorized = hands - hands % 8U;
    entries += sample.result_regret.size();
    for (std::size_t action = 0U; action < actions; ++action) {
      for (std::size_t hand = 0U; hand < hands; ++hand) {
        const auto index = action * hands + hand;
        regret_mismatch += encode_signed(sample.result_regret[index], sample.regret_scale, hand,
                                         vectorized) != sample.result_regret_codes[index];
        strategy_mismatch += encode_unsigned(sample.result_strategy[index], sample.strategy_scale,
                                             hand, vectorized) !=
                             sample.result_strategy_codes[index];
        const double old_regret =
            static_cast<double>(static_cast<std::int16_t>(sample.old_regret[index])) *
            sample.old_regret_scale;
        const double calculated_regret =
            old_regret * (old_regret > 0.0 ? sample.positive_discount : sample.negative_discount) +
            sample.regret_weight *
                (static_cast<double>(sample.action_values[index]) - sample.current_values[hand]);
        const double old_strategy =
            static_cast<double>(sample.old_strategy[index]) * sample.old_strategy_scale;
        regret_formula_error.push_back(
            std::abs(calculated_regret - sample.result_regret[index]));
        strategy_formula_error.push_back(std::abs(old_strategy + sample.average_contribution[index] -
                                                  sample.result_strategy[index]));
      }
    }
    for (std::size_t hand = 0U; hand < hands; ++hand) {
      parent_mismatch += std::bit_cast<std::uint32_t>(sample.parent_values[hand]) !=
                         std::bit_cast<std::uint32_t>(sample.current_values[hand]);
    }
  }
  return {{"entries", entries},
          {"regret_code_mismatch", regret_mismatch},
          {"strategy_code_mismatch", strategy_mismatch},
          {"parent_value_bit_mismatch", parent_mismatch},
          {"regret_codes_bit_equal", regret_mismatch == 0U},
          {"strategy_codes_bit_equal", strategy_mismatch == 0U},
          {"parent_values_bit_equal", parent_mismatch == 0U},
          {"regret_formula_abs_error", quantiles(regret_formula_error)},
          {"strategy_formula_abs_error", quantiles(strategy_formula_error)},
          {"regret_formula_max", regret_formula_error.empty()
                                     ? 0.0
                                     : *std::ranges::max_element(regret_formula_error)},
          {"strategy_formula_max", strategy_formula_error.empty()
                                       ? 0.0
                                       : *std::ranges::max_element(strategy_formula_error)}};
}

Json characterize(const std::vector<Sample> &samples) {
  std::vector<double> hands;
  std::vector<double> entries;
  std::vector<double> regret_magnitude;
  std::vector<double> action_value_magnitude;
  std::map<unsigned, std::uint64_t> streets;
  std::map<unsigned, std::uint64_t> actions;
  std::map<std::uint64_t, std::uint64_t> iterations;
  std::map<std::string, std::uint64_t> producer_actions;
  std::map<std::string, std::uint64_t> producer_entries;
  for (const auto &sample : samples) {
    hands.push_back(sample.hand_count);
    entries.push_back(static_cast<double>(sample.hand_count) * sample.action_count);
    double max_regret = 0.0;
    for (const auto raw : sample.old_regret) {
      max_regret = std::max(max_regret,
                            std::abs(static_cast<double>(static_cast<std::int16_t>(raw)) *
                                     sample.old_regret_scale));
    }
    regret_magnitude.push_back(max_regret);
    double max_value = 0.0;
    for (const auto value : sample.action_values) {
      max_value = std::max(max_value, std::abs(static_cast<double>(value)));
    }
    action_value_magnitude.push_back(max_value);
    ++streets[sample.street];
    ++actions[sample.action_count];
    ++iterations[sample.iteration];
    for (const auto &producer : sample.producers) {
      ++producer_actions[producer];
      producer_entries[producer] += sample.hand_count;
    }
  }
  return {{"hand_count", quantiles(hands)},
          {"entries_per_update", quantiles(entries)},
          {"old_regret_max_abs", quantiles(regret_magnitude)},
          {"action_value_max_abs", quantiles(action_value_magnitude)},
          {"street_sample_counts", streets},
          {"action_count_sample_counts", actions},
          {"iteration_sample_counts", iterations},
          {"producer_action_counts", producer_actions},
          {"producer_entry_counts", producer_entries}};
}

Json evaluate_precision(const std::vector<Sample> &samples, const unsigned regret_bits,
                        const unsigned strategy_bits) {
  std::vector<double> regret_error;
  std::vector<double> policy_l1;
  std::vector<double> policy_linf;
  std::vector<double> average_l1;
  std::vector<double> average_linf;
  std::uint64_t sign_changes = 0U;
  std::uint64_t positive_changes = 0U;
  std::uint64_t argmax_changes = 0U;
  std::uint64_t zero_to_positive = 0U;
  std::uint64_t single_to_multi = 0U;
  for (const auto &sample : samples) {
    const auto hands = static_cast<std::size_t>(sample.hand_count);
    const auto actions = static_cast<std::size_t>(sample.action_count);
    std::vector<float> candidate_regret(sample.result_regret.size());
    std::vector<float> candidate_average(sample.result_strategy.size());
    for (std::size_t index = 0U; index < candidate_regret.size(); ++index) {
      candidate_regret[index] = quantize_float(sample.result_regret[index], regret_bits);
      candidate_average[index] = quantize_float(sample.result_strategy[index], strategy_bits);
      regret_error.push_back(std::abs(static_cast<double>(candidate_regret[index]) -
                                      sample.result_regret[index]));
      sign_changes += std::signbit(candidate_regret[index]) !=
                      std::signbit(sample.result_regret[index]);
      positive_changes += (candidate_regret[index] > 0.0F) !=
                          (sample.result_regret[index] > 0.0F);
    }
    const auto reference_policy = policy_from_regret(sample.result_regret, actions, hands);
    const auto candidate_policy = policy_from_regret(candidate_regret, actions, hands);
    const auto reference_average = normalized_average(sample.result_strategy, actions, hands);
    const auto candidate_average_policy = normalized_average(candidate_average, actions, hands);
    for (std::size_t hand = 0U; hand < hands; ++hand) {
      double l1 = 0.0;
      double linf = 0.0;
      double avg_l1 = 0.0;
      double avg_linf = 0.0;
      std::size_t ref_argmax = 0U;
      std::size_t candidate_argmax = 0U;
      std::size_t ref_positive = 0U;
      std::size_t candidate_positive = 0U;
      for (std::size_t action = 0U; action < actions; ++action) {
        const auto index = action * hands + hand;
        const double delta = std::abs(static_cast<double>(reference_policy[index]) -
                                      candidate_policy[index]);
        const double avg_delta = std::abs(static_cast<double>(reference_average[index]) -
                                          candidate_average_policy[index]);
        l1 += delta;
        linf = std::max(linf, delta);
        avg_l1 += avg_delta;
        avg_linf = std::max(avg_linf, avg_delta);
        ref_argmax = reference_policy[index] > reference_policy[ref_argmax * hands + hand]
                         ? action
                         : ref_argmax;
        candidate_argmax = candidate_policy[index] >
                                   candidate_policy[candidate_argmax * hands + hand]
                               ? action
                               : candidate_argmax;
        ref_positive += sample.result_regret[index] > 0.0F;
        candidate_positive += candidate_regret[index] > 0.0F;
      }
      policy_l1.push_back(l1);
      policy_linf.push_back(linf);
      average_l1.push_back(avg_l1);
      average_linf.push_back(avg_linf);
      argmax_changes += ref_argmax != candidate_argmax;
      zero_to_positive += ref_positive == 0U && candidate_positive > 0U;
      single_to_multi += ref_positive == 1U && candidate_positive > 1U;
    }
  }
  return {{"regret_bits", regret_bits},
          {"strategy_bits", strategy_bits},
          {"modeled_state_bytes_per_action",
           (regret_bits + 7U) / 8U + (strategy_bits + 7U) / 8U},
          {"regret_abs_error", quantiles(regret_error)},
          {"regret_max_error",
           regret_error.empty() ? 0.0 : *std::ranges::max_element(regret_error)},
          {"sign_changes", sign_changes},
          {"positive_class_changes", positive_changes},
          {"policy_l1", quantiles(policy_l1)},
          {"policy_linf", quantiles(policy_linf)},
          {"average_l1", quantiles(average_l1)},
          {"average_linf", quantiles(average_linf)},
          {"argmax_changes", argmax_changes},
          {"zero_positive_to_positive", zero_to_positive},
          {"single_positive_to_multi_positive", single_to_multi}};
}

template <typename Function> double benchmark_seconds(Function &&function,
                                                       const std::uint64_t repetitions) {
  const auto started = std::chrono::steady_clock::now();
  for (std::uint64_t repetition = 0U; repetition < repetitions; ++repetition) {
    function();
  }
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
}

Json pipeline_costs(const std::vector<Sample> &samples, const std::uint64_t repetitions) {
  volatile float sink = 0.0F;
  const auto decode = benchmark_seconds(
      [&] {
        for (const auto &sample : samples) {
          for (const auto raw : sample.old_regret) {
            sink = sink + static_cast<float>(static_cast<std::int16_t>(raw)) *
                              sample.old_regret_scale;
          }
        }
      },
      repetitions);
  const auto current = benchmark_seconds(
      [&] {
        for (const auto &sample : samples) {
          const auto hands = static_cast<std::size_t>(sample.hand_count);
          for (std::size_t hand = 0U; hand < hands; ++hand) {
            float value = 0.0F;
            for (std::size_t action = 0U; action < sample.action_count; ++action) {
              const auto index = action * hands + hand;
              value += sample.policy[index] * sample.action_values[index];
            }
            sink = sink + value;
          }
        }
      },
      repetitions);
  const auto regret = benchmark_seconds(
      [&] {
        for (const auto &sample : samples) {
          for (std::size_t index = 0U; index < sample.old_regret.size(); ++index) {
            const double old = static_cast<double>(static_cast<std::int16_t>(sample.old_regret[index])) *
                               sample.old_regret_scale;
            sink = sink + static_cast<float>(old * (old > 0.0 ? sample.positive_discount
                                                               : sample.negative_discount) +
                                             sample.regret_weight *
                                                 (sample.action_values[index] -
                                                  sample.current_values[index % sample.hand_count]));
          }
        }
      },
      repetitions);
  const auto average = benchmark_seconds(
      [&] {
        for (const auto &sample : samples) {
          for (std::size_t index = 0U; index < sample.old_strategy.size(); ++index) {
            sink = sink + static_cast<float>(sample.old_strategy[index]) * sample.old_strategy_scale +
                   sample.average_contribution[index];
          }
        }
      },
      repetitions);
  const auto encode = benchmark_seconds(
      [&] {
        for (const auto &sample : samples) {
          const auto vectorized = sample.hand_count - sample.hand_count % 8U;
          for (std::size_t index = 0U; index < sample.result_regret.size(); ++index) {
            sink = sink + static_cast<float>(encode_signed(sample.result_regret[index],
                                                            sample.regret_scale,
                                                            index % sample.hand_count, vectorized));
            sink = sink + static_cast<float>(encode_unsigned(sample.result_strategy[index],
                                                              sample.strategy_scale,
                                                              index % sample.hand_count, vectorized));
          }
        }
      },
      repetitions);
  const auto total = decode + current + regret + average + encode;
  return {{"repetitions", repetitions},
          {"policy_decode_seconds", decode},
          {"current_value_reduction_seconds", current},
          {"regret_update_seconds", regret},
          {"average_update_seconds", average},
          {"encode_store_seconds", encode},
          {"replay_pipeline_seconds", total},
          {"sink", sink}};
}

Json throughput_frontier(const std::vector<Sample> &samples, const std::uint64_t repetitions) {
  std::vector<std::vector<float>> regret_scratch;
  std::vector<std::vector<float>> average_scratch;
  regret_scratch.reserve(samples.size());
  average_scratch.reserve(samples.size());
  for (const auto &sample : samples) {
    regret_scratch.emplace_back(sample.old_regret.size());
    average_scratch.emplace_back(sample.old_strategy.size());
  }
  volatile float sink = 0.0F;
  const auto scaled_seconds = benchmark_seconds(
      [&] {
        for (std::size_t sample_index = 0U; sample_index < samples.size(); ++sample_index) {
          const auto &sample = samples[sample_index];
          auto &regret = regret_scratch[sample_index];
          auto &average = average_scratch[sample_index];
          const auto hands = static_cast<std::size_t>(sample.hand_count);
          const auto actions = static_cast<std::size_t>(sample.action_count);
          float max_regret = 0.0F;
          float max_average = 0.0F;
          for (std::size_t hand = 0U; hand < hands; ++hand) {
            std::array<float, 8U> policies{};
            float positive_sum = 0.0F;
            for (std::size_t action = 0U; action < actions; ++action) {
              positive_sum += std::max(
                  0.0F, static_cast<float>(static_cast<std::int16_t>(
                            sample.old_regret[action * hands + hand])));
            }
            float current = 0.0F;
            for (std::size_t action = 0U; action < actions; ++action) {
              const auto index = action * hands + hand;
              const float positive = std::max(
                  0.0F, static_cast<float>(static_cast<std::int16_t>(sample.old_regret[index])));
              policies[action] = positive_sum > 0.0F
                                     ? positive / positive_sum
                                     : 1.0F / static_cast<float>(actions);
              current += policies[action] * sample.action_values[index];
            }
            for (std::size_t action = 0U; action < actions; ++action) {
              const auto index = action * hands + hand;
              const float old = static_cast<float>(static_cast<std::int16_t>(
                                    sample.old_regret[index])) *
                                sample.old_regret_scale;
              regret[index] = static_cast<float>(
                  old * (old > 0.0F ? sample.positive_discount : sample.negative_discount) +
                  sample.regret_weight * (sample.action_values[index] - current));
              average[index] = static_cast<float>(sample.old_strategy[index]) *
                               sample.old_strategy_scale +
                               static_cast<float>(sample.strategy_weight) * sample.reach[hand] *
                                   policies[action];
              max_regret = std::max(max_regret, std::abs(regret[index]));
              max_average = std::max(max_average, average[index]);
            }
          }
          const float regret_scale = max_regret > 0.0F ? max_regret / 32767.0F : 0.0F;
          const float strategy_scale = max_average > 0.0F ? max_average / 65535.0F : 0.0F;
          const auto vectorized = hands - hands % 8U;
          for (std::size_t index = 0U; index < regret.size(); ++index) {
            sink = sink + static_cast<float>(encode_signed(regret[index], regret_scale,
                                                            index % hands, vectorized));
            sink = sink + static_cast<float>(encode_unsigned(average[index], strategy_scale,
                                                              index % hands, vectorized));
          }
        }
      },
      repetitions);
  constexpr std::array<std::pair<unsigned, unsigned>, 10U> candidates{
      std::pair{16U, 16U}, std::pair{18U, 18U}, std::pair{20U, 16U}, std::pair{20U, 20U},
      std::pair{22U, 16U}, std::pair{22U, 22U}, std::pair{24U, 16U}, std::pair{24U, 24U},
      std::pair{32U, 16U}, std::pair{32U, 32U}};
  Json rows = Json::array();
  for (const auto [regret_bits, strategy_bits] : candidates) {
    const auto direct_seconds = benchmark_seconds(
        [&] {
          for (const auto &sample : samples) {
            const auto hands = static_cast<std::size_t>(sample.hand_count);
            const auto actions = static_cast<std::size_t>(sample.action_count);
            for (std::size_t hand = 0U; hand < hands; ++hand) {
              std::array<float, 8U> old_regret{};
              std::array<float, 8U> policy{};
              float positive_sum = 0.0F;
              for (std::size_t action = 0U; action < actions; ++action) {
                const auto index = action * hands + hand;
                old_regret[action] = quantize_float(
                    static_cast<float>(static_cast<std::int16_t>(sample.old_regret[index])) *
                        sample.old_regret_scale,
                    regret_bits);
                positive_sum += std::max(0.0F, old_regret[action]);
              }
              float current = 0.0F;
              for (std::size_t action = 0U; action < actions; ++action) {
                const auto index = action * hands + hand;
                policy[action] = positive_sum > 0.0F
                                     ? std::max(0.0F, old_regret[action]) / positive_sum
                                     : 1.0F / static_cast<float>(actions);
                current += policy[action] * sample.action_values[index];
              }
              for (std::size_t action = 0U; action < actions; ++action) {
                const auto index = action * hands + hand;
                const float updated = quantize_float(
                    static_cast<float>(old_regret[action] *
                                           (old_regret[action] > 0.0F
                                                ? sample.positive_discount
                                                : sample.negative_discount) +
                                       sample.regret_weight *
                                           (sample.action_values[index] - current)),
                    regret_bits);
                const float old_average = quantize_float(
                    static_cast<float>(sample.old_strategy[index]) * sample.old_strategy_scale,
                    strategy_bits);
                const float updated_average = quantize_float(
                    old_average + static_cast<float>(sample.strategy_weight) *
                                      sample.reach[hand] * policy[action],
                    strategy_bits);
                sink = sink + updated + updated_average;
              }
            }
          }
        },
        repetitions);
    rows.push_back({{"regret_bits", regret_bits},
                    {"strategy_bits", strategy_bits},
                    {"modeled_state_bytes_per_action",
                     (regret_bits + 7U) / 8U + (strategy_bits + 7U) / 8U},
                    {"scaled_current_seconds", scaled_seconds},
                    {"direct_seconds", direct_seconds},
                    {"replay_pipeline_speedup",
                     direct_seconds > 0.0 ? scaled_seconds / direct_seconds : 0.0}});
  }
  return {{"repetitions", repetitions}, {"rows", std::move(rows)}, {"sink", sink}};
}

Json packing_audit(const std::vector<Sample> &samples, const std::uint64_t repetitions) {
  std::vector<float> values;
  for (const auto &sample : samples) {
    values.insert(values.end(), sample.result_regret.begin(), sample.result_regret.end());
  }
  std::vector<std::uint32_t> aligned(values.size());
  std::vector<std::uint8_t> packed(values.size() * 3U);
  volatile std::uint32_t sink = 0U;
  const auto aligned_seconds = benchmark_seconds(
      [&] {
        for (std::size_t index = 0U; index < values.size(); ++index) {
          aligned[index] = std::bit_cast<std::uint32_t>(values[index]);
          sink = sink ^ aligned[index];
        }
      },
      repetitions);
  const auto packed_seconds = benchmark_seconds(
      [&] {
        for (std::size_t index = 0U; index < values.size(); ++index) {
          const auto bits = std::bit_cast<std::uint32_t>(values[index]);
          packed[index * 3U] = static_cast<std::uint8_t>(bits >> 8U);
          packed[index * 3U + 1U] = static_cast<std::uint8_t>(bits >> 16U);
          packed[index * 3U + 2U] = static_cast<std::uint8_t>(bits >> 24U);
          sink = sink ^ (static_cast<std::uint32_t>(packed[index * 3U]) |
                         (static_cast<std::uint32_t>(packed[index * 3U + 1U]) << 8U) |
                         (static_cast<std::uint32_t>(packed[index * 3U + 2U]) << 16U));
        }
      },
      repetitions);
  return {{"entries", values.size()},
          {"repetitions", repetitions},
          {"aligned_32_seconds", aligned_seconds},
          {"packed_24_seconds", packed_seconds},
          {"packed_over_aligned", aligned_seconds > 0.0 ? packed_seconds / aligned_seconds : 0.0},
          {"sink", sink}};
}

Json producer_streaming(const std::vector<Sample> &samples, const std::uint64_t repetitions) {
  struct Values {
    std::vector<float> entries;
    std::uint64_t actions{};
  };
  std::map<std::string, Values> by_producer;
  for (const auto &sample : samples) {
    const auto hands = static_cast<std::size_t>(sample.hand_count);
    for (std::size_t action = 0U; action < sample.action_count; ++action) {
      auto &group = by_producer[sample.producers[action]];
      ++group.actions;
      group.entries.insert(group.entries.end(), sample.action_values.begin() + action * hands,
                           sample.action_values.begin() + (action + 1U) * hands);
    }
  }
  Json rows = Json::array();
  for (auto &[name, group] : by_producer) {
    std::vector<float> materialized(group.entries.size());
    volatile float sink = 0.0F;
    const auto whole = benchmark_seconds(
        [&] {
          std::ranges::copy(group.entries, materialized.begin());
          for (const auto value : materialized) {
            sink = sink + value;
          }
        },
        repetitions);
    const auto stream = benchmark_seconds(
        [&] {
          constexpr std::size_t chunk = 32U;
          for (std::size_t begin = 0U; begin < group.entries.size(); begin += chunk) {
            const auto end = std::min(begin + chunk, group.entries.size());
            for (std::size_t index = begin; index < end; ++index) {
              sink = sink + group.entries[index];
            }
          }
        },
        repetitions);
    rows.push_back({{"producer", name},
                    {"actions", group.actions},
                    {"entries", group.entries.size()},
                    {"whole_vector_seconds", whole},
                    {"streaming_control_seconds", stream},
                    {"ideal_speedup", stream > 0.0 ? whole / stream : 0.0},
                    {"eliminable_bytes", group.entries.size() * sizeof(float) * 2U},
                    {"sink", sink}});
  }
  return rows;
}

Json multi_step(const std::vector<Sample> &samples, const unsigned regret_bits,
                const unsigned strategy_bits) {
  std::map<std::uint64_t, std::vector<const Sample *>> groups;
  for (const auto &sample : samples) {
    groups[sample.signature].push_back(&sample);
  }
  std::vector<double> policy_l1;
  std::vector<double> average_l1;
  std::uint64_t sequences = 0U;
  std::uint64_t steps = 0U;
  for (auto &[signature, sequence] : groups) {
    static_cast<void>(signature);
    std::ranges::sort(sequence, {}, [](const Sample *sample) { return sample->iteration; });
    if (sequence.size() < 2U) {
      continue;
    }
    ++sequences;
    const auto hands = static_cast<std::size_t>(sequence.front()->hand_count);
    const auto actions = static_cast<std::size_t>(sequence.front()->action_count);
    std::vector<float> reference(sequence.front()->old_regret.size());
    std::vector<float> candidate(reference.size());
    std::vector<float> reference_average(sequence.front()->old_strategy.size());
    std::vector<float> candidate_average(reference_average.size());
    for (std::size_t index = 0U; index < reference.size(); ++index) {
      reference[index] = static_cast<float>(static_cast<std::int16_t>(
                             sequence.front()->old_regret[index])) *
                         sequence.front()->old_regret_scale;
      candidate[index] = quantize_float(reference[index], regret_bits);
      reference_average[index] = static_cast<float>(sequence.front()->old_strategy[index]) *
                                 sequence.front()->old_strategy_scale;
      candidate_average[index] = quantize_float(reference_average[index], strategy_bits);
    }
    constexpr std::size_t cycles = 8U;
    for (std::size_t cycle = 0U; cycle < cycles; ++cycle) {
      for (const auto *sample : sequence) {
      if (sample->hand_count != hands || sample->action_count != actions) {
        continue;
      }
      ++steps;
      const auto reference_policy = policy_from_regret(reference, actions, hands);
      const auto candidate_policy = policy_from_regret(candidate, actions, hands);
      const auto reference_average_policy =
          normalized_average(reference_average, actions, hands);
      const auto candidate_average_policy =
          normalized_average(candidate_average, actions, hands);
      for (std::size_t hand = 0U; hand < hands; ++hand) {
        float ref_current = 0.0F;
        float candidate_current = 0.0F;
        double l1 = 0.0;
        double average_delta = 0.0;
        for (std::size_t action = 0U; action < actions; ++action) {
          const auto index = action * hands + hand;
          ref_current += reference_policy[index] * sample->action_values[index];
          candidate_current += candidate_policy[index] * sample->action_values[index];
          l1 += std::abs(static_cast<double>(reference_policy[index]) - candidate_policy[index]);
          average_delta += std::abs(static_cast<double>(reference_average_policy[index]) -
                                    candidate_average_policy[index]);
        }
        policy_l1.push_back(l1);
        average_l1.push_back(average_delta);
        for (std::size_t action = 0U; action < actions; ++action) {
          const auto index = action * hands + hand;
          const auto ref_discount = reference[index] > 0.0F ? sample->positive_discount
                                                            : sample->negative_discount;
          const auto candidate_discount = candidate[index] > 0.0F ? sample->positive_discount
                                                                   : sample->negative_discount;
          reference[index] = static_cast<float>(reference[index] * ref_discount +
                                                sample->regret_weight *
                                                    (sample->action_values[index] - ref_current));
          candidate[index] = quantize_float(
              static_cast<float>(candidate[index] * candidate_discount +
                                 sample->regret_weight *
                                     (sample->action_values[index] - candidate_current)),
              regret_bits);
          reference_average[index] += static_cast<float>(sample->strategy_weight) *
                                      sample->reach[hand] * reference_policy[index];
          candidate_average[index] = quantize_float(
              candidate_average[index] + static_cast<float>(sample->strategy_weight) *
                                             sample->reach[hand] * candidate_policy[index],
              strategy_bits);
          }
        }
      }
      }
    }
  return {{"regret_bits", regret_bits},
          {"strategy_bits", strategy_bits},
          {"compatible_sequences", sequences},
          {"steps", steps},
          {"cycles", 8U},
          {"policy_l1", quantiles(policy_l1)},
          {"policy_l1_max", policy_l1.empty() ? 0.0 : *std::ranges::max_element(policy_l1)},
          {"average_l1", quantiles(average_l1)},
          {"average_l1_max", average_l1.empty() ? 0.0 : *std::ranges::max_element(average_l1)}};
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 5 || std::string{argv[1]} != "--corpus" || std::string{argv[3]} != "--output") {
    std::cerr << "usage: gtosd_real_node_state_replay --corpus FILE --output FILE\n";
    return 2;
  }
  try {
    std::ifstream input(argv[2], std::ios::binary);
    Json corpus;
    input >> corpus;
    if (!input || corpus.value("schema", std::string{}) != "gtosd.real_node_replay.v1") {
      std::cerr << "invalid replay corpus\n";
      return 2;
    }
    std::vector<Sample> samples;
    for (const auto &value : corpus.at("samples")) {
      samples.push_back(parse_sample(value));
    }
    if (samples.empty()) {
      std::cerr << "empty replay corpus\n";
      return 2;
    }
    constexpr std::array<unsigned, 6U> precisions{16U, 18U, 20U, 22U, 24U, 32U};
    Json frontier = Json::array();
    for (const auto regret_bits : precisions) {
      for (const auto strategy_bits : precisions) {
        frontier.push_back(evaluate_precision(samples, regret_bits, strategy_bits));
      }
    }
    constexpr std::uint64_t repetitions = 7U;
    Json output{{"schema", "gtosd.real_node_state_replay_analysis.v1"},
                {"source_corpus", argv[2]},
                {"sample_count", samples.size()},
                {"characterization", characterize(samples)},
                {"replay_fidelity", fidelity(samples)},
                {"precision_frontier", std::move(frontier)},
                {"multi_step_controls",
                 Json::array({multi_step(samples, 16U, 16U), multi_step(samples, 18U, 18U),
                              multi_step(samples, 20U, 16U), multi_step(samples, 20U, 20U),
                              multi_step(samples, 22U, 16U), multi_step(samples, 22U, 22U),
                              multi_step(samples, 24U, 16U), multi_step(samples, 24U, 24U),
                              multi_step(samples, 32U, 16U), multi_step(samples, 32U, 32U)})},
                {"pipeline_cost", pipeline_costs(samples, repetitions)},
                {"throughput_frontier", throughput_frontier(samples, repetitions)},
                {"packing_alignment", packing_audit(samples, repetitions)},
                {"producer_streaming", producer_streaming(samples, repetitions)}};
    std::ofstream destination(argv[4], std::ios::binary | std::ios::trunc);
    destination << output.dump(2) << '\n';
    if (!destination) {
      std::cerr << "cannot write replay analysis\n";
      return 2;
    }
    std::cout << "real_node_replay samples=" << samples.size()
              << " fidelity_regret="
              << output["replay_fidelity"]["regret_codes_bit_equal"]
              << " fidelity_strategy="
              << output["replay_fidelity"]["strategy_codes_bit_equal"] << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "real-node replay failure: " << error.what() << '\n';
    return 1;
  }
}
