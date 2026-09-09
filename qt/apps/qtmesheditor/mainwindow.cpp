/*
 * QtMeshEditor, ported to EwokOS - main window implementation.
 */

#include "mainwindow.h"

#include "inspector.h"
#include "meshio.h"
#include "viewport.h"

#include <QAction>
#include <QActionGroup>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeWidget>

// No icon theme on EwokOS, so the Create commands paint their own little
// primitive pictograms.
static QIcon primIcon(Scene::Primitive prim)
{
    QPixmap pm(20, 20);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor face(150, 190, 220);
    QColor edge(60, 90, 120);
    p.setPen(QPen(edge, 1));
    p.setBrush(face);
    switch (prim) {
    case Scene::Cube: {
        p.drawRect(3, 8, 9, 9);
        QPointF top[4] = { QPointF(3, 8), QPointF(8, 3), QPointF(17, 3), QPointF(12, 8) };
        p.setBrush(face.lighter(120));
        p.drawPolygon(top, 4);
        QPointF side[4] = { QPointF(12, 8), QPointF(17, 3), QPointF(17, 12), QPointF(12, 17) };
        p.setBrush(face.darker(115));
        p.drawPolygon(side, 4);
        break;
    }
    case Scene::Plane: {
        QPointF quad[4] = { QPointF(2, 12), QPointF(9, 8), QPointF(18, 11), QPointF(10, 16) };
        p.drawPolygon(quad, 4);
        break;
    }
    case Scene::Sphere:
        p.drawEllipse(3, 3, 14, 14);
        p.setPen(QPen(edge, 1, Qt::DotLine));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(3, 8, 14, 5);
        break;
    case Scene::Cylinder:
        p.drawRect(5, 6, 10, 9);
        p.drawEllipse(5, 12, 10, 5);
        p.setBrush(face.lighter(120));
        p.drawEllipse(5, 3, 10, 5);
        break;
    case Scene::Cone: {
        QPointF tri[3] = { QPointF(10, 3), QPointF(3, 14), QPointF(17, 14) };
        p.drawPolygon(tri, 3);
        p.drawEllipse(3, 12, 14, 5);
        break;
    }
    case Scene::Torus:
        p.drawEllipse(2, 5, 16, 10);
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.setPen(Qt::NoPen);
        p.drawEllipse(7, 8, 6, 4);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setPen(QPen(edge, 1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(7, 8, 6, 4);
        break;
    }
    return QIcon(pm);
}

MainWindow::MainWindow(const QString &path)
    : m_scene(new Scene(this)), m_sceneItem(0)
{
    m_viewport = new Viewport(m_scene, this);
    setCentralWidget(m_viewport);

    buildDocks();
    buildMenus();
    statusBar()->showMessage(tr("Left drag: orbit.  Shift+drag: pan.  Wheel: zoom.  Keys 0-6: views."));

    connect(m_scene, SIGNAL(structureChanged()), this, SLOT(rebuildTree()));
    connect(m_scene, SIGNAL(changed()), this, SLOT(updateStatus()));
    connect(m_viewport, SIGNAL(selectionChanged(MeshEntity*)),
            this, SLOT(viewportSelectionChanged(MeshEntity*)));

    if (!path.isEmpty())
        fileImportWithPath(path);

    rebuildTree();
    updateTitle();
    resize(960, 640);
}

void MainWindow::buildDocks()
{
    // Scene tree over the entity inspector, docked on the left - the layout
    // upstream's SceneTreeWidget + Inspector panels use.
    m_tree = new QTreeWidget;
    m_tree->setHeaderLabel(tr("Scene"));
    connect(m_tree, SIGNAL(itemSelectionChanged()), this, SLOT(treeSelectionChanged()));

    m_inspector = new Inspector(m_scene);
    connect(m_inspector, SIGNAL(nameChanged(MeshEntity*)), this, SLOT(nameChanged(MeshEntity*)));

    QSplitter *side = new QSplitter(Qt::Vertical);
    side->addWidget(m_tree);
    side->addWidget(m_inspector);
    side->setStretchFactor(0, 2);
    side->setStretchFactor(1, 3);

    QDockWidget *dock = new QDockWidget(tr("Scene"), this);
    dock->setObjectName("sceneDock");
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    dock->setWidget(side);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void MainWindow::buildMenus()
{
    // ---- File
    QMenu *file = menuBar()->addMenu(tr("&File"));
    QAction *actNew = file->addAction(tr("&New Scene"), this, SLOT(fileNew()), QKeySequence::New);
    QAction *actImport = file->addAction(tr("&Import Mesh..."), this, SLOT(fileImport()),
                                         QKeySequence::Open);
    QAction *actExport = file->addAction(tr("&Export Selected..."), this, SLOT(fileExport()),
                                         QKeySequence::Save);
    file->addSeparator();
    file->addAction(tr("&Quit"), this, SLOT(close()), QKeySequence::Quit);

    // ---- Edit
    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(tr("&Duplicate"), this, SLOT(duplicateSelected()),
                    QKeySequence(Qt::CTRL + Qt::Key_D));
    edit->addAction(tr("De&lete"), this, SLOT(deleteSelected()), QKeySequence::Delete);

    // ---- Create: the procedural primitives.
    QMenu *create = menuBar()->addMenu(tr("&Create"));
    QList<QAction *> primActions;
    static const struct { const char *name; Scene::Primitive prim; } prims[] = {
        { "Cube",     Scene::Cube },
        { "Plane",    Scene::Plane },
        { "Sphere",   Scene::Sphere },
        { "Cylinder", Scene::Cylinder },
        { "Cone",     Scene::Cone },
        { "Torus",    Scene::Torus },
    };
    for (unsigned i = 0; i < sizeof(prims) / sizeof(prims[0]); i++) {
        QAction *a = create->addAction(primIcon(prims[i].prim), tr(prims[i].name),
                                       this, SLOT(addPrimitive()));
        a->setData((int)prims[i].prim);
        primActions.append(a);
    }

    // ---- View
    QMenu *view = menuBar()->addMenu(tr("&View"));
    QAction *actFit = view->addAction(tr("&Fit All"), m_viewport, SLOT(fitAll()),
                                      QKeySequence(Qt::CTRL + Qt::Key_F));
    QAction *actGrid = view->addAction(tr("Show &Grid"), this, SLOT(toggleGrid(bool)));
    actGrid->setCheckable(true);
    actGrid->setChecked(m_viewport->showGrid());
    view->addSeparator();
    QMenu *std = view->addMenu(tr("Standard &Views"));
    static const struct { const char *name; int view; int key; } views[] = {
        { "Perspective", Viewport::ViewPerspective, Qt::Key_0 },
        { "Front",       Viewport::ViewFront,       Qt::Key_1 },
        { "Back",        Viewport::ViewBack,        Qt::Key_2 },
        { "Top",         Viewport::ViewTop,         Qt::Key_3 },
        { "Bottom",      Viewport::ViewBottom,      Qt::Key_4 },
        { "Left",        Viewport::ViewLeft,        Qt::Key_5 },
        { "Right",       Viewport::ViewRight,       Qt::Key_6 },
    };
    for (unsigned i = 0; i < sizeof(views) / sizeof(views[0]); i++) {
        QAction *a = std->addAction(tr(views[i].name), this, SLOT(stdView()),
                                    QKeySequence(views[i].key));
        a->setData(views[i].view);
    }
    QMenu *styleMenu = view->addMenu(tr("&Draw Style"));
    QActionGroup *styleGroup = new QActionGroup(this);
    static const struct { const char *name; int style; } styles[] = {
        { "Shaded",      Viewport::Shaded },
        { "Shaded Wire", Viewport::ShadedWire },
        { "Wireframe",   Viewport::Wireframe },
    };
    for (unsigned i = 0; i < sizeof(styles) / sizeof(styles[0]); i++) {
        QAction *a = styleMenu->addAction(tr(styles[i].name), this, SLOT(setDrawStyle()));
        a->setData(styles[i].style);
        a->setCheckable(true);
        a->setChecked(styles[i].style == (int)m_viewport->drawStyle());
        styleGroup->addAction(a);
    }

    // ---- Help
    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About QtMeshEditor"), this, SLOT(about()));

    // ---- toolbar mirrors the high-traffic commands.
    QToolBar *tb = addToolBar(tr("Main"));
    tb->setMovable(false);
    tb->addAction(actNew);
    tb->addAction(actImport);
    tb->addAction(actExport);
    tb->addSeparator();
    for (int i = 0; i < primActions.size(); i++)
        tb->addAction(primActions.at(i));
    tb->addSeparator();
    tb->addAction(actFit);
}

// ---- file commands ----------------------------------------------------------

void MainWindow::fileNew()
{
    m_scene->clear();
    m_viewport->setSelection(0);
    m_inspector->setEntity(0);
    updateTitle();
}

// Shared by the dialog path and the command-line path.
void MainWindow::fileImportWithPath(const QString &path)
{
    Mesh mesh;
    QString err;
    if (!MeshIO::importMesh(path, &mesh, &err)) {
        QMessageBox::warning(this, tr("Import"), tr("Cannot import %1: %2").arg(path, err));
        return;
    }
    QString base = QFileInfo(path).completeBaseName();
    if (base.isEmpty())
        base = "Mesh";
    MeshEntity *ent = m_scene->addMesh(base, mesh, path);
    if (m_scene->entities().size() == 1)
        m_viewport->fitAll();
    selectEntity(ent);
}

void MainWindow::fileImport()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Import mesh"), "/",
                                                MeshIO::importFilter());
    if (path.isEmpty())
        return;
    fileImportWithPath(path);
}

void MainWindow::fileExport()
{
    MeshEntity *ent = currentEntity();
    if (!ent) {
        QMessageBox::information(this, tr("Export"), tr("Select an entity to export."));
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, tr("Export mesh"),
                                                QString("/") + ent->name + ".obj",
                                                MeshIO::exportFilter());
    if (path.isEmpty())
        return;
    // Bake the node transform so the exported mesh matches the viewport.
    Mesh baked;
    baked.verts.reserve(ent->mesh.verts.size());
    for (int i = 0; i < ent->mesh.verts.size(); i++)
        baked.verts.append(ent->toWorld(ent->mesh.verts.at(i)));
    baked.tris = ent->mesh.tris;

    QString err;
    if (!MeshIO::exportMesh(path, baked, &err))
        QMessageBox::warning(this, tr("Export"), tr("Cannot export %1: %2").arg(path, err));
    else
        statusBar()->showMessage(tr("Exported %1").arg(path), 4000);
}

// ---- entity commands --------------------------------------------------------

void MainWindow::addPrimitive()
{
    QAction *a = qobject_cast<QAction *>(sender());
    if (!a)
        return;
    MeshEntity *ent = m_scene->addPrimitive((Scene::Primitive)a->data().toInt());
    if (m_scene->entities().size() == 1)
        m_viewport->fitAll();
    selectEntity(ent);
}

void MainWindow::duplicateSelected()
{
    MeshEntity *ent = currentEntity();
    if (!ent)
        return;
    MeshEntity *copy = m_scene->duplicate(ent);
    selectEntity(copy);
}

void MainWindow::deleteSelected()
{
    MeshEntity *ent = currentEntity();
    if (!ent)
        return;
    m_viewport->setSelection(0);
    m_inspector->setEntity(0);
    m_scene->removeEntity(ent);
}

// ---- view commands ----------------------------------------------------------

void MainWindow::setDrawStyle()
{
    QAction *a = qobject_cast<QAction *>(sender());
    if (a)
        m_viewport->setDrawStyle((Viewport::DrawStyle)a->data().toInt());
}

void MainWindow::toggleGrid(bool on)
{
    m_viewport->setShowGrid(on);
}

void MainWindow::stdView()
{
    QAction *a = qobject_cast<QAction *>(sender());
    if (a)
        m_viewport->setStdView(a->data().toInt());
}

// ---- tree / selection sync ----------------------------------------------------

void MainWindow::rebuildTree()
{
    MeshEntity *keep = m_viewport->selection();
    m_tree->blockSignals(true);
    m_tree->clear();

    m_sceneItem = new QTreeWidgetItem(m_tree);
    m_sceneItem->setText(0, tr("Scene"));
    m_sceneItem->setData(0, Qt::UserRole, QVariant());

    const QList<MeshEntity *> &ents = m_scene->entities();
    QTreeWidgetItem *reselect = 0;
    for (int i = 0; i < ents.size(); i++) {
        MeshEntity *ent = ents.at(i);
        QTreeWidgetItem *item = new QTreeWidgetItem(m_sceneItem);
        item->setText(0, ent->name);
        item->setData(0, Qt::UserRole, QVariant::fromValue<quintptr>((quintptr)ent));
        if (!ent->visible)
            item->setForeground(0, palette().brush(QPalette::Disabled, QPalette::Text));
        if (ent == keep)
            reselect = item;
    }
    m_sceneItem->setExpanded(true);
    if (reselect)
        m_tree->setCurrentItem(reselect);
    m_tree->blockSignals(false);
    updateTitle();
    updateStatus();
}

QTreeWidgetItem *MainWindow::itemFor(MeshEntity *ent) const
{
    if (!m_sceneItem)
        return 0;
    for (int i = 0; i < m_sceneItem->childCount(); i++) {
        QTreeWidgetItem *item = m_sceneItem->child(i);
        if ((MeshEntity *)item->data(0, Qt::UserRole).value<quintptr>() == ent)
            return item;
    }
    return 0;
}

MeshEntity *MainWindow::currentEntity() const
{
    QList<QTreeWidgetItem *> sel = m_tree->selectedItems();
    if (sel.isEmpty())
        return m_viewport->selection();
    return (MeshEntity *)sel.first()->data(0, Qt::UserRole).value<quintptr>();
}

void MainWindow::selectEntity(MeshEntity *ent)
{
    m_viewport->setSelection(ent);
    m_inspector->setEntity(ent);
    m_tree->blockSignals(true);
    QTreeWidgetItem *item = itemFor(ent);
    if (item)
        m_tree->setCurrentItem(item);
    m_tree->blockSignals(false);
    updateStatus();
}

void MainWindow::treeSelectionChanged()
{
    MeshEntity *ent = 0;
    QList<QTreeWidgetItem *> sel = m_tree->selectedItems();
    if (!sel.isEmpty())
        ent = (MeshEntity *)sel.first()->data(0, Qt::UserRole).value<quintptr>();
    m_viewport->setSelection(ent);
    m_inspector->setEntity(ent);
    updateStatus();
}

void MainWindow::viewportSelectionChanged(MeshEntity *ent)
{
    m_tree->blockSignals(true);
    QTreeWidgetItem *item = itemFor(ent);
    if (item)
        m_tree->setCurrentItem(item);
    else
        m_tree->clearSelection();
    m_tree->blockSignals(false);
    m_inspector->setEntity(ent);
    updateStatus();
}

void MainWindow::nameChanged(MeshEntity *ent)
{
    QTreeWidgetItem *item = itemFor(ent);
    if (item)
        item->setText(0, ent->name);
}

// ---- title / status -----------------------------------------------------------

void MainWindow::updateTitle()
{
    setWindowTitle(tr("QtMeshEditor"));
}

void MainWindow::updateStatus()
{
    MeshEntity *ent = currentEntity();
    int n = m_scene->entities().size();
    if (ent)
        statusBar()->showMessage(tr("%1 entities   |   %2: %3 verts, %4 tris")
                                 .arg(n).arg(ent->name)
                                 .arg(ent->mesh.verts.size())
                                 .arg(ent->mesh.tris.size()));
    else
        statusBar()->showMessage(tr("%1 entities").arg(n));
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("About QtMeshEditor"),
        tr("<b>QtMeshEditor for EwokOS</b><br>"
           "A QtWidgets rewrite of https://github.com/fernandotonon/QtMeshEditor (GPL-3.0)<br>"
           "with OBJ/STL/PLY import &amp; export, procedural primitives and a<br>"
           "software-rendered 3D viewport (no OpenGL on this Qt build)."));
}
