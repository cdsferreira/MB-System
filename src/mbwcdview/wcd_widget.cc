/*--------------------------------------------------------------------
 *    The MB-system:  wcd_widget.cc  1/11/2026
 *
 *    Copyright (c) 2026 by
 *    Christian dos Santos Ferreira
 *      MARUM
 *      University of Bremen
 *      Bremen Germany
 *    David W. Caress (caress@mbari.org)
 *      Monterey Bay Aquarium Research Institute
 *      Moss Landing, California, USA
 *    Dale N. Chayes
 *      Center for Coastal and Ocean Mapping
 *      University of New Hampshire
 *      Durham, New Hampshire, USA
 *
 *    MB-System was created by Caress and Chayes in 1992 at the
 *      Lamont-Doherty Earth Observatory
 *      Columbia University
 *      Palisades, NY 10964
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/

#include "wcd_widget.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QShowEvent>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTextStream>
#include <QThread>
#include <QTimeZone>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidgetAction>

#include "mb_define.h"
#include "WcdView.h"
#include "wcd_curtain_view.h"
#include "wcd_export3d_dialog.h"

namespace wcd {

namespace {

QString utc_text(double time_d) {
  return QDateTime::fromMSecsSinceEpoch((qint64)std::llround(time_d * 1000.0), QTimeZone::utc())
             .toString("yyyy-MM-dd HH:mm:ss.zzz") + " UTC";
}

QString number_or_empty(double v, int decimals) { return std::isfinite(v) ? QString::number(v, 'f', decimals) : ""; }

QDoubleSpinBox *spin_box(double lo, double hi, double step, int decimals) {
  auto *box = new QDoubleSpinBox;
  box->setRange(lo, hi);
  box->setSingleStep(step);
  box->setDecimals(decimals);
  return box;
}

}  // namespace

/*--------------------------------------------------------------------*/
void OneStepWheelSlider::wheelEvent(QWheelEvent *event) {
  const int delta = event->angleDelta().y();
  if (delta > 0 && value() < maximum())
    setValue(value() + 1);
  else if (delta < 0 && value() > minimum())
    setValue(value() - 1);
  event->accept();
}

/*--------------------------------------------------------------------*/
RangeSlider::RangeSlider(QWidget *parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setToolTip("Range of beams: drag either handle");
}

void RangeSlider::setRange(int minimum, int maximum) {
  minimum_ = minimum;
  maximum_ = std::max(minimum, maximum);
  setValues(lower_, upper_);
}

void RangeSlider::setValues(int lower, int upper) {
  lower_ = std::min(std::max(lower, minimum_), maximum_);
  upper_ = std::min(std::max(upper, lower_), maximum_);
  update();
}

double RangeSlider::toX(int value) const {
  const double margin = 7.0, w = std::max(1.0, width() - 2.0 * margin);
  return margin + (maximum_ > minimum_ ? (double)(value - minimum_) / (maximum_ - minimum_) : 0.0) * w;
}

int RangeSlider::toValue(double x) const {
  const double margin = 7.0, w = std::max(1.0, width() - 2.0 * margin);
  return minimum_ + (int)std::lround(std::min(std::max((x - margin) / w, 0.0), 1.0) * (maximum_ - minimum_));
}

void RangeSlider::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  const double y = height() / 2.0;
  const QPalette &pal = palette();
  p.setPen(Qt::NoPen);
  p.setBrush(pal.color(QPalette::Mid));
  p.drawRoundedRect(QRectF(toX(minimum_), y - 2, toX(maximum_) - toX(minimum_), 4), 2, 2);
  p.setBrush(isEnabled() ? pal.color(QPalette::Highlight) : pal.color(QPalette::Mid));
  p.drawRect(QRectF(toX(lower_), y - 2, toX(upper_) - toX(lower_), 4));
  p.setPen(QPen(pal.color(QPalette::Dark), 1.0));
  p.setBrush(pal.color(QPalette::Button));
  for (const int v : {lower_, upper_})
    p.drawEllipse(QPointF(toX(v), y), 6.0, 6.0);
}

void RangeSlider::mousePressEvent(QMouseEvent *event) {
  const double x = event->position().x();
  /* the nearer handle; of two on one place, the one the pointer moves away to */
  const double dl = std::fabs(x - toX(lower_)), du = std::fabs(x - toX(upper_));
  handle_ = dl < du ? 0 : (du < dl ? 1 : (x < toX(lower_) ? 0 : 1));
  mouseMoveEvent(event);
}

void RangeSlider::mouseMoveEvent(QMouseEvent *event) {
  if (handle_ < 0)
    return;
  const int v = toValue(event->position().x());
  const int lower = handle_ == 0 ? std::min(v, upper_) : lower_;
  const int upper = handle_ == 1 ? std::max(v, lower_) : upper_;
  if (lower != lower_ || upper != upper_) {
    lower_ = lower;
    upper_ = upper;
    update();
    emit valuesChanged(lower_, upper_);
  }
}

void RangeSlider::mouseReleaseEvent(QMouseEvent *) { handle_ = -1; }

/*--------------------------------------------------------------------*/
const QStringList PickTable::columns = {"Ping", "UTC", "AcrossTrack_m", "Depth_m", "ShipLat",
                                         "ShipLon", "PointLat", "PointLon", "Azimuth"};

PickTable::PickTable(QWidget *parent) : QWidget(parent) {
  table_ = new QTableWidget(0, columns.size(), this);
  table_->setHorizontalHeaderLabels(columns);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  auto *save = new QPushButton("Save as TXT");
  auto *remove = new QPushButton("Remove selected");
  auto *close = new QPushButton("Close");
  connect(save, &QPushButton::clicked, this, &PickTable::saveDialog);
  connect(remove, &QPushButton::clicked, this, &PickTable::removeSelected);
  connect(close, &QPushButton::clicked, this, &PickTable::clearAndClose);
  auto *buttons = new QHBoxLayout;
  buttons->addWidget(save);
  buttons->addWidget(remove);
  buttons->addStretch(1);
  buttons->addWidget(close);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(table_);
  layout->addLayout(buttons);
}

void PickTable::addRow(const QStringList &values) {
  const int r = table_->rowCount();
  table_->insertRow(r);
  for (int c = 0; c < values.size() && c < columns.size(); c++)
    table_->setItem(r, c, new QTableWidgetItem(values[c]));
}

bool PickTable::saveText(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    return false;
  QTextStream out(&file);
  out << columns.join("\t") << "\n";
  for (int r = 0; r < table_->rowCount(); r++) {
    QStringList values;
    for (int c = 0; c < table_->columnCount(); c++)
      values << (table_->item(r, c) != nullptr ? table_->item(r, c)->text() : QString());
    out << values.join("\t") << "\n";
  }
  return true;
}

void PickTable::saveDialog() {
  const QString path =
      QFileDialog::getSaveFileName(this, "Save Picks", "picks.txt", "Text Files (*.txt);;All Files (*)");
  if (!path.isEmpty())
    saveText(path);
}

