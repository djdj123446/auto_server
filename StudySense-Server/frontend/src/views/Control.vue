<template>
  <div>
    <!-- 手动控制：图形化设备卡片 -->
    <div class="page-card">
      <div class="card-title">设备控制</div>
      <el-row :gutter="24">
        <!-- LED 照明 -->
        <el-col :xs="24" :sm="12" :md="8">
          <div class="ctrl-box" :class="{ active: ledOn }" @click="ledOn !== null && send('led', ledOn ? 0 : 1)">
            <!-- 自绘灯泡：玻璃壳 + 灯丝 + 螺纹底座 -->
            <div class="bulb" :class="{ on: ledOn }">
              <div class="bulb-glass"><i class="bulb-filament"></i></div>
              <div class="bulb-neck"></div>
              <div class="bulb-base"><i></i><i></i><i></i></div>
            </div>
            <div class="ctrl-label">LED 照明</div>
            <div class="ctrl-state" :class="{ lit: ledOn }">{{ ledOn ? '已开启' : '已关闭' }}</div>
          </div>
        </el-col>

        <!-- 风扇 -->
        <el-col :xs="24" :sm="12" :md="8">
          <div class="ctrl-box" :class="{ active: fanVal > 0 }">
            <!-- 自绘风扇：护罩圈 + 三片扇叶 + 中心毂 -->
            <div class="fan" :class="{ spinning: fanVal > 0, s1: fanVal === 1, s2: fanVal === 2, s3: fanVal === 3 }">
              <div class="fan-ring"></div>
              <div class="fan-rotor">
                <i class="blade b1"></i><i class="blade b2"></i><i class="blade b3"></i>
                <i class="fan-hub"></i>
              </div>
            </div>
            <div class="ctrl-label">风扇档位</div>
            <div class="fan-levels">
              <button
                v-for="lv in 4"
                :key="lv"
                class="level-btn"
                :class="{ sel: fanVal === lv - 1 }"
                :disabled="sending.fan"
                @click="send('fan', lv - 1)"
              >
                {{ ['关', '一', '二', '三'][lv - 1] }}
              </button>
            </div>
          </div>
        </el-col>

        <!-- 工作模式 -->
        <el-col :xs="24" :sm="12" :md="8">
          <div class="ctrl-box mode-box">
            <div class="ctrl-label">工作模式</div>
            <div class="mode-cards">
              <div
                v-for="m in MODES"
                :key="m.value"
                class="mode-card"
                :class="{ sel: modeVal === m.value, [`t${m.value}`]: true }"
                @click="onModeClick(m.value)"
              >
                <el-icon :size="30"><component :is="m.icon" /></el-icon>
                <span class="mode-name">{{ m.name }}</span>
                <span class="mode-desc">{{ m.desc }}</span>
              </div>
            </div>
          </div>
        </el-col>
      </el-row>
      <el-alert
        v-if="modeVal === 1"
        class="mode-tip"
        type="info"
        :closable="false"
        title="当前为自动模式：设备按阈值自动调节，手动指令可能被设备立即覆盖"
      />
      <!-- 专注横幅：与仪表盘一致，显示实时计时（设备每 10 秒上报基准，本地每秒插值走字） -->
      <div v-if="modeVal === 2" class="focus-banner mode-tip" :class="{ paused: focusState.paused }">
        <div class="focus-pulse"><el-icon :size="30"><Timer /></el-icon></div>
        <div class="focus-info">
          <div class="focus-title">{{ focusState.paused ? '专注已暂停' : '专注进行中' }}</div>
          <div class="focus-sub">
            {{ focusState.paused ? '等待确认是否结束本次专注' : '满休息提醒间隔设备会提醒休息 · 点击任意模式卡片可结束' }}
          </div>
        </div>
        <div class="focus-time">{{ focusText }}</div>
      </div>
    </div>

    <!-- 阈值设置：滑块 + 预设，无需手动输入 -->
    <div class="page-card">
      <div class="card-title">阈值设置</div>
      <el-row :gutter="28">
        <el-col :xs="24" :sm="12">
          <div class="th-card">
            <div class="th-head">
              <div class="th-icon t-warm"><el-icon :size="20"><Sunny /></el-icon></div>
              <div class="th-info">
                <div class="th-name">温度上限</div>
                <div class="th-sub">高于该温度自动开风扇</div>
              </div>
              <div class="th-value">{{ thForm.temp_max.toFixed(1) }}<i>°C</i></div>
            </div>
            <el-slider v-model="thForm.temp_max" :min="20" :max="35" :step="0.5" :marks="{ 24: '24', 28: '28', 32: '32' }" />
            <div class="th-presets">
              <button v-for="v in [24, 26, 28, 30]" :key="v" class="chip" :class="{ sel: thForm.temp_max === v }" @click="thForm.temp_max = v">{{ v }} °C</button>
            </div>
          </div>
        </el-col>
        <el-col :xs="24" :sm="12">
          <div class="th-card">
            <div class="th-head">
              <div class="th-icon t-sun"><el-icon :size="20"><Sunrise /></el-icon></div>
              <div class="th-info">
                <div class="th-name">光照下限</div>
                <div class="th-sub">有人且低于该亮度自动开灯</div>
              </div>
              <div class="th-value">{{ Math.round(thForm.light_min) }}<i>lx</i></div>
            </div>
            <el-slider v-model="thForm.light_min" :min="0" :max="1000" :step="10" :marks="{ 150: '150', 300: '300', 500: '500' }" />
            <div class="th-presets">
              <button v-for="v in [100, 150, 300, 500]" :key="v" class="chip" :class="{ sel: thForm.light_min === v }" @click="thForm.light_min = v">{{ v }} lx</button>
            </div>
          </div>
        </el-col>
      </el-row>

      <!-- 休息提醒间隔：滑块 + 预设 -->
      <div class="th-card rest-card">
        <div class="th-head">
          <div class="th-icon t-rest"><el-icon :size="20"><AlarmClock /></el-icon></div>
          <div class="th-info">
            <div class="th-name">专注休息提醒间隔</div>
            <div class="th-sub">专注模式每满该时长弹框提醒休息</div>
          </div>
          <div class="th-value">{{ thForm.rest_min }}<i>分钟</i></div>
        </div>
        <el-slider v-model="thForm.rest_min" :min="1" :max="120" :step="1" :marks="{ 15: '15', 30: '30', 60: '60', 120: '120' }" />
        <div class="th-presets">
          <button
            v-for="v in REST_OPTIONS"
            :key="v"
            class="chip"
            :class="{ sel: thForm.rest_min === v }"
            @click="thForm.rest_min = v"
          >
            {{ v >= 60 ? (v / 60) + ' 小时' : v + ' 分钟' }}
          </button>
        </div>
      </div>

      <div class="th-actions">
        <el-button type="primary" size="large" round :loading="sending.threshold" @click="saveThreshold">
          <el-icon style="margin-right: 6px"><Promotion /></el-icon>保存并下发
        </el-button>
        <el-button size="large" round @click="loadThreshold">重置</el-button>
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed, onMounted, onUnmounted, reactive, ref } from 'vue'
import { ElMessage } from 'element-plus'
import { Sunny, Sunrise, Refresh, AlarmClock, Promotion, Timer, MagicStick, Pointer } from '@element-plus/icons-vue'
import api from '../api'
import { wsOn } from '../ws'
import { focusState, applyFocusState, focusCmd, formatElapsed } from '../focus'

