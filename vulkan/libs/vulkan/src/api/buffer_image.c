/*
 * buffer_image.c - VkBuffer / VkBufferView / VkImage / VkImageView / VkSampler
 * (+ YCbCr conversion) creation and destruction, and image subresource layout
 * queries.  These objects are thin descriptors over the host-resident memory
 * bound to them; image geometry (bpp, row pitch) is resolved here so the CPU
 * back-end and the transfer ops can index pixels directly.
 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* ---- buffer -------------------------------------------------------------- */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(
    VkDevice device, const VkBufferCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer)
{
    vk_device *dev = (vk_device *)VK_H2P(device);
    vk_buffer *b;
    (void)pAllocator;
    if (!dev || !pCreateInfo || !pBuffer) return VK_ERROR_INITIALIZATION_FAILED;
    b = (vk_buffer *)vk_obj_alloc(sizeof(vk_buffer), VK_OBJ_BUFFER);
    if (!b) return VK_ERROR_OUT_OF_HOST_MEMORY;
    b->device = dev;
    b->size = pCreateInfo->size;
    b->usage = pCreateInfo->usage;
    *pBuffer = VK_P2H(VkBuffer, b);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(
    VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    vk_obj_free(VK_H2P(buffer));
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBufferView(
    VkDevice device, const VkBufferViewCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkBufferView *pView)
{
    vk_buffer_view *v;
    (void)device; (void)pAllocator;
    if (!pCreateInfo || !pView) return VK_ERROR_INITIALIZATION_FAILED;
    v = (vk_buffer_view *)vk_obj_alloc(sizeof(vk_buffer_view), VK_OBJ_BUFFER_VIEW);
    if (!v) return VK_ERROR_OUT_OF_HOST_MEMORY;
    v->buffer = (vk_buffer *)VK_H2P(pCreateInfo->buffer);
    v->format = pCreateInfo->format;
    v->offset = pCreateInfo->offset;
    v->range = pCreateInfo->range;
    *pView = VK_P2H(VkBufferView, v);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBufferView(
    VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    vk_obj_free(VK_H2P(bufferView));
}

/* ---- image --------------------------------------------------------------- */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(
    VkDevice device, const VkImageCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkImage *pImage)
{
    vk_device *dev = (vk_device *)VK_H2P(device);
    vk_image *im;
    (void)pAllocator;
    if (!dev || !pCreateInfo || !pImage) return VK_ERROR_INITIALIZATION_FAILED;
    im = (vk_image *)vk_obj_alloc(sizeof(vk_image), VK_OBJ_IMAGE);
    if (!im) return VK_ERROR_OUT_OF_HOST_MEMORY;
    im->device = dev;
    im->image_type = pCreateInfo->imageType;
    im->format = pCreateInfo->format;
    im->extent = pCreateInfo->extent;
    im->mip_levels = pCreateInfo->mipLevels ? pCreateInfo->mipLevels : 1;
    im->array_layers = pCreateInfo->arrayLayers ? pCreateInfo->arrayLayers : 1;
    im->samples = pCreateInfo->samples;
    im->tiling = pCreateInfo->tiling;
    im->usage = pCreateInfo->usage;
    im->layout = pCreateInfo->initialLayout;
    im->bpp = vk_format_bpp(pCreateInfo->format);
    im->row_pitch = im->bpp * im->extent.width;
    *pImage = VK_P2H(VkImage, im);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(
    VkDevice device, VkImage image, const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    vk_obj_free(VK_H2P(image));
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(
    VkDevice device, const VkImageViewCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkImageView *pView)
{
    vk_image_view *v;
    (void)device; (void)pAllocator;
    if (!pCreateInfo || !pView) return VK_ERROR_INITIALIZATION_FAILED;
    v = (vk_image_view *)vk_obj_alloc(sizeof(vk_image_view), VK_OBJ_IMAGE_VIEW);
    if (!v) return VK_ERROR_OUT_OF_HOST_MEMORY;
    v->image = (vk_image *)VK_H2P(pCreateInfo->image);
    v->view_type = pCreateInfo->viewType;
    v->format = pCreateInfo->format;
    v->range = pCreateInfo->subresourceRange;
    *pView = VK_P2H(VkImageView, v);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(
    VkDevice device, VkImageView imageView, const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    vk_obj_free(VK_H2P(imageView));
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout(
    VkDevice device, VkImage image, const VkImageSubresource *pSubresource,
    VkSubresourceLayout *pLayout)
{
    vk_image *im = (vk_image *)VK_H2P(image);
    (void)device; (void)pSubresource;
    if (!im || !pLayout) return;
    memset(pLayout, 0, sizeof(*pLayout));
    pLayout->offset = 0;
    pLayout->rowPitch = im->row_pitch;
    pLayout->arrayPitch = (VkDeviceSize)im->row_pitch * im->extent.height;
    pLayout->depthPitch = (VkDeviceSize)im->row_pitch * im->extent.height;
    pLayout->size = pLayout->arrayPitch * (im->array_layers ? im->array_layers : 1);
}

/* ---- sampler ------------------------------------------------------------- */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(
    VkDevice device, const VkSamplerCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSampler *pSampler)
{
    vk_sampler *s;
    (void)device; (void)pAllocator;
    if (!pCreateInfo || !pSampler) return VK_ERROR_INITIALIZATION_FAILED;
    s = (vk_sampler *)vk_obj_alloc(sizeof(vk_sampler), VK_OBJ_SAMPLER);
    if (!s) return VK_ERROR_OUT_OF_HOST_MEMORY;
    s->mag = pCreateInfo->magFilter;
    s->min_f = pCreateInfo->minFilter;
    s->addr_u = pCreateInfo->addressModeU;
    s->addr_v = pCreateInfo->addressModeV;
    s->addr_w = pCreateInfo->addressModeW;
    s->mip_bias = pCreateInfo->mipLodBias;
    s->aniso_enable = pCreateInfo->anisotropyEnable;
    s->max_aniso = pCreateInfo->maxAnisotropy;
    s->border = pCreateInfo->borderColor;
    s->unnormalized = pCreateInfo->unnormalizedCoordinates;
    s->compare = pCreateInfo->compareEnable ? pCreateInfo->compareOp : VK_COMPARE_OP_ALWAYS;
    *pSampler = VK_P2H(VkSampler, s);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySampler(
    VkDevice device, VkSampler sampler, const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    vk_obj_free(VK_H2P(sampler));
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSamplerYcbcrConversion(
    VkDevice device, const VkSamplerYcbcrConversionCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSamplerYcbcrConversion *pYcbcrConversion)
{
    vk_sampler_ycbcr_conversion *c;
    (void)device; (void)pCreateInfo; (void)pAllocator;
    if (!pYcbcrConversion) return VK_ERROR_INITIALIZATION_FAILED;
    c = (vk_sampler_ycbcr_conversion *)
        vk_obj_alloc(sizeof(vk_sampler_ycbcr_conversion), VK_OBJ_SAMPLER_YCBCR_CONVERSION);
    if (!c) return VK_ERROR_OUT_OF_HOST_MEMORY;
    *pYcbcrConversion = VK_P2H(VkSamplerYcbcrConversion, c);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySamplerYcbcrConversion(
    VkDevice device, VkSamplerYcbcrConversion ycbcrConversion,
    const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    vk_obj_free(VK_H2P(ycbcrConversion));
}
