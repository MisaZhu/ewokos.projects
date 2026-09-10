/*
 * Qucs-S, ported to EwokOS - the schematic document.
 *
 * Upstream: https://github.com/ra3xdh/qucs_s (GPL-2.0).  The model mirrors
 * qucs/schematic.cpp: a sheet holding Component, Wire and Diagram objects on
 * a 10 unit grid, and upstream's angle-bracket text file (qucs/
 * schematic_file.cpp) - the same `<R R1 1 200 160 0 0 "1 kOhm" 1>` element
 * lines, `<Wires>` records and `<Diagrams>` blocks.  What is dropped is the
 * project container: upstream's .prj wraps a list of documents, here a tab
 * holds one .sch and several tabs are several files, which is the part of a
 * project this port needs.
 *
 * Simulation results are deliberately not part of the document: they belong to
 * the last run and live in the main window, so this file has no dependency on
 * the simulator.
 */

#ifndef QUCS_SCHEMATIC_H
#define QUCS_SCHEMATIC_H

#include <QList>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QString>

#include "components.h"

// The sheet grid.  Upstream's default gridSize, and every port of every symbol
// in components.cpp lands on it.
enum { kGrid = 10 };

inline QPoint snapToGrid(const QPoint &p)
{
    return QPoint(qRound(p.x() / (double)kGrid) * kGrid,
                  qRound(p.y() / (double)kGrid) * kGrid);
}

// ---- wires ------------------------------------------------------------------

struct Wire {
    QPoint p1, p2;                           // always axis-aligned
    QString label;                           // net name, "" for an unnamed net

    Wire() {}
    Wire(const QPoint &a, const QPoint &b, const QString &l = QString())
        : p1(a), p2(b), label(l) {}

    bool horizontal() const { return p1.y() == p2.y(); }
    QRect bounds() const;                    // 3 px fat, for hit testing
    bool contains(const QPoint &p) const;    // p lies on the segment
};

// ---- diagrams ---------------------------------------------------------------

struct Diagram {
    enum Plot { Real = 0, Magnitude, Decibel, Phase, Imag };

    QPoint pos;                              // top-left corner, grid aligned
    QSize size;
    QStringList vars;                        // variable names, e.g. "_net0", "V1.I"
    bool logX, logY;
    int plot;                                // Plot, as an int for the file format

    Diagram();
    QRect rect() const { return QRect(pos, size); }
    static QString plotName(int p);
};

// ---- the document -----------------------------------------------------------

class Schematic : public QObject {
    Q_OBJECT
public:
    explicit Schematic(QObject *parent = 0);

    QList<Component> &components() { return m_components; }
    const QList<Component> &components() const { return m_components; }
    QList<Wire> &wires() { return m_wires; }
    const QList<Wire> &wires() const { return m_wires; }
    QList<Diagram> &diagrams() { return m_diagrams; }
    const QList<Diagram> &diagrams() const { return m_diagrams; }

    int addComponent(const Component &c);
    int addWire(const Wire &w);
    int addDiagram(const Diagram &d);

    // Indices in any order; each list is edited in one structureChanged().
    void removeComponents(const QList<int> &idx);
    void removeWires(const QList<int> &idx);
    void removeDiagrams(const QList<int> &idx);

    void clear();

    // Simulation blocks on the sheet, in placement order - what "Simulate"
    // runs, exactly as upstream reads the analysis off the schematic.
    QList<int> simComponents() const;
    // First active simulation block, or -1.
    int activeSim() const;

    // "R1", "R2", ... for a type's prefix, skipping names already taken.
    QString uniqueName(const QString &type) const;

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &p) { m_filePath = p; }
    QString displayName() const;
    bool isModified() const { return m_modified; }
    void setModified(bool m);

    // Called after an in-place edit (a property dialog, a move) that already
    // went through the accessors above.
    void touch();

    bool save(const QString &path, QString *error);
    bool load(const QString &path, QString *error);

signals:
    void changed();                          // repaint
    void structureChanged();                 // add/remove/load: also repaint

private:
    QList<Component> m_components;
    QList<Wire> m_wires;
    QList<Diagram> m_diagrams;
    QString m_filePath;
    bool m_modified;
};

#endif // QUCS_SCHEMATIC_H
