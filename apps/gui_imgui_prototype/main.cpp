#include "gtosd/gui_prototype/gui_prototype.hpp"

#include <d3d11.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <shellapi.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM word,
                                                             LPARAM long_value);

namespace {

using Microsoft::WRL::ComPtr;

struct Dx11State {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<IDXGISwapChain> swap_chain;
  ComPtr<ID3D11RenderTargetView> render_target;
  gtosd::GuiRenderBackend backend{gtosd::GuiRenderBackend::Unavailable};
};

Dx11State *active_dx11 = nullptr;

std::optional<std::wstring> environment_variable(const wchar_t *const name) {
  wchar_t *value = nullptr;
  std::size_t length = 0U;
  if (_wdupenv_s(&value, &length, name) != 0 || value == nullptr) {
    return std::nullopt;
  }
  std::wstring result(value, length == 0U ? 0U : length - 1U);
  std::free(value);
  return result;
}

std::optional<std::string> ascii_from_wide(const std::wstring &value) {
  std::string result;
  result.reserve(value.size());
  for (const auto character : value) {
    if (static_cast<unsigned int>(character) > 0x7fU) {
      return std::nullopt;
    }
    result.push_back(static_cast<char>(character));
  }
  return result;
}

void create_render_target(Dx11State &state) {
  ComPtr<ID3D11Texture2D> back_buffer;
  if (SUCCEEDED(state.swap_chain->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf())))) {
    state.device->CreateRenderTargetView(back_buffer.Get(), nullptr,
                                         state.render_target.GetAddressOf());
  }
}

bool create_device(HWND window, Dx11State &state) {
  DXGI_SWAP_CHAIN_DESC description{};
  description.BufferCount = 2U;
  description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  description.OutputWindow = window;
  description.SampleDesc.Count = 1U;
  description.Windowed = TRUE;
  description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  constexpr std::array<D3D_FEATURE_LEVEL, 3> levels{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                                    D3D_FEATURE_LEVEL_10_1};
  constexpr UINT device_flags =
      D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED;
  D3D_FEATURE_LEVEL selected{};
  const auto force_warp = environment_variable(L"GTOSD_FORCE_WARP").has_value();
  auto result = force_warp
                    ? E_FAIL
                    : D3D11CreateDeviceAndSwapChain(
                          nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, device_flags, levels.data(),
                          static_cast<UINT>(levels.size()), D3D11_SDK_VERSION, &description,
                          state.swap_chain.GetAddressOf(), state.device.GetAddressOf(), &selected,
                          state.context.GetAddressOf());
  state.backend = force_warp ? gtosd::GuiRenderBackend::Direct3D11Warp
                             : gtosd::GuiRenderBackend::Direct3D11Hardware;
  if (FAILED(result)) {
    state = {};
    result = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr, device_flags, levels.data(),
        static_cast<UINT>(levels.size()), D3D11_SDK_VERSION, &description,
        state.swap_chain.GetAddressOf(), state.device.GetAddressOf(), &selected,
        state.context.GetAddressOf());
    state.backend = gtosd::GuiRenderBackend::Direct3D11Warp;
  }
  if (FAILED(result)) {
    state = {};
    return false;
  }
  create_render_target(state);
  return state.render_target != nullptr;
}

LRESULT WINAPI window_proc(HWND window, const UINT message, const WPARAM word,
                           const LPARAM long_value) {
  if (ImGui::GetCurrentContext() != nullptr &&
      ImGui_ImplWin32_WndProcHandler(window, message, word, long_value)) {
    return 1;
  }
  switch (message) {
  case WM_SIZE:
    if (active_dx11 != nullptr && active_dx11->device != nullptr && word != SIZE_MINIMIZED) {
      active_dx11->render_target.Reset();
      active_dx11->swap_chain->ResizeBuffers(0U, static_cast<UINT>(LOWORD(long_value)),
                                             static_cast<UINT>(HIWORD(long_value)),
                                             DXGI_FORMAT_UNKNOWN, 0U);
      create_render_target(*active_dx11);
    }
    return 0;
  case WM_SYSCOMMAND:
    if ((word & 0xfff0U) == SC_KEYMENU) {
      return 0;
    }
    break;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    break;
  }
  return DefWindowProcW(window, message, word, long_value);
}

