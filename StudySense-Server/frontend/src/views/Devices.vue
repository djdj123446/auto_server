<template>
  <div>
    <el-tabs v-model="tab" class="dev-tabs">
      <!-- ================ 设备总览 ================ -->
      <el-tab-pane label="设备总览" name="devices">
        <el-row :gutter="24">
          <el-col :span="12" v-for="d in devices" :key="d.device_id">
            <div class="page-card dev-card" :class="{ offline: !d.online }">
              <div class="dev-top">
                <div class="dev-avatar">
                  <el-icon :size="26"><Cpu /></el-icon>
                </div>
                <div class="dev-title">
                  <div class="dev-name">{{ d.name || d.device_id }}</div>
                  <div class="dev-id">{{ d.device_id }}</div>
                </div>
                <div class="online-pill" :class="{ off: !d.online }">
                  <i class="pulse-dot"></i>
                  {{ d.online ? '在线' : '离线' }}
                </div>
              </div>

              <div class="dev-meta">
                <span><label>型号</label>{{ d.model }}</span>
                <span><label>位置</label>{{ d.location }}</span>
                <span><label>固件</label>{{ d.firmware }}</span>
              </div>

              <div class="dev-metrics" v-if="d.latest">
                <div class="dm"><i>{{ d.latest.temperature ?? '--' }}</i><label>°C 温度</label></div>
                <div class="dm"><i>{{ d.latest.humidity ?? '--' }}</i><label>% 湿度</label></div>
                <div class="dm"><i>{{ d.latest.light ?? '--' }}</i><label>lx 光照</label></div>
                <div class="dm">
                  <i>{{ d.latest.people ? '有人' : '无人' }}</i><label>人体感应</label>
                </div>
              </div>
              <div class="dev-metrics empty" v-else>
                <span>暂无上报数据</span>
              </div>

              <div class="dev-foot">
                <span>最近上报：{{ fmtFull(d.last_seen_at) }}</span>
                <el-button type="primary" plain size="small" @click="openDetail(d)">
                  查看详情
                </el-button>
              </div>
            </div>
          </el-col>
        </el-row>

        <!-- 设备详情抽屉 -->
        <el-drawer v-model="drawer" :title="detail?.name || detail?.device_id" size="420px">
          <template v-if="detail">
            <div class="dt-section">
              <div class="dt-title">基本信息</div>
              <div class="dt-row"><label>设备 ID</label><span>{{ detail.device_id }}</span></div>
              <div class="dt-row"><label>型号</label><span>{{ detail.model }}</span></div>
              <div class="dt-row"><label>固件版本</label><span>{{ detail.firmware }}</span></div>
              <div class="dt-row"><label>安装位置</label><span>{{ detail.location }}</span></div>
              <div class="dt-row"><label>注册时间</label><span>{{ fmtFull(detail.created_at) }}</span></div>
              <div class="dt-row"><label>最近上报</label><span>{{ fmtFull(detail.last_seen) }}</span></div>
            </div>

            <div class="dt-section">
              <div class="dt-title">运行状态（24 小时）</div>
              <div class="dt-stats">
                <div class="dts">
                  <i>{{ detail.samples_24h }}</i>
                  <label>上报样本</label>
                </div>
                <div class="dts">
                  <i>{{ detail.online_rate_24h }}<em>%</em></i>
                  <label>在线率</label>
                </div>
              </div>
            </div>

            <div class="dt-section" v-if="detail.latest">
              <div class="dt-title">实时数据</div>
              <div class="dt-row"><label>温度</label><span>{{ detail.latest.temperature }} °C</span></div>
              <div class="dt-row"><label>湿度</label><span>{{ detail.latest.humidity }} %</span></div>
              <div class="dt-row"><label>光照</label><span>{{ detail.latest.light }} lx</span></div>
              <div class="dt-row"><label>人体</label><span>{{ detail.latest.people ? '有人' : '无人' }}</span></div>
              <div class="dt-row"><label>LED</label><span>{{ detail.latest.led ? '开启' : '关闭' }}</span></div>
              <div class="dt-row"><label>风扇</label><span>{{ FAN_NAMES[detail.latest.fan] ?? detail.latest.fan }}</span></div>
            </div>

            <div class="dt-section" v-if="detail.threshold">
              <div class="dt-title">当前阈值</div>
              <div class="dt-row" v-for="(v, k) in detail.threshold" :key="k">
                <label>{{ TH_NAMES[k] || k }}</label><span>{{ v }}</span>
              </div>
            </div>
          </template>
        </el-drawer>
      </el-tab-pane>

      <!-- ================ 操作审计 ================ -->
      <el-tab-pane label="操作审计" name="audit">
        <div class="page-card">
          <div class="filter-bar">
            <el-select v-model="filter.source" placeholder="来源" clearable style="width: 140px">
              <el-option label="网页手动" value="web" />
              <el-option label="自动化规则" value="rule" />
            </el-select>
            <el-select v-model="filter.cmd" placeholder="指令" clearable style="width: 120px">
              <el-option v-for="c in ['led', 'fan', 'mode', 'focus_pause', 'focus_continue', 'focus_end']" :key="c" :label="c" :value="c" />
            </el-select>
            <el-select v-model="filter.result" placeholder="结果" clearable style="width: 120px">
              <el-option label="执行成功" value="ok" />
              <el-option label="执行失败" value="fail" />
              <el-option label="等待回执" value="pending" />
            </el-select>
            <el-button type="primary" plain @click="loadAudit">查询</el-button>
          </div>

          <el-table :data="auditRows" stripe>
            <el-table-column label="时间" width="170">
              <template #default="{ row }">{{ fmtFull(row.ts) }}</template>
            </el-table-column>
            <el-table-column label="来源" width="110">
              <template #default="{ row }">
                <el-tag :type="row.source === 'rule' ? 'warning' : 'primary'" effect="plain" round>
                  {{ row.source === 'rule' ? '自动化规则' : '网页手动' }}
                </el-tag>
              </template>
            </el-table-column>
            <el-table-column prop="operator" label="操作者" min-width="130" show-overflow-tooltip />
            <el-table-column prop="cmd" label="指令" width="130" />
            <el-table-column label="参数" width="80">
              <template #default="{ row }">{{ row.value ?? '--' }}</template>
            </el-table-column>
            <el-table-column label="结果" width="100">
              <template #default="{ row }">
                <span class="result" :class="'r-' + row.result">{{ resultName(row.result) }}</span>
              </template>
            </el-table-column>
            <el-table-column prop="detail" label="说明" min-width="160" show-overflow-tooltip />
          </el-table>

          <div class="pager">
            <el-pagination
              layout="total, prev, pager, next"
              :total="auditTotal"
              :page-size="auditPageSize"
              :current-page="auditPage"
              @current-change="onAuditPage"
            />
          </div>
        </div>
      </el-tab-pane>
    </el-tabs>
  </div>
