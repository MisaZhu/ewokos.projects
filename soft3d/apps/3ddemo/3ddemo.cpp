
#include "glcommon/gltools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <unistd.h>
#include <ewoksys/proc.h>
#include <ewoksys/keydef.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/timer.h>
#include <x/x.h>
#include <x/xwin.h>
#include <graph/graph.h>
#include <font/font.h>

#define sinf pgl_fast_sin
#define cosf pgl_fast_cos
#define sqrtf pgl_fast_sqrt
#define tanf pgl_fast_tan
#define acosf pgl_fast_acos
#define asinf pgl_fast_asin
#define atanf pgl_fast_atan
#define atanf2 pgl_fast_atan2

static int win_width = 640;
static int win_height = 480;
static glContext the_Context;
static pix_t* backbuf = NULL;
static xwin_t* g_xwin = NULL;
static font_t* font = NULL;

#include "src/obj.c"
#include "src/texture.c"

static GLuint program, vao, vbo;
static GLuint texture;

struct My_Uniforms {
	mat4 mvp_mat;
	GLuint tex0;
};
static My_Uniforms the_uniforms;

#ifdef PGL_G3D
#include <portablegl/pgl_g3d.h>
#include "3ddemo_glsl.h"

/* Full-GPU path (mirrors teapot.c / gears.c): the model's vertex transform and
 * its texture-modulate fragment shading are compiled to VideoCore VII QPU and
 * run on the V3D GPU.  g3d_active latches on only after the program compiles,
 * the uMVP uniform resolves by name, the pos+uv mesh uploads, and the texture
 * is staged as a linear image bound to sampler unit 0; any decline leaves it 0
 * and the CPU basic_transform_vp + pgl_ff_tex_modulate path below renders the
 * identical design.  On the GPU the draw is rasterised by the textured geom
 * compute kernel (geom_raster_tex_cs_glsl), not the binner. */
static int          g3d_active = 0;
static pgl_g3d_prog g3d_prog   = PGL_G3D_PROG_INVALID;
static pgl_g3d_mesh g3d_mesh   = PGL_G3D_MESH_INVALID;
static uint32_t     g3d_mesh_count = 0;
static int          g3d_tex_slot   = -1;
static int          g3d_off_mvp = -1;
static uint32_t     g3d_unif[PGL_G3D_MAX_UNIF];
static uint32_t     g3d_unif_count = 0;

/* EwokOS demo binaries are linked with ld directly and the C runtime _start
 * never calls __libc_init_array, so the C++ static constructors gathered in
 * .init_array are never run.  The on-device mesa GLSL->QPU compiler
 * (v3d_compile_glsl, pulled in through libv3dcompiler.a) depends on two of them
 * (the glsl type system and the built-in function table); without them the
 * first compile dereferences unconstructed globals and faults.  Run them here,
 * before any shader is compiled (same guard shadertoy.cpp uses). */
extern "C" void (*__init_array_start[])(void);
extern "C" void (*__init_array_end[])(void);
static void threeddemo_run_static_ctors(void)
{
    void (**p)(void);
    for (p = __init_array_start; p < __init_array_end; ++p)
        if (*p != 0)
            (*p)();
}
#endif /* PGL_G3D */

void basic_transform_vp(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms) {
    (void)vs_output;
    My_Uniforms* u = (My_Uniforms*)uniforms;
    builtins->gl_Position = mult_m4_v4(u->mvp_mat, vertex_attribs[0]);
    vs_output[0] = vertex_attribs[1].x;
    vs_output[1] = 1.0f - vertex_attribs[1].y; // Flip V coordinate
}

/* Phase D: the fragment shader is the built-in pgl_ff_tex_modulate (declared in
   portablegl.h), which calls texture2D(uniforms->tex0, fs_input[0], fs_input[1]).
   My_Uniforms { mat4 mvp_mat; GLuint tex0; } is byte-layout-identical to
   pgl_ff_tex_modulate_uniform, so the demo keeps its own VS (which flips V) and
   just swaps the FS pointer.  On raspi5 with PGL_G3D the matching triangles are
   offloaded to the V3D GPU's TEX_MOD kernel (point-sampled, CLAMP-to-edge,
   tightly-packed RGBA8); the texture params below are set to NEAREST +
   CLAMP_TO_EDGE so the GPU gate accepts them. */

