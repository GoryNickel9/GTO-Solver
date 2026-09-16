#pragma once

#include "gtosd/preflop_blueprint/compiled_game.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Export of the public postflop tree in the layout of the chart viewer
// (schema gtosd.hu_postflop_public_tree.v1): one entry per postflop entry of
// the compiled game (preflop node id, preflop history, chance target), the
// postflop decision nodes with contiguous ids, their public state in antes
// and their actions with targets (decision, chance with the street dealt, or
// terminal with status), plus counts. The compiled node ids are kept in the
// export (compiledNode) so that the query worker can address nodes.
namespace gtosd::preflop_blueprint {

struct PostflopTreeStats {
  std::uint64_t represented_nodes{0U};
  std::uint64_t decision_nodes{0U};
  std::uint64_t action_edges{0U};
  std::uint64_t chance_frontiers{0U};
  std::uint64_t terminal_folds{0U};
  std::uint64_t terminal_showdowns{0U};
  std::uint64_t terminal_all_in_runouts{0U};
  std::uint32_t maximum_depth{0U};
  std::uint32_t maximum_raise_count{0U};
};

struct PostflopTreeExport {
  std::string json;
  PostflopTreeStats stats;
  // Export id of every postflop decision node (no_node for the others).
  std::vector<std::uint32_t> export_id;
};

[[nodiscard]] PostflopTreeExport export_postflop_tree(const CompiledGame &game);

} // namespace gtosd::preflop_blueprint
