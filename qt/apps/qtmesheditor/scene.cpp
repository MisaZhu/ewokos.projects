/*
 * QtMeshEditor, ported to EwokOS - scene implementation.
 *
 * The procedural primitives mirror the shapes upstream gets from
 * ogre-procedural: unit-ish sizes around the origin, Y up.  Zero-area
 * triangles at sphere/torus poles are tolerated - the software viewport
 * simply rasterises them to nothing.
 */

#include "scene.h"

#include <math.h>

// ---- math -------------------------------------------------------------------

double Vec3::length() const
{
    return sqrt(x * x + y * y + z * z);
}

Vec3 Vec3::normalized() const
{
    double l = length();
    if (l < 1e-12)
        return Vec3(0, 0, 0);
    return Vec3(x / l, y / l, z / l);
}

Mat3::Mat3()
{
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            m[r][c] = (r == c) ? 1.0 : 0.0;
}

Mat3 Mat3::rotX(double deg)
{
    double a = deg * M_PI / 180.0, c = cos(a), s = sin(a);
    Mat3 r;
    r.m[1][1] = c; r.m[1][2] = -s;
    r.m[2][1] = s; r.m[2][2] = c;
    return r;
}

Mat3 Mat3::rotY(double deg)
{
    double a = deg * M_PI / 180.0, c = cos(a), s = sin(a);
    Mat3 r;
    r.m[0][0] = c;  r.m[0][2] = s;
    r.m[2][0] = -s; r.m[2][2] = c;
    return r;
}

Mat3 Mat3::rotZ(double deg)
{
    double a = deg * M_PI / 180.0, c = cos(a), s = sin(a);
    Mat3 r;
    r.m[0][0] = c; r.m[0][1] = -s;
    r.m[1][0] = s; r.m[1][1] = c;
    return r;
}

Mat3 Mat3::operator*(const Mat3 &o) const
{
    Mat3 r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j];
    return r;
}

Vec3 Mat3::apply(const Vec3 &v) const
{
    return Vec3(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z);
}

// ---- mesh -------------------------------------------------------------------

void Mesh::bounds(Vec3 *lo, Vec3 *hi) const
{
    if (verts.isEmpty()) {
        *lo = Vec3(0, 0, 0);
        *hi = Vec3(0, 0, 0);
        return;
    }
    *lo = Vec3(1e30, 1e30, 1e30);
    *hi = Vec3(-1e30, -1e30, -1e30);
    for (int i = 0; i < verts.size(); i++) {
        const Vec3 &v = verts.at(i);
        if (v.x < lo->x) lo->x = v.x;
        if (v.y < lo->y) lo->y = v.y;
        if (v.z < lo->z) lo->z = v.z;
        if (v.x > hi->x) hi->x = v.x;
        if (v.y > hi->y) hi->y = v.y;
        if (v.z > hi->z) hi->z = v.z;
    }
}

// ---- entities ---------------------------------------------------------------

MeshEntity::MeshEntity(const QString &name_)
    : name(name_), visible(true), scale(1, 1, 1), diffuse(178, 178, 178)
{
}

// ---- scene ------------------------------------------------------------------

Scene::Scene(QObject *parent)
    : QObject(parent)
{
}

Scene::~Scene()
{
    qDeleteAll(m_entities);
}

QString Scene::primitiveName(Primitive prim)
{
    switch (prim) {
    case Cube:     return QString("Cube");
    case Plane:    return QString("Plane");
    case Sphere:   return QString("Sphere");
    case Cylinder: return QString("Cylinder");
    case Cone:     return QString("Cone");
    case Torus:    return QString("Torus");
    }
    return QString("Entity");
}

// "Cube001"-style names, unique and stable like upstream's Ogre node names.
QString Scene::uniqueName(const QString &base) const
{
    for (int n = 1; ; n++) {
        QString name = QString("%1%2").arg(base).arg(n, 3, 10, QChar('0'));
        bool taken = false;
        for (int i = 0; i < m_entities.size(); i++) {
            if (m_entities.at(i)->name == name) {
                taken = true;
                break;
            }
        }
        if (!taken)
            return name;
    }
}

// Entity colours rotate through a small palette so a freshly built scene
// is readable without touching the material editor.
static QColor nextColor(int index)
{
    static const QColor palette[] = {
        QColor(178, 178, 178), QColor(204, 120, 96), QColor(110, 160, 200),
        QColor(140, 190, 120), QColor(200, 180, 100), QColor(170, 130, 190),
    };
    return palette[index % 6];
}

MeshEntity *Scene::addPrimitive(Primitive prim)
{
    MeshEntity *ent = new MeshEntity(uniqueName(primitiveName(prim)));
    ent->mesh = makePrimitive(prim);
    ent->diffuse = nextColor(m_entities.size());
    m_entities.append(ent);
    emit structureChanged();
    emit changed();
    return ent;
}

MeshEntity *Scene::addMesh(const QString &baseName, const Mesh &mesh, const QString &sourcePath)
{
    MeshEntity *ent = new MeshEntity(uniqueName(baseName));
    ent->mesh = mesh;
    ent->sourcePath = sourcePath;
    ent->diffuse = nextColor(m_entities.size());
    m_entities.append(ent);
    emit structureChanged();
    emit changed();
    return ent;
}

MeshEntity *Scene::duplicate(MeshEntity *ent)
{
    // Upstream's Ctrl+D: same mesh and material on a fresh node.
    MeshEntity *copy = new MeshEntity(uniqueName(ent->name));
    copy->mesh = ent->mesh;
    copy->visible = ent->visible;
    copy->position = ent->position;
    copy->rotationDeg = ent->rotationDeg;
    copy->scale = ent->scale;
    copy->diffuse = ent->diffuse;
    copy->sourcePath = ent->sourcePath;
    m_entities.append(copy);
    emit structureChanged();
    emit changed();
    return copy;
}

