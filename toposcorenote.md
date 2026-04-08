这个笔记不要冗长，这个笔记不是聊天记录，但所有信息都要有
任何有关软件的架构的知识的获得和软件架构的修改的知识都需要同步在这里
中心思想是要把所有的架构得知和信息和位置都同步在这里，积少成多得到整个软件的整体架构和各个信息的位置

因为我们可能之后要重构整个软件


所以新发现的架构和各种信息的同步在这里很重要

这个note是重构软件的指南和地图，所以请积累信息。这很重要
各种架构和现在所改的架构和各种潜在问题或思路或分析或暗坑或斟酌 或各种信息都要同步在这里

你发现的这个软件的不重要的结构也要记录进来，因为以后这些屎山垃圾需要被删除，所以需要被记录，发现垃圾或者发现不重要的东西也需要记录。

claude你思考不要过多，token很宝贵的，节约token，不要思考和研究过于多。

节约token别思考太多，直觉和猜测是优先的，允许基于直觉和猜测来行动，百分之80的置信度就足以开始汇报和行动和尝试，多尝试即使n次失败也几乎都会比浪费很多token得到一个你能说服自己 证明你自己不是有意做错 的安全感  要好。 要猜测和判断，当然也要推理和求证

此note主要记录实为，现在的状态样子，历史可删，重要的结构和架构和地址和点要记录，但主要记录实为就是现在的状态

比较重要的改进倾向和改进计划可保留。

每次更改的比较重要的值得记录的历史记录更改记录，保存在
工作目录/note/
里新建md，名字是 当前时间 例如 如果当前时间是2000年1月1日12点00分就记录 200001011200.md

======================分界线，以上是设计师给你的指导思想=====================================

---

# topos0.074 — vid_panel.c UI 层（下屏面板）

> 文件：`topos.core0.04/source/vid_panel.c`（~1320行）
> 详细历史：`note/202604081040.md`

## citro2d z 值行为（重要陷阱）

**z 值不可靠用于层叠，全部用 0.5f，靠绘制顺序控制覆盖。**

- citro2d 源码写 `GPU_GEQUAL`，理论"z 越大越靠前"
- 实测 z=0.0f 的矩形反而盖住一切（黑屏）
- `Draw_texture_internal` / `Draw_c` 等系统函数硬编码 z=0.0f
- **结论**：不用 z 做层叠，所有 draw call 统一 0.5f，后画覆盖先画

## 错误弹窗覆盖（已解决）

- 触发：文件无法打开时 `Vid_panel_files_show_play_error()`
- 弹窗函数：`p_draw_files_play_error_overlay()`
- **关键**：在 `Vid_panel_draw_bottom_screen()` FILES 分支**最末尾**调用（所有其他绘制之后），靠绘制顺序覆盖文件列表和 chrome
- 输入：touch OK 按钮或按 A 键 dismiss

## Setting 行文字接近 chrome 消失（已解决）

- 原因：旧代码 `if (text_y >= VP_SET_CONTENT_Y)` 整行跳过
- 修复：删除条件，chrome 矩形后画盖住溢出内容

## scissor test（撤回，勿重试）

- `C3D_SetScissor` 坐标系复杂（GPU y 轴从底部向上）
- 实测导致界面空白，已全部撤回
- 不用 scissor，用 z+绘制顺序

## 绘制顺序

```
draw_files_panel / draw_setting_panel:
  背景 → 行内容 → scrollbar → chrome footer → chrome top

Vid_panel_draw_bottom_screen FILES 分支:
  Vid_panel_draw_bottom() → expl/err_draw → overlay → p_draw_files_play_error_overlay
```




# Video Player for 3DS — 项目笔记

> **规则：每次改动后更新本文件。记录发现、架构、路径、判断、修改原因。简洁但全面，不是聊天记录。**

---

## 文件位置

- **父目录（仅含两个子工程）**：`D:\3dsvideoplayermock\Video_player_for_3DS1.62.8claudesimple1.14test4\`
  - **vp3ds（本笔记对应的主播放器源码树，简称 vp3ds）**：`...\videoplayerfor3ds1.62.8.1.14test4\` — Makefile、`source/`、`include/`、`PROJECT_NOTES.md`（本文件）等。
  - **mockrev（新 UI + 输入壳）**：`...\3dsvideoplayermockrev0.32\` — 见下「mockrev 与 vp3ds 合并计划」。
- **当前开发版（主力）**：vp3ds 根目录（上条）。
- 稳定基准：`D:\3dsvideoplayermock\Video_player_for_3DS1.62.5\`（MVD ABGR32 + CPU Morton，已验证）
- SBS 3D参考版：`D:\3dsvideoplayermock\Video_player_for_3DS1.62.8test1\`
- GitHub sync目录：`D:\3dsvideoplayermock\github\`（fork clone，仅同步/push用）
- Fork地址：https://github.com/mockmodular/Video_player_for_3DS_mock
- Note历史：`工作目录/note/` （历史改动记录存这里）

---

## mockrev 与 vp3ds 合并计划（进行中）

**简称**：**vp3ds** = `videoplayerfor3ds1.62.8.1.14test4` 本仓库；**mockrev** = 同级目录 `3dsvideoplayermockrev0.32`。

**目标形态**

- **mockrev**：只保留 **UI + 输入**（`ui.c`/`ui.h`、`input.c`/`input.h` 及与之直接相关的绘制/布局）。**移除** FFmpeg/软解、`video.c`、与播放内核绑定的 `fs`/`app` 里文件浏览与解码驱动等（凡「解码器模块之类」、非 UI/非输入的，均不再放在 mockrev 最终形态里）。长期由 mockrev **产生全部输入事件**，对接 vp3ds 侧已有思路：`vid_cmd` / `Vid_hid_*` 命令队列（具体接线后续实作）。
- **vp3ds**：把 **除输入以外** 的内核（`video_player` 链路、`decoder`、`draw`、`vid_*`、线程与设置等）**迁入 mockrev 工程树**，与 mockrev 的 UI/输入拼成单一应用。输入逻辑以 mockrev 为准，vp3ds 不再保留一套并行 HID（迁移完成后）。

**阶段顺序（当前共识）**

1. **先修 mockrev 的 UI**：在删减解码等大模块之前或同时，把下屏布局、触控区、状态显示、与后续 vp3ds 对接预留（例如命令/占位接口）调到可验收水平。**本阶段不要求**已迁入 vp3ds 内核。
2. **瘦身 mockrev**：删库与源码中与 UI/输入无关的解码、FFmpeg 链接、`video`/`fs`/`app` 中与「独立 demo 播放」绑死的部分；Makefile 改为只编 UI+输入 + 桩/空实现，直至能稳定编译。
3. **迁入 vp3ds 内核**：按模块把 vp3ds 源文件并入 mockrev（或统一 `Makefile` 与 include 路径），主循环改为 mockrev 采集输入 → 填命令 → vp3ds `Vid_main`/消费侧；删除重复输入路径。

**实态备注**：mockrev 当前 `Makefile` 仍链接 `lavformat`/`lavcodec`/… 且含 `video.c`、`app.c` 等，**尚未**完成瘦身；本段为**计划与目标**，落地后改「实态」并删过时句。

---

## 与上游（原版）差异 — 本 fork 摘要

### 原版功能对比（要点）

| 维度 | 原版软件 | 本 fork |
|---|---|---|
| **3D / 片源形态** | 偏 **AVI 容器**、**双路视频轨**、**MJPEG**、主要靠 **CPU 解码** 的怪异组合；与常见单路 H.264 SBS 片源不对路 | **H.264 MVD 硬解** 单路 SBS；**全自动**按规则识别/切分左右眼画面，**对齐屏幕并居中**，左右眼 **parallax barrier 真 3D**（`Vid_fit_to_screen` 右眼跟随左眼、CPU 行分割等，见「SBS 3D 当前状态」） |
| **上屏色深** | **RGB565** 为主的上屏路径 | 全链路 **RGB888 级**（**888 真彩色**）：纹理 `GPU_RGBA8`，解码侧 ABGR32 / RGBA8888；**硬解、软解** 均走该管线（见「三条解码路径」「渲染管线」） |
| **Y2R（软解 + 硬件色彩转换）** | **未实现** 可用的软解 Y2R 模式 | **新增**：**YUV420P → Y2R → 与 `GPU_RGBA8` 匹配**；sem **Advanced** 可切 **CPU / Y2R**（见「已知陷阱」Y2R 条、「sem菜单 Advanced」） |
| **上屏与 UI** | 上屏易与 OSD/控件共存，视频易随 UI 出现而缩放/让位 | **增加「上屏永久最大化」取向**：配合 **UI 模式** 时，**上屏不再出现任何 UI**，控件与信息**全部在下屏**；**上屏视频不再因 UI 出现而被缩放**（整体清爽化另见「UI 精简」） |

- **SBS 3D 补充**：AUTO / 3D·2D 菜单、AUTO 判据（`~3750`，如 `h==240 && w>=640`）— 与「任意分辨率无上限」不是同一概念；具体边界见「SBS 3D 当前状态」。
- **内存碎片 / 越播越卡**：**无复杂算法** — **拖动进度条 seek 时重开解码器**（`video_player.c:~5466`），缓解 heap 碎片导致的卡顿加剧（关键代码位置表同条）。
- **未穷举**：其余改动随开发继续补进本文件或 `note/日期.md`。

---

## 版本号规则

- **版本字符串**：`include/video_player.h` — `#define DEF_VID_VER "v1.62.9"`
- **APP_VER（Makefile）**：公式 `major×1024 + minor×16 + micro`，当前 1.62.8 → 2024
- **APP_VER 永远是 1122，不能变**（Makefile写死）
- minor 最大 63，micro 最大 15

---

## 构建

**直接双击 `build.bat`**（项目根目录）。必须用 **devkitARM r65 (GCC 14.2.0)**，r66/GCC 15.1 → data abort崩溃。

```bash
# 命令行等价：
"C:/devkitPro/msys2/usr/bin/bash.exe" -lc "cd '/d/3dsvideoplayermock/Video_player_for_3DS1.62.8claudesimple1.13test/' && make 2>&1 | grep 'error:'"
```

**陷阱：**
- 必须用 login shell（`-lc`），否则 TEMP/TMP 空，GCC 报权限错误
- `make cia_all` 不触发重编译，源码有error时仍"成功"打出旧版本，必须先 `make` 确认无error
- 换目录后必须 `make clean`，否则旧 `.d` 文件路径错误导致构建失败
- 备用devkit包：`D:\3dsvideoplayermock\devkitARM-r65-1-windows_x86_64.pkg.tar.zst`
- **high_ram版本已去除（2026-03-29）**：`Makefile` 中 `cia_all` 不再依赖 `cia_high_ram`，构建只输出普通 `.cia`

---

## 当前架构

```
main.c → Vid_main()（vid_draw.c）/ Vid_init、Vid_exit（vid_lifecycle.c）
           ├── menu.c           文件浏览/UI
           ├── vid_draw.c       渲染主循环：Vid_main + Vid_draw_init_exit_message（2026-04-02）
           ├── vid_lifecycle.c  生命周期+全局状态：vid_player/vid_msg全局变量、Vid_init/Vid_exit/Vid_resume/Vid_suspend/Vid_load_msg、Vid_init_thread/Vid_exit_thread、所有Vid_init_*数据函数（2026-04-02，video_player.c彻底退场）
           ├── vid_screen.c     全屏/亮度/fit/缩放（2026-03-29）
           ├── vid_hid.c        HID enqueue（Vid_hid_enqueue/enqueue_seek）
           ├── vid_cmd.c        命令队列（环形缓冲）
           ├── vid_seek_engine.c seek机制：on_bar_drag/commit/step_fwd/step_back/get_view（2026-04-02）
           ├── vid_input.c      输入处理器：HID_*宏/vid_fill_*/Vid_process_hid_cmd_queue/Vid_hid（2026-04-02）
           ├── vid_worker.c     解码统计/帧时序回调/Vid_decode_video_thread/Vid_convert_thread（2026-04-02）
           ├── vid_decode.c     解码流水线编排：Vid_decode_thread/Vid_read_packet_thread/Vid_expl_callback（2026-04-02）
           ├── draw.c           GPU纹理/渲染 (citro2d/3d)
           ├── decoder.c        FFmpeg + MVD硬解
           └── converter.c      颜色/音频转换 (swscale/Y2R/ASM)
```

include链：`video_player.c → draw.h → draw_types.h → citro3d.h → c3d/types.h → <3ds.h>`

---

## 解耦重构计划（进行中）

**目标架构：事件驱动，输入层与内核解耦**

```
用户按键
  ↓
vid_hid.c → 命令队列（中转站）
                ↓
         video_player.c（Vid_main 消费命令）
          ├── vid_sync.c    音画同步
          ├── vid_texture.c 纹理管理
          ├── vid_screen.c  屏幕控制
          └── vid_threads.c 后台解码线程
                  ↑
           vid_settings.c  配置数据
```

**各模块职责：**

| 模块 | 职责 |
|---|---|
| `vid_state.h` ✅ | 共享类型：所有枚举/结构体/常量，无逻辑 |
| `vid_texture.c` ✅ | GPU纹理：图像切块、上传、绘制。含 `Vid_get_min_texture_size` / `Vid_large_texture_*` 7个函数。`include/vid_texture.h` + `source/vid_texture.c`，从 video_player.c 移出 |
| `vid_settings.c` ✅ | 设置：初始化/load/save，不知道播放器状态。`include/vid_settings.h` + `source/vid_settings.c`，从 video_player.c 移出 |
| `vid_sync.c` ✅ | A/V同步：`Vid_init_desync_data` / `Vid_update_video_delay` / `Vid_update_decoding_delay`。`include/vid_sync.h` + `source/vid_sync.c`，从 video_player.c 移出 |
| `vid_screen.c` ✅ | 屏幕：`Vid_fit_to_screen` / `Vid_change_video_size` / 全屏进出场 / 亮度增减 / `Vid_control_full_screen`（底屏倒计时关 LCD 等）。`include/vid_screen.h` + `source/vid_screen.c`，从 video_player.c 移出（2026-03-29） |
| `vid_hid.c` ✅ | HID enqueue层：`Vid_hid_enqueue`/`Vid_hid_enqueue_seek`，只产命令，不碰vid_player |
| `vid_seek_engine.c` ✅ | Seek 引擎：`VidSeekEngineView` + `on_bar_drag/commit/step_fwd/step_back` + `get_view()`；集中管理 seek_pos/seek_pos_cache/seek_progress；`vid_seekbar.h` 持有几何常量（X/Y/W/H）+ 命中测试（2026-04-02） |
| `vid_input.c` ✅ | 输入处理器：HID_*宏 + vid_fill_* + `Vid_process_hid_cmd_queue`（命令执行器）+ `Vid_hid`（顶层入口）（2026-04-02） |
| `vid_worker.c` ✅ | 解码统计/帧时序回调/Vid_decode_video_thread/Vid_convert_thread（2026-04-02） |
| `vid_decode.c` ✅ | 解码流水线编排：Vid_decode_thread/Vid_read_packet_thread/Vid_expl_callback/Vid_expl_cancel_callback（2026-04-02） |
| `vid_draw.c` ✅ | 渲染主循环：`Vid_main`（~1114行，含3D/SBS渲染、底屏UI、调试图）+ `Vid_draw_init_exit_message`（init/exit状态屏）。`include/vid_draw.h`。从 video_player.c 移出（2026-04-02）。注意：需 `system/sem.h`（Sem_config/Sem_state）和 `<malloc.h>`（mallinfo） |
| `vid_lifecycle.c` ✅ | 生命周期：`vid_player`/`vid_msg`全局变量定义、`Vid_init`/`Vid_exit`/`Vid_resume`/`Vid_suspend`/`Vid_load_msg`/`Vid_query_*`/`Vid_get/set_use_hw_color_conversion`、`Vid_init_thread`/`Vid_exit_thread`、所有`Vid_init_*`数据函数（2026-04-02，video_player.c退场） |
| ~~`video_player.c`~~ | ~~核心壳~~ — **已删除（2026-04-02）**，内容全部迁入 `vid_lifecycle.c`；公开API声明仍在 `video_player.h` |

