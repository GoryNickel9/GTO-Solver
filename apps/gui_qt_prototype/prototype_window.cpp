#include "prototype_window.hpp"

#include <QAccessible>
#include <QApplication>
#include <QDockWidget>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QStatusBar>
#include <QTreeView>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <limits>

namespace gtosd::qt_prototype {
namespace {

constexpr std::uint64_t branching_factor = 3U;

QColor cell_color(const GuiMatrixCell &cell) {
  const auto fraction = static_cast<double>(cell.weight_basis_points) / 10'000.0;
  return QColor::fromRgbF(0.12 + 0.18 * (1.0 - fraction), 0.24 + 0.52 * fraction,
                          0.34 + 0.22 * fraction);
}

} // namespace

VirtualTreeModel::VirtualTreeModel(const std::uint64_t node_count, QObject *const parent)
    : QAbstractItemModel(parent), fixture_(node_count) {}

QModelIndex VirtualTreeModel::index(const int row, const int column,
                                    const QModelIndex &parent_index) const {
  if (row < 0 || column != 0 || row >= rowCount(parent_index)) {
    return {};
  }
  std::uint64_t node = 0U;
  if (parent_index.isValid()) {
    const auto parent_node = node_from_index(parent_index);
    node = parent_node * branching_factor + 1U + static_cast<std::uint64_t>(row);
  }
  if (node >= fixture_.node_count()) {
    return {};
  }
  return createIndex(row, column, static_cast<quintptr>(node + 1U));
}

QModelIndex VirtualTreeModel::parent(const QModelIndex &child) const {
  if (!child.isValid()) {
    return {};
  }
  const auto node = node_from_index(child);
  if (node == 0U) {
    return {};
  }
  const auto parent_node = (node - 1U) / branching_factor;
  const auto parent_row =
      parent_node == 0U ? 0 : static_cast<int>((parent_node - 1U) % branching_factor);
  return createIndex(parent_row, 0, static_cast<quintptr>(parent_node + 1U));
}

int VirtualTreeModel::rowCount(const QModelIndex &parent_index) const {
  if (!parent_index.isValid()) {
    return fixture_.node_count() == 0U ? 0 : 1;
  }
  if (parent_index.column() != 0) {
    return 0;
  }
  const auto node = node_from_index(parent_index);
  const auto first_child = node * branching_factor + 1U;
  if (first_child >= fixture_.node_count()) {
    return 0;
  }
  return static_cast<int>(
      std::min<std::uint64_t>(branching_factor, fixture_.node_count() - first_child));
}

int VirtualTreeModel::columnCount(const QModelIndex &) const { return 1; }

QVariant VirtualTreeModel::data(const QModelIndex &model_index, const int role) const {
  if (!model_index.isValid()) {
    return {};
  }
  if (role == Qt::DisplayRole || role == Qt::AccessibleTextRole || role == Qt::ToolTipRole) {
    return QString::fromStdString(fixture_.row(node_from_index(model_index)).label);
  }
  return {};
}

std::uint64_t VirtualTreeModel::logical_node_count() const noexcept {
  return fixture_.node_count();
}

std::uint64_t VirtualTreeModel::node_from_index(const QModelIndex &index) const {
  return static_cast<std::uint64_t>(index.internalId() - 1U);
}

RangeMatrixWidget::RangeMatrixWidget(QWidget *const parent) : QWidget(parent) {
  setObjectName(QStringLiteral("rangeMatrix"));
  setAccessibleName(QStringLiteral("Short Deck range matrix"));
  setAccessibleDescription(
      QStringLiteral("Nine by nine hand class matrix with basis-point weights"));
  setFocusPolicy(Qt::StrongFocus);
  setMinimumSize(450, 450);
}

QSize RangeMatrixWidget::sizeHint() const { return {540, 540}; }

void RangeMatrixWidget::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, false);
  const auto cell_width = static_cast<double>(width()) / RangeMatrixModel::rank_count;
  const auto cell_height = static_cast<double>(height()) / RangeMatrixModel::rank_count;
  for (std::size_t row = 0; row < RangeMatrixModel::rank_count; ++row) {
    for (std::size_t column = 0; column < RangeMatrixModel::rank_count; ++column) {
      const QRectF rectangle(static_cast<double>(column) * cell_width,
                             static_cast<double>(row) * cell_height, cell_width, cell_height);
      const auto &cell = model_.cell(row, column);
      painter.fillRect(rectangle, cell_color(cell));
      painter.setPen(QColor(62, 76, 91));
      painter.drawRect(rectangle);
      painter.setPen(Qt::white);
      painter.drawText(rectangle, Qt::AlignCenter, QString::fromStdString(cell.name));
      if (hasFocus() && row == model_.focused_row() && column == model_.focused_column()) {
        QPen focus_pen(QColor(255, 194, 71));
        focus_pen.setWidth(3);
        painter.setPen(focus_pen);
        painter.drawRect(rectangle.adjusted(2.0, 2.0, -2.0, -2.0));
      }
    }
  }
}

