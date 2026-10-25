/*
 * instance.c - VkInstance lifecycle, physical-device enumeration and every
 * vkGetPhysicalDevice* query, plus instance/device extension+layer enumeration
 * and vkEnumerateInstanceVersion.  There is exactly one instance and one
 * physical device (the EwokOS software device); the advertised profile is a
 * coherent Vulkan 1.3 surface identical on every machine, whether or not a GPU
 * back-end is present.
 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- pNext chain walking ------------------------------------------------- */
static void *pnext_find(const void *pNext, VkStructureType st)
{
    const VkBaseOutStructure *p = (const VkBaseOutStructure *)pNext;
    while (p) {
        if (p->sType == st)
            return (void *)p;
        p = p->pNext;
    }
    return NULL;
}

/* ---- instance lifecycle -------------------------------------------------- */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkInstance *pInstance)
{
    vk_instance *inst = vk_runtime_instance();
    (void)pAllocator;
    if (!pInstance)
        return VK_ERROR_INITIALIZATION_FAILED;
    inst->dispatch = &vk_global_dispatch;
    inst->magic = VK_OBJ_MAGIC;
    inst->type = VK_OBJ_INSTANCE;
    memset(inst->app_name, 0, sizeof(inst->app_name));
    memset(inst->engine_name, 0, sizeof(inst->engine_name));
    if (pCreateInfo && pCreateInfo->pApplicationInfo) {
        const VkApplicationInfo *ai = pCreateInfo->pApplicationInfo;
        inst->app_info = *ai;
        if (ai->pApplicationName)
            snprintf(inst->app_name, sizeof(inst->app_name), "%s", ai->pApplicationName);
        if (ai->pEngineName)
            snprintf(inst->engine_name, sizeof(inst->engine_name), "%s", ai->pEngineName);
        inst->api_version = ai->apiVersion;
    }
    inst->phys[0] = vk_runtime_physical();
    inst->num_phys = 1;
    *pInstance = VK_P2H(VkInstance, inst);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(
    VkInstance instance,
    const VkAllocationCallbacks *pAllocator)
{
    (void)instance; (void)pAllocator;
    /* the singleton instance is static; nothing to free */
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance instance,
    uint32_t *pPhysicalDeviceCount,
    VkPhysicalDevice *pPhysicalDevices)
{
    VkPhysicalDevice one;
    (void)instance;
    if (!pPhysicalDeviceCount)
        return VK_ERROR_INITIALIZATION_FAILED;
    one = VK_P2H(VkPhysicalDevice, vk_runtime_physical());
    return vk_enum_write(pPhysicalDeviceCount, *pPhysicalDeviceCount,
                         pPhysicalDevices, &one, 1, sizeof(one));
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceGroups(
    VkInstance instance,
    uint32_t *pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupProperties *pPhysicalDeviceGroupProperties)
{
    VkPhysicalDeviceGroupProperties grp;
    (void)instance;
    memset(&grp, 0, sizeof(grp));
    grp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES;
    grp.physicalDeviceCount = 1;
    grp.physicalDevices[0] = VK_P2H(VkPhysicalDevice, vk_runtime_physical());
    grp.subsetAllocation = VK_FALSE;
    if (!pPhysicalDeviceGroupCount)
        return VK_ERROR_INITIALIZATION_FAILED;
    return vk_enum_write(pPhysicalDeviceGroupCount, *pPhysicalDeviceGroupCount,
                         pPhysicalDeviceGroupProperties, &grp, 1, sizeof(grp));
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t *pApiVersion)
{
    if (!pApiVersion)
        return VK_ERROR_INITIALIZATION_FAILED;
    *pApiVersion = VK_MAKE_API_VERSION(0, 1, 3, VK_HEADER_VERSION);
    return VK_SUCCESS;
}

/* ---- extension / layer enumeration --------------------------------------- */
/* The software device exposes the WSI extensions required to present plus the
 * portability-enumeration surface; no layers. */
static const VkExtensionProperties g_instance_exts[] = {
    { VK_KHR_SURFACE_EXTENSION_NAME, 25 },
    { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, 2 },
};
static const VkExtensionProperties g_device_exts[] = {
    { VK_KHR_SWAPCHAIN_EXTENSION_NAME, 70 },
};

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char *pLayerName,
    uint32_t *pPropertyCount,
    VkExtensionProperties *pProperties)
{
    (void)pLayerName;
    if (pLayerName) {   /* no layers expose extensions */
        if (pPropertyCount) *pPropertyCount = 0;
        return VK_SUCCESS;
    }
    return vk_enum_write(pPropertyCount, pProperties ? *pPropertyCount : 0,
                         pProperties, g_instance_exts,
                         (uint32_t)(sizeof(g_instance_exts)/sizeof(g_instance_exts[0])),
                         sizeof(VkExtensionProperties));
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice,
    const char *pLayerName,
    uint32_t *pPropertyCount,
    VkExtensionProperties *pProperties)
{
    (void)physicalDevice;
    if (pLayerName) {
        if (pPropertyCount) *pPropertyCount = 0;
        return VK_SUCCESS;
    }
    return vk_enum_write(pPropertyCount, pProperties ? *pPropertyCount : 0,
                         pProperties, g_device_exts,
                         (uint32_t)(sizeof(g_device_exts)/sizeof(g_device_exts[0])),
                         sizeof(VkExtensionProperties));
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t *pPropertyCount,
    VkLayerProperties *pProperties)
{
    return vk_enum_write(pPropertyCount, pProperties ? *pPropertyCount : 0,
                         pProperties, NULL, 0, sizeof(VkLayerProperties));
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t *pPropertyCount,
    VkLayerProperties *pProperties)
{
    (void)physicalDevice;
    return vk_enum_write(pPropertyCount, pProperties ? *pPropertyCount : 0,
                         pProperties, NULL, 0, sizeof(VkLayerProperties));
}

/* ---- physical device queries --------------------------------------------- */
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties *pProperties)
{
    vk_physical_device *pd = (vk_physical_device *)VK_H2P(physicalDevice);
    if (pd && pProperties) *pProperties = pd->props;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties2 *pProperties)
{
    vk_physical_device *pd = (vk_physical_device *)VK_H2P(physicalDevice);
    if (!pd || !pProperties) return;
    pProperties->properties = pd->props;
    /* no chained feature structs required; leave pNext untouched */
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures *pFeatures)
{
    vk_physical_device *pd = (vk_physical_device *)VK_H2P(physicalDevice);
    if (pd && pFeatures) *pFeatures = pd->features;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures2 *pFeatures)
{
    vk_physical_device *pd = (vk_physical_device *)VK_H2P(physicalDevice);
    VkPhysicalDeviceVulkan11Features *f11;
    VkPhysicalDeviceVulkan12Features *f12;
    VkPhysicalDeviceVulkan13Features *f13;
    if (!pd || !pFeatures) return;
    pFeatures->features = pd->features;
    f11 = (VkPhysicalDeviceVulkan11Features *)
        pnext_find(pFeatures->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
    if (f11) {
        f11->multiview = VK_FALSE;
        f11->samplerYcbcrConversion = VK_FALSE;
    }
    f12 = (VkPhysicalDeviceVulkan12Features *)
        pnext_find(pFeatures->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    if (f12) {
        f12->timelineSemaphore = VK_TRUE;
        f12->bufferDeviceAddress = VK_TRUE;
        f12->descriptorIndexing = VK_FALSE;
        f12->shaderOutputLayer = VK_FALSE;
    }
    f13 = (VkPhysicalDeviceVulkan13Features *)
        pnext_find(pFeatures->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
    if (f13) {
        f13->dynamicRendering = VK_TRUE;
        f13->synchronization2 = VK_TRUE;
        f13->maintenance4 = VK_TRUE;
        f13->inlineUniformBlock = VK_FALSE;
        f13->descriptorBindingInlineUniformBlockUpdateAfterBind = VK_FALSE;
    }
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties *pMemoryProperties)
{
    vk_physical_device *pd = (vk_physical_device *)VK_H2P(physicalDevice);
    if (pd && pMemoryProperties) *pMemoryProperties = pd->mem;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
    vk_physical_device *pd = (vk_physical_device *)VK_H2P(physicalDevice);
    if (pd && pMemoryProperties) pMemoryProperties->memoryProperties = pd->mem;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties *pQueueFamilyProperties)
{
    vk_physical_device *pd = (vk_physical_device *)VK_H2P(physicalDevice);
    if (!pd || !pQueueFamilyPropertyCount) return;
    vk_write_out(*pQueueFamilyPropertyCount, pQueueFamilyProperties,
                 pQueueFamilyPropertyCount, pd->qfam, pd->num_qfam,
                 sizeof(VkQueueFamilyProperties));
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice physicalDevice,
    uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
    vk_physical_device *pd = (vk_physical_device *)VK_H2P(physicalDevice);
    uint32_t i, n;
    if (!pd || !pQueueFamilyPropertyCount) return;
    n = *pQueueFamilyPropertyCount;
    if (!pQueueFamilyProperties) { *pQueueFamilyPropertyCount = pd->num_qfam; return; }
    if (n > pd->num_qfam) n = pd->num_qfam;
    for (i = 0; i < n; i++)
        pQueueFamilyProperties[i].queueFamilyProperties = pd->qfam[i];
    *pQueueFamilyPropertyCount = n;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties *pFormatProperties)
{
    (void)physicalDevice;
    memset(pFormatProperties, 0, sizeof(*pFormatProperties));
    if (vk_format_bpp(format) == 0)
        return;   /* unsupported format: no features */
    pFormatProperties->linearTilingFeatures =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    pFormatProperties->optimalTilingFeatures = pFormatProperties->linearTilingFeatures;
    pFormatProperties->bufferFeatures =
        VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT | VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties2(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties2 *pFormatProperties)
{
    if (!pFormatProperties) return;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format,
                                        &pFormatProperties->formatProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags,
    VkImageFormatProperties *pImageFormatProperties)
{
    (void)physicalDevice; (void)type; (void)tiling; (void)usage; (void)flags;
    if (!pImageFormatProperties || vk_format_bpp(format) == 0)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    memset(pImageFormatProperties, 0, sizeof(*pImageFormatProperties));
    pImageFormatProperties->maxExtent.width = 4096;
    pImageFormatProperties->maxExtent.height = 4096;
    pImageFormatProperties->maxExtent.depth = 512;
    pImageFormatProperties->maxMipLevels = 12;
    pImageFormatProperties->maxArrayLayers = 256;
    pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
    pImageFormatProperties->maxResourceSize = 1u << 30;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
    VkImageFormatProperties2 *pImageFormatProperties)
{
    if (!pImageFormatInfo || !pImageFormatProperties)
        return VK_ERROR_INITIALIZATION_FAILED;
    return vkGetPhysicalDeviceImageFormatProperties(
        physicalDevice, pImageFormatInfo->format, pImageFormatInfo->type,
        pImageFormatInfo->tiling, pImageFormatInfo->usage, pImageFormatInfo->flags,
        &pImageFormatProperties->imageFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format, VkImageType type, VkSampleCountFlagBits samples,
    VkImageUsageFlags usage, VkImageTiling tiling,
    uint32_t *pPropertyCount,
    VkSparseImageFormatProperties *pProperties)
{
    (void)physicalDevice; (void)format; (void)type; (void)samples;
    (void)usage; (void)tiling; (void)pProperties;
    if (pPropertyCount) *pPropertyCount = 0;   /* no sparse residency support */
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties2(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo,
    uint32_t *pPropertyCount,
    VkSparseImageFormatProperties2 *pProperties)
{
    (void)physicalDevice; (void)pFormatInfo; (void)pProperties;
    if (pPropertyCount) *pPropertyCount = 0;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalBufferProperties(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
    VkExternalBufferProperties *pExternalBufferProperties)
{
    (void)physicalDevice; (void)pExternalBufferInfo;
    memset(pExternalBufferProperties, 0, sizeof(*pExternalBufferProperties));
    /* no external (cross-process) buffer sharing on the software device */
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalFenceProperties(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceExternalFenceInfo *pExternalFenceInfo,
    VkExternalFenceProperties *pExternalFenceProperties)
{
    (void)physicalDevice; (void)pExternalFenceInfo;
    memset(pExternalFenceProperties, 0, sizeof(*pExternalFenceProperties));
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalSemaphoreProperties(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceExternalSemaphoreInfo *pExternalSemaphoreInfo,
    VkExternalSemaphoreProperties *pExternalSemaphoreProperties)
{
    (void)physicalDevice; (void)pExternalSemaphoreInfo;
    memset(pExternalSemaphoreProperties, 0, sizeof(*pExternalSemaphoreProperties));
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceToolProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t *pToolCount,
    VkPhysicalDeviceToolProperties *pToolProperties)
{
    (void)physicalDevice; (void)pToolProperties;
    if (!pToolCount) return VK_ERROR_INITIALIZATION_FAILED;
    *pToolCount = 0;   /* no active layer tools */
    return VK_SUCCESS;
}
