/*--------------------------------------------------------------------
 *    The MB-system:  wcd_export3d_dialog.cc  28/11/2026
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

#include "wcd_export3d_dialog.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QThread>
#include <QVBoxLayout>

namespace wcd {

namespace {

QDoubleSpinBox *db_box(double minimum, double maximum, double step) {
  auto *box = new QDoubleSpinBox;
  box->setRange(minimum, maximum);
  box->setSingleStep(step);
  box->setDecimals(1);
  return box;
}

QString number(size_t n) { return QLocale(QLocale::English).toString((qulonglong)n); }

}  // namespace

Export3dDialog::Export3dDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle("Export 3D");
  setWindowModality(Qt::WindowModal);
  auto *outer = new QVBoxLayout(this);
  inputs_ = new QWidget;
  auto *form = new QFormLayout(inputs_);
  form->setContentsMargins(0, 0, 0, 0);

  what_ = new QComboBox;
  what_->addItems({"Fans", "Curtain", "Fans and curtain"});
  what_->setToolTip("The samples of the fans of the pings, the cells of the curtain, or both");
  form->addRow("Export:", what_);

  current_ = new QRadioButton("Current ping");
  range_ = new QRadioButton("Pings");
  current_->setChecked(true);
  first_ = new QSpinBox;
  last_ = new QSpinBox;
  step_ = new QSpinBox;
  step_->setRange(1, 1000000);
  step_->setToolTip("Every n-th ping of the range");
  auto *pings = new QHBoxLayout;
  pings->addWidget(current_);
  pings->addSpacing(12);
  pings->addWidget(range_);
  pings->addWidget(first_);
  pings->addWidget(new QLabel("to"));
  pings->addWidget(last_);
  pings->addWidget(new QLabel("every"));
  pings->addWidget(step_);
  pings->addStretch(1);
  form->addRow("Pings:", pings);

  vmin_ = db_box(-200.0, 200.0, 1.0);
  vmax_ = db_box(-200.0, 200.0, 1.0);
  auto *values = new QHBoxLayout;
  values->addWidget(vmin_);
  values->addWidget(new QLabel("to"));
  values->addWidget(vmax_);
  values->addWidget(new QLabel("dB (also the ends of the colours)"));
  values->addStretch(1);
  form->addRow("Amplitudes:", values);

  lasso_only_ = new QCheckBox("Only the samples in the lasso");
  form->addRow("Lasso:", lasso_only_);

  budget_on_ = new QCheckBox("At most");
  budget_ = new QSpinBox;
  budget_->setRange(1, 2000000000);
  budget_->setValue(2000000);
  budget_->setGroupSeparatorShown(true);
  auto *budget = new QHBoxLayout;
  budget->addWidget(budget_on_);
  budget->addWidget(budget_);
  budget->addWidget(new QLabel("points each for the fans and the curtain, thinned evenly"));
  budget->addStretch(1);
  form->addRow("Points:", budget);

  vec_ = new QCheckBox("mbgrdviz vector (.vec)");
  ply_ = new QCheckBox("PLY (CloudCompare, Blender)");
  las_ = new QCheckBox("LAS 1.4 (QGIS, PDAL)");
  vec_->setChecked(true);
  las_->setChecked(true);
  auto *formats = new QHBoxLayout;
  formats->addWidget(vec_);
  formats->addWidget(ply_);
  formats->addWidget(las_);
  formats->addStretch(1);
  form->addRow("Formats:", formats);

  name_ = new QLineEdit;
  name_->setMinimumWidth(360);
  name_->setToolTip("The start of the file names: <name>_fan.<ext> and <name>_curtain.<ext>");
  auto *browse = new QPushButton("Browse...");
  connect(browse, &QPushButton::clicked, this, &Export3dDialog::browse);
  auto *names = new QHBoxLayout;
  names->addWidget(name_, 1);
  names->addWidget(browse);
  form->addRow("Files:", names);

  settings_ = new QLabel;
  settings_->setWordWrap(true);
  form->addRow("From the window:", settings_);
  outer->addWidget(inputs_);

  result_ = new QLabel("Count shows how many points the settings select.");
  result_->setWordWrap(true);
  result_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  outer->addWidget(result_);
  progress_ = new QProgressBar;
  progress_->setRange(0, 1000);
  progress_->setTextVisible(false);
  progress_->hide();
  outer->addWidget(progress_);

  count_button_ = new QPushButton("Count");
  export_button_ = new QPushButton("Export");
  close_button_ = new QPushButton("Close");
  export_button_->setDefault(true);
  auto *buttons = new QHBoxLayout;
  buttons->addStretch(1);
  buttons->addWidget(count_button_);
  buttons->addWidget(export_button_);
  buttons->addWidget(close_button_);
  outer->addLayout(buttons);
  connect(count_button_, &QPushButton::clicked, this, &Export3dDialog::count);
  connect(export_button_, &QPushButton::clicked, this, &Export3dDialog::exportFiles);
  connect(close_button_, &QPushButton::clicked, this, [this]() {
    if (running())
      cancel();
    else
      close();
  });
  for (QRadioButton *b : {current_, range_})
    connect(b, &QRadioButton::toggled, this, &Export3dDialog::updateEnabled);
  connect(budget_on_, &QCheckBox::toggled, this, &Export3dDialog::updateEnabled);
  updateEnabled();
}

Export3dDialog::~Export3dDialog() {
  if (worker_ != nullptr) {
    cancel_->store(true);
    worker_->wait();
  }
}

void Export3dDialog::setContext(const Export3dContext &context) {
  context_ = context;
  const int n = context.line != nullptr ? (int)context.line->wc.size() : 0;
  const QString path = context.line != nullptr ? QString::fromStdString(context.line->kmwcd) : QString();
  for (QSpinBox *box : {first_, last_})
    box->setRange(0, std::max(0, n - 1));
  if (path != context_path_) {
    /* another line */
    context_path_ = path;
    first_->setValue(0);
    last_->setValue(std::max(0, n - 1));
    step_->setValue(1);
    current_->setChecked(true);
    name_->setText(context.default_name);
  }
  current_->setText(QString("Current ping (%1)").arg(context.current_ping));
  vmin_->setValue(context.base.color_min);
  vmax_->setValue(context.base.color_max);
  const size_t npolygons = context.base.lasso.polygons.size();
  lasso_only_->setEnabled(npolygons > 0);
  lasso_only_->setChecked(npolygons > 0);
  lasso_only_->setText(npolygons > 0 ? QString("Only the samples in the lasso (%1 polygon(s))").arg(npolygons)
                                     : QString("Only the samples in the lasso (none drawn)"));
  settings_->setText(context.settings);
  result_->setText("Count shows how many points the settings select.");
  updateEnabled();
}

