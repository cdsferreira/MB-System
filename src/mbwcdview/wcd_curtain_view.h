/*--------------------------------------------------------------------
 *    The MB-system:  wcd_curtain_view.h  28/11/2026
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
 * Plot of the curtain (wcd_curtain.h): along-track distance across, depth down, drawn
 * with QPainter as an image of the plot area (made again only when the curtain, the
 * view or the colours change), with the axes and a line at the ping shown in the
 * swath view. The scale starts at 1:1 (as many metres per pixel along track as in
 * depth); the vertical exaggeration is metres per pixel along track over metres per
 * pixel in depth. CurtainView is the widget: drag to move along track, the wheel to
 * zoom along track (the exaggeration), a click to show that ping.
 */

#ifndef MBWCDVIEW_WCD_CURTAIN_VIEW_H
#define MBWCDVIEW_WCD_CURTAIN_VIEW_H

#include <memory>

#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QWidget>

#include "wcd_curtain.h"
#include "wcd_lasso.h"
#include "wcd_palette.h"

namespace wcd {

class CurtainPlot {
 public:
  void setCurtain(std::shared_ptr<const Curtain> curtain);
  const Curtain *curtain() const { return curtain_.get(); }
  std::shared_ptr<const Curtain> sharedCurtain() const { return curtain_; }
  void setMaxMode(bool max);         // the strongest value instead of the mean
  void setPalette(Palette palette);
  void setColorRange(double vmin, double vmax);
  void setDepthWindow(double dmin, double dmax);
  void setCurrent(int ping);         // the ping of the swath view, -1 none
  int current() const { return current_; }
  void setMessage(const QString &message);  // shown instead of an empty plot
  void setLabel(const QString &label) { label_ = label; }  // ping label at the indicator line

  /* the exaggeration (1: 1:1) and the along-track position at the left of the plot area */
  void setExaggeration(double exaggeration, const QSize &size);
  double exaggeration() const { return exaggeration_; }
  void setStart(double along) { x0_ = along; }
  double start() const { return x0_; }
  /* the exaggeration that shows the whole curtain in the plot area */
  double fitExaggeration(const QSize &size) const;
  /* move so that the current ping is in view (centred when it was not) */
  void followCurrent(const QSize &size);
  /* the whole curtain from its start */
  void showStart() { x0_ = curtainStart(); }

  QRectF plotRect(const QSize &size) const;
  double metresPerPixelDepth(const QSize &size) const;
  double metresPerPixelAlong(const QSize &size) const { return metresPerPixelDepth(size) * exaggeration_; }
  /* the ping at this pixel, -1 if none */
  int pingAt(const QPointF &pixel, const QSize &size) const;
  /* along-track position and depth at a pixel, for one outside of the plot area at the nearest edge */
  void toDataClamped(const QPointF &pixel, const QSize &size, double &along, double &depth) const;
  /* the lasso drawn over the curtain, nullptr none */
  void setLasso(const LassoEditor *lasso) { lasso_ = lasso; }

  void draw(QPainter &painter, const QSize &size, double dpr, const QString &caption = QString());
  QImage render(const QSize &size, double scale, const QString &caption);

 private:
  double curtainStart() const;
  double curtainEnd() const;
  void depthView(double &z0, double &z1) const;
  std::shared_ptr<const Curtain> curtain_;
  bool max_ = false;
  Palette palette_ = Palette::Viridis;
  double vmin_ = -60.0, vmax_ = 0.0;
  double dmin_ = 0.0, dmax_ = 1.0e5;
  double exaggeration_ = 1.0, x0_ = 0.0;
  int current_ = -1;
  double depth_bottom_ = 0.0;  // bottom of the deepest cell with a value
  QString message_, label_;
  const LassoEditor *lasso_ = nullptr;
  QImage cache_;
  QString cache_key_;
};

class CurtainView : public QWidget {
  Q_OBJECT

 public:
  explicit CurtainView(QWidget *parent = nullptr);
  CurtainPlot &plot() { return plot_; }
  void setExaggeration(double exaggeration);  // keeps the current ping in view
  void fit();
  void followCurrent();  // after setCurrent()
  /* in lasso mode the left button draws the lasso (see LassoEditor) instead of moving the curtain */
  void setLassoMode(bool on);
  bool lassoMode() const { return lasso_mode_; }
  LassoEditor &lasso() { return lasso_; }
  const LassoEditor &lasso() const { return lasso_; }

 signals:
  void pingClicked(int ping);
  void exaggerationChanged(double exaggeration);
  void lassoChanged();

 protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;

 private:
  CurtainPlot plot_;
  QPointF press_;
  double press_start_ = 0.0;
  bool dragging_ = false, pressed_ = false;
  LassoEditor lasso_;
  bool lasso_mode_ = false;
};

}  // namespace wcd

#endif
