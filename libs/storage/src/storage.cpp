#include "gtosd/storage/storage.hpp"

#include "gtosd/tree/config.hpp"

#include <sodium.h>
#include <sqlite3.h>
#include <zdict.h>
#include <zstd.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <span>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace gtosd {
namespace {

constexpr std::array<std::byte, 8> file_magic{std::byte{'G'}, std::byte{'T'}, std::byte{'S'},
                                              std::byte{'D'}, std::byte{'S'}, std::byte{'O'},
                                              std::byte{'L'}, std::byte{1}};
constexpr std::array<std::byte, 8> footer_magic{std::byte{'G'}, std::byte{'T'}, std::byte{'S'},
                                                std::byte{'D'}, std::byte{'D'}, std::byte{'O'},
                                                std::byte{'N'}, std::byte{'E'}};
constexpr std::array<std::byte, 8> strategy_magic_v1{std::byte{'G'}, std::byte{'T'}, std::byte{'S'},
                                                     std::byte{'D'}, std::byte{'S'}, std::byte{'T'},
                                                     std::byte{'R'}, std::byte{1}};
constexpr std::array<std::byte, 8> strategy_magic_v2{std::byte{'G'}, std::byte{'T'}, std::byte{'S'},
                                                     std::byte{'D'}, std::byte{'S'}, std::byte{'T'},
                                                     std::byte{'R'}, std::byte{2}};
constexpr std::array<std::byte, 8> strategy_magic_v3{std::byte{'G'}, std::byte{'T'}, std::byte{'S'},
                                                     std::byte{'D'}, std::byte{'S'}, std::byte{'T'},
                                                     std::byte{'R'}, std::byte{3}};
constexpr std::array<std::byte, 8> metrics_magic_v1{std::byte{'G'}, std::byte{'T'}, std::byte{'S'},
                                                    std::byte{'D'}, std::byte{'M'}, std::byte{'E'},
                                                    std::byte{'T'}, std::byte{1}};
constexpr std::array<std::byte, 8> metrics_magic_v2{std::byte{'G'}, std::byte{'T'}, std::byte{'S'},
                                                    std::byte{'D'}, std::byte{'M'}, std::byte{'E'},
                                                    std::byte{'T'}, std::byte{2}};
constexpr std::array<std::byte, 8> metrics_magic_v3{std::byte{'G'}, std::byte{'T'}, std::byte{'S'},
                                                    std::byte{'D'}, std::byte{'M'}, std::byte{'E'},
                                                    std::byte{'T'}, std::byte{3}};
constexpr std::array<std::byte, 8> metrics_magic_v4{std::byte{'G'}, std::byte{'T'}, std::byte{'S'},
                                                    std::byte{'D'}, std::byte{'M'}, std::byte{'E'},
                                                    std::byte{'T'}, std::byte{4}};
constexpr std::array<std::byte, 8> ev_magic{std::byte{'G'}, std::byte{'T'}, std::byte{'S'},
                                            std::byte{'D'}, std::byte{'E'}, std::byte{'V'},
                                            std::byte{0},   std::byte{1}};
constexpr std::array<std::byte, 8> ranges_magic{std::byte{'G'}, std::byte{'T'}, std::byte{'S'},
                                                std::byte{'D'}, std::byte{'R'}, std::byte{'N'},
                                                std::byte{'G'}, std::byte{1}};
constexpr std::uint32_t current_major = 1U;
constexpr std::uint32_t current_minor = 0U;
constexpr std::uint64_t required_features =
    static_cast<std::uint64_t>(SolutionFeature::Chunked) |
    static_cast<std::uint64_t>(SolutionFeature::Zstd) |
    static_cast<std::uint64_t>(SolutionFeature::Secretstream) |
    static_cast<std::uint64_t>(SolutionFeature::RandomAccess);
constexpr std::uint64_t known_features =
    required_features | static_cast<std::uint64_t>(SolutionFeature::ExactStrategy) |
    static_cast<std::uint64_t>(SolutionFeature::QuantizedStrategy);
constexpr std::size_t header_prefix_size = 28U;
constexpr std::size_t authentication_size = crypto_generichash_BYTES;
constexpr std::size_t header_size = header_prefix_size + authentication_size;
constexpr std::size_t index_entry_size = 64U;
constexpr std::size_t footer_size = footer_magic.size() + authentication_size;
constexpr std::uint32_t maximum_chunks = 64U;
constexpr std::uint64_t maximum_chunk_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;

bool initialize_sodium() noexcept {
  static const bool initialized = sodium_init() >= 0;
  return initialized;
}

template <typename Integer>
void append_integer(std::vector<std::byte> &bytes, const Integer value) {
  static_assert(std::is_unsigned_v<Integer>);
  for (std::size_t index = 0; index < sizeof(Integer); ++index) {
    bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
  }
}

template <typename Integer>
bool read_integer(const std::span<const std::byte> bytes, std::size_t &cursor, Integer &value) {
  static_assert(std::is_unsigned_v<Integer>);
  if (cursor > bytes.size() || bytes.size() - cursor < sizeof(Integer)) {
    return false;
  }
  value = 0;
  for (std::size_t index = 0; index < sizeof(Integer); ++index) {
    value |= static_cast<Integer>(std::to_integer<unsigned char>(bytes[cursor + index]))
             << (index * 8U);
  }
  cursor += sizeof(Integer);
  return true;
}

void append_double(std::vector<std::byte> &bytes, const double value) {
  append_integer(bytes, std::bit_cast<std::uint64_t>(value));
}

bool read_double(const std::span<const std::byte> bytes, std::size_t &cursor, double &value) {
  std::uint64_t bits = 0;
  if (!read_integer(bytes, cursor, bits)) {
    return false;
  }
  value = std::bit_cast<double>(bits);
  return std::isfinite(value);
}

void append_bytes(std::vector<std::byte> &destination, const void *const source,
                  const std::size_t size) {
  const auto *const begin = static_cast<const std::byte *>(source);
  destination.insert(destination.end(), begin, begin + size);
}

bool append_string(std::vector<std::byte> &bytes, const std::string_view value) {
  if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  append_integer(bytes, static_cast<std::uint32_t>(value.size()));
  append_bytes(bytes, value.data(), value.size());
  return true;
}

bool read_string(const std::span<const std::byte> bytes, std::size_t &cursor, std::string &value) {
  std::uint32_t size = 0;
  if (!read_integer(bytes, cursor, size) || cursor > bytes.size() || bytes.size() - cursor < size) {
    return false;
  }
  value.assign(reinterpret_cast<const char *>(bytes.data() + cursor), size);
  cursor += size;
  return true;
}

std::vector<std::byte> make_associated_data(const std::uint32_t major, const std::uint32_t minor,
                                            const std::uint64_t features,
                                            const SolutionChunkType type,
                                            const std::uint64_t raw_size) {
  std::vector<std::byte> result;
  result.reserve(28U);
  append_integer(result, major);
  append_integer(result, minor);
  append_integer(result, features);
  append_integer(result, static_cast<std::uint32_t>(type));
  append_integer(result, raw_size);
  return result;
}

std::vector<std::byte> build_header_prefix(const std::uint32_t major, const std::uint32_t minor,
                                           const std::uint64_t features,
                                           const std::uint32_t chunk_count) {
  std::vector<std::byte> result;
  result.reserve(header_prefix_size);
  result.insert(result.end(), file_magic.begin(), file_magic.end());
  append_integer(result, static_cast<std::uint16_t>(major));
  append_integer(result, static_cast<std::uint16_t>(minor));
  append_integer(result, features);
  append_integer(result, chunk_count);
  append_integer(result, std::uint32_t{0});
  return result;
}

std::array<unsigned char, authentication_size>
authenticate_index(const std::vector<std::byte> &header_prefix, const std::vector<std::byte> &index,
                   const StorageKey &key) {
  std::array<unsigned char, authentication_size> result{};
  crypto_generichash_state state{};
  crypto_generichash_init(&state, key.data(), key.size(), result.size());
  crypto_generichash_update(&state, reinterpret_cast<const unsigned char *>(header_prefix.data()),
                            static_cast<unsigned long long>(header_prefix.size()));
  crypto_generichash_update(&state, reinterpret_cast<const unsigned char *>(index.data()),
                            static_cast<unsigned long long>(index.size()));
  crypto_generichash_final(&state, result.data(), result.size());
  return result;
}

struct PreparedChunk {
  SolutionIndexEntry entry;
  std::array<unsigned char, crypto_secretstream_xchacha20poly1305_HEADERBYTES> stream_header{};
  std::vector<std::byte> ciphertext;
};

std::vector<std::byte> serialize_index(const std::vector<PreparedChunk> &chunks) {
  std::vector<std::byte> result;
  result.reserve(chunks.size() * index_entry_size);
  for (const auto &chunk : chunks) {
    append_integer(result, static_cast<std::uint32_t>(chunk.entry.type));
    append_integer(result, chunk.entry.uses_dictionary ? std::uint32_t{1} : std::uint32_t{0});
    append_integer(result, chunk.entry.offset);
    append_integer(result, chunk.entry.stored_size);
    append_integer(result, chunk.entry.compressed_size);
    append_integer(result, chunk.entry.raw_size);
    append_bytes(result, chunk.stream_header.data(), chunk.stream_header.size());
  }
  return result;
}

Result<PreparedChunk, StorageError>
prepare_chunk(const SolutionChunk &chunk, const StorageKey &key, const std::uint32_t major,
              const std::uint32_t minor, const std::uint64_t features, const int compression_level,
              const std::span<const std::byte> dictionary) {
  if (chunk.payload.size() > maximum_chunk_bytes) {
    return Result<PreparedChunk, StorageError>::failure(StorageError::InvalidArgument);
  }
  const auto bound = ZSTD_compressBound(chunk.payload.size());
  if (ZSTD_isError(bound) != 0U) {
    return Result<PreparedChunk, StorageError>::failure(StorageError::CompressionFailed);
  }
  std::vector<std::byte> compressed(bound);
  std::size_t compressed_size = 0;
  if (dictionary.empty()) {
    compressed_size = ZSTD_compress(compressed.data(), compressed.size(), chunk.payload.data(),
                                    chunk.payload.size(), compression_level);
  } else {
    ZSTD_CCtx *const context = ZSTD_createCCtx();
    if (context == nullptr) {
      return Result<PreparedChunk, StorageError>::failure(StorageError::CompressionFailed);
    }
    compressed_size = ZSTD_compress_usingDict(
        context, compressed.data(), compressed.size(), chunk.payload.data(), chunk.payload.size(),
        dictionary.data(), dictionary.size(), compression_level);
    ZSTD_freeCCtx(context);
  }
  if (ZSTD_isError(compressed_size) != 0U) {
    return Result<PreparedChunk, StorageError>::failure(StorageError::CompressionFailed);
  }
  compressed.resize(compressed_size);

  PreparedChunk result;
  result.entry.type = chunk.type;
  result.entry.uses_dictionary = !dictionary.empty();
  result.entry.raw_size = static_cast<std::uint64_t>(chunk.payload.size());
  result.entry.compressed_size = static_cast<std::uint64_t>(compressed.size());
  result.ciphertext.resize(compressed.size() + crypto_secretstream_xchacha20poly1305_ABYTES);
  crypto_secretstream_xchacha20poly1305_state state{};
  if (crypto_secretstream_xchacha20poly1305_init_push(&state, result.stream_header.data(),
                                                      key.data()) != 0) {
    return Result<PreparedChunk, StorageError>::failure(StorageError::AuthenticationFailed);
  }
  const auto associated_data =
      make_associated_data(major, minor, features, chunk.type, result.entry.raw_size);
  unsigned long long ciphertext_size = 0;
  if (crypto_secretstream_xchacha20poly1305_push(
          &state, reinterpret_cast<unsigned char *>(result.ciphertext.data()), &ciphertext_size,
          reinterpret_cast<const unsigned char *>(compressed.data()),
          static_cast<unsigned long long>(compressed.size()),
          reinterpret_cast<const unsigned char *>(associated_data.data()),
          static_cast<unsigned long long>(associated_data.size()),
          crypto_secretstream_xchacha20poly1305_TAG_FINAL) != 0) {
    return Result<PreparedChunk, StorageError>::failure(StorageError::AuthenticationFailed);
  }
  result.ciphertext.resize(static_cast<std::size_t>(ciphertext_size));
  result.entry.stored_size = static_cast<std::uint64_t>(result.ciphertext.size());
  return Result<PreparedChunk, StorageError>::success(std::move(result));
}

bool write_all(std::ofstream &output, const std::span<const std::byte> bytes) {
  if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
    return false;
  }
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(output);
}

