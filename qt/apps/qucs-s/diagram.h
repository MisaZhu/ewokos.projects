/*
 * Qucs-S, ported to EwokOS - the diagrams.
 *
 * Upstream: https://github.com/ra3xdh/qucs_s (GPL-2.0), whose plots are a class
 * hierarchy under qucs/diagrams/ (diagram.cpp, rectdiagram.cpp, ...) that also
 * render to SVG and to a printer.  Here a diagram is the Diagram record of
 * schematic.h plus one painting routine: the schematic sheet is the only
 * surface it is ever drawn on, and this Qt build has neither QtSvg nor a
 * printer.  What is kept is upstream's rectangular cartesian diagram - frame,
 * ticks on a 1/2/5 x 10^n scale, one coloured trace per variable - with its
 * logarithmic axes and its magnitude/dB/phase views of a complex result.
 */

#ifndef QUCS_DIAGRAM_H
#define QUCS_DIAGRAM_H

#include <QColor>
#include <QString>

#include "schematic.h"
#include "simulator.h"

class QPainter;
class QFont;

// The unit a variable name implies: "_net0" is a node voltage, "V1.I" the
// current of a branch.
QString diagramVarUnit(const QString &name);

// What the legend calls a traced variable, plot mode included: "_net0 / V" for
// a real plot, "|_net0| / V" for a magnitude, "dB(_net0)" for decibels.
QString diagramVarCaption(const QString &name, int plot);

// Trace colours, cycling in the order the variables are listed.
QColor diagramCurveColor(int index);

// Draws one diagram onto the sheet.  `res` may be 0, or may not carry the
// variables the diagram asks for - it is then drawn as an empty frame with a
// hint, which is what a sheet looks like before its first simulation.
void paintDiagram(QPainter *p, const Diagram &d, const SimResult *res, const QFont &font);

#endif // QUCS_DIAGRAM_H
