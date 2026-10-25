/*
 * runtime.c - the libvulkan instance / physical-device registry and the shared
 * helpers every API translation unit uses (object alloc/validate, the get-info
 * marshalling primitives, the timestamp clock, memory residency, format sizes).
 *
 * There is exactly one instance and one physical device (the EwokOS software
 * device).  The physical device always advertises a coherent Vulkan 1.3 profile
 * with one GRAPHICS|COMPUTE queue family and a host-visible + device-local
 * memory model; whether a VkDevice executes on the software back-end or, on
 * raspi5, offloads to the V3D GPU is decided per device at vkCreateDevice by the
 * back-end probe, with no divergence in the advertised surface.
 */
#include "internal.h"
#include <stdlib.h>
#include <stdio.h>

#include <ewoksys/kernel_tic.h>

/* GPU device selection is compiled in only when the machine exports the
 * 3D/compute path (Makefile defines VULKAN_GPU_BACKEND under PGL_G3D=yes).
 * vk_gpu_probe() lives in src/gpu/gpu_device.c and returns non-zero when a
 * usable V3D device is present. */
#ifdef VULKAN_GPU_BACKEND
extern int vk_gpu_probe(void);
#endif

/* the dispatch tag stored as the first member of every dispatchable object */
const struct vk_dispatch vk_global_dispatch = { 0 };

/* ---- the singleton registry ---------------------------------------------- */
static vk_instance          g_instance;
static vk_physical_device   g_phys;
static int                  g_initialised;

static void fill_properties(VkPhysicalDeviceProperties *p)
{
    memset(p, 0, sizeof(*p));
    p->apiVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
    p->driverVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
    p->vendorID = 0x10001;           /* EwokOS */
    p->deviceID = 0x0001;
    p->deviceType = VK_PHYSICAL_DEVICE_TYPE_CPU;
    snprintf(p->deviceName, sizeof(p->deviceName), "EwokOS Vulkan Device");
    memset(p->pipelineCacheUUID, 0, sizeof(p->pipelineCacheUUID));
    p->pipelineCacheUUID[0] = 'E'; p->pipelineCacheUUID[1] = 'V';
    p->pipelineCacheUUID[2] = 'K'; p->pipelineCacheUUID[3] = '1';

    /* a coherent, deliberately conservative limits profile */
    {
        VkPhysicalDeviceLimits *l = &p->limits;
        l->maxImageDimension1D = 4096;
        l->maxImageDimension2D = 4096;
        l->maxImageDimension3D = 512;
        l->maxImageDimensionCube = 4096;
        l->maxImageArrayLayers = 256;
        l->maxTexelBufferElements = 65536;
        l->maxUniformBufferRange = 65536;
        l->maxStorageBufferRange = 1u << 28;
        l->maxPushConstantsSize = VK_MAX_PUSH_CONSTANT_RANGE;
        l->maxMemoryAllocationCount = 4096;
        l->maxSamplerAllocationCount = 4000;
        l->bufferImageGranularity = 64;
        l->maxBoundDescriptorSets = VK_MAX_DESCRIPTOR_SETS;
        l->maxPerStageDescriptorSamplers = 16;
        l->maxPerStageDescriptorUniformBuffers = 16;
        l->maxPerStageDescriptorStorageBuffers = 8;
        l->maxPerStageDescriptorSampledImages = 16;
        l->maxPerStageResources = 64;
        l->maxDescriptorSetSamplers = 16;
        l->maxDescriptorSetUniformBuffers = 16;
        l->maxDescriptorSetStorageBuffers = 8;
        l->maxDescriptorSetSampledImages = 16;
        l->maxVertexInputAttributes = VK_MAX_VERTEX_ATTRIBUTES;
        l->maxVertexInputBindings = VK_MAX_VERTEX_BINDINGS;
        l->maxVertexInputAttributeOffset = 2047;
        l->maxVertexInputBindingStride = 2048;
        l->maxFragmentCombinedOutputResources = 8;
        l->maxComputeWorkGroupCount[0] = 65535;
        l->maxComputeWorkGroupCount[1] = 65535;
        l->maxComputeWorkGroupCount[2] = 65535;
        l->maxComputeWorkGroupInvocations = 256;
        l->maxComputeWorkGroupSize[0] = 256;
        l->maxComputeWorkGroupSize[1] = 256;
        l->maxComputeWorkGroupSize[2] = 64;
        l->maxViewports = 1;
        l->maxViewportDimensions[0] = 4096;
        l->maxViewportDimensions[1] = 4096;
        l->framebufferColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
        l->framebufferDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT;
        l->maxColorAttachments = VK_MAX_COLOR_ATTACHMENTS;
        l->timestampPeriod = 1.0f;
        l->nonCoherentAtomSize = 64;
        l->minUniformBufferOffsetAlignment = 16;
        l->minStorageBufferOffsetAlignment = 16;
        l->mipmapPrecisionBits = 4;
        l->subTexelPrecisionBits = 4;
    }
    {
        VkPhysicalDeviceSparseProperties *s = &p->sparseProperties;
        s->residencyStandard2DBlockShape = VK_FALSE;
        s->residencyStandard2DMultisampleBlockShape = VK_FALSE;
        s->residencyStandard3DBlockShape = VK_FALSE;
        s->residencyAlignedMipSize = VK_FALSE;
        s->residencyNonResidentStrict = VK_FALSE;
    }
}

