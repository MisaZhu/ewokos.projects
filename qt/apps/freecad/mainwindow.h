/*
 * FreeCAD, ported to EwokOS.
 *
 * Upstream: https://github.com/FreeCAD/FreeCAD.git (LGPL-2.1).
 *
 * The upstream application is Qt + OpenCASCADE + Coin3D/OpenGL + an
 * embedded Python that the whole workbench system is written in.  None of
 * that stack exists on EwokOS, and this Qt build has no OpenGL, QProcess
 * or dlopen - so like pcmanfm-qt this is a rewrite rather than a
 * source-level port: the main window reproduces upstream's shape (combo
 * view with the model tree over the property editor, the Part primitive
 * commands, the View menu with standard views and draw styles, the 3D
 * viewport) on plain QtWidgets, with the geometry kernel replaced by
 * direct tessellation and the renderer by a software projector.
 */

#ifndef FREECAD_MAINWINDOW_H
#define FREECAD_MAINWINDOW_H

#include <QMainWindow>

#include "document.h"

class Viewport;
class PropertyPanel;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &path = QString());

protected:
    void closeEvent(QCloseEvent *ev);

private slots:
    void fileNew();
    void fileOpen();
    bool fileSave();
    bool fileSaveAs();

    void addPrimitive();                     // sender carries the type
    void deleteSelected();
    void duplicateSelected();

    void setDrawStyle();                     // sender carries the style
    void stdView();                          // sender carries the view
    void about();

    void rebuildTree();
    void treeSelectionChanged();
    void viewportSelectionChanged(DocObject *obj);
    void labelChanged(DocObject *obj);
    void docChanged();

private:
    void buildMenus();
    void buildDocks();
    bool maybeSave();                        // false = user cancelled
    void updateTitle();
    DocObject *currentObject() const;
    QTreeWidgetItem *itemFor(DocObject *obj) const;

    Document *m_doc;
    Viewport *m_viewport;
    QTreeWidget *m_tree;
    PropertyPanel *m_props;
    QTreeWidgetItem *m_docItem;
};

#endif // FREECAD_MAINWINDOW_H
