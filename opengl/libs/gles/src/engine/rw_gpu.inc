/* Adreno RW fragment offload. CPU keeps the original VS and homogeneous
 * clipping. A pixel owns its colour/depth pair and visits triangles in order.
 * Both renderers share D24S8, so a declined draw can use the CPU safely. */
#ifdef PGL_G3D
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <g2dclient/g2dclient.h>
#include "../../pglcompiler/include/pglcompiler.h"
#include "../../pglcompiler/include/rw_raster_cs.h"
typedef struct { int shm; uint32_t *px; uint32_t cap; } rw_canvas;
static rw_canvas rw_fb, rw_z, rw_attr, rw_chunk, rw_unif;
#define RW_GPU_ATTR_CLASS_COUNT 4u
static rw_canvas rw_attr_pool[RW_GPU_ATTR_CLASS_COUNT];
static glFramebuffer rw_saved_fb, rw_saved_z;
static int rw_adopted, rw_program = -1, rw_blocked, rw_terminal;
static struct pglc_program rw_rs, rw_xs;
static int rw_direct_mode = -1;
static size_t rw_tri_count;
static int rw_collect_failed;
static int rw_collect_fail_reason;
static int rw_collect_dumped;
static int rw_dump_enabled;
static unsigned rw_reason1_dumped;
static unsigned rw_texture_clock;
typedef struct {
    rw_canvas canvas;
    GLuint name, gen;
    unsigned age;
    int w,h;
    struct pglc_tex_layout layout;
} rw_texture;
/* Keep the established mapping budget (the OS permits 128 shm attachments
 * per process). Reserve 48 entries for expensive images and 16 for the
 * ordinary LRU stream, so small one-use textures cannot evict all large ones. */
#define RW_GPU_TEXTURE_CACHE_MAX 64
#define RW_GPU_TEXTURE_RETAINED 48
static rw_texture rw_textures[RW_GPU_TEXTURE_CACHE_MAX];
static int rw_texture_lru = -1;
static int rw_texture_profile_ab;
static void rw_gpu_profile_window(void)
{
    if(rw_texture_profile_ab) {
        printf("PGL_PROFILE rw_texture completed_policy=%s\n",rw_texture_lru?"lru":"size");
        rw_texture_lru=!rw_texture_lru;
    }
}
static struct rw_batch_key {
    GLuint name,gen;
    int w,h,depth,clip[4],blend[4],ts[4];
    GLuint name1,gen1;
    int w1,h1,ts1[4];
    float fx[4];
    float fog[4],alpha[4];
} rw_batch;
static rw_texture *rw_batch_texture,*rw_batch_texture1;
static int rw_verified;
/* The native RW vertex contracts emit a fog coordinate even when fog is
 * disabled; in that common case every vertex carries exactly 1.0.  Keep a
 * batch-wide flag so the raster kernel can skip the RGB mix entirely. */
static int rw_batch_fog_enabled = 0;
static int rw_min_tris_cached = -1;
static int rw_min_pixels_cached = -1;
static int rw_min_pixels_w, rw_min_pixels_h;
static int rw_profile_thresholds_dumped;
static int rw_gpu_flush(void);
/* rw_raster_cs keeps one 2048-entry candidate list per workgroup.  Split a
 * large staging batch at exactly that triangle capacity so the shader never
 * has to drop candidates or write past shared memory. */
#define RW_GPU_DISPATCH_VERTS (2048u * 3u)
#define RW_GPU_MAX_VERTS 20478u
#define RW_GPU_MAX_CHUNKS ((RW_GPU_MAX_VERTS + RW_GPU_DISPATCH_VERTS - 1u) / RW_GPU_DISPATCH_VERTS)
/* rw_attr uses persistent size-class shm segments reused by RW batches. Older
 * g2dd images cache the attach by shm id and do not refresh the mapping when
 * a later request advertises a shorter canvas. Each class therefore keeps its
 * advertised byte span stable; `n` below still limits the shader to the live
 * prefix. */
#define RW_GPU_ATTR_BYTES (RW_GPU_MAX_VERTS * 20u * 4u)
static const uint32_t rw_attr_class_bytes[RW_GPU_ATTR_CLASS_COUNT] = {
    64u * 1024u,
    256u * 1024u,
    768u * 1024u,
    RW_GPU_ATTR_BYTES
};

typedef struct {
    float lx, ly, ux, uy;
} rw_chunk_box;
static rw_chunk_box rw_chunk_boxes[RW_GPU_MAX_CHUNKS];

static void rw_chunk_boxes_clear(void)
{
    for (unsigned i = 0; i < RW_GPU_MAX_CHUNKS; ++i) {
        rw_chunk_boxes[i].lx = rw_chunk_boxes[i].ly = 1.0e30f;
        rw_chunk_boxes[i].ux = rw_chunk_boxes[i].uy = -1.0e30f;
    }
}

static void rw_chunk_box_update(size_t first, const float *sx,
                                const float *sy)
{
    unsigned chunk = (unsigned)(first / RW_GPU_DISPATCH_VERTS);
    rw_chunk_box *b;
    if (chunk >= RW_GPU_MAX_CHUNKS) return;
    b = &rw_chunk_boxes[chunk];
    for (int i = 0; i < 3; ++i) {
        if (sx[i] < b->lx) b->lx = sx[i];
        if (sx[i] > b->ux) b->ux = sx[i];
        if (sy[i] < b->ly) b->ly = sy[i];
        if (sy[i] > b->uy) b->uy = sy[i];
    }
}

static void rw_chunk_boxes_rebuild(size_t count)
{
    rw_chunk_boxes_clear();
    for (size_t i = 0; i + 2 < count; i += 3) {
        const float *a = (const float *)rw_attr.px + i * 20;
        const float *b = a + 20;
        const float *c = b + 20;
        float sx[3] = { a[0], b[0], c[0] };
        float sy[3] = { a[1], b[1], c[1] };
        rw_chunk_box_update(i, sx, sy);
    }
}

/* A g2d compute dispatch is synchronous and has a fixed cache-clean/IRQ/IPC
 * cost.  Keep the cutoff tunable for board-side profiling.  On q8b the CPU
 * interpreter is more expensive than a GPU submit for the small world meshes
 * emitted by re3, so the default leaves only empty/degenerate draws on CPU;
 * PGL_RW_MIN_TRIS can override the cutoff (set it to 0 to force collection
 * of every non-empty mesh for an A/B run).  Projected-area filtering is kept
 * as a separate opt-in knob below because the board profile favors GPU work
 * even for many small RenderWare meshes. */