void Export3dDialog::updateEnabled() {
  const bool idle = !running();
  inputs_->setEnabled(idle);
  for (QSpinBox *box : {first_, last_, step_})
    box->setEnabled(range_->isChecked());
  budget_->setEnabled(budget_on_->isChecked());
  count_button_->setEnabled(idle && context_.line != nullptr);
  export_button_->setEnabled(idle && context_.line != nullptr);
  close_button_->setText(idle ? "Close" : "Cancel");
}

bool Export3dDialog::options(Export3dOptions &o, QString *message) const {
  QString why;
  o = context_.base;
  o.fan = what_->currentIndex() != 1;
  o.curtain = what_->currentIndex() != 0;
  if (range_->isChecked()) {
    o.first_ping = first_->value();
    o.last_ping = last_->value();
    o.ping_step = step_->value();
  }
  else {
    o.first_ping = o.last_ping = context_.current_ping;
    o.ping_step = 1;
  }
  o.value_min = vmin_->value();
  o.value_max = vmax_->value();
  o.color_min = vmin_->value();
  o.color_max = vmax_->value();
  o.lasso_only = lasso_only_->isEnabled() && lasso_only_->isChecked();
  o.budget = budget_on_->isChecked() ? (size_t)budget_->value() : 0;
  o.vec = vec_->isChecked();
  o.ply = ply_->isChecked();
  o.las = las_->isChecked();
  o.prefix = name_->text().trimmed().toStdString();
  o.name.clear();
  if (context_.line == nullptr)
    why = "No file is open";
  else if (o.first_ping > o.last_ping)
    why = "The first ping is after the last";
  else if (!(o.value_min < o.value_max))
    why = "The amplitude range is empty";
  if (!why.isEmpty() && message != nullptr)
    *message = why;
  return why.isEmpty();
}

void Export3dDialog::browse() {
  const QString start = name_->text().isEmpty() ? QString() : name_->text() + "_fan.las";
  QString path = QFileDialog::getSaveFileName(this, "Export 3D: file names", start,
                                              "LAS (*.las);;PLY (*.ply);;mbgrdviz vector (*.vec);;All files (*)",
                                              nullptr, QFileDialog::DontConfirmOverwrite);
  if (path.isEmpty())
    return;
  /* the start of the names: without the extension and a _fan or _curtain ending */
  const QFileInfo info(path);
  QString base = info.suffix().isEmpty() ? path : path.left(path.size() - info.suffix().size() - 1);
  for (const QString &end : {QString("_fan"), QString("_curtain")})
    if (base.endsWith(end))
      base.chop(end.size());
  name_->setText(base);
}

void Export3dDialog::count() { start(false); }

void Export3dDialog::exportFiles() { start(true); }

void Export3dDialog::cancel() {
  if (cancel_ != nullptr)
    cancel_->store(true);
}

