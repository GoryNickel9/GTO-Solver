#include "gtosd/storage/storage.hpp"

#include "gtosd/tree/config.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::uint64_t assertions = 0;

void require(const bool condition, const char *const message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(message);
  }
}

gtosd::PostflopTreeConfig load_config() {
  const auto path = std::string(GTOSD_SOURCE_DIR) + "/tests/fixtures/postflop_river_bet.json";
  std::ifstream input(path, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  const auto config = gtosd::parse_tree_config_json(text);
  require(config.has_value(), "phase8 fixture parses");
  return config.value();
}

gtosd::PostflopCheckpoint make_checkpoint() {
  gtosd::PostflopCheckpoint checkpoint;
  checkpoint.game_fingerprint = "phase8-storage-fixture-v1";
  checkpoint.completed_iterations = 25;
  checkpoint.averaging_delay = 3;
  checkpoint.action_count = 6;
  checkpoint.cumulative_regret = {0.0, 2.5, -1.0, 3.25, 0.125, -8.0};
  checkpoint.cumulative_strategy = {4.0, 6.0, 1.0, 9.0, 2.0, 8.0};
  return checkpoint;
}

gtosd::PostflopCertification make_certification() {
  gtosd::PostflopCertification certification;
  certification.iteration = 25;
  certification.profile_value_antes = {0.125, -0.13};
  certification.best_response_value_antes = {0.14, -0.11};
  certification.nash_conv_antes = 0.035;
  certification.normalized_nash_conv = 0.007;
  certification.expected_payoff_sum_antes = -0.005;
  return certification;
}

void remove_file(const std::filesystem::path &path) {
  std::error_code error;
  std::filesystem::remove(path, error);
}

void test_round_trip_random_access_and_metrics() {
  const auto config = load_config();
  const auto checkpoint = make_checkpoint();
  const auto certification = make_certification();
  const auto archive = gtosd::make_postflop_solution(config, checkpoint, certification);
  require(archive.has_value() && archive.value().chunks.size() == 8U,
          "postflop solution has all v1 chunks");

  const auto key = gtosd::generate_storage_key();
  const auto key_text = gtosd::storage_key_to_hex(key);
  const auto decoded_key = gtosd::storage_key_from_hex(key_text);
  require(decoded_key.has_value() && decoded_key.value() == key,
          "storage key hex round-trip is exact");
  require(!gtosd::storage_key_from_hex("bad"), "malformed storage key is rejected");

  const auto path = std::filesystem::current_path() / "phase8_round_trip.gtsd";
  remove_file(path);
  const auto saved = gtosd::save_solution(path, archive.value(), key);
  require(saved.has_value(), "solution saves atomically");
  const auto reader = gtosd::open_solution(path, key);
  require(reader.has_value() && reader.value().major() == 1U && reader.value().minor() == 0U &&
              reader.value().index().size() == 8U,
          "solution opens from authenticated index");
  require(reader.value().metrics().peak_open_bytes < 4096U &&
              reader.value().metrics().file_size > reader.value().metrics().peak_open_bytes,
          "root open keeps only bounded metadata resident");
  const auto config_chunk =
      gtosd::read_solution_chunk(reader.value(), gtosd::SolutionChunkType::Config);
  require(config_chunk.has_value() && !config_chunk.value().empty(),
          "single config chunk is random-access readable");

  const auto restored = gtosd::restore_postflop_solution(reader.value());
  require(restored.has_value(), "postflop solution restores");
  require(gtosd::serialize_tree_config_json(restored.value().config) ==
              gtosd::serialize_tree_config_json(config),
          "config round-trip is exact");
  require(restored.value().checkpoint.cumulative_regret == checkpoint.cumulative_regret &&
              restored.value().checkpoint.cumulative_strategy == checkpoint.cumulative_strategy &&
              restored.value().checkpoint.game_fingerprint == checkpoint.game_fingerprint,
          "strategy and checkpoint metadata round-trip losslessly");
  require(
      restored.value().certification.normalized_nash_conv == certification.normalized_nash_conv &&
          restored.value().certification.profile_value_antes == certification.profile_value_antes,
      "EV and certification round-trip losslessly");
  const auto verified = gtosd::verify_solution(path, key);
  require(verified.has_value() && verified.value().index_authenticated &&
              verified.value().all_chunks_authenticated && verified.value().all_chunks_decompressed,
          "verification authenticates and decompresses every chunk");
  require(verified.value().metrics.raw_size > verified.value().metrics.compressed_size,
          "fixture records effective Zstandard compression");

  auto wrong_key = key;
  wrong_key.front() ^= 0x80U;
  const auto wrong = gtosd::open_solution(path, wrong_key);
  require(!wrong && wrong.error() == gtosd::StorageError::AuthenticationFailed,
          "wrong key fails index authentication");
  remove_file(path);
}

void test_corruption_truncation_and_versions() {
  const auto archive =
      gtosd::make_postflop_solution(load_config(), make_checkpoint(), make_certification());
  const auto key = gtosd::generate_storage_key();
  const auto path = std::filesystem::current_path() / "phase8_corruption.gtsd";
  remove_file(path);
  require(gtosd::save_solution(path, archive.value(), key).has_value(), "corruption fixture saves");
  const auto reader = gtosd::open_solution(path, key);
  require(reader.has_value(), "corruption fixture opens");
  const auto strategy = std::find_if(reader.value().index().begin(), reader.value().index().end(),
                                     [](const gtosd::SolutionIndexEntry &entry) {
                                       return entry.type == gtosd::SolutionChunkType::Strategy;
                                     });
  require(strategy != reader.value().index().end(), "strategy index exists");
  {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekg(static_cast<std::streamoff>(strategy->offset + 1U));
    char value = 0;
    file.read(&value, 1);
    value = static_cast<char>(value ^ 0x20);
    file.seekp(static_cast<std::streamoff>(strategy->offset + 1U));
    file.write(&value, 1);
  }
  const auto corrupted =
      gtosd::read_solution_chunk(reader.value(), gtosd::SolutionChunkType::Strategy);
  require(!corrupted && corrupted.error() == gtosd::StorageError::AuthenticationFailed,
          "ciphertext bit flip is detected");

  require(gtosd::save_solution(path, archive.value(), key).has_value(),
          "valid file replaces corrupted file");
  const auto original_size = std::filesystem::file_size(path);
  std::filesystem::resize_file(path, original_size - 7U);
  const auto truncated = gtosd::open_solution(path, key);
  require(!truncated && truncated.error() == gtosd::StorageError::TruncatedFile,
          "truncated file returns typed error");

  require(gtosd::save_solution(path, archive.value(), key).has_value(),
          "version fixture is restored");
  {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(8);
    const char future_major = 2;
    file.write(&future_major, 1);
  }
  const auto future = gtosd::open_solution(path, key);
  require(!future && future.error() == gtosd::StorageError::UnsupportedVersion,
          "future major is rejected explicitly");
  remove_file(path);
}

void test_atomic_preservation_migration_and_catalog() {
  const auto archive =
      gtosd::make_postflop_solution(load_config(), make_checkpoint(), make_certification());
  const auto key = gtosd::generate_storage_key();
  const auto path = std::filesystem::current_path() / "phase8_atomic.gtsd";
  const auto migrated_path = std::filesystem::current_path() / "phase8_migrated.gtsd";
  const auto catalog_path = std::filesystem::current_path() / "phase8_catalog.gtsddb";
  remove_file(path);
  remove_file(migrated_path);
  remove_file(catalog_path);
  remove_file(catalog_path.string() + "-wal");
  remove_file(catalog_path.string() + "-shm");
  require(gtosd::save_solution(path, archive.value(), key).has_value(), "atomic baseline saves");
  const auto baseline_size = std::filesystem::file_size(path);
  auto invalid = archive.value();
  invalid.chunks.push_back(invalid.chunks.front());
  const auto rejected = gtosd::save_solution(path, invalid, key);
  require(!rejected && rejected.error() == gtosd::StorageError::DuplicateChunk &&
              std::filesystem::file_size(path) == baseline_size &&
              gtosd::verify_solution(path, key).has_value(),
          "failed save leaves previous solution intact");

  const auto migrated = gtosd::migrate_solution(path, migrated_path, key);
  require(migrated.has_value() && gtosd::verify_solution(migrated_path, key),
          "migration writes and verifies a separate destination");
  require(gtosd::verify_solution(path, key).has_value(), "migration never overwrites the source");

  gtosd::CatalogEntry entry;
  entry.path = path.string();
  entry.game_fingerprint = make_checkpoint().game_fingerprint;
  entry.file_size = baseline_size;
  entry.modified_unix_ms = 123456789;
  entry.normalized_nash_conv = make_certification().normalized_nash_conv;
  require(gtosd::upsert_solution_catalog(catalog_path, entry).has_value(),
          "SQLite catalog upsert succeeds");
  entry.file_size += 1U;
  require(gtosd::upsert_solution_catalog(catalog_path, entry).has_value(),
          "SQLite catalog conflict updates existing path");
  const auto listed = gtosd::list_solution_catalog(catalog_path);
  require(listed.has_value() && listed.value().size() == 1U &&
              listed.value().front().file_size == entry.file_size &&
              listed.value().front().game_fingerprint == entry.game_fingerprint,
          "SQLite catalog lists updated solution metadata");

  remove_file(path);
  remove_file(migrated_path);
  remove_file(catalog_path);
  remove_file(catalog_path.string() + "-wal");
  remove_file(catalog_path.string() + "-shm");
}

void test_quantization_and_dictionary_training() {
  const std::vector<double> probabilities{0.1, 0.2, 0.3, 0.4};
  const auto quantized = gtosd::quantize_probabilities_u16(probabilities);
  require(quantized.has_value(), "experimental u16 strategy quantizes");
  std::uint64_t sum = 0;
  for (const auto value : quantized.value()) {
    sum += value;
  }
  require(sum == 65535U, "quantized row preserves exact integer mass");
  const auto restored = gtosd::dequantize_probabilities_u16(quantized.value());
  require(restored.size() == probabilities.size(), "quantized row restores shape");
  for (std::size_t index = 0; index < restored.size(); ++index) {
    require(std::abs(restored[index] - probabilities[index]) <= 1.0 / 65535.0,
            "quantization error stays within one u16 unit");
  }
  require(!gtosd::quantize_probabilities_u16({0.2, 0.2}),
          "non-normalized strategy cannot be quantized");

  std::vector<std::vector<std::byte>> samples;
  for (std::size_t sample = 0; sample < 64U; ++sample) {
    std::vector<std::byte> bytes(1024U);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
      bytes[index] = static_cast<std::byte>((index * 17U + sample * 13U + index / 11U) & 0xffU);
    }
    samples.push_back(std::move(bytes));
  }
  const auto dictionary = gtosd::train_zstd_dictionary(samples, 1024U);
  require(dictionary.has_value() && dictionary.value().size() <= 1024U,
          "Zstandard dictionary trains from deterministic fixtures");

  auto archive =
      gtosd::make_postflop_solution(load_config(), make_checkpoint(), make_certification());
  require(archive.has_value(), "dictionary integration archive builds");
  archive.value().chunks.push_back({gtosd::SolutionChunkType::Dictionary, dictionary.value()});
  const auto key = gtosd::generate_storage_key();
  const auto path = std::filesystem::current_path() / "phase8_dictionary.gtsd";
  remove_file(path);
  require(gtosd::save_solution(path, archive.value(), key).has_value(),
          "trained dictionary is integrated into save");
  const auto reader = gtosd::open_solution(path, key);
  require(reader.has_value() && reader.value().index().size() == 9U,
          "dictionary solution index opens");
  const auto dictionary_entry =
      std::find_if(reader.value().index().begin(), reader.value().index().end(),
                   [](const gtosd::SolutionIndexEntry &entry) {
                     return entry.type == gtosd::SolutionChunkType::Dictionary;
                   });
  require(dictionary_entry != reader.value().index().end() && !dictionary_entry->uses_dictionary &&
              std::all_of(reader.value().index().begin(), reader.value().index().end(),
                          [](const gtosd::SolutionIndexEntry &entry) {
                            return entry.type == gtosd::SolutionChunkType::Dictionary ||
                                   entry.uses_dictionary;
                          }),
          "dictionary is standalone and all data chunks reference it");
  require(gtosd::verify_solution(path, key).has_value(),
          "dictionary-compressed chunks authenticate and decompress");
  remove_file(path);
}

