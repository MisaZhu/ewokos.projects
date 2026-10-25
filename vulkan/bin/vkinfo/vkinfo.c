/*
 * vkinfo.c - a compact vkinfo (the clinfo analog): enumerates the instance
 * layers/extensions and every physical device libvulkan exposes, then prints the
 * subset of Vulkan 1.3 physical-device queries that confirm the runtime's
 * advertised profile (properties, limits, features, queue families, memory
 * types/heaps, and a sample of format support).  Read-only: it creates an
 * instance and a device to prove they enumerate, but records no work.
 *
 * Runs identically on every machine: the software device is always present, and
 * on raspi5 the same physical device additionally reports a GPU back-end.
 */
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <string.h>

/* The public headers leave the max queue-family count open, so query into a
 * fixed local bound well above the software device's single family. */
#define VKINFO_MAX_QUEUE_FAMILIES 16u

static const char *phys_type_name(VkPhysicalDeviceType t)
{
    switch (t) {
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:          return "OTHER";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "INTEGRATED_GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "DISCRETE_GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "VIRTUAL_GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "CPU";
    default:                                     return "?";
    }
}

static void dump_features(const VkPhysicalDeviceFeatures *f)
{
    printf("  Features (subset):\n");
    printf("    %-28s %s\n", "geometryShader", f->geometryShader ? "yes" : "no");
    printf("    %-28s %s\n", "tessellationShader", f->tessellationShader ? "yes" : "no");
    printf("    %-28s %s\n", "logicOp", f->logicOp ? "yes" : "no");
    printf("    %-28s %s\n", "samplerAnisotropy", f->samplerAnisotropy ? "yes" : "no");
    printf("    %-28s %s\n", "shaderStorageImageExtendedFormats",
           f->shaderStorageImageExtendedFormats ? "yes" : "no");
}

static void dump_limits(const VkPhysicalDeviceLimits *l)
{
    printf("  Limits (subset):\n");
    printf("    %-34s %u\n", "maxImageDimension2D", l->maxImageDimension2D);
    printf("    %-34s %u\n", "maxBoundDescriptorSets", l->maxBoundDescriptorSets);
    printf("    %-34s %u\n", "maxPushConstantsSize", l->maxPushConstantsSize);
    printf("    %-34s %llu\n", "maxMemoryAllocationCount",
           (unsigned long long)l->maxMemoryAllocationCount);
    printf("    %-34s %u\n", "maxComputeWorkGroupCount[0]", l->maxComputeWorkGroupCount[0]);
    printf("    %-34s %u\n", "maxComputeWorkGroupInvocations",
           l->maxComputeWorkGroupInvocations);
    printf("    %-34s %u\n", "maxComputeSharedMemorySize", l->maxComputeSharedMemorySize);
    printf("    %-34s %llu\n", "maxStorageBufferRange",
           (unsigned long long)l->maxStorageBufferRange);
}

