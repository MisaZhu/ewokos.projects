#include <string.h>
#include <stdio.h>

#include <install/gpt.h>

/* C12A7328-F81F-11D2-BA4B-00A0C93EC93B on disk. */
const uint8_t GPT_TYPE_ESP[16] = {
    0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
    0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b
};

/* 0FC63DAF-8483-4772-8E79-3D69D8477DE4 on disk. */
const uint8_t GPT_TYPE_LINUX_FS[16] = {
    0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47,
    0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4
};

uint32_t gpt_crc32(const void* data, uint32_t len) {
    static uint32_t table[256];
    static int table_ready = 0;
    const uint8_t* p = (const uint8_t*)data;
    uint32_t c = 0xffffffffu;

    if (!table_ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++)
                v = (v & 1) ? (0xedb88320u ^ (v >> 1)) : (v >> 1);
            table[i] = v;
        }
        table_ready = 1;
    }
    while (len--)
        c = table[(c ^ *p++) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

/* Recompute a header CRC in place: the CRC field (offset 16) is zeroed for
 * the computation, exactly as the UEFI spec prescribes. */
static uint32_t header_crc(gpt_header_t* h) {
    uint32_t saved = h->crc32;
    uint32_t crc;
    h->crc32 = 0;
    crc = gpt_crc32(h, h->size);
    h->crc32 = saved;
    return crc;
}

static uint32_t entry_table_sectors(const gpt_header_t* h) {
    uint32_t bytes = h->entry_count * h->entry_size;
    return (bytes + BLK_SECTOR - 1) / BLK_SECTOR;
}

int gpt_read(blockdev_t* dev, gpt_table_t* t) {
    uint8_t sec[BLK_SECTOR];

    memset(t, 0, sizeof(*t));
    if (dev->read(dev, 0, t->pmbr, 1) != 0)
        return -1;
    if (dev->read(dev, 1, sec, 1) != 0)
        return -1;
    memcpy(&t->hdr, sec, sizeof(gpt_header_t));

    if (t->hdr.signature != GPT_SIGNATURE)
        return -2;
    if (t->hdr.size < 92 || t->hdr.size > BLK_SECTOR ||
            t->hdr.entry_count == 0 ||
            t->hdr.entry_count > GPT_MAX_ENTRIES ||
            t->hdr.entry_size != GPT_ENTRY_BYTES ||
            t->hdr.entry_lba > 0x7fffffffULL)
        return -3;
    if (header_crc(&t->hdr) != t->hdr.crc32)
        return -3;

    if (dev->read(dev, t->hdr.entry_lba, t->entries,
                entry_table_sectors(&t->hdr)) != 0)
        return -4;
    if (gpt_crc32(t->entries,
                t->hdr.entry_count * t->hdr.entry_size) !=
            t->hdr.entry_crc32)
        return -5;

    t->count = (int)t->hdr.entry_count;
    return 0;
}

int gpt_probe(blockdev_t* dev) {
    gpt_table_t t;
    return gpt_read(dev, &t) == 0 ? 1 : 0;
}

const gpt_entry_t* gpt_entry_at(const gpt_table_t* t, int i) {
    if (i < 0 || i >= t->count)
        return NULL;
    return (const gpt_entry_t*)(t->entries + (size_t)i * GPT_ENTRY_BYTES);
}

int gpt_entry_used(const gpt_entry_t* e) {
    for (int i = 0; i < 16; i++) {
        if (e->type_guid[i])
            return 1;
    }
    return 0;
}

int gpt_is_esp(const gpt_entry_t* e) {
    return memcmp(e->type_guid, GPT_TYPE_ESP, 16) == 0;
}

void gpt_entry_name(const gpt_entry_t* e, char* out, int cap) {
    int n = 0;
    for (int i = 0; i < 36 && n + 1 < cap; i++) {
        uint16_t c = (uint16_t)(e->name[i * 2] | (e->name[i * 2 + 1] << 8));
        if (c == 0)
            break;
        out[n++] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
    }
    out[n] = 0;
}

const char* gpt_type_name(const gpt_entry_t* e) {
    if (gpt_is_esp(e))
        return "EFI system";
    if (memcmp(e->type_guid, GPT_TYPE_LINUX_FS, 16) == 0)
        return "Linux fs";
    return "unknown";
}

int gpt_find_type(const gpt_table_t* t, const uint8_t type[16]) {
    for (int i = 0; i < t->count; i++) {
        const gpt_entry_t* e = gpt_entry_at(t, i);
        if (e != NULL && gpt_entry_used(e) &&
                memcmp(e->type_guid, type, 16) == 0)
            return i + 1;
    }
    return 0;
}

uint64_t gpt_clone_min_sectors(const gpt_table_t* src) {
    uint64_t need = src->hdr.last_usable_lba + 1 +
        entry_table_sectors(&src->hdr) + 1; /* backup entries + header */
    for (int i = 0; i < src->count; i++) {
        const gpt_entry_t* e = gpt_entry_at(src, i);
        if (e != NULL && gpt_entry_used(e) && e->last_lba + 1 > need)
            need = e->last_lba + 1;
    }
    return need;
}

/* Patch a copy of the source header for the target disk.  The 1:1 mapping
 * keeps my_lba/entry_lba/usable range untouched; only the backup GPT
 * relocates to the target tail (which may differ from the source disk). */
static void patch_header(gpt_header_t* h, uint64_t dst_sectors, int backup) {
    if (!backup) {
        h->alternate_lba = dst_sectors - 1;
    } else {
        h->my_lba = dst_sectors - 1;
        h->alternate_lba = 1;
        h->entry_lba = h->last_usable_lba + 1;
    }
    h->crc32 = header_crc(h);
}

static int write_header(blockdev_t* dev, const gpt_header_t* h,
        uint64_t lba) {
    uint8_t sec[BLK_SECTOR];
    memset(sec, 0, sizeof(sec));
    memcpy(sec, h, h->size);
    return dev->write(dev, lba, sec, 1);
}

int gpt_write_primary(blockdev_t* dev, const gpt_table_t* src,
        uint64_t dst_sectors) {
    gpt_header_t h = src->hdr;

    patch_header(&h, dst_sectors, 0);
    if (dev->write(dev, 0, src->pmbr, 1) != 0)
        return -1;
    if (write_header(dev, &h, h.my_lba) != 0)
        return -1;
    if (dev->write(dev, h.entry_lba, src->entries,
                entry_table_sectors(&h)) != 0)
        return -1;
    return 0;
}

int gpt_write_backup(blockdev_t* dev, const gpt_table_t* src,
        uint64_t dst_sectors) {
    gpt_header_t b = src->hdr;

    patch_header(&b, dst_sectors, 1);
    if (dev->write(dev, b.entry_lba, src->entries,
                entry_table_sectors(&b)) != 0)
        return -1;
    if (write_header(dev, &b, b.my_lba) != 0)
        return -1;
    return dev->flush(dev);
}
