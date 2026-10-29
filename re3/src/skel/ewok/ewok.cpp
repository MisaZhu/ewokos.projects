// EwokOS platform skeleton for re3 (GTA III).
// Modelled on src/skel/glfw/glfw.cpp: same game-flow state machine, but the
// window/input/timer layer talks to the EwokOS X-like window server (libx)
// instead of GLFW, and the GL context comes from the EGL surface that
// vendor/librw/src/gl/gl3device.cpp creates in its LIBRW_EWOKOS branch.
//
// Input model:
//   - keyboard: XEVT_IM press/release -> RsKeyboardEventHandler(rsKEYDOWN/UP)
//   - mouse:    XEVT_MOUSE move/drag/down/up -> absolute position + button
//               state; CPad::UpdateMouse() (via the ewokGetCursorPos /
//               ewokGetMouseButton shims in crossplatform.h) turns that into
//               per-frame deltas like the glfw build.
//   - no joystick support (CapturePad keeps the glfw no-gamepad early return).
// Audio goes through sampman_null (no AUDIO_OAL/AUDIO_MSS defined).

#if defined RW_GL3

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>

#include "common.h"
#include <stdio.h>
#include "rwcore.h"
#include "skeleton.h"
#include "platform.h"
#include "crossplatform.h"

#include "main.h"
#include "FileMgr.h"
#include "Text.h"
#include "Pad.h"
#include "Timer.h"
#include "DMAudio.h"
#include "ControllerConfig.h"
#include "Frontend.h"
#include "Game.h"
#include "PCSave.h"
#include "Sprite2d.h"
#include "AnimViewer.h"
#include "Font.h"
#include "MemoryMgr.h"
#include "CutsceneMgr.h"
#include "Camera.h"

#include <x/x.h>
#include <x/xwin.h>
#include <ewoksys/keydef.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/thread.h>

// Exported by libGLESv2: pin all rendering to the CPU interpreter.
extern "C" void glForceCpuEWOK(void);

#define MAX_SUBSYSTEMS		(16)

rw::EngineOpenParams openParams;

static RwBool		  ForegroundApp = TRUE;
static RwBool		  WindowIconified = FALSE;
static RwBool		  WindowFocused = TRUE;

static RwBool		  RwInitialised = FALSE;

static RwSubSystemInfo GsubSysInfo[MAX_SUBSYSTEMS];
static RwInt32		GnumSubSystems = 0;
static RwInt32		GcurSel = 0, GcurSelVM = 0;

static RwBool useDefault;

static psGlobalType PsGlobal;

#define PSGLOBAL(var) (((psGlobalType *)(RsGlobal.ps))->var)

size_t _dwMemAvailPhys;
long _dwOperatingSystemVersion;
RwUInt32 gGameState;

// DETECT_JOYSTICK_MENU keeps a name buffer even though EwokOS has no
// joysticks (MenuScreensCustom references it).
char gSelectedJoystickName[128] = "";

// newlib declares iswdigit() in <wctype.h> but does not implement it.
// The only consumer is ASCII anim-association name matching.
extern "C" int iswdigit(wint_t wc)
{
	return wc >= '0' && wc <= '9';
}

bool lshiftStatus = false;
bool rshiftStatus = false;
bool ctrlStatus = false;

// libx window-server connection + our main window. The window itself is
// created by librw's startEwok() while the game is inside RsRwInitialize.
static x_t g_x;
static bool g_xInitialised = false;
static volatile bool g_ewokQuit = false;
static bool g_borderlessFullscreen = false;
static int g_mainThread = -1;
static bool g_fullscreenKeyDown = false;
static bool g_renderBenchmark = false;
static xinfo_t g_windowedInfo, g_windowedPrevious;

// Mouse state fed by XEVT_MOUSE, read back by CPad::UpdateMouse through the
// ewokGetCursorPos/ewokGetMouseButton shims. X starts at 1 so that
// CMousePointerStateHelper::GetMouseSetUp() (which treats pos 0 as "no
// mouse") sees a mouse before the first real event arrives.
static double g_mouseX = 1.0, g_mouseY = 0.0;
static bool g_mouseButtons[8];

// librw's gl3device (LIBRW_EWOKOS branch) resolves these two by C linkage.
extern "C" x_t *ewok_get_x(void) { return &g_x; }

void ewokGetCursorPos(double *x, double *y)
{
	*x = g_mouseX;
	*y = g_mouseY;
}

int ewokGetMouseButton(int button)
{
	// GLFW order: 0=left 1=right 2=middle. EwokOS MOUSE_BUTTON_*: 1=left
	// 2=middle 3=right.
	static const int ewokButton[5] = {
		MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MID, 0, 0
	};
	if (button < 0 || button > 4)
		return 0;
	return g_mouseButtons[ewokButton[button]] ? 1 : 0;
}

static int ewokWinWidth(void)
{
	xwin_t *win = (xwin_t*)PSGLOBAL(window);
	if (win == nil || win->xinfo == nil)
		return RsGlobal.maximumWidth;
	return win->xinfo->wsr.w;
}

static int ewokWinHeight(void)
{
	xwin_t *win = (xwin_t*)PSGLOBAL(window);
	if (win == nil || win->xinfo == nil)
		return RsGlobal.maximumHeight;
	return win->xinfo->wsr.h;
}

// ---- window callbacks (wired in psPostRWinit) -------------------------------

static bool ewokOnClose(xwin_t *xwin)
{
	g_ewokQuit = true;
	return true;
}

