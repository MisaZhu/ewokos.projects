/*
 * cl.h - OpenCL 1.2 API entry points for the EwokOS libOpenCL runtime.
 * The full 1.2 surface is declared here (platform, device, context, command
 * queue, memory, program, kernel, sampler, event and the GL/EGL interop
 * stubs); the signatures and semantics follow the Khronos 1.2 specification
 * so unmodified third-party OpenCL code links against libOpenCL.a.
 *
 * Two devices are exposed: an always-present CPU device (a software OpenCL-C
 * subset VM) and, on machines whose bsp_cl reports hardware support (raspi5
 * today), a GPU device that runs the same subset through the V3D compute path.
 * Applications select a device with clGetDeviceIDs(CL_DEVICE_TYPE_*); the
 * runtime is otherwise device-agnostic.
 */
#ifndef __OPENCL_CL_H
#define __OPENCL_CL_H

#include <CL/cl_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- structures used by the memory-object APIs -------------------------- */
typedef struct _cl_buffer_region {
    size_t origin;
    size_t size;
} cl_buffer_region;

typedef struct _cl_image_desc {
    cl_mem_object_type image_type;
    size_t             image_width;
    size_t             image_height;
    size_t             image_depth;
    size_t             image_array_size;
    size_t             image_row_pitch;
    size_t             image_slice_pitch;
    cl_uint            num_mip_levels;
    cl_uint            num_samples;
    cl_mem             buffer;
} cl_image_desc;

/* ---- Platform API ------------------------------------------------------- */
cl_int CL_API_ENTRY clGetPlatformIDs(cl_uint num_entries,
                                     cl_platform_id *platforms,
                                     cl_uint *num_platforms) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clGetPlatformInfo(cl_platform_id platform,
                                      cl_platform_info param_name,
                                      size_t param_value_size,
                                      void *param_value,
                                      size_t *param_value_size_ret) CL_API_SUFFIX__VERSION_1_0;

/* ---- Device APIs -------------------------------------------------------- */
cl_int CL_API_ENTRY clGetDeviceIDs(cl_platform_id platform,
                                   cl_device_type device_type,
                                   cl_uint num_entries,
                                   cl_device_id *devices,
                                   cl_uint *num_devices) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clGetDeviceInfo(cl_device_id device,
                                    cl_device_info param_name,
                                    size_t param_value_size,
                                    void *param_value,
                                    size_t *param_value_size_ret) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clCreateSubDevices(cl_device_id in_device,
                                       const cl_device_partition_property *properties,
                                       cl_uint num_devices,
                                       cl_device_id *out_devices,
                                       cl_uint *num_devices_ret) CL_API_SUFFIX__VERSION_1_2;

cl_int CL_API_ENTRY clRetainDevice(cl_device_id device) CL_API_SUFFIX__VERSION_1_2;
cl_int CL_API_ENTRY clReleaseDevice(cl_device_id device) CL_API_SUFFIX__VERSION_1_2;

