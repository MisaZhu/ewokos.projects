/****************************************************************************
** Meta object code from reading C++ file 'termwidgetholder.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.19)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../src/termwidgetholder.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'termwidgetholder.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.19. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_TermWidgetHolder_t {
    QByteArrayData data[23];
    char stringdata0[299];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_TermWidgetHolder_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_TermWidgetHolder_t qt_meta_stringdata_TermWidgetHolder = {
    {
QT_MOC_LITERAL(0, 0, 16), // "TermWidgetHolder"
QT_MOC_LITERAL(1, 17, 8), // "finished"
QT_MOC_LITERAL(2, 26, 0), // ""
QT_MOC_LITERAL(3, 27, 18), // "lastTerminalClosed"
QT_MOC_LITERAL(4, 46, 13), // "renameSession"
QT_MOC_LITERAL(5, 60, 16), // "termTitleChanged"
QT_MOC_LITERAL(6, 77, 5), // "title"
QT_MOC_LITERAL(7, 83, 4), // "icon"
QT_MOC_LITERAL(8, 88, 16), // "termFocusChanged"
QT_MOC_LITERAL(9, 105, 15), // "splitHorizontal"
QT_MOC_LITERAL(10, 121, 11), // "TermWidget*"
QT_MOC_LITERAL(11, 133, 4), // "term"
QT_MOC_LITERAL(12, 138, 13), // "splitVertical"
QT_MOC_LITERAL(13, 152, 13), // "splitCollapse"
QT_MOC_LITERAL(14, 166, 7), // "setWDir"
QT_MOC_LITERAL(15, 174, 4), // "wdir"
QT_MOC_LITERAL(16, 179, 21), // "directionalNavigation"
QT_MOC_LITERAL(17, 201, 19), // "NavigationDirection"
QT_MOC_LITERAL(18, 221, 3), // "dir"
QT_MOC_LITERAL(19, 225, 19), // "clearActiveTerminal"
QT_MOC_LITERAL(20, 245, 18), // "onTermTitleChanged"
QT_MOC_LITERAL(21, 264, 18), // "setCurrentTerminal"
QT_MOC_LITERAL(22, 283, 15) // "handle_finished"

    },
    "TermWidgetHolder\0finished\0\0"
    "lastTerminalClosed\0renameSession\0"
    "termTitleChanged\0title\0icon\0"
    "termFocusChanged\0splitHorizontal\0"
    "TermWidget*\0term\0splitVertical\0"
    "splitCollapse\0setWDir\0wdir\0"
    "directionalNavigation\0NavigationDirection\0"
    "dir\0clearActiveTerminal\0onTermTitleChanged\0"
    "setCurrentTerminal\0handle_finished"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_TermWidgetHolder[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      14,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       5,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   84,    2, 0x06 /* Public */,
       3,    0,   85,    2, 0x06 /* Public */,
       4,    0,   86,    2, 0x06 /* Public */,
       5,    2,   87,    2, 0x06 /* Public */,
       8,    0,   92,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       9,    1,   93,    2, 0x0a /* Public */,
      12,    1,   96,    2, 0x0a /* Public */,
      13,    1,   99,    2, 0x0a /* Public */,
      14,    1,  102,    2, 0x0a /* Public */,
      16,    1,  105,    2, 0x0a /* Public */,
      19,    0,  108,    2, 0x0a /* Public */,
      20,    2,  109,    2, 0x0a /* Public */,
      21,    1,  114,    2, 0x08 /* Private */,
      22,    0,  117,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,    6,    7,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 10,   11,
    QMetaType::Void, 0x80000000 | 10,   11,
    QMetaType::Void, 0x80000000 | 10,   11,
    QMetaType::Void, QMetaType::QString,   15,
    QMetaType::Void, 0x80000000 | 17,   18,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,    6,    7,
    QMetaType::Void, 0x80000000 | 10,   11,
    QMetaType::Void,

       0        // eod
};

void TermWidgetHolder::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<TermWidgetHolder *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->finished(); break;
        case 1: _t->lastTerminalClosed(); break;
        case 2: _t->renameSession(); break;
        case 3: _t->termTitleChanged((*reinterpret_cast< QString(*)>(_a[1])),(*reinterpret_cast< QString(*)>(_a[2]))); break;
        case 4: _t->termFocusChanged(); break;
        case 5: _t->splitHorizontal((*reinterpret_cast< TermWidget*(*)>(_a[1]))); break;
        case 6: _t->splitVertical((*reinterpret_cast< TermWidget*(*)>(_a[1]))); break;
        case 7: _t->splitCollapse((*reinterpret_cast< TermWidget*(*)>(_a[1]))); break;
        case 8: _t->setWDir((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 9: _t->directionalNavigation((*reinterpret_cast< NavigationDirection(*)>(_a[1]))); break;
        case 10: _t->clearActiveTerminal(); break;
        case 11: _t->onTermTitleChanged((*reinterpret_cast< QString(*)>(_a[1])),(*reinterpret_cast< QString(*)>(_a[2]))); break;
        case 12: _t->setCurrentTerminal((*reinterpret_cast< TermWidget*(*)>(_a[1]))); break;
        case 13: _t->handle_finished(); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 5:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< TermWidget* >(); break;
            }
            break;
        case 6:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< TermWidget* >(); break;
            }
            break;
        case 7:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< TermWidget* >(); break;
            }
            break;
        case 12:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< TermWidget* >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (TermWidgetHolder::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TermWidgetHolder::finished)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (TermWidgetHolder::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TermWidgetHolder::lastTerminalClosed)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (TermWidgetHolder::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TermWidgetHolder::renameSession)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (TermWidgetHolder::*)(QString , QString ) const;
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TermWidgetHolder::termTitleChanged)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (TermWidgetHolder::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TermWidgetHolder::termFocusChanged)) {
                *result = 4;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject TermWidgetHolder::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_TermWidgetHolder.data,
    qt_meta_data_TermWidgetHolder,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *TermWidgetHolder::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *TermWidgetHolder::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_TermWidgetHolder.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int TermWidgetHolder::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    }
    return _id;
}

// SIGNAL 0
void TermWidgetHolder::finished()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void TermWidgetHolder::lastTerminalClosed()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void TermWidgetHolder::renameSession()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void TermWidgetHolder::termTitleChanged(QString _t1, QString _t2)const
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(const_cast< TermWidgetHolder *>(this), &staticMetaObject, 3, _a);
}

// SIGNAL 4
void TermWidgetHolder::termFocusChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
