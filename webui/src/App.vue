<script setup lang="ts">
import { computed } from 'vue'
import { store } from './store'
import Sidebar from './components/Sidebar.vue'
import ChatThread from './components/ChatThread.vue'
import Composer from './components/Composer.vue'
import RuntimeDrawer from './components/RuntimeDrawer.vue'

const s = store.state

const sourceLabel = computed(() => {
  const b = s.backend
  if (!b) return '等待连接'
  const src = b.state_source || (b.direct_runtime ? 'direct_runtime' : b.bridge_available ? 'bridge' : 'legacy_acp')
  if (src === 'direct_runtime') return '直接运行层'
  if (src === 'bridge') return '主程序桥接'
  return 'ACP 降级'
})

const ready = computed(() => Boolean(s.backend?.ready))
const currentModel = computed(() => {
  const b = s.backend
  return (b?.current_model as string) || (b?.api_model as string) || '未装载'
})
const stateText = computed(() => (s.backend?.state as string) || (s.healthError ? '离线' : 'unknown'))
const dotClass = computed(() => {
  if (s.healthError) return 'dot--err'
  if (s.streaming) return 'dot--busy'
  return ready.value ? 'dot--ok' : 'dot--idle'
})
</script>

<template>
  <div class="app">
    <Sidebar />

    <main class="main">
      <header class="topbar">
        <div class="topbar__title">
          <span class="model" :title="currentModel">{{ currentModel }}</span>
          <span class="chip">
            <span class="dot" :class="dotClass"></span>
            {{ sourceLabel }} · {{ stateText }}
          </span>
        </div>
        <div class="topbar__actions">
          <button class="icon-btn" :title="s.theme === 'dark' ? '切换浅色' : '切换深色'" @click="store.toggleTheme()">
            <svg v-if="s.theme === 'dark'" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="4" /><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4" /></svg>
            <svg v-else width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z" /></svg>
          </button>
          <button class="btn btn--ghost" @click="store.setDrawer(true)">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="3" /><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09a1.65 1.65 0 0 0-1-1.51 1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09a1.65 1.65 0 0 0 1.51-1 1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z" /></svg>
            设置
          </button>
        </div>
      </header>

      <ChatThread />
      <Composer />
    </main>

    <RuntimeDrawer />
  </div>
</template>

<style scoped>
.app {
  display: flex;
  height: 100%;
  overflow: hidden;
}

.main {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-width: 0;
  background: var(--bg);
}

.topbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  padding: 12px 20px;
  border-bottom: 1px solid var(--border);
  background: var(--bg-elevated);
}

.topbar__title {
  display: flex;
  align-items: center;
  gap: 12px;
  min-width: 0;
}

.model {
  font-weight: 600;
  font-size: 14px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 38vw;
}

.chip {
  display: inline-flex;
  align-items: center;
  gap: 7px;
  font-size: 12.5px;
  color: var(--text-muted);
  background: var(--surface);
  border: 1px solid var(--border);
  padding: 4px 10px;
  border-radius: 999px;
  white-space: nowrap;
}

.dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  flex: none;
}
.dot--ok {
  background: var(--ok);
  box-shadow: 0 0 0 3px rgba(74, 222, 128, 0.18);
}
.dot--idle {
  background: var(--text-faint);
}
.dot--busy {
  background: var(--warn);
  box-shadow: 0 0 0 3px rgba(251, 191, 36, 0.18);
}
.dot--err {
  background: var(--danger);
  box-shadow: 0 0 0 3px rgba(255, 107, 107, 0.18);
}

.topbar__actions {
  display: flex;
  align-items: center;
  gap: 8px;
}

.icon-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 36px;
  height: 36px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border);
  background: var(--surface);
  color: var(--text-muted);
  transition: background 0.15s, color 0.15s;
}
.icon-btn:hover {
  background: var(--surface-hover);
  color: var(--text);
}

.btn {
  display: inline-flex;
  align-items: center;
  gap: 7px;
  height: 36px;
  padding: 0 14px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border);
  background: var(--surface);
  color: var(--text);
  font-size: 13.5px;
  font-weight: 500;
  transition: background 0.15s;
}
.btn:hover {
  background: var(--surface-hover);
}
</style>
