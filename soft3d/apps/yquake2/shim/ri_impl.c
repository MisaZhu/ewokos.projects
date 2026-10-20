/*
 * ri_impl.c - host implementation of the refimport_t (`ri`) entry points for
 * the standalone yquake2 gl3 refresher port (projects/soft3d/apps/yquake2).
 *
 * Upstream the refresher imports these from the engine's client/common layer.
 * This "refresher only" port does not vendor that layer, so the host side is
 * provided here on top of plain libc:
 *   - a static Cvar table (the front end has inert FBOs, so a few cvars are
 *     force-overridden to keep postprocessing/MSAA off),
 *   - FS_LoadFile/FS_FreeFile reading from the staged res/ dir,
 *   - Sys_Error / Com_VPrintf routed to stdout,
 *   - the Cmd_* and Vid_* entry points the render loop never exercises as
 *     no-op stubs.
 *
 * The three video refimports (GLimp_InitGraphics/GLimp_GetDesktopMode/
 * Vid_GetModeInfo) live in shim/vid_egl.c because they need the EGL/xwin
 * state; R_InitRefimport() wires everything into a caller-owned refimport_t.
 *
 * NOTE: files/common.c already defines Com_Printf/Com_DPrintf/Com_Error/
 * R_Printf/Sys_Error (the refresher-side wrappers) and the r_newrefdef/vid
 * globals; those route *into* ri, so this file must only define the ri.*
 * callbacks, never the wrappers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "ref_shared.h"   /* refimport_t, cvar_t, ERR_*, PRINT_*, ref_restart_t */

/* video refimports owned by shim/vid_egl.c */
extern qboolean GLimp_InitGraphics(int fullscreen, int *pwidth, int *pheight);
extern qboolean GLimp_GetDesktopMode(int *pwidth, int *pheight);
extern qboolean Vid_GetModeInfo(int *width, int *height, int mode);

/* ------------------------------------------------------------------ *
 * Cvar table
 * ------------------------------------------------------------------ */

#define MAX_CVARS     256
#define CVAR_NAME_LEN 64
#define CVAR_STR_LEN  128

static cvar_t cvar_pool[MAX_CVARS];
static char   cvar_namebuf[MAX_CVARS][CVAR_NAME_LEN];
static char   cvar_strbuf[MAX_CVARS][CVAR_STR_LEN];
static int    cvar_count = 0;

/* Values that must hold no matter what default the refresher asks for, because
 * the GLES2-subset front end has no real off-screen FBO and no postprocessing
 * or MSAA path.  gl3_usefbo=0 keeps GL3_SetupGL() off the FBO branch entirely. */
static const char *
cvar_force(const char *name)
{
	if (strcmp(name, "gl3_usefbo") == 0)      return "0";
	if (strcmp(name, "r_clear") == 0)         return "1";
	if (strcmp(name, "r_postprocessing") == 0) return "0";
	if (strcmp(name, "r_msaa_samples") == 0)  return "0";
	if (strcmp(name, "gl3_bloom") == 0)       return "0";
	/* r_cull=0 disables backface culling so the procedurally generated cube is
	 * visible regardless of triangle winding (GL3_SetupGL gates glCullFace on
	 * r_cull).  Depth test still resolves the nearest face. */
	if (strcmp(name, "r_cull") == 0)           return "0";
	return NULL;
}

static cvar_t *
cvar_find(const char *name)
{
	int i;
	for (i = 0; i < cvar_count; ++i)
	{
		if (strcmp(cvar_pool[i].name, name) == 0)
			return &cvar_pool[i];
	}
	return NULL;
}

static cvar_t *
host_Cvar_Get(const char *name, const char *value, int flags)
{
	cvar_t *c;
	const char *forced = cvar_force(name);
	const char *init = forced ? forced : value;

	c = cvar_find(name);
	if (c)
		return c;   /* keep whatever value it already has */

	if (cvar_count >= MAX_CVARS)
	{
		fprintf(stderr, "ri_impl: cvar table overflow on '%s'\n", name);
		return NULL;
	}

	c = &cvar_pool[cvar_count];
	snprintf(cvar_namebuf[cvar_count], CVAR_NAME_LEN, "%s", name);
	snprintf(cvar_strbuf[cvar_count], CVAR_STR_LEN, "%s", init);

	c->name = cvar_namebuf[cvar_count];
	c->string = cvar_strbuf[cvar_count];
	c->latched_string = NULL;
	c->flags = flags;
	c->modified = true;
	c->value = (float)atof(init);
	c->next = NULL;
	c->default_string = c->string;

	cvar_count++;
	return c;
}

static cvar_t *
host_Cvar_Set(const char *name, const char *value)
{
	cvar_t *c = cvar_find(name);

	if (!c)
		return host_Cvar_Get(name, value, 0);

	snprintf(c->string, CVAR_STR_LEN, "%s", value);
	c->value = (float)atof(value);
	c->modified = true;
	return c;
}

