/*
 * triangle.c - the graphics smoke test (a Vulkan triangle over an EwokOS
 * window).  It builds a graphics pipeline from two embedded SPIR-V stages -- a
 * vertex shader that emits a hard-coded 3-vertex RGB triangle positioned from
 * VertexIndex, and a fragment shader that passes the interpolated colour
 * straight through -- then renders into a swapchain image and presents it.
 *
 * Presentation rides the runtime's EwokOS WSI path: the app creates a
 * VkSurfaceKHR with vkCreateEwokOSSurfaceVK, registering a present callback, so
 * libvulkan.a never links libx/libgraph -- the app owns the window and blits the
 * rendered pixels into it.  The swapchain colour format is B8G8R8A8_UNORM whose
 * byte order (B,G,R,A) is bit-identical to the window graph's ARGB pixels on
 * little-endian, so present is a straight row copy.
 *
 * On every machine the software device can rasterise the triangle.  On raspi5
 * (PGL_G3D) this draw also qualifies for the GPU back-end: it is a plain
 * TRIANGLE_LIST into a single B8G8R8A8_UNORM attachment with no depth/blend and
 * a trivial VS/FS, so the runtime transpiles the two SPIR-V stages into a
 * geom-raster kernel pair (geom_kind 5) and dispatches them on the V3D CSD.  Any
 * step that falls outside the GPU subset -- or a kernel-compile/dispatch failure
 * -- transparently declines to the software rasteriser, so the presented pixels
 * are identical whether the GPU path runs or not.
 */
#include <vulkan/vulkan.h>
#include <x/x.h>
#include <x/xwin.h>
#include <x/xcntl.h>
#include <graph/graph.h>
#include <ewoksys/kernel_tic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TRI_W 480
#define TRI_H 320