bool durable_flush_file(const std::filesystem::path &path) {
#ifdef _WIN32
  const HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  const bool result = FlushFileBuffers(handle) != 0;
  CloseHandle(handle);
  return result;
#else
  const int descriptor = ::open(path.c_str(), O_RDWR);
  if (descriptor < 0) {
    return false;
  }
  const bool result = ::fsync(descriptor) == 0;
  ::close(descriptor);
  return result;
#endif
}

bool atomic_replace(const std::filesystem::path &temporary,
                    const std::filesystem::path &destination) {
#ifdef _WIN32
  constexpr std::array<DWORD, 8> retry_delays_ms{0U, 1U, 2U, 4U, 8U, 16U, 32U, 64U};
  for (const auto delay_ms : retry_delays_ms) {
    if (delay_ms != 0U) {
      Sleep(delay_ms);
    }
    std::error_code exists_error;
    const bool destination_exists = std::filesystem::exists(destination, exists_error);
    if (exists_error) {
      return false;
    }
    const bool replaced =
        destination_exists ? ReplaceFileW(destination.c_str(), temporary.c_str(), nullptr,
                                          REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != 0
                           : MoveFileExW(temporary.c_str(), destination.c_str(),
                                         MOVEFILE_WRITE_THROUGH | MOVEFILE_REPLACE_EXISTING) != 0;
    if (replaced) {
      return true;
    }
    const auto error = GetLastError();
    if (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION &&
        error != ERROR_LOCK_VIOLATION) {
      return false;
    }
  }
  return false;
#else
  std::error_code error;
  std::filesystem::rename(temporary, destination, error);
  if (error) {
    return false;
  }
  const auto directory =
      destination.parent_path().empty() ? std::filesystem::path{"."} : destination.parent_path();
  const int descriptor = ::open(directory.c_str(), O_RDONLY);
  if (descriptor >= 0) {
    static_cast<void>(::fsync(descriptor));
    ::close(descriptor);
  }
  return true;
#endif
}

std::filesystem::path temporary_path_for(const std::filesystem::path &destination) {
  std::array<unsigned char, 8> random{};
  randombytes_buf(random.data(), random.size());
  std::string suffix = ".tmp.";
  constexpr char digits[] = "0123456789abcdef";
  for (const auto value : random) {
    suffix.push_back(digits[value >> 4U]);
    suffix.push_back(digits[value & 0x0fU]);
  }
  return destination.string() + suffix;
}

bool is_known_chunk_type(const std::uint32_t raw_type) {
  return raw_type >= static_cast<std::uint32_t>(SolutionChunkType::Config) &&
         raw_type <= static_cast<std::uint32_t>(SolutionChunkType::Dictionary);
}

Result<std::vector<std::byte>, StorageError> encode_strategy(const PostflopCheckpoint &checkpoint) {
  if (checkpoint.action_count == 0U ||
      checkpoint.action_count > maximum_chunk_bytes / (2U * sizeof(double))) {
    return Result<std::vector<std::byte>, StorageError>::failure(StorageError::InvalidArgument);
  }
  std::vector<std::byte> result;
  const auto expected_values = static_cast<std::size_t>(checkpoint.action_count);
  result.reserve(strategy_magic_v1.size() + sizeof(std::uint64_t) +
                 expected_values * 2U * sizeof(double));

  if (!checkpoint.cumulative_regret.empty() || !checkpoint.cumulative_strategy.empty()) {
    if (checkpoint.state_precision != PostflopStatePrecision::Float64 ||
        checkpoint.cumulative_regret.size() != expected_values ||
        checkpoint.cumulative_strategy.size() != expected_values) {
      return Result<std::vector<std::byte>, StorageError>::failure(StorageError::InvalidArgument);
    }
    result.insert(result.end(), strategy_magic_v1.begin(), strategy_magic_v1.end());
    append_integer(result, checkpoint.action_count);
    for (const auto value : checkpoint.cumulative_regret) {
      if (!std::isfinite(value)) {
        return Result<std::vector<std::byte>, StorageError>::failure(StorageError::CorruptData);
      }
      append_double(result, value);
    }
    for (const auto value : checkpoint.cumulative_strategy) {
      if (!std::isfinite(value)) {
        return Result<std::vector<std::byte>, StorageError>::failure(StorageError::CorruptData);
      }
      append_double(result, value);
    }
    return Result<std::vector<std::byte>, StorageError>::success(std::move(result));
  }

  if (!checkpoint.cumulative_regret_float32.empty() ||
      !checkpoint.cumulative_strategy_float32.empty()) {
    if (checkpoint.state_precision != PostflopStatePrecision::Float32 ||
        checkpoint.cumulative_regret_float32.size() != expected_values ||
        checkpoint.cumulative_strategy_float32.size() != expected_values) {
      return Result<std::vector<std::byte>, StorageError>::failure(StorageError::InvalidArgument);
    }
    result.clear();
    result.reserve(strategy_magic_v2.size() + sizeof(std::uint64_t) + sizeof(std::uint8_t) +
                   expected_values * 2U * sizeof(float));
    result.insert(result.end(), strategy_magic_v2.begin(), strategy_magic_v2.end());
    append_integer(result, checkpoint.action_count);
    append_integer(result, static_cast<std::uint8_t>(checkpoint.state_precision));
    for (const auto value : checkpoint.cumulative_regret_float32) {
      if (!std::isfinite(value)) {
        return Result<std::vector<std::byte>, StorageError>::failure(StorageError::CorruptData);
      }
      append_integer(result, std::bit_cast<std::uint32_t>(value));
    }
    for (const auto value : checkpoint.cumulative_strategy_float32) {
      if (!std::isfinite(value)) {
        return Result<std::vector<std::byte>, StorageError>::failure(StorageError::CorruptData);
      }
      append_integer(result, std::bit_cast<std::uint32_t>(value));
    }
    return Result<std::vector<std::byte>, StorageError>::success(std::move(result));
  }

  if (!checkpoint.cumulative_regret_float24.empty() ||
      !checkpoint.cumulative_strategy_float16.empty()) {
    if (checkpoint.state_precision != PostflopStatePrecision::Float24RegretFloat16Strategy ||
        expected_values > std::numeric_limits<std::size_t>::max() / 3U ||
        checkpoint.cumulative_regret_float24.size() != expected_values * 3U ||
        checkpoint.cumulative_strategy_float16.size() != expected_values) {
      return Result<std::vector<std::byte>, StorageError>::failure(StorageError::InvalidArgument);
    }
    for (std::size_t offset = 0; offset < expected_values * 3U; offset += 3U) {
      const auto packed =
          static_cast<std::uint32_t>(checkpoint.cumulative_regret_float24[offset]) |
          (static_cast<std::uint32_t>(checkpoint.cumulative_regret_float24[offset + 1U]) << 8U) |
          (static_cast<std::uint32_t>(checkpoint.cumulative_regret_float24[offset + 2U]) << 16U);
      if ((packed & 0xff0000U) == 0xff0000U) {
        return Result<std::vector<std::byte>, StorageError>::failure(StorageError::CorruptData);
      }
    }
    if (std::ranges::any_of(checkpoint.cumulative_strategy_float16, [](const std::uint16_t value) {
          return (value & 0x7c00U) == 0x7c00U;
        })) {
      return Result<std::vector<std::byte>, StorageError>::failure(StorageError::CorruptData);
    }
    result.clear();
    result.reserve(strategy_magic_v2.size() + sizeof(std::uint64_t) + sizeof(std::uint8_t) +
                   expected_values * 5U);
    result.insert(result.end(), strategy_magic_v2.begin(), strategy_magic_v2.end());
    append_integer(result, checkpoint.action_count);
    append_integer(result, static_cast<std::uint8_t>(checkpoint.state_precision));
    append_bytes(result, checkpoint.cumulative_regret_float24.data(),
                 checkpoint.cumulative_regret_float24.size());
    for (const auto value : checkpoint.cumulative_strategy_float16) {
      append_integer(result, value);
    }
    return Result<std::vector<std::byte>, StorageError>::success(std::move(result));
  }

  if (!checkpoint.cumulative_regret_uint16.empty() ||
      !checkpoint.cumulative_strategy_uint16.empty() || !checkpoint.regret_node_scale.empty() ||
      !checkpoint.strategy_node_scale.empty()) {
    const auto scale_count = static_cast<std::size_t>(checkpoint.decision_node_count);
    if (checkpoint.state_precision != PostflopStatePrecision::ScaledUint16RegretStrategy ||
        checkpoint.cumulative_regret_uint16.size() != expected_values ||
        checkpoint.cumulative_strategy_uint16.size() != expected_values ||
        checkpoint.regret_node_scale.size() != scale_count ||
        checkpoint.strategy_node_scale.size() != scale_count ||
        std::ranges::any_of(
            checkpoint.regret_node_scale,
            [](const float value) { return !std::isfinite(value) || value < 0.0F; }) ||
        std::ranges::any_of(checkpoint.strategy_node_scale, [](const float value) {
          return !std::isfinite(value) || value < 0.0F;
        })) {
      return Result<std::vector<std::byte>, StorageError>::failure(StorageError::InvalidArgument);
    }
    const std::uint64_t payload_bytes = checkpoint.action_count * 2U * sizeof(std::uint16_t) +
                                        checkpoint.decision_node_count * 2U * sizeof(float);
    if (payload_bytes > maximum_chunk_bytes) {
      return Result<std::vector<std::byte>, StorageError>::failure(StorageError::InvalidArgument);
    }
    result.clear();
    result.reserve(strategy_magic_v3.size() + sizeof(std::uint64_t) + sizeof(std::uint8_t) +
                   sizeof(std::uint64_t) + static_cast<std::size_t>(payload_bytes));
    result.insert(result.end(), strategy_magic_v3.begin(), strategy_magic_v3.end());
    append_integer(result, checkpoint.action_count);
    append_integer(result, static_cast<std::uint8_t>(checkpoint.state_precision));
    append_integer(result, checkpoint.decision_node_count);
    for (const auto value : checkpoint.cumulative_regret_uint16) {
      append_integer(result, value);
    }
    for (const auto value : checkpoint.cumulative_strategy_uint16) {
      append_integer(result, value);
    }
    for (const auto value : checkpoint.regret_node_scale) {
      append_integer(result, std::bit_cast<std::uint32_t>(value));
    }
    for (const auto value : checkpoint.strategy_node_scale) {
      append_integer(result, std::bit_cast<std::uint32_t>(value));
    }
    return Result<std::vector<std::byte>, StorageError>::success(std::move(result));
  }

  if (!checkpoint.cumulative_compact_state.empty()) {
    if ((checkpoint.state_precision != PostflopStatePrecision::Float13RegretFloat11Strategy &&
         checkpoint.state_precision !=
             PostflopStatePrecision::ActionMajorFloat13RegretFloat11Strategy) ||
        expected_values > std::numeric_limits<std::size_t>::max() / 3U ||
        checkpoint.cumulative_compact_state.size() != expected_values * 3U) {
      return Result<std::vector<std::byte>, StorageError>::failure(StorageError::InvalidArgument);
    }
    for (std::size_t offset = 0; offset < expected_values * 3U; offset += 3U) {
      const auto word =
          static_cast<std::uint32_t>(checkpoint.cumulative_compact_state[offset]) |
          (static_cast<std::uint32_t>(checkpoint.cumulative_compact_state[offset + 1U]) << 8U) |
          (static_cast<std::uint32_t>(checkpoint.cumulative_compact_state[offset + 2U]) << 16U);
      if ((word & 0x1fffU) == 0x1fffU) {
        return Result<std::vector<std::byte>, StorageError>::failure(StorageError::CorruptData);
      }
    }
    result.clear();
    result.reserve(strategy_magic_v2.size() + sizeof(std::uint64_t) + sizeof(std::uint8_t) +
                   expected_values * 3U);
    result.insert(result.end(), strategy_magic_v2.begin(), strategy_magic_v2.end());
    append_integer(result, checkpoint.action_count);
    append_integer(result, static_cast<std::uint8_t>(checkpoint.state_precision));
    append_bytes(result, checkpoint.cumulative_compact_state.data(),
                 checkpoint.cumulative_compact_state.size());
    return Result<std::vector<std::byte>, StorageError>::success(std::move(result));
  }

  if (checkpoint.external_buffer_file.empty()) {
    return Result<std::vector<std::byte>, StorageError>::failure(StorageError::InvalidArgument);
  }
  std::error_code size_error;
  const auto external_size =
      std::filesystem::file_size(checkpoint.external_buffer_file, size_error);
  const auto expected_size = checkpoint.action_count * 2U * sizeof(double);
  if (size_error || external_size != expected_size) {
    return Result<std::vector<std::byte>, StorageError>::failure(StorageError::IoFailure);
  }
  std::ifstream input(checkpoint.external_buffer_file, std::ios::binary);
  if (!input) {
    return Result<std::vector<std::byte>, StorageError>::failure(StorageError::IoFailure);
  }
  std::array<double, 8192> buffer{};
  result.insert(result.end(), strategy_magic_v1.begin(), strategy_magic_v1.end());
  append_integer(result, checkpoint.action_count);
  std::uint64_t remaining = checkpoint.action_count * 2U;
  while (remaining != 0U) {
    const auto values = static_cast<std::size_t>(
        std::min<std::uint64_t>(remaining, static_cast<std::uint64_t>(buffer.size())));
    const auto byte_count = values * sizeof(double);
    input.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(byte_count));
    if (!input) {
      return Result<std::vector<std::byte>, StorageError>::failure(StorageError::IoFailure);
    }
    for (std::size_t index = 0; index < values; ++index) {
      if (!std::isfinite(buffer[index])) {
        return Result<std::vector<std::byte>, StorageError>::failure(StorageError::CorruptData);
      }
      append_double(result, buffer[index]);
    }
    remaining -= static_cast<std::uint64_t>(values);
  }
  return Result<std::vector<std::byte>, StorageError>::success(std::move(result));
}

