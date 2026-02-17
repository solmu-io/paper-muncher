#!/bin/bash
#
# build_solpdf.sh — Build libsolpdf.so from paper-muncher
#
# Usage:
#   ./build_solpdf.sh [--output DIR] [--pm-dir DIR]
#
# Requirements:
#   - clang++-21
#   - ninja
#   - System libraries: fontconfig, freetype, zlib, libpng, bzip2, libjpeg, liburing, libseccomp
#   - CuteKit (./ck) in the paper-muncher repo
#

set -euo pipefail

# ─── Defaults ────────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PM_DIR=""
OUTPUT_DIR=""
SO_NAME="libsolpdf.so"

# Fixed temp directory (cleaned up on exit)
TMP_DIR="/tmp/solpdf_build_$$"
mkdir -p "$TMP_DIR"
trap "rm -rf $TMP_DIR" EXIT

# ─── Parse arguments ────────────────────────────────────────────────────────

usage() {
    echo "Usage: $0 [--output DIR] [--pm-dir DIR]"
    echo ""
    echo "  --pm-dir DIR    Path to paper-muncher repo (default: auto-detect)"
    echo "  --output DIR    Where to place the .so and bundle (default: ./dist)"
    echo ""
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --pm-dir)   PM_DIR="$2"; shift 2 ;;
        --output)   OUTPUT_DIR="$2"; shift 2 ;;
        -h|--help)  usage ;;
        *)          echo "Unknown option: $1"; usage ;;
    esac
done

# Auto-detect paper-muncher directory
if [[ -z "$PM_DIR" ]]; then
    if [[ -f "./ck" ]]; then
        PM_DIR="$(pwd)"
    else
        echo "ERROR: Cannot find paper-muncher repo. Use --pm-dir or run from the repo root."
        exit 1
    fi
fi

PM_DIR="$(cd "$PM_DIR" && pwd)"
OUTPUT_DIR="${OUTPUT_DIR:-$SCRIPT_DIR/dist}"

# ─── Step 0: Fresh clone for reproducible builds ────────────────────────────

echo "▶ Step 0: Creating fresh shallow clone for reproducible build..."

# Get the remote URL and current branch from the existing repo
if [[ -d "$PM_DIR/.git" ]]; then
    PM_REMOTE=$(git -C "$PM_DIR" remote get-url origin)
    PM_BRANCH=$(git -C "$PM_DIR" rev-parse --abbrev-ref HEAD)
else
    echo "ERROR: $PM_DIR is not a git repository."
    exit 1
fi

CLONE_DIR="$TMP_DIR/paper-muncher"
git clone --depth 1 --branch "$PM_BRANCH" "$PM_REMOTE" "$CLONE_DIR"

# Check for uncommitted local changes and warn
if [[ -n "$(git -C "$PM_DIR" status --porcelain)" ]]; then
    echo "  ⚠ WARNING: Local repo has uncommitted changes that won't be in this build."
    echo "  Commit and push before building for a fully reproducible build."
fi

# Use the fresh clone from now on
PM_DIR="$CLONE_DIR"
cd "$PM_DIR"

echo "  Cloned $PM_REMOTE ($PM_BRANCH) into $CLONE_DIR"

# ─── Validate prerequisites ─────────────────────────────────────────────────

echo "═══════════════════════════════════════════════════════════════"
echo "  Building $SO_NAME"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "  paper-muncher: $PM_DIR"
echo "  output:        $OUTPUT_DIR"
echo ""

check_cmd() {
    if ! command -v "$1" &>/dev/null; then
        echo "ERROR: '$1' not found. Please install it."
        exit 1
    fi
}

check_cmd clang++-21
check_cmd ninja
check_cmd nm

cd "$PM_DIR"

# ─── Step 1: Build via CuteKit ──────────────────────────────────────────────

echo "▶ Step 1: Building paper-muncher-lib via CuteKit..."

./ck build paper-muncher-lib

# ─── Step 2: Find the build directory ────────────────────────────────────────

BUILD_DIR=$(find .cutekit/build/ -maxdepth 1 -name "host-*" -type d | head -1)

if [[ -z "$BUILD_DIR" ]]; then
    echo "ERROR: No build directory found in .cutekit/build/"
    exit 1
fi

echo "  Build dir: $BUILD_DIR"

