---
name: develop-gsp
description: >
  Develop, port, or modify ESP-GSP scenes and portable C for vibe projects
  under projects/<name>. Use when implementing GSP JSON, C interactions,
  assets, icons, or touch/gesture behavior. Do not use Mosaic claw hub,
  Lua, or the HTML review site.
---

# Develop ESP-GSP UI

Build 480×480 GSP UI for an ESP-Mosaico application. Keep the scene, the
portable C, the host WASM preview, and the real device as separate proofs.

## Start here

On skill activation, before any GSP edit:

1. Confirm `submodule/esp-gsp` is present (espressif/esp-gsp **1.1.0**).
   Initialize only that submodule if it is missing:

   ```sh
   git submodule update --init submodule/esp-gsp
   ```

2. Read [references/gsp-development.md](references/gsp-development.md). It is
   the project map. Do not rescan the entire ESP-GSP component by default.
3. Identify the target application under `projects/<name>/`. Start from
   `projects/gsp_hello` for a new GSP app. Do not put user UI in
   `projects/factory`.
4. Inspect only that project's `ui/`, `app/`, and `main/`.
5. Do not import Mosaic claw hub, Lua runtime, or the HTML review site.
6. Do not edit generated files under `projects/*/build/`,
   `tools/gsp-sim/build-web/`, or generated `*_binds.h` / `*_objects.h`.

Then load `skills/gsp-sim/SKILL.md` for the preview commands.

## Asset-first workflow

Before drawing or generating any icon or artwork:

1. Search the owning project and nearby GSP apps for an existing asset
   (`projects/<name>/ui/`, fonts, PNGs, and other `projects/*/ui/` assets).
2. If a matching repository asset exists, reuse it. Do not redraw it.
   Keep raster assets next to the scene, typically `projects/<name>/ui/`.
3. Do not ship SVG to GSP. Convert it first:

   ```sh
   python3 skills/develop-gsp/scripts/svg_to_png.py \
     path/to/icon.svg path/to/output.png --width 48 --height 48
   ```

   Give GSP the PNG path relative to the scene JSON.

## Image preparation tools

Use the bundled Python tools instead of one-off image scripts. They need
Pillow (and cairosvg for SVG). Prefer a project virtualenv; do not install
into system Python.

```sh
python3 skills/develop-gsp/scripts/inspect_image.py path/to/input.png --json

python3 skills/develop-gsp/scripts/make_transparent.py \
  path/to/input.png path/to/cutout.png \
  --background "#FFFFFF" --tolerance 24 --feather 1 --crop --padding 2

python3 skills/develop-gsp/scripts/crop_resize.py \
  path/to/cutout.png path/to/final.png \
  --trim-alpha --padding 2 --width 96 --height 96 --fit contain
```

Inspect every output. Use `make_transparent.py` only for flat or near-flat
backgrounds. For photographic backgrounds, use a proper matting tool rather
than raising tolerance until the subject is destroyed.

## Model-generated assets

If no suitable repository asset exists, the agent may use the available
image-generation capability without a separate permission ask.

Generate only the missing artwork (icons, empty/loading states, textures),
not a flattened screenshot of the whole UI. Store it under that project's
`ui/` unless several apps share it.

Before generating, inspect at least three neighboring assets of the same
role and pass their size, background, palette, and stroke traits to the
image tool. Match the owning project (gsp_hello is dark blue/gray, not
Mosaic claw black/red). Preview the result in the RGB565 GSP render.

Priority: existing repository asset → adaptable nearby asset → model
generation.

## Implementation rules

- Author layout, static appearance, assets, and simple declarative actions
  in `projects/<name>/ui/*.json` at **480×480 RGB565**.
- Put portable timers, binds, and click/touch behavior in
  `projects/<name>/app/*.c`. Export `gsp_app_start(esp_gsp_handle_t)`.
  The preview top-right key is GPIO7 (active-low). Handle it in portable
  `app/` with `driver/gpio.h` if the WASM preview should react; otherwise
  leave it unused so the key stays inert. Do not add a fake Back hook.
  Device `main/` only does board, Recovery/Iris, and `gsp_app_start`.
- Give dynamic objects stable `name`/`bind` and callbacks stable
  `callback` names. Prefer generated helpers after a device or host build
  has produced them. Never invent bind IDs or edit generated headers.
- Use PageFlow, StackView, Drawer, List/Grid, Canvas, and timers as
  summarized in the reference. Keep callbacks short and non-blocking.
- Keep UI and pure logic platform-neutral. Put FreeRTOS, drivers, NVS,
  networking, sensors, and board calls behind `#if defined(ESP_PLATFORM)`
  or in `main/`. Host/WASM success is not proof of ESP hardware behavior.
- `projects/factory` stays LVGL Recovery. `mosaico.py` remains the device
  path: Recovery first, then `install`.

## Game projects

Before designing or coding a game, look up the rules and established logic
of that game or its closest canonical equivalent. Confirm the state model,
tick timing, controls, collision, scoring, and win/lose/restart conditions.
Summarize those findings before editing files.

Do not copy copyrighted code, artwork, names, or sounds. Adapt controls to
the 480×480 touch panel. If sources disagree in a way that changes
gameplay, ask the user which variant to implement.

## Required completion loop

After every completed GSP scene or portable-C change, from the vibe
repository root:

```sh
python3 tools/gsp-sim/run.py --headless --dump-ppm /tmp/gsp-hello.ppm
python3 tools/gsp-sim/run.py projects/<name>/ui/main.json --interactive
```

Fix pack, compile, and link errors and rerun until they succeed. Interactive
mode builds the application WASM and serves it on port 8877. Use the printed
LAN URL when the browser is not on this machine (`127.0.0.1` is only the
host that runs `run.py`).

Report host/WASM validation and real ESP validation separately. If no
`mosaico.py install` happened, say `ESP target: not run`.
