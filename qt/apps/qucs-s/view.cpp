/*
 * Qucs-S, ported to EwokOS - the schematic canvas.
 *
 * Upstream: https://github.com/ra3xdh/qucs_s (GPL-2.0), which spreads this
 * over qucs/schematic.cpp (a QScrollView of canvas items) and qucs/
 * mouseactions.cpp (one class per mouse mode: SelectMouse, WireMouse,
 * PaintMouse, ...).  Here the widget *is* the sheet, so painting and the five
 * mouse modes are one file, and the modes are the Tool enum of view.h instead
 * of a class each: a sheet of a few hundred elements repaints in full quickly
 * enough that the per-item damage tracking upstream needs for big designs
 * buys nothing.
 *
 * Three of upstream's conventions are kept exactly - everything snaps to the
 * 10 unit grid; elements and wires are dark blue, the selection red, and an
 * element its `active` flag switched off is gray; and a wire end landing on
 * another wire connects the two (a T junction) while a bare crossing does not,
 * which is why only a T junction gets a junction dot.  netlist.cpp reads the
 * sheet by that same rule, so the dot is the promise it makes.
 */

#include "view.h"

#include "components.h"
#include "diagram.h"

#include <QFont>
#include <QFontMetrics>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QVector>

// ---- colours and sheet ------------------------------------------------------

static const QColor kSheetColor(255, 255, 255);
static const QColor kGridColor(215, 215, 215);
static const QColor kElemColor(0, 0, 130);           // upstream's element colour
static const QColor kSelColor(200, 0, 0);
static const QColor kOffColor(150, 150, 150);        // active == false
static const QColor kTextColor(90, 90, 90);
static const QColor kNetColor(140, 90, 0);           // a wire label
static const QColor kNetBox(255, 250, 225);
static const QColor kGhostColor(90, 90, 200);
static const QColor kBandFill(90, 90, 200, 28);

// The sheet this port draws on.  Upstream grows its canvas when something is
// placed past the edge; a fixed sheet keeps the widget, the scroll area and
// the file format simple, and 160 x 120 grid points is far more than a
// circuit this simulator solves in reasonable time needs.
static const int kSheetW = 1600;
static const int kSheetH = 1200;

// A diagram smaller than this has no room for its axes.
static const int kMinDiagW = 80;
static const int kMinDiagH = 60;

// ---- small geometry helpers -------------------------------------------------

static QPoint clampToSheet(const QPoint &p, const QSize &s)
{
    return QPoint(qBound(0, p.x(), s.width() - kGrid),
                  qBound(0, p.y(), s.height() - kGrid));
}

static int clampAxis(int lo, int v, int hi)
{
    return (lo > hi) ? v : qBound(lo, v, hi);
}

// True when p is on the wire but is not one of its ends.
static bool interior(const Wire &w, const QPoint &p)
{
    return w.contains(p) && p != w.p1 && p != w.p2;
}

// The points where wires join.  Two wires that cross in the middle of both are
// left out: Qucs does not connect them, so drawing a dot there would say
// something the netlist does not do.
static void junctionDots(const QList<Wire> &wires, QList<QPoint> *dots)
{
    for (int i = 0; i < wires.size(); i++) {
        const Wire &a = wires.at(i);
        for (int j = i + 1; j < wires.size(); j++) {
            const Wire &b = wires.at(j);
            QPoint c;
            bool found = false;

            if (a.horizontal() != b.horizontal()) {
                const Wire &h = a.horizontal() ? a : b;
                const Wire &v = a.horizontal() ? b : a;
                c = QPoint(v.p1.x(), h.p1.y());
                // Exactly one of them ends there - a T.  Both middles is a
                // crossing, neither is a plain corner.
                found = h.contains(c) && v.contains(c)
                        && (interior(h, c) != interior(v, c));
            } else {
                // Same direction: collinear overlap joins where an end of one
                // lands inside the other.
                const QPoint eb[2] = { b.p1, b.p2 };
                const QPoint ea[2] = { a.p1, a.p2 };
                for (int k = 0; k < 2 && !found; k++)
                    if (interior(a, eb[k])) { c = eb[k]; found = true; }
                for (int k = 0; k < 2 && !found; k++)
                    if (interior(b, ea[k])) { c = ea[k]; found = true; }
            }

            if (found && !dots->contains(c))
                dots->append(c);
        }
    }
}

