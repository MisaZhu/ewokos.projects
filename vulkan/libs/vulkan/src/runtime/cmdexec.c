/*
 * cmdexec.c - command recording primitives and the immediate-mode executor.
 * Command buffers record into a growable command list (vk_cmd_push); the bound
 * pipeline/descriptor/vertex/viewport state is snapshotted into each draw and
 * dispatch at record time (vk_snapshot_state).  vkQueueSubmit runs the recorded
 * list in order: memory-transfer / clear / query / event commands execute here
 * directly (they are back-end agnostic host memory operations), while draw and
 * dispatch are delegated to the device's vk_device_backend (cpu or gpu).  This
 * is the CPU analog of OpenCL's queue enqueue-and-execute model.
 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* ---- recording primitives ------------------------------------------------ */
vk_cmd *vk_cmd_push(vk_command_buffer *cb, vk_cmd_op op)
{
    vk_cmd *c;
    if (!cb)
        return NULL;
    if (cb->num_cmds >= cb->cap_cmds) {
        uint32_t ncap = cb->cap_cmds ? cb->cap_cmds * 2 : 64;
        vk_cmd *nc = (vk_cmd *)realloc(cb->cmds, (size_t)ncap * sizeof(vk_cmd));
        if (!nc)
            return NULL;
        cb->cmds = nc;
        cb->cap_cmds = ncap;
    }
    c = &cb->cmds[cb->num_cmds++];
    memset(c, 0, sizeof(*c));
    c->op = op;
    return c;
}

void vk_snapshot_state(vk_command_buffer *cb, vk_cmd *c)
{
    /* fill the draw command's execution-time snapshot from the bound state */
    memcpy(c->u.draw.sets, cb->sets, sizeof(cb->sets));
    c->u.draw.first_set = cb->first_set;
    c->u.draw.set_count = cb->set_count;
    memcpy(c->u.draw.push, cb->push, sizeof(cb->push));
    c->u.draw.push_size = cb->push_size;
    memcpy(c->u.draw.vb, cb->vb, sizeof(cb->vb));
    memcpy(c->u.draw.vb_off, cb->vb_off, sizeof(cb->vb_off));
    c->u.draw.ib = cb->ib;
    c->u.draw.ib_off = cb->ib_off;
    c->u.draw.ib_type = cb->ib_type;
    c->u.draw.vp = cb->vp;
    c->u.draw.sc = cb->sc;
    c->u.draw.has_vp = cb->has_vp;
    c->u.draw.has_sc = cb->has_sc;
    memcpy(c->u.draw.color, cb->color, sizeof(cb->color));
    c->u.draw.num_color = cb->num_color;
    c->u.draw.depth = cb->depth;
    c->u.draw.rt_width = cb->rt_width;
    c->u.draw.rt_height = cb->rt_height;
}

/* ---- image / attachment clear helpers ------------------------------------ */
static uint8_t cc_u8(float v)
{
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    return (uint8_t)(v * 255.0f + 0.5f);
}

static void encode_clear(VkFormat f, uint8_t *p, const VkClearColorValue *c)
{
    switch (f) {
    case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB:
        p[0]=cc_u8(c->float32[0]); p[1]=cc_u8(c->float32[1]);
        p[2]=cc_u8(c->float32[2]); p[3]=cc_u8(c->float32[3]); break;
    case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_B8G8R8A8_SRGB:
        p[0]=cc_u8(c->float32[2]); p[1]=cc_u8(c->float32[1]);
        p[2]=cc_u8(c->float32[0]); p[3]=cc_u8(c->float32[3]); break;
    case VK_FORMAT_R8G8B8A8_UINT: case VK_FORMAT_R32G32B32A32_UINT:
        if (f == VK_FORMAT_R8G8B8A8_UINT) {
            p[0]=(uint8_t)c->uint32[0]; p[1]=(uint8_t)c->uint32[1];
            p[2]=(uint8_t)c->uint32[2]; p[3]=(uint8_t)c->uint32[3];
        } else {
            memcpy(p, c->uint32, 16);
        }
        break;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        memcpy(p, c->float32, 16); break;
    case VK_FORMAT_R32G32B32_SFLOAT:
        memcpy(p, c->float32, 12); break;
    case VK_FORMAT_R8_UNORM:
        p[0]=cc_u8(c->float32[0]); break;
    default: break;
    }
}

