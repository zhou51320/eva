<script setup lang="ts">
import { ref, watch } from 'vue'
import type { ChatMessage } from '../types'
import { renderMarkdown } from '../markdown'

const props = defineProps<{ message: ChatMessage }>()
const emit = defineEmits<{ retry: [] }>()

const reasoningOpen = ref(true)
watch(
  () => ({
    pending: Boolean(props.message.pending),
    hasReasoning: Boolean(props.message.reasoning),
    hasContent: Boolean(props.message.content),
  }),
  (value, previous) => {
    if (!value.hasReasoning) return
    if (value.pending && !value.hasContent) reasoningOpen.value = true
    if (value.hasContent && (!previous?.hasContent || !previous?.hasReasoning)) reasoningOpen.value = false
    if (previous?.pending && !value.pending) reasoningOpen.value = false
  },
  { immediate: true },
)

function onReasoningToggle(event: Event) {
  reasoningOpen.value = (event.currentTarget as HTMLDetailsElement).open
}

function copyText(text: string, btn?: HTMLElement) {
  const done = () => {
    if (!btn) return
    const prev = btn.textContent
    btn.textContent = '已复制'
    btn.classList.add('copied')
    setTimeout(() => {
      btn.textContent = prev
      btn.classList.remove('copied')
    }, 1200)
  }
  if (navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(text).then(done).catch(() => fallbackCopy(text, done))
  } else {
    fallbackCopy(text, done)
  }
}

function fallbackCopy(text: string, done: () => void) {
  const ta = document.createElement('textarea')
  ta.value = text
  ta.style.position = 'fixed'
  ta.style.opacity = '0'
  document.body.appendChild(ta)
  ta.select()
  try {
    document.execCommand('copy')
  } catch {
    /* ignore */
  }
  document.body.removeChild(ta)
  done()
}

// Delegate clicks on per-codeblock copy buttons inside rendered markdown.
function onBodyClick(event: MouseEvent) {
  const target = event.target as HTMLElement
  const btn = target.closest('.codeblock__copy') as HTMLElement | null
  if (!btn) return
  const code = btn.closest('.codeblock')?.querySelector('code')
  if (code) copyText(code.textContent || '', btn)
}

function formatTokens(value?: number): string {
  if (!Number.isFinite(value)) return '0 tokens'
  return `${Math.round(value as number).toLocaleString()} tokens`
}

function formatDuration(ms?: number): string {
  if (!Number.isFinite(ms)) return '0s'
  const seconds = Math.max(0, (ms as number) / 1000)
  if (seconds < 10) return `${seconds.toFixed(1)}s`
  return `${Math.round(seconds)}s`
}

function formatSpeed(value?: number): string {
  if (!Number.isFinite(value) || (value as number) <= 0) return '0.00 t/s'
  return `${(value as number).toFixed(2)} t/s`
}
</script>

<template>
  <article class="msg" :class="`msg--${message.role}`">
    <!-- user: right-aligned bubble -->
    <template v-if="message.role === 'user'">
      <div class="user-col">
        <div v-if="message.images && message.images.length" class="user-imgs">
          <img v-for="(img, i) in message.images" :key="i" :src="img" alt="attachment" />
        </div>
        <div v-if="message.content" class="bubble">{{ message.content }}</div>
      </div>
    </template>

    <!-- assistant / system -->
    <template v-else>
      <div class="avatar">{{ message.role === 'system' ? 'S' : 'E' }}</div>
      <div class="content">
        <details v-if="message.reasoning" class="reasoning" :open="reasoningOpen" @toggle="onReasoningToggle">
          <summary>
            <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 18h6M10 22h4M12 2a7 7 0 0 0-4 12.7V17h8v-2.3A7 7 0 0 0 12 2z" /></svg>
            思考过程
          </summary>
          <div class="reasoning__body">{{ message.reasoning }}</div>
        </details>

        <div v-if="message.toolSteps && message.toolSteps.length" class="tools">
          <span v-for="(t, i) in message.toolSteps" :key="i" class="tool-chip">
            <svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M14.7 6.3a4 4 0 0 0-5.4 5.4l-6 6a1.5 1.5 0 0 0 2 2l6-6a4 4 0 0 0 5.4-5.4l-2.3 2.3-2-2 2.3-2.3z" /></svg>
            {{ t }}
          </span>
        </div>

        <div
          v-if="message.content"
          class="md body"
          :class="{ 'body--err': message.error }"
          v-html="renderMarkdown(message.content)"
          @click="onBodyClick"
        ></div>
        <div v-else-if="message.pending" class="body">
          <span class="cursor"></span>
        </div>

        <div class="footer">
          <span class="meta">{{ message.meta }}</span>
          <span v-if="message.content && !message.pending" class="actions">
            <button
              class="msg-action"
              title="复制"
              @click="(e) => copyText(message.content, (e.currentTarget as HTMLElement))"
            >
              复制
            </button>
            <button v-if="message.role === 'assistant'" class="msg-action" title="重答" @click="emit('retry')">重答</button>
          </span>
          <span v-if="message.role === 'assistant' && message.stats && !message.pending" class="stats">
            <span>{{ formatTokens(message.stats.tokens) }}</span>
            <span>{{ formatDuration(message.stats.elapsedMs) }}</span>
            <span>{{ formatSpeed(message.stats.tokensPerSecond) }}</span>
          </span>
        </div>
      </div>
    </template>
  </article>
