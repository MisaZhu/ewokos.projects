/*
 * internal.h - libvulkan runtime object model, shared by every src/api,
 * src/runtime, src/spirv, src/cpu and src/backend translation unit.  Not
 * installed; applications see only the public Khronos vulkan headers.
 *
 * Design (the structural twin of projects/opencl's internal.h):
 *   - one VkInstance owns one VkPhysicalDevice (the EwokOS software device).
 *     On machines whose bsp_vulkan reports hardware support (raspi5 today) the
 *     same physical device additionally carries a GPU back-end; execution is
 *     chosen per VkDevice by a vk_device_backend vtable.  A machine without GPU
 *     support therefore runs the identical software path with no divergence.
 *   - the runtime core is device-agnostic: it owns object lifetime, the handle
 *     registry, command recording, queues, memory residency and sync objects,
 *     and delegates shader execution (compute dispatch + graphics draw) to the
 *     selected back-end (cpu/ or gpu/).
 *   - shaders are SPIR-V.  src/spirv parses + validates a module once at
 *     vkCreateShaderModule; the software device interprets it, the GPU back-end
 *     transpiles the supported subset to GLSL and compiles it on-device.
 *
 * Handles: dispatchable objects (VkInstance/VkPhysicalDevice/VkDevice/VkQueue/
 * VkCommandBuffer) are pointers to structs whose FIRST member is a dispatch
 * pointer, per the Vulkan ABI.  Non-dispatchable handles are the object struct
 * address round-tripped through uintptr_t (uniform across arm32/aarch64/x86
 * whether the header models them as pointers or uint64_t).
 */
#ifndef VULKAN_INTERNAL_H
#define VULKAN_INTERNAL_H

#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* forward decl of the SPIR-V module representation (spirv/spirv.h) */
struct spirv_module;

#define VK_MAX_QUEUE_FAMILIES       4
#define VK_MAX_QUEUES_PER_FAMILY    8
/* VK_MAX_MEMORY_TYPES / VK_MAX_MEMORY_HEAPS come from vulkan_core.h (32 / 16). */
#define VK_MAX_DESCRIPTOR_SETS      8
#define VK_MAX_PUSH_CONSTANT_RANGE  256
#define VK_MAX_COLOR_ATTACHMENTS    4
#define VK_MAX_VERTEX_BINDINGS      8
#define VK_MAX_VERTEX_ATTRIBUTES    16
#define VK_MAX_SHADER_STAGES        5
#define VK_MAX_RENDER_PASS_ATTACH   8
#define VK_MAX_SUBPASS_ATTACH       8

/* Internal sentinel stored in a dynamic-rendering attachment's layout slot to
 * flag loadOp==CLEAR (never a real VkImageLayout).  cmd.c writes it, the
 * executor reads it to apply the clear. */
#define VK_LAYOUT_CLEAR_EWOK        ((VkImageLayout)0x7fffffff)

/* ---- handle <-> pointer round-trip (non-dispatchable) -------------------- */
#define VK_HANDLE_FROM_PTR(t, p)  ((t)(uintptr_t)(void *)(p))
#define VK_PTR_FROM_HANDLE(h)     ((void *)(uintptr_t)(h))

