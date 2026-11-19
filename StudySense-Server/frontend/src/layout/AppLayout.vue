<template>
  <el-container class="layout">
    <el-aside width="220px" class="aside">
      <div class="logo">
        <span class="logo-badge">SS</span>
        <span class="logo-text">StudySense</span>
      </div>
      <el-menu :default-active="active" router class="menu">
        <div class="menu-group">监控中心</div>
        <el-menu-item index="/dashboard">
          <el-icon><Odometer /></el-icon>
          <span>实时仪表盘</span>
        </el-menu-item>
        <el-menu-item index="/study">
          <el-icon><DataAnalysis /></el-icon>
          <span>学习报告</span>
        </el-menu-item>
        <el-menu-item index="/history">
          <el-icon><TrendCharts /></el-icon>
          <span>历史数据</span>
        </el-menu-item>

        <div class="menu-group">设备管理</div>
        <el-menu-item index="/devices">
          <el-icon><Cpu /></el-icon>
          <span>设备总览与审计</span>
        </el-menu-item>
        <el-menu-item index="/rules">
          <el-icon><MagicStick /></el-icon>
          <span>自动化规则</span>
        </el-menu-item>

        <div class="menu-group">控制中心</div>
        <el-menu-item index="/control">
          <el-icon><Setting /></el-icon>
          <span>设备控制</span>
        </el-menu-item>
      </el-menu>
    </el-aside>

    <el-container>
      <el-header class="header">
        <div class="header-title">智能书房环境调节系统</div>
        <div class="header-right">
          <div class="online-pill" :class="{ off: !online }">
            <i class="pulse-dot"></i>
            {{ online ? '设备在线' : '设备离线' }}
          </div>
          <el-dropdown @command="onCommand">
            <span class="user-chip">
              <el-icon><User /></el-icon>
              {{ username }}
              <el-icon><ArrowDown /></el-icon>
            </span>
            <template #dropdown>
              <el-dropdown-menu>
                <el-dropdown-item command="logout">退出登录</el-dropdown-item>
              </el-dropdown-menu>
            </template>
          </el-dropdown>
        </div>
      </el-header>

      <el-main class="main">
        <router-view />
      </el-main>
    </el-container>
  </el-container>

  <!-- ==================================================
       专注弹框：与 ESP32 OLED 完全同步
       弹框开关由设备上报的 focus_paused / focus_rest 驱动，
       网页只负责显示与回传选择，两端不会出现不一致
       ================================================== -->
  <transition name="fd">
    <div v-if="focusDialog" class="fd-mask">
      <div class="fd-card">
        <div class="fd-icon" :class="focusDialog">
          <el-icon :size="32"><component :is="focusDialog === 'rest' ? AlarmClock : Timer" /></el-icon>
        </div>
        <div class="fd-title">{{ focusDialog === 'rest' ? '休息提醒' : '专注中' }}</div>
        <div class="fd-time">{{ formatElapsed(focusState.elapsed) }}</div>
        <div class="fd-desc">
          {{ focusDialog === 'rest'
            ? `已专注 ${elapsedMinutes(focusState.elapsed)} 分钟，注意休息！`
            : '是否结束本次专注？' }}
        </div>
        <div class="fd-tip">
          {{ focusDialog === 'rest' ? '不操作将自动继续计时' : '计时已暂停' }}
        </div>
        <div class="fd-actions">
          <el-button size="large" round :disabled="acting" @click="onFocusContinue">继续专注</el-button>
          <el-button type="danger" size="large" round :disabled="acting" @click="onFocusEnd">结束计时</el-button>
        </div>
      </div>
    </div>
  </transition>
</template>

<script setup>
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage, ElNotification } from 'element-plus'
import {
  Odometer, TrendCharts, Setting, User, ArrowDown, AlarmClock, Timer,
  DataAnalysis, Cpu, MagicStick,
} from '@element-plus/icons-vue'
import { wsConnect, wsDisconnect, wsOn } from '../ws'
import { focusState, applyFocusState, startFocusTicker, stopFocusTicker, focusDeviceOffline, formatElapsed, elapsedMinutes, focusCmd } from '../focus'

const route = useRoute()
const router = useRouter()
const active = computed(() => route.path)
const username = localStorage.getItem('username') || 'admin'
const online = ref(false)

// 设备离线时不再显示弹框，避免设备掉线导致弹框永久卡住
const focusDialog = computed(() => {
  if (!online.value) return null
  if (focusState.rest) return 'rest'
  if (focusState.paused) return 'end'
  return null
})

const acting = ref(false)

const onStatus = (data) => {
  online.value = !!data.online
  // 设备离线：专注等同正常结束 —— 清计时退出专注显示，并提示本次时长（数据已由云端落库）
  if (!data.online && focusState.mode === 2) {
    const spent = formatElapsed(focusState.elapsed)
    focusDeviceOffline()
    ElMessage.success(`设备已离线，专注已结束，本次时长 ${spent}`)
  }
}

const onState = (data) => {
  online.value = true
  applyFocusState(data)
}

const onSnapshot = (snap) => {
  online.value = !!snap.online
  // 专注字段在 snapshot.data（设备最后一次上报）里，不能传整个快照包装层
  applyFocusState(snap.data && snap.data.data ? snap.data.data : null)
}

async function onFocusContinue() {
  if (acting.value) return
  acting.value = true
  try {
    await focusCmd.continue()
  } catch {
    /* 失败提示由 api 拦截器统一处理，弹框保持打开等待重试 */
  } finally {
    acting.value = false
  }
}

