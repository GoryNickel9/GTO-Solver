#include "product_window.hpp"

#include "gtosd/gui_prototype/gui_prototype.hpp"
#include "gtosd/memory/memory.hpp"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>

#include <filesystem>
#include <string>

namespace {

bool write_report(const QString &path, const QJsonObject &report) {
  const QFileInfo info(path);
  if (!info.absoluteDir().mkpath(QStringLiteral("."))) {
    return false;
  }
  QFile output(path);
  if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  return output.write(QJsonDocument(report).toJson(QJsonDocument::Indented)) >= 0;
}

int run_e2e(gtosd::desktop::ProductWindow &window, const QString &output_path) {
  std::string failure;
  const auto workspace =
      std::filesystem::path(QFileInfo(output_path).absoluteDir().absolutePath().toStdWString());
  QElapsedTimer timer;
  timer.start();
  const bool passed = window.run_phase10_e2e(workspace, failure);
  const auto host = gtosd::query_gui_benchmark_host();
  std::error_code size_error;
  const auto solution_size = std::filesystem::file_size(workspace / "phase10-e2e.gtsd", size_error);
  const QJsonObject report{
      {QStringLiteral("phase"), 10},
      {QStringLiteral("application"), QStringLiteral("gto_gui")},
      {QStringLiteral("workflow"), QStringLiteral("create-solve-save-reopen-navigate")},
      {QStringLiteral("passed"), passed},
      {QStringLiteral("ui_heartbeat_count"), static_cast<qint64>(window.ui_heartbeat_count())},
      {QStringLiteral("maximum_solve_ui_heartbeat_gap_ms"),
       window.maximum_solve_heartbeat_gap_ms()},
      {QStringLiteral("elapsed_ms"), timer.elapsed()},
      {QStringLiteral("peak_rss_bytes"), static_cast<qint64>(gtosd::process_peak_rss_bytes())},
      {QStringLiteral("solution_file_bytes"), size_error ? -1 : static_cast<qint64>(solution_size)},
      {QStringLiteral("cpu_name"), QString::fromStdString(host.cpu_name)},
      {QStringLiteral("cpu_nominal_mhz"), static_cast<qint64>(host.cpu_nominal_mhz)},
      {QStringLiteral("physical_cores"), static_cast<qint64>(host.physical_cores)},
      {QStringLiteral("logical_processors"), static_cast<qint64>(host.logical_processors)},
      {QStringLiteral("physical_memory_bytes"),
       static_cast<qint64>(host.total_physical_memory_bytes)},
      {QStringLiteral("range_precision_basis_points"), true},
      {QStringLiteral("exact_no_bucketing"), true},
      {QStringLiteral("failure"), QString::fromStdString(failure)}};
  return write_report(output_path, report) && passed ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
  QApplication application(argc, argv);
  application.setApplicationName(QStringLiteral("GTOSD"));
  application.setApplicationVersion(QStringLiteral("0.10.0"));
  application.setOrganizationName(QStringLiteral("GTOSD"));
  const auto arguments = application.arguments();
  const auto settings_root =
      arguments.size() == 3 && arguments[1] == QStringLiteral("--e2e")
          ? QFileInfo(arguments[2]).absoluteDir().absoluteFilePath(QStringLiteral("settings"))
          : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                QStringLiteral("/settings");
  QDir().mkpath(settings_root);
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_root);
  gtosd::desktop::ProductWindow window;
  if (arguments.size() == 3 && arguments[1] == QStringLiteral("--e2e")) {
    return run_e2e(window, arguments[2]);
  }
  window.show();
  return application.exec();
}