/* ---- object tags --------------------------------------------------------- */
typedef enum vk_obj_type {
    VK_OBJ_INSTANCE = 1,
    VK_OBJ_PHYSDEV,
    VK_OBJ_DEVICE,
    VK_OBJ_QUEUE,
    VK_OBJ_CMDBUF,
    VK_OBJ_DEVICE_MEMORY,
    VK_OBJ_BUFFER,
    VK_OBJ_BUFFER_VIEW,
    VK_OBJ_IMAGE,
    VK_OBJ_IMAGE_VIEW,
    VK_OBJ_SAMPLER,
    VK_OBJ_SHADER_MODULE,
    VK_OBJ_DESCRIPTOR_SET_LAYOUT,
    VK_OBJ_PIPELINE_LAYOUT,
    VK_OBJ_DESCRIPTOR_POOL,
    VK_OBJ_DESCRIPTOR_SET,
    VK_OBJ_DESCRIPTOR_UPDATE_TEMPLATE,
    VK_OBJ_PIPELINE,
    VK_OBJ_PIPELINE_CACHE,
    VK_OBJ_RENDER_PASS,
    VK_OBJ_FRAMEBUFFER,
    VK_OBJ_COMMAND_POOL,
    VK_OBJ_FENCE,
    VK_OBJ_SEMAPHORE,
    VK_OBJ_EVENT,
    VK_OBJ_QUERY_POOL,
    VK_OBJ_SAMPLER_YCBCR_CONVERSION,
    VK_OBJ_PRIVATE_DATA_SLOT,
    VK_OBJ_SURFACE,
    VK_OBJ_SWAPCHAIN,
    VK_OBJ_MAX
} vk_obj_type;

#define VK_OBJ_MAGIC 0x564B4F30u   /* "VKO0" */

/* every runtime object begins with this header */
#define VK_OBJ_HEADER            \
    uint32_t      magic;         \
    vk_obj_type   type;

/* dispatchable objects begin with a dispatch pointer (Vulkan ABI).  We keep a
 * single global trampoline table; getprocaddr resolves from a static name map,
 * so the pointer is a tag, not a per-object vtable. */
struct vk_dispatch { uintptr_t tag; };
#define VK_DISPATCHABLE_HEADER   \
    const struct vk_dispatch *dispatch; \
    VK_OBJ_HEADER

/* forward declarations of structs referenced (by pointer) before their full
 * definition below, so the object model can be written in dependency order. */
struct vk_render_pass;
struct vk_framebuffer;
struct vk_event;
struct vk_query_pool;
struct vk_command_buffer;
struct vk_command_pool;

/* ---- device backend vtable ---------------------------------------------- */
struct vk_device;
struct vk_command_buffer;
struct vk_pipeline;
struct vk_draw_ctx;

typedef struct vk_device_backend {
    const char *name;

    /* Execute one compute dispatch recorded in a command buffer. */
    VkResult (*dispatch)(struct vk_command_buffer *cb, struct vk_draw_ctx *dc,
                         uint32_t gx, uint32_t gy, uint32_t gz);

    /* Execute one graphics draw recorded in a command buffer. */
    VkResult (*draw)(struct vk_command_buffer *cb, struct vk_draw_ctx *dc,
                     uint32_t vertexCount, uint32_t firstVertex,
                     uint32_t instanceCount, uint32_t firstInstance,
                     const uint32_t *index_data, VkIndexType index_type);

    /* Release any backend-private state hung off a pipeline. */
    void (*pipeline_free)(struct vk_pipeline *p);
} vk_device_backend;

extern const vk_device_backend vk_backend_cpu;
#ifdef VULKAN_GPU_BACKEND
extern const vk_device_backend vk_backend_gpu;
#endif

/* ---- instance ------------------------------------------------------------ */
typedef struct vk_instance {
    VK_DISPATCHABLE_HEADER
    VkApplicationInfo app_info;
    char        app_name[128];
    char        engine_name[128];
    uint32_t    api_version;
    struct vk_physical_device *phys[VK_MAX_QUEUE_FAMILIES];
    uint32_t    num_phys;
} vk_instance;

/* ---- physical device ----------------------------------------------------- */
typedef struct vk_physical_device {
    VK_DISPATCHABLE_HEADER
    vk_instance  *instance;
    VkPhysicalDeviceProperties      props;
    VkPhysicalDeviceFeatures        features;
    VkPhysicalDeviceMemoryProperties mem;
    VkQueueFamilyProperties         qfam[VK_MAX_QUEUE_FAMILIES];
    uint32_t                        num_qfam;
    int                             gpu_available;   /* probe result           */
} vk_physical_device;

