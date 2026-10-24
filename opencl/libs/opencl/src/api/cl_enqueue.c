/*
 * cl_enqueue.c - every clEnqueue* entry point.  Commands execute synchronously
 * on the host: buffer/image transfers memcpy against the cl_mem host store,
 * maps hand back a pointer into that store, and NDRange dispatch delegates to
 * the queue's device backend.  Each command optionally produces an event that
 * is driven QUEUED -> SUBMITTED -> RUNNING -> COMPLETE before the call returns,
 * so profiling timestamps and wait lists behave per the 1.2 spec.
 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* ---- begin/finish helpers ------------------------------------------------ */
static cl_int q_begin(struct _cl_command_queue *q, cl_uint n, const cl_event *wl)
{
    if (!q)
        return CL_INVALID_COMMAND_QUEUE;
    return cl_event_wait_list(n, wl);
}

static void q_finish(struct _cl_command_queue *q, cl_command_type type, cl_event *event)
{
    struct _cl_event *ev;
    if (!event)
        return;
    ev = cl_event_create(q, type);
    if (!ev) {
        *event = NULL;
        return;
    }
    cl_event_set_status(ev, CL_SUBMITTED);
    cl_event_set_status(ev, CL_RUNNING);
    cl_event_set_status(ev, CL_COMPLETE);
    *event = (cl_event)ev;
}

static int is_buffer(struct _cl_mem *m)
{
    return m && (m->type == CL_MEM_OBJECT_BUFFER);
}
static int is_image(struct _cl_mem *m)
{
    return m && (m->type == CL_MEM_OBJECT_IMAGE1D || m->type == CL_MEM_OBJECT_IMAGE1D_BUFFER ||
                 m->type == CL_MEM_OBJECT_IMAGE2D || m->type == CL_MEM_OBJECT_IMAGE3D);
}

/* ---- buffer transfers ---------------------------------------------------- */
cl_int clEnqueueReadBuffer(cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_read,
                           size_t offset, size_t size, void *ptr, cl_uint n, const cl_event *wl,
                           cl_event *event)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    struct _cl_mem *m = (struct _cl_mem *)buffer;
    cl_int r = q_begin(q, n, wl);
    (void)blocking_read;
    if (r != CL_SUCCESS) return r;
    if (!is_buffer(m)) return CL_INVALID_MEM_OBJECT;
    if (!ptr) return CL_INVALID_VALUE;
    if (size == 0 || offset + size > m->size) return CL_INVALID_VALUE;
    memcpy(ptr, (char *)cl_mem_device_ptr(m) + offset, size);
    q_finish(q, CL_COMMAND_READ_BUFFER, event);
    return CL_SUCCESS;
}

cl_int clEnqueueWriteBuffer(cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_write,
                            size_t offset, size_t size, const void *ptr, cl_uint n, const cl_event *wl,
                            cl_event *event)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    struct _cl_mem *m = (struct _cl_mem *)buffer;
    cl_int r = q_begin(q, n, wl);
    (void)blocking_write;
    if (r != CL_SUCCESS) return r;
    if (!is_buffer(m)) return CL_INVALID_MEM_OBJECT;
    if (!ptr) return CL_INVALID_VALUE;
    if (size == 0 || offset + size > m->size) return CL_INVALID_VALUE;
    memcpy((char *)cl_mem_device_ptr(m) + offset, ptr, size);
    q_finish(q, CL_COMMAND_WRITE_BUFFER, event);
    return CL_SUCCESS;
}

