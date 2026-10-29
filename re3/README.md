# re3 on EwokOS

[re3](https://github.com/GTAmodding/re3) - the reverse-engineered implementation of
GTA III - ported to EwokOS as a native X-window (libx) + EGL/GLES2 application.

The `src/` and `vendor/librw/` trees are based on upstream commit
`3233ffe1c4b99e8efb4c41c6794b4fce880cf503` (master branch).
The port keeps the upstream game logic, with platform guards for unsupported
rendering features. The port lives in the EwokOS-specific
pieces listed below.

re3 requires the original GTA III PC game assets to run. This port embeds no
game content beyond the freely distributable files that upstream ships in its
`gamefiles/` directory (scripts, TXDs, GXT language files).

## What was ported / changed

| File | Change |
|---|---|
| `src/skel/ewok/ewok.cpp` | **new** - EwokOS platform skeleton: libx window + event loop (`x_poll_event`/`xwin_dispatch_event`), key/mouse translation to the `Rs*` skeleton API, `kernel_tic_ms()` timer, no-joystick `CapturePad`, game state machine from the glfw skeleton |
| `src/core/CdStreamPosix.cpp` | Gets archive size from the open descriptor, completes short reads before processing models, and services window events during loading waits |
| `src/skel/crossplatform.h` | `psGlobalType.window` is `void*` (an `xwin_t*`) instead of `GLFWwindow*`; declares the `ewokGetCursorPos`/`ewokGetMouseButton` mouse shims |
| `src/core/common.h` | `<strings.h>` include (strcasecmp), M_SQRT2/M_SQRT1_2/M_LN2/M_LOG2E fallbacks for the EwokOS newlib |
| `src/core/config.h` | `__ewokos__` block: `NO_MOVIES`, no `EXTENDED_COLOURFILTER`/`SCREEN_DROPLETS` (needs wide FBOs the GLES engine lacks), no `LOAD_INI_SETTINGS` (mINI needs STL bits EwokOS lacks), no `FLUSHABLE_STREAMING` (needs `pthread_kill`) |
| `src/core/Pad.cpp` | mouse polling goes through the shims instead of `glfwGetCursorPos`/`glfwGetMouseButton` |
| `src/core/ControllerConfig.cpp`, `Frontend.cpp`, `MenuScreensCustom.cpp` | GLFW/gamepad call sites compiled out under `__ewokos__` (no gamepads on EwokOS) |
| `src/animation/AnimBlendAssocGroup.cpp` | uses newlib `<wctype.h>` instead of EwokOS `<cwctype>` (which only ships `iswspace`) |
| `src/skel/crossplatform.cpp` | `<ctype.h>`/`<alloca.h>` includes; `GetDateFormat` without `nl_langinfo` |
| `vendor/librw/src/gl/rwgl3.h`, `rwgl3impl.h`, `gl3device.cpp` | new `LIBRW_EWOKOS` device branch: creates the xwin window, an EGL/GLES2 context and swaps via `eglSwapBuffers`; GL entry points resolved for glad through a direct-symbol table (EwokOS `eglGetProcAddress` returns NULL) |

Not built (excluded by `Makefile`): `skel/glfw`, `skel/win`, `audio/sampman_oal`,
`audio/sampman_miles`, `audio/oal/*`, `audio/eax/*`. With neither `AUDIO_OAL`
nor `AUDIO_MSS` defined the game uses **sampman_null** - there is currently no
sound (EwokOS has no OpenAL).

## Building

```sh
# from the repo root; the opengl project must have staged
# libEGL/libGLESv2/headers first (projects/Makefile enforces the order)
make -C projects/re3 ARCH=aarch64 HW=q8b
```

Any ARCH/HW with a staged `system/build_$(ARCH)/$(HW)` SDK works in principle
(aarch64 + q8b is the one exercised). Output lands in
`system/build_$(ARCH)/$(HW)/rootfs/apps/re3/` with the binary and the
`gamefiles/` data already laid out next to it.

## Installing game data

The staged directory already contains everything upstream ships freely:
`data/` (particle cfg, debug + freeroam scripts), `models/` (menu/particle/font
TXDs), `TEXT/*.gxt`, `neo/`. From an original GTA III PC install, copy into the
same directory (`/apps/re3` on the target):

- `models/gta3.img` and `models/gta3.dir` - the world/vehicle/ped archives
- `anim/` - ped animation packages
- `data/` - the original `.dat`/`.cfg` files (`gta3.dat`, `default.dat`,
  `handling.cfg`, `object.dat`, `ped.dat`, `plants.dat`, `timecyc.dat`, ...)
  on top of the ones provided
- `data/main.scm` - the original mission script (the provided
  `main_freeroam.scm`/`main_d.scm` are re3-authored alternatives; rename one
  to `main.scm` if you want to run without the original script)
- `TEXT/american.gxt` etc. come from this tree already

Save games/settings live in `userfiles/` next to the binary (created on first
run).

## Running

```sh
cd /apps/re3   # cwd must contain data/ and models/
./re3
```

The window opens centered at 640x480, with its title bar inside the desktop.
Drag the title bar to move it, and use its green button to maximize/restore.
Press **Ctrl+Enter** to toggle borderless fullscreen; returning restores the
previous window position, size and maximized state. ESC opens the pause menu.

Textures load directly from the original archive. The optional first-launch
conversion to `models/txd.img` is disabled on EwokOS to avoid a lengthy cache
generation step on the SD filesystem.

## Q8B rendering and profiling

Build `projects/opengl/libs/gles` with `PGL_G3D=yes` and
`PGL_G3D_COMPILER_DIR` pointing at `machines/q8b/system/libs/adrenocompiler`,
then rebuild re3. The Q8B g2dd must accept version-7 pixel-owned geometry
programs. Kind 6 directly consumes the clipped vertex stream by default; if
the daemon declines it, the client retries kind 5 with a GPU copy stage.
`PGL_RW_DIRECT=0` explicitly selects that two-stage GPU path.
Updating the game does not require
restarting g2dd.

The raw RW raster kernel must be tagged pixel-owned by the compiler for both
kind 5 and kind 6. A legacy triangle-grid blob can upload successfully yet
leave most of the dispatch rectangle unrendered. Run the native compiler
regression from the repository root with:

```sh
make -C machines/q8b/system/libs/adreno -f shaders/Makefile.host test-rw-pixel
```

The Adreno 690 compute route rasterizes the exact RenderWare simple and
environment-reflection fragment shaders. Recognized vertex shaders use native
CPU functions, with the original GLSL interpreter handling other shaders,
followed by CPU clipping/culling. Re3 defaults to evaluating vertices on the
render thread: waking seven workers for many small native draws costs more
than the vertex work on Q8B. `PGL_VERTEX_THREADS=8` enables the persistent
workers for other workloads. A per-draw index cache evaluates shared
vertices once and expands the results in the original primitive order.
The renderer merges consecutive draws
with identical fragment state, caches GPU textures, and skips tiles outside
the mesh bounds. Colour and D24S8 depth are shared with CPU fallback draws;
unsupported shaders or state keep the CPU route.

Set these variables in the shell before launching re3:

| Variable | Purpose |
|---|---|
| `PGL_PROFILE=1` | Print FPS, actual GPU submissions, route time and vertex/clip collection time every five seconds |
| `PGL_VERTEX_THREADS=1` | Evaluate vertices on the render thread (re3 default; allowed range 1–8) |
| `PGL_RW_DIRECT=0` | Use the two-stage GPU stream path for comparison; default prefers the direct stream |
| `PGL_VERTEX_CACHE=0` | Disable reuse of shared indexed vertex results for comparison |
| `PGL_RW_BATCH=0` | Submit each RW draw immediately for a batching comparison |
| `PGL_RW_TEXTURE_LRU=1` | Compare the original texture LRU with the default size-aware retention policy; `2` alternates policies each profile window with `PGL_PROFILE=1` |
| `PGL_RW_GPU=0` | Keep RW rasterization on the CPU while retaining native fragment functions and vertex workers |
| `PGL_RW_NATIVE=0` | Disable exact RW fragment recognition for an interpreter baseline |
| `RE3_RENDER_BENCHMARK=1` | Automatically enter a street scene, suppress keyboard/mouse input, and freeze simulation after the opening fade and camera warmup; rendering and streaming continue |

GPU draw counts describe the selected raster route; its time also includes
CPU vertex work and any synchronous flush. Submission time overlaps route
time and includes IPC/driver waits. Neither number is GPU utilization.
Compare the same scene at 640x480 after streaming settles; loading screens,
menus, cutscenes, and a 4K fullscreen target have very different costs.

On Q8B, the original interpreter-only game measured about 1.0–1.2 FPS.
With the compute route, native fragment fallback and vertex workers, the
tested 640x480 street view settles at about 3.5–3.6 FPS (other street samples
were 3.7–4.9 FPS), while the opening sequence varies around 7–11 FPS.
These are scene samples, not a deterministic replay benchmark. The stable
street sample has about 1000 GPU-routed draws and 740 actual submissions per
frame, with no CPU raster draws. It is still below a fluid gameplay rate:
CPU vertex/clipping work and synchronous state/texture submissions remain
substantial. The small vertex throughput test improves about 3.4x with four
threads; that does not imply a 3.4x whole-game gain from threads alone.

On 2026-10-29 the stationary 640x480 render benchmark measured:

| Configuration | FPS | GPU submits/frame | Submit time/frame | Vertex/clip collection/frame |
|---|---:|---:|---:|---:|
| Before optimization: two-stage GPU, controls checked per draw, 8 vertex threads | 5.83 | 768 | 65.45 ms | 54.43 ms |
| Direct GPU stream, controls checked per frame, 8 vertex threads | 8.52 | 768 | 55.16 ms | 49.63 ms |
| Direct GPU stream, controls checked per frame, 1 vertex thread (new default) | 10.41 | 769 | 55.78 ms | 20.41 ms |
| Installed program, default settings confirmed without tuning variables | 10.97 | 768 | 53.08 ms | 20.38 ms |

Each row aggregates four five-second windows after the scene froze and
streaming settled. The final configuration improves measured render FPS by
79% over the baseline; the installed default configuration repeated at
10.97 FPS (88% over baseline). Resolution, raster shader, texture filtering and depth
rules are unchanged. Screenshots confirmed the complete street scene, player,
radar and HUD. Minor ambient scene differences account for one extra draw in
the final sample. This freezes simulation and measures rendering; gameplay
and other scenes have different costs. The older live-scene samples above
are historical measurements, not the baseline for this comparison.

A second pass retains 48 expensive texture images plus 16 LRU streaming images
within the original 64 GPU staging buffers. This avoids exhausting the OS's
128 shared-memory attachments per process. Both material-effect images remain
protected until their draw is submitted. The clip-space collector also avoids
unused viewport work when culling is disabled, and vertex dump controls are
checked once per frame. Post-clip staging can grow into the next persistent
size class without losing pending vertices, avoiding capacity-related CPU replay
in the opening cutscene.

In one process on CPU core 3, alternating the texture policies in five-second
windows after freezing the street scene measured:

| Texture policy | FPS | GPU submit wait/frame | Texture upload/frame |
|---|---:|---:|---:|
| Original LRU | 8.69 | 65.43 ms | 14.16 MB |
| Size-aware retention | 9.42 | 60.66 ms | 5.66 MB |

This comparison measured an 8.4% render FPS gain and 60% less texture upload
traffic, with 768 GPU submissions/frame and no GPU declines or CPU replays.
It aggregates 219 LRU frames and 190 retention frames. The earlier 10.97 FPS
default sample ran on core 4; different CPU cores and desktop activity affect
absolute FPS, so compare the alternating windows within the same process.
The final default build repeated at 10.00 FPS on core 2 over 152 frames /
15.2004 seconds, with 51.91 ms submission time and 28.17 ms collection time
per frame, 768 GPU submissions/frame, and zero declines or CPU replays.
The raster shader, filtering, resolution and depth rules are unchanged.
Run `tools/re3_gpu_profile.sh root@192.168.8.18 50 texture` to repeat the
alternating comparison. The clip collector host regression is
`opengl/libs/gles/portablegl/tests/clip_collector.c` (compile instructions in
its first line); it checks clipping, culling, viewport changes, indexed vertex
caching and pixel-identical CPU rendering when a callback declines.

Run `tools/re3_gpu_profile.sh root@192.168.8.18 30` from the repository root
to compare the two GPU stream modes with the current build. It waits for the
visible street scene to freeze, allows ten seconds for streaming, and prints
the requested sample period to local logs. The file controls `/apps/rwoff`
and `/apps/rwdetach` are checked once per presented frame; detaching flushes
pending GPU work before restoring the shared CPU buffers.

## Controls

Keyboard (EwokOS `keydef.h` codes map to the re3 `rs*` codes; printable ASCII
maps to its uppercase counterpart):

| EwokOS key | re3 key |
|---|---|
| letters/digits/punctuation | same key (uppercase) |
| `KEY_ESC` | Esc (pause menu / quit) |
| `KEY_ENTER` | Enter |
| `KEY_TAB` | Tab |
| `KEY_BACKSPACE` | Backspace |
| `KEY_UP/DOWN/LEFT/RIGHT` | arrows |
| `KEY_HOME`, `KEY_END` | Home, End |
| `KEY_LSHIFT`/`KEY_RSHIFT`, `KEY_CTRL` | Shift, Ctrl |
| `Ctrl+Enter` | toggle fullscreen / restore window |

There are no F-key or numpad codes in the EwokOS key map; the few menu
shortcuts that use them are reachable through the mouse instead.

Mouse: move to steer the camera in game / hover the menu; left button fires
and clicks; wheel scrolls. No cursor warping is needed or performed - the
skeleton tracks deltas like the glfw build.

## Known limitations

- **No audio** (sampman_null); radio/cutscene music logic runs silently.
- **No gamepad** support.
- The display settings list one initial 640x480 windowed mode. Resizing,
  maximizing and fullscreen update both the EGL target and camera dimensions.
- Settings round-trip through `gta3.set` only (re3.ini is disabled).
- Performance remains scene dependent. Q8B accelerates supported RW fragments
  using compute; this is not a complete hardware GLES graphics pipeline.
  Vertex interpretation, clipping, game logic and streaming still use the CPU,
  and frequent texture/state changes limit draw merging.
- Motion-blur trails are disabled on EwokOS because the GLES framebuffer copy
  entry points are currently stubs. Sampling the uninitialized trails texture
  previously left font/loading textures over the game. The colour overlay is
  retained.
- `EXTENDED_COLOURFILTER`/`SCREEN_DROPLETS` are disabled at the config level
  (the GLES engine's FBO support is too narrow for them).
