/*
 * Cubemap (skybox + reflective sphere) -- EGL + GLES2 (software rasterizer
 * behind libGLESv2).
 *
 * Ported off portablegl's C-callback model.  The window/context now come from
 * libEGL (native window == xwin_t*), present is a plain eglSwapBuffers, and the
 * two cubemap programs are real GLSL ES 1.00 shaders compiled by libGLESv2's CPU
 * GLSL interpreter: a skybox (box interior sampled by direction) and a sphere
 * whose per-fragment colour is the cubemap sampled along the eye reflection
 * vector.  Both sample with textureCube(samplerCube, vec3), which the engine's
 * bridge_tex_sample resolves through the cube binding on the sampler's texture
 * unit (dim == 3 -> texture_cubemap).
 *
 * GPU note (raspi5 / PGL_G3D): a samplerCube fragment shader is deliberately
 * DECLINED by the V3D bridge (gles_engine_gpu_link) - the fixed geometry kernels
 * only sample a raster 2D slot by uv, never a cube map by direction, and cube
 * faces are never mirrored into a GPU raster slot.  So this demo renders on the
 * CPU interpreter exactly as it did under portablegl (arbitrary C cubemap
 * shaders never matched the old fixed-function GPU gate either); declining at
 * link time also keeps the program from being marked GPU-eligible, which would
 * otherwise flip the framebuffer to the slow uncached path for no benefit.  No
 * GPU acceleration is lost because none existed for this effect.
 *
 * Geometry, camera, materials and the cube-face texture set are unchanged.
 */
#include "glcommon/gltools.h"   /* GLES2/gl2.h + gl2ext.h + load_texture_cubemap */

#include <EGL/egl.h>

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

static int win_width = 640;
static int win_height = 480;

static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;

/* ---- column-major mat4 / mat3 helpers ---- */
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

static void mat4_perspective(float* m, float fovy, float aspect, float zn, float zf) {
    float f = 1.0f / tanf(fovy / 2.0f);
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1.0f;
    m[14] = (2.0f * zf * zn) / (zn - zf);
}

static void mat3_from_mat4(float* m3, const float* m4) {
    m3[0] = m4[0]; m3[1] = m4[1]; m3[2] = m4[2];
    m3[3] = m4[4]; m3[4] = m4[5]; m3[5] = m4[6];
    m3[6] = m4[8]; m3[7] = m4[9]; m3[8] = m4[10];
}

static void mat4_invert(float* inv, const float* m) {
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];

    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (fabsf(det) < 0.0001f) return;
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) inv[i] *= det;
}

/* ---- camera (unchanged from the original: euler rot in degrees + position) ---- */
struct Camera {
    float pos[3];
    float rot[3];

    Camera() {
        pos[0] = 0; pos[1] = 0; pos[2] = 4.0f;
        rot[0] = 0; rot[1] = 0; rot[2] = 0;
    }

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
        trans_mat[0] = 1; trans_mat[1] = 0; trans_mat[2] = 0; trans_mat[3] = 0;
        trans_mat[4] = 0; trans_mat[5] = 1; trans_mat[6] = 0; trans_mat[7] = 0;
        trans_mat[8] = 0; trans_mat[9] = 0; trans_mat[10] = 1; trans_mat[11] = 0;
        trans_mat[12] = -pos[0]; trans_mat[13] = -pos[1]; trans_mat[14] = -pos[2]; trans_mat[15] = 1;

        mat4_multiply(mat, rot_mat, trans_mat);
    }

    void get_rotation_only(float* mat) {
        float rx = rot[0] * M_PI / 180.0f;
        float ry = rot[1] * M_PI / 180.0f;
        float rz = rot[2] * M_PI / 180.0f;
        float cx = cosf(rx), sx = sinf(rx);
        float cy = cosf(ry), sy = sinf(ry);
        float cz = cosf(rz), sz = sinf(rz);

        mat[0] = cy*cz; mat[1] = cy*sz; mat[2] = -sy; mat[3] = 0;
        mat[4] = sx*sy*cz - cx*sz; mat[5] = sx*sy*sz + cx*cz; mat[6] = sx*cy; mat[7] = 0;
        mat[8] = cx*sy*cz + sx*sz; mat[9] = cx*sy*sz - sx*cz; mat[10] = cx*cy; mat[11] = 0;
        mat[12] = 0; mat[13] = 0; mat[14] = 0; mat[15] = 1;
    }

    void move_forward(float dist) {
        float rad = rot[1] * M_PI / 180.0f;
        pos[0] -= sinf(rad) * dist;
        pos[2] -= cosf(rad) * dist;
    }
    void move_right(float dist) {
        float rad = rot[1] * M_PI / 180.0f;
        pos[0] += cosf(rad) * dist;
        pos[2] -= sinf(rad) * dist;
    }
    void rotate_local_y(float rad) { rot[1] += rad * 180.0f / M_PI; }
    void rotate_local_x(float rad) { rot[0] += rad * 180.0f / M_PI; }
};

