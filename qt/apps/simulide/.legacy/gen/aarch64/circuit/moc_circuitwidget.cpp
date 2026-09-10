/****************************************************************************
** Meta object code from reading C++ file 'circuitwidget.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.19)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../circuit/circuitwidget.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'circuitwidget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.19. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_CircuitWidget_t {
    QByteArrayData data[30];
    char stringdata0[301];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_CircuitWidget_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_CircuitWidget_t qt_meta_stringdata_CircuitWidget = {
    {
QT_MOC_LITERAL(0, 0, 13), // "CircuitWidget"
QT_MOC_LITERAL(1, 14, 15), // "fileNameChanged"
QT_MOC_LITERAL(2, 30, 0), // ""
QT_MOC_LITERAL(3, 31, 4), // "path"
QT_MOC_LITERAL(4, 36, 15), // "modifiedChanged"
QT_MOC_LITERAL(5, 52, 8), // "modified"
QT_MOC_LITERAL(6, 61, 16), // "simuStateChanged"
QT_MOC_LITERAL(7, 78, 7), // "running"
QT_MOC_LITERAL(8, 86, 13), // "statusMessage"
QT_MOC_LITERAL(9, 100, 3), // "msg"
QT_MOC_LITERAL(10, 104, 14), // "propsRequested"
QT_MOC_LITERAL(11, 119, 10), // "Component*"
QT_MOC_LITERAL(12, 130, 4), // "comp"
QT_MOC_LITERAL(13, 135, 10), // "newCircuit"
QT_MOC_LITERAL(14, 146, 11), // "openCircuit"
QT_MOC_LITERAL(15, 158, 11), // "saveCircuit"
QT_MOC_LITERAL(16, 170, 13), // "saveCircuitAs"
QT_MOC_LITERAL(17, 184, 3), // "run"
QT_MOC_LITERAL(18, 188, 5), // "pause"
QT_MOC_LITERAL(19, 194, 4), // "stop"
QT_MOC_LITERAL(20, 199, 4), // "step"
QT_MOC_LITERAL(21, 204, 13), // "slotSimuState"
QT_MOC_LITERAL(22, 218, 12), // "slotStepDone"
QT_MOC_LITERAL(23, 231, 15), // "slotSpeedSlider"
QT_MOC_LITERAL(24, 247, 7), // "percent"
QT_MOC_LITERAL(25, 255, 12), // "slotRateSpin"
QT_MOC_LITERAL(26, 268, 4), // "rate"
QT_MOC_LITERAL(27, 273, 11), // "slotAnimate"
QT_MOC_LITERAL(28, 285, 2), // "on"
QT_MOC_LITERAL(29, 288, 12) // "slotModified"

    },
    "CircuitWidget\0fileNameChanged\0\0path\0"
    "modifiedChanged\0modified\0simuStateChanged\0"
    "running\0statusMessage\0msg\0propsRequested\0"
    "Component*\0comp\0newCircuit\0openCircuit\0"
    "saveCircuit\0saveCircuitAs\0run\0pause\0"
    "stop\0step\0slotSimuState\0slotStepDone\0"
    "slotSpeedSlider\0percent\0slotRateSpin\0"
    "rate\0slotAnimate\0on\0slotModified"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_CircuitWidget[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      20,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       5,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,  114,    2, 0x06 /* Public */,
       4,    1,  117,    2, 0x06 /* Public */,
       6,    1,  120,    2, 0x06 /* Public */,
       8,    1,  123,    2, 0x06 /* Public */,
      10,    1,  126,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      13,    0,  129,    2, 0x0a /* Public */,
      14,    0,  130,    2, 0x0a /* Public */,
      14,    1,  131,    2, 0x0a /* Public */,
      15,    0,  134,    2, 0x0a /* Public */,
      16,    0,  135,    2, 0x0a /* Public */,
      17,    0,  136,    2, 0x0a /* Public */,
      18,    0,  137,    2, 0x0a /* Public */,
      19,    0,  138,    2, 0x0a /* Public */,
      20,    0,  139,    2, 0x0a /* Public */,
      21,    1,  140,    2, 0x08 /* Private */,
      22,    0,  143,    2, 0x08 /* Private */,
      23,    1,  144,    2, 0x08 /* Private */,
      25,    1,  147,    2, 0x08 /* Private */,
      27,    1,  150,    2, 0x08 /* Private */,
      29,    1,  153,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::Bool,    5,
    QMetaType::Void, QMetaType::Bool,    7,
    QMetaType::Void, QMetaType::QString,    9,
    QMetaType::Void, 0x80000000 | 11,   12,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Bool,
    QMetaType::Bool, QMetaType::QString,    3,
    QMetaType::Bool,
    QMetaType::Bool,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,    7,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   24,
    QMetaType::Void, QMetaType::Int,   26,
    QMetaType::Void, QMetaType::Bool,   28,
    QMetaType::Void, QMetaType::Bool,    5,

       0        // eod
};

void CircuitWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<CircuitWidget *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->fileNameChanged((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 1: _t->modifiedChanged((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 2: _t->simuStateChanged((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 3: _t->statusMessage((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 4: _t->propsRequested((*reinterpret_cast< Component*(*)>(_a[1]))); break;
        case 5: _t->newCircuit(); break;
        case 6: { bool _r = _t->openCircuit();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 7: { bool _r = _t->openCircuit((*reinterpret_cast< const QString(*)>(_a[1])));
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 8: { bool _r = _t->saveCircuit();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 9: { bool _r = _t->saveCircuitAs();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 10: _t->run(); break;
        case 11: _t->pause(); break;
        case 12: _t->stop(); break;
        case 13: _t->step(); break;
        case 14: _t->slotSimuState((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 15: _t->slotStepDone(); break;
        case 16: _t->slotSpeedSlider((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 17: _t->slotRateSpin((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 18: _t->slotAnimate((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 19: _t->slotModified((*reinterpret_cast< bool(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (CircuitWidget::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CircuitWidget::fileNameChanged)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (CircuitWidget::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CircuitWidget::modifiedChanged)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (CircuitWidget::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CircuitWidget::simuStateChanged)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (CircuitWidget::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CircuitWidget::statusMessage)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (CircuitWidget::*)(Component * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CircuitWidget::propsRequested)) {
                *result = 4;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject CircuitWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_CircuitWidget.data,
    qt_meta_data_CircuitWidget,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *CircuitWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *CircuitWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_CircuitWidget.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int CircuitWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 20)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 20;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 20)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 20;
    }
    return _id;
}

// SIGNAL 0
void CircuitWidget::fileNameChanged(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void CircuitWidget::modifiedChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void CircuitWidget::simuStateChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void CircuitWidget::statusMessage(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void CircuitWidget::propsRequested(Component * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
