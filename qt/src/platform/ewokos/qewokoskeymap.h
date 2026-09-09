#ifndef QEWOKOSKEYMAP_H
#define QEWOKOSKEYMAP_H

#include <QtCore/qnamespace.h>
#include <QtCore/qstring.h>

QT_BEGIN_NAMESPACE

/*
 * EwokOS keyboard input reaches a client as XEVT_IM events carrying three
 * numbers, and none of them is a Qt::Key:
 *
 *   key_code  the raw code.  Printable keys arrive as ASCII; everything else
 *             (arrows, home/end/page, function keys, modifiers) is a private
 *             code, part of it in ewoksys/keydef.h and part of it only by
 *             convention shared across the tree.
 *   value     the character the input method already cooked: shift and ctrl
 *             applied.  This is what a text field wants, but it is NOT what
 *             identifies the key - the xim daemon turns Ctrl+C into 0x03 and
 *             the key identity is gone from value.
 *   shift     not a boolean.  It holds the code of the shift key involved
 *             (KEY_LSHIFT / KEY_RSHIFT) or 0, which is why the test is an
 *             equality against those two and not `if (shift)`.
 *   ctrl      nonzero while Ctrl is held.
 *
 * The mapping below is the same one projects/nx11/src/drivers/kbd_ewokos.c
 * applies for Nano-X, so Qt and the X11 shim agree on what a key is.
 */

struct EwokosKey {
    int key;        /* Qt::Key_*, or 0 when the code is unmappable */
    QString text;   /* empty for non-printable keys and bare modifiers */
};

/*
 * Translates one XEVT_IM payload.  press is unused here - the press/release
 * distinction is QEvent::KeyPress/KeyRelease at the call site - but value and
 * keyCode are both needed because each one is authoritative for a different
 * half of the keyspace.
 */
EwokosKey ewokosTranslateKey(int keyCode, int value, unsigned int shift, bool ctrl);

/* The modifier latch for a key event, derived from the same fields. */
Qt::KeyboardModifiers ewokosKeyModifiers(int keyCode, unsigned int shift, bool ctrl);

QT_END_NAMESPACE

#endif // QEWOKOSKEYMAP_H