void test_authenticated_mutation_corpus() {
  const auto archive =
      gtosd::make_postflop_solution(load_config(), make_checkpoint(), make_certification());
  const auto key = gtosd::generate_storage_key();
  const auto source = std::filesystem::current_path() / "phase8_mutation_source.gtsd";
  const auto mutated = std::filesystem::current_path() / "phase8_mutation_case.gtsd";
  remove_file(source);
  remove_file(mutated);
  require(gtosd::save_solution(source, archive.value(), key).has_value(),
          "mutation corpus source saves");
  std::ifstream input(source, std::ios::binary);
  const std::vector<char> baseline((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
  require(baseline.size() > 256U, "mutation corpus source is nontrivial");
  constexpr std::size_t mutation_count = 256U;
  for (std::size_t mutation = 0; mutation < mutation_count; ++mutation) {
    auto bytes = baseline;
    const auto offset = mutation * (bytes.size() - 1U) / (mutation_count - 1U);
    bytes[offset] = static_cast<char>(bytes[offset] ^ (1U << (mutation % 8U)));
    {
      std::ofstream output(mutated, std::ios::binary | std::ios::trunc);
      output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    require(!gtosd::verify_solution(mutated, key),
            "authenticated mutation is rejected without crash");
  }

  auto invalid_config_archive = archive.value();
  const auto config =
      std::find_if(invalid_config_archive.chunks.begin(), invalid_config_archive.chunks.end(),
                   [](const gtosd::SolutionChunk &chunk) {
                     return chunk.type == gtosd::SolutionChunkType::Config;
                   });
  require(config != invalid_config_archive.chunks.end(),
          "config chunk exists for semantic corruption test");
  config->payload = {std::byte{'{'}, std::byte{'}'}};
  require(gtosd::save_solution(mutated, invalid_config_archive, key).has_value(),
          "authenticated but invalid config container saves generically");
  const auto reader = gtosd::open_solution(mutated, key);
  require(reader.has_value() && !gtosd::restore_postflop_solution(reader.value()),
          "invalid authenticated config fails typed semantic restore");
  remove_file(source);
  remove_file(mutated);
}

} // namespace

int main() {
  try {
    test_round_trip_random_access_and_metrics();
    test_corruption_truncation_and_versions();
    test_atomic_preservation_migration_and_catalog();
    test_quantization_and_dictionary_training();
    test_authenticated_mutation_corpus();
    std::cout << "F8_STORAGE_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "F8_STORAGE_TESTS=FAIL assertions=" << assertions << " error=" << error.what()
              << '\n';
    return 1;
  }
}
