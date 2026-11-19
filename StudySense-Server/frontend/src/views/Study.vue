<template>
  <div>
    <!-- 概览统计卡 -->
    <el-row :gutter="24">
      <el-col :span="6" v-for="c in cards" :key="c.label">
        <div class="page-card stat-card">
          <div class="stat-head">
            <div class="stat-icon" :style="{ background: c.bg, color: c.color }">
              <el-icon :size="20"><component :is="c.icon" /></el-icon>
            </div>
            <div class="stat-label">{{ c.label }}</div>
          </div>
          <div class="stat-value">{{ c.value }}</div>
          <div class="stat-sub">{{ c.sub }}</div>
        </div>
      </el-col>
    </el-row>

    <el-row :gutter="24">
      <!-- 每日专注时长 -->
      <el-col :span="12">
        <div class="page-card">
          <div class="card-title">每日专注时长（近 30 天）</div>
          <div ref="dailyRef" class="chart"></div>
        </div>
      </el-col>
      <!-- 时段分布 -->
      <el-col :span="12">
        <div class="page-card">
          <div class="card-title">24 小时专注分布</div>
          <div ref="hoursRef" class="chart"></div>
        </div>
      </el-col>
    </el-row>

    <!-- 打卡日历热力图（GitHub 贡献图风格） -->
    <div class="page-card">
      <div class="cal-head">
        <div class="card-title cal-title">学习打卡日历（近一年）</div>
        <div class="cal-side">
          <span v-if="calendar.weeks.length" class="cal-checked">已打卡 {{ calendar.checked }} 天</span>
          <div class="cal-legend">
            <span class="cal-legend-text">少</span>
            <i class="cal-legend-cell lv0"></i>
            <i class="cal-legend-cell lv1"></i>
            <i class="cal-legend-cell lv2"></i>
            <i class="cal-legend-cell lv3"></i>
            <i class="cal-legend-cell lv4"></i>
            <span class="cal-legend-text">多</span>
          </div>
        </div>
      </div>
      <div v-if="calendar.weeks.length" ref="calWrap" class="cal-wrap" @mouseleave="tip = null">
        <div class="cal-main" :style="{ '--cols': calendar.weeks.length }">
          <div class="cal-grid">
            <template v-for="(day, idx) in calendar.flatDays" :key="idx">
              <span v-if="!day" class="cal-cell cal-empty"></span>
              <span
                v-else
                class="cal-cell"
                :class="`lv${day.level}`"
                @mouseenter="onCellEnter($event, day, idx % 7)"
              ></span>
            </template>
          </div>
          <div class="cal-months">
            <span
              v-for="m in calendar.months"
              :key="m.col"
              class="cal-month"
              :style="{ gridColumnStart: m.col + 1 }"
            >{{ m.label }}</span>
          </div>
        </div>
        <div
          v-if="tip"
          class="cal-tip"
          :class="{ below: tip.flip }"
          :style="{ left: `${tip.x}px`, top: `${tip.y}px` }"
        >
          <div class="cal-tip-date">{{ tip.date }}</div>
          <div class="cal-tip-info">
            <template v-if="tip.seconds > 0">专注 {{ fmtDur(tip.seconds) }} · {{ tip.sessions }} 次</template>
            <template v-else>未打卡</template>
          </div>
        </div>
      </div>
      <div v-else class="cal-empty-tip">暂无打卡数据</div>
    </div>

    <!-- 会话记录 -->
    <div class="page-card">
      <div class="card-title">专注会话记录</div>
      <el-table :data="rows" stripe>
        <el-table-column label="开始时间" width="180">
          <template #default="{ row }">{{ fmtFull(row.start_ts) }}</template>
        </el-table-column>
        <el-table-column label="结束时间" width="180">
          <template #default="{ row }">
            <span v-if="row.ongoing" class="ongoing">进行中</span>
            <span v-else>{{ fmtFull(row.end_ts) }}</span>
          </template>
        </el-table-column>
        <el-table-column label="专注时长" width="140">
          <template #default="{ row }">
            <span class="dur">{{ fmtDur(row.duration_sec) }}</span>
          </template>
        </el-table-column>
        <el-table-column label="休息提醒" width="110">
          <template #default="{ row }">{{ row.rest_count }} 次</template>
        </el-table-column>
        <el-table-column prop="device_id" label="设备" />
      </el-table>
      <div class="pager">
        <el-pagination
          layout="total, prev, pager, next"
          :total="total"
          :page-size="pageSize"
          :current-page="page"
          @current-change="onPage"
        />
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed, onMounted, onUnmounted, ref } from 'vue'
import * as echarts from 'echarts/core'
import { BarChart } from 'echarts/charts'
import { GridComponent, TooltipComponent } from 'echarts/components'
import { CanvasRenderer } from 'echarts/renderers'