**拆解顺序：**
0. `vid_state.h` ✅ 完成（2026-03-28）
1. `vid_texture.c` ✅ 完成（2026-03-29）
2. `vid_settings.c` ✅ 完成（2026-03-29）
3. `vid_sync.c` ✅ 完成（2026-03-29）
4. `vid_screen.c` ✅ 完成（2026-03-29）
5a. 定义命令接口（UI→core中转站）✅ `include/vid_cmd.h` + `source/vid_cmd.c`（环形队列 `Vid_cmd_push`/`pop`，`VidCmdId` 覆盖亮度/全屏/解码/菜单/seek 等）
5b. `vid_hid.c`（改为只产生命令）✅ 部分：`Vid_hid_compute_move_deltas` 已替代 `video_player.c` 内摇杆缩放平移的重复逻辑；`Vid_hid_enqueue` / `Vid_hid_enqueue_seek` 已实现入队（供后续接入），`include/vid_hid_macros.h` + `VidHidLayout` 与 `vid_player` 几何解耦
5c. core层消费命令 ✅ 完成（2026-03-30）：`Vid_process_hid_cmd_queue` 实装于 `video_player.c`；`Vid_hid()` CFM 执行段（原约466行宏直驱）替换为「enqueue→process」流程；旧 HID_*_CFM 宏已清理（见下）
5c+. 死代码宏清理 ✅ 完成（2026-03-30）：`video_player.c` 开头约61行 `HID_*_CFM` / `HID_*_UPDATE_RANGE` 宏（5c后无调用方）全部删除；保留：`HID_BRIGHTNESS_UP/DOWN_PRE_CFM`（auto_full_screen_count guard 仍在用）、`HID_SCROLL_BAR_CFM`（Scroll 段仍在用）、全部 `_SEL` / `_DESEL` 宏（SEL/DESEL 段仍在用）。编译 0 error 0 warning。
6. `vid_worker.c` ✅ 完成（2026-04-02）：统计辅助函数 + 帧时序回调 + `Vid_decode_video_thread` + `Vid_convert_thread` 已移出。
6b. `vid_input.c` ✅ 完成（2026-04-02）：HID_* 宏 + `vid_fill_layout/router_state/locks` + `Vid_process_hid_cmd_queue` + `Vid_hid` 移出；video_player.c 从 ~4052 行降至 ~3263 行（−789行）。
6c. `vid_decode.c` ✅ 完成（2026-04-02）：`Vid_decode_thread` + `Vid_read_packet_thread` + `Vid_expl_callback` + `Vid_expl_cancel_callback` 移出；video_player.c 从 ~3263 行降至 ~1802 行（−1461行）。新增 `include/vid_decode.h` + `include/vid_lifecycle.h`；4个 `Vid_init_*` 函数去掉 `static`（跨文件可见）；`vid_input.c` 的手工 `extern` 改为 `#include "vid_decode.h"`。
7. `vid_draw.c` ✅ 完成（2026-04-02）：`Vid_main` + `Vid_draw_init_exit_message` 移出；video_player.c 从 ~1802 行降至 ~625 行（−1177行）。`Vid_draw_init_exit_message` 从 static 改为全局（`Vid_init`/`Vid_exit` 跨文件调用），声明于 `vid_draw.h`。需 `system/sem.h` + `<malloc.h>`。
8. `vid_lifecycle.c` ✅ 完成（2026-04-02）：`Vid_init_thread`/`Vid_exit_thread` + 全部 `Vid_init_*` 数据函数 + `vid_player`/`vid_msg` 全局变量定义移出；`video_player.c` **彻底删除**，原 5135 行已全部拆进 8 个模块。公开 API 声明保留在 `video_player.h`，调用方零改动。
9. **seek 机制与进度条 UI 解耦** ✅ 完成（2026-04-02）：新增 `vid_seekbar.h`（几何常量 + 命中测试）、`vid_seek_engine.h/c`（Seek引擎 API）；`VidHidLayout` 移除 `seek_bar`，`VidHidRouterState` 移除 seek 相关字段；命中测试从 `Draw_texture` 副作用几何改为静态常量；4个 seek 命令处理集中进引擎；`vid_draw.c` 改用 `VidSeekEngine_get_view()` + `VID_SEEKBAR_*` 绘制，不再直接读 seek 字段。**效果：换进度条 UI 只改 vid_draw.c 绘制块，seek 逻辑零改动。**

### Step 5a–5c：动机、原理、结构（本实例说明）

**为什么要拆（动机）**

- 原先 `Vid_hid()` 里 **几百行宏 + 直接改 `vid_player`**：输入判定、UI 几何、播放器状态、解码线程请求全缠在一起，**难测、难换 UI、难做自动化**，重构整个播放器时风险大。
- 目标：**输入层只负责「用户干了什么」**，**核心只负责「收到命令后怎么改状态/发解码请求」**，中间用 **稳定的数据结构（命令）** 传话，和常见 GUI 的 *event → command → handler* 同构。

**5a / 5b / 5c 各解决什么（原理）**

| 步骤 | 名字 | 解决什么 | 类比 |
|---|---|---|---|
| **5a** | 命令接口 + 队列 | 约定「传什么话」：`VidCmdId` + 载荷（`iarg`/`darg0`…），以及 **先入先出队列**，避免散落的全局标志位。 | 邮件类型 + 收件箱 |
| **5b** | `vid_hid.c` 只产命令 | **硬件 HID + 屏幕布局快照** → 只做判定，产出 **与 `vid_player` 无关的语义**（或只读 `VidHidLayout`/`VidHidRouterState` 快照），**写入队列**，不在这里 `vid_player.xxx = …`（最终形态）。 | 前台接待只填工单，不直接改仓库账 |
| **5c** | core 消费命令 | `video_player.c`（或专用 dispatch）**从队列取出命令**，访问 `vid_player`、调 `Util_queue_add`、调 `Vid_*`，**唯一写状态的地方集中**。 | 仓库按工单出库、记账 |

**数据流结构（目标形态）**

```
hidScan → Hid_info
           ↓
   填 VidHidLayout（按钮矩形等几何快照）
   填 VidHidRouterState（菜单模式、是否全屏等只读路由信息）
           ↓
   vid_hid.c: Vid_hid_enqueue / Vid_hid_enqueue_seek
           ↓
   vid_cmd 环形队列（Vid_cmd_push）
           ↓
   video_player.c: Vid_process_hid_cmd_queue（switch 执行）
           ↓
   vid_player + decode_thread_command_queue 等
```

**本实例当前实态（2026-03-30）**

- **5c 已闭环**：`Vid_hid()` 的 CFM 执行段（原约466行 HID 宏直驱）已替换为：`vid_fill_layout` / `vid_fill_router_state` / `vid_fill_locks` 填快照 → `Vid_cmd_queue_reset` → `Vid_hid_enqueue` → `Vid_process_hid_cmd_queue`（move content 保留）→ 重填快照 → `Vid_cmd_queue_reset` → `Vid_hid_enqueue_seek` → `Vid_process_hid_cmd_queue`。
- **保留不变**：SEL（按钮高亮）、DESEL（解选）、Scroll（滚动惯性）三个段仍在 `Vid_hid()` 里；aptShouldJumpToHome() 顶部处理段不变。
- **旧 HID_*_CFM 宏**：已清理（2026-03-30）。61 行死代码宏删除，编译 0 warning。保留：`HID_BRIGHTNESS_UP/DOWN_PRE_CFM`、`HID_SCROLL_BAR_CFM`、所有 `_SEL`/`_DESEL`。
- **相关文件**：

| 文件 | 作用 |
|---|---|
| `include/vid_cmd.h`、`source/vid_cmd.c` | `VidCmdId`、`Vid_cmd_push`/`pop`/`reset` |
| `include/vid_hid.h`、`source/vid_hid.c` | `VidHidLayout`、`VidHidRouterState`、`Vid_hid_enqueue`、`Vid_hid_enqueue_seek`、`Vid_hid_compute_move_deltas` |
| `include/vid_hid_macros.h` | `VID_HID_*`，几何用 `layout` 参数 |
| `source/video_player.c` | `vid_fill_layout`、`vid_fill_router_state`、`vid_fill_locks`、`Vid_process_hid_cmd_queue`（均为 static） |

- **当前数据流（已落地）**：
```
Vid_hid() 填 VidHidLayout/RouterState/UiLocks 快照
  → Vid_hid_enqueue → vid_cmd 队列 → Vid_process_hid_cmd_queue（switch，写 vid_player）
  → move content（Vid_hid_compute_move_deltas）
  → 重填快照 → Vid_hid_enqueue_seek → 队列 → Vid_process_hid_cmd_queue
```

**vid_state.h 改动说明：**
- 所有 enum/struct/typedef 从 video_player.c 移入 include/vid_state.h
- `vid_player` 和 `vid_msg` 去掉 `static`，改为全局变量 + extern 声明
- video_player.c 中 HID 宏仍保留（直驱路径），与 Step 5 目标形态并行；完全迁移后改为 `VID_HID_*` + layout 或仅队列
- HW_DECODER_RAW_IMAGE_SIZE / SW_DECODER_RAW_IMAGE_SIZE 留在 video_player.c（引用 vid_player）

---

## 三条解码路径

路径选择逻辑（video_player.c:~3250）：
```
if(H264 && single_track && use_hw_decoding)
    → Util_decoder_mvd_init()
      成功 → sub_state |= HW_DECODING    → 路径A（MVD）
      失败 → goto error（不fallback！直接报错）
if(!HW_DECODING && use_hw_color_conversion && YUV420P && 尺寸合法)
    → Util_converter_y2r_init()
      成功 → sub_state |= HW_CONVERSION  → 路径B（Y2R）
      ※ SBS（is_sbs_3d）不再排除Y2R（2026-03-30改）
else → 路径C（纯软件）
```

| 路径 | 条件 | 解码输出 | 上传方式 |
|---|---|---|---|
| A: MVD硬解 | N3DS + H264 + use_hw_decoding | ABGR32（4字节） | CPU Morton |
| B: Y2R | !HW_DECODING + use_hw_color_conversion + YUV420P | RGBA8888（BLOCK_8_BY_8预瓦片化） | `Draw_set_texture_data_direct`（memcpy，DMA已禁） |
| C: 纯软件 | O3DS 或两个HW选项关 或非YUV420P | ABGR8888 | CPU Morton |

**N3DS实际覆盖：**
- H.264 → 路径A（MVD）
- MPEG2 / H.265 等（YUV420P输出）+ Y2R开 → 路径B（Y2R）
- 其余（非YUV420P或Y2R关）→ 路径C
- O3DS：全走路径C（MVD不可用）

---

## 渲染管线

```
LCD物理屏 ← GSP_BGR8_OES (draw.c:84)
  ↑ GX DMA
Citro3D RenderTarget: GPU_RB_RGBA8 (draw.c:117)
  ↑ GPU采样
视频帧纹理: GPU_RGBA8 (video_player.c:4906，三条路径统一）
  ↑ CPU Morton（Vid_large_texture_set_data, use_direct=false）
解码器输出: ABGR32（MVD路径）
```

---

## 纹理滤波：用户设置 + 自动最近邻（2026-03-29）

- **用户项**：`vid_player.use_linear_texture_filter`（默认 `true`）→ `Draw_set_texture_filter` → GPU `LINEAR` / `NEAREST`（`draw.c`）。
- **自动规则**：若当前片源 **codec 尺寸** 命中下列任一，则 **强制 NEAREST**（即使用户选线性也关线性），利于点对点/像素锐利：
  - **高=240 且 宽为偶数**（含 400/640/800×240 等；**不必**单独写「宽=800」，避免 800×600 等非典型分辨率误伤）
  - **宽=400 且 高为偶数**（如 400×360；400×240 已由上一行覆盖）
- **实现**：`vid_texture.c` → `Vid_texture_dimensions_prefer_nearest(w,h)`；`video_player.c` → `Vid_effective_use_linear_texture_filter(eye_k)`（按 `video_info[eye].codec_width/height`，无效则回退 `EYE_LEFT`）。
- **应用时机**：打开视频后统一 `Vid_large_texture_set_filter`；设置里切换纹理滤镜按钮时同样走有效线性标志。

---

## MVD 硬件规格

- N3DS专属 H264 硬解器，最大分辨率：800×240 @ 30fps（H.264 Level 3.1）
- 当前使用输出格式：ABGR32（0x00041002，4字节/像素）
- Alpha修复：video_player.c中循环 px[0]=0xFF（MVD H264无alpha）

---

## SBS 3D 当前状态

**已实现**：N3DS H.264 MVD硬解，800×240→左右400×240，parallax barrier真3D。

**左右眼映射（已确认）**：**左对左、右对右** — 正确；`sbs_swap_eyes` 与立体视觉一致，无需再标「不确定」。

**设置**：LCD子菜单 → Screen mode: 3D / 2D / auto

**Mono 2D（sem 选 2D/auto 非 SBS）+ 机器 3D 滑块打开**：硬件会显示左右眼两个 framebuffer；原先右眼仍采样 `large_image[..][EYE_RIGHT]`，但单路解码只写入 `EYE_LEFT`，故右眼黑/空。**修复（2026-03-29）**：`num_of_video_tracks==1` 时右眼改为绘制与左眼相同纹理与坐标（`Vid_fit_to_screen` 右眼同步左眼几何；帧索引镜像同 SBS）。双视频轨仍走左右独立纹理。

**SBS关键代码位置：**
| 位置 | 说明 |
|---|---|
| `video_player.c:~4821` | 800×240强制is_sbs_3d=true，SAR=1:1 |
| `video_player.c:~4884` | SBS纹理初始化（EYE_LEFT宽=codec_width/2） |
| `video_player.c:~1875` | EYE_RIGHT draw_index跟随EYE_LEFT |
| `video_player.c:~6554` | CPU逐行split左右帧（使用预分配 vid_player.sbs_right_buf） |
| `vid_state.h: sbs_right_buf` | SBS右眼帧预分配缓冲（半宽×高×4字节），视频打开后分配，两处清理点free+NULL |
| `source/vid_screen.c` `Vid_fit_to_screen` | SBS模式 zoom=1, offset=0 |

**任意分辨率SBS修复（2026-03-26）：**
`Vid_fit_to_screen` 开头加：
```c
if(vid_player.is_sbs_3d && eye_index == EYE_RIGHT)
{
    vid_player.video_zoom[EYE_RIGHT]     = vid_player.video_zoom[EYE_LEFT];
    vid_player.video_x_offset[EYE_RIGHT] = vid_player.video_x_offset[EYE_LEFT];
    vid_player.video_y_offset[EYE_RIGHT] = vid_player.video_y_offset[EYE_LEFT];
    return;
}
```

---

## 优化待办

