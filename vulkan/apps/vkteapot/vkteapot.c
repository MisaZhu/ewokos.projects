/*
 * vkteapot.c - the classic Newell Utah teapot rendered through the EwokOS
 * Vulkan software device, the Vulkan twin of projects/opengl/apps/teapot.
 *
 * The teapot is 32 bicubic Bezier patches over 306 control points, tessellated
 * once at start-up into a smooth triangle list (10x10 quads per patch -> 19200
 * non-indexed vertices) with analytic per-vertex normals.  It spins about its
 * own axis while the camera orbits it, exactly like the GL demo.
 *
 * Lighting is the same per-vertex two-sided Blinn-Phong model (ambient 0.22 +
 * diffuse 0.78 + specular 1.0, material 0.85/0.32/0.20) evaluated in eye space
 * against a world-fixed light -- but here it runs on the CPU (compute_colors)
 * and is uploaded as a per-vertex vec4 colour attribute, so the embedded vertex
 * shader is a pure transform+copy passthrough (vColor = aColor; gl_Position =
 * uMVP * aPosition) and the fragment shader just forwards the interpolated
 * colour.  Depth is a resident D32_SFLOAT attachment arbitrated LESS+write, so
 * the spout/handle occlude correctly.
 *
 * Presentation rides the runtime's EwokOS WSI path exactly as in bin/triangle:
 * a VkSurfaceKHR is created with vkCreateEwokOSSurfaceVK and a present callback
 * blits the rendered B8G8R8A8 pixels straight into the window graph.  On raspi5
 * (PGL_G3D) this draw is inside the GPU subset -- a TRIANGLE_LIST into one
 * BGRA8 attachment, LESS+write depth, colour+depth cleared, trivial VS/FS -- so
 * the runtime transpiles the two SPIR-V stages into a geom-raster kernel pair
 * and dispatches them on the V3D CSD; anything outside the subset, or a kernel
 * failure, transparently declines to the CPU rasteriser, so the presented pixels
 * are identical either way.
 */
#include <vulkan/vulkan.h>
#include <x/x.h>
#include <x/xwin.h>
#include <x/xcntl.h>
#include <graph/graph.h>
#include <ewoksys/kernel_tic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "teapot_data.h"
#include "vkteapot_spirv.h"

/* the EwokOS surface entry (a WSI extension: resolve via proc addr) */
typedef VkResult (VKAPI_PTR *PFN_vkCreateEwokOSSurfaceVK_t)(
    VkInstance instance, uint32_t width, uint32_t height, VkFormat format,
    void *present_fn, void *user, VkSurfaceKHR *pSurface);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TEAPOT_W 640
#define TEAPOT_H 480

/* ---- teapot mesh --------------------------------------------------------- */
/* CPU-side geometry: 6 floats/vertex (x,y,z, nx,ny,nz), lit per frame into a
 * 4-float RGBA colour array.  The GPU-visible position buffer holds vec4
 * (x,y,z,1) and the colour buffer vec4 RGBA, both nvertices long. */
typedef struct {
    float   *vertices;      /* nvertices * 6 : pos + normal                */
    float   *colors;        /* nvertices * 4 : rgba, recomputed each frame  */
    int      nvertices;
} teapot_mesh;

static teapot_mesh teapot;

static float spin_angle   = 0.0f;   /* teapot rotation about its own axis */
static float camera_angle = 0.0f;   /* camera orbit about the teapot      */
static float camera_elev  = 15.0f;  /* camera elevation (degrees)         */
static float projection_matrix[16];

/* ---- app state ----------------------------------------------------------- */
#define MAX_SC_IMAGES 4

/* one host-visible buffer + its mapping */
typedef struct {
    VkBuffer       buf;
    VkDeviceMemory mem;
    void          *map;
    VkDeviceSize   size;
} hbuf_t;

typedef struct {
    VkInstance       instance;
    VkSurfaceKHR     surface;
    VkPhysicalDevice phys;
    VkDevice         device;
    VkQueue          queue;
    uint32_t         qfi;
    VkSwapchainKHR   swapchain;
    VkFormat         format;
    uint32_t         width, height;
    VkImage          sc_images[MAX_SC_IMAGES];
    VkImageView      sc_views[MAX_SC_IMAGES];
    VkFramebuffer    sc_fbs[MAX_SC_IMAGES];
    uint32_t         sc_count;
    /* depth attachment (resident D32_SFLOAT, shared by every framebuffer) */
    VkFormat         depth_format;
    VkImage          depth_img;
    VkDeviceMemory   depth_mem;
    VkImageView      depth_view;
    VkRenderPass     rp;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout pl;
    VkPipeline       pipeline;
    VkDescriptorPool dpool;
    VkDescriptorSet  dset;
    /* resources */
    hbuf_t           pos;      /* vec4 position (x,y,z,1), static          */
    hbuf_t           col;      /* vec4 colour, refreshed each frame        */
    hbuf_t           ubo;      /* mat4 uMVP (64 bytes)                     */
    VkCommandPool    cpool;
    VkCommandBuffer  cmd;
    VkFence          fence;
    VkSemaphore      acquired;
    int              ready;
} vk_teapot;

static vk_teapot T;
static graph_t *g_cur;     /* window graph for the frame being presented */
static int  g_frames = 0;
/* Optional FPS log path taken from argv[1].  The EwokOS ssh exec channel does
 * not forward an app's stdout, so on-device FPS is measured by having the app
 * write it to a file itself (fetched back separately).  NULL on a normal
 * desktop launch, which leaves behaviour unchanged. */
static const char *g_fpslog = NULL;

