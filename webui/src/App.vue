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
      <div v-show="activeTab === 'speed'" class="tab-pane">
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
          <div class="hero-gauge-area" :class="{ 'speed-testing-active': speedtestState.isTesting }">
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

          <!-- Duration Selector -->
          <div style="display: flex; flex-direction: column; gap: 6px;">
            <div style="display: flex; justify-content: space-between; align-items: center;">
              <span style="font-size: 11px; color: var(--on-surface-variant); font-weight: 500;">Test duration</span>
              <span style="font-size: 10.5px; color: var(--primary); font-weight: 500;">{{ selectedDuration }}s {{ durationLabel(selectedDuration) }}</span>
            </div>
            <div class="duration-chips">
              <div
                v-for="d in durationPresets"
                :key="d.sec"
                class="duration-chip"
                :class="{ active: selectedDuration === d.sec }"
                @click="setDuration(d.sec)"
              >
                {{ d.label }}
              </div>
            </div>
          </div>

          <!-- Primary Action Button -->
          <button
            class="btn btn-block"
            :class="speedtestState.isTesting ? 'btn-danger' : 'btn-primary'"
            @click="toggleSpeedtest"
          >
            <Icons :name="speedtestState.isTesting ? 'stop' : 'play'" :size="14" />
            <span>{{ speedtestState.isTesting ? 'Stop test' : `Start ${selectedDuration}s speedtest` }}</span>
          </button>
        </section>

        <!-- Link Performance Analysis Card -->
        <section class="md3-card" v-if="speedtestState.lastTestedAt || speedtestState.download > 0">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="zap" :size="13" />
              <span>Link performance analysis</span>
            </span>
            <span class="badge-pill active">{{ speedtestState.bufferbloatGrade || 'Optimal' }}</span>
          </div>
          <div class="speed-details-grid">
            <div class="speed-detail-cell">
              <span class="speed-detail-label">Latency range</span>
              <span class="speed-detail-val">{{ speedtestState.minPing || speedtestState.ping }} - {{ speedtestState.maxPing || speedtestState.ping }} ms</span>
            </div>
            <div class="speed-detail-cell">
              <span class="speed-detail-label">Bufferbloat delta</span>
              <span class="speed-detail-val">+{{ speedtestState.bufferbloatDelta || 0 }} ms</span>
            </div>
            <div class="speed-detail-cell">
              <span class="speed-detail-label">Payload transferred</span>
              <span class="speed-detail-val">{{ speedtestState.bytesUsedMb ? speedtestState.bytesUsedMb.toFixed(1) : '0.0' }} MB</span>
            </div>
            <div class="speed-detail-cell">
              <span class="speed-detail-label">Benchmark duration</span>
              <span class="speed-detail-val">{{ speedtestState.durationSec || selectedDuration }}s</span>
            </div>
          </div>
        </section>

        <!-- Recent Results Card -->
        <section class="md3-card" v-if="speedHistory.length > 0">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="clock" :size="13" />
              <span>Recent results ({{ speedHistory.length }})</span>
            </span>
            <button class="btn btn-sm btn-secondary" @click="confirmClearHistory">Clear</button>
          </div>
          <div class="history-list">
            <div v-for="(h, idx) in speedHistory.slice(0, 5)" :key="idx" class="history-card" @click="showHistoryDetail(h)" style="cursor: pointer;">
              <div class="history-top">
                <div class="history-speeds">
                  <span class="speed-down">↓ {{ h.download }}</span>
                  <span class="speed-slash">/</span>
                  <span class="speed-up">↑ {{ h.upload }}</span>
                  <span class="speed-unit">Mbps</span>
                </div>
                <div style="display: flex; align-items: center; gap: 6px;">
                  <span v-if="h.bufferbloatGrade" class="badge-pill" style="padding: 1px 6px; font-size: 9.5px;">{{ h.bufferbloatGrade }}</span>
                  <span class="history-ping">{{ h.ping }} ms</span>
                </div>
              </div>
              <div class="history-bottom">
                <span>{{ h.date }}</span>
                <span>•</span>
                <span>{{ h.durationSec || 10 }}s</span>
                <span>•</span>
                <span>{{ h.bytesUsedMb ? h.bytesUsedMb.toFixed(1) + ' MB' : '' }}</span>
                <span>•</span>
                <span class="truncate-text">{{ h.server }}</span>
              </div>
            </div>
          </div>
        </section>
      </div>

      <!-- 2. DIAGNOSTICS TAB -->
      <div v-show="activeTab === 'diagnostics'" class="tab-pane">
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

        <!-- Network Throughput Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="zap" :size="14" />
              <span>Network throughput</span>
            </span>
            <span class="badge-pill online">{{ telemetry.network.active_iface || 'Active' }}</span>
          </div>
          <div class="stat-grid-2x2">
            <div class="stat-cell">
              <span class="stat-cell-label">Download rate</span>
              <span class="stat-cell-val">{{ liveRate.rxRateStr }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">Upload rate</span>
              <span class="stat-cell-val">{{ liveRate.txRateStr }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">SoC platform</span>
              <span class="stat-cell-val">{{ telemetry.device.platform || '--' }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">RAM capacity</span>
              <span class="stat-cell-val">{{ formatRamInstalled(telemetry.device.ram_total_mb, telemetry.device.ram_installed_gb) }} GB</span>
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
                  <span v-if="telemetry.cellular.band > 0"> (B{{ telemetry.cellular.band }})</span>
                </div>
              </div>
            </div>
            <div style="display: flex; align-items: center; gap: 6px; flex-shrink: 0;">
              <span class="badge-pill" :class="{ active: telemetry.cellular.data_enabled }">
                {{ telemetry.cellular.data_enabled ? 'Data active' : 'Data off' }}
              </span>
              <span class="badge-pill">SIM {{ telemetry.sim.active_slot + 1 }}</span>
            </div>
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
      <div v-show="activeTab === 'cellular'" class="tab-pane">
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

        <!-- Radio State Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <div style="display: flex; align-items: center; gap: 8px; min-width: 0; flex: 1;">
              <Icons name="radio" :size="14" />
              <span class="card-title truncate-text">{{ activeSimInfo.operator || 'Cellular radio' }}</span>
            </div>
            <div style="display: flex; align-items: center; gap: 6px;">
              <span class="badge-pill" :class="{ active: activeSimInfo.data_enabled }">
                {{ activeSimInfo.data_enabled ? 'Data active' : 'Data off' }}
              </span>
              <span class="badge-pill active">{{ activeSimInfo.network_type || 'Active' }}</span>
            </div>
          </div>

          <div class="metric-strip" style="margin-top: 2px;">
            <div class="metric-strip-col">
              <span class="metric-strip-label">RSRP</span>
              <span class="metric-strip-val">{{ activeSimInfo.rsrp ? activeSimInfo.rsrp : '--' }}<span class="metric-strip-unit">dBm</span></span>
              <span class="metric-strip-sub">Signal</span>
            </div>
            <div class="metric-strip-col">
              <span class="metric-strip-label">SINR</span>
              <span class="metric-strip-val">{{ activeSimInfo.sinr ? activeSimInfo.sinr : '--' }}<span class="metric-strip-unit">dB</span></span>
              <span class="metric-strip-sub">Quality</span>
            </div>
            <div class="metric-strip-col">
              <span class="metric-strip-label">Tower</span>
              <span class="metric-strip-val">{{ activeSimInfo.cell_id > 0 ? activeSimInfo.cell_id : '--' }}</span>
              <span class="metric-strip-sub">{{ activeSimInfo.band > 0 ? 'Band ' + activeSimInfo.band : 'Cell ID' }}</span>
            </div>
          </div>

          <div class="stat-grid-2x2" style="margin-top: 4px;">
            <div class="stat-cell">
              <span class="stat-cell-label">Signal quality (RSRQ)</span>
              <span class="stat-cell-val">{{ activeSimInfo.rsrq ? activeSimInfo.rsrq + ' dB' : '--' }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">Signal level</span>
              <span class="stat-cell-val">{{ activeSimInfo.level ? activeSimInfo.level + ' / 4 bars' : '--' }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">Physical Cell ID (PCI)</span>
              <span class="stat-cell-val">{{ activeSimInfo.pci > 0 ? activeSimInfo.pci : (activeSimInfo.cell_id > 0 ? activeSimInfo.cell_id : '--') }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">Link status</span>
              <span class="stat-cell-val" style="text-transform: capitalize;">
                {{ activeSimInfo.data_state === 'connected' ? 'Data connected' : (activeSimInfo.inserted ? 'Voice/SMS ready' : 'Absent') }}
              </span>
            </div>
          </div>

          <button class="btn btn-secondary btn-block" @click="confirmRadioRefresh" style="margin-top: 4px;">
            <Icons name="refresh" :size="13" />
            <span>Refresh tower attachment</span>
          </button>
        </section>

        <!-- Band & Mode Locker Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="sliders" :size="14" />
              <span>Preferred radio access mode</span>
            </span>
            <span class="badge-pill">SIM {{ selectedSimSlot + 1 }}</span>
          </div>
          <p style="font-size: 11px; color: var(--on-surface-variant); line-height: 1.4;">
            Lock the cellular radio to preferred network technologies to maintain consistent reception.
          </p>

          <div class="mode-grid-2col" style="margin-top: 2px;">
            <div
              v-for="mode in cellularModes"
              :key="mode.id"
              class="mode-card-compact"
              :class="{ selected: selectedCellularMode === mode.id }"
              @click="confirmCellularMode(mode)"
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
      <div v-show="activeTab === 'optimizer'" class="tab-pane">
        <!-- Intelligent Hardware Auto-Tuner Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="zap" :size="14" />
              <span>Hardware stack auto-tuning</span>
            </span>
            <span class="badge-pill active">
              {{ formatRamInstalled(telemetry.device.ram_total_mb, telemetry.device.ram_installed_gb) }} GB RAM
            </span>
          </div>

          <div style="font-size: 11px; color: var(--on-surface-variant); margin-top: -6px;">
            Optimized for {{ telemetry.device.brand || 'Device' }} {{ telemetry.device.model }} • {{ (telemetry.device.platform || 'universal').toUpperCase() }}
          </div>

          <div class="stat-grid-2x2" style="margin-top: 4px;">
            <div class="stat-cell">
              <span class="stat-cell-label">Hardware model</span>
              <span class="stat-cell-val truncate-text">{{ telemetry.device.brand }} {{ telemetry.device.model }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">SoC platform</span>
              <span class="stat-cell-val truncate-text">{{ (telemetry.device.platform || 'Universal').toUpperCase() }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">RAM profile</span>
              <span class="stat-cell-val truncate-text">{{ formatRamTier(telemetry.device.ram_tier) }}</span>
            </div>
            <div class="stat-cell">
              <span class="stat-cell-label">Installed memory</span>
              <span class="stat-cell-val truncate-text">{{ formatRamInstalled(telemetry.device.ram_total_mb, telemetry.device.ram_installed_gb) }} GB</span>
            </div>
          </div>

          <button class="btn btn-primary btn-block" :disabled="isOptimizing" @click="confirmSmartOptimize">
            <Icons name="sliders" :size="14" :class="{ 'spin-anim': isOptimizing }" />
            <span>{{ isOptimizing ? 'Calibrating network stack...' : 'Optimize network configuration' }}</span>
          </button>

          <div v-if="smartOptResult" class="tune-result-box">
            <Icons name="check" :size="13" style="color: var(--primary); flex-shrink: 0;" />
            <span>
              Configured with {{ smartOptResult.tcp_cc }} CC, {{ smartOptResult.buffer_profile }} socket buffers, and {{ smartOptResult.best_dns }} resolver ({{ smartOptResult.best_dns_latency_ms }} ms latency).
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

        <!-- Anti-Censorship & DPI Bypass Card -->
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="shield" :size="14" />
              <span>Anti-censorship &amp; DPI bypass</span>
            </span>
            <span class="badge-pill" :class="telemetry.settings.dpi_bypass ? 'active' : ''">
              {{ telemetry.settings.dpi_bypass ? 'Active' : 'Disabled' }}
            </span>
          </div>

          <div class="switch-row" style="margin-top: 2px;">
            <div class="switch-label-col">
              <span class="switch-title">Deep packet inspection (DPI) evasion</span>
              <span class="switch-desc">Splits TLS ClientHello across TCP segments (MSS 536) to bypass ISP SNI filtering (Reddit, Vimeo) without a VPN</span>
            </div>
            <label class="md3-switch">
              <input
                type="checkbox"
                :checked="telemetry.settings.dpi_bypass"
                :disabled="isTogglingDpi"
                @change="toggleDpiBypass"
              />
              <span class="md3-switch-track">
                <span class="md3-switch-thumb"></span>
              </span>
            </label>
          </div>

          <div style="display: flex; align-items: center; justify-content: space-between; gap: 8px; margin-top: 8px; padding-top: 8px; border-top: 1px solid var(--outline-variant);">
            <div style="font-size: 11px; color: var(--on-surface-variant); min-width: 0; flex: 1;" class="truncate-text">
              <span v-if="siteCheckStatus">{{ siteCheckStatus }}</span>
              <span v-else>Verify access to restricted services</span>
            </div>
            <button class="btn btn-secondary" style="padding: 4px 10px; font-size: 11px; flex-shrink: 0;" :disabled="isCheckingSite" @click="checkBlockedSite">
              <Icons name="refresh" :size="11" :class="{ 'spin-anim': isCheckingSite }" />
              <span>{{ isCheckingSite ? 'Probing...' : 'Verify Reddit' }}</span>
            </button>
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
              <span class="switch-desc">Reduce background scan frequency to optimize power consumption</span>
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
              <span class="switch-title">Mobile data standby</span>
              <span class="switch-desc">Maintain cellular connectivity during Wi-Fi sessions for seamless handover</span>
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
          <button class="btn btn-secondary btn-block" @click="confirmRestoreDefaults" style="margin-top: 4px;">
            <Icons name="refresh" :size="13" />
            <span>Restore kernel defaults</span>
          </button>
        </section>
      </div>

      <!-- 5. CONSOLE TAB -->
      <div v-show="activeTab === 'console'" class="tab-pane">
        <section class="md3-card">
          <div class="card-title-row">
            <span class="card-title">
              <Icons name="terminal" :size="14" />
              <span>Network diagnostic console</span>
            </span>
            <div style="display: flex; gap: 6px;">
              <button class="btn btn-sm btn-secondary" @click="copyConsoleOutput" :disabled="!consoleOutput">
                Copy
              </button>
              <button class="btn btn-sm btn-secondary" @click="consoleOutput = ''">
                Clear
              </button>
            </div>
          </div>

          <!-- Diagnostic Action Cards Grid -->
          <div class="console-cmd-grid" style="margin-top: 2px;">
            <div
              v-for="cmd in consoleCmds"
              :key="cmd.id"
              class="console-cmd-card"
              :class="{
                running: runningCmd === cmd.id,
                disabled: runningCmd && runningCmd !== cmd.id
              }"
              @click="runConsoleCmd(cmd.id)"
            >
              <div class="console-cmd-icon">
                <Icons
                  :name="cmd.icon"
                  :size="15"
                  :class="{ 'spin-anim': runningCmd === cmd.id }"
                />
              </div>
              <div class="console-cmd-info">
                <span class="console-cmd-name">{{ cmd.name }}</span>
                <span class="console-cmd-desc">
                  {{ runningCmd === cmd.id ? 'Running probe...' : cmd.desc }}
                </span>
              </div>
            </div>
          </div>

          <!-- Console Terminal Output -->
          <div ref="consoleBoxRef" class="console-box" style="margin-top: 10px;">
            {{ consoleOutput || 'Console ready. Select a diagnostic action above to run.' }}
          </div>
        </section>
      </div>
    </main>

    <!-- In-App Modal Dialog -->
    <div v-if="modalState.visible" class="modal-overlay" @click.self="modalState.visible = false">
      <div class="modal-dialog">
        <div style="display: flex; align-items: center; gap: 8px;">
          <Icons v-if="modalState.icon" :name="modalState.icon" :size="18" :style="{ color: modalState.isDanger ? 'var(--error, #cf6679)' : 'var(--primary)' }" />
          <div class="modal-title">{{ modalState.title }}</div>
        </div>
        <div class="modal-desc" style="white-space: pre-line;">{{ modalState.desc }}</div>
        <div class="modal-actions">
          <button
            v-if="modalState.cancelText"
            class="btn btn-secondary"
            style="flex: 1;"
            @click="modalState.visible = false"
          >
            {{ modalState.cancelText }}
          </button>
          <button
            class="btn"
            :class="modalState.isDanger ? 'btn-danger' : 'btn-primary'"
            style="flex: 1;"
            @click="onModalConfirm"
          >
            {{ modalState.confirmText || 'Confirm' }}
          </button>
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
import { ref, reactive, computed, nextTick, onMounted, onUnmounted } from 'vue'
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
    ram_installed_gb: 0,
    ram_tier: 'standard'
  },
  sim: {
    active_slot: 0,
    active_subid: 1,
    slot0: {
      inserted: false,
      state: '',
      operator: '',
      network_type: 'unknown',
      rsrp: 0,
      rsrq: 0,
      sinr: 0,
      level: 0,
      cell_id: -1,
      pci: 0,
      band: 0,
      data_enabled: false,
      data_state: 'disconnected'
    },
    slot1: {
      inserted: false,
      state: '',
      operator: '',
      network_type: 'unknown',
      rsrp: 0,
      rsrq: 0,
      sinr: 0,
      level: 0,
      cell_id: -1,
      pci: 0,
      band: 0,
      data_enabled: false,
      data_state: 'disconnected'
    }
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
    pci: 0,
    band: 0,
    data_enabled: false,
    data_state: 'disconnected',
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
    mobile_data_always_on: false,
    dpi_bypass: false
  }
})

const selectedSimSlot = ref(0)
const activeSimInfo = computed(() => {
  if (selectedSimSlot.value === 1 && telemetry.sim.slot1 && telemetry.sim.slot1.inserted) {
    return telemetry.sim.slot1
  }
  if (selectedSimSlot.value === 0 && telemetry.sim.slot0 && telemetry.sim.slot0.inserted) {
    return telemetry.sim.slot0
  }
  if (selectedSimSlot.value === 1 && telemetry.sim.slot1) {
    return telemetry.sim.slot1
  }
  if (telemetry.sim.slot0) {
    return telemetry.sim.slot0
  }
  return telemetry.cellular
})
const isOptimizing = ref(false)
const smartOptResult = ref(null)

function formatRamInstalled(mb, gb) {
  if (gb && gb > 0) return gb
  if (!mb || mb <= 0) return 0
  const raw = Math.round((mb + 650) / 1024)
  const tiers = [1, 2, 3, 4, 6, 8, 12, 16, 18, 24, 32]
  for (const t of tiers) {
    if (Math.abs(raw - t) <= 1 && raw <= t) return t
  }
  return raw
}

function formatRamTier(tier) {
  if (!tier) return 'Standard tier'
  const t = tier.toLowerCase()
  if (t === 'high') return 'High tier'
  if (t === 'low') return 'Entry tier'
  return 'Standard tier'
}

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

/* Traffic Rate State */
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
        telemetry.sim.active_slot = data.sim.active_slot
        telemetry.sim.active_subid = data.sim.active_subid
        if (data.sim.slot0) Object.assign(telemetry.sim.slot0, data.sim.slot0)
        if (data.sim.slot1) Object.assign(telemetry.sim.slot1, data.sim.slot1)
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
  { id: 'cf_anycast', name: 'Cloudflare Anycast', host: 'speed.cloudflare.com', url: 'https://speed.cloudflare.com' },
  { id: 'cf_stream', name: 'Cloudflare Streaming Edge', host: 'speed.cloudflare.com', url: 'https://speed.cloudflare.com' },
  { id: 'cf_latency', name: 'Cloudflare Low-Latency', host: 'speed.cloudflare.com', url: 'https://speed.cloudflare.com' },
  { id: 'cf_enterprise', name: 'Cloudflare Enterprise Edge', host: 'speed.cloudflare.com', url: 'https://speed.cloudflare.com' }
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

const durationPresets = [
  { sec: 5, label: '5s' },
  { sec: 10, label: '10s' },
  { sec: 15, label: '15s' },
  { sec: 30, label: '30s' }
]
const selectedDuration = ref(parseInt(localStorage.getItem('hypernet_speedtest_duration') || '10', 10))

function setDuration(sec) {
  selectedDuration.value = sec
  localStorage.setItem('hypernet_speedtest_duration', sec.toString())
}

function durationLabel(sec) {
  if (sec <= 5) return 'Quick'
  if (sec <= 10) return 'Standard'
  if (sec <= 15) return 'Thorough'
  return 'Sustained'
}

function showHistoryDetail(h) {
  modalState.title = `Benchmark Results (${h.date})`
  modalState.desc = `Server: ${h.server || 'Anycast'}\nDownload: ${h.download} Mbps\nUpload: ${h.upload} Mbps\nPing: ${h.ping} ms (min ${h.minPing || h.ping}, max ${h.maxPing || h.ping} ms, jitter ±${h.jitter || 0} ms)\nBufferbloat: ${h.bufferbloatGrade ? h.bufferbloatGrade + ' (+' + (h.bufferbloatDelta || 0) + ' ms)' : 'Optimal'}\nPayload: ${h.bytesUsedMb ? h.bytesUsedMb.toFixed(1) + ' MB' : '--'}\nDuration: ${h.durationSec || 10}s`
  modalState.icon = 'gauge'
  modalState.isDanger = false
  modalState.confirmText = 'Close'
  modalState.cancelText = ''
  modalState.action = () => { modalState.visible = false }
  modalState.visible = true
}

function confirmClearHistory() {
  modalState.title = 'Clear speedtest history?'
  modalState.desc = 'All recorded benchmark results and throughput logs will be permanently deleted from local storage.'
  modalState.icon = 'trash'
  modalState.isDanger = true
  modalState.confirmText = 'Clear history'
  modalState.cancelText = 'Cancel'
  modalState.action = () => {
    modalState.visible = false
    clearHistory()
  }
  modalState.visible = true
}

const speedtestState = reactive({
  isTesting: false,
  phase: 'idle',
  instantSpeed: 0,
  progressPct: 0,
  ping: 0,
  minPing: 0,
  maxPing: 0,
  jitter: 0,
  loadedPing: 0,
  bufferbloatDelta: 0,
  bufferbloatGrade: '',
  download: 0,
  upload: 0,
  bytesUsedMb: 0,
  downloadMb: 0,
  uploadMb: 0,
  durationSec: 10,
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
  if (speedHistory.value.length > 25) speedHistory.value.pop()
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
  speedtestState.minPing = 0
  speedtestState.maxPing = 0
  speedtestState.jitter = 0
  speedtestState.loadedPing = 0
  speedtestState.bufferbloatDelta = 0
  speedtestState.bufferbloatGrade = ''
  speedtestState.bytesUsedMb = 0
  speedtestState.graphPoints = []

  try {
    const res = await engine.runTest({
      serverUrl: selectedServer.value.url,
      serverName: selectedServer.value.name,
      serverId: selectedServer.value.id,
      durationSec: selectedDuration.value
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
      speedtestState.minPing = res.minPing
      speedtestState.maxPing = res.maxPing
      speedtestState.loadedPing = res.loadedPing
      speedtestState.bufferbloatDelta = res.bufferbloatDelta
      speedtestState.bufferbloatGrade = res.bufferbloatGrade
      speedtestState.downloadMb = res.downloadBytes ? Math.round((res.downloadBytes / 1048576) * 10) / 10 : 0
      speedtestState.uploadMb = res.uploadBytes ? Math.round((res.uploadBytes / 1048576) * 10) / 10 : 0
      speedtestState.durationSec = res.durationSec
      saveHistory({
        date: new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }),
        download: res.download,
        upload: res.upload,
        ping: res.ping,
        minPing: res.minPing,
        maxPing: res.maxPing,
        jitter: res.jitter,
        loadedPing: res.loadedPing,
        bufferbloatDelta: res.bufferbloatDelta,
        bufferbloatGrade: res.bufferbloatGrade,
        bytesUsedMb: res.bytesUsedMb,
        durationSec: res.durationSec,
        server: res.server
      })
      showToast(`Speedtest finished (${res.download} ↓ / ${res.upload} ↑ Mbps)`)
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
  { id: 'auto', title: 'Global multi-mode', desc: '5G, 4G LTE, 3G, and 2G auto' },
  { id: '5g_only', title: '5G New Radio (NR)', desc: 'Exclusive 5G standalone & NSA' },
  { id: '5g_lte', title: '5G & 4G preferred', desc: 'Prioritize 5G with LTE fallback' },
  { id: 'lte_only', title: '4G LTE only', desc: 'Restrict connection to LTE bands' },
  { id: '3g_only', title: '3G UMTS/HSPA', desc: 'Legacy 3G network coverage' },
  { id: '2g_only', title: '2G GSM only', desc: 'Power-efficient voice and SMS' }
]

const selectedCellularMode = ref('auto')

async function applyCellularMode(modeId) {
  selectedCellularMode.value = modeId
  const res = await runBridgeJson('set_mode', selectedSimSlot.value, modeId)
  if (res && res.success) {
    showToast(`SIM ${selectedSimSlot.value + 1} radio mode applied`)
    refreshTelemetry()
  } else {
    showToast('Failed to apply radio mode')
  }
}

function confirmCellularMode(mode) {
  if (mode.id === selectedCellularMode.value) return
  if (mode.id === 'auto') {
    applyCellularMode(mode.id)
    return
  }
  modalState.title = `Switch to ${mode.title}?`
  modalState.desc = `Locking cellular radio to ${mode.desc} on SIM ${selectedSimSlot.value + 1}.\n\nIf signal is weak or unavailable in your area, voice calls and mobile data may disconnect until unlocked.`
  modalState.icon = 'radio'
  modalState.isDanger = false
  modalState.confirmText = 'Apply lock'
  modalState.cancelText = 'Cancel'
  modalState.action = () => {
    modalState.visible = false
    applyCellularMode(mode.id)
  }
  modalState.visible = true
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

function confirmSmartOptimize() {
  modalState.title = 'Optimize network stack?'
  modalState.desc = `Run auto-calibration for ${telemetry.device.brand || 'Device'} ${telemetry.device.model}?\n\nThis will probe DNS latency, calculate optimal TCP socket buffers for your ${telemetry.device.ram_tier || 'standard'} RAM tier, and activate low-latency kernel queues.`
  modalState.icon = 'sliders'
  modalState.isDanger = false
  modalState.confirmText = 'Optimize now'
  modalState.cancelText = 'Cancel'
  modalState.action = async () => {
    modalState.visible = false
    await runSmartOptimize()
  }
  modalState.visible = true
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

/* Anti-Censorship & DPI Bypass */
const isTogglingDpi = ref(false)
const isCheckingSite = ref(false)
const siteCheckStatus = ref('')

async function toggleDpiBypass(e) {
  const enable = e.target.checked
  isTogglingDpi.value = true
  try {
    await runBridge('set_dpi_bypass', enable ? '1' : '0')
    telemetry.settings.dpi_bypass = enable
    showToast(enable ? 'DPI evasion active' : 'DPI evasion disabled')
    await refreshTelemetry()
  } catch (err) {
    showToast('Failed to toggle DPI evasion')
  } finally {
    isTogglingDpi.value = false
  }
}

async function checkBlockedSite() {
  isCheckingSite.value = true
  siteCheckStatus.value = 'Probing www.reddit.com...'
  try {
    const res = await runBridgeJson('check_site', 'www.reddit.com')
    if (res && res.reachable) {
      siteCheckStatus.value = `Accessible (${res.ip || 'OK'})`
      showToast('Website is reachable')
    } else if (res && res.reason === 'dns_poisoned') {
      siteCheckStatus.value = `DNS intercepted (${res.ip})`
      showToast('DNS was intercepted by ISP')
    } else {
      siteCheckStatus.value = res?.reason || 'Connection refused'
      showToast('Site unreachable, enable DPI evasion')
    }
  } catch (e) {
    siteCheckStatus.value = 'Probe failed'
    showToast('Verification failed')
  } finally {
    isCheckingSite.value = false
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

function confirmRestoreDefaults() {
  modalState.title = 'Restore kernel defaults?'
  modalState.desc = 'This will reset all TCP buffer allocations to kernel stock, revert congestion control to Cubic, disable DNS overrides, and restore system power-saving toggles.'
  modalState.icon = 'refresh'
  modalState.isDanger = true
  modalState.confirmText = 'Restore defaults'
  modalState.cancelText = 'Keep settings'
  modalState.action = async () => {
    modalState.visible = false
    await restoreDefaults()
  }
  modalState.visible = true
}

/* Modal Dialog State */
const modalState = reactive({
  visible: false,
  title: '',
  desc: '',
  icon: 'alert',
  isDanger: false,
  confirmText: 'Confirm',
  cancelText: 'Cancel',
  action: null
})

function confirmRadioRefresh() {
  modalState.title = 'Refresh cellular connection?'
  modalState.desc = 'This temporarily cycles the airplane mode interface to drop stale data contexts and renegotiate attachment with the optimal base station. Connectivity will briefly pause for two seconds.'
  modalState.icon = 'refresh'
  modalState.isDanger = false
  modalState.confirmText = 'Refresh radio'
  modalState.cancelText = 'Cancel'
  modalState.action = async () => {
    modalState.visible = false
    showToast('Refreshing radio connection...')
    await runBridge('radio_refresh')
    setTimeout(refreshTelemetry, 2500)
  }
  modalState.visible = true
}

function onModalConfirm() {
  if (modalState.action) modalState.action()
  else modalState.visible = false
}

/* Console Tab Commands */
const consoleOutput = ref('')
const runningCmd = ref('')
const consoleBoxRef = ref(null)

const consoleCmds = [
  { id: 'info', name: 'Module Status', desc: 'Hardware, SIM & Wi-Fi', icon: 'radio' },
  { id: 'ping', name: 'Ping 1.1.1.1', desc: 'Direct latency probe', icon: 'clock' },
  { id: 'speedtest', name: 'Socket Speed', desc: '3s throughput probe', icon: 'speed' },
  { id: 'dns_bench', name: 'DNS Benchmark', desc: 'Query 4 resolvers', icon: 'globe' },
  { id: 'routes', name: 'IP Routing', desc: 'Kernel routing table', icon: 'terminal' },
  { id: 'auto_tune', name: 'Stack Tune', desc: 'TCP & socket buffer tune', icon: 'sliders' }
]

function scrollConsoleBottom() {
  if (consoleBoxRef.value) {
    consoleBoxRef.value.scrollTop = consoleBoxRef.value.scrollHeight
  }
}

function formatJsonOutput(raw) {
  if (!raw) return ''
  try {
    const obj = JSON.parse(raw)
    return JSON.stringify(obj, null, 2)
  } catch {
    return raw
  }
}

function formatPingOutput(raw) {
  if (!raw) return ''
  try {
    const p = JSON.parse(raw)
    if (p.host) {
      return `Ping ${p.host} (${p.transmitted} probes):\n  Latency: avg ${p.avg_ms} ms (min ${p.min_ms}, max ${p.max_ms})\n  Jitter:  ±${p.jitter_ms} ms\n  Loss:    ${p.loss_pct}% (${p.received}/${p.transmitted} received)`
    }
  } catch {}
  return raw
}

function formatDnsOutput(raw) {
  if (!raw) return ''
  try {
    const arr = JSON.parse(raw)
    if (Array.isArray(arr)) {
      const lines = ['DNS Resolver Benchmark:']
      arr.forEach((d, i) => {
        lines.push(`  ${i + 1}. ${d.name.padEnd(12)} (${d.ip}) : ${d.latency_ms > 0 ? d.latency_ms + ' ms' : 'timeout'}`)
      })
      return lines.join('\n')
    }
  } catch {}
  return raw
}

async function copyConsoleOutput() {
  if (!consoleOutput.value) return
  try {
    if (navigator.clipboard && navigator.clipboard.writeText) {
      await navigator.clipboard.writeText(consoleOutput.value)
    } else {
      const ta = document.createElement('textarea')
      ta.value = consoleOutput.value
      document.body.appendChild(ta)
      ta.select()
      document.execCommand('copy')
      document.body.removeChild(ta)
    }
    showToast('Console output copied')
  } catch {
    showToast('Failed to copy')
  }
}

async function runConsoleCmd(cmdType) {
  if (runningCmd.value) return
  runningCmd.value = cmdType

  const cmdObj = consoleCmds.find(c => c.id === cmdType)
  const cmdTitle = cmdObj ? cmdObj.name : cmdType

  const timeStr = new Date().toLocaleTimeString()
  consoleOutput.value += `${consoleOutput.value ? '\n' : ''}[${timeStr}] ⚡ Running ${cmdTitle}...\n`
  await nextTick()
  scrollConsoleBottom()

  try {
    let out = ''
    if (cmdType === 'info') {
      const raw = await runBridge('info')
      out = formatJsonOutput(raw)
    } else if (cmdType === 'routes') {
      out = await execCommand('ip route show table 0 2>/dev/null || ip route')
    } else if (cmdType === 'speedtest') {
      out = await runBridge('speedtest', 'cf_latency', '3')
    } else if (cmdType === 'ping') {
      const raw = await runBridge('ping', '1.1.1.1', '2')
      out = formatPingOutput(raw)
    } else if (cmdType === 'dns_bench') {
      const raw = await runBridge('dns_bench')
      out = formatDnsOutput(raw)
    } else if (cmdType === 'auto_tune') {
      const raw = await runBridge('smart_optimize')
      out = formatJsonOutput(raw)
    }
    consoleOutput.value += (out ? out.trim() : 'Command finished with empty output.') + '\n'
  } catch (e) {
    consoleOutput.value += `Execution error: ${e.message || e}\n`
  } finally {
    runningCmd.value = ''
    await nextTick()
    scrollConsoleBottom()
  }
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
