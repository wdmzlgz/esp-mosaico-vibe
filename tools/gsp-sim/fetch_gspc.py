#!/usr/bin/env python3
"""Cache official ESP-GSP host tools and the Component Registry simulator SDK."""

from __future__ import annotations

import argparse
import hashlib
import os
import platform
import shutil
import sys
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
REPO_ROOT = TOOLS_DIR.parents[1]
GSP_ROOT = REPO_ROOT / "submodule" / "esp-gsp"
SDK_ROOT = TOOLS_DIR / "sdk" / "esp-gsp"
LICENSE_NAME = "THIRD_PARTY_LICENSES.txt"

# Official Component Registry package. 1.1.0 drops include/gsp/sim and
# prebuilt/sim from the archive; 1.0.0 still ships the session SDK.
ESP_GSP_SDK_VERSION = "1.0.0"
ESP_GSP_SDK_ZIP_URL = (
    "https://components-file.espressif.com/components/espressif/esp-gsp/"
    f"{ESP_GSP_SDK_VERSION}/espressif__esp-gsp-v{ESP_GSP_SDK_VERSION}.zip"
)
ESP_GSP_SDK_ZIP_SHA256 = (
    "31c32e1c93857e72e46476d662bcb217386aa1eca4d88c4de13b86371b283054"
)
SDK_MARKER_NAME = ".mosaico_origin"


def gspc_version() -> str:
    marker = GSP_ROOT / ".gspc_version"
    if marker.is_file():
        text = marker.read_text(encoding="utf-8").strip()
        if text:
            return text
    return "0.2.8"


def gsp_component_version() -> str:
    manifest = GSP_ROOT / "idf_component.yml"
    if manifest.is_file():
        for line in manifest.read_text(encoding="utf-8").splitlines():
            if line.startswith("version:"):
                return line.split(":", 1)[1].strip().strip("'\"")
    return "1.1.0"


def host_os_arch() -> tuple[str, str]:
    system = platform.system()
    machine = platform.machine().lower()
    if machine in {"amd64", "x86_64"}:
        arch = "x86_64"
    elif machine in {"aarch64", "arm64"}:
        arch = "aarch64"
    else:
        raise RuntimeError(f"unsupported host architecture: {machine}")
    if system == "Linux":
        return "linux", arch
    if system == "Darwin":
        return "macos", "universal2"
    if system == "Windows":
        return "windows", arch
    raise RuntimeError(f"unsupported GSP host: {system}/{machine}")


def archive_name(product: str, version: str) -> str:
    os_name, arch = host_os_arch()
    suffix = "zip" if os_name == "windows" else "tar.gz"
    return f"{product}-{version}-{os_name}-{arch}.{suffix}"


def default_cache_dir(kind: str, version: str) -> Path:
    env_by_kind = {
        "gspc": "GSPC_CACHE_DIR",
        "sim": "GSP_SIM_CACHE_DIR",
        "esp-gsp-sdk": "ESP_GSP_SDK_CACHE_DIR",
    }
    configured = os.environ.get(env_by_kind.get(kind, "GSP_SIM_CACHE_DIR"))
    if configured:
        path = Path(configured).expanduser()
        path.mkdir(parents=True, exist_ok=True)
        return path
    xdg = os.environ.get("XDG_CACHE_HOME")
    candidates = []
    if xdg:
        candidates.append(Path(xdg) / "esp-mosaico" / kind / version)
    candidates.append(Path.home() / ".cache" / "esp-mosaico" / kind / version)
    candidates.append(TOOLS_DIR / ".cache" / kind / version)
    for path in candidates:
        try:
            path.mkdir(parents=True, exist_ok=True)
            return path
        except OSError:
            continue
    raise RuntimeError(f"cannot create a {kind} cache directory")


def download(url: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file():
        return
    fd, temporary_name = tempfile.mkstemp(
        prefix=f".{destination.name}.",
        suffix=".download",
        dir=destination.parent,
    )
    temporary = Path(temporary_name)
    try:
        request = urllib.request.Request(
            url, headers={"User-Agent": "esp-mosaico-vibe-gsp/1"}
        )
        with os.fdopen(fd, "wb") as output:
            with urllib.request.urlopen(request, timeout=120) as response:
                while True:
                    chunk = response.read(1024 * 1024)
                    if not chunk:
                        break
                    output.write(chunk)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, destination)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise


def extract_binary(archive: Path, binary: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f"{binary}-extract-") as directory:
        staging = Path(directory)
        if archive.suffix == ".zip":
            with zipfile.ZipFile(archive) as bundle:
                bundle.extractall(staging)
        else:
            with tarfile.open(archive, "r:gz") as bundle:
                bundle.extractall(staging)
        matches = [path for path in staging.rglob(binary) if path.is_file()]
        if not matches:
            raise RuntimeError(f"{binary} missing from {archive.name}")
        shutil.copy2(matches[0], destination)
    if os.name != "nt":
        destination.chmod(0o755)


def resolve_release(
    *,
    product: str,
    version: str,
    env_var: str,
    cache_dir: Path | None = None,
) -> Path:
    configured = os.environ.get(env_var)
    if configured:
        executable = Path(configured).expanduser()
        if not executable.is_absolute():
            raise RuntimeError(f"{env_var} must be an absolute path")
        if not executable.is_file():
            raise RuntimeError(f"{env_var} is not a file: {executable}")
        return executable

    cache = cache_dir or default_cache_dir(product, version)
    binary = cache / product
    if binary.is_file():
        return binary
    name = archive_name(product, version)
    base = f"https://dl.espressif.com/AE/gsp/{product}/v{version}/"
    archive = cache / name
    download(base + name, archive)
    download(base + LICENSE_NAME, cache / LICENSE_NAME)
    extract_binary(archive, product, binary)
    return binary


