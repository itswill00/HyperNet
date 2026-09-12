<template>
  <div class="app-shell">
    <!-- Sticky Header -->
    <header class="page-header">
      <div style="min-width: 0; flex: 1;">
        <div class="page-header-title">HyperNet</div>
        <div class="page-header-sub truncate-text">
          {{ telemetry.device.brand ? `${telemetry.device.brand} ${telemetry.device.model} • Android ${telemetry.device.android_ver}` : 'Network toolkit & speedtest' }}
        </div>
      </div>
      <div style="display: flex; align-items: center; gap: 8px; flex-shrink: 0;">
        <span class="badge-pill" :class="isOnline ? 'online' : 'offline'" style="max-width: 150px;">
          <Icons :name="activeConnectionIcon" :size="11" />
          <span class="truncate-text">{{ activeConnectionLabel }}</span>
        </span>
        <span class="badge-pill" style="cursor: pointer; user-select: none;" @click="refreshTelemetry(true)" title="Tap to refresh">
          <Icons name="refresh" :size="11" :class="{ 'spin-anim': isRefreshing }" />
          <span>v1.0.0</span>
        </span>
      </div>
    </header>

    <!-- Navigation Tabs -->
    <div class="tabs-container">
      <div class="tabs-control">
        <button class="tab-btn" :class="{ active: activeTab === 'speed' }" @click="activeTab = 'speed'">
          <Icons name="gauge" :size="12" />
          <span>Speedtest</span>
        </button>
        <button class="tab-btn" :class="{ active: activeTab === 'diagnostics' }" @click="activeTab = 'diagnostics'">
          <Icons name="wifi" :size="12" />
          <span>Diagnostics</span>
        </button>
        <button class="tab-btn" :class="{ active: activeTab === 'cellular' }" @click="activeTab = 'cellular'">
          <Icons name="radio" :size="12" />
          <span>Cellular</span>
        </button>
        <button class="tab-btn" :class="{ active: activeTab === 'optimizer' }" @click="activeTab = 'optimizer'">
          <Icons name="sliders" :size="12" />
          <span>Optimizer</span>
        </button>
        <button class="tab-btn" :class="{ active: activeTab === 'console' }" @click="activeTab = 'console'">
          <Icons name="terminal" :size="12" />
          <span>Console</span>
        </button>
      </div>
    </div>

    <!-- Main Content Area -->
    <main class="content-area">
      <!-- 1. SPEEDTEST TAB -->
      <div v-show="activeTab === 'speed'" style="display: flex; flex-direction: column; gap: 12px;">
        <!-- Unified Hero Card -->
        <section class="md3-card speed-hero-card">
          <!-- Top Row: Route & Server selector -->
          <div class="hero-top-row">
            <div class="hero-iface-badge">
              <Icons :name="isWifiActive ? 'wifi' : 'radio'" :size="11" />
              <span class="truncate-text">{{ activeRouteSummary }}</span>
            </div>
            <div class="hero-server-badge" @click="toggleServerSelect" title="Click to switch server">
              <Icons name="server" :size="11" />
              <span>{{ selectedServer.name }}</span>
            </div>
          </div>

          <!-- Digital Speed Readout & Phase Status -->
          <div class="hero-gauge-area">
            <div class="speed-digital-value">{{ speedtestState.instantSpeed.toFixed(1) }}</div>
            <div class="speed-digital-unit">
              {{ speedtestState.phase === 'upload' ? 'Upload Mbps' : (speedtestState.phase === 'download' ? 'Download Mbps' : 'Mbps throughput') }}
            </div>
            <div class="speed-phase-status">
              <span v-if="speedtestState.isTesting">
                {{ formatTestPhase(speedtestState.phase) }} ({{ speedtestState.progressPct }}%)
              </span>
              <span v-else-if="speedtestState.lastTestedAt">
                Last tested {{ formatTimeAgo(speedtestState.lastTestedAt) }}
              </span>
              <span v-else>
                Ready to test active connection
              </span>
            </div>
          </div>

          <!-- Rolling Sparkline Chart -->
          <div class="chart-container">
            <svg viewBox="0 0 300 80" width="100%" height="80" preserveAspectRatio="none">
              <line x1="0" y1="20" x2="300" y2="20" stroke="var(--surface-container-high)" stroke-dasharray="3,3" />
              <line x1="0" y1="40" x2="300" y2="40" stroke="var(--surface-container-high)" stroke-dasharray="3,3" />
              <line x1="0" y1="60" x2="300" y2="60" stroke="var(--surface-container-high)" stroke-dasharray="3,3" />
              <polyline
                :points="chartSvgPoints"
                fill="none"
                stroke="var(--primary)"
                stroke-width="2.2"
                stroke-linecap="round"
                stroke-linejoin="round"
              />
            </svg>
          </div>

          <!-- Docked 3-Metric Strip (Ping, Download, Upload) -->
          <div class="metric-strip">
            <div class="metric-strip-col">
              <span class="metric-strip-label">Ping</span>
              <span class="metric-strip-val">
                {{ speedtestState.ping ? speedtestState.ping : '--' }}<span class="metric-strip-unit">ms</span>
              </span>
              <span class="metric-strip-sub">
                {{ speedtestState.jitter ? '±' + speedtestState.jitter + ' ms' : 'Latency' }}
              </span>
            </div>
            <div class="metric-strip-col">
              <span class="metric-strip-label">Download</span>
              <span class="metric-strip-val">
                {{ speedtestState.download ? speedtestState.download : '--' }}<span class="metric-strip-unit">Mbps</span>
              </span>
              <span class="metric-strip-sub">
                {{ speedtestState.bytesUsedMb ? speedtestState.bytesUsedMb.toFixed(1) + ' MB' : 'Payload' }}
              </span>
            </div>
            <div class="metric-strip-col">
              <span class="metric-strip-label">Upload</span>
              <span class="metric-strip-val">
                {{ speedtestState.upload ? speedtestState.upload : '--' }}<span class="metric-strip-unit">Mbps</span>
              </span>
              <span class="metric-strip-sub">
                {{ speedtestState.phase === 'upload' ? 'Testing' : (speedtestState.upload ? 'Done' : 'Throughput') }}
              </span>
            </div>
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

        <!-- Recent Results Card -->
        <section class="md3-card" v-if="speedHistory.length > 0">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="clock" :size="13" />
              <span>Recent results</span>
            </span>
            <button class="btn btn-sm btn-secondary" @click="clearHistory">Clear</button>
          </div>
          <div class="history-list">
            <div v-for="(h, idx) in speedHistory.slice(0, 4)" :key="idx" class="history-card">
              <div class="history-top">
                <div class="history-speeds">
                  <span class="speed-down">↓ {{ h.download }}</span>
                  <span class="speed-slash">/</span>
                  <span class="speed-up">↑ {{ h.upload }}</span>
                  <span class="speed-unit">Mbps</span>
                </div>
                <span class="history-ping">{{ h.ping }} ms</span>
              </div>
              <div class="history-bottom">
                <span>{{ h.date }}</span>
                <span>•</span>
                <span class="truncate-text">{{ h.server }}</span>
              </div>
            </div>
          </div>
        </section>
      </div>

      <!-- 2. DIAGNOSTICS TAB -->
      <div v-show="activeTab === 'diagnostics'" style="display: flex; flex-direction: column; gap: 12px;">
        <!-- Network Health Hero Card -->
        <section class="health-hero-card">
          <div class="health-top-row">
            <div class="health-score-badge">
              <span>{{ telemetry.health.score }}</span>
              <span class="health-score-max">/ 100</span>
            </div>
            <span class="health-grade-tag" :class="telemetry.health.grade">
              {{ telemetry.health.grade }}
            </span>
          </div>

          <div class="health-info-col">
            <div class="health-summary">{{ telemetry.health.summary || 'Analyzing network...' }}</div>
            <div class="health-recommendation">{{ telemetry.health.recommendation || 'Evaluating link quality...' }}</div>
          </div>

          <div class="hardware-strip">
            <div class="hardware-col">
              <span class="hw-label">Active route</span>
              <span class="hw-val">{{ telemetry.network.active_iface || '--' }}</span>
            </div>
            <div class="hardware-col">
              <span class="hw-label">Default gateway</span>
              <span class="hw-val">{{ telemetry.network.gateway || '--' }}</span>
            </div>
            <div class="hardware-col">
              <span class="hw-label">Local IP</span>
              <span class="hw-val">{{ telemetry.network.local_ip || telemetry.wifi.ip || '--' }}</span>
            </div>
          </div>
        </section>

        <!-- Live Network Throughput Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="zap" :size="14" />
              <span>Live network throughput</span>
            </span>
            <span class="badge-pill online">{{ telemetry.network.active_iface || 'Active' }}</span>
          </div>
          <div class="stat-grid-2x2">
            <div class="stat-cell">
              <span class="stat-cell-label">Live download</span>
              <span class="stat-cell-val">{{ liveRate.rxRateStr }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">Live upload</span>
              <span class="stat-cell-val">{{ liveRate.txRateStr }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">SoC platform</span>
              <span class="stat-cell-val">{{ telemetry.device.platform || '--' }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">RAM capacity</span>
              <span class="stat-cell-val">{{ telemetry.device.ram_total_mb ? telemetry.device.ram_total_mb + ' MB' : '--' }}</span>
            </div>
          </div>
        </section>

        <!-- Wi-Fi Link State Card -->
        <section class="md3-card" v-if="isWifiActive || telemetry.wifi.connected">
          <div style="display: flex; align-items: center; justify-content: space-between; gap: 8px;">
            <div style="display: flex; align-items: center; gap: 10px; min-width: 0; flex: 1;">
              <div class="icon-badge">
                <Icons name="wifi" :size="18" />
              </div>
              <div style="min-width: 0; flex: 1;">
                <div style="font-size: 13px; font-weight: 600; color: var(--on-bg);" class="truncate-text">
                  {{ (telemetry.wifi.ssid && telemetry.wifi.ssid !== 'unknown') ? telemetry.wifi.ssid : 'Wi-Fi network' }}
                </div>
                <div style="font-size: 11px; color: var(--on-surface-variant);" class="truncate-text">
                  {{ telemetry.wifi.standard !== 'unknown' ? telemetry.wifi.standard : 'Active link' }} • {{ (telemetry.wifi.bssid && telemetry.wifi.bssid !== 'unknown') ? telemetry.wifi.bssid : (telemetry.wifi.ip || telemetry.network.local_ip || 'Connected') }}
                </div>
              </div>
            </div>
            <span class="badge-pill online" style="flex-shrink: 0;">Connected</span>
          </div>

          <div class="stat-grid-2x2" style="margin-top: 2px;">
            <div class="stat-cell">
              <span class="stat-cell-label">Signal strength</span>
              <span class="stat-cell-val">{{ telemetry.wifi.rssi ? telemetry.wifi.rssi + ' dBm' : '--' }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">Link negotiation</span>
              <span class="stat-cell-val">{{ telemetry.wifi.link_speed_mbps ? telemetry.wifi.link_speed_mbps + ' Mbps' : '--' }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">Frequency / Channel</span>
              <span class="stat-cell-val">{{ telemetry.wifi.frequency_mhz ? telemetry.wifi.frequency_mhz + ' MHz (' + telemetry.wifi.band + ')' : '--' }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">Assigned IP</span>
              <span class="stat-cell-val">{{ telemetry.wifi.ip || telemetry.network.local_ip || '--' }}</span>
            </div>
          </div>
        </section>
        <div v-else class="offline-card">
          <Icons name="wifi-off" :size="16" />
          <span>Wi-Fi is currently inactive or disconnected</span>
        </div>

        <!-- Cellular Link State Card -->
        <section class="md3-card" v-if="telemetry.sim.slot0.inserted || telemetry.sim.slot1.inserted">
          <div style="display: flex; align-items: center; justify-content: space-between; gap: 8px;">
            <div style="display: flex; align-items: center; gap: 10px; min-width: 0; flex: 1;">
              <div class="icon-badge">
                <Icons name="radio" :size="18" />
              </div>
              <div style="min-width: 0; flex: 1;">
                <div style="font-size: 13px; font-weight: 600; color: var(--on-bg);" class="truncate-text">
                  {{ telemetry.cellular.operator || 'Cellular carrier' }}
                </div>
                <div style="font-size: 11px; color: var(--on-surface-variant);" class="truncate-text">
                  {{ telemetry.cellular.network_type }} • {{ telemetry.cellular.cell_id > 0 ? 'CID ' + telemetry.cellular.cell_id : 'Registered' }}
                </div>
              </div>
            </div>
            <span class="badge-pill" style="flex-shrink: 0;">SIM {{ telemetry.sim.active_slot + 1 }}</span>
          </div>

          <div class="stat-grid-2x2" style="margin-top: 2px;">
            <div class="stat-cell">
              <span class="stat-cell-label">Signal (RSRP)</span>
              <span class="stat-cell-val">{{ telemetry.cellular.rsrp ? telemetry.cellular.rsrp + ' dBm' : '--' }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">Signal quality (SINR)</span>
              <span class="stat-cell-val">{{ telemetry.cellular.sinr ? telemetry.cellular.sinr + ' dB' : '--' }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">Generation</span>
              <span class="stat-cell-val">{{ telemetry.cellular.network_type || '--' }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">Cell tower ID</span>
              <span class="stat-cell-val">{{ telemetry.cellular.cell_id > 0 ? telemetry.cellular.cell_id : '--' }}</span>
            </div>
          </div>
        </section>
        <div v-else class="offline-card">
          <Icons name="alert" :size="16" />
          <span>No cellular SIM card detected in device</span>
        </div>

        <!-- DNS Benchmark Tool Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="globe" :size="14" />
              <span>DNS latency comparison</span>
            </span>
            <div style="display: flex; gap: 6px;">
              <button
                v-if="dnsResults.length > 0 && fastestDns"
                class="btn btn-sm btn-primary"
                @click="applyFastestDns"
              >
                <span>Apply {{ fastestDns.name }}</span>
              </button>
              <button class="btn btn-sm btn-secondary" :disabled="dnsBenchmarking" @click="runDnsBenchmark">
                <Icons name="refresh" :size="11" :class="{ 'spin-anim': dnsBenchmarking }" />
                <span>{{ dnsBenchmarking ? 'Testing...' : 'Benchmark' }}</span>
              </button>
            </div>
          </div>
          <div v-if="dnsResults.length > 0" class="dns-grid-2col" style="margin-top: 2px;">
            <div v-for="d in dnsResults" :key="d.name" class="dns-chip">
              <span style="color: var(--on-surface-variant);" class="truncate-text">{{ d.name }}</span>
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
      <div v-show="activeTab === 'cellular'" style="display: flex; flex-direction: column; gap: 12px;">
        <!-- Multi-SIM Selector Strip -->
        <div v-if="telemetry.sim.slot0.inserted || telemetry.sim.slot1.inserted" class="sim-selector-strip">
          <button
            class="sim-selector-btn"
            :class="{ active: selectedSimSlot === 0, disabled: !telemetry.sim.slot0.inserted }"
            :disabled="!telemetry.sim.slot0.inserted"
            @click="selectedSimSlot = 0"
          >
            <div style="display: flex; align-items: center; gap: 6px; min-width: 0;">
              <Icons name="radio" :size="12" />
              <span class="truncate-text">SIM 1: {{ telemetry.sim.slot0.operator || (telemetry.sim.slot0.inserted ? 'Active' : 'Empty') }}</span>
            </div>
            <span v-if="telemetry.sim.active_slot === 0" class="sim-data-badge">Data</span>
          </button>
          <button
            class="sim-selector-btn"
            :class="{ active: selectedSimSlot === 1, disabled: !telemetry.sim.slot1.inserted }"
            :disabled="!telemetry.sim.slot1.inserted"
            @click="selectedSimSlot = 1"
          >
            <div style="display: flex; align-items: center; gap: 6px; min-width: 0;">
              <Icons name="radio" :size="12" />
              <span class="truncate-text">SIM 2: {{ telemetry.sim.slot1.operator || (telemetry.sim.slot1.inserted ? 'Active' : 'Empty') }}</span>
            </div>
            <span v-if="telemetry.sim.active_slot === 1" class="sim-data-badge">Data</span>
          </button>
        </div>

        <!-- Live Radio State Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <div style="display: flex; align-items: center; gap: 8px; min-width: 0; flex: 1;">
              <Icons name="radio" :size="14" />
              <span class="card-title truncate-text">{{ (selectedSimSlot === 0 ? telemetry.sim.slot0.operator : telemetry.sim.slot1.operator) || telemetry.cellular.operator || 'Cellular radio' }}</span>
            </div>
            <span class="badge-pill active">{{ telemetry.cellular.network_type || 'Active' }}</span>
          </div>

          <div class="metric-strip" style="margin-top: 2px;">
            <div class="metric-strip-col">
              <span class="metric-strip-label">RSRP</span>
              <span class="metric-strip-val">{{ telemetry.cellular.rsrp ? telemetry.cellular.rsrp : '--' }}<span class="metric-strip-unit">dBm</span></span>
              <span class="metric-strip-sub">Signal</span>
            </div>
            <div class="metric-strip-col">
              <span class="metric-strip-label">SINR</span>
              <span class="metric-strip-val">{{ telemetry.cellular.sinr ? telemetry.cellular.sinr : '--' }}<span class="metric-strip-unit">dB</span></span>
              <span class="metric-strip-sub">Quality</span>
            </div>
            <div class="metric-strip-col">
              <span class="metric-strip-label">Tower</span>
              <span class="metric-strip-val">{{ telemetry.cellular.cell_id > 0 ? telemetry.cellular.cell_id : '--' }}</span>
              <span class="metric-strip-sub">Cell ID</span>
            </div>
          </div>

          <button class="btn btn-secondary btn-block" @click="confirmRadioRefresh" style="margin-top: 2px;">
            <Icons name="refresh" :size="13" />
            <span>Refresh tower attachment</span>
          </button>
        </section>

        <!-- Band & Mode Locker Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="sliders" :size="14" />
              <span>Cellular band &amp; mode locker</span>
            </span>
            <span class="badge-pill">SIM {{ selectedSimSlot + 1 }}</span>
          </div>
          <p style="font-size: 11px; color: var(--on-surface-variant); line-height: 1.4;">
            Lock modem hardware to specific cellular generations to prevent unwanted network drops.
          </p>

          <div class="mode-grid-2col" style="margin-top: 2px;">
            <div
              v-for="mode in cellularModes"
              :key="mode.id"
              class="mode-card-compact"
              :class="{ selected: selectedCellularMode === mode.id }"
              @click="applyCellularMode(mode.id)"
            >
              <div class="mode-card-top">
                <span class="mode-card-title">{{ mode.title }}</span>
                <Icons v-if="selectedCellularMode === mode.id" name="check" :size="13" style="color: var(--primary); flex-shrink: 0;" />
              </div>
              <span class="mode-card-sub">{{ mode.desc }}</span>
            </div>
          </div>
        </section>
      </div>

      <!-- 4. OPTIMIZER TAB -->
      <div v-show="activeTab === 'optimizer'" style="display: flex; flex-direction: column; gap: 12px;">
        <!-- Intelligent Auto-Tuner Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <div style="display: flex; align-items: center; gap: 8px;">
              <div class="icon-badge">
                <Icons name="zap" :size="16" />
              </div>
              <div>
                <div class="card-title">Intelligent auto-tuning</div>
                <div style="font-size: 11px; color: var(--on-surface-variant);">
                  Hardware-tailored network stack &amp; DNS calibration
                </div>
              </div>
            </div>
            <span class="badge-pill active">{{ telemetry.device.ram_tier || 'standard' }} tier</span>
          </div>

          <p style="font-size: 11.5px; color: var(--on-surface-variant); line-height: 1.45;">
            Analyzes phone hardware profile, benchmarks DNS latency, and tunes kernel TCP parameters for peak network performance.
          </p>

          <div class="hardware-strip">
            <div class="hardware-col">
              <span class="hw-label">Device</span>
              <span class="hw-val truncate-text">{{ telemetry.device.brand }} {{ telemetry.device.model }}</span>
            </div>
            <div class="hardware-col">
              <span class="hw-label">SoC platform</span>
              <span class="hw-val truncate-text">{{ telemetry.device.platform || 'Universal' }}</span>
            </div>
            <div class="hardware-col">
              <span class="hw-label">RAM capacity</span>
              <span class="hw-val truncate-text">{{ telemetry.device.ram_total_mb }} MB</span>
            </div>
          </div>

          <button class="btn btn-primary btn-block" :disabled="isOptimizing" @click="runSmartOptimize">
            <Icons name="sliders" :size="14" :class="{ 'spin-anim': isOptimizing }" />
            <span>{{ isOptimizing ? 'Calibrating network stack...' : 'Auto-tune network stack' }}</span>
          </button>

          <div v-if="smartOptResult" class="tune-result-box">
            <Icons name="check" :size="13" style="color: var(--primary); flex-shrink: 0;" />
            <span>
              Calibrated with {{ smartOptResult.tcp_cc }} CC, {{ smartOptResult.buffer_profile }} buffers, and {{ smartOptResult.best_dns }} resolver ({{ smartOptResult.best_dns_latency_ms }} ms).
            </span>
          </div>
        </section>

        <!-- Kernel TCP Tuning Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="sliders" :size="14" />
              <span>Kernel TCP tuning</span>
            </span>
            <span class="badge-pill active">{{ telemetry.tcp.current_cc }}</span>
          </div>

          <!-- TCP Algorithm -->
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

          <!-- Buffer Profiles -->
          <div style="display: flex; flex-direction: column; gap: 4px; margin-top: 4px;">
            <div style="display: flex; justify-content: space-between; align-items: center;">
              <span style="font-size: 11px; color: var(--on-surface-variant);">Memory buffer profile</span>
              <span style="font-size: 10.5px; color: var(--primary); font-weight: 500;">{{ activeBufferProfile }}</span>
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

          <!-- TFO Switch with Authentic MD3 Switch -->
          <div class="switch-row" style="margin-top: 4px;">
            <div class="switch-label-col">
              <span class="switch-title">TCP Fast Open (TFO)</span>
              <span class="switch-desc">Eliminates handshake round-trips for repeat sessions</span>
            </div>
            <label class="md3-switch">
              <input
                type="checkbox"
                :checked="telemetry.tcp.fastopen > 0"
                @change="toggleTweak('fast_open', telemetry.tcp.fastopen > 0 ? 0 : 1)"
              />
              <span class="md3-switch-track">
                <span class="md3-switch-thumb"></span>
              </span>
            </label>
          </div>
        </section>

        <!-- DNS & Network Handover Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="globe" :size="14" />
              <span>DNS &amp; network handover</span>
            </span>
            <span class="badge-pill">{{ telemetry.settings.private_dns_mode }}</span>
          </div>

          <!-- System Default DNS Row -->
          <div style="display: flex; flex-direction: column; gap: 6px; margin-top: 2px;">
            <span style="font-size: 11px; color: var(--on-surface-variant);">Private DNS resolver</span>
            
            <div
              class="dns-option-row"
              :class="{ selected: selectedDns === 'off' }"
              @click="applyPrivateDns(dnsOptions[0])"
            >
              <div>
                <div style="font-size: 12px; font-weight: 600; color: var(--on-bg);">System default</div>
                <div style="font-size: 10.5px; color: var(--on-surface-variant);">Standard carrier / Wi-Fi resolver</div>
              </div>
              <Icons v-if="selectedDns === 'off'" name="check" :size="13" style="color: var(--primary);" />
            </div>

            <!-- 2x2 Grid for Encrypted Providers -->
            <div class="dns-grid-2col">
              <div
                v-for="dns in dnsOptions.slice(1)"
                :key="dns.id"
                class="dns-chip"
                :class="{ selected: selectedDns === dns.id }"
                @click="applyPrivateDns(dns)"
              >
                <div style="min-width: 0; flex: 1;">
                  <div style="font-weight: 600; color: var(--on-bg);" class="truncate-text">{{ dns.name }}</div>
                  <div style="font-size: 10px; color: var(--on-surface-variant);" class="truncate-text">{{ dns.host }}</div>
                </div>
                <Icons v-if="selectedDns === dns.id" name="check" :size="13" style="color: var(--primary); flex-shrink: 0; margin-left: 4px;" />
              </div>
            </div>
          </div>

          <!-- Wi-Fi Scan Throttling Switch -->
          <div class="switch-row" style="margin-top: 6px;">
            <div class="switch-label-col">
              <span class="switch-title">Wi-Fi scan throttling</span>
              <span class="switch-desc">Turn off to accelerate roaming between access points</span>
            </div>
            <label class="md3-switch">
              <input
                type="checkbox"
                v-model="telemetry.settings.wifi_scan_throttle"
                @change="toggleTweak('wifi_throttle', telemetry.settings.wifi_scan_throttle)"
              />
              <span class="md3-switch-track">
                <span class="md3-switch-thumb"></span>
              </span>
            </label>
          </div>

          <!-- Mobile Data Always On Switch -->
          <div class="switch-row">
            <div class="switch-label-col">
              <span class="switch-title">Mobile data always active</span>
              <span class="switch-desc">Keep cellular warm while on Wi-Fi for zero-lag handovers</span>
            </div>
            <label class="md3-switch">
              <input
                type="checkbox"
                v-model="telemetry.settings.mobile_data_always_on"
                @change="toggleTweak('mobile_data_always', telemetry.settings.mobile_data_always_on)"
              />
              <span class="md3-switch-track">
                <span class="md3-switch-thumb"></span>
              </span>
            </label>
          </div>

          <!-- Restore Defaults Button -->
          <button class="btn btn-secondary btn-block" @click="restoreDefaults" style="margin-top: 4px;">
            <Icons name="refresh" :size="13" />
            <span>Restore kernel defaults</span>
          </button>
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
          <div style="display: flex; gap: 6px; flex-wrap: wrap; margin-top: 2px;">
            <button class="btn btn-sm btn-secondary" @click="runConsoleCmd('info')">Status</button>
            <button class="btn btn-sm btn-secondary" @click="runConsoleCmd('routes')">Routes</button>
            <button class="btn btn-sm btn-secondary" @click="runConsoleCmd('speedtest')">Speedtest</button>
            <button class="btn btn-sm btn-secondary" @click="runConsoleCmd('ping')">Ping 1.1.1.1</button>
            <button class="btn btn-sm btn-secondary" @click="runConsoleCmd('dns_bench')">DNS bench</button>
            <button class="btn btn-sm btn-secondary" @click="runConsoleCmd('auto_tune')">Auto-tune</button>
          </div>
          <div class="console-box" style="margin-top: 8px;">
            {{ consoleOutput || 'Console ready. Execute diagnostic commands above.' }}
          </div>
        </section>
      </div>
    </main>

    <!-- In-App Modal Dialog -->
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
const browserOnline = ref(navigator.onLine)
if (typeof window !== 'undefined') {
  window.addEventListener('online', () => { browserOnline.value = true })
  window.addEventListener('offline', () => { browserOnline.value = false })
}

const isOnline = computed(() => {
  if (telemetry.wifi.connected || (telemetry.wifi.ip && telemetry.wifi.ip.length > 5)) return true
  if (telemetry.network.active_iface && telemetry.network.active_iface.length > 0) return true
  if (telemetry.cellular.operator && telemetry.cellular.operator.length > 0 && telemetry.cellular.network_type !== 'unknown') return true
  return browserOnline.value
})

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
  device: {
    brand: '',
    model: '',
    platform: '',
    android_ver: '',
    api_level: 0,
    ram_total_mb: 0,
    ram_tier: 'standard'
  },
  sim: {
    active_slot: 0,
    active_subid: 1,
    slot0: { inserted: false, state: '', operator: '' },
    slot1: { inserted: false, state: '', operator: '' }
  },
  wifi: {
    enabled: true,
    connected: false,
    ssid: '',
    bssid: 'unknown',
    ip: '',
    rssi: 0,
    link_speed_mbps: 0,
    frequency_mhz: 0,
    band: 'unknown',
    standard: 'unknown'
  },
  cellular: {
    operator: '',
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
    active_iface: '',
    local_ip: ''
  },
  health: {
    score: 80,
    grade: 'good',
    summary: 'Evaluating network link...',
    recommendation: 'Stable connection detected'
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

const selectedSimSlot = ref(0)
const isOptimizing = ref(false)
const smartOptResult = ref(null)

const isWifiActive = computed(() => {
  if (telemetry.network.active_iface && telemetry.network.active_iface.startsWith('wlan')) {
    return true
  }
  if (telemetry.wifi.connected) {
    return true
  }
  if (telemetry.wifi.ip && telemetry.wifi.ip.length > 6) {
    return true
  }
  return false
})

const activeConnectionLabel = computed(() => {
  if (!isOnline.value) return 'Offline'
  if (isWifiActive.value) {
    const ssid = telemetry.wifi.ssid
    if (ssid && ssid !== 'unknown' && ssid !== 'Scanning...') {
      return ssid
    }
    return 'Wi-Fi'
  }
  return telemetry.cellular.operator || 'Cellular'
})

const activeConnectionIcon = computed(() => {
  if (!isOnline.value) return 'wifi-off'
  if (isWifiActive.value) return 'wifi'
  return 'radio'
})

const activeRouteSummary = computed(() => {
  const iface = telemetry.network.active_iface
  const ip = telemetry.network.local_ip || telemetry.wifi.ip
  if (iface) {
    return ip ? `${iface} (${ip})` : iface
  }
  return isWifiActive.value ? 'Wi-Fi (wlan0)' : 'Cellular route'
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
      if (data.device) Object.assign(telemetry.device, data.device)
      if (data.sim) {
        Object.assign(telemetry.sim, data.sim)
        if (data.sim.active_slot !== undefined && !userTriggered) {
          selectedSimSlot.value = data.sim.active_slot
        }
      }
      if (data.wifi) Object.assign(telemetry.wifi, data.wifi)
      if (data.cellular) Object.assign(telemetry.cellular, data.cellular)
      if (data.network) Object.assign(telemetry.network, data.network)
      if (data.health) Object.assign(telemetry.health, data.health)
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
  { id: 'cf', name: 'Cloudflare Anycast', host: 'speed.cloudflare.com', url: 'https://speed.cloudflare.com' },
  { id: 'cf_stream', name: 'Cloudflare Streaming', host: 'speed.cloudflare.com', url: 'https://speed.cloudflare.com' },
  { id: 'cf_latency', name: 'Cloudflare Low-Latency', host: 'speed.cloudflare.com', url: 'https://speed.cloudflare.com' },
  { id: 'tele2', name: 'Tele2 Edge Global', host: 'speedtest.tele2.net', url: 'http://speedtest.tele2.net' }
]
const selectedServer = ref(servers[0])

function toggleServerSelect() {
  if (speedtestState.isTesting) {
    engine.abort()
    speedtestState.isTesting = false
  }
  const currentIdx = servers.findIndex(s => s.id === selectedServer.value.id)
  const nextIdx = (currentIdx + 1) % servers.length
  selectedServer.value = servers[nextIdx]
  speedtestState.instantSpeed = 0
  speedtestState.download = 0
  speedtestState.upload = 0
  speedtestState.ping = 0
  speedtestState.phase = 'idle'
  speedtestState.progressPct = 0
  speedtestState.graphPoints = []
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
    speedtestState.instantSpeed = 0
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
      serverId: selectedServer.value.id,
      durationSec: 6
    }, (progress) => {
      if (!speedtestState.isTesting) return
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

    if (res && speedtestState.isTesting) {
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
  const res = await runBridgeJson('set_mode', selectedSimSlot.value, modeId)
  if (res && res.success) {
    showToast(`Locked SIM ${selectedSimSlot.value + 1} to ${modeId}`)
    refreshTelemetry()
  } else {
    showToast('Failed to apply network mode')
  }
}

/* Intelligent Auto-Tuner */
async function runSmartOptimize() {
  isOptimizing.value = true
  try {
    const res = await runBridgeJson('smart_optimize')
    if (res && res.success) {
      smartOptResult.value = res
      await refreshTelemetry()
      showToast(`Optimized: ${res.tcp_cc} CC & ${res.best_dns} DNS`)
    } else {
      showToast('Optimization failed')
    }
  } catch (e) {
    showToast('Optimization error')
  } finally {
    isOptimizing.value = false
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

/* DNS Benchmark */
const dnsBenchmarking = ref(false)
const dnsResults = ref([])

const fastestDns = computed(() => {
  if (!dnsResults.value || dnsResults.value.length === 0) return null
  const valid = dnsResults.value.filter(d => d.latency_ms > 0)
  return valid.length > 0 ? valid[0] : null
})

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

async function applyFastestDns() {
  if (!fastestDns.value) return
  const matched = dnsOptions.find(o => o.name.toLowerCase() === fastestDns.value.name.toLowerCase())
  if (matched) {
    await applyPrivateDns(matched)
  }
}

/* System Tweaks */
async function toggleTweak(name, val) {
  await runBridgeJson('set_tweak', name, val ? 1 : 0)
  showToast('Setting updated')
}

/* Restore Kernel Defaults */
async function restoreDefaults() {
  await runBridgeJson('set_tcp_profile', 'stock')
  await runBridgeJson('set_tcp_cc', 'cubic')
  await runBridgeJson('set_dns', 'off', '')
  await runBridgeJson('set_tweak', 'fast_open', 1)
  await runBridgeJson('set_tweak', 'wifi_throttle', 1)
  await runBridgeJson('set_tweak', 'mobile_data_always', 0)
  activeBufferProfile.value = 'stock'
  smartOptResult.value = null
  await refreshTelemetry()
  showToast('Restored default network configuration')
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
  } else if (cmdType === 'auto_tune') {
    out = await runBridge('smart_optimize')
  }
  consoleOutput.value += (out || 'Command finished with empty output.') + '\n'
}

onMounted(() => {
  loadHistory()
  refreshTelemetry()
  setTimeout(refreshTelemetry, 500)
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
