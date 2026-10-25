/*
 * device.c - VkDevice lifecycle, queue retrieval, and the queue submit/wait
 * path.  Device creation fixes the execution back-end for the whole device:
 * always the software CPU back-end, plus the GPU back-end on raspi5 when the
 * probe found usable silicon.  Queue submission runs recorded command buffers
 * immediately (in order) then signals the submit's semaphores and fence, which
 * is the CPU analog of OpenCL's in-order queue.
 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

static void init_queue(vk_queue *q, vk_device *dev, uint32_t family, uint32_t index)
{
    q->dispatch = &vk_global_dispatch;
    q->magic = VK_OBJ_MAGIC;
    q->type = VK_OBJ_QUEUE;
    q->device = dev;
    q->family = family;
    q->index = index;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDevice *pDevice)
{
    vk_physical_device *pd = (vk_physical_device *)VK_H2P(physicalDevice);
    vk_device *dev;
    uint32_t qi;

    (void)pAllocator;
    if (!pd || !pCreateInfo || !pDevice)
        return VK_ERROR_INITIALIZATION_FAILED;

    dev = (vk_device *)vk_obj_alloc_dispatch(sizeof(vk_device), VK_OBJ_DEVICE);
    if (!dev)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    dev->phys = pd;
    dev->instance = pd->instance;
    dev->backend = &vk_backend_cpu;
#ifdef VULKAN_GPU_BACKEND
    if (pd->gpu_available)
        dev->backend = &vk_backend_gpu;
#endif
    if (pCreateInfo->pEnabledFeatures)
        dev->features = *pCreateInfo->pEnabledFeatures;
    else
        dev->features = pd->features;

    /* create the requested queues */
    for (qi = 0; qi < pCreateInfo->queueCreateInfoCount; qi++) {
        const VkDeviceQueueCreateInfo *qci = &pCreateInfo->pQueueCreateInfos[qi];
        uint32_t fam = qci->queueFamilyIndex, n = qci->queueCount, k;
        if (fam >= VK_MAX_QUEUE_FAMILIES)
            continue;
        if (n > VK_MAX_QUEUES_PER_FAMILY)
            n = VK_MAX_QUEUES_PER_FAMILY;
        for (k = 0; k < n; k++)
            init_queue(&dev->queues[fam][k], dev, fam, k);
        dev->num_queues[fam] = n;
    }

    *pDevice = VK_P2H(VkDevice, dev);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(
    VkDevice device,
    const VkAllocationCallbacks *pAllocator)
{
    vk_device *dev = (vk_device *)VK_H2P(device);
    (void)pAllocator;
    if (!dev) return;
    vk_obj_free(dev);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(
    VkDevice device,
    uint32_t queueFamilyIndex,
    uint32_t queueIndex,
    VkQueue *pQueue)
{
    vk_device *dev = (vk_device *)VK_H2P(device);
    if (!dev || !pQueue) return;
    if (queueFamilyIndex >= VK_MAX_QUEUE_FAMILIES ||
        queueIndex >= dev->num_queues[queueFamilyIndex]) {
        *pQueue = VK_NULL_HANDLE;
        return;
    }
    *pQueue = VK_P2H(VkQueue, &dev->queues[queueFamilyIndex][queueIndex]);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue2(
    VkDevice device,
    const VkDeviceQueueInfo2 *pQueueInfo,
    VkQueue *pQueue)
{
    if (!pQueueInfo) { if (pQueue) *pQueue = VK_NULL_HANDLE; return; }
    vkGetDeviceQueue(device, pQueueInfo->queueFamilyIndex, pQueueInfo->queueIndex, pQueue);
}

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device)
{
    (void)device;
    return VK_SUCCESS;   /* immediate-mode: nothing is ever in flight */
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue queue)
{
    (void)queue;
    return VK_SUCCESS;
}

/* Execute one submit's command buffers, then signal its semaphores + fence. */
static VkResult run_submit(const VkSubmitInfo *si, VkFence fence)
{
    uint32_t i;
    for (i = 0; i < si->commandBufferCount; i++) {
        vk_command_buffer *cb = (vk_command_buffer *)VK_H2P(si->pCommandBuffers[i]);
        if (cb)
            vk_execute_command_buffer(cb);
    }
    /* signal wait/signal semaphores (binary: mark signaled) */
    if (si->pSignalSemaphores) {
        for (i = 0; i < si->signalSemaphoreCount; i++) {
            vk_semaphore *s = (vk_semaphore *)VK_H2P(si->pSignalSemaphores[i]);
            if (s) { s->signaled = VK_TRUE; s->value++; }
        }
    }
    if (fence != VK_NULL_HANDLE) {
        vk_fence *f = (vk_fence *)VK_H2P(fence);
        if (f) f->signaled = VK_TRUE;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(
    VkQueue queue,
    uint32_t submitCount,
    const VkSubmitInfo *pSubmits,
    VkFence fence)
{
    uint32_t i;
    (void)queue;
    if (!pSubmits && submitCount) return VK_ERROR_INITIALIZATION_FAILED;
    for (i = 0; i < submitCount; i++) {
        VkResult r = run_submit(&pSubmits[i], (i == submitCount - 1) ? fence : VK_NULL_HANDLE);
        if (r != VK_SUCCESS) return r;
    }
    if (submitCount == 0 && fence != VK_NULL_HANDLE) {
        vk_fence *f = (vk_fence *)VK_H2P(fence);
        if (f) f->signaled = VK_TRUE;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2(
    VkQueue queue,
    uint32_t submitCount,
    const VkSubmitInfo2 *pSubmits,
    VkFence fence)
{
    uint32_t i, j;
    (void)queue;
    if (!pSubmits && submitCount) return VK_ERROR_INITIALIZATION_FAILED;
    for (i = 0; i < submitCount; i++) {
        const VkSubmitInfo2 *si = &pSubmits[i];
        for (j = 0; j < si->commandBufferInfoCount; j++) {
            vk_command_buffer *cb =
                (vk_command_buffer *)VK_H2P(si->pCommandBufferInfos[j].commandBuffer);
            if (cb) vk_execute_command_buffer(cb);
        }
        for (j = 0; j < si->signalSemaphoreInfoCount; j++) {
            vk_semaphore *s =
                (vk_semaphore *)VK_H2P(si->pSignalSemaphoreInfos[j].semaphore);
            if (s) {
                s->signaled = VK_TRUE;
                s->value = si->pSignalSemaphoreInfos[j].value
                               ? si->pSignalSemaphoreInfos[j].value : s->value + 1;
            }
        }
        if (i == submitCount - 1 && fence != VK_NULL_HANDLE) {
            vk_fence *f = (vk_fence *)VK_H2P(fence);
            if (f) f->signaled = VK_TRUE;
        }
    }
    if (submitCount == 0 && fence != VK_NULL_HANDLE) {
        vk_fence *f = (vk_fence *)VK_H2P(fence);
        if (f) f->signaled = VK_TRUE;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueBindSparse(
    VkQueue queue,
    uint32_t bindInfoCount,
    const VkBindSparseInfo *pBindInfo,
    VkFence fence)
{
    (void)queue; (void)bindInfoCount; (void)pBindInfo;
    /* sparse residency unsupported: treat as a no-op submit that signals fence */
    if (fence != VK_NULL_HANDLE) {
        vk_fence *f = (vk_fence *)VK_H2P(fence);
        if (f) f->signaled = VK_TRUE;
    }
    return VK_SUCCESS;
}
