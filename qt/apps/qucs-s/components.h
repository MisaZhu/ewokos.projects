/*
 * Qucs-S, ported to EwokOS.
 *
 * Upstream: https://github.com/ra3xdh/qucs_s (GPL-2.0), whose component
 * classes live one file each in qucs/components/ (resistor.cpp, volt_dc.cpp,
 * diode.cpp, ...) behind a common qucs/components/component.cpp base.
 *
 * Here the whole catalogue is data: a table of component definitions carrying
 * the type key Qucs writes into a .sch file ("R", "Vdc", "Tran"), the
 * property names and defaults of upstream's Component::registerProperties(),
 * the port offsets of upstream's Component::createSymbol(), and one QPainter
 * routine per symbol instead of a class per component.  The subset is the one
 * the in-process simulator of simulator.cpp can solve: lumped R/C/L, the
 * independent sources, ground, a diode, a bipolar transistor, the four
 * controlled sources, a current probe, and the DC/AC/transient simulation
 * blocks - which in Qucs are components placed on the schematic too, so the
 * analysis to run is read off the sheet rather than a menu.
 */

#ifndef QUCS_COMPONENTS_H
#define QUCS_COMPONENTS_H

#include <QIcon>
#include <QList>
#include <QPoint>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <QVector>

class QPainter;

// ---- one component instance on a schematic ----------------------------------

struct Component {
    QString type;                            // catalogue key, e.g. "R"
    QString name;                            // instance name, e.g. "R1"; "*" for ground
    bool active;                             // Qucs's per-element active flag
    QPoint pos;                              // grid-aligned origin
    int rot;                                 // 0..3, each step 90 degrees clockwise
    bool mirror;
    QStringList params;                      // property values, catalogue order
    QVector<bool> show;                      // draw that property on the sheet

    Component() : active(true), rot(0), mirror(false) {}
    Component(const QString &type_, const QPoint &pos_);

    int paramCount() const;
    QString param(int i) const;              // "" past the end
    void setParam(int i, const QString &v);
    double value(int i) const;               // param(i) through str2num()
    QString paramName(int i) const;
    QString paramUnit(int i) const;
    QString paramDesc(int i) const;
    QStringList paramChoices(int i) const;   // empty when it is a free field

    // Text as the sheet shows it: "R1" plus every property flagged visible.
    QStringList labelLines() const;
};

// ---- catalogue --------------------------------------------------------------

struct CompPort { int x, y; };

struct ParamDef {
    const char *name;
    const char *def;
    const char *unit;
    const char *desc;
    const char *choices;                     // "npn|pnp", or 0 for a free field
};

enum CompFlag {
    CompIsGround = 0x1,                      // one port, ties the net to node 0
    CompIsSim    = 0x2,                      // a simulation block, not an element
    CompIsSource = 0x4,                      // has a branch current worth naming
    CompIsNonlin = 0x8                       // needs Newton-Raphson
};

struct CompDef {
    const char *type;                        // "R"
    const char *label;                       // "Resistor"
    const char *prefix;                      // auto-naming prefix
    const char *category;                    // library dock group
    int flags;
    const CompPort *ports;
    int nports;
    const ParamDef *params;
    int nparams;
};

const CompDef *compDef(const QString &type);
QList<const CompDef *> compCatalogue();
QStringList compCategories();
QList<const CompDef *> compsInCategory(const QString &category);

// ---- geometry and painting --------------------------------------------------

// Local port offsets of `type`, before rotation and mirroring.
QList<QPoint> compLocalPorts(const QString &type);
// The same ports on the sheet.
QList<QPoint> compPorts(const Component &c);
// Bounding box in local coordinates, used for hit testing and icons.
QRectF compLocalBounds(const QString &type);
QRect compRect(const Component &c);

QTransform compTransform(const QPoint &pos, int rot, bool mirror);

// Draws the symbol about the local origin; the painter must already carry
// compTransform() and the pen colour the caller wants (Qucs colours the
// elements dark blue and the selection red, the view does that).
void paintCompSymbol(QPainter *p, const Component &c);

// Library-dock icon: the symbol centred in a square pixmap.
QIcon compIcon(const QString &type, int size = 26);

#endif // QUCS_COMPONENTS_H
