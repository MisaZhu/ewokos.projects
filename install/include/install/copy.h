#ifndef __INSTALL_COPY_H
#define __INSTALL_COPY_H

/*
 * Chunked copy/verify engine.
 *
 * A job is an incremental state machine: copy_step() runs chunks for at
 * most budget_ms wall time and returns, so both the blocking CLI and the
 * GUI event loop can drive the same engine.  All state is owned by the
 * job; devices are not touched outside step calls.
 */

#include <stdint.h>
#include <install/blockdev.h>

/* copy_step / job results */
#define COPY_RUNNING     0
#define COPY_DONE        1
#define COPY_ERR_IO     -1
#define COPY_ERR_VERIFY -2
#define COPY_ERR_MEM    -3

/* job direction */
#define COPY_MODE_WRITE 1 /* read src, write dst (optionally read-back verify) */
#define COPY_MODE_CHECK 2 /* read src + dst, compare (verify pass) */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct copy_job copy_job_t;

/* Progress cadence (sectors) used by the blocking wrappers; shared by the
 * CLI clone runner so both print at the same rhythm. */
#define COPY_PROGRESS_SECTORS (16 * 1024)

/* Called after every completed chunk.  ns is the elapsed time since the
 * job started; label is the user-facing range label. */
typedef void (*copy_progress_cb)(void* arg, const char* label,
        uint64_t done, uint64_t total, uint64_t ns);

/* Allocate a job.  In WRITE mode `verify` selects per-chunk read-back. */
copy_job_t* copy_start(blockdev_t* src, blockdev_t* dst,
        uint64_t src_lba, uint64_t dst_lba, uint64_t sectors,
        const char* label, int mode, int verify,
        copy_progress_cb cb, void* cb_arg);

/* Run chunks for up to budget_ms milliseconds (budget <= 0: one chunk). */
int copy_step(copy_job_t* job, uint32_t budget_ms);

void copy_finish(copy_job_t* job);
uint64_t copy_job_done(const copy_job_t* job);
uint64_t copy_job_total(const copy_job_t* job);
uint64_t copy_job_ns(const copy_job_t* job);
const char* copy_job_label(const copy_job_t* job);
/* First mismatching sector after a COPY_ERR_VERIFY result. */
uint64_t copy_job_err_lba(const copy_job_t* job);

/* Blocking convenience wrappers (CLI).  Progress lines are printed in
 * place.  Returns 0 ok, -1 io error, -2 verify mismatch. */
int copy_range(blockdev_t* src, blockdev_t* dst,
        uint64_t src_lba, uint64_t dst_lba, uint64_t sectors,
        const char* label, int verify);

/* Read the same range from both devices and compare.  Returns 0 when
 * identical, -2 on the first mismatch (with the LBA printed). */
int verify_range(blockdev_t* a, blockdev_t* b, uint64_t lba,
        uint64_t sectors, const char* label);

#ifdef __cplusplus
}
#endif

#endif
