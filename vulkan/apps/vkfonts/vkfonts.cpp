/*
 * vkfonts.cpp - "Better Text in Shaders" (Shadertoy 43t3WX, by kishimisu)
 * rendered through the EwokOS Vulkan software device.
 *
 * Unlike vkmatrix (whose raymarcher is too heavy for the SPIR-V interpreter
 * and is reproduced natively), this reference is cheap enough to run AS the
 * fragment shader: res/common.glsl + res/image.glsl are the verbatim Common /
 * Image tabs, compiled to SPIR-V by shaders/gen_spirv.sh and embedded in
 * vkfonts_spirv.h.  A fullscreen-triangle VS drives the interpreter over every
 * window pixel; the shader's int[] string arrays, dynamic access chains and
 * GLSL.std.450 calls are all within the interpreter's verified subset.
 *
 * The reference iChannel0 is a 1024x1024 font atlas sampled with
 * {filter: linear, wrap: repeat, vflip: true}.  The CPU interpreter only
 * fetches NEAREST + CLAMP texels, so the FS wrapper emulates LINEAR + REPEAT
 * explicitly (see shaders/vkfonts.frag) and this app honours vflip by
 * uploading the atlas rows bottom-up.  The atlas is loaded once with
 * png_image_new() (libgraph ARGB) and converted to RGBA8 into a host-visible
 * linear-tiling image, exactly the texture path vkmatrix uses per frame.
 *
 * Presentation rides the EwokOS WSI path as in vkteapot/vkmatrix:
 * vkCreateEwokOSSurfaceVK + a present callback blitting into the window graph.
 * A Dear ImGui HUD shows FPS, back-end and the shader's live iTime/iFrame.
 */
#include <vulkan/vulkan.h>
#include <x/x.h>
#include <x/xwin.h>
#include <x/xcntl.h>
#include <graph/graph.h>
#include <graph/graph_png.h>
#include <ewoksys/kernel_tic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_vulkan.h"

#include "vkfonts_spirv.h"

/* the EwokOS surface entry (a WSI extension: resolve via proc addr) */
typedef VkResult (VKAPI_PTR *PFN_vkCreateEwokOSSurfaceVK_t)(
    VkInstance instance, uint32_t width, uint32_t height, VkFormat format,
    void *present_fn, void *user, VkSurfaceKHR *pSurface);

#define FONTS_W 640
#define FONTS_H 360
#define FONT_TEX_SIZE 1024   /* res/font.png is a fixed 1024x1024 atlas */

/* ---- app state ----------------------------------------------------------- */
#define MAX_SC_IMAGES 4

typedef struct {
    VkBuffer       buf;
    VkDeviceMemory mem;
    void          *map;
    VkDeviceSize   size;
} hbuf_t;

/* std140 Params UBO (binding 0): vec2@0, float@8, int@12, float@16, pad to 32 */
typedef struct {
    float    resolution[2];
    float    time;
    int32_t  frame;
    float    time_delta;
    float    pad[3];
} fonts_ubo_t;

typedef struct {
    VkInstance       instance;
    VkSurfaceKHR     surface;
    VkPhysicalDevice phys;
    VkDevice         device;
    VkQueue          queue;
    uint32_t         qfi;
    VkSwapchainKHR   swapchain;
    VkFormat         format;
    uint32_t         width, height;      /* window / swapchain extent */
    VkImage          sc_images[MAX_SC_IMAGES];
    VkImageView      sc_views[MAX_SC_IMAGES];
    VkFramebuffer    sc_fbs[MAX_SC_IMAGES];
    uint32_t         sc_count;
    VkRenderPass     rp;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout pl;
    VkPipeline       pipeline;
    VkDescriptorPool dpool;
    VkDescriptorSet  dset;
    /* resources */
    hbuf_t           pos;      /* vec2 fullscreen-triangle corners */
    hbuf_t           ubo;      /* fonts_ubo_t, rewritten each frame */
    VkImage          atlas;    /* 1024x1024 RGBA8 font atlas (static) */
    VkDeviceMemory   atlas_mem;
    VkImageView      atlas_view;
    VkSampler        sampler;
    VkCommandPool    cpool;
    VkCommandBuffer  cmd;
    VkFence          fence;
    VkSemaphore      acquired;
    int              ready;
} vk_fonts;

static vk_fonts T;
static graph_t *g_cur;
static int g_gpu = 0;

/* effect state (HUD-editable time scale) */
static float time_scale  = 1.0f;
static float effect_time = 0.0f;   /* accumulated iTime */
static int   effect_frame = 0;     /* iFrame */

/* Dear ImGui input state, latched by on_event */
static int  mouse_x = 0, mouse_y = 0;
static bool mouse_buttons[3] = { false, false, false };
static bool key_ctrl = false, key_shift = false, key_alt = false, key_super = false;
static ImGuiKey KeyCodeToImGuiKey(int keycode);