const ACTION_NAMES = { led: 'LED', fan: '风扇', mode: '模式', threshold: '阈值' }

const MODES = [
  { value: 0, name: '手动', desc: '完全手动控制', icon: Pointer },
  { value: 1, name: '自动', desc: '按阈值调节', icon: MagicStick },
  { value: 2, name: '专注', desc: '计时提醒休息', icon: Timer },
]

const REST_OPTIONS = [1, 15, 30, 60]

const ledOn = ref(false)
const fanVal = ref(0)
const modeVal = ref(1)
const sending = reactive({ fan: false, threshold: false })

// 阈值表单（与固件字段一一对应，无需手动输入 key）
const thForm = reactive({
  temp_max: 28.0,
  light_min: 300,
  rest_min: 1,
})

// 乐观更新：点击瞬间先切 UI，不等设备上报走完整条链路
// pendingUntil 之前忽略设备旧状态回推，避免刚点的状态被上报延迟"闪回"
const pending = { led: 0, fan: 0, mode: 0 }

function applyLocal(action, value) {
  if (action === 'led') ledOn.value = value === 1
  else if (action === 'fan') fanVal.value = value
  else if (action === 'mode') modeVal.value = value
  pending[action] = Date.now() + 3000
}

async function send(action, value) {
  // 记录当前值，失败时回滚，保证 UI 与设备真实状态一致
  const prev = { led: ledOn.value, fan: fanVal.value, mode: modeVal.value }
  applyLocal(action, value)
  if (action === 'fan') sending.fan = true
  try {
    await api.post(`/control/${action}`, { value })
    ElMessage.success(`${ACTION_NAMES[action]}: 已下发`)
  } catch {
    // 下发失败（含设备离线被后端拒绝）：回滚 UI，等待设备状态上报纠正
    ledOn.value = prev.led
    fanVal.value = prev.fan
    modeVal.value = prev.mode
    pending[action] = 0
  } finally {
    if (action === 'fan') sending.fan = false
  }
}

