/* gles2_shader.c - the GLES2 shader / program / uniform object model.
 *
 * This TU sees ONLY the standard Khronos headers plus the portablegl-free
 * interpreter API (src/glsl/glsl.h) and the neutral engine bridge
 * (gles_engine.h).  It owns:
 *   - shader objects   : source text + a compiled glsl_stage
 *   - program objects  : a linked glsl_program, its uniform block (glsl_cell[])
 *                        and the portablegl program id returned by the engine
 *   - the current program (glUseProgram) that uniform setters write into
 *
 * Linking consumes the two attached stages (glsl_link takes ownership), creates
 * the engine-side interpreted portablegl program, and allocates the uniform
 * block the interpreter reads each vertex/pixel.
 */
#include <GLES2/gl2.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <ewoksys/klog.h>

/* TEMPORARY compile-phase wall-clock probe.  /tmp does not persist on this
 * raspi5 image, so the file sink is dead; slog -> /dev/log is the reliable
 * channel (the in-process mesa FEDBG markers prove it works).  city's faithful
 * port stalls >100s and glLinkProgram runs the CPU-interpreter glsl_link()
 * before the GPU bridge, so this brackets which of the two links burns the
 * wall time.  Remove once the hotspot is fixed. */
static double v3ct_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}
static void v3ct_log(const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    slog("v3ct: [%8.1f] %s\r\n", v3ct_now(), buf);
}

#include "../engine/gles_engine.h"
#include "../glsl/glsl.h"
#include "../glsl/glsl_util.h"

#define MAX_SHADERS  64
#define MAX_PROGRAMS 64
#define LOG_LEN      512
#define MAX_PENDING_BINDS 8

typedef struct {
    GLuint      handle;
    GLenum      type;
    char       *src;
    glsl_stage *stage;
    GLint       compile_status;
    GLint       delete_flag;
    char        log[LOG_LEN];
    int         in_use;
} shader_obj;

typedef struct {
    GLuint        handle;
    GLuint        vs_shader, fs_shader;
    glsl_program *gp;
    GLuint        pgl_id;
    GLint         link_status;
    GLint         validate_status;
    GLint         delete_flag;
    glsl_cell    *ublock;
    int           ublock_cells;
    char          log[LOG_LEN];
    char          validate_log[LOG_LEN];
    int           in_use;
    /* attribute-location binds requested before link */
    char          bind_name[MAX_PENDING_BINDS][GLSL_NAME_LEN];
    int           bind_loc[MAX_PENDING_BINDS];
    int           nbinds;
} program_obj;

static shader_obj  g_shaders[MAX_SHADERS];
static program_obj g_programs[MAX_PROGRAMS];
static GLuint      g_next_handle = 1;
static program_obj *g_cur = NULL;

/* ------------------------------------------------------------------ */
static shader_obj *find_shader(GLuint h)
{
    int i;
    for (i = 0; i < MAX_SHADERS; ++i)
        if (g_shaders[i].in_use && g_shaders[i].handle == h) return &g_shaders[i];
    return NULL;
}
static program_obj *find_program(GLuint h)
{
    int i;
    for (i = 0; i < MAX_PROGRAMS; ++i)
        if (g_programs[i].in_use && g_programs[i].handle == h) return &g_programs[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* shaders */
GLuint glCreateShader(GLenum type)
{
    int i;
    if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER) {
        gles_engine_set_error(GL_INVALID_ENUM);
        return 0;
    }
    for (i = 0; i < MAX_SHADERS; ++i) {
        if (!g_shaders[i].in_use) {
            shader_obj *s = &g_shaders[i];
            memset(s, 0, sizeof *s);
            s->in_use = 1; s->handle = g_next_handle++; s->type = type;
            s->compile_status = GL_FALSE;
            return s->handle;
        }
    }
    return 0;
}

void glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string,
                    const GLint *length)
{
    shader_obj *s = find_shader(shader);
    size_t total = 0; int i; char *buf; size_t off = 0;
    if (!s) { gles_engine_set_error(GL_INVALID_VALUE); return; }
    if (count < 0 || (count > 0 && !string)) {
        gles_engine_set_error(GL_INVALID_VALUE);
        return;
    }
    for (i = 0; i < count; ++i) {
        if (!string[i]) continue;
        total += (length && length[i] >= 0) ? (size_t)length[i] : strlen(string[i]);
    }
    buf = (char *)malloc(total + 1);
    if (!buf) return;
    for (i = 0; i < count; ++i) {
        size_t n;
        if (!string[i]) continue;
        n = (length && length[i] >= 0) ? (size_t)length[i] : strlen(string[i]);
        memcpy(buf + off, string[i], n); off += n;
    }
    buf[off] = '\0';
    free(s->src);
    s->src = buf;
}