Result<PostflopCheckpoint, StorageError> decode_strategy(const std::span<const std::byte> bytes,
                                                         PostflopCheckpoint checkpoint) {
  const bool version_1 =
      bytes.size() >= strategy_magic_v1.size() &&
      std::equal(strategy_magic_v1.begin(), strategy_magic_v1.end(), bytes.begin());
  const bool version_2 =
      bytes.size() >= strategy_magic_v2.size() &&
      std::equal(strategy_magic_v2.begin(), strategy_magic_v2.end(), bytes.begin());
  const bool version_3 =
      bytes.size() >= strategy_magic_v3.size() &&
      std::equal(strategy_magic_v3.begin(), strategy_magic_v3.end(), bytes.begin());
  if (!version_1 && !version_2 && !version_3) {
    return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
  }
  std::size_t cursor = strategy_magic_v1.size();
  std::uint64_t action_count = 0;
  if (!read_integer(bytes, cursor, action_count) || action_count == 0U ||
      action_count > std::numeric_limits<std::size_t>::max()) {
    return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
  }
  if (version_1) {
    if (action_count > maximum_chunk_bytes / (2U * sizeof(double)) ||
        bytes.size() - cursor != action_count * 2U * sizeof(double) ||
        checkpoint.state_precision != PostflopStatePrecision::Float64) {
      return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
    }
    checkpoint.action_count = action_count;
    checkpoint.external_buffer_file.clear();
    checkpoint.cumulative_regret.resize(static_cast<std::size_t>(action_count));
    checkpoint.cumulative_strategy.resize(static_cast<std::size_t>(action_count));
    for (auto &value : checkpoint.cumulative_regret) {
      if (!read_double(bytes, cursor, value)) {
        return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
      }
    }
    for (auto &value : checkpoint.cumulative_strategy) {
      if (!read_double(bytes, cursor, value)) {
        return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
      }
    }
    return Result<PostflopCheckpoint, StorageError>::success(std::move(checkpoint));
  }

  std::uint8_t encoded_precision = 0;
  if (!read_integer(bytes, cursor, encoded_precision) ||
      encoded_precision != static_cast<std::uint8_t>(checkpoint.state_precision)) {
    return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
  }
  checkpoint.action_count = action_count;
  checkpoint.external_buffer_file.clear();
  const auto count = static_cast<std::size_t>(action_count);
  if (version_3) {
    std::uint64_t encoded_decision_node_count = 0U;
    if (checkpoint.state_precision != PostflopStatePrecision::ScaledUint16RegretStrategy ||
        !read_integer(bytes, cursor, encoded_decision_node_count) ||
        encoded_decision_node_count != checkpoint.decision_node_count ||
        encoded_decision_node_count > std::numeric_limits<std::size_t>::max()) {
      return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
    }
    const auto scale_count = static_cast<std::size_t>(checkpoint.decision_node_count);
    const std::uint64_t payload_bytes = action_count * 2U * sizeof(std::uint16_t) +
                                        checkpoint.decision_node_count * 2U * sizeof(float);
    if (payload_bytes > maximum_chunk_bytes || bytes.size() - cursor != payload_bytes) {
      return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
    }
    checkpoint.cumulative_regret_uint16.resize(count);
    checkpoint.cumulative_strategy_uint16.resize(count);
    checkpoint.regret_node_scale.resize(scale_count);
    checkpoint.strategy_node_scale.resize(scale_count);
    for (auto &value : checkpoint.cumulative_regret_uint16) {
      if (!read_integer(bytes, cursor, value)) {
        return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
      }
    }
    for (auto &value : checkpoint.cumulative_strategy_uint16) {
      if (!read_integer(bytes, cursor, value)) {
        return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
      }
    }
    const auto read_scale = [&](float &value) {
      std::uint32_t bits = 0U;
      return read_integer(bytes, cursor, bits) &&
             std::isfinite(value = std::bit_cast<float>(bits)) && value >= 0.0F;
    };
    for (auto &value : checkpoint.regret_node_scale) {
      if (!read_scale(value)) {
        return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
      }
    }
    for (auto &value : checkpoint.strategy_node_scale) {
      if (!read_scale(value)) {
        return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
      }
    }
    return Result<PostflopCheckpoint, StorageError>::success(std::move(checkpoint));
  }
  if (checkpoint.state_precision == PostflopStatePrecision::Float32) {
    if (action_count > maximum_chunk_bytes / (2U * sizeof(float)) ||
        bytes.size() - cursor != action_count * 2U * sizeof(float)) {
      return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
    }
    checkpoint.cumulative_regret_float32.resize(count);
    checkpoint.cumulative_strategy_float32.resize(count);
    for (auto &value : checkpoint.cumulative_regret_float32) {
      std::uint32_t bits = 0;
      if (!read_integer(bytes, cursor, bits) ||
          !std::isfinite(value = std::bit_cast<float>(bits))) {
        return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
      }
    }
    for (auto &value : checkpoint.cumulative_strategy_float32) {
      std::uint32_t bits = 0;
      if (!read_integer(bytes, cursor, bits) ||
          !std::isfinite(value = std::bit_cast<float>(bits))) {
        return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
      }
    }
  } else if (checkpoint.state_precision == PostflopStatePrecision::Float24RegretFloat16Strategy) {
    if (action_count > maximum_chunk_bytes / 5U || bytes.size() - cursor != action_count * 5U) {
      return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
    }
    checkpoint.cumulative_regret_float24.resize(count * 3U);
    std::memcpy(checkpoint.cumulative_regret_float24.data(), bytes.data() + cursor, count * 3U);
    cursor += count * 3U;
    for (std::size_t offset = 0; offset < count * 3U; offset += 3U) {
      const auto packed =
          static_cast<std::uint32_t>(checkpoint.cumulative_regret_float24[offset]) |
          (static_cast<std::uint32_t>(checkpoint.cumulative_regret_float24[offset + 1U]) << 8U) |
          (static_cast<std::uint32_t>(checkpoint.cumulative_regret_float24[offset + 2U]) << 16U);
      if ((packed & 0xff0000U) == 0xff0000U) {
        return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
      }
    }
    checkpoint.cumulative_strategy_float16.resize(count);
    for (auto &value : checkpoint.cumulative_strategy_float16) {
      if (!read_integer(bytes, cursor, value) || (value & 0x7c00U) == 0x7c00U) {
        return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
      }
    }
  } else if (checkpoint.state_precision == PostflopStatePrecision::Float13RegretFloat11Strategy ||
             checkpoint.state_precision ==
                 PostflopStatePrecision::ActionMajorFloat13RegretFloat11Strategy) {
    if (action_count > maximum_chunk_bytes / 3U || bytes.size() - cursor != action_count * 3U) {
      return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
    }
    checkpoint.cumulative_compact_state.resize(count * 3U);
    std::memcpy(checkpoint.cumulative_compact_state.data(), bytes.data() + cursor, count * 3U);
    cursor += count * 3U;
    for (std::size_t offset = 0; offset < count * 3U; offset += 3U) {
      const auto word =
          static_cast<std::uint32_t>(checkpoint.cumulative_compact_state[offset]) |
          (static_cast<std::uint32_t>(checkpoint.cumulative_compact_state[offset + 1U]) << 8U) |
          (static_cast<std::uint32_t>(checkpoint.cumulative_compact_state[offset + 2U]) << 16U);
      if ((word & 0x1fffU) == 0x1fffU) {
        return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
      }
    }
  } else {
    return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
  }
  return Result<PostflopCheckpoint, StorageError>::success(std::move(checkpoint));
}

