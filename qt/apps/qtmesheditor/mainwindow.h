/*
 * QtMeshEditor, ported to EwokOS.
 *
 * Upstream: https://github.com/fernandotonon/QtMeshEditor (GPL-3.0).
 *
 * The upstream application is Qt + Ogre3D + Assimp (+ an MCP server, a REST
 * API, cloud sync, AI material generation and mocap).  None of that stack
 * exists on EwokOS, and this Qt build has no OpenGL, QProcess, dlopen or
 * network client - so like pcmanfm-qt/nomacs/freecad this is a rewrite rather
 * than a source-level port.  The main window reproduces the editor's shape on
 * plain QtWidgets: a scene tree docked over an entity inspector, a Create
 * menu with the procedural primitives, File Import/Export for OBJ/STL/PLY, a
 * View menu with draw styles and standard views, and a software-rendered 3D
 * viewport with orbit/pan/zoom and click-to-select.
 */

#ifndef QME_MAINWINDOW_H
#define QME_MAINWINDOW_H

#include <QMainWindow>

#include "scene.h"

class Viewport;
class Inspector;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &path = QString());

private slots:
    void fileNew();
    void fileImport();
    void fileExport();

    void addPrimitive();                     // sender carries the Primitive value
    void duplicateSelected();
    void deleteSelected();

    void setDrawStyle();                     // sender carries the style
    void toggleGrid(bool on);
    void stdView();                          // sender carries the view
    void about();

    void rebuildTree();
    void treeSelectionChanged();
    void viewportSelectionChanged(MeshEntity *ent);
    void nameChanged(MeshEntity *ent);

private:
    void buildMenus();
    void buildDocks();
    void fileImportWithPath(const QString &path);
    void updateTitle();
    void updateStatus();
    MeshEntity *currentEntity() const;
    void selectEntity(MeshEntity *ent);
    QTreeWidgetItem *itemFor(MeshEntity *ent) const;

    Scene *m_scene;
    Viewport *m_viewport;
    QTreeWidget *m_tree;
    Inspector *m_inspector;
    QTreeWidgetItem *m_sceneItem;
};

#endif // QME_MAINWINDOW_H
