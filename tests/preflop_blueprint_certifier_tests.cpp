#include "preflop_blueprint_test_support.hpp"

#include "gtosd/card_abstraction/card_abstraction.hpp"
#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/preflop_blueprint/certifier.hpp"
#include "gtosd/preflop_blueprint/history_bucket_rows.hpp"
#include "gtosd/preflop_blueprint/policy_file.hpp"
#include "gtosd/preflop_blueprint/river_engine.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
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

void test_class_row_lifting(const Resources &resources) {
  const auto game =
      pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "class lifting fixture compiles");
  const auto base = random_policy(game.value(), resources, 83U);
  const auto rows = pb::ClassBucketRows::build(*resources.flop, *resources.turn, *resources.river);
  require(rows.has_value(), "class mapping builds");
  const auto layout = pb::layout_state(game.value(), rows.value().count(ca::BucketStreet::Flop),
                                       rows.value().count(ca::BucketStreet::Turn),
                                       rows.value().count(ca::BucketStreet::River));
  pb::BucketPolicy lifted(game.value(), layout);
  const std::array<const ca::BucketTable *, 3> tables{&*resources.flop, &*resources.turn,
                                                      &*resources.river};
  for (const auto &node : game.value().nodes()) {
    if (node.kind != pb::NodeKind::Decision) {
      continue;
    }
    if (node.street == gtosd::Street::Preflop) {
      for (std::uint32_t row = 0; row < ca::preflop_hand_classes; ++row) {
        const auto source = base.row(node.id, row);
        std::copy(source.begin(), source.end(), lifted.row(node.id, row).begin());
      }
      continue;
    }
    const auto street = static_cast<ca::BucketStreet>(static_cast<unsigned>(node.street) - 1U);
    const auto capacity = tables[static_cast<std::size_t>(street)]->capacity();
    for (std::uint8_t hand_class = 0; hand_class < ca::preflop_hand_classes; ++hand_class) {
      for (std::uint16_t bucket = 0; bucket < capacity; ++bucket) {
        const auto mapped = rows.value().row(street, hand_class, bucket);
        if (mapped == ca::no_bucket) {
          continue;
        }
        const auto source = base.row(node.id, bucket);
        std::copy(source.begin(), source.end(), lifted.row(node.id, mapped).begin());
      }
    }
  }
  auto mapped_resources = response_resources(resources);
  mapped_resources.class_rows = &rows.value();
  const auto evaluator = pb::BestResponseEvaluator::create(game.value(), lifted, mapped_resources);
  const auto reference =
      pb::BestResponseEvaluator::create(game.value(), base, response_resources(resources));
  require(evaluator && reference, "mapped and base evaluators create");
  require(!pb::BestResponseEvaluator::create(game.value(), lifted, response_resources(resources)),
          "class policy without its mapping is rejected");
  require(!pb::BestResponseEvaluator::create(game.value(), base, mapped_resources),
          "base policy with a class mapping is rejected");
  const auto group = pb::full_runouts(resources.catalog->flops().front().cards);
  const auto actual = evaluator.value().evaluate_flop(group);
  const auto expected = reference.value().evaluate_flop(group);
  require(actual && expected, "full runout evaluations succeed");
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    for (const auto mode : {pb::average_mode, pb::response_mode}) {
      for (std::size_t entry = 0; entry < evaluator.value().entry_count(); ++entry) {
        for (std::size_t hand = 0; hand < ca::combo_count; ++hand) {
          require(close(actual.value().entry_values[hero][mode][entry][hand],
                        expected.value().entry_values[hero][mode][entry][hand], 1e-12),
                  "lifting identical strategies preserves physical EV and best response");
        }
      }
    }
  }
  std::cout << "class row lifting: " << layout.flop_capacity << "/" << layout.turn_capacity << "/"
            << layout.river_capacity << " rows, full-runout EV/BR equivalent PASS\n";
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
  if (left.postflop_entry_loss.size() != right.postflop_entry_loss.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.postflop_entry_loss.size(); ++index) {
    const auto &a = left.postflop_entry_loss[index];
    const auto &b = right.postflop_entry_loss[index];
    if (a.node != b.node || a.hero != b.hero || a.mean_gain != b.mean_gain ||
        a.opponent_reach != b.opponent_reach || a.entry_probability != b.entry_probability ||
        a.conditional_gain != b.conditional_gain) {
      return false;
    }
  }
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

// Independent oracle: enumerate ordered disjoint private deals and multiply
// the opponent's action probabilities along the public path. No per-card
// inclusion/exclusion sums or production reach propagation are used here.
std::vector<double>
enumerated_entry_probability(const pb::CompiledGame &game, const pb::BucketPolicy &policy,
                             const std::uint32_t entry, const std::uint8_t hero,
                             const std::array<std::vector<std::uint16_t>, 2> &subsets) {
  const auto &cards = ca::combo_table();
  std::vector<std::pair<std::uint32_t, std::size_t>> path;
  std::uint32_t cursor = entry;
  while (cursor != game.root()) {
    bool found = false;
    for (const auto &node : game.nodes()) {
      const auto edges = game.edges_of(node.id);
      for (std::size_t action = 0; action < edges.size(); ++action) {
        if (edges[action].child == cursor) {
          path.emplace_back(node.id, action);
          cursor = node.id;
          found = true;
          break;
        }
      }
      if (found) {
        break;
      }
    }
    require(found, "entry has a public path from the root");
  }
  const auto allowed = [&](const std::uint8_t player, const std::uint16_t hand) {
    return subsets[player].empty() ||
           std::find(subsets[player].begin(), subsets[player].end(), hand) != subsets[player].end();
  };
  std::vector<double> probability(cards.cards.size(), 0.0);
  for (std::uint16_t hand = 0; hand < cards.cards.size(); ++hand) {
    if (!allowed(hero, hand)) {
      continue;
    }
    double sum = 0.0;
    std::size_t count = 0;
    for (std::uint16_t other = 0; other < cards.cards.size(); ++other) {
      if (!allowed(static_cast<std::uint8_t>(1U - hero), other) ||
          (cards.masks[hand] & cards.masks[other]) != 0U) {
        continue;
      }
      ++count;
      double reach = 1.0;
      for (const auto &[node_id, action] : path) {
        const auto &node = game.nodes()[node_id];
        if (node.kind == pb::NodeKind::Decision && node.actor != hero) {
          reach *= policy.row(node_id, cards.hand_class[other])[action];
        }
      }
      sum += reach;
    }
    require(count > 0U, "every oracle hero combo has a compatible opponent");
    probability[hand] = sum / static_cast<double>(count);
  }
  return probability;
}

