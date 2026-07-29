#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
FIRMWARE_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
REPO_DIR=$(cd "$FIRMWARE_DIR/.." && pwd)

TAG=""
REPO="skrcta/ssd_1963"
REMOTE="origin"
OUTPUT_DIR="$FIRMWARE_DIR/dist"
BUILD_DIR="$FIRMWARE_DIR/build-release"
EXPECTED_FLASH_SIZE="16MB"
BUILD=1
CHECK_ONLY=0
CREATE_TAG=0
PUSH_TAG=0
UPLOAD=0
PUBLISH=0
PRERELEASE=0
TITLE=""
NOTES_FILE=""

usage() {
    cat <<'EOF'
Usage:
  firmware/tools/release_firmware.sh --tag v0.1.0 [options]

Builds, validates, and packages the ER-TFT050 SSD1963 display-test firmware for
the Waveshare ESP32-S3-Pico. By default it creates a local ZIP only.

Options:
  --tag TAG                 Release tag, for example v0.1.0. Required.
  --repo OWNER/REPO         GitHub repository for upload. Default: skrcta/ssd_1963.
  --remote NAME             Git remote used for tag push/check. Default: origin.
  --output-dir DIR          Package output directory. Default: firmware/dist.
  --build-dir DIR           ESP-IDF build directory. Default: firmware/build-release.
  --expected-flash-size N   Required ESP-IDF flash size. Default: 16MB.
  --skip-build              Package an existing build directory.
  --check                   Build/package/verify only; never tag, push, or upload.
  --create-tag              Create an annotated local git tag after validation.
  --push-tag                Push the release tag to the selected remote after validation.
  --upload                  Upload the ZIP to a GitHub release with gh.
  --publish                 When creating a release, publish it instead of draft.
  --prerelease              Mark a newly created release as prerelease.
  --title TITLE             Release title. Default is generated from TAG.
  --notes-file FILE         Release notes file for newly created releases.
  -h, --help                Show this help.

Examples:
  firmware/tools/release_firmware.sh --tag v0.1.0 --check
  firmware/tools/release_firmware.sh --tag v0.1.0
  firmware/tools/release_firmware.sh --tag v0.1.0 --create-tag --push-tag --upload
  firmware/tools/release_firmware.sh --tag v0.1.0 --create-tag --push-tag --upload --publish
EOF
}

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

info() {
    printf '%s\n' "$*"
}

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

tag_exists() {
    git -C "$REPO_DIR" rev-parse -q --verify "refs/tags/$TAG" >/dev/null
}

tag_commit() {
    git -C "$REPO_DIR" rev-list -n 1 "$TAG"
}

head_commit() {
    git -C "$REPO_DIR" rev-parse HEAD
}

require_clean_tree() {
    local status

    status=$(git -C "$REPO_DIR" status --porcelain)
    if [ -n "$status" ]; then
        printf '%s\n' "$status" >&2
        die "release tagging/upload requires a clean ssd_1963 working tree"
    fi
}

require_tag_at_head() {
    tag_exists || die "tag $TAG does not exist; use --create-tag or create it manually"

    local tag_rev
    local head_rev
    tag_rev=$(tag_commit)
    head_rev=$(head_commit)

    if [ "$tag_rev" != "$head_rev" ]; then
        die "tag $TAG points to $tag_rev, but HEAD is $head_rev"
    fi
}

remote_tag_exists() {
    git -C "$REPO_DIR" ls-remote --exit-code --tags "$REMOTE" "refs/tags/$TAG" >/dev/null 2>&1
}

release_title() {
    if [ -n "$TITLE" ]; then
        printf '%s\n' "$TITLE"
    else
        printf 'ESP32-S3-Pico display test firmware %s\n' "$TAG"
    fi
}

source_revision() {
    if tag_exists && [ "$(tag_commit)" = "$(head_commit)" ]; then
        printf '%s\n' "$TAG"
    else
        git -C "$REPO_DIR" describe --always --dirty
    fi
}