void PickTable::removeSelected() {
  QList<int> rows;
  for (const QModelIndex &index : table_->selectionModel()->selectedRows())
    rows << index.row();
  std::sort(rows.begin(), rows.end(), std::greater<int>());
  for (const int r : rows)
    table_->removeRow(r);
}

void PickTable::clearAndClose() {
  table_->setRowCount(0);
  emit closeRequested();
}

PickDialog::PickDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle("Picked Positions");
  table_ = new PickTable;
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(table_);
  connect(table_, &PickTable::closeRequested, this, &QWidget::close);
}

/*--------------------------------------------------------------------*/
WaterColumnWidget::WaterColumnWidget(Presentation presentation, QWidget *parent) : QWidget(parent), presentation_(presentation) {
  setFocusPolicy(Qt::StrongFocus);

  /* the controls are disabled while a file loads, but not the pick table, which is a child of this widget */
  central_ = new QWidget;
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->addWidget(central_);
  auto *layout = new QVBoxLayout(central_);

  /* header: a row, or the tool bar of the panel */
  const bool panel = presentation_ == Presentation::Panel;
  QToolBar *bar = panel ? new QToolBar : nullptr;
  QHBoxLayout *header = panel ? nullptr : new QHBoxLayout;
  const auto to_header = [bar, header](QWidget *widget) {
    if (bar != nullptr) {
      widget->setFocusPolicy(Qt::NoFocus);
      bar->addWidget(widget);
    }
    else {
      header->addWidget(widget);
    }
  };
  if (bar != nullptr) {
    bar->setObjectName("panel_bar");
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
  }
  auto *open = new QPushButton(panel ? "Open" : "Open .kmwcd/.kmall/.wcd/.all/.s7k/datalist");
  connect(open, &QPushButton::clicked, this, &WaterColumnWidget::openDialog);
  to_header(open);
  file_name_ = new QLabel("No file loaded");
  file_name_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  if (!panel)
    header->addWidget(file_name_);
  previous_file_ = new QPushButton("Previous file");
  next_file_ = new QPushButton("Next file");
  file_count_ = new QLabel;
  previous_file_->setToolTip("The file before in the datalist (Page Up)");
  next_file_->setToolTip("The file after in the datalist (Page Down)");
  connect(previous_file_, &QPushButton::clicked, this, [this]() { openListedFile(file_index_ - 1); });
  connect(next_file_, &QPushButton::clicked, this, [this]() { openListedFile(file_index_ + 1); });
  to_header(previous_file_);
  to_header(file_count_);
  to_header(next_file_);
  auto *save = new QPushButton("Save Image");
  connect(save, &QPushButton::clicked, this, &WaterColumnWidget::saveImageDialog);
  to_header(save);
  QAction *scales_place = panel ? bar->addSeparator() : nullptr;
  bottom_ = new QCheckBox("Bottom");
  wc_bottom_ = new QCheckBox("WC bottom");
  raytrace_ = new QCheckBox(panel ? "Ray trace" : "Ray trace (SVP)");
  raytrace_->setToolTip("Trace the water column samples through the sound velocity profile instead of "
                        "straight rays with the sound speed at the transducer");
  for (QCheckBox *box : {bottom_, wc_bottom_, raytrace_}) {
    connect(box, &QCheckBox::toggled, this, &WaterColumnWidget::redraw);
    to_header(box);
  }
  connect(raytrace_, &QCheckBox::toggled, this, &WaterColumnWidget::requestCurtain);
  pick_ = new QCheckBox("Pick");
  connect(pick_, &QCheckBox::clicked, this, &WaterColumnWidget::showPickDialog);
  connect(pick_, &QCheckBox::toggled, this, &WaterColumnWidget::togglePick);
  to_header(pick_);
  lasso_button_ = new QToolButton;
  lasso_button_->setText("Lasso");
  lasso_button_->setCheckable(true);
  lasso_button_->setObjectName("lasso");
  lasso_button_->setToolTip("Draw polygons in the swath view or the curtain that choose the samples of a 3D "
                            "export: drag to draw one, Shift-drag to add one, Alt-click (Option-click on macOS) "
                            "to remove the one under the pointer");
  auto *lasso_menu = new QMenu(this);
  lasso_menu->addAction("Clear Lasso", this, &WaterColumnWidget::clearLasso);
  lasso_menu->addAction("Save Lasso...", this, &WaterColumnWidget::saveLassoDialog);
  lasso_menu->addAction("Load Lasso...", this, &WaterColumnWidget::loadLassoDialog);
  lasso_button_->setMenu(lasso_menu);
  lasso_button_->setPopupMode(QToolButton::MenuButtonPopup);
  connect(lasso_button_, &QToolButton::toggled, this, &WaterColumnWidget::setLassoMode);
  to_header(lasso_button_);
  meta_ = new QLabel;
  if (panel) {
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    layout->addWidget(bar);
  }
  else {
    meta_->setWordWrap(true);
    header->addWidget(meta_);
    layout->addLayout(header);
  }

  /* depth, amplitude, across-track and replay controls */
  dmin_ = spin_box(0, 1.0e5, 1.0, 2);
  dmax_ = spin_box(0, 1.0e5, 1.0, 2);
  amin_ = spin_box(-200, 200, 0.5, 2);
  amax_ = spin_box(-200, 200, 0.5, 2);
  xmax_ = spin_box(0, 1.0e6, 10.0, 2);
  replay_rate_ = spin_box(0.1, 10.0, 0.1, 1);
  replay_rate_->setValue(2.0);
  replay_ = new QPushButton("Replay");
  replay_->setCheckable(true);
  if (panel) {
    /* the numbers are in a popup, so that the plot keeps the space */
    auto *scales = new QWidget;
    scales->setObjectName("panel_scales_controls");
    auto *form = new QFormLayout(scales);
    form->addRow("Depth Min (m):", dmin_);
    form->addRow("Depth Max (m):", dmax_);
    form->addRow("Amp Min (dB):", amin_);
    form->addRow("Amp Max (dB):", amax_);
    form->addRow("Across-track Max (m):", xmax_);
    form->addRow("Replay (pings/s):", replay_rate_);
    auto *scales_menu = new QMenu(this);
    auto *scales_action = new QWidgetAction(scales_menu);
    scales_action->setDefaultWidget(scales);
    scales_menu->addAction(scales_action);
    auto *scales_button = new QToolButton;
    scales_button->setText("Scales");
    scales_button->setObjectName("panel_scales");
    scales_button->setMenu(scales_menu);
    scales_button->setPopupMode(QToolButton::InstantPopup);
    scales_button->setFocusPolicy(Qt::NoFocus);
    bar->insertWidget(scales_place, scales_button);
    to_header(replay_);
  }
  else {
    auto *depth_row = new QHBoxLayout;
    depth_row->addWidget(new QLabel("Depth Min (m):"));
    depth_row->addWidget(dmin_);
    depth_row->addWidget(new QLabel("Depth Max (m):"));
    depth_row->addWidget(dmax_);
    layout->addLayout(depth_row);
    auto *amp_row = new QHBoxLayout;
    amp_row->addWidget(new QLabel("Amp Min (dB):"));
    amp_row->addWidget(amin_);
    amp_row->addWidget(new QLabel("Amp Max (dB):"));
    amp_row->addWidget(amax_);
    layout->addLayout(amp_row);
    auto *x_row = new QHBoxLayout;
    x_row->addWidget(new QLabel("Across-track Max (m):"));
    x_row->addWidget(xmax_);
    x_row->addWidget(new QLabel("Replay (pings/s):"));
    x_row->addWidget(replay_rate_);
    x_row->addWidget(replay_);
    layout->addLayout(x_row);
  }
  /* curtain: beam range, combination, palette, layout and scale */
  first_beam_ = new QSpinBox;
  last_beam_ = new QSpinBox;
  for (QSpinBox *box : {first_beam_, last_beam_}) {
    box->setRange(0, 0);
    box->setKeyboardTracking(false);
    box->setMinimumWidth(box->fontMetrics().horizontalAdvance("00000") + 28);
  }
  first_beam_->setToolTip("First beam of each swath in the curtain");
  last_beam_->setToolTip("Last beam of each swath in the curtain");
  beam_slider_ = new RangeSlider;
  beam_count_ = new QLabel("of 0-0");
  beam_angles_ = new QLabel;
  combine_ = new QComboBox;
  combine_->addItems({"Mean (linear)", "Max"});
  combine_->setToolTip("Samples of one depth cell: the mean of their linear power, or the strongest");
  palette_ = new QComboBox;
  palette_->addItems(palette_names());
  palette_->setToolTip("Colour palette of the amplitudes");
  curtain_layout_ = new QComboBox;
  curtain_layout_->addItems({"Below", "Beside", "Off"});
  curtain_layout_->setToolTip("Where the curtain (side view) is shown");
  exaggeration_ = spin_box(1.0e-3, 1.0e4, 0.1, 3);
  exaggeration_->setValue(1.0);
  exaggeration_->setKeyboardTracking(false);
  exaggeration_->setToolTip("Vertical exaggeration of the curtain: metres along track per pixel over metres in "
                            "depth per pixel (1 = 1:1)");
  auto *fit = new QPushButton("Fit");
  fit->setToolTip("The whole file in the curtain");
  auto *one_to_one = new QPushButton("1:1");
  one_to_one->setToolTip("As many metres per pixel along track as in depth");
  auto *save_curtain = new QPushButton("Save Curtain");
  auto *export3d = new QPushButton("Export 3D...");
  export3d->setToolTip("Write the fans and the curtain as 3D points for mbgrdviz, QGIS and other programs");
  connect(export3d, &QPushButton::clicked, this, &WaterColumnWidget::showExport3dDialog);
  if (panel) {
    auto *curtain = new QWidget;
    curtain->setObjectName("panel_curtain_controls");
    auto *form = new QFormLayout(curtain);
    auto *beams = new QHBoxLayout;
    beams->addWidget(first_beam_);
    beams->addWidget(beam_slider_, 1);
    beams->addWidget(last_beam_);
    beams->addWidget(beam_count_);
    form->addRow("Beams:", beams);
    form->addRow("", beam_angles_);
    form->addRow("Combine:", combine_);
    form->addRow("Palette:", palette_);
    form->addRow("Curtain:", curtain_layout_);
    auto *scale = new QHBoxLayout;
    scale->addWidget(exaggeration_);
    scale->addWidget(fit);
    scale->addWidget(one_to_one);
    form->addRow("Exaggeration:", scale);
    auto *saves = new QHBoxLayout;
    saves->addWidget(save_curtain);
    saves->addWidget(export3d);
    form->addRow("", saves);
    auto *curtain_menu = new QMenu(this);
    auto *curtain_action = new QWidgetAction(curtain_menu);
    curtain_action->setDefaultWidget(curtain);
    curtain_menu->addAction(curtain_action);
    auto *curtain_button = new QToolButton;
    curtain_button->setText("Curtain");
    curtain_button->setObjectName("panel_curtain");
    curtain_button->setMenu(curtain_menu);
    curtain_button->setPopupMode(QToolButton::InstantPopup);
    curtain_button->setFocusPolicy(Qt::NoFocus);
    bar->insertWidget(scales_place, curtain_button);
  }
  else {
    auto *curtain_row = new QHBoxLayout;
    curtain_row->addWidget(new QLabel("Beams:"));
    curtain_row->addWidget(first_beam_);
    curtain_row->addWidget(beam_slider_, 1);
    curtain_row->addWidget(last_beam_);
    curtain_row->addWidget(beam_count_);
    curtain_row->addWidget(beam_angles_);
    curtain_row->addWidget(new QLabel("Combine:"));
    curtain_row->addWidget(combine_);
    curtain_row->addWidget(new QLabel("Palette:"));
    curtain_row->addWidget(palette_);
    curtain_row->addWidget(new QLabel("Curtain:"));
    curtain_row->addWidget(curtain_layout_);
    curtain_row->addWidget(new QLabel("Vertical exaggeration:"));
    curtain_row->addWidget(exaggeration_);
    curtain_row->addWidget(fit);
    curtain_row->addWidget(one_to_one);
    curtain_row->addWidget(save_curtain);
    curtain_row->addWidget(export3d);
    layout->addLayout(curtain_row);
  }
  connect(first_beam_, &QSpinBox::valueChanged, this, &WaterColumnWidget::beamsFromSpinBoxes);
  connect(last_beam_, &QSpinBox::valueChanged, this, &WaterColumnWidget::beamsFromSpinBoxes);
  connect(beam_slider_, &RangeSlider::valuesChanged, this, &WaterColumnWidget::beamsFromSlider);
  connect(combine_, &QComboBox::currentIndexChanged, this, [this](int index) { setCombineMax(index == 1); });
  connect(palette_, &QComboBox::currentIndexChanged, this, [this](int index) { setPalette((Palette)index); });
  connect(curtain_layout_, &QComboBox::currentIndexChanged, this,
          [this](int index) { setCurtainLayout((CurtainLayout)index); });
  connect(exaggeration_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    if (std::fabs(value - curtain_view_->plot().exaggeration()) > 1.0e-9)
      curtain_view_->setExaggeration(value);
  });
  connect(fit, &QPushButton::clicked, this, [this]() { curtain_view_->fit(); });
  connect(one_to_one, &QPushButton::clicked, this, [this]() { exaggeration_->setValue(1.0); });
  connect(save_curtain, &QPushButton::clicked, this, [this]() {
    const QString path = QFileDialog::getSaveFileName(this, "Save Curtain", QFileInfo(file_label_).completeBaseName() +
                                                      "_curtain.png", "PNG (*.png);;JPEG (*.jpg)");
    if (!path.isEmpty() && !saveCurtainImage(path))
      showStatus("Cannot write " + path, 5000);
  });
  curtain_timer_ = new QTimer(this);
  curtain_timer_->setSingleShot(true);
  curtain_timer_->setInterval(150);
  connect(curtain_timer_, &QTimer::timeout, this, &WaterColumnWidget::startCurtain);

  for (QDoubleSpinBox *box : {dmin_, dmax_, amin_, amax_, xmax_})
    connect(box, &QDoubleSpinBox::valueChanged, this, &WaterColumnWidget::updatePlotSettings);
  connect(replay_, &QPushButton::toggled, this, &WaterColumnWidget::toggleReplay);
  connect(replay_rate_, &QDoubleSpinBox::valueChanged, this, &WaterColumnWidget::updateReplayInterval);
  replay_timer_ = new QTimer(this);
  connect(replay_timer_, &QTimer::timeout, this, &WaterColumnWidget::replayTick);

  /* plot */
  view_ = new WcdView;
  connect(view_, &WcdView::clicked, this, &WaterColumnWidget::onClicked);
  connect(view_, &WcdView::lassoChanged, this, &WaterColumnWidget::onLassoChanged);
  curtain_view_ = new CurtainView;
  connect(curtain_view_, &CurtainView::pingClicked, this, &WaterColumnWidget::onCurtainPing);
  connect(curtain_view_, &CurtainView::lassoChanged, this, &WaterColumnWidget::onLassoChanged);
  connect(curtain_view_, &CurtainView::exaggerationChanged, this, [this](double value) {
    exaggeration_->blockSignals(true);
    exaggeration_->setValue(value);
    exaggeration_->blockSignals(false);
  });
  splitter_ = new QSplitter(Qt::Vertical);
  splitter_->setChildrenCollapsible(false);
  splitter_->addWidget(view_);
  splitter_->addWidget(curtain_view_);
  splitter_->setStretchFactor(0, 3);
  splitter_->setStretchFactor(1, 2);
  layout->addWidget(splitter_, 1);

  /* ping slider */
  auto *slider_row = new QHBoxLayout;
  slider_row->addWidget(new QLabel("Ping:"));
  slider_ = new OneStepWheelSlider(Qt::Horizontal);
  connect(slider_, &QSlider::valueChanged, this, &WaterColumnWidget::onSlider);
  slider_row->addWidget(slider_);
  step_label_ = new QLabel("0");
  slider_row->addWidget(step_label_);
  layout->addLayout(slider_row);

  if (panel) {
    /* one line for the ping and for the messages of the tool; the host can show the messages too */
    file_name_->setParent(central_);
    file_name_->hide();
    auto *status = new QLabel;
    status->setObjectName("panel_status");
    for (QLabel *label : {meta_, status})
      label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *info = new QHBoxLayout;
    info->addWidget(meta_, 3);
    info->addWidget(status, 2);
    layout->addLayout(info);
    connect(this, &WaterColumnWidget::statusMessage, status, [status](const QString &message, int timeout_ms) {
      status->setText(message);
      status->setToolTip(message);
      if (timeout_ms > 0)
        QTimer::singleShot(timeout_ms, status, [status, message] {
          if (status->text() == message)
            status->clear();
        });
    });
    view_->setMinimumSize(200, 150);
    view_->setFocusPolicy(Qt::ClickFocus);
    setFocusProxy(view_);
  }
  showFiles();

  /* the panel is a part of another program's window: no curtain until asked for */
  if (panel)
    curtain_layout_->setCurrentIndex((int)CurtainLayout::Off);
}

