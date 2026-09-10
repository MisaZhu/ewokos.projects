/*
 * Qucs-S, ported to EwokOS - the simulation kernel.
 *
 * See simulator.h for the sign conventions.  The shape is the one every SPICE
 * has: stamp the elements into a modified nodal analysis matrix, solve it, and
 * for the junctions linearize about the last iterate and do it again until the
 * node voltages stop moving.  A transient run adds the trapezoidal companion
 * models that turn C and L into a conductance plus a history current source,
 * which is what lets the same matrix serve all three analyses.
 */

#include "simulator.h"

#include "misc.h"

// ---- numerical constants ----------------------------------------------------
//
// SPICE's defaults, except relTol, which ngspice sets to 1e-3 and which is
// loose enough here to hide small-signal detail on a diagram.

static const double kVt = 0.025852;        // thermal voltage at 300 K
static const double kGmin = 1e-12;         // node-to-ground conductance, and
                                           // the floor under a junction's
static const int kMaxIter = 100;           // Newton iterations per solve
static const int kDampFrom = 25;           // ... and where damping kicks in
static const double kRelTol = 1e-4;
static const double kAbsV = 1e-6;          // volts
static const double kAbsI = 1e-9;          // amperes
static const int kMaxPoints = 20001;       // sweep length guard
static const double kExpLimit = 80.0;      // exp() overflows past that

static inline bool convV(double a, double b)
{
    return fabs(a - b) <= kRelTol * fabs(a) + kAbsV;
}

static inline bool convI(double a, double b)
{
    return fabs(a - b) <= kRelTol * fabs(a) + kAbsI;
}

// ---- complex ----------------------------------------------------------------

Cx Cx::operator/(const Cx &o) const
{
    const double d = o.re * o.re + o.im * o.im;
    if (!(d > 0.0))
        return Cx(0.0, 0.0);
    return Cx((re * o.re + im * o.im) / d, (im * o.re - re * o.im) / d);
}

double Cx::mag() const
{
    return sqrt(re * re + im * im);
}

double Cx::phaseDeg() const
{
    return atan2(im, re) * 180.0 / M_PI;
}

static inline double realPart(double v) { return v; }
static inline double realPart(const Cx &v) { return v.re; }

// The imaginary part of a real sweep is nothing, so the same code can collect
// a DC and an AC point.
static inline double imagPart(double) { return 0.0; }
static inline double imagPart(const Cx &v) { return v.im; }

// j*v.  A real system has no imaginary axis, so the double form returns 0 and
// the same code can serve a DC and an AC sweep; the two cannot be plain
// overloads, since they would differ only in the return type.
template <class T> static inline T imagOf(double v);
template <> inline double imagOf<double>(double) { return 0.0; }
template <> inline Cx imagOf<Cx>(double v) { return Cx(0.0, v); }

// ---- results ----------------------------------------------------------------

double SimVar::valueAt(int i, int plot) const
{
    if (i < 0 || i >= re.size())
        return 0.0;
    switch (plot) {
    case PlotMagnitude:
        return complex ? sqrt(re.at(i) * re.at(i) + im.at(i) * im.at(i)) : fabs(re.at(i));
    case PlotDecibel: {
        double m = complex ? sqrt(re.at(i) * re.at(i) + im.at(i) * im.at(i)) : fabs(re.at(i));
        if (m < 1e-30)
            m = 1e-30;                       // log(0) would put -inf on the axis
        return 20.0 * log10(m);
    }
    case PlotPhase:
        return complex ? atan2(im.at(i), re.at(i)) * 180.0 / M_PI : 0.0;
    case PlotImag:
        return complex ? im.at(i) : 0.0;
    default:
        return re.at(i);
    }
}

int SimResult::varIndex(const QString &name) const
{
    for (int i = 0; i < vars.size(); i++) {
        if (vars.at(i).name == name)
            return i;
    }
    return -1;
}

QStringList SimResult::varNames() const
{
    QStringList out;
    for (int i = 0; i < vars.size(); i++)
        out.append(vars.at(i).name);
    return out;
}

// ---- the matrix -------------------------------------------------------------

// A dense square system with one augmented column, which is all the element
// set of netlist.cpp needs: circuits here run to a few dozen unknowns, and a
// dense solve is both shorter and faster than a sparse one at that size.
template <class T>
class Matrix {
public:
    Matrix() : m_n(0) {}

    void setSize(int n)
    {
        m_n = n;
        m_d.fill(T(), n * (n + 1));          // resize alone leaves doubles unset
    }
    int size() const { return m_n; }

    // A negative index is a no-op, which is how ground (node 0, index -1)
    // drops out of every stamp without the callers having to test for it.
    void add(int r, int c, const T &v)
    {
        if (r >= 0 && c >= 0 && r < m_n && c < m_n)
            m_d[r * (m_n + 1) + c] += v;
    }
    void addRhs(int r, const T &v)
    {
        if (r >= 0 && r < m_n)
            m_d[r * (m_n + 1) + m_n] += v;
    }

    // Gauss elimination with partial pivoting.  The matrix survives, so a
    // Newton loop may re-stamp and solve again; *badRow, when given, receives
    // the row that could not be pivoted, which names the floating node.
    bool solve(QVector<T> *x, int *badRow)
    {
        const int n = m_n;
        const int w = n + 1;
        QVector<T> a(m_d);
        x->resize(n);

        for (int col = 0; col < n; col++) {
            int best = col;
            double bestMag = magnitude(a[col * w + col]);
            for (int r = col + 1; r < n; r++) {
                const double mg = magnitude(a[r * w + col]);
                if (mg > bestMag) {
                    bestMag = mg;
                    best = r;
                }
            }
            if (!(bestMag > 1e-300)) {
                if (badRow)
                    *badRow = col;
                return false;
            }
            if (best != col) {
                for (int k = 0; k < w; k++) {
                    const T t = a[col * w + k];
                    a[col * w + k] = a[best * w + k];
                    a[best * w + k] = t;
                }
            }
            const T d = a[col * w + col];
            for (int r = col + 1; r < n; r++) {
                const T f = a[r * w + col] / d;
                for (int k = col; k < w; k++)
                    a[r * w + k] -= f * a[col * w + k];
            }
        }

        for (int r = n - 1; r >= 0; r--) {
            T s = a[r * w + n];
            for (int k = r + 1; k < n; k++)
                s -= a[r * w + k] * (*x)[k];
            (*x)[r] = s / a[r * w + r];
            if (!(magnitude((*x)[r]) < 1e250)) {   // catches inf and nan alike
                if (badRow)
                    *badRow = r;
                return false;
            }
        }
        return true;
    }

private:
    int m_n;
    QVector<T> m_d;
};

