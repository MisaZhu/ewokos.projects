/*
 * spirv_priv.h - the decoded SPIR-V module representation shared by
 * spirv_parse.c (which builds it) and spirv_interp.c (which walks it).  The
 * parser keeps the caller's word stream and indexes it: every instruction is
 * recorded as (op, word-count, pointer to its first word), and ids are resolved
 * through flat arrays sized by the module header's bound.
 */
#ifndef VULKAN_SPIRV_PRIV_H
#define VULKAN_SPIRV_PRIV_H

#include "spirv.h"

/* ---- opcodes we decode (SPIR-V 1.x core) --------------------------------- */
enum {
    OpNop = 0, OpUndef = 1,
    OpSourceContinued = 2, OpSource = 3, OpSourceExtension = 4,
    OpName = 5, OpMemberName = 6, OpString = 7, OpLine = 8,
    OpExtension = 10, OpExtInstImport = 11, OpExtInst = 12,
    OpMemoryModel = 14, OpEntryPoint = 15, OpExecutionMode = 16,
    OpExecutionModeId = 331, OpCapability = 17,
    OpTypeVoid = 19, OpTypeBool = 20, OpTypeInt = 21, OpTypeFloat = 22,
    OpTypeVector = 23, OpTypeMatrix = 24, OpTypeImage = 25, OpTypeSampler = 26,
    OpTypeSampledImage = 27, OpTypeArray = 28, OpTypeRuntimeArray = 29,
    OpTypeStruct = 30, OpTypeOpaque = 31, OpTypePointer = 32, OpTypeFunction = 33,
    OpConstantTrue = 41, OpConstantFalse = 42, OpConstant = 43,
    OpConstantComposite = 44, OpConstantNull = 46,
    OpSpecConstantTrue = 48, OpSpecConstantFalse = 49, OpSpecConstant = 50,
    OpSpecConstantComposite = 51, OpSpecConstantOp = 52,
    OpFunction = 54, OpFunctionParameter = 55, OpFunctionEnd = 56,
    OpFunctionCall = 57,
    OpVariable = 59, OpImageTexelPointer = 60, OpLoad = 61, OpStore = 62,
    OpCopyMemory = 63, OpAccessChain = 65, OpInBoundsAccessChain = 66,
    OpArrayLength = 68,
    OpDecorate = 71, OpMemberDecorate = 72, OpDecorationGroup = 73,
    OpGroupDecorate = 74, OpGroupMemberDecorate = 75,
    OpVectorExtractDynamic = 77, OpVectorInsertDynamic = 78, OpVectorShuffle = 79,
    OpCompositeConstruct = 80, OpCompositeExtract = 81, OpCompositeInsert = 82,
    OpCopyObject = 83, OpTranspose = 84,
    OpSampledImage = 86, OpImageSampleImplicitLod = 87, OpImageSampleExplicitLod = 88,
    OpImageRead = 98, OpImageWrite = 99, OpImage = 100, OpImageQuerySize = 104,
    OpConvertFToU = 109, OpConvertFToS = 110, OpConvertSToF = 111, OpConvertUToF = 112,
    OpUConvert = 113, OpSConvert = 114, OpFConvert = 115, OpBitcast = 124,
    OpSNegate = 126, OpFNegate = 127,
    OpIAdd = 128, OpFAdd = 129, OpISub = 130, OpFSub = 131,
    OpIMul = 132, OpFMul = 133, OpUDiv = 134, OpSDiv = 135, OpFDiv = 136,
    OpUMod = 137, OpSRem = 138, OpSMod = 139, OpFRem = 140, OpFMod = 141,
    OpVectorTimesScalar = 142, OpMatrixTimesScalar = 143,
    OpVectorTimesMatrix = 144, OpMatrixTimesVector = 145, OpMatrixTimesMatrix = 146,
    OpOuterProduct = 147, OpDot = 148,
    OpLogicalEqual = 164, OpLogicalNotEqual = 165, OpLogicalOr = 166,
    OpLogicalAnd = 167, OpLogicalNot = 168, OpSelect = 169,
    OpIEqual = 170, OpINotEqual = 171, OpUGreaterThan = 172, OpSGreaterThan = 173,
    OpUGreaterThanEqual = 174, OpSGreaterThanEqual = 175, OpULessThan = 176,
    OpSLessThan = 177, OpULessThanEqual = 178, OpSLessThanEqual = 179,
    OpFOrdEqual = 180, OpFUnordEqual = 181, OpFOrdNotEqual = 182,
    OpFUnordNotEqual = 183, OpFOrdLessThan = 184, OpFUnordLessThan = 185,
    OpFOrdGreaterThan = 186, OpFUnordGreaterThan = 187, OpFOrdLessThanEqual = 188,
    OpFUnordLessThanEqual = 189, OpFOrdGreaterThanEqual = 190,
    OpFUnordGreaterThanEqual = 191,
    OpShiftRightLogical = 194, OpShiftRightArithmetic = 195, OpShiftLeftLogical = 196,
    OpBitwiseOr = 197, OpBitwiseXor = 198, OpBitwiseAnd = 199, OpNot = 200,
    OpAtomicLoad = 227, OpAtomicStore = 228, OpAtomicExchange = 229,
    OpAtomicIIncrement = 232, OpAtomicIDecrement = 233,
    OpMemoryBarrier = 225, OpControlBarrier = 224,
    OpPhi = 245, OpLoopMerge = 246, OpSelectionMerge = 247, OpLabel = 248,
    OpBranch = 249, OpBranchConditional = 250, OpSwitch = 251, OpKill = 252,
    OpReturn = 253, OpReturnValue = 254, OpUnreachable = 255,
    OpTerminateInvocation = 4416
};

