#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
FIRMWARE_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
REPO_DIR=$(cd "$FIRMWARE_DIR/.." && pwd)

TAG=""
REPO="skrcta/ssd_1963"
OUTPUT_DIR="$FIRMWARE_DIR/dist"
BUILD_DIR="$FIRMWARE_DIR/build-release"
EXPECTED_FLASH_SIZE="16MB"
BUILD=1
UPLOAD=0
PUBLISH=0
PRERELEASE=0
TITLE=""
NOTES_FILE=""

usage() {
    cat <<'EOF'
Usage:
  firmware/tools/release_firmware.sh --tag v0.1.0 [options]

Builds and packages the ER-TFT050 SSD1963 display-test firmware for the
Waveshare ESP32-S3-Pico. By default it creates a local ZIP only.

Options:
  --tag TAG                 Release tag, for example v0.1.0. Required.
  --repo OWNER/REPO         GitHub repository for upload. Default: skrcta/ssd_1963.
  --output-dir DIR          Package output directory. Default: firmware/dist.
  --build-dir DIR           ESP-IDF build directory. Default: firmware/build-release.
  --expected-flash-size N   Required ESP-IDF flash size. Default: 16MB.
  --skip-build              Package an existing build directory.
  --upload                  Upload the ZIP to a GitHub release with gh.
  --publish                 When creating a release, publish it instead of draft.
  --prerelease              Mark a newly created release as prerelease.
  --title TITLE             Release title. Default is generated from TAG.
  --notes-file FILE         Release notes file for newly created releases.
  -h, --help                Show this help.

Examples:
  firmware/tools/release_firmware.sh --tag v0.1.0
  firmware/tools/release_firmware.sh --tag v0.1.0 --upload
  firmware/tools/release_firmware.sh --tag v0.1.0 --upload --publish
EOF
}

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --tag)
            [ "$#" -ge 2 ] || die "--tag requires a value"
            TAG=$2
            shift 2
            ;;
        --repo)
            [ "$#" -ge 2 ] || die "--repo requires a value"
            REPO=$2
            shift 2
            ;;
        --output-dir)
            [ "$#" -ge 2 ] || die "--output-dir requires a value"
            OUTPUT_DIR=$2
            shift 2
            ;;
        --build-dir)
            [ "$#" -ge 2 ] || die "--build-dir requires a value"
            BUILD_DIR=$2
            shift 2
            ;;
        --expected-flash-size)
            [ "$#" -ge 2 ] || die "--expected-flash-size requires a value"
            EXPECTED_FLASH_SIZE=$2
            shift 2
            ;;
        --skip-build)
            BUILD=0
            shift
            ;;
        --upload)
            UPLOAD=1
            shift
            ;;
        --publish)
            PUBLISH=1
            shift
            ;;
        --prerelease)
            PRERELEASE=1
            shift
            ;;
        --title)
            [ "$#" -ge 2 ] || die "--title requires a value"
            TITLE=$2
            shift 2
            ;;
        --notes-file)
            [ "$#" -ge 2 ] || die "--notes-file requires a value"
            NOTES_FILE=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1"
            ;;
    esac
done

[ -n "$TAG" ] || die "--tag is required"
case "$TAG" in
    v[0-9]*)
        ;;
    *)
        die "--tag should look like v0.1.0"
        ;;
esac

need_cmd python3
need_cmd sha256sum
need_cmd zip
need_cmd git

if [ "$BUILD" -eq 1 ]; then
    need_cmd idf.py
fi

if [ "$UPLOAD" -eq 1 ]; then
    need_cmd gh
fi

if [ -n "$NOTES_FILE" ] && [ ! -f "$NOTES_FILE" ]; then
    die "release notes file does not exist: $NOTES_FILE"
fi

PROJECT_NAME="ssd1963-er-tft050-esp32s3-pico-$TAG"
SOURCE_REV=$(git -C "$REPO_DIR" describe --always --dirty)
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR=$(cd "$OUTPUT_DIR" && pwd)
STAGE_DIR="$OUTPUT_DIR/$PROJECT_NAME"
ZIP_PATH="$OUTPUT_DIR/$PROJECT_NAME.zip"

if [ "$BUILD" -eq 1 ]; then
    mkdir -p "$BUILD_DIR"
    BUILD_DIR=$(cd "$BUILD_DIR" && pwd)
    (
        cd "$FIRMWARE_DIR"
        idf.py \
            -B "$BUILD_DIR" \
            -DSDKCONFIG="$BUILD_DIR/sdkconfig" \
            -DSDKCONFIG_DEFAULTS="$FIRMWARE_DIR/sdkconfig.defaults" \
            -DIDF_TARGET=esp32s3 \
            build
    )
else
    [ -d "$BUILD_DIR" ] || die "build directory does not exist: $BUILD_DIR"
    BUILD_DIR=$(cd "$BUILD_DIR" && pwd)
fi

[ -f "$BUILD_DIR/flasher_args.json" ] || die "missing build/flasher_args.json; run idf.py build first"

python3 - "$BUILD_DIR" "$STAGE_DIR" "$EXPECTED_FLASH_SIZE" "$TAG" "$SOURCE_REV" <<'PY'
import json
import os
import shutil
import sys
from datetime import datetime, timezone
from pathlib import Path

build_dir = Path(sys.argv[1])
stage_dir = Path(sys.argv[2])
expected_flash_size = sys.argv[3]
tag = sys.argv[4]
source_rev = sys.argv[5]

with (build_dir / "flasher_args.json").open("r", encoding="utf-8") as f:
    flasher = json.load(f)

chip = flasher.get("extra_esptool_args", {}).get("chip")
if chip != "esp32s3":
    raise SystemExit(f"error: expected chip esp32s3, got {chip!r}")

