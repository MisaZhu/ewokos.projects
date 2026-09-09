/*
 * QtMeshEditor, ported to EwokOS - the entity inspector.
 *
 * Upstream splits this across a TransformOperator (position/rotation/scale
 * gizmo + spinboxes), a MaterialEditor (diffuse/normal/metalness...) and a
 * mesh-info readout.  This Qt build has no Ogre materials, so the inspector
 * collapses them into one docked form: a node-transform group with
 * position/rotation/scale spinboxes, a material group reduced to the diffuse
 * colour (the only property a flat-shaded software rasteriser can honour), a
 * visibility toggle, and a read-only geometry summary.  Every edit touches
 * the scene so the viewport repaints live, matching upstream's feel.
 */

#ifndef QME_INSPECTOR_H
#define QME_INSPECTOR_H

#include <QWidget>

#include "scene.h"

class QLineEdit;
class QLabel;

class Inspector : public QWidget {
    Q_OBJECT
public:
    explicit Inspector(Scene *scene, QWidget *parent = 0);

    void setEntity(MeshEntity *ent);         // 0 clears the panel

signals:
    void nameChanged(MeshEntity *ent);       // tree item text needs updating

private slots:
    void onPositionChanged(double v);
    void onRotationChanged(double v);
    void onScaleChanged(double v);
    void onNameEdited();
    void onVisibleToggled(bool on);
    void onDiffuseClicked();
    void onResetTransform();

private:
    QWidget *makeVecRow(const char *slot, const Vec3 &value, double min, double max,
                        double step, int decimals);
    void rebuild();

    Scene *m_scene;
    MeshEntity *m_ent;
    QWidget *m_content;
    QLineEdit *m_nameEdit;
};

#endif // QME_INSPECTOR_H
