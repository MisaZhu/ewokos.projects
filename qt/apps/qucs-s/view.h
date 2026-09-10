/*
 * Qucs-S, ported to EwokOS - the schematic canvas.
 *
 * Upstream: https://github.com/ra3xdh/qucs_s (GPL-2.0), whose qucs/schematic.cpp
 * is a QScrollView holding a canvas of items with one class per interaction
 * (qucs/mouseactions.cpp).  Here it is a single widget that paints the
 * Component, Wire and Diagram records of the document and handles the mouse
 * for them, which is what a fixed sheet of a few hundred elements needs - and
 * this Qt build has no QScrollView, that being a Qt3 class upstream kept alive
 * through its compatibility layer.
 *
 * The sheet is the widget: no internal scrolling, so widget coordinates are
 * sheet coordinates and the main window puts this into a QScrollArea.
 */

#ifndef QUCS_VIEW_H
#define QUCS_VIEW_H

#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

#include "schematic.h"
#include "simulator.h"

class QFont;
class QFontMetrics;
class QKeyEvent;
class QMouseEvent;
class QPainter;

class SchematicView : public QWidget {
    Q_OBJECT
public:
    // What the mouse does.  Upstream keeps the same split: a pointer, a wire
    // pen, a net-label tool, and the "a component follows the cursor" state
    // its component library puts the sheet into.
    enum Tool {
        ToolSelect = 0,
        ToolWire,
        ToolLabel,
        ToolPlace,
        ToolDiagram
    };

    explicit SchematicView(QWidget *parent = 0);

    // The view does not own the document; it repaints on changed() and
    // structureChanged().
    void setSchematic(Schematic *sch);
    Schematic *schematic() const { return m_sch; }

    // The result the diagrams plot, borrowed from the main window and valid
    // until the next one is set; 0 draws the diagrams empty.
    void setSimResult(const SimResult *res);

    void setTool(int t);
    int tool() const { return m_tool; }
    // With ToolPlace, the type the cursor is carrying.
    void setPlaceType(const QString &type);
    QString placeType() const { return m_placeType; }

    bool hasSelection() const;
    int selectedCount() const;
    // The first of each kind that is selected, or -1: what the window's
    // Properties action and its enabled state go by.
    int firstSelectedComponent() const { return m_selComp.isEmpty() ? -1 : m_selComp.first(); }
    int firstSelectedDiagram() const { return m_selDiag.isEmpty() ? -1 : m_selDiag.first(); }
    int firstSelectedWire() const { return m_selWire.isEmpty() ? -1 : m_selWire.first(); }
    QSize sheetSize() const { return m_sheet; }

public slots:
    void clearSelection();
    void selectAll();
    void deleteSelection();
    void rotateSelection();
    void mirrorSelection();
    void toggleActive();               // the Qucs per-element active flag

signals:
    void toolChanged(int tool);
    void selectionChanged();
    void statusMessage(const QString &text);
    // The document changed through the mouse, which is what marks it modified.
    void edited();
    // A double-click, or the Properties action, on one of these.
    void editComponent(int index);
    void editDiagram(int index);

protected:
    void paintEvent(QPaintEvent *);
    void mousePressEvent(QMouseEvent *);
    void mouseMoveEvent(QMouseEvent *);
    void mouseReleaseEvent(QMouseEvent *);
    void mouseDoubleClickEvent(QMouseEvent *);
    void keyPressEvent(QKeyEvent *);
    void leaveEvent(QEvent *);

private:
    QFont labelFont() const;
    // The symbol plus the text beside it, which is what a click may land on.
    QRect compHitRect(const Component &c, const QFontMetrics &fm) const;

    int hitComponent(const QPoint &p) const;
    int hitWire(const QPoint &p) const;
    int hitDiagram(const QPoint &p) const;
    // The bottom-right corner of a diagram, which dragging resizes it by.
    int hitDiagramHandle(const QPoint &p) const;

    bool isSelected(int comp, int wire, int diagram) const;
    void setSelected(int comp, int wire, int diagram, bool add);
    void emitSelection();

    // The wires an L-shaped connection from a to b turns into.
    void addWireRun(const QPoint &a, const QPoint &b);
    void labelWire(int index);
    void rememberDrag();
    void applyDrag(const QPoint &delta);

    void paintComponent(QPainter *p, const Component &c, bool selected) const;

    Schematic *m_sch;
    const SimResult *m_res;
    QSize m_sheet;

    int m_tool;
    QString m_placeType;

    QList<int> m_selComp, m_selWire, m_selDiag;

    QPoint m_cursor;                   // grid-snapped
    QPoint m_pressPos;                 // grid-snapped press position
    bool m_wiring;                     // a wire run is half drawn
    bool m_dragging;                   // the selection is being moved
    bool m_banding;                    // a rubber band is open
    QRect m_band;
    int m_resizing;                    // diagram index, or -1
    QList<QPoint> m_dragComp;          // where the selection started
    QList<Wire> m_dragWire;
    QList<Diagram> m_dragDiag;
};

#endif // QUCS_VIEW_H