// 专注模式下点击任意模式卡片：先问是否结束专注（与 ESP32 按 K3/K4 一致），
// 由设备弹出弹框并暂停计时，网页跟随设备上报显示同一个弹框。
// 非专注模式：保持原有直接切模式的行为
function onModeClick(value) {
  if (modeVal.value !== 2) {
    send('mode', value)
    return
  }
  // 弹框已打开时忽略点击，等用户先在弹框里做选择
  if (focusState.paused || focusState.rest) return

  // 记下点击的目标模式：选择"结束计时"后切过去
  // （点"专注"卡片本身没有可切换目标，按设备端 K4 结束行为回手动）
  focusState.targetMode = value === 2 ? 0 : value
  focusCmd.pause()
}

function contentToForm(content) {
  const c = content || {}
  if (c.temp_max != null) thForm.temp_max = Number(c.temp_max)
  if (c.light_min != null) thForm.light_min = Number(c.light_min)
  if (c.rest_min != null) thForm.rest_min = Number(c.rest_min)
}

async function loadThreshold() {
  try {
    const { data } = await api.get('/threshold')
    contentToForm(data.content)
  } catch { /* 忽略 */ }
}

async function saveThreshold() {
  sending.threshold = true
  try {
    await api.put('/threshold', { content: { ...thForm } })
    ElMessage.success('阈值已保存并下发设备')
  } finally {
    sending.threshold = false
  }
}

// WebSocket：设备端按键改阈值 / 状态同步
const onThreshold = (data) => {
  contentToForm(data)
}

// 专注计时：读取共享的 focusState.elapsed（AppLayout 统一插值走字，与设备 OLED 一致）
const focusText = computed(() => formatElapsed(focusState.elapsed))

const onState = (data) => {
  // 刚点过的字段在 pending 窗口内不回写，防止设备上报延迟导致 UI 闪回旧值
  if (data.led != null && Date.now() >= pending.led) ledOn.value = Number(data.led) === 1
  if (data.fan != null && Date.now() >= pending.fan) fanVal.value = Number(data.fan)
  if (data.mode != null && Date.now() >= pending.mode) modeVal.value = Number(data.mode)
  applyFocusState(data)
}

// 设备离线：专注等同正常结束 —— 本地切回手动，专注横幅随之隐藏（数据已由云端落库）
const onDeviceStatus = (data) => {
  if (!data.online && modeVal.value === 2) modeVal.value = 0
}

const offs = []

onMounted(() => {
  offs.push(wsOn('threshold', onThreshold))
  offs.push(wsOn('state', onState))
  offs.push(wsOn('status', onDeviceStatus))
  loadThreshold()
  // 页面初始用最新状态初始化控件（设备离线时返回数据库里最后一次的数据）
  api.get('/data/latest').then(({ data }) => {
    if (data.data) onState(data.data)
  }).catch(() => {})
})

onUnmounted(() => {
  offs.forEach((off) => off())
})
</script>