</template>

<script setup>
import { onMounted, onUnmounted, reactive, ref } from 'vue'
import { Cpu } from '@element-plus/icons-vue'
import api from '../api'
import { FAN_NAMES } from '../format'
import { wsOn } from '../ws'

const TH_NAMES = { temp_max: '温度上限 (°C)', temp_min: '温度下限 (°C)', light_min: '光照下限 (lx)' }

function fmtFull(ts) {
  if (!ts) return '--'
  const d = new Date(ts)
  const p = (n) => String(n).padStart(2, '0')
  return `${d.getMonth() + 1}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`
}

// ---------- 设备 ----------
const tab = ref('devices')
const devices = ref([])
const drawer = ref(false)
const detail = ref(null)

async function loadDevices() {
  const { data } = await api.get('/devices')
  devices.value = data.devices
}

async function openDetail(d) {
  const { data } = await api.get(`/devices/${d.device_id}`)
  detail.value = data
  drawer.value = true
}

// ---------- 审计 ----------
const auditRows = ref([])
const auditTotal = ref(0)
const auditPage = ref(1)
const auditPageSize = 15
const filter = reactive({ source: null, cmd: null, result: null })

const resultName = (r) => ({ ok: '执行成功', fail: '执行失败', pending: '等待回执' }[r] || r)

