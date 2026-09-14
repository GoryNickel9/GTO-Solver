#include "gtosd/preflop/hu_preflop.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <new>
#include <sstream>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gtosd {
namespace {

using Json = nlohmann::json;
constexpr std::string_view file_marker = "GTOSD_HU_PREFLOP_SAMPLED_POSTFLOP_POLICY_FILE";

bool supported_policy_version(const HuPreflopSampledPostflopPolicy &policy) noexcept {
  return policy.major == HuPreflopSampledPostflopPolicy::format_major &&
         policy.minor >= HuPreflopSampledPostflopPolicy::minimum_supported_minor &&
         policy.minor <= HuPreflopSampledPostflopPolicy::format_minor &&
         (policy.representation !=
              HuPreflopPostflopRepresentation::DistributionalStrengthStructuredV7 ||
          policy.minor >= 6U) &&
         (policy.representation !=
              HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptiveV8 ||
          policy.minor >= 7U) &&
         (policy.representation !=
              HuPreflopPostflopRepresentation::DistributionalStrengthSelectiveHistoryV9 ||
          policy.minor >= 8U) &&
         (policy.representation !=
              HuPreflopPostflopRepresentation::DistributionalStrengthCategoryHistoryV10 ||
          policy.minor >= 9U) &&
         (policy.representation != HuPreflopPostflopRepresentation::
                                       DistributionalStrengthAdaptiveCategoryHistoryV11 ||
          policy.minor >= 10U);
}

std::string checksum(const std::string_view bytes) {
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  for (const unsigned char byte : bytes) {
    hash ^= byte;
    hash *= 1'099'511'628'211ULL;
  }
  std::ostringstream output;
  output << std::hex << std::setw(16) << std::setfill('0') << hash;
  return output.str();
}

std::string wrap_file(const std::string &payload) {
  std::ostringstream output;
  output << file_marker << ' ' << payload.size() << ' ' << checksum(payload) << '\n' << payload;
  return output.str();
}

Result<std::string, HuPreflopError> unwrap_file(const std::string &serialized) {
  const auto line_end = serialized.find('\n');
  if (line_end == std::string::npos) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  std::istringstream header(serialized.substr(0U, line_end));
  std::string marker;
  std::uint64_t payload_size = 0U;
  std::string expected_checksum;
  if (!(header >> marker >> payload_size >> expected_checksum) || marker != file_marker) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  header >> std::ws;
  if (!header.eof() || payload_size != serialized.size() - line_end - 1U) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  auto payload = serialized.substr(line_end + 1U);
  if (checksum(payload) != expected_checksum) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<std::string, HuPreflopError>::success(std::move(payload));
}

bool atomic_replace(const std::filesystem::path &temporary,
                    const std::filesystem::path &destination) {
#if defined(_WIN32)
  return MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  std::error_code error;
  std::filesystem::rename(temporary, destination, error);
  return !error;
#endif
}

Result<bool, HuPreflopError> save_payload(const std::string &path, const std::string &contents) {
  if (path.empty() ||
      contents.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IoFailure);
  }
  const std::filesystem::path destination(path);
  auto temporary = destination;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output || !output.write(contents.data(), static_cast<std::streamsize>(contents.size())) ||
        !output.flush()) {
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      return Result<bool, HuPreflopError>::failure(HuPreflopError::IoFailure);
    }
  }
  if (!atomic_replace(temporary, destination)) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IoFailure);
  }
  return Result<bool, HuPreflopError>::success(true);
}

Result<std::string, HuPreflopError>
load_payload_bounded(const std::string &path, const std::uint64_t maximum_payload_bytes) {
  if (path.empty() || maximum_payload_bytes == 0U ||
      maximum_payload_bytes > std::numeric_limits<std::uint64_t>::max() - 256U) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size > maximum_payload_bytes + 256U ||
      size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
    return Result<std::string, HuPreflopError>::failure(error ? HuPreflopError::IoFailure
                                                              : HuPreflopError::MemoryFailure);
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IoFailure);
  }
  std::string contents(static_cast<std::size_t>(size), '\0');
  if (!contents.empty() &&
      !input.read(contents.data(), static_cast<std::streamsize>(contents.size()))) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IoFailure);
  }
  return Result<std::string, HuPreflopError>::success(std::move(contents));
}

