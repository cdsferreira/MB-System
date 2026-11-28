/*--------------------------------------------------------------------
 *    The MB-system:  wcd_widget.h  1/11/2026
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

#ifndef MBWCDVIEW_WCD_WIDGET_H
#define MBWCDVIEW_WCD_WIDGET_H

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <QDialog>
#include <QList>
#include <QSlider>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "wcd_curtain.h"
#include "wcd_data.h"
#include "wcd_export3d.h"
#include "wcd_palette.h"
#include "wcd_scene.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QSplitter;
class QTableWidget;
class QThread;
class QTimer;
class QToolButton;

namespace wcd {

class CurtainView;
class WcdView;

/* a slider with two handles, for a range of values */
class RangeSlider : public QWidget {
  Q_OBJECT
 public:
  explicit RangeSlider(QWidget *parent = nullptr);
  void setRange(int minimum, int maximum);
  void setValues(int lower, int upper);  // no signal
  int lower() const { return lower_; }
  int upper() const { return upper_; }
  QSize sizeHint() const override { return QSize(200, 22); }
  QSize minimumSizeHint() const override { return QSize(60, 22); }

 signals:
  void valuesChanged(int lower, int upper);

 protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

 private:
  double toX(int value) const;
  int toValue(double x) const;
  int minimum_ = 0, maximum_ = 1, lower_ = 0, upper_ = 1;
  int handle_ = -1;  // 0 lower, 1 upper, -1 none
};

/* slider that moves exactly one step per mouse wheel notch */
class OneStepWheelSlider : public QSlider {
  Q_OBJECT
 public:
  using QSlider::QSlider;

 protected:
  void wheelEvent(QWheelEvent *event) override;
};

/* table of picked positions */
class PickTable : public QWidget {
  Q_OBJECT
 public:
  explicit PickTable(QWidget *parent = nullptr);
  void addRow(const QStringList &values);
  bool saveText(const QString &path);
  static const QStringList columns;

 signals:
  void closeRequested();

 private:
  void saveDialog();
  void removeSelected();
  void clearAndClose();
  QTableWidget *table_;
};

/* the table as a modeless window, when the program around the tool does not take it */
class PickDialog : public QDialog {
  Q_OBJECT
 public:
  explicit PickDialog(QWidget *parent = nullptr);
  PickTable *table() { return table_; }

 private:
  PickTable *table_;
};

/* Window: the full tool, for the window of the program. Panel: compact, for a part of another program's window. */
enum class Presentation { Window, Panel };

/* where the curtain is: below the swath view, beside it, or not shown */
enum class CurtainLayout { Below, Beside, Off };

/* The tool: the plot, its controls and the data. WaterColumnWindow puts it in the window of the program. */
class WaterColumnWidget : public QWidget {
  Q_OBJECT

 public:
  explicit WaterColumnWidget(Presentation presentation = Presentation::Window, QWidget *parent = nullptr);
  ~WaterColumnWidget() override;

  /* runs in the background; false when another file is still loading. A datalist opens its
     first file, and Previous/Next file go through the others */
  bool openFile(const QString &path);
  bool openListedFile(int index);  // file index of the datalist
  int fileIndex() const { return file_index_; }
  int fileCount() const { return (int)files_.size(); }  // 0 without a datalist
  bool busy() const { return worker_ != nullptr; }
  bool loaded() const { return !steps_.empty(); }
  void waitUntilIdle();  // also until the curtain is made
  bool showTime(double time_d);  // the ping nearest to this time; false when no file is in or the time is outside of it
  void setStep(int step);
  void showPing(int ping);  // first slider step of water column ping index ping; waits for a file that is loading
  bool saveImage(const QString &path);
  bool saveCurtainImage(const QString &path);
  /* beams first .. last of each swath (last -1: to the last) in the curtain, the others dimmed in the swath view */
  void setBeamRange(int first, int last);
  void setPalette(Palette palette);
  void setCombineMax(bool max);  // the strongest value instead of the mean in the curtain
  void setCurtainLayout(CurtainLayout layout);
  void setCurtainExaggeration(double exaggeration);  // 1: 1:1
  const Curtain *curtain() const;  // nullptr until it is made
  CurtainView *curtainView() const { return curtain_view_; }
  QStringList pick(double across, double depth);  // for tests: pick the cell nearest to a point