static void dump_phys(VkPhysicalDevice pd)
{
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceFeatures feats;
    VkPhysicalDeviceMemoryProperties mem;
    VkQueueFamilyProperties qfam[VKINFO_MAX_QUEUE_FAMILIES];
    uint32_t nq = VKINFO_MAX_QUEUE_FAMILIES, i;

    vkGetPhysicalDeviceProperties(pd, &props);
    vkGetPhysicalDeviceFeatures(pd, &feats);
    vkGetPhysicalDeviceMemoryProperties(pd, &mem);

    printf("  deviceName              %s\n", props.deviceName);
    printf("  deviceType              %s\n", phys_type_name(props.deviceType));
    printf("  apiVersion              %u.%u.%u\n",
           VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
           VK_VERSION_PATCH(props.apiVersion));
    printf("  driverVersion           0x%08x\n", props.driverVersion);
    printf("  vendorID / deviceID     0x%08x / 0x%08x\n", props.vendorID, props.deviceID);

    dump_limits(&props.limits);
    dump_features(&feats);

    vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, qfam);
    printf("  Queue Families: %u\n", nq);
    for (i = 0; i < nq; i++) {
        printf("    [%u] flags=%s%s%s count=%u timestampValidBits=%u\n", i,
               (qfam[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) ? "GRAPHICS " : "",
               (qfam[i].queueFlags & VK_QUEUE_COMPUTE_BIT) ? "COMPUTE " : "",
               (qfam[i].queueFlags & VK_QUEUE_TRANSFER_BIT) ? "TRANSFER" : "",
               qfam[i].queueCount, qfam[i].timestampValidBits);
    }

    printf("  Memory Types: %u\n", mem.memoryTypeCount);
    for (i = 0; i < mem.memoryTypeCount; i++) {
        VkMemoryPropertyFlags fl = mem.memoryTypes[i].propertyFlags;
        printf("    [%u] heap=%u %s%s%s%s\n", i, mem.memoryTypes[i].heapIndex,
               (fl & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? "DEVICE_LOCAL " : "",
               (fl & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ? "HOST_VISIBLE " : "",
               (fl & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ? "HOST_COHERENT " : "",
               (fl & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) ? "HOST_CACHED" : "");
    }
    printf("  Memory Heaps: %u\n", mem.memoryHeapCount);
    for (i = 0; i < mem.memoryHeapCount; i++)
        printf("    [%u] size=%llu flags=0x%x\n", i,
               (unsigned long long)mem.memoryHeaps[i].size, mem.memoryHeaps[i].flags);

    {   /* a couple of representative format queries */
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(pd, VK_FORMAT_R8G8B8A8_UNORM, &fp);
        printf("  R8G8B8A8_UNORM bufferFeatures=0x%x linear=0x%x optimal=0x%x\n",
               fp.bufferFeatures, fp.linearTilingFeatures, fp.optimalTilingFeatures);
        vkGetPhysicalDeviceFormatProperties(pd, VK_FORMAT_R32_SFLOAT, &fp);
        printf("  R32_SFLOAT     bufferFeatures=0x%x linear=0x%x optimal=0x%x\n",
               fp.bufferFeatures, fp.linearTilingFeatures, fp.optimalTilingFeatures);
    }
}

int main(void)
{
    VkInstance instance = VK_NULL_HANDLE;
    VkInstanceCreateInfo ici;
    VkApplicationInfo app;
    VkPhysicalDevice phys[8];
    uint32_t nphys = 0, i;
    VkResult rc;

    /* instance layers / extensions */
    {
        VkLayerProperties layers[16];
        VkExtensionProperties exts[64];
        uint32_t nl = 16, ne = 64, j;
        if (vkEnumerateInstanceLayerProperties(&nl, layers) == VK_SUCCESS) {
            printf("Instance Layers: %u\n", nl);
            for (j = 0; j < nl; j++)
                printf("  %s (spec %u)\n", layers[j].layerName, layers[j].specVersion);
        }
        if (vkEnumerateInstanceExtensionProperties(NULL, &ne, exts) == VK_SUCCESS) {
            printf("Instance Extensions: %u\n", ne);
            for (j = 0; j < ne; j++)
                printf("  %s\n", exts[j].extensionName);
        }
    }

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "vkinfo";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "none";
    app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion = VK_API_VERSION_1_3;

    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

    rc = vkCreateInstance(&ici, NULL, &instance);
    if (rc != VK_SUCCESS) {
        printf("vkinfo: vkCreateInstance failed (%d)\n", (int)rc);
        return 1;
    }

    rc = vkEnumeratePhysicalDevices(instance, &nphys, NULL);
    if (rc != VK_SUCCESS || nphys == 0) {
        printf("vkinfo: no physical devices (rc=%d n=%u)\n", (int)rc, nphys);
        vkDestroyInstance(instance, NULL);
        return 1;
    }
    if (nphys > 8) nphys = 8;
    vkEnumeratePhysicalDevices(instance, &nphys, phys);
    printf("\nPhysical Devices: %u\n", nphys);

    for (i = 0; i < nphys; i++) {
        printf("\nDevice %u:\n", i);
        dump_phys(phys[i]);

        /* prove a logical device + a graphics/compute queue enumerate. */
        {
            VkDevice device = VK_NULL_HANDLE;
            VkDeviceCreateInfo dci;
            float prio = 1.0f;
            VkDeviceQueueCreateInfo qci;
            VkQueue queue = VK_NULL_HANDLE;

            memset(&qci, 0, sizeof(qci));
            qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qci.queueFamilyIndex = 0;
            qci.queueCount = 1;
            qci.pQueuePriorities = &prio;

            memset(&dci, 0, sizeof(dci));
            dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            dci.queueCreateInfoCount = 1;
            dci.pQueueCreateInfos = &qci;

            if (vkCreateDevice(phys[i], &dci, NULL, &device) == VK_SUCCESS) {
                vkGetDeviceQueue(device, 0, 0, &queue);
                printf("  logical device created; queue0=%s\n",
                       queue != VK_NULL_HANDLE ? "ok" : "NULL");
                vkDestroyDevice(device, NULL);
            } else {
                printf("  vkCreateDevice failed\n");
            }
        }
    }

    vkDestroyInstance(instance, NULL);
    printf("\nvkinfo: done\n");
    return 0;
}