cl_int clEnqueueCopyBuffer(cl_command_queue command_queue, cl_mem src_buffer, cl_mem dst_buffer,
                           size_t src_offset, size_t dst_offset, size_t size, cl_uint n,
                           const cl_event *wl, cl_event *event)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    struct _cl_mem *s = (struct _cl_mem *)src_buffer;
    struct _cl_mem *d = (struct _cl_mem *)dst_buffer;
    cl_int r = q_begin(q, n, wl);
    if (r != CL_SUCCESS) return r;
    if (!is_buffer(s) || !is_buffer(d)) return CL_INVALID_MEM_OBJECT;
    if (size == 0 || src_offset + size > s->size || dst_offset + size > d->size)
        return CL_INVALID_VALUE;
    memmove((char *)cl_mem_device_ptr(d) + dst_offset,
            (char *)cl_mem_device_ptr(s) + src_offset, size);
    q_finish(q, CL_COMMAND_COPY_BUFFER, event);
    return CL_SUCCESS;
}

cl_int clEnqueueFillBuffer(cl_command_queue command_queue, cl_mem buffer, const void *pattern,
                           size_t pattern_size, size_t offset, size_t size, cl_uint n,
                           const cl_event *wl, cl_event *event)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    struct _cl_mem *m = (struct _cl_mem *)buffer;
    cl_int r = q_begin(q, n, wl);
    unsigned char *dst;
    size_t i;
    if (r != CL_SUCCESS) return r;
    if (!is_buffer(m)) return CL_INVALID_MEM_OBJECT;
    if (!pattern) return CL_INVALID_VALUE;
    if (pattern_size != 1 && pattern_size != 2 && pattern_size != 4 && pattern_size != 8 &&
        pattern_size != 16 && pattern_size != 32 && pattern_size != 64 && pattern_size != 128)
        return CL_INVALID_VALUE;
    if (size == 0 || (size % pattern_size) != 0 || (offset % pattern_size) != 0 ||
        offset + size > m->size)
        return CL_INVALID_VALUE;
    dst = (unsigned char *)cl_mem_device_ptr(m) + offset;
    for (i = 0; i + pattern_size <= size; i += pattern_size)
        memcpy(dst + i, pattern, pattern_size);
    q_finish(q, CL_COMMAND_FILL_BUFFER, event);
    return CL_SUCCESS;
}

/* ---- rect transfers ------------------------------------------------------ */
static void rect_copy(void *dst, size_t dst_row, size_t dst_slice,
                      const void *src, size_t src_row, size_t src_slice,
                      const size_t *region, size_t elem)
{
    size_t z, y;
    size_t w = region[0], h = region[1], d = region[2];
    for (z = 0; z < d; z++)
        for (y = 0; y < h; y++)
            memcpy((char *)dst + z * dst_slice + y * dst_row,
                   (const char *)src + z * src_slice + y * src_row,
                   w * elem);
}

cl_int clEnqueueReadBufferRect(cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_read,
                               const size_t *buffer_offset, const size_t *host_offset, const size_t *region,
                               size_t buffer_row_pitch, size_t buffer_slice_pitch, size_t host_row_pitch,
                               size_t host_slice_pitch, void *ptr, cl_uint n, const cl_event *wl, cl_event *event)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    struct _cl_mem *m = (struct _cl_mem *)buffer;
    cl_int r = q_begin(q, n, wl);
    size_t bo[3], ho[3], rg[3], brp, bsp, hrp, hsp;
    unsigned char *base, *dst;
    (void)blocking_read;
    if (r != CL_SUCCESS) return r;
    if (!is_buffer(m)) return CL_INVALID_MEM_OBJECT;
    if (!ptr || !buffer_offset || !host_offset || !region) return CL_INVALID_VALUE;
    memcpy(bo, buffer_offset, sizeof(bo));
    memcpy(ho, host_offset, sizeof(ho));
    memcpy(rg, region, sizeof(rg));
    if (rg[0] == 0 || rg[1] == 0 || rg[2] == 0) return CL_INVALID_VALUE;
    brp = buffer_row_pitch ? buffer_row_pitch : rg[0];
    bsp = buffer_slice_pitch ? buffer_slice_pitch : rg[1] * brp;
    hrp = host_row_pitch ? host_row_pitch : rg[0];
    hsp = host_slice_pitch ? host_slice_pitch : rg[1] * hrp;
    base = (unsigned char *)cl_mem_device_ptr(m) + bo[2] * bsp + bo[1] * brp + bo[0];
    dst = (unsigned char *)ptr + ho[2] * hsp + ho[1] * hrp + ho[0];
    rect_copy(dst, hrp, hsp, base, brp, bsp, rg, 1);
    q_finish(q, CL_COMMAND_READ_BUFFER_RECT, event);
    return CL_SUCCESS;
}

