/*
 * procaddr.c - vkGetInstanceProcAddr / vkGetDeviceProcAddr and the static
 * name->PFN table covering every Vulkan 1.3 core function (built from the
 * VK_CORE_FUNCTIONS X-macro) plus the WSI extension entry points needed to
 * present.  A single global trampoline table backs every dispatchable object's
 * leading dispatch pointer, so the pointer stored in objects is a tag rather
 * than a per-object vtable.
 */
#include "internal.h"
#include "vk_funcs.h"
#include <string.h>

/* WSI extension entry points (implemented in wsi.c) resolved by name. */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice, const VkSwapchainCreateInfoKHR *, const VkAllocationCallbacks *, VkSwapchainKHR *);
VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(VkDevice, VkSwapchainKHR, uint32_t *, VkImage *);
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t *);
VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue, const VkPresentInfoKHR *);
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImage2KHR(VkDevice, const VkAcquireNextImageInfoKHR *, uint32_t *);
VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(VkInstance, VkSurfaceKHR, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32 *);
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR *);
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t *, VkSurfaceFormatKHR *);
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t *, VkPresentModeKHR *);
VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupPresentCapabilitiesKHR(VkDevice, VkDeviceGroupPresentCapabilitiesKHR *);
VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupSurfacePresentModesKHR(VkDevice, VkSurfaceKHR, VkDeviceGroupPresentModeFlagsKHR *);
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t *, VkRect2D *);

/* EwokOS-specific surface creation (implemented in wsi.c). */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateEwokOSSurfaceVK(VkInstance, uint32_t, uint32_t,
                                                       VkFormat, void *, void *, VkSurfaceKHR *);

typedef struct {
    const char          *name;
    PFN_vkVoidFunction   fn;
} vk_proc;

#define VK__ENTRY(fn) { #fn, (PFN_vkVoidFunction)(void *)fn },

static const vk_proc g_core_procs[] = {
    VK_CORE_FUNCTIONS(VK__ENTRY)
};

static const vk_proc g_ext_procs[] = {
    VK__ENTRY(vkCreateSwapchainKHR)
    VK__ENTRY(vkDestroySwapchainKHR)
    VK__ENTRY(vkGetSwapchainImagesKHR)
    VK__ENTRY(vkAcquireNextImageKHR)
    VK__ENTRY(vkAcquireNextImage2KHR)
    VK__ENTRY(vkQueuePresentKHR)
    VK__ENTRY(vkDestroySurfaceKHR)
    VK__ENTRY(vkGetPhysicalDeviceSurfaceSupportKHR)
    VK__ENTRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)
    VK__ENTRY(vkGetPhysicalDeviceSurfaceFormatsKHR)
    VK__ENTRY(vkGetPhysicalDeviceSurfacePresentModesKHR)
    VK__ENTRY(vkGetDeviceGroupPresentCapabilitiesKHR)
    VK__ENTRY(vkGetDeviceGroupSurfacePresentModesKHR)
    VK__ENTRY(vkGetPhysicalDevicePresentRectanglesKHR)
    /* KHR-suffixed aliases of promoted core functions an older loader may ask
     * for by their extension names. */
    { "vkGetPhysicalDeviceProperties2KHR", (PFN_vkVoidFunction)(void *)vkGetPhysicalDeviceProperties2 },
    { "vkGetPhysicalDeviceFeatures2KHR",   (PFN_vkVoidFunction)(void *)vkGetPhysicalDeviceFeatures2 },
    { "vkGetPhysicalDeviceMemoryProperties2KHR", (PFN_vkVoidFunction)(void *)vkGetPhysicalDeviceMemoryProperties2 },
    { "vkGetPhysicalDeviceQueueFamilyProperties2KHR", (PFN_vkVoidFunction)(void *)vkGetPhysicalDeviceQueueFamilyProperties2 },
    { "vkGetPhysicalDeviceFormatProperties2KHR", (PFN_vkVoidFunction)(void *)vkGetPhysicalDeviceFormatProperties2 },
    { "vkCreateRenderPass2KHR", (PFN_vkVoidFunction)(void *)vkCreateRenderPass2 },
    { "vkCmdBeginRenderPass2KHR", (PFN_vkVoidFunction)(void *)vkCmdBeginRenderPass2 },
    { "vkCmdEndRenderPass2KHR", (PFN_vkVoidFunction)(void *)vkCmdEndRenderPass2 },
    { "vkCmdNextSubpass2KHR", (PFN_vkVoidFunction)(void *)vkCmdNextSubpass2 },
    { "vkGetBufferDeviceAddressKHR", (PFN_vkVoidFunction)(void *)vkGetBufferDeviceAddress },
    { "vkCreateSamplerYcbcrConversionKHR", (PFN_vkVoidFunction)(void *)vkCreateSamplerYcbcrConversion },
    { "vkDestroySamplerYcbcrConversionKHR", (PFN_vkVoidFunction)(void *)vkDestroySamplerYcbcrConversion },
    { "vkGetSemaphoreCounterValueKHR", (PFN_vkVoidFunction)(void *)vkGetSemaphoreCounterValue },
    { "vkSignalSemaphoreKHR", (PFN_vkVoidFunction)(void *)vkSignalSemaphore },
    { "vkWaitSemaphoresKHR", (PFN_vkVoidFunction)(void *)vkWaitSemaphores },
    { "vkCreatePrivateDataSlotEXT", (PFN_vkVoidFunction)(void *)vkCreatePrivateDataSlot },
    { "vkDestroyPrivateDataSlotEXT", (PFN_vkVoidFunction)(void *)vkDestroyPrivateDataSlot },
    { "vkSetPrivateDataEXT", (PFN_vkVoidFunction)(void *)vkSetPrivateData },
    { "vkGetPrivateDataEXT", (PFN_vkVoidFunction)(void *)vkGetPrivateData },
    /* EwokOS WSI surface entry point (not a Khronos extension). */
    VK__ENTRY(vkCreateEwokOSSurfaceVK)
};

static PFN_vkVoidFunction lookup(const char *name)
{
    size_t i;
    if (!name) return NULL;
    for (i = 0; i < sizeof(g_core_procs)/sizeof(g_core_procs[0]); i++)
        if (strcmp(g_core_procs[i].name, name) == 0)
            return g_core_procs[i].fn;
    for (i = 0; i < sizeof(g_ext_procs)/sizeof(g_ext_procs[0]); i++)
        if (strcmp(g_ext_procs[i].name, name) == 0)
            return g_ext_procs[i].fn;
    return NULL;
}

/* The set of commands resolvable with a NULL VkInstance (the global level). */
static int is_global_command(const char *name)
{
    return strcmp(name, "vkCreateInstance") == 0 ||
           strcmp(name, "vkGetInstanceProcAddr") == 0 ||
           strcmp(name, "vkEnumerateInstanceVersion") == 0 ||
           strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0 ||
           strcmp(name, "vkEnumerateInstanceLayerProperties") == 0;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance, const char *pName)
{
    if (!pName) return NULL;
    if (instance == VK_NULL_HANDLE && !is_global_command(pName))
        return NULL;
    return lookup(pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice device, const char *pName)
{
    (void)device;
    return lookup(pName);
}
