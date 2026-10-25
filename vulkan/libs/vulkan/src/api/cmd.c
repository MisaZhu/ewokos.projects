/*
 * cmd.c - command pool / command buffer lifecycle, begin/end, and the recording
 * of bind, dynamic-state, draw and dispatch commands.  Recording captures bound
 * state into each draw/dispatch (vk_snapshot_state); the executor later replays
 * them.  Viewport and scissor are the dynamic states the software rasterizer
 * consumes, so they update the command buffer's bound state immediately; the
 * remaining vkCmdSet* entry points are accepted (the pipeline's baked-in state
 * is used), which keeps the full 1.3 surface linkable and well-behaved.
 *
 * Indirect draw/dispatch read their parameter buffer at record time (the common
 * host-filled case); a device-generated indirect buffer that is not yet resident
 * simply yields the parameters as they stand.
 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* ---- command pool -------------------------------------------------------- */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(
    VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool)
{
    vk_device *dev = (vk_device *)VK_H2P(device);
    vk_command_pool *p;
    (void)pAllocator;
    if (!dev || !pCreateInfo || !pCommandPool) return VK_ERROR_INITIALIZATION_FAILED;
    p = (vk_command_pool *)vk_obj_alloc(sizeof(vk_command_pool), VK_OBJ_COMMAND_POOL);
    if (!p) return VK_ERROR_OUT_OF_HOST_MEMORY;
    p->device = dev;
    p->family = pCreateInfo->queueFamilyIndex;
    *pCommandPool = VK_P2H(VkCommandPool, p);
    return VK_SUCCESS;
}

static void free_recorded_cmds(vk_command_buffer *cb)
{
    uint32_t i;
    for (i = 0; i < cb->num_cmds; i++)
        if (cb->cmds[i].op == VK_CMD_EXECUTE_COMMANDS)
            free(cb->cmds[i].u.exec.cmds);
    cb->num_cmds = 0;
}