static QString singularError(const Netlist &nl, int row)
{
    if (row >= 0 && row < nl.nodeCount() - 1) {
        return QString("the circuit equations are singular at node %1: it has no\n"
                       "resistive path to ground, which a floating net or two voltage\n"
                       "sources in parallel both cause.")
                       .arg(nl.nodeNames.at(row + 1));
    }
    return QString("the circuit equations are singular (row %1).").arg(row);
}

// ---- the unknowns -----------------------------------------------------------

// Node voltages 1..n-1 in order, then one branch current per element that this
// particular analysis models as a voltage source.  Every caller prepares the
// layout for the analysis it is about to run, because the layout differs: an
// inductor is a short in a DC solve and an admittance in a transient step.
struct Solver {
    const Netlist *nl;

    explicit Solver(const Netlist &n) : nl(&n), m_size(0) {}

    // lAsVsrc: inductors are shorts, as a DC-type solve wants, rather than the
    // admittance a transient step needs.  icInit: SPICE's UIC startup, where a
    // capacitor carrying an initial voltage becomes a voltage source of that
    // value and an inductor carrying an initial current becomes a current
    // source of that value.
    void prepare(bool lAsVsrc, bool icInit)
    {
        m_vcol.resize(nl->elems.size());
        int col = nl->nodeCount() - 1;
        for (int i = 0; i < nl->elems.size(); i++) {
            const Element &e = nl->elems.at(i);
            bool isV = (e.kind == ElVoltageSource);
            if (e.kind == ElCapacitor)
                isV = icInit && e.p[0] != 0.0;
            else if (e.kind == ElInductor)
                isV = (icInit && e.p[0] != 0.0) ? false : lAsVsrc;
            m_vcol[i] = isV ? col++ : -1;
        }
        m_size = col;
    }

    int size() const { return m_size; }
    int vcol(int elem) const
    {
        return (elem >= 0 && elem < m_vcol.size()) ? m_vcol.at(elem) : -1;
    }

private:
    int m_size;
    QVector<int> m_vcol;
};

// The companion history of a transient run: the voltage across and the current
// through every capacitor and inductor at the end of the last accepted step,
// and the same for a diode's depletion capacitance.  Indexed by element and by
// Nonlin respectively.
struct TranState {
    QVector<double> vC, iC;
    QVector<double> vL, iL;
    QVector<double> djV, djI;

    void prepare(const Netlist &nl, int nonlinCount)
    {
        vC.resize(nl.elems.size());
        iC.resize(nl.elems.size());
        vL.resize(nl.elems.size());
        iL.resize(nl.elems.size());
        vC.fill(0.0);
        iC.fill(0.0);
        vL.fill(0.0);
        iL.fill(0.0);
        djV.resize(nonlinCount);
        djI.resize(nonlinCount);
        djV.fill(0.0);
        djI.fill(0.0);
    }
};

// ---- stamps -----------------------------------------------------------------

// Node index to matrix index.  Ground is node 0 and has no unknown.
static inline int nidx(int n) { return n > 0 ? n - 1 : -1; }

template <class T>
static inline const T &nodeV(const QVector<T> &nv, int n)
{
    static const T zero = T();
    return (n >= 0 && n < nv.size()) ? nv.at(n) : zero;
}

// Conductance between two nodes.
template <class T>
static void stampG(Matrix<T> &m, int na, int nb, const T &g)
{
    const int a = nidx(na), b = nidx(nb);
    m.add(a, a, g);
    m.add(b, b, g);
    m.add(a, b, -g);
    m.add(b, a, -g);
}

// A current of `i` flowing from na to nb.
template <class T>
static void stampI(Matrix<T> &m, int na, int nb, const T &i)
{
    m.addRhs(nidx(na), -i);
    m.addRhs(nidx(nb), i);
}

// A voltage source of value `v` between na and nb, whose branch current is the
// unknown at column `c` and flows na -> nb.
template <class T>
static void stampVsrc(Matrix<T> &m, int na, int nb, int c, const T &v)
{
    m.add(nidx(na), c, T(1.0));
    m.add(nidx(nb), c, T(-1.0));
    m.add(c, nidx(na), T(1.0));
    m.add(c, nidx(nb), T(-1.0));
    m.addRhs(c, v);
}

// A current `i0 + g*(v(na)-v(nb) - v0)` leaving node nt: the conductance goes
// into the row, the constant part into the right hand side.  Every nonlinear
// stamp below is this, called once per terminal.
template <class T>
static void stampNonlin(Matrix<T> &m, int nt, int na, int nb,
                        const T &i0, const T &g, const T &v0)
{
    m.addRhs(nidx(nt), -(i0 - g * v0));
    m.add(nidx(nt), nidx(na), g);
    m.add(nidx(nt), nidx(nb), -g);
}

// A current controlled source's controlling branch is a voltage source, so it
// always has a column - in every analysis, which is what makes this safe.
template <class T>
static void stampCurrentSource(Matrix<T> &m, const Element &e, const Solver &sv, const T &dc)
{
    if (e.ctrl == CtrlNone) {
        stampI(m, e.node[0], e.node[1], dc);
    } else if (e.ctrl == CtrlVoltage) {
        const T g(e.value);
        m.add(nidx(e.node[0]), nidx(e.node[2]), g);
        m.add(nidx(e.node[0]), nidx(e.node[3]), -g);
        m.add(nidx(e.node[1]), nidx(e.node[2]), -g);
        m.add(nidx(e.node[1]), nidx(e.node[3]), g);
    } else {
        const T g(e.value);
        m.add(nidx(e.node[0]), sv.vcol(e.ctrlIndex), g);
        m.add(nidx(e.node[1]), sv.vcol(e.ctrlIndex), -g);
    }
}

template <class T>
static void stampVoltageSource(Matrix<T> &m, const Element &e, int i, const Solver &sv, const T &dc)
{
    const int c = sv.vcol(i);
    if (e.ctrl == CtrlNone) {
        stampVsrc(m, e.node[0], e.node[1], c, dc);
    } else if (e.ctrl == CtrlVoltage) {
        // v(n+) - v(n-) - gain * (v(ctrl+) - v(ctrl-)) = 0
        const T k(e.value);
        stampVsrc(m, e.node[0], e.node[1], c, T());
        m.add(c, nidx(e.node[2]), -k);
        m.add(c, nidx(e.node[3]), k);
    } else {
        // v(n+) - v(n-) - gain * i(ctrl) = 0
        const T r(e.value);
        stampVsrc(m, e.node[0], e.node[1], c, T());
        m.add(c, sv.vcol(e.ctrlIndex), -r);
    }
}

