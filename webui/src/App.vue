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
      <div v-show="activeTab === 'speed'" style="display: flex; flex-direction: column; gap: 10px;">
        <!-- Speed Hero Display Card -->
        <section class="md3-card speed-hero-container">
          <!-- Top Row: Route & Server pills -->
          <div style="display: flex; justify-content: space-between; width: 100%; align-items: center;">
            <span class="badge-pill">
              <Icons :name="telemetry.wifi.connected ? 'wifi' : 'radio'" :size="11" />
              <span>{{ telemetry.network.active_iface ? `${telemetry.network.active_iface} (${telemetry.wifi.ip || 'online'})` : 'Auto interface' }}</span>
            </span>
            <span
              class="badge-pill active"
              style="cursor: pointer;"
              @click="toggleServerSelect"
              title="Click to switch server"
            >
              <Icons name="server" :size="11" />
              <span>{{ selectedServer.name }}</span>
            </span>
          </div>

          <!-- Digital Speedout Readout -->
          <div style="text-align: center; margin: 16px 0 4px 0;">
            <div class="speed-digital-value">{{ speedtestState.instantSpeed.toFixed(1) }}</div>
            <div class="speed-digital-unit">
              {{ speedtestState.phase === 'upload' ? 'Upload Mbps' : (speedtestState.phase === 'download' ? 'Download Mbps' : 'Mbps throughput') }}
            </div>
          </div>

          <!-- Phase & Progress indicator -->
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

          <!-- Integrated Rolling Sparkline Chart -->
          <div class="chart-container" style="margin-bottom: 14px;">
            <svg viewBox="0 0 300 90" width="100%" height="90" preserveAspectRatio="none">
              <line x1="0" y1="22" x2="300" y2="22" stroke="var(--surface-container-high)" stroke-dasharray="3,3" />
              <line x1="0" y1="45" x2="300" y2="45" stroke="var(--surface-container-high)" stroke-dasharray="3,3" />
              <line x1="0" y1="68" x2="300" y2="68" stroke="var(--surface-container-high)" stroke-dasharray="3,3" />
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

          <!-- Primary Action Button -->
          <button
            class="btn btn-block"
            :class="speedtestState.isTesting ? 'btn-danger' : 'btn-primary'"
            @click="toggleSpeedtest"
          >
            <Icons :name="speedtestState.isTesting ? 'stop' : 'play'" :size="14" />
            <span>{{ speedtestState.isTesting ? 'Stop test' : 'Start speedtest' }}</span>
          </button>
        </section>

        <!-- Live Metrics 3-box Grid -->
        <div class="metrics-grid-3">
          <div class="metric-box">
            <span class="metric-box-label">Ping / Jitter</span>
            <span class="metric-box-val">
              {{ speedtestState.ping ? speedtestState.ping + ' ms' : '--' }}
            </span>
            <span style="font-size: 10px; color: var(--on-surface-variant);">
              {{ speedtestState.jitter ? '±' + speedtestState.jitter + ' ms' : 'latency' }}
            </span>
          </div>
          <div class="metric-box">
            <span class="metric-box-label">Download</span>
            <span class="metric-box-val">
              {{ speedtestState.download ? speedtestState.download + ' M' : '--' }}
            </span>
            <span style="font-size: 10px; color: var(--on-surface-variant);">
              {{ speedtestState.bytesUsedMb ? speedtestState.bytesUsedMb.toFixed(1) + ' MB' : 'down' }}
            </span>
          </div>
          <div class="metric-box">
            <span class="metric-box-label">Upload</span>
            <span class="metric-box-val">
              {{ speedtestState.upload ? speedtestState.upload + ' M' : '--' }}
            </span>
            <span style="font-size: 10px; color: var(--on-surface-variant);">
              upload
            </span>
          </div>
        </div>

        <!-- Recent Results Card -->
        <section class="md3-card" v-if="speedHistory.length > 0">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="clock" :size="13" />
              <span>Recent results</span>
            </span>
            <button class="btn btn-sm btn-secondary" @click="clearHistory">Clear</button>
          </div>
          <div v-for="(h, idx) in speedHistory.slice(0, 4)" :key="idx" class="kv-row">
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
      <div v-show="activeTab === 'diagnostics'" style="display: flex; flex-direction: column; gap: 10px;">
        <!-- Live Real-Time Throughput Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="zap" :size="14" />
              <span>Real-time interface rate</span>
            </span>
            <span class="badge-pill online">{{ telemetry.network.active_iface || 'Traffic' }}</span>
          </div>
          <div class="stat-grid-2x2">
            <div class="stat-cell">
              <span class="stat-cell-label">Current download</span>
              <span class="stat-cell-val">{{ liveRate.rxRateStr }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">Current upload</span>
              <span class="stat-cell-val">{{ liveRate.txRateStr }}</span>
            </div>
          </div>
        </section>

        <!-- Wi-Fi Card (Header with Icon Badge + 2x2 Grid) -->
        <section class="md3-card">
          <div style="display: flex; align-items: center; justify-content: space-between;">
            <div style="display: flex; align-items: center; gap: 10px;">
              <div class="icon-badge">
                <Icons name="wifi" :size="18" />
              </div>
              <div>
                <div style="font-size: 13px; font-weight: 600; color: var(--on-bg);">
                  {{ telemetry.wifi.ssid }}
                </div>
                <div style="font-size: 11px; color: var(--on-surface-variant);">
                  {{ telemetry.wifi.standard }} • {{ telemetry.wifi.bssid }}
                </div>
              </div>
            </div>
            <span class="badge-pill" :class="telemetry.wifi.connected ? 'online' : 'offline'">
              {{ telemetry.wifi.connected ? 'Connected' : 'Offline' }}
            </span>
          </div>

          <div class="stat-grid-2x2" style="margin-top: 4px;">
            <div class="stat-cell">
              <span class="stat-cell-label">Signal strength</span>
              <span class="stat-cell-val">{{ telemetry.wifi.rssi ? telemetry.wifi.rssi + ' dBm' : '--' }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">Link speed</span>
              <span class="stat-cell-val">{{ telemetry.wifi.link_speed_mbps ? telemetry.wifi.link_speed_mbps + ' Mbps' : '--' }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">Frequency / Band</span>
              <span class="stat-cell-val">{{ telemetry.wifi.frequency_mhz ? telemetry.wifi.frequency_mhz + ' MHz (' + telemetry.wifi.band + ')' : '--' }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">Gateway IP</span>
              <span class="stat-cell-val">{{ telemetry.network.gateway || '--' }}</span>
            </div>
          </div>
        </section>

        <!-- Cellular Card (Header with Icon Badge + 2x2 Grid) -->
        <section class="md3-card">
          <div style="display: flex; align-items: center; justify-content: space-between;">
            <div style="display: flex; align-items: center; gap: 10px;">
              <div class="icon-badge">
                <Icons name="radio" :size="18" />
              </div>
              <div>
                <div style="font-size: 13px; font-weight: 600; color: var(--on-bg);">
                  {{ telemetry.cellular.operator || 'No SIM detected' }}
                </div>
                <div style="font-size: 11px; color: var(--on-surface-variant);">
                  {{ telemetry.cellular.network_type }} • {{ telemetry.cellular.cell_id > 0 ? 'CID ' + telemetry.cellular.cell_id : 'Cellular active' }}
                </div>
              </div>
            </div>
            <span class="badge-pill">SIM 1</span>
          </div>

          <div class="stat-grid-2x2" style="margin-top: 4px;">
            <div class="stat-cell">
              <span class="stat-cell-label">RSRP / RSRQ</span>
              <span class="stat-cell-val">{{ telemetry.cellular.rsrp ? telemetry.cellular.rsrp + ' dBm' : '--' }} / {{ telemetry.cellular.rsrq ? telemetry.cellular.rsrq + ' dB' : '--' }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">Signal quality (SINR)</span>
              <span class="stat-cell-val">{{ telemetry.cellular.sinr ? telemetry.cellular.sinr + ' dB' : '--' }}</span>
            </div>
            <div class="stat-cell" style="grid-column: span 2;">
              <span class="stat-cell-label">Allowed network bitmask</span>
              <span class="stat-cell-val" style="font-size: 11px;">{{ telemetry.cellular.allowed_types || 'Default carrier mode' }}</span>
            </div>
          </div>
        </section>

        <!-- DNS Benchmark Tool Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="globe" :size="14" />
              <span>DNS latency comparison</span>
            </span>
            <button class="btn btn-sm btn-secondary" :disabled="dnsBenchmarking" @click="runDnsBenchmark">
              <Icons name="refresh" :size="11" :class="{ 'spin-anim': dnsBenchmarking }" />
              <span>{{ dnsBenchmarking ? 'Testing...' : 'Benchmark' }}</span>
            </button>
          </div>
          <div v-if="dnsResults.length > 0" class="dns-grid-2col" style="margin-top: 2px;">
            <div v-for="d in dnsResults" :key="d.name" class="dns-chip">
              <span style="color: var(--on-surface-variant);">{{ d.name }}</span>
              <span style="font-weight: 600; font-variant-numeric: tabular-nums;" :style="{ color: d.latency_ms > 0 && d.latency_ms < 25 ? 'var(--primary)' : 'var(--on-bg)' }">
                {{ d.latency_ms > 0 ? d.latency_ms.toFixed(1) + ' ms' : 'Timeout' }}
              </span>
            </div>
          </div>
          <div v-else style="font-size: 11px; color: var(--on-surface-variant); padding: 4px 0;">
            Benchmark lookup speeds across Cloudflare, Google, Quad9, and AdGuard resolvers.
          </div>
        </section>
      </div>

      <!-- 3. CELLULAR TAB -->
      <div v-show="activeTab === 'cellular'" style="display: flex; flex-direction: column; gap: 10px;">
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="radio" :size="14" />
              <span>Cellular band & mode locker</span>
            </span>
            <span class="badge-pill">Slot 0 (SIM 1)</span>
          </div>
          <p style="font-size: 11px; color: var(--on-surface-variant); line-height: 1.35;">
            Lock radio hardware strictly to specific cellular generations to prevent unwanted network drops.
          </p>

          <!-- 2-Column Mode Grid -->
          <div class="mode-grid-2col" style="margin-top: 4px;">
            <div
              v-for="mode in cellularModes"
              :key="mode.id"
              class="mode-card-compact"
              :class="{ selected: selectedCellularMode === mode.id }"
              @click="applyCellularMode(mode.id)"
            >
              <div class="mode-card-top">
                <span class="mode-card-title">{{ mode.title }}</span>
                <Icons v-if="selectedCellularMode === mode.id" name="check" :size="13" style="color: var(--primary);" />
              </div>
              <span class="mode-card-sub">{{ mode.desc }}</span>
            </div>
          </div>

          <!-- Integrated Tower Refresh Action -->
          <div style="border-top: 1px solid var(--surface-container-high); padding-top: 10px; margin-top: 6px;">
            <button class="btn btn-secondary btn-block" @click="confirmRadioRefresh">
              <Icons name="refresh" :size="13" />
              <span>Refresh cellular tower attachment</span>
            </button>
          </div>
        </section>
      </div>

      <!-- 4. OPTIMIZER TAB -->
      <div v-show="activeTab === 'optimizer'" style="display: flex; flex-direction: column; gap: 10px;">
        <!-- Card 1: Kernel TCP Optimization -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="zap" :size="14" />
              <span>Kernel TCP tuning</span>
            </span>
            <span class="badge-pill active">{{ telemetry.tcp.current_cc }}</span>
          </div>

          <!-- TCP Algorithm Selector -->
          <div style="display: flex; flex-direction: column; gap: 4px;">
            <span style="font-size: 11px; color: var(--on-surface-variant);">Congestion control algorithm</span>
            <div class="segmented-control">
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
          </div>

          <!-- Buffer Profiles Selector -->
          <div style="display: flex; flex-direction: column; gap: 4px; margin-top: 4px;">
            <div style="display: flex; justify-content: space-between; align-items: center;">
              <span style="font-size: 11px; color: var(--on-surface-variant);">Memory buffer profile</span>
              <span style="font-size: 10px; color: var(--primary);">{{ activeBufferProfile }}</span>
            </div>
            <div class="segmented-control">
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
            <span style="font-size: 10px; color: var(--on-surface-variant); margin-top: 2px;">
              {{ bufferProfileDesc }}
            </span>
          </div>

          <!-- TCP Fast Open Toggle -->
          <div class="switch-row" style="margin-top: 4px;">
            <div class="switch-label-col">
              <span class="switch-title">TCP Fast Open (TFO)</span>
              <span class="switch-desc">Eliminates handshake round-trips for repeat sessions</span>
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

        <!-- Card 2: DNS & System Handover -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="globe" :size="14" />
              <span>DNS & network handover</span>
            </span>
            <span class="badge-pill">{{ telemetry.settings.private_dns_mode }}</span>
          </div>

          <!-- Private DNS 2-column list -->
          <div class="dns-grid-2col" style="margin-top: 2px;">
            <div
              v-for="dns in dnsOptions"
              :key="dns.id"
              class="dns-chip"
              style="cursor: pointer;"
              :style="{ borderColor: selectedDns === dns.id ? 'var(--outline)' : 'var(--surface-container-high)' }"
              @click="applyPrivateDns(dns)"
            >
              <div>
                <div style="font-weight: 500; color: var(--on-bg);">{{ dns.name }}</div>
                <div style="font-size: 10px; color: var(--on-surface-variant);">{{ dns.host || 'Default' }}</div>
              </div>
              <Icons v-if="selectedDns === dns.id" name="check" :size="13" style="color: var(--primary);" />
            </div>
          </div>

          <!-- Wi-Fi Scan Throttling -->
          <div class="switch-row" style="margin-top: 4px;">
            <div class="switch-label-col">
              <span class="switch-title">Wi-Fi scan throttling</span>
              <span class="switch-desc">Turn off to accelerate roaming between access points</span>
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
              <span class="switch-desc">Keep cellular warm while on Wi-Fi for zero-lag handovers</span>
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
        </section>
      </div>

      <!-- 5. CONSOLE TAB -->
      <div v-show="activeTab === 'console'" style="display: flex; flex-direction: column; gap: 10px;">
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="terminal" :size="14" />
              <span>Network diagnostic console</span>
            </span>
            <button class="btn btn-sm btn-secondary" @click="consoleOutput = ''">Clear</button>
          </div>
          <div style="display: flex; gap: 6px; flex-wrap: wrap; margin-top: 2px;">
            <button class="btn btn-sm btn-secondary" @click="runConsoleCmd('info')">Status</button>
            <button class="btn btn-sm btn-secondary" @click="runConsoleCmd('routes')">Routes</button>
            <button class="btn btn-sm btn-secondary" @click="runConsoleCmd('speedtest')">Speedtest</button>
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
  { id: 'cf', name: 'Cloudflare Anycast', url: 'https://speed.cloudflare.com' },
  { id: 'tele2', name: 'Tele2 Edge', url: 'https://speed.cloudflare.com' }
]
const selectedServer = ref(servers[0])

function toggleServerSelect() {
  const currentIdx = servers.findIndex(s => s.id === selectedServer.value.id)
  const nextIdx = (currentIdx + 1) % servers.length
  selectedServer.value = servers[nextIdx]
  showToast(`Switched server to ${selectedServer.value.name}`)
}

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
  if (phase === 'ping') return 'Measuring latency & jitter...'
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
      showToast('Test failed, check connection')
    }
  } finally {
    speedtestState.isTesting = false
  }
}

/* Cellular Modes */
const cellularModes = [
  { id: 'auto', title: 'Global auto', desc: '5G / 4G / 3G / 2G multi-mode' },
  { id: '5g_only', title: '5G only (NR)', desc: 'Strictly 5G New Radio bands' },
  { id: '5g_lte', title: '5G / 4G preferred', desc: '5G with 4G LTE fallback' },
  { id: 'lte_only', title: '4G only (LTE)', desc: 'LTE only, prevents 5G drain' },
  { id: '3g_only', title: '3G only', desc: 'Legacy WCDMA / HSPA' },
  { id: '2g_only', title: '2G only (GSM)', desc: 'Ultra-low battery voice/SMS' }
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
  if (activeBufferProfile.value === 'gaming') return 'Reduced queue buffers to eliminate multiplayer latency spikes.'
  if (activeBufferProfile.value === 'throughput') return 'Expanded 16MB TCP window sizes for full gigabit downloads.'
  return 'Standard Linux kernel default memory buffers.'
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
  { id: 'off', name: 'Off', mode: 'off', host: '' },
  { id: 'cf', name: 'Cloudflare', mode: 'hostname', host: 'one.one.one.one' },
  { id: 'adguard', name: 'AdGuard', mode: 'hostname', host: 'dns.adguard-dns.com' },
  { id: 'quad9', name: 'Quad9', mode: 'hostname', host: 'dns.quad9.net' },
  { id: 'google', name: 'Google', mode: 'hostname', host: 'dns.google' }
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
      showToast('DNS benchmark completed')
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
  modalState.desc = 'This temporarily cycles airplane mode for 1 second to drop stuck data sessions and reconnect to the strongest cell tower. Connectivity will pause for 2 seconds.'
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

/* Console Tab Commands */
const consoleOutput = ref('')

async function runConsoleCmd(cmdType) {
  consoleOutput.value = `[${new Date().toLocaleTimeString()}] Executing ${cmdType}...\n`
  let out = ''
  if (cmdType === 'info') {
    out = await runBridge('info')
  } else if (cmdType === 'routes') {
    out = await execCommand('ip route show table 0 2>/dev/null || ip route')
  } else if (cmdType === 'speedtest') {
    out = await runBridge('speedtest')
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