/* ---- geometry (unchanged) ---- */
static void make_box(std::vector<float>& v, float w, float h, float d) {
    float hw = w / 2.0f, hh = h / 2.0f, hd = d / 2.0f;
    /* 6 faces x 2 triangles = 36 vertices, wound clockwise (seen from inside). */
    /* Front (z = -hd) */
    v.push_back(-hw); v.push_back(-hh); v.push_back(-hd);
    v.push_back(-hw); v.push_back( hh); v.push_back(-hd);
    v.push_back( hw); v.push_back( hh); v.push_back(-hd);
    v.push_back(-hw); v.push_back(-hh); v.push_back(-hd);
    v.push_back( hw); v.push_back( hh); v.push_back(-hd);
    v.push_back( hw); v.push_back(-hh); v.push_back(-hd);
    /* Back (z = hd) */
    v.push_back( hw); v.push_back(-hh); v.push_back(hd);
    v.push_back( hw); v.push_back( hh); v.push_back(hd);
    v.push_back(-hw); v.push_back( hh); v.push_back(hd);
    v.push_back( hw); v.push_back(-hh); v.push_back(hd);
    v.push_back(-hw); v.push_back( hh); v.push_back(hd);
    v.push_back(-hw); v.push_back(-hh); v.push_back(hd);
    /* Right (x = hw) */
    v.push_back(hw); v.push_back(-hh); v.push_back(-hd);
    v.push_back(hw); v.push_back( hh); v.push_back(-hd);
    v.push_back(hw); v.push_back( hh); v.push_back( hd);
    v.push_back(hw); v.push_back(-hh); v.push_back(-hd);
    v.push_back(hw); v.push_back( hh); v.push_back( hd);
    v.push_back(hw); v.push_back(-hh); v.push_back( hd);
    /* Left (x = -hw) */
    v.push_back(-hw); v.push_back(-hh); v.push_back( hd);
    v.push_back(-hw); v.push_back( hh); v.push_back( hd);
    v.push_back(-hw); v.push_back( hh); v.push_back(-hd);
    v.push_back(-hw); v.push_back(-hh); v.push_back( hd);
    v.push_back(-hw); v.push_back( hh); v.push_back(-hd);
    v.push_back(-hw); v.push_back(-hh); v.push_back(-hd);
    /* Top (y = hh) */
    v.push_back(-hw); v.push_back(hh); v.push_back(-hd);
    v.push_back(-hw); v.push_back(hh); v.push_back( hd);
    v.push_back( hw); v.push_back(hh); v.push_back( hd);
    v.push_back(-hw); v.push_back(hh); v.push_back(-hd);
    v.push_back( hw); v.push_back(hh); v.push_back( hd);
    v.push_back( hw); v.push_back(hh); v.push_back(-hd);
    /* Bottom (y = -hh) */
    v.push_back(-hw); v.push_back(-hh); v.push_back( hd);
    v.push_back(-hw); v.push_back(-hh); v.push_back(-hd);
    v.push_back( hw); v.push_back(-hh); v.push_back(-hd);
    v.push_back(-hw); v.push_back(-hh); v.push_back( hd);
    v.push_back( hw); v.push_back(-hh); v.push_back(-hd);
    v.push_back( hw); v.push_back(-hh); v.push_back( hd);
}

static void make_sphere(std::vector<float>& draw_verts, float radius, int segments, int rings) {
    std::vector<float> sp;
    for (int ring = 0; ring <= rings; ring++) {
        float phi = M_PI * ring / rings;
        for (int seg = 0; seg <= segments; seg++) {
            float theta = 2.0f * M_PI * seg / segments;
            sp.push_back(sinf(phi) * cosf(theta) * radius);
            sp.push_back(cosf(phi) * radius);
            sp.push_back(sinf(phi) * sinf(theta) * radius);
        }
    }
    for (int ring = 0; ring < rings; ring++)
        for (int seg = 0; seg < segments; seg++) {
            int curr = ring * (segments + 1) + seg;
            int next = curr + segments + 1;
            float v0x = sp[curr*3], v0y = sp[curr*3+1], v0z = sp[curr*3+2];
            float v1x = sp[next*3], v1y = sp[next*3+1], v1z = sp[next*3+2];
            float v2x = sp[(curr+1)*3], v2y = sp[(curr+1)*3+1], v2z = sp[(curr+1)*3+2];
            float v3x = sp[(next+1)*3], v3y = sp[(next+1)*3+1], v3z = sp[(next+1)*3+2];
            draw_verts.push_back(v0x); draw_verts.push_back(v0y); draw_verts.push_back(v0z);
            draw_verts.push_back(v1x); draw_verts.push_back(v1y); draw_verts.push_back(v1z);
            draw_verts.push_back(v2x); draw_verts.push_back(v2y); draw_verts.push_back(v2z);
            draw_verts.push_back(v1x); draw_verts.push_back(v1y); draw_verts.push_back(v1z);
            draw_verts.push_back(v3x); draw_verts.push_back(v3y); draw_verts.push_back(v3z);
            draw_verts.push_back(v2x); draw_verts.push_back(v2y); draw_verts.push_back(v2z);
        }
}

