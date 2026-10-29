// Minimal EGL/GLES2 smoke test replicating librw's startEwok() init order:
// context -> window surface -> make current -> the exact calls that crash
// inside re3's initOpenGL (glGetFloatv with an extension enum, glClearColor,
// texture setup, shader compile).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <x/x.h>
#include <x/xwin.h>

static EGLDisplay egl_dpy;
static EGLContext egl_ctx;
static EGLSurface egl_surf;

static int init_egl(xwin_t *xwin)
{
	EGLint major, minor, ncfg = 0;
	EGLint cfg_attr[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
		EGL_NONE
	};
	EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLConfig cfg = NULL;
	egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (egl_dpy == EGL_NO_DISPLAY) return 0;
	if (!eglInitialize(egl_dpy, &major, &minor)) return 0;
	if (!eglChooseConfig(egl_dpy, cfg_attr, &cfg, 1, &ncfg) || ncfg < 1) return 0;
	egl_ctx = eglCreateContext(egl_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	if (egl_ctx == EGL_NO_CONTEXT) return 0;
	egl_surf = eglCreateWindowSurface(egl_dpy, cfg, (EGLNativeWindowType)xwin, NULL);
	if (egl_surf == EGL_NO_SURFACE) return 0;
	if (!eglMakeCurrent(egl_dpy, egl_surf, egl_surf, egl_ctx)) return 0;
	return 1;
}

static const char *test_vs =
	"attribute vec4 a_vert;\n"
	"void main(void) { gl_Position = a_vert; }\n";
static const char *test_fs =
	"precision mediump float;\n"
	"void main(void) { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";

int main(int argc, char **argv)
{
	setbuf(stdout, NULL);
	printf("gltest: start\n");

	x_t x;
	x_init(&x, NULL);
	xwin_t *win = xwin_open(&x, -1, 200, 200, 320, 240, "gltest", XWIN_STYLE_NORMAL);
	printf("gltest: xwin %p\n", win);
	if (win == NULL) return 1;

	if (!init_egl(win)) { printf("gltest: egl init FAILED\n"); return 1; }
	printf("gltest: egl ok\n");

	GLfloat aniso = 0.0f;
	glGetFloatv(0x84FF /* GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT */, &aniso);
	printf("gltest: glGetFloatv(EXT aniso) survived, val=%f err=0x%x\n", aniso, glGetError());

	glClearColor(0.25f, 0.25f, 0.25f, 1.0f);
	printf("gltest: glClearColor survived\n");

	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	printf("gltest: texture setup survived\n");

	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vs, 1, &test_vs, NULL);
	glCompileShader(vs);
	GLint ok = 0;
	glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
	printf("gltest: vs compiled ok=%d\n", ok);
	GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fs, 1, &test_fs, NULL);
	glCompileShader(fs);
	glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
	printf("gltest: fs compiled ok=%d\n", ok);
	GLuint prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	printf("gltest: prog linked ok=%d\n", ok);

	if (argc > 1 && strcmp(argv[1], "pre") == 0) {
		// librw-style prelight draw: interleaved pos0/normal1/color2/uv5,
		// shader reads pos + colour only. Window should fill GREEN.
		static const char* pvs =
			"attribute vec3 a_pos;\n"
			"attribute vec4 a_col;\n"
			"uniform mat4 u_mvp;\n"
			"varying vec4 v_col;\n"
			"void main(void) { v_col = a_col; gl_Position = u_mvp * vec4(a_pos, 1.0); }\n";
		static const char* pfs =
			"precision mediump float;\n"
			"varying vec4 v_col;\n"
			"void main(void) { gl_FragColor = v_col; }\n";
		GLuint vs = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vs, 1, &pvs, NULL);
		glCompileShader(vs);
		GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(fs, 1, &pfs, NULL);
		glCompileShader(fs);
		GLuint prog = glCreateProgram();
		glAttachShader(prog, vs);
		glAttachShader(prog, fs);
		glBindAttribLocation(prog, 0, "a_pos");
		glBindAttribLocation(prog, 2, "a_col");
		glLinkProgram(prog);
		GLint lok = 0;
		glGetProgramiv(prog, GL_LINK_STATUS, &lok);
		printf("gltest: prelight prog link=%d\n", lok);

		// fullscreen-ish quad covering the window, green via a_col
		float verts[4*12] = {
			-1,-1,0.5f,  0,1,0,  1,0,0,1,  0,0,
			 1,-1,0.5f,  0,1,0,  0,1,0,1,  1,0,
			 1, 1,0.5f,  0,1,0,  0,0,1,1,  1,1,
			-1, 1,0.5f,  0,1,0,  1,1,0,1,  0,1,
		};
		GLuint idx[6] = {0,1,2, 0,2,3};
		GLuint vbo, ibo;
		glGenBuffers(1, &vbo);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
		glGenBuffers(1, &ibo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
		glUseProgram(prog);
		// identity MVP
		float mvp[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
		glUniformMatrix4fv(glGetUniformLocation(prog, "u_mvp"), 1, GL_FALSE, mvp);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12*4, (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 12*4, (void*)(3*4));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 12*4, (void*)(6*4));
		glEnableVertexAttribArray(5);
		glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, 12*4, (void*)(10*4));
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
		eglSwapBuffers(egl_dpy, egl_surf);
		printf("gltest: prelight frame swapped; sleeping 20s\n");
		sleep(20);
		return 0;
	}

	// present one gray frame so the window is visible on screen
	glClearColor(0.2f, 0.4f, 0.8f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	eglSwapBuffers(egl_dpy, egl_surf);
	printf("gltest: swapped one frame; sleeping 20s (look at the screen)\n");
	sleep(20);

	eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroySurface(egl_dpy, egl_surf);
	eglDestroyContext(egl_dpy, egl_ctx);
	eglTerminate(egl_dpy);
	xwin_close(win);
	printf("gltest: done\n");
	return 0;
}
