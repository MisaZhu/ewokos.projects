/*
 * vkmatrix.cpp - "Inside the Matrix" (Shadertoy 4t3BWl, by And390) rendered
 * through the EwokOS Vulkan software device.
 *
 * The reference is a single-pass procedural fragment shader (iTime /
 * iResolution / iMouse only, no textures): a camera walks and turns through an
 * infinite city of falling rune strips.  Its 40-iteration per-pixel raymarch
 * with hundreds of sin() calls is far beyond the software SPIR-V interpreter's
 * per-frame budget, so this app follows the vkteapot precedent (CPU-side
 * shading presented through trivial Vulkan shaders): matrix.c reproduces the
 * reference algorithm value-for-value into an internal-resolution RGBA buffer,
 * a native bilinear upscale (what a LINEAR-filtered GPU sampler would show)
 * fills a window-sized RGBA8 texture, and a real Vulkan graphics pipeline --
 * fullscreen quad VS + texture-sampling FS, both embedded SPIR-V -- draws it
 * into the swapchain.  Presentation rides the EwokOS WSI path exactly as in
 * vkteapot: vkCreateEwokOSSurfaceVK + a present callback blitting into the
 * window graph.
 *
 * On a GPU-capable device the rain instead runs as a real Vulkan COMPUTE
 * dispatch at NATIVE window resolution (shaders/matrix.comp, embedded SPIR-V,
 * params/pixels in SSBOs -- the vktest compute precedent), probed once at
 * startup; the CPU path above remains the fallback when the probe declines.
 *
 * Interaction mirrors the reference's iMouse semantics: pressing (or dragging)
 * away from the bottom-left corner steers the camera (horizontal = turn,
 * vertical = look up/down), and the latch survives button release as on
 * Shadertoy; clicking the bottom-left corner (iMouse <= 10 in both axes)
 * returns to the auto demo walk.  A Dear ImGui HUD exposes FPS, back-end,
 * render scale, iteration count and speed.
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
#include <math.h>
#include <unistd.h>
#include <pthread.h>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_vulkan.h"

#include "matrix.h"
#include "vkmatrix_spirv.h"

/* the EwokOS surface entry (a WSI extension: resolve via proc addr) */
typedef VkResult (VKAPI_PTR *PFN_vkCreateEwokOSSurfaceVK_t)(
    VkInstance instance, uint32_t width, uint32_t height, VkFormat format,
    void *present_fn, void *user, VkSurfaceKHR *pSurface);
/* EwokOS query: contig-shm key backing a buffer's memory (0 = none), so the
 * compute output can be presented through the 2D hardware blit path */
typedef int32_t (VKAPI_PTR *PFN_vkGetBufferMemoryShmEWOK_t)(
    VkDevice device, VkBuffer buffer);

#define MATRIX_W 640
#define MATRIX_H 360

/* ---- app state ----------------------------------------------------------- */
#define MAX_SC_IMAGES 4

typedef struct {
    VkBuffer       buf;
    VkDeviceMemory mem;
    void          *map;
    VkDeviceSize   size;
} hbuf_t;

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
    hbuf_t           pos;      /* vec4 clip-space quad corners   */
    hbuf_t           uv;       /* vec2 texture coords            */
    VkImage          tex;      /* window-sized RGBA8 rain image  */
    VkDeviceMemory   tex_mem;
    VkImageView      tex_view;
    VkSampler        sampler;
    void            *tex_map;
    VkCommandPool    cpool;
    VkCommandBuffer  cmd;
    VkFence          fence;
    VkSemaphore      acquired;
    int              ready;
    /* compute rain: the reference raymarcher runs on the GPU at NATIVE
     * window resolution (shaders/matrix.comp); params/out are SSBOs */
    VkDescriptorSetLayout cs_dsl;
    VkPipelineLayout      cs_pl;
    VkPipeline            cs_pipeline;
    VkDescriptorPool      cs_dpool;
    VkDescriptorSet       cs_dset;
    hbuf_t                cs_params;   /* float[15]: time,W,H,ro,M */
    hbuf_t                cs_out;      /* W*H packed pixels          */
    uint32_t              cs_out_w, cs_out_h;
    int32_t               cs_shm;      /* out buffer's contig-shm key */
} vk_matrix;

static vk_matrix T;
static graph_t *g_cur;
static xwin_t *g_xwin;
static int g_gpu = 0;
/* 1 = the rain runs as a GPU compute dispatch at native resolution (probed
 * at startup); 0 = CPU rain bands into the internal-resolution canvas */
static int g_compute = 0;
static PFN_vkGetBufferMemoryShmEWOK_t pfnBufShm = NULL;

/* effect parameters (HUD-editable) */
static int   render_scale = 2;      /* internal resolution = window / scale */
static int   scale_auto   = 1;      /* pick scale from window height until touched */
static int   iterations   = 40;     /* reference ITERATIONS                   */
static float speed        = 1.0f;   /* reference SPEED                        */
static float effect_time  = 0.0f;   /* accumulated iTime * SPEED              */
static float mouse_gl[2]  = { 0.0f, 0.0f };  /* iMouse.xy, GL origin, internal px */

