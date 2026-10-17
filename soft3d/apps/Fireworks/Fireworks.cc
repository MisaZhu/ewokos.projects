/*
 * Fireworks demo for EwokOS.
 *
 * A CPU particle system: rockets launch, explode, and their glowing debris
 * falls under gravity.  Every particle is composited (alpha-blended radial glow
 * + dotted velocity trail) into an in-memory ARGB framebuffer exactly as the
 * original did.
 *
 * Presentation was rewritten off SDL2 + a portablegl scratch buffer onto the
 * standard EGL + GLES2 front end (libs/gles): the composited framebuffer is
 * uploaded each frame as a texture and drawn as a fullscreen quad by libGLESv2,
 * and libEGL owns the window surface, present and resize.  The particle physics
 * and the per-pixel drawing below are unchanged, so the visuals are identical.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

#define WIDTH 1024
#define HEIGHT 768
#define MAX_PARTICLES 5000
#define MAX_FIREWORKS 5

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Particle structure - dotted trail effect
typedef struct {
    float x, y;
    float vx, vy;
    float life;
    float maxLife;
    float r, g, b;
    float brightness;
    int active;
} Particle;

// Firework rocket
typedef struct {
    float x, y;
    float vx, vy;
    float r, g, b;
    int active;
} Firework;

// Global variables
static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;

static GLuint program = 0, quad_vbo = 0, texture = 0;
static GLint loc_tex = -1;

static int screen_width = WIDTH, screen_height = HEIGHT;

/* CPU compositing target: 0xAARRGGBB words, converted to RGBA8 for upload. */
static uint32_t* bbufpix = NULL;
static uint8_t*  rgba_buf = NULL;

Particle particles[MAX_PARTICLES];
int num_particles = 0;

Firework fireworks[MAX_FIREWORKS];
int next_firework_time = 0;

static const char *quad_vs =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "    vUV = aUV;\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

static const char *quad_fs =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uTex0;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(uTex0, vUV);\n"
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
        klog("Fireworks: shader compile failed: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

/* Fullscreen quad (two triangles) in clip space.  Texture row 0 maps to the top
 * of the window, matching the top-down screen coordinates the particles use. */
static const GLfloat quad_verts[] = {
    /* x,    y,    u,   v */
    -1.0f,  1.0f, 0.0f, 0.0f,
    -1.0f, -1.0f, 0.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 0.0f,
    -1.0f, -1.0f, 0.0f, 1.0f,
     1.0f, -1.0f, 1.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 0.0f,
};

static void init_gl(void)
{
    GLuint vs, fs;
    GLint ok = 0;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    vs = compile_shader(GL_VERTEX_SHADER, quad_vs);
    fs = compile_shader(GL_FRAGMENT_SHADER, quad_fs);
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "aPos");
    glBindAttribLocation(program, 1, "aUV");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        klog("Fireworks: program link failed: %s\n", log);
        return;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(program);

    loc_tex = glGetUniformLocation(program, "uTex0");

    glGenBuffers(1, &quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts), quad_verts, GL_STATIC_DRAW);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glUniform1i(loc_tex, 0);
}

/* (Re)allocate the CPU compositing buffers and the texture storage for a new
 * window size. */
static int ensure_buffer(int w, int h)
{
    if (w <= 0 || h <= 0)
        return 0;
    if (bbufpix != NULL && w == screen_width && h == screen_height)
        return 1;

    free(bbufpix);
    free(rgba_buf);
    bbufpix = (uint32_t*)malloc((size_t)w * h * sizeof(uint32_t));
    rgba_buf = (uint8_t*)malloc((size_t)w * h * 4);
    if (bbufpix == NULL || rgba_buf == NULL) {
        free(bbufpix); free(rgba_buf);
        bbufpix = NULL; rgba_buf = NULL;
        return 0;
    }
    screen_width = w;
    screen_height = h;

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glViewport(0, 0, w, h);
    return 1;
}

float random_float(float min, float max) {
    return min + (float)rand() / RAND_MAX * (max - min);
}

// HSV to RGB conversion
void hsv_to_rgb(float h, float s, float v, float* r, float* g, float* b) {
    int i = (int)(h * 6);
    float f = h * 6 - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);

    switch (i % 6) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        case 5: *r = v; *g = p; *b = q; break;
    }
}