std::vector<std::byte> encode_certification(const PostflopCertification &certification) {
  std::vector<std::byte> result;
  result.insert(result.end(), ev_magic.begin(), ev_magic.end());
  append_integer(result, certification.iteration);
  for (const auto value : certification.profile_value_antes) {
    append_double(result, value);
  }
  for (const auto value : certification.best_response_value_antes) {
    append_double(result, value);
  }
  append_double(result, certification.nash_conv_antes);
  append_double(result, certification.normalized_nash_conv);
  append_double(result, certification.expected_payoff_sum_antes);
  return result;
}

Result<PostflopCertification, StorageError>
decode_certification(const std::span<const std::byte> bytes) {
  if (bytes.size() < ev_magic.size() ||
      !std::equal(ev_magic.begin(), ev_magic.end(), bytes.begin())) {
    return Result<PostflopCertification, StorageError>::failure(StorageError::CorruptData);
  }
  std::size_t cursor = ev_magic.size();
  PostflopCertification result;
  if (!read_integer(bytes, cursor, result.iteration)) {
    return Result<PostflopCertification, StorageError>::failure(StorageError::CorruptData);
  }
  for (auto &value : result.profile_value_antes) {
    if (!read_double(bytes, cursor, value)) {
      return Result<PostflopCertification, StorageError>::failure(StorageError::CorruptData);
    }
  }
  for (auto &value : result.best_response_value_antes) {
    if (!read_double(bytes, cursor, value)) {
      return Result<PostflopCertification, StorageError>::failure(StorageError::CorruptData);
    }
  }
  if (!read_double(bytes, cursor, result.nash_conv_antes) ||
      !read_double(bytes, cursor, result.normalized_nash_conv) ||
      !read_double(bytes, cursor, result.expected_payoff_sum_antes) || cursor != bytes.size()) {
    return Result<PostflopCertification, StorageError>::failure(StorageError::CorruptData);
  }
  return Result<PostflopCertification, StorageError>::success(result);
}

std::vector<std::byte> encode_ranges(const PostflopRanges &ranges) {
  std::vector<std::byte> result;
  result.reserve(ranges_magic.size() + 2U * 630U * sizeof(std::uint16_t));
  result.insert(result.end(), ranges_magic.begin(), ranges_magic.end());
  for (const auto &range : ranges.players) {
    for (const auto weight : range) {
      append_integer(result, weight.basis_points());
    }
  }
  return result;
}

Result<PostflopRanges, StorageError> decode_ranges(const std::span<const std::byte> bytes) {
  constexpr std::string_view legacy = "physical-combos-630:range-config-v1";
  if (bytes.size() == legacy.size() &&
      std::memcmp(bytes.data(), legacy.data(), legacy.size()) == 0) {
    return Result<PostflopRanges, StorageError>::success(make_uniform_postflop_ranges());
  }
  if (bytes.size() != ranges_magic.size() + 2U * 630U * sizeof(std::uint16_t) ||
      !std::equal(ranges_magic.begin(), ranges_magic.end(), bytes.begin())) {
    return Result<PostflopRanges, StorageError>::failure(StorageError::CorruptData);
  }
  PostflopRanges result;
  std::size_t cursor = ranges_magic.size();
  for (auto &range : result.players) {
    for (auto &weight : range) {
      std::uint16_t basis_points = 0;
      if (!read_integer(bytes, cursor, basis_points)) {
        return Result<PostflopRanges, StorageError>::failure(StorageError::CorruptData);
      }
      const auto parsed = RangeWeight::from_basis_points(basis_points);
      if (!parsed) {
        return Result<PostflopRanges, StorageError>::failure(StorageError::CorruptData);
      }
      weight = parsed.value();
    }
  }
  if (cursor != bytes.size()) {
    return Result<PostflopRanges, StorageError>::failure(StorageError::CorruptData);
  }
  return Result<PostflopRanges, StorageError>::success(std::move(result));
}

Result<std::vector<std::byte>, StorageError> encode_metrics(const PostflopCheckpoint &checkpoint) {
  const auto algorithm = static_cast<std::uint8_t>(checkpoint.algorithm);
  if ((algorithm != static_cast<std::uint8_t>(PostflopAlgorithm::CfrPlus) &&
       algorithm != static_cast<std::uint8_t>(PostflopAlgorithm::DcfrPlus) &&
       algorithm != static_cast<std::uint8_t>(PostflopAlgorithm::Dcfr) &&
       algorithm != static_cast<std::uint8_t>(PostflopAlgorithm::HsDcfr30) &&
       algorithm != static_cast<std::uint8_t>(PostflopAlgorithm::ProductionDcfr)) ||
      !std::isfinite(checkpoint.dcfr_positive_regret_exponent) ||
      !std::isfinite(checkpoint.dcfr_average_exponent) ||
      checkpoint.dcfr_positive_regret_exponent < 0.0 || checkpoint.dcfr_average_exponent < 0.0) {
    return Result<std::vector<std::byte>, StorageError>::failure(StorageError::InvalidArgument);
  }
  std::vector<std::byte> result;
  const bool scaled =
      checkpoint.state_precision == PostflopStatePrecision::ScaledUint16RegretStrategy;
  result.insert(result.end(), metrics_magic_v4.begin(), metrics_magic_v4.end());
  append_integer(result, checkpoint.major);
  append_integer(result, checkpoint.minor);
  append_integer(result, checkpoint.completed_iterations);
  append_integer(result, checkpoint.averaging_delay);
  append_integer(result, checkpoint.action_count);
  if (!append_string(result, checkpoint.game_fingerprint)) {
    return Result<std::vector<std::byte>, StorageError>::failure(StorageError::InvalidArgument);
  }
  append_integer(result, static_cast<std::uint8_t>(checkpoint.state_precision));
  if (scaled) {
    append_integer(result, checkpoint.decision_node_count);
  }
  append_integer(result, algorithm);
  append_double(result, checkpoint.dcfr_positive_regret_exponent);
  append_double(result, checkpoint.dcfr_average_exponent);
  return Result<std::vector<std::byte>, StorageError>::success(std::move(result));
}