/* Mark an image's backing memory as freshly written.  The GPU graphics path
 * caches each sampled image's TMU tiling keyed on this version (see vkclient's
 * _tex_key), so every host-side pixel write into an image funnels a bump here
 * to guarantee the next draw that samples it re-tiles rather than reading a
 * stale tile.  A no-op when the image has no backing. */
static inline void image_content_touch(vk_image *im)
{
    if (im && im->mem)
        im->mem->content_version++;
}

/* clear the base mip/layer of an image to a color value */
static void clear_color_image(vk_image *im, const VkClearColorValue *c)
{
    uint8_t *base = (uint8_t *)vk_image_ptr(im);
    uint32_t bpp, pitch, rowbytes, x, y, layer;
    uint8_t texel[16];
    if (!base) return;
    bpp = im->bpp ? im->bpp : vk_format_bpp(im->format);
    if (!bpp || bpp > 16) return;
    pitch = im->row_pitch ? im->row_pitch : bpp * im->extent.width;
    rowbytes = bpp * im->extent.width;
    if (rowbytes == 0) return;
    encode_clear(im->format, texel, c);
    for (layer = 0; layer < im->array_layers; layer++) {
        uint8_t *lb = base + (size_t)layer * pitch * im->extent.height;
        /* A full-attachment clear is width*height copies of ONE texel.  Fill
         * scanline 0 texel-by-texel, then memcpy it down the remaining rows:
         * ~width+height calls instead of width*height.  The executor clears on
         * every begin_render_pass, so the old per-pixel loop cost several ms per
         * frame at 640x480 (the GPU back-end then re-clears via its first_frame
         * fill, so this host clear only matters for the CPU-raster fallback).
         * Row padding [rowbytes,pitch) is left untouched, exactly as before. */
        for (x = 0; x < im->extent.width; x++)
            memcpy(lb + (size_t)x * bpp, texel, bpp);
        for (y = 1; y < im->extent.height; y++)
            memcpy(lb + (size_t)y * pitch, lb, rowbytes);
    }
    image_content_touch(im);
}

static void clear_depth_image(vk_image *im, const VkClearDepthStencilValue *ds)
{
    uint8_t *base = (uint8_t *)vk_image_ptr(im);
    uint32_t bpp, pitch, rowbytes, x, y;
    uint8_t texel[8];
    if (!base) return;
    bpp = im->bpp ? im->bpp : vk_format_bpp(im->format);
    if (!bpp || bpp > 8) return;
    pitch = im->row_pitch ? im->row_pitch : bpp * im->extent.width;
    rowbytes = bpp * im->extent.width;
    if (rowbytes == 0) return;
    /* The depth/stencil clear value is a single scalar, so every pixel of the
     * attachment encodes to the SAME bpp-byte word.  Encode it once into texel,
     * fill scanline 0, then memcpy it down the rest: ~width+height calls instead
     * of a per-pixel switch over the whole attachment every frame (the old loop
     * cost several ms per frame at 640x480).  The GPU back-end keeps its own
     * private z canvas and never reads this host copy, so it only matters for
     * the CPU-raster fallback.  Row padding is left untouched, as before. */
    memset(texel, 0, sizeof(texel));
    switch (im->format) {
    case VK_FORMAT_D32_SFLOAT: {
        float f = ds->depth;
        memcpy(texel, &f, 4); break;
    }
    case VK_FORMAT_D16_UNORM: {
        float z = ds->depth < 0 ? 0 : (ds->depth > 1 ? 1 : ds->depth);
        uint16_t h = (uint16_t)(z * 65535.0f + 0.5f);
        memcpy(texel, &h, 2); break;
    }
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT_S8_UINT: {
        float z = ds->depth < 0 ? 0 : (ds->depth > 1 ? 1 : ds->depth);
        uint32_t d = (uint32_t)(z * 16777215.0f + 0.5f);
        uint32_t v = ((uint32_t)(ds->stencil & 0xff) << 24) | (d & 0x00ffffffu);
        if (im->format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
            float f = ds->depth;
            memcpy(texel, &f, 4);
            texel[4] = (uint8_t)(ds->stencil & 0xff);
        } else {
            memcpy(texel, &v, 4);
        }
        break;
    }
    default: return;   /* not a depth/stencil format this encoder knows */
    }
    for (x = 0; x < im->extent.width; x++)
        memcpy(base + (size_t)x * bpp, texel, bpp);
    for (y = 1; y < im->extent.height; y++)
        memcpy(base + (size_t)y * pitch, base, rowbytes);
    image_content_touch(im);
}

