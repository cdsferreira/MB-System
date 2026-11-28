/*--------------------------------------------------------------------
 *    The MB-system:  wcd_lasso.h  28/11/2026
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
 * The lasso of one view (the swath view or the curtain): closed polygons in the
 * view's data coordinates (x across or along track, y depth, in metres), drawn
 * with the mouse. A drag draws a polygon that replaces the view's polygons; with
 * Shift it is added to them; a click with Alt (Option on macOS) removes the
 * polygon under the pointer.
 */

#ifndef MBWCDVIEW_WCD_LASSO_H
#define MBWCDVIEW_WCD_LASSO_H

#include <functional>
#include <vector>

#include <QPainter>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>

namespace wcd {

class LassoEditor {
 public:
  using ToPixel = std::function<QPointF(double x, double depth)>;

  const std::vector<QPolygonF> &polygons() const { return polygons_; }
  void setPolygons(std::vector<QPolygonF> polygons);
  bool empty() const { return polygons_.empty(); }
  void clear();
  bool drawing() const { return drawing_; }

  /* a press at data point data (pixel at pixel): starts a polygon, or with Alt removes the
     polygon under it; true when the polygons changed */
  bool press(const QPointF &data, const QPointF &pixel, Qt::KeyboardModifiers modifiers);
  /* a vertex where the pointer moved at least 3 pixels from the last one */
  void move(const QPointF &data, const QPointF &pixel);
  /* ends the polygon; true when the polygons changed (a polygon needs three vertices) */
  bool release();
  void cancel();

  /* the polygons, and the one being drawn, clipped to the plot area */
  void draw(QPainter &painter, const QRectF &clip, const ToPixel &to_pixel) const;

 private:
  std::vector<QPolygonF> polygons_;
  QPolygonF current_;
  QPointF last_pixel_;
  bool drawing_ = false, add_ = false;
};

}  // namespace wcd

#endif
