#include "qewokosfontscan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Read as little of a font as FreeType would have needed to answer the
 * questions addTTFile() asks it, and no more.
 *
 * Everything here mirrors a specific place in FreeType, and the comments name
 * it, because the agreement is the whole value of the file: qewokosfontscan is
 * only allowed to be faster than FT_New_Face() if it is also indistinguishable
 * from it.  Where FreeType is lenient in a way that would mean guessing -
 * reading a field past the end of the table that declares it, say - this
 * declines instead and returns false, which sends that one file back through
 * addTTFile() and costs exactly what it costs today.
 *
 * Deliberately Qt-free and deliberately stdio-based: stdio is what FreeType's
 * own ANSI stream layer uses (ft_ansi_stream_io() in src/base/ftsystem.c), so
 * both paths pay the same per-read cost, and being Qt-free is what lets this
 * file be compiled on the host and diffed against a real FT_New_Face().
 */

namespace {

/* ---------------------------------------------------------------- sfnt --- */

constexpr uint32_t tag4(char a, char b, char c, char d)
{
    return (uint32_t(uint8_t(a)) << 24) | (uint32_t(uint8_t(b)) << 16)
         | (uint32_t(uint8_t(c)) << 8)  |  uint32_t(uint8_t(d));
}

/* The two sfnt flavours this parser covers.  A TrueType collection ('ttcf')
   carries several faces behind one header and would need the per-face offsets
   addTTFile()'s num_faces loop walks; the Mac 'true' flavour is exempt from
   FreeType's mandatory `hhea'; Type 1 ('typ1', and the .pfa/.pfb files the
   directory filter also matches) is not sfnt at all.  All three fall back. */
const uint32_t SFNT_TRUETYPE = 0x00010000u;
const uint32_t SFNT_CFF      = tag4('O', 'T', 'T', 'O');

const uint32_t TAG_HEAD = tag4('h', 'e', 'a', 'd');
const uint32_t TAG_HHEA = tag4('h', 'h', 'e', 'a');
const uint32_t TAG_HMTX = tag4('h', 'm', 't', 'x');
const uint32_t TAG_MAXP = tag4('m', 'a', 'x', 'p');
const uint32_t TAG_GLYF = tag4('g', 'l', 'y', 'f');
const uint32_t TAG_LOCA = tag4('l', 'o', 'c', 'a');
const uint32_t TAG_CFF  = tag4('C', 'F', 'F', ' ');
const uint32_t TAG_CFF2 = tag4('C', 'F', 'F', '2');
const uint32_t TAG_NAME = tag4('n', 'a', 'm', 'e');
const uint32_t TAG_OS2  = tag4('O', 'S', '/', '2');
const uint32_t TAG_POST = tag4('p', 'o', 's', 't');
const uint32_t TAG_CMAP = tag4('c', 'm', 'a', 'p');

/* Presence of any of these changes what FreeType concludes about the face, in
   a way this parser does not reproduce: `fvar' makes num_faces count the named
   instances, which addTTFile() then registers one by one, and the bitmap
   tables clear has_outline - which is what picks between os2.fsSelection and
   head.macStyle for the style flags. */
const uint32_t TAG_FVAR = tag4('f', 'v', 'a', 'r');
const uint32_t TAG_CBLC = tag4('C', 'B', 'L', 'C');
const uint32_t TAG_CBDT = tag4('C', 'B', 'D', 'T');
const uint32_t TAG_SBIX = tag4('s', 'b', 'i', 'x');

/* `head' is 54 bytes and its magic is what FreeType checks first
   (tt_face_load_header, ttload.c); `post' is read as a fixed 32-byte frame
   (tt_face_load_post) with isFixedPitch at 12. */
const uint32_t HEAD_SIZE = 54;
const uint32_t HEAD_MAGIC = 0x5F0F3CF5u;
const uint32_t HEAD_MAGIC_OFFSET = 12;
const uint32_t HEAD_UNITS_PER_EM_OFFSET = 18;
const uint32_t HEAD_MAC_STYLE_OFFSET = 44;
const uint32_t POST_SIZE = 32;
const uint32_t POST_FIXED_PITCH_OFFSET = 12;

/* sfnt_load_face() rejects unitsPerEm outside 16..16384 outright. */
const uint32_t UNITS_PER_EM_MIN = 16;
const uint32_t UNITS_PER_EM_MAX = 16384;

/* `os2' is read in four frames: 78 bytes always, then 8 more from version 1,
   10 more from version 2 and 4 more from version 5 (os2_fields,
   os2_fields_extra1/2/5 in ttload.c). */
uint32_t os2Size(uint16_t version)
{
    uint32_t size = 78;
    if (version >= 1) size += 8;
    if (version >= 2) size += 10;
    if (version >= 5) size += 4;
    return size;
}

const uint32_t OS2_WEIGHT_CLASS_OFFSET = 4;
const uint32_t OS2_WIDTH_CLASS_OFFSET = 6;
const uint32_t OS2_PANOSE_OFFSET = 32;
const uint32_t OS2_UNICODE_RANGE_OFFSET = 42;
const uint32_t OS2_FS_SELECTION_OFFSET = 62;
const uint32_t OS2_CODEPAGE_RANGE_OFFSET = 78;

/* fsSelection bits, as sfnt_load_face() reads them for style_flags. */
const uint16_t FS_SELECTION_ITALIC = 1;      /* bit 0 */
const uint16_t FS_SELECTION_BOLD   = 32;     /* bit 5 */
const uint16_t FS_SELECTION_OBLIQUE = 512;   /* bit 9, wins over bit 0 */
const uint16_t FS_SELECTION_WWS_ONLY = 256;  /* bit 8, changes the name ids */

/* head.macStyle bits, used instead of fsSelection when there is no os2. */
const uint16_t MAC_STYLE_BOLD = 1;
const uint16_t MAC_STYLE_ITALIC = 2;

/* ------------------------------------------------------------ `name' --- */

/* ttnameid.h */
const uint16_t NAME_FAMILY         = 1;
const uint16_t NAME_SUBFAMILY      = 2;
const uint16_t NAME_TYPO_FAMILY    = 16;
const uint16_t NAME_TYPO_SUBFAMILY = 17;
const uint16_t NAME_WWS_FAMILY     = 21;
const uint16_t NAME_WWS_SUBFAMILY  = 22;

/* tttables.h */
const uint16_t PLATFORM_UNICODE   = 0;
const uint16_t PLATFORM_MACINTOSH = 1;
const uint16_t PLATFORM_ISO       = 2;
const uint16_t PLATFORM_MICROSOFT = 3;

const uint16_t MAC_LANGID_ENGLISH = 0;   /* TT_MAC_LANGID_ENGLISH */
const uint16_t MAC_ID_ROMAN       = 0;   /* TT_MAC_ID_ROMAN */
const uint16_t MS_ID_SYMBOL_CS    = 0;
const uint16_t MS_ID_UNICODE_CS   = 1;
const uint16_t MS_ID_UCS_4        = 10;
const uint16_t MS_LANGID_ENGLISH  = 0x009;

/* The largest `name' table pulled in whole.  35 of the 40 shipped fonts fit,
   which makes their names cost one read; the five that do not (Hack-Regular at
   18 KB, Courier-Prime at 14 KB) hold few records but long license strings, so
   they read the record array here and then each wanted string on its own. */
const uint32_t NAME_BLOB_MAX = 4096;

/* ---------------------------------------------------------------- I/O ---- */

inline uint16_t be16(const uint8_t *p)
{
    return uint16_t((uint16_t(p[0]) << 8) | p[1]);
}

inline uint32_t be32(const uint8_t *p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16)
         | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

/* fseek is free on EwokOS - vfs_seek() in libewoksys just stores the offset in
   the process's own fd table, with no IPC - but fread is not: with no buffer in
   struct FILE every fread() is a read() syscall and a round trip to the
   filesystem server.  So the read count is what this class exists to keep
   down, and the position it tracks saves the redundant seeks on top. */
class Reader
{
public:
    explicit Reader(const char *path)
        : m_file(fopen(path, "rb")), m_pos(UINT32_MAX)
    {
    }

