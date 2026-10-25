/*
 * cmd_transfer.c - the transfer, clear, resolve, barrier, event, query and
 * timestamp recording commands.  Copy/blit/clear/fill/update are recorded with
 * self-contained payloads and executed by cmdexec.c against the host-resident
 * memory; barriers are ordering hints that are no-ops in the single-threaded
 * immediate model.  The *2 (Vulkan 1.3 synchronization2 / copy2) forms unwrap to
 * the same recording.
 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* ---- copy buffer --------------------------------------------------------- */
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(
    VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer,
    uint32_t regionCount, const VkBufferCopy *pRegions)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    if (!cb || !pRegions) return;
    c = vk_cmd_push(cb, VK_CMD_COPY_BUFFER);
    if (!c) return;
    c->u.copy_buf.src = (vk_buffer *)VK_H2P(srcBuffer);
    c->u.copy_buf.dst = (vk_buffer *)VK_H2P(dstBuffer);
    c->u.copy_buf.n = regionCount > 16 ? 16 : regionCount;
    memcpy(c->u.copy_buf.regions, pRegions, c->u.copy_buf.n * sizeof(VkBufferCopy));
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer2(
    VkCommandBuffer commandBuffer, const VkCopyBufferInfo2 *pCopyBufferInfo)
{
    VkBufferCopy regions[16];
    uint32_t i, n;
    if (!pCopyBufferInfo) return;
    n = pCopyBufferInfo->regionCount > 16 ? 16 : pCopyBufferInfo->regionCount;
    for (i = 0; i < n; i++) {
        regions[i].srcOffset = pCopyBufferInfo->pRegions[i].srcOffset;
        regions[i].dstOffset = pCopyBufferInfo->pRegions[i].dstOffset;
        regions[i].size = pCopyBufferInfo->pRegions[i].size;
    }
    vkCmdCopyBuffer(commandBuffer, pCopyBufferInfo->srcBuffer,
                    pCopyBufferInfo->dstBuffer, n, regions);
}

