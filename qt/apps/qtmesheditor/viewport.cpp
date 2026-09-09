/*
 * QtMeshEditor, ported to EwokOS - software 3D viewport.
 *
 * Coordinate conventions follow Ogre/the DCC world: Y is up, the default
 * perspective view looks at the origin from (+X, +Y, +Z), the ground grid
 * lies on the XZ plane at y=0, and the corner axis cross uses red/green/blue
 * for X/Y/Z.
 */

#include "viewport.h"

#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <math.h>

#include <algorithm>

static const double kFovDeg = 45.0;
static const double kNear = 0.05;

// One projected, shaded triangle ready to draw; sorted far-to-near.
struct Viewport::ScreenTri {
    QPointF p[3];
    double depth;                            // mean camera-space z
    QColor fill;
    QColor edge;
    MeshEntity *ent;
};

Viewport::Viewport(Scene *scene, QWidget *parent)
    : QWidget(parent), m_scene(scene), m_selection(0), m_style(ShadedWire),
      m_showGrid(true), m_target(0, 0, 0), m_yaw(45.0), m_pitch(30.0),
      m_distance(10.0), m_rotating(false), m_panning(false)
{
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 200);
    connect(m_scene, SIGNAL(changed()), this, SLOT(update()));
}

void Viewport::setSelection(MeshEntity *ent)
{
    if (m_selection == ent)
        return;
    m_selection = ent;
    update();
}

void Viewport::setDrawStyle(DrawStyle style)
{
    m_style = style;
    update();
}

// ---- camera -----------------------------------------------------------------

// Rows of the view matrix are the camera's right/up/forward axes, so
// apply() takes a world vector into camera space (x right, y up, z into
// the screen).
Mat3 Viewport::viewMatrix() const
{
    Vec3 eye = eyePos();
    Vec3 forward = (m_target - eye).normalized();
    Vec3 right = forward.cross(Vec3(0, 1, 0)).normalized();
    if (right.length() < 1e-6)
        right = Vec3(1, 0, 0);               // looking straight up/down
    Vec3 up = right.cross(forward).normalized();

    Mat3 v;
    v.m[0][0] = right.x;   v.m[0][1] = right.y;   v.m[0][2] = right.z;
    v.m[1][0] = up.x;      v.m[1][1] = up.y;      v.m[1][2] = up.z;
    v.m[2][0] = forward.x; v.m[2][1] = forward.y; v.m[2][2] = forward.z;
    return v;
}

Vec3 Viewport::eyePos() const
{
    double yaw = m_yaw * M_PI / 180.0;
    double pitch = m_pitch * M_PI / 180.0;
    Vec3 dir(cos(pitch) * sin(yaw), sin(pitch), cos(pitch) * cos(yaw));
    return m_target + dir * m_distance;
}

bool Viewport::project(const Vec3 &camPoint, QPointF *out) const
{
    if (camPoint.z < kNear)
        return false;
    double f = (height() / 2.0) / tan(kFovDeg * M_PI / 360.0);
    out->setX(width() / 2.0 + f * camPoint.x / camPoint.z);
    out->setY(height() / 2.0 - f * camPoint.y / camPoint.z);
    return true;
}

// ---- scene ------------------------------------------------------------------