static void
host_Cvar_SetValue(const char *name, float value)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%g", value);
	host_Cvar_Set(name, buf);
}

/* ------------------------------------------------------------------ *
 * Filesystem: read from the staged res/ dir, then the cwd
 * ------------------------------------------------------------------ */

static int
host_FS_LoadFile(const char *name, void **buf)
{
	/* Search order: cwd-relative staged res/, then cwd, then the app's absolute
	 * install dir.  The window manager launches us as /apps/yquake2/yquake2 from
	 * an arbitrary cwd (e.g. /home/root), so the absolute /apps/yquake2/res/
	 * fallback is what actually resolves the staged assets. */
	static const char *bases[] = {
		"res/%s", "%s", "/apps/yquake2/res/%s", "/apps/yquake2/%s"
	};
	char path[512];
	FILE *f = NULL;
	long len;
	void *p;
	size_t b;

	if (buf)
		*buf = NULL;
	if (!name)
		return -1;

	for (b = 0; b < sizeof(bases) / sizeof(bases[0]); ++b)
	{
		snprintf(path, sizeof(path), bases[b], name);
		f = fopen(path, "rb");
		if (f)
			break;
	}
	if (!f)
		return -1;

	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len <= 0)
	{
		fclose(f);
		return -1;
	}

	p = malloc((size_t)len);
	if (!p)
	{
		fclose(f);
		return -1;
	}
	if (fread(p, 1, (size_t)len, f) != (size_t)len)
	{
		free(p);
		fclose(f);
		return -1;
	}
	fclose(f);

	if (buf)
		*buf = p;
	else
		free(p);

	return (int)len;
}

static void
host_FS_FreeFile(void *buf)
{
	free(buf);
}

static const char *
host_FS_Gamedir(void)
{
	return "res";
}

/* ------------------------------------------------------------------ *
 * Errors and console output
 * ------------------------------------------------------------------ */

static void
host_Sys_Error(int err_level, const char *fmt, ...)
{
	char text[2048];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);

	fprintf(stderr, "yquake2 Sys_Error(%d): %s\n", err_level, text);
	fflush(stderr);

	/* No console to drop back to in this standalone demo: a fatal/drop error
	 * ends the process, ERR_QUIT is a clean exit. */
	exit(err_level == ERR_QUIT ? 0 : 1);
}

static void
host_Com_VPrintf(int print_level, const char *fmt, va_list argptr)
{
	(void)print_level;
	/* EwokOS libc provides vfprintf/vsnprintf but not vprintf. */
	vfprintf(stdout, fmt, argptr);
	fflush(stdout);
}

/* ------------------------------------------------------------------ *
 * Command / video stubs the render loop never exercises
 * ------------------------------------------------------------------ */

static void host_Cmd_AddCommand(const char *name, void (*cmd)(void)) { (void)name; (void)cmd; }
static void host_Cmd_RemoveCommand(const char *name) { (void)name; }
static int  host_Cmd_Argc(void) { return 0; }
static char *host_Cmd_Argv(int i) { (void)i; return (char *)""; }
static void host_Cmd_ExecuteText(int exec_when, char *text) { (void)exec_when; (void)text; }

static void host_Vid_MenuInit(void) { }
static void host_Vid_WriteScreenshot(int width, int height, int comp, const void *data)
{
	(void)width; (void)height; (void)comp; (void)data;
}
static void host_Vid_RequestRestart(ref_restart_t rs) { (void)rs; }

/* ------------------------------------------------------------------ *
 * Wire everything into a caller-owned refimport_t
 * ------------------------------------------------------------------ */

void
R_InitRefimport(refimport_t *r)
{
	memset(r, 0, sizeof(*r));

	r->Sys_Error         = host_Sys_Error;
	r->Cmd_AddCommand    = host_Cmd_AddCommand;
	r->Cmd_RemoveCommand = host_Cmd_RemoveCommand;
	r->Cmd_Argc          = host_Cmd_Argc;
	r->Cmd_Argv          = host_Cmd_Argv;
	r->Cmd_ExecuteText   = host_Cmd_ExecuteText;
	r->Com_VPrintf       = host_Com_VPrintf;
	r->FS_LoadFile       = host_FS_LoadFile;
	r->FS_FreeFile       = host_FS_FreeFile;
	r->FS_Gamedir        = host_FS_Gamedir;
	r->Cvar_Get          = host_Cvar_Get;
	r->Cvar_Set          = host_Cvar_Set;
	r->Cvar_SetValue     = host_Cvar_SetValue;
	r->Vid_GetModeInfo   = Vid_GetModeInfo;
	r->Vid_MenuInit      = host_Vid_MenuInit;
	r->Vid_WriteScreenshot = host_Vid_WriteScreenshot;
	r->GLimp_InitGraphics  = GLimp_InitGraphics;
	r->GLimp_GetDesktopMode = GLimp_GetDesktopMode;
	r->Vid_RequestRestart = host_Vid_RequestRestart;
}