// The grid dots of `r` in one drawPoints(): a full sheet holds 19200 of them
// and one call each per repaint is what makes wiring feel sluggish.
static void paintGrid(QPainter *p, const QRect &r)
{
    const int x0 = qMax(0, (r.left() / kGrid) * kGrid);
    const int y0 = qMax(0, (r.top() / kGrid) * kGrid);
    QVector<QPoint> pts;
    pts.reserve(((r.right() - x0) / kGrid + 1) * ((r.bottom() - y0) / kGrid + 1));
    for (int y = y0; y <= r.bottom(); y += kGrid)
        for (int x = x0; x <= r.right(); x += kGrid)
            pts.append(QPoint(x, y));
    if (pts.isEmpty())
        return;
    p->setPen(QPen(kGridColor, 1));
    p->drawPoints(pts.constData(), pts.size());
}

// ---- construction -----------------------------------------------------------

SchematicView::SchematicView(QWidget *parent)
    : QWidget(parent), m_sch(0), m_res(0), m_sheet(kSheetW, kSheetH),
      m_tool(ToolSelect), m_cursor(-1, -1), m_pressPos(0, 0),
      m_wiring(false), m_dragging(false), m_banding(false), m_resizing(-1)
{
    // The sheet is the widget and the main window scrolls it, so the size is
    // fixed and widget coordinates are sheet coordinates.
    setFixedSize(m_sheet);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::ArrowCursor);
}

void SchematicView::setSchematic(Schematic *sch)
{
    if (m_sch == sch)
        return;
    if (m_sch) {
        disconnect(m_sch, SIGNAL(changed()), this, SLOT(update()));
        disconnect(m_sch, SIGNAL(structureChanged()), this, SLOT(clearSelection()));
    }
    m_sch = sch;
    m_wiring = false;
    m_dragging = false;
    m_banding = false;
    m_resizing = -1;
    m_selComp.clear();
    m_selWire.clear();
    m_selDiag.clear();

    if (m_sch) {
        connect(m_sch, SIGNAL(changed()), this, SLOT(update()));
        // Adding or removing an element moves the indices the selection
        // holds, so a structural edit drops it rather than let it point at
        // whatever slid into the freed slot.
        connect(m_sch, SIGNAL(structureChanged()), this, SLOT(clearSelection()));
    }
    emit selectionChanged();
    update();
}

void SchematicView::setSimResult(const SimResult *res)
{
    m_res = res;
    update();
}