| 优先级 | 任务 | 难度 |
|---|---|---|
| ~~高~~ | ~~右眼right_buf预分配（现在每帧malloc/free）~~ | ✅ 已完成（2026-03-29） |
| ~~高~~ | ~~`is_sbs_3d`自动检测（width==800&&height==240）~~ | ✅ 已完成（2026-03-29，auto按钮） |
| ~~低~~ | ~~SBS Y2R tile行切割：memcpy/memmove → memcpy_asm~~ | ✅ 已完成（2026-03-30）：`video_player.c:~4759`；顶部加`extern void memcpy_asm`声明；memmove无重叠可安全替换 |
| ~~中~~ | ~~**Y2RU stride split（SBS Y2R_X2）**：两次Y2R+gap直接输出单眼数据，省掉tile行切割~~ | ✅ 已完成+验证（2026-04-02）：API改为直写（dst_left/dst_right/tex_width），vid_worker.c SBS 调用侧改直写，上传块换为 C3D_TexFlush+subtex，帧率提升显著，内存碎片归零 |
| ~~中~~ | ~~**2D Y2R_X2 直写纹理**：非SBS单眼路径用 transfer_gap 直写~~ | ✅ 已完成（2026-04-02）：新增 Util_converter_y2r_yuv420p_to_rgba8888_direct，vid_worker.c 2D Y2R_X2 分支改用 |
| ~~中~~ | ~~**O3DS decode_thread 移至 C1，C0 独留解码**~~ | ✅ 已完成（2026-04-02）：vid_lifecycle.c O3DS 分支 decode_thread affinity 0→1 |
| ~~中~~ | ~~**O3DS convert_thread 升 HIGH**~~ | ✅ 已完成（2026-04-02）：vid_lifecycle.c O3DS 分支 NORMAL→HIGH |
| ~~高~~ | ~~**FFmpeg ARMv6/ARMv5TE ASM 重编**：原库 `--disable-asm` 封掉所有外部汇编，MPEG-2 解码关键路径（IDCT、hpeldsp、mpegvideo motion）全跑纯 C~~ | ✅ 已完成（2026-03-30）：去掉 `--disable-asm` 重编，HAVE_ARMV6/ARMV5TE/VFP=1；新库在 `D:\3dsvideoplayermock\Topos0.01\ffmpeg_asm_built\lib\`，已替换 `library/lib/`；旧库备份于 `D:\3dsvideoplayermock\Topos0.01\library_lib_backup_no_asm\`；libavcodec.a 5.5MB→6.1MB；项目编译 0 error |
| 中 | **NEON汇编memcpy**：VLD1.128/VST1.128替代`test.s`的ldm/stm 36字节版，理论带宽2-4×。仅 NEONy2r 路径仍有 CPU memcpy，是当前主要剩余开销 | 中高 |
| ~~中~~ | ~~**音频解码移至C1**：`Vid_audio_decode_thread` 新增，在C1运行，C0专给视频解码~~ | ✅ 已完成（2026-04-02，Steps 1-6） |
| 中 | **O3DS APT core 1配额**：初始化完成后若无C2 core 1降回10%，所有C1线程严重节流。播放开始后改为50~70%可能是最大单点收益。位置：`vid_lifecycle.c` APT_SetAppCpuTimeLimit 调用处 | 中 |
| 中 | **A/V Sync via audio broadcast（Step 7，计划中）**：audio_decode_thread 更新 `vid_player.last_decoded_audio_pos`（已有），video 侧按此时间戳决定帧等待/丢帧；目前已有字段但未做精确同步逻辑 | 中 |
| 中 | GX_DisplayTransfer替代CPU Morton（仅影响软解/MVD路径，不影响Y2R路径） | 高（有坑，见note/202603280000.md） |
| 低 | **libmpeg2 替换 FFmpeg MPEG2 解码器**：ARM11 无 NEON，收益仅来自更低框架开销，估5~10%，集成复杂度高，性价比低 | 高 |
|| ~~低~~ | ~~**N3DS slice_cores限制**：已实现——vid_settings.c N3DS分支 slice_cores[1]=false，仅 C0/C2/C3；O3DS 包含 C1（start_core=1）（2026-04-03完成）~~ | ✅ |
| 低 | GPU做SBS分割 | 很高 |

---

## 可删 / 可拆清单

### ~~立刻可删（死代码 / 废弃功能）~~ ✅ 已完成（2026-03-30）

| 位置 | 内容 | 状态 |
|---|---|---|
| `menu.c` `Menu_send_app_info_thread` / `Menu_update_thread`（宏、声明、变量、threadCreate/Join、函数体，共~130行） | 遥测上报线程 | ✅ 已删 |
| `video_player.c` 语言 hack 两处（"de" scale 分支 + "ro"/"de" 字号分支） | `strcmp(config.lang,…)` 折叠为英文分支 | ✅ 已删 |
| `romfs/gfx/msg/` 非 `_en` 文件 18 个 | menu_de/es/hu/it/jp/pl/ro/ryu/zh-cn + vid 同组 | ✅ 已删 |

编译：**0 error 0 warning**（2026-03-30）

### ~~Controls 按钮 + 移动/缩放视频功能~~ ✅ 已删除（2026-03-30）

| 位置 | 内容 | 状态 |
|---|---|---|
| `video_player.c` / `vid_hid.c` | 下屏 Controls 按钮及其打开的控制说明菜单（`is_displaying_controls`、`VID_CMD_OPEN_CONTROLS/CONTROL_CLOSE`、control.t3x 纹理加载/释放） | ✅ 已删 |
| `video_player.c` / `vid_hid.c` | C-Stick 移动视频代码（`move_content_mode`、`Vid_hid_compute_move_deltas`、move content 主块） | ✅ 已删 |
| `video_player.c` / `vid_hid.c` | L/R 键缩放视频代码（`VID_HID_SHRINK/ENLARGE_CONTENT_CFM`，size_changes 计算与应用） | ✅ 已删 |
| `vid_hid_macros.h` | `VID_HID_CONTROL_*/MOVE_CONTENT_*/SHRINK/ENLARGE_CONTENT_CFM` 宏 | ✅ 已删 |
| `vid_cmd.h` | `VID_CMD_CONTROL_CLOSE`、`VID_CMD_OPEN_CONTROLS`、`VID_CMD_SE0_MOVE_MODE` | ✅ 已删 |
| `vid_hid.h` / `vid_state.h` | `control_button`、`move_content_button`、`is_displaying_controls`、`control_texture_handle`、`control[2]` 字段；`VidHidRouterState` 中 `is_displaying_controls`、`move_content_mode` | ✅ 已删 |
| `vid_state.h` / `vid_settings.c` | `Vid_move` 枚举与 `move_content_mode` 字段已删；存档 **`SETTINGS_ELEMENTS_V7`**（16 字段）：去掉原 `<10>` move 项，`<10>`～`<15>` 为 remember/playback/音视频开关/restart/threads；读档优先 V7，旧 V6 仍可读（忽略原 `[10]` move） | ✅ 已做 |
| `vid_state.h` / `romfs/gfx/msg/vid_en.txt` | `Vid_msg` 去掉 Controls/控制说明/Move 模式相关枚举项；`vid_en.txt` 重排为 **0～25** 共 26 条，与 `MSG_MAX` 一致 | ✅ 已做 |

**仍保留（渲染用）**：`video_x/y_offset`、`video_zoom` 仍在 `vid_player` 中并由 `Vid_fit_to_screen` 等使用；用户输入已不再修改其「平移/缩放游戏式」数值（仅几何/fit 逻辑会写）。

**新实例可选核对**：`make` 已通过；若不放心可在仓库根执行 `rg "MSG_CONTROLS|MSG_MOVE_MODE|move_content_mode|Vid_move" --glob "*.c" --glob "*.h"` 应无匹配（`mbedtls` 等第三方目录除外）。

编译：**0 error 0 warning**（2026-03-30）

### vid_worker.c 已完成内容（2026-04-02）

从 `video_player.c` 移至 `source/vid_worker.c`，头文件 `include/vid_worker.h`：

| 函数 | 说明 |
|---|---|
| `Vid_effective_use_linear_texture_filter` | 纹理滤波有效性判断（供vid_texture.c和vid_decode_thread用） |
| `Vid_update_decoding_statistics`（static） | 帧解码时间统计（仅内部调用，保持static） |
| `Vid_update_decoding_statistics_every_100ms` | 100ms统计刷新 |
| `Vid_get_media_duration` / `Vid_get_current_media_pos` | 媒体时长/当前位置计算 |
| `Vid_has_video` | 是否有视频轨判断 |
| `Vid_log_media_info` | 媒体信息日志 |
| `frame_worker_thread_start/end` | 帧解码计时回调（弱函数实现） |
| `dav1d_worker_task_start/end` | dav1d任务计时回调（弱函数实现） |
| `Vid_decode_video_thread` | 视频解码线程 |
| `Vid_convert_thread` | YUV→RGB色彩转换+纹理上传线程 |

**vid_worker.c includes（顺序）**：`vid_worker.h`、`extern memcpy_asm`、`vid_texture.h`、`vid_sync.h`、`video_player.h`、`<inttypes.h>`、`<stdlib.h>`、`<string.h>`、`system/util/converter.h`、`decoder.h`、**`err.h`**（必须显式加）、`log.h`、`speaker.h`、`str.h`、`util.h`

**陷阱：`DEF_ERR_*`/`DEF_SUCCESS` 必须显式 `#include "system/util/err.h"`** — 不经 vid_state.h 传递，拆出新 .c 文件时必加。`vid_seek_engine.c` 同此陷阱。`VID_SEEKBAR_*` 常量必须用 `double` 字面量（`5.0` 非 `5.0f`），否则 `-Wdouble-promotion`。

### vid_input.c 已完成内容（2026-04-02）

从 `video_player.c` 移至 `source/vid_input.c`，无新头文件（`Vid_hid` 声明已在 `video_player.h`）：

| 内容 | 说明 |
|---|---|
| `HID_*_SEL`/`HID_*_DESEL` 宏（~107个） | 直接引用 `vid_player` 的旧式宏，仅在 Vid_hid 内部用，保持为本地 #define |
| `vid_fill_layout` / `vid_fill_router_state` / `vid_fill_locks` | static helper，将 vid_player 字段打包成 VidHidLayout/RouterState/UiLocks 快照 |
| `Vid_process_hid_cmd_queue` | 命令执行器：从队列 pop → switch-case 写 vid_player 或发 decode_thread 请求 |
| `Vid_hid` | 顶层入口：home menu 处理 + SEL/DESEL 选中反馈 + enqueue + process + 滚动计算 |

**vid_input.c includes**：`video_player.h`、`vid_state.h`、`vid_screen.h`、`vid_texture.h`、`vid_worker.h`、`vid_cmd.h`、`vid_hid.h`、`<3ds.h>`、`<stdlib.h>`（`abs`）、`system/sem.h`、`util/err.h`、`util/expl.h`、`util/hid.h`、`util/log.h`、`util/util.h`

**跨文件依赖**：`Vid_expl_callback`/`Vid_expl_cancel_callback` 仍定义在 `video_player.c`，去掉 static，vid_input.c 用 `extern` 引用。Makefile 自动 glob，无需手动添加。

**陷阱**：`abs` 需显式 `#include <stdlib.h>`，不经 vid_state.h 链传递（编译时报 implicit declaration）。

### vid_decode.c 已完成内容（2026-04-02）

从 `video_player.c` 移至 `source/vid_decode.c`，头文件 `include/vid_decode.h`：

| 函数 | 说明 |
|---|---|
| `Vid_decode_thread` | 主解码调度线程：文件打开、音频/视频初始化、seek、abort、**音频包路由→audio_decode_thread**、视频帧分发（2026-04-02：音频软解已移出） |
| `Vid_audio_decode_thread` | 音频解码线程（2026-04-02新增）：接收 DECODE_REQUEST → PCM格式转换 → 音量调整 → ndsp speaker；Seek时排空队列；Abort时排空+通知 |
| `Vid_read_packet_thread` | FFmpeg 读包线程（file→packet queue） |
| `Vid_expl_callback` | 文件浏览器选中回调：malloc Vid_file消息 → enqueue DECODE_THREAD_PLAY_REQUEST |
| `Vid_expl_cancel_callback` | 文件浏览器取消回调（空实现） |

**新增头文件**：
- `include/vid_decode.h`：声明上述 4 个函数（含 `#include "system/util/str.h"` 给 Str_data 参数）
- `include/vid_lifecycle.h`：声明 `Vid_init_debug_view_data` / `Vid_init_media_data` / `Vid_init_video_data` / `Vid_init_audio_data`（这 4 个函数从 static 改为全局，供 vid_decode.c 调用；将来 vid_lifecycle.c 拆出时直接移定义）

**跨文件依赖处理**：
- `Vid_init_*` 4 个函数：去掉 video_player.c 里的 `static`，声明移到 `vid_lifecycle.h`，`video_player.c` 和 `vid_decode.c` 均 include 此头文件
- `HW_DECODER_RAW_IMAGE_SIZE` / `SW_DECODER_RAW_IMAGE_SIZE` 宏：从 `video_player.c` 移除（仅 vid_decode.c 用，定义在 vid_decode.c 顶部）
- `vid_input.c` 的手工 `extern` 声明改为 `#include "vid_decode.h"`

**vid_decode.c includes**：`video_player.h`、`extern memcpy_asm`、`vid_state.h`、`vid_texture.h`、`vid_settings.h`、`vid_sync.h`、`vid_screen.h`、`vid_worker.h`、`vid_lifecycle.h`、`vid_decode.h`、`<string.h>`、`<stdlib.h>`、`<malloc.h>`、`<time.h>`（`srand(time(NULL))`）、`system/menu.h`、`sem.h`、`converter.h`、`err.h`、`expl.h`、`file.h`、`hid.h`、`log.h`、`speaker.h`、`watch.h`

**陷阱**：`time()` 需显式 `#include <time.h>`；`vid_decode.c` 不需要 `<libavutil/cpu.h>`（avcodec 调用封装在 Util_decoder_* 内）。

### vid_draw.c 已完成内容（2026-04-02）

从 `video_player.c` 移至 `source/vid_draw.c`，头文件 `include/vid_draw.h`：

| 函数 | 说明 |
|---|---|
| `Vid_main` | 主渲染循环（~1114行）：帧时序、缓冲管理、VPS统计、3D/SBS渲染、底屏UI（设置菜单/调试图/seek条/音轨选择） |
| `Vid_draw_init_exit_message` | init/exit状态屏渲染（非static，供Vid_init/Vid_exit跨文件调用） |

**vid_draw.c includes**：`video_player.h`、`vid_state.h`、`vid_texture.h`、`vid_screen.h`、`vid_sync.h`、`vid_worker.h`、`vid_draw.h`、`<inttypes.h>`、`<math.h>`、**`<malloc.h>`**（mallinfo）、`<libavutil/cpu.h>`、`system/draw/draw.h`、`system/menu.h`、**`system/sem.h`**（Sem_config/Sem_state/DEF_SEM_MODEL_*）、`system/util/converter.h`、`cpu_usage.h`、`err.h`、`expl.h`、`hw_config.h`、`log.h`、`speaker.h`、`watch.h`

**陷阱**：`Sem_config`/`Sem_state` 类型和 `Sem_get_config`/`Sem_get_state` 函数来自 `system/sem.h`，拆出新文件时必须显式 include；`mallinfo`/`struct mallinfo` 来自 `<malloc.h>`。

### vid_lifecycle.c 已完成内容（2026-04-02，Step 8）

从 `video_player.c` 移至 `source/vid_lifecycle.c`（`video_player.c` 同时删除）：

| 内容 | 说明 |
|---|---|
| `vid_player` / `vid_msg` 全局变量定义 | 所有模块通过 `video_player.h` / `vid_state.h` extern 访问 |
| `Vid_query_init_flag` / `Vid_query_running_flag` / `Vid_get/set_use_hw_color_conversion` | 状态查询与 HW色彩转换模式读写 |
| `Vid_resume` / `Vid_suspend` / `Vid_load_msg` / `Vid_init` / `Vid_exit` | 生命周期公开 API |
| `Vid_init_variable` + `Vid_init_debug_view_mode` + `Vid_init_player_data` + `Vid_init_ui_data`（static） | 内部初始化辅助 |
| `Vid_init_debug_view_data` / `Vid_init_media_data` / `Vid_init_video_data` / `Vid_init_audio_data` | 跨文件可见（`vid_lifecycle.h` 声明，供 `vid_decode.c` 调用） |
| `Vid_init_thread` / `Vid_exit_thread` | 初始化/退出线程（全局，供 debug 符号访问） |

