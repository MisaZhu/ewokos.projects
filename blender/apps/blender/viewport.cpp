// Blender-style 3D viewport renderer implementation.
#include "viewport.h"
#include "theme.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace blender {

namespace {

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

// Gouraud-lit mesh shader.  mat4 uniforms are column-major (transpose=FALSE),
// so "uMVP * vec4(p,1)" reproduces Blender's mult_m4_v4 exactly.
const char* kShadedVS =
    "attribute vec3 aPosition;\n"
    "attribute vec3 aNormal;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat4 uNormalMat;\n"
    "uniform vec3 uColor;\n"
    "uniform vec4 uLightEye;\n"
    "uniform vec4 uShade;\n"        // x=ambient y=diffuse z=specular w=shininess
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    vec3 ne = normalize((uNormalMat * vec4(aNormal, 0.0)).xyz);\n"
    "    vec3 L  = normalize(uLightEye.xyz);\n"
    "    float diff = max(dot(ne, L), 0.0);\n"
    "    vec3 col = clamp(uColor * (uShade.x + uShade.y * diff), 0.0, 1.0);\n"
    "    vColor = vec4(col, 1.0);\n"
    "    gl_Position = uMVP * vec4(aPosition, 1.0);\n"
    "}\n";

const char* kShadedFS =
    "precision mediump float;\n"
    "varying vec4 vColor;\n"
    "void main() { gl_FragColor = vColor; }\n";

// Unlit shader for grid / axes / cursor / gizmo / wireframe overlays.
const char* kUnlitVS =
    "attribute vec3 aPosition;\n"
    "uniform mat4 uMVP;\n"
    "void main() { gl_Position = uMVP * vec4(aPosition, 1.0); }\n";

const char* kUnlitFS =
    "precision mediump float;\n"
    "uniform vec4 uColor;\n"
    "void main() { gl_FragColor = uColor; }\n";

unsigned int compile_shader(GLenum type, const char* src)
{
    unsigned int sh = glCreateShader(type);
    if (sh == 0) return 0;
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        printf("blender viewport: shader compile failed: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

unsigned int link_program(const char* vs_src, const char* fs_src,
                          const char* attr0_name, const char* attr1_name)
{
    unsigned int vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    unsigned int fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) return 0;
    unsigned int p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, attr0_name);
    if (attr1_name) glBindAttribLocation(p, 1, attr1_name);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        printf("blender viewport: program link failed: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

void rgb_to_float(const unsigned char c[3], float out[3])
{
    out[0] = c[0] / 255.0f;
    out[1] = c[1] / 255.0f;
    out[2] = c[2] / 255.0f;
}

// Build a VBO from a float array.  Returns 0 on failure.
unsigned int make_vbo(const float* data, size_t bytes)
{
    unsigned int vbo = 0;
    glGenBuffers(1, &vbo);
    if (vbo == 0) return 0;
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizei)bytes, data, GL_STATIC_DRAW);
    return vbo;
}

Mat4 mat4_perspective(float fovy_deg, float aspect, float zn, float zf)
{
    Mat4 r = mat4_identity();
    float f = 1.0f / tanf(fovy_deg * 0.5f * (float)M_PI / 180.0f);
    memset(&r, 0, sizeof(r));
    r.m[0]  = f / aspect;
    r.m[5]  = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zf * zn) / (zn - zf);
    return r;
}

