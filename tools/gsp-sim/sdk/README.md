# Host ESP-GSP SDK

`esp-gsp/` is **not** checked in. First preview downloads official
[`espressif/esp-gsp` 1.0.0](https://components.espressif.com/components/espressif/esp-gsp/versions/1.0.0)
from the Component Registry:

```sh
python3 tools/gsp-sim/fetch_gspc.py --sdk
```

That package includes `include/gsp/sim/esp_gsp_simulator.h` and
`prebuilt/sim/{wasm,linux-x86_64}`. Registry **1.1.0** excludes those files;
firmware still links `submodule/esp-gsp` 1.1.0.

Override the extract path with `ESP_GSP_SDK_ROOT`.