WaterColumnWidget::~WaterColumnWidget() {
  /* the threads write to data held by their lambdas and post to this object */
  if (worker_ != nullptr)
    worker_->wait();
  stopCurtain();
}

void WaterColumnWidget::waitUntilIdle() {
  while (worker_ != nullptr || curtain_worker_ != nullptr || curtain_timer_->isActive())
    QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
}

QString WaterColumnWidget::title() const { return "mbwcdview - water column through MB-System MBIO"; }

void WaterColumnWidget::showStatus(const QString &message, int timeout_ms) { emit statusMessage(message, timeout_ms); }

/*--------------------------------------------------------------------*/
void WaterColumnWidget::openDialog() {
  const QString path = QFileDialog::getOpenFileName(this, "Open .kmwcd/.kmall/.wcd/.all/.s7k or datalist", "",
                                                    "Swath files and datalists (*.kmwcd *.kmall *.wcd *.all *.s7k *.mb-1);;"
                                                    "Datalists (*.mb-1);;All files (*)");
  if (!path.isEmpty())
    openFile(path);
}

bool WaterColumnWidget::openFile(const QString &path) {
  if (worker_ != nullptr)
    return false;
  if (exporting()) {
    showStatus("A 3D export is running", 3000);
    return false;
  }
  const std::string file = path.toStdString();
  if (is_datalist(file)) {
    std::vector<std::pair<std::string, int>> files;
    std::string message;
    if (!read_datalist(file, files, &message) || files.empty()) {
      showStatus(QString("Cannot read datalist %1: %2").arg(QFileInfo(path).fileName(), QString::fromStdString(message)));
      emit fileOpenFailed(path);
      return true;
    }
    files_ = std::move(files);
    list_label_ = QFileInfo(path).fileName();
    return openListedFile(0);
  }
  files_.clear();
  file_index_ = -1;
  list_label_.clear();
  return loadFile(path, 0);
}

