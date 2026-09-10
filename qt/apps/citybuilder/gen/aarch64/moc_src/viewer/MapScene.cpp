/****************************************************************************
** Meta object code from reading C++ file 'MapScene.hpp'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.19)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../src/viewer/MapScene.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'MapScene.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.19. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_MapScene_t {
    QByteArrayData data[23];
    char stringdata0[350];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_MapScene_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_MapScene_t qt_meta_stringdata_MapScene = {
    {
QT_MOC_LITERAL(0, 0, 8), // "MapScene"
QT_MOC_LITERAL(1, 9, 25), // "buildingCreationRequested"
QT_MOC_LITERAL(2, 35, 0), // ""
QT_MOC_LITERAL(3, 36, 19), // "BuildingInformation"
QT_MOC_LITERAL(4, 56, 11), // "elementConf"
QT_MOC_LITERAL(5, 68, 15), // "TileCoordinates"
QT_MOC_LITERAL(6, 84, 10), // "leftCorner"
QT_MOC_LITERAL(7, 95, 9), // "Direction"
QT_MOC_LITERAL(8, 105, 11), // "orientation"
QT_MOC_LITERAL(9, 117, 26), // "requestBuildingPositioning"
QT_MOC_LITERAL(10, 144, 23), // "requestBuildingRotation"
QT_MOC_LITERAL(11, 168, 19), // "registerNewBuilding"
QT_MOC_LITERAL(12, 188, 13), // "BuildingState"
QT_MOC_LITERAL(13, 202, 13), // "buildingState"
QT_MOC_LITERAL(14, 216, 20), // "registerNewCharacter"
QT_MOC_LITERAL(15, 237, 14), // "CharacterState"
QT_MOC_LITERAL(16, 252, 14), // "characterState"
QT_MOC_LITERAL(17, 267, 24), // "registerNewNatureElement"
QT_MOC_LITERAL(18, 292, 18), // "NatureElementState"
QT_MOC_LITERAL(19, 311, 18), // "natureElementState"
QT_MOC_LITERAL(20, 330, 7), // "refresh"
QT_MOC_LITERAL(21, 338, 5), // "State"
QT_MOC_LITERAL(22, 344, 5) // "state"

    },
    "MapScene\0buildingCreationRequested\0\0"
    "BuildingInformation\0elementConf\0"
    "TileCoordinates\0leftCorner\0Direction\0"
    "orientation\0requestBuildingPositioning\0"
    "requestBuildingRotation\0registerNewBuilding\0"
    "BuildingState\0buildingState\0"
    "registerNewCharacter\0CharacterState\0"
    "characterState\0registerNewNatureElement\0"
    "NatureElementState\0natureElementState\0"
    "refresh\0State\0state"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_MapScene[] = {

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
       1,    3,   49,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       9,    1,   56,    2, 0x0a /* Public */,
      10,    0,   59,    2, 0x0a /* Public */,
      11,    1,   60,    2, 0x0a /* Public */,
      14,    1,   63,    2, 0x0a /* Public */,
      17,    1,   66,    2, 0x0a /* Public */,
      20,    1,   69,    2, 0x0a /* Public */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3, 0x80000000 | 5, 0x80000000 | 7,    4,    6,    8,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 12,   13,
    QMetaType::Void, 0x80000000 | 15,   16,
    QMetaType::Void, 0x80000000 | 18,   19,
    QMetaType::Void, 0x80000000 | 21,   22,

       0        // eod
};

void MapScene::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<MapScene *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->buildingCreationRequested((*reinterpret_cast< const BuildingInformation(*)>(_a[1])),(*reinterpret_cast< TileCoordinates(*)>(_a[2])),(*reinterpret_cast< Direction(*)>(_a[3]))); break;
        case 1: _t->requestBuildingPositioning((*reinterpret_cast< const BuildingInformation(*)>(_a[1]))); break;
        case 2: _t->requestBuildingRotation(); break;
        case 3: _t->registerNewBuilding((*reinterpret_cast< const BuildingState(*)>(_a[1]))); break;
        case 4: _t->registerNewCharacter((*reinterpret_cast< const CharacterState(*)>(_a[1]))); break;
        case 5: _t->registerNewNatureElement((*reinterpret_cast< const NatureElementState(*)>(_a[1]))); break;
        case 6: _t->refresh((*reinterpret_cast< const State(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (MapScene::*)(const BuildingInformation & , TileCoordinates , Direction );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MapScene::buildingCreationRequested)) {
                *result = 0;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject MapScene::staticMetaObject = { {
    QMetaObject::SuperData::link<QGraphicsScene::staticMetaObject>(),
    qt_meta_stringdata_MapScene.data,
    qt_meta_data_MapScene,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *MapScene::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MapScene::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_MapScene.stringdata0))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "TileLocatorInterface"))
        return static_cast< TileLocatorInterface*>(this);
    return QGraphicsScene::qt_metacast(_clname);
}

int MapScene::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QGraphicsScene::qt_metacall(_c, _id, _a);
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
void MapScene::buildingCreationRequested(const BuildingInformation & _t1, TileCoordinates _t2, Direction _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
