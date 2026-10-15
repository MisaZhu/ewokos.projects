/* EwokOS freestanding stub of gallium's frontend/api.h. The state-tracker
 * frontend interface is only referenced through pointers by mtypes.h and is
 * unused by the GLSL->NIR compiler frontend. */
#ifndef EWOKOS_PORT_FRONTEND_API_H
#define EWOKOS_PORT_FRONTEND_API_H
/* mesa/main/mtypes.h sizes an array by ST_ATTACHMENT_COUNT and stores
 * st_context / st_framebuffer_iface / st_visual as pointers. st_attachment_type
 * is copied verbatim from the pinned src/gallium/include/frontend/api.h; the
 * rest of the state-tracker frontend interface is unused by the compiler. */
enum st_attachment_type {
   ST_ATTACHMENT_FRONT_LEFT,
   ST_ATTACHMENT_BACK_LEFT,
   ST_ATTACHMENT_FRONT_RIGHT,
   ST_ATTACHMENT_BACK_RIGHT,
   ST_ATTACHMENT_DEPTH_STENCIL,
   ST_ATTACHMENT_ACCUM,

   ST_ATTACHMENT_COUNT,
   ST_ATTACHMENT_INVALID = -1
};
struct st_visual;
struct st_framebuffer_iface;
struct st_context;
#endif