    ~Reader()
    {
        if (m_file)
            fclose(m_file);
    }

    /* Bytes actually read, which is fewer than \a max at end of file. */
    uint32_t some(uint32_t offset, void *buf, uint32_t max)
    {
        if (!m_file || max == 0)
            return 0;
        if (offset != m_pos && fseek(m_file, long(offset), SEEK_SET) != 0) {
            m_pos = UINT32_MAX;
            return 0;
        }
        const uint32_t got = uint32_t(fread(buf, 1, max, m_file));
        /* Forget the position on a short read: fread stops at end of file, and
           a caller that seeks back to a spot inside what was already consumed
           must not be told it is already there. */
        m_pos = got == max ? offset + got : UINT32_MAX;
        return got;
    }

    bool all(uint32_t offset, void *buf, uint32_t len)
    {
        return some(offset, buf, len) == len;
    }

private:
    Reader(const Reader &);
    Reader &operator=(const Reader &);

    FILE *m_file;
    uint32_t m_pos;
};

struct Table
{
    uint32_t tag;
    uint32_t offset;
    uint32_t length;
};

/* 63 keeps the header-plus-directory read inside one 1 KB block.  No shipped
   font comes close - the largest has 25 tables - and a font that does is
   exactly the kind of thing worth handing to FreeType rather than widening a
   buffer for. */
const uint32_t MAX_TABLES = 63;

/* Table offsets and lengths are uint32 straight off the disk.  Bounding them
   here keeps every sum below - a record's storageBase + stringOffset, an
   offset + length, the long() a seek takes - inside a range that cannot wrap,
   so none of those needs its own overflow check.  A font whose tables live
   past the 2 GB mark is not one to guess about in any case. */
const uint32_t MAX_TABLE_EXTENT = 0x7FFFFFFFu;

const Table *findTable(const Table *tables, uint32_t count, uint32_t tag)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (tables[i].tag == tag)
            return &tables[i];
    }
    return 0;
}