/* present callback: blit the rendered swapchain image into the window graph. */
static void VKAPI_CALL teapot_present(void *user, const void *pixels,
                                      uint32_t width, uint32_t height,
                                      uint32_t stride_bytes, VkFormat format,
                                      int shm_id)
{
    graph_t *g = g_cur;
    const uint8_t *src = (const uint8_t *)pixels;
    uint32_t cw, ch, y;
    (void)user;
    if (!src) return;
    /* only the byte-identical B8G8R8A8 path is copied straight through */
    if (format != VK_FORMAT_B8G8R8A8_UNORM) return;
    if (!g || !g->buffer) return;
    if (shm_id > 0) {
        graph_t sg;
        graph_init(&sg, (const uint32_t *)src, width, height);
        sg.shm_id = shm_id;
        sg.shm_contig = 1;
        graph_blt(&sg, 0, 0, width, height, g, 0, 0, g->w, g->h);
        return;
    }
    cw = width  < (uint32_t)g->w ? width  : (uint32_t)g->w;
    ch = height < (uint32_t)g->h ? height : (uint32_t)g->h;
    for (y = 0; y < ch; y++) {
        uint32_t *dst = g->buffer + (size_t)y * g->w;
        memcpy(dst, src + (size_t)y * stride_bytes, (size_t)cw * 4);
    }
}

/* ------------------------------------------------------------------ *
 * Minimal matrix helpers (row-vector convention, column-major storage,
 * identical to projects/opengl/apps/teapot so the teapot is oriented
 * and lit the same way).  The 16-float arrays are uploaded verbatim as
 * the std140 column-major uMVP the vertex shader multiplies.
 * ------------------------------------------------------------------ */
static void multiply(float *m, const float *n)
{
    float tmp[16];
    const float *row, *column;
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

static void rotate(float *m, float angle, float x, float y, float z)
{
    float c = cosf(angle * (float)M_PI / 180.0f);
    float s = sinf(angle * (float)M_PI / 180.0f);
    float r[16] = {
        x * x * (1 - c) + c,     y * x * (1 - c) + z * s, x * z * (1 - c) - y * s, 0,
        x * y * (1 - c) - z * s, y * y * (1 - c) + c,     y * z * (1 - c) + x * s, 0,
        x * z * (1 - c) + y * s, y * z * (1 - c) - x * s, z * z * (1 - c) + c,     0,
        0, 0, 0, 1
    };
    multiply(m, r);
}

static void translate(float *m, float x, float y, float z)
{
    float t[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  x, y, z, 1 };
    multiply(m, t);
}

static void identity(float *m)
{
    float t[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    memcpy(m, t, sizeof(t));
}

static void transpose(float *m)
{
    float t[16] = {
        m[0], m[4], m[8],  m[12],
        m[1], m[5], m[9],  m[13],
        m[2], m[6], m[10], m[14],
        m[3], m[7], m[11], m[15]
    };
    memcpy(m, t, sizeof(t));
}

static void invert(float *m)
{
    float t[16];
    identity(t);
    t[12] = -m[12]; t[13] = -m[13]; t[14] = -m[14];
    m[12] = m[13] = m[14] = 0;
    transpose(m);
    multiply(m, t);
}

static void perspective(float *m, float fovy, float aspect, float z_near, float z_far)
{
    float tmp[16];
    float radians, sine, cosine, delta_z, cotangent;
    identity(tmp);
    radians = fovy / 2.0f * (float)M_PI / 180.0f;
    sine = sinf(radians);
    cosine = cosf(radians);
    delta_z = z_far - z_near;
    if (delta_z == 0 || sine == 0 || aspect == 0)
        return;
    cotangent = cosine / sine;
    tmp[0] = cotangent / aspect;
    tmp[5] = -cotangent;   /* negate Y: Vulkan framebuffer origin is top-left */
    tmp[10] = -(z_far + z_near) / delta_z;
    tmp[11] = -1.0f;
    tmp[14] = -2.0f * z_near * z_far / delta_z;
    tmp[15] = 0.0f;
    memcpy(m, tmp, sizeof(tmp));
}

static void look_at(float *m, float eye_x, float eye_y, float eye_z,
                    float center_x, float center_y, float center_z,
                    float up_x, float up_y, float up_z)
{
    float f[3], s[3], u[3];
    float f_len, s_len;
    float look_at_mat[16];

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

    look_at_mat[0]=s[0];  look_at_mat[1]=u[0];  look_at_mat[2]=-f[0]; look_at_mat[3]=0;
    look_at_mat[4]=s[1];  look_at_mat[5]=u[1];  look_at_mat[6]=-f[1]; look_at_mat[7]=0;
    look_at_mat[8]=s[2];  look_at_mat[9]=u[2];  look_at_mat[10]=-f[2];look_at_mat[11]=0;
    look_at_mat[12]=-(s[0]*eye_x + s[1]*eye_y + s[2]*eye_z);
    look_at_mat[13]=-(u[0]*eye_x + u[1]*eye_y + u[2]*eye_z);
    look_at_mat[14]= (f[0]*eye_x + f[1]*eye_y + f[2]*eye_z);
    look_at_mat[15]=1;
    memcpy(m, look_at_mat, sizeof(look_at_mat));
}

/* ------------------------------------------------------------------ *
 * Bezier patch tessellation.
 * ------------------------------------------------------------------ */
static void bernstein(float t, float b[4])
{
    float mt = 1.0f - t;
    b[0] = mt * mt * mt;
    b[1] = 3.0f * t * mt * mt;
    b[2] = 3.0f * t * t * mt;
    b[3] = t * t * t;
}

static void bernstein_d(float t, float d[4])
{
    float mt = 1.0f - t;
    d[0] = -3.0f * mt * mt;
    d[1] = 3.0f * mt * mt - 6.0f * t * mt;
    d[2] = 6.0f * t * mt - 3.0f * t * t;
    d[3] = 3.0f * t * t;
}

/* Evaluate one bicubic Bezier patch at (u,v); write position and the unit
 * surface normal (cross product of the u and v tangents). */
static void eval_patch(const int *pidx, float u, float v, float pos[3], float nrm[3])
{
    float bu[4], bv[4], du[4], dv[4];
    float px = 0, py = 0, pz = 0;
    float tu_x = 0, tu_y = 0, tu_z = 0;   /* dS/du */
    float tv_x = 0, tv_y = 0, tv_z = 0;   /* dS/dv */
    float nx, ny, nz, len;
    int i, j;

    bernstein(u, bu);
    bernstein(v, bv);
    bernstein_d(u, du);
    bernstein_d(v, dv);

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            const float *cp = cpdata[pidx[i * 4 + j] - 1];
            float w  = bu[i] * bv[j];
            float wu = du[i] * bv[j];
            float wv = bu[i] * dv[j];
            px += w * cp[0];  py += w * cp[1];  pz += w * cp[2];
            tu_x += wu * cp[0]; tu_y += wu * cp[1]; tu_z += wu * cp[2];
            tv_x += wv * cp[0]; tv_y += wv * cp[1]; tv_z += wv * cp[2];
        }
    }
    pos[0] = px; pos[1] = py; pos[2] = pz;

    nx = tu_y * tv_z - tu_z * tv_y;
    ny = tu_z * tv_x - tu_x * tv_z;
    nz = tu_x * tv_y - tu_y * tv_x;
    len = sqrtf(nx*nx + ny*ny + nz*nz);
    if (len > 1e-8f) {
        nx /= len; ny /= len; nz /= len;
    } else {
        nx = 0; ny = 0; nz = 1.0f;
    }
    nrm[0] = nx; nrm[1] = ny; nrm[2] = nz;
}