void Viewport::buildScene(QVector<ScreenTri> &out) const
{
    Mat3 view = viewMatrix();
    Vec3 eye = eyePos();

    // Headlight slightly up-left of the camera, in camera space.
    Vec3 light = Vec3(-0.3, 0.4, -0.86).normalized();

    const QList<MeshEntity *> &ents = m_scene->entities();
    for (int ei = 0; ei < ents.size(); ei++) {
        MeshEntity *ent = ents.at(ei);
        if (!ent->visible)
            continue;
        const Mesh &mesh = ent->mesh;
        Mat3 rot = ent->rotation();
        const Vec3 &s = ent->scale;
        const Vec3 &pos = ent->position;

        // Transform the vertex pool once per entity - not per triangle, and
        // not via toWorld(), which would rebuild the rotation matrix per
        // vertex on an imported mesh with thousands of them.
        QVector<Vec3> cam(mesh.verts.size());
        for (int i = 0; i < mesh.verts.size(); i++) {
            const Vec3 &v = mesh.verts.at(i);
            Vec3 world = rot.apply(Vec3(v.x * s.x, v.y * s.y, v.z * s.z)) + pos;
            cam[i] = view.apply(world - eye);
        }

        // Selected entities go bright; keep the base colour otherwise.
        QColor base = ent->diffuse;
        QColor edge = base.darker(160);
        if (ent == m_selection) {
            base = base.lighter(150);
            edge = QColor(255, 210, 60);
        }

        for (int ti = 0; ti < mesh.tris.size(); ti++) {
            const MeshTri &t = mesh.tris.at(ti);
            const Vec3 &a = cam.at(t.a), &b = cam.at(t.b), &c = cam.at(t.c);

            ScreenTri st;
            QPointF pa, pb, pc;
            if (!project(a, &pa) || !project(b, &pb) || !project(c, &pc))
                continue;                    // clipped at the near plane
            st.p[0] = pa; st.p[1] = pb; st.p[2] = pc;
            st.depth = (a.z + b.z + c.z) / 3.0;
            st.ent = ent;

            // Flat shading; abs() lights both sides so a stray winding
            // never renders black.
            Vec3 n = (b - a).cross(c - a).normalized();
            double lum = 0.28 + 0.72 * fabs(n.dot(light));
            st.fill = QColor::fromRgbF(qMin(1.0, base.redF() * lum),
                                       qMin(1.0, base.greenF() * lum),
                                       qMin(1.0, base.blueF() * lum));
            st.edge = edge;
            out.append(st);
        }
    }
    std::sort(out.begin(), out.end(),
              [](const ScreenTri &a, const ScreenTri &b) { return a.depth > b.depth; });
}

MeshEntity *Viewport::pick(const QPoint &pos) const
{
    QVector<ScreenTri> tris;
    buildScene(tris);

    // Nearest triangle whose projection contains the click.
    MeshEntity *hit = 0;
    double bestDepth = 1e30;
    QPointF p(pos);
    for (int i = 0; i < tris.size(); i++) {
        const ScreenTri &t = tris.at(i);
        // Same-side test, sign-agnostic so the winding on screen is free.
        double d1 = (t.p[1].x() - t.p[0].x()) * (p.y() - t.p[0].y()) - (t.p[1].y() - t.p[0].y()) * (p.x() - t.p[0].x());
        double d2 = (t.p[2].x() - t.p[1].x()) * (p.y() - t.p[1].y()) - (t.p[2].y() - t.p[1].y()) * (p.x() - t.p[1].x());
        double d3 = (t.p[0].x() - t.p[2].x()) * (p.y() - t.p[2].y()) - (t.p[0].y() - t.p[2].y()) * (p.x() - t.p[2].x());
        bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        bool pos_ = (d1 > 0) || (d2 > 0) || (d3 > 0);
        if (neg && pos_)
            continue;
        if (t.depth < bestDepth) {
            bestDepth = t.depth;
            hit = t.ent;
        }
    }
    return hit;
}

// ---- painting ---------------------------------------------------------------

void Viewport::drawGrid(QPainter &p, const Mat3 &view, const Vec3 &eye) const
{
    // Ground grid on XZ at y=0, one line per unit out to +/-8, with the two
    // axis lines tinted red (X) and blue (Z) like a DCC viewport.
    const int n = 8;
    p.setPen(QPen(QColor(70, 74, 82), 1));
    for (int i = -n; i <= n; i++) {
        bool onAxis = (i == 0);
        // Line parallel to Z at x=i, and line parallel to X at z=i.
        for (int axis = 0; axis < 2; axis++) {
            Vec3 a = (axis == 0) ? Vec3(i, 0, -n) : Vec3(-n, 0, i);
            Vec3 b = (axis == 0) ? Vec3(i, 0, n)  : Vec3(n, 0, i);
            QPointF pa, pb;
            if (!project(view.apply(a - eye), &pa) || !project(view.apply(b - eye), &pb))
                continue;
            if (onAxis)
                p.setPen(QPen(axis == 0 ? QColor(150, 70, 70) : QColor(80, 110, 200), 1));
            else
                p.setPen(QPen(QColor(70, 74, 82), 1));
            p.drawLine(pa, pb);
        }
    }
}