  /* the lasso: polygons drawn in the swath view (across track, depth) and in the curtain (along
     track, depth) that choose the samples of a 3D export. In lasso mode the left button draws
     them in both views. Opening a file clears the curtain's polygons, which belong to its track */
  void setLassoMode(bool on);
  bool lassoMode() const;
  Lasso lasso() const;
  void setLasso(const Lasso &lasso);
  void clearLasso();
  bool saveLasso(const QString &path, QString *message = nullptr) const;
  bool loadLasso(const QString &path, QString *message = nullptr);
  QString title() const;

  /* For a window that shows the table of picks itself, for example in a dock. It then gets
     pickTableWanted when the table should be shown, and calls pickTableClosed when the user closes it. */
  PickTable *takePickTable();
  void pickTableClosed();

 signals:
  void statusMessage(const QString &message, int timeout_ms);
  void diagnostic(const QString &message);  // the program writes these to stderr
  void busyChanged(bool busy);
  void fileOpened(const QString &path);
  void fileOpenFailed(const QString &path);
  void pingChanged(int step, int ping, int ping_number, int swath, double time_d);  // ping is the index in the file
  void pickAdded(const QStringList &row);  // the columns of PickTable
  void pickTableWanted();
  void lassoChanged();

 protected:
  void keyPressEvent(QKeyEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void showEvent(QShowEvent *event) override;

 private:
  void showStatus(const QString &message, int timeout_ms = 0);
  void openDialog();
  bool loadFile(const QString &path, int format);
  void finishOpen(const QString &path, Line line, bool ok, const std::string &message);
  void showFiles();
  void beamsFromSpinBoxes();
  void beamsFromSlider(int lower, int upper);
  void applyBeamRange();
  void updateBeamAngles();
  void requestCurtain();
  void startCurtain();
  void stopCurtain();
  void onCurtainPing(int ping);
  void saveImageDialog();
  void redraw();
  void updatePlotSettings();
  void onSlider(int value);
  void onClicked(double across, double depth);
  void togglePick(bool on);
  void showPickDialog();
  void onLassoChanged();
  void saveLassoDialog();
  void loadLassoDialog();
  void toggleReplay(bool on);
  void updateReplayInterval();
  void replayTick();

  wcd::Line line_;
  std::vector<std::pair<std::string, int>> files_;  // the files of a datalist with their formats
  int file_index_ = -1;
  QString list_label_;
  std::vector<std::pair<int, int>> steps_;  // (water column ping, swath) per slider step
  int current_ = 0;
  QString file_label_;
  QThread *worker_ = nullptr;
  bool job_had_focus_ = false;
  int pending_ping_ = -1;
  int reported_step_ = -1;

  Presentation presentation_;
  QWidget *central_;
  QList<QDialog *> hidden_dialogs_;
  WcdView *view_;
  QLabel *file_name_, *meta_, *step_label_;
  QCheckBox *bottom_, *wc_bottom_, *raytrace_, *pick_;
  QToolButton *lasso_button_;
  QDoubleSpinBox *dmin_, *dmax_, *amin_, *amax_, *xmax_, *replay_rate_;
  QPushButton *replay_;
  OneStepWheelSlider *slider_;
  QPushButton *previous_file_, *next_file_;
  QLabel *file_count_;
  QSplitter *splitter_;
  CurtainView *curtain_view_;
  QSpinBox *first_beam_, *last_beam_;
  RangeSlider *beam_slider_;
  QLabel *beam_count_, *beam_angles_;
  QComboBox *combine_, *palette_, *curtain_layout_;
  QDoubleSpinBox *exaggeration_;
  QTimer *curtain_timer_;
  QThread *curtain_worker_ = nullptr;
  std::shared_ptr<std::atomic<bool>> curtain_cancel_;
  bool curtain_again_ = false, curtain_stale_ = false, curtain_first_ = true;
  QTimer *replay_timer_;
  PickTable *pick_table_ = nullptr;
  bool pick_table_taken_ = false;
  PickDialog *pick_dialog_ = nullptr;
  bool pick_shown_once_ = false;
};

}  // namespace wcd

#endif
