<template>
  <div>
    <div class="page-card">
      <div class="rule-head">
        <div>
          <div class="card-title" style="margin-bottom: 6px">自动化规则</div>
          <div class="rule-desc">
            条件满足时（从满足到不满足的跳变沿触发，带冷却时间）由云端自动下发控制指令，执行记录可在设备审计中追溯。
          </div>
        </div>
        <el-button type="primary" @click="openCreate">
          <el-icon style="margin-right: 6px"><Plus /></el-icon>新建规则
        </el-button>
      </div>

      <el-table :data="rules" stripe>
        <el-table-column prop="name" label="规则名称" min-width="150" />
        <el-table-column label="触发条件" min-width="200">
          <template #default="{ row }">
            <span class="cond">
              当 <b>{{ metricName(row.trigger_metric) }}</b>
              {{ row.trigger_op === 'gt' ? '>' : '<' }}
              <b>{{ row.trigger_value }}{{ metricUnit(row.trigger_metric) }}</b>
            </span>
          </template>
        </el-table-column>
        <el-table-column label="执行动作" min-width="160">
          <template #default="{ row }">
            <span class="cond">
              {{ cmdName(row.action_cmd) }} → <b>{{ actionName(row) }}</b>
            </span>
          </template>
        </el-table-column>
        <el-table-column label="冷却" width="80">
          <template #default="{ row }">{{ row.cooldown_sec }}s</template>
        </el-table-column>
        <el-table-column label="状态" width="90">
          <template #default="{ row }">
            <el-switch :model-value="row.enabled" @change="onToggle(row)" />
          </template>
        </el-table-column>
        <el-table-column label="触发次数" width="90" prop="fire_count" />
        <el-table-column label="最近触发" width="160">
          <template #default="{ row }">{{ fmtFull(row.last_fired_at) }}</template>
        </el-table-column>
        <el-table-column label="操作" width="140">
          <template #default="{ row }">
            <el-button size="small" plain @click="openEdit(row)">编辑</el-button>
            <el-button size="small" type="danger" plain @click="onDelete(row)">删除</el-button>
          </template>
        </el-table-column>
      </el-table>

      <el-empty v-if="rules.length === 0" description="还没有自动化规则，点击右上角新建" />
    </div>

    <!-- 新建 / 编辑弹框 -->
    <el-dialog v-model="dialog" :title="form.id ? '编辑规则' : '新建规则'" width="520px">
      <el-form :model="form" label-width="90px">
        <el-form-item label="规则名称">
          <el-input v-model="form.name" placeholder="如：高温自动开风扇" maxlength="64" />
        </el-form-item>
        <el-form-item label="触发条件">
          <div class="cond-row">
            <span>当</span>
            <el-select v-model="form.trigger_metric" style="width: 110px">
              <el-option v-for="m in METRICS" :key="m.value" :label="m.label" :value="m.value" />
            </el-select>
            <el-select v-model="form.trigger_op" style="width: 76px">
              <el-option label=">" value="gt" />
              <el-option label="<" value="lt" />
            </el-select>
            <el-input-number v-model="form.trigger_value" :step="metricStep" style="width: 130px" />
            <span>{{ metricUnit(form.trigger_metric) }}</span>
          </div>
        </el-form-item>
        <el-form-item label="执行动作">
          <div class="cond-row">
            <el-select v-model="form.action_cmd" style="width: 110px" @change="onCmdChange">
              <el-option label="LED" value="led" />
              <el-option label="风扇" value="fan" />
              <el-option label="模式" value="mode" />
            </el-select>
            <span>设为</span>
            <el-select v-model="form.action_value" style="width: 130px">
              <el-option v-for="o in actionOptions" :key="o.value" :label="o.label" :value="o.value" />
            </el-select>
          </div>
        </el-form-item>
        <el-form-item label="冷却时间">
          <el-input-number v-model="form.cooldown_sec" :min="10" :max="3600" :step="10" style="width: 160px" />
          <span class="unit">秒（两次触发之间的最小间隔）</span>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="dialog = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="onSave">保存</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { computed, onMounted, onUnmounted, reactive, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Plus } from '@element-plus/icons-vue'
import api from '../api'
import { wsOn } from '../ws'

