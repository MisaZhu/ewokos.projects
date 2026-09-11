#ifndef QEWOKOSFONTSCAN_H
#define QEWOKOSFONTSCAN_H

#include <stdint.h>

/*
 * The registration metadata of one sfnt font, read without FreeType.
 *
 * QFreeTypeFontDatabase::addTTFile() derives everything Qt registers a font
 * with from an FT_Face, and getting an FT_Face means a full face init:
 * sfnt_init_face() loads head, maxp, cmap, name, post, os2, eblc, cpal, colr,
 * svg, pclt, gasp and kern, then tt_face_init() adds hdmx, loca, cvt, fpgm and
 * prep.  Each of those is a seek plus a read of the whole table, and for the
 * fonts in /usr/system/fonts the total comes to 1.5-2.2 MB across roughly
 * 600-900 read() calls.  EwokOS's stdio is unbuffered - struct FILE in
 * libewoksys/include/stdio.h holds an fd and nothing else - so each of those
 * reads is also an IPC round trip to the filesystem server, all of it spent
 * before the first glyph is on screen.
 *
 * Almost none of that I/O feeds the registration.  What addTTFile() actually
 * takes off the face is family_name and style_name (both from `name'),
 * style_flags (`os2' fsSelection, or `head' macStyle for fonts without an
 * os2), face_flags' FIXED_WIDTH bit (`post' isFixedPitch), the charmap
 * encodings (the `cmap' table's encoding records, not its subtables) and the
 * os2 table itself (unicodeRange, codePageRange, usWeightClass, usWidthClass,
 * panose).  That is five small tables, and reading only those costs about 110
 * KB and five reads per font.
 *
 * The fields below are the raw values those tables hold, deliberately not the
 * QFont::Weight/QFont::Stretch/QSupportedWritingSystems Qt ends up with: the
 * mapping from them is addTTFile()'s own code, and keeping it there means
 * there is one copy of it to read.  The one transformation applied here is the
 * ASCII folding of the name strings, because that is what FreeType's
 * tt_face_get_name() does - tt_name_ascii_from_utf16() replaces every code
 * point outside 32..127 with '?' - and a family that came back as real UTF-8
 * here would not be the family the rest of the system asks for.
 *
 * This header is Qt-free on purpose.  It lets the parser be compiled and run
 * on the host against the same FreeType sources the target builds with, which
 * is how the two were checked to agree on every shipped font.
 */

/* Family and style names longer than this are not truncated to fit - the
   caller is told to use FreeType instead, so a long name never becomes a
   different name. */
#define EWOK_FONT_NAME_MAX 128

struct EwokFontMeta
{
    /* Face names as FreeType would report them: ASCII, '?'-folded, and empty
       when the name table has no usable record for that id. */
    char family[EWOK_FONT_NAME_MAX];
    char style[EWOK_FONT_NAME_MAX];

    /* FT_STYLE_FLAG_BOLD / FT_STYLE_FLAG_ITALIC.  From os2.fsSelection when
       the font has an os2 table, from head.macStyle when it does not - which
       is what sfnt_load_face() does, and the two do not agree for every
       font. */
    bool bold;
    bool italic;

    /* FT_FACE_FLAG_FIXED_WIDTH, i.e. post.isFixedPitch.  False when the font
       has no `post' table or one too short to hold the field. */
    bool fixedPitch;

    /* A (platform 3, encoding 0) record in the cmap encoding table, which is
       how FreeType ends up with FT_ENCODING_MS_SYMBOL and how addTTFile()
       marks a font as a symbol font.  Only filled in when hasOs2 is false:
       addTTFile() overwrites the whole writing-system set from os2 when there
       is one, so the symbol bit it computed first is thrown away. */
    bool symbol;

    /* False when there is no os2 table, or one whose length does not cover
       the fields its own version declares - both of which leave FreeType with
       os2.version == 0xFFFF, and FT_Get_Sfnt_Table(ft_sfnt_os2) with NULL.
       The fields below are meaningless when this is false. */
    bool hasOs2;
    uint16_t weightClass;
    uint16_t widthClass;
    uint16_t fsSelection;
    uint8_t panose2;
    uint32_t unicodeRange[4];
    uint32_t codePageRange[2];
};

/*
 * Read the registration metadata of the sfnt font at \a path into \a meta.
 *
 * Returns false - and leaves \a meta alone - whenever the file is not a font
 * this parser is prepared to vouch for.  That is deliberately a wide net: a
 * TrueType collection, a variable font, a bitmap-only or bitmap-carrying font,
 * a name table in the format-1 layout, a missing or out-of-range table, a
 * name too long for EwokFontMeta.  The caller must treat false as "ask
 * FreeType", not as "not a font", because the point of the split is that the
 * slow path stays correct for anything the fast path does not fully
 * understand.
 */
bool ewokSfntPeek(const char *path, EwokFontMeta *meta);

#endif // QEWOKOSFONTSCAN_H
