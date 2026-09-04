---
name: gsp-sim
description: >
  Preview ESP-GSP scenes on the PC. Interactive mode uses the mosaico-ui
  WebAssembly player; headless dumps use the official sim matching
  submodule/esp-gsp (espressif/esp-gsp 1.1.0). Use when authoring or
  debugging GSP JSON/UI for ESP-Mosaico before flashing.
---

# ESP-GSP host simulation

Use this skill when the application UI is **GSP**, not LVGL. Factory remains
LVGL unless the task explicitly switches it.

## Pin

- Runtime: `submodule/esp-gsp` = **espressif/esp-gsp 1.1.0**
- Compiler: standalone `gspc` from `.gspc_version` (fetched by `fetch_gspc.py`)
- Interactive preview: mosaico-ui-style host in `tools/gsp-sim/host/`
  compiles `projects/<name>/app/*.c` (`gsp_app_start`) to WASM
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
