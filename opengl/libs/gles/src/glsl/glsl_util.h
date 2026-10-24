/* glsl_util.h - shared type helpers for the GLSL interpreter (header-only). */
#ifndef GLES_GLSL_UTIL_H
#define GLES_GLSL_UTIL_H

#include "glsl.h"

/* Number of cells occupied by one element of a non-array, non-struct type. */
static inline int glsl_base_comps(int type)
{
    switch (type) {
    case T_BOOL: case T_INT: case T_UINT: case T_FLOAT: case T_SAMPLER2D: case T_SAMPLERCUBE:
        return 1;
    case T_BVEC2: case T_IVEC2: case T_UVEC2: case T_VEC2: return 2;
    case T_BVEC3: case T_IVEC3: case T_UVEC3: case T_VEC3: return 3;
    case T_BVEC4: case T_IVEC4: case T_UVEC4: case T_VEC4: return 4;
    case T_MAT2: return 4;
    case T_MAT3: return 9;
    case T_MAT4: return 16;
    case T_VOID: return 0;
    default: return 1;
    }
}

/* Cells for one element of `type` (structs resolved via the stage). */
static inline int glsl_type_cells(const glsl_stage *st, int type, int st_id)
{
    if (type == T_STRUCT && st && st_id >= 0 && st_id < st->nstructs)
        return st->structs[st_id].total_floats;
    return glsl_base_comps(type);
}

/* Total cells for a variable (element cells * array length). */
static inline int glsl_var_cells(const glsl_stage *st, int type, int st_id, int array_len)
{
    int c = glsl_type_cells(st, type, st_id);
    return array_len > 0 ? c * array_len : c;
}

static inline int glsl_is_int_family(int type)
{
    switch (type) {
    case T_BOOL: case T_BVEC2: case T_BVEC3: case T_BVEC4:
    case T_INT:  case T_IVEC2: case T_IVEC3: case T_IVEC4:
    case T_UINT: case T_UVEC2: case T_UVEC3: case T_UVEC4:
    case T_SAMPLER2D: case T_SAMPLERCUBE:
        return 1;
    default:
        return 0;
    }
}

static inline int glsl_is_uint(int type)
{
    return type == T_UINT || type == T_UVEC2 || type == T_UVEC3 || type == T_UVEC4;
}

static inline int glsl_is_matrix(int type)
{
    return type == T_MAT2 || type == T_MAT3 || type == T_MAT4;
}

/* vector component count for a vector or scalar type (matrices handled apart) */
static inline int glsl_vec_len(int type)
{
    switch (type) {
    case T_VEC2: case T_IVEC2: case T_UVEC2: case T_BVEC2: return 2;
    case T_VEC3: case T_IVEC3: case T_UVEC3: case T_BVEC3: return 3;
    case T_VEC4: case T_IVEC4: case T_UVEC4: case T_BVEC4: return 4;
    default: return 1;
    }
}

/* float/int/bool vector type of a given component length (1 => scalar) */
static inline int glsl_float_type_of_len(int n)
{
    switch (n) { case 1: return T_FLOAT; case 2: return T_VEC2; case 3: return T_VEC3; default: return T_VEC4; }
}
static inline int glsl_int_type_of_len(int n)
{
    switch (n) { case 1: return T_INT; case 2: return T_IVEC2; case 3: return T_IVEC3; default: return T_IVEC4; }
}
static inline int glsl_uint_type_of_len(int n)
{
    switch (n) { case 1: return T_UINT; case 2: return T_UVEC2; case 3: return T_UVEC3; default: return T_UVEC4; }
}
static inline int glsl_bool_type_of_len(int n)
{
    switch (n) { case 1: return T_BOOL; case 2: return T_BVEC2; case 3: return T_BVEC3; default: return T_BVEC4; }
}

#endif /* GLES_GLSL_UTIL_H */
