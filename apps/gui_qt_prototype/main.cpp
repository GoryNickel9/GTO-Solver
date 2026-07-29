#include "prototype_window.hpp"

#include "gtosd/gui_prototype/gui_prototype.hpp"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace {

bool verify_lazy_solution_open(gtosd::qt_prototype::PrototypeWindow &window) {
  const auto config = gtosd::make_postflop_benchmark_config(gtosd::PostflopBenchmark::PfF1);
  if (!config) {
    return false;
  }
  gtosd::PostflopCheckpoint checkpoint;
  checkpoint.game_fingerprint = "phase9-qt-e2e-v1";
  checkpoint.completed_iterations = 1U;
  checkpoint.action_count = 100'000U;
  checkpoint.cumulative_regret.assign(100'000U, 0.0);
  checkpoint.cumulative_strategy.assign(100'000U, 1.0);
  gtosd::PostflopCertification certification;
  certification.iteration = 1U;
  const auto archive = gtosd::make_postflop_solution(config.value(), checkpoint, certification);
  if (!archive) {
    return false;
  }
  const auto key = gtosd::generate_storage_key();
  const auto path =
      std::filesystem::temp_directory_path() /
      ("gtosd_phase9_qt_e2e_" + std::to_string(QCoreApplication::applicationPid()) + ".gtsd");
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  const auto saved = gtosd::save_solution(path, archive.value(), key);
  const auto opened = saved && window.open_solution(path, key);
  std::filesystem::remove(path, ignored);
  return opened;
}

bool write_json(const QString &path, const QJsonObject &object) {
  const QFileInfo file_info(path);
  if (!file_info.absoluteDir().mkpath(QStringLiteral("."))) {
    return false;
  }
  QFile output(path);
  if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  return output.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) >= 0;
}

QJsonArray workflow_json(const std::vector<gtosd::GuiWorkflowResult> &workflows) {
  QJsonArray result;
  for (const auto &workflow : workflows) {
    result.push_back(QJsonObject{{QStringLiteral("name"), QString::fromStdString(workflow.name)},
                                 {QStringLiteral("passed"), workflow.passed}});
  }
  return result;
}

int run_e2e(gtosd::qt_prototype::PrototypeWindow &window, const QString &output_path) {
  const auto workflows = window.run_automated_workflows();
  const auto lazy_solution_open = verify_lazy_solution_open(window);
  const auto all_passed =
      lazy_solution_open &&
      std::all_of(workflows.begin(), workflows.end(), [](const auto &item) { return item.passed; });
  const QJsonObject report{
      {QStringLiteral("framework"), QStringLiteral("qt6_widgets")},
      {QStringLiteral("workflow_count"), static_cast<qint64>(workflows.size())},
      {QStringLiteral("all_passed"), all_passed},
      {QStringLiteral("lazy_solution_open"), lazy_solution_open},
      {QStringLiteral("workflows"), workflow_json(workflows)}};
  return write_json(output_path, report) && all_passed ? 0 : 1;
}

int run_benchmark(QApplication &application, gtosd::qt_prototype::PrototypeWindow &window,
                  const QString &output_path) {
  constexpr int warmup_frames = 30;
  constexpr int measured_frames = 300;
  const auto affinity_limited = gtosd::limit_gui_benchmark_affinity(4U);
  QImage frame(1280, 800, QImage::Format_ARGB32_Premultiplied);
  frame.setDevicePixelRatio(1.0);
  std::vector<double> frame_times;
  frame_times.reserve(measured_frames);
  window.show();
  application.processEvents();
  for (int frame_index = -warmup_frames; frame_index < measured_frames; ++frame_index) {
    QElapsedTimer timer;
    timer.start();
    frame.fill(Qt::transparent);
    QPainter painter(&frame);
    window.render(&painter);
    painter.end();
    application.processEvents(QEventLoop::AllEvents, 5);
    if (frame_index >= 0) {
      frame_times.push_back(static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0);
    }
  }
  auto sorted = frame_times;
  std::sort(sorted.begin(), sorted.end());
  const auto total_ms = std::accumulate(frame_times.begin(), frame_times.end(), 0.0);
  const auto mean_ms = total_ms / static_cast<double>(frame_times.size());
  const auto p95_index =
      static_cast<std::size_t>(std::floor(0.95 * static_cast<double>(sorted.size() - 1U)));
  const auto renderer = gtosd::probe_gui_renderer();
  const auto host = gtosd::query_gui_benchmark_host();
  const QJsonObject report{
      {QStringLiteral("framework"), QStringLiteral("qt6_widgets")},
      {QStringLiteral("logical_nodes"), 100'000},
      {QStringLiteral("matrix_cells"), 81},
      {QStringLiteral("measured_frames"), measured_frames},
      {QStringLiteral("mean_frame_ms"), mean_ms},
      {QStringLiteral("p95_frame_ms"), sorted[p95_index]},
      {QStringLiteral("fps"), 1'000.0 / mean_ms},
      {QStringLiteral("dpi_scale"), window.devicePixelRatioF()},
      {QStringLiteral("renderer_probe"),
       renderer ? QString::fromLatin1(gtosd::gui_render_backend_name(renderer.value().backend))
                : QStringLiteral("unavailable")},
      {QStringLiteral("processor_affinity_limit"), 4},
      {QStringLiteral("affinity_limited"), affinity_limited},
      {QStringLiteral("software_fallback"), true}};
  auto mutable_report = report;
  mutable_report.insert(QStringLiteral("cpu_name"), QString::fromStdString(host.cpu_name));
  mutable_report.insert(QStringLiteral("cpu_nominal_mhz"),
                        static_cast<qint64>(host.cpu_nominal_mhz));
  mutable_report.insert(QStringLiteral("physical_cores"), static_cast<qint64>(host.physical_cores));
  mutable_report.insert(QStringLiteral("logical_processors"),
                        static_cast<qint64>(host.logical_processors));
  mutable_report.insert(QStringLiteral("total_physical_memory_bytes"),
                        static_cast<qint64>(host.total_physical_memory_bytes));
  mutable_report.insert(QStringLiteral("windows_build"), static_cast<qint64>(host.windows_build));
  return write_json(output_path, mutable_report) ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
  QApplication application(argc, argv);
  application.setApplicationName(QStringLiteral("GTOSD Qt F9 prototype"));
  application.setOrganizationName(QStringLiteral("GTOSD"));
  gtosd::qt_prototype::PrototypeWindow window;
  const auto arguments = application.arguments();
  if (arguments.size() == 3 && arguments[1] == QStringLiteral("--e2e")) {
    return run_e2e(window, arguments[2]);
  }
  if (arguments.size() == 3 && arguments[1] == QStringLiteral("--benchmark")) {
    return run_benchmark(application, window, arguments[2]);
  }
  if (arguments.size() == 4 && arguments[1] == QStringLiteral("--solution")) {
    const auto key = gtosd::storage_key_from_hex(arguments[3].toStdString());
    if (!key ||
        !window.open_solution(std::filesystem::path(arguments[2].toStdWString()), key.value())) {
      return 2;
    }
  }
  window.show();
  return application.exec();
}
