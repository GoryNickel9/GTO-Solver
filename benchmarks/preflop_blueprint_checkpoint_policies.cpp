// Read-only extraction from a historical checkpoint. Its reconstructed average
// must equal the supplied, fingerprinted reference policy at every cell.
#include "../libs/preflop_blueprint/src/binary_io.hpp"
#include "../libs/preflop_blueprint/src/hashing.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/policy_file.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace pb = gtosd::preflop_blueprint;
using Json = nlohmann::json;

std::string read(const char *path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot read input");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}

int main(int argc, char **argv) {
  try {
    require(argc == 7,
            "usage: CONFIG CHECKPOINT AVERAGE_POLICY REFERENCE_JSON CURRENT_OUT IDENTITY_OUT");
    auto config = pb::parse_game_config_json(read(argv[1]));
    require(config.has_value(), "invalid config");
    auto game = pb::CompiledGame::compile(config.value());
    require(game.has_value(), "invalid game");
    pb::PolicyFileInfo info;
    auto average = pb::load_policy(argv[3], game.value(), &info);
    require(average.has_value(), "reference policy invalid");
    const auto reference = Json::parse(read(argv[4]));
    require(reference.at("tree_fingerprint") == info.tree_fingerprint &&
                reference.at("policy_fingerprint") == info.policy_fingerprint &&
                reference.at("capacities") ==
                    Json(std::array<std::uint32_t, 3>{info.flop_capacity, info.turn_capacity,
                                                      info.river_capacity}),
            "reference identity mismatch");
    auto bytes = read(argv[2]);
    require(bytes.size() >= 32 && bytes.substr(0, 8) == "GTOSDCKP", "checkpoint magic");
    const auto payload_size = bytes.size() - 8;
    const auto checksum_bytes = bytes.substr(payload_size);
    pb::binary_io::Reader checksum_reader(checksum_bytes);
    std::uint64_t checksum = 0;
    require(checksum_reader.read_little(checksum) &&
                checksum == pb::detail::fnv1a_text(std::string_view(bytes.data(), payload_size)),
            "checkpoint checksum");
    bytes = bytes.substr(8, payload_size - 8);
    pb::binary_io::Reader body(bytes);
    std::uint32_t version = 0;
    std::uint64_t iteration = 0, boards = 0, word = 0, entries = 0;
    std::string identity;
    require(body.read_little32(version) && version == 2 && body.read_string(identity) &&
                body.read_little(iteration) && body.read_little(boards),
            "checkpoint header");
    // Training and evaluation RNG states each contain four words.
    for (int i = 0; i < 8; ++i)
      require(body.read_little(word), "checkpoint RNG");
    require(body.read_little(entries) && entries == average.value()->table().size(),
            "checkpoint layout");
    require(info.source.starts_with(identity + "|iteration=" + std::to_string(iteration)) &&
                (info.source.size() == identity.size() + 11 + std::to_string(iteration).size() ||
                 info.source[identity.size() + 11 + std::to_string(iteration).size()] == '|'),
            "checkpoint identity/iteration differs from reference policy");
    std::vector<double> regrets, sums;
    require(body.read_doubles(regrets, static_cast<std::size_t>(entries)) &&
                body.read_doubles(sums, static_cast<std::size_t>(entries)) && body.at_end(),
            "checkpoint payload");
    pb::BucketPolicy current(game.value(), average.value()->layout());
    std::size_t checked = 0;
    for (const auto &node : game.value().nodes()) {
      if (node.kind != pb::NodeKind::Decision)
        continue;
      const auto rows = pb::StateLayout::rows_for(node.street, info.flop_capacity,
                                                  info.turn_capacity, info.river_capacity);
      for (std::uint32_t row = 0; row < rows; ++row) {
        const auto offset =
            current.layout().offsets[node.id] + static_cast<std::uint64_t>(row) * node.action_count;
        double total = 0, positive = 0;
        for (std::uint8_t a = 0; a < node.action_count; ++a) {
          require(std::isfinite(regrets[offset + a]) && std::isfinite(sums[offset + a]) &&
                      sums[offset + a] >= 0,
                  "invalid checkpoint numeric value");
          total += sums[offset + a];
          positive += std::max(0.0, regrets[offset + a]);
        }
        require(std::isfinite(total) && std::isfinite(positive), "nonfinite row sum");
        for (std::uint8_t a = 0; a < node.action_count; ++a) {
          const double now = positive > 0 ? std::max(0.0, regrets[offset + a]) / positive
                                          : 1.0 / node.action_count;
          const double mean = total > 0 ? sums[offset + a] / total : now;
          require(mean == average.value()->table()[offset + a], "average reconstruction mismatch");
          current.table()[offset + a] = now;
          ++checked;
        }
      }
    }
    require(checked == entries, "not every checkpoint cell was validated");
    require(pb::save_policy(argv[5], game.value(), current,
                            identity + "|current|iteration=" + std::to_string(iteration))
                .has_value(),
            "current policy output failed");
    Json output{{"schema", "gtosd.research.policy_identity.v1"},
                {"tree_fingerprint", info.tree_fingerprint},
                {"policy_fingerprint", pb::policy_fingerprint(current)},
                {"average_policy_fingerprint", info.policy_fingerprint},
                {"trainer_identity", identity},
                {"iteration", iteration},
                {"boards_processed", boards},
                {"checked_cells", checked}};
    for (const auto key : {"capacities", "flop_table_fingerprint", "turn_table_fingerprint",
                           "river_table_fingerprint"})
      output[key] = reference.at(key);
    std::ofstream file(argv[6]);
    file << output.dump(2) << '\n';
    file.close();
    require(static_cast<bool>(file), "identity output failed");
    std::cout << output.dump(2) << "\nCHECKPOINT_POLICIES=PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "CHECKPOINT_POLICIES=FAIL " << error.what() << '\n';
    return 1;
  }
}
