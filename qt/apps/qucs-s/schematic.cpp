/*
 * Qucs-S, ported to EwokOS - schematic model and its file format.
 */

#include "schematic.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>

// ---- Wire -------------------------------------------------------------------

QRect Wire::bounds() const
{
    QRect r(p1, p2);
    r.adjust(-3, -3, 4, 4);
    return r.normalized();
}

bool Wire::contains(const QPoint &p) const
{
    if (horizontal())
        return p.y() == p1.y() && p.x() >= qMin(p1.x(), p2.x()) && p.x() <= qMax(p1.x(), p2.x());
    return p.x() == p1.x() && p.y() >= qMin(p1.y(), p2.y()) && p.y() <= qMax(p1.y(), p2.y());
}

// ---- Diagram ----------------------------------------------------------------

Diagram::Diagram()
    : size(260, 170), logX(false), logY(false), plot(Real)
{
}

QString Diagram::plotName(int p)
{
    switch (p) {
    case Magnitude: return QString("magnitude");
    case Decibel:   return QString("dB");
    case Phase:     return QString("phase");
    case Imag:      return QString("imag");
    default:        return QString("real");
    }
}

// ---- Schematic --------------------------------------------------------------

Schematic::Schematic(QObject *parent)
    : QObject(parent), m_modified(false)
{
}

int Schematic::addComponent(const Component &c)
{
    m_components.append(c);
    m_modified = true;
    emit structureChanged();
    return m_components.size() - 1;
}

int Schematic::addWire(const Wire &w)
{
    // A zero-length wire is what a stray click in wire mode would produce.
    if (w.p1 == w.p2)
        return -1;
    m_wires.append(w);
    m_modified = true;
    emit structureChanged();
    return m_wires.size() - 1;
}

int Schematic::addDiagram(const Diagram &d)
{
    m_diagrams.append(d);
    m_modified = true;
    emit structureChanged();
    return m_diagrams.size() - 1;
}

// Removes the given indices from `list`, highest first so the remaining ones
// stay valid; reports whether anything went.
template <class T>
static bool removeIndices(QList<T> &list, QList<int> idx)
{
    std::sort(idx.begin(), idx.end());
    bool any = false;
    for (int i = idx.size() - 1; i >= 0; i--) {
        const int at = idx.at(i);
        if (at < 0 || at >= list.size())
            continue;
        list.removeAt(at);
        any = true;
    }
    return any;
}

void Schematic::removeComponents(const QList<int> &idx)
{
    if (removeIndices(m_components, idx)) {
        m_modified = true;
        emit structureChanged();
    }
}

void Schematic::removeWires(const QList<int> &idx)
{
    if (removeIndices(m_wires, idx)) {
        m_modified = true;
        emit structureChanged();
    }
}

void Schematic::removeDiagrams(const QList<int> &idx)
{
    if (removeIndices(m_diagrams, idx)) {
        m_modified = true;
        emit structureChanged();
    }
}

void Schematic::clear()
{
    m_components.clear();
    m_wires.clear();
    m_diagrams.clear();
    m_modified = false;
    emit structureChanged();
}

QList<int> Schematic::simComponents() const
{
    QList<int> out;
    for (int i = 0; i < m_components.size(); i++) {
        const CompDef *def = compDef(m_components.at(i).type);
        if (def && (def->flags & CompIsSim))
            out.append(i);
    }
    return out;
}

int Schematic::activeSim() const
{
    const QList<int> sims = simComponents();
    for (int i = 0; i < sims.size(); i++) {
        if (m_components.at(sims.at(i)).active)
            return sims.at(i);
    }
    return sims.isEmpty() ? -1 : sims.first();
}

QString Schematic::uniqueName(const QString &type) const
{
    const CompDef *def = compDef(type);
    if (!def)
        return type;
    // Ground carries no instance name in a Qucs file - the element line reads
    // `<GND * 1 x y 0 0>` - so every ground shares the "*".
    if (def->flags & CompIsGround)
        return QLatin1String("*");

    const QString prefix = QLatin1String(def->prefix);
    for (int n = 1; n < 10000; n++) {
        const QString candidate = prefix + QString::number(n);
        bool taken = false;
        for (int i = 0; i < m_components.size(); i++) {
            if (m_components.at(i).name == candidate) {
                taken = true;
                break;
            }
        }
        if (!taken)
            return candidate;
    }
    return prefix;
}

void Schematic::setModified(bool m)
{
    if (m_modified == m)
        return;
    m_modified = m;
    emit changed();
}