/* --------------------------------------------------------------- names --- */

struct NameRecord
{
    uint16_t platformId;
    uint16_t encodingId;
    uint16_t languageId;
    uint16_t nameId;
    uint16_t length;
    uint32_t stringOffset;   /* absolute in the file */
};

/* A `name' table whose record array is already in memory.  Records are read
   out of the blob rather than copied into an array of their own: the count is
   unbounded in principle (a uint16) and the blob is capped, so an array sized
   to fit every possible record would be the largest thing on the stack here
   for no benefit. */
struct NameTable
{
    const uint8_t *records;   /* the blob, from byte 6 on */
    uint32_t count;
    uint32_t storageBase;     /* nameTab->offset + the header's storageOffset */
    uint32_t storageStart;    /* first byte a string may occupy */
    uint32_t storageLimit;    /* one past the last, i.e. end of the table */
};

/* One record, with the bounds check tt_face_load_name() applies: a record is
   dropped when its string is empty or falls outside the table, which is what
   keeps a corrupt name table from being followed past its end. */
bool readNameRecord(const NameTable &table, uint32_t index, NameRecord *out)
{
    const uint8_t *rec = table.records + 12 * index;
    out->platformId = be16(rec);
    out->encodingId = be16(rec + 2);
    out->languageId = be16(rec + 4);
    out->nameId = be16(rec + 6);
    out->length = be16(rec + 8);
    out->stringOffset = table.storageBase + be16(rec + 10);

    if (out->length == 0)
        return false;
    if (out->stringOffset < table.storageStart)
        return false;
    if (out->length > table.storageLimit - out->stringOffset)
        return false;
    return true;
}

