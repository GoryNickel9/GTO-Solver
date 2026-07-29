#include "gtosd/gui_prototype/gui_prototype.hpp"

#include "gtosd/memory/memory.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::size_t assertions = 0;

void require(const bool condition, const std::string &message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(message);
  }
}

gtosd::PostflopCheckpoint make_checkpoint() {
  gtosd::PostflopCheckpoint checkpoint;
  checkpoint.game_fingerprint = "phase9-gui-fixture-v1";
  checkpoint.completed_iterations = 1U;
  checkpoint.action_count = 100'000U;
  checkpoint.cumulative_regret.assign(100'000U, 0.0);
  checkpoint.cumulative_strategy.assign(100'000U, 1.0);
  return checkpoint;
}

gtosd::PostflopCertification make_certification() {
  gtosd::PostflopCertification certification;
  certification.iteration = 1U;
  certification.normalized_nash_conv = 0.01;
  return certification;
}

void test_virtual_tree() {
  gtosd::VirtualTreeFixture tree(100'000U);
  require(tree.node_count() == 100'000U, "fixture exposes 100,000 logical nodes");
  const auto root = tree.row(0U);
  require(root.node == 0U && root.parent == 0U && root.depth == 0U && root.has_children,
          "root row is deterministic");
  const auto child = tree.row(4U);
  require(child.parent == 1U && child.depth == 2U, "ternary hierarchy is generated lazily");
  const auto page = tree.rows(90'000U, 10'000U);
  require(page.size() == 10'000U && page.front().node == 90'000U && page.back().node == 99'999U,
          "10,000-row window materializes without storing the full fixture");
  require(tree.rows(100'000U, 1U).empty(), "out-of-range page is empty");
  require(!gtosd::VirtualTreeFixture(1U).row(0U).has_children,
          "single-node fixture does not invent children");
}

void test_matrix_and_keyboard() {
  gtosd::RangeMatrixModel matrix;
  require(matrix.cell(0U, 0U).name == "AA" && matrix.cell(0U, 1U).name == "AKs" &&
              matrix.cell(1U, 0U).name == "AKo" && matrix.cell(8U, 8U).name == "66",
          "matrix uses the canonical Short Deck 9x9 layout");
  require(matrix.set_weight(0U, 1U, 4'250U) && matrix.cell(0U, 1U).weight_basis_points == 4'250U,
          "matrix weights retain basis-point precision");
  require(!matrix.set_weight(0U, 1U, 10'001U), "invalid matrix weight is rejected");
  require(matrix.navigate(gtosd::GuiNavigationKey::End) && matrix.focused_row() == 8U &&
              matrix.focused_column() == 8U,
          "End focuses the final cell");
  require(matrix.navigate(gtosd::GuiNavigationKey::Left) &&
              matrix.navigate(gtosd::GuiNavigationKey::Up) && matrix.focused_row() == 7U &&
              matrix.focused_column() == 7U,
          "arrow-key navigation is bounded and deterministic");
  require(matrix.navigate(gtosd::GuiNavigationKey::Home) && matrix.focused_row() == 0U &&
              matrix.focused_column() == 0U,
          "Home returns focus to AA");

  const auto ace_spades = gtosd::CardId::from_parts(gtosd::Rank::Ace, gtosd::Suit::Spades);
  require(matrix.physical_combos(0U, 0U, {}).size() == 6U, "AA expands to all six physical pairs");
  require(matrix.physical_combos(0U, 0U, {ace_spades}).size() == 3U,
          "board blocker disables incompatible physical combos");
  require(matrix.physical_combos(0U, 1U, {}).size() == 4U &&
              matrix.physical_combos(1U, 0U, {}).size() == 12U,
          "suited and offsuit cells preserve physical masses");
}

void test_dpi_contract() {
  const auto dpi_100 = gtosd::make_gui_dpi_layout(100U);
  const auto dpi_150 = gtosd::make_gui_dpi_layout(150U);
  const auto dpi_200 = gtosd::make_gui_dpi_layout(200U);
  require(dpi_100.has_value() && dpi_100.value().matrix_extent_pixels == 450U &&
              dpi_100.value().minimum_cell_pixels == 50U,
          "100 percent DPI layout preserves the baseline");
  require(dpi_150.has_value() && dpi_150.value().matrix_extent_pixels == 675U &&
              dpi_150.value().minimum_cell_pixels == 75U,
          "150 percent DPI layout scales exactly");
  require(dpi_200.has_value() && dpi_200.value().matrix_extent_pixels == 900U &&
              dpi_200.value().minimum_cell_pixels == 100U,
          "200 percent DPI layout scales exactly");
  require(!gtosd::make_gui_dpi_layout(125U), "unsupported prototype DPI is rejected explicitly");
}

void test_lazy_solution_open() {
  const auto config = gtosd::make_postflop_benchmark_config(gtosd::PostflopBenchmark::PfF1);
  require(config.has_value(), "PF-F1 config is available");
  const auto checkpoint = make_checkpoint();
  const auto archive =
      gtosd::make_postflop_solution(config.value(), checkpoint, make_certification());
  require(archive.has_value(), "GUI fixture archive builds");
  const auto key = gtosd::generate_storage_key();
  const auto path = std::filesystem::current_path() / "phase9_gui_fixture.gtsd";
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  require(gtosd::save_solution(path, archive.value(), key).has_value(),
          "GUI fixture saves as a real gtsd container");

  const auto opened = gtosd::open_solution_for_gui(path, key);
  require(opened.has_value(), "GUI opens the authenticated solution");
  require(opened.value().total_chunks == 8U && opened.value().loaded_chunks == 1U &&
              !opened.value().strategy_loaded,
          "opening reads config only and leaves strategy unloaded");
  require(opened.value().payload_bytes_read < opened.value().total_logical_bytes &&
              opened.value().index_resident_bytes < opened.value().total_logical_bytes,
          "root-open data is bounded below the full logical solution");
  require(opened.value().config.flop == config.value().flop,
          "lazy open retains the exact physical board");
  std::filesystem::remove(path, ignored);
}

void test_workflow_contract_and_renderer() {
  gtosd::VirtualTreeFixture tree(100'000U);
  gtosd::RangeMatrixModel matrix;
  const auto workflows = gtosd::run_gui_workflows(tree, matrix);
  require(workflows.size() == 10U, "exactly ten E2E workflows are automated");
  for (const auto &workflow : workflows) {
    require(workflow.passed, "workflow passes: " + workflow.name);
  }
  const auto renderer = gtosd::probe_gui_renderer();
  require(renderer.has_value(), "Direct3D 11 hardware or WARP renderer is available");
  require(renderer.value().backend == gtosd::GuiRenderBackend::Direct3D11Hardware ||
              renderer.value().backend == gtosd::GuiRenderBackend::Direct3D11Warp,
          "renderer uses an approved F9 backend");
  const auto warp = gtosd::probe_gui_renderer(true);
  require(warp.has_value() && warp.value().backend == gtosd::GuiRenderBackend::Direct3D11Warp,
          "WARP fallback is created and exercised explicitly");
  require(gtosd::limit_gui_benchmark_affinity(4U),
          "benchmark process can be constrained to four physical-core representatives");
  const auto host = gtosd::query_gui_benchmark_host();
  require(host.physical_cores >= 4U, "benchmark host has at least four physical cores");
  require(host.logical_processors >= 4U, "benchmark host has at least four logical processors");
  require(host.total_physical_memory_bytes >= 16ULL * 1024ULL * 1024ULL * 1024ULL,
          "benchmark host meets the 16 GB minimum-memory contract");
  require(!host.cpu_name.empty() && host.cpu_nominal_mhz != 0U,
          "benchmark host CPU is identified without WMI");
}

} // namespace

int main() {
  try {
    test_virtual_tree();
    test_matrix_and_keyboard();
    test_dpi_contract();
    test_lazy_solution_open();
    test_workflow_contract_and_renderer();
    std::cout << "phase9 assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "phase9 failure after assertions=" << assertions << ": " << error.what() << '\n';
    return 1;
  }
}