// SPICE's gmin from every node to ground.  It keeps a half-drawn schematic
// solvable instead of singular, and it costs 1e-12 S of accuracy.
template <class T>
static void stampGmin(Matrix<T> &m, int nodeCount)
{
    for (int n = 1; n < nodeCount; n++)
        m.add(n - 1, n - 1, T(kGmin));
}

template <class T>
static QVector<T> nodeVoltages(int nodeCount, const QVector<T> &x)
{
    QVector<T> nv(nodeCount);
    for (int n = 0; n < nodeCount; n++)
        nv[n] = (n > 0) ? x.at(n - 1) : T();
    return nv;
}

// ---- nonlinear devices ------------------------------------------------------

// One junction, linearized about vd.
struct Junction {
    double is, n;                            // saturation current, emission coeff.
    double vd;                               // the linearization point
    double i0, g;                            // current and slope there
};

struct Nonlin {
    int elem;                                // index into Netlist::elems
    int kind;                                // ElDiode or ElBjt
    int n0, n1, n2;                          // diode: anode, cathode
                                             // bjt: base, collector, emitter
    int jn[2][2];                            // junction polarity: v = jn[k][0] - jn[k][1]
    Junction j[2];
    int nj;
    double bf, br;                           // bjt betas
    bool pnp;
    double cj;                               // diode depletion capacitance at vd
    double cj0, vj, mexp;
};

static double junctionCap(const Nonlin &n, double vd)
{
    if (!(n.cj0 > 0.0) || !(n.vj > 0.0))
        return 0.0;
    const double fc = 0.5;                   // SPICE's Fc
    if (vd < fc * n.vj)
        return n.cj0 * pow(1.0 - vd / n.vj, -n.mexp);
    // Past Fc*Vj the depletion formula has a pole, so SPICE continues it with
    // the tangent instead - which also keeps a forward-biased diode from
    // producing an infinite capacitance.
    const double f = pow(1.0 - fc, -1.0 - n.mexp);
    return n.cj0 * f * (1.0 - fc * (1.0 + n.mexp) + n.mexp * vd / n.vj);
}

static void linearize(Nonlin *n)
{
    for (int k = 0; k < n->nj; k++) {
        Junction *j = &n->j[k];
        double arg = j->vd / (j->n * kVt);
        if (arg > kExpLimit)
            arg = kExpLimit;                 // exp() would overflow past that
        const double ex = exp(arg);
        j->i0 = j->is * (ex - 1.0);
        j->g = j->is * ex / (j->n * kVt);
        if (!(j->g > kGmin))
            j->g = kGmin;
    }
    n->cj = (n->kind == ElDiode) ? junctionCap(*n, n->j[0].vd) : 0.0;
}

static void collectNonlinear(const Netlist &nl, QList<Nonlin> *out)
{
    out->clear();
    for (int i = 0; i < nl.elems.size(); i++) {
        const Element &e = nl.elems.at(i);
        if (e.kind != ElDiode && e.kind != ElBjt)
            continue;

        Nonlin n;
        n.elem = i;
        n.kind = e.kind;
        n.n0 = e.node[0];
        n.n1 = e.node[1];
        n.n2 = e.node[2];
        n.nj = (e.kind == ElDiode) ? 1 : 2;
        n.bf = n.br = 1.0;
        n.pnp = false;
        n.cj = n.cj0 = n.vj = n.mexp = 0.0;
        n.jn[0][0] = n.jn[0][1] = n.jn[1][0] = n.jn[1][1] = 0;
        for (int k = 0; k < 2; k++) {
            n.j[k].is = 1e-15;
            n.j[k].n = 1.0;
            n.j[k].vd = 0.0;
            n.j[k].i0 = 0.0;
            n.j[k].g = kGmin;
        }

        if (e.kind == ElDiode) {
            n.j[0].is = e.value;
            n.j[0].n = e.p[0];
            n.jn[0][0] = e.node[0];          // anode
            n.jn[0][1] = e.node[1];          // cathode
            n.cj0 = e.p[2];
            n.vj = e.p[3];
            n.mexp = e.p[4];
        } else {
            n.pnp = (e.p[3] != 0.0);
            n.j[0].is = n.j[1].is = e.p[0];
            n.bf = e.p[1];
            n.br = e.p[2];
            // A pnp is an npn with every voltage and every current negated, so
            // the junctions are simply measured the other way round.
            if (n.pnp) {
                n.jn[0][0] = e.node[2];      // v(e) - v(b)
                n.jn[0][1] = e.node[0];
                n.jn[1][0] = e.node[1];      // v(c) - v(b)
                n.jn[1][1] = e.node[0];
            } else {
                n.jn[0][0] = e.node[0];      // v(b) - v(e)
                n.jn[0][1] = e.node[2];
                n.jn[1][0] = e.node[0];      // v(b) - v(c)
                n.jn[1][1] = e.node[1];
            }
        }
        linearize(&n);
        out->append(n);
    }
}

// Element index -> Nonlin index, or -1.
static QVector<int> nonlinMap(const Netlist &nl, const QList<Nonlin> &nls)
{
    QVector<int> map(nl.elems.size());
    map.fill(-1);
    for (int k = 0; k < nls.size(); k++)
        map[nls.at(k).elem] = k;
    return map;
}

// Re-linearizes every junction at the node voltages of a previous solution,
// which is how a sweep continues from the point before it.
static void setNonlinFromNodeV(QList<Nonlin> *nls, const QVector<double> &nv)
{
    for (int k = 0; k < nls->size(); k++) {
        Nonlin &n = (*nls)[k];
        for (int j = 0; j < n.nj; j++)
            n.j[j].vd = nodeV(nv, n.jn[j][0]) - nodeV(nv, n.jn[j][1]);
        linearize(&n);
    }
}

