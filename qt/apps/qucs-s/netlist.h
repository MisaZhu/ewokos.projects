/*
 * Qucs-S, ported to EwokOS - netlist extraction.
 *
 * Upstream: https://github.com/ra3xdh/qucs_s (GPL-2.0).  Upstream builds a
 * text netlist per simulation kernel (qucs/netlist.cpp writes SPICE for
 * ngspice/Xyce and the qucsator dialect for its own kernel) and hands it to a
 * child process.  There is no QProcess in this Qt build and no kernel to run,
 * so the extraction stops one step earlier: it turns the sheet into the
 * element/node tables that simulator.cpp solves in-process.
 *
 * Connectivity is upstream's rule, which is not SPICE's: elements join where
 * their ports land on a wire, including in the middle of one - a T junction
 * connects, two wires merely crossing do not.  Wire labels name the net.
 */

#ifndef QUCS_NETLIST_H
#define QUCS_NETLIST_H

#include <QList>
#include <QPoint>
#include <QString>
#include <QStringList>

#include "schematic.h"

enum ElemKind {
    ElResistor,
    ElCapacitor,
    ElInductor,
    ElVoltageSource,                         // Vdc, Vac, Vpulse, Iprobe, VCVS, CCVS
    ElCurrentSource,                         // Idc, VCCS, CCCS
    ElDiode,
    ElBjt
};

// How an independent source behaves in a transient run.
enum WaveKind {
    WaveNone,                                // a DC source: constant `value`
    WaveSine,                                // Vac
    WavePulse                                // Vpulse
};

// A controlled source's controlling quantity.
enum CtrlKind {
    CtrlNone,
    CtrlVoltage,                             // VCVS, VCCS: v(node[2]) - v(node[3])
    CtrlCurrent                              // CCVS, CCCS: branch current of ctrlIndex
};

struct Element {
    int kind;                                // ElemKind
    QString name;                            // instance name, for "V1.I"
    int node[4];                             // node indices; -1 when unused
    double value;                            // R, C, L, U, I or gain
    double p[8];                             // model parameters, per kind
    int wave;                                // WaveKind
    int ctrl;                                // CtrlKind
    QString ctrlName;                        // controlling source, CCVS/CCCS
    int ctrlIndex;                           // resolved index for CtrlCurrent
    bool reportsCurrent;                     // publish "<name>.I"
    bool active;

    Element();
};

struct Netlist {
    QStringList nodeNames;                   // index 0 is always ground
    QList<Element> elems;
    QList<QList<QPoint> > nodePoints;        // sheet points of each node
    QString error;                           // fatal: no simulation possible
    QStringList warnings;

    void clear();
    int nodeCount() const { return nodeNames.size(); }
    int indexOfSource(const QString &name) const;
};

// Ground is node 0 and gets no variable of its own.
enum { kGndNode = 0 };

bool buildNetlist(const Schematic &sch, Netlist *nl);

#endif // QUCS_NETLIST_H
