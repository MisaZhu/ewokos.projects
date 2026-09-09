/****************************************************************************
** Meta object code from reading C++ file 'Pty.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.19)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../qtermwidget/Pty.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'Pty.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.19. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_Konsole__Pty_t {
    QByteArrayData data[16];
    char stringdata0[149];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Konsole__Pty_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Konsole__Pty_t qt_meta_stringdata_Konsole__Pty = {
    {
QT_MOC_LITERAL(0, 0, 12), // "Konsole::Pty"
QT_MOC_LITERAL(1, 13, 12), // "receivedData"
QT_MOC_LITERAL(2, 26, 0), // ""
QT_MOC_LITERAL(3, 27, 11), // "const char*"
QT_MOC_LITERAL(4, 39, 6), // "buffer"
QT_MOC_LITERAL(5, 46, 6), // "length"
QT_MOC_LITERAL(6, 53, 8), // "finished"
QT_MOC_LITERAL(7, 62, 8), // "exitCode"
QT_MOC_LITERAL(8, 71, 24), // "Konsole::Pty::ExitStatus"
QT_MOC_LITERAL(9, 96, 10), // "exitStatus"
QT_MOC_LITERAL(10, 107, 11), // "setUtf8Mode"
QT_MOC_LITERAL(11, 119, 2), // "on"
QT_MOC_LITERAL(12, 122, 7), // "lockPty"
QT_MOC_LITERAL(13, 130, 4), // "lock"
QT_MOC_LITERAL(14, 135, 8), // "sendData"
QT_MOC_LITERAL(15, 144, 4) // "pump"

    },
    "Konsole::Pty\0receivedData\0\0const char*\0"
    "buffer\0length\0finished\0exitCode\0"
    "Konsole::Pty::ExitStatus\0exitStatus\0"
    "setUtf8Mode\0on\0lockPty\0lock\0sendData\0"
    "pump"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Konsole__Pty[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       6,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    2,   44,    2, 0x06 /* Public */,
       6,    2,   49,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      10,    1,   54,    2, 0x0a /* Public */,
      12,    1,   57,    2, 0x0a /* Public */,
      14,    2,   60,    2, 0x0a /* Public */,
      15,    0,   65,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3, QMetaType::Int,    4,    5,
    QMetaType::Void, QMetaType::Int, 0x80000000 | 8,    7,    9,

 // slots: parameters
    QMetaType::Void, QMetaType::Bool,   11,
    QMetaType::Void, QMetaType::Bool,   13,
    QMetaType::Void, 0x80000000 | 3, QMetaType::Int,    4,    5,
    QMetaType::Void,

       0        // eod
};

void Konsole::Pty::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<Pty *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->receivedData((*reinterpret_cast< const char*(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2]))); break;
        case 1: _t->finished((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< Konsole::Pty::ExitStatus(*)>(_a[2]))); break;
        case 2: _t->setUtf8Mode((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 3: _t->lockPty((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 4: _t->sendData((*reinterpret_cast< const char*(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2]))); break;
        case 5: _t->pump(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (Pty::*)(const char * , int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Pty::receivedData)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (Pty::*)(int , Konsole::Pty::ExitStatus );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Pty::finished)) {
                *result = 1;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject Konsole::Pty::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_Konsole__Pty.data,
    qt_meta_data_Konsole__Pty,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Konsole::Pty::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Konsole::Pty::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Konsole__Pty.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int Konsole::Pty::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 6;
    }
    return _id;
}

// SIGNAL 0
void Konsole::Pty::receivedData(const char * _t1, int _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void Konsole::Pty::finished(int _t1, Konsole::Pty::ExitStatus _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