static int rw_gpu_min_tris(void)
{
    const char *v = getenv("PGL_RW_MIN_TRIS");
    int n;
    if (rw_min_tris_cached >= 0) return rw_min_tris_cached;
    if (v) {
        n = atoi(v);
        rw_min_tris_cached = n > 0 ? n : 0;
        return rw_min_tris_cached;
    }
    /* Do not classify by triangle count alone: a three-triangle road/sky
     * primitive can cover the whole 640x480 target and is much cheaper on the
     * GPU than through the scalar CPU rasterizer.  The projected-area test
     * below makes this decision after clipping.  Keep the explicit knob for
     * board-side A/B experiments, but leave its default disabled. */
    rw_min_tris_cached = 0;
    return rw_min_tris_cached;
}

static int rw_gpu_min_pixels(void)
{
    const char *v = getenv("PGL_RW_MIN_PIXELS");
    long n;
    if (rw_min_pixels_cached >= 0 && rw_min_pixels_w == g_w &&
        rw_min_pixels_h == g_h)
        return rw_min_pixels_cached;
    rw_min_pixels_w = g_w;
    rw_min_pixels_h = g_h;
    if (v) {
        n = strtol(v, NULL, 10);
        rw_min_pixels_cached = n > 0 ? (n > 0x7fffffffL ? 0x7fffffff : (int)n) : 0;
        return rw_min_pixels_cached;
    }
    /* The GPU route still wins for low-triangle meshes with a large projected
     * area: the board profile showed that sending these through the scalar CPU
     * rasterizer increased CPU time by roughly 1.3 s per five-second window
     * and reduced FPS, even though it removed several hundred GPU submits.
     * Keep the default GPU-first; PGL_RW_MIN_PIXELS remains available for an
     * explicit CPU/GPU A/B experiment. */
    rw_min_pixels_cached = 0;
    return rw_min_pixels_cached;
}

static int rw_gpu_batch_is_tiny(size_t previous)
{
    int min_pixels = rw_gpu_min_pixels();
    float lx, ly, ux, uy;
    /* If a compatible batch is already open, keep appending to it; splitting
     * that batch to run only the newest small object on the CPU would add an
     * extra GPU submit and defeat the coalescer. */
    if (!min_pixels || previous || rw_tri_count <= previous || rw_tri_count > 256)
        return 0;
    lx = ly = 1.0e30f;
    ux = uy = -1.0e30f;
    /* rw_tri_count is a vertex count (the collector appends three records per
     * triangle), so walk the newly collected vertex records directly.  The
     * previous code multiplied both bounds by three and read past rw_attr for
     * ordinary batches, corrupting the tiny-batch decision and occasionally
     * producing the garbage-vertex diagnostics seen on the board. */
    for (size_t i = previous; i < rw_tri_count; ++i) {
        const float *v = (const float*)rw_attr.px + i * 20;
        if (v[0] < lx) lx = v[0];
        if (v[0] > ux) ux = v[0];
        if (v[1] < ly) ly = v[1];
        if (v[1] > uy) uy = v[1];
    }
    return (ux - lx) * (uy - ly) < (float)min_pixels;
}

static void rw_gpu_texture_deleted(GEint n,const GEuint *names)
{
    rw_gpu_flush();
    for(int i=0;i<n;i++) for(int j=0;j<RW_GPU_TEXTURE_CACHE_MAX;j++)
        if(rw_textures[j].name==names[i]) {
            rw_textures[j].name=rw_textures[j].gen=rw_textures[j].age=0;
        }
}
static int rw_ensure(rw_canvas *c, uint32_t bytes)
{
    int shm=0; uint32_t *px=NULL;
    if (c->px && c->cap>=bytes) return 1;
    if (!bytes || g2d_shm_alloc(bytes,&shm,&px)!=0 || !px || shm<=0) return 0;
    if(c->px) g2d_shm_free(c->px);
    c->shm=shm; c->px=px; c->cap=bytes;
    return 1;
}
/* Select a persistent size class for the current batch. Each class has its
 * own shm id and is always advertised with its full capacity, so old g2dd
 * attach caches remain valid. Small batches avoid cleaning the 1.6 MB maximum
 * staging segment, while the maximum class preserves the overflow/replay
 * safety bound. */
static int rw_attr_select(size_t bytes)
{
    unsigned i;
    /* A compatible draw appends to the pending batch. Re-selecting a
     * smaller class here loses the vertices already collected into rw_attr
     * when clipping emitted fewer vertices than the original reservation. */
    if (rw_tri_count)
        return rw_attr.px != NULL && bytes <= rw_attr.cap;
    if (bytes == 0) bytes = 1;
    for (i = 0; i < RW_GPU_ATTR_CLASS_COUNT; ++i) {
        if (bytes <= rw_attr_class_bytes[i]) {
            if (!rw_ensure(&rw_attr_pool[i], rw_attr_class_bytes[i]))
                return 0;
            rw_attr = rw_attr_pool[i];
            return 1;
        }
    }
    return 0;
}
/* Clipping can exceed the input reservation. Migrate the live prefix into a
 * larger persistent class rather than replaying the whole draw on the CPU.
 * Collection runs before submission, so neither buffer is in GPU flight. */
