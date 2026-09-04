# ESP-GSP host simulator

PC preview for GSP scenes on ESP-Mosaico. It packs JSON with the GSPC version
recorded in `submodule/esp-gsp/.gspc_version` and then:

- **interactive** — Emscripten-builds the project's portable C into a WASM
  host (GSP session + `gsp_app_start`)
- **headless** — runs the matching official ESP-GSP `sim` for PPM dumps

## Prerequisites

- Linux x86_64 (or another host published in the gspc/sim manifests)
- Git submodule `submodule/esp-gsp` initialized
- Network once: standalone `gspc` (and `sim` for headless dumps), plus official
  [`espressif/esp-gsp` 1.0.0](https://components.espressif.com/components/espressif/esp-gsp/versions/1.0.0)
  for the WASM session SDK. Registry 1.1.0 omits `include/gsp/sim` and
  `prebuilt/sim`.
- A browser, for interactive preview

## Run the GSP Hello World demo

Headless smoke (writes a PPM):

```sh
python3 tools/gsp-sim/run.py --headless --dump-ppm /tmp/gsp-hello.ppm
```

Interactive application WebAssembly preview (default port **8877**, binds
`0.0.0.0`). First run needs Emscripten (`~/emsdk` or `MOSAIC_EMSDK_ENV`):

```sh
python3 tools/gsp-sim/run.py --interactive
```

Open `http://127.0.0.1:8877/` or the printed LAN URL. Override with
`--host`, `--port`, `MOSAIC_WASM_HOST`, or `MOSAIC_WASM_PORT`.

The default scene is [`projects/gsp_hello/ui/main.json`](../../projects/gsp_hello/ui/main.json).
Preview another scene:

```sh
python3 tools/gsp-sim/run.py projects/<name>/ui/main.json --interactive
```

Pass a precompiled `.gspb` to skip `gspc`. Set `GSPC_EXECUTABLE` to skip the
compiler download cache. Extra official-`sim` flags (`--tap`, `--drag`,
`--wait`) go after `--` and require `--headless` or `--native`.

`--native` keeps the official 1.1.0 `sim` loopback preview at
`http://127.0.0.1:3222/` if you need that host.

## New GSP projects

Keep scene JSON under the application, typically `projects/<name>/ui/`.
The reference demo is [`projects/gsp_hello`](../../projects/gsp_hello).
Author at **480×480 RGB565** to match the CO5300 panel. Firmware should depend
on `espressif/esp-gsp` `==1.1.0` (or `override_path` to `submodule/esp-gsp`).
The tools-owned Recovery firmware remains LVGL-based and is not a GSP
application template.
