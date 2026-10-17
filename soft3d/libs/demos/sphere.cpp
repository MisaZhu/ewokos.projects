/*
 * Sphereworld Color -- EGL + GLES2 (software rasterizer behind libGLESv2).
 *
 * Ported off SDL2 + portablegl.  The window/context now come from libEGL
 * (native window == xwin_t*), present is a plain eglSwapBuffers, and the two
 * lighting models are real GLSL ES 1.00 programs compiled by libGLESv2's CPU
 * GLSL interpreter:
 *
 *   - gouraud (default): per-vertex diffuse lighting written EXACTLY like the
 *     gears demo (uMVP / uNormalMat / uMaterial / uLightEye / uShade feeding a
 *     passthrough vColor fragment shader).  That shape is what the raspi5 V3D
 *     bridge recognises and offloads to the fixed lit-gouraud QPU kernel, so the
 *     CPU interpreter and the GPU produce identical pixels - GPU acceleration is
 *     preserved transparently.  The 50 scattered spheres are baked into ONE
 *     vertex buffer (their instance offsets are static) and drawn per instance
 *     with a shared MVP/material, which the bridge coalesces into a handful of
 *     GPU dispatches instead of 50.
 *
 *   - phong (toggle 'l'): per-pixel ADS lighting.  An arbitrary fragment shader
 *     never ran on the GPU under portablegl either (only the fixed-function FS
 *     did), so this stays on the CPU interpreter exactly as before; its normal
 *     matrix uniform is deliberately named uNMat (not uNormalMat) so the bridge
 *     does not mistake it for the gouraud kernel.
 *
 * Geometry, materials and the rotating sphere/torus animation are unchanged.
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>

#include <unistd.h>
#include <ewoksys/proc.h>
#include <ewoksys/keydef.h>
#include <ewoksys/kernel_tic.h>
#include <x/x.h>
#include <x/xwin.h>
#include <graph/graph.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEG_TO_RAD(x)   ((x) * 0.017453292519943296)

#define WIDTH 640
#define HEIGHT 480

#define FLOOR_SIZE 40
#define NUM_SPHERES 50

static int win_width = WIDTH;
static int win_height = HEIGHT;

static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;

/* ---- column-major mat4 helpers ---- */
static void mat4_multiply(float* result, const float* a, const float* b) {
    float temp[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            temp[j*4+i] = 0;
            for (int k = 0; k < 4; k++)
                temp[j*4+i] += a[k*4+i] * b[j*4+k];
        }
    memcpy(result, temp, sizeof(temp));
}

static void mat4_perspective(float* m, float fovy, float aspect, float nearv, float farv) {
    float f = 1.0f / tanf(fovy / 2.0f);
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (farv + nearv) / (nearv - farv);
    m[11] = -1.0f;
    m[14] = (2.0f * farv * nearv) / (nearv - farv);
}

