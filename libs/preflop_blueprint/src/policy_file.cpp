#include "gtosd/preflop_blueprint/policy_file.hpp"

#include "binary_io.hpp"
#include "hashing.hpp"
#include "stream_io.hpp"

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
  stream_io::Reader body(input);
  std::array<char, 8> magic{};
  if (!body.bytes(magic.data(), magic.size()) ||
      std::string_view(magic.data(), magic.size()) != policy_magic)
    return Outcome::failure(PolicyFileError::IntegrityFailure);
  std::uint32_t version = 0U;
  if (!body.u32(version) || version != policy_version) {
    return Outcome::failure(PolicyFileError::UnsupportedVersion);
  }
  ParsedPolicy parsed;
  std::uint32_t flop = 0U;
  std::uint32_t turn = 0U;
  std::uint32_t river = 0U;
  if (!body.string(parsed.info.tree_fingerprint) || !body.u32(flop) || !body.u32(turn) ||
      !body.u32(river) || !body.u64(parsed.info.entries) || !body.string(parsed.info.source)) {
    return Outcome::failure(PolicyFileError::IntegrityFailure);
  }
  // No narrowing: the file already carries 32 bit capacities and truncating
  // them silently corrupted any representation above 65,535 rows.
  parsed.info.flop_capacity = flop;
  parsed.info.turn_capacity = turn;
  parsed.info.river_capacity = river;
  if (body.remaining() % sizeof(double) != 0 ||
      parsed.info.entries != body.remaining() / sizeof(double)) {
    return Outcome::failure(PolicyFileError::IntegrityFailure);
  }
  std::uint64_t hash = 0;
  if (with_table) {
    parsed.table.resize(static_cast<std::size_t>(parsed.info.entries));
    if (!body.doubles(parsed.table, true))
      return Outcome::failure(PolicyFileError::IntegrityFailure);
    hash = detail::fnv1a_text(std::string_view(reinterpret_cast<const char *>(parsed.table.data()),
                                               parsed.table.size() * sizeof(double)));
  } else if (!body.scan_doubles(parsed.info.entries, true, &hash)) {
    return Outcome::failure(PolicyFileError::IntegrityFailure);
  }
  if (!body.finish())
    return Outcome::failure(PolicyFileError::IntegrityFailure);
  parsed.info.policy_fingerprint = "fnv1a64:" + detail::hex64_text(hash);
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

  const auto temporary = std::filesystem::path(path.string() + ".tmp");
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      return Outcome::failure(PolicyFileError::IoFailure);
    }
    const std::array<std::span<const double>, 1> arrays{policy.table()};
    if (!stream_io::write(output, buffer, arrays)) {
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
  auto policy = std::make_unique<BucketPolicy>(game, layout, std::move(value.table));
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
