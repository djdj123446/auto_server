/** 通用格式化工具 */
export function fmtTemp(v) {
  return v == null ? '--' : `温度: ${Number(v).toFixed(1)} °C`
}

export function fmtHumi(v) {
  return v == null ? '--' : `湿度: ${Number(v).toFixed(0)} %`
}

export function fmtLight(v) {
  return v == null ? '--' : `光照: ${Number(v)} lx`
}

export function fmtPeople(v) {
  if (v == null) return '人体: --'
  return v ? '人体感应: 有人' : '人体感应: 无人'
}

export function fmtLed(v) {
  if (v == null) return 'LED: --'
  return `LED: ${Number(v) === 1 ? '开启' : '关闭'}`
}

export const FAN_NAMES = ['关闭', '一档', '二档', '三档']

export function fmtFan(v) {
  if (v == null) return '风扇: --'
  return `风扇: ${FAN_NAMES[Number(v)] ?? Number(v)} 档`
}

export function fmtMode(v) {
  if (v == null) return '模式: --'
  return `模式: ${Number(v) === 1 ? '自动' : '手动'}`
}

export function fmtTime(ts) {
  if (!ts) return '--'
  const d = new Date(ts)
  const p = (n) => String(n).padStart(2, '0')
  return `${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`
}

export function fmtDateTime(ts) {
  if (!ts) return '--'
  const d = new Date(ts)
  const p = (n) => String(n).padStart(2, '0')
  return `${d.getMonth() + 1}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`
}
