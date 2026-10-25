/*
 * renderpass.c - VkRenderPass (1.0 and 1.3 RenderPass2 forms), VkFramebuffer,
 * and render-area granularity.  The software device uses a single-subpass model:
 * attachment descriptions and the (single) subpass's color/depth references are
 * captured so the executor can apply load/store clears and the rasterizer knows
 * which image views are the render targets.
 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(
    VkDevice device, const VkRenderPassCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass)
{
    vk_render_pass *rp;
    uint32_t i;
    (void)device; (void)pAllocator;
    if (!pCreateInfo || !pRenderPass) return VK_ERROR_INITIALIZATION_FAILED;
    rp = (vk_render_pass *)vk_obj_alloc(sizeof(vk_render_pass), VK_OBJ_RENDER_PASS);
    if (!rp) return VK_ERROR_OUT_OF_HOST_MEMORY;
    rp->num_attachments = pCreateInfo->attachmentCount;
    if (rp->num_attachments > VK_MAX_RENDER_PASS_ATTACH)
        rp->num_attachments = VK_MAX_RENDER_PASS_ATTACH;
    for (i = 0; i < rp->num_attachments; i++) {
        const VkAttachmentDescription *a = &pCreateInfo->pAttachments[i];
        rp->attachments[i].format = a->format;
        rp->attachments[i].samples = a->samples;
        rp->attachments[i].load = a->loadOp;
        rp->attachments[i].store = a->storeOp;
        rp->attachments[i].stencil_load = a->stencilLoadOp;
        rp->attachments[i].stencil_store = a->stencilStoreOp;
        rp->attachments[i].initial = a->initialLayout;
        rp->attachments[i].final_ = a->finalLayout;
    }
    if (pCreateInfo->subpassCount > 0 && pCreateInfo->pSubpasses) {
        const VkSubpassDescription *sp = &pCreateInfo->pSubpasses[0];
        rp->num_color_refs = sp->colorAttachmentCount;
        if (rp->num_color_refs > VK_MAX_SUBPASS_ATTACH)
            rp->num_color_refs = VK_MAX_SUBPASS_ATTACH;
        for (i = 0; i < rp->num_color_refs; i++)
            rp->color_refs[i] = sp->pColorAttachments[i];
        if (sp->pDepthStencilAttachment) {
            rp->depth_ref = *sp->pDepthStencilAttachment;
            rp->has_depth = VK_TRUE;
        }
    }
    *pRenderPass = VK_P2H(VkRenderPass, rp);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass2(
    VkDevice device, const VkRenderPassCreateInfo2 *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass)
{
    vk_render_pass *rp;
    uint32_t i;
    (void)device; (void)pAllocator;
    if (!pCreateInfo || !pRenderPass) return VK_ERROR_INITIALIZATION_FAILED;
    rp = (vk_render_pass *)vk_obj_alloc(sizeof(vk_render_pass), VK_OBJ_RENDER_PASS);
    if (!rp) return VK_ERROR_OUT_OF_HOST_MEMORY;
    rp->num_attachments = pCreateInfo->attachmentCount;
    if (rp->num_attachments > VK_MAX_RENDER_PASS_ATTACH)
        rp->num_attachments = VK_MAX_RENDER_PASS_ATTACH;
    for (i = 0; i < rp->num_attachments; i++) {
        const VkAttachmentDescription2 *a = &pCreateInfo->pAttachments[i];
        rp->attachments[i].format = a->format;
        rp->attachments[i].samples = a->samples;
        rp->attachments[i].load = a->loadOp;
        rp->attachments[i].store = a->storeOp;
        rp->attachments[i].stencil_load = a->stencilLoadOp;
        rp->attachments[i].stencil_store = a->stencilStoreOp;
        rp->attachments[i].initial = a->initialLayout;
        rp->attachments[i].final_ = a->finalLayout;
    }
    if (pCreateInfo->subpassCount > 0 && pCreateInfo->pSubpasses) {
        const VkSubpassDescription2 *sp = &pCreateInfo->pSubpasses[0];
        rp->num_color_refs = sp->colorAttachmentCount;
        if (rp->num_color_refs > VK_MAX_SUBPASS_ATTACH)
            rp->num_color_refs = VK_MAX_SUBPASS_ATTACH;
        for (i = 0; i < rp->num_color_refs; i++) {
            rp->color_refs[i].attachment = sp->pColorAttachments[i].attachment;
            rp->color_refs[i].layout = sp->pColorAttachments[i].layout;
        }
        if (sp->pDepthStencilAttachment) {
            rp->depth_ref.attachment = sp->pDepthStencilAttachment->attachment;
            rp->depth_ref.layout = sp->pDepthStencilAttachment->layout;
            rp->has_depth = VK_TRUE;
        }
    }
    *pRenderPass = VK_P2H(VkRenderPass, rp);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(
    VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    vk_obj_free(VK_H2P(renderPass));
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(
    VkDevice device, const VkFramebufferCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer)
{
    vk_framebuffer *fb;
    uint32_t i;
    (void)device; (void)pAllocator;
    if (!pCreateInfo || !pFramebuffer) return VK_ERROR_INITIALIZATION_FAILED;
    fb = (vk_framebuffer *)vk_obj_alloc(sizeof(vk_framebuffer), VK_OBJ_FRAMEBUFFER);
    if (!fb) return VK_ERROR_OUT_OF_HOST_MEMORY;
    fb->render_pass = (vk_render_pass *)VK_H2P(pCreateInfo->renderPass);
    fb->num_views = pCreateInfo->attachmentCount;
    if (fb->num_views > VK_MAX_RENDER_PASS_ATTACH)
        fb->num_views = VK_MAX_RENDER_PASS_ATTACH;
    for (i = 0; i < fb->num_views; i++)
        fb->views[i] = (vk_image_view *)VK_H2P(pCreateInfo->pAttachments[i]);
    fb->width = pCreateInfo->width;
    fb->height = pCreateInfo->height;
    fb->layers = pCreateInfo->layers;
    *pFramebuffer = VK_P2H(VkFramebuffer, fb);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(
    VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    vk_obj_free(VK_H2P(framebuffer));
}

VKAPI_ATTR void VKAPI_CALL vkGetRenderAreaGranularity(
    VkDevice device, VkRenderPass renderPass, VkExtent2D *pGranularity)
{
    (void)device; (void)renderPass;
    if (pGranularity) { pGranularity->width = 1; pGranularity->height = 1; }
}
