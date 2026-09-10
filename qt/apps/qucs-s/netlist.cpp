/*
 * Qucs-S, ported to EwokOS - netlist extraction.
 */

#include "netlist.h"

#include <QMap>
#include <QVector>

#include <math.h>

Element::Element()
    : kind(ElResistor), value(0.0), wave(WaveNone), ctrl(CtrlNone), ctrlIndex(-1),
      reportsCurrent(false), active(true)
{
    node[0] = node[1] = node[2] = node[3] = -1;
    for (int i = 0; i < 8; i++)
        p[i] = 0.0;
}

void Netlist::clear()
{
    nodeNames.clear();
    elems.clear();
    nodePoints.clear();
    error.clear();
    warnings.clear();
}

int Netlist::indexOfSource(const QString &name) const
{
    for (int i = 0; i < elems.size(); i++) {
        if (elems.at(i).name == name)
            return i;
    }
    return -1;
}

// ---- union-find over sheet points -------------------------------------------

static qint64 pointKey(const QPoint &p)
{
    return ((qint64)p.x() << 32) ^ (qint64)(p.y() & 0xffffffff);
}

class UnionFind {
public:
    int add() { m_parent.append(m_parent.size()); return m_parent.size() - 1; }
    int find(int x) {
        while (m_parent[x] != x) {
            m_parent[x] = m_parent[m_parent[x]];   // path halving
            x = m_parent[x];
        }
        return x;
    }
    void unite(int a, int b) {
        a = find(a);
        b = find(b);
        if (a != b)
            m_parent[b] = a;
    }
private:
    QList<int> m_parent;
};

// Sheet point -> node index, through the union-find group it ended up in.
static int nodeOfPoint(const QPoint &p, const QMap<qint64, int> &ids,
                       const QMap<int, int> &rootToNode, UnionFind &uf)
{
    if (!ids.contains(pointKey(p)))
        return -1;
    return rootToNode.value(uf.find(ids.value(pointKey(p))), -1);
}