bool WaterColumnWidget::openListedFile(int index) {
  if (worker_ != nullptr || index < 0 || index >= (int)files_.size())
    return false;
  file_index_ = index;
  return loadFile(QString::fromStdString(files_[index].first), files_[index].second);
}

void WaterColumnWidget::showFiles() {
  const bool list = !files_.empty();
  previous_file_->setVisible(list);
  next_file_->setVisible(list);
  file_count_->setVisible(list);
  previous_file_->setEnabled(list && file_index_ > 0);
  next_file_->setEnabled(list && file_index_ + 1 < (int)files_.size());
  file_count_->setText(list ? QString("File %1 / %2").arg(file_index_ + 1).arg(files_.size()) : QString());
  file_count_->setToolTip(list_label_);
}

bool WaterColumnWidget::loadFile(const QString &path, int format) {
  if (worker_ != nullptr)
    return false;
  replay_->setChecked(false);
  stopCurtain();
  curtain_view_->plot().setCurtain(nullptr);
  curtain_view_->plot().setMessage("Loading...");
  curtain_view_->update();
  showFiles();
  file_label_ = QFileInfo(path).fileName();
  file_name_->setText(file_label_);
  showStatus("Reading survey file (navigation, bathymetry)...");
  line_ = wcd::Line();
  steps_.clear();
  reported_step_ = -1;

  struct Load {
    wcd::Line line;
    std::string message;
    bool ok = false;
  };
  auto load = std::make_shared<Load>();
  const std::string file = path.toStdString();
  worker_ = QThread::create([this, load, file, format] {
    int last = -1;
    const wcd::Progress progress = [this, &last](int npings, double fraction) {
      const int pct = (int)(100.0 * fraction);
      if (pct != last) {
        last = pct;
        QMetaObject::invokeMethod(
            this, [this, pct, npings] { showStatus(QString("Loading pings... %1% (%2 pings)").arg(pct).arg(npings)); }, Qt::QueuedConnection);
      }
    };
    load->ok = load->line.load(file, progress, &load->message, format);
  });
  worker_->setParent(this);
  connect(worker_, &QThread::finished, this, [this, load, path] {
    worker_->deleteLater();
    worker_ = nullptr;
    central_->setEnabled(true);
    /* disabling the controls took the focus away, and the keys need it */
    if (job_had_focus_ || QApplication::focusWidget() == nullptr)
      setFocus();
    finishOpen(path, std::move(load->line), load->ok, load->message);
    emit busyChanged(false);
  });
  const QWidget *focus = QApplication::focusWidget();
  job_had_focus_ = focus != nullptr && (focus == this || isAncestorOf(focus));
  central_->setEnabled(false);
  worker_->start();
  emit busyChanged(true);
  return true;
}