void Schematic::touch()
{
    m_modified = true;
    emit changed();
}

QString Schematic::displayName() const
{
    if (m_filePath.isEmpty())
        return QString("untitled.sch");
    return QFileInfo(m_filePath).fileName();
}

// ---- file format ------------------------------------------------------------

// Splits an element line on whitespace, keeping "quoted strings" in one piece
// and dropping the quotes - the fields of a Qucs element line are either bare
// numbers or quoted property values.
static QStringList splitFields(const QString &line)
{
    QStringList out;
    QString cur;
    bool inQuote = false;
    bool has = false;
    for (int i = 0; i < line.size(); i++) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('"')) {
            inQuote = !inQuote;
            has = true;
            continue;
        }
        if (!inQuote && ch.isSpace()) {
            if (has) {
                out.append(cur);
                cur.clear();
                has = false;
            }
            continue;
        }
        cur.append(ch);
        has = true;
    }
    if (has)
        out.append(cur);
    return out;
}

// Strips the enclosing angle brackets of an element line, if it has any.
static QString stripBrackets(const QString &line)
{
    QString s = line.trimmed();
    if (s.startsWith(QLatin1Char('<')))
        s.remove(0, 1);
    if (s.endsWith(QLatin1Char('>')))
        s.chop(1);
    return s.trimmed();
}

static int fieldInt(const QStringList &f, int i, int def = 0)
{
    if (i >= f.size())
        return def;
    bool ok = false;
    const int v = f.at(i).toInt(&ok);
    return ok ? v : def;
}

bool Schematic::save(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QTextStream ts(&file);
    ts << "<Qucs Schematic 0.0.19>\n";
    ts << "<Properties>\n";
    ts << "  View=0,0,800,600,1,0,0\n";
    ts << "  gridSize=" << kGrid << "\n";
    ts << "  showFrame=0\n";
    ts << "</Properties>\n";
    ts << "<Symbol>\n</Symbol>\n";

    ts << "<Components>\n";
    for (int i = 0; i < m_components.size(); i++) {
        const Component &c = m_components.at(i);
        ts << "  <" << c.type << ' ' << c.name << ' ' << (c.active ? 1 : 0)
           << ' ' << c.pos.x() << ' ' << c.pos.y() << ' ' << c.rot
           << ' ' << (c.mirror ? 1 : 0);
        for (int p = 0; p < c.params.size(); p++) {
            ts << " \"" << c.params.at(p) << "\" "
               << ((p < c.show.size() && c.show.at(p)) ? 1 : 0);
        }
        ts << ">\n";
    }
    ts << "</Components>\n";

    ts << "<Wires>\n";
    for (int i = 0; i < m_wires.size(); i++) {
        const Wire &w = m_wires.at(i);
        ts << "  <" << w.p1.x() << ' ' << w.p1.y() << ' '
           << w.p2.x() << ' ' << w.p2.y() << " \"" << w.label << "\" 0 0 0>\n";
    }
    ts << "</Wires>\n";

    ts << "<Diagrams>\n";
    for (int i = 0; i < m_diagrams.size(); i++) {
        const Diagram &d = m_diagrams.at(i);
        ts << "  <Rect " << d.pos.x() << ' ' << d.pos.y() << ' '
           << d.size.width() << ' ' << d.size.height() << ">\n";
        ts << "    vars=\"" << d.vars.join(QLatin1String(",")) << "\"\n";
        ts << "    logX=" << (d.logX ? 1 : 0) << "\n";
        ts << "    logY=" << (d.logY ? 1 : 0) << "\n";
        ts << "    plot=" << d.plot << "\n";
        ts << "  </Rect>\n";
    }
    ts << "</Diagrams>\n";
    ts << "<Paintings>\n</Paintings>\n";

    file.close();
    if (ts.status() != QTextStream::Ok) {
        if (error)
            *error = QString("write error");
        return false;
    }

    m_filePath = path;
    m_modified = false;
    emit changed();
    return true;
}