/* present callback: blit the rendered swapchain image into the window graph. */
static void VKAPI_CALL fonts_present(void *user, const void *pixels,
                                     uint32_t width, uint32_t height,
                                     uint32_t stride_bytes, VkFormat format,
                                     int shm_id)
{
    graph_t *g = g_cur;
    const uint8_t *src = (const uint8_t *)pixels;
    uint32_t cw, ch, y;
    (void)user;
    if (!src) return;
    if (format != VK_FORMAT_B8G8R8A8_UNORM) return;
    if (!g || !g->buffer) return;
    if (shm_id > 0 && (stride_bytes & 3u) == 0) {
        graph_t sg;
        graph_init(&sg, (const uint32_t *)src, stride_bytes / 4u, height);
        sg.shm_id = shm_id;
        graph_blt(&sg, 0, 0, width, height, g, 0, 0, g->w, g->h);
        return;
    }
    cw = width  < (uint32_t)g->w ? width  : (uint32_t)g->w;
    ch = height < (uint32_t)g->h ? height : (uint32_t)g->h;
    for (y = 0; y < ch; y++) {
        uint32_t *dst = g->buffer + (size_t)y * g->w;
        memcpy(dst, src + (size_t)y * stride_bytes, (size_t)cw * 4);
    }
}

/* ---- host-visible buffer helper ------------------------------------------ */
static uint32_t find_memory_type(const VkPhysicalDeviceMemoryProperties *mem,
                                 uint32_t typeFilter, VkMemoryPropertyFlags want)
{
    uint32_t i;
    for (i = 0; i < mem->memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) &&
            (mem->memoryTypes[i].propertyFlags & want) == want)
            return i;
    }
    for (i = 0; i < mem->memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) &&
            (mem->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            return i;
    }
    return 0xffffffffu;
}

static int hbuf_create(VkDevice dev, VkPhysicalDevice pd, hbuf_t *hb,
                       VkDeviceSize size, VkBufferUsageFlags usage)
{
    VkBufferCreateInfo bci;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo mai;
    VkPhysicalDeviceMemoryProperties mem;
    uint32_t mt;

    memset(hb, 0, sizeof(*hb));
    hb->size = size;
    memset(&bci, 0, sizeof(bci));
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &bci, NULL, &hb->buf) != VK_SUCCESS) return -1;
    vkGetBufferMemoryRequirements(dev, hb->buf, &req);
    vkGetPhysicalDeviceMemoryProperties(pd, &mem);
    mt = find_memory_type(&mem, req.memoryTypeBits,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == 0xffffffffu) return -1;
    memset(&mai, 0, sizeof(mai));
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mt;
    if (vkAllocateMemory(dev, &mai, NULL, &hb->mem) != VK_SUCCESS) return -1;
    if (vkBindBufferMemory(dev, hb->buf, hb->mem, 0) != VK_SUCCESS) return -1;
    if (vkMapMemory(dev, hb->mem, 0, size, 0, &hb->map) != VK_SUCCESS) return -1;
    return 0;
}

static void hbuf_destroy(VkDevice dev, hbuf_t *hb)
{
    if (!hb) return;
    if (hb->map) vkUnmapMemory(dev, hb->mem);
    if (hb->buf) vkDestroyBuffer(dev, hb->buf, NULL);
    if (hb->mem) vkFreeMemory(dev, hb->mem, NULL);
    memset(hb, 0, sizeof(*hb));
}

/* ---- font atlas (static 1024x1024 RGBA8, uploaded once, rows bottom-up) --- */
static void destroy_atlas(void)
{
    if (T.atlas_view) { vkDestroyImageView(T.device, T.atlas_view, NULL); T.atlas_view = VK_NULL_HANDLE; }
    if (T.atlas)      { vkDestroyImage(T.device, T.atlas, NULL); T.atlas = VK_NULL_HANDLE; }
    if (T.atlas_mem)  { vkFreeMemory(T.device, T.atlas_mem, NULL); T.atlas_mem = VK_NULL_HANDLE; }
}

