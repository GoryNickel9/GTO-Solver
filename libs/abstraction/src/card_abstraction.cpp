#include "gtosd/abstraction/card_abstraction.hpp"

#include "gtosd/equity/evaluator.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace gtosd {
namespace {

constexpr std::uint64_t short_deck_mask = (std::uint64_t{1} << 36U) - 1U;
constexpr double distance_tolerance = 1.0e-15;

struct PartitionDefinition {
  std::uint8_t player{0};
  std::size_t dimensions{0};
  std::vector<std::size_t> observations;
};

void hash_bytes(std::uint64_t &hash, const std::string_view value) {
  constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
  for (const unsigned char byte : value) {
    hash ^= byte;
    hash *= fnv_prime;
  }
}

template <typename T> void hash_integer(std::uint64_t &hash, const T value) {
  const auto bytes = std::bit_cast<std::array<std::uint8_t, sizeof(T)>>(value);
  hash_bytes(hash, std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
}

void hash_string(std::uint64_t &hash, const std::string_view value) {
  hash_integer(hash, static_cast<std::uint64_t>(value.size()));
  hash_bytes(hash, value);
}

std::string hex_hash(const std::uint64_t hash) {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0') << std::setw(16) << hash;
  return stream.str();
}

double squared_distance(const std::vector<double> &left, const std::vector<double> &right) {
  double result = 0.0;
  for (std::size_t index = 0; index < left.size(); ++index) {
    const double delta = left[index] - right[index];
    result += delta * delta;
  }
  return result;
}

bool feature_less(const CardAbstractionObservation &left, const CardAbstractionObservation &right) {
  if (left.equity_features != right.equity_features) {
    return std::lexicographical_compare(left.equity_features.begin(), left.equity_features.end(),
                                        right.equity_features.begin(), right.equity_features.end());
  }
  return left.information_set < right.information_set;
}

Result<std::map<std::string, PartitionDefinition>, CardAbstractionError>
validate_observations(const std::vector<CardAbstractionObservation> &observations,
                      const CardAbstractionConfig &config) {
  if (config.major != CardAbstractionConfig::format_major ||
      config.minor > CardAbstractionConfig::format_minor) {
    return Result<std::map<std::string, PartitionDefinition>, CardAbstractionError>::failure(
        CardAbstractionError::UnsupportedVersion);
  }
  if (config.kind != CardAbstractionKind::ExactIdentity &&
      config.kind != CardAbstractionKind::EquityFeatureKMeans) {
    return Result<std::map<std::string, PartitionDefinition>, CardAbstractionError>::failure(
        CardAbstractionError::InvalidConfiguration);
  }
  if (observations.empty() || config.feature_schema_id.empty() || config.maximum_iterations == 0U ||
      (config.kind == CardAbstractionKind::EquityFeatureKMeans &&
       config.buckets_per_partition == 0U)) {
    return Result<std::map<std::string, PartitionDefinition>, CardAbstractionError>::failure(
        CardAbstractionError::InvalidConfiguration);
  }

  const auto combos = all_combos();
  std::set<std::string> information_sets;
  std::map<std::string, PartitionDefinition> partitions;
  for (std::size_t index = 0; index < observations.size(); ++index) {
    const auto &observation = observations[index];
    if (observation.information_set.empty() || observation.partition.empty() ||
        observation.player > 1U || observation.combo >= combos.size() ||
        (observation.public_card_mask & ~short_deck_mask) != 0U ||
        !std::isfinite(observation.reach_weight) || observation.reach_weight < 0.0 ||
        observation.equity_features.empty() ||
        !std::ranges::all_of(
            observation.equity_features, [](const double value) { return std::isfinite(value); })) {
      return Result<std::map<std::string, PartitionDefinition>, CardAbstractionError>::failure(
          CardAbstractionError::InvalidObservation);
    }
    const auto combo_mask =
        combos[observation.combo].first.mask() | combos[observation.combo].second.mask();
    if ((combo_mask & observation.public_card_mask) != 0U) {
      return Result<std::map<std::string, PartitionDefinition>, CardAbstractionError>::failure(
          CardAbstractionError::InvalidObservation);
    }
    if (!information_sets.insert(observation.information_set).second) {
      return Result<std::map<std::string, PartitionDefinition>, CardAbstractionError>::failure(
          CardAbstractionError::DuplicateInformationSet);
    }

    auto [partition, inserted] = partitions.try_emplace(
        observation.partition,
        PartitionDefinition{observation.player, observation.equity_features.size(), {}});
    if (!inserted && (partition->second.player != observation.player ||
                      partition->second.dimensions != observation.equity_features.size())) {
      return Result<std::map<std::string, PartitionDefinition>, CardAbstractionError>::failure(
          CardAbstractionError::IncompatiblePartition);
    }
    partition->second.observations.push_back(index);
  }
  return Result<std::map<std::string, PartitionDefinition>, CardAbstractionError>::success(
      std::move(partitions));
}

std::vector<std::vector<double>>
compute_centroids(const std::vector<CardAbstractionObservation> &observations,
                  const std::vector<std::size_t> &indices,
                  const std::vector<std::uint32_t> &assignments, const std::uint32_t count,
                  const std::size_t dimensions) {
  std::vector<std::vector<double>> centroids(count, std::vector<double>(dimensions, 0.0));
  std::vector<double> weights(count, 0.0);
  std::vector<std::uint32_t> members(count, 0U);
  for (std::size_t local = 0; local < indices.size(); ++local) {
    const auto &observation = observations[indices[local]];
    const auto bucket = assignments[local];
    const double weight = observation.reach_weight;
    weights[bucket] += weight;
    ++members[bucket];
    for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
      centroids[bucket][dimension] += weight * observation.equity_features[dimension];
    }
  }
  for (std::uint32_t bucket = 0; bucket < count; ++bucket) {
    if (weights[bucket] > 0.0) {
      for (double &value : centroids[bucket]) {
        value /= weights[bucket];
      }
      continue;
    }
    // Zero-reach states remain represented. They cannot influence weighted
    // centroids, so use their unweighted mean for deterministic assignment.
    if (members[bucket] == 0U) {
      continue;
    }
    for (std::size_t local = 0; local < indices.size(); ++local) {
      if (assignments[local] != bucket) {
        continue;
      }
      const auto &features = observations[indices[local]].equity_features;
      for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
        centroids[bucket][dimension] += features[dimension];
      }
    }
    for (double &value : centroids[bucket]) {
      value /= static_cast<double>(members[bucket]);
    }
  }
  return centroids;
}

