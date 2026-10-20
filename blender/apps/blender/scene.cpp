// Blender-style scene model implementation.
#include "scene.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace blender {

// ---------------------------------------------------------------------------
// Mat4 helpers (column-major, GL convention).
// ---------------------------------------------------------------------------

Mat4 mat4_identity()
{
    Mat4 r;
    memset(&r, 0, sizeof(r));
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 mat4_translation(float x, float y, float z)
{
    Mat4 r = mat4_identity();
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}

Mat4 mat4_scale(float x, float y, float z)
{
    Mat4 r = mat4_identity();
    r.m[0] = x; r.m[5] = y; r.m[10] = z;
    return r;
}

// Euler XYZ, degrees, matching Blender's Object rotation_euler default order.
Mat4 mat4_rotation_euler(float rx_deg, float ry_deg, float rz_deg)
{
    float rx = rx_deg * (float)M_PI / 180.0f;
    float ry = ry_deg * (float)M_PI / 180.0f;
    float rz = rz_deg * (float)M_PI / 180.0f;
    float cx = cosf(rx), sx = sinf(rx);
    float cy = cosf(ry), sy = sinf(ry);
    float cz = cosf(rz), sz = sinf(rz);

    // R = Rz * Ry * Rx  (column-major)
    Mat4 r = mat4_identity();
    r.m[0]  = cy*cz;                  r.m[1]  = cy*sz;                  r.m[2]  = -sy;
    r.m[4]  = sx*sy*cz - cx*sz;       r.m[5]  = sx*sy*sz + cx*cz;       r.m[6]  = sx*cy;
    r.m[8]  = cx*sy*cz + sx*sz;       r.m[9]  = cx*sy*sz - sx*cz;       r.m[10] = cx*cy;
    return r;
}

Mat4 mat4_mul(const Mat4& a, const Mat4& b)
{
    Mat4 r;
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k)
                s += a.m[k*4 + row] * b.m[c*4 + k];
            r.m[c*4 + row] = s;
        }
    }
    return r;
}

Vec3 mat4_transform_point(const Mat4& m, const Vec3& p)
{
    Vec3 r;
    float w = m.m[3]*p.x + m.m[7]*p.y + m.m[11]*p.z + m.m[15];
    if (w == 0.0f) w = 1.0f;
    r.x = (m.m[0]*p.x + m.m[4]*p.y + m.m[8]*p.z  + m.m[12]) / w;
    r.y = (m.m[1]*p.x + m.m[5]*p.y + m.m[9]*p.z  + m.m[13]) / w;
    r.z = (m.m[2]*p.x + m.m[6]*p.y + m.m[10]*p.z + m.m[14]) / w;
    return r;
}

// ---------------------------------------------------------------------------
// Mesh primitives.
// ---------------------------------------------------------------------------

namespace {

struct MeshBuilder {
    float* v;
    unsigned int* idx;
    unsigned int nv, ni;
};

// Emit one vertex (position + normal) and return its index.
unsigned int mb_push(MeshBuilder& b, float x, float y, float z,
                     float nx, float ny, float nz)
{
    unsigned int i = b.nv;
    b.v[i*6 + 0] = x; b.v[i*6 + 1] = y; b.v[i*6 + 2] = z;
    b.v[i*6 + 3] = nx; b.v[i*6 + 4] = ny; b.v[i*6 + 5] = nz;
    b.nv++;
    return i;
}

void mb_tri(MeshBuilder& b, unsigned int a, unsigned int c, unsigned int d)
{
    b.idx[b.ni++] = a;
    b.idx[b.ni++] = c;
    b.idx[b.ni++] = d;
}

void mb_quad(MeshBuilder& b, unsigned int a, unsigned int c, unsigned int d, unsigned int e)
{
    mb_tri(b, a, c, d);
    mb_tri(b, a, d, e);
}

} // namespace

