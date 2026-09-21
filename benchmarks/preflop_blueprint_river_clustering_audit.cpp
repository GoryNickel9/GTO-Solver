// Performs one exact Lloyd refinement audit on the saved river table. The
// candidate remains in memory and is never used to train or certify a policy.
#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>

namespace {
namespace ca = gtosd::card_abstraction;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

std::uint64_t squared_distance(const std::uint16_t *left, const std::uint16_t *right) noexcept {
  std::uint64_t result = 0U;
  for (std::size_t i = 0U; i < ca::river_feature_count; ++i) {
    const auto delta = static_cast<std::int64_t>(left[i]) - right[i];
    result += static_cast<std::uint64_t>(delta * delta);
  }
  return result;
}
} // namespace

int main(const int argc, char **argv) {
  try {
    std::map<std::string, std::string> args;
    for (int i = 1; i < argc; i += 2) {
      if (i + 1 >= argc)
        throw std::runtime_error("missing argument value");
      args.emplace(argv[i], argv[i + 1]);
    }
    const auto required = [&](const std::string &key) -> const std::string & {
      if (!args.contains(key))
        throw std::runtime_error("required " + key);
      return args.at(key);
    };
    const unsigned threads =
        static_cast<unsigned>(std::stoul(args.contains("--threads") ? args.at("--threads") : "8"));
    if (threads == 0U || threads > 64U)
      throw std::runtime_error("invalid thread count");
    const auto resources = std::filesystem::path(required("--resources-dir"));
    const auto buckets = std::filesystem::path(required("--buckets-dir"));
    auto features = ca::RiverFeatureTable::load(resources / "river_features_v1.bin");
    auto baseline = ca::BucketTable::load(buckets / "river_buckets_v1.bin");
    if (!features || !baseline ||
        features.value().fingerprint() != baseline.value().feature_fingerprint()) {
      throw std::runtime_error("incompatible inputs");
    }
    const auto started = Clock::now();
    const auto catalog = ca::BoardCatalog::build();
    const auto capacity = baseline.value().capacity();
    std::vector<std::uint64_t> weights(capacity, 0U);
    std::vector<std::uint64_t> sums(static_cast<std::size_t>(capacity) * ca::river_feature_count,
                                    0U);
    const auto &combos = ca::combo_table();
    std::uint64_t observations = 0U, total_weight = 0U, baseline_inertia = 0U;
    for (std::uint32_t board = 0U; board < catalog.river_boards().size(); ++board) {
      const auto &entry = catalog.river_boards()[board];
      std::uint64_t mask = 0U;
      for (const auto card : entry.cards)
        mask |= card.mask();
      for (std::uint16_t combo = 0U; combo < ca::combo_count; ++combo) {
        if ((combos.masks[combo] & mask) != 0U)
          continue;
        const auto bucket = baseline.value().bucket(board, combo);
        const auto values = features.value().features(board, combo);
        const auto *centroid = baseline.value().centroids().data() +
                               static_cast<std::size_t>(bucket) * ca::river_feature_count;
        baseline_inertia += squared_distance(values.data(), centroid) * entry.multiplicity;
        weights[bucket] += entry.multiplicity;
        auto *sum = sums.data() + static_cast<std::size_t>(bucket) * ca::river_feature_count;
        for (std::size_t i = 0U; i < values.size(); ++i) {
          sum[i] += static_cast<std::uint64_t>(values[i]) * entry.multiplicity;
        }
        ++observations;
        total_weight += entry.multiplicity;
      }
    }
    auto refit = baseline.value().centroids();
    for (std::uint16_t bucket = 0U; bucket < capacity; ++bucket) {
      if (weights[bucket] == 0U)
        throw std::runtime_error("empty baseline bucket");
      auto *centroid = refit.data() + static_cast<std::size_t>(bucket) * ca::river_feature_count;
      const auto *sum = sums.data() + static_cast<std::size_t>(bucket) * ca::river_feature_count;
      for (std::size_t i = 0U; i < ca::river_feature_count; ++i) {
        centroid[i] = static_cast<std::uint16_t>((sum[i] + weights[bucket] / 2U) / weights[bucket]);
      }
    }
    struct Partial {
      std::uint64_t refit_inertia{0U};
      std::uint64_t reassigned_inertia{0U};
      std::uint64_t changed_rows{0U};
      std::uint64_t changed_weight{0U};
    };
    std::vector<Partial> partial(threads);
    std::vector<std::thread> workers;
    for (unsigned worker = 0U; worker < threads; ++worker) {
      workers.emplace_back([&, worker] {
        const auto first = catalog.river_boards().size() * worker / threads;
        const auto last = catalog.river_boards().size() * (worker + 1U) / threads;
        auto &result = partial[worker];
        for (std::size_t board = first; board < last; ++board) {
          const auto &entry = catalog.river_boards()[board];
          std::uint64_t mask = 0U;
          for (const auto card : entry.cards)
            mask |= card.mask();
          for (std::uint16_t combo = 0U; combo < ca::combo_count; ++combo) {
            if ((combos.masks[combo] & mask) != 0U)
              continue;
            const auto current = baseline.value().bucket(static_cast<std::uint32_t>(board), combo);
            const auto values = features.value().features(static_cast<std::uint32_t>(board), combo);
            const auto current_distance =
                squared_distance(values.data(), refit.data() + static_cast<std::size_t>(current) *
                                                                   ca::river_feature_count);
            result.refit_inertia += current_distance * entry.multiplicity;
            auto best_distance = std::numeric_limits<std::uint64_t>::max();
            std::uint16_t best = 0U;
            for (std::uint16_t bucket = 0U; bucket < capacity; ++bucket) {
              const auto distance =
                  squared_distance(values.data(), refit.data() + static_cast<std::size_t>(bucket) *
                                                                     ca::river_feature_count);
              if (distance < best_distance) {
                best_distance = distance;
                best = bucket;
              }
            }
            result.reassigned_inertia += best_distance * entry.multiplicity;
            if (best != current) {
              ++result.changed_rows;
              result.changed_weight += entry.multiplicity;
            }
          }
        }
      });
    }
    for (auto &worker : workers)
      worker.join();
    Partial total;
    for (const auto &item : partial) {
      total.refit_inertia += item.refit_inertia;
      total.reassigned_inertia += item.reassigned_inertia;
      total.changed_rows += item.changed_rows;
      total.changed_weight += item.changed_weight;
    }
    Json report = {
        {"schema", "gtosd.river_clustering_one_step_audit.v1"},
        {"baseline_fingerprint", baseline.value().fingerprint()},
        {"feature_fingerprint", features.value().fingerprint()},
        {"capacity", capacity},
        {"baseline_maximum_iterations", baseline.value().parameters().maximum_iterations},
        {"observations", observations},
        {"total_weight", total_weight},
        {"baseline_inertia", baseline_inertia},
        {"refit_same_assignment_inertia", total.refit_inertia},
        {"one_step_reassigned_inertia", total.reassigned_inertia},
        {"relative_one_step_reduction",
         static_cast<double>(baseline_inertia - total.reassigned_inertia) / baseline_inertia},
        {"changed_rows", total.changed_rows},
        {"changed_rows_fraction", static_cast<double>(total.changed_rows) / observations},
        {"changed_weight", total.changed_weight},
        {"changed_weight_fraction", static_cast<double>(total.changed_weight) / total_weight},
        {"seconds", std::chrono::duration<double>(Clock::now() - started).count()},
        {"saved", false},
        {"interpretation",
         "One exact Lloyd update from the saved 1000-bucket river table; no policy was trained."}};
    std::ofstream output(required("--output"));
    output << report.dump(2) << '\n';
    if (!output)
      throw std::runtime_error("cannot write output");
    std::cout << "RIVER_CLUSTERING_AUDIT=PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RIVER_CLUSTERING_AUDIT=FAIL " << error.what() << '\n';
    return 1;
  }
}
