# Vibe GSP development map

This is the stable project map for routine GSP work. Read the target
application after this document. Read vendored ESP-GSP docs only for an API
not covered here or when the component version changes.

Do not import Mosaic claw hub, Lua apps, or the HTML review site.

## Two runtimes, one scene

| Layer | Source | Purpose | Proof it provides |
| --- | --- | --- | --- |
| Host WASM / official `sim` | `tools/gsp-sim/` + `projects/<name>/app/` | Same scene and portable C on PC | Pack, timers, binds, touch |
| ESP target | `projects/<name>/main/` + `submodule/esp-gsp` **1.1.0** | Display, touch, Recovery/OTA | Real-board behavior |

Firmware links `submodule/esp-gsp` 1.1.0. The interactive WASM host links
official `espressif/esp-gsp` **1.0.0** from the Component Registry
(`tools/gsp-sim/fetch_gspc.py --sdk` → `tools/gsp-sim/sdk/esp-gsp`). Host
success is not board proof. Registry 1.1.0 strips `include/gsp/sim` and
`prebuilt/sim`; 1.0.0 still ships the session SDK.

## Architecture and files

A GSP application looks like `projects/gsp_hello/`:

- `ui/main.json`: authored scene. Keep extra JSON and fonts/images beside it.
- `ui/sim_backend.json`: optional timers for the scene-only player
  (`run.py --scene-player`). The application WASM path does not need it.
- `app/*.c`, `app/*.h`: portable logic. Must export
  `gsp_app_start(esp_gsp_handle_t)`.
- `main/main.c`: NVS, display/touch, `esp_gsp_esp_lcd_start`,
  `gsp_app_start`, Iris enter-Recovery RPC.
- `main/idf_component.yml`: `espressif/esp-gsp` `==1.1.0` or
  `override_path` to `submodule/esp-gsp`.
- `main/CMakeLists.txt`: `gsp_add_bundle()` on the scene JSON; compile
  `app/` into the firmware component.

Host files:

- `tools/gsp-sim/run.py`: pack with gspc 0.2.8, build/serve WASM, or dump PPM.
- `tools/gsp-sim/host/gsp_app_sim.c`: Emscripten host. Creates a GSP
  session, then calls `gsp_app_start`.
- `tools/gsp-sim/scripts/build_wasm.sh`: `emcmake` build. Needs `~/emsdk`
  or `MOSAIC_EMSDK_ENV`.
- Official standalone `sim`: headless PPM only (`--headless --dump-ppm`).

Generated `*_binds.h`, `*_actions.h`, `*_objects.h` appear under the IDF
build directory. Never edit or commit them. Bind ids in portable C must
match the scene (`"bind": "load"` → `GSP_BIND_LOAD` / `1` in gsp_hello).

## Normal existing-app change

1. Read `ui/main.json` and `app/*.c`.
2. Copy/reuse assets into that project's `ui/`.
3. Edit the scene JSON. Keep 480×480, existing font policy, object
   naming, and callback conventions.
4. Run `python3 tools/gsp-sim/run.py --headless --dump-ppm /tmp/gsp-hello.ppm`.
5. If C needs new binds or callbacks, rebuild the host
   (`run.py --interactive`) and inspect generated headers from a device
   build when typed helpers are required.
6. Flash only after Recovery: `python mosaico.py install --project projects/<name>`.

