/* EwokOS freestanding MINIMAL stub of gallium's auxiliary util/u_helpers.h
 * (upstream real path: src/gallium/auxiliary/util/u_helpers.h, resolved there
 * via -I src/gallium/auxiliary). The v3d compiler back half
 * (broadcom/compiler/nir_to_vir.c) includes "util/u_helpers.h" but only ever
 * calls the self-contained inline util_varying_is_point_coord(); every other
 * u_helpers API is gallium pipe/draw/upload code the compiler never touches.
 *
 * So we provide just that one inline (copied verbatim from the pinned upstream
 * header f45825a0) and deliberately omit the pipe/p_state.h + c11/threads.h
 * dependent declarations, which would otherwise cascade a large gallium
 * auxiliary vendoring effort into this freestanding port. This is NOT the full
 * upstream header and must not be used outside libv3dcompiler.a. */
#ifndef EWOKOS_PORT_UTIL_U_HELPERS_H
#define EWOKOS_PORT_UTIL_U_HELPERS_H

#include <stdint.h>
#include <stdbool.h>
#include "compiler/shader_enums.h"

/* Helper function to determine if the varying should contain the point
 * coordinates, given the sprite_coord_enable mask.  Requires
 * pipe_caps.tgsi_texcoord to be enabled.
 */
static inline bool
util_varying_is_point_coord(gl_varying_slot slot, uint32_t sprite_coord_enable)
{
   if (slot == VARYING_SLOT_PNTC)
      return true;

   if (slot >= VARYING_SLOT_TEX0 && slot <= VARYING_SLOT_TEX7 &&
       (sprite_coord_enable & (1 << (slot - VARYING_SLOT_TEX0)))) {
      return true;
   }

   return false;
}

#endif /* EWOKOS_PORT_UTIL_U_HELPERS_H */
