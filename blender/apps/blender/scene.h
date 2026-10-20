// Minimal Blender-style scene model.
//
// Mirrors the shape of Blender's own data blocks (Object -> Mesh/Material) but
// pared down to what a lightweight port can render on the EwokOS GLES2 front
// end: transform, mesh (vertices + normals + triangle indices), material
// diffuse/base colour, and object type (for icon selection in the Outliner).
//
// The startup scene reproduces Blender's own default: a cube, a camera and a
// point light at fixed offsets, plus a world-locked 3D cursor at the origin.
#ifndef BLENDER_SCENE_H
#define BLENDER_SCENE_H

#include <stddef.h>

namespace blender {

enum class ObjectType {
    Mesh,
    Camera,
    Light,
    Empty,
};

enum class LightType {
    Point,
    Sun,
    Spot,
    Area,
};

// Column-major 4x4, matching the layout used by GLES2 glUniformMatrix4fv.
struct Mat4 {
    float m[16];
};

struct Vec3 {
    float x, y, z;
};

Mat4  mat4_identity();
Mat4  mat4_translation(float x, float y, float z);
Mat4  mat4_scale(float x, float y, float z);
Mat4  mat4_rotation_euler(float rx_deg, float ry_deg, float rz_deg);
Mat4  mat4_mul(const Mat4& a, const Mat4& b);
Vec3  mat4_transform_point(const Mat4& m, const Vec3& p);

struct Mesh {
    // Interleaved position + normal, one entry per vertex.  Uploaded as-is
    // to a GL_ARRAY_BUFFER with stride 6*sizeof(float).
    float* verts;        // [x y z nx ny nz] * nverts
    unsigned int nverts;
    unsigned int* tris;  // triangle indices, 3 per face
    unsigned int ntris;
};

struct Material {
    float base_color[4];  // RGBA linear, Blender's Principled BSDF "Base Color"
    float metallic;       // 0..1
    float roughness;      // 0..1
};

struct Object {
    char name[32];
    ObjectType type;
    LightType light_type;

    Vec3 pos;
    Vec3 rot_euler_deg;
    Vec3 scale;

    Mesh mesh;              // owned by the Object (Mesh.type == Mesh only)
    Material material;

    bool selected;
    bool visible;
    bool hide_viewport;
    bool hide_render;
};

struct Collection {
    char name[32];
    bool expanded;
};

// The startup scene, matching Blender's default_file.blend contents:
//   Scene Collection
//     Collection
//       Cube   (Mesh, 2x2x2 at origin)
//       Camera (at (7.36, -6.93, 4.96), rotated to look at the origin)
//       Light  (Point, at (4.08, 1.01, 5.90), strength 1000W)
class Scene {
public:
    Scene();
    ~Scene();

    // Build the default startup scene.  Safe to call more than once (drops
    // everything first).
    void load_startup();

    // Object lifetime
    Object* add_mesh_object(const char* name, const Mesh& mesh);
    Object* add_camera(const char* name, const Vec3& pos, const Vec3& rot_deg);
    Object* add_light(const char* name, LightType type, const Vec3& pos,
                      float strength_w);
    void    remove(Object* obj);

    Object*       object_at(size_t i)       { return objects_[i]; }
    const Object* object_at(size_t i) const { return objects_[i]; }
    size_t        object_count() const      { return n_objects_; }

    Object*       selected()       { return selected_; }
    const Object* selected() const { return selected_; }
    void          select(Object* obj);

    // 3D cursor position (world space), rendered as the classic red/white
    // circle overlay.
    const Vec3& cursor3d() const { return cursor3d_; }
    void        set_cursor3d(const Vec3& v) { cursor3d_ = v; }

    // --- Undo / redo (linear snapshot history) ----------------------------
    // commit() captures the CURRENT (post-mutation) state onto the history
    // tail; call it after every discrete edit (add / delete / duplicate /
    // transform-confirm / new-scene).  undo() / redo() step the cursor and
    // restore the corresponding snapshot.  load_startup() seeds history[0].
    void commit();
    bool undo();
    bool redo();
    bool can_undo() const { return history_cur_ > 0; }
    bool can_redo() const { return history_cur_ < history_len_ - 1; }

    // Select every visible object / clear the selection (Edit menu).
    void select_all();
    void select_none();

    // Active collection name (Outliner root row).
    const char* collection_name() const { return collection_.name; }

    // Frame counter used by the timeline.
    int  frame_current() const { return frame_current_; }
    void set_frame_current(int f);
    int  frame_start() const { return frame_start_; }
    int  frame_end()   const { return frame_end_; }
    void set_frame_range(int start, int end);

    // Interaction mode (Object / Edit / Sculpt / ...) — mirrored in the
    // viewport header strip and the mode selector in the top-left of the
    // 3D view.
    enum class Mode { Object, Edit, Sculpt, VertexPaint, WeightPaint, TexturePaint, Shading };
    Mode mode() const { return mode_; }
    void set_mode(Mode m) { mode_ = m; }
    static const char* mode_name(Mode m);

    // Shading mode (Blender's viewport-shading pie menu).
    enum class Shading { Wireframe, Solid, Material, Rendered };
    Shading shading() const { return shading_; }
    void    set_shading(Shading s) { shading_ = s; }
    static const char* shading_name(Shading s);

private:
    static const size_t kMaxObjects = 32;
    static const int    kMaxUndo    = 16;

    // An opaque deep copy of the whole scene, used for undo/redo snapshots.
    struct Snapshot;
    Snapshot* capture() const;
    void      restore(Snapshot* s);
    void      free_snapshot(Snapshot* s);
    void      clear_history();

    void clear_objects();

    Object*    objects_[kMaxObjects];
    size_t     n_objects_;
    Object*    selected_;
    Vec3       cursor3d_;
    Collection collection_;
    int        frame_current_;
    int        frame_start_;
    int        frame_end_;
    Mode       mode_;
    Shading    shading_;

    // Linear undo history: history_[0..history_len_-1] are committed states,
    // history_cur_ is the index of the state currently loaded.
    Snapshot*  history_[kMaxUndo + 1];
    int        history_len_;
    int        history_cur_;
};

// Mesh constructors for the primitives Blender exposes under Add > Mesh.
// The caller takes ownership of the returned buffers (free with mesh_free()).
Mesh mesh_cube(float size);
Mesh mesh_uv_sphere(float radius, int segments, int rings);
Mesh mesh_ico_sphere(float radius, int subdivisions);
Mesh mesh_cylinder(float radius, float depth, int vertices);
Mesh mesh_plane(float size);
void mesh_free(Mesh& m);

// Camera / light "empty" mesh placeholders — used by the viewport renderer to
// draw Blender's iconography (small camera box, sphere for point light).
Mesh mesh_camera_icon();
Mesh mesh_light_icon();
Mesh mesh_empty_icon();

} // namespace blender

#endif // BLENDER_SCENE_H
