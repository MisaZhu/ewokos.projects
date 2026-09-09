/*
 * FreeCAD, ported to EwokOS - document implementation.
 *
 * Tessellation replaces OpenCASCADE: every primitive builds its own triangle
 * mesh with the same origin conventions as upstream's Part workbench (Box
 * grows from its corner into +X/+Y/+Z, the round solids sit on/around the
 * local Z axis).
 */

#include "document.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

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

static inline double deg2rad(double d) { return d * M_PI / 180.0; }

Mat3 Mat3::rotX(double deg)
{
    double s = sin(deg2rad(deg)), c = cos(deg2rad(deg));
    Mat3 r;
    r.m[1][1] = c; r.m[1][2] = -s;
    r.m[2][1] = s; r.m[2][2] = c;
    return r;
}

Mat3 Mat3::rotY(double deg)
{
    double s = sin(deg2rad(deg)), c = cos(deg2rad(deg));
    Mat3 r;
    r.m[0][0] = c;  r.m[0][2] = s;
    r.m[2][0] = -s; r.m[2][2] = c;
    return r;
}

Mat3 Mat3::rotZ(double deg)
{
    double s = sin(deg2rad(deg)), c = cos(deg2rad(deg));
    Mat3 r;
    r.m[0][0] = c; r.m[0][1] = -s;
    r.m[1][0] = s; r.m[1][1] = c;
    return r;
}

Mat3 Mat3::operator*(const Mat3 &o) const
{
    Mat3 r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            double s = 0;
            for (int k = 0; k < 3; k++)
                s += m[i][k] * o.m[k][j];
            r.m[i][j] = s;
        }
    return r;
}

Vec3 Mat3::apply(const Vec3 &v) const
{
    return Vec3(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z);
}

// ---- DocObject --------------------------------------------------------------

// Upstream's default shape color, the familiar FreeCAD gray.
static const QColor kDefaultShapeColor(204, 204, 204);

// Tessellation resolution for the round primitives.  Fixed rather than a
// per-object "Angular Deflection": the painter's-algorithm viewport pays per
// triangle, and these are the sweet spot on a software renderer.
static const int kSegments = 32;             // around an axis
static const int kStacks = 16;               // along a sphere/torus section

DocObject::DocObject(Type type, const QString &name)
    : color(kDefaultShapeColor), visible(true),
      m_type(type), m_name(name), m_label(name), m_meshDirty(true)
{
    // Upstream Part workbench defaults, in millimeters.
    switch (type) {
    case Box:
        m_params["Length"] = 10.0;
        m_params["Width"] = 10.0;
        m_params["Height"] = 10.0;
        break;
    case Cylinder:
        m_params["Radius"] = 2.0;
        m_params["Height"] = 10.0;
        break;
    case Sphere:
        m_params["Radius"] = 5.0;
        break;
    case Cone:
        m_params["Radius1"] = 2.0;
        m_params["Radius2"] = 4.0;
        m_params["Height"] = 10.0;
        break;
    case Torus:
        m_params["Radius1"] = 10.0;
        m_params["Radius2"] = 2.0;
        break;
    }
}

QString DocObject::typeName(Type t)
{
    switch (t) {
    case Box:      return "Box";
    case Cylinder: return "Cylinder";
    case Sphere:   return "Sphere";
    case Cone:     return "Cone";
    case Torus:    return "Torus";
    }
    return "Box";
}

QStringList DocObject::paramNames() const
{
    // Deterministic order for the property panel, not QMap's sort order.
    switch (m_type) {
    case Box:      return QStringList() << "Length" << "Width" << "Height";
    case Cylinder: return QStringList() << "Radius" << "Height";
    case Sphere:   return QStringList() << "Radius";
    case Cone:     return QStringList() << "Radius1" << "Radius2" << "Height";
    case Torus:    return QStringList() << "Radius1" << "Radius2";
    }
    return QStringList();
}

void DocObject::setParam(const QString &key, double v)
{
    if (!m_params.contains(key))
        return;
    m_params[key] = v;
    m_meshDirty = true;
}

const Mesh &DocObject::mesh() const
{
    if (m_meshDirty) {
        rebuildMesh();
        m_meshDirty = false;
    }
    return m_mesh;
}

