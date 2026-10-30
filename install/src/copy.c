#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <ewoksys/kernel_tic.h>

#include <install/copy.h>

/* 1 MiB per in-flight chunk; the q8b backends split further against their
 * driver limits (SDCC2 ADMA batches, NVMe max_xfer). */
#define COPY_BUF_SECTORS 2048

struct copy_job {
    blockdev_t* src;
    blockdev_t* dst;
    uint64_t src_lba;
    uint64_t dst_lba;
    uint64_t sectors;
    uint64_t done;
    char     label[64];
    int      mode;
    int      verify;
    int      state; /* COPY_RUNNING / COPY_DONE / negative error */

    uint8_t* buf;  /* source data (WRITE) / device-a data (CHECK) */
    uint8_t* rbuf; /* read-back (WRITE verify) / device-b data (CHECK) */

    uint64_t err_lba; /* precise sector of a verify mismatch */

    uint64_t t0;
    copy_progress_cb cb;
    void* cb_arg;
};

static uint64_t now_ns(void) {
    uint64_t ns = 0;
    if (kernel_tic_nsec(&ns) != 0)
        return 0;
    return ns;
}

copy_job_t* copy_start(blockdev_t* src, blockdev_t* dst,
        uint64_t src_lba, uint64_t dst_lba, uint64_t sectors,
        const char* label, int mode, int verify,
        copy_progress_cb cb, void* cb_arg) {
    copy_job_t* j = (copy_job_t*)calloc(1, sizeof(copy_job_t));
    if (j == NULL)
        return NULL;

    j->src = src;
    j->dst = dst;
    j->src_lba = src_lba;
    j->dst_lba = dst_lba;
    j->sectors = sectors;
    j->mode = mode;
    j->verify = verify;
    j->state = COPY_RUNNING;
    j->t0 = now_ns();
    j->cb = cb;
    j->cb_arg = cb_arg;
    snprintf(j->label, sizeof(j->label), "%s", label != NULL ? label : "");

    if (mode == COPY_MODE_WRITE && dst == NULL)
        mode = COPY_MODE_CHECK; /* degenerate: source-only compare */
    if (mode != COPY_MODE_WRITE && mode != COPY_MODE_CHECK) {
        free(j);
        return NULL;
    }

    j->buf = (uint8_t*)malloc(COPY_BUF_SECTORS * BLK_SECTOR);
    if (mode == COPY_MODE_WRITE && verify)
        j->rbuf = (uint8_t*)malloc(COPY_BUF_SECTORS * BLK_SECTOR);
    if (mode == COPY_MODE_CHECK)
        j->rbuf = (uint8_t*)malloc(COPY_BUF_SECTORS * BLK_SECTOR);
    if (j->buf == NULL || (mode == COPY_MODE_CHECK && j->rbuf == NULL) ||
            (mode == COPY_MODE_WRITE && verify && j->rbuf == NULL)) {
        copy_finish(j);
        return NULL;
    }
    return j;
}

/* One chunk of IO; returns 0 chunk done, COPY_DONE, or a negative error. */
static int chunk(copy_job_t* j) {
    uint32_t n = (j->sectors - j->done > COPY_BUF_SECTORS) ?
            COPY_BUF_SECTORS : (uint32_t)(j->sectors - j->done);

    if (j->src->read(j->src, j->src_lba + j->done, j->buf, n) != 0)
        return COPY_ERR_IO;

    if (j->mode == COPY_MODE_WRITE) {
        if (j->dst->write(j->dst, j->dst_lba + j->done, j->buf, n) != 0)
            return COPY_ERR_IO;
        if (j->verify) {
            if (j->dst->read(j->dst, j->dst_lba + j->done, j->rbuf, n) != 0)
                return COPY_ERR_IO;
            if (memcmp(j->rbuf, j->buf, (size_t)n * BLK_SECTOR) != 0) {
                uint32_t i;
                for (i = 0; i < n; i++) {
                    if (memcmp(j->rbuf + (size_t)i * BLK_SECTOR,
                                j->buf + (size_t)i * BLK_SECTOR,
                                BLK_SECTOR) != 0)
                        break;
                }
                j->err_lba = j->dst_lba + j->done + i;
                return COPY_ERR_VERIFY;
            }
        }
    } else {
        if (j->dst->read(j->dst, j->dst_lba + j->done, j->rbuf, n) != 0)
            return COPY_ERR_IO;
        if (memcmp(j->rbuf, j->buf, (size_t)n * BLK_SECTOR) != 0) {
            /* locate the first bad sector for a precise message */
            uint32_t i;
            for (i = 0; i < n; i++) {
                if (memcmp(j->buf + (size_t)i * BLK_SECTOR,
                            j->rbuf + (size_t)i * BLK_SECTOR,
                            BLK_SECTOR) != 0)
                    break;
            }
            j->err_lba = j->dst_lba + j->done + i;
            return COPY_ERR_VERIFY;
        }
    }

    j->done += n;
    if (j->cb != NULL)
        j->cb(j->cb_arg, j->label, j->done, j->sectors, now_ns() - j->t0);
    if (j->done >= j->sectors) {
        if (j->mode == COPY_MODE_WRITE && j->dst->flush(j->dst) != 0)
            return COPY_ERR_IO;
        return COPY_DONE;
    }
    return 0;
}