static void ewokOnFocus(xwin_t *xwin)
{
	WindowFocused = TRUE;
	ForegroundApp = TRUE;
}

static void ewokOnUnfocus(xwin_t *xwin)
{
	WindowFocused = FALSE;
	ForegroundApp = FALSE;
	g_fullscreenKeyDown = false;
	lshiftStatus = rshiftStatus = ctrlStatus = false;
	/* The window server stops sending button-up events when focus changes.
	 * Clear the cached state so a click held during an alt-tab cannot remain
	 * stuck after the game is focused again. */
	memset(g_mouseButtons, 0, sizeof(g_mouseButtons));
	PSGLOBAL(mouseWheel) = 0.0;
	RsEventHandler(rsACTIVATE, (void *)FALSE);
}

static void ewokOnMin(xwin_t *xwin)
{
	WindowIconified = TRUE;
}

static void ewokOnShow(xwin_t *xwin)
{
	WindowIconified = FALSE;
}

static void ewokOnResize(xwin_t *xwin)
{
	if (xwin->xinfo == nil)
		return;
	int width = xwin->xinfo->wsr.w;
	int height = xwin->xinfo->wsr.h;
	if (height <= 0 || width <= 0)
		return;
	/* Keep the camera and GPU target at the logical game size.  Fullscreen
	 * scaling is performed by EGL when the finished frame is presented. */
	if (getenv("RE3_NATIVE_RENDER_SIZE") != nil)
		return;

	RsGlobal.maximumHeight = height;
	RsGlobal.maximumWidth = width;
	/* xwin_fullscreen() is called while the RenderWare/EGL surface is still
	 * being created.  The window server invokes this callback once during that
	 * transition, before Scene.camera exists; defer the camera event until the
	 * normal post-initialisation path has a camera to resize. */
	if (Scene.camera == nil)
		return;

	RwRect r;
	r.x = 0;
	r.y = 0;
	r.w = width;
	r.h = height;
	RsEventHandler(rsCAMERASIZE, &r);
}

static void ewokToggleFullscreen(xwin_t *win)
{
	if (win == nil || win->xinfo == nil)
		return;
	if (!g_borderlessFullscreen) {
		g_windowedInfo = *win->xinfo;
		g_windowedPrevious = win->xinfo_prev;
		xwin_fullscreen(win);
		g_borderlessFullscreen = true;
	} else {
		// Restore through libx's geometry handoff, never copy stale canvas ids
		// over the live xinfo. Retain the normal restore rect if we entered
		// fullscreen from an already maximized window.
		win->xinfo->style = g_windowedInfo.style;
		win->xinfo_prev = g_windowedInfo;
		xevent_t ev;
		memset(&ev, 0, sizeof(ev));
		ev.type = XEVT_WIN;
		ev.value.window.event = XEVT_WIN_MAX;
		xwin_event_handle(win, &ev);
		win->xinfo_prev = g_windowedPrevious;
		g_borderlessFullscreen = false;
	}
	PSGLOBAL(fullScreen) = g_borderlessFullscreen;
}

// ---- input ------------------------------------------------------------------

// EwokOS keydef.h values that don't fall in the printable ASCII range.
// Everything 32..126 is mapped straight to its uppercase ASCII counterpart.
#define EWOK_KEY_HOME		0xF0
#define EWOK_KEY_END		0xF1
#define EWOK_KEY_CTRL		0x11

static int keymap[256];

static void
initkeymap(void)
{
	int i;
	for (i = 0; i < 256; i++)
		keymap[i] = rsNULL;

	// printable ASCII -> uppercase counterpart (as the glfw/X11 maps do)
	for (i = 'a'; i <= 'z'; i++)
		keymap[i] = i - 'a' + 'A';
	for (i = 'A'; i <= 'Z'; i++)
		keymap[i] = i;
	for (i = '0'; i <= '9'; i++)
		keymap[i] = i;

	keymap[' '] = ' ';
	keymap['\''] = '\'';
	keymap['&'] = '&';
	keymap['%'] = '%';
	keymap['$'] = '$';
	keymap[','] = ',';
	keymap['-'] = '-';
	keymap['.'] = '.';
	keymap['/'] = '/';
	keymap['?'] = '?';
	keymap['!'] = '!';
	keymap['"'] = '"';
	keymap[':'] = ':';
	keymap[';'] = ';';
	keymap['='] = '=';
	keymap['['] = '[';
	keymap['\\'] = '\\';
	keymap[']'] = ']';
	keymap['`'] = '`';
	keymap['('] = '(';
	keymap[')'] = ')';
	keymap['*'] = '*';
	keymap['+'] = '+';
	keymap['<'] = '<';
	keymap['>'] = '>';
	keymap['@'] = '@';
	keymap['^'] = '^';
	keymap['_'] = '_';
	keymap['|'] = '|';
	keymap['~'] = '~';
	keymap['#'] = '#';
	keymap['{'] = '{';
	keymap['}'] = '}';

	keymap[KEY_ESC] = rsESC;
	keymap[KEY_ENTER] = rsENTER;
	keymap[KEY_TAB] = rsTAB;
	keymap[KEY_BACKSPACE] = rsBACKSP;
	keymap[KEY_UP] = rsUP;
	keymap[KEY_DOWN] = rsDOWN;
	keymap[KEY_LEFT] = rsLEFT;
	keymap[KEY_RIGHT] = rsRIGHT;
	keymap[EWOK_KEY_HOME] = rsHOME;
	keymap[EWOK_KEY_END] = rsEND;
	keymap[EWOK_KEY_CTRL] = rsLCTRL;
	keymap[KEY_LSHIFT] = rsLSHIFT;
	keymap[KEY_RSHIFT] = rsRSHIFT;
}