static void clear_view(vk_image_view *iv, const VkClearValue *val, int is_depth)
{
    if (!iv || !iv->image) return;
    if (is_depth) clear_depth_image(iv->image, &val->depthStencil);
    else          clear_color_image(iv->image, &val->color);
}

/* apply the load ops of a render pass against its framebuffer */
static void exec_begin_render_pass(const vk_cmd *c, int defer_gpu_clear)
{
    const vk_render_pass *rp = c->u.begin_rp.rp;
    const vk_framebuffer *fb = c->u.begin_rp.fb;
    uint32_t i;
    if (!rp || !fb) return;
    for (i = 0; i < rp->num_attachments && i < fb->num_views; i++) {
        const vk_attachment_desc *a = &rp->attachments[i];
        int is_depth = (a->format == VK_FORMAT_D16_UNORM ||
                        a->format == VK_FORMAT_D24_UNORM_S8_UINT ||
                        a->format == VK_FORMAT_D32_SFLOAT ||
                        a->format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                        a->format == VK_FORMAT_X8_D24_UNORM_PACK32);
        VkAttachmentLoadOp op = is_depth ? a->stencil_load : a->load;
        if (defer_gpu_clear &&
            ((rp->num_color_refs > 0 && i == rp->color_refs[0].attachment) ||
             (rp->has_depth && i == rp->depth_ref.attachment)))
            continue;
        if (op == VK_ATTACHMENT_LOAD_OP_CLEAR && i < c->u.begin_rp.num_clears)
            clear_view(fb->views[i], &c->u.begin_rp.clears[i], is_depth);
    }
}

static void exec_begin_rendering(const vk_cmd *c, int defer_gpu_clear)
{
    /* The recorder flags a loadOp==CLEAR attachment by storing the sentinel
     * VK_LAYOUT_CLEAR_EWOK in its color_layout / depth_layout slot. */
    uint32_t i;
    for (i = 0; i < c->u.begin_rendering.num_color; i++) {
        vk_image_view *iv = c->u.begin_rendering.color[i];
        if (iv && c->u.begin_rendering.color_layout[i] == VK_LAYOUT_CLEAR_EWOK &&
            !(defer_gpu_clear && i == 0))
            clear_view(iv, &c->u.begin_rendering.color_clear[i], 0);
    }
    if (c->u.begin_rendering.depth &&
        c->u.begin_rendering.depth_layout == VK_LAYOUT_CLEAR_EWOK &&
        !defer_gpu_clear)
        clear_view(c->u.begin_rendering.depth, &c->u.begin_rendering.depth_clear, 1);
}

/* ---- GPU clear handoff ---------------------------------------------------
 * The GPU graphics back-end reproduces a render pass's loadOp==CLEAR on the
 * device (its first_frame fill) and needs the clear values plus a "this is the
 * pass's first draw" flag.  The executor carries that state from the begin
 * command to the draws that follow it; the values mirror what clear_view()
 * just applied to host memory, so the GPU fill and the CPU/host clear agree. */