cl_int clEnqueueWriteBufferRect(cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_write,
                                const size_t *buffer_offset, const size_t *host_offset, const size_t *region,
                                size_t buffer_row_pitch, size_t buffer_slice_pitch, size_t host_row_pitch,
                                size_t host_slice_pitch, const void *ptr, cl_uint n, const cl_event *wl, cl_event *event)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    struct _cl_mem *m = (struct _cl_mem *)buffer;
    cl_int r = q_begin(q, n, wl);
    size_t bo[3], ho[3], rg[3], brp, bsp, hrp, hsp;
    unsigned char *base;
    const unsigned char *src;
    (void)blocking_write;
    if (r != CL_SUCCESS) return r;
    if (!is_buffer(m)) return CL_INVALID_MEM_OBJECT;
    if (!ptr || !buffer_offset || !host_offset || !region) return CL_INVALID_VALUE;
    memcpy(bo, buffer_offset, sizeof(bo));
    memcpy(ho, host_offset, sizeof(ho));
    memcpy(rg, region, sizeof(rg));
    if (rg[0] == 0 || rg[1] == 0 || rg[2] == 0) return CL_INVALID_VALUE;
    brp = buffer_row_pitch ? buffer_row_pitch : rg[0];
    bsp = buffer_slice_pitch ? buffer_slice_pitch : rg[1] * brp;
    hrp = host_row_pitch ? host_row_pitch : rg[0];
    hsp = host_slice_pitch ? host_slice_pitch : rg[1] * hrp;
    base = (unsigned char *)cl_mem_device_ptr(m) + bo[2] * bsp + bo[1] * brp + bo[0];
    src = (const unsigned char *)ptr + ho[2] * hsp + ho[1] * hrp + ho[0];
    rect_copy(base, brp, bsp, src, hrp, hsp, rg, 1);
    q_finish(q, CL_COMMAND_WRITE_BUFFER_RECT, event);
    return CL_SUCCESS;
}

cl_int clEnqueueCopyBufferRect(cl_command_queue command_queue, cl_mem src_buffer, cl_mem dst_buffer,
                               const size_t *src_origin, const size_t *dst_origin, const size_t *region,
                               size_t src_row_pitch, size_t src_slice_pitch, size_t dst_row_pitch,
                               size_t dst_slice_pitch, cl_uint n, const cl_event *wl, cl_event *event)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    struct _cl_mem *s = (struct _cl_mem *)src_buffer;
    struct _cl_mem *d = (struct _cl_mem *)dst_buffer;
    cl_int r = q_begin(q, n, wl);
    size_t so[3], do_[3], rg[3], srp, ssp, drp, dsp;
    unsigned char *sb, *db;
    if (r != CL_SUCCESS) return r;
    if (!is_buffer(s) || !is_buffer(d)) return CL_INVALID_MEM_OBJECT;
    if (!src_origin || !dst_origin || !region) return CL_INVALID_VALUE;
    memcpy(so, src_origin, sizeof(so));
    memcpy(do_, dst_origin, sizeof(do_));
    memcpy(rg, region, sizeof(rg));
    if (rg[0] == 0 || rg[1] == 0 || rg[2] == 0) return CL_INVALID_VALUE;
    srp = src_row_pitch ? src_row_pitch : rg[0];
    ssp = src_slice_pitch ? src_slice_pitch : rg[1] * srp;
    drp = dst_row_pitch ? dst_row_pitch : rg[0];
    dsp = dst_slice_pitch ? dst_slice_pitch : rg[1] * drp;
    sb = (unsigned char *)cl_mem_device_ptr(s) + so[2] * ssp + so[1] * srp + so[0];
    db = (unsigned char *)cl_mem_device_ptr(d) + do_[2] * dsp + do_[1] * drp + do_[0];
    rect_copy(db, drp, dsp, sb, srp, ssp, rg, 1);
    q_finish(q, CL_COMMAND_COPY_BUFFER_RECT, event);
    return CL_SUCCESS;
}