static void mat4_identity(float* m) {
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_translation(float* m, float x, float y, float z) {
    mat4_identity(m);
    m[12] = x; m[13] = y; m[14] = z;
}

static void mat4_rotation(float* m, float angle, float x, float y, float z) {
    float c = cosf(angle), s = sinf(angle), t = 1.0f - c;
    float len = sqrtf(x*x + y*y + z*z);
    if (len > 0.0001f) { x /= len; y /= len; z /= len; }
    m[0] = t*x*x + c;     m[1] = t*x*y + s*z;   m[2] = t*x*z - s*y;   m[3] = 0;
    m[4] = t*x*y - s*z;   m[5] = t*y*y + c;     m[6] = t*y*z + s*x;   m[7] = 0;
    m[8] = t*x*z + s*y;   m[9] = t*y*z - s*x;   m[10] = t*z*z + c;    m[11] = 0;
    m[12] = 0;            m[13] = 0;            m[14] = 0;            m[15] = 1;
}

/* ---- camera ---- */
struct Camera {
    float pos[3];
    float rot[3];   /* degrees */

    Camera() { pos[0] = 0; pos[1] = 0.3f; pos[2] = 2.5f; rot[0] = rot[1] = rot[2] = 0; }

    void get_matrix(float* mat) {
        float rx = rot[0] * M_PI / 180.0f;
        float ry = rot[1] * M_PI / 180.0f;
        float rz = rot[2] * M_PI / 180.0f;
        float cx = cosf(rx), sx = sinf(rx);
        float cy = cosf(ry), sy = sinf(ry);
        float cz = cosf(rz), sz = sinf(rz);

        float rot_mat[16];
        rot_mat[0] = cy*cz; rot_mat[1] = cy*sz; rot_mat[2] = -sy; rot_mat[3] = 0;
        rot_mat[4] = sx*sy*cz - cx*sz; rot_mat[5] = sx*sy*sz + cx*cz; rot_mat[6] = sx*cy; rot_mat[7] = 0;
        rot_mat[8] = cx*sy*cz + sx*sz; rot_mat[9] = cx*sy*sz - sx*cz; rot_mat[10] = cx*cy; rot_mat[11] = 0;
        rot_mat[12] = 0; rot_mat[13] = 0; rot_mat[14] = 0; rot_mat[15] = 1;

        float trans_mat[16];
        mat4_identity(trans_mat);
        trans_mat[12] = -pos[0]; trans_mat[13] = -pos[1]; trans_mat[14] = -pos[2];

        mat4_multiply(mat, rot_mat, trans_mat);
    }

    void move_right(float dist) {
        float rad = rot[1] * M_PI / 180.0f;
        pos[0] += cosf(rad) * dist;
        pos[2] -= sinf(rad) * dist;
    }
    void move_up(float dist) { pos[1] += dist; }
    void move_forward(float dist) {
        float rad = rot[1] * M_PI / 180.0f;
        pos[0] -= sinf(rad) * dist;
        pos[2] -= cosf(rad) * dist;
    }
    void rotate_local_x(float deg) { rot[0] += deg; }
    void rotate_local_y(float deg) { rot[1] += deg; }
};

/* ---- geometry ---- */
struct V3 { float x, y, z; };
static inline V3 mkv3(float x, float y, float z) { V3 v = {x, y, z}; return v; }
static inline V3 v3_add(V3 a, V3 b) { return mkv3(a.x+b.x, a.y+b.y, a.z+b.z); }
static inline V3 v3_scale(V3 v, float s) { return mkv3(v.x*s, v.y*s, v.z*s); }
static inline V3 v3_cross(V3 a, V3 b) {
    return mkv3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x);
}
static inline V3 v3_norm(V3 v) {
    float l = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    if (l > 0.0001f) { v.x /= l; v.y /= l; v.z /= l; }
    return v;
}
static float randf_range(float lo, float hi) {
    return lo + (float)rand() / RAND_MAX * (hi - lo);
}

static void make_sphere(std::vector<V3>& verts, std::vector<int>& tris,
                        float radius, int segments, int rings) {
    for (int ring = 0; ring <= rings; ring++) {
        float phi = M_PI * ring / rings;
        for (int seg = 0; seg <= segments; seg++) {
            float theta = 2.0f * M_PI * seg / segments;
            float x = sinf(phi) * cosf(theta);
            float y = cosf(phi);
            float z = sinf(phi) * sinf(theta);
            verts.push_back(mkv3(x*radius, y*radius, z*radius));
        }
    }
    for (int ring = 0; ring < rings; ring++)
        for (int seg = 0; seg < segments; seg++) {
            int curr = ring * (segments + 1) + seg;
            int next = curr + segments + 1;
            tris.push_back(curr); tris.push_back(next); tris.push_back(curr + 1);
            tris.push_back(curr + 1); tris.push_back(next); tris.push_back(next + 1);
        }
}