/* ---- device -------------------------------------------------------------- */
typedef struct vk_queue {
    VK_DISPATCHABLE_HEADER
    struct vk_device *device;
    uint32_t          family;
    uint32_t          index;
} vk_queue;

typedef struct vk_device {
    VK_DISPATCHABLE_HEADER
    vk_physical_device       *phys;
    vk_instance              *instance;
    const vk_device_backend  *backend;
    VkPhysicalDeviceFeatures  features;
    vk_queue                  queues[VK_MAX_QUEUE_FAMILIES][VK_MAX_QUEUES_PER_FAMILY];
    uint32_t                  num_queues[VK_MAX_QUEUE_FAMILIES];
    void                     *backend_data;    /* GPU back-end private state   */
} vk_device;

/* ---- memory / buffer / image --------------------------------------------- */
typedef struct vk_device_memory {
    VK_OBJ_HEADER
    vk_device    *device;
    VkDeviceSize  size;
    uint32_t      type_index;
    void         *host;          /* always host-resident in the software model */
    int32_t       gfx_shm;       /* >0: GPU-addressable swapchain backing */
    void         *mapped;        /* non-NULL while mapped                      */
    VkDeviceSize  map_offset, map_size;
} vk_device_memory;

typedef struct vk_buffer {
    VK_OBJ_HEADER
    vk_device          *device;
    VkDeviceSize        size;
    VkBufferUsageFlags  usage;
    vk_device_memory   *mem;
    VkDeviceSize        offset;
} vk_buffer;

typedef struct vk_buffer_view {
    VK_OBJ_HEADER
    vk_buffer  *buffer;
    VkFormat    format;
    VkDeviceSize offset, range;
} vk_buffer_view;

typedef struct vk_image {
    VK_OBJ_HEADER
    vk_device        *device;
    VkImageType       image_type;
    VkFormat          format;
    VkExtent3D        extent;
    uint32_t          mip_levels;
    uint32_t          array_layers;
    VkSampleCountFlagBits samples;
    VkImageTiling     tiling;
    VkImageUsageFlags usage;
    VkImageLayout     layout;
    vk_device_memory *mem;
    VkDeviceSize      offset;
    uint32_t          bpp;       /* bytes per texel, resolved from format      */
    uint32_t          row_pitch; /* bytes per row of mip 0 / layer 0           */
} vk_image;

typedef struct vk_image_view {
    VK_OBJ_HEADER
    vk_image              *image;
    VkImageViewType        view_type;
    VkFormat               format;
    VkImageSubresourceRange range;
} vk_image_view;

typedef struct vk_sampler {
    VK_OBJ_HEADER
    VkFilter        mag, min_f;
    VkSamplerAddressMode addr_u, addr_v, addr_w;
    float           mip_bias, max_aniso;
    VkBool32        aniso_enable;
    VkBorderColor   border;
    VkBool32        unnormalized;
    VkCompareOp     compare;
} vk_sampler;

/* ---- shader module ------------------------------------------------------- */
typedef struct vk_shader_module {
    VK_OBJ_HEADER
    uint32_t           *code;        /* owned copy of the SPIR-V words         */
    size_t              code_size;   /* in dwords                              */
    struct spirv_module *module;     /* parsed (NULL if parse failed)          */
    /* Reference count: 1 for the creator plus 1 per pipeline stage that embeds
     * this module.  vkDestroyShaderModule only releases the creator's ref, so a
     * pipeline keeps the parsed SPIR-V alive until it is itself destroyed (the
     * spec allows destroying a module as soon as the pipeline is created). */
    uint32_t            refs;
} vk_shader_module;

/* ---- descriptors --------------------------------------------------------- */
typedef struct vk_descriptor_set_layout {
    VK_OBJ_HEADER
    VkDescriptorSetLayoutBinding bindings[32];
    uint32_t                     num_bindings;
} vk_descriptor_set_layout;

