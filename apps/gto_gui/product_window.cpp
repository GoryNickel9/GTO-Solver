#include "product_window.hpp"

#include "gtosd/gui_prototype/gui_prototype.hpp"
#include "gtosd/memory/memory.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <random>
#include <set>
#include <thread>

namespace gtosd::desktop {
namespace {

QString tr_text(const char *const text) { return QCoreApplication::translate("GtoDesktop", text); }

QString action_name(const ActionType type) {
  switch (type) {
  case ActionType::Fold:
    return tr_text("Fold");
  case ActionType::Check:
    return tr_text("Check");
  case ActionType::Call:
    return tr_text("Call");
  case ActionType::Bet:
    return tr_text("Bet");
  case ActionType::Raise:
    return tr_text("Raise");
  case ActionType::AllIn:
    return tr_text("All-in");
  }
  return tr_text("Unknown");
}

QString street_name(const Street street) {
  switch (street) {
  case Street::Preflop:
    return tr_text("Preflop");
  case Street::Flop:
    return tr_text("Flop");
  case Street::Turn:
    return tr_text("Turn");
  case Street::River:
    return tr_text("River");
  }
  return tr_text("Unknown");
}

QString node_kind_name(const PublicNodeKind kind) {
  switch (kind) {
  case PublicNodeKind::Decision:
    return tr_text("Decision");
  case PublicNodeKind::Chance:
    return tr_text("Chance");
  case PublicNodeKind::TerminalFold:
    return tr_text("Fold terminal");
  case PublicNodeKind::TerminalShowdown:
    return tr_text("Showdown");
  }
  return tr_text("Unknown");
}

std::optional<HandClassId> matrix_class(const int row, const int column) {
  constexpr std::string_view ranks = "AKQJT9876";
  std::string wanted;
  if (row == column) {
    wanted = {ranks[static_cast<std::size_t>(row)], ranks[static_cast<std::size_t>(row)]};
  } else if (row < column) {
    wanted = {ranks[static_cast<std::size_t>(row)], ranks[static_cast<std::size_t>(column)], 's'};
  } else {
    wanted = {ranks[static_cast<std::size_t>(column)], ranks[static_cast<std::size_t>(row)], 'o'};
  }
  for (std::uint8_t id = 0; id < 81U; ++id) {
    if (class_name(id) == wanted) {
      return id;
    }
  }
  return std::nullopt;
}

bool combo_blocked(const Combo &combo, const PostflopTreeConfig &config) {
  std::uint64_t board_mask = 0U;
  for (const auto card : configured_board(config)) {
    board_mask |= card.mask();
  }
  return ((combo.first.mask() | combo.second.mask()) & board_mask) != 0U;
}

QString combo_name(const Combo &combo) {
  return QString::fromStdString(format_card(combo.first) + format_card(combo.second));
}

std::filesystem::path recovery_checkpoint_path() {
  return std::filesystem::temp_directory_path() / "gtosd_phase10_recovery.gtsd";
}

QString log_directory_path() {
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
         QStringLiteral("/logs");
}

QString log_file_path() { return log_directory_path() + QStringLiteral("/gtosd.log"); }

void append_log(const QString &level, const QString &event, const QString &details = {}) {
  QDir().mkpath(log_directory_path());
  QFile file(log_file_path());
  if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
    return;
  }
  const auto clean = QString(details).replace('\n', QStringLiteral("\\n")).replace('"', '\'');
  const auto line =
      QStringLiteral("{\"time\":\"%1\",\"level\":\"%2\",\"event\":\"%3\",\"details\":\"%4\"}\n")
          .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), level, event, clean);
  static_cast<void>(file.write(line.toUtf8()));
}

QString solution_key_setting(const std::filesystem::path &path) {
  const auto normalized = QFileInfo(QString::fromStdWString(path.wstring())).absoluteFilePath();
  const auto digest = QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha256);
  return QStringLiteral("solutionKeys/") + QString::fromLatin1(digest.toHex());
}

} // namespace

ConvergenceChart::ConvergenceChart(QWidget *const parent) : QWidget(parent) {
  setObjectName(QStringLiteral("convergenceChart"));
  setAccessibleName(tr_text("Grafico EV e NashConv"));
  setMinimumHeight(180);
}

void ConvergenceChart::set_points(std::vector<PostflopCertification> points) {
  points_ = std::move(points);
  update();
}

void ConvergenceChart::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor(20, 27, 36));
  painter.setRenderHint(QPainter::Antialiasing);
  const QRectF plot = QRectF(rect()).adjusted(48.0, 14.0, -18.0, -30.0);
  painter.setPen(QColor(91, 105, 122));
  painter.drawRect(plot);
  if (points_.empty()) {
    painter.setPen(Qt::white);
    painter.drawText(plot, Qt::AlignCenter, tr_text("Nessuna metrica"));
    return;
  }
  double minimum = 0.0;
  double maximum = 0.0;
  for (const auto &point : points_) {
    minimum = std::min({minimum, point.profile_value_antes[0], point.profile_value_antes[1]});
    maximum = std::max({maximum, point.profile_value_antes[0], point.profile_value_antes[1],
                        point.normalized_nash_conv});
  }
  const double span = std::max(1e-12, maximum - minimum);
  const auto map_point = [&](const std::size_t index, const double value) {
    const double x = points_.size() == 1U
                         ? plot.center().x()
                         : plot.left() + plot.width() * static_cast<double>(index) /
                                             static_cast<double>(points_.size() - 1U);
    const double y = plot.bottom() - plot.height() * (value - minimum) / span;
    return QPointF(x, y);
  };
  const auto draw_series = [&](const QColor color, const auto value) {
    QPainterPath path;
    for (std::size_t index = 0; index < points_.size(); ++index) {
      const auto point = map_point(index, value(points_[index]));
      if (index == 0U) {
        path.moveTo(point);
      } else {
        path.lineTo(point);
      }
    }
    QPen pen(color);
    pen.setWidth(2);
    painter.setPen(pen);
    painter.drawPath(path);
  };
  draw_series(QColor(64, 176, 255), [](const auto &point) { return point.profile_value_antes[0]; });
  draw_series(QColor(255, 167, 71), [](const auto &point) { return point.profile_value_antes[1]; });
  draw_series(QColor(110, 219, 139), [](const auto &point) { return point.normalized_nash_conv; });
  painter.setPen(Qt::white);
  painter.drawText(QRectF(plot.left(), plot.bottom() + 5.0, plot.width(), 22.0), Qt::AlignCenter,
                   tr_text("Iterazioni · blu EV CO · arancio EV BTN · verde NashConv/Pot"));
}

PublicTreeModel::PublicTreeModel(QObject *const parent) : QAbstractItemModel(parent) {}

void PublicTreeModel::set_tree(std::shared_ptr<const PublicTree> tree) {
  beginResetModel();
  tree_ = std::move(tree);
  parents_.clear();
  if (tree_) {
    parents_.assign(tree_->nodes.size(), std::numeric_limits<NodeId>::max());
    for (const auto &node : tree_->nodes) {
      for (const auto &edge : node.edges) {
        if (edge.child < parents_.size()) {
          parents_[static_cast<std::size_t>(edge.child)] = node.id;
        }
      }
    }
  }
  endResetModel();
}

QModelIndex PublicTreeModel::index(const int row, const int column,
                                   const QModelIndex &parent_index) const {
  if (!tree_ || row < 0 || column < 0 || column >= columnCount(parent_index)) {
    return {};
  }
  NodeId node = tree_->root;
  if (parent_index.isValid()) {
    const auto parent_node = node_id(parent_index);
    const auto &edges = tree_->nodes[static_cast<std::size_t>(parent_node)].edges;
    if (static_cast<std::size_t>(row) >= edges.size()) {
      return {};
    }
    node = edges[static_cast<std::size_t>(row)].child;
  } else if (row != 0) {
    return {};
  }
  return createIndex(row, column, static_cast<quintptr>(node + 1U));
}

QModelIndex PublicTreeModel::parent(const QModelIndex &child) const {
  if (!tree_ || !child.isValid()) {
    return {};
  }
  const auto child_node = node_id(child);
  if (child_node == tree_->root || child_node >= parents_.size()) {
    return {};
  }
  const auto parent_node = parents_[static_cast<std::size_t>(child_node)];
  if (parent_node == tree_->root) {
    return createIndex(0, 0, static_cast<quintptr>(parent_node + 1U));
  }
  if (parent_node >= parents_.size()) {
    return {};
  }
  const auto grandparent = parents_[static_cast<std::size_t>(parent_node)];
  if (grandparent >= tree_->nodes.size()) {
    return {};
  }
  const auto &siblings = tree_->nodes[static_cast<std::size_t>(grandparent)].edges;
  const auto found =
      std::find_if(siblings.begin(), siblings.end(),
                   [parent_node](const auto &edge) { return edge.child == parent_node; });
  if (found == siblings.end()) {
    return {};
  }
  return createIndex(static_cast<int>(found - siblings.begin()), 0,
                     static_cast<quintptr>(parent_node + 1U));
}

int PublicTreeModel::rowCount(const QModelIndex &parent_index) const {
  if (!tree_) {
    return 0;
  }
  if (!parent_index.isValid()) {
    return 1;
  }
  if (parent_index.column() != 0) {
    return 0;
  }
  return static_cast<int>(
      tree_->nodes[static_cast<std::size_t>(node_id(parent_index))].edges.size());
}

int PublicTreeModel::columnCount(const QModelIndex &) const { return 3; }

QVariant PublicTreeModel::data(const QModelIndex &model_index, const int role) const {
  if (!tree_ || !model_index.isValid() ||
      (role != Qt::DisplayRole && role != Qt::AccessibleTextRole && role != Qt::ToolTipRole)) {
    return {};
  }
  const auto &node = tree_->nodes[static_cast<std::size_t>(node_id(model_index))];
  if (model_index.column() == 0) {
    return QStringLiteral("#%1 · %2")
        .arg(static_cast<qulonglong>(node.id))
        .arg(node_kind_name(node.kind));
  }
  if (model_index.column() == 1) {
    return street_name(node.state.street);
  }
  return tr_text("Pot %1 · %2 to act")
      .arg(QString::fromStdString(format_money(node.state.pot)))
      .arg(node.state.player_to_act == 0U ? QStringLiteral("CO") : QStringLiteral("BTN"));
}