Result<PostflopCheckpoint, StorageError> decode_metrics(const std::span<const std::byte> bytes) {
  const bool version_1 =
      bytes.size() >= metrics_magic_v1.size() &&
      std::equal(metrics_magic_v1.begin(), metrics_magic_v1.end(), bytes.begin());
  const bool version_2 =
      bytes.size() >= metrics_magic_v2.size() &&
      std::equal(metrics_magic_v2.begin(), metrics_magic_v2.end(), bytes.begin());
  const bool version_3 =
      bytes.size() >= metrics_magic_v3.size() &&
      std::equal(metrics_magic_v3.begin(), metrics_magic_v3.end(), bytes.begin());
  const bool version_4 =
      bytes.size() >= metrics_magic_v4.size() &&
      std::equal(metrics_magic_v4.begin(), metrics_magic_v4.end(), bytes.begin());
  if (!version_1 && !version_2 && !version_3 && !version_4) {
    return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
  }
  std::size_t cursor = metrics_magic_v1.size();
  PostflopCheckpoint result;
  if (!read_integer(bytes, cursor, result.major) || !read_integer(bytes, cursor, result.minor) ||
      !read_integer(bytes, cursor, result.completed_iterations) ||
      !read_integer(bytes, cursor, result.averaging_delay) ||
      !read_integer(bytes, cursor, result.action_count) ||
      !read_string(bytes, cursor, result.game_fingerprint) ||
      result.major != PostflopCheckpoint::format_major ||
      result.minor > PostflopCheckpoint::format_minor || result.game_fingerprint.empty()) {
    return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
  }
  if (version_2 || version_3 || version_4) {
    std::uint8_t precision = 0;
    if (!read_integer(bytes, cursor, precision) ||
        precision > static_cast<std::uint8_t>(
                        PostflopStatePrecision::ActionMajorFloat13RegretFloat11Strategy)) {
      return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
    }
    result.state_precision = static_cast<PostflopStatePrecision>(precision);
    if (version_3 && result.state_precision != PostflopStatePrecision::ScaledUint16RegretStrategy) {
      return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
    }
    const bool has_decision_node_count =
        version_3 ||
        (version_4 && result.state_precision == PostflopStatePrecision::ScaledUint16RegretStrategy);
    if (has_decision_node_count && !read_integer(bytes, cursor, result.decision_node_count)) {
      return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
    }
  }
  if (version_4) {
    std::uint8_t algorithm = 0U;
    double positive_exponent = 0.0;
    double average_exponent = 0.0;
    if (!read_integer(bytes, cursor, algorithm) ||
        (algorithm != static_cast<std::uint8_t>(PostflopAlgorithm::CfrPlus) &&
         algorithm != static_cast<std::uint8_t>(PostflopAlgorithm::DcfrPlus) &&
         algorithm != static_cast<std::uint8_t>(PostflopAlgorithm::Dcfr) &&
         algorithm != static_cast<std::uint8_t>(PostflopAlgorithm::HsDcfr30) &&
         algorithm != static_cast<std::uint8_t>(PostflopAlgorithm::ProductionDcfr)) ||
        !read_double(bytes, cursor, positive_exponent) ||
        !read_double(bytes, cursor, average_exponent) || !std::isfinite(positive_exponent) ||
        !std::isfinite(average_exponent) || positive_exponent < 0.0 || average_exponent < 0.0) {
      return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
    }
    result.algorithm = static_cast<PostflopAlgorithm>(algorithm);
    result.dcfr_positive_regret_exponent = positive_exponent;
    result.dcfr_average_exponent = average_exponent;
  }
  if (cursor != bytes.size()) {
    return Result<PostflopCheckpoint, StorageError>::failure(StorageError::CorruptData);
  }
  return Result<PostflopCheckpoint, StorageError>::success(std::move(result));
}

std::vector<std::byte> bytes_from_string(const std::string_view value) {
  std::vector<std::byte> result;
  append_bytes(result, value.data(), value.size());
  return result;
}

std::string string_from_bytes(const std::vector<std::byte> &value) {
  return {reinterpret_cast<const char *>(value.data()), value.size()};
}

class SqliteDatabase {
public:
  explicit SqliteDatabase(const std::filesystem::path &path) {
    if (sqlite3_open_v2(path.string().c_str(), &database_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
      if (database_ != nullptr) {
        sqlite3_close(database_);
        database_ = nullptr;
      }
    }
  }
  SqliteDatabase(const SqliteDatabase &) = delete;
  SqliteDatabase &operator=(const SqliteDatabase &) = delete;
  ~SqliteDatabase() {
    if (database_ != nullptr) {
      sqlite3_close(database_);
    }
  }
  [[nodiscard]] sqlite3 *get() const noexcept { return database_; }

private:
  sqlite3 *database_{nullptr};
};

bool ensure_catalog_schema(sqlite3 *const database) {
  constexpr const char *schema = "PRAGMA journal_mode=WAL;"
                                 "PRAGMA synchronous=FULL;"
                                 "CREATE TABLE IF NOT EXISTS solutions("
                                 "path TEXT PRIMARY KEY NOT NULL,"
                                 "game_fingerprint TEXT NOT NULL,"
                                 "file_size INTEGER NOT NULL CHECK(file_size>=0),"
                                 "modified_unix_ms INTEGER NOT NULL,"
                                 "normalized_nash_conv REAL NOT NULL);";
  return sqlite3_exec(database, schema, nullptr, nullptr, nullptr) == SQLITE_OK;
}

} // namespace

Result<StorageKey, StorageError> storage_key_from_hex(const std::string_view text) {
  if (text.size() != 64U || !initialize_sodium()) {
    return Result<StorageKey, StorageError>::failure(StorageError::InvalidArgument);
  }
  StorageKey result{};
  std::size_t decoded_size = 0;
  if (sodium_hex2bin(result.data(), result.size(), text.data(), text.size(), nullptr, &decoded_size,
                     nullptr) != 0 ||
      decoded_size != result.size()) {
    return Result<StorageKey, StorageError>::failure(StorageError::InvalidArgument);
  }
  return Result<StorageKey, StorageError>::success(result);
}

std::string storage_key_to_hex(const StorageKey &key) {
  std::array<char, 65> result{};
  sodium_bin2hex(result.data(), result.size(), key.data(), key.size());
  return result.data();
}

StorageKey generate_storage_key() {
  StorageKey result{};
  if (initialize_sodium()) {
    randombytes_buf(result.data(), result.size());
  }
  return result;
}

Result<bool, StorageError> save_solution(const std::filesystem::path &path,
                                         const SolutionArchive &archive, const StorageKey &key,
                                         const int compression_level) {
  if (!initialize_sodium() || path.empty() || archive.major != current_major ||
      archive.minor > current_minor || archive.chunks.empty() ||
      archive.chunks.size() > maximum_chunks || compression_level < ZSTD_minCLevel() ||
      compression_level > ZSTD_maxCLevel()) {
    return Result<bool, StorageError>::failure(StorageError::InvalidArgument);
  }
  const std::uint64_t features = archive.features | required_features;
  if ((features & ~known_features) != 0U ||
      ((features & static_cast<std::uint64_t>(SolutionFeature::ExactStrategy)) != 0U &&
       (features & static_cast<std::uint64_t>(SolutionFeature::QuantizedStrategy)) != 0U)) {
    return Result<bool, StorageError>::failure(StorageError::UnsupportedFeature);
  }
  std::set<SolutionChunkType> types;
  std::vector<PreparedChunk> prepared;
  prepared.reserve(archive.chunks.size());
  const auto dictionary_chunk =
      std::find_if(archive.chunks.begin(), archive.chunks.end(), [](const SolutionChunk &chunk) {
        return chunk.type == SolutionChunkType::Dictionary;
      });
  const auto dictionary = dictionary_chunk == archive.chunks.end()
                              ? std::span<const std::byte>{}
                              : std::span<const std::byte>{dictionary_chunk->payload};
  for (const auto &chunk : archive.chunks) {
    if (!types.insert(chunk.type).second) {
      return Result<bool, StorageError>::failure(StorageError::DuplicateChunk);
    }
    const auto chunk_dictionary =
        chunk.type == SolutionChunkType::Dictionary ? std::span<const std::byte>{} : dictionary;
    auto encoded = prepare_chunk(chunk, key, archive.major, archive.minor, features,
                                 compression_level, chunk_dictionary);
    if (!encoded) {
      return Result<bool, StorageError>::failure(encoded.error());
    }
    prepared.push_back(std::move(encoded.value()));
  }

  std::uint64_t offset = header_size + prepared.size() * index_entry_size;
  for (auto &chunk : prepared) {
    chunk.entry.offset = offset;
    if (chunk.entry.stored_size > std::numeric_limits<std::uint64_t>::max() - offset) {
      return Result<bool, StorageError>::failure(StorageError::InvalidArgument);
    }
    offset += chunk.entry.stored_size;
  }
  const auto header_prefix = build_header_prefix(archive.major, archive.minor, features,
                                                 static_cast<std::uint32_t>(prepared.size()));
  const auto index = serialize_index(prepared);
  const auto authentication = authenticate_index(header_prefix, index, key);
  std::vector<std::byte> header = header_prefix;
  append_bytes(header, authentication.data(), authentication.size());

  std::error_code directory_error;
  const auto parent = path.parent_path();
  if (!parent.empty() && !std::filesystem::exists(parent, directory_error)) {
    return Result<bool, StorageError>::failure(StorageError::IoFailure);
  }
  const auto temporary = temporary_path_for(path);
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output || !write_all(output, header) || !write_all(output, index)) {
      std::error_code remove_error;
      output.close();
      std::filesystem::remove(temporary, remove_error);
      return Result<bool, StorageError>::failure(StorageError::IoFailure);
    }
    for (const auto &chunk : prepared) {
      if (!write_all(output, chunk.ciphertext)) {
        std::error_code remove_error;
        output.close();
        std::filesystem::remove(temporary, remove_error);
        return Result<bool, StorageError>::failure(StorageError::IoFailure);
      }
    }
    if (!write_all(output, footer_magic)) {
      std::error_code remove_error;
      output.close();
      std::filesystem::remove(temporary, remove_error);
      return Result<bool, StorageError>::failure(StorageError::IoFailure);
    }
    output.write(reinterpret_cast<const char *>(authentication.data()),
                 static_cast<std::streamsize>(authentication.size()));
    output.flush();
    if (!output) {
      std::error_code remove_error;
      output.close();
      std::filesystem::remove(temporary, remove_error);
      return Result<bool, StorageError>::failure(StorageError::IoFailure);
    }
  }
  if (!durable_flush_file(temporary)) {
    std::error_code remove_error;
    std::filesystem::remove(temporary, remove_error);
    return Result<bool, StorageError>::failure(StorageError::IoFailure);
  }
  const auto verified = verify_solution(temporary, key);
  if (!verified || !verified.value().all_chunks_authenticated ||
      !verified.value().all_chunks_decompressed) {
    std::error_code remove_error;
    std::filesystem::remove(temporary, remove_error);
    return Result<bool, StorageError>::failure(verified ? StorageError::CorruptData
                                                        : verified.error());
  }
  if (!atomic_replace(temporary, path)) {
    std::error_code remove_error;
    std::filesystem::remove(temporary, remove_error);
    return Result<bool, StorageError>::failure(StorageError::IoFailure);
  }
  return Result<bool, StorageError>::success(true);
}

