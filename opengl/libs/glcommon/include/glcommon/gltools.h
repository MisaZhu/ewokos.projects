#ifndef GLTOOLS_H
#define GLTOOLS_H

/* glcommon now targets the standard GLES2 + EGL front end (libs/gles) instead
 * of portablegl's C-callback model.  portablegl remains the internal software
 * rasterizer behind libGLESv2, but consumers only ever see these headers. */
#define USING_GLES2

#ifdef __cplusplus
extern "C" {
#endif

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

/* GLES2 has no GL_COMPRESSED_RGBA upload enum; the engine treats a plain RGBA8
 * upload as the compressed path did under portablegl. */
#ifndef GL_COMPRESSED_RGBA
#define GL_COMPRESSED_RGBA GL_RGBA
#endif

#include <stb_image.h>

// Universal includes
#include <stdio.h>
#include <math.h>

#ifndef MAX_TEX_DIM
	#define MAX_TEX_DIM 4096
#endif



//general
void check_errors(int n, const char* str);




//shaders
// These functions are defined in c_utils.c, not exported from gltools.c
// int file_read(FILE* file, char** out);
// int file_open_read(const char* filename, const char* mode, char** out);

int compile_shader_str(GLuint shader, const char* shader_str);
int link_program(GLuint program);
GLuint load_shader_pair(const char* vert_shader_src, const char* frag_shader_src);
GLuint load_shader_file_pair(const char* vert_file, const char* frag_file);

void set_uniform1i(GLuint program, const char* name, int val);
void set_uniform2i(GLuint program, const char* name, int x, int y);
void set_uniform3i(GLuint program, const char* name, int x, int y, int z);
void set_uniform4i(GLuint program, const char* name, int x, int y, int z, int w);

void set_uniform1f(GLuint program, const char* name, float val);
void set_uniform2f(GLuint program, const char* name, float x, float y);
void set_uniform3f(GLuint program, const char* name, float x, float y, float z);
void set_uniform4f(GLuint program, const char* name, float x, float y, float z, float w);

void set_uniform2fv(GLuint program, const char* name, const GLfloat* v);
void set_uniform3fv(GLuint program, const char* name, const GLfloat* v);
void set_uniform4fv(GLuint program, const char* name, const GLfloat* v);

void set_uniform_mat4f(GLuint program, const char* name, const GLfloat* mat);
void set_uniform_mat3f(GLuint program, const char* name, const GLfloat* mat);






//textures
void flip_2D_tex(GLubyte* pix, int w, int h);

void load_texture2D_from_mem(GLubyte* image, int w, int h, GLenum min_filter, GLenum mag_filter, GLenum wrap_mode, GLboolean flip, GLboolean mapdata);
GLboolean load_texture2D(const char* filename, GLenum min_filter, GLenum mag_filter, GLenum wrap_mode, GLboolean flip, GLboolean mapdata, int* width, int* height);
GLboolean load_texture_cubemap_from_mem(GLubyte* image, int w, int h, GLenum min_filter, GLenum mag_filter, GLboolean flip, GLboolean mapdata);
GLboolean load_texture_cubemap(const char* filename[], GLenum min_filter, GLenum mag_filter, GLboolean flip, GLboolean mapdata);

#ifndef USING_GLES2
int load_texture2D_array_gif(const char* filename, GLenum min_filter, GLenum mag_filter, GLenum wrap_mode);
GLboolean load_texture_rect(const char* filename, GLenum min_filter, GLenum mag_filter, GLenum wrap_mode, GLboolean flip);
#endif

#ifdef __cplusplus
}
#endif

#endif // GLTOOLS_H
