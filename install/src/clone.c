#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <ewoksys/kernel_tic.h>

#include <install/clone.h>

/* Internal runner states.  WRITE: patch the primary GPT, run one copy job
 * per selected partition, then relocate the backup GPT.  CHECK: one check
 * job per selected partition. */
enum {
    CRS_GPT_PRIMARY = 0,
    CRS_PARTITIONS  = 1,
    CRS_GPT_BACKUP  = 2,
    CRS_DONE        = 3,
    CRS_FAILED      = 4
};

struct clone_runner {
    clone_cfg_t cfg;
    gpt_table_t gpt;      /* private copy; cfg.gpt may go away */
    int      state;
    int      index;       /* last partition index tried */
    int      cur_part;    /* 1-based number for clone_partition() */
    uint64_t total;
    uint64_t done;        /* sectors of fully finished partitions */
    copy_job_t* job;
    char     label[64];
};

static uint64_t now_ns(void) {
    uint64_t ns = 0;
    if (kernel_tic_nsec(&ns) != 0)
        return 0;
    return ns;
}

static void part_label(const gpt_table_t* t, int idx1, char* out, int cap) {
    const gpt_entry_t* e = gpt_entry_at(t, idx1 - 1);
    char name[40];
    gpt_entry_name(e, name, sizeof(name));
    if (name[0] != 0)
        snprintf(out, (size_t)cap, "part%d '%s'", idx1, name);
    else
        snprintf(out, (size_t)cap, "part%d (%s)", idx1, gpt_type_name(e));
}

/* Does this runner process partition table entry `idx1` (1-based)? */
static int selected(const clone_runner_t* r, int idx1) {
    const gpt_entry_t* e = gpt_entry_at(&r->gpt, idx1 - 1);
    if (e == NULL || !gpt_entry_used(e))
        return 0;
    if (r->cfg.only_part > 0 && r->cfg.only_part != idx1)
        return 0;
    if (r->cfg.only_esp && !gpt_is_esp(e))
        return 0;
    return 1;
}

clone_runner_t* clone_start(const clone_cfg_t* cfg) {
    clone_runner_t* r;

    if (cfg == NULL || cfg->src == NULL || cfg->gpt == NULL ||
            (cfg->mode != CLONE_MODE_WRITE && cfg->mode != CLONE_MODE_CHECK))
        return NULL;
    if (cfg->mode == CLONE_MODE_WRITE && cfg->dst == NULL)
        return NULL;
    r = (clone_runner_t*)calloc(1, sizeof(clone_runner_t));
    if (r == NULL)
        return NULL;

    r->cfg = *cfg;
    r->gpt = *cfg->gpt;
    r->cfg.gpt = NULL;

    /* pre-compute the partition total for progress aggregation */
    for (int i = 1; i <= r->gpt.count; i++) {
        if (!selected(r, i))
            continue;
        const gpt_entry_t* e = gpt_entry_at(&r->gpt, i - 1);
        r->total += e->last_lba - e->first_lba + 1;
    }
    if (r->total == 0) {
        free(r);
        return NULL;
    }

    r->state = (cfg->mode == CLONE_MODE_WRITE) ?
            CRS_GPT_PRIMARY : CRS_PARTITIONS;
    snprintf(r->label, sizeof(r->label), "GPT");
    return r;
}

/* Advance to the next selected partition and open its job.
 * Returns 0 job opened, 1 no more partitions, -1 out of memory. */
static int next_partition_job(clone_runner_t* r) {
    while (r->index < r->gpt.count) {
        const gpt_entry_t* e;
        int idx1;

        r->index++;
        idx1 = r->index;
        if (!selected(r, idx1))
            continue;
        e = gpt_entry_at(&r->gpt, idx1 - 1);
        part_label(&r->gpt, idx1, r->label, sizeof(r->label));
        r->cur_part = idx1;
        /* both modes walk the identical LBA range on the two devices */
        r->job = copy_start(r->cfg.src, r->cfg.dst,
                e->first_lba, e->first_lba,
                e->last_lba - e->first_lba + 1,
                r->label, r->cfg.mode,
                r->cfg.verify, r->cfg.progress, r->cfg.progress_arg);
        return r->job != NULL ? 0 : -1;
    }
    return 1;
}

int clone_step(clone_runner_t* r, uint32_t budget_ms) {
    uint64_t t0 = now_ns();
    uint64_t deadline = (budget_ms == 0) ? ~0ull :
            t0 + (uint64_t)budget_ms * 1000000ull;

    while (1) {
        if (r->state == CRS_DONE)
            return COPY_DONE;
        if (r->state == CRS_FAILED)
            return COPY_ERR_IO;

        if (r->state == CRS_GPT_PRIMARY) {
            r->cur_part = 0;
            snprintf(r->label, sizeof(r->label), "GPT");
            if (gpt_write_primary(r->cfg.dst, &r->gpt,
                        r->cfg.dst_sectors) != 0) {
                r->state = CRS_FAILED;
                return COPY_ERR_IO;
            }
            r->index = 0;
            r->state = CRS_PARTITIONS;
        } else if (r->state == CRS_PARTITIONS) {
            if (r->job == NULL) {
                int have = next_partition_job(r);
                if (have < 0) {
                    r->state = CRS_FAILED;
                    return COPY_ERR_MEM;
                }
                if (have > 0) {
                    /* no more partitions */
                    r->state = (r->cfg.mode == CLONE_MODE_WRITE) ?
                            CRS_GPT_BACKUP : CRS_DONE;
                    continue;
                }
            }
            {
                int st = copy_step(r->job, budget_ms);
                if (st < 0) {
                    r->state = CRS_FAILED;
                    return st;
                }
                if (st == COPY_DONE) {
                    r->done += copy_job_total(r->job);
                    copy_finish(r->job);
                    r->job = NULL;
                    /* fall through; the next iteration opens the next
                     * partition while budget remains */
                } else {
                    return COPY_RUNNING;
                }
            }
        } else if (r->state == CRS_GPT_BACKUP) {
            r->cur_part = 0;
            snprintf(r->label, sizeof(r->label), "GPT backup");
            if (gpt_write_backup(r->cfg.dst, &r->gpt,
                        r->cfg.dst_sectors) != 0) {
                r->state = CRS_FAILED;
                return COPY_ERR_IO;
            }
            r->state = CRS_DONE;
        }

        if (now_ns() >= deadline)
            return r->state == CRS_DONE ? COPY_DONE : COPY_RUNNING;
    }
}

const char* clone_label(const clone_runner_t* r) {
    return r->label;
}

void clone_progress(const clone_runner_t* r, uint64_t* done,
        uint64_t* total) {
    if (done != NULL)
        *done = r->done + (r->job != NULL ? copy_job_done(r->job) : 0);
    if (total != NULL)
        *total = r->total;
}

int clone_partition(const clone_runner_t* r) {
    return r->cur_part;
}

uint64_t clone_err_lba(const clone_runner_t* r) {
    return r->job != NULL ? copy_job_err_lba(r->job) : 0;
}

void clone_finish(clone_runner_t* r) {
    if (r == NULL)
        return;
    copy_finish(r->job);
    free(r);
}
