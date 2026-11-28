/*--------------------------------------------------------------------
 *    The MB-system:  wcd_export3d_dialog.h  28/11/2026
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
/*
 * The Export 3D dialog of mbwcdview: the settings of export_3d() (what, pings,
 * amplitude range, lasso, budget, formats, file names), a count of the points
 * before writing, and the export, both in the background with a progress bar.
 * The beam range, combination, palette and colour range come from the window.
 */

#ifndef MBWCDVIEW_WCD_EXPORT3D_DIALOG_H
#define MBWCDVIEW_WCD_EXPORT3D_DIALOG_H

#include <atomic>
#include <memory>

#include <QDialog>
#include <QString>

#include "wcd_curtain.h"
#include "wcd_data.h"
#include "wcd_export3d.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QThread;
class QWidget;

namespace wcd {

/* what the window gives the dialog */
struct Export3dContext {
  const Line *line = nullptr;
  std::shared_ptr<const Curtain> curtain;  // the window's curtain when made with base.beams, else nullptr
  int current_ping = 0;
  Export3dOptions base;    // beams, combination, colours and colour range, lasso polygons
  QString default_name;    // start of the file names, with directory
  QString settings;        // the window's settings used, for the dialog to show
};

class Export3dDialog : public QDialog {
  Q_OBJECT

 public:
  explicit Export3dDialog(QWidget *parent = nullptr);
  ~Export3dDialog() override;

  /* the window's state; resets the amplitude range and, for another line, the pings and name */
  void setContext(const Export3dContext &context);
  /* false and the reason when the settings do not make an export */
  bool options(Export3dOptions &options, QString *message = nullptr) const;
  bool running() const { return worker_ != nullptr; }
  void waitUntilIdle();
  /* without a question before replacing files (tests) */
  void setConfirmReplace(bool confirm) { confirm_replace_ = confirm; }

  /* the controls, for tests */
  QComboBox *what() const { return what_; }
  QRadioButton *currentPing() const { return current_; }
  QRadioButton *pingRange() const { return range_; }
  QSpinBox *firstPing() const { return first_; }
  QSpinBox *lastPing() const { return last_; }
  QSpinBox *pingStep() const { return step_; }
  QDoubleSpinBox *valueMin() const { return vmin_; }
  QDoubleSpinBox *valueMax() const { return vmax_; }
  QCheckBox *lassoOnly() const { return lasso_only_; }
  QCheckBox *budgetOn() const { return budget_on_; }
  QSpinBox *budget() const { return budget_; }
  QCheckBox *formatVec() const { return vec_; }
  QCheckBox *formatPly() const { return ply_; }
  QCheckBox *formatLas() const { return las_; }
  QLineEdit *name() const { return name_; }
  QLabel *resultLabel() const { return result_; }
  const Export3dResult &lastResult() const { return last_result_; }
  bool lastOk() const { return last_ok_; }

 public slots:
  void count();
  void exportFiles();
  void cancel();

 signals:
  void exported(const QStringList &files);

 protected:
  void closeEvent(QCloseEvent *event) override;
  void reject() override;

 private:
  void start(bool write);
  void finished(bool write, bool ok, const Export3dResult &result, const QString &message);
  void updateEnabled();
  void browse();
  QString summary(const Export3dResult &result, bool write) const;

  Export3dContext context_;
  QString context_path_;
  QComboBox *what_;
  QRadioButton *current_, *range_;
  QSpinBox *first_, *last_, *step_;
  QDoubleSpinBox *vmin_, *vmax_;
  QCheckBox *lasso_only_, *budget_on_;
  QSpinBox *budget_;
  QCheckBox *vec_, *ply_, *las_;
  QLineEdit *name_;
  QLabel *settings_, *result_;
  QProgressBar *progress_;
  QPushButton *count_button_, *export_button_, *close_button_;
  QWidget *inputs_;
  QThread *worker_ = nullptr;
  std::shared_ptr<std::atomic<bool>> cancel_;
  Export3dResult last_result_;
  bool last_ok_ = false;
  bool confirm_replace_ = true;
};

}  // namespace wcd

#endif
