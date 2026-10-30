/*
 * install - install EwokOS onto another boot medium (CLI).
 *
 * The installer clones the current boot media onto the target medium at
 * raw-sector level: protective MBR, GPT, every partition range, and the
 * backup GPT relocated to the target tail.  It links the machine's
 * user-space device drivers directly (see src/mach/), so it works no
 * matter which filesystem daemons are running.
 *
 *   install scan                       probe devices, show GPT layouts
 *   install plan                       dry run of `disk`
 *   install disk [--force] [--no-verify]
 *                                      full clone (q8b: SD card -> NVMe)
 *   install part <n> [--no-verify]     clone partition n only
 *   install boot [--no-verify]         clone the EFI System Partition only
 *   install verify                     re-compare all partitions
 *
 * The GUI front end (gui/) drives the same engine; this binary is the
 * console/SSH path.
 *
 * Machine backends are registered in src/mach/mach.c; the backend name is
 * accepted as an optional trailing argument (default: q8b).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ewoksys/kernel_tic.h>

#include <install/blockdev.h>
#include <install/gpt.h>
#include <install/clone.h>

#include "mach/mach.h"

static uint64_t now_ns(void) {
    uint64_t ns = 0;
    if (kernel_tic_nsec(&ns) != 0)
        return 0;
    return ns;
}

typedef struct {
    blockdev_t src;
    blockdev_t dst;
    int src_ok;
    int dst_ok;
} ctx_t;

static gpt_table_t src_gpt;

static void usage(void) {
    printf("usage: install <command> [options] [machine]\n"
           "\n"
           "commands:\n"
           "  scan                        probe devices, show GPT layouts\n"
           "  plan                        dry run of `disk`\n"
           "  disk [--force] [--no-verify]\n"
           "                              clone GPT + all partitions\n"
           "  part <n> [--no-verify]      clone partition <n> only\n"
           "  boot [--no-verify]          clone the EFI System Partition\n"
           "  verify                      re-compare all partitions\n"
           "\n"
           "machines:");
    for (int i = 0; i < mach_count(); i++)
        printf(" %s", mach_at(i)->name);
    printf(" (default: %s)\n", mach_default()->name);
}

/* ------------------------------------------------------------------ */
/* device / GPT helpers                                                */
/* ------------------------------------------------------------------ */

static void dev_close(ctx_t* c) {
    if (c->dst_ok)
        c->dst.shutdown(&c->dst);
    if (c->src_ok)
        c->src.shutdown(&c->src);
}

static int dev_open(blockdev_t* d) {
    uint64_t sectors = 0;
    if (d->init(d) != 0)
        return -1;
    if (d->sectors(d, &sectors) != 0)
        return -1;
    if (d->detail[0] != 0)
        printf("install: %s: %llu MiB (%llu sectors) -- %s\n", d->name,
                (unsigned long long)(sectors >> 11),
                (unsigned long long)sectors, d->detail);
    else
        printf("install: %s: %llu MiB (%llu sectors)\n", d->name,
                (unsigned long long)(sectors >> 11),
                (unsigned long long)sectors);
    return 0;
}

static int open_pair(const machine_backend_t* m, ctx_t* c) {
    memset(c, 0, sizeof(*c));
    m->source(&c->src);
    m->target(&c->dst);

    if (dev_open(&c->src) != 0) {
        printf("install: source (%s) init failed\n", c->src.name);
        return -1;
    }
    c->src_ok = 1;
    if (dev_open(&c->dst) != 0) {
        printf("install: target (%s) init failed -- no install medium?\n",
                c->dst.name);
        dev_close(c);
        return -1;
    }
    c->dst_ok = 1;
    return 0;
}

static void print_gpt(const char* devname, const gpt_table_t* t) {
    printf("%s GPT: %d entries, usable LBA %llu..%llu\n", devname,
            t->count,
            (unsigned long long)t->hdr.first_usable_lba,
            (unsigned long long)t->hdr.last_usable_lba);
    for (int i = 0; i < t->count; i++) {
        const gpt_entry_t* e = gpt_entry_at(t, i);
        char name[40];
        if (e == NULL || !gpt_entry_used(e))
            continue;
        gpt_entry_name(e, name, sizeof(name));
        printf("  part%-2d %-10s %9llu..%-9llu %6u MiB  %s\n", i + 1,
                gpt_type_name(e),
                (unsigned long long)e->first_lba,
                (unsigned long long)e->last_lba,
                (unsigned)((e->last_lba - e->first_lba + 1) >> 11),
                name);
    }
}

