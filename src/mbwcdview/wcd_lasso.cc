/*--------------------------------------------------------------------
 *    The MB-system:  wcd_lasso.cc  28/11/2026
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

#include "wcd_lasso.h"

#include <cmath>

#include <QPen>

namespace wcd {

void LassoEditor::setPolygons(std::vector<QPolygonF> polygons) {
  polygons_ = std::move(polygons);
  cancel();
}

void LassoEditor::clear() {
  polygons_.clear();
  cancel();
}

void LassoEditor::cancel() {
  current_.clear();
  drawing_ = false;
}

bool LassoEditor::press(const QPointF &data, const QPointF &pixel, Qt::KeyboardModifiers modifiers) {
  if (modifiers & Qt::AltModifier) {
    /* the last drawn polygon under the pointer */
    for (size_t i = polygons_.size(); i > 0; i--)
      if (polygons_[i - 1].containsPoint(data, Qt::OddEvenFill)) {
        polygons_.erase(polygons_.begin() + (long)(i - 1));
        return true;
      }
    return false;
  }
  current_.clear();
  current_ << data;
  last_pixel_ = pixel;
  add_ = (modifiers & Qt::ShiftModifier) != 0;
  drawing_ = true;
  return false;
}

void LassoEditor::move(const QPointF &data, const QPointF &pixel) {
  if (!drawing_)
    return;
  if (std::hypot(pixel.x() - last_pixel_.x(), pixel.y() - last_pixel_.y()) >= 3.0) {
    current_ << data;
    last_pixel_ = pixel;
  }
}

bool LassoEditor::release() {
  if (!drawing_)
    return false;
  drawing_ = false;
  if (current_.size() < 3) {
    current_.clear();
    return false;
  }
  if (!add_)
    polygons_.clear();
  polygons_.push_back(current_);
  current_.clear();
  return true;
}

void LassoEditor::draw(QPainter &p, const QRectF &clip, const ToPixel &to_pixel) const {
  if (polygons_.empty() && !drawing_)
    return;
  p.save();
  p.setClipRect(clip);
  p.setRenderHint(QPainter::Antialiasing, true);
  const QColor color(230, 0, 140);
  auto pixels = [&](const QPolygonF &polygon) {
    QPolygonF out;
    for (const QPointF &v : polygon)
      out << to_pixel(v.x(), v.y());
    return out;
  };
  p.setPen(QPen(color, 1.5));
  p.setBrush(QColor(color.red(), color.green(), color.blue(), 40));
  for (const QPolygonF &polygon : polygons_)
    p.drawPolygon(pixels(polygon), Qt::OddEvenFill);
  if (drawing_ && current_.size() > 1) {
    QPen pen(color, 1.5, Qt::DashLine);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(pixels(current_));
  }
  p.restore();
}

}  // namespace wcd