async function onFocusEnd() {
  if (acting.value) return
  acting.value = true
  // 先取本次时长：设备执行后立即上报 focus=0，等接口返回再取就变 0 了
  const spent = formatElapsed(focusState.elapsed)
  try {
    await focusCmd.end(focusState.targetMode ?? 0)
    ElMessage.success(`专注结束，本次学习时长 ${spent}`)
  } catch {
    /* 同上 */
  } finally {
    acting.value = false
  }
}

const offs = []

onMounted(() => {
  wsConnect()
  startFocusTicker()
  offs.push(wsOn('status', onStatus))
  offs.push(wsOn('state', onState))
  offs.push(wsOn('snapshot', onSnapshot))
  offs.push(wsOn('rule_fired', (r) => {
    ElNotification({
      title: '自动化规则触发',
      message: `规则「${r.name}」已执行：${r.cmd} = ${r.value}`,
      type: 'info',
      duration: 4000,
    })
  }))
})
onUnmounted(() => {
  offs.forEach((off) => off())
  stopFocusTicker()
  wsDisconnect()
})

function onCommand(cmd) {
  if (cmd === 'logout') {
    localStorage.removeItem('token')
    localStorage.removeItem('username')
    wsDisconnect()
    ElMessage.success('已退出登录')
    router.push('/login')
  }
}
</script>

<style scoped>
.layout {
  height: 100vh;
}
.aside {
  background: #fff;
  border-right: 1px solid #e4e7ed;
  display: flex;
  flex-direction: column;
}
.logo {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 24px 20px;
}
.logo-badge {
  width: 40px;
  height: 40px;
  border-radius: 12px;
  background: #409eff;
  color: #fff;
  font-weight: 700;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 15px;
  box-shadow: 0 4px 14px rgba(64, 158, 255, 0.35);
}
.logo-text {
  font-size: 19px;
  font-weight: 700;
  color: #1f2d3d;
}
.menu {
  border-right: none;
  padding: 8px;
}
.menu-group {
  font-size: 12px;
  color: #b0b3b8;
  padding: 14px 14px 6px;
  letter-spacing: 1px;
}
.menu :deep(.el-menu-item) {
  border-radius: 10px;
  margin-bottom: 6px;
  height: 50px;
}
.header {
  background: #fff;
  display: flex;
  align-items: center;
  justify-content: space-between;
  border-bottom: 1px solid #e4e7ed;
}
.header-title {
  font-size: 17px;
  font-weight: 600;
}
.header-right {
  display: flex;
  align-items: center;
  gap: 20px;
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
/* 用伪元素缩放代替 box-shadow 动画（box-shadow 动画每帧触发重绘，伪元素 transform 走合成器） */
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
.user-chip {
  display: flex;
  align-items: center;
  gap: 6px;
  cursor: pointer;
  color: #606266;
  font-size: 14px;
  padding: 6px 14px;
  border-radius: 999px;
  background: #f5f7fa;
  border: 1px solid #e4e7ed;
  transition: background 0.2s;
}
.user-chip:hover {
  background: #ecf5ff;
}
.main {
  padding: 24px;
  overflow-y: auto;
}

/* ===================== 专注弹框（与 OLED 同步） ===================== */
.fd-mask {
  position: fixed;
  inset: 0;
  z-index: 3000;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(31, 45, 61, 0.4);
}
.fd-card {
  width: 380px;
  padding: 36px 34px 30px;
  border-radius: 22px;
  background: #fff;
  border: 1px solid #e4e7ed;
  box-shadow: 0 24px 60px rgba(31, 45, 61, 0.22);
  display: flex;
  flex-direction: column;
  align-items: center;
  text-align: center;
}
.fd-icon {
  width: 68px;
  height: 68px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #fff;
  position: relative;
  margin-bottom: 20px;
}
.fd-icon.end {
  background: #f56c6c;
  box-shadow: 0 10px 24px rgba(245, 108, 108, 0.35);
}
.fd-icon.rest {
  background: #8e44ad;
  box-shadow: 0 10px 24px rgba(142, 68, 173, 0.35);
}
/* 呼吸圈：伪元素 scale 动画走合成器，不用 box-shadow 动画（每帧重绘） */
.fd-icon::after {
  content: '';
  position: absolute;
  inset: 0;
  border-radius: 50%;
  border: 2px solid currentColor;
  color: rgba(245, 108, 108, 0.55);
  animation: fd-breathe 1.8s ease-out infinite;
}
.fd-icon.rest::after {
  color: rgba(142, 68, 173, 0.55);
}
@keyframes fd-breathe {
  0% { transform: scale(1); opacity: 0.9; }
  100% { transform: scale(1.55); opacity: 0; }
}
.fd-title {
  font-size: 19px;
  font-weight: 700;
  color: #1f2d3d;
}
.fd-time {
  font-size: 40px;
  font-weight: 700;
  color: #1f2d3d;
  font-variant-numeric: tabular-nums;
  letter-spacing: 2px;
  margin: 14px 0 10px;
}
.fd-desc {
  font-size: 14px;
  color: #606266;
}
.fd-tip {
  font-size: 12px;
  color: #b0b3b8;
  margin-top: 8px;
}
.fd-actions {
  display: flex;
  gap: 14px;
  margin-top: 26px;
}
.fd-actions :deep(.el-button) {
  min-width: 118px;
}

.fd-enter-active, .fd-leave-active {
  transition: opacity 0.22s;
}
.fd-enter-from, .fd-leave-to {
  opacity: 0;
}
.fd-enter-active .fd-card, .fd-leave-active .fd-card {
  transition: transform 0.22s;
}
.fd-enter-from .fd-card, .fd-leave-to .fd-card {
  transform: scale(0.94);
}
</style>
