/*--------------------------------------------------------------------
 *    The MB-system:  mbwcdview_window_test.cc  9/11/2026
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
 * Drives the window of mbwcdview: opening files, moving between pings with the
 * slider, keys and wheel, the check boxes, picking and replay. It needs no
 * OpenGL: it reads what is drawn from the data of the plot. Arguments: the
 * .kmall file of a line with its .kmwcd beside it, and a directory to write in.
 */

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QRadioButton>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextStream>
#include <QToolButton>
#include <QWheelEvent>

#include <cmath>
#include <cstdio>
#include <vector>

#include "WcdView.h"
#include "wcd_curtain_view.h"
#include "wcd_export3d_dialog.h"
#include "wcd_window.h"

namespace {

int checks = 0, failures = 0;

#define CHECK(condition)                                  \
  do {                                                    \
    checks++;                                             \
    if (!(condition)) {                                   \
      failures++;                                         \
      printf("FAIL line %d: %s\n", __LINE__, #condition); \
    }                                                     \
  } while (0)

void wait(int ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < ms)
    QApplication::processEvents(QEventLoop::AllEvents, 10);
}

QCheckBox *box(QWidget &w, const QString &text) {
  for (QCheckBox *b : w.findChildren<QCheckBox *>())
    if (b->text() == text)
      return b;
  return nullptr;
}

QPushButton *button(QWidget &w, const QString &text) {
  for (QPushButton *b : w.findChildren<QPushButton *>())
    if (b->text() == text)
      return b;
  return nullptr;
}

bool has_label(QWidget &w, const QString &text) {
  for (QLabel *l : w.findChildren<QLabel *>())
    if (l->text() == text)
      return true;
  return false;
}

QString meta_text(QWidget &w) {
  for (QLabel *l : w.findChildren<QLabel *>())
    if (l->wordWrap())
      return l->text();
  return QString();
}

void key(QWidget &w, int code) {
  QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier);
  QApplication::sendEvent(&w, &event);
}

void mouse(QWidget *target, QEvent::Type type, const QPointF &pos, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
  const Qt::MouseButtons buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
  QMouseEvent event(type, pos, target->mapToGlobal(pos), button, buttons, modifiers);
  QApplication::sendEvent(target, &event);
}

/* a left-button drag through the corners, in steps of a few pixels */
void drag(QWidget *target, const std::vector<QPointF> &corners, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  mouse(target, QEvent::MouseButtonPress, corners.front(), modifiers);
  for (size_t i = 1; i < corners.size(); i++)
    for (int k = 1; k <= 10; k++)
      mouse(target, QEvent::MouseMove, corners[i - 1] + (corners[i] - corners[i - 1]) * (k / 10.0), modifiers);
  mouse(target, QEvent::MouseButtonRelease, corners.back(), modifiers);
}

/* corners of a rectangle at fractions fx0 .. fx1, fz0 .. fz1 of a plot area */
std::vector<QPointF> box_corners(const QRectF &r, double fx0, double fx1, double fz0, double fz1) {
  auto at = [&](double fx, double fz) { return QPointF(r.left() + fx * r.width(), r.top() + fz * r.height()); };
  return {at(fx0, fz0), at(fx1, fz0), at(fx1, fz1), at(fx0, fz1)};
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: mbwcdview_window_test file.kmall directory\n");
    return 2;
  }
  QApplication app(argc, argv);
  const QString kmall = argv[1], dir = argv[2];

  wcd::WaterColumnWindow window;
  wcd::WaterColumnWidget &w = *window.editor();
  /* never shown: on a platform without OpenGL, such as offscreen, Qt 6.4 crashes showing a window that holds an OpenGL widget */
  wait(100);
  QStatusBar *status = window.statusBar();
  auto *view = window.findChild<wcd::WcdView *>();
  auto *slider = window.findChild<QSlider *>();
  /* depth, amplitude, across-track, replay rate, then the curtain's vertical exaggeration */
  const auto spins = window.findChildren<QDoubleSpinBox *>();
  CHECK(view != nullptr && slider != nullptr && spins.size() == 7);

  /* the swath is drawn with QRhi where it is built, unless MB_RENDERER=legacy-gl asks for OpenGL */