std::string abstraction_fingerprint(const CardAbstractionConfig &config,
                                    const std::vector<CardAbstractionObservation> &observations,
                                    const std::vector<CardAbstractionAssignment> &assignments) {
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  hash_integer(hash, config.major);
  hash_integer(hash, config.minor);
  hash_integer(hash, static_cast<std::uint8_t>(config.kind));
  hash_integer(hash, config.buckets_per_partition);
  hash_integer(hash, config.maximum_iterations);
  hash_string(hash, config.feature_schema_id);
  std::vector<std::size_t> ordered(observations.size());
  for (std::size_t index = 0; index < ordered.size(); ++index) {
    ordered[index] = index;
  }
  std::ranges::sort(ordered, [&](const std::size_t left, const std::size_t right) {
    return observations[left].information_set < observations[right].information_set;
  });
  for (const std::size_t index : ordered) {
    const auto &observation = observations[index];
    const auto &assignment = assignments[index];
    hash_string(hash, observation.information_set);
    hash_string(hash, observation.partition);
    hash_integer(hash, observation.player);
    hash_integer(hash, observation.combo);
    hash_integer(hash, observation.public_card_mask);
    hash_integer(hash, std::bit_cast<std::uint64_t>(observation.reach_weight));
    for (const double feature : observation.equity_features) {
      hash_integer(hash, std::bit_cast<std::uint64_t>(feature));
    }
    hash_integer(hash, assignment.bucket);
    hash_string(hash, assignment.abstract_information_set);
  }
  return hex_hash(hash);
}

std::map<std::string, std::string> assignment_map(const CardAbstraction &abstraction) {
  std::map<std::string, std::string> result;
  for (const auto &assignment : abstraction.assignments) {
    result.emplace(assignment.information_set, assignment.abstract_information_set);
  }
  return result;
}