static void
ewokKeyboardEvent(int value, bool pressed)
{
	if (value < 0 || value > 255)
		return;

	int rs = keymap[value];
	if (rs == rsNULL)
		return;

	if (rs == rsLSHIFT)
		lshiftStatus = pressed;
	else if (rs == rsRSHIFT)
		rshiftStatus = pressed;
	else if (rs == rsLCTRL)
		ctrlStatus = pressed;

	RsKeyboardEventHandler(pressed ? rsKEYDOWN : rsKEYUP, &rs);
}

static void
ewokImEvent(xevent_t *ev)
{
	if (g_renderBenchmark) return;
	bool pressed = ev->state == XIM_STATE_PRESS;
	// EwokOS has no F11/Alt key codes. Ctrl+Enter toggles borderless
	// fullscreen; consume both halves so Enter cannot activate a menu item.
	if (ev->value.im.key_code == KEY_ENTER || ev->value.im.value == KEY_ENTER) {
		if (pressed && (ev->value.im.ctrl || ctrlStatus) && !g_fullscreenKeyDown) {
			g_fullscreenKeyDown = true;
			ewokToggleFullscreen((xwin_t*)PSGLOBAL(window));
			return;
		}
		if (g_fullscreenKeyDown) {
			if (!pressed) g_fullscreenKeyDown = false;
			return;
		}
	}

	// Some input methods attach held modifiers to every key; xim_none only
	// flags the modifier press itself. Keep it held until its release instead
	// of treating a zero flag on the next key as a release.
	bool shiftNow = ev->value.im.shift != 0;
	if (pressed && shiftNow && !(lshiftStatus || rshiftStatus)) {
		lshiftStatus = true;
		int rs = rsLSHIFT;
		RsKeyboardEventHandler(rsKEYDOWN, &rs);
	}
	bool ctrlNow = ev->value.im.ctrl != 0;
	if (pressed && ctrlNow && !ctrlStatus) {
		ctrlStatus = true;
		int rs = rsLCTRL;
		RsKeyboardEventHandler(rsKEYDOWN, &rs);
	}

	ewokKeyboardEvent(ev->value.im.value, pressed);
}

static void
ewokMouseEvent(xwin_t *xwin, xevent_t *ev)
{
	if (g_renderBenchmark) return;
	/* xserverd reports screen coordinates. RE3's mouse code and the frontend
	 * use coordinates relative to the game window, so remove the window's
	 * screen origin before caching the position. This is essential when the
	 * window is moved or restored from fullscreen. */
	if (ev->value.mouse.x >= 0 && ev->value.mouse.y >= 0) {
		int originX = 0, originY = 0;
		if (xwin != nil && xwin->xinfo != nil) {
			originX = xwin->xinfo->wsr.x;
			originY = xwin->xinfo->wsr.y;
		}
		g_mouseX = ev->value.mouse.x - originX;
		g_mouseY = ev->value.mouse.y - originY;
		int width = ewokWinWidth();
		int height = ewokWinHeight();
		if (width > 0) {
			if (g_mouseX < 0) g_mouseX = 0;
			if (g_mouseX >= width) g_mouseX = width - 1;
		}
		if (height > 0) {
			if (g_mouseY < 0) g_mouseY = 0;
			if (g_mouseY >= height) g_mouseY = height - 1;
		}
		PSGLOBAL(cursorIsInWindow) = TRUE;
	}

	switch (ev->state) {
	case MOUSE_STATE_DOWN:
		if (ev->value.mouse.button > 0 && ev->value.mouse.button < 8)
			g_mouseButtons[ev->value.mouse.button] = true;
		if (ev->value.mouse.button == MOUSE_BUTTON_SCROLL_UP)
			PSGLOBAL(mouseWheel) += 1.0;
		else if (ev->value.mouse.button == MOUSE_BUTTON_SCROLL_DOWN)
			PSGLOBAL(mouseWheel) -= 1.0;
		break;
	case MOUSE_STATE_UP:
		if (ev->value.mouse.button > 0 && ev->value.mouse.button < 8)
			g_mouseButtons[ev->value.mouse.button] = false;
		break;
	case MOUSE_STATE_MOVE:
	case MOUSE_STATE_DRAG:
		break;
	}

	// Front-end needs the scaled pointer position every frame (this is what
	// the glfw cursorCB does; it only matters while the menu is up).
	if (FrontEndMenuManager.m_bMenuActive) {
		int winw = ewokWinWidth();
		int winh = ewokWinHeight();
		if (winw > 0 && winh > 0) {
			FrontEndMenuManager.m_nMouseTempPosX = g_mouseX * (RsGlobal.maximumWidth / winw);
			FrontEndMenuManager.m_nMouseTempPosY = g_mouseY * (RsGlobal.maximumHeight / winh);
		}
	}
}

static void
ewokOnEvent(xwin_t *xwin, xevent_t *ev)
{
	if (ev->type == XEVT_IM)
		ewokImEvent(ev);
	else if (ev->type == XEVT_MOUSE)
		ewokMouseEvent(xwin, ev);
}