def resolve_gspc(cache_dir: Path | None = None) -> Path:
    return resolve_release(
        product="gspc",
        version=gspc_version(),
        env_var="GSPC_EXECUTABLE",
        cache_dir=cache_dir,
    )


def resolve_sim(cache_dir: Path | None = None) -> Path:
    return resolve_release(
        product="sim",
        version=gsp_component_version(),
        env_var="GSP_SIM_EXECUTABLE",
        cache_dir=cache_dir,
    )


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def _sdk_version_string(root: Path) -> str | None:
    header = root / "include" / "gsp" / "gsp_version.h"
    if not header.is_file():
        return None
    for line in header.read_text(encoding="utf-8").splitlines():
        if "GSP_VERSION_STRING" in line and '"' in line:
            return line.split('"', 2)[1]
    return None


def sdk_ready(root: Path | None = None) -> bool:
    destination = root or SDK_ROOT
    required = (
        destination / "include" / "gsp" / "sim" / "esp_gsp_simulator.h",
        destination / "cmake" / "gsp_sim_prebuilt.cmake",
        destination / "prebuilt" / "sim" / "wasm" / "libgsp_portable_core.a",
        destination / "adapters" / "sdl" / "esp_gsp_sim_sdl.c",
    )
    if not all(path.is_file() for path in required):
        return False
    return _sdk_version_string(destination) == ESP_GSP_SDK_VERSION


def _write_sdk_marker(root: Path) -> None:
    (root / SDK_MARKER_NAME).write_text(
        "\n".join(
            (
                "namespace=espressif",
                "name=esp-gsp",
                f"version={ESP_GSP_SDK_VERSION}",
                f"url={ESP_GSP_SDK_ZIP_URL}",
                f"sha256={ESP_GSP_SDK_ZIP_SHA256}",
                "source=https://components.espressif.com/components/espressif/esp-gsp/versions/1.0.0",
                "",
            )
        ),
        encoding="utf-8",
    )


def resolve_sdk(destination: Path | None = None) -> Path:
    """Install official espressif/esp-gsp 1.0.0 for the WASM host session SDK."""
    configured = os.environ.get("ESP_GSP_SDK_ROOT")
    if configured:
        root = Path(configured).expanduser()
        if not root.is_absolute():
            raise RuntimeError("ESP_GSP_SDK_ROOT must be an absolute path")
        if not sdk_ready(root):
            raise RuntimeError(
                f"ESP_GSP_SDK_ROOT is not official espressif/esp-gsp "
                f"{ESP_GSP_SDK_VERSION} with simulator headers: {root}"
            )
        return root

    root = destination or SDK_ROOT
    if sdk_ready(root):
        marker = root / SDK_MARKER_NAME
        if not marker.is_file():
            _write_sdk_marker(root)
        return root

    cache = default_cache_dir("esp-gsp-sdk", ESP_GSP_SDK_VERSION)
    archive = cache / f"espressif__esp-gsp-v{ESP_GSP_SDK_VERSION}.zip"
    download(ESP_GSP_SDK_ZIP_URL, archive)
    digest = _sha256_file(archive)
    if digest != ESP_GSP_SDK_ZIP_SHA256:
        archive.unlink(missing_ok=True)
        raise RuntimeError(
            f"esp-gsp {ESP_GSP_SDK_VERSION} zip hash mismatch: {digest}"
        )

    with tempfile.TemporaryDirectory(prefix="esp-gsp-sdk-") as directory:
        staging = Path(directory) / "unpacked"
        staging.mkdir()
        with zipfile.ZipFile(archive) as bundle:
            bundle.extractall(staging)
        if not (staging / "idf_component.yml").is_file():
            nested = [path for path in staging.iterdir() if path.is_dir()]
            if len(nested) == 1 and (nested[0] / "idf_component.yml").is_file():
                staging = nested[0]
        if not sdk_ready(staging):
            raise RuntimeError(
                f"esp-gsp {ESP_GSP_SDK_VERSION} archive is missing "
                "include/gsp/sim or prebuilt/sim/wasm"
            )
        _write_sdk_marker(staging)
        root.parent.mkdir(parents=True, exist_ok=True)
        replacement = root.parent / f".{root.name}.new"
        if replacement.exists():
            shutil.rmtree(replacement)
        shutil.copytree(staging, replacement, symlinks=True)
        if root.exists():
            shutil.rmtree(root)
        replacement.rename(root)
    return root


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--sim", action="store_true", help="print the simulator path")
    parser.add_argument(
        "--sdk",
        action="store_true",
        help="install official espressif/esp-gsp 1.0.0 and print its path",
    )
    args = parser.parse_args()
    kind = "gspc"
    try:
        if args.sdk:
            kind = "sdk"
            path = resolve_sdk(args.output_dir)
        elif args.sim:
            kind = "sim"
            path = resolve_sim(args.output_dir)
        else:
            path = resolve_gspc(args.output_dir)
    except Exception as error:
        print(f"{kind}: {error}", file=sys.stderr)
        return 1
    print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