static void make_torus(std::vector<V3>& verts, std::vector<int>& tris,
                       float major_radius, float minor_radius,
                       int major_segments, int minor_segments) {
    for (int i = 0; i < major_segments; i++) {
        float theta = 2.0f * M_PI * i / major_segments;
        float ct = cosf(theta), st = sinf(theta);
        for (int j = 0; j < minor_segments; j++) {
            float phi = 2.0f * M_PI * j / minor_segments;
            float cp = cosf(phi), sp = sinf(phi);
            float x = (major_radius + minor_radius * cp) * ct;
            float y = minor_radius * sp;
            float z = (major_radius + minor_radius * cp) * st;
            verts.push_back(mkv3(x, y, z));
        }
    }
    for (int i = 0; i < major_segments; i++) {
        int ni = (i + 1) % major_segments;
        for (int j = 0; j < minor_segments; j++) {
            int nj = (j + 1) % minor_segments;
            int curr = i * minor_segments + j;
            int right = i * minor_segments + nj;
            int down = ni * minor_segments + j;
            int diag = ni * minor_segments + nj;
            tris.push_back(curr); tris.push_back(down); tris.push_back(right);
            tris.push_back(right); tris.push_back(down); tris.push_back(diag);
        }
    }
}

static void compute_normals(const std::vector<V3>& verts, const std::vector<int>& tris,
                            std::vector<V3>& normals) {
    normals.assign(verts.size(), mkv3(0, 0, 0));
    std::vector<int> counts(verts.size(), 0);
    for (size_t i = 0; i < tris.size(); i += 3) {
        int i0 = tris[i], i1 = tris[i+1], i2 = tris[i+2];
        V3 v0 = verts[i0], v1 = verts[i1], v2 = verts[i2];
        V3 n = v3_norm(v3_cross(mkv3(v1.x-v0.x, v1.y-v0.y, v1.z-v0.z),
                                mkv3(v2.x-v0.x, v2.y-v0.y, v2.z-v0.z)));
        normals[i0] = v3_add(normals[i0], n);
        normals[i1] = v3_add(normals[i1], n);
        normals[i2] = v3_add(normals[i2], n);
        counts[i0]++; counts[i1]++; counts[i2]++;
    }
    for (size_t i = 0; i < normals.size(); i++)
        if (counts[i] > 0) normals[i] = v3_norm(v3_scale(normals[i], 1.0f / counts[i]));
}

/* expand an indexed mesh to an interleaved pos3+normal3 triangle list */
static void expand_interleaved(const std::vector<V3>& verts, const std::vector<int>& tris,
                               const std::vector<V3>& normals, std::vector<float>& out) {
    for (size_t i = 0; i < tris.size(); i++) {
        int idx = tris[i];
        out.push_back(verts[idx].x); out.push_back(verts[idx].y); out.push_back(verts[idx].z);
        out.push_back(normals[idx].x); out.push_back(normals[idx].y); out.push_back(normals[idx].z);
    }
}

/* ---- GLSL ES 1.00 programs ---- */
/* gouraud: byte-for-byte the gears demo shape so the raspi5 V3D lit-gouraud
 * kernel and the CPU interpreter agree exactly. */
static const char* gouraud_vs =
    "attribute vec3 aPosition;\n"
    "attribute vec3 aNormal;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat4 uNormalMat;\n"
    "uniform vec3 uMaterial;\n"
    "uniform vec4 uLightEye;\n"
    "uniform vec4 uShade;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    vec3 ne = normalize((uNormalMat * vec4(aNormal, 0.0)).xyz);\n"
    "    vec3 L = normalize(uLightEye.xyz);\n"
    "    float diff = max(dot(ne, L), 0.0);\n"
    "    float spec = 0.0;\n"
    "    vec3 col = clamp(uMaterial * (uShade.x + uShade.y * diff) +\n"
    "                   vec3(uShade.z * spec), 0.0, 1.0);\n"
    "    vColor = vec4(col, 1.0);\n"
    "    gl_Position = uMVP * vec4(aPosition, 1.0);\n"
    "}\n";

static const char* gouraud_fs =
    "precision mediump float;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    gl_FragColor = vColor;\n"
    "}\n";

/* phong: per-pixel ADS, CPU-only (uNMat keeps it off the gouraud GPU kernel). */
static const char* phong_vs =
    "attribute vec3 aPosition;\n"
    "attribute vec3 aNormal;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat4 uNMat;\n"
    "varying vec3 vNormal;\n"
    "void main() {\n"
    "    vNormal = normalize((uNMat * vec4(aNormal, 0.0)).xyz);\n"
    "    gl_Position = uMVP * vec4(aPosition, 1.0);\n"
    "}\n";

