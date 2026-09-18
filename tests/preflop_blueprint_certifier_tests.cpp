#include "preflop_blueprint_test_support.hpp"

#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/preflop_blueprint/certifier.hpp"
#include "gtosd/preflop_blueprint/policy_file.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace pb_test;

// Deterministic pseudo-random bucket policy: every row a strictly positive
// distribution. Symmetric under suit permutations by construction (rows are
// hand classes and canonical buckets).
pb::BucketPolicy random_policy(const pb::CompiledGame &game, const Resources &resources,
                               const std::uint64_t seed) {
  const auto layout = pb::layout_state(game, resources.flop->capacity(),
                                       resources.turn->capacity(), resources.river->capacity());
  pb::BucketPolicy policy(game, layout);
  ca::DeterministicRandom random(seed);
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::Decision) {
      continue;
    }
    const auto rows = pb::StateLayout::rows_for(node.street, layout.flop_capacity,
                                                layout.turn_capacity, layout.river_capacity);
    for (std::uint32_t row = 0; row < rows; ++row) {
      auto probabilities = policy.row(node.id, row);
      double total = 0.0;
      for (auto &probability : probabilities) {
        probability = 0.05 + random.uniform_unit();
        total += probability;
      }
      for (auto &probability : probabilities) {
        probability /= total;
      }
    }
  }
  return policy;
}

std::uint16_t preimage_combo(const std::uint16_t combo, const ca::SuitPermutation &permutation) {
  const auto inverse = ca::inverse_permutation(permutation);
  const auto cards = ca::combo_table().cards[combo];
  const auto first = ca::permute_card(gtosd::CardId::from_index(cards[0]).value(), inverse);
  const auto second = ca::permute_card(gtosd::CardId::from_index(cards[1]).value(), inverse);
  return ca::combo_index(first, second);
}

pb::BestResponseResources response_resources(const Resources &resources) {
  pb::BestResponseResources view;
  view.ranks = &resources.ranks.value();
  view.all_in = &resources.all_in.value();
  view.catalog = &resources.catalog.value();
  view.flop = &resources.flop.value();
  view.turn = &resources.turn.value();
  view.river = &resources.river.value();
  return view;
}

