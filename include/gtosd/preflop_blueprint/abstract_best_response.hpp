#pragma once

#include "gtosd/preflop_blueprint/best_response.hpp"
#include "gtosd/preflop_blueprint/trainer.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

// Exact best response inside a HistoryBucketRows abstraction. The fixed
// policy traversal first accumulates counterfactual action advantages over
// chance and opponent reach. Perfect recall then permits a reverse sequence
// dynamic program:
//
//   G(I) = max_a [d(I,a) + sum_{J: predecessor(J)=(I,a)} G(J)].
//
// Unlike the physical best response, the responder observes only the saved
// history row. The result separates optimization error inside that abstract
// game from information lost by its card abstraction.
namespace gtosd::preflop_blueprint {

struct AbstractBestResponseProgress {
  std::uint32_t flops_done{0U};
  std::uint32_t flops_total{0U};
  std::uint64_t boards_done{0U};
  double seconds{0.0};
};

struct AbstractBestResponseOptions {
  unsigned threads{1U};
  std::array<std::vector<std::uint16_t>, 2> hand_subsets{};
  std::function<void(const AbstractBestResponseProgress &)> progress;
};

struct AbstractBestResponseReport {
  std::uint32_t flops{0U};
  std::uint64_t boards{0U};
  std::array<double, 2> gain{};
  double max_gain{0.0};
  double seconds{0.0};
  std::uint64_t process_bytes{0U};
};

[[nodiscard]] Result<AbstractBestResponseReport, TrainerError>
evaluate_abstract_best_response(const CompiledGame &game, BucketPolicy policy,
                                const TrainerResources &resources,
                                const std::vector<FlopGroup> &groups,
                                const AbstractBestResponseOptions &options = {});

} // namespace gtosd::preflop_blueprint
