/**
 * HyperNet Standalone Speedtest Engine
 * High-precision multi-stream network performance benchmarking.
 * 
 * Copyright (C) 2026 @itswill00
 * Licensed under the GNU General Public License v3.0
 */

export class SpeedTestEngine {
  constructor() {
    this.abortController = null
    this.isRunning = false
    this.currentRunId = 0
    this.activeIntervals = []
  }

  abort() {
    this.currentRunId++
    if (this.abortController) {
      this.abortController.abort()
      this.abortController = null
    }
    for (const timer of this.activeIntervals) {
      clearInterval(timer)
    }
    this.activeIntervals = []
    this.isRunning = false
  }

  async runTest(options = {}, onProgress = () => {}) {
    this.abort()
    const runId = ++this.currentRunId
    this.isRunning = true
    this.abortController = new AbortController()
    const signal = this.abortController.signal

    const serverName = options.serverName || 'Cloudflare Anycast'
    const serverUrl = options.serverUrl || 'https://speed.cloudflare.com'
    const serverHost = options.serverHost || (new URL(serverUrl)).hostname
    const totalDuration = Math.max(4, Math.min(60, options.durationSec || 10))

    // Allocate time proportionally:
    // Ping: ~1.2s
    // Download: ~58% of remaining
    // Upload: ~42% of remaining
    const remainingTime = Math.max(2.5, totalDuration - 1.2)
    const downloadDurationSec = Math.max(2.0, Math.round(remainingTime * 0.58 * 10) / 10)
    const uploadDurationSec = Math.max(1.5, Math.round(remainingTime * 0.42 * 10) / 10)

    const safeProgress = (data) => {
      if (this.currentRunId !== runId || signal.aborted) return
      onProgress(data)
    }

    const result = {
      ping: 0,
      minPing: 0,
      maxPing: 0,
      jitter: 0,
      loadedPing: 0,
      loadedPingDl: 0,
      loadedPingUl: 0,
      bufferbloatDelta: 0,
      bufferbloatGrade: 'N/A',
      download: 0,
      upload: 0,
      downloadBytes: 0,
      uploadBytes: 0,
      bytesTransferred: 0,
      bytesUsedMb: 0,
      durationSec: totalDuration,
      server: serverName,
      serverHost: serverHost,
      timestamp: Date.now()
    }

    const graphPoints = []
    const overallStartTime = performance.now()

    try {
      // -----------------------------------------------------------
      // Phase 1: Idle Latency & Jitter Measurement (~1.2s)
      // -----------------------------------------------------------
      safeProgress({
        phase: 'ping',
        speedMbps: 0,
        progressPct: 5,
        pingMs: 0,
        jitterMs: 0,
        downloadMbps: 0,
        uploadMbps: 0,
        bytesTransferred: 0,
        graphPoints: []
      })

      const pingSamples = []
      const probeCount = Math.min(8, Math.max(4, Math.round(totalDuration * 0.6)))
      
      for (let i = 0; i < probeCount; i++) {
        if (signal.aborted || this.currentRunId !== runId) return null
        const start = performance.now()
        try {
          await fetch(`${serverUrl}/__down?bytes=0&_p=${Date.now()}_${i}`, {
            cache: 'no-store',
            signal
          })
          const rtt = Math.round((performance.now() - start) * 10) / 10
          if (rtt > 0) {
            pingSamples.push(rtt)
          }
          const progressPct = 5 + Math.round(((i + 1) / probeCount) * 10)
          safeProgress({
            phase: 'ping',
            speedMbps: 0,
            progressPct,
            pingMs: rtt,
            jitterMs: 0,
            downloadMbps: 0,
            uploadMbps: 0,
            bytesTransferred: 0,
            graphPoints: []
          })
        } catch (e) {
          if (signal.aborted) return null
        }
        await new Promise(r => setTimeout(r, 60))
      }

      if (pingSamples.length > 0) {
        pingSamples.sort((a, b) => a - b)
        result.minPing = pingSamples[0]
        result.maxPing = pingSamples[pingSamples.length - 1]
        const avg = pingSamples.reduce((sum, v) => sum + v, 0) / pingSamples.length
        let jitterSum = 0
        for (let i = 1; i < pingSamples.length; i++) {
          jitterSum += Math.abs(pingSamples[i] - pingSamples[i - 1])
        }
        result.ping = Math.round(avg * 10) / 10
        result.jitter = Math.round((jitterSum / Math.max(1, pingSamples.length - 1)) * 10) / 10
      }

      safeProgress({
        phase: 'ping_done',
        speedMbps: 0,
        progressPct: 15,
        pingMs: result.ping,
        jitterMs: result.jitter,
        downloadMbps: 0,
        uploadMbps: 0,
        bytesTransferred: 0,
        graphPoints: []
      })

      // -----------------------------------------------------------
      // Phase 2: Multi-Stream Download with Loaded Latency Probe
      // -----------------------------------------------------------
      safeProgress({
        phase: 'download',
        speedMbps: 0,
        progressPct: 15,
        pingMs: result.ping,
        jitterMs: result.jitter,
        downloadMbps: 0,
        uploadMbps: 0,
        bytesTransferred: 0,
        graphPoints: []
      })

      let totalDownloadBytes = 0
      let lastBytes = 0
      let lastTime = performance.now()
      const downloadStartTime = performance.now()
      const downloadEndTime = downloadStartTime + (downloadDurationSec * 1000)

      const dlPingSamples = []
      const dlPingInterval = setInterval(async () => {
        if (signal.aborted || this.currentRunId !== runId || performance.now() >= downloadEndTime) {
          clearInterval(dlPingInterval)
          return
        }
        const t0 = performance.now()
        try {
          await fetch(`${serverUrl}/__down?bytes=0&_lp=${Date.now()}`, { cache: 'no-store', signal })
          const rtt = performance.now() - t0
          if (rtt > 0 && rtt < 4000) dlPingSamples.push(rtt)
        } catch {}
      }, 700)
      this.activeIntervals.push(dlPingInterval)

      const downloadWorker = async (chunkBytes) => {
        while (performance.now() < downloadEndTime && !signal.aborted && this.currentRunId === runId) {
          try {
            const resp = await fetch(`${serverUrl}/__down?bytes=${chunkBytes}&_t=${Date.now()}_${Math.random()}`, {
              cache: 'no-store',
              signal
            })
            if (!resp.body) break
            const reader = resp.body.getReader()
            while (!signal.aborted && this.currentRunId === runId) {
              const { done, value } = await reader.read()
              if (done) break
              if (value) {
                totalDownloadBytes += value.length
              }
              if (performance.now() >= downloadEndTime) {
                reader.cancel().catch(() => {})
                break
              }
            }
          } catch (e) {
            if (signal.aborted) break
            await new Promise(r => setTimeout(r, 80))
          }
        }
      }

      let currentInstantMbps = 0
      const downInterval = setInterval(() => {
        if (signal.aborted || this.currentRunId !== runId) {
          clearInterval(downInterval)
          return
        }
        const now = performance.now()
        const timeDelta = (now - lastTime) / 1000
        const bytesDelta = totalDownloadBytes - lastBytes

        if (timeDelta > 0.05) {
          const instantSpeed = (bytesDelta * 8) / (timeDelta * 1000000)
          currentInstantMbps = currentInstantMbps === 0 ? instantSpeed : (currentInstantMbps * 0.65 + instantSpeed * 0.35)
          lastBytes = totalDownloadBytes
          lastTime = now

          const elapsedSec = (now - downloadStartTime) / 1000
          const progressPct = Math.min(60, 15 + Math.round((elapsedSec / downloadDurationSec) * 45))
          graphPoints.push(Math.round(currentInstantMbps * 10) / 10)

          safeProgress({
            phase: 'download',
            speedMbps: Math.round(currentInstantMbps * 10) / 10,
            progressPct,
            pingMs: result.ping,
            jitterMs: result.jitter,
            downloadMbps: Math.round(currentInstantMbps * 10) / 10,
            uploadMbps: 0,
            bytesTransferred: totalDownloadBytes,
            graphPoints: [...graphPoints]
          })
        }
      }, 80)
      this.activeIntervals.push(downInterval)

      const streamCount = 4
      const downWorkers = []
      const chunkSizes = [15000000, 25000000, 25000000, 40000000]
      for (let s = 0; s < streamCount; s++) {
        downWorkers.push(downloadWorker(chunkSizes[s] || 25000000))
      }

      await Promise.all(downWorkers)
      clearInterval(downInterval)
      clearInterval(dlPingInterval)

      if (signal.aborted || this.currentRunId !== runId) return null

      const actualDownloadSec = (performance.now() - downloadStartTime) / 1000
      if (totalDownloadBytes > 0 && actualDownloadSec > 0.1) {
        result.download = Math.round(((totalDownloadBytes * 8) / (actualDownloadSec * 1000000)) * 10) / 10
      } else {
        result.download = Math.round(currentInstantMbps * 10) / 10
      }
      result.downloadBytes = totalDownloadBytes
      result.bytesTransferred += totalDownloadBytes

      if (dlPingSamples.length > 0) {
        result.loadedPingDl = Math.round((dlPingSamples.reduce((a, b) => a + b, 0) / dlPingSamples.length) * 10) / 10
      }

      // -----------------------------------------------------------
      // Phase 3: Multi-Stream Upload with Loaded Latency Probe
      // -----------------------------------------------------------
      safeProgress({
        phase: 'upload',
        speedMbps: 0,
        progressPct: 60,
        pingMs: result.ping,
        jitterMs: result.jitter,
        downloadMbps: result.download,
        uploadMbps: 0,
        bytesTransferred: result.bytesTransferred,
        graphPoints: [...graphPoints]
      })

      let totalUploadBytes = 0
      let lastUploadBytes = 0
      let lastUploadTime = performance.now()
      const uploadStartTime = performance.now()
      const uploadEndTime = uploadStartTime + (uploadDurationSec * 1000)
      const uploadChunk = new Uint8Array(1024 * 256) // 256KB chunk

      const ulPingSamples = []
      const ulPingInterval = setInterval(async () => {
        if (signal.aborted || this.currentRunId !== runId || performance.now() >= uploadEndTime) {
          clearInterval(ulPingInterval)
          return
        }
        const t0 = performance.now()
        try {
          await fetch(`${serverUrl}/__down?bytes=0&_lp=${Date.now()}`, { cache: 'no-store', signal })
          const rtt = performance.now() - t0
          if (rtt > 0 && rtt < 4000) ulPingSamples.push(rtt)
        } catch {}
      }, 700)
      this.activeIntervals.push(ulPingInterval)

      const uploadWorker = async () => {
        while (performance.now() < uploadEndTime && !signal.aborted && this.currentRunId === runId) {
          try {
            const resp = await fetch(`${serverUrl}/__up`, {
              method: 'POST',
              body: uploadChunk,
              cache: 'no-store',
              signal
            })
            if (resp.ok) {
              totalUploadBytes += uploadChunk.length
            }
          } catch (e) {
            if (signal.aborted) break
            await new Promise(r => setTimeout(r, 60))
          }
        }
      }

      let currentInstantUploadMbps = 0
      const upInterval = setInterval(() => {
        if (signal.aborted || this.currentRunId !== runId) {
          clearInterval(upInterval)
          return
        }
        const now = performance.now()
        const timeDelta = (now - lastUploadTime) / 1000
        const bytesDelta = totalUploadBytes - lastUploadBytes

        if (timeDelta > 0.05) {
          const instantSpeed = (bytesDelta * 8) / (timeDelta * 1000000)
          currentInstantUploadMbps = currentInstantUploadMbps === 0 ? instantSpeed : (currentInstantUploadMbps * 0.65 + instantSpeed * 0.35)
          lastUploadBytes = totalUploadBytes
          lastUploadTime = now

          const elapsedSec = (now - uploadStartTime) / 1000
          const progressPct = Math.min(98, 60 + Math.round((elapsedSec / uploadDurationSec) * 38))
          graphPoints.push(Math.round(currentInstantUploadMbps * 10) / 10)

          safeProgress({
            phase: 'upload',
            speedMbps: Math.round(currentInstantUploadMbps * 10) / 10,
            progressPct,
            pingMs: result.ping,
            jitterMs: result.jitter,
            downloadMbps: result.download,
            uploadMbps: Math.round(currentInstantUploadMbps * 10) / 10,
            bytesTransferred: result.bytesTransferred + totalUploadBytes,
            graphPoints: [...graphPoints]
          })
        }
      }, 80)
      this.activeIntervals.push(upInterval)

      const upWorkers = []
      for (let u = 0; u < 3; u++) {
        upWorkers.push(uploadWorker())
      }

      await Promise.all(upWorkers)
      clearInterval(upInterval)
      clearInterval(ulPingInterval)

      if (signal.aborted || this.currentRunId !== runId) return null

      const actualUploadSec = (performance.now() - uploadStartTime) / 1000
      if (totalUploadBytes > 0 && actualUploadSec > 0.1) {
        result.upload = Math.round(((totalUploadBytes * 8) / (actualUploadSec * 1000000)) * 10) / 10
      } else {
        result.upload = Math.round(currentInstantUploadMbps * 10) / 10
      }
      result.uploadBytes = totalUploadBytes
      result.bytesTransferred += totalUploadBytes
      result.bytesUsedMb = Math.round((result.bytesTransferred / 1048576) * 10) / 10

      if (ulPingSamples.length > 0) {
        result.loadedPingUl = Math.round((ulPingSamples.reduce((a, b) => a + b, 0) / ulPingSamples.length) * 10) / 10
      }

      // -----------------------------------------------------------
      // Phase 4: Bufferbloat Calculation & Final Synthesis
      // -----------------------------------------------------------
      const loadedCandidates = [result.loadedPingDl, result.loadedPingUl].filter(v => v > 0)
      if (loadedCandidates.length > 0) {
        result.loadedPing = Math.round((loadedCandidates.reduce((a, b) => a + b, 0) / loadedCandidates.length) * 10) / 10
        result.bufferbloatDelta = Math.max(0, Math.round((result.loadedPing - result.ping) * 10) / 10)
        
        if (result.bufferbloatDelta <= 5) result.bufferbloatGrade = 'A+ (Minimal)'
        else if (result.bufferbloatDelta <= 15) result.bufferbloatGrade = 'A (Low)'
        else if (result.bufferbloatDelta <= 30) result.bufferbloatGrade = 'B (Moderate)'
        else if (result.bufferbloatDelta <= 60) result.bufferbloatGrade = 'C (Degraded)'
        else result.bufferbloatGrade = 'D (High)'
      } else {
        result.loadedPing = result.ping
        result.bufferbloatGrade = 'A'
      }

      const totalElapsedSec = (performance.now() - overallStartTime) / 1000
      result.durationSec = Math.round(totalElapsedSec * 10) / 10

      safeProgress({
        phase: 'complete',
        speedMbps: 0,
        progressPct: 100,
        pingMs: result.ping,
        jitterMs: result.jitter,
        downloadMbps: result.download,
        uploadMbps: result.upload,
        bytesTransferred: result.bytesTransferred,
        graphPoints: [...graphPoints],
        result
      })

      this.isRunning = false
      return result
    } catch (err) {
      if (signal.aborted || this.currentRunId !== runId) {
        return null
      }
      safeProgress({ phase: 'cancelled' })
      return null
    } finally {
      this.isRunning = false
      this.abortController = null
      for (const timer of this.activeIntervals) {
        clearInterval(timer)
      }
      this.activeIntervals = []
    }
  }
}