static float *emit_vert(float *v, const float *pos, const float *nrm)
{
    v[0] = pos[0]; v[1] = pos[1]; v[2] = pos[2];
    v[3] = nrm[0]; v[4] = nrm[1]; v[5] = nrm[2];
    return v + 6;
}

/* Tessellate all 32 patches into one non-indexed triangle list. */
static int build_teapot_geometry(void)
{
    const int G = TEAPOT_GRID;
    const int per_patch = G * G * 2 * 3;      /* triangles * 3 verts */
    const int total = NPATCH * per_patch;
    static float lat_pos[(TEAPOT_GRID + 1) * (TEAPOT_GRID + 1)][3];
    static float lat_nrm[(TEAPOT_GRID + 1) * (TEAPOT_GRID + 1)][3];
    float *v;
    int p, a, b;

    teapot.vertices = (float *)malloc((size_t)total * 6 * sizeof(float));
    teapot.colors   = (float *)malloc((size_t)total * 4 * sizeof(float));
    if (!teapot.vertices || !teapot.colors) {
        printf("vkteapot: out of memory building geometry\n");
        return -1;
    }

    v = teapot.vertices;
    for (p = 0; p < NPATCH; p++) {
        const int *pidx = patches[p];
        for (a = 0; a <= G; a++) {
            float u = (float)a / (float)G;
            for (b = 0; b <= G; b++) {
                float w = (float)b / (float)G;
                int idx = a * (G + 1) + b;
                eval_patch(pidx, u, w, lat_pos[idx], lat_nrm[idx]);
            }
        }
        for (a = 0; a < G; a++) {
            for (b = 0; b < G; b++) {
                int i00 = a * (G + 1) + b;
                int i10 = (a + 1) * (G + 1) + b;
                int i01 = a * (G + 1) + b + 1;
                int i11 = (a + 1) * (G + 1) + b + 1;
                v = emit_vert(v, lat_pos[i00], lat_nrm[i00]);
                v = emit_vert(v, lat_pos[i10], lat_nrm[i10]);
                v = emit_vert(v, lat_pos[i11], lat_nrm[i11]);
                v = emit_vert(v, lat_pos[i00], lat_nrm[i00]);
                v = emit_vert(v, lat_pos[i11], lat_nrm[i11]);
                v = emit_vert(v, lat_pos[i01], lat_nrm[i01]);
            }
        }
    }
    teapot.nvertices = (int)(v - teapot.vertices) / 6;
    return 0;
}

/* ------------------------------------------------------------------ *
 * Per-vertex lighting (formerly the GLSL vertex shader).
 * ------------------------------------------------------------------ */
/* Transform a point (w=1) by a column-major mat4. */
static void xform_point(const float *m, float x, float y, float z, float out[3])
{
    out[0] = m[0]*x + m[4]*y + m[8]*z  + m[12];
    out[1] = m[1]*x + m[5]*y + m[9]*z  + m[13];
    out[2] = m[2]*x + m[6]*y + m[10]*z + m[14];
}

/* Exponent-48 power by repeated squaring (48 == 32 + 16), avoiding libm powf. */
static float fpow48(float x)
{
    float x2 = x * x, x4 = x2 * x2, x8 = x4 * x4, x16 = x8 * x8, x32 = x16 * x16;
    return x32 * x16;
}

static void norm3(const float *v, float *o)
{
    float len = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (len < 1e-8f) { o[0] = 0.0f; o[1] = 0.0f; o[2] = 0.0f; return; }
    o[0] = v[0]/len; o[1] = v[1]/len; o[2] = v[2]/len;
}

/* Per-vertex two-sided Blinn-Phong in eye space against a positional light,
 * reproducing projects/opengl/apps/teapot's compute_colors exactly. */
static void compute_colors(const float *model_view, const float *normal_matrix,
                           const float *light_eye)
{
    const float mat[3] = { 0.85f, 0.32f, 0.20f };
    const float amb = 0.22f, dif = 0.78f, spc = 1.0f;
    int i;
    if (!teapot.vertices || !teapot.colors) return;
    for (i = 0; i < teapot.nvertices; ++i) {
        const float *v = teapot.vertices + (size_t)i * 6;
        float ep[3], ne[3], V[3], Ld[3], L[3], H[3], neg[3];
        float diff, ndoth, ndotv, spec;
        int c;

        xform_point(model_view, v[0], v[1], v[2], ep);          /* eye position */
        ne[0] = normal_matrix[0]*v[3] + normal_matrix[4]*v[4] + normal_matrix[8]*v[5];
        ne[1] = normal_matrix[1]*v[3] + normal_matrix[5]*v[4] + normal_matrix[9]*v[5];
        ne[2] = normal_matrix[2]*v[3] + normal_matrix[6]*v[4] + normal_matrix[10]*v[5];
        norm3(ne, ne);                                          /* eye normal */

        neg[0] = -ep[0]; neg[1] = -ep[1]; neg[2] = -ep[2];      /* eye at origin */
        norm3(neg, V);

        ndotv = ne[0]*V[0] + ne[1]*V[1] + ne[2]*V[2];           /* two-sided flip */
        if (ndotv < 0.0f) { ne[0] = -ne[0]; ne[1] = -ne[1]; ne[2] = -ne[2]; }

        Ld[0] = light_eye[0] - ep[0];                           /* positional light */
        Ld[1] = light_eye[1] - ep[1];
        Ld[2] = light_eye[2] - ep[2];
        norm3(Ld, L);

        diff = ne[0]*L[0] + ne[1]*L[1] + ne[2]*L[2];
        if (diff < 0.0f) diff = 0.0f;

        H[0] = L[0] + V[0]; H[1] = L[1] + V[1]; H[2] = L[2] + V[2];
        norm3(H, H);
        ndoth = ne[0]*H[0] + ne[1]*H[1] + ne[2]*H[2];
        if (ndoth < 0.0f) ndoth = 0.0f;
        spec = fpow48(ndoth);

        for (c = 0; c < 3; ++c) {
            float col = mat[c] * (amb + dif * diff) + spc * spec;
            if (col < 0.0f) col = 0.0f;
            if (col > 1.0f) col = 1.0f;
            teapot.colors[(size_t)i * 4 + c] = col;
        }
        teapot.colors[(size_t)i * 4 + 3] = 1.0f;
    }
}

