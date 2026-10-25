/*
 * misc.c - the remaining core surface: private-data slots (a small global
 * handle->value registry) and device-group peer memory features (none, single
 * device).  These complete the 215-function Vulkan 1.3 core list.
 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* ---- private data -------------------------------------------------------- */
#define VK_PRIV_SLOTS 64
static struct {
    int        used;
    uint64_t   handle;
    VkPrivateDataSlot slot;
    uint64_t   value;
} g_priv[VK_PRIV_SLOTS];

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePrivateDataSlot(
    VkDevice device, const VkPrivateDataSlotCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkPrivateDataSlot *pPrivateDataSlot)
{
    vk_private_data_slot *s;
    (void)device; (void)pCreateInfo; (void)pAllocator;
    if (!pPrivateDataSlot) return VK_ERROR_INITIALIZATION_FAILED;
    s = (vk_private_data_slot *)
        vk_obj_alloc(sizeof(vk_private_data_slot), VK_OBJ_PRIVATE_DATA_SLOT);
    if (!s) return VK_ERROR_OUT_OF_HOST_MEMORY;
    *pPrivateDataSlot = VK_P2H(VkPrivateDataSlot, s);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPrivateDataSlot(
    VkDevice device, VkPrivateDataSlot privateDataSlot,
    const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    vk_obj_free(VK_H2P(privateDataSlot));
}

VKAPI_ATTR VkResult VKAPI_CALL vkSetPrivateData(
    VkDevice device, VkObjectType objectType, uint64_t objectHandle,
    VkPrivateDataSlot privateDataSlot, uint64_t data)
{
    int i, free_slot = -1;
    (void)device; (void)objectType;
    for (i = 0; i < VK_PRIV_SLOTS; i++) {
        if (g_priv[i].used && g_priv[i].handle == objectHandle &&
            g_priv[i].slot == privateDataSlot) {
            g_priv[i].value = data;
            return VK_SUCCESS;
        }
        if (!g_priv[i].used && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return VK_ERROR_OUT_OF_HOST_MEMORY;
    g_priv[free_slot].used = 1;
    g_priv[free_slot].handle = objectHandle;
    g_priv[free_slot].slot = privateDataSlot;
    g_priv[free_slot].value = data;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetPrivateData(
    VkDevice device, VkObjectType objectType, uint64_t objectHandle,
    VkPrivateDataSlot privateDataSlot, uint64_t *pData)
{
    int i;
    (void)device; (void)objectType;
    if (!pData) return;
    *pData = 0;
    for (i = 0; i < VK_PRIV_SLOTS; i++)
        if (g_priv[i].used && g_priv[i].handle == objectHandle &&
            g_priv[i].slot == privateDataSlot) {
            *pData = g_priv[i].value;
            return;
        }
}

/* ---- device group -------------------------------------------------------- */
VKAPI_ATTR void VKAPI_CALL vkGetDeviceGroupPeerMemoryFeatures(
    VkDevice device, uint32_t heapIndex, uint32_t localDeviceIndex,
    uint32_t remoteDeviceIndex, VkPeerMemoryFeatureFlags *pPeerMemoryFeatures)
{
    (void)device; (void)heapIndex; (void)localDeviceIndex; (void)remoteDeviceIndex;
    if (pPeerMemoryFeatures) *pPeerMemoryFeatures = 0;   /* single device: no peers */
}
