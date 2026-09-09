/*
 * FreeCAD, ported to EwokOS - the document layer.
 *
 * Upstream: https://github.com/FreeCAD/FreeCAD.git (LGPL-2.1).
 *
 * Upstream's App::Document sits on OpenCASCADE for the geometry kernel and
 * on Python for the object system; neither exists here.  What this file
 * keeps is the shape of the model: a Document owning a list of named,
 * parametric objects (the Part primitives - Box, Cylinder, Sphere, Cone,
 * Torus), each with a Placement and view properties (color, visibility).
 * Instead of B-rep solids the objects tessellate themselves directly into
 * triangle meshes, which is all the software viewport needs.  Files are
 * plain JSON rather than the upstream zipped .FCStd container.
 */

#ifndef FREECAD_DOCUMENT_H
#define FREECAD_DOCUMENT_H

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

// ---- minimal 3D math --------------------------------------------------------

struct Vec3 {
    double x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3 &o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3 &o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator*(double s) const { return Vec3(x * s, y * s, z * s); }
    double dot(const Vec3 &o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3 &o) const {
        return Vec3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x);
    }
    double length() const;
    Vec3 normalized() const;
};

// Column-major-free tiny 3x3 rotation matrix; enough for placements and the
// camera.  Rotations compose left-to-right via operator*.
struct Mat3 {
    double m[3][3];
    Mat3();                                  // identity
    static Mat3 rotX(double deg);
    static Mat3 rotY(double deg);
    static Mat3 rotZ(double deg);
    Mat3 operator*(const Mat3 &o) const;
    Vec3 apply(const Vec3 &v) const;
};

// Upstream App::Placement is a quaternion + position; euler XYZ degrees are
// friendlier to a spinbox-based property panel and round-trip through JSON.
struct Placement {
    Vec3 position;
    Vec3 rotationDeg;                        // applied X, then Y, then Z

    Mat3 rotation() const {
        return Mat3::rotZ(rotationDeg.z) * Mat3::rotY(rotationDeg.y) * Mat3::rotX(rotationDeg.x);
    }
    Vec3 toWorld(const Vec3 &local) const { return rotation().apply(local) + position; }
};

// ---- triangle mesh ----------------------------------------------------------

struct MeshTri {
    int a, b, c;
    MeshTri() : a(0), b(0), c(0) {}
    MeshTri(int a_, int b_, int c_) : a(a_), b(b_), c(c_) {}
};

struct Mesh {
    QVector<Vec3> verts;                     // object-local coordinates
    QVector<MeshTri> tris;                   // outward CCW winding
    void clear() { verts.clear(); tris.clear(); }
};

// ---- document objects -------------------------------------------------------

class DocObject {
public:
    enum Type { Box, Cylinder, Sphere, Cone, Torus };

    DocObject(Type type, const QString &name);

    Type type() const { return m_type; }
    static QString typeName(Type t);

    // The internal name is unique and stable ("Box001"); the label is what
    // the user renames in the tree, exactly like upstream.
    QString name() const { return m_name; }
    QString label() const { return m_label; }
    void setLabel(const QString &l) { m_label = l; }

    Placement placement;
    QColor color;
    bool visible;

    // Parameters in upstream's names and defaults (Part workbench):
    //   Box:      Length, Width, Height
    //   Cylinder: Radius, Height
    //   Sphere:   Radius
    //   Cone:     Radius1, Radius2, Height
    //   Torus:    Radius1, Radius2
    QStringList paramNames() const;
    double param(const QString &key) const { return m_params.value(key, 0.0); }
    void setParam(const QString &key, double v);

    // Tessellation, cached until a parameter changes.
    const Mesh &mesh() const;

    QJsonObject toJson() const;
    static DocObject *fromJson(const QJsonObject &obj);

private:
    void rebuildMesh() const;

    Type m_type;
    QString m_name;
    QString m_label;
    QMap<QString, double> m_params;
    mutable Mesh m_mesh;
    mutable bool m_meshDirty;
};

// ---- the document -----------------------------------------------------------

class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QObject *parent = 0);
    ~Document();

    const QList<DocObject *> &objects() const { return m_objects; }
    DocObject *addObject(DocObject::Type type);
    void removeObject(DocObject *obj);
    void clear();

    // Called by whoever edits an object in place (property panel);
    // re-emits changed() so the views repaint.
    void touch();

    QString filePath() const { return m_filePath; }
    QString displayName() const;
    bool isModified() const { return m_modified; }

    bool saveAs(const QString &path, QString *error);
    bool load(const QString &path, QString *error);

signals:
    void changed();                          // geometry/props: repaint views
    void structureChanged();                 // add/remove/load: rebuild tree

private:
    QString uniqueName(DocObject::Type type) const;

    QList<DocObject *> m_objects;
    QString m_filePath;
    bool m_modified;
};

#endif // FREECAD_DOCUMENT_H