// Stamps the junctions of one device.  `smallSignal` drops the constant
// current and keeps only the frozen slope, which is what an AC sweep wants:
// it solves for the response on top of an operating point that already
// balances the DC terms.
template <class T>
static void stampNonlinear(Matrix<T> &m, const Nonlin &n, bool smallSignal)
{
    const double sc = smallSignal ? 0.0 : 1.0;
    const T v0[2] = { T(smallSignal ? 0.0 : n.j[0].vd), T(smallSignal ? 0.0 : n.j[1].vd) };

    if (n.kind == ElDiode) {
        const T i0(sc * n.j[0].i0), g(n.j[0].g);
        stampNonlin(m, n.n0, n.jn[0][0], n.jn[0][1], i0, g, v0[0]);
        stampNonlin(m, n.n1, n.jn[0][0], n.jn[0][1], -i0, -g, v0[0]);
        return;
    }

    // Ebers-Moll, transport form.  With iF the base-emitter and iR the
    // base-collector diode current, the currents into the terminals are
    //   iB = iF/Bf + iR/Br
    //   iC = iF - iR*(1 + 1/Br)
    //   iE = iR - iF*(1 + 1/Bf)
    // which sum to zero, as they must.  A pnp negates all three.
    const double bf = n.bf, br = n.br, sg = n.pnp ? -1.0 : 1.0;
    const T iF(sc * n.j[0].i0), gF(n.j[0].g);
    const T iR(sc * n.j[1].i0), gR(n.j[1].g);

    stampNonlin(m, n.n0, n.jn[0][0], n.jn[0][1], iF * T(sg / bf), gF * T(sg / bf), v0[0]);
    stampNonlin(m, n.n0, n.jn[1][0], n.jn[1][1], iR * T(sg / br), gR * T(sg / br), v0[1]);

    stampNonlin(m, n.n1, n.jn[0][0], n.jn[0][1], iF * T(sg), gF * T(sg), v0[0]);
    stampNonlin(m, n.n1, n.jn[1][0], n.jn[1][1],
                -iR * T(sg * (1.0 + 1.0 / br)), -gR * T(sg * (1.0 + 1.0 / br)), v0[1]);

    stampNonlin(m, n.n2, n.jn[0][0], n.jn[0][1],
                -iF * T(sg * (1.0 + 1.0 / bf)), -gF * T(sg * (1.0 + 1.0 / bf)), v0[0]);
    stampNonlin(m, n.n2, n.jn[1][0], n.jn[1][1], iR * T(sg), gR * T(sg), v0[1]);
}

// SPICE's junction voltage limiter.  An iterate that jumps far past the knee
// of the exponential linearizes into nonsense, so the step is capped; the
// cap relaxes as the solution approaches, which is what makes Newton converge
// from a cold start of zero volts everywhere.
static double pnjlim(double vNew, double vOld, double is, double n, bool *limited)
{
    const double vt = n * kVt;
    const double vcrit = vt * log(vt / (1.4142135623730951 * is));   // sqrt(2)
    if (vNew > vcrit && fabs(vNew - vOld) > 2.0 * vt) {
        vNew = (vOld > 0.0) ? (vOld + 2.0 * vt) : vcrit;
        *limited = true;
    } else if (vOld - vNew > 10.0 * vt) {
        // A long reverse step is harmless for the exponential but swings a
        // feedback loop wildly, so it is capped too.
        vNew = vOld - 10.0 * vt;
        *limited = true;
    }
    return vNew;
}

// ---- waveforms --------------------------------------------------------------

double sourceValue(const Element &e, double t)
{
    switch (e.wave) {
    case WaveSine:
        // p[0] amplitude, p[1] phase in radians, p[2] frequency, p[3] damping
    {
        double v = e.p[0] * sin(2.0 * M_PI * e.p[2] * t + e.p[1]);
        if (e.p[3] != 0.0)
            v *= exp(-fabs(e.p[3]) * t);
        return v;
    }
    case WavePulse:
        // p[0] U1 low, p[1] U2 high, p[2] T1 delay, p[3] T2 rise,
        // p[4] T3 width, p[5] T4 fall, p[6] T5 period - SPICE's PULSE, whose
        // delay happens once and whose period then repeats the whole shape.
    {
        const double u1 = e.p[0], u2 = e.p[1];
        const double delay = (e.p[2] > 0.0) ? e.p[2] : 0.0;
        if (t < delay)
            return u1;
        double tt = t - delay;
        if (e.p[6] > 0.0)
            tt = fmod(tt, e.p[6]);
        const double rise = (e.p[3] > 0.0) ? e.p[3] : 0.0;
        const double width = (e.p[4] > 0.0) ? e.p[4] : 0.0;
        const double fall = (e.p[5] > 0.0) ? e.p[5] : 0.0;
        if (tt < rise)
            return u1 + (u2 - u1) * tt / rise;
        tt -= rise;
        if (tt < width)
            return u2;
        tt -= width;
        if (tt < fall)
            return u2 + (u1 - u2) * tt / fall;
        return u1;
    }
    default:
        return e.value;
    }
}

// ---- element currents -------------------------------------------------------

// The current of every element in the reported sign: from node[0] to node[1],
// and for a bipolar transistor the collector current, positive into the
// collector.  `w` is the angular frequency of an AC sweep and 0 otherwise;
// `st` is a transient run's companion history, whose capacitors and inductors
// are admittances rather than the sources a DC-type solve turns them into.
template <class T>
static void elementCurrents(const Netlist &nl, const Solver &sv, const QVector<T> &nv,
                            const QVector<T> &x, const QList<Nonlin> &nls,
                            double w, const TranState *st, QVector<T> *out)
{
    const QVector<int> map = nonlinMap(nl, nls);
    out->resize(nl.elems.size());
    for (int i = 0; i < nl.elems.size(); i++)
        (*out)[i] = T();

    for (int i = 0; i < nl.elems.size(); i++) {
        const Element &e = nl.elems.at(i);
        const T dv = nodeV(nv, e.node[0]) - nodeV(nv, e.node[1]);
        const int nk = map.at(i);
        switch (e.kind) {
        case ElResistor:
            (*out)[i] = dv * T(1.0 / e.value);
            break;
        case ElCapacitor:
            if (st)
                (*out)[i] = T(st->iC.at(i));
            else if (w > 0.0)
                (*out)[i] = dv * imagOf<T>(w * e.value);
            else if (sv.vcol(i) >= 0)
                (*out)[i] = x.at(sv.vcol(i));
            break;
        case ElInductor:
            if (st)
                (*out)[i] = T(st->iL.at(i));
            else if (w > 0.0)
                (*out)[i] = dv / imagOf<T>(w * e.value);
            else if (sv.vcol(i) >= 0)
                (*out)[i] = x.at(sv.vcol(i));
            break;
        case ElVoltageSource:
            if (sv.vcol(i) >= 0)
                (*out)[i] = x.at(sv.vcol(i));
            break;
        case ElCurrentSource:
            if (e.ctrl == CtrlNone) {
                // An independent current source has no AC magnitude in this
                // catalogue, so it contributes nothing to a small-signal sweep.
                (*out)[i] = (w > 0.0) ? T() : T(e.value);
            } else if (e.ctrl == CtrlVoltage) {
                (*out)[i] = (nodeV(nv, e.node[2]) - nodeV(nv, e.node[3])) * T(e.value);
            } else if (sv.vcol(e.ctrlIndex) >= 0) {
                (*out)[i] = x.at(sv.vcol(e.ctrlIndex)) * T(e.value);
            }
            break;
        case ElDiode:
            if (nk >= 0) {
                const Nonlin &n = nls.at(nk);
                if (w > 0.0)
                    (*out)[i] = (nodeV(nv, n.jn[0][0]) - nodeV(nv, n.jn[0][1])) * T(n.j[0].g);
                else
                    (*out)[i] = T(n.j[0].i0);
            }
            break;
        case ElBjt:
            if (nk >= 0) {
                const Nonlin &n = nls.at(nk);
                const T vbe = nodeV(nv, n.jn[0][0]) - nodeV(nv, n.jn[0][1]);
                const T vbc = nodeV(nv, n.jn[1][0]) - nodeV(nv, n.jn[1][1]);
                const double sg = n.pnp ? -1.0 : 1.0;
                const double rr = sg * (1.0 + 1.0 / n.br);
                if (w > 0.0)
                    (*out)[i] = vbe * T(sg * n.j[0].g) - vbc * T(rr * n.j[1].g);
                else
                    (*out)[i] = T(sg * n.j[0].i0) - T(rr * n.j[1].i0);
            }
            break;
        }
    }
}

