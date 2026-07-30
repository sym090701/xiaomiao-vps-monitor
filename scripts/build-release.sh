#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUTPUT_DIR="${1:-$ROOT_DIR/dist}"
PIO="${PIO:-pio}"
PYTHON="${PYTHON:-python3}"
CORE_DIR="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
ESPTOOL="${ESPTOOL:-$CORE_DIR/packages/tool-esptoolpy/esptool.py}"
BUILD_DIR="$ROOT_DIR/.pio/build/xiaomiao"
BOOT_APP0="$CORE_DIR/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
VERSION="${VERSION:-1.2.0}"

if [[ ! -f "$ESPTOOL" ]]; then
    echo "esptool.py not found: $ESPTOOL" >&2
    echo "Set ESPTOOL to PlatformIO's tool-esptoolpy/esptool.py path." >&2
    exit 1
fi
if [[ ! -f "$BOOT_APP0" ]]; then
    echo "boot_app0.bin not found: $BOOT_APP0" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
cd "$ROOT_DIR"
"$PIO" run

cp "$BUILD_DIR/firmware.bin" "$OUTPUT_DIR/XiaoMiao-VPS-Monitor-v${VERSION}-Launcher.bin"
"$PYTHON" "$ESPTOOL" --chip esp32 merge_bin \
    --output "$OUTPUT_DIR/XiaoMiao-VPS-Monitor-v${VERSION}-Full-4MB.bin" \
    --flash_mode qio --flash_freq 40m --flash_size 4MB --fill-flash-size 4MB \
    0x1000 "$BUILD_DIR/bootloader.bin" \
    0x8000 "$BUILD_DIR/partitions.bin" \
    0xe000 "$BOOT_APP0" \
    0x10000 "$BUILD_DIR/firmware.bin"

cd "$OUTPUT_DIR"
if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "XiaoMiao-VPS-Monitor-v${VERSION}-Launcher.bin" \
              "XiaoMiao-VPS-Monitor-v${VERSION}-Full-4MB.bin" > SHA256SUMS
else
    shasum -a 256 "XiaoMiao-VPS-Monitor-v${VERSION}-Launcher.bin" \
                  "XiaoMiao-VPS-Monitor-v${VERSION}-Full-4MB.bin" > SHA256SUMS
fi

echo "Release binaries written to $OUTPUT_DIR"