/* StorageClass enum values we recognise. */
enum {
    SC_UniformConstant = 0, SC_Input = 1, SC_Uniform = 2, SC_Output = 3,
    SC_Workgroup = 4, SC_CrossWorkgroup = 5, SC_Private = 6, SC_Function = 7,
    SC_Generic = 8, SC_PushConstant = 9, SC_StorageBuffer = 12
};

/* Decoration enum values we recognise. */
enum {
    DecoBuiltIn = 11, DecoBlock = 2, DecoBufferBlock = 3, DecoRowMajor = 4,
    DecoColMajor = 5, DecoArrayStride = 6, DecoMatrixStride = 7,
    DecoGLSLShared = 8, DecoNonWritable = 24, DecoNonReadable = 25,
    DecoLocation = 30, DecoBinding = 33, DecoDescriptorSet = 34,
    DecoOffset = 35, DecoFlat = 14, DecoInvariant = 22
};

/* BuiltIn enum values we recognise. */
enum {
    BiPosition = 0, BiPointSize = 1, BiClipDistance = 3, BiFragCoord = 15,
    BiFrontFacing = 17, BiNumSubgroups = 38, BiSubgroupInvocationId = 40,
    BiVertexIndex = 42, BiInstanceIndex = 43,
    BiGlobalInvocationId = 28, BiLocalInvocationId = 27, BiWorkgroupId = 26,
    BiLocalInvocationIndex = 29, BiNumWorkgroups = 24, BiWorkgroupSize = 25
};

/* ExecutionMode values. */
enum { ExecLocalSize = 17, ExecLocalSizeId = 38 };

/* GLSL.std.450 extended-instruction numbers shared by the interpreter and the
 * spirv_to_glsl transpiler. */
