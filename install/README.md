# install — 将 EwokOS 安装到其他启动介质

`install` 是一个用户态安装器:它把当前启动介质上的 EwokOS 完整克隆到另一块
介质上(q8b 上即 SD 卡 → M.2 NVMe),安装后目标介质就是一块可引导的
EwokOS 盘。包含两个前端:

- **CLI** `/bin/install` — 控制台/SSH 使用;
- **GUI** `/apps/xinstall/xinstall` — 桌面图形安装器,两个前端驱动同一
  克隆引擎。

工具**自带设备驱动**:每个机型的后端直接链接该机型 BSP 库里的用户态驱动
(q8b 为 SDCC2 SD 卡驱动 + NVMe 轮询驱动),不依赖系统中正在运行的
`sdfsd`/存储守护进程,所有读写都是裸扇区 IO——与 `sdupdate`/`espwrite`
同一模型。

## 安装模型:1:1 LBA 克隆

安装即把源盘的 GPT 布局原样搬到目标盘:

1. **保护 MBR + 主 GPT**:从源盘读取(LBA0/1/2..),写到目标盘相同 LBA;
   仅补丁主 GPT 头的 `alternate_lba` 指向目标盘尾部,并重算头部 CRC32。
2. **分区数据**:每个在用分区按相同 LBA 范围逐块复制(SD → NVMe),
   默认每块写入后立即从目标盘回读比对。
3. **备份 GPT**:条目表仍写在 `last_usable_lba + 1`(与源盘相同),备份头
   写到**目标盘**最后一个 LBA(源盘与目标盘容量不同时自动迁移),
   `my_lba`/`alternate_lba`/`entry_lba` 相应补丁并重算 CRC。

克隆结束后目标盘与源盘分区内容逐字节一致;目标盘更大的尾部空间保持空闲
(不做 ext3 扩容)。

## q8b:从 SD 卡安装到 NVMe

q8b 的启动链:板载 EDK2 固件(SPI NOR)→ 启动介质 ESP 里的
`BOOTAA64.EFI` → 同卷 `KERNEL.BIN`(及可选 `ROOTFS.IMG`)→ 根文件系统
(SD 上的 ext3 分区,或 ESP 里的 ROOTFS.IMG 内存盘)。固件在
ExitBootServices 之前已完成 PCIe 控制器初始化和链路训练,因此用户态 NVMe
驱动(`machines/q8b/system/libs/bsp/src/nvme.c`)只需遍历 ECAM、映射
BAR0、跑轮询队列即可。

安装后从 NVMe 启动:固件启动菜单(F12 / Boot Maintenance Manager →
Boot From File)选择 NVMe 上的 `\EFI\BOOT\BOOTAA64.EFI`。

## GUI 使用(xinstall)

在板端桌面启动 `xinstall`(应用目录 apps/xinstall),窗口自上而下:

- **设备面板**:源/目标设备名、探测到的型号与容量(未探测时显示
  not probed);
- **分区列表**:Scan 后列出源盘 GPT 全部在用分区(类型/LBA 范围/容量/
  名称);
- **进度条**:当前阶段标签(分区名/GPT)、已完成/总 MiB、百分比、实时
  速度;
- **日志区**:滚动输出各步骤结果(绿色成功/红色失败);
- **按钮**:Scan(探测)→ Install(安装)→ Verify(比对)→ Exit。

Install 点击后弹出确认对话框(提示目标盘将被完全覆盖、是否已有 GPT),
确认后才开始写入;安装完成弹出完成对话框并提示重启。所有块 IO 在窗口
定时器回调中以小预算分步执行(`clone_step` 每 tick ≤20ms),事件循环保持
响应;运行期间按钮自动禁用,窗口拒绝关闭(防止半途退出)。

## CLI 使用

```sh
install scan                        # 枚举源/目标设备,打印两块盘的 GPT
install plan                        # 干跑:显示将要复制的内容与容量检查
install disk [--force] [--no-verify]
                                    # 完整安装:GPT + 全部分区(SD → NVMe)
install part <n> [--no-verify]      # 只克隆第 n 个分区
install boot [--no-verify]          # 只克隆 EFI 系统分区(ESP)
install verify                      # 安装后全量比对所有分区
```

