/*
 * cl_mem.c - buffer memory objects: create/sub-buffer, retain/release, info,
 * destructor callback.  GPU-context buffers prefer a shared contig backing;
 * CL_MEM_USE_HOST_PTR adopts the caller's pointer instead.  Images are created
 * in cl_image.c but share struct _cl_mem and its info/refcount paths.
 */
#include "internal.h"
#include <stdlib.h>
#ifdef OPENCL_GPU_BACKEND
#include <g2dclient/g2dclient.h>
#include <sys/shm.h>
#endif

/* destructor callback list (small, per object) */
typedef struct mem_dtor {
    struct _cl_mem *mem;
    void (*fn)(cl_mem, void *);
    void *user_data;
    struct mem_dtor *next;
} mem_dtor;
static mem_dtor *g_dtors;

static void fire_dtors(struct _cl_mem *m)
{
    mem_dtor **pp = &g_dtors;
    while (*pp) {
        if ((*pp)->mem == m) {
            mem_dtor *d = *pp;
            *pp = d->next;
            if (d->fn)
                d->fn((cl_mem)m, d->user_data);
            free(d);
        } else {
            pp = &(*pp)->next;
        }
    }
}

#ifdef OPENCL_GPU_BACKEND
static int context_has_gpu(struct _cl_context *context)
{
    cl_uint i;

    for (i = 0; context && i < context->num_devices; i++)
        if (context->devices[i]->backend == &cl_backend_gpu)
            return 1;
    return 0;
}
#endif

static void mem_free(struct _cl_mem *m)
{
    fire_dtors(m);
    if (m->parent) {
        clReleaseMemObject((cl_mem)m->parent);
    } else if (m->owns_ptr && m->host_ptr) {
#ifdef OPENCL_GPU_BACKEND
        if (m->gfx_shm > 0) {
            g2d_shm_free((uint32_t *)m->host_ptr);
            shmctl(m->gfx_shm, IPC_RMID, NULL);
        } else
#endif
            free(m->host_ptr);
    }
    free(m);
}

cl_mem clCreateBuffer(cl_context context, cl_mem_flags flags, size_t size,
                      void *host_ptr, cl_int *errcode_ret)
{
    struct _cl_mem *m;
    cl_int err = CL_SUCCESS;

    if (!context) { err = CL_INVALID_CONTEXT; goto done; }
    if (size == 0) { err = CL_INVALID_BUFFER_SIZE; goto done; }
    if (flags & ~(CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY | CL_MEM_READ_ONLY |
                  CL_MEM_USE_HOST_PTR | CL_MEM_ALLOC_HOST_PTR | CL_MEM_COPY_HOST_PTR)) {
        err = CL_INVALID_VALUE; goto done;
    }
    if ((flags & CL_MEM_USE_HOST_PTR) && (flags & (CL_MEM_ALLOC_HOST_PTR | CL_MEM_COPY_HOST_PTR))) {
        err = CL_INVALID_VALUE; goto done;
    }
    if ((flags & (CL_MEM_USE_HOST_PTR | CL_MEM_ALLOC_HOST_PTR | CL_MEM_COPY_HOST_PTR)) && !host_ptr) {
        err = CL_INVALID_HOST_PTR; goto done;
    }
    if (!(flags & (CL_MEM_USE_HOST_PTR | CL_MEM_ALLOC_HOST_PTR | CL_MEM_COPY_HOST_PTR)) && host_ptr) {
        err = CL_INVALID_HOST_PTR; goto done;
    }

    m = (struct _cl_mem *)calloc(1, sizeof(*m));
    if (!m) { err = CL_OUT_OF_HOST_MEMORY; goto done; }
    m->refcount = 1;
    m->type = CL_MEM_OBJECT_BUFFER;
    m->flags = flags ? (flags & (CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY | CL_MEM_READ_ONLY)) : CL_MEM_READ_WRITE;
    if ((m->flags & (CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY | CL_MEM_READ_ONLY)) == 0)
        m->flags = CL_MEM_READ_WRITE;
    m->context = context;
    m->size = size;

    if (flags & CL_MEM_USE_HOST_PTR) {
        m->host_ptr = host_ptr;
        m->owns_ptr = 0;
    } else {
#ifdef OPENCL_GPU_BACKEND
        if (context_has_gpu(context) && size <= UINT32_MAX) {
            uint32_t *px = NULL;
            if (g2d_shm_alloc((uint32_t)size, &m->gfx_shm, &px) == 0 &&
                m->gfx_shm > 0 && px != NULL) {
                m->host_ptr = px;
                memset(m->host_ptr, 0, size);
            }
        }
#endif
        if (!m->host_ptr) {
            m->gfx_shm = 0;
            m->host_ptr = calloc(1, size);
        }
        if (!m->host_ptr) { free(m); err = CL_MEM_OBJECT_ALLOCATION_FAILURE; goto done; }
        m->owns_ptr = 1;
        if ((flags & CL_MEM_COPY_HOST_PTR) && host_ptr)
            memcpy(m->host_ptr, host_ptr, size);
    }
    clRetainContext(context);
    if (errcode_ret) *errcode_ret = CL_SUCCESS;
    return (cl_mem)m;

done:
    if (errcode_ret) *errcode_ret = err;
    return NULL;
}