/* ---- image transfers ----------------------------------------------------- */
static void image_copy_region(void *dst, size_t dst_row, size_t dst_slice,
                              const void *src, size_t src_row, size_t src_slice,
                              const size_t *origin, const size_t *region, size_t elem,
                              int to_host)
{
    size_t z, y;
    size_t w = region[0], h = region[1], d = region[2];
    size_t ox = origin[0], oy = origin[1], oz = origin[2];
    for (z = 0; z < d; z++) {
        for (y = 0; y < h; y++) {
            unsigned char *dp;
            const unsigned char *sp;
            if (to_host) {
                dp = (unsigned char *)dst + z * dst_slice + y * dst_row;
                sp = (const unsigned char *)src + (oz + z) * src_slice + (oy + y) * src_row + ox * elem;
            } else {
                dp = (unsigned char *)dst + (oz + z) * dst_slice + (oy + y) * dst_row + ox * elem;
                sp = (const unsigned char *)src + z * src_slice + y * src_row;
            }
            memcpy(dp, sp, w * elem);
        }
    }
}

cl_int clEnqueueReadImage(cl_command_queue command_queue, cl_mem image, cl_bool blocking_read,
                          const size_t *origin, const size_t *region, size_t row_pitch,
                          size_t slice_pitch, void *ptr, cl_uint n, const cl_event *wl, cl_event *event)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    struct _cl_mem *m = (struct _cl_mem *)image;
    cl_int r = q_begin(q, n, wl);
    size_t og[3], rg[3], hrp, hsp;
    (void)blocking_read;
    if (r != CL_SUCCESS) return r;
    if (!is_image(m)) return CL_INVALID_MEM_OBJECT;
    if (!ptr || !origin || !region) return CL_INVALID_VALUE;
    memcpy(og, origin, sizeof(og));
    memcpy(rg, region, sizeof(rg));
    if (rg[0] == 0) return CL_INVALID_VALUE;
    if (rg[1] == 0) rg[1] = 1;
    if (rg[2] == 0) rg[2] = 1;
    hrp = row_pitch ? row_pitch : rg[0] * m->elem_size;
    hsp = slice_pitch ? slice_pitch : rg[1] * hrp;
    image_copy_region(ptr, hrp, hsp, cl_mem_device_ptr(m), m->row_pitch, m->slice_pitch,
                      og, rg, m->elem_size, 1);
    q_finish(q, CL_COMMAND_READ_IMAGE, event);
    return CL_SUCCESS;
}

cl_int clEnqueueWriteImage(cl_command_queue command_queue, cl_mem image, cl_bool blocking_write,
                           const size_t *origin, const size_t *region, size_t input_row_pitch,
                           size_t input_slice_pitch, const void *ptr, cl_uint n, const cl_event *wl, cl_event *event)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    struct _cl_mem *m = (struct _cl_mem *)image;
    cl_int r = q_begin(q, n, wl);
    size_t og[3], rg[3], hrp, hsp;
    (void)blocking_write;
    if (r != CL_SUCCESS) return r;
    if (!is_image(m)) return CL_INVALID_MEM_OBJECT;
    if (!ptr || !origin || !region) return CL_INVALID_VALUE;
    memcpy(og, origin, sizeof(og));
    memcpy(rg, region, sizeof(rg));
    if (rg[0] == 0) return CL_INVALID_VALUE;
    if (rg[1] == 0) rg[1] = 1;
    if (rg[2] == 0) rg[2] = 1;
    hrp = input_row_pitch ? input_row_pitch : rg[0] * m->elem_size;
    hsp = input_slice_pitch ? input_slice_pitch : rg[1] * hrp;
    image_copy_region(cl_mem_device_ptr(m), m->row_pitch, m->slice_pitch, ptr, hrp, hsp,
                      og, rg, m->elem_size, 0);
    q_finish(q, CL_COMMAND_WRITE_IMAGE, event);
    return CL_SUCCESS;
}

