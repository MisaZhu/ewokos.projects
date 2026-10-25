/*
 * sync.c - fences, semaphores (binary + timeline), events, and query pools.
 * Because submission is immediate-mode, a fence/semaphore is signaled the moment
 * its submit runs, so wait calls observe final state without blocking; a wait on
 * an object that was never submitted reports timeout/not-ready rather than
 * hanging.
 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* ---- fence --------------------------------------------------------------- */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(
    VkDevice device, const VkFenceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkFence *pFence)
{
    vk_device *dev = (vk_device *)VK_H2P(device);
    vk_fence *f;
    (void)pAllocator;
    if (!dev || !pFence) return VK_ERROR_INITIALIZATION_FAILED;
    f = (vk_fence *)vk_obj_alloc(sizeof(vk_fence), VK_OBJ_FENCE);
    if (!f) return VK_ERROR_OUT_OF_HOST_MEMORY;
    f->device = dev;
    f->signaled = (pCreateInfo && (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT))
                      ? VK_TRUE : VK_FALSE;
    *pFence = VK_P2H(VkFence, f);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFence(
    VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    vk_obj_free(VK_H2P(fence));
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice device, VkFence fence)
{
    vk_fence *f = (vk_fence *)VK_H2P(fence);
    (void)device;
    if (!f) return VK_ERROR_DEVICE_LOST;
    return f->signaled ? VK_SUCCESS : VK_NOT_READY;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(
    VkDevice device, uint32_t fenceCount, const VkFence *pFences)
{
    uint32_t i;
    (void)device;
    for (i = 0; i < fenceCount; i++) {
        vk_fence *f = (vk_fence *)VK_H2P(pFences[i]);
        if (f) f->signaled = VK_FALSE;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(
    VkDevice device, uint32_t fenceCount, const VkFence *pFences,
    VkBool32 waitAll, uint64_t timeout)
{
    uint32_t i;
    int all = 1, any = 0;
    (void)device;
    for (i = 0; i < fenceCount; i++) {
        vk_fence *f = (vk_fence *)VK_H2P(pFences[i]);
        if (f && f->signaled) any = 1; else all = 0;
    }
    if (waitAll ? all : any) return VK_SUCCESS;
    return (timeout == 0) ? VK_TIMEOUT : VK_TIMEOUT;
}

/* ---- semaphore ----------------------------------------------------------- */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(
    VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore)
{
    vk_device *dev = (vk_device *)VK_H2P(device);
    vk_semaphore *s;
    const VkBaseInStructure *pn;
    (void)pAllocator;
    if (!dev || !pSemaphore) return VK_ERROR_INITIALIZATION_FAILED;
    s = (vk_semaphore *)vk_obj_alloc(sizeof(vk_semaphore), VK_OBJ_SEMAPHORE);
    if (!s) return VK_ERROR_OUT_OF_HOST_MEMORY;
    s->device = dev;
    s->sem_type = VK_SEMAPHORE_TYPE_BINARY;
    for (pn = (const VkBaseInStructure *)pCreateInfo->pNext; pn; pn = pn->pNext) {
        if (pn->sType == VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO) {
            const VkSemaphoreTypeCreateInfo *t = (const VkSemaphoreTypeCreateInfo *)pn;
            s->sem_type = t->semaphoreType;
            s->value = t->initialValue;
        }
    }
    *pSemaphore = VK_P2H(VkSemaphore, s);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(
    VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    vk_obj_free(VK_H2P(semaphore));
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreCounterValue(
    VkDevice device, VkSemaphore semaphore, uint64_t *pValue)
{
    vk_semaphore *s = (vk_semaphore *)VK_H2P(semaphore);
    (void)device;
    if (!s || !pValue) return VK_ERROR_DEVICE_LOST;
    if (s->sem_type != VK_SEMAPHORE_TYPE_TIMELINE) return VK_ERROR_VALIDATION_FAILED_EXT;
    *pValue = s->value;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkSignalSemaphore(
    VkDevice device, const VkSemaphoreSignalInfo *pSignalInfo)
{
    vk_semaphore *s = pSignalInfo ? (vk_semaphore *)VK_H2P(pSignalInfo->semaphore) : NULL;
    (void)device;
    if (!s) return VK_ERROR_DEVICE_LOST;
    s->value = pSignalInfo->value;
    s->signaled = VK_TRUE;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphores(
    VkDevice device, const VkSemaphoreWaitInfo *pWaitInfo, uint64_t timeout)
{
    uint32_t i;
    int all = 1, any = 0;
    VkBool32 wait_all;
    (void)device; (void)timeout;
    if (!pWaitInfo) return VK_ERROR_DEVICE_LOST;
    wait_all = (pWaitInfo->flags & VK_SEMAPHORE_WAIT_ANY_BIT) ? VK_FALSE : VK_TRUE;
    for (i = 0; i < pWaitInfo->semaphoreCount; i++) {
        vk_semaphore *s = (vk_semaphore *)VK_H2P(pWaitInfo->pSemaphores[i]);
        int done = (s && s->value >= pWaitInfo->pValues[i]);
        if (done) any = 1; else all = 0;
    }
    return (wait_all ? all : any) ? VK_SUCCESS : VK_TIMEOUT;
}

/* ---- event --------------------------------------------------------------- */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateEvent(
    VkDevice device, const VkEventCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkEvent *pEvent)
{
    vk_device *dev = (vk_device *)VK_H2P(device);
    vk_event *e;
    (void)pCreateInfo; (void)pAllocator;
    if (!dev || !pEvent) return VK_ERROR_INITIALIZATION_FAILED;
    e = (vk_event *)vk_obj_alloc(sizeof(vk_event), VK_OBJ_EVENT);
    if (!e) return VK_ERROR_OUT_OF_HOST_MEMORY;
    e->device = dev;
    e->set = VK_FALSE;
    *pEvent = VK_P2H(VkEvent, e);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyEvent(
    VkDevice device, VkEvent event, const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    vk_obj_free(VK_H2P(event));
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetEventStatus(VkDevice device, VkEvent event)
{
    vk_event *e = (vk_event *)VK_H2P(event);
    (void)device;
    if (!e) return VK_ERROR_DEVICE_LOST;
    return e->set ? VK_EVENT_SET : VK_EVENT_RESET;
}

VKAPI_ATTR VkResult VKAPI_CALL vkSetEvent(VkDevice device, VkEvent event)
{
    vk_event *e = (vk_event *)VK_H2P(event);
    (void)device;
    if (!e) return VK_ERROR_DEVICE_LOST;
    e->set = VK_TRUE;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetEvent(VkDevice device, VkEvent event)
{
    vk_event *e = (vk_event *)VK_H2P(event);
    (void)device;
    if (!e) return VK_ERROR_DEVICE_LOST;
    e->set = VK_FALSE;
    return VK_SUCCESS;
}

/* ---- query pool ---------------------------------------------------------- */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateQueryPool(
    VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkQueryPool *pQueryPool)
{
    vk_device *dev = (vk_device *)VK_H2P(device);
    vk_query_pool *q;
    (void)pAllocator;
    if (!dev || !pCreateInfo || !pQueryPool || pCreateInfo->queryCount == 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    q = (vk_query_pool *)vk_obj_alloc(sizeof(vk_query_pool), VK_OBJ_QUERY_POOL);
    if (!q) return VK_ERROR_OUT_OF_HOST_MEMORY;
    q->device = dev;
    q->qtype = pCreateInfo->queryType;
    q->count = pCreateInfo->queryCount;
    q->results = (uint64_t *)calloc(q->count, sizeof(uint64_t));
    if (!q->results) { vk_obj_free(q); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    *pQueryPool = VK_P2H(VkQueryPool, q);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyQueryPool(
    VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks *pAllocator)
{
    vk_query_pool *q = (vk_query_pool *)VK_H2P(queryPool);
    (void)device; (void)pAllocator;
    if (!q) return;
    free(q->results);
    vk_obj_free(q);
}

VKAPI_ATTR void VKAPI_CALL vkResetQueryPool(
    VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount)
{
    vk_query_pool *q = (vk_query_pool *)VK_H2P(queryPool);
    uint32_t i;
    (void)device;
    if (!q || !q->results) return;
    for (i = firstQuery; i < firstQuery + queryCount && i < q->count; i++)
        q->results[i] = 0;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(
    VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount,
    size_t dataSize, void *pData, VkDeviceSize stride, VkQueryResultFlags flags)
{
    vk_query_pool *q = (vk_query_pool *)VK_H2P(queryPool);
    uint32_t i;
    int is64 = (flags & VK_QUERY_RESULT_64_BIT) != 0;
    int avail = (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) != 0;
    (void)device; (void)dataSize;
    if (!q || !pData || !q->results) return VK_ERROR_INITIALIZATION_FAILED;
    for (i = 0; i < queryCount && (firstQuery + i) < q->count; i++) {
        uint8_t *out = (uint8_t *)pData + (size_t)i * stride;
        uint64_t v = q->results[firstQuery + i];
        if (is64) {
            *(uint64_t *)out = v;
            if (avail) *(uint64_t *)(out + 8) = 1;
        } else {
            *(uint32_t *)out = (uint32_t)v;
            if (avail) *(uint32_t *)(out + 4) = 1;
        }
    }
    return VK_SUCCESS;
}