typedef struct vk_pipeline_layout {
    VK_OBJ_HEADER
    vk_descriptor_set_layout *sets[VK_MAX_DESCRIPTOR_SETS];
    uint32_t                  num_sets;
    VkPushConstantRange       push[4];
    uint32_t                  num_push;
} vk_pipeline_layout;

typedef struct vk_descriptor_update_template {
    VK_OBJ_HEADER
    uint32_t dummy;
} vk_descriptor_update_template;

/* one written descriptor binding */
typedef struct vk_descriptor_binding {
    VkDescriptorType type;
    uint32_t         binding;
    /* buffer info */
    vk_buffer       *buffer;
    VkDeviceSize     offset, range;
    /* image info */
    vk_image_view   *view;
    vk_sampler      *sampler;
    VkImageLayout    layout;
    uint32_t         count;
} vk_descriptor_binding;

typedef struct vk_descriptor_set {
    VK_OBJ_HEADER
    vk_descriptor_set_layout *layout;
    vk_descriptor_binding     bindings[64];
    uint32_t                  num_bindings;
} vk_descriptor_set;

typedef struct vk_descriptor_pool {
    VK_OBJ_HEADER
    vk_device         *device;
    VkDescriptorPoolSize sizes[8];
    uint32_t          num_sizes;
    uint32_t          max_sets;
    vk_descriptor_set *sets[256];
    uint32_t          num_sets;
} vk_descriptor_pool;

/* ---- pipeline ------------------------------------------------------------ */
typedef struct vk_pipeline_stage {
    VkShaderStageFlagBits stage;
    vk_shader_module     *module;
    char                  entry[64];
    /* specialization info (ignored beyond storage for now) */
} vk_pipeline_stage;

typedef struct vk_vertex_binding {
    uint32_t binding, stride;
    VkVertexInputRate rate;
} vk_vertex_binding;

typedef struct vk_vertex_attr {
    uint32_t location, binding, offset;
    VkFormat format;
} vk_vertex_attr;

typedef struct vk_pipeline {
    VK_OBJ_HEADER
    vk_device          *device;
    VkPipelineBindPoint bind_point;
    vk_pipeline_layout *layout;

    vk_pipeline_stage   stages[VK_MAX_SHADER_STAGES];
    uint32_t            num_stages;

    /* graphics state (subset that the software rasterizer honours) */
    vk_vertex_binding   vtx_bindings[VK_MAX_VERTEX_BINDINGS];
    uint32_t            num_vtx_bindings;
    vk_vertex_attr      vtx_attrs[VK_MAX_VERTEX_ATTRIBUTES];
    uint32_t            num_vtx_attrs;
    VkPrimitiveTopology topology;
    VkBool32            depth_test_enable, depth_write_enable;
    VkCompareOp         depth_compare;
    VkBool32            blend_enable;
    VkBlendFactor       src_color, dst_color, src_alpha, dst_alpha;
    VkBlendOp           color_op, alpha_op;
    VkCullModeFlags     cull_mode;
    VkFrontFace         front_face;
    VkBool32            rasterizer_discard;

    /* render pass linkage */
    struct vk_render_pass *render_pass;
    uint32_t            subpass;

    void               *backend_data;   /* GPU-compiled program handle(s)      */
} vk_pipeline;

typedef struct vk_pipeline_cache {
    VK_OBJ_HEADER
    uint8_t *data;
    size_t   size;
} vk_pipeline_cache;

/* ---- render pass / framebuffer ------------------------------------------- */
typedef struct vk_attachment_desc {
    VkFormat format;
    VkSampleCountFlagBits samples;
    VkAttachmentLoadOp  load;
    VkAttachmentStoreOp store;
    VkAttachmentLoadOp  stencil_load;
    VkAttachmentStoreOp stencil_store;
    VkImageLayout initial, final_;
} vk_attachment_desc;

typedef struct vk_render_pass {
    VK_OBJ_HEADER
    vk_attachment_desc attachments[VK_MAX_RENDER_PASS_ATTACH];
    uint32_t           num_attachments;
    /* single-subpass model is enough for the software device */
    VkAttachmentReference color_refs[VK_MAX_SUBPASS_ATTACH];
    uint32_t              num_color_refs;
    VkAttachmentReference depth_ref;
    VkBool32              has_depth;
} vk_render_pass;