typedef struct {
    int   first_draw;         /* the next draw is the pass's first            */
    int   color_clear;        /* colour attachment[0] loadOp was CLEAR        */
    float clear_color[4];     /* RGBA float clear value                       */
    int   depth_clear;        /* the depth attachment loadOp was CLEAR        */
    float clear_depth;        /* depth clear value                            */
} gfx_clear_state;

static void gfx_clear_from_render_pass(const vk_cmd *c, gfx_clear_state *cs)
{
    const vk_render_pass *rp = c->u.begin_rp.rp;
    uint32_t idx;

    cs->first_draw = 1;
    cs->color_clear = 0;
    cs->depth_clear = 0;
    cs->clear_depth = 1.0f;
    if (!rp)
        return;
    if (rp->num_color_refs > 0) {
        idx = rp->color_refs[0].attachment;
        if (idx < rp->num_attachments && idx < c->u.begin_rp.num_clears &&
            rp->attachments[idx].load == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            const VkClearValue *cv = &c->u.begin_rp.clears[idx];
            cs->color_clear = 1;
            cs->clear_color[0] = cv->color.float32[0];
            cs->clear_color[1] = cv->color.float32[1];
            cs->clear_color[2] = cv->color.float32[2];
            cs->clear_color[3] = cv->color.float32[3];
        }
    }
    if (rp->has_depth) {
        idx = rp->depth_ref.attachment;
        if (idx < rp->num_attachments && idx < c->u.begin_rp.num_clears &&
            rp->attachments[idx].load == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            cs->depth_clear = 1;
            cs->clear_depth = c->u.begin_rp.clears[idx].depthStencil.depth;
        }
    }
}

static void gfx_clear_from_rendering(const vk_cmd *c, gfx_clear_state *cs)
{
    cs->first_draw = 1;
    cs->color_clear = 0;
    cs->depth_clear = 0;
    cs->clear_depth = 1.0f;
    if (c->u.begin_rendering.num_color > 0 &&
        c->u.begin_rendering.color_layout[0] == VK_LAYOUT_CLEAR_EWOK) {
        const VkClearValue *cv = &c->u.begin_rendering.color_clear[0];
        cs->color_clear = 1;
        cs->clear_color[0] = cv->color.float32[0];
        cs->clear_color[1] = cv->color.float32[1];
        cs->clear_color[2] = cv->color.float32[2];
        cs->clear_color[3] = cv->color.float32[3];
    }
    if (c->u.begin_rendering.depth &&
        c->u.begin_rendering.depth_layout == VK_LAYOUT_CLEAR_EWOK) {
        cs->depth_clear = 1;
        cs->clear_depth = c->u.begin_rendering.depth_clear.depthStencil.depth;
    }
}

static int gfx_can_defer_clear(const gfx_clear_state *cs)
{
    return cs->color_clear && cs->depth_clear && cs->clear_depth >= 1.0f;
}

/* Deferring the clear into the first draw is a GPU-backend optimisation only;
 * on CPU-only builds vk_backend_gpu does not exist (internal.h declares it
 * under VULKAN_GPU_BACKEND), so the answer is a constant false there. */
static int gfx_device_is_gpu(const vk_command_buffer *cb)
{
#ifdef VULKAN_GPU_BACKEND
    return cb->device && cb->device->backend == &vk_backend_gpu;
#else
    (void)cb;
    return 0;
#endif
}

void vk_clear_deferred(vk_draw_ctx *dc)
{
    VkClearValue cv;
    if (!dc->gfx_first_draw || dc->gfx_color_clear == 0 ||
        dc->gfx_depth_clear == 0 || dc->gfx_clear_depth < 1.0f)
        return;
    memcpy(cv.color.float32, dc->gfx_clear_color, sizeof(cv.color.float32));
    clear_view(dc->color[0], &cv, 0);
    memset(&cv, 0, sizeof(cv));
    cv.depthStencil.depth = dc->gfx_clear_depth;
    clear_view(dc->depth, &cv, 1);
}