/* ---- GLSL ES 1.00 programs ---- */
/* Reflective sphere: compute the eye-space reflection vector, rotate it back to
 * world space by the inverse camera rotation, and sample the cubemap along it.
 * The normal matrix is a mat3 (upper-left of the view matrix) exactly as the
 * original C shader used it. */
static const char* reflection_vs =
    "attribute vec3 aPosition;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat4 uMV;\n"
    "uniform mat3 uNormalMat;\n"
    "uniform mat4 uInvCamera;\n"
    "varying vec3 vWorld;\n"
    "void main() {\n"
    "    vec3 eye_normal = normalize(uNormalMat * aPosition);\n"
    "    vec3 eye_vert = normalize((uMV * vec4(aPosition, 1.0)).xyz);\n"
    "    vec3 reflect_vec = reflect(eye_vert, eye_normal);\n"
    "    vWorld = normalize((uInvCamera * vec4(reflect_vec, 1.0)).xyz);\n"
    "    gl_Position = uMVP * vec4(aPosition, 1.0);\n"
    "}\n";

static const char* reflection_fs =
    "precision mediump float;\n"
    "varying vec3 vWorld;\n"
    "uniform samplerCube uTex;\n"
    "void main() {\n"
    "    gl_FragColor = textureCube(uTex, vWorld);\n"
    "}\n";

/* Skybox: the box is drawn from the inside, so the vertex position IS the sample
 * direction; flip Y because the cubemap faces are stored top-down. */
static const char* skybox_vs =
    "attribute vec3 aPosition;\n"
    "uniform mat4 uMVP;\n"
    "varying vec3 vDir;\n"
    "void main() {\n"
    "    vDir = vec3(aPosition.x, -aPosition.y, aPosition.z);\n"
    "    gl_Position = uMVP * vec4(aPosition, 1.0);\n"
    "}\n";

static const char* skybox_fs =
    "precision mediump float;\n"
    "varying vec3 vDir;\n"
    "uniform samplerCube uTex;\n"
    "void main() {\n"
    "    gl_FragColor = textureCube(uTex, vDir);\n"
    "}\n";

static GLuint reflection_prog = 0, skybox_prog = 0;
static GLint r_mvp = -1, r_mv = -1, r_normal = -1, r_invcam = -1, r_tex = -1;
static GLint s_mvp = -1, s_tex = -1;

static GLuint skybox_vbo = 0, sphere_vbo = 0, cube_tex = 0;
static int skybox_vertex_count = 0, sphere_vertex_count = 0;

