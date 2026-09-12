<template>
  <div class="app-shell">
    <!-- Sticky Header -->
    <header class="page-header">
      <div>
        <div class="page-header-title">HyperNet</div>
        <div class="page-header-sub">Network toolkit & speedtest</div>
      </div>
      <div style="display: flex; align-items: center; gap: 8px;">
        <span class="badge-pill" :class="isOnline ? 'online' : 'offline'">
          <Icons :name="isOnline ? (telemetry.wifi.connected ? 'wifi' : 'radio') : 'wifi-off'" :size="11" />
          <span>{{ isOnline ? (telemetry.wifi.connected ? 'Wi-Fi' : telemetry.cellular.operator || 'Cellular') : 'Offline' }}</span>
        </span>
        <span class="badge-pill" style="cursor: pointer; user-select: none;" @click="refreshTelemetry(true)">
          <Icons name="refresh" :size="11" :class="{ 'spin-anim': isRefreshing }" />
          <span>v1.0.0</span>
        </span>
      </div>
    </header>

    <!-- Navigation Tabs -->
    <div style="padding: 10px 16px 0 16px; background: var(--bg);">
      <div class="tabs-bar">
        <button class="tab-btn" :class="{ active: activeTab === 'speed' }" @click="activeTab = 'speed'">
          <Icons name="gauge" :size="13" />
          <span>Speedtest</span>
        </button>
        <button class="tab-btn" :class="{ active: activeTab === 'diagnostics' }" @click="activeTab = 'diagnostics'">
          <Icons name="wifi" :size="13" />
          <span>Diagnostics</span>
        </button>
        <button class="tab-btn" :class="{ active: activeTab === 'cellular' }" @click="activeTab = 'cellular'">
          <Icons name="radio" :size="13" />
          <span>Cellular</span>
        </button>
        <button class="tab-btn" :class="{ active: activeTab === 'optimizer' }" @click="activeTab = 'optimizer'">
          <Icons name="sliders" :size="13" />
          <span>Optimizer</span>
        </button>
        <button class="tab-btn" :class="{ active: activeTab === 'console' }" @click="activeTab = 'console'">
          <Icons name="terminal" :size="13" />
          <span>Console</span>
        </button>
      </div>
    </div>

    <!-- Main Content Area -->
    <main class="content-area">
      <!-- 1. SPEEDTEST TAB -->
      <div v-show="activeTab === 'speed'" style="display: flex; flex-direction: column; gap: 12px;">
        <!-- Speed Hero Display -->
        <section class="md3-card speed-hero-container">
          <!-- Server & Route badge -->
          <div style="display: flex; justify-content: space-between; width: 100%; align-items: center; margin-bottom: 8px;">
            <span class="badge-pill">
              <Icons name="server" :size="11" />
              <span>{{ selectedServer.name }}</span>
            </span>
            <span class="badge-pill">
              <Icons name="globe" :size="11" />
              <span>{{ telemetry.network.active_iface || 'auto' }}</span>
            </span>
          </div>

          <!-- Digital Mbps readout -->
          <div style="text-align: center; margin: 12px 0 6px 0;">
            <div class="speed-digital-value">{{ speedtestState.instantSpeed.toFixed(1) }}</div>
            <div class="speed-digital-unit">
              {{ speedtestState.phase === 'upload' ? 'Upload Mbps' : 'Download Mbps' }}
            </div>
          </div>

          <!-- Status phase indicator -->
          <div style="font-size: 11px; color: var(--on-surface-variant); margin-bottom: 12px; font-variant-numeric: tabular-nums;">
            <span v-if="speedtestState.isTesting">
              {{ formatTestPhase(speedtestState.phase) }} ({{ speedtestState.progressPct }}%)
            </span>
            <span v-else-if="speedtestState.lastTestedAt">
              Last tested {{ formatTimeAgo(speedtestState.lastTestedAt) }}
            </span>
            <span v-else>
              Ready to test any active connection
            </span>
          </div>

          <!-- Rolling Sparkline Chart -->
          <div class="chart-container" style="margin-bottom: 14px;">
            <svg viewBox="0 0 300 90" width="100%" height="90" preserveAspectRatio="none">
              <!-- Grid horizontal lines -->
              <line x1="0" y1="22" x2="300" y2="22" stroke="var(--surface-container-high)" stroke-dasharray="3,3" />
              <line x1="0" y1="45" x2="300" y2="45" stroke="var(--surface-container-high)" stroke-dasharray="3,3" />
              <line x1="0" y1="68" x2="300" y2="68" stroke="var(--surface-container-high)" stroke-dasharray="3,3" />
              <!-- Throughput polyline -->
              <polyline
                :points="chartSvgPoints"
                fill="none"
                stroke="var(--primary)"
                stroke-width="2"
                stroke-linecap="round"
                stroke-linejoin="round"
              />
            </svg>
          </div>

          <!-- Action Button -->
          <button
            class="btn btn-block"
            :class="speedtestState.isTesting ? 'btn-danger' : 'btn-primary'"
            @click="toggleSpeedtest"
          >
            <Icons :name="speedtestState.isTesting ? 'stop' : 'play'" :size="14" />
            <span>{{ speedtestState.isTesting ? 'Stop test' : 'Start speedtest' }}</span>
          </button>
        </section>

        <!-- Metrics Grid -->
        <div class="metrics-grid-3">
          <div class="metric-box">
            <span class="metric-box-label">Ping / Jitter</span>
            <span class="metric-box-val">
              {{ speedtestState.ping ? speedtestState.ping + ' ms' : '--' }}
            </span>
            <span style="font-size: 10px; color: var(--on-surface-variant);">
              {{ speedtestState.jitter ? '±' + speedtestState.jitter + ' ms' : '' }}
            </span>
          </div>
          <div class="metric-box">
            <span class="metric-box-label">Download</span>
            <span class="metric-box-val">
              {{ speedtestState.download ? speedtestState.download + ' M' : '--' }}
            </span>
            <span style="font-size: 10px; color: var(--on-surface-variant);">
              {{ speedtestState.bytesUsedMb ? speedtestState.bytesUsedMb.toFixed(1) + ' MB' : '' }}
            </span>
          </div>
          <div class="metric-box">
            <span class="metric-box-label">Upload</span>
            <span class="metric-box-val">
              {{ speedtestState.upload ? speedtestState.upload + ' M' : '--' }}
            </span>
            <span style="font-size: 10px; color: var(--on-surface-variant);">
              Mbps
            </span>
          </div>
        </div>

        <!-- Server Selector Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="server" :size="14" />
              <span>Speedtest server</span>
            </span>
            <span class="badge-pill">{{ servers.length }} available</span>
          </div>
          <div class="segmented-control">
            <button
              v-for="srv in servers"
              :key="srv.id"
              class="segment-btn"
              :class="{ active: selectedServer.id === srv.id }"
              @click="selectedServer = srv"
            >
              {{ srv.name }}
            </button>
          </div>
        </section>

        <!-- Test History Card -->
        <section class="md3-card" v-if="speedHistory.length > 0">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="clock" :size="14" />
              <span>Recent results</span>
            </span>
            <button class="btn btn-sm btn-secondary" @click="clearHistory">Clear</button>
          </div>
          <div v-for="(h, idx) in speedHistory.slice(0, 5)" :key="idx" class="kv-row">
            <div class="kv-label">
              <span>{{ h.date }}</span>
              <span style="color: var(--outline);">•</span>
              <span style="font-size: 11px;">{{ h.server }}</span>
            </div>
            <div class="kv-value">
              <span style="color: var(--primary);">↓ {{ h.download }}</span>
              <span style="margin: 0 4px; color: var(--outline);">/</span>
              <span style="color: var(--on-surface-variant);">↑ {{ h.upload }} Mbps</span>
              <span style="margin-left: 6px; font-size: 10px; color: var(--on-surface-variant);">({{ h.ping }}ms)</span>
            </div>
          </div>
        </section>
      </div>

      <!-- 2. DIAGNOSTICS TAB -->
      <div v-show="activeTab === 'diagnostics'" style="display: flex; flex-direction: column; gap: 12px;">
        <!-- Live Traffic Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="zap" :size="14" />
              <span>Live interface throughput</span>
            </span>
            <span class="badge-pill online">Active</span>
          </div>
          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin-top: 4px;">
            <div class="metric-box">
              <span class="metric-box-label">Current download</span>
              <span class="metric-box-val">{{ liveRate.rxRateStr }}</span>
            </div>
            <div class="metric-box">
              <span class="metric-box-label">Current upload</span>
              <span class="metric-box-val">{{ liveRate.txRateStr }}</span>
            </div>
          </div>
        </section>

        <!-- Wi-Fi Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="wifi" :size="14" />
              <span>Wi-Fi details</span>
            </span>
            <span class="badge-pill" :class="telemetry.wifi.connected ? 'online' : 'offline'">
              {{ telemetry.wifi.connected ? 'Connected' : 'Disconnected' }}
            </span>
          </div>
          <div class="kv-row">
            <span class="kv-label">SSID</span>
            <span class="kv-value">{{ telemetry.wifi.ssid }}</span>
          </div>
          <div class="kv-row">
            <span class="kv-label">BSSID</span>
            <span class="kv-value">{{ telemetry.wifi.bssid }}</span>
          </div>
          <div class="kv-row">
            <span class="kv-label">Signal strength</span>
            <span class="kv-value">{{ telemetry.wifi.rssi ? telemetry.wifi.rssi + ' dBm' : '--' }}</span>
          </div>
          <div class="kv-row">
            <span class="kv-label">Frequency / Band</span>
            <span class="kv-value">{{ telemetry.wifi.frequency_mhz ? telemetry.wifi.frequency_mhz + ' MHz (' + telemetry.wifi.band + ')' : '--' }}</span>
          </div>
          <div class="kv-row">
            <span class="kv-label">Standard</span>
            <span class="kv-value">{{ telemetry.wifi.standard }}</span>
          </div>
          <div class="kv-row">
            <span class="kv-label">Link speed</span>
            <span class="kv-value">{{ telemetry.wifi.link_speed_mbps ? telemetry.wifi.link_speed_mbps + ' Mbps' : '--' }}</span>
          </div>
          <div class="kv-row">
            <span class="kv-label">Local IP / Gateway</span>
            <span class="kv-value">{{ telemetry.wifi.ip || '--' }} / {{ telemetry.network.gateway || '--' }}</span>
          </div>
        </section>

        <!-- Cellular Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="radio" :size="14" />
              <span>Cellular telemetry</span>
            </span>
            <span class="badge-pill">{{ telemetry.cellular.operator || 'No SIM' }}</span>
          </div>
          <div class="kv-row">
            <span class="kv-label">Network technology</span>
            <span class="kv-value">{{ telemetry.cellular.network_type }}</span>
          </div>
          <div class="kv-row">
            <span class="kv-label">Carrier signal (RSRP / RSRQ)</span>
            <span class="kv-value">
              {{ telemetry.cellular.rsrp ? telemetry.cellular.rsrp + ' dBm' : '--' }} / {{ telemetry.cellular.rsrq ? telemetry.cellular.rsrq + ' dB' : '--' }}
            </span>
          </div>
          <div class="kv-row">
            <span class="kv-label">Signal quality (SINR)</span>
            <span class="kv-value">{{ telemetry.cellular.sinr ? telemetry.cellular.sinr + ' dB' : '--' }}</span>
          </div>
          <div class="kv-row">
            <span class="kv-label">Cell ID (CID)</span>
            <span class="kv-value">{{ telemetry.cellular.cell_id > 0 ? telemetry.cellular.cell_id : 'Hidden' }}</span>
          </div>
          <div class="kv-row">
            <span class="kv-label">Allowed network modes</span>
            <span class="kv-value" style="font-size: 11px;">{{ telemetry.cellular.allowed_types || 'Default' }}</span>
          </div>
        </section>

        <!-- DNS Benchmark Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="globe" :size="14" />
              <span>DNS latency benchmark</span>
            </span>
            <button class="btn btn-sm btn-secondary" :disabled="dnsBenchmarking" @click="runDnsBenchmark">
              <Icons name="refresh" :size="11" :class="{ 'spin-anim': dnsBenchmarking }" />
              <span>{{ dnsBenchmarking ? 'Testing...' : 'Benchmark' }}</span>
            </button>
          </div>
          <div v-if="dnsResults.length > 0" style="display: flex; flex-direction: column; gap: 4px; margin-top: 4px;">
            <div v-for="d in dnsResults" :key="d.name" class="kv-row">
              <span class="kv-label">{{ d.name }} ({{ d.ip }})</span>
              <span class="kv-value" :style="{ color: d.latency_ms > 0 && d.latency_ms < 25 ? 'var(--primary)' : 'var(--on-bg)' }">
                {{ d.latency_ms > 0 ? d.latency_ms.toFixed(1) + ' ms' : 'Timeout' }}
              </span>
            </div>
          </div>
          <div v-else style="font-size: 11px; color: var(--on-surface-variant); padding: 4px 0;">
            Compare DNS lookup response times across top global resolvers.
          </div>
        </section>
      </div>

      <!-- 3. CELLULAR TAB -->
      <div v-show="activeTab === 'cellular'" style="display: flex; flex-direction: column; gap: 12px;">
        <!-- Band Control Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="radio" :size="14" />
              <span>Network mode & band locker</span>
            </span>
            <span class="badge-pill">SIM 1</span>
          </div>
          <p style="font-size: 11px; color: var(--on-surface-variant); line-height: 1.4;">
            Lock radio strictly to selected cellular generations to avoid unwanted network handovers or excessive 5G battery drain.
          </p>

          <div class="radio-card-list" style="margin-top: 6px;">
            <div
              v-for="mode in cellularModes"
              :key="mode.id"
              class="radio-card-item"
              :class="{ selected: selectedCellularMode === mode.id }"
              @click="applyCellularMode(mode.id)"
            >
              <div class="radio-card-left">
                <span class="radio-card-name">{{ mode.title }}</span>
                <span class="radio-card-sub">{{ mode.desc }}</span>
              </div>
              <Icons v-if="selectedCellularMode === mode.id" name="check" :size="15" style="color: var(--primary);" />
            </div>
          </div>
        </section>

        <!-- Radio Refresh Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="refresh" :size="14" />
              <span>Cellular tower reconnection</span>
            </span>
          </div>
          <p style="font-size: 11px; color: var(--on-surface-variant); line-height: 1.4;">
            Safely refresh cellular radio link to force re-association with the nearest tower and clear frozen carrier data sessions.
          </p>
          <button class="btn btn-secondary btn-block" style="margin-top: 6px;" @click="confirmRadioRefresh">
            <Icons name="refresh" :size="13" />
            <span>Refresh cellular radio</span>
          </button>
        </section>
      </div>

      <!-- 4. OPTIMIZER TAB -->
      <div v-show="activeTab === 'optimizer'" style="display: flex; flex-direction: column; gap: 12px;">
        <!-- TCP Congestion Control Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="zap" :size="14" />
              <span>TCP congestion algorithm</span>
            </span>
            <span class="badge-pill active">{{ telemetry.tcp.current_cc }}</span>
          </div>
          <p style="font-size: 11px; color: var(--on-surface-variant); line-height: 1.4;">
            Kernel algorithm controlling packet pacing, window expansion, and loss recovery.
          </p>
          <div class="segmented-control" style="margin-top: 4px;">
            <button
              v-for="algo in availableTcpCc"
              :key="algo"
              class="segment-btn"
              :class="{ active: telemetry.tcp.current_cc === algo }"
              @click="applyTcpCc(algo)"
            >
              {{ algo }}
            </button>
          </div>
        </section>

        <!-- TCP Buffer Profiles Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="sliders" :size="14" />
              <span>TCP buffer profiles</span>
            </span>
            <span class="badge-pill">{{ activeBufferProfile }}</span>
          </div>
          <div class="segmented-control" style="margin-top: 4px;">
            <button
              class="segment-btn"
              :class="{ active: activeBufferProfile === 'stock' }"
              @click="applyBufferProfile('stock')"
            >
              Stock
            </button>
            <button
              class="segment-btn"
              :class="{ active: activeBufferProfile === 'gaming' }"
              @click="applyBufferProfile('gaming')"
            >
              Gaming
            </button>
            <button
              class="segment-btn"
              :class="{ active: activeBufferProfile === 'throughput' }"
              @click="applyBufferProfile('throughput')"
            >
              Streaming
            </button>
          </div>
          <p style="font-size: 11px; color: var(--on-surface-variant); margin-top: 4px;">
            {{ bufferProfileDesc }}
          </p>
        </section>

        <!-- Android Private DNS Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="globe" :size="14" />
              <span>Private DNS resolver</span>
            </span>
            <span class="badge-pill">{{ telemetry.settings.private_dns_mode }}</span>
          </div>
          <div class="radio-card-list" style="margin-top: 4px;">
            <div
              v-for="dns in dnsOptions"
              :key="dns.id"
              class="radio-card-item"
              :class="{ selected: selectedDns === dns.id }"
              @click="applyPrivateDns(dns)"
            >
              <div class="radio-card-left">
                <span class="radio-card-name">{{ dns.name }}</span>
                <span class="radio-card-sub">{{ dns.host || 'System default DNS' }}</span>
              </div>
              <Icons v-if="selectedDns === dns.id" name="check" :size="14" style="color: var(--primary);" />
            </div>
          </div>
        </section>

        <!-- System Tweak Switches Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="tune" :size="14" />
              <span>System network tweaks</span>
            </span>
          </div>

          <!-- Wi-Fi Scan Throttling -->
          <div class="switch-row">
            <div class="switch-label-col">
              <span class="switch-title">Wi-Fi scan throttling</span>
              <span class="switch-desc">Turn off to allow faster roaming between access points</span>
            </div>
            <label class="toggle-switch">
              <input
                type="checkbox"
                v-model="telemetry.settings.wifi_scan_throttle"
                @change="toggleTweak('wifi_throttle', telemetry.settings.wifi_scan_throttle)"
              />
              <span class="toggle-slider"></span>
            </label>
          </div>

          <!-- Mobile Data Always On -->
          <div class="switch-row">
            <div class="switch-label-col">
              <span class="switch-title">Mobile data always active</span>
              <span class="switch-desc">Keep cellular standby active while on Wi-Fi for zero-lag handovers</span>
            </div>
            <label class="toggle-switch">
              <input
                type="checkbox"
                v-model="telemetry.settings.mobile_data_always_on"
                @change="toggleTweak('mobile_data_always', telemetry.settings.mobile_data_always_on)"
              />
              <span class="toggle-slider"></span>
            </label>
          </div>

          <!-- TCP Fast Open -->
          <div class="switch-row">
            <div class="switch-label-col">
              <span class="switch-title">TCP Fast Open (TFO)</span>
              <span class="switch-desc">Eliminates handshake latency for returning connections</span>
            </div>
            <label class="toggle-switch">
              <input
                type="checkbox"
                :checked="telemetry.tcp.fastopen > 0"
                @change="toggleTweak('fast_open', telemetry.tcp.fastopen > 0 ? 0 : 1)"
              />
              <span class="toggle-slider"></span>
            </label>
          </div>
        </section>
      </div>

      <!-- 5. CONSOLE TAB -->
      <div v-show="activeTab === 'console'" style="display: flex; flex-direction: column; gap: 12px;">
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="terminal" :size="14" />
              <span>Network diagnostic console</span>
            </span>
            <button class="btn btn-sm btn-secondary" @click="consoleOutput = ''">Clear</button>
          </div>
          <div style="display: flex; gap: 6px; flex-wrap: wrap; margin-top: 4px;">
            <button class="btn btn-sm btn-secondary" @click="runConsoleCmd('info')">Status</button>
            <button class="btn btn-sm btn-secondary" @click="runConsoleCmd('routes')">Routes</button>
            <button class="btn btn-sm btn-secondary" @click="runConsoleCmd('ping')">Ping 1.1.1.1</button>
            <button class="btn btn-sm btn-secondary" @click="runConsoleCmd('dns_bench')">DNS bench</button>
          </div>
          <div class="console-box" style="margin-top: 8px;">
            {{ consoleOutput || 'Console ready. Execute diagnostic commands above.' }}
          </div>
        </section>
      </div>
    </main>

    <!-- In-App Modal Dialog (Mounted synchronously with v-if, zero transition backdrop) -->
    <div v-if="modalState.visible" class="modal-overlay" @click.self="modalState.visible = false">
      <div class="modal-dialog">
        <div class="modal-title">{{ modalState.title }}</div>
        <div class="modal-desc">{{ modalState.desc }}</div>
        <div class="modal-actions">
          <button class="btn btn-secondary" style="flex: 1;" @click="modalState.visible = false">Cancel</button>
          <button class="btn btn-primary" style="flex: 1;" @click="onModalConfirm">Confirm</button>
        </div>
      </div>
    </div>

    <!-- Toast Notification Pill -->
    <div v-if="toastMsg" class="toast-pill">
      <Icons name="check" :size="14" style="color: var(--primary);" />
      <span>{{ toastMsg }}</span>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, onUnmounted } from 'vue'
