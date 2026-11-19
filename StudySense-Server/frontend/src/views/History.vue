<template>
  <div>
    <!-- 统计卡片 -->
    <el-row :gutter="24" v-if="stats" class="card-row">
      <el-col :span="6">
        <div class="stat-card page-card">
          <div class="stat-head">
            <div class="stat-icon t-warm"><el-icon :size="20"><Odometer /></el-icon></div>
            <div class="stat-title">温度 (°C)</div>
          </div>
          <div class="stat-line">最低: {{ stats.temperature?.min ?? '--' }}</div>
          <div class="stat-line">最高: {{ stats.temperature?.max ?? '--' }}</div>
          <div class="stat-line">平均: {{ stats.temperature?.avg ?? '--' }}</div>
        </div>
      </el-col>
      <el-col :span="6">
        <div class="stat-card page-card">
          <div class="stat-head">
            <div class="stat-icon t-cool"><el-icon :size="20"><Cloudy /></el-icon></div>
            <div class="stat-title">湿度 (%)</div>
          </div>
          <div class="stat-line">最低: {{ stats.humidity?.min ?? '--' }}</div>
          <div class="stat-line">最高: {{ stats.humidity?.max ?? '--' }}</div>
          <div class="stat-line">平均: {{ stats.humidity?.avg ?? '--' }}</div>
        </div>
      </el-col>
      <el-col :span="6">
        <div class="stat-card page-card">
          <div class="stat-head">
            <div class="stat-icon t-sun"><el-icon :size="20"><Sunrise /></el-icon></div>
            <div class="stat-title">光照 (lx)</div>
          </div>
          <div class="stat-line">最低: {{ stats.light?.min ?? '--' }}</div>
          <div class="stat-line">最高: {{ stats.light?.max ?? '--' }}</div>
          <div class="stat-line">平均: {{ stats.light?.avg ?? '--' }}</div>
        </div>
      </el-col>
      <el-col :span="6">
        <div class="stat-card page-card">
          <div class="stat-head">
            <div class="stat-icon t-people"><el-icon :size="20"><User /></el-icon></div>
            <div class="stat-title">有人比例</div>
          </div>
          <div class="people-ring" :style="{ background: `conic-gradient(#8e44ad ${stats.people_pct ?? 0}%, rgba(31,45,61,0.07) 0)` }">
            <div class="people-hole">
              <span class="people-num">{{ stats.people_pct ?? 0 }}<i>%</i></span>
            </div>
          </div>
          <div class="stat-line">样本数: {{ stats.samples ?? 0 }} 条</div>
        </div>
      </el-col>
    </el-row>

    <!-- 温湿度曲线 -->
    <div class="page-card">
      <div class="card-title">温湿度曲线</div>
      <div ref="tempChartRef" class="chart"></div>
    </div>
  </div>
</template>

<script setup>
import { onMounted, onUnmounted, ref } from 'vue'
// 按需引入 echarts（全量引入约 1MB，按需后仅打包用到的部分）
import * as echarts from 'echarts/core'
import { LineChart } from 'echarts/charts'
import { GridComponent, TooltipComponent, LegendComponent } from 'echarts/components'
import { CanvasRenderer } from 'echarts/renderers'

echarts.use([LineChart, GridComponent, TooltipComponent, LegendComponent, CanvasRenderer])

import { Odometer, Cloudy, Sunrise, User } from '@element-plus/icons-vue'
import api from '../api'

const stats = ref(null)

const tempChartRef = ref(null)
let tempChart = null

async function query() {
  try {
    // 固定查询最近 24 小时
    const end = new Date()
    const start = new Date()
    start.setHours(start.getHours() - 24)
    const [h, s] = await Promise.all([
      api.get('/data/history', { params: { start: fmt(start), end: fmt(end), points: 200 } }),
      api.get('/data/stats', { params: { start: fmt(start), end: fmt(end) } }),
    ])
    stats.value = s.data

    const rows = h.data.rows || []
    const ts = rows.map((r) => r.ts.slice(5, 16).replace('T', ' '))

    tempChart.setOption({
      xAxis: { data: ts },
      series: [
        { data: rows.map((r) => r.temperature) },
        { data: rows.map((r) => r.humidity) },
      ],
    })
  } catch (e) {
    console.error('加载历史数据失败', e)
  }
}

function initCharts() {
  tempChart = echarts.init(tempChartRef.value)
  tempChart.setOption({
    tooltip: { trigger: 'axis' },
    legend: { data: ['温度 (°C)', '湿度 (%)'], top: 0 },
    grid: { left: 56, right: 56, top: 40, bottom: 32 },
    xAxis: { type: 'category', data: [], boundaryGap: false },
    yAxis: [
      { type: 'value', name: '温度', scale: true },
      { type: 'value', name: '湿度', scale: true, splitLine: { show: false } },
    ],
    series: [
      { name: '温度 (°C)', type: 'line', smooth: true, showSymbol: false, data: [], animation: false, itemStyle: { color: '#f78c3c' }, areaStyle: { opacity: 0.08 } },
      { name: '湿度 (%)', type: 'line', smooth: true, showSymbol: false, yAxisIndex: 1, data: [], animation: false, itemStyle: { color: '#409eff' }, areaStyle: { opacity: 0.08 } },
    ],
  })
}

const onResize = () => {
  tempChart && tempChart.resize()
}

onMounted(() => {
  initCharts()
  window.addEventListener('resize', onResize)
  // 加载最近 24 小时数据
  query()
})

function fmt(d) {
  const p = (n) => String(n).padStart(2, '0')
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}T${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`
}

onUnmounted(() => {
  window.removeEventListener('resize', onResize)
  tempChart && tempChart.dispose()
})
</script>

<style scoped>
/* ===================== 统计卡片 ===================== */
.card-row {
  /* 间距改由行控制：卡片自身撑满列高，不再依赖 page-card 的 margin-bottom */
  margin-bottom: 24px;
}
.stat-card {
  text-align: center;
  padding: 22px 16px;
  height: 100%;
  margin-bottom: 0;
  display: flex;
  flex-direction: column;
  justify-content: center;
}
.stat-head {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 10px;
  margin-bottom: 14px;
}
.stat-icon {
  width: 38px;
  height: 38px;
  border-radius: 11px;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #fff;
  flex-shrink: 0;
}
.stat-icon.t-warm { background: #f78c3c; }
.stat-icon.t-cool { background: #409eff; }
.stat-icon.t-sun { background: #f7b731; }
.stat-icon.t-people { background: #8e44ad; }
.stat-title {
  font-weight: 600;
  font-size: 15px;
}
.stat-line {
  color: #606266;
  font-size: 14px;
  line-height: 1.9;
}
.people-ring {
  width: 96px;
  height: 96px;
  margin: 4px auto 10px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
}
.people-hole {
  width: 74px;
  height: 74px;
  border-radius: 50%;
  background: #fff;
  display: flex;
  align-items: center;
  justify-content: center;
}
.people-num {
  font-size: 22px;
  font-weight: 700;
  color: #1f2d3d;
}
.people-num i {
  font-style: normal;
  font-size: 12px;
  color: #909399;
  margin-left: 2px;
}
.chart {
  height: 340px;
}
</style>