// ---- the systems ------------------------------------------------------------

// The DC-type system: resistors, sources, controlled sources, inductors as
// shorts and capacitors open, with the junctions at their current
// linearization.  `dcOnly` uses a source's DC value rather than its waveform
// at time t, which is what an operating point, a DC sweep and the
// linearization an AC sweep sits on all want.
static void stampDc(const Netlist &nl, const Solver &sv, const QList<Nonlin> &nls,
                    double t, bool dcOnly, Matrix<double> *m)
{
    m->setSize(sv.size());
    for (int i = 0; i < nl.elems.size(); i++) {
        const Element &e = nl.elems.at(i);
        const double v = dcOnly ? e.value : sourceValue(e, t);
        switch (e.kind) {
        case ElResistor:
            stampG(*m, e.node[0], e.node[1], 1.0 / e.value);
            break;
        case ElCapacitor:
            if (sv.vcol(i) >= 0)
                stampVsrc(*m, e.node[0], e.node[1], sv.vcol(i), e.p[0]);
            break;                                 // otherwise an open circuit
        case ElInductor:
            if (sv.vcol(i) >= 0)
                stampVsrc(*m, e.node[0], e.node[1], sv.vcol(i), 0.0);
            else
                stampI(*m, e.node[0], e.node[1], e.p[0]);   // UIC current source
            break;
        case ElVoltageSource:
            stampVoltageSource(*m, e, i, sv, v);
            break;
        case ElCurrentSource:
            stampCurrentSource(*m, e, sv, v);
            break;
        default:
            break;                                 // nonlinear, stamped below
        }
    }
    for (int k = 0; k < nls.size(); k++)
        stampNonlinear(*m, nls.at(k), false);
    stampGmin(*m, nl.nodeCount());
}

// Newton over the DC-type system.  `seed`, when given, is the node voltages of
// a previous solution to start from and nls must already be linearized at
// them; a DC sweep continues from the point before it that way.
static bool dcSolve(const Netlist &nl, const Solver &sv, QList<Nonlin> *nls,
                    double t, bool dcOnly, QVector<double> *x, QString *error,
                    const QVector<double> *seed = 0)
{
    Matrix<double> m;

    if (nls->isEmpty()) {                          // a linear circuit solves once
        stampDc(nl, sv, *nls, t, dcOnly, &m);
        int badRow = -1;
        if (!m.solve(x, &badRow)) {
            *error = singularError(nl, badRow);
            return false;
        }
        return true;
    }

    QVector<double> prev = seed ? *seed : QVector<double>();
    if (prev.size() != nl.nodeCount()) {
        prev.resize(nl.nodeCount());
        prev.fill(0.0);
    }
    QVector<double> sol, prevSol;

    for (int iter = 0; iter < kMaxIter; iter++) {
        stampDc(nl, sv, *nls, t, dcOnly, &m);
        int badRow = -1;
        if (!m.solve(&sol, &badRow)) {
            *error = singularError(nl, badRow);
            return false;
        }
        const QVector<double> nv = nodeVoltages(nl.nodeCount(), sol);

        bool limited = false;
        const bool damp = (iter >= kDampFrom);
        for (int k = 0; k < nls->size(); k++) {
            Nonlin &n = (*nls)[k];
            for (int j = 0; j < n.nj; j++) {
                const double vOld = n.j[j].vd;
                double v = pnjlim(nodeV(nv, n.jn[j][0]) - nodeV(nv, n.jn[j][1]),
                                  vOld, n.j[j].is, n.j[j].n, &limited);
                if (damp && fabs(v - vOld) > kAbsV) {
                    v = vOld + 0.5 * (v - vOld);   // half a step still converges,
                    limited = true;                // and stops a diode feedback
                }                                  // loop from oscillating
                n.j[j].vd = v;
            }
            linearize(&n);
        }

        bool conv = !limited && !prevSol.isEmpty();
        for (int i = 0; conv && i < sol.size(); i++) {
            conv = (i < nl.nodeCount() - 1) ? convV(nv.at(i + 1), prev.at(i + 1))
                                            : convI(sol.at(i), prevSol.at(i));
        }
        prev = nv;
        prevSol = sol;
        if (conv) {
            *x = sol;
            return true;
        }
    }

    *error = QString("the junctions did not settle in %1 iterations, which a diode\n"
                     "or transistor wired straight across a source, with no resistance\n"
                     "in between, does.").arg(kMaxIter);
    return false;
}

// The AC system: the same topology, but complex, with C and L as admittances
// and every junction frozen at the operating point's slope.  Only a source
// carrying an AC magnitude excites it - the DC values were consumed by the
// operating point this sweep is linearized about.
static void stampAc(const Netlist &nl, const Solver &sv, const QList<Nonlin> &nls,
                    double w, Matrix<Cx> *m)
{
    m->setSize(sv.size());
    for (int i = 0; i < nl.elems.size(); i++) {
        const Element &e = nl.elems.at(i);
        switch (e.kind) {
        case ElResistor:
            stampG(*m, e.node[0], e.node[1], Cx(1.0 / e.value));
            break;
        case ElCapacitor:
            stampG(*m, e.node[0], e.node[1], imagOf<Cx>(w * e.value));
            break;
        case ElInductor:
            stampG(*m, e.node[0], e.node[1], imagOf<Cx>(-1.0 / (w * e.value)));
            break;
        case ElVoltageSource:
            // p[4] and p[5] are the AC magnitude and phase, which only the "AC
            // voltage" component fills in.
            stampVoltageSource(*m, e, i, sv, Cx(e.p[4] * cos(e.p[5]), e.p[4] * sin(e.p[5])));
            break;
        case ElCurrentSource:
            stampCurrentSource(*m, e, sv, Cx());
            break;
        default:
            break;
        }
    }
    for (int k = 0; k < nls.size(); k++)
        stampNonlinear(*m, nls.at(k), true);
    stampGmin(*m, nl.nodeCount());
}

