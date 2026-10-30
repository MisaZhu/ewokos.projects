/*
 * xinstall - GUI front end for the EwokOS installer.
 *
 * Drives the same engine as the `install` CLI (raw 1:1 GPT clone, user
 * space SD + NVMe drivers) from a desktop window: device summary, source
 * partition list, progress bar with live speed, rolling log, and a
 * confirmation dialog before anything destructive happens.
 *
 * All block IO runs inside the window timer tick (clone_step with a small
 * budget), so the event loop stays responsive; the SD controller is still
 * re-initialised by the probe, so the board must reboot after installing.
 */
#include <Widget/WidgetWin.h>
#include <Widget/WidgetX.h>
#include <Widget/Container.h>
#include <Widget/RootWidget.h>
#include <Widget/LabelButton.h>
#include <Widget/List.h>
#include <Widget/Splitter.h>
#include <WidgetEx/ConfirmDialog.h>

#include <x++/X.h>
#include <graph/graph.h>
#include <ewoksys/kernel_tic.h>

#include <install/gpt.h>
#include <install/clone.h>

extern "C" {
#include "mach/mach.h"
}

#include <stdio.h>
#include <string.h>

using namespace Ewok;

#define BAR_COLOR      0xff2f88d0
#define BAR_BG_COLOR   0xff282c34
#define PANEL_BG_COLOR 0xff1e2127
#define LOG_OK_COLOR   0xff9fd09f
#define LOG_ERR_COLOR  0xffe08080
#define LOG_INFO_COLOR 0xffc8c8c8

#define LOG_LINES   64
#define LOG_TEXTLEN 160
#define PART_MAX    64
#define PART_TEXTLEN 128

static const char* DIALOG_INSTALL = "install";

enum RunState {
    ST_IDLE,       /* nothing probed yet */
    ST_OPENING,    /* next timer tick runs the device probe */
    ST_READY,      /* devices probed, buttons armed */
    ST_INSTALLING,
    ST_VERIFYING
};

struct PartLine {
    char text[PART_TEXTLEN];
};

struct LogLine {
    char text[LOG_TEXTLEN];
    uint32_t color;
};

static uint64_t now_ns(void) {
    uint64_t ns = 0;
    if (kernel_tic_nsec(&ns) != 0)
        return 0;
    return ns;
}

/* ------------------------------------------------------------------ */
/* widgets                                                             */
/* ------------------------------------------------------------------ */

class LogPanel: public Widget {
    LogLine lines[LOG_LINES];
    uint32_t head;   /* next write slot */
    uint32_t count;
public:
    LogPanel() {
        head = 0;
        count = 0;
    }

    void append(const char* text, uint32_t color) {
        snprintf(lines[head].text, LOG_TEXTLEN, "%s", text);
        lines[head].color = color;
        head = (head + 1) % LOG_LINES;
        if (count < LOG_LINES)
            count++;
        update();
    }

protected:
    void onRepaint(graph_t* g, XTheme* theme, const grect_t& r) {
        uint32_t fh = theme->basic.fontSize + 4;
        int32_t visible = (int32_t)(r.h / fh);
        int32_t first = 0;
        graph_fill_rect(g, r.x, r.y, r.w, r.h, PANEL_BG_COLOR);
        if ((int32_t)count > visible)
            first = (int32_t)count - visible;
        for (uint32_t i = (uint32_t)first; i < count; i++) {
            uint32_t idx = (head + LOG_LINES - count + i) % LOG_LINES;
            graph_draw_text_font(g, r.x + 6,
                    r.y + (int32_t)(i - first) * (int32_t)fh + 2,
                    lines[idx].text, theme->getFont(),
                    theme->basic.fontSize, lines[idx].color);
        }
    }
};

static LogPanel* g_log = NULL;

static void inst_log(const char* text, uint32_t color) {
    if (g_log != NULL)
        g_log->append(text, color);
}

class InfoPanel: public Widget {
protected:
    void onRepaint(graph_t* g, XTheme* theme, const grect_t& r);
};

class PartList: public List {
protected:
    void drawItem(graph_t* g, XTheme* theme, int32_t index,
            const grect_t& r);
};

class ProgressPanel: public Widget {
protected:
    void onRepaint(graph_t* g, XTheme* theme, const grect_t& r);
};

class InstallWin;
class Installer;

/* ------------------------------------------------------------------ */
/* shared installer state                                              */
/* ------------------------------------------------------------------ */

