/*
 * QtMeshEditor, ported to EwokOS - mesh import/export implementation.
 *
 * Parsers are deliberately forgiving: unknown lines/properties are skipped,
 * polygons are fan-triangulated, and STL soup is welded back into an
 * indexed mesh so the imported mesh is the same Mesh shape the primitives
 * produce.
 */

#include "meshio.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTextStream>
#include <QtEndian>

#include <string.h>

namespace MeshIO {

QString importFilter()
{
    return QString("3D meshes (*.obj *.stl *.ply);;"
                   "Wavefront OBJ (*.obj);;"
                   "STL (*.stl);;"
                   "Stanford PLY (*.ply);;"
                   "All files (*)");
}

QString exportFilter()
{
    return QString("Wavefront OBJ (*.obj);;"
                   "STL ascii (*.stl);;"
                   "Stanford PLY (*.ply)");
}

// ---- shared helpers -----------------------------------------------------------

// STL repeats every vertex per facet; weld exact-duplicate coordinates so
// the imported mesh is indexed like everything else.
class VertexWelder {
public:
    explicit VertexWelder(Mesh *mesh) : m_mesh(mesh) {}
    int add(const Vec3 &v) {
        QByteArray key(reinterpret_cast<const char *>(&v), sizeof(v));
        QHash<QByteArray, int>::const_iterator it = m_seen.constFind(key);
        if (it != m_seen.constEnd())
            return it.value();
        int idx = m_mesh->verts.size();
        m_mesh->verts.append(v);
        m_seen.insert(key, idx);
        return idx;
    }
private:
    Mesh *m_mesh;
    QHash<QByteArray, int> m_seen;
};

static void fanTriangulate(Mesh *mesh, const QVector<int> &face)
{
    for (int i = 2; i < face.size(); i++)
        mesh->tris.append(MeshTri(face.at(0), face.at(i - 1), face.at(i)));
}

// ---- OBJ ----------------------------------------------------------------------

static bool importObj(QFile &f, Mesh *out, QString *error)
{
    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        QStringList tok = line.split(' ', Qt::SkipEmptyParts);
        if (tok.at(0) == "v" && tok.size() >= 4) {
            out->verts.append(Vec3(tok.at(1).toDouble(), tok.at(2).toDouble(),
                                   tok.at(3).toDouble()));
        } else if (tok.at(0) == "f" && tok.size() >= 4) {
            QVector<int> face;
            for (int i = 1; i < tok.size(); i++) {
                // "v", "v/vt", "v//vn", "v/vt/vn"; may be negative (relative).
                int idx = tok.at(i).section('/', 0, 0).toInt();
                if (idx < 0)
                    idx = out->verts.size() + idx;
                else
                    idx -= 1;
                if (idx < 0 || idx >= out->verts.size()) {
                    *error = QString("bad vertex index in face: %1").arg(line);
                    return false;
                }
                face.append(idx);
            }
            fanTriangulate(out, face);
        }
        // vt/vn/usemtl/o/g/s/mtllib: no textures or normals here, skip.
    }
    if (out->verts.isEmpty()) {
        *error = "no vertices found";
        return false;
    }
    return true;
}

// ---- STL ----------------------------------------------------------------------

static float leFloat(const uchar *p)
{
    quint32 u = qFromLittleEndian<quint32>(p);
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

static bool importStlBinary(const QByteArray &data, Mesh *out, QString *error)
{
    quint32 count = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(data.constData()) + 80);
    if (data.size() < (int)(84ull + (qulonglong)count * 50ull)) {
        *error = "truncated binary STL";
        return false;
    }
    VertexWelder weld(out);
    const uchar *p = reinterpret_cast<const uchar *>(data.constData()) + 84;
    for (quint32 i = 0; i < count; i++, p += 50) {
        int idx[3];
        for (int v = 0; v < 3; v++) {
            const uchar *vp = p + 12 + v * 12;   // skip the facet normal
            idx[v] = weld.add(Vec3(leFloat(vp), leFloat(vp + 4), leFloat(vp + 8)));
        }
        out->tris.append(MeshTri(idx[0], idx[1], idx[2]));
    }
    return true;
}

