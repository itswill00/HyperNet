#!/system/bin/sh
MODDIR="${0%/*}"

# Wait for system boot completion
while [ "$(getprop sys.boot_completed)" != "1" ]; do
    sleep 3
done

# Ensure persistent directory exists
mkdir -p /data/adb/hypernet 2>/dev/null || true
chmod 0755 /data/adb/hypernet 2>/dev/null || true

# Cleanup transient runtime caches
rm -f /data/local/tmp/hypernet* 2>/dev/null || true

# Execute boot configuration restoration
BIN=""
for b in "$MODDIR/system/bin/libhypernet.so" /data/adb/modules/hypernet/system/bin/libhypernet.so; do
    if [ -x "$b" ]; then
        BIN="$b"
        break
    fi
done

if [ -n "$BIN" ]; then
    $BIN apply_boot >/dev/null 2>&1 &
fi