cl_mem clCreateSubBuffer(cl_mem buffer, cl_mem_flags flags,
                         cl_buffer_create_type buffer_create_type,
                         const void *buffer_create_info, cl_int *errcode_ret)
{
    struct _cl_mem *parent = (struct _cl_mem *)buffer;
    const cl_buffer_region *region = (const cl_buffer_region *)buffer_create_info;
    struct _cl_mem *m;
    cl_int err = CL_SUCCESS;

    if (!parent || parent->type != CL_MEM_OBJECT_BUFFER || parent->parent) {
        err = CL_INVALID_MEM_OBJECT; goto done;
    }
    if (buffer_create_type != CL_BUFFER_CREATE_TYPE_REGION || !region) {
        err = CL_INVALID_VALUE; goto done;
    }
    if (region->size == 0 || region->origin + region->size > parent->size) {
        err = CL_INVALID_VALUE; goto done;
    }

    m = (struct _cl_mem *)calloc(1, sizeof(*m));
    if (!m) { err = CL_OUT_OF_HOST_MEMORY; goto done; }
    m->refcount = 1;
    m->type = CL_MEM_OBJECT_BUFFER;
    m->flags = flags ? flags : parent->flags;
    m->context = parent->context;
    m->size = region->size;
    m->parent = parent;
    m->origin = region->origin;
    clRetainMemObject((cl_mem)parent);
    if (errcode_ret) *errcode_ret = CL_SUCCESS;
    return (cl_mem)m;

done:
    if (errcode_ret) *errcode_ret = err;
    return NULL;
}

cl_int clRetainMemObject(cl_mem memobj)
{
    if (!memobj)
        return CL_INVALID_MEM_OBJECT;
    memobj->refcount++;
    return CL_SUCCESS;
}

cl_int clReleaseMemObject(cl_mem memobj)
{
    if (!memobj)
        return CL_INVALID_MEM_OBJECT;
    if (--memobj->refcount <= 0)
        mem_free(memobj);
    return CL_SUCCESS;
}

cl_int clSetMemObjectDestructorCallback(cl_mem memobj,
                                        void (CL_CALLBACK *pfn_notify)(cl_mem, void *),
                                        void *user_data)
{
    mem_dtor *d;
    if (!memobj)
        return CL_INVALID_MEM_OBJECT;
    if (!pfn_notify)
        return CL_INVALID_VALUE;
    d = (mem_dtor *)calloc(1, sizeof(*d));
    if (!d)
        return CL_OUT_OF_HOST_MEMORY;
    d->mem = memobj;
    d->fn = pfn_notify;
    d->user_data = user_data;
    d->next = g_dtors;
    g_dtors = d;
    return CL_SUCCESS;
}

#define INFO_M_SCALAR(ctype, expr) do { \
    ctype _v = (ctype)(expr); \
    return cl_get_info(&_v, sizeof(_v), param_value_size, param_value, param_value_size_ret); \
} while (0)

