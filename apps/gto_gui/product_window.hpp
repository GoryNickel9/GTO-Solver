#pragma once

#include "gtosd/postflop/postflop_solver.hpp"
#include "gtosd/storage/storage.hpp"
#include "gtosd/tree/tree.hpp"

#include <QAbstractItemModel>
#include <QMainWindow>
#include <QWidget>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QModelIndex;
class QObject;
class QProgressBar;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTreeView;

namespace gtosd::desktop {

struct DesktopProject {
  PostflopTreeConfig config;
  PostflopRanges ranges{make_uniform_postflop_ranges()};
  std::uint64_t iterations{2};
  std::uint64_t certification_interval{1};
  std::uint64_t ram_budget_bytes{8ULL * 1024ULL * 1024ULL * 1024ULL};
  std::uint64_t disk_budget_bytes{16ULL * 1024ULL * 1024ULL * 1024ULL};
  MemoryPrototype memory_backend{MemoryPrototype::LazyInRam};
};

class ConvergenceChart final : public QWidget {
public:
  explicit ConvergenceChart(QWidget *parent = nullptr);
  void set_points(std::vector<PostflopCertification> points);

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  std::vector<PostflopCertification> points_;
};

class PublicTreeModel final : public QAbstractItemModel {
public:
  explicit PublicTreeModel(QObject *parent = nullptr);

  void set_tree(std::shared_ptr<const PublicTree> tree);
  [[nodiscard]] QModelIndex index(int row, int column,
                                  const QModelIndex &parent = {}) const override;
  [[nodiscard]] QModelIndex parent(const QModelIndex &child) const override;
  [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
  [[nodiscard]] int columnCount(const QModelIndex &parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
  [[nodiscard]] NodeId node_id(const QModelIndex &index) const;

private:
  std::shared_ptr<const PublicTree> tree_;
  std::vector<NodeId> parents_;
};

struct SolveSession {
  mutable std::mutex mutex;
  std::optional<PostflopSolveResult> result;
  std::optional<PostflopCertification> latest;
  std::shared_ptr<PostflopCheckpoint> resume;
  std::string error;
  std::atomic<std::uint64_t> current_iteration{0};
  std::atomic<PostflopControlCommand> command{PostflopControlCommand::Continue};
  std::atomic<bool> done{false};
};

class ProductWindow final : public QMainWindow {
public:
  explicit ProductWindow(QWidget *parent = nullptr);
  ~ProductWindow() override;

  [[nodiscard]] bool load_config_text(const std::string &json);
  [[nodiscard]] bool estimate_current_project();
  [[nodiscard]] bool start_current_solve();
  [[nodiscard]] bool wait_for_solve(std::chrono::milliseconds timeout);
  [[nodiscard]] bool save_current_solution(const std::filesystem::path &path,
                                           const StorageKey &key);
  [[nodiscard]] bool open_saved_solution(const std::filesystem::path &path, const StorageKey &key);
  [[nodiscard]] bool browse(NodeId node, ComboId combo);
  [[nodiscard]] bool run_phase10_e2e(const std::filesystem::path &workspace, std::string &failure);
  [[nodiscard]] std::uint64_t ui_heartbeat_count() const noexcept { return ui_heartbeat_count_; }
  [[nodiscard]] double maximum_ui_heartbeat_gap_ms() const noexcept {
    return maximum_ui_heartbeat_gap_ms_;
  }
  [[nodiscard]] double maximum_solve_heartbeat_gap_ms() const noexcept {
    return maximum_solve_heartbeat_gap_ms_;
  }
  [[nodiscard]] const DesktopProject &project() const noexcept { return project_; }

protected:
  void closeEvent(QCloseEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  struct ScenarioWidgets {
    QLineEdit *sizes{nullptr};
    QSpinBox *raise_depth{nullptr};
    QComboBox *all_in_mode{nullptr};
    QDoubleSpinBox *all_in_threshold{nullptr};
    QDoubleSpinBox *minimum_bet{nullptr};
  };

  void build_ui();
  void load_default_project();
  void refresh_config_controls();
  [[nodiscard]] bool sync_visual_config();
  void refresh_range_matrix();
  void paint_range_cell(int row, int column, bool erase);
  void sync_project_settings();
  void poll_worker();
  void finish_worker();
  void show_builder();
  void show_browser();
  void update_browser_tree();
  void refresh_strategy_matrix();
  void invalidate_solution();
  void update_recent(const std::filesystem::path &path);
  void store_solution_key(const std::filesystem::path &path, const StorageKey &key);
  [[nodiscard]] std::optional<StorageKey>
  stored_solution_key(const std::filesystem::path &path) const;
  [[nodiscard]] std::optional<PostflopCheckpoint> snapshot_checkpoint() const;
  [[nodiscard]] std::optional<PostflopCertification> snapshot_certification() const;

  DesktopProject project_;
  StorageKey current_key_{};
  std::filesystem::path current_solution_path_;
  std::shared_ptr<SolveSession> session_;
  std::jthread worker_;
  std::shared_ptr<PublicTree> browser_tree_;
  std::optional<PostflopCheckpoint> checkpoint_;
  std::optional<PostflopCertification> certification_;

  QStackedWidget *pages_{nullptr};
  QListWidget *recent_{nullptr};
  QDoubleSpinBox *starting_pot_{nullptr};
  QDoubleSpinBox *effective_stack_{nullptr};
  QCheckBox *rake_enabled_{nullptr};
  QDoubleSpinBox *rake_percentage_{nullptr};
  QDoubleSpinBox *rake_cap_{nullptr};
  std::array<QComboBox *, 5> board_cards_{};
  QTableWidget *postflop_settings_{nullptr};
  std::array<std::array<std::array<ScenarioWidgets, 3>, 2>, 3> scenario_widgets_{};
  QComboBox *range_player_{nullptr};
  QTableWidget *range_matrix_{nullptr};
  QDoubleSpinBox *range_weight_{nullptr};
  bool range_painting_{false};
  bool range_erasing_{false};
  QSpinBox *iterations_{nullptr};
  QSpinBox *certification_interval_{nullptr};
  QDoubleSpinBox *ram_budget_gib_{nullptr};
  QDoubleSpinBox *disk_budget_gib_{nullptr};
  QComboBox *memory_backend_{nullptr};
  QLabel *estimate_summary_{nullptr};
  QLabel *solve_status_{nullptr};
  QLabel *solve_metrics_{nullptr};
  QProgressBar *solve_progress_{nullptr};
  QTableWidget *convergence_{nullptr};
  ConvergenceChart *convergence_chart_{nullptr};
  QTreeView *tree_view_{nullptr};
  PublicTreeModel *tree_model_{nullptr};
  QSpinBox *combo_selector_{nullptr};
  QComboBox *browser_action_{nullptr};
  QTableWidget *strategy_matrix_{nullptr};
  QTableWidget *strategy_table_{nullptr};
  QLabel *browser_summary_{nullptr};
  std::uint64_t ui_heartbeat_count_{0};
  std::chrono::steady_clock::time_point last_ui_heartbeat_{std::chrono::steady_clock::now()};
  double maximum_ui_heartbeat_gap_ms_{0.0};
  double maximum_solve_heartbeat_gap_ms_{0.0};
  std::vector<PostflopStrategyQuery> browser_queries_;
};

} // namespace gtosd::desktop
