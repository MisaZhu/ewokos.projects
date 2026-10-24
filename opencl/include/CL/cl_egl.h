/*
 * cl_egl.h - OpenCL 1.2 / EGL interoperability (cl_khr_egl_image,
 * cl_khr_egl_sharing).  Declared so EGL-interop sources compile; the runtime
 * implements the entry points as CL 1.2-consistent stubs.
 */
#ifndef __OPENCL_CL_EGL_H
#define __OPENCL_CL_EGL_H

#include <CL/cl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *CLeglImageKHR;
typedef void *CLeglDisplayKHR;
typedef void *CLeglSyncKHR;
typedef intptr_t CLeglImagePropertiesKHR;
typedef cl_uint  CLenum;

#define CL_EGL_IMAGE_PROPERTIES_KHR 0x202E

cl_mem CL_API_ENTRY clCreateFromEGLImageKHR(cl_context context,
                                            CLeglDisplayKHR egldisplay,
                                            CLeglImageKHR eglimage,
                                            cl_mem_flags flags,
                                            const CLeglImagePropertiesKHR *properties,
                                            cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clEnqueueAcquireEGLObjectsKHR(cl_command_queue command_queue,
                                                  cl_uint num_objects,
                                                  const cl_mem *mem_objects,
                                                  cl_uint num_events_in_wait_list,
                                                  const cl_event *event_wait_list,
                                                  cl_event *event) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clEnqueueReleaseEGLObjectsKHR(cl_command_queue command_queue,
                                                  cl_uint num_objects,
                                                  const cl_mem *mem_objects,
                                                  cl_uint num_events_in_wait_list,
                                                  const cl_event *event_wait_list,
                                                  cl_event *event) CL_API_SUFFIX__VERSION_1_0;

#ifdef __cplusplus
}
#endif

#endif /* __OPENCL_CL_EGL_H */