// Helpers shared by the round solids: a ring of vertices around Z.
static void addRing(Mesh &mesh, double radius, double z)
{
    for (int i = 0; i < kSegments; i++) {
        double a = 2.0 * M_PI * i / kSegments;
        mesh.verts.append(Vec3(radius * cos(a), radius * sin(a), z));
    }
}

// Quads between two rings (bottom ring index b0, top ring index t0),
// wound CCW seen from outside.
static void stitchRings(Mesh &mesh, int b0, int t0)
{
    for (int i = 0; i < kSegments; i++) {
        int j = (i + 1) % kSegments;
        mesh.tris.append(MeshTri(b0 + i, b0 + j, t0 + j));
        mesh.tris.append(MeshTri(b0 + i, t0 + j, t0 + i));
    }
}

// A cap fan around a center vertex.  up=true caps the top (+Z normal).
static void capRing(Mesh &mesh, int ring0, double z, bool up)
{
    int center = mesh.verts.size();
    mesh.verts.append(Vec3(0, 0, z));
    for (int i = 0; i < kSegments; i++) {
        int j = (i + 1) % kSegments;
        if (up)
            mesh.tris.append(MeshTri(center, ring0 + i, ring0 + j));
        else
            mesh.tris.append(MeshTri(center, ring0 + j, ring0 + i));
    }
}

void DocObject::rebuildMesh() const
{
    Mesh &mesh = m_mesh;
    mesh.clear();

    switch (m_type) {
    case Box: {
        double l = param("Length"), w = param("Width"), h = param("Height");
        // Corner at the origin, like upstream.
        for (int i = 0; i < 8; i++)
            mesh.verts.append(Vec3((i & 1) ? l : 0, (i & 2) ? w : 0, (i & 4) ? h : 0));
        static const int f[6][4] = {
            {0, 2, 3, 1},                    // bottom (-Z)
            {4, 5, 7, 6},                    // top (+Z)
            {0, 1, 5, 4},                    // front (-Y)
            {2, 6, 7, 3},                    // back (+Y)
            {0, 4, 6, 2},                    // left (-X)
            {1, 3, 7, 5},                    // right (+X)
        };
        for (int i = 0; i < 6; i++) {
            mesh.tris.append(MeshTri(f[i][0], f[i][1], f[i][2]));
            mesh.tris.append(MeshTri(f[i][0], f[i][2], f[i][3]));
        }
        break;
    }
    case Cylinder: {
        double r = param("Radius"), h = param("Height");
        addRing(mesh, r, 0);
        addRing(mesh, r, h);
        stitchRings(mesh, 0, kSegments);
        capRing(mesh, 0, 0, false);
        capRing(mesh, kSegments, h, true);
        break;
    }
    case Cone: {
        double r1 = param("Radius1"), r2 = param("Radius2"), h = param("Height");
        addRing(mesh, r1, 0);
        addRing(mesh, r2, h);
        stitchRings(mesh, 0, kSegments);
        if (r1 > 1e-9)
            capRing(mesh, 0, 0, false);
        if (r2 > 1e-9)
            capRing(mesh, kSegments, h, true);
        break;
    }
    case Sphere: {
        double r = param("Radius");
        // Stacked rings from south to north pole; the poles are rings too
        // (degenerate triangles at radius ~0 shade to nothing and keep the
        // stitching loop uniform).
        for (int s = 0; s <= kStacks; s++) {
            double phi = M_PI * s / kStacks - M_PI / 2.0;  // -90..90
            addRing(mesh, r * cos(phi), r * sin(phi));
        }
        for (int s = 0; s < kStacks; s++)
            stitchRings(mesh, s * kSegments, (s + 1) * kSegments);
        break;
    }
    case Torus: {
        double R = param("Radius1"), r = param("Radius2");
        for (int s = 0; s < kStacks; s++) {
            double phi = 2.0 * M_PI * s / kStacks;
            addRing(mesh, R + r * cos(phi), r * sin(phi));
        }
        for (int s = 0; s < kStacks; s++)
            stitchRings(mesh, s * kSegments, ((s + 1) % kStacks) * kSegments);
        break;
    }
    }
}

static QJsonObject vecToJson(const Vec3 &v)
{
    QJsonObject o;
    o["x"] = v.x;
    o["y"] = v.y;
    o["z"] = v.z;
    return o;
}