// One transient step's system.  The trapezoidal companion models turn C and L
// into a conductance with a history current source beside it, so within a step
// the matrix stays linear and only the junctions need Newton.
static void stampTran(const Netlist &nl, const Solver &sv, const QList<Nonlin> &nls,
                      const TranState &st, double t, double h, Matrix<double> *m)
{
    m->setSize(sv.size());
    for (int i = 0; i < nl.elems.size(); i++) {
        const Element &e = nl.elems.at(i);
        switch (e.kind) {
        case ElResistor:
            stampG(*m, e.node[0], e.node[1], 1.0 / e.value);
            break;
        case ElCapacitor: {
            // i(t) = 2C/h * (v(t) - v(t-h)) - i(t-h)
            const double geq = 2.0 * e.value / h;
            stampG(*m, e.node[0], e.node[1], geq);
            stampI(*m, e.node[0], e.node[1], -(geq * st.vC.at(i) + st.iC.at(i)));
            break;
        }
        case ElInductor: {
            // i(t) = i(t-h) + h/2L * (v(t) + v(t-h))
            const double geq = h / (2.0 * e.value);
            stampG(*m, e.node[0], e.node[1], geq);
            stampI(*m, e.node[0], e.node[1], st.iL.at(i) + geq * st.vL.at(i));
            break;
        }
        case ElVoltageSource:
            stampVoltageSource(*m, e, i, sv, sourceValue(e, t));
            break;
        case ElCurrentSource:
            stampCurrentSource(*m, e, sv, sourceValue(e, t));
            break;
        default:
            break;
        }
    }
    for (int k = 0; k < nls.size(); k++) {
        const Nonlin &n = nls.at(k);
        stampNonlinear(*m, n, false);
        if (n.cj > 0.0) {
            // The depletion capacitance, frozen at the last iterate's junction
            // voltage and integrated like any other capacitor.
            const double geq = 2.0 * n.cj / h;
            stampG(*m, n.jn[0][0], n.jn[0][1], geq);
            stampI(*m, n.jn[0][0], n.jn[0][1], -(geq * st.djV.at(k) + st.djI.at(k)));
        }
    }
    stampGmin(*m, nl.nodeCount());
}

// ---- results ----------------------------------------------------------------

static void resultVars(const Netlist &nl, SimResult *res, bool complex, int points)
{
    res->vars.clear();
    for (int n = 1; n < nl.nodeCount(); n++) {
        SimVar v;
        v.name = nl.nodeNames.at(n);
        v.complex = complex;
        v.re.resize(points);
        if (complex)
            v.im.resize(points);
        res->vars.append(v);
    }
    for (int i = 0; i < nl.elems.size(); i++) {
        if (!nl.elems.at(i).reportsCurrent)
            continue;
        SimVar v;
        v.name = nl.elems.at(i).name + QString(".I");
        v.complex = complex;
        v.re.resize(points);
        if (complex)
            v.im.resize(points);
        res->vars.append(v);
    }
}

// One sweep point, in the order resultVars() built: the node voltages first,
// then the currents of the elements that report one.
template <class T>
static void storePoint(SimResult *res, int idx, const Netlist &nl,
                       const QVector<T> &nodeVs, const QVector<T> &elemIs)
{
    int k = 0;
    for (int n = 1; n < nl.nodeCount(); n++) {
        SimVar &v = res->vars[k++];
        v.re[idx] = realPart(nodeVs.at(n));
        if (v.complex)
            v.im[idx] = imagPart(nodeVs.at(n));
    }
    for (int i = 0; i < nl.elems.size(); i++) {
        if (!nl.elems.at(i).reportsCurrent)
            continue;
        SimVar &v = res->vars[k++];
        v.re[idx] = realPart(elemIs.at(i));
        if (v.complex)
            v.im[idx] = imagPart(elemIs.at(i));
    }
}

// Keeps the part of a sweep that did converge, so a diagram can still show it
// when the run stopped early.
static void truncateResult(SimResult *res, int points)
{
    res->sweep.resize(points);
    for (int i = 0; i < res->vars.size(); i++) {
        res->vars[i].re.resize(points);
        res->vars[i].im.resize(points);
    }
}

// ---- analyses ---------------------------------------------------------------

// Guards a sweep length: a tiny Step or a huge Number would otherwise spend
// the machine's memory on a single diagram.
static bool checkPoints(const Component &c, int n, int *out, QString *error)
{
    if (n < 1)
        n = 1;
    if (n > kMaxPoints) {
        *error = c.name + QString(": the sweep asks for %1 points, and this simulator\n"
                                  "runs at most %2 of them.")
                          .arg(n).arg(kMaxPoints);
        return false;
    }
    *out = n;
    return true;
}

bool parseSimParams(const Component &c, SimParams *p, QString *error)
{
    const CompDef *def = compDef(c.type);
    if (!def || !(def->flags & CompIsSim)) {
        *error = c.name + QString(" is not a simulation block.");
        return false;
    }
    *p = SimParams();
    p->name = c.name;

    if (c.type == QLatin1String("DC")) {
        p->kind = SimParams::DC;
        p->src = c.param(0).trimmed();
        p->start = c.value(1);
        p->stop = c.value(2);
        p->step = c.value(3);
        if (p->src.isEmpty()) {
            p->points = 1;                       // a plain operating point
            return true;
        }
        if (p->step == 0.0) {
            *error = c.name + QString(": Step must not be zero.");
            return false;
        }
        const double span = p->stop - p->start;
        if (span != 0.0 && ((span > 0.0) != (p->step > 0.0))) {
            *error = c.name + QString(": Step must be %1 to sweep from %2 to %3.")
                              .arg((span > 0.0) ? QString("positive") : QString("negative"))
                              .arg(num2str(p->start))
                              .arg(num2str(p->stop));
            return false;
        }
        if (!checkPoints(c, (int)floor(fabs(span / p->step)) + 1, &p->points, error))
            return false;
        if (p->points > 1)
            p->step = span / (double)(p->points - 1);
        return true;
    }

    if (c.type == QLatin1String("AC")) {
        p->kind = SimParams::AC;
        p->log = (c.param(0).trimmed().toLower() != QLatin1String("lin"));
        p->start = c.value(1);
        p->stop = c.value(2);
        if (!checkPoints(c, (int)c.value(3), &p->points, error))
            return false;
        if (p->points < 2)
            p->points = 2;
        if (!(p->start > 0.0)) {
            *error = c.name + QString(": Start must be a positive frequency.");
            return false;
        }
        if (!(p->stop > p->start)) {
            *error = c.name + QString(": Stop must lie above Start.");
            return false;
        }
        return true;
    }

    // The only other simulation block in the catalogue.
    p->kind = SimParams::Tran;
    p->start = c.value(0);
    p->stop = c.value(1);
    if (!checkPoints(c, (int)c.value(2), &p->points, error))
        return false;
    if (p->points < 2)
        p->points = 2;
    if (!(p->stop > p->start)) {
        *error = c.name + QString(": Stop must lie after Start.");
        return false;
    }
    return true;
}

