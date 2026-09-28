#include "gtosd/card_abstraction/bucket_tables.hpp"

#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "resource_file.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <new>
#include <numeric>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

namespace gtosd::card_abstraction {

// Builder attorney: the only path that assembles a table from clustered parts.
struct BucketTableBuilderAccess {
  static void fill(BucketTable &target, const BucketStreet street, const std::uint32_t rows,
                   const std::uint32_t width, const ClusteringParameters &parameters,
                   std::vector<std::uint16_t> buckets, std::vector<std::uint16_t> centroids,
                   const std::string &catalog, const std::string &features) {
    target.street_ = street;
    target.capacity_ = parameters.capacity;
    target.rows_ = rows;
    target.width_ = width;
    target.parameters_ = parameters;
    target.buckets_ = std::move(buckets);
    target.centroids_ = std::move(centroids);
    target.catalog_fingerprint_ = catalog;
    target.feature_fingerprint_ = features;
    target.finalize();
  }
};

namespace {

constexpr std::string_view resource_kind = "bucket_table";
using Clock = std::chrono::steady_clock;

enum class Metric : std::uint8_t { CdfL1, FixedL2 };

// Compact observation set: values are uint16 vectors of `width` coordinates,
// weights are board multiplicities, rows point back into the bucket table.
struct Observations {
  std::uint32_t width{0U};
  std::uint32_t maximum_value{0U};
  Metric metric{Metric::CdfL1};
  std::vector<std::uint16_t> values;
  std::vector<std::uint32_t> weights;
  std::vector<std::uint32_t> rows;

  [[nodiscard]] std::size_t size() const noexcept { return weights.size(); }
  [[nodiscard]] const std::uint16_t *at(const std::size_t index) const noexcept {
    return values.data() + index * width;
  }
};

std::uint64_t distance(const Metric metric, const std::uint16_t *a, const std::uint16_t *b,
                       const std::uint32_t width) noexcept {
  std::uint64_t total = 0U;
  if (metric == Metric::CdfL1) {
    for (std::uint32_t i = 0U; i < width; ++i) {
      total += a[i] > b[i] ? static_cast<std::uint64_t>(a[i] - b[i])
                           : static_cast<std::uint64_t>(b[i] - a[i]);
    }
  } else {
    for (std::uint32_t i = 0U; i < width; ++i) {
      const auto delta = static_cast<std::int64_t>(a[i]) - static_cast<std::int64_t>(b[i]);
      total += static_cast<std::uint64_t>(delta * delta);
    }
  }
  return total;
}

template <typename Body>
void parallel_chunks(const std::size_t count, const unsigned threads, Body &&body) {
  std::vector<std::thread> workers;
  workers.reserve(threads);
  for (unsigned worker = 0U; worker < threads; ++worker) {
    const auto first = count * worker / threads;
    const auto last = count * (worker + 1U) / threads;
    workers.emplace_back([first, last, worker, &body] { body(worker, first, last); });
  }
  for (auto &thread : workers) {
    thread.join();
  }
}

struct AssignmentResult {
  std::uint64_t inertia{0U};
  std::uint64_t changed{0U};
  std::uint64_t farthest_distance{0U};
  std::size_t farthest_index{0U};
};

// Per-cluster statistics accumulated in integers so that every thread count
// yields the same centroids.
struct ClusterStatistics {
  Metric metric{Metric::CdfL1};
  std::uint32_t width{0U};
  std::uint32_t values_per_coordinate{0U};
  std::uint16_t capacity{0U};
  std::vector<std::uint64_t> weight;             // capacity
  std::vector<std::uint64_t> value_histograms;   // L1: capacity * width * values_per_coordinate
  std::vector<std::uint64_t> sums;               // L2: capacity * width

  void reset(const Metric m, const std::uint32_t w, const std::uint32_t maximum_value,
             const std::uint16_t k) {
    metric = m;
    width = w;
    values_per_coordinate = maximum_value + 1U;
    capacity = k;
    weight.assign(k, 0U);
    if (metric == Metric::CdfL1) {
      value_histograms.assign(static_cast<std::size_t>(k) * width * values_per_coordinate, 0U);
    } else {
      sums.assign(static_cast<std::size_t>(k) * width, 0U);
    }
  }

  void add(const std::uint16_t cluster, const std::uint16_t *values, const std::uint32_t w) {
    weight[cluster] += w;
    if (metric == Metric::CdfL1) {
      auto *base = value_histograms.data() +
                   static_cast<std::size_t>(cluster) * width * values_per_coordinate;
      for (std::uint32_t i = 0U; i < width; ++i) {
        base[static_cast<std::size_t>(i) * values_per_coordinate + values[i]] += w;
      }
    } else {
      auto *base = sums.data() + static_cast<std::size_t>(cluster) * width;
      for (std::uint32_t i = 0U; i < width; ++i) {
        base[i] += static_cast<std::uint64_t>(values[i]) * w;
      }
    }
  }

