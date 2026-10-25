/*
 * cpu_backend.c - the software (CPU) device back-end: descriptor/push-constant
 * resolution into the interpreter's resource table, compute dispatch (one
 * spirv_run per work-item across the whole grid), and the vk_device_backend
 * vtable.  Graphics draw lives in cpu_raster.c; this file owns everything the
 * two share plus the always-present CPU back-end instance.
 */
#include "cpu_priv.h"
#include <stdlib.h>
#include <string.h>

/* ---- descriptor -> interpreter binding resolution ------------------------ */

/* Fill one spirv_binding for a descriptor binding write.  Returns 0 on success,
 * -1 when the binding references a resource we cannot resolve to a host pointer
 * (which declines the whole draw/dispatch to a no-op rather than mis-executing). */
static int resolve_binding(const vk_descriptor_binding *b, uint32_t set,
                           spirv_binding *out)
{
    memset(out, 0, sizeof(*out));
    out->set = set;
    out->binding = b->binding;
    switch (b->type) {
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
        void *base;
        if (!b->buffer)
            return -1;
        base = vk_buffer_ptr(b->buffer);
        if (!base)
            return -1;
        out->kind = (b->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                        ? SPIRV_RES_STORAGE_BUFFER : SPIRV_RES_UNIFORM_BUFFER;
        out->data = (char *)base + b->offset;
        out->size = (size_t)(b->range == VK_WHOLE_SIZE
                                 ? (b->buffer->size - b->offset) : b->range);
        out->writable = (b->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        return 0;
    }
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: {
        vk_image *im;
        void *base;
        if (!b->view || !b->view->image)
            return -1;
        im = b->view->image;
        base = vk_image_ptr(im);
        if (!base)
            return -1;
        if (b->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            out->kind = SPIRV_RES_COMBINED_IMAGE_SAMPLER;
        else if (b->type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
            out->kind = SPIRV_RES_SAMPLED_IMAGE;
        else
            out->kind = SPIRV_RES_STORAGE_IMAGE;
        out->data = base;
        out->width = im->extent.width;
        out->height = im->extent.height;
        out->depth = im->extent.depth;
        out->format = (uint32_t)im->format;
        out->bpp = im->bpp ? im->bpp : vk_format_bpp(im->format);
        out->size = (size_t)out->width * out->height * out->depth * out->bpp;
        out->writable = (b->type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        return 0;
    }
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        out->kind = SPIRV_RES_SAMPLER;
        out->data = NULL;
        return 0;
    default:
        /* uniform/sampler texel buffers and input attachments are unsupported
         * on the software device: decline rather than mis-execute. */
        return -1;
    }
}

int cpu_fill_bindings(const vk_draw_ctx *dc, spirv_resources *res)
{
    uint32_t s, i;
    res->num_bindings = 0;
    res->push = dc->push;
    res->push_size = dc->push_size;
    for (s = 0; s < dc->set_count; s++) {
        const vk_descriptor_set *ds = dc->sets[s];
        uint32_t set_index = dc->first_set + s;
        if (!ds)
            continue;
        for (i = 0; i < ds->num_bindings; i++) {
            spirv_binding sb;
            if (res->num_bindings >= SPIRV_MAX_BINDINGS)
                return -1;
            if (resolve_binding(&ds->bindings[i], set_index, &sb) != 0)
                return -1;
            res->bindings[res->num_bindings++] = sb;
        }
    }
    return 0;
}

/* ---- shader stage lookup ------------------------------------------------- */

/* Find the spirv entry point for a pipeline stage.  Prefers an exact name
 * match, else the first entry with a compatible execution model, else entry 0. */
static const spirv_entry *find_stage_entry(const vk_pipeline_stage *st,
                                           uint32_t exec_model,
                                           const spirv_module **out_mod)
{
    const spirv_module *m;
    const spirv_entry *e;
    if (!st || !st->module || !st->module->module)
        return NULL;
    m = st->module->module;
    *out_mod = m;
    if (st->entry[0]) {
        e = spirv_find_entry(m, st->entry, exec_model);
        if (e)
            return e;
    }
    e = spirv_find_entry(m, NULL, exec_model);
    if (e)
        return e;
    if (spirv_num_entries(m) > 0)
        return spirv_get_entry(m, 0);
    return NULL;
}

const spirv_entry *cpu_find_compute(vk_pipeline *p, const spirv_module **mod)
{
    uint32_t i;
    for (i = 0; i < p->num_stages; i++) {
        if (p->stages[i].stage == VK_SHADER_STAGE_COMPUTE_BIT) {
            const spirv_entry *e =
                find_stage_entry(&p->stages[i], SPIRV_EXEC_GLCOMPUTE, mod);
            if (e)
                return e;
        }
    }
    return NULL;
}

/* ---- compute dispatch ---------------------------------------------------- */

VkResult cpu_dispatch(vk_command_buffer *cb, vk_draw_ctx *dc,
                      uint32_t gx, uint32_t gy, uint32_t gz)
{
    vk_pipeline *p = dc->pipeline;
    const spirv_module *mod = NULL;
    const spirv_entry *e;
    spirv_resources res;
    uint32_t lsx, lsy, lsz;
    uint32_t wx, wy, wz, lx, ly, lz;

    (void)cb;
    if (!p)
        return VK_ERROR_INITIALIZATION_FAILED;
    e = cpu_find_compute(p, &mod);
    if (!e || !mod)
        return VK_ERROR_INITIALIZATION_FAILED;   /* decline: no compute entry  */

    if (cpu_fill_bindings(dc, &res) != 0)
        return VK_SUCCESS;   /* unresolvable bindings: benign no-op dispatch   */

    lsx = e->local_size[0] ? e->local_size[0] : 1;
    lsy = e->local_size[1] ? e->local_size[1] : 1;
    lsz = e->local_size[2] ? e->local_size[2] : 1;

    res.wg_size[0] = lsx; res.wg_size[1] = lsy; res.wg_size[2] = lsz;
    res.num_wg[0] = gx;   res.num_wg[1] = gy;   res.num_wg[2] = gz;

    for (wz = 0; wz < gz; wz++) {
        for (wy = 0; wy < gy; wy++) {
            for (wx = 0; wx < gx; wx++) {
                res.workgroup_id[0] = wx;
                res.workgroup_id[1] = wy;
                res.workgroup_id[2] = wz;
                for (lz = 0; lz < lsz; lz++) {
                    for (ly = 0; ly < lsy; ly++) {
                        for (lx = 0; lx < lsx; lx++) {
                            res.local_id[0] = lx;
                            res.local_id[1] = ly;
                            res.local_id[2] = lz;
                            res.local_index = lx + lsx * (ly + lsy * lz);
                            res.global_id[0] = wx * lsx + lx;
                            res.global_id[1] = wy * lsy + ly;
                            res.global_id[2] = wz * lsz + lz;
                            /* run one invocation; a decline leaves results as
                             * the interpreter wrote them (never mis-executed
                             * beyond the point of the unsupported construct). */
                            if (spirv_run(mod, e, &res) != 0)
                                return VK_SUCCESS;
                        }
                    }
                }
            }
        }
    }
    return VK_SUCCESS;
}

/* ---- back-end vtable ----------------------------------------------------- */

static void cpu_pipeline_free(vk_pipeline *p)
{
    if (p)
        p->backend_data = NULL;   /* CPU back-end keeps no private state */
}

const vk_device_backend vk_backend_cpu = {
    "cpu",
    cpu_dispatch,
    cpu_draw,
    cpu_pipeline_free
};
