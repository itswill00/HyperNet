let cbSeq = 0

export function execCommand(cmd, timeoutMs = 45000) {
  return new Promise((resolve, reject) => {
    if (typeof ksu !== 'undefined' && typeof ksu.exec === 'function') {
      const id = `_hnet_${++cbSeq}_${Date.now()}`

      const timer = setTimeout(() => {
        if (window[id]) {
          delete window[id]
          resolve('')
        }
      }, timeoutMs)

      window[id] = (errno, stdout, stderr) => {
        clearTimeout(timer)
        delete window[id]
        resolve(stdout || stderr || '')
      }

      try {
        ksu.exec(cmd, '{}', id)
      } catch (e) {
        clearTimeout(timer)
        delete window[id]
        reject(e)
      }
    } else if (typeof exec === 'function') {
      exec(cmd)
        .then(r => resolve(typeof r === 'object' ? (r.stdout || r.stderr || '') : String(r)))
        .catch(reject)
    } else {
      fetch('/api/exec', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ cmd })
      })
      .then(res => res.text())
      .then(resolve)
      .catch(() => {
        console.warn('Running without root bridge (browser mock mode):', cmd)
        resolve('')
      })
    }
  })
}

export function getBridgeBinary() {
  return `if [ -x /data/adb/modules/hypernet/system/bin/libhypernet.so ]; then echo /data/adb/modules/hypernet/system/bin/libhypernet.so; elif [ -x /data/adb/modules_update/hypernet/system/bin/libhypernet.so ]; then echo /data/adb/modules_update/hypernet/system/bin/libhypernet.so; elif [ -x /system/bin/libhypernet.so ]; then echo /system/bin/libhypernet.so; elif [ -x /data/data/com.termux/files/home/HyperNet_Module/system/bin/libhypernet.so ]; then echo /data/data/com.termux/files/home/HyperNet_Module/system/bin/libhypernet.so; else echo ""; fi`
}

export async function runBridge(action, ...args) {
  const safeArgs = args.map(a => "'" + String(a).replace(/'/g, "'\\''") + "'").join(' ')
  const cmd = `for bin in /data/adb/modules/hypernet/system/bin/libhypernet.so /data/adb/modules_update/hypernet/system/bin/libhypernet.so /system/bin/libhypernet.so /data/data/com.termux/files/home/HyperNet_Module/system/bin/libhypernet.so; do if [ -x "$bin" ]; then exec "$bin" ${action} ${safeArgs}; fi; done; echo '{"error":"bridge_binary_not_found"}'`
  try {
    const raw = await execCommand(cmd, 30000)
    return raw.trim()
  } catch (err) {
    console.error(`Error running bridge action ${action}:`, err)
    return ''
  }
}

export async function runBridgeJson(action, ...args) {
  const raw = await runBridge(action, ...args)
  if (!raw) return null
  try {
    return JSON.parse(raw)
  } catch (e) {
    console.warn(`Failed to parse bridge JSON for ${action}:`, raw)
    return null
  }
}