Mat4 mat4_ortho(float l, float rr, float b, float t, float n, float f)
{
    Mat4 m = mat4_identity();
    m.m[0]  = 2.0f / (rr - l);
    m.m[5]  = 2.0f / (t - b);
    m.m[10] = -2.0f / (f - n);
    m.m[12] = -(rr + l) / (rr - l);
    m.m[13] = -(t + b) / (t - b);
    m.m[14] = -(f + n) / (f - n);
    return m;
}

Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up)
{
    float f[3] = { center.x - eye.x, center.y - eye.y, center.z - eye.z };
    float fl = sqrtf(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    if (fl > 0.0f) { f[0] /= fl; f[1] /= fl; f[2] /= fl; }
    float s[3] = { f[1]*up.z - f[2]*up.y, f[2]*up.x - f[0]*up.z, f[0]*up.y - f[1]*up.x };
    float sl = sqrtf(s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
    if (sl > 0.0f) { s[0] /= sl; s[1] /= sl; s[2] /= sl; }
    float u[3] = { s[1]*f[2] - s[2]*f[1], s[2]*f[0] - s[0]*f[2], s[0]*f[1] - s[1]*f[0] };

    Mat4 m = mat4_identity();
    m.m[0]  = s[0]; m.m[1]  = u[0]; m.m[2]  = -f[0];
    m.m[4]  = s[1]; m.m[5]  = u[1]; m.m[6]  = -f[1];
    m.m[8]  = s[2]; m.m[9]  = u[2]; m.m[10] = -f[2];
    m.m[12] = -(s[0]*eye.x + s[1]*eye.y + s[2]*eye.z);
    m.m[13] = -(u[0]*eye.x + u[1]*eye.y + u[2]*eye.z);
    m.m[14] =  (f[0]*eye.x + f[1]*eye.y + f[2]*eye.z);
    return m;
}

Mat4 mat4_transpose(const Mat4& m)
{
    Mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[j*4 + i] = m.m[i*4 + j];
    return r;
}

// Cheap inverse for the affine view matrix (rotation + translation).
Mat4 mat4_inverse_affine(const Mat4& m)
{
    Mat4 r = mat4_identity();
    r.m[0] = m.m[0]; r.m[1] = m.m[4]; r.m[2]  = m.m[8];
    r.m[4] = m.m[1]; r.m[5] = m.m[5]; r.m[6]  = m.m[9];
    r.m[8] = m.m[2]; r.m[9] = m.m[6]; r.m[10] = m.m[10];
    r.m[12] = -(m.m[0]*m.m[12] + m.m[1]*m.m[13] + m.m[2]*m.m[14]);
    r.m[13] = -(m.m[4]*m.m[12] + m.m[5]*m.m[13] + m.m[6]*m.m[14]);
    r.m[14] = -(m.m[8]*m.m[12] + m.m[9]*m.m[13] + m.m[10]*m.m[14]);
    return r;
}

// Bind the interleaved [pos3 normal3] attribute layout used by mesh VBOs.
// Attribute 0 = position, attribute 1 = normal.  Currently unused because the
// viewport expands indexed meshes into triangle soup on the CPU and binds
// attributes directly from the expanded buffer; kept as a helper for future
// VBO-cached mesh draws.
__attribute__((unused))
static void bind_pos_normal(unsigned int vbo)
{
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
}

void bind_pos_only(unsigned int vbo)
{
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

// ---------------------------------------------------------------------------
// Geometry builders — each returns a float array and writes the vertex count.
// Positions only (3 floats/vert) for unlit lines.
// ---------------------------------------------------------------------------

// Blender's viewport grid: 1-unit minor cells, 10-unit major cells, extending
// +/- kGridExtent on X/Y at Z=0.  The +X axis line is red and the +Y axis
// line is green, matching Blender's overlay defaults.
float* build_grid_lines(int* out_count)
{
    const int extent = 25;      // -25 .. +25 (51 lines each way)
    const int total  = (extent * 2 + 1) * 2 * 2;  // 2 verts per line, 2 axes
    float* v = (float*)malloc(sizeof(float) * 3 * total);
    int i = 0;
    for (int k = -extent; k <= extent; ++k) {
        // Line along X (constant Y = k)
        v[i++] = (float)-extent; v[i++] = (float)k; v[i++] = 0.0f;
        v[i++] = (float) extent; v[i++] = (float)k; v[i++] = 0.0f;
        // Line along Y (constant X = k)
        v[i++] = (float)k; v[i++] = (float)-extent; v[i++] = 0.0f;
        v[i++] = (float)k; v[i++] = (float) extent; v[i++] = 0.0f;
    }
    *out_count = i / 3;
    return v;
}

// Long coloured world axes: +X red, +Y green, +Z blue, drawn from the origin
// out to a fixed length.  3 floats per vertex position + 3 floats per vertex
// colour, packed as [pos, col] pairs and split at draw time.
struct AxisLine { float p0[3], p1[3], c[3]; };

// Blender's 3D cursor is a red/white circle split into quadrants.  We render
// it as an XZ-plane ring (radius 0.25) with alternating red/white segments.
float* build_cursor_ring(int segments, float radius, int* out_count)
{
    float* v = (float*)malloc(sizeof(float) * 3 * (segments * 2));
    int i = 0;
    for (int s = 0; s < segments; ++s) {
        float a0 = 2.0f * (float)M_PI * s / segments;
        float a1 = 2.0f * (float)M_PI * (s + 1) / segments;
        v[i++] = cosf(a0) * radius; v[i++] = sinf(a0) * radius; v[i++] = 0.0f;
        v[i++] = cosf(a1) * radius; v[i++] = sinf(a1) * radius; v[i++] = 0.0f;
    }
    *out_count = i / 3;
    return v;
}

// Small axis gizmo drawn in a corner of the viewport.  Three short lines
// from the origin along +X/+Y/+Z, colour-coded.
float* build_gizmo_lines(int* out_count)
{
    const float L = 1.0f;
    float* v = (float*)malloc(sizeof(float) * 3 * 6);
    int i = 0;
    v[i++] = 0; v[i++] = 0; v[i++] = 0;   v[i++] = L; v[i++] = 0; v[i++] = 0;   // +X
    v[i++] = 0; v[i++] = 0; v[i++] = 0;   v[i++] = 0; v[i++] = L; v[i++] = 0;   // +Y
    v[i++] = 0; v[i++] = 0; v[i++] = 0;   v[i++] = 0; v[i++] = 0; v[i++] = L;   // +Z
    *out_count = i / 3;
    return v;
}

// Wireframe cube edges (12 line segments, 24 verts) used to draw the
// selection outline around any selected mesh / camera / light icon.
float* build_wire_cube_edges(float half_extent, int* out_count)
{
    const float h = half_extent;
    // 8 corners
    const float p[8][3] = {
        {-h,-h,-h}, { h,-h,-h}, { h, h,-h}, {-h, h,-h},
        {-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h},
    };
    const int e[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7},
    };
    float* v = (float*)malloc(sizeof(float) * 3 * 24);
    int i = 0;
    for (int k = 0; k < 12; ++k) {
        v[i++] = p[e[k][0]][0]; v[i++] = p[e[k][0]][1]; v[i++] = p[e[k][0]][2];
        v[i++] = p[e[k][1]][0]; v[i++] = p[e[k][1]][1]; v[i++] = p[e[k][1]][2];
    }
    *out_count = 24;
    return v;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Viewport
// ---------------------------------------------------------------------------

Viewport::Viewport()
    : origin_x_(0), origin_y_(0),
      width_(640), height_(480), initialised_(false), gpu_active_(false),
      distance_(10.0f), azimuth_deg_(45.0f), elevation_deg_(25.0f),
      fov_deg_(39.598f),      // Blender's default 50mm-equivalent FOV
      near_(0.1f), far_(1000.0f),
      navigating_(false), ortho_(false),
      prog_shaded_(0), prog_unlit_(0),
      vbo_grid_(0), vbo_axes_(0), vbo_cursor_(0), vbo_gizmo_(0), vbo_wire_cube_(0),
      grid_vertex_count_(0), axes_vertex_count_(0), cursor_vertex_count_(0),
      gizmo_vertex_count_(0), wire_cube_vertex_count_(0),
      loc_sh_mvp_(-1), loc_sh_normal_(-1), loc_sh_color_(-1),
      loc_sh_light_(-1), loc_sh_shade_(-1),
      loc_un_mvp_(-1), loc_un_color_(-1)
{
    target_ = { 0, 0, 0 };
}

Viewport::~Viewport()
{
    shutdown();
}

bool Viewport::init(int width, int height)
{
    width_  = width;
    height_ = height;

    prog_shaded_ = link_program(kShadedVS, kShadedFS, "aPosition", "aNormal");
    prog_unlit_  = link_program(kUnlitVS,  kUnlitFS,  "aPosition", NULL);
    if (!prog_shaded_ || !prog_unlit_) return false;

    loc_sh_mvp_    = glGetUniformLocation(prog_shaded_, "uMVP");
    loc_sh_normal_ = glGetUniformLocation(prog_shaded_, "uNormalMat");
    loc_sh_color_  = glGetUniformLocation(prog_shaded_, "uColor");
    loc_sh_light_  = glGetUniformLocation(prog_shaded_, "uLightEye");
    loc_sh_shade_  = glGetUniformLocation(prog_shaded_, "uShade");
    loc_un_mvp_    = glGetUniformLocation(prog_unlit_, "uMVP");
    loc_un_color_  = glGetUniformLocation(prog_unlit_, "uColor");

    build_grid();
    build_axes();
    build_cursor();
    build_gizmo();
    build_wire_cube();

    initialised_ = true;
    return true;
}

void Viewport::shutdown()
{
    if (!initialised_) return;
    if (vbo_grid_)      { glDeleteBuffers(1, &vbo_grid_);      vbo_grid_ = 0; }
    if (vbo_axes_)      { glDeleteBuffers(1, &vbo_axes_);      vbo_axes_ = 0; }
    if (vbo_cursor_)    { glDeleteBuffers(1, &vbo_cursor_);    vbo_cursor_ = 0; }
    if (vbo_gizmo_)     { glDeleteBuffers(1, &vbo_gizmo_);     vbo_gizmo_ = 0; }
    if (vbo_wire_cube_) { glDeleteBuffers(1, &vbo_wire_cube_); vbo_wire_cube_ = 0; }
    if (prog_shaded_)   { glDeleteProgram(prog_shaded_);       prog_shaded_ = 0; }
    if (prog_unlit_)    { glDeleteProgram(prog_unlit_);        prog_unlit_ = 0; }
    initialised_ = false;
}

void Viewport::resize(int gl_x, int gl_y, int width, int height)
{
    origin_x_ = gl_x;
    origin_y_ = gl_y;
    width_  = width  > 0 ? width  : 1;
    height_ = height > 0 ? height : 1;
}

void Viewport::build_grid()
{
    int n = 0;
    float* v = build_grid_lines(&n);
    vbo_grid_ = make_vbo(v, sizeof(float) * 3 * n);
    grid_vertex_count_ = n;
    free(v);
}

void Viewport::build_axes()
{
    // Long world axes drawn from the origin along +X, +Y, +Z.  Same layout
    // as the grid (positions only).
    const float L = 50.0f;
    float v[18] = {
        -L, 0, 0,   L, 0, 0,
         0,-L, 0,   0, L, 0,
         0, 0,-L,   0, 0, L,
    };
    vbo_axes_ = make_vbo(v, sizeof(v));
    axes_vertex_count_ = 6;
}

void Viewport::build_cursor()
{
    int n = 0;
    float* v = build_cursor_ring(32, 0.25f, &n);
    vbo_cursor_ = make_vbo(v, sizeof(float) * 3 * n);
    cursor_vertex_count_ = n;
    free(v);
}

void Viewport::build_gizmo()
{
    int n = 0;
    float* v = build_gizmo_lines(&n);
    vbo_gizmo_ = make_vbo(v, sizeof(float) * 3 * n);
    gizmo_vertex_count_ = n;
    free(v);
}

void Viewport::build_wire_cube()
{
    int n = 0;
    float* v = build_wire_cube_edges(1.0f, &n);
    vbo_wire_cube_ = make_vbo(v, sizeof(float) * 3 * n);
    wire_cube_vertex_count_ = n;
    free(v);
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

void Viewport::compute_view_matrix(Mat4& out_view) const
{
    float az = azimuth_deg_   * (float)M_PI / 180.0f;
    float el = elevation_deg_ * (float)M_PI / 180.0f;
    float ce = cosf(el), se = sinf(el);
    Vec3 eye;
    eye.x = target_.x + distance_ * ce * cosf(az);
    eye.y = target_.y + distance_ * ce * sinf(az);
    eye.z = target_.z + distance_ * se;
    Vec3 up = { 0.0f, 0.0f, 1.0f };
    // Blender's default up is +Z; when looking straight down the pole we tilt
    // the up vector slightly to keep the view matrix well-defined.
    if (fabsf(el) > (float)M_PI * 0.49f) {
        up.x = 0.001f;
    }
    out_view = mat4_look_at(eye, target_, up);
}

void Viewport::compute_proj_matrix(Mat4& out_proj, float aspect) const
{
    if (ortho_) {
        // Blender's ortho scale is derived from the perspective distance so
        // numpad views preserve apparent size when switching.
        float half_h = distance_ * tanf(fov_deg_ * 0.5f * (float)M_PI / 180.0f);
        float half_w = half_h * aspect;
        out_proj = mat4_ortho(-half_w, half_w, -half_h, half_h, near_, far_);
    } else {
        out_proj = mat4_perspective(fov_deg_, aspect, near_, far_);
    }
}

void Viewport::orbit(float dx_pixels, float dy_pixels)
{
    // Blender: full window width = 360 degrees of azimuth.
    const float kAzScale  = 360.0f / (float)(width_  > 0 ? width_  : 1);
    const float kElScale  = 180.0f / (float)(height_ > 0 ? height_ : 1);
    azimuth_deg_   -= dx_pixels * kAzScale;
    elevation_deg_ += dy_pixels * kElScale;
    if (azimuth_deg_ >  180.0f) azimuth_deg_ -= 360.0f;
    if (azimuth_deg_ < -180.0f) azimuth_deg_ += 360.0f;
    if (elevation_deg_ >  89.5f) elevation_deg_ =  89.5f;
    if (elevation_deg_ < -89.5f) elevation_deg_ = -89.5f;
}

void Viewport::pan(float dx_pixels, float dy_pixels)
{
    // Pan in the camera's local XY plane, scaled by distance so the motion
    // feels natural at every zoom level.
    float az = azimuth_deg_   * (float)M_PI / 180.0f;
    float el = elevation_deg_ * (float)M_PI / 180.0f;
    Vec3 fwd = {
        cosf(el) * cosf(az),
        cosf(el) * sinf(az),
        sinf(el),
    };
    Vec3 world_up = { 0, 0, 1 };
    Vec3 right = {
        fwd.y * world_up.z - fwd.z * world_up.y,
        fwd.z * world_up.x - fwd.x * world_up.z,
        fwd.x * world_up.y - fwd.y * world_up.x,
    };
    float rl = sqrtf(right.x*right.x + right.y*right.y + right.z*right.z);
    if (rl > 0) { right.x /= rl; right.y /= rl; right.z /= rl; }
    Vec3 up = {
        right.y * fwd.z - right.z * fwd.y,
        right.z * fwd.x - right.x * fwd.z,
        right.x * fwd.y - right.y * fwd.x,
    };

    float scale = 2.0f * distance_ * tanf(fov_deg_ * 0.5f * (float)M_PI / 180.0f)
                  / (float)(height_ > 0 ? height_ : 1);
    float dx = -dx_pixels * scale;
    float dy =  dy_pixels * scale;
    target_.x += right.x * dx + up.x * dy;
    target_.y += right.y * dx + up.y * dy;
    target_.z += right.z * dx + up.z * dy;
}

void Viewport::zoom(float delta)
{
    // Blender: wheel up = zoom in (distance shrinks).  Log scale so repeated
    // wheel steps feel uniform.
    float factor = powf(1.1f, -delta);
    distance_ *= factor;
    if (distance_ < 0.05f)  distance_ = 0.05f;
    if (distance_ > 500.0f) distance_ = 500.0f;
}

void Viewport::set_view_preset(ViewPreset v)
{
    switch (v) {
        case ViewPreset::Front:  azimuth_deg_ =   0.0f; elevation_deg_ =  0.0f; ortho_ = true;  break;
        case ViewPreset::Back:   azimuth_deg_ = 180.0f; elevation_deg_ =  0.0f; ortho_ = true;  break;
        case ViewPreset::Left:   azimuth_deg_ = -90.0f; elevation_deg_ =  0.0f; ortho_ = true;  break;
        case ViewPreset::Right:  azimuth_deg_ =  90.0f; elevation_deg_ =  0.0f; ortho_ = true;  break;
        case ViewPreset::Top:    azimuth_deg_ =   0.0f; elevation_deg_ = 89.5f; ortho_ = true;  break;
        case ViewPreset::Bottom: azimuth_deg_ =   0.0f; elevation_deg_ =-89.5f; ortho_ = true;  break;
        case ViewPreset::User:   azimuth_deg_ =  45.0f; elevation_deg_ = 25.0f; ortho_ = false; break;
        case ViewPreset::Camera: /* leave orbit alone; UI toggles "camera view" */ break;
    }
}

void Viewport::frame_selected(const Scene& scene)
{
    const Object* sel = scene.selected();
    if (sel) {
        target_ = sel->pos;
        distance_ = 6.0f;
    } else {
        target_ = { 0, 0, 0 };
        distance_ = 10.0f;
    }
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void Viewport::render(const Scene& scene, unsigned int overlays)
{
    if (!initialised_) return;

    float vp_bg[3];
    rgb_to_float(theme::palette.viewport, vp_bg);

    // Clear exactly the panel rectangle: an origin-less (0,0,w,h) rect here
    // would leave the L-shaped gap between it and the panel unwritten, which
    // shows stale back-buffer garbage after a resize realloc.
    glViewport(origin_x_, origin_y_, width_, height_);
    glScissor(origin_x_, origin_y_, width_, height_);
    glClearColor(vp_bg[0], vp_bg[1], vp_bg[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    Mat4 view, proj, vp;
    float aspect = (float)width_ / (float)(height_ > 0 ? height_ : 1);
    compute_view_matrix(view);
    compute_proj_matrix(proj, aspect);
    vp = mat4_mul(proj, view);

    // --- World-space overlays under the meshes -----------------------------
    if (overlays & OVERLAY_GRID) draw_grid(vp);
    if (overlays & OVERLAY_AXES) draw_axes(vp);

    // --- Scene objects (solid shading) ------------------------------------
    glUseProgram(prog_shaded_);
    // Blender's default sun-ish light direction in eye space.
    glUniform4f(loc_sh_light_, 5.0f, 5.0f, 10.0f, 0.0f);
    // ambient, diffuse, specular, unused shininess
    glUniform4f(loc_sh_shade_, 0.35f, 0.75f, 0.0f, 0.0f);

    // Matrices needed by the shaded program:
    Mat4 view_inv_t;
    {
        Mat4 view_inv = mat4_inverse_affine(view);
        view_inv_t = mat4_transpose(view_inv);
    }

    for (size_t i = 0; i < scene.object_count(); ++i) {
        const Object* obj = scene.object_at(i);
        if (!obj || !obj->visible || obj->hide_viewport) continue;
        if (scene.shading() == Scene::Shading::Wireframe) {
            draw_mesh_object(vp, *obj, true);
        } else {
            draw_mesh_object(vp, *obj, false);
        }
    }
    (void)view_inv_t;

    // --- Selection outlines + origins -------------------------------------
    if (overlays & OVERLAY_WIREFRAME) {
        for (size_t i = 0; i < scene.object_count(); ++i) {
            const Object* obj = scene.object_at(i);
            if (!obj || !obj->visible || !obj->selected) continue;
            draw_selection_outline(vp, *obj);
        }
    }
    if (overlays & OVERLAY_ORIGIN) {
        for (size_t i = 0; i < scene.object_count(); ++i) {
            const Object* obj = scene.object_at(i);
            if (!obj || !obj->visible) continue;
            draw_object_origin(vp, *obj);
        }
    }

    // --- 3D cursor ---------------------------------------------------------
    if (overlays & OVERLAY_CURSOR) draw_cursor(vp, scene.cursor3d());

    // --- Corner axis gizmo (drawn last, in a fixed viewport rectangle) ----
    if (overlays & OVERLAY_GIZMO) draw_gizmo();

    // --- GPU-active probe (PGL_G3D path on raspi5) ------------------------
    glUseProgram(prog_shaded_);
    gpu_active_ = (glGpuActiveEWOK() != 0);
}

void Viewport::draw_grid(const Mat4& vp)
{
    if (!vbo_grid_ || grid_vertex_count_ == 0) return;
    glUseProgram(prog_unlit_);
    glUniformMatrix4fv(loc_un_mvp_, 1, GL_FALSE, vp.m);
    float c[3]; rgb_to_float(theme::palette.grid, c);
    glUniform4f(loc_un_color_, c[0], c[1], c[2], 1.0f);

    glDisable(GL_DEPTH_TEST);   // Blender draws the grid under everything
    glDisable(GL_CULL_FACE);
    bind_pos_only(vbo_grid_);
    glDrawArrays(GL_LINES, 0, grid_vertex_count_);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisableVertexAttribArray(0);
}

void Viewport::draw_axes(const Mat4& vp)
{
    if (!vbo_axes_ || axes_vertex_count_ == 0) return;
    glUseProgram(prog_unlit_);
    glUniformMatrix4fv(loc_un_mvp_, 1, GL_FALSE, vp.m);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    bind_pos_only(vbo_axes_);

    float cx[3], cy[3], cz[3];
    rgb_to_float(theme::palette.axis_x, cx);
    rgb_to_float(theme::palette.axis_y, cy);
    rgb_to_float(theme::palette.axis_z, cz);

    glUniform4f(loc_un_color_, cx[0], cx[1], cx[2], 1.0f);
    glDrawArrays(GL_LINES, 0, 2);
    glUniform4f(loc_un_color_, cy[0], cy[1], cy[2], 1.0f);
    glDrawArrays(GL_LINES, 2, 2);
    glUniform4f(loc_un_color_, cz[0], cz[1], cz[2], 1.0f);
    glDrawArrays(GL_LINES, 4, 2);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisableVertexAttribArray(0);
}

void Viewport::draw_cursor(const Mat4& vp, const Vec3& world)
{
    if (!vbo_cursor_ || cursor_vertex_count_ == 0) return;
    glUseProgram(prog_unlit_);

    Mat4 model = mat4_translation(world.x, world.y, world.z);
    Mat4 mvp = mat4_mul(vp, model);
    glUniformMatrix4fv(loc_un_mvp_, 1, GL_FALSE, mvp.m);

    float c[3]; rgb_to_float(theme::palette.cursor3d, c);
    glUniform4f(loc_un_color_, c[0], c[1], c[2], 1.0f);

    glDisable(GL_CULL_FACE);
    glLineWidth(1.0f);
    bind_pos_only(vbo_cursor_);
    glDrawArrays(GL_LINES, 0, cursor_vertex_count_);
    glDisableVertexAttribArray(0);
    glEnable(GL_CULL_FACE);
}

void Viewport::draw_gizmo()
{
    if (!vbo_gizmo_ || gizmo_vertex_count_ == 0) return;

    // Fixed viewport rectangle in the top-right corner, 80x80 px.
    const int gx = origin_x_ + width_ - 90;
    const int gy = origin_y_ + height_ - 90;
    const int gs = 80;
    glViewport(gx, gy, gs, gs);
    glScissor(gx, gy, gs, gs);
    glEnable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_DEPTH_BUFFER_BIT);

    glUseProgram(prog_unlit_);

    // Build a small ortho projection and reuse the main view rotation only.
    Mat4 view_rot;
    compute_view_matrix(view_rot);
    // Strip the translation part so the gizmo stays centred.
    view_rot.m[12] = view_rot.m[13] = view_rot.m[14] = 0.0f;
    Mat4 proj = mat4_ortho(-1.4f, 1.4f, -1.4f, 1.4f, -10.0f, 10.0f);
    Mat4 mvp = mat4_mul(proj, view_rot);
    glUniformMatrix4fv(loc_un_mvp_, 1, GL_FALSE, mvp.m);

    float cx[3], cy[3], cz[3];
    rgb_to_float(theme::palette.axis_x, cx);
    rgb_to_float(theme::palette.axis_y, cy);
    rgb_to_float(theme::palette.axis_z, cz);

    glDisable(GL_CULL_FACE);
    glLineWidth(2.0f);
    bind_pos_only(vbo_gizmo_);
    glUniform4f(loc_un_color_, cx[0], cx[1], cx[2], 1.0f);
    glDrawArrays(GL_LINES, 0, 2);
    glUniform4f(loc_un_color_, cy[0], cy[1], cy[2], 1.0f);
    glDrawArrays(GL_LINES, 2, 2);
    glUniform4f(loc_un_color_, cz[0], cz[1], cz[2], 1.0f);
    glDrawArrays(GL_LINES, 4, 2);
    glDisableVertexAttribArray(0);
    glEnable(GL_CULL_FACE);

    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glViewport(origin_x_, origin_y_, width_, height_);
    glScissor(origin_x_, origin_y_, width_, height_);
}

void Viewport::draw_mesh_object(const Mat4& vp, const Object& obj, bool wireframe)
{
    if (!obj.mesh.verts || obj.mesh.nverts == 0) return;

    // Model = T * R * S
    Mat4 T = mat4_translation(obj.pos.x, obj.pos.y, obj.pos.z);
    Mat4 R = mat4_rotation_euler(obj.rot_euler_deg.x, obj.rot_euler_deg.y,
                                 obj.rot_euler_deg.z);
    Mat4 S = mat4_scale(obj.scale.x, obj.scale.y, obj.scale.z);
    Mat4 model = mat4_mul(T, mat4_mul(R, S));
    Mat4 mvp = mat4_mul(vp, model);

    if (wireframe) {
        // Wireframe: draw the mesh as GL_LINES using the triangle indices
        // (3 lines per triangle).  Falls back to a solid draw if we run out
        // of indices.  Blender's actual wireframe renderer uses edge dedup;
        // for this lightweight port we accept the overdraw.
        glUseProgram(prog_unlit_);
        glUniformMatrix4fv(loc_un_mvp_, 1, GL_FALSE, mvp.m);
        float c[3]; rgb_to_float(theme::palette.wire, c);
        glUniform4f(loc_un_color_, c[0], c[1], c[2], 1.0f);
        glDisable(GL_CULL_FACE);
        glLineWidth(1.0f);

        // Upload mesh on the fly as a static VBO would be overkill for
        // wireframe; use client-side vertex arrays via glBindBuffer(0).
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              (void*)obj.mesh.verts);
        glEnableVertexAttribArray(0);
        glDisableVertexAttribArray(1);

        // Without an element buffer we can't use glDrawElements; instead we
        // emit each triangle as GL_LINE_LOOP by expanding into a temp buffer.
        // For small meshes (<= a few thousand tris) this is fine.
        unsigned int ntris = obj.mesh.ntris;
        if (ntris > 0 && obj.mesh.tris) {
            // Draw each triangle as a line loop: 3 verts, restart via a fresh
            // draw call per triangle.  Slow but correct for a lightweight port.
            for (unsigned int t = 0; t < ntris; ++t) {
                unsigned int i0 = obj.mesh.tris[t*3 + 0];
                unsigned int i1 = obj.mesh.tris[t*3 + 1];
                unsigned int i2 = obj.mesh.tris[t*3 + 2];
                float tri[9];
                memcpy(tri + 0, obj.mesh.verts + i0 * 6, 3 * sizeof(float));
                memcpy(tri + 3, obj.mesh.verts + i1 * 6, 3 * sizeof(float));
                memcpy(tri + 6, obj.mesh.verts + i2 * 6, 3 * sizeof(float));
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                                      (void*)tri);
                glDrawArrays(GL_LINE_LOOP, 0, 3);
            }
        }
        glDisableVertexAttribArray(0);
        glEnable(GL_CULL_FACE);
        return;
    }

    glUseProgram(prog_shaded_);
    glUniformMatrix4fv(loc_sh_mvp_, 1, GL_FALSE, mvp.m);

    // Normal matrix = transpose(inverse(model)).  For uniform scale this is
    // just the rotation part; we compute it properly to handle non-uniform.
    Mat4 normal_mat = mat4_transpose(mat4_inverse_affine(model));
    glUniformMatrix4fv(loc_sh_normal_, 1, GL_FALSE, normal_mat.m);

    float bc[3]; rgb_to_float(theme::palette.header, bc);
    // Mesh objects use their material base colour; icon objects use the
    // palette's dim gray so they don't visually compete with the scene.
    if (obj.type == ObjectType::Mesh) {
        glUniform3f(loc_sh_color_, obj.material.base_color[0],
                    obj.material.base_color[1], obj.material.base_color[2]);
    } else if (obj.type == ObjectType::Camera) {
        float cam_c[3] = { 0.55f, 0.55f, 0.55f };
        glUniform3f(loc_sh_color_, cam_c[0], cam_c[1], cam_c[2]);
    } else if (obj.type == ObjectType::Light) {
        float light_c[3] = { 1.0f, 0.85f, 0.35f };
        glUniform3f(loc_sh_color_, light_c[0], light_c[1], light_c[2]);
    } else {
        glUniform3f(loc_sh_color_, bc[0], bc[1], bc[2]);
    }

    // Per-object VBO isn't cached; upload the interleaved data each draw.
    // For a lightweight port with a handful of objects this is acceptable.
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)obj.mesh.verts);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(obj.mesh.verts + 3));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    // Without an element buffer we have to expand indices into a temporary
    // triangle soup.  Build once per object per frame — acceptable for the
    // small primitive counts a lightweight port deals with.
    unsigned int ntris = obj.mesh.ntris;
    if (ntris > 0 && obj.mesh.tris) {
        size_t soup_floats = (size_t)ntris * 3 * 6;
        float* soup = (float*)malloc(sizeof(float) * soup_floats);
        if (soup) {
            for (unsigned int t = 0; t < ntris; ++t) {
                for (int k = 0; k < 3; ++k) {
                    unsigned int idx = obj.mesh.tris[t*3 + k];
                    memcpy(soup + (t*3 + k) * 6,
                           obj.mesh.verts + (size_t)idx * 6,
                           6 * sizeof(float));
                }
            }
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                                  (void*)soup);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                                  (void*)(soup + 3));
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(ntris * 3));
            free(soup);
        }
    }
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