echarts.use([BarChart, GridComponent, TooltipComponent, CanvasRenderer])

import { Timer, Medal, Trophy, Clock } from '@element-plus/icons-vue'
import api from '../api'

// ---------- 概览 ----------
const summary = ref(null)
const cards = computed(() => {
  const s = summary.value
  return [
    { label: '今日专注', value: s ? fmtDur(s.today_seconds) : '--', sub: '今日累计专注时长', icon: Timer, bg: 'rgba(64,158,255,0.12)', color: '#409eff' },
    { label: '连续打卡', value: s ? `${s.streak_days} 天` : '--', sub: '每天至少专注一次', icon: Trophy, bg: 'rgba(230,90,48,0.12)', color: '#d85a30' },
    { label: '近 7 天合计', value: s ? fmtDur(s.range_seconds) : '--', sub: `共 ${s?.range_sessions ?? 0} 次专注`, icon: Clock, bg: 'rgba(83,74,183,0.12)', color: '#534ab7' },
    { label: '累计专注', value: s ? fmtDur(s.total_seconds) : '--', sub: `历史共 ${s?.total_sessions ?? 0} 次`, icon: Medal, bg: 'rgba(29,158,117,0.12)', color: '#0f6e56' },
  ]
})

// ---------- 图表 ----------
const dailyRef = ref()
const hoursRef = ref()
let charts = []

function fmtDur(sec) {
  const s = Math.max(0, Number(sec) || 0)
  if (s < 60) return `${s} 秒`
  const h = Math.floor(s / 3600)
  const m = Math.round((s % 3600) / 60)
  return h > 0 ? `${h} 小时 ${m} 分` : `${m} 分钟`
}

function fmtFull(ts) {
  if (!ts) return '--'
  const d = new Date(ts)
  const p = (n) => String(n).padStart(2, '0')
  return `${d.getMonth() + 1}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`
}

async function loadDaily() {
  const { data } = await api.get('/study/daily', { params: { days: 30 } })
  const chart = echarts.init(dailyRef.value)
  charts.push(chart)
  chart.setOption({
    grid: { left: 50, right: 16, top: 30, bottom: 28 },
    tooltip: {
      trigger: 'axis',
      valueFormatter: (v) => fmtDur(v),
    },
    xAxis: {
      type: 'category',
      data: data.days.map((d) => d.date.slice(5)),
      axisLabel: { color: '#909399' },
      axisLine: { lineStyle: { color: '#e4e7ed' } },
    },
    yAxis: {
      type: 'value',
      name: '分钟',
      axisLabel: { color: '#909399' },
      splitLine: { lineStyle: { color: '#f0f2f5' } },
    },
    series: [{
      type: 'bar',
      data: data.days.map((d) => Math.round(d.seconds / 60)),
      barMaxWidth: 18,
      itemStyle: { color: '#534ab7', borderRadius: [4, 4, 0, 0] },
    }],
  })
}

async function loadHours() {
  const { data } = await api.get('/study/hours', { params: { days: 30 } })
  const chart = echarts.init(hoursRef.value)
  charts.push(chart)
  chart.setOption({
    grid: { left: 50, right: 16, top: 30, bottom: 28 },
    tooltip: { trigger: 'axis', valueFormatter: (v) => fmtDur(v) },
    xAxis: {
      type: 'category',
      data: data.hours.map((h) => `${h.hour}`),
      axisLabel: { color: '#909399', formatter: (v) => `${v}时` },
      axisLine: { lineStyle: { color: '#e4e7ed' } },
    },
    yAxis: {
      type: 'value',
      name: '分钟',
      axisLabel: { color: '#909399' },
      splitLine: { lineStyle: { color: '#f0f2f5' } },
    },
    series: [{
      type: 'bar',
      data: data.hours.map((h) => Math.round(h.seconds / 60)),
      barMaxWidth: 14,
      itemStyle: { color: '#185fa5', borderRadius: [4, 4, 0, 0] },
    }],
  })
}

