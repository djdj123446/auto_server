<template>
  <div>
    <!-- 顶部状态条 -->
    <div class="page-card status-bar">
      <div class="status-left">
        <div class="online-pill" :class="{ off: !online }">
          <i class="pulse-dot"></i>
          {{ online ? '设备在线' : '设备离线' }}
        </div>
        <span class="status-time">最近上报: {{ lastSeen || '--' }}</span>
      </div>
      <div class="status-chips">
        <span class="st-chip" :class="{ on: led === 1 }">
          <el-icon :size="16"><Sunny /></el-icon>LED: {{ led === 1 ? '开启' : '关闭' }}
        </span>
        <span class="st-chip" :class="{ on: fan > 0 }">
          <el-icon :size="16"><WindPower /></el-icon>风扇: {{ FAN_NAMES[fan] ?? '--' }}
        </span>
        <span class="st-chip" :class="'m' + mode">
          <el-icon :size="16"><component :is="modeIcon" /></el-icon>模式: {{ modeName }}
        </span>
      </div>
    </div>

    <!-- 专注模式横幅（图形化计时） -->
    <transition name="fade">
      <div v-if="mode === 2" class="focus-banner" :class="{ paused: focusState.paused }">
        <div class="focus-pulse"><el-icon :size="30"><Timer /></el-icon></div>
        <div class="focus-info">
          <div class="focus-title">{{ focusState.paused ? '专注已暂停' : '专注进行中' }}</div>
          <div class="focus-sub">
            {{ focusState.paused ? '等待确认是否结束本次专注' : '满休息提醒间隔设备会提醒休息' }}
          </div>
        </div>
        <div class="focus-time">{{ focusText }}</div>
      </div>
    </transition>

    <!-- 数据卡片：环形仪表（单块玻璃卡承载四列，减少同屏模糊层数） -->
    <div class="page-card metric-card">
      <div class="metric-grid">
        <div class="metric-item" v-for="m in metrics" :key="m.label">
          <div
            class="ring"
            :style="{ background: `conic-gradient(${m.color} ${m.pct}%, rgba(31,45,61,0.07) 0)` }"
          >
            <div class="ring-hole">
              <el-icon :size="20" :color="m.color" class="ring-icon"><component :is="m.icon" /></el-icon>
              <div class="ring-val">
                {{ m.value == null ? '--' : m.value }}<i>{{ m.unit }}</i>
              </div>
            </div>
          </div>
          <div class="metric-label">{{ m.label }}</div>
          <div class="metric-extra">{{ m.extra }}</div>
        </div>
      </div>
    </div>

    <!-- 实时曲线 -->
    <div class="page-card">
      <div class="card-title">实时曲线（最近 5 分钟）</div>
      <div ref="chartRef" class="chart"></div>
    </div>
  </div>
</template>

<script setup>
import { computed, onMounted, onUnmounted, reactive, ref } from 'vue'
// 按需引入 echarts（全量引入约 1MB，按需后仅打包用到的部分）
import * as echarts from 'echarts/core'
import { LineChart } from 'echarts/charts'
import { GridComponent, TooltipComponent, LegendComponent } from 'echarts/components'
import { CanvasRenderer } from 'echarts/renderers'

echarts.use([LineChart, GridComponent, TooltipComponent, LegendComponent, CanvasRenderer])

import { Odometer, TrendCharts, Cloudy, Sunrise, Sunny, WindPower, MagicStick, Pointer, Timer, User } from '@element-plus/icons-vue'
import api from '../api'
import { wsOn } from '../ws'
import { FAN_NAMES } from '../format'
import { focusState, applyFocusState, formatElapsed } from '../focus'

const online = ref(false)
const lastSeen = ref('--')
const led = ref(null)
const fan = ref(null)
const mode = ref(null)

const modeName = computed(() => ({ 1: '自动', 0: '手动', 2: '专注' }[mode.value] ?? '--'))
const modeIcon = computed(() => ({ 1: MagicStick, 0: Pointer, 2: Timer }[mode.value] ?? MagicStick))

// 专注计时：读取共享的 focusState.elapsed（AppLayout 统一插值），
// 弹框暂停态由 focusState.paused 驱动（与设备端计时暂停一致）
const focusText = computed(() => formatElapsed(focusState.elapsed))