/* ---- embedded SPIR-V (validated against the runtime's interpreter) -------- */
/* VS: VertexIndex -> position {(-.9,-.9),(0.9,-.9),(0,0.9)} + colour {R,G,B}. */
static const uint32_t kSpirvVs[179] = {
    0x07230203, 0x00010000, 0x00000000, 0x00000040, 0x00000000, 0x00020011,
    0x00000001, 0x0003000e, 0x00000000, 0x00000001, 0x0008000f, 0x00000000,
    0x00000028, 0x6e69616d, 0x00000000, 0x0000000a, 0x0000000b, 0x0000000c,
    0x00040047, 0x0000000a, 0x0000000b, 0x0000002a, 0x00040047, 0x0000000b,
    0x0000000b, 0x00000000, 0x00040047, 0x0000000c, 0x0000001e, 0x00000000,
    0x00020013, 0x00000001, 0x00030021, 0x00000002, 0x00000001, 0x00040015,
    0x00000003, 0x00000020, 0x00000000, 0x00030016, 0x00000004, 0x00000020,
    0x00020014, 0x00000006, 0x00040017, 0x00000005, 0x00000004, 0x00000004,
    0x00040020, 0x00000007, 0x00000001, 0x00000003, 0x00040020, 0x00000008,
    0x00000003, 0x00000005, 0x0004002b, 0x00000004, 0x00000014, 0x00000000,
    0x0004002b, 0x00000004, 0x00000015, 0x3f800000, 0x0004002b, 0x00000004,
    0x00000016, 0xbf666666, 0x0004002b, 0x00000004, 0x00000017, 0x3f666666,
    0x0004002b, 0x00000003, 0x00000018, 0x00000001, 0x0004002b, 0x00000003,
    0x00000019, 0x00000002, 0x0007002c, 0x00000005, 0x0000001b, 0x00000015,
    0x00000014, 0x00000014, 0x00000015, 0x0007002c, 0x00000005, 0x0000001c,
    0x00000014, 0x00000015, 0x00000014, 0x00000015, 0x0007002c, 0x00000005,
    0x0000001d, 0x00000014, 0x00000014, 0x00000015, 0x00000015, 0x0004003b,
    0x00000007, 0x0000000a, 0x00000001, 0x0004003b, 0x00000008, 0x0000000b,
    0x00000003, 0x0004003b, 0x00000008, 0x0000000c, 0x00000003, 0x00050036,
    0x00000001, 0x00000028, 0x00000000, 0x00000002, 0x000200f8, 0x00000029,
    0x0004003d, 0x00000003, 0x00000032, 0x0000000a, 0x000500aa, 0x00000006,
    0x00000033, 0x00000032, 0x00000018, 0x000500aa, 0x00000006, 0x00000034,
    0x00000032, 0x00000019, 0x000600a9, 0x00000004, 0x00000035, 0x00000033,
    0x00000017, 0x00000016, 0x000600a9, 0x00000004, 0x00000036, 0x00000034,
    0x00000014, 0x00000035, 0x000600a9, 0x00000004, 0x00000037, 0x00000034,
    0x00000017, 0x00000016, 0x00070050, 0x00000005, 0x00000038, 0x00000036,
    0x00000037, 0x00000014, 0x00000015, 0x0003003e, 0x0000000b, 0x00000038,
    0x000600a9, 0x00000005, 0x00000039, 0x00000033, 0x0000001c, 0x0000001b,
    0x000600a9, 0x00000005, 0x0000003a, 0x00000034, 0x0000001d, 0x00000039,
    0x0003003e, 0x0000000c, 0x0000003a, 0x000100fd, 0x00010038
};
/* FS: out(location 0) = in(location 0). */
static const uint32_t kSpirvFs[72] = {
    0x07230203, 0x00010000, 0x00000000, 0x00000040, 0x00000000, 0x00020011,
    0x00000001, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000004,
    0x00000028, 0x6e69616d, 0x00000000, 0x0000000a, 0x0000000b, 0x00030010,
    0x00000028, 0x00000007, 0x00040047, 0x0000000a, 0x0000001e, 0x00000000,
    0x00040047, 0x0000000b, 0x0000001e, 0x00000000, 0x00020013, 0x00000001,
    0x00030021, 0x00000002, 0x00000001, 0x00030016, 0x00000004, 0x00000020,
    0x00040017, 0x00000005, 0x00000004, 0x00000004, 0x00040020, 0x00000007,
    0x00000001, 0x00000005, 0x00040020, 0x00000008, 0x00000003, 0x00000005,
    0x0004003b, 0x00000007, 0x0000000a, 0x00000001, 0x0004003b, 0x00000008,
    0x0000000b, 0x00000003, 0x00050036, 0x00000001, 0x00000028, 0x00000000,
    0x00000002, 0x000200f8, 0x00000029, 0x0004003d, 0x00000005, 0x00000032,
    0x0000000a, 0x0003003e, 0x0000000b, 0x00000032, 0x000100fd, 0x00010038
};

/* ---- the EwokOS surface entry (a WSI extension: resolve via proc addr) ----- */
typedef VkResult (VKAPI_PTR *PFN_vkCreateEwokOSSurfaceVK)(
    VkInstance instance, uint32_t width, uint32_t height, VkFormat format,
    void *present_fn, void *user, VkSurfaceKHR *pSurface);

/* ---- app state ----------------------------------------------------------- */
#define MAX_SC_IMAGES 4

typedef struct {
    VkInstance       instance;
    VkSurfaceKHR     surface;
    VkPhysicalDevice phys;
    VkDevice         device;
    VkQueue          queue;
    uint32_t         qfi;
    VkSwapchainKHR   swapchain;
    VkFormat         format;
    uint32_t         width, height;
    VkImage          sc_images[MAX_SC_IMAGES];
    VkImageView      sc_views[MAX_SC_IMAGES];
    VkFramebuffer    sc_fbs[MAX_SC_IMAGES];
    uint32_t         sc_count;
    VkRenderPass     rp;
    VkPipelineLayout pl;
    VkPipeline       pipeline;
    VkCommandPool    cpool;
    VkCommandBuffer  cmd;
    VkFence          fence;
    VkSemaphore      acquired;
    int              ready;
} vk_tri;

static vk_tri T;
static graph_t *g_cur;     /* window graph for the frame being presented */

/* One-shot framebuffer dump: the present callback receives the final rendered
 * pixels whether the GPU (geom_kind 5) or the CPU rasteriser produced them, so
 * dumping here lets a GPU build and a CPU-only build be compared byte-for-byte
 * off-device.  Path overridable via argv[1]; written on the first present. */