static const char* phong_fs =
    "precision mediump float;\n"
    "varying vec3 vNormal;\n"
    "uniform vec3 uLightDir;\n"
    "uniform vec3 uKa;\n"
    "uniform vec3 uKd;\n"
    "uniform vec3 uKs;\n"
    "uniform float uShininess;\n"
    "void main() {\n"
    "    vec3 N = normalize(vNormal);\n"
    "    vec3 L = normalize(uLightDir);\n"
    "    vec3 col = uKa;\n"
    "    float diff = max(dot(N, L), 0.0);\n"
    "    col += uKd * diff;\n"
    "    if (diff > 0.0) {\n"
    "        vec3 r = reflect(-L, N);\n"
    "        float spec = max(dot(r, vec3(0.0, 0.0, 1.0)), 0.0);\n"
    "        col += uKs * pow(spec, uShininess);\n"
    "    }\n"
    "    gl_FragColor = vec4(col, 1.0);\n"
    "}\n";

/* solid: floor grid lines (pos-only). */
static const char* solid_vs =
    "attribute vec3 aPosition;\n"
    "uniform mat4 uMVP;\n"
    "uniform vec4 uColor;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    vColor = uColor;\n"
    "    gl_Position = uMVP * vec4(aPosition, 1.0);\n"
    "}\n";

static const char* solid_fs =
    "precision mediump float;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    gl_FragColor = vColor;\n"
    "}\n";

static GLuint gouraud_prog = 0, phong_prog = 0, solid_prog = 0;
static int cur_shader = 0;   /* 0 = gouraud (GPU), 1 = phong (CPU) */

/* gouraud uniform locations */
static GLint g_mvp = -1, g_normal = -1, g_material = -1, g_light = -1, g_shade = -1;
/* phong uniform locations */
static GLint p_mvp = -1, p_nmat = -1, p_light = -1, p_ka = -1, p_kd = -1, p_ks = -1, p_shin = -1;
/* solid uniform locations */
static GLint s_mvp = -1, s_color = -1;

static GLuint baked_vbo = 0, sphere_vbo = 0, torus_vbo = 0, line_vbo = 0;
static int sphere_count = 0, torus_count = 0, line_count = 0;

static Camera camera;
static float proj_mat[16];
static float light_direction[3];

/* materials */
static const float sphere_diffuse[3] = { 0.0f, 0.0f, 0.7f };
static const float sphere_ambient[3] = { 0.0f, 0.0f, 0.2f };
static const float sphere_specular[3] = { 1.0f, 1.0f, 1.0f };
static const float torus_diffuse[3]  = { 1.0f, 0.0f, 0.0f };
static const float torus_ambient[3]  = { 0.0f, 0.0f, 0.0f };
static const float torus_specular[3] = { 0.0f, 0.0f, 0.0f };
static const float floor_color[4]    = { 0.0f, 1.0f, 0.0f, 1.0f };
static const float SHININESS = 128.0f;

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    GLint ok = 0;
    if (sh == 0) return 0;
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        printf("sphere: shader compile failed: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint link_prog(const char* vs_src, const char* fs_src, int bind_normal) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    GLuint prog = glCreateProgram();
    GLint ok = 0;
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPosition");
    if (bind_normal) glBindAttribLocation(prog, 1, "aNormal");
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        printf("sphere: program link failed: %s\n", log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

static GLuint upload_vbo(const std::vector<float>& data) {
    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), &data[0], GL_STATIC_DRAW);
    return vbo;
}

static void bind_mesh(GLuint vbo) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), 0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
}

