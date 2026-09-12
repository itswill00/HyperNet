# HyperNet

Standalone network analysis toolkit, multi-engine speedtest, cellular band locker, and kernel TCP/IP optimizer for Android rooted environments (KernelSU, APatch, and Magisk).

Featuring a lightweight native C bridge (`libhypernet.so`) and a single-file Vue 3 WebUI built with a minimalist dark monochrome Material 3 aesthetic.

---

## Key features

- **Universal network speedtest**:
  - Benchmarks any active interface: Wi-Fi, cellular (5G/4G), Ethernet, USB tethering, or VPN.
  - Multi-stream parallel downloads and uploads using global Anycast edge infrastructure (Cloudflare edge and secondary CDNs).
  - High-precision latency and jitter measurements with rolling SVG throughput sparkline.
  - Local history tracking with instant clearing.

- **Deep network diagnostics**:
  - Real-time interface traffic rates (KB/s and MB/s) read directly from `/proc/net/dev` with zero UI latency.
  - Wi-Fi telemetry: SSID, BSSID, RSSI (dBm), frequency (MHz), band (2.4 GHz, 5 GHz, 6 GHz), link speeds, Wi-Fi standard (Wi-Fi 4/5/6/7), local IP, and default gateway.
  - Cellular telemetry: carrier operator, active radio technology (5G NR, LTE), RSRP, RSRQ, SINR, signal quality bars, Cell ID (CID), and allowed network types.
  - Multi-provider DNS benchmark: tests lookup response times across Cloudflare, Google, Quad9, AdGuard, and OpenDNS.

- **Cellular band & mode locker**:
  - Lock radio directly via Android telephony service (`cmd phone`):
    - 5G only (NR)
    - 5G / 4G preferred (NR / LTE)
    - 4G only (LTE)
    - 3G only (WCDMA / HSPA)
    - 2G only (GSM)
    - Global auto
  - One-tap cellular tower reconnection: safely resets the radio link to drop stuck carrier sessions and acquire the strongest cell tower.

- **Kernel TCP & system optimizer**:
  - TCP congestion control switcher: dynamically checks kernel capabilities and applies algorithms like `cubic`, `bbr`, or `reno`.
  - TCP buffer profiles:
    - **Gaming**: minimized queue depths and aggressive ACK pacing to reduce latency jitter.
    - **Streaming / Throughput**: expanded 16MB window scaling for maximum bandwidth saturation.
    - **Stock**: kernel defaults.
  - Android Private DNS quick-switch: one-tap configuration for Cloudflare (`one.one.one.one`), AdGuard (`dns.adguard-dns.com`), Quad9 (`dns.quad9.net`), or Google (`dns.google`).
  - System performance toggles: Wi-Fi scan throttle toggle, mobile data always active toggle, and TCP Fast Open (TFO).

- **Zero runtime dependencies**:
  - Unlike modules requiring heavy Python or Node runtimes on user devices, HyperNet compiles to a native C executable of under 30KB.
  - Single-file WebUI bundle (under 120KB) renders instantaneously in KernelSU / APatch / MMRL WebViews.

---

## Architecture

```
HyperNet_Module/
├── module.prop          # Magisk / KernelSU metadata
├── customize.sh         # Installation and permission rules
├── service.sh           # Boot-time configuration persistence
├── uninstall.sh         # Clean uninstallation and sysctl restoration
├── build.sh             # Automated compilation and packaging pipeline
├── src/
│   └── main.c           # Native C bridge (libhypernet.so)
├── system/bin/
│   └── libhypernet.so   # Stripped native executable
├── webroot/
│   └── index.html       # Bundled single-file Vue 3 WebUI
└── webui/               # Vue 3 + Vite development source
```

---

## Native C bridge commands (`libhypernet.so`)

The native binary can be invoked directly from root terminal or by the WebUI:

```sh
# Display comprehensive network, Wi-Fi, cellular, and TCP telemetry (JSON format)
libhypernet.so info

# Real-time traffic bytes per interface from /proc/net/dev
libhypernet.so traffic

# Measure latency and packet loss to a host
libhypernet.so ping 1.1.1.1 3

# Benchmark DNS resolution across major resolvers
libhypernet.so dns_bench

# Lock cellular radio network mode
libhypernet.so set_mode 0 5g_only
libhypernet.so set_mode 0 lte_only
libhypernet.so set_mode 0 auto

# Change TCP congestion control algorithm
libhypernet.so set_tcp_cc bbr
libhypernet.so set_tcp_cc cubic

# Apply TCP buffer profile
libhypernet.so set_tcp_profile gaming
libhypernet.so set_tcp_profile throughput
libhypernet.so set_tcp_profile stock

# Configure Android Private DNS
libhypernet.so set_dns hostname one.one.one.one
libhypernet.so set_dns off

# Toggle system performance tweaks
libhypernet.so set_tweak wifi_throttle 0
libhypernet.so set_tweak mobile_data_always 1
libhypernet.so set_tweak fast_open 1

# Refresh cellular tower connection
libhypernet.so radio_refresh
```

---

## Building from source

Requirements:
- Clang compiler
- Node.js (v18+)
- Zip utility

To build the flashable zip:

```sh
chmod +x build.sh
./build.sh
```

The output zip will be placed in `~/HyperNet_Releases/` or `/sdcard/HyperNet_Releases/`.

To build and deploy directly to a rooted device:

```sh
./build.sh --deploy
```

---

## Compatibility

- **Root managers**: KernelSU, KernelSU Next, APatch, Magisk, MMRL.
- **Android versions**: Android 10, 11, 12, 13, 14, 15, and 16.
- **Architectures**: `arm64-v8a` (aarch64).
- **WebViews**: Tested on Android system WebView, Chromium, and Bromite with hardware-accelerated monochrome styling.

---

## License

Licensed under the GNU General Public License v3.0 (GPL-3.0).
Copyright (C) 2026 @itswill00.