static char g_dump_path[128] = "/home/root/tri.ppm";
static int  g_dumped = 0;

static void dump_ppm(const uint8_t *src, uint32_t w, uint32_t h,
                     uint32_t stride, const char *path)
{
    FILE *f = fopen(path, "wb");
    uint32_t x, y;
    if (!f) { printf("triangle: dump open failed: %s\n", path); return; }
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (y = 0; y < h; y++) {
        const uint8_t *row = src + (size_t)y * stride;
        for (x = 0; x < w; x++) {
            fputc(row[(size_t)x * 4u + 2u], f);   /* R (src is B8G8R8A8) */
            fputc(row[(size_t)x * 4u + 1u], f);   /* G */
            fputc(row[(size_t)x * 4u + 0u], f);   /* B */
        }
    }
    fclose(f);
    printf("triangle: dumped %ux%u to %s\n", w, h, path);
}

/* present callback: blit the rendered swapchain image into the window graph. */
static void VKAPI_CALL tri_present(void *user, const void *pixels,
                                   uint32_t width, uint32_t height,
                                   uint32_t stride_bytes, VkFormat format)
{
    graph_t *g = g_cur;
    const uint8_t *src = (const uint8_t *)pixels;
    uint32_t cw, ch, y;
    (void)user;
    if (!src) return;
    /* only the byte-identical B8G8R8A8 path is copied straight through */
    if (format != VK_FORMAT_B8G8R8A8_UNORM) return;
    if (!g_dumped) { g_dumped = 1; dump_ppm(src, width, height, stride_bytes, g_dump_path); }
    if (!g || !g->buffer) return;
    cw = width  < (uint32_t)g->w ? width  : (uint32_t)g->w;
    ch = height < (uint32_t)g->h ? height : (uint32_t)g->h;
    for (y = 0; y < ch; y++) {
        uint32_t *dst = g->buffer + (size_t)y * g->w;
        memcpy(dst, src + (size_t)y * stride_bytes, (size_t)cw * 4);
    }
}

static int create_swapchain_views_fbs(void)
{
    uint32_t i;
    VkImageViewCreateInfo ivci;
    VkFramebufferCreateInfo fbci;

    if (vkGetSwapchainImagesKHR(T.device, T.swapchain, &T.sc_count, NULL) != VK_SUCCESS)
        return -1;
    if (T.sc_count > MAX_SC_IMAGES) T.sc_count = MAX_SC_IMAGES;
    if (vkGetSwapchainImagesKHR(T.device, T.swapchain, &T.sc_count, T.sc_images) != VK_SUCCESS)
        return -1;

    for (i = 0; i < T.sc_count; i++) {
        memset(&ivci, 0, sizeof(ivci));
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = T.sc_images[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = T.format;
        ivci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel = 0;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(T.device, &ivci, NULL, &T.sc_views[i]) != VK_SUCCESS)
            return -1;

        memset(&fbci, 0, sizeof(fbci));
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = T.rp;
        fbci.attachmentCount = 1;
        fbci.pAttachments = &T.sc_views[i];
        fbci.width = T.width;
        fbci.height = T.height;
        fbci.layers = 1;
        if (vkCreateFramebuffer(T.device, &fbci, NULL, &T.sc_fbs[i]) != VK_SUCCESS)
            return -1;
    }
    return 0;
}