static void init_gl(void) {
    gouraud_prog = link_prog(gouraud_vs, gouraud_fs, 1);
    phong_prog   = link_prog(phong_vs, phong_fs, 1);
    solid_prog   = link_prog(solid_vs, solid_fs, 0);

    g_mvp      = glGetUniformLocation(gouraud_prog, "uMVP");
    g_normal   = glGetUniformLocation(gouraud_prog, "uNormalMat");
    g_material = glGetUniformLocation(gouraud_prog, "uMaterial");
    g_light    = glGetUniformLocation(gouraud_prog, "uLightEye");
    g_shade    = glGetUniformLocation(gouraud_prog, "uShade");

    p_mvp   = glGetUniformLocation(phong_prog, "uMVP");
    p_nmat  = glGetUniformLocation(phong_prog, "uNMat");
    p_light = glGetUniformLocation(phong_prog, "uLightDir");
    p_ka    = glGetUniformLocation(phong_prog, "uKa");
    p_kd    = glGetUniformLocation(phong_prog, "uKd");
    p_ks    = glGetUniformLocation(phong_prog, "uKs");
    p_shin  = glGetUniformLocation(phong_prog, "uShininess");

    s_mvp   = glGetUniformLocation(solid_prog, "uMVP");
    s_color = glGetUniformLocation(solid_prog, "uColor");

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    light_direction[0] = 0.0f; light_direction[1] = 10.0f; light_direction[2] = 5.0f;
    float ll = sqrtf(light_direction[0]*light_direction[0] +
                     light_direction[1]*light_direction[1] +
                     light_direction[2]*light_direction[2]);
    light_direction[0] /= ll; light_direction[1] /= ll; light_direction[2] /= ll;

    /* ---- floor grid (pos-only GL_LINES) ---- */
    std::vector<float> line_verts;
    for (int i = 0, j = -FLOOR_SIZE/2; i < 11; ++i, j += FLOOR_SIZE/10) {
        line_verts.push_back((float)j); line_verts.push_back(-1.0f); line_verts.push_back((float)-FLOOR_SIZE/2);
        line_verts.push_back((float)j); line_verts.push_back(-1.0f); line_verts.push_back((float)FLOOR_SIZE/2);
        line_verts.push_back((float)-FLOOR_SIZE/2); line_verts.push_back(-1.0f); line_verts.push_back((float)j);
        line_verts.push_back((float)FLOOR_SIZE/2); line_verts.push_back(-1.0f); line_verts.push_back((float)j);
    }
    line_count = (int)(line_verts.size() / 3);
    line_vbo = upload_vbo(line_verts);

    /* ---- sphere mesh ---- */
    std::vector<V3> sph_v, sph_n;
    std::vector<int> sph_t;
    make_sphere(sph_v, sph_t, 0.1f, 26, 13);
    compute_normals(sph_v, sph_t, sph_n);
    std::vector<float> sphere_il;
    expand_interleaved(sph_v, sph_t, sph_n, sphere_il);
    sphere_count = (int)sph_t.size();
    sphere_vbo = upload_vbo(sphere_il);

    /* ---- torus mesh (rotated 90 deg around X, as the original) ---- */
    std::vector<V3> tor_v, tor_n;
    std::vector<int> tor_t;
    make_torus(tor_v, tor_t, 0.3f, 0.1f, 40, 20);
    for (size_t i = 0; i < tor_v.size(); ++i) {
        float y = tor_v[i].y, z = tor_v[i].z;
        tor_v[i].y = -z;
        tor_v[i].z = y;
    }
    compute_normals(tor_v, tor_t, tor_n);
    std::vector<float> torus_il;
    expand_interleaved(tor_v, tor_t, tor_n, torus_il);
    torus_count = (int)tor_t.size();
    torus_vbo = upload_vbo(torus_il);

    /* ---- bake the 50 static sphere instances into ONE buffer ---- */
    std::vector<float> baked;
    for (int i = 0; i < NUM_SPHERES; ++i) {
        float ox, oy, oz;
        if (i == 0) { ox = 0.0f; oy = 0.0f; oz = 0.0f; }
        else {
            ox = randf_range(-FLOOR_SIZE/2.0f, FLOOR_SIZE/2.0f);
            oy = 0.4f;
            oz = randf_range(-FLOOR_SIZE/2.0f, FLOOR_SIZE/2.0f);
        }
        for (size_t k = 0; k < sphere_il.size(); k += 6) {
            baked.push_back(sphere_il[k]   + ox);
            baked.push_back(sphere_il[k+1] + oy);
            baked.push_back(sphere_il[k+2] + oz);
            baked.push_back(sphere_il[k+3]);
            baked.push_back(sphere_il[k+4]);
            baked.push_back(sphere_il[k+5]);
        }
    }
    baked_vbo = upload_vbo(baked);
}