// Launch firework - random height, supports multiple simultaneous fireworks
void launch_firework() {
    // Find an available firework slot
    int slot = -1;
    for (int i = 0; i < MAX_FIREWORKS; i++) {
        if (!fireworks[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return; // No available slots

    Firework* fw = &fireworks[slot];
    fw->x = random_float(screen_width * 0.1f, screen_width * 0.9f);
    fw->y = screen_height;
    fw->vx = random_float(-0.5f, 0.5f);
    // Speed adjusted based on target height, higher targets need faster speed
    fw->vy = random_float(-12.0f, -18.0f);

    float hue = random_float(0.0f, 1.0f);
    hsv_to_rgb(hue, 1.0f, 1.0f, &fw->r, &fw->g, &fw->b);

    fw->active = 1;
}

// Create explosion - smaller and more natural range
void create_explosion(float x, float y, float r, float g, float b) {
    // Check if particle array is full
    if (num_particles >= MAX_PARTICLES - 500) return;

    int streams = 20 + rand() % 12; // 20-32 streams

    for (int s = 0; s < streams; s++) {
        float base_angle = (M_PI * 2 * s) / streams;
        // Add natural random angle variation
        float angle_var = random_float(-0.15f, 0.15f);

        // Reduce particles per stream
        int particles_per_stream = 12 + rand() % 8;

        for (int i = 0; i < particles_per_stream; i++) {
            if (num_particles >= MAX_PARTICLES) break;

            Particle* p = &particles[num_particles++];

            float angle = base_angle + angle_var + random_float(-0.08f, 0.08f);
            // Slower speed, more compact range
            float base_speed = 1.2f + (float)i * 0.25f;
            float speed = base_speed * random_float(0.9f, 1.1f);

            p->x = x + random_float(-2.0f, 2.0f); // Slight randomness in starting position
            p->y = y + random_float(-2.0f, 2.0f);
            p->vx = cos(angle) * speed;
            p->vy = sin(angle) * speed;
            p->life = 1.0f;
            p->maxLife = random_float(50.0f, 90.0f);
            p->brightness = 1.0f - (float)i / particles_per_stream * 0.25f;

            // Color gradient
            float fade = 1.0f - (float)i / particles_per_stream * 0.35f;
            p->r = r * fade;
            p->g = g * fade;
            p->b = b * fade;

            p->active = 1;
        }
    }

    // Reduce center particles
    int center_particles = 15;
    for (int i = 0; i < center_particles; i++) {
        if (num_particles >= MAX_PARTICLES) break;

        Particle* p = &particles[num_particles++];

        float angle = random_float(0, M_PI * 2);
        float speed = random_float(0.3f, 1.5f);

        p->x = x;
        p->y = y;
        p->vx = cos(angle) * speed;
        p->vy = sin(angle) * speed;
        p->life = 1.0f;
        p->maxLife = random_float(30.0f, 60.0f);
        p->brightness = 1.0f;
        p->r = r;
        p->g = g;
        p->b = b;
        p->active = 1;
    }
}

// Update fireworks - random explosion height
void update_fireworks() {
    for (int i = 0; i < MAX_FIREWORKS; i++) {
        Firework* fw = &fireworks[i];
        if (!fw->active) continue;

        fw->x += fw->vx;
        fw->y += fw->vy;
        fw->vy += 0.1f;

        // Random explosion height: 10%-50% of screen height (Y axis points down, smaller value means higher)
        float explode_height = screen_height * random_float(0.1f, 0.3f);

        if (fw->vy > -1.0f || fw->y < explode_height) {
            create_explosion(fw->x, fw->y, fw->r, fw->g, fw->b);
            fw->active = 0;
        }
    }
}

// Update particles - enhanced gravity effect
void update_particles() {
    int i = 0;
    while (i < num_particles) {
        Particle* p = &particles[i];
        if (!p->active) {
            i++;
            continue;
        }

        p->x += p->vx;
        p->y += p->vy;
        p->vy += 0.12f; // Enhanced gravity
        p->life -= 1.0f / p->maxLife;

        // Air resistance
        p->vx *= 0.985f;
        p->vy *= 0.985f;

        if (p->life <= 0) {
            p->active = 0;
            particles[i] = particles[--num_particles];
        } else {
            i++;
        }
    }
}

// Draw pixel
void set_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < screen_width && y >= 0 && y < screen_height) {
        bbufpix[y * screen_width + x] = color;
    }
}

// Draw pixel with alpha blending
void blend_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x < 0 || x >= screen_width || y < 0 || y >= screen_height) return;

    long idx = (long)y * screen_width + x;
    uint32_t dst = bbufpix[idx];

    uint8_t dstR = (dst >> 16) & 0xFF;
    uint8_t dstG = (dst >> 8) & 0xFF;
    uint8_t dstB = dst & 0xFF;

    uint8_t outR = (r * a + dstR * (255 - a)) / 255;
    uint8_t outG = (g * a + dstG * (255 - a)) / 255;
    uint8_t outB = (b * a + dstB * (255 - a)) / 255;

    bbufpix[idx] = 0xFF000000 | (outR << 16) | (outG << 8) | outB;
}