/* internal effect buffer */
static uint32_t *eff_buf = NULL;
static int eff_w = 0, eff_h = 0;
/* cached staging buffer the rain workers render into: the contig shm canvas
 * is mapped NOCACHE (kernel shm.c), so per-pixel shading writes must stay in
 * cached memory and only the finished frame is streamed into the canvas */
static uint32_t *rain_buf = NULL;
/* keyed shm canvas backing eff_buf: lets the present blit upscale on the 2D
 * hardware (graph_blt routes to g2d only for shm+contig sources) */
static graph_t *eff_g = NULL;

static uint32_t tic_now(void)
{
    uint32_t t = 0;
    kernel_tic32(NULL, NULL, &t);
    return t;
}

static float ms_since(uint32_t t0)
{
    return (float)(tic_now() - t0) / 1000.0f;
}

/* Dear ImGui input state, latched by on_event */
static int  mouse_x = 0, mouse_y = 0;
static bool mouse_buttons[3] = { false, false, false };
static bool key_ctrl = false, key_shift = false, key_alt = false, key_super = false;
/* overlay visibility: a clean tap outside the HUD window toggles it */
static bool g_show_ui = true;
/* deferred quit: the Exit button runs inside the repaint callback (which holds
 * the server painting lock), so it must NOT call xwin_close there or it
 * deadlocks.  It sets this flag; loop() performs the close in main-loop context. */
static bool g_quit = false;
static ImGuiKey KeyCodeToImGuiKey(int keycode);

