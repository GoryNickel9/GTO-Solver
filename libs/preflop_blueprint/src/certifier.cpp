#include "gtosd/preflop_blueprint/certifier.hpp"

#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/policy_file.hpp"
#include "gtosd/preflop_blueprint/trainer.hpp"

#include "binary_io.hpp"
#include "hashing.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <optional>
#include <sstream>
#include <string_view>
#include <thread>

namespace gtosd::preflop_blueprint {
namespace {

namespace ca = card_abstraction;
using Clock = std::chrono::steady_clock;
constexpr std::string_view state_magic = "GTOSDCRT";
constexpr std::uint32_t state_version = 1U;
constexpr std::size_t combo_total = 630U;
constexpr double ante_scale = 1.0 / static_cast<double>(Money::units_per_ante);

template <typename Function>
void run_parallel(const unsigned threads, const std::size_t count, Function &&function) {
  const auto workers = static_cast<std::size_t>(std::max(1U, threads));
  if (workers <= 1U || count <= 1U) {
    for (std::size_t index = 0; index < count; ++index) {
      function(index);
    }
    return;
  }
  std::atomic<std::size_t> next{0U};
  const auto worker = [&] {
    for (std::size_t index = next.fetch_add(1U); index < count; index = next.fetch_add(1U)) {
      function(index);
    }
  };
  std::vector<std::thread> pool;
  const auto spawned = std::min(workers, count) - 1U;
  for (std::size_t thread = 0; thread < spawned; ++thread) {
    pool.emplace_back(worker);
  }
  worker();
  for (auto &thread : pool) {
    thread.join();
  }
}

std::string state_header(const std::string &tree_fingerprint, const std::string &policy_fingerprint,
                         const std::string &catalog_fingerprint, const std::uint32_t entries,
                         const std::uint32_t flop_count) {
  std::string header;
  header.append(state_magic.data(), state_magic.size());
  binary_io::append_little32(header, state_version);
  binary_io::append_string(header, tree_fingerprint);
  binary_io::append_string(header, policy_fingerprint);
  binary_io::append_string(header, catalog_fingerprint);
  binary_io::append_little32(header, entries);
  binary_io::append_little32(header, flop_count);
  return header;
}

// Record: length, body (index, flop, weight, boards, images, compatible,
// values), checksum of the body.
void append_record(std::string &out, const std::uint32_t index, const FlopValues &values) {
  std::string body;
  binary_io::append_little32(body, index);
  for (const auto card : values.flop) {
    body.push_back(static_cast<char>(card.value()));
  }
  std::uint64_t weight_bits = 0U;
  std::memcpy(&weight_bits, &values.weight, sizeof(double));
  binary_io::append_little(body, weight_bits);
  binary_io::append_little32(body, values.boards);
  binary_io::append_little32(body, static_cast<std::uint32_t>(values.images.size()));
  for (const auto &image : values.images) {
    for (const auto suit : image) {
      body.push_back(static_cast<char>(suit));
    }
  }
  body.append(reinterpret_cast<const char *>(values.compatible.data()), values.compatible.size());
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    for (auto mode : {response_mode, average_mode}) {
      for (const auto &entry : values.entry_values[hero][mode]) {
        binary_io::append_doubles(body, entry);
      }
    }
  }
  binary_io::append_little32(out, static_cast<std::uint32_t>(body.size()));
  out += body;
  binary_io::append_little(out, detail::fnv1a_text(body));
}

bool read_record(const std::string &body, const std::size_t entries, std::uint32_t &index,
                 FlopValues &values) {
  binary_io::Reader reader(body);
  if (!reader.read_little32(index)) {
    return false;
  }
  std::vector<std::uint8_t> cards;
  if (!reader.read_bytes(cards, 3U)) {
    return false;
  }
  for (std::size_t position = 0; position < 3U; ++position) {
    const auto card = CardId::from_index(cards[position]);
    if (!card) {
      return false;
    }
    values.flop[position] = card.value();
  }
  std::uint64_t weight_bits = 0U;
  if (!reader.read_little(weight_bits)) {
    return false;
  }
  std::memcpy(&values.weight, &weight_bits, sizeof(double));
  std::uint32_t image_count = 0U;
  if (!reader.read_little32(values.boards) || !reader.read_little32(image_count) ||
      image_count == 0U || image_count > ca::suit_permutation_count) {
    return false;
  }
  values.images.clear();
  for (std::uint32_t image = 0; image < image_count; ++image) {
    std::vector<std::uint8_t> suits;
    if (!reader.read_bytes(suits, 4U)) {
      return false;
    }
    ca::SuitPermutation permutation{};
    for (std::size_t suit = 0; suit < 4U; ++suit) {
      if (suits[suit] >= 4U) {
        return false;
      }
      permutation[suit] = suits[suit];
    }
    values.images.push_back(permutation);
  }
  if (!reader.read_bytes(values.compatible, combo_total)) {
    return false;
  }
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    for (auto mode : {response_mode, average_mode}) {
      values.entry_values[hero][mode].resize(entries);
      for (auto &entry : values.entry_values[hero][mode]) {
        if (!reader.read_doubles(entry, combo_total)) {
          return false;
        }
      }
    }
  }
  return reader.at_end();
}