void Viewport::drawAxisCross(QPainter &p) const
{
    // Bottom-left navigation cross, rotation only - the small red/green/blue
    // axis indicator every DCC viewport has.
    Mat3 view = viewMatrix();
    QPointF center(38, height() - 38);
    double len = 26;

    static const struct { Vec3 dir; QColor color; const char *label; } axes[3] = {
        { Vec3(1, 0, 0), QColor(220, 60, 60), "X" },
        { Vec3(0, 1, 0), QColor(60, 190, 60), "Y" },
        { Vec3(0, 0, 1), QColor(80, 110, 230), "Z" },
    };
    p.setRenderHint(QPainter::Antialiasing, true);
    for (int i = 0; i < 3; i++) {
        Vec3 v = view.apply(axes[i].dir);
        QPointF end = center + QPointF(v.x * len, -v.y * len);
        p.setPen(QPen(axes[i].color, 2));
        p.drawLine(center, end);
        p.setPen(QColor(230, 230, 230));
        p.drawText(QRectF(end.x() - 7, end.y() - 7, 14, 14), Qt::AlignCenter, axes[i].label);
    }
}

void Viewport::paintEvent(QPaintEvent *ev)
{
    Q_UNUSED(ev);
    QPainter p(this);

    // A dark studio gradient, the neutral backdrop a mesh editor wants.
    QLinearGradient grad(0, 0, 0, height());
    grad.setColorAt(0.0, QColor(58, 62, 70));
    grad.setColorAt(1.0, QColor(28, 30, 34));
    p.fillRect(rect(), grad);

    Mat3 view = viewMatrix();
    Vec3 eye = eyePos();
    if (m_showGrid)
        drawGrid(p, view, eye);

    QVector<ScreenTri> tris;
    buildScene(tris);

    p.setRenderHint(QPainter::Antialiasing, true);
    for (int i = 0; i < tris.size(); i++) {
        const ScreenTri &t = tris.at(i);
        QPointF poly[3] = { t.p[0], t.p[1], t.p[2] };
        switch (m_style) {
        case Shaded:
            p.setPen(Qt::NoPen);
            p.setBrush(t.fill);
            break;
        case Wireframe:
            p.setPen(QPen(t.edge.lighter(140), 1));
            p.setBrush(Qt::NoBrush);
            break;
        case ShadedWire:
            p.setPen(QPen(t.edge, 1));
            p.setBrush(t.fill);
            break;
        }
        p.drawPolygon(poly, 3);
    }

    drawAxisCross(p);
}

// ---- interaction ------------------------------------------------------------

void Viewport::mousePressEvent(QMouseEvent *ev)
{
    m_lastPos = ev->pos();
    m_pressPos = ev->pos();
    m_rotating = false;
    m_panning = false;
    if (ev->button() == Qt::MiddleButton
        || (ev->button() == Qt::LeftButton && (ev->modifiers() & Qt::ShiftModifier)))
        m_panning = true;
    else if (ev->button() == Qt::LeftButton)
        m_rotating = true;
    setFocus();
}

void Viewport::mouseMoveEvent(QMouseEvent *ev)
{
    QPoint delta = ev->pos() - m_lastPos;
    m_lastPos = ev->pos();

    if (m_panning) {
        // Move the target in the camera plane; scale so a pixel is a pixel
        // at the target's depth.
        Mat3 view = viewMatrix();
        double f = (height() / 2.0) / tan(kFovDeg * M_PI / 360.0);
        double s = m_distance / f;
        Vec3 right(view.m[0][0], view.m[0][1], view.m[0][2]);
        Vec3 up(view.m[1][0], view.m[1][1], view.m[1][2]);
        m_target = m_target - right * (delta.x() * s) + up * (delta.y() * s);
        update();
    } else if (m_rotating) {
        m_yaw -= delta.x() * 0.5;
        m_pitch += delta.y() * 0.5;
        if (m_pitch > 89.9) m_pitch = 89.9;
        if (m_pitch < -89.9) m_pitch = -89.9;
        update();
    }
}