static Vec3 vecFromJson(const QJsonObject &o)
{
    return Vec3(o["x"].toDouble(), o["y"].toDouble(), o["z"].toDouble());
}

QJsonObject DocObject::toJson() const
{
    QJsonObject o;
    o["type"] = typeName(m_type);
    o["name"] = m_name;
    o["label"] = m_label;
    o["color"] = color.name();
    o["visible"] = visible;
    o["position"] = vecToJson(placement.position);
    o["rotation"] = vecToJson(placement.rotationDeg);
    QJsonObject params;
    for (QMap<QString, double>::const_iterator it = m_params.begin(); it != m_params.end(); ++it)
        params[it.key()] = it.value();
    o["params"] = params;
    return o;
}

DocObject *DocObject::fromJson(const QJsonObject &o)
{
    QString tn = o["type"].toString();
    Type type = Box;
    for (int t = Box; t <= Torus; t++)
        if (typeName((Type)t) == tn)
            type = (Type)t;

    DocObject *obj = new DocObject(type, o["name"].toString());
    obj->m_label = o["label"].toString(obj->m_name);
    obj->color = QColor(o["color"].toString());
    if (!obj->color.isValid())
        obj->color = kDefaultShapeColor;
    obj->visible = o["visible"].toBool(true);
    obj->placement.position = vecFromJson(o["position"].toObject());
    obj->placement.rotationDeg = vecFromJson(o["rotation"].toObject());
    QJsonObject params = o["params"].toObject();
    for (QJsonObject::const_iterator it = params.begin(); it != params.end(); ++it)
        obj->setParam(it.key(), it.value().toDouble());
    return obj;
}

// ---- Document ---------------------------------------------------------------

Document::Document(QObject *parent)
    : QObject(parent), m_modified(false)
{
}

Document::~Document()
{
    qDeleteAll(m_objects);
}

QString Document::uniqueName(DocObject::Type type) const
{
    // Upstream numbering: Box, Box001, Box002...
    QString base = DocObject::typeName(type);
    for (int n = 0; ; n++) {
        QString name = (n == 0) ? base
                                : QString("%1%2").arg(base).arg(n, 3, 10, QChar('0'));
        bool taken = false;
        for (int i = 0; i < m_objects.size(); i++)
            if (m_objects.at(i)->name() == name)
                taken = true;
        if (!taken)
            return name;
    }
}

DocObject *Document::addObject(DocObject::Type type)
{
    DocObject *obj = new DocObject(type, uniqueName(type));
    m_objects.append(obj);
    m_modified = true;
    emit structureChanged();
    emit changed();
    return obj;
}

void Document::removeObject(DocObject *obj)
{
    if (!m_objects.removeOne(obj))
        return;
    delete obj;
    m_modified = true;
    emit structureChanged();
    emit changed();
}

void Document::clear()
{
    qDeleteAll(m_objects);
    m_objects.clear();
    m_filePath.clear();
    m_modified = false;
    emit structureChanged();
    emit changed();
}

void Document::touch()
{
    m_modified = true;
    emit changed();
}

QString Document::displayName() const
{
    if (m_filePath.isEmpty())
        return "Unnamed";
    return QFileInfo(m_filePath).completeBaseName();
}

bool Document::saveAs(const QString &path, QString *error)
{
    QJsonArray arr;
    for (int i = 0; i < m_objects.size(); i++)
        arr.append(m_objects.at(i)->toJson());
    QJsonObject root;
    root["application"] = "FreeCAD-ewokos";
    root["objects"] = arr;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();

    m_filePath = path;
    m_modified = false;
    emit structureChanged();
    return true;
}

bool Document::load(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QJsonParseError perr;
    QJsonDocument jdoc = QJsonDocument::fromJson(f.readAll(), &perr);
    f.close();
    if (jdoc.isNull() || !jdoc.isObject()) {
        if (error)
            *error = perr.errorString();
        return false;
    }

    qDeleteAll(m_objects);
    m_objects.clear();
    QJsonArray arr = jdoc.object()["objects"].toArray();
    for (int i = 0; i < arr.size(); i++)
        m_objects.append(DocObject::fromJson(arr.at(i).toObject()));

    m_filePath = path;
    m_modified = false;
    emit structureChanged();
    emit changed();
    return true;
}
