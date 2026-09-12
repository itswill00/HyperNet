#!/system/bin/sh
# Copyright (C) 2026 @itswill00
# Licensed under the GNU General Public License v3.0

set -e

PROJECT_DIR="/data/data/com.termux/files/home/HyperNet_Module"
cd "$PROJECT_DIR"

DEPLOY=false
CLEAN=false
CUSTOM_OUTPUT=""

while [ $# -gt 0 ]; do
    case "$1" in
        -d|--deploy)
            DEPLOY=true
            shift
            ;;
        -o|--output)
            CUSTOM_OUTPUT="$2"
            shift 2
            ;;
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -h|--help)
            echo "Usage: ./build.sh [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  -d, --deploy       Deploy module directly to /data/adb/modules/hypernet"
            echo "  -o, --output DIR   Specify custom output directory for zip releases"
            echo "  -c, --clean        Clean build caches before build"
            echo "  -h, --help         Show this help information"
            exit 0
            ;;
        *)
            echo "error: unrecognized option '$1' (use -h for help)"
            exit 1
            ;;
    esac
done

if [ ! -f "module.prop" ]; then
    echo "error: module.prop not found in $PROJECT_DIR"
    exit 1
fi

VERSION=$(grep '^version=' module.prop | cut -d= -f2)
VERSION_CODE=$(grep '^versionCode=' module.prop | cut -d= -f2)

if [ -n "$CUSTOM_OUTPUT" ]; then
    OUTPUT_DIR="$CUSTOM_OUTPUT"
else
    OUTPUT_DIR="/data/data/com.termux/files/home/HyperNet_Releases"
fi
mkdir -p "$OUTPUT_DIR"

ZIP_NAME="HyperNet-${VERSION}-b${VERSION_CODE}-Standalone.zip"
ZIP_ALIAS="HyperNet-${VERSION}.zip"
ZIP_LATEST="HyperNet-latest.zip"

echo "=========================================="
echo "  HyperNet Build Pipeline"
echo "  Version: ${VERSION} (b${VERSION_CODE})"
echo "  Target:  ${OUTPUT_DIR}/${ZIP_NAME}"
echo "=========================================="

for tool in clang zip node; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: required tool '$tool' is not installed"
        exit 1
    fi
done

if [ "$CLEAN" = "true" ]; then
    echo "cleaning build artifacts..."
    rm -rf webui/dist webroot/index.html system/bin/libhypernet.so releases
fi

# 1. Compile native C bridge
echo "compiling native c bridge..."
mkdir -p system/bin
clang -O3 -Wall -Wextra src/main.c -o system/bin/libhypernet.so
strip --strip-unneeded system/bin/libhypernet.so
chmod 755 system/bin/libhypernet.so

# 2. Build single-file WebUI
if [ -d "webui" ]; then
    if [ ! -d "webui/node_modules" ]; then
        echo "installing webui build dependencies..."
        (cd webui && npm install --no-audit --no-fund)
    fi
    echo "building single-file webui bundle..."
    (cd webui && node ./node_modules/vite/bin/vite.js build)
    mkdir -p webroot
    cp -f webui/dist/index.html webroot/index.html
    chmod 644 webroot/index.html
fi

# 3. Package module zip
STAGING_DIR="${PROJECT_DIR}/releases"
mkdir -p "$STAGING_DIR"
rm -f "$STAGING_DIR/HyperNet-${VERSION}-b${VERSION_CODE}"*.zip

echo "packaging module zip..."
zip -qr9 "$STAGING_DIR/$ZIP_NAME" \
    module.prop \
    customize.sh \
    service.sh \
    uninstall.sh \
    system \
    webroot \
    -x "*.git*" "webui/*" "src/*"

cp -f "$STAGING_DIR/$ZIP_NAME" "$STAGING_DIR/$ZIP_ALIAS"
cp -f "$STAGING_DIR/$ZIP_NAME" "$STAGING_DIR/$ZIP_LATEST"

if [ "$OUTPUT_DIR" != "$STAGING_DIR" ]; then
    cp -f "$STAGING_DIR"/* "$OUTPUT_DIR/" 2>/dev/null || true
fi

# Sync copy to /sdcard/HyperNet_Releases for external root file managers
su -c "mkdir -p /sdcard/HyperNet_Releases && cp -f '$STAGING_DIR'/* /sdcard/HyperNet_Releases/ && chmod 666 /sdcard/HyperNet_Releases/*" 2>/dev/null || true
am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE -d "file:///sdcard/HyperNet_Releases/$ZIP_NAME" >/dev/null 2>&1 || true

ZIP_SIZE=$(du -h "$STAGING_DIR/$ZIP_NAME" | cut -f1)
CHECKSUM=$(sha256sum "$STAGING_DIR/$ZIP_NAME" | cut -d' ' -f1)

echo "=========================================="
echo "  Build successful!"
echo "  Package:  ${OUTPUT_DIR}/${ZIP_NAME} (${ZIP_SIZE})"
echo "  Aliases:  ${OUTPUT_DIR}/${ZIP_ALIAS}"
echo "            ${OUTPUT_DIR}/${ZIP_LATEST}"
echo "  SHA-256:  ${CHECKSUM}"
echo "=========================================="

if [ "$DEPLOY" = "true" ]; then
    echo "deploying to live device modules (/data/adb/modules/hypernet)..."
    su -c "
        pkill -9 -x libhypernet.so 2>/dev/null || true
        MOD_TARGET=\"/data/adb/modules/hypernet\"
        mkdir -p \"\$MOD_TARGET/system/bin\" \"\$MOD_TARGET/webroot\"
        cp -f \"$PROJECT_DIR/module.prop\" \"\$MOD_TARGET/\"
        cp -f \"$PROJECT_DIR/customize.sh\" \"\$MOD_TARGET/\"
        cp -f \"$PROJECT_DIR/service.sh\" \"\$MOD_TARGET/\"
        cp -f \"$PROJECT_DIR/uninstall.sh\" \"\$MOD_TARGET/\"
        cp -f \"$PROJECT_DIR/system/bin/libhypernet.so\" \"\$MOD_TARGET/system/bin/\"
        cp -f \"$PROJECT_DIR/system/bin/wg\" \"\$MOD_TARGET/system/bin/\" 2>/dev/null || true
        cp -f \"$PROJECT_DIR/webroot/index.html\" \"\$MOD_TARGET/webroot/\"
        chmod 755 \"\$MOD_TARGET/service.sh\" \"\$MOD_TARGET/uninstall.sh\" \"\$MOD_TARGET/system/bin/libhypernet.so\" \"\$MOD_TARGET/system/bin/wg\"
        chmod 644 \"\$MOD_TARGET/module.prop\" \"\$MOD_TARGET/customize.sh\" \"\$MOD_TARGET/webroot/index.html\"
        chcon -R u:object_r:system_file:s0 \"\$MOD_TARGET\" 2>/dev/null || true
        echo 'deployment completed'
    "
fi