import Icons from './components/Icons.vue'
import { SpeedTestEngine } from './helpers/speedtest.js'
import { runBridge, runBridgeJson, execCommand } from './helpers/shell.js'

const activeTab = ref('speed')
const isOnline = ref(navigator.onLine)
const isRefreshing = ref(false)
const toastMsg = ref('')
let toastTimer = null

function showToast(msg) {
  toastMsg.value = msg
  clearTimeout(toastTimer)
  toastTimer = setTimeout(() => {
    toastMsg.value = ''
  }, 2400)
}

/* Telemetry State */
const telemetry = reactive({
  wifi: {
    enabled: true,
    connected: false,
    ssid: 'Scanning...',
    bssid: 'unknown',
    ip: '',
    rssi: 0,
    link_speed_mbps: 0,
    frequency_mhz: 0,
    band: 'unknown',
    standard: 'unknown'
  },
  cellular: {
    operator: 'Cellular',
    network_type: 'unknown',
    rsrp: 0,
    rsrq: 0,
    sinr: 0,
    level: 0,
    cell_id: -1,
    allowed_types: ''
  },
  network: {
    gateway: '',
    active_iface: ''
  },
  tcp: {
    current_cc: 'cubic',
    available_cc: 'cubic reno',
    fastopen: 1
  },
  settings: {
    private_dns_mode: 'off',
    private_dns_specifier: '',
    wifi_scan_throttle: true,
    mobile_data_always_on: false
  }
})

