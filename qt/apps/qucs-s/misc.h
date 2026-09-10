/*
 * Qucs-S, ported to EwokOS.
 *
 * Upstream: https://github.com/ra3xdh/qucs_s (GPL-2.0).
 *
 * The value <-> text conversion of upstream's qucs/misc.cpp, reduced to what
 * a schematic needs: SPICE-style engineering suffixes on the way in
 * ("4k7", "100n", "10 Meg", "2.2pF"), engineering notation on the way out
 * ("4.7k", "100n").  Also the axis tick helper the diagrams use, which
 * upstream keeps in qucs/diagrams/diagram.cpp.
 */

#ifndef QUCS_MISC_H
#define QUCS_MISC_H

#include <QString>
#include <QVector>

// Parses a component property such as "4k7", "1e-9", "10 Meg" or "2.2pF".
// Returns 0.0 (and sets ok=false when given) on garbage rather than throwing
// - this build has no exceptions, and a bad value in a file must not abort.
double str2num(const QString &text, bool *ok = 0);

// Engineering notation with at most `prec` significant decimals, trailing
// zeros dropped: 4700 -> "4.7k", 1e-9 -> "1n".  The unit suffix ("Ohm", "F")
// is the caller's business, since it depends on the component.
QString num2str(double value, int prec = 4);

// Value plus its unit in the form a Qucs property carries: "4.7k Ohm".
QString num2strUnit(double value, const QString &unit, int prec = 4);

// `count`-ish ticks over [lo, hi] with a 1/2/5 x 10^n step, as an axis wants.
// Returns the step and writes the first tick >= lo into *first.
double niceTicks(double lo, double hi, int count, double *first);

// Formats an axis label: engineering suffix below 1e-3 / above 1e4, plain
// %g in between, so a 1 kHz..10 MHz sweep reads "1k".."10Meg".
QString axisNum(double value);

#endif // QUCS_MISC_H
