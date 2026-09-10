/*
 * Qucs-S, ported to EwokOS - component catalogue and symbols.
 *
 * The symbols follow upstream's European style (resistor as a rectangle,
 * ground as the three-bar sign) and are drawn in a local coordinate system
 * with the sheet grid of 10 units, ports on grid points, y downwards.  The
 * view applies compTransform() before calling paintCompSymbol(), so rotating
 * and mirroring a component is a matter of its rot/mirror fields and nothing
 * here has to know about either.
 */

#include "components.h"

#include "misc.h"

#include <QFont>
#include <QPainter>
#include <QPixmap>

#include <math.h>

// ---- ports ------------------------------------------------------------------

static const CompPort kTwo[]   = { { -20, 0 }, { 20, 0 } };
static const CompPort kGnd[]   = { { 0, -20 } };
static const CompPort kBjt[]   = { { -20, 0 }, { 10, -20 }, { 10, 20 } };
static const CompPort kFour[]  = { { -20, -10 }, { -20, 10 }, { 20, -10 }, { 20, 10 } };

// ---- properties -------------------------------------------------------------
//
// Names and defaults are upstream's (qucs/components/resistor.cpp and the rest
// register them in the same order), trimmed to the ones this simulator reads.

static const ParamDef kRParams[] = {
    { "R", "1 kOhm", "Ohm", "resistance", 0 },
};
static const ParamDef kCParams[] = {
    { "C", "100 nF", "F", "capacitance", 0 },
    { "V", "0 V", "V", "initial voltage", 0 },
};
static const ParamDef kLParams[] = {
    { "L", "1 mH", "H", "inductance", 0 },
    { "I", "0 A", "A", "initial current", 0 },
};
static const ParamDef kVdcParams[] = {
    { "U", "5 V", "V", "voltage", 0 },
};
static const ParamDef kIdcParams[] = {
    { "I", "1 mA", "A", "current", 0 },
};
static const ParamDef kVacParams[] = {
    { "U", "1 V", "V", "amplitude", 0 },
    { "Phase", "0", "deg", "phase", 0 },
    { "f", "1 kHz", "Hz", "frequency", 0 },
    { "Theta", "0", "1/s", "damping", 0 },
};
static const ParamDef kVpulseParams[] = {
    { "U1", "0 V", "V", "low voltage", 0 },
    { "U2", "5 V", "V", "high voltage", 0 },
    { "T1", "0 s", "s", "delay time", 0 },
    { "T2", "1 ns", "s", "rise time", 0 },
    { "T3", "1 us", "s", "pulse width", 0 },
    { "T4", "1 ns", "s", "fall time", 0 },
    { "T5", "10 us", "s", "period", 0 },
};
static const ParamDef kDiodeParams[] = {
    { "Is", "1e-15 A", "A", "saturation current", 0 },
    { "N", "1", "", "emission coefficient", 0 },
    { "Rs", "0 Ohm", "Ohm", "series resistance", 0 },
    { "Cj0", "0 F", "F", "junction capacitance at Vj", 0 },
    { "Vj", "0.7 V", "V", "junction potential", 0 },
    { "M", "0.5", "", "grading coefficient", 0 },
};
static const ParamDef kBjtParams[] = {
    { "Type", "npn", "", "polarity", "npn|pnp" },
    { "Is", "1e-16 A", "A", "saturation current", 0 },
    { "Bf", "100", "", "forward beta", 0 },
    { "Br", "1", "", "reverse beta", 0 },
};
static const ParamDef kVcvsParams[] = {
    { "Gain", "1", "", "voltage gain", 0 },
};
static const ParamDef kVccsParams[] = {
    { "Gain", "1", "S", "transconductance", 0 },
};
static const ParamDef kCcvsParams[] = {
    { "Source", "", "", "controlling voltage source", 0 },
    { "Gain", "1", "Ohm", "transresistance", 0 },
};
static const ParamDef kCccsParams[] = {
    { "Source", "", "", "controlling voltage source", 0 },
    { "Gain", "1", "", "current gain", 0 },
};
static const ParamDef kDcSimParams[] = {
    { "Src", "", "", "swept source (empty: operating point)", 0 },
    { "Start", "0 V", "", "sweep start", 0 },
    { "Stop", "5 V", "", "sweep stop", 0 },
    { "Step", "0.5 V", "", "sweep step", 0 },
};
static const ParamDef kAcSimParams[] = {
    { "Type", "log", "", "sweep type", "lin|log" },
    { "Start", "10 Hz", "Hz", "start frequency", 0 },
    { "Stop", "10 MHz", "Hz", "stop frequency", 0 },
    { "Number", "201", "", "number of points", 0 },
};
static const ParamDef kTranSimParams[] = {
    { "Start", "0 s", "s", "start time", 0 },
    { "Stop", "10 ms", "s", "stop time", 0 },
    { "Number", "501", "", "number of steps", 0 },
};