cl_int clGetMemObjectInfo(cl_mem memobj, cl_mem_info param_name,
                          size_t param_value_size, void *param_value,
                          size_t *param_value_size_ret)
{
    struct _cl_mem *m = (struct _cl_mem *)memobj;
    if (!m)
        return CL_INVALID_MEM_OBJECT;
    switch (param_name) {
    case CL_MEM_TYPE:               INFO_M_SCALAR(cl_mem_object_type, m->type);
    case CL_MEM_FLAGS:              INFO_M_SCALAR(cl_mem_flags, m->flags);
    case CL_MEM_SIZE:               INFO_M_SCALAR(size_t, m->size);
    case CL_MEM_MAP_COUNT:          INFO_M_SCALAR(cl_uint, m->map_count);
    case CL_MEM_REFERENCE_COUNT:    INFO_M_SCALAR(cl_uint, m->refcount);
    case CL_MEM_HOST_PTR: {
        void *p = cl_mem_device_ptr(m);
        return cl_get_info(&p, sizeof(p), param_value_size, param_value, param_value_size_ret);
    }
    case CL_MEM_CONTEXT: {
        cl_context c = (cl_context)m->context;
        return cl_get_info(&c, sizeof(c), param_value_size, param_value, param_value_size_ret);
    }
    case CL_MEM_ASSOCIATED_MEMOBJECT: {
        cl_mem p = (cl_mem)m->parent;
        return cl_get_info(&p, sizeof(p), param_value_size, param_value, param_value_size_ret);
    }
    case CL_MEM_OFFSET:             INFO_M_SCALAR(size_t, m->origin);
    default:
        return CL_INVALID_VALUE;
    }
}

cl_int clGetSupportedImageFormats(cl_context context, cl_mem_flags flags,
                                  cl_mem_object_type image_type, cl_uint num_entries,
                                  cl_image_format *image_formats, cl_uint *num_image_formats)
{
    static const cl_channel_order orders[] = { CL_R, CL_A, CL_RG, CL_RA, CL_RGBA, CL_BGRA, CL_INTENSITY, CL_LUMINANCE };
    static const cl_channel_type types[] = { CL_SNORM_INT8, CL_UNORM_INT8, CL_SIGNED_INT8, CL_UNSIGNED_INT8,
                                             CL_SIGNED_INT16, CL_UNSIGNED_INT16, CL_SIGNED_INT32, CL_UNSIGNED_INT32,
                                             CL_HALF_FLOAT, CL_FLOAT };
    cl_uint n = (cl_uint)(sizeof(orders) / sizeof(orders[0]) * sizeof(types) / sizeof(types[0]));
    cl_uint i = 0, oi, ti;
    (void)flags; (void)image_type;
    if (!context)
        return CL_INVALID_CONTEXT;
    for (oi = 0; oi < sizeof(orders) / sizeof(orders[0]); oi++) {
        for (ti = 0; ti < sizeof(types) / sizeof(types[0]); ti++) {
            if (image_formats && i < num_entries) {
                image_formats[i].image_channel_order = orders[oi];
                image_formats[i].image_channel_data_type = types[ti];
            }
            i++;
        }
    }
    if (num_image_formats)
        *num_image_formats = (num_entries && num_entries < n) ? num_entries : n;
    return CL_SUCCESS;
}

cl_int clEnqueueMigrateMemObjects(cl_command_queue command_queue, cl_uint num_mem_objects,
                                  const cl_mem *mem_objects, cl_mem_migration_flags flags,
                                  cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
                                  cl_event *event)
{
    (void)num_mem_objects; (void)mem_objects; (void)flags;
    if (!command_queue)
        return CL_INVALID_COMMAND_QUEUE;
    if (num_events_in_wait_list && !event_wait_list)
        return CL_INVALID_EVENT_WAIT_LIST;
    if (event) {
        struct _cl_event *ev = cl_event_create(command_queue, CL_COMMAND_MIGRATE_MEM_OBJECTS);
        if (!ev)
            return CL_OUT_OF_HOST_MEMORY;
        cl_event_set_status(ev, CL_COMPLETE);
        *event = (cl_event)ev;
    }
    return CL_SUCCESS; /* host-unified memory: nothing to migrate */
}