void SchematicView::setTool(int t)
{
    if (m_tool == t)
        return;
    m_tool = t;
    m_wiring = false;
    m_dragging = false;
    m_banding = false;
    m_resizing = -1;
    m_band = QRect();
    if (t != ToolSelect)
        clearSelection();
    setCursor(t == ToolSelect ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
    emit toolChanged(t);
}

void SchematicView::setPlaceType(const QString &type)
{
    m_placeType = type;
    if (m_tool != ToolPlace)
        setTool(ToolPlace);
    else
        update();
}

// ---- selection --------------------------------------------------------------

bool SchematicView::hasSelection() const
{
    return !m_selComp.isEmpty() || !m_selWire.isEmpty() || !m_selDiag.isEmpty();
}

int SchematicView::selectedCount() const
{
    return m_selComp.size() + m_selWire.size() + m_selDiag.size();
}

bool SchematicView::isSelected(int comp, int wire, int diagram) const
{
    return (comp >= 0 && m_selComp.contains(comp))
           || (wire >= 0 && m_selWire.contains(wire))
           || (diagram >= 0 && m_selDiag.contains(diagram));
}

void SchematicView::emitSelection()
{
    const int n = selectedCount();
    if (n > 0)
        emit statusMessage(tr("%1 element(s) selected").arg(n));
    emit selectionChanged();
}

void SchematicView::clearSelection()
{
    m_dragging = false;
    m_resizing = -1;
    if (!hasSelection()) {
        update();
        return;
    }
    m_selComp.clear();
    m_selWire.clear();
    m_selDiag.clear();
    update();
    emit selectionChanged();
}

void SchematicView::setSelected(int comp, int wire, int diagram, bool add)
{
    if (!add) {
        m_selComp.clear();
        m_selWire.clear();
        m_selDiag.clear();
    }
    if (comp >= 0 && !m_selComp.contains(comp))
        m_selComp.append(comp);
    if (wire >= 0 && !m_selWire.contains(wire))
        m_selWire.append(wire);
    if (diagram >= 0 && !m_selDiag.contains(diagram))
        m_selDiag.append(diagram);
    update();
    emitSelection();
}

void SchematicView::selectAll()
{
    if (!m_sch)
        return;
    m_selComp.clear();
    m_selWire.clear();
    m_selDiag.clear();
    for (int i = 0; i < m_sch->components().size(); i++)
        m_selComp.append(i);
    for (int i = 0; i < m_sch->wires().size(); i++)
        m_selWire.append(i);
    for (int i = 0; i < m_sch->diagrams().size(); i++)
        m_selDiag.append(i);
    update();
    emitSelection();
}

void SchematicView::deleteSelection()
{
    if (!m_sch || !hasSelection())
        return;
    // Copies: each remove*() below emits structureChanged(), which is wired to
    // clearSelection() and would empty the lists half way through.
    const QList<int> comps = m_selComp;
    const QList<int> wires = m_selWire;
    const QList<int> diags = m_selDiag;
    m_selComp.clear();
    m_selWire.clear();
    m_selDiag.clear();

    m_sch->removeDiagrams(diags);
    m_sch->removeWires(wires);
    m_sch->removeComponents(comps);
    update();
    emitSelection();
    emit edited();
}

void SchematicView::rotateSelection()
{
    if (!m_sch || !hasSelection())
        return;

    // The centre of the selected components and wires, snapped: turning about
    // a centre between grid points would leave the wires hanging off them.
    QPoint c(0, 0);
    int n = 0;
    for (int i = 0; i < m_selComp.size(); i++) {
        c += m_sch->components().at(m_selComp.at(i)).pos;
        n++;
    }
    for (int i = 0; i < m_selWire.size(); i++) {
        const Wire &w = m_sch->wires().at(m_selWire.at(i));
        c += w.p1;
        c += w.p2;
        n += 2;
    }
    if (n == 0)
        return;                    // diagrams only, and those have no orientation
    c = snapToGrid(QPoint(c.x() / n, c.y() / n));

    QList<Component> &comps = m_sch->components();
    for (int i = 0; i < m_selComp.size(); i++) {
        Component &x = comps[m_selComp.at(i)];
        const QPoint d = x.pos - c;
        // 90 degrees clockwise on a sheet whose y points down: (x,y) -> (-y,x).
        x.pos = clampToSheet(c + QPoint(-d.y(), d.x()), m_sheet);
        x.rot = (x.rot + 1) & 3;
    }
    QList<Wire> &wires = m_sch->wires();
    for (int i = 0; i < m_selWire.size(); i++) {
        Wire &w = wires[m_selWire.at(i)];
        const QPoint d1 = w.p1 - c, d2 = w.p2 - c;
        w.p1 = clampToSheet(c + QPoint(-d1.y(), d1.x()), m_sheet);
        w.p2 = clampToSheet(c + QPoint(-d2.y(), d2.x()), m_sheet);
    }
    m_sch->touch();
    update();
    emit edited();
}

void SchematicView::mirrorSelection()
{
    if (!m_sch || !hasSelection())
        return;

    QPoint c(0, 0);
    int n = 0;
    for (int i = 0; i < m_selComp.size(); i++) {
        c += m_sch->components().at(m_selComp.at(i)).pos;
        n++;
    }
    for (int i = 0; i < m_selWire.size(); i++) {
        const Wire &w = m_sch->wires().at(m_selWire.at(i));
        c += w.p1;
        c += w.p2;
        n += 2;
    }
    if (n == 0)
        return;
    c = snapToGrid(QPoint(c.x() / n, c.y() / n));

    QList<Component> &comps = m_sch->components();
    for (int i = 0; i < m_selComp.size(); i++) {
        Component &x = comps[m_selComp.at(i)];
        x.pos = clampToSheet(QPoint(2 * c.x() - x.pos.x(), x.pos.y()), m_sheet);
        x.mirror = !x.mirror;
    }
    QList<Wire> &wires = m_sch->wires();
    for (int i = 0; i < m_selWire.size(); i++) {
        Wire &w = wires[m_selWire.at(i)];
        w.p1 = clampToSheet(QPoint(2 * c.x() - w.p1.x(), w.p1.y()), m_sheet);
        w.p2 = clampToSheet(QPoint(2 * c.x() - w.p2.x(), w.p2.y()), m_sheet);
    }
    m_sch->touch();
    update();
    emit edited();
}

void SchematicView::toggleActive()
{
    if (!m_sch || m_selComp.isEmpty()) {
        emit statusMessage(tr("Select the element to switch on or off first."));
        return;
    }
    QList<Component> &comps = m_sch->components();
    for (int i = 0; i < m_selComp.size(); i++)
        comps[m_selComp.at(i)].active = !comps[m_selComp.at(i)].active;
    m_sch->touch();
    update();
    emit edited();
}

// ---- hit testing ------------------------------------------------------------

QFont SchematicView::labelFont() const
{
    QFont f = font();
    // diagram.cpp scales the font it is handed by its point size, which a
    // font specified in pixels has not got - it would come back as -1.
    if (f.pointSizeF() <= 0.0)
        f.setPointSizeF(9.0);
    return f;
}

QRect SchematicView::compHitRect(const Component &c, const QFontMetrics &fm) const
{
    const QRect r = compRect(c);
    const CompDef *def = compDef(c.type);
    // A simulation block draws its name and its properties inside itself.
    if (def && (def->flags & CompIsSim))
        return r;

    const QStringList lines = c.labelLines();
    int w = 0;
    for (int i = 0; i < lines.size(); i++)
        w = qMax(w, fm.horizontalAdvance(lines.at(i)));
    if (w <= 0)
        return r;
    return r.united(QRect(r.right() + 6, r.top(), w, lines.size() * fm.height()));
}

int SchematicView::hitComponent(const QPoint &p) const
{
    if (!m_sch)
        return -1;
    const QFontMetrics fm(labelFont());
    const QList<Component> &cs = m_sch->components();
    // Backwards: the last one drawn is the one on top.
    for (int i = cs.size() - 1; i >= 0; i--)
        if (compHitRect(cs.at(i), fm).contains(p))
            return i;
    return -1;
}

int SchematicView::hitWire(const QPoint &p) const
{
    if (!m_sch)
        return -1;
    const QList<Wire> &ws = m_sch->wires();
    for (int i = ws.size() - 1; i >= 0; i--) {
        const Wire &w = ws.at(i);
        // bounds() is 3 px fat on all four sides, so it also reaches past the
        // ends of the segment - the exact extent decides.
        if (!w.bounds().contains(p))
            continue;
        if (w.horizontal()) {
            if (p.x() < qMin(w.p1.x(), w.p2.x()) || p.x() > qMax(w.p1.x(), w.p2.x()))
                continue;
        } else if (p.y() < qMin(w.p1.y(), w.p2.y()) || p.y() > qMax(w.p1.y(), w.p2.y())) {
            continue;
        }
        return i;
    }
    return -1;
}

int SchematicView::hitDiagram(const QPoint &p) const
{
    if (!m_sch)
        return -1;
    const QList<Diagram> &ds = m_sch->diagrams();
    for (int i = ds.size() - 1; i >= 0; i--)
        if (ds.at(i).rect().contains(p))
            return i;
    return -1;
}

int SchematicView::hitDiagramHandle(const QPoint &p) const
{
    if (!m_sch)
        return -1;
    const QList<Diagram> &ds = m_sch->diagrams();
    for (int i = ds.size() - 1; i >= 0; i--) {
        const QRect h(ds.at(i).rect().bottomRight() - QPoint(4, 4), QSize(9, 9));
        if (h.contains(p))
            return i;
    }
    return -1;
}

// ---- editing helpers --------------------------------------------------------

void SchematicView::addWireRun(const QPoint &a, const QPoint &b)
{
    if (!m_sch || a == b)
        return;
    if (a.x() == b.x() || a.y() == b.y()) {
        m_sch->addWire(Wire(a, b));
        return;
    }
    // An L: horizontal first, then vertical - the way upstream's wire pen
    // bends when the cursor is on neither the same row nor the same column.
    const QPoint mid(b.x(), a.y());
    m_sch->addWire(Wire(a, mid));
    m_sch->addWire(Wire(mid, b));
}

void SchematicView::labelWire(int index)
{
    if (!m_sch || index < 0 || index >= m_sch->wires().size())
        return;
    Wire &w = m_sch->wires()[index];

    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Wire label"),
            tr("Name of the net this wire belongs to (empty removes it):"),
            QLineEdit::Normal, w.label, &ok);
    if (!ok)
        return;

    const QString label = text.trimmed();
    if (label == w.label)
        return;
    w.label = label;
    m_sch->touch();
    update();
    emit edited();
    emit statusMessage(label.isEmpty() ? tr("Net label removed.")
                                       : tr("Net named %1.").arg(label));
}