// Drain all pending window-server events (the glfwPollEvents equivalent).
static void
ewokPumpEvents(void)
{
	xevent_t ev;
	// A steady input stream must not prevent the next render/present.
	for (int i = 0; i < 64 && x_poll_event(&g_x, &ev) == 0; ++i)
		xwin_dispatch_event(&g_x, &ev);
	xwin_retry_pending_presents();
}

// Streaming waits can outlive a frame by several seconds. Service them on
// the same thread as rendering so EGL and RenderWare never resize in parallel.
extern "C" void ewok_poll_loading_events(void)
{
	static bool active = false;
	if (thread_get_id() != g_mainThread || gGameState != GS_INIT_PLAYING_GAME || active)
		return;
	active = true;
	int width = ewokWinWidth(), height = ewokWinHeight();
	ewokPumpEvents();
	if (g_ewokQuit)
		_exit(0);
	if (Scene.camera && (width != ewokWinWidth() || height != ewokWinHeight()))
		LoadingScreen(nil, nil, nil);
	active = false;
}

// Called by librw's startEwok() right after the window exists, before any
// events can arrive.
extern "C" void
ewok_on_window_created(xwin_t *xwin)
{
	g_borderlessFullscreen = false;
	g_fullscreenKeyDown = false;
	xwin->on_event = ewokOnEvent;
	xwin->on_close = ewokOnClose;
	xwin->on_focus = ewokOnFocus;
	xwin->on_unfocus = ewokOnUnfocus;
	xwin->on_min = ewokOnMin;
	xwin->on_show = ewokOnShow;
	xwin->on_resize = ewokOnResize;
	/* RE3 presents synchronously.  Set this before the first fullscreen
	 * rebuild, rather than in psPostRWinit after EGL creation, so xserverd does
	 * not allocate a second full-screen handoff canvas for a GUI-launched
	 * process.  That transient allocation is enough to exhaust q8b's shared
	 * graphics memory when launched from xlauncher or xterm. */
	if (xwin->xinfo != nil) {
		xwin->xinfo->fps_async_force_sync = true;
		xwin->xinfo->fps_async = false;
	}

	/* RE3 starts in fullscreen mode.  This must happen before EGL creates its
	 * surface so the GPU backbuffer is allocated at the final workspace size;
	 * resizing an already-created surface can require a large contiguous q8b
	 * allocation and may force an avoidable CPU fallback.  RE3_WINDOWED=1 is a
	 * deliberate debugging escape hatch and keeps the old windowed behaviour. */
	if (getenv("RE3_WINDOWED") == NULL &&
			xwin->xinfo->state != XWIN_STATE_MAX) {
		g_windowedInfo = *xwin->xinfo;
		g_windowedPrevious = xwin->xinfo_prev;
		xwin_fullscreen(xwin);
		g_borderlessFullscreen = true;
		PSGLOBAL(fullScreen) = TRUE;
	} else if (xwin->xinfo->state == XWIN_STATE_MAX) {
		/* XWIN_STYLE_MAX made xwin_open allocate the final fullscreen
		 * workspace atomically.  Do not call xwin_fullscreen here: that would
		 * issue a synchronous repaint before EGL and the on_repaint callback
		 * exist, which can spin forever when launched from xterm. */
		g_windowedInfo = *xwin->xinfo;
		g_windowedPrevious = xwin->xinfo_prev;
		g_borderlessFullscreen = true;
		PSGLOBAL(fullScreen) = TRUE;
	}
}

// ---- skeleton platform API --------------------------------------------------

void _psCreateFolder(const char *path)
{
	mkdir(path, 0755);
}

const char *_psGetUserFilesFolder()
{
	static char szUserFiles[256];
	strcpy(szUserFiles, "userfiles");
	_psCreateFolder(szUserFiles);
	return szUserFiles;
}

RwBool
psCameraBeginUpdate(RwCamera *camera)
{
	if ( !RwCameraBeginUpdate(Scene.camera) )
	{
		ForegroundApp = FALSE;
		RsEventHandler(rsACTIVATE, (void *)FALSE);
		return FALSE;
	}

	return TRUE;
}

void
psCameraShowRaster(RwCamera *camera)
{
	if (CMenuManager::m_PrefsVsync)
		RwCameraShowRaster(camera, PSGLOBAL(window), rwRASTERFLIPWAITVSYNC);
	else
		RwCameraShowRaster(camera, PSGLOBAL(window), rwRASTERFLIPDONTWAIT);
}

RwImage *
psGrabScreen(RwCamera *pCamera)
{
	rw::Image *image = RwCameraGetRaster(pCamera)->toImage();
	image->removeMask();
	if(image)
		return image;
	return nil;
}

double
psTimer(void)
{
	return (double)kernel_tic_ms(0);
}

void
psMouseSetPos(RwV2d *pos)
{
	// No cursor warp in the window server; just resync our tracked position
	// so the next UpdateMouse() delta is measured from here.
	PSGLOBAL(lastMousePos.x) = (RwInt32)pos->x;
	PSGLOBAL(lastMousePos.y) = (RwInt32)pos->y;
	g_mouseX = pos->x;
	g_mouseY = pos->y;
}

RwMemoryFunctions*
psGetMemoryFunctions(void)
{
	return nil;
}

RwBool
psInstallFileSystem(void)
{
	return (TRUE);
}

RwBool
psNativeTextureSupport(void)
{
	return true;
}