NodeId PublicTreeModel::node_id(const QModelIndex &index) const {
  return static_cast<NodeId>(index.internalId() - 1U);
}

ProductWindow::ProductWindow(QWidget *const parent) : QMainWindow(parent) {
  setObjectName(QStringLiteral("gtoDesktopWindow"));
  setWindowTitle(QStringLiteral("GTOSD — Solver HU Short Deck"));
  resize(1440, 900);
  build_ui();
  load_default_project();
  append_log(QStringLiteral("info"), QStringLiteral("application_started"),
             QStringLiteral("version=0.10.0"));
  auto *const heartbeat = new QTimer(this);
  heartbeat->setInterval(10);
  connect(heartbeat, &QTimer::timeout, this, [this] {
    const auto now = std::chrono::steady_clock::now();
    maximum_ui_heartbeat_gap_ms_ =
        std::max(maximum_ui_heartbeat_gap_ms_,
                 std::chrono::duration<double, std::milli>(now - last_ui_heartbeat_).count());
    last_ui_heartbeat_ = now;
    ++ui_heartbeat_count_;
    poll_worker();
  });
  heartbeat->start();
}

ProductWindow::~ProductWindow() {
  if (session_ && !session_->done.load()) {
    session_->command.store(PostflopControlCommand::Cancel);
  }
  if (worker_.joinable()) {
    worker_.join();
  }
}

