/****************************************************************************
** Meta object code from reading C++ file 'switches.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.19)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../components/switches.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'switches.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.19. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_Switch_t {
    QByteArrayData data[5];
    char stringdata0[31];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Switch_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Switch_t qt_meta_stringdata_Switch = {
    {
QT_MOC_LITERAL(0, 0, 6), // "Switch"
QT_MOC_LITERAL(1, 7, 5), // "Poles"
QT_MOC_LITERAL(2, 13, 2), // "DT"
QT_MOC_LITERAL(3, 16, 3), // "Key"
QT_MOC_LITERAL(4, 20, 10) // "Norm_Close"

    },
    "Switch\0Poles\0DT\0Key\0Norm_Close"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Switch[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       4,   14, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // properties: name, type, flags
       1, QMetaType::Int, 0x00195103,
       2, QMetaType::Bool, 0x00195103,
       3, QMetaType::QString, 0x00195103,
       4, QMetaType::Bool, 0x00195003,

       0        // eod
};

void Switch::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{

#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<Switch *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< int*>(_v) = _t->poles(); break;
        case 1: *reinterpret_cast< bool*>(_v) = _t->dt(); break;
        case 2: *reinterpret_cast< QString*>(_v) = _t->key(); break;
        case 3: *reinterpret_cast< bool*>(_v) = _t->normClose(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<Switch *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setPoles(*reinterpret_cast< int*>(_v)); break;
        case 1: _t->setDT(*reinterpret_cast< bool*>(_v)); break;
        case 2: _t->setKey(*reinterpret_cast< QString*>(_v)); break;
        case 3: _t->setNormClose(*reinterpret_cast< bool*>(_v)); break;
        default: break;
        }
    } else if (_c == QMetaObject::ResetProperty) {
    }
#endif // QT_NO_PROPERTIES
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject Switch::staticMetaObject = { {
    QMetaObject::SuperData::link<Component::staticMetaObject>(),
    qt_meta_stringdata_Switch.data,
    qt_meta_data_Switch,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Switch::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Switch::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Switch.stringdata0))
        return static_cast<void*>(this);
    return Component::qt_metacast(_clname);
}

int Switch::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Component::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    
#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    } else if (_c == QMetaObject::QueryPropertyDesignable) {
        _id -= 4;
    } else if (_c == QMetaObject::QueryPropertyScriptable) {
        _id -= 4;
    } else if (_c == QMetaObject::QueryPropertyStored) {
        _id -= 4;
    } else if (_c == QMetaObject::QueryPropertyEditable) {
        _id -= 4;
    } else if (_c == QMetaObject::QueryPropertyUser) {
        _id -= 4;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}
struct qt_meta_stringdata_Push_t {
    QByteArrayData data[1];
    char stringdata0[5];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Push_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Push_t qt_meta_stringdata_Push = {
    {
QT_MOC_LITERAL(0, 0, 4) // "Push"

    },
    "Push"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Push[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

       0        // eod
};

void Push::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject Push::staticMetaObject = { {
    QMetaObject::SuperData::link<Switch::staticMetaObject>(),
    qt_meta_stringdata_Push.data,
    qt_meta_data_Push,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Push::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Push::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Push.stringdata0))
        return static_cast<void*>(this);
    return Switch::qt_metacast(_clname);
}

int Push::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Switch::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_SwitchDip_t {
    QByteArrayData data[2];
    char stringdata0[15];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_SwitchDip_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_SwitchDip_t qt_meta_stringdata_SwitchDip = {
    {
QT_MOC_LITERAL(0, 0, 9), // "SwitchDip"
QT_MOC_LITERAL(1, 10, 4) // "Size"

    },
    "SwitchDip\0Size"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_SwitchDip[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       1,   14, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // properties: name, type, flags
       1, QMetaType::Int, 0x00195103,

       0        // eod
};

void SwitchDip::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{

#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<SwitchDip *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< int*>(_v) = _t->size(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<SwitchDip *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setSize(*reinterpret_cast< int*>(_v)); break;
        default: break;
        }
    } else if (_c == QMetaObject::ResetProperty) {
    }
#endif // QT_NO_PROPERTIES
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject SwitchDip::staticMetaObject = { {
    QMetaObject::SuperData::link<Component::staticMetaObject>(),
    qt_meta_stringdata_SwitchDip.data,
    qt_meta_data_SwitchDip,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *SwitchDip::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SwitchDip::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_SwitchDip.stringdata0))
        return static_cast<void*>(this);
    return Component::qt_metacast(_clname);
}

int SwitchDip::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Component::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    
#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyDesignable) {
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyScriptable) {
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyStored) {
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyEditable) {
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyUser) {
        _id -= 1;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}
struct qt_meta_stringdata_RelaySPST_t {
    QByteArrayData data[8];
    char stringdata0[50];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_RelaySPST_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_RelaySPST_t qt_meta_stringdata_RelaySPST = {
    {
QT_MOC_LITERAL(0, 0, 9), // "RelaySPST"
QT_MOC_LITERAL(1, 10, 10), // "Norm_Close"
QT_MOC_LITERAL(2, 21, 5), // "Poles"
QT_MOC_LITERAL(3, 27, 2), // "DT"
QT_MOC_LITERAL(4, 30, 5), // "Rcoil"
QT_MOC_LITERAL(5, 36, 4), // "Unit"
QT_MOC_LITERAL(6, 41, 3), // "IOn"
QT_MOC_LITERAL(7, 45, 4) // "IOff"

    },
    "RelaySPST\0Norm_Close\0Poles\0DT\0Rcoil\0"
    "Unit\0IOn\0IOff"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_RelaySPST[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       7,   14, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // properties: name, type, flags
       1, QMetaType::Bool, 0x00195003,
       2, QMetaType::Int, 0x00195103,
       3, QMetaType::Bool, 0x00195103,
       4, QMetaType::Double, 0x00195103,
       5, QMetaType::QString, 0x00195103,
       6, QMetaType::Double, 0x00195103,
       7, QMetaType::Double, 0x00195103,

       0        // eod
};

void RelaySPST::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{

#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<RelaySPST *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< bool*>(_v) = _t->normClose(); break;
        case 1: *reinterpret_cast< int*>(_v) = _t->poles(); break;
        case 2: *reinterpret_cast< bool*>(_v) = _t->dt(); break;
        case 3: *reinterpret_cast< double*>(_v) = _t->rcoil(); break;
        case 4: *reinterpret_cast< QString*>(_v) = _t->unit(); break;
        case 5: *reinterpret_cast< double*>(_v) = _t->iOn(); break;
        case 6: *reinterpret_cast< double*>(_v) = _t->iOff(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<RelaySPST *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setNormClose(*reinterpret_cast< bool*>(_v)); break;
        case 1: _t->setPoles(*reinterpret_cast< int*>(_v)); break;
        case 2: _t->setDT(*reinterpret_cast< bool*>(_v)); break;
        case 3: _t->setRcoil(*reinterpret_cast< double*>(_v)); break;
        case 4: _t->setUnit(*reinterpret_cast< QString*>(_v)); break;
        case 5: _t->setIOn(*reinterpret_cast< double*>(_v)); break;
        case 6: _t->setIOff(*reinterpret_cast< double*>(_v)); break;
        default: break;
        }
    } else if (_c == QMetaObject::ResetProperty) {
    }
#endif // QT_NO_PROPERTIES
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject RelaySPST::staticMetaObject = { {
    QMetaObject::SuperData::link<Component::staticMetaObject>(),
    qt_meta_stringdata_RelaySPST.data,
    qt_meta_data_RelaySPST,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *RelaySPST::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *RelaySPST::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_RelaySPST.stringdata0))
        return static_cast<void*>(this);
    return Component::qt_metacast(_clname);
}

int RelaySPST::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Component::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    
#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    } else if (_c == QMetaObject::QueryPropertyDesignable) {
        _id -= 7;
    } else if (_c == QMetaObject::QueryPropertyScriptable) {
        _id -= 7;
    } else if (_c == QMetaObject::QueryPropertyStored) {
        _id -= 7;
    } else if (_c == QMetaObject::QueryPropertyEditable) {
        _id -= 7;
    } else if (_c == QMetaObject::QueryPropertyUser) {
        _id -= 7;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