/* ---- Context APIs ------------------------------------------------------- */
cl_context CL_API_ENTRY clCreateContext(const cl_context_properties *properties,
                                        cl_uint num_devices,
                                        const cl_device_id *devices,
                                        void (CL_CALLBACK *pfn_notify)(const char *, const void *, size_t, void *),
                                        void *user_data,
                                        cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_0;

cl_context CL_API_ENTRY clCreateContextFromType(const cl_context_properties *properties,
                                                cl_device_type device_type,
                                                void (CL_CALLBACK *pfn_notify)(const char *, const void *, size_t, void *),
                                                void *user_data,
                                                cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clRetainContext(cl_context context) CL_API_SUFFIX__VERSION_1_0;
cl_int CL_API_ENTRY clReleaseContext(cl_context context) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clGetContextInfo(cl_context context,
                                     cl_context_info param_name,
                                     size_t param_value_size,
                                     void *param_value,
                                     size_t *param_value_size_ret) CL_API_SUFFIX__VERSION_1_0;

/* ---- Command Queue APIs ------------------------------------------------- */
cl_command_queue CL_API_ENTRY clCreateCommandQueue(cl_context context,
                                                   cl_device_id device,
                                                   cl_command_queue_properties properties,
                                                   cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clRetainCommandQueue(cl_command_queue command_queue) CL_API_SUFFIX__VERSION_1_0;
cl_int CL_API_ENTRY clReleaseCommandQueue(cl_command_queue command_queue) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clGetCommandQueueInfo(cl_command_queue command_queue,
                                          cl_command_queue_info param_name,
                                          size_t param_value_size,
                                          void *param_value,
                                          size_t *param_value_size_ret) CL_API_SUFFIX__VERSION_1_0;

/* ---- Memory Object APIs ------------------------------------------------- */
cl_mem CL_API_ENTRY clCreateBuffer(cl_context context,
                                   cl_mem_flags flags,
                                   size_t size,
                                   void *host_ptr,
                                   cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_0;

cl_mem CL_API_ENTRY clCreateSubBuffer(cl_mem buffer,
                                      cl_mem_flags flags,
                                      cl_buffer_create_type buffer_create_type,
                                      const void *buffer_create_info,
                                      cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_1;

cl_mem CL_API_ENTRY clCreateImage(cl_context context,
                                  cl_mem_flags flags,
                                  const cl_image_format *image_format,
                                  const cl_image_desc *image_desc,
                                  void *host_ptr,
                                  cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_2;

cl_mem CL_API_ENTRY clCreateImage2D(cl_context context,
                                    cl_mem_flags flags,
                                    const cl_image_format *image_format,
                                    size_t image_width,
                                    size_t image_height,
                                    size_t image_row_pitch,
                                    void *host_ptr,
                                    cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_0;

cl_mem CL_API_ENTRY clCreateImage3D(cl_context context,
                                    cl_mem_flags flags,
                                    const cl_image_format *image_format,
                                    size_t image_width,
                                    size_t image_height,
                                    size_t image_depth,
                                    size_t image_row_pitch,
                                    size_t image_slice_pitch,
                                    void *host_ptr,
                                    cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clRetainMemObject(cl_mem memobj) CL_API_SUFFIX__VERSION_1_0;
cl_int CL_API_ENTRY clReleaseMemObject(cl_mem memobj) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clGetSupportedImageFormats(cl_context context,
                                               cl_mem_flags flags,
                                               cl_mem_object_type image_type,
                                               cl_uint num_entries,
                                               cl_image_format *image_formats,
                                               cl_uint *num_image_formats) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clGetMemObjectInfo(cl_mem memobj,
                                       cl_mem_info param_name,
                                       size_t param_value_size,
                                       void *param_value,
                                       size_t *param_value_size_ret) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clGetImageInfo(cl_mem image,
                                   cl_image_info param_name,
                                   size_t param_value_size,
                                   void *param_value,
                                   size_t *param_value_size_ret) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clSetMemObjectDestructorCallback(cl_mem memobj,
                                                     void (CL_CALLBACK *pfn_notify)(cl_mem, void *),
                                                     void *user_data) CL_API_SUFFIX__VERSION_1_1;

/* ---- Sampler APIs ------------------------------------------------------- */
cl_sampler CL_API_ENTRY clCreateSampler(cl_context context,
                                        cl_bool normalized_coords,
                                        cl_addressing_mode addressing_mode,
                                        cl_filter_mode filter_mode,
                                        cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clRetainSampler(cl_sampler sampler) CL_API_SUFFIX__VERSION_1_0;
cl_int CL_API_ENTRY clReleaseSampler(cl_sampler sampler) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clGetSamplerInfo(cl_sampler sampler,
                                     cl_sampler_info param_name,
                                     size_t param_value_size,
                                     void *param_value,
                                     size_t *param_value_size_ret) CL_API_SUFFIX__VERSION_1_0;

/* ---- Program Object APIs ------------------------------------------------ */
cl_program CL_API_ENTRY clCreateProgramWithSource(cl_context context,
                                                  cl_uint count,
                                                  const char **strings,
                                                  const size_t *lengths,
                                                  cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_0;

cl_program CL_API_ENTRY clCreateProgramWithBinary(cl_context context,
                                                  cl_uint num_devices,
                                                  const cl_device_id *device_list,
                                                  const size_t *lengths,
                                                  const unsigned char **binaries,
                                                  cl_int *binary_status,
                                                  cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_0;

cl_program CL_API_ENTRY clCreateProgramWithBuiltInKernels(cl_context context,
                                                          cl_uint num_devices,
                                                          const cl_device_id *device_list,
                                                          const char *kernel_names,
                                                          cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_2;

cl_int CL_API_ENTRY clRetainProgram(cl_program program) CL_API_SUFFIX__VERSION_1_0;
cl_int CL_API_ENTRY clReleaseProgram(cl_program program) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clBuildProgram(cl_program program,
                                   cl_uint num_devices,
                                   const cl_device_id *device_list,
                                   const char *options,
                                   void (CL_CALLBACK *pfn_notify)(cl_program, void *),
                                   void *user_data) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clCompileProgram(cl_program program,
                                     cl_uint num_devices,
                                     const cl_device_id *device_list,
                                     const char *options,
                                     cl_uint num_input_headers,
                                     const cl_program *input_headers,
                                     const char **header_include_names,
                                     void (CL_CALLBACK *pfn_notify)(cl_program, void *),
                                     void *user_data) CL_API_SUFFIX__VERSION_1_2;

cl_program CL_API_ENTRY clLinkProgram(cl_context context,
                                      cl_uint num_devices,
                                      const cl_device_id *device_list,
                                      const char *options,
                                      cl_uint num_input_programs,
                                      const cl_program *input_programs,
                                      void (CL_CALLBACK *pfn_notify)(cl_program, void *),
                                      void *user_data,
                                      cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_2;

cl_int CL_API_ENTRY clUnloadCompiler(void) CL_API_SUFFIX__VERSION_1_0;
cl_int CL_API_ENTRY clUnloadPlatformCompiler(cl_platform_id platform) CL_API_SUFFIX__VERSION_1_2;

cl_int CL_API_ENTRY clGetProgramInfo(cl_program program,
                                     cl_program_info param_name,
                                     size_t param_value_size,
                                     void *param_value,
                                     size_t *param_value_size_ret) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clGetProgramBuildInfo(cl_program program,
                                          cl_device_id device,
                                          cl_program_build_info param_name,
                                          size_t param_value_size,
                                          void *param_value,
                                          size_t *param_value_size_ret) CL_API_SUFFIX__VERSION_1_0;

/* ---- Kernel Object APIs ------------------------------------------------- */
cl_kernel CL_API_ENTRY clCreateKernel(cl_program program,
                                      const char *kernel_name,
                                      cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clCreateKernelsInProgram(cl_program program,
                                             cl_uint num_kernels,
                                             cl_kernel *kernels,
                                             cl_uint *num_kernels_ret) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clRetainKernel(cl_kernel kernel) CL_API_SUFFIX__VERSION_1_0;
cl_int CL_API_ENTRY clReleaseKernel(cl_kernel kernel) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clSetKernelArg(cl_kernel kernel,
                                   cl_uint arg_index,
                                   size_t arg_size,
                                   const void *arg_value) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clGetKernelInfo(cl_kernel kernel,
                                    cl_kernel_info param_name,
                                    size_t param_value_size,
                                    void *param_value,
                                    size_t *param_value_size_ret) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clGetKernelArgInfo(cl_kernel kernel,
                                       cl_uint arg_indx,
                                       cl_kernel_arg_info param_name,
                                       size_t param_value_size,
                                       void *param_value,
                                       size_t *param_value_size_ret) CL_API_SUFFIX__VERSION_1_2;

cl_int CL_API_ENTRY clGetKernelWorkGroupInfo(cl_kernel kernel,
                                             cl_device_id device,
                                             cl_kernel_work_group_info param_name,
                                             size_t param_value_size,
                                             void *param_value,
                                             size_t *param_value_size_ret) CL_API_SUFFIX__VERSION_1_0;

/* ---- Event Object APIs -------------------------------------------------- */
cl_int CL_API_ENTRY clWaitForEvents(cl_uint num_events,
                                    const cl_event *event_list) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clGetEventInfo(cl_event event,
                                   cl_event_info param_name,
                                   size_t param_value_size,
                                   void *param_value,
                                   size_t *param_value_size_ret) CL_API_SUFFIX__VERSION_1_0;

cl_event CL_API_ENTRY clCreateUserEvent(cl_context context,
                                        cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_1;

cl_int CL_API_ENTRY clRetainEvent(cl_event event) CL_API_SUFFIX__VERSION_1_0;
cl_int CL_API_ENTRY clReleaseEvent(cl_event event) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clSetUserEventStatus(cl_event event,
                                         cl_int execution_status) CL_API_SUFFIX__VERSION_1_1;

cl_int CL_API_ENTRY clSetEventCallback(cl_event event,
                                       cl_int command_exec_callback_type,
                                       void (CL_CALLBACK *pfn_notify)(cl_event, cl_int, void *),
                                       void *user_data) CL_API_SUFFIX__VERSION_1_1;

/* ---- Profiling APIs ----------------------------------------------------- */
cl_int CL_API_ENTRY clGetEventProfilingInfo(cl_event event,
                                            cl_profiling_info param_name,
                                            size_t param_value_size,
                                            void *param_value,
                                            size_t *param_value_size_ret) CL_API_SUFFIX__VERSION_1_0;

/* ---- Flush and Finish APIs ---------------------------------------------- */
cl_int CL_API_ENTRY clFlush(cl_command_queue command_queue) CL_API_SUFFIX__VERSION_1_0;
cl_int CL_API_ENTRY clFinish(cl_command_queue command_queue) CL_API_SUFFIX__VERSION_1_0;

/* ---- Enqueued Commands APIs --------------------------------------------- */
cl_int CL_API_ENTRY clEnqueueReadBuffer(cl_command_queue command_queue,
                                        cl_mem buffer,
                                        cl_bool blocking_read,
                                        size_t offset,
                                        size_t size,
                                        void *ptr,
                                        cl_uint num_events_in_wait_list,
                                        const cl_event *event_wait_list,
                                        cl_event *event) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clEnqueueReadBufferRect(cl_command_queue command_queue,
                                            cl_mem buffer,
                                            cl_bool blocking_read,
                                            const size_t *buffer_offset,
                                            const size_t *host_offset,
                                            const size_t *region,
                                            size_t buffer_row_pitch,
                                            size_t buffer_slice_pitch,
                                            size_t host_row_pitch,
                                            size_t host_slice_pitch,
                                            void *ptr,
                                            cl_uint num_events_in_wait_list,
                                            const cl_event *event_wait_list,
                                            cl_event *event) CL_API_SUFFIX__VERSION_1_1;

cl_int CL_API_ENTRY clEnqueueWriteBuffer(cl_command_queue command_queue,
                                         cl_mem buffer,
                                         cl_bool blocking_write,
                                         size_t offset,
                                         size_t size,
                                         const void *ptr,
                                         cl_uint num_events_in_wait_list,
                                         const cl_event *event_wait_list,
                                         cl_event *event) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clEnqueueWriteBufferRect(cl_command_queue command_queue,
                                             cl_mem buffer,
                                             cl_bool blocking_write,
                                             const size_t *buffer_offset,
                                             const size_t *host_offset,
                                             const size_t *region,
                                             size_t buffer_row_pitch,
                                             size_t buffer_slice_pitch,
                                             size_t host_row_pitch,
                                             size_t host_slice_pitch,
                                             const void *ptr,
                                             cl_uint num_events_in_wait_list,
                                             const cl_event *event_wait_list,
                                             cl_event *event) CL_API_SUFFIX__VERSION_1_1;

cl_int CL_API_ENTRY clEnqueueFillBuffer(cl_command_queue command_queue,
                                        cl_mem buffer,
                                        const void *pattern,
                                        size_t pattern_size,
                                        size_t offset,
                                        size_t size,
                                        cl_uint num_events_in_wait_list,
                                        const cl_event *event_wait_list,
                                        cl_event *event) CL_API_SUFFIX__VERSION_1_2;

cl_int CL_API_ENTRY clEnqueueCopyBuffer(cl_command_queue command_queue,
                                        cl_mem src_buffer,
                                        cl_mem dst_buffer,
                                        size_t src_offset,
                                        size_t dst_offset,
                                        size_t size,
                                        cl_uint num_events_in_wait_list,
                                        const cl_event *event_wait_list,
                                        cl_event *event) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clEnqueueCopyBufferRect(cl_command_queue command_queue,
                                            cl_mem src_buffer,
                                            cl_mem dst_buffer,
                                            const size_t *src_origin,
                                            const size_t *dst_origin,
                                            const size_t *region,
                                            size_t src_row_pitch,
                                            size_t src_slice_pitch,
                                            size_t dst_row_pitch,
                                            size_t dst_slice_pitch,
                                            cl_uint num_events_in_wait_list,
                                            const cl_event *event_wait_list,
                                            cl_event *event) CL_API_SUFFIX__VERSION_1_1;

cl_int CL_API_ENTRY clEnqueueReadImage(cl_command_queue command_queue,
                                       cl_mem image,
                                       cl_bool blocking_read,
                                       const size_t *origin,
                                       const size_t *region,
                                       size_t row_pitch,
                                       size_t slice_pitch,
                                       void *ptr,
                                       cl_uint num_events_in_wait_list,
                                       const cl_event *event_wait_list,
                                       cl_event *event) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clEnqueueWriteImage(cl_command_queue command_queue,
                                        cl_mem image,
                                        cl_bool blocking_write,
                                        const size_t *origin,
                                        const size_t *region,
                                        size_t input_row_pitch,
                                        size_t input_slice_pitch,
                                        const void *ptr,
                                        cl_uint num_events_in_wait_list,
                                        const cl_event *event_wait_list,
                                        cl_event *event) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clEnqueueFillImage(cl_command_queue command_queue,
                                       cl_mem image,
                                       const void *fill_color,
                                       const size_t *origin,
                                       const size_t *region,
                                       cl_uint num_events_in_wait_list,
                                       const cl_event *event_wait_list,
                                       cl_event *event) CL_API_SUFFIX__VERSION_1_2;

cl_int CL_API_ENTRY clEnqueueCopyImage(cl_command_queue command_queue,
                                       cl_mem src_image,
                                       cl_mem dst_image,
                                       const size_t *src_origin,
                                       const size_t *dst_origin,
                                       const size_t *region,
                                       cl_uint num_events_in_wait_list,
                                       const cl_event *event_wait_list,
                                       cl_event *event) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clEnqueueCopyImageToBuffer(cl_command_queue command_queue,
                                               cl_mem src_image,
                                               cl_mem dst_buffer,
                                               const size_t *src_origin,
                                               const size_t *region,
                                               size_t dst_offset,
                                               cl_uint num_events_in_wait_list,
                                               const cl_event *event_wait_list,
                                               cl_event *event) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clEnqueueCopyBufferToImage(cl_command_queue command_queue,
                                               cl_mem src_buffer,
                                               cl_mem dst_image,
                                               size_t src_offset,
                                               const size_t *dst_origin,
                                               const size_t *region,
                                               cl_uint num_events_in_wait_list,
                                               const cl_event *event_wait_list,
                                               cl_event *event) CL_API_SUFFIX__VERSION_1_0;

void * CL_API_ENTRY clEnqueueMapBuffer(cl_command_queue command_queue,
                                       cl_mem buffer,
                                       cl_bool blocking_map,
                                       cl_map_flags map_flags,
                                       size_t offset,
                                       size_t size,
                                       cl_uint num_events_in_wait_list,
                                       const cl_event *event_wait_list,
                                       cl_event *event,
                                       cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_0;

void * CL_API_ENTRY clEnqueueMapImage(cl_command_queue command_queue,
                                      cl_mem image,
                                      cl_bool blocking_map,
                                      cl_map_flags map_flags,
                                      const size_t *origin,
                                      const size_t *region,
                                      size_t *image_row_pitch,
                                      size_t *image_slice_pitch,
                                      cl_uint num_events_in_wait_list,
                                      const cl_event *event_wait_list,
                                      cl_event *event,
                                      cl_int *errcode_ret) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clEnqueueUnmapMemObject(cl_command_queue command_queue,
                                            cl_mem memobj,
                                            void *mapped_ptr,
                                            cl_uint num_events_in_wait_list,
                                            const cl_event *event_wait_list,
                                            cl_event *event) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clEnqueueMigrateMemObjects(cl_command_queue command_queue,
                                               cl_uint num_mem_objects,
                                               const cl_mem *mem_objects,
                                               cl_mem_migration_flags flags,
                                               cl_uint num_events_in_wait_list,
                                               const cl_event *event_wait_list,
                                               cl_event *event) CL_API_SUFFIX__VERSION_1_2;

cl_int CL_API_ENTRY clEnqueueNDRangeKernel(cl_command_queue command_queue,
                                           cl_kernel kernel,
                                           cl_uint work_dim,
                                           const size_t *global_work_offset,
                                           const size_t *global_work_size,
                                           const size_t *local_work_size,
                                           cl_uint num_events_in_wait_list,
                                           const cl_event *event_wait_list,
                                           cl_event *event) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clEnqueueTask(cl_command_queue command_queue,
                                  cl_kernel kernel,
                                  cl_uint num_events_in_wait_list,
                                  const cl_event *event_wait_list,
                                  cl_event *event) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clEnqueueNativeKernel(cl_command_queue command_queue,
                                          void (CL_CALLBACK *user_func)(void *),
                                          void *args,
                                          size_t cb_args,
                                          cl_uint num_mem_objects,
                                          const cl_mem *mem_list,
                                          const void **args_mem_loc,
                                          cl_uint num_events_in_wait_list,
                                          const cl_event *event_wait_list,
                                          cl_event *event) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clEnqueueMarkerWithWaitList(cl_command_queue command_queue,
                                                cl_uint num_events_in_wait_list,
                                                const cl_event *event_wait_list,
                                                cl_event *event) CL_API_SUFFIX__VERSION_1_2;

cl_int CL_API_ENTRY clEnqueueBarrierWithWaitList(cl_command_queue command_queue,
                                                 cl_uint num_events_in_wait_list,
                                                 const cl_event *event_wait_list,
                                                 cl_event *event) CL_API_SUFFIX__VERSION_1_2;

/* ---- Extension function access ------------------------------------------ */
void * CL_API_ENTRY clGetExtensionFunctionAddressForPlatform(cl_platform_id platform,
                                                             const char *func_name) CL_API_SUFFIX__VERSION_1_2;

void * CL_API_ENTRY clGetExtensionFunctionAddress(const char *func_name) CL_API_SUFFIX__VERSION_1_0;

/* ---- Deprecated 1.1 image APIs (kept for source compatibility) ---------- */
cl_int CL_API_ENTRY clSetCommandQueueProperty(cl_command_queue command_queue,
                                              cl_command_queue_properties properties,
                                              cl_bool enable,
                                              cl_command_queue_properties *old_properties) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clEnqueueMarker(cl_command_queue command_queue,
                                    cl_event *event) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clEnqueueWaitForEvents(cl_command_queue command_queue,
                                           cl_uint num_events,
                                           const cl_event *event_list) CL_API_SUFFIX__VERSION_1_0;

cl_int CL_API_ENTRY clEnqueueBarrier(cl_command_queue command_queue) CL_API_SUFFIX__VERSION_1_0;

#ifdef __cplusplus
}
#endif

#endif /* __OPENCL_CL_H */