#define NP(a) a, (int)(sizeof(a) / sizeof(a[0]))

static const CompDef kCatalogue[] = {
    { "R",      "Resistor",        "R",    "Lumped components",  0,
      NP(kTwo),  NP(kRParams) },
    { "C",      "Capacitor",       "C",    "Lumped components",  0,
      NP(kTwo),  NP(kCParams) },
    { "L",      "Inductor",        "L",    "Lumped components",  CompIsSource,
      NP(kTwo),  NP(kLParams) },

    { "GND",    "Ground",          "*",    "Sources",            CompIsGround,
      NP(kGnd),  0, 0 },
    { "Vdc",    "DC voltage",      "V",    "Sources",            CompIsSource,
      NP(kTwo),  NP(kVdcParams) },
    { "Idc",    "DC current",      "I",    "Sources",            0,
      NP(kTwo),  NP(kIdcParams) },
    { "Vac",    "AC voltage",      "V",    "Sources",            CompIsSource,
      NP(kTwo),  NP(kVacParams) },
    { "Vpulse", "Pulse voltage",   "V",    "Sources",            CompIsSource,
      NP(kTwo),  NP(kVpulseParams) },

    { "Diode",  "Diode",           "D",    "Nonlinear",          CompIsNonlin,
      NP(kTwo),  NP(kDiodeParams) },
    { "BJT",    "Bipolar junction transistor", "Q", "Nonlinear",  CompIsNonlin,
      NP(kBjt),  NP(kBjtParams) },

    { "VCVS",   "Voltage controlled voltage source", "VCVS", "Controlled sources",
      CompIsSource, NP(kFour), NP(kVcvsParams) },
    { "VCCS",   "Voltage controlled current source", "VCCS", "Controlled sources",
      0, NP(kFour), NP(kVccsParams) },
    { "CCVS",   "Current controlled voltage source", "CCVS", "Controlled sources",
      CompIsSource, NP(kFour), NP(kCcvsParams) },
    { "CCCS",   "Current controlled current source", "CCCS", "Controlled sources",
      0, NP(kFour), NP(kCccsParams) },

    { "Iprobe", "Current probe",   "Pr",   "Probes",             CompIsSource,
      NP(kTwo),  0, 0 },

    { "DC",     "DC simulation",   "DC",   "Simulations",        CompIsSim,
      0, 0,      NP(kDcSimParams) },
    { "AC",     "AC simulation",   "AC",   "Simulations",        CompIsSim,
      0, 0,      NP(kAcSimParams) },
    { "Tran",   "Transient simulation", "Tran", "Simulations",   CompIsSim,
      0, 0,      NP(kTranSimParams) },
};

static const int kCatalogueCount = (int)(sizeof(kCatalogue) / sizeof(kCatalogue[0]));

const CompDef *compDef(const QString &type)
{
    for (int i = 0; i < kCatalogueCount; i++) {
        if (type == QLatin1String(kCatalogue[i].type))
            return &kCatalogue[i];
    }
    return 0;
}

QList<const CompDef *> compCatalogue()
{
    QList<const CompDef *> out;
    for (int i = 0; i < kCatalogueCount; i++)
        out.append(&kCatalogue[i]);
    return out;
}

QStringList compCategories()
{
    QStringList cats;
    for (int i = 0; i < kCatalogueCount; i++) {
        const QString c = QLatin1String(kCatalogue[i].category);
        if (!cats.contains(c))
            cats.append(c);
    }
    return cats;
}

QList<const CompDef *> compsInCategory(const QString &category)
{
    QList<const CompDef *> out;
    for (int i = 0; i < kCatalogueCount; i++) {
        if (category == QLatin1String(kCatalogue[i].category))
            out.append(&kCatalogue[i]);
    }
    return out;
}

// ---- Component --------------------------------------------------------------

Component::Component(const QString &type_, const QPoint &pos_)
    : type(type_), active(true), pos(pos_), rot(0), mirror(false)
{
    const CompDef *def = compDef(type);
    if (!def)
        return;
    name = QLatin1String(def->prefix);
    for (int i = 0; i < def->nparams; i++) {
        params.append(QLatin1String(def->params[i].def));
        // Upstream shows the value of a passive part but hides the model
        // parameters of a diode or transistor, which would clutter the sheet.
        show.append(i == 0 && (def->flags & (CompIsSim | CompIsNonlin)) == 0);
    }
}