/* ---- copy buffer <-> image ----------------------------------------------- */
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(
    VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
    VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy *pRegions)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    (void)dstImageLayout;
    if (!cb || !pRegions) return;
    c = vk_cmd_push(cb, VK_CMD_COPY_BUFFER_TO_IMAGE);
    if (!c) return;
    c->u.copy_b2i.src = (vk_buffer *)VK_H2P(srcBuffer);
    c->u.copy_b2i.dst = (vk_image *)VK_H2P(dstImage);
    c->u.copy_b2i.n = regionCount > 16 ? 16 : regionCount;
    memcpy(c->u.copy_b2i.regions, pRegions, c->u.copy_b2i.n * sizeof(VkBufferImageCopy));
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage2(
    VkCommandBuffer commandBuffer, const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo)
{
    VkBufferImageCopy regions[16];
    uint32_t i, n;
    if (!pCopyBufferToImageInfo) return;
    n = pCopyBufferToImageInfo->regionCount > 16 ? 16 : pCopyBufferToImageInfo->regionCount;
    for (i = 0; i < n; i++) {
        const VkBufferImageCopy2 *s = &pCopyBufferToImageInfo->pRegions[i];
        regions[i].bufferOffset = s->bufferOffset;
        regions[i].bufferRowLength = s->bufferRowLength;
        regions[i].bufferImageHeight = s->bufferImageHeight;
        regions[i].imageSubresource = s->imageSubresource;
        regions[i].imageOffset = s->imageOffset;
        regions[i].imageExtent = s->imageExtent;
    }
    vkCmdCopyBufferToImage(commandBuffer, pCopyBufferToImageInfo->srcBuffer,
                           pCopyBufferToImageInfo->dstImage,
                           pCopyBufferToImageInfo->dstImageLayout, n, regions);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(
    VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
    VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy *pRegions)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    (void)srcImageLayout;
    if (!cb || !pRegions) return;
    c = vk_cmd_push(cb, VK_CMD_COPY_IMAGE_TO_BUFFER);
    if (!c) return;
    c->u.copy_i2b.src = (vk_image *)VK_H2P(srcImage);
    c->u.copy_i2b.dst = (vk_buffer *)VK_H2P(dstBuffer);
    c->u.copy_i2b.n = regionCount > 16 ? 16 : regionCount;
    memcpy(c->u.copy_i2b.regions, pRegions, c->u.copy_i2b.n * sizeof(VkBufferImageCopy));
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer2(
    VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo)
{
    VkBufferImageCopy regions[16];
    uint32_t i, n;
    if (!pCopyImageToBufferInfo) return;
    n = pCopyImageToBufferInfo->regionCount > 16 ? 16 : pCopyImageToBufferInfo->regionCount;
    for (i = 0; i < n; i++) {
        const VkBufferImageCopy2 *s = &pCopyImageToBufferInfo->pRegions[i];
        regions[i].bufferOffset = s->bufferOffset;
        regions[i].bufferRowLength = s->bufferRowLength;
        regions[i].bufferImageHeight = s->bufferImageHeight;
        regions[i].imageSubresource = s->imageSubresource;
        regions[i].imageOffset = s->imageOffset;
        regions[i].imageExtent = s->imageExtent;
    }
    vkCmdCopyImageToBuffer(commandBuffer, pCopyImageToBufferInfo->srcImage,
                           pCopyImageToBufferInfo->srcImageLayout,
                           pCopyImageToBufferInfo->dstBuffer, n, regions);
}

/* ---- copy / blit / resolve image ----------------------------------------- */
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(
    VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
    VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
    const VkImageCopy *pRegions)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    (void)srcImageLayout; (void)dstImageLayout;
    if (!cb || !pRegions) return;
    c = vk_cmd_push(cb, VK_CMD_COPY_IMAGE);
    if (!c) return;
    c->u.copy_img.src = (vk_image *)VK_H2P(srcImage);
    c->u.copy_img.dst = (vk_image *)VK_H2P(dstImage);
    c->u.copy_img.n = regionCount > 8 ? 8 : regionCount;
    memcpy(c->u.copy_img.regions, pRegions, c->u.copy_img.n * sizeof(VkImageCopy));
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage2(
    VkCommandBuffer commandBuffer, const VkCopyImageInfo2 *pCopyImageInfo)
{
    VkImageCopy regions[8];
    uint32_t i, n;
    if (!pCopyImageInfo) return;
    n = pCopyImageInfo->regionCount > 8 ? 8 : pCopyImageInfo->regionCount;
    for (i = 0; i < n; i++) {
        const VkImageCopy2 *s = &pCopyImageInfo->pRegions[i];
        regions[i].srcSubresource = s->srcSubresource;
        regions[i].srcOffset = s->srcOffset;
        regions[i].dstSubresource = s->dstSubresource;
        regions[i].dstOffset = s->dstOffset;
        regions[i].extent = s->extent;
    }
    vkCmdCopyImage(commandBuffer, pCopyImageInfo->srcImage, pCopyImageInfo->srcImageLayout,
                   pCopyImageInfo->dstImage, pCopyImageInfo->dstImageLayout, n, regions);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(
    VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
    VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
    const VkImageBlit *pRegions, VkFilter filter)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    (void)srcImageLayout; (void)dstImageLayout;
    if (!cb || !pRegions) return;
    c = vk_cmd_push(cb, VK_CMD_BLIT_IMAGE);
    if (!c) return;
    c->u.blit.src = (vk_image *)VK_H2P(srcImage);
    c->u.blit.dst = (vk_image *)VK_H2P(dstImage);
    c->u.blit.n = regionCount > 8 ? 8 : regionCount;
    c->u.blit.filter = filter;
    memcpy(c->u.blit.regions, pRegions, c->u.blit.n * sizeof(VkImageBlit));
}

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage2(
    VkCommandBuffer commandBuffer, const VkBlitImageInfo2 *pBlitImageInfo)
{
    VkImageBlit regions[8];
    uint32_t i, n;
    if (!pBlitImageInfo) return;
    n = pBlitImageInfo->regionCount > 8 ? 8 : pBlitImageInfo->regionCount;
    for (i = 0; i < n; i++) {
        const VkImageBlit2 *s = &pBlitImageInfo->pRegions[i];
        regions[i].srcSubresource = s->srcSubresource;
        regions[i].srcOffsets[0] = s->srcOffsets[0];
        regions[i].srcOffsets[1] = s->srcOffsets[1];
        regions[i].dstSubresource = s->dstSubresource;
        regions[i].dstOffsets[0] = s->dstOffsets[0];
        regions[i].dstOffsets[1] = s->dstOffsets[1];
    }
    vkCmdBlitImage(commandBuffer, pBlitImageInfo->srcImage, pBlitImageInfo->srcImageLayout,
                   pBlitImageInfo->dstImage, pBlitImageInfo->dstImageLayout,
                   n, regions, pBlitImageInfo->filter);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage(
    VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
    VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
    const VkImageResolve *pRegions)
{
    /* multisample resolve: the software device renders single-sampled, so a
     * resolve degenerates to a plain region copy. */
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    uint32_t i;
    (void)srcImageLayout; (void)dstImageLayout;
    if (!cb || !pRegions) return;
    c = vk_cmd_push(cb, VK_CMD_COPY_IMAGE);
    if (!c) return;
    c->u.copy_img.src = (vk_image *)VK_H2P(srcImage);
    c->u.copy_img.dst = (vk_image *)VK_H2P(dstImage);
    c->u.copy_img.n = regionCount > 8 ? 8 : regionCount;
    for (i = 0; i < c->u.copy_img.n; i++) {
        c->u.copy_img.regions[i].srcSubresource = pRegions[i].srcSubresource;
        c->u.copy_img.regions[i].srcOffset = pRegions[i].srcOffset;
        c->u.copy_img.regions[i].dstSubresource = pRegions[i].dstSubresource;
        c->u.copy_img.regions[i].dstOffset = pRegions[i].dstOffset;
        c->u.copy_img.regions[i].extent = pRegions[i].extent;
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage2(
    VkCommandBuffer commandBuffer, const VkResolveImageInfo2 *pResolveImageInfo)
{
    VkImageResolve regions[8];
    uint32_t i, n;
    if (!pResolveImageInfo) return;
    n = pResolveImageInfo->regionCount > 8 ? 8 : pResolveImageInfo->regionCount;
    for (i = 0; i < n; i++) {
        const VkImageResolve2 *s = &pResolveImageInfo->pRegions[i];
        regions[i].srcSubresource = s->srcSubresource;
        regions[i].srcOffset = s->srcOffset;
        regions[i].dstSubresource = s->dstSubresource;
        regions[i].dstOffset = s->dstOffset;
        regions[i].extent = s->extent;
    }
    vkCmdResolveImage(commandBuffer, pResolveImageInfo->srcImage,
                      pResolveImageInfo->srcImageLayout, pResolveImageInfo->dstImage,
                      pResolveImageInfo->dstImageLayout, n, regions);
}

/* ---- clear --------------------------------------------------------------- */
VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(
    VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
    const VkClearColorValue *pColor, uint32_t rangeCount,
    const VkImageSubresourceRange *pRanges)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    (void)imageLayout; (void)rangeCount;
    if (!cb || !pColor) return;
    c = vk_cmd_push(cb, VK_CMD_CLEAR_COLOR_IMAGE);
    if (!c) return;
    c->u.clear_color.img = (vk_image *)VK_H2P(image);
    c->u.clear_color.color = *pColor;
    if (pRanges) c->u.clear_color.range = pRanges[0];
}

VKAPI_ATTR void VKAPI_CALL vkCmdClearDepthStencilImage(
    VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
    const VkClearDepthStencilValue *pDepthStencil, uint32_t rangeCount,
    const VkImageSubresourceRange *pRanges)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    (void)imageLayout; (void)rangeCount;
    if (!cb || !pDepthStencil) return;
    c = vk_cmd_push(cb, VK_CMD_CLEAR_DEPTH_STENCIL_IMAGE);
    if (!c) return;
    c->u.clear_ds.img = (vk_image *)VK_H2P(image);
    c->u.clear_ds.ds = *pDepthStencil;
    if (pRanges) c->u.clear_ds.range = pRanges[0];
}

VKAPI_ATTR void VKAPI_CALL vkCmdClearAttachments(
    VkCommandBuffer commandBuffer, uint32_t attachmentCount,
    const VkClearAttachment *pAttachments, uint32_t rectCount, const VkClearRect *pRects)
{
    /* Applied to the currently bound render target(s) at record time; clears
     * precede draws in the common case, and the executor runs the recorded list
     * in order at submit. */
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    uint32_t i;
    (void)rectCount; (void)pRects;
    if (!cb || !pAttachments) return;
    for (i = 0; i < attachmentCount; i++) {
        const VkClearAttachment *a = &pAttachments[i];
        vk_cmd *c;
        if (a->aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT ||
            a->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) {
            vk_image_view *iv = cb->depth;
            if (!iv || !iv->image) continue;
            c = vk_cmd_push(cb, VK_CMD_CLEAR_DEPTH_STENCIL_IMAGE);
            if (c) { c->u.clear_ds.img = iv->image; c->u.clear_ds.ds = a->clearValue.depthStencil; }
        } else {
            uint32_t ci = a->colorAttachment;
            vk_image_view *iv = (ci < cb->num_color) ? cb->color[ci] : NULL;
            if (!iv || !iv->image) continue;
            c = vk_cmd_push(cb, VK_CMD_CLEAR_COLOR_IMAGE);
            if (c) { c->u.clear_color.img = iv->image; c->u.clear_color.color = a->clearValue.color; }
        }
    }
}

/* ---- fill / update buffer ------------------------------------------------ */
VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(
    VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset,
    VkDeviceSize size, uint32_t data)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    if (!cb) return;
    c = vk_cmd_push(cb, VK_CMD_FILL_BUFFER);
    if (!c) return;
    c->u.fill.dst = (vk_buffer *)VK_H2P(dstBuffer);
    c->u.fill.offset = dstOffset;
    c->u.fill.size = size;
    c->u.fill.data = data;
}

VKAPI_ATTR void VKAPI_CALL vkCmdUpdateBuffer(
    VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset,
    VkDeviceSize dataSize, const void *pData)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    if (!cb || !pData || dataSize > 4096) return;
    c = vk_cmd_push(cb, VK_CMD_UPDATE_BUFFER);
    if (!c) return;
    c->u.update.dst = (vk_buffer *)VK_H2P(dstBuffer);
    c->u.update.offset = dstOffset;
    c->u.update.size = dataSize;
    memcpy(c->u.update.data, pData, (size_t)dataSize);
}