const availableTcpCc = computed(() => {
  return (telemetry.tcp.available_cc || 'cubic reno').split(' ').filter(Boolean)
})

/* Live Traffic State */
const liveRate = reactive({
  rxRateStr: '0 KB/s',
  txRateStr: '0 KB/s',
  lastRx: 0,
  lastTx: 0,
  lastTime: 0
})

let trafficInterval = null

async function pollTraffic() {
  const data = await runBridgeJson('traffic')
  if (!data || !data.interfaces) return

  const now = Date.now()
  let currentRx = 0
  let currentTx = 0

  for (const iface of data.interfaces) {
    currentRx += iface.rx_bytes
    currentTx += iface.tx_bytes
  }

  if (liveRate.lastTime > 0) {
    const elapsedSec = (now - liveRate.lastTime) / 1000
    if (elapsedSec > 0.5) {
      const rxDelta = Math.max(0, currentRx - liveRate.lastRx)
      const txDelta = Math.max(0, currentTx - liveRate.lastTx)

      const rxSec = rxDelta / elapsedSec
      const txSec = txDelta / elapsedSec

      liveRate.rxRateStr = formatBytesPerSec(rxSec)
      liveRate.txRateStr = formatBytesPerSec(txSec)
    }
  }

  liveRate.lastRx = currentRx
  liveRate.lastTx = currentTx
  liveRate.lastTime = now
}