**includes**：与原 `video_player.c` 完全一致，无新增。

### 低优先级清理

| 位置 | 内容 | 说明 |
|---|---|---|
| `converter.c` | `yuv420p_to_rgb565le` / `yuv420p_to_rgb888le` / `rgb888_rotate_90` 系列（~4函数 + 对应 2 个 `.s` ASM文件） | 当前 `video_player.c` 不调用，属未使用路径 |
| `source/system/util/cpu_usage.c` + `sem.c` MENU_ADVANCED | sem菜单 Advanced 里的 CPU 占用率图形显示（`Util_cpu_usage_draw()`、`Util_cpu_usage_query_show_flag()`、`Util_cpu_usage_init()`、相关按钮/变量）| 用户已决定删除；`video_player.c` 侧的调用已于 2026-03-30 删除（布局重整时一并移除），`sem.c` 侧 Advanced 菜单 UI 待清理 |

---

## 已知陷阱

1. **mvdstdGenerateDefaultConfig 会覆盖 output_type** — 调用后必须显式设 `output_type = MVD_OUTPUT_ABGR32`（decoder.c:1903）
9. **TEST BUILD 硬编码已删除** — 原 video_player.c:3100 有 `use_hw_color_conversion = false` 强制覆盖，导致sem菜单的Y2R按钮完全无效。已删除。
10. **DEF_DRAW_DMA_ENABLE 已禁用（2026-03-30）** — `Draw_set_texture_data_direct` 的DMA路径有stride bug：DMA把目标纹理的stride（tex_width×32）错误地应用在源数据上（应用DST_CONFIG但API不支持，退而用SRC_CONFIG），导致读取源数据时跨度过大，Y2R上传时x轴右侧出现随机乱块。禁用DMA后走手动tile行memcpy，正确处理pic_width ≠ tex_width的stride差。位置：`include/system/draw/draw_types.h:9`。MVD路径走CPU Morton不经过此函数，不受影响。
11. ~~**Y2R SBS stride split**~~ — ✅ 已验证（2026-04-02）：gap参数输出侧生效，硬件行为正确，Y2R_X2 SBS帧率提升显著无图像错乱。API已改直写（dst_left/dst_right/tex_width），旧风险消除。位置：converter.c Util_converter_y2r_yuv420p_sbs_to_rgba8888，vid_worker.c Y2R_X2 SBS块。
2. **GPU_RGB8（3字节）不可用** — PICA200上产生3重影花屏，用GPU_RGBA8
3. ~~**Y2R路径格式不匹配**~~ — 已确认无bug：Y2R实际调用 `yuv420p_to_rgba8888`（OUTPUT_RGB_32），输出RGBA8888+BLOCK_8_BY_8预瓦片化，与GPU_RGBA8完全匹配，跳过CPU Morton，显示正常
4. **DEF_DRAW_CYAN 不存在** — 用 DEF_DRAW_AQUA 替代
5. **GX_DisplayTransfer PPF事件不可靠** — 系统共享，无法区分是谁的DMA（已回退，勿重试）
6. **换目录后必须 make clean** — .d文件含绝对路径，旧路径导致构建失败
7. **Util_load_msg / Util_parse_file 严格** — 任意一个索引缺失整个消息数组全部加载失败→菜单全空白。sem_en.txt 必须覆盖 0~MSG_MAX-1 所有索引

14. **decode_thread 通知队列不可 blocking 等待（2026-04-02发现）** — `decode_thread_notification_queue` 在主循环末尾以 timeout=0 非阻塞轮询（`queue.c` 证实：`next_index==0 && wait_us>0` 才等待，wait_us=0 立即返回 `DEF_ERR_TRY_AGAIN`）。若在命令 handler 里 `while(true)` 等待该队列上的特定通知，会消费掉其他线程（read_packet_thread 等）发来的通知，破坏 seek/abort 状态机。教训：Seek 只 fire-and-forget 发请求；只有 Abort（此时所有其他线程已停止）才可 blocking 等待。

