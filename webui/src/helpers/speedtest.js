export class SpeedTestEngine {
  constructor() {
    this.abortController = null
    this.isRunning = false
  }

  abort() {
    if (this.abortController) {
      this.abortController.abort()
      this.abortController = null
    }
    this.isRunning = false
  }

  async runTest(options = {}, onProgress = () => {}) {
    if (this.isRunning) return
    this.isRunning = true
    this.abortController = new AbortController()
    const signal = this.abortController.signal

    const serverUrl = options.serverUrl || 'https://speed.cloudflare.com'
    const durationSec = options.durationSec || 9

    const result = {
      ping: 0,
      jitter: 0,
      download: 0,
      upload: 0,
      bytesTransferred: 0,
      server: options.serverName || 'Cloudflare edge'
    }

    try {
      // 1. Latency & Jitter phase
      onProgress({
        phase: 'ping',
        speedMbps: 0,
        progressPct: 5,
        pingMs: 0,
        jitterMs: 0,
        downloadMbps: 0,
        uploadMbps: 0,
        bytesTransferred: 0
      })

      const pingSamples = []
      for (let i = 0; i < 6; i++) {
        if (signal.aborted) break
        const start = performance.now()
        try {
          const resp = await fetch(`${serverUrl}/__down?bytes=0&_t=${Date.now()}_${i}`, {
            method: 'GET',
            cache: 'no-store',
            signal
          })
          await resp.text()
          const duration = performance.now() - start
          pingSamples.push(duration)
        } catch (e) {
          if (signal.aborted) throw e
        }
        await new Promise(r => setTimeout(r, 60))
      }

      if (pingSamples.length > 0) {
        pingSamples.sort((a, b) => a - b)
        const avg = pingSamples.reduce((sum, v) => sum + v, 0) / pingSamples.length
        let jitterSum = 0
        for (let i = 1; i < pingSamples.length; i++) {
          jitterSum += Math.abs(pingSamples[i] - pingSamples[i - 1])
        }
        result.ping = Math.round(avg * 10) / 10
        result.jitter = Math.round((jitterSum / Math.max(1, pingSamples.length - 1)) * 10) / 10
      }

      onProgress({
        phase: 'ping_done',
        speedMbps: 0,
        progressPct: 15,
        pingMs: result.ping,
        jitterMs: result.jitter,
        downloadMbps: 0,
        uploadMbps: 0,
        bytesTransferred: 0
      })

      // 2. Download phase
      onProgress({
        phase: 'download',
        speedMbps: 0,
        progressPct: 15,
        pingMs: result.ping,
        jitterMs: result.jitter,
        downloadMbps: 0,
        uploadMbps: 0,
        bytesTransferred: result.bytesTransferred
      })

      let totalDownloadBytes = 0
      let lastBytes = 0
      let lastTime = performance.now()
      const downloadStartTime = performance.now()
      const downloadEndTime = downloadStartTime + (durationSec * 1000)
      const graphPoints = []

      // Worker stream downloader
      const downloadWorker = async (chunkBytes) => {
        while (performance.now() < downloadEndTime && !signal.aborted) {
          try {
            const resp = await fetch(`${serverUrl}/__down?bytes=${chunkBytes}&_t=${Date.now()}_${Math.random()}`, {
              cache: 'no-store',
              signal
            })
            if (!resp.body) break
            const reader = resp.body.getReader()
            while (!signal.aborted) {
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
            await new Promise(r => setTimeout(r, 100))
          }
        }
      }

      // Interval ticker for instantaneous speed reporting
      let currentInstantMbps = 0
      const tickInterval = setInterval(() => {
        if (signal.aborted) {
          clearInterval(tickInterval)
          return
        }
        const now = performance.now()
        const timeDelta = (now - lastTime) / 1000
        const bytesDelta = totalDownloadBytes - lastBytes

        if (timeDelta > 0.05) {
          const instantSpeed = (bytesDelta * 8) / (timeDelta * 1000000)
          // Smooth with exponential moving average
          currentInstantMbps = currentInstantMbps === 0 ? instantSpeed : (currentInstantMbps * 0.7 + instantSpeed * 0.3)
          lastBytes = totalDownloadBytes
          lastTime = now

          const elapsedSec = (now - downloadStartTime) / 1000
          const progressPct = Math.min(60, 15 + Math.round((elapsedSec / durationSec) * 45))
          graphPoints.push(Math.round(currentInstantMbps * 10) / 10)

          onProgress({
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
      }, 100)

      // Launch parallel streams
      const streamCount = 4
      const promises = []
      const chunkSizes = [10000000, 25000000, 25000000, 50000000]
      for (let s = 0; s < streamCount; s++) {
        promises.push(downloadWorker(chunkSizes[s] || 25000000))
      }

      await Promise.all(promises)
      clearInterval(tickInterval)

      const totalDownloadTimeSec = (performance.now() - downloadStartTime) / 1000
      result.download = Math.round(((totalDownloadBytes * 8) / (Math.max(1, totalDownloadTimeSec) * 1000000)) * 10) / 10
      result.bytesTransferred += totalDownloadBytes

      // 3. Upload phase
      onProgress({
        phase: 'upload',
        speedMbps: 0,
        progressPct: 60,
        pingMs: result.ping,
        jitterMs: result.jitter,
        downloadMbps: result.download,
        uploadMbps: 0,
        bytesTransferred: result.bytesTransferred
      })

      let totalUploadBytes = 0
      let lastUploadBytes = 0
      let lastUploadTime = performance.now()
      const uploadStartTime = performance.now()
      const uploadEndTime = uploadStartTime + ((durationSec * 0.8) * 1000)
      const uploadChunk = new Uint8Array(1024 * 512) // 512KB payload chunk

      const uploadWorker = async () => {
        while (performance.now() < uploadEndTime && !signal.aborted) {
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
            await new Promise(r => setTimeout(r, 100))
          }
        }
      }

      let currentInstantUploadMbps = 0
      const uploadTickInterval = setInterval(() => {
        if (signal.aborted) {
          clearInterval(uploadTickInterval)
          return
        }
        const now = performance.now()
        const timeDelta = (now - lastUploadTime) / 1000
        const bytesDelta = totalUploadBytes - lastUploadBytes

        if (timeDelta > 0.05) {
          const instantSpeed = (bytesDelta * 8) / (timeDelta * 1000000)
          currentInstantUploadMbps = currentInstantUploadMbps === 0 ? instantSpeed : (currentInstantUploadMbps * 0.7 + instantSpeed * 0.3)
          lastUploadBytes = totalUploadBytes
          lastUploadTime = now

          const elapsedSec = (now - uploadStartTime) / 1000
          const progressPct = Math.min(98, 60 + Math.round((elapsedSec / (durationSec * 0.8)) * 38))

          onProgress({
            phase: 'upload',
            speedMbps: Math.round(currentInstantUploadMbps * 10) / 10,
            progressPct,
            pingMs: result.ping,
            jitterMs: result.jitter,
            downloadMbps: result.download,
            uploadMbps: Math.round(currentInstantUploadMbps * 10) / 10,
            bytesTransferred: result.bytesTransferred + totalUploadBytes
          })
        }
      }, 100)

      const uploadStreams = 3
      const upPromises = []
      for (let u = 0; u < uploadStreams; u++) {
        upPromises.push(uploadWorker())
      }

      await Promise.all(upPromises)
      clearInterval(uploadTickInterval)

      const totalUploadTimeSec = (performance.now() - uploadStartTime) / 1000
      result.upload = Math.round(((totalUploadBytes * 8) / (Math.max(1, totalUploadTimeSec) * 1000000)) * 10) / 10
      result.bytesTransferred += totalUploadBytes

      // Final complete
      onProgress({
        phase: 'complete',
        speedMbps: 0,
        progressPct: 100,
        pingMs: result.ping,
        jitterMs: result.jitter,
        downloadMbps: result.download,
        uploadMbps: result.upload,
        bytesTransferred: result.bytesTransferred,
        graphPoints: [...graphPoints]
      })

      return result
    } catch (err) {
      if (signal.aborted) {
        onProgress({ phase: 'cancelled' })
        return null
      }
      console.error('Speed test error:', err)
      onProgress({ phase: 'error', error: err.message || 'Test failed' })
      throw err
    } finally {
      this.isRunning = false
      this.abortController = null
    }
  }
}