void glCompileShader(GLuint shader)
{
    shader_obj *s = find_shader(shader);
    if (!s) { gles_engine_set_error(GL_INVALID_VALUE); return; }
    if (s->stage) { glsl_free_stage(s->stage); s->stage = NULL; }
    s->log[0] = '\0';
    if (!s->src) { s->compile_status = GL_FALSE; snprintf(s->log, LOG_LEN, "no source"); return; }
    s->stage = glsl_compile_stage(s->src, s->type == GL_VERTEX_SHADER ? 1 : 0,
                                  s->log, LOG_LEN);
    s->compile_status = s->stage ? GL_TRUE : GL_FALSE;
}

void glGetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
    shader_obj *s = find_shader(shader);
    if (!s) { gles_engine_set_error(GL_INVALID_VALUE); return; }
    if (!params) return;
    switch (pname) {
    case GL_COMPILE_STATUS:    *params = s->compile_status; break;
    case GL_SHADER_TYPE:       *params = (GLint)s->type; break;
    case GL_DELETE_STATUS:     *params = s->delete_flag; break;
    case GL_INFO_LOG_LENGTH:   *params = (GLint)strlen(s->log) + 1; break;
    case GL_SHADER_SOURCE_LENGTH: *params = s->src ? (GLint)strlen(s->src) + 1 : 0; break;
    default: *params = 0; break;
    }
}

void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog)
{
    shader_obj *s = find_shader(shader);
    GLsizei n;
    if (!s) return;
    n = (GLsizei)strlen(s->log);
    if (bufSize > 0 && infoLog) {
        GLsizei c = n < bufSize - 1 ? n : bufSize - 1;
        memcpy(infoLog, s->log, c); infoLog[c] = '\0';
        if (length) *length = c;
    } else if (length) *length = 0;
}

void glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source)
{
    shader_obj *s = find_shader(shader);
    GLsizei n;
    if (!s || !s->src) { if (length) *length = 0; if (bufSize>0&&source) source[0]='\0'; return; }
    n = (GLsizei)strlen(s->src);
    if (bufSize > 0 && source) {
        GLsizei c = n < bufSize - 1 ? n : bufSize - 1;
        memcpy(source, s->src, c); source[c] = '\0';
        if (length) *length = c;
    } else if (length) *length = 0;
}

void glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype,
                                GLint *range, GLint *precision)
{
    (void)shadertype; (void)precisiontype;
    if (range) { range[0] = 127; range[1] = 127; }
    if (precision) *precision = 23;
}

void glReleaseShaderCompiler(void) { }

static int shader_attached(GLuint h)
{
    int i;
    for (i = 0; i < MAX_PROGRAMS; ++i)
        if (g_programs[i].in_use &&
            (g_programs[i].vs_shader == h || g_programs[i].fs_shader == h)) return 1;
    return 0;
}

void glDeleteShader(GLuint shader)
{
    shader_obj *s = find_shader(shader);
    if (!s) return;
    s->delete_flag = 1;
    if (!shader_attached(shader)) {
        free(s->src); s->src = NULL;
        if (s->stage) { glsl_free_stage(s->stage); s->stage = NULL; }
        s->in_use = 0;
    }
}

