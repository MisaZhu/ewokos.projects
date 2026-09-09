/****************************************************************************
** Meta object code from reading C++ file 'propertypanel.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.19)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../propertypanel.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'propertypanel.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.19. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_PropertyPanel_t {
    QByteArrayData data[13];
    char stringdata0[142];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_PropertyPanel_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_PropertyPanel_t qt_meta_stringdata_PropertyPanel = {
    {
QT_MOC_LITERAL(0, 0, 13), // "PropertyPanel"
QT_MOC_LITERAL(1, 14, 12), // "labelChanged"
QT_MOC_LITERAL(2, 27, 0), // ""
QT_MOC_LITERAL(3, 28, 10), // "DocObject*"
QT_MOC_LITERAL(4, 39, 3), // "obj"
QT_MOC_LITERAL(5, 43, 14), // "onParamChanged"
QT_MOC_LITERAL(6, 58, 1), // "v"
QT_MOC_LITERAL(7, 60, 17), // "onPositionChanged"
QT_MOC_LITERAL(8, 78, 14), // "onAngleChanged"
QT_MOC_LITERAL(9, 93, 13), // "onLabelEdited"
QT_MOC_LITERAL(10, 107, 16), // "onVisibleToggled"
QT_MOC_LITERAL(11, 124, 2), // "on"
QT_MOC_LITERAL(12, 127, 14) // "onColorClicked"

    },
    "PropertyPanel\0labelChanged\0\0DocObject*\0"
    "obj\0onParamChanged\0v\0onPositionChanged\0"
    "onAngleChanged\0onLabelEdited\0"
    "onVisibleToggled\0on\0onColorClicked"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_PropertyPanel[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   49,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       5,    1,   52,    2, 0x08 /* Private */,
       7,    1,   55,    2, 0x08 /* Private */,
       8,    1,   58,    2, 0x08 /* Private */,
       9,    0,   61,    2, 0x08 /* Private */,
      10,    1,   62,    2, 0x08 /* Private */,
      12,    0,   65,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,

 // slots: parameters
    QMetaType::Void, QMetaType::Double,    6,
    QMetaType::Void, QMetaType::Double,    6,
    QMetaType::Void, QMetaType::Double,    6,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,   11,
    QMetaType::Void,

       0        // eod
};

void PropertyPanel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<PropertyPanel *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->labelChanged((*reinterpret_cast< DocObject*(*)>(_a[1]))); break;
        case 1: _t->onParamChanged((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 2: _t->onPositionChanged((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 3: _t->onAngleChanged((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 4: _t->onLabelEdited(); break;
        case 5: _t->onVisibleToggled((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 6: _t->onColorClicked(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (PropertyPanel::*)(DocObject * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&PropertyPanel::labelChanged)) {
                *result = 0;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject PropertyPanel::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_PropertyPanel.data,
    qt_meta_data_PropertyPanel,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *PropertyPanel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *PropertyPanel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_PropertyPanel.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int PropertyPanel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void PropertyPanel::labelChanged(DocObject * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
