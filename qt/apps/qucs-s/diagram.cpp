/*
 * Qucs-S, ported to EwokOS - the diagrams.
 */

#include "diagram.h"

#include "misc.h"

#include <QFont>
#include <QFontMetrics>
#include <QList>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

#include <math.h>

// ---- axis -------------------------------------------------------------------

// One axis: the data range it covers and where a value lands on it.  The
// caller passes the pixel edges in the order lo -> hi, so a y axis is given
// bottom and top and comes out the right way up.
struct Axis {
    double lo, hi;
    bool log;

    Axis() : lo(0.0), hi(1.0), log(false) {}

    double at(double v, double px0, double px1) const
    {
        if (px1 == px0)
            return px0;
        if (log) {
            if (!(v > 0.0))
                return px0;                    // a non-positive value has no place
            const double b = log10(lo), c = log10(hi);
            if (!(c > b))
                return px0;
            return px0 + (px1 - px0) * (log10(v) - b) / (c - b);
        }
        if (!(hi > lo))
            return px0;
        return px0 + (px1 - px0) * (v - lo) / (hi - lo);
    }
};

static bool usable(double v)
{
    return v < 1e250 && v > -1e250;            // rejects inf and nan alike
}

// The sweep axis.  A logarithmic one is what an AC diagram wants, and it is
// only used when the sweep really is positive and rising.
static void xAxis(const Diagram &d, const SimResult *res, Axis *a)
{
    *a = Axis();
    if (!res || res->sweep.isEmpty())
        return;
    double lo = res->sweep.first(), hi = res->sweep.last();
    if (lo > hi)
        qSwap(lo, hi);
    if (hi <= lo)
        hi = lo + 1.0;
    if (d.logX && lo > 0.0)
        a->log = true;
    a->lo = lo;
    a->hi = hi;
    if (a->log) {
        a->lo = pow(10.0, floor(log10(lo)));
        a->hi = pow(10.0, ceil(log10(hi)));
        if (!(a->hi > a->lo))
            a->hi = a->lo * 10.0;
    }
}

// The value axis, over every variable the diagram traces.
static void yAxis(const Diagram &d, const SimResult *res, Axis *a)
{
    *a = Axis();
    if (!res || res->sweep.isEmpty())
        return;

    double lo = 0.0, hi = 0.0;
    double smallestPositive = 1e300;
    bool any = false;
    for (int k = 0; k < d.vars.size(); k++) {
        const int vi = res->varIndex(d.vars.at(k));
        if (vi < 0)
            continue;
        const SimVar &v = res->vars.at(vi);
        const int n = qMin(v.re.size(), res->sweep.size());
        for (int i = 0; i < n; i++) {
            const double y = v.valueAt(i, d.plot);
            if (!usable(y))
                continue;
            if (!any) {
                lo = hi = y;
                any = true;
            } else {
                if (y < lo) lo = y;
                if (y > hi) hi = y;
            }
            if (y > 0.0 && y < smallestPositive)
                smallestPositive = y;
        }
    }
    if (!any)
        return;

    // A logarithmic axis cannot carry zero or a negative value, so it starts at
    // the smallest positive sample and the rest simply falls off the bottom.
    if (d.logY && smallestPositive < 1e300 && hi > smallestPositive) {
        a->log = true;
        a->lo = pow(10.0, floor(log10(smallestPositive)));
        a->hi = pow(10.0, ceil(log10(hi)));
        if (!(a->hi > a->lo))
            a->hi = a->lo * 10.0;
        return;
    }

    // Extended to whole ticks, which leaves the trace a little air above and
    // below instead of running along the frame.
    double first;
    const double step = niceTicks(lo, hi, 6, &first);
    a->lo = floor(lo / step) * step;
    a->hi = ceil(hi / step) * step;
    if (a->hi - a->lo < step) {                  // a flat trace, or one landing
        a->lo -= step;                           // exactly on two ticks
        a->hi += step;
    }
}

// The tick positions of an axis.  A logarithmic one gets its decades, with
// 2 and 5 in between while there are few enough decades to label them.
static QList<double> axisTicks(const Axis &a, int count)
{
    QList<double> out;
    if (a.log) {
        const double d0 = floor(log10(a.lo)), d1 = ceil(log10(a.hi));
        const bool subdivide = (d1 - d0) <= 3.0;
        for (double e = d0; e <= d1 + 1e-9; e += 1.0) {
            const double base = pow(10.0, e);
            if (base > a.hi * (1.0 + 1e-9))
                break;
            out.append(base);
            if (!subdivide)
                continue;
            static const double sub[2] = { 2.0, 5.0 };
            for (int k = 0; k < 2; k++) {
                const double v = base * sub[k];
                if (v <= a.hi * (1.0 + 1e-9))
                    out.append(v);
            }
        }
        return out;
    }

    double first;
    const double step = niceTicks(a.lo, a.hi, count, &first);
    for (double v = first; v <= a.hi + step * 1e-6; v += step)
        out.append(v);
    return out;
}