static void fill_features(VkPhysicalDeviceFeatures *f)
{
    memset(f, 0, sizeof(*f));
    /* the software device + SPIR-V interpreter supports this core subset */
    f->robustBufferAccess = VK_FALSE;
    f->fullDrawIndexUint32 = VK_TRUE;
    f->imageCubeArray = VK_FALSE;
    f->shaderInt64 = VK_FALSE;
    f->shaderInt16 = VK_FALSE;
    f->shaderFloat64 = VK_FALSE;
    f->shaderStorageImageExtendedFormats = VK_TRUE;
    f->textureCompressionETC2 = VK_FALSE;
    f->logicOp = VK_TRUE;
    f->multiDrawIndirect = VK_FALSE;
    f->samplerAnisotropy = VK_TRUE;
    f->independentBlend = VK_FALSE;
    f->largePoints = VK_TRUE;
    f->wideLines = VK_TRUE;
}

static void fill_memory(VkPhysicalDeviceMemoryProperties *m)
{
    memset(m, 0, sizeof(*m));
    /* type 0: device-local + host-visible + host-coherent (the single unified
     * store the software model uses); type 1: host-visible + host-coherent. */
    m->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    m->memoryTypes[0].heapIndex = 0;
    m->memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    m->memoryTypes[1].heapIndex = 1;
    m->memoryTypeCount = 2;

    m->memoryHeaps[0].size = 512ull * 1024 * 1024;
    m->memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
    m->memoryHeaps[1].size = 512ull * 1024 * 1024;
    m->memoryHeaps[1].flags = 0;
    m->memoryHeapCount = 2;
}