const METRICS = [
  { value: 'temperature', label: '温度', unit: '°C', step: 0.5 },
  { value: 'humidity', label: '湿度', unit: '%', step: 1 },
  { value: 'light', label: '光照', unit: 'lx', step: 10 },
  { value: 'people', label: '人体感应', unit: '', step: 1 },
]
const CMDS = { led: 'LED', fan: '风扇', mode: '模式' }

const metricName = (v) => METRICS.find((m) => m.value === v)?.label || v
const metricUnit = (v) => METRICS.find((m) => m.value === v)?.unit || ''
const metricStep = computed(() => METRICS.find((m) => m.value === form.trigger_metric)?.step ?? 1)
const cmdName = (v) => CMDS[v] || v

const actionOptions = computed(() => {
  if (form.action_cmd === 'led') return [{ value: 0, label: '关闭' }, { value: 1, label: '开启' }]
  if (form.action_cmd === 'fan') return [
    { value: 0, label: '关闭' }, { value: 1, label: '一档' },
    { value: 2, label: '二档' }, { value: 3, label: '三档' },
  ]
  return [{ value: 0, label: '手动' }, { value: 1, label: '自动' }]
})

const actionName = (row) => {
  const o = actionOptionsFor(row.action_cmd).find((x) => x.value === row.action_value)
  return o?.label ?? row.action_value
}

function actionOptionsFor(cmd) {
  if (cmd === 'led') return [{ value: 0, label: '关闭' }, { value: 1, label: '开启' }]
  if (cmd === 'fan') return [
    { value: 0, label: '关闭' }, { value: 1, label: '一档' },
    { value: 2, label: '二档' }, { value: 3, label: '三档' },
  ]
  return [{ value: 0, label: '手动' }, { value: 1, label: '自动' }]
}

function fmtFull(ts) {
  if (!ts) return '从未触发'
  const d = new Date(ts)
  const p = (n) => String(n).padStart(2, '0')
  return `${d.getMonth() + 1}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`
}

// ---------- 列表 ----------
const rules = ref([])

async function load() {
  const { data } = await api.get('/rules')
  rules.value = data.rules
}

// ---------- 表单 ----------
const dialog = ref(false)
const saving = ref(false)
const form = reactive({
  id: null, name: '',
  trigger_metric: 'temperature', trigger_op: 'gt', trigger_value: 28,
  action_cmd: 'fan', action_value: 1, cooldown_sec: 120,
})

function onCmdChange() {
  form.action_value = actionOptions.value[0]?.value ?? 0
}

function openCreate() {
  Object.assign(form, {
    id: null, name: '',
    trigger_metric: 'temperature', trigger_op: 'gt', trigger_value: 28,
    action_cmd: 'fan', action_value: 1, cooldown_sec: 120,
  })
  dialog.value = true
}

function openEdit(row) {
  Object.assign(form, JSON.parse(JSON.stringify(row)))
  dialog.value = true
}

async function onSave() {
  if (!form.name.trim()) {
    ElMessage.warning('请填写规则名称')
    return
  }
  saving.value = true
  try {
    if (form.id) {
      await api.put(`/rules/${form.id}`, form)
    } else {
      await api.post('/rules', form)
    }
    ElMessage.success('规则已保存')
    dialog.value = false
    load()
  } finally {
    saving.value = false
  }
}

async function onToggle(row) {
  await api.post(`/rules/${row.id}/toggle`)
  load()
}

async function onDelete(row) {
  await ElMessageBox.confirm(`确定删除规则「${row.name}」？`, '删除确认', { type: 'warning' })
  await api.delete(`/rules/${row.id}`)
  ElMessage.success('已删除')
  load()
}

// 规则触发实时刷新
let offFired = null
onMounted(() => {
  load()
  offFired = wsOn('rule_fired', () => load())
})
onUnmounted(() => offFired && offFired())
</script>

<style scoped>
.rule-head {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  margin-bottom: 18px;
}
.rule-desc {
  font-size: 13px;
  color: #909399;
  max-width: 640px;
}
.cond {
  font-size: 13px;
  color: #606266;
}
.cond b {
  color: #1f2d3d;
}
.cond-row {
  display: flex;
  align-items: center;
  gap: 10px;
  color: #606266;
  font-size: 13px;
}
.unit {
  margin-left: 10px;
  font-size: 12px;
  color: #909399;
}
</style>