static int create_atlas(void)
{
    VkImageCreateInfo ici;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo mai;
    VkPhysicalDeviceMemoryProperties mem;
    VkImageViewCreateInfo ivci;
    uint32_t mt;
    void *map = NULL;
    graph_t *g;

    /* res/ is copied next to the binary by the Makefile; the absolute path
     * covers launches from a different working directory. */
    g = png_image_new("res/font.png");
    if (!g) g = png_image_new("/apps/vulkan/vkfonts/res/font.png");
    if (!g || !g->buffer) {
        printf("vkfonts: cannot load res/font.png\n");
        if (g) graph_free(g);
        return -1;
    }
    if (g->w != FONT_TEX_SIZE || g->h != FONT_TEX_SIZE) {
        printf("vkfonts: font atlas is %dx%d, expected %dx%d\n",
               g->w, g->h, FONT_TEX_SIZE, FONT_TEX_SIZE);
        graph_free(g);
        return -1;
    }

    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent.width = FONT_TEX_SIZE;
    ici.extent.height = FONT_TEX_SIZE;
    ici.extent.depth = 1;
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_LINEAR;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    if (vkCreateImage(T.device, &ici, NULL, &T.atlas) != VK_SUCCESS) {
        graph_free(g); return -1;
    }
    vkGetImageMemoryRequirements(T.device, T.atlas, &req);
    vkGetPhysicalDeviceMemoryProperties(T.phys, &mem);
    mt = find_memory_type(&mem, req.memoryTypeBits,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == 0xffffffffu) { graph_free(g); return -1; }
    memset(&mai, 0, sizeof(mai));
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mt;
    if (vkAllocateMemory(T.device, &mai, NULL, &T.atlas_mem) != VK_SUCCESS) {
        graph_free(g); return -1;
    }
    if (vkBindImageMemory(T.device, T.atlas, T.atlas_mem, 0) != VK_SUCCESS) {
        graph_free(g); return -1;
    }
    if (vkMapMemory(T.device, T.atlas_mem, 0, req.size, 0, &map) != VK_SUCCESS) {
        graph_free(g); return -1;
    }

    /* libgraph stores ARGB uint32 (A<<24|R<<16|G<<8|B); the CPU sampler wants
     * RGBA8 bytes.  Rows are written bottom-up so v=0 addresses the image's
     * bottom row: the vflip=true half of the reference sampler contract. */
    {
        uint8_t *dst = (uint8_t *)map;
        for (int y = 0; y < FONT_TEX_SIZE; y++) {
            const uint32_t *src = g->buffer +
                (size_t)(FONT_TEX_SIZE - 1 - y) * FONT_TEX_SIZE;
            uint8_t *row = dst + (size_t)y * FONT_TEX_SIZE * 4;
            for (int x = 0; x < FONT_TEX_SIZE; x++) {
                uint32_t p = src[x];
                row[x * 4 + 0] = (uint8_t)(p >> 16);
                row[x * 4 + 1] = (uint8_t)(p >> 8);
                row[x * 4 + 2] = (uint8_t)p;
                row[x * 4 + 3] = (uint8_t)(p >> 24);
            }
        }
    }
    vkUnmapMemory(T.device, T.atlas_mem);
    graph_free(g);

    memset(&ivci, 0, sizeof(ivci));
    ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image = T.atlas;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = VK_FORMAT_R8G8B8A8_UNORM;
    ivci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    ivci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    ivci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    ivci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivci.subresourceRange.baseMipLevel = 0;
    ivci.subresourceRange.levelCount = 1;
    ivci.subresourceRange.baseArrayLayer = 0;
    ivci.subresourceRange.layerCount = 1;
    if (vkCreateImageView(T.device, &ivci, NULL, &T.atlas_view) != VK_SUCCESS)
        return -1;
    return 0;
}

/* ---- swapchain / framebuffers -------------------------------------------- */
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

static int create_swapchain(void)
{
    VkSwapchainCreateInfoKHR sci;
    memset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = T.surface;
    sci.minImageCount = 1;
    sci.imageFormat = T.format;
    sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sci.imageExtent.width = T.width;
    sci.imageExtent.height = T.height;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(T.device, &sci, NULL, &T.swapchain) != VK_SUCCESS)
        return -1;
    return 0;
}

static void destroy_sc_objects(void)
{
    uint32_t i;
    for (i = 0; i < T.sc_count; i++) {
        if (T.sc_fbs[i])   { vkDestroyFramebuffer(T.device, T.sc_fbs[i], NULL); T.sc_fbs[i] = VK_NULL_HANDLE; }
        if (T.sc_views[i]) { vkDestroyImageView(T.device, T.sc_views[i], NULL); T.sc_views[i] = VK_NULL_HANDLE; }
    }
    T.sc_count = 0;
    if (T.swapchain) { vkDestroySwapchainKHR(T.device, T.swapchain, NULL); T.swapchain = VK_NULL_HANDLE; }
}

static void recreate_swapchain(uint32_t w, uint32_t h)
{
    if (!T.ready || w == 0 || h == 0) return;
    if (w == T.width && h == T.height) return;

    vkDeviceWaitIdle(T.device);
    destroy_sc_objects();

    T.width = w; T.height = h;
    if (create_swapchain() != 0 || create_swapchain_views_fbs() != 0) {
        T.ready = 0;
        printf("vkfonts: swapchain recreate failed at %ux%u\n", w, h);
        return;
    }
    /* the atlas and both descriptor bindings survive a resize untouched */
}

