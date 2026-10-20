// Blender UI built with Dear ImGui.
//
// Reproduces Blender's default "Layout" workspace chrome:
//   - top menu bar: File / Edit / Render / Window / Help
//   - workspace tab strip: Layout, Modeling, Sculpting, UV Editing, ...
//   - 3D Viewport header: mode selector, view/shading/overlay popovers,
//     transform orientation + pivot, snapping toggle, gizmo toggles
//   - left toolbar (T): Select Box, Cursor, Measure, Add Cube/Sphere/...,
//     Move / Rotate / Scale, Transform, Annotate
//   - right column: Outliner (top) + Properties editor (bottom) with
//     Render / Output / View Layer / Scene / World / Object / Modifiers /
//     Constraints / Data / Material tabs
//   - bottom strip: Timeline with playhead + range fields
//   - status bar: collection | object | (n) Mode | memory / GPU / FPS
//
// Layout is computed once per frame by compute_layout() from the window size
// and stored in member rects, so main.cpp (GL scissor/viewport) and the panel
// builders always agree on where the 3D viewport lives.  Every panel is a
// fixed-position ImGui window with tight padding; no docking branch is used.
#ifndef BLENDER_UI_H
#define BLENDER_UI_H

#include "scene.h"
#include "viewport.h"

namespace blender {

// Transform tool requested by the toolbar / menu / hotkeys.  main.cpp polls
// take_tool_request() once per frame and drives the modal transform machine.
enum class ToolRequest {
    None,
    Move,
    Rotate,
    Scale,
};

class Ui {
public:
    struct Rect { int x, y, w, h; };

    Ui(Scene& scene, Viewport& viewport);

    // Compute every panel rect from the window size.  Call once per frame
    // BEFORE the 3D pass (main.cpp reads viewport_rect() for GL scissor),
    // then call draw() between ImGui::NewFrame() and ImGui::Render().
    void compute_layout(int win_w, int win_h);
    void draw(float dt_seconds);

    const Rect& viewport_rect() const { return vp_rect_; }
    int window_width()  const { return win_w_; }
    int window_height() const { return win_h_; }

    bool wants_quit() const { return wants_quit_; }
    int  workspace_index() const { return workspace_index_; }
    const char* status_message() const { return status_message_; }

    // --- Public hooks called from main.cpp's input handler ----------------
    void action_quit_requested()             { wants_quit_ = true; }
    void action_delete_selected_public()     { action_delete_selected(); }
    void action_duplicate_selected_public()  { action_duplicate_selected(); }
    void action_add_primitive_public(int id) { action_add_primitive(id); }
    void action_undo_public()                { action_undo(); }
    void action_redo_public()                { action_redo(); }
    void action_select_all_public()          { action_select_all(); }
    void action_select_none_public()         { action_select_none(); }
    void action_frame_selected_public()      { action_frame_selected(); }
    void action_set_view_public(Viewport::ViewPreset v) { action_set_view(v); }
    void set_status_public(const char* msg)  { set_status(msg); }

    // Transform tool request, polled and cleared by main.cpp each frame.
    ToolRequest take_tool_request() {
        ToolRequest t = tool_request_;
        tool_request_ = ToolRequest::None;
        return t;
    }
    void request_tool(ToolRequest t) { tool_request_ = t; }

    // Banner shown centred over the viewport while a modal transform runs.
    void set_transform_banner(const char* text);

    // Active overlay bitmask consumed by Viewport::render each frame.
    unsigned int overlays() const { return overlays_; }
    void set_overlays(unsigned int o) { overlays_ = o; }

private:
    // Panel builders (all read the member rects computed by compute_layout).
    void draw_menu_bar();
    void draw_workspace_tabs();
    void draw_viewport_header();
    void draw_toolbar();
    void draw_outliner();
    void draw_properties();
    void draw_timeline();
    void draw_status_bar();
    void draw_modals();

    // Sub-widgets
    void properties_tabs();
    void render_tab();
    void output_tab();
    void view_layer_tab();
    void scene_tab();
    void world_tab();
    void object_tab();
    void modifiers_tab();
    void constraints_tab();
    void data_tab();
    void material_tab();

    // Begin a fixed, non-movable chrome window with tight padding so buttons
    // are not clipped by the default 6x4 WindowPadding in short strips.
    void begin_chrome(const char* id, const Rect& r, bool menu_bar);
    void end_chrome();

    // Actions
    void action_new_scene();
    void action_add_primitive(int primitive_id);
    void action_delete_selected();
    void action_duplicate_selected();
    void action_frame_selected();
    void action_undo();
    void action_redo();
    void action_select_all();
    void action_select_none();
    void action_set_view(Viewport::ViewPreset v);
    void action_set_shading(Scene::Shading s);
    void action_set_mode(Scene::Mode m);
    void action_set_workspace(int i);
    void action_show_info(const char* msg);
    void set_status(const char* msg);

    Scene&    scene_;
    Viewport& viewport_;

    // Layout rects, recomputed every frame by compute_layout().
    Rect r_menu_, r_tabs_, r_vpheader_, r_toolbar_;
    Rect r_outliner_, r_props_, r_timeline_, r_status_;
    Rect vp_rect_;
    int  win_w_, win_h_;

    bool wants_quit_;
    int  workspace_index_;
    int  properties_tab_;
    bool snapping_enabled_;
    float fps_display_;
    int   frame_count_accum_;
    float fps_accum_timer_;

    char status_message_[128];
    unsigned int overlays_;
    ToolRequest tool_request_;

    // Modal info dialog (operations that are visual-only in this port).
    bool info_open_;
    char info_text_[128];

    // Centred transform banner.
    bool banner_active_;
    char banner_text_[96];

    // Timeline playback state (persists across frames).
    bool  playing_;
    float play_accum_;
};

} // namespace blender

#endif // BLENDER_UI_H