12. **FFmpeg重编必须加 `--disable-neon`（2026-03-30）** — 去掉`--disable-asm`后 GNU as 能汇编 NEON → configure 将 HAVE_NEON 设为 1 → arm-none-eabi 非Linux分支 `ff_get_cpu_flags_arm()` 直接返回 `AV_CPU_FLAG_NEON`（用 HAVE_NEON，非 HAVE_NEON_EXTERNAL）→ blockdsp_init_neon 指向 ff_clear_blocks_neon → ARM11 执行 NEON → **崩溃**（PC=mpeg_decode_slice，LR=ff_clear_blocks_neon）。修复：加 `--disable-neon`。构建脚本：`D:\\3dsvideoplayermock\\Topos0.01\\build_ffmpeg_asm.sh`（已更新）。
13. **替换 library/lib/*.a 后必须 make clean** — devkitPro Makefile 不跟踪 `.a` 依赖，直接 make 不重链，新 ASM 代码不生效。换库后必须 `make clean && make`。
8. **Util_cpu_usage_set_show_flag 有 init 保护** — 未调用 `Util_cpu_usage_init()` 时点ON无效，函数直接return

---

## sem菜单精简（2026-03-29 持续）

### Control 子菜单删除
- **整个 MENU_CONTROL 子菜单已删除**：`MENU_CONTROL` 枚举删除，MENU_ADVANCED 变为索引1，MENU_MAX=2
- **scroll_speed 写死为 0.5**：默认值、文件加载均硬编码，不再从文件读取，不再 watch
- **删除**：`HID_CTRL_SCROLL_SPEED_BAR_*` 宏、`sem_scroll_speed_slider/bar` 变量、绘制、HID SEL/CFM/DESEL、watch_add/remove
- **顶部菜单**：原三项（LCD/Control/Advanced）改为两项（LCD/Advanced），Advanced y 位置从 50 → 25

### LCD 子菜单删除 time_to_turn_off_lcd / time_to_enter_sleep 滑块
- **两个值永久写死为 0（off）**：默认值硬编码，文件加载也写死，不读文件值
- **删除**：`HID_LCD_LCD_OFF_BAR_*` / `HID_LCD_SLEEP_BAR_*` 宏；`sem_screen_off_time_slider/bar` / `sem_sleep_time_slider/bar` 变量；绘制块；HID SEL/CFM/DESEL；watch_add/remove；校验逻辑
- **HID_SCROLL_MODE_DESEL**：删除 `sem_screen_off_time_bar.selected` / `sem_sleep_time_bar.selected` 条件
- **hw_config 线程**：判断 `> 0` 才触发关屏/sleep，值为 0 则条件永不成立，逻辑正确

---

## 耳机防睡眠逻辑删除（2026-03-29）

- **删除**：`Vid_update_sleep_policy()` 函数及声明（`source/video_player.c:~2579`）
- **原调用处**（`video_player.c:~2352`）改为始终 `aptSetSleepAllowed(true)`：插耳机播放时不再阻止合盖睡眠
- **结果**：合盖行为完全由 sem 的 sleep timer 控制（当前 timer=0 即永不自动睡眠），系统本身合盖仍可正常睡眠

---

## 颜色转换模式三选一（计划，未实现）

**目标**：sem菜单 Advanced 里 `HW color conv` 从现在的「CPU / Y2R」两按钮改为三按钮：

| 按钮 | 行为 |
|---|---|
| `CPU`（强制） | 永远走路径C（swscale），忽略YUV420P和尺寸 |
| `Y2R`（强制） | 永远尝试Y2R，失败则报错（不fallback） |
| `auto`（默认） | 优先Y2R（YUV420P且宽高均为8整数倍），否则自动fallback到CPU |

**auto模式fallback条件**：`width % 8 != 0 || height % 8 != 0` → 走CPU，避免tile行切割切错。

**实现要点**：
- `vid_player.use_hw_color_conversion` 从 `bool` 改为 3值枚举（`HW_CONV_CPU` / `HW_CONV_Y2R` / `HW_CONV_AUTO`）
- Y2R初始化条件按枚举分支处理
- sem菜单增加第三个按钮，消息文件新增对应idx
- SBS Y2R分支的8整除guard：auto模式下不满足则is_y2r=false

---

## sem菜单 Advanced 相关（2026-03-29）

- **位置**：sem菜单 → Advanced → 底部（需下滑）
- **功能**：`HW color conv` 标签 + `CPU`/`y2r*2`/`NEONy2r` 三按钮，切换 `vid_player.use_hw_color_conversion`（uint8_t，值0/1/2）
- **接口**：sem.c 通过 `Vid_get_use_hw_color_conversion()` / `Vid_set_use_hw_color_conversion(uint8_t)` 访问；常量 `VID_HW_CONV_CPU/Y2R_X2/NEON_Y2R` 在 `include/video_player.h`
- **三个模式**：`CPU(0)`=swscale；`y2r*2(1)`=双次Y2R stride-split（SBS直接出左右眼，当前默认）；`NEONy2r(2)`=单次Y2R全帧→memcpy_asm tile行切割分左右眼
- **Advanced菜单新增滚动**：`MENU_ADVANCED_Y_OFFSET_MIN = -50`（sem.c:31）
- **sem_en.txt** idx 73/74/75/76，MSG_MAX = 77（76=NEONy2r）
- **滚动修复**：Advanced子菜单所有选项绘制 y 坐标加 `sem_y_offset`（之前只有 back 键在动，选项静止）
- **back键固定**：back 按键绘制改为固定 y=0，不随 `sem_y_offset` 移动
- **CPU占用显示移到下屏**：`Util_cpu_usage_draw()` 从上屏（TOP_LEFT/TOP_RIGHT）移除，改在下屏 section 调用（video_player.c 诊断文字之后）；`Vid_draw_init_exit_message` 无下屏 section，同步移除

## 打开新视频不触发全屏切换（2026-03-29 修订）

- **现状**：打开有视频轨的文件时，**不再**自动进入全屏模式。打开前是什么模式（UI / 全屏），打开后保持不变。
- **删除**：`video_player.c:~3771` 原有的 `vid_player.auto_full_screen_count = 0` 和 `Vid_enter_full_screen(0)` 两行。
- **保留**：`Vid_fit_to_screen(FULL_SCREEN_WIDTH, FULL_SCREEN_HEIGHT, i)` 和 `Util_hid_reset_key_state` 不动。
- **位置**：`video_player.c` — 解码就绪后「Fit video to screen if file has video tracks」分支（原注释已同步更新）。

## Night Mode 固定（2026-03-29）

- **`config.is_night` 永远为 `true`**，系统固定 night 模式，light 模式已删除。
- **删除**：night on/off 按钮、flash 按钮的 HID宏/变量/watch/init/draw/handler 全部从 `sem.c` 移除。
- **删除**：flash 定时器（50ms 切换 `is_night` 的循环）及 `previous_flash_ts` 变量、`UPDATE_FLASH_INTERVAL_MS` 宏。
- **删除**：`config.is_flash` 字段相关的默认值、watch add/remove。
- **保留**：`is_night` 的文件保存行（`<6>`）；亮度/熄屏时间等 LCD 菜单控件不变。
- **位置**：`source/system/sem.c`

---

## UI 精简（2026-03-29）

- **上屏永久最大化（相对原版）**：目标为 **UI 模式开启时上屏纯视频** — 上屏 **不出现** 顶条/系统式 OSD，信息与控制 **集中在下屏**；避免上屏视频因叠加 UI 而 **被缩放或让位**。与「与上游（原版）差异」表中「上屏与 UI」一致。
- **删下屏渐隐逻辑**：`turn_off_bottom_screen_count` 字段及全部相关逻辑已删除。`Vid_enter_full_screen` 改为无参，全屏时底屏不再倒计时降亮/关LCD。OLD2DS 全屏时填黑逻辑保留（去掉了对已删字段的判断）。
- **删上屏顶条 UI**：`Draw_top_ui(...)` 调用全部删除（menu.c 初始化+主循环、sem.c 设置菜单）。时间/WiFi/省电绿叶不再显示。函数定义保留在 draw.c，无需改 draw.h。

---

## 下屏诊断布局重整（2026-03-30，再设计 2026-03-30）

- **当前状态**：监控文字分 5 组，总高约 135px（y=1~132），使用整个下屏中间区域，CPU 为视觉核心
  - STATUS  y=1   : `state  VER`（左绿右弱绿，scale 0.40）
  - MEDIA   y=13  : `A:codec  V:codec  WxH  @fps`（默认色，scale 0.40）
  - DECODE  y=25  : `Dec:MVD/HW  Tex:LINEAR  SAR:w/h  z:zoom`（绿，scale 0.40）
  - CPU     y=40  : `CPU  XMHz`（黄，scale 0.44）+ 4 条 bar 图（C0~C3，y=53/67/81/95，每条 265×7px，黄色填充/弱白轨道）+ 百分比文字 x=286
  - MEMORY  y=111 : `Lin:XKB  Heap:free/total KB`（黄，scale 0.39，**每秒缓存**）
  - GEOMETRY y=123: `SBS:X  3D:X  img:WxH  xo:X  yo:X`（弱白，scale 0.39，次要信息）
- **CPU/内存所有值统一 1s 静态缓存**：`static diag_ts` 控制，每秒刷新一次，每帧零 mallinfo/cpu_usage 调用
- **Bar 图参数**：x=18, w=265（track），填充 w=(int)(265*pct/100)，h=7，颜色 track=WEAK_WHITE / fill=YELLOW
- **息屏零开销**：同 is_bottom_lcd_on guard（不变）
- **位置**：`video_player.c` line ~1376~1460

---

## 关键代码位置

| 位置 | 说明 |
|---|---|
| `include/video_player.h:13` | DEF_VID_VER 版本字符串 |
| `include/vid_screen.h` + `source/vid_screen.c` | 屏幕几何与全屏/亮度（见「解耦重构计划」vid_screen 行） |
| `include/vid_state.h` | 所有枚举/结构体/常量（2026-03-28 新建） |
| `vid_lifecycle.c:47~48` | vid_msg / vid_player 全局变量定义（2026-04-02 迁入，video_player.c 已删） |
| ~~`video_player.c:~2205`~~ | ~~下屏版本号显示~~ — 已并入 row 0（line ~1376），条目过时 |
| ~~`video_player.c:~2267`~~ | ~~下屏诊断显示~~ — 已并入 5 行布局（line ~1374），条目过时 |
| `video_player.c:~4153` | O3DS强制 use_hw_decoding=false |
| `video_player.c:~4821` | 800×240强制is_sbs_3d=true，SAR=1:1 |
| `video_player.c:~4906` | 纹理初始化：RAW_PIXEL_ABGR8888 |
| `source/vid_texture.c` | `Vid_texture_dimensions_prefer_nearest`：命中则强制 NEAREST（见「纹理滤波」） |
| `source/video_player.c` | `Vid_effective_use_linear_texture_filter`：用户线性 ∧ 非自动最近邻；两处 `Vid_large_texture_set_filter` |
| `video_player.c:~5466` | seek重开解码器（解决heap碎片） |
| `video_player.c:~6554` | SBS帧分割（CPU逐行memcpy） |
| `decoder.c:868` | mvdstdInit(..., MVD_OUTPUT_ABGR32, ...) |
| `decoder.c:1903` | GenerateDefaultConfig后强制覆盖output_type=ABGR32 |
| ~~`decoder.c:2576`~~ | ~~ZeroCopy实现起点~~ — ZeroCopy已回退，会增加内存碎片，当前版本无此优化 |
| `source/system/sem.c:18` | cpu_usage.h include（修编译，2026-03-29加） |
| `source/system/menu.c:~254` | Util_cpu_usage_init() 调用（2026-03-29加，缺失导致ON按钮无效） |
| `source/system/sem.c:MENU_ADVANCED` | HW color conv CPU/Y2R 按钮（2026-03-29加） |
| `source/system/sem.c:MENU_LCD` | Screen mode 3D/2D/auto 三按钮（2026-03-29加 auto），MSG_AUTO=69，按钮x=15/120/225 宽=80，auto缩放0.5 |
| `source/video_player.c:~3750` | AUTO模式 is_sbs_3d 判断：h==240 && w>=640 → SBS，否则2D |
| `include/system/draw/draw_types.h:9` | `DEF_DRAW_DMA_ENABLE` 已注释禁用，`Draw_set_texture_data_direct` 走memcpy路径（2026-03-30） |
| `video_player.c:~3250` | Y2R初始化条件：`!HW_DECODING && use_hw_color_conversion`（去除了`!is_sbs_3d`，2026-03-30） |
| `video_player.c:~4868` | SBS分割：Y2R走tile行切割（`is_y2r`分支），线性走像素行切割（保留原逻辑） |
| `video_player.c:~1376` | 底屏诊断区 5 组布局（2026-03-30 再设计）：STATUS y=1 / MEDIA y=13 / DECODE y=25 / CPU-bars y=40~107 / MEMORY y=111 / GEOMETRY y=123，总高~135px |
| `video_player.c:~1424` | CPU bar chart：header y=40（yellow scale 0.44），C0~C3 bar y=53/67/81/95，bar 265×7px，填充 YELLOW / 轨道 WEAK_WHITE，百分比 x=286，所有值 1s 静态缓存 |
| `romfs/gfx/msg/sem_en.txt idx 69` | "auto(y240x≧640)"（原"Auto"，2026-03-29改） |
| `include/video_player.h` | Vid_get/set_use_hw_color_conversion() 声明 |
| `source/video_player.c:~277` | Vid_get/set_use_hw_color_conversion() 实现 |
| `source/video_player.c` DECODE行 | `ASM:A6/A5/--` 指示器：`av_get_cpu_flags()` 检查 `AV_CPU_FLAG_ARMV6`，static cache，首次调用后不再查询；`#include <libavutil/cpu.h>` 已加入项目顶部 |
| `source/system/util/cpu_usage.c:152` | CPU占用绘制位置：下屏 x=220 y=90（box 100×60，ALIGN_RIGHT→box 覆盖 220~320 右侧，CENTER_Y→中线≈120），黄色。**坑：原为 x=360 超出 320px 底屏不可见，已修为 x=220** |
| `romfs/gfx/msg/sem_en.txt` | sem菜单消息文件，必须含0~75全部索引（MSG_MAX=76），非英文已删除 |
| `romfs/gfx/msg/vid_*.txt` | 播放器 `Vid_msg` 字符串：`MSG_BRIGHTNESS`=索引32，须与 `vid_state.h` 枚举顺序一致。曾误把「Only subtitle」等放在32~34、亮度在35，导致亮度提示前缀错字（2026-03-29 已按枚举重排32~37） |

---

## Eco 模式（2026-03-29 写死）

- **`config.is_eco` 永远为 `true`**：默认值（sem.c:281）本来就是 true；文件加载处（sem.c:384）也改为写死 `true`，不再从存档读取。
- **sem 里无 eco 切换按钮**（本 fork 已无此 UI）。
- **作用**：控制 GPU 渲染频率——eco=true 时只在 watch 变量变化或 `Draw_set_refresh_needed(true)` 时渲染；eco=false 则每帧强制渲染（耗电）。
- **位置**：`source/system/sem.c:281`（默认）、`sem.c:384`（文件加载，已改）

---

## 帧率限制 / 主循环节奏

**不需要也没有 svcSleepThread 帧率限制**，使用更精确的硬件同步：

| 情况 | 机制 | 位置 |
|---|---|---|
| 有东西要画 | `C3D_FrameBegin(C3D_FRAME_SYNCDRAW)` 阻塞至 VSync，天然锁 60fps | `draw.c:972` |
| eco=true 且无变化（跳过渲染） | `gspWaitForVBlank()` 主动睡到下一 VBlank | `video_player.c:2344/2407`、`sem.c:736` |

两条路径都不空转，不需要额外 svcSleepThread。

---

## 音频处理路径（2026-04-02 重构后）

**流水线：**
```
Vid_decode_thread (C0)
  → Util_decoder_ready_audio_packet / Util_decoder_audio_decode
  → malloc Vid_audio_decoded_data { audio*, audio_samples, pos, packet_index }
  → Util_queue_add(audio_decode_thread_command_queue, AUDIO_DECODE_THREAD_DECODE_REQUEST)

Vid_audio_decode_thread (C1)
  → Util_converter_convert_audio (PCM格式转换, 最多2ch, S16输出)
  → 音量缩放 (vid_player.volume != 100 时 int16 乘法, 溢出 clamp INT16_MAX)
  → Util_speaker_add_buffer (最多重试125次×2ms=250ms)
  → free converted / audio_data
```

**Seek/Abort 处理：**
- **Seek**：decode_thread 调 `Util_speaker_clear_buffer` + 发 `AUDIO_DECODE_THREAD_SEEK_REQUEST`（SEND_TO_FRONT，不等通知）→ audio_decode_thread 排空队列旧数据（free，不送 speaker）
- **Abort**：decode_thread 发 `AUDIO_DECODE_THREAD_ABORT_REQUEST`（SEND_TO_FRONT）→ **等** `AUDIO_DECODE_THREAD_FINISHED_ABORTING_NOTIFICATION` → 再 `Util_speaker_exit()` / `Util_decoder_close_file()`

**关键设计约束（陷阱）**：
- decode_thread 通知队列设计为**非阻塞轮询**（主循环末尾 timeout=0 poll），不可在命令 handler 里 blocking 等待通知队列——会吃掉其他线程的通知，破坏状态机
- Seek 只能 fire-and-forget；Abort 可以 blocking 等（因为其他线程已全部停止发通知）

**相关文件**：
- `source/vid_decode.c`：`Vid_decode_thread`（路由）、`Vid_audio_decode_thread`（PCM转换+speaker）
- `include/vid_state.h`：`Vid_audio_decoded_data`、`AUDIO_DECODE_THREAD_*` 枚举
- `include/vid_decode.h`：`Vid_audio_decode_thread` 声明
- `source/vid_lifecycle.c`：audio_decode_thread 在 `Vid_init_thread` 末尾创建（C1，HIGH），`Vid_exit_thread` 里 join+free

**DSP / decoder**：`source/system/util/decoder.c`（FFmpeg 软解 → PCM）、`source/system/util/speaker.c`（ndsp 输出）

---

## 线程管理 / 核心分配

### 系统级常驻线程

| 线程 | 核心 | 优先级 | 职责 |
|---|---|---|---|
| 主线程（aptMainLoop） | core 0 | — | 渲染 + HID + Vid_main |
| `menu_worker_thread` | core 0 | ABOVE_NORMAL | 子应用 init/exit 等耗时操作 |
| `menu_update_thread` | core 1 | REALTIME | 时钟/状态定时更新 |
| `sem_hw_config_thread` | core 1 | HIGH-1 | 亮度/LCD/睡眠硬件配置 |
| `hid_scan_thread` | core 0 | REALTIME | 按键扫描 |

### 播放器线程（播放期间）

**N3DS / O3DS 主线程（全部在 C0，2026-04-03 统一）：**

| 线程 | 核心 | 优先级 | 职责 |
|---|---|---|---|
| `Vid_decode_thread` | **core 0** | REALTIME | 包路由调度/状态机（几乎全在等队列，CPU 消耗极低） |
| `Vid_decode_video_thread` | **core 0** | NORMAL | 调用 FFmpeg send_packet/receive_frame，实际解码交给 worker |
| `Vid_convert_thread` | **core 0** | HIGH | 颜色转换（CPU Morton/Y2R） |
| `Vid_read_packet_thread` | **core 0** | REALTIME | 文件读包 → 队列 |
| `Vid_audio_decode_thread` | **core 0** | HIGH | PCM格式转换 + ndsp speaker输出 |

**FFmpeg worker 线程（fake_pthread，avcodec_open2 时创建，thread_count-1 个，2026-04-03）：**

| 机型 | 轮询顺序 | start_core | 优先级 |
|---|---|---|---|
| N3DS | C2 → C3 → C0 → C2 → … | 2 | NORMAL（2026-04-03改：HIGH→NORMAL） |
| O3DS | C1 → C0 → C1 → C0 → … | 1 | NORMAL |

**O3DS thread_count=3（2 workers）核心分布（2026-04-03 实测）：**
- C0：decode_video_thread（主 FFmpeg 调用）+ 1 worker = 2 个解码实体
- C1：1 worker
- 理论解码性能提升：**20%～50%**
- 实测（N3DS 以 O3DS 模式运行）：**MPEG-2 800×240 SBS 3D → 24fps 稳定**（默认 thread_count 计划设为 3）

### Core 3 使用情况

- **`threadCreate` 不用 core 3**：core 3 是半系统核，可用性不稳定，硬绑定会崩
- **fake_pthread（FFmpeg 内部多线程）用 core 3**：`vid_settings.c:70` 对所有核动态检测 `Util_is_core_available(i)`，可用则用，不可用则跳过，安全
- **结论**：core 3 已通过 fake_pthread 按需使用，无需改动

### APT CPU 时间限制（core 1 配额）

| 时机 | 值 |
|---|---|
| 启动默认 | 30%（menu.c:214） |
| 视频初始化中 N3DS | 80% |
| 视频初始化中 O3DS | 70% |
| 初始化完成后（无 core 2） | 10% |

### 关键位置

| 位置 | 说明 |
|---|---|
| `video_player.c:2769~2782` | N3DS/O3DS 线程创建分支 |
| `source/vid_settings.c:68~84` | fake_pthread 核心分配（含 core 3） |
| `source/system/util/fake_pthread.c:44` | 默认核心列表 `{0, 1, -3, -3}` |

### Y2R 全链路（convert_thread 内，软解+Y2R+SBS 路径）

**路径分叉逻辑（`vid_worker.c` convert_thread，2026-04-02 后）：**
```
if(is_sbs_3d && sbs_right_buf):
    if(Y2R_X2):  → SBS 直写路径
    else(NEONy2r): → SBS 30次循环路径
else (2D):
    if(Y2R_X2):  → 2D 直写路径
    else(NEONy2r): → 2D 30次循环路径
```

**NEONy2r SBS（3D）：**
1. `Util_converter_y2r_yuv420p_to_rgba8888()` — 单次 Y2R 800×240 全帧，输出至 linearAlloc `video`
2. 30次 tile行循环：每次 2× `memcpy_asm`（左右各半行 12800B）拆到 `l_tex->data`/`r_tex->data`，**CPU memcpy ❌**
3. `C3D_TexFlush` ×2 + 更新 subtex + `Vid_large_texture_crop`

**NEONy2r 2D（非 SBS）：**
1. `Util_converter_y2r_yuv420p_to_rgba8888()` — 单次 Y2R 全帧，输出至 `video`
2. `Vid_large_texture_set_data(..., true)` — 内部 30次 tile行 memcpy，**CPU memcpy ❌**

**Y2R_X2 SBS（3D）— ✅ 零拷贝（2026-04-02 优化）：**
1. `Util_converter_y2r_yuv420p_sbs_to_rgba8888(yuv, l_tex->data, r_tex->data, tex_width, width, height)` — 两次 Y2RU，`transfer_gap=(tex_width−half_w)×4×8` 直写至纹理内存，**HW DMA ✅，CPU 0字节**
2. `C3D_TexFlush` ×2 + 直接更新 subtex 字段（无 `Vid_large_texture_set_data` / `Vid_large_texture_crop`）

**Y2R_X2 2D（非 SBS）— ✅ 零拷贝（2026-04-02 新增）：**
1. `Util_converter_y2r_yuv420p_to_rgba8888_direct(yuv, tex->data, tex_width, width, height)` — 单次 Y2R，`transfer_gap=(tex_width−width)×4×8` 直写至纹理内存，**HW DMA ✅，CPU 0字节**
2. `C3D_TexFlush` + 直接更新 subtex 字段 + `Vid_large_texture_crop`（供显示分辨率裁剪）

**关键 API（converter.c）：**
- `Util_converter_y2r_yuv420p_sbs_to_rgba8888(yuv, dst_left, dst_right, tex_width, full_width, height)` — SBS 直写，签名已改（旧：`**left_rgba, **right_rgba`；新：`*dst_left, *dst_right, tex_width`；无内部 alloc/free）
- `Util_converter_y2r_yuv420p_to_rgba8888_direct(yuv, dst, tex_width, width, height)` — 2D 直写新增函数
- `Util_converter_y2r_yuv420p_to_rgba8888(yuv, **rgba8888, width, height, tex_fmt)` — 原函数，仍供 NEONy2r 路径使用

**Y2R_X2 内存碎片消除**：旧版 `sbs_to_rgba8888` 每帧 `free + linearAlloc` 两次（48次/秒），新版零 alloc/free，碎片归零。

**DMA 上传状态**：`DEF_DRAW_DMA_ENABLE` 禁用（libctru stride bug）；Y2R_X2 路径通过 Y2RU `transfer_gap` 绕开 DMA，实现零 CPU 拷贝；NEONy2r 路径仍有 CPU memcpy。

全链路核心：**N系 core 0 / O系 core 1**（convert_thread 所在核）。

### FFmpeg 软解线程模式（2026-04-03）

**模式选择逻辑（decoder_video_soft.c，FRAME 优先）：**
- `AV_CODEC_CAP_FRAME_THREADS` → `FF_THREAD_FRAME`：workers 各自独立解码完整帧，pipeline 并行
- else `AV_CODEC_CAP_SLICE_THREADS` → `FF_THREAD_SLICE`：单帧内 slice/CTU行 并行
- else → `thread_type=0`，`thread_count=1`（单线程）

**各编码格式实际模式：**

| 编码 | 模式 | 原因 |
|---|---|---|
| H.264 | FRAME | 有 CAP_FRAME_THREADS；1-slice/frame 使 SLICE 只跑 C0 |
| H.265/HEVC | SLICE | 无 CAP_FRAME_THREADS，只有 CAP_SLICE_THREADS；CTU 行级，10+ jobs/frame |
| MPEG-2 | SLICE | 同 H.265；宏块行级，每帧约 15~30 jobs |
| AV1（dav1d） | 单线程（FFmpeg 层） | 两者皆无；dav1d 自管线程池，frame/slice 由 dav1d 决定 |

**关键代码位置：**
- `source/system/util/decoder_video_soft.c`：FRAME/SLICE 选择逻辑 + `need_more_input` 标签
- `source/system/util/fake_pthread.c`：`Util_fake_pthread_set_enabled_core(cores, start_core)`
- `source/vid_settings.c`：N3DS/O3DS 分支分别调 `Util_decoder_video_set_enabled_cores(frame, slice, start_core)`

**receive=EAGAIN 修复（frame threading pipeline）：**
send=0（包已接受）且 receive=AVERROR(EAGAIN)（流水线填充中）→ 旧代码报错；新代码跳 `need_more_input`
→ 消费包，返回 `DEF_ERR_NEED_MORE_INPUT`，调用方送下一包继续填充。

### N系 / O系 分类机制与调度

- **O3DS 当前分配（2026-04-02 后）**：C0 = 全部主线程（decode_thread REALTIME + decode_video_thread NORMAL + convert_thread HIGH + read_packet_thread REALTIME + audio_decode_thread HIGH）；C1 = FFmpeg worker 线程（优先从 C1 开始轮询）
- **2026-04-03 改动**：所有主线程统一迁至 C0，不再按核心分组；decode_video_thread NORMAL 在 C0 被 HIGH 线程抢占时自动让出给 worker；worker NORMAL 不与 HIGH 主线程竞争。

---

## 性能优化记录

### __wrap_malloc 堆探测缓存（已实装）

- **问题**：`source/system/util/util.c` 的 `__wrap_malloc` 每次调用都执行 `Util_is_heap_low()`，该函数做 `__real_malloc(2500000)` + 立即 `free` 来探测堆余量。播放期间解码器频繁 malloc → ~3000次/秒无效 2.5MB 分配+释放。
- **修复**：缓存探测结果，每 100ms 重新探测一次（`svcGetSystemTick` 计时），其余时间返回缓存值。
- **效果**：探测频率从 ~3000次/秒 → ~10次/秒。
- **位置**：`source/system/util/util.c`，`__wrap_malloc` / `Util_is_heap_low()`

### SwsContext / SwrContext 每帧重建修复（已实装）

- **问题**：`converter.c` 的 `Util_converter_convert_color()` 每帧调用 `sws_getContext()`，内含格式协商、滤波链构建、内存分配，30fps = 每秒 30 次创建。
- **修复**：在流初始化时创建一次 context，缓存于 converter 状态，每帧只调 `sws_scale()`；销毁时 `sws_freeContext()`。
- **位置**：`source/system/util/converter.c`

---

## 颜色常量（draw_types.h）

```c
DEF_DRAW_RED, GREEN, BLUE, BLACK, WHITE, AQUA, YELLOW
DEF_DRAW_WEAK_RED/GREEN/BLUE/BLACK/WHITE/AQUA/YELLOW
DEF_DRAW_NO_COLOR
// DEF_DRAW_CYAN 不存在！用 DEF_DRAW_AQUA 代替
```
| 低 | GPU做SBS分割 | 很高 |

---

## 可删 / 可拆清单

### ~~立刻可删（死代码 / 废弃功能）~~ ✅ 已完成（2026-03-30）

| 位置 | 内容 | 状态 |
|---|---|---|
| `menu.c` `Menu_send_app_info_thread` / `Menu_update_thread`（宏、声明、变量、threadCreate/Join、函数体，共~130行） | 遥测上报线程 | ✅ 已删 |
| `video_player.c` 语言 hack 两处（"de" scale 分支 + "ro"/"de" 字号分支） | `strcmp(config.lang,…)` 折叠为英文分支 | ✅ 已删 |
| `romfs/gfx/msg/` 非 `_en` 文件 18 个 | menu_de/es/hu/it/jp/pl/ro/ryu/zh-cn + vid 同组 | ✅ 已删 |

编译：**0 error 0 warning**（2026-03-30）

### ~~Controls 按钮 + 移动/缩放视频功能~~ ✅ 已删除（2026-03-30）

| 位置 | 内容 | 状态 |
|---|---|---|
| `video_player.c` / `vid_hid.c` | 下屏 Controls 按钮及其打开的控制说明菜单（`is_displaying_controls`、`VID_CMD_OPEN_CONTROLS/CONTROL_CLOSE`、control.t3x 纹理加载/释放） | ✅ 已删 |
| `video_player.c` / `vid_hid.c` | C-Stick 移动视频代码（`move_content_mode`、`Vid_hid_compute_move_deltas`、move content 主块） | ✅ 已删 |
| `video_player.c` / `vid_hid.c` | L/R 键缩放视频代码（`VID_HID_SHRINK/ENLARGE_CONTENT_CFM`，size_changes 计算与应用） | ✅ 已删 |
| `vid_hid_macros.h` | `VID_HID_CONTROL_*/MOVE_CONTENT_*/SHRINK/ENLARGE_CONTENT_CFM` 宏 | ✅ 已删 |
| `vid_cmd.h` | `VID_CMD_CONTROL_CLOSE`、`VID_CMD_OPEN_CONTROLS`、`VID_CMD_SE0_MOVE_MODE` | ✅ 已删 |
| `vid_hid.h` / `vid_state.h` | `control_button`、`move_content_button`、`is_displaying_controls`、`control_texture_handle`、`control[2]` 字段；`VidHidRouterState` 中 `is_displaying_controls`、`move_content_mode` | ✅ 已删 |
| `vid_state.h` / `vid_settings.c` | `Vid_move` 枚举与 `move_content_mode` 字段已删；存档 **`SETTINGS_ELEMENTS_V7`**（16 字段）：去掉原 `<10>` move 项，`<10>`～`<15>` 为 remember/playback/音视频开关/restart/threads；读档优先 V7，旧 V6 仍可读（忽略原 `[10]` move） | ✅ 已做 |
| `vid_state.h` / `romfs/gfx/msg/vid_en.txt` | `Vid_msg` 去掉 Controls/控制说明/Move 模式相关枚举项；`vid_en.txt` 重排为 **0～25** 共 26 条，与 `MSG_MAX` 一致 | ✅ 已做 |

**仍保留（渲染用）**：`video_x/y_offset`、`video_zoom` 仍在 `vid_player` 中并由 `Vid_fit_to_screen` 等使用；用户输入已不再修改其「平移/缩放游戏式」数值（仅几何/fit 逻辑会写）。

**新实例可选核对**：`make` 已通过；若不放心可在仓库根执行 `rg "MSG_CONTROLS|MSG_MOVE_MODE|move_content_mode|Vid_move" --glob "*.c" --glob "*.h"` 应无匹配（`mbedtls` 等第三方目录除外）。

编译：**0 error 0 warning**（2026-03-30）

### vid_worker.c 已完成内容（2026-04-02）

从 `video_player.c` 移至 `source/vid_worker.c`，头文件 `include/vid_worker.h`：

| 函数 | 说明 |
|---|---|
| `Vid_effective_use_linear_texture_filter` | 纹理滤波有效性判断（供vid_texture.c和vid_decode_thread用） |
| `Vid_update_decoding_statistics`（static） | 帧解码时间统计（仅内部调用，保持static） |
| `Vid_update_decoding_statistics_every_100ms` | 100ms统计刷新 |
| `Vid_get_media_duration` / `Vid_get_current_media_pos` | 媒体时长/当前位置计算 |
| `Vid_has_video` | 是否有视频轨判断 |
| `Vid_log_media_info` | 媒体信息日志 |
| `frame_worker_thread_start/end` | 帧解码计时回调（弱函数实现） |
| `dav1d_worker_task_start/end` | dav1d任务计时回调（弱函数实现） |
| `Vid_decode_video_thread` | 视频解码线程 |
| `Vid_convert_thread` | YUV→RGB色彩转换+纹理上传线程 |

**vid_worker.c includes（顺序）**：`vid_worker.h`、`extern memcpy_asm`、`vid_texture.h`、`vid_sync.h`、`video_player.h`、`<inttypes.h>`、`<stdlib.h>`、`<string.h>`、`system/util/converter.h`、`decoder.h`、**`err.h`**（必须显式加）、`log.h`、`speaker.h`、`str.h`、`util.h`

**陷阱：`DEF_ERR_*`/`DEF_SUCCESS` 必须显式 `#include "system/util/err.h"`** — 不经 vid_state.h 传递，拆出新 .c 文件时必加。`vid_seek_engine.c` 同此陷阱。`VID_SEEKBAR_*` 常量必须用 `double` 字面量（`5.0` 非 `5.0f`），否则 `-Wdouble-promotion`。