/* ---- init ---------------------------------------------------------------- */
static int init_vulkan(uint32_t width, uint32_t height)
{
    PFN_vkCreateEwokOSSurfaceVK_t pfnCreateSurface;
    VkInstanceCreateInfo ici;
    VkApplicationInfo app;
    VkDeviceCreateInfo dci;
    VkDeviceQueueCreateInfo qci;
    VkQueueFamilyProperties qfam[16];
    uint32_t nphys = 0, nq = 16, i;
    float prio = 1.0f;
    VkSurfaceFormatKHR sfmt;
    VkAttachmentDescription att;
    VkAttachmentReference colorRef;
    VkSubpassDescription subpass;
    VkRenderPassCreateInfo rpci;
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo smci;
    VkPipelineShaderStageCreateInfo stages[2];
    VkPipelineVertexInputStateCreateInfo vi;
    VkVertexInputBindingDescription vbind;
    VkVertexInputAttributeDescription vattr;
    VkPipelineInputAssemblyStateCreateInfo ia;
    VkPipelineViewportStateCreateInfo vps;
    VkPipelineRasterizationStateCreateInfo rs;
    VkPipelineMultisampleStateCreateInfo ms;
    VkPipelineDepthStencilStateCreateInfo ds;
    VkPipelineColorBlendAttachmentState cba;
    VkPipelineColorBlendStateCreateInfo cb;
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dsci;
    VkGraphicsPipelineCreateInfo gpci;
    VkPipelineLayoutCreateInfo plci;
    VkDescriptorSetLayoutBinding binds[2];
    VkDescriptorSetLayoutCreateInfo dslci;
    VkDescriptorPoolSize psz[2];
    VkDescriptorPoolCreateInfo dpci;
    VkDescriptorSetAllocateInfo dsai;
    VkDescriptorBufferInfo bi;
    VkDescriptorImageInfo ii;
    VkWriteDescriptorSet wr[2];
    VkSamplerCreateInfo sci;
    VkCommandPoolCreateInfo cpci;
    VkCommandBufferAllocateInfo cbai;
    VkFenceCreateInfo fci;
    VkSemaphoreCreateInfo semci;

    memset(&T, 0, sizeof(T));
    T.width = width; T.height = height;

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "vkfonts";
    app.apiVersion = VK_API_VERSION_1_3;
    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, NULL, &T.instance) != VK_SUCCESS) {
        printf("vkfonts: vkCreateInstance failed\n"); return -1;
    }

    pfnCreateSurface = (PFN_vkCreateEwokOSSurfaceVK_t)(void *)
        vkGetInstanceProcAddr(T.instance, "vkCreateEwokOSSurfaceVK");
    if (!pfnCreateSurface) { printf("vkfonts: no EwokOS surface entry\n"); return -1; }
    if (pfnCreateSurface(T.instance, width, height, VK_FORMAT_B8G8R8A8_UNORM,
                         (void *)fonts_present, NULL, &T.surface) != VK_SUCCESS) {
        printf("vkfonts: vkCreateEwokOSSurfaceVK failed\n"); return -1;
    }

    if (vkEnumeratePhysicalDevices(T.instance, &nphys, NULL) != VK_SUCCESS || nphys == 0) {
        printf("vkfonts: no physical devices\n"); return -1;
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
        printf("vkfonts: vkCreateDevice failed\n"); return -1;
    }
    vkGetDeviceQueue(T.device, T.qfi, 0, &T.queue);

    {
        VkPhysicalDeviceProperties pprops;
        vkGetPhysicalDeviceProperties(T.phys, &pprops);
        g_gpu = (pprops.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ||
                 pprops.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
    }

    T.format = VK_FORMAT_B8G8R8A8_UNORM;
    {
        uint32_t nfmt = 1;
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(T.phys, T.surface, &nfmt, &sfmt) == VK_SUCCESS
            && nfmt >= 1 && sfmt.format != VK_FORMAT_UNDEFINED)
            T.format = sfmt.format;
    }

    if (create_swapchain() != 0) return -1;
    if (create_atlas() != 0) return -1;

    /* render pass: colour only (the effect is a fullscreen image, no depth) */
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
        printf("vkfonts: vkCreateRenderPass failed\n"); return -1;
    }

    if (create_swapchain_views_fbs() != 0) {
        printf("vkfonts: swapchain views/framebuffers failed\n"); return -1;
    }

    /* fullscreen triangle: one oversized vec2 triangle clipped to the
     * viewport covers every pixel exactly once (no diagonal seam). */
    {
        static const float tri_pos[3 * 2] = {
            -1, -1,   3, -1,   -1, 3,
        };
        if (hbuf_create(T.device, T.phys, &T.pos, sizeof(tri_pos),
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) != 0) {
            printf("vkfonts: triangle buffer failed\n"); return -1;
        }
        memcpy(T.pos.map, tri_pos, sizeof(tri_pos));
    }
    if (hbuf_create(T.device, T.phys, &T.ubo, sizeof(fonts_ubo_t),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) != 0) {
        printf("vkfonts: UBO buffer failed\n"); return -1;
    }
    memset(T.ubo.map, 0, sizeof(fonts_ubo_t));

    /* sampler: NEAREST + CLAMP is all the CPU device implements; the FS
     * wrapper emulates the reference's LINEAR + REPEAT on top of it. */
    memset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_NEAREST;
    sci.minFilter = VK_FILTER_NEAREST;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 1.0f;
    if (vkCreateSampler(T.device, &sci, NULL, &T.sampler) != VK_SUCCESS) {
        printf("vkfonts: vkCreateSampler failed\n"); return -1;
    }

    /* descriptor set layout: binding 0 = Params UBO, binding 1 = iChannel0 */
    memset(binds, 0, sizeof(binds));
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    memset(&dslci, 0, sizeof(dslci));
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 2;
    dslci.pBindings = binds;
    if (vkCreateDescriptorSetLayout(T.device, &dslci, NULL, &T.dsl) != VK_SUCCESS) {
        printf("vkfonts: vkCreateDescriptorSetLayout failed\n"); return -1;
    }

    memset(&plci, 0, sizeof(plci));
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &T.dsl;
    if (vkCreatePipelineLayout(T.device, &plci, NULL, &T.pl) != VK_SUCCESS) {
        printf("vkfonts: vkCreatePipelineLayout failed\n"); return -1;
    }

    memset(&smci, 0, sizeof(smci));
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = sizeof(kFontsVs); smci.pCode = kFontsVs;
    if (vkCreateShaderModule(T.device, &smci, NULL, &vs) != VK_SUCCESS) {
        printf("vkfonts: VS module failed\n"); return -1;
    }
    smci.codeSize = sizeof(kFontsFs); smci.pCode = kFontsFs;
    if (vkCreateShaderModule(T.device, &smci, NULL, &fs) != VK_SUCCESS) {
        printf("vkfonts: FS module failed\n"); return -1;
    }
    memset(stages, 0, sizeof(stages));
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs; stages[1].pName = "main";

    memset(&vbind, 0, sizeof(vbind));
    vbind.binding = 0; vbind.stride = 2 * sizeof(float);
    vbind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    memset(&vattr, 0, sizeof(vattr));
    vattr.location = 0; vattr.binding = 0; vattr.offset = 0;
    vattr.format = VK_FORMAT_R32G32_SFLOAT;
    memset(&vi, 0, sizeof(vi));
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &vbind;
    vi.vertexAttributeDescriptionCount = 1; vi.pVertexAttributeDescriptions = &vattr;

    memset(&ia, 0, sizeof(ia));
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    memset(&vps, 0, sizeof(vps));
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1; vps.scissorCount = 1;
    memset(&rs, 0, sizeof(rs));
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    memset(&ms, 0, sizeof(ms));
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    memset(&ds, 0, sizeof(ds));
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    memset(&cba, 0, sizeof(cba));
    cba.blendEnable = VK_FALSE;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    memset(&cb, 0, sizeof(cb));
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;
    memset(&dsci, 0, sizeof(dsci));
    dsci.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dsci.dynamicStateCount = 2; dsci.pDynamicStates = dyn;

    memset(&gpci, 0, sizeof(gpci));
    gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpci.stageCount = 2; gpci.pStages = stages;
    gpci.pVertexInputState = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vps;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pDepthStencilState = &ds;
    gpci.pColorBlendState = &cb;
    gpci.pDynamicState = &dsci;
    gpci.layout = T.pl;
    gpci.renderPass = T.rp;
    gpci.subpass = 0;
    if (vkCreateGraphicsPipelines(T.device, VK_NULL_HANDLE, 1, &gpci, NULL, &T.pipeline)
            != VK_SUCCESS) {
        printf("vkfonts: vkCreateGraphicsPipelines failed\n"); return -1;
    }
    vkDestroyShaderModule(T.device, vs, NULL);
    vkDestroyShaderModule(T.device, fs, NULL);

    psz[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    psz[0].descriptorCount = 1;
    psz[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    psz[1].descriptorCount = 1;
    memset(&dpci, 0, sizeof(dpci));
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 2;
    dpci.pPoolSizes = psz;
    if (vkCreateDescriptorPool(T.device, &dpci, NULL, &T.dpool) != VK_SUCCESS) {
        printf("vkfonts: vkCreateDescriptorPool failed\n"); return -1;
    }
    memset(&dsai, 0, sizeof(dsai));
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = T.dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &T.dsl;
    if (vkAllocateDescriptorSets(T.device, &dsai, &T.dset) != VK_SUCCESS) {
        printf("vkfonts: vkAllocateDescriptorSets failed\n"); return -1;
    }

    memset(&bi, 0, sizeof(bi));
    bi.buffer = T.ubo.buf;
    bi.offset = 0;
    bi.range = sizeof(fonts_ubo_t);
    memset(&ii, 0, sizeof(ii));
    ii.sampler = T.sampler;
    ii.imageView = T.atlas_view;
    ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    memset(wr, 0, sizeof(wr));
    wr[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr[0].dstSet = T.dset;
    wr[0].dstBinding = 0;
    wr[0].descriptorCount = 1;
    wr[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    wr[0].pBufferInfo = &bi;
    wr[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr[1].dstSet = T.dset;
    wr[1].dstBinding = 1;
    wr[1].descriptorCount = 1;
    wr[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr[1].pImageInfo = &ii;
    vkUpdateDescriptorSets(T.device, 2, wr, 0, NULL);

    memset(&cpci, 0, sizeof(cpci));
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = T.qfi;
    if (vkCreateCommandPool(T.device, &cpci, NULL, &T.cpool) != VK_SUCCESS) {
        printf("vkfonts: vkCreateCommandPool failed\n"); return -1;
    }
    memset(&cbai, 0, sizeof(cbai));
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = T.cpool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(T.device, &cbai, &T.cmd) != VK_SUCCESS) {
        printf("vkfonts: vkAllocateCommandBuffers failed\n"); return -1;
    }
    memset(&fci, 0, sizeof(fci));
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(T.device, &fci, NULL, &T.fence) != VK_SUCCESS) {
        printf("vkfonts: vkCreateFence failed\n"); return -1;
    }
    memset(&semci, 0, sizeof(semci));
    semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(T.device, &semci, NULL, &T.acquired) != VK_SUCCESS) {
        printf("vkfonts: vkCreateSemaphore failed\n"); return -1;
    }

    T.ready = 1;
    return 0;
}

/* ---- frame --------------------------------------------------------------- */
static void render_frame(uint32_t idx, float dt)
{
    VkCommandBufferBeginInfo bi;
    VkRenderPassBeginInfo rpi;
    VkClearValue clear;
    VkViewport vp;
    VkRect2D sc;
    VkDeviceSize offset = 0;
    VkSubmitInfo si;
    VkPresentInfoKHR pi;
    VkResult res;
    fonts_ubo_t *u = (fonts_ubo_t *)T.ubo.map;

    effect_time += dt * time_scale;
    u->resolution[0] = (float)T.width;
    u->resolution[1] = (float)T.height;
    u->time = effect_time;
    u->frame = effect_frame;
    u->time_delta = dt;
    effect_frame++;

    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(T.cmd, &bi) != VK_SUCCESS) return;

    clear.color.float32[0] = 0.0f;
    clear.color.float32[1] = 0.0f;
    clear.color.float32[2] = 0.0f;
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
    vkCmdBindDescriptorSets(T.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, T.pl,
                            0, 1, &T.dset, 0, NULL);

    vp.x = 0; vp.y = 0;
    vp.width = (float)T.width; vp.height = (float)T.height;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    vkCmdSetViewport(T.cmd, 0, 1, &vp);
    sc.offset.x = 0; sc.offset.y = 0;
    sc.extent.width = T.width; sc.extent.height = T.height;
    vkCmdSetScissor(T.cmd, 0, 1, &sc);

    vkCmdBindVertexBuffers(T.cmd, 0, 1, &T.pos.buf, &offset);
    vkCmdDraw(T.cmd, 3, 1, 0, 0);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), T.cmd);

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

