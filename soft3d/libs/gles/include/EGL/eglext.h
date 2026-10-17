#ifndef __eglext_h_
#define __eglext_h_ 1

#ifdef __cplusplus
extern "C" {
#endif

/*
** Copyright 2013-2020 The Khronos Group Inc.
** SPDX-License-Identifier: Apache-2.0
**
** Minimal EGL extension header for the EwokOS libEGL front end.  Only the
** extension tokens/types that the portablegl-backed implementation actually
** exposes are declared here; the full Khronos eglext.h is intentionally not
** vendored.  Include after <EGL/egl.h>.
*/

#include <EGL/egl.h>

#ifndef EGL_KHR_get_all_proc_addresses
#define EGL_KHR_get_all_proc_addresses 1
#endif

#ifndef EGL_KHR_fence_sync
#define EGL_KHR_fence_sync 1
#define EGL_SYNC_PRIOR_COMMANDS_COMPLETE_KHR 0x30F0
#define EGL_SYNC_CONDITION_KHR          0x30F8
#define EGL_SYNC_FENCE_KHR              0x30F9
#endif

#ifndef EGL_KHR_wait_sync
#define EGL_KHR_wait_sync 1
#endif

#ifdef __cplusplus
}
#endif

#endif /* __eglext_h_ */
