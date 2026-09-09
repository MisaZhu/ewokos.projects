/*
 * QtMeshEditor, ported to EwokOS - the scene layer.
 *
 * Upstream: https://github.com/fernandotonon/QtMeshEditor (GPL-3.0).
 *
 * Upstream's scene is an Ogre3D SceneManager: every loaded mesh becomes an
 * Ogre::Entity attached to a SceneNode with a position/rotation/scale, and
 * Assimp does the importing.  Neither Ogre nor Assimp exists on EwokOS and
 * this Qt build has no OpenGL, so this file keeps only the shape of that
 * model: a Scene owning a list of named MeshEntity objects, each a plain
 * triangle mesh plus a node transform and a simple diffuse material.
 * Importing is handled by the hand-written parsers in meshio.cpp, and the
 * procedural primitives below stand in for upstream's PrimitiveObject
 * (ogre-procedural) shapes.  Ogre convention is kept: Y is up.
 */

#ifndef QME_SCENE_H
#define QME_SCENE_H

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>
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

// Tiny 3x3 rotation matrix; enough for node transforms and the camera.
struct Mat3 {
    double m[3][3];
    Mat3();                                  // identity
    static Mat3 rotX(double deg);
    static Mat3 rotY(double deg);
    static Mat3 rotZ(double deg);
    Mat3 operator*(const Mat3 &o) const;
    Vec3 apply(const Vec3 &v) const;
};

// ---- triangle mesh ----------------------------------------------------------

struct MeshTri {
    int a, b, c;
    MeshTri() : a(0), b(0), c(0) {}
    MeshTri(int a_, int b_, int c_) : a(a_), b(b_), c(c_) {}
};

struct Mesh {
    QVector<Vec3> verts;                     // entity-local coordinates
    QVector<MeshTri> tris;
    void clear() { verts.clear(); tris.clear(); }
    void bounds(Vec3 *lo, Vec3 *hi) const;   // zeroes when empty
};

// ---- scene entities ---------------------------------------------------------

// Upstream: an Ogre::Entity on its SceneNode.  The node transform is euler
// XYZ degrees + position + per-axis scale (what upstream's TransformOperator
// spinboxes edit), the material is reduced to the diffuse colour (the one
// MaterialEditor property a colour-only software shader can honour).
class MeshEntity {
public:
    explicit MeshEntity(const QString &name);

    QString name;                            // unique node name ("Cube001")
    bool visible;

    Vec3 position;
    Vec3 rotationDeg;                        // applied X, then Y, then Z
    Vec3 scale;                              // per-axis, defaults to 1

    QColor diffuse;
    QString sourcePath;                      // import origin, empty for primitives

    Mesh mesh;

    Mat3 rotation() const {
        return Mat3::rotZ(rotationDeg.z) * Mat3::rotY(rotationDeg.y) * Mat3::rotX(rotationDeg.x);
    }
    Vec3 toWorld(const Vec3 &local) const {
        return rotation().apply(Vec3(local.x * scale.x, local.y * scale.y, local.z * scale.z))
               + position;
    }
};

// ---- the scene --------------------------------------------------------------

class Scene : public QObject {
    Q_OBJECT
public:
    // Upstream's procedural primitives (ogre-procedural PrimitiveObject).
    enum Primitive { Cube, Plane, Sphere, Cylinder, Cone, Torus };

    explicit Scene(QObject *parent = 0);
    ~Scene();

    const QList<MeshEntity *> &entities() const { return m_entities; }
    MeshEntity *addPrimitive(Primitive prim);
    MeshEntity *addMesh(const QString &baseName, const Mesh &mesh, const QString &sourcePath);
    MeshEntity *duplicate(MeshEntity *ent);
    void removeEntity(MeshEntity *ent);
    void clear();

    static QString primitiveName(Primitive prim);
    QString uniqueName(const QString &base) const;

    // Called by whoever edits an entity in place (inspector, mesh tools);
    // re-emits changed() so the views repaint.
    void touch();

signals:
    void changed();                          // transforms/material: repaint views
    void structureChanged();                 // add/remove: rebuild the tree

private:
    static Mesh makePrimitive(Primitive prim);

    QList<MeshEntity *> m_entities;
};

#endif // QME_SCENE_H
