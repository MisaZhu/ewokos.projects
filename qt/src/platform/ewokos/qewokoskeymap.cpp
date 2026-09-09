#include "qewokoskeymap.h"

#include <ewoksys/keydef.h>

QT_BEGIN_NAMESPACE

/* Codes ewoksys/keydef.h does not define.  These are the same values the rest
   of the tree uses - projects/nx11/src/drivers/kbd_ewokos.c documents them as
   "only by convention", and minivmac and macemu assume the same numbers - so
   matching them is what keeps one physical keyboard meaning the same thing in
   a Qt app and in an X11 one. */
#define EWOK_KEY_CAPSLOCK   0xA1
#define EWOK_KEY_ALT        0xA5
#define EWOK_KEY_RCTRL      0xA6
#define EWOK_KEY_RALT       0xA7
#define EWOK_KEY_INSERT     0xF3
#define EWOK_KEY_PAGEUP     0xF4
#define EWOK_KEY_PAGEDOWN   0xF5
#define EWOK_KEY_F1         0xF6
#define EWOK_KEY_F2         0xF7
#define EWOK_KEY_F3         0xF8
#define EWOK_KEY_F4         0xF9
#define EWOK_KEY_F5         0xFA
#define EWOK_KEY_F6         0xFB
#define EWOK_KEY_F7         0xFC
#define EWOK_KEY_F8         0xFD
#define EWOK_KEY_F9         0xFE
#define EWOK_KEY_F10        0xFF
#define EWOK_KEY_F11        0x100
#define EWOK_KEY_F12        0x101

/* Returns 0 for anything that is not a known special key, which is the signal
   for the caller to fall through to the printable path. */
static int ewokosSpecialKey(int keyCode)
{
    switch (keyCode) {
    case KEY_UP:            return Qt::Key_Up;
    case KEY_DOWN:          return Qt::Key_Down;
    case KEY_LEFT:          return Qt::Key_Left;
    case KEY_RIGHT:         return Qt::Key_Right;
    case KEY_HOME:          return Qt::Key_Home;
    case KEY_END:           return Qt::Key_End;
    case KEY_TAB:           return Qt::Key_Tab;
    case KEY_ENTER:         return Qt::Key_Return;
    case KEY_ESC:           return Qt::Key_Escape;
    /* hid_keybd's down map emits '\b' for backspace and only the on-screen
       vkey sends KEY_BACKSPACE (127), so both have to be accepted - the same
       pair nx11 collapses onto MWKEY_BACKSPACE. */
    case KEY_BACKSPACE:
    case CONSOLE_LEFT:      return Qt::Key_Backspace;
    case EWOK_KEY_INSERT:   return Qt::Key_Insert;
    case EWOK_KEY_PAGEUP:   return Qt::Key_PageUp;
    case EWOK_KEY_PAGEDOWN: return Qt::Key_PageDown;
    case KEY_LSHIFT:        return Qt::Key_Shift;
    case KEY_RSHIFT:        return Qt::Key_Shift;
    case KEY_CTRL:          return Qt::Key_Control;
    case EWOK_KEY_RCTRL:    return Qt::Key_Control;
    case EWOK_KEY_ALT:      return Qt::Key_Alt;
    case EWOK_KEY_RALT:     return Qt::Key_Alt;
    case EWOK_KEY_CAPSLOCK: return Qt::Key_CapsLock;
    case KEY_POWER:         return Qt::Key_PowerOff;
    case EWOK_KEY_F1:       return Qt::Key_F1;
    case EWOK_KEY_F2:       return Qt::Key_F2;
    case EWOK_KEY_F3:       return Qt::Key_F3;
    case EWOK_KEY_F4:       return Qt::Key_F4;
    case EWOK_KEY_F5:       return Qt::Key_F5;
    case EWOK_KEY_F6:       return Qt::Key_F6;
    case EWOK_KEY_F7:       return Qt::Key_F7;
    case EWOK_KEY_F8:       return Qt::Key_F8;
    case EWOK_KEY_F9:       return Qt::Key_F9;
    case EWOK_KEY_F10:      return Qt::Key_F10;
    case EWOK_KEY_F11:      return Qt::Key_F11;
    case EWOK_KEY_F12:      return Qt::Key_F12;
    /* Gamepads report their buttons as key codes.  Map the two action buttons
       onto what an application is most likely to bind, as nx11 does. */
    case JOYSTICK_A:        return Qt::Key_Return;
    case JOYSTICK_B:        return Qt::Key_Escape;
    case JOYSTICK_START:    return Qt::Key_Menu;
    }
    return 0;
}

EwokosKey ewokosTranslateKey(int keyCode, int value, unsigned int shift, bool ctrl)
{
    Q_UNUSED(shift);
    Q_UNUSED(ctrl);

    EwokosKey result;
    result.key = ewokosSpecialKey(keyCode);
    if (result.key != 0)
        return result;

    /* Printable.  value is the character the input method already cooked, but
       it is a control code whenever Ctrl was held, and then the identity of
       the key only survives in key_code.  Recover it the way the SDL2 backend
       does: trust key_code only for the control-character range, because the
       on-screen vkey fills value alone and leaves key_code unset. */
    int ch = value;
    if (ch > 0 && ch < 0x20 && keyCode >= 0x20 && keyCode < 0x100)
        ch = keyCode;

    if (ch >= 0x20 && ch < 0x7f) {
        /* Qt's convention is that Key_A means the 'a' key regardless of shift,
           so the code is the uppercased character while the text keeps the
           shift already applied. */
        result.key = (ch >= 'a' && ch <= 'z') ? (Qt::Key_A + (ch - 'a')) : ch;
        result.text = QString(QChar(ch));
        return result;
    }

    if (ch == 8 || ch == 9 || ch == 13 || ch == 27 || ch == 127) {
        /* A control character the special table did not already claim: it is
           still a key an application can bind, so keep the raw code as the
           text and let Qt's own Key_Backspace/Key_Tab/... numbering apply
           through the value itself. */
        result.key = ch;
        result.text = QString(QChar(ch));
        return result;
    }

    /* Ctrl held on a letter: value is the control character, key_code gave us
       the letter above, so ch is now the letter and this branch is not taken.
       Anything left is unmappable - drop it rather than inventing a key. */
    result.key = 0;
    return result;
}

Qt::KeyboardModifiers ewokosKeyModifiers(int keyCode, unsigned int shift, bool ctrl)
{
    Qt::KeyboardModifiers mods = Qt::NoModifier;

    /* shift carries the code of the shift key, not a flag, so test it against
       both shift codes and not for truthiness. */
    if (shift == KEY_LSHIFT || shift == KEY_RSHIFT ||
            keyCode == KEY_LSHIFT || keyCode == KEY_RSHIFT)
        mods |= Qt::ShiftModifier;
    if (ctrl || keyCode == KEY_CTRL || keyCode == EWOK_KEY_RCTRL)
        mods |= Qt::ControlModifier;
    if (keyCode == EWOK_KEY_ALT || keyCode == EWOK_KEY_RALT)
        mods |= Qt::AltModifier;

    return mods;
}

QT_END_NAMESPACE