bool buildNetlist(const Schematic &sch, Netlist *nl)
{
    nl->clear();

    const QList<Component> &comps = sch.components();
    const QList<Wire> &wires = sch.wires();

    // ---- register every point that can carry a net -------------------------
    UnionFind uf;
    QMap<qint64, int> ids;                     // sheet point -> union-find id
    QList<QPoint> points;

    for (int i = 0; i < comps.size(); i++) {
        const Component &c = comps.at(i);
        const CompDef *def = compDef(c.type);
        if (!def || (def->flags & CompIsSim) || !c.active)
            continue;
        const QList<QPoint> ports = compPorts(c);
        for (int k = 0; k < ports.size(); k++) {
            const qint64 key = pointKey(ports.at(k));
            if (!ids.contains(key)) {
                ids.insert(key, uf.add());
                points.append(ports.at(k));
            }
        }
    }
    for (int i = 0; i < wires.size(); i++) {
        const QPoint ends[2] = { wires.at(i).p1, wires.at(i).p2 };
        for (int k = 0; k < 2; k++) {
            const qint64 key = pointKey(ends[k]);
            if (!ids.contains(key)) {
                ids.insert(key, uf.add());
                points.append(ends[k]);
            }
        }
    }

    if (ids.isEmpty()) {
        nl->error = QString("The schematic is empty.");
        return false;
    }

    // ---- join them ---------------------------------------------------------
    // A wire joins its own ends, and any registered point lying on a wire
    // joins that wire - which is how a T junction connects without the user
    // having to split the wire first.
    for (int i = 0; i < wires.size(); i++) {
        const Wire &w = wires.at(i);
        uf.unite(ids.value(pointKey(w.p1)), ids.value(pointKey(w.p2)));
    }
    for (int i = 0; i < points.size(); i++) {
        const int pid = ids.value(pointKey(points.at(i)));
        for (int k = 0; k < wires.size(); k++) {
            if (wires.at(k).contains(points.at(i))) {
                uf.unite(pid, ids.value(pointKey(wires.at(k).p1)));
                break;
            }
        }
    }

    // ---- number the nodes ---------------------------------------------------
    // Ground first: every GND port's group becomes node 0, as SPICE wants.
    QMap<int, int> rootToNode;
    QList<int> groundRoots;
    for (int i = 0; i < comps.size(); i++) {
        const Component &c = comps.at(i);
        const CompDef *def = compDef(c.type);
        if (!def || !(def->flags & CompIsGround) || !c.active)
            continue;
        const QList<QPoint> ports = compPorts(c);
        for (int k = 0; k < ports.size(); k++) {
            const int r = uf.find(ids.value(pointKey(ports.at(k))));
            if (!groundRoots.contains(r))
                groundRoots.append(r);
        }
    }
    if (groundRoots.isEmpty()) {
        nl->error = QString("No ground component on the schematic.\n"
                            "Insert one from the Sources group of the component library.");
        return false;
    }
    // Several grounds that are not wired together are still one node 0, which
    // is what joining them means electrically.
    for (int i = 1; i < groundRoots.size(); i++)
        uf.unite(groundRoots.at(0), groundRoots.at(i));

    rootToNode.insert(uf.find(groundRoots.at(0)), kGndNode);
    nl->nodeNames.append(QString("gnd"));

    for (int i = 0; i < points.size(); i++) {
        const int r = uf.find(ids.value(pointKey(points.at(i))));
        if (!rootToNode.contains(r)) {
            rootToNode.insert(r, nl->nodeNames.size());
            nl->nodeNames.append(QString("_net") + QString::number(nl->nodeNames.size() - 1));
        }
    }
    for (int i = 0; i < nl->nodeCount(); i++)
        nl->nodePoints.append(QList<QPoint>());
    for (int i = 0; i < points.size(); i++) {
        const int r = uf.find(ids.value(pointKey(points.at(i))));
        nl->nodePoints[rootToNode.value(r)].append(points.at(i));
    }

    // A wire label renames the net it sits on, upstream's "wire label".
    for (int i = 0; i < wires.size(); i++) {
        const Wire &w = wires.at(i);
        if (w.label.trimmed().isEmpty())
            continue;
        const int r = uf.find(ids.value(pointKey(w.p1)));
        const int n = rootToNode.value(r, -1);
        if (n > kGndNode)
            nl->nodeNames[n] = w.label.trimmed();
    }

    // ---- elements -----------------------------------------------------------
    int internalNodes = 0;
    for (int i = 0; i < comps.size(); i++) {
        const Component &c = comps.at(i);
        const CompDef *def = compDef(c.type);
        if (!def || (def->flags & CompIsSim) || !c.active)
            continue;

        const QList<QPoint> ports = compPorts(c);
        int pn[4] = { -1, -1, -1, -1 };
        for (int k = 0; k < ports.size() && k < 4; k++)
            pn[k] = nodeOfPoint(ports.at(k), ids, rootToNode, uf);

        if (def->flags & CompIsGround)
            continue;                          // it only defined node 0

        const QString t = c.type;
        Element e;
        e.name = c.name;

        if (t == QLatin1String("R")) {
            e.kind = ElResistor;
            e.value = c.value(0);
            if (!(e.value > 0.0)) {
                nl->warnings.append(c.name + QString(": resistance must be positive, using 1 uOhm."));
                e.value = 1e-6;
            }
            e.node[0] = pn[0];
            e.node[1] = pn[1];
        } else if (t == QLatin1String("C")) {
            e.kind = ElCapacitor;
            e.value = c.value(0);
            if (!(e.value > 0.0)) {
                nl->warnings.append(c.name + QString(": capacitance must be positive, using 1 pF."));
                e.value = 1e-12;
            }
            e.p[0] = c.value(1);               // initial voltage
            e.node[0] = pn[0];
            e.node[1] = pn[1];
        } else if (t == QLatin1String("L")) {
            e.kind = ElInductor;
            e.value = c.value(0);
            if (!(e.value > 0.0)) {
                nl->warnings.append(c.name + QString(": inductance must be positive, using 1 uH."));
                e.value = 1e-6;
            }
            e.p[0] = c.value(1);               // initial current
            e.node[0] = pn[0];
            e.node[1] = pn[1];
            e.reportsCurrent = true;
        } else if (t == QLatin1String("Vdc")) {
            e.kind = ElVoltageSource;
            e.wave = WaveNone;
            e.value = c.value(0);
            e.node[0] = pn[0];
            e.node[1] = pn[1];
            e.reportsCurrent = true;
        } else if (t == QLatin1String("Idc")) {
            e.kind = ElCurrentSource;
            e.ctrl = CtrlNone;
            e.value = c.value(0);
            e.node[0] = pn[0];
            e.node[1] = pn[1];
        } else if (t == QLatin1String("Vac")) {
            e.kind = ElVoltageSource;
            e.wave = WaveSine;
            e.value = 0.0;                     // no DC component, as in SPICE's
                                               // "V1 n+ n- DC 0 AC 1 0 SIN(...)"
            e.p[0] = c.value(0);               // amplitude
            e.p[1] = c.value(1) * M_PI / 180.0;  // phase, radians
            e.p[2] = c.value(2);               // frequency, Hz
            e.p[3] = c.value(3);               // damping, 1/s
            e.p[4] = c.value(0);               // AC magnitude
            e.p[5] = c.value(1) * M_PI / 180.0;  // AC phase
            e.node[0] = pn[0];
            e.node[1] = pn[1];
            e.reportsCurrent = true;
        } else if (t == QLatin1String("Vpulse")) {
            e.kind = ElVoltageSource;
            e.wave = WavePulse;
            for (int k = 0; k < 7; k++)
                e.p[k] = c.value(k);
            e.value = e.p[0];                  // quiescent level
            e.node[0] = pn[0];
            e.node[1] = pn[1];
            e.reportsCurrent = true;
        } else if (t == QLatin1String("Iprobe")) {
            e.kind = ElVoltageSource;
            e.wave = WaveNone;
            e.value = 0.0;                     // an ammeter is a 0 V source
            e.node[0] = pn[0];
            e.node[1] = pn[1];
            e.reportsCurrent = true;
        } else if (t == QLatin1String("Diode")) {
            e.kind = ElDiode;
            e.value = c.value(0);              // Is
            e.p[0] = c.value(1);               // N
            e.p[1] = c.value(2);               // Rs
            e.p[2] = c.value(3);               // Cj0
            e.p[3] = c.value(4);               // Vj
            e.p[4] = c.value(5);               // M
            if (!(e.value > 0.0))
                e.value = 1e-15;
            if (!(e.p[0] > 0.0))
                e.p[0] = 1.0;
            e.node[0] = pn[0];                 // anode
            e.node[1] = pn[1];                 // cathode

            // A series resistance needs a node of its own, so it becomes a
            // resistor in front of an ideal junction rather than a model the
            // Newton loop would have to iterate on.
            const double rs = e.p[1];
            if (rs > 0.0 && e.node[0] != e.node[1]) {
                Element r;
                r.kind = ElResistor;
                r.name = c.name + QString("_Rs");
                r.value = rs;
                r.node[0] = e.node[0];
                r.node[1] = nl->nodeNames.size();
                nl->nodeNames.append(QString("_%1_rs%2").arg(c.name).arg(internalNodes++));
                nl->nodePoints.append(QList<QPoint>());
                e.node[0] = r.node[1];
                e.p[1] = 0.0;
                nl->elems.append(r);
            }
        } else if (t == QLatin1String("BJT")) {
            e.kind = ElBjt;
            e.p[0] = c.value(1);               // Is
            e.p[1] = c.value(2);               // Bf
            e.p[2] = c.value(3);               // Br
            e.p[3] = (c.param(0).trimmed().toLower() == QLatin1String("pnp")) ? 1.0 : 0.0;
            if (!(e.p[0] > 0.0))
                e.p[0] = 1e-16;
            if (!(e.p[1] > 0.0))
                e.p[1] = 1.0;
            if (!(e.p[2] > 0.0))
                e.p[2] = 1.0;
            e.node[0] = pn[0];                 // base
            e.node[1] = pn[1];                 // collector
            e.node[2] = pn[2];                 // emitter
        } else if (t == QLatin1String("VCVS") || t == QLatin1String("CCVS")) {
            e.kind = ElVoltageSource;
            e.ctrl = (t == QLatin1String("VCVS")) ? CtrlVoltage : CtrlCurrent;
            e.value = c.value(t == QLatin1String("VCVS") ? 0 : 1);
            e.ctrlName = (e.ctrl == CtrlCurrent) ? c.param(0) : QString();
            e.node[0] = pn[2];                 // output +
            e.node[1] = pn[3];                 // output -
            e.node[2] = pn[0];                 // control +
            e.node[3] = pn[1];                 // control -
            e.reportsCurrent = true;
        } else if (t == QLatin1String("VCCS") || t == QLatin1String("CCCS")) {
            e.kind = ElCurrentSource;
            e.ctrl = (t == QLatin1String("VCCS")) ? CtrlVoltage : CtrlCurrent;
            e.value = c.value(t == QLatin1String("VCCS") ? 0 : 1);
            e.ctrlName = (e.ctrl == CtrlCurrent) ? c.param(0) : QString();
            e.node[0] = pn[2];                 // current flows out of here
            e.node[1] = pn[3];
            e.node[2] = pn[0];
            e.node[3] = pn[1];
        } else {
            nl->warnings.append(c.type + QString(" (%1): not supported by this simulator, ignored.").arg(c.name));
            continue;
        }

        nl->elems.append(e);
    }

    if (nl->elems.isEmpty()) {
        nl->error = QString("The schematic holds no circuit elements.");
        return false;
    }

    // ---- controlled sources naming a controlling source ----------------------
    // Resolved after the whole element list exists, so a CCCS may name a
    // source placed later on the sheet.
    for (int i = 0; i < nl->elems.size(); i++) {
        Element &e = nl->elems[i];
        if (e.ctrl != CtrlCurrent)
            continue;
        const QString ctrlName = e.ctrlName.trimmed();
        if (ctrlName.isEmpty()) {
            nl->error = e.name + QString(": a current controlled source needs the name of\n"
                                         "the voltage source whose current controls it (property Source).");
            return false;
        }
        e.ctrlIndex = nl->indexOfSource(ctrlName);
        if (e.ctrlIndex < 0 || nl->elems.at(e.ctrlIndex).kind != ElVoltageSource) {
            nl->error = e.name + QString(": controlling voltage source \"%1\" not found.").arg(ctrlName);
            return false;
        }
        if (e.ctrlIndex == i) {
            nl->error = e.name + QString(": a source cannot control itself.");
            return false;
        }
    }

    // ---- connectivity warnings ----------------------------------------------
    // A node carrying a single terminal has nowhere for the current to go; it
    // is legal (an open circuit) but almost always a missing wire.  The
    // control input of a VCVS or VCCS is high impedance and does not count.
    QVector<int> terminals(nl->nodeCount());
    for (int i = 0; i < nl->elems.size(); i++) {
        const Element &e = nl->elems.at(i);
        const int last = (e.kind == ElBjt) ? 3 : 2;
        for (int k = 0; k < last; k++) {
            if (e.node[k] >= 0)
                terminals[e.node[k]]++;
        }
    }
    for (int n = kGndNode + 1; n < nl->nodeCount(); n++) {
        if (terminals.at(n) == 1)
            nl->warnings.append(nl->nodeNames.at(n) + QString(": connected to a single terminal only."));
    }

    return true;
}