/* ---- input --------------------------------------------------------------- */
static void on_event(xwin_t *xwin, xevent_t *ev)
{
    ImGuiIO& io = ImGui::GetIO();

    if (ev->type == XEVT_IM) {
        int key = ev->value.im.value;
        int32_t state = ev->state;

        key_shift = (ev->value.im.shift != 0);
        key_ctrl  = (ev->value.im.ctrl  != 0);

        if (key == 27 && state == XIM_STATE_PRESS) {
            xwin_close(xwin);
            return;
        }
        ImGuiKey ik = KeyCodeToImGuiKey(key);
        if (ik != ImGuiKey_None)
            io.AddKeyEvent(ik, state == XIM_STATE_PRESS);
        if (state == XIM_STATE_PRESS && key >= 32 && key < 127)
            io.AddInputCharacter(key);
    }
    else if (ev->type == XEVT_MOUSE) {
        gpos_t pos = xwin_get_inside_pos(xwin, ev->value.mouse.x, ev->value.mouse.y);
        int32_t state  = ev->state;
        int32_t button = ev->value.mouse.button;
        bool down = (state == MOUSE_STATE_DOWN || state == MOUSE_STATE_DRAG);

        mouse_x = pos.x;
        mouse_y = pos.y;
        if (button == MOUSE_BUTTON_LEFT)
            mouse_buttons[0] = down;
        else if (button == MOUSE_BUTTON_RIGHT)
            mouse_buttons[1] = down;
        else if (button == MOUSE_BUTTON_MID)
            mouse_buttons[2] = down;
    }
}