/* ------------------------------------------------------------------ *
 * host-visible buffer helper
 * ------------------------------------------------------------------ */
static uint32_t find_memory_type(const VkPhysicalDeviceMemoryProperties *mem,
                                 uint32_t typeFilter, VkMemoryPropertyFlags want)
{
    uint32_t i;
    for (i = 0; i < mem->memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) &&
            (mem->memoryTypes[i].propertyFlags & want) == want)
            return i;
    }
    for (i = 0; i < mem->memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) &&
            (mem->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            return i;
    }
    return 0xffffffffu;
}

static int hbuf_create(VkDevice dev, VkPhysicalDevice pd, hbuf_t *hb,
                       VkDeviceSize size, VkBufferUsageFlags usage)
{
    VkBufferCreateInfo bci;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo mai;
    VkPhysicalDeviceMemoryProperties mem;
    uint32_t mt;

    memset(hb, 0, sizeof(*hb));
    hb->size = size;

    memset(&bci, 0, sizeof(bci));
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &bci, NULL, &hb->buf) != VK_SUCCESS) {
        printf("vkteapot: vkCreateBuffer failed\n");
        return -1;
    }
    vkGetBufferMemoryRequirements(dev, hb->buf, &req);
    vkGetPhysicalDeviceMemoryProperties(pd, &mem);
    mt = find_memory_type(&mem, req.memoryTypeBits,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == 0xffffffffu) { printf("vkteapot: no host-visible memory type\n"); return -1; }

    memset(&mai, 0, sizeof(mai));
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mt;
    if (vkAllocateMemory(dev, &mai, NULL, &hb->mem) != VK_SUCCESS) {
        printf("vkteapot: vkAllocateMemory failed\n");
        return -1;
    }
    if (vkBindBufferMemory(dev, hb->buf, hb->mem, 0) != VK_SUCCESS) {
        printf("vkteapot: vkBindBufferMemory failed\n");
        return -1;
    }
    if (vkMapMemory(dev, hb->mem, 0, size, 0, &hb->map) != VK_SUCCESS) {
        printf("vkteapot: vkMapMemory failed\n");
        return -1;
    }
    return 0;
}

static void hbuf_destroy(VkDevice dev, hbuf_t *hb)
{
    if (!hb) return;
    if (hb->map) vkUnmapMemory(dev, hb->mem);
    if (hb->buf) vkDestroyBuffer(dev, hb->buf, NULL);
    if (hb->mem) vkFreeMemory(dev, hb->mem, NULL);
    memset(hb, 0, sizeof(*hb));
}

/* ------------------------------------------------------------------ *
 * depth attachment (resident D32_SFLOAT image + host-visible memory)
 * ------------------------------------------------------------------ */
static int create_depth(uint32_t width, uint32_t height)
{
    VkImageCreateInfo ici;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo mai;
    VkPhysicalDeviceMemoryProperties mem;
    VkImageViewCreateInfo ivci;
    uint32_t mt;

    T.depth_format = VK_FORMAT_D32_SFLOAT;

    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = T.depth_format;
    ici.extent.width = width;
    ici.extent.height = height;
    ici.extent.depth = 1;
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(T.device, &ici, NULL, &T.depth_img) != VK_SUCCESS) {
        printf("vkteapot: vkCreateImage (depth) failed\n");
        return -1;
    }
    vkGetImageMemoryRequirements(T.device, T.depth_img, &req);
    vkGetPhysicalDeviceMemoryProperties(T.phys, &mem);
    mt = find_memory_type(&mem, req.memoryTypeBits,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == 0xffffffffu) { printf("vkteapot: no host-visible memory (depth)\n"); return -1; }
    memset(&mai, 0, sizeof(mai));
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mt;
    if (vkAllocateMemory(T.device, &mai, NULL, &T.depth_mem) != VK_SUCCESS) {
        printf("vkteapot: vkAllocateMemory (depth) failed\n");
        return -1;
    }
    if (vkBindImageMemory(T.device, T.depth_img, T.depth_mem, 0) != VK_SUCCESS) {
        printf("vkteapot: vkBindImageMemory (depth) failed\n");
        return -1;
    }
    memset(&ivci, 0, sizeof(ivci));
    ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image = T.depth_img;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = T.depth_format;
    ivci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    ivci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    ivci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    ivci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    ivci.subresourceRange.baseMipLevel = 0;
    ivci.subresourceRange.levelCount = 1;
    ivci.subresourceRange.baseArrayLayer = 0;
    ivci.subresourceRange.layerCount = 1;
    if (vkCreateImageView(T.device, &ivci, NULL, &T.depth_view) != VK_SUCCESS) {
        printf("vkteapot: vkCreateImageView (depth) failed\n");
        return -1;
    }
    return 0;
}

