// Dear ImGui: standalone example application for EwokOS xwin
//
// Rewritten onto the standard EGL + GLES2 + GLSL ES 1.00 front end (libs/gles)
// instead of PortableGL.  The window/context now come from libEGL (native window
// == xwin_t*), present is a plain eglSwapBuffers, and ImGui draws through the
// new GLES2 renderer backend (imgui_impl_gles2).  The visual design is
// unchanged: an editable clear colour, a solid red background triangle and the
// ImGui demo/Hello/Another windows on top.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <ewoksys/proc.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/timer.h>
#include <ewoksys/klog.h>
#include <x/x.h>
#include <x/xwin.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_gles2.h"

static int win_width = 640;
static int win_height = 480;

static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;

// ImGui state
static bool show_demo_window = true;
static bool show_another_window = false;
static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

// Background triangle: interleaved vec4 position + vec4 colour, drawn with the
// native gouraud colour pair (vColor = aColor; gl_Position = uMVP * aPosition)
// so it stays a plain solid triangle but can be offloaded on raspi5.
static GLfloat tri_verts[] = {
    // x,     y,     z,    w,    r,    g,    b,    a
    -0.5f, -0.5f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
     0.5f, -0.5f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
     0.0f,  0.5f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
};

static GLuint tri_program = 0;
static GLuint tri_vbo = 0;
static GLint  tri_loc_mvp = -1;

// Mouse state
static int mouse_x = 0;
static int mouse_y = 0;
static bool mouse_buttons[3] = {false, false, false}; // left, right, middle

// Keyboard state
static bool key_ctrl = false;
static bool key_shift = false;
static bool key_alt = false;
static bool key_super = false;

// FPS counter
static int fps = 0;
static int frame_count = 0;
static uint32_t last_tic = 0;

static void update_fps(void)
{
    uint32_t low;
    kernel_tic32(NULL, NULL, &low);

    if (last_tic == 0 || (low - last_tic) >= 3000000) {
        last_tic = low;
        fps = frame_count / 3;
        frame_count = 0;
    }
    frame_count++;
}

static const char *tri_vs =
    "attribute vec4 aPosition;\n"
    "attribute vec4 aColor;\n"
    "uniform mat4 uMVP;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    vColor = aColor;\n"
    "    gl_Position = uMVP * aPosition;\n"
    "}\n";

static const char *tri_fs =
    "precision mediump float;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    gl_FragColor = vColor;\n"
    "}\n";

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
        klog("imgui_demo: shader compile failed: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static void init_triangle(void)
{
    GLuint vs, fs;
    GLint ok = 0;

    vs = compile_shader(GL_VERTEX_SHADER, tri_vs);
    fs = compile_shader(GL_FRAGMENT_SHADER, tri_fs);
    tri_program = glCreateProgram();
    glAttachShader(tri_program, vs);
    glAttachShader(tri_program, fs);
    glBindAttribLocation(tri_program, 0, "aPosition");
    glBindAttribLocation(tri_program, 1, "aColor");
    glLinkProgram(tri_program);
    glGetProgramiv(tri_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(tri_program, sizeof(log), NULL, log);
        klog("imgui_demo: program link failed: %s\n", log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    tri_loc_mvp = glGetUniformLocation(tri_program, "uMVP");

    glGenBuffers(1, &tri_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, tri_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tri_verts), tri_verts, GL_STATIC_DRAW);
}

static void init_imgui(void)
{
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    // Disable imgui.ini
    io.IniFilename = NULL;

    // Setup display size
    io.DisplaySize = ImVec2((float)win_width, (float)win_height);

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Renderer backend
    ImGui_ImplGLES2_Init();

    // Load Fonts
    io.Fonts->AddFontFromFileTTF("/usr/system/fonts/system.ttf", 16.0f);
}

// Map EwokOS key code to ImGuiKey
static ImGuiKey KeyCodeToImGuiKey(int keycode)
{
    switch (keycode)
    {
        case 9: return ImGuiKey_Tab;
        case 27: return ImGuiKey_Escape;
        case 13: return ImGuiKey_Enter;
        case 8: return ImGuiKey_Backspace;
        case 127: return ImGuiKey_Delete;
        case 32: return ImGuiKey_Space;

        // Arrow keys
        case 276: return ImGuiKey_LeftArrow;
        case 275: return ImGuiKey_RightArrow;
        case 273: return ImGuiKey_UpArrow;
        case 274: return ImGuiKey_DownArrow;

        // Page/Home/End
        case 278: return ImGuiKey_Home;
        case 279: return ImGuiKey_End;
        case 280: return ImGuiKey_PageUp;
        case 281: return ImGuiKey_PageDown;
        case 277: return ImGuiKey_Insert;

        // Numbers
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

        // Letters
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

        // Function keys
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

        // Special characters
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

        default: return ImGuiKey_None;
    }
}

static void process_imgui_frame(void)
{
    ImGuiIO& io = ImGui::GetIO();

    // Update display size (in case of resize)
    io.DisplaySize = ImVec2((float)win_width, (float)win_height);

    // Update mouse position
    io.MousePos = ImVec2((float)mouse_x, (float)mouse_y);

    // Update mouse buttons
    io.MouseDown[0] = mouse_buttons[0]; // left
    io.MouseDown[1] = mouse_buttons[1]; // right
    io.MouseDown[2] = mouse_buttons[2]; // middle

    // Update keyboard modifiers
    io.KeyCtrl = key_ctrl;
    io.KeyShift = key_shift;
    io.KeyAlt = key_alt;
    io.KeySuper = key_super;

    // Start the Dear ImGui frame
    ImGui_ImplGLES2_NewFrame();
    ImGui::NewFrame();

    // 1. Show the big demo window
    if (show_demo_window)
        ImGui::ShowDemoWindow(&show_demo_window);

    // 2. Show a simple window
    {
        static float f = 0.0f;
        static int counter = 0;

        ImGui::Begin("Hello, world!");
        ImGui::Text("This is some useful text.");
        ImGui::Checkbox("Demo Window", &show_demo_window);
        ImGui::Checkbox("Another Window", &show_another_window);

        ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
        ImGui::ColorEdit3("clear color", (float*)&clear_color);

        if (ImGui::Button("Button")) {
            counter++;
            printf("%.1f FPS\n", ImGui::GetIO().Framerate);
        }
        ImGui::SameLine();
        ImGui::Text("counter = %d", counter);

        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                    1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::End();
    }

    // 3. Show another simple window
    if (show_another_window)
    {
        ImGui::Begin("Another Window", &show_another_window);
        ImGui::Text("Hello from another window!");
        if (ImGui::Button("Close Me"))
            show_another_window = false;
        ImGui::End();
    }
}

static void draw_triangle(void)
{
    // Identity projection: the triangle vertices are already in clip space.
    const GLfloat identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    glUseProgram(tri_program);
    glUniformMatrix4fv(tri_loc_mvp, 1, GL_FALSE, identity);

    glBindBuffer(GL_ARRAY_BUFFER, tri_vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void*)0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat),
                          (void*)(4 * sizeof(GLfloat)));
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
}

