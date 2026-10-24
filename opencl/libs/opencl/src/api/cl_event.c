/*
 * cl_event.c - the public event API: wait, info, user events, refcounts,
 * callbacks and profiling timestamps.  Event lifecycle primitives
 * (cl_event_create / _set_status / _wait_list / _stamp) live in runtime/event.c.
 * Commands complete synchronously, so a normal event is already CL_COMPLETE by
 * the time its enqueue returns; user events are the only long-lived ones.
 */
#include "internal.h"
#include <stdlib.h>

#define INFO_E_SCALAR(ctype, expr) do { \
    ctype _v = (ctype)(expr); \
    return cl_get_info(&_v, sizeof(_v), param_value_size, param_value, param_value_size_ret); \
} while (0)

cl_int clWaitForEvents(cl_uint num_events, const cl_event *event_list)
{
    cl_uint i;
    if (num_events == 0 || !event_list)
        return CL_INVALID_VALUE;
    for (i = 0; i < num_events; i++) {
        if (!event_list[i])
            return CL_INVALID_EVENT;
        /* synchronous execution: events are already terminal.  A pending user
         * event can never complete from another thread in this runtime, so we
         * treat a still-queued user event as an error rather than spinning. */
        if (event_list[i]->status > CL_COMPLETE)
            return CL_INVALID_VALUE;
        if (event_list[i]->status < 0)
            return CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST;
    }
    return CL_SUCCESS;
}

cl_int clGetEventInfo(cl_event event, cl_event_info param_name,
                      size_t param_value_size, void *param_value,
                      size_t *param_value_size_ret)
{
    struct _cl_event *e = (struct _cl_event *)event;
    if (!e) return CL_INVALID_EVENT;
    switch (param_name) {
    case CL_EVENT_COMMAND_QUEUE: {
        cl_command_queue q = (cl_command_queue)e->queue;
        return cl_get_info(&q, sizeof(q), param_value_size, param_value, param_value_size_ret);
    }
    case CL_EVENT_COMMAND_TYPE:
        INFO_E_SCALAR(cl_command_type, e->type);
    case CL_EVENT_COMMAND_EXECUTION_STATUS:
        INFO_E_SCALAR(cl_int, e->status);
    case CL_EVENT_REFERENCE_COUNT:
        INFO_E_SCALAR(cl_uint, e->refcount);
    case CL_EVENT_CONTEXT: {
        cl_context c = (cl_context)e->context;
        return cl_get_info(&c, sizeof(c), param_value_size, param_value, param_value_size_ret);
    }
    default:
        return CL_INVALID_VALUE;
    }
}

cl_event clCreateUserEvent(cl_context context, cl_int *errcode_ret)
{
    struct _cl_event *e;
    if (!context) { if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT; return NULL; }
    e = (struct _cl_event *)calloc(1, sizeof(*e));
    if (!e) { if (errcode_ret) *errcode_ret = CL_OUT_OF_HOST_MEMORY; return NULL; }
    e->refcount = 1;
    e->context = context;
    e->queue = NULL;
    e->type = CL_COMMAND_USER;
    e->status = CL_SUBMITTED;   /* user events start not-complete */
    clRetainContext(context);
    if (errcode_ret) *errcode_ret = CL_SUCCESS;
    return (cl_event)e;
}

cl_int clRetainEvent(cl_event event)
{
    if (!event) return CL_INVALID_EVENT;
    event->refcount++;
    return CL_SUCCESS;
}

cl_int clReleaseEvent(cl_event event)
{
    if (!event) return CL_INVALID_EVENT;
    if (--event->refcount <= 0) {
        if (event->context)
            clReleaseContext(event->context);
        free(event);
    }
    return CL_SUCCESS;
}

cl_int clSetUserEventStatus(cl_event event, cl_int execution_status)
{
    struct _cl_event *e = (struct _cl_event *)event;
    if (!e) return CL_INVALID_EVENT;
    if (e->queue != NULL || e->type != CL_COMMAND_USER) return CL_INVALID_EVENT;
    if (e->status <= CL_COMPLETE) return CL_INVALID_OPERATION;
    if (execution_status != CL_COMPLETE && execution_status >= 0) return CL_INVALID_VALUE;
    cl_event_set_status(e, execution_status);
    return CL_SUCCESS;
}

cl_int clSetEventCallback(cl_event event, cl_int command_exec_callback_type,
                          void (CL_CALLBACK *pfn_notify)(cl_event, cl_int, void *),
                          void *user_data)
{
    struct _cl_event *e = (struct _cl_event *)event;
    if (!e) return CL_INVALID_EVENT;
    if (!pfn_notify) return CL_INVALID_VALUE;
    if (command_exec_callback_type != CL_SUBMITTED &&
        command_exec_callback_type != CL_RUNNING &&
        command_exec_callback_type != CL_COMPLETE)
        return CL_INVALID_VALUE;
    if (e->num_callbacks >= 4) return CL_OUT_OF_HOST_MEMORY;
    /* if the event is already at/through this status, fire immediately */
    if (e->status <= CL_COMPLETE && command_exec_callback_type == CL_COMPLETE) {
        pfn_notify(event, e->status, user_data);
        return CL_SUCCESS;
    }
    e->callbacks[e->num_callbacks].trigger = command_exec_callback_type;
    e->callbacks[e->num_callbacks].fn = pfn_notify;
    e->callbacks[e->num_callbacks].user_data = user_data;
    e->num_callbacks++;
    return CL_SUCCESS;
}

cl_int clGetEventProfilingInfo(cl_event event, cl_profiling_info param_name,
                               size_t param_value_size, void *param_value,
                               size_t *param_value_size_ret)
{
    struct _cl_event *e = (struct _cl_event *)event;
    if (!e) return CL_INVALID_EVENT;
    if (e->status != CL_COMPLETE) return CL_PROFILING_INFO_NOT_AVAILABLE;
    switch (param_name) {
    case CL_PROFILING_COMMAND_QUEUED:
        INFO_E_SCALAR(cl_ulong, e->t_queued);
    case CL_PROFILING_COMMAND_SUBMIT:
        INFO_E_SCALAR(cl_ulong, e->t_submit);
    case CL_PROFILING_COMMAND_START:
        INFO_E_SCALAR(cl_ulong, e->t_start);
    case CL_PROFILING_COMMAND_END:
        INFO_E_SCALAR(cl_ulong, e->t_end);
    default:
        return CL_INVALID_VALUE;
    }
}
