/*
 * vktest.c - Vulkan interface test + compute correctness/performance benchmark.
 * This is the Vulkan counterpart of opencl/bin/cltest: it runs saxpy, matmul and
 * a transcendental workload from embedded SPIR-V modules, checks every result
 * against a host reference, and times repeated vkCmdDispatch submissions.
 *
 * Vulkan exposes one physical device on EwokOS.  Each logical device selects
 * the V3D back-end when available; EWOK_VULKAN_CPU=1 selects the software
 * interpreter instead.  On GPU machines this test runs both logical-device
 * variants over identical modules and grids, then reports CPU/GPU speedups.
 * Cold dispatch includes lazy shader translation/compile/upload; steady does not.
 *
 * The coverage phase additionally exercises instance/device queries, mapped
 * memory, buffer/image transfers, views/samplers, descriptor copies, command
 * buffer lifecycle, fences, events, timeline semaphores and query pools.
 */
#include <vulkan/vulkan.h>
#include <ewoksys/kernel_tic.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vktest_spirv.h"

#define SAXPY_N     (256 * 1024)
#define MATMUL_N    48
#define MATH_N      (8 * 1024)
#define MATH_INNER  32
#define BENCH_ITERS 2

/* SPIR-V local_size per kernel (baked into vktest_spirv.h).  saxpy/math use the
 * widest 1-D group the GPU offload path accepts (256 lanes) so the fixed
 * per-workgroup CSD overhead is amortized over 4x more lanes; matmul stays at
 * 64 because its dispatch is 2-D (one row per group) and widening 1-D would only
 * add guarded-off lanes without cutting the workgroup count. */
#define SAXPY_LSZ   256
#define MATMUL_LSZ  64
#define MATH_LSZ    256

struct test_ctx {
    VkInstance instance;
    VkPhysicalDevice phys;
    VkDevice device;
    VkQueue queue;
    uint32_t qfi;
    int is_gpu;
};

typedef struct {
    VkBuffer buf;
    VkDeviceMemory mem;
    void *map;
    VkDeviceSize size;
} buffer_t;

struct cov {
    int pass;
    int fail;
};