static void reset_cb(vk_command_buffer *cb)
{
    free_recorded_cmds(cb);
    cb->recording = 0;
    cb->in_render_pass = 0;
    cb->in_rendering = 0;
    cb->pipeline_gfx = NULL;
    cb->pipeline_compute = NULL;
    memset(cb->sets, 0, sizeof(cb->sets));
    cb->first_set = 0; cb->set_count = 0;
    cb->push_size = 0;
    memset(cb->vb, 0, sizeof(cb->vb));
    memset(cb->vb_off, 0, sizeof(cb->vb_off));
    cb->ib = NULL; cb->ib_off = 0; cb->ib_type = VK_INDEX_TYPE_UINT16;
    cb->has_vp = 0; cb->has_sc = 0;
    memset(cb->color, 0, sizeof(cb->color));
    cb->num_color = 0; cb->depth = NULL;
    cb->rt_width = 0; cb->rt_height = 0;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(
    VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *pAllocator)
{
    vk_command_pool *p = (vk_command_pool *)VK_H2P(commandPool);
    uint32_t i;
    (void)device; (void)pAllocator;
    if (!p) return;
    for (i = 0; i < p->num_buffers; i++) {
        free_recorded_cmds(p->buffers[i]);
        free(p->buffers[i]->cmds);
        vk_obj_free(p->buffers[i]);
    }
    vk_obj_free(p);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandPool(
    VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags)
{
    vk_command_pool *p = (vk_command_pool *)VK_H2P(commandPool);
    uint32_t i;
    (void)device; (void)flags;
    if (!p) return VK_ERROR_INITIALIZATION_FAILED;
    for (i = 0; i < p->num_buffers; i++)
        reset_cb(p->buffers[i]);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkTrimCommandPool(
    VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlags flags)
{
    (void)device; (void)commandPool; (void)flags;
}

/* ---- command buffers ----------------------------------------------------- */
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(
    VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo,
    VkCommandBuffer *pCommandBuffers)
{
    vk_device *dev = (vk_device *)VK_H2P(device);
    vk_command_pool *pool = pAllocateInfo
        ? (vk_command_pool *)VK_H2P(pAllocateInfo->commandPool) : NULL;
    uint32_t i;
    if (!dev || !pool || !pAllocateInfo || !pCommandBuffers)
        return VK_ERROR_INITIALIZATION_FAILED;
    for (i = 0; i < pAllocateInfo->commandBufferCount; i++) {
        vk_command_buffer *cb;
        if (pool->num_buffers >= 256) return VK_ERROR_OUT_OF_HOST_MEMORY;
        cb = (vk_command_buffer *)
            vk_obj_alloc_dispatch(sizeof(vk_command_buffer), VK_OBJ_CMDBUF);
        if (!cb) return VK_ERROR_OUT_OF_HOST_MEMORY;
        cb->pool = pool;
        cb->device = dev;
        cb->level = pAllocateInfo->level;
        reset_cb(cb);
        pool->buffers[pool->num_buffers++] = cb;
        pCommandBuffers[i] = VK_P2H(VkCommandBuffer, cb);
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(
    VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount,
    const VkCommandBuffer *pCommandBuffers)
{
    vk_command_pool *pool = (vk_command_pool *)VK_H2P(commandPool);
    uint32_t i, j;
    (void)device;
    if (!pool || !pCommandBuffers) return;
    for (i = 0; i < commandBufferCount; i++) {
        vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(pCommandBuffers[i]);
        if (!cb) continue;
        for (j = 0; j < pool->num_buffers; j++)
            if (pool->buffers[j] == cb) {
                pool->buffers[j] = pool->buffers[--pool->num_buffers];
                break;
            }
        free_recorded_cmds(cb);
        free(cb->cmds);
        vk_obj_free(cb);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(
    VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    (void)flags;
    if (!cb) return VK_ERROR_INITIALIZATION_FAILED;
    reset_cb(cb);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(
    VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    if (!cb || !pBeginInfo) return VK_ERROR_INITIALIZATION_FAILED;
    if (cb->recording) return VK_ERROR_VALIDATION_FAILED_EXT;
    cb->usage = pBeginInfo->flags;
    free_recorded_cmds(cb);   /* fresh recording */
    cb->recording = 1;
    cb->in_render_pass = 0;
    cb->in_rendering = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer commandBuffer)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    if (!cb) return VK_ERROR_INITIALIZATION_FAILED;
    if (!cb->recording) return VK_ERROR_VALIDATION_FAILED_EXT;
    if (cb->in_render_pass || cb->in_rendering) return VK_ERROR_VALIDATION_FAILED_EXT;
    cb->recording = 0;
    return VK_SUCCESS;
}

/* ---- bind commands ------------------------------------------------------- */
VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(
    VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    if (!cb) return;
    if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS)
        cb->pipeline_gfx = (vk_pipeline *)VK_H2P(pipeline);
    else
        cb->pipeline_compute = (vk_pipeline *)VK_H2P(pipeline);
    c = vk_cmd_push(cb, VK_CMD_BIND_PIPELINE);
    if (c) c->u.bind_pipeline.pipeline = (vk_pipeline *)VK_H2P(pipeline);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(
    VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
    VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount,
    const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount,
    const uint32_t *pDynamicOffsets)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    uint32_t i;
    (void)pipelineBindPoint; (void)dynamicOffsetCount; (void)pDynamicOffsets;
    if (!cb || !pDescriptorSets) return;
    cb->first_set = firstSet;
    cb->set_count = descriptorSetCount;
    for (i = 0; i < descriptorSetCount && i < VK_MAX_DESCRIPTOR_SETS; i++)
        cb->sets[i] = (vk_descriptor_set *)VK_H2P(pDescriptorSets[i]);
    c = vk_cmd_push(cb, VK_CMD_BIND_DESCRIPTOR_SETS);
    if (c) {
        c->u.bind_desc.first_set = firstSet;
        c->u.bind_desc.set_count = descriptorSetCount;
        c->u.bind_desc.layout = (vk_pipeline_layout *)VK_H2P(layout);
        for (i = 0; i < descriptorSetCount && i < VK_MAX_DESCRIPTOR_SETS; i++)
            c->u.bind_desc.sets[i] = cb->sets[i];
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(
    VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount,
    const VkBuffer *pBuffers, const VkDeviceSize *pOffsets)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    uint32_t i;
    if (!cb || !pBuffers) return;
    for (i = 0; i < bindingCount && (firstBinding + i) < VK_MAX_VERTEX_BINDINGS; i++) {
        cb->vb[firstBinding + i] = (vk_buffer *)VK_H2P(pBuffers[i]);
        cb->vb_off[firstBinding + i] = pOffsets ? pOffsets[i] : 0;
    }
    vk_cmd_push(cb, VK_CMD_BIND_VERTEX_BUFFERS);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers2(
    VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount,
    const VkBuffer *pBuffers, const VkDeviceSize *pOffsets, const VkDeviceSize *pSizes,
    const VkDeviceSize *pStrides)
{
    (void)pSizes; (void)pStrides;
    vkCmdBindVertexBuffers(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(
    VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    if (!cb) return;
    cb->ib = (vk_buffer *)VK_H2P(buffer);
    cb->ib_off = offset;
    cb->ib_type = indexType;
    c = vk_cmd_push(cb, VK_CMD_BIND_INDEX_BUFFER);
    if (c) {
        c->u.bind_ib.buffer = cb->ib;
        c->u.bind_ib.offset = offset;
        c->u.bind_ib.type = indexType;
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(
    VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags,
    uint32_t offset, uint32_t size, const void *pValues)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    (void)layout; (void)stageFlags;
    if (!cb || !pValues) return;
    if (offset + size > VK_MAX_PUSH_CONSTANT_RANGE)
        size = VK_MAX_PUSH_CONSTANT_RANGE - offset;
    memcpy(cb->push + offset, pValues, size);
    if (offset + size > cb->push_size) cb->push_size = offset + size;
    c = vk_cmd_push(cb, VK_CMD_PUSH_CONSTANTS);
    if (c) {
        c->u.push.offset = offset;
        c->u.push.size = size;
        memcpy(c->u.push.data, pValues, size);
    }
}

/* ---- dynamic state ------------------------------------------------------- */
VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(
    VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount,
    const VkViewport *pViewports)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    (void)firstViewport;
    if (!cb || !pViewports || viewportCount == 0) return;
    cb->vp = pViewports[0];
    cb->has_vp = 1;
    c = vk_cmd_push(cb, VK_CMD_SET_VIEWPORT);
    if (c) c->u.viewport.vp = pViewports[0];
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(
    VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount,
    const VkRect2D *pScissors)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    (void)firstScissor;
    if (!cb || !pScissors || scissorCount == 0) return;
    cb->sc = pScissors[0];
    cb->has_sc = 1;
    c = vk_cmd_push(cb, VK_CMD_SET_SCISSOR);
    if (c) c->u.scissor.sc = pScissors[0];
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportWithCount(
    VkCommandBuffer commandBuffer, uint32_t viewportCount, const VkViewport *pViewports)
{
    vkCmdSetViewport(commandBuffer, 0, viewportCount, pViewports);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissorWithCount(
    VkCommandBuffer commandBuffer, uint32_t scissorCount, const VkRect2D *pScissors)
{
    vkCmdSetScissor(commandBuffer, 0, scissorCount, pScissors);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetBlendConstants(
    VkCommandBuffer commandBuffer, const float blendConstants[4])
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    if (!cb || !blendConstants) return;
    c = vk_cmd_push(cb, VK_CMD_SET_BLEND_CONSTANTS);
    if (c) memcpy(c->u.blend_const.c, blendConstants, sizeof(float) * 4);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineWidth(
    VkCommandBuffer commandBuffer, float lineWidth)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    if (!cb) return;
    c = vk_cmd_push(cb, VK_CMD_SET_LINE_WIDTH);
    if (c) c->u.line_width.w = lineWidth;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBias(
    VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp,
    float depthBiasSlopeFactor)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    if (!cb) return;
    c = vk_cmd_push(cb, VK_CMD_SET_DEPTH_BIAS);
    if (c) {
        c->u.depth_bias.cf = depthBiasConstantFactor;
        c->u.depth_bias.sb = depthBiasSlopeFactor;
        c->u.depth_bias.cl = depthBiasClamp;
    }
}

/* The remaining dynamic-state setters are accepted; the software device uses the
 * pipeline's baked-in state for these (they are only mutable when the pipeline
 * declared them dynamic, which the smoke paths do not rely on). */
#define VK_NOOP_CMD1(fn, T1, a1) \
    VKAPI_ATTR void VKAPI_CALL fn(VkCommandBuffer commandBuffer, T1 a1) { (void)commandBuffer; (void)a1; }
#define VK_NOOP_CMD2(fn, T1, a1, T2, a2) \
    VKAPI_ATTR void VKAPI_CALL fn(VkCommandBuffer commandBuffer, T1 a1, T2 a2) { (void)commandBuffer; (void)a1; (void)a2; }
#define VK_NOOP_CMD3(fn, T1, a1, T2, a2, T3, a3) \
    VKAPI_ATTR void VKAPI_CALL fn(VkCommandBuffer commandBuffer, T1 a1, T2 a2, T3 a3) { (void)commandBuffer; (void)a1; (void)a2; (void)a3; }

VK_NOOP_CMD1(vkCmdSetCullMode, VkCullModeFlags, cullMode)
VK_NOOP_CMD1(vkCmdSetFrontFace, VkFrontFace, frontFace)
VK_NOOP_CMD1(vkCmdSetPrimitiveTopology, VkPrimitiveTopology, primitiveTopology)
VK_NOOP_CMD1(vkCmdSetDepthTestEnable, VkBool32, depthTestEnable)
VK_NOOP_CMD1(vkCmdSetDepthWriteEnable, VkBool32, depthWriteEnable)
VK_NOOP_CMD1(vkCmdSetDepthCompareOp, VkCompareOp, depthCompareOp)
VK_NOOP_CMD1(vkCmdSetDepthBoundsTestEnable, VkBool32, depthBoundsTestEnable)
VK_NOOP_CMD1(vkCmdSetStencilTestEnable, VkBool32, stencilTestEnable)
VK_NOOP_CMD1(vkCmdSetRasterizerDiscardEnable, VkBool32, rasterizerDiscardEnable)
VK_NOOP_CMD1(vkCmdSetDepthBiasEnable, VkBool32, depthBiasEnable)
VK_NOOP_CMD1(vkCmdSetPrimitiveRestartEnable, VkBool32, primitiveRestartEnable)
VK_NOOP_CMD1(vkCmdSetDeviceMask, uint32_t, deviceMask)
VK_NOOP_CMD2(vkCmdSetDepthBounds, float, minDepthBounds, float, maxDepthBounds)
VK_NOOP_CMD2(vkCmdSetStencilCompareMask, VkStencilFaceFlags, faceMask, uint32_t, compareMask)
VK_NOOP_CMD2(vkCmdSetStencilWriteMask, VkStencilFaceFlags, faceMask, uint32_t, writeMask)
VK_NOOP_CMD2(vkCmdSetStencilReference, VkStencilFaceFlags, faceMask, uint32_t, reference)

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilOp(
    VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, VkStencilOp failOp,
    VkStencilOp passOp, VkStencilOp depthFailOp, VkCompareOp compareOp)
{
    (void)commandBuffer; (void)faceMask; (void)failOp; (void)passOp;
    (void)depthFailOp; (void)compareOp;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent2(
    VkCommandBuffer commandBuffer, VkEvent event, const VkDependencyInfo *pDependencyInfo)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    (void)pDependencyInfo;
    if (!cb) return;
    c = vk_cmd_push(cb, VK_CMD_SET_EVENT);
    if (c) c->u.event_op.event = (vk_event *)VK_H2P(event);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent2(
    VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2 stageMask)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    (void)stageMask;
    if (!cb) return;
    c = vk_cmd_push(cb, VK_CMD_RESET_EVENT);
    if (c) c->u.event_op.event = (vk_event *)VK_H2P(event);
}

/* ---- draw / dispatch ----------------------------------------------------- */
VKAPI_ATTR void VKAPI_CALL vkCmdDraw(
    VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount,
    uint32_t firstVertex, uint32_t firstInstance)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    if (!cb) return;
    c = vk_cmd_push(cb, VK_CMD_DRAW);
    if (!c) return;
    c->u.draw.pipeline = cb->pipeline_gfx;
    c->u.draw.vertexCount = vertexCount;
    c->u.draw.firstVertex = firstVertex;
    c->u.draw.instanceCount = instanceCount ? instanceCount : 1;
    c->u.draw.firstInstance = firstInstance;
    vk_snapshot_state(cb, c);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(
    VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount,
    uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    uint32_t isz;
    if (!cb) return;
    c = vk_cmd_push(cb, VK_CMD_DRAW_INDEXED);
    if (!c) return;
    isz = (cb->ib_type == VK_INDEX_TYPE_UINT16) ? 2 : 4;
    c->u.draw.pipeline = cb->pipeline_gfx;
    c->u.draw.vertexCount = indexCount;
    c->u.draw.firstVertex = (uint32_t)vertexOffset;
    c->u.draw.instanceCount = instanceCount ? instanceCount : 1;
    c->u.draw.firstInstance = firstInstance;
    vk_snapshot_state(cb, c);
    c->u.draw.ib_off = cb->ib_off + (VkDeviceSize)firstIndex * isz;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(
    VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY,
    uint32_t groupCountZ)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    if (!cb) return;
    c = vk_cmd_push(cb, VK_CMD_DISPATCH);
    if (!c) return;
    c->u.dispatch.pipeline = cb->pipeline_compute;
    c->u.dispatch.gx = groupCountX;
    c->u.dispatch.gy = groupCountY;
    c->u.dispatch.gz = groupCountZ;
    memcpy(c->u.dispatch.sets, cb->sets, sizeof(cb->sets));
    c->u.dispatch.first_set = cb->first_set;
    c->u.dispatch.set_count = cb->set_count;
    memcpy(c->u.dispatch.push, cb->push, sizeof(cb->push));
    c->u.dispatch.push_size = cb->push_size;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchBase(
    VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY,
    uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    (void)baseGroupX; (void)baseGroupY; (void)baseGroupZ;
    vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchIndirect(
    VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset)
{
    vk_buffer *b = (vk_buffer *)VK_H2P(buffer);
    const VkDispatchIndirectCommand *ic;
    if (!b || !b->mem || !b->mem->host) return;
    ic = (const VkDispatchIndirectCommand *)((char *)b->mem->host + b->offset + offset);
    vkCmdDispatch(commandBuffer, ic->x, ic->y, ic->z);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirect(
    VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
    uint32_t drawCount, uint32_t stride)
{
    vk_buffer *b = (vk_buffer *)VK_H2P(buffer);
    const char *base;
    uint32_t i;
    if (!b || !b->mem || !b->mem->host) return;
    base = (const char *)b->mem->host + b->offset + offset;
    for (i = 0; i < drawCount; i++) {
        const VkDrawIndirectCommand *d = (const VkDrawIndirectCommand *)(base + (size_t)i * stride);
        vkCmdDraw(commandBuffer, d->vertexCount, d->instanceCount, d->firstVertex, d->firstInstance);
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirect(
    VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
    uint32_t drawCount, uint32_t stride)
{
    vk_buffer *b = (vk_buffer *)VK_H2P(buffer);
    const char *base;
    uint32_t i;
    if (!b || !b->mem || !b->mem->host) return;
    base = (const char *)b->mem->host + b->offset + offset;
    for (i = 0; i < drawCount; i++) {
        const VkDrawIndexedIndirectCommand *d =
            (const VkDrawIndexedIndirectCommand *)(base + (size_t)i * stride);
        vkCmdDrawIndexed(commandBuffer, d->indexCount, d->instanceCount,
                         d->firstIndex, d->vertexOffset, d->firstInstance);
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectCount(
    VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
    VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
    uint32_t stride)
{
    vk_buffer *cb2 = (vk_buffer *)VK_H2P(countBuffer);
    uint32_t count = maxDrawCount;
    if (cb2 && cb2->mem && cb2->mem->host) {
        uint32_t c = *(const uint32_t *)((char *)cb2->mem->host + cb2->offset + countBufferOffset);
        if (c < count) count = c;
    }
    vkCmdDrawIndirect(commandBuffer, buffer, offset, count, stride);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirectCount(
    VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
    VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
    uint32_t stride)
{
    vk_buffer *cb2 = (vk_buffer *)VK_H2P(countBuffer);
    uint32_t count = maxDrawCount;
    if (cb2 && cb2->mem && cb2->mem->host) {
        uint32_t c = *(const uint32_t *)((char *)cb2->mem->host + cb2->offset + countBufferOffset);
        if (c < count) count = c;
    }
    vkCmdDrawIndexedIndirect(commandBuffer, buffer, offset, count, stride);
}

/* ---- render pass / dynamic rendering begin-end --------------------------- */
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(
    VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin,
    VkSubpassContents contents)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    uint32_t i;
    (void)contents;
    if (!cb || !pRenderPassBegin) return;
    c = vk_cmd_push(cb, VK_CMD_BEGIN_RENDER_PASS);
    if (!c) return;
    c->u.begin_rp.rp = (vk_render_pass *)VK_H2P(pRenderPassBegin->renderPass);
    c->u.begin_rp.fb = (vk_framebuffer *)VK_H2P(pRenderPassBegin->framebuffer);
    c->u.begin_rp.area = pRenderPassBegin->renderArea;
    c->u.begin_rp.num_clears = pRenderPassBegin->clearValueCount;
    if (c->u.begin_rp.num_clears > VK_MAX_RENDER_PASS_ATTACH)
        c->u.begin_rp.num_clears = VK_MAX_RENDER_PASS_ATTACH;
    for (i = 0; i < c->u.begin_rp.num_clears; i++)
        c->u.begin_rp.clears[i] = pRenderPassBegin->pClearValues[i];
    /* publish the framebuffer's attachments as the active render targets */
    if (c->u.begin_rp.fb) {
        vk_framebuffer *fb = c->u.begin_rp.fb;
        vk_render_pass *rp = c->u.begin_rp.rp;
        cb->num_color = 0;
        cb->depth = NULL;
        for (i = 0; i < fb->num_views; i++) {
            int is_depth = rp && i < rp->num_attachments &&
                (rp->attachments[i].format == VK_FORMAT_D16_UNORM ||
                 rp->attachments[i].format == VK_FORMAT_D24_UNORM_S8_UINT ||
                 rp->attachments[i].format == VK_FORMAT_D32_SFLOAT ||
                 rp->attachments[i].format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                 rp->attachments[i].format == VK_FORMAT_X8_D24_UNORM_PACK32);
            if (is_depth) cb->depth = fb->views[i];
            else if (cb->num_color < VK_MAX_COLOR_ATTACHMENTS)
                cb->color[cb->num_color++] = fb->views[i];
        }
        cb->rt_width = fb->width;
        cb->rt_height = fb->height;
    }
    cb->in_render_pass = 1;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass2(
    VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin,
    const VkSubpassBeginInfo *pSubpassBeginInfo)
{
    (void)pSubpassBeginInfo;
    vkCmdBeginRenderPass(commandBuffer, pRenderPassBegin, VK_SUBPASS_CONTENTS_INLINE);
}

VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass(
    VkCommandBuffer commandBuffer, VkSubpassContents contents)
{
    (void)contents;
    vk_cmd_push((vk_command_buffer *)VK_H2P(commandBuffer), VK_CMD_NEXT_SUBPASS);
}

VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass2(
    VkCommandBuffer commandBuffer, const VkSubpassBeginInfo *pSubpassBeginInfo,
    const VkSubpassEndInfo *pSubpassEndInfo)
{
    (void)pSubpassBeginInfo; (void)pSubpassEndInfo;
    vkCmdNextSubpass(commandBuffer, VK_SUBPASS_CONTENTS_INLINE);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer commandBuffer)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    if (!cb) return;
    vk_cmd_push(cb, VK_CMD_END_RENDER_PASS);
    cb->in_render_pass = 0;
    cb->num_color = 0; cb->depth = NULL;
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass2(
    VkCommandBuffer commandBuffer, const VkSubpassEndInfo *pSubpassEndInfo)
{
    (void)pSubpassEndInfo;
    vkCmdEndRenderPass(commandBuffer);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRendering(
    VkCommandBuffer commandBuffer, const VkRenderingInfo *pRenderingInfo)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    uint32_t i;
    if (!cb || !pRenderingInfo) return;
    c = vk_cmd_push(cb, VK_CMD_BEGIN_RENDERING);
    if (!c) return;
    c->u.begin_rendering.width = pRenderingInfo->renderArea.extent.width;
    c->u.begin_rendering.height = pRenderingInfo->renderArea.extent.height;
    c->u.begin_rendering.num_color = pRenderingInfo->colorAttachmentCount;
    if (c->u.begin_rendering.num_color > VK_MAX_COLOR_ATTACHMENTS)
        c->u.begin_rendering.num_color = VK_MAX_COLOR_ATTACHMENTS;
    for (i = 0; i < c->u.begin_rendering.num_color; i++) {
        const VkRenderingAttachmentInfo *ai = &pRenderingInfo->pColorAttachments[i];
        c->u.begin_rendering.color[i] = (vk_image_view *)VK_H2P(ai->imageView);
        c->u.begin_rendering.color_clear[i] = ai->clearValue;
        c->u.begin_rendering.color_layout[i] =
            (ai->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) ? VK_LAYOUT_CLEAR_EWOK : ai->imageLayout;
    }
    if (pRenderingInfo->pDepthAttachment) {
        const VkRenderingAttachmentInfo *ai = pRenderingInfo->pDepthAttachment;
        c->u.begin_rendering.depth = (vk_image_view *)VK_H2P(ai->imageView);
        c->u.begin_rendering.depth_clear = ai->clearValue;
        c->u.begin_rendering.depth_layout =
            (ai->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) ? VK_LAYOUT_CLEAR_EWOK : ai->imageLayout;
    }
    /* publish active targets */
    cb->num_color = c->u.begin_rendering.num_color;
    for (i = 0; i < cb->num_color; i++)
        cb->color[i] = c->u.begin_rendering.color[i];
    cb->depth = c->u.begin_rendering.depth;
    cb->rt_width = c->u.begin_rendering.width;
    cb->rt_height = c->u.begin_rendering.height;
    cb->in_rendering = 1;
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering(VkCommandBuffer commandBuffer)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    if (!cb) return;
    vk_cmd_push(cb, VK_CMD_END_RENDERING);
    cb->in_rendering = 0;
    cb->num_color = 0; cb->depth = NULL;
}

VKAPI_ATTR void VKAPI_CALL vkCmdExecuteCommands(
    VkCommandBuffer commandBuffer, uint32_t commandBufferCount,
    const VkCommandBuffer *pCommandBuffers)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    uint32_t i;
    if (!cb || !pCommandBuffers) return;
    c = vk_cmd_push(cb, VK_CMD_EXECUTE_COMMANDS);
    if (!c) return;
    c->u.exec.n = commandBufferCount;
    c->u.exec.cmds = (vk_command_buffer **)malloc(sizeof(vk_command_buffer *) * commandBufferCount);
    if (!c->u.exec.cmds) { c->u.exec.n = 0; return; }
    for (i = 0; i < commandBufferCount; i++)
        c->u.exec.cmds[i] = (vk_command_buffer *)VK_H2P(pCommandBuffers[i]);
}