static int init_vulkan(uint32_t width, uint32_t height)
{
    PFN_vkCreateEwokOSSurfaceVK pfnCreateSurface;
    VkInstanceCreateInfo ici;
    VkApplicationInfo app;
    VkDeviceCreateInfo dci;
    VkDeviceQueueCreateInfo qci;
    VkQueueFamilyProperties qfam[16];
    uint32_t nphys = 0, nq = 16, i;
    float prio = 1.0f;
    VkSwapchainCreateInfoKHR sci;
    VkSurfaceFormatKHR sfmt;
    VkAttachmentDescription att;
    VkAttachmentReference colorRef;
    VkSubpassDescription subpass;
    VkRenderPassCreateInfo rpci;
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo smci;
    VkPipelineShaderStageCreateInfo stages[2];
    VkPipelineVertexInputStateCreateInfo vi;
    VkPipelineInputAssemblyStateCreateInfo ia;
    VkPipelineViewportStateCreateInfo vps;
    VkPipelineRasterizationStateCreateInfo rs;
    VkPipelineMultisampleStateCreateInfo ms;
    VkPipelineColorBlendAttachmentState cba;
    VkPipelineColorBlendStateCreateInfo cb;
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds;
    VkGraphicsPipelineCreateInfo gpci;
    VkCommandPoolCreateInfo cpci;
    VkCommandBufferAllocateInfo cbai;
    VkFenceCreateInfo fci;
    VkSemaphoreCreateInfo semci;

    memset(&T, 0, sizeof(T));
    T.width = width; T.height = height;

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "vktriangle";
    app.apiVersion = VK_API_VERSION_1_3;
    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, NULL, &T.instance) != VK_SUCCESS) {
        printf("triangle: vkCreateInstance failed\n"); return -1;
    }

    pfnCreateSurface = (PFN_vkCreateEwokOSSurfaceVK)(void *)
        vkGetInstanceProcAddr(T.instance, "vkCreateEwokOSSurfaceVK");
    if (!pfnCreateSurface) { printf("triangle: no EwokOS surface entry\n"); return -1; }
    if (pfnCreateSurface(T.instance, width, height, VK_FORMAT_B8G8R8A8_UNORM,
                         (void *)tri_present, NULL, &T.surface) != VK_SUCCESS) {
        printf("triangle: vkCreateEwokOSSurfaceVK failed\n"); return -1;
    }

    if (vkEnumeratePhysicalDevices(T.instance, &nphys, NULL) != VK_SUCCESS || nphys == 0) {
        printf("triangle: no physical devices\n"); return -1;
    }
    nphys = 1;
    vkEnumeratePhysicalDevices(T.instance, &nphys, &T.phys);

    vkGetPhysicalDeviceQueueFamilyProperties(T.phys, &nq, qfam);
    for (i = 0; i < nq; i++)
        if (qfam[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { T.qfi = i; break; }

    memset(&qci, 0, sizeof(qci));
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = T.qfi;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    memset(&dci, 0, sizeof(dci));
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    if (vkCreateDevice(T.phys, &dci, NULL, &T.device) != VK_SUCCESS) {
        printf("triangle: vkCreateDevice failed\n"); return -1;
    }
    vkGetDeviceQueue(T.device, T.qfi, 0, &T.queue);

    /* surface format -> swapchain */
    T.format = VK_FORMAT_B8G8R8A8_UNORM;
    {
        uint32_t nfmt = 1;
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(T.phys, T.surface, &nfmt, &sfmt) == VK_SUCCESS
            && nfmt >= 1 && sfmt.format != VK_FORMAT_UNDEFINED)
            T.format = sfmt.format;
    }

    memset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = T.surface;
    sci.minImageCount = 1;
    sci.imageFormat = T.format;
    sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sci.imageExtent.width = width;
    sci.imageExtent.height = height;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(T.device, &sci, NULL, &T.swapchain) != VK_SUCCESS) {
        printf("triangle: vkCreateSwapchainKHR failed\n"); return -1;
    }

    /* render pass: one colour attachment, cleared, stored for present */
    memset(&att, 0, sizeof(att));
    att.format = T.format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    memset(&rpci, 0, sizeof(rpci));
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    if (vkCreateRenderPass(T.device, &rpci, NULL, &T.rp) != VK_SUCCESS) {
        printf("triangle: vkCreateRenderPass failed\n"); return -1;
    }

    if (create_swapchain_views_fbs() != 0) {
        printf("triangle: swapchain views/framebuffers failed\n"); return -1;
    }

    /* pipeline */
    {
        VkPipelineLayoutCreateInfo plci;
        memset(&plci, 0, sizeof(plci));
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        if (vkCreatePipelineLayout(T.device, &plci, NULL, &T.pl) != VK_SUCCESS) {
            printf("triangle: vkCreatePipelineLayout failed\n"); return -1;
        }
    }

    memset(&smci, 0, sizeof(smci));
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = sizeof(kSpirvVs); smci.pCode = kSpirvVs;
    if (vkCreateShaderModule(T.device, &smci, NULL, &vs) != VK_SUCCESS) {
        printf("triangle: VS module failed\n"); return -1;
    }
    smci.codeSize = sizeof(kSpirvFs); smci.pCode = kSpirvFs;
    if (vkCreateShaderModule(T.device, &smci, NULL, &fs) != VK_SUCCESS) {
        printf("triangle: FS module failed\n"); return -1;
    }

    memset(stages, 0, sizeof(stages));
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs; stages[1].pName = "main";

    memset(&vi, 0, sizeof(vi));
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    memset(&ia, 0, sizeof(ia));
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    memset(&vps, 0, sizeof(vps));
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1; vps.scissorCount = 1;   /* dynamic viewport+scissor */
    memset(&rs, 0, sizeof(rs));
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    memset(&ms, 0, sizeof(ms));
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    memset(&cba, 0, sizeof(cba));
    cba.blendEnable = VK_FALSE;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    memset(&cb, 0, sizeof(cb));
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;
    memset(&ds, 0, sizeof(ds));
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;

    memset(&gpci, 0, sizeof(gpci));
    gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpci.stageCount = 2; gpci.pStages = stages;
    gpci.pVertexInputState = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vps;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pColorBlendState = &cb;
    gpci.pDynamicState = &ds;
    gpci.layout = T.pl;
    gpci.renderPass = T.rp;
    gpci.subpass = 0;
    if (vkCreateGraphicsPipelines(T.device, VK_NULL_HANDLE, 1, &gpci, NULL, &T.pipeline)
            != VK_SUCCESS) {
        printf("triangle: vkCreateGraphicsPipelines failed\n"); return -1;
    }
    vkDestroyShaderModule(T.device, vs, NULL);
    vkDestroyShaderModule(T.device, fs, NULL);

    /* command buffer + sync */
    memset(&cpci, 0, sizeof(cpci));
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = T.qfi;
    if (vkCreateCommandPool(T.device, &cpci, NULL, &T.cpool) != VK_SUCCESS) {
        printf("triangle: vkCreateCommandPool failed\n"); return -1;
    }
    memset(&cbai, 0, sizeof(cbai));
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = T.cpool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(T.device, &cbai, &T.cmd) != VK_SUCCESS) {
        printf("triangle: vkAllocateCommandBuffers failed\n"); return -1;
    }
    memset(&fci, 0, sizeof(fci));
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(T.device, &fci, NULL, &T.fence) != VK_SUCCESS) {
        printf("triangle: vkCreateFence failed\n"); return -1;
    }
    memset(&semci, 0, sizeof(semci));
    semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(T.device, &semci, NULL, &T.acquired) != VK_SUCCESS) {
        printf("triangle: vkCreateSemaphore failed\n"); return -1;
    }

    T.ready = 1;
    printf("triangle: vulkan ready (%ux%u, format=%d, %u swapchain images)\n",
           T.width, T.height, (int)T.format, T.sc_count);
    return 0;
}