void Viewport::draw_selection_outline(const Mat4& vp, const Object& obj)
{
    if (!vbo_wire_cube_) return;

    // Compute a world-space AABB-ish half extent from the object's mesh.
    float half = 1.0f;
    if (obj.mesh.verts && obj.mesh.nverts > 0) {
        float mn[3] = {  1e30f,  1e30f,  1e30f };
        float mx[3] = { -1e30f, -1e30f, -1e30f };
        for (unsigned int i = 0; i < obj.mesh.nverts; ++i) {
            const float* p = obj.mesh.verts + i * 6;
            for (int k = 0; k < 3; ++k) {
                float v = p[k] * obj.scale.x;  // non-uniform handled below
                if (k == 1) v = p[k] * obj.scale.y;
                if (k == 2) v = p[k] * obj.scale.z;
                if (v < mn[k]) mn[k] = v;
                if (v > mx[k]) mx[k] = v;
            }
        }
        half = 0.5f * fmaxf(fmaxf(mx[0]-mn[0], mx[1]-mn[1]), mx[2]-mn[2]);
        if (half < 0.05f) half = 0.05f;
    }

    Mat4 T = mat4_translation(obj.pos.x, obj.pos.y, obj.pos.z);
    Mat4 R = mat4_rotation_euler(obj.rot_euler_deg.x, obj.rot_euler_deg.y,
                                 obj.rot_euler_deg.z);
    Mat4 S = mat4_scale(half, half, half);
    Mat4 model = mat4_mul(T, mat4_mul(R, S));
    Mat4 mvp = mat4_mul(vp, model);

    glUseProgram(prog_unlit_);
    glUniformMatrix4fv(loc_un_mvp_, 1, GL_FALSE, mvp.m);
    float c[3]; rgb_to_float(theme::palette.select, c);
    glUniform4f(loc_un_color_, c[0], c[1], c[2], 1.0f);

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glLineWidth(1.0f);
    bind_pos_only(vbo_wire_cube_);
    glDrawArrays(GL_LINES, 0, wire_cube_vertex_count_);
    glDisableVertexAttribArray(0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

void Viewport::draw_object_origin(const Mat4& vp, const Object& obj)
{
    // Small cross at each object's origin (Blender's "Origins" overlay).
    // GLES2 has no glPointSize, so we draw three short line pairs instead.
    glUseProgram(prog_unlit_);
    Mat4 T = mat4_translation(obj.pos.x, obj.pos.y, obj.pos.z);
    Mat4 mvp = mat4_mul(vp, T);
    glUniformMatrix4fv(loc_un_mvp_, 1, GL_FALSE, mvp.m);
    float c[3] = { 0.9f, 0.6f, 0.2f };
    glUniform4f(loc_un_color_, c[0], c[1], c[2], 1.0f);

    // 6 verts = 3 line segments forming an RGB-ish cross along each axis.
    const float s = 0.08f;
    float cross[18] = {
        -s, 0, 0,   s, 0, 0,
         0,-s, 0,   0, s, 0,
         0, 0,-s,   0, 0, s,
    };
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                          (void*)cross);
    glEnableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glLineWidth(1.0f);
    glDrawArrays(GL_LINES, 0, 6);
    glDisableVertexAttribArray(0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

} // namespace blender