void Viewport::mouseReleaseEvent(QMouseEvent *ev)
{
    // A press-release without real dragging is a pick.
    if (ev->button() == Qt::LeftButton && !m_panning
        && (ev->pos() - m_pressPos).manhattanLength() < 4) {
        MeshEntity *hit = pick(ev->pos());
        setSelection(hit);
        emit selectionChanged(hit);
    }
    m_rotating = false;
    m_panning = false;
}

void Viewport::wheelEvent(QWheelEvent *ev)
{
    double steps = ev->angleDelta().y() / 120.0;
    m_distance *= pow(1.0 / 1.15, steps);
    if (m_distance < 0.2) m_distance = 0.2;
    if (m_distance > 100000.0) m_distance = 100000.0;
    update();
}

void Viewport::keyPressEvent(QKeyEvent *ev)
{
    // Standard view hotkeys: 0 perspective, 1..6 the orthographic faces.
    switch (ev->key()) {
    case Qt::Key_0: setStdView(ViewPerspective); return;
    case Qt::Key_1: setStdView(ViewFront); return;
    case Qt::Key_2: setStdView(ViewBack); return;
    case Qt::Key_3: setStdView(ViewTop); return;
    case Qt::Key_4: setStdView(ViewBottom); return;
    case Qt::Key_5: setStdView(ViewLeft); return;
    case Qt::Key_6: setStdView(ViewRight); return;
    default:
        QWidget::keyPressEvent(ev);
    }
}

// ---- view commands ----------------------------------------------------------

void Viewport::setStdView(int view)
{
    switch (view) {
    case ViewPerspective: m_yaw = 45.0;  m_pitch = 30.0;  break;
    case ViewFront:       m_yaw = 0.0;   m_pitch = 0.0;   break;
    case ViewBack:        m_yaw = 180.0; m_pitch = 0.0;   break;
    case ViewTop:         m_yaw = 0.0;   m_pitch = 89.9;  break;
    case ViewBottom:      m_yaw = 0.0;   m_pitch = -89.9; break;
    case ViewLeft:        m_yaw = -90.0; m_pitch = 0.0;   break;
    case ViewRight:       m_yaw = 90.0;  m_pitch = 0.0;   break;
    }
    update();
}

void Viewport::fitAll()
{
    // World-space bounding box over every visible entity.
    bool any = false;
    Vec3 lo(1e30, 1e30, 1e30), hi(-1e30, -1e30, -1e30);
    const QList<MeshEntity *> &ents = m_scene->entities();
    for (int ei = 0; ei < ents.size(); ei++) {
        MeshEntity *ent = ents.at(ei);
        if (!ent->visible)
            continue;
        for (int i = 0; i < ent->mesh.verts.size(); i++) {
            Vec3 w = ent->toWorld(ent->mesh.verts.at(i));
            if (w.x < lo.x) lo.x = w.x;
            if (w.y < lo.y) lo.y = w.y;
            if (w.z < lo.z) lo.z = w.z;
            if (w.x > hi.x) hi.x = w.x;
            if (w.y > hi.y) hi.y = w.y;
            if (w.z > hi.z) hi.z = w.z;
            any = true;
        }
    }
    if (!any) {
        m_target = Vec3(0, 0, 0);
        m_distance = 10.0;
    } else {
        m_target = (lo + hi) * 0.5;
        double radius = (hi - lo).length() * 0.5;
        if (radius < 0.5)
            radius = 0.5;
        m_distance = radius / sin(kFovDeg * M_PI / 360.0) * 1.15;
    }
    update();
}