static int read_source_gpt(ctx_t* c) {
    int r = gpt_read(&c->src, &src_gpt);
    if (r == -2)
        printf("install: source has no GPT (MBR-only media not supported)\n");
    else if (r == -3)
        printf("install: source GPT header malformed\n");
    else if (r == -5)
        printf("install: source GPT entry table CRC mismatch\n");
    else if (r != 0)
        printf("install: source GPT read failed (%d)\n", r);
    return r == 0 ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* clone driving                                                       */
/* ------------------------------------------------------------------ */

static int check_target(ctx_t* c, int force, int warn) {
    uint64_t dst_sectors = 0;
    uint64_t need = gpt_clone_min_sectors(&src_gpt);

    c->dst.sectors(&c->dst, &dst_sectors);
    if (dst_sectors < need) {
        printf("install: target too small: %llu sectors, clone needs %llu\n",
                (unsigned long long)dst_sectors,
                (unsigned long long)need);
        return -1;
    }
    if (gpt_probe(&c->dst) && !force) {
        printf("install: target already carries a GPT; refusing to "
                "overwrite without --force\n");
        return -1;
    }
    if (warn) {
        printf("install: WARNING: everything on the target will be "
                "overwritten (%u MiB clone)\n",
                (unsigned)(need >> 11));
        sleep(3);
    }
    return 0;
}

/* Blocking clone run with in-place CLI progress. */
static int run_clone(ctx_t* c, int mode, int verify, int only_part,
        int only_esp) {
    clone_cfg_t cfg;
    clone_runner_t* r;
    uint64_t dst_sectors = 0;
    uint64_t last_tick = 0;
    uint64_t t0 = 0;
    int st;

    memset(&cfg, 0, sizeof(cfg));
    cfg.src = &c->src;
    cfg.dst = &c->dst;
    cfg.gpt = &src_gpt;
    cfg.mode = mode;
    cfg.verify = verify;
    cfg.only_part = only_part;
    cfg.only_esp = only_esp;
    if (mode == CLONE_MODE_WRITE)
        c->dst.sectors(&c->dst, &dst_sectors);
    cfg.dst_sectors = dst_sectors;

    r = clone_start(&cfg);
    if (r == NULL) {
        printf("install: nothing to do (no partitions selected?)\n");
        return 1;
    }

    t0 = now_ns();
    do {
        uint64_t done = 0;
        uint64_t total = 0;
        st = clone_step(r, 250);
        clone_progress(r, &done, &total);
        if (done - last_tick >= COPY_PROGRESS_SECTORS ||
                done == total ||
                st != COPY_RUNNING) {
            uint64_t ns = now_ns() - t0;
            double pct = total != 0 ?
                    (double)done * 100.0 / (double)total : 100.0;
            double mbps = ns != 0 ?
                    ((double)done * BLK_SECTOR / (1024.0 * 1024.0)) /
                    ((double)ns / 1e9) : 0.0;
            printf("\r%s: %llu/%llu MiB (%u.%u%%, %u.%u MiB/s)  ",
                    clone_label(r),
                    (unsigned long long)(done >> 11),
                    (unsigned long long)(total >> 11),
                    (unsigned)pct, (unsigned)(pct * 10.0) % 10,
                    (unsigned)mbps, (unsigned)(mbps * 10.0) % 10);
            fflush(stdout);
            last_tick = done;
        }
        if (st == COPY_ERR_IO)
            printf("\ninstall: io error in %s\n", clone_label(r));
        else if (st == COPY_ERR_VERIFY)
            printf("\ninstall: verify mismatch in %s at LBA %llu\n",
                    clone_label(r),
                    (unsigned long long)clone_err_lba(r));
        else if (st == COPY_ERR_MEM)
            printf("\ninstall: out of memory\n");
    } while (st == COPY_RUNNING);
    printf("\n");
    clone_finish(r);
    return st == COPY_DONE ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* commands                                                            */
/* ------------------------------------------------------------------ */

static int cmd_scan(ctx_t* c) {
    gpt_table_t dst_gpt;

    if (read_source_gpt(c) != 0)
        return 1;
    print_gpt("source", &src_gpt);

    if (gpt_read(&c->dst, &dst_gpt) == 0)
        print_gpt("target", &dst_gpt);
    else
        printf("target: no valid GPT (blank or foreign layout)\n");
    return 0;
}

static int cmd_plan(ctx_t* c) {
    uint64_t dst_sectors = 0;
    uint64_t need;
    uint64_t total = 0;

    if (read_source_gpt(c) != 0)
        return 1;
    print_gpt("source", &src_gpt);
    need = gpt_clone_min_sectors(&src_gpt);
    c->dst.sectors(&c->dst, &dst_sectors);

    printf("plan: 1:1 LBA clone onto the target\n");
    printf("plan: primary GPT at LBA 0..%llu, partitions:\n",
            (unsigned long long)(src_gpt.hdr.entry_lba +
                (src_gpt.hdr.entry_count * src_gpt.hdr.entry_size +
                    BLK_SECTOR - 1) / BLK_SECTOR - 1));
    for (int i = 0; i < src_gpt.count; i++) {
        const gpt_entry_t* e = gpt_entry_at(&src_gpt, i);
        if (e == NULL || !gpt_entry_used(e))
            continue;
        if (e->last_lba >= e->first_lba)
            total += e->last_lba - e->first_lba + 1;
        printf("plan:   part%d %s: %llu MiB\n", i + 1,
                gpt_type_name(e),
                (unsigned long long)((e->last_lba - e->first_lba + 1) >> 11));
    }
    printf("plan: backup GPT at target tail (LBA %llu), %u MiB of "
            "partition data total\n",
            (unsigned long long)(dst_sectors - 1),
            (unsigned)(total >> 11));
    printf("plan: target %s (%llu < %llu sectors)\n",
            dst_sectors >= need ? "is big enough" : "is TOO SMALL",
            (unsigned long long)dst_sectors,
            (unsigned long long)need);
    if (gpt_probe(&c->dst))
        printf("plan: target already carries a GPT -- `disk` will need "
                "--force\n");
    return 0;
}

static int cmd_disk(ctx_t* c, int force, int verify) {
    if (read_source_gpt(c) != 0)
        return 1;
    print_gpt("source", &src_gpt);
    if (check_target(c, force, 1) != 0)
        return 1;
    return run_clone(c, CLONE_MODE_WRITE, verify, 0, 0);
}

static int require_target_gpt(ctx_t* c) {
    if (gpt_probe(&c->dst))
        return 0;
    printf("install: target has no GPT -- run `install disk` first\n");
    return -1;
}

static int cmd_part(ctx_t* c, int n, int verify) {
    if (read_source_gpt(c) != 0)
        return 1;
    if (n < 1 || n > src_gpt.count ||
            !gpt_entry_used(gpt_entry_at(&src_gpt, n - 1))) {
        printf("install: no partition %d on the source\n", n);
        return 1;
    }
    if (require_target_gpt(c) != 0)
        return 1;
    return run_clone(c, CLONE_MODE_WRITE, verify, n, 0);
}

static int cmd_boot(ctx_t* c, int verify) {
    int esp;

    if (read_source_gpt(c) != 0)
        return 1;
    esp = gpt_find_type(&src_gpt, GPT_TYPE_ESP);
    if (esp == 0) {
        printf("install: no EFI System Partition on the source\n");
        return 1;
    }
    if (require_target_gpt(c) != 0)
        return 1;
    return run_clone(c, CLONE_MODE_WRITE, verify, 0, 1);
}

static int cmd_verify(ctx_t* c) {
    if (read_source_gpt(c) != 0)
        return 1;
    if (run_clone(c, CLONE_MODE_CHECK, 0, 0, 0) == 0) {
        printf("install: all partitions identical\n");
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */

int main(int argc, char** argv) {
    const machine_backend_t* mach = mach_default();
    const char* cmd;
    int force = 0;
    int verify = 1;
    int part_n = 0;
    ctx_t ctx;
    int r;

    setbuf(stdout, NULL);
    if (argc < 2) {
        usage();
        return 1;
    }
    cmd = argv[1];
    for (int i = 2; i < argc; i++) {
        const machine_backend_t* m;
        if (strcmp(argv[i], "--force") == 0) {
            force = 1;
        } else if (strcmp(argv[i], "--no-verify") == 0) {
            verify = 0;
        } else if ((m = mach_find(argv[i])) != NULL) {
            mach = m;
        } else if (strcmp(cmd, "part") == 0 &&
                part_n == 0 && argv[i][0] >= '1' && argv[i][0] <= '9') {
            part_n = atoi(argv[i]);
        } else {
            printf("install: unknown argument: %s\n", argv[i]);
            usage();
            return 1;
        }
    }

    if (open_pair(mach, &ctx) != 0)
        return 1;

    if (strcmp(cmd, "scan") == 0)
        r = cmd_scan(&ctx);
    else if (strcmp(cmd, "plan") == 0)
        r = cmd_plan(&ctx);
    else if (strcmp(cmd, "disk") == 0)
        r = cmd_disk(&ctx, force, verify);
    else if (strcmp(cmd, "part") == 0)
        r = part_n > 0 ? cmd_part(&ctx, part_n, verify) :
                (printf("install: part requires a partition number\n"), 1);
    else if (strcmp(cmd, "boot") == 0)
        r = cmd_boot(&ctx, verify);
    else if (strcmp(cmd, "verify") == 0)
        r = cmd_verify(&ctx);
    else {
        printf("install: unknown command: %s\n", cmd);
        usage();
        r = 1;
    }

    if (r == 0) {
        printf("install: done.\n"
               "install: the SD controller was re-initialised; reboot "
               "before the running system touches the card again.\n"
               "install: boot from the NVMe through the firmware boot "
               "menu (Boot From File -> NVMe -> \\EFI\\BOOT\\BOOTAA64."
               "EFI).\n");
    }
    dev_close(&ctx);
    return r;
}