/* Set the lighting/material uniforms of the active program for one mesh draw. */
static void set_shading(const float* mvp, const float* nmat,
                        const float* kd, const float* ka, const float* ks) {
    if (cur_shader == 0) {
        glUniformMatrix4fv(g_mvp, 1, GL_FALSE, mvp);
        glUniformMatrix4fv(g_normal, 1, GL_FALSE, nmat);
        glUniform3f(g_material, kd[0], kd[1], kd[2]);
        glUniform4f(g_light, light_direction[0], light_direction[1], light_direction[2], 0.0f);
        /* ambient factor approximates the original per-object Ka against uMaterial */
        float amb = (kd[0] > 0.0001f) ? (ka[0] / kd[0]) :
                    (kd[1] > 0.0001f) ? (ka[1] / kd[1]) :
                    (kd[2] > 0.0001f) ? (ka[2] / kd[2]) : 0.0f;
        glUniform4f(g_shade, amb, 1.0f, 0.0f, 0.0f);
    } else {
        glUniformMatrix4fv(p_mvp, 1, GL_FALSE, mvp);
        glUniformMatrix4fv(p_nmat, 1, GL_FALSE, nmat);
        glUniform3f(p_light, light_direction[0], light_direction[1], light_direction[2]);
        glUniform3f(p_ka, ka[0], ka[1], ka[2]);
        glUniform3f(p_kd, kd[0], kd[1], kd[2]);
        glUniform3f(p_ks, ks[0], ks[1], ks[2]);
        glUniform1f(p_shin, SHININESS);
    }
}

static void draw_mesh(GLuint vbo, int first, int count,
                      const float* mvp, const float* nmat,
                      const float* kd, const float* ka, const float* ks) {
    glUseProgram(cur_shader == 0 ? gouraud_prog : phong_prog);
    set_shading(mvp, nmat, kd, ka, ks);
    bind_mesh(vbo);
    glDrawArrays(GL_TRIANGLES, first, count);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
}

static float total_time = 0.0f;

static void render_frame(void) {
    EGLint w = 0, h = 0;
    eglQuerySurface(egl_dpy, egl_surf, EGL_WIDTH, &w);
    eglQuerySurface(egl_dpy, egl_surf, EGL_HEIGHT, &h);
    if (w > 0 && h > 0 && (w != win_width || h != win_height)) {
        win_width = w; win_height = h;
        glViewport(0, 0, w, h);
        mat4_perspective(proj_mat, DEG_TO_RAD(35.0f), (float)w / (float)h, 0.3f, 100.0f);
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float view_mat[16];
    camera.get_matrix(view_mat);
    float view_mvp[16];
    mat4_multiply(view_mvp, proj_mat, view_mat);

    /* floor grid */
    glUseProgram(solid_prog);
    glUniformMatrix4fv(s_mvp, 1, GL_FALSE, view_mvp);
    glUniform4f(s_color, floor_color[0], floor_color[1], floor_color[2], floor_color[3]);
    glBindBuffer(GL_ARRAY_BUFFER, line_vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_LINES, 0, line_count);
    glDisableVertexAttribArray(0);

    /* scattered spheres: one shared MVP/material so the GPU bridge coalesces
     * the per-instance draws; each instance is sphere_count vertices. */
    glBindBuffer(GL_ARRAY_BUFFER, baked_vbo);
    glUseProgram(cur_shader == 0 ? gouraud_prog : phong_prog);
    set_shading(view_mvp, view_mat, sphere_diffuse, sphere_ambient, sphere_specular);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), 0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    for (int i = 0; i < NUM_SPHERES; ++i)
        glDrawArrays(GL_TRIANGLES, i * sphere_count, sphere_count);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);

    /* rotating sphere at (0.8, 0.4, 0) */
    float rot_mat[16], trans_mat[16], temp_mat[16], mv[16], mvp[16];
    mat4_rotation(rot_mat, -total_time * DEG_TO_RAD(60.0f), 0, 1, 0);
    mat4_translation(trans_mat, 0.8f, 0.4f, 0.0f);
    mat4_multiply(temp_mat, rot_mat, trans_mat);
    mat4_multiply(mv, view_mat, temp_mat);
    mat4_multiply(mvp, proj_mat, mv);
    draw_mesh(sphere_vbo, 0, sphere_count, mvp, mv,
              sphere_diffuse, sphere_ambient, sphere_specular);

    /* rotating torus at (0, 0.3, -1.2) */
    mat4_rotation(rot_mat, total_time * DEG_TO_RAD(60.0f), 0, 1, 0);
    mat4_translation(trans_mat, 0.0f, 0.3f, -1.2f);
    mat4_multiply(temp_mat, rot_mat, trans_mat);
    mat4_multiply(mv, view_mat, temp_mat);
    mat4_multiply(mvp, proj_mat, mv);
    draw_mesh(torus_vbo, 0, torus_count, mvp, mv,
              torus_diffuse, torus_ambient, torus_specular);

    eglSwapBuffers(egl_dpy, egl_surf);
}