async function loadAudit() {
  const { data } = await api.get('/audit', {
    params: { page: auditPage.value, page_size: auditPageSize, ...filter },
  })
  auditRows.value = data.rows
  auditTotal.value = data.total
}

function onAuditPage(p) {
  auditPage.value = p
  loadAudit()
}

// ack 回执实时刷新审计结果
let offAck = null
onMounted(() => {
  loadDevices()
  loadAudit()
  offAck = wsOn('ack', () => loadAudit())
})
onUnmounted(() => offAck && offAck())
</script>

<style scoped>
.dev-tabs {
  margin-bottom: 8px;
}
.dev-tabs :deep(.el-tabs__item) {
  font-size: 15px;
}
.dev-card {
  transition: opacity 0.2s;
}
.dev-card.offline {
  opacity: 0.75;
}
.dev-top {
  display: flex;
  align-items: center;
  gap: 14px;
}
.dev-avatar {
  width: 52px;
  height: 52px;
  border-radius: 14px;
  background: linear-gradient(135deg, #409eff, #534ab7);
  color: #fff;
  display: flex;
  align-items: center;
  justify-content: center;
}
.dev-title {
  flex: 1;
}
.dev-name {
  font-size: 17px;
  font-weight: 700;
  color: #1f2d3d;
}
.dev-id {
  font-size: 12px;
  color: #909399;
  font-family: Consolas, monospace;
  margin-top: 2px;
}
.dev-meta {
  display: flex;
  gap: 26px;
  margin: 18px 0 14px;
  font-size: 13px;
  color: #606266;
}
.dev-meta label {
  color: #909399;
  margin-right: 6px;
}
.dev-metrics {
  display: flex;
  justify-content: space-between;
  background: #f7f9fc;
  border-radius: 12px;
  padding: 14px 20px;
}
.dev-metrics.empty {
  justify-content: center;
  color: #909399;
  font-size: 13px;
}
.dm {
  text-align: center;
}
.dm i {
  display: block;
  font-style: normal;
  font-size: 20px;
  font-weight: 700;
  color: #1f2d3d;
}
.dm label {
  font-size: 12px;
  color: #909399;
}
.dev-foot {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-top: 16px;
  font-size: 13px;
  color: #909399;
}

/* 在线胶囊（与布局页一致） */
.online-pill {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 5px 14px;
  border-radius: 999px;
  font-size: 13px;
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
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #22c55e;
}
.online-pill.off .pulse-dot {
  background: #ef4444;
}

/* 详情抽屉 */
.dt-section {
  margin-bottom: 26px;
}
.dt-title {
  font-size: 14px;
  font-weight: 600;
  color: #1f2d3d;
  margin-bottom: 12px;
  padding-left: 10px;
  border-left: 3px solid #409eff;
}
.dt-row {
  display: flex;
  justify-content: space-between;
  padding: 8px 0;
  font-size: 13px;
  border-bottom: 1px dashed #f0f2f5;
}
.dt-row label {
  color: #909399;
}
.dt-row span {
  color: #1f2d3d;
  font-weight: 500;
}
.dt-stats {
  display: flex;
  justify-content: space-around;
  background: #f7f9fc;
  border-radius: 12px;
  padding: 16px 0;
}
.dts {
  text-align: center;
}
.dts i {
  display: block;
  font-style: normal;
  font-size: 22px;
  font-weight: 700;
  color: #1f2d3d;
}
.dts em {
  font-style: normal;
  font-size: 13px;
}
.dts label {
  font-size: 12px;
  color: #909399;
}

/* 审计 */
.filter-bar {
  display: flex;
  gap: 12px;
  align-items: center;
  margin-bottom: 18px;
}
.result {
  font-size: 13px;
  font-weight: 600;
}
.r-ok { color: #0f6e56; }
.r-fail { color: #e24b4a; }
.r-pending { color: #ba7517; }
.pager {
  display: flex;
  justify-content: flex-end;
  margin-top: 16px;
}
</style>
