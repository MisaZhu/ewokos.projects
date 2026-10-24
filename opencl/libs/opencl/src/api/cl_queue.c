/*
 * cl_queue.c - command queue create/retain/release/info.  Queues execute
 * commands synchronously in enqueue order, so an out-of-order queue behaves
 * like an in-order one (still reported via CL_QUEUE_PROPERTIES).
 */
#include "internal.h"
#include <stdlib.h>

#define INFO_Q_SCALAR(ctype, expr) do { \
    ctype _v = (ctype)(expr); \
    return cl_get_info(&_v, sizeof(_v), param_value_size, param_value, param_value_size_ret); \
} while (0)

cl_command_queue clCreateCommandQueue(cl_context context, cl_device_id device,
                                      cl_command_queue_properties properties,
                                      cl_int *errcode_ret)
{
    struct _cl_command_queue *q;
    if (!context) {
        if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT;
        return NULL;
    }
    if (!cl_context_has_device(context, (struct _cl_device_id *)device)) {
        if (errcode_ret) *errcode_ret = CL_INVALID_DEVICE;
        return NULL;
    }
    if (properties & ~(CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_PROFILING_ENABLE)) {
        if (errcode_ret) *errcode_ret = CL_INVALID_VALUE;
        return NULL;
    }
    q = (struct _cl_command_queue *)calloc(1, sizeof(*q));
    if (!q) {
        if (errcode_ret) *errcode_ret = CL_OUT_OF_HOST_MEMORY;
        return NULL;
    }
    q->refcount = 1;
    q->context = context;
    q->device = (struct _cl_device_id *)device;
    q->properties = properties;
    clRetainContext(context);
    if (errcode_ret) *errcode_ret = CL_SUCCESS;
    return (cl_command_queue)q;
}

cl_int clRetainCommandQueue(cl_command_queue command_queue)
{
    if (!command_queue)
        return CL_INVALID_COMMAND_QUEUE;
    command_queue->refcount++;
    return CL_SUCCESS;
}

cl_int clReleaseCommandQueue(cl_command_queue command_queue)
{
    if (!command_queue)
        return CL_INVALID_COMMAND_QUEUE;
    if (--command_queue->refcount <= 0) {
        clReleaseContext(command_queue->context);
        free(command_queue);
    }
    return CL_SUCCESS;
}

cl_int clGetCommandQueueInfo(cl_command_queue command_queue,
                             cl_command_queue_info param_name,
                             size_t param_value_size, void *param_value,
                             size_t *param_value_size_ret)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    if (!q)
        return CL_INVALID_COMMAND_QUEUE;
    switch (param_name) {
    case CL_QUEUE_REFERENCE_COUNT:
        INFO_Q_SCALAR(cl_uint, q->refcount);
    case CL_QUEUE_PROPERTIES:
        INFO_Q_SCALAR(cl_command_queue_properties, q->properties);
    case CL_QUEUE_CONTEXT: {
        cl_context c = (cl_context)q->context;
        return cl_get_info(&c, sizeof(c), param_value_size, param_value, param_value_size_ret);
    }
    case CL_QUEUE_DEVICE: {
        cl_device_id d = (cl_device_id)q->device;
        return cl_get_info(&d, sizeof(d), param_value_size, param_value, param_value_size_ret);
    }
    default:
        return CL_INVALID_VALUE;
    }
}

cl_int clSetCommandQueueProperty(cl_command_queue command_queue,
                                 cl_command_queue_properties properties,
                                 cl_bool enable,
                                 cl_command_queue_properties *old_properties)
{
    struct _cl_command_queue *q = (struct _cl_command_queue *)command_queue;
    if (!q)
        return CL_INVALID_COMMAND_QUEUE;
    if (old_properties)
        *old_properties = q->properties;
    if (enable)
        q->properties |= properties;
    else
        q->properties &= ~properties;
    return CL_SUCCESS;
}

cl_int clFlush(cl_command_queue command_queue)
{
    if (!command_queue)
        return CL_INVALID_COMMAND_QUEUE;
    return CL_SUCCESS; /* commands are synchronous: nothing is pending */
}

cl_int clFinish(cl_command_queue command_queue)
{
    if (!command_queue)
        return CL_INVALID_COMMAND_QUEUE;
    return CL_SUCCESS;
}