void WaterColumnWidget::finishOpen(const QString &path, Line line, bool ok, const std::string &message) {
  line_ = std::move(line);
  const int ping = pending_ping_;
  pending_ping_ = -1;
  if (!ok || !line_.has_wc) {
    showStatus(line_.has_survey ? "No water column data found."
                                : QString("Cannot read %1: %2").arg(file_label_, QString::fromStdString(message)));
    view_->plot().setData(wcd::PlotData());
    view_->redraw();
    curtain_view_->plot().setMessage("No water column data");
    curtain_view_->update();
    showFiles();
    emit fileOpenFailed(path);
    return;
  }
  emit diagnostic(QString("survey=%1 water column=%2").arg(QString::fromStdString(line_.kmall), QString::fromStdString(line_.kmwcd)));
  for (size_t i = 0; i < line_.wc.size(); i++)
    for (const int s : line_.wc.swaths(i))
      steps_.emplace_back((int)i, s);

  /* file wide ranges */
  const wcd::WaterColumnFile &wc = line_.wc;
  for (QDoubleSpinBox *box : {amin_, amax_, dmin_, dmax_, xmax_})
    box->blockSignals(true);
  amin_->setRange(wc.amp_min, wc.amp_max);
  amax_->setRange(wc.amp_min, wc.amp_max);
  amin_->setValue(wc.amp_min);
  amax_->setValue(wc.amp_max);
  double draft_max = 0.0;
  for (const double d : line_.survey.sensordepth)
    draft_max = std::max(draft_max, d);
  const double depth_max = wc.range_max_m + draft_max;
  dmin_->setRange(0, depth_max);
  dmax_->setRange(0, depth_max);
  dmin_->setValue(line_.default_depth_min());
  dmax_->setValue(depth_max);
  xmax_->setRange(0, wc.range_max_m > 0 ? 2.0 * wc.range_max_m : 1.0e5);
  xmax_->setValue(0);
  for (QDoubleSpinBox *box : {amin_, amax_, dmin_, dmax_, xmax_})
    box->blockSignals(false);
  const bool has_svp = line_.svp(0.0) != nullptr;
  raytrace_->blockSignals(true);
  raytrace_->setEnabled(has_svp);
  raytrace_->setChecked(has_svp);
  raytrace_->blockSignals(false);

  slider_->blockSignals(true);
  slider_->setRange(0, (int)steps_.size() - 1);
  slider_->setValue(0);
  slider_->blockSignals(false);
  current_ = 0;

  /* the beam range: kept from the file before where this file has as many beams, else all */
  const int beams = std::max(1, beams_per_swath_max(wc));
  const bool keep = first_beam_->maximum() == beams - 1 && first_beam_->maximum() > 0;
  for (QSpinBox *box : {first_beam_, last_beam_}) {
    box->blockSignals(true);
    box->setRange(0, beams - 1);
  }
  if (!keep) {
    first_beam_->setValue(0);
    last_beam_->setValue(beams - 1);
  }
  for (QSpinBox *box : {first_beam_, last_beam_})
    box->blockSignals(false);
  beam_slider_->setRange(0, beams - 1);
  beam_slider_->setValues(first_beam_->value(), last_beam_->value());
  beam_count_->setText(QString("of 0-%1").arg(beams - 1));
  view_->plot().setBeamRange(first_beam_->value(), last_beam_->value());
  showFiles();

  updatePlotSettings();
  if (!curtain_view_->lasso().empty()) {
    /* along-track distances of another line */
    curtain_view_->lasso().clear();
    onLassoChanged();
  }
  emit fileOpened(path);
  redraw();
  curtain_view_->plot().setMessage("Making the curtain...");
  curtain_first_ = true;
  curtain_stale_ = true;
  startCurtain();

  const QString summary =
      QString("Nav summary: survey pings=%1, position records=%2%3, WC pings=%4, matched=%5, fill value=%6")
          .arg(line_.has_survey ? (int)line_.survey.size() : 0)
          .arg(line_.has_survey ? (int)line_.survey.nav.size() : (int)wc.nav.size())
          .arg(line_.has_survey ? "" : " (from water column file)")
          .arg((int)wc.size())
          .arg(line_.matched())
          .arg(wc.has_fill ? QString::number(wc.fill_value, 'f', 1) : QString("none"));
  emit diagnostic(summary);
  QTimer::singleShot(900, this, [this, summary]() { showStatus(summary, 6000); });
  if (ping >= 0)
    showPing(ping);
}

/*--------------------------------------------------------------------*/
void WaterColumnWidget::updatePlotSettings() {
  wcd::WcdPlot &plot = view_->plot();
  plot.setColorRange(amin_->value(), amax_->value());
  plot.setDepthWindow(dmin_->value(), dmax_->value());
  plot.setAcrossMax(xmax_->value());
  view_->redraw();
  CurtainPlot &curtain = curtain_view_->plot();
  curtain.setColorRange(amin_->value(), amax_->value());
  curtain.setDepthWindow(dmin_->value(), dmax_->value());
  curtain_view_->followCurrent();
}

void WaterColumnWidget::redraw() {
  if (steps_.empty())
    return;
  current_ = std::min(std::max(current_, 0), (int)steps_.size() - 1);
  const auto [ping, swath] = steps_[current_];
  wcd::SceneOptions options;
  options.raytrace = raytrace_->isChecked();
  options.bottom = bottom_->isChecked();
  options.wc_bottom = wc_bottom_->isChecked();
  wcd::PlotData data;
  wcd::SceneInfo info;
  wcd::build_scene(line_, ping, swath, options, data, info);
  view_->plot().setData(std::move(data));
  view_->redraw();
  curtain_view_->plot().setCurrent(ping);
  curtain_view_->plot().setLabel(QString("ping %1").arg(line_.wc.pings[ping].ping_number));
  curtain_view_->followCurrent();
  updateBeamAngles();
  meta_->setText(wcd::metadata_text(line_, ping, swath));
  if (presentation_ == Presentation::Panel)
    meta_->setToolTip(file_label_ + "\n" + meta_->text());
  step_label_->setText(QString("%1 / %2").arg(current_).arg((int)steps_.size() - 1));
  if (!info.status.isEmpty())
    showStatus(info.status);
  if (current_ != reported_step_) {
    reported_step_ = current_;
    emit pingChanged(current_, ping, line_.wc.pings[ping].ping_number, swath, line_.wc.pings[ping].time_d);
  }
}