class ImGuiPrototype {
public:
  bool initialize(const bool visible) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    window_class_.cbSize = sizeof(window_class_);
    window_class_.style = CS_CLASSDC;
    window_class_.lpfnWndProc = window_proc;
    window_class_.hInstance = GetModuleHandleW(nullptr);
    window_class_.lpszClassName = L"GTOSD_ImGui_F9";
    if (RegisterClassExW(&window_class_) == 0U) {
      return false;
    }
    window_ = CreateWindowW(window_class_.lpszClassName, L"GTOSD - Dear ImGui F9 prototype",
                            WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr,
                            window_class_.hInstance, nullptr);
    if (window_ == nullptr || !create_device(window_, dx11_)) {
      shutdown();
      return false;
    }
    active_dx11 = &dx11_;
    if (visible) {
      ShowWindow(window_, SW_SHOWDEFAULT);
      UpdateWindow(window_);
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().AntiAliasedLines = false;
    ImGui::GetStyle().AntiAliasedFill = false;
    const auto requested_dpi = environment_variable(L"GTOSD_EXPECTED_DPI_PERCENT");
    if (requested_dpi.has_value()) {
      const auto percent = std::wcstoul(requested_dpi->c_str(), nullptr, 10);
      const auto layout = gtosd::make_gui_dpi_layout(static_cast<std::uint32_t>(percent));
      if (!layout) {
        shutdown();
        return false;
      }
      effective_dpi_scale_ = static_cast<float>(percent) / 100.0F;
    } else {
      effective_dpi_scale_ = static_cast<float>(GetDpiForWindow(window_)) / 96.0F;
    }
    ImGui::GetStyle().ScaleAllSizes(effective_dpi_scale_);
    io.FontGlobalScale = effective_dpi_scale_;
    if (!ImGui_ImplWin32_Init(window_) ||
        !ImGui_ImplDX11_Init(dx11_.device.Get(), dx11_.context.Get())) {
      shutdown();
      return false;
    }
    initialized_ = true;
    return true;
  }

  void shutdown() {
    if (initialized_) {
      ImGui_ImplDX11_Shutdown();
      ImGui_ImplWin32_Shutdown();
      ImGui::DestroyContext();
      initialized_ = false;
    }
    active_dx11 = nullptr;
    dx11_ = {};
    if (window_ != nullptr) {
      DestroyWindow(window_);
      window_ = nullptr;
    }
    if (window_class_.lpszClassName != nullptr && window_class_.hInstance != nullptr) {
      UnregisterClassW(window_class_.lpszClassName, window_class_.hInstance);
      window_class_ = {};
    }
  }

  ~ImGuiPrototype() { shutdown(); }

  void render_frame(const bool present) {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    draw_ui();
    ImGui::Render();
    constexpr float clear_color[4]{0.055F, 0.071F, 0.09F, 1.0F};
    dx11_.context->OMSetRenderTargets(1U, dx11_.render_target.GetAddressOf(), nullptr);
    dx11_.context->ClearRenderTargetView(dx11_.render_target.Get(), clear_color);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if (present) {
      dx11_.swap_chain->Present(0U, 0U);
    }
  }

  [[nodiscard]] bool synchronize_renderer() const {
    D3D11_QUERY_DESC description{};
    description.Query = D3D11_QUERY_EVENT;
    ComPtr<ID3D11Query> completion;
    if (FAILED(dx11_.device->CreateQuery(&description, completion.GetAddressOf()))) {
      return false;
    }
    dx11_.context->End(completion.Get());
    auto status = dx11_.context->GetData(completion.Get(), nullptr, 0U, 0U);
    while (status == S_FALSE) {
      SwitchToThread();
      status = dx11_.context->GetData(completion.Get(), nullptr, 0U, 0U);
    }
    return status == S_OK;
  }

