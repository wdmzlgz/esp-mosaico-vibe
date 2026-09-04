# ESP-Mosaico Game Platform

## 定位

Game Platform 为 ESP32-S31/ESP-Mosaico 提供 480×480 的 2D 游戏运行环境。游戏逻辑面向受控的 Raylib 兼容 API；设备侧使用 RGB565 framebuffer，ESP-GSP 负责 Canvas 展示，ESP-Iris 负责管理、镜像、输入与 Recovery-first 更新。

平台不把 PNG、WAV、Tiled JSON 或通用 OpenGL 解析器带入设备运行时。源资源在构建阶段转为可 mmap 的确定性二进制。

## 分层

| 层 | 位置 | 职责 |
|---|---|---|
| 游戏应用 | `projects/<game>/main` | 状态模型、玩法、渲染组合、输入语义 |
| Raylib API | `mosaico_raylib_fast` | 常用 Raylib 2D 调用到 RGB565 快速路径的映射 |
| 游戏运行时 | `mosaico_game`、`input`、`debug` | 生命周期、事件队列、帧统计、运行诊断 |
| 内容运行时 | `assets`、`2d`、`tilemap`、`audio` | mmap 资源、Atlas、地图、PCM/ADPCM 混音 |
| 显示适配 | `mosaico_raylib_port` | PSRAM framebuffer、多缓冲、GSP 非阻塞提交 |
| 构建工具 | `game_sdk/tools`、`game_sdk/cmake` | 资源编译、能力组件选择、GSP 编译器发现 |
| Host 工具 | `game_sdk/host` | 确定性模型回放、截图与浏览器预览 |
| 设备运维 | `mosaico.py`、ESP-Iris | 构建、Recovery、安装、监控、截图和远程输入 |

依赖方向必须保持自上而下。玩法模型不得依赖 FreeRTOS、BSP 或 ESP-Iris，使其能由 Host C 编译器直接测试。

## 项目结构

```text
projects/<game>/
├── CMakeLists.txt
├── sdkconfig.defaults
├── sdkconfig.application.defaults
├── partitions.csv
├── assets_src/              # PNG、TMJ、WAV 和生成脚本
├── assets/generated/        # .atlas、.map、.sound、稳定资源 ID
└── main/
    ├── main.c               # 硬件启动与 update/render 循环
    ├── <game>.c/.h          # 无 ESP-IDF 依赖的玩法模型
    ├── scene.json.in
    └── CMakeLists.txt
```

项目在顶层 CMake 中声明能力，不再逐项复制组件目录：

```cmake
include("${CMAKE_CURRENT_LIST_DIR}/../../game_sdk/cmake/mosaico_game_sdk.cmake")
mosaico_game_sdk_configure_gsp_compiler()
mosaico_game_sdk_add_components(RAYLIB AUDIO TILEMAP)
```

只请求实际使用的能力。`RAYLIB` 自动带入 core、assets、2d 与显示 port；`AUDIO`、`TILEMAP` 为可选层。

## 一帧的生命周期

1. 触摸任务或 ESP-Iris RPC 写入统一事件队列。
2. 游戏任务在固定时间步内消费输入并更新纯玩法状态。
3. `BeginDrawing()` 借用一个空闲 PSRAM framebuffer。
4. Raylib 兼容调用直接写 RGB565；Atlas 按需执行 binary-alpha/A8 混合。
5. `EndDrawing()` 非阻塞提交给 GSP；GSP 回调释放 framebuffer。
6. 屏幕镜像从最近一帧复制稳定快照，不直接占用 LCD/USB。

不要在 update/render 热路径动态分配对象、解析源资源或执行阻塞 I/O。

## 触摸输入边界

板载 CST9217/CST9220 系列硬件最多支持两个同时触点。新的
`78/esp_lcd_touch_cst92xx` 0.1.0 驱动可通过标准 `esp_lcd_touch` API 返回两个触点和
track ID，并要求 `CONFIG_ESP_LCD_TOUCH_MAX_POINTS>=2`。当前 BSP 仍引用旧的
`waveshare/esp_lcd_touch_cst9217`，现有游戏也只读取一个点，因此驱动、BSP 和游戏输入层需要一起迁移。

平台事件层应保留两个触点的 ID、坐标和 down/move/up 生命周期。Raylib mouse 状态只是主触点兼容视图，不能作为多点输入的内部数据模型。这样才能可靠支持“左手移动、右手跳跃”、双指手势和触点交叉而不跳号。

## 资源和音频

构建工具将源文件转换为：

- `.atlas`：RGB565 加可选 A8，使用 FNV-1a 稳定 frame ID；
- `.map`：有限正交 Tiled 地图、对象与路径；
- `.sound`：24 kHz mono，短音效 PCM16，长音频 IMA-ADPCM；
- `game_assets.bin`：ESP-Iris system-update 写入的 mmap 分区镜像。

资源总量当前不得超过 1 MiB。项目应保留资源生成脚本与源文件，生成结果必须可重复。

## 启动与恢复约束

正常应用必须在显示初始化前启动管理面：

1. 初始化 NVS；
2. 注册 system inventory；
3. 调用 `iris_ota_support_start()`；
4. 初始化电源、显示、资源和游戏；
5. 首帧成功提交后调用 `esp_iris_mark_healthy()`。

正常固件保持 `CONFIG_ESP_IRIS_OTA_DEFAULT_VIA_RECOVERY=y`，不包含 OTA writer。空白或未验证设备首次安装先执行 `python mosaico.py recover`，应用只通过 `python mosaico.py install --project ...` 安装。

## 参考项目

- `raylib_shooter`：最小 Raylib 快速绘制与固定对象池；
- `tower_defense`：Atlas、Tiled、音频、Host replay 的完整资源化项目；
- `sky_hop`：横版物理、卷轴相机、动画与原创资源生成流水线。

后续平台能力、交付顺序和效率指标见
[`game-platform-upgrade-plan.md`](game-platform-upgrade-plan.md)。
