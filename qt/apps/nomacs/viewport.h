#ifndef NOMACS_VIEWPORT_H
#define NOMACS_VIEWPORT_H

/*
 * The image display area - nomacs' DkViewPort reduced to what a plain
 * QWidget can do: wheel zoom anchored at the cursor, drag panning, fit /
 * 100% modes with a double-click toggle, and the dark background upstream
 * paints behind the image.  The image itself lives in ImageLoader; this
 * widget only owns the view transform (zoom + pan).
 */

#include <QImage>
#include <QPoint>
#include <QWidget>

class Viewport : public QWidget {
    Q_OBJECT
public:
    explicit Viewport(QWidget *parent = nullptr);

    void setImage(const QImage &image);

    qreal zoom() const { return zoom_; }
    bool fitMode() const { return fit_; }

    void zoomIn();
    void zoomOut();
    void setZoom(qreal zoom, const QPoint &anchor = QPoint(-1, -1));
    void zoomToFit();       // and track resizes until the zoom is touched
    void zoomActualSize();

signals:
    void zoomChanged(qreal zoom);
    // Wheel-with-no-Ctrl and double-click-free clicks are view business;
    // navigation stays with the window, which owns the loader.
    void nextRequested();
    void previousRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    qreal fitZoom() const;
    QRect imageRect() const;    // where the image lands in widget coords
    void clampPan();

    QImage image_;
    qreal zoom_ = 1.0;
    bool fit_ = true;       // sticky fit-to-window until the user zooms
    QPoint pan_;            // extra offset applied on top of centring
    QPoint dragStart_;      // mouse position when panning began
    QPoint panStart_;       // pan_ when panning began
    bool dragging_ = false;
};

#endif // NOMACS_VIEWPORT_H