Json policy_json(const HuPreflopSampledPostflopPolicy &policy) {
  Json entries = Json::array();
  for (const auto &entry : policy.entries) {
    Json item{{"public_history", entry.key.public_history},
              {"physical_cards", entry.key.physical_cards},
              {"bucket_history", entry.key.bucket_history},
              {"preflop_class", entry.key.preflop_class},
              {"player", entry.key.player},
              {"street", static_cast<std::uint8_t>(entry.key.street)},
              {"action_count", entry.action_count},
              {"probabilities", entry.probabilities}};
    if (policy.minor >= 11U) {
      item["current_probabilities"] = entry.current_probabilities;
    }
    entries.push_back(std::move(item));
  }
  Json result{{"schema", "gtosd.hu_preflop_sampled_postflop_policy.v1"},
              {"major", policy.major},
              {"minor", policy.minor},
              {"tree_fingerprint", policy.tree_fingerprint},
              {"algorithm", policy.algorithm},
              {"abstraction_id", policy.abstraction_id},
              {"iterations", policy.iterations},
              {"seed", policy.seed},
              {"partition_seed", policy.partition_seed},
              {"equity_samples_per_bucket", policy.equity_samples_per_bucket},
              {"distributional_bucket_capacities", policy.distributional_bucket_capacities},
              {"representation", static_cast<std::uint8_t>(policy.representation)},
              {"missing_infoset_fallback",
               static_cast<std::uint8_t>(policy.missing_infoset_fallback)},
              {"entries", std::move(entries)},
              {"fingerprint", policy.fingerprint}};
  if (policy.minor >= 11U) {
    result["current_policy_present"] = policy.current_policy_present;
  }
  return result;
}

} // namespace

Result<std::string, HuPreflopError>
serialize_hu_preflop_sampled_postflop_policy(const HuPreflopSampledPostflopPolicy &policy) {
  if (!supported_policy_version(policy) || policy.fingerprint.empty() ||
      policy.fingerprint != fingerprint_hu_preflop_sampled_postflop_policy(policy)) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  try {
    return Result<std::string, HuPreflopError>::success(policy_json(policy).dump());
  } catch (const std::bad_alloc &) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::MemoryFailure);
  }
}