typedef struct vk_framebuffer {
    VK_OBJ_HEADER
    vk_render_pass *render_pass;
    vk_image_view  *views[VK_MAX_RENDER_PASS_ATTACH];
    uint32_t        num_views;
    uint32_t        width, height, layers;
} vk_framebuffer;

/* ---- command buffer ------------------------------------------------------ */
typedef enum vk_cmd_op {
    VK_CMD_BIND_PIPELINE,
    VK_CMD_BIND_DESCRIPTOR_SETS,
    VK_CMD_BIND_VERTEX_BUFFERS,
    VK_CMD_BIND_INDEX_BUFFER,
    VK_CMD_PUSH_CONSTANTS,
    VK_CMD_SET_VIEWPORT,
    VK_CMD_SET_SCISSOR,
    VK_CMD_SET_BLEND_CONSTANTS,
    VK_CMD_SET_LINE_WIDTH,
    VK_CMD_SET_DEPTH_BIAS,
    VK_CMD_BEGIN_RENDER_PASS,
    VK_CMD_NEXT_SUBPASS,
    VK_CMD_END_RENDER_PASS,
    VK_CMD_BEGIN_RENDERING,
    VK_CMD_END_RENDERING,
    VK_CMD_DRAW,
    VK_CMD_DRAW_INDEXED,
    VK_CMD_DISPATCH,
    VK_CMD_COPY_BUFFER,
    VK_CMD_COPY_BUFFER_TO_IMAGE,
    VK_CMD_COPY_IMAGE_TO_BUFFER,
    VK_CMD_COPY_IMAGE,
    VK_CMD_BLIT_IMAGE,
    VK_CMD_CLEAR_COLOR_IMAGE,
    VK_CMD_CLEAR_DEPTH_STENCIL_IMAGE,
    VK_CMD_CLEAR_ATTACHMENTS,
    VK_CMD_FILL_BUFFER,
    VK_CMD_UPDATE_BUFFER,
    VK_CMD_PIPELINE_BARRIER,
    VK_CMD_SET_EVENT,
    VK_CMD_RESET_EVENT,
    VK_CMD_WAIT_EVENTS,
    VK_CMD_WRITE_TIMESTAMP,
    VK_CMD_BEGIN_QUERY,
    VK_CMD_END_QUERY,
    VK_CMD_RESET_QUERY_POOL,
    VK_CMD_COPY_QUERY_POOL_RESULTS,
    VK_CMD_EXECUTE_COMMANDS
} vk_cmd_op;