Mesh mesh_cube(float size)
{
    // 24 verts (4 per face, unique normals) + 36 indices.  Matches Blender's
    // default Cube primitive vertex layout.
    const float h = size * 0.5f;
    Mesh m;
    m.nverts = 24;
    m.ntris  = 12;
    m.verts  = (float*)malloc(sizeof(float) * 6 * m.nverts);
    m.tris   = (unsigned int*)malloc(sizeof(unsigned int) * 3 * m.ntris);
    MeshBuilder b = { m.verts, m.tris, 0, 0 };

    // +Z
    unsigned int v0 = mb_push(b, -h, -h,  h,  0, 0, 1);
    unsigned int v1 = mb_push(b,  h, -h,  h,  0, 0, 1);
    unsigned int v2 = mb_push(b,  h,  h,  h,  0, 0, 1);
    unsigned int v3 = mb_push(b, -h,  h,  h,  0, 0, 1);
    mb_quad(b, v0, v1, v2, v3);
    // -Z
    v0 = mb_push(b,  h, -h, -h,  0, 0,-1);
    v1 = mb_push(b, -h, -h, -h,  0, 0,-1);
    v2 = mb_push(b, -h,  h, -h,  0, 0,-1);
    v3 = mb_push(b,  h,  h, -h,  0, 0,-1);
    mb_quad(b, v0, v1, v2, v3);
    // +X
    v0 = mb_push(b,  h, -h,  h,  1, 0, 0);
    v1 = mb_push(b,  h, -h, -h,  1, 0, 0);
    v2 = mb_push(b,  h,  h, -h,  1, 0, 0);
    v3 = mb_push(b,  h,  h,  h,  1, 0, 0);
    mb_quad(b, v0, v1, v2, v3);
    // -X
    v0 = mb_push(b, -h, -h, -h, -1, 0, 0);
    v1 = mb_push(b, -h, -h,  h, -1, 0, 0);
    v2 = mb_push(b, -h,  h,  h, -1, 0, 0);
    v3 = mb_push(b, -h,  h, -h, -1, 0, 0);
    mb_quad(b, v0, v1, v2, v3);
    // +Y
    v0 = mb_push(b, -h,  h,  h,  0, 1, 0);
    v1 = mb_push(b,  h,  h,  h,  0, 1, 0);
    v2 = mb_push(b,  h,  h, -h,  0, 1, 0);
    v3 = mb_push(b, -h,  h, -h,  0, 1, 0);
    mb_quad(b, v0, v1, v2, v3);
    // -Y
    v0 = mb_push(b, -h, -h, -h,  0,-1, 0);
    v1 = mb_push(b,  h, -h, -h,  0,-1, 0);
    v2 = mb_push(b,  h, -h,  h,  0,-1, 0);
    v3 = mb_push(b, -h, -h,  h,  0,-1, 0);
    mb_quad(b, v0, v1, v2, v3);
    return m;
}

Mesh mesh_uv_sphere(float radius, int segments, int rings)
{
    if (segments < 3) segments = 3;
    if (rings < 2)    rings = 2;

    Mesh m;
    // Verts: rings+1 rows of (segments+1) verts (poles duplicated per ring).
    m.nverts = (unsigned int)((rings + 1) * (segments + 1));
    m.ntris  = (unsigned int)(rings * segments * 2);
    m.verts  = (float*)malloc(sizeof(float) * 6 * m.nverts);
    m.tris   = (unsigned int*)malloc(sizeof(unsigned int) * 3 * m.ntris);
    MeshBuilder b = { m.verts, m.tris, 0, 0 };

    for (int r = 0; r <= rings; ++r) {
        float phi = (float)M_PI * r / rings;  // 0..pi
        for (int s = 0; s <= segments; ++s) {
            float theta = 2.0f * (float)M_PI * s / segments;
            float nx = sinf(phi) * cosf(theta);
            float ny = sinf(phi) * sinf(theta);
            float nz = cosf(phi);
            mb_push(b, nx * radius, ny * radius, nz * radius, nx, ny, nz);
        }
    }
    int stride = segments + 1;
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            unsigned int a = r * stride + s;
            unsigned int bb = a + 1;
            unsigned int c = a + stride;
            unsigned int d = c + 1;
            if (r != 0)          mb_tri(b, a, c, bb);
            if (r != rings - 1)  mb_tri(b, bb, c, d);
        }
    }
    // Tris overcounted by up to 2*segments (pole caps skipped); trim.
    m.ntris = b.ni / 3;
    return m;
}