<style scoped>
/* ===================== 控制卡片 ===================== */
.ctrl-box {
  position: relative;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 18px;
  padding: 30px 16px 26px;
  background: #f8f9fb;
  border: 1px solid #e4e7ed;
  border-radius: 18px;
  min-height: 240px;
  height: 100%;
  justify-content: center;
  cursor: pointer;
  transition: transform 0.25s, border-color 0.25s;
}
/* hover 阴影放在伪元素上做 opacity 过渡（box-shadow 变化会触发重绘，opacity 走合成器） */
.ctrl-box::after {
  content: '';
  position: absolute;
  inset: 0;
  border-radius: inherit;
  box-shadow: 0 14px 36px rgba(31, 45, 61, 0.13);
  opacity: 0;
  transition: opacity 0.25s;
  pointer-events: none;
}
.ctrl-box:hover {
  transform: translateY(-4px);
}
.ctrl-box:hover::after {
  opacity: 1;
}
.ctrl-box.active {
  border-color: rgba(230, 162, 60, 0.55);
  box-shadow: 0 10px 30px rgba(230, 162, 60, 0.18);
}
.ctrl-label {
  font-size: 15px;
  font-weight: 600;
  color: #1f2d3d;
}
.ctrl-state {
  font-size: 13px;
  color: #909399;
  padding: 3px 14px;
  border-radius: 999px;
  background: #f5f7fa;
  transition: color 0.3s, background-color 0.3s;
}
.ctrl-state.lit {
  color: #b45309;
  background: #fdf6ec;
}