std::string json_number(const double value) {
  std::ostringstream stream;
  stream << std::setprecision(17) << value;
  return stream.str();
}

std::string json_string(const std::string &value) {
  std::string out = "\"";
  for (const auto character : value) {
    if (character == '"' || character == '\\') {
      out.push_back('\\');
    }
    out.push_back(character);
  }
  out.push_back('"');
  return out;
}

} // namespace

Result<Certificate, CertifierError> certify(const CompiledGame &game, const BucketPolicy &policy,
                                            const BestResponseResources &resources,
                                            const CertifierOptions &options) {
  using Outcome = Result<Certificate, CertifierError>;
  const auto started = Clock::now();
  if (resources.ranks == nullptr || resources.catalog == nullptr || resources.flop == nullptr ||
      resources.turn == nullptr || resources.river == nullptr) {
    return Outcome::failure(CertifierError::MissingResource);
  }
  if (game.stats().preflop_all_in_runouts > 0U && resources.all_in == nullptr) {
    return Outcome::failure(CertifierError::MissingResource);
  }
  if (game.config().player_count != 2U || options.chunk_flops == 0U) {
    return Outcome::failure(CertifierError::InvalidConfiguration);
  }
  auto evaluator = BestResponseEvaluator::create(game, policy, resources);
  if (!evaluator) {
    return Outcome::failure(CertifierError::EvaluationFailure);
  }

  Certificate certificate;
  const auto &config = game.config();
  certificate.config_id = config.id;
  certificate.rules_fingerprint = game_config_fingerprint(config);
  certificate.tree_fingerprint = game.fingerprint();
  certificate.catalog_fingerprint = resources.catalog->fingerprint();
  certificate.flop_table_fingerprint = resources.flop->fingerprint();
  certificate.turn_table_fingerprint = resources.turn->fingerprint();
  certificate.river_table_fingerprint = resources.river->fingerprint();
  certificate.policy_fingerprint = policy_fingerprint(policy);
  certificate.flop_capacity = policy.layout().flop_capacity;
  certificate.turn_capacity = policy.layout().turn_capacity;
  certificate.river_capacity = policy.layout().river_capacity;
  certificate.initial_pot_antes =
      static_cast<double>(config.ante.units() * config.player_count + config.button_blind.units()) *
      ante_scale;
  certificate.stack_antes = static_cast<double>(config.effective_stack.units()) * ante_scale;

  const auto finish = [&](Certificate &result) {
    result.normalized_dev = result.initial_pot_antes > 0.0
                                ? result.report.max_gain / result.initial_pot_antes
                                : 0.0;
    result.normalized_stack =
        result.stack_antes > 0.0 ? result.report.max_gain / result.stack_antes : 0.0;
    result.process_bytes = process_working_set_bytes();
    result.seconds = std::chrono::duration<double>(Clock::now() - started).count();
  };

  if (options.sample_flops > 0U) {
    ca::DeterministicRandom random(options.sample_seed);
    std::vector<FlopGroup> groups;
    groups.reserve(options.sample_flops);
    for (std::uint32_t draw = 0; draw < options.sample_flops; ++draw) {
      groups.push_back(full_runouts(resources.catalog->sample_physical_history(random).flop));
    }
    BestResponseOptions evaluation;
    evaluation.threads = options.threads;
    const auto report = evaluate_best_response(game, policy, resources, groups, evaluation);
    if (!report) {
      return Outcome::failure(CertifierError::EvaluationFailure);
    }
    certificate.report = report.value();
    certificate.sampled = true;
    certificate.flops = report.value().flops;
    certificate.physical_flops = report.value().flops;
    certificate.boards = report.value().boards;
    certificate.evaluation_seconds = report.value().seconds;
    finish(certificate);
    return Outcome::success(std::move(certificate));
  }

  const auto &canonical = resources.catalog->flops();
  const auto total = options.flop_limit > 0U
                         ? std::min<std::size_t>(options.flop_limit, canonical.size())
                         : canonical.size();
  const auto entries = static_cast<std::uint32_t>(evaluator.value().entry_count());
  std::vector<std::optional<FlopValues>> done(total);

  const auto header = state_header(certificate.tree_fingerprint, certificate.policy_fingerprint,
                                   certificate.catalog_fingerprint, entries,
                                   static_cast<std::uint32_t>(canonical.size()));
  if (!options.state_path.empty()) {
    if (std::filesystem::exists(options.state_path)) {
      std::ifstream input(options.state_path, std::ios::binary);
      if (!input) {
        return Outcome::failure(CertifierError::IoFailure);
      }
      const std::string data((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
      if (data.size() < header.size() ||
          std::memcmp(data.data(), header.data(), header.size()) != 0) {
        return Outcome::failure(CertifierError::IntegrityFailure);
      }
      std::size_t position = header.size();
      while (position + 4U <= data.size()) {
        const std::string length_bytes = data.substr(position, 4U);
        binary_io::Reader length_reader(length_bytes);
        std::uint32_t length = 0U;
        static_cast<void>(length_reader.read_little32(length));
        if (position + 4U + length + 8U > data.size()) {
          break; // truncated record: ignored, recomputed
        }
        const std::string body = data.substr(position + 4U, length);
        const std::string checksum_bytes = data.substr(position + 4U + length, 8U);
        binary_io::Reader checksum_reader(checksum_bytes);
        std::uint64_t checksum = 0U;
        static_cast<void>(checksum_reader.read_little(checksum));
        if (checksum != detail::fnv1a_text(body)) {
          break;
        }
        std::uint32_t index = 0U;
        FlopValues values;
        if (!read_record(body, entries, index, values)) {
          break;
        }
        if (index < total && !done[index].has_value()) {
          done[index] = std::move(values);
          ++certificate.resumed_flops;
        }
        position += 4U + length + 8U;
      }
    } else {
      std::ofstream output(options.state_path, std::ios::binary | std::ios::trunc);
      if (!output) {
        return Outcome::failure(CertifierError::IoFailure);
      }
      output.write(header.data(), static_cast<std::streamsize>(header.size()));
      output.flush();
      if (!output) {
        return Outcome::failure(CertifierError::IoFailure);
      }
    }
  }

  std::vector<std::uint32_t> pending;
  for (std::uint32_t index = 0; index < total; ++index) {
    if (!done[index].has_value()) {
      pending.push_back(index);
    }
  }
  const auto evaluation_started = Clock::now();
  std::uint64_t boards_done = 0U;
  for (std::uint32_t index = 0; index < total; ++index) {
    if (done[index].has_value()) {
      boards_done += done[index]->boards;
    }
  }
  std::atomic<bool> failed{false};
  std::atomic<bool> inconsistent{false};
  for (std::size_t start = 0; start < pending.size(); start += options.chunk_flops) {
    const auto count = std::min<std::size_t>(options.chunk_flops, pending.size() - start);
    std::vector<FlopValues> chunk(count);
    run_parallel(options.threads, count, [&](const std::size_t offset) {
      const auto index = pending[start + offset];
      auto flop = canonical[index].cards;
      std::sort(flop.begin(), flop.end());
      auto values = evaluator.value().evaluate_flop(full_runouts(flop));
      if (!values) {
        failed.store(true);
        return;
      }
      values.value().images = flop_images(flop);
      if (values.value().images.size() != canonical[index].multiplicity) {
        inconsistent.store(true);
      }
      chunk[offset] = std::move(values.value());
    });
    if (failed.load()) {
      return Outcome::failure(CertifierError::EvaluationFailure);
    }
    if (inconsistent.load()) {
      return Outcome::failure(CertifierError::IntegrityFailure);
    }
    if (!options.state_path.empty()) {
      std::string records;
      for (std::size_t offset = 0; offset < count; ++offset) {
        append_record(records, pending[start + offset], chunk[offset]);
      }
      std::ofstream output(options.state_path, std::ios::binary | std::ios::app);
      if (!output) {
        return Outcome::failure(CertifierError::IoFailure);
      }
      output.write(records.data(), static_cast<std::streamsize>(records.size()));
      output.flush();
      if (!output) {
        return Outcome::failure(CertifierError::IoFailure);
      }
    }
    for (std::size_t offset = 0; offset < count; ++offset) {
      boards_done += chunk[offset].boards;
      done[pending[start + offset]] = std::move(chunk[offset]);
    }
    if (options.progress) {
      CertifierProgress progress;
      progress.flops_total = static_cast<std::uint32_t>(total);
      progress.flops_done = static_cast<std::uint32_t>(
          std::count_if(done.begin(), done.end(), [](const auto &entry) { return entry.has_value(); }));
      progress.boards_done = boards_done;
      progress.seconds = std::chrono::duration<double>(Clock::now() - started).count();
      options.progress(progress);
    }
  }
  certificate.evaluation_seconds =
      std::chrono::duration<double>(Clock::now() - evaluation_started).count();

  std::vector<const FlopValues *> pointers;
  pointers.reserve(total);
  for (std::size_t index = 0; index < total; ++index) {
    pointers.push_back(&done[index].value());
    certificate.physical_flops += static_cast<std::uint32_t>(done[index]->images.size());
    certificate.boards += done[index]->boards;
  }
  certificate.flops = static_cast<std::uint32_t>(total);
  certificate.partial = total < canonical.size();
  certificate.exact = !certificate.partial;
  const auto aggregation_started = Clock::now();
  auto report = evaluator.value().aggregate(pointers, certificate.exact);
  if (!report) {
    return Outcome::failure(CertifierError::EvaluationFailure);
  }
  certificate.aggregation_seconds =
      std::chrono::duration<double>(Clock::now() - aggregation_started).count();
  certificate.report = report.value();
  finish(certificate);
  return Outcome::success(std::move(certificate));
}

std::string certificate_json(const Certificate &certificate) {
  const auto &report = certificate.report;
  std::string out = "{\n";
  out += "  \"schema\": " + json_string(certificate.schema) + ",\n";
  out += "  \"config_id\": " + json_string(certificate.config_id) + ",\n";
  out += std::string("  \"exact\": ") + (certificate.exact ? "true" : "false") + ",\n";
  out += std::string("  \"partial\": ") + (certificate.partial ? "true" : "false") + ",\n";
  out += std::string("  \"sampled\": ") + (certificate.sampled ? "true" : "false") + ",\n";
  out += "  \"flops\": " + std::to_string(certificate.flops) + ",\n";
  out += "  \"physical_flops\": " + std::to_string(certificate.physical_flops) + ",\n";
  out += "  \"boards\": " + std::to_string(certificate.boards) + ",\n";
  out += "  \"resumed_flops\": " + std::to_string(certificate.resumed_flops) + ",\n";
  out += "  \"ev\": [" + json_number(report.ev[0]) + ", " + json_number(report.ev[1]) + "],\n";
  out += "  \"best_response\": [" + json_number(report.best_response[0]) + ", " +
         json_number(report.best_response[1]) + "],\n";
  out += "  \"gain\": [" + json_number(report.gain[0]) + ", " + json_number(report.gain[1]) + "],\n";
  out += "  \"best_response_lower\": [" + json_number(report.best_response_lower[0]) + ", " +
         json_number(report.best_response_lower[1]) + "],\n";
  out += "  \"gain_lower\": [" + json_number(report.gain_lower[0]) + ", " +
         json_number(report.gain_lower[1]) + "],\n";
  out += "  \"gain_standard_error\": [" +
         json_number(std::sqrt(report.best_response_standard_error[0] *
                                   report.best_response_standard_error[0] +
                               report.ev_standard_error[0] * report.ev_standard_error[0])) +
         ", " +
         json_number(std::sqrt(report.best_response_standard_error[1] *
                                   report.best_response_standard_error[1] +
                               report.ev_standard_error[1] * report.ev_standard_error[1])) +
         "],\n";
  out += "  \"max_gain\": " + json_number(report.max_gain) + ",\n";
  out += "  \"max_gain_lower\": " + json_number(report.max_gain_lower) + ",\n";
  out += "  \"max_gain_half_width\": " + json_number(report.max_gain_half_width) + ",\n";
  out += "  \"nashconv\": " + json_number(report.nashconv) + ",\n";
  out += "  \"normalized_dev\": " + json_number(certificate.normalized_dev) + ",\n";
  out += "  \"normalized_stack\": " + json_number(certificate.normalized_stack) + ",\n";
  out += "  \"initial_pot_antes\": " + json_number(certificate.initial_pot_antes) + ",\n";
  out += "  \"effective_stack_antes\": " + json_number(certificate.stack_antes) + ",\n";
  out += "  \"capacities\": [" + std::to_string(certificate.flop_capacity) + ", " +
         std::to_string(certificate.turn_capacity) + ", " +
         std::to_string(certificate.river_capacity) + "],\n";
  out += "  \"rules_fingerprint\": " + json_string(certificate.rules_fingerprint) + ",\n";
  out += "  \"tree_fingerprint\": " + json_string(certificate.tree_fingerprint) + ",\n";
  out += "  \"catalog_fingerprint\": " + json_string(certificate.catalog_fingerprint) + ",\n";
  out += "  \"flop_table_fingerprint\": " + json_string(certificate.flop_table_fingerprint) + ",\n";
  out += "  \"turn_table_fingerprint\": " + json_string(certificate.turn_table_fingerprint) + ",\n";
  out += "  \"river_table_fingerprint\": " + json_string(certificate.river_table_fingerprint) + ",\n";
  out += "  \"policy_fingerprint\": " + json_string(certificate.policy_fingerprint) + ",\n";
  out += "  \"seconds\": " + json_number(certificate.seconds) + ",\n";
  out += "  \"evaluation_seconds\": " + json_number(certificate.evaluation_seconds) + ",\n";
  out += "  \"aggregation_seconds\": " + json_number(certificate.aggregation_seconds) + ",\n";
  out += "  \"process_bytes\": " + std::to_string(certificate.process_bytes) + "\n";
  out += "}\n";
  return out;
}

const char *certifier_error_name(const CertifierError error) noexcept {
  switch (error) {
  case CertifierError::InvalidConfiguration:
    return "invalid_configuration";
  case CertifierError::MissingResource:
    return "missing_resource";
  case CertifierError::EvaluationFailure:
    return "evaluation_failure";
  case CertifierError::IoFailure:
    return "io_failure";
  case CertifierError::IntegrityFailure:
    return "integrity_failure";
  case CertifierError::UnsupportedVersion:
    return "unsupported_version";
  }
  return "unknown";
}

} // namespace gtosd::preflop_blueprint