  [[nodiscard]] bool pump_messages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
      if (message.message == WM_QUIT) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] gtosd::GuiRenderBackend backend() const noexcept { return dx11_.backend; }
  [[nodiscard]] float dpi_scale() const { return effective_dpi_scale_; }
  [[nodiscard]] bool open_solution(const std::filesystem::path &path,
                                   const gtosd::StorageKey &key) {
    const auto opened = gtosd::open_solution_for_gui(path, key);
    if (!opened) {
      return false;
    }
    open_report_ = opened.value();
    solution_name_ = path.filename().string();
    return !open_report_->strategy_loaded && open_report_->loaded_chunks == 1U;
  }

private:
  void draw_ui() {
    ImGui::DockSpaceOverViewport();

    ImGui::Begin("Action tree");
    ImGui::TextUnformatted("100,000 logical nodes - clipped on demand");
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(tree_.node_count()));
    while (clipper.Step()) {
      for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
        const auto value = tree_.row(static_cast<std::uint64_t>(row));
        ImGui::PushID(row);
        ImGui::Indent(static_cast<float>(std::min(value.depth, 8U)) * 6.0F);
        ImGui::Selectable(value.label.c_str(), selected_node_ == value.node);
        if (ImGui::IsItemClicked()) {
          selected_node_ = value.node;
        }
        ImGui::Unindent(static_cast<float>(std::min(value.depth, 8U)) * 6.0F);
        ImGui::PopID();
      }
    }
    ImGui::End();

    ImGui::Begin("Range");
    if (ImGui::BeginTable("short_deck_matrix", 9,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame)) {
      for (std::size_t row = 0; row < gtosd::RangeMatrixModel::rank_count; ++row) {
        ImGui::TableNextRow();
        for (std::size_t column = 0; column < gtosd::RangeMatrixModel::rank_count; ++column) {
          ImGui::TableSetColumnIndex(static_cast<int>(column));
          const auto &cell = matrix_.cell(row, column);
          ImGui::PushID(static_cast<int>(row * 9U + column));
          ImGui::Selectable(cell.name.c_str(),
                            row == matrix_.focused_row() && column == matrix_.focused_column(),
                            ImGuiSelectableFlags_None, ImVec2(0.0F, 32.0F));
          ImGui::PopID();
        }
      }
      ImGui::EndTable();
    }
    ImGui::End();

    ImGui::Begin("Node details");
    ImGui::Text("Node: %llu", static_cast<unsigned long long>(selected_node_));
    ImGui::TextUnformatted("Board: As Qd 7c");
    ImGui::TextUnformatted("CO to act - exact average strategy");
    ImGui::Text("Renderer: %s", gtosd::gui_render_backend_name(dx11_.backend));
    if (open_report_.has_value()) {
      ImGui::Separator();
      ImGui::Text("Solution: %s", solution_name_.c_str());
      ImGui::Text("Chunks: %llu total / %llu loaded",
                  static_cast<unsigned long long>(open_report_->total_chunks),
                  static_cast<unsigned long long>(open_report_->loaded_chunks));
      ImGui::Text("Index resident: %llu bytes",
                  static_cast<unsigned long long>(open_report_->index_resident_bytes));
      ImGui::TextUnformatted("Strategy: lazy");
    }
    ImGui::End();
  }

  WNDCLASSEXW window_class_{};
  HWND window_{nullptr};
  Dx11State dx11_;
  bool initialized_{false};
  float effective_dpi_scale_{1.0F};
  gtosd::VirtualTreeFixture tree_{100'000U};
  gtosd::RangeMatrixModel matrix_;
  gtosd::NodeId selected_node_{0U};
  std::optional<gtosd::GuiSolutionOpenReport> open_report_;
  std::string solution_name_;
};

bool write_text(const std::filesystem::path &path, const std::string &text) {
  std::error_code error;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
      return false;
    }
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << text;
  return output.good();
}

