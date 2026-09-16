#include "gtosd/preflop_blueprint/policy_file.hpp"

#include "binary_io.hpp"
#include "hashing.hpp"

#include <cstring>
#include <memory>
#include <fstream>
#include <iterator>
#include <string_view>

namespace gtosd::preflop_blueprint {
namespace {

constexpr std::string_view policy_magic = "GTOSDPOL";
constexpr std::uint32_t policy_version = 1U;

struct ParsedPolicy {
  PolicyFileInfo info;
  std::vector<double> table;
};

Result<ParsedPolicy, PolicyFileError> parse_policy(const std::filesystem::path &path,
                                                   const bool with_table) {
  using Outcome = Result<ParsedPolicy, PolicyFileError>;
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Outcome::failure(PolicyFileError::IoFailure);
  }
  std::string data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (data.size() < policy_magic.size() + 12U ||
      std::memcmp(data.data(), policy_magic.data(), policy_magic.size()) != 0) {
    return Outcome::failure(PolicyFileError::IntegrityFailure);
  }
  const auto payload = data.size() - 8U;
  const std::string checksum_bytes = data.substr(payload);
  binary_io::Reader checksum_reader(checksum_bytes);
  std::uint64_t stored_checksum = 0U;
  if (!checksum_reader.read_little(stored_checksum) ||
      detail::fnv1a_text(std::string_view(data.data(), payload)) != stored_checksum) {
    return Outcome::failure(PolicyFileError::IntegrityFailure);
  }
  const std::string body_bytes = data.substr(policy_magic.size(), payload - policy_magic.size());
  binary_io::Reader body(body_bytes);
  std::uint32_t version = 0U;
  if (!body.read_little32(version) || version != policy_version) {
    return Outcome::failure(PolicyFileError::UnsupportedVersion);
  }
  ParsedPolicy parsed;
  std::uint32_t flop = 0U;
  std::uint32_t turn = 0U;
  std::uint32_t river = 0U;
  if (!body.read_string(parsed.info.tree_fingerprint) || !body.read_little32(flop) ||
      !body.read_little32(turn) || !body.read_little32(river) ||
      !body.read_little(parsed.info.entries) || !body.read_string(parsed.info.source)) {
    return Outcome::failure(PolicyFileError::IntegrityFailure);
  }
  parsed.info.flop_capacity = static_cast<std::uint16_t>(flop);
  parsed.info.turn_capacity = static_cast<std::uint16_t>(turn);
  parsed.info.river_capacity = static_cast<std::uint16_t>(river);
  const auto table_bytes = static_cast<std::size_t>(parsed.info.entries) * sizeof(double);
  if (body.position() + table_bytes != body_bytes.size()) {
    return Outcome::failure(PolicyFileError::IntegrityFailure);
  }
  parsed.info.policy_fingerprint =
      "fnv1a64:" + detail::hex64_text(detail::fnv1a_text(
                       std::string_view(body_bytes.data() + body.position(), table_bytes)));
  if (with_table &&
      !body.read_doubles(parsed.table, static_cast<std::size_t>(parsed.info.entries))) {
    return Outcome::failure(PolicyFileError::IntegrityFailure);
  }
  return Outcome::success(std::move(parsed));
}

} // namespace

std::string policy_fingerprint(const BucketPolicy &policy) {
  const auto &table = policy.table();
  const std::string_view bytes(reinterpret_cast<const char *>(table.data()),
                               table.size() * sizeof(double));
  return "fnv1a64:" + detail::hex64_text(detail::fnv1a_text(bytes));
}

Result<bool, PolicyFileError> save_policy(const std::filesystem::path &path,
                                          const CompiledGame &game, const BucketPolicy &policy,
                                          const std::string &source) {
  using Outcome = Result<bool, PolicyFileError>;
  const auto &layout = policy.layout();
  if (policy.table().size() != layout.entries) {
    return Outcome::failure(PolicyFileError::IntegrityFailure);
  }
  std::string buffer;
  buffer.append(policy_magic.data(), policy_magic.size());
  binary_io::append_little32(buffer, policy_version);
  binary_io::append_string(buffer, game.fingerprint());
  binary_io::append_little32(buffer, layout.flop_capacity);
  binary_io::append_little32(buffer, layout.turn_capacity);
  binary_io::append_little32(buffer, layout.river_capacity);
  binary_io::append_little(buffer, layout.entries);
  binary_io::append_string(buffer, source);
  binary_io::append_doubles(buffer, policy.table());
  binary_io::append_little(buffer, detail::fnv1a_text(buffer));

  const auto temporary = std::filesystem::path(path.string() + ".tmp");
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      return Outcome::failure(PolicyFileError::IoFailure);
    }
    output.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    output.flush();
    if (!output) {
      return Outcome::failure(PolicyFileError::IoFailure);
    }
  }
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return Outcome::failure(PolicyFileError::IoFailure);
  }
  return Outcome::success(true);
}

Result<PolicyFileInfo, PolicyFileError> read_policy_info(const std::filesystem::path &path) {
  using Outcome = Result<PolicyFileInfo, PolicyFileError>;
  auto parsed = parse_policy(path, false);
  if (!parsed) {
    return Outcome::failure(parsed.error());
  }
  return Outcome::success(std::move(parsed.value().info));
}

Result<std::unique_ptr<BucketPolicy>, PolicyFileError>
load_policy(const std::filesystem::path &path, const CompiledGame &game, PolicyFileInfo *info) {
  using Outcome = Result<std::unique_ptr<BucketPolicy>, PolicyFileError>;
  auto parsed = parse_policy(path, true);
  if (!parsed) {
    return Outcome::failure(parsed.error());
  }
  auto &value = parsed.value();
  if (value.info.tree_fingerprint != game.fingerprint()) {
    return Outcome::failure(PolicyFileError::GameMismatch);
  }
  const auto layout = layout_state(game, value.info.flop_capacity, value.info.turn_capacity,
                                   value.info.river_capacity);
  if (layout.entries != value.info.entries || value.table.size() != layout.entries) {
    return Outcome::failure(PolicyFileError::GameMismatch);
  }
  auto policy = std::make_unique<BucketPolicy>(game, layout);
  policy->table() = std::move(value.table);
  if (info != nullptr) {
    *info = value.info;
  }
  return Outcome::success(std::move(policy));
}

const char *policy_file_error_name(const PolicyFileError error) noexcept {
  switch (error) {
  case PolicyFileError::IoFailure:
    return "io_failure";
  case PolicyFileError::IntegrityFailure:
    return "integrity_failure";
  case PolicyFileError::UnsupportedVersion:
    return "unsupported_version";
  case PolicyFileError::GameMismatch:
    return "game_mismatch";
  }
  return "unknown";
}

} // namespace gtosd::preflop_blueprint