// Draw glowing pixel - larger and brighter
void draw_glow_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, float alpha) {
    if (alpha <= 0) return;

    // Increase brightness
    uint8_t bright_r = (uint8_t)fminf(255, r * 1.5f);
    uint8_t bright_g = (uint8_t)fminf(255, g * 1.5f);
    uint8_t bright_b = (uint8_t)fminf(255, b * 1.5f);

    uint8_t a = (uint8_t)(alpha * 255);

    // Center point - larger 3x3
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            float factor = (dx == 0 && dy == 0) ? 1.0f : 0.85f;
            blend_pixel(x+dx, y+dy, bright_r, bright_g, bright_b, (uint8_t)(a * factor));
        }
    }

    // Surrounding glow - larger 5x5 area
    uint8_t glow_a = a / 2;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            if (abs(dx) <= 1 && abs(dy) <= 1) continue; // Skip center 3x3
            float factor = 1.0f - (abs(dx) + abs(dy)) * 0.15f;
            if (factor > 0) {
                blend_pixel(x+dx, y+dy, bright_r, bright_g, bright_b, (uint8_t)(glow_a * factor));
            }
        }
    }
}

// Draw dotted trail effect - longer trail
void draw_dotted_trail(float x0, float y0, float x1, float y1,
                       uint8_t r, uint8_t g, uint8_t b, float alpha) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float dist = sqrtf(dx * dx + dy * dy);

    if (dist < 1.0f) {
        draw_glow_pixel((int)x0, (int)y0, r, g, b, alpha);
        return;
    }

    // Dot spacing - one dot every 3-4 pixels, larger grain
    float dot_spacing = 3.5f;
    int num_dots = (int)(dist / dot_spacing);
    if (num_dots < 1) num_dots = 1;
    if (num_dots > 30) num_dots = 30; // cap the max number of dots

    for (int i = 0; i <= num_dots; i++) {
        float t = (float)i / num_dots;
        float x = x0 + dx * t;
        float y = y0 + dy * t;

        // Head bright, tail dim, but tail is brighter (obvious trail)
        float dot_alpha = alpha * (0.6f + 0.4f * (1.0f - t));
        if (dot_alpha > 0.05f) {
            draw_glow_pixel((int)x, (int)y, r, g, b, dot_alpha);
        }
    }
}

// Draw all particles - dotted firework effect, independent of screen ratio
void draw_particles() {
    if (!bbufpix) return;

    for (int i = 0; i < num_particles; i++) {
        Particle* p = &particles[i];
        if (!p->active) continue;

        // Calculate trail - only related to velocity, independent of screen ratio
        float speed = sqrtf(p->vx * p->vx + p->vy * p->vy);
        float tail_len = speed * 3.0f;
        if (tail_len > 18.0f) tail_len = 18.0f;
        if (tail_len < 3.0f) tail_len = 3.0f;

        float x0 = p->x;
        float y0 = p->y;
        // Trail direction is opposite to velocity direction, maintaining isotropy
        float x1 = p->x - p->vx * tail_len * 0.5f;
        float y1 = p->y - p->vy * tail_len * 0.5f;

        uint8_t r = (uint8_t)(p->r * 255 * p->brightness);
        uint8_t g = (uint8_t)(p->g * 255 * p->brightness);
        uint8_t b = (uint8_t)(p->b * 255 * p->brightness);

        // Draw dotted trail
        draw_dotted_trail(x0, y0, x1, y1, r, g, b, p->life);
    }
}

