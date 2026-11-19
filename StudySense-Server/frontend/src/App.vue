<template>
  <!-- 中文语言包（原全量引入时通过 app.use 配置，按需引入后改用 ConfigProvider） -->
  <el-config-provider :locale="zhCn">
    <router-view />
  </el-config-provider>
</template>

<script setup>
import zhCn from 'element-plus/es/locale/lang/zh-cn'
</script>