static int rw_attr_grow(size_t count)
{
    size_t bytes=count*20u*4u;
    if(count>RW_GPU_MAX_VERTS) return 0;
    if(bytes<=rw_attr.cap) return 1;
    for(unsigned i=0;i<RW_GPU_ATTR_CLASS_COUNT;i++) {
        if(bytes<=rw_attr_class_bytes[i]) {
            rw_canvas *next=&rw_attr_pool[i];
            if(!rw_ensure(next,rw_attr_class_bytes[i])) return 0;
            memcpy(next->px,rw_attr.px,rw_tri_count*20u*4u);
            rw_attr=*next;
            if(profile_enabled()) {
                static unsigned reported;
                if(reported++<4) printf("PGL_PROFILE rw_attr_grow verts=%u capacity=%u\n",
                    (unsigned)rw_tri_count,(unsigned)rw_attr.cap);
            }
            return 1;
        }
    }
    return 0;
}
static void rw_gpu_detach(void)
{
    if(!rw_adopted) return;
    if(!rw_terminal) {
        memcpy(rw_saved_fb.buf,rw_fb.px,(size_t)g_w*g_h*4);
        memcpy(rw_saved_z.buf,rw_z.px,(size_t)g_w*g_h*4);
    }
    g_ctx.back_buffer=rw_saved_fb; g_ctx.zbuf=rw_saved_z;
    g_ctx.stencil_buf=rw_saved_z;
    g_ctx.user_alloced_backbuf=0;
    rw_adopted=0;
}
static void rw_gpu_poll_controls(void)
{
    /* These filesystem queries involve VFS IPC. Poll after the frame flush,
     * rather than twice for every object, while retaining safe live detach. */
    if(rw_program<0 || rw_blocked) return;
    rw_dump_enabled=access("/apps/rwdump",F_OK)==0;
    if(access("/apps/rwdetach",F_OK)==0 || access("/apps/rwoff",F_OK)==0) {
        rw_gpu_detach();
        rw_blocked=1;
        printf("PGL RW GPU detached by request\n");
    }
}
static void rw_gpu_fini(void)
{
    rw_gpu_flush();
    rw_gpu_detach();
    rw_canvas *canvases[]={&rw_fb,&rw_z,&rw_chunk,&rw_unif};
    for(unsigned i=0;i<4;i++) if(canvases[i]->px) {
        /* A terminal dispatch may still reference these buffers. Retain them
         * rather than recycling DMA storage before a device reset. */
        if(!rw_terminal) g2d_shm_free(canvases[i]->px);
        memset(canvases[i],0,sizeof(rw_canvas));
    }
    for(unsigned i=0;i<RW_GPU_ATTR_CLASS_COUNT;i++) if(rw_attr_pool[i].px) {
        if(!rw_terminal) g2d_shm_free(rw_attr_pool[i].px);
        memset(&rw_attr_pool[i],0,sizeof(rw_attr_pool[i]));
    }
    memset(&rw_attr,0,sizeof(rw_attr));
    for(unsigned i=0;i<RW_GPU_TEXTURE_CACHE_MAX;i++) if(rw_textures[i].canvas.px) {
        if(!rw_terminal) g2d_shm_free(rw_textures[i].canvas.px);
        memset(&rw_textures[i],0,sizeof(rw_texture));
    }
    if(rw_program>=0 && !rw_terminal) {
        g2d_cl_program_free_req_t req;
        g2d_cl_program_free_req_init(&req,rw_program); g2d_cl_program_free(&req);
    }
    rw_program=-1; rw_blocked=rw_terminal;rw_verified=0;rw_direct_mode=-1;
    rw_profile_thresholds_dumped=0;
    pglc_program_free(&rw_rs); pglc_program_free(&rw_xs);
    rw_tri_count=0;
    rw_chunk_boxes_clear();
}
static int rw_gpu_shm(void) { return rw_adopted ? rw_fb.shm : 0; }
static int rw_gpu_compile(void)
{
    struct pglc_blob_opts opts;
    rw_canvas blob={0};
    uint32_t bytes,ver=0;
    const char *enabled=getenv("PGL_RW_GPU");
    const char *direct=getenv("PGL_RW_DIRECT");
    if(rw_blocked || (enabled && !atoi(enabled))) return 0;
    if(rw_program>=0) return 1;
    rw_dump_enabled=access("/apps/rwdump",F_OK)==0;
    if(access("/apps/rwoff",F_OK)==0 || access("/apps/rwdetach",F_OK)==0) {
        rw_blocked=1; return 0;
    }
    if(g2d_get_version(&ver)!=G2D_OK || ver!=690) { rw_blocked=1; return 0; }
    pglc_set_version(ver);
    /* Kind 5 retains the xform pass; kind 6 consumes the stream directly.
     * Both require a pixel-grid blob: this shader owns pixels, not triangles.
     * Prefer the direct stream to avoid a redundant GPU copy and barrier;
     * older daemons can still use the two-pass GPU program. */
    rw_direct_mode = !direct || atoi(direct) != 0;
    if(!pglc_compile_compute_raw(rw_raster_cs,&rw_rs) ||
       (!rw_direct_mode && !pglc_compile_compute_raw(rw_xform_cs,&rw_xs))) {
        printf("PGL RW GPU compile failed: raster=%s xform=%s\n",rw_rs.log,rw_xs.log);
        rw_blocked=1; return 0;
    }
    for(int attempt=0;attempt<2;attempt++) {
        memset(&opts,0,sizeof opts);
        opts.geom_kind=rw_direct_mode ? 6 : 5; opts.geom_tpw=256;
        opts.attr_stride=20; opts.vout_stride=20;
        bytes=pglc_program_serialize(&rw_rs,rw_direct_mode ? NULL : &rw_xs,&opts,NULL,0);
        if(!bytes || !rw_ensure(&blob,bytes)) break;
        if(pglc_program_serialize(&rw_rs,rw_direct_mode ? NULL : &rw_xs,&opts,blob.px,bytes)==bytes) {
            /* Use the same owner table for program upload and free. */
            g2d_cl_program_req_t req;
            g2d_cl_program_req_init(&req,g2d_canvas(blob.shm,bytes,bytes/4,1,1));
            rw_program=g2d_cl_program(&req);
        }
        if(rw_program>=0 || !rw_direct_mode) break;
        rw_direct_mode=0;
        printf("PGL RW GPU direct stream unavailable; trying two-pass GPU\n");
        if(!pglc_compile_compute_raw(rw_xform_cs,&rw_xs)) break;
    }
    if(blob.px) g2d_shm_free(blob.px);
    if(rw_program<0) { printf("PGL RW GPU daemon declined program %d\n",rw_program); rw_blocked=1; return 0; }
    if(profile_enabled()) printf("PGL_PROFILE RW GPU ordered raster ready handle=%d kind=%d\n",
                                 rw_program,opts.geom_kind);
    return 1;
}
static int rw_gpu_target(void)
{
    uint32_t bytes=(uint32_t)g_w*g_h*4u;
    if(rw_adopted) return 1;
    if(g_ctx.user_alloced_backbuf) return 0;
    if(!rw_ensure(&rw_fb,bytes)||!rw_ensure(&rw_z,bytes)) return 0;
    rw_saved_fb=g_ctx.back_buffer; rw_saved_z=g_ctx.zbuf;
    memcpy(rw_fb.px,rw_saved_fb.buf,bytes); memcpy(rw_z.px,rw_saved_z.buf,bytes);
    pglSetBackBuffer(rw_fb.px,g_w,g_h,GL_TRUE);
    g_ctx.zbuf.buf=(u8*)rw_z.px;
    g_ctx.zbuf.lastrow=(u8*)rw_z.px+(g_h-1)*g_w*4;
    g_ctx.stencil_buf=g_ctx.zbuf;
    rw_adopted=1;
    return 1;
}
static int rw_factor(GLenum f)
{
    switch(f) {
    case GL_ZERO:return 0; case GL_ONE:return 1;
    case GL_SRC_COLOR:return 2; case GL_ONE_MINUS_SRC_COLOR:return 3;
    case GL_DST_COLOR:return 4; case GL_ONE_MINUS_DST_COLOR:return 5;
    case GL_SRC_ALPHA:return 6; case GL_ONE_MINUS_SRC_ALPHA:return 7;
    case GL_DST_ALPHA:return 8; case GL_ONE_MINUS_DST_ALPHA:return 9;
    default:return -1;
    }
}
static int rw_depth_func(void)
{
    if(!g_ctx.depth_test) return 0;
    switch(g_ctx.depth_func) {
    case GL_LESS:return 1;case GL_EQUAL:return 2;case GL_LEQUAL:return 3;
    case GL_GREATER:return 4;case GL_NOTEQUAL:return 5;case GL_GEQUAL:return 6;
    case GL_NEVER:return 7;case GL_ALWAYS:return 8;default:return -1;
    }
}
static int rw_wrap(GLenum f)
{ return f==GL_CLAMP_TO_EDGE?1:f==GL_MIRRORED_REPEAT?2:f==GL_REPEAT?0:-1; }

