/****************************************************************************
** Meta object code from reading C++ file 'Vt102Emulation.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.19)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../qtermwidget/Vt102Emulation.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'Vt102Emulation.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.19. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_Konsole__Vt102Emulation_t {
    QByteArrayData data[18];
    char stringdata0[182];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Konsole__Vt102Emulation_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Konsole__Vt102Emulation_t qt_meta_stringdata_Konsole__Vt102Emulation = {
    {
QT_MOC_LITERAL(0, 0, 23), // "Konsole::Vt102Emulation"
QT_MOC_LITERAL(1, 24, 10), // "sendString"
QT_MOC_LITERAL(2, 35, 0), // ""
QT_MOC_LITERAL(3, 36, 11), // "const char*"
QT_MOC_LITERAL(4, 48, 6), // "length"
QT_MOC_LITERAL(5, 55, 8), // "sendText"
QT_MOC_LITERAL(6, 64, 4), // "text"
QT_MOC_LITERAL(7, 69, 12), // "sendKeyEvent"
QT_MOC_LITERAL(8, 82, 10), // "QKeyEvent*"
QT_MOC_LITERAL(9, 93, 9), // "fromPaste"
QT_MOC_LITERAL(10, 103, 14), // "sendMouseEvent"
QT_MOC_LITERAL(11, 118, 7), // "buttons"
QT_MOC_LITERAL(12, 126, 6), // "column"
QT_MOC_LITERAL(13, 133, 4), // "line"
QT_MOC_LITERAL(14, 138, 9), // "eventType"
QT_MOC_LITERAL(15, 148, 9), // "focusLost"
QT_MOC_LITERAL(16, 158, 11), // "focusGained"
QT_MOC_LITERAL(17, 170, 11) // "updateTitle"

    },
    "Konsole::Vt102Emulation\0sendString\0\0"
    "const char*\0length\0sendText\0text\0"
    "sendKeyEvent\0QKeyEvent*\0fromPaste\0"
    "sendMouseEvent\0buttons\0column\0line\0"
    "eventType\0focusLost\0focusGained\0"
    "updateTitle"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Konsole__Vt102Emulation[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       8,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    2,   54,    2, 0x0a /* Public */,
       1,    1,   59,    2, 0x2a /* Public | MethodCloned */,
       5,    1,   62,    2, 0x0a /* Public */,
       7,    2,   65,    2, 0x0a /* Public */,
      10,    4,   70,    2, 0x0a /* Public */,
      15,    0,   79,    2, 0x0a /* Public */,
      16,    0,   80,    2, 0x0a /* Public */,
      17,    0,   81,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 3, QMetaType::Int,    2,    4,
    QMetaType::Void, 0x80000000 | 3,    2,
    QMetaType::Void, QMetaType::QString,    6,
    QMetaType::Void, 0x80000000 | 8, QMetaType::Bool,    2,    9,
    QMetaType::Void, QMetaType::Int, QMetaType::Int, QMetaType::Int, QMetaType::Int,   11,   12,   13,   14,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void Konsole::Vt102Emulation::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<Vt102Emulation *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->sendString((*reinterpret_cast< const char*(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2]))); break;
        case 1: _t->sendString((*reinterpret_cast< const char*(*)>(_a[1]))); break;
        case 2: _t->sendText((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 3: _t->sendKeyEvent((*reinterpret_cast< QKeyEvent*(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
        case 4: _t->sendMouseEvent((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3])),(*reinterpret_cast< int(*)>(_a[4]))); break;
        case 5: _t->focusLost(); break;
        case 6: _t->focusGained(); break;
        case 7: _t->updateTitle(); break;
        default: ;
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject Konsole::Vt102Emulation::staticMetaObject = { {
    QMetaObject::SuperData::link<Emulation::staticMetaObject>(),
    qt_meta_stringdata_Konsole__Vt102Emulation.data,
    qt_meta_data_Konsole__Vt102Emulation,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Konsole::Vt102Emulation::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Konsole::Vt102Emulation::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Konsole__Vt102Emulation.stringdata0))
        return static_cast<void*>(this);
    return Emulation::qt_metacast(_clname);
}

int Konsole::Vt102Emulation::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Emulation::qt_metacall(_c, _id, _a);
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
QT_WARNING_POP
QT_END_MOC_NAMESPACE