enum {
    G_Round = 1, G_RoundEven = 2, G_Trunc = 3, G_FAbs = 4, G_SAbs = 5,
    G_FSign = 6, G_SSign = 7, G_Floor = 8, G_Ceil = 9, G_Fract = 10,
    G_Radians = 11, G_Degrees = 12, G_Sin = 13, G_Cos = 14, G_Tan = 15,
    G_Asin = 16, G_Acos = 17, G_Atan = 18, G_Sinh = 19, G_Cosh = 20,
    G_Tanh = 21, G_Asinh = 22, G_Acosh = 23, G_Atanh = 24, G_Atan2 = 25,
    G_Pow = 26, G_Exp = 27, G_Log = 28, G_Exp2 = 29, G_Log2 = 30,
    G_Sqrt = 31, G_InverseSqrt = 32, G_Determinant = 33, G_MatrixInverse = 34,
    G_FMin = 37, G_UMin = 38, G_SMin = 39, G_FMax = 40, G_UMax = 41,
    G_SMax = 42, G_FClamp = 43, G_UClamp = 44, G_SClamp = 45, G_FMix = 46,
    G_Step = 48, G_SmoothStep = 49, G_Fma = 50, G_Length = 66,
    G_Distance = 67, G_Cross = 68, G_Normalize = 69, G_FaceForward = 70,
    G_Reflect = 71, G_Refract = 72, G_FindILsb = 73, G_FindSMsb = 75,
    G_FindUMsb = 76, G_NMin = 79, G_NMax = 80, G_NClamp = 81
};

/* value classes produced by the interpreter */
enum {
    SV_UNDEF = 0, SV_BOOL, SV_INT, SV_UINT, SV_FLOAT,
    SV_VEC,        /* vector of n scalar components of one kind */
    SV_PTR,        /* pointer: base + byte offset + pointee type */
    SV_STRUCT,     /* composite of >4 or mixed members (heap backed) */
    SV_IMAGE       /* sampled/storage image handle (binding index) */
};

typedef struct sval {
    uint32_t type;         /* spirv type id (0 = none) */
    uint8_t  klass;        /* SV_* */
    uint8_t  n;            /* component count (1..16); matrices are cols*rows */
    uint8_t  skind;        /* component kind: SV_BOOL/INT/UINT/FLOAT */
    uint8_t  sign;
    uint8_t  cols, rows;   /* matrix dimensions (0 for non-matrices) */
    uint16_t pad;
    union {
        uint32_t u[16];    /* scalar/vector/matrix component raw bits (col-major) */
        struct { void *base; int64_t off; uint32_t elem; uint32_t storage;
                 uint32_t binding; } ptr;
        void *agg;         /* SV_STRUCT heap block */
    };
} sval;

typedef struct spirv_type {
    uint32_t id;
    uint32_t op;           /* OpTypeXxx */
    uint32_t width;        /* int/float bit width */
    uint32_t is_signed;
    uint32_t comps;        /* vector component count / matrix column count */
    uint32_t elem;         /* element/pointee/base type id */
    uint32_t count;        /* array length (0 = runtime array) */
    uint32_t storage;      /* pointer storage class / image storage */
    uint32_t nmembers;     /* struct member count */
    uint32_t stride;       /* array/matrix stride (from ArrayStride/MatrixStride) */
    uint32_t is_rtarray_ptr; /* runtime array pointee (for ArrayLength) */
} spirv_type;

typedef struct spirv_const {
    uint32_t id;
    uint32_t type;
    uint32_t u[4];         /* up to vec4 of 32-bit literals */
    uint32_t n;            /* component count */
    uint32_t is_null;      /* OpConstantNull */
} spirv_const;

typedef struct spirv_deco {
    uint32_t target;
    uint32_t member;       /* 0xffffffff for non-member decorations */
    uint32_t deco;
    uint32_t args[4];
    uint32_t nargs;
} spirv_deco;

typedef struct spirv_var {
    uint32_t id;
    uint32_t type;         /* pointer type id */
    uint32_t storage;
    uint32_t init;         /* initializer constant id (0 = none) */
} spirv_var;

/* Layout note: the first three members MUST match the public spirv_entry
 * exactly (name, exec_model, local_size) because spirv_get_entry/find_entry
 * hand out a `const spirv_entry *` aliasing this struct.  `id` trails. */
