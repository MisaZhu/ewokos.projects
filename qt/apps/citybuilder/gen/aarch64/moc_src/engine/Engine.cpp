/****************************************************************************
** Meta object code from reading C++ file 'Engine.hpp'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.19)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../src/engine/Engine.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'Engine.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.19. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_Engine_t {
    QByteArrayData data[16];
    char stringdata0[196];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Engine_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Engine_t qt_meta_stringdata_Engine = {
    {
QT_MOC_LITERAL(0, 0, 6), // "Engine"
QT_MOC_LITERAL(1, 7, 12), // "stateUpdated"
QT_MOC_LITERAL(2, 20, 0), // ""
QT_MOC_LITERAL(3, 21, 5), // "State"
QT_MOC_LITERAL(4, 27, 5), // "pause"
QT_MOC_LITERAL(5, 33, 22), // "setProcessorSpeedRatio"
QT_MOC_LITERAL(6, 56, 10), // "speedRatio"
QT_MOC_LITERAL(7, 67, 22), // "getProcessorSpeedRatio"
QT_MOC_LITERAL(8, 90, 16), // "forceNextProcess"
QT_MOC_LITERAL(9, 107, 14), // "createBuilding"
QT_MOC_LITERAL(10, 122, 19), // "BuildingInformation"
QT_MOC_LITERAL(11, 142, 4), // "type"
QT_MOC_LITERAL(12, 147, 15), // "TileCoordinates"
QT_MOC_LITERAL(13, 163, 10), // "leftCorner"
QT_MOC_LITERAL(14, 174, 9), // "Direction"
QT_MOC_LITERAL(15, 184, 11) // "orientation"

    },
    "Engine\0stateUpdated\0\0State\0pause\0"
    "setProcessorSpeedRatio\0speedRatio\0"
    "getProcessorSpeedRatio\0forceNextProcess\0"
    "createBuilding\0BuildingInformation\0"
    "type\0TileCoordinates\0leftCorner\0"
    "Direction\0orientation"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Engine[] = {

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
       4,    1,   52,    2, 0x0a /* Public */,
       4,    0,   55,    2, 0x2a /* Public | MethodCloned */,
       5,    1,   56,    2, 0x0a /* Public */,
       7,    0,   59,    2, 0x0a /* Public */,
       8,    0,   60,    2, 0x0a /* Public */,
       9,    3,   61,    2, 0x0a /* Public */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    2,

 // slots: parameters
    QMetaType::Void, QMetaType::Bool,    4,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QReal,    6,
    QMetaType::QReal,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 10, 0x80000000 | 12, 0x80000000 | 14,   11,   13,   15,

       0        // eod
};

void Engine::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<Engine *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->stateUpdated((*reinterpret_cast< State(*)>(_a[1]))); break;
        case 1: _t->pause((*reinterpret_cast< const bool(*)>(_a[1]))); break;
        case 2: _t->pause(); break;
        case 3: _t->setProcessorSpeedRatio((*reinterpret_cast< const qreal(*)>(_a[1]))); break;
        case 4: { qreal _r = _t->getProcessorSpeedRatio();
            if (_a[0]) *reinterpret_cast< qreal*>(_a[0]) = std::move(_r); }  break;
        case 5: _t->forceNextProcess(); break;
        case 6: _t->createBuilding((*reinterpret_cast< const BuildingInformation(*)>(_a[1])),(*reinterpret_cast< const TileCoordinates(*)>(_a[2])),(*reinterpret_cast< Direction(*)>(_a[3]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (Engine::*)(State );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Engine::stateUpdated)) {
                *result = 0;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject Engine::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_Engine.data,
    qt_meta_data_Engine,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Engine::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Engine::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Engine.stringdata0))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AreaCheckerInterface"))
        return static_cast< AreaCheckerInterface*>(this);
    if (!strcmp(_clname, "RoadPathGeneratorInterface"))
        return static_cast< RoadPathGeneratorInterface*>(this);
    return QObject::qt_metacast(_clname);
}

int Engine::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
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
void Engine::stateUpdated(State _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
