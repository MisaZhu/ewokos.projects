/*
 * Qucs-S, ported to EwokOS - the property dialogs.
 *
 * See compdialog.h for what upstream does instead.
 */

#include "compdialog.h"

#include "misc.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVariant>
#include <QVBoxLayout>

#include <math.h>

// The four independent sources of the Sources group: what a DC sweep may
// sweep.  The controlled sources are left out on purpose - sweeping one from
// another is a loop this simulator would have to unwind, and simulator.cpp
// says so when it is handed one.
static bool isIndependentSource(const QString &type)
{
    return type == QLatin1String("Vdc") || type == QLatin1String("Idc")
           || type == QLatin1String("Vac") || type == QLatin1String("Vpulse");
}

// The properties that end up in a denominator somewhere in the netlist.  A
// non-positive one is replaced by a default with a warning in netlist.cpp;
// saying so here saves the round trip through the simulator.
static bool mustBePositive(const QString &name)
{
    return name == QLatin1String("R") || name == QLatin1String("C")
           || name == QLatin1String("L") || name == QLatin1String("Is")
           || name == QLatin1String("N") || name == QLatin1String("Bf")
           || name == QLatin1String("Br");
}

// ---- ComponentDialog --------------------------------------------------------

ComponentDialog::ComponentDialog(const Component &c, const Schematic *sch, int index,
                                 QWidget *parent)
    : QDialog(parent), m_comp(c), m_sch(sch), m_index(index),
      m_name(0), m_active(0)
{
    const CompDef *def = compDef(c.type);
    setWindowTitle(def ? tr("%1 - properties").arg(QLatin1String(def->label))
                       : tr("Component properties"));

    QVBoxLayout *top = new QVBoxLayout(this);

    // ---- what this is -------------------------------------------------------
    QHBoxLayout *head = new QHBoxLayout;
    QLabel *icon = new QLabel;
    icon->setPixmap(compIcon(c.type, 36).pixmap(36, 36));
    head->addWidget(icon);
    QVBoxLayout *htext = new QVBoxLayout;
    QLabel *title = new QLabel(def ? QLatin1String(def->label) : c.type);
    QFont tf = title->font();
    tf.setBold(true);
    title->setFont(tf);
    htext->addWidget(title);
    htext->addWidget(new QLabel(tr("Type in the schematic file: %1").arg(c.type)));
    head->addLayout(htext);
    head->addStretch(1);
    top->addLayout(head);

    // ---- element ------------------------------------------------------------
    QGroupBox *elem = new QGroupBox(tr("Element"));
    QGridLayout *eg = new QGridLayout(elem);
    const bool grounded = def && (def->flags & CompIsGround);
    eg->addWidget(new QLabel(tr("Name")), 0, 0);
    m_name = new QLineEdit(c.name);
    // A ground carries no instance name: upstream writes `<GND * 1 x y 0 0>`,
    // and every ground on the sheet is node 0 whichever symbol it is.
    m_name->setEnabled(!grounded);
    if (grounded)
        m_name->setToolTip(tr("Ground is always \"*\" - every ground symbol on\n"
                              "the sheet is the same node."));
    eg->addWidget(m_name, 0, 1);
    m_active = new QCheckBox(tr("active (take it into the netlist)"));
    m_active->setChecked(c.active);
    m_active->setToolTip(tr("An inactive element is drawn gray and left out of the\n"
                            "netlist, which is upstream's way of trying a circuit\n"
                            "with one part lifted out."));
    eg->addWidget(m_active, 1, 0, 1, 2);
    top->addWidget(elem);

    // ---- properties ---------------------------------------------------------
    const int n = c.paramCount();
    if (n > 0) {
        QGroupBox *props = new QGroupBox(tr("Properties"));
        QGridLayout *pg = new QGridLayout(props);
        QLabel *h0 = new QLabel(tr("Property"));
        QLabel *h1 = new QLabel(tr("Value"));
        QLabel *h2 = new QLabel(tr("Show"));
        QFont hf = h0->font();
        hf.setBold(true);
        h0->setFont(hf);
        h1->setFont(hf);
        h2->setFont(hf);
        pg->addWidget(h0, 0, 0);
        pg->addWidget(h1, 0, 1);
        pg->addWidget(h2, 0, 2);

        for (int i = 0; i < n; i++) {
            const QString unit = c.paramUnit(i);
            QLabel *l = new QLabel(unit.isEmpty() ? c.paramName(i)
                                                  : c.paramName(i) + " [" + unit + "]");
            const QString desc = c.paramDesc(i);
            if (!desc.isEmpty())
                l->setToolTip(desc);
            pg->addWidget(l, i + 1, 0);
            pg->addWidget(paramRow(i), i + 1, 1);

            // Upstream's per-property "show on schematic", the flag that keeps
            // a transistor's model parameters off the sheet.
            QCheckBox *sh = new QCheckBox;
            sh->setChecked(i < m_comp.show.size() && m_comp.show.at(i));
            sh->setToolTip(tr("Draw this property next to the symbol"));
            m_show.append(sh);
            pg->addWidget(sh, i + 1, 2, Qt::AlignHCenter);
        }
        pg->setColumnStretch(1, 1);
        top->addWidget(props);
    }

    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok
                                                | QDialogButtonBox::Cancel);
    connect(bb, SIGNAL(accepted()), this, SLOT(accept()));
    connect(bb, SIGNAL(rejected()), this, SLOT(reject()));
    top->addWidget(bb);
}