void WaterColumnWidget::setStep(int step) { slider_->setValue(step); }

void WaterColumnWidget::showPing(int ping) {
  if (worker_ != nullptr) {
    pending_ping_ = ping;
    return;
  }
  for (size_t k = 0; k < steps_.size(); k++) {
    if (steps_[k].first >= ping) {
      slider_->setValue((int)k);
      return;
    }
  }
}

bool WaterColumnWidget::showTime(double time_d) {
  if (steps_.empty())
    return false;
  const auto time_of = [this](size_t k) { return line_.wc.pings[steps_[k].first].time_d; };
  if (time_d < time_of(0) || time_d > time_of(steps_.size() - 1))
    return false;
  size_t best = 0;
  for (size_t k = 0; k < steps_.size(); k++)
    if (std::abs(time_of(k) - time_d) < std::abs(time_of(best) - time_d))
      best = k;
  slider_->setValue((int)best);
  return true;
}

void WaterColumnWidget::onSlider(int value) {
  current_ = value;
  redraw();
}

void WaterColumnWidget::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Left && current_ > 0)
    slider_->setValue(current_ - 1);
  else if (event->key() == Qt::Key_Right && current_ < slider_->maximum())
    slider_->setValue(current_ + 1);
  else if (event->key() == Qt::Key_PageUp && !files_.empty())
    openListedFile(file_index_ - 1);
  else if (event->key() == Qt::Key_PageDown && !files_.empty())
    openListedFile(file_index_ + 1);
  else
    QWidget::keyPressEvent(event);
}

/* the pick table is a window of its own: it goes and comes with the panel, and a hidden panel does not replay */
void WaterColumnWidget::hideEvent(QHideEvent *event) {
  if (presentation_ == Presentation::Panel && !event->spontaneous()) {
    replay_->setChecked(false);
    hidden_dialogs_.clear();
    for (QDialog *dialog : findChildren<QDialog *>(QString(), Qt::FindDirectChildrenOnly)) {
      if (dialog->isVisible()) {
        hidden_dialogs_.push_back(dialog);
        dialog->hide();
      }
    }
  }
  QWidget::hideEvent(event);
}

void WaterColumnWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  if (!event->spontaneous()) {
    for (QDialog *dialog : hidden_dialogs_)
      dialog->show();
    hidden_dialogs_.clear();
  }
}

/*--------------------------------------------------------------------*/
void WaterColumnWidget::toggleReplay(bool on) {
  if (on && !steps_.empty()) {
    updateReplayInterval();
    replay_timer_->start();
  }
  else {
    replay_timer_->stop();
    replay_->setText("Replay");
  }
}

void WaterColumnWidget::updateReplayInterval() {
  const double pps = std::max(replay_rate_->value(), 0.1);
  replay_timer_->setInterval(std::max(1, (int)(1000.0 / pps)));
  if (replay_timer_->isActive() || replay_->isChecked())
    replay_->setText(QString("Stop (%1 p/s)").arg(pps, 0, 'f', 1));
}

void WaterColumnWidget::replayTick() {
  if (steps_.empty()) {
    replay_->setChecked(false);
    return;
  }
  slider_->setValue((current_ + 1) % (int)steps_.size());
}

/*--------------------------------------------------------------------*/
bool WaterColumnWidget::saveImage(const QString &path) {
  if (steps_.empty())
    return false;
  const QImage image = view_->plot().render(view_->size(), 2.0, file_label_ + " | " + meta_->text());
  return image.save(path);
}

bool WaterColumnWidget::saveCurtainImage(const QString &path) {
  if (curtain() == nullptr)
    return false;
  const QSize size = curtain_view_->isVisible() ? curtain_view_->size() : QSize(1400, 500);
  const QImage image = curtain_view_->plot().render(size, 2.0, file_label_);
  return image.save(path);
}

void WaterColumnWidget::saveImageDialog() {
  const QString path = QFileDialog::getSaveFileName(this, "Save Image", file_label_, "PNG (*.png);;JPEG (*.jpg)");
  if (!path.isEmpty() && !saveImage(path))
    showStatus("Cannot write " + path, 5000);
}

/*--------------------------------------------------------------------*/
void WaterColumnWidget::setLassoMode(bool on) {
  if (lasso_button_->isChecked() != on) {
    lasso_button_->blockSignals(true);
    lasso_button_->setChecked(on);
    lasso_button_->blockSignals(false);
  }
  view_->setLassoMode(on);
  curtain_view_->setLassoMode(on);
}

bool WaterColumnWidget::lassoMode() const { return lasso_button_->isChecked(); }

Lasso WaterColumnWidget::lasso() const {
  Lasso lasso;
  for (const bool curtain : {false, true}) {
    const LassoEditor &editor = curtain ? curtain_view_->lasso() : view_->lasso();
    for (const QPolygonF &polygon : editor.polygons()) {
      LassoPolygon p;
      p.curtain = curtain;
      for (const QPointF &v : polygon) {
        p.x.push_back(v.x());
        p.z.push_back(v.y());
      }
      lasso.polygons.push_back(std::move(p));
    }
  }
  return lasso;
}

void WaterColumnWidget::setLasso(const Lasso &lasso) {
  std::vector<QPolygonF> fan, curtain;
  for (const LassoPolygon &p : lasso.polygons) {
    QPolygonF polygon;
    for (size_t i = 0; i < p.x.size() && i < p.z.size(); i++)
      polygon << QPointF(p.x[i], p.z[i]);
    (p.curtain ? curtain : fan).push_back(polygon);
  }
  view_->lasso().setPolygons(std::move(fan));
  curtain_view_->lasso().setPolygons(std::move(curtain));
  onLassoChanged();
}

void WaterColumnWidget::clearLasso() { setLasso(Lasso()); }

void WaterColumnWidget::onLassoChanged() {
  view_->redraw();
  curtain_view_->update();
  const size_t nfan = view_->lasso().polygons().size(), ncurtain = curtain_view_->lasso().polygons().size();
  showStatus(nfan + ncurtain == 0 ? QString("No lasso")
                                  : QString("Lasso: %1 polygon(s) in the swath view, %2 in the curtain")
                                        .arg(nfan)
                                        .arg(ncurtain),
             4000);
  emit lassoChanged();
}

bool WaterColumnWidget::saveLasso(const QString &path, QString *message) const {
  std::string why;
  const bool ok = write_lasso(path.toStdString(), lasso(), &why);
  if (!ok && message != nullptr)
    *message = QString::fromStdString(why);
  return ok;
}

bool WaterColumnWidget::loadLasso(const QString &path, QString *message) {
  Lasso read;
  std::string why;
  if (!read_lasso(path.toStdString(), read, &why)) {
    if (message != nullptr)
      *message = QString::fromStdString(why);
    return false;
  }
  setLasso(read);
  return true;
}

void WaterColumnWidget::saveLassoDialog() {
  const QString path = QFileDialog::getSaveFileName(this, "Save Lasso", QFileInfo(file_label_).completeBaseName() +
                                                    ".lasso", "Lasso (*.lasso);;All files (*)");
  QString message;
  if (!path.isEmpty() && !saveLasso(path, &message))
    showStatus(message, 5000);
}

