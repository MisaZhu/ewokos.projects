// Blender UI panel implementation.
#include "ui.h"
#include "theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui/imgui.h"

namespace blender {

namespace {

// Fixed panel metrics — Blender's own defaults scaled for a small display.
// These live in ONE place; main.cpp reads the resulting viewport_rect() rather
// than duplicating the constants, so the chrome and the GL scissor always agree.
const int kMenuBarHeight      = 26;
const int kWorkspaceTabHeight = 26;
const int kViewportHeaderH    = 30;
const int kToolbarWidth       = 86;
const int kRightPanelWidth    = 232;
const int kOutlinerHeight     = 180;
const int kTimelineHeight     = 92;
const int kStatusBarHeight    = 20;

// Blender workspace tab labels (Blender 3.x/4.x default).
const char* kWorkspaces[] = {
    "Layout", "Modeling", "Sculpting", "UV Editing", "Texture Paint",
    "Shading", "Animation", "Rendering", "Compositing", "Geometry Nodes",
    "Scripting",
};
const int kWorkspaceCount = (int)(sizeof(kWorkspaces) / sizeof(kWorkspaces[0]));

// Properties editor tab labels (left column of icons in Blender).
const char* kPropTabs[] = {
    "Render", "Output", "View Layer", "Scene", "World", "Object",
    "Modifiers", "Constraints", "Data", "Material",
};
const int kPropTabCount = (int)(sizeof(kPropTabs) / sizeof(kPropTabs[0]));

// Left-toolbar tools.  Short labels so they fit the narrow strip; the full
// Blender name is shown as a tooltip.
struct ToolEntry { const char* label; const char* tip; int prim; int tool; };
// prim >= 0 -> add primitive; tool: 0 none, 1 move, 2 rotate, 3 scale,
// 4 cursor, 5 info-only.
const ToolEntry kTools[] = {
    { "Select",  "Select Box (drag in viewport)", -1, 5 },
    { "Cursor",  "Place the 3D cursor",           -1, 4 },
    { "Measure", "Measure distance",              -1, 5 },
    { "+Cube",   "Add Mesh > Cube",                0, 0 },
    { "+Sphere", "Add Mesh > UV Sphere",           1, 0 },
    { "+Ico",    "Add Mesh > Ico Sphere",          2, 0 },
    { "+Cyl",    "Add Mesh > Cylinder",            3, 0 },
    { "+Plane",  "Add Mesh > Plane",               4, 0 },
    { "Move",    "Move (G)",                      -1, 1 },
    { "Rotate",  "Rotate (R)",                    -1, 2 },
    { "Scale",   "Scale (S)",                     -1, 3 },
    { "Xform",   "Transform panel",               -1, 5 },
    { "Draw",    "Annotate",                      -1, 5 },
};
const int kToolCount = (int)(sizeof(kTools) / sizeof(kTools[0]));

} // namespace

Ui::Ui(Scene& scene, Viewport& viewport)
    : scene_(scene), viewport_(viewport),
      win_w_(640), win_h_(480),
      wants_quit_(false), workspace_index_(0), properties_tab_(5),
      snapping_enabled_(false),
      fps_display_(0.0f), frame_count_accum_(0), fps_accum_timer_(0.0f),
      overlays_(OVERLAY_ALL), tool_request_(ToolRequest::None),
      info_open_(false), banner_active_(false),
      playing_(false), play_accum_(0.0f)
{
    status_message_[0] = '\0';
    info_text_[0] = '\0';
    banner_text_[0] = '\0';
    Rect z = { 0, 0, 0, 0 };
    r_menu_ = r_tabs_ = r_vpheader_ = r_toolbar_ = z;
    r_outliner_ = r_props_ = r_timeline_ = r_status_ = z;
    vp_rect_ = { 0, 0, 320, 240 };
}

void Ui::set_status(const char* msg)
{
    snprintf(status_message_, sizeof(status_message_), "%s", msg ? msg : "");
}

void Ui::set_transform_banner(const char* text)
{
    if (text && text[0]) {
        banner_active_ = true;
        snprintf(banner_text_, sizeof(banner_text_), "%s", text);
    } else {
        banner_active_ = false;
        banner_text_[0] = '\0';
    }
}

// ---------------------------------------------------------------------------
// Layout — single source of truth for every panel rect.
// ---------------------------------------------------------------------------

void Ui::compute_layout(int win_w, int win_h)
{
    win_w_ = win_w > 0 ? win_w : 1;
    win_h_ = win_h > 0 ? win_h : 1;

    int y0 = kMenuBarHeight + kWorkspaceTabHeight;
    int status_y   = win_h_ - kStatusBarHeight;
    int timeline_y = status_y - kTimelineHeight;
    if (timeline_y < y0) timeline_y = y0;
    int content_h = timeline_y - y0;
    if (content_h < 1) content_h = 1;

    r_menu_     = { 0, 0, win_w_, kMenuBarHeight };
    r_tabs_     = { 0, kMenuBarHeight, win_w_, kWorkspaceTabHeight };
    r_toolbar_  = { 0, y0, kToolbarWidth, content_h };

    int vp_x = kToolbarWidth;
    int vp_w = win_w_ - kToolbarWidth - kRightPanelWidth;
    if (vp_w < 1) vp_w = 1;
    r_vpheader_ = { vp_x, y0, vp_w, kViewportHeaderH };
    vp_rect_    = { vp_x, y0 + kViewportHeaderH, vp_w, content_h - kViewportHeaderH };
    if (vp_rect_.h < 1) vp_rect_.h = 1;

    int right_x = win_w_ - kRightPanelWidth;
    if (right_x < vp_x) right_x = vp_x;
    r_outliner_ = { right_x, y0, kRightPanelWidth, kOutlinerHeight };
    int props_h = content_h - kOutlinerHeight;
    if (props_h < 1) props_h = 1;
    r_props_    = { right_x, y0 + kOutlinerHeight, kRightPanelWidth, props_h };

    r_timeline_ = { 0, timeline_y, win_w_, kTimelineHeight };
    r_status_   = { 0, status_y, win_w_, kStatusBarHeight };
}

void Ui::draw(float dt_seconds)
{
    frame_count_accum_++;
    fps_accum_timer_ += dt_seconds;
    if (fps_accum_timer_ >= 1.0f) {
        fps_display_ = (float)frame_count_accum_ / fps_accum_timer_;
        frame_count_accum_ = 0;
        fps_accum_timer_ = 0.0f;
    }

    draw_menu_bar();
    draw_workspace_tabs();
    draw_viewport_header();
    draw_toolbar();
    draw_outliner();
    draw_properties();
    draw_timeline();
    draw_status_bar();
    draw_modals();
}

// ---------------------------------------------------------------------------
// Chrome window helper: fixed position, tight padding, no move/resize.
// ---------------------------------------------------------------------------

void Ui::begin_chrome(const char* id, const Rect& r, bool menu_bar)
{
    ImGui::SetNextWindowPos(ImVec2((float)r.x, (float)r.y));
    ImGui::SetNextWindowSize(ImVec2((float)r.w, (float)r.h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 3));
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (menu_bar) flags |= ImGuiWindowFlags_MenuBar;
    ImGui::Begin(id, NULL, flags);
}

void Ui::end_chrome()
{
    ImGui::End();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------

void Ui::draw_menu_bar()
{
    begin_chrome("##menubar", r_menu_, true);
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New", "Ctrl+N"))            { action_new_scene(); }
            if (ImGui::MenuItem("Open...", "Ctrl+O"))        { action_show_info("Open .blend: not available in this lightweight port"); }
            if (ImGui::BeginMenu("Open Recent")) {
                if (ImGui::MenuItem("startup.blend"))        { action_new_scene(); }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Save", "Ctrl+S"))           { action_show_info("Save .blend: not available in this lightweight port"); }
            if (ImGui::MenuItem("Save As..."))               { action_show_info("Save As: not available in this lightweight port"); }
            if (ImGui::MenuItem("Revert"))                   { action_new_scene(); set_status("Reverted to startup"); }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Ctrl+Q"))           { wants_quit_ = true; }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, scene_.can_undo()))       { action_undo(); }
            if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, scene_.can_redo())) { action_redo(); }
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate", "Shift+D"))  { action_duplicate_selected(); }
            if (ImGui::MenuItem("Delete", "X"))           { action_delete_selected(); }
            ImGui::Separator();
            if (ImGui::MenuItem("Select All", "A"))       { action_select_all(); }
            if (ImGui::MenuItem("Select None", "Alt+A"))  { action_select_none(); }
            ImGui::Separator();
            if (ImGui::MenuItem("Frame Selected", "."))   { action_frame_selected(); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Add")) {
            if (ImGui::MenuItem("Cube"))       { action_add_primitive(0); }
            if (ImGui::MenuItem("UV Sphere"))  { action_add_primitive(1); }
            if (ImGui::MenuItem("Ico Sphere")) { action_add_primitive(2); }
            if (ImGui::MenuItem("Cylinder"))   { action_add_primitive(3); }
            if (ImGui::MenuItem("Plane"))      { action_add_primitive(4); }
            ImGui::Separator();
            if (ImGui::BeginMenu("Camera / Light")) {
                if (ImGui::MenuItem("Add Camera")) {
                    Vec3 p = scene_.cursor3d();
                    Vec3 r = { 66.93f, 0.0f, 46.692f };
                    p.x += 7.36f; p.y -= 6.93f; p.z += 4.96f;
                    Object* o = scene_.add_camera("Camera", p, r);
                    if (o) { scene_.select(o); scene_.commit(); set_status("Added Camera"); }
                }
                if (ImGui::MenuItem("Add Point Light")) {
                    Vec3 p = scene_.cursor3d();
                    p.x += 4.08f; p.y += 1.01f; p.z += 5.90f;
                    Object* o = scene_.add_light("Light", LightType::Point, p, 1000.0f);
                    if (o) { scene_.select(o); scene_.commit(); set_status("Added Light"); }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Front",  "Num1"))      { action_set_view(Viewport::ViewPreset::Front); }
            if (ImGui::MenuItem("Back",   "Ctrl+Num1")) { action_set_view(Viewport::ViewPreset::Back); }
            if (ImGui::MenuItem("Right",  "Num3"))      { action_set_view(Viewport::ViewPreset::Right); }
            if (ImGui::MenuItem("Left",   "Ctrl+Num3")) { action_set_view(Viewport::ViewPreset::Left); }
            if (ImGui::MenuItem("Top",    "Num7"))      { action_set_view(Viewport::ViewPreset::Top); }
            if (ImGui::MenuItem("Bottom", "Ctrl+Num7")) { action_set_view(Viewport::ViewPreset::Bottom); }
            ImGui::Separator();
            if (ImGui::MenuItem("User Perspective", "Num5")) { action_set_view(Viewport::ViewPreset::User); }
            if (ImGui::MenuItem("Frame Selected", "."))      { action_frame_selected(); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Render")) {
            if (ImGui::MenuItem("Render Image", "F12"))
                action_show_info("Cycles / EEVEE render is not available;\nthe viewport uses Workbench-style solid shading.");
            if (ImGui::MenuItem("Render Animation", "Ctrl+F12"))
                action_show_info("Animation rendering is not available in this port.");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window")) {
            if (ImGui::MenuItem("Toggle Fullscreen", "Alt+Enter"))
                action_show_info("Fullscreen toggle is handled by the window manager.");
            ImGui::Separator();
            for (int i = 0; i < kWorkspaceCount; ++i) {
                if (ImGui::MenuItem(kWorkspaces[i], NULL, workspace_index_ == i))
                    action_set_workspace(i);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Blender Manual"))
                action_show_info("Blender Manual: see blender.org/manual");
            if (ImGui::MenuItem("About Blender"))
                action_show_info("Blender for EwokOS\nLightweight port (Layout workspace)\nGLES2 + Dear ImGui");
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    end_chrome();
}

// ---------------------------------------------------------------------------
// Workspace tabs
// ---------------------------------------------------------------------------

void Ui::draw_workspace_tabs()
{
    begin_chrome("##workspaces", r_tabs_, false);
    for (int i = 0; i < kWorkspaceCount; ++i) {
        if (i > 0) ImGui::SameLine(0, 2);
        bool selected = (workspace_index_ == i);
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(kWorkspaces[i], ImVec2(0, (float)r_tabs_.h - 8.0f)))
            action_set_workspace(i);
        if (selected) ImGui::PopStyleColor();
    }
    end_chrome();
}

// ---------------------------------------------------------------------------
// 3D Viewport header strip
// ---------------------------------------------------------------------------

void Ui::draw_viewport_header()
{
    begin_chrome("##vp_header", r_vpheader_, false);

    // Mode selector
    if (ImGui::Button(Scene::mode_name(scene_.mode()), ImVec2(104, 0)))
        ImGui::OpenPopup("##mode_popup");
    if (ImGui::BeginPopup("##mode_popup")) {
        Scene::Mode modes[] = {
            Scene::Mode::Object, Scene::Mode::Edit, Scene::Mode::Sculpt,
            Scene::Mode::VertexPaint, Scene::Mode::WeightPaint,
            Scene::Mode::TexturePaint, Scene::Mode::Shading,
        };
        for (int i = 0; i < (int)(sizeof(modes)/sizeof(modes[0])); ++i) {
            bool sel = (scene_.mode() == modes[i]);
            if (ImGui::MenuItem(Scene::mode_name(modes[i]), NULL, sel))
                action_set_mode(modes[i]);
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    static const char* orients[] = { "Global", "Local", "Normal", "Gimbal", "View" };
    static int orient = 0;
    if (ImGui::Button(orients[orient], ImVec2(58, 0))) ImGui::OpenPopup("##orient_popup");
    if (ImGui::BeginPopup("##orient_popup")) {
        for (int i = 0; i < 5; ++i)
            if (ImGui::MenuItem(orients[i], NULL, orient == i)) {
                orient = i;
                char m[48]; snprintf(m, sizeof(m), "Orientation: %s", orients[i]);
                set_status(m);
            }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    static const char* pivots[] = { "Median", "3D Cursor", "Individual", "Origin", "BB Center" };
    static int pivot = 0;
    if (ImGui::Button(pivots[pivot], ImVec2(64, 0))) ImGui::OpenPopup("##pivot_popup");
    if (ImGui::BeginPopup("##pivot_popup")) {
        for (int i = 0; i < 5; ++i)
            if (ImGui::MenuItem(pivots[i], NULL, pivot == i)) {
                pivot = i;
                char m[48]; snprintf(m, sizeof(m), "Pivot: %s", pivots[i]);
                set_status(m);
            }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button(snapping_enabled_ ? "Snap ON" : "Snap OFF", ImVec2(66, 0))) {
        snapping_enabled_ = !snapping_enabled_;
        set_status(snapping_enabled_ ? "Snapping enabled" : "Snapping disabled");
    }

    ImGui::SameLine();
    if (ImGui::Button("View", ImVec2(42, 0))) ImGui::OpenPopup("##view_popup");
    if (ImGui::BeginPopup("##view_popup")) {
        if (ImGui::MenuItem("Frame Selected", "."))  action_frame_selected();
        ImGui::Separator();
        if (ImGui::MenuItem("Front",  "Num1"))      action_set_view(Viewport::ViewPreset::Front);
        if (ImGui::MenuItem("Back",   "Ctrl+Num1")) action_set_view(Viewport::ViewPreset::Back);
        if (ImGui::MenuItem("Right",  "Num3"))      action_set_view(Viewport::ViewPreset::Right);
        if (ImGui::MenuItem("Left",   "Ctrl+Num3")) action_set_view(Viewport::ViewPreset::Left);
        if (ImGui::MenuItem("Top",    "Num7"))      action_set_view(Viewport::ViewPreset::Top);
        if (ImGui::MenuItem("Bottom", "Ctrl+Num7")) action_set_view(Viewport::ViewPreset::Bottom);
        ImGui::Separator();
        if (ImGui::MenuItem("User Perspective", "Num5")) action_set_view(Viewport::ViewPreset::User);
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button(Scene::shading_name(scene_.shading()), ImVec2(96, 0)))
        ImGui::OpenPopup("##shading_popup");
    if (ImGui::BeginPopup("##shading_popup")) {
        Scene::Shading modes[] = {
            Scene::Shading::Wireframe, Scene::Shading::Solid,
            Scene::Shading::Material, Scene::Shading::Rendered,
        };
        for (int i = 0; i < 4; ++i) {
            bool sel = (scene_.shading() == modes[i]);
            if (ImGui::MenuItem(Scene::shading_name(modes[i]), NULL, sel))
                action_set_shading(modes[i]);
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Overlays", ImVec2(64, 0))) ImGui::OpenPopup("##overlays_popup");
    if (ImGui::BeginPopup("##overlays_popup")) {
        ImGui::TextDisabled("Overlays");
        ImGui::Separator();
        struct { const char* name; unsigned int flag; } items[] = {
            { "Grid",      OVERLAY_GRID },      { "Axes",      OVERLAY_AXES },
            { "3D Cursor", OVERLAY_CURSOR },    { "Gizmo",     OVERLAY_GIZMO },
            { "Wireframe", OVERLAY_WIREFRAME }, { "Origins",   OVERLAY_ORIGIN },
            { "Floor",     OVERLAY_FLOOR },
        };
        for (int i = 0; i < (int)(sizeof(items)/sizeof(items[0])); ++i) {
            bool b = (overlays_ & items[i].flag) != 0;
            if (ImGui::Checkbox(items[i].name, &b)) {
                if (b) overlays_ |= items[i].flag; else overlays_ &= ~items[i].flag;
            }
        }
        ImGui::EndPopup();
    }

    // Right-aligned readout.
    const Object* sel = scene_.selected();
    char label[96];
    snprintf(label, sizeof(label), "%s  |  %s",
             sel ? sel->name : "(none)", Scene::mode_name(scene_.mode()));
    float tw = ImGui::CalcTextSize(label).x;
    float avail = (float)r_vpheader_.w - tw - 12.0f;
    if (avail < 0) avail = 0;
    ImGui::SameLine(avail);
    ImGui::TextUnformatted(label);

    end_chrome();
}

// ---------------------------------------------------------------------------
// Left toolbar
// ---------------------------------------------------------------------------

void Ui::draw_toolbar()
{
    begin_chrome("##toolbar", r_toolbar_, false);
    float bw = (float)r_toolbar_.w - 10.0f;
    for (int i = 0; i < kToolCount; ++i) {
        const ToolEntry& t = kTools[i];
        if (ImGui::Button(t.label, ImVec2(bw, 0))) {
            if (t.prim >= 0) {
                action_add_primitive(t.prim);
            } else switch (t.tool) {
                case 1: request_tool(ToolRequest::Move);   set_status("Move: drag, X/Y/Z to constrain, LMB confirm, ESC cancel"); break;
                case 2: request_tool(ToolRequest::Rotate); set_status("Rotate: drag, X/Y/Z to constrain, LMB confirm, ESC cancel"); break;
                case 3: request_tool(ToolRequest::Scale);  set_status("Scale: drag, X/Y/Z to constrain, LMB confirm, ESC cancel"); break;
                case 4: {
                    // Cursor tool: snap the 3D cursor to the selection or origin.
                    Object* sel = scene_.selected();
                    Vec3 c = sel ? sel->pos : Vec3{0,0,0};
                    scene_.set_cursor3d(c);
                    scene_.commit();
                    set_status("3D cursor moved");
                    break;
                }
                default: set_status(t.tip); break;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t.tip);
        if (i == 2 || i == 7) ImGui::Separator();
    }
    end_chrome();
}

// ---------------------------------------------------------------------------
// Outliner
// ---------------------------------------------------------------------------

void Ui::draw_outliner()
{
    ImGui::SetNextWindowPos(ImVec2((float)r_outliner_.x, (float)r_outliner_.y));
    ImGui::SetNextWindowSize(ImVec2((float)r_outliner_.w, (float)r_outliner_.h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 3));
    ImGui::Begin("Outliner", NULL,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);

    if (ImGui::TreeNodeEx("Scene Collection", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::TreeNodeEx(scene_.collection_name(), ImGuiTreeNodeFlags_DefaultOpen)) {
            for (size_t i = 0; i < scene_.object_count(); ++i) {
                Object* obj = scene_.object_at(i);
                if (!obj) continue;
                ImGuiTreeNodeFlags tf = ImGuiTreeNodeFlags_Leaf |
                                        ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (obj->selected) tf |= ImGuiTreeNodeFlags_Selected;

                const char* icon = "?";
                switch (obj->type) {
                    case ObjectType::Mesh:   icon = "M"; break;
                    case ObjectType::Camera: icon = "C"; break;
                    case ObjectType::Light:  icon = "L"; break;
                    case ObjectType::Empty:  icon = "E"; break;
                }
                char label[80];
                snprintf(label, sizeof(label), "%s %s##%p", icon, obj->name, (void*)obj);
                ImGui::TreeNodeEx(label, tf);
                if (ImGui::IsItemClicked()) {
                    scene_.select(obj);
                    char msg[80];
                    snprintf(msg, sizeof(msg), "Selected: %s", obj->name);
                    set_status(msg);
                }
                if (ImGui::BeginPopupContextItem("##obj_ctx")) {
                    if (ImGui::MenuItem("Frame Selected"))  { scene_.select(obj); action_frame_selected(); }
                    if (ImGui::MenuItem("Duplicate"))       { scene_.select(obj); action_duplicate_selected(); }
                    if (ImGui::MenuItem("Delete"))          { scene_.select(obj); action_delete_selected(); }
                    ImGui::Separator();
                    if (ImGui::MenuItem(obj->visible ? "Hide" : "Unhide")) {
                        obj->visible = !obj->visible;
                        scene_.commit();
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// Properties editor
// ---------------------------------------------------------------------------

void Ui::draw_properties()
{
    ImGui::SetNextWindowPos(ImVec2((float)r_props_.x, (float)r_props_.y));
    ImGui::SetNextWindowSize(ImVec2((float)r_props_.w, (float)r_props_.h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 2));
    ImGui::Begin("##properties", NULL,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);
    properties_tabs();
    ImGui::End();
    ImGui::PopStyleVar();
}

void Ui::properties_tabs()
{
    float strip_w = 24.0f;
    ImGui::BeginChild("##prop_tabs", ImVec2(strip_w, 0), true);
    for (int i = 0; i < kPropTabCount; ++i) {
        char label[8];
        snprintf(label, sizeof(label), "%c##pt%d", kPropTabs[i][0], i);
        bool selected = (properties_tab_ == i);
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label, ImVec2(strip_w - 6.0f, 0))) {
            properties_tab_ = i;
            set_status(kPropTabs[i]);
        }
        if (selected) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kPropTabs[i]);
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##prop_body", ImVec2(0, 0), false);
    switch (properties_tab_) {
        case 0: render_tab();      break;
        case 1: output_tab();      break;
        case 2: view_layer_tab();  break;
        case 3: scene_tab();       break;
        case 4: world_tab();       break;
        case 5: object_tab();      break;
        case 6: modifiers_tab();   break;
        case 7: constraints_tab(); break;
        case 8: data_tab();        break;
        case 9: material_tab();    break;
        default: object_tab();     break;
    }
    ImGui::EndChild();
}

void Ui::render_tab()
{
    if (ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen)) {
        static int engine = 0;
        ImGui::Text("Engine");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##engine", &engine, "Cycles\0EEVEE\0Workbench\0"))
            set_status(engine == 2 ? "Workbench (viewport shading)"
                                   : "Cycles/EEVEE are placeholders in this port");
        ImGui::Separator();
        static int samples = 128;
        ImGui::SetNextItemWidth(-1);
        ImGui::DragInt("Samples", &samples, 1, 1, 4096);
        static bool denoise = true;
        ImGui::Checkbox("Denoise", &denoise);
    }
    if (ImGui::CollapsingHeader("Lighting")) {
        ImGui::TextDisabled("World lighting is fixed to Blender's default.");
    }
}

void Ui::output_tab()
{
    if (ImGui::CollapsingHeader("Format", ImGuiTreeNodeFlags_DefaultOpen)) {
        static int res_x = 1920, res_y = 1080, fps = 24;
        ImGui::Text("Resolution");
        ImGui::SetNextItemWidth(70);
        ImGui::DragInt("##resx", &res_x, 1, 4, 8192, "%d X");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        ImGui::DragInt("##resy", &res_y, 1, 4, 8192, "%d Y");
        ImGui::Separator();
        ImGui::SetNextItemWidth(-1);
        ImGui::DragInt("Frame Rate", &fps, 1, 1, 240);
        ImGui::Separator();
        static char path[128] = "/tmp/";
        ImGui::Text("Output");
        ImGui::InputText("##outpath", path, sizeof(path));
    }
}

void Ui::view_layer_tab()
{
    if (ImGui::CollapsingHeader("View Layer", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextUnformatted(scene_.collection_name());
        ImGui::Separator();
        for (size_t i = 0; i < scene_.object_count(); ++i) {
            Object* obj = scene_.object_at(i);
            if (!obj) continue;
            if (ImGui::Checkbox(obj->name, &obj->visible))
                scene_.commit();
        }
    }
}

void Ui::scene_tab()
{
    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Camera");
        ImGui::SameLine();
        ImGui::TextDisabled("(none)");
        ImGui::Separator();
        static float bg[4] = { 0.05f, 0.05f, 0.05f, 1.0f };
        ImGui::ColorEdit4("Background", bg);
        ImGui::Separator();
        static int unit_sys = 0;
        ImGui::Combo("Units", &unit_sys, "Metric\0Imperial\0None\0");
        static float unit_scale = 1.0f;
        ImGui::DragFloat("Unit Scale", &unit_scale, 0.001f, 0.001f, 1000.0f);
    }
}

void Ui::world_tab()
{
    if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen)) {
        static float wcol[3] = { 0.05f, 0.05f, 0.05f };
        ImGui::ColorEdit3("Color", wcol);
        static float strength = 1.0f;
        ImGui::DragFloat("Strength", &strength, 0.01f, 0.0f, 100.0f);
    }
}

void Ui::object_tab()
{
    Object* obj = scene_.selected();
    if (!obj) { ImGui::TextDisabled("No active object."); return; }

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Location");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat3("##loc", &obj->pos.x, 0.01f)) {}
        if (ImGui::IsItemDeactivatedAfterEdit()) scene_.commit();
        ImGui::Text("Rotation");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat3("##rot", &obj->rot_euler_deg.x, 0.5f, -360.0f, 360.0f, "%.1f")) {}
        if (ImGui::IsItemDeactivatedAfterEdit()) scene_.commit();
        ImGui::Text("Scale");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat3("##scl", &obj->scale.x, 0.01f, 0.001f, 100.0f)) {}
        if (ImGui::IsItemDeactivatedAfterEdit()) scene_.commit();
    }
    if (ImGui::CollapsingHeader("Visibility")) {
        bool changed = false;
        changed |= ImGui::Checkbox("Show in Viewport", &obj->visible);
        changed |= ImGui::Checkbox("Hide Render", &obj->hide_render);
        if (changed) scene_.commit();
    }
    if (ImGui::CollapsingHeader("Relations")) {
        ImGui::TextDisabled("No parent / constraints.");
    }
}

void Ui::modifiers_tab()
{
    Object* obj = scene_.selected();
    if (!obj || obj->type != ObjectType::Mesh) {
        ImGui::TextDisabled("Modifiers are only available on mesh objects.");
        return;
    }
    if (ImGui::CollapsingHeader("Modifiers", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Add Modifier", ImVec2(-1, 0))) ImGui::OpenPopup("##add_mod");
        if (ImGui::BeginPopup("##add_mod")) {
            const char* mods[] = { "Subdivision Surface", "Mirror", "Array", "Bevel", "Boolean" };
            for (int i = 0; i < 5; ++i)
                if (ImGui::MenuItem(mods[i])) {
                    char m[64]; snprintf(m, sizeof(m), "%s: not implemented", mods[i]);
                    action_show_info(m);
                }
            ImGui::EndPopup();
        }
        ImGui::Separator();
        ImGui::TextDisabled("No modifiers on this mesh.");
    }
}

void Ui::constraints_tab()
{
    ImGui::TextDisabled("No constraints on the active object.");
    if (ImGui::Button("Add Constraint", ImVec2(-1, 0)))
        action_show_info("Constraints are not implemented in this port.");
}

void Ui::data_tab()
{
    Object* obj = scene_.selected();
    if (!obj) { ImGui::TextDisabled("No active object."); return; }
    const char* type_str = "?";
    switch (obj->type) {
        case ObjectType::Mesh:   type_str = "Mesh";   break;
        case ObjectType::Camera: type_str = "Camera"; break;
        case ObjectType::Light:  type_str = "Light";  break;
        case ObjectType::Empty:  type_str = "Empty";  break;
    }
    if (ImGui::CollapsingHeader(type_str, ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Name: %s", obj->name);
        ImGui::Text("Type: %s", type_str);
        if (obj->type == ObjectType::Mesh) {
            ImGui::Separator();
            ImGui::Text("Vertices: %u", obj->mesh.nverts);
            ImGui::Text("Faces:    %u", obj->mesh.ntris);
        } else if (obj->type == ObjectType::Light) {
            ImGui::Separator();
            const char* lt = "Point";
            switch (obj->light_type) {
                case LightType::Point: lt = "Point"; break;
                case LightType::Sun:   lt = "Sun";   break;
                case LightType::Spot:  lt = "Spot";  break;
                case LightType::Area:  lt = "Area";  break;
            }
            ImGui::Text("Light Type: %s", lt);
            static float energy = 1000.0f;
            ImGui::DragFloat("Energy (W)", &energy, 10.0f, 0.0f, 100000.0f);
        } else if (obj->type == ObjectType::Camera) {
            ImGui::Separator();
            static float lens = 50.0f;
            ImGui::DragFloat("Lens (mm)", &lens, 0.5f, 1.0f, 500.0f);
            static int sensor = 0;
            ImGui::Combo("Sensor Fit", &sensor, "Auto\0Horizontal\0Vertical\0");
        }
    }
}

void Ui::material_tab()
{
    Object* obj = scene_.selected();
    if (!obj || obj->type != ObjectType::Mesh) {
        ImGui::TextDisabled("Materials are only available on mesh objects.");
        return;
    }
    if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextUnformatted("Principled BSDF");
        ImGui::Indent();
        if (ImGui::ColorEdit4("Base Color", obj->material.base_color)) {}
        if (ImGui::IsItemDeactivatedAfterEdit()) scene_.commit();
        if (ImGui::SliderFloat("Metallic",  &obj->material.metallic,  0.0f, 1.0f)) {}
        if (ImGui::IsItemDeactivatedAfterEdit()) scene_.commit();
        if (ImGui::SliderFloat("Roughness", &obj->material.roughness, 0.0f, 1.0f)) {}
        if (ImGui::IsItemDeactivatedAfterEdit()) scene_.commit();
        ImGui::Unindent();
    }
    if (ImGui::CollapsingHeader("Settings")) {
        ImGui::TextDisabled("Blend Mode: Opaque");
        ImGui::TextDisabled("Backface Culling: Off");
    }
}

// ---------------------------------------------------------------------------
// Timeline
// ---------------------------------------------------------------------------

void Ui::draw_timeline()
{
    begin_chrome("##timeline", r_timeline_, false);

    if (ImGui::Button("|<<")) scene_.set_frame_current(scene_.frame_start());
    ImGui::SameLine();
    if (ImGui::Button("<"))  scene_.set_frame_current(scene_.frame_current() - 1);
    ImGui::SameLine();
    if (ImGui::Button(playing_ ? "||" : ">")) {
        playing_ = !playing_;
        set_status(playing_ ? "Playing" : "Paused");
    }
    ImGui::SameLine();
    if (ImGui::Button(">"))  scene_.set_frame_current(scene_.frame_current() + 1);
    ImGui::SameLine();
    if (ImGui::Button(">>|")) scene_.set_frame_current(scene_.frame_end());

    ImGui::SameLine();
    int start = scene_.frame_start(), end = scene_.frame_end();
    ImGui::SetNextItemWidth(56);
    if (ImGui::DragInt("##start", &start, 1, -100000, end)) {
        if (start != scene_.frame_start()) scene_.set_frame_range(start, end);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(56);
    if (ImGui::DragInt("##end", &end, 1, start, 100000)) {
        if (end != scene_.frame_end()) scene_.set_frame_range(start, end);
    }
    ImGui::SameLine();
    int cur = scene_.frame_current();
    ImGui::SetNextItemWidth(56);
    if (ImGui::DragInt("##cur", &cur, 1, start, end)) scene_.set_frame_current(cur);

    ImGui::Separator();
    int scrub = scene_.frame_current();
    ImGui::SetNextItemWidth(-1);
    char fmt[32];
    snprintf(fmt, sizeof(fmt), "Frame %d", scrub);
    if (ImGui::SliderInt("##scrub", &scrub, scene_.frame_start(), scene_.frame_end(), fmt))
        scene_.set_frame_current(scrub);

    if (playing_) {
        play_accum_ += 1.0f / 24.0f;   // 24 fps playback
        while (play_accum_ >= 1.0f / 24.0f) {
            play_accum_ -= 1.0f / 24.0f;
            int n = scene_.frame_current() + 1;
            if (n > scene_.frame_end()) n = scene_.frame_start();
            scene_.set_frame_current(n);
        }
    }
    end_chrome();
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

void Ui::draw_status_bar()
{
    begin_chrome("##statusbar", r_status_, false);
    const Object* sel = scene_.selected();
    ImGui::Text("%s | %s | (%d) %s",
                scene_.collection_name(),
                sel ? sel->name : "(none)",
                (int)scene_.object_count(),
                Scene::mode_name(scene_.mode()));

    char right[96];
    snprintf(right, sizeof(right), "%s | %.1f FPS",
             viewport_.gpu_active() ? "GPU" : "CPU", fps_display_);
    float tw = ImGui::CalcTextSize(right).x;
    float avail = (float)r_status_.w - tw - 12.0f;
    if (avail < 0) avail = 0;
    ImGui::SameLine(avail);
    ImGui::TextUnformatted(right);

    if (status_message_[0]) {
        ImGui::SameLine();
        ImGui::Text("| %s", status_message_);
    }
    end_chrome();
}

// ---------------------------------------------------------------------------
// Modals: info dialog + transform banner
// ---------------------------------------------------------------------------

void Ui::draw_modals()
{
    // Centred transform banner (non-interactive).
    if (banner_active_) {
        ImVec2 sz = ImGui::CalcTextSize(banner_text_);
        float w = sz.x + 24.0f, h = sz.y + 12.0f;
        float x = (float)vp_rect_.x + ((float)vp_rect_.w - w) * 0.5f;
        float y = (float)vp_rect_.y + 36.0f;
        ImGui::SetNextWindowPos(ImVec2(x, y));
        ImGui::SetNextWindowSize(ImVec2(w, h));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.85f));
        ImGui::Begin("##banner", NULL,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
        ImGui::TextUnformatted(banner_text_);
        ImGui::End();
        ImGui::PopStyleColor();
    }

    // Info dialog (edge-triggered open).
    static bool was_open = false;
    if (info_open_ && !was_open) ImGui::OpenPopup("Info##blender");
    was_open = info_open_;
    if (ImGui::BeginPopupModal("Info##blender", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(info_text_);
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            info_open_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void Ui::action_new_scene()
{
    scene_.load_startup();
    viewport_.frame_selected(scene_);
    set_status("New scene");
}

void Ui::action_add_primitive(int prim)
{
    Mesh m;
    const char* name = "Object";
    switch (prim) {
        case 0: m = mesh_cube(2.0f);              name = "Cube";      break;
        case 1: m = mesh_uv_sphere(1.0f, 32, 16); name = "Sphere";    break;
        case 2: m = mesh_ico_sphere(1.0f, 0);     name = "IcoSphere"; break;
        case 3: m = mesh_cylinder(1.0f, 2.0f, 32);name = "Cylinder";  break;
        case 4: m = mesh_plane(2.0f);             name = "Plane";     break;
        default: return;
    }
    char full[40];
    int suffix = 0;
    for (size_t i = 0; i < scene_.object_count(); ++i) {
        const Object* o = scene_.object_at(i);
        if (o && strncmp(o->name, name, strlen(name)) == 0) suffix++;
    }
    if (suffix > 0) snprintf(full, sizeof(full), "%s.%03d", name, suffix);
    else            snprintf(full, sizeof(full), "%s", name);

    Object* o = scene_.add_mesh_object(full, m);
    if (o) {
        o->pos = scene_.cursor3d();
        scene_.select(o);
        scene_.commit();
        char msg[80];
        snprintf(msg, sizeof(msg), "Added %s", full);
        set_status(msg);
    } else {
        mesh_free(m);
        set_status("Add failed: scene is full");
    }
}

void Ui::action_delete_selected()
{
    Object* sel = scene_.selected();
    if (!sel) { set_status("Delete: nothing selected"); return; }
    char msg[80];
    snprintf(msg, sizeof(msg), "Deleted %s", sel->name);
    scene_.remove(sel);
    scene_.commit();
    set_status(msg);
}

void Ui::action_duplicate_selected()
{
    Object* sel = scene_.selected();
    if (!sel || sel->type != ObjectType::Mesh) {
        set_status("Duplicate: select a mesh object");
        return;
    }
    Mesh copy;
    copy.nverts = sel->mesh.nverts;
    copy.ntris  = sel->mesh.ntris;
    copy.verts  = (float*)malloc(sizeof(float) * 6 * copy.nverts);
    copy.tris   = (unsigned int*)malloc(sizeof(unsigned int) * 3 * copy.ntris);
    if (copy.verts) memcpy(copy.verts, sel->mesh.verts, sizeof(float)*6*copy.nverts);
    if (copy.tris)  memcpy(copy.tris,  sel->mesh.tris,  sizeof(unsigned int)*3*copy.ntris);

    char name[40];
    int suffix = 0;
    for (size_t i = 0; i < scene_.object_count(); ++i) {
        const Object* o = scene_.object_at(i);
        if (o && strncmp(o->name, sel->name, strlen(sel->name)) == 0) suffix++;
    }
    snprintf(name, sizeof(name), "%s.%03d", sel->name, suffix);
    Object* dup = scene_.add_mesh_object(name, copy);
    if (dup) {
        dup->pos = sel->pos;
        dup->pos.x += 1.0f;
        dup->material = sel->material;
        scene_.select(dup);
        scene_.commit();
        char msg[80];
        snprintf(msg, sizeof(msg), "Duplicated as %s", name);
        set_status(msg);
    } else {
        mesh_free(copy);
        set_status("Duplicate failed: scene is full");
    }
}

void Ui::action_frame_selected()
{
    viewport_.frame_selected(scene_);
    set_status("Framed selection");
}

void Ui::action_undo()
{
    if (scene_.undo()) set_status("Undo");
    else               set_status("Nothing to undo");
}

void Ui::action_redo()
{
    if (scene_.redo()) set_status("Redo");
    else               set_status("Nothing to redo");
}

void Ui::action_select_all()
{
    scene_.select_all();
    set_status("Selected all");
}

void Ui::action_select_none()
{
    scene_.select_none();
    set_status("Selection cleared");
}

void Ui::action_set_view(Viewport::ViewPreset v)
{
    viewport_.set_view_preset(v);
    set_status("View preset applied");
}

void Ui::action_set_shading(Scene::Shading s)
{
    scene_.set_shading(s);
    set_status(Scene::shading_name(s));
}

void Ui::action_set_mode(Scene::Mode m)
{
    scene_.set_mode(m);
    set_status(Scene::mode_name(m));
    if (m != Scene::Mode::Object)
        action_show_info("Only Object Mode is fully interactive in this port;\nother modes are visual placeholders.");
}

void Ui::action_set_workspace(int i)
{
    workspace_index_ = i;
    set_status(kWorkspaces[i]);
    if (i != 0)
        action_show_info("Only the Layout workspace is implemented;\nother workspaces show the same panels.");
}

void Ui::action_show_info(const char* msg)
{
    snprintf(info_text_, sizeof(info_text_), "%s", msg ? msg : "");
    info_open_ = true;
}

} // namespace blender
