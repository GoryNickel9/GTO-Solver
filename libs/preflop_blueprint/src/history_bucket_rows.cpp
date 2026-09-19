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

double distance(const double *left, const double *right, const std::size_t width) {
  double sum = 0;
  for (std::size_t d = 0; d < width; ++d) {
    const auto delta = left[d] - right[d];
    sum += delta * delta;
  }
  return sum;
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
  result.fingerprint_ = "fnv1a64:" + detail::hex64_text(detail::fnv1a_text(result.payload()));
  if (report != nullptr)
    *report = stats;
  return Outcome::success(std::move(result));
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
    const auto found = std::lower_bound(turn_keys_.begin(), turn_keys_.end(), tkey);
    return found != turn_keys_.end() && *found == tkey
               ? static_cast<std::uint32_t>(found - turn_keys_.begin())
               : no_history_row;
  }
  if (street != Street::River || river >= capacities_[2])
    return no_history_row;
  const auto rkey = tkey * capacities_[2] + river;
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
  return 4ULL * (flop_rows_.size() + river_rows_.size()) +
         8ULL * (turn_keys_.size() + river_keys_.size());
}

std::string HistoryBucketRows::payload() const {
  using namespace binary_io;
  std::string data("GTOSDHR1");
  for (std::size_t i = 0; i < 3; ++i) {
    append_little32(data, capacities_[i]);
    append_little32(data, counts_[i]);
    append_string(data, tables_[i]);
  }
  append_little32(data, static_cast<std::uint32_t>(flop_rows_.size()));
  for (const auto value : flop_rows_)
    append_little32(data, value);
  append_little32(data, static_cast<std::uint32_t>(turn_keys_.size()));
  for (const auto value : turn_keys_)
    append_little(data, value);
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
  if (!input || data.substr(0, 8) != "GTOSDHR1")
    return fail();
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
  if (!body.read_little32(size) || size != result.counts_[1])
    return fail();
  result.turn_keys_.resize(size);
  for (auto &value : result.turn_keys_)
    if (!body.read_little(value))
      return fail();
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
        key >= 81ULL * result.capacities_[0] * result.capacities_[1] * result.capacities_[2] ||
        (i > 0 && key <= result.river_keys_[i - 1]))
      return fail();
    const auto parent = key / result.capacities_[2];
    if (parents[row] != std::numeric_limits<std::uint64_t>::max() && parents[row] != parent)
      return fail();
    parents[row] = parent;
    seen_parents.insert(parent);
    if (result.flop_rows_[parent / result.capacities_[1]] == no_history_row)
      return fail();
  }
  if (!body.at_end() || seen_parents.size() != result.turn_keys_.size() ||
      !std::equal(seen_parents.begin(), seen_parents.end(), result.turn_keys_.begin()) ||
      std::find(parents.begin(), parents.end(), std::numeric_limits<std::uint64_t>::max()) !=
          parents.end())
    return fail();
  result.fingerprint_ = "fnv1a64:" + detail::hex64_text(stored_hash);
  return Outcome::success(std::move(result));
}
} // namespace gtosd::preflop_blueprint
