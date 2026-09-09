/*
 * FreeCAD, ported to EwokOS - property editor implementation.
 */

#include "propertypanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

PropertyPanel::PropertyPanel(Document *doc, QWidget *parent)
    : QWidget(parent), m_doc(doc), m_obj(0), m_content(0), m_labelEdit(0)
{
    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    rebuild();
}

void PropertyPanel::setObject(DocObject *obj)
{
    if (m_obj == obj)
        return;
    m_obj = obj;
    rebuild();
}

// A spinbox carrying its property key on itself, so one slot serves every
// row - the panel is torn down and rebuilt per object, so the connections
// never outlive the object they edit.
static QDoubleSpinBox *makeSpin(QObject *receiver, const char *slot,
                                const QString &key, double value,
                                double min, double max)
{
    QDoubleSpinBox *spin = new QDoubleSpinBox;
    spin->setRange(min, max);
    spin->setDecimals(2);
    spin->setSingleStep(1.0);
    spin->setValue(value);
    spin->setProperty("key", key);
    spin->setKeyboardTracking(false);        // apply on Enter/step, not per digit
    QObject::connect(spin, SIGNAL(valueChanged(double)), receiver, slot);
    return spin;
}

QWidget *PropertyPanel::makeVecRow(const char *slot, const Vec3 &value,
                                   const char *keys[3], double min, double max)
{
    QWidget *row = new QWidget;
    QHBoxLayout *lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);
    double vals[3] = { value.x, value.y, value.z };
    for (int i = 0; i < 3; i++)
        lay->addWidget(makeSpin(this, slot, keys[i], vals[i], min, max));
    return row;
}

void PropertyPanel::rebuild()
{
    delete m_content;
    m_labelEdit = 0;

    m_content = new QWidget;
    QVBoxLayout *lay = new QVBoxLayout(m_content);
    lay->setContentsMargins(4, 4, 4, 4);

    if (!m_obj) {
        QLabel *hint = new QLabel(tr("No selection"));
        hint->setAlignment(Qt::AlignCenter);
        hint->setEnabled(false);
        lay->addWidget(hint);
        lay->addStretch(1);
        layout()->addWidget(m_content);
        return;
    }

    // ---- Base group: what upstream shows under "Base" + the view props.
    QGroupBox *base = new QGroupBox(tr("Base"));
    QFormLayout *form = new QFormLayout(base);
    form->setContentsMargins(6, 4, 6, 4);
    form->setSpacing(3);

    m_labelEdit = new QLineEdit(m_obj->label());
    connect(m_labelEdit, SIGNAL(editingFinished()), this, SLOT(onLabelEdited()));
    form->addRow(tr("Label"), m_labelEdit);

    static const char *posKeys[3] = { "x", "y", "z" };
    form->addRow(tr("Position"),
                 makeVecRow(SLOT(onPositionChanged(double)),
                            m_obj->placement.position, posKeys, -1e6, 1e6));
    form->addRow(tr("Angle"),
                 makeVecRow(SLOT(onAngleChanged(double)),
                            m_obj->placement.rotationDeg, posKeys, -360.0, 360.0));

    QCheckBox *visible = new QCheckBox;
    visible->setChecked(m_obj->visible);
    connect(visible, SIGNAL(toggled(bool)), this, SLOT(onVisibleToggled(bool)));
    form->addRow(tr("Visibility"), visible);

    QPushButton *colorBtn = new QPushButton;
    colorBtn->setText(m_obj->color.name());
    QPixmap pm(12, 12);
    pm.fill(m_obj->color);
    colorBtn->setIcon(QIcon(pm));
    connect(colorBtn, SIGNAL(clicked()), this, SLOT(onColorClicked()));
    form->addRow(tr("Shape Color"), colorBtn);

    lay->addWidget(base);

    // ---- shape group: the Part parameters for this primitive type.
    QGroupBox *shape = new QGroupBox(DocObject::typeName(m_obj->type()));
    QFormLayout *sform = new QFormLayout(shape);
    sform->setContentsMargins(6, 4, 6, 4);
    sform->setSpacing(3);
    QStringList names = m_obj->paramNames();
    for (int i = 0; i < names.size(); i++)
        sform->addRow(names.at(i),
                      makeSpin(this, SLOT(onParamChanged(double)),
                               names.at(i), m_obj->param(names.at(i)), 0.0, 1e6));
    lay->addWidget(shape);

    lay->addStretch(1);
    layout()->addWidget(m_content);
}

void PropertyPanel::onParamChanged(double v)
{
    if (!m_obj)
        return;
    m_obj->setParam(sender()->property("key").toString(), v);
    m_doc->touch();
}

void PropertyPanel::onPositionChanged(double v)
{
    if (!m_obj)
        return;
    QString key = sender()->property("key").toString();
    if (key == "x") m_obj->placement.position.x = v;
    else if (key == "y") m_obj->placement.position.y = v;
    else m_obj->placement.position.z = v;
    m_doc->touch();
}

void PropertyPanel::onAngleChanged(double v)
{
    if (!m_obj)
        return;
    QString key = sender()->property("key").toString();
    if (key == "x") m_obj->placement.rotationDeg.x = v;
    else if (key == "y") m_obj->placement.rotationDeg.y = v;
    else m_obj->placement.rotationDeg.z = v;
    m_doc->touch();
}

void PropertyPanel::onLabelEdited()
{
    if (!m_obj || !m_labelEdit)
        return;
    QString label = m_labelEdit->text().trimmed();
    if (label.isEmpty() || label == m_obj->label())
        return;
    m_obj->setLabel(label);
    m_doc->touch();
    emit labelChanged(m_obj);
}

void PropertyPanel::onVisibleToggled(bool on)
{
    if (!m_obj)
        return;
    m_obj->visible = on;
    m_doc->touch();
}

void PropertyPanel::onColorClicked()
{
    if (!m_obj)
        return;
    QColor c = QColorDialog::getColor(m_obj->color, this, tr("Shape Color"));
    if (!c.isValid())
        return;
    m_obj->color = c;
    m_doc->touch();
    rebuild();                               // refresh the button swatch
}
