/*
 * pipeline.c - VkShaderModule (stores + parses the SPIR-V once), graphics and
 * compute pipeline creation (capturing the pipeline state the software
 * rasterizer honours), and the pipeline-cache surface.  A shader module keeps an
 * owned copy of its SPIR-V words plus the parsed module the interpreter walks;
 * a parse failure leaves module NULL so the pipeline simply declines at execute
 * time rather than being rejected outright.
 */
#include "internal.h"
#include "spirv.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- shader module ------------------------------------------------------- */
/* Pipelines embed a pointer to the parsed SPIR-V of their stages, so they hold
 * a reference that outlives vkDestroyShaderModule (legal per spec once the
 * pipeline exists).  The module is freed when the last reference drops. */
static void shader_module_retain(vk_shader_module *sm)
{
    if (sm) sm->refs++;
}

static void shader_module_release(vk_shader_module *sm)
{
    if (!sm) return;
    if (sm->refs) sm->refs--;
    if (sm->refs) return;
    if (sm->module) spirv_free(sm->module);
    free(sm->code);
    vk_obj_free(sm);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(
    VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule)
{
    vk_shader_module *sm;
    size_t dwords;
    char err[256];
    (void)device; (void)pAllocator;
    if (!pCreateInfo || !pShaderModule || !pCreateInfo->pCode ||
        (pCreateInfo->codeSize & 3) != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    sm = (vk_shader_module *)vk_obj_alloc(sizeof(vk_shader_module), VK_OBJ_SHADER_MODULE);
    if (!sm) return VK_ERROR_OUT_OF_HOST_MEMORY;
    dwords = pCreateInfo->codeSize / 4;
    sm->code = (uint32_t *)malloc(pCreateInfo->codeSize);
    if (!sm->code) { vk_obj_free(sm); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    memcpy(sm->code, pCreateInfo->pCode, pCreateInfo->codeSize);
    sm->code_size = dwords;
    sm->refs = 1;
    err[0] = 0;
    sm->module = spirv_parse(sm->code, dwords, err, sizeof(err));
    /* a module that fails to parse is still returned (module == NULL): every
     * execute path declines it, so it can never mis-render. */
    *pShaderModule = VK_P2H(VkShaderModule, sm);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(
    VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks *pAllocator)
{
    vk_shader_module *sm = (vk_shader_module *)VK_H2P(shaderModule);
    (void)device; (void)pAllocator;
    if (!sm) return;
    shader_module_release(sm);
}

/* ---- pipeline helpers ---------------------------------------------------- */
static void capture_gfx_state(vk_pipeline *p, const VkGraphicsPipelineCreateInfo *ci)
{
    p->bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    p->layout = (vk_pipeline_layout *)VK_H2P(ci->layout);
    p->render_pass = (vk_render_pass *)VK_H2P(ci->renderPass);
    p->subpass = ci->subpass;

    /* shader stages */
    if (ci->pStages) {
        uint32_t i;
        p->num_stages = ci->stageCount;
        if (p->num_stages > VK_MAX_SHADER_STAGES) p->num_stages = VK_MAX_SHADER_STAGES;
        for (i = 0; i < p->num_stages; i++) {
            p->stages[i].stage = ci->pStages[i].stage;
            p->stages[i].module = (vk_shader_module *)VK_H2P(ci->pStages[i].module);
            shader_module_retain(p->stages[i].module);
            if (ci->pStages[i].pName)
                snprintf(p->stages[i].entry, sizeof(p->stages[i].entry),
                         "%s", ci->pStages[i].pName);
        }
    }
    /* vertex input */
    if (ci->pVertexInputState) {
        const VkPipelineVertexInputStateCreateInfo *vi = ci->pVertexInputState;
        uint32_t i;
        p->num_vtx_bindings = vi->vertexBindingDescriptionCount;
        if (p->num_vtx_bindings > VK_MAX_VERTEX_BINDINGS)
            p->num_vtx_bindings = VK_MAX_VERTEX_BINDINGS;
        for (i = 0; i < p->num_vtx_bindings; i++) {
            p->vtx_bindings[i].binding = vi->pVertexBindingDescriptions[i].binding;
            p->vtx_bindings[i].stride = vi->pVertexBindingDescriptions[i].stride;
            p->vtx_bindings[i].rate = vi->pVertexBindingDescriptions[i].inputRate;
        }
        p->num_vtx_attrs = vi->vertexAttributeDescriptionCount;
        if (p->num_vtx_attrs > VK_MAX_VERTEX_ATTRIBUTES)
            p->num_vtx_attrs = VK_MAX_VERTEX_ATTRIBUTES;
        for (i = 0; i < p->num_vtx_attrs; i++) {
            p->vtx_attrs[i].location = vi->pVertexAttributeDescriptions[i].location;
            p->vtx_attrs[i].binding = vi->pVertexAttributeDescriptions[i].binding;
            p->vtx_attrs[i].offset = vi->pVertexAttributeDescriptions[i].offset;
            p->vtx_attrs[i].format = vi->pVertexAttributeDescriptions[i].format;
        }
    }
    /* input assembly */
    p->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    if (ci->pInputAssemblyState)
        p->topology = ci->pInputAssemblyState->topology;
    /* rasterization */
    if (ci->pRasterizationState) {
        p->cull_mode = ci->pRasterizationState->cullMode;
        p->front_face = ci->pRasterizationState->frontFace;
        p->rasterizer_discard = ci->pRasterizationState->rasterizerDiscardEnable;
    }
    /* depth stencil */
    p->depth_compare = VK_COMPARE_OP_LESS;
    if (ci->pDepthStencilState) {
        p->depth_test_enable = ci->pDepthStencilState->depthTestEnable;
        p->depth_write_enable = ci->pDepthStencilState->depthWriteEnable;
        p->depth_compare = ci->pDepthStencilState->depthCompareOp;
    }
    /* color blend (attachment 0) */
    if (ci->pColorBlendState && ci->pColorBlendState->attachmentCount > 0) {
        const VkPipelineColorBlendAttachmentState *a = &ci->pColorBlendState->pAttachments[0];
        p->blend_enable = a->blendEnable;
        p->src_color = a->srcColorBlendFactor;
        p->dst_color = a->dstColorBlendFactor;
        p->src_alpha = a->srcAlphaBlendFactor;
        p->dst_alpha = a->dstAlphaBlendFactor;
        p->color_op = a->colorBlendOp;
        p->alpha_op = a->alphaBlendOp;
    }
}

/* ---- graphics / compute pipelines ---------------------------------------- */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(
    VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount,
    const VkGraphicsPipelineCreateInfo *pCreateInfos,
    const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
    vk_device *dev = (vk_device *)VK_H2P(device);
    uint32_t i;
    (void)pipelineCache; (void)pAllocator;
    if (!dev || !pCreateInfos || !pPipelines) return VK_ERROR_INITIALIZATION_FAILED;
    for (i = 0; i < createInfoCount; i++) {
        vk_pipeline *p = (vk_pipeline *)vk_obj_alloc(sizeof(vk_pipeline), VK_OBJ_PIPELINE);
        if (!p) return VK_ERROR_OUT_OF_HOST_MEMORY;
        p->device = dev;
        capture_gfx_state(p, &pCreateInfos[i]);
        pPipelines[i] = VK_P2H(VkPipeline, p);
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(
    VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount,
    const VkComputePipelineCreateInfo *pCreateInfos,
    const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
    vk_device *dev = (vk_device *)VK_H2P(device);
    uint32_t i;
    (void)pipelineCache; (void)pAllocator;
    if (!dev || !pCreateInfos || !pPipelines) return VK_ERROR_INITIALIZATION_FAILED;
    for (i = 0; i < createInfoCount; i++) {
        const VkComputePipelineCreateInfo *ci = &pCreateInfos[i];
        vk_pipeline *p = (vk_pipeline *)vk_obj_alloc(sizeof(vk_pipeline), VK_OBJ_PIPELINE);
        if (!p) return VK_ERROR_OUT_OF_HOST_MEMORY;
        p->device = dev;
        p->bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
        p->layout = (vk_pipeline_layout *)VK_H2P(ci->layout);
        p->num_stages = 1;
        p->stages[0].stage = VK_SHADER_STAGE_COMPUTE_BIT;
        p->stages[0].module = (vk_shader_module *)VK_H2P(ci->stage.module);
        shader_module_retain(p->stages[0].module);
        if (ci->stage.pName)
            snprintf(p->stages[0].entry, sizeof(p->stages[0].entry), "%s", ci->stage.pName);
        pPipelines[i] = VK_P2H(VkPipeline, p);
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(
    VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *pAllocator)
{
    vk_device *dev = (vk_device *)VK_H2P(device);
    vk_pipeline *p = (vk_pipeline *)VK_H2P(pipeline);
    (void)pAllocator;
    if (!p) return;
    {
        uint32_t i;
        for (i = 0; i < p->num_stages; i++)
            shader_module_release(p->stages[i].module);
    }
    if (dev && dev->backend && dev->backend->pipeline_free)
        dev->backend->pipeline_free(p);
    vk_obj_free(p);
}

/* ---- pipeline cache ------------------------------------------------------ */
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(
    VkDevice device, const VkPipelineCacheCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkPipelineCache *pPipelineCache)
{
    vk_pipeline_cache *c;
    (void)device; (void)pAllocator;
    if (!pPipelineCache) return VK_ERROR_INITIALIZATION_FAILED;
    c = (vk_pipeline_cache *)vk_obj_alloc(sizeof(vk_pipeline_cache), VK_OBJ_PIPELINE_CACHE);
    if (!c) return VK_ERROR_OUT_OF_HOST_MEMORY;
    if (pCreateInfo && pCreateInfo->initialDataSize && pCreateInfo->pInitialData) {
        c->data = (uint8_t *)malloc(pCreateInfo->initialDataSize);
        if (c->data) {
            memcpy(c->data, pCreateInfo->pInitialData, pCreateInfo->initialDataSize);
            c->size = pCreateInfo->initialDataSize;
        }
    }
    *pPipelineCache = VK_P2H(VkPipelineCache, c);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineCache(
    VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks *pAllocator)
{
    vk_pipeline_cache *c = (vk_pipeline_cache *)VK_H2P(pipelineCache);
    (void)device; (void)pAllocator;
    if (!c) return;
    free(c->data);
    vk_obj_free(c);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineCacheData(
    VkDevice device, VkPipelineCache pipelineCache, size_t *pDataSize, void *pData)
{
    vk_pipeline_cache *c = (vk_pipeline_cache *)VK_H2P(pipelineCache);
    (void)device;
    if (!pDataSize) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pData) { *pDataSize = c ? c->size : 0; return VK_SUCCESS; }
    if (c && c->size && c->size <= *pDataSize) {
        memcpy(pData, c->data, c->size);
        *pDataSize = c->size;
        return VK_SUCCESS;
    }
    *pDataSize = 0;
    return VK_INCOMPLETE;
}

VKAPI_ATTR VkResult VKAPI_CALL vkMergePipelineCaches(
    VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount,
    const VkPipelineCache *pSrcCaches)
{
    (void)device; (void)dstCache; (void)srcCacheCount; (void)pSrcCaches;
    return VK_SUCCESS;   /* software device produces no reusable cache blob */
}
