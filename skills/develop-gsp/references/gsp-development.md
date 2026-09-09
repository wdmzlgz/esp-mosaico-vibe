# Vibe GSP development map

Use this map for routine GSP work, then read the target application. Consult
vendored ESP-GSP documentation only for an API not covered here or when the
pinned component version changes.

Do not import Mosaic claw hub, Lua applications, or the HTML review site.

## Validation layers

| Layer | Source | What it proves |
| --- | --- | --- |
| Standalone PC simulator | `tools/gsp-sim/` | Scene packing and static rendering |
| `sim_bridge` | Project `pc/` + portable application C | Timers, binds, callbacks, and generated API behavior on PC |
| ESP target build | `projects/<name>/` + esp-gsp 1.2.0 | Application C and generated API compile/link |
| ESP-Mosaico device | Installed with `mosaico.py` | Display, touch, hardware, Recovery/Iris |

Do not infer that a static rendered scene proves application C or hardware
behavior. Use `sim_bridge` for portable C; only the device proves hardware.

## Project layout

Use `projects/gsp_hello/` as the reference:

- `ui/main.json`: authored scene; keep related fonts and images beside it.
- `main/main.c`: GSP session startup, application interaction, board setup,
  and Iris/Recovery integration.
- `main/idf_component.yml`: `espressif/esp-gsp ==1.2.0` from the ESP
  Component Registry.
- `main/CMakeLists.txt`: `gsp_add_bundle()` generates the deployable bundle
  and typed headers.
- Portable UI C shared by device and PC, following `projects/gsp_hello`.
- `pc/CMakeLists.txt` and its thin platform adapter: `sim_bridge` backend.

`tools/gsp-sim/run.py` uses `sim_bridge` by default when the project has a
`pc/CMakeLists.txt`; scene-only flags pack the same JSON with the pinned
`gspc` and launch the standalone `sim`.

Generated `*_binds.h`, `*_actions.h`, and `*_objects.h` appear under the
project build directory. Never edit, copy, or commit them. Their symbols must
come from the authored `bind`, `callback`, and `name` fields.

## Existing-application workflow

1. Read `ui/main.json`, relevant application C, and the project's build file.
2. Reuse or prepare assets under the owning project's `ui/`.
3. Edit the scene while preserving 480×480 RGB565, font policy, object names,
   bind names, and callback conventions.
4. Run the headless scene-only simulator and inspect the image.
5. Run the interactive `sim_bridge` preview for portable C behavior.
6. Build the ESP target after changing C or fields that generate typed APIs.
   Inspect generated headers rather than guessing symbols or numeric IDs.
7. Install only through the retained Recovery workflow with `mosaico.py`.

For a new application, copy `projects/gsp_hello`, retain its Recovery/Iris
contract, and place the new scene under `projects/<name>/ui/`.

## Scene authoring

The pinned compiler supports content widgets, containers and navigation,
List/Grid-style data widgets, composite widgets, Canvas, images, and pointer
or gesture input. Confirm exact field names against:

- `projects/<name>/managed_components/espressif__esp-gsp/docs/en/reference/authoring.md`
- `projects/<name>/managed_components/espressif__esp-gsp/docs/en/guide/scenes.md`

Use 480×480 coordinates. Object-array order is paint and hit order. Keep
layout and static appearance in the scene. Prefer declarative actions for
simple behavior:

- `show`, `hide`, `toggle`
- `set_value`, `add_value`, `toggle_value`, `set_text`,
  `set_bg_color`
- `goto`, `set_page`, `stack_push`, `stack_pop`
- `drawer_open`, `drawer_close`
- `call` for application logic

An object `bind` generates `GSP_BIND_*`; stable `name` and `callback`
fields generate typed helpers. Never guess a generated symbol. Rebuild and
inspect the headers.

For dynamic text, declare a `font_charset` covering every runtime glyph.
Reference raster images relative to the scene. Do not ship SVG directly.

## Application C

Application code works with a live `esp_gsp_handle_t` and may use:

- `esp_gsp_timer_create/delete()`
- `esp_gsp_set_text/value/color/visible()` and generated component setters
- PageFlow, StackView, and Drawer APIs
- List/Grid bind, total, refresh, and row/cell publication
- `esp_gsp_set_image*()` for occasional images
- `esp_gsp_canvas_push()` or direct drawing for changing pixels

Setters are asynchronous. Keep timer, event, list-binder, image-release, and
Canvas callbacks short. Do not call `esp_gsp_flush()` in normal frame loops.

Image ownership:

- `esp_gsp_set_image()` copies.
- `esp_gsp_set_image_borrowed()` borrows until the release callback.
- `esp_gsp_set_image_owned()` takes malloc-compatible memory after submit.
- Canvas frames stay borrowed until their release callback.

If submission fails immediately, ownership remains with the caller. List/Grid
row handles are callback-scoped; bind each list once per UI lifetime.

## Build, simulate, and install

Scene simulation:

```sh
python3 tools/gsp-sim/run.py --headless --dump-ppm /tmp/gsp-preview.ppm
python3 tools/gsp-sim/run.py projects/<name>/ui/main.json --interactive
```

The first `sim_bridge` preview in a new project needs
`espressif/esp-gsp==1.2.0` under `managed_components/`; run
`idf.py reconfigure` in that project after resolving ESP-IDF.

Use the repository's ESP-IDF environment and low-noise build guidance for an
ESP target build. For device operations:

```sh
python mosaico.py recover
python mosaico.py install --project projects/<name>
```

Use `python mosaico.py system-update --project projects/<name>` when the scene
or packed UI partition changes and the project's documented workflow requires
it.