/* record + submit + present one frame into swapchain image `idx`. */
static void render_frame(uint32_t idx)
{
    VkCommandBufferBeginInfo bi;
    VkRenderPassBeginInfo rpi;
    VkClearValue clear;
    VkViewport vp;
    VkRect2D sc;
    VkSubmitInfo si;
    VkPresentInfoKHR pi;
    VkResult res;

    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(T.cmd, &bi) != VK_SUCCESS) return;

    clear.color.float32[0] = 0.08f;
    clear.color.float32[1] = 0.08f;
    clear.color.float32[2] = 0.12f;
    clear.color.float32[3] = 1.0f;
    memset(&rpi, 0, sizeof(rpi));
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass = T.rp;
    rpi.framebuffer = T.sc_fbs[idx];
    rpi.renderArea.offset.x = 0;
    rpi.renderArea.offset.y = 0;
    rpi.renderArea.extent.width = T.width;
    rpi.renderArea.extent.height = T.height;
    rpi.clearValueCount = 1;
    rpi.pClearValues = &clear;
    vkCmdBeginRenderPass(T.cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(T.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, T.pipeline);

    vp.x = 0; vp.y = 0;
    vp.width = (float)T.width; vp.height = (float)T.height;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    vkCmdSetViewport(T.cmd, 0, 1, &vp);
    sc.offset.x = 0; sc.offset.y = 0;
    sc.extent.width = T.width; sc.extent.height = T.height;
    vkCmdSetScissor(T.cmd, 0, 1, &sc);

    vkCmdDraw(T.cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(T.cmd);
    if (vkEndCommandBuffer(T.cmd) != VK_SUCCESS) return;

    vkResetFences(T.device, 1, &T.fence);
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &T.cmd;
    if (vkQueueSubmit(T.queue, 1, &si, T.fence) != VK_SUCCESS) return;
    vkWaitForFences(T.device, 1, &T.fence, VK_TRUE, 2000000000ull);

    memset(&pi, 0, sizeof(pi));
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.swapchainCount = 1;
    pi.pSwapchains = &T.swapchain;
    pi.pImageIndices = &idx;
    pi.pResults = &res;
    vkQueuePresentKHR(T.queue, &pi);
}

static void on_event(xwin_t *xwin, xevent_t *ev)
{
    if (ev->type == XEVT_IM && ev->value.im.value == 27)  /* ESC closes */
        xwin_close(xwin);
}

static int g_frames = 0;
static void on_repaint(xwin_t *xwin, graph_t *g)
{
    uint32_t idx = 0;
    (void)xwin;
    if (!T.ready) return;
    g_cur = g;
    if (vkAcquireNextImageKHR(T.device, T.swapchain, UINT64_MAX, T.acquired,
                              VK_NULL_HANDLE, &idx) != VK_SUCCESS)
        return;
    if (idx >= T.sc_count) idx = 0;
    render_frame(idx);
    g_frames++;
}

static void loop(void *p)
{
    xwin_repaint((xwin_t *)p);
    usleep(16000);   /* ~60fps */
}

static void cleanup_vulkan(void)
{
    uint32_t i;
    if (!T.device) return;
    for (i = 0; i < T.sc_count; i++) {
        if (T.sc_fbs[i])   vkDestroyFramebuffer(T.device, T.sc_fbs[i], NULL);
        if (T.sc_views[i]) vkDestroyImageView(T.device, T.sc_views[i], NULL);
    }
    if (T.acquired)  vkDestroySemaphore(T.device, T.acquired, NULL);
    if (T.fence)     vkDestroyFence(T.device, T.fence, NULL);
    if (T.cpool)     vkDestroyCommandPool(T.device, T.cpool, NULL);
    if (T.pipeline)  vkDestroyPipeline(T.device, T.pipeline, NULL);
    if (T.pl)        vkDestroyPipelineLayout(T.device, T.pl, NULL);
    if (T.rp)        vkDestroyRenderPass(T.device, T.rp, NULL);
    if (T.swapchain) vkDestroySwapchainKHR(T.device, T.swapchain, NULL);
    vkDestroyDevice(T.device, NULL);
    if (T.surface)  vkDestroySurfaceKHR(T.instance, T.surface, NULL);
    if (T.instance) vkDestroyInstance(T.instance, NULL);
}

int main(int argc, char *argv[])
{
    x_t x;
    xwin_t *xwin;
    if (argc > 1 && argv[1][0]) {
        strncpy(g_dump_path, argv[1], sizeof(g_dump_path) - 1);
        g_dump_path[sizeof(g_dump_path) - 1] = '\0';
    }

    if (init_vulkan(TRI_W, TRI_H) != 0) {
        printf("triangle: Vulkan init failed\n");
        cleanup_vulkan();
        return -1;
    }

    x_init(&x, NULL);
    x.on_loop = loop;
    xwin = xwin_open(&x, -1, 32, 32, TRI_W, TRI_H, "vktriangle", XWIN_STYLE_NORMAL);
    if (xwin == NULL) {
        cleanup_vulkan();
        return -1;
    }
    xwin->on_event = on_event;
    xwin->on_repaint = on_repaint;
    xwin_set_visible(xwin, true);

    x_run(&x, xwin);

    xwin_destroy(xwin);
    cleanup_vulkan();
    printf("triangle: presented %d frames\n", g_frames);
    return 0;
}