void SchematicView::rememberDrag()
{
    m_dragComp.clear();
    m_dragWire.clear();
    m_dragDiag.clear();
    if (!m_sch)
        return;
    for (int i = 0; i < m_selComp.size(); i++)
        if (m_selComp.at(i) < m_sch->components().size())
            m_dragComp.append(m_sch->components().at(m_selComp.at(i)).pos);
    for (int i = 0; i < m_selWire.size(); i++)
        if (m_selWire.at(i) < m_sch->wires().size())
            m_dragWire.append(m_sch->wires().at(m_selWire.at(i)));
    for (int i = 0; i < m_selDiag.size(); i++)
        if (m_selDiag.at(i) < m_sch->diagrams().size())
            m_dragDiag.append(m_sch->diagrams().at(m_selDiag.at(i)));
}

void SchematicView::applyDrag(const QPoint &delta)
{
    if (!m_sch || delta.isNull())
        return;

    // The selection moves as one, so what has to stay on the sheet is the
    // delta and not each element on its own - clamping element by element
    // would pull a group apart.  The bounds come from where the drag started,
    // not from where the elements are now, or the delta would count twice.
    QRect all;
    for (int i = 0; i < m_dragComp.size() && i < m_selComp.size(); i++) {
        Component t = m_sch->components().at(m_selComp.at(i));
        t.pos = m_dragComp.at(i);
        all = all.united(compRect(t));
    }
    for (int i = 0; i < m_dragWire.size(); i++)
        all = all.united(m_dragWire.at(i).bounds());
    for (int i = 0; i < m_dragDiag.size(); i++)
        all = all.united(m_dragDiag.at(i).rect());
    if (all.isNull())
        return;

    QPoint d(delta);
    d.setX(clampAxis(-all.left(), d.x(), (m_sheet.width() - kGrid) - all.right()));
    d.setY(clampAxis(-all.top(), d.y(), (m_sheet.height() - kGrid) - all.bottom()));
    if (d.isNull())
        return;

    for (int i = 0; i < m_dragComp.size() && i < m_selComp.size(); i++)
        m_sch->components()[m_selComp.at(i)].pos = m_dragComp.at(i) + d;
    for (int i = 0; i < m_dragWire.size() && i < m_selWire.size(); i++) {
        Wire &w = m_sch->wires()[m_selWire.at(i)];
        w.p1 = m_dragWire.at(i).p1 + d;
        w.p2 = m_dragWire.at(i).p2 + d;
    }
    for (int i = 0; i < m_dragDiag.size() && i < m_selDiag.size(); i++)
        m_sch->diagrams()[m_selDiag.at(i)].pos = m_dragDiag.at(i).pos + d;

    m_sch->touch();
    update();
    emit edited();
}