Result<SolutionReader, StorageError> open_solution(const std::filesystem::path &path,
                                                   const StorageKey &key) {
  if (!initialize_sodium() || path.empty()) {
    return Result<SolutionReader, StorageError>::failure(StorageError::InvalidArgument);
  }
  std::error_code size_error;
  const auto file_size = std::filesystem::file_size(path, size_error);
  if (size_error) {
    return Result<SolutionReader, StorageError>::failure(StorageError::IoFailure);
  }
  if (file_size < header_size + footer_size) {
    return Result<SolutionReader, StorageError>::failure(StorageError::TruncatedFile);
  }
  std::ifstream input(path, std::ios::binary);
  std::vector<std::byte> header(header_size);
  if (!input || !input.read(reinterpret_cast<char *>(header.data()),
                            static_cast<std::streamsize>(header.size()))) {
    return Result<SolutionReader, StorageError>::failure(StorageError::TruncatedFile);
  }
  if (!std::equal(file_magic.begin(), file_magic.end(), header.begin())) {
    return Result<SolutionReader, StorageError>::failure(StorageError::InvalidMagic);
  }
  std::size_t cursor = file_magic.size();
  std::uint16_t major = 0;
  std::uint16_t minor = 0;
  std::uint64_t features = 0;
  std::uint32_t chunk_count = 0;
  std::uint32_t reserved = 0;
  const auto header_span = std::span<const std::byte>(header);
  if (!read_integer(header_span, cursor, major) || !read_integer(header_span, cursor, minor) ||
      !read_integer(header_span, cursor, features) ||
      !read_integer(header_span, cursor, chunk_count) ||
      !read_integer(header_span, cursor, reserved) || reserved != 0U || chunk_count == 0U ||
      chunk_count > maximum_chunks) {
    return Result<SolutionReader, StorageError>::failure(StorageError::CorruptData);
  }
  if (major != current_major || minor > current_minor) {
    return Result<SolutionReader, StorageError>::failure(StorageError::UnsupportedVersion);
  }
  if ((features & required_features) != required_features || (features & ~known_features) != 0U) {
    return Result<SolutionReader, StorageError>::failure(StorageError::UnsupportedFeature);
  }
  std::array<unsigned char, authentication_size> stored_authentication{};
  std::memcpy(stored_authentication.data(), header.data() + header_prefix_size,
              stored_authentication.size());
  const auto index_size = static_cast<std::size_t>(chunk_count) * index_entry_size;
  std::vector<std::byte> index(index_size);
  if (!input.read(reinterpret_cast<char *>(index.data()),
                  static_cast<std::streamsize>(index.size()))) {
    return Result<SolutionReader, StorageError>::failure(StorageError::TruncatedFile);
  }
  std::vector<std::byte> header_prefix(header.begin(), header.begin() + header_prefix_size);
  const auto computed_authentication = authenticate_index(header_prefix, index, key);
  if (sodium_memcmp(stored_authentication.data(), computed_authentication.data(),
                    stored_authentication.size()) != 0) {
    return Result<SolutionReader, StorageError>::failure(StorageError::AuthenticationFailed);
  }

  SolutionReader result;
  result.path_ = path;
  result.key_ = key;
  result.major_ = major;
  result.minor_ = minor;
  result.features_ = features;
  result.index_.reserve(chunk_count);
  result.stream_headers_.reserve(chunk_count);
  std::set<SolutionChunkType> types;
  cursor = 0;
  std::uint64_t expected_offset = header_size + index_size;
  std::uint64_t total_raw = 0;
  std::uint64_t total_compressed = 0;
  std::uint64_t total_encrypted = 0;
  const auto index_span = std::span<const std::byte>(index);
  for (std::uint32_t chunk = 0; chunk < chunk_count; ++chunk) {
    std::uint32_t raw_type = 0;
    std::uint32_t flags = 0;
    SolutionIndexEntry entry;
    if (!read_integer(index_span, cursor, raw_type) || !read_integer(index_span, cursor, flags) ||
        !read_integer(index_span, cursor, entry.offset) ||
        !read_integer(index_span, cursor, entry.stored_size) ||
        !read_integer(index_span, cursor, entry.compressed_size) ||
        !read_integer(index_span, cursor, entry.raw_size) || !is_known_chunk_type(raw_type) ||
        flags > 1U || cursor > index_span.size() ||
        index_span.size() - cursor < crypto_secretstream_xchacha20poly1305_HEADERBYTES) {
      return Result<SolutionReader, StorageError>::failure(StorageError::InvalidIndex);
    }
    entry.type = static_cast<SolutionChunkType>(raw_type);
    entry.uses_dictionary = flags == 1U;
    std::array<unsigned char, crypto_secretstream_xchacha20poly1305_HEADERBYTES> stream_header{};
    std::memcpy(stream_header.data(), index.data() + cursor, stream_header.size());
    cursor += stream_header.size();
    if (!types.insert(entry.type).second || entry.offset != expected_offset ||
        expected_offset > file_size || entry.raw_size > maximum_chunk_bytes ||
        entry.compressed_size > maximum_chunk_bytes ||
        (entry.type == SolutionChunkType::Dictionary && entry.uses_dictionary) ||
        entry.stored_size != entry.compressed_size + crypto_secretstream_xchacha20poly1305_ABYTES ||
        entry.stored_size > std::numeric_limits<std::uint64_t>::max() - expected_offset ||
        entry.raw_size > std::numeric_limits<std::uint64_t>::max() - total_raw ||
        entry.compressed_size > std::numeric_limits<std::uint64_t>::max() - total_compressed ||
        entry.stored_size > std::numeric_limits<std::uint64_t>::max() - total_encrypted ||
        entry.stored_size > file_size - expected_offset) {
      return Result<SolutionReader, StorageError>::failure(StorageError::InvalidIndex);
    }
    expected_offset += entry.stored_size;
    total_raw += entry.raw_size;
    total_compressed += entry.compressed_size;
    total_encrypted += entry.stored_size;
    result.index_.push_back(entry);
    result.stream_headers_.push_back(stream_header);
  }
  const bool has_dictionary = types.contains(SolutionChunkType::Dictionary);
  if (!has_dictionary &&
      std::any_of(result.index_.begin(), result.index_.end(),
                  [](const SolutionIndexEntry &entry) { return entry.uses_dictionary; })) {
    return Result<SolutionReader, StorageError>::failure(StorageError::InvalidIndex);
  }
  if (expected_offset > file_size || file_size - expected_offset != footer_size) {
    return Result<SolutionReader, StorageError>::failure(StorageError::TruncatedFile);
  }
  input.seekg(static_cast<std::streamoff>(expected_offset));
  std::array<std::byte, footer_size> footer{};
  if (!input.read(reinterpret_cast<char *>(footer.data()),
                  static_cast<std::streamsize>(footer.size())) ||
      !std::equal(footer_magic.begin(), footer_magic.end(), footer.begin()) ||
      sodium_memcmp(footer.data() + footer_magic.size(), stored_authentication.data(),
                    stored_authentication.size()) != 0) {
    return Result<SolutionReader, StorageError>::failure(StorageError::TruncatedFile);
  }
  result.metrics_.file_size = file_size;
  result.metrics_.raw_size = total_raw;
  result.metrics_.compressed_size = total_compressed;
  result.metrics_.encrypted_size = total_encrypted;
  result.metrics_.peak_open_bytes =
      static_cast<std::uint64_t>(header.size() + index.size() + footer.size());
  result.metrics_.compression_ratio =
      total_raw == 0U ? 1.0
                      : static_cast<double>(total_compressed) / static_cast<double>(total_raw);
  result.metrics_.chunk_count = chunk_count;
  return Result<SolutionReader, StorageError>::success(std::move(result));
}

