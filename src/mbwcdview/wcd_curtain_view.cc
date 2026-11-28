/*--------------------------------------------------------------------
 *    The MB-system:  wcd_curtain_view.cc  28/11/2026
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

#include "wcd_curtain_view.h"

#include <algorithm>
#include <cmath>

#include <QMouseEvent>
#include <QPaintEvent>
#include <QWheelEvent>

#include "wcd_plot.h"

namespace wcd {

namespace {

/* margins around the plot area (logical pixels); the left one as in the swath plot */
constexpr double MARGIN_LEFT = 72.0, MARGIN_RIGHT = 24.0, MARGIN_TOP = 8.0, MARGIN_BOTTOM = 46.0;
constexpr double CAPTION_HEIGHT = 20.0;
constexpr double EXAGGERATION_MIN = 1.0e-3, EXAGGERATION_MAX = 1.0e4;

}  // namespace

/*--------------------------------------------------------------------*/
void CurtainPlot::setCurtain(std::shared_ptr<const Curtain> curtain) {
  curtain_ = std::move(curtain);
  depth_bottom_ = 0.0;
  if (curtain_ != nullptr) {
    for (int i = 0; i < curtain_->ncols; i++) {
      const float *col = &curtain_->mean[(size_t)i * curtain_->nz];
      for (int k = curtain_->nz - 1; k >= 0; k--) {
        if (std::isfinite(col[k])) {
          depth_bottom_ = std::max(depth_bottom_, (k + 1) * curtain_->dz);
          break;
        }
      }
    }
    message_.clear();
  }
  cache_key_.clear();
}

void CurtainPlot::setMaxMode(bool max) { max_ = max; }
void CurtainPlot::setPalette(Palette palette) { palette_ = palette; }

void CurtainPlot::setColorRange(double vmin, double vmax) {
  vmin_ = vmin;
  vmax_ = vmax;
}

void CurtainPlot::setDepthWindow(double dmin, double dmax) {
  dmin_ = dmin;
  dmax_ = dmax;
}

void CurtainPlot::setCurrent(int ping) { current_ = ping; }

void CurtainPlot::setMessage(const QString &message) { message_ = message; }

double CurtainPlot::curtainStart() const {
  if (curtain_ == nullptr || curtain_->ncols == 0)
    return 0.0;
  double x0, x1;
  curtain_->column_span(0, x0, x1);
  return x0;
}

double CurtainPlot::curtainEnd() const {
  if (curtain_ == nullptr || curtain_->ncols == 0)
    return 1.0;
  double x0, x1;
  curtain_->column_span(curtain_->ncols - 1, x0, x1);
  return x1;
}

/* depth window, down to the deepest value */
void CurtainPlot::depthView(double &z0, double &z1) const {
  z0 = dmin_;
  z1 = depth_bottom_ > 0.0 ? std::min(dmax_, depth_bottom_) : dmax_;
  if (!(z1 - z0 > 1.0e-3))
    z1 = z0 + 1.0e-3;
}

QRectF CurtainPlot::plotRect(const QSize &size) const {
  return QRectF(MARGIN_LEFT, MARGIN_TOP, std::max(10.0, size.width() - MARGIN_LEFT - MARGIN_RIGHT),
                std::max(10.0, size.height() - MARGIN_TOP - MARGIN_BOTTOM - CAPTION_HEIGHT));
}

double CurtainPlot::metresPerPixelDepth(const QSize &size) const {
  double z0, z1;
  depthView(z0, z1);
  return (z1 - z0) / plotRect(size).height();
}

void CurtainPlot::setExaggeration(double exaggeration, const QSize &) {
  exaggeration_ = std::min(std::max(exaggeration, EXAGGERATION_MIN), EXAGGERATION_MAX);
}

double CurtainPlot::fitExaggeration(const QSize &size) const {
  const double mz = metresPerPixelDepth(size);
  if (!(mz > 0.0))
    return 1.0;
  return std::min(std::max((curtainEnd() - curtainStart()) / plotRect(size).width() / mz, EXAGGERATION_MIN),
                  EXAGGERATION_MAX);
}