/* ---- memory transfer ops ------------------------------------------------- */
static void exec_copy_buffer(const vk_cmd *c)
{
    const uint8_t *src = (const uint8_t *)vk_buffer_ptr(c->u.copy_buf.src);
    uint8_t *dst = (uint8_t *)vk_buffer_ptr(c->u.copy_buf.dst);
    uint32_t i;
    if (!src || !dst) return;
    for (i = 0; i < c->u.copy_buf.n; i++) {
        const VkBufferCopy *r = &c->u.copy_buf.regions[i];
        memmove(dst + r->dstOffset, src + r->srcOffset, (size_t)r->size);
    }
}

static void exec_fill_buffer(const vk_cmd *c)
{
    uint8_t *dst = (uint8_t *)vk_buffer_ptr(c->u.fill.dst);
    VkDeviceSize off = c->u.fill.offset, size = c->u.fill.size, i;
    uint32_t data = c->u.fill.data;
    if (!dst) return;
    if (size == VK_WHOLE_SIZE && c->u.fill.dst->mem)
        size = c->u.fill.dst->size - off;
    for (i = 0; i + 4 <= size; i += 4)
        memcpy(dst + off + i, &data, 4);
}

static void exec_update_buffer(const vk_cmd *c)
{
    uint8_t *dst = (uint8_t *)vk_buffer_ptr(c->u.update.dst);
    if (!dst) return;
    memcpy(dst + c->u.update.offset, c->u.update.data, (size_t)c->u.update.size);
}

static void exec_copy_buffer_to_image(const vk_cmd *c)
{
    const uint8_t *src = (const uint8_t *)vk_buffer_ptr(c->u.copy_b2i.src);
    vk_image *im = c->u.copy_b2i.dst;
    uint8_t *dbase = (uint8_t *)vk_image_ptr(im);
    uint32_t r, y, z;
    if (!src || !dbase || !im->bpp) return;
    for (r = 0; r < c->u.copy_b2i.n; r++) {
        const VkBufferImageCopy *rg = &c->u.copy_b2i.regions[r];
        uint32_t w = rg->imageExtent.width ? rg->imageExtent.width : 1;
        uint32_t h = rg->imageExtent.height ? rg->imageExtent.height : 1;
        uint32_t d = rg->imageExtent.depth ? rg->imageExtent.depth : 1;
        uint32_t rowbytes = w * im->bpp;
        const uint8_t *sp = src + rg->bufferOffset;
        for (z = 0; z < d; z++)
            for (y = 0; y < h; y++) {
                uint8_t *dp = dbase +
                    (size_t)(rg->imageOffset.z + z) * im->row_pitch * im->extent.height +
                    (size_t)(rg->imageOffset.y + y) * im->row_pitch +
                    (size_t)(rg->imageOffset.x) * im->bpp;
                memcpy(dp, sp, rowbytes);
                sp += rowbytes;
            }
    }
    image_content_touch(im);
}

static void exec_copy_image_to_buffer(const vk_cmd *c)
{
    vk_image *im = c->u.copy_i2b.src;
    const uint8_t *sbase = (const uint8_t *)vk_image_ptr(im);
    uint8_t *dst = (uint8_t *)vk_buffer_ptr(c->u.copy_i2b.dst);
    uint32_t r, y, z;
    if (!dst || !sbase || !im->bpp) return;
    for (r = 0; r < c->u.copy_i2b.n; r++) {
        const VkBufferImageCopy *rg = &c->u.copy_i2b.regions[r];
        uint32_t w = rg->imageExtent.width ? rg->imageExtent.width : 1;
        uint32_t h = rg->imageExtent.height ? rg->imageExtent.height : 1;
        uint32_t d = rg->imageExtent.depth ? rg->imageExtent.depth : 1;
        uint32_t rowbytes = w * im->bpp;
        uint8_t *dp = dst + rg->bufferOffset;
        for (z = 0; z < d; z++)
            for (y = 0; y < h; y++) {
                const uint8_t *sp = sbase +
                    (size_t)(rg->imageOffset.z + z) * im->row_pitch * im->extent.height +
                    (size_t)(rg->imageOffset.y + y) * im->row_pitch +
                    (size_t)(rg->imageOffset.x) * im->bpp;
                memcpy(dp, sp, rowbytes);
                dp += rowbytes;
            }
    }
}