void WaterColumnWidget::loadLassoDialog() {
  const QString path = QFileDialog::getOpenFileName(this, "Load Lasso", QString(), "Lasso (*.lasso);;All files (*)");
  QString message;
  if (!path.isEmpty() && !loadLasso(path, &message))
    showStatus(message, 5000);
}

bool WaterColumnWidget::exporting() const { return export3d_dialog_ != nullptr && export3d_dialog_->running(); }

Export3dDialog *WaterColumnWidget::showExport3dDialog() {
  if (!loaded() || current_ < 0 || current_ >= (int)steps_.size()) {
    showStatus("No file to export", 3000);
    return nullptr;
  }
  if (export3d_dialog_ == nullptr) {
    export3d_dialog_ = new Export3dDialog(this);
    connect(export3d_dialog_, &Export3dDialog::exported, this, [this](const QStringList &files) {
      QStringList names;
      for (const QString &f : files)
        names << QFileInfo(f).fileName();
      showStatus("Exported " + names.join(", "), 8000);
    });
  }
  if (!export3d_dialog_->running()) {
    Export3dContext c;
    c.line = &line_;
    c.current_ping = steps_[(size_t)current_].first;
    Export3dOptions &o = c.base;
    o.beams.first_beam = first_beam_->value();
    o.beams.last_beam = last_beam_->value();
    o.beams.raytrace = raytrace_->isChecked();
    o.combine_max = combine_->currentIndex() == 1;
    o.color_min = amin_->value();
    o.color_max = amax_->value();
    const Palette palette = view_->plot().palette();
    for (int i = 0; i < 256; i++) {
      const QRgb rgb = palette_color(palette, i);
      o.colors[(size_t)i * 3] = (uint8_t)qRed(rgb);
      o.colors[(size_t)i * 3 + 1] = (uint8_t)qGreen(rgb);
      o.colors[(size_t)i * 3 + 2] = (uint8_t)qBlue(rgb);
    }
    o.lasso = lasso();
    /* the window's curtain when it is made with these beams and is not about to be made again */
    const std::shared_ptr<const Curtain> curtain = curtain_view_->plot().sharedCurtain();
    if (curtain != nullptr && curtain_worker_ == nullptr && !curtain_timer_->isActive() && !curtain_stale_ &&
        curtain->first_beam == o.beams.first_beam && curtain->last_beam == o.beams.last_beam)
      c.curtain = curtain;
    const QString path = QString::fromStdString(line_.kmall.empty() ? line_.kmwcd : line_.kmall);
    const QFileInfo info(path);
    c.default_name = info.dir().filePath(info.completeBaseName());
    c.settings = QString("Beams %1 to %2 of each swath, %3 rays, curtain %4, %5 palette")
                     .arg(o.beams.first_beam)
                     .arg(o.beams.last_beam)
                     .arg(o.beams.raytrace ? "ray traced" : "straight")
                     .arg(o.combine_max ? "max" : "mean")
                     .arg(palette_name(palette));
    export3d_dialog_->setContext(c);
  }
  export3d_dialog_->show();
  export3d_dialog_->raise();
  return export3d_dialog_;
}

void WaterColumnWidget::togglePick(bool on) { view_->setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor); }

PickTable *WaterColumnWidget::takePickTable() {
  pick_table_ = new PickTable(this);
  pick_table_->hide();
  pick_table_taken_ = true;
  connect(pick_table_, &PickTable::closeRequested, this, &WaterColumnWidget::pickTableClosed);
  return pick_table_;
}

void WaterColumnWidget::pickTableClosed() { pick_->setChecked(false); }

void WaterColumnWidget::showPickDialog() {
  if (pick_table_taken_) {
    emit pickTableWanted();
    return;
  }
  if (pick_dialog_ == nullptr) {
    pick_dialog_ = new PickDialog(this);
    pick_table_ = pick_dialog_->table();
    connect(pick_dialog_, &QDialog::finished, this, [this]() { pick_->setChecked(false); });
    const QWidget *beside = presentation_ == Presentation::Panel ? static_cast<const QWidget *>(this) : window();
    pick_dialog_->resize(std::max(600, beside->width()), std::max(240, beside->height() / 3));
    pick_dialog_->setMinimumSize(600, 240);
  }
  pick_dialog_->show();
  if (!pick_shown_once_) {
    pick_dialog_->raise();
    pick_dialog_->activateWindow();
    pick_shown_once_ = true;
  }
}

void WaterColumnWidget::onClicked(double across, double depth) {
  if (pick_->isChecked())
    pick(across, depth);
}

QStringList WaterColumnWidget::pick(double across, double depth) {
  const wcd::PlotData &d = view_->plot().data();
  size_t best = d.across.size();
  double best_d2 = std::numeric_limits<double>::max();
  for (size_t i = 0; i < d.across.size(); i++) {
    if (!std::isfinite(d.amplitude[i]) || !std::isfinite(d.across[i]) || !std::isfinite(d.depth[i]) ||
        d.depth[i] < dmin_->value() || d.depth[i] > dmax_->value() ||
        !view_->plot().inBeamRange((int)(i / (size_t)std::max(1, d.nsamples))))
      continue;
    const double d2 = (d.across[i] - across) * (d.across[i] - across) + (d.depth[i] - depth) * (d.depth[i] - depth);
    if (d2 < best_d2) {
      best_d2 = d2;
      best = i;
    }
  }
  if (best == d.across.size() || steps_.empty()) {
    showStatus("Nothing to pick (no grid).", 3000);
    return QStringList();
  }
  const auto [ping, swath] = steps_[current_];
  const double time_d = line_.wc.pings[ping].time_d;
  const double x = d.across[best], z = d.depth[best];
  double lon, lat, sog, cog;
  line_.nav_at(time_d, lon, lat, sog, cog);
  const double heading = line_.heading_at(time_d);
  double azimuth = std::numeric_limits<double>::quiet_NaN();
  QString source = "None";
  if (std::isfinite(heading)) {
    azimuth = heading;
    source = "Heading";
  }
  else if (std::isfinite(cog)) {
    azimuth = cog;
    source = "COG";
  }
  double pt_lat = std::numeric_limits<double>::quiet_NaN(), pt_lon = pt_lat;
  if (std::isfinite(lat) && std::isfinite(lon)) {
    /* the across-track offset is to starboard of the azimuth */
    const double east = std::isfinite(azimuth) ? x * std::cos(DTR * azimuth) : x;
    const double north = std::isfinite(azimuth) ? -x * std::sin(DTR * azimuth) : 0.0;
    pt_lat = lat + north / 111320.0;
    pt_lon = lon + east / (111320.0 * std::max(1.0e-6, std::cos(DTR * lat)));
  }
  const QStringList row = {QString("%1 (%2/%3)").arg(current_).arg(line_.wc.pings[ping].ping_number).arg(swath),
                           utc_text(time_d),
                           QString::number(x, 'f', 2),
                           QString::number(z, 'f', 2),
                           number_or_empty(lat, 6),
                           number_or_empty(lon, 6),
                           number_or_empty(pt_lat, 6),
                           number_or_empty(pt_lon, 6),
                           source};
  showPickDialog();
  pick_table_->addRow(row);
  emit pickAdded(row);
  return row;
}