// ---- captions and colours ---------------------------------------------------

QString diagramVarUnit(const QString &name)
{
    if (name.endsWith(QLatin1String(".I")))
        return QString("A");
    return QString("V");                       // a node voltage, or a source's
}

QString diagramVarCaption(const QString &name, int plot)
{
    switch (plot) {
    case Diagram::Magnitude:
        return QString("|%1| / %2").arg(name, diagramVarUnit(name));
    case Diagram::Decibel:
        return QString("dB(%1)").arg(name);
    case Diagram::Phase:
        return QString("ph(%1) / deg").arg(name);
    case Diagram::Imag:
        return QString("im(%1) / %2").arg(name, diagramVarUnit(name));
    default:
        return QString("%1 / %2").arg(name, diagramVarUnit(name));
    }
}

QColor diagramCurveColor(int index)
{
    // Upstream's trace palette, in its order: blue first, then red and green.
    static const QRgb kColors[] = {
        qRgb(0, 0, 190), qRgb(200, 0, 0), qRgb(0, 150, 0), qRgb(160, 0, 160),
        qRgb(0, 150, 160), qRgb(200, 120, 0), qRgb(100, 100, 100), qRgb(0, 0, 0)
    };
    static const int n = (int)(sizeof(kColors) / sizeof(kColors[0]));
    if (index < 0)
        index = 0;
    return QColor(kColors[index % n]);
}

// ---- painting ---------------------------------------------------------------