static int fps = 0, frame_count = 0;
static uint32_t last_tic = 0, last_frame_tic = 0;

static void loop(void* p) {
    (void)p;
    uint32_t now;
    kernel_tic32(NULL, NULL, &now);
    if (last_frame_tic == 0) last_frame_tic = now;
    float dt = (now - last_frame_tic) / 1000000.0f;
    last_frame_tic = now;
    if (dt > 0.1f) dt = 0.1f;
    total_time += dt;

    render_frame();

    if (last_tic == 0 || (now - last_tic) >= 1000000) {
        last_tic = now; fps = frame_count; frame_count = 0;
        printf("%d FPS\n", fps); fflush(stdout);
    }
    frame_count++;
    usleep(1000);
}

static void on_event(xwin_t* xwin, xevent_t* ev) {
    if (ev->type == XEVT_IM && ev->state == XIM_STATE_PRESS) {
        int key = ev->value.im.value;
        switch (key) {
        case KEY_ESC:   xwin_close(xwin); break;
        case KEY_LEFT:  camera.rotate_local_y(-5.0f); break;
        case KEY_RIGHT: camera.rotate_local_y(5.0f); break;
        case KEY_UP:    camera.rotate_local_x(-5.0f); break;
        case KEY_DOWN:  camera.rotate_local_x(5.0f); break;
        case 'a': camera.move_right(0.2f); break;
        case 'd': camera.move_right(-0.2f); break;
        case 'w': camera.move_forward(0.2f); break;
        case 's': camera.move_forward(-0.2f); break;
        case 'l': cur_shader = (cur_shader + 1) % 2; break;
        }
    }
}

static int init_egl(xwin_t* xwin) {
    EGLint major, minor, ncfg = 0;
    EGLint cfg_attr[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
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

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    x_t x;
    x_init(&x, NULL);
    x.on_loop = loop;

    xwin_t* xwin = xwin_open(&x, -1, 32, 32, WIDTH, HEIGHT, "Sphereworld Color", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("Failed to open window\n");
        return 1;
    }
    xwin_set_alpha(xwin, false);
    xwin->on_event = on_event;

    if (!init_egl(xwin)) {
        printf("Failed to initialize EGL\n");
        return 1;
    }

    srand(12345);
    win_width = WIDTH; win_height = HEIGHT;
    glViewport(0, 0, WIDTH, HEIGHT);
    mat4_perspective(proj_mat, DEG_TO_RAD(35.0f), WIDTH / (float)HEIGHT, 0.3f, 100.0f);
    init_gl();

    xwin_set_visible(xwin, true);
    x_run(&x, xwin);

    xwin_destroy(xwin);
    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (egl_surf != EGL_NO_SURFACE) eglDestroySurface(egl_dpy, egl_surf);
    if (egl_ctx != EGL_NO_CONTEXT) eglDestroyContext(egl_dpy, egl_ctx);
    eglTerminate(egl_dpy);
    return 0;
}