/* ---- fps accounting ------------------------------------------------------ */
static int   fps_count = 0, fps = 0;
static uint32_t last_tic = 0, last_frame_tic = 0;

static float frame_dt(void)
{
    uint32_t cur = 0;
    float dt;
    kernel_tic32(NULL, NULL, &cur);
    if (last_frame_tic == 0) last_frame_tic = cur;
    dt = (float)(cur - last_frame_tic) / 1000000.0f;
    last_frame_tic = cur;
    if (dt > 0.1f) dt = 0.1f;
    return dt;
}

static void update_fps(void)
{
    uint32_t low = 0;
    kernel_tic32(NULL, NULL, &low);
    if (last_tic == 0 || (low - last_tic) >= 1000000) {
        last_tic = low;
        fps = fps_count;
        fps_count = 0;
    }
    fps_count++;
}

/* ---- Dear ImGui overlay -------------------------------------------------- */
static void imgui_check_vk(VkResult err)
{
    if (err != VK_SUCCESS)
        printf("vkfonts: imgui vulkan error %d\n", (int)err);
}

static void init_imgui(void)
{
    ImGui_ImplVulkan_InitInfo init_info;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.DisplaySize = ImVec2((float)T.width, (float)T.height);
    ImGui::StyleColorsDark();

    memset(&init_info, 0, sizeof(init_info));
    init_info.ApiVersion         = VK_API_VERSION_1_3;
    init_info.Instance           = T.instance;
    init_info.PhysicalDevice     = T.phys;
    init_info.Device             = T.device;
    init_info.QueueFamily        = T.qfi;
    init_info.Queue              = T.queue;
    init_info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
    init_info.MinImageCount      = 2;
    init_info.ImageCount         = (T.sc_count >= 2) ? T.sc_count : 2;
    init_info.CheckVkResultFn    = imgui_check_vk;
    init_info.PipelineInfoMain.RenderPass = T.rp;
    init_info.PipelineInfoMain.Subpass    = 0;
    if (!ImGui_ImplVulkan_Init(&init_info))
        printf("vkfonts: ImGui_ImplVulkan_Init failed\n");
    io.Fonts->AddFontFromFileTTF("/usr/system/fonts/system.ttf", 16.0f);
}