// 环形仪表：图形大于文字
const metrics = reactive([
  { label: '温度', value: null, unit: '°C', pct: 0, extra: '舒适区间: 22 ~ 28 °C', icon: Odometer, color: '#f78c3c', max: 40 },
  { label: '湿度', value: null, unit: '%', pct: 0, extra: '舒适区间: 40 ~ 70 %', icon: Cloudy, color: '#409eff', max: 100 },
  { label: '光照', value: null, unit: 'lx', pct: 0, extra: '阅读建议: 300 lx 以上', icon: Sunrise, color: '#f7b731', max: 1000 },
  { label: '人体感应', value: null, unit: '', pct: 0, extra: '--', icon: User, color: '#8e44ad', max: 1 },
])

const chartRef = ref(null)
let chart = null

// 滚动窗口：最近 150 个点（10 秒上报一次约 25 分钟）
const MAX_POINTS = 150
const buf = { ts: [], temperature: [], humidity: [], light: [] }

function pushState(data) {
  const now = new Date()
  const p = (n) => String(n).padStart(2, '0')
  const hhmmss = `${p(now.getHours())}:${p(now.getMinutes())}:${p(now.getSeconds())}`

  // 同一秒的重复推送（snapshot + latest 初始双拉）只画一个点
  if (buf.ts[buf.ts.length - 1] === hhmmss) return

  buf.ts.push(hhmmss)
  buf.temperature.push(data.temperature ?? 0)
  buf.humidity.push(data.humidity ?? 0)
  buf.light.push(data.light ?? 0)
  if (buf.ts.length > MAX_POINTS) {
    buf.ts.shift()
    buf.temperature.shift()
    buf.humidity.shift()
    buf.light.shift()
  }
}

function renderChart() {
  if (!chart) return
  // lazyUpdate: 数据更新先缓存、下一帧再统一渲染，避免高频推送时逐条重绘
  chart.setOption({
    xAxis: { data: buf.ts },
    series: [
      { data: buf.temperature },
      { data: buf.humidity },
      { data: buf.light },
    ],
  }, { lazyUpdate: true })
}

function applyState(data) {
  metrics[0].value = data.temperature
  metrics[1].value = data.humidity
  metrics[2].value = data.light
  metrics[3].value = data.people == null ? null : (data.people ? '有人' : '无人')
  metrics[3].extra = data.people ? '书房有人，注意通风' : '书房无人'

  // 环形进度：值 / 量程，人体感应有人=满环
  const pct = (i, v, max) => {
    if (v == null) return 0
    return Math.min(100, Math.max(0, (Number(v) / max) * 100))
  }
  metrics[0].pct = pct(0, data.temperature, metrics[0].max)
  metrics[1].pct = pct(1, data.humidity, metrics[1].max)
  metrics[2].pct = pct(2, data.light, metrics[2].max)
  metrics[3].pct = data.people ? 100 : 0

  led.value = data.led
  fan.value = data.fan
  mode.value = data.mode

  // 专注计时/弹框状态统一交给 focus.js（含精确 focus_ms 插值与 focus_paused 冻结）
  applyFocusState(data)

  lastSeen.value = new Date().toLocaleTimeString('zh-CN', { hour12: false })
  pushState(data)
  renderChart()
}

const onStatus = (data) => {
  online.value = !!data.online
  // 设备离线：专注等同正常结束 —— 本地切回手动，专注横幅随之隐藏（数据已由云端落库）
  if (!data.online && mode.value === 2) mode.value = 0
}

// 仅实时上报的 state 事件才算在线（与顶部栏 AppLayout 逻辑一致；
// snapshot / latest 里的历史数据不允许把状态翻回在线）
const onState = (data) => {
  online.value = true
  applyState(data)
}

let offlineTick = null
const offs = []