  void merge(const ClusterStatistics &other) {
    for (std::size_t i = 0U; i < weight.size(); ++i) {
      weight[i] += other.weight[i];
    }
    for (std::size_t i = 0U; i < value_histograms.size(); ++i) {
      value_histograms[i] += other.value_histograms[i];
    }
    for (std::size_t i = 0U; i < sums.size(); ++i) {
      sums[i] += other.sums[i];
    }
  }

  // Returns the number of clusters that received no observation.
  std::uint32_t update_centroids(std::vector<std::uint16_t> &centroids) const {
    std::uint32_t empty = 0U;
    for (std::uint16_t cluster = 0U; cluster < capacity; ++cluster) {
      if (weight[cluster] == 0U) {
        ++empty;
        continue;
      }
      auto *centroid = centroids.data() + static_cast<std::size_t>(cluster) * width;
      if (metric == Metric::CdfL1) {
        const auto *base = value_histograms.data() +
                           static_cast<std::size_t>(cluster) * width * values_per_coordinate;
        const auto half = (weight[cluster] + 1U) / 2U;
        for (std::uint32_t i = 0U; i < width; ++i) {
          const auto *histogram = base + static_cast<std::size_t>(i) * values_per_coordinate;
          std::uint64_t cumulative = 0U;
          std::uint32_t value = 0U;
          while (value + 1U < values_per_coordinate) {
            cumulative += histogram[value];
            if (cumulative >= half) {
              break;
            }
            ++value;
          }
          centroid[i] = static_cast<std::uint16_t>(value);
        }
      } else {
        const auto *base = sums.data() + static_cast<std::size_t>(cluster) * width;
        for (std::uint32_t i = 0U; i < width; ++i) {
          centroid[i] = static_cast<std::uint16_t>((base[i] + weight[cluster] / 2U) /
                                                   weight[cluster]);
        }
      }
    }
    return empty;
  }
};

AssignmentResult assign(const Observations &observations,
                        const std::span<const std::size_t> indices,
                        const std::vector<std::uint16_t> &centroids, const std::uint16_t capacity,
                        std::vector<std::uint16_t> &assignment, ClusterStatistics &statistics,
                        const unsigned threads) {
  std::vector<AssignmentResult> partial(threads);
  std::vector<ClusterStatistics> partial_statistics(threads);
  for (auto &entry : partial_statistics) {
    entry.reset(observations.metric, observations.width, observations.maximum_value, capacity);
  }
  parallel_chunks(indices.size(), threads,
                  [&](const unsigned worker, const std::size_t first, const std::size_t last) {
                    auto &result = partial[worker];
                    auto &stats = partial_statistics[worker];
                    for (std::size_t position = first; position < last; ++position) {
                      const auto index = indices[position];
                      const auto *values = observations.at(index);
                      std::uint64_t best_distance = std::numeric_limits<std::uint64_t>::max();
                      std::uint16_t best = 0U;
                      for (std::uint16_t cluster = 0U; cluster < capacity; ++cluster) {
                        const auto d = distance(observations.metric, values,
                                                centroids.data() +
                                                    static_cast<std::size_t>(cluster) *
                                                        observations.width,
                                                observations.width);
                        if (d < best_distance) {
                          best_distance = d;
                          best = cluster;
                        }
                      }
                      if (assignment[index] != best) {
                        ++result.changed;
                        assignment[index] = best;
                      }
                      const auto weight = observations.weights[index];
                      result.inertia += best_distance * weight;
                      stats.add(best, values, weight);
                      if (best_distance > result.farthest_distance ||
                          (best_distance == result.farthest_distance &&
                           index < result.farthest_index)) {
                        result.farthest_distance = best_distance;
                        result.farthest_index = index;
                      }
                    }
                  });
  AssignmentResult total;
  total.farthest_index = std::numeric_limits<std::size_t>::max();
  statistics.reset(observations.metric, observations.width, observations.maximum_value, capacity);
  for (unsigned worker = 0U; worker < threads; ++worker) {
    total.inertia += partial[worker].inertia;
    total.changed += partial[worker].changed;
    if (partial[worker].farthest_distance > total.farthest_distance ||
        (partial[worker].farthest_distance == total.farthest_distance &&
         partial[worker].farthest_index < total.farthest_index)) {
      total.farthest_distance = partial[worker].farthest_distance;
      total.farthest_index = partial[worker].farthest_index;
    }
    statistics.merge(partial_statistics[worker]);
  }
  return total;
}

// k-means++ seeding on a subsample, weighted by observation weight and by the
// distance (L1) or squared distance (L2) to the nearest chosen centroid.
std::vector<std::uint16_t> seed_centroids(const Observations &observations,
                                          const std::span<const std::size_t> indices,
                                          const std::uint16_t capacity,
                                          DeterministicRandom &random) {
  std::vector<std::uint16_t> centroids(static_cast<std::size_t>(capacity) * observations.width);
  std::vector<double> nearest(indices.size(), std::numeric_limits<double>::infinity());
  std::vector<double> cumulative(indices.size());
  const auto pick = [&](const bool weighted_by_distance) {
    double total = 0.0;
    for (std::size_t position = 0U; position < indices.size(); ++position) {
      const auto weight = static_cast<double>(observations.weights[indices[position]]);
      total += weighted_by_distance ? weight * nearest[position] : weight;
      cumulative[position] = total;
    }
    if (!(total > 0.0)) {
      return static_cast<std::size_t>(0U);
    }
    const auto target = random.uniform_unit() * total;
    const auto found = std::upper_bound(cumulative.begin(), cumulative.end(), target);
    return static_cast<std::size_t>(std::min<std::ptrdiff_t>(
        std::distance(cumulative.begin(), found),
        static_cast<std::ptrdiff_t>(indices.size() - 1U)));
  };
  for (std::uint16_t cluster = 0U; cluster < capacity; ++cluster) {
    const auto position = pick(cluster > 0U);
    const auto *chosen = observations.at(indices[position]);
    std::copy_n(chosen, observations.width,
                centroids.begin() + static_cast<std::ptrdiff_t>(cluster) * observations.width);
    for (std::size_t other = 0U; other < indices.size(); ++other) {
      const auto d = static_cast<double>(
          distance(observations.metric, observations.at(indices[other]), chosen,
                   observations.width));
      nearest[other] = std::min(nearest[other], d);
    }
  }
  return centroids;
}

std::vector<std::size_t> systematic_sample(const std::size_t count, const std::uint32_t sample,
                                           DeterministicRandom &random) {
  std::vector<std::size_t> indices;
  if (sample == 0U || sample >= count) {
    indices.resize(count);
    std::iota(indices.begin(), indices.end(), static_cast<std::size_t>(0U));
    return indices;
  }
  const auto stride = count / sample;
  const auto offset = random.uniform_below(static_cast<std::uint32_t>(std::min<std::size_t>(
      stride, std::numeric_limits<std::uint32_t>::max())));
  indices.reserve(sample);
  for (std::size_t position = offset; position < count && indices.size() < sample;
       position += stride) {
    indices.push_back(position);
  }
  return indices;
}

double centroid_strength(const Metric metric, const std::uint16_t *centroid,
                         const std::uint32_t width, const std::uint32_t maximum_value) noexcept {
  if (metric == Metric::FixedL2) {
    return static_cast<double>(centroid[0]);
  }
  // Mean equity of the distribution described by cumulative counts.
  double mean = 0.0;
  std::uint32_t previous = 0U;
  for (std::uint32_t bin = 0U; bin < width; ++bin) {
    const auto mass = centroid[bin] >= previous ? centroid[bin] - previous : 0U;
    mean += static_cast<double>(mass) * (static_cast<double>(bin) + 0.5) / width;
    previous = std::max(previous, static_cast<std::uint32_t>(centroid[bin]));
  }
  return maximum_value == 0U ? mean : mean / maximum_value;
}

struct ClusteringOutput {
  std::vector<std::uint16_t> assignment;
  std::vector<std::uint16_t> centroids;
};

Result<ClusteringOutput, ResourceError> cluster(const Observations &observations,
                                                const ClusteringParameters &parameters,
                                                ClusteringDiagnostics &diagnostics) {
  using Output = Result<ClusteringOutput, ResourceError>;
  const auto started = Clock::now();
  if (parameters.capacity == 0U || parameters.capacity > maximum_bucket_capacity ||
      parameters.threads == 0U || parameters.threads > 64U || parameters.restarts == 0U ||
      observations.size() < parameters.capacity) {
    return Output::failure(ResourceError::InvalidInput);
  }
  try {
    const auto capacity = parameters.capacity;
    std::vector<std::size_t> all(observations.size());
    std::iota(all.begin(), all.end(), static_cast<std::size_t>(0U));

    diagnostics.observations = observations.size();
    diagnostics.total_weight =
        std::accumulate(observations.weights.begin(), observations.weights.end(), std::uint64_t{0});

    // Screening restarts on a systematic subsample.
    std::vector<std::uint16_t> best_centroids;
    std::uint64_t best_inertia = std::numeric_limits<std::uint64_t>::max();
    diagnostics.screening_inertia.clear();
    for (std::uint32_t restart = 0U; restart < parameters.restarts; ++restart) {
      DeterministicRandom random(parameters.partition_seed ^
                                 (0x9E37'79B9'7F4A'7C15ULL * (restart + 1U)));
      const auto subsample = systematic_sample(observations.size(), parameters.screening_sample,
                                               random);
      auto centroids = seed_centroids(observations, subsample, capacity, random);
      std::vector<std::uint16_t> assignment(observations.size(), no_bucket);
      ClusterStatistics statistics;
      std::uint64_t inertia = 0U;
      for (std::uint32_t iteration = 0U; iteration < parameters.screening_iterations; ++iteration) {
        const auto result =
            assign(observations, subsample, centroids, capacity, assignment, statistics,
                   parameters.threads);
        inertia = result.inertia;
        const auto empty = statistics.update_centroids(centroids);
        if (empty > 0U) {
          // Reseed empty clusters with the farthest subsample point.
          for (std::uint16_t c = 0U; c < capacity; ++c) {
            if (statistics.weight[c] == 0U) {
              std::copy_n(observations.at(result.farthest_index), observations.width,
                          centroids.begin() +
                              static_cast<std::ptrdiff_t>(c) * observations.width);
            }
          }
        }
        if (result.changed == 0U && iteration > 0U) {
          break;
        }
      }
      diagnostics.screening_inertia.push_back(static_cast<double>(inertia));
      if (inertia < best_inertia) {
        best_inertia = inertia;
        best_centroids = centroids;
        diagnostics.chosen_restart = restart;
      }
    }

    // Full refinement from the best screening centroids.
    std::vector<std::uint16_t> assignment(observations.size(), no_bucket);
    ClusterStatistics statistics;
    diagnostics.inertia_by_iteration.clear();
    diagnostics.empty_reseeds = 0U;
    diagnostics.iterations = 0U;
    for (std::uint32_t iteration = 0U; iteration < parameters.maximum_iterations; ++iteration) {
      const auto result = assign(observations, all, best_centroids, capacity, assignment,
                                 statistics, parameters.threads);
      diagnostics.inertia_by_iteration.push_back(static_cast<double>(result.inertia));
      ++diagnostics.iterations;
      if (result.changed == 0U && iteration > 0U) {
        break;
      }
      const auto empty = statistics.update_centroids(best_centroids);
      if (empty > 0U) {
        diagnostics.empty_reseeds += empty;
        for (std::uint16_t c = 0U; c < capacity; ++c) {
          if (statistics.weight[c] == 0U) {
            std::copy_n(observations.at(result.farthest_index), observations.width,
                        best_centroids.begin() +
                            static_cast<std::ptrdiff_t>(c) * observations.width);
          }
        }
      }
    }
    // Final consistent assignment against the final centroids.
    const auto final_result =
        assign(observations, all, best_centroids, capacity, assignment, statistics,
               parameters.threads);
    if (diagnostics.inertia_by_iteration.empty() ||
        diagnostics.inertia_by_iteration.back() != static_cast<double>(final_result.inertia)) {
      diagnostics.inertia_by_iteration.push_back(static_cast<double>(final_result.inertia));
    }

    // Relabel by increasing centroid strength (stable on ties by old id).
    std::vector<std::uint16_t> order(capacity);
    std::iota(order.begin(), order.end(), static_cast<std::uint16_t>(0U));
    std::stable_sort(order.begin(), order.end(), [&](const std::uint16_t a, const std::uint16_t b) {
      return centroid_strength(observations.metric,
                               best_centroids.data() + static_cast<std::size_t>(a) *
                                                           observations.width,
                               observations.width, observations.maximum_value) <
             centroid_strength(observations.metric,
                               best_centroids.data() + static_cast<std::size_t>(b) *
                                                           observations.width,
                               observations.width, observations.maximum_value);
    });
    std::vector<std::uint16_t> new_label(capacity);
    for (std::uint16_t position = 0U; position < capacity; ++position) {
      new_label[order[position]] = position;
    }
    ClusteringOutput output;
    output.centroids.resize(best_centroids.size());
    for (std::uint16_t old = 0U; old < capacity; ++old) {
      std::copy_n(best_centroids.begin() + static_cast<std::ptrdiff_t>(old) * observations.width,
                  observations.width,
                  output.centroids.begin() +
                      static_cast<std::ptrdiff_t>(new_label[old]) * observations.width);
    }
    output.assignment.resize(assignment.size());
    diagnostics.occupancy_weight.assign(capacity, 0U);
    diagnostics.occupancy_rows.assign(capacity, 0U);
    std::vector<double> distance_sum(capacity, 0.0);
    for (std::size_t index = 0U; index < assignment.size(); ++index) {
      const auto label = new_label[assignment[index]];
      output.assignment[index] = label;
      diagnostics.occupancy_weight[label] += observations.weights[index];
      ++diagnostics.occupancy_rows[label];
      distance_sum[label] +=
          static_cast<double>(observations.weights[index]) *
          static_cast<double>(distance(observations.metric, observations.at(index),
                                       output.centroids.data() +
                                           static_cast<std::size_t>(label) * observations.width,
                                       observations.width));
    }
    diagnostics.mean_distance.assign(capacity, 0.0);
    for (std::uint16_t c = 0U; c < capacity; ++c) {
      if (diagnostics.occupancy_weight[c] > 0U) {
        diagnostics.mean_distance[c] =
            distance_sum[c] / static_cast<double>(diagnostics.occupancy_weight[c]);
      }
    }
    diagnostics.seconds = std::chrono::duration<double>(Clock::now() - started).count();
    return Output::success(std::move(output));
  } catch (const std::bad_alloc &) {
    return Output::failure(ResourceError::MemoryFailure);
  } catch (const std::system_error &) {
    return Output::failure(ResourceError::InvalidInput);
  }
}

template <typename Counts>
void append_cdf_observation(Observations &observations, const Counts &counts,
                            const std::uint32_t weight, const std::uint32_t row) {
  std::uint32_t cumulative = 0U;
  for (std::size_t bin = 0U; bin < equity_histogram_bins; ++bin) {
    cumulative += counts[bin];
    observations.values.push_back(static_cast<std::uint16_t>(cumulative));
  }
  observations.weights.push_back(weight);
  observations.rows.push_back(row);
}

std::string make_fingerprint(const BucketStreet street, const ClusteringParameters &parameters,
                             const std::string &catalog, const std::string &features,
                             const std::vector<std::uint16_t> &centroids,
                             const std::vector<std::uint16_t> &buckets) {
  auto hash = detail::fnv1a_text("gtosd.card_abstraction.bucket_table.v1|");
  hash = detail::fnv1a_text(bucket_street_name(street), hash);
  hash = detail::fnv1a_text("|", hash);
  hash = detail::fnv1a_text(std::to_string(parameters.capacity) + "|" +
                                std::to_string(parameters.restarts) + "|" +
                                std::to_string(parameters.screening_iterations) + "|" +
                                std::to_string(parameters.maximum_iterations) + "|" +
                                std::to_string(parameters.screening_sample) + "|" +
                                detail::hex64(parameters.partition_seed) + "|",
                            hash);
  hash = detail::fnv1a_text(catalog, hash);
  hash = detail::fnv1a_text("|", hash);
  hash = detail::fnv1a_text(features, hash);
  std::vector<std::uint8_t> payload;
  detail::append_vector(payload, centroids);
  detail::append_vector(payload, buckets);
  hash = detail::fnv1a(payload, hash);
  return "fnv1a64:" + detail::hex64(hash);
}

Result<BucketTable, ResourceError> finish_table(const BucketStreet street, const std::uint32_t rows,
                                                const Observations &observations,
                                                const ClusteringParameters &parameters,
                                                const std::string &catalog_fingerprint,
                                                const std::string &feature_fingerprint,
                                                ClusteringDiagnostics *diagnostics) {
  using Built = Result<BucketTable, ResourceError>;
  ClusteringDiagnostics local;
  auto clustered = cluster(observations, parameters, local);
  if (!clustered) {
    return Built::failure(clustered.error());
  }
  if (diagnostics != nullptr) {
    *diagnostics = local;
  }
  BucketTable table;
  std::vector<std::uint16_t> buckets(static_cast<std::size_t>(rows) * combo_count, no_bucket);
  for (std::size_t index = 0U; index < observations.size(); ++index) {
    buckets[observations.rows[index]] = clustered.value().assignment[index];
  }
  BucketTableBuilderAccess::fill(table, street, rows, observations.width, parameters,
                                 std::move(buckets), std::move(clustered.value().centroids),
                                 catalog_fingerprint, feature_fingerprint);
  return Built::success(std::move(table));
}

} // namespace

void BucketTable::finalize() {
  fingerprint_ = make_fingerprint(street_, parameters_, catalog_fingerprint_, feature_fingerprint_,
                                  centroids_, buckets_);
}

Result<BucketTable, ResourceError> BucketTable::build_flop(const BoardCatalog &catalog,
                                                           const FlopFeatureTable &features,
                                                           const ClusteringParameters &parameters,
                                                           ClusteringDiagnostics *diagnostics) {
  if (features.catalog_fingerprint() != catalog.fingerprint()) {
    return Result<BucketTable, ResourceError>::failure(ResourceError::InvalidInput);
  }
  const auto &combos = combo_table();
  Observations observations;
  observations.width = equity_histogram_bins;
  observations.maximum_value = flop_runout_count;
  observations.metric = Metric::CdfL1;
  const auto rows = static_cast<std::uint32_t>(catalog.flops().size());
  observations.values.reserve(static_cast<std::size_t>(rows) * 528U * equity_histogram_bins);
  observations.weights.reserve(static_cast<std::size_t>(rows) * 528U);
  observations.rows.reserve(static_cast<std::size_t>(rows) * 528U);
  for (std::uint32_t flop_index = 0U; flop_index < rows; ++flop_index) {
    const auto &entry = catalog.flops()[flop_index];
    const std::uint64_t mask = entry.cards[0].mask() | entry.cards[1].mask() | entry.cards[2].mask();
    for (std::uint16_t combo = 0U; combo < combo_count; ++combo) {
      if ((combos.masks[combo] & mask) != 0U) {
        continue;
      }
      append_cdf_observation(observations, features.histogram(flop_index, combo),
                             entry.multiplicity, flop_index * combo_count + combo);
    }
  }
  return finish_table(BucketStreet::Flop, rows, observations, parameters, catalog.fingerprint(),
                      features.fingerprint(), diagnostics);
}

Result<BucketTable, ResourceError> BucketTable::build_turn(const BoardCatalog &catalog,
                                                           const TurnFeatureTable &features,
                                                           const ClusteringParameters &parameters,
                                                           ClusteringDiagnostics *diagnostics) {
  const auto &combos = combo_table();
  Observations observations;
  observations.width = equity_histogram_bins;
  observations.maximum_value = turn_runout_count;
  observations.metric = Metric::CdfL1;
  const auto rows = static_cast<std::uint32_t>(catalog.flop_turns().size());
  observations.values.reserve(static_cast<std::size_t>(rows) * 496U * equity_histogram_bins);
  observations.weights.reserve(static_cast<std::size_t>(rows) * 496U);
  observations.rows.reserve(static_cast<std::size_t>(rows) * 496U);
  for (std::uint32_t index = 0U; index < rows; ++index) {
    const auto &entry = catalog.flop_turns()[index];
    const std::uint64_t mask = entry.flop[0].mask() | entry.flop[1].mask() | entry.flop[2].mask() |
                               entry.turn.mask();
    for (std::uint16_t combo = 0U; combo < combo_count; ++combo) {
      if ((combos.masks[combo] & mask) != 0U) {
        continue;
      }
      append_cdf_observation(observations, features.histogram(index, combo), entry.multiplicity,
                             index * combo_count + combo);
    }
  }
  return finish_table(BucketStreet::Turn, rows, observations, parameters, catalog.fingerprint(),
                      features.fingerprint(), diagnostics);
}

Result<BucketTable, ResourceError> BucketTable::build_river(const BoardCatalog &catalog,
                                                            const RiverFeatureTable &features,
                                                            const ClusteringParameters &parameters,
                                                            ClusteringDiagnostics *diagnostics) {
  const auto &combos = combo_table();
  Observations observations;
  observations.width = river_feature_count;
  observations.maximum_value = equity_fixed_point_scale;
  observations.metric = Metric::FixedL2;
  const auto rows = static_cast<std::uint32_t>(catalog.river_boards().size());
  observations.values.reserve(static_cast<std::size_t>(rows) * 465U * river_feature_count);
  observations.weights.reserve(static_cast<std::size_t>(rows) * 465U);
  observations.rows.reserve(static_cast<std::size_t>(rows) * 465U);
  for (std::uint32_t index = 0U; index < rows; ++index) {
    const auto &entry = catalog.river_boards()[index];
    std::uint64_t mask = 0U;
    for (const auto card : entry.cards) {
      mask |= card.mask();
    }
    for (std::uint16_t combo = 0U; combo < combo_count; ++combo) {
      if ((combos.masks[combo] & mask) != 0U) {
        continue;
      }
      const auto values = features.features(index, combo);
      observations.values.insert(observations.values.end(), values.begin(), values.end());
      observations.weights.push_back(entry.multiplicity);
      observations.rows.push_back(index * combo_count + combo);
    }
  }
  return finish_table(BucketStreet::River, rows, observations, parameters, catalog.fingerprint(),
                      features.fingerprint(), diagnostics);
}

Result<BucketTable, ResourceError>
BucketTable::from_assignment(const BucketStreet street, const BoardCatalog &catalog,
                             const ClusteringParameters &parameters,
                             std::vector<std::uint16_t> buckets,
                             std::vector<std::uint16_t> centroids,
                             const std::string &recipe_fingerprint) {
  using Built = Result<BucketTable, ResourceError>;
  const auto catalog_rows = street == BucketStreet::Flop   ? catalog.flops().size()
                            : street == BucketStreet::Turn ? catalog.flop_turns().size()
                                                           : catalog.river_boards().size();
  const auto expected_rows = street == BucketStreet::Flop    ? canonical_flop_count
                             : street == BucketStreet::Turn ? canonical_flop_turn_count
                                                            : canonical_river_board_count;
  const auto width = street == BucketStreet::River ? river_feature_count : equity_histogram_bins;
  if (catalog_rows != expected_rows || parameters.capacity == 0U ||
      parameters.capacity > maximum_bucket_capacity ||
      buckets.size() != static_cast<std::size_t>(expected_rows) * combo_count ||
      centroids.size() != static_cast<std::size_t>(parameters.capacity) * width) {
    return Built::failure(ResourceError::InvalidInput);
  }
  for (const auto bucket : buckets) {
    if (bucket != no_bucket && bucket >= parameters.capacity) {
      return Built::failure(ResourceError::InvalidInput);
    }
  }
  BucketTable table;
  BucketTableBuilderAccess::fill(table, street, expected_rows, width, parameters,
                                 std::move(buckets), std::move(centroids), catalog.fingerprint(),
                                 recipe_fingerprint);
  return Built::success(std::move(table));
}

Result<bool, ResourceError> BucketTable::save(const std::filesystem::path &path) const {
  std::vector<std::uint8_t> payload;
  payload.reserve(payload_bytes() + 256U);
  payload.push_back(static_cast<std::uint8_t>(street_));
  detail::append_little(payload, capacity_);
  detail::append_little(payload, rows_);
  detail::append_little(payload, width_);
  detail::append_little(payload, parameters_.restarts);
  detail::append_little(payload, parameters_.screening_iterations);
  detail::append_little(payload, parameters_.maximum_iterations);
  detail::append_little(payload, parameters_.screening_sample);
  detail::append_little(payload, parameters_.partition_seed);
  detail::append_little(payload, static_cast<std::uint32_t>(catalog_fingerprint_.size()));
  for (const auto character : catalog_fingerprint_) {
    payload.push_back(static_cast<std::uint8_t>(character));
  }
  detail::append_little(payload, static_cast<std::uint32_t>(feature_fingerprint_.size()));
  for (const auto character : feature_fingerprint_) {
    payload.push_back(static_cast<std::uint8_t>(character));
  }
  detail::append_vector(payload, centroids_);
  detail::append_vector(payload, buckets_);
  return detail::write_resource(path, resource_kind, format_version, fingerprint_, payload);
}

Result<BucketTable, ResourceError> BucketTable::load(const std::filesystem::path &path) {
  using Loaded = Result<BucketTable, ResourceError>;
  const auto resource = detail::read_resource(path, resource_kind, format_version);
  if (!resource) {
    return Loaded::failure(resource.error());
  }
  const std::span<const std::uint8_t> bytes(resource.value().bytes);
  if (bytes.empty()) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  BucketTable table;
  std::size_t position = 0U;
  const auto street = bytes[position++];
  if (street > static_cast<std::uint8_t>(BucketStreet::River)) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  table.street_ = static_cast<BucketStreet>(street);
  std::uint32_t catalog_size = 0U;
  std::uint32_t feature_size = 0U;
  if (!detail::read_little(bytes, position, table.capacity_) ||
      !detail::read_little(bytes, position, table.rows_) ||
      !detail::read_little(bytes, position, table.width_) ||
      !detail::read_little(bytes, position, table.parameters_.restarts) ||
      !detail::read_little(bytes, position, table.parameters_.screening_iterations) ||
      !detail::read_little(bytes, position, table.parameters_.maximum_iterations) ||
      !detail::read_little(bytes, position, table.parameters_.screening_sample) ||
      !detail::read_little(bytes, position, table.parameters_.partition_seed) ||
      !detail::read_little(bytes, position, catalog_size) || position + catalog_size > bytes.size()) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  table.parameters_.capacity = table.capacity_;
  table.catalog_fingerprint_.assign(reinterpret_cast<const char *>(bytes.data() + position),
                                    catalog_size);
  position += catalog_size;
  if (!detail::read_little(bytes, position, feature_size) || position + feature_size > bytes.size()) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  table.feature_fingerprint_.assign(reinterpret_cast<const char *>(bytes.data() + position),
                                    feature_size);
  position += feature_size;
  const auto expected_rows = table.street_ == BucketStreet::Flop    ? canonical_flop_count
                             : table.street_ == BucketStreet::Turn ? canonical_flop_turn_count
                                                                   : canonical_river_board_count;
  const auto expected_width =
      table.street_ == BucketStreet::River ? river_feature_count : equity_histogram_bins;
  if (table.rows_ != expected_rows || table.width_ != expected_width || table.capacity_ == 0U ||
      table.capacity_ > maximum_bucket_capacity ||
      !detail::read_vector(bytes, position, table.centroids_,
                           static_cast<std::uint64_t>(table.capacity_) * table.width_) ||
      !detail::read_vector(bytes, position, table.buckets_,
                           static_cast<std::uint64_t>(table.rows_) * combo_count) ||
      position != bytes.size()) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  for (const auto bucket : table.buckets_) {
    if (bucket != no_bucket && bucket >= table.capacity_) {
      return Loaded::failure(ResourceError::IntegrityFailure);
    }
  }
  table.finalize();
  if (table.fingerprint_ != resource.value().fingerprint) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  return Loaded::success(std::move(table));
}

namespace {

Result<BucketLookup, CardError> lookup_with(const BucketTable &table,
                                             const Result<CanonicalLookup, CardError> &canonical,
                                             const std::array<CardId, 2> &hand,
                                             const std::uint64_t board_mask) {
  if (!canonical) {
    return Result<BucketLookup, CardError>::failure(canonical.error());
  }
  if (hand[0] == hand[1] || ((hand[0].mask() | hand[1].mask()) & board_mask) != 0U) {
    return Result<BucketLookup, CardError>::failure(CardError::DuplicateCard);
  }
  BucketLookup lookup;
  lookup.row_index = canonical.value().index;
  lookup.permutation = canonical.value().permutation;
  lookup.combo = combo_index(permute_card(hand[0], lookup.permutation),
                             permute_card(hand[1], lookup.permutation));
  lookup.bucket = table.bucket(lookup.row_index, lookup.combo);
  return Result<BucketLookup, CardError>::success(lookup);
}

} // namespace

Result<BucketLookup, CardError> lookup_flop_bucket(const BoardCatalog &catalog,
                                                   const BucketTable &table,
                                                   const std::array<CardId, 3> &flop,
                                                   const std::array<CardId, 2> &hand) {
  if (table.street() != BucketStreet::Flop) {
    return Result<BucketLookup, CardError>::failure(CardError::InvalidCard);
  }
  return lookup_with(table, catalog.lookup_flop(flop), hand,
                     flop[0].mask() | flop[1].mask() | flop[2].mask());
}

Result<BucketLookup, CardError> lookup_turn_bucket(const BoardCatalog &catalog,
                                                   const BucketTable &table,
                                                   const std::array<CardId, 3> &flop,
                                                   const CardId turn,
                                                   const std::array<CardId, 2> &hand) {
  if (table.street() != BucketStreet::Turn) {
    return Result<BucketLookup, CardError>::failure(CardError::InvalidCard);
  }
  return lookup_with(table, catalog.lookup_flop_turn(flop, turn), hand,
                     flop[0].mask() | flop[1].mask() | flop[2].mask() | turn.mask());
}

Result<BucketLookup, CardError> lookup_river_bucket(const BoardCatalog &catalog,
                                                    const BucketTable &table,
                                                    const std::array<CardId, 5> &board,
                                                    const std::array<CardId, 2> &hand) {
  if (table.street() != BucketStreet::River) {
    return Result<BucketLookup, CardError>::failure(CardError::InvalidCard);
  }
  std::uint64_t mask = 0U;
  for (const auto card : board) {
    mask |= card.mask();
  }
  return lookup_with(table, catalog.lookup_river_board(board), hand, mask);
}

const char *bucket_street_name(const BucketStreet street) noexcept {
  switch (street) {
  case BucketStreet::Flop:
    return "flop";
  case BucketStreet::Turn:
    return "turn";
  case BucketStreet::River:
    return "river";
  }
  return "unknown";
}

} // namespace gtosd::card_abstraction