### vid_input.c 已完成内容（2026-04-02）

从 `video_player.c` 移至 `source/vid_input.c`，无新头文件（`Vid_hid` 声明已在 `video_player.h`）：

| 内容 | 说明 |
|---|---|
| `HID_*_SEL`/`HID_*_DESEL` 宏（~107个） | 直接引用 `vid_player` 的旧式宏，仅在 Vid_hid 内部用，保持为本地 #define |
| `vid_fill_layout` / `vid_fill_router_state` / `vid_fill_locks` | static helper，将 vid_player 字段打包成 VidHidLayout/RouterState/UiLocks 快照 |
| `Vid_process_hid_cmd_queue` | 命令执行器：从队列 pop → switch-case 写 vid_player 或发 decode_thread 请求 |
| `Vid_hid` | 顶层入口：home menu 处理 + SEL/DESEL 选中反馈 + enqueue + process + 滚动计算 |

**vid_input.c includes**：`video_player.h`、`vid_state.h`、`vid_screen.h`、`vid_texture.h`、`vid_worker.h`、`vid_cmd.h`、`vid_hid.h`、`<3ds.h>`、`<stdlib.h>`（`abs`）、`system/sem.h`、`util/err.h`、`util/expl.h`、`util/hid.h`、`util/log.h`、`util/util.h`

**跨文件依赖**：`Vid_expl_callback`/`Vid_expl_cancel_callback` 仍定义在 `video_player.c`，去掉 static，vid_input.c 用 `extern` 引用。Makefile 自动 glob，无需手动添加。

**陷阱**：`abs` 需显式 `#include <stdlib.h>`，不经 vid_state.h 链传递（编译时报 implicit declaration）。

### vid_decode.c 已完成内容（2026-04-02）

从 `video_player.c` 移至 `source/vid_decode.c`，头文件 `include/vid_decode.h`：

| 函数 | 说明 |
|---|---|
| `Vid_decode_thread` | 主解码调度线程：文件打开、音频/视频初始化、seek、abort、**音频包路由→audio_decode_thread**、视频帧分发（2026-04-02：音频软解已移出） |
| `Vid_audio_decode_thread` | 音频解码线程（2026-04-02新增）：接收 DECODE_REQUEST → PCM格式转换 → 音量调整 → ndsp speaker；Seek时排空队列；Abort时排空+通知 |
| `Vid_read_packet_thread` | FFmpeg 读包线程（file→packet queue） |
| `Vid_expl_callback` | 文件浏览器选中回调：malloc Vid_file消息 → enqueue DECODE_THREAD_PLAY_REQUEST |
| `Vid_expl_cancel_callback` | 文件浏览器取消回调（空实现） |

**新增头文件**：
- `include/vid_decode.h`：声明上述 4 个函数（含 `#include "system/util/str.h"` 给 Str_data 参数）
- `include/vid_lifecycle.h`：声明 `Vid_init_debug_view_data` / `Vid_init_media_data` / `Vid_init_video_data` / `Vid_init_audio_data`（这 4 个函数从 static 改为全局，供 vid_decode.c 调用；将来 vid_lifecycle.c 拆出时直接移定义）

**跨文件依赖处理**：
- `Vid_init_*` 4 个函数：去掉 video_player.c 里的 `static`，声明移到 `vid_lifecycle.h`，`video_player.c` 和 `vid_decode.c` 均 include 此头文件
- `HW_DECODER_RAW_IMAGE_SIZE` / `SW_DECODER_RAW_IMAGE_SIZE` 宏：从 `video_player.c` 移除（仅 vid_decode.c 用，定义在 vid_decode.c 顶部）
- `vid_input.c` 的手工 `extern` 声明改为 `#include "vid_decode.h"`

**vid_decode.c includes**：`video_player.h`、`extern memcpy_asm`、`vid_state.h`、`vid_texture.h`、`vid_settings.h`、`vid_sync.h`、`vid_screen.h`、`vid_worker.h`、`vid_lifecycle.h`、`vid_decode.h`、`<string.h>`、`<stdlib.h>`、`<malloc.h>`、`<time.h>`（`srand(time(NULL))`）、`system/menu.h`、`sem.h`、`converter.h`、`err.h`、`expl.h`、`file.h`、`hid.h`、`log.h`、`speaker.h`、`watch.h`

**陷阱**：`time()` 需显式 `#include <time.h>`；`vid_decode.c` 不需要 `<libavutil/cpu.h>`（avcodec 调用封装在 Util_decoder_* 内）。

### vid_draw.c 已完成内容（2026-04-02）

从 `video_player.c` 移至 `source/vid_draw.c`，头文件 `include/vid_draw.h`：

| 函数 | 说明 |
|---|---|
| `Vid_main` | 主渲染循环（~1114行）：帧时序、缓冲管理、VPS统计、3D/SBS渲染、底屏UI（设置菜单/调试图/seek条/音轨选择） |
| `Vid_draw_init_exit_message` | init/exit状态屏渲染（非static，供Vid_init/Vid_exit跨文件调用） |

**vid_draw.c includes**：`video_player.h`、`vid_state.h`、`vid_texture.h`、`vid_screen.h`、`vid_sync.h`、`vid_worker.h`、`vid_draw.h`、`<inttypes.h>`、`<math.h>`、**`<malloc.h>`**（mallinfo）、`<libavutil/cpu.h>`、`system/draw/draw.h`、`system/menu.h`、**`system/sem.h`**（Sem_config/Sem_state/DEF_SEM_MODEL_*）、`system/util/converter.h`、`cpu_usage.h`、`err.h`、`expl.h`、`hw_config.h`、`log.h`、`speaker.h`、`watch.h`

**陷阱**：`Sem_config`/`Sem_state` 类型和 `Sem_get_config`/`Sem_get_state` 函数来自 `system/sem.h`，拆出新文件时必须显式 include；`mallinfo`/`struct mallinfo` 来自 `<malloc.h>`。

### vid_lifecycle.c 已完成内容（2026-04-02，Step 8）

从 `video_player.c` 移至 `source/vid_lifecycle.c`（`video_player.c` 同时删除）：

| 内容 | 说明 |
|---|---|
| `vid_player` / `vid_msg` 全局变量定义 | 所有模块通过 `video_player.h` / `vid_state.h` extern 访问 |
| `Vid_query_init_flag` / `Vid_query_running_flag` / `Vid_get/set_use_hw_color_conversion` | 状态查询与 HW色彩转换模式读写 |
| `Vid_resume` / `Vid_suspend` / `Vid_load_msg` / `Vid_init` / `Vid_exit` | 生命周期公开 API |
| `Vid_init_variable` + `Vid_init_debug_view_mode` + `Vid_init_player_data` + `Vid_init_ui_data`（static） | 内部初始化辅助 |
| `Vid_init_debug_view_data` / `Vid_init_media_data` / `Vid_init_video_data` / `Vid_init_audio_data` | 跨文件可见（`vid_lifecycle.h` 声明，供 `vid_decode.c` 调用） |
| `Vid_init_thread` / `Vid_exit_thread` | 初始化/退出线程（全局，供 debug 符号访问） |

**includes**：与原 `video_player.c` 完全一致，无新增。

### 低优先级清理

| 位置 | 内容 | 说明 |
|---|---|---|
| `converter.c` | `yuv420p_to_rgb565le` / `yuv420p_to_rgb888le` / `rgb888_rotate_90` 系列（~4函数 + 对应 2 个 `.s` ASM文件） | 当前 `video_player.c` 不调用，属未使用路径 |
| `source/system/util/cpu_usage.c` + `sem.c` MENU_ADVANCED | sem菜单 Advanced 里的 CPU 占用率图形显示（`Util_cpu_usage_draw()`、`Util_cpu_usage_query_show_flag()`、`Util_cpu_usage_init()`、相关按钮/变量）| 用户已决定删除；`video_player.c` 侧的调用已于 2026-03-30 删除（布局重整时一并移除），`sem.c` 侧 Advanced 菜单 UI 待清理 |

---

## 已知陷阱