static void init_mesh() {
    float* vertex_data = (float*)malloc(num_faces * 3 * 5 * sizeof(float));
    int idx = 0;
    for (int i = 0; i < num_faces; i++) {
        for (int j = 0; j < 3; j++) {
            int vidx;
            float u, v;
            if (j == 0) {
                vidx = faces[i].a - 1;
                u = faces[i].a_uv.u;
                v = faces[i].a_uv.v;
            } else if (j == 1) {
                vidx = faces[i].b - 1;
                u = faces[i].b_uv.u;
                v = faces[i].b_uv.v;
            } else {
                vidx = faces[i].c - 1;
                u = faces[i].c_uv.u;
                v = faces[i].c_uv.v;
            }

            vertex_data[idx++] = vertices[vidx].x;
            vertex_data[idx++] = vertices[vidx].y;
            vertex_data[idx++] = vertices[vidx].z;
            vertex_data[idx++] = u;
            vertex_data[idx++] = v;
        }
    }

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, num_faces * 3 * 5 * sizeof(float), vertex_data, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    free(vertex_data);
}

static void init_texture() {
    extern int texture_width;
    extern int texture_height;
    extern uint32_t* mesh_texture;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    int num_pixels = texture_width * texture_height;
    uint8_t* rgba_data = (uint8_t*)malloc(num_pixels * 4);
    for (int i = 0; i < num_pixels; i++) {
        uint32_t argb = ((uint32_t*)mesh_texture)[i];
        uint8_t a = (argb >> 24) & 0xFF;
        uint8_t r = (argb >> 16) & 0xFF;
        uint8_t g = (argb >> 8) & 0xFF;
        uint8_t b = argb & 0xFF;
        rgba_data[i * 4 + 0] = r;
        rgba_data[i * 4 + 1] = g;
        rgba_data[i * 4 + 2] = b;
        rgba_data[i * 4 + 3] = a;
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture_width, texture_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    free(rgba_data);
}

#ifdef PGL_G3D
/* ------------------------------------------------------------------ *
 * Full-GPU path: pack the per-draw uniform blob (the single uMVP mat4) and
 * issue one compiled-mesh draw.  The matrix is uploaded in portablegl's
 * column-major flat order (3ddemo_glsl.h documents why this is bit-identical
 * to the C mult_m4_v4 path).
 * ------------------------------------------------------------------ */
static void g3d_set_f(int off, float v)
{
    uint32_t bits;
    if (off < 0 || (uint32_t)off >= PGL_G3D_MAX_UNIF)
        return;
    memcpy(&bits, &v, sizeof(bits));
    g3d_unif[off] = bits;
    if ((uint32_t)off + 1u > g3d_unif_count)
        g3d_unif_count = (uint32_t)off + 1u;
}

static void g3d_set_mat4(int off, const float* m)
{
    for (int i = 0; i < 16; i++)
        g3d_set_f(off + i, m[i]);
}

/* Compile the VS+FS to QPU, resolve uMVP by name, stage the pos+uv mesh (padded
 * to float3 per attribute so pgl_g3d_upload_mesh reads position at attr0 and
 * (u,v,0) at attr1), then tile the texture into slot 0 and bind it to sampler
 * unit 0.  On any decline g3d_active stays 0 and the CPU path renders.  Called
 * once at the end of init_gl() (after pglGpuBegin()). */
static void g3d_init()
{
    extern int texture_width;
    extern int texture_height;
    extern uint32_t* mesh_texture;

    char errlog[512];
    uint32_t comps;
    g3d_active = 0;
    errlog[0] = '\0';

    g3d_prog = pgl_g3d_program_compile(threeddemo_vs_glsl, threeddemo_fs_glsl,
                                       errlog, sizeof(errlog));
    if (!pgl_g3d_program_valid(g3d_prog)) {
        printf("3ddemo: GPU shader compile failed: %s\n", errlog);
        return;
    }

    g3d_off_mvp = pgl_g3d_shader_uniform_dword(g3d_prog, "uMVP", &comps);
    if (g3d_off_mvp < 0 || comps != 16) {
        printf("3ddemo: uMVP unresolved\n");
        return;
    }

    /* Build a stride-6 (pos float3 + uv float3, uv padded with 0) triangle list
     * mirroring init_mesh's stride-5 layout, so the compiled-mesh path stages
     * position at attribute 0 and (u,v,0) at attribute 1. */
    int nverts = num_faces * 3;
    float* vdata = (float*)malloc((size_t)nverts * 6 * sizeof(float));
    if (vdata == NULL)
        return;
    int idx = 0;
    for (int i = 0; i < num_faces; i++) {
        for (int j = 0; j < 3; j++) {
            int vidx; float u, v;
            if (j == 0)      { vidx = faces[i].a - 1; u = faces[i].a_uv.u; v = faces[i].a_uv.v; }
            else if (j == 1) { vidx = faces[i].b - 1; u = faces[i].b_uv.u; v = faces[i].b_uv.v; }
            else             { vidx = faces[i].c - 1; u = faces[i].c_uv.u; v = faces[i].c_uv.v; }
            vdata[idx++] = vertices[vidx].x;
            vdata[idx++] = vertices[vidx].y;
            vdata[idx++] = vertices[vidx].z;
            vdata[idx++] = u;
            vdata[idx++] = v;
            vdata[idx++] = 0.0f;   /* pad uv to float3 for attribute 1 */
        }
    }
    g3d_mesh = pgl_g3d_upload_mesh(vdata, (uint32_t)nverts, 6u);
    g3d_mesh_count = (uint32_t)nverts;
    free(vdata);
    if (g3d_mesh == PGL_G3D_MESH_INVALID) {
        printf("3ddemo: GPU mesh upload declined\n");
        return;
    }

    /* Upload the texture as a LINEAR (raster) image: the textured geom
     * rasterizer kernel indexes it as a raw uint32 array (NEAREST + CLAMP,
     * no TMU), and mesh_texture's uint32 words are already 0xAARRGGBB - the
     * exact fb packing the kernel writes - so the data goes up unshuffled.
     * Then bind the slot to sampler unit 0 (the FS's iChannel0). */
    {
        int ok = pgl_g3d_upload_texture(0, texture_width, texture_height,
                                        mesh_texture);
        if (!ok) {
            printf("3ddemo: GPU texture upload declined\n");
            return;
        }
        g3d_tex_slot = 0;
        pgl_g3d_bind_tex_unit(0, g3d_tex_slot);
    }

    g3d_active = 1;
}
#endif /* PGL_G3D */

void init_gl() {
	GLenum interpolation[2] = { PGL_SMOOTH, PGL_SMOOTH };
	program = CreateProgram(basic_transform_vp, pgl_ff_tex_modulate, 2, interpolation, GL_FALSE);
	glUseProgram(program);
	SetUniform(&the_uniforms);

	init_mesh();
	init_texture();

	glViewport(0, 0, win_width, win_height);
	glEnable(GL_DEPTH_TEST);

	the_uniforms.tex0 = texture;
	printf("Texture ID: %u\n", texture);

#ifdef PGL_G3D
	/* Compile the VS+FS to QPU, stage the mesh and bind the texture now that
	 * the geometry/texture exist and (if pglGpuBegin succeeded) the GPU target
	 * is up.  Any decline leaves g3d_active = 0 and the CPU path renders. */
	g3d_init();
#endif
}

static float camera_angle = 0;

static int fps = 0;
static int count = 0;
static uint32_t last_tic = 0;

static void update_fps() {
    uint32_t low;
    kernel_tic32(NULL, NULL, &low);

    /* Update FPS statistics once per second */
    if (last_tic == 0 || (low - last_tic) >= 1000000) {
        last_tic = low;
        fps = count;
        count = 0;
    }
    count++;
}

static void render_mesh() {
    glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    mat4 view_mat, proj_mat, model_mat, mvp_mat, scale_mat;

    // Camera orbits the model horizontally (like gears)
    float cam_radius = 4.0f;
    float cam_x = sinf(camera_angle) * cam_radius;
    float cam_z = cosf(camera_angle) * cam_radius;
    float cam_y = 1.0f;
    
    // Camera always looks at the model center
    lookAt(view_mat, make_v3(cam_x, cam_y, cam_z), make_v3(0.0f, 0.0f, 0.0f), make_v3(0.0f, 1.0f, 0.0f));
    make_perspective_m4(proj_mat, DEG_TO_RAD(60.0f), win_width / (float)win_height, 0.1f, 100.0f);

    // The model does not rotate, only scales (the model center is already offset via the vertex data)
    scale_m4(scale_mat, 1.0f, 1.0f, 1.0f);
    memcpy(model_mat, scale_mat, sizeof(mat4));

    mult_m4_m4(mvp_mat, proj_mat, view_mat);
    mult_m4_m4(mvp_mat, mvp_mat, model_mat);

    memcpy(the_uniforms.mvp_mat, mvp_mat, sizeof(float) * 16);

#ifdef PGL_G3D
    /* Full-GPU path: pack uMVP, (re)bind the texture to sampler unit 0, and
     * issue one compiled-mesh draw (VS transform + texture sample on the QPU).
     * glClear above already drove pgl_g3d_begin_frame, so this frame's dispatch
     * clears the persistent colour+Z.  GL_LESS depth + Z-write, no cull (the CPU
     * path enables only GL_DEPTH_TEST).  On decline latch the GPU off, re-clear
     * and fall through to the CPU draw. */
    if (g3d_active) {
        g3d_unif_count = 0;
        g3d_set_mat4(g3d_off_mvp, mvp_mat);
        pgl_g3d_set_uniforms(g3d_unif, g3d_unif_count);
        pgl_g3d_bind_tex_unit(0, g3d_tex_slot);
        if (pgl_g3d_draw_mesh_compiled(g3d_prog, g3d_mesh, 0u, g3d_mesh_count,
                                       PGL_G3D_DEPTH_LESS, 1u, PGL_G3D_CULL_NONE))
            return;   /* model rendered on the QPU */
        g3d_active = 0;
        glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
#endif /* PGL_G3D */

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, num_faces * 3);
}

static void on_repaint(xwin_t* xwin, graph_t* g) {
    (void)xwin;
    camera_angle += 0.05f;
    render_mesh();
    /* The GPU path adopts pgl_g3d_pixels() as the back buffer on its first
     * draw (pglGpuBegin frees the CPU buffer init_glContext handed us), so
     * re-read the live pointer every frame instead of only in on_resize -
     * otherwise the first frames blit a stale, freed buffer and the window
     * stays blank until a resize. */
    backbuf = (pix_t*)GetBackBuffer();
    graph_fill_rect(g, 0, 0, win_width, win_height, 0xFF1a1a2e);
    graph_t bg;
    graph_init(&bg, backbuf, win_width, win_height);
    graph_blt(&bg, 0, 0, win_width, win_height, g, 0, 0, win_width, win_height);
    update_fps();

    if (font != NULL) {
        char txt[48];
#ifdef PGL_G3D
        snprintf(txt, sizeof(txt), "FPS:%d gpu:%d %dx%d", fps, g3d_active, win_width, win_height);
#else
        snprintf(txt, sizeof(txt), "FPS: %d", fps);
#endif
        graph_draw_text_font(g, 8, 8, txt, font, 16, 0xff00ff00);
    }
}

static void on_event(xwin_t* xwin, xevent_t* ev) {
    if (ev->type == XEVT_IM && ev->state == XIM_STATE_PRESS && ev->value.im.value == KEY_ESC)
        xwin_close(xwin);
}

static void on_resize(xwin_t* xwin) {
    if (!xwin || !xwin->xinfo) return;
    win_width = xwin->xinfo->wsr.w;
    win_height = xwin->xinfo->wsr.h;
    if (win_width <= 0 || win_height <= 0) return;
    ResizeFramebuffer(win_width, win_height);
    backbuf = (pix_t*)GetBackBuffer();
    glViewport(0, 0, win_width, win_height);
    glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void loop(void* p) {
    xwin_t* xwin = (xwin_t*)p;
    xwin_repaint(xwin);
    usleep(1000);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
#ifdef PGL_G3D
    /* Run the C++ static constructors the on-device mesa compiler needs before
     * the first v3d_compile_glsl() (see threeddemo_run_static_ctors). */
    threeddemo_run_static_ctors();
#endif
    x_t x;
    x_init(&x, NULL);
    x.on_loop = loop;
    xwin_t* xwin = xwin_open(&x, -1, 32, 32, win_width, win_height, "3D F22 Demo", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("Failed to open window\n");
        return 1;
    }
    xwin_set_alpha(xwin, false);
    g_xwin = xwin;
    xwin->on_repaint = on_repaint;
    xwin->on_event = on_event;
    xwin->on_resize = on_resize;

    pgl_set_max_vertices(PGL_TINY_MAX_VERTICES);
    if (!init_glContext(&the_Context, (u32**)&backbuf, win_width, win_height)) {
        printf("Failed to initialize glContext\n");
        return 1;
    }

#ifdef PGL_G3D
    /* Bring up the V3D GPU target and adopt pgl_g3d_pixels() as the back buffer,
     * so the compiled-mesh draw and the CPU fallback share one surface and the
     * present blit stays zero-copy.  On success refresh backbuf; on failure the
     * CPU back buffer from init_glContext stands (g3d_init then declines and the
     * CPU path renders). */
    if (pglGpuBegin(win_width, win_height))
        backbuf = (pix_t*)pglGetBackBuffer();
#endif

    init_gl();
    font = font_new(DEFAULT_SYSTEM_FONT, true);
    xwin_set_visible(xwin, true);
    x_run(&x, xwin);
    xwin_destroy(xwin);
    if (font != NULL)
        font_free(font);
    free_glContext(&the_Context);
    return 0;
}