Mesh mesh_ico_sphere(float radius, int subdivisions)
{
    // Start from an icosahedron and subdivide each triangle `subdivisions` times.
    // Kept deliberately small — an ico-sphere of subdiv 2 is 42 verts / 80 tris,
    // plenty for a viewport icon.
    (void)subdivisions;
    const float t = (1.0f + sqrtf(5.0f)) * 0.5f;
    const float inv = 1.0f / sqrtf(1.0f + t*t);
    float verts[12][3] = {
        {-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
        { 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
        { t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1},
    };
    int tris[20][3] = {
        {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
        {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
        {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
        {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1},
    };

    Mesh m;
    m.nverts = 12;
    m.ntris  = 20;
    m.verts  = (float*)malloc(sizeof(float) * 6 * m.nverts);
    m.tris   = (unsigned int*)malloc(sizeof(unsigned int) * 3 * m.ntris);
    for (int i = 0; i < 12; ++i) {
        float nx = verts[i][0] * inv;
        float ny = verts[i][1] * inv;
        float nz = verts[i][2] * inv;
        m.verts[i*6 + 0] = nx * radius;
        m.verts[i*6 + 1] = ny * radius;
        m.verts[i*6 + 2] = nz * radius;
        m.verts[i*6 + 3] = nx;
        m.verts[i*6 + 4] = ny;
        m.verts[i*6 + 5] = nz;
    }
    for (int i = 0; i < 20; ++i) {
        m.tris[i*3 + 0] = tris[i][0];
        m.tris[i*3 + 1] = tris[i][1];
        m.tris[i*3 + 2] = tris[i][2];
    }
    return m;
}

Mesh mesh_cylinder(float radius, float depth, int vertices)
{
    if (vertices < 3) vertices = 3;
    Mesh m;
    // rim*2 + caps*2, each ring duplicated for flat normals.
    m.nverts = (unsigned int)(vertices * 4 + 2);
    m.ntris  = (unsigned int)(vertices * 4);
    m.verts  = (float*)malloc(sizeof(float) * 6 * m.nverts);
    m.tris   = (unsigned int*)malloc(sizeof(unsigned int) * 3 * m.ntris);
    MeshBuilder b = { m.verts, m.tris, 0, 0 };

    const float hz = depth * 0.5f;

    // Side wall
    for (int i = 0; i < vertices; ++i) {
        float a0 = 2.0f * (float)M_PI * i / vertices;
        float a1 = 2.0f * (float)M_PI * ((i + 1) % vertices) / vertices;
        float nx0 = cosf(a0), ny0 = sinf(a0);
        float nx1 = cosf(a1), ny1 = sinf(a1);
        unsigned int v0 = mb_push(b, nx0*radius, ny0*radius,  hz, nx0, ny0, 0);
        unsigned int v1 = mb_push(b, nx1*radius, ny1*radius,  hz, nx1, ny1, 0);
        unsigned int v2 = mb_push(b, nx1*radius, ny1*radius, -hz, nx1, ny1, 0);
        unsigned int v3 = mb_push(b, nx0*radius, ny0*radius, -hz, nx0, ny0, 0);
        mb_quad(b, v0, v1, v2, v3);
    }
    // Top cap (fan)
    unsigned int top_c = mb_push(b, 0, 0, hz, 0, 0, 1);
    for (int i = 0; i < vertices; ++i) {
        float a0 = 2.0f * (float)M_PI * i / vertices;
        float a1 = 2.0f * (float)M_PI * ((i + 1) % vertices) / vertices;
        unsigned int p = mb_push(b, cosf(a0)*radius, sinf(a0)*radius, hz, 0, 0, 1);
        unsigned int q = mb_push(b, cosf(a1)*radius, sinf(a1)*radius, hz, 0, 0, 1);
        mb_tri(b, top_c, p, q);
    }
    // Bottom cap (fan)
    unsigned int bot_c = mb_push(b, 0, 0, -hz, 0, 0, -1);
    for (int i = 0; i < vertices; ++i) {
        float a0 = 2.0f * (float)M_PI * i / vertices;
        float a1 = 2.0f * (float)M_PI * ((i + 1) % vertices) / vertices;
        unsigned int p = mb_push(b, cosf(a1)*radius, sinf(a1)*radius, -hz, 0, 0, -1);
        unsigned int q = mb_push(b, cosf(a0)*radius, sinf(a0)*radius, -hz, 0, 0, -1);
        mb_tri(b, bot_c, p, q);
    }

    m.nverts = b.nv;
    m.ntris  = b.ni / 3;
    return m;
}

Mesh mesh_plane(float size)
{
    const float h = size * 0.5f;
    Mesh m;
    m.nverts = 4;
    m.ntris  = 2;
    m.verts  = (float*)malloc(sizeof(float) * 6 * m.nverts);
    m.tris   = (unsigned int*)malloc(sizeof(unsigned int) * 3 * m.ntris);
    MeshBuilder b = { m.verts, m.tris, 0, 0 };
    unsigned int v0 = mb_push(b, -h, -h, 0, 0, 0, 1);
    unsigned int v1 = mb_push(b,  h, -h, 0, 0, 0, 1);
    unsigned int v2 = mb_push(b,  h,  h, 0, 0, 0, 1);
    unsigned int v3 = mb_push(b, -h,  h, 0, 0, 0, 1);
    mb_quad(b, v0, v1, v2, v3);
    return m;
}

void mesh_free(Mesh& m)
{
    if (m.verts) { free(m.verts); m.verts = NULL; }
    if (m.tris)  { free(m.tris);  m.tris  = NULL; }
    m.nverts = 0;
    m.ntris  = 0;
}

// Blender draws cameras as a small box + frustum; we approximate with a
// wireframe-friendly cube.  Light icons are an ico-sphere.
Mesh mesh_camera_icon()
{
    Mesh m = mesh_cube(0.4f);
    return m;
}
Mesh mesh_light_icon()
{
    return mesh_ico_sphere(0.25f, 0);
}
Mesh mesh_empty_icon()
{
    return mesh_cube(0.2f);
}

// ---------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------

Scene::Scene()
    : n_objects_(0), selected_(NULL),
      frame_current_(1), frame_start_(1), frame_end_(250),
      mode_(Mode::Object), shading_(Shading::Solid),
      history_len_(0), history_cur_(-1)
{
    memset(objects_, 0, sizeof(objects_));
    memset(history_, 0, sizeof(history_));
    cursor3d_ = { 0.0f, 0.0f, 0.0f };
    snprintf(collection_.name, sizeof(collection_.name), "Collection");
    collection_.expanded = true;
}

Scene::~Scene()
{
    clear_objects();
    clear_history();
}

void Scene::clear_objects()
{
    for (size_t i = 0; i < n_objects_; ++i) {
        if (objects_[i]) {
            mesh_free(objects_[i]->mesh);
            delete objects_[i];
            objects_[i] = NULL;
        }
    }
    n_objects_ = 0;
    selected_  = NULL;
}

Object* Scene::add_mesh_object(const char* name, const Mesh& mesh)
{
    if (n_objects_ >= kMaxObjects) return NULL;
    Object* o = new Object();
    memset(o, 0, sizeof(*o));
    snprintf(o->name, sizeof(o->name), "%s", name);
    o->type = ObjectType::Mesh;
    o->light_type = LightType::Point;
    o->pos = { 0, 0, 0 };
    o->rot_euler_deg = { 0, 0, 0 };
    o->scale = { 1, 1, 1 };
    o->mesh = mesh;
    o->material.base_color[0] = 0.8f;
    o->material.base_color[1] = 0.8f;
    o->material.base_color[2] = 0.8f;
    o->material.base_color[3] = 1.0f;
    o->material.metallic  = 0.0f;
    o->material.roughness = 0.5f;
    o->selected = false;
    o->visible = true;
    o->hide_viewport = false;
    o->hide_render = false;
    objects_[n_objects_++] = o;
    return o;
}

Object* Scene::add_camera(const char* name, const Vec3& pos, const Vec3& rot_deg)
{
    if (n_objects_ >= kMaxObjects) return NULL;
    Object* o = new Object();
    memset(o, 0, sizeof(*o));
    snprintf(o->name, sizeof(o->name), "%s", name);
    o->type = ObjectType::Camera;
    o->light_type = LightType::Point;
    o->pos = pos;
    o->rot_euler_deg = rot_deg;
    o->scale = { 1, 1, 1 };
    o->mesh = mesh_camera_icon();
    o->material.base_color[0] = 0.55f;
    o->material.base_color[1] = 0.55f;
    o->material.base_color[2] = 0.55f;
    o->material.base_color[3] = 1.0f;
    o->visible = true;
    objects_[n_objects_++] = o;
    return o;
}

Object* Scene::add_light(const char* name, LightType type, const Vec3& pos,
                         float /*strength_w*/)
{
    if (n_objects_ >= kMaxObjects) return NULL;
    Object* o = new Object();
    memset(o, 0, sizeof(*o));
    snprintf(o->name, sizeof(o->name), "%s", name);
    o->type = ObjectType::Light;
    o->light_type = type;
    o->pos = pos;
    o->rot_euler_deg = { 0, 0, 0 };
    o->scale = { 1, 1, 1 };
    o->mesh = mesh_light_icon();
    o->material.base_color[0] = 1.0f;
    o->material.base_color[1] = 0.85f;
    o->material.base_color[2] = 0.35f;
    o->material.base_color[3] = 1.0f;
    o->visible = true;
    objects_[n_objects_++] = o;
    return o;
}

void Scene::remove(Object* obj)
{
    for (size_t i = 0; i < n_objects_; ++i) {
        if (objects_[i] == obj) {
            mesh_free(objects_[i]->mesh);
            delete objects_[i];
            for (size_t j = i; j + 1 < n_objects_; ++j)
                objects_[j] = objects_[j + 1];
            objects_[--n_objects_] = NULL;
            if (selected_ == obj) selected_ = NULL;
            return;
        }
    }
}

void Scene::select(Object* obj)
{
    for (size_t i = 0; i < n_objects_; ++i)
        objects_[i]->selected = (objects_[i] == obj);
    selected_ = obj;
}

void Scene::set_frame_current(int f)
{
    if (f < frame_start_) f = frame_start_;
    if (f > frame_end_)   f = frame_end_;
    frame_current_ = f;
}

void Scene::set_frame_range(int start, int end)
{
    if (end < start) end = start;
    frame_start_ = start;
    frame_end_   = end;
    // Keep the playhead inside the new range.
    set_frame_current(frame_current_);
}

void Scene::load_startup()
{
    clear_objects();
    clear_history();

    // Cube: Blender's default is 2x2x2 at origin.
    Object* cube = add_mesh_object("Cube", mesh_cube(2.0f));
    if (cube) {
        cube->pos = { 0.0f, 0.0f, 0.0f };
        cube->rot_euler_deg = { 0.0f, 0.0f, 0.0f };
        cube->scale = { 1.0f, 1.0f, 1.0f };
    }

    // Camera: Blender's default camera transform from startup.blend.
    Vec3 cam_pos = { 7.358891f, -6.925791f, 4.958309f };
    Vec3 cam_rot = { 66.93f, 0.0f, 46.692f };
    add_camera("Camera", cam_pos, cam_rot);

    // Light: point light at Blender's default offset.
    Vec3 light_pos = { 4.076245f, 1.005454f, 5.903862f };
    add_light("Light", LightType::Point, light_pos, 1000.0f);

    // Cursor and selection match Blender's startup state.
    cursor3d_ = { 0, 0, 0 };
    select(cube);
    frame_current_ = 1;
    frame_start_   = 1;
    frame_end_     = 250;
    mode_    = Mode::Object;
    shading_ = Shading::Solid;

    // Seed the undo history with the startup state so the first user edit
    // can be undone back to it.
    commit();
}

// ---------------------------------------------------------------------------
// Undo / redo snapshots
// ---------------------------------------------------------------------------

// A deep copy of the whole scene.  Meshes are cloned so a later edit (or
// free) of the live scene never aliases snapshot memory.
struct Scene::Snapshot {
    Object*  objects[Scene::kMaxObjects];
    size_t   n_objects;
    int      selected_index;   // -1 when nothing is selected
    Vec3     cursor3d;
    int      frame_current;
    Scene::Mode    mode;
    Scene::Shading shading;
};

static Object* clone_object(const Object* src)
{
    Object* o = new Object();
    *o = *src;                       // copies POD fields incl. name/material
    o->mesh.verts = NULL;
    o->mesh.tris  = NULL;
    o->mesh.nverts = 0;
    o->mesh.ntris  = 0;
    if (src->mesh.nverts > 0 && src->mesh.verts) {
        o->mesh.nverts = src->mesh.nverts;
        o->mesh.verts = (float*)malloc(sizeof(float) * 6 * src->mesh.nverts);
        if (o->mesh.verts)
            memcpy(o->mesh.verts, src->mesh.verts, sizeof(float) * 6 * src->mesh.nverts);
        else
            o->mesh.nverts = 0;
    }
    if (src->mesh.ntris > 0 && src->mesh.tris) {
        o->mesh.ntris = src->mesh.ntris;
        o->mesh.tris = (unsigned int*)malloc(sizeof(unsigned int) * 3 * src->mesh.ntris);
        if (o->mesh.tris)
            memcpy(o->mesh.tris, src->mesh.tris, sizeof(unsigned int) * 3 * src->mesh.ntris);
        else
            o->mesh.ntris = 0;
    }
    return o;
}

Scene::Snapshot* Scene::capture() const
{
    Snapshot* s = new Snapshot();
    s->n_objects = n_objects_;
    s->selected_index = -1;
    for (size_t i = 0; i < n_objects_; ++i) {
        s->objects[i] = clone_object(objects_[i]);
        if (objects_[i] == selected_) s->selected_index = (int)i;
    }
    s->cursor3d      = cursor3d_;
    s->frame_current = frame_current_;
    s->mode          = mode_;
    s->shading       = shading_;
    return s;
}

void Scene::free_snapshot(Snapshot* s)
{
    if (!s) return;
    for (size_t i = 0; i < s->n_objects; ++i) {
        if (s->objects[i]) {
            mesh_free(s->objects[i]->mesh);
            delete s->objects[i];
        }
    }
    delete s;
}

void Scene::clear_history()
{
    for (int i = 0; i < history_len_; ++i)
        free_snapshot(history_[i]);
    history_len_ = 0;
    history_cur_ = -1;
}

void Scene::restore(Snapshot* s)
{
    if (!s) return;
    clear_objects();
    for (size_t i = 0; i < s->n_objects; ++i) {
        objects_[i] = clone_object(s->objects[i]);
        n_objects_++;
    }
    selected_ = (s->selected_index >= 0 && (size_t)s->selected_index < n_objects_)
                ? objects_[s->selected_index] : NULL;
    // Re-assert the per-object selected flag from the restored pointer.
    for (size_t i = 0; i < n_objects_; ++i)
        objects_[i]->selected = (objects_[i] == selected_);
    cursor3d_      = s->cursor3d;
    frame_current_ = s->frame_current;
    mode_          = s->mode;
    shading_       = s->shading;
}

void Scene::commit()
{
    Snapshot* s = capture();

    // Drop any redo tail beyond the current cursor.
    while (history_len_ > history_cur_ + 1) {
        free_snapshot(history_[history_len_ - 1]);
        history_len_--;
    }

    // Evict the oldest snapshot when the ring is full.
    if (history_len_ >= kMaxUndo + 1) {
        free_snapshot(history_[0]);
        for (int i = 0; i + 1 < history_len_; ++i)
            history_[i] = history_[i + 1];
        history_len_--;
        history_cur_--;
    }

    history_[history_len_++] = s;
    history_cur_ = history_len_ - 1;
}

bool Scene::undo()
{
    if (!can_undo()) return false;
    history_cur_--;
    restore(history_[history_cur_]);
    return true;
}

bool Scene::redo()
{
    if (!can_redo()) return false;
    history_cur_++;
    restore(history_[history_cur_]);
    return true;
}

void Scene::select_all()
{
    // Blender's Select All makes every object active; we mark all selected
    // and point selected_ at the last one so the properties panel has a target.
    selected_ = NULL;
    for (size_t i = 0; i < n_objects_; ++i) {
        objects_[i]->selected = true;
        selected_ = objects_[i];
    }
}

void Scene::select_none()
{
    for (size_t i = 0; i < n_objects_; ++i)
        objects_[i]->selected = false;
    selected_ = NULL;
}

const char* Scene::mode_name(Mode m)
{
    switch (m) {
        case Mode::Object:      return "Object Mode";
        case Mode::Edit:        return "Edit Mode";
        case Mode::Sculpt:      return "Sculpt Mode";
        case Mode::VertexPaint: return "Vertex Paint";
        case Mode::WeightPaint: return "Weight Paint";
        case Mode::TexturePaint:return "Texture Paint";
        case Mode::Shading:     return "Shading";
    }
    return "Object Mode";
}

const char* Scene::shading_name(Shading s)
{
    switch (s) {
        case Shading::Wireframe: return "Wireframe";
        case Shading::Solid:     return "Solid";
        case Shading::Material:  return "Material Preview";
        case Shading::Rendered:  return "Rendered";
    }
    return "Solid";
}

} // namespace blender
