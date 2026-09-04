# Mosaico Game SDK

`game_sdk` 是 ESP-Mosaico 的通用 2D 游戏层。游戏继续面向 Raylib 兼容 API，
GSP 只承担 480×480 RGB565 Canvas 提交、镜像和展示。

完整的分层、项目结构、帧生命周期和部署约束见
[`docs/game-platform.md`](../docs/game-platform.md)。新项目通过
`cmake/mosaico_game_sdk.cmake` 声明 `RAYLIB`、`AUDIO`、`TILEMAP` 能力，
不要复制维护 `EXTRA_COMPONENT_DIRS`。
组件职责、API 所有权与生命周期约定见
[`components/README.md`](components/README.md)。

## 组件

- `mosaico_game`：固定时间步、生命周期和帧统计。
- `mosaico_game_assets`：通过 `esp_mmap_assets` 从 `game_assets` 分区零拷贝访问资源。
- `mosaico_game_2d`：RGB565 Atlas、不透明/binary-alpha 快速路径、A8 Alpha、裁剪、翻转、缩放、动画帧选择和旋转回退。
- `mosaico_game_tilemap`：有限正交 Tiled 地图、视口裁剪、碰撞、对象与路径查询。
- `mosaico_game_audio`：8 路 SFX、1 路循环 BGM、PCM16/IMA-ADPCM 解码、饱和混音及 underrun/voice stealing 统计。
- `mosaico_raylib_fast`：`LoadTexture`、`DrawTexture*` 和基础 Raylib 绘制兼容层。
- `mosaico_raylib_port`：PSRAM framebuffer 到 GSP Canvas 的非阻塞提交。

设备运行时不会解析 PNG、JSON、TMJ 或 WAV。构建工具
`tools/pack_game_assets.py` 根据 `mosaico-game-assets/v1` 的
`game_assets.json` 将这些源文件转换为确定性的 `.atlas`、`.map` 和
`.sound`，同时生成稳定资源 ID 与动画表 `assets_ids.h`，随后打包成与 Host 共用的
`game_assets.bin`。项目只声明实际存在的 Atlas、地图与声音；工具不再隐式生成
项目专用内容。Atlas 可按项目选择平滑 A8 或构建期 binary-alpha。资源上限为 1 MiB，超限会在
构建阶段失败。

## 开发循环

```bash
python mosaico.py game build --project projects/tower_defense
python mosaico.py game run --project projects/tower_defense --headless
python mosaico.py game run --project projects/tower_defense --headless \
  --replay replay.json --state-output artifacts/state.json
python mosaico.py game run --project projects/tower_defense
python mosaico.py install --project projects/tower_defense
```

回放文件按帧记录 `tap`、`pause`、`resume`、`step` 和 `reset`，例如：

```json
{"events":[{"frame":0,"type":"tap","x":240,"y":220},{"frame":60,"type":"pause"},{"frame":61,"type":"step"}]}
```

创建第三方项目时可选择简单射击模板，或包含 Atlas、Tilemap、音频和动画的资源化模板：

```bash
python mosaico.py game new my_game
python mosaico.py game new my_strategy_game --template tower-defense
```

交互预览默认监听 `127.0.0.1:8460`。需要局域网访问时显式传入
`--listen 0.0.0.0`；不要在不可信网络开放该开发服务。

首次采用资源分区的设备必须先运行一次 `python mosaico.py recover`。之后 `install`
会自动发现 `game_assets.bin`，构造 ESP-Iris system-update 包，依次写入资源和应用，
两者校验成功后才启动应用。没有资源镜像的旧项目仍走原单应用 OTA。
