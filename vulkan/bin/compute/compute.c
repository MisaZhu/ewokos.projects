/*
 * compute.c - a self-contained vkCmdDispatch smoke test (the cltest/saxpy
 * analog).  It embeds a tiny GLCompute SPIR-V module computing out[i] = x[i]*y[i]
 * over three storage-buffer bindings (set 0, bindings 0/1/2, LocalSize 1x1x1),
 * then walks the whole Vulkan compute path against the software device:
 *
 *   instance -> physical device -> device + compute queue
 *   3 storage buffers + host-visible memory (mapped, filled)
 *   shader module -> descriptor set layout -> pipeline layout -> compute pipeline
 *   descriptor pool -> set -> update (3 buffer infos)
 *   command pool -> buffer -> bind pipeline + descriptor set -> vkCmdDispatch
 *   fence -> vkQueueSubmit -> vkWaitForFences -> map out[] -> verify
 *
 * It runs on every machine (the software device is always present).  On raspi5
 * the same dispatch is transparently offloaded to the V3D CSD by the GPU back-end
 * when the shader is inside the GPU subset -- the results are identical either
 * way, only the timing differs, so the printed ms/launch reports whichever path
 * the runtime selected.
 *
 * Usage: vkcompute [N] [iters]
 *   N      item count / dispatch grid X (default 256, capped to 65535 by
 *          maxComputeWorkGroupCount[0] since LocalSize is 1x1x1);
 *   iters  timed re-dispatch count (default 1).
 * Two timings are reported: the COLD first dispatch (which on the GPU path also
 * pays the one-time on-device mesa compile + program upload) and the STEADY
 * per-dispatch average over `iters` re-submits of the same command buffer (the
 * program is already uploaded, so this isolates attach + IPC + CSD exec).  A
 * large N with several iters is what lets the GPU's throughput overcome its
 * fixed per-launch overhead; at the default tiny N the CPU software device wins.
 */
#include <vulkan/vulkan.h>
#include <ewoksys/kernel_tic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* out[i] = x[i] * y[i]; set=0 bindings 0(x) 1(y) 2(out); LocalSize 1,1,1.
 * Generated from the SPIR-V assembly the runtime's own interpreter + transpiler
 * are validated against; 168 words, bound 64. */
static const uint32_t kSpirvSaxpy[168] = {
    0x07230203, 0x00010000, 0x00000000, 0x00000040, 0x00000000, 0x00020011,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0006000f, 0x00000005,
    0x00000028, 0x6e69616d, 0x00000000, 0x00000017, 0x00060010, 0x00000028,
    0x00000011, 0x00000001, 0x00000001, 0x00000001, 0x00040047, 0x00000006,
    0x00000006, 0x00000004, 0x00030047, 0x00000007, 0x00000002, 0x00050048,
    0x00000007, 0x00000000, 0x00000023, 0x00000000, 0x00040047, 0x00000014,
    0x00000022, 0x00000000, 0x00040047, 0x00000014, 0x00000021, 0x00000000,
    0x00040047, 0x00000015, 0x00000022, 0x00000000, 0x00040047, 0x00000015,
    0x00000021, 0x00000001, 0x00040047, 0x00000016, 0x00000022, 0x00000000,
    0x00040047, 0x00000016, 0x00000021, 0x00000002, 0x00040047, 0x00000017,
    0x0000000b, 0x0000001c, 0x00020013, 0x00000001, 0x00030021, 0x00000002,
    0x00000001, 0x00040015, 0x00000003, 0x00000020, 0x00000000, 0x00030016,
    0x00000004, 0x00000020, 0x0003001d, 0x00000006, 0x00000004, 0x0003001e,
    0x00000007, 0x00000006, 0x00040020, 0x00000008, 0x0000000c, 0x00000007,
    0x00040020, 0x00000009, 0x0000000c, 0x00000004, 0x00040017, 0x0000000b,
    0x00000003, 0x00000003, 0x00040020, 0x0000000c, 0x00000001, 0x0000000b,
    0x0004003b, 0x00000008, 0x00000014, 0x0000000c, 0x0004003b, 0x00000008,
    0x00000015, 0x0000000c, 0x0004003b, 0x00000008, 0x00000016, 0x0000000c,
    0x0004003b, 0x0000000c, 0x00000017, 0x00000001, 0x0004002b, 0x00000003,
    0x0000001e, 0x00000000, 0x00050036, 0x00000001, 0x00000028, 0x00000000,
    0x00000002, 0x000200f8, 0x00000029, 0x0004003d, 0x0000000b, 0x0000002a,
    0x00000017, 0x00050051, 0x00000003, 0x0000002b, 0x0000002a, 0x00000000,
    0x00060041, 0x00000009, 0x0000002c, 0x00000014, 0x0000001e, 0x0000002b,
    0x0004003d, 0x00000004, 0x0000002d, 0x0000002c, 0x00060041, 0x00000009,
    0x0000002e, 0x00000015, 0x0000001e, 0x0000002b, 0x0004003d, 0x00000004,
    0x0000002f, 0x0000002e, 0x00050085, 0x00000004, 0x00000030, 0x0000002d,
    0x0000002f, 0x00060041, 0x00000009, 0x00000031, 0x00000016, 0x0000001e,
    0x0000002b, 0x0003003e, 0x00000031, 0x00000030, 0x000100fd, 0x00010038
};