function formatBytesPerSec(bps) {
  if (bps >= 1048576) {
    return (bps / 1048576).toFixed(1) + ' MB/s'
  }
  return (bps / 1024).toFixed(0) + ' KB/s'
}

/* Refresh Telemetry */
async function refreshTelemetry(userTriggered = false) {
  if (isRefreshing.value) return
  isRefreshing.value = true
  try {
    const data = await runBridgeJson('info')
    if (data) {
      if (data.wifi) Object.assign(telemetry.wifi, data.wifi)
      if (data.cellular) Object.assign(telemetry.cellular, data.cellular)
      if (data.network) Object.assign(telemetry.network, data.network)
      if (data.tcp) Object.assign(telemetry.tcp, data.tcp)
      if (data.settings) Object.assign(telemetry.settings, data.settings)
    }
    if (userTriggered) showToast('Telemetry updated')
  } catch (e) {
    console.error('Failed to refresh telemetry:', e)
  } finally {
    isRefreshing.value = false
  }
}

/* Speedtest Engine Integration */
const engine = new SpeedTestEngine()

const servers = [
  { id: 'cf', name: 'Cloudflare edge', url: 'https://speed.cloudflare.com' },
  { id: 'tele2', name: 'Tele2 CDN', url: 'https://speed.cloudflare.com' }
]
const selectedServer = ref(servers[0])

