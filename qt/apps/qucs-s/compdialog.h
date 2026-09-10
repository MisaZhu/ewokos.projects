/*
 * Qucs-S, ported to EwokOS - the property dialogs.
 *
 * Upstream: https://github.com/ra3xdh/qucs_s (GPL-2.0), which edits a
 * component's properties in place on the sheet - qucs/mouseactions.cpp puts a
 * QLineEdit over the property text under the cursor (Element::activate) and
 * qucs/components/component.cpp holds the list - and keeps the diagram's
 * settings in qucs/diagrams/diagram.cpp behind a right-click menu.
 *
 * Here both are dialogs.  Inline editing needs a focus proxy and a style that
 * this Qt build's platform plugin does not really offer, and a dialog says
 * what a property is: upstream's registerProperties() descriptions are shown
 * next to the field, which the sheet has no room for.  What is kept is the
 * content - name, the per-property "show on schematic" flag of upstream's
 * Element::showPropList, the active flag, and for a diagram its variables,
 * its plot mode and its logarithmic axes.
 */

#ifndef QUCS_COMPDIALOG_H
#define QUCS_COMPDIALOG_H

#include <QDialog>
#include <QList>

#include "components.h"
#include "schematic.h"
#include "simulator.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QSpinBox;

// ---- one component ----------------------------------------------------------

class ComponentDialog : public QDialog {
    Q_OBJECT
public:
    // `sch` and `index` are the component's place on the sheet, used to keep
    // instance names apart and to offer the sources a controlled source or a
    // DC sweep can be told to look at.
    ComponentDialog(const Component &c, const Schematic *sch, int index,
                    QWidget *parent = 0);

    // The edited copy, valid once exec() returned QDialog::Accepted.
    Component result() const { return m_comp; }

public slots:
    // Overridden so the OK button runs the validation first.
    void accept();

private:
    // The element names a property may refer to.  A DC sweep names one of the
    // independent sources; a current controlled source names anything with a
    // branch current of its own.
    QStringList sourceNames(bool independent) const;
    // The value editor of property i, registered in m_edit/m_combo.
    QWidget *paramRow(int i);

    Component m_comp;
    const Schematic *m_sch;
    int m_index;

    QLineEdit *m_name;
    QCheckBox *m_active;
    QList<QLineEdit *> m_edit;
    QList<QComboBox *> m_combo;      // parallel to m_edit, 0 where there is none
    QList<QCheckBox *> m_show;
};

// ---- one diagram ------------------------------------------------------------

class DiagramDialog : public QDialog {
    Q_OBJECT
public:
    // `res` is the last simulation, borrowed: its variable names are what the
    // diagram can plot, and it may be 0 before the first run.
    DiagramDialog(const Diagram &d, const Schematic *sch, const SimResult *res,
                  QWidget *parent = 0);

    Diagram result() const { return m_diag; }

public slots:
    void accept();

private slots:
    void addVariable();
    void removeVariable();

private:
    QStringList variableChoices() const;

    Diagram m_diag;
    const Schematic *m_sch;
    const SimResult *m_res;

    QListWidget *m_vars;
    QComboBox *m_choice;
    QComboBox *m_plot;
    QCheckBox *m_logX;
    QCheckBox *m_logY;
    QSpinBox *m_width;
    QSpinBox *m_height;
};

#endif // QUCS_COMPDIALOG_H