bool operatingPoint(const Netlist &nl, double t, OpPoint *op, QString *error)
{
    op->nodeV.clear();
    op->elemI.clear();
    if (!nl.error.isEmpty()) {
        *error = nl.error;
        return false;
    }

    Solver sv(nl);
    sv.prepare(true, false);
    QList<Nonlin> nls;
    collectNonlinear(nl, &nls);

    QVector<double> x;
    if (!dcSolve(nl, sv, &nls, t, false, &x, error))
        return false;

    op->nodeV = nodeVoltages(nl.nodeCount(), x);
    elementCurrents(nl, sv, op->nodeV, x, nls, 0.0, 0, &op->elemI);
    return true;
}

static bool runDc(const Netlist &nl, const SimParams &p, SimResult *res)
{
    res->kind = QString("DC");

    // ---- what, if anything, is swept --------------------------------------
    int swept = -1;
    if (!p.src.isEmpty()) {
        swept = nl.indexOfSource(p.src);
        if (swept < 0) {
            res->error = p.name + QString(": nothing on the schematic is named \"%1\".").arg(p.src);
            return false;
        }
        const Element &e = nl.elems.at(swept);
        if (e.ctrl != CtrlNone || (e.kind != ElVoltageSource && e.kind != ElCurrentSource)) {
            res->error = p.name + QString(": \"%1\" is not an independent source, and only\n"
                                          "those can be swept.").arg(p.src);
            return false;
        }
    }

    // The sweep rewrites a source value, so it works on a copy.
    Netlist work = nl;

    int npts;
    if (swept >= 0) {
        const bool isV = (work.elems.at(swept).kind == ElVoltageSource);
        res->xName = p.src + (isV ? QString(".U") : QString(".I"));
        res->xUnit = isV ? QString("V") : QString("A");
        npts = p.points;
    } else {
        res->xName = QString("index");
        res->xUnit = QString();
        npts = 1;
        res->notes.append(p.name + QString(": no swept source, so this is one operating point."));
    }
    res->sweep.resize(npts);
    for (int i = 0; i < npts; i++)
        res->sweep[i] = (swept >= 0) ? (p.start + p.step * (double)i) : 0.0;
    resultVars(nl, res, false, npts);

    Solver sv(work);
    sv.prepare(true, false);
    QList<Nonlin> nls;
    collectNonlinear(work, &nls);

    QVector<double> x, nv, iv, seed(work.nodeCount());
    seed.fill(0.0);

    for (int s = 0; s < npts; s++) {
        if (swept >= 0)
            work.elems[swept].value = res->sweep.at(s);

        QString err;
        if (!dcSolve(work, sv, &nls, 0.0, true, &x, &err, (s > 0) ? &seed : 0)) {
            res->error = p.name + QString(": at %1 = %2, %3.")
                                    .arg(res->xName)
                                    .arg(axisNum(res->sweep.at(s)), err);
            truncateResult(res, s);
            return false;
        }
        nv = nodeVoltages(work.nodeCount(), x);
        elementCurrents(work, sv, nv, x, nls, 0.0, 0, &iv);
        storePoint(res, s, work, nv, iv);
        seed = nv;                               // the next point starts here
    }
    return true;
}

static bool runAc(const Netlist &nl, const SimParams &p, SimResult *res)
{
    res->kind = QString("AC");
    res->xName = QString("frequency");
    res->xUnit = QString("Hz");
    res->sweep.resize(p.points);
    for (int i = 0; i < p.points; i++) {
        const double f = (double)i / (double)(p.points - 1);
        res->sweep[i] = p.log ? p.start * pow(p.stop / p.start, f)
                              : p.start + (p.stop - p.start) * f;
    }
    resultVars(nl, res, true, p.points);

    // ---- the operating point this sweep is linearized about ----------------
    Solver svDc(nl);
    svDc.prepare(true, false);
    QList<Nonlin> nls;
    collectNonlinear(nl, &nls);
    QVector<double> xd;
    QString err;
    if (!dcSolve(nl, svDc, &nls, 0.0, true, &xd, &err)) {
        res->error = p.name + QString(": the DC operating point an AC sweep is\n"
                                      "linearized about could not be found:\n") + err;
        return false;
    }
    setNonlinFromNodeV(&nls, nodeVoltages(nl.nodeCount(), xd));

    bool hasAcSrc = false;
    for (int i = 0; i < nl.elems.size() && !hasAcSrc; i++) {
        const Element &e = nl.elems.at(i);
        if (e.kind == ElVoltageSource && e.ctrl == CtrlNone && e.p[4] != 0.0)
            hasAcSrc = true;
    }
    if (!hasAcSrc) {
        res->notes.append(p.name + QString(": no component carries an AC magnitude - only an\n"
                                           "\"AC voltage\" source does - so the response is zero."));
    }

    // ---- the sweep ---------------------------------------------------------
    Solver sv(nl);
    sv.prepare(false, false);                    // C and L are admittances here
    Matrix<Cx> m;
    QVector<Cx> x, nv, iv;

    for (int s = 0; s < p.points; s++) {
        double w = 2.0 * M_PI * res->sweep.at(s);
        if (!(w > 1e-6))
            w = 1e-6;                            // 1/jwL has a pole at f = 0
        stampAc(nl, sv, nls, w, &m);
        int badRow = -1;
        if (!m.solve(&x, &badRow)) {
            res->error = p.name + QString(": at %1 Hz, %2.")
                                    .arg(axisNum(res->sweep.at(s)), singularError(nl, badRow));
            truncateResult(res, s);
            return false;
        }
        nv = nodeVoltages(nl.nodeCount(), x);
        elementCurrents(nl, sv, nv, x, nls, w, 0, &iv);
        storePoint(res, s, nl, nv, iv);
    }
    return true;
}