/* ===================== 自绘灯泡（玻璃壳 + 灯丝 + 螺纹底座） ===================== */
.bulb {
  position: relative;
  width: 72px;
  height: 104px;
  transition: transform 0.18s;
}
.bulb.on {
  transform: scale(1.04);
}
.bulb-glass {
  position: absolute;
  top: 0;
  left: 50%;
  transform: translateX(-50%);
  width: 55px;
  height: 56px;
  border-radius: 50% 50% 44% 44%;
  background: linear-gradient(150deg, rgba(255, 255, 255, 0.9), rgba(228, 232, 240, 0.55));
  border: 2px solid rgba(31, 45, 61, 0.14);
  overflow: hidden;
  /* 全部属性在 0.16s 内完成：光晕是大半径 box-shadow，拖太长会有明显滞后感 */
  transition: background 0.16s, border-color 0.16s, box-shadow 0.16s;
}
.bulb.on .bulb-glass {
  background: linear-gradient(150deg, #fff7d6, #ffe08a);
  border-color: rgba(230, 162, 60, 0.6);
  box-shadow: 0 0 16px 5px rgba(255, 197, 61, 0.5);
}
/* 灯丝：两道竖弧 + 中间桥 */
.bulb-filament {
  position: absolute;
  left: 50%;
  bottom: 7px;
  transform: translateX(-50%);
  width: 22px;
  height: 14px;
  border: 2px solid rgba(31, 45, 61, 0.35);
  border-bottom: none;
  border-radius: 11px 11px 0 0;
  transition: border-color 0.16s, box-shadow 0.16s;
}
.bulb.on .bulb-filament {
  border-color: #ff9f1a;
  box-shadow: 0 0 10px rgba(255, 159, 26, 0.9);
  background: linear-gradient(to top, rgba(255, 159, 26, 0.35), transparent);
}
/* 玻璃壳高光 */
.bulb-glass::before {
  content: '';
  position: absolute;
  top: 8px;
  left: 10px;
  width: 12px;
  height: 17px;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.85);
  transform: rotate(-22deg);
}
.bulb-neck {
  position: absolute;
  top: 56px;
  left: 50%;
  transform: translateX(-50%);
  width: 26px;
  height: 7px;
  background: rgba(31, 45, 61, 0.22);
  clip-path: polygon(12% 0, 88% 0, 100% 100%, 0 100%);
}
.bulb-base {
  position: absolute;
  top: 63px;
  left: 50%;
  transform: translateX(-50%);
  width: 29px;
  height: 27px;
  border-radius: 0 0 8px 8px;
  background: linear-gradient(180deg, #c9ced8, #9aa1ad);
  display: flex;
  flex-direction: column;
  justify-content: space-evenly;
  padding: 0 4px;
  box-shadow: inset 0 -3px 4px rgba(0, 0, 0, 0.18);
}
.bulb-base i {
  display: block;
  height: 3px;
  border-radius: 2px;
  background: rgba(255, 255, 255, 0.55);
  box-shadow: 0 1px 1px rgba(0, 0, 0, 0.25);
}
/* 底座触点 */
.bulb-base::after {
  content: '';
  position: absolute;
  bottom: -5px;
  left: 50%;
  transform: translateX(-50%);
  width: 12px;
  height: 5px;
  border-radius: 0 0 6px 6px;
  background: #7d848f;
}

/* ===================== 自绘风扇（护罩 + 三叶 + 毂） ===================== */
.fan {
  position: relative;
  width: 82px;
  height: 82px;
  margin-top: 10px;
}
.fan-ring {
  position: absolute;
  inset: 0;
  border-radius: 50%;
  border: 3px solid rgba(31, 45, 61, 0.16);
  background:
    radial-gradient(circle, transparent 62%, rgba(31, 45, 61, 0.05) 63%, transparent 66%),
    radial-gradient(circle, transparent 40%, rgba(31, 45, 61, 0.05) 41%, transparent 44%);
}
.fan-rotor {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
}
.fan.spinning .fan-rotor {
  /* 固定在合成层旋转，避免 clip-path 每帧重栅格化 */
  will-change: transform;
  animation: fan-spin 1s linear infinite;
}
.fan.spinning.s1 .fan-rotor { animation-duration: 1.4s; }
.fan.spinning.s2 .fan-rotor { animation-duration: 0.8s; }
/* 三档不能过快：三叶片对称间隔 120°，转速过陡会触发频闪"倒转"错觉
   （屏幕每帧间叶片转过角度超过相邻叶片间隔）。0.6s 是临界以下同时仍快于二档 */
.fan.spinning.s3 .fan-rotor { animation-duration: 0.6s; }
@keyframes fan-spin {
  to { transform: rotate(360deg); }
}
.blade {
  position: absolute;
  width: 30px;
  height: 34px;
  bottom: 50%;
  left: 50%;
  margin-left: -15px;
  transform-origin: 50% 100%;
  background: linear-gradient(180deg, #64b5f6, #409eff);
  border-radius: 50% 50% 50% 50% / 60% 60% 40% 40%;
  /* 用椭圆裁剪收窄成清晰扇叶轮廓（否则退化成圆鼓鼓的饼状）。
     clip-path 在父级旋转时每帧重栅格化，但仅 80px 小元素、仅旋转时，代价可接受 */
  clip-path: ellipse(46% 52% at 50% 42%);
  opacity: 0.9;
}
.blade.b1 { transform: rotate(0deg); }
.blade.b2 { transform: rotate(120deg); }
.blade.b3 { transform: rotate(240deg); }
.fan.spinning .blade {
  background: linear-gradient(180deg, #90caf9, #42a5f5);
}
.fan-hub {
  position: absolute;
  width: 19px;
  height: 19px;
  border-radius: 50%;
  background: radial-gradient(circle at 35% 35%, #ffffff, #b7c1cd 70%);
  border: 2px solid rgba(31, 45, 61, 0.2);
  box-shadow: 0 2px 6px rgba(31, 45, 61, 0.25);
}

/* 档位按钮 */
.fan-levels {
  display: flex;
  flex-wrap: wrap;
  justify-content: center;
  gap: 10px;
}
.level-btn {
  width: 44px;
  height: 40px;
  border: 1.5px solid rgba(31, 45, 61, 0.16);
  border-radius: 12px;
  background: #fff;
  color: #606266;
  font-size: 14px;
  font-weight: 600;
  cursor: pointer;
  transition: border-color 0.2s, color 0.2s, background 0.2s, box-shadow 0.2s;
}
.level-btn:hover {
  border-color: #409eff;
  color: #409eff;
}
.level-btn.sel {
  background: #409eff;
  border-color: #409eff;
  color: #fff;
  box-shadow: 0 4px 12px rgba(64, 158, 255, 0.3);
}

/* ===================== 模式卡片 ===================== */
.mode-box {
  cursor: default;
  justify-content: center;
}
.mode-cards {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
  width: 100%;
  justify-content: center;
}
.mode-card {
  flex: 1 1 92px;
  max-width: 110px;
  min-width: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 7px;
  padding: 16px 8px 13px;
  border-radius: 14px;
  border: 1.5px solid rgba(31, 45, 61, 0.12);
  background: #f5f7fa;
  color: #909399;
  cursor: pointer;
  transition: transform 0.25s, color 0.25s, background 0.25s, border-color 0.25s, box-shadow 0.25s;
}
.mode-card:hover {
  transform: translateY(-3px);
  color: #606266;
}
.mode-card.sel {
  color: #fff;
  border-color: transparent;
  transform: translateY(-3px);
}
.mode-card.sel.t1 { background: #67c23a; box-shadow: 0 4px 14px rgba(103, 194, 58, 0.35); }
.mode-card.sel.t0 { background: #909399; box-shadow: 0 4px 14px rgba(96, 98, 102, 0.35); }
.mode-card.sel.t2 { background: #f56c6c; box-shadow: 0 4px 14px rgba(245, 108, 108, 0.35); }
.mode-name {
  font-size: 15px;
  font-weight: 700;
}
.mode-desc {
  font-size: 11px;
  opacity: 0.75;
  white-space: nowrap;
}

.mode-tip {
  margin-top: 22px;
}

/* ===================== 专注横幅（与仪表盘一致） ===================== */
.focus-banner {
  display: flex;
  align-items: center;
  gap: 20px;
  padding: 20px 30px;
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
/* 用伪元素缩放代替 box-shadow 呼吸（box-shadow 动画每帧重绘） */
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
.focus-info {
  flex: 1;
  min-width: 0;
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

/* ===================== 阈值卡片（滑块 + 预设芯片） ===================== */
.th-card {
  background: #f8f9fb;
  border: 1px solid #e4e7ed;
  border-radius: 16px;
  padding: 20px 22px 16px;
  margin-bottom: 22px;
}
.th-head {
  display: flex;
  align-items: center;
  gap: 14px;
  margin-bottom: 20px;
}
.th-icon {
  width: 44px;
  height: 44px;
  border-radius: 13px;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #fff;
  flex-shrink: 0;
}
.th-icon.t-warm { background: #f78c3c; }
.th-icon.t-cool { background: #409eff; }
.th-icon.t-sun { background: #f7b731; }
.th-icon.t-rest { background: #8e44ad; }
.th-info {
  flex: 1;
  min-width: 0;
}
.th-name {
  font-size: 15px;
  font-weight: 600;
  color: #1f2d3d;
}
.th-sub {
  font-size: 12px;
  color: #909399;
  margin-top: 3px;
}
.th-value {
  font-size: 26px;
  font-weight: 700;
  color: #1f2d3d;
  white-space: nowrap;
}
.th-value i {
  font-style: normal;
  font-size: 12px;
  font-weight: 400;
  color: #909399;
  margin-left: 5px;
}

/* 滑块美化：加粗轨道 + 渐变 */
.th-card :deep(.el-slider__runway) {
  height: 10px;
  border-radius: 6px;
  background: rgba(31, 45, 61, 0.1);
}
.th-card :deep(.el-slider__bar) {
  height: 10px;
  border-radius: 6px;
  background: #409eff;
}
.th-card :deep(.el-slider__button) {
  width: 22px;
  height: 22px;
  border: 3px solid #409eff;
  box-shadow: 0 3px 10px rgba(64, 158, 255, 0.4);
}
.th-card :deep(.el-slider__stop) {
  width: 4px;
  height: 4px;
}
.th-card :deep(.el-slider__marks-text) {
  font-size: 11px;
  color: #b0b3b8;
  margin-top: 12px;
}
.th-card :deep(.el-slider) {
  padding: 0 2px 26px;
}

/* 预设芯片 */
.th-presets {
  display: flex;
  gap: 10px;
  flex-wrap: wrap;
  margin-top: 4px;
}
.chip {
  padding: 6px 16px;
  border-radius: 999px;
  border: 1.5px solid rgba(31, 45, 61, 0.14);
  background: #fff;
  color: #606266;
  font-size: 13px;
  cursor: pointer;
  transition: border-color 0.2s, color 0.2s, background 0.2s;
}
.chip:hover {
  border-color: #409eff;
  color: #409eff;
}
.chip.sel {
  background: rgba(64, 158, 255, 0.14);
  border-color: #409eff;
  color: #337ecc;
  font-weight: 600;
}
.rest-card {
  margin-bottom: 6px;
}
.th-actions {
  display: flex;
  gap: 14px;
  margin-top: 8px;
}
</style>