static int create_swapchain_views_fbs(void)
{
    uint32_t i;
    VkImageViewCreateInfo ivci;
    VkFramebufferCreateInfo fbci;
    VkImageView att[2];

    if (vkGetSwapchainImagesKHR(T.device, T.swapchain, &T.sc_count, NULL) != VK_SUCCESS)
        return -1;
    if (T.sc_count > MAX_SC_IMAGES) T.sc_count = MAX_SC_IMAGES;
    if (vkGetSwapchainImagesKHR(T.device, T.swapchain, &T.sc_count, T.sc_images) != VK_SUCCESS)
        return -1;

    for (i = 0; i < T.sc_count; i++) {
        memset(&ivci, 0, sizeof(ivci));
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = T.sc_images[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = T.format;
        ivci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel = 0;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(T.device, &ivci, NULL, &T.sc_views[i]) != VK_SUCCESS)
            return -1;

        att[0] = T.sc_views[i];
        att[1] = T.depth_view;
        memset(&fbci, 0, sizeof(fbci));
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = T.rp;
        fbci.attachmentCount = 2;
        fbci.pAttachments = att;
        fbci.width = T.width;
        fbci.height = T.height;
        fbci.layers = 1;
        if (vkCreateFramebuffer(T.device, &fbci, NULL, &T.sc_fbs[i]) != VK_SUCCESS)
            return -1;
    }
    return 0;
}

