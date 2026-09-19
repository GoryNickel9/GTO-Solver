#pragma once

// Research-only exact distance between uniform collections of conditional
// river-equity histograms. No clustering or production bucket change here.
#include "gtosd/card_abstraction/exact_features.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <numeric>
#include <span>
#include <stdexcept>
#include <vector>

namespace gtosd::research {
using EquityHistogram = std::array<std::uint16_t, card_abstraction::equity_histogram_bins>;
struct TemporalDistance {
  double marginal{0};
  double potential{0};
  std::int64_t assignment_cost{0};
};

inline std::int64_t histogram_distance(const EquityHistogram &a, const EquityHistogram &b) {
  std::int64_t ca = 0, cb = 0, cost = 0;
  for (std::size_t bin = 0; bin < a.size(); ++bin) {
    ca += a[bin];
    cb += b[bin];
    cost += ca > cb ? ca - cb : cb - ca;
  }
  return cost;
}

// Equal-mass transport between n future states reduces to minimum-cost
// assignment (an optimum of the doubly-stochastic polytope is a permutation).
// Ground cost is 1D EMD between each pair of conditional equity histograms.
// Hungarian primal-dual assignment uses integer costs and stable tie order.
inline TemporalDistance temporal_distance(std::span<const EquityHistogram> left,
                                          std::span<const EquityHistogram> right) {
  const auto n = left.size();
  if (n == 0 || n > 64 || right.size() != n) {
    throw std::invalid_argument("need equal nonempty collections of at most 64 histograms");
  }
  const auto count = std::accumulate(left.front().begin(), left.front().end(), std::uint32_t{0});
  if (count == 0) {
    throw std::invalid_argument("empty histogram");
  }
  for (const auto side : {left, right}) {
    for (const auto &histogram : side) {
      if (std::accumulate(histogram.begin(), histogram.end(), std::uint32_t{0}) != count) {
        throw std::invalid_argument("histograms need equal positive total count");
      }
    }
  }
  std::vector<std::int64_t> cost(n * n);
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = 0; j < n; ++j) {
      cost[i * n + j] = histogram_distance(left[i], right[j]);
    }
  }
  std::vector<std::int64_t> u(n + 1), v(n + 1);
  std::vector<std::size_t> p(n + 1), way(n + 1);
  constexpr auto infinity = std::numeric_limits<std::int64_t>::max() / 4;
  for (std::size_t i = 1; i <= n; ++i) {
    p[0] = i;
    std::size_t j0 = 0;
    std::vector<std::int64_t> minimum(n + 1, infinity);
    std::vector<bool> used(n + 1, false);
    do {
      used[j0] = true;
      const auto i0 = p[j0];
      auto delta = infinity;
      std::size_t j1 = 0;
      for (std::size_t j = 1; j <= n; ++j) {
        if (!used[j]) {
          const auto reduced = cost[(i0 - 1) * n + j - 1] - u[i0] - v[j];
          if (reduced < minimum[j]) {
            minimum[j] = reduced;
            way[j] = j0;
          }
          if (minimum[j] < delta) {
            delta = minimum[j];
            j1 = j;
          }
        }
      }
      for (std::size_t j = 0; j <= n; ++j) {
        if (used[j]) {
          u[p[j]] += delta;
          v[j] -= delta;
        } else {
          minimum[j] -= delta;
        }
      }
      j0 = j1;
    } while (p[j0] != 0);
    do {
      const auto j1 = way[j0];
      p[j0] = p[j1];
      j0 = j1;
    } while (j0 != 0);
  }
  std::int64_t assignment = 0;
  for (std::size_t j = 1; j <= n; ++j) {
    assignment += cost[(p[j] - 1) * n + j - 1];
  }
  if (assignment != -v[0]) {
    throw std::runtime_error("assignment primal/dual mismatch");
  }
  std::int64_t ca = 0, cb = 0, marginal = 0;
  for (std::size_t bin = 0; bin < card_abstraction::equity_histogram_bins; ++bin) {
    for (std::size_t i = 0; i < n; ++i) {
      ca += left[i][bin];
      cb += right[i][bin];
    }
    marginal += ca > cb ? ca - cb : cb - ca;
  }
  const double normalization = static_cast<double>(n) * count;
  return {static_cast<double>(marginal) / normalization,
          static_cast<double>(assignment) / normalization, assignment};
}
} // namespace gtosd::research