QStringList ComponentDialog::sourceNames(bool independent) const
{
    QStringList out;
    if (!m_sch)
        return out;
    const QList<Component> &cs = m_sch->components();
    for (int i = 0; i < cs.size(); i++) {
        if (i == m_index)
            continue;
        const CompDef *def = compDef(cs.at(i).type);
        if (!def)
            continue;
        if (independent) {
            if (!isIndependentSource(cs.at(i).type))
                continue;
        } else if (!(def->flags & CompIsSource) || def->nports != 2) {
            // No branch current of its own, so there is nothing to control
            // from: the controlled sources are four-port, and a current source
            // is stamped without a row in the matrix.
            continue;
        }
        const QString n = cs.at(i).name;
        if (!n.isEmpty() && n != QLatin1String("*") && !out.contains(n))
            out.append(n);
    }
    return out;
}

QWidget *ComponentDialog::paramRow(int i)
{
    const QString pname = m_comp.paramName(i);
    const bool pickSource = (pname == QLatin1String("Source")
                             || pname == QLatin1String("Src"));
    const QStringList choices = m_comp.paramChoices(i);

    QLineEdit *edit = 0;
    QComboBox *combo = 0;

    if (pickSource || !choices.isEmpty()) {
        combo = new QComboBox;
        if (pickSource) {
            // The names of other elements, so the list comes off the sheet
            // rather than out of the catalogue - and stays editable, because a
            // file may name one this sheet no longer has.
            combo->setEditable(true);
            combo->addItems(sourceNames(pname == QLatin1String("Src")));
            if (pname == QLatin1String("Src"))
                combo->insertItem(0, QString());     // empty: one operating point
            combo->setToolTip(tr("The element on the schematic this refers to."));
            combo->setEditText(m_comp.param(i).trimmed());
        } else {
            combo->addItems(choices);
            const int at = combo->findText(m_comp.param(i).trimmed());
            combo->setCurrentIndex(at >= 0 ? at : 0);
        }
    } else {
        edit = new QLineEdit(m_comp.param(i));
        edit->setToolTip(tr("A number, with the SPICE suffixes allowed: 4k7, 100n,\n"
                            "10 Meg, 2.2pF."));
    }
    m_edit.append(edit);
    m_combo.append(combo);
    return edit ? (QWidget *)edit : (QWidget *)combo;
}