int Component::paramCount() const
{
    const CompDef *def = compDef(type);
    return def ? def->nparams : 0;
}

QString Component::param(int i) const
{
    return (i >= 0 && i < params.size()) ? params.at(i) : QString();
}

void Component::setParam(int i, const QString &v)
{
    while (params.size() <= i) {
        params.append(QString());
        show.append(false);
    }
    params[i] = v;
}

double Component::value(int i) const
{
    return str2num(param(i));
}

static const ParamDef *paramDefAt(const Component &c, int i)
{
    const CompDef *def = compDef(c.type);
    if (!def || i < 0 || i >= def->nparams)
        return 0;
    return &def->params[i];
}

QString Component::paramName(int i) const
{
    const ParamDef *p = paramDefAt(*this, i);
    return p ? QLatin1String(p->name) : QString();
}

QString Component::paramUnit(int i) const
{
    const ParamDef *p = paramDefAt(*this, i);
    return (p && p->unit[0]) ? QLatin1String(p->unit) : QString();
}

QString Component::paramDesc(int i) const
{
    const ParamDef *p = paramDefAt(*this, i);
    return p ? QLatin1String(p->desc) : QString();
}

QStringList Component::paramChoices(int i) const
{
    const ParamDef *p = paramDefAt(*this, i);
    if (!p || !p->choices)
        return QStringList();
    return QString(QLatin1String(p->choices)).split(QLatin1Char('|'));
}

QStringList Component::labelLines() const
{
    QStringList lines;
    if (name != QLatin1String("*"))
        lines.append(name);
    for (int i = 0; i < params.size(); i++) {
        if (i < show.size() && show.at(i))
            lines.append(params.at(i));
    }
    return lines;
}

// ---- geometry ---------------------------------------------------------------

QList<QPoint> compLocalPorts(const QString &type)
{
    QList<QPoint> out;
    const CompDef *def = compDef(type);
    if (!def)
        return out;
    for (int i = 0; i < def->nports; i++)
        out.append(QPoint(def->ports[i].x, def->ports[i].y));
    return out;
}

QTransform compTransform(const QPoint &pos, int rot, bool mirror)
{
    QTransform t;
    t.translate(pos.x(), pos.y());
    t.rotate(rot * 90.0);
    if (mirror)
        t.scale(-1, 1);
    return t;
}

QList<QPoint> compPorts(const Component &c)
{
    const QTransform t = compTransform(c.pos, c.rot, c.mirror);
    const QList<QPoint> local = compLocalPorts(c.type);
    QList<QPoint> out;
    for (int i = 0; i < local.size(); i++) {
        const QPointF p = t.map(QPointF(local.at(i)));
        out.append(QPoint(qRound(p.x()), qRound(p.y())));
    }
    return out;
}

QRectF compLocalBounds(const QString &type)
{
    const CompDef *def = compDef(type);
    if (!def)
        return QRectF(-10, -10, 20, 20);
    if (def->flags & CompIsSim)
        return QRectF(-50, -25, 100, 50);
    switch (def->nports) {
    case 1:  return QRectF(-12, -20, 24, 32);        // ground
    case 3:  return QRectF(-20, -20, 32, 40);        // transistor
    case 4:  return QRectF(-20, -18, 40, 36);        // controlled source
    default: return QRectF(-20, -10, 40, 20);        // two-terminal
    }
}

QRect compRect(const Component &c)
{
    const QTransform t = compTransform(c.pos, c.rot, c.mirror);
    // QTransform maps a rectangle to the polygon of its four corners, which
    // for a 90 degree turn is a rectangle again but only boundingRect() says
    // so - and the label the view draws beside it is not part of this.
    return t.map(compLocalBounds(c.type)).boundingRect().toAlignedRect();
}

// ---- symbols ----------------------------------------------------------------

static void drawArrow(QPainter *p, const QPointF &from, const QPointF &to, double head)
{
    p->drawLine(from, to);
    const QPointF d = to - from;
    const double len = sqrt(d.x() * d.x() + d.y() * d.y());
    if (len < 1e-6)
        return;
    const QPointF u(d.x() / len, d.y() / len);
    const QPointF n(-u.y(), u.x());
    QPolygonF tri;
    tri << to << (to - u * head + n * head * 0.6) << (to - u * head - n * head * 0.6);
    const QPen old = p->pen();
    const QBrush ob = p->brush();
    p->setBrush(old.color());
    p->drawPolygon(tri);
    p->setBrush(ob);
}

