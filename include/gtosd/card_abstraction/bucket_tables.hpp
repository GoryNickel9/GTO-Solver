#pragma once

#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/exact_features.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

// Bucket tables produced by clustering the exact street features.
//
// Flop and turn observations are compared with the one-dimensional earth
// mover's distance between their equity histograms, computed exactly as the
// L1 distance between cumulative counts; the centroid of a cluster is the
// coordinate-wise weighted median of the cumulative counts, which is the
// exact L1 barycenter. River observations use the squared L2 distance between
// fixed-point equity vectors with rounded weighted means as centroids.
// Every step uses integer arithmetic and static work partitioning, so the
// result does not depend on the number of threads. Bucket ids are relabeled
// by increasing centroid strength after convergence.
namespace gtosd::card_abstraction {

enum class BucketStreet : std::uint8_t { Flop, Turn, River };

inline constexpr std::uint16_t no_bucket = 0xFFFFU;
inline constexpr std::uint16_t maximum_bucket_capacity = 4'096U;

struct ClusteringParameters {
  std::uint16_t capacity{0U};
  std::uint32_t restarts{10U};
  std::uint32_t screening_iterations{10U};
  std::uint32_t maximum_iterations{25U};
  std::uint32_t screening_sample{500'000U};
  std::uint64_t partition_seed{0x5041'5254'4954'494FULL};
  unsigned threads{1U};
};

struct ClusteringDiagnostics {
  std::uint64_t observations{0U};
  std::uint64_t total_weight{0U};
  std::uint32_t chosen_restart{0U};
  std::vector<double> screening_inertia;
  std::vector<double> inertia_by_iteration;
  std::uint32_t iterations{0U};
  std::uint32_t empty_reseeds{0U};
  std::vector<std::uint64_t> occupancy_weight;
  std::vector<std::uint32_t> occupancy_rows;
  std::vector<double> mean_distance;
  double seconds{0.0};
};

struct BucketTableBuilderAccess;

class BucketTable {
public:
  static constexpr std::uint32_t format_version = 1U;

  [[nodiscard]] static Result<BucketTable, ResourceError>
  build_flop(const BoardCatalog &catalog, const FlopFeatureTable &features,
             const ClusteringParameters &parameters, ClusteringDiagnostics *diagnostics = nullptr);
  [[nodiscard]] static Result<BucketTable, ResourceError>
  build_turn(const BoardCatalog &catalog, const TurnFeatureTable &features,
             const ClusteringParameters &parameters, ClusteringDiagnostics *diagnostics = nullptr);
  [[nodiscard]] static Result<BucketTable, ResourceError>
  build_river(const BoardCatalog &catalog, const RiverFeatureTable &features,
              const ClusteringParameters &parameters,
              ClusteringDiagnostics *diagnostics = nullptr);

  [[nodiscard]] static Result<BucketTable, ResourceError> load(const std::filesystem::path &path);
  [[nodiscard]] Result<bool, ResourceError> save(const std::filesystem::path &path) const;

  [[nodiscard]] BucketStreet street() const noexcept { return street_; }
  [[nodiscard]] std::uint16_t capacity() const noexcept { return capacity_; }
  [[nodiscard]] std::uint32_t rows() const noexcept { return rows_; }
  [[nodiscard]] std::uint32_t centroid_width() const noexcept { return width_; }
  [[nodiscard]] std::uint16_t bucket(const std::uint32_t row_index,
                                     const std::uint16_t combo) const noexcept {
    return buckets_[static_cast<std::size_t>(row_index) * combo_count + combo];
  }
  // All 630 combos of a canonical board; overlapping combos hold no_bucket.
  [[nodiscard]] std::span<const std::uint16_t, combo_count>
  row(const std::uint32_t row_index) const noexcept {
    return std::span<const std::uint16_t, combo_count>(
        buckets_.data() + static_cast<std::size_t>(row_index) * combo_count, combo_count);
  }
  [[nodiscard]] const std::vector<std::uint16_t> &buckets() const noexcept { return buckets_; }
  [[nodiscard]] const std::vector<std::uint16_t> &centroids() const noexcept {
    return centroids_;
  }
  [[nodiscard]] const ClusteringParameters &parameters() const noexcept { return parameters_; }
  [[nodiscard]] const std::string &fingerprint() const noexcept { return fingerprint_; }
  [[nodiscard]] const std::string &feature_fingerprint() const noexcept {
    return feature_fingerprint_;
  }
  [[nodiscard]] const std::string &catalog_fingerprint() const noexcept {
    return catalog_fingerprint_;
  }
  [[nodiscard]] std::uint64_t payload_bytes() const noexcept {
    return (buckets_.size() + centroids_.size()) * sizeof(std::uint16_t);
  }

  friend bool operator==(const BucketTable &left, const BucketTable &right) {
    return left.street_ == right.street_ && left.capacity_ == right.capacity_ &&
           left.buckets_ == right.buckets_ && left.centroids_ == right.centroids_ &&
           left.fingerprint_ == right.fingerprint_;
  }

private:
  friend struct BucketTableBuilderAccess;
  void finalize();

  BucketStreet street_{BucketStreet::Flop};
  std::uint16_t capacity_{0U};
  std::uint32_t rows_{0U};
  std::uint32_t width_{0U};
  ClusteringParameters parameters_{};
  std::vector<std::uint16_t> buckets_;
  std::vector<std::uint16_t> centroids_;
  std::string catalog_fingerprint_;
  std::string feature_fingerprint_;
  std::string fingerprint_;
};

struct BucketLookup {
  std::uint16_t bucket{no_bucket};
  std::uint32_t row_index{0U};
  std::uint16_t combo{0U};
  SuitPermutation permutation{identity_permutation};
};

[[nodiscard]] Result<BucketLookup, CardError>
lookup_flop_bucket(const BoardCatalog &catalog, const BucketTable &table,
                   const std::array<CardId, 3> &flop, const std::array<CardId, 2> &hand);
[[nodiscard]] Result<BucketLookup, CardError>
lookup_turn_bucket(const BoardCatalog &catalog, const BucketTable &table,
                   const std::array<CardId, 3> &flop, CardId turn,
                   const std::array<CardId, 2> &hand);
[[nodiscard]] Result<BucketLookup, CardError>
lookup_river_bucket(const BoardCatalog &catalog, const BucketTable &table,
                    const std::array<CardId, 5> &board, const std::array<CardId, 2> &hand);

[[nodiscard]] const char *bucket_street_name(BucketStreet street) noexcept;

} // namespace gtosd::card_abstraction