// Draw all rockets
void draw_fireworks() {
    if (!bbufpix) return;

    for (int i = 0; i < MAX_FIREWORKS; i++) {
        Firework* fw = &fireworks[i];
        if (!fw->active) continue;

        int px = (int)fw->x;
        int py = (int)fw->y;

        uint8_t r = (uint8_t)(fw->r * 255);
        uint8_t g = (uint8_t)(fw->g * 255);
        uint8_t b = (uint8_t)(fw->b * 255);

        // Rocket head glow
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                int dist = dx*dx + dy*dy;
                if (dist <= 4) {
                    uint8_t alpha = 255 - dist * 40;
                    blend_pixel(px + dx, py + dy, r, g, b, alpha);
                }
            }
        }

        // Rocket dotted trail
        for (int j = 1; j <= 12; j++) {
            float t = j * 0.8f;
            int tx = (int)(fw->x - fw->vx * t);
            int ty = (int)(fw->y - fw->vy * t);
            uint8_t alpha = (uint8_t)(200 - j * 15);
            draw_glow_pixel(tx, ty, r, g, b, alpha / 255.0f);
        }
    }
}

/* Composite one animation frame into bbufpix, upload it and present it as a
 * fullscreen textured quad. */
static int frame_count = 0;

static void render_frame(void)
{
    EGLint w = 0, h = 0;

    eglQuerySurface(egl_dpy, egl_surf, EGL_WIDTH, &w);
    eglQuerySurface(egl_dpy, egl_surf, EGL_HEIGHT, &h);
    if (w <= 0 || h <= 0) { w = WIDTH; h = HEIGHT; }
    if (!ensure_buffer(w, h))
        return;

    // Random launch timing, no need to wait for previous to finish
    if (frame_count >= next_firework_time) {
        launch_firework();
        // Next launch time: 10-60 frames later (about 0.16-1 second)
        next_firework_time = frame_count + rand() % 50 + 10;
    }

    update_fireworks();
    update_particles();

    // Clear the CPU framebuffer to opaque black, then composite the show.
    long total_pixels = (long)screen_width * screen_height;
    for (long i = 0; i < total_pixels; ++i)
        bbufpix[i] = 0xFF000000;

    draw_fireworks();
    draw_particles();

    // Convert 0xAARRGGBB words to tightly-packed RGBA8 for the texture upload.
    for (long i = 0; i < total_pixels; ++i) {
        uint32_t px = bbufpix[i];
        rgba_buf[i * 4 + 0] = (px >> 16) & 0xFF;   /* r */
        rgba_buf[i * 4 + 1] = (px >> 8)  & 0xFF;   /* g */
        rgba_buf[i * 4 + 2] = px & 0xFF;           /* b */
        rgba_buf[i * 4 + 3] = (px >> 24) & 0xFF;   /* a */
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, screen_width, screen_height,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba_buf);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), 0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                          (void*)(2 * sizeof(GLfloat)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);

    eglSwapBuffers(egl_dpy, egl_surf);
    frame_count++;
}

static void on_event(xwin_t* xwin, xevent_t* ev)
{
    if (ev->type == XEVT_IM && ev->state == XIM_STATE_PRESS && ev->value.im.value == KEY_ESC)
        xwin_close(xwin);
}

static void loop(void* p)
{
    (void)p;
    render_frame();
    usleep(16000);
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

    (void)argc; (void)argv;
    srand((unsigned int)time(NULL));

    // Random seed initialization for launch timing
    next_firework_time = rand() % 30 + 10;

    x_init(&x, NULL);
    x.on_loop = loop;

    xwin = xwin_open(&x, -1, 32, 32, WIDTH, HEIGHT, "Fireworks", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("Failed to open window\n");
        return 1;
    }
    xwin->on_event = on_event;

    /* libEGL owns on_repaint/on_resize once the window surface is created. */
    if (!init_egl(xwin)) {
        printf("Failed to initialize EGL\n");
        return 1;
    }

    init_gl();

    xwin_set_visible(xwin, true);
    x_run(&x, xwin);

    xwin_destroy(xwin);
    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl_dpy, egl_surf);
    eglDestroyContext(egl_dpy, egl_ctx);
    eglTerminate(egl_dpy);

    free(bbufpix);
    free(rgba_buf);
    return 0;
}
