// Blender for EwokOS — app bootstrap.
//
// Structure mirrors apps/imgui_demo: xwin window + EGL/GLES2 context +
// Dear ImGui for the Blender-style chrome + a custom GLES2 renderer for the
// 3D viewport.  All Blender UI panels are drawn as fixed-position ImGui
// windows; the 3D viewport occupies the rectangle computed by Ui::compute_layout.
//
// Input mapping (Blender conventions):
//   Middle-mouse drag  — orbit (shift+middle drag — pan)
//   Wheel              — dolly / zoom (over the viewport) or scroll (over UI)
//   Left click         — select object under cursor (ray-sphere pick)
//   G / R / S          — grab(move) / rotate / scale modal transform
//   X / Y / Z (modal)  — constrain the transform to that world axis
//   LMB (modal)        — confirm;  ESC / RMB (modal) — cancel
//   A                  — select all;   Shift+A — select none
//   Ctrl+Z / Ctrl+Shift+Z — undo / redo
//   X                  — delete selected;   Shift+D — duplicate selected
//   Numpad 1/3/7/5/0   — front / right / top / user / (camera) views
//   .  /  Home         — frame selected
//   ESC                — quit (when no modal / popup is active)
//
// EwokOS delivers pointer movement as MOUSE_STATE_MOVE with button==NONE; the
// button identity only appears on the DOWN/UP edge events.  We therefore LATCH
// each button's held state across events instead of trusting the per-event
// button field, which is what makes orbit/pan and ImGui widget dragging work.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include <ewoksys/proc.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/klog.h>
#include <x/x.h>
#include <x/xwin.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_gles2.h"

#include "scene.h"
#include "theme.h"
#include "ui.h"
#include "viewport.h"

using namespace blender;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// Window / EGL state
// ---------------------------------------------------------------------------

static int win_width  = 1280;
static int win_height = 720;

static EGLDisplay egl_dpy  = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx  = EGL_NO_CONTEXT;

static Scene*    g_scene    = NULL;
static Viewport* g_viewport = NULL;
static Ui*       g_ui       = NULL;
static xwin_t*   g_xwin     = NULL;   // tracked so loop() can close on quit

// Latched pointer state (window-relative coords).
static int  mouse_x = 0, mouse_y = 0;
static bool left_held = false, mid_held = false, right_held = false;
static int  last_mouse_x = 0, last_mouse_y = 0;
static bool nav_active   = false;   // middle-drag navigation in progress
static bool nav_pan      = false;   // shift held at nav start -> pan instead of orbit
static int  mouse_wheel_accum = 0;

// Modifier state (EwokOS key events carry shift/ctrl; alt is not reported).
static bool key_ctrl = false, key_shift = false, key_alt = false;

// Frame timing
static uint32_t last_frame_tic = 0;

// ---------------------------------------------------------------------------
// Modal transform state (G/R/S)
// ---------------------------------------------------------------------------

static bool        transform_active = false;
static ToolRequest transform_mode   = ToolRequest::None;
static Object*     transform_obj    = NULL;
static Vec3        t_orig_pos, t_orig_rot, t_orig_scale;
static int         t_grab_x = 0, t_grab_y = 0;   // viewport-local grab point
static int         t_axis   = 0;                 // 0 none, 1 X, 2 Y, 3 Z

static const float kFovDeg = 39.598f;            // must match Viewport::fov_deg_

// ---------------------------------------------------------------------------
// Small vector helpers
// ---------------------------------------------------------------------------

