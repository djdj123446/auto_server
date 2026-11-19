/**
 * 专注模式交互状态（网页与 ESP32 OLED 同步）
 *
 * 弹框不由网页自行决定，完全跟随设备上报的两个状态位：
 *   focus_paused = 1  设备端"是否结束专注"弹框打开中（计时已暂停）
 *   focus_rest   = 1  设备端休息提醒弹框打开中（计时继续，超时自动关闭）
 *
 * 计时以设备上报的精确毫秒 focus_ms 为基准，本地按墙钟插值走字；
 * 所有页面（仪表盘/控制中心/全局弹框）读同一个 focusState.elapsed，
 * 保证网页内部与设备 OLED 显示一致的 mm:ss（不再有整数秒截断的相位差）。
 *
 * 网页点击模式卡片只是下发 focus_pause，设备弹出弹框后网页跟着弹，
 * 因此两端永远显示同一个弹框；设备端本地按键操作同样会实时反映到网页。
 */
import { reactive } from 'vue'
import api from './api'

export const focusState = reactive({
  paused: false,    // 设备端"是否结束专注"弹框打开中
  rest: false,      // 设备端休息提醒弹框打开中
  mode: null,       // 设备模式（1=自动 0=手动 2=专注），插值计时只在专注模式走字
  elapsed: 0,       // 当前已专注秒数（浮点，本地插值后）
  targetMode: null, // 网页点击模式卡片时记下的目标模式（结束计时后切过去）
  _baseSec: 0,      // 最近一次上报的精确基准秒数
  _at: 0,           // 基准上报的本地时刻（毫秒）
})

let ticker = null

/** 收到设备状态上报时更新（state 事件与 snapshot 快照共用） */
export function applyFocusState(data) {
  if (!data) return
  const wasFocus = focusState.mode === 2
  if (data.mode != null) focusState.mode = Number(data.mode)

  // 精确毫秒优先（focus_ms 由固件上报），缺省退回整数秒（历史/快照兜底）
  const baseSec = data.focus_ms != null
    ? Number(data.focus_ms) / 1000
    : Number(data.focus ?? 0) || 0
  focusState._baseSec = baseSec

  // 插值锚点用服务器接收时刻（_ts），补偿 MQTT->WS 传输延迟；
  // 超过一个上报周期未更新的旧数据（离线兜底）仍用本地时刻，避免计时虚增
  const ts = Number(data._ts) || 0
  focusState._at = ts > 0 && Date.now() - ts < 15000
    ? Math.min(ts, Date.now())
    : Date.now()
    ? Math.min(ts, Date.now())
    : Date.now()

  // 刚切入专注且计时≈0：这个 0 对应的是"现在"而不是上一次上报时刻，
  // 锚本地时刻，避免计时从"距上次上报的秒数"起步（不是从 0 开始）
  if (focusState.mode === 2 && !wasFocus && baseSec < 1) {
    focusState._at = Date.now()
  }

  focusState.elapsed = baseSec

  const paused = Number(data.focus_paused) === 1

  // 弹框被关闭（设备本地按键 / 休息提醒超时自动继续 / 云端结束）时，
  // 清掉网页暂存的目标模式，避免影响下一次交互
  if (focusState.paused && !paused) focusState.targetMode = null

  focusState.paused = paused
  focusState.rest = Number(data.focus_rest) === 1
}

/** 每秒插值走字（专注模式且未暂停时），由 AppLayout 统一驱动 */
function focusTick() {
  if (focusState.mode !== 2 || focusState.paused || !focusState._at) return
  focusState.elapsed = focusState._baseSec + (Date.now() - focusState._at) / 1000
}

/** 设备离线：专注等同正常结束 —— 清零计时、收弹框、退出专注显示（数据已由云端落库） */
export function focusDeviceOffline() {
  focusState.mode = null
  focusState.elapsed = 0
  focusState._baseSec = 0
  focusState._at = 0
  focusState.paused = false
  focusState.rest = false
  focusState.targetMode = null
}

export function startFocusTicker() {
  if (ticker) return
  focusTick()
  ticker = setInterval(focusTick, 1000)
}

export function stopFocusTicker() {
  if (ticker) {
    clearInterval(ticker)
    ticker = null
  }
}

/** 秒数格式化为 mm:ss / h:mm:ss */
export function formatElapsed(sec) {
  const s = Math.max(0, Math.floor(Number(sec) || 0))
  const p = (n) => String(n).padStart(2, '0')
  const h = Math.floor(s / 3600)
  const m = Math.floor((s % 3600) / 60)
  return h > 0 ? `${h}:${p(m)}:${p(s % 60)}` : `${p(m)}:${p(s % 60)}`
}

/** 休息提醒弹框文案用的"已专注 N 分钟"：
 *  弹框恰好在满一个提醒周期时打开，直接由已专注秒数换算即可 */
export function elapsedMinutes(sec) {
  return Math.max(1, Math.round((Number(sec) || 0) / 60))
}

export const focusCmd = {
  /** 专注中打开"是否结束专注"弹框并暂停计时 */
  pause: () => api.post('/control/focus/pause'),
  /** 弹框选择"继续"：恢复计时 */
  continue: () => api.post('/control/focus/continue'),
  /** 弹框选择"结束计时"：切到目标模式（0=手动 1=自动） */
  end: (value) => api.post('/control/focus/end', { value }),
}