const speedtestState = reactive({
  isTesting: false,
  phase: 'idle',
  instantSpeed: 0,
  progressPct: 0,
  ping: 0,
  jitter: 0,
  download: 0,
  upload: 0,
  bytesUsedMb: 0,
  lastTestedAt: null,
  graphPoints: []
})

const speedHistory = ref([])

function loadHistory() {
  try {
    const raw = localStorage.getItem('hypernet_speed_history')
    if (raw) speedHistory.value = JSON.parse(raw)
  } catch {}
}

function saveHistory(item) {
  speedHistory.value.unshift(item)
  if (speedHistory.value.length > 20) speedHistory.value.pop()
  try {
    localStorage.setItem('hypernet_speed_history', JSON.stringify(speedHistory.value))
  } catch {}
}

function clearHistory() {
  speedHistory.value = []
  localStorage.removeItem('hypernet_speed_history')
  showToast('History cleared')
}

const chartSvgPoints = computed(() => {
  const pts = speedtestState.graphPoints
  if (!pts || pts.length < 2) return '0,85 300,85'

  const maxVal = Math.max(10, ...pts) * 1.15
  const stepX = 300 / (pts.length - 1)

  return pts.map((val, idx) => {
    const x = (idx * stepX).toFixed(1)
    const y = (85 - (val / maxVal) * 75).toFixed(1)
    return `${x},${y}`
  }).join(' ')
})

