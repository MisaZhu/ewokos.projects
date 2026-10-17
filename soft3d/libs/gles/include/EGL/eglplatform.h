#ifndef __eglplatform_h_
#define __eglplatform_h_

/*
** Copyright 2007-2020 The Khronos Group Inc.
** SPDX-License-Identifier: Apache-2.0
*/

/* Platform-specific types and definitions for egl.h
 *
 * Adopters may modify khrplatform.h and this file to suit their platform.
 * Please contribute modifications back to Khronos as pull requests on the
 * public github repository:
 *      https://github.com/KhronosGroup/EGL-Registry
 */

#include <KHR/khrplatform.h>

/* Macros used in EGL function prototype declarations.
 *
 * EGL functions should be prototyped as:
 *
 * EGLAPI return-type EGLAPIENTRY eglFunction(arguments);
 * typedef return-type (EXPAPIENTRYP PFNEGLFUNCTIONPROC) (arguments);
 *
 * KHRONOS_APICALL and KHRONOS_APIENTRY are defined in KHR/khrplatform.h
 */

#ifndef EGLAPI
#define EGLAPI KHRONOS_APICALL
#endif

#ifndef EGLAPIENTRY
#define EGLAPIENTRY  KHRONOS_APIENTRY
#endif
#define EGLAPIENTRYP EGLAPIENTRY*

/* The types native display, native pixmap and native window have to be
 * defined by the platform-specific header.  On EwokOS there is no OS-provided
 * EGL platform header, so this library owns the definitions:
 *
 *  - EGLNativeDisplayType is unused (a single implicit display), so it is an
 *    opaque integer.
 *  - EGLNativeWindowType is the EwokOS window handle, xwin_t*.  It is typed as
 *    void* here so this public header stays free of any EwokOS include; the
 *    libEGL implementation casts it back to xwin_t*.
 *  - EGLNativePixmapType is unused (surfaces are always window or pbuffer).
 */

typedef khronos_intptr_t  EGLNativeDisplayType;
typedef khronos_intptr_t  EGLNativePixmapType;
typedef void*             EGLNativeWindowType;

#define EGL_CAST(type, value) ((type) (value))

#endif /* __eglplatform_h */
