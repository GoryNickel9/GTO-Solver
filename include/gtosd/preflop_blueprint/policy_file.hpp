#pragma once

#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/traversal.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <vector>

// Portable file for a bucket policy (the average strategy of the trainer):
// magic "GTOSDPOL", version, fingerprint of the compiled tree, capacities of
// the three street tables, free-text source (trainer identity, iteration),
// the dense (node, row, action) table and an FNV-1a checksum. A policy is
// loaded only for the game whose tree fingerprint it carries.
namespace gtosd::preflop_blueprint {

enum class PolicyFileError : std::uint8_t {
  IoFailure,
  IntegrityFailure,
  UnsupportedVersion,
  GameMismatch
};

struct PolicyFileInfo {
  std::string tree_fingerprint;
  std::uint32_t flop_capacity{0U};
  std::uint32_t turn_capacity{0U};
  std::uint32_t river_capacity{0U};
  std::uint64_t entries{0U};
  std::string source;
  // FNV-1a over the table bytes: equal fingerprints mean bit-identical policies.
  std::string policy_fingerprint;
};

[[nodiscard]] std::string policy_fingerprint(const BucketPolicy &policy);

// Writes atomically (temporary file then rename).
[[nodiscard]] Result<bool, PolicyFileError> save_policy(const std::filesystem::path &path,
                                                        const CompiledGame &game,
                                                        const BucketPolicy &policy,
                                                        const std::string &source);

// Same file, written incrementally: the caller appends the dense table in
// layout order through a bounded buffer instead of holding it in memory.
// finish() renames the temporary file into place and returns the policy
// fingerprint (FNV-1a over the table bytes, as policy_fingerprint does).
class PolicyStreamWriter {
public:
  PolicyStreamWriter(const std::filesystem::path &path, const CompiledGame &game,
                     const StateLayout &layout, const std::string &source);
  ~PolicyStreamWriter();
  PolicyStreamWriter(const PolicyStreamWriter &) = delete;
  PolicyStreamWriter &operator=(const PolicyStreamWriter &) = delete;
  [[nodiscard]] bool ok() const noexcept { return ok_; }
  void append(std::span<const double> values);
  [[nodiscard]] Result<std::string, PolicyFileError> finish();

private:
  void flush_buffer();
  std::filesystem::path path_;
  std::filesystem::path temporary_;
  std::ofstream output_;
  std::vector<double> buffer_;
  std::uint64_t expected_entries_{0U};
  std::uint64_t written_entries_{0U};
  std::uint64_t file_hash_{0U};
  std::uint64_t table_hash_{0U};
  bool ok_{true};
  bool finished_{false};
};

// Reads the header only (no game needed).
[[nodiscard]] Result<PolicyFileInfo, PolicyFileError>
read_policy_info(const std::filesystem::path &path);

// Loads the table into a policy of `game`; the tree fingerprint must match.
[[nodiscard]] Result<std::unique_ptr<BucketPolicy>, PolicyFileError>
load_policy(const std::filesystem::path &path, const CompiledGame &game,
            PolicyFileInfo *info = nullptr);

[[nodiscard]] const char *policy_file_error_name(PolicyFileError error) noexcept;

} // namespace gtosd::preflop_blueprint