flash_size = flasher.get("flash_settings", {}).get("flash_size")
if flash_size != expected_flash_size:
    raise SystemExit(
        f"error: expected flash size {expected_flash_size}, got {flash_size!r}; "
        "check Serial flasher config -> Flash size and rebuild"
    )

flash_files = flasher.get("flash_files", {})
required_offsets = {
    "0x0": "bootloader.bin",
    "0x8000": "partition-table.bin",
    "0x10000": "er_tft050_display_test.bin",
}

missing = [offset for offset in required_offsets if offset not in flash_files]
if missing:
    raise SystemExit(f"error: missing flash offsets in flasher_args.json: {', '.join(missing)}")

if stage_dir.exists():
    shutil.rmtree(stage_dir)
stage_dir.mkdir(parents=True)

for offset, dest_name in required_offsets.items():
    src = build_dir / flash_files[offset]
    if not src.is_file():
        raise SystemExit(f"error: missing build artifact for {offset}: {src}")
    shutil.copy2(src, stage_dir / dest_name)

shutil.copy2(build_dir / "flasher_args.json", stage_dir / "flasher_args.json")

write_args = " ".join(flasher.get("write_flash_args", []))
flash_settings = flasher.get("flash_settings", {})
flash_mode = flash_settings.get("flash_mode", "dio")
flash_freq = flash_settings.get("flash_freq", "80m")
with (stage_dir / "flash_args.txt").open("w", encoding="utf-8") as f:
    if write_args:
        f.write(f"{write_args}\n")
    for offset, dest_name in required_offsets.items():
        f.write(f"{offset} {dest_name}\n")

with (stage_dir / "flash_linux.sh").open("w", encoding="utf-8") as f:
    f.write(f"""#!/usr/bin/env sh
set -eu

PORT=${{1:-/dev/ttyACM0}}

esptool.py \\
  --chip esp32s3 \\
  --port "$PORT" \\
  --baud 921600 \\
  write_flash \\
  --flash_mode {flash_mode} \\
  --flash_freq {flash_freq} \\
  --flash_size {expected_flash_size} \\
  0x0 bootloader.bin \\
  0x8000 partition-table.bin \\
  0x10000 er_tft050_display_test.bin
""")

with (stage_dir / "flash_windows.cmd").open("w", encoding="utf-8", newline="\r\n") as f:
    f.write(f"""@echo off
set PORT=%1
if "%PORT%"=="" set PORT=COM5

esptool.py ^
  --chip esp32s3 ^
  --port %PORT% ^
  --baud 921600 ^
  write_flash ^
  --flash_mode {flash_mode} ^
  --flash_freq {flash_freq} ^
  --flash_size {expected_flash_size} ^
  0x0 bootloader.bin ^
  0x8000 partition-table.bin ^
  0x10000 er_tft050_display_test.bin
""")

readme = f"""# SSD1963 ER-TFT050 firmware {tag}

Prebuilt display-test firmware for the ER-TFT050-6-5654 SSD1963 display on the
Waveshare ESP32-S3-Pico.

This package targets:

- chip: ESP32-S3
- board: Waveshare ESP32-S3-Pico
- flash size: {expected_flash_size}
- app: er_tft050_display_test.bin

Flash examples:

Windows:

```cmd
flash_windows.cmd COM5
```

Linux/macOS:

```sh
chmod +x flash_linux.sh
./flash_linux.sh /dev/ttyACM0
```

Manual flash offsets:

- 0x0 bootloader.bin
- 0x8000 partition-table.bin
- 0x10000 er_tft050_display_test.bin

This firmware is still a display bring-up diagnostic. Confirm colour order,
timing, latch edge, and backlight polarity on real hardware before treating the
driver as proven.

Source revision: {source_rev}
Packaged at: {datetime.now(timezone.utc).isoformat(timespec="seconds")}
"""
with (stage_dir / "README.md").open("w", encoding="utf-8") as f:
    f.write(readme)

os.chmod(stage_dir / "flash_linux.sh", 0o755)
PY

(
    cd "$STAGE_DIR"
    sha256sum \
        bootloader.bin \
        partition-table.bin \
        er_tft050_display_test.bin \
        flash_args.txt \
        flasher_args.json \
        flash_linux.sh \
        flash_windows.cmd \
        README.md > SHA256SUMS.txt
)

rm -f "$ZIP_PATH"
(
    cd "$OUTPUT_DIR"
    zip -qr "$(basename "$ZIP_PATH")" "$PROJECT_NAME"
)

printf 'Created %s\n' "$ZIP_PATH"

if [ "$UPLOAD" -eq 1 ]; then
    RELEASE_TITLE=${TITLE:-"ESP32-S3-Pico display test firmware $TAG"}
    RELEASE_ARGS=(--repo "$REPO" --title "$RELEASE_TITLE")

    if [ "$PUBLISH" -eq 0 ]; then
        RELEASE_ARGS+=(--draft)
    fi
    if [ "$PRERELEASE" -eq 1 ]; then
        RELEASE_ARGS+=(--prerelease)
    fi
    if [ -n "$NOTES_FILE" ]; then
        RELEASE_ARGS+=(--notes-file "$NOTES_FILE")
    else
        RELEASE_ARGS+=(--notes "Prebuilt ER-TFT050-6-5654 SSD1963 display-test firmware for Waveshare ESP32-S3-Pico. Hardware validation is still pending.")
    fi

    (
        cd "$REPO_DIR"
        if gh release view "$TAG" --repo "$REPO" >/dev/null 2>&1; then
            gh release upload "$TAG" "$ZIP_PATH" --repo "$REPO" --clobber
        else
            gh release create "$TAG" "$ZIP_PATH" "${RELEASE_ARGS[@]}"
        fi
    )
fi