static Installer* g_inst = NULL;

class Installer {
public:
    const machine_backend_t* mach;
    RunState state;

    blockdev_t src;
    blockdev_t dst;
    bool src_ok;
    bool dst_ok;
    bool dst_has_gpt;

    gpt_table_t src_gpt;
    bool have_gpt;
    uint64_t dst_sectors;

    clone_runner_t* runner;
    uint64_t t0;

    PartLine parts[PART_MAX];
    uint32_t part_count;

    Installer() {
        mach = mach_default();
        state = ST_IDLE;
        src_ok = false;
        dst_ok = false;
        dst_has_gpt = false;
        have_gpt = false;
        dst_sectors = 0;
        runner = NULL;
        t0 = 0;
        part_count = 0;
        memset(&src, 0, sizeof(src));
        memset(&dst, 0, sizeof(dst));
        mach->source(&src);
        mach->target(&dst);
    }

    /* Blocking probe; called from the timer tick so the click that armed
     * it gets painted first. */
    void probe() {
        uint64_t sectors = 0;
        char line[LOG_TEXTLEN];

        state = ST_IDLE;
        src_ok = false;
        dst_ok = false;
        have_gpt = false;
        dst_has_gpt = false;
        part_count = 0;

        if (src.init(&src) != 0 || src.sectors(&src, &sectors) != 0) {
            inst_log("Source probe failed.", LOG_ERR_COLOR);
            return;
        }
        src_ok = true;
        inst_log("Source ready.", LOG_OK_COLOR);

        if (dst.init(&dst) != 0 || dst.sectors(&dst, &sectors) != 0) {
            inst_log("Target probe failed -- no install medium?",
                    LOG_ERR_COLOR);
            return;
        }
        dst_ok = true;
        dst.sectors(&dst, &dst_sectors);
        inst_log("Target ready.", LOG_OK_COLOR);

        int r = gpt_read(&src, &src_gpt);
        if (r == 0) {
            have_gpt = true;
            snprintf(line, sizeof(line), "Source GPT: %d partitions.",
                    src_gpt.count);
            inst_log(line, LOG_INFO_COLOR);
        } else {
            inst_log("Source has no readable GPT.", LOG_ERR_COLOR);
        }
        dst_has_gpt = gpt_probe(&dst) ? true : false;
        if (dst_has_gpt)
            inst_log("Target already carries a GPT.", LOG_INFO_COLOR);
        state = ST_READY;
    }

    void fillParts() {
        part_count = 0;
        if (!have_gpt)
            return;
        for (int i = 0; i < src_gpt.count && part_count < PART_MAX; i++) {
            const gpt_entry_t* e = gpt_entry_at(&src_gpt, i);
            char name[40];
            if (e == NULL || !gpt_entry_used(e))
                continue;
            gpt_entry_name(e, name, sizeof(name));
            snprintf(parts[part_count].text, PART_TEXTLEN,
                    "part%-2d %-10s %9llu..%-9llu %6u MiB  %s", i + 1,
                    gpt_type_name(e),
                    (unsigned long long)e->first_lba,
                    (unsigned long long)e->last_lba,
                    (unsigned)((e->last_lba - e->first_lba + 1) >> 11),
                    name);
            part_count++;
        }
    }
};

/* ------------------------------------------------------------------ */
/* panel painting                                                      */
/* ------------------------------------------------------------------ */

void InfoPanel::onRepaint(graph_t* g, XTheme* theme, const grect_t& r) {
    char line[224];
    graph_fill_rect(g, r.x, r.y, r.w, r.h, PANEL_BG_COLOR);
    if (g_inst == NULL)
        return;

    snprintf(line, sizeof(line), "Source: %s  %s", g_inst->src.name,
            g_inst->src_ok ? g_inst->src.detail : "(not probed)");
    graph_draw_text_font(g, r.x + 8, r.y + 5, line, theme->getFont(),
            theme->basic.fontSize, LOG_INFO_COLOR);

    snprintf(line, sizeof(line), "Target: %s  %s", g_inst->dst.name,
            g_inst->dst_ok ? g_inst->dst.detail : "(not probed)");
    graph_draw_text_font(g, r.x + 8,
            r.y + 7 + (int32_t)theme->basic.fontSize, line,
            theme->getFont(), theme->basic.fontSize, LOG_INFO_COLOR);
}

