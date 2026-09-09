/*
 * QtMeshEditor, ported to EwokOS - inspector implementation.
 */

#include "inspector.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

Inspector::Inspector(Scene *scene, QWidget *parent)
    : QWidget(parent), m_scene(scene), m_ent(0), m_content(0), m_nameEdit(0)
{
    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    rebuild();
}

void Inspector::setEntity(MeshEntity *ent)
{
    if (m_ent == ent)
        return;
    m_ent = ent;
    rebuild();
}

// A spinbox carrying its axis key on itself, so one slot per vector serves
// all three rows - the panel is torn down and rebuilt per entity, so the
// connections never outlive the entity they edit.
static QDoubleSpinBox *makeSpin(QObject *receiver, const char *slot,
                                const char *key, double value,
                                double min, double max, double step, int decimals)
{
    QDoubleSpinBox *spin = new QDoubleSpinBox;
    spin->setRange(min, max);
    spin->setDecimals(decimals);
    spin->setSingleStep(step);
    spin->setValue(value);
    spin->setProperty("key", key);
    spin->setKeyboardTracking(false);        // apply on Enter/step, not per digit
    QObject::connect(spin, SIGNAL(valueChanged(double)), receiver, slot);
    return spin;
}

QWidget *Inspector::makeVecRow(const char *slot, const Vec3 &value, double min,
                               double max, double step, int decimals)
{
    QWidget *row = new QWidget;
    QHBoxLayout *lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);
    double vals[3] = { value.x, value.y, value.z };
    static const char *keys[3] = { "x", "y", "z" };
    for (int i = 0; i < 3; i++)
        lay->addWidget(makeSpin(this, slot, keys[i], vals[i], min, max, step, decimals));
    return row;
}

void Inspector::rebuild()
{
    delete m_content;
    m_nameEdit = 0;

    m_content = new QWidget;
    QVBoxLayout *lay = new QVBoxLayout(m_content);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(6);

    if (!m_ent) {
        QLabel *hint = new QLabel(tr("No selection"));
        hint->setAlignment(Qt::AlignCenter);
        hint->setEnabled(false);
        lay->addWidget(hint);
        lay->addStretch(1);
        layout()->addWidget(m_content);
        return;
    }

    // ---- Node group: name + visibility + the node transform.
    QGroupBox *node = new QGroupBox(tr("Node"));
    QFormLayout *form = new QFormLayout(node);
    form->setContentsMargins(6, 4, 6, 4);
    form->setSpacing(3);

    m_nameEdit = new QLineEdit(m_ent->name);
    connect(m_nameEdit, SIGNAL(editingFinished()), this, SLOT(onNameEdited()));
    form->addRow(tr("Name"), m_nameEdit);

    QCheckBox *visible = new QCheckBox;
    visible->setChecked(m_ent->visible);
    connect(visible, SIGNAL(toggled(bool)), this, SLOT(onVisibleToggled(bool)));
    form->addRow(tr("Visible"), visible);

    lay->addWidget(node);

    // ---- Transform group: what upstream's TransformOperator edits.
    QGroupBox *xform = new QGroupBox(tr("Transform"));
    QFormLayout *xf = new QFormLayout(xform);
    xf->setContentsMargins(6, 4, 6, 4);
    xf->setSpacing(3);
    xf->addRow(tr("Position"),
               makeVecRow(SLOT(onPositionChanged(double)), m_ent->position, -1e6, 1e6, 0.1, 3));
    xf->addRow(tr("Rotation"),
               makeVecRow(SLOT(onRotationChanged(double)), m_ent->rotationDeg, -360, 360, 1.0, 1));
    xf->addRow(tr("Scale"),
               makeVecRow(SLOT(onScaleChanged(double)), m_ent->scale, 0.001, 1000, 0.1, 3));

    QPushButton *reset = new QPushButton(tr("Reset Transform"));
    connect(reset, SIGNAL(clicked()), this, SLOT(onResetTransform()));
    xf->addRow(QString(), reset);
    lay->addWidget(xform);

    // ---- Material group: the diffuse colour a flat shader can honour.
    QGroupBox *mat = new QGroupBox(tr("Material"));
    QFormLayout *mf = new QFormLayout(mat);
    mf->setContentsMargins(6, 4, 6, 4);
    mf->setSpacing(3);

    QPushButton *colorBtn = new QPushButton;
    colorBtn->setText(m_ent->diffuse.name());
    QPixmap pm(12, 12);
    pm.fill(m_ent->diffuse);
    colorBtn->setIcon(QIcon(pm));
    connect(colorBtn, SIGNAL(clicked()), this, SLOT(onDiffuseClicked()));
    mf->addRow(tr("Diffuse"), colorBtn);
    lay->addWidget(mat);

    // ---- Geometry summary (read-only), like upstream's mesh info.
    QGroupBox *geo = new QGroupBox(tr("Geometry"));
    QFormLayout *gf = new QFormLayout(geo);
    gf->setContentsMargins(6, 4, 6, 4);
    gf->setSpacing(3);
    Vec3 lo, hi;
    m_ent->mesh.bounds(&lo, &hi);
    gf->addRow(tr("Vertices"), new QLabel(QString::number(m_ent->mesh.verts.size())));
    gf->addRow(tr("Triangles"), new QLabel(QString::number(m_ent->mesh.tris.size())));
    gf->addRow(tr("Local bounds"),
               new QLabel(QString("%1 x %2 x %3")
                          .arg(hi.x - lo.x, 0, 'g', 4)
                          .arg(hi.y - lo.y, 0, 'g', 4)
                          .arg(hi.z - lo.z, 0, 'g', 4)));
    if (!m_ent->sourcePath.isEmpty())
        gf->addRow(tr("Source"), new QLabel(m_ent->sourcePath));
    lay->addWidget(geo);

    lay->addStretch(1);
    layout()->addWidget(m_content);
}

