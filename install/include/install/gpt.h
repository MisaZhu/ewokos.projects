#ifndef __INSTALL_GPT_H
#define __INSTALL_GPT_H

/*
 * Self-contained GPT reader/writer for the installer.
 *
 * The install model is a 1:1 LBA clone: protective MBR, primary header and
 * partition entries are rewritten at their source LBAs, every partition
 * range is copied block-for-block, and only the backup GPT moves - to the
 * tail of the target disk, which may differ in size from the source.
 */

#include <stdint.h>
#include <install/blockdev.h>

#define GPT_SIGNATURE 0x5452415020494645ULL /* "EFI PART" */
#define GPT_MAX_ENTRIES 128
#define GPT_ENTRY_BYTES 128

/* EFI System Partition: C12A7328-F81F-11D2-BA4B-00A0C93EC93B, as it lies
 * on disk (first three fields little-endian). */
#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t GPT_TYPE_ESP[16];
/* Linux filesystem: 0FC63DAF-8483-4772-8E79-3D69D8477DE4. */
extern const uint8_t GPT_TYPE_LINUX_FS[16];

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t size;
    uint32_t crc32;
    uint32_t reserved;
    uint64_t my_lba;
    uint64_t alternate_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t entry_lba;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t entry_crc32;
} __attribute__((packed)) gpt_header_t;

typedef struct {
    uint8_t  type_guid[16];
    uint8_t  part_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attrs;
    uint8_t  name[72];
} __attribute__((packed)) gpt_entry_t;

typedef struct {
    gpt_header_t hdr;
    uint8_t      entries[GPT_MAX_ENTRIES * GPT_ENTRY_BYTES];
    uint8_t      pmbr[BLK_SECTOR]; /* LBA0 as found on the source disk */
    int          count;            /* number of entries in the table */
} gpt_table_t;

/* Read + validate the GPT from a device (signature, entry CRC).  Also
 * captures LBA0 for the protective MBR.  0 on success; negative errors:
 * -1 io, -2 no GPT, -3 malformed header, -4 entry table io, -5 entry CRC. */
int gpt_read(blockdev_t* dev, gpt_table_t* t);

/* True when the device carries a valid GPT (used for the --force guard). */
int gpt_probe(blockdev_t* dev);

const gpt_entry_t* gpt_entry_at(const gpt_table_t* t, int i);
int  gpt_entry_used(const gpt_entry_t* e);
int  gpt_is_esp(const gpt_entry_t* e);
void gpt_entry_name(const gpt_entry_t* e, char* out, int cap);
const char* gpt_type_name(const gpt_entry_t* e);

/* First used entry of the given type, 1-based; 0 when absent. */
int gpt_find_type(const gpt_table_t* t, const uint8_t type[16]);

uint32_t gpt_crc32(const void* data, uint32_t len);

/* Minimum target capacity (sectors) that still fits a 1:1 clone of src. */
uint64_t gpt_clone_min_sectors(const gpt_table_t* src);

/* Write the protective MBR + patched primary header + entry array. */
int gpt_write_primary(blockdev_t* dev, const gpt_table_t* src,
        uint64_t dst_sectors);
/* Write the backup entry array + backup header at the target tail. */
int gpt_write_backup(blockdev_t* dev, const gpt_table_t* src,
        uint64_t dst_sectors);

#ifdef __cplusplus
}
#endif

#endif
