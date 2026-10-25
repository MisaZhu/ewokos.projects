/*
 * descriptor.c - descriptor set layouts, pipeline layouts, descriptor pools and
 * sets, and the descriptor write/copy path.  Descriptor sets are plain arrays of
 * resolved bindings; vkUpdateDescriptorSets copies the referenced buffer / image
 * / sampler into the set so the CPU back-end can resolve them straight to host
 * pointers at execute time.
 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* ---- set layout / pipeline layout ---------------------------------------- */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(
    VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout)
{
    vk_descriptor_set_layout *l;
    uint32_t i;
    (void)device; (void)pAllocator;
    if (!pCreateInfo || !pSetLayout) return VK_ERROR_INITIALIZATION_FAILED;
    l = (vk_descriptor_set_layout *)
        vk_obj_alloc(sizeof(vk_descriptor_set_layout), VK_OBJ_DESCRIPTOR_SET_LAYOUT);
    if (!l) return VK_ERROR_OUT_OF_HOST_MEMORY;
    l->num_bindings = pCreateInfo->bindingCount;
    if (l->num_bindings > 32) l->num_bindings = 32;
    for (i = 0; i < l->num_bindings; i++)
        l->bindings[i] = pCreateInfo->pBindings[i];
    *pSetLayout = VK_P2H(VkDescriptorSetLayout, l);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(
    VkDevice device, VkDescriptorSetLayout descriptorSetLayout,
    const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    vk_obj_free(VK_H2P(descriptorSetLayout));
}

VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutSupport(
    VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
    VkDescriptorSetLayoutSupport *pSupport)
{
    (void)device; (void)pCreateInfo;
    /* the software device supports every layout the app can describe here */
    if (pSupport) pSupport->supported = VK_TRUE;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(
    VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkPipelineLayout *pPipelineLayout)
{
    vk_pipeline_layout *l;
    uint32_t i;
    (void)device; (void)pAllocator;
    if (!pCreateInfo || !pPipelineLayout) return VK_ERROR_INITIALIZATION_FAILED;
    l = (vk_pipeline_layout *)vk_obj_alloc(sizeof(vk_pipeline_layout), VK_OBJ_PIPELINE_LAYOUT);
    if (!l) return VK_ERROR_OUT_OF_HOST_MEMORY;
    l->num_sets = pCreateInfo->setLayoutCount;
    if (l->num_sets > VK_MAX_DESCRIPTOR_SETS) l->num_sets = VK_MAX_DESCRIPTOR_SETS;
    for (i = 0; i < l->num_sets; i++)
        l->sets[i] = (vk_descriptor_set_layout *)VK_H2P(pCreateInfo->pSetLayouts[i]);
    l->num_push = pCreateInfo->pushConstantRangeCount;
    if (l->num_push > 4) l->num_push = 4;
    for (i = 0; i < l->num_push; i++)
        l->push[i] = pCreateInfo->pPushConstantRanges[i];
    *pPipelineLayout = VK_P2H(VkPipelineLayout, l);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(
    VkDevice device, VkPipelineLayout pipelineLayout,
    const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    vk_obj_free(VK_H2P(pipelineLayout));
}

/* ---- descriptor pool ----------------------------------------------------- */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(
    VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool)
{
    vk_device *dev = (vk_device *)VK_H2P(device);
    vk_descriptor_pool *p;
    (void)pAllocator;
    if (!dev || !pCreateInfo || !pDescriptorPool) return VK_ERROR_INITIALIZATION_FAILED;
    p = (vk_descriptor_pool *)vk_obj_alloc(sizeof(vk_descriptor_pool), VK_OBJ_DESCRIPTOR_POOL);
    if (!p) return VK_ERROR_OUT_OF_HOST_MEMORY;
    p->device = dev;
    p->max_sets = pCreateInfo->maxSets;
    p->num_sizes = pCreateInfo->poolSizeCount;
    if (p->num_sizes > 8) p->num_sizes = 8;
    memcpy(p->sizes, pCreateInfo->pPoolSizes, p->num_sizes * sizeof(VkDescriptorPoolSize));
    *pDescriptorPool = VK_P2H(VkDescriptorPool, p);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(
    VkDevice device, VkDescriptorPool descriptorPool,
    const VkAllocationCallbacks *pAllocator)
{
    vk_descriptor_pool *p = (vk_descriptor_pool *)VK_H2P(descriptorPool);
    uint32_t i;
    (void)device; (void)pAllocator;
    if (!p) return;
    for (i = 0; i < p->num_sets; i++)
        vk_obj_free(p->sets[i]);
    vk_obj_free(p);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(
    VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags)
{
    vk_descriptor_pool *p = (vk_descriptor_pool *)VK_H2P(descriptorPool);
    uint32_t i;
    (void)device; (void)flags;
    if (!p) return VK_ERROR_INITIALIZATION_FAILED;
    for (i = 0; i < p->num_sets; i++)
        vk_obj_free(p->sets[i]);
    p->num_sets = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(
    VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo,
    VkDescriptorSet *pDescriptorSets)
{
    vk_descriptor_pool *pool = (vk_descriptor_pool *)VK_H2P(pAllocateInfo->descriptorPool);
    uint32_t i;
    (void)device;
    if (!pool || !pAllocateInfo || !pDescriptorSets) return VK_ERROR_INITIALIZATION_FAILED;
    for (i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
        vk_descriptor_set *s;
        if (pool->num_sets >= 256) return VK_ERROR_OUT_OF_POOL_MEMORY;
        s = (vk_descriptor_set *)vk_obj_alloc(sizeof(vk_descriptor_set), VK_OBJ_DESCRIPTOR_SET);
        if (!s) return VK_ERROR_OUT_OF_HOST_MEMORY;
        s->layout = (vk_descriptor_set_layout *)VK_H2P(pAllocateInfo->pSetLayouts[i]);
        s->num_bindings = 0;
        pool->sets[pool->num_sets++] = s;
        pDescriptorSets[i] = VK_P2H(VkDescriptorSet, s);
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(
    VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount,
    const VkDescriptorSet *pDescriptorSets)
{
    vk_descriptor_pool *pool = (vk_descriptor_pool *)VK_H2P(descriptorPool);
    uint32_t i, j;
    (void)device;
    if (!pool || !pDescriptorSets) return VK_ERROR_INITIALIZATION_FAILED;
    for (i = 0; i < descriptorSetCount; i++) {
        vk_descriptor_set *s = (vk_descriptor_set *)VK_H2P(pDescriptorSets[i]);
        if (!s) continue;
        for (j = 0; j < pool->num_sets; j++)
            if (pool->sets[j] == s) {
                pool->sets[j] = pool->sets[--pool->num_sets];
                break;
            }
        vk_obj_free(s);
    }
    return VK_SUCCESS;
}

/* ---- descriptor writes --------------------------------------------------- */
static vk_descriptor_binding *find_or_add(vk_descriptor_set *s, uint32_t binding,
                                          VkDescriptorType type)
{
    uint32_t i;
    for (i = 0; i < s->num_bindings; i++)
        if (s->bindings[i].binding == binding && s->bindings[i].type == type)
            return &s->bindings[i];
    if (s->num_bindings >= 64) return NULL;
    memset(&s->bindings[s->num_bindings], 0, sizeof(vk_descriptor_binding));
    s->bindings[s->num_bindings].binding = binding;
    s->bindings[s->num_bindings].type = type;
    s->bindings[s->num_bindings].count = 1;
    return &s->bindings[s->num_bindings++];
}

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(
    VkDevice device, uint32_t descriptorWriteCount,
    const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount,
    const VkCopyDescriptorSet *pDescriptorCopies)
{
    uint32_t i;
    (void)device;
    for (i = 0; i < descriptorWriteCount; i++) {
        const VkWriteDescriptorSet *w = &pDescriptorWrites[i];
        vk_descriptor_set *s = (vk_descriptor_set *)VK_H2P(w->dstSet);
        vk_descriptor_binding *b;
        if (!s) continue;
        b = find_or_add(s, w->dstBinding, w->descriptorType);
        if (!b) continue;
        b->count = w->descriptorCount ? w->descriptorCount : 1;
        switch (w->descriptorType) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            if (w->pBufferInfo) {
                b->buffer = (vk_buffer *)VK_H2P(w->pBufferInfo[0].buffer);
                b->offset = w->pBufferInfo[0].offset;
                b->range = w->pBufferInfo[0].range;
            }
            break;
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            if (w->pImageInfo) {
                b->view = (vk_image_view *)VK_H2P(w->pImageInfo[0].imageView);
                b->sampler = (vk_sampler *)VK_H2P(w->pImageInfo[0].sampler);
                b->layout = w->pImageInfo[0].imageLayout;
            }
            break;
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            if (w->pImageInfo) {
                b->view = (vk_image_view *)VK_H2P(w->pImageInfo[0].imageView);
                b->layout = w->pImageInfo[0].imageLayout;
            }
            break;
        case VK_DESCRIPTOR_TYPE_SAMPLER:
            if (w->pImageInfo)
                b->sampler = (vk_sampler *)VK_H2P(w->pImageInfo[0].sampler);
            break;
        default:
            break;
        }
    }
    for (i = 0; i < descriptorCopyCount; i++) {
        const VkCopyDescriptorSet *c = &pDescriptorCopies[i];
        vk_descriptor_set *src = (vk_descriptor_set *)VK_H2P(c->srcSet);
        vk_descriptor_set *dst = (vk_descriptor_set *)VK_H2P(c->dstSet);
        uint32_t j;
        if (!src || !dst) continue;
        for (j = 0; j < src->num_bindings; j++) {
            if (src->bindings[j].binding != c->srcBinding) continue;
            if (dst->num_bindings >= 64) break;
            dst->bindings[dst->num_bindings] = src->bindings[j];
            dst->bindings[dst->num_bindings].binding = c->dstBinding;
            dst->num_bindings++;
            break;
        }
    }
}

/* ---- update templates (stored, applied on demand) ------------------------ */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplate(
    VkDevice device, const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate)
{
    vk_descriptor_update_template *t;
    (void)device; (void)pCreateInfo; (void)pAllocator;
    if (!pDescriptorUpdateTemplate) return VK_ERROR_INITIALIZATION_FAILED;
    t = (vk_descriptor_update_template *)
        vk_obj_alloc(sizeof(vk_descriptor_update_template), VK_OBJ_DESCRIPTOR_UPDATE_TEMPLATE);
    if (!t) return VK_ERROR_OUT_OF_HOST_MEMORY;
    *pDescriptorUpdateTemplate = VK_P2H(VkDescriptorUpdateTemplate, t);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplate(
    VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate,
    const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    vk_obj_free(VK_H2P(descriptorUpdateTemplate));
}

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplate(
    VkDevice device, VkDescriptorSet descriptorSet,
    VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void *pData)
{
    (void)device; (void)descriptorSet; (void)descriptorUpdateTemplate; (void)pData;
    /* templates are accepted but not replayed: the software device resolves
     * descriptors through the plain vkUpdateDescriptorSets path. */
}