cl_int clEnqueueCopyImage(cl_command_queue command_queue, cl_mem src_image, cl_mem dst_image,
                          const size_t *src_origin, const size_t *dst_origin, const size_t *region,
                          cl_uint n, const cl_event *wl, cl_event *event)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    struct _cl_mem *s = (struct _cl_mem *)src_image;
    struct _cl_mem *d = (struct _cl_mem *)dst_image;
    cl_int r = q_begin(q, n, wl);
    size_t so[3], do_[3], rg[3];
    size_t z, y;
    if (r != CL_SUCCESS) return r;
    if (!is_image(s) || !is_image(d)) return CL_INVALID_MEM_OBJECT;
    if (!src_origin || !dst_origin || !region) return CL_INVALID_VALUE;
    memcpy(so, src_origin, sizeof(so));
    memcpy(do_, dst_origin, sizeof(do_));
    memcpy(rg, region, sizeof(rg));
    if (rg[1] == 0) rg[1] = 1;
    if (rg[2] == 0) rg[2] = 1;
    for (z = 0; z < rg[2]; z++)
        for (y = 0; y < rg[1]; y++) {
            unsigned char *dp = (unsigned char *)cl_mem_device_ptr(d) +
                (do_[2] + z) * d->slice_pitch + (do_[1] + y) * d->row_pitch + do_[0] * d->elem_size;
            const unsigned char *sp = (const unsigned char *)cl_mem_device_ptr(s) +
                (so[2] + z) * s->slice_pitch + (so[1] + y) * s->row_pitch + so[0] * s->elem_size;
            memcpy(dp, sp, rg[0] * s->elem_size);
        }
    q_finish(q, CL_COMMAND_COPY_IMAGE, event);
    return CL_SUCCESS;
}

cl_int clEnqueueFillImage(cl_command_queue command_queue, cl_mem image, const void *fill_color,
                          const size_t *origin, const size_t *region, cl_uint n, const cl_event *wl, cl_event *event)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    struct _cl_mem *m = (struct _cl_mem *)image;
    cl_int r = q_begin(q, n, wl);
    size_t og[3], rg[3], z, y;
    if (r != CL_SUCCESS) return r;
    if (!is_image(m)) return CL_INVALID_MEM_OBJECT;
    if (!fill_color || !origin || !region) return CL_INVALID_VALUE;
    memcpy(og, origin, sizeof(og));
    memcpy(rg, region, sizeof(rg));
    if (rg[1] == 0) rg[1] = 1;
    if (rg[2] == 0) rg[2] = 1;
    for (z = 0; z < rg[2]; z++)
        for (y = 0; y < rg[1]; y++) {
            unsigned char *row = (unsigned char *)cl_mem_device_ptr(m) +
                (og[2] + z) * m->slice_pitch + (og[1] + y) * m->row_pitch + og[0] * m->elem_size;
            size_t x;
            for (x = 0; x < rg[0]; x++)
                memcpy(row + x * m->elem_size, fill_color, m->elem_size);
        }
    q_finish(q, CL_COMMAND_FILL_IMAGE, event);
    return CL_SUCCESS;
}

