#include "qewokosscreen.h"

#include <x/x.h>

QT_BEGIN_NAMESPACE

EwokosScreen::EwokosScreen(int index)
    : m_index(index)
    , m_refreshRate(60)
{
    refresh();
}

void EwokosScreen::refresh()
{
    xscreen_info_t info;
    memset(&info, 0, sizeof(info));

    /* x_screen_info() resolves a negative index to the default display, the
       same way x_get_display_id() does everywhere else in libx. */
    if (x_screen_info(&info, m_index) == 0 && info.size.w > 0 && info.size.h > 0) {
        m_geometry = QRect(0, 0, info.size.w, info.size.h);
        if (info.fps > 0)
            m_refreshRate = info.fps;
        m_name = QString::fromLatin1("EwokOS Screen %1").arg(info.id);
    } else {
        /* No server, or the query failed.  A screen with an empty geometry
           makes QGuiApplication abort with "Could not find a screen", so fall
           back to a size that at least lets an offscreen-ish app start and log
           something useful instead of dying in the platform plugin. */
        m_geometry = QRect(0, 0, 640, 480);
        m_name = QString::fromLatin1("EwokOS Screen %1").arg(m_index);
    }

    /* The desktop space is the screen minus whatever the window manager keeps
       for itself (taskbar, panel).  x_get_desktop_space() returns the rect the
       WM published, which is what Qt means by availableGeometry - the region a
       new window may be placed in without being covered. */
    grect_t space;
    memset(&space, 0, sizeof(space));
    if (x_get_desktop_space(m_index, &space) == 0 && space.w > 0 && space.h > 0)
        m_availableGeometry = QRect(space.x, space.y, space.w, space.h);
    else
        m_availableGeometry = m_geometry;
}

QT_END_NAMESPACE