Result<CardAbstraction, CardAbstractionError>
validate_abstraction_object(const CardAbstraction &abstraction) {
  if (abstraction.assignments.empty()) {
    return Result<CardAbstraction, CardAbstractionError>::failure(
        CardAbstractionError::InvalidSerializedData);
  }
  std::vector<CardAbstractionObservation> observations;
  observations.reserve(abstraction.assignments.size());
  for (const auto &assignment : abstraction.assignments) {
    observations.push_back({assignment.information_set, assignment.partition, assignment.player,
                            assignment.combo, assignment.public_card_mask, assignment.reach_weight,
                            assignment.equity_features});
  }
  auto rebuilt = build_card_abstraction(observations, abstraction.config);
  if (!rebuilt || rebuilt.value() != abstraction) {
    return Result<CardAbstraction, CardAbstractionError>::failure(
        CardAbstractionError::InvalidSerializedData);
  }
  return rebuilt;
}

} // namespace

Result<std::vector<CardAbstractionObservation>, CardAbstractionError>
build_exact_postflop_equity_observations(const std::vector<CardId> &board,
                                         const PostflopRanges &ranges, const std::uint8_t player,
                                         const std::string &partition,
                                         const std::string &information_set_prefix) {
  if (player > 1U || board.size() < 3U || board.size() > 5U || partition.empty() ||
      information_set_prefix.empty()) {
    return Result<std::vector<CardAbstractionObservation>, CardAbstractionError>::failure(
        CardAbstractionError::InvalidConfiguration);
  }
  const auto board_mask_result = card_mask(board);
  if (!board_mask_result) {
    return Result<std::vector<CardAbstractionObservation>, CardAbstractionError>::failure(
        CardAbstractionError::InvalidObservation);
  }
  const std::uint64_t board_mask_value = board_mask_result.value();
  const auto combos = all_combos();
  const auto deck = short_deck();
  const std::uint8_t opponent = static_cast<std::uint8_t>(1U - player);
  const std::size_t missing_board_cards = 5U - board.size();
  std::vector<CardAbstractionObservation> observations;

  for (std::size_t hero_id = 0; hero_id < combos.size(); ++hero_id) {
    const auto hero_weight = ranges.players[player][hero_id].basis_points();
    const auto &hero = combos[hero_id];
    const std::uint64_t hero_mask = hero.first.mask() | hero.second.mask();
    if (hero_weight == 0U || (hero_mask & board_mask_value) != 0U) {
      continue;
    }
    double wins = 0.0;
    double ties = 0.0;
    double losses = 0.0;
    for (std::size_t opponent_id = 0; opponent_id < combos.size(); ++opponent_id) {
      const auto opponent_weight = ranges.players[opponent][opponent_id].basis_points();
      const auto &villain = combos[opponent_id];
      const std::uint64_t villain_mask = villain.first.mask() | villain.second.mask();
      if (opponent_weight == 0U || (villain_mask & (board_mask_value | hero_mask)) != 0U) {
        continue;
      }
      const std::uint64_t dead_mask = board_mask_value | hero_mask | villain_mask;
      std::vector<CardId> available;
      available.reserve(deck.size());
      for (const auto card : deck) {
        if ((card.mask() & dead_mask) == 0U) {
          available.push_back(card);
        }
      }
      std::vector<CardId> runout;
      runout.reserve(missing_board_cards);
      std::function<Result<bool, CardAbstractionError>(std::size_t)> enumerate =
          [&](const std::size_t begin) -> Result<bool, CardAbstractionError> {
        if (runout.size() == missing_board_cards) {
          std::array<CardId, 7> hero_cards{};
          std::array<CardId, 7> villain_cards{};
          hero_cards[0] = hero.first;
          hero_cards[1] = hero.second;
          villain_cards[0] = villain.first;
          villain_cards[1] = villain.second;
          for (std::size_t index = 0; index < board.size(); ++index) {
            hero_cards[index + 2U] = board[index];
            villain_cards[index + 2U] = board[index];
          }
          for (std::size_t index = 0; index < runout.size(); ++index) {
            hero_cards[board.size() + 2U + index] = runout[index];
            villain_cards[board.size() + 2U + index] = runout[index];
          }
          const auto hero_value = evaluate_seven(hero_cards);
          const auto villain_value = evaluate_seven(villain_cards);
          if (!hero_value || !villain_value) {
            return Result<bool, CardAbstractionError>::failure(
                CardAbstractionError::NumericalFailure);
          }
          const double weight = static_cast<double>(opponent_weight);
          if (hero_value.value() > villain_value.value()) {
            wins += weight;
          } else if (hero_value.value() == villain_value.value()) {
            ties += weight;
          } else {
            losses += weight;
          }
          return Result<bool, CardAbstractionError>::success(true);
        }
        const std::size_t required = missing_board_cards - runout.size();
        for (std::size_t index = begin; index + required <= available.size(); ++index) {
          runout.push_back(available[index]);
          const auto result = enumerate(index + 1U);
          runout.pop_back();
          if (!result) {
            return result;
          }
        }
        return Result<bool, CardAbstractionError>::success(true);
      };
      const auto enumerated = enumerate(0U);
      if (!enumerated) {
        return Result<std::vector<CardAbstractionObservation>, CardAbstractionError>::failure(
            enumerated.error());
      }
    }
    const double total = wins + ties + losses;
    if (total <= 0.0 || !std::isfinite(total)) {
      return Result<std::vector<CardAbstractionObservation>, CardAbstractionError>::failure(
          CardAbstractionError::InvalidObservation);
    }
    const double win_probability = wins / total;
    const double tie_probability = ties / total;
    const double loss_probability = losses / total;
    observations.push_back({information_set_prefix + ":combo=" + std::to_string(hero_id),
                            partition,
                            player,
                            static_cast<ComboId>(hero_id),
                            board_mask_value,
                            static_cast<double>(hero_weight) / 10'000.0,
                            {loss_probability, tie_probability, win_probability,
                             win_probability + 0.5 * tie_probability}});
  }
  if (observations.empty()) {
    return Result<std::vector<CardAbstractionObservation>, CardAbstractionError>::failure(
        CardAbstractionError::InvalidObservation);
  }
  return Result<std::vector<CardAbstractionObservation>, CardAbstractionError>::success(
      std::move(observations));
}

