/* EwokOS freestanding stub of mesa's state_tracker/st_atom.h.
 * mesa/main/shader_types.h declares fields of type st_state_bitset, which
 * upstream is `typedef BITSET_DECLARE(st_state_bitset, ST_NUM_ATOMS)` (an
 * unsigned[] built by util/bitset.h). The GLSL frontend never reads
 * state-tracker atom bits, so any consistent word count keeps the struct
 * layout stable across TUs. mtypes.h also stores st_context as a pointer. */
#ifndef EWOKOS_PORT_ST_ATOM_H
#define EWOKOS_PORT_ST_ATOM_H
#include "util/bitset.h"
#ifndef ST_NUM_ATOMS
#define ST_NUM_ATOMS 128
#endif
typedef BITSET_DECLARE(st_state_bitset, ST_NUM_ATOMS);
struct st_context;
#endif
