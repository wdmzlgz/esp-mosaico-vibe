# GSP Air Battle

Vertical shooter copied from `projects/gsp_hello`, then rewritten as a
480×480 ESP-GSP game. The same scene and portable C run in the PC
WebAssembly host and on the device `ota_0` image.

Rules follow the common mobile “airplane battle” loop (drag to move,
automatic fire, three enemy sizes, three lives). Artwork and names are
original. Factory remains the LVGL Recovery template.

## Simulate

From the vibe repository root:

```sh
python3 tools/gsp-sim/run.py projects/gsp_air_battle/ui/main.json --headless --dump-ppm /tmp/gsp-air-battle.ppm
python3 tools/gsp-sim/run.py projects/gsp_air_battle/ui/main.json --interactive
```

Open the printed URL. Use the LAN address when the browser is not on the
machine that runs `run.py`.

Portable logic lives in `app/gsp_air_battle_app.c`.

## Flash

Recovery must already be on the device (`python mosaico.py recover`). Then:

```sh
python mosaico.py install --project projects/gsp_air_battle
```