Result<CardAbstraction, CardAbstractionError>
build_card_abstraction(const std::vector<CardAbstractionObservation> &observations,
                       const CardAbstractionConfig &config) {
  const auto partitions = validate_observations(observations, config);
  if (!partitions) {
    return Result<CardAbstraction, CardAbstractionError>::failure(partitions.error());
  }

  CardAbstraction result;
  result.config = config;
  result.assignments.resize(observations.size());
  double weighted_squared_error = 0.0;
  double total_weight = 0.0;
  double unweighted_squared_error = 0.0;

  for (const auto &[partition_name, definition] : partitions.value()) {
    auto indices = definition.observations;
    std::ranges::sort(indices, [&](const std::size_t left, const std::size_t right) {
      return feature_less(observations[left], observations[right]);
    });
    const std::uint32_t bucket_count =
        config.kind == CardAbstractionKind::ExactIdentity
            ? static_cast<std::uint32_t>(indices.size())
            : std::min(config.buckets_per_partition, static_cast<std::uint32_t>(indices.size()));
    std::vector<std::uint32_t> local_assignments(indices.size(), 0U);
    for (std::size_t local = 0; local < indices.size(); ++local) {
      local_assignments[local] =
          config.kind == CardAbstractionKind::ExactIdentity
              ? static_cast<std::uint32_t>(local)
              : static_cast<std::uint32_t>(local * bucket_count / indices.size());
    }

    auto centroids = compute_centroids(observations, indices, local_assignments, bucket_count,
                                       definition.dimensions);
    if (config.kind == CardAbstractionKind::EquityFeatureKMeans) {
      for (std::uint32_t iteration = 0; iteration < config.maximum_iterations; ++iteration) {
        bool changed = false;
        for (std::size_t local = 0; local < indices.size(); ++local) {
          const auto previous = local_assignments[local];
          std::uint32_t selected = previous;
          double best =
              squared_distance(observations[indices[local]].equity_features, centroids[previous]);
          for (std::uint32_t bucket = 0; bucket < bucket_count; ++bucket) {
            const double candidate =
                squared_distance(observations[indices[local]].equity_features, centroids[bucket]);
            if (candidate + distance_tolerance < best) {
              best = candidate;
              selected = bucket;
            }
          }
          changed = changed || selected != previous;
          local_assignments[local] = selected;
        }
        centroids = compute_centroids(observations, indices, local_assignments, bucket_count,
                                      definition.dimensions);
        if (!changed) {
          break;
        }
      }
    }

    std::map<std::uint32_t, std::uint32_t> compact_ids;
    for (const auto bucket : local_assignments) {
      if (!compact_ids.contains(bucket)) {
        compact_ids.emplace(bucket, static_cast<std::uint32_t>(compact_ids.size()));
      }
    }
    const std::string partition_hash = [&]() {
      std::uint64_t hash = 14'695'981'039'346'656'037ULL;
      hash_string(hash, partition_name);
      hash_integer(hash, definition.player);
      return hex_hash(hash);
    }();

    std::map<std::uint32_t, std::size_t> bucket_positions;
    for (std::size_t local = 0; local < indices.size(); ++local) {
      const std::size_t observation_index = indices[local];
      const auto &observation = observations[observation_index];
      const std::uint32_t original_bucket = local_assignments[local];
      const std::uint32_t compact_bucket = compact_ids.at(original_bucket);
      const double squared =
          squared_distance(observation.equity_features, centroids[original_bucket]);
      const double distance = std::sqrt(squared);
      const std::string abstract_information_set =
          config.kind == CardAbstractionKind::ExactIdentity
              ? observation.information_set
              : "card_abs:v1:" + partition_hash + ":b" + std::to_string(compact_bucket);
      result.assignments[observation_index] = {observation.information_set,
                                               abstract_information_set,
                                               observation.partition,
                                               observation.player,
                                               observation.combo,
                                               observation.public_card_mask,
                                               compact_bucket,
                                               observation.reach_weight,
                                               distance,
                                               observation.equity_features};

      auto found = bucket_positions.find(compact_bucket);
      if (found == bucket_positions.end()) {
        const std::size_t position = result.buckets.size();
        bucket_positions.emplace(compact_bucket, position);
        result.buckets.push_back({observation.partition,
                                  observation.player,
                                  compact_bucket,
                                  0.0,
                                  centroids[original_bucket],
                                  {}});
        found = bucket_positions.find(compact_bucket);
      }
      auto &bucket = result.buckets[found->second];
      bucket.total_reach_weight += observation.reach_weight;
      bucket.members.push_back(observation.information_set);
      total_weight += observation.reach_weight;
      weighted_squared_error += observation.reach_weight * squared;
      unweighted_squared_error += squared;
      result.metrics.maximum_l2_error = std::max(result.metrics.maximum_l2_error, distance);
    }
  }

  result.metrics.exact_information_sets = observations.size();
  result.metrics.abstract_information_sets = result.buckets.size();
  result.metrics.compression_ratio =
      static_cast<double>(observations.size()) / static_cast<double>(result.buckets.size());
  result.metrics.weighted_mean_squared_error =
      total_weight > 0.0 ? weighted_squared_error / total_weight
                         : unweighted_squared_error / static_cast<double>(observations.size());
  result.metrics.uses_lossy_bucketing =
      std::ranges::any_of(result.buckets, [](const CardAbstractionBucket &bucket) {
        return bucket.members.size() > 1U;
      });
  if (!std::isfinite(result.metrics.weighted_mean_squared_error) ||
      !std::isfinite(result.metrics.maximum_l2_error)) {
    return Result<CardAbstraction, CardAbstractionError>::failure(
        CardAbstractionError::NumericalFailure);
  }
  result.fingerprint = abstraction_fingerprint(config, observations, result.assignments);
  return Result<CardAbstraction, CardAbstractionError>::success(std::move(result));
}

