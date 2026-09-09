/****************************************************************************
** Meta object code from reading C++ file 'termwidget.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.19)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../src/termwidget.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'termwidget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.19. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_TermWidgetImpl_t {
    QByteArrayData data[13];
    char stringdata0[139];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_TermWidgetImpl_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_TermWidgetImpl_t qt_meta_stringdata_TermWidgetImpl = {
    {
QT_MOC_LITERAL(0, 0, 14), // "TermWidgetImpl"
QT_MOC_LITERAL(1, 15, 13), // "renameSession"
QT_MOC_LITERAL(2, 29, 0), // ""
QT_MOC_LITERAL(3, 30, 20), // "removeCurrentSession"
QT_MOC_LITERAL(4, 51, 6), // "zoomIn"
QT_MOC_LITERAL(5, 58, 7), // "zoomOut"
QT_MOC_LITERAL(6, 66, 9), // "zoomReset"
QT_MOC_LITERAL(7, 76, 21), // "customContextMenuCall"
QT_MOC_LITERAL(8, 98, 3), // "pos"
QT_MOC_LITERAL(9, 102, 11), // "activateUrl"
QT_MOC_LITERAL(10, 114, 3), // "url"
QT_MOC_LITERAL(11, 118, 15), // "fromContextMenu"
QT_MOC_LITERAL(12, 134, 4) // "bell"

    },
    "TermWidgetImpl\0renameSession\0\0"
    "removeCurrentSession\0zoomIn\0zoomOut\0"
    "zoomReset\0customContextMenuCall\0pos\0"
    "activateUrl\0url\0fromContextMenu\0bell"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_TermWidgetImpl[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       8,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   54,    2, 0x06 /* Public */,
       3,    0,   55,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       4,    0,   56,    2, 0x0a /* Public */,
       5,    0,   57,    2, 0x0a /* Public */,
       6,    0,   58,    2, 0x0a /* Public */,
       7,    1,   59,    2, 0x0a /* Public */,
       9,    2,   62,    2, 0x08 /* Private */,
      12,    0,   67,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QPoint,    8,
    QMetaType::Void, QMetaType::QUrl, QMetaType::Bool,   10,   11,
    QMetaType::Void,

       0        // eod
};

