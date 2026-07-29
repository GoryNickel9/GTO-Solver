#pragma once

#include "gtosd/postflop/postflop_solver.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace gtosd {

enum class StorageError : std::uint8_t {
  InvalidArgument,
  IoFailure,
  InvalidMagic,
  TruncatedFile,
  UnsupportedVersion,
  UnsupportedFeature,
  InvalidIndex,
  DuplicateChunk,
  MissingChunk,
  AuthenticationFailed,
  CompressionFailed,
  CorruptData,
  MigrationFailed,
  CatalogFailure,
  NotFound
};

enum class SolutionChunkType : std::uint32_t {
  Config = 1,
  Tree = 2,
  Isomorphism = 3,
  Strategy = 4,
  Ev = 5,
  Ranges = 6,
  Nodelocks = 7,
  Metrics = 8,
  Dictionary = 9
};

enum class SolutionFeature : std::uint64_t {
  Chunked = 1ULL << 0U,
  Zstd = 1ULL << 1U,
  Secretstream = 1ULL << 2U,
  RandomAccess = 1ULL << 3U,
  ExactStrategy = 1ULL << 4U,
  QuantizedStrategy = 1ULL << 5U
};

using StorageKey = std::array<unsigned char, 32>;

struct SolutionChunk {
  SolutionChunkType type{SolutionChunkType::Config};
  std::vector<std::byte> payload;
};

struct SolutionArchive {
  std::uint32_t major{1};
  std::uint32_t minor{0};
  std::uint64_t features{0};
  std::vector<SolutionChunk> chunks;
};

struct SolutionIndexEntry {
  SolutionChunkType type{SolutionChunkType::Config};
  bool uses_dictionary{false};
  std::uint64_t offset{0};
  std::uint64_t stored_size{0};
  std::uint64_t compressed_size{0};
  std::uint64_t raw_size{0};
};

struct StorageMetrics {
  std::uint64_t file_size{0};
  std::uint64_t raw_size{0};
  std::uint64_t compressed_size{0};
  std::uint64_t encrypted_size{0};
  std::uint64_t peak_open_bytes{0};
  double compression_ratio{0.0};
  std::size_t chunk_count{0};
};

struct VerificationReport {
  StorageMetrics metrics;
  bool index_authenticated{false};
  bool all_chunks_authenticated{false};
  bool all_chunks_decompressed{false};
};

struct PostflopSolution {
  PostflopTreeConfig config;
  PostflopRanges ranges;
  PostflopCheckpoint checkpoint;
  PostflopCertification certification;
};

struct CatalogEntry {
  std::string path;
  std::string game_fingerprint;
  std::uint64_t file_size{0};
  std::int64_t modified_unix_ms{0};
  double normalized_nash_conv{0.0};
};

class SolutionReader {
public:
  SolutionReader() = default;

  [[nodiscard]] std::uint32_t major() const noexcept { return major_; }
  [[nodiscard]] std::uint32_t minor() const noexcept { return minor_; }
  [[nodiscard]] std::uint64_t features() const noexcept { return features_; }
  [[nodiscard]] const std::vector<SolutionIndexEntry> &index() const noexcept { return index_; }
  [[nodiscard]] const StorageMetrics &metrics() const noexcept { return metrics_; }

private:
  friend Result<SolutionReader, StorageError> open_solution(const std::filesystem::path &,
                                                            const StorageKey &);
  friend Result<std::vector<std::byte>, StorageError> read_solution_chunk(const SolutionReader &,
                                                                          SolutionChunkType);

  std::filesystem::path path_;
  StorageKey key_{};
  std::uint32_t major_{0};
  std::uint32_t minor_{0};
  std::uint64_t features_{0};
  std::vector<SolutionIndexEntry> index_;
  std::vector<std::array<unsigned char, 24>> stream_headers_;
  StorageMetrics metrics_{};
};

[[nodiscard]] Result<StorageKey, StorageError> storage_key_from_hex(std::string_view text);
[[nodiscard]] std::string storage_key_to_hex(const StorageKey &key);
[[nodiscard]] StorageKey generate_storage_key();

[[nodiscard]] Result<bool, StorageError> save_solution(const std::filesystem::path &path,
                                                       const SolutionArchive &archive,
                                                       const StorageKey &key,
                                                       int compression_level = 9);

[[nodiscard]] Result<SolutionReader, StorageError> open_solution(const std::filesystem::path &path,
                                                                 const StorageKey &key);

[[nodiscard]] Result<std::vector<std::byte>, StorageError>
read_solution_chunk(const SolutionReader &reader, SolutionChunkType type);

[[nodiscard]] Result<SolutionArchive, StorageError> load_solution(const std::filesystem::path &path,
                                                                  const StorageKey &key);

[[nodiscard]] Result<VerificationReport, StorageError>
verify_solution(const std::filesystem::path &path, const StorageKey &key);

[[nodiscard]] Result<SolutionArchive, StorageError>
make_postflop_solution(const PostflopTreeConfig &config, const PostflopCheckpoint &checkpoint,
                       const PostflopCertification &certification);

[[nodiscard]] Result<SolutionArchive, StorageError>
make_postflop_solution(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                       const PostflopCheckpoint &checkpoint,
                       const PostflopCertification &certification);

[[nodiscard]] Result<PostflopSolution, StorageError>
restore_postflop_solution(const SolutionReader &reader);

[[nodiscard]] Result<bool, StorageError> migrate_solution(const std::filesystem::path &source,
                                                          const std::filesystem::path &destination,
                                                          const StorageKey &key);

[[nodiscard]] Result<std::vector<std::byte>, StorageError>
train_zstd_dictionary(const std::vector<std::vector<std::byte>> &samples,
                      std::size_t dictionary_size);

[[nodiscard]] Result<std::vector<std::byte>, StorageError>
train_solution_dictionary(const SolutionArchive &archive, std::size_t dictionary_size);

[[nodiscard]] Result<std::vector<std::uint16_t>, StorageError>
quantize_probabilities_u16(const std::vector<double> &probabilities);

[[nodiscard]] std::vector<double>
dequantize_probabilities_u16(const std::vector<std::uint16_t> &probabilities);

[[nodiscard]] Result<bool, StorageError>
upsert_solution_catalog(const std::filesystem::path &database, const CatalogEntry &entry);

[[nodiscard]] Result<std::vector<CatalogEntry>, StorageError>
list_solution_catalog(const std::filesystem::path &database);

[[nodiscard]] const char *storage_error_name(StorageError error) noexcept;

} // namespace gtosd