GLboolean glIsShader(GLuint shader) { return find_shader(shader) ? GL_TRUE : GL_FALSE; }

/* ------------------------------------------------------------------ */
/* programs */
GLuint glCreateProgram(void)
{
    int i;
    for (i = 0; i < MAX_PROGRAMS; ++i) {
        if (!g_programs[i].in_use) {
            program_obj *p = &g_programs[i];
            memset(p, 0, sizeof *p);
            p->in_use = 1; p->handle = g_next_handle++;
            p->link_status = GL_FALSE;
            return p->handle;
        }
    }
    return 0;
}

void glAttachShader(GLuint program, GLuint shader)
{
    program_obj *p = find_program(program);
    shader_obj *s = find_shader(shader);
    if (!p || !s) {
        gles_engine_set_error((find_shader(program) || find_program(shader)) ?
                              GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }
    if (p->vs_shader == shader || p->fs_shader == shader) {
        gles_engine_set_error(GL_INVALID_OPERATION);
        return;
    }
    if (s->type == GL_VERTEX_SHADER)   { p->vs_shader = shader; }
    else if (s->type == GL_FRAGMENT_SHADER) { p->fs_shader = shader; }
}

void glDetachShader(GLuint program, GLuint shader)
{
    program_obj *p = find_program(program);
    if (!p) return;
    if (p->vs_shader == shader) p->vs_shader = 0;
    if (p->fs_shader == shader) p->fs_shader = 0;
}

void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name)
{
    program_obj *p = find_program(program);
    if (!p || !name) return;
    if (p->nbinds < MAX_PENDING_BINDS) {
        strncpy(p->bind_name[p->nbinds], name, GLSL_NAME_LEN-1);
        p->bind_name[p->nbinds][GLSL_NAME_LEN-1] = '\0';
        p->bind_loc[p->nbinds] = (int)index;
        p->nbinds++;
    }
}

void glLinkProgram(GLuint program)
{
    program_obj *p = find_program(program);
    shader_obj *vs, *fs;
    int i;
    if (!p) { gles_engine_set_error(GL_INVALID_VALUE); return; }

    /* release a previous link */
    if (p->pgl_id) { gles_engine_gpu_delete(p->pgl_id); gles_engine_delete_program(p->pgl_id); p->pgl_id = 0; }
    if (p->gp) { glsl_free_program(p->gp); p->gp = NULL; }
    free(p->ublock); p->ublock = NULL; p->ublock_cells = 0;
    p->log[0] = '\0'; p->link_status = GL_FALSE;
        p->validate_status = GL_FALSE; p->validate_log[0] = '\0';

    vs = find_shader(p->vs_shader);
    fs = find_shader(p->fs_shader);
    if (!vs || !vs->stage || !fs || !fs->stage) {
        snprintf(p->log, LOG_LEN, "link: vertex and fragment shaders must be attached and compiled");
        return;
    }
    for (i = 0; i < p->nbinds; ++i)
        glsl_bind_attrib_location(vs->stage, p->bind_name[i], p->bind_loc[i]);

    /* glsl_link consumes both stages */
    double _t_cpu = v3ct_now();
    v3ct_log("glLinkProgram: ENTER cpu glsl_link");
    p->gp = glsl_link(vs->stage, fs->stage, p->log, LOG_LEN);
    v3ct_log("glLinkProgram: cpu glsl_link ms=%.0f ok=%d", v3ct_now() - _t_cpu,
             p->gp != NULL);
    vs->stage = NULL; fs->stage = NULL;
    if (!p->gp) return;

    p->ublock_cells = p->gp->uniform_block_floats > 0 ? p->gp->uniform_block_floats : 1;
    p->ublock = (glsl_cell *)calloc(p->ublock_cells, sizeof(glsl_cell));
    if (!p->ublock) { snprintf(p->log, LOG_LEN, "link: out of memory (uniform block)"); glsl_free_program(p->gp); p->gp=NULL; return; }

    p->pgl_id = gles_engine_create_interpreted_program(p->gp, p->ublock, p->ublock_cells);
    if (!p->pgl_id) {
        snprintf(p->log, LOG_LEN, "link: engine could not create the interpreted program");
        glsl_free_program(p->gp); p->gp = NULL;
        free(p->ublock); p->ublock = NULL;
        return;
    }
    p->link_status = GL_TRUE;

    /* Hand the still-alive GLSL sources to the engine's transparent GPU
     * bridge.  It compiles them to V3D QPU and routes eligible draws onto the
     * GPU, or stays inert (CPU interpreter) when the GPU path is off.  This TU
     * passes opaque strings only - it has no GPU awareness. */
    double _t_gpu = v3ct_now();
    v3ct_log("glLinkProgram: ENTER gpu bridge");
    gles_engine_gpu_link(p->pgl_id, vs->src, fs->src);
    v3ct_log("glLinkProgram: gpu bridge ms=%.0f", v3ct_now() - _t_gpu);
}

