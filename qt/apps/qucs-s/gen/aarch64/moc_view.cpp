/****************************************************************************
** Meta object code from reading C++ file 'view.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.19)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../view.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'view.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.19. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_SchematicView_t {
    QByteArrayData data[17];
    char stringdata0[193];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_SchematicView_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_SchematicView_t qt_meta_stringdata_SchematicView = {
    {
QT_MOC_LITERAL(0, 0, 13), // "SchematicView"
QT_MOC_LITERAL(1, 14, 11), // "toolChanged"
QT_MOC_LITERAL(2, 26, 0), // ""
QT_MOC_LITERAL(3, 27, 4), // "tool"
QT_MOC_LITERAL(4, 32, 16), // "selectionChanged"
QT_MOC_LITERAL(5, 49, 13), // "statusMessage"
QT_MOC_LITERAL(6, 63, 4), // "text"
QT_MOC_LITERAL(7, 68, 6), // "edited"
QT_MOC_LITERAL(8, 75, 13), // "editComponent"
QT_MOC_LITERAL(9, 89, 5), // "index"
QT_MOC_LITERAL(10, 95, 11), // "editDiagram"
QT_MOC_LITERAL(11, 107, 14), // "clearSelection"
QT_MOC_LITERAL(12, 122, 9), // "selectAll"
QT_MOC_LITERAL(13, 132, 15), // "deleteSelection"
QT_MOC_LITERAL(14, 148, 15), // "rotateSelection"
QT_MOC_LITERAL(15, 164, 15), // "mirrorSelection"
QT_MOC_LITERAL(16, 180, 12) // "toggleActive"

    },
    "SchematicView\0toolChanged\0\0tool\0"
    "selectionChanged\0statusMessage\0text\0"
    "edited\0editComponent\0index\0editDiagram\0"
    "clearSelection\0selectAll\0deleteSelection\0"
    "rotateSelection\0mirrorSelection\0"
    "toggleActive"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_SchematicView[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      12,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       6,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   74,    2, 0x06 /* Public */,
       4,    0,   77,    2, 0x06 /* Public */,
       5,    1,   78,    2, 0x06 /* Public */,
       7,    0,   81,    2, 0x06 /* Public */,
       8,    1,   82,    2, 0x06 /* Public */,
      10,    1,   85,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      11,    0,   88,    2, 0x0a /* Public */,
      12,    0,   89,    2, 0x0a /* Public */,
      13,    0,   90,    2, 0x0a /* Public */,
      14,    0,   91,    2, 0x0a /* Public */,
      15,    0,   92,    2, 0x0a /* Public */,
      16,    0,   93,    2, 0x0a /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    6,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    9,
    QMetaType::Void, QMetaType::Int,    9,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void SchematicView::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<SchematicView *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->toolChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 1: _t->selectionChanged(); break;
        case 2: _t->statusMessage((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 3: _t->edited(); break;
        case 4: _t->editComponent((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 5: _t->editDiagram((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 6: _t->clearSelection(); break;
        case 7: _t->selectAll(); break;
        case 8: _t->deleteSelection(); break;
        case 9: _t->rotateSelection(); break;
        case 10: _t->mirrorSelection(); break;
        case 11: _t->toggleActive(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (SchematicView::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SchematicView::toolChanged)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (SchematicView::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SchematicView::selectionChanged)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (SchematicView::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SchematicView::statusMessage)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (SchematicView::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SchematicView::edited)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (SchematicView::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SchematicView::editComponent)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (SchematicView::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SchematicView::editDiagram)) {
                *result = 5;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject SchematicView::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_SchematicView.data,
    qt_meta_data_SchematicView,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *SchematicView::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SchematicView::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_SchematicView.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int SchematicView::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 12)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 12;
    }
    return _id;
}

// SIGNAL 0
void SchematicView::toolChanged(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void SchematicView::selectionChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void SchematicView::statusMessage(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void SchematicView::edited()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void SchematicView::editComponent(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void SchematicView::editDiagram(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