void test_policy_file(const Resources &resources, const std::filesystem::path &scratch) {
  const auto started = Clock::now();
  const auto reduced = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  const auto full = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_full_v1.json"));
  require(reduced.has_value() && full.has_value(), "HU10 fixtures compile");
  const auto policy = random_policy(reduced.value(), resources, 7U);
  const auto path = scratch / "policy_roundtrip.bin";
  const auto saved = pb::save_policy(path, reduced.value(), policy, "certifier test");
  require(saved.has_value(), "policy saves");
  pb::PolicyFileInfo info;
  const auto loaded = pb::load_policy(path, reduced.value(), &info);
  require(loaded.has_value(), "policy loads");
  require(loaded.value()->table() == policy.table(), "policy table round trip is bit-identical");
  require(info.tree_fingerprint == reduced.value().fingerprint(), "policy carries the tree fingerprint");
  require(info.source == "certifier test", "policy carries the source text");
  require(info.flop_capacity == resources.flop->capacity() &&
              info.turn_capacity == resources.turn->capacity() &&
              info.river_capacity == resources.river->capacity(),
          "policy carries the capacities");
  require(info.policy_fingerprint == pb::policy_fingerprint(policy),
          "policy fingerprint of the file equals the in-memory one");
  const auto header = pb::read_policy_info(path);
  require(header.has_value() && header.value().policy_fingerprint == info.policy_fingerprint,
          "policy header reads without the game");
  const auto mismatch = pb::load_policy(path, full.value());
  require(!mismatch.has_value() && mismatch.error() == pb::PolicyFileError::GameMismatch,
          "policy of another tree is rejected");
  {
    std::string bytes = read_file(path);
    bytes[bytes.size() / 2U] = static_cast<char>(bytes[bytes.size() / 2U] ^ 0x5A);
    const auto corrupt_path = scratch / "policy_corrupt.bin";
    std::ofstream output(corrupt_path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    const auto corrupt = pb::load_policy(corrupt_path, reduced.value());
    require(!corrupt.has_value() && corrupt.error() == pb::PolicyFileError::IntegrityFailure,
            "corrupt policy is rejected");
  }
  std::cout << "policy file: " << policy.table().size() << " entries, fingerprint "
            << info.policy_fingerprint << ", "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

// A canonical flop with its suit images must give the same aggregation as
// the explicit enumeration of its physical images, and the values of hand h
// on sigma(flop) must equal the values of sigma^-1(h) on the flop.
void test_orbit_aggregation(const Resources &resources) {
  const auto started = Clock::now();
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  const auto policy = random_policy(game.value(), resources, 11U);
  const auto view = response_resources(resources);
  const auto evaluator = pb::BestResponseEvaluator::create(game.value(), policy, view);
  require(evaluator.has_value(), "evaluator creates");

  const auto &catalog = resources.catalog.value();
  std::vector<std::uint32_t> chosen;
  for (const std::uint32_t wanted : {4U, 12U}) {
    for (std::uint32_t index = 0; index < catalog.flops().size(); ++index) {
      if (catalog.flops()[index].multiplicity == wanted) {
        chosen.push_back(index);
        break;
      }
    }
  }
  require(chosen.size() == 2U, "canonical flops with orbits of 4 and 12 exist");

  std::vector<pb::FlopValues> canonical_values;
  std::vector<pb::FlopValues> physical_values;
  std::uint32_t physical_flops = 0U;
  for (const auto index : chosen) {
    auto flop = catalog.flops()[index].cards;
    std::sort(flop.begin(), flop.end());
    auto values = evaluator.value().evaluate_flop(pb::full_runouts(flop));
    require(values.has_value(), "canonical flop evaluates");
    values.value().images = pb::flop_images(flop);
    require(values.value().images.size() == catalog.flops()[index].multiplicity,
            "orbit size equals the catalog multiplicity");
    require(values.value().images.front() == ca::identity_permutation, "identity image first");
    for (const auto &permutation : values.value().images) {
      std::array<gtosd::CardId, 3> image{};
      for (std::size_t position = 0; position < 3U; ++position) {
        image[position] = ca::permute_card(flop[position], permutation);
      }
      std::sort(image.begin(), image.end());
      auto physical = evaluator.value().evaluate_flop(pb::full_runouts(image));
      require(physical.has_value(), "physical image evaluates");
      const auto lookup = catalog.lookup_flop(image);
      require(lookup.has_value() && lookup.value().index == index,
              "physical image maps back to the canonical flop");
      // Symmetry of the values, combo by combo.
      for (std::uint16_t combo = 0; combo < 630U; ++combo) {
        const auto source = preimage_combo(combo, permutation);
        require(physical.value().compatible[combo] == values.value().compatible[source],
                "compatibility is permutation-invariant");
        for (std::uint8_t hero = 0; hero < 2U; ++hero) {
          for (auto mode : {pb::response_mode, pb::average_mode}) {
            for (std::size_t entry = 0; entry < evaluator.value().entry_count(); ++entry) {
              const double left = physical.value().entry_values[hero][mode][entry][combo];
              const double right = values.value().entry_values[hero][mode][entry][source];
              require(std::abs(left - right) <= 1e-12 * std::max(1.0, std::abs(right)),
                      "values on the image equal the values of the preimage hand");
            }
          }
        }
      }
      physical_values.push_back(std::move(physical.value()));
      ++physical_flops;
    }
    canonical_values.push_back(std::move(values.value()));
  }
  std::vector<const pb::FlopValues *> canonical_pointers;
  for (const auto &values : canonical_values) {
    canonical_pointers.push_back(&values);
  }
  std::vector<const pb::FlopValues *> physical_pointers;
  for (const auto &values : physical_values) {
    physical_pointers.push_back(&values);
  }
  const auto orbit = evaluator.value().aggregate(canonical_pointers, false);
  const auto explicit_report = evaluator.value().aggregate(physical_pointers, false);
  require(orbit.has_value() && explicit_report.has_value(), "aggregations succeed");
  for (std::uint8_t player = 0; player < 2U; ++player) {
    require(close(orbit.value().ev[player], explicit_report.value().ev[player], 1e-12),
            "orbit EV equals the explicit enumeration");
    require(close(orbit.value().best_response[player], explicit_report.value().best_response[player], 1e-12),
            "orbit best response equals the explicit enumeration");
    require(close(orbit.value().best_response_lower[player],
                  explicit_report.value().best_response_lower[player], 1e-12),
            "orbit lower bound equals the explicit enumeration");
    require(orbit.value().gain[player] >= -1e-12, "gain is not negative");
    require(orbit.value().gain_lower[player] >= -1e-12, "lower gain is not negative");
  }
  // EV is zero-sum only on the full catalog: on a subset of flops the two
  // players condition on different compatible-flop counts (P7 report).
  require(explicit_report.value().flops == physical_flops && orbit.value().flops == 2U,
          "flop counts");
  std::cout << "orbit aggregation: " << physical_flops << " physical flops of 2 canonical, EV ["
            << orbit.value().ev[0] << ", " << orbit.value().ev[1] << "], BR ["
            << orbit.value().best_response[0] << ", " << orbit.value().best_response[1]
            << "], " << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

bool same_report(const pb::BestResponseReport &left, const pb::BestResponseReport &right) {
  for (std::uint8_t player = 0; player < 2U; ++player) {
    if (left.ev[player] != right.ev[player] || left.best_response[player] != right.best_response[player] ||
        left.best_response_lower[player] != right.best_response_lower[player] ||
        left.best_response_preflop[player] != right.best_response_preflop[player] ||
        left.gain[player] != right.gain[player] ||
        left.best_response_standard_error[player] != right.best_response_standard_error[player] ||
        left.ev_standard_error[player] != right.ev_standard_error[player]) {
      return false;
    }
  }
  return left.flops == right.flops && left.boards == right.boards &&
         left.max_gain == right.max_gain && left.nashconv == right.nashconv;
}

// Partial pass with chunks and state file: a resumed pass equals the
// continuous one bit for bit; invariants and certificate JSON.
void test_partial_pass_and_resume(const Resources &resources, const std::filesystem::path &scratch) {
  const auto started = Clock::now();
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  const auto policy = random_policy(game.value(), resources, 23U);
  const auto view = response_resources(resources);

  pb::CertifierOptions continuous;
  continuous.threads = 8U;
  continuous.chunk_flops = 3U;
  continuous.flop_limit = 7U;
  continuous.state_path = scratch / "certifier_state_continuous.bin";
  std::filesystem::remove(continuous.state_path);
  std::uint32_t progress_calls = 0U;
  continuous.progress = [&](const pb::CertifierProgress &) { ++progress_calls; };
  const auto first = pb::certify(game.value(), policy, view, continuous);
  require(first.has_value(), "partial pass succeeds");
  require(first.value().partial && !first.value().exact && !first.value().sampled, "partial flags");
  require(first.value().flops == 7U && first.value().resumed_flops == 0U, "partial flop count");
  require(progress_calls == 3U, "one progress call per chunk");
  require(first.value().boards == 7U * 1056U, "all runouts of every flop");

  pb::CertifierOptions interrupted = continuous;
  interrupted.chunk_flops = 2U;
  interrupted.flop_limit = 4U;
  interrupted.state_path = scratch / "certifier_state_resumed.bin";
  interrupted.progress = nullptr;
  std::filesystem::remove(interrupted.state_path);
  const auto partial = pb::certify(game.value(), policy, view, interrupted);
  require(partial.has_value() && partial.value().flops == 4U, "interrupted pass writes four flops");
  pb::CertifierOptions resumed = interrupted;
  resumed.flop_limit = 7U;
  resumed.chunk_flops = 5U;
  const auto second = pb::certify(game.value(), policy, view, resumed);
  require(second.has_value(), "resumed pass succeeds");
  require(second.value().resumed_flops == 4U, "resumed pass reads the four stored flops");
  require(same_report(first.value().report, second.value().report),
          "resumed pass equals the continuous pass bit for bit");

  // A state file of another policy is rejected.
  const auto other = random_policy(game.value(), resources, 29U);
  const auto rejected = pb::certify(game.value(), other, view, resumed);
  require(!rejected.has_value() && rejected.error() == pb::CertifierError::IntegrityFailure,
          "state of another policy is rejected");

  const auto &report = first.value().report;
  for (std::uint8_t player = 0; player < 2U; ++player) {
    require(report.gain[player] >= -1e-12 && report.gain_lower[player] >= -1e-12,
            "gains are not negative");
    require(report.best_response[player] >= report.best_response_lower[player] - 1e-12,
            "plain estimate dominates the lower bound on the same flops");
    // Maximising one level alone can neither lose to maximising none nor beat
    // maximising both, so the preflop-only deviation is bracketed by ev and
    // the plain best response.
    require(report.gain_preflop[player] >= -1e-12, "the preflop-only gain is not negative");
    require(report.best_response[player] >= report.best_response_preflop[player] - 1e-12,
            "the plain best response dominates the preflop-only deviation");
  }
  const auto json = pb::certificate_json(first.value());
  require(json.find("\"schema\": \"gtosd.preflop_blueprint_certificate.v1\"") != std::string::npos,
          "certificate schema");
  require(json.find(game.value().fingerprint()) != std::string::npos, "certificate tree fingerprint");
  require(json.find(pb::policy_fingerprint(policy)) != std::string::npos,
          "certificate policy fingerprint");
  require(json.find(resources.catalog->fingerprint()) != std::string::npos,
          "certificate catalog fingerprint");
  require(json.find("\"exact\": false") != std::string::npos && json.find("\"partial\": true") != std::string::npos,
          "certificate flags");
  std::cout << "partial pass: 7 flops, EV [" << report.ev[0] << ", " << report.ev[1] << "], max gain "
            << report.max_gain << " (lower " << report.max_gain_lower << "), evaluation "
            << first.value().evaluation_seconds << " s, total "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

// The sampled command reproduces the trainer's estimator on the same flops.
void test_sampled_matches_trainer(const Resources &resources, const std::filesystem::path &scratch) {
  const auto started = Clock::now();
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  auto config = resources.config();
  config.batch_boards = 4U;
  config.threads = 4U;
  auto trainer = pb::Trainer::create(game.value(), resources.view(), config);
  require(trainer.has_value(), "trainer creates");
  for (int iteration = 0; iteration < 5; ++iteration) {
    require(trainer.value()->iterate().has_value(), "iteration succeeds");
  }
  const auto estimate = trainer.value()->estimate_exploitability(3U);
  require(estimate.has_value(), "trainer estimate succeeds");
  const auto policy_path = scratch / "sampled_policy.bin";
  const auto average = trainer.value()->average_policy();
  require(pb::save_policy(policy_path, game.value(), average, "sampled test").has_value(), "policy saves");
  const auto loaded = pb::load_policy(policy_path, game.value());
  require(loaded.has_value(), "policy loads");
  pb::CertifierOptions options;
  options.threads = 4U;
  options.sample_flops = 3U;
  options.sample_seed = config.evaluation_seed;
  const auto certificate = pb::certify(game.value(), *loaded.value(), response_resources(resources), options);
  require(certificate.has_value(), "sampled certification succeeds");
  require(certificate.value().sampled && !certificate.value().exact, "sampled flags");
  const auto &report = certificate.value().report;
  for (std::uint8_t player = 0; player < 2U; ++player) {
    require(close(report.ev[player], estimate.value().ev[player], 1e-12), "sampled EV equals the trainer");
    require(close(report.best_response[player], estimate.value().best_response[player], 1e-12),
            "sampled best response equals the trainer");
    require(close(report.gain_lower[player], estimate.value().gain_lower[player], 1e-12),
            "sampled lower bound equals the trainer");
  }
  require(report.boards == estimate.value().boards && report.flops == 3U, "sampled counts");
  std::cout << "sampled command: max gain " << report.max_gain << " +- " << report.max_gain_half_width
            << " (trainer " << estimate.value().max_gain << "), "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path resources_dir;
    std::filesystem::path buckets_dir;
    std::filesystem::path scratch_dir = std::filesystem::temp_directory_path() / "gtosd_certifier_tests";
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string_view name = argv[index];
      const std::string_view value = argv[index + 1];
      if (name == "--resources-dir") {
        resources_dir = std::filesystem::path(value);
      } else if (name == "--buckets-dir") {
        buckets_dir = std::filesystem::path(value);
      } else if (name == "--scratch-dir") {
        scratch_dir = std::filesystem::path(value);
      }
    }
    std::filesystem::create_directories(scratch_dir);
    const auto resources = load_resources(resources_dir, buckets_dir);
    std::cout << "resources " << (resources.loaded ? "loaded" : "built") << ", bucket tables "
              << (resources.buckets_loaded ? "loaded" : "synthetic") << " ("
              << resources.flop->capacity() << "/" << resources.turn->capacity() << "/"
              << resources.river->capacity() << ")\n";
    test_policy_file(resources, scratch_dir);
    test_orbit_aggregation(resources);
    test_partial_pass_and_resume(resources, scratch_dir);
    test_sampled_matches_trainer(resources, scratch_dir);
    std::cout << "PREFLOP_BLUEPRINT_CERTIFIER_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_CERTIFIER_TESTS=FAIL " << error.what() << '\n';
    return 1;
  }
}