/* portablegl canonicalises every accepted 2D upload to four-byte RGBA8
 * storage, while retaining the application's internalformat in t->format.
 * The old GL_RGBA-only gate therefore sent RGB/alpha HUD and world textures
 * through the CPU even though their staged bytes have exactly the same layout
 * as RGBA textures. */
static int rw_texture_format_ok(GLenum f)
{
    switch (f) {
    case GL_ALPHA:
    case GL_LUMINANCE:
    case GL_LUMINANCE_ALPHA:
    case GL_RED:
    case GL_RG:
    case GL_RGB:
    case GL_BGR:
    case GL_RGBA:
    case GL_BGRA:
        return 1;
    default:
        return 0;
    }
}

/* Project the post-clip homogeneous position without consulting
 * glVertex::screen_space.  The latter is a cached optimization in
 * portablegl and can be produced by a worker or invalidated by the clipper
 * between two callback visits.  clip_space is the value that the CPU
 * rasterizer itself consumes, so deriving the viewport coordinates here keeps
 * the compute path on the same source of truth. */
static int rw_project_clip(vec4 clip, float *x, float *y, float *z, float *iw)
{
    float invw, nx, ny, nz;
    float vx0, vy0, vx1, vy1;
    if (!isfinite(clip.x) || !isfinite(clip.y) ||
        !isfinite(clip.z) || !isfinite(clip.w) ||
        clip.w <= 1.0e-6f || g_ctx.width <= 0 || g_ctx.height <= 0)
        return 0;
    invw = 1.0f / clip.w;
    nx = clip.x * invw;
    ny = clip.y * invw;
    nz = clip.z * invw;
    /* Keep the same small right/top epsilon as make_viewport_m4(). */
    vx0 = (float)g_ctx.xmin;
    vy0 = (float)g_ctx.ymin;
    vx1 = vx0 + (float)g_ctx.width - 0.01f;
    vy1 = vy0 + (float)g_ctx.height - 0.01f;
    *x = vx0 + (nx + 1.0f) * 0.5f * (vx1 - vx0);
    *y = vy0 + (ny + 1.0f) * 0.5f * (vy1 - vy0);
    /* CPU fallbacks share D24S8 with this target. Store window depth, just
     * like draw_triangle_fill(), rather than clamping NDC depth in the GPU
     * rasterizer. Otherwise mixed CPU/GPU draws compare different ranges. */
    *z = (nz + 1.0f) * 0.5f *
         (g_ctx.depth_range_far - g_ctx.depth_range_near) + g_ctx.depth_range_near;
    *iw = invw;
    return isfinite(*x) && isfinite(*y) && isfinite(*z);
}