cl_int clEnqueueCopyImageToBuffer(cl_command_queue command_queue, cl_mem src_image, cl_mem dst_buffer,
                                  const size_t *src_origin, const size_t *region, size_t dst_offset,
                                  cl_uint n, const cl_event *wl, cl_event *event)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    struct _cl_mem *s = (struct _cl_mem *)src_image;
    struct _cl_mem *d = (struct _cl_mem *)dst_buffer;
    cl_int r = q_begin(q, n, wl);
    size_t so[3], rg[3], rowbytes, z, y;
    if (r != CL_SUCCESS) return r;
    if (!is_image(s) || !is_buffer(d)) return CL_INVALID_MEM_OBJECT;
    if (!src_origin || !region) return CL_INVALID_VALUE;
    memcpy(so, src_origin, sizeof(so));
    memcpy(rg, region, sizeof(rg));
    if (rg[1] == 0) rg[1] = 1;
    if (rg[2] == 0) rg[2] = 1;
    rowbytes = rg[0] * s->elem_size;
    for (z = 0; z < rg[2]; z++)
        for (y = 0; y < rg[1]; y++) {
            const unsigned char *sp = (const unsigned char *)cl_mem_device_ptr(s) +
                (so[2] + z) * s->slice_pitch + (so[1] + y) * s->row_pitch + so[0] * s->elem_size;
            unsigned char *dp = (unsigned char *)cl_mem_device_ptr(d) + dst_offset +
                (z * rg[1] + y) * rowbytes;
            memcpy(dp, sp, rowbytes);
        }
    q_finish(q, CL_COMMAND_COPY_IMAGE_TO_BUFFER, event);
    return CL_SUCCESS;
}

cl_int clEnqueueCopyBufferToImage(cl_command_queue command_queue, cl_mem src_buffer, cl_mem dst_image,
                                  size_t src_offset, const size_t *dst_origin, const size_t *region,
                                  cl_uint n, const cl_event *wl, cl_event *event)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    struct _cl_mem *s = (struct _cl_mem *)src_buffer;
    struct _cl_mem *d = (struct _cl_mem *)dst_image;
    cl_int r = q_begin(q, n, wl);
    size_t do_[3], rg[3], rowbytes, z, y;
    if (r != CL_SUCCESS) return r;
    if (!is_buffer(s) || !is_image(d)) return CL_INVALID_MEM_OBJECT;
    if (!dst_origin || !region) return CL_INVALID_VALUE;
    memcpy(do_, dst_origin, sizeof(do_));
    memcpy(rg, region, sizeof(rg));
    if (rg[1] == 0) rg[1] = 1;
    if (rg[2] == 0) rg[2] = 1;
    rowbytes = rg[0] * d->elem_size;
    for (z = 0; z < rg[2]; z++)
        for (y = 0; y < rg[1]; y++) {
            const unsigned char *sp = (const unsigned char *)cl_mem_device_ptr(s) + src_offset +
                (z * rg[1] + y) * rowbytes;
            unsigned char *dp = (unsigned char *)cl_mem_device_ptr(d) +
                (do_[2] + z) * d->slice_pitch + (do_[1] + y) * d->row_pitch + do_[0] * d->elem_size;
            memcpy(dp, sp, rowbytes);
        }
    q_finish(q, CL_COMMAND_COPY_BUFFER_TO_IMAGE, event);
    return CL_SUCCESS;
}

/* ---- map / unmap --------------------------------------------------------- */
void *clEnqueueMapBuffer(cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_map,
                         cl_map_flags map_flags, size_t offset, size_t size, cl_uint n,
                         const cl_event *wl, cl_event *event, cl_int *errcode_ret)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    struct _cl_mem *m = (struct _cl_mem *)buffer;
    cl_int r = q_begin(q, n, wl);
    void *p;
    (void)blocking_map; (void)map_flags;
    if (r != CL_SUCCESS) { if (errcode_ret) *errcode_ret = r; return NULL; }
    if (!is_buffer(m)) { if (errcode_ret) *errcode_ret = CL_INVALID_MEM_OBJECT; return NULL; }
    if (size == 0 || offset + size > m->size) { if (errcode_ret) *errcode_ret = CL_INVALID_VALUE; return NULL; }
    p = (char *)cl_mem_device_ptr(m) + offset;
    m->map_count++;
    q_finish(q, CL_COMMAND_MAP_BUFFER, event);
    if (errcode_ret) *errcode_ret = CL_SUCCESS;
    return p;
}