# ─── Step 3: Inject -fPIC and rebuild ────────────────────────────────────────

echo "▶ Step 2: Injecting -fPIC and rebuilding all objects..."

NINJA_FILE="$BUILD_DIR/build.ninja"

if grep -q "^cxxflags = -std=gnu++2c -fPIC" "$NINJA_FILE"; then
    echo "  -fPIC already present."
else
    sed -i 's/cxxflags = -std=gnu++2c/cxxflags = -std=gnu++2c -fPIC/' "$NINJA_FILE"
    echo "  Injected -fPIC into cxxflags."
fi

ninja -f "$NINJA_FILE" -t clean
echo "  Cleaned previous objects."

ninja -f "$NINJA_FILE"
echo "  Rebuilt all objects with -fPIC."

# ─── Step 4: Filter object files ────────────────────────────────────────────

echo "▶ Step 3: Selecting library object files..."

find "$BUILD_DIR/" -name "*.o" \
    | grep -E "(paper-muncher-lib|vaev-engine|vaev-script|\
karm-sys[/.]|karm-sys\.posix|karm-sys\.uring|karm-sys\.seccomp|\
karm-core|karm-math|karm-gfx|karm-font[/.]|karm-font\.ttf|\
karm-font\.fontconfig|karm-http|karm-print|karm-scene|\
karm-image[/.]|karm-image\.|karm-ref|karm-gc|karm-logger|\
karm-debug|karm-archive|karm-md|karm-crypto|\
ce-libc|ce-libm|ce-stdcpp|ce-bootfs[/.]|fonts-|\
karm-pdf|karm-tty)" \
    | grep -v "\.main/" \
    | grep -v "\.cli/" \
    | grep -v "\.benchs/" \
    | grep -v "karm-font/__obj__/ttf/fontface" \
    | grep -v "karm-vte" \
    | grep -v "/tests/" \
    | grep -v "test-" \
    > "$TMP_DIR/objects.txt"

OBJ_COUNT=$(wc -l < "$TMP_DIR/objects.txt")
echo "  Selected $OBJ_COUNT object files."

if [[ "$OBJ_COUNT" -eq 0 ]]; then
    echo "ERROR: No object files matched the filter."
    exit 1
fi

# ─── Step 5: Compute build digest ───────────────────────────────────────────

echo "▶ Step 4: Computing build digest..."

PM_SHA=$(git -C "$PM_DIR" rev-parse HEAD)
echo "  paper-muncher: $PM_SHA"

KARM_DIR="$PM_DIR/.cutekit/extern/skift/karm"
if [[ -d "$KARM_DIR/.git" ]] || [[ -f "$KARM_DIR/.git" ]]; then
    KARM_SHA=$(git -C "$KARM_DIR" rev-parse HEAD)
else
    SKIFT_DIR="$PM_DIR/.cutekit/extern/skift"
    if [[ -d "$SKIFT_DIR/.git" ]] || [[ -f "$SKIFT_DIR/.git" ]]; then
        KARM_SHA=$(git -C "$SKIFT_DIR" rev-parse HEAD)
    else
        echo "WARNING: Cannot determine karm commit SHA. Using 'unknown'."
        KARM_SHA="unknown"
    fi
fi
echo "  karm/skift:    $KARM_SHA"

BUILD_DIGEST=$(echo -n "${PM_SHA}${KARM_SHA}" | sha256sum | cut -d' ' -f1)
echo "  Build digest:  ${BUILD_DIGEST:0:16}..."

# ─── Step 6: Rebuild API object with digest ──────────────────────────────────

echo "▶ Step 5: Rebuilding API object with baked-in digest..."

API_MODMAP="$BUILD_DIR/paper-muncher-lib/__obj__/paper_muncher_api.cpp.o.modmap"

if [[ ! -f "$API_MODMAP" ]]; then
    echo "ERROR: Cannot find API modmap at $API_MODMAP"
    exit 1
fi

clang++-21 -fPIC -std=gnu++2c -Wall -Wextra -Werror -fcolor-diagnostics \
    -fmodules-reduced-bmi \
    -DBUILD_DIGEST=\""${BUILD_DIGEST}"\" \
    @"$API_MODMAP" \
    -c -o "$TMP_DIR/api.o" \
    src/paper_muncher/paper_muncher_api.cpp