void glUseProgram(GLuint program)
{
    if (program == 0) { g_cur = NULL; gles_engine_use_program(0); return; }
    {
        program_obj *p = find_program(program);
        if (!p) { gles_engine_set_error(GL_INVALID_VALUE); return; }
        if (!p->link_status || !p->pgl_id) {
            gles_engine_set_error(GL_INVALID_OPERATION);
            return;
        }
        g_cur = p;
        gles_engine_use_program(p->pgl_id);
    }
}

/* GL_CURRENT_PROGRAM must report the GLES2 handle, not the engine-side
 * pgl_id behind it; glGetIntegerv (gles2_api.c) asks here instead of the
 * engine. */
GLuint gles2_current_program_handle(void) { return g_cur ? g_cur->handle : 0; }

void glValidateProgram(GLuint program)
{
    program_obj *p = find_program(program);
    int sampler_type[32] = { 0 };
    int i;
    if (!p) { gles_engine_set_error(GL_INVALID_VALUE); return; }
    p->validate_status = GL_FALSE;
    p->validate_log[0] = '\0';
    if (!p->link_status || !p->gp || !p->ublock ||
        !gles_engine_program_executable(p->pgl_id)) {
        snprintf(p->validate_log, LOG_LEN, "validate: program is not linked and executable");
        return;
    }
    for (i = 0; i < p->gp->nuniforms; ++i) {
        glsl_uniform *u = &p->gp->uniforms[i];
        int unit, cube;
        if (u->type != T_SAMPLER2D && u->type != T_SAMPLERCUBE) continue;
        unit = p->ublock[u->offset].i;
        cube = u->type == T_SAMPLERCUBE;
        if (unit < 0 || unit >= 32) {
            snprintf(p->validate_log, LOG_LEN, "validate: %s selects invalid texture unit %d",
                     u->name, unit);
            return;
        }
        if (sampler_type[unit] && sampler_type[unit] != u->type) {
            snprintf(p->validate_log, LOG_LEN,
                     "validate: sampler2D and samplerCube alias texture unit %d", unit);
            return;
        }
        sampler_type[unit] = u->type;
        if (!gles_engine_texture_complete(unit, cube)) {
            snprintf(p->validate_log, LOG_LEN, "validate: %s has an incomplete texture on unit %d",
                     u->name, unit);
            return;
        }
    }
    p->validate_status = GL_TRUE;
}
GLboolean glIsProgram(GLuint program) { return find_program(program) ? GL_TRUE : GL_FALSE; }

void glDeleteProgram(GLuint program)
{
    program_obj *p = find_program(program);
    if (!p) return;
    p->delete_flag = 1;
    if (g_cur == p) g_cur = NULL;
    if (p->pgl_id) { gles_engine_gpu_delete(p->pgl_id); gles_engine_delete_program(p->pgl_id); p->pgl_id = 0; }
    if (p->gp) { glsl_free_program(p->gp); p->gp = NULL; }
    free(p->ublock); p->ublock = NULL;
    p->in_use = 0;
}