RwBool
psInitialize(void)
{
	PsGlobal.lastMousePos.x = PsGlobal.lastMousePos.y = 0.0f;

	RsGlobal.ps = &PsGlobal;

	PsGlobal.fullScreen = FALSE;
	PsGlobal.cursorIsInWindow = TRUE;
	WindowFocused = TRUE;
	WindowIconified = FALSE;

	PsGlobal.joy1id	= -1;
	PsGlobal.joy2id	= -1;

	CFileMgr::Initialise();

	CPad::Initialise();
	CPad::GetPad(0)->Mode = 0;

	CGame::frenchGame = false;
	CGame::germanGame = false;
	CGame::nastyGame = true;
	CMenuManager::m_PrefsAllowNastyGame = true;

	C_PcSave::SetSaveDirectory(_psGetUserFilesFolder());

	InitialiseLanguage();

#if GTA_VERSION < GTA3_PC_11
	FrontEndMenuManager.LoadSettings();
#endif

	gGameState = GS_START_UP;
	TRACE("gGameState = GS_START_UP");

	_dwOperatingSystemVersion = OS_WINXP; // To fool other classes

#if GTA_VERSION >= GTA3_PC_11
	FrontEndMenuManager.LoadSettings();
#endif

	// No sysinfo() on EwokOS - report a fixed budget so streaming sizes itself
	// as on a low-memory PC.
	_dwMemAvailPhys = 256 * 1024 * 1024;
	debug("Available physical memory %u\n", _dwMemAvailPhys);

	TheText.Unload();

	return TRUE;
}

void
psTerminate(void)
{
	return;
}

static RwChar **_VMList;

RwInt32 _psGetNumVideModes()
{
	return RwEngineGetNumVideoModes();
}

RwBool _psFreeVideoModeList()
{
	RwInt32 numModes;
	RwInt32 i;

	numModes = _psGetNumVideModes();

	if ( _VMList == nil )
		return TRUE;

	for ( i = 0; i < numModes; i++ )
	{
		RwFree(_VMList[i]);
	}

	RwFree(_VMList);

	_VMList = nil;

	return TRUE;
}

RwChar **_psGetVideoModeList()
{
	RwInt32 numModes;
	RwInt32 i;

	if ( _VMList != nil )
	{
		return _VMList;
	}

	numModes = RwEngineGetNumVideoModes();

	_VMList = (RwChar **)RwCalloc(numModes, sizeof(RwChar*));

	for ( i = 0; i < numModes; i++	)
	{
		RwVideoMode			vm;

		RwEngineGetVideoModeInfo(&vm, i);

		if ( vm.flags & rwVIDEOMODEEXCLUSIVE )
		{
			_VMList[i] = (RwChar*)RwCalloc(100, sizeof(RwChar));
			rwsprintf(_VMList[i],"%d X %d X %d", vm.width, vm.height, vm.depth);
		}
		else
			_VMList[i] = nil;
	}

	return _VMList;
}

void _psSelectScreenVM(RwInt32 videoMode)
{
	RwTexDictionarySetCurrent( nil );

	FrontEndMenuManager.UnloadTextures();

	if (!_psSetVideoMode(RwEngineGetCurrentSubSystem(), videoMode))
	{
		RsGlobal.quit = TRUE;

		printf("ERROR: Failed to select new screen resolution\n");
	}
	else
		FrontEndMenuManager.LoadAllTextures();
}

RwBool IsForegroundApp()
{
	return !!ForegroundApp;
}

RwBool
psSelectDevice()
{
	RwVideoMode			vm;
	RwInt32				subSysNum;

	if ( !useDefault )
	{
		GnumSubSystems = RwEngineGetNumSubSystems();
		if ( !GnumSubSystems )
		{
			 return FALSE;
		}

		/* Just to be sure ... */
		GnumSubSystems = (GnumSubSystems > MAX_SUBSYSTEMS) ? MAX_SUBSYSTEMS : GnumSubSystems;

		/* Get the names of all the sub systems */
		for (subSysNum = 0; subSysNum < GnumSubSystems; subSysNum++)
		{
			RwEngineGetSubSystemInfo(&GsubSysInfo[subSysNum], subSysNum);
		}

		/* Get the default selection */
		GcurSel = RwEngineGetCurrentSubSystem();
	}

	/* Set the driver to use the correct sub system */
	if (!RwEngineSetSubSystem(GcurSel))
	{
		return FALSE;
	}

	// The EwokOS device exposes one resizable windowed mode. The desktop
	// controls its geometry; no exclusive 640x480 mode search is needed.
	GcurSelVM = 0;
	FrontEndMenuManager.m_nPrefsVideoMode = 0;
	FrontEndMenuManager.m_nDisplayVideoMode = 0;

	RwEngineGetVideoModeInfo(&vm, GcurSelVM);

#ifndef PS2_MENU
	FrontEndMenuManager.m_nCurrOption = 0;
#endif

	/* Set up the video mode and set the apps window
	* dimensions to match */
	if (!RwEngineSetVideoMode(GcurSelVM))
	{
		return FALSE;
	}

	if (vm.flags & rwVIDEOMODEEXCLUSIVE)
	{
		RsGlobal.maximumWidth = vm.width;
		RsGlobal.maximumHeight = vm.height;
		RsGlobal.width = vm.width;
		RsGlobal.height = vm.height;

		PSGLOBAL(fullScreen) = TRUE;
	}

	return TRUE;
}

long _InputInitialiseMouse()
{
	return 0;
}

void _InputInitialiseJoys()
{
	PSGLOBAL(joy1id) = -1;
	PSGLOBAL(joy2id) = -1;
}