std::string json_escape(const std::string &text) {
  std::string result;
  for (const auto character : text) {
    if (character == '"' || character == '\\') {
      result.push_back('\\');
    }
    result.push_back(character);
  }
  return result;
}

int run_e2e(ImGuiPrototype &prototype, const std::filesystem::path &output_path) {
  gtosd::VirtualTreeFixture tree(100'000U);
  gtosd::RangeMatrixModel matrix;
  const auto workflows = gtosd::run_gui_workflows(tree, matrix);
  prototype.render_frame(false);
  const auto &io = ImGui::GetIO();
  const bool flags_valid = (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) != 0 &&
                           (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
  bool dpi_valid = prototype.dpi_scale() > 0.0F;
  const auto requested_dpi = environment_variable(L"GTOSD_EXPECTED_DPI_PERCENT");
  if (requested_dpi.has_value()) {
    const auto expected_scale =
        static_cast<float>(std::wcstoul(requested_dpi->c_str(), nullptr, 10)) / 100.0F;
    dpi_valid = dpi_valid && std::abs(prototype.dpi_scale() - expected_scale) <= 0.01F;
  }
  const auto config = gtosd::make_postflop_benchmark_config(gtosd::PostflopBenchmark::PfF1);
  gtosd::PostflopCheckpoint checkpoint;
  checkpoint.game_fingerprint = "phase9-imgui-e2e-v1";
  checkpoint.completed_iterations = 1U;
  checkpoint.action_count = 100'000U;
  checkpoint.cumulative_regret.assign(100'000U, 0.0);
  checkpoint.cumulative_strategy.assign(100'000U, 1.0);
  gtosd::PostflopCertification certification;
  certification.iteration = 1U;
  const auto archive =
      config ? gtosd::make_postflop_solution(config.value(), checkpoint, certification)
             : gtosd::Result<gtosd::SolutionArchive, gtosd::StorageError>::failure(
                   gtosd::StorageError::InvalidArgument);
  const auto key = gtosd::generate_storage_key();
  const auto solution_path =
      std::filesystem::temp_directory_path() /
      ("gtosd_phase9_imgui_e2e_" + std::to_string(GetCurrentProcessId()) + ".gtsd");
  std::error_code ignored;
  std::filesystem::remove(solution_path, ignored);
  const auto saved =
      archive
          ? gtosd::save_solution(solution_path, archive.value(), key)
          : gtosd::Result<bool, gtosd::StorageError>::failure(gtosd::StorageError::InvalidArgument);
  const auto lazy_solution_open = saved && prototype.open_solution(solution_path, key);
  prototype.render_frame(false);
  std::filesystem::remove(solution_path, ignored);

  bool all_passed = flags_valid && dpi_valid && lazy_solution_open;
  std::ostringstream json;
  json << "{\n  \"framework\": \"dear_imgui_docking\",\n"
       << "  \"renderer\": \"" << gtosd::gui_render_backend_name(prototype.backend())
       << "\",\n  \"workflow_count\": " << workflows.size()
       << ",\n  \"docking_enabled\": true,\n  \"keyboard_navigation\": true,\n"
       << "  \"dpi_scale\": " << prototype.dpi_scale()
       << ",\n  \"dpi_valid\": " << (dpi_valid ? "true" : "false") << ",\n"
       << "  \"lazy_solution_open\": " << (lazy_solution_open ? "true" : "false") << ",\n"
       << "  \"workflows\": [\n";
  for (std::size_t index = 0; index < workflows.size(); ++index) {
    all_passed = all_passed && workflows[index].passed;
    json << "    {\"name\": \"" << json_escape(workflows[index].name)
         << "\", \"passed\": " << (workflows[index].passed ? "true" : "false") << "}";
    json << (index + 1U == workflows.size() ? "\n" : ",\n");
  }
  json << "  ],\n  \"all_passed\": " << (all_passed ? "true" : "false") << "\n}\n";
  return write_text(output_path, json.str()) && all_passed ? 0 : 1;
}

int run_benchmark(ImGuiPrototype &prototype, const std::filesystem::path &output_path) {
  constexpr int warmup_frames = 30;
  constexpr int measured_frames = 300;
  const auto affinity_limited = gtosd::limit_gui_benchmark_affinity(4U);
  std::vector<double> timings;
  timings.reserve(measured_frames);
  for (int frame = -warmup_frames; frame < measured_frames; ++frame) {
    const auto start = std::chrono::steady_clock::now();
    prototype.render_frame(false);
    if (!prototype.synchronize_renderer()) {
      return 2;
    }
    const auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
    if (frame >= 0) {
      timings.push_back(elapsed.count());
    }
  }
  auto sorted = timings;
  std::sort(sorted.begin(), sorted.end());
  const auto mean =
      std::accumulate(timings.begin(), timings.end(), 0.0) / static_cast<double>(timings.size());
  const auto p95_index =
      static_cast<std::size_t>(std::floor(0.95 * static_cast<double>(sorted.size() - 1U)));
  const auto host = gtosd::query_gui_benchmark_host();
  std::ostringstream json;
  json << std::fixed << std::setprecision(6) << "{\n  \"framework\": \"dear_imgui_docking\",\n"
       << "  \"logical_nodes\": 100000,\n  \"matrix_cells\": 81,\n"
       << "  \"measured_frames\": " << measured_frames << ",\n"
       << "  \"mean_frame_ms\": " << mean << ",\n"
       << "  \"p95_frame_ms\": " << sorted[p95_index] << ",\n"
       << "  \"fps\": " << 1'000.0 / mean << ",\n"
       << "  \"dpi_scale\": " << prototype.dpi_scale() << ",\n"
       << "  \"renderer\": \"" << gtosd::gui_render_backend_name(prototype.backend())
       << "\",\n  \"processor_affinity_limit\": 4,\n"
       << "  \"affinity_limited\": " << (affinity_limited ? "true" : "false")
       << ",\n  \"software_fallback\": true,\n"
       << "  \"cpu_name\": \"" << json_escape(host.cpu_name) << "\",\n"
       << "  \"cpu_nominal_mhz\": " << host.cpu_nominal_mhz << ",\n"
       << "  \"physical_cores\": " << host.physical_cores << ",\n"
       << "  \"logical_processors\": " << host.logical_processors << ",\n"
       << "  \"total_physical_memory_bytes\": " << host.total_physical_memory_bytes << ",\n"
       << "  \"windows_build\": " << host.windows_build << "\n}\n";
  return write_text(output_path, json.str()) ? 0 : 1;
}

std::vector<std::wstring> command_line_arguments() {
  int count = 0;
  auto **const arguments = CommandLineToArgvW(GetCommandLineW(), &count);
  std::vector<std::wstring> result;
  if (arguments != nullptr) {
    result.assign(arguments, arguments + count);
    LocalFree(arguments);
  }
  return result;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  const auto arguments = command_line_arguments();
  const bool automated =
      arguments.size() == 3U && (arguments[1] == L"--e2e" || arguments[1] == L"--benchmark");
  ImGuiPrototype prototype;
  if (!prototype.initialize(!automated)) {
    return 2;
  }
  if (arguments.size() == 3U && arguments[1] == L"--e2e") {
    return run_e2e(prototype, std::filesystem::path(arguments[2]));
  }
  if (arguments.size() == 3U && arguments[1] == L"--benchmark") {
    return run_benchmark(prototype, std::filesystem::path(arguments[2]));
  }
  if (arguments.size() == 4U && arguments[1] == L"--solution") {
    const auto key_text = ascii_from_wide(arguments[3]);
    const auto key = key_text ? gtosd::storage_key_from_hex(*key_text)
                              : gtosd::Result<gtosd::StorageKey, gtosd::StorageError>::failure(
                                    gtosd::StorageError::InvalidArgument);
    if (!key || !prototype.open_solution(std::filesystem::path(arguments[2]), key.value())) {
      return 3;
    }
  }
  while (prototype.pump_messages()) {
    prototype.render_frame(true);
  }
  return 0;
}