static void render_frame(void)
{
    EGLint w = 0, h = 0;

    // Pick up a window resize: libEGL already resized the engine back buffer,
    // we only mirror the new size into the viewport and ImGui's display size.
    eglQuerySurface(egl_dpy, egl_surf, EGL_WIDTH, &w);
    eglQuerySurface(egl_dpy, egl_surf, EGL_HEIGHT, &h);
    if (w > 0 && h > 0 && (w != win_width || h != win_height)) {
        win_width = w;
        win_height = h;
    }

    glViewport(0, 0, win_width, win_height);
    glScissor(0, 0, win_width, win_height);
    glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Background triangle
    draw_triangle();

    // GUI Rendering
    process_imgui_frame();
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
        uint32_t shift = ev->value.im.shift;
        uint32_t ctrl = ev->value.im.ctrl;
        int32_t state = ev->state; // XIM_STATE_PRESS or XIM_STATE_RELEASE

        // Update modifier keys
        key_shift = (shift != 0);
        key_ctrl = (ctrl != 0);

        // Handle ESC key to close window (only on press)
        if (key == 27 && state == XIM_STATE_PRESS) {
            xwin_close(xwin);
            return;
        }

        // Map key to ImGuiKey and add event
        ImGuiKey imgui_key = KeyCodeToImGuiKey(key);
        if (imgui_key != ImGuiKey_None) {
            bool pressed = (state == XIM_STATE_PRESS);
            io.AddKeyEvent(imgui_key, pressed);
        }

        // Add character input for printable characters (only on press)
        if (state == XIM_STATE_PRESS && key >= 32 && key < 127) {
            io.AddInputCharacter(key);
        }
    }
    else if (ev->type == XEVT_MOUSE) {
        gpos_t pos = xwin_get_inside_pos(xwin, ev->value.mouse.x, ev->value.mouse.y);

        mouse_x = pos.x;
        mouse_y = pos.y;

        // Update mouse buttons based on state (state is at top level of xevent_t)
        int32_t state = ev->state;
        int32_t button = ev->value.mouse.button;

        if (button == MOUSE_BUTTON_LEFT) {
            mouse_buttons[0] = (state == MOUSE_STATE_DOWN || state == MOUSE_STATE_DRAG);
        }
        else if (button == MOUSE_BUTTON_RIGHT) {
            mouse_buttons[1] = (state == MOUSE_STATE_DOWN || state == MOUSE_STATE_DRAG);
        }
        else if (button == MOUSE_BUTTON_MID) {
            mouse_buttons[2] = (state == MOUSE_STATE_DOWN || state == MOUSE_STATE_DRAG);
        }
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

int main(int argc, char* argv[])
{
    x_t x;
    xwin_t* xwin;

    (void)argc;
    (void)argv;

    x_init(&x, NULL);
    x.on_loop = loop;

    xwin = xwin_open(&x, -1, 32, 32, win_width, win_height, "ImGui Demo", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("Failed to open window\n");
        return 1;
    }
    xwin->on_event = on_event;

    // libEGL owns on_repaint/on_resize once the window surface is created.
    if (!init_egl(xwin)) {
        printf("Failed to initialize EGL\n");
        return 1;
    }

    glViewport(0, 0, win_width, win_height);
    glEnable(GL_DEPTH_TEST);

    init_triangle();
    init_imgui();

    // Make window visible
    xwin_set_visible(xwin, true);

    x_run(&x, xwin);

    // Cleanup
    ImGui_ImplGLES2_Shutdown();
    ImGui::DestroyContext();

    xwin_destroy(xwin);
    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl_dpy, egl_surf);
    eglDestroyContext(egl_dpy, egl_ctx);
    eglTerminate(egl_dpy);

    return 0;
}
