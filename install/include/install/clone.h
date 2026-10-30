#ifndef __INSTALL_CLONE_H
#define __INSTALL_CLONE_H

/*
 * Multi-partition clone runner.
 *
 * Wraps the whole install operation - primary GPT, every selected
 * partition, backup GPT (write mode) or the full verify pass (check
 * mode) - behind the same step interface as the chunk engine, so the CLI
 * and the GUI drive identical logic.
 */

#include <stdint.h>
#include <install/blockdev.h>
#include <install/gpt.h>
#include <install/copy.h>

#define CLONE_MODE_WRITE 1 /* primary GPT + partitions + backup GPT */
#define CLONE_MODE_CHECK 2 /* compare partitions only */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct clone_runner clone_runner_t;

typedef struct {
    blockdev_t* src;
    blockdev_t* dst;
    const gpt_table_t* gpt;  /* source layout to replicate */
    uint64_t dst_sectors;
    int      mode;           /* CLONE_MODE_* */
    int      verify;         /* WRITE mode: read-back each chunk */
    int      only_part;      /* 0 = all used; else 1-based partition # */
    int      only_esp;       /* non-zero: only the EFI System Partition */

    copy_progress_cb progress; /* per-chunk callback (optional) */
    void*    progress_arg;
} clone_cfg_t;

clone_runner_t* clone_start(const clone_cfg_t* cfg);

/* Run for up to budget_ms milliseconds.
 * Returns COPY_RUNNING / COPY_DONE / COPY_ERR_IO / COPY_ERR_VERIFY /
 * COPY_ERR_MEM.  CLONE_MODE_WRITE also fails with COPY_ERR_IO when either
 * GPT write step fails. */
int clone_step(clone_runner_t* r, uint32_t budget_ms);

/* Current range label, e.g. "part2 'rootfs'" ("GPT" during table writes). */
const char* clone_label(const clone_runner_t* r);

/* Aggregated progress across all partitions of the run.  Callers track
 * elapsed time themselves. */
void clone_progress(const clone_runner_t* r, uint64_t* done,
        uint64_t* total);

/* 1-based partition number currently being processed, 0 during GPT IO. */
int clone_partition(const clone_runner_t* r);

/* First mismatching sector of the failed job after a COPY_ERR_VERIFY. */
uint64_t clone_err_lba(const clone_runner_t* r);

void clone_finish(clone_runner_t* r);

#ifdef __cplusplus
}
#endif

#endif