void test_entry_normalization(const Resources &resources) {
  const auto game =
      pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "normalization fixture compiles");
  const auto entries = game.value().postflop_entries();
  for (const bool deterministic : {false, true}) {
    auto policy = random_policy(game.value(), resources, 173U);
    if (deterministic) {
      for (const auto &node : game.value().nodes()) {
        if (node.kind == pb::NodeKind::Decision && node.street == gtosd::Street::Preflop) {
          for (std::uint32_t row = 0; row < ca::preflop_hand_classes; ++row) {
            auto probabilities = policy.row(node.id, row);
            std::fill(probabilities.begin(), probabilities.end(), 0.0);
            probabilities.front() = 1.0;
          }
        }
      }
    }
    for (const bool restricted : {false, true}) {
      std::array<std::vector<std::uint16_t>, 2> subsets;
      if (restricted) {
        subsets[0] = {0U, 1U, 35U, 180U};
        subsets[1] = {2U, 3U, 70U, 150U, 629U};
      }
      const auto evaluator = pb::BestResponseEvaluator::create(
          game.value(), policy, response_resources(resources), subsets);
      require(evaluator.has_value(), "normalization evaluator creates");
      // Inject analytically known leaf values to isolate aggregation from
      // showdown/CFR: responding gains exactly 2 antes whenever entry occurs.
      // This is an aggregation unit fixture, not a physical poker certificate.
      pb::FlopValues values;
      values.weight = 1.0;
      values.boards = 1U;
      values.images = {ca::identity_permutation};
      values.compatible.assign(ca::combo_table().cards.size(), 1U);
      std::array<std::vector<double>, 2> expected;
      for (std::uint8_t hero = 0; hero < 2U; ++hero) {
        for (const auto mode : {pb::average_mode, pb::response_mode}) {
          values.entry_values[hero][mode].resize(entries.size());
        }
        for (std::size_t index = 0; index < entries.size(); ++index) {
          const auto probabilities =
              enumerated_entry_probability(game.value(), policy, entries[index], hero, subsets);
          double sum = 0.0;
          for (const auto probability : probabilities) {
            sum += probability;
          }
          const auto count = subsets[hero].empty() ? probabilities.size() : subsets[hero].size();
          expected[hero].push_back(sum / static_cast<double>(count));
          auto &average = values.entry_values[hero][pb::average_mode][index];
          auto &response = values.entry_values[hero][pb::response_mode][index];
          average.resize(probabilities.size());
          response.resize(probabilities.size());
          for (std::size_t hand = 0; hand < probabilities.size(); ++hand) {
            average[hand] = -3.0 * probabilities[hand];
            response[hand] = -1.0 * probabilities[hand];
          }
        }
      }
      const auto exact = evaluator.value().aggregate({&values}, true);
      const auto partial = evaluator.value().aggregate({&values}, false);
      require(exact.has_value() && partial.has_value(), "analytic entry aggregation succeeds");
      require(exact.value().ev == partial.value().ev &&
                  exact.value().best_response == partial.value().best_response &&
                  exact.value().max_gain == partial.value().max_gain,
              "normalization availability does not change global values");
      std::size_t unreachable = 0U;
      for (const auto &loss : exact.value().postflop_entry_loss) {
        const auto found = std::find(entries.begin(), entries.end(), loss.node);
        require(found != entries.end(), "diagnostic entry exists");
        const auto index = static_cast<std::size_t>(found - entries.begin());
        require(close(loss.entry_probability, expected[loss.hero][index], 1e-12),
                "entry probability equals independent ordered-deal enumeration");
        require(close(loss.mean_gain, 2.0 * loss.entry_probability, 1e-12),
                "analytic two-ante gain has exactly one probability normalization");
        if (!restricted) {
          require(close(loss.entry_probability,
                        loss.opponent_reach / static_cast<double>(values.compatible.size()), 1e-12),
                  "full uniform range reduces to raw opponent mass / combo count");
        }
        if (loss.entry_probability == 0.0) {
          ++unreachable;
        }
        if (!restricted && loss.entry_probability > 0.0) {
          require(loss.conditional_gain && close(*loss.conditional_gain, 2.0, 1e-12),
                  "conditional gain is two antes, not two / 630");
        } else {
          require(!loss.conditional_gain,
                  "unreachable or restricted entry has no physical conditional EV");
        }
      }
      if (deterministic) {
        require(unreachable > 0U, "deterministic policy exercises unreachable entries");
      }
      for (const auto &loss : partial.value().postflop_entry_loss) {
        require(!loss.conditional_gain,
                "partial evaluation does not claim conditional physical EV");
      }
      pb::Certificate certificate;
      certificate.report = partial.value();
      const auto json = pb::certificate_json(certificate);
      require(json.find("\"conditional_gain\": null") != std::string::npos,
              "unavailable diagnostic serializes as JSON null");
    }
  }
  std::cout << "entry normalization: independent disjoint deals, blockers, fractional paths, "
               "subsets, zero reach PASS\n";
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

// The joint river engine must reproduce every double of the reference path,
// signed zeros included, so its tests compare bit patterns.
bool same_bits(const double left, const double right) {
  return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

bool same_flop_values(const pb::FlopValues &left, const pb::FlopValues &right) {
  if (left.flop != right.flop || !same_bits(left.weight, right.weight) ||
      left.boards != right.boards || left.images != right.images ||
      left.compatible != right.compatible) {
    return false;
  }
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    // The restricted set is empty on both sides without a street restriction.
    for (const auto mode : {pb::response_mode, pb::average_mode, pb::restricted_mode}) {
      const auto &left_entries = left.entry_values[hero][mode];
      const auto &right_entries = right.entry_values[hero][mode];
      if (left_entries.size() != right_entries.size()) {
        return false;
      }
      for (std::size_t entry = 0; entry < left_entries.size(); ++entry) {
        if (left_entries[entry].size() != right_entries[entry].size()) {
          return false;
        }
        for (std::size_t combo = 0; combo < left_entries[entry].size(); ++combo) {
          if (!same_bits(left_entries[entry][combo], right_entries[entry][combo])) {
            return false;
          }
        }
      }
    }
  }
  return true;
}