void Inspector::onPositionChanged(double v)
{
    if (!m_ent)
        return;
    QString key = sender()->property("key").toString();
    if (key == "x") m_ent->position.x = v;
    else if (key == "y") m_ent->position.y = v;
    else m_ent->position.z = v;
    m_scene->touch();
}

void Inspector::onRotationChanged(double v)
{
    if (!m_ent)
        return;
    QString key = sender()->property("key").toString();
    if (key == "x") m_ent->rotationDeg.x = v;
    else if (key == "y") m_ent->rotationDeg.y = v;
    else m_ent->rotationDeg.z = v;
    m_scene->touch();
}

void Inspector::onScaleChanged(double v)
{
    if (!m_ent)
        return;
    QString key = sender()->property("key").toString();
    if (key == "x") m_ent->scale.x = v;
    else if (key == "y") m_ent->scale.y = v;
    else m_ent->scale.z = v;
    m_scene->touch();
}

void Inspector::onNameEdited()
{
    if (!m_ent || !m_nameEdit)
        return;
    QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty() || name == m_ent->name)
        return;
    m_ent->name = name;
    emit nameChanged(m_ent);
}

void Inspector::onVisibleToggled(bool on)
{
    if (!m_ent)
        return;
    m_ent->visible = on;
    m_scene->touch();
}

void Inspector::onDiffuseClicked()
{
    if (!m_ent)
        return;
    QColor c = QColorDialog::getColor(m_ent->diffuse, this, tr("Diffuse Colour"));
    if (!c.isValid())
        return;
    m_ent->diffuse = c;
    m_scene->touch();
    rebuild();                               // refresh the button swatch
}

void Inspector::onResetTransform()
{
    if (!m_ent)
        return;
    m_ent->position = Vec3(0, 0, 0);
    m_ent->rotationDeg = Vec3(0, 0, 0);
    m_ent->scale = Vec3(1, 1, 1);
    m_scene->touch();
    rebuild();                               // refresh the spinbox values
}