void paintDiagram(QPainter *p, const Diagram &d, const SimResult *res, const QFont &font)
{
    const QRect box = d.rect();
    if (box.width() < 60 || box.height() < 40)
        return;                                // dragged below anything drawable

    // The tick text is smaller than the sheet's own font, as upstream's is.
    QFont f = font;
    f.setPointSizeF(font.pointSizeF() * 0.8);
    p->save();
    p->setFont(f);
    p->setRenderHint(QPainter::Antialiasing, false);
    const QFontMetrics fm(f);

    Axis xa, ya;
    xAxis(d, res, &xa);
    yAxis(d, res, &ya);

    const QString xcap = (res && !res->xName.isEmpty())
                         ? (res->xUnit.isEmpty() ? res->xName
                            : res->xName + QLatin1String(" / ") + res->xUnit)
                         : QString();
    QStringList caps;
    for (int k = 0; k < d.vars.size(); k++)
        caps.append(diagramVarCaption(d.vars.at(k), d.plot));

    const QList<double> xt = axisTicks(xa, qMax(2, box.width() / 80));
    const QList<double> yt = axisTicks(ya, qMax(2, box.height() / 36));

    // ---- margins: whatever the tick text and the caption need --------------
    int left = 10, right = 10;
    for (int i = 0; i < yt.size(); i++)
        left = qMax(left, fm.horizontalAdvance(axisNum(yt.at(i))) + 10);
    if (!xt.isEmpty()) {
        // The outermost x labels hang half over the frame's corners.
        left = qMax(left, fm.horizontalAdvance(axisNum(xt.first())) / 2 + 4);
        right = qMax(right, fm.horizontalAdvance(axisNum(xt.last())) / 2 + 4);
    }
    int top = 8;
    int bottom = 6 + fm.height() + 2;
    if (!xcap.isEmpty())
        bottom += fm.height() + 2;

    const QRect plot(box.left() + left, box.top() + top,
                     box.width() - left - right, box.height() - top - bottom);
    if (plot.width() < 24 || plot.height() < 20) {
        p->setPen(QPen(QColor(30, 30, 30), 1));
        p->drawRect(plot);
        p->restore();
        return;
    }

    // ---- grid and frame ----------------------------------------------------
    p->setPen(QPen(QColor(218, 218, 218), 0));
    for (int i = 0; i < xt.size(); i++) {
        const int x = qRound(xa.at(xt.at(i), plot.left(), plot.right()));
        if (x > plot.left() && x < plot.right())
            p->drawLine(x, plot.top(), x, plot.bottom());
    }
    for (int i = 0; i < yt.size(); i++) {
        const int y = qRound(ya.at(yt.at(i), plot.bottom(), plot.top()));
        if (y > plot.top() && y < plot.bottom())
            p->drawLine(plot.left(), y, plot.right(), y);
    }

    p->setPen(QPen(QColor(30, 30, 30), 1));
    p->drawRect(plot);
    for (int i = 0; i < xt.size(); i++) {
        const double xv = xa.at(xt.at(i), plot.left(), plot.right());
        if (xv < plot.left() - 0.5 || xv > plot.right() + 0.5)
            continue;
        const int x = qRound(xv);
        p->drawLine(x, plot.bottom(), x, plot.bottom() + 4);
        p->drawText(QRect(x - 50, plot.bottom() + 5, 100, fm.height()),
                    Qt::AlignHCenter | Qt::AlignTop, axisNum(xt.at(i)));
    }
    for (int i = 0; i < yt.size(); i++) {
        const double yv = ya.at(yt.at(i), plot.bottom(), plot.top());
        if (yv < plot.top() - 0.5 || yv > plot.bottom() + 0.5)
            continue;
        const int y = qRound(yv);
        p->drawLine(plot.left() - 4, y, plot.left(), y);
        const QString s = axisNum(yt.at(i));
        p->drawText(QRect(plot.left() - 6 - fm.horizontalAdvance(s),
                          y - fm.height() / 2, fm.horizontalAdvance(s), fm.height()),
                    Qt::AlignRight | Qt::AlignVCenter, s);
    }
    if (!xcap.isEmpty()) {
        p->drawText(QRect(plot.left(), plot.bottom() + 7 + fm.height(),
                          plot.width(), fm.height()),
                    Qt::AlignHCenter | Qt::AlignTop, xcap);
    }

    // ---- the traces --------------------------------------------------------
    int traced = 0;
    if (res && !res->sweep.isEmpty()) {
        const int n = res->sweep.size();
        p->setClipRect(plot);
        for (int k = 0; k < d.vars.size(); k++) {
            const int vi = res->varIndex(d.vars.at(k));
            if (vi < 0)
                continue;
            const SimVar &v = res->vars.at(vi);
            p->setPen(QPen(diagramCurveColor(k), 1.6));
            QPolygonF poly;
            const int m = qMin(v.re.size(), n);
            for (int i = 0; i < m; i++) {
                const double y = v.valueAt(i, d.plot);
                if (!usable(y)) {                // nan or inf: break the trace
                    if (poly.size() > 1)
                        p->drawPolyline(poly);
                    poly.clear();
                    continue;
                }
                poly.append(QPointF(xa.at(res->sweep.at(i), plot.left(), plot.right()),
                                    ya.at(y, plot.bottom(), plot.top())));
            }
            if (poly.size() > 1)
                p->drawPolyline(poly);
            else if (poly.size() == 1)
                p->drawPoint(poly.at(0));        // a single operating point
            traced++;
        }
        p->setClipping(false);
    }

    if (traced == 0) {
        p->setPen(QColor(150, 150, 150));
        p->drawText(plot, Qt::AlignCenter,
                    res ? QString("nothing to plot") : QString("not simulated yet"));
        p->restore();
        return;
    }

    // ---- legend ------------------------------------------------------------
    // Inside the plot at the top right: the traces of the sweeps a diagram
    // usually carries - a filter, a transfer function - start low on the left.
    int lw = 0;
    for (int k = 0; k < caps.size(); k++)
        lw = qMax(lw, fm.horizontalAdvance(caps.at(k)));
    const int rowH = fm.height() + 2;
    const QRect lr(plot.right() - lw - 26, plot.top() + 5, lw + 22, caps.size() * rowH + 6);
    if (lr.left() > plot.left() + 8 && lr.top() + lr.height() < plot.bottom() - 4) {
        p->setPen(QPen(QColor(170, 170, 170), 0));
        p->setBrush(QColor(255, 255, 255, 210));
        p->drawRect(lr);
        p->setBrush(Qt::NoBrush);
        for (int k = 0; k < caps.size(); k++) {
            const int y = lr.top() + 4 + k * rowH + fm.height() / 2;
            p->setPen(QPen(diagramCurveColor(k), 1.6));
            p->drawLine(lr.left() + 3, y, lr.left() + 15, y);
            p->setPen(QColor(20, 20, 20));
            p->drawText(QPoint(lr.left() + 19, y + fm.ascent() / 2 - 1), caps.at(k));
        }
    }

    p->restore();
}
