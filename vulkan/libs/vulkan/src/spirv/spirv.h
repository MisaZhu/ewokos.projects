/*
 * spirv.h - the SPIR-V front-end contract shared by the software device (which
 * interprets a module) and the GPU back-end (which transpiles the supported
 * subset to GLSL).  A module is parsed once at vkCreateShaderModule; the parse
 * keeps the raw word stream plus the decoded instruction/type/decoration tables
 * the interpreter walks and the transpiler reads.
 *
 * The interpreter covers the instruction subset real compute/vertex/fragment
 * kernels emit: scalar+vector arithmetic, comparison, logic, conversion, select,
 * composite build/extract/insert, load/store through access chains into
 * Uniform / StorageBuffer / Workgroup / Private / Function storage, control flow
 * (branch, conditional branch, loop merge, selection merge, return), the common
 * GLSL.std.450 extended instructions, and the Vulkan built-in / interface
 * model.  Anything outside the subset is DECLINED at parse or run time (never
 * mis-executed), so an unsupported shader simply stays on the caller's fallback.
 */
#ifndef VULKAN_SPIRV_H
#define VULKAN_SPIRV_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPIRV_MAX_BINDINGS   32
#define SPIRV_MAX_LOCATIONS  16

/* Execution models (SPIR-V ExecutionModel enum values we care about). */
#define SPIRV_EXEC_VERTEX    0u
#define SPIRV_EXEC_FRAGMENT  4u
#define SPIRV_EXEC_GLCOMPUTE 5u

/* Descriptor resource kinds, reported per referenced binding. */
typedef enum spirv_res_kind {
    SPIRV_RES_STORAGE_BUFFER = 0,   /* buffer block (SSBO)         */
    SPIRV_RES_UNIFORM_BUFFER,       /* uniform block (UBO)         */
    SPIRV_RES_COMBINED_IMAGE_SAMPLER,
    SPIRV_RES_SAMPLER,
    SPIRV_RES_SAMPLED_IMAGE,
    SPIRV_RES_STORAGE_IMAGE,
    SPIRV_RES_PUSH_CONSTANT,
    SPIRV_RES_INPUT,
    SPIRV_RES_OUTPUT
} spirv_res_kind;

typedef struct spirv_binding_desc {
    uint32_t       set;
    uint32_t       binding;
    spirv_res_kind kind;
} spirv_binding_desc;

/* one entry point of a module */
typedef struct spirv_entry {
    char     name[64];
    uint32_t exec_model;
    uint32_t local_size[3];   /* GLCompute: LocalSize or LocalSizeId resolved */
} spirv_entry;

/* A bound resource the interpreter reads/writes during one invocation. */
typedef struct spirv_binding {
    uint32_t set, binding;
    spirv_res_kind kind;
    void    *data;          /* buffer/image host base                       */
    size_t   size;          /* buffer byte size                             */
    /* image geometry (combined/storage/sampled images) */
    uint32_t width, height, depth;
    uint32_t format;        /* VkFormat                                     */
    uint32_t bpp;
    int      writable;
} spirv_binding;

/* Everything one invocation observes or produces.  The caller fills the inputs
 * (builtins, location inputs, bound resources, push constants); the interpreter
 * writes location outputs and the Position/PointSize builtins. */
typedef struct spirv_resources {
    spirv_binding bindings[SPIRV_MAX_BINDINGS];
    uint32_t      num_bindings;

    const void   *push;
    uint32_t      push_size;

    /* per-location interface (vec4 each; unused slots ignored) */
    float in_loc[SPIRV_MAX_LOCATIONS][4];
    float out_loc[SPIRV_MAX_LOCATIONS][4];
    uint32_t num_in_loc, num_out_loc;

    /* compute builtins */
    uint32_t global_id[3], local_id[3], workgroup_id[3], num_wg[3];
    uint32_t local_index;
    uint32_t wg_size[3];

    /* vertex builtins */
    uint32_t vertex_index, instance_index;

    /* fragment builtins */
    float frag_coord[4];
    int   front_facing;

    /* outputs (written by the interpreter) */
    float position[4];
    float point_size;
    int   wrote_position;
    int   killed;          /* fragment shader executed OpKill / discard */
} spirv_resources;

typedef struct spirv_module spirv_module;

/* Parse + validate a SPIR-V word stream.  Returns NULL on failure with a
 * human-readable reason in err.  On success the module owns a copy of nothing
 * (it indexes the caller's words), so the caller must keep `words` alive for
 * the module's lifetime -- vk_shader_module does (it stores the owned copy). */
spirv_module *spirv_parse(const uint32_t *words, size_t nwords,
                          char *err, size_t errsz);
void          spirv_free(spirv_module *m);

/* Entry-point introspection. */
int                  spirv_num_entries(const spirv_module *m);
const spirv_entry   *spirv_get_entry(const spirv_module *m, int i);
const spirv_entry   *spirv_find_entry(const spirv_module *m, const char *name,
                                      uint32_t exec_model);

/* Enumerate the descriptor resources an entry point references (for the caller
 * to bind).  Returns the count written (<= max). */
int spirv_entry_bindings(const spirv_module *m, const spirv_entry *e,
                         spirv_binding_desc *out, int max);

/* Run one invocation of `e` against `res`.  Returns 0 on success, non-zero when
 * the module hits an unsupported construct (the caller falls back). */
int spirv_run(const spirv_module *m, const spirv_entry *e, spirv_resources *res);

#ifdef __cplusplus
}
#endif

#endif /* VULKAN_SPIRV_H */