void glGetProgramiv(GLuint program, GLenum pname, GLint *params)
{
    program_obj *p = find_program(program);
    if (!p) { gles_engine_set_error(GL_INVALID_VALUE); return; }
    if (!params) return;
    switch (pname) {
    case GL_LINK_STATUS:     *params = p->link_status; break;
    case GL_VALIDATE_STATUS: *params = p->validate_status; break;
    case GL_DELETE_STATUS:   *params = p->delete_flag; break;
    case GL_ATTACHED_SHADERS:*params = (p->vs_shader?1:0) + (p->fs_shader?1:0); break;
    case GL_ACTIVE_UNIFORMS: *params = p->gp ? p->gp->nuniforms : 0; break;
    case GL_ACTIVE_ATTRIBUTES:*params = p->gp ? p->gp->nattribs : 0; break;
    case GL_INFO_LOG_LENGTH: *params = (GLint)strlen(p->log) + 1; break;
    case GL_ACTIVE_UNIFORM_MAX_LENGTH:
    case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH: *params = GLSL_NAME_LEN; break;
    default: *params = 0; break;
    }
}

void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog)
{
    program_obj *p = find_program(program);
    GLsizei n;
    if (!p) return;
    n = (GLsizei)strlen(p->log);
    if (bufSize > 0 && infoLog) {
        GLsizei c = n < bufSize - 1 ? n : bufSize - 1;
        memcpy(infoLog, p->log, c); infoLog[c] = '\0';
        if (length) *length = c;
    } else if (length) *length = 0;
}

void glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei *count, GLuint *shaders)
{
    program_obj *p = find_program(program);
    GLsizei n = 0;
    if (!p) { if (count) *count = 0; return; }
    if (shaders && n < maxCount && p->vs_shader) shaders[n++] = p->vs_shader;
    if (shaders && n < maxCount && p->fs_shader) shaders[n++] = p->fs_shader;
    if (count) *count = n;
}

GLint glGetUniformLocation(GLuint program, const GLchar *name)
{
    program_obj *p = find_program(program);
    if (!p || !p->gp || !name) return -1;
    return glsl_uniform_index(p->gp, name);
}

GLint glGetAttribLocation(GLuint program, const GLchar *name)
{
    program_obj *p = find_program(program);
    if (!p || !p->gp || !name) return -1;
    return glsl_attrib_index(p->gp, name);
}

static GLenum gl_type_of(int t)
{
    switch (t) {
    case T_FLOAT: return GL_FLOAT; case T_VEC2: return GL_FLOAT_VEC2;
    case T_VEC3:  return GL_FLOAT_VEC3; case T_VEC4: return GL_FLOAT_VEC4;
    case T_INT:   return GL_INT;   case T_IVEC2: return GL_INT_VEC2;
    case T_IVEC3: return GL_INT_VEC3; case T_IVEC4: return GL_INT_VEC4;
    case T_BOOL:  return GL_BOOL;  case T_BVEC2: return GL_BOOL_VEC2;
    case T_BVEC3: return GL_BOOL_VEC3; case T_BVEC4: return GL_BOOL_VEC4;
    case T_MAT2:  return GL_FLOAT_MAT2; case T_MAT3: return GL_FLOAT_MAT3;
    case T_MAT4:  return GL_FLOAT_MAT4;
    case T_SAMPLER2D: return GL_SAMPLER_2D;
    case T_SAMPLERCUBE: return GL_SAMPLER_CUBE;
    default: return GL_FLOAT;
    }
}

static void copy_active_name(const char *src, GLsizei bufSize, GLsizei *length,
                             GLint *size, GLenum *type, GLchar *name,
                             int sz, int ty)
{
    GLsizei n = (GLsizei)strlen(src);
    if (name && bufSize > 0) {
        GLsizei c = n < bufSize - 1 ? n : bufSize - 1;
        memcpy(name, src, c); name[c] = '\0';
        if (length) *length = c;
    } else if (length) *length = 0;
    if (size) *size = sz;
    if (type) *type = (GLenum)ty;
}

