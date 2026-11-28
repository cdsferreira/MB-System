/*--------------------------------------------------------------------
 *    The MB-system:  WcdView.cc  1/11/2026
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

#include "WcdView.h"

#include <QMouseEvent>
#include <QVBoxLayout>

#ifdef MBWCD_HAVE_RHI
#include "wcd_view_rhi.h"
#endif
#ifdef MBWCD_HAVE_GL
#include "wcd_view_gl.h"
#endif

#if !defined(MBWCD_HAVE_RHI) && !defined(MBWCD_HAVE_GL)
#error "mbwcdview needs QRhi (MBWCD_HAVE_RHI) or OpenGL (MBWCD_HAVE_GL) to draw the swath"
#endif

namespace wcd {

WcdView::WcdView(QWidget *parent) : QWidget(parent) {
  setMinimumSize(400, 300);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
#ifdef MBWCD_HAVE_RHI
  if (use_rhi()) {
    auto *view = new RhiSwathView(plot_);
    connect(view, &RhiSwathView::clicked, this, &WcdView::clicked);
    drawing_widget_ = view;
    drawing_ = view;
    rhi_ = true;
  }
#endif
#ifdef MBWCD_HAVE_GL
  if (drawing_ == nullptr) {
    auto *view = new GlSwathView(plot_);
    connect(view, &GlSwathView::clicked, this, &WcdView::clicked);
    drawing_widget_ = view;
    drawing_ = view;
  }
#endif
  layout->addWidget(drawing_widget_);
  plot_.setLasso(&lasso_);
  drawing_widget_->installEventFilter(this);
}

void WcdView::setLassoMode(bool on) {
  lasso_mode_ = on;
  if (!on && lasso_.drawing()) {
    lasso_.cancel();
    redraw();
  }
  if (on)
    drawing_widget_->setCursor(Qt::CrossCursor);
  else
    drawing_widget_->unsetCursor();
}

bool WcdView::eventFilter(QObject *watched, QEvent *event) {
  if (watched != drawing_widget_ || !lasso_mode_)
    return QWidget::eventFilter(watched, event);
  const QEvent::Type type = event->type();
  if (type != QEvent::MouseButtonPress && type != QEvent::MouseMove && type != QEvent::MouseButtonRelease &&
      type != QEvent::MouseButtonDblClick)
    return QWidget::eventFilter(watched, event);
  auto *mouse = static_cast<QMouseEvent *>(event);
  const QSize size = drawing_widget_->size();
  double across, depth;
  plot_.toDataClamped(mouse->position(), size, across, depth);
  const QPointF data(across, depth);
  if (type == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton) {
    if (!plot_.plotRect(size).contains(mouse->position()))
      return true;
    if (lasso_.press(data, mouse->position(), mouse->modifiers()))
      emit lassoChanged();
    redraw();
  }
  else if (type == QEvent::MouseMove && lasso_.drawing()) {
    lasso_.move(data, mouse->position());
    redraw();
  }
  else if (type == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton && lasso_.drawing()) {
    lasso_.move(data, mouse->position());
    if (lasso_.release())
      emit lassoChanged();
    redraw();
  }
  return true;
}

/* the drawing widget uses the plot: delete it first */
WcdView::~WcdView() { delete drawing_widget_; }

void WcdView::redraw() { drawing_->redraw(); }

}  // namespace wcd