static rw_texture *rw_gpu_texture(GLuint name,const glTexture *t,const rw_texture *keep)
{
    rw_texture *r=NULL, *empty=NULL, *stream=NULL, *cheap=NULL, *stale=NULL;
    unsigned oldest=~0u, stream_age=~0u;
    uint32_t bytes=((uint32_t)t->w*4u+63u)&~63u;
    bytes*=(uint32_t)t->h;
    if(rw_texture_lru<0) {
        const char *v=getenv("PGL_RW_TEXTURE_LRU");
        rw_texture_lru=v && atoi(v)!=0;
        /* Profiling-only A/B mode keeps the process, core and camera fixed. */
        rw_texture_profile_ab=v && atoi(v)==2;
        if(profile_enabled()) printf("PGL_PROFILE rw_texture policy=%s slots=%d retained=%d\n",
            rw_texture_lru?"lru":"size",RW_GPU_TEXTURE_CACHE_MAX,RW_GPU_TEXTURE_RETAINED);
    }
    for(int i=0;i<RW_GPU_TEXTURE_CACHE_MAX;i++) {
        rw_texture *slot=&rw_textures[i];
        if(slot->name==name) {r=slot;break;}
        /* Matfx resolves two images before submission. The second lookup must
         * not recycle the first image, even when it is the cheapest retained one. */
        if(slot==keep) continue;
        if(!slot->name && !empty) empty=slot;
        if(slot->age<oldest) {oldest=slot->age;r=slot;}
        if(i>=RW_GPU_TEXTURE_RETAINED && slot->age<stream_age) {
            stream_age=slot->age;stream=slot;
        }
        if(i<RW_GPU_TEXTURE_RETAINED) {
            if(!cheap || slot->layout.size<cheap->layout.size) cheap=slot;
            /* A changed camera/scene must be able to replace old large images. */
            if(rw_texture_clock-slot->age>8192u && (!stale || slot->age<stale->age))
                stale=slot;
        }
    }
    if(r && r->name!=name) {
        if(empty) r=empty;
        else if(!rw_texture_lru) r=stale ? stale :
            (cheap && bytes>cheap->layout.size ? cheap : stream);
    }
    if(!r) return NULL;
    r->age=++rw_texture_clock;
    if(r->name==name&&r->gen==t->datagen&&r->w==t->w&&r->h==t->h) return r;
    struct pglc_tex_layout layout;
    if(!pglc_tex_layout_2d(t->w,t->h,4,&layout)) return NULL;
    /* A6xx linear sampled images require whole 64-byte rows, including tiny
     * textures. SSBO consumers can use a compact image; the sampler cannot. */
    layout.stride=(layout.stride+63u)&~63u;
    layout.padded_w=layout.stride/4;
    layout.size=layout.stride*t->h;
    if(!rw_ensure(&r->canvas,layout.size)) return NULL;
    pglc_tex_store_2d(r->canvas.px,t->data,t->w*4,t->w,t->h,4,&layout);
    if (profile_enabled()) {
        ++g_prof_rw_tex_uploads;
        g_prof_rw_tex_bytes += layout.size;
    }
    r->name=name; r->gen=t->datagen; r->w=t->w;r->h=t->h;r->layout=layout;
    return r;
}
static void rw_patch(const struct pglc_stage *s,uint32_t base,const char *name,const void *value,uint32_t n)
{
    for(uint32_t i=0;i<s->num_uniform_bindings;i++) {
        const struct pglc_uniform_binding *u=&s->uniform_bindings[i];
        if(!strcmp(u->name,name) && u->num_components==n)
            memcpy(rw_unif.px+base+u->dword_offset,value,n*4);
    }
}
static int rw_collect(const glVertex *a,const glVertex *b,const glVertex *c,void *user)
{
    const interp_entry *e=(interp_entry*)user;
    const glVertex *vertices[]={a,b,c};
    float sx[3], sy[3], sz[3], siw[3];
    float minx, miny, maxx, maxy;
    if(rw_collect_failed) return 1;
    /* A few RenderWare immediate-mode setup draws leave gl_Position at its
     * zero-initialized value.  portablegl's CPU path treats the resulting
     * zero-w triangle as a no-op after clipping; replaying it here only adds
     * a full interpreter pass and floods the profile with reason=1.  Drop it
     * when all three vertices are exactly the zero sentinel.  Mixed triangles
     * still take the validation path below so a real malformed vertex cannot
     * hide a neighboring primitive. */
    {
        int zero_sentinel = 1;
        for (int i = 0; i < 3; ++i) {
            const vec4 q = vertices[i]->clip_space;
            if (q.x != 0.0f || q.y != 0.0f || q.z != 0.0f || q.w != 0.0f) {
                zero_sentinel = 0;
                break;
            }
        }
        if (zero_sentinel) return 1;
    }
    /* Match the CPU rasterizer's v4_to_v3h() conversion here: viewport x/y/z
     * are divided by clip-space w before they become pixel coordinates.  The
     * reciprocal w is retained separately for perspective-correct varyings.
     * Reject malformed clipped vertices as one draw so the caller replays it
     * through the CPU path; a NaN in a GPU triangle poisons tile bounds and can
     * suppress unrelated pixels in the same dispatch. */
    minx = miny = 1.0e30f;
    maxx = maxy = -1.0e30f;
    for(int i=0;i<3;i++) {
        const vec4 s=vertices[i]->screen_space;
        const vec4 clip=vertices[i]->clip_space;
        glVertex *mv=(glVertex *)vertices[i];
        /* Do not use the cached screen_space/rw_projected fields here.  A
         * worker may have filled them before clipping, while this callback is
         * visiting a temporary post-clip vertex. */
        if(!rw_project_clip(clip, &sx[i], &sy[i], &sz[i], &siw[i])) {
            rw_collect_fail_reason=1;
            if (rw_reason1_dumped < 8) {
                printf("PGL_PROFILE rw_reason1 screen=(%.7g,%.7g,%.7g,%.7g) clip=(%.7g,%.7g,%.7g,%.7g) valid=%d\n",
                       s.x,s.y,s.z,s.w,
                       clip.x,clip.y,clip.z,clip.w,
                       (int)vertices[i]->screen_space_valid);
                ++rw_reason1_dumped;
            }
            rw_collect_failed=1;
            return 1;
        }
        if (sx[i] < minx) minx=sx[i];
        if (sx[i] > maxx) maxx=sx[i];
        if (sy[i] < miny) miny=sy[i];
        if (sy[i] > maxy) maxy=sy[i];
        mv->rw_projected_x=sx[i];
        mv->rw_projected_y=sy[i];
        mv->rw_projected_z=clip.z;
        mv->rw_projected_iw=siw[i];
        mv->rw_projected_valid=GL_TRUE;
    }
    if (fabsf(sx[0]) > (float)g_w*16.0f || fabsf(sx[1]) > (float)g_w*16.0f ||
        fabsf(sx[2]) > (float)g_w*16.0f || fabsf(sy[0]) > (float)g_h*16.0f ||
        fabsf(sy[1]) > (float)g_h*16.0f || fabsf(sy[2]) > (float)g_h*16.0f) {
        rw_collect_fail_reason=3;
        rw_collect_failed=1;
        return 1;
    }
    /* The callback runs after portablegl clipping, but a triangle can still
     * lie wholly outside the current viewport/scissor.  Dropping it here is
     * exact (it cannot touch a pixel) and prevents an off-screen outlier from
     * expanding the GPU dispatch to the whole framebuffer. */
    if (maxx <= (float)g_ctx.lx || minx >= (float)g_ctx.ux ||
        maxy <= (float)g_ctx.ly || miny >= (float)g_ctx.uy)
        return 1;
    if ((sx[1]-sx[0])*(sy[2]-sy[0]) -
        (sy[1]-sy[0])*(sx[2]-sx[0]) == 0.0f)
        return 1;
    /* portablegl recursively splits each triangle at every clip plane. Its
     * subdivision can emit more triangles than a polygon clipped then fanned
     * once, so never assume the polygon's seven-triangle bound here. */
    /* Collect directly into DMA staging. Grow only when post-clip output
     * exceeds its reservation; preserve the older triangles in that batch. */
    if(!rw_attr.px || !rw_attr_grow(rw_tri_count+3)) {
        rw_collect_fail_reason=4;
        rw_collect_failed=1;
        return 1;
    }
    size_t first = rw_tri_count;
    for(int i=0;i<3;i++) {
        const glVertex *v=vertices[i];
        float *out=(float*)rw_attr.px+rw_tri_count++*20;
        float iw=siw[i];
        out[0]=sx[i];out[1]=sy[i];
        /* rw_project_clip has already mapped NDC z to the window depth range
         * shared with portablegl's CPU rasterizer. */
        out[2]=sz[i];
        out[3]=iw;
        if(out[0] < 0.0f && !rw_collect_dumped && rw_dump_enabled) {
            /* post-clip vertices are always inside: x<0 means garbage fields.
             * Dump raw vertex state for host-side analysis. */
            rw_collect_dumped=1;
            FILE *f=fopen("/tmp/rwvert.bin","wb");
            if(f) {
                for(int k=0;k<3;k++) {
                    const glVertex *vv=vertices[k];
                    fwrite(&vv->screen_space,sizeof(vec4),1,f);
                    fwrite(&vv->clip_space,sizeof(vec4),1,f);
                    fwrite(vv->vs_out,sizeof(float),16,f);
                }
                fwrite(&e->rw_uv,sizeof(int),1,f);
                fwrite(&e->rw_color,sizeof(int),1,f);
                fwrite(&e->rw_fog,sizeof(int),1,f);
                fclose(f);
                printf("PGL RW GPU dumped garbage vertices x=%g y=%g\n",out[0],out[1]);
            }
        }
        memcpy(out+4,v->vs_out+e->rw_uv*4,8);
        memcpy(out+6,v->vs_out+e->rw_color*4,16);
        out[10]=v->vs_out[e->rw_fog*4];out[11]=0;
        if (fabsf(out[10]-1.0f)>1.0e-5f)
            rw_batch_fog_enabled=1;
        if(e->rw_simple==2) {
            memcpy(out+12,v->vs_out+e->rw_uv1*4,8);
            memcpy(out+14,v->vs_out+e->rw_envcolor*4,16);
            /* The matfx kernel consumes slots 12..17; keep the two padding
             * words deterministic without clearing the whole eight-word tail
             * for every vertex.  The simple kernel never reads this tail. */
            out[18]=out[19]=0.0f;
        }
    }
    rw_chunk_box_update(first, sx, sy);
    return 1;
}

/* Triangle collection consumes the portablegl draw through the callback.  If
 * the GPU path later declines that collected batch (for example because its
 * projected area is below the dispatch threshold), the original draw must be
 * replayed after older GPU work has landed.  Replaying here also preserves
 * painter's order when a compatible batch was already pending. */