Result<FiniteGame, CardAbstractionError>
apply_card_abstraction(const FiniteGame &exact_game, const CardAbstraction &abstraction) {
  if (!validate_finite_game(exact_game)) {
    return Result<FiniteGame, CardAbstractionError>::failure(
        CardAbstractionError::IncompatibleGame);
  }
  const auto validated = validate_abstraction_object(abstraction);
  if (!validated) {
    return Result<FiniteGame, CardAbstractionError>::failure(validated.error());
  }
  const auto mapping = assignment_map(validated.value());
  std::set<std::string> used;
  FiniteGame abstract_game = exact_game;
  for (auto &node : abstract_game.nodes) {
    if (node.kind != GameNodeKind::Decision) {
      continue;
    }
    const auto found = mapping.find(node.information_set);
    if (found != mapping.end()) {
      used.insert(found->first);
      node.information_set = found->second;
    }
  }
  if (used.size() != mapping.size()) {
    return Result<FiniteGame, CardAbstractionError>::failure(
        CardAbstractionError::IncompatibleGame);
  }
  abstract_game.game_id += "|card_abstraction=" + abstraction.fingerprint;
  if (!validate_finite_game(abstract_game)) {
    return Result<FiniteGame, CardAbstractionError>::failure(
        CardAbstractionError::IncompatibleGame);
  }
  return Result<FiniteGame, CardAbstractionError>::success(std::move(abstract_game));
}

