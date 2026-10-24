/*
 * cl_image.c - 2D/3D image objects (CPU raster layout) and clGetImageInfo.
 * Images share struct _cl_mem with buffers; this file owns creation (the
 * deprecated clCreateImage2D/3D plus the 1.2 clCreateImage) and the pixel
 * geometry (element size, row/slice pitch) that the enqueue image paths and the
 * CPU image builtins rely on.
 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

static cl_uint channel_count(cl_channel_order order)
{
    switch (order) {
    case CL_R: case CL_A: case CL_Rx:
    case CL_INTENSITY: case CL_LUMINANCE:
        return 1;
    case CL_RG: case CL_RA: case CL_RGx:
        return 2;
    case CL_RGB: case CL_RGBx:
        return 3;
    case CL_RGBA: case CL_BGRA: case CL_ARGB:
        return 4;
    default:
        return 0;
    }
}

static cl_uint channel_type_size(cl_channel_type type)
{
    switch (type) {
    case CL_SNORM_INT8: case CL_UNORM_INT8:
    case CL_SIGNED_INT8: case CL_UNSIGNED_INT8:
        return 1;
    case CL_SNORM_INT16: case CL_UNORM_INT16:
    case CL_SIGNED_INT16: case CL_UNSIGNED_INT16:
    case CL_HALF_FLOAT: case CL_UNORM_INT24:
        return (type == CL_UNORM_INT24) ? 3 : 2;
    case CL_SIGNED_INT32: case CL_UNSIGNED_INT32: case CL_FLOAT:
        return 4;
    default:
        return 0;
    }
}

static int valid_image_format(const cl_image_format *f)
{
    return f && channel_count(f->image_channel_order) > 0 &&
           channel_type_size(f->image_channel_data_type) > 0;
}

static cl_mem image_create(cl_context context, cl_mem_flags flags,
                           const cl_image_format *fmt, cl_mem_object_type type,
                           size_t width, size_t height, size_t depth,
                           size_t row_pitch, size_t slice_pitch,
                           void *host_ptr, cl_int *errcode_ret)
{
    struct _cl_mem *m;
    cl_uint nch, tsz, elem;
    cl_int err = CL_SUCCESS;
    size_t rp, sp, size;

    if (!context) { err = CL_INVALID_CONTEXT; goto done; }
    if (!valid_image_format(fmt)) { err = CL_INVALID_IMAGE_FORMAT_DESCRIPTOR; goto done; }
    if (width == 0) { err = CL_INVALID_IMAGE_SIZE; goto done; }
    if (type != CL_MEM_OBJECT_IMAGE1D && height == 0) { err = CL_INVALID_IMAGE_SIZE; goto done; }
    if (type == CL_MEM_OBJECT_IMAGE3D && depth == 0) { err = CL_INVALID_IMAGE_SIZE; goto done; }

    nch = channel_count(fmt->image_channel_order);
    tsz = channel_type_size(fmt->image_channel_data_type);
    elem = nch * tsz;
    if (elem == 0) { err = CL_INVALID_IMAGE_FORMAT_DESCRIPTOR; goto done; }

    rp = row_pitch ? row_pitch : width * elem;
    if (row_pitch && rp < width * elem) { err = CL_INVALID_VALUE; goto done; }
    if (height <= 1)
        sp = slice_pitch ? slice_pitch : rp;
    else
        sp = slice_pitch ? slice_pitch : rp * height;

    if (height <= 1) size = rp;
    else if (depth <= 1) size = rp * height;
    else size = sp * depth;

    m = (struct _cl_mem *)calloc(1, sizeof(*m));
    if (!m) { err = CL_OUT_OF_HOST_MEMORY; goto done; }
    m->refcount = 1;
    m->type = type;
    m->flags = flags ? (flags & (CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY | CL_MEM_READ_ONLY)) : CL_MEM_READ_WRITE;
    if ((m->flags & (CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY | CL_MEM_READ_ONLY)) == 0)
        m->flags = CL_MEM_READ_WRITE;
    m->context = context;
    m->size = size;
    m->format = *fmt;
    m->width = width;
    m->height = height ? height : 1;
    m->depth = depth ? depth : 1;
    m->row_pitch = rp;
    m->slice_pitch = sp;
    m->elem_size = elem;

    if (flags & CL_MEM_USE_HOST_PTR) {
        m->host_ptr = host_ptr;
        m->owns_ptr = 0;
    } else {
        m->host_ptr = calloc(1, size);
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

cl_mem clCreateImage(cl_context context, cl_mem_flags flags,
                     const cl_image_format *image_format, const cl_image_desc *image_desc,
                     void *host_ptr, cl_int *errcode_ret)
{
    if (!image_desc) { if (errcode_ret) *errcode_ret = CL_INVALID_VALUE; return NULL; }
    switch (image_desc->image_type) {
    case CL_MEM_OBJECT_IMAGE1D:
    case CL_MEM_OBJECT_IMAGE1D_BUFFER:
        return image_create(context, flags, image_format, image_desc->image_type,
                            image_desc->image_width, 0, 0,
                            image_desc->image_row_pitch, 0, host_ptr, errcode_ret);
    case CL_MEM_OBJECT_IMAGE2D:
        return image_create(context, flags, image_format, CL_MEM_OBJECT_IMAGE2D,
                            image_desc->image_width, image_desc->image_height, 0,
                            image_desc->image_row_pitch, 0, host_ptr, errcode_ret);
    case CL_MEM_OBJECT_IMAGE3D:
        return image_create(context, flags, image_format, CL_MEM_OBJECT_IMAGE3D,
                            image_desc->image_width, image_desc->image_height,
                            image_desc->image_depth, image_desc->image_row_pitch,
                            image_desc->image_slice_pitch, host_ptr, errcode_ret);
    default:
        if (errcode_ret) *errcode_ret = CL_INVALID_VALUE;
        return NULL;
    }
}

cl_mem clCreateImage2D(cl_context context, cl_mem_flags flags,
                       const cl_image_format *image_format, size_t image_width,
                       size_t image_height, size_t image_row_pitch, void *host_ptr,
                       cl_int *errcode_ret)
{
    return image_create(context, flags, image_format, CL_MEM_OBJECT_IMAGE2D,
                        image_width, image_height, 0, image_row_pitch, 0,
                        host_ptr, errcode_ret);
}

cl_mem clCreateImage3D(cl_context context, cl_mem_flags flags,
                       const cl_image_format *image_format, size_t image_width,
                       size_t image_height, size_t image_depth, size_t image_row_pitch,
                       size_t image_slice_pitch, void *host_ptr, cl_int *errcode_ret)
{
    return image_create(context, flags, image_format, CL_MEM_OBJECT_IMAGE3D,
                        image_width, image_height, image_depth, image_row_pitch,
                        image_slice_pitch, host_ptr, errcode_ret);
}

#define INFO_I_SCALAR(ctype, expr) do { \
    ctype _v = (ctype)(expr); \
    return cl_get_info(&_v, sizeof(_v), param_value_size, param_value, param_value_size_ret); \
} while (0)

cl_int clGetImageInfo(cl_mem image, cl_image_info param_name,
                      size_t param_value_size, void *param_value,
                      size_t *param_value_size_ret)
{
    struct _cl_mem *m = (struct _cl_mem *)image;
    if (!m) return CL_INVALID_MEM_OBJECT;
    switch (param_name) {
    case CL_IMAGE_FORMAT:
        return cl_get_info(&m->format, sizeof(m->format), param_value_size, param_value, param_value_size_ret);
    case CL_IMAGE_ELEMENT_SIZE:
        INFO_I_SCALAR(size_t, m->elem_size);
    case CL_IMAGE_ROW_PITCH:
        INFO_I_SCALAR(size_t, m->row_pitch);
    case CL_IMAGE_SLICE_PITCH:
        INFO_I_SCALAR(size_t, (m->type == CL_MEM_OBJECT_IMAGE3D) ? m->slice_pitch : 0);
    case CL_IMAGE_WIDTH:
        INFO_I_SCALAR(size_t, m->width);
    case CL_IMAGE_HEIGHT:
        INFO_I_SCALAR(size_t, (m->type == CL_MEM_OBJECT_IMAGE1D) ? 0 : m->height);
    case CL_IMAGE_DEPTH:
        INFO_I_SCALAR(size_t, (m->type == CL_MEM_OBJECT_IMAGE3D) ? m->depth : 0);
    case CL_IMAGE_ARRAY_SIZE:
        INFO_I_SCALAR(size_t, 0);
    case CL_IMAGE_BUFFER: {
        cl_mem b = NULL;
        return cl_get_info(&b, sizeof(b), param_value_size, param_value, param_value_size_ret);
    }
    case CL_IMAGE_NUM_MIP_LEVELS:
        INFO_I_SCALAR(cl_uint, 0);
    case CL_IMAGE_NUM_SAMPLES:
        INFO_I_SCALAR(cl_uint, 0);
    default:
        return CL_INVALID_VALUE;
    }
}