void PartList::drawItem(graph_t* g, XTheme* theme, int32_t index,
        const grect_t& r) {
    const char* s = "";

    if (g_inst != NULL && index >= 0 &&
            (uint32_t)index < g_inst->part_count)
        s = g_inst->parts[index].text;
    graph_fill_rect(g, r.x, r.y, r.w, r.h,
            index == itemSelected ? theme->basic.selectColor :
            theme->basic.bgColor);
    graph_draw_text_font(g, r.x + 4, r.y + 2, s, theme->getFont(),
            theme->basic.fontSize, theme->basic.fgColor);
}

void ProgressPanel::onRepaint(graph_t* g, XTheme* theme, const grect_t& r) {
    char line[96];
    uint64_t done = 0;
    uint64_t total = 0;
    double pct = 0.0;
    double mbps = 0.0;

    graph_fill_rect(g, r.x, r.y, r.w, r.h, PANEL_BG_COLOR);
    if (g_inst == NULL)
        return;

    if (g_inst->runner != NULL) {
        clone_progress(g_inst->runner, &done, &total);
        if (total != 0)
            pct = (double)done * 100.0 / (double)total;
        if (g_inst->t0 != 0) {
            uint64_t ns = now_ns() - g_inst->t0;
            if (ns != 0)
                mbps = ((double)done * BLK_SECTOR / (1024.0 * 1024.0)) /
                    ((double)ns / 1e9);
        }
    }

    int32_t bx = r.x + 8;
    int32_t by = r.y + 5;
    int32_t bw = (int32_t)r.w - 16;
    int32_t bh = 14;
    graph_fill_rect(g, bx, by, bw, bh, BAR_BG_COLOR);
    if (bw > 2 && pct > 0.0) {
        int32_t fw = (int32_t)((double)(bw - 2) * pct / 100.0);
        if (fw > bw - 2)
            fw = bw - 2;
        if (fw > 0)
            graph_fill_rect(g, bx + 1, by + 1, (uint32_t)fw,
                    (uint32_t)(bh - 2), BAR_COLOR);
    }

    if (g_inst->runner != NULL)
        snprintf(line, sizeof(line), "%s  %llu/%llu MiB  %u.%u%%  "
                "%u.%u MiB/s", clone_label(g_inst->runner),
                (unsigned long long)(done >> 11),
                (unsigned long long)(total >> 11),
                (unsigned)pct, (unsigned)(pct * 10.0) % 10,
                (unsigned)mbps, (unsigned)(mbps * 10.0) % 10);
    else if (g_inst->state == ST_READY)
        snprintf(line, sizeof(line), "Ready.");
    else if (g_inst->state == ST_IDLE)
        snprintf(line, sizeof(line), "Press Scan to probe the devices.");
    else if (g_inst->state == ST_OPENING)
        snprintf(line, sizeof(line), "Probing devices...");
    else
        snprintf(line, sizeof(line), "Working...");
    graph_draw_text_font(g, r.x + 8, r.y + 23, line, theme->getFont(),
            theme->basic.fontSize, theme->basic.fgColor);
}

/* ------------------------------------------------------------------ */
/* main window                                                         */
/* ------------------------------------------------------------------ */

class InstallWin: public WidgetWin {
public:
    LabelButton* scanBtn;
    LabelButton* installBtn;
    LabelButton* verifyBtn;
    LabelButton* exitBtn;
    PartList* partList;
    InfoPanel* infoPanel;
    ProgressPanel* progressPanel;
    ConfirmDialog confirm;

    InstallWin() {
        scanBtn = NULL;
        installBtn = NULL;
        verifyBtn = NULL;
        exitBtn = NULL;
        partList = NULL;
        infoPanel = NULL;
        progressPanel = NULL;
    }