// ---- mouse ------------------------------------------------------------------

void SchematicView::mousePressEvent(QMouseEvent *e)
{
    const QPoint p = snapToGrid(e->pos());
    m_cursor = p;
    setFocus(Qt::MouseFocusReason);

    if (e->button() == Qt::RightButton) {
        // The right button is upstream's way out of whatever is going on: the
        // half-drawn wire, the placement state, the selection.
        if (m_wiring || m_banding || m_tool != ToolSelect) {
            m_wiring = false;
            m_banding = false;
            m_band = QRect();
            m_dragging = false;
            m_resizing = -1;
            if (m_tool != ToolSelect)
                setTool(ToolSelect);
            else
                update();
            emit statusMessage(QString());
            return;
        }
        clearSelection();
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;

    // The wire pen gets the press before m_pressPos moves: that member holds
    // the point the run started at, and the run is only over once this press
    // has been answered.
    if (m_tool == ToolWire) {
        if (!m_wiring) {
            m_wiring = true;
            m_pressPos = p;
            emit statusMessage(tr("Click to add a bend, Esc or a right-click to end the wire."));
        } else if (m_pressPos == p) {
            // A click back on the start ends the run without drawing one.
            m_wiring = false;
            emit statusMessage(QString());
        } else {
            addWireRun(m_pressPos, p);
            emit edited();
            // Chaining: the next run starts where this one ended, the way
            // upstream's pen keeps going until it is told to stop.
            m_pressPos = p;
        }
        update();
        return;
    }

    m_pressPos = p;

    switch (m_tool) {
    case ToolWire:
        break;                                       // handled above

    case ToolLabel: {
        const int w = hitWire(p);
        if (w >= 0)
            labelWire(w);
        else
            emit statusMessage(tr("Click on a wire to name the net it carries."));
        break;
    }

    case ToolPlace: {
        const CompDef *def = compDef(m_placeType);
        if (!m_sch || !def)
            break;
        Component c(m_placeType, clampToSheet(p, m_sheet));
        c.name = m_sch->uniqueName(m_placeType);
        const int idx = m_sch->addComponent(c);      // clears the selection
        emit edited();
        setSelected(idx, -1, -1, false);
        // R and M work straight after placing, which is what puts a resistor
        // upright without a detour through the pointer tool.
        emit statusMessage(tr("%1 placed - R rotates, M mirrors, Esc stops placing.")
                           .arg(QLatin1String(def->label)));
        break;
    }

    case ToolDiagram:
        m_banding = true;
        m_band = QRect(p, QSize(0, 0));
        update();
        break;

    default: {                                       // ToolSelect
        const bool add = (e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) != 0;

        const int h = hitDiagramHandle(p);
        if (h >= 0) {
            m_resizing = h;
            setSelected(-1, -1, h, false);
            break;
        }

        // Reverse paint order, so the topmost thing under the cursor wins.
        const int dg = hitDiagram(p);
        const int cp = (dg < 0) ? hitComponent(p) : -1;
        const int wr = (dg < 0 && cp < 0) ? hitWire(p) : -1;

        if (dg < 0 && cp < 0 && wr < 0) {
            if (!add)
                clearSelection();
            m_banding = true;
            m_band = QRect(p, QSize(0, 0));
            update();
            break;
        }
        if (add && isSelected(cp, wr, dg)) {
            // Shift on something already selected takes it out again.
            if (cp >= 0) m_selComp.removeAll(cp);
            if (wr >= 0) m_selWire.removeAll(wr);
            if (dg >= 0) m_selDiag.removeAll(dg);
            m_dragging = false;
            update();
            emitSelection();
            break;
        }
        setSelected(cp, wr, dg, add);
        m_dragging = true;
        rememberDrag();
        break;
    }
    }
}

void SchematicView::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint p = snapToGrid(e->pos());

    if (m_banding) {
        m_band = QRect(m_pressPos, p).normalized();
        update();
        return;
    }

    if (m_resizing >= 0 && m_sch && m_resizing < m_sch->diagrams().size()) {
        Diagram &d = m_sch->diagrams()[m_resizing];
        const int w = qMax(kMinDiagW, p.x() - d.pos.x());
        const int h = qMax(kMinDiagH, p.y() - d.pos.y());
        const QSize s(qMin(w, m_sheet.width() - d.pos.x()),
                      qMin(h, m_sheet.height() - d.pos.y()));
        if (s != d.size) {
            d.size = s;
            m_sch->touch();
            update();
            emit edited();
        }
        return;
    }

    if (m_dragging) {
        applyDrag(p - m_pressPos);
        return;
    }

    if (p == m_cursor)
        return;
    m_cursor = p;
    emit statusMessage(tr("%1, %2").arg(p.x()).arg(p.y()));
    // The wire preview, the component ghost and the wire the label tool would
    // name all follow the cursor, so those modes repaint on every grid step.
    if (m_tool != ToolSelect)
        update();
}

