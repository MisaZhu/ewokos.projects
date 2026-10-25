/*
 * wsi.c - the presentation path: VkSurfaceKHR (an EwokOS window/g2d canvas
 * reached through a present callback the app registers, keeping libvulkan.a free
 * of any libx/libgraph dependency) and VK_KHR_swapchain.  A swapchain owns a few
 * host-resident color images; acquire hands out the next image index and present
 * pushes its pixels to the app's callback.
 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#ifdef VULKAN_GPU_BACKEND
#include <g2dclient/g2dclient.h>
#include <sys/shm.h>
#endif

static void release_sc_storage(vk_device_memory *mem)
{
#ifdef VULKAN_GPU_BACKEND
    if (mem->gfx_shm > 0) {
        g2d_shm_free((uint32_t *)mem->host);
        shmctl(mem->gfx_shm, IPC_RMID, NULL);
        return;
    }
#endif
    free(mem->host);
}

/* ---- surface ------------------------------------------------------------- */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateEwokOSSurfaceVK(
    VkInstance instance, uint32_t width, uint32_t height, VkFormat format,
    void *present_fn, void *user, VkSurfaceKHR *pSurface)
{
    vk_instance *inst = (vk_instance *)VK_H2P(instance);
    vk_surface *s;
    if (!pSurface) return VK_ERROR_INITIALIZATION_FAILED;
    s = (vk_surface *)vk_obj_alloc(sizeof(vk_surface), VK_OBJ_SURFACE);
    if (!s) return VK_ERROR_OUT_OF_HOST_MEMORY;
    s->instance = inst;
    s->width = width ? width : 1;
    s->height = height ? height : 1;
    s->format = format ? format : VK_FORMAT_B8G8R8A8_UNORM;
    s->present = (vk_ewokos_present_fn)present_fn;
    s->user = user;
    *pSurface = VK_P2H(VkSurfaceKHR, s);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(
    VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator)
{
    (void)instance; (void)pAllocator;
    vk_obj_free(VK_H2P(surface));
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface,
    VkBool32 *pSupported)
{
    (void)physicalDevice; (void)queueFamilyIndex; (void)surface;
    if (pSupported) *pSupported = VK_TRUE;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR *pSurfaceCapabilities)
{
    vk_surface *s = (vk_surface *)VK_H2P(surface);
    VkExtent2D ext;
    (void)physicalDevice;
    if (!pSurfaceCapabilities) return VK_ERROR_INITIALIZATION_FAILED;
    memset(pSurfaceCapabilities, 0, sizeof(*pSurfaceCapabilities));
    ext.width = s ? s->width : 1;
    ext.height = s ? s->height : 1;
    pSurfaceCapabilities->minImageCount = 1;
    pSurfaceCapabilities->maxImageCount = 4;
    pSurfaceCapabilities->currentExtent = ext;
    pSurfaceCapabilities->minImageExtent = ext;
    pSurfaceCapabilities->maxImageExtent = ext;
    pSurfaceCapabilities->maxImageArrayLayers = 1;
    pSurfaceCapabilities->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    pSurfaceCapabilities->supportedUsageFlags =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pSurfaceFormatCount,
    VkSurfaceFormatKHR *pSurfaceFormats)
{
    vk_surface *s = (vk_surface *)VK_H2P(surface);
    VkSurfaceFormatKHR fmt;
    (void)physicalDevice;
    fmt.format = s ? s->format : VK_FORMAT_B8G8R8A8_UNORM;
    fmt.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    if (!pSurfaceFormatCount) return VK_ERROR_INITIALIZATION_FAILED;
    return vk_enum_write(pSurfaceFormatCount, pSurfaceFormats ? *pSurfaceFormatCount : 0,
                         pSurfaceFormats, &fmt, 1, sizeof(fmt));
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pPresentModeCount,
    VkPresentModeKHR *pPresentModes)
{
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    (void)physicalDevice; (void)surface;
    if (!pPresentModeCount) return VK_ERROR_INITIALIZATION_FAILED;
    return vk_enum_write(pPresentModeCount, pPresentModes ? *pPresentModeCount : 0,
                         pPresentModes, &mode, 1, sizeof(mode));
}

/* ---- swapchain ----------------------------------------------------------- */
static int create_sc_image(vk_swapchain *sc, uint32_t i)
{
    vk_image *im = (vk_image *)vk_obj_alloc(sizeof(vk_image), VK_OBJ_IMAGE);
    vk_device_memory *mem;
    vk_image_view *view;
    VkDeviceSize size;
    if (!im) return -1;
    im->device = sc->device;
    im->image_type = VK_IMAGE_TYPE_2D;
    im->format = sc->format;
    im->extent.width = sc->width;
    im->extent.height = sc->height;
    im->extent.depth = 1;
    im->mip_levels = 1;
    im->array_layers = 1;
    im->samples = VK_SAMPLE_COUNT_1_BIT;
    im->tiling = VK_IMAGE_TILING_OPTIMAL;
    im->usage = sc->usage;
    im->layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    im->bpp = vk_format_bpp(sc->format);
    if (!im->bpp) im->bpp = 4;
    im->row_pitch = im->bpp * sc->width;
    size = (VkDeviceSize)im->row_pitch * sc->height;

    mem = (vk_device_memory *)vk_obj_alloc(sizeof(vk_device_memory), VK_OBJ_DEVICE_MEMORY);
    if (!mem) { vk_obj_free(im); return -1; }
    mem->device = sc->device;
    mem->size = size;
    mem->type_index = 0;
#ifdef VULKAN_GPU_BACKEND
    {
        uint32_t *px = NULL;
        if (g2d_shm_alloc((uint32_t)size, &mem->gfx_shm, &px) == 0 &&
            mem->gfx_shm > 0 && px != NULL) {
            mem->host = px;
            memset(mem->host, 0, (size_t)size);
        }
    }
#endif
    if (!mem->host) {
        mem->gfx_shm = 0;
        mem->host = calloc(1, (size_t)size);
    }
    if (!mem->host) { vk_obj_free(mem); vk_obj_free(im); return -1; }
    im->mem = mem;
    im->offset = 0;

    view = (vk_image_view *)vk_obj_alloc(sizeof(vk_image_view), VK_OBJ_IMAGE_VIEW);
    if (!view) { release_sc_storage(mem); vk_obj_free(mem); vk_obj_free(im); return -1; }
    view->image = im;
    view->view_type = VK_IMAGE_VIEW_TYPE_2D;
    view->format = sc->format;
    view->range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view->range.baseMipLevel = 0;
    view->range.levelCount = 1;
    view->range.baseArrayLayer = 0;
    view->range.layerCount = 1;

    sc->images[i] = im;
    sc->mems[i] = mem;
    sc->views[i] = view;
    return 0;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(
    VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain)
{
    vk_device *dev = (vk_device *)VK_H2P(device);
    vk_swapchain *sc;
    uint32_t i, count;
    (void)pAllocator;
    if (!dev || !pCreateInfo || !pSwapchain) return VK_ERROR_INITIALIZATION_FAILED;
    sc = (vk_swapchain *)vk_obj_alloc(sizeof(vk_swapchain), VK_OBJ_SWAPCHAIN);
    if (!sc) return VK_ERROR_OUT_OF_HOST_MEMORY;
    sc->device = dev;
    sc->surface = (vk_surface *)VK_H2P(pCreateInfo->surface);
    sc->width = pCreateInfo->imageExtent.width;
    sc->height = pCreateInfo->imageExtent.height;
    sc->format = pCreateInfo->imageFormat;
    sc->usage = pCreateInfo->imageUsage;
    count = pCreateInfo->minImageCount;
    if (count < 1) count = 1;
    if (count > 4) count = 4;
    sc->count = count;
    sc->current = 0;
    for (i = 0; i < count; i++) {
        if (create_sc_image(sc, i) != 0) {
            /* tear down what we made */
            uint32_t j;
            for (j = 0; j < i; j++) {
                vk_obj_free(sc->views[j]);
                release_sc_storage(sc->mems[j]); vk_obj_free(sc->mems[j]);
                vk_obj_free(sc->images[j]);
            }
            vk_obj_free(sc);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
    }
    *pSwapchain = VK_P2H(VkSwapchainKHR, sc);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(
    VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator)
{
    vk_swapchain *sc = (vk_swapchain *)VK_H2P(swapchain);
    uint32_t i;
    (void)device; (void)pAllocator;
    if (!sc) return;
    for (i = 0; i < sc->count; i++) {
        vk_obj_free(sc->views[i]);
        if (sc->mems[i]) { release_sc_storage(sc->mems[i]); vk_obj_free(sc->mems[i]); }
        vk_obj_free(sc->images[i]);
    }
    vk_obj_free(sc);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(
    VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages)
{
    vk_swapchain *sc = (vk_swapchain *)VK_H2P(swapchain);
    uint32_t i, n, avail;
    (void)device;
    if (!sc || !pSwapchainImageCount) return VK_ERROR_INITIALIZATION_FAILED;
    avail = *pSwapchainImageCount;
    if (!pSwapchainImages) { *pSwapchainImageCount = sc->count; return VK_SUCCESS; }
    n = sc->count < avail ? sc->count : avail;
    for (i = 0; i < n; i++)
        pSwapchainImages[i] = VK_P2H(VkImage, sc->images[i]);
    *pSwapchainImageCount = n;
    return (n < sc->count) ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(
    VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore,
    VkFence fence, uint32_t *pImageIndex)
{
    vk_swapchain *sc = (vk_swapchain *)VK_H2P(swapchain);
    (void)device; (void)timeout;
    if (!sc || !pImageIndex) return VK_ERROR_INITIALIZATION_FAILED;
    *pImageIndex = sc->current;
    sc->current = (sc->current + 1) % sc->count;
    if (semaphore != VK_NULL_HANDLE) {
        vk_semaphore *s = (vk_semaphore *)VK_H2P(semaphore);
        if (s) { s->signaled = VK_TRUE; s->value++; }
    }
    if (fence != VK_NULL_HANDLE) {
        vk_fence *f = (vk_fence *)VK_H2P(fence);
        if (f) f->signaled = VK_TRUE;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImage2KHR(
    VkDevice device, const VkAcquireNextImageInfoKHR *pAcquireInfo, uint32_t *pImageIndex)
{
    if (!pAcquireInfo) return VK_ERROR_INITIALIZATION_FAILED;
    return vkAcquireNextImageKHR(device, pAcquireInfo->swapchain, pAcquireInfo->timeout,
                                 pAcquireInfo->semaphore, pAcquireInfo->fence, pImageIndex);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(
    VkQueue queue, const VkPresentInfoKHR *pPresentInfo)
{
    uint32_t i;
    (void)queue;
    if (!pPresentInfo) return VK_ERROR_INITIALIZATION_FAILED;
    for (i = 0; i < pPresentInfo->swapchainCount; i++) {
        vk_swapchain *sc = (vk_swapchain *)VK_H2P(pPresentInfo->pSwapchains[i]);
        uint32_t idx = pPresentInfo->pImageIndices[i];
        vk_surface *surf;
        if (!sc || idx >= sc->count) continue;
        surf = sc->surface;
        if (surf && surf->present && sc->images[idx]) {
            vk_image *im = sc->images[idx];
            surf->present(surf->user, vk_image_ptr(im), sc->width, sc->height,
                          im->row_pitch, sc->format,
                          (im->mem && im->mem->gfx_shm > 0) ? im->mem->gfx_shm : 0);
        }
        if (pPresentInfo->pResults)
            pPresentInfo->pResults[i] = VK_SUCCESS;
    }
    return VK_SUCCESS;
}

/* ---- device-group present (single device: local only) -------------------- */
VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupPresentCapabilitiesKHR(
    VkDevice device, VkDeviceGroupPresentCapabilitiesKHR *pDeviceGroupPresentCapabilities)
{
    (void)device;
    if (!pDeviceGroupPresentCapabilities) return VK_ERROR_INITIALIZATION_FAILED;
    memset(pDeviceGroupPresentCapabilities, 0, sizeof(*pDeviceGroupPresentCapabilities));
    pDeviceGroupPresentCapabilities->presentMask[0] = 1;
    pDeviceGroupPresentCapabilities->modes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupSurfacePresentModesKHR(
    VkDevice device, VkSurfaceKHR surface,
    VkDeviceGroupPresentModeFlagsKHR *pModes)
{
    (void)device; (void)surface;
    if (pModes) *pModes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDevicePresentRectanglesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pRectCount, VkRect2D *pRects)
{
    vk_surface *s = (vk_surface *)VK_H2P(surface);
    VkRect2D r;
    (void)physicalDevice;
    if (!pRectCount) return VK_ERROR_INITIALIZATION_FAILED;
    r.offset.x = 0; r.offset.y = 0;
    r.extent.width = s ? s->width : 0;
    r.extent.height = s ? s->height : 0;
    return vk_enum_write(pRectCount, pRects ? *pRectCount : 0, pRects, &r, 1, sizeof(r));
}