// Bucket policy with exact zeros: a third of the postflop decision nodes play
// one action in every row, so whole subtrees lose the reach of a hero and the
// traversals prune them; the other rows mix exact zeros and positive
// probabilities.
pb::BucketPolicy sparse_policy(const pb::CompiledGame &game, const pb::StateLayout &layout,
                               const std::uint64_t seed) {
  pb::BucketPolicy policy(game, layout);
  ca::DeterministicRandom random(seed);
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::Decision) {
      continue;
    }
    const auto rows = pb::StateLayout::rows_for(node.street, layout.flop_capacity,
                                                layout.turn_capacity, layout.river_capacity);
    const bool pure = node.street != gtosd::Street::Preflop && random.uniform_below(3U) == 0U;
    const std::size_t pure_action = random.uniform_below(node.action_count);
    for (std::uint32_t row = 0; row < rows; ++row) {
      auto probabilities = policy.row(node.id, row);
      double total = 0.0;
      for (std::size_t action = 0; action < probabilities.size(); ++action) {
        if (pure) {
          probabilities[action] = action == pure_action ? 1.0 : 0.0;
        } else {
          probabilities[action] = random.uniform_below(3U) == 0U ? 0.0 : random.uniform_unit();
        }
        total += probabilities[action];
      }
      if (total == 0.0) {
        probabilities[0] = 1.0;
        total = 1.0;
      }
      for (auto &probability : probabilities) {
        probability /= total;
      }
    }
  }
  return policy;
}

// Two canonical flops whose full runouts are the support of the test history
// maps.
std::vector<std::array<gtosd::CardId, 3>> support_flops(const Resources &resources) {
  const auto &flops = resources.catalog->flops();
  std::vector<std::array<gtosd::CardId, 3>> support;
  for (const std::size_t index : {std::size_t{0}, flops.size() / 3U}) {
    auto flop = flops[index].cards;
    std::sort(flop.begin(), flop.end());
    support.push_back(flop);
  }
  return support;
}

// Flat (v1) and hierarchical (v2) history maps over the full runouts of the
// support flops, so that every prefix hand of those flops has its rows, as in
// the trainer's history oracle.
struct HistoryMaps {
  std::vector<pb::HistoryObservation> observations;
  std::optional<pb::HistoryBucketRows> flat;
  std::optional<pb::HistoryBucketRows> hierarchy;
};

HistoryMaps build_history_maps(const Resources &resources,
                               const std::vector<std::array<gtosd::CardId, 3>> &support) {
  const pb::AbstractionTables tables{&*resources.catalog, &*resources.flop, &*resources.turn,
                                     &*resources.river};
  std::map<std::uint64_t, std::uint64_t> weights;
  for (const auto &flop : support) {
    for (const auto &board : pb::full_runouts(flop).boards) {
      const auto context = pb::BoardContext::build(board.history, *resources.ranks, &tables);
      require(context.has_value(), "history support context builds");
      for (std::uint16_t hand = 0; hand < pb::live_hand_count; ++hand) {
        const auto fkey = static_cast<std::uint64_t>(context.value().hand_classes()[hand]) *
                              resources.flop->capacity() +
                          context.value().row(gtosd::Street::Flop, hand);
        const auto tkey =
            fkey * resources.turn->capacity() + context.value().row(gtosd::Street::Turn, hand);
        ++weights[tkey * resources.river->capacity() +
                  context.value().row(gtosd::Street::River, hand)];
      }
    }
  }
  HistoryMaps maps;
  for (const auto &[key, weight] : weights) {
    maps.observations.push_back({key, weight});
  }
  auto flat = pb::HistoryBucketRows::build(*resources.flop, *resources.turn, *resources.river,
                                           maps.observations, 2U);
  auto hierarchy = pb::HistoryBucketRows::build_hierarchy(
      *resources.flop, *resources.turn, *resources.river, maps.observations, 2U, 2U);
  require(flat.has_value() && hierarchy.has_value(), "history maps build");
  maps.flat.emplace(std::move(flat.value()));
  maps.hierarchy.emplace(std::move(hierarchy.value()));
  return maps;
}