typedef struct spirv_ep {
    char     name[64];
    uint32_t exec_model;
    uint32_t local_size[3];
    uint32_t id;
} spirv_ep;

typedef struct spirv_insn {
    uint32_t op;
    uint32_t wc;
    const uint32_t *w;     /* points at word0 of the instruction */
    uint32_t index;        /* position in the module instruction stream */
} spirv_insn;

typedef struct spirv_func {
    uint32_t id;
    uint32_t start;        /* insn index of OpFunction */
    uint32_t end;          /* insn index of OpFunctionEnd */
    uint32_t first_block;  /* insn index of the first OpLabel */
    uint32_t ret_type;
    uint32_t ctrl_mask;    /* 1 if the function uses workgroup/barriers */
} spirv_func;

struct spirv_module {
    const uint32_t *words;
    size_t          nwords;
    uint32_t        bound;
    uint32_t        version;

    spirv_insn *insns;  uint32_t ninsns;
    spirv_type *types;  uint32_t ntypes;
    spirv_const *consts; uint32_t nconsts;
    spirv_deco *decos;  uint32_t ndecos;
    spirv_var  *vars;   uint32_t nvars;
    spirv_ep   *eps;    uint32_t neps;
    spirv_func *funcs;  uint32_t nfuncs;

    /* id -> insn index of the instruction that defines it (labels, values,
     * variables, constants, functions, types).  Sized by bound; -1 = none. */
    int32_t   *def_at;

    /* O(1) id -> object indexes, built once at parse finalize.  The interpreter
     * resolves a result type (describe -> spirv_type_find) on nearly every
     * instruction, materializes constants per invocation, and queries
     * decorations while setting up interface/descriptor variables for every
     * fragment.  Those finders were linear scans over m->types / m->consts /
     * m->decos and dominated fragment-shading cost (~2.9 us/fragment measured
     * on the blended ImGui overlay).  NULL => index not built; the finders then
     * fall back to a linear scan, so behaviour is unchanged. */
    spirv_type  **type_at;    /* [bound] id -> &types[k] or NULL              */
    spirv_const **const_at;   /* [bound] id -> &consts[k] or NULL             */
    uint32_t   *deco_start;   /* [bound+1] target -> [start,end) in deco_order */
    uint32_t   *deco_order;   /* [ndecos] deco indices count-sorted by target  */

    uint32_t   glsl450_set;  /* ExtInstImport id for "GLSL.std.450" (0 = none) */
    int        uses_workgroup;

    /* interpreter scratch, allocated lazily on first spirv_run and reused
     * across invocations so a compute dispatch does not malloc per work-item. */
    sval      *scratch;      /* SSA value cache indexed by id (scratch_n)     */
    uint8_t   *scratch_valid;
    uint32_t   scratch_n;

    /* label id -> insn index of its OpLabel (-1 = none).  Built once at parse
     * time; the CFG walker uses it instead of def_at (which control-flow ops
     * may shadow with their target-label operands). */
    int32_t   *label_at;

    /* bump arena for per-invocation Function/Private/Input/Output variable
     * storage.  Reset (not freed) between invocations. */
    uint8_t   *arena;
    size_t     arena_cap, arena_used;
};

/* parser entry (spirv_parse.c) builds the module; interpreter (spirv_interp.c)
 * uses these lookup helpers. */
spirv_type  *spirv_type_find(spirv_module *m, uint32_t id);
spirv_const *spirv_const_find(spirv_module *m, uint32_t id);
spirv_deco  *spirv_deco_find(spirv_module *m, uint32_t target, uint32_t member,
                             uint32_t deco);
int          spirv_deco_int(spirv_module *m, uint32_t target, uint32_t member,
                            uint32_t deco, int dflt);
uint32_t     spirv_type_size(spirv_module *m, uint32_t type_id, uint32_t *stride);
spirv_func  *spirv_func_find(spirv_module *m, uint32_t id);

#endif /* VULKAN_SPIRV_PRIV_H */
