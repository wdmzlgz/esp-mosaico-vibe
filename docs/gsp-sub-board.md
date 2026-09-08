# 子板演示与 WASM 仿真

应用位于 `projects/gsp_sub_board`，共用 ESP-GSP 场景、交互逻辑、RGB565 裁剪与灯效计算。

## 操作

在仓库根目录启动（选项放在场景路径前）：

```sh
python3 tools/gsp-sim/run.py --interactive projects/gsp_sub_board/ui/main.json
```

打开 http://127.0.0.1:8877/，点击设备右下方电源键开机。下方卡片模拟插入或拔出子板；再次点击已选卡片即拔出。摄像头仅支持左槽，左右槽均支持 8×8 WS2812 灯板。

插拔时屏幕弹出状态提示，对应功能按钮随连接状态显示或消失。点击任一灯板入口启动当前已连接灯板的同步呼吸效果；演示中插入另一块灯板也会加入。摄像头和灯效互斥。右上方橙色 GPIO7 按键退出演示，停止摄像头或熄灭灯板。拔出正在演示的摄像头也会退出；两块灯板中拔出一块时另一块继续工作。

摄像头入口会请求浏览器访问电脑摄像头。必须允许权限；使用本机 localhost 或 HTTPS。普通局域网 HTTP 地址通常无法使用浏览器摄像头，可通过端口转发在电脑 localhost 访问。网页只模拟子板连接状态，不读取或校验 EEPROM。

## 实机实现

- 热插拔组件通过 BSP `mosaico_module_mgr` 对 EEPROM 去抖及校验，注册 `CameraBoard`（类型 0x07）和 `MatrixLedBoard`（类型 0x11），每个槽独立识别。类型定义和烧录工具一致；不将三灯 `ButtonLedBoard`（0x14）当作矩阵板。
- 摄像头由 BSP `mosaico_module_camera` 在后台打开 OV3640，采集 1024×768 UYVY，居中裁剪实际 480×480 像素并转换 RGB565，提交 GSP Canvas。浏览器同样居中取 480×480；源分辨率不足时对中央正方形缩放。
- 每帧先归还摄像头采集缓冲，再提交独立 RGB565 缓冲；GSP release callback 负责释放显示缓冲。退出释放摄像头、恢复 EEPROM 发现。摄像头占用槽位期间 BSP 暂停该槽 EEPROM 扫描，拔出通过采集失败发现，随后恢复扫描。
- 用户确认的灯板 DIN 为左 **GPIO48**、右 **GPIO47**。分别创建 WS2812 GRB / 10 MHz RMT 实例，64 灯。可通过 `CONFIG_SUB_BOARD_DIN_LEFT_GPIO`、`CONFIG_SUB_BOARD_DIN_RIGHT_GPIO` 修改。
- 实物确认矩阵电气灯序为列优先。右侧同款硬件安装旋转 180°，逻辑像素 `i` 映射到物理 `63-i`；浏览器按列还原物理灯序并抵消右侧安装旋转，使两侧显示与实物一致。若后续 PCB 改为蛇形灯序，应同步调整 `sb_matrix_index()` 和浏览器位置映射。
- 共用约三秒的余弦呼吸和约九秒旋转一周的 HSV 彩虹渐变，再转换为 RGB 同时提供给真机和浏览器；真机单通道范围为 0～96/255，以限制 64 颗灯同时点亮时的功耗。浏览器桥接层将 RGB 拆成归一化色相和亮度，使用亮度控制叠加在浅色灯罩上的发光层；熄灭时只显示灯罩基底，低亮度不会用暗 RGB 覆盖灯罩。退出、移除或初始化错误都会清灯并释放 RMT。
- 摄像头 D2 占 GPIO33，已按 BSP 摄像头示例关闭 USB Serial/JTAG，ESP-Iris 继续使用独立 High-Speed USB。保留原应用的 Recovery、OTA 和 `ui_apps` 分区合同。

## 验证与安装

```sh
python3 tools/gsp-sim/run.py --headless --dump-ppm /tmp/gsp-sub-board.ppm projects/gsp_sub_board/ui/main.json
```

实机安装仅通过仓库 `mosaico.py`。空白或未验证设备先 `python mosaico.py recover`，普通应用安装使用 `python mosaico.py install --project projects/gsp_sub_board`；UI 分区更新遵循该工具的 System Update 工作流。烧录成功后仍需验证同一 Device ID 的 normal → Recovery → normal、Boot ID 变化及实际摄像头和灯效。

WASM 的浏览器测试使用 Chromium 模拟摄像头验证媒体路径；这不替代电脑实体摄像头或真实子板验收。
