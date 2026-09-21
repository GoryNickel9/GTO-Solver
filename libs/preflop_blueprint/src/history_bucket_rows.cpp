#include "gtosd/preflop_blueprint/history_bucket_rows.hpp"
#include "binary_io.hpp"
#include "hashing.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <set>

namespace gtosd::preflop_blueprint {
namespace ca = card_abstraction;
namespace {
constexpr std::uint32_t maximum_lloyd_iterations = 25;
constexpr std::uint64_t maximum_file_bytes = 256ULL * 1024 * 1024;
constexpr std::uint32_t maximum_support = 10'000'000;

double distance(const double *left, const double *right, const std::size_t width,
                const bool squared_l2 = true) {
  double sum = 0;
  for (std::size_t d = 0; d < width; ++d) {
    const auto delta = left[d] - right[d];
    sum += squared_l2 ? delta * delta : std::abs(delta);
  }
  return sum;
}

bool cluster_children(const ca::BucketTable &table,
                      const std::vector<HistoryObservation> &observations,
                      const std::uint32_t maximum_children,
                      std::vector<std::uint64_t> &keys,
                      std::vector<std::uint32_t> &rows, std::uint32_t &row_count,
                      HistoryClusteringReport &stats) {
  const auto width = static_cast<std::size_t>(table.centroid_width());
  const bool squared_l2 = table.street() == ca::BucketStreet::River;
  if (maximum_children == 0 || maximum_children > table.capacity() || width == 0)
    return false;
  stats.support = observations.size();
  keys.reserve(observations.size());
  rows.reserve(observations.size());
  for (std::size_t begin = 0; begin < observations.size();) {
    const auto parent = observations[begin].key / table.capacity();
    auto end = begin + 1;
    while (end < observations.size() && observations[end].key / table.capacity() == parent)
      ++end;
    const auto n = end - begin;
    const auto k = std::min<std::size_t>(maximum_children, n);
    std::vector<double> points(n * width), centers(k * width);
    for (std::size_t i = 0; i < n; ++i) {
      const auto bucket = observations[begin + i].key % table.capacity();
      for (std::size_t d = 0; d < width; ++d)
        points[i * width + d] = table.centroids()[bucket * width + d];
    }
    std::vector<std::uint8_t> selected(n, 0);
    std::vector<double> nearest(n, std::numeric_limits<double>::infinity());
    for (std::size_t c = 0; c < k; ++c) {
      std::size_t best = 0;
      double score = -1;
      for (std::size_t i = 0; i < n; ++i) {
        const double candidate =
            static_cast<double>(observations[begin + i].weight) * (c == 0 ? 1.0 : nearest[i]);
        if (selected[i] == 0 && candidate > score) {
          best = i;
          score = candidate;
        }
      }
      selected[best] = 1;
      std::copy_n(points.data() + best * width, width, centers.data() + c * width);
      for (std::size_t i = 0; i < n; ++i)
        nearest[i] = std::min(
            nearest[i], distance(points.data() + i * width, centers.data() + c * width, width,
                                 squared_l2));
    }
    std::vector<std::uint32_t> assignments(n, no_history_row);
    std::uint32_t iterations = 0;
    for (; iterations < maximum_lloyd_iterations; ++iterations) {
      bool changed = false;
      std::vector<double> sums(k * width, 0), weights(k, 0);
      for (std::size_t i = 0; i < n; ++i) {
        std::uint32_t best = 0;
        double closest = std::numeric_limits<double>::infinity();
        for (std::uint32_t c = 0; c < k; ++c) {
          const auto value = distance(points.data() + i * width, centers.data() + c * width,
                                      width, squared_l2);
          if (value < closest) {
            closest = value;
            best = c;
          }
        }
        changed = changed || assignments[i] != best;
        assignments[i] = best;
        const auto weight = static_cast<double>(observations[begin + i].weight);
        weights[best] += weight;
        for (std::size_t d = 0; d < width; ++d)
          sums[best * width + d] += weight * points[i * width + d];
      }
      for (std::size_t c = 0; c < k; ++c) {
        if (weights[c] == 0)
          continue;
        for (std::size_t d = 0; d < width; ++d) {
          if (squared_l2) {
            centers[c * width + d] = sums[c * width + d] / weights[c];
            continue;
          }
          std::vector<std::pair<double, std::uint64_t>> values;
          values.reserve(n);
          std::uint64_t total = 0;
          for (std::size_t i = 0; i < n; ++i) {
            if (assignments[i] != c)
              continue;
            if (total > std::numeric_limits<std::uint64_t>::max() -
                            observations[begin + i].weight)
              return false;
            values.emplace_back(points[i * width + d], observations[begin + i].weight);
            total += observations[begin + i].weight;
          }
          std::sort(values.begin(), values.end());
          std::uint64_t cumulative = 0;
          for (const auto &[value, weight] : values) {
            cumulative += weight;
            if (cumulative >= (total + 1U) / 2U) {
              centers[c * width + d] = value;
              break;
            }
          }
        }
      }
      if (!changed) {
        ++iterations;
        break;
      }
    }
    stats.maximum_iterations = std::max(stats.maximum_iterations, iterations);
    std::vector<std::uint32_t> labels(k, no_history_row);
    for (std::size_t i = 0; i < n; ++i) {
      const auto cluster = assignments[i];
      if (labels[cluster] == no_history_row)
        labels[cluster] = row_count++;
      keys.push_back(observations[begin + i].key);
      rows.push_back(labels[cluster]);
      const auto error =
          distance(points.data() + i * width, centers.data() + cluster * width, width, squared_l2);
      stats.weighted_squared_distance +=
          error * static_cast<double>(observations[begin + i].weight);
      stats.maximum_squared_distance = std::max(stats.maximum_squared_distance, error);
      if (stats.weight > std::numeric_limits<std::uint64_t>::max() -
                             observations[begin + i].weight)
        return false;
      stats.weight += observations[begin + i].weight;
    }
    begin = end;
  }
  if (stats.weight == 0)
    return false;
  stats.weighted_squared_distance /= static_cast<double>(stats.weight);
  return true;
}
} // namespace

Result<HistoryBucketRows, KernelError>
HistoryBucketRows::build(const ca::BucketTable &flop, const ca::BucketTable &turn,
                         const ca::BucketTable &river, std::vector<HistoryObservation> observations,
                         const std::uint32_t maximum_children, HistoryClusteringReport *report) {
  using Outcome = Result<HistoryBucketRows, KernelError>;
  if (maximum_children == 0 || maximum_children > river.capacity() || observations.empty() ||
      observations.size() > maximum_support || river.centroid_width() == 0)
    return Outcome::failure(KernelError::InvalidInput);
  const auto classes = ClassBucketRows::build(flop, turn, river);
  if (!classes)
    return Outcome::failure(classes.error());
  HistoryBucketRows result;
  const std::array<const ca::BucketTable *, 3> tables{&flop, &turn, &river};
  for (std::size_t street = 0; street < 3; ++street) {
    result.capacities_[street] = tables[street]->capacity();
    result.tables_[street] = tables[street]->fingerprint();
  }
  result.counts_[0] = classes.value().count(ca::BucketStreet::Flop);
  result.flop_rows_.resize(81ULL * flop.capacity());
  for (std::uint8_t cls = 0; cls < 81; ++cls)
    for (std::uint16_t bucket = 0; bucket < flop.capacity(); ++bucket) {
      const auto mapped = classes.value().row(ca::BucketStreet::Flop, cls, bucket);
      result.flop_rows_[static_cast<std::size_t>(cls) * flop.capacity() + bucket] =
          mapped == ca::no_bucket ? no_history_row : mapped;
    }
  std::sort(observations.begin(), observations.end(),
            [](const auto &a, const auto &b) { return a.key < b.key; });
  const auto maximum_key = 81ULL * flop.capacity() * turn.capacity() * river.capacity();
  HistoryClusteringReport stats;
  stats.support = observations.size();
  for (std::size_t i = 0; i < observations.size(); ++i) {
    const auto &item = observations[i];
    if (item.key >= maximum_key || item.weight == 0 ||
        (i > 0 && observations[i - 1].key == item.key) ||
        stats.weight > std::numeric_limits<std::uint64_t>::max() - item.weight ||
        result.flop_rows_[item.key / river.capacity() / turn.capacity()] == no_history_row)
      return Outcome::failure(KernelError::InvalidInput);
    stats.weight += item.weight;
  }
  result.river_keys_.reserve(observations.size());
  result.river_rows_.reserve(observations.size());
  const auto width = static_cast<std::size_t>(river.centroid_width());
  for (std::size_t begin = 0; begin < observations.size();) {
    const auto parent = observations[begin].key / river.capacity();
    auto end = begin + 1;
    while (end < observations.size() && observations[end].key / river.capacity() == parent)
      ++end;
    result.turn_keys_.push_back(parent);
    const auto n = end - begin;
    const auto k = std::min<std::size_t>(maximum_children, n);
    std::vector<double> points(n * width), centers(k * width);
    for (std::size_t i = 0; i < n; ++i) {
      const auto bucket = observations[begin + i].key % river.capacity();
      for (std::size_t d = 0; d < width; ++d)
        points[i * width + d] = river.centroids()[bucket * width + d];
    }
    // Deterministic weighted farthest-first initialization, with key order
    // breaking ties. No random restart or training-policy information.
    std::vector<std::uint8_t> selected(n, 0);
    std::vector<double> nearest(n, std::numeric_limits<double>::infinity());
    for (std::size_t c = 0; c < k; ++c) {
      std::size_t best = 0;
      double score = -1;
      for (std::size_t i = 0; i < n; ++i) {
        const double candidate =
            static_cast<double>(observations[begin + i].weight) * (c == 0 ? 1.0 : nearest[i]);
        if (selected[i] == 0 && candidate > score) {
          best = i;
          score = candidate;
        }
      }
      selected[best] = 1;
      std::copy_n(points.data() + best * width, width, centers.data() + c * width);
      for (std::size_t i = 0; i < n; ++i)
        nearest[i] = std::min(
            nearest[i], distance(points.data() + i * width, centers.data() + c * width, width));
    }
    std::vector<std::uint32_t> assignments(n, no_history_row);
    std::uint32_t iterations = 0;
    for (; iterations < maximum_lloyd_iterations; ++iterations) {
      bool changed = false;
      std::vector<double> sums(k * width, 0), weights(k, 0);
      for (std::size_t i = 0; i < n; ++i) {
        std::uint32_t best = 0;
        double closest = std::numeric_limits<double>::infinity();
        for (std::uint32_t c = 0; c < k; ++c) {
          const auto value = distance(points.data() + i * width, centers.data() + c * width, width);
          if (value < closest) {
            closest = value;
            best = c;
          }
        }
        changed = changed || assignments[i] != best;
        assignments[i] = best;
        const auto weight = static_cast<double>(observations[begin + i].weight);
        weights[best] += weight;
        for (std::size_t d = 0; d < width; ++d)
          sums[best * width + d] += weight * points[i * width + d];
      }
      for (std::size_t c = 0; c < k; ++c)
        if (weights[c] > 0)
          for (std::size_t d = 0; d < width; ++d)
            centers[c * width + d] = sums[c * width + d] / weights[c];
      if (!changed) {
        ++iterations;
        break;
      }
    }
    stats.maximum_iterations = std::max(stats.maximum_iterations, iterations);
    std::vector<std::uint32_t> labels(k, no_history_row);
    // Number occupied clusters by their first observation; unique row ids per
    // parent ensure that no river row ever merges distinct turn histories.
    for (std::size_t i = 0; i < n; ++i) {
      const auto c = assignments[i];
      if (labels[c] == no_history_row)
        labels[c] = result.counts_[2]++;
      result.river_keys_.push_back(observations[begin + i].key);
      result.river_rows_.push_back(labels[c]);
      const auto error = distance(points.data() + i * width, centers.data() + c * width, width);
      stats.weighted_squared_distance +=
          error * static_cast<double>(observations[begin + i].weight);
      stats.maximum_squared_distance = std::max(stats.maximum_squared_distance, error);
    }
    begin = end;
  }
  result.counts_[1] = static_cast<std::uint32_t>(result.turn_keys_.size());
  stats.weighted_squared_distance /= static_cast<double>(stats.weight);
  if (!result.build_parent_rows())
    return Outcome::failure(KernelError::InvalidInput);
  result.fingerprint_ = "fnv1a64:" + detail::hex64_text(detail::fnv1a_text(result.payload()));
  if (report != nullptr)
    *report = stats;
  return Outcome::success(std::move(result));
}

Result<HistoryBucketRows, KernelError> HistoryBucketRows::build_hierarchy(
    const ca::BucketTable &flop, const ca::BucketTable &turn, const ca::BucketTable &river,
    std::vector<HistoryObservation> observations, const std::uint32_t maximum_turn_children,
    const std::uint32_t maximum_river_children, HistoryHierarchyReport *report) {
  using Outcome = Result<HistoryBucketRows, KernelError>;
  if (maximum_turn_children == 0 || maximum_turn_children > turn.capacity() ||
      maximum_river_children == 0 || maximum_river_children > river.capacity() ||
      observations.empty() || observations.size() > maximum_support || turn.centroid_width() == 0 ||
      river.centroid_width() == 0)
    return Outcome::failure(KernelError::InvalidInput);
  const auto classes = ClassBucketRows::build(flop, turn, river);
  if (!classes)
    return Outcome::failure(classes.error());

  HistoryBucketRows result;
  result.format_version_ = 2;
  const std::array<const ca::BucketTable *, 3> tables{&flop, &turn, &river};
  for (std::size_t street = 0; street < 3; ++street) {
    result.capacities_[street] = tables[street]->capacity();
    result.tables_[street] = tables[street]->fingerprint();
  }
  result.counts_[0] = classes.value().count(ca::BucketStreet::Flop);
  result.flop_rows_.resize(81ULL * flop.capacity());
  for (std::uint8_t cls = 0; cls < 81; ++cls)
    for (std::uint16_t bucket = 0; bucket < flop.capacity(); ++bucket) {
      const auto mapped = classes.value().row(ca::BucketStreet::Flop, cls, bucket);
      result.flop_rows_[static_cast<std::size_t>(cls) * flop.capacity() + bucket] =
          mapped == ca::no_bucket ? no_history_row : mapped;
    }

  std::sort(observations.begin(), observations.end(),
            [](const auto &a, const auto &b) { return a.key < b.key; });
  const auto maximum_key = 81ULL * flop.capacity() * turn.capacity() * river.capacity();
  std::vector<HistoryObservation> turn_observations;
  turn_observations.reserve(observations.size());
  for (std::size_t i = 0; i < observations.size(); ++i) {
    const auto &item = observations[i];
    if (item.key >= maximum_key || item.weight == 0 ||
        (i > 0 && observations[i - 1].key == item.key) ||
        result.flop_rows_[item.key / river.capacity() / turn.capacity()] == no_history_row)
      return Outcome::failure(KernelError::InvalidInput);
    const auto turn_key = item.key / river.capacity();
    if (turn_observations.empty() || turn_observations.back().key != turn_key)
      turn_observations.push_back({turn_key, item.weight});
    else if (turn_observations.back().weight >
             std::numeric_limits<std::uint64_t>::max() - item.weight)
      return Outcome::failure(KernelError::InvalidInput);
    else
      turn_observations.back().weight += item.weight;
  }

  HistoryHierarchyReport stats;
  if (!cluster_children(turn, turn_observations, maximum_turn_children, result.turn_keys_,
                        result.turn_rows_, result.counts_[1], stats.turn))
    return Outcome::failure(KernelError::InvalidInput);

  for (auto &item : observations) {
    const auto turn_key = item.key / river.capacity();
    const auto found = std::lower_bound(result.turn_keys_.begin(), result.turn_keys_.end(), turn_key);
    if (found == result.turn_keys_.end() || *found != turn_key)
      return Outcome::failure(KernelError::InvalidInput);
    const auto index = static_cast<std::size_t>(found - result.turn_keys_.begin());
    item.key = static_cast<std::uint64_t>(result.turn_rows_[index]) * river.capacity() +
               item.key % river.capacity();
  }
  std::sort(observations.begin(), observations.end(),
            [](const auto &a, const auto &b) { return a.key < b.key; });
  std::vector<HistoryObservation> river_observations;
  river_observations.reserve(observations.size());
  for (const auto &item : observations) {
    if (river_observations.empty() || river_observations.back().key != item.key)
      river_observations.push_back(item);
    else if (river_observations.back().weight >
             std::numeric_limits<std::uint64_t>::max() - item.weight)
      return Outcome::failure(KernelError::InvalidInput);
    else
      river_observations.back().weight += item.weight;
  }
  if (!cluster_children(river, river_observations, maximum_river_children, result.river_keys_,
                        result.river_rows_, result.counts_[2], stats.river) ||
      !result.build_parent_rows() || !result.build_lookup_rows())
    return Outcome::failure(KernelError::InvalidInput);
  result.fingerprint_ = "fnv1a64:" + detail::hex64_text(detail::fnv1a_text(result.payload()));
  if (report != nullptr)
    *report = stats;
  return Outcome::success(std::move(result));
}

std::uint32_t HistoryBucketRows::parent_row(const Street street,
                                            const std::uint32_t row) const noexcept {
  if (street == Street::Preflop)
    return no_history_row;
  const auto index = static_cast<std::size_t>(street) - 1U;
  return index < parent_rows_.size() && row < parent_rows_[index].size()
             ? parent_rows_[index][row]
             : no_history_row;
}

bool HistoryBucketRows::build_parent_rows() noexcept {
  for (std::size_t street = 0; street < parent_rows_.size(); ++street)
    parent_rows_[street].assign(counts_[street], no_history_row);

  for (std::size_t key = 0; key < flop_rows_.size(); ++key) {
    const auto row = flop_rows_[key];
    if (row == no_history_row)
      continue;
    const auto parent = static_cast<std::uint32_t>(key / capacities_[0]);
    if (row >= parent_rows_[0].size() || parent >= 81U ||
        (parent_rows_[0][row] != no_history_row && parent_rows_[0][row] != parent))
      return false;
    parent_rows_[0][row] = parent;
  }
  if (std::find(parent_rows_[0].begin(), parent_rows_[0].end(), no_history_row) !=
      parent_rows_[0].end())
    return false;

  for (std::size_t index = 0; index < turn_keys_.size(); ++index) {
    const auto row = format_version_ == 1 ? static_cast<std::uint32_t>(index) : turn_rows_[index];
    const auto fkey = turn_keys_[index] / capacities_[1];
    if (fkey >= flop_rows_.size() || flop_rows_[fkey] == no_history_row)
      return false;
    const auto parent = flop_rows_[fkey];
    if (row >= parent_rows_[1].size() ||
        (parent_rows_[1][row] != no_history_row && parent_rows_[1][row] != parent))
      return false;
    parent_rows_[1][row] = parent;
  }
  if (std::find(parent_rows_[1].begin(), parent_rows_[1].end(), no_history_row) !=
      parent_rows_[1].end())
    return false;

  for (std::size_t index = 0; index < river_rows_.size(); ++index) {
    const auto row = river_rows_[index];
    std::uint32_t parent = no_history_row;
    if (format_version_ == 1) {
      const auto tkey = river_keys_[index] / capacities_[2];
      const auto found = std::lower_bound(turn_keys_.begin(), turn_keys_.end(), tkey);
      if (found == turn_keys_.end() || *found != tkey)
        return false;
      parent = static_cast<std::uint32_t>(found - turn_keys_.begin());
    } else {
      const auto encoded_parent = river_keys_[index] / capacities_[2];
      if (encoded_parent >= counts_[1])
        return false;
      parent = static_cast<std::uint32_t>(encoded_parent);
    }
    if (row >= parent_rows_[2].size())
      return false;
    if (parent_rows_[2][row] != no_history_row && parent_rows_[2][row] != parent)
      return false;
    parent_rows_[2][row] = parent;
  }
  return std::find(parent_rows_[2].begin(), parent_rows_[2].end(), no_history_row) ==
         parent_rows_[2].end();
}

bool HistoryBucketRows::build_lookup_rows() {
  turn_lookup_.clear();
  river_lookup_.clear();
  if (format_version_ == 1)
    return true;
  const auto turn_size = 81ULL * capacities_[0] * capacities_[1];
  const auto river_size = static_cast<std::uint64_t>(counts_[1]) * capacities_[2];
  if (turn_size > std::numeric_limits<std::size_t>::max() ||
      river_size > std::numeric_limits<std::size_t>::max())
    return false;
  turn_lookup_.assign(static_cast<std::size_t>(turn_size), no_history_row);
  river_lookup_.assign(static_cast<std::size_t>(river_size), no_history_row);
  for (std::size_t index = 0; index < turn_keys_.size(); ++index) {
    const auto key = turn_keys_[index];
    if (key >= turn_lookup_.size() || turn_lookup_[key] != no_history_row)
      return false;
    turn_lookup_[key] = turn_rows_[index];
  }
  for (std::size_t index = 0; index < river_keys_.size(); ++index) {
    const auto key = river_keys_[index];
    if (key >= river_lookup_.size() || river_lookup_[key] != no_history_row)
      return false;
    river_lookup_[key] = river_rows_[index];
  }
  return true;
}

std::uint32_t HistoryBucketRows::row(const Street street, const std::uint8_t hand_class,
                                     const std::uint16_t flop, const std::uint16_t turn,
                                     const std::uint16_t river) const noexcept {
  if (hand_class >= 81)
    return no_history_row;
  if (street == Street::Preflop)
    return hand_class;
  if (flop >= capacities_[0])
    return no_history_row;
  const auto fkey = static_cast<std::uint64_t>(hand_class) * capacities_[0] + flop;
  if (street == Street::Flop)
    return flop_rows_[fkey];
  if (turn >= capacities_[1])
    return no_history_row;
  const auto tkey = fkey * capacities_[1] + turn;
  if (street == Street::Turn) {
    if (format_version_ == 2)
      return tkey < turn_lookup_.size() ? turn_lookup_[tkey] : no_history_row;
    const auto found = std::lower_bound(turn_keys_.begin(), turn_keys_.end(), tkey);
    if (found == turn_keys_.end() || *found != tkey)
      return no_history_row;
    const auto index = static_cast<std::size_t>(found - turn_keys_.begin());
    return format_version_ == 1 ? static_cast<std::uint32_t>(index) : turn_rows_[index];
  }
  if (street != Street::River || river >= capacities_[2])
    return no_history_row;
  std::uint64_t river_parent = tkey;
  if (format_version_ == 2) {
    if (tkey >= turn_lookup_.size() || turn_lookup_[tkey] == no_history_row)
      return no_history_row;
    river_parent = turn_lookup_[tkey];
  }
  const auto rkey = river_parent * capacities_[2] + river;
  if (format_version_ == 2)
    return rkey < river_lookup_.size() ? river_lookup_[rkey] : no_history_row;
  const auto found = std::lower_bound(river_keys_.begin(), river_keys_.end(), rkey);
  return found != river_keys_.end() && *found == rkey
             ? river_rows_[static_cast<std::size_t>(found - river_keys_.begin())]
             : no_history_row;
}

bool HistoryBucketRows::matches(const ca::BucketTable &table) const noexcept {
  const auto index = static_cast<std::size_t>(table.street());
  return index < 3 && tables_[index] == table.fingerprint();
}

std::uint64_t HistoryBucketRows::byte_size() const noexcept {
  return 4ULL * (flop_rows_.size() + turn_rows_.size() + river_rows_.size()) +
         8ULL * (turn_keys_.size() + river_keys_.size());
}

std::string HistoryBucketRows::payload() const {
  using namespace binary_io;
  std::string data(format_version_ == 1 ? "GTOSDHR1" : "GTOSDHR2");
  for (std::size_t i = 0; i < 3; ++i) {
    append_little32(data, capacities_[i]);
    append_little32(data, counts_[i]);
    append_string(data, tables_[i]);
  }
  append_little32(data, static_cast<std::uint32_t>(flop_rows_.size()));
  for (const auto value : flop_rows_)
    append_little32(data, value);
  append_little32(data, static_cast<std::uint32_t>(turn_keys_.size()));
  for (std::size_t i = 0; i < turn_keys_.size(); ++i) {
    append_little(data, turn_keys_[i]);
    if (format_version_ == 2)
      append_little32(data, turn_rows_[i]);
  }
  append_little32(data, static_cast<std::uint32_t>(river_keys_.size()));
  for (std::size_t i = 0; i < river_keys_.size(); ++i) {
    append_little(data, river_keys_[i]);
    append_little32(data, river_rows_[i]);
  }
  return data;
}

Result<bool, KernelError> HistoryBucketRows::save(const std::filesystem::path &path) const {
  using Outcome = Result<bool, KernelError>;
  auto data = payload();
  binary_io::append_little(data, detail::fnv1a_text(data));
  const auto temporary = std::filesystem::path(path.string() + ".tmp");
  std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
  file.write(data.data(), static_cast<std::streamsize>(data.size()));
  file.close();
  if (!file)
    return Outcome::failure(KernelError::InvalidInput);
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error)
    return Outcome::failure(KernelError::InvalidInput);
  return Outcome::success(true);
}