    void beginClone(int mode, int verify) {
        clone_cfg_t cfg;

        if (g_inst == NULL || !g_inst->have_gpt) {
            inst_log("No source GPT -- run Scan first.", LOG_ERR_COLOR);
            return;
        }
        if (g_inst->runner != NULL)
            return;

        memset(&cfg, 0, sizeof(cfg));
        cfg.src = &g_inst->src;
        cfg.dst = &g_inst->dst;
        cfg.gpt = &g_inst->src_gpt;
        cfg.mode = mode;
        cfg.verify = verify;
        cfg.dst_sectors = g_inst->dst_sectors;
        g_inst->runner = clone_start(&cfg);
        if (g_inst->runner == NULL) {
            inst_log("Nothing to clone (no partitions).", LOG_ERR_COLOR);
            return;
        }
        g_inst->t0 = now_ns();
        g_inst->state = (mode == CLONE_MODE_WRITE) ?
                ST_INSTALLING : ST_VERIFYING;
        setBusy(true);
        inst_log(mode == CLONE_MODE_WRITE ?
                "Installing: writing GPT + partitions..." :
                "Verifying partitions...", LOG_INFO_COLOR);
    }

protected:
    void setBusy(bool busy) {
        if (busy) {
            if (scanBtn != NULL) scanBtn->disable();
            if (installBtn != NULL) installBtn->disable();
            if (verifyBtn != NULL) verifyBtn->disable();
        } else {
            if (scanBtn != NULL) scanBtn->enable();
            if (installBtn != NULL) installBtn->enable();
            if (verifyBtn != NULL) verifyBtn->enable();
        }
    }

    void finishClone(int st) {
        if (g_inst == NULL)
            return;
        clone_finish(g_inst->runner);
        g_inst->runner = NULL;
        g_inst->t0 = 0;
        setBusy(false);
        if (st == COPY_DONE) {
            if (g_inst->state == ST_INSTALLING) {
                inst_log("Install complete.", LOG_OK_COLOR);
                confirm.setMessage("Install complete. Reboot the board, "
                        "then boot from the NVMe via the firmware boot "
                        "menu (Boot From File).");
            } else {
                inst_log("Verify: all partitions identical.",
                        LOG_OK_COLOR);
                confirm.setMessage("Verify passed: the target matches "
                        "the source.");
            }
        } else if (st == COPY_ERR_VERIFY) {
            char msg[LOG_TEXTLEN];
            snprintf(msg, sizeof(msg),
                    "FAILED: verify mismatch at LBA %llu.",
                    (unsigned long long)clone_err_lba(g_inst->runner));
            inst_log(msg, LOG_ERR_COLOR);
            confirm.setMessage("Verify mismatch -- the clone is not "
                    "identical. Re-run Install.");
        } else if (st == COPY_ERR_MEM) {
            inst_log("FAILED: out of memory.", LOG_ERR_COLOR);
            confirm.setMessage("Out of memory.");
        } else {
            inst_log("FAILED: io error.", LOG_ERR_COLOR);
            confirm.setMessage("IO error while accessing the devices.");
        }
        g_inst->state = ST_READY;
        confirm.popup(this, 420, 130, "Installer", XWIN_STYLE_NO_TITLE);
        if (progressPanel != NULL)
            progressPanel->update();
    }

    /* all block IO lives here so the event loop keeps dispatching
     * between ticks */
    void onTimer(uint32_t timerFPS, uint32_t timerSteps) {
        (void)timerFPS;
        (void)timerSteps;

        if (g_inst == NULL)
            return;

        if (g_inst->state == ST_OPENING) {
            g_inst->probe();
            g_inst->fillParts();
            if (partList != NULL) {
                partList->setItemNum(g_inst->part_count);
                partList->update();
            }
            if (infoPanel != NULL)
                infoPanel->update();
            if (progressPanel != NULL)
                progressPanel->update();
            return;
        }

        if (g_inst->state == ST_INSTALLING ||
                g_inst->state == ST_VERIFYING) {
            int st = clone_step(g_inst->runner, 20);
            if (st != COPY_RUNNING)
                finishClone(st);
            if (progressPanel != NULL)
                progressPanel->update();
        }
    }

    bool onClose() {
        if (g_inst != NULL && g_inst->runner != NULL) {
            inst_log("Busy: a run is active, cannot close.",
                    LOG_ERR_COLOR);
            return false;
        }
        return true;
    }

public:
    /* Dialog results.  Only the install-confirm dialog (tagged via the
     * popup arg) starts a write; result dialogs just display. */
    void onDialoged(XWin* from, int res, void* arg) {
        (void)from;
        if (res != Dialog::RES_OK || arg != (void*)DIALOG_INSTALL)
            return;
        if (g_inst == NULL || g_inst->state != ST_READY ||
                g_inst->runner != NULL || !g_inst->dst_ok)
            return;
        beginClone(CLONE_MODE_WRITE, 1);
    }
};

/* ------------------------------------------------------------------ */
/* button handlers                                                     */
/* ------------------------------------------------------------------ */