bool Schematic::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QTextStream ts(&file);
    const QString head = ts.readLine();
    if (!head.contains(QLatin1String("Qucs"))) {
        if (error)
            *error = QString("not a Qucs schematic");
        return false;
    }

    QList<Component> comps;
    QList<Wire> wireList;
    QList<Diagram> diags;

    // Section names nest one level, and a diagram carries its own inner
    // property lines, so a tiny state machine over the lines is enough.
    enum { None, InComponents, InWires, InDiagrams, InDiagram } section = None;
    Diagram pending;

    while (!ts.atEnd()) {
        const QString raw = ts.readLine();
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith(QLatin1Char('<')) && !line.startsWith(QLatin1String("</"))) {
            const QString tag = line.mid(1).section(QLatin1Char(' '), 0, 0);
            if (tag == QLatin1String("Components")) { section = InComponents; continue; }
            if (tag == QLatin1String("Wires"))      { section = InWires; continue; }
            if (tag == QLatin1String("Diagrams"))   { section = InDiagrams; continue; }
            if (tag == QLatin1String("Properties") || tag == QLatin1String("Symbol")
                || tag == QLatin1String("Paintings") || tag == QLatin1String("Qucs")) {
                section = None;
                continue;
            }
            if (section == InDiagrams && (tag == QLatin1String("Rect")
                                          || tag == QLatin1String("Tab")
                                          || tag == QLatin1String("Polar"))) {
                const QStringList f = splitFields(stripBrackets(line));
                pending = Diagram();
                pending.pos = snapToGrid(QPoint(fieldInt(f, 1), fieldInt(f, 2)));
                const int w = fieldInt(f, 3, 260), h = fieldInt(f, 4, 170);
                if (w > 20 && h > 20)
                    pending.size = QSize(w, h);
                section = InDiagram;
                continue;
            }
        }

        if (line.startsWith(QLatin1String("</"))) {
            const QString tag = line.mid(2).section(QLatin1Char(' '), 0, 0);
            if (tag == QLatin1String("Rect") || tag == QLatin1String("Tab")
                || tag == QLatin1String("Polar")) {
                diags.append(pending);
                pending = Diagram();
                section = InDiagrams;
            } else if (tag == QLatin1String("Components")) {
                section = None;
            } else if (tag == QLatin1String("Wires")) {
                section = None;
            } else if (tag == QLatin1String("Diagrams")) {
                section = None;
            }
            continue;
        }

        if (section == InDiagram) {
            // "vars=a,b" / "logX=1" / "plot=2"
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0)
                continue;
            const QString key = line.left(eq).trimmed();
            QString val = line.mid(eq + 1).trimmed();
            if (val.startsWith(QLatin1Char('"')) && val.endsWith(QLatin1Char('"')) && val.size() > 1)
                val = val.mid(1, val.size() - 2);
            if (key == QLatin1String("vars")) {
                pending.vars = val.split(QLatin1Char(','), Qt::SkipEmptyParts);
                for (int i = 0; i < pending.vars.size(); i++)
                    pending.vars[i] = pending.vars.at(i).trimmed();
            } else if (key == QLatin1String("logX")) {
                pending.logX = (val.toInt() != 0);
            } else if (key == QLatin1String("logY")) {
                pending.logY = (val.toInt() != 0);
            } else if (key == QLatin1String("plot")) {
                pending.plot = val.toInt();
            }
            continue;
        }

        if (section == InComponents || section == InWires) {
            const QStringList f = splitFields(stripBrackets(line));
            if (f.isEmpty())
                continue;
            if (section == InComponents) {
                if (!compDef(f.at(0)))
                    continue;                // element of a newer Qucs: skip it
                Component c(f.at(0), QPoint(0, 0));
                c.name = (f.size() > 1) ? f.at(1) : c.name;
                c.active = (f.size() > 2) ? (fieldInt(f, 2, 1) != 0) : true;
                c.pos = snapToGrid(QPoint(fieldInt(f, 3), fieldInt(f, 4)));
                c.rot = ((fieldInt(f, 5) % 4) + 4) % 4;
                c.mirror = (fieldInt(f, 6) != 0);
                for (int p = 7; p + 1 < f.size(); p += 2) {
                    const int pi = (p - 7) / 2;
                    c.setParam(pi, f.at(p));
                    if (pi < c.show.size())
                        c.show[pi] = (f.at(p + 1).toInt() != 0);
                }
                comps.append(c);
            } else {
                if (f.size() < 4)
                    continue;
                Wire w(snapToGrid(QPoint(fieldInt(f, 0), fieldInt(f, 1))),
                       snapToGrid(QPoint(fieldInt(f, 2), fieldInt(f, 3))));
                if (f.size() > 4)
                    w.label = f.at(4);
                if (w.p1 != w.p2)
                    wireList.append(w);
            }
        }
    }
    file.close();

    m_components = comps;
    m_wires = wireList;
    m_diagrams = diags;
    m_filePath = path;
    m_modified = false;
    emit structureChanged();
    return true;
}