void SchematicView::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;

    if (m_banding) {
        m_banding = false;
        const QRect r = m_band.normalized();
        m_band = QRect();

        if (m_tool == ToolDiagram && m_sch) {
            // A drag that went nowhere still places a diagram, at its default
            // size - one click is less than click, drag, release.
            Diagram d;
            if (r.width() >= kMinDiagW && r.height() >= kMinDiagH)
                d.size = QSize(r.width(), r.height());
            d.pos = clampToSheet(QPoint(qMin(r.left(), m_sheet.width() - d.size.width()),
                                        qMin(r.top(), m_sheet.height() - d.size.height())),
                                 m_sheet);
            const int idx = m_sch->addDiagram(d);    // clears the selection
            emit edited();
            setSelected(-1, -1, idx, false);
            emit statusMessage(tr("Diagram added - double-click it to choose what it plots."));
        } else if (m_sch && (r.width() > 4 || r.height() > 4)) {
            const bool add = (e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) != 0;
            if (!add) {
                m_selComp.clear();
                m_selWire.clear();
                m_selDiag.clear();
            }
            const QFontMetrics fm(labelFont());
            for (int i = 0; i < m_sch->components().size(); i++)
                if (!m_selComp.contains(i) && r.intersects(compHitRect(m_sch->components().at(i), fm)))
                    m_selComp.append(i);
            for (int i = 0; i < m_sch->wires().size(); i++)
                if (!m_selWire.contains(i) && r.intersects(m_sch->wires().at(i).bounds()))
                    m_selWire.append(i);
            for (int i = 0; i < m_sch->diagrams().size(); i++)
                if (!m_selDiag.contains(i) && r.intersects(m_sch->diagrams().at(i).rect()))
                    m_selDiag.append(i);
            emitSelection();
        }
        update();
        return;
    }

    if (m_resizing >= 0) {
        m_resizing = -1;
        update();
        return;
    }
    m_dragging = false;
}