static void on_scan(Widget* wd, xevent_t* evt, void* arg) {
    (void)wd;
    (void)arg;
    if (evt->type != XEVT_MOUSE || evt->state != MOUSE_STATE_CLICK)
        return;
    if (g_inst == NULL || g_inst->state != ST_IDLE ||
            g_inst->runner != NULL)
        return;
    g_inst->state = ST_OPENING;
}

static void on_install(Widget* wd, xevent_t* evt, void* arg) {
    (void)wd;
    InstallWin* win = (InstallWin*)arg;
    if (evt->type != XEVT_MOUSE || evt->state != MOUSE_STATE_CLICK)
        return;
    if (g_inst == NULL || win == NULL || g_inst->state != ST_READY ||
            g_inst->runner != NULL)
        return;
    if (!g_inst->have_gpt) {
        inst_log("Scan first: the source has no readable GPT.",
                LOG_ERR_COLOR);
        return;
    }
    char msg[LOG_TEXTLEN];
    snprintf(msg, sizeof(msg),
            "Install will OVERWRITE everything on the %s%s. Continue?",
            g_inst->dst.name,
            g_inst->dst_has_gpt ? " (already has a GPT)" : "");
    win->confirm.setMessage(msg);
    win->confirm.popup(win, 420, 130, "Confirm install",
            XWIN_STYLE_NO_TITLE, (void*)DIALOG_INSTALL);
}

static void on_verify(Widget* wd, xevent_t* evt, void* arg) {
    (void)wd;
    InstallWin* win = (InstallWin*)arg;
    if (evt->type != XEVT_MOUSE || evt->state != MOUSE_STATE_CLICK)
        return;
    if (g_inst == NULL || win == NULL || g_inst->state != ST_READY ||
            g_inst->runner != NULL)
        return;
    win->beginClone(CLONE_MODE_CHECK, 0);
}

static void on_exit(Widget* wd, xevent_t* evt, void* arg) {
    (void)wd;
    InstallWin* win = (InstallWin*)arg;
    if (evt->type != XEVT_MOUSE || evt->state != MOUSE_STATE_CLICK)
        return;
    if (win != NULL)
        win->close();
}

/* ------------------------------------------------------------------ */

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    X x;
    InstallWin win;

    Installer* inst = new Installer();
    g_inst = inst;

    RootWidget* root = new RootWidget();
    win.setRoot(root);
    root->setType(Container::VERTICAL);
    root->setAlpha(false);

    win.infoPanel = new InfoPanel();
    win.infoPanel->fix(0, 44);
    root->add(win.infoPanel);

    win.partList = new PartList();
    root->add(win.partList);
    win.partList->setItemSize(22);

    win.progressPanel = new ProgressPanel();
    win.progressPanel->fix(0, 44);
    root->add(win.progressPanel);

    g_log = new LogPanel();
    root->add(g_log);

    Splitter* split = new Splitter();
    split->attach(g_log);
    root->add(split);

    Container* btns = new Container();
    btns->setType(Container::HORIZONTAL);
    btns->fix(0, 34);
    root->add(btns);

    win.scanBtn = new LabelButton("Scan");
    win.scanBtn->setEventFunc(on_scan, &win);
    btns->add(win.scanBtn);

    split = new Splitter();
    split->attach(win.scanBtn);
    btns->add(split);

    win.installBtn = new LabelButton("Install");
    win.installBtn->setEventFunc(on_install, &win);
    btns->add(win.installBtn);

    split = new Splitter();
    split->attach(win.installBtn);
    btns->add(split);

    win.verifyBtn = new LabelButton("Verify");
    win.verifyBtn->setEventFunc(on_verify, &win);
    btns->add(win.verifyBtn);

    split = new Splitter();
    split->attach(win.verifyBtn);
    btns->add(split);

    win.exitBtn = new LabelButton("Exit");
    win.exitBtn->setEventFunc(on_exit, &win);
    btns->add(win.exitBtn);

    inst_log("xinstall: clone the boot media onto another medium.",
            LOG_INFO_COLOR);

    win.open(&x, 0, -1, -1, 640, 460, "xinstall", XWIN_STYLE_NORMAL);
    win.setTimer(30);
    widgetXRun(&x, &win);

    g_log = NULL;
    g_inst = NULL;
    if (inst->runner != NULL)
        clone_finish(inst->runner);
    if (inst->dst_ok)
        inst->dst.shutdown(&inst->dst);
    if (inst->src_ok)
        inst->src.shutdown(&inst->src);
    delete inst;
    return 0;
}
