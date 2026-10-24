/*
 * clc_builtin_impl.c - evaluation of the OpenCL-C builtin library for the CPU
 * interpreter.  Each builtin operates per lane; scalar arguments broadcast to
 * vector lanes.  Floating point is computed in double then narrowed to the
 * result type, which is within the subset's accuracy expectations.
 */
#include "clc.h"
#include "clc_interp.h"
#include "clc_builtins.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ---- local helpers (mirrors of the interpreter's, kept private) ---------- */
static int b_elem_size(clc_type t)
{
    clc_type s = t;
    int n;
    if (t.pointer) return (int)sizeof(void *);
    s.vec = 1;
    n = clc_type_size(&s);
    return n > 0 ? n : 1;
}
static int b_lanes(clc_type t) { return t.vec > 1 ? t.vec : 1; }

static double ad(const clc_value *v, int l)
{
    int sl = (v->type.vec > 1) ? l : 0;
    return clc_type_is_float(&v->type) ? v->f[sl] : (double)v->i[sl];
}
static long long ai(const clc_value *v, int l)
{
    int sl = (v->type.vec > 1) ? l : 0;
    return clc_type_is_float(&v->type) ? (long long)v->f[sl] : v->i[sl];
}
static long long b_mask(long long v, clc_type t)
{
    int es;
    if (clc_type_is_float(&t)) return v;
    es = b_elem_size(t);
    switch (es) {
    case 1: return clc_type_is_signed(&t)?(long long)(signed char)v:(long long)(unsigned char)v;
    case 2: return clc_type_is_signed(&t)?(long long)(short)v:(long long)(unsigned short)v;
    case 4: return clc_type_is_signed(&t)?(long long)(int)v:(long long)(unsigned int)v;
    default: return v;
    }
}
static long long b_read_int(const void *a, int size, int sg)
{
    switch (size) {
    case 1: return sg?(long long)(*(const signed char*)a):(long long)(*(const unsigned char*)a);
    case 2: return sg?(long long)(*(const short*)a):(long long)(*(const unsigned short*)a);
    case 4: return sg?(long long)(*(const int*)a):(long long)(*(const unsigned int*)a);
    case 8: return *(const long long*)a;
    default: return 0;
    }
}
static void b_write_int(void *a, int size, long long v)
{
    switch (size) {
    case 1: *(signed char*)a=(signed char)v; break;
    case 2: *(short*)a=(short)v; break;
    case 4: *(int*)a=(int)v; break;
    case 8: *(long long*)a=v; break;
    default: break;
    }
}

static void set_out_f(clc_value *o, clc_type t, int l, double r)
{
    if (clc_type_is_float(&t)) { o->f[l] = r; o->i[l] = (long long)r; }
    else { o->i[l] = b_mask((long long)r, t); o->f[l] = (double)o->i[l]; }
}
static void set_out_i(clc_value *o, clc_type t, int l, long long r)
{
    if (clc_type_is_float(&t)) { o->f[l] = (double)r; o->i[l] = r; }
    else { o->i[l] = b_mask(r, t); o->f[l] = (double)o->i[l]; }
}

typedef double (*f1_t)(double);
static double f_rsqrt(double x){ return 1.0/sqrt(x); }
static double f_exp10(double x){ return pow(10.0,x); }
static double f_degrees(double x){ return x*(180.0/M_PI); }
static double f_radians(double x){ return x*(M_PI/180.0); }
static double f_sign(double x){ return (x>0)?1.0:((x<0)?-1.0:0.0); }
static double f_round(double x){ return round(x); }
static double f_rint(double x){ return rint(x); }
/* not provided by the EwokOS libm: derived from the primitives it does have */
static double f_expm1(double x){ return exp(x)-1.0; }
static double f_log1p(double x){ return log(1.0+x); }
static double f_fdim(double x,double y){ return (x>y)?(x-y):0.0; }
static int b_signbit(double x){ return copysign(1.0,x)<0.0; }

