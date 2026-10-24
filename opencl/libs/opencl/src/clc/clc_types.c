/*
 * clc_types.c - type-system helpers for the OpenCL-C subset: element size,
 * alignment, canonical spelling and the float/signed/pointer/vector
 * predicates the interpreter and parser rely on.  Sizes/alignments follow the
 * OpenCL C ABI (3-element vectors round to four lanes; vectors align to their
 * size capped at 16 bytes).
 */
#include "clc.h"
#include <stdio.h>

static int base_size(clc_base b)
{
    switch (b) {
    case CLC_VOID:      return 0;
    case CLC_BOOL:
    case CLC_CHAR:
    case CLC_UCHAR:     return 1;
    case CLC_SHORT:
    case CLC_USHORT:
    case CLC_HALF:      return 2;
    case CLC_INT:
    case CLC_UINT:
    case CLC_FLOAT:     return 4;
    case CLC_LONG:
    case CLC_ULONG:
    case CLC_DOUBLE:    return 8;
    case CLC_SIZE_T:
    case CLC_PTRDIFF_T: return (int)sizeof(size_t);
    case CLC_SAMPLER_T:
    case CLC_IMAGE2D_T:
    case CLC_IMAGE3D_T: return (int)sizeof(void *);
    default:            return 0;
    }
}

int clc_type_size(const clc_type *t)
{
    int bs, lanes;
    if (t->pointer)
        return (int)sizeof(void *);
    if (t->base == CLC_STRUCT)
        return t->str ? t->str->size : 0;
    bs = base_size(t->base);
    lanes = t->vec;
    if (lanes == 3) lanes = 4;
    if (lanes < 1) lanes = 1;
    return bs * lanes;
}

int clc_type_align(const clc_type *t)
{
    int sz = clc_type_size(t);
    if (t->pointer)
        return (int)sizeof(void *);
    if (t->vec > 1) {
        int a = sz;
        if (a > 16) a = 16;
        return a ? a : 1;
    }
    return sz ? sz : 1;
}

int clc_type_is_float(const clc_type *t)
{
    return t->base == CLC_FLOAT || t->base == CLC_DOUBLE || t->base == CLC_HALF;
}

int clc_type_is_signed(const clc_type *t)
{
    switch (t->base) {
    case CLC_CHAR:
    case CLC_SHORT:
    case CLC_INT:
    case CLC_LONG:
    case CLC_PTRDIFF_T:
        return 1;
    default:
        return 0;
    }
}

int clc_type_is_pointer(const clc_type *t)
{
    return t->pointer || t->base == CLC_SAMPLER_T ||
           t->base == CLC_IMAGE2D_T || t->base == CLC_IMAGE3D_T;
}

int clc_type_is_vector(const clc_type *t)
{
    return !t->pointer && t->vec > 1;
}

static const char *base_name(clc_base b)
{
    switch (b) {
    case CLC_VOID:      return "void";
    case CLC_BOOL:      return "bool";
    case CLC_CHAR:      return "char";
    case CLC_UCHAR:     return "uchar";
    case CLC_SHORT:     return "short";
    case CLC_USHORT:    return "ushort";
    case CLC_INT:       return "int";
    case CLC_UINT:      return "uint";
    case CLC_LONG:      return "long";
    case CLC_ULONG:     return "ulong";
    case CLC_FLOAT:     return "float";
    case CLC_DOUBLE:    return "double";
    case CLC_HALF:      return "half";
    case CLC_SIZE_T:    return "size_t";
    case CLC_PTRDIFF_T: return "ptrdiff_t";
    case CLC_SAMPLER_T: return "sampler_t";
    case CLC_IMAGE2D_T: return "image2d_t";
    case CLC_IMAGE3D_T: return "image3d_t";
    default:            return "struct";
    }
}

const char *clc_type_name(const clc_type *t)
{
    static char buf[96];
    const char *bn;
    if (t->base == CLC_STRUCT && t->str && t->str->name)
        bn = t->str->name;
    else
        bn = base_name(t->base);

    if (t->pointer) {
        const char *q = "";
        if (t->as == CLC_AS_GLOBAL) q = "";
        snprintf(buf, sizeof(buf), "%s%s*", bn, q);
    } else if (t->vec > 1) {
        snprintf(buf, sizeof(buf), "%s%d", bn, t->vec);
    } else {
        snprintf(buf, sizeof(buf), "%s", bn);
    }
    return buf;
}