void Export3dDialog::start(bool write) {
  if (running())
    return;
  Export3dOptions o;
  QString message;
  if (!options(o, &message)) {
    result_->setText(message);
    return;
  }
  if (write) {
    if (o.prefix.empty()) {
      result_->setText("No file names given");
      return;
    }
    if (!o.vec && !o.ply && !o.las) {
      result_->setText("No format chosen");
      return;
    }
    const QFileInfo dir(QFileInfo(QString::fromStdString(o.prefix)).absolutePath());
    if (!dir.isDir() || !dir.isWritable()) {
      result_->setText("Cannot write in " + dir.filePath());
      return;
    }
    QStringList existing;
    for (const QString &kind : {QString("fan"), QString("curtain")}) {
      if ((kind == "fan" && !o.fan) || (kind == "curtain" && !o.curtain))
        continue;
      for (const auto &format : {std::make_pair(o.vec, "vec"), std::make_pair(o.ply, "ply"), std::make_pair(o.las, "las")})
        if (format.first) {
          const QString f = QString::fromStdString(o.prefix) + "_" + kind + "." + format.second;
          if (QFileInfo::exists(f))
            existing << QFileInfo(f).fileName();
        }
    }
    if (!existing.isEmpty() && confirm_replace_ &&
        QMessageBox::question(this, "Export 3D", "Replace " + existing.join(", ") + "?") != QMessageBox::Yes)
      return;
  }
  const Line *line = context_.line;
  const std::shared_ptr<const Curtain> curtain = context_.curtain;
  auto cancel = std::make_shared<std::atomic<bool>>(false);
  cancel_ = cancel;
  struct Job {
    Export3dResult result;
    std::string message;
    bool ok = false;
  };
  auto job = std::make_shared<Job>();
  worker_ = QThread::create([this, line, curtain, o, cancel, job, write] {
    int last = -1;
    const Export3dProgress progress = [this, &last](double fraction) {
      const int value = (int)std::lround(1000.0 * fraction);
      if (value != last) {
        last = value;
        QMetaObject::invokeMethod(this, [this, value] { progress_->setValue(value); }, Qt::QueuedConnection);
      }
    };
    job->ok = write ? export_3d(*line, o, job->result, curtain.get(), cancel.get(), progress, &job->message)
                    : count_3d(*line, o, job->result, curtain.get(), cancel.get(), progress, &job->message);
    if (!job->ok && cancel->load())
      job->message = "Cancelled";
  });
  worker_->setParent(this);
  QThread *thread = worker_;
  connect(thread, &QThread::finished, this, [this, thread, job, write] {
    thread->deleteLater();
    worker_ = nullptr;
    finished(write, job->ok, job->result, QString::fromStdString(job->message));
  });
  progress_->setValue(0);
  progress_->show();
  result_->setText(write ? "Exporting..." : "Counting...");
  worker_->start();
  updateEnabled();
}

QString Export3dDialog::summary(const Export3dResult &r, bool write) const {
  QStringList lines;
  for (int object = 0; object < 2; object++) {
    const bool fan = object == 0;
    if ((fan && what_->currentIndex() == 1) || (!fan && what_->currentIndex() == 0))
      continue;
    const Export3dCount &c = fan ? r.fan : r.curtain;
    QString line = QString("%1: %2 points of %3 ping(s)").arg(fan ? "Fans" : "Curtain").arg(number(c.points)).arg(c.pings);
    if (c.points > 0)
      line += QString(", %1 to %2 dB").arg(c.value_min, 0, 'f', 1).arg(c.value_max, 0, 'f', 1);
    if (c.stride > 1)
      line += QString("; %1 %2 (every %3.)").arg(write ? "written" : "to write").arg(number(c.written)).arg(c.stride);
    if (c.points == 0)
      line += write ? " - nothing written" : " - nothing to write";
    lines << line;
  }
  if (write && !r.files.empty()) {
    QStringList names;
    for (const std::string &f : r.files)
      names << QFileInfo(QString::fromStdString(f)).fileName();
    lines << "Written: " + names.join(", ") + (r.crs.empty() ? QString() : " (" + QString::fromStdString(r.crs) + ")");
  }
  return lines.join("\n");
}

void Export3dDialog::finished(bool write, bool ok, const Export3dResult &result, const QString &message) {
  progress_->hide();
  last_result_ = result;
  last_ok_ = ok;
  result_->setText(ok ? summary(result, write) : (write ? "Export failed: " : "Count failed: ") + message);
  updateEnabled();
  if (ok && write) {
    QStringList files;
    for (const std::string &f : result.files)
      files << QString::fromStdString(f);
    emit exported(files);
  }
}

void Export3dDialog::waitUntilIdle() {
  while (worker_ != nullptr) {
    worker_->wait(10);
    QApplication::processEvents(QEventLoop::AllEvents, 10);
  }
}

void Export3dDialog::closeEvent(QCloseEvent *event) {
  if (running()) {
    cancel();
    waitUntilIdle();
  }
  QDialog::closeEvent(event);
}

void Export3dDialog::reject() {
  if (running()) {
    cancel();
    waitUntilIdle();
  }
  QDialog::reject();
}

}  // namespace wcd