echo "  Compiled API with BUILD_DIGEST."

# Swap the API object: remove old, add new
grep -v "paper_muncher_api" "$TMP_DIR/objects.txt" > "$TMP_DIR/objects_final.txt"
echo "$TMP_DIR/api.o" >> "$TMP_DIR/objects_final.txt"

# ─── Step 7: Link shared library ────────────────────────────────────────────

echo "▶ Step 6: Linking $SO_NAME..."

clang++-21 -shared -o "$TMP_DIR/$SO_NAME" \
    $(cat "$TMP_DIR/objects_final.txt") \
    -lfontconfig -lfreetype -lz -lpng -lbz2 -ljpeg -luring -lseccomp

echo "  Linked successfully."
echo "  File: $(file "$TMP_DIR/$SO_NAME")"
echo "  Size: $(du -h "$TMP_DIR/$SO_NAME" | cut -f1)"

# ─── Step 8: Verify ─────────────────────────────────────────────────────────

echo "▶ Step 7: Verifying..."

# Dump symbols once to a file
nm -D "$TMP_DIR/$SO_NAME" > "$TMP_DIR/symbols.txt" 2>/dev/null

EXPECTED_SYMBOLS="pm_init pm_shutdown pm_html_to_pdf_buffer pm_free pm_last_error pm_build_digest"
MISSING=""

for sym in $EXPECTED_SYMBOLS; do
    if ! grep -q "$sym" "$TMP_DIR/symbols.txt"; then
        MISSING="$MISSING $sym"
    fi
done

if [[ -n "$MISSING" ]]; then
    echo "ERROR: Missing symbols:$MISSING"
    echo ""
    echo "  Debug — all pm_ symbols found:"
    grep "pm_" "$TMP_DIR/symbols.txt" || echo "    (none)"
    exit 1
fi

if grep -qi "SDL_Init" "$TMP_DIR/symbols.txt"; then
    echo "ERROR: SDL symbols found in .so — UI dependencies leaked in."
    exit 1
fi

echo "  ✓ All symbols present"
echo "  ✓ No SDL contamination"

# ─── Step 9: Copy outputs ───────────────────────────────────────────────────

echo "▶ Step 8: Copying outputs to $OUTPUT_DIR..."

mkdir -p "$OUTPUT_DIR"
cp "$TMP_DIR/$SO_NAME" "$OUTPUT_DIR/$SO_NAME"

for bundle_dir in "$BUILD_DIR"/*/__res__; do
    if [[ -d "$bundle_dir" ]] && ls "$bundle_dir"/* &>/dev/null; then
        bundle_name="$(basename "$(dirname "$bundle_dir")")"
        mkdir -p "$OUTPUT_DIR/bundle/$bundle_name/__res__"
        cp -r "$bundle_dir/"* "$OUTPUT_DIR/bundle/$bundle_name/__res__/"
    fi
done

cat > "$OUTPUT_DIR/BUILD_INFO" <<EOF
build_digest=$BUILD_DIGEST
paper_muncher_sha=$PM_SHA
karm_sha=$KARM_SHA
built_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)
built_on=$(hostname)
clang_version=$(clang++-21 --version | head -1)
EOF

echo "  Copied $SO_NAME"
echo "  Copied bundle resources"
echo "  Wrote BUILD_INFO"

# ─── Done ────────────────────────────────────────────────────────────────────

cat > "$OUTPUT_DIR/BUILD_INFO" <<EOF
build_digest=$BUILD_DIGEST
paper_muncher_sha=$PM_SHA
karm_sha=$KARM_SHA
built_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)
built_on=$(hostname)
clang_version=$(clang++-21 --version | head -1)
EOF

echo "  Copied $SO_NAME"
echo "  Copied bundle resources"
echo "  Wrote BUILD_INFO"

# ─── Done ────────────────────────────────────────────────────────────────────

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  ✓ Build complete!"
echo ""
echo "  Output:"
echo "    $OUTPUT_DIR/$SO_NAME"
echo "    $OUTPUT_DIR/bundle/"
echo "    $OUTPUT_DIR/BUILD_INFO"
echo ""
echo "  Digest: $BUILD_DIGEST"
echo ""
echo "  To use: copy contents of $OUTPUT_DIR into your gem's ext/solpdf/"
echo "═══════════════════════════════════════════════════════════════"