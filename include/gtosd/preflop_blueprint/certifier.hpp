#pragma once

#include "gtosd/preflop_blueprint/best_response.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/traversal.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

// Board-major certifier (roadmap P7): exact best response of the physical
// game against the lifted average strategy. Every canonical flop of the
// catalog is evaluated once with all its 33 x 32 runouts (stage one of
// best_response.hpp) and stands for its whole suit orbit in the preflop
// aggregation (stage two), which is exact because the strategy is
// suit-symmetric: 573 flop groups, 605,088 boards, 7,140 physical flops.
// Per-flop values can be appended to a state file after every chunk and the
// pass resumes from it; the aggregation is deterministic in the flop order,
// so a resumed pass equals a continuous one bit for bit. The sampled mode
// draws physical flops instead (the P6 estimator) and is flagged inexact.
namespace gtosd::preflop_blueprint {

enum class CertifierError : std::uint8_t {
  InvalidConfiguration,
  MissingResource,
  EvaluationFailure,
  IoFailure,
  IntegrityFailure,
  UnsupportedVersion
};

struct CertifierProgress {
  std::uint32_t flops_done{0U};
  std::uint32_t flops_total{0U};
  std::uint64_t boards_done{0U};
  double seconds{0.0};
};

struct CertifierOptions {
  unsigned threads{1U};
  // Flops evaluated between two writes of the state file.
  std::uint32_t chunk_flops{16U};
  // Resumable per-flop values; empty keeps everything in memory.
  std::filesystem::path state_path;
  // 0 = all canonical flops; otherwise only the first N (partial pass, for
  // timing projections; the report is then neither exact nor unbiased).
  std::uint32_t flop_limit{0U};
  // > 0 = sampled physical flops with all runouts instead of the exact pass.
  std::uint32_t sample_flops{0U};
  std::uint64_t sample_seed{0x4345'5254'4946'5931ULL};
  std::function<void(const CertifierProgress &)> progress;
  // River path of the flop evaluations; the certificate is the same bit for
  // bit with either engine.
  RiverEngine river_engine{RiverEngine::Joint};
  // First street on which the best responder may deviate (street-restricted
  // diagnostic, best_response.hpp); Preflop is the full best response. The
  // exact, partial and sampled passes all support it, and the certificate
  // then reports the restricted response in its best-response fields with a
  // "deviation_from" entry. The state file of a restricted pass carries the
  // restriction in its header and the restricted values in its records, so a
  // pass resumes only from a state of the same restriction.
  DeviationStreet deviation_from{DeviationStreet::Preflop};
};

struct Certificate {
  std::string schema{"gtosd.preflop_blueprint_certificate.v1"};
  bool exact{false};
  bool partial{false};
  bool sampled{false};
  std::uint32_t flops{0U};
  std::uint32_t physical_flops{0U};
  std::uint32_t boards{0U};
  BestResponseReport report;
  double initial_pot_antes{0.0};
  double stack_antes{0.0};
  double normalized_dev{0.0};
  double normalized_stack{0.0};
  std::string config_id;
  std::string rules_fingerprint;
  std::string tree_fingerprint;
  std::string catalog_fingerprint;
  std::string flop_table_fingerprint;
  std::string turn_table_fingerprint;
  std::string river_table_fingerprint;
  std::string policy_fingerprint;
  std::uint32_t flop_capacity{0U};
  std::uint32_t turn_capacity{0U};
  std::uint32_t river_capacity{0U};
  double seconds{0.0};
  double evaluation_seconds{0.0};
  double aggregation_seconds{0.0};
  std::uint32_t resumed_flops{0U};
  std::uint64_t process_bytes{0U};
};

[[nodiscard]] Result<Certificate, CertifierError> certify(const CompiledGame &game,
                                                          const BucketPolicy &policy,
                                                          const BestResponseResources &resources,
                                                          const CertifierOptions &options);

[[nodiscard]] std::string certificate_json(const Certificate &certificate);
[[nodiscard]] const char *certifier_error_name(CertifierError error) noexcept;

} // namespace gtosd::preflop_blueprint