Result<StrategyProfile, CardAbstractionError>
lift_card_abstraction_strategy(const FiniteGame &exact_game, const CardAbstraction &abstraction,
                               const StrategyProfile &abstract_profile) {
  const auto exact_definitions = uniform_strategy_profile(exact_game);
  if (!exact_definitions) {
    return Result<StrategyProfile, CardAbstractionError>::failure(
        CardAbstractionError::IncompatibleGame);
  }
  const auto validated = validate_abstraction_object(abstraction);
  if (!validated) {
    return Result<StrategyProfile, CardAbstractionError>::failure(validated.error());
  }
  const auto mapping = assignment_map(validated.value());
  StrategyProfile lifted;
  for (const auto &[information_set, definition] : exact_definitions.value()) {
    const auto mapped = mapping.find(information_set);
    const std::string &source = mapped == mapping.end() ? information_set : mapped->second;
    const auto strategy = abstract_profile.find(source);
    if (strategy == abstract_profile.end() || strategy->second.player != definition.player ||
        strategy->second.actions != definition.actions) {
      return Result<StrategyProfile, CardAbstractionError>::failure(
          CardAbstractionError::InvalidStrategy);
    }
    lifted.emplace(information_set, strategy->second);
  }
  if (!validate_strategy_profile(exact_game, lifted)) {
    return Result<StrategyProfile, CardAbstractionError>::failure(
        CardAbstractionError::InvalidStrategy);
  }
  return Result<StrategyProfile, CardAbstractionError>::success(std::move(lifted));
}

Result<std::string, CardAbstractionError>
serialize_card_abstraction(const CardAbstraction &abstraction) {
  const auto validated = validate_abstraction_object(abstraction);
  if (!validated) {
    return Result<std::string, CardAbstractionError>::failure(validated.error());
  }
  const auto &canonical = validated.value();
  std::ostringstream stream;
  stream << "GTOSD_CARD_ABSTRACTION " << canonical.config.major << ' ' << canonical.config.minor
         << '\n';
  stream << "CONFIG " << static_cast<unsigned>(canonical.config.kind) << ' '
         << canonical.config.buckets_per_partition << ' ' << canonical.config.maximum_iterations
         << ' ' << std::quoted(canonical.config.feature_schema_id) << '\n';
  stream << "FINGERPRINT " << std::quoted(canonical.fingerprint) << '\n';
  stream << "OBSERVATIONS " << canonical.assignments.size() << '\n';
  for (const auto &assignment : canonical.assignments) {
    stream << "O " << std::quoted(assignment.information_set) << ' '
           << std::quoted(assignment.partition) << ' ' << static_cast<unsigned>(assignment.player)
           << ' ' << assignment.combo << ' ' << assignment.public_card_mask << ' '
           << std::bit_cast<std::uint64_t>(assignment.reach_weight) << ' '
           << assignment.equity_features.size();
    for (const double value : assignment.equity_features) {
      stream << ' ' << std::bit_cast<std::uint64_t>(value);
    }
    stream << '\n';
  }
  return Result<std::string, CardAbstractionError>::success(stream.str());
}

