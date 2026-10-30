/*
 * q8b backend: SD card (source) and M.2 NVMe (target).
 *
 * Both drivers are the board's own user-space implementations, linked into
 * the installer:
 *   - SD:  machines/q8b/system/libs/bsp/src/{bsp_sd,sdcc2}.c via libsd;
 *     bsp_sd_init_physical() force-probes the physical card even when the
 *     firmware supplied a RAM-disk rootfs.
 *   - NVMe: machines/q8b/system/libs/bsp/src/nvme.c; the EDK2 firmware has
 *     already performed PCIe bring-up and link training before
 *     ExitBootServices, so the driver only walks the ECAM, maps BAR0 and
 *     runs polled admin/IO queues.
 *
 * The SD controller is re-initialised out from under the running system:
 * like sdupdate/espwrite, the caller must reboot after installing.
 */
#include <string.h>
#include <stdio.h>

#include <bsp/bsp_sd.h>
#include <bsp/sdcc2.h>
#include <bsp/nvme.h>
#include <sd/sd.h>

#include <install/blockdev.h>

#include "mach.h"

/* libsd takes int32 sector numbers and the SDCC2 backend moves at most a
 * few dozen sectors per command; 128 KiB keeps every call well inside the
 * driver's ADMA/PIO limits and int32 range. */
#define SD_XFER_SECTORS 256

/* ------------------------------------------------------------------ */
/* SD card (source)                                                    */
/* ------------------------------------------------------------------ */

static int q8b_sd_init(blockdev_t* dev) {
    if (bsp_sd_init_physical() != 0)
        return -1;
    /* Bulk raw IO; the libsd sector cache only adds copies here. */
    sd_enable_sector_buffer(0);
    snprintf(dev->detail, sizeof(dev->detail), "SDCC2, %llu MiB",
            (unsigned long long)(sdcc2_capacity_sectors() >> 11));
    return 0;
}

static int q8b_sd_sectors(blockdev_t* dev, uint64_t* count) {
    (void)dev;
    uint64_t n = sdcc2_capacity_sectors();
    if (n == 0)
        return -1;
    *count = n;
    return 0;
}

static int q8b_sd_read(blockdev_t* dev, uint64_t lba, void* buf,
        uint32_t count) {
    (void)dev;
    uint8_t* p = (uint8_t*)buf;
    while (count > 0) {
        uint32_t n = count > SD_XFER_SECTORS ? SD_XFER_SECTORS : count;
        if (lba + n > 0x7fffffffULL)
            return -1;
        if (sd_read_sectors((int32_t)lba, p, n) !=
                (int32_t)(n * BLK_SECTOR))
            return -1;
        lba += n;
        p += (size_t)n * BLK_SECTOR;
        count -= n;
    }
    return 0;
}

static int q8b_sd_write(blockdev_t* dev, uint64_t lba, const void* buf,
        uint32_t count) {
    (void)dev;
    const uint8_t* p = (const uint8_t*)buf;
    while (count > 0) {
        uint32_t n = count > SD_XFER_SECTORS ? SD_XFER_SECTORS : count;
        if (lba + n > 0x7fffffffULL)
            return -1;
        if (sd_write_sectors((int32_t)lba, p, n) !=
                (int32_t)(n * BLK_SECTOR))
            return -1;
        lba += n;
        p += (size_t)n * BLK_SECTOR;
        count -= n;
    }
    return 0;
}

static int q8b_sd_flush(blockdev_t* dev) {
    (void)dev;
    return sd_flush();
}

static void q8b_sd_shutdown(blockdev_t* dev) {
    (void)dev;
    sd_quit();
}

int q8b_sd_dev(blockdev_t* out) {
    memset(out, 0, sizeof(*out));
    out->name = "SD card";
    out->init = q8b_sd_init;
    out->sectors = q8b_sd_sectors;
    out->read = q8b_sd_read;
    out->write = q8b_sd_write;
    out->flush = q8b_sd_flush;
    out->shutdown = q8b_sd_shutdown;
    return 0;
}

/* ------------------------------------------------------------------ */
/* NVMe (target)                                                       */
/* ------------------------------------------------------------------ */

static nvme_info_t _nvme;

static int q8b_nvme_init(blockdev_t* dev) {
    nvme_pci_dev_t devs[8];
    int n = q8b_pcie_scan(NVME_SLOT_PCIE2A, devs, 8);
    if (n <= 0) {
        /* Same policy as nvprobe: pcie3b config reads external-abort while
         * the slot is unpowered, so only try it when pcie2a came up empty. */
        n = q8b_pcie_scan(NVME_SLOT_PCIE3B, devs, 8);
        if (n <= 0)
            return -1;
    }
    if (nvme_open(&_nvme, &devs[0]) != 0)
        return -1;
    /* GPT structures and the copy engine are 512-byte oriented. */
    if (_nvme.block_size != BLK_SECTOR) {
        nvme_close();
        return -2;
    }
    snprintf(dev->detail, sizeof(dev->detail), "%s %s (fw %s)",
            _nvme.model, _nvme.serial, _nvme.fw_rev);
    return 0;
}

static int q8b_nvme_sectors(blockdev_t* dev, uint64_t* count) {
    (void)dev;
    *count = _nvme.nsze * (_nvme.block_size / BLK_SECTOR);
    return 0;
}

/* nvme_read/nvme_write report back the transferred block count; LBA units
 * are the drive's logical blocks, which the 512-byte guard in init makes
 * identical to this tool's sectors. */
static int q8b_nvme_read(blockdev_t* dev, uint64_t lba, void* buf,
        uint32_t count) {
    (void)dev;
    return nvme_read(lba, buf, count) == (int)count ? 0 : -1;
}

static int q8b_nvme_write(blockdev_t* dev, uint64_t lba, const void* buf,
        uint32_t count) {
    (void)dev;
    return nvme_write(lba, buf, count) == (int)count ? 0 : -1;
}

static int q8b_nvme_flush(blockdev_t* dev) {
    (void)dev;
    /* The driver has no NVMe FLUSH opcode; every command completes through
     * the polled queue before nvme_write returns, and the installer's
     * read-back verify is the durability gate. */
    return 0;
}

static void q8b_nvme_shutdown(blockdev_t* dev) {
    (void)dev;
    nvme_close();
}

int q8b_nvme_dev(blockdev_t* out) {
    memset(out, 0, sizeof(*out));
    out->name = "NVMe";
    out->init = q8b_nvme_init;
    out->sectors = q8b_nvme_sectors;
    out->read = q8b_nvme_read;
    out->write = q8b_nvme_write;
    out->flush = q8b_nvme_flush;
    out->shutdown = q8b_nvme_shutdown;
    return 0;
}
