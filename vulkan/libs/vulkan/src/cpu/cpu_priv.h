/*
 * cpu_priv.h - shared internals of the software (CPU) device back-end.  The
 * back-end executes recorded draw/dispatch commands by driving the SPIR-V
 * interpreter (src/spirv): compute runs one invocation per work-item, graphics
 * runs the vertex shader per vertex, rasterizes on the CPU and runs the
 * fragment shader per fragment.  Everything is host-resident and single
 * threaded; there is no portablegl / mesa dependency, mirroring how OpenCL's
 * CPU back-end is self-contained.
 */
#ifndef VULKAN_CPU_PRIV_H
#define VULKAN_CPU_PRIV_H

#include "internal.h"
#include "spirv.h"

#define CPU_MAX_VARYING   SPIRV_MAX_LOCATIONS
#define CPU_MAX_VTX_OUT   4096

/* Resolve the descriptor sets / push constants bound in a draw context into the
 * interpreter's resource table.  Returns 0 on success, -1 if a referenced
 * binding cannot be resolved (the caller declines to the fallback). */
int cpu_fill_bindings(const vk_draw_ctx *dc, spirv_resources *res);

/* Graphics draw entry (cpu_raster.c): runs the pipeline's VS/FS through the
 * software rasterizer into the bound color/depth attachments. */
VkResult cpu_draw(vk_command_buffer *cb, vk_draw_ctx *dc,
                  uint32_t vertexCount, uint32_t firstVertex,
                  uint32_t instanceCount, uint32_t firstInstance,
                  const uint32_t *index_data, VkIndexType index_type);

/* Compute dispatch entry (cpu_backend.c). */
VkResult cpu_dispatch(vk_command_buffer *cb, vk_draw_ctx *dc,
                      uint32_t gx, uint32_t gy, uint32_t gz);

/* Locate the pipeline's GLCompute entry point and its module (cpu_backend.c). */
const spirv_entry *cpu_find_compute(vk_pipeline *p, const spirv_module **mod);

#endif /* VULKAN_CPU_PRIV_H */
