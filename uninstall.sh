#!/system/bin/sh

# 1. Stop active processes
pkill -9 -f libhypernet.so 2>/dev/null || true

# 2. Restore factory network baseline from stock_state.conf
STOCK_CONF="/data/adb/hypernet/stock_state.conf"
if [ -f "$STOCK_CONF" ]; then
    STOCK_CC=$(grep '^stock_tcp_cc=' "$STOCK_CONF" 2>/dev/null | cut -d= -f2)
    STOCK_FO=$(grep '^stock_tcp_fastopen=' "$STOCK_CONF" 2>/dev/null | cut -d= -f2)
    STOCK_RMEM=$(grep '^stock_tcp_rmem=' "$STOCK_CONF" 2>/dev/null | cut -d= -f2-)
    STOCK_WMEM=$(grep '^stock_tcp_wmem=' "$STOCK_CONF" 2>/dev/null | cut -d= -f2-)
    STOCK_RMAX=$(grep '^stock_core_rmem_max=' "$STOCK_CONF" 2>/dev/null | cut -d= -f2)
    STOCK_WMAX=$(grep '^stock_core_wmem_max=' "$STOCK_CONF" 2>/dev/null | cut -d= -f2)
    STOCK_DNS_MODE=$(grep '^stock_private_dns_mode=' "$STOCK_CONF" 2>/dev/null | cut -d= -f2)
    STOCK_DNS_SPEC=$(grep '^stock_private_dns_specifier=' "$STOCK_CONF" 2>/dev/null | cut -d= -f2)
    STOCK_WIFI_THROTTLE=$(grep '^stock_wifi_throttle=' "$STOCK_CONF" 2>/dev/null | cut -d= -f2)
    STOCK_MOBILE_ALWAYS=$(grep '^stock_mobile_always=' "$STOCK_CONF" 2>/dev/null | cut -d= -f2)
    STOCK_TYPES_S0=$(grep '^stock_allowed_types_s0=' "$STOCK_CONF" 2>/dev/null | cut -d= -f2-)
    STOCK_TYPES_S1=$(grep '^stock_allowed_types_s1=' "$STOCK_CONF" 2>/dev/null | cut -d= -f2-)

    # Restore TCP parameters
    [ -n "$STOCK_CC" ] && sysctl -w net.ipv4.tcp_congestion_control="$STOCK_CC" >/dev/null 2>&1 || true
    [ -n "$STOCK_FO" ] && sysctl -w net.ipv4.tcp_fastopen="$STOCK_FO" >/dev/null 2>&1 || true
    [ -n "$STOCK_RMEM" ] && sysctl -w net.ipv4.tcp_rmem="$STOCK_RMEM" >/dev/null 2>&1 || true
    [ -n "$STOCK_WMEM" ] && sysctl -w net.ipv4.tcp_wmem="$STOCK_WMEM" >/dev/null 2>&1 || true
    [ -n "$STOCK_RMAX" ] && sysctl -w net.core.rmem_max="$STOCK_RMAX" >/dev/null 2>&1 || true
    [ -n "$STOCK_WMAX" ] && sysctl -w net.core.wmem_max="$STOCK_WMAX" >/dev/null 2>&1 || true

    # Restore Android system settings
    if [ -n "$STOCK_DNS_MODE" ]; then
        settings put global private_dns_mode "$STOCK_DNS_MODE" 2>/dev/null || true
    fi
    if [ -n "$STOCK_DNS_SPEC" ]; then
        settings put global private_dns_specifier "$STOCK_DNS_SPEC" 2>/dev/null || true
    else
        settings delete global private_dns_specifier 2>/dev/null || true
    fi
    if [ -n "$STOCK_WIFI_THROTTLE" ]; then
        settings put global wifi_scan_throttle_enabled "$STOCK_WIFI_THROTTLE" 2>/dev/null || true
    fi
    if [ -n "$STOCK_MOBILE_ALWAYS" ]; then
        settings put global mobile_data_always_on "$STOCK_MOBILE_ALWAYS" 2>/dev/null || true
    fi

    # Restore cellular network modes to stock baseline
    if [ -n "$STOCK_TYPES_S0" ]; then
        cmd phone set-allowed-network-types-for-users -s 0 "$STOCK_TYPES_S0" >/dev/null 2>&1 || true
    else
        cmd phone set-allowed-network-types-for-users -s 0 11001111101111111111 >/dev/null 2>&1 || true
    fi
    if [ -n "$STOCK_TYPES_S1" ]; then
        cmd phone set-allowed-network-types-for-users -s 1 "$STOCK_TYPES_S1" >/dev/null 2>&1 || true
    else
        cmd phone set-allowed-network-types-for-users -s 1 11001111101111111111 >/dev/null 2>&1 || true
    fi
else
    # Fallback to safe standard defaults
    sysctl -w net.ipv4.tcp_congestion_control=cubic >/dev/null 2>&1 || true
    sysctl -w net.ipv4.tcp_fastopen=1 >/dev/null 2>&1 || true
    settings put global private_dns_mode off 2>/dev/null || true
    cmd phone set-allowed-network-types-for-users -s 0 11001111101111111111 >/dev/null 2>&1 || true
fi

# 3. Cleanup transient caches, tunnels, and DPI evasion iptables rules
ip link del dev hypernet-warp >/dev/null 2>&1 || true
ip route flush table 1337 >/dev/null 2>&1 || true
ip rule del priority 9000 >/dev/null 2>&1 || true
iptables -t nat -D POSTROUTING -o hypernet-warp -j MASQUERADE >/dev/null 2>&1 || true

while iptables -t mangle -D POSTROUTING -p tcp --dport 443 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1; do :; done
while ip6tables -t mangle -D POSTROUTING -p tcp --dport 443 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1; do :; done
while iptables -t mangle -D POSTROUTING -p tcp --dport 80 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1; do :; done
while ip6tables -t mangle -D POSTROUTING -p tcp --dport 80 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1; do :; done
while iptables -D OUTPUT -p udp --dport 443 -j DROP >/dev/null 2>&1; do :; done
while ip6tables -D OUTPUT -p udp --dport 443 -j DROP >/dev/null 2>&1; do :; done

rm -f /data/local/tmp/hypernet* 2>/dev/null || true

# 4. Remove module configuration and history
rm -rf /data/adb/hypernet 2>/dev/null || true

exit 0
