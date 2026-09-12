#!/system/bin/sh
SKIPUNZIP=1

ui_print "=========================================="
ui_print "  HyperNet Installer"
ui_print "=========================================="

# 1. Architecture validation
if [ "$ARCH" != "arm64" ]; then
    ui_print "! Incompatible CPU architecture: $ARCH"
    ui_print "! HyperNet native engine requires 64-bit ARM (arm64)."
    abort "! Installation aborted."
fi

# 2. Environment & Root Manager Detection
ROOT_MGR="Unknown"
if [ -n "$KSU" ]; then
    ROOT_MGR="KernelSU (v$KSU_VER, code $KSU_VER_CODE)"
elif [ -n "$APATCH" ]; then
    ROOT_MGR="APatch (v$APATCH_VER, code $APATCH_VER_CODE)"
elif [ -n "$MAGISK_VER" ]; then
    ROOT_MGR="Magisk (v$MAGISK_VER, code $MAGISK_VER_CODE)"
fi

ANDROID_VER=$(getprop ro.build.version.release 2>/dev/null || echo "unknown")
SDK_API=$(getprop ro.build.version.sdk 2>/dev/null || echo "unknown")
KERNEL_VER=$(uname -r)

ui_print "- Environment: $ROOT_MGR"
ui_print "- Android:     $ANDROID_VER (API $SDK_API)"
ui_print "- Kernel:      $KERNEL_VER"

# 3. Terminate active binary instances before file extraction
ui_print "- Stopping active processes..."
pkill -9 -f libhypernet.so 2>/dev/null || true
rm -f /data/local/tmp/hypernet*.pid /data/local/tmp/hypernet*.sock 2>/dev/null || true

# 4. Extract module files
ui_print "- Extracting files..."
unzip -o "$ZIPFILE" -x 'META-INF/*' -d "$MODPATH" >/dev/null 2>&1

# 5. Prepare persistent configuration directory
mkdir -p /data/adb/hypernet 2>/dev/null || true
chmod 0755 /data/adb/hypernet 2>/dev/null || true
mkdir -p /data/local/tmp 2>/dev/null || true
chmod 0777 /data/local/tmp 2>/dev/null || true

# 6. Capture or preserve stock baseline
STOCK_CONF="/data/adb/hypernet/stock_state.conf"
if [ -f "$STOCK_CONF" ]; then
    ui_print "- Preserving existing factory network baseline..."
else
    ui_print "- Capturing live network baseline for safe rollback..."

    STOCK_TCP_CC=$(cat /proc/sys/net/ipv4/tcp_congestion_control 2>/dev/null || echo "cubic")
    STOCK_TCP_FO=$(cat /proc/sys/net/ipv4/tcp_fastopen 2>/dev/null || echo "1")
    STOCK_TCP_RMEM=$(cat /proc/sys/net/ipv4/tcp_rmem 2>/dev/null || echo "4096 87380 6291456")
    STOCK_TCP_WMEM=$(cat /proc/sys/net/ipv4/tcp_wmem 2>/dev/null || echo "4096 16384 4194304")
    STOCK_CORE_RMAX=$(cat /proc/sys/net/core/rmem_max 2>/dev/null || echo "2097152")
    STOCK_CORE_WMAX=$(cat /proc/sys/net/core/wmem_max 2>/dev/null || echo "2097152")

    STOCK_DNS_MODE=$(settings get global private_dns_mode 2>/dev/null || echo "off")
    STOCK_DNS_SPEC=$(settings get global private_dns_specifier 2>/dev/null || echo "")
    STOCK_WIFI_THROTTLE=$(settings get global wifi_scan_throttle_enabled 2>/dev/null || echo "1")
    STOCK_MOBILE_ALWAYS=$(settings get global mobile_data_always_on 2>/dev/null || echo "0")

    STOCK_TYPES_S0=$(cmd phone get-allowed-network-types-for-users -s 0 2>/dev/null || echo "")
    STOCK_TYPES_S1=$(cmd phone get-allowed-network-types-for-users -s 1 2>/dev/null || echo "")

    cat << EOF > "$STOCK_CONF"
# HyperNet Stock Factory Baseline
# Captured automatically during initial module installation
has_baseline=1
stock_tcp_cc=$STOCK_TCP_CC
stock_tcp_fastopen=$STOCK_TCP_FO
stock_tcp_rmem=$STOCK_TCP_RMEM
stock_tcp_wmem=$STOCK_TCP_WMEM
stock_core_rmem_max=$STOCK_CORE_RMAX
stock_core_wmem_max=$STOCK_CORE_WMAX
stock_private_dns_mode=$STOCK_DNS_MODE
stock_private_dns_specifier=$STOCK_DNS_SPEC
stock_wifi_throttle=$STOCK_WIFI_THROTTLE
stock_mobile_always=$STOCK_MOBILE_ALWAYS
stock_allowed_types_s0=$STOCK_TYPES_S0
stock_allowed_types_s1=$STOCK_TYPES_S1
EOF
    chmod 0644 "$STOCK_CONF"
fi

# 7. Set permissions and system SELinux contexts
ui_print "- Applying security permissions & SELinux contexts..."
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm_recursive "$MODPATH/system/bin" 0 0 0755 0755
chmod 755 "$MODPATH/system/bin/libhypernet.so" 2>/dev/null
chmod 755 "$MODPATH/service.sh" 2>/dev/null
chmod 755 "$MODPATH/uninstall.sh" 2>/dev/null
chmod 644 "$MODPATH/module.prop" 2>/dev/null
chmod 644 "$MODPATH/webroot/index.html" 2>/dev/null

# Enforce standard system file context
chcon -R u:object_r:system_file:s0 "$MODPATH" 2>/dev/null || true

ui_print "- WebUI and native engine ready."
ui_print "=========================================="
ui_print "  Installation completed successfully."
ui_print "=========================================="