Result<HistoryBucketRows, KernelError> HistoryBucketRows::load(const std::filesystem::path &path) {
  using Outcome = Result<HistoryBucketRows, KernelError>;
  const auto fail = [] { return Outcome::failure(KernelError::InvalidInput); };
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input || input.tellg() < 16 ||
      input.tellg() > static_cast<std::streamoff>(maximum_file_bytes))
    return fail();
  std::string data(static_cast<std::size_t>(input.tellg()), '\0');
  input.seekg(0);
  input.read(data.data(), static_cast<std::streamsize>(data.size()));
  if (!input || (data.substr(0, 8) != "GTOSDHR1" && data.substr(0, 8) != "GTOSDHR2"))
    return fail();
  const bool hierarchy = data.substr(0, 8) == "GTOSDHR2";
  const std::string checksum_data = data.substr(data.size() - 8);
  binary_io::Reader checksum(checksum_data);
  std::uint64_t stored_hash = 0;
  if (!checksum.read_little(stored_hash))
    return fail();
  data.resize(data.size() - 8);
  if (detail::fnv1a_text(data) != stored_hash)
    return fail();
  const auto body_data = data.substr(8);
  binary_io::Reader body(body_data);
  HistoryBucketRows result;
  result.format_version_ = hierarchy ? 2U : 1U;
  for (std::size_t i = 0; i < 3; ++i) {
    if (!body.read_little32(result.capacities_[i]) || !body.read_little32(result.counts_[i]) ||
        !body.read_string(result.tables_[i]) || result.capacities_[i] == 0 ||
        result.capacities_[i] > ca::maximum_bucket_capacity || result.counts_[i] == 0 ||
        result.counts_[i] > maximum_support)
      return fail();
  }
  std::uint32_t size = 0;
  if (!body.read_little32(size) || size != 81 * result.capacities_[0])
    return fail();
  result.flop_rows_.resize(size);
  for (auto &value : result.flop_rows_)
    if (!body.read_little32(value) || (value != no_history_row && value >= result.counts_[0]))
      return fail();
  if (!body.read_little32(size) || size > maximum_support ||
      (!hierarchy && size != result.counts_[1]) || (hierarchy && size < result.counts_[1]))
    return fail();
  result.turn_keys_.resize(size);
  if (hierarchy)
    result.turn_rows_.resize(size);
  for (std::size_t i = 0; i < size; ++i) {
    auto &value = result.turn_keys_[i];
    if (!body.read_little(value) ||
        value >= 81ULL * result.capacities_[0] * result.capacities_[1] ||
        (i > 0 && value <= result.turn_keys_[i - 1]) ||
        (hierarchy &&
         (!body.read_little32(result.turn_rows_[i]) || result.turn_rows_[i] >= result.counts_[1])))
      return fail();
  }
  if (!body.read_little32(size) || size > maximum_support || size < result.counts_[2])
    return fail();
  result.river_keys_.resize(size);
  result.river_rows_.resize(size);
  std::vector<std::uint64_t> parents(result.counts_[2], std::numeric_limits<std::uint64_t>::max());
  std::set<std::uint64_t> seen_parents;
  for (std::size_t i = 0; i < size; ++i) {
    auto &key = result.river_keys_[i];
    auto &row = result.river_rows_[i];
    if (!body.read_little(key) || !body.read_little32(row) || row >= result.counts_[2] ||
        key >= (hierarchy ? static_cast<std::uint64_t>(result.counts_[1]) * result.capacities_[2]
                          : 81ULL * result.capacities_[0] * result.capacities_[1] *
                                result.capacities_[2]) ||
        (i > 0 && key <= result.river_keys_[i - 1]))
      return fail();
    const auto parent = key / result.capacities_[2];
    if (parents[row] != std::numeric_limits<std::uint64_t>::max() && parents[row] != parent)
      return fail();
    parents[row] = parent;
    seen_parents.insert(parent);
    if (!hierarchy && result.flop_rows_[parent / result.capacities_[1]] == no_history_row)
      return fail();
  }
  const bool parents_valid = hierarchy
                                 ? seen_parents.size() == result.counts_[1] &&
                                       !seen_parents.empty() && *seen_parents.begin() == 0 &&
                                       *seen_parents.rbegin() == result.counts_[1] - 1
                                 : seen_parents.size() == result.turn_keys_.size() &&
                                       std::equal(seen_parents.begin(), seen_parents.end(),
                                                  result.turn_keys_.begin());
  if (!body.at_end() || !parents_valid ||
      std::find(parents.begin(), parents.end(), std::numeric_limits<std::uint64_t>::max()) !=
          parents.end() ||
      !result.build_parent_rows() || !result.build_lookup_rows())
    return fail();
  result.fingerprint_ = "fnv1a64:" + detail::hex64_text(stored_hash);
  return Outcome::success(std::move(result));
}
} // namespace gtosd::preflop_blueprint