void ProductWindow::build_ui() {
  auto *const toolbar = addToolBar(tr_text("Navigazione"));
  toolbar->setObjectName(QStringLiteral("mainToolbar"));
  const auto add_page_action = [this, toolbar](const QString &name, const int page) {
    auto *const action = toolbar->addAction(name);
    connect(action, &QAction::triggered, this, [this, page] { pages_->setCurrentIndex(page); });
  };
  auto *const open_action = toolbar->addAction(tr_text("Apri…"));
  auto *const save_action = toolbar->addAction(tr_text("Salva con nome…"));
  connect(open_action, &QAction::triggered, this, [this] {
    const auto path = QFileDialog::getOpenFileName(this, tr_text("Apri soluzione"), {},
                                                   tr_text("Soluzione GTOSD (*.gtsd)"));
    if (path.isEmpty()) {
      return;
    }
    const auto solution_path = std::filesystem::path(path.toStdWString());
    auto key = stored_solution_key(solution_path);
    if (!key) {
      bool accepted = false;
      const auto key_text = QInputDialog::getText(
          this, tr_text("Importazione soluzione protetta"),
          tr_text("Questa soluzione non è stata salvata su questo PC. Inserisci la chiave "
                  "di esportazione a 64 caratteri."),
          QLineEdit::Normal, {}, &accepted);
      const auto parsed = storage_key_from_hex(key_text.toStdString());
      if (!accepted || !parsed) {
        return;
      }
      key = parsed.value();
    }
    if (!open_saved_solution(solution_path, *key)) {
      QMessageBox::critical(this, tr_text("Apertura fallita"),
                            tr_text("Chiave errata, file corrotto o versione incompatibile."));
    }
  });
  connect(save_action, &QAction::triggered, this, [this] {
    const auto path = QFileDialog::getSaveFileName(this, tr_text("Salva soluzione"), {},
                                                   tr_text("Soluzione GTOSD (*.gtsd)"));
    if (path.isEmpty()) {
      return;
    }
    if (std::all_of(current_key_.begin(), current_key_.end(),
                    [](const auto byte) { return byte == 0U; })) {
      current_key_ = generate_storage_key();
    }
    if (!save_current_solution(std::filesystem::path(path.toStdWString()), current_key_)) {
      QMessageBox::critical(this, tr_text("Salvataggio fallito"),
                            tr_text("La soluzione non è stata salvata."));
      return;
    }
    QMessageBox::information(
        this, tr_text("Soluzione salvata"),
        tr_text("La soluzione è stata salvata e protetta. La chiave locale viene gestita "
                "automaticamente dall'applicazione."));
  });
  auto *const logs_action = toolbar->addAction(tr_text("Log…"));
  connect(logs_action, &QAction::triggered, this, [this] {
    QDir().mkpath(log_directory_path());
    append_log(QStringLiteral("info"), QStringLiteral("log_folder_opened"));
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(log_directory_path()))) {
      QMessageBox::information(this, tr_text("Log diagnostici"), log_file_path());
    }
  });
  toolbar->addSeparator();
  pause_action_ = toolbar->addAction(tr_text("Pausa solve"));
  cancel_action_ = toolbar->addAction(tr_text("Annulla solve"));
  connect(pause_action_, &QAction::triggered, this, [this] {
    if (session_) {
      session_->command.store(PostflopControlCommand::Pause);
      solve_status_->setText(tr_text("Pausa richiesta…"));
    }
  });
  connect(cancel_action_, &QAction::triggered, this, [this] {
    if (session_) {
      session_->command.store(PostflopControlCommand::Cancel);
      solve_status_->setText(tr_text("Annullamento richiesto…"));
    }
  });
  pause_action_->setEnabled(false);
  cancel_action_->setEnabled(false);

  pages_ = new QStackedWidget(this);
  setCentralWidget(pages_);

  auto *const home = new QWidget(pages_);
  auto *const home_layout = new QVBoxLayout(home);
  auto *const title = new QLabel(tr_text("Solver exact HU Short Deck postflop"), home);
  QFont title_font = title->font();
  title_font.setPointSize(title_font.pointSize() + 6);
  title_font.setBold(true);
  title->setFont(title_font);
  home_layout->addWidget(title);
  auto *const new_button = new QPushButton(tr_text("Nuova soluzione"), home);
  new_button->setObjectName(QStringLiteral("newSolutionButton"));
  connect(new_button, &QPushButton::clicked, this, [this] { show_builder(); });
  home_layout->addWidget(new_button);
  recent_ = new QListWidget(home);
  recent_->setObjectName(QStringLiteral("recentSolutions"));
  recent_->setAccessibleName(tr_text("Soluzioni recenti"));
  home_layout->addWidget(new QLabel(tr_text("Recenti"), home));
  home_layout->addWidget(recent_, 1);
  pages_->addWidget(home);

  auto *const builder = new QWidget(pages_);
  auto *const builder_layout = new QVBoxLayout(builder);
  auto *const builder_tabs = new QTabWidget(builder);

  auto *const preflop_tab = new QWidget(builder_tabs);
  auto *const preflop_form = new QFormLayout(preflop_tab);
  starting_pot_ = new QDoubleSpinBox(preflop_tab);
  starting_pot_->setObjectName(QStringLiteral("startingPot"));
  starting_pot_->setRange(0.01, 1'000'000.0);
  starting_pot_->setDecimals(2);
  effective_stack_ = new QDoubleSpinBox(preflop_tab);
  effective_stack_->setObjectName(QStringLiteral("effectiveStack"));
  effective_stack_->setRange(0.01, 1'000'000.0);
  effective_stack_->setDecimals(2);
  rake_percentage_ = new QDoubleSpinBox(preflop_tab);
  rake_percentage_->setRange(0.0, 100.0);
  rake_percentage_->setDecimals(2);
  rake_percentage_->setSuffix(QStringLiteral("%"));
  rake_cap_ = new QDoubleSpinBox(preflop_tab);
  rake_cap_->setRange(0.0, 1'000'000.0);
  rake_cap_->setDecimals(2);
  preflop_form->addRow(tr_text("Starting pot"), starting_pot_);
  preflop_form->addRow(tr_text("Stack effettivo"), effective_stack_);
  preflop_form->addRow(tr_text("Rake"), rake_percentage_);
  preflop_form->addRow(tr_text("Rake cap"), rake_cap_);
  builder_tabs->addTab(preflop_tab, tr_text("Preflop setting"));

  auto *const postflop_tab = new QWidget(builder_tabs);
  auto *const postflop_layout = new QVBoxLayout(postflop_tab);
  postflop_layout->addWidget(
      new QLabel(tr_text("Le bet sono separate per posizione. Le size di raise si applicano "
                         "quando il giocatore affronta una puntata o un rilancio."),
                 postflop_tab));
  auto *const player_panels = new QHBoxLayout();
  constexpr std::array<const char *, 3> street_labels{"Flop", "Turn", "River"};
  constexpr std::array<const char *, 2> player_labels{"CO / OOP", "BTN / IP"};
  for (std::size_t player = 0; player < 2U; ++player) {
    auto *const panel = new QGroupBox(tr_text(player_labels[player]), postflop_tab);
    panel->setObjectName(QStringLiteral("bettingPanel%1").arg(static_cast<qulonglong>(player)));
    auto *const panel_layout = new QVBoxLayout(panel);
    auto *const defaults = new QFormLayout();
    auto &player_widgets = player_betting_[player];
    player_widgets.default_bet = new QDoubleSpinBox(panel);
    player_widgets.default_bet->setRange(0.01, 1000.0);
    player_widgets.default_bet->setDecimals(2);
    player_widgets.default_bet->setSuffix(QStringLiteral("%"));
    player_widgets.default_bet->setValue(50.0);
    player_widgets.all_in_policy = new QCheckBox(panel);
    player_widgets.all_in_policy->setTristate(true);
    player_widgets.all_in_threshold = new QDoubleSpinBox(panel);
    player_widgets.all_in_threshold->setRange(0.01, 1000.0);
    player_widgets.all_in_threshold->setDecimals(2);
    player_widgets.all_in_threshold->setSuffix(QStringLiteral("% pot"));
    player_widgets.all_in_threshold->setValue(150.0);
    const auto refresh_policy_text =
        [policy = player_widgets.all_in_policy,
         threshold = player_widgets.all_in_threshold](const Qt::CheckState state) {
          if (state == Qt::Unchecked) {
            policy->setText(tr_text("All-in automatico disabilitato"));
            threshold->setEnabled(false);
          } else if (state == Qt::PartiallyChecked) {
            policy->setText(tr_text("Add all-in if push <"));
            threshold->setEnabled(true);
          } else {
            policy->setText(tr_text("Go all-in if push <"));
            threshold->setEnabled(true);
          }
        };
    connect(player_widgets.all_in_policy, &QCheckBox::checkStateChanged, this, refresh_policy_text);
    refresh_policy_text(Qt::Unchecked);
    defaults->addRow(tr_text("Default bet"), player_widgets.default_bet);
    defaults->addRow(player_widgets.all_in_policy, player_widgets.all_in_threshold);
    panel_layout->addLayout(defaults);

    for (std::size_t street = 0; street < 3U; ++street) {
      auto &street_widgets = player_widgets.streets[street];
      auto *const street_box = new QGroupBox(tr_text(street_labels[street]), panel);
      auto *const street_form = new QFormLayout(street_box);
      street_widgets.custom = new QCheckBox(tr_text("Usa size personalizzate"), street_box);
      street_widgets.bet_sizes = new QLineEdit(street_box);
      street_widgets.bet_sizes->setPlaceholderText(QStringLiteral("25, 50, 100"));
      street_widgets.raise_sizes = new QLineEdit(street_box);
      street_widgets.raise_sizes->setPlaceholderText(QStringLiteral("50, 100"));
      street_widgets.maximum_raises = new QSpinBox(street_box);
      street_widgets.maximum_raises->setRange(0, 4);
      street_widgets.maximum_raises->setToolTip(
          tr_text("Numero massimo di raise e re-raise consentiti nella street."));
      street_form->addRow(street_widgets.custom);
      street_form->addRow(player == 0U ? tr_text("Bet OOP %") : tr_text("Bet dopo check %"),
                          street_widgets.bet_sizes);
      street_form->addRow(tr_text("Raise sizes %"), street_widgets.raise_sizes);
      street_form->addRow(tr_text("Max raise"), street_widgets.maximum_raises);
      const auto set_custom_enabled = [street_widgets](const bool enabled) {
        street_widgets.bet_sizes->setEnabled(enabled);
        street_widgets.raise_sizes->setEnabled(enabled);
        street_widgets.maximum_raises->setEnabled(enabled);
      };
      connect(street_widgets.custom, &QCheckBox::toggled, this, set_custom_enabled);
      set_custom_enabled(false);
      panel_layout->addWidget(street_box);
    }
    panel_layout->addStretch(1);
    player_panels->addWidget(panel, 1);
  }
  postflop_layout->addLayout(player_panels, 1);
  builder_tabs->addTab(postflop_tab, tr_text("Postflop setting"));

  auto *const board_tab = new QWidget(builder_tabs);
  auto *const board_layout = new QHBoxLayout(board_tab);
  auto *const card_grid = new QGridLayout();
  constexpr std::array<Rank, 9> board_ranks{Rank::Ace,   Rank::King,  Rank::Queen,
                                            Rank::Jack,  Rank::Ten,   Rank::Nine,
                                            Rank::Eight, Rank::Seven, Rank::Six};
  constexpr std::array<Suit, 4> board_suits{Suit::Hearts, Suit::Clubs, Suit::Diamonds,
                                            Suit::Spades};
  constexpr std::array<const char *, 4> suit_styles{
      "QToolButton{background:#f6b7b7;color:#7a2020;} QToolButton:checked{background:#d9534f;"
      "color:white;}",
      "QToolButton{background:#c8e6c9;color:#1b5e20;} QToolButton:checked{background:#43a047;"
      "color:white;}",
      "QToolButton{background:#bbdefb;color:#0d47a1;} QToolButton:checked{background:#1e88e5;"
      "color:white;}",
      "QToolButton{background:#e0e0e0;color:#263238;} QToolButton:checked{background:#616161;"
      "color:white;}"};
  for (std::size_t row = 0; row < board_ranks.size(); ++row) {
    for (std::size_t column = 0; column < board_suits.size(); ++column) {
      const auto card = CardId::from_parts(board_ranks[row], board_suits[column]);
      auto *const button = new QToolButton(board_tab);
      button->setText(QString::fromStdString(format_card(card)));
      button->setCheckable(true);
      button->setFixedSize(58, 42);
      button->setStyleSheet(QString::fromLatin1(suit_styles[column]));
      button->setObjectName(QStringLiteral("boardCard%1").arg(card.value()));
      board_buttons_[card.value()] = button;
      connect(button, &QToolButton::clicked, this, [this, card](const bool checked) {
        if (checked) {
          if (selected_board_.size() >= 5U) {
            board_buttons_[card.value()]->setChecked(false);
            statusBar()->showMessage(tr_text("Il board può contenere al massimo cinque carte."));
            return;
          }
          selected_board_.push_back(card);
        } else {
          std::erase(selected_board_, card);
        }
        refresh_board_picker();
      });
      card_grid->addWidget(button, static_cast<int>(row), static_cast<int>(column));
    }
  }
  board_layout->addLayout(card_grid);
  auto *const board_actions = new QVBoxLayout();
  board_selection_ = new QLabel(board_tab);
  board_selection_->setObjectName(QStringLiteral("boardSelection"));
  board_selection_->setWordWrap(true);
  auto *const clear_board = new QPushButton(tr_text("Clear"), board_tab);
  auto *const random_board = new QPushButton(tr_text("Random"), board_tab);
  auto *const done_board = new QPushButton(tr_text("Done"), board_tab);
  connect(clear_board, &QPushButton::clicked, this, [this] {
    selected_board_.clear();
    refresh_board_picker();
  });
  connect(random_board, &QPushButton::clicked, this, [this] {
    auto deck = short_deck();
    std::mt19937 engine(std::random_device{}());
    std::shuffle(deck.begin(), deck.end(), engine);
    const auto count = selected_board_.size() >= 3U ? selected_board_.size() : std::size_t{3};
    selected_board_.assign(deck.begin(), deck.begin() + static_cast<std::ptrdiff_t>(count));
    refresh_board_picker();
  });
  connect(done_board, &QPushButton::clicked, this, [this, builder_tabs] {
    if (sync_visual_config()) {
      builder_tabs->setCurrentIndex(3);
    }
  });
  board_actions->addWidget(board_selection_);
  board_actions->addWidget(clear_board);
  board_actions->addWidget(random_board);
  board_actions->addStretch(1);
  board_actions->addWidget(done_board);
  board_layout->addLayout(board_actions, 1);
  builder_tabs->addTab(board_tab, tr_text("Board"));

  auto *const range_tab = new QWidget(builder_tabs);
  auto *const range_layout = new QVBoxLayout(range_tab);
  range_player_ = new QComboBox(range_tab);
  range_player_->addItems({QStringLiteral("CO"), QStringLiteral("BTN")});
  connect(range_player_, &QComboBox::currentIndexChanged, this, [this] { refresh_range_matrix(); });
  range_layout->addWidget(range_player_);
  range_layout->addWidget(new QLabel(
      tr_text("Scegli il peso e dipingi il range: click o trascinamento sinistro applica il "
              "peso, il tasto destro azzera."),
      range_tab));
  range_matrix_ = new QTableWidget(9, 9, range_tab);
  range_matrix_->setObjectName(QStringLiteral("rangeMatrixEditor"));
  range_matrix_->setSelectionMode(QAbstractItemView::NoSelection);
  range_matrix_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  range_matrix_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  range_matrix_->horizontalHeader()->setDefaultSectionSize(68);
  range_matrix_->verticalHeader()->setDefaultSectionSize(68);
  range_matrix_->setFixedSize(9 * 68 + 36, 9 * 68 + 36);
  range_matrix_->viewport()->installEventFilter(this);
  range_matrix_->viewport()->setMouseTracking(true);
  auto *const range_center = new QHBoxLayout();
  range_center->addStretch(1);
  range_center->addWidget(range_matrix_);
  range_center->addStretch(1);
  range_layout->addLayout(range_center, 1);
  auto *const weight_row = new QHBoxLayout();
  range_weight_ = new QDoubleSpinBox(range_tab);
  range_weight_->setRange(0.0, 100.0);
  range_weight_->setDecimals(2);
  range_weight_->setSingleStep(0.25);
  range_weight_->setValue(100.0);
  range_slider_ = new QSlider(Qt::Horizontal, range_tab);
  range_slider_->setRange(0, 10'000);
  range_slider_->setValue(10'000);
  range_slider_->setMinimumWidth(320);
  connect(range_weight_, &QDoubleSpinBox::valueChanged, this, [this](const double value) {
    const QSignalBlocker blocker(range_slider_);
    range_slider_->setValue(static_cast<int>(std::llround(value * 100.0)));
  });
  connect(range_slider_, &QSlider::valueChanged, this, [this](const int value) {
    const QSignalBlocker blocker(range_weight_);
    range_weight_->setValue(value / 100.0);
  });
  weight_row->addWidget(new QLabel(tr_text("Pennello %"), range_tab));
  weight_row->addWidget(range_weight_);
  weight_row->addWidget(range_slider_, 1);
  weight_row->addStretch(1);
  range_layout->addLayout(weight_row);
  builder_tabs->addTab(range_tab, tr_text("Range"));
  connect(builder_tabs, &QTabWidget::currentChanged, this, [this](const int index) {
    if (index == 3) {
      static_cast<void>(sync_visual_config());
    }
  });
  builder_layout->addWidget(builder_tabs, 1);

  auto *const settings = new QGroupBox(tr_text("Criterio di convergenza"), builder);
  auto *const settings_form = new QFormLayout(settings);
  target_dev_ = new QDoubleSpinBox(settings);
  target_dev_->setObjectName(QStringLiteral("targetDev"));
  target_dev_->setRange(0.0001, 100.0);
  target_dev_->setDecimals(4);
  target_dev_->setValue(1.0);
  target_dev_->setSuffix(QStringLiteral("%"));
  target_dev_->setToolTip(
      tr_text("Target dEV misurato come NashConv/Pot. Il solve certifica ogni iterazione e "
              "si arresta automaticamente quando raggiunge la soglia."));
  settings_form->addRow(tr_text("Target dEV"), target_dev_);
  settings_form->addRow(
      new QLabel(tr_text("RAM, disco e modalità memoria vengono scelti automaticamente dal "
                         "preflight. I log sono disponibili dalla toolbar Log…"),
                 settings));
  builder_layout->addWidget(settings);
  estimate_summary_ = new QLabel(tr_text("Eseguire la stima prima del solve."), builder);
  estimate_summary_->setObjectName(QStringLiteral("estimateSummary"));
  builder_layout->addWidget(estimate_summary_);
  auto *const builder_buttons = new QHBoxLayout();
  auto *const estimate_button = new QPushButton(tr_text("Valida e stima"), builder);
  auto *const solve_button = new QPushButton(tr_text("Avvia solve exact"), builder);
  estimate_button->setObjectName(QStringLiteral("estimateButton"));
  solve_button->setObjectName(QStringLiteral("solveButton"));
  connect(estimate_button, &QPushButton::clicked, this,
          [this] { static_cast<void>(estimate_current_project()); });
  connect(solve_button, &QPushButton::clicked, this,
          [this] { static_cast<void>(start_current_solve()); });
  builder_buttons->addWidget(estimate_button);
  builder_buttons->addWidget(solve_button);
  builder_layout->addLayout(builder_buttons);
  pages_->addWidget(builder);

  auto *const monitor = new QWidget(pages_);
  auto *const monitor_layout = new QVBoxLayout(monitor);
  solve_status_ = new QLabel(tr_text("Nessun solve attivo"), monitor);
  solve_status_->setObjectName(QStringLiteral("solveStatus"));
  solve_progress_ = new QProgressBar(monitor);
  solve_progress_->setObjectName(QStringLiteral("solveProgress"));
  solve_metrics_ = new QLabel(monitor);
  solve_metrics_->setObjectName(QStringLiteral("solveMetrics"));
  convergence_ = new QTableWidget(0, 5, monitor);
  convergence_->setHorizontalHeaderLabels({tr_text("Iterazione"), tr_text("EV CO"),
                                           tr_text("EV BTN"), tr_text("NashConv"),
                                           tr_text("NashConv/Pot")});
  convergence_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  convergence_chart_ = new ConvergenceChart(monitor);
  auto *const monitor_buttons = new QHBoxLayout();
  pause_button_ = new QPushButton(tr_text("Pausa"), monitor);
  cancel_button_ = new QPushButton(tr_text("Annulla"), monitor);
  connect(pause_button_, &QPushButton::clicked, this, [this] {
    if (session_) {
      session_->command.store(PostflopControlCommand::Pause);
      solve_status_->setText(tr_text("Pausa richiesta…"));
    }
  });
  connect(cancel_button_, &QPushButton::clicked, this, [this] {
    if (session_) {
      session_->command.store(PostflopControlCommand::Cancel);
      solve_status_->setText(tr_text("Annullamento richiesto…"));
    }
  });
  monitor_buttons->addWidget(pause_button_);
  monitor_buttons->addWidget(cancel_button_);
  pause_button_->setEnabled(false);
  cancel_button_->setEnabled(false);
  monitor_layout->addWidget(solve_status_);
  monitor_layout->addWidget(solve_progress_);
  monitor_layout->addWidget(solve_metrics_);
  monitor_layout->addWidget(convergence_chart_);
  monitor_layout->addWidget(convergence_, 1);
  monitor_layout->addLayout(monitor_buttons);
  pages_->addWidget(monitor);

  auto *const browser = new QWidget(pages_);
  auto *const browser_layout = new QVBoxLayout(browser);
  browser_summary_ = new QLabel(browser);
  browser_summary_->setObjectName(QStringLiteral("browserSummary"));
  auto *const browser_splitter = new QSplitter(browser);
  tree_view_ = new QTreeView(browser_splitter);
  tree_view_->setObjectName(QStringLiteral("solutionTree"));
  tree_view_->setUniformRowHeights(true);
  tree_model_ = new PublicTreeModel(tree_view_);
  tree_view_->setModel(tree_model_);
  auto *const strategy_tabs = new QTabWidget(browser_splitter);
  strategy_matrix_ = new QTableWidget(9, 9, strategy_tabs);
  strategy_matrix_->setObjectName(QStringLiteral("strategyMatrix"));
  strategy_matrix_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  strategy_matrix_->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  strategy_table_ = new QTableWidget(0, 3, browser_splitter);
  strategy_table_->setObjectName(QStringLiteral("strategyTable"));
  strategy_table_->setHorizontalHeaderLabels(
      {tr_text("Azione"), tr_text("Importo"), tr_text("Frequenza")});
  strategy_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  strategy_tabs->addTab(strategy_matrix_, tr_text("Strategia per classe"));
  strategy_tabs->addTab(strategy_table_, tr_text("Dettaglio combo"));
  browser_splitter->addWidget(tree_view_);
  browser_splitter->addWidget(strategy_tabs);
  auto *const query_row = new QHBoxLayout();
  combo_selector_ = new QSpinBox(browser);
  combo_selector_->setRange(0, 629);
  browser_action_ = new QComboBox(browser);
  connect(browser_action_, &QComboBox::currentIndexChanged, this,
          [this] { refresh_strategy_matrix(); });
  auto *const query_button = new QPushButton(tr_text("Mostra classe/combo"), browser);
  connect(query_button, &QPushButton::clicked, this, [this] {
    const auto index = tree_view_->currentIndex().isValid() ? tree_view_->currentIndex()
                                                            : tree_model_->index(0, 0);
    static_cast<void>(
        browse(tree_model_->node_id(index), static_cast<ComboId>(combo_selector_->value())));
  });
  query_row->addWidget(new QLabel(tr_text("Combo fisica 0–629"), browser));
  query_row->addWidget(combo_selector_);
  query_row->addWidget(new QLabel(tr_text("Heatmap azione"), browser));
  query_row->addWidget(browser_action_);
  query_row->addWidget(query_button);
  browser_layout->addWidget(browser_summary_);
  browser_layout->addWidget(browser_splitter, 1);
  browser_layout->addLayout(query_row);
  pages_->addWidget(browser);

  add_page_action(tr_text("Home"), 0);
  add_page_action(tr_text("Progetto"), 1);
  add_page_action(tr_text("Solve"), 2);
  add_page_action(tr_text("Soluzione"), 3);
}

void ProductWindow::load_default_project() {
  const auto config = make_postflop_benchmark_config(PostflopBenchmark::PfF1);
  if (!config) {
    statusBar()->showMessage(tr_text("Configurazione predefinita non disponibile"));
    return;
  }
  project_.config = config.value();
  project_.config.initial_pot = Money::from_antes(40).value();
  project_.config.effective_stack = Money::from_antes(100).value();
  project_.config.rake.enabled = true;
  project_.config.rake.percentage = RangeWeight::from_basis_points(0).value();
  project_.config.rake.cap = Money{};
  const auto zero = RangeWeight::from_basis_points(0).value();
  for (auto &range : project_.ranges.players) {
    range.fill(zero);
  }
  refresh_config_controls();
  target_dev_->setValue(project_.target_normalized_nash_conv * 100.0);
  for (auto &player : player_betting_) {
    player.default_bet->setValue(50.0);
    for (auto &street : player.streets) {
      street.custom->setChecked(false);
    }
  }
  refresh_range_matrix();

  QSettings settings;
  for (const auto &path : settings.value(QStringLiteral("recentSolutions")).toStringList()) {
    recent_->addItem(path);
  }
  connect(recent_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *const item) {
    const auto path = std::filesystem::path(item->text().toStdWString());
    const auto key = stored_solution_key(path);
    if (key) {
      static_cast<void>(open_saved_solution(path, *key));
    } else {
      statusBar()->showMessage(
          tr_text("Chiave locale non trovata: usa Apri… per importare la soluzione."));
    }
  });
  const auto recovery_key =
      storage_key_from_hex(settings.value(QStringLiteral("recoveryKey")).toString().toStdString());
  if (!QCoreApplication::arguments().contains(QStringLiteral("--e2e")) && recovery_key &&
      std::filesystem::exists(recovery_checkpoint_path())) {
    QTimer::singleShot(0, this, [this, key = recovery_key.value()] {
      if (QMessageBox::question(this, tr_text("Ripristino disponibile"),
                                tr_text("È disponibile un checkpoint verificato. Riaprirlo?")) ==
          QMessageBox::Yes) {
        static_cast<void>(open_saved_solution(recovery_checkpoint_path(), key));
      }
    });
  }
}

