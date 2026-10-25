/*
 * memory.c - VkDeviceMemory lifecycle and mapping, buffer/image memory binding,
 * memory-requirement queries, and the buffer-device-address / opaque-capture
 * surface.  In the software model every allocation is a plain host buffer that
 * is simultaneously device-local and host-visible, so map/bind/flush are direct
 * host operations and a device address is simply the host pointer.
 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* ---- device memory ------------------------------------------------------- */
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(
    VkDevice device,
    const VkMemoryAllocateInfo *pAllocateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDeviceMemory *pMemory)
{
    vk_device *dev = (vk_device *)VK_H2P(device);
    vk_device_memory *m;
    (void)pAllocator;
    if (!dev || !pAllocateInfo || !pMemory || pAllocateInfo->allocationSize == 0)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    m = (vk_device_memory *)vk_obj_alloc(sizeof(vk_device_memory), VK_OBJ_DEVICE_MEMORY);
    if (!m) return VK_ERROR_OUT_OF_HOST_MEMORY;
    m->device = dev;
    m->size = pAllocateInfo->allocationSize;
    m->type_index = pAllocateInfo->memoryTypeIndex;
    m->host = calloc(1, (size_t)pAllocateInfo->allocationSize);
    if (!m->host) { vk_obj_free(m); return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
    *pMemory = VK_P2H(VkDeviceMemory, m);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkFreeMemory(
    VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *pAllocator)
{
    vk_device_memory *m = (vk_device_memory *)VK_H2P(memory);
    (void)device; (void)pAllocator;
    if (!m) return;
    free(m->host);
    vk_obj_free(m);
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(
    VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size,
    VkMemoryMapFlags flags, void **ppData)
{
    vk_device_memory *m = (vk_device_memory *)VK_H2P(memory);
    (void)device; (void)flags;
    if (!m || !ppData) return VK_ERROR_INITIALIZATION_FAILED;
    if (offset >= m->size) return VK_ERROR_MEMORY_MAP_FAILED;
    if (size == VK_WHOLE_SIZE || offset + size > m->size) size = m->size - offset;
    m->mapped = (char *)m->host + offset;
    m->map_offset = offset;
    m->map_size = size;
    *ppData = m->mapped;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice device, VkDeviceMemory memory)
{
    vk_device_memory *m = (vk_device_memory *)VK_H2P(memory);
    (void)device;
    if (m) { m->mapped = NULL; m->map_size = 0; }
}

VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(
    VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange *pMemoryRanges)
{
    (void)device; (void)memoryRangeCount; (void)pMemoryRanges;
    return VK_SUCCESS;   /* host-coherent: nothing to flush */
}

VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(
    VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange *pMemoryRanges)
{
    (void)device; (void)memoryRangeCount; (void)pMemoryRanges;
    return VK_SUCCESS;   /* host-coherent: nothing to invalidate */
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceMemoryCommitment(
    VkDevice device, VkDeviceMemory memory, VkDeviceSize *pCommittedMemoryInBytes)
{
    vk_device_memory *m = (vk_device_memory *)VK_H2P(memory);
    (void)device;
    if (pCommittedMemoryInBytes) *pCommittedMemoryInBytes = m ? m->size : 0;
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetDeviceMemoryOpaqueCaptureAddress(
    VkDevice device, const VkDeviceMemoryOpaqueCaptureAddressInfo *pInfo)
{
    vk_device_memory *m = pInfo ? (vk_device_memory *)VK_H2P(pInfo->memory) : NULL;
    (void)device;
    return (VkDeviceAddress)(uintptr_t)(m ? m->host : NULL);
}

/* ---- memory requirements ------------------------------------------------- */
static void fill_mem_req(VkMemoryRequirements *r, VkDeviceSize size)
{
    r->size = (size + 15) & ~(VkDeviceSize)15;
    r->alignment = 16;
    r->memoryTypeBits = 0x3;   /* both advertised types are compatible */
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(
    VkDevice device, VkBuffer buffer, VkMemoryRequirements *pMemoryRequirements)
{
    vk_buffer *b = (vk_buffer *)VK_H2P(buffer);
    (void)device;
    if (!pMemoryRequirements) return;
    fill_mem_req(pMemoryRequirements, b ? b->size : 0);
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2(
    VkDevice device, const VkBufferMemoryRequirementsInfo2 *pInfo,
    VkMemoryRequirements2 *pMemoryRequirements)
{
    (void)device;
    if (!pInfo || !pMemoryRequirements) return;
    vkGetBufferMemoryRequirements(device, pInfo->buffer, &pMemoryRequirements->memoryRequirements);
}

static VkDeviceSize image_size(const vk_image *im)
{
    VkDeviceSize s;
    uint32_t bpp = im->bpp ? im->bpp : vk_format_bpp(im->format);
    if (!bpp) return 0;
    s = (VkDeviceSize)bpp * im->extent.width * im->extent.height * im->extent.depth;
    s *= im->array_layers ? im->array_layers : 1;
    s *= im->mip_levels ? im->mip_levels : 1;
    return s;
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(
    VkDevice device, VkImage image, VkMemoryRequirements *pMemoryRequirements)
{
    vk_image *im = (vk_image *)VK_H2P(image);
    (void)device;
    if (!pMemoryRequirements) return;
    fill_mem_req(pMemoryRequirements, im ? image_size(im) : 0);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2(
    VkDevice device, const VkImageMemoryRequirementsInfo2 *pInfo,
    VkMemoryRequirements2 *pMemoryRequirements)
{
    (void)device;
    if (!pInfo || !pMemoryRequirements) return;
    vkGetImageMemoryRequirements(device, pInfo->image, &pMemoryRequirements->memoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements(
    VkDevice device, VkImage image, uint32_t *pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements *pSparseMemoryRequirements)
{
    (void)device; (void)image; (void)pSparseMemoryRequirements;
    if (pSparseMemoryRequirementCount) *pSparseMemoryRequirementCount = 0;
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements2(
    VkDevice device, const VkImageSparseMemoryRequirementsInfo2 *pInfo,
    uint32_t *pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
    (void)device; (void)pInfo; (void)pSparseMemoryRequirements;
    if (pSparseMemoryRequirementCount) *pSparseMemoryRequirementCount = 0;
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceBufferMemoryRequirements(
    VkDevice device, const VkDeviceBufferMemoryRequirements *pInfo,
    VkMemoryRequirements2 *pMemoryRequirements)
{
    (void)device;
    if (!pInfo || !pInfo->pCreateInfo || !pMemoryRequirements) return;
    fill_mem_req(&pMemoryRequirements->memoryRequirements, pInfo->pCreateInfo->size);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageMemoryRequirements(
    VkDevice device, const VkDeviceImageMemoryRequirements *pInfo,
    VkMemoryRequirements2 *pMemoryRequirements)
{
    const VkImageCreateInfo *ci;
    vk_image tmp;
    (void)device;
    if (!pInfo || !(ci = pInfo->pCreateInfo) || !pMemoryRequirements) return;
    memset(&tmp, 0, sizeof(tmp));
    tmp.format = ci->format;
    tmp.extent = ci->extent;
    tmp.mip_levels = ci->mipLevels ? ci->mipLevels : 1;
    tmp.array_layers = ci->arrayLayers ? ci->arrayLayers : 1;
    tmp.bpp = vk_format_bpp(ci->format);
    fill_mem_req(&pMemoryRequirements->memoryRequirements, image_size(&tmp));
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSparseMemoryRequirements(
    VkDevice device, const VkDeviceImageMemoryRequirements *pInfo,
    uint32_t *pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
    (void)device; (void)pInfo; (void)pSparseMemoryRequirements;
    if (pSparseMemoryRequirementCount) *pSparseMemoryRequirementCount = 0;
}

/* ---- binding ------------------------------------------------------------- */
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(
    VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{
    vk_buffer *b = (vk_buffer *)VK_H2P(buffer);
    vk_device_memory *m = (vk_device_memory *)VK_H2P(memory);
    (void)device;
    if (!b || !m) return VK_ERROR_INITIALIZATION_FAILED;
    b->mem = m;
    b->offset = memoryOffset;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(
    VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{
    vk_image *im = (vk_image *)VK_H2P(image);
    vk_device_memory *m = (vk_device_memory *)VK_H2P(memory);
    (void)device;
    if (!im || !m) return VK_ERROR_INITIALIZATION_FAILED;
    im->mem = m;
    im->offset = memoryOffset;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2(
    VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo *pBindInfos)
{
    uint32_t i;
    for (i = 0; i < bindInfoCount; i++) {
        VkResult r = vkBindBufferMemory(device, pBindInfos[i].buffer,
                                        pBindInfos[i].memory, pBindInfos[i].memoryOffset);
        if (r != VK_SUCCESS) return r;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2(
    VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo *pBindInfos)
{
    uint32_t i;
    for (i = 0; i < bindInfoCount; i++) {
        VkResult r = vkBindImageMemory(device, pBindInfos[i].image,
                                       pBindInfos[i].memory, pBindInfos[i].memoryOffset);
        if (r != VK_SUCCESS) return r;
    }
    return VK_SUCCESS;
}

/* ---- buffer device address ----------------------------------------------- */
VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddress(
    VkDevice device, const VkBufferDeviceAddressInfo *pInfo)
{
    vk_buffer *b = pInfo ? (vk_buffer *)VK_H2P(pInfo->buffer) : NULL;
    (void)device;
    if (!b || !b->mem || !b->mem->host) return 0;
    return (VkDeviceAddress)(uintptr_t)((char *)b->mem->host + b->offset);
}

VKAPI_ATTR uint64_t VKAPI_CALL vkGetBufferOpaqueCaptureAddress(
    VkDevice device, const VkBufferDeviceAddressInfo *pInfo)
{
    (void)device;
    return (uint64_t)vkGetBufferDeviceAddress(device, pInfo);
}