/* portable high-word multiply (no __int128: libOpenCL also builds for 32-bit
 * arm targets whose toolchain lacks it). */
static unsigned long long mulhi64u(unsigned long long a, unsigned long long b)
{
    unsigned long long a_lo=a&0xFFFFFFFFull, a_hi=a>>32;
    unsigned long long b_lo=b&0xFFFFFFFFull, b_hi=b>>32;
    unsigned long long p0=a_lo*b_lo, p1=a_lo*b_hi, p2=a_hi*b_lo, p3=a_hi*b_hi;
    unsigned long long mid=(p0>>32)+(p1&0xFFFFFFFFull)+(p2&0xFFFFFFFFull);
    return p3+(p1>>32)+(p2>>32)+(mid>>32);
}
static long long b_mul_hi(long long x, long long y, int esz, int sg)
{
    if (esz >= 8) {
        unsigned long long ua=(unsigned long long)(x<0?-x:x);
        unsigned long long ub=(unsigned long long)(y<0?-y:y);
        unsigned long long hi=mulhi64u(ua,ub);
        if (sg && ((x<0)!=(y<0))) {
            unsigned long long lo=ua*ub;
            return (lo==0)? -(long long)hi : -(long long)hi-1;
        }
        return (long long)hi;
    }
    if (sg) return (long long)(((int64_t)(int32_t)x*(int64_t)(int32_t)y)>>32);
    return (long long)(((uint64_t)(uint32_t)x*(uint64_t)(uint32_t)y)>>32);
}

static void apply_f1(const clc_value *a, clc_value *o, clc_type t, f1_t fn)
{
    int n = b_lanes(t), l;
    o->type = t;
    for (l = 0; l < n; l++) set_out_f(o, t, l, fn(ad(a, l)));
}

typedef double (*f2_t)(double,double);
static double f_powr(double x,double y){ return pow(x,y); }
static double f_pown(double x,double y){ return pow(x,(double)(long long)y); }
static void apply_f2(const clc_value *a, const clc_value *b, clc_value *o, clc_type t, f2_t fn)
{
    int n = b_lanes(t), l;
    o->type = t;
    for (l = 0; l < n; l++) set_out_f(o, t, l, fn(ad(a,l), ad(b,l)));
}

static clc_value b_conv(clc_value v, clc_type t)
{
    clc_value o;
    int sv, dv, vf, df, l;
    memset(&o, 0, sizeof(o));
    o.type = t;
    if (t.pointer) { o.i[0] = v.i[0]; return o; }
    if (t.base == CLC_STRUCT) { o.i[0] = v.i[0]; return o; }
    sv = b_lanes(v.type); dv = b_lanes(t);
    vf = clc_type_is_float(&v.type); df = clc_type_is_float(&t);
    for (l = 0; l < dv; l++) {
        int sl = (sv==1)?0:(l<sv?l:0);
        if (df) o.f[l] = vf? v.f[sl] : (double)v.i[sl];
        else o.i[l] = b_mask(vf?(long long)v.f[sl]:v.i[sl], t);
    }
    return o;
}

/* ---- relational helpers -------------------------------------------------- */
static void rel_out(const clc_value *a, const clc_value *b, clc_value *o, clc_type t, int cmp)
{
    int n = b_lanes(t), l;
    int vec = (t.vec > 1);
    o->type = t;
    for (l = 0; l < n; l++) {
        double x = ad(a,l), y = ad(b,l);
        int r;
        switch (cmp) {
        case 0: r = (x==y); break;
        case 1: r = (x!=y); break;
        case 2: r = (x>y); break;
        case 3: r = (x>=y); break;
        case 4: r = (x<y); break;
        case 5: r = (x<=y); break;
        case 6: r = (x<y)||(x>y); break;
        default: r = 0; break;
        }
        o->i[l] = vec ? (r?-1:0) : (r?1:0);
        o->f[l] = (double)o->i[l];
    }
}

