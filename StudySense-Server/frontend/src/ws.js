/**
 * WebSocket 实时通道（单例）
 * 用法：wsOn('state', cb) 订阅；页面 onMounted 里订阅，onUnmounted 里无需处理（简单场景）
 */
let ws = null
let timer = null
const handlers = {}

export function wsConnect() {
  const token = localStorage.getItem('token')
  if (!token) return
  if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) return

  const proto = location.protocol === 'https:' ? 'wss' : 'ws'
  ws = new WebSocket(`${proto}://${location.host}/ws/realtime?token=${token}`)

  ws.onmessage = (e) => {
    try {
      const msg = JSON.parse(e.data)
        ; (handlers[msg.type] || []).forEach((cb) => cb(msg.data ?? msg))
    } catch {
      /* 忽略非法消息 */
    }
  }
  ws.onclose = () => {
    timer = setTimeout(wsConnect, 3000)
  }
  ws.onerror = () => ws && ws.close()
}

export function wsDisconnect() {
  clearTimeout(timer)
  if (ws) {
    ws.onclose = null
    ws.close()
    ws = null
  }
}

/**
 * 订阅某类消息，返回取消订阅函数
 * 用法：const off = wsOn('state', cb)；onUnmounted(() => off())
 */
export function wsOn(type, cb) {
  ; (handlers[type] ||= []).push(cb)
  return () => {
    const arr = handlers[type] || []
    const i = arr.indexOf(cb)
    if (i > -1) arr.splice(i, 1)
  }
}