void psPostRWinit(void)
{
	RwVideoMode vm;
	RwEngineGetVideoModeInfo(&vm, GcurSelVM);

	xwin_t *win = (xwin_t*)PSGLOBAL(window);

	if (win != nil) {
		/* RenderWare's EGL surface is opaque.  Keep the X window explicitly
		 * opaque so the compositor cannot blend the initial empty workspace
		 * with the desktop while the first raster is being presented. */
		xwin_set_alpha(win, false);
		/* re3 renders directly into the EGL window workspace.  The asynchronous
		 * xserver handoff can leave that first workspace in the compositor's
		 * initial empty buffer while the game is still starting; use the blocking
		 * present handshake for this client so every swap is consumed before the
		 * next frame is rendered. */
		if (win->xinfo != NULL)
			win->xinfo->fps_async = false;
		xwin_set_visible(win, true);
		xwin_top(win);

	}

	_InputInitialiseJoys();
	_InputInitialiseMouse();

	if(!(vm.flags & rwVIDEOMODEEXCLUSIVE)) {
		// Windowed mode: adopt the game's preferred size.
		RsGlobal.maximumWidth = vm.width;
		RsGlobal.maximumHeight = vm.height;
	}

	// Make sure all keys are released
	CPad::GetPad(0)->Clear(true);
	CPad::GetPad(1)->Clear(true);
}

RwBool _psSetVideoMode(RwInt32 subSystem, RwInt32 videoMode)
{
	RwInitialised = FALSE;

	RsEventHandler(rsRWTERMINATE, nil);

	GcurSel = subSystem;
	GcurSelVM = videoMode;

	useDefault = TRUE;

	if ( RsEventHandler(rsRWINITIALIZE, &openParams) == rsEVENTERROR )
		return FALSE;

	RwInitialised = TRUE;
	useDefault = FALSE;

	RwRect r;

	r.x = 0;
	r.y = 0;
	r.w = RsGlobal.maximumWidth;
	r.h = RsGlobal.maximumHeight;

	RsEventHandler(rsCAMERASIZE, &r);

	psPostRWinit();

	return TRUE;
}

void InitialiseLanguage()
{
	// EwokOS has no locale environment; always US English.
	CMenuManager::OS_Language = LANG_ENGLISH;
	CMenuManager::m_PrefsLanguage = CMenuManager::LANGUAGE_AMERICAN;

	TheText.Unload();
	TheText.Load();
}

void HandleExit()
{
	return;
}

// R* calls that in ControllerConfig, idk why
void
_InputTranslateShiftKeyUpDown(RsKeyCodes *rs) {
	RsKeyboardEventHandler(lshiftStatus ? rsKEYDOWN : rsKEYUP, &(*rs = rsLSHIFT));
	RsKeyboardEventHandler(rshiftStatus ? rsKEYDOWN : rsKEYUP, &(*rs = rsRSHIFT));
}

void CapturePad(RwInt32 padID)
{
	// No joystick support on EwokOS - behave exactly like the glfw skeleton
	// when no gamepad is connected (early return keeps CPad's keyboard-only
	// path).
	int8 pad = -1;

	if( padID == 0 )
		pad = PSGLOBAL(joy1id);
	else if( padID == 1)
		pad = PSGLOBAL(joy2id);
	else
		return;

	if ( pad == -1 )
		return;
}

void joysChangeCB(int jid, int event)
{
}

// ---- main -------------------------------------------------------------------

static bool
ewokInitWindowServer(void)
{
	g_mainThread = thread_get_id();
	x_init(&g_x, NULL);
	g_xInitialised = true;
	return true;
}