static void drawSine(QPainter *p, double x0, double x1, double amp)
{
    QPolygonF poly;
    const int steps = 24;
    for (int i = 0; i <= steps; i++) {
        const double x = x0 + (x1 - x0) * i / steps;
        poly << QPointF(x, -amp * sin(2.0 * M_PI * i / steps));
    }
    p->drawPolyline(poly);
}

// The four controlled sources share a box with the type name in it; only what
// is drawn inside differs (a voltage or a current arrow).
static void drawControlled(QPainter *p, const Component &c)
{
    p->drawLine(QPointF(-20, -10), QPointF(-14, -10));
    p->drawLine(QPointF(-20, 10), QPointF(-14, 10));
    p->drawLine(QPointF(14, -10), QPointF(20, -10));
    p->drawLine(QPointF(14, 10), QPointF(20, 10));
    p->drawRect(QRectF(-14, -16, 28, 32));

    const bool currentOut = (c.type == QLatin1String("VCCS") || c.type == QLatin1String("CCCS"));
    const bool currentIn = (c.type == QLatin1String("CCVS") || c.type == QLatin1String("CCCS"));

    QFont f = p->font();
    f.setPixelSize(9);
    p->setFont(f);
    p->drawText(QRectF(-14, -16, 28, currentOut ? 20 : 32),
                Qt::AlignCenter, currentIn ? QLatin1String("C") : QLatin1String("V"));
    if (currentOut)
        drawArrow(p, QPointF(-8, 8), QPointF(8, 8), 5.0);
    if (currentIn)
        p->drawText(QRectF(-14, 0, 28, 16), Qt::AlignCenter, QLatin1String("I"));
}

// A simulation block: upstream draws the name and the parameters inside a
// rectangle, and grays an inactive one out.
static void drawSimBlock(QPainter *p, const Component &c)
{
    QPen pen = p->pen();
    if (!c.active)
        pen.setStyle(Qt::DashLine);
    p->setPen(pen);
    p->drawRect(QRectF(-50, -25, 100, 50));
    p->setPen(QPen(pen.color(), 1));

    QFont f = p->font();
    f.setPixelSize(11);
    f.setBold(true);
    p->setFont(f);
    QString title = c.type;
    if (c.name != QLatin1String("*") && !c.name.isEmpty())
        title += QLatin1Char(' ') + c.name;
    p->drawText(QRectF(-46, -23, 92, 14), Qt::AlignCenter, title);

    f.setPixelSize(9);
    f.setBold(false);
    p->setFont(f);
    int y = -6;
    for (int i = 0; i < c.params.size() && y < 20; i++, y += 11) {
        const QString v = c.params.at(i).trimmed();
        if (v.isEmpty())
            continue;
        p->drawText(QRectF(-46, y, 92, 11), Qt::AlignLeft | Qt::AlignVCenter,
                    c.paramName(i) + QLatin1Char('=') + v);
    }
}