void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length,
                        GLint *size, GLenum *type, GLchar *name)
{
    program_obj *p = find_program(program);
    glsl_uniform *U;
    if (!p || !p->gp || (int)index >= p->gp->nuniforms) return;
    U = &p->gp->uniforms[index];
    copy_active_name(U->name, bufSize, length, size, type, name,
                     U->array_len > 0 ? U->array_len : 1, (int)gl_type_of(U->type));
}

void glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length,
                       GLint *size, GLenum *type, GLchar *name)
{
    program_obj *p = find_program(program);
    glsl_attrib *A;
    if (!p || !p->gp || (int)index >= p->gp->nattribs) return;
    A = &p->gp->attribs[index];
    copy_active_name(A->name, bufSize, length, size, type, name, 1,
                     (int)gl_type_of(glsl_float_type_of_len(A->size)));
}

/* ------------------------------------------------------------------ */
/* uniform setters (operate on the current program) */
static glsl_uniform *cur_uniform(GLint loc, GLsizei count, const void *values,
                                 int comps, int kind, int dim)
{
    glsl_uniform *u;
    int elements;
    if (loc == -1) return NULL;
    if (!g_cur || !g_cur->link_status || !g_cur->gp || !g_cur->ublock ||
        loc < 0 || loc >= g_cur->gp->nuniforms) {
        gles_engine_set_error(GL_INVALID_OPERATION);
        return NULL;
    }
    if (count < 0 || !values) {
        gles_engine_set_error(GL_INVALID_VALUE);
        return NULL;
    }
    if (count == 0) return NULL;
    u = &g_cur->gp->uniforms[loc];
    elements = u->array_len > 0 ? u->array_len : 1;
    if (count > elements) {
        gles_engine_set_error(GL_INVALID_OPERATION);
        return NULL;
    }
    if (kind == 0) {
        int expected = glsl_float_type_of_len(comps);
        if (u->type != expected) {
            gles_engine_set_error(GL_INVALID_OPERATION);
            return NULL;
        }
    } else if (kind == 1) {
        if (!glsl_is_int_family(u->type) || glsl_vec_len(u->type) != comps ||
            ((u->type == T_SAMPLER2D || u->type == T_SAMPLERCUBE) && comps != 1)) {
            gles_engine_set_error(GL_INVALID_OPERATION);
            return NULL;
        }
    } else {
        int expected = dim == 2 ? T_MAT2 : dim == 3 ? T_MAT3 : T_MAT4;
        if (u->type != expected) {
            gles_engine_set_error(GL_INVALID_OPERATION);
            return NULL;
        }
    }
    return u;
}

static void uni_fv(GLint loc, GLsizei count, const GLfloat *v, int comps)
{
    glsl_uniform *U = cur_uniform(loc, count, v, comps, 0, 0);
    int k, j;
    glsl_cell *c;
    if (!U) return;
    for (k = 0; k < count; ++k) {
        c = g_cur->ublock + U->offset + k * U->elem_stride;
        for (j = 0; j < comps; ++j) c[j].f = v[k * comps + j];
    }
}

static void uni_iv(GLint loc, GLsizei count, const GLint *v, int comps)
{
    glsl_uniform *U = cur_uniform(loc, count, v, comps, 1, 0);
    int k, j;
    glsl_cell *c;
    if (!U) return;
    for (k = 0; k < count; ++k) {
        c = g_cur->ublock + U->offset + k * U->elem_stride;
        for (j = 0; j < comps; ++j) c[j].i = v[k * comps + j];
    }
}

static void uni_mat(GLint loc, GLsizei count, const GLfloat *v, int dim)
{
    glsl_uniform *U = cur_uniform(loc, count, v, dim * dim, 2, dim);
    int k, j, n = dim * dim;
    glsl_cell *c;
    if (!U || !v || count < 1) return;
    for (k = 0; k < count; ++k) {
        c = g_cur->ublock + U->offset + k * U->elem_stride;
        for (j = 0; j < n && j < U->ncomp; ++j) c[j].f = v[k * n + j];
    }
}

