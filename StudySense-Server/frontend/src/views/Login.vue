<template>
  <div class="login-wrap">
    <div class="login-card">
      <div class="login-badge">SS</div>
      <div class="login-title">StudySense</div>
      <div class="login-sub">智能书房环境调节系统</div>

      <el-form :model="form" @submit.prevent="onLogin" class="login-form">
        <el-form-item>
          <el-input v-model="form.username" placeholder="用户名" size="large" :prefix-icon="User" />
        </el-form-item>
        <el-form-item>
          <el-input
            v-model="form.password"
            type="password"
            placeholder="密码"
            size="large"
            :prefix-icon="Lock"
            show-password
            @keyup.enter="onLogin"
          />
        </el-form-item>
        <el-button type="primary" size="large" class="login-btn" :loading="loading" @click="onLogin">
          登 录
        </el-button>
      </el-form>
    </div>
  </div>
</template>

<script setup>
import { reactive, ref } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { User, Lock } from '@element-plus/icons-vue'
import api from '../api'
import { wsConnect } from '../ws'

const router = useRouter()
const loading = ref(false)
const form = reactive({ username: 'admin', password: '' })

async function onLogin() {
  if (!form.username || !form.password) {
    ElMessage.warning('请输入用户名和密码')
    return
  }
  loading.value = true
  try {
    const { data } = await api.post('/auth/login', {
      username: form.username,
      password: form.password,
    })
    localStorage.setItem('token', data.access_token)
    localStorage.setItem('username', form.username)
    ElMessage.success('登录成功')
    wsConnect()
    router.push('/dashboard')
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.login-wrap {
  height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
}
.login-card {
  width: 400px;
  background: #fff;
  border: 1px solid #e4e7ed;
  border-radius: 16px;
  padding: 48px 44px;
  box-shadow: 0 4px 24px rgba(31, 45, 61, 0.1);
  text-align: center;
}
.login-badge {
  width: 64px;
  height: 64px;
  margin: 0 auto 18px;
  border-radius: 18px;
  background: #409eff;
  color: #fff;
  font-size: 24px;
  font-weight: 700;
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 8px 20px rgba(64, 158, 255, 0.35);
}
.login-title {
  font-size: 26px;
  font-weight: 700;
  color: #1f2d3d;
}
.login-sub {
  margin: 10px 0 36px;
  color: #909399;
  font-size: 14px;
  letter-spacing: 2px;
}
.login-form {
  text-align: left;
}
.login-form :deep(.el-form-item) {
  margin-bottom: 24px;
}
.login-btn {
  width: 100%;
  margin-top: 4px;
  letter-spacing: 8px;
}
</style>
