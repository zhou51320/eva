<script setup lang="ts">
import { computed, nextTick, ref, watch } from 'vue'
import { store } from '../store'
import MessageItem from './MessageItem.vue'

const s = store.state
const scroller = ref<HTMLElement | null>(null)

const session = computed(() => s.sessions.find((item) => item.id === s.activeSessionId) || null)
const messages = computed(() => session.value?.messages ?? [])

function scrollToBottom() {
  const el = scroller.value
  if (!el) return
  el.scrollTop = el.scrollHeight
}

// Follow the stream / new messages to the bottom.
watch(
  () => messages.value.map((m) => m.content).join('|') + messages.value.length,
  () => nextTick(scrollToBottom),
)
watch(() => s.activeSessionId, () => nextTick(scrollToBottom))
</script>

<template>
  <div ref="scroller" class="thread">
    <div class="thread__inner">
      <div v-if="messages.length === 0" class="welcome">
        <div class="welcome__mark">E</div>
        <h1>EVA 控制台</h1>
        <p>直接驱动 EVA 运行层进行对话。先覆盖文本链路,后续接入附件、知识库、MCP 与工具流。</p>
      </div>

      <MessageItem v-for="(msg, i) in messages" :key="i" :message="msg" />
    </div>
  </div>
</template>

<style scoped>
.thread {
  flex: 1;
  overflow-y: auto;
  scroll-behavior: smooth;
}
.thread__inner {
  max-width: 820px;
  margin: 0 auto;
  padding: 28px 24px 8px;
  display: flex;
  flex-direction: column;
  gap: 22px;
}

.welcome {
  text-align: center;
  margin: 14vh auto 0;
  max-width: 460px;
  color: var(--text-muted);
}
.welcome__mark {
  width: 56px;
  height: 56px;
  border-radius: 16px;
  margin: 0 auto 18px;
  display: grid;
  place-items: center;
  font-size: 28px;
  font-weight: 700;
  color: #fff;
  background: linear-gradient(135deg, var(--accent), #9a6cff);
}
.welcome h1 {
  color: var(--text);
  font-size: 22px;
  margin: 0 0 8px;
}
.welcome p {
  font-size: 14px;
  line-height: 1.7;
}
</style>
