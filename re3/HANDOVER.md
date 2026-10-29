# re3 (GTA III) EwokOS 移植 — 移交文档

> 更新日期：2026-10-28
> 状态：**可玩（CPU 渲染正确）；GPU 渲染路线已定位到数据层 bug，修复进行中**（见 §6 调试现状）

---

## 1. 项目概述

将 reverse-engineered GTA III（re3，约 20 万行 C++）移植为 EwokOS 原生应用，部署到 q8b 板（radxa-dragon-q8b，Adreno A6xx GPU）。

- 源码：`/Users/tony/work/re3`（上游），移植树在 `EwokOS/projects/re3`
- 渲染：librw gl3 后端 → EwokOS GLES2/EGL → portablegl 引擎（CPU 解释器 / Adreno GPU compute 路线）
- 音频：`sampman_null`（静音占位，未接音频后端）
- 游戏资产：原版 GTA III CD 提取（RAW MODE1 BIN → 去 2352 字节扇区头 → ISO9660 → InstallShield CAB → unshield），音频由用户提供的 `gta-3-audio_202202.zip` 解出

## 2. 文件清单（本次移植新增/修改的关键文件）

### projects/re3（游戏侧）
| 文件 | 作用 |
|---|---|
| `Makefile` | 全量构建：src/*.cpp（排除 glfw/win skel、oal/miles 音频后端）+ librw(core+gl3+d3d stubs+ps2)+lodepng+glad，链 GLES_LIBS |
| `src/skel/ewok/ewok.cpp` | **核心新文件（~900 行）**：EwokOS 平台骨架。libx 窗口生命周期、键位映射（keydef.h→rs* 码）、鼠标、游戏状态机（GS_START_UP→GS_INIT_ONCE→GS_INIT_FRONTEND→GS_FRONTEND→GS_INIT_PLAYING_GAME→GS_PLAYING_GAME）、SIGSEGV handler、`ewok_get_x()/ewok_on_window_created()` 供 librw 回调；标准输入输出保持由启动端继承 |
| `src/core/config.h` | `__ewokos__` 配置块：NO_MOVIES、USE_UNNAMED_SEM（板子不支持 named semaphore，流式加载会挂）、关 EXTENDED_PIPELINES/NEW_RENDERER 等 |
| `vendor/librw/src/gl/gl3device.cpp` | `LIBRW_EWOKOS` 分支：startEwok() 建 xwin+EGL context；`ewokGLGetString()` 返回 "OpenGL ES 2.0 EwokOS portablegl"（portablegl 自报 "0.101.0" 会让 glad 加载出全 NULL 函数表）；跳过 aniso 查询（引擎 wrapper 对扩展枚举会 GL_INVALID_ENUM） |
| `vendor/librw/src/gl/ewokglprocs.c` | 纯 C 的 GL proc 解析表（130+ 项）。必须绕开 glad.h——它的 `#define glXxx glad_glXxx` 宏会把符号表项污染成 NULL 指针 |
| `vendor/librw/src/gl/gl3shader.cpp` | `ewokPreprocess()`：portablegl 的 shader lexer 不吃 `#define/#ifdef`，这里是 mini 预处理器（define/ifdef/宏展开，注意每行末尾补 `\n` 否则 // 注释吞掉整个 shader）；LIBRW_EWOKOS 分支把多字符串 shader 源先拼接再 glShaderSource |
| `vendor/librw/src/gl/shaders/*.inc` | `VSIN(ATTRIB_*)`→attribute、`FRAGCOLOR(color)`→gl_FragColor 的函数式宏展开 |
| `tools/screencap/` | 板上抓屏工具（x_fetch_screen_graph → BMP 2560x1440） |
| `tools/gltest/` | EGL/GLES2 冒烟测试（librw 式顶点布局 pos0/normal1/color2/uv5），用于验证 GL 栈 |
| `FileMgr.cpp` | `CFileMgr::Initialise()`：`/data/gta3` 存在则以此为资源根（大资产装数据分区），否则 cwd |

### EwokOS 系统侧（渲染栈）
| 文件 | 作用 |
|---|---|
| `projects/opengl/libs/gles/src/engine/rw_gpu.inc` | **re3 专用 Adreno GPU 路线**（详见 §4.3），当前 bug 所在 |
| `projects/opengl/libs/gles/src/engine/pgl_engine.c` | portablegl 适配器：GPU 分类（gpu_link）/分发/纹理槽/coalesce；`gles_engine_gpu_link` 是 GLSL→GPU 的桥（q8b 上全部 decline） |
| `projects/opengl/libs/gles/src/engine/rw_simple.inc` | CPU 快速路径：对 librw FS 做**逐 token 精确匹配**（simple/matfx 两种契约），命中则替换解释执行 |
| `projects/opengl/libs/gles/src/egl/egl_api.c` | `egl_on_repaint`：alpha 强制 0xFF（GL 客户端写 alpha=0 会让窗口透出桌面）——保留的修复，非调试代码 |
| `projects/opengl/libs/gles/pglcompiler/include/rw_raster_cs.h` | Adreno 上的 RW 光栅 compute 内核（GLSL ES 310）+ rw_xform_cs |

## 3. 构建与部署

### 3.1 构建（q8b 目标）

```bash
cd /Users/tony/work/EwokOS
export ARCH=aarch64 HW=q8b
export PGL_G3D=yes
export PGL_G3D_COMPILER_DIR=$PWD/machines/q8b/system/libs/adrenocompiler

# 关键：翻转 PGL_G3D 后必须清对象（见下方“陈旧对象陷阱”）
rm -rf projects/opengl/libs/gles/aarch64 projects/opengl/libs/gles/portablegl/aarch64
make -C projects/opengl/libs/gles -j8     # 产出 libGLESv2.a + libEGL.a
make -C projects/re3 -j8                  # 产出 system/build_aarch64/q8b/rootfs/apps/re3/re3
```

**产物自检（按内容不按习惯）**：
- `system/build_aarch64/q8b/lib/libGLESv2.a` ≈ **8.7MB**（含 mesa→Adreno 编译器合并），`nm` 应有 ~276 个 `T pglc_/ir3_` 定义
- `re3` 二进制 ≈ **8.0MB**；`strings re3 | grep -c PGLGPU` 非零（当前含调试打印）
- 若 libGLESv2.a ≈ 540KB / re3 ≈ 3.5MB ⇒ **PGL_G3D 没生效，GPU 代码完全没编进去**（链接仍会成功，因为 CPU 形状的引擎不引用编译器符号——务必按大小/符号校验）

**⚠️ 陈旧对象陷阱（本次浪费数小时的根因）**：gles/portablegl 的对象目录只按 `$(ARCH)/$(HW)` 键控，**翻转 PGL_G3D 不会触发重编**，陈旧的 CPU 形状 `.o` 会被静默复用（症状：链接成功但 `pglGpuBegin`/`pglc_*` 未定义或缺失）。改标志必清 `gles/aarch64` 与 `gles/portablegl/aarch64`。

**机器级构建**（`machines/q8b/system`）会通过其 make.inc 的 `export PGL_G3D=yes` 自动传播；从 projects 树单独构建时必须手工 export 上面两个变量（`projects/make.inc` 不包含机器 make.inc）。

### 3.2 部署（板子 root@192.168.8.50，密码 root）

```bash
# sftp（expect 自动输密码；无 sshpass）
#   1) rm /apps/re3/re3          ← sftp 覆盖不截断，必须先 rm，否则残留尾部字节
#   2) put re3 /apps/re3/re3.tmp
# shell（板子）：
cp /bin/ls /apps/re3/re3           # 占位，获得执行位（sftp put 不给执行位）
cp /apps/re3/re3.tmp /apps/re3/re3 # cp 覆盖保留目标执行位
rm /apps/re3/re3.tmp
```

- 资产位置：`/data/gta3/{TEXT,data,models,neo,userfiles}`（SD 数据分区）；程序在 `/apps/re3/`
- **启动**：必须用**前台 ssh 常驻**（`ssh root@... /apps/re3/re3`，本机侧 keep-alive）。`&` 后台 + 断开 ssh 会被杀
- 日志：stdout/stderr 不再重定向，由终端、串口或 GUI 启动端自行接收
- 抓屏：`/apps/screencap /tmp/x.bmp` → sftp get → 本机 `sips -z` 缩放查看

### 3.3 板子 shell 的坑
- 不支持 `VAR=val cmd` 环境变量前缀、无 `tail`/`mv`/`&&`；`ls -l 单个文件` 无输出（用 `ls 目录`）
- 用**文件开关**代替环境变量（见 §6.3）

## 4. 渲染架构（q8b 实际路径）

### 4.1 分层
```
re3/librw (GLSL shader 源)
  → EwokOS GLES2 (gles2_api + glsl 解释器)   ← CPU：解释执行 VS/FS
  → pgl_engine (pgl_engine.c 适配器)
      ├─ CPU 路线：glsl 解释器 + rw_simple FS 快速路径   ← 目前正确渲染的路线
      └─ GPU 路线（Adreno）：
          ├─ GLSL 程序编译 pglc_compile_glsl：**未移植**（decline，见 §6.1）
          ├─ 固定函数内核路线（pgl_g3d_batch_tri）：需要 gpu_bringup，
          │   而 bringup 依赖“存在编译成功的 eligible 程序”→ q8b+re3 上**从未启用**
          └─ RW compute 路线（rw_gpu.inc）→ g2dd (ver==690) → rw_raster_cs  ← 唯一启用的 GPU 路线，**当前损坏**
```

### 4.2 RW compute 路线工作方式（rw_gpu.inc）
1. librw 的 FS 与已知 RW 契约（`rw_shader_contract.h`）逐 token 匹配 → `rw_simple=1/2`
2. 画三角形时：portablegl CPU 变换/裁剪管线照常跑，三角形回调 `rw_collect` 截获**裁剪后**的三角形，每顶点打包 20 floats：`[x, y, z, 1/w | uv(2) | color(4) | fog | (matfx: uv1+envColor)...]`（x/y/z 为 screen_space 除 w 后的像素坐标，y-up）
3. 批量（≤20478 三角，状态 key 含纹理/混合/深度/剪裁/雾）经 `g2d_pgl_draw` 提交 g2dd
4. `rw_raster_cs` compute 内核逐像素owner、按提交顺序混合（保透明次序），D24S8 深度（CPU MAX_Z=0xFFFFFF<<8 一致）、0xAARRGGBB 一致

### 4.3 渲染路线现状
| 内容 | 路线 | 状态 |
|---|---|---|
| splash/菜单 2D quad（w=1） | RW GPU 路线 | 基本正常 |
| 世界几何（透视 w≠1） | RW GPU 路线 | **损坏**（见 §6.2） |
| HUD/sprite2d | CPU（固定函数路线未 bringup） | 正常 |
| 全部 GLSL 程序 | CPU glsl 解释器 | 正确 |

## 5. 资产与运行时
- 资产根 `/data/gta3`：`models/gta3.img(.dir)`、`TEXT/*.gxt`、`data/*.dat`、`audio/`、`neo/`（PC 反射贴图）
- 流式加载：`CdStream` 单线程；sem 用 `USE_UNNAMED_SEM`
- 已知无害噪音：`casepath couldn't find X360BTNS.TXD/gta3.set/skins`、`CObjectData: Cannot find object adboard1` 等

## 6. 调试现状（移交重点）

### 6.1 已确认事实
1. **Adreno 后端无图形管线**：`pglc_compile_glsl` 一律返回
   `the Adreno backend has no graphics pipeline port; use the compute route (pglc_compile_compute*)`
   —— re3 的 14 个 librw 程序全部静默回退 CPU。要真 GPU 加速全部程序，需给
   `machines/q8b/system/libs/adrenocompiler` 移植图形管线（当前只有 compute）。
2. **唯一启用的 GPU 路线（RW compute）会损坏渲染**，但提交全部 G2D_OK、无 MMU fault。
3. **损坏在喂给 GPU 的顶点数据，不在内核数学**（板上实测统计）：
   - 第 1 批（正交 quad，不裁剪）：`x[0..640] y[0..480]` 正常
   - 第 101+ 批（菜单/世界，过裁剪器）：`x[-0.5..0]、y 到 1.4e14、z 到 2.3e18、iw 为负`
     —— `out[0]=screen_space.x*iw ≤ 0` 说明回调收到的顶点字段非法（合法屏内顶点 x≥0）
   - 头号嫌疑：`draw_triangle_clip` 产生的插值顶点（tmp1/tmp2）路径上的某字段
     （`screen_space`/`vs_out`/`clip_space`）；`update_clip_pt` 本身已核对无误
4. 内核数学契约已逐条核对自洽：坐标（screen_space 为齐次、CPU 侧 v4_to_v3h 才除 w）、
   y-up 约定与内核 `target.y-xy.y` 翻转、双 winding 重心、透视修正 `q=bc*iw/w`、
   D24S8 深度格式、RGBA 打包、blend/fog/alpha 语义均与 CPU 一致。
5. **挂死风险**：`kill -9` 正在跑 GPU 的 re3（即使先 rwdetach）会把 g2dd/驱动搞死
   ——板子 ping 通但 sshd 假死，只能硬复位（已发生 2 次）。改用文件开关（§6.3）。

### 6.2 当前已布置的抓取手段
- `rw_collect` 检测到 `out[0]<0`（垃圾特征）且存在 `/apps/rwdump` 时，把 3 个顶点的
  原始 `screen_space/clip_space/vs_out[16]` + `rw_uv/rw_color/rw_fog` 写 `/tmp/rwvert.bin`
  （本机解析脚本：`/tmp/rwvert_parse.py`，注意 /tmp 是 ramfs，别丢）
- 每 100 次 flush 打印三角形范围统计（`PGL RW GPU flush #N tris=… x[…] y[…] z[…] iw[…]`）
- 首次 flush 可 dump 全部三角形到 `/tmp/rwtris.bin`
- pgl_engine.c：`PGLGPU: link/bringup/compile declined/draw` 打印（draw 追踪默认开，
  `PGLGPU_NOTRACE` 可关——但板子设不了环境变量，直接改代码默认值）

### 6.3 板上文件开关（cp /bin/ls 创建、rm 删除）
| 文件 | 效果 |
|---|---|
| `/apps/rwoff` | 阻止 RW GPU 路线采纳（全部 CPU）——A/B 验证用 |
| `/apps/rwdetach` | 运行中优雅脱离 GPU 路线（flush+恢复 CPU back buffer），**替代 kill** |
| `/apps/rwdump` | 允许写 /tmp/rwvert.bin、/tmp/rwtris.bin 转储 |

### 6.4 建议的下一步
1. **（进行中）** 复位后部署 21:04 构建，抓 `/tmp/rwvert.bin`，按 §6.2 字段定位坏数据来源：
   - 若 `screen_space` 错而 `clip_space` 对 → 在 `rw_collect` 里直接从 clip_space 重算
     屏幕坐标（`x=W/2*clip.x+W/2*clip.w` 等），绕过坏字段——一条稳妥的修复路线
   - 若 `vs_out` 错 → 查裁剪插值/`c->vs_output.size` 与 interp_entry varying 布局
2. 渲染修通后：清理全部调试打印（§6.2），恢复 draw 追踪默认关
3. 中期：给 Adreno 后端移植图形管线（`pglc_compile_glsl`），让 librw 的真实 shader 上 GPU
4. `sampman_null` → 接一个音频后端
5. 用机器级 `make` 重打 `root_aarch64.img`，把修好的 re3 烧进 SD 镜像（当前 SD 是手工增量部署）
6. 清理 re3 源码里残留的 initOpenGL 调试打印

## 7. 快速验证清单（每次大改后）
1. `gltest`（`/apps/gltest/gltest`）：应打印 `prog linked ok=1` + 每条 GLSL 的
   `PGLGPU: compile declined …no graphics pipeline port…`（q8b 上的预期行为）
2. 启动 re3 → 在启动端输出中出现 `PGL RW GPU route adopted w=640 h=480` → 抓屏看渲染
3. A/B：`cp /bin/ls /apps/rwoff` 后重启 re3 → 全 CPU 渲染应完全正确（基线）
4. 抓屏对比：`/apps/screencap /tmp/g.png` → sftp get → 本机查看

## 8. 经验教训速查
- 翻编译开关必清对象目录（§3.1 陈旧对象陷阱）
- 校验产物用 `nm`/`strings`/大小，别信“链接成功”
- 板子挂死 = ping 通 + sshd 超时；**永远别 kill -9 GPU 活跃的进程**，用 `/apps/rwdetach`
- /tmp（含 GPU 转储）是 ramfs——要带走的数据先 sftp get 再动板子
- expect 脚本对短输出常吞回显，重要结果落文件再 sftp 拉回
- sftp 覆盖不截断 → 先 rm 再 put；执行位用 cp 占位法