bool ProductWindow::load_config_text(const std::string &json) {
  const auto parsed = parse_tree_config_json(json);
  if (!parsed) {
    statusBar()->showMessage(tr_text("Configurazione non valida: %1")
                                 .arg(QString::fromLatin1(tree_config_error_name(parsed.error()))));
    return false;
  }
  const bool changed =
      serialize_tree_config_json(project_.config) != serialize_tree_config_json(parsed.value());
  project_.config = parsed.value();
  if (changed) {
    invalidate_solution();
  }
  refresh_config_controls();
  refresh_range_matrix();
  return true;
}

void ProductWindow::refresh_config_controls() {
  constexpr double units_per_ante = static_cast<double>(Money::units_per_ante);
  starting_pot_->setValue(static_cast<double>(project_.config.initial_pot.units()) /
                          units_per_ante);
  effective_stack_->setValue(static_cast<double>(project_.config.effective_stack.units()) /
                             units_per_ante);
  rake_percentage_->setValue(project_.config.rake.percentage.basis_points() / 100.0);
  rake_cap_->setValue(static_cast<double>(project_.config.rake.cap.units()) / units_per_ante);

  selected_board_ = configured_board(project_.config);
  refresh_board_picker();

  const auto sizes_text = [](const std::vector<PotPercentage> &source) {
    QStringList sizes;
    for (const auto size : source) {
      sizes.push_back(QString::number(size.basis_points() / 100.0, 'f', 2));
    }
    return sizes.join(QStringLiteral(", "));
  };

  for (std::size_t player = 0; player < 2U; ++player) {
    auto &widgets = player_betting_[player];
    const auto bet_scenario = player == 0U ? BettingScenario::Lead : BettingScenario::AfterCheck;
    const auto &first_bet =
        project_.config.streets[0].players[player][static_cast<std::size_t>(bet_scenario)];
    if (!first_bet.aggressive_sizes.empty()) {
      widgets.default_bet->setValue(first_bet.aggressive_sizes.front().basis_points() / 100.0);
    }
    const auto policy = first_bet.all_in_mode == AllInMode::Add  ? Qt::PartiallyChecked
                        : first_bet.all_in_mode == AllInMode::Go ? Qt::Checked
                                                                 : Qt::Unchecked;
    widgets.all_in_policy->setCheckState(policy);
    widgets.all_in_threshold->setValue(first_bet.all_in_threshold.basis_points() / 100.0);
    for (std::size_t street = 0; street < 3U; ++street) {
      const auto &bet =
          project_.config.streets[street].players[player][static_cast<std::size_t>(bet_scenario)];
      const auto &raise =
          project_.config.streets[street]
              .players[player][static_cast<std::size_t>(BettingScenario::FacingBet)];
      auto &street_widgets = widgets.streets[street];
      street_widgets.bet_sizes->setText(sizes_text(bet.aggressive_sizes));
      street_widgets.raise_sizes->setText(sizes_text(raise.aggressive_sizes));
      street_widgets.maximum_raises->setValue(raise.raise_depth);
      street_widgets.custom->setChecked(!bet.aggressive_sizes.empty() ||
                                        !raise.aggressive_sizes.empty());
    }
  }
}

