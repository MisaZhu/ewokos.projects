// Blender-style 3D viewport renderer (GLES2).
//
// Draws the classic Blender viewport backdrop: an infinite-ish gray grid with
// +X red / +Y green axis lines, the origin axes gizmo in the top-right corner,
// scene meshes with per-vertex diffuse shading, wireframe overlays for
// selected objects, the 3D cursor as a red/white circle, and icon placeholders
// for camera / light / empty objects.
//
// The camera is an orbit / pan / zoom rig matching Blender's numpad-based
// navigation semantics (middle-mouse orbit, shift+middle-mouse pan, wheel
// dolly).  All rendering goes through the EwokOS libGLESv2 front end; no
// VAOs, no instancing, no polygon-mode — meshes are drawn as GL_TRIANGLES
// from a single interleaved VBO.
#ifndef BLENDER_VIEWPORT_H
#define BLENDER_VIEWPORT_H

#include "scene.h"

namespace blender {

// Which overlay layers the viewport draws, matching Blender's viewport
// "Overlays" popover.  Bit flags; combine with |.
enum OverlayFlags : unsigned int {
    OVERLAY_NONE         = 0,
    OVERLAY_GRID         = 1 << 0,
    OVERLAY_AXES         = 1 << 1,   // world X/Y/Z axes through the origin
    OVERLAY_CURSOR       = 1 << 2,   // 3D cursor
    OVERLAY_GIZMO        = 1 << 3,   // top-right axis gizmo
    OVERLAY_WIREFRAME    = 1 << 4,   // wireframe overlay on selected objects
    OVERLAY_ORIGIN       = 1 << 5,   // small dot at each object's origin
    OVERLAY_FLOOR        = 1 << 6,   // solid floor plane (Blender "Floor" checkbox)
    OVERLAY_ALL          = OVERLAY_GRID | OVERLAY_AXES | OVERLAY_CURSOR |
                           OVERLAY_GIZMO | OVERLAY_WIREFRAME | OVERLAY_ORIGIN |
                           OVERLAY_FLOOR,
};

class Viewport {
public:
    Viewport();
    ~Viewport();

    // Create GPU resources.  Must be called after eglMakeCurrent().
    bool init(int width, int height);
    void shutdown();

    // Resize the viewport (window resize / panel splitter drag).
    void resize(int width, int height);

    int width()  const { return width_; }
    int height() const { return height_; }

    // Render one frame.  Clears the colour+depth buffer with Blender's
    // viewport background (#393939) and draws the scene + overlays.
    void render(const Scene& scene, unsigned int overlays);

    // --- Camera navigation (Blender conventions) ---------------------------

    // Orbit the camera around its target by (dx, dy) pixels.
    void orbit(float dx_pixels, float dy_pixels);
    // Pan the camera target perpendicular to the view direction.
    void pan(float dx_pixels, float dy_pixels);
    // Dolly the camera toward / away from the target (wheel).
    void zoom(float delta);
    // Snap to Blender's canonical views (numpad 1/3/7 + ctrl variants).
    enum class ViewPreset { Front, Back, Left, Right, Top, Bottom, User, Camera };
    void set_view_preset(ViewPreset v);

    // Frame the selection (Blender's "." / Home key).
    void frame_selected(const Scene& scene);

    // Current orbit state, exposed for the header strip readout.
    float distance() const { return distance_; }
    float azimuth_deg() const { return azimuth_deg_; }
    float elevation_deg() const { return elevation_deg_; }
    const Vec3& target() const { return target_; }

    // True while the user is holding a nav button (used to suppress clicks).
    bool is_navigating() const { return navigating_; }
    void begin_navigate() { navigating_ = true; }
    void end_navigate()   { navigating_ = false; }

    // True if the GPU path (PGL_G3D on raspi5) is dispatching scene draws.
    bool gpu_active() const { return gpu_active_; }

private:
    void build_grid();
    void build_axes();
    void build_cursor();
    void build_gizmo();
    void build_wire_cube();

    void compute_view_matrix(Mat4& out_view) const;
    void compute_proj_matrix(Mat4& out_proj, float aspect) const;

    void draw_grid(const Mat4& vp);
    void draw_axes(const Mat4& vp);
    void draw_cursor(const Mat4& vp, const Vec3& world);
    void draw_gizmo();
    void draw_mesh_object(const Mat4& vp, const Object& obj, bool wireframe);
    void draw_selection_outline(const Mat4& vp, const Object& obj);
    void draw_object_origin(const Mat4& vp, const Object& obj);

    int   width_, height_;
    bool  initialised_;
    bool  gpu_active_;

    // Orbit camera
    Vec3  target_;
    float distance_;
    float azimuth_deg_;    // around +Z
    float elevation_deg_;  // above XY plane
    float fov_deg_;
    float near_, far_;
    bool  navigating_;
    bool  ortho_;          // Blender's numpad views are orthographic

    // GPU resources
    unsigned int prog_shaded_;      // mesh + gouraud
    unsigned int prog_unlit_;       // grid / axes / cursor / gizmo / wireframe
    unsigned int vbo_grid_;
    unsigned int vbo_axes_;
    unsigned int vbo_cursor_;
    unsigned int vbo_gizmo_;
    unsigned int vbo_wire_cube_;
    int grid_vertex_count_;
    int axes_vertex_count_;
    int cursor_vertex_count_;
    int gizmo_vertex_count_;
    int wire_cube_vertex_count_;

    // Uniform locations (cached at init).
    int loc_sh_mvp_, loc_sh_normal_, loc_sh_color_, loc_sh_light_, loc_sh_shade_;
    int loc_un_mvp_, loc_un_color_;
};

} // namespace blender

#endif // BLENDER_VIEWPORT_H
