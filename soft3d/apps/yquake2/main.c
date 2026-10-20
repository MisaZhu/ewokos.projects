/*
 * main.c - standalone bootstrap + render loop for the yquake2 gl3 refresher
 * port (projects/soft3d/apps/yquake2).
 *
 * This is NOT the Quake2 client: there is no server, no game logic, no
 * netplay and no dlopen.  It mirrors apps/gears + apps/imgui_demo: create an
 * xwin window, hand it to the refresher's EGL context, then drive the gl3
 * export table (refexport_t) directly.
 *
 * Flow:
 *   1. create the xwin_t (g_xwin) that shim/vid_egl.c builds EGL on top of;
 *   2. fill a refimport_t via R_InitRefimport() (shim/ri_impl.c) and call
 *      GetRefAPI(ri) to obtain the export table `re`;
 *   3. re.Init() -> GL3_Init() creates the context (through
 *      ri.GLimp_InitGraphics -> re.InitContext -> GL3_InitContext), loads
 *      pics/colormap.pcx and compiles the down-ported GLSL ES 1.00 shaders;
 *   4. register a procedurally generated .md2 model (res/models/...);
 *   5. every x.on_loop tick: re.BeginFrame(); re.RenderFrame(&fd) with
 *      RDF_NOWORLDMODEL (no .bsp world, entity-only pass); re.EndFrame()
 *      (which swaps the buffers).
 *
 * There is deliberately no world model: RDF_NOWORLDMODEL makes GL3_DrawWorld()
 * early-return and GL3_RenderView() tolerate a NULL gl3_worldmodel, so a single
 * rotating model renders against the clear colour.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <unistd.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/keydef.h>
#include <x/x.h>
#include <x/xwin.h>

#include "ref.h"   /* refexport_t, refimport_t, entity_t, refdef_t, RDF_NOWORLDMODEL */

/* shim/ri_impl.c */
extern void R_InitRefimport(refimport_t *r);

/* gl3_main.c: the refresher's single entry point.  Upstream declares it only
 * through the GetRefAPI_t typedef (ref.h); there is no function prototype in a
 * header this bootstrap includes, so declare it here (signature must match
 * gl3_main.c: `refexport_t GetRefAPI(refimport_t imp)`). */
extern refexport_t GetRefAPI(refimport_t imp);

/* The refresher export table.  GetRefAPI() fills it; shim/vid_egl.c reads it
 * back through re.PrepareForWindow()/re.InitContext()/re.GetDrawableSize(). */
refexport_t re;

/* Window state owned here, read by shim/vid_egl.c. */
xwin_t *g_xwin = NULL;
int     g_win_w = 640;
int     g_win_h = 480;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* RF_FULLBRIGHT is defined in common/header/shared.h (entity render flags),
 * which this standalone bootstrap deliberately does not pull in.  Mirror the
 * value here (see shared.h: "allways draw full intensity"). */
#ifndef RF_FULLBRIGHT
#define RF_FULLBRIGHT 8
#endif

/* The one model we render (procedurally generated .md2 staged under res/). */
#define YQ2_MODEL_NAME "models/probe/tris.md2"

static struct model_s *g_model = NULL;
static entity_t        g_ent;
static lightstyle_t    g_lightstyles[MAX_LIGHTSTYLES];

static float    g_time = 0.0f;      /* seconds since start, drives animation */
static uint32_t g_last_tic = 0;

/* Player-controlled camera (free-fly).  Position and view angles are integrated
 * in loop() from held key/mouse state set in on_event(). */
static float g_pos[3]   = { -128.0f, 0.0f, 0.0f };
static float g_yaw      = 0.0f;   /* degrees, 0 = look down +X at the model */
static float g_pitch    = 0.0f;   /* degrees, + = look up */
static int g_key_fwd, g_key_back, g_key_left, g_key_right;
static int g_key_up, g_key_down;
static int g_key_yawl, g_key_yawr, g_key_pitu, g_key_pitd;

/* Quake2's vertical-FOV helper (see client/cl_view.c). */
static float
CalcFov(float fov_x, float width, float height)
{
	float x, a;

	if (fov_x < 1.0f || fov_x > 179.0f)
		fov_x = 90.0f;

	x = width / tanf(fov_x / 360.0f * (float)M_PI);
	a = atanf(height / x);
	a = a * 360.0f / (float)M_PI;
	return a;
}