static ImGuiKey KeyCodeToImGuiKey(int keycode)
{
    switch (keycode) {
        case 9:   return ImGuiKey_Tab;
        case 27:  return ImGuiKey_Escape;
        case 13:  return ImGuiKey_Enter;
        case 8:   return ImGuiKey_Backspace;
        case 127: return ImGuiKey_Delete;
        case 32:  return ImGuiKey_Space;
        case 276: return ImGuiKey_LeftArrow;
        case 275: return ImGuiKey_RightArrow;
        case 273: return ImGuiKey_UpArrow;
        case 274: return ImGuiKey_DownArrow;
        case '0': return ImGuiKey_0;
        case '1': return ImGuiKey_1;
        case '2': return ImGuiKey_2;
        case '3': return ImGuiKey_3;
        case '4': return ImGuiKey_4;
        case '5': return ImGuiKey_5;
        case '6': return ImGuiKey_6;
        case '7': return ImGuiKey_7;
        case '8': return ImGuiKey_8;
        case '9': return ImGuiKey_9;
        case 'a': case 'A': return ImGuiKey_A;
        case 'b': case 'B': return ImGuiKey_B;
        case 'c': case 'C': return ImGuiKey_C;
        case 'd': case 'D': return ImGuiKey_D;
        case 'e': case 'E': return ImGuiKey_E;
        case 'f': case 'F': return ImGuiKey_F;
        case 'g': case 'G': return ImGuiKey_G;
        case 'h': case 'H': return ImGuiKey_H;
        case 'i': case 'I': return ImGuiKey_I;
        case 'j': case 'J': return ImGuiKey_J;
        case 'k': case 'K': return ImGuiKey_K;
        case 'l': case 'L': return ImGuiKey_L;
        case 'm': case 'M': return ImGuiKey_M;
        case 'n': case 'N': return ImGuiKey_N;
        case 'o': case 'O': return ImGuiKey_O;
        case 'p': case 'P': return ImGuiKey_P;
        case 'q': case 'Q': return ImGuiKey_Q;
        case 'r': case 'R': return ImGuiKey_R;
        case 's': case 'S': return ImGuiKey_S;
        case 't': case 'T': return ImGuiKey_T;
        case 'u': case 'U': return ImGuiKey_U;
        case 'v': case 'V': return ImGuiKey_V;
        case 'w': case 'W': return ImGuiKey_W;
        case 'x': case 'X': return ImGuiKey_X;
        case 'y': case 'Y': return ImGuiKey_Y;
        case 'z': case 'Z': return ImGuiKey_Z;
        case ',': return ImGuiKey_Comma;
        case '.': return ImGuiKey_Period;
        case '/': return ImGuiKey_Slash;
        case '-': return ImGuiKey_Minus;
        case '=': return ImGuiKey_Equal;
        default:  return ImGuiKey_None;
    }
}