int main(int argc, char *argv[])
{
	RwV2d pos;
	RwInt32 i;

#ifdef __ewokos__
	/* Render at the original logical size and scale only during presentation;
	 * this avoids allocating a full display-sized GPU target for GUI launches. */
	setenv("RE3_NATIVE_RENDER_SIZE", "640x480", 1);
	g_renderBenchmark = getenv("RE3_RENDER_BENCHMARK") != NULL;
	/* RW emits many small native vertex draws. Waking seven threads for
	 * each one costs more than evaluating them on the render thread on Q8B.
	 * Keep the indexed vertex cache; an explicit setting still selects 1..8. */
	if (!getenv("PGL_VERTEX_THREADS"))
		setenv("PGL_VERTEX_THREADS", "1", 0);
	// Explicit CPU reference mode; normal launches retain GPU rasterization.
	if (getenv("RE3_FORCE_CPU"))
		glForceCpuEWOK();   // RE3_FORCE_CPU=1 pins the CPU interpreter
	{
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = [](int sig) {
			const char msg[] = "re3: FATAL signal caught\n";
			write(2, msg, sizeof(msg) - 1);
			_exit(99);
		};
		sa.sa_flags = SA_RESTART;
		sigaction(SIGSEGV, &sa, NULL);
		sigaction(SIGBUS, &sa, NULL);
		sigaction(SIGILL, &sa, NULL);
	}
#endif

	// Connect to the window server before librw's startEwok() opens the
	// game window on it.
	ewokInitWindowServer();

	/*
	 * Initialize the platform independent data.
	 * This will in turn initialize the platform specific data...
	 */
	if( RsEventHandler(rsINITIALIZE, nil) == rsEVENTERROR )
	{
		return FALSE;
	}

	for(i=1; i<argc; i++)
	{
		RsEventHandler(rsPREINITCOMMANDLINE, argv[i]);
	}

	/*
	 * Parameters to be used in RwEngineOpen / rsRWINITIALISE event
	 */
	openParams.width = RsGlobal.maximumWidth;
	openParams.height = RsGlobal.maximumHeight;
	openParams.windowtitle = RsGlobal.appName;
	openParams.window = &PSGLOBAL(window);

	ControlsManager.MakeControllerActionsBlank();
	ControlsManager.InitDefaultControlConfiguration();

	/*
	 * Initialize the 3D (RenderWare) components of the app...
	 * This creates the xwin window and the EGL context (librw startEwok).
	 */
	if( rsEVENTERROR == RsEventHandler(rsRWINITIALIZE, &openParams) )
	{
		RsEventHandler(rsTERMINATE, nil);

		return 0;
	}

	psPostRWinit();

	ControlsManager.InitDefaultControlConfigMouse(MousePointerStateHelper.GetMouseSetUp());

	/*
	 * Parse command line parameters (except program name) one at
	 * a time AFTER RenderWare initialization...
	 */
	for(i=1; i<argc; i++)
	{
		RsEventHandler(rsCOMMANDLINE, argv[i]);
	}

	/*
	 * Force a camera resize event...
	 */
	{
		RwRect r;

		r.x = 0;
		r.y = 0;
		r.w = RsGlobal.maximumWidth;
		r.h = RsGlobal.maximumHeight;

		RsEventHandler(rsCAMERASIZE, &r);
	}

	{
		CFileMgr::SetDirMyDocuments();

#ifdef LOAD_INI_SETTINGS
		// At this point InitDefaultControlConfigJoyPad must have set all bindings to default and ms_padButtonsInited to number of detected buttons.
		// We will load stored bindings below, but let's cache ms_padButtonsInited before LoadINIControllerSettings and LoadSettings clears it,
		// so we can add new joy bindings **on top of** stored bindings.
		int connectedPadButtons = ControlsManager.ms_padButtonsInited;
#endif

		int32 gta3set = CFileMgr::OpenFile("gta3.set", "r");

		if ( gta3set )
		{
			ControlsManager.LoadSettings(gta3set);
			CFileMgr::CloseFile(gta3set);
		}

		CFileMgr::SetDir("");

#ifdef LOAD_INI_SETTINGS
		LoadINIControllerSettings();
		if (connectedPadButtons != 0)
			ControlsManager.InitDefaultControlConfigJoyPad(connectedPadButtons); // add (connected-saved) amount of new button assignments on top of ours

		// these have 2 purposes: creating .ini at the start, and adding newly introduced settings to old .ini at the start
		SaveINISettings();
		SaveINIControllerSettings();
#endif
	}

	initkeymap();

	// Wait for the window server to publish our window info (as x_run does
	// before entering its loop).
	while (g_xInitialised) {
		if (RsGlobal.ps != nil && PSGLOBAL(window) != nil) {
			xwin_t *win = (xwin_t*)PSGLOBAL(window);
			if (win->xinfo != NULL) {
				break;
			}
		}
		usleep(10000);
	}

	while ( TRUE )
	{
		RwInitialised = TRUE;

		/*
		* Set the initial mouse position...
		*/
		pos.x = RsGlobal.maximumWidth * 0.5f;
		pos.y = RsGlobal.maximumHeight * 0.5f;

		RsMouseSetPos(&pos);

		/*
		* Enter the message processing loop...
		*/

#ifndef MASTER
		if (gbModelViewer) {
			// This is TheModelViewer in LCS, but not compiled on III Mobile.
			LoadingScreen("Loading the ModelViewer", NULL, GetRandomSplashScreen());
			CAnimViewer::Initialise();
			CTimer::Update();
#ifndef PS2_MENU
			FrontEndMenuManager.m_bGameNotLoaded = false;
#endif
		}
#endif

		while( !RsGlobal.quit && !FrontEndMenuManager.m_bWantToRestart && !g_ewokQuit)
		{
			ewokPumpEvents();
			if (g_ewokQuit || RsGlobal.quit)
				break;

#ifndef MASTER
			if (gbModelViewer) {
				// This is TheModelViewerCore in LCS, but TheModelViewer on other state-machine III-VCs.
				TheModelViewer();
			} else
#endif
			if ( ForegroundApp )
			{
				switch ( gGameState )
				{
					case GS_START_UP:
					{
						gGameState = GS_INIT_ONCE;
						TRACE("gGameState = GS_INIT_ONCE");
						break;
					}

					case GS_INIT_ONCE:
					{

						LoadingScreen(nil, nil, "loadsc0");

						if ( !CGame::InitialiseOnceAfterRW() )
						{
							printf("re3: InitialiseOnceAfterRW FAILED, quitting\n");
							RsGlobal.quit = TRUE;
						}


						gGameState = GS_INIT_FRONTEND;
						TRACE("gGameState = GS_INIT_FRONTEND;");
						break;
					}

					case GS_INIT_FRONTEND:
					{

						LoadingScreen(nil, nil, "loadsc0");

						FrontEndMenuManager.m_bGameNotLoaded = true;

						CMenuManager::m_bStartUpFrontEndRequested = true;

						gGameState = GS_FRONTEND;
						TRACE("gGameState = GS_FRONTEND;");
						break;
					}

					case GS_FRONTEND:
					{
						/* The board has no X-event injection tool.  Keep the normal
						 * frontend path unchanged, but allow automated render tests to
						 * enter the real world scene without synthesising a mouse/Enter
						 * event through the window server. */
						if ((g_renderBenchmark || getenv("RE3_AUTOSTART") != NULL) &&
							FrontEndMenuManager.m_bGameNotLoaded) {
							/* Automated board runs enter the world without a menu
							 * event.  Clear the pending frontend request as well as the
							 * active flag, otherwise the first game tick calls
							 * LoadAllTextures() and can block on a multi-megabyte MENU.TXD
							 * before any gameplay frame reaches the GPU. */
						FrontEndMenuManager.m_bMenuActive = false;
						CMenuManager::m_bStartUpFrontEndRequested = false;
							gGameState = GS_INIT_PLAYING_GAME;
							TRACE("gGameState = GS_INIT_PLAYING_GAME (RE3_AUTOSTART)");
							break;
						}
						if(!WindowIconified)
							RsEventHandler(rsFRONTENDIDLE, nil);

						if ( !FrontEndMenuManager.m_bMenuActive || FrontEndMenuManager.m_bWantToLoad )
						{
							gGameState = GS_INIT_PLAYING_GAME;
							TRACE("gGameState = GS_INIT_PLAYING_GAME;");
						}

						if ( FrontEndMenuManager.m_bWantToLoad )
						{
							InitialiseGame();
							FrontEndMenuManager.m_bGameNotLoaded = false;
							gGameState = GS_PLAYING_GAME;
							TRACE("gGameState = GS_PLAYING_GAME;");
						}
						break;
					}

					case GS_INIT_PLAYING_GAME:
					{
						InitialiseGame();

						FrontEndMenuManager.m_bGameNotLoaded = false;

						gGameState = GS_PLAYING_GAME;
						TRACE("gGameState = GS_PLAYING_GAME;");
						break;
					}

					case GS_PLAYING_GAME:
					{
						/* Optional stationary render benchmark: finish the opening
						 * cutscene, let the camera settle, then freeze simulation.
						 * Rendering and asset streaming continue at full resolution. */
						if (g_renderBenchmark) {
							static bool sawIntro = false;
							static unsigned warmup = 0;
							if (CCutsceneMgr::IsRunning()) {
								sawIntro = true;
								if (CCutsceneMgr::ms_cutsceneLoadStatus == 0)
									CCutsceneMgr::FinishCutscene();
								warmup = 0;
							} else if (sawIntro && !CGame::playingIntro &&
							           !CCutsceneMgr::IsCutsceneProcessing() &&
							           !TheCamera.m_WideScreenOn &&
							           TheCamera.GetScreenFadeStatus() == FADE_0 && ++warmup == 60) {
								CTimer::SetCodePause(true);
								printf("RE3_RENDER_BENCHMARK frozen street scene\n");
							}
						}
						float ms = (float)CTimer::GetCurrentTimeInCycles() / (float)CTimer::GetCyclesPerMillisecond();
						if ( RwInitialised )
						{
							if (!CMenuManager::m_PrefsFrameLimiter || (1000.0f / (float)RsGlobal.maxFPS) < ms)
								RsEventHandler(rsIDLE, (void *)TRUE);
						}
						break;
					}
				}
			}
			else
			{
				if ( RwCameraBeginUpdate(Scene.camera) )
				{
					RwCameraEndUpdate(Scene.camera);
					ForegroundApp = TRUE;
					RsEventHandler(rsACTIVATE, (void *)TRUE);
				}

			}
		}


		/*
		* About to shut down - block resize events again...
		*/
		RwInitialised = FALSE;

		FrontEndMenuManager.UnloadTextures();

		if ( !FrontEndMenuManager.m_bWantToRestart )
			break;

		CPad::ResetCheats();
		CPad::StopPadsShaking();

		DMAudio.ChangeMusicMode(MUSICMODE_DISABLE);

		CTimer::Stop();

		if ( FrontEndMenuManager.m_bWantToLoad )
		{
			CGame::ShutDownForRestart();
			CGame::InitialiseWhenRestarting();
			DMAudio.ChangeMusicMode(MUSICMODE_GAME);
			LoadSplash(GetLevelSplashScreen(CGame::currLevel));
			FrontEndMenuManager.m_bWantToLoad = false;
		}
		else
		{
#ifndef MASTER
			if ( gbModelViewer )
				CAnimViewer::Shutdown();
			else
#endif
			if ( gGameState == GS_PLAYING_GAME )
				CGame::ShutDown();

			CTimer::Stop();

			if ( FrontEndMenuManager.m_bFirstTime == true )
			{
				gGameState = GS_INIT_FRONTEND;
				TRACE("gGameState = GS_INIT_FRONTEND;");
			}
			else
			{
				gGameState = GS_INIT_PLAYING_GAME;
				TRACE("gGameState = GS_INIT_PLAYING_GAME;");
			}
		}

		FrontEndMenuManager.m_bFirstTime = false;
		FrontEndMenuManager.m_bWantToRestart = false;
	}


#ifndef MASTER
	if ( gbModelViewer )
		CAnimViewer::Shutdown();
	else
#endif
	if ( gGameState == GS_PLAYING_GAME )
		CGame::ShutDown();

	DMAudio.Terminate();

	_psFreeVideoModeList();


	/*
	 * Tidy up the 3D (RenderWare) components of the application...
	 */
	RsEventHandler(rsRWTERMINATE, nil);

	/*
	 * Free the platform dependent data...
	 */
	RsEventHandler(rsTERMINATE, nil);

	// Detach from the window server (what x_run() does on its way out).
	x_terminate(&g_x);

	return 0;
}

#endif
