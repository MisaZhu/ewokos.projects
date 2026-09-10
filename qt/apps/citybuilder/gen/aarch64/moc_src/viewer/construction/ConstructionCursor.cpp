/****************************************************************************
** Meta object code from reading C++ file 'ConstructionCursor.hpp'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.19)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../src/viewer/construction/ConstructionCursor.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#include <QtCore/QList>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'ConstructionCursor.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.19. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_ConstructionCursor_t {
    QByteArrayData data[10];
    char stringdata0[123];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_ConstructionCursor_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_ConstructionCursor_t qt_meta_stringdata_ConstructionCursor = {
    {
QT_MOC_LITERAL(0, 0, 18), // "ConstructionCursor"
QT_MOC_LITERAL(1, 19, 6), // "cancel"
QT_MOC_LITERAL(2, 26, 0), // ""
QT_MOC_LITERAL(3, 27, 9), // "construct"
QT_MOC_LITERAL(4, 37, 19), // "BuildingInformation"
QT_MOC_LITERAL(5, 57, 12), // "buildingConf"
QT_MOC_LITERAL(6, 70, 22), // "QList<TileCoordinates>"
QT_MOC_LITERAL(7, 93, 9), // "locations"
QT_MOC_LITERAL(8, 103, 9), // "Direction"
QT_MOC_LITERAL(9, 113, 9) // "direction"

    },
    "ConstructionCursor\0cancel\0\0construct\0"
    "BuildingInformation\0buildingConf\0"
    "QList<TileCoordinates>\0locations\0"
    "Direction\0direction"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_ConstructionCursor[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       2,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   24,    2, 0x06 /* Public */,
       3,    3,   25,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 4, 0x80000000 | 6, 0x80000000 | 8,    5,    7,    9,

       0        // eod
};

void ConstructionCursor::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ConstructionCursor *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->cancel(); break;
        case 1: _t->construct((*reinterpret_cast< const BuildingInformation(*)>(_a[1])),(*reinterpret_cast< QList<TileCoordinates>(*)>(_a[2])),(*reinterpret_cast< Direction(*)>(_a[3]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (ConstructionCursor::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ConstructionCursor::cancel)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (ConstructionCursor::*)(const BuildingInformation & , QList<TileCoordinates> , Direction );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ConstructionCursor::construct)) {
                *result = 1;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject ConstructionCursor::staticMetaObject = { {
    QMetaObject::SuperData::link<QGraphicsObject::staticMetaObject>(),
    qt_meta_stringdata_ConstructionCursor.data,
    qt_meta_data_ConstructionCursor,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *ConstructionCursor::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ConstructionCursor::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ConstructionCursor.stringdata0))
        return static_cast<void*>(this);
    return QGraphicsObject::qt_metacast(_clname);
}

int ConstructionCursor::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QGraphicsObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 2)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 2)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 2;
    }
    return _id;
}

// SIGNAL 0
void ConstructionCursor::cancel()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void ConstructionCursor::construct(const BuildingInformation & _t1, QList<TileCoordinates> _t2, Direction _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
