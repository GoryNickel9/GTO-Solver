#pragma once

#include "gtosd/gui_prototype/gui_prototype.hpp"

#include <QAbstractItemModel>
#include <QMainWindow>
#include <QWidget>

class QDockWidget;
class QLabel;
class QModelIndex;
class QTreeView;

namespace gtosd::qt_prototype {

class VirtualTreeModel final : public QAbstractItemModel {
public:
  explicit VirtualTreeModel(std::uint64_t node_count, QObject *parent = nullptr);

  [[nodiscard]] QModelIndex index(int row, int column,
                                  const QModelIndex &parent = {}) const override;
  [[nodiscard]] QModelIndex parent(const QModelIndex &child) const override;
  [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
  [[nodiscard]] int columnCount(const QModelIndex &parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
  [[nodiscard]] std::uint64_t logical_node_count() const noexcept;

private:
  [[nodiscard]] std::uint64_t node_from_index(const QModelIndex &index) const;

  VirtualTreeFixture fixture_;
};

class RangeMatrixWidget final : public QWidget {
public:
  explicit RangeMatrixWidget(QWidget *parent = nullptr);

  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] const RangeMatrixModel &model() const noexcept { return model_; }

protected:
  void paintEvent(QPaintEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;

private:
  void move_focus(GuiNavigationKey key);

  RangeMatrixModel model_;
};

class PrototypeWindow final : public QMainWindow {
public:
  explicit PrototypeWindow(QWidget *parent = nullptr);

  [[nodiscard]] VirtualTreeModel *tree_model() const noexcept { return tree_model_; }
  [[nodiscard]] QTreeView *tree_view() const noexcept { return tree_view_; }
  [[nodiscard]] RangeMatrixWidget *matrix_widget() const noexcept { return matrix_; }
  [[nodiscard]] std::vector<GuiWorkflowResult> run_automated_workflows();
  [[nodiscard]] bool open_solution(const std::filesystem::path &path, const StorageKey &key);

private:
  VirtualTreeModel *tree_model_{nullptr};
  QTreeView *tree_view_{nullptr};
  RangeMatrixWidget *matrix_{nullptr};
  QLabel *summary_{nullptr};
};

} // namespace gtosd::qt_prototype
