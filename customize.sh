#!/system/bin/sh
SKIPUNZIP=1

if [ "$ARCH" != "arm64" ]; then
    ui_print "! Unsupported architecture: $ARCH"
    ui_print "! HyperNet requires 64-bit ARM (arm64)."
    abort "! Installation aborted."
fi

ui_print "- Installing HyperNet..."

# Stop any running processes from prior versions
pkill -9 -f libhypernet.so 2>/dev/null || true

ui_print "- Extracting files..."
unzip -o "$ZIPFILE" -x 'META-INF/*' -d "$MODPATH" >/dev/null 2>&1

ui_print "- Preparing persistent directories..."
mkdir -p /data/adb/hypernet 2>/dev/null || true
chmod 0755 /data/adb/hypernet 2>/dev/null || true
mkdir -p /data/local/tmp 2>/dev/null || true
chmod 0777 /data/local/tmp 2>/dev/null || true

ui_print "- Setting file permissions and SELinux contexts..."
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm_recursive "$MODPATH/system/bin" 0 0 0755 0755
chmod 755 "$MODPATH/system/bin/libhypernet.so" 2>/dev/null
chmod 755 "$MODPATH/service.sh" 2>/dev/null
chmod 755 "$MODPATH/uninstall.sh" 2>/dev/null
chmod 644 "$MODPATH/module.prop" 2>/dev/null
chmod 644 "$MODPATH/webroot/index.html" 2>/dev/null

# Enforce system SELinux contexts for Android 10-15+ compatibility
chcon -R u:object_r:system_file:s0 "$MODPATH" 2>/dev/null || true

if [ -f "$MODPATH/webroot/index.html" ]; then
    ui_print "- WebUI configured successfully."
fi

ui_print "- HyperNet installation completed."
