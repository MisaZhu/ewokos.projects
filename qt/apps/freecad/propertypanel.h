/*
 * FreeCAD, ported to EwokOS - the property editor.
 *
 * Upstream's Gui::PropertyEditor is a generic view over the dynamic App
 * property system; the objects here have a fixed set of properties, so
 * this is a hand-built form in the same visual order: a "Base" group with
 * Label, Placement and visibility/color view-properties, then a group per
 * shape type with the Part parameters.  Every edit re-tessellates and
 * repaints immediately, which is what parametric modelling feels like.
 */

#ifndef FREECAD_PROPERTYPANEL_H
#define FREECAD_PROPERTYPANEL_H

#include <QWidget>

#include "document.h"

class QLineEdit;

class PropertyPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertyPanel(Document *doc, QWidget *parent = 0);

    void setObject(DocObject *obj);          // 0 clears the panel

signals:
    void labelChanged(DocObject *obj);       // tree item text needs updating

private slots:
    void onParamChanged(double v);
    void onPositionChanged(double v);
    void onAngleChanged(double v);
    void onLabelEdited();
    void onVisibleToggled(bool on);
    void onColorClicked();

private:
    QWidget *makeVecRow(const char *slot, const Vec3 &value, const char *keys[3],
                        double min, double max);
    void rebuild();

    Document *m_doc;
    DocObject *m_obj;
    QWidget *m_content;
    QLineEdit *m_labelEdit;
};

#endif // FREECAD_PROPERTYPANEL_H