void paintCompSymbol(QPainter *p, const Component &c)
{
    const QString t = c.type;

    if (compDef(t) && (compDef(t)->flags & CompIsSim)) {
        drawSimBlock(p, c);
        return;
    }

    if (t == QLatin1String("R")) {
        p->drawLine(QPointF(-20, 0), QPointF(-10, 0));
        p->drawLine(QPointF(10, 0), QPointF(20, 0));
        p->drawRect(QRectF(-10, -5, 20, 10));
    } else if (t == QLatin1String("C")) {
        p->drawLine(QPointF(-20, 0), QPointF(-4, 0));
        p->drawLine(QPointF(4, 0), QPointF(20, 0));
        p->drawLine(QPointF(-4, -8), QPointF(-4, 8));
        p->drawLine(QPointF(4, -8), QPointF(4, 8));
    } else if (t == QLatin1String("L")) {
        p->drawLine(QPointF(-20, 0), QPointF(-15, 0));
        p->drawLine(QPointF(15, 0), QPointF(20, 0));
        for (int i = 0; i < 3; i++)
            p->drawArc(QRectF(-15 + i * 10, -5, 10, 10), 0, 180 * 16);
    } else if (t == QLatin1String("GND")) {
        p->drawLine(QPointF(0, -20), QPointF(0, -2));
        p->drawLine(QPointF(-10, -2), QPointF(10, -2));
        p->drawLine(QPointF(-6, 4), QPointF(6, 4));
        p->drawLine(QPointF(-2, 10), QPointF(2, 10));
    } else if (t == QLatin1String("Diode")) {
        p->drawLine(QPointF(-20, 0), QPointF(-6, 0));
        p->drawLine(QPointF(6, 0), QPointF(20, 0));
        QPolygonF tri;
        tri << QPointF(-6, -7) << QPointF(-6, 7) << QPointF(6, 0);
        const QBrush ob = p->brush();
        p->setBrush(p->pen().color());
        p->drawPolygon(tri);
        p->setBrush(ob);
        p->drawLine(QPointF(6, -7), QPointF(6, 7));
    } else if (t == QLatin1String("BJT")) {
        const bool pnp = (c.param(0).trimmed().toLower() == QLatin1String("pnp"));
        p->drawLine(QPointF(-20, 0), QPointF(-2, 0));
        p->drawLine(QPointF(-2, -10), QPointF(-2, 10));            // base bar
        QPolygonF col;
        col << QPointF(10, -20) << QPointF(10, -8) << QPointF(-2, -5);
        p->drawPolyline(col);
        const QPointF e0(-2, 5), e1(10, 8), e2(10, 20);
        p->drawLine(e1, e2);
        // The emitter arrow points out of an npn and into a pnp.
        if (pnp)
            drawArrow(p, e1, e0, 5.0);
        else
            drawArrow(p, e0, e1, 5.0);
    } else if (t == QLatin1String("Vdc")) {
        p->drawLine(QPointF(-20, 0), QPointF(-8, 0));
        p->drawLine(QPointF(8, 0), QPointF(20, 0));
        p->drawEllipse(QPointF(0, 0), 8, 8);
        p->drawLine(QPointF(-6, 0), QPointF(-2, 0));
        p->drawLine(QPointF(-4, -2), QPointF(-4, 2));              // '+'
        p->drawLine(QPointF(2, 0), QPointF(6, 0));                 // '-'
    } else if (t == QLatin1String("Vac")) {
        p->drawLine(QPointF(-20, 0), QPointF(-8, 0));
        p->drawLine(QPointF(8, 0), QPointF(20, 0));
        p->drawEllipse(QPointF(0, 0), 8, 8);
        p->save();
        p->setClipRect(QRectF(-8, -8, 16, 16));
        drawSine(p, -6, 6, 3);
        p->restore();
    } else if (t == QLatin1String("Vpulse")) {
        p->drawLine(QPointF(-20, 0), QPointF(-8, 0));
        p->drawLine(QPointF(8, 0), QPointF(20, 0));
        p->drawEllipse(QPointF(0, 0), 8, 8);
        QPolygonF sq;
        sq << QPointF(-6, 3) << QPointF(-3, 3) << QPointF(-3, -3)
           << QPointF(3, -3) << QPointF(3, 3) << QPointF(6, 3);
        p->drawPolyline(sq);
    } else if (t == QLatin1String("Idc")) {
        p->drawLine(QPointF(-20, 0), QPointF(-8, 0));
        p->drawLine(QPointF(8, 0), QPointF(20, 0));
        p->drawEllipse(QPointF(0, 0), 8, 8);
        drawArrow(p, QPointF(-4, 0), QPointF(4, 0), 4.0);
    } else if (t == QLatin1String("Iprobe")) {
        p->drawLine(QPointF(-20, 0), QPointF(-8, 0));
        p->drawLine(QPointF(8, 0), QPointF(20, 0));
        // The little end ticks are what tells a probe from a current source.
        p->drawLine(QPointF(-12, -4), QPointF(-12, 4));
        p->drawLine(QPointF(12, -4), QPointF(12, 4));
        p->drawEllipse(QPointF(0, 0), 8, 8);
        drawArrow(p, QPointF(-4, 0), QPointF(4, 0), 4.0);
    } else if (t == QLatin1String("VCVS") || t == QLatin1String("VCCS")
               || t == QLatin1String("CCVS") || t == QLatin1String("CCCS")) {
        drawControlled(p, c);
    } else {
        // Unknown type from a newer file: a labelled box keeps it visible and
        // selectable instead of drawing nothing at all.
        p->drawRect(QRectF(-20, -10, 40, 20));
        QFont f = p->font();
        f.setPixelSize(9);
        p->setFont(f);
        p->drawText(QRectF(-20, -10, 40, 20), Qt::AlignCenter, t);
    }
}

QIcon compIcon(const QString &type, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);

    Component c(type, QPoint(0, 0));
    const QRectF b = compLocalBounds(type);
    const double scale = qMin((size - 6.0) / b.width(), (size - 6.0) / b.height());

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(size / 2.0 - b.center().x() * scale, size / 2.0 - b.center().y() * scale);
    p.scale(scale, scale);
    // A hairline at icon size: the pen is scaled back up by the transform, so
    // ask for 1.4 device pixels in local units.
    p.setPen(QPen(QColor(0, 0, 130), 1.4 / scale));
    paintCompSymbol(&p, c);
    p.end();
    return QIcon(pm);
}
