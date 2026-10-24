/*
 * 3D model demo (textured F22) for EwokOS.
 *
 * Rewritten onto the standard EGL + GLES2 + GLSL ES 1.00 front end (libs/gles):
 * the window/context come from libEGL (native window == xwin_t*), the model's
 * vertex transform + texture-modulate shading run as real GLSL ES 1.00 shaders
 * compiled by libGLESv2's CPU GLSL interpreter, and present is a plain
 * eglSwapBuffers.  libEGL owns on_repaint/on_resize; the app drives frames from
 * its run loop and polls the surface size for resizes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <unistd.h>
#include <ewoksys/proc.h>
#include <ewoksys/keydef.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/timer.h>
#include <ewoksys/klog.h>
#include <x/x.h>
#include <x/xwin.h>
#include <graph/graph.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_gles2.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int win_width = 640;
static int win_height = 480;

static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;

/* Model + texture data (self-contained; no portablegl types). */
#include "src/obj.c"
#include "src/texture.c"

static GLuint program = 0, vbo = 0;
static GLuint texture = 0;
static GLint loc_mvp = -1, loc_tex = -1;

/* GLSL ES 1.00: transform position by uMVP, pass the (V-flipped) texture
 * coordinate to a texture-modulate fragment shader.  mat4 uniforms are uploaded
 * column-major (transpose = GL_FALSE), matching "uMVP * vec4(p,1)". */
static const char *demo_vs =
    "attribute vec3 aPosition;\n"
    "attribute vec2 aUV;\n"
    "uniform mat4 uMVP;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "    vUV = vec2(aUV.x, 1.0 - aUV.y);\n"
    "    gl_Position = uMVP * vec4(aPosition, 1.0);\n"
    "}\n";

static const char *demo_fs =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uTex0;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(uTex0, vUV);\n"
    "}\n";

/* ------------------------------------------------------------------ *
 * Minimal column-major matrix helpers (same convention as gears.c).
 * multiply(a, b) computes a = a * b.
 * ------------------------------------------------------------------ */
static void multiply(GLfloat *m, const GLfloat *n)
{
    GLfloat tmp[16];
    const GLfloat *row, *column;
    div_t d;
    int i, j;

    for (i = 0; i < 16; i++) {
        tmp[i] = 0;
        d = div(i, 4);
        row = n + d.quot * 4;
        column = m + d.rem;
        for (j = 0; j < 4; j++)
            tmp[i] += row[j] * column[j * 4];
    }
    memcpy(m, tmp, sizeof(tmp));
}

