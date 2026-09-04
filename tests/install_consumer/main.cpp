#include "gtosd/abstraction/card_abstraction.hpp"
#include "gtosd/gui_prototype/gui_prototype.hpp"
#include "gtosd/postflop/postflop_solver.hpp"
#include "gtosd/subgame/subgame_solver.hpp"
#include "gtosd/version.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

int main() {
  const auto key = gtosd::generate_storage_key();
  const bool nonzero =
      std::any_of(key.begin(), key.end(), [](const auto value) { return value != 0U; });
  const auto dpi = gtosd::make_gui_dpi_layout(150U);
  const auto postflop_ranges = gtosd::make_uniform_postflop_ranges();
  const std::vector<gtosd::CardAbstractionObservation> observations{
      {"consumer:combo=0", "consumer", 0U, 0U, 0U, 1.0, {0.5}}};
  const auto feature_cache =
      gtosd::build_card_abstraction_feature_cache(observations, "consumer-l2-v1", "consumer");
  bool valid_feature_cache = false;
  if (feature_cache) {
    const auto validated = gtosd::validate_card_abstraction_feature_cache(feature_cache.value());
    valid_feature_cache = validated && validated.value();
  }
  const bool solver_components =
      std::string_view(gtosd::card_abstraction_kind_name(
          gtosd::CardAbstractionKind::EquityFeatureKMeans)) == "equity_feature_kmeans" &&
      std::string_view(gtosd::subgame_safety_mode_name(
          gtosd::SubgameSafetyMode::ExactNashConvGuard)) == "exact_nash_conv_guard";
  return gtosd::api_version_minor == 11U && nonzero && solver_components && feature_cache &&
                 valid_feature_cache && postflop_ranges.players[0][0].basis_points() == 10'000U &&
                 dpi.has_value() && dpi.value().matrix_extent_pixels == 675U
             ? 0
             : 1;
}
