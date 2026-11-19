import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import AutoImport from 'unplugin-auto-import/vite'
import Components from 'unplugin-vue-components/vite'
import { ElementPlusResolver } from 'unplugin-vue-components/resolvers'

export default defineConfig({
  plugins: [
    vue(),
    // Element Plus 按需引入：模板里用到的组件自动注册并携带样式（替代全量 app.use）
    Components({ resolvers: [ElementPlusResolver()] }),
    AutoImport({ resolvers: [ElementPlusResolver()] }),
  ],
  server: {
    // 监听所有网卡：默认只绑 ::1，本机代理/手机局域网访问会连不上
    host: true,
    port: 5173,
    proxy: {
      // 与 backend/.env 的 APP_PORT 保持一致（默认 8800）
      '/api': 'http://127.0.0.1:8800',
      '/ws': { target: 'ws://127.0.0.1:8800', ws: true },
    },
  },
})
