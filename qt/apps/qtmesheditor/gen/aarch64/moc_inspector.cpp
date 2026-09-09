/****************************************************************************
** Meta object code from reading C++ file 'inspector.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.19)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../inspector.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'inspector.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.19. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_Inspector_t {
    QByteArrayData data[14];
    char stringdata0[159];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Inspector_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Inspector_t qt_meta_stringdata_Inspector = {
    {
QT_MOC_LITERAL(0, 0, 9), // "Inspector"
QT_MOC_LITERAL(1, 10, 11), // "nameChanged"
QT_MOC_LITERAL(2, 22, 0), // ""
QT_MOC_LITERAL(3, 23, 11), // "MeshEntity*"
QT_MOC_LITERAL(4, 35, 3), // "ent"
QT_MOC_LITERAL(5, 39, 17), // "onPositionChanged"
QT_MOC_LITERAL(6, 57, 1), // "v"
QT_MOC_LITERAL(7, 59, 17), // "onRotationChanged"
QT_MOC_LITERAL(8, 77, 14), // "onScaleChanged"
QT_MOC_LITERAL(9, 92, 12), // "onNameEdited"
QT_MOC_LITERAL(10, 105, 16), // "onVisibleToggled"
QT_MOC_LITERAL(11, 122, 2), // "on"
QT_MOC_LITERAL(12, 125, 16), // "onDiffuseClicked"
QT_MOC_LITERAL(13, 142, 16) // "onResetTransform"

    },
    "Inspector\0nameChanged\0\0MeshEntity*\0"
    "ent\0onPositionChanged\0v\0onRotationChanged\0"
    "onScaleChanged\0onNameEdited\0"
    "onVisibleToggled\0on\0onDiffuseClicked\0"
    "onResetTransform"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Inspector[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       8,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   54,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       5,    1,   57,    2, 0x08 /* Private */,
       7,    1,   60,    2, 0x08 /* Private */,
       8,    1,   63,    2, 0x08 /* Private */,
       9,    0,   66,    2, 0x08 /* Private */,
      10,    1,   67,    2, 0x08 /* Private */,
      12,    0,   70,    2, 0x08 /* Private */,
      13,    0,   71,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,

 // slots: parameters
    QMetaType::Void, QMetaType::Double,    6,
    QMetaType::Void, QMetaType::Double,    6,
    QMetaType::Void, QMetaType::Double,    6,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,   11,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void Inspector::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<Inspector *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->nameChanged((*reinterpret_cast< MeshEntity*(*)>(_a[1]))); break;
        case 1: _t->onPositionChanged((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 2: _t->onRotationChanged((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 3: _t->onScaleChanged((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 4: _t->onNameEdited(); break;
        case 5: _t->onVisibleToggled((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 6: _t->onDiffuseClicked(); break;
        case 7: _t->onResetTransform(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (Inspector::*)(MeshEntity * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Inspector::nameChanged)) {
                *result = 0;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject Inspector::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_Inspector.data,
    qt_meta_data_Inspector,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Inspector::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Inspector::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Inspector.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int Inspector::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 8)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 8)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 8;
    }
    return _id;
}

// SIGNAL 0
void Inspector::nameChanged(MeshEntity * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