/* ---- printf -------------------------------------------------------------- */
static void emit_char(clc_exec_ctx *ctx, char c)
{
    if (ctx->printf_buf && ctx->printf_len && *ctx->printf_len + 1 < ctx->printf_cap)
        ctx->printf_buf[(*ctx->printf_len)++] = c;
    else
        fputc(c, stdout);
}
static void emit_str(clc_exec_ctx *ctx, const char *s)
{
    while (*s) emit_char(ctx, *s++);
}

static int builtin_printf(clc_exec_ctx *ctx, const clc_value *args, int nargs)
{
    const char *fmt = (const char *)(uintptr_t)args[0].i[0];
    int ai_idx = 1;
    if (!fmt) return 0;
    while (*fmt) {
        if (*fmt != '%') { emit_char(ctx, *fmt++); continue; }
        fmt++;
        {
            char spec[64]; int si = 0; char conv;
            char tmp[128];
            spec[si++] = '%';
            while (*fmt && (strchr("-+ #0123456789.", *fmt))) spec[si++] = *fmt++;
            conv = *fmt;
            if (!conv) break;
            fmt++;
            if (si > 60) si = 60;
            switch (conv) {
            case '%': emit_char(ctx,'%'); break;
            case 'c': {
                spec[si++]='c'; spec[si]=0;
                snprintf(tmp,sizeof tmp,spec,(int)(ai_idx<nargs?ai(&args[ai_idx],0):0));
                ai_idx++; emit_str(ctx,tmp); break;
            }
            case 's': {
                const char *sp = (ai_idx<nargs)?(const char*)(uintptr_t)args[ai_idx].i[0]:"";
                spec[si++]='s'; spec[si]=0;
                snprintf(tmp,sizeof tmp,spec,sp?sp:"");
                ai_idx++; emit_str(ctx,tmp); break;
            }
            case 'f': case 'e': case 'g': case 'E': case 'G': {
                double d = (ai_idx<nargs)?ad(&args[ai_idx],0):0.0;
                spec[si++]=conv; spec[si]=0;
                snprintf(tmp,sizeof tmp,spec,d);
                ai_idx++; emit_str(ctx,tmp); break;
            }
            case 'x': case 'X': case 'u': {
                unsigned long long u = (ai_idx<nargs)?(unsigned long long)ai(&args[ai_idx],0):0;
                spec[si++]=conv; spec[si]=0;
                if (conv=='u') snprintf(tmp,sizeof tmp,spec,(unsigned)u);
                else snprintf(tmp,sizeof tmp,spec,(unsigned)u);
                ai_idx++; emit_str(ctx,tmp); break;
            }
            default: { /* d,i,l,p and anything else -> integer */
                long long iv = (ai_idx<nargs)?ai(&args[ai_idx],0):0;
                spec[si++]=(conv=='l')?'d':conv; spec[si]=0;
                if (conv=='l') snprintf(tmp,sizeof tmp,"%lld",iv);
                else snprintf(tmp,sizeof tmp,spec,(int)iv);
                ai_idx++; emit_str(ctx,tmp); break;
            }
            }
        }
    }
    return 0;
}