/* ---- barrier (no-op ordering hint) --------------------------------------- */
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(
    VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
    uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers)
{
    (void)srcStageMask; (void)dstStageMask; (void)dependencyFlags;
    (void)memoryBarrierCount; (void)pMemoryBarriers;
    (void)bufferMemoryBarrierCount; (void)pBufferMemoryBarriers;
    (void)imageMemoryBarrierCount; (void)pImageMemoryBarriers;
    vk_cmd_push((vk_command_buffer *)VK_H2P(commandBuffer), VK_CMD_PIPELINE_BARRIER);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier2(
    VkCommandBuffer commandBuffer, const VkDependencyInfo *pDependencyInfo)
{
    (void)pDependencyInfo;
    vk_cmd_push((vk_command_buffer *)VK_H2P(commandBuffer), VK_CMD_PIPELINE_BARRIER);
}

/* ---- events -------------------------------------------------------------- */
VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent(
    VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    (void)stageMask;
    if (!cb) return;
    c = vk_cmd_push(cb, VK_CMD_SET_EVENT);
    if (c) c->u.event_op.event = (vk_event *)VK_H2P(event);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent(
    VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    (void)stageMask;
    if (!cb) return;
    c = vk_cmd_push(cb, VK_CMD_RESET_EVENT);
    if (c) c->u.event_op.event = (vk_event *)VK_H2P(event);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents(
    VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent *pEvents,
    VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
    uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers)
{
    (void)eventCount; (void)pEvents; (void)srcStageMask; (void)dstStageMask;
    (void)memoryBarrierCount; (void)pMemoryBarriers;
    (void)bufferMemoryBarrierCount; (void)pBufferMemoryBarriers;
    (void)imageMemoryBarrierCount; (void)pImageMemoryBarriers;
    vk_cmd_push((vk_command_buffer *)VK_H2P(commandBuffer), VK_CMD_WAIT_EVENTS);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents2(
    VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent *pEvents,
    const VkDependencyInfo *pDependencyInfos)
{
    (void)eventCount; (void)pEvents; (void)pDependencyInfos;
    vk_cmd_push((vk_command_buffer *)VK_H2P(commandBuffer), VK_CMD_WAIT_EVENTS);
}

/* ---- query / timestamp --------------------------------------------------- */
VKAPI_ATTR void VKAPI_CALL vkCmdBeginQuery(
    VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    (void)flags;
    if (!cb) return;
    c = vk_cmd_push(cb, VK_CMD_BEGIN_QUERY);
    if (c) {
        c->u.query_op.pool = (vk_query_pool *)VK_H2P(queryPool);
        c->u.query_op.query = query;
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndQuery(
    VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    if (!cb) return;
    c = vk_cmd_push(cb, VK_CMD_END_QUERY);
    if (c) {
        c->u.query_op.pool = (vk_query_pool *)VK_H2P(queryPool);
        c->u.query_op.query = query;
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetQueryPool(
    VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    (void)firstQuery; (void)queryCount;
    if (!cb) return;
    c = vk_cmd_push(cb, VK_CMD_RESET_QUERY_POOL);
    if (c) c->u.query_op.pool = (vk_query_pool *)VK_H2P(queryPool);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp(
    VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage,
    VkQueryPool queryPool, uint32_t query)
{
    vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(commandBuffer);
    vk_cmd *c;
    (void)pipelineStage;
    if (!cb) return;
    c = vk_cmd_push(cb, VK_CMD_WRITE_TIMESTAMP);
    if (c) {
        c->u.query_op.pool = (vk_query_pool *)VK_H2P(queryPool);
        c->u.query_op.query = query;
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp2(
    VkCommandBuffer commandBuffer, VkPipelineStageFlags2 pipelineStage,
    VkQueryPool queryPool, uint32_t query)
{
    (void)pipelineStage;
    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, query);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyQueryPoolResults(
    VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery,
    uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride,
    VkQueryResultFlags flags)
{
    vk_query_pool *q = (vk_query_pool *)VK_H2P(queryPool);
    vk_buffer *b = (vk_buffer *)VK_H2P(dstBuffer);
    uint8_t *dstp;
    uint32_t i;
    (void)commandBuffer;
    if (!q || !q->results || !b || !b->mem || !b->mem->host) return;
    dstp = (uint8_t *)b->mem->host + b->offset + dstOffset;
    for (i = 0; i < queryCount && (firstQuery + i) < q->count; i++) {
        uint64_t v = q->results[firstQuery + i];
        if (flags & VK_QUERY_RESULT_64_BIT) {
            *(uint64_t *)(dstp + (size_t)i * stride) = v;
            if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
                *(uint64_t *)(dstp + (size_t)i * stride + 8) = 1;
        } else {
            *(uint32_t *)(dstp + (size_t)i * stride) = (uint32_t)v;
            if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
                *(uint32_t *)(dstp + (size_t)i * stride + 4) = 1;
        }
    }
}