static inline float vdot(const Vec3& a, const Vec3& b)
{
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

static inline Vec3 vnorm(const Vec3& a)
{
    float l = sqrtf(vdot(a, a));
    Vec3 r = { 0, 0, 0 };
    if (l > 1e-8f) { r.x = a.x/l; r.y = a.y/l; r.z = a.z/l; }
    return r;
}

static inline Vec3 vcross(const Vec3& a, const Vec3& b)
{
    Vec3 r;
    r.x = a.y*b.z - a.z*b.y;
    r.y = a.z*b.x - a.x*b.z;
    r.z = a.x*b.y - a.y*b.x;
    return r;
}

// Camera basis matching Viewport::pan(): fwd points from target toward eye,
// right/up span the view plane.  wpp = world units per screen pixel at the
// target depth (identical for the perspective and ortho projections because
// Blender derives the ortho half-height from the same distance*tan(fov/2)).
struct CamBasis { Vec3 fwd, right, up; float wpp, ppw; };

static CamBasis get_cam_basis()
{
    CamBasis b;
    Vec3 target = g_viewport->target();
    float dist  = g_viewport->distance();
    float az = g_viewport->azimuth_deg()   * (float)M_PI / 180.0f;
    float el = g_viewport->elevation_deg() * (float)M_PI / 180.0f;
    (void)target;
    b.fwd.x = cosf(el) * cosf(az);
    b.fwd.y = cosf(el) * sinf(az);
    b.fwd.z = sinf(el);
    Vec3 world_up = { 0, 0, 1 };
    b.right = vnorm(vcross(b.fwd, world_up));
    b.up    = vcross(b.right, b.fwd);
    int h = g_viewport->height() > 0 ? g_viewport->height() : 1;
    b.wpp = 2.0f * dist * tanf(kFovDeg * 0.5f * (float)M_PI / 180.0f) / (float)h;
    b.ppw = 1.0f / b.wpp;
    return b;
}

static inline Vec3 axis_vec(int a)
{
    Vec3 v = { 0, 0, 0 };
    if (a == 1) v.x = 1.0f;
    else if (a == 2) v.y = 1.0f;
    else if (a == 3) v.z = 1.0f;
    return v;
}

// ---------------------------------------------------------------------------
// ImGui key mapping (subset matching imgui_demo)
// ---------------------------------------------------------------------------

static ImGuiKey keycode_to_imgui(int keycode)
{
    switch (keycode) {
        case 9:   return ImGuiKey_Tab;
        case 27:  return ImGuiKey_Escape;
        case 13:  return ImGuiKey_Enter;
        case 8:   return ImGuiKey_Backspace;
        case 127: return ImGuiKey_Delete;
        case 32:  return ImGuiKey_Space;
        case 276: return ImGuiKey_LeftArrow;
        case 275: return ImGuiKey_RightArrow;
        case 273: return ImGuiKey_UpArrow;
        case 274: return ImGuiKey_DownArrow;
        case 278: return ImGuiKey_Home;
        case 279: return ImGuiKey_End;
        case 280: return ImGuiKey_PageUp;
        case 281: return ImGuiKey_PageDown;
        case 277: return ImGuiKey_Insert;
        case '0': return ImGuiKey_0; case '1': return ImGuiKey_1;
        case '2': return ImGuiKey_2; case '3': return ImGuiKey_3;
        case '4': return ImGuiKey_4; case '5': return ImGuiKey_5;
        case '6': return ImGuiKey_6; case '7': return ImGuiKey_7;
        case '8': return ImGuiKey_8; case '9': return ImGuiKey_9;
        case 'a': case 'A': return ImGuiKey_A;
        case 'b': case 'B': return ImGuiKey_B;
        case 'c': case 'C': return ImGuiKey_C;
        case 'd': case 'D': return ImGuiKey_D;
        case 'e': case 'E': return ImGuiKey_E;
        case 'f': case 'F': return ImGuiKey_F;
        case 'g': case 'G': return ImGuiKey_G;
        case 'h': case 'H': return ImGuiKey_H;
        case 'i': case 'I': return ImGuiKey_I;
        case 'j': case 'J': return ImGuiKey_J;
        case 'k': case 'K': return ImGuiKey_K;
        case 'l': case 'L': return ImGuiKey_L;
        case 'm': case 'M': return ImGuiKey_M;
        case 'n': case 'N': return ImGuiKey_N;
        case 'o': case 'O': return ImGuiKey_O;
        case 'p': case 'P': return ImGuiKey_P;
        case 'q': case 'Q': return ImGuiKey_Q;
        case 'r': case 'R': return ImGuiKey_R;
        case 's': case 'S': return ImGuiKey_S;
        case 't': case 'T': return ImGuiKey_T;
        case 'u': case 'U': return ImGuiKey_U;
        case 'v': case 'V': return ImGuiKey_V;
        case 'w': case 'W': return ImGuiKey_W;
        case 'x': case 'X': return ImGuiKey_X;
        case 'y': case 'Y': return ImGuiKey_Y;
        case 'z': case 'Z': return ImGuiKey_Z;
        case 282: return ImGuiKey_F1; case 283: return ImGuiKey_F2;
        case 284: return ImGuiKey_F3; case 285: return ImGuiKey_F4;
        case 286: return ImGuiKey_F5; case 287: return ImGuiKey_F6;
        case 288: return ImGuiKey_F7; case 289: return ImGuiKey_F8;
        case 290: return ImGuiKey_F9; case 291: return ImGuiKey_F10;
        case 292: return ImGuiKey_F11; case 293: return ImGuiKey_F12;
        case ',': return ImGuiKey_Comma;
        case '.': return ImGuiKey_Period;
        case '/': return ImGuiKey_Slash;
        case ';': return ImGuiKey_Semicolon;
        case '\'':return ImGuiKey_Apostrophe;
        case '[': return ImGuiKey_LeftBracket;
        case ']': return ImGuiKey_RightBracket;
        case '-': return ImGuiKey_Minus;
        case '=': return ImGuiKey_Equal;
        case '`': return ImGuiKey_GraveAccent;
        default:  return ImGuiKey_None;
    }
}

// ---------------------------------------------------------------------------
// Modal transform implementation
// ---------------------------------------------------------------------------

static void transform_update(int mx, int my);

static void transform_begin(ToolRequest mode)
{
    if (!g_scene || !g_ui) return;
    Object* sel = g_scene->selected();
    if (!sel) { g_ui->set_status_public("No selection to transform"); return; }

    transform_active = true;
    transform_mode   = mode;
    transform_obj    = sel;
    t_orig_pos   = sel->pos;
    t_orig_rot   = sel->rot_euler_deg;
    t_orig_scale = sel->scale;
    t_axis = 0;

    Ui::Rect vp = g_ui->viewport_rect();
    t_grab_x = mouse_x - vp.x;
    t_grab_y = mouse_y - vp.y;

    transform_update(mouse_x, mouse_y);
}

static void transform_update(int mx, int my)
{
    if (!transform_obj || !g_ui) return;

    Ui::Rect vp = g_ui->viewport_rect();
    int lx = mx - vp.x;
    int ly = my - vp.y;
    int dx = lx - t_grab_x;
    int dy = ly - t_grab_y;

    CamBasis b = get_cam_basis();
    char head[64];
    head[0] = '\0';

    if (transform_mode == ToolRequest::Move) {
        Vec3 np = t_orig_pos;
        if (t_axis == 0) {
            float wx = dx * b.wpp;
            float wy = -dy * b.wpp;
            np.x += b.right.x * wx + b.up.x * wy;
            np.y += b.right.y * wx + b.up.y * wy;
            np.z += b.right.z * wx + b.up.z * wy;
        } else {
            Vec3 a = axis_vec(t_axis);
            float sdx =  vdot(a, b.right) * b.ppw;   // screen px per world unit
            float sdy = -vdot(a, b.up)    * b.ppw;   // (y is down on screen)
            float denom = sdx*sdx + sdy*sdy;
            float t = (denom > 1e-6f)
                      ? ((float)dx * sdx + (float)dy * sdy) / denom
                      : 0.0f;
            np.x += a.x * t; np.y += a.y * t; np.z += a.z * t;
        }
        transform_obj->pos = np;
        snprintf(head, sizeof(head), "Move  %.2f %.2f %.2f", np.x, np.y, np.z);
    } else if (transform_mode == ToolRequest::Rotate) {
        float ddeg = -(float)dx * 0.5f;
        Vec3 nr = t_orig_rot;
        if (t_axis == 1)      nr.x = t_orig_rot.x + ddeg;
        else if (t_axis == 2) nr.y = t_orig_rot.y + ddeg;
        else                  nr.z = t_orig_rot.z + ddeg;
        transform_obj->rot_euler_deg = nr;
        snprintf(head, sizeof(head), "Rotate  %.1f deg", ddeg);
    } else { // Scale
        float f = 1.0f + (float)dx * 0.01f;
        if (f < 0.01f) f = 0.01f;
        Vec3 ns = t_orig_scale;
        if (t_axis == 0)      { ns.x = t_orig_scale.x*f; ns.y = t_orig_scale.y*f; ns.z = t_orig_scale.z*f; }
        else if (t_axis == 1)   ns.x = t_orig_scale.x*f;
        else if (t_axis == 2)   ns.y = t_orig_scale.y*f;
        else                    ns.z = t_orig_scale.z*f;
        transform_obj->scale = ns;
        snprintf(head, sizeof(head), "Scale  %.2f", f);
    }

    const char* axis = (t_axis == 1) ? "  [X]" : (t_axis == 2) ? "  [Y]"
                       : (t_axis == 3) ? "  [Z]" : "";
    char banner[96];
    snprintf(banner, sizeof(banner), "%s%s   LMB ok / ESC cancel", head, axis);
    g_ui->set_transform_banner(banner);
}

static void transform_confirm()
{
    if (transform_obj && g_scene) g_scene->commit();
    transform_active = false;
    transform_obj    = NULL;
    transform_mode   = ToolRequest::None;
    if (g_ui) { g_ui->set_transform_banner(""); g_ui->set_status_public("Transform applied"); }
}

static void transform_cancel()
{
    if (transform_obj) {
        transform_obj->pos           = t_orig_pos;
        transform_obj->rot_euler_deg = t_orig_rot;
        transform_obj->scale         = t_orig_scale;
    }
    transform_active = false;
    transform_obj    = NULL;
    transform_mode   = ToolRequest::None;
    if (g_ui) { g_ui->set_transform_banner(""); g_ui->set_status_public("Transform cancelled"); }
}

// ---------------------------------------------------------------------------
// Blender-style keyboard shortcuts (handled outside ImGui when no widget
// has focus — ImGui eats keys while a text field is active).
// ---------------------------------------------------------------------------

static void handle_blender_key(int keycode, bool pressed)
{
    if (!pressed) return;

    // A modal transform consumes navigation keys.
    if (transform_active) {
        switch (keycode) {
            case 27: transform_cancel(); break;                  // ESC
            case 'x': case 'X': t_axis = 1; transform_update(mouse_x, mouse_y); break;
            case 'y': case 'Y': t_axis = 2; transform_update(mouse_x, mouse_y); break;
            case 'z': case 'Z': t_axis = 3; transform_update(mouse_x, mouse_y); break;
            default: break;                                       // ignore the rest
        }
        return;
    }

    switch (keycode) {
        case 27: // ESC -> quit (popups, if any, already consumed the key)
            if (g_ui) g_ui->action_quit_requested();
            break;
        case '.':
        case 278: // Home
            if (g_viewport && g_scene) g_viewport->frame_selected(*g_scene);
            break;
        case 'g': case 'G': transform_begin(ToolRequest::Move);   break;
        case 'r': case 'R': transform_begin(ToolRequest::Rotate); break;
        case 's': case 'S': if (!key_ctrl) transform_begin(ToolRequest::Scale); break;
        case 'x': case 'X':
            if (!key_ctrl && g_ui) g_ui->action_delete_selected_public();
            break;
        case 'd': case 'D':
            if (key_shift && g_ui) g_ui->action_duplicate_selected_public();
            break;
        case 'a': case 'A':
            if (g_ui) {
                if (key_shift) g_ui->action_select_none_public();
                else           g_ui->action_select_all_public();
            }
            break;
        case 'z': case 'Z':
            if (key_ctrl && g_ui) {
                if (key_shift) g_ui->action_redo_public();
                else           g_ui->action_undo_public();
            }
            break;
        // Numpad-style view presets.  EwokOS sends digits as ASCII; treat
        // Ctrl+digit as the "opposite" view like Blender does.
        case '1':
            if (g_viewport)
                g_viewport->set_view_preset(key_ctrl ? Viewport::ViewPreset::Back
                                                     : Viewport::ViewPreset::Front);
            break;
        case '3':
            if (g_viewport)
                g_viewport->set_view_preset(key_ctrl ? Viewport::ViewPreset::Left
                                                     : Viewport::ViewPreset::Right);
            break;
        case '7':
            if (g_viewport)
                g_viewport->set_view_preset(key_ctrl ? Viewport::ViewPreset::Bottom
                                                     : Viewport::ViewPreset::Top);
            break;
        case '5':
            if (g_viewport) g_viewport->set_view_preset(Viewport::ViewPreset::User);
            break;
        case '0':
            if (g_viewport) g_viewport->set_view_preset(Viewport::ViewPreset::Camera);
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Ray-sphere pick for left-click selection.
// ---------------------------------------------------------------------------

static Object* pick_object(int win_x, int win_y)
{
    if (!g_scene || !g_viewport) return NULL;

    Vec3 target = g_viewport->target();
    float dist  = g_viewport->distance();
    float az    = g_viewport->azimuth_deg()   * (float)M_PI / 180.0f;
    float el    = g_viewport->elevation_deg() * (float)M_PI / 180.0f;
    Vec3 eye;
    eye.x = target.x + dist * cosf(el) * cosf(az);
    eye.y = target.y + dist * cosf(el) * sinf(az);
    eye.z = target.z + dist * sinf(el);

    int vp_x = g_viewport->width()  > 0 ? g_viewport->width()  : 1;
    int vp_y = g_viewport->height() > 0 ? g_viewport->height() : 1;
    float ndc_x = (2.0f * (float)win_x) / (float)vp_x - 1.0f;
    float ndc_y = 1.0f - (2.0f * (float)win_y) / (float)vp_y;

    float fov = kFovDeg * (float)M_PI / 180.0f;
    float aspect = (float)vp_x / (float)vp_y;
    float tan_h = tanf(fov * 0.5f);
    float tan_w = tan_h * aspect;

    Vec3 fwd = { target.x - eye.x, target.y - eye.y, target.z - eye.z };
    fwd = vnorm(fwd);
    Vec3 right = vnorm(Vec3{ fwd.y, -fwd.x, 0.0f });
    Vec3 up = vcross(right, fwd);

    Vec3 dir;
    dir.x = fwd.x + right.x * ndc_x * tan_w + up.x * ndc_y * tan_h;
    dir.y = fwd.y + right.y * ndc_x * tan_w + up.y * ndc_y * tan_h;
    dir.z = fwd.z                             + up.z * ndc_y * tan_h;
    dir = vnorm(dir);

    Object* best = NULL;
    float best_t = 1e30f;
    for (size_t i = 0; i < g_scene->object_count(); ++i) {
        Object* o = g_scene->object_at(i);
        if (!o || !o->visible || o->hide_viewport) continue;
        float radius = 1.0f;
        if (o->mesh.nverts > 0 && o->mesh.verts) {
            float mn[3] = { 1e30f, 1e30f, 1e30f };
            float mx[3] = {-1e30f,-1e30f,-1e30f };
            for (unsigned int v = 0; v < o->mesh.nverts; ++v) {
                const float* p = o->mesh.verts + v * 6;
                for (int k = 0; k < 3; ++k) {
                    float s = (k==0) ? o->scale.x : (k==1) ? o->scale.y : o->scale.z;
                    float val = p[k] * s;
                    if (val < mn[k]) mn[k] = val;
                    if (val > mx[k]) mx[k] = val;
                }
            }
            float ex = mx[0]-mn[0], ey = mx[1]-mn[1], ez = mx[2]-mn[2];
            radius = 0.5f * sqrtf(ex*ex + ey*ey + ez*ez);
            if (radius < 0.05f) radius = 0.05f;
        }
        Vec3 c = { o->pos.x - eye.x, o->pos.y - eye.y, o->pos.z - eye.z };
        float tca = vdot(c, dir);
        if (tca < 0) continue;
        float d2 = vdot(c, c) - tca*tca;
        float r2 = radius * radius;
        if (d2 > r2) continue;
        float thc = sqrtf(r2 - d2);
        float t0 = tca - thc;
        if (t0 < best_t) { best_t = t0; best = o; }
    }
    return best;
}

// True when a window-relative point lands inside the 3D viewport rect.
static bool inside_viewport(int x, int y)
{
    if (!g_ui) return false;
    Ui::Rect r = g_ui->viewport_rect();
    return (x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h);
}

// ---------------------------------------------------------------------------
// Event handler
// ---------------------------------------------------------------------------

static void on_event(xwin_t* xwin, xevent_t* ev)
{
    ImGuiIO& io = ImGui::GetIO();

    if (ev->type == XEVT_IM) {
        int key = ev->value.im.value;
        uint32_t shift = ev->value.im.shift;
        uint32_t ctrl  = ev->value.im.ctrl;
        int32_t state  = ev->state;

        key_shift = (shift != 0);
        key_ctrl  = (ctrl  != 0);

        bool pressed = (state == XIM_STATE_PRESS);

        ImGuiKey ik = keycode_to_imgui(key);
        if (ik != ImGuiKey_None) io.AddKeyEvent(ik, pressed);
        if (pressed && key >= 32 && key < 127) io.AddInputCharacter(key);

        // Blender-level shortcuts, dispatched after ImGui so focused widgets
        // still receive the key and popups close on ESC first.
        if (!io.WantCaptureKeyboard)
            handle_blender_key(key, pressed);
    }
    else if (ev->type == XEVT_MOUSE) {
        gpos_t pos = xwin_get_inside_pos(xwin, ev->value.mouse.x, ev->value.mouse.y);
        mouse_x = pos.x;
        mouse_y = pos.y;

        int32_t state  = ev->state;
        int32_t button = ev->value.mouse.button;

        // Latch held-state from the DOWN/UP edges; MOVE events carry NONE.
        bool left_edge = false, mid_edge = false, right_edge = false;
        bool left_up = false, mid_up = false, right_up = false;
        if (state == MOUSE_STATE_DOWN) {
            if      (button == MOUSE_BUTTON_LEFT)  { left_held  = true;  left_edge  = true; }
            else if (button == MOUSE_BUTTON_MID)   { mid_held   = true;  mid_edge   = true; }
            else if (button == MOUSE_BUTTON_RIGHT) { right_held = true;  right_edge = true; }
        } else if (state == MOUSE_STATE_UP) {
            if      (button == MOUSE_BUTTON_LEFT)  { left_held  = false; left_up  = true; }
            else if (button == MOUSE_BUTTON_MID)   { mid_held   = false; mid_up   = true; }
            else if (button == MOUSE_BUTTON_RIGHT) { right_held = false; right_up = true; }
        }
        // Publish to ImGui (persistent held-state => widget dragging works).
        io.MouseDown[0] = left_held;
        io.MouseDown[1] = right_held;
        io.MouseDown[2] = mid_held;

        bool in_vp = inside_viewport(mouse_x, mouse_y);

        if (transform_active) {
            // Modal transform: LMB confirms, RMB cancels, motion drags.
            if      (left_edge)  transform_confirm();
            else if (right_edge) transform_cancel();
            else                 transform_update(mouse_x, mouse_y);
        } else {
            // Middle-drag orbit / pan, only when the gesture starts in the view.
            if (mid_edge && in_vp) {
                nav_active   = true;
                nav_pan      = key_shift;
                last_mouse_x = mouse_x;
                last_mouse_y = mouse_y;
                if (g_viewport) g_viewport->begin_navigate();
            }
            if (nav_active && mid_held && g_viewport) {
                int dx = mouse_x - last_mouse_x;
                int dy = mouse_y - last_mouse_y;
                if (nav_pan) g_viewport->pan((float)dx, (float)dy);
                else         g_viewport->orbit((float)dx, (float)dy);
                last_mouse_x = mouse_x;
                last_mouse_y = mouse_y;
            }
            if (mid_up && nav_active) {
                nav_active = false;
                if (g_viewport) g_viewport->end_navigate();
            }

            // Left-click selection (press edge, inside viewport, not over UI).
            if (left_edge && in_vp && !io.WantCaptureMouse && g_scene && g_ui) {
                Ui::Rect vp = g_ui->viewport_rect();
                Object* hit = pick_object(mouse_x - vp.x, mouse_y - vp.y);
                g_scene->select(hit);
                if (hit) {
                    char msg[80];
                    snprintf(msg, sizeof(msg), "Selected: %s", hit->name);
                    g_ui->set_status_public(msg);
                } else {
                    g_ui->set_status_public("Deselect all");
                }
            }
        }

        // Wheel zoom (Blender: wheel up = dolly in).
        if (button == MOUSE_BUTTON_SCROLL_UP   && state == MOUSE_STATE_DOWN)
            mouse_wheel_accum++;
        else if (button == MOUSE_BUTTON_SCROLL_DOWN && state == MOUSE_STATE_DOWN)
            mouse_wheel_accum--;

        (void)left_up; (void)right_up;
    }
    else if (ev->type == XEVT_WIN_CLOSE) {
        xwin_close(xwin);
    }
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

static void render_frame()
{
    // Pick up window resize from EGL.
    EGLint w = 0, h = 0;
    eglQuerySurface(egl_dpy, egl_surf, EGL_WIDTH, &w);
    eglQuerySurface(egl_dpy, egl_surf, EGL_HEIGHT, &h);
    if (w > 0 && h > 0 && (w != win_width || h != win_height)) {
        win_width  = w;
        win_height = h;
    }

    // Frame timing
    uint32_t now = 0;
    kernel_tic32(NULL, NULL, &now);
    if (last_frame_tic == 0) last_frame_tic = now;
    float dt = (float)(now - last_frame_tic) / 1000000.0f;
    last_frame_tic = now;
    if (dt > 0.1f) dt = 0.1f;

    // Layout is owned by the UI; the GL pass reads the resulting viewport rect
    // so the chrome and the scissor always agree (single source of truth).
    if (g_ui) g_ui->compute_layout(win_width, win_height);
    Ui::Rect vp_rect = g_ui ? g_ui->viewport_rect()
                            : Ui::Rect{ 0, 0, win_width, win_height };

    bool in_vp = inside_viewport(mouse_x, mouse_y);

    // Wheel over the viewport dollies the camera; over UI it scrolls widgets.
    if (mouse_wheel_accum != 0 && in_vp && g_viewport)
        g_viewport->zoom((float)mouse_wheel_accum);

    // --- Render the 3D viewport into its sub-rectangle --------------------
    glEnable(GL_SCISSOR_TEST);
    if (g_viewport) {
        // GLES scissor/viewport origin is bottom-left; our rect is top-left.
        // The viewport derives every GL rectangle (clear + corner gizmo) from
        // this origin, so the clear always covers the whole panel rect.
        g_viewport->resize(vp_rect.x, win_height - (vp_rect.y + vp_rect.h),
                           vp_rect.w, vp_rect.h);
        unsigned int overlays = g_ui ? g_ui->overlays() : OVERLAY_ALL;
        g_viewport->render(*g_scene, overlays);
    }
    glDisable(GL_SCISSOR_TEST);

    // --- ImGui chrome on top ---------------------------------------------
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)win_width, (float)win_height);
    io.MousePos = ImVec2((float)mouse_x, (float)mouse_y);
    io.MouseDown[0] = left_held;
    io.MouseDown[1] = right_held;
    io.MouseDown[2] = mid_held;
    io.KeyCtrl  = key_ctrl;
    io.KeyShift = key_shift;
    io.KeyAlt   = key_alt;
    io.KeySuper = false;
    // Hand the wheel to ImGui only when it was not consumed by the camera.
    io.MouseWheel = in_vp ? 0.0f : (float)mouse_wheel_accum;
    mouse_wheel_accum = 0;

    ImGui_ImplGLES2_NewFrame();
    ImGui::NewFrame();

    if (g_ui) g_ui->draw(dt);

    // A toolbar/menu click may have requested a transform tool this frame.
    if (g_ui) {
        ToolRequest tr = g_ui->take_tool_request();
        if (tr != ToolRequest::None && !transform_active)
            transform_begin(tr);
    }

    ImGui::Render();
    // Reset GL viewport to full window before imgui draws (it uses window
    // coordinates, not viewport-local).
    glViewport(0, 0, win_width, win_height);
    glScissor(0, 0, win_width, win_height);
    glDisable(GL_SCISSOR_TEST);
    ImGui_ImplGLES2_RenderDrawData(ImGui::GetDrawData());

    eglSwapBuffers(egl_dpy, egl_surf);
}

static void loop(void* /*p*/)
{
    render_frame();
    if (g_ui && g_ui->wants_quit() && g_xwin) {
        xwin_close(g_xwin);
    }
    usleep(1000);
}

// ---------------------------------------------------------------------------
// EGL / init
// ---------------------------------------------------------------------------

static int init_egl(xwin_t* xwin)
{
    EGLint major, minor, ncfg = 0;
    EGLint cfg_attr[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE
    };
    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLConfig cfg = NULL;

    egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_dpy == EGL_NO_DISPLAY) return 0;
    if (!eglInitialize(egl_dpy, &major, &minor)) return 0;
    if (!eglChooseConfig(egl_dpy, cfg_attr, &cfg, 1, &ncfg) || ncfg < 1) return 0;

    egl_ctx = eglCreateContext(egl_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (egl_ctx == EGL_NO_CONTEXT) return 0;

    egl_surf = eglCreateWindowSurface(egl_dpy, cfg, (EGLNativeWindowType)xwin, NULL);
    if (egl_surf == EGL_NO_SURFACE) return 0;

    if (!eglMakeCurrent(egl_dpy, egl_surf, egl_surf, egl_ctx)) return 0;
    return 1;
}

static void init_imgui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.DisplaySize = ImVec2((float)win_width, (float)win_height);

    // Blender Dark theme.
    ImGui::StyleColorsDark();
    theme::apply(ImGui::GetStyle());

    ImGui_ImplGLES2_Init();
    io.Fonts->AddFontFromFileTTF("/usr/system/fonts/system.ttf", 14.0f);
}

int main(int argc, char* argv[])
{
    (void)argc; (void)argv;

    x_t x;
    x_init(&x, NULL);
    x.on_loop = loop;

    // Blender's default window title is "Blender"; use the same.
    xwin_t* xwin = xwin_open(&x, -1, 24, 24, win_width, win_height,
                             "Blender", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("blender: failed to open window\n");
        return 1;
    }
    xwin->on_event = on_event;
    g_xwin = xwin;

    if (!init_egl(xwin)) {
        printf("blender: failed to initialize EGL\n");
        return 1;
    }

    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, win_width, win_height);

    g_scene    = new Scene();
    g_viewport = new Viewport();
    g_scene->load_startup();

    if (!g_viewport->init(win_width, win_height)) {
        printf("blender: failed to initialize viewport\n");
        return 1;
    }
    g_viewport->frame_selected(*g_scene);

    init_imgui();

    g_ui = new Ui(*g_scene, *g_viewport);
    g_ui->compute_layout(win_width, win_height);

    xwin_set_visible(xwin, true);
    x_run(&x, xwin);

    // Teardown
    if (g_ui)       { delete g_ui;       g_ui = NULL; }
    if (g_viewport) { g_viewport->shutdown(); delete g_viewport; g_viewport = NULL; }
    if (g_scene)    { delete g_scene;    g_scene = NULL; }

    ImGui_ImplGLES2_Shutdown();
    ImGui::DestroyContext();

    xwin_destroy(xwin);
    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl_dpy, egl_surf);
    eglDestroyContext(egl_dpy, egl_ctx);
    eglTerminate(egl_dpy);

    return 0;
}
