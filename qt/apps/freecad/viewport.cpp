/*
 * FreeCAD, ported to EwokOS - software 3D viewport.
 *
 * Coordinate conventions follow the CAD world: Z is up, the default
 * axonometric view looks at the origin from (+X, -Y, +Z), and the corner
 * axis cross uses the standard red/green/blue for X/Y/Z.
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
static const double kNear = 0.1;

// One projected, shaded triangle ready to draw; sorted far-to-near.
struct Viewport::ScreenTri {
    QPointF p[3];
    double depth;                            // mean camera-space z
    QColor fill;
    DocObject *obj;
};

Viewport::Viewport(Document *doc, QWidget *parent)
    : QWidget(parent), m_doc(doc), m_selection(0), m_style(ShadedWire),
      m_target(0, 0, 0), m_yaw(-45.0), m_pitch(35.264), m_distance(80.0),
      m_rotating(false), m_panning(false)
{
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 200);
    connect(m_doc, SIGNAL(changed()), this, SLOT(update()));
}

void Viewport::setSelection(DocObject *obj)
{
    if (m_selection == obj)
        return;
    m_selection = obj;
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
    double yaw = m_yaw * M_PI / 180.0;
    double pitch = m_pitch * M_PI / 180.0;
    Vec3 dir(cos(pitch) * cos(yaw), cos(pitch) * sin(yaw), sin(pitch));  // target -> eye
    Vec3 forward = dir * -1.0;
    Vec3 right = forward.cross(Vec3(0, 0, 1)).normalized();
    if (right.length() < 1e-6)
        right = Vec3(0, 1, 0);               // looking straight up/down
    Vec3 up = right.cross(forward).normalized();

    Mat3 v;
    v.m[0][0] = right.x;   v.m[0][1] = right.y;   v.m[0][2] = right.z;
    v.m[1][0] = up.x;      v.m[1][1] = up.y;      v.m[1][2] = up.z;
    v.m[2][0] = forward.x; v.m[2][1] = forward.y; v.m[2][2] = forward.z;
    return v;
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
    double yaw = m_yaw * M_PI / 180.0;
    double pitch = m_pitch * M_PI / 180.0;
    Vec3 eye = m_target + Vec3(cos(pitch) * cos(yaw), cos(pitch) * sin(yaw), sin(pitch)) * m_distance;

    // Headlight slightly up-left of the camera, in camera space.
    Vec3 light = Vec3(-0.3, 0.4, -0.86).normalized();

    const QList<DocObject *> &objs = m_doc->objects();
    for (int oi = 0; oi < objs.size(); oi++) {
        DocObject *obj = objs.at(oi);
        if (!obj->visible)
            continue;
        const Mesh &mesh = obj->mesh();
        Mat3 rot = obj->placement.rotation();

        // Transform the vertex pool once per object, not per triangle.
        QVector<Vec3> cam(mesh.verts.size());
        for (int i = 0; i < mesh.verts.size(); i++) {
            Vec3 world = rot.apply(mesh.verts.at(i)) + obj->placement.position;
            cam[i] = view.apply(world - eye);
        }

        // Selected shapes go FreeCAD-green.
        QColor base = (obj == m_selection)
            ? QColor(30, 210, 30)
            : obj->color;

        for (int ti = 0; ti < mesh.tris.size(); ti++) {
            const MeshTri &t = mesh.tris.at(ti);
            const Vec3 &a = cam.at(t.a), &b = cam.at(t.b), &c = cam.at(t.c);

            ScreenTri st;
            QPointF pa, pb, pc;
            if (!project(a, &pa) || !project(b, &pb) || !project(c, &pc))
                continue;                    // clipped at the near plane
            st.p[0] = pa; st.p[1] = pb; st.p[2] = pc;
            st.depth = (a.z + b.z + c.z) / 3.0;
            st.obj = obj;

            // Flat shading; abs() lights both sides so a stray winding
            // never renders black.
            Vec3 n = (b - a).cross(c - a).normalized();
            double lum = 0.25 + 0.75 * fabs(n.dot(light));
            st.fill = QColor((int)(base.red() * lum),
                             (int)(base.green() * lum),
                             (int)(base.blue() * lum));
            out.append(st);
        }
    }
    std::sort(out.begin(), out.end(),
              [](const ScreenTri &a, const ScreenTri &b) { return a.depth > b.depth; });
}

DocObject *Viewport::pick(const QPoint &pos) const
{
    QVector<ScreenTri> tris;
    buildScene(tris);

    // Nearest triangle whose projection contains the click.
    DocObject *hit = 0;
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
            hit = t.obj;
        }
    }
    return hit;
}

// ---- painting ---------------------------------------------------------------

void Viewport::drawOriginAxes(QPainter &p, const Mat3 &view) const
{
    double yaw = m_yaw * M_PI / 180.0;
    double pitch = m_pitch * M_PI / 180.0;
    Vec3 eye = m_target + Vec3(cos(pitch) * cos(yaw), cos(pitch) * sin(yaw), sin(pitch)) * m_distance;

    double len = m_distance * 0.35;
    static const struct { Vec3 dir; QColor color; } axes[3] = {
        { Vec3(1, 0, 0), QColor(200, 60, 60) },
        { Vec3(0, 1, 0), QColor(60, 160, 60) },
        { Vec3(0, 0, 1), QColor(70, 100, 220) },
    };
    for (int i = 0; i < 3; i++) {
        QPointF o, e;
        if (!project(view.apply(Vec3(0, 0, 0) - eye), &o))
            continue;
        if (!project(view.apply(axes[i].dir * len - eye), &e))
            continue;
        p.setPen(QPen(axes[i].color, 1, Qt::DashLine));
        p.drawLine(o, e);
    }
}

void Viewport::drawAxisCross(QPainter &p) const
{
    // Bottom-left navigation cross, rotation only - upstream's small
    // red/green/blue axis indicator.
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
        p.drawText(QRectF(end.x() - 7, end.y() - 7, 14, 14), Qt::AlignCenter, axes[i].label);
    }
}

void Viewport::paintEvent(QPaintEvent *ev)
{
    Q_UNUSED(ev);
    QPainter p(this);

    // Upstream's default gradient background.
    QLinearGradient grad(0, 0, 0, height());
    grad.setColorAt(0.0, QColor(77, 123, 178));
    grad.setColorAt(1.0, QColor(22, 35, 58));
    p.fillRect(rect(), grad);

    drawOriginAxes(p, viewMatrix());

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
            p.setPen(QPen(t.fill.lighter(130), 1));
            p.setBrush(Qt::NoBrush);
            break;
        case ShadedWire:
            p.setPen(QPen(t.fill.darker(150), 1));
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
    // A press-release without real dragging is a pick, like upstream's
    // CAD navigation style.
    if (ev->button() == Qt::LeftButton && !m_panning
        && (ev->pos() - m_pressPos).manhattanLength() < 4) {
        DocObject *hit = pick(ev->pos());
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
    if (m_distance < 0.5) m_distance = 0.5;
    if (m_distance > 100000.0) m_distance = 100000.0;
    update();
}

void Viewport::keyPressEvent(QKeyEvent *ev)
{
    // Upstream's standard view hotkeys: 0 axonometric, 1..6 the faces.
    switch (ev->key()) {
    case Qt::Key_0: setStdView(ViewAxonometric); return;
    case Qt::Key_1: setStdView(ViewFront); return;
    case Qt::Key_2: setStdView(ViewTop); return;
    case Qt::Key_3: setStdView(ViewRight); return;
    case Qt::Key_4: setStdView(ViewRear); return;
    case Qt::Key_5: setStdView(ViewBottom); return;
    case Qt::Key_6: setStdView(ViewLeft); return;
    default:
        QWidget::keyPressEvent(ev);
    }
}

// ---- view commands ----------------------------------------------------------

void Viewport::setStdView(int view)
{
    switch (view) {
    case ViewAxonometric: m_yaw = -45.0;  m_pitch = 35.264; break;
    case ViewFront:       m_yaw = -90.0;  m_pitch = 0.0;    break;
    case ViewTop:         m_yaw = -90.0;  m_pitch = 89.9;   break;
    case ViewRight:       m_yaw = 0.0;    m_pitch = 0.0;    break;
    case ViewRear:        m_yaw = 90.0;   m_pitch = 0.0;    break;
    case ViewBottom:      m_yaw = -90.0;  m_pitch = -89.9;  break;
    case ViewLeft:        m_yaw = 180.0;  m_pitch = 0.0;    break;
    }
    update();
}

void Viewport::fitAll()
{
    // World-space bounding box over every visible object.
    bool any = false;
    Vec3 lo(1e30, 1e30, 1e30), hi(-1e30, -1e30, -1e30);
    const QList<DocObject *> &objs = m_doc->objects();
    for (int oi = 0; oi < objs.size(); oi++) {
        DocObject *obj = objs.at(oi);
        if (!obj->visible)
            continue;
        const Mesh &mesh = obj->mesh();
        for (int i = 0; i < mesh.verts.size(); i++) {
            Vec3 w = obj->placement.toWorld(mesh.verts.at(i));
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
        m_distance = 80.0;
    } else {
        m_target = (lo + hi) * 0.5;
        double radius = (hi - lo).length() * 0.5;
        if (radius < 1.0)
            radius = 1.0;
        m_distance = radius / sin(kFovDeg * M_PI / 360.0) * 1.15;
    }
    update();
}