void CurtainPlot::followCurrent(const QSize &size) {
  if (curtain_ == nullptr || current_ < 0 || current_ >= curtain_->ncols)
    return;
  const double x = curtain_->along[current_];
  const double span = plotRect(size).width() * metresPerPixelAlong(size);
  if (x < x0_ || x > x0_ + span)
    x0_ = x - 0.5 * span;
  /* no empty space before the start where the whole curtain fits */
  if (curtainEnd() - curtainStart() <= span)
    x0_ = curtainStart();
}

int CurtainPlot::pingAt(const QPointF &pixel, const QSize &size) const {
  const QRectF r = plotRect(size);
  if (curtain_ == nullptr || pixel.x() < r.left() || pixel.x() > r.right())
    return -1;
  return curtain_->column(x0_ + (pixel.x() - r.left()) * metresPerPixelAlong(size));
}

void CurtainPlot::toDataClamped(const QPointF &pixel, const QSize &size, double &along, double &depth) const {
  const QRectF r = plotRect(size);
  double z0, z1;
  depthView(z0, z1);
  const double x = std::min(std::max(pixel.x(), r.left()), r.right());
  const double y = std::min(std::max(pixel.y(), r.top()), r.bottom());
  along = x0_ + (x - r.left()) * metresPerPixelAlong(size);
  depth = z0 + (y - r.top()) / r.height() * (z1 - z0);
}