void ComponentDialog::accept()
{
    const CompDef *def = compDef(m_comp.type);

    // Gather first, so a refusal leaves the dialog showing what it checked.
    m_comp.active = m_active->isChecked();
    for (int i = 0; i < m_edit.size(); i++) {
        const QString v = m_edit.at(i) ? m_edit.at(i)->text().trimmed()
                                       : m_combo.at(i)->currentText().trimmed();
        m_comp.setParam(i, v);
        if (i < m_show.size())
            m_comp.show[i] = m_show.at(i)->isChecked();
    }

    const bool grounded = def && (def->flags & CompIsGround);
    if (grounded) {
        m_comp.name = QLatin1String("*");
    } else {
        m_comp.name = m_name->text().trimmed();
        if (m_comp.name.isEmpty()) {
            QMessageBox::warning(this, windowTitle(),
                    tr("The element needs a name: it is what the netlist calls\n"
                       "it and what a diagram plots."));
            return;
        }
        if (m_comp.name.contains(QLatin1Char(' '))) {
            QMessageBox::warning(this, windowTitle(),
                    tr("\"%1\" has a space in it, and the schematic file\n"
                       "separates its fields with spaces.").arg(m_comp.name));
            return;
        }
        for (int i = 0; m_sch && i < m_sch->components().size(); i++) {
            if (i == m_index)
                continue;
            if (m_sch->components().at(i).name != m_comp.name)
                continue;
            QMessageBox::warning(this, windowTitle(),
                    tr("\"%1\" is already used by another element on this sheet.")
                    .arg(m_comp.name));
            return;
        }
    }

    for (int i = 0; i < m_edit.size(); i++) {
        if (m_combo.at(i))
            continue;                       // a pick list cannot hold garbage
        const QString text = m_comp.param(i);
        const QString pname = m_comp.paramName(i);
        bool ok = false;
        const double v = str2num(text, &ok);
        if (!ok) {
            QMessageBox::warning(this, windowTitle(),
                    tr("%1: \"%2\" is not a number.").arg(pname).arg(text));
            return;
        }
        if (mustBePositive(pname) && !(v > 0.0)) {
            QMessageBox::warning(this, windowTitle(),
                    tr("%1 has to be greater than zero.").arg(pname));
            return;
        }
    }

    QDialog::accept();
}

// ---- DiagramDialog ----------------------------------------------------------

DiagramDialog::DiagramDialog(const Diagram &d, const Schematic *sch,
                             const SimResult *res, QWidget *parent)
    : QDialog(parent), m_diag(d), m_sch(sch), m_res(res),
      m_vars(0), m_choice(0), m_plot(0), m_logX(0), m_logY(0),
      m_width(0), m_height(0)
{
    setWindowTitle(tr("Diagram - properties"));

    QVBoxLayout *top = new QVBoxLayout(this);

    QLabel *hint = new QLabel(tr("What this diagram draws, in the order the trace "
                                 "colours are handed out:"));
    hint->setWordWrap(true);
    top->addWidget(hint);

    m_vars = new QListWidget;
    m_vars->addItems(d.vars);
    m_vars->setMinimumHeight(90);
    top->addWidget(m_vars);

    QHBoxLayout *row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Variable")));
    m_choice = new QComboBox;
    m_choice->setEditable(true);
    m_choice->addItems(variableChoices());
    row->addWidget(m_choice, 1);
    QPushButton *add = new QPushButton(tr("Add"));
    QPushButton *del = new QPushButton(tr("Remove"));
    row->addWidget(add);
    row->addWidget(del);
    top->addLayout(row);
    connect(add, SIGNAL(clicked()), this, SLOT(addVariable()));
    connect(del, SIGNAL(clicked()), this, SLOT(removeVariable()));
    connect(m_vars, SIGNAL(itemDoubleClicked(QListWidgetItem *)),
            this, SLOT(removeVariable()));

    // ---- axes ---------------------------------------------------------------
    QGroupBox *axes = new QGroupBox(tr("Axes"));
    QGridLayout *ag = new QGridLayout(axes);
    ag->addWidget(new QLabel(tr("Plot")), 0, 0);
    m_plot = new QComboBox;
    for (int p = (int)Diagram::Real; p <= (int)Diagram::Imag; p++)
        m_plot->addItem(Diagram::plotName(p), p);
    m_plot->setCurrentIndex(qBound(0, d.plot, (int)Diagram::Imag));
    m_plot->setToolTip(tr("What of a complex result is drawn: the value itself, its\n"
                          "magnitude, that in dB, its phase, or its imaginary part."));
    ag->addWidget(m_plot, 0, 1);
    m_logX = new QCheckBox(tr("logarithmic x axis"));
    m_logX->setChecked(d.logX);
    ag->addWidget(m_logX, 1, 0, 1, 2);
    m_logY = new QCheckBox(tr("logarithmic y axis"));
    m_logY->setChecked(d.logY);
    ag->addWidget(m_logY, 2, 0, 1, 2);
    ag->setColumnStretch(1, 1);
    top->addWidget(axes);

    // ---- size ---------------------------------------------------------------
    QGroupBox *size = new QGroupBox(tr("Size on the sheet"));
    QHBoxLayout *sg = new QHBoxLayout(size);
    sg->addWidget(new QLabel(tr("Width")));
    m_width = new QSpinBox;
    m_width->setRange(80, 1600);
    m_width->setSingleStep(10);
    m_width->setValue(qBound(80, d.size.width(), 1600));
    sg->addWidget(m_width);
    sg->addWidget(new QLabel(tr("Height")));
    m_height = new QSpinBox;
    m_height->setRange(60, 1200);
    m_height->setSingleStep(10);
    m_height->setValue(qBound(60, d.size.height(), 1200));
    sg->addWidget(m_height);
    sg->addStretch(1);
    top->addWidget(size);

    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok
                                                | QDialogButtonBox::Cancel);
    connect(bb, SIGNAL(accepted()), this, SLOT(accept()));
    connect(bb, SIGNAL(rejected()), this, SLOT(reject()));
    top->addWidget(bb);
}