void *clEnqueueMapImage(cl_command_queue command_queue, cl_mem image, cl_bool blocking_map,
                        cl_map_flags map_flags, const size_t *origin, const size_t *region,
                        size_t *image_row_pitch, size_t *image_slice_pitch, cl_uint n,
                        const cl_event *wl, cl_event *event, cl_int *errcode_ret)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    struct _cl_mem *m = (struct _cl_mem *)image;
    cl_int r = q_begin(q, n, wl);
    size_t og[3];
    void *p;
    (void)blocking_map; (void)map_flags; (void)region;
    if (r != CL_SUCCESS) { if (errcode_ret) *errcode_ret = r; return NULL; }
    if (!is_image(m)) { if (errcode_ret) *errcode_ret = CL_INVALID_MEM_OBJECT; return NULL; }
    if (!origin) { if (errcode_ret) *errcode_ret = CL_INVALID_VALUE; return NULL; }
    memcpy(og, origin, sizeof(og));
    p = (char *)cl_mem_device_ptr(m) + og[2] * m->slice_pitch + og[1] * m->row_pitch + og[0] * m->elem_size;
    if (image_row_pitch) *image_row_pitch = m->row_pitch;
    if (image_slice_pitch) *image_slice_pitch = m->slice_pitch;
    m->map_count++;
    q_finish(q, CL_COMMAND_MAP_IMAGE, event);
    if (errcode_ret) *errcode_ret = CL_SUCCESS;
    return p;
}

cl_int clEnqueueUnmapMemObject(cl_command_queue command_queue, cl_mem memobj, void *mapped_ptr,
                               cl_uint n, const cl_event *wl, cl_event *event)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    struct _cl_mem *m = (struct _cl_mem *)memobj;
    cl_int r = q_begin(q, n, wl);
    (void)mapped_ptr;
    if (r != CL_SUCCESS) return r;
    if (!m) return CL_INVALID_MEM_OBJECT;
    if (m->map_count > 0) m->map_count--;
    q_finish(q, CL_COMMAND_UNMAP_MEM_OBJECT, event);
    return CL_SUCCESS;
}

/* ---- kernel dispatch ----------------------------------------------------- */
cl_int clEnqueueNDRangeKernel(cl_command_queue command_queue, cl_kernel kernel, cl_uint work_dim,
                              const size_t *global_work_offset, const size_t *global_work_size,
                              const size_t *local_work_size, cl_uint n, const cl_event *wl, cl_event *event)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    struct _cl_kernel *k = (struct _cl_kernel *)kernel;
    cl_int r = q_begin(q, n, wl);
    struct _cl_device_id *dev;
    cl_uint d;
    size_t total_local = 1;

    if (r != CL_SUCCESS) return r;
    if (!k) return CL_INVALID_KERNEL;
    if (!k->build || !k->build->exec || k->build->status != CL_BUILD_SUCCESS)
        return CL_INVALID_PROGRAM_EXECUTABLE;
    if (work_dim < 1 || work_dim > 3) return CL_INVALID_WORK_DIMENSION;
    if (!global_work_size) return CL_INVALID_VALUE;

    dev = q->device;
    if (k->build->device != dev) return CL_INVALID_PROGRAM_EXECUTABLE;

    for (d = 0; d < work_dim; d++) {
        if (global_work_size[d] == 0) return CL_INVALID_GLOBAL_WORK_SIZE;
        if (global_work_size[d] > dev->max_work_item_sizes[d] * 65536)
            return CL_INVALID_GLOBAL_WORK_SIZE;
    }
    if (local_work_size) {
        for (d = 0; d < work_dim; d++) {
            if (local_work_size[d] == 0) return CL_INVALID_WORK_GROUP_SIZE;
            if (global_work_size[d] % local_work_size[d] != 0) return CL_INVALID_WORK_GROUP_SIZE;
            if (local_work_size[d] > dev->max_work_item_sizes[d]) return CL_INVALID_WORK_ITEM_SIZE;
            total_local *= local_work_size[d];
        }
        if (total_local > dev->max_work_group_size) return CL_INVALID_WORK_GROUP_SIZE;
    }

    r = dev->backend->ndrange(q, k, work_dim, global_work_offset, global_work_size, local_work_size);
    if (r != CL_SUCCESS) return r;
    q_finish(q, CL_COMMAND_NDRANGE_KERNEL, event);
    return CL_SUCCESS;
}