function toLocalIso(d) {
  const p = (n) => String(n).padStart(2, '0')
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}T${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`
}

function setLastSeen(ts) {
  if (!ts) return
  lastSeen.value = new Date(ts).toLocaleTimeString('zh-CN', { hour12: false })
}

onMounted(async () => {
  chart = echarts.init(chartRef.value)
  chart.setOption({
    tooltip: { trigger: 'axis' },
    legend: { data: ['温度 (°C)', '湿度 (%)', '光照 (lx)'], top: 0 },
    grid: { left: 56, right: 56, top: 40, bottom: 32 },
    xAxis: { type: 'category', data: [], boundaryGap: false },
    yAxis: [
      { type: 'value', name: '温度/湿度', min: 0, max: 100 },
      { type: 'value', name: '光照', splitLine: { show: false } },
    ],
    series: [
      { name: '温度 (°C)', type: 'line', smooth: true, showSymbol: false, data: [], animation: false, itemStyle: { color: '#f78c3c' } },
      { name: '湿度 (%)', type: 'line', smooth: true, showSymbol: false, data: [], animation: false, itemStyle: { color: '#409eff' } },
      { name: '光照 (lx)', type: 'line', smooth: true, showSymbol: false, yAxisIndex: 1, data: [], animation: false, itemStyle: { color: '#f7b731' } },
    ],
  })
  window.addEventListener('resize', onResize)

  // 先预填最近 5 分钟历史，再接实时流：
  // 实时点用"本地接收时间"入队，若先收实时点后补历史，历史会排到实时点后面，导致时间轴乱序
  try {
    const end = new Date()
    const start = new Date(Date.now() - 5 * 60 * 1000)
    const { data } = await api.get('/data/history', {
      params: { start: toLocalIso(start), end: toLocalIso(end), points: 50 },
    })
    for (const r of data.rows || []) {
      buf.ts.push(r.ts.slice(11, 19))
      buf.temperature.push(r.temperature ?? 0)
      buf.humidity.push(r.humidity ?? 0)
      buf.light.push(r.light ?? 0)
    }
    if (buf.ts.length > MAX_POINTS) {
      const cut = buf.ts.length - MAX_POINTS
      buf.ts.splice(0, cut)
      buf.temperature.splice(0, cut)
      buf.humidity.splice(0, cut)
      buf.light.splice(0, cut)
    }
    renderChart()
  } catch {
    /* 无历史数据时忽略 */
  }

  offs.push(wsOn('state', onState))
  offs.push(wsOn('status', onStatus))
  offs.push(wsOn('snapshot', (snap) => {
    online.value = !!snap.online
    setLastSeen(snap.last_seen)
    if (snap.data) applyState(snap.data)
  }))

  // 初始加载最新状态（设备离线时返回数据库里最后一次的数据）
  try {
    const { data } = await api.get('/data/latest')
    online.value = !!data.online
    setLastSeen(data.last_seen)
    if (data.data) applyState(data.data)
  } catch {
    /* 后端未就绪时静默 */
  }

  // 每 30 秒做一次离线超时的本地校验（后端也会推 status）
  offlineTick = setInterval(async () => {
    try {
      const { data } = await api.get('/data/latest')
      online.value = !!data.online
    } catch { /* 忽略 */ }
  }, 30000)
})

const onResize = () => chart && chart.resize()

onUnmounted(() => {
  window.removeEventListener('resize', onResize)
  clearInterval(offlineTick)
  offs.forEach((off) => off())
  chart && chart.dispose()
})
</script>

<style scoped>
.status-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 18px 28px;
}
.status-left {
  display: flex;
  align-items: center;
  gap: 16px;
}
.status-time {
  color: #909399;
  font-size: 14px;
}

/* 在线状态胶囊 + 呼吸点 */
.online-pill {
  display: inline-flex;
  align-items: center;
  gap: 9px;
  padding: 7px 18px;
  border-radius: 999px;
  font-size: 14px;
  font-weight: 600;
  color: #16803c;
  background: rgba(103, 194, 58, 0.16);
  border: 1px solid rgba(103, 194, 58, 0.35);
}
.online-pill.off {
  color: #b91c1c;
  background: rgba(245, 108, 108, 0.14);
  border-color: rgba(245, 108, 108, 0.35);
}
.pulse-dot {
  width: 9px;
  height: 9px;
  border-radius: 50%;
  background: #22c55e;
  position: relative;
  flex-shrink: 0;
}
/* 用伪元素缩放代替 box-shadow 动画（走合成器，不触发重绘） */
.pulse-dot::after {
  content: '';
  position: absolute;
  inset: 0;
  border-radius: 50%;
  background: #22c55e;
  animation: breathe 1.8s ease-out infinite;
}
.online-pill.off .pulse-dot {
  background: #ef4444;
}
.online-pill.off .pulse-dot::after {
  background: #ef4444;
  animation: none;
}
@keyframes breathe {
  0% { transform: scale(1); opacity: 0.55; }
  100% { transform: scale(3.2); opacity: 0; }
}

/* 状态芯片 */
.status-chips {
  display: flex;
  gap: 14px;
}
.st-chip {
  display: inline-flex;
  align-items: center;
  gap: 7px;
  padding: 8px 16px;
  border-radius: 999px;
  font-size: 14px;
  color: #909399;
  background: #f5f7fa;
  border: 1px solid #e4e7ed;
  transition: color 0.3s, background-color 0.3s, border-color 0.3s;
}
.st-chip.on {
  color: #b45309;
  background: #fdf6ec;
  border-color: #f5dab1;
}
.st-chip.m2 {
  color: #c2410c;
  background: #fef0f0;
  border-color: #fbc4c4;
}
.st-chip.m1 {
  color: #16803c;
  background: #f0f9eb;
  border-color: #c2e7b0;
}

/* ===================== 专注横幅 ===================== */
.focus-banner {
  display: flex;
  align-items: center;
  gap: 20px;
  padding: 20px 30px;
  margin-bottom: 24px;
  border-radius: 12px;
  background: #fef0f0;
  border: 1px solid #fbc4c4;
}
.focus-pulse {
  width: 58px;
  height: 58px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #f56c6c;
  background: #fff;
  position: relative;
  flex-shrink: 0;
}
/* 同 pulse-dot：伪元素缩放代替 box-shadow 呼吸 */
.focus-pulse::after {
  content: '';
  position: absolute;
  inset: 0;
  border-radius: 50%;
  border: 2px solid rgba(245, 108, 108, 0.5);
  animation: focus-breathe 2s ease-out infinite;
}
@keyframes focus-breathe {
  0% { transform: scale(1); opacity: 1; }
  100% { transform: scale(1.5); opacity: 0; }
}
.focus-title {
  font-size: 17px;
  font-weight: 700;
  color: #c2410c;
}
.focus-sub {
  font-size: 13px;
  color: #b57860;
  margin-top: 5px;
}
/* 弹框暂停态：横幅整体转灰，与设备端"计时已暂停"对应 */
.focus-banner.paused {
  background: #f4f4f5;
  border-color: #d3d4d6;
}
.focus-banner.paused .focus-pulse {
  color: #909399;
}
.focus-banner.paused .focus-pulse::after {
  border-color: rgba(144, 147, 153, 0.45);
  animation: none;
}
.focus-banner.paused .focus-title {
  color: #606266;
}
.focus-banner.paused .focus-sub {
  color: #909399;
}
.focus-time {
  margin-left: auto;
  font-size: 40px;
  font-weight: 700;
  color: #1f2d3d;
  font-variant-numeric: tabular-nums;
  letter-spacing: 2px;
}
.fade-enter-active, .fade-leave-active {
  transition: opacity 0.3s, transform 0.3s;
}
.fade-enter-from, .fade-leave-to {
  opacity: 0;
  transform: translateY(-8px);
}

/* ===================== 环形仪表卡 ===================== */
.metric-card {
  padding: 30px 20px 26px;
}
.metric-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 12px;
}
@media (max-width: 992px) {
  .metric-grid {
    grid-template-columns: repeat(2, 1fr);
    row-gap: 26px;
  }
}
.metric-item {
  text-align: center;
}
.ring {
  width: 148px;
  height: 148px;
  margin: 0 auto 18px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
}
.ring-hole {
  width: 120px;
  height: 120px;
  border-radius: 50%;
  background: #fff;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 4px;
}
.ring-icon {
  opacity: 0.9;
}
.ring-val {
  font-size: 27px;
  font-weight: 700;
  color: #1f2d3d;
  line-height: 1;
  font-variant-numeric: tabular-nums;
}
.ring-val i {
  font-style: normal;
  font-size: 13px;
  font-weight: 400;
  color: #909399;
  margin-left: 3px;
}
.metric-label {
  color: #606266;
  font-size: 14px;
  font-weight: 600;
  margin-bottom: 10px;
}
.metric-extra {
  font-size: 12px;
  color: #b0b3b8;
}
.chart {
  height: 380px;
}
</style>
