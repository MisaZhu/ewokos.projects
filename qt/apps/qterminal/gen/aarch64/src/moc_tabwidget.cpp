/****************************************************************************
** Meta object code from reading C++ file 'tabwidget.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.19)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../src/tabwidget.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'tabwidget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.19. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_TabWidget_t {
    QByteArrayData data[52];
    char stringdata0[773];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_TabWidget_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_TabWidget_t qt_meta_stringdata_TabWidget = {
    {
QT_MOC_LITERAL(0, 0, 9), // "TabWidget"
QT_MOC_LITERAL(1, 10, 24), // "closeLastTabNotification"
QT_MOC_LITERAL(2, 35, 0), // ""
QT_MOC_LITERAL(3, 36, 18), // "tabRenameRequested"
QT_MOC_LITERAL(4, 55, 28), // "tabTitleColorChangeRequested"
QT_MOC_LITERAL(5, 84, 19), // "currentTitleChanged"
QT_MOC_LITERAL(6, 104, 9), // "addNewTab"
QT_MOC_LITERAL(7, 114, 14), // "TerminalConfig"
QT_MOC_LITERAL(8, 129, 3), // "cfg"
QT_MOC_LITERAL(9, 133, 9), // "removeTab"
QT_MOC_LITERAL(10, 143, 9), // "switchTab"
QT_MOC_LITERAL(11, 153, 8), // "onAction"
QT_MOC_LITERAL(12, 162, 16), // "onCurrentChanged"
QT_MOC_LITERAL(13, 179, 16), // "removeCurrentTab"
QT_MOC_LITERAL(14, 196, 13), // "switchToRight"
QT_MOC_LITERAL(15, 210, 12), // "switchToLeft"
QT_MOC_LITERAL(16, 223, 14), // "removeFinished"
QT_MOC_LITERAL(17, 238, 8), // "moveLeft"
QT_MOC_LITERAL(18, 247, 9), // "moveRight"
QT_MOC_LITERAL(19, 257, 13), // "renameSession"
QT_MOC_LITERAL(20, 271, 20), // "renameCurrentSession"
QT_MOC_LITERAL(21, 292, 13), // "setTitleColor"
QT_MOC_LITERAL(22, 306, 21), // "switchLeftSubterminal"
QT_MOC_LITERAL(23, 328, 22), // "switchRightSubterminal"
QT_MOC_LITERAL(24, 351, 20), // "switchTopSubterminal"
QT_MOC_LITERAL(25, 372, 23), // "switchBottomSubterminal"
QT_MOC_LITERAL(26, 396, 17), // "splitHorizontally"
QT_MOC_LITERAL(27, 414, 15), // "splitVertically"
QT_MOC_LITERAL(28, 430, 13), // "splitCollapse"
QT_MOC_LITERAL(29, 444, 13), // "copySelection"
QT_MOC_LITERAL(30, 458, 14), // "pasteClipboard"
QT_MOC_LITERAL(31, 473, 14), // "pasteSelection"
QT_MOC_LITERAL(32, 488, 6), // "zoomIn"
QT_MOC_LITERAL(33, 495, 7), // "zoomOut"
QT_MOC_LITERAL(34, 503, 9), // "zoomReset"
QT_MOC_LITERAL(35, 513, 17), // "changeTabPosition"
QT_MOC_LITERAL(36, 531, 8), // "QAction*"
QT_MOC_LITERAL(37, 540, 20), // "changeScrollPosition"
QT_MOC_LITERAL(38, 561, 25), // "changeKeyboardCursorShape"
QT_MOC_LITERAL(39, 587, 17), // "propertiesChanged"
QT_MOC_LITERAL(40, 605, 19), // "clearActiveTerminal"
QT_MOC_LITERAL(41, 625, 11), // "saveSession"
QT_MOC_LITERAL(42, 637, 11), // "loadSession"
QT_MOC_LITERAL(43, 649, 17), // "preset2Horizontal"
QT_MOC_LITERAL(44, 667, 15), // "preset2Vertical"
QT_MOC_LITERAL(45, 683, 16), // "preset4Terminals"
QT_MOC_LITERAL(46, 700, 12), // "switchToNext"
QT_MOC_LITERAL(47, 713, 12), // "switchToPrev"
QT_MOC_LITERAL(48, 726, 16), // "updateTabIndices"
QT_MOC_LITERAL(49, 743, 18), // "onTermTitleChanged"
QT_MOC_LITERAL(50, 762, 5), // "title"
QT_MOC_LITERAL(51, 768, 4) // "icon"

    },
    "TabWidget\0closeLastTabNotification\0\0"
    "tabRenameRequested\0tabTitleColorChangeRequested\0"
    "currentTitleChanged\0addNewTab\0"
    "TerminalConfig\0cfg\0removeTab\0switchTab\0"
    "onAction\0onCurrentChanged\0removeCurrentTab\0"
    "switchToRight\0switchToLeft\0removeFinished\0"
    "moveLeft\0moveRight\0renameSession\0"
    "renameCurrentSession\0setTitleColor\0"
    "switchLeftSubterminal\0switchRightSubterminal\0"
    "switchTopSubterminal\0switchBottomSubterminal\0"
    "splitHorizontally\0splitVertically\0"
    "splitCollapse\0copySelection\0pasteClipboard\0"
    "pasteSelection\0zoomIn\0zoomOut\0zoomReset\0"
    "changeTabPosition\0QAction*\0"
    "changeScrollPosition\0changeKeyboardCursorShape\0"
    "propertiesChanged\0clearActiveTerminal\0"
    "saveSession\0loadSession\0preset2Horizontal\0"
    "preset2Vertical\0preset4Terminals\0"
    "switchToNext\0switchToPrev\0updateTabIndices\0"
    "onTermTitleChanged\0title\0icon"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_TabWidget[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      45,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       4,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,  239,    2, 0x06 /* Public */,
       3,    1,  240,    2, 0x06 /* Public */,
       4,    1,  243,    2, 0x06 /* Public */,
       5,    1,  246,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       6,    1,  249,    2, 0x0a /* Public */,
       9,    1,  252,    2, 0x0a /* Public */,
      10,    1,  255,    2, 0x0a /* Public */,
      11,    0,  258,    2, 0x0a /* Public */,
      12,    1,  259,    2, 0x0a /* Public */,
      13,    0,  262,    2, 0x0a /* Public */,
      14,    0,  263,    2, 0x0a /* Public */,
      15,    0,  264,    2, 0x0a /* Public */,
      16,    0,  265,    2, 0x0a /* Public */,
      17,    0,  266,    2, 0x0a /* Public */,
      18,    0,  267,    2, 0x0a /* Public */,
      19,    1,  268,    2, 0x0a /* Public */,
      20,    0,  271,    2, 0x0a /* Public */,
      21,    1,  272,    2, 0x0a /* Public */,
      22,    0,  275,    2, 0x0a /* Public */,
      23,    0,  276,    2, 0x0a /* Public */,
      24,    0,  277,    2, 0x0a /* Public */,
      25,    0,  278,    2, 0x0a /* Public */,
      26,    0,  279,    2, 0x0a /* Public */,
      27,    0,  280,    2, 0x0a /* Public */,
      28,    0,  281,    2, 0x0a /* Public */,
      29,    0,  282,    2, 0x0a /* Public */,
      30,    0,  283,    2, 0x0a /* Public */,
      31,    0,  284,    2, 0x0a /* Public */,
      32,    0,  285,    2, 0x0a /* Public */,
      33,    0,  286,    2, 0x0a /* Public */,
      34,    0,  287,    2, 0x0a /* Public */,
      35,    1,  288,    2, 0x0a /* Public */,
      37,    1,  291,    2, 0x0a /* Public */,
      38,    1,  294,    2, 0x0a /* Public */,
      39,    0,  297,    2, 0x0a /* Public */,
      40,    0,  298,    2, 0x0a /* Public */,
      41,    0,  299,    2, 0x0a /* Public */,
      42,    0,  300,    2, 0x0a /* Public */,
      43,    0,  301,    2, 0x0a /* Public */,
      44,    0,  302,    2, 0x0a /* Public */,
      45,    0,  303,    2, 0x0a /* Public */,
      46,    0,  304,    2, 0x0a /* Public */,
      47,    0,  305,    2, 0x0a /* Public */,
      48,    0,  306,    2, 0x09 /* Protected */,
      49,    2,  307,    2, 0x09 /* Protected */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    2,
    QMetaType::Void, QMetaType::Int,    2,
    QMetaType::Void, QMetaType::Int,    2,

 // slots: parameters
    QMetaType::Int, 0x80000000 | 7,    8,
    QMetaType::Void, QMetaType::Int,    2,
    QMetaType::Void, QMetaType::Int,    2,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    2,
    QMetaType::Void,
    QMetaType::Int,
    QMetaType::Int,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    2,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    2,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 36,    2,
    QMetaType::Void, 0x80000000 | 36,    2,
    QMetaType::Void, 0x80000000 | 36,    2,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,   50,   51,

       0        // eod
};

void TabWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<TabWidget *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->closeLastTabNotification(); break;
        case 1: _t->tabRenameRequested((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 2: _t->tabTitleColorChangeRequested((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 3: _t->currentTitleChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 4: { int _r = _t->addNewTab((*reinterpret_cast< TerminalConfig(*)>(_a[1])));
            if (_a[0]) *reinterpret_cast< int*>(_a[0]) = std::move(_r); }  break;
        case 5: _t->removeTab((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 6: _t->switchTab((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 7: _t->onAction(); break;
        case 8: _t->onCurrentChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 9: _t->removeCurrentTab(); break;
        case 10: { int _r = _t->switchToRight();
            if (_a[0]) *reinterpret_cast< int*>(_a[0]) = std::move(_r); }  break;
        case 11: { int _r = _t->switchToLeft();
            if (_a[0]) *reinterpret_cast< int*>(_a[0]) = std::move(_r); }  break;
        case 12: _t->removeFinished(); break;
        case 13: _t->moveLeft(); break;
        case 14: _t->moveRight(); break;
        case 15: _t->renameSession((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 16: _t->renameCurrentSession(); break;
        case 17: _t->setTitleColor((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 18: _t->switchLeftSubterminal(); break;
        case 19: _t->switchRightSubterminal(); break;
        case 20: _t->switchTopSubterminal(); break;
        case 21: _t->switchBottomSubterminal(); break;
        case 22: _t->splitHorizontally(); break;
        case 23: _t->splitVertically(); break;
        case 24: _t->splitCollapse(); break;
        case 25: _t->copySelection(); break;
        case 26: _t->pasteClipboard(); break;
        case 27: _t->pasteSelection(); break;
        case 28: _t->zoomIn(); break;
        case 29: _t->zoomOut(); break;
        case 30: _t->zoomReset(); break;
        case 31: _t->changeTabPosition((*reinterpret_cast< QAction*(*)>(_a[1]))); break;
        case 32: _t->changeScrollPosition((*reinterpret_cast< QAction*(*)>(_a[1]))); break;
        case 33: _t->changeKeyboardCursorShape((*reinterpret_cast< QAction*(*)>(_a[1]))); break;
        case 34: _t->propertiesChanged(); break;
        case 35: _t->clearActiveTerminal(); break;
        case 36: _t->saveSession(); break;
        case 37: _t->loadSession(); break;
        case 38: _t->preset2Horizontal(); break;
        case 39: _t->preset2Vertical(); break;
        case 40: _t->preset4Terminals(); break;
        case 41: _t->switchToNext(); break;
        case 42: _t->switchToPrev(); break;
        case 43: _t->updateTabIndices(); break;
        case 44: _t->onTermTitleChanged((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 31:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< QAction* >(); break;
            }
            break;
        case 32:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< QAction* >(); break;
            }
            break;
        case 33:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< QAction* >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (TabWidget::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TabWidget::closeLastTabNotification)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (TabWidget::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TabWidget::tabRenameRequested)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (TabWidget::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TabWidget::tabTitleColorChangeRequested)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (TabWidget::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TabWidget::currentTitleChanged)) {
                *result = 3;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject TabWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QTabWidget::staticMetaObject>(),
    qt_meta_stringdata_TabWidget.data,
    qt_meta_data_TabWidget,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *TabWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *TabWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_TabWidget.stringdata0))
        return static_cast<void*>(this);
    return QTabWidget::qt_metacast(_clname);
}

int TabWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QTabWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 45)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 45;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 45)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 45;
    }
    return _id;
}

// SIGNAL 0
void TabWidget::closeLastTabNotification()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void TabWidget::tabRenameRequested(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void TabWidget::tabTitleColorChangeRequested(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void TabWidget::currentTitleChanged(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
