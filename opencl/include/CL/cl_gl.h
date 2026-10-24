/*
 * cl_gl.h - OpenCL 1.2 / OpenGL interoperability API.
 * The EwokOS libOpenCL exposes the full interop entry-point set so OpenCL-GL
 * sources compile and link; the runtime implements them as CL 1.2-consistent
 * stubs (there is no shared GL/CL device today), returning CL_INVALID_GL_OBJECT
 * / CL_INVALID_OPERATION as the specification prescribes for an unsupported
 * share.  The GL scalar types are declared locally so this header needs no
 * GLES dependency.
 */
#ifndef __OPENCL_CL_GL_H
#define __OPENCL_CL_GL_H

#include <CL/cl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef cl_uint     cl_GLuint;
typedef cl_int      cl_GLint;
typedef cl_uint     cl_GLenum;
typedef void *      cl_GLsync;

typedef cl_uint     cl_gl_object_type;
typedef cl_uint     cl_gl_texture_info;
typedef cl_uint     cl_gl_platform_info;
typedef struct __GLsync *cl_GLsync_handle;

/* cl_gl_object_type */
#define CL_GL_OBJECT_BUFFER             0x2000
#define CL_GL_OBJECT_TEXTURE2D          0x2001
#define CL_GL_OBJECT_TEXTURE3D          0x2002
#define CL_GL_OBJECT_RENDERBUFFER       0x2003
#define CL_GL_OBJECT_TEXTURE2D_ARRAY    0x200E
#define CL_GL_OBJECT_TEXTURE1D          0x200F
#define CL_GL_OBJECT_TEXTURE1D_ARRAY    0x2010
#define CL_GL_OBJECT_TEXTURE_BUFFER     0x2011

/* cl_gl_texture_info */
#define CL_GL_TEXTURE_TARGET            0x2004
#define CL_GL_MIPMAP_LEVEL              0x2005

/* cl_khr_gl_sharing */
#define CL_INVALID_GL_SHAREGROUP_REFERENCE_KHR  -1000
#define CL_CURRENT_DEVICE_FOR_GL_CONTEXT_KHR    0x2006
#define CL_DEVICES_FOR_GL_CONTEXT_KHR           0x2007
#define CL_GL_CONTEXT_KHR                       0x2008
#define CL_EGL_DISPLAY_KHR                      0x2009
#define CL_GLX_DISPLAY_KHR                      0x200A
#define CL_WGL_HDC_KHR                          0x200B
#define CL_CGL_SHAREGROUP_KHR                   0x200C

cl_mem CL_API_ENTRY clCreateFromGLBuffer(cl_context context,
                                         cl_mem_flags flags,
                                         cl_GLuint bufobj,
                                         cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_0;

cl_mem CL_API_ENTRY clCreateFromGLTexture(cl_context context,
                                          cl_mem_flags flags,
                                          cl_GLenum target,
                                          cl_GLint miplevel,
                                          cl_GLuint texture,
                                          cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_2;

cl_mem CL_API_ENTRY clCreateFromGLTexture2D(cl_context context,
                                            cl_mem_flags flags,
                                            cl_GLenum target,
                                            cl_GLint miplevel,
                                            cl_GLuint texture,
                                            cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_0;

cl_mem CL_API_ENTRY clCreateFromGLTexture3D(cl_context context,
                                            cl_mem_flags flags,
                                            cl_GLenum target,
                                            cl_GLint miplevel,
                                            cl_GLuint texture,
                                            cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_0;

cl_mem CL_API_ENTRY clCreateFromGLRenderbuffer(cl_context context,
                                               cl_mem_flags flags,
                                               cl_GLuint renderbuffer,
                                               cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clGetGLObjectInfo(cl_mem memobj,
                                      cl_gl_object_type *gl_object_type,
                                      cl_GLuint *gl_object_name) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clGetGLTextureInfo(cl_mem memobj,
                                       cl_gl_texture_info param_name,
                                       size_t param_value_size,
                                       void *param_value,
                                       size_t *param_value_size_ret) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clEnqueueAcquireGLObjects(cl_command_queue command_queue,
                                              cl_uint num_objects,
                                              const cl_mem *mem_objects,
                                              cl_uint num_events_in_wait_list,
                                              const cl_event *event_wait_list,
                                              cl_event *event) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clEnqueueReleaseGLObjects(cl_command_queue command_queue,
                                              cl_uint num_objects,
                                              const cl_mem *mem_objects,
                                              cl_uint num_events_in_wait_list,
                                              const cl_event *event_wait_list,
                                              cl_event *event) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clGetGLContextInfoKHR(const cl_context_properties *properties,
                                          cl_gl_platform_info param_name,
                                          size_t param_value_size,
                                          void *param_value,
                                          size_t *param_value_size_ret) CL_API_SUFFIX__VERSION_1_0;

#ifdef __cplusplus
}
#endif

#endif /* __OPENCL_CL_GL_H */