Result<HuPreflopSampledPostflopPolicy, HuPreflopError>
deserialize_hu_preflop_sampled_postflop_policy(const HuPreflopTree &tree,
                                               const std::string &serialized,
                                               const std::uint64_t maximum_payload_bytes) {
  if (maximum_payload_bytes == 0U || serialized.empty() ||
      static_cast<std::uint64_t>(serialized.size()) > maximum_payload_bytes) {
    return Result<HuPreflopSampledPostflopPolicy, HuPreflopError>::failure(
        HuPreflopError::MemoryFailure);
  }
  try {
    const auto source = Json::parse(serialized);
    if (source.at("schema") != "gtosd.hu_preflop_sampled_postflop_policy.v1") {
      return Result<HuPreflopSampledPostflopPolicy, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    HuPreflopSampledPostflopPolicy result;
    result.major = source.at("major").get<std::uint32_t>();
    result.minor = source.at("minor").get<std::uint32_t>();
    if (result.major != HuPreflopSampledPostflopPolicy::format_major ||
        result.minor < HuPreflopSampledPostflopPolicy::minimum_supported_minor ||
        result.minor > HuPreflopSampledPostflopPolicy::format_minor) {
      return Result<HuPreflopSampledPostflopPolicy, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    result.tree_fingerprint = source.at("tree_fingerprint").get<std::string>();
    result.algorithm = source.at("algorithm").get<std::string>();
    result.abstraction_id = source.at("abstraction_id").get<std::string>();
    result.iterations = source.at("iterations").get<std::uint64_t>();
    result.seed = source.at("seed").get<std::uint64_t>();
    result.partition_seed = source.at("partition_seed").get<std::uint64_t>();
    result.equity_samples_per_bucket = source.at("equity_samples_per_bucket").get<std::uint32_t>();
    result.distributional_bucket_capacities =
        source.at("distributional_bucket_capacities")
            .get<decltype(result.distributional_bucket_capacities)>();
    const auto representation = source.at("representation").get<std::uint8_t>();
    const auto fallback = source.at("missing_infoset_fallback").get<std::uint8_t>();
    if (representation >
            static_cast<std::uint8_t>(
                HuPreflopPostflopRepresentation::
                    DistributionalStrengthAdaptiveCategoryHistoryV11) ||
        (representation == static_cast<std::uint8_t>(
                               HuPreflopPostflopRepresentation::DistributionalStrengthStructuredV7) &&
         result.minor < 6U) ||
        (representation == static_cast<std::uint8_t>(
                               HuPreflopPostflopRepresentation::
                                   DistributionalStrengthStreetAdaptiveV8) &&
         result.minor < 7U) ||
        (representation == static_cast<std::uint8_t>(
                               HuPreflopPostflopRepresentation::
                                   DistributionalStrengthSelectiveHistoryV9) &&
         result.minor < 8U) ||
        (representation == static_cast<std::uint8_t>(
                               HuPreflopPostflopRepresentation::
                                   DistributionalStrengthCategoryHistoryV10) &&
         result.minor < 9U) ||
        (representation == static_cast<std::uint8_t>(
                               HuPreflopPostflopRepresentation::
                                   DistributionalStrengthAdaptiveCategoryHistoryV11) &&
         result.minor < 10U) ||
        fallback != static_cast<std::uint8_t>(HuPreflopSampledPolicyFallback::Uniform) ||
        !source.at("entries").is_array()) {
      return Result<HuPreflopSampledPostflopPolicy, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    result.representation = static_cast<HuPreflopPostflopRepresentation>(representation);
    result.missing_infoset_fallback = static_cast<HuPreflopSampledPolicyFallback>(fallback);
    result.current_policy_present =
        result.minor >= 11U ? source.at("current_policy_present").get<bool>() : false;
    result.entries.reserve(source.at("entries").size());
    for (const auto &item : source.at("entries")) {
      HuPreflopSampledPostflopPolicyEntry entry;
      entry.key.public_history = item.at("public_history").get<std::uint64_t>();
      entry.key.physical_cards = item.at("physical_cards").get<std::uint64_t>();
      entry.key.bucket_history =
          item.at("bucket_history").get<decltype(entry.key.bucket_history)>();
      entry.key.preflop_class = item.at("preflop_class").get<HandClassId>();
      entry.key.player = item.at("player").get<std::uint8_t>();
      entry.key.street = static_cast<Street>(item.at("street").get<std::uint8_t>());
      entry.action_count = item.at("action_count").get<std::uint8_t>();
      entry.probabilities = item.at("probabilities").get<decltype(entry.probabilities)>();
      if (result.minor >= 11U) {
        entry.current_probabilities =
            item.at("current_probabilities").get<decltype(entry.current_probabilities)>();
      }
      result.entries.push_back(std::move(entry));
    }
    result.fingerprint = source.at("fingerprint").get<std::string>();
    const auto valid = validate_hu_preflop_sampled_postflop_policy(tree, result);
    return valid
               ? Result<HuPreflopSampledPostflopPolicy, HuPreflopError>::success(std::move(result))
               : Result<HuPreflopSampledPostflopPolicy, HuPreflopError>::failure(valid.error());
  } catch (const Json::exception &) {
    return Result<HuPreflopSampledPostflopPolicy, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  } catch (const std::bad_alloc &) {
    return Result<HuPreflopSampledPostflopPolicy, HuPreflopError>::failure(
        HuPreflopError::MemoryFailure);
  }
}

Result<bool, HuPreflopError>
save_hu_preflop_sampled_postflop_policy(const HuPreflopSampledPostflopPolicy &policy,
                                        const std::string &path) {
  const auto payload = serialize_hu_preflop_sampled_postflop_policy(policy);
  return payload ? save_payload(path, wrap_file(payload.value()))
                 : Result<bool, HuPreflopError>::failure(payload.error());
}

Result<HuPreflopSampledPostflopPolicy, HuPreflopError>
load_hu_preflop_sampled_postflop_policy(const HuPreflopTree &tree, const std::string &path,
                                        const std::uint64_t maximum_payload_bytes) {
  const auto file = load_payload_bounded(path, maximum_payload_bytes);
  if (!file) {
    return Result<HuPreflopSampledPostflopPolicy, HuPreflopError>::failure(file.error());
  }
  const auto payload = unwrap_file(file.value());
  return payload ? deserialize_hu_preflop_sampled_postflop_policy(tree, payload.value(),
                                                                  maximum_payload_bytes)
                 : Result<HuPreflopSampledPostflopPolicy, HuPreflopError>::failure(payload.error());
}

} // namespace gtosd