1. **mvdstdGenerateDefaultConfig 会覆盖 output_type** — 调用后必须显式设 `output_type = MVD_OUTPUT_ABGR32`（decoder.c:1903）
9. **TEST BUILD 硬编码已删除** — 原 video_player.c:3100 有 `use_hw_color_conversion = false` 强制覆盖，导致sem菜单的Y2R按钮完全无效。已删除。
10. **DEF_DRAW_DMA_ENABLE 已禁用（2026-03-30）** — `Draw_set_texture_data_direct` 的DMA路径有stride bug：DMA把目标纹理的stride（tex_width×32）错误地应用在源数据上（应用DST_CONFIG但API不支持，退而用SRC_CONFIG），导致读取源数据时跨度过大，Y2R上传时x轴右侧出现随机乱块。禁用DMA后走手动tile行memcpy，正确处理pic_width ≠ tex_width的stride差。位置：`include/system/draw/draw_types.h:9`。MVD路径走CPU Morton不经过此函数，不受影响。
11. ~~**Y2R SBS stride split**~~ — ✅ 已验证（2026-04-02）：gap参数输出侧生效，硬件行为正确，Y2R_X2 SBS帧率提升显著无图像错乱。API已改直写（dst_left/dst_right/tex_width），旧风险消除。位置：converter.c Util_converter_y2r_yuv420p_sbs_to_rgba8888，vid_worker.c Y2R_X2 SBS块。
2. **GPU_RGB8（3字节）不可用** — PICA200上产生3重影花屏，用GPU_RGBA8
3. ~~**Y2R路径格式不匹配**~~ — 已确认无bug：Y2R实际调用 `yuv420p_to_rgba8888`（OUTPUT_RGB_32），输出RGBA8888+BLOCK_8_BY_8预瓦片化，与GPU_RGBA8完全匹配，跳过CPU Morton，显示正常
4. **DEF_DRAW_CYAN 不存在** — 用 DEF_DRAW_AQUA 替代
5. **GX_DisplayTransfer PPF事件不可靠** — 系统共享，无法区分是谁的DMA（已回退，勿重试）
6. **换目录后必须 make clean** — .d文件含绝对路径，旧路径导致构建失败
7. **Util_load_msg / Util_parse_file 严格** — 任意一个索引缺失整个消息数组全部加载失败→菜单全空白。sem_en.txt 必须覆盖 0~MSG_MAX-1 所有索引

14. **decode_thread 通知队列不可 blocking 等待（2026-04-02发现）** — `decode_thread_notification_queue` 在主循环末尾以 timeout=0 非阻塞轮询（`queue.c` 证实：`next_index==0 && wait_us>0` 才等待，wait_us=0 立即返回 `DEF_ERR_TRY_AGAIN`）。若在命令 handler 里 `while(true)` 等待该队列上的特定通知，会消费掉其他线程（read_packet_thread 等）发来的通知，破坏 seek/abort 状态机。教训：Seek 只 fire-and-forget 发请求；只有 Abort（此时所有其他线程已停止）才可 blocking 等待。