static bool runTran(const Netlist &nl, const SimParams &p, SimResult *res)
{
    res->kind = QString("Tran");
    res->xName = QString("time");
    res->xUnit = QString("s");

    const int npts = p.points;
    const double h = (p.stop - p.start) / (double)(npts - 1);
    res->sweep.resize(npts);
    for (int i = 0; i < npts; i++)
        res->sweep[i] = p.start + h * (double)i;
    resultVars(nl, res, false, npts);

    QList<Nonlin> nls;
    collectNonlinear(nl, &nls);

    // ---- the state the run starts from -------------------------------------
    // Without initial conditions that is the DC operating point, as in SPICE
    // without UIC.  With them it is SPICE's UIC equivalent circuit, where a
    // capacitor holding a voltage is that voltage source and an inductor
    // holding a current is that current source, solved at t = Start.
    bool hasIc = false;
    for (int i = 0; i < nl.elems.size() && !hasIc; i++) {
        const Element &e = nl.elems.at(i);
        if ((e.kind == ElCapacitor || e.kind == ElInductor) && e.p[0] != 0.0)
            hasIc = true;
    }

    Solver sv0(nl);
    sv0.prepare(true, hasIc);
    QVector<double> x0;
    QString err;
    if (!dcSolve(nl, sv0, &nls, p.start, false, &x0, &err)) {
        res->error = p.name + QString(": the state at t = %1 s could not be found:\n%2")
                              .arg(num2str(p.start), err);
        return false;
    }
    const QVector<double> nv0 = nodeVoltages(nl.nodeCount(), x0);
    setNonlinFromNodeV(&nls, nv0);
    if (hasIc) {
        res->notes.append(p.name + QString(": started from the components' initial conditions\n"
                                           "instead of the DC operating point."));
    }

    TranState st;
    st.prepare(nl, nls.size());
    for (int i = 0; i < nl.elems.size(); i++) {
        const Element &e = nl.elems.at(i);
        const double dv = nodeV(nv0, e.node[0]) - nodeV(nv0, e.node[1]);
        if (e.kind == ElCapacitor) {
            if (sv0.vcol(i) >= 0) {
                st.vC[i] = e.p[0];
                st.iC[i] = x0.at(sv0.vcol(i));
            } else {
                st.vC[i] = dv;                   // charged to whatever the op left
            }
        } else if (e.kind == ElInductor) {
            if (sv0.vcol(i) >= 0) {
                st.iL[i] = x0.at(sv0.vcol(i));   // a short, so the voltage is nil
            } else {
                st.vL[i] = dv;                   // a UIC current source
                st.iL[i] = e.p[0];
            }
        }
    }
    for (int k = 0; k < nls.size(); k++)
        st.djV[k] = nls.at(k).j[0].vd;           // no depletion charge moving yet

    QVector<double> iv;
    elementCurrents(nl, sv0, nv0, x0, nls, 0.0, &st, &iv);
    storePoint(res, 0, nl, nv0, iv);

    // ---- the steps ---------------------------------------------------------
    Solver sv(nl);
    sv.prepare(false, false);

    Matrix<double> m;
    QVector<double> x, nv, itX, itNv;

    for (int s = 1; s < npts; s++) {
        const double t = res->sweep.at(s);
        itX.clear();
        itNv.clear();
        bool conv = false;
        QString fail;

        for (int iter = 0; iter < kMaxIter; iter++) {
            stampTran(nl, sv, nls, st, t, h, &m);
            int badRow = -1;
            if (!m.solve(&x, &badRow)) {
                fail = singularError(nl, badRow);
                break;
            }
            nv = nodeVoltages(nl.nodeCount(), x);

            bool limited = false;
            const bool damp = (iter >= kDampFrom);
            for (int k = 0; k < nls.size(); k++) {
                Nonlin &n = nls[k];
                for (int j = 0; j < n.nj; j++) {
                    const double vOld = n.j[j].vd;
                    double v = pnjlim(nodeV(nv, n.jn[j][0]) - nodeV(nv, n.jn[j][1]),
                                      vOld, n.j[j].is, n.j[j].n, &limited);
                    if (damp && fabs(v - vOld) > kAbsV) {
                        v = vOld + 0.5 * (v - vOld);
                        limited = true;
                    }
                    n.j[j].vd = v;
                }
                linearize(&n);
            }

            conv = !limited && !itX.isEmpty();
            for (int i = 0; conv && i < x.size(); i++) {
                conv = (i < nl.nodeCount() - 1) ? convV(nv.at(i + 1), itNv.at(i + 1))
                                                : convI(x.at(i), itX.at(i));
            }
            itX = x;
            itNv = nv;
            if (conv)
                break;
        }

        if (fail.isEmpty() && !conv)
            fail = QString("did not settle in %1 iterations").arg(kMaxIter);
        if (!fail.isEmpty()) {
            truncateResult(res, s);
            res->error = p.name + QString(": at t = %1 s the step %2.")
                                    .arg(num2str(t), fail);
            res->notes.append(QString("%1 of %2 points were computed before the run stopped.")
                              .arg(s).arg(npts));
            return false;
        }

        // Accepted: the companion history moves to the end of this step, using
        // the same trapezoidal rule the stamping used.
        for (int i = 0; i < nl.elems.size(); i++) {
            const Element &e = nl.elems.at(i);
            const double dv = nodeV(nv, e.node[0]) - nodeV(nv, e.node[1]);
            if (e.kind == ElCapacitor) {
                const double geq = 2.0 * e.value / h;
                st.iC[i] = geq * (dv - st.vC.at(i)) - st.iC.at(i);
                st.vC[i] = dv;
            } else if (e.kind == ElInductor) {
                const double geq = h / (2.0 * e.value);
                st.iL[i] = st.iL.at(i) + geq * (dv + st.vL.at(i));
                st.vL[i] = dv;
            }
        }
        for (int k = 0; k < nls.size(); k++) {
            const Nonlin &n = nls.at(k);
            if (!(n.cj > 0.0))
                continue;
            const double dv = n.j[0].vd;         // the accepted junction voltage
            const double geq = 2.0 * n.cj / h;
            st.djI[k] = geq * (dv - st.djV.at(k)) - st.djI.at(k);
            st.djV[k] = dv;
        }

        elementCurrents(nl, sv, nv, x, nls, 0.0, &st, &iv);
        storePoint(res, s, nl, nv, iv);
    }
    return true;
}

bool runSimulation(const Netlist &nl, const SimParams &p, SimResult *res)
{
    res->sim = p.name;
    res->kind.clear();
    res->xName.clear();
    res->xUnit.clear();
    res->sweep.clear();
    res->vars.clear();
    res->error.clear();
    res->notes = nl.warnings;

    if (!nl.error.isEmpty()) {
        res->error = nl.error;
        return false;
    }

    switch (p.kind) {
    case SimParams::DC:
        return runDc(nl, p, res);
    case SimParams::AC:
        return runAc(nl, p, res);
    default:
        return runTran(nl, p, res);
    }
}