// ---------- 打卡日历（GitHub 贡献图风格） ----------
const WEEKS = 53 // 近一年：52 个完整周 + 本周
const calDays = ref([])
const calWrap = ref()
const tip = ref(null)

function parseDate(s) {
  const [y, m, d] = s.split('-').map(Number)
  return new Date(y, m - 1, d)
}

function keyOf(dt) {
  const p = (n) => String(n).padStart(2, '0')
  return `${dt.getFullYear()}-${p(dt.getMonth() + 1)}-${p(dt.getDate())}`
}

function addDays(dt, n) {
  const d = new Date(dt)
  d.setDate(d.getDate() + n)
  return d
}

// 专注时长 → 颜色档位：未打卡 / <10min / <30min / <60min / ≥60min
function levelOf(sec) {
  if (!sec || sec <= 0) return 0
  if (sec < 600) return 1
  if (sec < 1800) return 2
  if (sec < 3600) return 3
  return 4
}

const calendar = computed(() => {
  const list = calDays.value
  if (!list.length) return { weeks: [], months: [], flatDays: [], checked: 0 }
  const byDate = new Map(list.map((d) => [d.date, d]))
  const first = parseDate(list[0].date)
  const last = parseDate(list[list.length - 1].date)
  // 以周日为起点、本周六为终点，保证网格是完整周的矩形
  const start = addDays(first, -first.getDay())
  const end = addDays(last, 6 - last.getDay())
  const totalWeeks = Math.floor((end - start) / 86400000 / 7) + 1

  const weeks = []
  for (let w = 0; w < totalWeeks; w++) {
    const col = []
    for (let dow = 0; dow < 7; dow++) {
      const dt = addDays(start, w * 7 + dow)
      const rec = byDate.get(keyOf(dt))
      col.push(rec ? { ...rec, level: levelOf(rec.seconds) } : null)
    }
    weeks.push(col)
  }

  // 月份标签：标在月份切换的那一列；首列是上一个月的残段，不标注
  const months = []
  const monthOf = (col) => {
    const valid = col.filter(Boolean)
    return valid.length ? parseDate(valid[valid.length - 1].date).getMonth() : null
  }
  let lastMonth = monthOf(weeks[0])
  weeks.forEach((col, w) => {
    if (w === 0) return
    const month = monthOf(col)
    if (month !== null && month !== lastMonth) {
      months.push({ col: w, label: `${month + 1}月` })
      lastMonth = month
    }
  })

  const checked = list.filter((d) => d.seconds > 0).length

  return { weeks, months, flatDays: weeks.flat(), checked }
})

function onCellEnter(evt, day, row) {
  const wrapEl = calWrap.value
  if (!wrapEl) return
  const cell = evt.currentTarget.getBoundingClientRect()
  const wr = wrapEl.getBoundingClientRect()
  const x = cell.left + cell.width / 2 - wr.left
  const flip = row < 2 // 靠近顶部时改为向下弹出，避免遮挡标题
  tip.value = {
    date: day.date,
    seconds: day.seconds,
    sessions: day.sessions,
    x: Math.min(Math.max(x, 80), wr.width - 80),
    y: flip ? cell.bottom - wr.top + 8 : cell.top - wr.top - 8,
    flip,
  }
}

async function loadCalendar() {
  // 固定 53 列（52 周 + 本周），从周日开始，铺满卡片宽度
  const today = new Date()
  today.setHours(0, 0, 0, 0)
  const daysNeeded = (WEEKS - 1) * 7 + today.getDay() + 1
  const { data } = await api.get('/study/daily', { params: { days: daysNeeded } })
  calDays.value = data.days || []
}

// ---------- 会话表 ----------
const rows = ref([])
const total = ref(0)
const page = ref(1)
const pageSize = 15