/* a recorded command: op + a union of payloads (self-contained copies) */
typedef struct vk_cmd {
    vk_cmd_op op;
    union {
        struct { vk_pipeline *pipeline; } bind_pipeline;
        struct {
            uint32_t first_set, set_count;
            vk_descriptor_set *sets[VK_MAX_DESCRIPTOR_SETS];
            uint32_t dyn_offsets[8]; uint32_t num_dyn;
            vk_pipeline_layout *layout;
        } bind_desc;
        struct {
            uint32_t first, count;
            vk_buffer *buffers[VK_MAX_VERTEX_BINDINGS];
            VkDeviceSize offsets[VK_MAX_VERTEX_BINDINGS];
        } bind_vb;
        struct { vk_buffer *buffer; VkDeviceSize offset; VkIndexType type; } bind_ib;
        struct { uint32_t offset, size; uint8_t data[VK_MAX_PUSH_CONSTANT_RANGE]; } push;
        struct { VkViewport vp; } viewport;
        struct { VkRect2D sc; } scissor;
        struct { float c[4]; } blend_const;
        struct { float w; } line_width;
        struct { float cf, sb, cl; } depth_bias;
        struct {
            vk_render_pass *rp; vk_framebuffer *fb; VkRect2D area;
            uint32_t num_clears; VkClearValue clears[VK_MAX_RENDER_PASS_ATTACH];
        } begin_rp;
        struct {
            uint32_t num_color; vk_image_view *color[VK_MAX_COLOR_ATTACHMENTS];
            VkClearValue color_clear[VK_MAX_COLOR_ATTACHMENTS];
            VkImageLayout color_layout[VK_MAX_COLOR_ATTACHMENTS];
            vk_image_view *depth; VkClearValue depth_clear; VkImageLayout depth_layout;
            uint32_t width, height;
        } begin_rendering;
        struct {
            vk_pipeline *pipeline;
            uint32_t vertexCount, firstVertex, instanceCount, firstInstance;
            /* snapshot of bound state needed at execute time */
            vk_descriptor_set *sets[VK_MAX_DESCRIPTOR_SETS]; uint32_t first_set, set_count;
            uint8_t push[VK_MAX_PUSH_CONSTANT_RANGE]; uint32_t push_size;
            vk_buffer *vb[VK_MAX_VERTEX_BINDINGS]; VkDeviceSize vb_off[VK_MAX_VERTEX_BINDINGS];
            vk_buffer *ib; VkDeviceSize ib_off; VkIndexType ib_type;
            VkViewport vp; VkRect2D sc; int has_vp, has_sc;
            /* active render target (render pass or dynamic rendering) */
            vk_image_view *color[VK_MAX_COLOR_ATTACHMENTS]; uint32_t num_color;
            vk_image_view *depth;
            uint32_t rt_width, rt_height;
        } draw;
        struct {
            vk_pipeline *pipeline;
            uint32_t gx, gy, gz;
            vk_descriptor_set *sets[VK_MAX_DESCRIPTOR_SETS]; uint32_t first_set, set_count;
            uint8_t push[VK_MAX_PUSH_CONSTANT_RANGE]; uint32_t push_size;
        } dispatch;
        struct { vk_buffer *src, *dst; VkBufferCopy regions[16]; uint32_t n; } copy_buf;
        struct {
            vk_buffer *src; vk_image *dst; VkBufferImageCopy regions[16]; uint32_t n;
        } copy_b2i;
        struct {
            vk_image *src; vk_buffer *dst; VkBufferImageCopy regions[16]; uint32_t n;
        } copy_i2b;
        struct { vk_image *src, *dst; VkImageCopy regions[8]; uint32_t n; } copy_img;
        struct { vk_image *src, *dst; VkImageBlit regions[8]; uint32_t n; VkFilter filter; } blit;
        struct { vk_image *img; VkImageSubresourceRange range; VkClearColorValue color; } clear_color;
        struct { vk_image *img; VkImageSubresourceRange range; VkClearDepthStencilValue ds; } clear_ds;
        struct { vk_buffer *dst; VkDeviceSize offset, size; uint32_t data; } fill;
        struct { vk_buffer *dst; VkDeviceSize offset, size; uint8_t data[4096]; } update;
        struct { uint32_t dummy; } barrier;
        struct { struct vk_event *event; } event_op;
        struct { struct vk_query_pool *pool; uint32_t query; } query_op;
        struct { struct vk_query_pool *pool; uint32_t first, count; uint64_t dst; } query_results;
        struct { struct vk_command_buffer **cmds; uint32_t n; } exec;
    } u;
} vk_cmd;