#if defined(MBWCD_HAVE_RHI) && defined(MBWCD_HAVE_GL)
  CHECK(view->usesRhi() == (qgetenv("MB_RENDERER") != "legacy-gl"));
#elif defined(MBWCD_HAVE_RHI)
  CHECK(view->usesRhi());
#else
  CHECK(!view->usesRhi());
#endif

  /* nothing open */
  CHECK(window.windowTitle().startsWith("mbwcdview"));
  CHECK(status->currentMessage() == "Ready");
  CHECK(has_label(window, "No file loaded"));
  CHECK(!w.busy() && !w.loaded());
  CHECK(!w.saveImage(dir + "/empty.png"));
  CHECK(w.pick(0.0, 100.0).isEmpty());
  CHECK(status->currentMessage().startsWith("Nothing to pick"));

  /* a file that cannot be read */
  CHECK(w.openFile(dir + "/missing.kmall"));
  CHECK(w.busy());
  w.waitUntilIdle();
  CHECK(!w.busy() && !w.loaded());
  CHECK(status->currentMessage().startsWith("Cannot read missing.kmall"));

  /* a line, loaded in the background with the controls off */
  CHECK(w.openFile(kmall));
  CHECK(w.busy());
  CHECK(!slider->isEnabled() && !button(window, "Save Image")->isEnabled());
  CHECK(!w.openFile(kmall));
  w.waitUntilIdle();
  CHECK(!w.busy() && w.loaded());
  CHECK(slider->isEnabled() && button(window, "Save Image")->isEnabled());
  const int last = slider->maximum();
  CHECK(last > 2);
  CHECK(slider->value() == 0);
  const QString name = kmall.mid(kmall.lastIndexOf('/') + 1);
  CHECK(has_label(window, name));
  CHECK(has_label(window, QString("0 / %1").arg(last)));
  CHECK(!view->plot().data().across.empty());
  {
    /* the fill value is no data only before the first echo of a beam: synth_kmall starts every
       beam with 2 fill samples (NFILL) and puts one more at sample 12 of beam 1 (INNER_FILL),
       which is drawn with the lowest colour */
    const wcd::PlotData &d = view->plot().data();
    bool leading = d.nbeams > 1 && d.nsamples > 12;
    for (int b = 0; leading && b < d.nbeams; b++)
      leading = !std::isfinite(d.amplitude[(size_t)b * d.nsamples]) &&
                !std::isfinite(d.amplitude[(size_t)b * d.nsamples + 1]) &&
                std::isfinite(d.amplitude[(size_t)b * d.nsamples + 2]);
    CHECK(leading);
    CHECK(d.nsamples > 12 && std::fabs(d.amplitude[(size_t)d.nsamples + 12] + 94.0f) < 0.01f);
  }
  CHECK(box(window, "Ray trace (SVP)")->isEnabled() && box(window, "Ray trace (SVP)")->isChecked());
  CHECK(spins[2]->value() == spins[2]->minimum() && spins[3]->value() == spins[3]->maximum());
  CHECK(spins[0]->value() == 0.0 && spins[1]->value() > 0.0);
  wait(1100);
  CHECK(status->currentMessage().startsWith("Nav summary"));

  /* between the pings */
  const QString first_meta = meta_text(window);
  CHECK(!first_meta.isEmpty());
  w.setStep(1);
  CHECK(slider->value() == 1);
  CHECK(has_label(window, QString("1 / %1").arg(last)));
  CHECK(meta_text(window) != first_meta);
  key(w, Qt::Key_Right);
  CHECK(slider->value() == 2);
  key(w, Qt::Key_Left);
  key(w, Qt::Key_Left);
  CHECK(slider->value() == 0);
  key(w, Qt::Key_Left);
  CHECK(slider->value() == 0);
  w.setStep(last);
  key(w, Qt::Key_Right);
  CHECK(slider->value() == last);
  w.setStep(0);
  {
    QWheelEvent up(QPointF(5, 5), QPointF(5, 5), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(slider, &up);
    CHECK(slider->value() == 1);
    QWheelEvent down(QPointF(5, 5), QPointF(5, 5), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(slider, &down);
    CHECK(slider->value() == 0);
  }
  w.showPing(2);
  CHECK(slider->value() > 1);

  /* what the check boxes draw */
  w.setStep(0);
  CHECK(view->plot().data().soundings.empty());
  box(window, "Bottom")->click();
  CHECK(!view->plot().data().soundings.empty());
  box(window, "Bottom")->click();
  CHECK(view->plot().data().soundings.empty());
  CHECK(!view->plot().data().show_wc_bottom);
  box(window, "WC bottom")->click();
  CHECK(view->plot().data().show_wc_bottom);
  box(window, "WC bottom")->click();

  /* picking the cell nearest to a point */
  const wcd::PlotData &data = view->plot().data();
  size_t cell = 0;
  while (cell < data.amplitude.size() && !(std::isfinite(data.amplitude[cell]) && std::isfinite(data.across[cell]) && std::isfinite(data.depth[cell])))
    cell++;
  CHECK(cell < data.amplitude.size());
  const double x = data.across[cell], z = data.depth[cell];
  const QStringList row = w.pick(x, z);
  CHECK(row.size() == wcd::PickTable::columns.size());
  CHECK(row.size() > 3 && std::abs(row[2].toDouble() - x) < 0.01 && std::abs(row[3].toDouble() - z) < 0.01);
  /* the table opens in a floating dock */
  auto *dialog = window.findChild<QDockWidget *>();
  CHECK(dialog != nullptr && dialog->isVisible() && dialog->isFloating());
  CHECK(dialog->features() & QDockWidget::DockWidgetMovable);
  auto *pick_table = dialog->findChild<wcd::PickTable *>();
  CHECK(pick_table != nullptr);
  auto *table = dialog->findChild<QTableWidget *>();
  CHECK(table->rowCount() == 1);
  const QString picks = dir + "/picks.txt";
  CHECK(pick_table->saveText(picks));
  {
    QFile file(picks);
    CHECK(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList lines = QTextStream(&file).readAll().split('\n', Qt::SkipEmptyParts);
    CHECK(lines.size() == 2 && lines[0].startsWith("Ping\tUTC"));
  }

  /* a click picks only with Pick on */
  emit view->clicked(x, z);
  CHECK(table->rowCount() == 1);
  box(window, "Pick")->click();
  emit view->clicked(x, z);
  CHECK(table->rowCount() == 2);

  /* nothing to pick outside of the depth window */
  const double dmin = spins[0]->value();
  spins[0]->setValue(spins[0]->maximum());
  CHECK(w.pick(x, z).isEmpty());
  spins[0]->setValue(dmin);

  /* closing the table empties it and stops picking */
  button(*dialog, "Close")->click();
  CHECK(table->rowCount() == 0 && !dialog->isVisible());
  CHECK(!box(window, "Pick")->isChecked());

  /* the close button of the dock stops picking and keeps the rows */
  CHECK(!w.pick(x, z).isEmpty());
  CHECK(dialog->isVisible() && table->rowCount() == 1);
  box(window, "Pick")->click();
  CHECK(box(window, "Pick")->isChecked());
  dialog->close();
  CHECK(!dialog->isVisible() && table->rowCount() == 1);
  CHECK(!box(window, "Pick")->isChecked());

  /* docked, it still takes the picks */
  dialog->setFloating(false);
  CHECK(!dialog->isFloating());
  CHECK(window.dockWidgetArea(dialog) == Qt::BottomDockWidgetArea);
  CHECK(!w.pick(x, z).isEmpty());
  CHECK(table->rowCount() == 2);
  button(*dialog, "Close")->click();
  CHECK(table->rowCount() == 0);

  /* the curtain: one column per water column ping, all beams, at 1:1 */
  {
    const wcd::Curtain *c = w.curtain();
    CHECK(c != nullptr);
    CHECK(c != nullptr && c->ncols > 2 && c->first_beam == 0 && c->last_beam == c->beams_max - 1);
    CHECK(c != nullptr && c->along_is_distance && c->along.back() > c->along.front());
    auto *curtain_view = w.curtainView();
    CHECK(curtain_view->plot().exaggeration() == 1.0 && spins[6]->value() == 1.0);
    const auto beam_boxes = window.findChildren<QSpinBox *>();
    CHECK(beam_boxes.size() == 2 && c != nullptr && beam_boxes[1]->maximum() == c->beams_max - 1);

    /* a range of beams: the curtain is made again, the swath view dims the others */
    w.setBeamRange(4, 11);
    w.waitUntilIdle();
    c = w.curtain();
    CHECK(c != nullptr && c->first_beam == 4 && c->last_beam == 11);
    CHECK(beam_boxes[0]->value() == 4 && beam_boxes[1]->value() == 11);
    CHECK(!view->plot().inBeamRange(3) && view->plot().inBeamRange(4) && view->plot().inBeamRange(11) &&
          !view->plot().inBeamRange(12));
    /* the first beam never after the last */
    beam_boxes[0]->setValue(13);
    w.waitUntilIdle();
    CHECK(beam_boxes[1]->value() == 13 && w.curtain()->first_beam == 13 && w.curtain()->last_beam == 13);
    w.setBeamRange(0, -1);
    w.waitUntilIdle();
    CHECK(w.curtain()->first_beam == 0 && w.curtain()->last_beam == w.curtain()->beams_max - 1);

    /* a click in the curtain shows that ping, the line follows the ping */
    emit curtain_view->pingClicked(2);
    CHECK(slider->value() > 0 && curtain_view->plot().current() == 2);
    w.setStep(0);
    CHECK(curtain_view->plot().current() == 0);

    /* exaggeration, layout, palette and the strongest value */
    spins[6]->setValue(2.5);
    CHECK(curtain_view->plot().exaggeration() == 2.5);
    button(window, "1:1")->click();
    CHECK(curtain_view->plot().exaggeration() == 1.0);
    auto *splitter = window.findChild<QSplitter *>();
    w.setCurtainLayout(wcd::CurtainLayout::Beside);
    CHECK(splitter->orientation() == Qt::Horizontal && !curtain_view->isHidden());
    w.setCurtainLayout(wcd::CurtainLayout::Off);
    CHECK(curtain_view->isHidden());
    w.setCurtainLayout(wcd::CurtainLayout::Below);
    CHECK(splitter->orientation() == Qt::Vertical && !curtain_view->isHidden());
    w.setPalette(wcd::Palette::Haxby);
    CHECK(view->plot().palette() == wcd::Palette::Haxby);
    w.setPalette(wcd::Palette::Viridis);
    w.setCombineMax(true);
    w.setCombineMax(false);
    CHECK(w.saveCurtainImage(dir + "/curtain.png") && QFile::exists(dir + "/curtain.png"));
  }

  /* the lasso: drawn with the left button in both views while the Lasso button is on */
  {
    auto *lasso_button = window.findChild<QToolButton *>("lasso");
    CHECK(lasso_button != nullptr && lasso_button->isCheckable() && !lasso_button->isChecked());
    CHECK(w.lasso().empty());
    QWidget *drawing = view->drawingWidget();
    auto *curtain_view = w.curtainView();
    /* the window is never shown: give the views the sizes a layout would */
    drawing->resize(800, 600);
    curtain_view->resize(800, 400);
    int clicks = 0;
    QObject::connect(view, &wcd::WcdView::clicked, [&clicks](double, double) { clicks++; });
    int changes = 0;
    QObject::connect(&w, &wcd::WaterColumnWidget::lassoChanged, [&changes]() { changes++; });
    const QRectF r = view->plot().plotRect(drawing->size());
    const std::vector<QPointF> square = box_corners(r, 0.3, 0.7, 0.2, 0.6);

    /* without lasso mode a press is a click, and draws nothing */
    drag(drawing, square);
    CHECK(clicks == 1 && w.lasso().empty());

    lasso_button->click();
    CHECK(w.lassoMode() && view->lassoMode() && curtain_view->lassoMode());
    drag(drawing, square);
    CHECK(clicks == 1);
    CHECK(changes == 1);
    wcd::Lasso lasso = w.lasso();
    CHECK(lasso.polygons.size() == 1 && !lasso.polygons[0].curtain && lasso.polygons[0].x.size() >= 25);
    if (lasso.polygons.size() == 1 && !lasso.polygons[0].x.empty()) {
      double across, depth;
      CHECK(view->plot().toData(square[0], drawing->size(), across, depth));
      CHECK(std::fabs(lasso.polygons[0].x[0] - across) < 1.0e-6 && std::fabs(lasso.polygons[0].z[0] - depth) < 1.0e-6);
      /* the centre of the square is in, a point outside it is out */
      double cx, cz, ox, oz;
      view->plot().toData(QPointF(r.left() + 0.5 * r.width(), r.top() + 0.4 * r.height()), drawing->size(), cx, cz);
      view->plot().toData(QPointF(r.left() + 0.9 * r.width(), r.top() + 0.4 * r.height()), drawing->size(), ox, oz);
      CHECK(lasso.contains(false, cx, cz) && !lasso.contains(false, ox, oz) && !lasso.contains(true, cx, cz));
    }
    /* Shift adds a polygon, a plain drag replaces them, a click draws none */
    drag(drawing, box_corners(r, 0.05, 0.2, 0.7, 0.9), Qt::ShiftModifier);
    CHECK(view->lasso().polygons().size() == 2);
    drag(drawing, box_corners(r, 0.05, 0.2, 0.7, 0.9));
    CHECK(view->lasso().polygons().size() == 1);
    drag(drawing, square, Qt::ShiftModifier);
    CHECK(view->lasso().polygons().size() == 2);
    mouse(drawing, QEvent::MouseButtonPress, square[0]);
    mouse(drawing, QEvent::MouseButtonRelease, square[0]);
    CHECK(view->lasso().polygons().size() == 2 && !view->lasso().drawing());
    /* Alt removes the polygon under the pointer, and nothing elsewhere */
    const QPointF centre(r.left() + 0.5 * r.width(), r.top() + 0.4 * r.height());
    mouse(drawing, QEvent::MouseButtonPress, QPointF(r.left() + 0.95 * r.width(), r.top() + 0.05 * r.height()),
          Qt::AltModifier);
    CHECK(view->lasso().polygons().size() == 2);
    mouse(drawing, QEvent::MouseButtonPress, centre, Qt::AltModifier);
    mouse(drawing, QEvent::MouseButtonRelease, centre, Qt::AltModifier);
    CHECK(view->lasso().polygons().size() == 1);
    /* a press outside of the plot area starts nothing */
    drag(drawing, {QPointF(2, 2), QPointF(r.center()), QPointF(r.right(), r.bottom())});
    CHECK(view->lasso().polygons().size() == 1);

    /* the curtain draws instead of moving */
    const double start = curtain_view->plot().start();
    const QRectF cr = curtain_view->plot().plotRect(curtain_view->size());
    drag(curtain_view, box_corners(cr, 0.1, 0.6, 0.1, 0.5));
    CHECK(curtain_view->plot().start() == start);
    lasso = w.lasso();
    CHECK(lasso.polygons.size() == 2 && lasso.has(false) && lasso.has(true));

    /* saved and loaded */
    const QString path = dir + "/window.lasso";
    CHECK(w.saveLasso(path) && QFile::exists(path));
    w.clearLasso();
    CHECK(w.lasso().empty() && view->lasso().empty() && curtain_view->lasso().empty());
    CHECK(w.loadLasso(path));
    const wcd::Lasso loaded = w.lasso();
    CHECK(loaded.polygons.size() == 2);
    for (size_t i = 0; i < loaded.polygons.size() && i < lasso.polygons.size(); i++) {
      CHECK(loaded.polygons[i].curtain == lasso.polygons[i].curtain &&
            loaded.polygons[i].x.size() == lasso.polygons[i].x.size());
      for (size_t j = 0; j < loaded.polygons[i].x.size() && j < lasso.polygons[i].x.size(); j++)
        CHECK(std::fabs(loaded.polygons[i].x[j] - lasso.polygons[i].x[j]) < 6.0e-4 &&
              std::fabs(loaded.polygons[i].z[j] - lasso.polygons[i].z[j]) < 6.0e-4);
    }
    QString message;
    CHECK(!w.loadLasso(dir + "/missing.lasso", &message) && !message.isEmpty());
    CHECK(w.lasso().polygons.size() == 2);

    /* off: presses click again, and the curtain moves */
    lasso_button->click();
    CHECK(!w.lassoMode() && !view->lassoMode() && !curtain_view->lassoMode());
    mouse(drawing, QEvent::MouseButtonPress, centre);
    mouse(drawing, QEvent::MouseButtonRelease, centre);
    CHECK(clicks == 2);
    CHECK(w.lasso().polygons.size() == 2);

    /* another file keeps the swath view's polygons and clears the curtain's, which belong to a track */
    CHECK(w.openFile(kmall));
    w.waitUntilIdle();
    CHECK(view->lasso().polygons().size() == 1 && curtain_view->lasso().empty());
    w.clearLasso();
  }

  /* the Export 3D dialog: counts and writes in the background with the window's settings */
  {
    QWidget *drawing = view->drawingWidget();
    const QRectF r = view->plot().plotRect(drawing->size());
    w.setLassoMode(true);
    drag(drawing, box_corners(r, 0.0, 0.5, 0.0, 1.0));  // the port half of the fans
    w.setLassoMode(false);
    CHECK(w.lasso().polygons.size() == 1);
    wcd::Export3dDialog *d = w.showExport3dDialog();
    CHECK(d != nullptr);
    if (d != nullptr) {
      d->setConfirmReplace(false);
      CHECK(d->lassoOnly()->isEnabled() && d->lassoOnly()->isChecked());
      CHECK(d->name()->text().endsWith("/0001_20250101_120000"));
      CHECK(d->valueMin()->value() == spins[2]->value() && d->valueMax()->value() == spins[3]->value());
      CHECK(d->currentPing()->isChecked() && d->lastPing()->maximum() == 3);
      d->name()->setText(dir + "/dialog");
      d->what()->setCurrentIndex(2);
      d->pingRange()->setChecked(true);
      d->firstPing()->setValue(0);
      d->lastPing()->setValue(3);
      d->lassoOnly()->setChecked(false);
      d->count();
      d->waitUntilIdle();
      CHECK(d->lastOk());
      const size_t all_fan = d->lastResult().fan.points, all_curtain = d->lastResult().curtain.points;
      CHECK(all_fan > 100 && all_curtain > 10 && d->lastResult().fan.pings == 4);
      CHECK(!QFile::exists(dir + "/dialog_fan.vec"));
      CHECK(d->resultLabel()->text().startsWith("Fans: "));
      /* the lasso: about half of the fans, no curtain cells (no polygon there) */
      d->lassoOnly()->setChecked(true);
      d->count();
      d->waitUntilIdle();
      CHECK(d->lastOk() && d->lastResult().fan.points > all_fan / 4 && d->lastResult().fan.points < all_fan * 3 / 4);
      CHECK(d->lastResult().curtain.points == 0);
      /* written in the three formats; the window refuses another file meanwhile */
      d->formatPly()->setChecked(true);
      CHECK(d->formatVec()->isChecked() && d->formatLas()->isChecked());
      d->exportFiles();
      if (d->running())
        CHECK(!w.openFile(kmall));
      d->waitUntilIdle();
      CHECK(d->lastOk() && !w.exporting());
      for (const char *ext : {"vec", "ply", "las"})
        CHECK(QFile::exists(dir + "/dialog_fan." + ext) && !QFile::exists(dir + "/dialog_curtain." + ext));
      CHECK(d->lastResult().files.size() == 3 && d->resultLabel()->text().contains("Written: dialog_fan.vec"));
      /* the budget thins evenly */
      d->lassoOnly()->setChecked(false);
      d->budgetOn()->setChecked(true);
      d->budget()->setValue(10);
      d->count();
      d->waitUntilIdle();
      CHECK(d->lastOk() && d->lastResult().fan.written <= 10 && d->lastResult().fan.stride > 1);
      d->budgetOn()->setChecked(false);
      /* the current ping only */
      d->currentPing()->setChecked(true);
      d->count();
      d->waitUntilIdle();
      CHECK(d->lastOk() && d->lastResult().fan.pings == 1);
      /* settings that make no export */
      d->pingRange()->setChecked(true);
      d->firstPing()->setValue(3);
      d->lastPing()->setValue(1);
      d->count();
      d->waitUntilIdle();
      CHECK(d->resultLabel()->text() == "The first ping is after the last");
      d->firstPing()->setValue(0);
      d->formatVec()->setChecked(false);
      d->formatPly()->setChecked(false);
      d->formatLas()->setChecked(false);
      d->exportFiles();
      d->waitUntilIdle();
      CHECK(d->resultLabel()->text() == "No format chosen");
      d->close();
    }
    w.clearLasso();
  }

  /* replay goes round the steps */
  spins[5]->setValue(10.0);
  w.setStep(last);
  QPushButton *replay = button(window, "Replay");
  replay->click();
  CHECK(replay->text() == "Stop (10.0 p/s)");
  wait(300);
  CHECK(slider->value() < last);
  replay->click();
  CHECK(replay->text() == "Replay");
  const int stopped = slider->value();
  wait(250);
  CHECK(slider->value() == stopped);

  /* an image of the plot */
  const QString image = dir + "/window.png";
  CHECK(w.saveImage(image));
  CHECK(QFile::exists(image));

  /* opening again stops replay and starts at the first step */
  w.setStep(1);
  replay->click();
  CHECK(replay->isChecked());
  CHECK(w.openFile(kmall));
  CHECK(!replay->isChecked());
  w.waitUntilIdle();
  CHECK(slider->value() == 0 && slider->maximum() == last);
  CHECK(has_label(window, QString("0 / %1").arg(last)));

  /* a ping asked for while the file loads is shown when it is in */
  CHECK(w.openFile(kmall));
  w.showPing(2);
  w.waitUntilIdle();
  CHECK(slider->value() > 1);

  /* a datalist: the line listed twice counts once */
  {
    const QString list = dir + "/lines.mb-1";
    QFile file(list);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream(&file) << kmall << " 261\n" << kmall << " 261\n";
    file.close();
    CHECK(button(window, "Next file")->isHidden());
    CHECK(w.openFile(list));
    w.waitUntilIdle();
    CHECK(w.fileCount() == 1 && w.fileIndex() == 0 && w.loaded());
    CHECK(!button(window, "Next file")->isHidden() && !button(window, "Next file")->isEnabled());
    CHECK(has_label(window, "File 1 / 1"));
    CHECK(!w.openListedFile(1));
    CHECK(w.openFile(kmall));
    w.waitUntilIdle();
    CHECK(w.fileCount() == 0 && button(window, "Next file")->isHidden());
  }

  printf("%d checks, %d failed\n", checks, failures);
  return failures != 0;
}