#define DEFAULT_ITEMS 256u
/* One dispatch grid is capped by maxComputeWorkGroupCount[0] (65535); the
 * kernel uses only GlobalInvocationId.x with LocalSize 1x1x1, so N cannot
 * exceed the X work-group count in a single dispatch. */
#define MAX_ITEMS     65535u

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

static uint32_t find_memory_type(const VkPhysicalDeviceMemoryProperties *mem,
                                 uint32_t typeFilter, VkMemoryPropertyFlags want)
{
    uint32_t i;
    for (i = 0; i < mem->memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) &&
            (mem->memoryTypes[i].propertyFlags & want) == want)
            return i;
    }
    /* fall back to any host-visible type in the filter */
    for (i = 0; i < mem->memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) &&
            (mem->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            return i;
    }
    return 0xffffffffu;
}

/* one storage buffer + its host-visible backing memory */
typedef struct {
    VkBuffer       buf;
    VkDeviceMemory mem;
    void          *map;
    VkDeviceSize   size;
} sb_t;

static int sb_create(VkDevice dev, VkPhysicalDevice pd, sb_t *sb, VkDeviceSize size)
{
    VkBufferCreateInfo bci;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo mai;
    VkPhysicalDeviceMemoryProperties mem;
    uint32_t mt;

    memset(sb, 0, sizeof(*sb));
    sb->size = size;

    memset(&bci, 0, sizeof(bci));
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &bci, NULL, &sb->buf) != VK_SUCCESS) {
        printf("  vkCreateBuffer failed\n");
        return -1;
    }

    vkGetBufferMemoryRequirements(dev, sb->buf, &req);
    vkGetPhysicalDeviceMemoryProperties(pd, &mem);
    mt = find_memory_type(&mem, req.memoryTypeBits,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == 0xffffffffu) { printf("  no host-visible memory type\n"); return -1; }

    memset(&mai, 0, sizeof(mai));
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mt;
    if (vkAllocateMemory(dev, &mai, NULL, &sb->mem) != VK_SUCCESS) {
        printf("  vkAllocateMemory failed\n");
        return -1;
    }
    if (vkBindBufferMemory(dev, sb->buf, sb->mem, 0) != VK_SUCCESS) {
        printf("  vkBindBufferMemory failed\n");
        return -1;
    }
    if (vkMapMemory(dev, sb->mem, 0, size, 0, &sb->map) != VK_SUCCESS) {
        printf("  vkMapMemory failed\n");
        return -1;
    }
    return 0;
}

static void sb_destroy(VkDevice dev, sb_t *sb)
{
    if (!sb) return;
    if (sb->map) vkUnmapMemory(dev, sb->mem);
    if (sb->buf) vkDestroyBuffer(dev, sb->buf, NULL);
    if (sb->mem) vkFreeMemory(dev, sb->mem, NULL);
}