validate_inputs() {
    [ -n "$TAG" ] || die "--tag is required"

    if [[ ! "$TAG" =~ ^v[0-9]+[.][0-9]+[.][0-9]+([-+][0-9A-Za-z.-]+)?$ ]]; then
        die "--tag should look like v0.1.0"
    fi

    if [[ ! "$REPO" =~ ^[0-9A-Za-z_.-]+/[0-9A-Za-z_.-]+$ ]]; then
        die "--repo should look like OWNER/REPO"
    fi

    if [ "$CHECK_ONLY" -eq 1 ] && {
        [ "$CREATE_TAG" -eq 1 ] || [ "$PUSH_TAG" -eq 1 ] || [ "$UPLOAD" -eq 1 ]
    }; then
        die "--check cannot be combined with --create-tag, --push-tag, or --upload"
    fi

    if [ "$PUBLISH" -eq 1 ] && [ "$UPLOAD" -eq 0 ]; then
        die "--publish requires --upload"
    fi

    if [ "$PRERELEASE" -eq 1 ] && [ "$UPLOAD" -eq 0 ]; then
        die "--prerelease requires --upload"
    fi

    if [ -n "$NOTES_FILE" ] && [ ! -f "$NOTES_FILE" ]; then
        die "release notes file does not exist: $NOTES_FILE"
    fi
}

check_tools() {
    need_cmd git
    need_cmd python3
    need_cmd sha256sum
    need_cmd zip

    if [ "$BUILD" -eq 1 ]; then
        need_cmd idf.py
    fi

    if [ "$UPLOAD" -eq 1 ]; then
        need_cmd gh
    fi
}

remote_repo_slug() {
    local remote_url

    remote_url=$(git -C "$REPO_DIR" remote get-url --push "$REMOTE")
    python3 - "$remote_url" <<'PY'
import re
import sys
from urllib.parse import urlparse

url = sys.argv[1]

if "://" in url:
    parsed = urlparse(url)
    path = parsed.path.lstrip("/")
else:
    match = re.match(r"[^@]+@[^:]+:(.+)$", url)
    path = match.group(1) if match else url

path = path.strip("/")
if path.endswith(".git"):
    path = path[:-4]

parts = path.split("/")
if len(parts) < 2:
    raise SystemExit(1)

print("/".join(parts[-2:]))
PY
}

require_remote_matches_repo() {
    local remote_slug
    local repo_lower
    local remote_lower

    remote_slug=$(remote_repo_slug) || die "could not derive OWNER/REPO from remote $REMOTE"
    repo_lower=$(printf '%s' "$REPO" | tr '[:upper:]' '[:lower:]')
    remote_lower=$(printf '%s' "$remote_slug" | tr '[:upper:]' '[:lower:]')

    if [ "$remote_lower" != "$repo_lower" ]; then
        die "remote $REMOTE points to $remote_slug, but --repo is $REPO"
    fi
}

prepare_paths() {
    PROJECT_NAME="ssd1963-er-tft050-esp32s3-pico-$TAG"
    mkdir -p "$OUTPUT_DIR"
    OUTPUT_DIR=$(cd "$OUTPUT_DIR" && pwd)
    STAGE_DIR="$OUTPUT_DIR/$PROJECT_NAME"
    ZIP_PATH="$OUTPUT_DIR/$PROJECT_NAME.zip"
}

build_firmware() {
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
}

stage_package() {
    [ -f "$BUILD_DIR/flasher_args.json" ] || die "missing build/flasher_args.json; run idf.py build first"

    python3 - "$BUILD_DIR" "$STAGE_DIR" "$EXPECTED_FLASH_SIZE" "$TAG" "$(source_revision)" <<'PY'
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

python3 -m esptool \\
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

py -m esptool ^
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
}

write_hashes() {
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
}

build_zip() {
    rm -f "$ZIP_PATH"
    (
        cd "$OUTPUT_DIR"
        zip -qr "$(basename "$ZIP_PATH")" "$PROJECT_NAME"
        zip -T "$(basename "$ZIP_PATH")" >/dev/null
    )
}

verify_package() {
    (
        cd "$STAGE_DIR"
        sha256sum -c SHA256SUMS.txt
    )

    python3 - "$STAGE_DIR" "$EXPECTED_FLASH_SIZE" <<'PY'
import json
import sys
from pathlib import Path

stage_dir = Path(sys.argv[1])
expected_flash_size = sys.argv[2]
expected = {
    "bootloader.bin",
    "partition-table.bin",
    "er_tft050_display_test.bin",
    "flash_args.txt",
    "flasher_args.json",
    "flash_linux.sh",
    "flash_windows.cmd",
    "README.md",
    "SHA256SUMS.txt",
}

actual = {path.name for path in stage_dir.iterdir() if path.is_file()}
if actual != expected:
    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    raise SystemExit(f"error: package contents mismatch; missing={missing}, extra={extra}")

with (stage_dir / "flasher_args.json").open("r", encoding="utf-8") as f:
    flasher = json.load(f)

if flasher.get("extra_esptool_args", {}).get("chip") != "esp32s3":
    raise SystemExit("error: flasher_args.json does not target esp32s3")

flash_size = flasher.get("flash_settings", {}).get("flash_size")
if flash_size != expected_flash_size:
    raise SystemExit(
        f"error: flasher_args.json uses flash size {flash_size!r}, "
        f"expected {expected_flash_size!r}"
    )
PY
}