static void runtime_init(void)
{
    if (g_initialised)
        return;
    g_initialised = 1;

    memset(&g_phys, 0, sizeof(g_phys));
    g_phys.dispatch = &vk_global_dispatch;
    g_phys.magic = VK_OBJ_MAGIC;
    g_phys.type = VK_OBJ_PHYSDEV;
    g_phys.instance = &g_instance;
    fill_properties(&g_phys.props);
    fill_features(&g_phys.features);
    fill_memory(&g_phys.mem);

    g_phys.num_qfam = 1;
    g_phys.qfam[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT |
                                VK_QUEUE_TRANSFER_BIT;
    g_phys.qfam[0].queueCount = VK_MAX_QUEUES_PER_FAMILY;
    g_phys.qfam[0].timestampValidBits = 64;
    g_phys.qfam[0].minImageTransferGranularity.width = 1;
    g_phys.qfam[0].minImageTransferGranularity.height = 1;
    g_phys.qfam[0].minImageTransferGranularity.depth = 1;

#ifdef VULKAN_GPU_BACKEND
    g_phys.gpu_available = vk_gpu_probe() ? 1 : 0;
    if (g_phys.gpu_available) {
        g_phys.props.deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
        snprintf(g_phys.props.deviceName, sizeof(g_phys.props.deviceName),
                 "EwokOS GPU (V3D)");
    }
#else
    g_phys.gpu_available = 0;
#endif
}

vk_instance *vk_runtime_instance(void)
{
    return &g_instance;
}

vk_physical_device *vk_runtime_physical(void)
{
    runtime_init();
    return &g_phys;
}

/* ---- object alloc / validate --------------------------------------------- */
void *vk_obj_alloc(size_t size, vk_obj_type type)
{
    /* every runtime object begins with the VK_OBJ_HEADER (magic + type); the
     * dispatchable ones begin with a dispatch pointer BEFORE that header, so
     * callers of dispatchable objects fill the dispatch member themselves and
     * pass the offset-adjusted size.  This helper zeroes and stamps the header
     * at offset 0, which is correct for the non-dispatchable majority. */
    void *p = calloc(1, size);
    if (!p)
        return NULL;
    {
        uint32_t *magic = (uint32_t *)p;
        vk_obj_type *t = (vk_obj_type *)(magic + 1);
        *magic = VK_OBJ_MAGIC;
        *t = type;
    }
    return p;
}

void *vk_obj_alloc_dispatch(size_t size, vk_obj_type type)
{
    /* dispatchable layout: [const struct vk_dispatch *][uint32_t magic][type].
     * The dispatch pointer occupies offset 0 (the Vulkan ABI requirement), the
     * object header follows.  vk_obj_valid() reads the header at offset 0 of
     * the returned pointer only for non-dispatchable objects, so dispatchable
     * objects are validated through their own struct (magic after dispatch). */
    uint8_t *p = (uint8_t *)calloc(1, size);
    if (!p)
        return NULL;
    {
        const struct vk_dispatch **dpp = (const struct vk_dispatch **)p;
        uint32_t   *magic = (uint32_t *)(dpp + 1);
        vk_obj_type *t    = (vk_obj_type *)(magic + 1);
        *dpp   = &vk_global_dispatch;
        *magic = VK_OBJ_MAGIC;
        *t     = type;
    }
    return p;
}

void vk_obj_free(void *obj)
{
    free(obj);
}

int vk_obj_valid(const void *obj, vk_obj_type type)
{
    const uint32_t *magic;
    if (!obj)
        return 0;
    magic = (const uint32_t *)obj;
    if (*magic != VK_OBJ_MAGIC)
        return 0;
    {
        const vk_obj_type *t = (const vk_obj_type *)(magic + 1);
        return (*t == (vk_obj_type)type) ? 1 : 0;
    }
}

int vk_obj_valid_dispatch(const void *obj, vk_obj_type type)
{
    /* dispatchable objects carry the header AFTER the leading dispatch pointer,
     * mirroring vk_obj_alloc_dispatch's layout. */
    const struct vk_dispatch *const *dpp;
    const uint32_t *magic;
    if (!obj)
        return 0;
    dpp = (const struct vk_dispatch *const *)obj;
    if (*dpp != &vk_global_dispatch)
        return 0;
    magic = (const uint32_t *)(dpp + 1);
    if (*magic != VK_OBJ_MAGIC)
        return 0;
    {
        const vk_obj_type *t = (const vk_obj_type *)(magic + 1);
        return (*t == (vk_obj_type)type) ? 1 : 0;
    }
}

/* ---- get-info / enumeration marshalling ---------------------------------- */
uint32_t vk_write_out(uint32_t avail_count, void *out, uint32_t *out_count,
                      const void *items, uint32_t item_count, size_t item_size)
{
    uint32_t n = item_count < avail_count ? item_count : avail_count;
    if (out && n)
        memcpy(out, items, (size_t)n * item_size);
    if (out_count)
        *out_count = item_count;
    return n;
}

VkResult vk_enum_write(uint32_t *count, uint32_t avail, void *out,
                       const void *items, uint32_t item_count, size_t item_size)
{
    if (!count)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (!out) {
        /* Count-only query (pX == NULL): the spec returns VK_SUCCESS here with
         * the total in *count; VK_INCOMPLETE is reserved for the sized-array
         * path below when the caller's buffer is too small. */
        *count = item_count;
        return VK_SUCCESS;
    }
    {
        uint32_t n = item_count < avail ? item_count : avail;
        if (n)
            memcpy(out, items, (size_t)n * item_size);
        *count = n;
        return (n < item_count) ? VK_INCOMPLETE : VK_SUCCESS;
    }
}

/* ---- timestamp clock ----------------------------------------------------- */
uint64_t vk_now_ns(void)
{
    uint64_t ns = 0;
    if (kernel_tic_nsec(&ns) != 0) {
        uint32_t sec = 0;
        uint64_t usec = 0;
        kernel_tic(&sec, &usec);
        ns = (uint64_t)sec * 1000000000ull + usec * 1000ull;
    }
    return ns;
}

/* ---- memory residency ---------------------------------------------------- */
void *vk_buffer_ptr(vk_buffer *b)
{
    if (!b || !b->mem || !b->mem->host)
        return NULL;
    return (char *)b->mem->host + b->offset;
}

void *vk_image_ptr(vk_image *im)
{
    if (!im || !im->mem || !im->mem->host)
        return NULL;
    return (char *)im->mem->host + im->offset;
}

uint32_t vk_format_bpp(VkFormat f)
{
    switch (f) {
    case VK_FORMAT_R8_UNORM: case VK_FORMAT_R8_SNORM:
    case VK_FORMAT_R8_UINT: case VK_FORMAT_R8_SINT:
    case VK_FORMAT_R8_SRGB:
        return 1;
    case VK_FORMAT_R8G8_UNORM: case VK_FORMAT_R8G8_UINT:
    case VK_FORMAT_R16_SFLOAT: case VK_FORMAT_R16_UNORM:
    case VK_FORMAT_D16_UNORM:
        return 2;
    case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_R8G8B8A8_UINT: case VK_FORMAT_R8G8B8A8_SINT:
    case VK_FORMAT_R32_SFLOAT: case VK_FORMAT_R32_UINT:
    case VK_FORMAT_R32_SINT: case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT: case VK_FORMAT_D24_UNORM_S8_UINT:
        return 4;
    case VK_FORMAT_R32G32_SFLOAT: case VK_FORMAT_R32G32_UINT:
    case VK_FORMAT_R16G16_SFLOAT: case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return 8;
    case VK_FORMAT_R32G32B32_SFLOAT:
        return 12;
    case VK_FORMAT_R32G32B32A32_SFLOAT: case VK_FORMAT_R32G32B32A32_UINT:
        return 16;
    default:
        return 0;
    }
}
