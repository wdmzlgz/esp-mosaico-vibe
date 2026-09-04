#!/usr/bin/env bash
# Build the application WASM host (portable C + GSP session).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOOLS_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${GSP_APP_WASM_BUILD_DIR:-$TOOLS_DIR/build-web}"
APP_DIR="${GSP_APP_DIR:-}"
BUNDLE="${GSP_APP_BUNDLE:-$BUILD_DIR/scene/preview.gspb}"

if [[ -n "${MOSAIC_EMSDK_ENV:-}" ]]; then
    EMSDK_ENV="$MOSAIC_EMSDK_ENV"
elif command -v emcmake >/dev/null 2>&1; then
    EMSDK_ENV=""
else
    EMSDK_ENV=""
    for candidate in \
        "${HOME}/emsdk/emsdk_env.sh" \
        "/tmp/esp_claw_emsdk/emsdk_env.sh"
    do
        if [[ -f "$candidate" ]]; then
            EMSDK_ENV="$candidate"
            break
        fi
    done
    if [[ -z "$EMSDK_ENV" ]]; then
        echo "build_wasm.sh: emcmake was not found." >&2
        echo "Install Emscripten to ~/emsdk or set MOSAIC_EMSDK_ENV." >&2
        exit 1
    fi
fi
if [[ -n "$EMSDK_ENV" ]]; then
    # shellcheck disable=SC1090
    source "$EMSDK_ENV" >/dev/null
fi

if [[ ! -f "$BUNDLE" ]]; then
    echo "build_wasm.sh: packed scene is missing: $BUNDLE" >&2
    exit 1
fi

if [[ -z "${ESP_GSP_SDK_ROOT:-}" ]]; then
    ESP_GSP_SDK_ROOT="$(python3 "$TOOLS_DIR/fetch_gspc.py" --sdk)"
    export ESP_GSP_SDK_ROOT
fi

CMAKE_ARGS=("-DGSP_APP_BUNDLE=$BUNDLE")
if [[ -n "$APP_DIR" ]]; then
    CMAKE_ARGS+=("-DGSP_APP_DIR=$APP_DIR")
fi

emcmake cmake -S "$TOOLS_DIR/host" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DESP_GSP_ROOT="$ESP_GSP_SDK_ROOT" \
    "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" --parallel "${MOSAIC_SIM_JOBS:-4}"

echo
echo "Web preview package created in: $BUILD_DIR"
echo "Open gsp_app_sim.html from that directory."
