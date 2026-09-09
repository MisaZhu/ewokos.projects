#ifndef QEWOKOSSCREEN_H
#define QEWOKOSSCREEN_H

#include <qpa/qplatformscreen.h>

QT_BEGIN_NAMESPACE

/*
 * One QPlatformScreen per EwokOS display.
 *
 * The geometry, refresh rate and pixel format all come from the window server
 * rather than from a mode list: xwin has no client-visible mode setting, the
 * server owns the scan-out, and x_screen_info() reports what it decided.  So
 * modes()/currentMode()/preferredMode() are left at the base implementation -
 * advertising a switchable mode list we cannot switch would only let Qt offer
 * a screen mode change that silently does nothing.
 *
 * physicalSize() and logicalDpi() are also left alone.  xwin publishes no
 * physical dimensions, and the base computes them from a 96 dpi assumption,
 * which is the honest answer here and the one every other framebuffer QPA
 * plugin gives.
 */
class EwokosScreen : public QPlatformScreen
{
public:
    explicit EwokosScreen(int index);

    QRect geometry() const override { return m_geometry; }
    QRect availableGeometry() const override { return m_availableGeometry; }

    int depth() const override { return 32; }

    /* graph_t's buffer is uint32_t per pixel in ARGB order, which is exactly
       QImage::Format_ARGB32 (0xAARRGGBB, non-premultiplied).  SDL's ewokos
       backend picks SDL_PIXELFORMAT_ABGR8888 for the same buffer - that is the
       same layout named by byte order instead of by word value.  Format_ARGB32
       rather than _Premultiplied because xwin's compositor blends the window
       surface with straight alpha (graph_blt_alpha), not premultiplied. */
    QImage::Format format() const override { return QImage::Format_ARGB32; }

    qreal refreshRate() const override { return m_refreshRate; }
    QString name() const override { return m_name; }

    /* Re-reads geometry, desktop space and refresh rate from the server.  The
       integration calls it when the server reports the desktop space changed. */
    void refresh();

private:
    int m_index;
    QRect m_geometry;
    QRect m_availableGeometry;
    qreal m_refreshRate;
    QString m_name;
};

QT_END_NAMESPACE

#endif // QEWOKOSSCREEN_H