async function loadSessions() {
  const { data } = await api.get('/study/sessions', { params: { page: page.value, page_size: pageSize } })
  rows.value = data.rows
  total.value = data.total
}

function onPage(p) {
  page.value = p
  loadSessions()
}

function onResize() {
  charts.forEach((c) => c.resize())
}

onMounted(async () => {
  const [s] = await Promise.all([
    api.get('/study/summary', { params: { days: 7 } }),
    loadDaily(),
    loadHours(),
    loadCalendar(),
    loadSessions(),
  ])
  summary.value = s.data
  window.addEventListener('resize', onResize)
})

onUnmounted(() => {
  window.removeEventListener('resize', onResize)
  charts.forEach((c) => c.dispose())
  charts = []
})
</script>

<style scoped>
.stat-card {
  padding: 20px 24px;
  margin-bottom: 24px;
}
.stat-head {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 14px;
}
.stat-icon {
  width: 42px;
  height: 42px;
  border-radius: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
}
.stat-label {
  font-size: 14px;
  color: #606266;
}
.stat-value {
  font-size: 28px;
  font-weight: 700;
  color: #1f2d3d;
}
.stat-sub {
  font-size: 12px;
  color: #909399;
  margin-top: 6px;
}
.chart {
  height: 280px;
}
/* ---------- 打卡日历：GitHub 贡献图风格 ---------- */
.cal-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 16px;
}
.cal-title {
  margin-bottom: 0;
}
.cal-side {
  display: flex;
  align-items: center;
  gap: 16px;
}
.cal-checked {
  font-size: 12px;
  color: var(--text-weak, #909399);
}
.cal-legend {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 12px;
  color: var(--text-weak, #909399);
}
.cal-legend-cell {
  width: 12px;
  height: 12px;
  border-radius: 3px;
  display: inline-block;
}
.cal-wrap {
  position: relative;
  padding: 2px 0 0;
}
/* 铺满卡片宽度：列宽自适应，格子保持正方形 */
.cal-main {
  --gap: 4px;
}
.cal-grid {
  display: grid;
  grid-auto-flow: column;
  grid-template-rows: repeat(7, auto);
  grid-auto-columns: minmax(0, 1fr);
  gap: var(--gap);
}
.cal-months {
  display: grid;
  grid-template-columns: repeat(var(--cols), minmax(0, 1fr));
  gap: var(--gap);
  margin-top: 8px;
  font-size: 12px;
  color: #909399;
}
.cal-month {
  white-space: nowrap;
  line-height: 18px;
}
.cal-cell {
  aspect-ratio: 1;
  border-radius: 4px;
  background: #ebedf0;
  transition: outline-color 0.1s;
  outline: 1px solid transparent;
  outline-offset: -1px;
}
.cal-cell:not(.cal-empty) {
  cursor: pointer;
}
.cal-cell:not(.cal-empty):hover {
  outline-color: rgba(31, 45, 61, 0.35);
}
.cal-empty {
  background: transparent;
}
.lv0 {
  background: #ebedf0;
}
.lv1 {
  background: #9be9a8;
}
.lv2 {
  background: #40c463;
}
.lv3 {
  background: #30a14e;
}
.lv4 {
  background: #216e39;
}
.cal-tip {
  position: absolute;
  transform: translate(-50%, -100%);
  background: #24292f;
  color: #fff;
  padding: 6px 10px;
  border-radius: 6px;
  font-size: 12px;
  line-height: 1.5;
  white-space: nowrap;
  pointer-events: none;
  z-index: 10;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.18);
}
.cal-tip.below {
  transform: translate(-50%, 0);
}
.cal-tip-date {
  font-weight: 600;
}
.cal-tip-info {
  color: rgba(255, 255, 255, 0.75);
}
.cal-empty-tip {
  text-align: center;
  color: #909399;
  font-size: 13px;
  padding: 32px 0;
}
.dur {
  font-weight: 600;
  color: #534ab7;
}
.ongoing {
  color: #409eff;
  font-size: 12px;
  padding: 2px 10px;
  border-radius: 999px;
  background: rgba(64, 158, 255, 0.12);
}
.pager {
  display: flex;
  justify-content: flex-end;
  margin-top: 16px;
}
</style>