int copy_step(copy_job_t* job, uint32_t budget_ms) {
    uint64_t t0 = now_ns();

    if (job->state != COPY_RUNNING)
        return job->state;
    if (job->sectors == 0) {
        job->state = COPY_DONE;
        return job->state;
    }

    while (1) {
        int st = chunk(job);
        if (st != 0) {
            job->state = st;
            return st;
        }
        if (budget_ms == 0 ||
                (now_ns() - t0) >= (uint64_t)budget_ms * 1000000ull)
            return COPY_RUNNING;
    }
}

void copy_finish(copy_job_t* job) {
    if (job == NULL)
        return;
    free(job->buf);
    free(job->rbuf);
    free(job);
}

uint64_t copy_job_done(const copy_job_t* job) {
    return job->done;
}

uint64_t copy_job_total(const copy_job_t* job) {
    return job->sectors;
}

uint64_t copy_job_ns(const copy_job_t* job) {
    return now_ns() - job->t0;
}

const char* copy_job_label(const copy_job_t* job) {
    return job->label;
}

uint64_t copy_job_err_lba(const copy_job_t* job) {
    return job->err_lba;
}

/* ------------------------------------------------------------------ */
/* blocking wrappers (CLI)                                             */
/* ------------------------------------------------------------------ */

static void print_speed(const char* label, uint64_t sectors,
        uint64_t ns) {
    double mb = (double)sectors * BLK_SECTOR / (1024.0 * 1024.0);
    double sec = (double)ns / 1e9;
    if (ns == 0)
        sec = 1;
    printf("%s: %llu MiB in %llu.%03u s (%u.%u MiB/s)\n",
            label,
            (unsigned long long)(sectors >> 11),
            (unsigned long long)(ns / 1000000000ull),
            (unsigned)((ns / 1000000ull) % 1000),
            (unsigned)(mb / sec),
            (unsigned)((mb / sec * 10.0)) % 10);
}

static void cli_progress(void* arg, const char* label, uint64_t done,
        uint64_t total, uint64_t ns) {
    uint64_t* last_tick = (uint64_t*)arg;
    if (done - *last_tick < COPY_PROGRESS_SECTORS && done != total)
        return;
    *last_tick = done;
    double pct = (double)done * 100.0 / (double)total;
    double mbps = ns != 0 ?
            ((double)done * BLK_SECTOR / (1024.0 * 1024.0)) /
            ((double)ns / 1e9) : 0.0;
    printf("\r%s: %llu/%llu MiB (%u.%u%%, %u.%u MiB/s)  ",
            label,
            (unsigned long long)(done >> 11),
            (unsigned long long)(total >> 11),
            (unsigned)pct, (unsigned)(pct * 10.0) % 10,
            (unsigned)mbps, (unsigned)(mbps * 10.0) % 10);
    fflush(stdout);
}

int copy_range(blockdev_t* src, blockdev_t* dst,
        uint64_t src_lba, uint64_t dst_lba, uint64_t sectors,
        const char* label, int verify) {
    uint64_t last_tick = 0;
    copy_job_t* j = copy_start(src, dst, src_lba, dst_lba, sectors,
            label, COPY_MODE_WRITE, verify, cli_progress, &last_tick);
    int st;

    if (j == NULL) {
        printf("%s: out of memory\n", label != NULL ? label : "copy");
        return -1;
    }
    do {
        st = copy_step(j, 250);
        if (st == COPY_ERR_IO)
            printf("%s: io error at +%llu/%llu\n", j->label,
                    (unsigned long long)(copy_job_done(j) + 1),
                    (unsigned long long)copy_job_total(j));
        else if (st == COPY_ERR_VERIFY)
            printf("%s: verify mismatch at LBA %llu\n", j->label,
                    (unsigned long long)copy_job_err_lba(j));
    } while (st == COPY_RUNNING);
    printf("\n");
    if (st == COPY_DONE)
        print_speed(j->label, sectors, copy_job_ns(j));
    copy_finish(j);
    return st == COPY_DONE ? 0 : (st == COPY_ERR_VERIFY ? -2 : -1);
}

int verify_range(blockdev_t* a, blockdev_t* b, uint64_t lba,
        uint64_t sectors, const char* label) {
    uint64_t last_tick = 0;
    copy_job_t* j = copy_start(a, b, lba, lba, sectors, label,
            COPY_MODE_CHECK, 0, cli_progress, &last_tick);
    int st;

    if (j == NULL) {
        printf("%s: out of memory\n", label != NULL ? label : "verify");
        return -1;
    }
    do {
        st = copy_step(j, 250);
        if (st == COPY_ERR_IO)
            printf("%s: io error at +%llu\n", j->label,
                    (unsigned long long)(copy_job_done(j) + 1));
        else if (st == COPY_ERR_VERIFY)
            printf("%s: MISMATCH at LBA %llu\n", j->label,
                    (unsigned long long)copy_job_err_lba(j));
    } while (st == COPY_RUNNING);
    printf("\n");
    if (st == COPY_DONE)
        print_speed(j->label, sectors, copy_job_ns(j));
    copy_finish(j);
    return st == COPY_DONE ? 0 : (st == COPY_ERR_VERIFY ? -2 : -1);
}