12. **FFmpeg重编必须加 `--disable-neon`（2026-03-30）** — 去掉`--disable-asm`后 GNU as 能汇编 NEON → configure 将 HAVE_NEON 设为 1 → arm-none-eabi 非Linux分支 `ff_get_cpu_flags_arm()` 直接返回 `AV_CPU_FLAG_NEON`（用 HAVE_NEON，非 HAVE_NEON_EXTERNAL）→ blockdsp_init_neon 指向 ff_clear_blocks_neon → ARM11 执行 NEON → **崩溃**（PC=mpeg_decode_slice，LR=ff_clear_blocks_neon）。修复：加 `--disable-neon`。构建脚本：`D:\\3dsvideoplayermock\\Topos0.01\\build_ffmpeg_asm.sh`（已更新）。
13. **替换 library/lib/*.a 后必须 make clean** — devkitPro Makefile 不跟踪 `.a` 依赖，直接 make 不重链，新 ASM 代码不生效。换库后必须 `make clean && make`。
8. **Util_cpu_usage_set_show_flag 有 init 保护** — 未调用 `Util_cpu_usage_init()` 时点ON无效，函数直接return

---

## sem菜单精简（2026-03-29 持续）

### Control 子菜单删除
- **整个 MENU_CONTROL 子菜单已删除**：`MENU_CONTROL` 枚举删除，MENU_ADVANCED 变为索引1，MENU_MAX=2
- **scroll_speed 写死为 0.5**：默认值、文件加载均硬编码，不再从文件读取，不再 watch
- **删除**：`HID_CTRL_SCROLL_SPEED_BAR_*` 宏、`sem_scroll_speed_slider/bar` 变量、绘制、HID SEL/CFM/DESEL、watch_add/remove
- **顶部菜单**：原三项（LCD/Control/Advanced）改为两项（LCD/Advanced），Advanced y 位置从 50 → 25

### LCD 子菜单删除 time_to_turn_off_lcd / time_to_enter_sleep 滑块
- **两个值永久写死为 0（off）**：默认值硬编码，文件加载也写死，不读文件值
- **删除**：`HID_LCD_LCD_OFF_BAR_*` / `HID_LCD_SLEEP_BAR_*` 宏；`sem_screen_off_time_slider/bar` / `sem_sleep_time_slider/bar` 变量；绘制块；HID SEL/CFM/DESEL；watch_add/remove；校验逻辑
- **HID_SCROLL_MODE_DESEL**：删除 `sem_screen_off_time_bar.selected` / `sem_sleep_time_bar.selected` 条件
- **hw_config 线程**：判断 `> 0` 才触发关屏/sleep，值为 0 则条件永不成立，逻辑正确

---

## 耳机防睡眠逻辑删除（2026-03-29）

- **删除**：`Vid_update_sleep_policy()` 函数及声明（`source/video_player.c:~2579`）
- **原调用处**（`video_player.c:~2352`）改为始终 `aptSetSleepAllowed(true)`：插耳机播放时不再阻止合盖睡眠
- **结果**：合盖行为完全由 sem 的 sleep timer 控制（当前 timer=0 即永不自动睡眠），系统本身合盖仍可正常睡眠

---

## 颜色转换模式三选一（计划，未实现）

**目标**：sem菜单 Advanced 里 `HW color conv` 从现在的「CPU / Y2R」两按钮改为三按钮：

| 按钮 | 行为 |
|---|---|
| `CPU`（强制） | 永远走路径C（swscale），忽略YUV420P和尺寸 |
| `Y2R`（强制） | 永远尝试Y2R，失败则报错（不fallback） |
| `auto`（默认） | 优先Y2R（YUV420P且宽高均为8整数倍），否则自动fallback到CPU |

**auto模式fallback条件**：`width % 8 != 0 || height % 8 != 0` → 走CPU，避免tile行切割切错。

**实现要点**：
- `vid_player.use_hw_color_conversion` 从 `bool` 改为 3值枚举（`HW_CONV_CPU` / `HW_CONV_Y2R` / `HW_CONV_AUTO`）
- Y2R初始化条件按枚举分支处理
- sem菜单增加第三个按钮，消息文件新增对应idx
- SBS Y2R分支的8整除guard：auto模式下不满足则is_y2r=false

---

## sem菜单 Advanced 相关（2026-03-29）

- **位置**：sem菜单 → Advanced → 底部（需下滑）
- **功能**：`HW color conv` 标签 + `CPU`/`y2r*2`/`NEONy2r` 三按钮，切换 `vid_player.use_hw_color_conversion`（uint8_t，值0/1/2）
- **接口**：sem.c 通过 `Vid_get_use_hw_color_conversion()` / `Vid_set_use_hw_color_conversion(uint8_t)` 访问；常量 `VID_HW_CONV_CPU/Y2R_X2/NEON_Y2R` 在 `include/video_player.h`
- **三个模式**：`CPU(0)`=swscale；`y2r*2(1)`=双次Y2R stride-split（SBS直接出左右眼，当前默认）；`NEONy2r(2)`=单次Y2R全帧→memcpy_asm tile行切割分左右眼
- **Advanced菜单新增滚动**：`MENU_ADVANCED_Y_OFFSET_MIN = -50`（sem.c:31）
- **sem_en.txt** idx 73/74/75/76，MSG_MAX = 77（76=NEONy2r）
- **滚动修复**：Advanced子菜单所有选项绘制 y 坐标加 `sem_y_offset`（之前只有 back 键在动，选项静止）
- **back键固定**：back 按键绘制改为固定 y=0，不随 `sem_y_offset` 移动
- **CPU占用显示移到下屏**：`Util_cpu_usage_draw()` 从上屏（TOP_LEFT/TOP_RIGHT）移除，改在下屏 section 调用（video_player.c 诊断文字之后）；`Vid_draw_init_exit_message` 无下屏 section，同步移除

## 打开新视频不触发全屏切换（2026-03-29 修订）

- **现状**：打开有视频轨的文件时，**不再**自动进入全屏模式。打开前是什么模式（UI / 全屏），打开后保持不变。
- **删除**：`video_player.c:~3771` 原有的 `vid_player.auto_full_screen_count = 0` 和 `Vid_enter_full_screen(0)` 两行。
- **保留**：`Vid_fit_to_screen(FULL_SCREEN_WIDTH, FULL_SCREEN_HEIGHT, i)` 和 `Util_hid_reset_key_state` 不动。
- **位置**：`video_player.c` — 解码就绪后「Fit video to screen if file has video tracks」分支（原注释已同步更新）。

## Night Mode 固定（2026-03-29）

- **`config.is_night` 永远为 `true`**，系统固定 night 模式，light 模式已删除。
- **删除**：night on/off 按钮、flash 按钮的 HID宏/变量/watch/init/draw/handler 全部从 `sem.c` 移除。
- **删除**：flash 定时器（50ms 切换 `is_night` 的循环）及 `previous_flash_ts` 变量、`UPDATE_FLASH_INTERVAL_MS` 宏。
- **删除**：`config.is_flash` 字段相关的默认值、watch add/remove。
- **保留**：`is_night` 的文件保存行（`<6>`）；亮度/熄屏时间等 LCD 菜单控件不变。
- **位置**：`source/system/sem.c`

---

## UI 精简（2026-03-29）

- **上屏永久最大化（相对原版）**：目标为 **UI 模式开启时上屏纯视频** — 上屏 **不出现** 顶条/系统式 OSD，信息与控制 **集中在下屏**；避免上屏视频因叠加 UI 而 **被缩放或让位**。与「与上游（原版）差异」表中「上屏与 UI」一致。
- **删下屏渐隐逻辑**：`turn_off_bottom_screen_count` 字段及全部相关逻辑已删除。`Vid_enter_full_screen` 改为无参，全屏时底屏不再倒计时降亮/关LCD。OLD2DS 全屏时填黑逻辑保留（去掉了对已删字段的判断）。
- **删上屏顶条 UI**：`Draw_top_ui(...)` 调用全部删除（menu.c 初始化+主循环、sem.c 设置菜单）。时间/WiFi/省电绿叶不再显示。函数定义保留在 draw.c，无需改 draw.h。

---

## 下屏诊断布局重整（2026-03-30，再设计 2026-03-30）

- **当前状态**：监控文字分 5 组，总高约 135px（y=1~132），使用整个下屏中间区域，CPU 为视觉核心
  - STATUS  y=1   : `state  VER`（左绿右弱绿，scale 0.40）
  - MEDIA   y=13  : `A:codec  V:codec  WxH  @fps`（默认色，scale 0.40）
  - DECODE  y=25  : `Dec:MVD/HW  Tex:LINEAR  SAR:w/h  z:zoom`（绿，scale 0.40）
  - CPU     y=40  : `CPU  XMHz`（黄，scale 0.44）+ 4 条 bar 图（C0~C3，y=53/67/81/95，每条 265×7px，黄色填充/弱白轨道）+ 百分比文字 x=286
  - MEMORY  y=111 : `Lin:XKB  Heap:free/total KB`（黄，scale 0.39，**每秒缓存**）
  - GEOMETRY y=123: `SBS:X  3D:X  img:WxH  xo:X  yo:X`（弱白，scale 0.39，次要信息）
- **CPU/内存所有值统一 1s 静态缓存**：`static diag_ts` 控制，每秒刷新一次，每帧零 mallinfo/cpu_usage 调用
- **Bar 图参数**：x=18, w=265（track），填充 w=(int)(265*pct/100)，h=7，颜色 track=WEAK_WHITE / fill=YELLOW
- **息屏零开销**：同 is_bottom_lcd_on guard（不变）
- **位置**：`video_player.c` line ~1376~1460

---

## 关键代码位置

| 位置 | 说明 |
|---|---|
| `include/video_player.h:13` | DEF_VID_VER 版本字符串 |
| `include/vid_screen.h` + `source/vid_screen.c` | 屏幕几何与全屏/亮度（见「解耦重构计划」vid_screen 行） |
| `include/vid_state.h` | 所有枚举/结构体/常量（2026-03-28 新建） |
| `vid_lifecycle.c:47~48` | vid_msg / vid_player 全局变量定义（2026-04-02 迁入，video_player.c 已删） |
| ~~`video_player.c:~2205`~~ | ~~下屏版本号显示~~ — 已并入 row 0（line ~1376），条目过时 |
| ~~`video_player.c:~2267`~~ | ~~下屏诊断显示~~ — 已并入 5 行布局（line ~1374），条目过时 |
| `video_player.c:~4153` | O3DS强制 use_hw_decoding=false |
| `video_player.c:~4821` | 800×240强制is_sbs_3d=true，SAR=1:1 |
| `video_player.c:~4906` | 纹理初始化：RAW_PIXEL_ABGR8888 |
| `source/vid_texture.c` | `Vid_texture_dimensions_prefer_nearest`：命中则强制 NEAREST（见「纹理滤波」） |
| `source/video_player.c` | `Vid_effective_use_linear_texture_filter`：用户线性 ∧ 非自动最近邻；两处 `Vid_large_texture_set_filter` |
| `video_player.c:~5466` | seek重开解码器（解决heap碎片） |
| `video_player.c:~6554` | SBS帧分割（CPU逐行memcpy） |
| `decoder.c:868` | mvdstdInit(..., MVD_OUTPUT_ABGR32, ...) |
| `decoder.c:1903` | GenerateDefaultConfig后强制覆盖output_type=ABGR32 |
| ~~`decoder.c:2576`~~ | ~~ZeroCopy实现起点~~ — ZeroCopy已回退，会增加内存碎片，当前版本无此优化 |
| `source/system/sem.c:18` | cpu_usage.h include（修编译，2026-03-29加） |
| `source/system/menu.c:~254` | Util_cpu_usage_init() 调用（2026-03-29加，缺失导致ON按钮无效） |
| `source/system/sem.c:MENU_ADVANCED` | HW color conv CPU/Y2R 按钮（2026-03-29加） |
| `source/system/sem.c:MENU_LCD` | Screen mode 3D/2D/auto 三按钮（2026-03-29加 auto），MSG_AUTO=69，按钮x=15/120/225 宽=80，auto缩放0.5 |
| `source/video_player.c:~3750` | AUTO模式 is_sbs_3d 判断：h==240 && w>=640 → SBS，否则2D |
| `include/system/draw/draw_types.h:9` | `DEF_DRAW_DMA_ENABLE` 已注释禁用，`Draw_set_texture_data_direct` 走memcpy路径（2026-03-30） |
| `video_player.c:~3250` | Y2R初始化条件：`!HW_DECODING && use_hw_color_conversion`（去除了`!is_sbs_3d`，2026-03-30） |
| `video_player.c:~4868` | SBS分割：Y2R走tile行切割（`is_y2r`分支），线性走像素行切割（保留原逻辑） |
| `video_player.c:~1376` | 底屏诊断区 5 组布局（2026-03-30 再设计）：STATUS y=1 / MEDIA y=13 / DECODE y=25 / CPU-bars y=40~107 / MEMORY y=111 / GEOMETRY y=123，总高~135px |
| `video_player.c:~1424` | CPU bar chart：header y=40（yellow scale 0.44），C0~C3 bar y=53/67/81/95，bar 265×7px，填充 YELLOW / 轨道 WEAK_WHITE，百分比 x=286，所有值 1s 静态缓存 |
| `romfs/gfx/msg/sem_en.txt idx 69` | "auto(y240x≧640)"（原"Auto"，2026-03-29改） |
| `include/video_player.h` | Vid_get/set_use_hw_color_conversion() 声明 |
| `source/video_player.c:~277` | Vid_get/set_use_hw_color_conversion() 实现 |
| `source/video_player.c` DECODE行 | `ASM:A6/A5/--` 指示器：`av_get_cpu_flags()` 检查 `AV_CPU_FLAG_ARMV6`，static cache，首次调用后不再查询；`#include <libavutil/cpu.h>` 已加入项目顶部 |
| `source/system/util/cpu_usage.c:152` | CPU占用绘制位置：下屏 x=220 y=90（box 100×60，ALIGN_RIGHT→box 覆盖 220~320 右侧，CENTER_Y→中线≈120），黄色。**坑：原为 x=360 超出 320px 底屏不可见，已修为 x=220** |
| `romfs/gfx/msg/sem_en.txt` | sem菜单消息文件，必须含0~75全部索引（MSG_MAX=76），非英文已删除 |
| `romfs/gfx/msg/vid_*.txt` | 播放器 `Vid_msg` 字符串：`MSG_BRIGHTNESS`=索引32，须与 `vid_state.h` 枚举顺序一致。曾误把「Only subtitle」等放在32~34、亮度在35，导致亮度提示前缀错字（2026-03-29 已按枚举重排32~37） |

---

## Eco 模式（2026-03-29 写死）

- **`config.is_eco` 永远为 `true`**：默认值（sem.c:281）本来就是 true；文件加载处（sem.c:384）也改为写死 `true`，不再从存档读取。
- **sem 里无 eco 切换按钮**（本 fork 已无此 UI）。
- **作用**：控制 GPU 渲染频率——eco=true 时只在 watch 变量变化或 `Draw_set_refresh_needed(true)` 时渲染；eco=false 则每帧强制渲染（耗电）。
- **位置**：`source/system/sem.c:281`（默认）、`sem.c:384`（文件加载，已改）

---

## 帧率限制 / 主循环节奏

**不需要也没有 svcSleepThread 帧率限制**，使用更精确的硬件同步：

| 情况 | 机制 | 位置 |
|---|---|---|
| 有东西要画 | `C3D_FrameBegin(C3D_FRAME_SYNCDRAW)` 阻塞至 VSync，天然锁 60fps | `draw.c:972` |
| eco=true 且无变化（跳过渲染） | `gspWaitForVBlank()` 主动睡到下一 VBlank | `video_player.c:2344/2407`、`sem.c:736` |

两条路径都不空转，不需要额外 svcSleepThread。

---

## 音频处理路径（2026-04-02 重构后）

**流水线：**
```
Vid_decode_thread (C0)
  → Util_decoder_ready_audio_packet / Util_decoder_audio_decode
  → malloc Vid_audio_decoded_data { audio*, audio_samples, pos, packet_index }
  → Util_queue_add(audio_decode_thread_command_queue, AUDIO_DECODE_THREAD_DECODE_REQUEST)

Vid_audio_decode_thread (C1)
  → Util_converter_convert_audio (PCM格式转换, 最多2ch, S16输出)
  → 音量缩放 (vid_player.volume != 100 时 int16 乘法, 溢出 clamp INT16_MAX)
  → Util_speaker_add_buffer (最多重试125次×2ms=250ms)
  → free converted / audio_data
```

**Seek/Abort 处理：**
- **Seek**：decode_thread 调 `Util_speaker_clear_buffer` + 发 `AUDIO_DECODE_THREAD_SEEK_REQUEST`（SEND_TO_FRONT，不等通知）→ audio_decode_thread 排空队列旧数据（free，不送 speaker）
- **Abort**：decode_thread 发 `AUDIO_DECODE_THREAD_ABORT_REQUEST`（SEND_TO_FRONT）→ **等** `AUDIO_DECODE_THREAD_FINISHED_ABORTING_NOTIFICATION` → 再 `Util_speaker_exit()` / `Util_decoder_close_file()`

**关键设计约束（陷阱）**：
- decode_thread 通知队列设计为**非阻塞轮询**（主循环末尾 timeout=0 poll），不可在命令 handler 里 blocking 等待通知队列——会吃掉其他线程的通知，破坏状态机
- Seek 只能 fire-and-forget；Abort 可以 blocking 等（因为其他线程已全部停止发通知）

**相关文件**：
- `source/vid_decode.c`：`Vid_decode_thread`（路由）、`Vid_audio_decode_thread`（PCM转换+speaker）
- `include/vid_state.h`：`Vid_audio_decoded_data`、`AUDIO_DECODE_THREAD_*` 枚举
- `include/vid_decode.h`：`Vid_audio_decode_thread` 声明
- `source/vid_lifecycle.c`：audio_decode_thread 在 `Vid_init_thread` 末尾创建（C1，HIGH），`Vid_exit_thread` 里 join+free

**DSP / decoder**：`source/system/util/decoder.c`（FFmpeg 软解 → PCM）、`source/system/util/speaker.c`（ndsp 输出）

---

## 线程管理 / 核心分配

### 系统级常驻线程

| 线程 | 核心 | 优先级 | 职责 |
|---|---|---|---|
| 主线程（aptMainLoop） | core 0 | — | 渲染 + HID + Vid_main |
| `menu_worker_thread` | core 0 | ABOVE_NORMAL | 子应用 init/exit 等耗时操作 |
| `menu_update_thread` | core 1 | REALTIME | 时钟/状态定时更新 |
| `sem_hw_config_thread` | core 1 | HIGH-1 | 亮度/LCD/睡眠硬件配置 |
| `hid_scan_thread` | core 0 | REALTIME | 按键扫描 |

### 播放器线程（播放期间）

**N3DS / O3DS 主线程（全部在 C0，2026-04-03 统一）：**

| 线程 | 核心 | 优先级 | 职责 |
|---|---|---|---|
| `Vid_decode_thread` | **core 0** | REALTIME | 包路由调度/状态机（几乎全在等队列，CPU 消耗极低） |
| `Vid_decode_video_thread` | **core 0** | NORMAL | 调用 FFmpeg send_packet/receive_frame，实际解码交给 worker |
| `Vid_convert_thread` | **core 0** | HIGH | 颜色转换（CPU Morton/Y2R） |
| `Vid_read_packet_thread` | **core 0** | REALTIME | 文件读包 → 队列 |
| `Vid_audio_decode_thread` | **core 0** | HIGH | PCM格式转换 + ndsp speaker输出 |

**FFmpeg worker 线程（fake_pthread，avcodec_open2 时创建，thread_count-1 个，2026-04-03）：**

| 机型 | 轮询顺序 | start_core | 优先级 |
|---|---|---|---|
| N3DS | C2 → C3 → C0 → C2 → … | 2 | NORMAL（2026-04-03改：HIGH→NORMAL） |
| O3DS | C1 → C0 → C1 → C0 → … | 1 | NORMAL |

**O3DS thread_count=3（2 workers）核心分布（2026-04-03 实测）：**
- C0：decode_video_thread（主 FFmpeg 调用）+ 1 worker = 2 个解码实体
- C1：1 worker
- 理论解码性能提升：**20%～50%**
- 实测（N3DS 以 O3DS 模式运行）：**MPEG-2 800×240 SBS 3D → 24fps 稳定**（默认 thread_count 计划设为 3）

### Core 3 使用情况

- **`threadCreate` 不用 core 3**：core 3 是半系统核，可用性不稳定，硬绑定会崩
- **fake_pthread（FFmpeg 内部多线程）用 core 3**：`vid_settings.c:70` 对所有核动态检测 `Util_is_core_available(i)`，可用则用，不可用则跳过，安全
- **结论**：core 3 已通过 fake_pthread 按需使用，无需改动

### APT CPU 时间限制（core 1 配额）

| 时机 | 值 |
|---|---|
| 启动默认 | 30%（menu.c:214） |
| 视频初始化中 N3DS | 80% |
| 视频初始化中 O3DS | 70% |
| 初始化完成后（无 core 2） | 10% |

### 关键位置

| 位置 | 说明 |
|---|---|
| `video_player.c:2769~2782` | N3DS/O3DS 线程创建分支 |
| `source/vid_settings.c:68~84` | fake_pthread 核心分配（含 core 3） |
| `source/system/util/fake_pthread.c:44` | 默认核心列表 `{0, 1, -3, -3}` |

### Y2R 全链路（convert_thread 内，软解+Y2R+SBS 路径）

**路径分叉逻辑（`vid_worker.c` convert_thread，2026-04-02 后）：**
```
if(is_sbs_3d && sbs_right_buf):
    if(Y2R_X2):  → SBS 直写路径
    else(NEONy2r): → SBS 30次循环路径
else (2D):
    if(Y2R_X2):  → 2D 直写路径
    else(NEONy2r): → 2D 30次循环路径
```

**NEONy2r SBS（3D）：**
1. `Util_converter_y2r_yuv420p_to_rgba8888()` — 单次 Y2R 800×240 全帧，输出至 linearAlloc `video`
2. 30次 tile行循环：每次 2× `memcpy_asm`（左右各半行 12800B）拆到 `l_tex->data`/`r_tex->data`，**CPU memcpy ❌**
3. `C3D_TexFlush` ×2 + 更新 subtex + `Vid_large_texture_crop`

**NEONy2r 2D（非 SBS）：**
1. `Util_converter_y2r_yuv420p_to_rgba8888()` — 单次 Y2R 全帧，输出至 `video`
2. `Vid_large_texture_set_data(..., true)` — 内部 30次 tile行 memcpy，**CPU memcpy ❌**

**Y2R_X2 SBS（3D）— ✅ 零拷贝（2026-04-02 优化）：**
1. `Util_converter_y2r_yuv420p_sbs_to_rgba8888(yuv, l_tex->data, r_tex->data, tex_width, width, height)` — 两次 Y2RU，`transfer_gap=(tex_width−half_w)×4×8` 直写至纹理内存，**HW DMA ✅，CPU 0字节**
2. `C3D_TexFlush` ×2 + 直接更新 subtex 字段（无 `Vid_large_texture_set_data` / `Vid_large_texture_crop`）

**Y2R_X2 2D（非 SBS）— ✅ 零拷贝（2026-04-02 新增）：**
1. `Util_converter_y2r_yuv420p_to_rgba8888_direct(yuv, tex->data, tex_width, width, height)` — 单次 Y2R，`transfer_gap=(tex_width−width)×4×8` 直写至纹理内存，**HW DMA ✅，CPU 0字节**
2. `C3D_TexFlush` + 直接更新 subtex 字段 + `Vid_large_texture_crop`（供显示分辨率裁剪）

**关键 API（converter.c）：**
- `Util_converter_y2r_yuv420p_sbs_to_rgba8888(yuv, dst_left, dst_right, tex_width, full_width, height)` — SBS 直写，签名已改（旧：`**left_rgba, **right_rgba`；新：`*dst_left, *dst_right, tex_width`；无内部 alloc/free）
- `Util_converter_y2r_yuv420p_to_rgba8888_direct(yuv, dst, tex_width, width, height)` — 2D 直写新增函数
- `Util_converter_y2r_yuv420p_to_rgba8888(yuv, **rgba8888, width, height, tex_fmt)` — 原函数，仍供 NEONy2r 路径使用

**Y2R_X2 内存碎片消除**：旧版 `sbs_to_rgba8888` 每帧 `free + linearAlloc` 两次（48次/秒），新版零 alloc/free，碎片归零。

**DMA 上传状态**：`DEF_DRAW_DMA_ENABLE` 禁用（libctru stride bug）；Y2R_X2 路径通过 Y2RU `transfer_gap` 绕开 DMA，实现零 CPU 拷贝；NEONy2r 路径仍有 CPU memcpy。

全链路核心：**N系 core 0 / O系 core 1**（convert_thread 所在核）。

### FFmpeg 软解线程模式（2026-04-03）

**模式选择逻辑（decoder_video_soft.c，FRAME 优先）：**
- `AV_CODEC_CAP_FRAME_THREADS` → `FF_THREAD_FRAME`：workers 各自独立解码完整帧，pipeline 并行
- else `AV_CODEC_CAP_SLICE_THREADS` → `FF_THREAD_SLICE`：单帧内 slice/CTU行 并行
- else → `thread_type=0`，`thread_count=1`（单线程）

**各编码格式实际模式：**

| 编码 | 模式 | 原因 |
|---|---|---|
| H.264 | FRAME | 有 CAP_FRAME_THREADS；1-slice/frame 使 SLICE 只跑 C0 |
| H.265/HEVC | SLICE | 无 CAP_FRAME_THREADS，只有 CAP_SLICE_THREADS；CTU 行级，10+ jobs/frame |
| MPEG-2 | SLICE | 同 H.265；宏块行级，每帧约 15~30 jobs |
| AV1（dav1d） | 单线程（FFmpeg 层） | 两者皆无；dav1d 自管线程池，frame/slice 由 dav1d 决定 |

**关键代码位置：**
- `source/system/util/decoder_video_soft.c`：FRAME/SLICE 选择逻辑 + `need_more_input` 标签
- `source/system/util/fake_pthread.c`：`Util_fake_pthread_set_enabled_core(cores, start_core)`
- `source/vid_settings.c`：N3DS/O3DS 分支分别调 `Util_decoder_video_set_enabled_cores(frame, slice, start_core)`

**receive=EAGAIN 修复（frame threading pipeline）：**
send=0（包已接受）且 receive=AVERROR(EAGAIN)（流水线填充中）→ 旧代码报错；新代码跳 `need_more_input`
→ 消费包，返回 `DEF_ERR_NEED_MORE_INPUT`，调用方送下一包继续填充。

### N系 / O系 分类机制与调度

- **O3DS 当前分配（2026-04-02 后）**：C0 = `decode_video_thread` 独占；C1 = `decode_thread`(HIGH) + `convert_thread`(HIGH) + `read_packet_thread`(HIGH) + `audio_decode_thread`(HIGH)
- **O3DS 改动动机**：旧版 `decode_thread` 在 C0 与 `decode_video_thread` 竞争时间片；`convert_thread` NORMAL 被其他三个 HIGH 线程抢占导致纹理上传延迟。

---

## 性能优化记录

### __wrap_malloc 堆探测缓存（已实装）

- **问题**：`source/system/util/util.c` 的 `__wrap_malloc` 每次调用都执行 `Util_is_heap_low()`，该函数做 `__real_malloc(2500000)` + 立即 `free` 来探测堆余量。播放期间解码器频繁 malloc → ~3000次/秒无效 2.5MB 分配+释放。
- **修复**：缓存探测结果，每 100ms 重新探测一次（`svcGetSystemTick` 计时），其余时间返回缓存值。
- **效果**：探测频率从 ~3000次/秒 → ~10次/秒。
- **位置**：`source/system/util/util.c`，`__wrap_malloc` / `Util_is_heap_low()`

### SwsContext / SwrContext 每帧重建修复（已实装）

- **问题**：`converter.c` 的 `Util_converter_convert_color()` 每帧调用 `sws_getContext()`，内含格式协商、滤波链构建、内存分配，30fps = 每秒 30 次创建。
- **修复**：在流初始化时创建一次 context，缓存于 converter 状态，每帧只调 `sws_scale()`；销毁时 `sws_freeContext()`。
- **位置**：`source/system/util/converter.c`

---

## 颜色常量（draw_types.h）

```c
DEF_DRAW_RED, GREEN, BLUE, BLACK, WHITE, AQUA, YELLOW
DEF_DRAW_WEAK_RED/GREEN/BLUE/BLACK/WHITE/AQUA/YELLOW
DEF_DRAW_NO_COLOR
// DEF_DRAW_CYAN 不存在！用 DEF_DRAW_AQUA 代替
```
