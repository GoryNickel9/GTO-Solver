#include "gtosd/gui_prototype/gui_prototype.hpp"

#include "gtosd/tree/config.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <intrin.h>
#include <winternl.h>
#include <wrl/client.h>
#endif

namespace gtosd {
namespace {

constexpr std::array<char, RangeMatrixModel::rank_count> rank_names{'A', 'K', 'Q', 'J', 'T',
                                                                    '9', '8', '7', '6'};
constexpr std::uint64_t virtual_branching_factor = 3U;

#ifdef _WIN32
std::vector<DWORD_PTR> physical_core_masks() {
  DWORD bytes = 0U;
  if (GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes) != FALSE ||
      GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0U) {
    return {};
  }
  std::vector<std::byte> buffer(bytes);
  if (GetLogicalProcessorInformationEx(
          RelationProcessorCore,
          reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),
          &bytes) == FALSE) {
    return {};
  }
  std::vector<DWORD_PTR> result;
  std::size_t offset = 0U;
  while (offset < bytes) {
    const auto *const entry =
        reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(buffer.data() + offset);
    if (entry->Size == 0U || offset + entry->Size > bytes) {
      return {};
    }
    if (entry->Relationship == RelationProcessorCore && entry->Processor.GroupCount != 0U &&
        entry->Processor.GroupMask[0].Group == 0U) {
      result.push_back(entry->Processor.GroupMask[0].Mask);
    }
    offset += entry->Size;
  }
  return result;
}
#endif

std::string matrix_class_name(const std::size_t row, const std::size_t column) {
  std::string result;
  result.push_back(rank_names[std::min(row, column)]);
  result.push_back(rank_names[std::max(row, column)]);
  if (row < column) {
    result.push_back('s');
  } else if (row > column) {
    result.push_back('o');
  }
  return result;
}

HandClassId find_hand_class(const std::string_view name) {
  for (std::uint8_t id = 0; id < RangeMatrixModel::cell_count; ++id) {
    if (class_name(id) == name) {
      return id;
    }
  }
  return std::numeric_limits<HandClassId>::max();
}

} // namespace

VirtualTreeFixture::VirtualTreeFixture(const std::uint64_t node_count) : node_count_(node_count) {}

Result<GuiDpiLayout, GuiPrototypeError> make_gui_dpi_layout(const std::uint32_t scale_percent) {
  if (scale_percent != 100U && scale_percent != 150U && scale_percent != 200U) {
    return Result<GuiDpiLayout, GuiPrototypeError>::failure(GuiPrototypeError::InvalidArgument);
  }
  return Result<GuiDpiLayout, GuiPrototypeError>::success(
      {scale_percent, 450U * scale_percent / 100U, 50U * scale_percent / 100U});
}

GuiTreeRow VirtualTreeFixture::row(const std::uint64_t index) const {
  if (index >= node_count_) {
    return {};
  }
  GuiTreeRow result;
  result.node = index;
  result.parent = index == 0U ? 0U : (index - 1U) / virtual_branching_factor;
  auto cursor = index;
  while (cursor != 0U) {
    cursor = (cursor - 1U) / virtual_branching_factor;
    ++result.depth;
  }
  result.has_children = node_count_ > 1U && index <= (node_count_ - 2U) / virtual_branching_factor;
  result.label = "Node " + std::to_string(index);
  return result;
}

std::vector<GuiTreeRow> VirtualTreeFixture::rows(const std::uint64_t offset,
                                                 const std::size_t count) const {
  std::vector<GuiTreeRow> result;
  if (offset >= node_count_ || count == 0U) {
    return result;
  }
  const auto available = node_count_ - offset;
  const auto requested = std::min<std::uint64_t>(available, count);
  result.reserve(static_cast<std::size_t>(requested));
  for (std::uint64_t index = 0; index < requested; ++index) {
    result.push_back(row(offset + index));
  }
  return result;
}

