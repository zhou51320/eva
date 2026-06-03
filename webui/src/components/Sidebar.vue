<script setup lang="ts">
import { computed, ref } from 'vue'
import { store } from '../store'

const s = store.state
const search = ref('')

const filtered = computed(() => {
  const kw = search.value.trim()
  if (!kw) return s.sessions
  return s.sessions.filter(
    (item) =>
      (item.title || '').includes(kw) ||
      (item.messages[0]?.content || '').includes(kw),
  )
})

function preview(messages: { content: string }[]): string {
  return messages[messages.length - 1]?.content?.slice(0, 40) || '空会话'
}
</script>

<template>
  <aside class="sidebar">
    <div class="brand">
      <div class="brand__mark">E</div>
      <div class="brand__text">
        <div class="brand__title">EVA</div>
        <div class="brand__sub">ACP console</div>
      </div>
    </div>

    <button class="new-chat" @click="store.newChat()">
      <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 5v14M5 12h14" /></svg>
      新建对话
    </button>

    <div class="search">
      <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="7" /><path d="m21 21-4.3-4.3" /></svg>
      <input v-model="search" type="text" placeholder="搜索会话" />
    </div>

    <div class="sessions">
      <p v-if="filtered.length === 0" class="empty">没有会话</p>
      <button
        v-for="item in filtered"
        :key="item.id"
        class="session"
        :class="{ 'session--active': item.id === s.activeSessionId }"
        @click="store.selectSession(item.id)"
      >
        <div class="session__text">
          <div class="session__title">{{ item.title || '新对话' }}</div>
          <div class="session__preview">{{ preview(item.messages) }}</div>
        </div>
        <span class="session__del" title="删除" @click.stop="store.deleteSession(item.id)">
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 6h18M8 6V4h8v2M6 6l1 14h10l1-14" /></svg>
        </span>
      </button>
    </div>

    <div class="footer">
      <span>会话仅保存在本机浏览器</span>
    </div>
  </aside>
</template>

<style scoped>
.sidebar {
  width: 268px;
  flex: none;
  display: flex;
  flex-direction: column;
  background: var(--bg-elevated);
  border-right: 1px solid var(--border);
  padding: 14px 12px;
  gap: 12px;
}

.brand {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 4px 6px;
}
.brand__mark {
  width: 34px;
  height: 34px;
  border-radius: 9px;
  background: linear-gradient(135deg, var(--accent), #9a6cff);
  color: #fff;
  display: grid;
  place-items: center;
  font-weight: 700;
  font-size: 18px;
}
.brand__title {
  font-weight: 700;
  font-size: 15px;
  letter-spacing: 0.3px;
}
.brand__sub {
  font-size: 11.5px;
  color: var(--text-faint);
}

.new-chat {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  height: 40px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border-strong);
  background: var(--surface);
  color: var(--text);
  font-weight: 500;
  transition: background 0.15s, border-color 0.15s;
}
.new-chat:hover {
  background: var(--accent-soft);
  border-color: var(--accent);
}

.search {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 0 10px;
  height: 36px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border);
  background: var(--surface);
  color: var(--text-faint);
}
.search input {
  border: none;
  background: transparent;
  outline: none;
  width: 100%;
  color: var(--text);
}

.sessions {
  flex: 1;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 3px;
  margin: 0 -4px;
  padding: 0 4px;
}

.empty {
  color: var(--text-faint);
  font-size: 13px;
  text-align: center;
  margin-top: 24px;
}

.session {
  display: flex;
  align-items: center;
  gap: 8px;
  text-align: left;
  padding: 9px 10px;
  border-radius: var(--radius-sm);
  border: 1px solid transparent;
  background: transparent;
  color: var(--text);
  transition: background 0.12s;
}
.session:hover {
  background: var(--surface-hover);
}
.session--active {
  background: var(--accent-soft);
  border-color: var(--border);
}
.session__text {
  min-width: 0;
  flex: 1;
}
.session__title {
  font-size: 13.5px;
  font-weight: 500;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.session__preview {
  font-size: 12px;
  color: var(--text-faint);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  margin-top: 1px;
}
.session__del {
  flex: none;
  display: grid;
  place-items: center;
  width: 24px;
  height: 24px;
  border-radius: 6px;
  color: var(--text-faint);
  opacity: 0;
  transition: opacity 0.12s, color 0.12s, background 0.12s;
}
.session:hover .session__del {
  opacity: 1;
}
.session__del:hover {
  color: var(--danger);
  background: var(--surface);
}

.footer {
  font-size: 11.5px;
  color: var(--text-faint);
  text-align: center;
  padding-top: 4px;
}
</style>