function formatTestPhase(phase) {
  if (phase === 'ping') return 'Testing latency & jitter...'
  if (phase === 'ping_done') return 'Measuring download throughput...'
  if (phase === 'download') return 'Downloading test payload...'
  if (phase === 'upload') return 'Measuring upload throughput...'
  if (phase === 'complete') return 'Test completed'
  return 'Ready'
}

function formatTimeAgo(ts) {
  const sec = Math.round((Date.now() - ts) / 1000)
  if (sec < 60) return `${sec}s ago`
  const min = Math.round(sec / 60)
  if (min < 60) return `${min}m ago`
  return `${Math.round(min / 60)}h ago`
}

async function toggleSpeedtest() {
  if (speedtestState.isTesting) {
    engine.abort()
    speedtestState.isTesting = false
    speedtestState.phase = 'idle'
    showToast('Speedtest cancelled')
    return
  }

  speedtestState.isTesting = true
  speedtestState.phase = 'ping'
  speedtestState.instantSpeed = 0
  speedtestState.progressPct = 0
  speedtestState.download = 0
  speedtestState.upload = 0
  speedtestState.ping = 0
  speedtestState.jitter = 0
  speedtestState.bytesUsedMb = 0
  speedtestState.graphPoints = []

  try {
    const res = await engine.runTest({
      serverUrl: selectedServer.value.url,
      serverName: selectedServer.value.name,
      durationSec: 9
    }, (progress) => {
      speedtestState.phase = progress.phase
      speedtestState.progressPct = progress.progressPct || 0
      speedtestState.instantSpeed = progress.speedMbps || 0
      if (progress.pingMs) speedtestState.ping = progress.pingMs
      if (progress.jitterMs) speedtestState.jitter = progress.jitterMs
      if (progress.downloadMbps) speedtestState.download = progress.downloadMbps
      if (progress.uploadMbps) speedtestState.upload = progress.uploadMbps
      if (progress.bytesTransferred) speedtestState.bytesUsedMb = progress.bytesTransferred / 1048576
      if (progress.graphPoints) speedtestState.graphPoints = progress.graphPoints
    })

    if (res) {
      speedtestState.lastTestedAt = Date.now()
      saveHistory({
        date: new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }),
        download: res.download,
        upload: res.upload,
        ping: res.ping,
        server: res.server
      })
      showToast('Speedtest completed')
    }
  } catch (err) {
    if (speedtestState.phase !== 'cancelled') {
      showToast('Test failed, check connectivity')
    }
  } finally {
    speedtestState.isTesting = false
  }
}