/* tt_face_get_name()'s choice of record for one name id.  Returns the index
   into \a table, or -1 when the font has no usable record for \a nameId -
   which FreeType reports as a NULL name, and addTTFile() turns into an empty
   QString. */
int pickNameRecord(const NameTable &table, uint16_t nameId, bool *isUtf16)
{
    int appleEnglish = -1, appleRoman = -1, win = -1, unicode = -1;
    bool winIsEnglish = false;

    for (uint32_t n = 0; n < table.count; ++n) {
        NameRecord rec;
        if (!readNameRecord(table, n, &rec))
            continue;
        if (rec.nameId != nameId)
            continue;

        switch (rec.platformId) {
        case PLATFORM_UNICODE:
        case PLATFORM_ISO:
            /* Last one wins, as in FreeType: these are the fallback of last
               resort and only reached when nothing else exists. */
            unicode = n;
            break;
        case PLATFORM_MACINTOSH:
            /* "Some fonts will use either an English language id, or a Roman
               encoding id, to indicate the English version of its font
               name." */
            if (rec.languageId == MAC_LANGID_ENGLISH)
                appleEnglish = n;
            else if (rec.encodingId == MAC_ID_ROMAN)
                appleRoman = n;
            break;
        case PLATFORM_MICROSOFT:
            /* Only a non-English name when there is nothing else, and only the
               three Unicode-ish encodings - the CJK ones are left alone. */
            if (win == -1 || (rec.languageId & 0x3FF) == MS_LANGID_ENGLISH) {
                if (rec.encodingId == MS_ID_SYMBOL_CS
                        || rec.encodingId == MS_ID_UNICODE_CS
                        || rec.encodingId == MS_ID_UCS_4) {
                    winIsEnglish = (rec.languageId & 0x3FF) == MS_LANGID_ENGLISH;
                    win = n;
                }
            }
            break;
        default:
            break;
        }
    }

    int apple = appleRoman;
    if (appleEnglish >= 0)
        apple = appleEnglish;

    /* "We will thus favor names encoded in Windows formats if available
       (provided it is an English name)." */
    if (win >= 0 && !(apple >= 0 && !winIsEnglish)) {
        *isUtf16 = true;
        return win;
    }
    if (apple >= 0) {
        *isUtf16 = false;
        return apple;
    }
    if (unicode >= 0) {
        *isUtf16 = true;
        return unicode;
    }
    return -1;
}

/* tt_name_ascii_from_utf16() / tt_name_ascii_from_other(): stop at the first
   NUL, and fold everything outside printable ASCII to '?'.  A family name in
   Han characters therefore comes out as a row of question marks - which is
   what it comes out as through FreeType today, and what the rest of the system
   would ask for, so it is reproduced rather than improved on. */
void foldName(const uint8_t *str, uint32_t length, bool isUtf16, char *out)
{
    const uint32_t units = isUtf16 ? length / 2 : length;
    uint32_t n = 0;
    for (; n < units; ++n) {
        const uint32_t code = isUtf16 ? be16(str + n * 2) : str[n];
        if (code == 0)
            break;
        out[n] = char(code < 32 || code > 127 ? '?' : code);
    }
    out[n] = '\0';
}

} // namespace