static void identity(GLfloat *m)
{
    GLfloat t[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    memcpy(m, t, sizeof(t));
}

static void perspective(GLfloat *m, GLfloat fovy, GLfloat aspect, GLfloat z_near, GLfloat z_far)
{
    GLfloat tmp[16];
    identity(tmp);

    GLfloat radians = fovy / 2.0f * M_PI / 180.0f;
    GLfloat sine = sinf(radians);
    GLfloat cosine = cosf(radians);
    GLfloat delta_z = z_far - z_near;

    if (delta_z == 0 || sine == 0 || aspect == 0)
        return;

    GLfloat cotangent = cosine / sine;

    tmp[0] = cotangent / aspect;
    tmp[5] = cotangent;
    tmp[10] = -(z_far + z_near) / delta_z;
    tmp[11] = -1.0f;
    tmp[14] = -2.0f * z_near * z_far / delta_z;
    tmp[15] = 0.0f;

    memcpy(m, tmp, sizeof(tmp));
}

static void look_at(GLfloat *m, GLfloat eye_x, GLfloat eye_y, GLfloat eye_z,
                    GLfloat center_x, GLfloat center_y, GLfloat center_z,
                    GLfloat up_x, GLfloat up_y, GLfloat up_z)
{
    GLfloat f[3], s[3], u[3];
    GLfloat f_len, s_len;

    f[0] = center_x - eye_x;
    f[1] = center_y - eye_y;
    f[2] = center_z - eye_z;
    f_len = sqrtf(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    if (f_len == 0.0f) f_len = 1.0f;
    f[0] /= f_len; f[1] /= f_len; f[2] /= f_len;

    s[0] = f[1]*up_z - f[2]*up_y;
    s[1] = f[2]*up_x - f[0]*up_z;
    s[2] = f[0]*up_y - f[1]*up_x;
    s_len = sqrtf(s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
    if (s_len == 0.0f) s_len = 1.0f;
    s[0] /= s_len; s[1] /= s_len; s[2] /= s_len;

    u[0] = s[1]*f[2] - s[2]*f[1];
    u[1] = s[2]*f[0] - s[0]*f[2];
    u[2] = s[0]*f[1] - s[1]*f[0];

    GLfloat look_at_mat[16] = {
        s[0], u[0], -f[0], 0,
        s[1], u[1], -f[1], 0,
        s[2], u[2], -f[2], 0,
        -(s[0]*eye_x + s[1]*eye_y + s[2]*eye_z),
        -(u[0]*eye_x + u[1]*eye_y + u[2]*eye_z),
        f[0]*eye_x + f[1]*eye_y + f[2]*eye_z,
        1
    };

    memcpy(m, look_at_mat, sizeof(look_at_mat));
}

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type);
    GLint ok = 0;
    if (sh == 0) return 0;
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        klog("3ddemo: shader compile failed: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static void init_mesh(void)
{
    float* vertex_data = (float*)malloc(num_faces * 3 * 5 * sizeof(float));
    int idx = 0;
    for (int i = 0; i < num_faces; i++) {
        for (int j = 0; j < 3; j++) {
            int vidx;
            float u, v;
            if (j == 0)      { vidx = faces[i].a - 1; u = faces[i].a_uv.u; v = faces[i].a_uv.v; }
            else if (j == 1) { vidx = faces[i].b - 1; u = faces[i].b_uv.u; v = faces[i].b_uv.v; }
            else             { vidx = faces[i].c - 1; u = faces[i].c_uv.u; v = faces[i].c_uv.v; }

            vertex_data[idx++] = vertices[vidx].x;
            vertex_data[idx++] = vertices[vidx].y;
            vertex_data[idx++] = vertices[vidx].z;
            vertex_data[idx++] = u;
            vertex_data[idx++] = v;
        }
    }

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, num_faces * 3 * 5 * sizeof(float), vertex_data, GL_STATIC_DRAW);
    free(vertex_data);
}

static void init_texture(void)
{
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    int num_pixels = texture_width * texture_height;
    uint8_t* rgba_data = (uint8_t*)malloc(num_pixels * 4);
    for (int i = 0; i < num_pixels; i++) {
        uint32_t argb = ((uint32_t*)mesh_texture)[i];
        rgba_data[i * 4 + 0] = (argb >> 16) & 0xFF;   /* r */
        rgba_data[i * 4 + 1] = (argb >> 8)  & 0xFF;   /* g */
        rgba_data[i * 4 + 2] = argb & 0xFF;           /* b */
        rgba_data[i * 4 + 3] = (argb >> 24) & 0xFF;   /* a */
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture_width, texture_height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba_data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    free(rgba_data);
}

static void init_imgui(void);

static void init_gl(void)
{
    GLuint vs, fs;
    GLint ok = 0;

    glEnable(GL_DEPTH_TEST);

    vs = compile_shader(GL_VERTEX_SHADER, demo_vs);
    fs = compile_shader(GL_FRAGMENT_SHADER, demo_fs);
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aUV");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        klog("3ddemo: program link failed: %s\n", log);
        return;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(program);

    loc_mvp = glGetUniformLocation(program, "uMVP");
    loc_tex = glGetUniformLocation(program, "uTex0");

    init_mesh();
    init_texture();

    /* Bind the model texture to sampler unit 0 and tell the shader about it. */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(loc_tex, 0);

    glViewport(0, 0, win_width, win_height);
    printf("Texture ID: %u\n", texture);

    init_imgui();
}

static float camera_angle = 0.0f;

static int fps = 0;
static int count = 0;
static uint32_t last_tic = 0;
static uint32_t last_frame_tic = 0;

static void update_fps(void)
{
    uint32_t low;
    kernel_tic32(NULL, NULL, &low);
    if (last_tic == 0 || (low - last_tic) >= 1000000) {
        last_tic = low;
        fps = count;
        count = 0;
    }
    count++;
}

static void render_mesh(void)
{
    GLfloat view_mat[16], proj_mat[16], mvp_mat[16];

    /* ImGui leaves its own program bound and its font atlas on texture unit 0
     * after RenderDrawData (the GLES2 backend restores only the viewport/scissor/
     * buffer/enable-cap state), so re-bind the scene program AND the model
     * texture every frame before drawing the textured mesh. */
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Camera orbits the model horizontally, always looking at its centre. */
    float cam_radius = 4.0f;
    float cam_x = sinf(camera_angle) * cam_radius;
    float cam_z = cosf(camera_angle) * cam_radius;
    float cam_y = 1.0f;

    look_at(view_mat, cam_x, cam_y, cam_z, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    perspective(proj_mat, 60.0f, (GLfloat)win_width / (GLfloat)win_height, 0.1f, 100.0f);

    memcpy(mvp_mat, proj_mat, sizeof(mvp_mat));
    multiply(mvp_mat, view_mat);   /* model matrix is identity */

    glUniformMatrix4fv(loc_mvp, 1, GL_FALSE, mvp_mat);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), 0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    glDrawArrays(GL_TRIANGLES, 0, num_faces * 3);

    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
}

static void reshape(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;
    win_width = width;
    win_height = height;
    glViewport(0, 0, width, height);
}

/* ------------------------------------------------------------------ *
 * Dear ImGui overlay: the FPS / GPU-vs-CPU readout is presented as a
 * small draggable ImGui window drawn on top of the model (replacing the
 * old glcommon bitmap HUD).  ImGui is C++ and renders through the GLES2
 * backend; xwin mouse/keyboard events are forwarded to it so the window
 * stays interactive, and ESC still quits like the other opengl demos.
 * ------------------------------------------------------------------ */
static int  mouse_x = 0, mouse_y = 0;
static bool mouse_buttons[3] = { false, false, false };    /* left, right, mid */
static bool key_ctrl = false, key_shift = false, key_alt = false, key_super = false;

static void init_imgui(void)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;                                 /* no imgui.ini    */
    io.DisplaySize = ImVec2((float)win_width, (float)win_height);
    ImGui::StyleColorsDark();
    ImGui_ImplGLES2_Init();
    io.Fonts->AddFontFromFileTTF("/usr/system/fonts/system.ttf", 16.0f);
}

/* Map an EwokOS key code to the matching ImGuiKey (same table as imgui_demo). */
static ImGuiKey KeyCodeToImGuiKey(int keycode)
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
        case '0': return ImGuiKey_0;
        case '1': return ImGuiKey_1;
        case '2': return ImGuiKey_2;
        case '3': return ImGuiKey_3;
        case '4': return ImGuiKey_4;
        case '5': return ImGuiKey_5;
        case '6': return ImGuiKey_6;
        case '7': return ImGuiKey_7;
        case '8': return ImGuiKey_8;
        case '9': return ImGuiKey_9;
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
        case 282: return ImGuiKey_F1;
        case 283: return ImGuiKey_F2;
        case 284: return ImGuiKey_F3;
        case 285: return ImGuiKey_F4;
        case 286: return ImGuiKey_F5;
        case 287: return ImGuiKey_F6;
        case 288: return ImGuiKey_F7;
        case 289: return ImGuiKey_F8;
        case 290: return ImGuiKey_F9;
        case 291: return ImGuiKey_F10;
        case 292: return ImGuiKey_F11;
        case 293: return ImGuiKey_F12;
        case ',': return ImGuiKey_Comma;
        case '.': return ImGuiKey_Period;
        case '/': return ImGuiKey_Slash;
        case ';': return ImGuiKey_Semicolon;
        case '\'': return ImGuiKey_Apostrophe;
        case '[': return ImGuiKey_LeftBracket;
        case ']': return ImGuiKey_RightBracket;
        case '-': return ImGuiKey_Minus;
        case '=': return ImGuiKey_Equal;
        case '`': return ImGuiKey_GraveAccent;
        default:  return ImGuiKey_None;
    }
}