void ProductWindow::refresh_board_picker() {
  for (std::size_t index = 0; index < board_buttons_.size(); ++index) {
    if (board_buttons_[index] == nullptr) {
      continue;
    }
    const auto card = CardId::from_index(static_cast<std::uint8_t>(index)).value();
    const QSignalBlocker blocker(board_buttons_[index]);
    board_buttons_[index]->setChecked(std::ranges::find(selected_board_, card) !=
                                      selected_board_.end());
  }
  if (board_selection_ != nullptr) {
    QStringList cards;
    for (const auto card : selected_board_) {
      cards.push_back(QString::fromStdString(format_card(card)));
    }
    const auto street = selected_board_.size() == 3U   ? tr_text("Flop")
                        : selected_board_.size() == 4U ? tr_text("Turn")
                        : selected_board_.size() == 5U ? tr_text("River")
                                                       : tr_text("Seleziona 3–5 carte");
    board_selection_->setText(tr_text("%1\n%2").arg(street, cards.join(QStringLiteral(" "))));
  }
}

bool ProductWindow::sync_visual_config() {
  auto config = project_.config;
  const auto pot = Money::from_units(
      static_cast<std::int64_t>(std::llround(starting_pot_->value() * Money::units_per_ante)));
  const auto stack = Money::from_units(
      static_cast<std::int64_t>(std::llround(effective_stack_->value() * Money::units_per_ante)));
  const auto rake_percentage = RangeWeight::from_basis_points(
      static_cast<std::int64_t>(std::llround(rake_percentage_->value() * 100.0)));
  const auto rake_cap = Money::from_units(
      static_cast<std::int64_t>(std::llround(rake_cap_->value() * Money::units_per_ante)));
  if (!pot || !stack || !rake_percentage || !rake_cap) {
    statusBar()->showMessage(tr_text("Pot, stack o rake non validi."));
    append_log(QStringLiteral("error"), QStringLiteral("visual_config_invalid_money"));
    return false;
  }
  config.initial_pot = pot.value();
  config.effective_stack = stack.value();
  config.rake.enabled = true;
  config.rake.percentage = rake_percentage.value();
  config.rake.cap = rake_cap.value();

  if (selected_board_.size() < 3U || selected_board_.size() > 5U) {
    statusBar()->showMessage(tr_text("Seleziona da tre a cinque carte per il board."));
    return false;
  }
  for (std::size_t index = 0; index < 3U; ++index) {
    config.flop[index] = selected_board_[index];
  }
  config.turn.reset();
  config.river.reset();
  if (selected_board_.size() >= 4U) {
    config.turn = selected_board_[3];
  }
  if (selected_board_.size() == 5U) {
    config.river = selected_board_[4];
  }

  const auto parse_sizes =
      [this](const QString &text) -> std::optional<std::vector<PotPercentage>> {
    std::vector<PotPercentage> result;
    const auto values =
        text.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
    if (values.size() > 3) {
      statusBar()->showMessage(tr_text("Sono consentite al massimo tre size per campo."));
      return std::nullopt;
    }
    for (const auto &value : values) {
      bool ok = false;
      const auto percentage = value.toDouble(&ok);
      const auto parsed = PotPercentage::from_basis_points(
          static_cast<std::int64_t>(std::llround(percentage * 100.0)));
      if (!ok || !parsed) {
        statusBar()->showMessage(tr_text("Size postflop non valida: %1").arg(value));
        return std::nullopt;
      }
      result.push_back(parsed.value());
    }
    return result;
  };
  const auto minimum_bet = Money::from_antes(1).value();
  for (std::size_t player = 0; player < 2U; ++player) {
    const auto &player_widgets = player_betting_[player];
    const auto default_bet = PotPercentage::from_basis_points(
        static_cast<std::int64_t>(std::llround(player_widgets.default_bet->value() * 100.0)));
    const auto threshold = PotPercentage::from_basis_points(
        static_cast<std::int64_t>(std::llround(player_widgets.all_in_threshold->value() * 100.0)));
    if (!default_bet || !threshold) {
      statusBar()->showMessage(tr_text("Default bet o soglia all-in non validi."));
      return false;
    }
    const auto policy = player_widgets.all_in_policy->checkState();
    const auto all_in_mode = policy == Qt::PartiallyChecked ? AllInMode::Add
                             : policy == Qt::Checked        ? AllInMode::Go
                                                            : AllInMode::Disabled;
    const auto bet_scenario = player == 0U ? BettingScenario::Lead : BettingScenario::AfterCheck;
    const auto unused_scenario = player == 0U ? BettingScenario::AfterCheck : BettingScenario::Lead;
    for (std::size_t street = 0; street < 3U; ++street) {
      const auto &widgets = player_widgets.streets[street];
      std::vector<PotPercentage> bet_sizes{default_bet.value()};
      std::vector<PotPercentage> raise_sizes;
      std::uint8_t maximum_raises = 0U;
      if (widgets.custom->isChecked()) {
        const auto parsed_bets = parse_sizes(widgets.bet_sizes->text());
        const auto parsed_raises = parse_sizes(widgets.raise_sizes->text());
        if (!parsed_bets || !parsed_raises || parsed_bets->empty()) {
          statusBar()->showMessage(
              tr_text("Una street personalizzata deve contenere almeno una bet size."));
          return false;
        }
        bet_sizes = *parsed_bets;
        raise_sizes = *parsed_raises;
        maximum_raises = static_cast<std::uint8_t>(widgets.maximum_raises->value());
      }
      auto configure = [&](ScenarioConfig &target, std::vector<PotPercentage> sizes,
                           const std::uint8_t raise_depth) {
        target.aggressive_sizes = std::move(sizes);
        target.raise_depth = raise_depth;
        target.all_in_mode = all_in_mode;
        target.all_in_threshold = threshold.value();
        target.minimum_bet = minimum_bet;
      };
      auto &street_config = config.streets[street].players[player];
      configure(street_config[static_cast<std::size_t>(bet_scenario)], bet_sizes, 0U);
      configure(street_config[static_cast<std::size_t>(unused_scenario)], bet_sizes, 0U);
      configure(street_config[static_cast<std::size_t>(BettingScenario::FacingBet)],
                std::move(raise_sizes), maximum_raises);
    }
  }
  const auto valid = validate_tree_config(config);
  if (!valid) {
    statusBar()->showMessage(tr_text("Configurazione non valida: %1")
                                 .arg(QString::fromLatin1(tree_config_error_name(valid.error()))));
    append_log(QStringLiteral("error"), QStringLiteral("visual_config_invalid"),
               QString::fromLatin1(tree_config_error_name(valid.error())));
    return false;
  }
  const bool changed =
      serialize_tree_config_json(project_.config) != serialize_tree_config_json(config);
  project_.config = std::move(config);
  if (changed) {
    invalidate_solution();
  }
  refresh_range_matrix();
  return true;
}

void ProductWindow::sync_project_settings() {
  project_.certification_interval = 1U;
  project_.target_normalized_nash_conv = target_dev_->value() / 100.0;
  const auto host = query_gui_benchmark_host();
  project_.ram_budget_bytes =
      host.total_physical_memory_bytes > 0U
          ? static_cast<std::uint64_t>(host.total_physical_memory_bytes * 0.8)
          : 8ULL * 1024ULL * 1024ULL * 1024ULL;
  std::error_code space_error;
  const auto space = std::filesystem::space(std::filesystem::temp_directory_path(), space_error);
  project_.disk_budget_bytes = !space_error ? static_cast<std::uint64_t>(space.available * 0.8)
                                            : 16ULL * 1024ULL * 1024ULL * 1024ULL;
}