bool ewokSfntPeek(const char *path, EwokFontMeta *meta)
{
    if (!path || !meta)
        return false;

    Reader reader(path);

    /* The 12-byte header and the whole table directory, in one read. */
    uint8_t dir[12 + 16 * MAX_TABLES];
    const uint32_t got = reader.some(0, dir, sizeof(dir));
    if (got < 12)
        return false;

    const uint32_t version = be32(dir);
    const bool isTrueType = version == SFNT_TRUETYPE;
    const bool isCff = version == SFNT_CFF;
    if (!isTrueType && !isCff)
        return false;

    const uint32_t numTables = be16(dir + 4);
    if (numTables == 0 || numTables > MAX_TABLES || got < 12 + 16 * numTables)
        return false;

    Table tables[MAX_TABLES];
    for (uint32_t i = 0; i < numTables; ++i) {
        const uint8_t *rec = dir + 12 + 16 * i;
        tables[i].tag = be32(rec);
        tables[i].offset = be32(rec + 8);
        tables[i].length = be32(rec + 12);
        if (tables[i].offset > MAX_TABLE_EXTENT
                || tables[i].length > MAX_TABLE_EXTENT - tables[i].offset)
            return false;
    }

    /* FreeType binary-searches a copy of the directory it sorted by tag
       (sfnt_open_face), so with a tag listed twice it is not defined which
       entry it lands on.  Rather than pick one and hope, hand the file over. */
    for (uint32_t i = 0; i < numTables; ++i) {
        for (uint32_t j = i + 1; j < numTables; ++j) {
            if (tables[i].tag == tables[j].tag)
                return false;
        }
    }

    const Table *headTab = findTable(tables, numTables, TAG_HEAD);
    const Table *hheaTab = findTable(tables, numTables, TAG_HHEA);
    const Table *hmtxTab = findTable(tables, numTables, TAG_HMTX);
    const Table *maxpTab = findTable(tables, numTables, TAG_MAXP);
    const Table *glyfTab = findTable(tables, numTables, TAG_GLYF);
    const Table *locaTab = findTable(tables, numTables, TAG_LOCA);
    const Table *cffTab = findTable(tables, numTables, TAG_CFF);
    const Table *cff2Tab = findTable(tables, numTables, TAG_CFF2);
    const Table *nameTab = findTable(tables, numTables, TAG_NAME);
    const Table *os2Tab = findTable(tables, numTables, TAG_OS2);
    const Table *postTab = findTable(tables, numTables, TAG_POST);
    const Table *cmapTab = findTable(tables, numTables, TAG_CMAP);

    /* Outlines: sfnt_load_face() sets has_outline from glyf/CFF/CFF2 and then
       clears it again for CBLC/CBDT fonts, and has_outline is what decides
       whether the style flags come from os2 or from head.  A font this parser
       cannot place on that question is not a font it can register. */
    if (findTable(tables, numTables, TAG_FVAR)
            || findTable(tables, numTables, TAG_CBLC)
            || findTable(tables, numTables, TAG_CBDT)
            || findTable(tables, numTables, TAG_SBIX))
        return false;
    if (isTrueType && (!glyfTab || !locaTab))
        return false;
    if (isCff && (!cffTab && !cff2Tab))
        return false;

    /* Mandatory in sfnt_load_face() for every flavour but the Mac 'true' one,
       which isTrueType/isCff already excluded: a missing `hhea' throws
       Horiz_Header_Missing and a missing `hmtx' throws Hmtx_Table_Missing.
       `maxp' is tolerated when absent but tt_face_load_loca() then validates
       the loca count against a numGlyphs of zero, so a font without it does
       not survive face init either. */
    if (!hheaTab || !hmtxTab || !maxpTab)
        return false;

    /* ---------------------------------------------------------- `head' --- */

    if (!headTab || headTab->length < HEAD_SIZE)
        return false;

    uint8_t head[HEAD_SIZE];
    if (!reader.all(headTab->offset, head, HEAD_SIZE))
        return false;
    if (be32(head + HEAD_MAGIC_OFFSET) != HEAD_MAGIC)
        return false;

    const uint32_t unitsPerEm = be16(head + HEAD_UNITS_PER_EM_OFFSET);
    if (unitsPerEm < UNITS_PER_EM_MIN || unitsPerEm > UNITS_PER_EM_MAX)
        return false;

    /* ------------------------------------------------------------ os2 ---- */

    EwokFontMeta out;
    memset(&out, 0, sizeof(out));

    if (os2Tab) {
        /* A table shorter than the frame FreeType reads for its version is a
           malformed font, and FreeType answers it by reading the fields from
           whatever follows the table in the file - FT_Stream_EnterFrame bounds
           the frame by the file, not by the table.  Reproducing that means
           inventing values, so decline instead. */
        uint8_t os2[100];
        uint16_t os2Version = 0;
        uint32_t size = 0;
        if (os2Tab->length >= 78 && reader.all(os2Tab->offset, os2, 78)) {
            os2Version = be16(os2);
            size = os2Size(os2Version);
            if (size > sizeof(os2) || os2Tab->length < size)
                size = 0;
            else if (size > 78 && !reader.all(os2Tab->offset + 78, os2 + 78, size - 78))
                size = 0;
        }
        if (size != 0) {
            out.hasOs2 = true;
            out.weightClass = be16(os2 + OS2_WEIGHT_CLASS_OFFSET);
            out.widthClass = be16(os2 + OS2_WIDTH_CLASS_OFFSET);
            out.panose2 = os2[OS2_PANOSE_OFFSET + 2];
            for (int i = 0; i < 4; ++i)
                out.unicodeRange[i] = be32(os2 + OS2_UNICODE_RANGE_OFFSET + 4 * i);
            out.fsSelection = be16(os2 + OS2_FS_SELECTION_OFFSET);
            if (os2Version >= 1) {
                for (int i = 0; i < 2; ++i)
                    out.codePageRange[i] = be32(os2 + OS2_CODEPAGE_RANGE_OFFSET + 4 * i);
            }
        }
    }

    /* Style flags, exactly as sfnt_load_face() computes them. */
    if (out.hasOs2) {
        out.italic = (out.fsSelection & FS_SELECTION_OBLIQUE) != 0
                || (out.fsSelection & FS_SELECTION_ITALIC) != 0;
        out.bold = (out.fsSelection & FS_SELECTION_BOLD) != 0;
    } else {
        const uint16_t macStyle = be16(head + HEAD_MAC_STYLE_OFFSET);
        out.bold = (macStyle & MAC_STYLE_BOLD) != 0;
        out.italic = (macStyle & MAC_STYLE_ITALIC) != 0;
    }

    /* ----------------------------------------------------------- `post' --- */

    if (postTab) {
        /* Absent is fine and leaves isFixedPitch zero, which is what FreeType
           has: tt_face_load_post() returns before touching the struct.  Too
           short is not, for the same out-of-table reason as os2 above. */
        if (postTab->length < POST_SIZE)
            return false;
        uint8_t post[POST_SIZE];
        if (!reader.all(postTab->offset, post, POST_SIZE))
            return false;
        out.fixedPitch = be32(post + POST_FIXED_PITCH_OFFSET) != 0;
    }

    /* ----------------------------------------------------------- `name' --- */

    if (!nameTab || nameTab->length < 6)
        return false;

    uint8_t *blob = (uint8_t *)malloc(NAME_BLOB_MAX);
    if (!blob)
        return false;

    const uint32_t want = nameTab->length < NAME_BLOB_MAX ? nameTab->length : NAME_BLOB_MAX;
    const uint32_t have = reader.some(nameTab->offset, blob, want);

    NameTable names;
    names.records = 0;
    names.count = 0;
    names.storageBase = 0;
    names.storageStart = 0;
    names.storageLimit = 0;

    bool ok = have >= 6;
    if (ok) {
        /* Format 1 appends language-tag records after the name records and
           makes any record with languageID >= 0x8000 valid only if its tag is.
           No shipped font uses it; rather than carry the extra read and the
           extra rule, leave those to FreeType. */
        if (be16(blob) != 0)
            ok = false;
        const uint32_t recordCount = be16(blob + 2);
        const uint32_t storageOffset = be16(blob + 4);
        if (6 + 12 * recordCount > have) {
            ok = false;
        } else {
            names.records = blob + 6;
            names.count = recordCount;
            names.storageBase = nameTab->offset + storageOffset;
            names.storageStart = nameTab->offset + 6 + 12 * recordCount;
            names.storageLimit = nameTab->offset + nameTab->length;
            /* tt_face_load_name() rejects the whole table when the record
               array runs past its end. */
            if (names.storageStart > names.storageLimit)
                ok = false;
        }
    }

    /* sfnt_load_face() picks the ids in this order, and which order depends on
       fsSelection bit 8: a WWS-only face skips the WWS ids, every other face
       tries them first. */
    const bool wwsOnly = out.hasOs2 && (out.fsSelection & FS_SELECTION_WWS_ONLY) != 0;
    const uint16_t familyIds[3] = {
        wwsOnly ? NAME_TYPO_FAMILY : NAME_WWS_FAMILY,
        NAME_TYPO_FAMILY,
        NAME_FAMILY
    };
    const uint16_t styleIds[3] = {
        wwsOnly ? NAME_TYPO_SUBFAMILY : NAME_WWS_SUBFAMILY,
        NAME_TYPO_SUBFAMILY,
        NAME_SUBFAMILY
    };

    const uint32_t idCount = wwsOnly ? 2 : 3;
    for (int which = 0; ok && which < 2; ++which) {
        char *dst = which == 0 ? out.family : out.style;
        const uint16_t *ids = which == 0 ? familyIds : styleIds;

        bool found = false;
        for (uint32_t i = 0; i < idCount && !found; ++i) {
            bool isUtf16 = false;
            const int idx = pickNameRecord(names, ids[i], &isUtf16);
            if (idx < 0)
                continue;
            NameRecord rec;
            if (!readNameRecord(names, uint32_t(idx), &rec))
                continue;

            /* FreeType has no cap on a face name.  A name that would not fit
               is not a name this parser can report faithfully, so the file
               goes back to FreeType rather than coming out truncated. */
            const uint32_t limit = uint32_t(EWOK_FONT_NAME_MAX - 1) * (isUtf16 ? 2 : 1);
            if (rec.length > limit) {
                ok = false;
                break;
            }

            const uint8_t *str = 0;
            uint8_t spill[EWOK_FONT_NAME_MAX * 2];
            const uint32_t rel = rec.stringOffset - nameTab->offset;
            if (rel + rec.length <= have) {
                /* Already inside the blob the record array came in with. */
                str = blob + rel;
            } else if (reader.all(rec.stringOffset, spill, rec.length)) {
                str = spill;
            } else {
                ok = false;
                break;
            }

            foldName(str, rec.length, isUtf16, dst);
            found = true;
        }
        /* A font with no record for any of the ids leaves FreeType's
           family_name NULL and addTTFile() registers an empty family.  That
           is worth falling back for: registering nothing at all here would
           quietly drop a font that today shows up, however uselessly. */
        if (which == 0 && out.family[0] == '\0')
            ok = false;
    }

    free(blob);
    if (!ok)
        return false;

    /* ----------------------------------------------------------- `cmap' --- */

    if (!out.hasOs2) {
        /* Only reached for a font with no usable os2, because addTTFile()
           overwrites the writing systems wholesale from os2 when there is one
           and throws the symbol bit away.  Reading the encoding records is
           enough - the subtables, which are the part of `cmap' that makes a
           CJK font's copy of it hundreds of kilobytes, are never looked at. */
        out.symbol = false;
        if (cmapTab && cmapTab->length >= 4) {
            uint8_t cmapHead[4];
            if (!reader.all(cmapTab->offset, cmapHead, 4))
                return false;
            const uint32_t encodings = be16(cmapHead + 2);
            if (encodings > 0 && cmapTab->length >= 4 + 8 * encodings) {
                uint8_t *recs8 = (uint8_t *)malloc(8 * encodings);
                if (!recs8)
                    return false;
                if (reader.all(cmapTab->offset + 4, recs8, 8 * encodings)) {
                    for (uint32_t i = 0; i < encodings; ++i) {
                        if (be16(recs8 + 8 * i) == PLATFORM_MICROSOFT
                                && be16(recs8 + 8 * i + 2) == MS_ID_SYMBOL_CS) {
                            out.symbol = true;
                            break;
                        }
                    }
                }
                free(recs8);
            }
        }
    }

    *meta = out;
    return true;
}