static int init_vulkan(uint32_t width, uint32_t height)
{
    PFN_vkCreateEwokOSSurfaceVK_t pfnCreateSurface;
    VkInstanceCreateInfo ici;
    VkApplicationInfo app;
    VkDeviceCreateInfo dci;
    VkDeviceQueueCreateInfo qci;
    VkQueueFamilyProperties qfam[16];
    uint32_t nphys = 0, nq = 16, i;
    float prio = 1.0f;
    VkSwapchainCreateInfoKHR sci;
    VkSurfaceFormatKHR sfmt;
    VkAttachmentDescription att[2];
    VkAttachmentReference colorRef;
    VkAttachmentReference depthRef;
    VkSubpassDescription subpass;
    VkRenderPassCreateInfo rpci;
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo smci;
    VkPipelineShaderStageCreateInfo stages[2];
    VkPipelineVertexInputStateCreateInfo vi;
    VkVertexInputBindingDescription vbind[2];
    VkVertexInputAttributeDescription vattr[2];
    VkPipelineInputAssemblyStateCreateInfo ia;
    VkPipelineViewportStateCreateInfo vps;
    VkPipelineRasterizationStateCreateInfo rs;
    VkPipelineMultisampleStateCreateInfo ms;
    VkPipelineDepthStencilStateCreateInfo ds;
    VkPipelineColorBlendAttachmentState cba;
    VkPipelineColorBlendStateCreateInfo cb;
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dsci;
    VkGraphicsPipelineCreateInfo gpci;
    VkPipelineLayoutCreateInfo plci;
    VkDescriptorSetLayoutBinding ub;
    VkDescriptorSetLayoutCreateInfo dslci;
    VkDescriptorPoolSize psz;
    VkDescriptorPoolCreateInfo dpci;
    VkDescriptorSetAllocateInfo dsai;
    VkDescriptorBufferInfo ubo_info;
    VkWriteDescriptorSet wr;
    VkCommandPoolCreateInfo cpci;
    VkCommandBufferAllocateInfo cbai;
    VkFenceCreateInfo fci;
    VkSemaphoreCreateInfo semci;
    VkDeviceSize pos_bytes, col_bytes;

    memset(&T, 0, sizeof(T));
    T.width = width; T.height = height;

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "vkteapot";
    app.apiVersion = VK_API_VERSION_1_3;
    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, NULL, &T.instance) != VK_SUCCESS) {
        printf("vkteapot: vkCreateInstance failed\n"); return -1;
    }

    pfnCreateSurface = (PFN_vkCreateEwokOSSurfaceVK_t)(void *)
        vkGetInstanceProcAddr(T.instance, "vkCreateEwokOSSurfaceVK");
    if (!pfnCreateSurface) { printf("vkteapot: no EwokOS surface entry\n"); return -1; }
    if (pfnCreateSurface(T.instance, width, height, VK_FORMAT_B8G8R8A8_UNORM,
                         (void *)teapot_present, NULL, &T.surface) != VK_SUCCESS) {
        printf("vkteapot: vkCreateEwokOSSurfaceVK failed\n"); return -1;
    }

    if (vkEnumeratePhysicalDevices(T.instance, &nphys, NULL) != VK_SUCCESS || nphys == 0) {
        printf("vkteapot: no physical devices\n"); return -1;
    }
    nphys = 1;
    vkEnumeratePhysicalDevices(T.instance, &nphys, &T.phys);

    vkGetPhysicalDeviceQueueFamilyProperties(T.phys, &nq, qfam);
    for (i = 0; i < nq; i++)
        if (qfam[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { T.qfi = i; break; }

    memset(&qci, 0, sizeof(qci));
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = T.qfi;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    memset(&dci, 0, sizeof(dci));
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    if (vkCreateDevice(T.phys, &dci, NULL, &T.device) != VK_SUCCESS) {
        printf("vkteapot: vkCreateDevice failed\n"); return -1;
    }
    vkGetDeviceQueue(T.device, T.qfi, 0, &T.queue);

    /* surface format -> swapchain */
    T.format = VK_FORMAT_B8G8R8A8_UNORM;
    {
        uint32_t nfmt = 1;
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(T.phys, T.surface, &nfmt, &sfmt) == VK_SUCCESS
            && nfmt >= 1 && sfmt.format != VK_FORMAT_UNDEFINED)
            T.format = sfmt.format;
    }

    memset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = T.surface;
    sci.minImageCount = 1;
    sci.imageFormat = T.format;
    sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sci.imageExtent.width = width;
    sci.imageExtent.height = height;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(T.device, &sci, NULL, &T.swapchain) != VK_SUCCESS) {
        printf("vkteapot: vkCreateSwapchainKHR failed\n"); return -1;
    }

    /* depth attachment must exist before the framebuffers reference its view */
    if (create_depth(width, height) != 0) return -1;

    /* render pass: attachment 0 = colour (clear/store), 1 = depth (clear).
     * Both loadOps are CLEAR so the GPU back-end's first_frame fill reproduces
     * them; the depth stencilLoadOp is CLEAR too because the CPU executor keys
     * a depth attachment's clear off stencil_load. */
    memset(att, 0, sizeof(att));
    att[0].format = T.format;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    att[1].format = T.depth_format;
    att[1].samples = VK_SAMPLE_COUNT_1_BIT;
    att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;
    memset(&rpci, 0, sizeof(rpci));
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 2;
    rpci.pAttachments = att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    if (vkCreateRenderPass(T.device, &rpci, NULL, &T.rp) != VK_SUCCESS) {
        printf("vkteapot: vkCreateRenderPass failed\n"); return -1;
    }

    if (create_swapchain_views_fbs() != 0) {
        printf("vkteapot: swapchain views/framebuffers failed\n"); return -1;
    }

    /* ---- resources: position / colour vertex buffers + mat4 UBO ----------- */
    pos_bytes = (VkDeviceSize)teapot.nvertices * 4 * sizeof(float);
    col_bytes = (VkDeviceSize)teapot.nvertices * 4 * sizeof(float);
    if (hbuf_create(T.device, T.phys, &T.pos, pos_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) != 0 ||
        hbuf_create(T.device, T.phys, &T.col, col_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) != 0 ||
        hbuf_create(T.device, T.phys, &T.ubo, 64, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) != 0) {
        printf("vkteapot: resource buffers failed\n"); return -1;
    }
    /* static vec4 position (x,y,z,1) from the interleaved pos+normal geometry */
    {
        float *dst = (float *)T.pos.map;
        int n;
        for (n = 0; n < teapot.nvertices; ++n) {
            dst[n*4+0] = teapot.vertices[n*6+0];
            dst[n*4+1] = teapot.vertices[n*6+1];
            dst[n*4+2] = teapot.vertices[n*6+2];
            dst[n*4+3] = 1.0f;
        }
    }

    /* ---- descriptor set layout / pool / set (binding 0 = UBO, VERTEX) ----- */
    memset(&ub, 0, sizeof(ub));
    ub.binding = 0;
    ub.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ub.descriptorCount = 1;
    ub.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    memset(&dslci, 0, sizeof(dslci));
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 1;
    dslci.pBindings = &ub;
    if (vkCreateDescriptorSetLayout(T.device, &dslci, NULL, &T.dsl) != VK_SUCCESS) {
        printf("vkteapot: vkCreateDescriptorSetLayout failed\n"); return -1;
    }

    /* ---- pipeline layout ---- */
    memset(&plci, 0, sizeof(plci));
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &T.dsl;
    if (vkCreatePipelineLayout(T.device, &plci, NULL, &T.pl) != VK_SUCCESS) {
        printf("vkteapot: vkCreatePipelineLayout failed\n"); return -1;
    }

    /* ---- shader modules ---- */
    memset(&smci, 0, sizeof(smci));
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = sizeof(kTeapotVs); smci.pCode = kTeapotVs;
    if (vkCreateShaderModule(T.device, &smci, NULL, &vs) != VK_SUCCESS) {
        printf("vkteapot: VS module failed\n"); return -1;
    }
    smci.codeSize = sizeof(kTeapotFs); smci.pCode = kTeapotFs;
    if (vkCreateShaderModule(T.device, &smci, NULL, &fs) != VK_SUCCESS) {
        printf("vkteapot: FS module failed\n"); return -1;
    }
    memset(stages, 0, sizeof(stages));
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs; stages[1].pName = "main";

    /* ---- vertex input: binding 0 = position (loc 0), binding 1 = colour (loc 1) ---- */
    memset(vbind, 0, sizeof(vbind));
    vbind[0].binding = 0; vbind[0].stride = 4 * sizeof(float);
    vbind[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vbind[1].binding = 1; vbind[1].stride = 4 * sizeof(float);
    vbind[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    memset(vattr, 0, sizeof(vattr));
    vattr[0].location = 0; vattr[0].binding = 0; vattr[0].offset = 0;
    vattr[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vattr[1].location = 1; vattr[1].binding = 1; vattr[1].offset = 0;
    vattr[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    memset(&vi, 0, sizeof(vi));
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = vbind;
    vi.vertexAttributeDescriptionCount = 2; vi.pVertexAttributeDescriptions = vattr;

    memset(&ia, 0, sizeof(ia));
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    memset(&vps, 0, sizeof(vps));
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1; vps.scissorCount = 1;   /* dynamic viewport+scissor */
    memset(&rs, 0, sizeof(rs));
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;   /* two-sided lit, like the GL demo */
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    memset(&ms, 0, sizeof(ms));
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    memset(&ds, 0, sizeof(ds));
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    memset(&cba, 0, sizeof(cba));
    cba.blendEnable = VK_FALSE;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    memset(&cb, 0, sizeof(cb));
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;
    memset(&dsci, 0, sizeof(dsci));
    dsci.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dsci.dynamicStateCount = 2; dsci.pDynamicStates = dyn;

    memset(&gpci, 0, sizeof(gpci));
    gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpci.stageCount = 2; gpci.pStages = stages;
    gpci.pVertexInputState = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vps;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pDepthStencilState = &ds;
    gpci.pColorBlendState = &cb;
    gpci.pDynamicState = &dsci;
    gpci.layout = T.pl;
    gpci.renderPass = T.rp;
    gpci.subpass = 0;
    if (vkCreateGraphicsPipelines(T.device, VK_NULL_HANDLE, 1, &gpci, NULL, &T.pipeline)
            != VK_SUCCESS) {
        printf("vkteapot: vkCreateGraphicsPipelines failed\n"); return -1;
    }
    vkDestroyShaderModule(T.device, vs, NULL);
    vkDestroyShaderModule(T.device, fs, NULL);

    /* ---- descriptor pool + set + update (UBO) ---- */
    psz.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    psz.descriptorCount = 1;
    memset(&dpci, 0, sizeof(dpci));
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &psz;
    if (vkCreateDescriptorPool(T.device, &dpci, NULL, &T.dpool) != VK_SUCCESS) {
        printf("vkteapot: vkCreateDescriptorPool failed\n"); return -1;
    }
    memset(&dsai, 0, sizeof(dsai));
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = T.dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &T.dsl;
    if (vkAllocateDescriptorSets(T.device, &dsai, &T.dset) != VK_SUCCESS) {
        printf("vkteapot: vkAllocateDescriptorSets failed\n"); return -1;
    }
    memset(&ubo_info, 0, sizeof(ubo_info));
    ubo_info.buffer = T.ubo.buf;
    ubo_info.offset = 0;
    ubo_info.range = 64;
    memset(&wr, 0, sizeof(wr));
    wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet = T.dset;
    wr.dstBinding = 0;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    wr.pBufferInfo = &ubo_info;
    vkUpdateDescriptorSets(T.device, 1, &wr, 0, NULL);

    /* ---- command buffer + sync ---- */
    memset(&cpci, 0, sizeof(cpci));
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = T.qfi;
    if (vkCreateCommandPool(T.device, &cpci, NULL, &T.cpool) != VK_SUCCESS) {
        printf("vkteapot: vkCreateCommandPool failed\n"); return -1;
    }
    memset(&cbai, 0, sizeof(cbai));
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = T.cpool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(T.device, &cbai, &T.cmd) != VK_SUCCESS) {
        printf("vkteapot: vkAllocateCommandBuffers failed\n"); return -1;
    }
    memset(&fci, 0, sizeof(fci));
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(T.device, &fci, NULL, &T.fence) != VK_SUCCESS) {
        printf("vkteapot: vkCreateFence failed\n"); return -1;
    }
    memset(&semci, 0, sizeof(semci));
    semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(T.device, &semci, NULL, &T.acquired) != VK_SUCCESS) {
        printf("vkteapot: vkCreateSemaphore failed\n"); return -1;
    }

    /* fixed projection for the (non-resizable) window */
    perspective(projection_matrix, 45.0f, (float)width / (float)height, 0.5f, 100.0f);

    T.ready = 1;
    printf("vkteapot: vulkan ready (%ux%u, format=%d, %u swapchain images, %d vertices)\n",
           T.width, T.height, (int)T.format, T.sc_count, teapot.nvertices);
    return 0;
}