bool ProductWindow::estimate_current_project() {
  if (!sync_visual_config()) {
    return false;
  }
  sync_project_settings();
  if (!validate_postflop_ranges(project_.config, project_.ranges)) {
    estimate_summary_->setText(tr_text("Range non valido: nessun deal privato compatibile."));
    return false;
  }
  auto report = analyze_postflop_config(project_.config, MemoryPrototype::LazyInRam);
  if (!report) {
    estimate_summary_->setText(
        tr_text("Stima fallita: %1").arg(QString::fromLatin1(memory_error_name(report.error()))));
    return false;
  }
  project_.memory_backend = MemoryPrototype::LazyInRam;
  if (report.value().memory.peak_resident_bytes > project_.ram_budget_bytes) {
    auto out_of_core = analyze_postflop_config(project_.config, MemoryPrototype::OutOfCore);
    if (!out_of_core) {
      estimate_summary_->setText(
          tr_text("Stima out-of-core fallita: %1")
              .arg(QString::fromLatin1(memory_error_name(out_of_core.error()))));
      return false;
    }
    project_.memory_backend = MemoryPrototype::OutOfCore;
    report = std::move(out_of_core);
  }
  const auto &memory = report.value().memory;
  const bool ram_ok = memory.peak_resident_bytes <= project_.ram_budget_bytes;
  const bool disk_ok = memory.backing_store_bytes <= project_.disk_budget_bytes;
  const auto mode = project_.memory_backend == MemoryPrototype::LazyInRam
                        ? tr_text("RAM automatica")
                        : tr_text("Out-of-core automatico");
  estimate_summary_->setText(
      tr_text("%1 nodi · %2 infoset · %3 azioni · picco RAM %4 MiB · disco %5 MiB · %6 · %7")
          .arg(static_cast<qulonglong>(report.value().public_tree.node_count))
          .arg(static_cast<qulonglong>(report.value().information_sets))
          .arg(static_cast<qulonglong>(report.value().actions))
          .arg(static_cast<qulonglong>(memory.peak_resident_bytes / (1024U * 1024U)))
          .arg(static_cast<qulonglong>(memory.backing_store_bytes / (1024U * 1024U)))
          .arg(mode)
          .arg(ram_ok && disk_ok ? tr_text("risorse sufficienti")
                                 : tr_text("RISORSE INSUFFICIENTI")));
  return ram_ok && disk_ok;
}

bool ProductWindow::start_current_solve() {
  if (session_ && !session_->done.load()) {
    statusBar()->showMessage(tr_text("Un solve è già in esecuzione."));
    return false;
  }
  if (!estimate_current_project()) {
    statusBar()->showMessage(tr_text("Solve bloccato dal preflight risorse."));
    return false;
  }
  if (worker_.joinable()) {
    worker_.join();
  }
  maximum_ui_heartbeat_gap_ms_ = 0.0;
  last_ui_heartbeat_ = std::chrono::steady_clock::now();
  session_ = std::make_shared<SolveSession>();
  const auto session = session_;
  const auto project = project_;
  if (std::all_of(current_key_.begin(), current_key_.end(),
                  [](const auto byte) { return byte == 0U; })) {
    current_key_ = generate_storage_key();
  }
  const auto recovery_key = current_key_;
  if (checkpoint_) {
    session->resume = std::make_shared<PostflopCheckpoint>(std::move(checkpoint_.value()));
    checkpoint_.reset();
  }
  const auto resume = session->resume;
  QSettings().setValue(QStringLiteral("recoveryKey"),
                       QString::fromStdString(storage_key_to_hex(recovery_key)));
  solve_progress_->setRange(0, 0);
  solve_status_->setText(tr_text("Solving exact CFR+…"));
  append_log(QStringLiteral("info"), QStringLiteral("solve_started"),
             tr_text("target_dev=%1% backend=%2")
                 .arg(project.target_normalized_nash_conv * 100.0, 0, 'f', 4)
                 .arg(project.memory_backend == MemoryPrototype::LazyInRam
                          ? QStringLiteral("ram")
                          : QStringLiteral("disk")));
  convergence_->setRowCount(0);
  pages_->setCurrentIndex(2);
  set_solve_controls_enabled(true);
  worker_ = std::jthread([session, project, recovery_key, resume] {
    PostflopSolveOptions options;
    options.iterations = project.iterations;
    options.certification_interval = project.certification_interval;
    options.target_normalized_nash_conv = project.target_normalized_nash_conv;
    options.memory_backend = project.memory_backend;
    if (project.memory_backend == MemoryPrototype::OutOfCore) {
      options.backing_file =
          (std::filesystem::temp_directory_path() / "gtosd_phase10_actions.bin").string();
    }
    options.progress_callback = [session](const PostflopCertification &certification) {
      std::scoped_lock lock(session->mutex);
      session->latest = certification;
    };
    options.checkpoint_callback = [project,
                                   recovery_key](const PostflopCertification &certification,
                                                 const PostflopCheckpoint &checkpoint) {
      const auto archive =
          make_postflop_solution(project.config, project.ranges, checkpoint, certification);
      return archive &&
             save_solution(recovery_checkpoint_path(), archive.value(), recovery_key).has_value();
    };
    options.control_callback = [session](const std::uint64_t iteration) {
      session->current_iteration.store(iteration);
      return session->command.load();
    };
    const auto *const resume_pointer = resume.get();
    auto solved = solve_postflop_exact(project.config, project.ranges, options, resume_pointer);
    {
      std::scoped_lock lock(session->mutex);
      if (solved) {
        session->result = std::move(solved.value());
      } else {
        session->error = postflop_solver_error_name(solved.error());
      }
    }
    session->done.store(true);
  });
  return true;
}

void ProductWindow::poll_worker() {
  if (!session_) {
    return;
  }
  solve_progress_->setValue(static_cast<int>(
      std::min<std::uint64_t>(session_->current_iteration.load(),
                              static_cast<std::uint64_t>(std::numeric_limits<int>::max()))));
  std::optional<PostflopCertification> latest;
  {
    std::scoped_lock lock(session_->mutex);
    latest = session_->latest;
  }
  if (latest) {
    solve_progress_->setValue(static_cast<int>(std::min<std::uint64_t>(
        latest->iteration, static_cast<std::uint64_t>(std::numeric_limits<int>::max()))));
    solve_metrics_->setText(tr_text("Iterazione %1 · EV CO %2 · EV BTN %3 · NashConv/Pot %4%")
                                .arg(static_cast<qulonglong>(latest->iteration))
                                .arg(latest->profile_value_antes[0], 0, 'f', 6)
                                .arg(latest->profile_value_antes[1], 0, 'f', 6)
                                .arg(latest->normalized_nash_conv * 100.0, 0, 'f', 6));
  }
  if (session_->done.load()) {
    finish_worker();
  }
}

void ProductWindow::finish_worker() {
  if (!session_) {
    return;
  }
  std::optional<PostflopSolveResult> result;
  std::string error;
  {
    std::scoped_lock lock(session_->mutex);
    if (session_->result) {
      result = std::move(session_->result);
      session_->result.reset();
    }
    error = session_->error;
  }
  if (worker_.joinable()) {
    worker_.join();
  }
  set_solve_controls_enabled(false);
  solve_progress_->setRange(0, 1);
  solve_progress_->setValue(result ? 1 : 0);
  maximum_solve_heartbeat_gap_ms_ = maximum_ui_heartbeat_gap_ms_;
  if (!result) {
    if (session_->resume) {
      checkpoint_ = std::move(*session_->resume);
    }
    if (!error.empty()) {
      solve_status_->setText(tr_text("Solve fallito: %1").arg(QString::fromStdString(error)));
      append_log(QStringLiteral("error"), QStringLiteral("solve_failed"),
                 QString::fromStdString(error));
    }
    session_.reset();
    return;
  }
  checkpoint_ = std::move(result->checkpoint);
  if (!result->convergence.empty()) {
    certification_ = result->convergence.back();
  }
  convergence_->setRowCount(static_cast<int>(result->convergence.size()));
  for (std::size_t row = 0; row < result->convergence.size(); ++row) {
    const auto &point = result->convergence[row];
    const std::array<QString, 5> values{
        QString::number(static_cast<qulonglong>(point.iteration)),
        QString::number(point.profile_value_antes[0], 'f', 8),
        QString::number(point.profile_value_antes[1], 'f', 8),
        QString::number(point.nash_conv_antes, 'f', 8),
        QString::number(point.normalized_nash_conv * 100.0, 'f', 8) + QStringLiteral("%")};
    for (int column = 0; column < static_cast<int>(values.size()); ++column) {
      convergence_->setItem(static_cast<int>(row), column,
                            new QTableWidgetItem(values[static_cast<std::size_t>(column)]));
    }
  }
  convergence_chart_->set_points(result->convergence);
  const auto reason = result->stop_reason == PostflopStopReason::Completed ? tr_text("Completato")
                      : result->stop_reason == PostflopStopReason::Converged
                          ? tr_text("Target dEV raggiunto")
                      : result->stop_reason == PostflopStopReason::Paused ? tr_text("In pausa")
                                                                          : tr_text("Annullato");
  solve_status_->setText(reason);
  append_log(QStringLiteral("info"), QStringLiteral("solve_finished"), reason);
  session_.reset();
  if (certification_) {
    solve_progress_->setValue(static_cast<int>(std::min<std::uint64_t>(
        certification_->iteration, static_cast<std::uint64_t>(std::numeric_limits<int>::max()))));
  }
  update_browser_tree();
}

