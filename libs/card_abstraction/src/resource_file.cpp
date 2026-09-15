#include "resource_file.hpp"

#include <fstream>
#include <iterator>
#include <system_error>

namespace gtosd::card_abstraction::detail {
namespace {

constexpr std::string_view resource_magic = "GTOSDRES";

} // namespace

std::uint64_t fnv1a(const std::span<const std::uint8_t> bytes, std::uint64_t hash) noexcept {
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= fnv_prime;
  }
  return hash;
}

std::uint64_t fnv1a_text(const std::string_view text, std::uint64_t hash) noexcept {
  for (const auto character : text) {
    hash ^= static_cast<std::uint8_t>(character);
    hash *= fnv_prime;
  }
  return hash;
}

std::string hex64(std::uint64_t value) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result(16U, '0');
  for (std::size_t index = 0; index < result.size(); ++index) {
    result[result.size() - 1U - index] = digits[static_cast<std::size_t>(value & 0xFU)];
    value >>= 4U;
  }
  return result;
}

Result<bool, ResourceError> write_resource(const std::filesystem::path &path,
                                           const std::string_view kind,
                                           const std::uint32_t version,
                                           const std::string_view fingerprint,
                                           const std::span<const std::uint8_t> payload) {
  std::vector<std::uint8_t> header;
  for (const auto character : resource_magic) {
    header.push_back(static_cast<std::uint8_t>(character));
  }
  append_little(header, version);
  append_little(header, static_cast<std::uint32_t>(kind.size()));
  for (const auto character : kind) {
    header.push_back(static_cast<std::uint8_t>(character));
  }
  append_little(header, static_cast<std::uint32_t>(fingerprint.size()));
  for (const auto character : fingerprint) {
    header.push_back(static_cast<std::uint8_t>(character));
  }
  append_little(header, static_cast<std::uint64_t>(payload.size()));
  auto checksum = fnv1a(header);
  checksum = fnv1a(payload, checksum);
  std::vector<std::uint8_t> trailer;
  append_little(trailer, checksum);

  const auto temporary = path.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      return Result<bool, ResourceError>::failure(ResourceError::IoFailure);
    }
    output.write(reinterpret_cast<const char *>(header.data()),
                 static_cast<std::streamsize>(header.size()));
    output.write(reinterpret_cast<const char *>(payload.data()),
                 static_cast<std::streamsize>(payload.size()));
    output.write(reinterpret_cast<const char *>(trailer.data()),
                 static_cast<std::streamsize>(trailer.size()));
    if (!output) {
      return Result<bool, ResourceError>::failure(ResourceError::IoFailure);
    }
  }
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return Result<bool, ResourceError>::failure(ResourceError::IoFailure);
  }
  return Result<bool, ResourceError>::success(true);
}

Result<ResourcePayload, ResourceError> read_resource(const std::filesystem::path &path,
                                                     const std::string_view expected_kind,
                                                     const std::uint32_t expected_version) {
  using Loaded = Result<ResourcePayload, ResourceError>;
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Loaded::failure(ResourceError::IoFailure);
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                  std::istreambuf_iterator<char>());
  if (bytes.size() < resource_magic.size() + 3U * sizeof(std::uint32_t) + 2U * sizeof(std::uint64_t)) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  const auto payload_end = bytes.size() - sizeof(std::uint64_t);
  std::size_t position = payload_end;
  std::uint64_t stored_checksum = 0U;
  if (!read_little(std::span<const std::uint8_t>(bytes), position, stored_checksum) ||
      fnv1a(std::span<const std::uint8_t>(bytes.data(), payload_end)) != stored_checksum) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  for (std::size_t index = 0U; index < resource_magic.size(); ++index) {
    if (bytes[index] != static_cast<std::uint8_t>(resource_magic[index])) {
      return Loaded::failure(ResourceError::IntegrityFailure);
    }
  }
  position = resource_magic.size();
  ResourcePayload payload;
  std::uint32_t kind_size = 0U;
  std::uint32_t fingerprint_size = 0U;
  std::uint64_t payload_size = 0U;
  const std::span<const std::uint8_t> view(bytes);
  if (!read_little(view, position, payload.version) || !read_little(view, position, kind_size) ||
      position + kind_size > payload_end) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  payload.kind.assign(reinterpret_cast<const char *>(bytes.data() + position), kind_size);
  position += kind_size;
  if (!read_little(view, position, fingerprint_size) || position + fingerprint_size > payload_end) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  payload.fingerprint.assign(reinterpret_cast<const char *>(bytes.data() + position),
                             fingerprint_size);
  position += fingerprint_size;
  if (!read_little(view, position, payload_size) || position + payload_size != payload_end) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  if (payload.kind != expected_kind) {
    return Loaded::failure(ResourceError::IntegrityFailure);
  }
  if (payload.version != expected_version) {
    return Loaded::failure(ResourceError::UnsupportedVersion);
  }
  payload.bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(position),
                       bytes.begin() + static_cast<std::ptrdiff_t>(payload_end));
  return Loaded::success(std::move(payload));
}

} // namespace gtosd::card_abstraction::detail
