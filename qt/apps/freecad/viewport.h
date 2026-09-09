/*
 * FreeCAD, ported to EwokOS - the 3D view.
 *
 * Upstream's Gui::View3DInventor is Coin3D on top of OpenGL; this Qt build
 * has neither (QT_FEATURE_opengl is -1 on the ewokos QPA), so the viewport
 * is a plain QWidget that renders the scene itself: placement transform,
 * look-at camera, perspective projection, painter's-algorithm depth sort
 * and flat-shaded QPainter polygons.  What it keeps from upstream is the
 * feel: the gradient background, the origin cross, the corner axis
 * indicator, green selection highlight, the numeric standard-view hotkeys
 * and orbit/pan/zoom mouse navigation.
 */

#ifndef FREECAD_VIEWPORT_H
#define FREECAD_VIEWPORT_H

#include <QWidget>

#include "document.h"

class Viewport : public QWidget {
    Q_OBJECT
public:
    explicit Viewport(Document *doc, QWidget *parent = 0);

    enum DrawStyle { Shaded, Wireframe, ShadedWire };
    enum StdView { ViewAxonometric, ViewFront, ViewTop, ViewRight, ViewRear, ViewBottom, ViewLeft };

    DocObject *selection() const { return m_selection; }
    void setSelection(DocObject *obj);
    void setDrawStyle(DrawStyle style);
    DrawStyle drawStyle() const { return m_style; }

public slots:
    void fitAll();
    void setStdView(int view);               // StdView value

signals:
    void selectionChanged(DocObject *obj);   // picked in the 3D view (may be 0)

protected:
    void paintEvent(QPaintEvent *ev);
    void mousePressEvent(QMouseEvent *ev);
    void mouseMoveEvent(QMouseEvent *ev);
    void mouseReleaseEvent(QMouseEvent *ev);
    void wheelEvent(QWheelEvent *ev);
    void keyPressEvent(QKeyEvent *ev);

private:
    struct ScreenTri;                        // projected triangle, defined in the .cpp

    Mat3 viewMatrix() const;
    bool project(const Vec3 &camPoint, QPointF *out) const;
    void buildScene(QVector<ScreenTri> &out) const;
    DocObject *pick(const QPoint &pos) const;
    void drawAxisCross(QPainter &p) const;
    void drawOriginAxes(QPainter &p, const Mat3 &view) const;

    Document *m_doc;
    DocObject *m_selection;
    DrawStyle m_style;

    // Orbit camera: the eye sits at m_target + distance * direction(yaw,pitch).
    Vec3 m_target;
    double m_yaw, m_pitch;                   // degrees
    double m_distance;

    QPoint m_lastPos;
    QPoint m_pressPos;
    bool m_rotating;
    bool m_panning;
};

#endif // FREECAD_VIEWPORT_H