static void process_imgui_frame(void)
{
    ImGuiIO& io = ImGui::GetIO();

    io.DisplaySize  = ImVec2((float)T.width, (float)T.height);
    io.MousePos     = ImVec2((float)mouse_x, (float)mouse_y);
    io.MouseDown[0] = mouse_buttons[0];
    io.MouseDown[1] = mouse_buttons[1];
    io.MouseDown[2] = mouse_buttons[2];
    io.KeyCtrl      = key_ctrl;
    io.KeyShift     = key_shift;
    io.KeyAlt       = key_alt;
    io.KeySuper     = key_super;

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();

    /* The shader writes from the very top of the window, so park the HUD in
     * the empty band between the debug lines and the numbered list, collapsed
     * by default: the reference render stays unobstructed until expanded. */
    ImGui::SetNextWindowPos(ImVec2(8.0f, 112.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("vkfonts", NULL, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("FPS: %d", fps);
    ImGui::TextColored(g_gpu ? ImVec4(0.40f, 1.00f, 0.55f, 1.0f)
                             : ImVec4(1.00f, 0.70f, 0.30f, 1.0f),
                       "Renderer: %s", g_gpu ? "GPU" : "CPU");
    ImGui::Text("Resolution: %ux%u", T.width, T.height);
    ImGui::Text("iTime: %.2f  iFrame: %d", effect_time, effect_frame);
    ImGui::SliderFloat("time scale", &time_scale, 0.0f, 3.0f, "%.2f");
    ImGui::TextUnformatted("Shadertoy 43t3WX (kishimisu)");
    ImGui::End();
}

static void on_repaint(xwin_t *xwin, graph_t *g)
{
    uint32_t idx = 0;
    float dt;
    (void)xwin;
    if (!T.ready) return;
    if (g && g->w > 0 && g->h > 0 &&
        ((uint32_t)g->w != T.width || (uint32_t)g->h != T.height))
        recreate_swapchain((uint32_t)g->w, (uint32_t)g->h);
    if (!T.ready) return;
    g_cur = g;
    if (vkAcquireNextImageKHR(T.device, T.swapchain, UINT64_MAX, T.acquired,
                              VK_NULL_HANDLE, &idx) != VK_SUCCESS)
        return;
    if (idx >= T.sc_count) idx = 0;
    dt = frame_dt();
    process_imgui_frame();
    ImGui::Render();
    render_frame(idx, dt);
    update_fps();
}

static void loop(void *p)
{
    xwin_repaint((xwin_t *)p);
    usleep(1000);
}

static void cleanup_vulkan(void)
{
    uint32_t i;
    if (!T.device) return;
    for (i = 0; i < T.sc_count; i++) {
        if (T.sc_fbs[i])   vkDestroyFramebuffer(T.device, T.sc_fbs[i], NULL);
        if (T.sc_views[i]) vkDestroyImageView(T.device, T.sc_views[i], NULL);
    }
    hbuf_destroy(T.device, &T.pos);
    hbuf_destroy(T.device, &T.ubo);
    destroy_atlas();
    if (T.sampler)  vkDestroySampler(T.device, T.sampler, NULL);
    if (T.acquired) vkDestroySemaphore(T.device, T.acquired, NULL);
    if (T.fence)    vkDestroyFence(T.device, T.fence, NULL);
    if (T.cpool)    vkDestroyCommandPool(T.device, T.cpool, NULL);
    if (T.dpool)    vkDestroyDescriptorPool(T.device, T.dpool, NULL);
    if (T.pipeline) vkDestroyPipeline(T.device, T.pipeline, NULL);
    if (T.pl)       vkDestroyPipelineLayout(T.device, T.pl, NULL);
    if (T.dsl)      vkDestroyDescriptorSetLayout(T.device, T.dsl, NULL);
    if (T.rp)       vkDestroyRenderPass(T.device, T.rp, NULL);
    if (T.swapchain) vkDestroySwapchainKHR(T.device, T.swapchain, NULL);
    vkDestroyDevice(T.device, NULL);
    if (T.surface)  vkDestroySurfaceKHR(T.instance, T.surface, NULL);
    if (T.instance) vkDestroyInstance(T.instance, NULL);
}

int main(int argc, char *argv[])
{
    x_t x;
    xwin_t *xwin;
    (void)argc; (void)argv;

    if (init_vulkan(FONTS_W, FONTS_H) != 0) {
        printf("vkfonts: Vulkan init failed\n");
        cleanup_vulkan();
        return -1;
    }
    init_imgui();

    x_init(&x, NULL);
    x.on_loop = loop;
    xwin = xwin_open(&x, -1, 32, 32, FONTS_W, FONTS_H, "vkfonts", XWIN_STYLE_NORMAL);
    if (xwin == NULL) {
        cleanup_vulkan();
        return -1;
    }
    xwin->on_event = on_event;
    xwin->on_repaint = on_repaint;
    xwin_set_visible(xwin, true);

    x_run(&x, xwin);

    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();

    xwin_destroy(xwin);
    cleanup_vulkan();
    return 0;
}