/*
 * FreeCAD, ported to EwokOS - main window implementation.
 */

#include "mainwindow.h"

#include "propertypanel.h"
#include "viewport.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeWidget>

// No icon theme on EwokOS, so the Part commands paint their own little
// primitive pictograms in FreeCAD's part-yellow.
static QIcon primIcon(DocObject::Type type)
{
    QPixmap pm(20, 20);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor face(230, 190, 60);
    QColor edge(120, 95, 20);
    p.setPen(QPen(edge, 1));
    p.setBrush(face);
    switch (type) {
    case DocObject::Box: {
        // Front face + top and side parallelograms.
        p.drawRect(3, 8, 9, 9);
        QPointF top[4] = { QPointF(3, 8), QPointF(8, 3), QPointF(17, 3), QPointF(12, 8) };
        p.setBrush(face.lighter(115));
        p.drawPolygon(top, 4);
        QPointF side[4] = { QPointF(12, 8), QPointF(17, 3), QPointF(17, 12), QPointF(12, 17) };
        p.setBrush(face.darker(115));
        p.drawPolygon(side, 4);
        break;
    }
    case DocObject::Cylinder:
        p.drawRect(5, 6, 10, 9);
        p.drawEllipse(5, 12, 10, 5);
        p.setBrush(face.lighter(120));
        p.drawEllipse(5, 3, 10, 5);
        break;
    case DocObject::Sphere:
        p.drawEllipse(3, 3, 14, 14);
        p.setPen(QPen(edge, 1, Qt::DotLine));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(3, 8, 14, 5);
        break;
    case DocObject::Cone: {
        QPointF tri[3] = { QPointF(10, 3), QPointF(3, 14), QPointF(17, 14) };
        p.drawPolygon(tri, 3);
        p.drawEllipse(3, 12, 14, 5);
        break;
    }
    case DocObject::Torus:
        p.drawEllipse(2, 5, 16, 10);
        p.setBrush(Qt::transparent);
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
    : m_doc(new Document(this)), m_docItem(0)
{
    m_viewport = new Viewport(m_doc, this);
    setCentralWidget(m_viewport);

    buildDocks();
    buildMenus();
    statusBar()->showMessage(tr("Left drag: rotate.  Shift+drag: pan.  Wheel: zoom.  Keys 0-6: standard views."));

    connect(m_doc, SIGNAL(structureChanged()), this, SLOT(rebuildTree()));
    connect(m_doc, SIGNAL(changed()), this, SLOT(docChanged()));
    connect(m_viewport, SIGNAL(selectionChanged(DocObject*)),
            this, SLOT(viewportSelectionChanged(DocObject*)));

    if (!path.isEmpty()) {
        QString err;
        if (!m_doc->load(path, &err))
            QMessageBox::warning(this, tr("Open"), tr("Cannot open %1: %2").arg(path, err));
        m_viewport->fitAll();
    }

    rebuildTree();
    updateTitle();
    resize(900, 620);
}

void MainWindow::buildDocks()
{
    // Upstream's combo view: model tree over the property editor,
    // docked on the left.
    m_tree = new QTreeWidget;
    m_tree->setHeaderLabel(tr("Labels & Attributes"));
    connect(m_tree, SIGNAL(itemSelectionChanged()), this, SLOT(treeSelectionChanged()));

    m_props = new PropertyPanel(m_doc);
    connect(m_props, SIGNAL(labelChanged(DocObject*)), this, SLOT(labelChanged(DocObject*)));

    QSplitter *combo = new QSplitter(Qt::Vertical);
    combo->addWidget(m_tree);
    combo->addWidget(m_props);
    combo->setStretchFactor(0, 3);
    combo->setStretchFactor(1, 2);

    QDockWidget *dock = new QDockWidget(tr("Model"), this);
    dock->setObjectName("comboView");
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    dock->setWidget(combo);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void MainWindow::buildMenus()
{
    // ---- File
    QMenu *file = menuBar()->addMenu(tr("&File"));
    QAction *actNew = file->addAction(tr("&New"), this, SLOT(fileNew()), QKeySequence::New);
    QAction *actOpen = file->addAction(tr("&Open..."), this, SLOT(fileOpen()), QKeySequence::Open);
    QAction *actSave = file->addAction(tr("&Save"), this, SLOT(fileSave()), QKeySequence::Save);
    file->addAction(tr("Save &As..."), this, SLOT(fileSaveAs()));
    file->addSeparator();
    file->addAction(tr("&Quit"), this, SLOT(close()), QKeySequence::Quit);

    // ---- Edit
    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(tr("&Duplicate"), this, SLOT(duplicateSelected()),
                    QKeySequence(Qt::CTRL + Qt::Key_D));
    edit->addAction(tr("De&lete"), this, SLOT(deleteSelected()), QKeySequence::Delete);

    // ---- View
    QMenu *view = menuBar()->addMenu(tr("&View"));
    QAction *actFit = view->addAction(tr("&Fit All"), m_viewport, SLOT(fitAll()),
                                      QKeySequence(Qt::CTRL + Qt::Key_F));
    view->addSeparator();
    QMenu *std = view->addMenu(tr("Standard &Views"));
    static const struct { const char *name; int view; int key; } views[] = {
        { "Axonometric", Viewport::ViewAxonometric, Qt::Key_0 },
        { "Front",       Viewport::ViewFront,       Qt::Key_1 },
        { "Top",         Viewport::ViewTop,         Qt::Key_2 },
        { "Right",       Viewport::ViewRight,       Qt::Key_3 },
        { "Rear",        Viewport::ViewRear,        Qt::Key_4 },
        { "Bottom",      Viewport::ViewBottom,      Qt::Key_5 },
        { "Left",        Viewport::ViewLeft,        Qt::Key_6 },
    };
    for (unsigned i = 0; i < sizeof(views) / sizeof(views[0]); i++) {
        QAction *a = std->addAction(tr(views[i].name), this, SLOT(stdView()),
                                    QKeySequence(views[i].key));
        a->setData(views[i].view);
    }
    QMenu *styleMenu = view->addMenu(tr("&Draw Style"));
    QActionGroup *styleGroup = new QActionGroup(this);
    static const struct { const char *name; int style; } styles[] = {
        { "Flat Lines", Viewport::ShadedWire },
        { "Shaded",     Viewport::Shaded },
        { "Wireframe",  Viewport::Wireframe },
    };
    for (unsigned i = 0; i < sizeof(styles) / sizeof(styles[0]); i++) {
        QAction *a = styleMenu->addAction(tr(styles[i].name), this, SLOT(setDrawStyle()));
        a->setData(styles[i].style);
        a->setCheckable(true);
        a->setChecked(styles[i].style == (int)m_viewport->drawStyle());
        styleGroup->addAction(a);
    }

    // ---- Part: upstream's primitive commands.
    QMenu *part = menuBar()->addMenu(tr("&Part"));
    QList<QAction *> primActions;
    static const struct { const char *name; DocObject::Type type; } prims[] = {
        { "Cube",     DocObject::Box },
        { "Cylinder", DocObject::Cylinder },
        { "Sphere",   DocObject::Sphere },
        { "Cone",     DocObject::Cone },
        { "Torus",    DocObject::Torus },
    };
    for (unsigned i = 0; i < sizeof(prims) / sizeof(prims[0]); i++) {
        QAction *a = part->addAction(primIcon(prims[i].type), tr(prims[i].name),
                                     this, SLOT(addPrimitive()));
        a->setData((int)prims[i].type);
        primActions.append(a);
    }

    // ---- Help
    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About FreeCAD"), this, SLOT(about()));

    // ---- toolbar mirrors the high-traffic commands.
    QToolBar *tb = addToolBar(tr("Main"));
    tb->setMovable(false);
    tb->addAction(actNew);
    tb->addAction(actOpen);
    tb->addAction(actSave);
    tb->addSeparator();
    for (int i = 0; i < primActions.size(); i++)
        tb->addAction(primActions.at(i));
    tb->addSeparator();
    tb->addAction(actFit);
}

// ---- file commands ----------------------------------------------------------

bool MainWindow::maybeSave()
{
    if (!m_doc->isModified())
        return true;
    int ret = QMessageBox::warning(this, tr("Unsaved document"),
                                   tr("The document has been modified.\nSave your changes?"),
                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                   QMessageBox::Save);
    if (ret == QMessageBox::Save)
        return fileSave();
    return ret == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent *ev)
{
    if (maybeSave())
        ev->accept();
    else
        ev->ignore();
}

void MainWindow::fileNew()
{
    if (!maybeSave())
        return;
    m_doc->clear();
    m_viewport->setSelection(0);
    m_props->setObject(0);
    updateTitle();
}

void MainWindow::fileOpen()
{
    if (!maybeSave())
        return;
    QString path = QFileDialog::getOpenFileName(this, tr("Open document"), "/",
                                                tr("FreeCAD document (*.fcjson);;All files (*)"));
    if (path.isEmpty())
        return;
    QString err;
    if (!m_doc->load(path, &err)) {
        QMessageBox::warning(this, tr("Open"), tr("Cannot open %1: %2").arg(path, err));
        return;
    }
    m_viewport->setSelection(0);
    m_props->setObject(0);
    m_viewport->fitAll();
    updateTitle();
}

bool MainWindow::fileSave()
{
    if (m_doc->filePath().isEmpty())
        return fileSaveAs();
    QString err;
    if (!m_doc->saveAs(m_doc->filePath(), &err)) {
        QMessageBox::warning(this, tr("Save"), tr("Cannot save: %1").arg(err));
        return false;
    }
    updateTitle();
    return true;
}

bool MainWindow::fileSaveAs()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save document"), "/",
                                                tr("FreeCAD document (*.fcjson)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".fcjson"))
        path += ".fcjson";
    QString err;
    if (!m_doc->saveAs(path, &err)) {
        QMessageBox::warning(this, tr("Save"), tr("Cannot save %1: %2").arg(path, err));
        return false;
    }
    updateTitle();
    return true;
}

// ---- object commands --------------------------------------------------------

void MainWindow::addPrimitive()
{
    QAction *a = qobject_cast<QAction *>(sender());
    if (!a)
        return;
    DocObject *obj = m_doc->addObject((DocObject::Type)a->data().toInt());
    // First shape: frame it, like an empty upstream document does.
    if (m_doc->objects().size() == 1)
        m_viewport->fitAll();
    QTreeWidgetItem *item = itemFor(obj);
    if (item)
        m_tree->setCurrentItem(item);
}

void MainWindow::deleteSelected()
{
    DocObject *obj = currentObject();
    if (!obj)
        return;
    m_viewport->setSelection(0);
    m_props->setObject(0);
    m_doc->removeObject(obj);
}

void MainWindow::duplicateSelected()
{
    DocObject *obj = currentObject();
    if (!obj)
        return;
    DocObject *copy = m_doc->addObject(obj->type());
    QStringList names = obj->paramNames();
    for (int i = 0; i < names.size(); i++)
        copy->setParam(names.at(i), obj->param(names.at(i)));
    copy->placement = obj->placement;
    copy->color = obj->color;
    copy->visible = obj->visible;
    m_doc->touch();
    rebuildTree();
    QTreeWidgetItem *item = itemFor(copy);
    if (item)
        m_tree->setCurrentItem(item);
}

// ---- view commands ----------------------------------------------------------

void MainWindow::setDrawStyle()
{
    QAction *a = qobject_cast<QAction *>(sender());
    if (a)
        m_viewport->setDrawStyle((Viewport::DrawStyle)a->data().toInt());
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
    DocObject *keep = m_viewport->selection();
    m_tree->blockSignals(true);
    m_tree->clear();

    m_docItem = new QTreeWidgetItem(m_tree);
    m_docItem->setText(0, m_doc->displayName());
    m_docItem->setData(0, Qt::UserRole, QVariant());

    const QList<DocObject *> &objs = m_doc->objects();
    QTreeWidgetItem *reselect = 0;
    for (int i = 0; i < objs.size(); i++) {
        DocObject *obj = objs.at(i);
        QTreeWidgetItem *item = new QTreeWidgetItem(m_docItem);
        item->setText(0, obj->label());
        item->setIcon(0, primIcon(obj->type()));
        item->setData(0, Qt::UserRole, QVariant::fromValue<quintptr>((quintptr)obj));
        if (!obj->visible)
            item->setForeground(0, palette().brush(QPalette::Disabled, QPalette::Text));
        if (obj == keep)
            reselect = item;
    }
    m_docItem->setExpanded(true);
    if (reselect)
        m_tree->setCurrentItem(reselect);
    m_tree->blockSignals(false);
    updateTitle();
}

QTreeWidgetItem *MainWindow::itemFor(DocObject *obj) const
{
    if (!m_docItem)
        return 0;
    for (int i = 0; i < m_docItem->childCount(); i++) {
        QTreeWidgetItem *item = m_docItem->child(i);
        if ((DocObject *)item->data(0, Qt::UserRole).value<quintptr>() == obj)
            return item;
    }
    return 0;
}

DocObject *MainWindow::currentObject() const
{
    QList<QTreeWidgetItem *> sel = m_tree->selectedItems();
    if (sel.isEmpty())
        return m_viewport->selection();
    return (DocObject *)sel.first()->data(0, Qt::UserRole).value<quintptr>();
}

void MainWindow::treeSelectionChanged()
{
    DocObject *obj = 0;
    QList<QTreeWidgetItem *> sel = m_tree->selectedItems();
    if (!sel.isEmpty())
        obj = (DocObject *)sel.first()->data(0, Qt::UserRole).value<quintptr>();
    m_viewport->setSelection(obj);
    m_props->setObject(obj);
}

void MainWindow::viewportSelectionChanged(DocObject *obj)
{
    m_tree->blockSignals(true);
    QTreeWidgetItem *item = itemFor(obj);
    if (item)
        m_tree->setCurrentItem(item);
    else
        m_tree->clearSelection();
    m_tree->blockSignals(false);
    m_props->setObject(obj);
}

void MainWindow::labelChanged(DocObject *obj)
{
    QTreeWidgetItem *item = itemFor(obj);
    if (item)
        item->setText(0, obj->label());
}

void MainWindow::docChanged()
{
    // Visibility toggles gray the tree item out; cheap enough to redo the
    // brushes on every change instead of tracking which one it was.
    if (m_docItem) {
        for (int i = 0; i < m_docItem->childCount(); i++) {
            QTreeWidgetItem *item = m_docItem->child(i);
            DocObject *obj = (DocObject *)item->data(0, Qt::UserRole).value<quintptr>();
            item->setForeground(0, obj && !obj->visible
                ? palette().brush(QPalette::Disabled, QPalette::Text)
                : palette().brush(QPalette::Active, QPalette::Text));
        }
    }
    updateTitle();
}

void MainWindow::updateTitle()
{
    QString mark = m_doc->isModified() ? "*" : "";
    setWindowTitle(QString("%1%2 - FreeCAD").arg(m_doc->displayName(), mark));
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("About FreeCAD"),
        tr("<b>FreeCAD for EwokOS</b><br>"
           "A QtWidgets rewrite of https://github.com/FreeCAD/FreeCAD (LGPL-2.1)<br>"
           "with the Part primitives on a software-rendered 3D viewport."));
}
