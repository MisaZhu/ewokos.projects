/****************************************************************************
** Meta object code from reading C++ file 'logic.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.19)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../components/logic.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'logic.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.19. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_LogicBase_t {
    QByteArrayData data[7];
    char stringdata0[85];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_LogicBase_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_LogicBase_t qt_meta_stringdata_LogicBase = {
    {
QT_MOC_LITERAL(0, 0, 9), // "LogicBase"
QT_MOC_LITERAL(1, 10, 12), // "Input_High_V"
QT_MOC_LITERAL(2, 23, 11), // "Input_Low_V"
QT_MOC_LITERAL(3, 35, 13), // "Out_High_Volt"
QT_MOC_LITERAL(4, 49, 12), // "Out_Low_Volt"
QT_MOC_LITERAL(5, 62, 9), // "Out_Imped"
QT_MOC_LITERAL(6, 72, 12) // "Logic_Symbol"

    },
    "LogicBase\0Input_High_V\0Input_Low_V\0"
    "Out_High_Volt\0Out_Low_Volt\0Out_Imped\0"
    "Logic_Symbol"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_LogicBase[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       6,   14, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // properties: name, type, flags
       1, QMetaType::Double, 0x00195003,
       2, QMetaType::Double, 0x00195003,
       3, QMetaType::Double, 0x00195003,
       4, QMetaType::Double, 0x00195003,
       5, QMetaType::Double, 0x00195003,
       6, QMetaType::Bool, 0x00195003,

       0        // eod
};

void LogicBase::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{

#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<LogicBase *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< double*>(_v) = _t->inputHighV(); break;
        case 1: *reinterpret_cast< double*>(_v) = _t->inputLowV(); break;
        case 2: *reinterpret_cast< double*>(_v) = _t->outHighV(); break;
        case 3: *reinterpret_cast< double*>(_v) = _t->outLowV(); break;
        case 4: *reinterpret_cast< double*>(_v) = _t->outImped(); break;
        case 5: *reinterpret_cast< bool*>(_v) = _t->logicSymbol(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<LogicBase *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setInputHighV(*reinterpret_cast< double*>(_v)); break;
        case 1: _t->setInputLowV(*reinterpret_cast< double*>(_v)); break;
        case 2: _t->setOutHighV(*reinterpret_cast< double*>(_v)); break;
        case 3: _t->setOutLowV(*reinterpret_cast< double*>(_v)); break;
        case 4: _t->setOutImped(*reinterpret_cast< double*>(_v)); break;
        case 5: _t->setLogicSymbol(*reinterpret_cast< bool*>(_v)); break;
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

QT_INIT_METAOBJECT const QMetaObject LogicBase::staticMetaObject = { {
    QMetaObject::SuperData::link<Component::staticMetaObject>(),
    qt_meta_stringdata_LogicBase.data,
    qt_meta_data_LogicBase,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *LogicBase::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *LogicBase::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_LogicBase.stringdata0))
        return static_cast<void*>(this);
    return Component::qt_metacast(_clname);
}

int LogicBase::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Component::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    
#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    } else if (_c == QMetaObject::QueryPropertyDesignable) {
        _id -= 6;
    } else if (_c == QMetaObject::QueryPropertyScriptable) {
        _id -= 6;
    } else if (_c == QMetaObject::QueryPropertyStored) {
        _id -= 6;
    } else if (_c == QMetaObject::QueryPropertyEditable) {
        _id -= 6;
    } else if (_c == QMetaObject::QueryPropertyUser) {
        _id -= 6;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}
struct qt_meta_stringdata_Gate_t {
    QByteArrayData data[3];
    char stringdata0[21];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Gate_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Gate_t qt_meta_stringdata_Gate = {
    {
QT_MOC_LITERAL(0, 0, 4), // "Gate"
QT_MOC_LITERAL(1, 5, 8), // "Inverted"
QT_MOC_LITERAL(2, 14, 6) // "Inputs"

    },
    "Gate\0Inverted\0Inputs"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Gate[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       2,   14, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // properties: name, type, flags
       1, QMetaType::Bool, 0x00195103,
       2, QMetaType::Int, 0x00195103,

       0        // eod
};

void Gate::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{

#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<Gate *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< bool*>(_v) = _t->inverted(); break;
        case 1: *reinterpret_cast< int*>(_v) = _t->inputs(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<Gate *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setInverted(*reinterpret_cast< bool*>(_v)); break;
        case 1: _t->setInputs(*reinterpret_cast< int*>(_v)); break;
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

QT_INIT_METAOBJECT const QMetaObject Gate::staticMetaObject = { {
    QMetaObject::SuperData::link<LogicBase::staticMetaObject>(),
    qt_meta_stringdata_Gate.data,
    qt_meta_data_Gate,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Gate::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Gate::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Gate.stringdata0))
        return static_cast<void*>(this);
    return LogicBase::qt_metacast(_clname);
}

int Gate::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = LogicBase::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    
#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyDesignable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyScriptable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyStored) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyEditable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyUser) {
        _id -= 2;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}
struct qt_meta_stringdata_Buffer_t {
    QByteArrayData data[1];
    char stringdata0[7];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Buffer_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Buffer_t qt_meta_stringdata_Buffer = {
    {
QT_MOC_LITERAL(0, 0, 6) // "Buffer"

    },
    "Buffer"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Buffer[] = {

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

void Buffer::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject Buffer::staticMetaObject = { {
    QMetaObject::SuperData::link<Gate::staticMetaObject>(),
    qt_meta_stringdata_Buffer.data,
    qt_meta_data_Buffer,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Buffer::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Buffer::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Buffer.stringdata0))
        return static_cast<void*>(this);
    return Gate::qt_metacast(_clname);
}

int Buffer::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Gate::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_AndGate_t {
    QByteArrayData data[1];
    char stringdata0[8];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_AndGate_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_AndGate_t qt_meta_stringdata_AndGate = {
    {
QT_MOC_LITERAL(0, 0, 7) // "AndGate"

    },
    "AndGate"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_AndGate[] = {

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

void AndGate::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject AndGate::staticMetaObject = { {
    QMetaObject::SuperData::link<Gate::staticMetaObject>(),
    qt_meta_stringdata_AndGate.data,
    qt_meta_data_AndGate,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *AndGate::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AndGate::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_AndGate.stringdata0))
        return static_cast<void*>(this);
    return Gate::qt_metacast(_clname);
}

int AndGate::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Gate::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_OrGate_t {
    QByteArrayData data[1];
    char stringdata0[7];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_OrGate_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_OrGate_t qt_meta_stringdata_OrGate = {
    {
QT_MOC_LITERAL(0, 0, 6) // "OrGate"

    },
    "OrGate"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_OrGate[] = {

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

void OrGate::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject OrGate::staticMetaObject = { {
    QMetaObject::SuperData::link<Gate::staticMetaObject>(),
    qt_meta_stringdata_OrGate.data,
    qt_meta_data_OrGate,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *OrGate::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *OrGate::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_OrGate.stringdata0))
        return static_cast<void*>(this);
    return Gate::qt_metacast(_clname);
}

int OrGate::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Gate::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_XorGate_t {
    QByteArrayData data[1];
    char stringdata0[8];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_XorGate_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_XorGate_t qt_meta_stringdata_XorGate = {
    {
QT_MOC_LITERAL(0, 0, 7) // "XorGate"

    },
    "XorGate"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_XorGate[] = {

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

void XorGate::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject XorGate::staticMetaObject = { {
    QMetaObject::SuperData::link<Gate::staticMetaObject>(),
    qt_meta_stringdata_XorGate.data,
    qt_meta_data_XorGate,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *XorGate::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *XorGate::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_XorGate.stringdata0))
        return static_cast<void*>(this);
    return Gate::qt_metacast(_clname);
}

int XorGate::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Gate::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_Function_t {
    QByteArrayData data[2];
    char stringdata0[19];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Function_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Function_t qt_meta_stringdata_Function = {
    {
QT_MOC_LITERAL(0, 0, 8), // "Function"
QT_MOC_LITERAL(1, 9, 9) // "Functions"

    },
    "Function\0Functions"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Function[] = {

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
       1, QMetaType::QStringList, 0x00195103,

       0        // eod
};

void Function::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{

#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<Function *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< QStringList*>(_v) = _t->functions(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<Function *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setFunctions(*reinterpret_cast< QStringList*>(_v)); break;
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

QT_INIT_METAOBJECT const QMetaObject Function::staticMetaObject = { {
    QMetaObject::SuperData::link<LogicBase::staticMetaObject>(),
    qt_meta_stringdata_Function.data,
    qt_meta_data_Function,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Function::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Function::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Function.stringdata0))
        return static_cast<void*>(this);
    return LogicBase::qt_metacast(_clname);
}

int Function::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = LogicBase::qt_metacall(_c, _id, _a);
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
struct qt_meta_stringdata_FlipFlop_t {
    QByteArrayData data[2];
    char stringdata0[27];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_FlipFlop_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_FlipFlop_t qt_meta_stringdata_FlipFlop = {
    {
QT_MOC_LITERAL(0, 0, 8), // "FlipFlop"
QT_MOC_LITERAL(1, 9, 17) // "Set_Reset_Enabled"

    },
    "FlipFlop\0Set_Reset_Enabled"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_FlipFlop[] = {

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
       1, QMetaType::Bool, 0x00195003,

       0        // eod
};

void FlipFlop::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{

#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<FlipFlop *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< bool*>(_v) = _t->setResetEnabled(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<FlipFlop *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setSetResetEnabled(*reinterpret_cast< bool*>(_v)); break;
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

QT_INIT_METAOBJECT const QMetaObject FlipFlop::staticMetaObject = { {
    QMetaObject::SuperData::link<LogicBase::staticMetaObject>(),
    qt_meta_stringdata_FlipFlop.data,
    qt_meta_data_FlipFlop,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *FlipFlop::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *FlipFlop::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_FlipFlop.stringdata0))
        return static_cast<void*>(this);
    return LogicBase::qt_metacast(_clname);
}

int FlipFlop::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = LogicBase::qt_metacall(_c, _id, _a);
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
struct qt_meta_stringdata_FlipFlopD_t {
    QByteArrayData data[1];
    char stringdata0[10];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_FlipFlopD_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_FlipFlopD_t qt_meta_stringdata_FlipFlopD = {
    {
QT_MOC_LITERAL(0, 0, 9) // "FlipFlopD"

    },
    "FlipFlopD"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_FlipFlopD[] = {

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

void FlipFlopD::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject FlipFlopD::staticMetaObject = { {
    QMetaObject::SuperData::link<FlipFlop::staticMetaObject>(),
    qt_meta_stringdata_FlipFlopD.data,
    qt_meta_data_FlipFlopD,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *FlipFlopD::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *FlipFlopD::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_FlipFlopD.stringdata0))
        return static_cast<void*>(this);
    return FlipFlop::qt_metacast(_clname);
}

int FlipFlopD::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = FlipFlop::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_FlipFlopJK_t {
    QByteArrayData data[1];
    char stringdata0[11];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_FlipFlopJK_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_FlipFlopJK_t qt_meta_stringdata_FlipFlopJK = {
    {
QT_MOC_LITERAL(0, 0, 10) // "FlipFlopJK"

    },
    "FlipFlopJK"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_FlipFlopJK[] = {

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

void FlipFlopJK::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject FlipFlopJK::staticMetaObject = { {
    QMetaObject::SuperData::link<FlipFlop::staticMetaObject>(),
    qt_meta_stringdata_FlipFlopJK.data,
    qt_meta_data_FlipFlopJK,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *FlipFlopJK::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *FlipFlopJK::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_FlipFlopJK.stringdata0))
        return static_cast<void*>(this);
    return FlipFlop::qt_metacast(_clname);
}

int FlipFlopJK::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = FlipFlop::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_LatchD_t {
    QByteArrayData data[2];
    char stringdata0[12];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_LatchD_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_LatchD_t qt_meta_stringdata_LatchD = {
    {
QT_MOC_LITERAL(0, 0, 6), // "LatchD"
QT_MOC_LITERAL(1, 7, 4) // "Size"

    },
    "LatchD\0Size"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_LatchD[] = {

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

void LatchD::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{

#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<LatchD *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< int*>(_v) = _t->size(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<LatchD *>(_o);
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

QT_INIT_METAOBJECT const QMetaObject LatchD::staticMetaObject = { {
    QMetaObject::SuperData::link<LogicBase::staticMetaObject>(),
    qt_meta_stringdata_LatchD.data,
    qt_meta_data_LatchD,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *LatchD::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *LatchD::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_LatchD.stringdata0))
        return static_cast<void*>(this);
    return LogicBase::qt_metacast(_clname);
}

int LatchD::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = LogicBase::qt_metacall(_c, _id, _a);
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
struct qt_meta_stringdata_BinCounter_t {
    QByteArrayData data[3];
    char stringdata0[26];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_BinCounter_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_BinCounter_t qt_meta_stringdata_BinCounter = {
    {
QT_MOC_LITERAL(0, 0, 10), // "BinCounter"
QT_MOC_LITERAL(1, 11, 4), // "Bits"
QT_MOC_LITERAL(2, 16, 9) // "Max_Value"

    },
    "BinCounter\0Bits\0Max_Value"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_BinCounter[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       2,   14, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // properties: name, type, flags
       1, QMetaType::Int, 0x00195103,
       2, QMetaType::Int, 0x00195003,

       0        // eod
};

void BinCounter::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{

#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<BinCounter *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< int*>(_v) = _t->bits(); break;
        case 1: *reinterpret_cast< int*>(_v) = _t->maxValue(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<BinCounter *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setBits(*reinterpret_cast< int*>(_v)); break;
        case 1: _t->setMaxValue(*reinterpret_cast< int*>(_v)); break;
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

QT_INIT_METAOBJECT const QMetaObject BinCounter::staticMetaObject = { {
    QMetaObject::SuperData::link<LogicBase::staticMetaObject>(),
    qt_meta_stringdata_BinCounter.data,
    qt_meta_data_BinCounter,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *BinCounter::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *BinCounter::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_BinCounter.stringdata0))
        return static_cast<void*>(this);
    return LogicBase::qt_metacast(_clname);
}

int BinCounter::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = LogicBase::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    
#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyDesignable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyScriptable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyStored) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyEditable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyUser) {
        _id -= 2;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}
struct qt_meta_stringdata_FullAdder_t {
    QByteArrayData data[1];
    char stringdata0[10];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_FullAdder_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_FullAdder_t qt_meta_stringdata_FullAdder = {
    {
QT_MOC_LITERAL(0, 0, 9) // "FullAdder"

    },
    "FullAdder"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_FullAdder[] = {

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

void FullAdder::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject FullAdder::staticMetaObject = { {
    QMetaObject::SuperData::link<LogicBase::staticMetaObject>(),
    qt_meta_stringdata_FullAdder.data,
    qt_meta_data_FullAdder,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *FullAdder::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *FullAdder::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_FullAdder.stringdata0))
        return static_cast<void*>(this);
    return LogicBase::qt_metacast(_clname);
}

int FullAdder::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = LogicBase::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_ShiftReg_t {
    QByteArrayData data[3];
    char stringdata0[30];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_ShiftReg_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_ShiftReg_t qt_meta_stringdata_ShiftReg = {
    {
QT_MOC_LITERAL(0, 0, 8), // "ShiftReg"
QT_MOC_LITERAL(1, 9, 4), // "Bits"
QT_MOC_LITERAL(2, 14, 15) // "Input_Lsb_First"

    },
    "ShiftReg\0Bits\0Input_Lsb_First"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_ShiftReg[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       2,   14, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // properties: name, type, flags
       1, QMetaType::Int, 0x00195103,
       2, QMetaType::Bool, 0x00195003,

       0        // eod
};

void ShiftReg::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{

#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<ShiftReg *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< int*>(_v) = _t->bits(); break;
        case 1: *reinterpret_cast< bool*>(_v) = _t->lsbFirst(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<ShiftReg *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setBits(*reinterpret_cast< int*>(_v)); break;
        case 1: _t->setLsbFirst(*reinterpret_cast< bool*>(_v)); break;
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

QT_INIT_METAOBJECT const QMetaObject ShiftReg::staticMetaObject = { {
    QMetaObject::SuperData::link<LogicBase::staticMetaObject>(),
    qt_meta_stringdata_ShiftReg.data,
    qt_meta_data_ShiftReg,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *ShiftReg::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ShiftReg::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ShiftReg.stringdata0))
        return static_cast<void*>(this);
    return LogicBase::qt_metacast(_clname);
}

int ShiftReg::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = LogicBase::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    
#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyDesignable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyScriptable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyStored) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyEditable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyUser) {
        _id -= 2;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}
struct qt_meta_stringdata_Mux_t {
    QByteArrayData data[2];
    char stringdata0[11];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Mux_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Mux_t qt_meta_stringdata_Mux = {
    {
QT_MOC_LITERAL(0, 0, 3), // "Mux"
QT_MOC_LITERAL(1, 4, 6) // "Inputs"

    },
    "Mux\0Inputs"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Mux[] = {

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

void Mux::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{

#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<Mux *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< int*>(_v) = _t->inputs(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<Mux *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setInputs(*reinterpret_cast< int*>(_v)); break;
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

QT_INIT_METAOBJECT const QMetaObject Mux::staticMetaObject = { {
    QMetaObject::SuperData::link<LogicBase::staticMetaObject>(),
    qt_meta_stringdata_Mux.data,
    qt_meta_data_Mux,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Mux::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Mux::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Mux.stringdata0))
        return static_cast<void*>(this);
    return LogicBase::qt_metacast(_clname);
}

int Mux::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = LogicBase::qt_metacall(_c, _id, _a);
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
struct qt_meta_stringdata_Demux_t {
    QByteArrayData data[2];
    char stringdata0[14];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Demux_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Demux_t qt_meta_stringdata_Demux = {
    {
QT_MOC_LITERAL(0, 0, 5), // "Demux"
QT_MOC_LITERAL(1, 6, 7) // "Outputs"

    },
    "Demux\0Outputs"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Demux[] = {

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

void Demux::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{

#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<Demux *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< int*>(_v) = _t->outputs(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<Demux *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setOutputs(*reinterpret_cast< int*>(_v)); break;
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

QT_INIT_METAOBJECT const QMetaObject Demux::staticMetaObject = { {
    QMetaObject::SuperData::link<LogicBase::staticMetaObject>(),
    qt_meta_stringdata_Demux.data,
    qt_meta_data_Demux,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Demux::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Demux::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Demux.stringdata0))
        return static_cast<void*>(this);
    return LogicBase::qt_metacast(_clname);
}

int Demux::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = LogicBase::qt_metacall(_c, _id, _a);
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
struct qt_meta_stringdata_BcdToDec_t {
    QByteArrayData data[1];
    char stringdata0[9];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_BcdToDec_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_BcdToDec_t qt_meta_stringdata_BcdToDec = {
    {
QT_MOC_LITERAL(0, 0, 8) // "BcdToDec"

    },
    "BcdToDec"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_BcdToDec[] = {

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

void BcdToDec::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject BcdToDec::staticMetaObject = { {
    QMetaObject::SuperData::link<LogicBase::staticMetaObject>(),
    qt_meta_stringdata_BcdToDec.data,
    qt_meta_data_BcdToDec,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *BcdToDec::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *BcdToDec::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_BcdToDec.stringdata0))
        return static_cast<void*>(this);
    return LogicBase::qt_metacast(_clname);
}

int BcdToDec::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = LogicBase::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_DecToBcd_t {
    QByteArrayData data[1];
    char stringdata0[9];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_DecToBcd_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_DecToBcd_t qt_meta_stringdata_DecToBcd = {
    {
QT_MOC_LITERAL(0, 0, 8) // "DecToBcd"

    },
    "DecToBcd"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_DecToBcd[] = {

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

void DecToBcd::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject DecToBcd::staticMetaObject = { {
    QMetaObject::SuperData::link<LogicBase::staticMetaObject>(),
    qt_meta_stringdata_DecToBcd.data,
    qt_meta_data_DecToBcd,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *DecToBcd::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *DecToBcd::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_DecToBcd.stringdata0))
        return static_cast<void*>(this);
    return LogicBase::qt_metacast(_clname);
}

int DecToBcd::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = LogicBase::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_BcdTo7S_t {
    QByteArrayData data[1];
    char stringdata0[8];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_BcdTo7S_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_BcdTo7S_t qt_meta_stringdata_BcdTo7S = {
    {
QT_MOC_LITERAL(0, 0, 7) // "BcdTo7S"

    },
    "BcdTo7S"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_BcdTo7S[] = {

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

void BcdTo7S::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject BcdTo7S::staticMetaObject = { {
    QMetaObject::SuperData::link<LogicBase::staticMetaObject>(),
    qt_meta_stringdata_BcdTo7S.data,
    qt_meta_data_BcdTo7S,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *BcdTo7S::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *BcdTo7S::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_BcdTo7S.stringdata0))
        return static_cast<void*>(this);
    return LogicBase::qt_metacast(_clname);
}

int BcdTo7S::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = LogicBase::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_SevenSegmentBCD_t {
    QByteArrayData data[1];
    char stringdata0[16];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_SevenSegmentBCD_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_SevenSegmentBCD_t qt_meta_stringdata_SevenSegmentBCD = {
    {
QT_MOC_LITERAL(0, 0, 15) // "SevenSegmentBCD"

    },
    "SevenSegmentBCD"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_SevenSegmentBCD[] = {

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

void SevenSegmentBCD::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject SevenSegmentBCD::staticMetaObject = { {
    QMetaObject::SuperData::link<LogicBase::staticMetaObject>(),
    qt_meta_stringdata_SevenSegmentBCD.data,
    qt_meta_data_SevenSegmentBCD,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *SevenSegmentBCD::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SevenSegmentBCD::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_SevenSegmentBCD.stringdata0))
        return static_cast<void*>(this);
    return LogicBase::qt_metacast(_clname);
}

int SevenSegmentBCD::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = LogicBase::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_ADC_t {
    QByteArrayData data[3];
    char stringdata0[14];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_ADC_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_ADC_t qt_meta_stringdata_ADC = {
    {
QT_MOC_LITERAL(0, 0, 3), // "ADC"
QT_MOC_LITERAL(1, 4, 4), // "Size"
QT_MOC_LITERAL(2, 9, 4) // "Vref"

    },
    "ADC\0Size\0Vref"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_ADC[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       2,   14, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // properties: name, type, flags
       1, QMetaType::Int, 0x00195103,
       2, QMetaType::Double, 0x00195103,

       0        // eod
};

void ADC::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{

#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<ADC *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< int*>(_v) = _t->size(); break;
        case 1: *reinterpret_cast< double*>(_v) = _t->vref(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<ADC *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setSize(*reinterpret_cast< int*>(_v)); break;
        case 1: _t->setVref(*reinterpret_cast< double*>(_v)); break;
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

QT_INIT_METAOBJECT const QMetaObject ADC::staticMetaObject = { {
    QMetaObject::SuperData::link<LogicBase::staticMetaObject>(),
    qt_meta_stringdata_ADC.data,
    qt_meta_data_ADC,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *ADC::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ADC::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ADC.stringdata0))
        return static_cast<void*>(this);
    return LogicBase::qt_metacast(_clname);
}

int ADC::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = LogicBase::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    
#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyDesignable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyScriptable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyStored) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyEditable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyUser) {
        _id -= 2;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}
struct qt_meta_stringdata_DAC_t {
    QByteArrayData data[3];
    char stringdata0[14];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_DAC_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_DAC_t qt_meta_stringdata_DAC = {
    {
QT_MOC_LITERAL(0, 0, 3), // "DAC"
QT_MOC_LITERAL(1, 4, 4), // "Size"
QT_MOC_LITERAL(2, 9, 4) // "Vref"

    },
    "DAC\0Size\0Vref"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_DAC[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       2,   14, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // properties: name, type, flags
       1, QMetaType::Int, 0x00195103,
       2, QMetaType::Double, 0x00195103,

       0        // eod
};

void DAC::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{

#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<DAC *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< int*>(_v) = _t->size(); break;
        case 1: *reinterpret_cast< double*>(_v) = _t->vref(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<DAC *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setSize(*reinterpret_cast< int*>(_v)); break;
        case 1: _t->setVref(*reinterpret_cast< double*>(_v)); break;
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

QT_INIT_METAOBJECT const QMetaObject DAC::staticMetaObject = { {
    QMetaObject::SuperData::link<LogicBase::staticMetaObject>(),
    qt_meta_stringdata_DAC.data,
    qt_meta_data_DAC,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *DAC::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *DAC::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_DAC.stringdata0))
        return static_cast<void*>(this);
    return LogicBase::qt_metacast(_clname);
}

int DAC::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = LogicBase::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    
#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyDesignable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyScriptable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyStored) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyEditable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyUser) {
        _id -= 2;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}
struct qt_meta_stringdata_Bus_t {
    QByteArrayData data[2];
    char stringdata0[9];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Bus_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Bus_t qt_meta_stringdata_Bus = {
    {
QT_MOC_LITERAL(0, 0, 3), // "Bus"
QT_MOC_LITERAL(1, 4, 4) // "Size"

    },
    "Bus\0Size"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Bus[] = {

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

void Bus::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{

#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<Bus *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< int*>(_v) = _t->size(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<Bus *>(_o);
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

QT_INIT_METAOBJECT const QMetaObject Bus::staticMetaObject = { {
    QMetaObject::SuperData::link<Component::staticMetaObject>(),
    qt_meta_stringdata_Bus.data,
    qt_meta_data_Bus,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Bus::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Bus::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Bus.stringdata0))
        return static_cast<void*>(this);
    return Component::qt_metacast(_clname);
}

int Bus::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
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
struct qt_meta_stringdata_Memory_t {
    QByteArrayData data[4];
    char stringdata0[39];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Memory_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Memory_t qt_meta_stringdata_Memory = {
    {
QT_MOC_LITERAL(0, 0, 6), // "Memory"
QT_MOC_LITERAL(1, 7, 9), // "Addr_Bits"
QT_MOC_LITERAL(2, 17, 9), // "Data_Bits"
QT_MOC_LITERAL(3, 27, 11) // "Persistence"

    },
    "Memory\0Addr_Bits\0Data_Bits\0Persistence"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Memory[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       3,   14, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // properties: name, type, flags
       1, QMetaType::Int, 0x00195003,
       2, QMetaType::Int, 0x00195003,
       3, QMetaType::Bool, 0x00195103,

       0        // eod
};

void Memory::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{

#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<Memory *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< int*>(_v) = _t->addrBits(); break;
        case 1: *reinterpret_cast< int*>(_v) = _t->dataBits(); break;
        case 2: *reinterpret_cast< bool*>(_v) = _t->persistence(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<Memory *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setAddrBits(*reinterpret_cast< int*>(_v)); break;
        case 1: _t->setDataBits(*reinterpret_cast< int*>(_v)); break;
        case 2: _t->setPersistence(*reinterpret_cast< bool*>(_v)); break;
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

QT_INIT_METAOBJECT const QMetaObject Memory::staticMetaObject = { {
    QMetaObject::SuperData::link<Chip::staticMetaObject>(),
    qt_meta_stringdata_Memory.data,
    qt_meta_data_Memory,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Memory::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Memory::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Memory.stringdata0))
        return static_cast<void*>(this);
    return Chip::qt_metacast(_clname);
}

int Memory::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Chip::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    
#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    } else if (_c == QMetaObject::QueryPropertyDesignable) {
        _id -= 3;
    } else if (_c == QMetaObject::QueryPropertyScriptable) {
        _id -= 3;
    } else if (_c == QMetaObject::QueryPropertyStored) {
        _id -= 3;
    } else if (_c == QMetaObject::QueryPropertyEditable) {
        _id -= 3;
    } else if (_c == QMetaObject::QueryPropertyUser) {
        _id -= 3;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}
struct qt_meta_stringdata_I2CBase_t {
    QByteArrayData data[2];
    char stringdata0[21];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_I2CBase_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_I2CBase_t qt_meta_stringdata_I2CBase = {
    {
QT_MOC_LITERAL(0, 0, 7), // "I2CBase"
QT_MOC_LITERAL(1, 8, 12) // "Control_Code"

    },
    "I2CBase\0Control_Code"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_I2CBase[] = {

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
       1, QMetaType::Int, 0x00195003,

       0        // eod
};

void I2CBase::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{

#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<I2CBase *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< int*>(_v) = _t->controlCode(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<I2CBase *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setControlCode(*reinterpret_cast< int*>(_v)); break;
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

QT_INIT_METAOBJECT const QMetaObject I2CBase::staticMetaObject = { {
    QMetaObject::SuperData::link<LogicBase::staticMetaObject>(),
    qt_meta_stringdata_I2CBase.data,
    qt_meta_data_I2CBase,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *I2CBase::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *I2CBase::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_I2CBase.stringdata0))
        return static_cast<void*>(this);
    return LogicBase::qt_metacast(_clname);
}

int I2CBase::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = LogicBase::qt_metacall(_c, _id, _a);
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
struct qt_meta_stringdata_I2CRam_t {
    QByteArrayData data[2];
    char stringdata0[12];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_I2CRam_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_I2CRam_t qt_meta_stringdata_I2CRam = {
    {
QT_MOC_LITERAL(0, 0, 6), // "I2CRam"
QT_MOC_LITERAL(1, 7, 4) // "Size"

    },
    "I2CRam\0Size"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_I2CRam[] = {

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

void I2CRam::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{

#ifndef QT_NO_PROPERTIES
    if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<I2CRam *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< int*>(_v) = _t->size(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<I2CRam *>(_o);
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

QT_INIT_METAOBJECT const QMetaObject I2CRam::staticMetaObject = { {
    QMetaObject::SuperData::link<I2CBase::staticMetaObject>(),
    qt_meta_stringdata_I2CRam.data,
    qt_meta_data_I2CRam,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *I2CRam::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *I2CRam::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_I2CRam.stringdata0))
        return static_cast<void*>(this);
    return I2CBase::qt_metacast(_clname);
}

int I2CRam::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = I2CBase::qt_metacall(_c, _id, _a);
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
struct qt_meta_stringdata_I2CToParallel_t {
    QByteArrayData data[1];
    char stringdata0[14];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_I2CToParallel_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_I2CToParallel_t qt_meta_stringdata_I2CToParallel = {
    {
QT_MOC_LITERAL(0, 0, 13) // "I2CToParallel"

    },
    "I2CToParallel"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_I2CToParallel[] = {

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

void I2CToParallel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject I2CToParallel::staticMetaObject = { {
    QMetaObject::SuperData::link<I2CBase::staticMetaObject>(),
    qt_meta_stringdata_I2CToParallel.data,
    qt_meta_data_I2CToParallel,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *I2CToParallel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *I2CToParallel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_I2CToParallel.stringdata0))
        return static_cast<void*>(this);
    return I2CBase::qt_metacast(_clname);
}

int I2CToParallel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = I2CBase::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_Lm555_t {
    QByteArrayData data[1];
    char stringdata0[6];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Lm555_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Lm555_t qt_meta_stringdata_Lm555 = {
    {
QT_MOC_LITERAL(0, 0, 5) // "Lm555"

    },
    "Lm555"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Lm555[] = {

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

void Lm555::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject Lm555::staticMetaObject = { {
    QMetaObject::SuperData::link<Chip::staticMetaObject>(),
    qt_meta_stringdata_Lm555.data,
    qt_meta_data_Lm555,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Lm555::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Lm555::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Lm555.stringdata0))
        return static_cast<void*>(this);
    return Chip::qt_metacast(_clname);
}

int Lm555::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Chip::qt_metacall(_c, _id, _a);
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
