/****************************************************************************
** Meta object code from reading C++ file 'component.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.19)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../circuit/component.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'component.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.19. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_Component_t {
    QByteArrayData data[26];
    char stringdata0[201];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Component_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Component_t qt_meta_stringdata_Component = {
    {
QT_MOC_LITERAL(0, 0, 9), // "Component"
QT_MOC_LITERAL(1, 10, 5), // "moved"
QT_MOC_LITERAL(2, 16, 0), // ""
QT_MOC_LITERAL(3, 17, 14), // "slotProperties"
QT_MOC_LITERAL(4, 32, 8), // "slotCopy"
QT_MOC_LITERAL(5, 41, 8), // "rotateCW"
QT_MOC_LITERAL(6, 50, 9), // "rotateCCW"
QT_MOC_LITERAL(7, 60, 10), // "rotateHalf"
QT_MOC_LITERAL(8, 71, 6), // "H_flip"
QT_MOC_LITERAL(9, 78, 6), // "V_flip"
QT_MOC_LITERAL(10, 85, 10), // "slotRemove"
QT_MOC_LITERAL(11, 96, 6), // "remove"
QT_MOC_LITERAL(12, 103, 8), // "itemtype"
QT_MOC_LITERAL(13, 112, 2), // "id"
QT_MOC_LITERAL(14, 115, 7), // "Show_id"
QT_MOC_LITERAL(15, 123, 1), // "x"
QT_MOC_LITERAL(16, 125, 1), // "y"
QT_MOC_LITERAL(17, 127, 8), // "rotation"
QT_MOC_LITERAL(18, 136, 5), // "hflip"
QT_MOC_LITERAL(19, 142, 5), // "vflip"
QT_MOC_LITERAL(20, 148, 6), // "labelx"
QT_MOC_LITERAL(21, 155, 6), // "labely"
QT_MOC_LITERAL(22, 162, 8), // "labelrot"
QT_MOC_LITERAL(23, 171, 9), // "valLabelx"
QT_MOC_LITERAL(24, 181, 9), // "valLabely"
QT_MOC_LITERAL(25, 191, 9) // "valLabRot"

    },
    "Component\0moved\0\0slotProperties\0"
    "slotCopy\0rotateCW\0rotateCCW\0rotateHalf\0"
    "H_flip\0V_flip\0slotRemove\0remove\0"
    "itemtype\0id\0Show_id\0x\0y\0rotation\0hflip\0"
    "vflip\0labelx\0labely\0labelrot\0valLabelx\0"
    "valLabely\0valLabRot"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Component[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      10,   14, // methods
      14,   74, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   64,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       3,    0,   65,    2, 0x0a /* Public */,
       4,    0,   66,    2, 0x0a /* Public */,
       5,    0,   67,    2, 0x0a /* Public */,
       6,    0,   68,    2, 0x0a /* Public */,
       7,    0,   69,    2, 0x0a /* Public */,
       8,    0,   70,    2, 0x0a /* Public */,
       9,    0,   71,    2, 0x0a /* Public */,
      10,    0,   72,    2, 0x0a /* Public */,
      11,    0,   73,    2, 0x0a /* Public */,

 // signals: parameters
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

 // properties: name, type, flags
      12, QMetaType::QString, 0x00195001,
      13, QMetaType::QString, 0x00195003,
      14, QMetaType::Bool, 0x00195003,
      15, QMetaType::Int, 0x00095003,
      16, QMetaType::Int, 0x00095003,
      17, QMetaType::QReal, 0x00095103,
      18, QMetaType::Int, 0x00095103,
      19, QMetaType::Int, 0x00095103,
      20, QMetaType::Int, 0x00095003,
      21, QMetaType::Int, 0x00095003,
      22, QMetaType::Int, 0x00095003,
      23, QMetaType::Int, 0x00095003,
      24, QMetaType::Int, 0x00095003,
      25, QMetaType::Int, 0x00095103,

       0        // eod
};

