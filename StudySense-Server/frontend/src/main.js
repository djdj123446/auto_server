import { createApp } from 'vue'
import App from './App.vue'
import router from './router'
import './style.css'
// 按需样式补充：ElMessage / ElNotification 是函数式调用（unplugin 只处理模板组件），样式需手动引入
import 'element-plus/es/components/message/style/css'
import 'element-plus/es/components/notification/style/css'

const app = createApp(App)
app.use(router)
app.mount('#app')