/* Cellular Modes */
const cellularModes = [
  { id: 'auto', title: 'Global auto', desc: 'Default automatic network mode (5G / 4G / 3G / 2G)' },
  { id: '5g_only', title: '5G only (NR)', desc: 'Strictly locks to 5G New Radio bands' },
  { id: '5g_lte', title: '5G / 4G preferred', desc: 'Allows high-speed 5G with 4G LTE fallback' },
  { id: 'lte_only', title: '4G only (LTE)', desc: 'Prevents 5G fallback and battery drain' },
  { id: '3g_only', title: '3G only', desc: 'Legacy WCDMA / HSPA networks' },
  { id: '2g_only', title: '2G only (GSM)', desc: 'Ultra battery saving for voice/SMS' }
]

const selectedCellularMode = ref('auto')

async function applyCellularMode(modeId) {
  selectedCellularMode.value = modeId
  const res = await runBridgeJson('set_mode', 0, modeId)
  if (res && res.success) {
    showToast(`Locked mode to ${modeId}`)
    refreshTelemetry()
  } else {
    showToast('Failed to apply network mode')
  }
}

/* TCP Congestion Control */
async function applyTcpCc(algo) {
  telemetry.tcp.current_cc = algo
  const res = await runBridgeJson('set_tcp_cc', algo)
  if (res && res.success) {
    showToast(`TCP congestion set to ${algo}`)
  } else {
    showToast('Kernel does not support this algorithm')
  }
}

