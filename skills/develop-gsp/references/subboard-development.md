# ESP-Mosaico subboards

Read this reference when a GSP application discovers, drives, or simulates a
subboard. Treat the BSP as the hardware source of truth:

- Pin map: `submodule/esp-mosaico-bsp/components/esp-mosaico-bsp/onboard/subboard.c`
- Public API: `submodule/esp-mosaico-bsp/components/esp-mosaico-bsp/include/bsp/subboard.h`
- Board identity and slot state: `mosaico_module_mgr`

## Fixed main-board and slot wiring

- Main-board top-right key: **GPIO7**, active-low.
- Shared subboard I2C: SDA **GPIO0**, SCL **GPIO1**.
- Left EEPROM: A0 **GPIO14 = 0**, 7-bit address **0x50**.
- Right EEPROM: A0 **GPIO39 = 1**, 7-bit address **0x51**.
- The right connector is the same hardware mounted 180 degrees.

Connector GPIO pairs, written as left/right:

- H2: **GPIO53 / GPIO46**
- H4: **GPIO48 / GPIO47**
- H6: **GPIO13 / GPIO11**
- H8: **GPIO12 / GPIO10**
- H10 / EEPROM A0: **GPIO14 / GPIO39**
- H12 / button-board WS2812: **GPIO4 / GPIO5**
- Extended KEY1 / camera D0: **GPIO16 / GPIO40**
- Extended KEY2 / camera D1: **GPIO15 / GPIO38**
- Camera PCLK: **GPIO17 / GPIO37**
- Camera D6: **GPIO18 / GPIO54**
- Camera DE: **GPIO19 / GPIO52**
- Camera VSYNC: **GPIO55 / GPIO49**

Use `bsp_subboard_map_gpio()` instead of duplicating this left/right map in
application code.

## Existing subboard drivers

Matrix LED board used by `projects/gsp_sub_board`:

- 8×8 WS2812, 64 pixels, GRB, RMT at 10 MHz.
- DIN is H4: left **GPIO48**, right **GPIO47**.
- This is not the three-pixel button/LED board.

Button/LED board:

- Left: KEY1 **GPIO12**, KEY2 **GPIO15**, WS2812 **GPIO4**.
- Right: KEY1 **GPIO10**, KEY2 **GPIO38**, WS2812 **GPIO5**.
- Resolve these with `bsp_subboard_button_led_get_config()`.

Joystick board:

- Left X/Y: **GPIO48 / GPIO53**.
- Right X/Y: **GPIO47 / GPIO46**.
- Left buttons B/1/2/3/4: **GPIO16/4/15/12/13**.
- Right buttons B/1/2/3/4: **GPIO40/5/38/10/11**.
- B is active-high; buttons 1–4 are active-low.
- Resolve these with `bsp_subboard_joystick_get_config()`.

Camera board:

- Supported in the left slot only.
- D0–D7: **GPIO16, GPIO15, GPIO33, GPIO4, GPIO14, GPIO12, GPIO18, GPIO13**.
- VSYNC **GPIO55**, DE **GPIO19**, PCLK **GPIO17**.
- RESET **GPIO53**, PWDN **GPIO48**, flash **GPIO34** active-low.
- XCLK is supplied by the board's external 24 MHz oscillator.
- Camera D2 shares GPIO33 with USB Serial/JTAG. Acquiring the camera releases
  those pads; independent High-Speed USB remains available.
- Use `bsp_subboard_camera_acquire()` / `bsp_subboard_camera_release()` rather
  than configuring these pins directly.

## Portable implementation boundary

Keep business state, interactions, frame generation, color calculation, and
logical pixel mapping in portable `app/` code. Keep real hardware acquisition
and output in the ESP backend.

Use `#if !defined(ESP_PLATFORM)` only for simulation adapters. Do not fork or
reimplement normal business behavior for the host. Prefer one shared frame
generator with a small output interface:

- ESP backend: module discovery, camera driver, GPIO, RMT, `led_strip`.
- Host backend: virtual presence, browser camera, and DOM visualization.

## Subboard simulation contract

When the application involves subboard detection:

- Do not simulate EEPROM probing, I2C traffic, IDs, CRCs, descriptors, or other
  discovery details.
- Below the simulated device, list only the subboard types used by that
  application and only in their supported slots.
- A click inserts the selected subboard. Clicking the selected item again
  removes it.
- Insertion only changes virtual presence. The application’s portable business
  logic defines what insertion, removal, buttons, sensors, camera, or LEDs do.
- Preserve independent left/right state and removal while an operation is
  active. Do not invent generic interactions or automatic app behavior.

For a camera subboard:

- `getUserMedia()` may use the computer camera.
- Match the application’s real capture size, aspect ratio, crop rectangle,
  scaling algorithm, output size, clipping range, orientation, and frame
  lifetime. Do not use a convenient browser-only fit that changes the result.
- Stop all media tracks on removal, exit, permission failure, or a stale
  asynchronous permission result.
- Browser camera simulation does not replace real-device validation.

For LED subboards:

- Show separate left and right boards whenever the application supports both.
- The right board is mounted 180 degrees. Identical physical-index output on
  both sides therefore appears center-symmetric; preserve that result unless
  the application explicitly normalizes logical coordinates.
- Generate animation and RGB values once in shared portable code. Platform
  code should only send pixels or visualize them.
- Browser rendering must keep the unlit diffuser material visible and layer
  emitted color according to brightness; a zero value returns to the unlit
  material instead of painting the cell black.
- Compare color order, pixel order, orientation, cadence, and removal behavior
  against both boards on real hardware.

RMT without DMA can be interrupted while feeding a long WS2812 transaction.
An occasional shifted or corrupted partial region can result even when the
pixel buffer is correct. If this is observed, consider enabling
`led_strip_rmt_config_t.flags.with_dma`, then verify target support, memory
placement, power integrity, and timing before changing the animation logic.
