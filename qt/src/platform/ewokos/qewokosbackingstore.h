#ifndef QEWOKOSBACKINGSTORE_H
#define QEWOKOSBACKINGSTORE_H

#include <qpa/qplatformbackingstore.h>

QT_BEGIN_NAMESPACE

/*
 * The backing store is a thin shell: the pixels live in EwokosWindow::m_surface.
 *
 * Qt's painting sequence is beginPaint() / paint / endPaint() / flush().  paintDevice()
 * is what the raster engine draws into, and flush() is what has to get those pixels
 * onto the screen.  EwokosWindow owns both halves of that pair, because the only
 * moment it is safe to touch xwin's shared canvas is inside libx's on_repaint
 * callback - so flush() records the region and asks the window to present, and the
 * present ends up calling back into repaintThunk().
 */
class EwokosBackingStore : public QPlatformBackingStore
{
public:
    explicit EwokosBackingStore(QWindow *window);
    ~EwokosBackingStore() override;

    QPaintDevice *paintDevice() override;

    void flush(QWindow *window, const QRegion &region, const QPoint &offset) override;

    void resize(const QSize &size, const QRegion &staticContents) override;

    /* beginPaint()/endPaint() are deliberately left at the base implementation.
       The base sets the region Qt is about to paint and does nothing else for a
       QImage-backed device, which is what is wanted here: the surface must stay
       fully intact between paints because on_repaint copies from it, and a real
       "clear the paint region" step would destroy pixels Qt still expects to
       find when it draws a smaller region than the whole window. */
};

QT_END_NAMESPACE

#endif // QEWOKOSBACKINGSTORE_H