bool ProductWindow::wait_for_solve(const std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (session_ && std::chrono::steady_clock::now() < deadline) {
    QApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return !session_ && checkpoint_.has_value() && certification_.has_value();
}

bool ProductWindow::save_current_solution(const std::filesystem::path &path,
                                          const StorageKey &key) {
  if (!checkpoint_ || !certification_ || path.empty()) {
    statusBar()->showMessage(tr_text("Nessuna soluzione completa da salvare."));
    return false;
  }
  const auto archive =
      make_postflop_solution(project_.config, project_.ranges, *checkpoint_, *certification_);
  if (!archive) {
    statusBar()->showMessage(tr_text("Creazione archivio fallita: %1")
                                 .arg(QString::fromLatin1(storage_error_name(archive.error()))));
    return false;
  }
  const auto saved = save_solution(path, archive.value(), key);
  if (!saved) {
    statusBar()->showMessage(tr_text("Salvataggio fallito: %1")
                                 .arg(QString::fromLatin1(storage_error_name(saved.error()))));
    return false;
  }
  current_solution_path_ = path;
  current_key_ = key;
  store_solution_key(path, key);
  update_recent(path);
  std::error_code ignored;
  std::filesystem::remove(recovery_checkpoint_path(), ignored);
  QSettings().remove(QStringLiteral("recoveryKey"));
  statusBar()->showMessage(tr_text("Soluzione salvata e verificata atomicamente."));
  append_log(QStringLiteral("info"), QStringLiteral("solution_saved"),
             QString::fromStdWString(path.wstring()));
  return true;
}

bool ProductWindow::open_saved_solution(const std::filesystem::path &path, const StorageKey &key) {
  const auto lazy = open_solution_for_gui(path, key);
  if (!lazy || lazy.value().strategy_loaded || lazy.value().loaded_chunks != 1U) {
    statusBar()->showMessage(tr_text("Apertura root lazy fallita."));
    return false;
  }
  const auto reader = open_solution(path, key);
  if (!reader) {
    return false;
  }
  auto restored = restore_postflop_solution(reader.value());
  if (!restored) {
    statusBar()->showMessage(tr_text("Soluzione corrotta o incompatibile: %1")
                                 .arg(QString::fromLatin1(storage_error_name(restored.error()))));
    return false;
  }
  project_.config = restored.value().config;
  project_.ranges = std::move(restored.value().ranges);
  checkpoint_ = std::move(restored.value().checkpoint);
  certification_ = restored.value().certification;
  project_.iterations = std::max(project_.iterations, certification_->iteration);
  current_solution_path_ = path;
  current_key_ = key;
  store_solution_key(path, key);
  refresh_config_controls();
  refresh_range_matrix();
  update_recent(path);
  update_browser_tree();
  show_browser();
  append_log(QStringLiteral("info"), QStringLiteral("solution_opened"),
             QString::fromStdWString(path.wstring()));
  return browser_tree_ != nullptr;
}

void ProductWindow::update_browser_tree() {
  const auto tree = build_public_tree(project_.config);
  if (!tree) {
    browser_summary_->setText(tr_text("Ricostruzione albero fallita: %1")
                                  .arg(QString::fromLatin1(tree_error_name(tree.error()))));
    return;
  }
  browser_tree_ = std::make_shared<PublicTree>(std::move(tree.value()));
  tree_model_->set_tree(browser_tree_);
  const auto certification = snapshot_certification();
  browser_summary_->setText(
      certification
          ? tr_text("%1 nodi · EV CO %2 · EV BTN %3 · NashConv/Pot %4% · exact/no bucketing")
                .arg(static_cast<qulonglong>(browser_tree_->stats.node_count))
                .arg(certification->profile_value_antes[0], 0, 'f', 6)
                .arg(certification->profile_value_antes[1], 0, 'f', 6)
                .arg(certification->normalized_nash_conv * 100.0, 0, 'f', 6)
          : tr_text("%1 nodi · soluzione non certificata")
                .arg(static_cast<qulonglong>(browser_tree_->stats.node_count)));
  tree_view_->setCurrentIndex(tree_model_->index(0, 0));
}

bool ProductWindow::browse(const NodeId node, const ComboId combo) {
  if (!checkpoint_ || !browser_tree_ || node >= browser_tree_->nodes.size() || combo >= 630U) {
    return false;
  }
  auto queries = query_postflop_strategies(project_.config, project_.ranges, *checkpoint_, node);
  if (!queries) {
    strategy_table_->setRowCount(0);
    statusBar()->showMessage(
        tr_text("Nodo/combo non interrogabile: %1")
            .arg(QString::fromLatin1(postflop_solver_error_name(queries.error()))));
    return false;
  }
  browser_queries_ = std::move(queries.value());
  const auto selected = std::find_if(browser_queries_.begin(), browser_queries_.end(),
                                     [combo](const auto &query) { return query.combo == combo; });
  if (selected == browser_queries_.end()) {
    return false;
  }
  browser_action_->blockSignals(true);
  browser_action_->clear();
  for (const auto &action : selected->actions) {
    browser_action_->addItem(action_name(action.type));
  }
  browser_action_->blockSignals(false);
  strategy_table_->setRowCount(static_cast<int>(selected->actions.size()));
  for (std::size_t row = 0; row < selected->actions.size(); ++row) {
    const auto &action = selected->actions[row];
    strategy_table_->setItem(static_cast<int>(row), 0,
                             new QTableWidgetItem(action_name(action.type)));
    strategy_table_->setItem(
        static_cast<int>(row), 1,
        new QTableWidgetItem(QString::fromStdString(format_money(action.amount))));
    auto *const frequency = new QProgressBar(strategy_table_);
    frequency->setRange(0, 10'000);
    frequency->setValue(static_cast<int>(std::llround(selected->probabilities[row] * 10'000.0)));
    frequency->setFormat(QString::number(selected->probabilities[row] * 100.0, 'f', 4) +
                         QStringLiteral("%"));
    strategy_table_->setCellWidget(static_cast<int>(row), 2, frequency);
  }
  refresh_strategy_matrix();
  const auto &physical = all_combos()[combo];
  browser_summary_->setText(
      browser_summary_->text() +
      tr_text(" · Nodo %1 · %2 (%3) · peso CO %4% · peso BTN %5%")
          .arg(static_cast<qulonglong>(node))
          .arg(combo_name(physical))
          .arg(QString::fromStdString(class_name(hand_class(physical))))
          .arg(project_.ranges.players[0][combo].basis_points() / 100.0, 0, 'f', 2)
          .arg(project_.ranges.players[1][combo].basis_points() / 100.0, 0, 'f', 2));
  return true;
}

void ProductWindow::refresh_strategy_matrix() {
  if (browser_queries_.empty() || browser_action_ == nullptr ||
      browser_action_->currentIndex() < 0) {
    return;
  }
  const auto action = static_cast<std::size_t>(browser_action_->currentIndex());
  const auto actor =
      browser_tree_
          ? static_cast<std::size_t>(
                browser_tree_->nodes[static_cast<std::size_t>(browser_queries_.front().public_node)]
                    .state.player_to_act)
          : 0U;
  const auto combos = all_combos();
  for (int row = 0; row < 9; ++row) {
    for (int column = 0; column < 9; ++column) {
      const auto class_id = matrix_class(row, column).value();
      double weighted_probability = 0.0;
      double total_weight = 0.0;
      for (const auto &query : browser_queries_) {
        if (hand_class(combos[query.combo]) != class_id || action >= query.probabilities.size()) {
          continue;
        }
        const double weight =
            static_cast<double>(project_.ranges.players[actor][query.combo].basis_points());
        weighted_probability += weight * query.probabilities[action];
        total_weight += weight;
      }
      auto *item = strategy_matrix_->item(row, column);
      if (item == nullptr) {
        item = new QTableWidgetItem();
        strategy_matrix_->setItem(row, column, item);
      }
      if (total_weight == 0.0) {
        item->setText(QStringLiteral("%1\n—").arg(QString::fromStdString(class_name(class_id))));
        item->setBackground(QColor(45, 45, 45));
        item->setToolTip(tr_text("Classe assente dal range sorgente"));
      } else {
        const double probability = weighted_probability / total_weight;
        item->setText(QStringLiteral("%1\n%2%")
                          .arg(QString::fromStdString(class_name(class_id)))
                          .arg(probability * 100.0, 0, 'f', 2));
        item->setBackground(QColor(35, 55 + static_cast<int>(probability * 150.0), 90));
        item->setToolTip(
            tr_text("Media combo-weighted; nessuna rinormalizzazione del range sorgente."));
      }
    }
  }
}

void ProductWindow::refresh_range_matrix() {
  const auto player = static_cast<std::size_t>(range_player_->currentIndex());
  const auto combos = all_combos();
  for (int row = 0; row < 9; ++row) {
    for (int column = 0; column < 9; ++column) {
      const auto class_id = matrix_class(row, column).value();
      std::uint64_t total = 0;
      std::uint64_t count = 0;
      std::uint64_t available = 0;
      for (std::size_t combo = 0; combo < combos.size(); ++combo) {
        if (hand_class(combos[combo]) == class_id) {
          ++count;
          if (!combo_blocked(combos[combo], project_.config)) {
            total += project_.ranges.players[player][combo].basis_points();
            ++available;
          }
        }
      }
      auto *item = range_matrix_->item(row, column);
      if (item == nullptr) {
        item = new QTableWidgetItem();
        range_matrix_->setItem(row, column, item);
      }
      item->setData(Qt::UserRole, class_id);
      const double average =
          available == 0U ? 0.0
                          : static_cast<double>(total) / static_cast<double>(available) / 100.0;
      item->setText(QStringLiteral("%1\n%2%")
                        .arg(QString::fromStdString(class_name(class_id)))
                        .arg(average, 0, 'f', 2));
      item->setFlags(available == 0U ? Qt::NoItemFlags : Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      const auto intensity = static_cast<int>(std::lround(average * 1.6));
      item->setBackground(QColor(30, 55 + intensity, 85));
      item->setToolTip(tr_text("%1 combo fisiche, %2 disponibili")
                           .arg(static_cast<qulonglong>(count))
                           .arg(static_cast<qulonglong>(available)));
    }
  }
}

void ProductWindow::paint_range_cell(const int row, const int column, const bool erase) {
  if (row < 0 || row >= range_matrix_->rowCount() || column < 0 ||
      column >= range_matrix_->columnCount()) {
    return;
  }
  const auto *const item = range_matrix_->item(row, column);
  if (item == nullptr || !(item->flags() & Qt::ItemIsEnabled)) {
    return;
  }
  const auto class_id = static_cast<HandClassId>(item->data(Qt::UserRole).toUInt());
  const auto player = static_cast<std::size_t>(range_player_->currentIndex());
  const auto parsed = RangeWeight::from_basis_points(
      erase ? 0 : static_cast<std::int64_t>(std::llround(range_weight_->value() * 100.0)));
  if (!parsed) {
    return;
  }
  const auto combos = all_combos();
  for (std::size_t combo = 0; combo < combos.size(); ++combo) {
    if (hand_class(combos[combo]) == class_id && !combo_blocked(combos[combo], project_.config)) {
      project_.ranges.players[player][combo] = parsed.value();
    }
  }
  invalidate_solution();
  refresh_range_matrix();
}

bool ProductWindow::eventFilter(QObject *const watched, QEvent *const event) {
  if (range_matrix_ != nullptr && watched == range_matrix_->viewport()) {
    if (event->type() == QEvent::MouseButtonPress) {
      const auto *const mouse = static_cast<QMouseEvent *>(event);
      if (mouse->button() == Qt::LeftButton || mouse->button() == Qt::RightButton) {
        range_painting_ = true;
        range_erasing_ = mouse->button() == Qt::RightButton;
        const auto index = range_matrix_->indexAt(mouse->position().toPoint());
        paint_range_cell(index.row(), index.column(), range_erasing_);
        return true;
      }
    } else if (event->type() == QEvent::MouseMove && range_painting_) {
      const auto *const mouse = static_cast<QMouseEvent *>(event);
      const auto index = range_matrix_->indexAt(mouse->position().toPoint());
      paint_range_cell(index.row(), index.column(), range_erasing_);
      return true;
    } else if (event->type() == QEvent::MouseButtonRelease) {
      range_painting_ = false;
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void ProductWindow::show_builder() { pages_->setCurrentIndex(1); }
void ProductWindow::show_browser() { pages_->setCurrentIndex(3); }

void ProductWindow::set_solve_controls_enabled(const bool solving) {
  if (pause_button_ != nullptr) {
    pause_button_->setEnabled(solving);
  }
  if (cancel_button_ != nullptr) {
    cancel_button_->setEnabled(solving);
  }
  if (pause_action_ != nullptr) {
    pause_action_->setEnabled(solving);
  }
  if (cancel_action_ != nullptr) {
    cancel_action_->setEnabled(solving);
  }
}

void ProductWindow::invalidate_solution() {
  checkpoint_.reset();
  certification_.reset();
  browser_tree_.reset();
  browser_queries_.clear();
  if (tree_model_ != nullptr) {
    tree_model_->set_tree({});
  }
  if (strategy_table_ != nullptr) {
    strategy_table_->setRowCount(0);
  }
}

void ProductWindow::update_recent(const std::filesystem::path &path) {
  const auto value = QString::fromStdWString(std::filesystem::absolute(path).wstring());
  QStringList recent;
  recent.push_back(value);
  for (int row = 0; row < recent_->count() && recent.size() < 10; ++row) {
    const auto existing = recent_->item(row)->text();
    if (existing != value) {
      recent.push_back(existing);
    }
  }
  recent_->clear();
  recent_->addItems(recent);
  QSettings settings;
  settings.setValue(QStringLiteral("recentSolutions"), recent);
}

void ProductWindow::store_solution_key(const std::filesystem::path &path, const StorageKey &key) {
  QSettings settings;
  settings.setValue(solution_key_setting(path), QString::fromStdString(storage_key_to_hex(key)));
  settings.sync();
}

std::optional<StorageKey>
ProductWindow::stored_solution_key(const std::filesystem::path &path) const {
  const auto key =
      storage_key_from_hex(QSettings().value(solution_key_setting(path)).toString().toStdString());
  if (!key) {
    return std::nullopt;
  }
  return key.value();
}

std::optional<PostflopCheckpoint> ProductWindow::snapshot_checkpoint() const { return checkpoint_; }

std::optional<PostflopCertification> ProductWindow::snapshot_certification() const {
  return certification_;
}

void ProductWindow::closeEvent(QCloseEvent *const event) {
  if (session_ && !session_->done.load()) {
    session_->command.store(PostflopControlCommand::Pause);
    solve_status_->setText(
        tr_text("Pausa richiesta; il checkpoint verrà completato prima della chiusura."));
    event->ignore();
    return;
  }
  event->accept();
}

bool ProductWindow::run_phase10_e2e(const std::filesystem::path &workspace, std::string &failure) {
  const auto check = [&failure](const bool condition, const std::string_view message) {
    if (!condition && failure.empty()) {
      failure = message;
    }
    return condition;
  };
  auto config = make_postflop_benchmark_config(PostflopBenchmark::PfF1);
  if (!check(config.has_value(), "default config unavailable")) {
    return false;
  }
  for (std::size_t street = 0; street < 2U; ++street) {
    for (auto &player : config.value().streets[street].players) {
      for (auto &scenario : player) {
        scenario.aggressive_sizes.clear();
        scenario.raise_depth = 0;
        scenario.all_in_mode = AllInMode::Disabled;
      }
    }
  }
  for (auto &player : config.value().streets[2].players) {
    for (auto &scenario : player) {
      scenario.aggressive_sizes.clear();
      scenario.raise_depth = 0;
      scenario.all_in_mode = AllInMode::Disabled;
    }
  }
  if (!check(load_config_text(serialize_tree_config_json(config.value())),
             "create/configure failed")) {
    return false;
  }
  if (!check(player_betting_[0].default_bet != nullptr &&
                 player_betting_[1].default_bet != nullptr && board_buttons_[0] != nullptr &&
                 board_buttons_[35] != nullptr && target_dev_ != nullptr,
             "visual tree controls unavailable")) {
    return false;
  }
  range_weight_->setValue(25.0);
  paint_range_cell(0, 0, false);
  const auto painted_class = matrix_class(0, 0).value();
  const auto painted = std::ranges::any_of(all_combos(), [this, painted_class](const Combo &combo) {
    const auto combos = all_combos();
    const auto iterator = std::ranges::find(combos, combo);
    const auto index = static_cast<std::size_t>(std::distance(combos.begin(), iterator));
    return hand_class(combo) == painted_class && !combo_blocked(combo, project_.config) &&
           project_.ranges.players[0][index].basis_points() == 2'500U;
  });
  if (!check(painted, "click-to-paint range failed")) {
    return false;
  }
  const auto half = RangeWeight::from_basis_points(5'000).value();
  const auto combos = all_combos();
  for (std::size_t combo = 0; combo < combos.size(); ++combo) {
    if (!combo_blocked(combos[combo], project_.config) && combo % 7U == 0U) {
      project_.ranges.players[0][combo] = half;
      project_.ranges.players[1][combo] = half;
    }
  }
  project_.iterations = 2;
  target_dev_->setValue(0.0001);
  if (!check(estimate_current_project(), "estimate failed")) {
    return false;
  }
  const auto heartbeat_before = ui_heartbeat_count_;
  if (!check(start_current_solve(), "solve did not start") ||
      !check(pause_button_->isEnabled() && cancel_button_->isEnabled() &&
                 pause_action_->isEnabled() && cancel_action_->isEnabled(),
             "pause/cancel controls are not available during solve") ||
      !check(wait_for_solve(std::chrono::seconds(90)), "solve did not complete") ||
      !check(ui_heartbeat_count_ > heartbeat_before + 5U, "UI heartbeat stopped during solve") ||
      !check(maximum_solve_heartbeat_gap_ms_ < 100.0,
             "UI heartbeat exceeded 100 ms during solve")) {
    return false;
  }
  const auto recovery = verify_solution(recovery_checkpoint_path(), current_key_);
  if (!check(recovery.has_value() && recovery.value().index_authenticated &&
                 recovery.value().all_chunks_authenticated,
             "authenticated crash recovery was not written")) {
    return false;
  }
  const auto solution_path = workspace / "phase10-e2e.gtsd";
  const auto key = generate_storage_key();
  if (!check(save_current_solution(solution_path, key), "save failed")) {
    return false;
  }
  if (!check(stored_solution_key(solution_path) == key, "local solution key was not retained")) {
    return false;
  }
  checkpoint_.reset();
  certification_.reset();
  browser_tree_.reset();
  tree_model_->set_tree({});
  if (!check(open_saved_solution(solution_path, key), "reopen failed")) {
    return false;
  }
  ComboId legal_combo = 0U;
  for (std::size_t combo = 0; combo < combos.size(); ++combo) {
    if (!combo_blocked(combos[combo], project_.config)) {
      legal_combo = static_cast<ComboId>(combo);
      break;
    }
  }
  if (!check(browse(browser_tree_->root, legal_combo), "root navigation/query failed") ||
      !check(strategy_table_->rowCount() > 0, "strategy not visible per combo") ||
      !check(strategy_matrix_->item(0, 0) != nullptr, "strategy heatmap not visible per class") ||
      !check(certification_->iteration == 2U, "certification not restored") ||
      !check(project_.ranges.players[0][7U].basis_points() == 5'000U ||
                 combo_blocked(combos[7U], project_.config),
             "physical range did not round-trip")) {
    return false;
  }
  project_.iterations = 3;
  const auto resume_heartbeat = ui_heartbeat_count_;
  if (!check(start_current_solve(), "resume did not start") ||
      !check(wait_for_solve(std::chrono::seconds(90)), "resume did not complete") ||
      !check(certification_->iteration == 3U, "resume did not advance the checkpoint") ||
      !check(ui_heartbeat_count_ > resume_heartbeat, "UI heartbeat stopped during resume") ||
      !check(maximum_solve_heartbeat_gap_ms_ < 100.0,
             "UI heartbeat exceeded 100 ms during resume")) {
    return false;
  }
  return true;
}

} // namespace gtosd::desktop