void glUniform1f (GLint l, GLfloat v0) { GLfloat a[1]={v0}; uni_fv(l,1,a,1); }
void glUniform2f (GLint l, GLfloat v0, GLfloat v1) { GLfloat a[2]={v0,v1}; uni_fv(l,1,a,2); }
void glUniform3f (GLint l, GLfloat v0, GLfloat v1, GLfloat v2) { GLfloat a[3]={v0,v1,v2}; uni_fv(l,1,a,3); }
void glUniform4f (GLint l, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { GLfloat a[4]={v0,v1,v2,v3}; uni_fv(l,1,a,4); }
void glUniform1i (GLint l, GLint v0) { GLint a[1]={v0}; uni_iv(l,1,a,1); }
void glUniform2i (GLint l, GLint v0, GLint v1) { GLint a[2]={v0,v1}; uni_iv(l,1,a,2); }
void glUniform3i (GLint l, GLint v0, GLint v1, GLint v2) { GLint a[3]={v0,v1,v2}; uni_iv(l,1,a,3); }
void glUniform4i (GLint l, GLint v0, GLint v1, GLint v2, GLint v3) { GLint a[4]={v0,v1,v2,v3}; uni_iv(l,1,a,4); }
void glUniform1fv(GLint l, GLsizei c, const GLfloat *v) { uni_fv(l,c,v,1); }
void glUniform2fv(GLint l, GLsizei c, const GLfloat *v) { uni_fv(l,c,v,2); }
void glUniform3fv(GLint l, GLsizei c, const GLfloat *v) { uni_fv(l,c,v,3); }
void glUniform4fv(GLint l, GLsizei c, const GLfloat *v) { uni_fv(l,c,v,4); }
void glUniform1iv(GLint l, GLsizei c, const GLint *v)   { uni_iv(l,c,v,1); }
void glUniform2iv(GLint l, GLsizei c, const GLint *v)   { uni_iv(l,c,v,2); }
void glUniform3iv(GLint l, GLsizei c, const GLint *v)   { uni_iv(l,c,v,3); }
void glUniform4iv(GLint l, GLsizei c, const GLint *v)   { uni_iv(l,c,v,4); }
void glUniformMatrix2fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if (t != GL_FALSE) { gles_engine_set_error(GL_INVALID_VALUE); return; } uni_mat(l,c,v,2); }
void glUniformMatrix3fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if (t != GL_FALSE) { gles_engine_set_error(GL_INVALID_VALUE); return; } uni_mat(l,c,v,3); }
void glUniformMatrix4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if (t != GL_FALSE) { gles_engine_set_error(GL_INVALID_VALUE); return; } uni_mat(l,c,v,4); }

void glGetUniformfv(GLuint program, GLint location, GLfloat *params)
{
    program_obj *p = find_program(program);
    glsl_uniform *U; int j;
    if (!p || !p->gp || !params) return;
    if (location < 0 || location >= p->gp->nuniforms) return;
    U = &p->gp->uniforms[location];
    for (j = 0; j < U->ncomp; ++j)
        params[j] = glsl_is_int_family(U->type) ? (GLfloat)p->ublock[U->offset+j].i
                                                : p->ublock[U->offset+j].f;
}

void glGetUniformiv(GLuint program, GLint location, GLint *params)
{
    program_obj *p = find_program(program);
    glsl_uniform *U; int j;
    if (!p || !p->gp || !params) return;
    if (location < 0 || location >= p->gp->nuniforms) return;
    U = &p->gp->uniforms[location];
    for (j = 0; j < U->ncomp; ++j)
        params[j] = glsl_is_int_family(U->type) ? p->ublock[U->offset+j].i
                                                : (GLint)p->ublock[U->offset+j].f;
}