/* Build one ImGui frame: forward input, then lay out the stats window.  `gpu`
 * is the route the model program used this frame, queried while it was bound. */
static void process_imgui_frame(int gpu)
{
    ImGuiIO& io = ImGui::GetIO();

    io.DisplaySize  = ImVec2((float)win_width, (float)win_height);
    io.MousePos     = ImVec2((float)mouse_x, (float)mouse_y);
    io.MouseDown[0] = mouse_buttons[0];
    io.MouseDown[1] = mouse_buttons[1];
    io.MouseDown[2] = mouse_buttons[2];
    io.KeyCtrl      = key_ctrl;
    io.KeyShift     = key_shift;
    io.KeyAlt       = key_alt;
    io.KeySuper     = key_super;

    ImGui_ImplGLES2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("3D F22 Demo", NULL,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("FPS: %d", fps);
    ImGui::TextColored(gpu ? ImVec4(0.40f, 1.00f, 0.55f, 1.0f)
                           : ImVec4(1.00f, 0.70f, 0.30f, 1.0f),
                       "Renderer: %s", gpu ? "GPU" : "CPU");
    ImGui::End();
}

static void render_frame(void)
{
    EGLint w = 0, h = 0;
    uint32_t current_tic;
    float dt;
    int gpu;

    eglQuerySurface(egl_dpy, egl_surf, EGL_WIDTH, &w);
    eglQuerySurface(egl_dpy, egl_surf, EGL_HEIGHT, &h);
    if (w > 0 && h > 0 && (w != win_width || h != win_height))
        reshape(w, h);

    kernel_tic32(NULL, NULL, &current_tic);
    if (last_frame_tic == 0)
        last_frame_tic = current_tic;
    dt = (current_tic - last_frame_tic) / 1000000.0f;
    last_frame_tic = current_tic;
    if (dt > 0.1f) dt = 0.1f;

    camera_angle += 1.5f * dt;

    render_mesh();

    /* Query the GPU/CPU route while the model program is still bound: the engine
     * reports the currently-bound program and ImGui binds its own next.  The
     * readout is then presented as a Dear ImGui window drawn on top. */
    gpu = glGpuActiveEWOK();
    process_imgui_frame(gpu);
    ImGui::Render();
    ImGui_ImplGLES2_RenderDrawData(ImGui::GetDrawData());

    eglSwapBuffers(egl_dpy, egl_surf);
    update_fps();
}

static void on_event(xwin_t* xwin, xevent_t* ev)
{
    ImGuiIO& io = ImGui::GetIO();

    if (ev->type == XEVT_IM) {
        int key = ev->value.im.value;
        int32_t state = ev->state;                 /* XIM_STATE_PRESS/RELEASE */

        key_shift = (ev->value.im.shift != 0);
        key_ctrl  = (ev->value.im.ctrl  != 0);

        /* ESC (KEY_ESC == 27) quits, matching the other opengl demos. */
        if (key == KEY_ESC && state == XIM_STATE_PRESS) {
            xwin_close(xwin);
            return;
        }

        ImGuiKey ik = KeyCodeToImGuiKey(key);
        if (ik != ImGuiKey_None)
            io.AddKeyEvent(ik, state == XIM_STATE_PRESS);
        if (state == XIM_STATE_PRESS && key >= 32 && key < 127)
            io.AddInputCharacter(key);
    }
    else if (ev->type == XEVT_MOUSE) {
        gpos_t pos = xwin_get_inside_pos(xwin, ev->value.mouse.x, ev->value.mouse.y);
        int32_t state  = ev->state;
        int32_t button = ev->value.mouse.button;
        bool down = (state == MOUSE_STATE_DOWN || state == MOUSE_STATE_DRAG);

        mouse_x = pos.x;
        mouse_y = pos.y;
        if (button == MOUSE_BUTTON_LEFT)
            mouse_buttons[0] = down;
        else if (button == MOUSE_BUTTON_RIGHT)
            mouse_buttons[1] = down;
        else if (button == MOUSE_BUTTON_MID)
            mouse_buttons[2] = down;
    }
}

static void loop(void* p)
{
    (void)p;
    render_frame();
    usleep(1000);
}

static int init_egl(xwin_t* xwin)
{
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

int main(int argc, char** argv)
{
    x_t x;
    xwin_t* xwin;

    (void)argc;
    (void)argv;

    x_init(&x, NULL);
    x.on_loop = loop;

    xwin = xwin_open(&x, -1, 32, 32, win_width, win_height, "3D F22 Demo", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("Failed to open window\n");
        return 1;
    }
    xwin_set_alpha(xwin, false);
    xwin->on_event = on_event;

    /* libEGL owns on_repaint/on_resize once the window surface is created. */
    if (!init_egl(xwin)) {
        printf("Failed to initialize EGL\n");
        return 1;
    }

    init_gl();

    xwin_set_visible(xwin, true);
    x_run(&x, xwin);

    ImGui_ImplGLES2_Shutdown();
    ImGui::DestroyContext();

    xwin_destroy(xwin);
    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl_dpy, egl_surf);
    eglDestroyContext(egl_dpy, egl_ctx);
    eglTerminate(egl_dpy);

    return 0;
}
