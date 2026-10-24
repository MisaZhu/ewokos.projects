/*
 * event.c - event object lifecycle shared by every enqueue path: allocation,
 * status transitions (with callback firing), profiling timestamps and
 * wait-list draining.  In this runtime commands complete synchronously (the
 * CPU device executes in the enqueue call; the GPU dispatch blocks on the
 * daemon), so an event is normally created, stamped and set COMPLETE before
 * the enqueue returns; the ordering machinery still honours wait lists and
 * user events so multi-command dependency graphs behave per the spec.
 */
#include "internal.h"
#include <stdlib.h>

struct _cl_event *cl_event_create(struct _cl_command_queue *queue,
                                  cl_command_type type)
{
    struct _cl_event *ev = (struct _cl_event *)calloc(1, sizeof(*ev));
    if (!ev)
        return NULL;
    ev->refcount = 1;
    ev->queue = queue;
    ev->context = queue ? queue->context : NULL;
    ev->type = type;
    ev->status = CL_QUEUED;
    ev->t_queued = cl_now_ns();
    return ev;
}

void cl_event_stamp(struct _cl_event *ev, int which)
{
    cl_ulong t = cl_now_ns();
    if (!ev)
        return;
    switch (which) {
    case 0: ev->t_submit = t; break;
    case 1: ev->t_start = t; break;
    case 2: ev->t_end = t; break;
    default: break;
    }
}

void cl_event_set_status(struct _cl_event *ev, cl_int status)
{
    cl_uint i;
    if (!ev)
        return;
    ev->status = status;
    if (status == CL_SUBMITTED)
        cl_event_stamp(ev, 0);
    else if (status == CL_RUNNING)
        cl_event_stamp(ev, 1);
    else if (status <= CL_COMPLETE)
        cl_event_stamp(ev, 2);

    /* fire any callbacks registered for this status (or for COMPLETE when the
     * event reaches a terminal state). */
    for (i = 0; i < ev->num_callbacks; i++) {
        cl_event_callback *cb = &ev->callbacks[i];
        if (cb->trigger == status ||
            (status <= CL_COMPLETE && cb->trigger == CL_COMPLETE)) {
            if (cb->fn)
                cb->fn((cl_event)ev, status, cb->user_data);
        }
    }
}

int cl_event_wait_list(cl_uint n, const cl_event *list)
{
    cl_uint i;
    if (n == 0)
        return CL_SUCCESS;
    if (!list)
        return CL_INVALID_EVENT_WAIT_LIST;
    for (i = 0; i < n; i++) {
        if (!list[i])
            return CL_INVALID_EVENT_WAIT_LIST;
        /* commands are synchronous, so a non-user event is already terminal;
         * a user event still pending means the enqueue cannot proceed. */
        if (list[i]->status > CL_COMPLETE)
            return CL_INVALID_EVENT_WAIT_LIST;
        if (list[i]->status < 0)
            return CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST;
    }
    return CL_SUCCESS;
}