static void
setup_frame(refdef_t *fd)
{
	memset(fd, 0, sizeof(*fd));

	fd->x = 0;
	fd->y = 0;
	fd->width = g_win_w;
	fd->height = g_win_h;
	fd->fov_x = 90.0f;
	fd->fov_y = CalcFov(fd->fov_x, (float)g_win_w, (float)g_win_h);

	/* Player-controlled camera: position and view angles come from the input
	 * state integrated in loop().  Yaw 0 looks down +X at the model. */
	fd->vieworg[0] = g_pos[0];
	fd->vieworg[1] = g_pos[1];
	fd->vieworg[2] = g_pos[2];
	fd->viewangles[0] = g_pitch;   /* pitch */
	fd->viewangles[1] = g_yaw;     /* yaw   */
	fd->viewangles[2] = 0.0f;      /* roll  */

	fd->blend[0] = fd->blend[1] = fd->blend[2] = fd->blend[3] = 0.0f;
	fd->time = g_time;
	fd->rdflags = RDF_NOWORLDMODEL;   /* entity-only pass, no .bsp world */
	fd->areabits = NULL;

	fd->lightstyles = g_lightstyles;

	if (g_model != NULL)
	{
		/* spin the model on its Z axis and animate frames over time */
		float spin = fmodf(g_time * 45.0f, 360.0f);
		memset(&g_ent, 0, sizeof(g_ent));
		g_ent.model = g_model;
		g_ent.origin[0] = 64.0f;
		g_ent.origin[1] = 0.0f;
		g_ent.origin[2] = 0.0f;
		g_ent.angles[0] = 0.0f;
		g_ent.angles[1] = spin;
		g_ent.angles[2] = 0.0f;
		g_ent.frame = 0;
		g_ent.oldframe = 0;
		g_ent.backlerp = 0.0f;
		g_ent.skinnum = 0;
		g_ent.skin = NULL;
		g_ent.alpha = 1.0f;
		/* RF_FULLBRIGHT: there is no world model, so GL3_LightPoint() would
		 * return black.  Fullbright sets shadelight={1,1,1} and the per-vertex
		 * normal shading (shadedots[lightnormalindex]) still shapes the model. */
		g_ent.flags = RF_FULLBRIGHT;

		fd->num_entities = 1;
		fd->entities = &g_ent;
	}
	else
	{
		fd->num_entities = 0;
		fd->entities = NULL;
	}

	fd->num_dlights = 0;
	fd->dlights = NULL;
	fd->num_particles = 0;
	fd->particles = NULL;
}

static void
render_frame(void)
{
	refdef_t fd;

	re.BeginFrame(0.0f);
	setup_frame(&fd);
	re.RenderFrame(&fd);
	re.EndFrame();   /* flushes the 2D batch and swaps the buffers */
}

static void
on_event(xwin_t *xwin, xevent_t *ev)
{
	if (ev->type == XEVT_IM)
	{
		int key = ev->value.im.value;
		int down = (ev->state == XIM_STATE_PRESS);

		if (key == 27)   /* ESC */
		{
			if (down)
				xwin_close(xwin);
			return;
		}
		switch (key)
		{
		case 'w': case 'W': g_key_fwd = down; break;
		case 's': case 'S': g_key_back = down; break;
		case 'a': case 'A': g_key_left = down; break;
		case 'd': case 'D': g_key_right = down; break;
		case ' ': g_key_up = down; break;
		case 'c': case 'C': g_key_down = down; break;
		case KEY_LEFT:  g_key_yawl = down; break;
		case KEY_RIGHT: g_key_yawr = down; break;
		case KEY_UP:    g_key_pitu = down; break;
		case KEY_DOWN:  g_key_pitd = down; break;
		default: break;
		}
	}
	else if (ev->type == XEVT_MOUSE)
	{
		/* drag with a button held to look around */
		if (ev->value.mouse.button != 0)
		{
			g_yaw -= (float)ev->value.mouse.rx * 0.3f;
			g_pitch -= (float)ev->value.mouse.ry * 0.3f;
			if (g_pitch > 89.0f) g_pitch = 89.0f;
			if (g_pitch < -89.0f) g_pitch = -89.0f;
		}
	}
}

