#ifndef __INSTALL_MACH_H
#define __INSTALL_MACH_H

/*
 * Machine backend registry.
 *
 * Each board provides two block devices: the media ewokos currently boots
 * from (source) and the media to install onto (target).  The backends link
 * the board's own user-space drivers, so the installer carries its device
 * support with it and never goes through the running system's daemons.
 */

#include <install/blockdev.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct machine_backend {
    const char* name;
    int (*source)(blockdev_t* out); /* fill vtable for the boot media */
    int (*target)(blockdev_t* out); /* fill vtable for the install media */
} machine_backend_t;

const machine_backend_t* mach_find(const char* name);
const machine_backend_t* mach_default(void);
int mach_count(void);
const machine_backend_t* mach_at(int i);

/* q8b backend (mach/q8b.c) */
int q8b_sd_dev(blockdev_t* out);
int q8b_nvme_dev(blockdev_t* out);

#ifdef __cplusplus
}
#endif

#endif