static void exec_copy_image(const vk_cmd *c)
{
    vk_image *s = c->u.copy_img.src, *d = c->u.copy_img.dst;
    const uint8_t *sb = (const uint8_t *)vk_image_ptr(s);
    uint8_t *db = (uint8_t *)vk_image_ptr(d);
    uint32_t r, y, z;
    if (!sb || !db || !s->bpp || s->bpp != d->bpp) return;
    for (r = 0; r < c->u.copy_img.n; r++) {
        const VkImageCopy *rg = &c->u.copy_img.regions[r];
        uint32_t w = rg->extent.width, h = rg->extent.height, dep = rg->extent.depth;
        uint32_t rowbytes = w * s->bpp;
        for (z = 0; z < dep; z++)
            for (y = 0; y < h; y++) {
                const uint8_t *sp = sb +
                    (size_t)(rg->srcOffset.z + z) * s->row_pitch * s->extent.height +
                    (size_t)(rg->srcOffset.y + y) * s->row_pitch +
                    (size_t)rg->srcOffset.x * s->bpp;
                uint8_t *dp = db +
                    (size_t)(rg->dstOffset.z + z) * d->row_pitch * d->extent.height +
                    (size_t)(rg->dstOffset.y + y) * d->row_pitch +
                    (size_t)rg->dstOffset.x * d->bpp;
                memcpy(dp, sp, rowbytes);
            }
    }
    image_content_touch(d);
}

static void exec_blit_image(const vk_cmd *c)
{
    /* nearest-neighbour blit of each region (software path) */
    vk_image *s = c->u.blit.src, *d = c->u.blit.dst;
    const uint8_t *sb = (const uint8_t *)vk_image_ptr(s);
    uint8_t *db = (uint8_t *)vk_image_ptr(d);
    uint32_t r;
    if (!sb || !db || !s->bpp || s->bpp != d->bpp) return;
    for (r = 0; r < c->u.blit.n; r++) {
        const VkImageBlit *b = &c->u.blit.regions[r];
        int32_t sx0 = b->srcOffsets[0].x, sy0 = b->srcOffsets[0].y;
        int32_t sx1 = b->srcOffsets[1].x, sy1 = b->srcOffsets[1].y;
        int32_t dx0 = b->dstOffsets[0].x, dy0 = b->dstOffsets[0].y;
        int32_t dx1 = b->dstOffsets[1].x, dy1 = b->dstOffsets[1].y;
        int32_t sw = sx1 - sx0, sh = sy1 - sy0;
        int32_t dw = dx1 - dx0, dh = dy1 - dy0;
        int32_t yy, xx;
        if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) continue;
        for (yy = 0; yy < dh; yy++) {
            int32_t sy = sy0 + (int32_t)((int64_t)yy * sh / dh);
            for (xx = 0; xx < dw; xx++) {
                int32_t sx = sx0 + (int32_t)((int64_t)xx * sw / dw);
                const uint8_t *sp = sb + (size_t)sy * s->row_pitch + (size_t)sx * s->bpp;
                uint8_t *dp = db + (size_t)(dy0 + yy) * d->row_pitch + (size_t)(dx0 + xx) * d->bpp;
                memcpy(dp, sp, d->bpp);
            }
        }
    }
    image_content_touch(d);
}

static void exec_clear_color_image(const vk_cmd *c)
{
    clear_color_image(c->u.clear_color.img, &c->u.clear_color.color);
}

static void exec_clear_depth_image(const vk_cmd *c)
{
    clear_depth_image(c->u.clear_ds.img, &c->u.clear_ds.ds);
}

