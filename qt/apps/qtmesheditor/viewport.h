/*
 * QtMeshEditor, ported to EwokOS - the 3D viewport.
 *
 * Upstream renders through Ogre3D on OpenGL; this Qt build has neither
 * (QT_FEATURE_opengl is -1 on the ewokos QPA), so the viewport is a plain
 * QWidget that renders the scene itself: node transform (scale -> rotate ->
 * translate), a Y-up look-at camera, perspective projection, painter's-
 * algorithm depth sort and flat-shaded QPainter polygons.  What it keeps
 * from upstream is the feel: the dark studio gradient, the ground grid, the
 * corner RGB axis cross, the bright selection tint, and orbit/pan/zoom mouse
 * navigation plus the numeric standard-view hotkeys.
 */

#ifndef QME_VIEWPORT_H
#define QME_VIEWPORT_H

#include <QWidget>

#include "scene.h"

class Viewport : public QWidget {
    Q_OBJECT
public:
    explicit Viewport(Scene *scene, QWidget *parent = 0);

    enum DrawStyle { Shaded, Wireframe, ShadedWire };
    enum StdView { ViewPerspective, ViewFront, ViewBack, ViewTop, ViewBottom, ViewLeft, ViewRight };

    MeshEntity *selection() const { return m_selection; }
    void setSelection(MeshEntity *ent);
    void setDrawStyle(DrawStyle style);
    DrawStyle drawStyle() const { return m_style; }
    void setShowGrid(bool on) { m_showGrid = on; update(); }
    bool showGrid() const { return m_showGrid; }

public slots:
    void fitAll();
    void setStdView(int view);               // StdView value

signals:
    void selectionChanged(MeshEntity *ent);  // picked in the 3D view (may be 0)

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
    Vec3 eyePos() const;
    bool project(const Vec3 &camPoint, QPointF *out) const;
    void buildScene(QVector<ScreenTri> &out) const;
    MeshEntity *pick(const QPoint &pos) const;
    void drawAxisCross(QPainter &p) const;
    void drawGrid(QPainter &p, const Mat3 &view, const Vec3 &eye) const;

    Scene *m_scene;
    MeshEntity *m_selection;
    DrawStyle m_style;
    bool m_showGrid;

    // Orbit camera: the eye sits at m_target + distance * direction(yaw,pitch).
    Vec3 m_target;
    double m_yaw, m_pitch;                   // degrees
    double m_distance;

    QPoint m_lastPos;
    QPoint m_pressPos;
    bool m_rotating;
    bool m_panning;
};

#endif // QME_VIEWPORT_H