static void
loop(void *p)
{
	uint32_t tic;
	float dt;

	(void)p;

	kernel_tic32(NULL, NULL, &tic);
	if (g_last_tic == 0)
		g_last_tic = tic;
	dt = (tic - g_last_tic) / 1000000.0f;
	g_last_tic = tic;
	if (dt > 0.1f)
		dt = 0.1f;
	g_time += dt;

	/* integrate free-fly camera from held input */
	{
		float a = g_yaw * (float)M_PI / 180.0f;
		float p = g_pitch * (float)M_PI / 180.0f;
		float fx = cosf(p) * cosf(a), fy = cosf(p) * sinf(a), fz = sinf(p);
		float rx = -sinf(a), ry = cosf(a);
		float speed = 120.0f, rspeed = 90.0f;
		int mv = (g_key_fwd ? 1 : 0) - (g_key_back ? 1 : 0);
		int st = (g_key_right ? 1 : 0) - (g_key_left ? 1 : 0);
		int vv = (g_key_up ? 1 : 0) - (g_key_down ? 1 : 0);
		int yw = (g_key_yawl ? 1 : 0) - (g_key_yawr ? 1 : 0);
		int pt = (g_key_pitu ? 1 : 0) - (g_key_pitd ? 1 : 0);

		g_pos[0] += (fx * mv + rx * st) * speed * dt;
		g_pos[1] += (fy * mv + ry * st) * speed * dt;
		g_pos[2] += (fz * mv + (float)vv) * speed * dt;
		g_yaw += (float)yw * rspeed * dt;
		g_pitch += (float)pt * rspeed * dt;
		if (g_pitch > 89.0f) g_pitch = 89.0f;
		if (g_pitch < -89.0f) g_pitch = -89.0f;
		g_yaw = fmodf(g_yaw, 360.0f);
	}

	render_frame();
	usleep(1000);
}

int
main(int argc, char *argv[])
{
	x_t x;
	refimport_t ri;
	int i;

	(void)argc;
	(void)argv;

	/* white lightstyles so any style-modulated path reads fullbright */
	for (i = 0; i < MAX_LIGHTSTYLES; ++i)
	{
		g_lightstyles[i].rgb[0] = 1.0f;
		g_lightstyles[i].rgb[1] = 1.0f;
		g_lightstyles[i].rgb[2] = 1.0f;
		g_lightstyles[i].white = 3.0f;
	}

	x_init(&x, NULL);
	x.on_loop = loop;

	g_xwin = xwin_open(&x, -1, 32, 32, g_win_w, g_win_h, "yquake2 gl3", XWIN_STYLE_NORMAL);
	if (!g_xwin)
	{
		printf("yquake2: failed to open window\n");
		return 1;
	}
	g_xwin->on_event = on_event;

	/* Wire the host imports, then get the refresher exports.  GetRefAPI()
	 * copies `ri` into the refresher's global and returns the export table. */
	R_InitRefimport(&ri);
	re = GetRefAPI(ri);

	if (re.api_version != API_VERSION)
	{
		printf("yquake2: refresher api version mismatch (%d != %d)\n",
		       re.api_version, API_VERSION);
		return 1;
	}

	/* re.Init() creates the EGL context on g_xwin (via GLimp_InitGraphics),
	 * loads colormap.pcx and compiles the shaders. */
	if (!re.Init())
	{
		printf("yquake2: re.Init() failed\n");
		return 1;
	}

	/* No BeginRegistration()/EndRegistration(): those require a valid .bsp
	 * world.  RegisterModel() is graceful (crash=false) and works against the
	 * registration_sequence GL3_Init() already established. */
	g_model = re.RegisterModel(YQ2_MODEL_NAME);
	if (g_model == NULL)
		printf("yquake2: note - '%s' not loaded, rendering empty scene\n", YQ2_MODEL_NAME);

	xwin_set_visible(g_xwin, true);
	x_run(&x, g_xwin);

	re.Shutdown();

	xwin_destroy(g_xwin);
	return 0;
}