/* ---- main dispatch ------------------------------------------------------- */
int clc_exec_builtin(int id, const clc_value *args, int nargs, clc_value *out,
                     clc_exec_ctx *ctx, clc_type out_type, const char *name)
{
    clc_type t = out_type;
    int n = b_lanes(t), l;
    const clc_value *A = (nargs>0)?&args[0]:NULL;
    const clc_value *B = (nargs>1)?&args[1]:NULL;
    const clc_value *C = (nargs>2)?&args[2]:NULL;
    out->type = t;
    (void)name;

    switch (id) {
    /* ---- work-item ---- */
    case BI_GET_GLOBAL_ID:   out->type=(clc_type){CLC_SIZE_T,1,0,0,NULL}; out->i[0]=(long long)ctx->global_id[ai(A,0)&3]; return 0;
    case BI_GET_GLOBAL_SIZE: out->type=(clc_type){CLC_SIZE_T,1,0,0,NULL}; out->i[0]=(long long)ctx->global_size[ai(A,0)&3]; return 0;
    case BI_GET_GLOBAL_OFFSET:out->type=(clc_type){CLC_SIZE_T,1,0,0,NULL};out->i[0]=(long long)ctx->global_offset[ai(A,0)&3]; return 0;
    case BI_GET_LOCAL_ID:    out->type=(clc_type){CLC_SIZE_T,1,0,0,NULL}; out->i[0]=(long long)ctx->local_id[ai(A,0)&3]; return 0;
    case BI_GET_LOCAL_SIZE:  out->type=(clc_type){CLC_SIZE_T,1,0,0,NULL}; out->i[0]=(long long)ctx->local_size[ai(A,0)&3]; return 0;
    case BI_GET_GROUP_ID:    out->type=(clc_type){CLC_SIZE_T,1,0,0,NULL}; out->i[0]=(long long)ctx->group_id[ai(A,0)&3]; return 0;
    case BI_GET_NUM_GROUPS:  out->type=(clc_type){CLC_SIZE_T,1,0,0,NULL}; out->i[0]=(long long)ctx->num_groups[ai(A,0)&3]; return 0;
    case BI_GET_WORK_DIM:    out->type=(clc_type){CLC_INT,1,0,0,NULL};    out->i[0]=(long long)ctx->work_dim; return 0;
    case BI_BARRIER:         out->type=(clc_type){CLC_VOID,1,0,0,NULL};   return 0; /* serial: no-op fence */

    /* ---- float unary ---- */
    case BI_SQRT:   apply_f1(A,out,t,sqrt); return 0;
    case BI_RSQRT:  apply_f1(A,out,t,f_rsqrt); return 0;
    case BI_FABS:   apply_f1(A,out,t,fabs); return 0;
    case BI_FLOOR:  apply_f1(A,out,t,floor); return 0;
    case BI_CEIL:   apply_f1(A,out,t,ceil); return 0;
    case BI_ROUND:  apply_f1(A,out,t,f_round); return 0;
    case BI_TRUNC:  apply_f1(A,out,t,trunc); return 0;
    case BI_RINT:   apply_f1(A,out,t,f_rint); return 0;
    case BI_EXP:    apply_f1(A,out,t,exp); return 0;
    case BI_EXP2:   apply_f1(A,out,t,exp2); return 0;
    case BI_EXP10:  apply_f1(A,out,t,f_exp10); return 0;
    case BI_EXPM1:  apply_f1(A,out,t,f_expm1); return 0;
    case BI_LOG:    apply_f1(A,out,t,log); return 0;
    case BI_LOG2:   apply_f1(A,out,t,log2); return 0;
    case BI_LOG10:  apply_f1(A,out,t,log10); return 0;
    case BI_LOG1P:  apply_f1(A,out,t,f_log1p); return 0;
    case BI_SIN:    apply_f1(A,out,t,sin); return 0;
    case BI_COS:    apply_f1(A,out,t,cos); return 0;
    case BI_TAN:    apply_f1(A,out,t,tan); return 0;
    case BI_ASIN:   apply_f1(A,out,t,asin); return 0;
    case BI_ACOS:   apply_f1(A,out,t,acos); return 0;
    case BI_ATAN:   apply_f1(A,out,t,atan); return 0;
    case BI_SINH:   apply_f1(A,out,t,sinh); return 0;
    case BI_COSH:   apply_f1(A,out,t,cosh); return 0;
    case BI_TANH:   apply_f1(A,out,t,tanh); return 0;
    case BI_ASINH:  apply_f1(A,out,t,asinh); return 0;
    case BI_ACOSH:  apply_f1(A,out,t,acosh); return 0;
    case BI_ATANH:  apply_f1(A,out,t,atanh); return 0;
    case BI_CBRT:   apply_f1(A,out,t,cbrt); return 0;
    case BI_DEGREES:apply_f1(A,out,t,f_degrees); return 0;
    case BI_RADIANS:apply_f1(A,out,t,f_radians); return 0;
    case BI_SIGN:   apply_f1(A,out,t,f_sign); return 0;

    /* ---- float binary ---- */
    case BI_POW:    apply_f2(A,B,out,t,pow); return 0;
    case BI_POWR:   apply_f2(A,B,out,t,f_powr); return 0;
    case BI_POWN:   apply_f2(A,B,out,t,f_pown); return 0;
    case BI_ATAN2:  apply_f2(A,B,out,t,atan2); return 0;
    case BI_FMIN:   apply_f2(A,B,out,t,fmin); return 0;
    case BI_FMAX:   apply_f2(A,B,out,t,fmax); return 0;
    case BI_HYPOT:  apply_f2(A,B,out,t,hypot); return 0;
    case BI_COPYSIGN:apply_f2(A,B,out,t,copysign); return 0;
    case BI_FDIM:   apply_f2(A,B,out,t,f_fdim); return 0;

    /* ---- float ternary ---- */
    case BI_MAD: case BI_FMA:
        for (l=0;l<n;l++) set_out_f(out,t,l, ad(A,l)*ad(B,l)+ad(C,l));
        return 0;
    case BI_MIX:
        for (l=0;l<n;l++) { double x=ad(A,l),y=ad(B,l),z=ad(C,l); set_out_f(out,t,l, x+(y-x)*z); }
        return 0;
    case BI_SMOOTHSTEP: {
        for (l=0;l<n;l++) {
            double e0=ad(A,l),e1=ad(B,l),x=ad(C,l);
            double u=(e1==e0)?0.0:(x-e0)/(e1-e0);
            u = u<0?0:(u>1?1:u);
            set_out_f(out,t,l, u*u*(3.0-2.0*u));
        }
        return 0;
    }
    case BI_SINCOS: {
        /* sincos(x, __global float* s, __global float* c) */
        uintptr_t sp=(uintptr_t)B->i[0], cp=(uintptr_t)C->i[0];
        int es=b_elem_size(A->type);
        for (l=0;l<b_lanes(A->type);l++) {
            double x=ad(A,l);
            if (es==8){ ((double*)sp)[l]=sin(x); ((double*)cp)[l]=cos(x); }
            else { ((float*)sp)[l]=(float)sin(x); ((float*)cp)[l]=(float)cos(x); }
        }
        out->type=(clc_type){CLC_VOID,1,0,0,NULL};
        return 0;
    }

    /* ---- min/max/clamp (float or int) ---- */
    case BI_MIN: case BI_MAX:
        if (clc_type_is_float(&t)) { for(l=0;l<n;l++){ double x=ad(A,l),y=ad(B,l); set_out_f(out,t,l, id==BI_MIN?(x<y?x:y):(x>y?x:y)); } }
        else { for(l=0;l<n;l++){ long long x=ai(A,l),y=ai(B,l); set_out_i(out,t,l, id==BI_MIN?(x<y?x:y):(x>y?x:y)); } }
        return 0;
    case BI_CLAMP:
        if (clc_type_is_float(&t)) { for(l=0;l<n;l++){ double x=ad(A,l),lo=ad(B,l),hi=ad(C,l); if(x<lo)x=lo; if(x>hi)x=hi; set_out_f(out,t,l,x);} }
        else { for(l=0;l<n;l++){ long long x=ai(A,l),lo=ai(B,l),hi=ai(C,l); if(x<lo)x=lo; if(x>hi)x=hi; set_out_i(out,t,l,x);} }
        return 0;
    case BI_STEP:
        for(l=0;l<n;l++){ double e=ad(A,l),x=ad(B,l); set_out_f(out,t,l, x<e?0.0:1.0); }
        return 0;

    /* ---- integer ops ---- */
    case BI_ABS:
        for(l=0;l<n;l++){ long long x=ai(A,l); set_out_i(out,t,l, x<0?-x:x); }
        return 0;
    case BI_ABS_DIFF:
        for(l=0;l<n;l++){ long long x=ai(A,l),y=ai(B,l); set_out_i(out,t,l, x>y?x-y:y-x); }
        return 0;
    case BI_MUL_HI:
        for(l=0;l<n;l++){ long long x=ai(A,l),y=ai(B,l); set_out_i(out,t,l,b_mul_hi(x,y,b_elem_size(t),clc_type_is_signed(&t))); }
        return 0;
    case BI_MAD_HI:
        for(l=0;l<n;l++){ long long x=ai(A,l),y=ai(B,l),z=ai(C,l); set_out_i(out,t,l,b_mul_hi(x,y,b_elem_size(t),clc_type_is_signed(&t))+z); }
        return 0;
    case BI_MAD24:
        for(l=0;l<n;l++){ long long x=ai(A,l)&0xFFFFFF,y=ai(B,l)&0xFFFFFF,z=ai(C,l); set_out_i(out,t,l,x*y+z); }
        return 0;
    case BI_MUL24:
        for(l=0;l<n;l++){ long long x=ai(A,l)&0xFFFFFF,y=ai(B,l)&0xFFFFFF; set_out_i(out,t,l,x*y); }
        return 0;
    case BI_ROTATE:
        for(l=0;l<n;l++){ int bits=b_elem_size(t)*8; unsigned long long v=(unsigned long long)ai(A,l); int k=(int)(ai(B,l)&(bits-1)); set_out_i(out,t,l,(long long)((v<<k)|(v>>(bits-k)))); }
        return 0;
    case BI_ADD_SAT: case BI_SUB_SAT: case BI_MAD_SAT: {
        for(l=0;l<n;l++){
            long long x=ai(A,l), y=(id==BI_MAD_SAT)? (ai(A,l)*ai(B,l)) : ai(B,l);
            long long z = (id==BI_ADD_SAT||id==BI_MAD_SAT)? x+y : x-y;
            if (id==BI_MAD_SAT) z = ai(A,l)*ai(B,l)+ai(C,l);
            if (clc_type_is_signed(&t)) {
                long long mx=(t.base==CLC_LONG)?0x7FFFFFFFFFFFFFFFLL:0x7FFFFFFFLL;
                long long mn=(t.base==CLC_LONG)?(-0x7FFFFFFFFFFFFFFFLL-1):(-0x7FFFFFFFLL-1);
                if(z>mx)z=mx;
                if(z<mn)z=mn;
            } else {
                unsigned long long mx=(t.base==CLC_ULONG)?0xFFFFFFFFFFFFFFFFULL:0xFFFFFFFFULL;
                if((unsigned long long)z>mx)z=(long long)mx;
            }
            set_out_i(out,t,l,z);
        }
        return 0;
    }
    case BI_CLZ:
        for(l=0;l<n;l++){ unsigned long long v=(unsigned long long)ai(A,l); int bits=b_elem_size(t)*8; int c=0; if(v==0)c=bits; else { while(!(v&((unsigned long long)1<<(bits-1)))) { v<<=1; c++; } } set_out_i(out,t,l,c); }
        return 0;
    case BI_POPCOUNT:
        for(l=0;l<n;l++){ unsigned long long v=(unsigned long long)ai(A,l); int c=0; while(v){c+=v&1;v>>=1;} set_out_i(out,t,l,c); }
        return 0;
    case BI_UPSAMPLE:
        for(l=0;l<n;l++){ long long hi=ai(A,l), lo=ai(B,l); set_out_i(out,t,l,(hi<<32)|(lo&0xFFFFFFFFLL)); }
        return 0;

    /* ---- geometric ---- */
    case BI_DOT: {
        double s=0; int k; for(k=0;k<b_lanes(A->type);k++) s+=ad(A,k)*ad(B,k);
        out->type=(clc_type){(A->type.base==CLC_DOUBLE?CLC_DOUBLE:CLC_FLOAT),1,0,0,NULL};
        out->f[0]=s; out->i[0]=(long long)s; return 0;
    }
    case BI_LENGTH: case BI_FAST_LENGTH: {
        double s=0; int k; for(k=0;k<b_lanes(A->type);k++){double x=ad(A,k);s+=x*x;} s=sqrt(s);
        out->type=(clc_type){(A->type.base==CLC_DOUBLE?CLC_DOUBLE:CLC_FLOAT),1,0,0,NULL};
        out->f[0]=s; out->i[0]=(long long)s; return 0;
    }
    case BI_DISTANCE: case BI_FAST_DISTANCE: {
        double s=0; int k; for(k=0;k<b_lanes(A->type);k++){double d=ad(A,k)-ad(B,k);s+=d*d;} s=sqrt(s);
        out->type=(clc_type){(A->type.base==CLC_DOUBLE?CLC_DOUBLE:CLC_FLOAT),1,0,0,NULL};
        out->f[0]=s; out->i[0]=(long long)s; return 0;
    }
    case BI_NORMALIZE: case BI_FAST_NORMALIZE: {
        double s=0; int k; for(k=0;k<n;k++){double x=ad(A,k);s+=x*x;} s=sqrt(s); if(s==0)s=1;
        out->type=t; for(k=0;k<n;k++) set_out_f(out,t,k, ad(A,k)/s); return 0;
    }
    case BI_CROSS: {
        double x0=ad(A,0),x1=ad(A,1),x2=ad(A,2);
        double y0=ad(B,0),y1=ad(B,1),y2=ad(B,2);
        out->type=t;
        set_out_f(out,t,0, x1*y2-x2*y1);
        set_out_f(out,t,1, x2*y0-x0*y2);
        set_out_f(out,t,2, x0*y1-x1*y0);
        return 0;
    }

    /* ---- relational ---- */
    case BI_ISEQUAL:        rel_out(A,B,out,t,0); return 0;
    case BI_ISNOTEQUAL:     rel_out(A,B,out,t,1); return 0;
    case BI_ISGREATER:      rel_out(A,B,out,t,2); return 0;
    case BI_ISGREATEREQUAL: rel_out(A,B,out,t,3); return 0;
    case BI_ISLESS:         rel_out(A,B,out,t,4); return 0;
    case BI_ISLESSEQUAL:    rel_out(A,B,out,t,5); return 0;
    case BI_ISLESSGREATER:  rel_out(A,B,out,t,6); return 0;
    case BI_ISFINITE: case BI_ISINF: case BI_ISNAN: case BI_ISNORMAL:
    case BI_ISORDERED: case BI_ISUNORDERED: case BI_SIGNBIT: {
        int vec=(t.vec>1);
        for(l=0;l<n;l++){
            double x=ad(A,l); int r;
            switch(id){
            case BI_ISFINITE: r=isfinite(x); break;
            case BI_ISINF:    r=isinf(x); break;
            case BI_ISNAN:    r=isnan(x); break;
            case BI_ISNORMAL: r=(x!=0 && isfinite(x)); break;
            case BI_ISORDERED:r=!(isnan(x)||isnan(ad(B,l))); break;
            case BI_ISUNORDERED:r=(isnan(x)||isnan(ad(B,l))); break;
            case BI_SIGNBIT:  r=b_signbit(x); break;
            default: r=0; break;
            }
            out->i[l]=vec?(r?-1:0):(r?1:0); out->f[l]=(double)out->i[l];
        }
        return 0;
    }
    case BI_ANY: case BI_ALL: {
        int k, cnt=0, tot=b_lanes(A->type);
        for(k=0;k<tot;k++) if(ai(A,k)!=0) cnt++;
        { int r = (id==BI_ANY)? (cnt>0) : (cnt==tot); out->type=(clc_type){CLC_INT,1,0,0,NULL}; out->i[0]=r; out->f[0]=r; }
        return 0;
    }
    case BI_SELECT: {
        const clc_value *cc = C;
        int vec=(A->type.vec>1);
        out->type=A->type;
        for(l=0;l<b_lanes(A->type);l++){
            long long cv=ai(cc,l);
            int pick = vec ? ((cv<0)?1:0) : ((cv!=0)?1:0);
            if(clc_type_is_float(&A->type)) set_out_f(out,A->type,l, pick?ad(B,l):ad(A,l));
            else set_out_i(out,A->type,l, pick?ai(B,l):ai(A,l));
        }
        return 0;
    }
    case BI_BITSELECT: {
        out->type=A->type;
        for(l=0;l<b_lanes(A->type);l++){
            unsigned long long av=(unsigned long long)ai(A,l),bv=(unsigned long long)ai(B,l),cv=(unsigned long long)ai(C,l);
            unsigned long long r=(av & ~cv)|(bv & cv);
            set_out_i(out,A->type,l,(long long)r);
        }
        return 0;
    }

    /* ---- conversions ---- */
    case BI_CONVERT:
        *out = b_conv(*A, t);
        return 0;
    case BI_AS: {
        int es=b_elem_size(t);
        int lanes=b_lanes(t);
        out->type=t;
        for(l=0;l<lanes;l++){
            int sl=(A->type.vec>1)?l:0;
            int sf=clc_type_is_float(&A->type), df=clc_type_is_float(&t);
            if(sf && !df){
                if(es==8){ double d=A->f[sl]; unsigned long long u; memcpy(&u,&d,8); out->i[l]=(long long)u; }
                else { float f=(float)A->f[sl]; unsigned int u; memcpy(&u,&f,4); out->i[l]=(int)u; }
            } else if(!sf && df){
                if(es==8){ unsigned long long u=(unsigned long long)A->i[sl]; double d; memcpy(&d,&u,8); out->f[l]=d; }
                else { unsigned int u=(unsigned int)A->i[sl]; float f; memcpy(&f,&u,4); out->f[l]=f; }
            } else {
                if(df) out->f[l]=A->f[sl]; else out->i[l]=A->i[sl];
            }
            out->f[l]= df? out->f[l] : (double)out->i[l];
            if(!df) out->f[l]=(double)out->i[l];
        }
        return 0;
    }
    case BI_VLOAD: {
        long long off=ai(A,0);
        uintptr_t base=(uintptr_t)B->i[0];
        int es=b_elem_size(t);
        const char *p=(const char*)(base + (uintptr_t)(off*n*es));
        out->type=t;
        for(l=0;l<n;l++){
            if(clc_type_is_float(&t)) { out->f[l]=(t.base==CLC_DOUBLE)?((const double*)p)[l]:((const float*)p)[l]; out->i[l]=(long long)out->f[l]; }
            else out->i[l]=b_read_int(p+l*es, es, clc_type_is_signed(&t));
        }
        return 0;
    }
    case BI_VSTORE: {
        const clc_value *data=&args[0];
        long long off=ai(&args[1],0);
        uintptr_t base=(uintptr_t)args[2].i[0];
        int dn=b_lanes(data->type);
        int es=b_elem_size(data->type);
        char *p=(char*)(base + (uintptr_t)(off*dn*es));
        for(l=0;l<dn;l++){
            if(clc_type_is_float(&data->type)){ if(data->type.base==CLC_DOUBLE)((double*)p)[l]=data->f[l]; else ((float*)p)[l]=(float)data->f[l]; }
            else b_write_int(p+l*es, es, data->i[l]);
        }
        out->type=(clc_type){CLC_VOID,1,0,0,NULL};
        return 0;
    }
    case BI_PRINTF:
        out->type=(clc_type){CLC_INT,1,0,0,NULL};
        out->i[0]=builtin_printf(ctx,args,nargs);
        return 0;

    default:
        out->type=t;
        return 0;
    }
}