/*--------------------------------------------------------------------*/
void CurtainPlot::draw(QPainter &p, const QSize &size, double dpr, const QString &caption) {
  const QRectF r = plotRect(size);
  double z0, z1;
  depthView(z0, z1);
  const double mx = metresPerPixelAlong(size);
  const double x1 = x0_ + r.width() * mx;
  auto px = [&](double x) { return r.left() + (x - x0_) / mx; };
  auto pz = [&](double z) { return r.top() + (z - z0) / (z1 - z0) * r.height(); };

  /* the plot area as an image in device pixels, made again only after a change */
  const int w = std::max(1, (int)std::lround(r.width() * dpr)), h = std::max(1, (int)std::lround(r.height() * dpr));
  if (curtain_ != nullptr) {
    const QString key = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10")
                            .arg(w).arg(h).arg(x0_, 0, 'g', 17).arg(mx, 0, 'g', 17).arg(z0, 0, 'g', 17)
                            .arg(z1, 0, 'g', 17).arg(vmin_).arg(vmax_).arg((int)palette_).arg(max_ ? 1 : 0);
    if (key != cache_key_) {
      const Curtain &c = *curtain_;
      const std::vector<float> &values = max_ ? c.max : c.mean;
      QRgb colors[256];
      for (int i = 0; i < 256; i++)
        colors[i] = palette_color(palette_, i);
      const QRgb background = qRgb(255, 255, 255);
      const double scale = 255.0 / std::max(vmax_ - vmin_, 1.0e-6);
      std::vector<int> cell(h);
      for (int y = 0; y < h; y++) {
        const double z = z0 + (y + 0.5) / h * (z1 - z0);
        const int k = (int)std::floor(z / c.dz);
        cell[y] = k >= 0 && k < c.nz ? k : -1;
      }
      cache_ = QImage(w, h, QImage::Format_RGB32);
      for (int x = 0; x < w; x++) {
        const int col = c.column(x0_ + (x + 0.5) / dpr * mx);
        const float *v = col >= 0 ? &values[(size_t)col * c.nz] : nullptr;
        for (int y = 0; y < h; y++) {
          QRgb rgb = background;
          if (v != nullptr && cell[y] >= 0 && std::isfinite(v[cell[y]]))
            rgb = colors[std::min(255, std::max(0, (int)std::lround((v[cell[y]] - vmin_) * scale)))];
          reinterpret_cast<QRgb *>(cache_.scanLine(y))[x] = rgb;
        }
      }
      cache_key_ = key;
    }
    p.drawImage(r, cache_);
  }

  /* the ping of the swath view */
  p.setRenderHint(QPainter::Antialiasing, true);
  const QFontMetricsF fm(p.font());
  if (curtain_ != nullptr && current_ >= 0 && current_ < curtain_->ncols) {
    const double x = px(curtain_->along[current_]);
    if (x >= r.left() && x <= r.right()) {
      p.setPen(QPen(QColor(220, 0, 0), 1.5));
      p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
      if (!label_.isEmpty()) {
        const double tw = fm.horizontalAdvance(label_);
        const double tx = x + 4 + tw < r.right() ? x + 4 : x - 4 - tw;
        p.fillRect(QRectF(tx - 2, r.top() + 2, tw + 4, fm.height()), QColor(255, 255, 255, 190));
        p.drawText(QPointF(tx, r.top() + 2 + fm.ascent()), label_);
      }
    }
  }

  /* the lasso of the curtain */
  if (lasso_ != nullptr)
    lasso_->draw(p, r, [&](double x, double z) { return QPointF(px(x), pz(z)); });

  /* frame, ticks and labels */
  p.setRenderHint(QPainter::Antialiasing, false);
  p.setPen(QPen(Qt::black, 1.0));
  p.setBrush(Qt::NoBrush);
  p.drawRect(r);
  if (curtain_ == nullptr) {
    const QString text = message_.isEmpty() ? QString("No curtain") : message_;
    p.drawText(r, Qt::AlignCenter, text);
  }
  else {
    const double xs = tick_step(x1 - x0_, std::max(2, (int)(r.width() / 90.0)));
    for (double x = std::ceil(x0_ / xs) * xs; x <= x1 + 1.0e-9; x += xs) {
      p.drawLine(QPointF(px(x), r.bottom()), QPointF(px(x), r.bottom() + 4));
      const QString t = tick_label(x, xs);
      p.drawText(QPointF(px(x) - fm.horizontalAdvance(t) / 2, r.bottom() + 6 + fm.ascent()), t);
    }
    const double zs = tick_step(z1 - z0, std::max(2, (int)(r.height() / 40.0)));
    for (double z = std::ceil(z0 / zs) * zs; z <= z1 + 1.0e-9; z += zs) {
      p.drawLine(QPointF(r.left() - 4, pz(z)), QPointF(r.left(), pz(z)));
      const QString t = tick_label(z, zs);
      p.drawText(QPointF(r.left() - 6 - fm.horizontalAdvance(t), pz(z) + fm.ascent() / 2 - 1), t);
    }
    const QString exaggeration =
        std::fabs(exaggeration_ - 1.0) < 1.0e-6 ? QString("1:1") : QString("exaggeration %1").arg(exaggeration_, 0, 'g', 3);
    const QString xlabel = QString("%1 - beams %2-%3, %4 - %5")
                               .arg(curtain_->along_is_distance ? "Along-track distance (m)" : "Ping")
                               .arg(curtain_->first_beam)
                               .arg(curtain_->last_beam)
                               .arg(max_ ? "strongest value" : "mean of linear power")
                               .arg(curtain_->along_is_distance ? exaggeration : QString("no positions"));
    p.drawText(QPointF(r.center().x() - fm.horizontalAdvance(xlabel) / 2, r.bottom() + 8 + 2 * fm.height()), xlabel);
    p.save();
    const QString ylabel = "Depth (m)";
    p.translate(14, r.center().y() + fm.horizontalAdvance(ylabel) / 2);
    p.rotate(-90);
    p.drawText(QPointF(0, 0), ylabel);
    p.restore();
  }

  /* caption (file name and metadata) for saved images */
  if (!caption.isEmpty()) {
    const QRectF box(4, size.height() - CAPTION_HEIGHT + 2, fm.horizontalAdvance(caption) + 8, CAPTION_HEIGHT - 4);
    p.fillRect(box, QColor(0, 0, 0, 128));
    p.setPen(Qt::white);
    p.drawText(QPointF(box.left() + 4, box.top() + fm.ascent() + 1), caption);
  }
}

