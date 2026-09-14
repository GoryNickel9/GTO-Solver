#pragma once

#include "gtosd/core/ranges.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gtosd {

struct PostflopRootCounterfactualValue {
  ComboId combo{0};
  double source_range_weight{0.0};
  double counterfactual_reach{0.0};
  double conditional_value_antes{0.0};
  bool positive_reach{false};
};

enum class PostflopRootValueMode : std::uint8_t { AverageStrategy, ExactBestResponse };

struct PostflopRootCounterfactualValues {
  std::string game_fingerprint;
  std::uint64_t blueprint_iterations{0};
  PostflopRootValueMode mode{PostflopRootValueMode::AverageStrategy};
  std::array<std::vector<PostflopRootCounterfactualValue>, 2> players;
  std::array<double, 2> recomposed_value_antes{0.0, 0.0};
  double maximum_recomposition_error_antes{0.0};
};

} // namespace gtosd
