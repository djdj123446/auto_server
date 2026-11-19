import { createRouter, createWebHistory } from 'vue-router'

const routes = [
  { path: '/login', name: 'login', component: () => import('./views/Login.vue') },
  {
    path: '/',
    component: () => import('./layout/AppLayout.vue'),
    children: [
      { path: '', redirect: '/dashboard' },
      { path: 'dashboard', name: 'dashboard', component: () => import('./views/Dashboard.vue') },
      { path: 'study', name: 'study', component: () => import('./views/Study.vue') },
      { path: 'history', name: 'history', component: () => import('./views/History.vue') },
      { path: 'control', name: 'control', component: () => import('./views/Control.vue') },
      { path: 'devices', name: 'devices', component: () => import('./views/Devices.vue') },
      { path: 'rules', name: 'rules', component: () => import('./views/Rules.vue') },
    ],
  },
]

const router = createRouter({ history: createWebHistory(), routes })

router.beforeEach((to) => {
  const token = localStorage.getItem('token')
  if (to.path !== '/login' && !token) return '/login'
})

export default router