static bool importStlAscii(const QByteArray &data, Mesh *out, QString *error)
{
    QTextStream in(data);
    VertexWelder weld(out);
    QVector<int> face;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        QStringList tok = line.split(' ', Qt::SkipEmptyParts);
        if (tok.isEmpty())
            continue;
        if (tok.at(0) == "vertex" && tok.size() >= 4) {
            face.append(weld.add(Vec3(tok.at(1).toDouble(), tok.at(2).toDouble(),
                                      tok.at(3).toDouble())));
        } else if (tok.at(0) == "endfacet") {
            if (face.size() >= 3)
                fanTriangulate(out, face);
            face.clear();
        }
    }
    if (out->verts.isEmpty()) {
        *error = "no vertices found";
        return false;
    }
    return true;
}

static bool importStl(QFile &f, Mesh *out, QString *error)
{
    QByteArray data = f.readAll();
    if (data.size() < 15) {
        *error = "file too short for STL";
        return false;
    }
    // The "solid" keyword lies on plenty of binary files; trust the
    // byte-exact record arithmetic instead.
    if (data.size() >= 84) {
        quint32 count = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(data.constData()) + 80);
        if ((qulonglong)84ull + (qulonglong)count * 50ull == (qulonglong)data.size())
            return importStlBinary(data, out, error);
    }
    return importStlAscii(data, out, error);
}

// ---- PLY ----------------------------------------------------------------------

static bool importPly(QFile &f, Mesh *out, QString *error)
{
    QTextStream in(&f);
    if (in.readLine().trimmed() != "ply") {
        *error = "missing ply magic";
        return false;
    }

    int nVerts = -1, nFaces = -1;
    int xProp = -1, yProp = -1, zProp = -1;
    int vertProps = 0;
    QString element;
    bool ascii = false;

    // Header: track the vertex property order so x/y/z can sit anywhere
    // among colours/normals; only ascii 1.0 bodies are supported.
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        QStringList tok = line.split(' ', Qt::SkipEmptyParts);
        if (tok.isEmpty())
            continue;
        if (tok.at(0) == "format") {
            ascii = tok.size() >= 2 && tok.at(1) == "ascii";
        } else if (tok.at(0) == "element" && tok.size() >= 3) {
            element = tok.at(1);
            if (element == "vertex")
                nVerts = tok.at(2).toInt();
            else if (element == "face")
                nFaces = tok.at(2).toInt();
        } else if (tok.at(0) == "property" && element == "vertex"
                   && tok.at(1) != "list") {
            QString name = tok.last();
            if (name == "x") xProp = vertProps;
            else if (name == "y") yProp = vertProps;
            else if (name == "z") zProp = vertProps;
            vertProps++;
        } else if (tok.at(0) == "end_header") {
            break;
        }
    }
    if (!ascii) {
        *error = "only ascii PLY is supported";
        return false;
    }
    if (nVerts <= 0 || xProp < 0 || yProp < 0 || zProp < 0) {
        *error = "bad PLY vertex header";
        return false;
    }

    for (int i = 0; i < nVerts && !in.atEnd(); i++) {
        QStringList tok = in.readLine().trimmed().split(' ', Qt::SkipEmptyParts);
        if (tok.size() < vertProps) {
            *error = QString("short vertex line %1").arg(i + 1);
            return false;
        }
        out->verts.append(Vec3(tok.at(xProp).toDouble(), tok.at(yProp).toDouble(),
                               tok.at(zProp).toDouble()));
    }
    for (int i = 0; i < nFaces && !in.atEnd(); i++) {
        QStringList tok = in.readLine().trimmed().split(' ', Qt::SkipEmptyParts);
        if (tok.isEmpty())
            continue;
        int cnt = tok.at(0).toInt();
        if (cnt < 3 || tok.size() < cnt + 1)
            continue;
        QVector<int> face;
        for (int v = 0; v < cnt; v++) {
            int idx = tok.at(v + 1).toInt();
            if (idx < 0 || idx >= out->verts.size()) {
                face.clear();
                break;
            }
            face.append(idx);
        }
        if (face.size() >= 3)
            fanTriangulate(out, face);
    }
    return true;
}