/* TCP Buffer Profiles */
const activeBufferProfile = ref('stock')
const bufferProfileDesc = computed(() => {
  if (activeBufferProfile.value === 'gaming') return 'Reduced buffer queues to prevent latency spikes in competitive multiplayer.'
  if (activeBufferProfile.value === 'throughput') return 'Expanded 16MB TCP window sizes for full saturation during large downloads.'
  return 'Standard kernel memory buffer parameters.'
})

async function applyBufferProfile(prof) {
  activeBufferProfile.value = prof
  const res = await runBridgeJson('set_tcp_profile', prof)
  if (res && res.success) {
    showToast(`Applied ${prof} profile`)
  }
}

/* Private DNS */
const dnsOptions = [
  { id: 'off', name: 'Off (System default)', mode: 'off', host: '' },
  { id: 'cf', name: 'Cloudflare 1.1.1.1', mode: 'hostname', host: 'one.one.one.one' },
  { id: 'adguard', name: 'AdGuard Ad-blocking', mode: 'hostname', host: 'dns.adguard-dns.com' },
  { id: 'quad9', name: 'Quad9 Security', mode: 'hostname', host: 'dns.quad9.net' },
  { id: 'google', name: 'Google DNS', mode: 'hostname', host: 'dns.google' }
]

const selectedDns = computed(() => {
  const host = telemetry.settings.private_dns_specifier
  const match = dnsOptions.find(d => d.host === host)
  return match ? match.id : (telemetry.settings.private_dns_mode === 'hostname' ? 'custom' : 'off')
})

async function applyPrivateDns(option) {
  const res = await runBridgeJson('set_dns', option.mode, option.host)
  if (res && res.success) {
    telemetry.settings.private_dns_mode = option.mode
    telemetry.settings.private_dns_specifier = option.host
    showToast(`DNS set to ${option.name}`)
  }
}

/* System Tweaks */
async function toggleTweak(name, val) {
  await runBridgeJson('set_tweak', name, val ? 1 : 0)
  showToast('Setting updated')
}

/* DNS Benchmark */
const dnsBenchmarking = ref(false)
const dnsResults = ref([])

async function runDnsBenchmark() {
  dnsBenchmarking.value = true
  try {
    const res = await runBridgeJson('dns_bench')
    if (Array.isArray(res)) {
      dnsResults.value = res.sort((a, b) => a.latency_ms - b.latency_ms)
      showToast('DNS benchmark finished')
    }
  } catch (e) {
    showToast('Benchmark failed')
  } finally {
    dnsBenchmarking.value = false
  }
}

/* Modal Dialog State */
const modalState = reactive({
  visible: false,
  title: '',
  desc: '',
  action: null
})

function confirmRadioRefresh() {
  modalState.title = 'Refresh cellular radio?'
  modalState.desc = 'This temporarily cycles airplane mode for 1 second to drop stuck data sessions and reconnect to the strongest cell tower. Internet will pause for 2 seconds.'
  modalState.action = async () => {
    modalState.visible = false
    showToast('Refreshing radio link...')
    await runBridge('radio_refresh')
    setTimeout(refreshTelemetry, 2500)
  }
  modalState.visible = true
}

function onModalConfirm() {
  if (modalState.action) modalState.action()
}

/* Console tab commands */
const consoleOutput = ref('')

async function runConsoleCmd(cmdType) {
  consoleOutput.value = `[${new Date().toLocaleTimeString()}] Executing ${cmdType}...\n`
  let out = ''
  if (cmdType === 'info') {
    out = await runBridge('info')
  } else if (cmdType === 'routes') {
    out = await execCommand('ip route show table 0 2>/dev/null || ip route')
  } else if (cmdType === 'ping') {
    out = await runBridge('ping', '1.1.1.1', '3')
  } else if (cmdType === 'dns_bench') {
    out = await runBridge('dns_bench')
  }
  consoleOutput.value += (out || 'Command finished with empty output.') + '\n'
}

onMounted(() => {
  loadHistory()
  refreshTelemetry()
  pollTraffic()
  trafficInterval = setInterval(pollTraffic, 1500)
})

onUnmounted(() => {
  if (trafficInterval) clearInterval(trafficInterval)
  engine.abort()
})
</script>

<style scoped>
.spin-anim {
  animation: spin 0.8s linear infinite;
}

@keyframes spin {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}
</style>