/*--------------------------------------------------------------------*/
const Curtain *WaterColumnWidget::curtain() const { return curtain_view_->plot().curtain(); }

void WaterColumnWidget::setBeamRange(int first, int last) {
  const int beams = first_beam_->maximum() + 1;
  if (last < 0 || last >= beams)
    last = beams - 1;
  first = std::min(std::max(first, 0), last);
  for (QSpinBox *box : {first_beam_, last_beam_})
    box->blockSignals(true);
  first_beam_->setValue(first);
  last_beam_->setValue(last);
  for (QSpinBox *box : {first_beam_, last_beam_})
    box->blockSignals(false);
  beam_slider_->setValues(first, last);
  applyBeamRange();
}

void WaterColumnWidget::beamsFromSpinBoxes() {
  /* the first beam never after the last: the other box follows */
  QSpinBox *changed = qobject_cast<QSpinBox *>(sender());
  if (first_beam_->value() > last_beam_->value()) {
    QSpinBox *other = changed == last_beam_ ? first_beam_ : last_beam_;
    other->blockSignals(true);
    other->setValue(changed == last_beam_ ? last_beam_->value() : first_beam_->value());
    other->blockSignals(false);
  }
  beam_slider_->setValues(first_beam_->value(), last_beam_->value());
  applyBeamRange();
}

void WaterColumnWidget::beamsFromSlider(int lower, int upper) {
  for (QSpinBox *box : {first_beam_, last_beam_})
    box->blockSignals(true);
  first_beam_->setValue(lower);
  last_beam_->setValue(upper);
  for (QSpinBox *box : {first_beam_, last_beam_})
    box->blockSignals(false);
  applyBeamRange();
}

void WaterColumnWidget::applyBeamRange() {
  view_->plot().setBeamRange(first_beam_->value(), last_beam_->value());
  view_->redraw();
  updateBeamAngles();
  requestCurtain();
}

/* the pointing angles of the first and last beam of the range in the ping and swath shown */
void WaterColumnWidget::updateBeamAngles() {
  QString text;
  if (!steps_.empty()) {
    const auto [ping, swath] = steps_[current_];
    const WcPing &p = line_.wc.pings[ping];
    std::vector<int> beams;
    for (int b = 0; b < p.nbeams(); b++)
      if (p.swath[b] == swath)
        beams.push_back(b);
    if (!beams.empty()) {
      const int first = std::min(first_beam_->value(), (int)beams.size() - 1);
      const int last = std::min(last_beam_->value(), (int)beams.size() - 1);
      text = QString("(%1%2 ... %3%4)")
                 .arg(p.angle_across[beams[first]], 0, 'f', 1)
                 .arg(QChar(0x00B0))
                 .arg(p.angle_across[beams[last]], 0, 'f', 1)
                 .arg(QChar(0x00B0));
    }
  }
  beam_angles_->setText(text);
  beam_angles_->setToolTip("Pointing angles across track of the first and last beam of the range in the swath shown "
                           "(degrees from vertical, starboard positive)");
}

void WaterColumnWidget::setPalette(Palette palette) {
  if (palette_->currentIndex() != (int)palette) {
    palette_->setCurrentIndex((int)palette);  // comes back here
    return;
  }
  view_->plot().setPalette(palette);
  view_->redraw();
  curtain_view_->plot().setPalette(palette);
  curtain_view_->update();
}

void WaterColumnWidget::setCombineMax(bool max) {
  if (combine_->currentIndex() != (max ? 1 : 0)) {
    combine_->setCurrentIndex(max ? 1 : 0);
    return;
  }
  curtain_view_->plot().setMaxMode(max);
  curtain_view_->update();
}

void WaterColumnWidget::setCurtainExaggeration(double exaggeration) { exaggeration_->setValue(exaggeration); }

void WaterColumnWidget::setCurtainLayout(CurtainLayout layout) {
  if (curtain_layout_->currentIndex() != (int)layout) {
    curtain_layout_->setCurrentIndex((int)layout);
    return;
  }
  curtain_view_->setVisible(layout != CurtainLayout::Off);
  splitter_->setOrientation(layout == CurtainLayout::Beside ? Qt::Horizontal : Qt::Vertical);
  if (layout != CurtainLayout::Off && curtain_stale_)
    startCurtain();
}

/*--------------------------------------------------------------------*/
/* the curtain is made again a moment after the last change, so that dragging the range does not queue work */
void WaterColumnWidget::requestCurtain() {
  curtain_stale_ = true;
  if (loaded() && curtain_view_->isVisibleTo(this))
    curtain_timer_->start();
}

void WaterColumnWidget::startCurtain() {
  if (!loaded() || !curtain_view_->isVisibleTo(this))
    return;
  if (curtain_worker_ != nullptr) {
    curtain_cancel_->store(true);
    curtain_again_ = true;
    return;
  }
  curtain_stale_ = false;
  CurtainOptions options;
  options.first_beam = first_beam_->value();
  options.last_beam = last_beam_->value();
  options.raytrace = raytrace_->isChecked();
  auto cancel = std::make_shared<std::atomic<bool>>(false);
  auto result = std::make_shared<Curtain>();
  auto ok = std::make_shared<bool>(false);
  curtain_cancel_ = cancel;
  const Line *line = &line_;
  curtain_worker_ = QThread::create([line, options, cancel, result, ok] {
    *ok = build_curtain(*line, options, *result, cancel.get());
  });
  curtain_worker_->setParent(this);
  QThread *thread = curtain_worker_;
  connect(thread, &QThread::finished, this, [this, thread, result, ok] {
    thread->deleteLater();
    if (thread != curtain_worker_)
      return;  // stopped
    curtain_worker_ = nullptr;
    if (curtain_again_) {
      curtain_again_ = false;
      startCurtain();
      return;
    }
    if (!*ok)
      return;
    CurtainPlot &plot = curtain_view_->plot();
    plot.setCurtain(result);
    if (curtain_first_) {
      curtain_first_ = false;
      curtain_view_->setExaggeration(exaggeration_->value());
      plot.showStart();
    }
    curtain_view_->followCurrent();
  });
  curtain_worker_->start();
}

/* stops a curtain being made and waits for its thread: before the line it reads changes */
void WaterColumnWidget::stopCurtain() {
  curtain_timer_->stop();
  curtain_again_ = false;
  if (curtain_worker_ != nullptr) {
    curtain_cancel_->store(true);
    curtain_worker_->wait();
    curtain_worker_ = nullptr;  // its finished handler deletes it
    curtain_stale_ = true;
  }
}

void WaterColumnWidget::onCurtainPing(int ping) {
  if (steps_.empty() || steps_[current_].first == ping)
    return;
  for (size_t k = 0; k < steps_.size(); k++) {
    if (steps_[k].first == ping) {
      slider_->setValue((int)k);
      return;
    }
  }
}

}  // namespace wcd
