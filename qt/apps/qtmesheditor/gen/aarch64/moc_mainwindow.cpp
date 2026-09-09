/****************************************************************************
** Meta object code from reading C++ file 'mainwindow.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.19)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../mainwindow.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'mainwindow.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.19. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_MainWindow_t {
    QByteArrayData data[19];
    char stringdata0[215];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_MainWindow_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_MainWindow_t qt_meta_stringdata_MainWindow = {
    {
QT_MOC_LITERAL(0, 0, 10), // "MainWindow"
QT_MOC_LITERAL(1, 11, 7), // "fileNew"
QT_MOC_LITERAL(2, 19, 0), // ""
QT_MOC_LITERAL(3, 20, 10), // "fileImport"
QT_MOC_LITERAL(4, 31, 10), // "fileExport"
QT_MOC_LITERAL(5, 42, 12), // "addPrimitive"
QT_MOC_LITERAL(6, 55, 17), // "duplicateSelected"
QT_MOC_LITERAL(7, 73, 14), // "deleteSelected"
QT_MOC_LITERAL(8, 88, 12), // "setDrawStyle"
QT_MOC_LITERAL(9, 101, 10), // "toggleGrid"
QT_MOC_LITERAL(10, 112, 2), // "on"
QT_MOC_LITERAL(11, 115, 7), // "stdView"
QT_MOC_LITERAL(12, 123, 5), // "about"
QT_MOC_LITERAL(13, 129, 11), // "rebuildTree"
QT_MOC_LITERAL(14, 141, 20), // "treeSelectionChanged"
QT_MOC_LITERAL(15, 162, 24), // "viewportSelectionChanged"
QT_MOC_LITERAL(16, 187, 11), // "MeshEntity*"
QT_MOC_LITERAL(17, 199, 3), // "ent"
QT_MOC_LITERAL(18, 203, 11) // "nameChanged"

    },
    "MainWindow\0fileNew\0\0fileImport\0"
    "fileExport\0addPrimitive\0duplicateSelected\0"
    "deleteSelected\0setDrawStyle\0toggleGrid\0"
    "on\0stdView\0about\0rebuildTree\0"
    "treeSelectionChanged\0viewportSelectionChanged\0"
    "MeshEntity*\0ent\0nameChanged"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_MainWindow[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      14,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    0,   84,    2, 0x08 /* Private */,
       3,    0,   85,    2, 0x08 /* Private */,
       4,    0,   86,    2, 0x08 /* Private */,
       5,    0,   87,    2, 0x08 /* Private */,
       6,    0,   88,    2, 0x08 /* Private */,
       7,    0,   89,    2, 0x08 /* Private */,
       8,    0,   90,    2, 0x08 /* Private */,
       9,    1,   91,    2, 0x08 /* Private */,
      11,    0,   94,    2, 0x08 /* Private */,
      12,    0,   95,    2, 0x08 /* Private */,
      13,    0,   96,    2, 0x08 /* Private */,
      14,    0,   97,    2, 0x08 /* Private */,
      15,    1,   98,    2, 0x08 /* Private */,
      18,    1,  101,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,   10,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 16,   17,
    QMetaType::Void, 0x80000000 | 16,   17,

       0        // eod
};

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<MainWindow *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->fileNew(); break;
        case 1: _t->fileImport(); break;
        case 2: _t->fileExport(); break;
        case 3: _t->addPrimitive(); break;
        case 4: _t->duplicateSelected(); break;
        case 5: _t->deleteSelected(); break;
        case 6: _t->setDrawStyle(); break;
        case 7: _t->toggleGrid((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 8: _t->stdView(); break;
        case 9: _t->about(); break;
        case 10: _t->rebuildTree(); break;
        case 11: _t->treeSelectionChanged(); break;
        case 12: _t->viewportSelectionChanged((*reinterpret_cast< MeshEntity*(*)>(_a[1]))); break;
        case 13: _t->nameChanged((*reinterpret_cast< MeshEntity*(*)>(_a[1]))); break;
        default: ;
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_meta_stringdata_MainWindow.data,
    qt_meta_data_MainWindow,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_MainWindow.stringdata0))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 14)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 14;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
