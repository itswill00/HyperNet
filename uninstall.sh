#!/system/bin/sh

# Terminate active processes
pkill -9 -f libhypernet.so 2>/dev/null || true

# Cleanup transient runtime caches
rm -f /data/local/tmp/hypernet* 2>/dev/null || true

# Remove internal configuration and history
rm -rf /data/adb/hypernet 2>/dev/null || true

# Restore standard kernel defaults
sysctl -w net.ipv4.tcp_congestion_control=cubic >/dev/null 2>&1 || true
sysctl -w net.ipv4.tcp_fastopen=1 >/dev/null 2>&1 || true

exit 0
