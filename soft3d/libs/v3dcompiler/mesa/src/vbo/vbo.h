/* EwokOS freestanding stub of gallium's vbo/vbo.h (see VENDORED.txt).
 * mesa/main/mtypes.h defines `struct vbo_context` embedding vbo_exec_context
 * and vbo_save_context BY VALUE. The GLSL->NIR frontend never touches the VBO
 * (vertex-buffer-object) execution/save state, so opaque complete types keep
 * gl_context's layout consistent without vendoring the whole VBO/TNL stack.
 * Sizes are arbitrary but MUST stay identical across all TUs (single header). */
#ifndef EWOKOS_PORT_VBO_H
#define EWOKOS_PORT_VBO_H
/* mesa/main/mtypes.h sizes vbo_context.current[] by VBO_ATTRIB_MAX (the
 * terminator of mesa's src/mesa/vbo/vbo_attrib.h enum = 46 at this pin). The
 * GLSL frontend never reads that array, but the constant must exist and be
 * identical across TUs so gl_context's layout stays consistent. */
#define VBO_ATTRIB_MAX 46
struct vbo_exec_context { unsigned _opaque[512]; };
struct vbo_save_context { unsigned _opaque[256]; };
#endif
