/*
 * Viewport - see viewport.h for the mapping to upstream's DkViewPort.
 */

#include "viewport.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>

// Upstream clamps between 0.01 and 100; the same limits keep a runaway
// wheel from allocating absurd scaled images.
static const qreal kMinZoom = 0.01;
static const qreal kMaxZoom = 100.0;

Viewport::Viewport(QWidget *parent)
    : QWidget(parent)
{
    // nomacs' trademark dark canvas.
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(37, 37, 37));
    setPalette(pal);
    setMouseTracking(false);
    setCursor(Qt::OpenHandCursor);
}

void Viewport::setImage(const QImage &image)
{
    image_ = image;
    pan_ = QPoint();
    if (fit_)
        zoom_ = fitZoom();
    emit zoomChanged(zoom_);
    update();
}

// ---- geometry -------------------------------------------------------------

qreal Viewport::fitZoom() const
{
    if (image_.isNull() || width() <= 0 || height() <= 0)
        return 1.0;
    const qreal zx = qreal(width()) / image_.width();
    const qreal zy = qreal(height()) / image_.height();
    // Fit means "entirely visible", and like upstream small images are not
    // blown up - they sit centred at 100%.
    return qMin(qMin(zx, zy), 1.0);
}

QRect Viewport::imageRect() const
{
    const int w = qRound(image_.width() * zoom_);
    const int h = qRound(image_.height() * zoom_);
    return QRect((width() - w) / 2 + pan_.x(),
                 (height() - h) / 2 + pan_.y(), w, h);
}

void Viewport::clampPan()
{
    // Keep the image over the widget: no panning along an axis on which it
    // fits, and no dragging it fully out of view on one that does not.
    const int w = qRound(image_.width() * zoom_);
    const int h = qRound(image_.height() * zoom_);
    const int mx = qMax(0, (w - width()) / 2);
    const int my = qMax(0, (h - height()) / 2);
    pan_.setX(qBound(-mx, pan_.x(), mx));
    pan_.setY(qBound(-my, pan_.y(), my));
}

// ---- zoom -----------------------------------------------------------------

void Viewport::setZoom(qreal zoom, const QPoint &anchor)
{
    if (image_.isNull())
        return;
    zoom = qBound(kMinZoom, zoom, kMaxZoom);
    if (qFuzzyCompare(zoom, zoom_))
        return;

    // Keep the image point under the anchor stationary - the wheel-zoom
    // feel nomacs has.  With no anchor the widget centre is used.
    const QPoint a = (anchor.x() < 0) ? rect().center() : anchor;
    const QRect before = imageRect();
    const qreal relX = before.width()  > 0 ? qreal(a.x() - before.x()) / before.width()  : 0.5;
    const qreal relY = before.height() > 0 ? qreal(a.y() - before.y()) / before.height() : 0.5;

    zoom_ = zoom;
    fit_ = false;

    const int w = qRound(image_.width() * zoom_);
    const int h = qRound(image_.height() * zoom_);
    pan_.setX(a.x() - qRound(relX * w) - (width() - w) / 2);
    pan_.setY(a.y() - qRound(relY * h) - (height() - h) / 2);
    clampPan();

    emit zoomChanged(zoom_);
    update();
}

void Viewport::zoomIn()
{
    setZoom(zoom_ * 1.25);
}

void Viewport::zoomOut()
{
    setZoom(zoom_ / 1.25);
}

void Viewport::zoomToFit()
{
    fit_ = true;
    pan_ = QPoint();
    zoom_ = fitZoom();
    emit zoomChanged(zoom_);
    update();
}

void Viewport::zoomActualSize()
{
    setZoom(1.0);
}

// ---- events ----------------------------------------------------------------

void Viewport::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.fillRect(event->rect(), palette().color(QPalette::Window));
    if (image_.isNull())
        return;
    // Smooth only when shrinking: enlarged pixels are supposed to be
    // blocky (upstream shows them the same way), and nearest-neighbour
    // keeps big zoom factors cheap on this CPU-only pipeline.
    p.setRenderHint(QPainter::SmoothPixmapTransform, zoom_ < 1.0);
    p.drawImage(imageRect(), image_);
}

void Viewport::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (fit_)
        zoom_ = fitZoom();
    clampPan();
    emit zoomChanged(zoom_);
}

void Viewport::wheelEvent(QWheelEvent *event)
{
    // Plain wheel zooms (the nomacs default); Ctrl+wheel flips through the
    // folder the way upstream's "wheel navigates" option does.
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->angleDelta().y() < 0)
            emit nextRequested();
        else
            emit previousRequested();
    } else {
        const qreal factor = event->angleDelta().y() > 0 ? 1.25 : 1.0 / 1.25;
        setZoom(zoom_ * factor, event->position().toPoint());
    }
    event->accept();
}

void Viewport::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        dragStart_ = event->pos();
        panStart_ = pan_;
        setCursor(Qt::ClosedHandCursor);
    }
}

void Viewport::mouseMoveEvent(QMouseEvent *event)
{
    if (dragging_) {
        pan_ = panStart_ + (event->pos() - dragStart_);
        clampPan();
        update();
    }
}

void Viewport::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
        setCursor(Qt::OpenHandCursor);
    }
}

void Viewport::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Upstream toggles between fit and 100% on double click.
    if (fit_ || !qFuzzyCompare(zoom_, 1.0))
        setZoom(1.0, event->pos());
    else
        zoomToFit();
}