cl_int clEnqueueTask(cl_command_queue command_queue, cl_kernel kernel, cl_uint n,
                     const cl_event *wl, cl_event *event)
{
    size_t one[3] = { 1, 1, 1 };
    size_t zero[3] = { 0, 0, 0 };
    return clEnqueueNDRangeKernel(command_queue, kernel, 1, zero, one, one, n, wl, event);
}

cl_int clEnqueueNativeKernel(cl_command_queue command_queue, void (CL_CALLBACK *user_func)(void *),
                             void *args, size_t cb_args, cl_uint num_mem_objects, const cl_mem *mem_list,
                             const void **args_mem_loc, cl_uint n, const cl_event *wl, cl_event *event)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    cl_int r = q_begin(q, n, wl);
    unsigned char *copy;
    cl_uint i;
    if (r != CL_SUCCESS) return r;
    if (!user_func) return CL_INVALID_VALUE;
    if ((args == NULL) != (cb_args == 0)) return CL_INVALID_VALUE;
    if (cb_args == 0) {
        user_func(NULL);
        q_finish(q, CL_COMMAND_NATIVE_KERNEL, event);
        return CL_SUCCESS;
    }
    copy = (unsigned char *)malloc(cb_args);
    if (!copy) return CL_OUT_OF_HOST_MEMORY;
    memcpy(copy, args, cb_args);
    for (i = 0; i < num_mem_objects; i++) {
        void *loc = (void *)args_mem_loc[i];
        /* args_mem_loc entries are pointers into the caller's arg buffer; we
         * only accept ones that fall inside the copied range. */
        if (loc >= (void *)args && loc < (void *)((char *)args + cb_args)) {
            void **slot = (void **)(copy + ((char *)loc - (char *)args));
            *slot = cl_mem_device_ptr((struct _cl_mem *)mem_list[i]);
        }
    }
    user_func(copy);
    free(copy);
    q_finish(q, CL_COMMAND_NATIVE_KERNEL, event);
    return CL_SUCCESS;
}

/* ---- markers / barriers -------------------------------------------------- */
cl_int clEnqueueMarkerWithWaitList(cl_command_queue command_queue, cl_uint n, const cl_event *wl,
                                   cl_event *event)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    cl_int r = q_begin(q, n, wl);
    if (r != CL_SUCCESS) return r;
    q_finish(q, CL_COMMAND_MARKER, event);
    return CL_SUCCESS;
}

cl_int clEnqueueBarrierWithWaitList(cl_command_queue command_queue, cl_uint n, const cl_event *wl,
                                    cl_event *event)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    cl_int r = q_begin(q, n, wl);
    if (r != CL_SUCCESS) return r;
    q_finish(q, CL_COMMAND_BARRIER, event);
    return CL_SUCCESS;
}

cl_int clEnqueueMarker(cl_command_queue command_queue, cl_event *event)
{
    return clEnqueueMarkerWithWaitList(command_queue, 0, NULL, event);
}

cl_int clEnqueueWaitForEvents(cl_command_queue command_queue, cl_uint num_events, const cl_event *event_list)
{
    (void)command_queue;
    return clWaitForEvents(num_events, event_list);
}

cl_int clEnqueueBarrier(cl_command_queue command_queue)
{
    return clEnqueueBarrierWithWaitList(command_queue, 0, NULL, NULL);
}