QStringList DiagramDialog::variableChoices() const
{
    // What the last simulation actually produced is the list worth offering,
    // so it comes first.
    QStringList out;
    if (m_res)
        out = m_res->varNames();
    if (!m_sch)
        return out;

    // Before the first run there is no result, but the names are predictable:
    // a wire label names its net, and a source's branch current is "<name>.I".
    for (int i = 0; i < m_sch->wires().size(); i++) {
        const QString l = m_sch->wires().at(i).label.trimmed();
        if (!l.isEmpty() && !out.contains(l))
            out.append(l);
    }
    const QList<Component> &cs = m_sch->components();
    for (int i = 0; i < cs.size(); i++) {
        const CompDef *def = compDef(cs.at(i).type);
        if (!def || !(def->flags & CompIsSource))
            continue;
        const QString n = cs.at(i).name;
        if (n.isEmpty() || n == QLatin1String("*"))
            continue;
        const QString cur = n + QLatin1String(".I");
        if (!out.contains(cur))
            out.append(cur);
    }
    return out;
}

void DiagramDialog::addVariable()
{
    const QString v = m_choice->currentText().trimmed();
    if (v.isEmpty())
        return;
    for (int i = 0; i < m_vars->count(); i++) {
        if (m_vars->item(i)->text() != v)
            continue;
        m_vars->setCurrentRow(i);      // already plotted; show where
        return;
    }
    m_vars->addItem(v);
    m_vars->setCurrentRow(m_vars->count() - 1);
}

void DiagramDialog::removeVariable()
{
    const int row = m_vars->currentRow();
    if (row < 0)
        return;
    delete m_vars->takeItem(row);
}

void DiagramDialog::accept()
{
    m_diag.vars.clear();
    for (int i = 0; i < m_vars->count(); i++)
        m_diag.vars.append(m_vars->item(i)->text());
    m_diag.plot = m_plot->currentData().toInt();
    m_diag.logX = m_logX->isChecked();
    m_diag.logY = m_logY->isChecked();
    m_diag.size = QSize(m_width->value(), m_height->value());

    // An empty variable list is allowed: the diagram then draws its frame with
    // a hint, which is what a sheet looks like before its first simulation.
    QDialog::accept();
}