Result<std::vector<std::byte>, StorageError> read_solution_chunk(const SolutionReader &reader,
                                                                 const SolutionChunkType type) {
  const auto found =
      std::find_if(reader.index_.begin(), reader.index_.end(),
                   [type](const SolutionIndexEntry &entry) { return entry.type == type; });
  if (found == reader.index_.end()) {
    return Result<std::vector<std::byte>, StorageError>::failure(StorageError::NotFound);
  }
  const auto index = static_cast<std::size_t>(found - reader.index_.begin());
  if (found->stored_size > std::numeric_limits<std::size_t>::max() ||
      found->compressed_size > std::numeric_limits<std::size_t>::max() ||
      found->raw_size > std::numeric_limits<std::size_t>::max()) {
    return Result<std::vector<std::byte>, StorageError>::failure(StorageError::InvalidIndex);
  }
  std::ifstream input(reader.path_, std::ios::binary);
  input.seekg(static_cast<std::streamoff>(found->offset));
  std::vector<std::byte> ciphertext(static_cast<std::size_t>(found->stored_size));
  if (!input || !input.read(reinterpret_cast<char *>(ciphertext.data()),
                            static_cast<std::streamsize>(ciphertext.size()))) {
    return Result<std::vector<std::byte>, StorageError>::failure(StorageError::TruncatedFile);
  }
  crypto_secretstream_xchacha20poly1305_state state{};
  if (crypto_secretstream_xchacha20poly1305_init_pull(&state, reader.stream_headers_[index].data(),
                                                      reader.key_.data()) != 0) {
    return Result<std::vector<std::byte>, StorageError>::failure(
        StorageError::AuthenticationFailed);
  }
  std::vector<std::byte> compressed(static_cast<std::size_t>(found->compressed_size));
  const auto associated_data =
      make_associated_data(reader.major_, reader.minor_, reader.features_, type, found->raw_size);
  unsigned long long output_size = 0;
  unsigned char tag = 0;
  if (crypto_secretstream_xchacha20poly1305_pull(
          &state, reinterpret_cast<unsigned char *>(compressed.data()), &output_size, &tag,
          reinterpret_cast<const unsigned char *>(ciphertext.data()),
          static_cast<unsigned long long>(ciphertext.size()),
          reinterpret_cast<const unsigned char *>(associated_data.data()),
          static_cast<unsigned long long>(associated_data.size())) != 0 ||
      tag != crypto_secretstream_xchacha20poly1305_TAG_FINAL || output_size != compressed.size()) {
    return Result<std::vector<std::byte>, StorageError>::failure(
        StorageError::AuthenticationFailed);
  }
  std::vector<std::byte> result(static_cast<std::size_t>(found->raw_size));
  std::size_t decompressed_size = 0;
  if (!found->uses_dictionary) {
    decompressed_size =
        ZSTD_decompress(result.data(), result.size(), compressed.data(), compressed.size());
  } else {
    const auto dictionary = read_solution_chunk(reader, SolutionChunkType::Dictionary);
    if (!dictionary) {
      return Result<std::vector<std::byte>, StorageError>::failure(
          dictionary.error() == StorageError::NotFound ? StorageError::MissingChunk
                                                       : dictionary.error());
    }
    ZSTD_DCtx *const context = ZSTD_createDCtx();
    if (context == nullptr) {
      return Result<std::vector<std::byte>, StorageError>::failure(StorageError::CompressionFailed);
    }
    decompressed_size = ZSTD_decompress_usingDict(
        context, result.data(), result.size(), compressed.data(), compressed.size(),
        dictionary.value().data(), dictionary.value().size());
    ZSTD_freeDCtx(context);
  }
  if (ZSTD_isError(decompressed_size) != 0U || decompressed_size != result.size()) {
    return Result<std::vector<std::byte>, StorageError>::failure(StorageError::CompressionFailed);
  }
  return Result<std::vector<std::byte>, StorageError>::success(std::move(result));
}

Result<SolutionArchive, StorageError> load_solution(const std::filesystem::path &path,
                                                    const StorageKey &key) {
  const auto reader = open_solution(path, key);
  if (!reader) {
    return Result<SolutionArchive, StorageError>::failure(reader.error());
  }
  SolutionArchive result;
  result.major = reader.value().major();
  result.minor = reader.value().minor();
  result.features = reader.value().features();
  result.chunks.reserve(reader.value().index().size());
  for (const auto &entry : reader.value().index()) {
    auto payload = read_solution_chunk(reader.value(), entry.type);
    if (!payload) {
      return Result<SolutionArchive, StorageError>::failure(payload.error());
    }
    result.chunks.push_back({entry.type, std::move(payload.value())});
  }
  return Result<SolutionArchive, StorageError>::success(std::move(result));
}

Result<VerificationReport, StorageError> verify_solution(const std::filesystem::path &path,
                                                         const StorageKey &key) {
  const auto reader = open_solution(path, key);
  if (!reader) {
    return Result<VerificationReport, StorageError>::failure(reader.error());
  }
  VerificationReport result;
  result.metrics = reader.value().metrics();
  result.index_authenticated = true;
  for (const auto &entry : reader.value().index()) {
    const auto payload = read_solution_chunk(reader.value(), entry.type);
    if (!payload) {
      return Result<VerificationReport, StorageError>::failure(payload.error());
    }
  }
  result.all_chunks_authenticated = true;
  result.all_chunks_decompressed = true;
  return Result<VerificationReport, StorageError>::success(result);
}

Result<SolutionArchive, StorageError>
make_postflop_solution(const PostflopTreeConfig &config, const PostflopCheckpoint &checkpoint,
                       const PostflopCertification &certification) {
  return make_postflop_solution(config, make_uniform_postflop_ranges(), checkpoint, certification);
}

Result<SolutionArchive, StorageError>
make_postflop_solution(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                       const PostflopCheckpoint &checkpoint,
                       const PostflopCertification &certification) {
  const auto valid_config = validate_tree_config(config);
  const auto valid_ranges = validate_postflop_ranges(config, ranges);
  if (!valid_config || !valid_ranges || checkpoint.game_fingerprint.empty() ||
      checkpoint.completed_iterations != certification.iteration) {
    return Result<SolutionArchive, StorageError>::failure(StorageError::InvalidArgument);
  }
  auto strategy = encode_strategy(checkpoint);
  auto metrics = encode_metrics(checkpoint);
  if (!strategy || !metrics) {
    return Result<SolutionArchive, StorageError>::failure(strategy ? metrics.error()
                                                                   : strategy.error());
  }
  SolutionArchive result;
  result.major = current_major;
  result.minor = current_minor;
  result.features = required_features | static_cast<std::uint64_t>(SolutionFeature::ExactStrategy);
  result.chunks.push_back(
      {SolutionChunkType::Config, bytes_from_string(serialize_tree_config_json(config))});
  result.chunks.push_back(
      {SolutionChunkType::Tree, bytes_from_string("regenerate-from-config:public-tree-v1")});
  result.chunks.push_back(
      {SolutionChunkType::Isomorphism, bytes_from_string("global-suit-permutation-v1")});
  result.chunks.push_back({SolutionChunkType::Strategy, std::move(strategy.value())});
  result.chunks.push_back({SolutionChunkType::Ev, encode_certification(certification)});
  result.chunks.push_back({SolutionChunkType::Ranges, encode_ranges(ranges)});
  result.chunks.push_back({SolutionChunkType::Nodelocks, bytes_from_string("nodelocks-v1:none")});
  result.chunks.push_back({SolutionChunkType::Metrics, std::move(metrics.value())});
  return Result<SolutionArchive, StorageError>::success(std::move(result));
}

Result<PostflopSolution, StorageError> restore_postflop_solution(const SolutionReader &reader) {
  const auto config_bytes = read_solution_chunk(reader, SolutionChunkType::Config);
  const auto strategy_bytes = read_solution_chunk(reader, SolutionChunkType::Strategy);
  const auto ev_bytes = read_solution_chunk(reader, SolutionChunkType::Ev);
  const auto ranges_bytes = read_solution_chunk(reader, SolutionChunkType::Ranges);
  const auto metrics_bytes = read_solution_chunk(reader, SolutionChunkType::Metrics);
  if (!config_bytes || !strategy_bytes || !ev_bytes || !ranges_bytes || !metrics_bytes) {
    const auto error = !config_bytes     ? config_bytes.error()
                       : !strategy_bytes ? strategy_bytes.error()
                       : !ev_bytes       ? ev_bytes.error()
                       : !ranges_bytes   ? ranges_bytes.error()
                                         : metrics_bytes.error();
    return Result<PostflopSolution, StorageError>::failure(error);
  }
  const auto config = parse_tree_config_json(string_from_bytes(config_bytes.value()));
  auto checkpoint = decode_metrics(metrics_bytes.value());
  const auto certification = decode_certification(ev_bytes.value());
  auto ranges = decode_ranges(ranges_bytes.value());
  if (!config || !checkpoint || !certification || !ranges ||
      !validate_postflop_ranges(config.value(), ranges.value())) {
    return Result<PostflopSolution, StorageError>::failure(StorageError::CorruptData);
  }
  checkpoint = decode_strategy(strategy_bytes.value(), std::move(checkpoint.value()));
  if (!checkpoint || checkpoint.value().completed_iterations != certification.value().iteration) {
    return Result<PostflopSolution, StorageError>::failure(StorageError::CorruptData);
  }
  return Result<PostflopSolution, StorageError>::success({config.value(), std::move(ranges.value()),
                                                          std::move(checkpoint.value()),
                                                          certification.value()});
}

Result<bool, StorageError> migrate_solution(const std::filesystem::path &source,
                                            const std::filesystem::path &destination,
                                            const StorageKey &key) {
  if (source.empty() || destination.empty() || source == destination) {
    return Result<bool, StorageError>::failure(StorageError::InvalidArgument);
  }
  const auto archive = load_solution(source, key);
  if (!archive) {
    return Result<bool, StorageError>::failure(archive.error());
  }
  if (archive.value().major != current_major || archive.value().minor > current_minor) {
    return Result<bool, StorageError>::failure(StorageError::MigrationFailed);
  }
  return save_solution(destination, archive.value(), key);
}

