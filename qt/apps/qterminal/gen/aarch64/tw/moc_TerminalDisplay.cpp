/****************************************************************************
** Meta object code from reading C++ file 'TerminalDisplay.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.19)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../qtermwidget/TerminalDisplay.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'TerminalDisplay.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.19. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_Konsole__TerminalDisplay_t {
    QByteArrayData data[57];
    char stringdata0[776];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Konsole__TerminalDisplay_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Konsole__TerminalDisplay_t qt_meta_stringdata_Konsole__TerminalDisplay = {
    {
QT_MOC_LITERAL(0, 0, 24), // "Konsole::TerminalDisplay"
QT_MOC_LITERAL(1, 25, 16), // "keyPressedSignal"
QT_MOC_LITERAL(2, 42, 0), // ""
QT_MOC_LITERAL(3, 43, 10), // "QKeyEvent*"
QT_MOC_LITERAL(4, 54, 1), // "e"
QT_MOC_LITERAL(5, 56, 9), // "fromPaste"
QT_MOC_LITERAL(6, 66, 11), // "mouseSignal"
QT_MOC_LITERAL(7, 78, 6), // "button"
QT_MOC_LITERAL(8, 85, 6), // "column"
QT_MOC_LITERAL(9, 92, 4), // "line"
QT_MOC_LITERAL(10, 97, 9), // "eventType"
QT_MOC_LITERAL(11, 107, 23), // "changedFontMetricSignal"
QT_MOC_LITERAL(12, 131, 6), // "height"
QT_MOC_LITERAL(13, 138, 5), // "width"
QT_MOC_LITERAL(14, 144, 24), // "changedContentSizeSignal"
QT_MOC_LITERAL(15, 169, 16), // "configureRequest"
QT_MOC_LITERAL(16, 186, 8), // "position"
QT_MOC_LITERAL(17, 195, 21), // "overrideShortcutCheck"
QT_MOC_LITERAL(18, 217, 8), // "keyEvent"
QT_MOC_LITERAL(19, 226, 5), // "bool&"
QT_MOC_LITERAL(20, 232, 8), // "override"
QT_MOC_LITERAL(21, 241, 15), // "isBusySelecting"
QT_MOC_LITERAL(22, 257, 15), // "sendStringToEmu"
QT_MOC_LITERAL(23, 273, 11), // "const char*"
QT_MOC_LITERAL(24, 285, 13), // "copyAvailable"
QT_MOC_LITERAL(25, 299, 12), // "termGetFocus"
QT_MOC_LITERAL(26, 312, 13), // "termLostFocus"
QT_MOC_LITERAL(27, 326, 10), // "notifyBell"
QT_MOC_LITERAL(28, 337, 16), // "usesMouseChanged"
QT_MOC_LITERAL(29, 354, 11), // "updateImage"
QT_MOC_LITERAL(30, 366, 13), // "updateFilters"
QT_MOC_LITERAL(31, 380, 20), // "updateLineProperties"
QT_MOC_LITERAL(32, 401, 13), // "copyClipboard"
QT_MOC_LITERAL(33, 415, 14), // "pasteClipboard"
QT_MOC_LITERAL(34, 430, 14), // "pasteSelection"
QT_MOC_LITERAL(35, 445, 28), // "setFlowControlWarningEnabled"
QT_MOC_LITERAL(36, 474, 7), // "enabled"
QT_MOC_LITERAL(37, 482, 25), // "flowControlWarningEnabled"
QT_MOC_LITERAL(38, 508, 15), // "outputSuspended"
QT_MOC_LITERAL(39, 524, 9), // "suspended"
QT_MOC_LITERAL(40, 534, 12), // "setUsesMouse"
QT_MOC_LITERAL(41, 547, 9), // "usesMouse"
QT_MOC_LITERAL(42, 557, 21), // "setBracketedPasteMode"
QT_MOC_LITERAL(43, 579, 18), // "bracketedPasteMode"
QT_MOC_LITERAL(44, 598, 4), // "bell"
QT_MOC_LITERAL(45, 603, 7), // "message"
QT_MOC_LITERAL(46, 611, 18), // "setBackgroundColor"
QT_MOC_LITERAL(47, 630, 5), // "color"
QT_MOC_LITERAL(48, 636, 18), // "setForegroundColor"
QT_MOC_LITERAL(49, 655, 16), // "selectionChanged"
QT_MOC_LITERAL(50, 672, 24), // "scrollBarPositionChanged"
QT_MOC_LITERAL(51, 697, 5), // "value"
QT_MOC_LITERAL(52, 703, 10), // "blinkEvent"
QT_MOC_LITERAL(53, 714, 16), // "blinkCursorEvent"
QT_MOC_LITERAL(54, 731, 10), // "enableBell"
QT_MOC_LITERAL(55, 742, 14), // "swapColorTable"
QT_MOC_LITERAL(56, 757, 18) // "tripleClickTimeout"

    },
    "Konsole::TerminalDisplay\0keyPressedSignal\0"
    "\0QKeyEvent*\0e\0fromPaste\0mouseSignal\0"
    "button\0column\0line\0eventType\0"
    "changedFontMetricSignal\0height\0width\0"
    "changedContentSizeSignal\0configureRequest\0"
    "position\0overrideShortcutCheck\0keyEvent\0"
    "bool&\0override\0isBusySelecting\0"
    "sendStringToEmu\0const char*\0copyAvailable\0"
    "termGetFocus\0termLostFocus\0notifyBell\0"
    "usesMouseChanged\0updateImage\0updateFilters\0"
    "updateLineProperties\0copyClipboard\0"
    "pasteClipboard\0pasteSelection\0"
    "setFlowControlWarningEnabled\0enabled\0"
    "flowControlWarningEnabled\0outputSuspended\0"
    "suspended\0setUsesMouse\0usesMouse\0"
    "setBracketedPasteMode\0bracketedPasteMode\0"
    "bell\0message\0setBackgroundColor\0color\0"
    "setForegroundColor\0selectionChanged\0"
    "scrollBarPositionChanged\0value\0"
    "blinkEvent\0blinkCursorEvent\0enableBell\0"
    "swapColorTable\0tripleClickTimeout"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Konsole__TerminalDisplay[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      36,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      13,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    2,  194,    2, 0x06 /* Public */,
       6,    4,  199,    2, 0x06 /* Public */,
      11,    2,  208,    2, 0x06 /* Public */,
      14,    2,  213,    2, 0x06 /* Public */,
      15,    1,  218,    2, 0x06 /* Public */,
      17,    2,  221,    2, 0x06 /* Public */,
      21,    1,  226,    2, 0x06 /* Public */,
      22,    1,  229,    2, 0x06 /* Public */,
      24,    1,  232,    2, 0x06 /* Public */,
      25,    0,  235,    2, 0x06 /* Public */,
      26,    0,  236,    2, 0x06 /* Public */,
      27,    1,  237,    2, 0x06 /* Public */,
      28,    0,  240,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      29,    0,  241,    2, 0x0a /* Public */,
      30,    0,  242,    2, 0x0a /* Public */,
      31,    0,  243,    2, 0x0a /* Public */,
      32,    0,  244,    2, 0x0a /* Public */,
      33,    0,  245,    2, 0x0a /* Public */,
      34,    0,  246,    2, 0x0a /* Public */,
      35,    1,  247,    2, 0x0a /* Public */,
      37,    0,  250,    2, 0x0a /* Public */,
      38,    1,  251,    2, 0x0a /* Public */,
      40,    1,  254,    2, 0x0a /* Public */,
      41,    0,  257,    2, 0x0a /* Public */,
      42,    1,  258,    2, 0x0a /* Public */,
      43,    0,  261,    2, 0x0a /* Public */,
      44,    1,  262,    2, 0x0a /* Public */,
      46,    1,  265,    2, 0x0a /* Public */,
      48,    1,  268,    2, 0x0a /* Public */,
      49,    0,  271,    2, 0x0a /* Public */,
      50,    1,  272,    2, 0x09 /* Protected */,
      52,    0,  275,    2, 0x09 /* Protected */,
      53,    0,  276,    2, 0x09 /* Protected */,
      54,    0,  277,    2, 0x09 /* Protected */,
      55,    0,  278,    2, 0x08 /* Private */,
      56,    0,  279,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3, QMetaType::Bool,    4,    5,
    QMetaType::Void, QMetaType::Int, QMetaType::Int, QMetaType::Int, QMetaType::Int,    7,    8,    9,   10,
    QMetaType::Void, QMetaType::Int, QMetaType::Int,   12,   13,
    QMetaType::Void, QMetaType::Int, QMetaType::Int,   12,   13,
    QMetaType::Void, QMetaType::QPoint,   16,
    QMetaType::Void, 0x80000000 | 3, 0x80000000 | 19,   18,   20,
    QMetaType::Void, QMetaType::Bool,    2,
    QMetaType::Void, 0x80000000 | 23,    2,
    QMetaType::Void, QMetaType::Bool,    2,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    2,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,   36,
    QMetaType::Bool,
    QMetaType::Void, QMetaType::Bool,   39,
    QMetaType::Void, QMetaType::Bool,   41,
    QMetaType::Bool,
    QMetaType::Void, QMetaType::Bool,   43,
    QMetaType::Bool,
    QMetaType::Void, QMetaType::QString,   45,
    QMetaType::Void, QMetaType::QColor,   47,
    QMetaType::Void, QMetaType::QColor,   47,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   51,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void Konsole::TerminalDisplay::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<TerminalDisplay *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->keyPressedSignal((*reinterpret_cast< QKeyEvent*(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
        case 1: _t->mouseSignal((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3])),(*reinterpret_cast< int(*)>(_a[4]))); break;
        case 2: _t->changedFontMetricSignal((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2]))); break;
        case 3: _t->changedContentSizeSignal((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2]))); break;
        case 4: _t->configureRequest((*reinterpret_cast< const QPoint(*)>(_a[1]))); break;
        case 5: _t->overrideShortcutCheck((*reinterpret_cast< QKeyEvent*(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
        case 6: _t->isBusySelecting((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 7: _t->sendStringToEmu((*reinterpret_cast< const char*(*)>(_a[1]))); break;
        case 8: _t->copyAvailable((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 9: _t->termGetFocus(); break;
        case 10: _t->termLostFocus(); break;
        case 11: _t->notifyBell((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 12: _t->usesMouseChanged(); break;
        case 13: _t->updateImage(); break;
        case 14: _t->updateFilters(); break;
        case 15: _t->updateLineProperties(); break;
        case 16: _t->copyClipboard(); break;
        case 17: _t->pasteClipboard(); break;
        case 18: _t->pasteSelection(); break;
        case 19: _t->setFlowControlWarningEnabled((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 20: { bool _r = _t->flowControlWarningEnabled();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 21: _t->outputSuspended((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 22: _t->setUsesMouse((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 23: { bool _r = _t->usesMouse();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 24: _t->setBracketedPasteMode((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 25: { bool _r = _t->bracketedPasteMode();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 26: _t->bell((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 27: _t->setBackgroundColor((*reinterpret_cast< const QColor(*)>(_a[1]))); break;
        case 28: _t->setForegroundColor((*reinterpret_cast< const QColor(*)>(_a[1]))); break;
        case 29: _t->selectionChanged(); break;
        case 30: _t->scrollBarPositionChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 31: _t->blinkEvent(); break;
        case 32: _t->blinkCursorEvent(); break;
        case 33: _t->enableBell(); break;
        case 34: _t->swapColorTable(); break;
        case 35: _t->tripleClickTimeout(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (TerminalDisplay::*)(QKeyEvent * , bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TerminalDisplay::keyPressedSignal)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (TerminalDisplay::*)(int , int , int , int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TerminalDisplay::mouseSignal)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (TerminalDisplay::*)(int , int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TerminalDisplay::changedFontMetricSignal)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (TerminalDisplay::*)(int , int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TerminalDisplay::changedContentSizeSignal)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (TerminalDisplay::*)(const QPoint & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TerminalDisplay::configureRequest)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (TerminalDisplay::*)(QKeyEvent * , bool & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TerminalDisplay::overrideShortcutCheck)) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (TerminalDisplay::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TerminalDisplay::isBusySelecting)) {
                *result = 6;
                return;
            }
        }
        {
            using _t = void (TerminalDisplay::*)(const char * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TerminalDisplay::sendStringToEmu)) {
                *result = 7;
                return;
            }
        }
        {
            using _t = void (TerminalDisplay::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TerminalDisplay::copyAvailable)) {
                *result = 8;
                return;
            }
        }
        {
            using _t = void (TerminalDisplay::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TerminalDisplay::termGetFocus)) {
                *result = 9;
                return;
            }
        }
        {
            using _t = void (TerminalDisplay::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TerminalDisplay::termLostFocus)) {
                *result = 10;
                return;
            }
        }
        {
            using _t = void (TerminalDisplay::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TerminalDisplay::notifyBell)) {
                *result = 11;
                return;
            }
        }
        {
            using _t = void (TerminalDisplay::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&TerminalDisplay::usesMouseChanged)) {
                *result = 12;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject Konsole::TerminalDisplay::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_Konsole__TerminalDisplay.data,
    qt_meta_data_Konsole__TerminalDisplay,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Konsole::TerminalDisplay::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Konsole::TerminalDisplay::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Konsole__TerminalDisplay.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int Konsole::TerminalDisplay::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 36)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 36;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 36)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 36;
    }
    return _id;
}

// SIGNAL 0
void Konsole::TerminalDisplay::keyPressedSignal(QKeyEvent * _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void Konsole::TerminalDisplay::mouseSignal(int _t1, int _t2, int _t3, int _t4)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void Konsole::TerminalDisplay::changedFontMetricSignal(int _t1, int _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void Konsole::TerminalDisplay::changedContentSizeSignal(int _t1, int _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void Konsole::TerminalDisplay::configureRequest(const QPoint & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void Konsole::TerminalDisplay::overrideShortcutCheck(QKeyEvent * _t1, bool & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void Konsole::TerminalDisplay::isBusySelecting(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void Konsole::TerminalDisplay::sendStringToEmu(const char * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}

// SIGNAL 8
void Konsole::TerminalDisplay::copyAvailable(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 8, _a);
}

// SIGNAL 9
void Konsole::TerminalDisplay::termGetFocus()
{
    QMetaObject::activate(this, &staticMetaObject, 9, nullptr);
}

// SIGNAL 10
void Konsole::TerminalDisplay::termLostFocus()
{
    QMetaObject::activate(this, &staticMetaObject, 10, nullptr);
}

// SIGNAL 11
void Konsole::TerminalDisplay::notifyBell(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 11, _a);
}

// SIGNAL 12
void Konsole::TerminalDisplay::usesMouseChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 12, nullptr);
}
struct qt_meta_stringdata_Konsole__AutoScrollHandler_t {
    QByteArrayData data[1];
    char stringdata0[27];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Konsole__AutoScrollHandler_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Konsole__AutoScrollHandler_t qt_meta_stringdata_Konsole__AutoScrollHandler = {
    {
QT_MOC_LITERAL(0, 0, 26) // "Konsole::AutoScrollHandler"

    },
    "Konsole::AutoScrollHandler"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Konsole__AutoScrollHandler[] = {

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

void Konsole::AutoScrollHandler::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject Konsole::AutoScrollHandler::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_Konsole__AutoScrollHandler.data,
    qt_meta_data_Konsole__AutoScrollHandler,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Konsole::AutoScrollHandler::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Konsole::AutoScrollHandler::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Konsole__AutoScrollHandler.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int Konsole::AutoScrollHandler::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