目标盘已存在 GPT 时,`disk` 拒绝执行,需要 `--force` 显式确认覆盖。
目标盘容量必须装得下源盘布局(含备份 GPT 迁移后的尾部),`plan` 会预检。

典型流程:

```sh
install scan            # 确认 SD 与 NVMe 都在,记录分区布局
install disk            # 克隆,默认带逐块回读校验
install verify          # (可选)全量二次比对
reboot                  # 重要:见下
```

## 注意事项

- **安装完成后必须重启**:SDCC2 探测会重新初始化 SD 总线,正在运行的
  系统不应在安装后继续使用该卡(与 `espwrite`/`sdupdate` 相同约束)。
  GUI 版在桌面运行时同样适用——安装过程中桌面会因 SD 重新探测出现短暂
  卡顿,属预期行为。
- 安装目标盘会被**完全覆盖**;CLI 用 `--force` + 3 秒警告,GUI 用确认
  对话框把关。
- NVMe 只支持 512 字节逻辑块(驱动初始化时强制检查);工具整体按 512
  字节扇区寻址。
- 驱动无 NVMe FLUSH 命令;持久性由"写入 + 回读比对"保证。安装期间断电
  可能留下半成品布局,重跑 `install disk --force`(GUI 重跑 Install)
  即可。
- 源盘必须带 GPT(MBR-only 介质暂不支持);`scan` 会明确报错。
- 需要在板端直接运行(rootfs 或 PXE/RAM-disk 环境均可;RAM-disk 启动时
  同样可用,`bsp_sd_init_physical()` 会强制探测物理卡)。

## 构建

后端依赖机型 BSP,只随对应 HW 构建:

```sh
make -C projects install ARCH=aarch64 HW=q8b
# 产物:machines/q8b SDK rootfs 的 /bin/install(CLI)
#       与 rootfs 的 /apps/xinstall/xinstall(GUI)
```

## 新增机型

在 `src/mach/mach.c` 注册一个后端,并提供两个 blockdev
(`include/install/blockdev.h`):`source`(当前启动介质)与 `target`
(安装介质)。GPT 解析/克隆/校验全部机型无关,CLI 与 GUI 自动同时获得
新机型支持。raspi5 已有用户态 NVMe
驱动(`machines/raspi5/system/libs/bsp/src/bsp_nvme.c`),补一个 SD 后端
即可接入。

## 代码结构

```text
projects/install/
├── Makefile                 # 同时构建 CLI 与 GUI,链接 -lbsp -lsd
├── include/install/
│   ├── blockdev.h           # 裸块设备接口(六函数 vtable + detail)
│   ├── gpt.h                # GPT 读/写/克隆接口
│   ├── copy.h               # 分步 copy job 引擎 + 阻塞封装
│   └── clone.h              # 多分区 runner(写 GPT→分区→备份 GPT)
├── src/
│   ├── main.c               # CLI:scan/plan/disk/part/boot/verify
│   ├── gpt.c                # GPT 解析、CRC32、头部补丁与备份 GPT 迁移
│   ├── copy.c               # 1 MiB 分块 job(budget 预算)、进度、比对
│   ├── clone.c              # 状态机 runner,GUI/CLI 共用
│   └── mach/
│       ├── mach.c           # 机型后端注册表
│       └── q8b.c            # SD(libsd/SDCC2)+ NVMe(bsp/nvme.c)后端
└── gui/
    └── xinstall.cc          # widget++ 图形前端(信息/列表/进度/日志)
```

引擎分层:`copy_step(job, budget_ms)` 是最小执行单元(单 job、限时),
`clone_step(runner, budget_ms)` 在其上调度"GPT → 分区们 → 备份 GPT"整
个流程;CLI 用大预算阻塞循环,GUI 用窗口定时器小预算驱动,完全同一套
代码路径。