/* present callback: blit the rendered swapchain image into the window graph. */
static void VKAPI_CALL matrix_present(void *user, const void *pixels,
                                      uint32_t width, uint32_t height,
                                      uint32_t stride_bytes, VkFormat format,
                                      int shm_id)
{
    graph_t *g = g_cur;
    const uint8_t *src = (const uint8_t *)pixels;
    (void)user;
    if (!src) return;
    if (format != VK_FORMAT_B8G8R8A8_UNORM) return;
    if (!g || !g->buffer) return;
    if ((stride_bytes & 3u) != 0) return;
    /* rain first, then the panel-res HUD (transparent clear + ImGui)
     * alpha-composited over it.  The rain never goes through the Vulkan
     * rasterizer: GPU mode blits the compute SSBO 1:1 (its contig-shm key
     * routes graph_blt to the 2D hardware), CPU mode scale-blits the
     * internal canvas. */
    if (g_compute && T.cs_out.map) {
        graph_t cg;
        graph_init(&cg, (const uint32_t *)T.cs_out.map, T.cs_out_w, T.cs_out_h);
        if (T.cs_shm > 0) {
            cg.shm_id = T.cs_shm;
            cg.shm_contig = 1;
        }
        graph_blt(&cg, 0, 0, cg.w, cg.h, g, 0, 0, g->w, g->h);
    } else if (eff_g) {
        graph_blt(eff_g, 0, 0, eff_g->w, eff_g->h, g, 0, 0, g->w, g->h);
    }
    graph_t sg;
    graph_init(&sg, (const uint32_t *)src, stride_bytes / 4u, height);
    if (shm_id > 0) {
        sg.shm_id = shm_id;
        sg.shm_contig = 1;
    }
    graph_blt_alpha(&sg, 0, 0, width, height, g, 0, 0, g->w, g->h, 0xff);
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

/* ---- rain texture (window-sized RGBA8, host-written each frame) ---------- */
static void destroy_texture(void)
{
    if (T.tex_map)  { vkUnmapMemory(T.device, T.tex_mem); T.tex_map = NULL; }
    if (T.tex_view) { vkDestroyImageView(T.device, T.tex_view, NULL); T.tex_view = VK_NULL_HANDLE; }
    if (T.tex)      { vkDestroyImage(T.device, T.tex, NULL); T.tex = VK_NULL_HANDLE; }
    if (T.tex_mem)  { vkFreeMemory(T.device, T.tex_mem, NULL); T.tex_mem = VK_NULL_HANDLE; }
}

static int create_texture(uint32_t width, uint32_t height)
{
    VkImageCreateInfo ici;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo mai;
    VkPhysicalDeviceMemoryProperties mem;
    VkImageViewCreateInfo ivci;
    uint32_t mt;

    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent.width = width;
    ici.extent.height = height;
    ici.extent.depth = 1;
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_LINEAR;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    if (vkCreateImage(T.device, &ici, NULL, &T.tex) != VK_SUCCESS) return -1;
    vkGetImageMemoryRequirements(T.device, T.tex, &req);
    vkGetPhysicalDeviceMemoryProperties(T.phys, &mem);
    mt = find_memory_type(&mem, req.memoryTypeBits,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == 0xffffffffu) return -1;
    memset(&mai, 0, sizeof(mai));
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mt;
    if (vkAllocateMemory(T.device, &mai, NULL, &T.tex_mem) != VK_SUCCESS) return -1;
    if (vkBindImageMemory(T.device, T.tex, T.tex_mem, 0) != VK_SUCCESS) return -1;
    if (vkMapMemory(T.device, T.tex_mem, 0, req.size, 0, &T.tex_map) != VK_SUCCESS) return -1;

    memset(&ivci, 0, sizeof(ivci));
    ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image = T.tex;
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
    if (vkCreateImageView(T.device, &ivci, NULL, &T.tex_view) != VK_SUCCESS) return -1;
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
    destroy_texture();
    if (T.swapchain) { vkDestroySwapchainKHR(T.device, T.swapchain, NULL); T.swapchain = VK_NULL_HANDLE; }
}

/* refresh the descriptor set's image binding after a texture rebuild */
static void write_tex_descriptor(void)
{
    VkDescriptorImageInfo ii;
    VkWriteDescriptorSet wr;
    memset(&ii, 0, sizeof(ii));
    ii.sampler = T.sampler;
    ii.imageView = T.tex_view;
    ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    memset(&wr, 0, sizeof(wr));
    wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet = T.dset;
    wr.dstBinding = 0;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo = &ii;
    vkUpdateDescriptorSets(T.device, 1, &wr, 0, NULL);
}

/* ---- GPU compute rain (shaders/matrix.comp, SSBO params + pixels) -------- */

/* bind the compute set: binding 0 = params SSBO, binding 1 = pixel out SSBO */
static void write_cs_descriptors(void)
{
    VkDescriptorBufferInfo dbi[2];
    VkWriteDescriptorSet wr[2];
    int i;
    memset(dbi, 0, sizeof(dbi));
    dbi[0].buffer = T.cs_params.buf; dbi[0].offset = 0; dbi[0].range = VK_WHOLE_SIZE;
    dbi[1].buffer = T.cs_out.buf;    dbi[1].offset = 0; dbi[1].range = VK_WHOLE_SIZE;
    memset(wr, 0, sizeof(wr));
    for (i = 0; i < 2; i++) {
        wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[i].dstSet = T.cs_dset;
        wr[i].dstBinding = (uint32_t)i;
        wr[i].descriptorCount = 1;
        wr[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wr[i].pBufferInfo = &dbi[i];
    }
    vkUpdateDescriptorSets(T.device, 2, wr, 0, NULL);
}

/* (re)allocate the native-resolution pixel SSBO; its contig-shm key lets
 * matrix_present blit it through the 2D hardware.  Any failure disables the
 * compute path (the CPU rain bands remain the always-correct fallback). */
static void resize_compute_out(uint32_t w, uint32_t h)
{
    if (w == T.cs_out_w && h == T.cs_out_h && T.cs_out.map) return;
    hbuf_destroy(T.device, &T.cs_out);
    T.cs_out_w = 0; T.cs_out_h = 0; T.cs_shm = 0;
    if (hbuf_create(T.device, T.phys, &T.cs_out, (VkDeviceSize)w * h * 4,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) != 0) {
        printf("vkmatrix: compute out buffer %ux%u failed\n", w, h);
        g_compute = 0;
        return;
    }
    T.cs_out_w = w; T.cs_out_h = h;
    T.cs_shm = pfnBufShm ? pfnBufShm(T.device, T.cs_out.buf) : 0;
}

static int create_compute(void)
{
    VkShaderModuleCreateInfo smci;
    VkShaderModule cs;
    VkDescriptorSetLayoutBinding bnd[2];
    VkDescriptorSetLayoutCreateInfo dslci;
    VkPipelineLayoutCreateInfo plci;
    VkComputePipelineCreateInfo cpci;
    VkDescriptorPoolSize psz;
    VkDescriptorPoolCreateInfo dpci;
    VkDescriptorSetAllocateInfo dsai;
    int i;

    memset(&smci, 0, sizeof(smci));
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = sizeof(kMatrixCs); smci.pCode = kMatrixCs;
    if (vkCreateShaderModule(T.device, &smci, NULL, &cs) != VK_SUCCESS) {
        printf("vkmatrix: CS module failed\n"); return -1;
    }

    memset(bnd, 0, sizeof(bnd));
    for (i = 0; i < 2; i++) {
        bnd[i].binding = (uint32_t)i;
        bnd[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bnd[i].descriptorCount = 1;
        bnd[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    memset(&dslci, 0, sizeof(dslci));
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 2;
    dslci.pBindings = bnd;
    if (vkCreateDescriptorSetLayout(T.device, &dslci, NULL, &T.cs_dsl) != VK_SUCCESS) {
        vkDestroyShaderModule(T.device, cs, NULL);
        printf("vkmatrix: CS descriptor layout failed\n"); return -1;
    }
    memset(&plci, 0, sizeof(plci));
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &T.cs_dsl;
    if (vkCreatePipelineLayout(T.device, &plci, NULL, &T.cs_pl) != VK_SUCCESS) {
        vkDestroyShaderModule(T.device, cs, NULL);
        printf("vkmatrix: CS pipeline layout failed\n"); return -1;
    }
    memset(&cpci, 0, sizeof(cpci));
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = cs;
    cpci.stage.pName = "main";
    cpci.layout = T.cs_pl;
    if (vkCreateComputePipelines(T.device, VK_NULL_HANDLE, 1, &cpci, NULL,
                                 &T.cs_pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(T.device, cs, NULL);
        printf("vkmatrix: vkCreateComputePipelines failed\n"); return -1;
    }
    vkDestroyShaderModule(T.device, cs, NULL);

    psz.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    psz.descriptorCount = 2;
    memset(&dpci, 0, sizeof(dpci));
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &psz;
    if (vkCreateDescriptorPool(T.device, &dpci, NULL, &T.cs_dpool) != VK_SUCCESS) {
        printf("vkmatrix: CS descriptor pool failed\n"); return -1;
    }
    memset(&dsai, 0, sizeof(dsai));
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = T.cs_dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &T.cs_dsl;
    if (vkAllocateDescriptorSets(T.device, &dsai, &T.cs_dset) != VK_SUCCESS) {
        printf("vkmatrix: CS descriptor set failed\n"); return -1;
    }

    /* float[15] used, padded to 64 bytes for the backend's size checks */
    if (hbuf_create(T.device, T.phys, &T.cs_params, 16 * sizeof(float),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) != 0) {
        printf("vkmatrix: CS params buffer failed\n"); return -1;
    }
    return 0;
}

/* Startup GPU probe: a transparent backend decline would CPU-interpret the
 * dispatch (minutes per native frame), so verify the GPU really executes it
 * BEFORE enabling the compute path.  Pass 1 warms the lazy transpile/compile/
 * upload chain, pass 2 is timed; enable only if it is fast AND lit pixels. */
static void probe_compute(void)
{
    enum { PW = 64, PH = 64 };
    hbuf_t out;
    matrix_frame f;
    float params[16];
    VkDescriptorBufferInfo dbi[2];
    VkWriteDescriptorSet wr[2];
    VkCommandBufferBeginInfo bi;
    VkSubmitInfo si;
    uint32_t t0;
    float ms = 0.0f;
    int nonzero = 0, pass, i;
    const uint32_t *px;

    if (hbuf_create(T.device, T.phys, &out, PW * PH * 4,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) != 0) {
        fprintf(stderr, "vkmatrix: compute probe buffer failed\n");
        return;
    }
    memset(&f, 0, sizeof(f));
    f.width = PW; f.height = PH; f.time = 4.2f; f.iterations = 40;
    memset(params, 0, sizeof(params));
    matrix_gpu_params(&f, params);
    memcpy(T.cs_params.map, params, sizeof(params));
    memset(out.map, 0, PW * PH * 4);

    memset(dbi, 0, sizeof(dbi));
    dbi[0].buffer = T.cs_params.buf; dbi[0].offset = 0; dbi[0].range = VK_WHOLE_SIZE;
    dbi[1].buffer = out.buf;         dbi[1].offset = 0; dbi[1].range = VK_WHOLE_SIZE;
    memset(wr, 0, sizeof(wr));
    for (i = 0; i < 2; i++) {
        wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[i].dstSet = T.cs_dset;
        wr[i].dstBinding = (uint32_t)i;
        wr[i].descriptorCount = 1;
        wr[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wr[i].pBufferInfo = &dbi[i];
    }
    vkUpdateDescriptorSets(T.device, 2, wr, 0, NULL);

    for (pass = 0; pass < 2; pass++) {
        memset(&bi, 0, sizeof(bi));
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(T.cmd, &bi) != VK_SUCCESS) break;
        vkCmdBindPipeline(T.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, T.cs_pipeline);
        vkCmdBindDescriptorSets(T.cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                T.cs_pl, 0, 1, &T.cs_dset, 0, NULL);
        vkCmdDispatch(T.cmd, (PW * PH + 63) / 64, 1, 1);
        if (vkEndCommandBuffer(T.cmd) != VK_SUCCESS) break;
        vkResetFences(T.device, 1, &T.fence);
        memset(&si, 0, sizeof(si));
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &T.cmd;
        /* the backend dispatch is BLOCKING inside vkQueueSubmit's replay,
         * so the timer must start before the submit, not after it */
        t0 = tic_now();
        if (vkQueueSubmit(T.queue, 1, &si, T.fence) != VK_SUCCESS) break;
        if (vkWaitForFences(T.device, 1, &T.fence, VK_TRUE,
                            10000000000ull) != VK_SUCCESS) {
            fprintf(stderr, "vkmatrix: compute probe fence timeout (pass %d)\n", pass);
            return;   /* work may still be in flight: keep the probe buffer */
        }
        ms = ms_since(t0);
    }
    px = (const uint32_t *)out.map;
    for (i = 0; i < PW * PH; i++)
        if ((px[i] & 0x00ffffffu) != 0) nonzero++;

    /* A warm/cached GPU dispatch can finish below the timer's resolution and
     * read 0.0 ms; that is a SUCCESS, not a failure.  The lit-pixel count proves
     * the GPU really wrote output, and ms < 150 rejects the slow CPU-interpreter
     * fallback (which measured ~5000 ms).  Do NOT require ms > 0. */
    if (ms < 150.0f && nonzero > PW * PH / 100) {
        g_compute = 1;
    } else {
        fprintf(stderr, "vkmatrix: compute probe declined (%.1f ms, %d/%d lit) -> CPU rain\n",
                ms, nonzero, PW * PH);
    }
    hbuf_destroy(T.device, &out);
    if (g_compute) {
        resize_compute_out(T.width, T.height);
        if (g_compute) write_cs_descriptors();
    }
}

/* internal effect buffer: a keyed shm canvas so the present blit can upscale
 * it on the 2D hardware (graph_blt routes to g2d only for shm+contig srcs) */
static void resize_effect_buffer(void)
{
    int w = (int)T.width / render_scale;
    int h = (int)T.height / render_scale;
    if (w < 32) w = 32;
    if (h < 18) h = 18;
    if (w == eff_w && h == eff_h && eff_buf) return;
    if (eff_g) graph_free(eff_g);
    eff_g = graph_new_shm(w, h);
    if (!eff_g) {
        eff_buf = NULL; eff_w = 0; eff_h = 0;
        return;
    }
    eff_buf = eff_g->buffer;
    free(rain_buf);
    rain_buf = (uint32_t *)malloc((size_t)w * h * 4);
    eff_w = w; eff_h = h;
}

static void recreate_swapchain(uint32_t w, uint32_t h)
{
    if (!T.ready || w == 0 || h == 0) return;
    if (w == T.width && h == T.height) return;

    vkDeviceWaitIdle(T.device);
    destroy_sc_objects();

    T.width = w; T.height = h;
    /* keep the internal rain near 180 rows on any window size (fullscreen
     * panels included); the HUD slider overrides this once touched */
    if (scale_auto) {
        render_scale = (int)(h / 180);
        if (render_scale < 1) render_scale = 1;
        if (render_scale > 16) render_scale = 16;
    }
    resize_effect_buffer();
    if (create_swapchain() != 0 ||
        create_texture((uint32_t)eff_w, (uint32_t)eff_h) != 0 ||
        create_swapchain_views_fbs() != 0) {
        T.ready = 0;
        printf("vkmatrix: swapchain recreate failed at %ux%u\n", w, h);
        return;
    }
    write_tex_descriptor();
    if (g_compute) {
        resize_compute_out(w, h);
        if (g_compute) write_cs_descriptors();
    }
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
    VkVertexInputBindingDescription vbind[2];
    VkVertexInputAttributeDescription vattr[2];
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
    VkDescriptorSetLayoutBinding ub;
    VkDescriptorSetLayoutCreateInfo dslci;
    VkDescriptorPoolSize psz;
    VkDescriptorPoolCreateInfo dpci;
    VkDescriptorSetAllocateInfo dsai;
    VkSamplerCreateInfo sci;
    VkCommandPoolCreateInfo cpci;
    VkCommandBufferAllocateInfo cbai;
    VkFenceCreateInfo fci;
    VkSemaphoreCreateInfo semci;

    memset(&T, 0, sizeof(T));
    T.width = width; T.height = height;

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "vkmatrix";
    app.apiVersion = VK_API_VERSION_1_3;
    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, NULL, &T.instance) != VK_SUCCESS) {
        printf("vkmatrix: vkCreateInstance failed\n"); return -1;
    }

    pfnCreateSurface = (PFN_vkCreateEwokOSSurfaceVK_t)(void *)
        vkGetInstanceProcAddr(T.instance, "vkCreateEwokOSSurfaceVK");
    if (!pfnCreateSurface) { printf("vkmatrix: no EwokOS surface entry\n"); return -1; }
    if (pfnCreateSurface(T.instance, width, height, VK_FORMAT_B8G8R8A8_UNORM,
                         (void *)matrix_present, NULL, &T.surface) != VK_SUCCESS) {
        printf("vkmatrix: vkCreateEwokOSSurfaceVK failed\n"); return -1;
    }

    if (vkEnumeratePhysicalDevices(T.instance, &nphys, NULL) != VK_SUCCESS || nphys == 0) {
        printf("vkmatrix: no physical devices\n"); return -1;
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
        printf("vkmatrix: vkCreateDevice failed\n"); return -1;
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
    resize_effect_buffer();
    if (create_texture((uint32_t)eff_w, (uint32_t)eff_h) != 0) return -1;

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
        printf("vkmatrix: vkCreateRenderPass failed\n"); return -1;
    }

    if (create_swapchain_views_fbs() != 0) {
        printf("vkmatrix: swapchain views/framebuffers failed\n"); return -1;
    }

    /* fullscreen quad: two triangles, clip-space corners + matching UVs.
     * Vulkan NDC y grows downward, and the rain texture's row 0 is the top of
     * the image, so uv (0,0) belongs at the top-left corner (-1,-1). */
    {
        static const float quad_pos[6 * 4] = {
            -1, -1, 0, 1,   1, -1, 0, 1,   1,  1, 0, 1,
            -1, -1, 0, 1,   1,  1, 0, 1,  -1,  1, 0, 1,
        };
        static const float quad_uv[6 * 2] = {
            0, 0,  1, 0,  1, 1,
            0, 0,  1, 1,  0, 1,
        };
        if (hbuf_create(T.device, T.phys, &T.pos, sizeof(quad_pos),
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) != 0 ||
            hbuf_create(T.device, T.phys, &T.uv, sizeof(quad_uv),
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) != 0) {
            printf("vkmatrix: quad buffers failed\n"); return -1;
        }
        memcpy(T.pos.map, quad_pos, sizeof(quad_pos));
        memcpy(T.uv.map, quad_uv, sizeof(quad_uv));
    }

    /* sampler: the CPU device samples NEAREST anyway; the quad maps texel
     * centres 1:1 at the swapchain resolution so filtering never kicks in */
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
        printf("vkmatrix: vkCreateSampler failed\n"); return -1;
    }

    /* descriptor set layout: binding 0 = combined image sampler (FS) */
    memset(&ub, 0, sizeof(ub));
    ub.binding = 0;
    ub.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ub.descriptorCount = 1;
    ub.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    memset(&dslci, 0, sizeof(dslci));
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 1;
    dslci.pBindings = &ub;
    if (vkCreateDescriptorSetLayout(T.device, &dslci, NULL, &T.dsl) != VK_SUCCESS) {
        printf("vkmatrix: vkCreateDescriptorSetLayout failed\n"); return -1;
    }

    memset(&plci, 0, sizeof(plci));
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &T.dsl;
    if (vkCreatePipelineLayout(T.device, &plci, NULL, &T.pl) != VK_SUCCESS) {
        printf("vkmatrix: vkCreatePipelineLayout failed\n"); return -1;
    }

    memset(&smci, 0, sizeof(smci));
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = sizeof(kMatrixVs); smci.pCode = kMatrixVs;
    if (vkCreateShaderModule(T.device, &smci, NULL, &vs) != VK_SUCCESS) {
        printf("vkmatrix: VS module failed\n"); return -1;
    }
    smci.codeSize = sizeof(kMatrixFs); smci.pCode = kMatrixFs;
    if (vkCreateShaderModule(T.device, &smci, NULL, &fs) != VK_SUCCESS) {
        printf("vkmatrix: FS module failed\n"); return -1;
    }
    memset(stages, 0, sizeof(stages));
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs; stages[1].pName = "main";

    memset(vbind, 0, sizeof(vbind));
    vbind[0].binding = 0; vbind[0].stride = 4 * sizeof(float);
    vbind[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vbind[1].binding = 1; vbind[1].stride = 2 * sizeof(float);
    vbind[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    memset(vattr, 0, sizeof(vattr));
    vattr[0].location = 0; vattr[0].binding = 0; vattr[0].offset = 0;
    vattr[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vattr[1].location = 1; vattr[1].binding = 1; vattr[1].offset = 0;
    vattr[1].format = VK_FORMAT_R32G32_SFLOAT;
    memset(&vi, 0, sizeof(vi));
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = vbind;
    vi.vertexAttributeDescriptionCount = 2; vi.pVertexAttributeDescriptions = vattr;

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
        printf("vkmatrix: vkCreateGraphicsPipelines failed\n"); return -1;
    }
    vkDestroyShaderModule(T.device, vs, NULL);
    vkDestroyShaderModule(T.device, fs, NULL);

    psz.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    psz.descriptorCount = 1;
    memset(&dpci, 0, sizeof(dpci));
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &psz;
    if (vkCreateDescriptorPool(T.device, &dpci, NULL, &T.dpool) != VK_SUCCESS) {
        printf("vkmatrix: vkCreateDescriptorPool failed\n"); return -1;
    }
    memset(&dsai, 0, sizeof(dsai));
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = T.dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &T.dsl;
    if (vkAllocateDescriptorSets(T.device, &dsai, &T.dset) != VK_SUCCESS) {
        printf("vkmatrix: vkAllocateDescriptorSets failed\n"); return -1;
    }
    write_tex_descriptor();

    memset(&cpci, 0, sizeof(cpci));
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = T.qfi;
    if (vkCreateCommandPool(T.device, &cpci, NULL, &T.cpool) != VK_SUCCESS) {
        printf("vkmatrix: vkCreateCommandPool failed\n"); return -1;
    }
    memset(&cbai, 0, sizeof(cbai));
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = T.cpool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(T.device, &cbai, &T.cmd) != VK_SUCCESS) {
        printf("vkmatrix: vkAllocateCommandBuffers failed\n"); return -1;
    }
    memset(&fci, 0, sizeof(fci));
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(T.device, &fci, NULL, &T.fence) != VK_SUCCESS) {
        printf("vkmatrix: vkCreateFence failed\n"); return -1;
    }
    memset(&semci, 0, sizeof(semci));
    semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(T.device, &semci, NULL, &T.acquired) != VK_SUCCESS) {
        printf("vkmatrix: vkCreateSemaphore failed\n"); return -1;
    }

    resize_effect_buffer();

    /* GPU compute path: probe once before the first frame; any decline keeps
     * the CPU rain bands (never a silent slow path on a GPU-capable board) */
    pfnBufShm = (PFN_vkGetBufferMemoryShmEWOK_t)(void *)
        vkGetInstanceProcAddr(T.instance, "vkGetBufferMemoryShmEWOK");
    if (create_compute() == 0)
        probe_compute();

    T.ready = 1;
    return 0;
}

/* ---- frame --------------------------------------------------------------- */

/* ---- parallel row bands ---------------------------------------------------
 * The rain march and the bilinear upscale are both pure per-row work, so
 * RAIN_WORKERS pthreads split the rows between them; each rain worker owns a
 * private memo-cache set inside matrix.c (see MATRIX_MAX_WORKERS). */
#define RAIN_WORKERS 4

typedef struct {
    const matrix_frame *f;
    int y0, y1, worker;
} rain_band_arg;

static void *rain_band_proc(void *p)
{
    rain_band_arg *a = (rain_band_arg *)p;
    matrix_render_band(a->f, rain_buf ? rain_buf : eff_buf,
                       a->y0, a->y1, a->worker);
    return NULL;
}

static void render_rain(const matrix_frame *f)
{
    pthread_t tid[RAIN_WORKERS - 1];
    rain_band_arg arg[RAIN_WORKERS];
    int i;
    memset(tid, 0, sizeof(tid));
    for (i = 0; i < RAIN_WORKERS; i++) {
        arg[i].f = f;
        arg[i].worker = i;
        arg[i].y0 = eff_h * i / RAIN_WORKERS;
        arg[i].y1 = eff_h * (i + 1) / RAIN_WORKERS;
        if (i > 0) {
            if (pthread_create(&tid[i - 1], NULL, rain_band_proc, &arg[i]) != 0) {
                tid[i - 1] = 0;
                rain_band_proc(&arg[i]);   /* never leave rows unrendered */
            }
        }
    }
    rain_band_proc(&arg[0]);
    for (i = 1; i < RAIN_WORKERS; i++)
        if (tid[i - 1]) pthread_join(tid[i - 1], NULL);
    /* stream the finished frame into the NOCACHE shm canvas once: a bulk
     * memcpy is orders of magnitude cheaper than per-pixel uncached stores */
    if (rain_buf)
        memcpy(eff_buf, rain_buf, (size_t)eff_w * eff_h * 4);
}

static void render_frame(uint32_t idx, float dt)
{
    VkCommandBufferBeginInfo bi;
    VkRenderPassBeginInfo rpi;
    VkClearValue clear;
    VkSubmitInfo si;
    VkPresentInfoKHR pi;
    VkResult res;
    matrix_frame f;

    effect_time += dt * speed;
    f.time = effect_time;
    f.iterations = iterations;
    if (g_compute && T.cs_out.map &&
        T.cs_out_w == T.width && T.cs_out_h == T.height) {
        /* GPU rain at NATIVE window resolution.  The mouse latch is stored
         * in internal-canvas pixels; camera_build normalizes by f->width,
         * so rescale it to window pixels to keep the same camera. */
        float params[16];
        f.width = (int)T.width;
        f.height = (int)T.height;
        f.mouse[0] = mouse_gl[0] * (float)T.width / (float)(eff_w > 0 ? eff_w : 1);
        f.mouse[1] = mouse_gl[1] * (float)T.height / (float)(eff_h > 0 ? eff_h : 1);
        memset(params, 0, sizeof(params));
        matrix_gpu_params(&f, params);
        memcpy(T.cs_params.map, params, sizeof(params));

        memset(&bi, 0, sizeof(bi));
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(T.cmd, &bi) != VK_SUCCESS) return;
        vkCmdBindPipeline(T.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, T.cs_pipeline);
        vkCmdBindDescriptorSets(T.cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                T.cs_pl, 0, 1, &T.cs_dset, 0, NULL);
        vkCmdDispatch(T.cmd, (T.width * T.height + 63u) / 64u, 1, 1);
        if (vkEndCommandBuffer(T.cmd) != VK_SUCCESS) return;
        vkResetFences(T.device, 1, &T.fence);
        memset(&si, 0, sizeof(si));
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &T.cmd;
        if (vkQueueSubmit(T.queue, 1, &si, T.fence) != VK_SUCCESS) return;
        vkWaitForFences(T.device, 1, &T.fence, VK_TRUE, 2000000000ull);
    } else {
        f.width = eff_w;
        f.height = eff_h;
        f.mouse[0] = mouse_gl[0];
        f.mouse[1] = mouse_gl[1];
        render_rain(&f);
    }

    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(T.cmd, &bi) != VK_SUCCESS) return;

    clear.color.float32[0] = 0.0f;
    clear.color.float32[1] = 0.0f;
    clear.color.float32[2] = 0.0f;
    clear.color.float32[3] = 0.0f;   /* transparent: HUD composites over rain */
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

    /* HUD only: the rain reaches the screen through the present scale blit,
     * so no fullscreen textured quad is rasterized here (see matrix_present) */
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

/* Reference iMouse semantics: xy tracks the pressed position (GL bottom-left
 * origin, in internal-resolution pixels); both axes <= 10 means the auto demo
 * camera.  The latch survives button release, as on Shadertoy. */
static void latch_mouse(int wx, int wy)
{
    float mx = (float)wx * (float)eff_w / (float)T.width;
    float my = (float)((int)T.height - wy) * (float)eff_h / (float)T.height;
    if (mx <= 10.0f && my <= 10.0f) {
        mouse_gl[0] = 0.0f;
        mouse_gl[1] = 0.0f;
    } else {
        mouse_gl[0] = mx;
        mouse_gl[1] = my;
    }
}

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
        if (button == MOUSE_BUTTON_LEFT) {
            mouse_buttons[0] = down;
            /* steering only when the press is not consumed by the HUD */
            if (down && !io.WantCaptureMouse)
                latch_mouse(pos.x, pos.y);
            /* a clean tap (CLICK, not a drag) outside the HUD toggles the
             * overlay; when hidden, WantCaptureMouse is false everywhere so
             * the next tap brings it back */
            if (state == MOUSE_STATE_CLICK && !io.WantCaptureMouse)
                g_show_ui = !g_show_ui;
        }
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
        printf("vkmatrix: imgui vulkan error %d\n", (int)err);
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
        printf("vkmatrix: ImGui_ImplVulkan_Init failed\n");
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
    /* ImGui >= 1.87 ignores the legacy io.MousePos / io.MouseDown[] fields;
     * the backend must queue events, otherwise no widget is ever clickable
     * and io.WantCaptureMouse stays false. */
    io.AddMousePosEvent((float)mouse_x, (float)mouse_y);
    io.AddMouseButtonEvent(0, mouse_buttons[0]);
    io.AddMouseButtonEvent(1, mouse_buttons[1]);
    io.AddMouseButtonEvent(2, mouse_buttons[2]);
    io.KeyCtrl      = key_ctrl;
    io.KeyShift     = key_shift;
    io.KeyAlt       = key_alt;
    io.KeySuper     = key_super;

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();

    if (g_show_ui) {
        ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.55f);
        ImGui::Begin("vkmatrix", NULL,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("FPS: %d", fps);
        ImGui::TextColored(g_gpu ? ImVec4(0.40f, 1.00f, 0.55f, 1.0f)
                                 : ImVec4(1.00f, 0.70f, 0.30f, 1.0f),
                           "Renderer: %s",
                           g_compute ? "GPU compute" : (g_gpu ? "GPU" : "CPU"));
        if (g_compute)
            ImGui::Text("Rain: %ux%u native (compute, 40 iter)", T.width, T.height);
        else
            ImGui::Text("Rain: %dx%d -> %ux%u (%d workers)", eff_w, eff_h,
                        T.width, T.height, RAIN_WORKERS);
        ImGui::Text("Camera: %s", (mouse_gl[0] > 10.0f || mouse_gl[1] > 10.0f)
                                  ? "mouse" : "auto demo");
        /* in compute mode the resolution is native and iterations are baked into
         * the SPIR-V module, so the CPU-only knobs are disabled */
        if (g_compute) ImGui::BeginDisabled();
        if (ImGui::SliderInt("render scale", &render_scale, 1, 16)) {
            scale_auto = 0;
            resize_effect_buffer();
        }
        ImGui::SliderInt("iterations", &iterations, 8, 40);
        if (g_compute) ImGui::EndDisabled();
        ImGui::SliderFloat("speed", &speed, 0.0f, 3.0f, "%.2f");
        if (ImGui::Button("auto demo")) {
            mouse_gl[0] = 0.0f;
            mouse_gl[1] = 0.0f;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("(or click bottom-left)");
        if (ImGui::Button("Exit"))
            g_quit = true;   /* closed by loop(), not here (repaint holds lock) */
        ImGui::SameLine();
        ImGui::TextUnformatted("(click outside to hide)");
        ImGui::End();
    }
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
    /* deferred Exit-button close: safe here (main-loop context, no paint lock) */
    if (g_quit) {
        xwin_close((xwin_t *)p);
        return;
    }
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
    hbuf_destroy(T.device, &T.uv);
    hbuf_destroy(T.device, &T.cs_params);
    hbuf_destroy(T.device, &T.cs_out);
    if (T.cs_dpool)    vkDestroyDescriptorPool(T.device, T.cs_dpool, NULL);
    if (T.cs_pipeline) vkDestroyPipeline(T.device, T.cs_pipeline, NULL);
    if (T.cs_pl)       vkDestroyPipelineLayout(T.device, T.cs_pl, NULL);
    if (T.cs_dsl)      vkDestroyDescriptorSetLayout(T.device, T.cs_dsl, NULL);
    destroy_texture();
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
    (void)argc;
    (void)argv;

    if (init_vulkan(MATRIX_W, MATRIX_H) != 0) {
        printf("vkmatrix: Vulkan init failed\n");
        cleanup_vulkan();
        return -1;
    }
    init_imgui();

    x_init(&x, NULL);
    x.on_loop = loop;
    xwin = xwin_open(&x, -1, 32, 32, MATRIX_W, MATRIX_H, "vkmatrix", XWIN_STYLE_NORMAL);
    if (xwin == NULL) {
        cleanup_vulkan();
        return -1;
    }
    xwin->on_event = on_event;
    xwin->on_repaint = on_repaint;
    g_xwin = xwin;
    /* glmatrix precedent: fullscreen BEFORE the first repaint, so the
     * swapchain/texture are recreated once at panel size by on_repaint.
     * Synchronous: xinfo->wsr already carries the MAX-state size on return;
     * the 640x360 opening size stays as fallback if the WM rejects MAX. */
    xwin_fullscreen(xwin);
    xwin_set_visible(xwin, true);

    x_run(&x, xwin);

    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();

    xwin_destroy(xwin);
    cleanup_vulkan();
    if (eff_g) graph_free(eff_g);
    free(rain_buf);
    return 0;
}