For a new GSP app, copy `projects/gsp_hello`, keep the Recovery/Iris
contract from that project (not factory's LVGL UI), and put the new scene
under `projects/<name>/ui/`.

## Scene authoring model

The pinned GSP compiler supports:

- Content/state: label, image, button, slider, progress, toggle, checkbox,
  radio, dropdown, arc, chart, clock, spinner, shape, needle.
- Structure/navigation: container, layer, PageFlow, StackView, Drawer.
- Data: List, Grid, Wheel, Message List with recycled rows/cells.
- Composites: table, TabView, keyboard, message box.
- Runtime media: encoded images and Canvas.
- Input: click, press, release, long press, value change, pointer, pinch.

Use 480×480 coordinates. Object array order is paint and hit order.
Layout and static appearance belong in the scene. Prefer scene-local
declarative actions for simple behavior:

- `show`, `hide`, `toggle`
- `set_value`, `add_value`, `toggle_value`, `set_text`, `set_bg_color`
- `goto`, `set_page`, `stack_push`, `stack_pop`
- `drawer_open`, `drawer_close`
- `call` for application logic

An object `bind` generates `GSP_BIND_*`; a stable `name` enables typed
component helpers; a `callback` generates event helpers. Never guess a
generated symbol. Rebuild and inspect the headers.

For dynamic text, declare a `font_charset` that covers every runtime
glyph. For a static PNG, use a path relative to the scene. Do not ship
SVG directly.

Widget field details live in
`submodule/esp-gsp/docs/en/reference/authoring.md` and
`submodule/esp-gsp/docs/en/guide/scenes.md`.

## C interaction model

Portable code talks to a live `esp_gsp_handle_t`:

- `esp_gsp_timer_create/delete()`
- `esp_gsp_set_text/value/color/visible()` and component setters
- `esp_gsp_page_flow_*`, `esp_gsp_stack_view_*`, `esp_gsp_drawer_*`
- List/Grid bind, total, refresh, row/cell publication
- `esp_gsp_set_image*()` for occasional PNG/JPEG
- `esp_gsp_canvas_push()` or direct draw for continuously changing pixels

Device `app_main` starts the LCD session, then `gsp_app_start(ui)`. The
WASM host does the same after `esp_gsp_sim_session_create`.

ESP-IDF setters are asynchronous. Keep timer, event, list-binder, image
release, and Canvas callbacks short. Do not call `esp_gsp_flush()` in
normal frame loops.

Image ownership:

- `esp_gsp_set_image()` copies.
- `esp_gsp_set_image_borrowed()` borrows until the release callback.
- `esp_gsp_set_image_owned()` takes malloc-compatible memory after submit.
- Canvas frames stay borrowed until their release callback.

If submit fails immediately, ownership stays with the caller. A List/Grid
row handle is callback-scoped. Bind each list once per UI lifetime.

Reference: `projects/gsp_hello/app/gsp_hello_app.c` (timer + bind).

## Host versus ESP_PLATFORM

Portable `app/` must compile for host and ESP:

```c
static void feature_tick(esp_gsp_handle_t ui, void *ctx)
{
#if defined(ESP_PLATFORM)
    /* Read staged product data; no board init here. */
#else
    /* Deterministic simulator/demo state. */
#endif
    (void)esp_gsp_set_value(ui, GSP_BIND_LOAD, value);
}
```

Keep ESP-IDF-only headers, FreeRTOS, drivers, NVS, and sensors in
`main/` or behind `#if defined(ESP_PLATFORM)`. Do not claim host mocks
are device behavior.

## Build and run

From the vibe repository root:

```sh
python3 tools/gsp-sim/run.py --headless --dump-ppm /tmp/gsp-hello.ppm
python3 tools/gsp-sim/run.py --interactive
python3 tools/gsp-sim/run.py projects/<name>/ui/main.json --interactive
```

Interactive preview listens on `0.0.0.0:8877`. Open
`http://127.0.0.1:8877/` only on the machine that runs `run.py`. From
another PC on the LAN, use the printed `http://<host-ip>:8877/`.

`--native` is the official 1.1.0 loopback sim. `--scene-player` is the
scene-only WASM player (optional `ui/sim_backend.json`).

Device:

```sh
python mosaico.py recover
python mosaico.py install --project projects/<name>
```

Final reports must state separately:

- Scene/generated API: pass/fail
- WASM/host: pass/fail
- ESP target build: run/not run
- Board: run/not run