void TermWidgetImpl::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<TermWidgetImpl *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->renameSession(); break;
        case 1: _t->removeCurrentSession(); break;
        case 2: _t->zoomIn(); break;
        case 3: _t->zoomOut(); break;
        case 4: _t->zoomReset(); break;
        case 5: _t->customContextMenuCall((*reinterpret_cast< const QPoint(*)>(_a[1]))); break;
        case 6: _t->activateUrl((*reinterpret_cast< const QUrl(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
        case 7: _t->bell(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (TermWidgetImpl::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TermWidgetImpl::renameSession)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (TermWidgetImpl::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TermWidgetImpl::removeCurrentSession)) {
                *result = 1;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject TermWidgetImpl::staticMetaObject = { {
    QMetaObject::SuperData::link<QTermWidget::staticMetaObject>(),
    qt_meta_stringdata_TermWidgetImpl.data,
    qt_meta_data_TermWidgetImpl,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *TermWidgetImpl::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *TermWidgetImpl::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_TermWidgetImpl.stringdata0))
        return static_cast<void*>(this);
    return QTermWidget::qt_metacast(_clname);
}

int TermWidgetImpl::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QTermWidget::qt_metacall(_c, _id, _a);
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

// SIGNAL 0
void TermWidgetImpl::renameSession()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void TermWidgetImpl::removeCurrentSession()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}
struct qt_meta_stringdata_TermWidget_t {
    QByteArrayData data[16];
    char stringdata0[199];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_TermWidget_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_TermWidget_t qt_meta_stringdata_TermWidget = {
    {
QT_MOC_LITERAL(0, 0, 10), // "TermWidget"
QT_MOC_LITERAL(1, 11, 8), // "finished"
QT_MOC_LITERAL(2, 20, 0), // ""
QT_MOC_LITERAL(3, 21, 13), // "renameSession"
QT_MOC_LITERAL(4, 35, 20), // "removeCurrentSession"
QT_MOC_LITERAL(5, 56, 15), // "splitHorizontal"
QT_MOC_LITERAL(6, 72, 11), // "TermWidget*"
QT_MOC_LITERAL(7, 84, 4), // "self"
QT_MOC_LITERAL(8, 89, 13), // "splitVertical"
QT_MOC_LITERAL(9, 103, 13), // "splitCollapse"
QT_MOC_LITERAL(10, 117, 12), // "termGetFocus"
QT_MOC_LITERAL(11, 130, 16), // "termTitleChanged"
QT_MOC_LITERAL(12, 147, 9), // "titleText"
QT_MOC_LITERAL(13, 157, 4), // "icon"
QT_MOC_LITERAL(14, 162, 17), // "term_termGetFocus"
QT_MOC_LITERAL(15, 180, 18) // "term_termLostFocus"

    },
    "TermWidget\0finished\0\0renameSession\0"
    "removeCurrentSession\0splitHorizontal\0"
    "TermWidget*\0self\0splitVertical\0"
    "splitCollapse\0termGetFocus\0termTitleChanged\0"
    "titleText\0icon\0term_termGetFocus\0"
    "term_termLostFocus"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_TermWidget[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      10,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       8,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   64,    2, 0x06 /* Public */,
       3,    0,   65,    2, 0x06 /* Public */,
       4,    0,   66,    2, 0x06 /* Public */,
       5,    1,   67,    2, 0x06 /* Public */,
       8,    1,   70,    2, 0x06 /* Public */,
       9,    1,   73,    2, 0x06 /* Public */,
      10,    1,   76,    2, 0x06 /* Public */,
      11,    2,   79,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      14,    0,   84,    2, 0x08 /* Private */,
      15,    0,   85,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 6,    7,
    QMetaType::Void, 0x80000000 | 6,    7,
    QMetaType::Void, 0x80000000 | 6,    7,
    QMetaType::Void, 0x80000000 | 6,    7,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,   12,   13,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void TermWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<TermWidget *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->finished(); break;
        case 1: _t->renameSession(); break;
        case 2: _t->removeCurrentSession(); break;
        case 3: _t->splitHorizontal((*reinterpret_cast< TermWidget*(*)>(_a[1]))); break;
        case 4: _t->splitVertical((*reinterpret_cast< TermWidget*(*)>(_a[1]))); break;
        case 5: _t->splitCollapse((*reinterpret_cast< TermWidget*(*)>(_a[1]))); break;
        case 6: _t->termGetFocus((*reinterpret_cast< TermWidget*(*)>(_a[1]))); break;
        case 7: _t->termTitleChanged((*reinterpret_cast< QString(*)>(_a[1])),(*reinterpret_cast< QString(*)>(_a[2]))); break;
        case 8: _t->term_termGetFocus(); break;
        case 9: _t->term_termLostFocus(); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 3:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< TermWidget* >(); break;
            }
            break;
        case 4:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< TermWidget* >(); break;
            }
            break;
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
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (TermWidget::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TermWidget::finished)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (TermWidget::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TermWidget::renameSession)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (TermWidget::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TermWidget::removeCurrentSession)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (TermWidget::*)(TermWidget * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TermWidget::splitHorizontal)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (TermWidget::*)(TermWidget * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TermWidget::splitVertical)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (TermWidget::*)(TermWidget * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TermWidget::splitCollapse)) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (TermWidget::*)(TermWidget * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TermWidget::termGetFocus)) {
                *result = 6;
                return;
            }
        }
        {
            using _t = void (TermWidget::*)(QString , QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TermWidget::termTitleChanged)) {
                *result = 7;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject TermWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_TermWidget.data,
    qt_meta_data_TermWidget,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *TermWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *TermWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_TermWidget.stringdata0))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "DBusAddressable"))
        return static_cast< DBusAddressable*>(this);
    return QWidget::qt_metacast(_clname);
}

int TermWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 10)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 10;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 10)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 10;
    }
    return _id;
}

// SIGNAL 0
void TermWidget::finished()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void TermWidget::renameSession()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void TermWidget::removeCurrentSession()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void TermWidget::splitHorizontal(TermWidget * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void TermWidget::splitVertical(TermWidget * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void TermWidget::splitCollapse(TermWidget * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void TermWidget::termGetFocus(TermWidget * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void TermWidget::termTitleChanged(QString _t1, QString _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