// ---- dispatch -------------------------------------------------------------------

bool importMesh(const QString &path, Mesh *out, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = f.errorString();
        return false;
    }
    out->clear();
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "obj")
        return importObj(f, out, error);
    if (ext == "stl")
        return importStl(f, out, error);
    if (ext == "ply")
        return importPly(f, out, error);
    *error = QString("unsupported format: .%1").arg(ext);
    return false;
}

// ---- exporters --------------------------------------------------------------------

static void exportObj(QTextStream &out, const Mesh &mesh)
{
    out << "# exported by QtMeshEditor for EwokOS\n";
    for (int i = 0; i < mesh.verts.size(); i++) {
        const Vec3 &v = mesh.verts.at(i);
        out << "v " << v.x << ' ' << v.y << ' ' << v.z << '\n';
    }
    for (int i = 0; i < mesh.tris.size(); i++) {
        const MeshTri &t = mesh.tris.at(i);
        out << "f " << t.a + 1 << ' ' << t.b + 1 << ' ' << t.c + 1 << '\n';
    }
}

static void exportStl(QTextStream &out, const Mesh &mesh)
{
    out << "solid mesh\n";
    for (int i = 0; i < mesh.tris.size(); i++) {
        const MeshTri &t = mesh.tris.at(i);
        const Vec3 &a = mesh.verts.at(t.a), &b = mesh.verts.at(t.b), &c = mesh.verts.at(t.c);
        Vec3 n = (b - a).cross(c - a).normalized();
        out << "  facet normal " << n.x << ' ' << n.y << ' ' << n.z << '\n';
        out << "    outer loop\n";
        out << "      vertex " << a.x << ' ' << a.y << ' ' << a.z << '\n';
        out << "      vertex " << b.x << ' ' << b.y << ' ' << b.z << '\n';
        out << "      vertex " << c.x << ' ' << c.y << ' ' << c.z << '\n';
        out << "    endloop\n";
        out << "  endfacet\n";
    }
    out << "endsolid mesh\n";
}

static void exportPly(QTextStream &out, const Mesh &mesh)
{
    out << "ply\nformat ascii 1.0\ncomment exported by QtMeshEditor for EwokOS\n";
    out << "element vertex " << mesh.verts.size() << '\n';
    out << "property float x\nproperty float y\nproperty float z\n";
    out << "element face " << mesh.tris.size() << '\n';
    out << "property list uchar int vertex_indices\nend_header\n";
    for (int i = 0; i < mesh.verts.size(); i++) {
        const Vec3 &v = mesh.verts.at(i);
        out << v.x << ' ' << v.y << ' ' << v.z << '\n';
    }
    for (int i = 0; i < mesh.tris.size(); i++) {
        const MeshTri &t = mesh.tris.at(i);
        out << "3 " << t.a << ' ' << t.b << ' ' << t.c << '\n';
    }
}

bool exportMesh(const QString &path, const Mesh &mesh, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = f.errorString();
        return false;
    }
    QTextStream out(&f);
    out.setRealNumberPrecision(8);
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "obj") {
        exportObj(out, mesh);
    } else if (ext == "stl") {
        exportStl(out, mesh);
    } else if (ext == "ply") {
        exportPly(out, mesh);
    } else {
        *error = QString("unsupported format: .%1").arg(ext);
        return false;
    }
    return true;
}

}