int main(int argc, char **argv)
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkInstanceCreateInfo ici;
    VkApplicationInfo app;
    VkDeviceCreateInfo dci;
    VkDeviceQueueCreateInfo qci;
    VkQueueFamilyProperties qfam[16];
    uint32_t nphys = 0, nq = 16, qfi = 0, i;
    float prio = 1.0f;
    VkResult rc;

    sb_t xs, ys, os;
    VkShaderModule module = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkPipelineLayout pl = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    VkDescriptorSet dset = VK_NULL_HANDLE;
    VkCommandPool cpool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    int ret = 1;
    uint64_t t0, t1;
    double ms, cold_ms;
    uint32_t n = DEFAULT_ITEMS;   /* item count (argv[1]), capped to one grid */
    uint32_t iters = 1;           /* timed re-dispatch count (argv[2]) */
    uint32_t it;

    if (argc > 1) { long v = strtol(argv[1], NULL, 10); if (v > 0) n = (uint32_t)v; }
    if (argc > 2) { long v = strtol(argv[2], NULL, 10); if (v > 0) iters = (uint32_t)v; }
    if (n > MAX_ITEMS) n = MAX_ITEMS;

    printf("vkcompute: %u-item saxpy (out[i] = x[i]*y[i]), %u timed dispatch(es)\n",
           n, iters);

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "vkcompute";
    app.apiVersion = VK_API_VERSION_1_3;
    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, NULL, &instance) != VK_SUCCESS) {
        printf("vkcompute: vkCreateInstance failed\n");
        return 1;
    }
    if (vkEnumeratePhysicalDevices(instance, &nphys, NULL) != VK_SUCCESS || nphys == 0) {
        printf("vkcompute: no physical devices\n");
        vkDestroyInstance(instance, NULL);
        return 1;
    }
    nphys = 1;
    vkEnumeratePhysicalDevices(instance, &nphys, &phys);

    /* pick a queue family that supports compute */
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &nq, qfam);
    for (i = 0; i < nq; i++) {
        if (qfam[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { qfi = i; break; }
    }

    memset(&qci, 0, sizeof(qci));
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = qfi;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    memset(&dci, 0, sizeof(dci));
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    if (vkCreateDevice(phys, &dci, NULL, &device) != VK_SUCCESS) {
        printf("vkcompute: vkCreateDevice failed\n");
        vkDestroyInstance(instance, NULL);
        return 1;
    }
    vkGetDeviceQueue(device, qfi, 0, &queue);

    /* ---- storage buffers -------------------------------------------------- */
    if (sb_create(device, phys, &xs, n * sizeof(float)) != 0 ||
        sb_create(device, phys, &ys, n * sizeof(float)) != 0 ||
        sb_create(device, phys, &os, n * sizeof(float)) != 0)
        goto cleanup;

    {
        float *x = (float *)xs.map, *y = (float *)ys.map, *o = (float *)os.map;
        for (i = 0; i < n; i++) {
            x[i] = (float)(i + 1);
            y[i] = (float)(i + 1) * 0.5f;
            o[i] = -1.0f;
        }
    }

    /* ---- shader module ---------------------------------------------------- */
    {
        VkShaderModuleCreateInfo smci;
        memset(&smci, 0, sizeof(smci));
        smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = sizeof(kSpirvSaxpy);
        smci.pCode = kSpirvSaxpy;
        if (vkCreateShaderModule(device, &smci, NULL, &module) != VK_SUCCESS) {
            printf("vkcompute: vkCreateShaderModule failed\n");
            goto cleanup;
        }
    }

    /* ---- descriptor set layout (3 storage buffers, bindings 0/1/2) -------- */
    {
        VkDescriptorSetLayoutBinding b[3];
        VkDescriptorSetLayoutCreateInfo dslci;
        memset(b, 0, sizeof(b));
        for (i = 0; i < 3; i++) {
            b[i].binding = i;
            b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[i].descriptorCount = 1;
            b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        memset(&dslci, 0, sizeof(dslci));
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = 3;
        dslci.pBindings = b;
        if (vkCreateDescriptorSetLayout(device, &dslci, NULL, &dsl) != VK_SUCCESS) {
            printf("vkcompute: vkCreateDescriptorSetLayout failed\n");
            goto cleanup;
        }
    }

    /* ---- pipeline layout + compute pipeline ------------------------------- */
    {
        VkPipelineLayoutCreateInfo plci;
        VkComputePipelineCreateInfo cpci;
        VkPipelineShaderStageCreateInfo stage;

        memset(&plci, 0, sizeof(plci));
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &dsl;
        if (vkCreatePipelineLayout(device, &plci, NULL, &pl) != VK_SUCCESS) {
            printf("vkcompute: vkCreatePipelineLayout failed\n");
            goto cleanup;
        }

        memset(&stage, 0, sizeof(stage));
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = module;
        stage.pName = "main";

        memset(&cpci, 0, sizeof(cpci));
        cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage = stage;
        cpci.layout = pl;
        rc = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, NULL, &pipeline);
        if (rc != VK_SUCCESS) {
            printf("vkcompute: vkCreateComputePipelines failed (%d)\n", (int)rc);
            goto cleanup;
        }
    }

    /* ---- descriptor pool + set + update ----------------------------------- */
    {
        VkDescriptorPoolSize psz;
        VkDescriptorPoolCreateInfo dpci;
        VkDescriptorSetAllocateInfo dsai;
        VkWriteDescriptorSet wr[3];
        VkDescriptorBufferInfo bi[3];
        VkBuffer bufs[3];
        bufs[0] = xs.buf; bufs[1] = ys.buf; bufs[2] = os.buf;

        psz.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        psz.descriptorCount = 3;
        memset(&dpci, 0, sizeof(dpci));
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &psz;
        if (vkCreateDescriptorPool(device, &dpci, NULL, &dpool) != VK_SUCCESS) {
            printf("vkcompute: vkCreateDescriptorPool failed\n");
            goto cleanup;
        }
        memset(&dsai, 0, sizeof(dsai));
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = dpool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &dsl;
        if (vkAllocateDescriptorSets(device, &dsai, &dset) != VK_SUCCESS) {
            printf("vkcompute: vkAllocateDescriptorSets failed\n");
            goto cleanup;
        }

        memset(bi, 0, sizeof(bi));
        memset(wr, 0, sizeof(wr));
        for (i = 0; i < 3; i++) {
            bi[i].buffer = bufs[i];
            bi[i].offset = 0;
            bi[i].range = VK_WHOLE_SIZE;
            wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wr[i].dstSet = dset;
            wr[i].dstBinding = i;
            wr[i].descriptorCount = 1;
            wr[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            wr[i].pBufferInfo = &bi[i];
        }
        vkUpdateDescriptorSets(device, 3, wr, 0, NULL);
    }

    /* ---- command buffer --------------------------------------------------- */
    {
        VkCommandPoolCreateInfo cpci;
        VkCommandBufferAllocateInfo cbai;
        VkCommandBufferBeginInfo bi;

        memset(&cpci, 0, sizeof(cpci));
        cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpci.queueFamilyIndex = qfi;
        cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (vkCreateCommandPool(device, &cpci, NULL, &cpool) != VK_SUCCESS) {
            printf("vkcompute: vkCreateCommandPool failed\n");
            goto cleanup;
        }
        memset(&cbai, 0, sizeof(cbai));
        cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool = cpool;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device, &cbai, &cmd) != VK_SUCCESS) {
            printf("vkcompute: vkAllocateCommandBuffers failed\n");
            goto cleanup;
        }
        memset(&bi, 0, sizeof(bi));
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(cmd, &bi) != VK_SUCCESS) {
            printf("vkcompute: vkBeginCommandBuffer failed\n");
            goto cleanup;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &dset, 0, NULL);
        vkCmdDispatch(cmd, n, 1, 1);   /* LocalSize 1x1x1 -> n groups */
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
            printf("vkcompute: vkEndCommandBuffer failed\n");
            goto cleanup;
        }
    }

    /* ---- fence + submit + wait -------------------------------------------- */
    {
        VkFenceCreateInfo fci;
        VkSubmitInfo si;
        memset(&fci, 0, sizeof(fci));
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (vkCreateFence(device, &fci, NULL, &fence) != VK_SUCCESS) {
            printf("vkcompute: vkCreateFence failed\n");
            goto cleanup;
        }
        memset(&si, 0, sizeof(si));
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;

        /* Cold dispatch: the first submit triggers the backend's lazy
         * compile+upload (on the GPU path), so it is timed separately from the
         * steady state below. */
        t0 = now_ns();
        if (vkQueueSubmit(queue, 1, &si, fence) != VK_SUCCESS) {
            printf("vkcompute: vkQueueSubmit failed\n");
            goto cleanup;
        }
        if (vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull) != VK_SUCCESS) {
            printf("vkcompute: vkWaitForFences failed/timeout\n");
            goto cleanup;
        }
        t1 = now_ns();
        cold_ms = (double)(t1 - t0) / 1.0e6;

        /* Steady state: re-submit the SAME command buffer `iters` times.  The
         * GPU program is already compiled+uploaded after the cold dispatch, so
         * this isolates the raw per-dispatch cost (attach + IPC + CSD exec)
         * from the one-time compile.  Each dispatch is idempotent
         * (out = x*y), so the verification below stays valid. */
        t0 = now_ns();
        for (it = 0; it < iters; it++) {
            vkResetFences(device, 1, &fence);
            if (vkQueueSubmit(queue, 1, &si, fence) != VK_SUCCESS) {
                printf("vkcompute: vkQueueSubmit (iter %u) failed\n", it);
                goto cleanup;
            }
            if (vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull) != VK_SUCCESS) {
                printf("vkcompute: vkWaitForFences (iter %u) failed/timeout\n", it);
                goto cleanup;
            }
        }
        t1 = now_ns();
        ms = (double)(t1 - t0) / 1.0e6;
        if (iters) ms /= (double)iters;   /* per-dispatch average */
    }

    /* ---- verify ------------------------------------------------------------ */
    {
        const float *x = (const float *)xs.map;
        const float *y = (const float *)ys.map;
        const float *o = (const float *)os.map;
        uint32_t bad = 0;
        for (i = 0; i < n; i++) {
            float want = x[i] * y[i];
            if (o[i] != want) {
                if (bad < 8)
                    printf("  MISMATCH [%u]: got %f want %f\n", i, o[i], want);
                bad++;
            }
        }
        printf("  cold dispatch:   %.3f ms  (first submit, incl. lazy compile/upload)\n",
               cold_ms);
        printf("  steady dispatch: %.3f ms avg  (%u items x %u dispatch(es))\n",
               ms, n, iters);
        printf("  out[0..3] = %.1f %.1f %.1f %.1f\n", o[0], o[1], o[2], o[3]);
        if (bad) {
            printf("vkcompute: FAIL (%u/%u mismatches)\n", bad, n);
            goto cleanup;
        }
        printf("vkcompute: PASS (all %u correct)\n", n);
        ret = 0;
    }

cleanup:
    if (fence)    vkDestroyFence(device, fence, NULL);
    if (cpool)    vkDestroyCommandPool(device, cpool, NULL);
    if (dpool)    vkDestroyDescriptorPool(device, dpool, NULL);
    if (pipeline) vkDestroyPipeline(device, pipeline, NULL);
    if (pl)       vkDestroyPipelineLayout(device, pl, NULL);
    if (dsl)      vkDestroyDescriptorSetLayout(device, dsl, NULL);
    if (module)   vkDestroyShaderModule(device, module, NULL);
    sb_destroy(device, &os);
    sb_destroy(device, &ys);
    sb_destroy(device, &xs);
    if (device)   vkDestroyDevice(device, NULL);
    if (instance) vkDestroyInstance(instance, NULL);
    return ret;
}