</template>

<style scoped>
.msg {
  display: flex;
  gap: 12px;
}
.msg--user {
  justify-content: flex-end;
}

.user-col {
  display: flex;
  flex-direction: column;
  align-items: flex-end;
  gap: 8px;
  max-width: 80%;
}
.user-imgs {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  justify-content: flex-end;
}
.user-imgs img {
  max-width: 180px;
  max-height: 180px;
  border-radius: 12px;
  border: 1px solid var(--border);
}

.bubble {
  background: var(--user-bubble);
  color: var(--text);
  padding: 11px 15px;
  border-radius: 16px 16px 4px 16px;
  white-space: pre-wrap;
  word-wrap: break-word;
  line-height: 1.55;
}

.avatar {
  flex: none;
  width: 30px;
  height: 30px;
  border-radius: 9px;
  display: grid;
  place-items: center;
  font-weight: 700;
  font-size: 13px;
  color: #fff;
  background: linear-gradient(135deg, var(--accent), #9a6cff);
  margin-top: 2px;
}

.content {
  min-width: 0;
  flex: 1;
}

.body {
  color: var(--text);
}
.body--err {
  color: var(--danger);
}

.tools {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-bottom: 10px;
}
.tool-chip {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  font-size: 12px;
  padding: 3px 9px;
  border-radius: 999px;
  border: 1px solid var(--border);
  background: var(--surface);
  color: var(--text-muted);
}

.reasoning {
  margin-bottom: 12px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: var(--surface);
  overflow: hidden;
}
.reasoning summary {
  display: flex;
  align-items: center;
  gap: 7px;
  cursor: pointer;
  padding: 8px 12px;
  font-size: 12.5px;
  color: var(--text-muted);
  user-select: none;
}
.reasoning summary::-webkit-details-marker {
  display: none;
}
.reasoning__body {
  padding: 0 14px 12px;
  font-size: 13px;
  color: var(--text-muted);
  white-space: pre-wrap;
  border-top: 1px solid var(--border);
  padding-top: 10px;
  line-height: 1.6;
}

.footer {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  column-gap: 9px;
  row-gap: 4px;
  margin-top: 8px;
  min-height: 20px;
  font-size: 11.5px;
  line-height: 20px;
}
.meta {
  color: var(--text-faint);
  line-height: 20px;
}
.stats {
  display: inline-flex;
  align-items: center;
  gap: 10px;
  color: var(--text-faint);
  line-height: 20px;
}
.stats span {
  white-space: nowrap;
  line-height: 20px;
}
.actions {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  height: 20px;
  line-height: 20px;
}
.msg-action {
  display: inline-flex;
  align-items: center;
  height: 20px;
  font: inherit;
  line-height: 20px;
  color: var(--text-faint);
  background: transparent;
  border: none;
  padding: 0;
  margin: 0;
  transition: color 0.15s;
}
.msg-action:hover {
  color: var(--accent-text);
}

.cursor {
  display: inline-block;
  width: 8px;
  height: 17px;
  background: var(--accent);
  border-radius: 2px;
  animation: blink 1s steps(2, start) infinite;
  vertical-align: text-bottom;
}
@keyframes blink {
  50% {
    opacity: 0;
  }
}
</style>