void Component::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<Component *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->moved(); break;
        case 1: _t->slotProperties(); break;
        case 2: _t->slotCopy(); break;
        case 3: _t->rotateCW(); break;
        case 4: _t->rotateCCW(); break;
        case 5: _t->rotateHalf(); break;
        case 6: _t->H_flip(); break;
        case 7: _t->V_flip(); break;
        case 8: _t->slotRemove(); break;
        case 9: _t->remove(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (Component::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Component::moved)) {
                *result = 0;
                return;
            }
        }
    }
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<Component *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< QString*>(_v) = _t->itemType(); break;
        case 1: *reinterpret_cast< QString*>(_v) = _t->idLabel(); break;
        case 2: *reinterpret_cast< bool*>(_v) = _t->showId(); break;
        case 3: *reinterpret_cast< int*>(_v) = _t->gridX(); break;
        case 4: *reinterpret_cast< int*>(_v) = _t->gridY(); break;
        case 5: *reinterpret_cast< qreal*>(_v) = _t->rotation(); break;
        case 6: *reinterpret_cast< int*>(_v) = _t->hflip(); break;
        case 7: *reinterpret_cast< int*>(_v) = _t->vflip(); break;
        case 8: *reinterpret_cast< int*>(_v) = _t->labelx(); break;
        case 9: *reinterpret_cast< int*>(_v) = _t->labely(); break;
        case 10: *reinterpret_cast< int*>(_v) = _t->labelRot(); break;
        case 11: *reinterpret_cast< int*>(_v) = _t->valLabelx(); break;
        case 12: *reinterpret_cast< int*>(_v) = _t->valLabely(); break;
        case 13: *reinterpret_cast< int*>(_v) = _t->valLabRot(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<Component *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 1: _t->setIdLabel(*reinterpret_cast< QString*>(_v)); break;
        case 2: _t->setShowId(*reinterpret_cast< bool*>(_v)); break;
        case 3: _t->setGridX(*reinterpret_cast< int*>(_v)); break;
        case 4: _t->setGridY(*reinterpret_cast< int*>(_v)); break;
        case 5: _t->setRotation(*reinterpret_cast< qreal*>(_v)); break;
        case 6: _t->setHflip(*reinterpret_cast< int*>(_v)); break;
        case 7: _t->setVflip(*reinterpret_cast< int*>(_v)); break;
        case 8: _t->setLabelX(*reinterpret_cast< int*>(_v)); break;
        case 9: _t->setLabelY(*reinterpret_cast< int*>(_v)); break;
        case 10: _t->setLabelRot(*reinterpret_cast< int*>(_v)); break;
        case 11: _t->setValLabelX(*reinterpret_cast< int*>(_v)); break;
        case 12: _t->setValLabelY(*reinterpret_cast< int*>(_v)); break;
        case 13: _t->setValLabRot(*reinterpret_cast< int*>(_v)); break;
        default: break;
        }
    } else if (_c == QMetaObject::ResetProperty) {
    }
#endif // QT_NO_PROPERTIES
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject Component::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_Component.data,
    qt_meta_data_Component,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Component::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Component::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Component.stringdata0))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "QGraphicsItem"))
        return static_cast< QGraphicsItem*>(this);
    if (!strcmp(_clname, "eElement"))
        return static_cast< eElement*>(this);
    if (!strcmp(_clname, "org.qt-project.Qt.QGraphicsItem"))
        return static_cast< QGraphicsItem*>(this);
    return QObject::qt_metacast(_clname);
}

int Component::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 10)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 10;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 10)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 10;
    }
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    } else if (_c == QMetaObject::QueryPropertyDesignable) {
        _id -= 14;
    } else if (_c == QMetaObject::QueryPropertyScriptable) {
        _id -= 14;
    } else if (_c == QMetaObject::QueryPropertyStored) {
        _id -= 14;
    } else if (_c == QMetaObject::QueryPropertyEditable) {
        _id -= 14;
    } else if (_c == QMetaObject::QueryPropertyUser) {
        _id -= 14;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}

// SIGNAL 0
void Component::moved()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
