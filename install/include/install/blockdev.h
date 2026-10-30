#ifndef __INSTALL_BLOCKDEV_H
#define __INSTALL_BLOCKDEV_H

/*
 * Minimal raw block-device interface for the installer.
 *
 * A backend plugs a source (the media ewokos currently boots from) and a
 * target (the media to install onto) behind these six calls.  Everything
 * above this interface - GPT parsing, cloning, verification - is machine
 * independent, so a new board only needs new init/read/write glue around
 * its own user-space drivers.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLK_SECTOR 512

typedef struct blockdev {
    const char* name;    /* human label for logs, e.g. "SD card" */
    char detail[112];    /* backend-probed description, "" before init */

    /* Probe and bring the device up (map windows, identify, init queues).
     * 0 on success. */
    int  (*init)(struct blockdev* dev);
    /* Capacity in 512-byte sectors. */
    int  (*sectors)(struct blockdev* dev, uint64_t* count);
    /* Raw sector IO.  Implementations must accept any count and split it
     * internally against their driver limits. */
    int  (*read)(struct blockdev* dev, uint64_t lba, void* buf,
            uint32_t count);
    int  (*write)(struct blockdev* dev, uint64_t lba, const void* buf,
            uint32_t count);
    /* Persist any volatile caches (write-back caches etc). */
    int  (*flush)(struct blockdev* dev);
    /* Release the hardware (close queues, re-init the bus, ...). */
    void (*shutdown)(struct blockdev* dev);
} blockdev_t;

#ifdef __cplusplus
}
#endif

#endif
