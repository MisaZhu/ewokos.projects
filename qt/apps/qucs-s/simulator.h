/*
 * Qucs-S, ported to EwokOS - the simulation kernel.
 *
 * Upstream: https://github.com/ra3xdh/qucs_s (GPL-2.0).  Upstream runs no
 * analysis itself: qucs/netlist.cpp writes a netlist and QProcess hands it to
 * ngspice, Xyce, SpiceOpus or the bundled qucsator, then qucs/dataset.cpp
 * reads back a .dat file.  This Qt build has no QProcess and EwokOS has no
 * SPICE kernel, so the analysis happens here, in process: modified nodal
 * analysis with a dense LU solve, Newton-Raphson for the junctions, and
 * trapezoidal integration for the transient run - the same three pieces
 * qucsator is built from, for the element set netlist.cpp extracts.
 *
 * Conventions worth knowing before reading the code:
 *
 *   - Node 0 is ground and has no unknown; the unknowns are the remaining
 *     node voltages followed by one branch current per voltage-source-like
 *     element (an independent source, a probe, a VCVS/CCVS, and an inductor
 *     in a DC or operating-point solve where it is a short).
 *   - "<name>.I" is the current flowing through the element from its first
 *     terminal to its second, which is SPICE's sign convention: a source that
 *     delivers power reads negative, and a current probe reads positive when
 *     the loop current enters its first terminal.
 *   - A CCVS or CCCS is controlled by that same current of the voltage source
 *     named in its Source property.
 */

#ifndef QUCS_SIMULATOR_H
#define QUCS_SIMULATOR_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

#include <math.h>

#include "netlist.h"

// ---- complex ----------------------------------------------------------------
//
// This C++ runtime ships no <complex>, and an AC sweep needs one, so here is
// the twenty lines of it that modified nodal analysis uses.

struct Cx {
    double re, im;

    Cx() : re(0.0), im(0.0) {}
    Cx(double r, double i = 0.0) : re(r), im(i) {}

    Cx operator+(const Cx &o) const { return Cx(re + o.re, im + o.im); }
    Cx operator-(const Cx &o) const { return Cx(re - o.re, im - o.im); }
    Cx operator-() const { return Cx(-re, -im); }
    Cx operator*(const Cx &o) const {
        return Cx(re * o.re - im * o.im, re * o.im + im * o.re);
    }
    Cx operator/(const Cx &o) const;
    Cx &operator+=(const Cx &o) { re += o.re; im += o.im; return *this; }
    Cx &operator-=(const Cx &o) { re -= o.re; im -= o.im; return *this; }

    double mag() const;
    double phaseDeg() const;
};

static inline double magnitude(double v) { return fabs(v); }
static inline double magnitude(const Cx &v) { return v.mag(); }

// ---- results ----------------------------------------------------------------

// How a diagram turns a (possibly complex) variable into a y value.
enum PlotMode {
    PlotReal = 0,
    PlotMagnitude,
    PlotDecibel,
    PlotPhase,
    PlotImag
};

struct SimVar {
    QString name;                            // "_net0", "V1.I"
    QVector<double> re;                      // the value; for AC the real part
    QVector<double> im;                      // AC only
    bool complex;

    SimVar() : complex(false) {}
    double valueAt(int i, int plot) const;
};

struct SimResult {
    QString sim;                             // simulation block name, "Tran1"
    QString kind;                            // "DC", "AC" or "Tran"
    QString xName;                           // "time", "frequency", "V1.U"
    QString xUnit;                           // "s", "Hz", "V"
    QVector<double> sweep;
    QList<SimVar> vars;
    QString error;                           // empty on success
    QStringList notes;                       // warnings and convergence info

    bool ok() const { return error.isEmpty(); }
    int varIndex(const QString &name) const;
    QStringList varNames() const;
};

// DC operating point, also what a transient run starts from.
struct OpPoint {
    QVector<double> nodeV;                   // by node index, [0] is 0
    QVector<double> elemI;                   // by element index, reported sign
};

// ---- analyses ---------------------------------------------------------------

// The parameters of one simulation block, read off the sheet.
struct SimParams {
    enum Kind { DC, AC, Tran } kind;
    QString name;
    QString src;                             // DC: swept source, may be empty
    bool log;                                // AC: logarithmic frequency axis
    double start, stop, step;                // DC uses step, the others do not
    int points;

    SimParams() : kind(DC), log(false), start(0), stop(0), step(0), points(0) {}
};

bool parseSimParams(const Component &simComp, SimParams *p, QString *error);

// One operating point at time `t` (t only matters for waveform sources).
bool operatingPoint(const Netlist &nl, double t, OpPoint *op, QString *error);

// Runs one simulation block; appends its notes and, on failure, sets error.
bool runSimulation(const Netlist &nl, const SimParams &p, SimResult *res);

// Current of an independent source at time t, for the transient waveforms.
double sourceValue(const Element &e, double t);

#endif // QUCS_SIMULATOR_H
