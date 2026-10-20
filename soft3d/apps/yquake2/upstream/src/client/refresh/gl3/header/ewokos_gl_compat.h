/*
 * ewokos_gl_compat.h - minimal shims so yquake2's gl3 refresher compiles
 * against the EwokOS libGLESv2 front end (projects/soft3d/libs/gles), whose
 * <GLES3/gl3.h> is a strict OpenGL ES header.
 *
 * Desktop-GL / extension symbols that gl3 references but ES does not declare
 * are provided here.  They only need to COMPILE: the front end ignores
 * anisotropic filtering (gl3config.anisotropic stays false, so the calls are
 * never issued) and has no double-precision GL path.
 */

#ifndef SRC_CLIENT_REFRESH_GL3_HEADER_EWOKOS_GL_COMPAT_H_
#define SRC_CLIENT_REFRESH_GL3_HEADER_EWOKOS_GL_COMPAT_H_

/* ES has no double-precision scalars; gl3's GL3_SetPerspective() takes a
 * GLdouble fovy.  Provide the desktop typedef so the signature compiles. */
#ifndef GLdouble
typedef double GLdouble;
#endif
#ifndef GLclampd
typedef double GLclampd;
#endif

/* GL_EXT_texture_filter_anisotropic tokens (not in the ES header). */
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT       0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT   0x84FF
#endif

#endif /* SRC_CLIENT_REFRESH_GL3_HEADER_EWOKOS_GL_COMPAT_H_ */
