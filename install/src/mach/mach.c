#include <string.h>

#include "mach.h"

static const machine_backend_t _machines[] = {
    /* qcom8gen3 (HW name "8gen3"): boots from the SDCC2 card; installs
     * onto the M.2 NVMe the firmware already brought up before
     * ExitBootServices. */
    { "8gen3", qcom8gen3_sd_dev, qcom8gen3_nvme_dev },
    /* raspi5 could plug in here: machines/raspi5 already carries a
     * user-space NVMe driver (system/libs/bsp/src/bsp_nvme.c) next to its
     * SD host backend. */
};

#define MACH_NUM ((int)(sizeof(_machines) / sizeof(_machines[0])))

const machine_backend_t* mach_find(const char* name) {
    if (name == NULL)
        return NULL;
    for (int i = 0; i < MACH_NUM; i++) {
        if (strcmp(_machines[i].name, name) == 0)
            return &_machines[i];
    }
    return NULL;
}

const machine_backend_t* mach_default(void) {
    return &_machines[0];
}

int mach_count(void) {
    return MACH_NUM;
}

const machine_backend_t* mach_at(int i) {
    if (i < 0 || i >= MACH_NUM)
        return NULL;
    return &_machines[i];
}
