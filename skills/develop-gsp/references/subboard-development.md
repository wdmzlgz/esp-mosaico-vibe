# ESP-Mosaico subboards

Read this reference when a GSP application discovers or drives a subboard.
Treat the BSP as the hardware source of truth:

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

Matrix LED board:

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

## Implementation and validation boundary

Keep business state, interactions, frame generation, color calculation, and
logical pixel mapping separate from hardware acquisition and output. Device
code owns module discovery, camera drivers, GPIO, RMT, and `led_strip`.

The scene-only simulator does not emulate EEPROM discovery, subboard presence,
camera input, GPIO, or LEDs. A `sim_bridge` backend may expose explicit
project-level adapters, but must not be treated as real hardware. Validate
detection, left/right slot mapping, hot removal, camera lifetime, and physical
output on the ESP target and real boards.

For LED boards, the right board is mounted 180 degrees. Identical
physical-index output on both sides therefore appears center-symmetric unless
the application explicitly normalizes logical coordinates. Compare color
order, pixel order, orientation, cadence, and removal behavior on both boards.

RMT without DMA can be interrupted while feeding a long WS2812 transaction.
An occasional shifted or corrupted partial region can result even when the
pixel buffer is correct. If this is observed, consider enabling
`led_strip_rmt_config_t.flags.with_dma`, then verify target support, memory
placement, power integrity, and timing before changing the animation logic.