static Camera camera;
static float proj_mat[16];
static float view_mat[16];
static float view_rot_mat[16];

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
        printf("cubemap: shader compile failed: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint link_prog(const char* vs_src, const char* fs_src) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    GLuint prog = glCreateProgram();
    GLint ok = 0;
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPosition");
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        printf("cubemap: program link failed: %s\n", log);
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

static void init_gl(void) {
    reflection_prog = link_prog(reflection_vs, reflection_fs);
    skybox_prog     = link_prog(skybox_vs, skybox_fs);

    r_mvp    = glGetUniformLocation(reflection_prog, "uMVP");
    r_mv     = glGetUniformLocation(reflection_prog, "uMV");
    r_normal = glGetUniformLocation(reflection_prog, "uNormalMat");
    r_invcam = glGetUniformLocation(reflection_prog, "uInvCamera");
    r_tex    = glGetUniformLocation(reflection_prog, "uTex");
    s_mvp    = glGetUniformLocation(skybox_prog, "uMVP");
    s_tex    = glGetUniformLocation(skybox_prog, "uTex");

    /* Both samplers read texture unit 0; the cube map stays bound there. */
    glUseProgram(reflection_prog); glUniform1i(r_tex, 0);
    glUseProgram(skybox_prog);     glUniform1i(s_tex, 0);

    /* ---- cubemap texture: gen + bind, then glcommon uploads the 6 faces ---- */
    const char* cube_map_textures[] = {
        "/data/media/textures/skybox/right.jpg",
        "/data/media/textures/skybox/left.jpg",
        "/data/media/textures/skybox/top.jpg",
        "/data/media/textures/skybox/bottom.jpg",
        "/data/media/textures/skybox/front.jpg",
        "/data/media/textures/skybox/back.jpg"
    };
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &cube_tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cube_tex);
    if (!load_texture_cubemap(cube_map_textures, GL_NEAREST, GL_NEAREST, GL_FALSE, GL_TRUE))
        printf("cubemap: failed to load one or more skybox faces\n");

    /* ---- meshes (pos-only, so attr0 alone; no VAOs in the GLES2 front end) ---- */
    std::vector<float> skybox_verts;
    make_box(skybox_verts, 40.0f, 40.0f, 40.0f);
    skybox_vertex_count = (int)(skybox_verts.size() / 3);
    skybox_vbo = upload_vbo(skybox_verts);

    std::vector<float> sphere_verts;
    make_sphere(sphere_verts, 0.5f, 32, 16);
    sphere_vertex_count = (int)(sphere_verts.size() / 3);
    sphere_vbo = upload_vbo(sphere_verts);

    glEnable(GL_DEPTH_TEST);
    glCullFace(GL_BACK);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

static void reshape(int w, int h) {
    if (w <= 0 || h <= 0) return;
    win_width = w; win_height = h;
    glViewport(0, 0, w, h);
    mat4_perspective(proj_mat, 35.0f * (float)M_PI / 180.0f, (float)w / (float)h, 1.0f, 100.0f);
}

static void render_frame(void) {
    EGLint w = 0, h = 0;
    eglQuerySurface(egl_dpy, egl_surf, EGL_WIDTH, &w);
    eglQuerySurface(egl_dpy, egl_surf, EGL_HEIGHT, &h);
    if (w > 0 && h > 0 && (w != win_width || h != win_height))
        reshape(w, h);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    camera.get_matrix(view_mat);
    camera.get_rotation_only(view_rot_mat);

    /* ---- skybox first: rotation-only MVP, no depth write/test, no cull ---- */
    float skybox_mvp[16];
    mat4_multiply(skybox_mvp, proj_mat, view_rot_mat);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(skybox_prog);
    glUniformMatrix4fv(s_mvp, 1, GL_FALSE, skybox_mvp);
    glBindBuffer(GL_ARRAY_BUFFER, skybox_vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, skybox_vertex_count);
    glDisableVertexAttribArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    /* ---- reflective sphere at the origin (model matrix is identity) ---- */
    float mvp_mat[16], normal_mat[9], inverse_camera[16];
    mat4_multiply(mvp_mat, proj_mat, view_mat);
    mat3_from_mat4(normal_mat, view_mat);
    mat4_invert(inverse_camera, view_rot_mat);
    glUseProgram(reflection_prog);
    glUniformMatrix4fv(r_mvp, 1, GL_FALSE, mvp_mat);
    glUniformMatrix4fv(r_mv, 1, GL_FALSE, view_mat);
    glUniformMatrix3fv(r_normal, 1, GL_FALSE, normal_mat);
    glUniformMatrix4fv(r_invcam, 1, GL_FALSE, inverse_camera);
    glBindBuffer(GL_ARRAY_BUFFER, sphere_vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, sphere_vertex_count);
    glDisableVertexAttribArray(0);

    eglSwapBuffers(egl_dpy, egl_surf);
}

static void on_event(xwin_t* xwin, xevent_t* ev) {
    if (ev->type == XEVT_IM && ev->state == XIM_STATE_PRESS) {
        int key = ev->value.im.value;
        switch (key) {
        case KEY_ESC:   xwin_close(xwin); break;
        case KEY_LEFT:  camera.rotate_local_y(DEG_TO_RAD(-5.0f)); break;
        case KEY_RIGHT: camera.rotate_local_y(DEG_TO_RAD(5.0f)); break;
        case KEY_UP:    camera.rotate_local_x(DEG_TO_RAD(-5.0f)); break;
        case KEY_DOWN:  camera.rotate_local_x(DEG_TO_RAD(5.0f)); break;
        }
    }
}

static void loop(void* p) {
    (void)p;
    render_frame();
    usleep(16000);
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

    xwin_t* xwin = xwin_open(&x, -1, 32, 32, win_width, win_height, "Cubemap", XWIN_STYLE_NORMAL);
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

    camera.pos[0] = 0; camera.pos[1] = 0; camera.pos[2] = 3.0f;
    reshape(win_width, win_height);
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