void RangeMatrixWidget::keyPressEvent(QKeyEvent *const event) {
  switch (event->key()) {
  case Qt::Key_Left:
    move_focus(GuiNavigationKey::Left);
    return;
  case Qt::Key_Right:
    move_focus(GuiNavigationKey::Right);
    return;
  case Qt::Key_Up:
    move_focus(GuiNavigationKey::Up);
    return;
  case Qt::Key_Down:
    move_focus(GuiNavigationKey::Down);
    return;
  case Qt::Key_Home:
    move_focus(GuiNavigationKey::Home);
    return;
  case Qt::Key_End:
    move_focus(GuiNavigationKey::End);
    return;
  default:
    QWidget::keyPressEvent(event);
  }
}

void RangeMatrixWidget::mousePressEvent(QMouseEvent *const event) {
  const auto row = std::min<std::size_t>(
      static_cast<std::size_t>(event->position().y() * RangeMatrixModel::rank_count /
                               std::max(1, height())),
      RangeMatrixModel::rank_count - 1U);
  const auto column = std::min<std::size_t>(
      static_cast<std::size_t>(event->position().x() * RangeMatrixModel::rank_count /
                               std::max(1, width())),
      RangeMatrixModel::rank_count - 1U);
  static_cast<void>(model_.navigate(GuiNavigationKey::Home));
  for (std::size_t index = 0; index < row; ++index) {
    static_cast<void>(model_.navigate(GuiNavigationKey::Down));
  }
  for (std::size_t index = 0; index < column; ++index) {
    static_cast<void>(model_.navigate(GuiNavigationKey::Right));
  }
  setFocus(Qt::MouseFocusReason);
  update();
}

void RangeMatrixWidget::move_focus(const GuiNavigationKey key) {
  static_cast<void>(model_.navigate(key));
  update();
  QAccessibleEvent event(this, QAccessible::Focus);
  QAccessible::updateAccessibility(&event);
}

