#include "qewokosbackingstore.h"
#include "qewokoswindow.h"

#include <QtGui/qwindow.h>

QT_BEGIN_NAMESPACE

EwokosBackingStore::EwokosBackingStore(QWindow *window)
    : QPlatformBackingStore(window)
{
}

EwokosBackingStore::~EwokosBackingStore()
{
}

QPaintDevice *EwokosBackingStore::paintDevice()
{
    EwokosWindow *w = static_cast<EwokosWindow *>(window()->handle());
    return w ? w->paintSurface() : nullptr;
}

void EwokosBackingStore::flush(QWindow *window, const QRegion &region, const QPoint &offset)
{
    EwokosWindow *w = static_cast<EwokosWindow *>(this->window()->handle());
    if (!w)
        return;

    /* offset is where the top-left of this backing store sits relative to the
       window, and it is only non-zero for a child window sharing its parent's
       store.  Every window here has its own store covering exactly its own
       canvas, so it is always (0,0); honoring a hypothetical non-zero value
       would mean translating the region, and a mistranslated copy is much worse
       than an ignored one that lands in the right place anyway. */
    Q_UNUSED(offset);
    Q_UNUSED(window);

    w->setDirtyRegion(region);
    w->present();
}

void EwokosBackingStore::resize(const QSize &size, const QRegion &staticContents)
{
    Q_UNUSED(staticContents);

    /* Nothing to do, and it has to be nothing.  The canvas is sized by the x
       server, not by Qt: xwin_open() clamps the request to the desktop space and
       every resize after that arrives from the server as XEVT_WIN_RESIZE, at
       which point EwokosWindow::handleResize() has already grown m_surface to
       match.  Resizing the surface here would let Qt set a size the server has
       not allocated a buffer for, and the very next on_repaint would copy into a
       canvas of a different shape. */
    Q_UNUSED(size);
}

QT_END_NAMESPACE