Result<std::vector<std::byte>, StorageError>
train_zstd_dictionary(const std::vector<std::vector<std::byte>> &samples,
                      const std::size_t dictionary_size) {
  if (samples.size() < 2U || samples.size() > std::numeric_limits<unsigned int>::max() ||
      dictionary_size < 256U) {
    return Result<std::vector<std::byte>, StorageError>::failure(StorageError::InvalidArgument);
  }
  std::size_t total_size = 0;
  std::vector<std::size_t> sizes;
  sizes.reserve(samples.size());
  for (const auto &sample : samples) {
    if (sample.empty() || sample.size() > std::numeric_limits<std::size_t>::max() - total_size) {
      return Result<std::vector<std::byte>, StorageError>::failure(StorageError::InvalidArgument);
    }
    total_size += sample.size();
    sizes.push_back(sample.size());
  }
  std::vector<std::byte> joined;
  joined.reserve(total_size);
  for (const auto &sample : samples) {
    joined.insert(joined.end(), sample.begin(), sample.end());
  }
  std::vector<std::byte> dictionary(dictionary_size);
  const auto trained = ZDICT_trainFromBuffer(dictionary.data(), dictionary.size(), joined.data(),
                                             sizes.data(), static_cast<unsigned int>(sizes.size()));
  if (ZDICT_isError(trained) != 0U) {
    return Result<std::vector<std::byte>, StorageError>::failure(StorageError::CompressionFailed);
  }
  dictionary.resize(trained);
  return Result<std::vector<std::byte>, StorageError>::success(std::move(dictionary));
}

Result<std::vector<std::byte>, StorageError>
train_solution_dictionary(const SolutionArchive &archive, const std::size_t dictionary_size) {
  constexpr std::size_t sample_size = 16U * 1024U;
  constexpr std::size_t samples_per_chunk = 256U;
  std::vector<std::vector<std::byte>> samples;
  for (const auto &chunk : archive.chunks) {
    if (chunk.type == SolutionChunkType::Dictionary || chunk.payload.empty()) {
      continue;
    }
    const auto count =
        std::min(samples_per_chunk, (chunk.payload.size() + sample_size - 1U) / sample_size);
    for (std::size_t sample = 0; sample < count; ++sample) {
      const auto maximum_offset =
          chunk.payload.size() > sample_size ? chunk.payload.size() - sample_size : 0U;
      const auto offset = count == 1U ? 0U : maximum_offset * sample / (count - 1U);
      const auto size = std::min(sample_size, chunk.payload.size() - offset);
      samples.emplace_back(chunk.payload.begin() + static_cast<std::ptrdiff_t>(offset),
                           chunk.payload.begin() + static_cast<std::ptrdiff_t>(offset + size));
    }
  }
  return train_zstd_dictionary(samples, dictionary_size);
}

Result<std::vector<std::uint16_t>, StorageError>
quantize_probabilities_u16(const std::vector<double> &probabilities) {
  if (probabilities.empty()) {
    return Result<std::vector<std::uint16_t>, StorageError>::failure(StorageError::InvalidArgument);
  }
  double sum = 0.0;
  for (const auto value : probabilities) {
    if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
      return Result<std::vector<std::uint16_t>, StorageError>::failure(
          StorageError::InvalidArgument);
    }
    sum += value;
  }
  if (std::abs(sum - 1.0) > 1e-9) {
    return Result<std::vector<std::uint16_t>, StorageError>::failure(StorageError::InvalidArgument);
  }
  std::vector<std::uint16_t> result;
  result.reserve(probabilities.size());
  std::uint64_t quantized_sum = 0;
  for (const auto value : probabilities) {
    const auto quantized = static_cast<std::uint16_t>(std::llround(value * 65535.0));
    result.push_back(quantized);
    quantized_sum += quantized;
  }
  if (quantized_sum != 65535U) {
    const auto maximum =
        static_cast<std::size_t>(std::max_element(result.begin(), result.end()) - result.begin());
    const auto correction =
        static_cast<std::int64_t>(65535U) - static_cast<std::int64_t>(quantized_sum);
    const auto corrected = static_cast<std::int64_t>(result[maximum]) + correction;
    if (corrected < 0 || corrected > 65535) {
      return Result<std::vector<std::uint16_t>, StorageError>::failure(StorageError::CorruptData);
    }
    result[maximum] = static_cast<std::uint16_t>(corrected);
  }
  return Result<std::vector<std::uint16_t>, StorageError>::success(std::move(result));
}

std::vector<double> dequantize_probabilities_u16(const std::vector<std::uint16_t> &probabilities) {
  std::vector<double> result;
  result.reserve(probabilities.size());
  for (const auto value : probabilities) {
    result.push_back(static_cast<double>(value) / 65535.0);
  }
  return result;
}

Result<bool, StorageError> upsert_solution_catalog(const std::filesystem::path &database,
                                                   const CatalogEntry &entry) {
  if (database.empty() || entry.path.empty() || entry.game_fingerprint.empty() ||
      !std::isfinite(entry.normalized_nash_conv)) {
    return Result<bool, StorageError>::failure(StorageError::InvalidArgument);
  }
  SqliteDatabase connection(database);
  if (connection.get() == nullptr || !ensure_catalog_schema(connection.get())) {
    return Result<bool, StorageError>::failure(StorageError::CatalogFailure);
  }
  constexpr const char *statement_text =
      "INSERT INTO solutions(path,game_fingerprint,file_size,modified_unix_ms,"
      "normalized_nash_conv) VALUES(?,?,?,?,?) "
      "ON CONFLICT(path) DO UPDATE SET "
      "game_fingerprint=excluded.game_fingerprint,file_size=excluded.file_size,"
      "modified_unix_ms=excluded.modified_unix_ms,"
      "normalized_nash_conv=excluded.normalized_nash_conv;";
  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(connection.get(), statement_text, -1, &statement, nullptr) != SQLITE_OK) {
    return Result<bool, StorageError>::failure(StorageError::CatalogFailure);
  }
  const auto finalize = [&statement]() { sqlite3_finalize(statement); };
  const bool bound =
      sqlite3_bind_text(statement, 1, entry.path.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
      sqlite3_bind_text(statement, 2, entry.game_fingerprint.c_str(), -1, SQLITE_TRANSIENT) ==
          SQLITE_OK &&
      sqlite3_bind_int64(statement, 3, static_cast<sqlite3_int64>(entry.file_size)) == SQLITE_OK &&
      sqlite3_bind_int64(statement, 4, static_cast<sqlite3_int64>(entry.modified_unix_ms)) ==
          SQLITE_OK &&
      sqlite3_bind_double(statement, 5, entry.normalized_nash_conv) == SQLITE_OK;
  if (!bound || sqlite3_step(statement) != SQLITE_DONE) {
    finalize();
    return Result<bool, StorageError>::failure(StorageError::CatalogFailure);
  }
  finalize();
  return Result<bool, StorageError>::success(true);
}

Result<std::vector<CatalogEntry>, StorageError>
list_solution_catalog(const std::filesystem::path &database) {
  if (database.empty()) {
    return Result<std::vector<CatalogEntry>, StorageError>::failure(StorageError::InvalidArgument);
  }
  SqliteDatabase connection(database);
  if (connection.get() == nullptr || !ensure_catalog_schema(connection.get())) {
    return Result<std::vector<CatalogEntry>, StorageError>::failure(StorageError::CatalogFailure);
  }
  constexpr const char *statement_text =
      "SELECT path,game_fingerprint,file_size,modified_unix_ms,normalized_nash_conv "
      "FROM solutions ORDER BY modified_unix_ms DESC,path ASC;";
  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(connection.get(), statement_text, -1, &statement, nullptr) != SQLITE_OK) {
    return Result<std::vector<CatalogEntry>, StorageError>::failure(StorageError::CatalogFailure);
  }
  std::vector<CatalogEntry> result;
  int step = SQLITE_ROW;
  while ((step = sqlite3_step(statement)) == SQLITE_ROW) {
    const auto *const path = sqlite3_column_text(statement, 0);
    const auto *const fingerprint = sqlite3_column_text(statement, 1);
    const auto file_size = sqlite3_column_int64(statement, 2);
    if (path == nullptr || fingerprint == nullptr || file_size < 0) {
      sqlite3_finalize(statement);
      return Result<std::vector<CatalogEntry>, StorageError>::failure(StorageError::CatalogFailure);
    }
    CatalogEntry entry;
    entry.path = reinterpret_cast<const char *>(path);
    entry.game_fingerprint = reinterpret_cast<const char *>(fingerprint);
    entry.file_size = static_cast<std::uint64_t>(file_size);
    entry.modified_unix_ms = sqlite3_column_int64(statement, 3);
    entry.normalized_nash_conv = sqlite3_column_double(statement, 4);
    result.push_back(std::move(entry));
  }
  sqlite3_finalize(statement);
  if (step != SQLITE_DONE) {
    return Result<std::vector<CatalogEntry>, StorageError>::failure(StorageError::CatalogFailure);
  }
  return Result<std::vector<CatalogEntry>, StorageError>::success(std::move(result));
}

const char *storage_error_name(const StorageError error) noexcept {
  switch (error) {
  case StorageError::InvalidArgument:
    return "InvalidArgument";
  case StorageError::IoFailure:
    return "IoFailure";
  case StorageError::InvalidMagic:
    return "InvalidMagic";
  case StorageError::TruncatedFile:
    return "TruncatedFile";
  case StorageError::UnsupportedVersion:
    return "UnsupportedVersion";
  case StorageError::UnsupportedFeature:
    return "UnsupportedFeature";
  case StorageError::InvalidIndex:
    return "InvalidIndex";
  case StorageError::DuplicateChunk:
    return "DuplicateChunk";
  case StorageError::MissingChunk:
    return "MissingChunk";
  case StorageError::AuthenticationFailed:
    return "AuthenticationFailed";
  case StorageError::CompressionFailed:
    return "CompressionFailed";
  case StorageError::CorruptData:
    return "CorruptData";
  case StorageError::MigrationFailed:
    return "MigrationFailed";
  case StorageError::CatalogFailure:
    return "CatalogFailure";
  case StorageError::NotFound:
    return "NotFound";
  }
  return "UnknownStorageError";
}

} // namespace gtosd
