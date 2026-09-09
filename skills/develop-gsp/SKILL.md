---
name: develop-gsp
description: >
  Develop, port, or modify ESP-GSP scenes and application C for projects in
  this repository. Use when implementing GSP JSON, generated APIs,
  assets, icons, touch/gesture behavior, or subboard detection and drivers.
  Do not use Mosaic claw hub, Lua, or the HTML review site.
---

# Develop ESP-GSP UI

Build 480×480 GSP UI for an ESP-Mosaico application. Keep the scene-only
render, the `sim_bridge` application preview, the ESP target build, and
real-device behavior as separate proofs.

## Start here

Before any GSP edit:

1. Confirm the target project declares **espressif/esp-gsp 1.2.0** from the
   ESP Component Registry. If its managed component is missing, resolve the
   ESP-IDF environment and run from that project:

   ```sh
   idf.py reconfigure
   ```

2. Read [references/gsp-development.md](references/gsp-development.md), then
   inspect only the target project's `ui/`, `main/`, and optional `app/`.
3. Start a new GSP application from `projects/gsp_hello`. Do not use the
   Recovery project as an application template.
4. For expansion-board work, read
   [references/subboard-development.md](references/subboard-development.md)
   before changing detection, GPIO, camera, LED, or related application code.
5. Load `skills/gsp-sim/SKILL.md` for the repository's preview commands.
6. Do not import Mosaic claw hub, Lua runtime, or the HTML review site.
7. Do not edit generated files under `projects/*/build/` or generated
   `*_binds.h`, `*_actions.h`, or `*_objects.h`.

## Asset-first workflow

Before drawing or generating artwork:

1. Search the owning project and nearby GSP applications for a suitable asset.
2. Reuse a matching repository asset when one exists. Keep raster assets next
   to the owning scene, normally under `projects/<name>/ui/`.
3. Convert SVG to PNG before referencing it from a scene:

   ```sh
   python3 skills/develop-gsp/scripts/svg_to_png.py \
     path/to/icon.svg path/to/output.png --width 48 --height 48
   ```

Use the bundled image tools instead of one-off scripts. They require Pillow;
SVG conversion also requires CairoSVG. Prefer a project virtual environment.

```sh
python3 skills/develop-gsp/scripts/inspect_image.py path/to/input.png --json

python3 skills/develop-gsp/scripts/make_transparent.py \
  path/to/input.png path/to/cutout.png \
  --background "#FFFFFF" --tolerance 24 --feather 1 --crop --padding 2

python3 skills/develop-gsp/scripts/crop_resize.py \
  path/to/cutout.png path/to/final.png \
  --trim-alpha --padding 2 --width 96 --height 96 --fit contain
```

Inspect every output. Use background removal only for flat or near-flat
backgrounds; use a proper matting tool for photographs.

If no suitable repository asset exists, model-generated artwork may be used.
Generate only the missing icon, texture, or state—not a flattened screenshot
of the whole UI. Choose the generation style from the target project's actual
product needs, audience, information hierarchy, existing visual language, and
480×480 RGB565 display constraints; do not reuse a generic house style across
unrelated projects. Inspect neighboring assets of the same role and match
their size, background, palette, stroke, material, and lighting where that
supports the project direction.

After integrating generated artwork, render the complete GSP scene and inspect
the simulator screenshot at its real 480×480 output size. Judge the whole
interface rather than the source asset in isolation: visual consistency,
hierarchy, spacing, contrast, icon weight, legibility after downscaling, and
generation artifacts all matter. If the result is unattractive or does not
fit the project, identify the concrete causes, revise the style or prompt and
any crop/scale/composition choices, regenerate the affected artwork, and
repeat the screenshot review. Do not accept an asset merely because generation
and scene packing succeeded.

Priority: existing repository asset → adaptable nearby asset → generated asset.

## Implementation rules

- Author layout, static appearance, assets, and simple declarative actions in
  `projects/<name>/ui/*.json` at **480×480 RGB565**.
- Put timers, binds, and click/touch behavior in application C. Existing
  projects may keep this in `main/`; a project may separate reusable logic
  into `app/` and expose `gsp_app_start(esp_gsp_handle_t)`.
- Give dynamic objects stable `name`/`bind` values and callbacks stable
  `callback` names. Generate typed helpers with the project build. Never
  invent bind IDs or edit generated headers.
- Keep callbacks short and non-blocking. Keep board initialization, Recovery,
  Iris, hardware drivers, and platform-specific services in device code.
- Use `sim_bridge` to validate portable application C and generated-symbol
  behavior on the PC. Use the scene-only simulator for static render captures.
  Neither is proof of ESP hardware behavior.
- Keep the retained Recovery path and use `mosaico.py` for device operations.

## Game projects

Before implementing a game, verify the rules and established logic of the
canonical game or closest equivalent: state, timing, controls, collision,
scoring, and win/lose/restart conditions. Do not copy copyrighted code,
artwork, names, or sounds.

## Completion loop

After every scene change, from the repository root:

```sh
python3 tools/gsp-sim/run.py --headless --dump-ppm /tmp/gsp-preview.ppm
python3 tools/gsp-sim/run.py projects/<name>/ui/main.json --interactive
```

Fix packing and rendering failures and rerun. Projects with portable
application C must include `pc/CMakeLists.txt` so interactive preview uses
`sim_bridge`; verify its timers, binds, and callbacks actually change the
live preview. Also build the ESP project with the resolved ESP-IDF environment
and inspect generated headers in that build when needed. Never copy generated
headers into source.

Report these independently:

- Scene simulation: pass/fail/not run
- sim_bridge application behavior: pass/fail/not run
- ESP target build and generated API: pass/fail/not run
- Board validation: pass/fail/not run
