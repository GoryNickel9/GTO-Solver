// Audits whether the production flop clustering stopped too early. It builds
// an in-memory table with the same inputs and capacity and never trains a policy.
#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
namespace ca = gtosd::card_abstraction;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

std::uint64_t inertia(const ca::BoardCatalog &catalog, const ca::FlopFeatureTable &features,
                      const ca::BucketTable &table) {
  std::uint64_t total = 0U;
  const auto &combos = ca::combo_table();
  for (std::uint32_t flop = 0U; flop < catalog.flops().size(); ++flop) {
    const auto &entry = catalog.flops()[flop];
    const auto mask = entry.cards[0].mask() | entry.cards[1].mask() | entry.cards[2].mask();
    for (std::uint16_t combo = 0U; combo < ca::combo_count; ++combo) {
      if ((combos.masks[combo] & mask) != 0U)
        continue;
      const auto bucket = table.bucket(flop, combo);
      const auto histogram = features.histogram(flop, combo);
      const auto *centroid =
          table.centroids().data() + static_cast<std::size_t>(bucket) * table.centroid_width();
      std::uint32_t cumulative = 0U;
      std::uint64_t distance = 0U;
      for (std::size_t bin = 0U; bin < histogram.size(); ++bin) {
        cumulative += histogram[bin];
        distance +=
            cumulative > centroid[bin] ? cumulative - centroid[bin] : centroid[bin] - cumulative;
      }
      total += distance * entry.multiplicity;
    }
  }
  return total;
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
    const auto threads =
        static_cast<unsigned>(std::stoul(args.contains("--threads") ? args.at("--threads") : "8"));
    const auto maximum = static_cast<std::uint32_t>(
        std::stoul(args.contains("--max-iterations") ? args.at("--max-iterations") : "100"));
    const auto resources = std::filesystem::path(required("--resources-dir"));
    const auto buckets = std::filesystem::path(required("--buckets-dir"));
    auto features = ca::FlopFeatureTable::load(resources / "flop_features_v1.bin");
    auto baseline = ca::BucketTable::load(buckets / "flop_buckets_v1.bin");
    if (!features || !baseline ||
        features.value().fingerprint() != baseline.value().feature_fingerprint()) {
      throw std::runtime_error("incompatible inputs");
    }
    const auto catalog = ca::BoardCatalog::build();
    const auto baseline_inertia = inertia(catalog, features.value(), baseline.value());
    auto parameters = baseline.value().parameters();
    parameters.maximum_iterations = maximum;
    parameters.threads = threads;
    ca::ClusteringDiagnostics diagnostics;
    const auto started = Clock::now();
    auto extended =
        ca::BucketTable::build_flop(catalog, features.value(), parameters, &diagnostics);
    if (!extended)
      throw std::runtime_error("extended clustering failed");
    const auto total_weight = diagnostics.total_weight;
    const auto final_inertia = static_cast<std::uint64_t>(diagnostics.inertia_by_iteration.back());
    Json report = {
        {"schema", "gtosd.flop_clustering_audit.v1"},
        {"baseline_fingerprint", baseline.value().fingerprint()},
        {"extended_fingerprint", extended.value().fingerprint()},
        {"feature_fingerprint", features.value().fingerprint()},
        {"capacity", parameters.capacity},
        {"restarts", parameters.restarts},
        {"screening_iterations", parameters.screening_iterations},
        {"screening_sample", parameters.screening_sample},
        {"partition_seed", parameters.partition_seed},
        {"baseline_maximum_iterations", baseline.value().parameters().maximum_iterations},
        {"extended_maximum_iterations", maximum},
        {"extended_iterations", diagnostics.iterations},
        {"chosen_restart", diagnostics.chosen_restart},
        {"total_weight", total_weight},
        {"baseline_inertia", baseline_inertia},
        {"extended_inertia", final_inertia},
        {"baseline_mean_distance", static_cast<double>(baseline_inertia) / total_weight},
        {"extended_mean_distance", static_cast<double>(final_inertia) / total_weight},
        {"relative_inertia_reduction",
         static_cast<double>(baseline_inertia - final_inertia) / baseline_inertia},
        {"seconds", std::chrono::duration<double>(Clock::now() - started).count()},
        {"saved", false},
        {"interpretation", "Same features, capacity, seed, restarts and screening; only the "
                           "maximum Lloyd iterations change."}};
    std::ofstream output(required("--output"));
    output << report.dump(2) << '\n';
    if (!output)
      throw std::runtime_error("cannot write output");
    std::cout << "FLOP_CLUSTERING_AUDIT=PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FLOP_CLUSTERING_AUDIT=FAIL " << error.what() << '\n';
    return 1;
  }
}
