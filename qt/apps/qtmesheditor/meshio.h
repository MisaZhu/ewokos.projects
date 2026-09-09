/*
 * QtMeshEditor, ported to EwokOS - mesh import/export.
 *
 * Upstream feeds everything through Assimp (40+ formats) and Ogre's own
 * serializers.  Neither library exists here, so this file hand-parses the
 * three self-describing formats that need no external code: Wavefront OBJ
 * (ascii), STL (ascii and binary) and Stanford PLY (ascii).  The same three
 * are written back out on export.
 */

#ifndef QME_MESHIO_H
#define QME_MESHIO_H

#include <QString>

#include "scene.h"

namespace MeshIO {

// Dialog filter strings for the supported formats.
QString importFilter();
QString exportFilter();

// Both dispatch on the file extension; *error is set on failure.
bool importMesh(const QString &path, Mesh *out, QString *error);
bool exportMesh(const QString &path, const Mesh &mesh, QString *error);

}

#endif // QME_MESHIO_H