// river_row(river_cursor(...)) equals row(Street::River, ...) in both map
// formats: on observed keys (at most about 50,000 per map, evenly spaced), on
// other river buckets below the same turn histories, on arbitrary keys and on
// out-of-range inputs.
void test_history_river_cursor(const Resources &resources, const HistoryMaps &maps) {
  const auto started = Clock::now();
  const std::uint64_t flop_capacity = resources.flop->capacity();
  const std::uint64_t turn_capacity = resources.turn->capacity();
  const std::uint64_t river_capacity = resources.river->capacity();
  constexpr std::size_t observed_checks = 50'000U;
  const std::size_t stride = std::max<std::size_t>(1U, maps.observations.size() / observed_checks);
  std::uint64_t lookups = 0U;
  for (const pb::HistoryBucketRows *rows : {&*maps.flat, &*maps.hierarchy}) {
    const std::string message =
        std::string("cursor rows equal the map rows: ") + rows->format_name();
    const auto check = [&](const std::uint64_t hand_class, const std::uint64_t flop,
                           const std::uint64_t turn, const std::uint64_t river) {
      const auto class_index = static_cast<std::uint8_t>(hand_class);
      const auto flop_bucket = static_cast<std::uint16_t>(flop);
      const auto turn_bucket = static_cast<std::uint16_t>(turn);
      const auto river_bucket = static_cast<std::uint16_t>(river);
      const auto expected =
          rows->row(gtosd::Street::River, class_index, flop_bucket, turn_bucket, river_bucket);
      require(rows->river_row(rows->river_cursor(class_index, flop_bucket, turn_bucket),
                              river_bucket) == expected,
              message);
      ++lookups;
      return expected;
    };
    // Draws in [0, bound], one past the capacity included.
    ca::DeterministicRandom random(0x5031'0101ULL);
    const auto draw_up_to = [&](const std::uint64_t bound) {
      const auto limit = static_cast<std::uint32_t>(bound) + 1U;
      return static_cast<std::uint64_t>(random.uniform_below(limit));
    };
    for (std::size_t index = 0; index < maps.observations.size(); index += stride) {
      const auto &observation = maps.observations[index];
      const auto river = observation.key % river_capacity;
      const auto turn_key = observation.key / river_capacity;
      const auto flop_key = turn_key / turn_capacity;
      const auto hand_class = flop_key / flop_capacity;
      require(check(hand_class, flop_key % flop_capacity, turn_key % turn_capacity, river) !=
                  pb::no_history_row,
              "observed keys have river rows");
      const auto other_river = draw_up_to(river_capacity);
      static_cast<void>(
          check(hand_class, flop_key % flop_capacity, turn_key % turn_capacity, other_river));
    }
    constexpr int arbitrary_keys = 20'000;
    for (int draw = 0; draw < arbitrary_keys; ++draw) {
      const auto hand_class = draw_up_to(ca::preflop_hand_classes);
      const auto flop = draw_up_to(flop_capacity);
      const auto turn = draw_up_to(turn_capacity);
      const auto river = draw_up_to(river_capacity);
      static_cast<void>(check(hand_class, flop, turn, river));
    }
  }
  std::cout << "history river cursor: " << lookups << " lookups equal row() in v1 and v2, "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

// RiverBoard with a RiverPrefix against BoardContext::build with the same
// tables: the same boards accepted, the same hands, ranks, rank order and
// river rows, with plain buckets, class rows and both history formats, on
// boards of the history support and on arbitrary boards.
void test_river_board_rows(const Resources &resources, const HistoryMaps &maps,
                           const std::vector<std::array<gtosd::CardId, 3>> &support) {
  const auto class_rows =
      pb::ClassBucketRows::build(*resources.flop, *resources.turn, *resources.river);
  require(class_rows.has_value(), "class rows build");
  struct RowSource {
    const pb::ClassBucketRows *class_rows;
    const pb::HistoryBucketRows *history_rows;
  };
  const std::array<RowSource, 4> sources{{{nullptr, nullptr},
                                          {&class_rows.value(), nullptr},
                                          {nullptr, &*maps.flat},
                                          {nullptr, &*maps.hierarchy}}};
  ca::DeterministicRandom random(0x5031'0102ULL);
  pb::RiverPrefix prefix;
  pb::RiverBoard river;
  std::uint32_t compared = 0U;
  std::uint32_t rejected = 0U;
  constexpr int boards_per_source = 24;
  for (const auto &source : sources) {
    const pb::AbstractionTables tables{&*resources.catalog, &*resources.flop,
                                       &*resources.turn,    &*resources.river,
                                       source.class_rows,   source.history_rows};
    for (int draw = 0; draw < boards_per_source; ++draw) {
      auto history = resources.catalog->sample_physical_history(random);
      const bool on_support = draw % 2 == 0;
      if (on_support) {
        const auto group = pb::full_runouts(
            support[random.uniform_below(static_cast<std::uint32_t>(support.size()))]);
        history =
            group.boards[random.uniform_below(static_cast<std::uint32_t>(group.boards.size()))]
                .history;
      }
      const auto context = pb::BoardContext::build(history, *resources.ranks, &tables);
      const bool built = prefix.assign(history.flop, history.turn, tables).has_value() &&
                         river.assign(history, *resources.ranks, &prefix).has_value();
      require(built == context.has_value(), "river board and board context accept the same boards");
      require(built || (source.history_rows != nullptr && !on_support),
              "boards with complete tables are accepted");
      if (!built) {
        ++rejected;
        continue;
      }
      for (std::uint16_t hand = 0; hand < pb::live_hand_count; ++hand) {
        require(river.combo_ids()[hand] == context.value().combo_ids()[hand] &&
                    river.cards()[hand] == context.value().cards()[hand] &&
                    river.ranks()[hand] == context.value().ranks()[hand] &&
                    river.order_by_rank()[hand] == context.value().order_by_rank()[hand],
                "river board has the hands, ranks and rank order of the context");
        require(river.rows()[hand] == context.value().row(gtosd::Street::River, hand),
                "river board rows equal the context rows");
      }
      ++compared;
    }
  }
  std::cout << "river board rows: " << compared << " boards equal to BoardContext, " << rejected
            << " rejected by both (outside the history support)\n";
}

// Opponent reach of one lane: none, on about half the hands, on every hand,
// or only on the live hands holding one card (Blocked). With Blocked, a hero
// hand holding that card blocks all the opponent reach: its disjoint mass is
// zero (or a rounding residue of the per-card sums), so its fold value can be
// -0.0 when the hero folded, the case where the operand order of the response
// maximum and the 0.0 + x starting the sums decide the bits.
enum class LaneFill { Zero, Sparse, Dense, Blocked };

void fill_lane(std::array<double, pb::pair_values> &reach, const std::size_t lane,
               const LaneFill fill, const pb::RiverBoard &board,
               ca::DeterministicRandom &random) {
  const auto cards = board.cards();
  const auto holder = random.uniform_below(static_cast<std::uint32_t>(pb::live_hand_count));
  const auto held = cards[holder][0];
  for (std::size_t hand = 0; hand < pb::live_hand_count; ++hand) {
    const bool holds = cards[hand][0] == held || cards[hand][1] == held;
    double value = 0.0;
    if (fill == LaneFill::Dense || (fill == LaneFill::Sparse && random.uniform_below(2U) == 0U) ||
        (fill == LaneFill::Blocked && holds)) {
      value = random.uniform_unit();
    }
    reach[pb::hero_lanes * hand + lane] = value;
  }
}

// JointRiverTraversal against ValueTraversal on every river subtree of the
// HU10 trees: each hero and mode bit for bit, with sparse, empty or blocked
// reach lanes and a policy with exact zeros; rows outside the policy layout
// fail both.
void test_joint_traversal(const Resources &resources) {
  const auto started = Clock::now();
  const pb::HeadsUpShowdownKernel kernel;
  const pb::AbstractionTables tables{&*resources.catalog, &*resources.flop, &*resources.turn,
                                     &*resources.river};
  const std::array<std::array<LaneFill, pb::hero_lanes>, 7> fills{
      {{LaneFill::Sparse, LaneFill::Sparse},
       {LaneFill::Dense, LaneFill::Sparse},
       {LaneFill::Sparse, LaneFill::Zero},
       {LaneFill::Zero, LaneFill::Dense},
       {LaneFill::Zero, LaneFill::Zero},
       {LaneFill::Blocked, LaneFill::Dense},
       {LaneFill::Dense, LaneFill::Blocked}}};
  constexpr int boards_per_fixture = 3;
  ca::DeterministicRandom random(0x5031'0104ULL);
  pb::RiverPrefix prefix;
  pb::RiverBoard river;
  std::array<double, pb::pair_values> reach{};
  std::array<double, pb::pair_values> response{};
  std::array<double, pb::pair_values> average{};
  std::array<double, pb::live_hand_count> lane_reach{};
  std::array<double, pb::live_hand_count> expected{};
  std::uint64_t compared = 0U;
  for (const std::string_view fixture :
       {"preflop_blueprint_hu10_reduced_v1.json", "preflop_blueprint_hu10_full_v1.json"}) {
    const auto game = pb::CompiledGame::compile(load_fixture(fixture));
    require(game.has_value(), "joint traversal fixture compiles");
    const auto layout =
        pb::layout_state(game.value(), resources.flop->capacity(), resources.turn->capacity(),
                         resources.river->capacity());
    const auto policy = sparse_policy(game.value(), layout, 0x5031'0103ULL);
    std::vector<std::uint32_t> roots;
    for (const auto &node : game.value().nodes()) {
      if (node.kind == pb::NodeKind::Chance && node.street == gtosd::Street::Turn) {
        roots.push_back(game.value().edges_of(node.id)[0].child);
      }
    }
    require(!roots.empty(), "the fixture has river subtrees");
    pb::JointRiverTraversal joint(game.value(), policy);
    for (int draw = 0; draw < boards_per_fixture; ++draw) {
      const auto history = resources.catalog->sample_physical_history(random);
      const auto context = pb::BoardContext::build(history, *resources.ranks, &tables);
      require(context.has_value() &&
                  prefix.assign(history.flop, history.turn, tables).has_value() &&
                  river.assign(history, *resources.ranks, &prefix).has_value(),
              "board context and river board build");
      pb::ValueTraversal traversal(game.value(), context.value(), kernel, nullptr);
      for (const auto root : roots) {
        for (const auto &fill : fills) {
          for (std::size_t lane = 0; lane < pb::hero_lanes; ++lane) {
            fill_lane(reach, lane, fill[lane], river, random);
          }
          require(joint.evaluate(root, river, reach, response, average).has_value(),
                  "joint traversal evaluates");
          for (std::uint8_t hero = 0; hero < pb::hero_lanes; ++hero) {
            for (std::size_t hand = 0; hand < pb::live_hand_count; ++hand) {
              lane_reach[hand] = reach[pb::hero_lanes * hand + hero];
            }
            for (const bool best : {true, false}) {
              pb::TraversalOptions options;
              options.best_response = best;
              require(traversal.evaluate_from(root, policy, hero, lane_reach, expected, options)
                          .has_value(),
                      "reference traversal evaluates");
              const auto &values = best ? response : average;
              for (std::size_t hand = 0; hand < pb::live_hand_count; ++hand) {
                require(same_bits(values[pb::hero_lanes * hand + hero], expected[hand]),
                        "joint traversal equals ValueTraversal bit for bit");
              }
            }
          }
          ++compared;
        }
      }
    }

    // A layout with one river row leaves the rows of the board outside it:
    // the reference fails on the first row it reads, the joint traversal too.
    const auto history = resources.catalog->sample_physical_history(random);
    const auto context = pb::BoardContext::build(history, *resources.ranks, &tables);
    require(context.has_value() && prefix.assign(history.flop, history.turn, tables).has_value() &&
                river.assign(history, *resources.ranks, &prefix).has_value(),
            "board context and river board build");
    const auto narrow = pb::layout_state(game.value(), resources.flop->capacity(),
                                         resources.turn->capacity(), 1U);
    const pb::BucketPolicy narrow_policy(game.value(), narrow);
    pb::JointRiverTraversal narrow_joint(game.value(), narrow_policy);
    pb::ValueTraversal traversal(game.value(), context.value(), kernel, nullptr);
    reach.fill(1.0);
    lane_reach.fill(1.0);
    const pb::TraversalOptions policy_options{};
    const auto joint_result = narrow_joint.evaluate(roots.front(), river, reach, response, average);
    const auto first = traversal.evaluate_from(roots.front(), narrow_policy, 0U, lane_reach,
                                               expected, policy_options);
    const auto second = traversal.evaluate_from(roots.front(), narrow_policy, 1U, lane_reach,
                                                expected, policy_options);
    require(river.maximum_row() == 0U ||
                (!joint_result.has_value() &&
                 joint_result.error() == pb::KernelError::MissingTable && !first.has_value() &&
                 !second.has_value()),
            "rows outside the layout fail the joint and the reference traversal");
  }
  std::cout << "joint river traversal: " << compared
            << " subtree evaluations equal ValueTraversal for both heroes and modes, "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

// evaluate_flop with the joint engine against the reference engine: the flop
// values are bit-identical with plain bucket rows on three flops, with hand
// subsets, with class rows and with both history formats; the certifier
// gives the same report with either engine, exact and sampled.
void test_joint_engine_matches_reference(const Resources &resources, const HistoryMaps &maps,
                                         const std::vector<std::array<gtosd::CardId, 3>> &support) {
  const auto started = Clock::now();
  const auto game =
      pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  std::uint32_t flops_compared = 0U;
  double joint_seconds = 0.0;
  double reference_seconds = 0.0;
  const auto compare = [&](const pb::BestResponseEvaluator &evaluator,
                           const std::array<gtosd::CardId, 3> &flop, const std::string &what) {
    require(evaluator.river_engine() == pb::RiverEngine::Joint, "the joint engine is the default");
    auto reference = evaluator;
    reference.set_river_engine(pb::RiverEngine::Reference);
    const auto group = pb::full_runouts(flop);
    const auto joint_started = Clock::now();
    const auto joint_values = evaluator.evaluate_flop(group);
    const auto reference_started = Clock::now();
    const auto reference_values = reference.evaluate_flop(group);
    joint_seconds += std::chrono::duration<double>(reference_started - joint_started).count();
    reference_seconds += std::chrono::duration<double>(Clock::now() - reference_started).count();
    require(joint_values.has_value() && reference_values.has_value(),
            "both river engines evaluate: " + what);
    require(same_flop_values(joint_values.value(), reference_values.value()),
            "joint river engine equals the reference bit for bit: " + what);
    ++flops_compared;
  };
  const auto view = response_resources(resources);
  const auto layout =
      pb::layout_state(game.value(), resources.flop->capacity(), resources.turn->capacity(),
                       resources.river->capacity());
  const auto policy = sparse_policy(game.value(), layout, 0x5031'0105ULL);
  const auto plain = pb::BestResponseEvaluator::create(game.value(), policy, view);
  require(plain.has_value(), "bucket evaluator creates");
  const auto &catalog_flops = resources.catalog->flops();
  for (const std::size_t index :
       {std::size_t{1}, catalog_flops.size() / 2U, catalog_flops.size() - 1U}) {
    auto flop = catalog_flops[index].cards;
    std::sort(flop.begin(), flop.end());
    compare(plain.value(), flop, "bucket rows");
  }

  // Player 1 holds only hands with one card off the flop: hero 0 hands with
  // that card have no opponent mass and are skipped by the accumulation.
  std::uint8_t held_card = 0U;
  while (std::any_of(support[0].begin(), support[0].end(),
                     [&](const gtosd::CardId card) { return card.value() == held_card; })) {
    ++held_card;
  }
  std::array<std::vector<std::uint16_t>, 2> subsets;
  for (std::size_t combo = 0; combo < ca::combo_count; ++combo) {
    const auto &cards = ca::combo_table().cards[combo];
    if (combo % 3U != 0U) {
      subsets[0].push_back(static_cast<std::uint16_t>(combo));
    }
    if (cards[0] == held_card || cards[1] == held_card) {
      subsets[1].push_back(static_cast<std::uint16_t>(combo));
    }
  }
  const auto restricted = pb::BestResponseEvaluator::create(game.value(), policy, view, subsets);
  require(restricted.has_value(), "restricted evaluator creates");
  compare(restricted.value(), support[0], "hand subsets");

  const auto class_rows =
      pb::ClassBucketRows::build(*resources.flop, *resources.turn, *resources.river);
  require(class_rows.has_value(), "class rows build");
  const auto class_layout = pb::layout_state(game.value(),
                                             class_rows.value().count(ca::BucketStreet::Flop),
                                             class_rows.value().count(ca::BucketStreet::Turn),
                                             class_rows.value().count(ca::BucketStreet::River));
  const auto class_policy = sparse_policy(game.value(), class_layout, 0x5031'0106ULL);
  auto class_view = view;
  class_view.class_rows = &class_rows.value();
  const auto classes = pb::BestResponseEvaluator::create(game.value(), class_policy, class_view);
  require(classes.has_value(), "class evaluator creates");
  compare(classes.value(), support[1], "class rows");

  for (const pb::HistoryBucketRows *history : {&*maps.flat, &*maps.hierarchy}) {
    const auto history_layout = pb::layout_state(game.value(),
                                                 history->count(ca::BucketStreet::Flop),
                                                 history->count(ca::BucketStreet::Turn),
                                                 history->count(ca::BucketStreet::River));
    const auto history_policy = sparse_policy(game.value(), history_layout, 0x5031'0107ULL);
    auto history_view = view;
    history_view.history_rows = history;
    const auto mapped =
        pb::BestResponseEvaluator::create(game.value(), history_policy, history_view);
    require(mapped.has_value(), "history evaluator creates");
    compare(mapped.value(), support[0], history->format_name());
  }

  // The whole certificate JSON (17 significant digits, so every double and
  // the sign of a zero show) must agree once the timing and memory fields,
  // which differ between any two runs, are zeroed.
  const auto canonical = [](pb::Certificate certificate) {
    certificate.seconds = 0.0;
    certificate.evaluation_seconds = 0.0;
    certificate.aggregation_seconds = 0.0;
    certificate.report.seconds = 0.0;
    certificate.process_bytes = 0U;
    return pb::certificate_json(certificate);
  };
  for (const std::uint32_t sampled : {0U, 2U}) {
    pb::CertifierOptions options;
    options.threads = 2U;
    options.flop_limit = 2U;
    options.sample_flops = sampled;
    const auto joint = pb::certify(game.value(), policy, view, options);
    options.river_engine = pb::RiverEngine::Reference;
    const auto reference = pb::certify(game.value(), policy, view, options);
    require(joint.has_value() && reference.has_value() &&
                canonical(joint.value()) == canonical(reference.value()),
            "the certifier gives the same certificate with either river engine");
  }
  std::cout << "joint river engine: " << flops_compared
            << " flops bit-identical to the reference engine (evaluate_flop " << joint_seconds
            << " s joint, " << reference_seconds
            << " s reference), certificates identical, "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

// Allowance between gains or values computed along different summations.
constexpr double summation_tolerance = 1e-12;

// The certificate JSON without the fields that differ between any two runs
// of the same pass (timings, memory, resumed flops).
std::string canonical_certificate(pb::Certificate certificate) {
  certificate.seconds = 0.0;
  certificate.evaluation_seconds = 0.0;
  certificate.aggregation_seconds = 0.0;
  certificate.report.seconds = 0.0;
  certificate.process_bytes = 0U;
  certificate.resumed_flops = 0U;
  return pb::certificate_json(certificate);
}

// Bit-for-bit equality of two sets of entry values ([entry][combo]).
bool same_entry_values(const std::vector<std::vector<double>> &left,
                       const std::vector<std::vector<double>> &right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t entry = 0; entry < left.size(); ++entry) {
    if (left[entry].size() != right[entry].size()) {
      return false;
    }
    for (std::size_t combo = 0; combo < left[entry].size(); ++combo) {
      if (!same_bits(left[entry][combo], right[entry][combo])) {
        return false;
      }
    }
  }
  return true;
}

// Street-restricted best response (deviation_from). On one flop the Flop and
// None restrictions repeat the response and the average entry values bit for
// bit, and no restriction changes the two modes. On partial certificates of
// HU10 reduced: Preflop is the default certificate byte for byte, None gains
// exactly zero, the gain never grows as the first deviating street moves
// later, both river engines give the same certificate under every
// restriction (exact and sampled), the report fields follow their restricted
// definitions, and a restricted state resumes bit for bit under its own
// restriction only. On one flop of HU10 full (three turns with all their
// rivers, since the order holds on any board set): the entry values and the
// gains follow the same order.
void test_street_restricted_response(const Resources &resources,
                                     const std::filesystem::path &scratch) {
  const auto started = Clock::now();
  const auto game =
      pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  const auto policy = random_policy(game.value(), resources, 31U);
  const auto view = response_resources(resources);
  // Each restriction lets the responder deviate on fewer streets than the
  // previous one.
  const std::array<pb::DeviationStreet, 5> streets{
      pb::DeviationStreet::Preflop, pb::DeviationStreet::Flop, pb::DeviationStreet::Turn,
      pb::DeviationStreet::River, pb::DeviationStreet::None};
  for (const auto street : streets) {
    const auto parsed = pb::parse_deviation_street(pb::deviation_street_name(street));
    require(parsed.has_value() && *parsed == street, "deviation street names parse back");
  }
  require(!pb::parse_deviation_street("showdown").has_value(),
          "unknown deviation streets are rejected");

  // One flop: the two modes never change, Flop and None repeat a mode.
  {
    const auto evaluator = pb::BestResponseEvaluator::create(game.value(), policy, view);
    require(evaluator.has_value(), "evaluator creates");
    require(evaluator.value().deviation_from() == pb::DeviationStreet::Preflop,
            "the full best response is the default");
    auto flop = resources.catalog->flops()[resources.catalog->flops().size() / 4U].cards;
    std::sort(flop.begin(), flop.end());
    const auto group = pb::full_runouts(flop);
    const auto plain = evaluator.value().evaluate_flop(group);
    require(plain.has_value(), "unrestricted flop evaluates");
    for (std::uint8_t hero = 0; hero < 2U; ++hero) {
      require(plain.value().entry_values[hero][pb::restricted_mode].empty(),
              "an unrestricted evaluation has no restricted values");
    }
    for (const auto street : {pb::DeviationStreet::Flop, pb::DeviationStreet::None}) {
      auto restricted = evaluator.value();
      restricted.set_deviation_from(street);
      const auto values = restricted.evaluate_flop(group);
      require(values.has_value(), "restricted flop evaluates");
      const auto repeated =
          street == pb::DeviationStreet::Flop ? pb::response_mode : pb::average_mode;
      for (std::uint8_t hero = 0; hero < 2U; ++hero) {
        for (const auto mode : {pb::response_mode, pb::average_mode}) {
          require(same_entry_values(values.value().entry_values[hero][mode],
                                    plain.value().entry_values[hero][mode]),
                  "a street restriction leaves both modes bit for bit");
        }
        require(same_entry_values(values.value().entry_values[hero][pb::restricted_mode],
                                  plain.value().entry_values[hero][repeated]),
                "Flop repeats the response values and None the average ones bit for bit");
      }
    }
  }

  // Partial certificates, every restriction with both river engines.
  pb::CertifierOptions options;
  options.threads = 2U;
  options.flop_limit = 2U;
  const auto baseline = pb::certify(game.value(), policy, view, options);
  require(baseline.has_value(), "default partial pass succeeds");
  const auto baseline_json = canonical_certificate(baseline.value());
  require(baseline_json.find("deviation_from") == std::string::npos,
          "the default certificate has no deviation_from entry");
  std::vector<pb::Certificate> certificates;
  for (const auto street : streets) {
    options.deviation_from = street;
    options.river_engine = pb::RiverEngine::Joint;
    const auto joint = pb::certify(game.value(), policy, view, options);
    options.river_engine = pb::RiverEngine::Reference;
    const auto reference = pb::certify(game.value(), policy, view, options);
    require(joint.has_value() && reference.has_value(), "restricted partial passes succeed");
    const auto joint_json = canonical_certificate(joint.value());
    require(joint_json == canonical_certificate(reference.value()),
            std::string("both river engines give the same certificate under deviation_from ") +
                pb::deviation_street_name(street));
    require((street == pb::DeviationStreet::Preflop) ==
                (joint_json.find("deviation_from") == std::string::npos),
            "only a restricted certificate names its restriction");
    certificates.push_back(joint.value());
  }
  require(canonical_certificate(certificates.front()) == baseline_json,
          "deviation_from preflop is the default certificate byte for byte");

  const auto &full_report = baseline.value().report;
  for (std::size_t index = 1; index < certificates.size(); ++index) {
    const auto &report = certificates[index].report;
    require(report.deviation_from == streets[index], "the report names its restriction");
    std::array<double, 2> route_gain{};
    for (const auto &route : report.postflop_entry_route) {
      require(same_bits(route.response_probability, route.average_probability),
              "a restricted responder follows the average preflop route");
      route_gain[route.hero] += route.postflop_gain_on_response_route;
    }
    for (std::uint8_t player = 0; player < 2U; ++player) {
      require(same_bits(report.ev[player], full_report.ev[player]),
              "a street restriction leaves ev bit for bit");
      require(same_bits(report.best_response_preflop[player], report.ev[player]) &&
                  report.gain_preflop[player] == 0.0,
              "a restricted responder has no preflop-only deviation");
      require(same_bits(report.best_response_lower[player], report.best_response[player]) &&
                  same_bits(report.gain_lower[player], report.gain[player]),
              "a restricted responder's lower bound is its own value");
      require(close(route_gain[player],
                    report.best_response[player] -
                        report.best_response_route_average_value[player],
                    1e-9),
              "restricted entry gains add up to the restricted continuation gain");
      require(report.gain[player] <=
                  certificates[index - 1].report.gain[player] + summation_tolerance,
              "a later first deviating street never gains more");
    }
    require(report.best_response_preflop_mix.size() ==
                full_report.best_response_preflop_mix.size(),
            "a restricted mix covers the preflop nodes of the full one");
    for (std::size_t slot = 0; slot < report.best_response_preflop_mix.size(); ++slot) {
      const auto &mix = report.best_response_preflop_mix[slot];
      const auto &full_mix = full_report.best_response_preflop_mix[slot];
      require(mix.node == full_mix.node && mix.hero == full_mix.hero && mix.split_classes == 0U,
              "a restricted mix has the nodes of the full one and no split classes");
      for (std::size_t action = 0; action < mix.action_count; ++action) {
        double expected = 0.0;
        for (std::uint32_t hand_class = 0; hand_class < ca::preflop_hand_classes; ++hand_class) {
          expected += policy.row(mix.node, hand_class)[action];
        }
        expected /= static_cast<double>(ca::preflop_hand_classes);
        require(close(mix.frequency[action], expected, summation_tolerance),
                "a restricted preflop mix is the policy's");
      }
    }
  }
  // From the flop on the responder uses the full response's entry values with
  // the average preflop: the full lower bound, along another summation.
  for (std::uint8_t player = 0; player < 2U; ++player) {
    require(close(certificates[1].report.best_response[player],
                  full_report.best_response_lower[player], summation_tolerance),
            "deviation_from flop is the full response's lower bound");
  }
  const auto &never = certificates.back().report;
  for (std::uint8_t player = 0; player < 2U; ++player) {
    require(same_bits(never.best_response[player], never.ev[player]) &&
                never.gain[player] == 0.0,
            "deviation_from none gains exactly zero");
  }
  require(never.max_gain == 0.0 && never.max_gain_lower == 0.0 && never.nashconv == 0.0,
          "deviation_from none has zero max gains and nashconv");

  // The sampled pass under a restriction, with either river engine.
  {
    pb::CertifierOptions sampled;
    sampled.threads = 2U;
    sampled.sample_flops = 2U;
    const auto unrestricted = pb::certify(game.value(), policy, view, sampled);
    sampled.deviation_from = pb::DeviationStreet::Turn;
    const auto joint = pb::certify(game.value(), policy, view, sampled);
    sampled.river_engine = pb::RiverEngine::Reference;
    const auto reference = pb::certify(game.value(), policy, view, sampled);
    require(unrestricted.has_value() && joint.has_value() && reference.has_value() &&
                joint.value().sampled,
            "restricted sampled passes succeed");
    require(canonical_certificate(joint.value()) == canonical_certificate(reference.value()),
            "both river engines give the same restricted sampled certificate");
    for (std::uint8_t player = 0; player < 2U; ++player) {
      require(same_bits(joint.value().report.ev[player], unrestricted.value().report.ev[player]) &&
                  joint.value().report.gain[player] <=
                      unrestricted.value().report.gain[player] + summation_tolerance,
              "a restricted sampled pass keeps ev and never gains more");
    }
  }

  // A restricted state resumes bit for bit under its own restriction only,
  // and a restricted pass refuses an unrestricted state.
  {
    pb::CertifierOptions stored;
    stored.threads = 2U;
    stored.chunk_flops = 1U;
    stored.flop_limit = 1U;
    stored.deviation_from = pb::DeviationStreet::River;
    stored.state_path = scratch / "certifier_state_river.bin";
    std::filesystem::remove(stored.state_path);
    const auto first = pb::certify(game.value(), policy, view, stored);
    require(first.has_value() && first.value().flops == 1U, "restricted pass stores one flop");
    stored.flop_limit = 2U;
    const auto resumed = pb::certify(game.value(), policy, view, stored);
    require(resumed.has_value() && resumed.value().resumed_flops == 1U,
            "restricted pass resumes the stored flop");
    require(canonical_certificate(resumed.value()) == canonical_certificate(certificates[3]),
            "a resumed restricted pass equals the continuous one bit for bit");
    for (const auto street : {pb::DeviationStreet::Preflop, pb::DeviationStreet::Turn}) {
      auto other = stored;
      other.deviation_from = street;
      const auto rejected = pb::certify(game.value(), policy, view, other);
      require(!rejected.has_value() && rejected.error() == pb::CertifierError::IntegrityFailure,
              "a state of another restriction is rejected");
    }
    pb::CertifierOptions unrestricted;
    unrestricted.threads = 2U;
    unrestricted.flop_limit = 1U;
    unrestricted.state_path = scratch / "certifier_state_unrestricted.bin";
    std::filesystem::remove(unrestricted.state_path);
    require(pb::certify(game.value(), policy, view, unrestricted).has_value(),
            "unrestricted pass stores one flop");
    unrestricted.deviation_from = pb::DeviationStreet::Flop;
    const auto refused = pb::certify(game.value(), policy, view, unrestricted);
    require(!refused.has_value() && refused.error() == pb::CertifierError::IntegrityFailure,
            "a restricted pass rejects an unrestricted state");
  }

  // HU10 full, one flop with three turns and all their rivers.
  {
    const auto full =
        pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_full_v1.json"));
    require(full.has_value(), "HU10 full compiles");
    const auto full_policy = random_policy(full.value(), resources, 37U);
    const auto evaluator = pb::BestResponseEvaluator::create(full.value(), full_policy, view);
    require(evaluator.has_value(), "HU10 full evaluator creates");
    auto flop = resources.catalog->flops()[resources.catalog->flops().size() / 3U].cards;
    std::sort(flop.begin(), flop.end());
    auto group = pb::full_runouts(flop);
    constexpr std::size_t kept_turns = 3U;
    std::vector<std::uint8_t> turns;
    for (const auto &board : group.boards) {
      const auto turn = board.history.turn.value();
      if (turns.size() < kept_turns && std::find(turns.begin(), turns.end(), turn) == turns.end()) {
        turns.push_back(turn);
      }
    }
    group.boards.erase(std::remove_if(group.boards.begin(), group.boards.end(),
                                      [&](const pb::WeightedBoard &kept) {
                                        return std::find(turns.begin(), turns.end(),
                                                         kept.history.turn.value()) ==
                                               turns.end();
                                      }),
                       group.boards.end());
    const std::array<pb::DeviationStreet, 3> deviating{
        pb::DeviationStreet::Flop, pb::DeviationStreet::Turn, pb::DeviationStreet::River};
    std::vector<pb::FlopValues> restricted_values;
    for (const auto street : deviating) {
      auto street_evaluator = evaluator.value();
      street_evaluator.set_deviation_from(street);
      auto values = street_evaluator.evaluate_flop(group);
      require(values.has_value(), "HU10 full restricted flop evaluates");
      restricted_values.push_back(std::move(values.value()));
    }
    const auto at_most = [](const double lower, const double upper) {
      return lower <= upper + summation_tolerance * std::max(1.0, std::abs(upper));
    };
    for (std::uint8_t hero = 0; hero < 2U; ++hero) {
      const auto &average = restricted_values[0].entry_values[hero][pb::average_mode];
      const auto &response = restricted_values[0].entry_values[hero][pb::response_mode];
      const auto &from_turn = restricted_values[1].entry_values[hero][pb::restricted_mode];
      const auto &from_river = restricted_values[2].entry_values[hero][pb::restricted_mode];
      require(same_entry_values(restricted_values[0].entry_values[hero][pb::restricted_mode],
                                response),
              "HU10 full: Flop repeats the response values bit for bit");
      for (std::size_t entry = 0; entry < average.size(); ++entry) {
        for (std::size_t combo = 0; combo < average[entry].size(); ++combo) {
          require(at_most(average[entry][combo], from_river[entry][combo]) &&
                      at_most(from_river[entry][combo], from_turn[entry][combo]) &&
                      at_most(from_turn[entry][combo], response[entry][combo]),
                  "HU10 full: entry values grow as the first deviating street moves earlier");
        }
      }
    }
    // The two modes do not depend on the restriction: the default evaluator
    // aggregates the full response from any of the three evaluations.
    const auto unrestricted = evaluator.value().aggregate({&restricted_values[0]}, false);
    require(unrestricted.has_value(), "HU10 full aggregation succeeds");
    std::array<double, 2> bound = unrestricted.value().gain;
    for (std::size_t index = 0; index < deviating.size(); ++index) {
      auto street_evaluator = evaluator.value();
      street_evaluator.set_deviation_from(deviating[index]);
      const auto report = street_evaluator.aggregate({&restricted_values[index]}, false);
      require(report.has_value(), "HU10 full restricted aggregation succeeds");
      for (std::uint8_t player = 0; player < 2U; ++player) {
        require(report.value().gain[player] <= bound[player] + summation_tolerance,
                "HU10 full: a later first deviating street never gains more");
        bound[player] = report.value().gain[player];
      }
    }
    for (std::uint8_t player = 0; player < 2U; ++player) {
      require(bound[player] >= -summation_tolerance,
              "HU10 full: the river-restricted gain is not negative");
    }
  }

  std::cout << "street-restricted response: HU10 reduced gains";
  for (std::size_t index = 0; index < certificates.size(); ++index) {
    std::cout << ' ' << pb::deviation_street_name(streets[index]) << " ["
              << certificates[index].report.gain[0] << ", "
              << certificates[index].report.gain[1] << "] ("
              << certificates[index].evaluation_seconds << " s)";
  }
  std::cout << ", engines, sampled pass, resume and HU10 full order PASS, "
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
    test_entry_normalization(resources);
    test_class_row_lifting(resources);
    test_orbit_aggregation(resources);
    test_partial_pass_and_resume(resources, scratch_dir);
    test_sampled_matches_trainer(resources, scratch_dir);
    const auto support = support_flops(resources);
    const auto history_maps = build_history_maps(resources, support);
    test_history_river_cursor(resources, history_maps);
    test_river_board_rows(resources, history_maps, support);
    test_joint_traversal(resources);
    test_joint_engine_matches_reference(resources, history_maps, support);
    test_street_restricted_response(resources, scratch_dir);
    std::cout << "PREFLOP_BLUEPRINT_CERTIFIER_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_CERTIFIER_TESTS=FAIL " << error.what() << '\n';
    return 1;
  }
}
