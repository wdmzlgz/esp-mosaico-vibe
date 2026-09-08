---
name: gsp-sim
description: >
  Preview ESP-GSP scenes on the PC. Interactive mode uses the application
  WebAssembly host; headless dumps use the official sim matching
  submodule/esp-gsp (espressif/esp-gsp 1.1.0). Use when authoring or
  debugging GSP JSON/UI or virtual subboards for ESP-Mosaico before flashing.
---

# ESP-GSP host simulation

Use this skill to **preview** GSP scenes. For authoring scenes, portable C,
and assets, load `skills/develop-gsp/SKILL.md` first. Factory remains LVGL
unless the task explicitly switches it.

## Pin

- Runtime: `submodule/esp-gsp` = **espressif/esp-gsp 1.1.0**
- Host session SDK: official Component Registry **espressif/esp-gsp 1.0.0**
  (`python3 tools/gsp-sim/fetch_gspc.py --sdk`)
- Compiler: standalone `gspc` from `.gspc_version` (fetched by `fetch_gspc.py`)
- Interactive preview: host in `tools/gsp-sim/host/` compiles
  `projects/<name>/app/*.c` (`gsp_app_start`) to WASM
- Headless dump: official standalone `sim` (`GSP_SIM_EXECUTABLE`)

## Run

From the vibe repository root:

```sh
python3 tools/gsp-sim/run.py --headless --dump-ppm /tmp/gsp-hello.ppm
python3 tools/gsp-sim/run.py --interactive
python3 tools/gsp-sim/run.py projects/<name>/ui/main.json --interactive
```

The default scene is `projects/gsp_hello/ui/main.json`. Headless dump is the
Agent-safe check. Interactive mode serves `http://0.0.0.0:8877/` (open
`http://127.0.0.1:8877/`). Extra official-`sim` flags go after `--` and
require `--headless` or `--native`.

## Device chrome and GPIO7

The preview draws 480×480 hardware chrome. The top-right orange key is
**GPIO7**, active-low (idle high, pressed = 0). Pointer down/up writes
that level into the host GPIO stub (`tools/gsp-sim/host/gsp_sim_gpio.c`).

Enable the key only when portable `app/` C actually uses GPIO7:

- `gpio_config()` with bit 7, or
- `gpio_get_level(GPIO_NUM_7)`, or
- `gpio_isr_handler_add(GPIO_NUM_7, …)` / `gpio_set_intr_type(7, …)`

The key always plays the press animation. GPIO level changes only when
`app/` has armed GPIO7. If it never does, the key is chrome-only. Do
**not** invent a Back callback (`gsp_app_on_back` is gone). Device-only
GPIO in `main/` does not reach the WASM binary, so it does not drive
the preview pin.

Host `driver/gpio.h` is a pin-level stub so the same `app/` C can compile
off-target. It is not ESP-IDF. Keep `iot_button` and other IDF extras in
`main/` or behind `#if defined(ESP_PLATFORM)`.

## Subboard simulation

Before simulating expansion hardware, read the pin map and complete contract
in
[`../develop-gsp/references/subboard-development.md`](../develop-gsp/references/subboard-development.md).

- Put host adapters behind `#if !defined(ESP_PLATFORM)` and keep normal
  business logic shared.
- Do not emulate EEPROM or descriptor validation. Show the application’s
  supported subboards below the device; click inserts, and clicking the
  selected item again removes it.
- The project logic owns every interaction after virtual insertion.
- A camera simulation may use the computer camera, but must preserve the real
  application's capture, crop, scale, orientation, output range, and cleanup.
- Show left and right LED boards separately. Identical physical output appears
  center-symmetric because the right board is mounted 180 degrees.
- Keep LED animation/RGB generation shared. Browser code visualizes emitted
  light over the unlit diffuser; it must not paint a zero-brightness cell black.
- If real WS2812 output occasionally shifts only part of the image, investigate
  RMT starvation without DMA before changing pixel mapping; consider RMT DMA
  after checking target support and memory/power constraints.

## Authoring rules

- Scene size **480×480**, RGB565, matching the CO5300 panel.
- Keep JSON under the application, typically `projects/<name>/ui/`.
- Start from `projects/gsp_hello` for a sim + flash Hello World.
- Firmware depends on `espressif/esp-gsp` `==1.1.0` or
  `override_path: ../../../submodule/esp-gsp`.
- Do not import Mosaic claw hub, Lua runtime, or HTML review site into vibe.
- `mosaico.py` is unchanged: device install still goes through Recovery/OTA.

## Acceptance

1. `run.py --headless --dump-ppm` exits 0 and writes a 480×480 PPM.
2. `run.py --interactive` builds `gsp_app_sim.html` and serves it on :8877.
3. The same scene JSON is what firmware will pack with the pinned ESP-GSP.
4. True-device validation still uses `python mosaico.py install` after Recovery.
