<script setup lang="ts">
import { nextTick, ref } from 'vue'
import { store } from '../store'

const s = store.state
const text = ref('')
const stream = ref(true)
const images = ref<string[]>([])
const textarea = ref<HTMLTextAreaElement | null>(null)
const fileInput = ref<HTMLInputElement | null>(null)

function autoGrow() {
  const el = textarea.value
  if (!el) return
  el.style.height = 'auto'
  el.style.height = Math.min(el.scrollHeight, 220) + 'px'
}

function pickImages() {
  fileInput.value?.click()
}

function onFiles(event: Event) {
  const input = event.target as HTMLInputElement
  const files = Array.from(input.files || [])
  for (const file of files) {
    if (!file.type.startsWith('image/')) continue
    const reader = new FileReader()
    reader.onload = () => {
      if (typeof reader.result === 'string') images.value.push(reader.result)
    }
    reader.readAsDataURL(file)
  }
  input.value = '' // allow re-picking the same file
}

function removeImage(i: number) {
  images.value.splice(i, 1)
}

async function submit() {
  const value = text.value.trim()
  if ((!value && images.value.length === 0) || s.streaming) return
  const imgs = images.value.slice()
  text.value = ''
  images.value = []
  await nextTick()
  autoGrow()
  store.sendMessage(value, imgs, stream.value)
}

function onKeydown(event: KeyboardEvent) {
  if (event.key === 'Enter' && !event.shiftKey && !event.isComposing) {
    event.preventDefault()
    submit()
  }
}
</script>

<template>
  <div class="composer-wrap">
    <div class="composer">
      <div v-if="images.length" class="thumbs">
        <div v-for="(img, i) in images" :key="i" class="thumb">
          <img :src="img" alt="attachment" />
          <button class="thumb__x" title="移除" @click="removeImage(i)">×</button>
        </div>
      </div>

      <textarea
        ref="textarea"
        v-model="text"
        class="input"
        rows="1"
        placeholder="给 EVA 发送消息…  (Enter 发送 / Shift+Enter 换行)"
        @input="autoGrow"
        @keydown="onKeydown"
      ></textarea>

      <div class="row">
        <button class="tool" title="附加图片(视觉模型 / 链接模式)" @click="pickImages">
          <svg width="17" height="17" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="18" height="18" rx="2" /><circle cx="8.5" cy="8.5" r="1.5" /><path d="m21 15-5-5L5 21" /></svg>
        </button>
        <input ref="fileInput" type="file" accept="image/*" multiple hidden @change="onFiles" />
        <label class="stream">
          <input v-model="stream" type="checkbox" />
          <span>流式</span>
        </label>
        <div class="spacer"></div>
        <button v-if="s.streaming" class="stop" @click="store.stop()">
          <svg width="13" height="13" viewBox="0 0 24 24" fill="currentColor"><rect x="6" y="6" width="12" height="12" rx="2" /></svg>
          停止
        </button>
        <button class="send" :disabled="(!text.trim() && images.length === 0) || s.streaming" @click="submit">
          <svg width="17" height="17" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M22 2 11 13M22 2l-7 20-4-9-9-4 20-7z" /></svg>
        </button>
      </div>
    </div>
    <p class="hint">WebUI 历史仅本地展示;运行状态与真实上下文以 EVA 运行层为准。图片需视觉模型 / 链接模式。</p>
  </div>
</template>

<style scoped>
.composer-wrap {
  max-width: 820px;
  width: 100%;
  margin: 0 auto;
  padding: 8px 24px 16px;
}

.composer {
  border: 1px solid var(--border-strong);
  border-radius: var(--radius);
  background: var(--surface);
  padding: 10px 12px 8px;
  transition: border-color 0.15s;
}
.composer:focus-within {
  border-color: var(--accent);
}

.thumbs {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-bottom: 8px;
}
.thumb {
  position: relative;
  width: 56px;
  height: 56px;
  border-radius: 8px;
  overflow: hidden;
  border: 1px solid var(--border);
}
.thumb img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
}
.thumb__x {
  position: absolute;
  top: 2px;
  right: 2px;
  width: 18px;
  height: 18px;
  border-radius: 50%;
  border: none;
  background: rgba(0, 0, 0, 0.6);
  color: #fff;
  font-size: 13px;
  line-height: 1;
  display: grid;
  place-items: center;
}

.input {
  width: 100%;
  border: none;
  outline: none;
  resize: none;
  background: transparent;
  color: var(--text);
  line-height: 1.55;
  max-height: 220px;
  padding: 2px 2px 6px;
}

.row {
  display: flex;
  align-items: center;
  gap: 10px;
}
.spacer {
  flex: 1;
}

.tool {
  display: inline-grid;
  place-items: center;
  width: 34px;
  height: 34px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border);
  background: transparent;
  color: var(--text-muted);
}
.tool:hover {
  background: var(--surface-hover);
  color: var(--text);
}

.stream {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 12.5px;
  color: var(--text-muted);
  user-select: none;
  cursor: pointer;
}
.stream input {
  accent-color: var(--accent);
}

.stop {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  height: 34px;
  padding: 0 14px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border-strong);
  background: var(--surface-2);
  color: var(--text);
  font-size: 13px;
}
.stop:hover {
  background: var(--surface-hover);
}

.send {
  display: inline-grid;
  place-items: center;
  width: 38px;
  height: 38px;
  border-radius: var(--radius-sm);
  border: none;
  background: var(--accent);
  color: #fff;
  transition: filter 0.15s;
}
.send:hover:not(:disabled) {
  filter: brightness(1.1);
}
.send:disabled {
  background: var(--surface-2);
  color: var(--text-faint);
}

.hint {
  font-size: 11px;
  color: var(--text-faint);
  text-align: center;
  margin: 8px 0 0;
}
</style>