static int rw_replay_cpu(GLenum mode, int first, int count, GLenum type,
                         const void *indices, size_t previous)
{
    rw_tri_count = previous;
    rw_chunk_boxes_rebuild(previous);
    if (!rw_gpu_flush()) return 0;
    if (type) pgl_DrawElements(mode, count, type, indices);
    else pgl_DrawArrays(mode, first, count);
    return 1;
}

static int rw_gpu_flush(void)
{
    if(!rw_tri_count) return !rw_blocked;
    { static long rw_fl=0; ++rw_fl;
      const char *trace=getenv("PGL_RW_TRACE");
      if((trace && atoi(trace)) && (rw_fl <= 20 || rw_fl % 100 == 0)) {
        float mnx=1e9f,mny=1e9f,mnz=1e9f,mnw=1e9f,mxx=-1e9f,mxy=-1e9f,mxz=-1e9f,mxw=-1e9f;
        for(size_t i=0;i<rw_tri_count;i++) {
            const float *v=(const float*)rw_attr.px+i*20;
            mnx=fminf(mnx,v[0]); mxx=fmaxf(mxx,v[0]);
            mny=fminf(mny,v[1]); mxy=fmaxf(mxy,v[1]);
            mnz=fminf(mnz,v[2]); mxz=fmaxf(mxz,v[2]);
            mnw=fminf(mnw,v[3]); mxw=fmaxf(mxw,v[3]);
        }
        printf("PGL RW GPU flush #%ld tris=%u tex=%ux%u tex1=%ux%u depth=%d blend=%d clip=%d,%d,%d,%d x[%.1f..%.1f] y[%.1f..%.1f] z[%.3f..%.3f] iw[%.4f..%.4f]\n",
               rw_fl, (unsigned)rw_tri_count, (unsigned)rw_batch.w, (unsigned)rw_batch.h,
               (unsigned)rw_batch.w1, (unsigned)rw_batch.h1, rw_batch.depth, rw_batch.blend[0],
               rw_batch.clip[0],rw_batch.clip[1],rw_batch.clip[2],rw_batch.clip[3],
               mnx,mxx, mny,mxy, mnz,mxz, mnw,mxw);
      }
      if(rw_fl == 1 && rw_dump_enabled) {
        FILE *f=fopen("/tmp/rwtris.bin","wb");
        if(f) { fwrite(rw_attr.px,20*4,rw_tri_count,f); fclose(f);
                printf("PGL RW GPU dumped %u vertices (%u tris) to /tmp/rwtris.bin\n",
                       (unsigned)rw_tri_count, (unsigned)(rw_tri_count/3)); }
      }
    }
    rw_texture *tex=rw_batch_texture,*tex1=rw_batch_texture1;
    uint32_t split=rw_direct_mode ? 0u : rw_xs.stage[PGLC_STAGE_COMPUTE].uniform_blob_dwords;
    uint32_t nwords=split+rw_rs.stage[PGLC_STAGE_COMPUTE].uniform_blob_dwords;
    if (rw_tri_count > RW_GPU_DISPATCH_VERTS &&
        !rw_ensure(&rw_chunk, RW_GPU_DISPATCH_VERTS * 20u * 4u)) {
        rw_blocked = 1;
        rw_tri_count = 0;
        rw_chunk_boxes_clear();
        return 0;
    }
    for(size_t start=0;start<rw_tri_count;start+=RW_GPU_DISPATCH_VERTS) {
        uint32_t n=(uint32_t)(rw_tri_count-start);
        if(n>RW_GPU_DISPATCH_VERTS) n=RW_GPU_DISPATCH_VERTS;
        int attr_shm=rw_attr.shm;
        uint32_t attr_bytes=rw_attr.cap;
        if (start) {
            memcpy(rw_chunk.px, (const uint8_t *)rw_attr.px + start*20u*4u,
                   (size_t)n*20u*4u);
            attr_shm=rw_chunk.shm;
            attr_bytes=rw_chunk.cap;
        }
        g2d_pgl_req_t req;
        int target[4]={g_w,g_h,(int)n/3,rw_batch.depth};
        /* Reject whole workgroups outside this mesh before the shader loads
         * triangle candidates. Small world objects must not scan every tile. */
        rw_chunk_box box = rw_chunk_boxes[start / RW_GPU_DISPATCH_VERTS];
        float lx=box.lx,ly=box.ly,ux=box.ux,uy=box.uy;
        int clip[4]={ (int)floorf(lx), (int)floorf(ly), (int)ceilf(ux), (int)ceilf(uy) };
        if(clip[0]<rw_batch.clip[0]) clip[0]=rw_batch.clip[0];
        if(clip[1]<rw_batch.clip[1]) clip[1]=rw_batch.clip[1];
        if(clip[2]>rw_batch.clip[2]) clip[2]=rw_batch.clip[2];
        if(clip[3]>rw_batch.clip[3]) clip[3]=rw_batch.clip[3];
        if(clip[0]<0) clip[0]=0;
        if(clip[1]<0) clip[1]=0;
        if(clip[2]>g_w) clip[2]=g_w;
        if(clip[3]>g_h) clip[3]=g_h;
        if(clip[0]>=clip[2] || clip[1]>=clip[3]) continue;
        memset(rw_unif.px,0,nwords*4);
        if (!rw_direct_mode)
            rw_patch(&rw_xs.stage[PGLC_STAGE_COMPUTE],0,"nverts",&n,1);
        rw_patch(&rw_rs.stage[PGLC_STAGE_COMPUTE],split,"target",target,4);
        rw_patch(&rw_rs.stage[PGLC_STAGE_COMPUTE],split,"clipRect",clip,4);
        {
            /* The compute launch window is expressed in framebuffer rows
             * (top-left origin), while RW/portablegl screen coordinates use
             * an OpenGL bottom-left origin.  X is unchanged; the first
             * framebuffer row for [clip[1], clip[3]) is H-clip[3].  Passing
             * clip[1] here launches the opposite vertical band and leaves
             * most pixels disabled by the shader's clipRect test. */
            int dispatch_offset[2]={clip[0],g_h-clip[3]};
            rw_patch(&rw_rs.stage[PGLC_STAGE_COMPUTE],split,
                "dispatchOffset",dispatch_offset,2);
        }
        rw_patch(&rw_rs.stage[PGLC_STAGE_COMPUTE],split,"blendState",rw_batch.blend,4);
        rw_patch(&rw_rs.stage[PGLC_STAGE_COMPUTE],split,"texState",rw_batch.ts,4);
        {
            float fog[4];
            memcpy(fog,rw_batch.fog,sizeof fog);
            /* fogColor.a is unused by the RW fragment contract and serves as
             * a uniform branch flag for the compute rasterizer. */
            fog[3]=rw_batch_fog_enabled ? 1.0f : 0.0f;
            rw_patch(&rw_rs.stage[PGLC_STAGE_COMPUTE],split,"fogColor",fog,4);
        }
        /* These values are draw/batch state, not per-vertex state.  Patch them
         * once immediately before dispatch; doing this from rw_collect for
         * every vertex needlessly copied the same uniform block millions of
         * times per frame. */
        rw_patch(&rw_rs.stage[PGLC_STAGE_COMPUTE],split,"alphaRef",rw_batch.alpha,4);
        rw_patch(&rw_rs.stage[PGLC_STAGE_COMPUTE],split,"envState",rw_batch.ts1,4);
        rw_patch(&rw_rs.stage[PGLC_STAGE_COMPUTE],split,"fxParams",rw_batch.fx,4);
        /* Keep the declared span stable for rw_attr's shm id.  The shader
         * consumes only `n` vertices; the fixed span prevents an older g2dd
         * attach cache from retaining a short mapping across draw layouts. */
        g2d_pgl_draw_req_init(&req,g2d_canvas(rw_fb.shm,g_w*g_h*4,g_w,g_h,1),
            g2d_canvas(attr_shm,attr_bytes,n,20,1),g2d_canvas(0,0,0,0,0),
            G2D_PGL_MODE_TRIANGLES,n,G2D_PGL_FMT_RGBA8,g_w*4,G2D_PGL_FS_SOLID,0,0,
            G2D_PGL_DEPTH_NONE,0,G2D_PGL_CULL_NONE,g2d_canvas(rw_z.shm,g_w*g_h*4,g_w,g_h,1),g_w*4,0,0,1.0f);
        /* g2d's dispatch rectangle is top-left-origin, matching
         * dispatchOffset and the linear framebuffer layout. */
        req.ext.dispatch_x=clip[0];
        req.ext.dispatch_y=g_h-clip[3];
        req.ext.dispatch_w=clip[2]-clip[0];
        req.ext.dispatch_h=clip[3]-clip[1];
        g2d_pgl_draw_req_set_shader(&req,rw_program,rw_unif.shm,nwords,1);
        req.ctex[0]=g2d_canvas(tex->canvas.shm,tex->layout.size,rw_batch.w,rw_batch.h,1);
        req.ctex_w[0]=rw_batch.w;req.ctex_h[0]=rw_batch.h;req.ctex_stride[0]=tex->layout.stride;
        req.ctex_tiling[0]=tex->layout.tiling;
        req.ctex_ub_pad[0]=G2D_PGL_CTEX_UB_PAD_PACK(tex->layout.ub_pad,
            rw_batch.ts[2]?G2D_PGL_CTEX_FILTER_LINEAR:G2D_PGL_CTEX_FILTER_NEAREST);
        req.ctex[1]=g2d_canvas(tex1->canvas.shm,tex1->layout.size,rw_batch.w1,rw_batch.h1,1);
        req.ctex_w[1]=rw_batch.w1;req.ctex_h[1]=rw_batch.h1;req.ctex_stride[1]=tex1->layout.stride;
        req.ctex_tiling[1]=tex1->layout.tiling;
        req.ctex_ub_pad[1]=G2D_PGL_CTEX_UB_PAD_PACK(tex1->layout.ub_pad,
            rw_batch.ts1[2]?G2D_PGL_CTEX_FILTER_LINEAR:G2D_PGL_CTEX_FILTER_NEAREST);
        ++g_prof_submits;
        unsigned long long submit_start=profile_enabled()?profile_us():0;
        int rc=g2d_pgl_draw(&req);
        if(submit_start) g_prof_submit_us+=profile_us()-submit_start;
        if(rc!=G2D_OK) {
            printf("PGL RW GPU draw declined/failed %d\n",rc);rw_blocked=1;
            rw_terminal=rc==G2D_ERR_TIMEOUT || rc==G2D_ERR_MMU_FAULT;
            g2d_quiesce_on_fault(rc);
            rw_gpu_detach();
            rw_tri_count=0;
            rw_chunk_boxes_clear();
            return 0;
        }
    }
    rw_tri_count=0;rw_chunk_boxes_clear();rw_verified=1;
    { static long rw_done=0; ++rw_done;
      const char *trace=getenv("PGL_RW_TRACE");
      if((trace && atoi(trace)) && (rw_done <= 20 || rw_done % 100 == 0))
        printf("PGL RW GPU flush #%ld ok\n", rw_done); }
    return 1;
}
static int rw_gpu_draw_inner(GLenum mode,int first,int count,GLenum type,const void *indices)
{
    interp_entry *e=g_active;
    GLuint name,name1;
    const glTexture *t,*t1;
    rw_texture *tex,*tex1;
    uint32_t nwords,split;
    int src=rw_factor(g_ctx.blend_sRGB),dst=rw_factor(g_ctx.blend_dRGB),df=rw_depth_func();
    if(!e||!e->rw_simple||count<=0||count>20480||g_ctx.poly_offset_fill||g_ctx.stencil_test||
       (mode!=GL_TRIANGLES&&mode!=GL_TRIANGLE_STRIP&&mode!=GL_TRIANGLE_FAN)||df<0) return 0;
    if(!rw_gpu_allowed()) return 0;
#if !defined(PGL_D24S8) || !defined(PGL_ARGB32)
    return 0;
#endif
    if(g_ctx.blend&&(src<0||dst<0||g_ctx.blend_sA!=g_ctx.blend_sRGB||g_ctx.blend_dA!=g_ctx.blend_dRGB||
        g_ctx.blend_eqRGB!=GL_FUNC_ADD||g_ctx.blend_eqA!=GL_FUNC_ADD)) return 0;
#ifndef PGL_DISABLE_COLOR_MASK
    if(g_ctx.color_mask!=(pix_t)~0u) return 0;
#endif
    name=gles_engine_texture_for_unit(e->ublock[e->rw_sampler].i);
    if(!name || name>=(GLuint)g_ctx.textures.size) return 0;
    t=&g_ctx.textures.a[name];
    if(t->deleted||!t->data||t->w<=0||t->h<=0||t->type!=GL_TEXTURE_2D-(GL_TEXTURE_UNBOUND+1)||
       rw_wrap(t->wrap_s)<0||rw_wrap(t->wrap_t)<0||!rw_texture_format_ok(t->format)) return 0;
    name1=e->rw_simple==2?gles_engine_texture_for_unit(e->ublock[e->rw_sampler1].i):name;
    if(!name1 || name1>=(GLuint)g_ctx.textures.size) return 0;
    t1=&g_ctx.textures.a[name1];
    if(t1->deleted||!t1->data||t1->w<=0||t1->h<=0||t1->type!=GL_TEXTURE_2D-(GL_TEXTURE_UNBOUND+1)||
       rw_wrap(t1->wrap_s)<0||rw_wrap(t1->wrap_t)<0||!rw_texture_format_ok(t1->format)) return 0;
    if(!rw_gpu_compile()) return 0;
    if (profile_enabled() && !rw_profile_thresholds_dumped) {
        printf("PGL_PROFILE rw_thresholds min_tris=%d min_pixels=%d viewport=%dx%d\n",
               rw_gpu_min_tris(), rw_gpu_min_pixels(), g_w, g_h);
        fflush(stdout);
        rw_profile_thresholds_dumped = 1;
    }
    { static int once=0; if(!once) { once=1;
      printf("PGL RW GPU route adopted w=%d h=%d prog=%d kind=%d\n",
             g_w,g_h,rw_program,e->rw_simple); } }
    rw_gpu_legacy_flush();
    /* Reserve the unclipped size; the DMA staging canvas has a fixed
     * per-dispatch capacity and an overrun takes the safe CPU replay path. */
    size_t ntri=mode==GL_TRIANGLES?(size_t)count/3:(count>2?(size_t)count-2:0);
    /* Keep small meshes on the CPU.  The wrapper flushes any earlier GPU batch
     * before replaying this draw, preserving painter's order. */
    if ((int)ntri < rw_gpu_min_tris()) {
        if (profile_enabled()) ++g_prof_rw_small;
        return 0;
    }
    struct rw_batch_key key;memset(&key,0,sizeof key);
    key.name=name;key.gen=t->datagen;key.w=t->w;key.h=t->h;key.depth=df;
    key.clip[0]=g_ctx.lx;key.clip[1]=g_ctx.ly;key.clip[2]=g_ctx.ux;key.clip[3]=g_ctx.uy;
    key.blend[0]=g_ctx.blend;key.blend[1]=g_ctx.blend?src:0;key.blend[2]=g_ctx.blend?dst:0;
    key.blend[3]=g_ctx.depth_test&&g_ctx.depth_mask;
    key.ts[0]=rw_wrap(t->wrap_s);key.ts[1]=rw_wrap(t->wrap_t);key.ts[2]=t->mag_filter==GL_LINEAR;
    key.ts[3]=e->rw_simple==2;
    key.name1=name1;key.gen1=t1->datagen;key.w1=t1->w;key.h1=t1->h;
    key.ts1[0]=rw_wrap(t1->wrap_s);key.ts1[1]=rw_wrap(t1->wrap_t);key.ts1[2]=t1->mag_filter==GL_LINEAR;
    if(e->rw_simple==2) for(int i=0;i<4;i++) key.fx[i]=e->ublock[e->rw_fx+i].f;
    for(int i=0;i<4;i++) {key.fog[i]=e->ublock[e->rw_fogcolor+i].f;key.alpha[i]=e->ublock[e->rw_alpha+i].f;}
    if(!e->rw_alpha_test) {
        key.alpha[0]=-INFINITY;
        key.alpha[1]=INFINITY;
    } else if(!isfinite(key.alpha[0]) || !isfinite(key.alpha[1]) ||
              key.alpha[1] <= key.alpha[0]) {
        /* The first RW draw can arrive before the front-end has copied the
         * alpha range into its uniform block.  A zero/empty interval is not
         * a valid RenderWare alpha function (ALWAYS/GE/LESS all have a
         * strictly ordered range); treating it as an empty range makes the
         * compute shader discard every pixel and produces a black first
         * frame.  Match the CPU path's default alpha state instead. */
        key.alpha[0]=-INFINITY;
        key.alpha[1]=INFINITY;
    }
    if(rw_tri_count && (memcmp(&key,&rw_batch,sizeof key) ||
                        rw_tri_count+ntri*3>RW_GPU_MAX_VERTS))
        if(!rw_gpu_flush()) return 0;
    /* A clipped draw can expand beyond its input triangle count. Select a
     * class for the input reservation and flush before changing classes while
     * a batch is open; the callback still performs the final exact capacity
     * check and replays to CPU if clipping exceeds the reservation. */
    if (rw_tri_count &&
        (rw_tri_count + ntri * 3u) * 20u * 4u > rw_attr.cap)
        if(!rw_gpu_flush()) return 0;
    tex=rw_gpu_texture(name,t,NULL);
    tex1=rw_gpu_texture(name1,t1,tex);
    split=rw_direct_mode ? 0u : rw_xs.stage[PGLC_STAGE_COMPUTE].uniform_blob_dwords;
    nwords=split+rw_rs.stage[PGLC_STAGE_COMPUTE].uniform_blob_dwords;
    if(!tex||!tex1||!rw_attr_select((rw_tri_count+ntri*3u)*20u*4u) ||
       !rw_ensure(&rw_unif,nwords*4)||!rw_gpu_target()) return 0;
    rw_batch=key;rw_batch_texture=tex;rw_batch_texture1=tex1;
    if (!rw_tri_count) {
        rw_chunk_boxes_clear();
        rw_batch_fog_enabled=0;
    }
    size_t previous=rw_tri_count;rw_collect_failed=0;
    pglSetClipTriangleCallback(rw_collect,e);
    unsigned long long collect_start=profile_enabled()?profile_us():0;
    if(type) pgl_DrawElements(mode,count,type,indices); else pgl_DrawArrays(mode,first,count);
    if(collect_start) g_prof_rw_collect_us+=profile_us()-collect_start;
    pglSetTriangleCallback(NULL,NULL);
    if(rw_collect_failed)
    {
        if (profile_enabled()) {
            ++g_prof_rw_replay;
            printf("PGL_PROFILE rw_collect_replay reason=%d verts=%u mode=%u count=%d\n",
                   rw_collect_fail_reason, (unsigned)rw_tri_count,
                   (unsigned)mode, count);
        }
        return rw_replay_cpu(mode, first, count, type, indices, previous);
    }
    /* Collection has already done the CPU VS and clipping work, so a tiny
     * projected batch is replayed by the normal CPU path instead of paying a
     * synchronous GPU dispatch.  Roll back only the newly collected data. */
    if (rw_gpu_batch_is_tiny(previous)) {
        if (profile_enabled()) ++g_prof_rw_small;
        ++g_prof_rw_replay;
        return rw_replay_cpu(mode, first, count, type, indices, previous);
    }
    const char *batching=getenv("PGL_RW_BATCH");
    if(!rw_verified || (batching && !atoi(batching))) return rw_gpu_flush();
    return 1;
}
static int rw_gpu_draw(GLenum mode,int first,int count,GLenum type,const void *indices)
{
    unsigned long long route_start=profile_enabled()?profile_us():0;
    if (g_active && g_active->rw_native_vs)
        rw_native_prepare(g_active);
    int handled=rw_gpu_draw_inner(mode,first,count,type,indices);
    if(!handled) {
        if (profile_enabled()) ++g_prof_rw_decline;
        rw_gpu_flush();
    }
    if(route_start) g_prof_rw_route_us+=profile_us()-route_start;
    return handled;
}

#else
static int rw_gpu_shm(void) { return 0; }
static void rw_gpu_detach(void) {}
static int rw_gpu_flush(void) {return 1;}
static void rw_gpu_fini(void) {}
static void rw_gpu_texture_deleted(GEint n,const GEuint *names) {(void)n;(void)names;}
#endif