Result<CardAbstraction, CardAbstractionError>
deserialize_card_abstraction(const std::string &serialized) {
  std::istringstream stream(serialized);
  std::string token;
  CardAbstractionConfig config;
  if (!(stream >> token >> config.major >> config.minor) || token != "GTOSD_CARD_ABSTRACTION") {
    return Result<CardAbstraction, CardAbstractionError>::failure(
        CardAbstractionError::InvalidSerializedData);
  }
  if (config.major != CardAbstractionConfig::format_major ||
      config.minor > CardAbstractionConfig::format_minor) {
    return Result<CardAbstraction, CardAbstractionError>::failure(
        CardAbstractionError::UnsupportedVersion);
  }
  unsigned kind = 0U;
  if (!(stream >> token >> kind >> config.buckets_per_partition >> config.maximum_iterations >>
        std::quoted(config.feature_schema_id)) ||
      token != "CONFIG" || kind > static_cast<unsigned>(CardAbstractionKind::EquityFeatureKMeans)) {
    return Result<CardAbstraction, CardAbstractionError>::failure(
        CardAbstractionError::InvalidSerializedData);
  }
  config.kind = static_cast<CardAbstractionKind>(kind);
  std::string expected_fingerprint;
  if (!(stream >> token >> std::quoted(expected_fingerprint)) || token != "FINGERPRINT") {
    return Result<CardAbstraction, CardAbstractionError>::failure(
        CardAbstractionError::InvalidSerializedData);
  }
  std::size_t count = 0;
  if (!(stream >> token >> count) || token != "OBSERVATIONS" || count == 0U) {
    return Result<CardAbstraction, CardAbstractionError>::failure(
        CardAbstractionError::InvalidSerializedData);
  }
  std::vector<CardAbstractionObservation> observations;
  observations.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    CardAbstractionObservation observation;
    unsigned player = 0U;
    std::uint64_t weight_bits = 0U;
    std::size_t dimensions = 0U;
    if (!(stream >> token >> std::quoted(observation.information_set) >>
          std::quoted(observation.partition) >> player >> observation.combo >>
          observation.public_card_mask >> weight_bits >> dimensions) ||
        token != "O" || player > 1U || dimensions == 0U) {
      return Result<CardAbstraction, CardAbstractionError>::failure(
          CardAbstractionError::InvalidSerializedData);
    }
    observation.player = static_cast<std::uint8_t>(player);
    observation.reach_weight = std::bit_cast<double>(weight_bits);
    observation.equity_features.resize(dimensions);
    for (double &value : observation.equity_features) {
      std::uint64_t bits = 0U;
      if (!(stream >> bits)) {
        return Result<CardAbstraction, CardAbstractionError>::failure(
            CardAbstractionError::InvalidSerializedData);
      }
      value = std::bit_cast<double>(bits);
    }
    observations.push_back(std::move(observation));
  }
  if (stream >> token) {
    return Result<CardAbstraction, CardAbstractionError>::failure(
        CardAbstractionError::InvalidSerializedData);
  }
  auto rebuilt = build_card_abstraction(observations, config);
  if (!rebuilt || rebuilt.value().fingerprint != expected_fingerprint) {
    return Result<CardAbstraction, CardAbstractionError>::failure(
        CardAbstractionError::InvalidSerializedData);
  }
  return rebuilt;
}

const char *card_abstraction_kind_name(const CardAbstractionKind kind) noexcept {
  switch (kind) {
  case CardAbstractionKind::ExactIdentity:
    return "exact_identity";
  case CardAbstractionKind::EquityFeatureKMeans:
    return "equity_feature_kmeans";
  }
  return "unknown";
}

const char *card_abstraction_error_name(const CardAbstractionError error) noexcept {
  switch (error) {
  case CardAbstractionError::InvalidConfiguration:
    return "invalid_configuration";
  case CardAbstractionError::InvalidObservation:
    return "invalid_observation";
  case CardAbstractionError::DuplicateInformationSet:
    return "duplicate_information_set";
  case CardAbstractionError::IncompatiblePartition:
    return "incompatible_partition";
  case CardAbstractionError::IncompatibleGame:
    return "incompatible_game";
  case CardAbstractionError::InvalidStrategy:
    return "invalid_strategy";
  case CardAbstractionError::NumericalFailure:
    return "numerical_failure";
  case CardAbstractionError::InvalidSerializedData:
    return "invalid_serialized_data";
  case CardAbstractionError::UnsupportedVersion:
    return "unsupported_version";
  }
  return "unknown_card_abstraction_error";
}

} // namespace gtosd