void Scene::removeEntity(MeshEntity *ent)
{
    if (m_entities.removeOne(ent)) {
        delete ent;
        emit structureChanged();
        emit changed();
    }
}

void Scene::clear()
{
    if (m_entities.isEmpty())
        return;
    qDeleteAll(m_entities);
    m_entities.clear();
    emit structureChanged();
    emit changed();
}

void Scene::touch()
{
    emit changed();
}

// ---- procedural primitives ----------------------------------------------------

static void addQuad(Mesh &m, int a, int b, int c, int d)
{
    m.tris.append(MeshTri(a, b, c));
    m.tris.append(MeshTri(a, c, d));
}

Mesh Scene::makePrimitive(Primitive prim)
{
    Mesh m;
    const int segs = 32, stacks = 16;

    switch (prim) {
    case Cube: {
        // 2x2x2 centered on the origin.
        static const double v[8][3] = {
            { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
            { -1, -1,  1 }, { 1, -1,  1 }, { 1, 1,  1 }, { -1, 1,  1 },
        };
        for (int i = 0; i < 8; i++)
            m.verts.append(Vec3(v[i][0], v[i][1], v[i][2]));
        addQuad(m, 0, 1, 2, 3);              // back  (-Z)
        addQuad(m, 5, 4, 7, 6);              // front (+Z)
        addQuad(m, 4, 0, 3, 7);              // left  (-X)
        addQuad(m, 1, 5, 6, 2);              // right (+X)
        addQuad(m, 3, 2, 6, 7);              // top   (+Y)
        addQuad(m, 4, 5, 1, 0);              // bottom(-Y)
        break;
    }
    case Plane: {
        // 4x4 ground patch on XZ.
        m.verts.append(Vec3(-2, 0, -2));
        m.verts.append(Vec3(2, 0, -2));
        m.verts.append(Vec3(2, 0, 2));
        m.verts.append(Vec3(-2, 0, 2));
        addQuad(m, 0, 1, 2, 3);
        break;
    }
    case Sphere: {
        // Radius 1; rings pole-to-pole, degenerate pole quads tolerated.
        for (int st = 0; st <= stacks; st++) {
            double phi = M_PI * st / stacks - M_PI / 2.0;
            double y = sin(phi), r = cos(phi);
            for (int sg = 0; sg < segs; sg++) {
                double th = 2.0 * M_PI * sg / segs;
                m.verts.append(Vec3(r * cos(th), y, r * sin(th)));
            }
        }
        for (int st = 0; st < stacks; st++) {
            for (int sg = 0; sg < segs; sg++) {
                int a = st * segs + sg;
                int b = st * segs + (sg + 1) % segs;
                int c = (st + 1) * segs + (sg + 1) % segs;
                int d = (st + 1) * segs + sg;
                addQuad(m, a, b, c, d);
            }
        }
        break;
    }
    case Cylinder: {
        // Radius 1, height 2 centered: two rings plus cap centers.
        for (int sg = 0; sg < segs; sg++) {
            double th = 2.0 * M_PI * sg / segs;
            m.verts.append(Vec3(cos(th), -1, sin(th)));
        }
        for (int sg = 0; sg < segs; sg++) {
            double th = 2.0 * M_PI * sg / segs;
            m.verts.append(Vec3(cos(th), 1, sin(th)));
        }
        int bc = m.verts.size(); m.verts.append(Vec3(0, -1, 0));
        int tc = m.verts.size(); m.verts.append(Vec3(0, 1, 0));
        for (int sg = 0; sg < segs; sg++) {
            int n = (sg + 1) % segs;
            addQuad(m, sg, n, segs + n, segs + sg);          // side
            m.tris.append(MeshTri(bc, n, sg));               // bottom cap
            m.tris.append(MeshTri(tc, segs + sg, segs + n)); // top cap
        }
        break;
    }
    case Cone: {
        // Radius 1 base at y=-1, apex at y=+1.
        for (int sg = 0; sg < segs; sg++) {
            double th = 2.0 * M_PI * sg / segs;
            m.verts.append(Vec3(cos(th), -1, sin(th)));
        }
        int apex = m.verts.size(); m.verts.append(Vec3(0, 1, 0));
        int bc = m.verts.size(); m.verts.append(Vec3(0, -1, 0));
        for (int sg = 0; sg < segs; sg++) {
            int n = (sg + 1) % segs;
            m.tris.append(MeshTri(apex, sg, n));             // flank
            m.tris.append(MeshTri(bc, n, sg));               // base cap
        }
        break;
    }
    case Torus: {
        // Major radius 1, tube radius 0.4, ring around Y.
        const int maj = 32, min_ = 16;
        const double R = 1.0, r = 0.4;
        for (int i = 0; i < maj; i++) {
            double u = 2.0 * M_PI * i / maj;
            for (int j = 0; j < min_; j++) {
                double v = 2.0 * M_PI * j / min_;
                double d = R + r * cos(v);
                m.verts.append(Vec3(d * cos(u), r * sin(v), d * sin(u)));
            }
        }
        for (int i = 0; i < maj; i++) {
            for (int j = 0; j < min_; j++) {
                int a = i * min_ + j;
                int b = i * min_ + (j + 1) % min_;
                int c = ((i + 1) % maj) * min_ + (j + 1) % min_;
                int d = ((i + 1) % maj) * min_ + j;
                addQuad(m, a, b, c, d);
            }
        }
        break;
    }
    }
    return m;
}