typedef struct vk_command_buffer {
    VK_DISPATCHABLE_HEADER
    struct vk_command_pool *pool;
    vk_device       *device;
    VkCommandBufferLevel level;
    VkCommandBufferUsageFlags usage;

    /* recording state */
    int               recording;         /* inside begin/end                   */
    int               in_render_pass;
    int               in_rendering;
    vk_cmd           *cmds;
    uint32_t          num_cmds, cap_cmds;

    /* bound state (snapshotted into each draw/dispatch command) */
    vk_pipeline      *pipeline_gfx, *pipeline_compute;
    vk_descriptor_set *sets[VK_MAX_DESCRIPTOR_SETS];
    uint32_t          first_set, set_count;
    uint8_t           push[VK_MAX_PUSH_CONSTANT_RANGE];
    uint32_t          push_size;
    vk_buffer        *vb[VK_MAX_VERTEX_BINDINGS];
    VkDeviceSize      vb_off[VK_MAX_VERTEX_BINDINGS];
    vk_buffer        *ib; VkDeviceSize ib_off; VkIndexType ib_type;
    VkViewport        vp; VkRect2D sc; int has_vp, has_sc;

    /* active render target (render pass or dynamic rendering) */
    vk_image_view    *color[VK_MAX_COLOR_ATTACHMENTS]; uint32_t num_color;
    vk_image_view    *depth;
    uint32_t          rt_width, rt_height;
} vk_command_buffer;

typedef struct vk_command_pool {
    VK_OBJ_HEADER
    vk_device *device;
    uint32_t   family;
    vk_command_buffer *buffers[256];
    uint32_t   num_buffers;
} vk_command_pool;

/* ---- sync / query -------------------------------------------------------- */
typedef struct vk_fence {
    VK_OBJ_HEADER
    vk_device *device;
    VkBool32   signaled;
} vk_fence;

typedef struct vk_semaphore {
    VK_OBJ_HEADER
    vk_device *device;
    VkSemaphoreType sem_type;
    uint64_t   value;      /* timeline counter; binary uses 0/1              */
    VkBool32   signaled;
} vk_semaphore;

typedef struct vk_event {
    VK_OBJ_HEADER
    vk_device *device;
    VkBool32   set;
} vk_event;

typedef struct vk_query_pool {
    VK_OBJ_HEADER
    vk_device    *device;
    VkQueryType   qtype;
    uint32_t      count;
    uint64_t     *results;
} vk_query_pool;

typedef struct vk_sampler_ycbcr_conversion {
    VK_OBJ_HEADER
    uint32_t dummy;
} vk_sampler_ycbcr_conversion;

typedef struct vk_private_data_slot {
    VK_OBJ_HEADER
    uint32_t dummy;
} vk_private_data_slot;

/* ---- WSI (EwokOS surface + swapchain) ------------------------------------ */
/* Present callback the app registers when it creates an EwokOS surface: the
 * runtime renders into the swapchain image, then hands its pixels here to push
 * to the window.  Keeping presentation a callback leaves libvulkan.a free of any
 * libx/libgraph dependency (the app links those). */
typedef void (*vk_ewokos_present_fn)(void *user, const void *pixels,
                                     uint32_t width, uint32_t height,
                                     uint32_t stride_bytes, VkFormat format,
                                     int shm_id);

typedef struct vk_surface {
    VK_OBJ_HEADER
    vk_instance           *instance;
    uint32_t               width, height;
    VkFormat               format;
    vk_ewokos_present_fn   present;
    void                  *user;
} vk_surface;

typedef struct vk_swapchain {
    VK_OBJ_HEADER
    vk_device    *device;
    vk_surface   *surface;
    uint32_t      width, height;
    VkFormat      format;
    VkImageUsageFlags usage;
    vk_image     *images[4];
    vk_image_view *views[4];
    vk_device_memory *mems[4];
    uint32_t      count;
    uint32_t      current;
} vk_swapchain;