QImage CurtainPlot::render(const QSize &size, double scale, const QString &caption) {
  QImage image(QSize((int)std::lround(size.width() * scale), (int)std::lround(size.height() * scale)),
               QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  image.setDevicePixelRatio(scale);
  QPainter painter(&image);
  draw(painter, size, scale, caption);
  painter.end();
  return image;
}

/*--------------------------------------------------------------------*/
CurtainView::CurtainView(QWidget *parent) : QWidget(parent) {
  setMinimumSize(300, 150);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setMouseTracking(false);
  setToolTip("Curtain: drag to move along track, wheel to zoom along track, click to show that ping");
  plot_.setLasso(&lasso_);
}

void CurtainView::setExaggeration(double exaggeration) {
  plot_.setExaggeration(exaggeration, size());
  plot_.followCurrent(size());
  update();
}

void CurtainView::fit() {
  plot_.setExaggeration(plot_.fitExaggeration(size()), size());
  plot_.showStart();
  emit exaggerationChanged(plot_.exaggeration());
  update();
}

void CurtainView::followCurrent() {
  plot_.followCurrent(size());
  update();
}

void CurtainView::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.fillRect(rect(), Qt::white);
  plot_.draw(painter, size(), devicePixelRatioF());
}

void CurtainView::setLassoMode(bool on) {
  lasso_mode_ = on;
  if (!on && lasso_.drawing()) {
    lasso_.cancel();
    update();
  }
  if (on)
    setCursor(Qt::CrossCursor);
  else
    unsetCursor();
}

void CurtainView::mousePressEvent(QMouseEvent *event) {
  if (lasso_mode_) {
    if (event->button() == Qt::LeftButton && plot_.curtain() != nullptr &&
        plot_.plotRect(size()).contains(event->position())) {
      double along, depth;
      plot_.toDataClamped(event->position(), size(), along, depth);
      if (lasso_.press(QPointF(along, depth), event->position(), event->modifiers()))
        emit lassoChanged();
      update();
    }
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton) {
    pressed_ = true;
    dragging_ = false;
    press_ = event->position();
    press_start_ = plot_.start();
  }
  QWidget::mousePressEvent(event);
}

void CurtainView::mouseMoveEvent(QMouseEvent *event) {
  if (lasso_mode_) {
    if (lasso_.drawing()) {
      double along, depth;
      plot_.toDataClamped(event->position(), size(), along, depth);
      lasso_.move(QPointF(along, depth), event->position());
      update();
    }
    event->accept();
    return;
  }
  if (pressed_) {
    const double dx = event->position().x() - press_.x();
    if (std::fabs(dx) > 3.0)
      dragging_ = true;
    if (dragging_) {
      plot_.setStart(press_start_ - dx * plot_.metresPerPixelAlong(size()));
      setCursor(Qt::ClosedHandCursor);
      update();
    }
  }
  QWidget::mouseMoveEvent(event);
}

void CurtainView::mouseReleaseEvent(QMouseEvent *event) {
  if (lasso_mode_) {
    if (event->button() == Qt::LeftButton && lasso_.drawing()) {
      double along, depth;
      plot_.toDataClamped(event->position(), size(), along, depth);
      lasso_.move(QPointF(along, depth), event->position());
      if (lasso_.release())
        emit lassoChanged();
      update();
    }
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton && pressed_) {
    pressed_ = false;
    unsetCursor();
    if (!dragging_) {
      const int ping = plot_.pingAt(event->position(), size());
      if (ping >= 0)
        emit pingClicked(ping);
    }
    dragging_ = false;
  }
  QWidget::mouseReleaseEvent(event);
}

/* zoom along track about the pointer */
void CurtainView::wheelEvent(QWheelEvent *event) {
  const double steps = event->angleDelta().y() / 120.0;
  if (steps != 0.0 && plot_.curtain() != nullptr) {
    const QRectF r = plot_.plotRect(size());
    const double cx = std::min(std::max(event->position().x(), r.left()), r.right());
    const double along = plot_.start() + (cx - r.left()) * plot_.metresPerPixelAlong(size());
    plot_.setExaggeration(plot_.exaggeration() * std::pow(1.25, -steps), size());
    plot_.setStart(along - (cx - r.left()) * plot_.metresPerPixelAlong(size()));
    emit exaggerationChanged(plot_.exaggeration());
    update();
  }
  event->accept();
}

}  // namespace wcd