void SchematicView::mouseDoubleClickEvent(QMouseEvent *e)
{
    const QPoint p = snapToGrid(e->pos());
    const int dg = hitDiagram(p);
    if (dg >= 0) {
        setSelected(-1, -1, dg, false);
        emit editDiagram(dg);
        return;
    }
    const int cp = hitComponent(p);
    if (cp >= 0) {
        setSelected(cp, -1, -1, false);
        emit editComponent(cp);
        return;
    }
    const int wr = hitWire(p);
    if (wr >= 0)
        labelWire(wr);
}

void SchematicView::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Escape:
        if (m_wiring) {
            m_wiring = false;
            emit statusMessage(QString());
        } else if (m_banding) {
            m_banding = false;
            m_band = QRect();
        } else if (m_tool != ToolSelect) {
            setTool(ToolSelect);                     // emits toolChanged
        } else {
            clearSelection();
        }
        update();
        break;

    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        deleteSelection();
        break;

    case Qt::Key_R:
        rotateSelection();
        break;

    case Qt::Key_M:
        mirrorSelection();
        break;

    case Qt::Key_A:
        if ((e->modifiers() & Qt::ControlModifier) != 0)
            selectAll();
        else
            e->ignore();
        break;

    case Qt::Key_Left:
        rememberDrag();
        applyDrag(QPoint(-kGrid, 0));
        break;
    case Qt::Key_Right:
        rememberDrag();
        applyDrag(QPoint(kGrid, 0));
        break;
    case Qt::Key_Up:
        rememberDrag();
        applyDrag(QPoint(0, -kGrid));
        break;
    case Qt::Key_Down:
        rememberDrag();
        applyDrag(QPoint(0, kGrid));
        break;

    default:
        e->ignore();
        return;
    }
    e->accept();
}

void SchematicView::leaveEvent(QEvent *)
{
    // Out of the widget the ghost has nowhere to be; -1 says so and the
    // paint code skips it.
    if (m_cursor.x() >= 0) {
        m_cursor = QPoint(-1, -1);
        if (m_tool != ToolSelect)
            update();
    }
}

// ---- painting ---------------------------------------------------------------

void SchematicView::paintComponent(QPainter *p, const Component &c, bool selected) const
{
    QColor col = kElemColor;
    if (selected)
        col = kSelColor;
    else if (!c.active)
        col = kOffColor;

    p->save();
    p->setTransform(compTransform(c.pos, c.rot, c.mirror), true);
    p->setPen(QPen(col, 2));
    p->setBrush(Qt::NoBrush);
    paintCompSymbol(p, c);            // sets its own fonts, hence the save
    p->restore();

    const CompDef *def = compDef(c.type);
    if (def && (def->flags & CompIsSim))
        return;                       // a simulation block is labelled inside

    // The instance name and the properties flagged visible, horizontal beside
    // the symbol however the symbol itself is turned.
    const QStringList lines = c.labelLines();
    if (lines.isEmpty())
        return;

    const QFont f = labelFont();
    const QFontMetrics fm(f);
    const QRect r = compRect(c);
    p->save();
    p->setFont(f);
    p->setPen(selected ? kSelColor : kTextColor);
    for (int i = 0; i < lines.size(); i++) {
        const int y = r.top() + fm.ascent() + i * fm.height();
        if (y > m_sheet.height())
            break;
        p->drawText(r.right() + 6, y, lines.at(i));
    }
    p->restore();
}