/* ---- draw execution context (shared by both back-ends) ------------------- */
typedef struct vk_draw_ctx {
    vk_pipeline       *pipeline;
    vk_descriptor_set *sets[VK_MAX_DESCRIPTOR_SETS];
    uint32_t           first_set, set_count;
    const uint8_t     *push; uint32_t push_size;
    /* graphics targets */
    vk_image_view     *color[VK_MAX_COLOR_ATTACHMENTS]; uint32_t num_color;
    vk_image_view     *depth;
    uint32_t           rt_width, rt_height;
    VkViewport         vp; VkRect2D sc; int has_vp, has_sc;
    /* vertex/index */
    vk_buffer         *vb[VK_MAX_VERTEX_BINDINGS]; VkDeviceSize vb_off[VK_MAX_VERTEX_BINDINGS];
    vk_buffer         *ib; VkDeviceSize ib_off; VkIndexType ib_type;

    /* GPU graphics clear handoff.  The GPU back-end's first draw reproduces a
     * supported colour/depth clear on the device (first_frame fill and its
     * PRIVATE depth surface), so the executor can skip the redundant host
     * clear.  cpu_draw() applies that deferred clear before a CPU replay.
     * All fields default to 0 (memset), leaving ordinary CPU draws unchanged. */
    int      gfx_first_draw;      /* 1 = first draw since begin_render_pass/rendering */
    int      gfx_color_clear;     /* 1 = colour attachment[0] loadOp was CLEAR */
    float    gfx_clear_color[4];  /* RGBA float clear value (attachment[0]) */
    int      gfx_depth_clear;     /* 1 = the depth attachment loadOp was CLEAR */
    float    gfx_clear_depth;     /* depth clear value */
} vk_draw_ctx;

/* ---- shared runtime helpers (src/runtime) -------------------------------- */

/* The singleton instance/physical-device registry (initialised on first use). */
vk_instance          *vk_runtime_instance(void);
vk_physical_device   *vk_runtime_physical(void);

/* Object allocation with the common header pre-filled. */
void *vk_obj_alloc(size_t size, vk_obj_type type);
/* Allocate a DISPATCHABLE object: the vk_dispatch pointer occupies offset 0,
 * followed by the magic + type header. */
void *vk_obj_alloc_dispatch(size_t size, vk_obj_type type);
void  vk_obj_free(void *obj);
int   vk_obj_valid(const void *obj, vk_obj_type type);
/* Validate a DISPATCHABLE object (header lives after the leading dispatch ptr). */
int   vk_obj_valid_dispatch(const void *obj, vk_obj_type type);

/* Handle helpers. */
#define VK_H2P(h)      VK_PTR_FROM_HANDLE(h)
#define VK_P2H(t, p)   VK_HANDLE_FROM_PTR(t, p)

/* Copy data with the standard vkGet*Properties size/count semantics.  Returns
 * the number of items written (or that would be written when out is NULL). */
uint32_t vk_write_out(uint32_t avail_count, void *out, uint32_t *out_count,
                      const void *items, uint32_t item_count, size_t item_size);

/* Fill a VkResult-count style array (extension/layer enumeration). */
VkResult vk_enum_write(uint32_t *count, uint32_t avail, void *out,
                       const void *items, uint32_t item_count, size_t item_size);

/* Apply a render-pass clear deferred by the GPU executor before CPU replay. */
void vk_clear_deferred(vk_draw_ctx *dc);

/* Monotonic nanosecond clock for timestamps. */
uint64_t vk_now_ns(void);

/* Resolve a buffer's device-visible host pointer (memory->host + offset). */
void *vk_buffer_ptr(vk_buffer *b);
/* Resolve an image's device-visible host pointer. */
void *vk_image_ptr(vk_image *im);

/* Bytes-per-texel + row pitch helpers for a VkFormat (0 when unsupported). */
uint32_t vk_format_bpp(VkFormat f);

/* Command recording helpers. */
vk_cmd *vk_cmd_push(vk_command_buffer *cb, vk_cmd_op op);

/* Execute one recorded command buffer against its device back-end. */
VkResult vk_execute_command_buffer(vk_command_buffer *cb);

/* Snapshot the command buffer's bound state into a draw/dispatch command. */
void vk_snapshot_state(vk_command_buffer *cb, vk_cmd *c);

/* The global dispatch tag stored as the first member of dispatchable objects. */
extern const struct vk_dispatch vk_global_dispatch;

#ifdef __cplusplus
}
#endif

#endif /* VULKAN_INTERNAL_H */