create_tag() {
    if tag_exists; then
        require_tag_at_head
        info "Tag $TAG already exists at HEAD"
        return
    fi

    git -C "$REPO_DIR" tag -a "$TAG" -m "SSD1963 ESP32-S3-Pico firmware $TAG"
    info "Created tag $TAG"
}

push_tag() {
    require_tag_at_head
    git -C "$REPO_DIR" push "$REMOTE" "$TAG"
}

upload_release() {
    require_tag_at_head

    if ! remote_tag_exists; then
        die "remote tag $TAG does not exist on $REMOTE; use --push-tag first"
    fi

    RELEASE_TITLE=$(release_title)
    RELEASE_ARGS=(--repo "$REPO" --title "$RELEASE_TITLE")
    RELEASE_EDIT_ARGS=(--repo "$REPO" --title "$RELEASE_TITLE")

    if [ "$PUBLISH" -eq 0 ]; then
        RELEASE_ARGS+=(--draft)
    else
        RELEASE_EDIT_ARGS+=(--draft=false)
    fi
    if [ "$PRERELEASE" -eq 1 ]; then
        RELEASE_ARGS+=(--prerelease)
        RELEASE_EDIT_ARGS+=(--prerelease)
    fi
    if [ -n "$NOTES_FILE" ]; then
        RELEASE_ARGS+=(--notes-file "$NOTES_FILE")
        RELEASE_EDIT_ARGS+=(--notes-file "$NOTES_FILE")
    else
        RELEASE_NOTES="Prebuilt ER-TFT050-6-5654 SSD1963 display-test firmware for Waveshare ESP32-S3-Pico. Hardware validation is still pending."
        RELEASE_ARGS+=(--notes "$RELEASE_NOTES")
        RELEASE_EDIT_ARGS+=(--notes "$RELEASE_NOTES")
    fi

    if gh release view "$TAG" --repo "$REPO" >/dev/null 2>&1; then
        info "Updating existing release metadata for $TAG in $REPO"
        gh release edit "$TAG" "${RELEASE_EDIT_ARGS[@]}"
        info "Replacing existing release asset $(basename "$ZIP_PATH") for $TAG"
        gh release upload "$TAG" "$ZIP_PATH" --repo "$REPO" --clobber
    else
        gh release create "$TAG" "$ZIP_PATH" "${RELEASE_ARGS[@]}"
    fi
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
        --remote)
            [ "$#" -ge 2 ] || die "--remote requires a value"
            REMOTE=$2
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
        --check)
            CHECK_ONLY=1
            shift
            ;;
        --create-tag)
            CREATE_TAG=1
            shift
            ;;
        --push-tag)
            PUSH_TAG=1
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

validate_inputs
check_tools
prepare_paths

if [ "$CREATE_TAG" -eq 1 ] || [ "$PUSH_TAG" -eq 1 ] || [ "$UPLOAD" -eq 1 ]; then
    require_clean_tree
    require_remote_matches_repo
fi

build_firmware
stage_package
write_hashes
build_zip
verify_package

info "Created $ZIP_PATH"

if [ "$CHECK_ONLY" -eq 1 ]; then
    info "Check complete; no tag, push, or release upload performed"
    exit 0
fi

if [ "$CREATE_TAG" -eq 1 ]; then
    create_tag
    # Repack so the package README records the annotated tag instead of a raw
    # commit description. The binaries are not rebuilt here.
    stage_package
    write_hashes
    build_zip
    verify_package
    info "Repacked $ZIP_PATH with source revision $(source_revision)"
fi

if [ "$PUSH_TAG" -eq 1 ]; then
    push_tag
fi

if [ "$UPLOAD" -eq 1 ]; then
    upload_release
fi
