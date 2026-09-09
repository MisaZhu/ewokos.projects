/****************************************************************************
** Meta object code from reading C++ file 'ScreenWindow.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.19)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../qtermwidget/ScreenWindow.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'ScreenWindow.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.19. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_Konsole__ScreenWindow_t {
    QByteArrayData data[11];
    char stringdata0[162];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Konsole__ScreenWindow_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Konsole__ScreenWindow_t qt_meta_stringdata_Konsole__ScreenWindow = {
    {
QT_MOC_LITERAL(0, 0, 21), // "Konsole::ScreenWindow"
QT_MOC_LITERAL(1, 22, 13), // "outputChanged"
QT_MOC_LITERAL(2, 36, 0), // ""
QT_MOC_LITERAL(3, 37, 8), // "scrolled"
QT_MOC_LITERAL(4, 46, 4), // "line"
QT_MOC_LITERAL(5, 51, 16), // "selectionChanged"
QT_MOC_LITERAL(6, 68, 11), // "scrollToEnd"
QT_MOC_LITERAL(7, 80, 19), // "notifyOutputChanged"
QT_MOC_LITERAL(8, 100, 25), // "handleCommandFromKeyboard"
QT_MOC_LITERAL(9, 126, 27), // "KeyboardTranslator::Command"
QT_MOC_LITERAL(10, 154, 7) // "command"

    },
    "Konsole::ScreenWindow\0outputChanged\0"
    "\0scrolled\0line\0selectionChanged\0"
    "scrollToEnd\0notifyOutputChanged\0"
    "handleCommandFromKeyboard\0"
    "KeyboardTranslator::Command\0command"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Konsole__ScreenWindow[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       6,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       4,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   44,    2, 0x06 /* Public */,
       3,    1,   45,    2, 0x06 /* Public */,
       5,    0,   48,    2, 0x06 /* Public */,
       6,    0,   49,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       7,    0,   50,    2, 0x0a /* Public */,
       8,    1,   51,    2, 0x0a /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    4,
    QMetaType::Void,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 9,   10,

       0        // eod
};

void Konsole::ScreenWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ScreenWindow *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->outputChanged(); break;
        case 1: _t->scrolled((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 2: _t->selectionChanged(); break;
        case 3: _t->scrollToEnd(); break;
        case 4: _t->notifyOutputChanged(); break;
        case 5: _t->handleCommandFromKeyboard((*reinterpret_cast< KeyboardTranslator::Command(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (ScreenWindow::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ScreenWindow::outputChanged)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (ScreenWindow::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ScreenWindow::scrolled)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (ScreenWindow::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ScreenWindow::selectionChanged)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (ScreenWindow::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ScreenWindow::scrollToEnd)) {
                *result = 3;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject Konsole::ScreenWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_Konsole__ScreenWindow.data,
    qt_meta_data_Konsole__ScreenWindow,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Konsole::ScreenWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Konsole::ScreenWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Konsole__ScreenWindow.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int Konsole::ScreenWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
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
void Konsole::ScreenWindow::outputChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void Konsole::ScreenWindow::scrolled(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void Konsole::ScreenWindow::selectionChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void Konsole::ScreenWindow::scrollToEnd()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