#define COV_OK(call) do { \
    VkResult _r = (call); \
    if (_r == VK_SUCCESS) c->pass++; \
    else { c->fail++; printf("  COV FAIL %s -> %d\n", #call, (int)_r); } \
} while (0)
#define COV_ERR(call, want) do { \
    VkResult _r = (call); \
    if (_r == (want)) c->pass++; \
    else { c->fail++; printf("  COV FAIL %s -> %d (want %d)\n", #call, (int)_r, (int)(want)); } \
} while (0)
#define COV_TRUE(cond) do { \
    if (cond) c->pass++; \
    else { c->fail++; printf("  COV FAIL %s\n", #cond); } \
} while (0)
#define ZERO_INFO(p, st) do { memset((p), 0, sizeof(*(p))); (p)->sType = (st); } while (0)

static uint64_t now_ns(void)
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

static uint32_t find_memory_type(VkPhysicalDevice phys, uint32_t filter)
{
    VkPhysicalDeviceMemoryProperties mem;
    uint32_t i;
    vkGetPhysicalDeviceMemoryProperties(phys, &mem);
    for (i = 0; i < mem.memoryTypeCount; i++) {
        VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((filter & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags & want) == want)
            return i;
    }
    return 0xffffffffu;
}

static int buffer_create(struct test_ctx *ctx, buffer_t *b, VkDeviceSize size,
                         VkBufferUsageFlags usage)
{
    VkBufferCreateInfo bci;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo mai;
    uint32_t mt;
    memset(b, 0, sizeof(*b));
    b->size = size;
    ZERO_INFO(&bci, VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(ctx->device, &bci, NULL, &b->buf) != VK_SUCCESS)
        return -1;
    vkGetBufferMemoryRequirements(ctx->device, b->buf, &req);
    mt = find_memory_type(ctx->phys, req.memoryTypeBits);
    if (mt == 0xffffffffu)
        return -1;
    ZERO_INFO(&mai, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mt;
    if (vkAllocateMemory(ctx->device, &mai, NULL, &b->mem) != VK_SUCCESS ||
        vkBindBufferMemory(ctx->device, b->buf, b->mem, 0) != VK_SUCCESS ||
        vkMapMemory(ctx->device, b->mem, 0, VK_WHOLE_SIZE, 0, &b->map) != VK_SUCCESS)
        return -1;
    return 0;
}

static void buffer_destroy(struct test_ctx *ctx, buffer_t *b)
{
    if (b->map) vkUnmapMemory(ctx->device, b->mem);
    if (b->buf) vkDestroyBuffer(ctx->device, b->buf, NULL);
    if (b->mem) vkFreeMemory(ctx->device, b->mem, NULL);
    memset(b, 0, sizeof(*b));
}

static int check_floats(const char *tag, const float *got, const float *want,
                        uint32_t n, float tol)
{
    uint32_t i, bad = 0;
    for (i = 0; i < n; i++) {
        float d = (float)fabs(got[i] - want[i]);
        float s = (float)fabs(want[i]);
        if (d > tol && d > tol * s) {
            if (bad < 3)
                printf("  %s: mismatch[%u] got %f want %f\n", tag, i, got[i], want[i]);
            bad++;
        }
    }
    if (bad) printf("  %s: VERIFY FAIL (%u/%u)\n", tag, bad, n);
    return bad != 0;
}

static double run_bench(struct test_ctx *ctx, const char *tag,
                        const uint32_t *code, size_t code_bytes, uint32_t nbuf,
                        const float **initial, uint32_t out_index, uint32_t n,
                        uint32_t gx, uint32_t gy, const float *want, float tol)
{
    buffer_t bufs[3];
    VkShaderModule module = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    VkDescriptorSet dset = VK_NULL_HANDLE;
    VkCommandPool cpool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo smci;
    VkDescriptorSetLayoutBinding bindings[3];
    VkDescriptorSetLayoutCreateInfo dslci;
    VkPipelineLayoutCreateInfo plci;
    VkPipelineShaderStageCreateInfo stage;
    VkComputePipelineCreateInfo cpci;
    VkDescriptorPoolSize psz;
    VkDescriptorPoolCreateInfo dpci;
    VkDescriptorSetAllocateInfo dsai;
    VkDescriptorBufferInfo dbi[3];
    VkWriteDescriptorSet writes[3];
    VkCommandPoolCreateInfo cpoolci;
    VkCommandBufferAllocateInfo cbai;
    VkCommandBufferBeginInfo cbbi;
    VkFenceCreateInfo fci;
    VkSubmitInfo si;
    double cold = -1.0, steady = -1.0;
    uint64_t t0, t1;
    uint32_t i;
    int iter;
    memset(bufs, 0, sizeof(bufs));

    for (i = 0; i < nbuf; i++) {
        if (buffer_create(ctx, &bufs[i], n * sizeof(float),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0)
            goto out;
        if (initial[i]) memcpy(bufs[i].map, initial[i], n * sizeof(float));
        else memset(bufs[i].map, 0, n * sizeof(float));
    }
    ZERO_INFO(&smci, VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    smci.codeSize = code_bytes;
    smci.pCode = code;
    if (vkCreateShaderModule(ctx->device, &smci, NULL, &module) != VK_SUCCESS)
        goto out;
    memset(bindings, 0, sizeof(bindings));
    for (i = 0; i < nbuf; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    ZERO_INFO(&dslci, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    dslci.bindingCount = nbuf;
    dslci.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(ctx->device, &dslci, NULL, &dsl) != VK_SUCCESS)
        goto out;
    ZERO_INFO(&plci, VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &dsl;
    if (vkCreatePipelineLayout(ctx->device, &plci, NULL, &layout) != VK_SUCCESS)
        goto out;
    ZERO_INFO(&stage, VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";
    ZERO_INFO(&cpci, VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
    cpci.stage = stage;
    cpci.layout = layout;
    if (vkCreateComputePipelines(ctx->device, VK_NULL_HANDLE, 1, &cpci,
                                 NULL, &pipeline) != VK_SUCCESS)
        goto out;
    psz.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    psz.descriptorCount = nbuf;
    ZERO_INFO(&dpci, VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &psz;
    if (vkCreateDescriptorPool(ctx->device, &dpci, NULL, &dpool) != VK_SUCCESS)
        goto out;
    ZERO_INFO(&dsai, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    dsai.descriptorPool = dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &dsl;
    if (vkAllocateDescriptorSets(ctx->device, &dsai, &dset) != VK_SUCCESS)
        goto out;
    memset(dbi, 0, sizeof(dbi));
    memset(writes, 0, sizeof(writes));
    for (i = 0; i < nbuf; i++) {
        dbi[i].buffer = bufs[i].buf;
        dbi[i].range = VK_WHOLE_SIZE;
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = dset;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &dbi[i];
    }
    vkUpdateDescriptorSets(ctx->device, nbuf, writes, 0, NULL);
    ZERO_INFO(&cpoolci, VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    cpoolci.queueFamilyIndex = ctx->qfi;
    if (vkCreateCommandPool(ctx->device, &cpoolci, NULL, &cpool) != VK_SUCCESS)
        goto out;
    ZERO_INFO(&cbai, VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    cbai.commandPool = cpool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(ctx->device, &cbai, &cmd) != VK_SUCCESS)
        goto out;
    ZERO_INFO(&cbbi, VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    if (vkBeginCommandBuffer(cmd, &cbbi) != VK_SUCCESS)
        goto out;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout,
                            0, 1, &dset, 0, NULL);
    vkCmdDispatch(cmd, gx, gy, 1);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        goto out;
    ZERO_INFO(&fci, VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    if (vkCreateFence(ctx->device, &fci, NULL, &fence) != VK_SUCCESS)
        goto out;
    ZERO_INFO(&si, VK_STRUCTURE_TYPE_SUBMIT_INFO);
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;

    t0 = now_ns();
    if (vkQueueSubmit(ctx->queue, 1, &si, fence) != VK_SUCCESS ||
        vkWaitForFences(ctx->device, 1, &fence, VK_TRUE, 5000000000ull) != VK_SUCCESS)
        goto out;
    t1 = now_ns();
    cold = (double)(t1 - t0) / 1e6;
    t0 = now_ns();
    for (iter = 0; iter < BENCH_ITERS; iter++) {
        if (vkResetFences(ctx->device, 1, &fence) != VK_SUCCESS ||
            vkQueueSubmit(ctx->queue, 1, &si, fence) != VK_SUCCESS ||
            vkWaitForFences(ctx->device, 1, &fence, VK_TRUE, 5000000000ull) != VK_SUCCESS)
            goto out;
    }
    t1 = now_ns();
    steady = (double)(t1 - t0) / 1e6 / BENCH_ITERS;
    if (check_floats(tag, (const float *)bufs[out_index].map, want, n, tol))
        steady = -1.0;
    printf("  %-6s cold %8.2f ms  steady %8.2f ms/dispatch\n", tag, cold, steady);
    fflush(stdout);
out:
    if (steady < 0 && cold < 0) printf("  %s: setup/dispatch failed\n", tag);
    if (fence) vkDestroyFence(ctx->device, fence, NULL);
    if (cpool) vkDestroyCommandPool(ctx->device, cpool, NULL);
    if (dpool) vkDestroyDescriptorPool(ctx->device, dpool, NULL);
    if (pipeline) vkDestroyPipeline(ctx->device, pipeline, NULL);
    if (layout) vkDestroyPipelineLayout(ctx->device, layout, NULL);
    if (dsl) vkDestroyDescriptorSetLayout(ctx->device, dsl, NULL);
    if (module) vkDestroyShaderModule(ctx->device, module, NULL);
    for (i = 0; i < nbuf; i++) buffer_destroy(ctx, &bufs[i]);
    return steady;
}

static int coverage_record_submit(struct test_ctx *ctx, struct cov *c,
                                  VkCommandBuffer cmd)
{
    COV_OK(vkEndCommandBuffer(cmd));
    COV_OK(vkQueueSubmit(ctx->queue, 0, NULL, VK_NULL_HANDLE));
    {
        VkSubmitInfo si;
        ZERO_INFO(&si, VK_STRUCTURE_TYPE_SUBMIT_INFO);
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        COV_OK(vkQueueSubmit(ctx->queue, 1, &si, VK_NULL_HANDLE));
    }
    COV_OK(vkQueueWaitIdle(ctx->queue));
    return c->fail != 0;
}

static void cov_queries(struct test_ctx *ctx, struct cov *c)
{
    uint32_t version = 0, count = 0;
    VkPhysicalDeviceGroupProperties groups[2];
    VkPhysicalDeviceProperties2 props2;
    VkPhysicalDeviceVulkan12Features f12;
    VkPhysicalDeviceVulkan13Features f13;
    VkPhysicalDeviceFeatures2 features2;
    VkPhysicalDeviceMemoryProperties2 mem2;
    VkQueueFamilyProperties2 qfam2[4];
    VkFormatProperties2 fmt2;
    VkPhysicalDeviceImageFormatInfo2 ifi;
    VkImageFormatProperties2 ifp;
    VkExtensionProperties exts[8];
    VkQueue queue = VK_NULL_HANDLE;
    VkDeviceQueueInfo2 dqi;

    COV_OK(vkEnumerateInstanceVersion(&version));
    COV_TRUE(VK_VERSION_MAJOR(version) == 1 && VK_VERSION_MINOR(version) >= 3);
    count = 2;
    memset(groups, 0, sizeof(groups));
    groups[0].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES;
    groups[1].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES;
    COV_OK(vkEnumeratePhysicalDeviceGroups(ctx->instance, &count, groups));
    COV_TRUE(count == 1 && groups[0].physicalDeviceCount == 1);
    count = 8;
    COV_OK(vkEnumerateDeviceExtensionProperties(ctx->phys, NULL, &count, exts));
    COV_TRUE(count > 0);
    count = 0;
    COV_OK(vkEnumerateInstanceLayerProperties(&count, NULL));
    COV_TRUE(count == 0);

    ZERO_INFO(&props2, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
    vkGetPhysicalDeviceProperties2(ctx->phys, &props2);
    COV_TRUE(props2.properties.limits.maxComputeWorkGroupInvocations > 0);
    ZERO_INFO(&f12, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    ZERO_INFO(&f13, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
    ZERO_INFO(&features2, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    f12.pNext = &f13;
    features2.pNext = &f12;
    vkGetPhysicalDeviceFeatures2(ctx->phys, &features2);
    COV_TRUE(f12.timelineSemaphore && f12.bufferDeviceAddress);
    COV_TRUE(f13.dynamicRendering && f13.synchronization2 && f13.maintenance4);
    ZERO_INFO(&mem2, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2);
    vkGetPhysicalDeviceMemoryProperties2(ctx->phys, &mem2);
    COV_TRUE(mem2.memoryProperties.memoryTypeCount > 0);
    count = 4;
    memset(qfam2, 0, sizeof(qfam2));
    qfam2[0].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    vkGetPhysicalDeviceQueueFamilyProperties2(ctx->phys, &count, qfam2);
    COV_TRUE(count > 0 && (qfam2[0].queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT));
    ZERO_INFO(&fmt2, VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2);
    vkGetPhysicalDeviceFormatProperties2(ctx->phys, VK_FORMAT_R8G8B8A8_UNORM, &fmt2);
    COV_TRUE(fmt2.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
    ZERO_INFO(&ifi, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2);
    ifi.format = VK_FORMAT_R8G8B8A8_UNORM;
    ifi.type = VK_IMAGE_TYPE_2D;
    ifi.tiling = VK_IMAGE_TILING_LINEAR;
    ifi.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ZERO_INFO(&ifp, VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2);
    COV_OK(vkGetPhysicalDeviceImageFormatProperties2(ctx->phys, &ifi, &ifp));
    COV_TRUE(ifp.imageFormatProperties.maxExtent.width >= 4);
    COV_ERR(vkGetPhysicalDeviceImageFormatProperties(ctx->phys, VK_FORMAT_UNDEFINED,
            VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_LINEAR, 0, 0,
            &ifp.imageFormatProperties), VK_ERROR_FORMAT_NOT_SUPPORTED);
    count = 1;
    COV_OK(vkGetPhysicalDeviceToolProperties(ctx->phys, &count, NULL));
    COV_TRUE(count == 0);
    COV_TRUE(vkGetInstanceProcAddr(ctx->instance, "vkQueueSubmit2") != NULL);
    COV_TRUE(vkGetDeviceProcAddr(ctx->device, "vkCmdDispatch") != NULL);
    ZERO_INFO(&dqi, VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2);
    dqi.queueFamilyIndex = ctx->qfi;
    dqi.queueIndex = 0;
    vkGetDeviceQueue2(ctx->device, &dqi, &queue);
    COV_TRUE(queue == ctx->queue);
    COV_OK(vkDeviceWaitIdle(ctx->device));
}

static int image_create(struct test_ctx *ctx, VkImage *image, VkDeviceMemory *mem)
{
    VkImageCreateInfo ici;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo mai;
    uint32_t mt;
    *image = VK_NULL_HANDLE;
    *mem = VK_NULL_HANDLE;
    ZERO_INFO(&ici, VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent.width = 4;
    ici.extent.height = 4;
    ici.extent.depth = 1;
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_LINEAR;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    if (vkCreateImage(ctx->device, &ici, NULL, image) != VK_SUCCESS)
        return -1;
    vkGetImageMemoryRequirements(ctx->device, *image, &req);
    mt = find_memory_type(ctx->phys, req.memoryTypeBits);
    ZERO_INFO(&mai, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mt;
    if (vkAllocateMemory(ctx->device, &mai, NULL, mem) != VK_SUCCESS ||
        vkBindImageMemory(ctx->device, *image, *mem, 0) != VK_SUCCESS)
        return -1;
    return 0;
}

static void cov_transfers(struct test_ctx *ctx, struct cov *c)
{
    buffer_t src, dst;
    VkImage img = VK_NULL_HANDLE, img2 = VK_NULL_HANDLE;
    VkDeviceMemory imem = VK_NULL_HANDLE, imem2 = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkBufferView bview = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pci;
    VkCommandBufferAllocateInfo cbai;
    VkCommandBufferBeginInfo cbbi;
    VkBufferCopy bc;
    VkBufferImageCopy bic;
    VkImageCopy ic;
    VkImageViewCreateInfo vci;
    VkSamplerCreateInfo sci;
    VkBufferViewCreateInfo bvci;
    VkImageSubresource sub;
    VkSubresourceLayout sl;
    VkMappedMemoryRange mr;
    VkDeviceSize committed = 0;
    VkBufferDeviceAddressInfo bai;
    unsigned char pixels[64], back[64];
    uint32_t i;

    memset(&src, 0, sizeof(src));
    memset(&dst, 0, sizeof(dst));
    for (i = 0; i < 64; i++) pixels[i] = (unsigned char)i;
    if (buffer_create(ctx, &src, 64, VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0 ||
        buffer_create(ctx, &dst, 64, VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0)
        goto out;
    memcpy(src.map, pixels, 64);
    ZERO_INFO(&mr, VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
    mr.memory = src.mem;
    mr.size = VK_WHOLE_SIZE;
    COV_OK(vkFlushMappedMemoryRanges(ctx->device, 1, &mr));
    COV_OK(vkInvalidateMappedMemoryRanges(ctx->device, 1, &mr));
    vkGetDeviceMemoryCommitment(ctx->device, src.mem, &committed);
    COV_TRUE(committed == 64);
    ZERO_INFO(&bai, VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
    bai.buffer = src.buf;
    COV_TRUE(vkGetBufferDeviceAddress(ctx->device, &bai) != 0);
    COV_TRUE(vkGetBufferOpaqueCaptureAddress(ctx->device, &bai) != 0);
    ZERO_INFO(&bvci, VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO);
    bvci.buffer = src.buf;
    bvci.format = VK_FORMAT_R32_SFLOAT;
    bvci.range = VK_WHOLE_SIZE;
    COV_OK(vkCreateBufferView(ctx->device, &bvci, NULL, &bview));

    ZERO_INFO(&pci, VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    pci.queueFamilyIndex = ctx->qfi;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    COV_OK(vkCreateCommandPool(ctx->device, &pci, NULL, &pool));
    if (!pool) goto out;
    ZERO_INFO(&cbai, VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    cbai.commandPool = pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    COV_OK(vkAllocateCommandBuffers(ctx->device, &cbai, &cmd));
    if (!cmd) goto out;
    ZERO_INFO(&cbbi, VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    COV_OK(vkBeginCommandBuffer(cmd, &cbbi));
    vkCmdFillBuffer(cmd, dst.buf, 0, 64, 0x5a5a5a5au);
    bc.srcOffset = bc.dstOffset = 0;
    bc.size = 64;
    vkCmdCopyBuffer(cmd, dst.buf, src.buf, 1, &bc);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 0, NULL, 0, NULL);
    coverage_record_submit(ctx, c, cmd);
    for (i = 0; i < 64; i += 4)
        if (*(uint32_t *)((unsigned char *)src.map + i) != 0x5a5a5a5au) break;
    COV_TRUE(i == 64);
    COV_OK(vkResetCommandBuffer(cmd, 0));
    COV_OK(vkBeginCommandBuffer(cmd, &cbbi));
    vkCmdUpdateBuffer(cmd, src.buf, 0, 64, pixels);
    vkCmdCopyBuffer(cmd, src.buf, dst.buf, 1, &bc);
    coverage_record_submit(ctx, c, cmd);
    COV_TRUE(memcmp(dst.map, pixels, 64) == 0);

    if (image_create(ctx, &img, &imem) != 0 || image_create(ctx, &img2, &imem2) != 0)
        goto out;
    memset(&sub, 0, sizeof(sub));
    sub.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vkGetImageSubresourceLayout(ctx->device, img, &sub, &sl);
    COV_TRUE(sl.rowPitch == 16 && sl.size == 64);
    ZERO_INFO(&vci, VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    vci.image = img;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    COV_OK(vkCreateImageView(ctx->device, &vci, NULL, &view));
    ZERO_INFO(&sci, VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    sci.magFilter = VK_FILTER_NEAREST;
    sci.minFilter = VK_FILTER_NEAREST;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    COV_OK(vkCreateSampler(ctx->device, &sci, NULL, &sampler));

    memset(&bic, 0, sizeof(bic));
    bic.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bic.imageSubresource.layerCount = 1;
    bic.imageExtent.width = bic.imageExtent.height = 4;
    bic.imageExtent.depth = 1;
    memset(&ic, 0, sizeof(ic));
    ic.srcSubresource = ic.dstSubresource = bic.imageSubresource;
    ic.extent = bic.imageExtent;
    COV_OK(vkResetCommandPool(ctx->device, pool, 0));
    COV_OK(vkBeginCommandBuffer(cmd, &cbbi));
    vkCmdCopyBufferToImage(cmd, src.buf, img, VK_IMAGE_LAYOUT_GENERAL, 1, &bic);
    vkCmdCopyImage(cmd, img, VK_IMAGE_LAYOUT_GENERAL, img2, VK_IMAGE_LAYOUT_GENERAL, 1, &ic);
    vkCmdCopyImageToBuffer(cmd, img2, VK_IMAGE_LAYOUT_GENERAL, dst.buf, 1, &bic);
    coverage_record_submit(ctx, c, cmd);
    COV_TRUE(memcmp(dst.map, pixels, 64) == 0);
    memcpy(back, dst.map, 64);
    COV_TRUE(memcmp(back, pixels, 64) == 0);
    vkTrimCommandPool(ctx->device, pool, 0);
out:
    if (bview) vkDestroyBufferView(ctx->device, bview, NULL);
    if (sampler) vkDestroySampler(ctx->device, sampler, NULL);
    if (view) vkDestroyImageView(ctx->device, view, NULL);
    if (pool) vkDestroyCommandPool(ctx->device, pool, NULL);
    if (img2) vkDestroyImage(ctx->device, img2, NULL);
    if (img) vkDestroyImage(ctx->device, img, NULL);
    if (imem2) vkFreeMemory(ctx->device, imem2, NULL);
    if (imem) vkFreeMemory(ctx->device, imem, NULL);
    buffer_destroy(ctx, &dst);
    buffer_destroy(ctx, &src);
}

static void cov_descriptors(struct test_ctx *ctx, struct cov *c)
{
    buffer_t b;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet sets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDescriptorSetLayoutBinding binding;
    VkDescriptorSetLayoutCreateInfo lci;
    VkDescriptorSetLayoutSupport support;
    VkDescriptorPoolSize psz;
    VkDescriptorPoolCreateInfo pci;
    VkDescriptorSetAllocateInfo ai;
    VkDescriptorSetLayout layouts[2];
    VkDescriptorBufferInfo bi;
    VkWriteDescriptorSet wr;
    VkCopyDescriptorSet cp;
    memset(&b, 0, sizeof(b));
    if (buffer_create(ctx, &b, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) != 0)
        return;
    memset(&binding, 0, sizeof(binding));
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    ZERO_INFO(&lci, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    lci.bindingCount = 1;
    lci.pBindings = &binding;
    COV_OK(vkCreateDescriptorSetLayout(ctx->device, &lci, NULL, &layout));
    ZERO_INFO(&support, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT);
    vkGetDescriptorSetLayoutSupport(ctx->device, &lci, &support);
    COV_TRUE(support.supported == VK_TRUE);
    psz.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    psz.descriptorCount = 2;
    ZERO_INFO(&pci, VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    pci.maxSets = 2;
    pci.poolSizeCount = 1;
    pci.pPoolSizes = &psz;
    COV_OK(vkCreateDescriptorPool(ctx->device, &pci, NULL, &pool));
    if (!layout || !pool) goto out;
    layouts[0] = layouts[1] = layout;
    ZERO_INFO(&ai, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    ai.descriptorPool = pool;
    ai.descriptorSetCount = 2;
    ai.pSetLayouts = layouts;
    COV_OK(vkAllocateDescriptorSets(ctx->device, &ai, sets));
    memset(&bi, 0, sizeof(bi));
    bi.buffer = b.buf;
    bi.range = VK_WHOLE_SIZE;
    ZERO_INFO(&wr, VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    wr.dstSet = sets[0];
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    wr.pBufferInfo = &bi;
    ZERO_INFO(&cp, VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET);
    cp.srcSet = sets[0];
    cp.dstSet = sets[1];
    cp.descriptorCount = 1;
    vkUpdateDescriptorSets(ctx->device, 1, &wr, 1, &cp);
    c->pass++;
    COV_OK(vkFreeDescriptorSets(ctx->device, pool, 1, &sets[1]));
    COV_OK(vkResetDescriptorPool(ctx->device, pool, 0));
out:
    if (pool) vkDestroyDescriptorPool(ctx->device, pool, NULL);
    if (layout) vkDestroyDescriptorSetLayout(ctx->device, layout, NULL);
    buffer_destroy(ctx, &b);
}

static void cov_sync(struct test_ctx *ctx, struct cov *c)
{
    VkFence fence = VK_NULL_HANDLE;
    VkEvent event = VK_NULL_HANDLE;
    VkSemaphore sem = VK_NULL_HANDLE;
    VkQueryPool qp = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFenceCreateInfo fci;
    VkEventCreateInfo eci;
    VkSemaphoreTypeCreateInfo sti;
    VkSemaphoreCreateInfo sci;
    VkSemaphoreSignalInfo ssi;
    VkSemaphoreWaitInfo swi;
    VkQueryPoolCreateInfo qpci;
    VkCommandPoolCreateInfo cpci;
    VkCommandBufferAllocateInfo cbai;
    VkCommandBufferBeginInfo cbbi;
    uint64_t value = 0, qresult[2] = { 0, 0 };

    ZERO_INFO(&fci, VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    COV_OK(vkCreateFence(ctx->device, &fci, NULL, &fence));
    if (fence) {
        COV_ERR(vkGetFenceStatus(ctx->device, fence), VK_NOT_READY);
        COV_ERR(vkWaitForFences(ctx->device, 1, &fence, VK_TRUE, 0), VK_TIMEOUT);
        COV_OK(vkQueueSubmit(ctx->queue, 0, NULL, fence));
        COV_OK(vkGetFenceStatus(ctx->device, fence));
        COV_OK(vkWaitForFences(ctx->device, 1, &fence, VK_TRUE, 0));
        COV_OK(vkResetFences(ctx->device, 1, &fence));
    }
    ZERO_INFO(&eci, VK_STRUCTURE_TYPE_EVENT_CREATE_INFO);
    COV_OK(vkCreateEvent(ctx->device, &eci, NULL, &event));
    if (event) {
        COV_ERR(vkGetEventStatus(ctx->device, event), VK_EVENT_RESET);
        COV_OK(vkSetEvent(ctx->device, event));
        COV_ERR(vkGetEventStatus(ctx->device, event), VK_EVENT_SET);
        COV_OK(vkResetEvent(ctx->device, event));
    }
    ZERO_INFO(&sti, VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
    sti.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    sti.initialValue = 3;
    ZERO_INFO(&sci, VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    sci.pNext = &sti;
    COV_OK(vkCreateSemaphore(ctx->device, &sci, NULL, &sem));
    if (sem) {
        COV_OK(vkGetSemaphoreCounterValue(ctx->device, sem, &value));
        COV_TRUE(value == 3);
        ZERO_INFO(&ssi, VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO);
        ssi.semaphore = sem;
        ssi.value = 7;
        COV_OK(vkSignalSemaphore(ctx->device, &ssi));
        ZERO_INFO(&swi, VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
        swi.semaphoreCount = 1;
        swi.pSemaphores = &sem;
        swi.pValues = &ssi.value;
        COV_OK(vkWaitSemaphores(ctx->device, &swi, 0));
    }
    ZERO_INFO(&qpci, VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
    qpci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qpci.queryCount = 1;
    COV_OK(vkCreateQueryPool(ctx->device, &qpci, NULL, &qp));
    ZERO_INFO(&cpci, VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    cpci.queueFamilyIndex = ctx->qfi;
    COV_OK(vkCreateCommandPool(ctx->device, &cpci, NULL, &pool));
    if (!qp || !pool) goto out;
    ZERO_INFO(&cbai, VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    cbai.commandPool = pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    COV_OK(vkAllocateCommandBuffers(ctx->device, &cbai, &cmd));
    ZERO_INFO(&cbbi, VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    COV_OK(vkBeginCommandBuffer(cmd, &cbbi));
    vkCmdResetQueryPool(cmd, qp, 0, 1);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, qp, 0);
    if (event) vkCmdSetEvent(cmd, event, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    coverage_record_submit(ctx, c, cmd);
    COV_OK(vkGetQueryPoolResults(ctx->device, qp, 0, 1, sizeof(qresult), qresult,
                                 sizeof(uint64_t), VK_QUERY_RESULT_64_BIT |
                                 VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
    COV_TRUE(qresult[1] == 1);
    if (event) COV_ERR(vkGetEventStatus(ctx->device, event), VK_EVENT_SET);
out:
    if (qp) vkDestroyQueryPool(ctx->device, qp, NULL);
    if (pool) vkDestroyCommandPool(ctx->device, pool, NULL);
    if (sem) vkDestroySemaphore(ctx->device, sem, NULL);
    if (event) vkDestroyEvent(ctx->device, event, NULL);
    if (fence) vkDestroyFence(ctx->device, fence, NULL);
}

static int run_coverage(struct test_ctx *ctx)
{
    struct cov c;
    c.pass = 0;
    c.fail = 0;
    cov_queries(ctx, &c);
    cov_transfers(ctx, &c);
    cov_descriptors(ctx, &c);
    cov_sync(ctx, &c);
    printf("  coverage: %d/%d API checks passed\n", c.pass, c.pass + c.fail);
    return c.fail;
}

static int init_ctx(struct test_ctx *ctx, int force_cpu)
{
    VkApplicationInfo app;
    VkInstanceCreateInfo ici;
    VkQueueFamilyProperties qfam[16];
    VkDeviceQueueCreateInfo qci;
    VkDeviceCreateInfo dci;
    VkPhysicalDeviceProperties props;
    uint32_t n = 0, nq = 16, i;
    float priority = 1.0f;
    memset(ctx, 0, sizeof(*ctx));
    ZERO_INFO(&app, VK_STRUCTURE_TYPE_APPLICATION_INFO);
    app.pApplicationName = "vktest";
    app.apiVersion = VK_API_VERSION_1_3;
    ZERO_INFO(&ici, VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, NULL, &ctx->instance) != VK_SUCCESS ||
        vkEnumeratePhysicalDevices(ctx->instance, &n, NULL) != VK_SUCCESS || n == 0)
        return -1;
    n = 1;
    if (vkEnumeratePhysicalDevices(ctx->instance, &n, &ctx->phys) != VK_SUCCESS)
        return -1;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx->phys, &nq, qfam);
    for (i = 0; i < nq; i++)
        if (qfam[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { ctx->qfi = i; break; }
    ZERO_INFO(&qci, VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    qci.queueFamilyIndex = ctx->qfi;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    ZERO_INFO(&dci, VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    setenv("EWOK_VULKAN_CPU", force_cpu ? "1" : "0", 1);
    if (vkCreateDevice(ctx->phys, &dci, NULL, &ctx->device) != VK_SUCCESS)
        return -1;
    vkGetDeviceQueue(ctx->device, ctx->qfi, 0, &ctx->queue);
    vkGetPhysicalDeviceProperties(ctx->phys, &props);
    ctx->is_gpu = !force_cpu &&
                  (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ||
                   props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
    if (!force_cpu)
        printf("vktest: %s (Vulkan %u.%u.%u)\n", props.deviceName,
               VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
               VK_VERSION_PATCH(props.apiVersion));
    return ctx->queue ? 0 : -1;
}

int main(void)
{
    struct test_ctx ctx;
    float *sx = NULL, *sy0 = NULL, *sy_ref = NULL;
    float *ma = NULL, *mb = NULL, *mc = NULL;
    float *tin = NULL, *tout = NULL;
    const float *inputs[3];
    double times[2][3] = { { -1.0, -1.0, -1.0 }, { -1.0, -1.0, -1.0 } };
    int fails = 0, i, j, k, pass, npass = 1;
    setvbuf(stdout, NULL, _IONBF, 0);
    memset(&ctx, 0, sizeof(ctx));
    sx = malloc(SAXPY_N * sizeof(float));
    sy0 = malloc(SAXPY_N * sizeof(float));
    sy_ref = malloc(SAXPY_N * sizeof(float));
    ma = malloc(MATMUL_N * MATMUL_N * sizeof(float));
    mb = malloc(MATMUL_N * MATMUL_N * sizeof(float));
    mc = malloc(MATMUL_N * MATMUL_N * sizeof(float));
    tin = malloc(MATH_N * sizeof(float));
    tout = malloc(MATH_N * sizeof(float));
    if (!sx || !sy0 || !sy_ref || !ma || !mb || !mc || !tin || !tout) {
        printf("vktest: host allocation failed\n");
        fails++;
        goto out;
    }
    for (i = 0; i < SAXPY_N; i++) {
        float v;
        sx[i] = (float)(i % 1000) * 0.01f;
        sy0[i] = (float)(SAXPY_N - i) * 0.25f;
        v = sy0[i];
        for (j = 0; j < BENCH_ITERS + 1; j++) v = 2.5f * sx[i] + v;
        sy_ref[i] = v;
    }
    for (i = 0; i < MATMUL_N * MATMUL_N; i++) {
        ma[i] = (float)((i * 7) % 13) * 0.5f - 3.0f;
        mb[i] = (float)((i * 5) % 11) * 0.25f - 1.0f;
    }
    for (i = 0; i < MATMUL_N; i++)
        for (j = 0; j < MATMUL_N; j++) {
            float sum = 0.0f;
            for (k = 0; k < MATMUL_N; k++)
                sum += ma[i * MATMUL_N + k] * mb[k * MATMUL_N + j];
            mc[i * MATMUL_N + j] = sum;
        }
    for (i = 0; i < MATH_N; i++) {
        float v = (float)((i * 37) % 101) * 0.125f - 6.0f;
        float acc = 0.0f;
        tin[i] = v;
        for (j = 0; j < MATH_INNER; j++) {
            acc += (float)(sin(v) * cos(v) + sqrt(fabs(v) + 1.0));
            v = acc * 0.001f + v;
        }
        tout[i] = acc;
    }
    for (pass = 0; pass < npass; pass++) {
        if (init_ctx(&ctx, pass) != 0) {
            printf("vktest: failed to create instance/device/queue\n");
            fails++;
            goto out;
        }
        if (pass == 0) npass = ctx.is_gpu ? 2 : 1;
        printf("\n=== %s backend ===\n", ctx.is_gpu ? "GPU" : "CPU");
        inputs[0] = sx; inputs[1] = sy0; inputs[2] = NULL;
        times[pass][0] = run_bench(&ctx, "saxpy", kSpirvSaxpy,
                         sizeof(kSpirvSaxpy), 2, inputs, 1, SAXPY_N,
                         SAXPY_N / 4 / SAXPY_LSZ, 1, sy_ref, 1e-2f);
        inputs[0] = ma; inputs[1] = mb; inputs[2] = NULL;
        times[pass][1] = run_bench(&ctx, "matmul", kSpirvMatmul,
                         sizeof(kSpirvMatmul), 3, inputs, 2,
                         MATMUL_N * MATMUL_N,
                         (MATMUL_N + MATMUL_LSZ - 1) / MATMUL_LSZ, MATMUL_N,
                         mc, 1e-2f);
        inputs[0] = tin; inputs[1] = NULL;
        times[pass][2] = run_bench(&ctx, "math", kSpirvMath,
                         sizeof(kSpirvMath), 2, inputs, 1, MATH_N,
                         MATH_N / MATH_LSZ, 1, tout, 1e-2f);
        for (i = 0; i < 3; i++) if (times[pass][i] < 0) fails++;
        if (pass == 0) fails += run_coverage(&ctx);
        vkDestroyDevice(ctx.device, NULL);
        vkDestroyInstance(ctx.instance, NULL);
        memset(&ctx, 0, sizeof(ctx));
    }
    if (npass == 2) {
        const char *tags[3] = { "saxpy", "matmul", "math" };
        printf("\n=== summary (ms/dispatch, lower is better) ===\n");
        printf("%-10s %12s %12s %10s\n", "bench", "CPU", "GPU", "speedup");
        for (i = 0; i < 3; i++) {
            double gpu = times[0][i], cpu = times[1][i];
            if (cpu > 0 && gpu > 0)
                printf("%-10s %12.2f %12.2f %9.1fx\n",
                       tags[i], cpu, gpu, cpu / gpu);
            else
                printf("%-10s %12s %12s %10s\n", tags[i],
                       cpu > 0 ? "done" : "n/a", gpu > 0 ? "done" : "n/a", "-");
        }
    }
out:
    free(sx); free(sy0); free(sy_ref);
    free(ma); free(mb); free(mc);
    free(tin); free(tout);
    if (ctx.device) vkDestroyDevice(ctx.device, NULL);
    if (ctx.instance) vkDestroyInstance(ctx.instance, NULL);
    printf("vktest %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
