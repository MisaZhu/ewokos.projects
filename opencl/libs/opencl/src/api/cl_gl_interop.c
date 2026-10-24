/*
 * cl_gl_interop.c - the OpenCL/GL(EGL) interoperability entry points and the
 * extension-function-address lookups.  There is no shared GL/CL device in this
 * runtime, so the interop creators return the CL 1.2-prescribed errors for an
 * unsupported share (CL_INVALID_GL_OBJECT / CL_INVALID_OPERATION) while keeping
 * the full symbol set so OpenCL-GL sources link unchanged.  Full interop is a
 * Phase 2 stretch; the CPU device never advertises cl_khr_gl_sharing.
 */
#include "internal.h"
#include <CL/cl_gl.h>
#include <CL/cl_egl.h>

#define GL_STUB(errcode_ret, err) do { \
    (void)context; (void)flags; \
    if (errcode_ret) *errcode_ret = (err); \
    return NULL; \
} while (0)

cl_mem clCreateFromGLBuffer(cl_context context, cl_mem_flags flags, cl_GLuint bufobj,
                            cl_int *errcode_ret)
{
    (void)bufobj;
    GL_STUB(errcode_ret, CL_INVALID_GL_OBJECT);
}

cl_mem clCreateFromGLTexture(cl_context context, cl_mem_flags flags, cl_GLenum target,
                             cl_GLint miplevel, cl_GLuint texture, cl_int *errcode_ret)
{
    (void)target; (void)miplevel; (void)texture;
    GL_STUB(errcode_ret, CL_INVALID_GL_OBJECT);
}

cl_mem clCreateFromGLTexture2D(cl_context context, cl_mem_flags flags, cl_GLenum target,
                               cl_GLint miplevel, cl_GLuint texture, cl_int *errcode_ret)
{
    (void)target; (void)miplevel; (void)texture;
    GL_STUB(errcode_ret, CL_INVALID_GL_OBJECT);
}

cl_mem clCreateFromGLTexture3D(cl_context context, cl_mem_flags flags, cl_GLenum target,
                               cl_GLint miplevel, cl_GLuint texture, cl_int *errcode_ret)
{
    (void)target; (void)miplevel; (void)texture;
    GL_STUB(errcode_ret, CL_INVALID_GL_OBJECT);
}

cl_mem clCreateFromGLRenderbuffer(cl_context context, cl_mem_flags flags, cl_GLuint renderbuffer,
                                  cl_int *errcode_ret)
{
    (void)renderbuffer;
    GL_STUB(errcode_ret, CL_INVALID_GL_OBJECT);
}

cl_int clGetGLObjectInfo(cl_mem memobj, cl_gl_object_type *gl_object_type, cl_GLuint *gl_object_name)
{
    (void)memobj;
    if (gl_object_type) *gl_object_type = 0;
    if (gl_object_name) *gl_object_name = 0;
    return CL_INVALID_MEM_OBJECT;
}

cl_int clGetGLTextureInfo(cl_mem memobj, cl_gl_texture_info param_name, size_t param_value_size,
                          void *param_value, size_t *param_value_size_ret)
{
    (void)memobj; (void)param_name; (void)param_value_size;
    (void)param_value; (void)param_value_size_ret;
    return CL_INVALID_MEM_OBJECT;
}

cl_int clEnqueueAcquireGLObjects(cl_command_queue command_queue, cl_uint num_objects,
                                 const cl_mem *mem_objects, cl_uint n, const cl_event *wl,
                                 cl_event *event)
{
    (void)num_objects; (void)mem_objects; (void)n; (void)wl; (void)event;
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    return CL_INVALID_GL_OBJECT;
}

cl_int clEnqueueReleaseGLObjects(cl_command_queue command_queue, cl_uint num_objects,
                                 const cl_mem *mem_objects, cl_uint n, const cl_event *wl,
                                 cl_event *event)
{
    (void)num_objects; (void)mem_objects; (void)n; (void)wl; (void)event;
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    return CL_INVALID_GL_OBJECT;
}

cl_int clGetGLContextInfoKHR(const cl_context_properties *properties, cl_gl_platform_info param_name,
                             size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    (void)properties; (void)param_name; (void)param_value_size;
    (void)param_value; (void)param_value_size_ret;
    return CL_INVALID_GL_SHAREGROUP_REFERENCE_KHR;
}

/* ---- extension function address ------------------------------------------ */
typedef struct { const char *name; void *fn; } cl_ext_entry;

static const cl_ext_entry g_exts[] = {
    { "clGetGLContextInfoKHR", (void *)clGetGLContextInfoKHR },
    { NULL, NULL }
};

void *clGetExtensionFunctionAddressForPlatform(cl_platform_id platform, const char *func_name)
{
    int i;
    (void)platform;
    if (!func_name) return NULL;
    for (i = 0; g_exts[i].name; i++)
        if (!strcmp(g_exts[i].name, func_name))
            return g_exts[i].fn;
    return NULL;
}

void *clGetExtensionFunctionAddress(const char *func_name)
{
    return clGetExtensionFunctionAddressForPlatform(NULL, func_name);
}