RangeMatrixModel::RangeMatrixModel() {
  for (std::size_t row = 0; row < rank_count; ++row) {
    for (std::size_t column = 0; column < rank_count; ++column) {
      const auto name = matrix_class_name(row, column);
      cells_[index(row, column)] = {find_hand_class(name), name, 10'000U};
    }
  }
}

const GuiMatrixCell &RangeMatrixModel::cell(const std::size_t row, const std::size_t column) const {
  return cells_.at(index(row, column));
}

bool RangeMatrixModel::set_weight(const std::size_t row, const std::size_t column,
                                  const std::uint16_t basis_points) {
  if (row >= rank_count || column >= rank_count || basis_points > 10'000U) {
    return false;
  }
  cells_[index(row, column)].weight_basis_points = basis_points;
  return true;
}

bool RangeMatrixModel::navigate(const GuiNavigationKey key) {
  switch (key) {
  case GuiNavigationKey::Left:
    focused_column_ = focused_column_ == 0U ? 0U : focused_column_ - 1U;
    break;
  case GuiNavigationKey::Right:
    focused_column_ = std::min(focused_column_ + 1U, rank_count - 1U);
    break;
  case GuiNavigationKey::Up:
    focused_row_ = focused_row_ == 0U ? 0U : focused_row_ - 1U;
    break;
  case GuiNavigationKey::Down:
    focused_row_ = std::min(focused_row_ + 1U, rank_count - 1U);
    break;
  case GuiNavigationKey::Home:
    focused_row_ = 0U;
    focused_column_ = 0U;
    break;
  case GuiNavigationKey::End:
    focused_row_ = rank_count - 1U;
    focused_column_ = rank_count - 1U;
    break;
  }
  return true;
}

std::vector<Combo> RangeMatrixModel::physical_combos(const std::size_t row,
                                                     const std::size_t column,
                                                     const std::vector<CardId> &board) const {
  if (row >= rank_count || column >= rank_count) {
    return {};
  }
  std::uint64_t blocked = 0U;
  for (const auto card : board) {
    blocked |= card.mask();
  }
  const auto target_class = cell(row, column).hand_class;
  std::vector<Combo> result;
  for (const auto combo : all_combos()) {
    if (hand_class(combo) == target_class &&
        ((combo.first.mask() | combo.second.mask()) & blocked) == 0U) {
      result.push_back(combo);
    }
  }
  return result;
}

std::size_t RangeMatrixModel::index(const std::size_t row, const std::size_t column) {
  if (row >= rank_count || column >= rank_count) {
    throw std::out_of_range("range matrix cell");
  }
  return row * rank_count + column;
}

Result<GuiSolutionOpenReport, GuiPrototypeError>
open_solution_for_gui(const std::filesystem::path &path, const StorageKey &key) {
  const auto reader = open_solution(path, key);
  if (!reader) {
    return Result<GuiSolutionOpenReport, GuiPrototypeError>::failure(
        GuiPrototypeError::StorageFailure);
  }
  const auto config_payload = read_solution_chunk(reader.value(), SolutionChunkType::Config);
  if (!config_payload) {
    return Result<GuiSolutionOpenReport, GuiPrototypeError>::failure(
        GuiPrototypeError::StorageFailure);
  }
  std::string config_text;
  config_text.resize(config_payload.value().size());
  std::transform(config_payload.value().begin(), config_payload.value().end(), config_text.begin(),
                 [](const std::byte value) { return static_cast<char>(value); });
  const auto config = parse_tree_config_json(config_text);
  if (!config) {
    return Result<GuiSolutionOpenReport, GuiPrototypeError>::failure(
        GuiPrototypeError::StorageFailure);
  }

  GuiSolutionOpenReport report;
  report.config = config.value();
  report.file_size = reader.value().metrics().file_size;
  report.total_logical_bytes = reader.value().metrics().raw_size;
  report.index_resident_bytes = reader.value().metrics().peak_open_bytes;
  report.payload_bytes_read = config_payload.value().size();
  report.total_chunks = reader.value().index().size();
  report.loaded_chunks = 1U;
  report.strategy_loaded = false;
  return Result<GuiSolutionOpenReport, GuiPrototypeError>::success(std::move(report));
}

std::vector<GuiWorkflowResult> run_gui_workflows(VirtualTreeFixture &tree,
                                                 RangeMatrixModel &matrix) {
  std::vector<GuiWorkflowResult> result;
  result.reserve(10U);
  result.push_back({"open_fixture", tree.node_count() >= 100'000U});
  result.push_back({"show_root", tree.row(0U).node == 0U});
  result.push_back({"expand_root", tree.row(1U).parent == 0U});
  result.push_back({"virtualize_10000_rows", tree.rows(45'000U, 10'000U).size() == 10'000U});
  result.push_back({"focus_matrix", matrix.cell(0U, 0U).name == "AA"});
  result.push_back({"edit_weight", matrix.set_weight(0U, 1U, 5'000U) &&
                                       matrix.cell(0U, 1U).weight_basis_points == 5'000U});
  result.push_back({"keyboard_navigation", matrix.navigate(GuiNavigationKey::End) &&
                                               matrix.focused_row() == 8U &&
                                               matrix.focused_column() == 8U});
  const auto ace_spades = CardId::from_parts(Rank::Ace, Suit::Spades);
  result.push_back({"expand_combos", matrix.physical_combos(0U, 0U, {ace_spades}).size() == 3U});
  result.push_back({"dpi_100_150_200", make_gui_dpi_layout(100U).has_value() &&
                                           make_gui_dpi_layout(150U).has_value() &&
                                           make_gui_dpi_layout(200U).has_value()});
  result.push_back(
      {"export_metrics", tree.row(tree.node_count() - 1U).node == tree.node_count() - 1U});
  return result;
}

Result<GuiRendererProbe, GuiPrototypeError> probe_gui_renderer(const bool force_warp) {
#ifdef _WIN32
  constexpr std::array<D3D_FEATURE_LEVEL, 4> requested_levels{
      D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0};
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  D3D_FEATURE_LEVEL selected = D3D_FEATURE_LEVEL_10_0;
  auto result =
      force_warp ? E_FAIL
                 : D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0U,
                                     requested_levels.data(),
                                     static_cast<UINT>(requested_levels.size()), D3D11_SDK_VERSION,
                                     device.GetAddressOf(), &selected, context.GetAddressOf());
  auto backend =
      force_warp ? GuiRenderBackend::Direct3D11Warp : GuiRenderBackend::Direct3D11Hardware;
  if (FAILED(result)) {
    device.Reset();
    context.Reset();
    result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0U, requested_levels.data(),
                               static_cast<UINT>(requested_levels.size()), D3D11_SDK_VERSION,
                               device.GetAddressOf(), &selected, context.GetAddressOf());
    backend = GuiRenderBackend::Direct3D11Warp;
  }
  if (FAILED(result)) {
    return Result<GuiRendererProbe, GuiPrototypeError>::failure(
        GuiPrototypeError::RendererUnavailable);
  }
  return Result<GuiRendererProbe, GuiPrototypeError>::success(
      {backend, static_cast<std::uint32_t>(selected)});
#else
  return Result<GuiRendererProbe, GuiPrototypeError>::failure(
      GuiPrototypeError::RendererUnavailable);
#endif
}

bool limit_gui_benchmark_affinity(const std::uint32_t maximum_processors) {
#ifdef _WIN32
  if (maximum_processors == 0U) {
    return false;
  }
  DWORD_PTR process_mask = 0U;
  DWORD_PTR system_mask = 0U;
  if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) == 0) {
    return false;
  }
  DWORD_PTR selected_mask = 0U;
  std::uint32_t selected = 0U;
  for (const auto core_mask : physical_core_masks()) {
    const auto eligible_mask = core_mask & process_mask & system_mask;
    for (std::size_t bit = 0U; bit < sizeof(DWORD_PTR) * 8U; ++bit) {
      const DWORD_PTR candidate = DWORD_PTR{1} << bit;
      if ((eligible_mask & candidate) != 0U) {
        selected_mask |= candidate;
        ++selected;
        break;
      }
    }
    if (selected == maximum_processors) {
      break;
    }
  }
  return selected != 0U && SetProcessAffinityMask(GetCurrentProcess(), selected_mask) != 0;
#else
  static_cast<void>(maximum_processors);
  return false;
#endif
}

GuiBenchmarkHost query_gui_benchmark_host() {
  GuiBenchmarkHost result;
#ifdef _WIN32
  SYSTEM_INFO system{};
  GetNativeSystemInfo(&system);
  result.physical_cores = static_cast<std::uint32_t>(physical_core_masks().size());
  result.logical_processors = system.dwNumberOfProcessors;

  MEMORYSTATUSEX memory{};
  memory.dwLength = sizeof(memory);
  if (GlobalMemoryStatusEx(&memory) != 0) {
    result.total_physical_memory_bytes = memory.ullTotalPhys;
  }

  std::array<int, 4> cpu_values{};
  __cpuid(cpu_values.data(), 0x80000000);
  const auto maximum_cpu_leaf = static_cast<unsigned int>(cpu_values[0]);
  if (maximum_cpu_leaf >= 0x80000004U) {
    std::array<char, 49> brand{};
    for (unsigned int leaf = 0; leaf < 3U; ++leaf) {
      __cpuid(cpu_values.data(), static_cast<int>(0x80000002U + leaf));
      std::memcpy(brand.data() + leaf * 16U, cpu_values.data(), 16U);
    }
    result.cpu_name = brand.data();
    const auto first = result.cpu_name.find_first_not_of(' ');
    const auto last = result.cpu_name.find_last_not_of(' ');
    result.cpu_name = first == std::string::npos ? std::string{}
                                                 : result.cpu_name.substr(first, last - first + 1U);
  }

  DWORD frequency = 0U;
  DWORD frequency_size = sizeof(frequency);
  if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                   L"~MHz", RRF_RT_REG_DWORD, nullptr, &frequency,
                   &frequency_size) == ERROR_SUCCESS) {
    result.cpu_nominal_mhz = frequency;
  }

  using RtlGetVersionFunction = LONG(WINAPI *)(PRTL_OSVERSIONINFOW);
  const auto module = GetModuleHandleW(L"ntdll.dll");
  const auto rtl_get_version =
      module == nullptr
          ? nullptr
          : reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(module, "RtlGetVersion"));
  RTL_OSVERSIONINFOW version{};
  version.dwOSVersionInfoSize = sizeof(version);
  if (rtl_get_version != nullptr && rtl_get_version(&version) == 0) {
    result.windows_major = version.dwMajorVersion;
    result.windows_minor = version.dwMinorVersion;
    result.windows_build = version.dwBuildNumber;
  }
#endif
  return result;
}

const char *gui_render_backend_name(const GuiRenderBackend backend) noexcept {
  switch (backend) {
  case GuiRenderBackend::Unavailable:
    return "unavailable";
  case GuiRenderBackend::Direct3D11Hardware:
    return "direct3d11_hardware";
  case GuiRenderBackend::Direct3D11Warp:
    return "direct3d11_warp";
  }
  return "unknown";
}

const char *gui_prototype_error_name(const GuiPrototypeError error) noexcept {
  switch (error) {
  case GuiPrototypeError::InvalidArgument:
    return "invalid_argument";
  case GuiPrototypeError::StorageFailure:
    return "storage_failure";
  case GuiPrototypeError::InvalidMatrixCell:
    return "invalid_matrix_cell";
  case GuiPrototypeError::RendererUnavailable:
    return "renderer_unavailable";
  }
  return "unknown";
}

} // namespace gtosd