PrototypeWindow::PrototypeWindow(QWidget *const parent) : QMainWindow(parent) {
  setObjectName(QStringLiteral("prototypeWindow"));
  setWindowTitle(QStringLiteral("GTOSD — Qt 6 F9 prototype"));
  resize(1280, 800);
  setDockNestingEnabled(true);

  summary_ =
      new QLabel(QStringLiteral("PF-F1 · As Qd 7c · pot 10.0000 · stack 20.0000 · exact"), this);
  summary_->setAlignment(Qt::AlignCenter);
  summary_->setObjectName(QStringLiteral("solutionSummary"));
  setCentralWidget(summary_);

  auto *const tree_dock = new QDockWidget(QStringLiteral("Action tree"), this);
  tree_dock->setObjectName(QStringLiteral("treeDock"));
  tree_view_ = new QTreeView(tree_dock);
  tree_view_->setObjectName(QStringLiteral("actionTree"));
  tree_view_->setAccessibleName(QStringLiteral("Virtualized action tree"));
  tree_view_->setUniformRowHeights(true);
  tree_view_->setAnimated(false);
  tree_model_ = new VirtualTreeModel(100'000U, tree_view_);
  tree_view_->setModel(tree_model_);
  tree_view_->header()->hide();
  tree_view_->expand(tree_model_->index(0, 0));
  tree_dock->setWidget(tree_view_);
  addDockWidget(Qt::LeftDockWidgetArea, tree_dock);

  auto *const matrix_dock = new QDockWidget(QStringLiteral("Range"), this);
  matrix_dock->setObjectName(QStringLiteral("matrixDock"));
  matrix_ = new RangeMatrixWidget(matrix_dock);
  matrix_dock->setWidget(matrix_);
  addDockWidget(Qt::RightDockWidgetArea, matrix_dock);

  auto *const details_dock = new QDockWidget(QStringLiteral("Node details"), this);
  details_dock->setObjectName(QStringLiteral("detailsDock"));
  auto *const details = new QLabel(
      QStringLiteral("CO to act\nCheck 42.00%\nBet 50% 58.00%\nNashConv 0.741405%"), details_dock);
  details->setObjectName(QStringLiteral("nodeDetails"));
  details_dock->setWidget(details);
  addDockWidget(Qt::BottomDockWidgetArea, details_dock);

  const auto renderer = probe_gui_renderer();
  statusBar()->showMessage(
      renderer ? QStringLiteral("Renderer probe: %1 · Qt Widgets raster fallback available")
                     .arg(QString::fromLatin1(gui_render_backend_name(renderer.value().backend)))
               : QStringLiteral("Renderer probe unavailable · Qt Widgets raster fallback active"));
}

bool PrototypeWindow::open_solution(const std::filesystem::path &path, const StorageKey &key) {
  const auto opened = open_solution_for_gui(path, key);
  if (!opened) {
    statusBar()->showMessage(
        QStringLiteral("Solution open failed: %1")
            .arg(QString::fromLatin1(gui_prototype_error_name(opened.error()))));
    return false;
  }
  const auto &report = opened.value();
  summary_->setText(QStringLiteral("%1 · %2 chunks, %3 loaded · %4 index bytes · strategy lazy")
                        .arg(QString::fromStdString(path.filename().string()))
                        .arg(static_cast<qulonglong>(report.total_chunks))
                        .arg(static_cast<qulonglong>(report.loaded_chunks))
                        .arg(static_cast<qulonglong>(report.index_resident_bytes)));
  statusBar()->showMessage(QStringLiteral("Authenticated .gtsd root opened without strategy load"));
  return !report.strategy_loaded && report.loaded_chunks == 1U;
}

std::vector<GuiWorkflowResult> PrototypeWindow::run_automated_workflows() {
  VirtualTreeFixture tree(tree_model_->logical_node_count());
  RangeMatrixModel matrix;
  auto result = run_gui_workflows(tree, matrix);
  result[1].passed = result[1].passed && tree_view_->model()->rowCount() == 1;
  const auto root = tree_model_->index(0, 0);
  tree_view_->expand(root);
  result[2].passed = result[2].passed && tree_view_->isExpanded(root);
  matrix_->setFocus(Qt::OtherFocusReason);
  QKeyEvent end_event(QEvent::KeyPress, Qt::Key_End, Qt::NoModifier);
  QApplication::sendEvent(matrix_, &end_event);
  result[6].passed = result[6].passed && matrix_->model().focused_row() == 8U &&
                     matrix_->model().focused_column() == 8U;
  const auto *const matrix_accessible = QAccessible::queryAccessibleInterface(matrix_);
  const auto *const tree_accessible = QAccessible::queryAccessibleInterface(tree_view_);
  result[4].passed = result[4].passed && matrix_accessible != nullptr &&
                     tree_accessible != nullptr &&
                     !matrix_accessible->text(QAccessible::Name).isEmpty() &&
                     !tree_accessible->text(QAccessible::Name).isEmpty();
  show();
  QApplication::processEvents();
  const auto rendered = grab();
  bool scale_valid = devicePixelRatioF() > 0.0;
  bool expected_ok = false;
  const auto expected_dpi =
      qEnvironmentVariableIntValue("GTOSD_EXPECTED_DPI_PERCENT", &expected_ok);
  if (expected_ok) {
    const auto expected_scale = static_cast<double>(expected_dpi) / 100.0;
    scale_valid = scale_valid &&
                  make_gui_dpi_layout(static_cast<std::uint32_t>(expected_dpi)).has_value() &&
                  std::abs(devicePixelRatioF() - expected_scale) <= 0.01;
  }
  result[8].passed = result[8].passed && scale_valid && !rendered.isNull();
  result[9].passed = result[9].passed && findChildren<QDockWidget *>().size() == 3;
  return result;
}

} // namespace gtosd::qt_prototype