/* compute the frame's MVP + lighting and refresh the colour VBO + UBO. */
static void update_scene(float dt)
{
    float view[16], model_view[16], normal_matrix[16], mvp[16], le[3];
    float dist = 9.0f;
    float elev = camera_elev * (float)M_PI / 180.0f;
    float azim = camera_angle * (float)M_PI / 180.0f;
    float cam_y = dist * sinf(elev);
    float r = dist * cosf(elev);
    float cam_x = r * sinf(azim);
    float cam_z = r * cosf(azim);

    spin_angle += 45.0f * dt;
    if (spin_angle >= 360.0f) spin_angle -= 360.0f;
    camera_angle += 20.0f * dt;
    if (camera_angle >= 360.0f) camera_angle -= 360.0f;

    look_at(view, cam_x, cam_y, cam_z, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    memcpy(model_view, view, sizeof(model_view));
    translate(model_view, 0.0f, -1.3f, 0.0f);
    rotate(model_view, -90.0f, 1.0f, 0.0f, 0.0f);
    rotate(model_view, spin_angle, 0.0f, 0.0f, 1.0f);

    memcpy(mvp, projection_matrix, sizeof(mvp));
    multiply(mvp, model_view);

    memcpy(normal_matrix, model_view, sizeof(normal_matrix));
    invert(normal_matrix);
    transpose(normal_matrix);

    /* world-fixed light transformed into eye space */
    xform_point(view, 6.0f, 8.0f, 10.0f, le);

    compute_colors(model_view, normal_matrix, le);

    /* upload the lit colours and the MVP matrix */
    memcpy(T.col.map, teapot.colors, (size_t)teapot.nvertices * 4 * sizeof(float));
    memcpy(T.ubo.map, mvp, 64);
}

/* record + submit + present one frame into swapchain image `idx`. */
static void render_frame(uint32_t idx, float dt)
{
    VkCommandBufferBeginInfo bi;
    VkRenderPassBeginInfo rpi;
    VkClearValue clear[2];
    VkViewport vp;
    VkRect2D sc;
    VkDeviceSize offsets[2] = { 0, 0 };
    VkBuffer bufs[2];
    VkSubmitInfo si;
    VkPresentInfoKHR pi;
    VkResult res;

    update_scene(dt);

    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(T.cmd, &bi) != VK_SUCCESS) return;

    clear[0].color.float32[0] = 0.05f;
    clear[0].color.float32[1] = 0.06f;
    clear[0].color.float32[2] = 0.09f;
    clear[0].color.float32[3] = 0x88 / 255.0f;
    clear[1].depthStencil.depth = 1.0f;
    clear[1].depthStencil.stencil = 0;
    memset(&rpi, 0, sizeof(rpi));
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass = T.rp;
    rpi.framebuffer = T.sc_fbs[idx];
    rpi.renderArea.offset.x = 0;
    rpi.renderArea.offset.y = 0;
    rpi.renderArea.extent.width = T.width;
    rpi.renderArea.extent.height = T.height;
    rpi.clearValueCount = 2;
    rpi.pClearValues = clear;
    vkCmdBeginRenderPass(T.cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(T.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, T.pipeline);
    vkCmdBindDescriptorSets(T.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, T.pl,
                            0, 1, &T.dset, 0, NULL);

    vp.x = 0; vp.y = 0;
    vp.width = (float)T.width; vp.height = (float)T.height;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    vkCmdSetViewport(T.cmd, 0, 1, &vp);
    sc.offset.x = 0; sc.offset.y = 0;
    sc.extent.width = T.width; sc.extent.height = T.height;
    vkCmdSetScissor(T.cmd, 0, 1, &sc);

    bufs[0] = T.pos.buf; bufs[1] = T.col.buf;
    vkCmdBindVertexBuffers(T.cmd, 0, 2, bufs, offsets);

    /* one draw for the whole mesh (the GL demo issues 32 per-patch draws only
     * to keep each dispatch small; a single whole-mesh draw is equivalent and
     * lets the GPU back-end stage the entire triangle list in one dispatch). */
    vkCmdDraw(T.cmd, (uint32_t)teapot.nvertices, 1, 0, 0);
    vkCmdEndRenderPass(T.cmd);
    if (vkEndCommandBuffer(T.cmd) != VK_SUCCESS) return;

    vkResetFences(T.device, 1, &T.fence);
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &T.cmd;
    if (vkQueueSubmit(T.queue, 1, &si, T.fence) != VK_SUCCESS) return;
    vkWaitForFences(T.device, 1, &T.fence, VK_TRUE, 2000000000ull);

    memset(&pi, 0, sizeof(pi));
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.swapchainCount = 1;
    pi.pSwapchains = &T.swapchain;
    pi.pImageIndices = &idx;
    pi.pResults = &res;
    vkQueuePresentKHR(T.queue, &pi);
}

static void on_event(xwin_t *xwin, xevent_t *ev)
{
    if (ev->type == XEVT_IM && ev->value.im.value == 27)  /* ESC closes */
        xwin_close(xwin);
}

/* ---- fps accounting ------------------------------------------------------ */
static int   fps_count = 0, fps = 0;
static uint32_t last_tic = 0, last_frame_tic = 0;

static float frame_dt(void)
{
    uint32_t cur = 0;
    float dt;
    kernel_tic32(NULL, NULL, &cur);
    if (last_frame_tic == 0) last_frame_tic = cur;
    dt = (float)(cur - last_frame_tic) / 1000000.0f;
    last_frame_tic = cur;
    if (dt > 0.1f) dt = 0.1f;
    return dt;
}

static void update_fps(void)
{
    uint32_t low = 0;
    kernel_tic32(NULL, NULL, &low);
    if (last_tic == 0 || (low - last_tic) >= 1000000) {
        last_tic = low;
        fps = fps_count;
        fps_count = 0;
        printf("vkteapot: %d fps\n", fps);
        if (g_fpslog) {           /* app-written file: the only ssh-readable FPS */
            FILE *fp = fopen(g_fpslog, "w");
            if (fp) { fprintf(fp, "%d\n", fps); fclose(fp); }
        }
    }
    fps_count++;
}

static void on_repaint(xwin_t *xwin, graph_t *g)
{
    uint32_t idx = 0;
    float dt;
    (void)xwin;
    if (!T.ready) return;
    g_cur = g;
    if (vkAcquireNextImageKHR(T.device, T.swapchain, UINT64_MAX, T.acquired,
                              VK_NULL_HANDLE, &idx) != VK_SUCCESS)
        return;
    if (idx >= T.sc_count) idx = 0;
    dt = frame_dt();
    render_frame(idx, dt);
    g_frames++;
    update_fps();
}

static void loop(void *p)
{
    xwin_repaint((xwin_t *)p);
    /* xwin_repaint already blocks on the server present handshake, so the loop
     * needs no frame-rate sleep of its own.  16ms here throttled vkteapot to
     * ~30fps while the GL twin (teapot.cpp loop: render_frame + usleep(1000))
     * free-runs at ~79fps through the very same xwin_repaint present path.
     * Match the GL demo: a 1ms yield keeps the CPU off a busy spin without
     * capping the frame rate below what the GPU + compositor can deliver. */
    usleep(1000);
}

static void cleanup_vulkan(void)
{
    uint32_t i;
    if (!T.device) return;
    for (i = 0; i < T.sc_count; i++) {
        if (T.sc_fbs[i])   vkDestroyFramebuffer(T.device, T.sc_fbs[i], NULL);
        if (T.sc_views[i]) vkDestroyImageView(T.device, T.sc_views[i], NULL);
    }
    hbuf_destroy(T.device, &T.pos);
    hbuf_destroy(T.device, &T.col);
    hbuf_destroy(T.device, &T.ubo);
    if (T.depth_view) vkDestroyImageView(T.device, T.depth_view, NULL);
    if (T.depth_img)  vkDestroyImage(T.device, T.depth_img, NULL);
    if (T.depth_mem)  vkFreeMemory(T.device, T.depth_mem, NULL);
    if (T.acquired)   vkDestroySemaphore(T.device, T.acquired, NULL);
    if (T.fence)      vkDestroyFence(T.device, T.fence, NULL);
    if (T.cpool)      vkDestroyCommandPool(T.device, T.cpool, NULL);
    if (T.dpool)      vkDestroyDescriptorPool(T.device, T.dpool, NULL);
    if (T.pipeline)   vkDestroyPipeline(T.device, T.pipeline, NULL);
    if (T.pl)         vkDestroyPipelineLayout(T.device, T.pl, NULL);
    if (T.dsl)        vkDestroyDescriptorSetLayout(T.device, T.dsl, NULL);
    if (T.rp)         vkDestroyRenderPass(T.device, T.rp, NULL);
    if (T.swapchain)  vkDestroySwapchainKHR(T.device, T.swapchain, NULL);
    vkDestroyDevice(T.device, NULL);
    if (T.surface)  vkDestroySurfaceKHR(T.instance, T.surface, NULL);
    if (T.instance) vkDestroyInstance(T.instance, NULL);
}

/* the EwokOS surface entry (a WSI extension: resolve via proc addr) */
int main(int argc, char *argv[])
{
    x_t x;
    xwin_t *xwin;
    (void)argc;

    if (argc > 1) g_fpslog = argv[1];   /* optional FPS log path (see g_fpslog) */
    setbuf(stdout, NULL);   /* unbuffered so ssh sees prints immediately */

    if (build_teapot_geometry() != 0)
        return -1;

    if (init_vulkan(TEAPOT_W, TEAPOT_H) != 0) {
        printf("vkteapot: Vulkan init failed\n");
        cleanup_vulkan();
        return -1;
    }

    x_init(&x, NULL);
    x.on_loop = loop;
    xwin = xwin_open(&x, -1, 32, 32, TEAPOT_W, TEAPOT_H, "vkteapot", XWIN_STYLE_NORMAL);
    if (xwin == NULL) {
        cleanup_vulkan();
        return -1;
    }
    xwin->on_event = on_event;
    xwin->on_repaint = on_repaint;
    /* The scene clears to a translucent background (alpha 0x88), exactly like the
     * GL teapot twin. Without alpha mode the compositor re-blends those
     * translucent pixels over the previous frame every repaint (alpha stacked on
     * alpha), which makes the background flicker; enabling it makes draw_win
     * restore the backdrop and blend each pixel exactly once. */
    xwin_set_alpha(xwin, true);
    xwin_set_visible(xwin, true);

    x_run(&x, xwin);

    xwin_destroy(xwin);
    cleanup_vulkan();
    if (teapot.vertices) free(teapot.vertices);
    if (teapot.colors)   free(teapot.colors);
    printf("vkteapot: presented %d frames\n", g_frames);
    return 0;
}