void SchematicView::paintEvent(QPaintEvent *e)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), kSheetColor);
    paintGrid(&p, e->rect());

    if (!m_sch)
        return;

    const QFont lf = labelFont();
    const QFontMetrics fm(lf);
    const QList<Wire> &wires = m_sch->wires();

    // ---- wires and their net labels ----
    for (int i = 0; i < wires.size(); i++) {
        const Wire &w = wires.at(i);
        const bool sel = m_selWire.contains(i);
        p.setPen(QPen(sel ? kSelColor : kElemColor, 2));
        p.drawLine(w.p1, w.p2);
        if (w.label.isEmpty())
            continue;

        const int tw = fm.horizontalAdvance(w.label);
        const QPoint mid((w.p1.x() + w.p2.x()) / 2, (w.p1.y() + w.p2.y()) / 2);
        const QRect box(mid.x() - tw / 2 - 3, mid.y() - fm.height() - 1,
                        tw + 6, fm.height() + 2);
        p.save();
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setFont(lf);
        p.fillRect(box, kNetBox);
        p.setPen(QPen(sel ? kSelColor : kNetColor, 1));
        p.drawRect(box);
        p.drawText(box, Qt::AlignCenter, w.label);
        p.restore();
    }

    // ---- junction dots ----
    QList<QPoint> dots;
    junctionDots(wires, &dots);
    p.setPen(Qt::NoPen);
    p.setBrush(kElemColor);
    for (int i = 0; i < dots.size(); i++)
        p.drawEllipse(dots.at(i), 3, 3);
    p.setBrush(Qt::NoBrush);

    // ---- components ----
    for (int i = 0; i < m_sch->components().size(); i++)
        paintComponent(&p, m_sch->components().at(i), m_selComp.contains(i));

    // ---- diagrams ----
    for (int i = 0; i < m_sch->diagrams().size(); i++) {
        const Diagram &d = m_sch->diagrams().at(i);
        paintDiagram(&p, d, m_res, lf);
        if (!m_selDiag.contains(i))
            continue;
        p.save();
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(kSelColor, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(d.rect());
        // The handle a drag resizes the diagram by.
        p.setPen(Qt::NoPen);
        p.setBrush(kSelColor);
        p.drawRect(QRect(d.rect().bottomRight() - QPoint(4, 4), QSize(8, 8)));
        p.restore();
    }

    // ---- what the cursor is about to do ----
    if (m_cursor.x() < 0)
        return;

    if (m_tool == ToolWire && m_wiring) {
        p.setPen(QPen(kGhostColor, 2, Qt::DashLine));
        if (m_pressPos.x() == m_cursor.x() || m_pressPos.y() == m_cursor.y()) {
            p.drawLine(m_pressPos, m_cursor);
        } else {
            const QPoint mid(m_cursor.x(), m_pressPos.y());
            p.drawLine(m_pressPos, mid);
            p.drawLine(mid, m_cursor);
        }
        p.setBrush(kGhostColor);
        p.setPen(Qt::NoPen);
        p.drawEllipse(m_pressPos, 3, 3);
        p.drawEllipse(m_cursor, 3, 3);
        p.setBrush(Qt::NoBrush);
    } else if (m_tool == ToolPlace && !m_placeType.isEmpty()) {
        Component ghost(m_placeType, clampToSheet(m_cursor, m_sheet));
        ghost.name = m_sch->uniqueName(m_placeType);
        p.setOpacity(0.6);
        paintComponent(&p, ghost, false);
        p.setOpacity(1.0);
        // The ports, so a drop lands where the wires will meet.
        const QList<QPoint> ports = compPorts(ghost);
        p.setPen(Qt::NoPen);
        p.setBrush(kGhostColor);
        for (int i = 0; i < ports.size(); i++)
            p.drawEllipse(ports.at(i), 3, 3);
        p.setBrush(Qt::NoBrush);
    } else if (m_tool == ToolLabel) {
        const int w = hitWire(m_cursor);
        if (w >= 0) {
            p.setPen(QPen(kSelColor, 4));
            p.drawLine(wires.at(w).p1, wires.at(w).p2);
        }
    }

    if (!m_banding)
        return;
    const QRect r = m_band.normalized();
    if (r.width() < 2 && r.height() < 2)
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    if (m_tool == ToolDiagram) {
        p.setPen(QPen(kElemColor, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
    } else {
        p.setPen(QPen(kGhostColor, 1, Qt::DashLine));
        p.setBrush(kBandFill);
    }
    p.drawRect(r);
    p.restore();
}