/* ---- draw / dispatch delegation ------------------------------------------ */
static void exec_draw(vk_command_buffer *cb, const vk_cmd *c, gfx_clear_state *cs)
{
    const vk_device_backend *be = cb->device ? cb->device->backend : NULL;
    vk_draw_ctx dc;
    if (!be || !be->draw) return;
    memset(&dc, 0, sizeof(dc));
    dc.pipeline = c->u.draw.pipeline;
    memcpy(dc.sets, c->u.draw.sets, sizeof(dc.sets));
    dc.first_set = c->u.draw.first_set;
    dc.set_count = c->u.draw.set_count;
    dc.push = c->u.draw.push;
    dc.push_size = c->u.draw.push_size;
    memcpy(dc.color, c->u.draw.color, sizeof(dc.color));
    dc.num_color = c->u.draw.num_color;
    dc.depth = c->u.draw.depth;
    dc.rt_width = c->u.draw.rt_width;
    dc.rt_height = c->u.draw.rt_height;
    dc.vp = c->u.draw.vp; dc.sc = c->u.draw.sc;
    dc.has_vp = c->u.draw.has_vp; dc.has_sc = c->u.draw.has_sc;
    memcpy(dc.vb, c->u.draw.vb, sizeof(dc.vb));
    memcpy(dc.vb_off, c->u.draw.vb_off, sizeof(dc.vb_off));
    dc.ib = c->u.draw.ib; dc.ib_off = c->u.draw.ib_off; dc.ib_type = c->u.draw.ib_type;

    /* GPU clear handoff: flag the pass's first draw and carry the loadOp==CLEAR
     * values the begin command recorded (a no-op for the CPU back-end). */
    dc.gfx_first_draw = cs->first_draw;
    dc.gfx_color_clear = cs->color_clear;
    dc.gfx_clear_color[0] = cs->clear_color[0];
    dc.gfx_clear_color[1] = cs->clear_color[1];
    dc.gfx_clear_color[2] = cs->clear_color[2];
    dc.gfx_clear_color[3] = cs->clear_color[3];
    dc.gfx_depth_clear = cs->depth_clear;
    dc.gfx_clear_depth = cs->clear_depth;
    cs->first_draw = 0;   /* only the pass's first draw may GPU-clear */

    if (c->op == VK_CMD_DRAW_INDEXED && c->u.draw.ib) {
        vk_buffer *ib = c->u.draw.ib;
        const uint8_t *base = (const uint8_t *)vk_buffer_ptr(ib);
        uint32_t cnt = c->u.draw.vertexCount;
        uint32_t isz = (c->u.draw.ib_type == VK_INDEX_TYPE_UINT16) ? 2 : 4;
        uint32_t voff = c->u.draw.firstVertex;
        uint32_t *idx = NULL, k;
        if (!base) return;
        base += c->u.draw.ib_off;
        idx = (uint32_t *)malloc((size_t)cnt * sizeof(uint32_t));
        if (!idx) return;
        for (k = 0; k < cnt; k++) {
            uint32_t v;
            if (isz == 2) v = ((const uint16_t *)base)[k];
            else          v = ((const uint32_t *)base)[k];
            idx[k] = v + voff;
        }
        be->draw(cb, &dc, cnt, 0, c->u.draw.instanceCount, c->u.draw.firstInstance,
                 idx, c->u.draw.ib_type);
        free(idx);
    } else {
        be->draw(cb, &dc, c->u.draw.vertexCount, c->u.draw.firstVertex,
                 c->u.draw.instanceCount, c->u.draw.firstInstance,
                 NULL, c->u.draw.ib_type);
    }

    /* The draw wrote the colour/depth attachments: bump their backing versions
     * so a render-to-texture target that is sampled later re-tiles. */
    {
        uint32_t ci;
        for (ci = 0; ci < dc.num_color; ci++)
            if (dc.color[ci])
                image_content_touch(dc.color[ci]->image);
        if (dc.depth)
            image_content_touch(dc.depth->image);
    }
}

