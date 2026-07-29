#pragma once

#include "gtosd/core/ranges.hpp"
#include "gtosd/storage/storage.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gtosd {

enum class GuiPrototypeError : std::uint8_t {
  InvalidArgument,
  StorageFailure,
  InvalidMatrixCell,
  RendererUnavailable
};

enum class GuiNavigationKey : std::uint8_t { Left, Right, Up, Down, Home, End };

enum class GuiRenderBackend : std::uint8_t { Unavailable, Direct3D11Hardware, Direct3D11Warp };

struct GuiDpiLayout {
  std::uint32_t scale_percent{100U};
  std::uint32_t matrix_extent_pixels{450U};
  std::uint32_t minimum_cell_pixels{50U};
};

[[nodiscard]] Result<GuiDpiLayout, GuiPrototypeError>
make_gui_dpi_layout(std::uint32_t scale_percent);

struct GuiTreeRow {
  NodeId node{0};
  NodeId parent{0};
  std::uint32_t depth{0};
  bool has_children{false};
  std::string label;
};

class VirtualTreeFixture {
public:
  explicit VirtualTreeFixture(std::uint64_t node_count = 100'000U);

  [[nodiscard]] std::uint64_t node_count() const noexcept { return node_count_; }
  [[nodiscard]] GuiTreeRow row(std::uint64_t index) const;
  [[nodiscard]] std::vector<GuiTreeRow> rows(std::uint64_t offset, std::size_t count) const;

private:
  std::uint64_t node_count_{0};
};

struct GuiMatrixCell {
  HandClassId hand_class{0};
  std::string name;
  std::uint16_t weight_basis_points{10'000};
};

class RangeMatrixModel {
public:
  static constexpr std::size_t rank_count = 9U;
  static constexpr std::size_t cell_count = rank_count * rank_count;

  RangeMatrixModel();

  [[nodiscard]] const GuiMatrixCell &cell(std::size_t row, std::size_t column) const;
  [[nodiscard]] std::size_t focused_row() const noexcept { return focused_row_; }
  [[nodiscard]] std::size_t focused_column() const noexcept { return focused_column_; }
  [[nodiscard]] bool set_weight(std::size_t row, std::size_t column, std::uint16_t basis_points);
  [[nodiscard]] bool navigate(GuiNavigationKey key);
  [[nodiscard]] std::vector<Combo> physical_combos(std::size_t row, std::size_t column,
                                                   const std::vector<CardId> &board) const;

private:
  [[nodiscard]] static std::size_t index(std::size_t row, std::size_t column);

  std::array<GuiMatrixCell, cell_count> cells_{};
  std::size_t focused_row_{0};
  std::size_t focused_column_{0};
};

struct GuiSolutionOpenReport {
  PostflopTreeConfig config;
  std::uint64_t file_size{0};
  std::uint64_t total_logical_bytes{0};
  std::uint64_t index_resident_bytes{0};
  std::uint64_t payload_bytes_read{0};
  std::size_t total_chunks{0};
  std::size_t loaded_chunks{0};
  bool strategy_loaded{false};
};

[[nodiscard]] Result<GuiSolutionOpenReport, GuiPrototypeError>
open_solution_for_gui(const std::filesystem::path &path, const StorageKey &key);

struct GuiWorkflowResult {
  std::string name;
  bool passed{false};
};

[[nodiscard]] std::vector<GuiWorkflowResult> run_gui_workflows(VirtualTreeFixture &tree,
                                                               RangeMatrixModel &matrix);

struct GuiRendererProbe {
  GuiRenderBackend backend{GuiRenderBackend::Unavailable};
  std::uint32_t feature_level{0};
};

struct GuiBenchmarkHost {
  std::string cpu_name;
  std::uint32_t cpu_nominal_mhz{0};
  std::uint32_t physical_cores{0};
  std::uint32_t logical_processors{0};
  std::uint64_t total_physical_memory_bytes{0};
  std::uint32_t windows_major{0};
  std::uint32_t windows_minor{0};
  std::uint32_t windows_build{0};
};

[[nodiscard]] Result<GuiRendererProbe, GuiPrototypeError>
probe_gui_renderer(bool force_warp = false);
[[nodiscard]] bool limit_gui_benchmark_affinity(std::uint32_t maximum_processors);
[[nodiscard]] GuiBenchmarkHost query_gui_benchmark_host();
[[nodiscard]] const char *gui_render_backend_name(GuiRenderBackend backend) noexcept;
[[nodiscard]] const char *gui_prototype_error_name(GuiPrototypeError error) noexcept;

} // namespace gtosd