static void exec_dispatch(vk_command_buffer *cb, const vk_cmd *c)
{
    const vk_device_backend *be = cb->device ? cb->device->backend : NULL;
    vk_draw_ctx dc;
    if (!be || !be->dispatch) return;
    memset(&dc, 0, sizeof(dc));
    dc.pipeline = c->u.dispatch.pipeline;
    memcpy(dc.sets, c->u.dispatch.sets, sizeof(dc.sets));
    dc.first_set = c->u.dispatch.first_set;
    dc.set_count = c->u.dispatch.set_count;
    dc.push = c->u.dispatch.push;
    dc.push_size = c->u.dispatch.push_size;
    be->dispatch(cb, &dc, c->u.dispatch.gx, c->u.dispatch.gy, c->u.dispatch.gz);
}

/* ---- query / timestamp --------------------------------------------------- */
static void exec_write_timestamp(const vk_cmd *c)
{
    vk_query_pool *qp = c->u.query_op.pool;
    if (qp && qp->results && c->u.query_op.query < qp->count)
        qp->results[c->u.query_op.query] = vk_now_ns();
}

/* ---- the executor -------------------------------------------------------- */
VkResult vk_execute_command_buffer(vk_command_buffer *cb)
{
    gfx_clear_state cs;
    uint32_t i;
    if (!cb) return VK_ERROR_INITIALIZATION_FAILED;
    memset(&cs, 0, sizeof(cs));
    cs.clear_depth = 1.0f;
    for (i = 0; i < cb->num_cmds; i++) {
        const vk_cmd *c = &cb->cmds[i];
        switch (c->op) {
        case VK_CMD_DRAW:
        case VK_CMD_DRAW_INDEXED:      exec_draw(cb, c, &cs); break;
        case VK_CMD_DISPATCH:          exec_dispatch(cb, c); break;
        case VK_CMD_COPY_BUFFER:       exec_copy_buffer(c); break;
        case VK_CMD_FILL_BUFFER:       exec_fill_buffer(c); break;
        case VK_CMD_UPDATE_BUFFER:     exec_update_buffer(c); break;
        case VK_CMD_COPY_BUFFER_TO_IMAGE: exec_copy_buffer_to_image(c); break;
        case VK_CMD_COPY_IMAGE_TO_BUFFER: exec_copy_image_to_buffer(c); break;
        case VK_CMD_COPY_IMAGE:        exec_copy_image(c); break;
        case VK_CMD_BLIT_IMAGE:        exec_blit_image(c); break;
        case VK_CMD_CLEAR_COLOR_IMAGE: exec_clear_color_image(c); break;
        case VK_CMD_CLEAR_DEPTH_STENCIL_IMAGE: exec_clear_depth_image(c); break;
        case VK_CMD_BEGIN_RENDER_PASS:
            gfx_clear_from_render_pass(c, &cs);
            exec_begin_render_pass(c, gfx_device_is_gpu(cb) &&
                gfx_can_defer_clear(&cs));
            break;
        case VK_CMD_BEGIN_RENDERING:
            gfx_clear_from_rendering(c, &cs);
            exec_begin_rendering(c, gfx_device_is_gpu(cb) &&
                gfx_can_defer_clear(&cs));
            break;
        case VK_CMD_WRITE_TIMESTAMP:   exec_write_timestamp(c); break;
        case VK_CMD_SET_EVENT:
            if (c->u.event_op.event) c->u.event_op.event->set = VK_TRUE;
            break;
        case VK_CMD_RESET_EVENT:
            if (c->u.event_op.event) c->u.event_op.event->set = VK_FALSE;
            break;
        case VK_CMD_RESET_QUERY_POOL:
            if (c->u.query_op.pool && c->u.query_op.pool->results)
                memset(c->u.query_op.pool->results, 0,
                       (size_t)c->u.query_op.pool->count * sizeof(uint64_t));
            break;
        case VK_CMD_EXECUTE_COMMANDS: {
            uint32_t k;
            for (k = 0; k < c->u.exec.n; k++)
                if (c->u.exec.cmds[k]) vk_execute_command_buffer(c->u.exec.cmds[k]);
            break;
        }
        /* bind / state / barrier / subpass commands are captured at record time
         * (into the draw/dispatch snapshots) or are no-ops in immediate mode. */
        default: break;
        }
    }
    return VK_SUCCESS;
}
