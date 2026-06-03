<script setup lang="ts">
import { computed, reactive, ref, watch } from 'vue'
import { store } from '../store'
import type { BackendCapabilities, LoadPayload, RuntimeMode } from '../types'

const s = store.state
const tab = ref<'settings' | 'load' | 'models' | 'tools' | 'state'>('settings')

const form = reactive({
  mode: 'local' as RuntimeMode,
  backend: 'auto',
  modelPath: '',
  endpoint: '',
  apiKey: '',
  apiModel: '',
  port: '',
  nthread: '',
  nctx: '',
})

const localModels = computed(() => s.models.filter((m) => m.source !== 'remote'))

watch(
  () => s.drawerOpen,
  (open) => {
    if (!open) return
    const b = s.backend
    if (!b) return
    form.mode = b.mode === 'link' ? 'link' : 'local'
    if (b.api_endpoint) form.endpoint = String(b.api_endpoint)
    if (b.api_model) form.apiModel = String(b.api_model)
    if (b.port) form.port = String(b.port)
    if (b.nthread) form.nthread = String(b.nthread)
    if (b.nctx) form.nctx = String(b.nctx)
    if (b.backend_choice && b.backend_choice !== 'link') form.backend = String(b.backend_choice)
  },
)

function persistSettings() {
  store.updateSettings(s.settings)
}

function submit() {
  const payload: LoadPayload = { mode: form.mode }
  if (form.port.trim()) payload.port = form.port.trim()
  if (form.nthread.trim()) payload.nthread = Number(form.nthread.trim())
  if (form.nctx.trim()) payload.nctx = Number(form.nctx.trim())
  if (form.mode === 'link') {
    payload.api_endpoint = form.endpoint.trim()
    payload.api_key = form.apiKey.trim()
    payload.api_model = form.apiModel.trim()
  } else {
    payload.backend = form.backend
    if (form.modelPath) payload.model_path = form.modelPath
  }
  store.applyLoad(payload)
}

function close() {
  store.setDrawer(false)
}

// --- Tools & capabilities ---
const TOOL_DEFS: { key: string; label: string; note?: string }[] = [
  { key: 'engineer', label: '系统工程师', note: '代码/命令执行' },
  { key: 'knowledge', label: '知识库', note: '需主程序桥接' },
  { key: 'mcp', label: 'MCP', note: '需主程序桥接' },
  { key: 'controller', label: '机体控制', note: '需主程序桥接' },
  { key: 'stablediffusion', label: '视觉 / 绘图', note: '需主程序桥接' },
  { key: 'calculator', label: '计算器' },
]

const caps = computed(() => (s.backend?.capabilities || {}) as BackendCapabilities)

function toolState(key: string): { configured: boolean; enabled: boolean } {
  const c = caps.value
  const configured =
    (c.configured_tools && c.configured_tools[key]) ||
    (Array.isArray(c.configured_tools_list) && c.configured_tools_list.includes(key)) ||
    false
  const enabled =
    (c.tools && c.tools[key]) ||
    (Array.isArray(c.enabled_tools) && c.enabled_tools.includes(key)) ||
    false
  return { configured: Boolean(configured), enabled: Boolean(enabled) }
}

const sourceLabel = computed(() => {
  const b = s.backend
  if (!b) return '未连接'
  const src = b.state_source || (b.direct_runtime ? 'direct_runtime' : b.bridge_available ? 'bridge' : 'legacy_acp')
  if (src === 'direct_runtime') return '直接运行层 (direct)'
  if (src === 'bridge') return '主程序桥接 (bridge)'
  return 'ACP 降级 (degraded)'
})

const statePairs = computed(() => {
  const b = s.backend || {}
  const c = caps.value
  const tts = c.tts || {}
  return [
    ['运行来源', sourceLabel.value],
    ['运行状态', b.state || '-'],
    ['当前模式', b.mode || '-'],
    ['当前模型', b.current_model || b.api_model || '-'],
    ['端点', b.endpoint || b.api_endpoint || '-'],
    ['聊天路径', b.chat_route || '-'],
    ['EVA 能力', c.full_eva_stack ? '完整主程序桥接' : 'direct 基础推理'],
    ['会话归属', c.conversation_owner || '-'],
    ['输入模式', c.message_input_mode || '-'],
    ['工具执行', c.tool_execution_route || '-'],
    ['TTS', `model:${tts.model_configured ? 'ok' : '-'} program:${tts.program_available ? 'ok' : '-'}`],
    ['后端选择 / 解析', `${b.backend_choice || '-'} / ${b.backend_resolved || '-'}`],
    ['线程 / 上下文', `${b.nthread || '-'} / ${b.nctx || '-'}`],
    ['最近错误', b.last_error || '无'],
  ] as [string, string][]
})

const bridged = computed(() => Boolean(caps.value.full_eva_stack))
</script>

<template>
  <transition name="fade">
    <div v-if="s.drawerOpen" class="scrim" @click="close"></div>
  </transition>
  <transition name="slide">
    <aside v-if="s.drawerOpen" class="drawer">
      <header class="drawer__head">
        <span>控制台</span>
        <button class="x" @click="close">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M18 6 6 18M6 6l12 12" /></svg>
        </button>
      </header>

      <nav class="tabs">
        <button :class="{ on: tab === 'settings' }" @click="tab = 'settings'">设置</button>
        <button :class="{ on: tab === 'load' }" @click="tab = 'load'">装载</button>
        <button :class="{ on: tab === 'models' }" @click="tab = 'models'">模型</button>
        <button :class="{ on: tab === 'tools' }" @click="tab = 'tools'">工具</button>
        <button :class="{ on: tab === 'state' }" @click="tab = 'state'">状态</button>
      </nav>

      <div class="drawer__body">
        <!-- SETTINGS -->
        <section v-if="tab === 'settings'" class="pane">
          <div class="slider">
            <div class="slider__top"><span>温度 temperature (0–1)</span><b>{{ s.settings.temperature.toFixed(2) }}</b></div>
            <input v-model.number="s.settings.temperature" type="range" min="0" max="1" step="0.05" @change="persistSettings" />
          </div>
          <div class="slider">
            <div class="slider__top"><span>top_p</span><b>{{ s.settings.topP.toFixed(2) }}</b></div>
            <input v-model.number="s.settings.topP" type="range" min="0" max="1" step="0.01" @change="persistSettings" />
          </div>
          <div class="grid2">
            <label class="field">
              <span>top_k (本地后端)</span>
              <input v-model.number="s.settings.topK" type="number" min="0" step="1" @change="persistSettings" />
            </label>
            <label class="field">
              <span>最大 tokens (0=自动)</span>
              <input v-model.number="s.settings.maxTokens" type="number" min="0" step="1" @change="persistSettings" />
            </label>
          </div>
          <label class="field">
            <span>系统提示词</span>
            <textarea v-model="s.settings.systemPrompt" rows="4" placeholder="可选。作为前置 system 消息发送(桥接模式由主程序接管,可能不生效)。" @change="persistSettings"></textarea>
          </label>
          <button class="ghost" @click="store.resetSettings()">恢复默认</button>
          <p class="muted">温度采用 EVA 约定 0–1(发往后端会换算);其余为标准 OpenAI 字段,direct / 链接模式生效。</p>
        </section>

        <!-- LOAD -->
        <section v-else-if="tab === 'load'" class="pane">
          <label class="field">
            <span>模式</span>
            <select v-model="form.mode">
              <option value="local">本地</option>
              <option value="link">链接</option>
            </select>
          </label>

          <template v-if="form.mode === 'local'">
            <label class="field">
              <span>本地模型</span>
              <select v-model="form.modelPath">
                <option v-if="localModels.length === 0" value="">未发现本地模型</option>
                <option v-for="m in localModels" :key="m.path || m.id" :value="m.path || ''">{{ m.id || m.path }}</option>
              </select>
            </label>
            <label class="field">
              <span>后端</span>
              <select v-model="form.backend">
                <option value="auto">auto</option>
                <option value="cpu">cpu</option>
                <option value="cpu-noavx">cpu-noavx</option>
                <option value="cuda">cuda</option>
                <option value="vulkan">vulkan</option>
                <option value="opencl">opencl</option>
              </select>
            </label>
          </template>

          <template v-else>
            <label class="field"><span>端点</span><input v-model="form.endpoint" type="text" placeholder="https://example.com/v1" /></label>
            <label class="field"><span>密钥</span><input v-model="form.apiKey" type="password" placeholder="sk-..." /></label>
            <label class="field"><span>模型</span><input v-model="form.apiModel" type="text" placeholder="gpt-4o-mini / qwen / ..." /></label>
          </template>

          <div class="grid3">
            <label class="field"><span>端口</span><input v-model="form.port" type="text" placeholder="8080" /></label>
            <label class="field"><span>线程</span><input v-model="form.nthread" type="number" min="1" placeholder="自动" /></label>
            <label class="field"><span>上下文</span><input v-model="form.nctx" type="number" min="1" placeholder="4096" /></label>
          </div>

          <button class="apply" :disabled="s.loading" @click="submit">{{ s.loading ? '应用中…' : '应用并装载' }}</button>
          <p v-if="s.loadFeedback" class="feedback">{{ s.loadFeedback }}</p>
        </section>

        <!-- MODELS -->
        <section v-else-if="tab === 'models'" class="pane">
          <button class="ghost" @click="store.refreshAll()">刷新列表</button>
          <p v-if="s.models.length === 0" class="muted">当前没有可用模型。链接模式下请先应用端点和模型名。</p>
          <div v-for="m in s.models" :key="m.id" class="model" :class="{ 'model--cur': m.current }">
            <div class="model__id">{{ m.id || 'unnamed' }}</div>
            <div class="model__detail">{{ m.source === 'remote' ? m.endpoint : m.path }}</div>
          </div>
        </section>

        <!-- TOOLS & CAPABILITIES -->
        <section v-else-if="tab === 'tools'" class="pane">
          <button class="ghost" @click="store.refreshAll()">刷新能力</button>
          <p class="muted">
            工具与扩展能力由 EVA 主程序拥有。{{ bridged ? '当前已桥接主程序,可直接开关;切换会重置当前对话。' : '当前为直连/降级模式,开关需先桥接主程序(在运行中的主程序上操作)。' }}
          </p>
          <div v-for="t in TOOL_DEFS" :key="t.key" class="tool-row">
            <div class="tool-row__main">
              <div class="tool-row__name">{{ t.label }}</div>
              <div class="tool-row__note">{{ t.note || '' }}</div>
            </div>
            <label v-if="bridged" class="switch" :class="{ 'switch--busy': s.loading }">
              <input
                type="checkbox"
                :checked="toolState(t.key).enabled"
                :disabled="s.loading"
                @change="store.setTool(t.key, ($event.target as HTMLInputElement).checked)"
              />
              <span class="switch__track"><span class="switch__thumb"></span></span>
            </label>
            <div v-else class="tool-row__badges">
              <span class="badge" :class="toolState(t.key).configured ? 'badge--on' : ''">{{ toolState(t.key).configured ? '已配置' : '未配置' }}</span>
              <span class="badge" :class="toolState(t.key).enabled ? 'badge--ok' : ''">{{ toolState(t.key).enabled ? '已启用' : '未启用' }}</span>
            </div>
          </div>
          <p v-if="s.loadFeedback" class="feedback">{{ s.loadFeedback }}</p>
          <div class="cap-line">工具执行路径:<b>{{ caps.tool_execution_route || '-' }}</b></div>
        </section>

        <!-- STATE / CONNECTION -->
        <section v-else class="pane">
          <button class="ghost" @click="store.refreshAll()">刷新状态</button>
          <p v-if="s.healthError" class="feedback">连接错误:{{ s.healthError }}</p>
          <div class="state">
            <div v-for="[k, v] in statePairs" :key="k" class="state__row">
              <span class="state__k">{{ k }}</span>
              <span class="state__v">{{ v }}</span>
            </div>
          </div>
        </section>
      </div>
    </aside>
  </transition>
</template>

<style scoped>
.scrim {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.45);
  z-index: 40;
}
.drawer {
  position: fixed;
  top: 0;
  right: 0;
  bottom: 0;
  width: 400px;
  max-width: 94vw;
  z-index: 41;
  background: var(--bg-elevated);
  border-left: 1px solid var(--border);
  box-shadow: var(--shadow);
  display: flex;
  flex-direction: column;
}

.drawer__head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 16px 18px;
  font-weight: 600;
  border-bottom: 1px solid var(--border);
}
.x {
  display: grid;
  place-items: center;
  width: 32px;
  height: 32px;
  border-radius: 8px;
  border: none;
  background: transparent;
  color: var(--text-muted);
}
.x:hover {
  background: var(--surface-hover);
  color: var(--text);
}

.tabs {
  display: flex;
  padding: 8px 12px 0;
  border-bottom: 1px solid var(--border);
}
.tabs button {
  flex: 1;
  padding: 9px 2px;
  border: none;
  background: transparent;
  color: var(--text-muted);
  font-size: 13px;
  border-bottom: 2px solid transparent;
  margin-bottom: -1px;
}
.tabs button.on {
  color: var(--text);
  border-bottom-color: var(--accent);
}

.drawer__body {
  flex: 1;
  overflow-y: auto;
  padding: 18px;
}
.pane {
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.field {
  display: flex;
  flex-direction: column;
  gap: 6px;
  font-size: 12.5px;
  color: var(--text-muted);
}
.field input,
.field select,
.field textarea {
  padding: 9px 11px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border-strong);
  background: var(--surface);
  color: var(--text);
  outline: none;
  font-family: inherit;
}
.field input,
.field select {
  height: 38px;
}
.field textarea {
  resize: vertical;
  line-height: 1.5;
}
.field input:focus,
.field select:focus,
.field textarea:focus {
  border-color: var(--accent);
}

.grid2 {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
}
.grid3 {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 10px;
}

.slider {
  display: flex;
  flex-direction: column;
  gap: 7px;
}
.slider__top {
  display: flex;
  justify-content: space-between;
  font-size: 12.5px;
  color: var(--text-muted);
}
.slider__top b {
  color: var(--text);
  font-variant-numeric: tabular-nums;
}
.slider input[type='range'] {
  width: 100%;
  accent-color: var(--accent);
}

.apply {
  height: 42px;
  border-radius: var(--radius-sm);
  border: none;
  background: var(--accent);
  color: #fff;
  font-weight: 600;
  margin-top: 4px;
}
.apply:hover:not(:disabled) {
  filter: brightness(1.1);
}

.ghost {
  align-self: flex-start;
  height: 32px;
  padding: 0 14px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border);
  background: var(--surface);
  color: var(--text);
  font-size: 12.5px;
}
.ghost:hover {
  background: var(--surface-hover);
}

.feedback {
  font-size: 12.5px;
  color: var(--accent-text);
  margin: 0;
  word-break: break-word;
}
.muted {
  font-size: 12px;
  color: var(--text-faint);
  margin: 0;
  line-height: 1.6;
}

.model {
  padding: 11px 13px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border);
  background: var(--surface);
}
.model--cur {
  border-color: var(--accent);
  background: var(--accent-soft);
}
.model__id {
  font-weight: 500;
  font-size: 13.5px;
}
.model__detail {
  font-size: 12px;
  color: var(--text-faint);
  word-break: break-all;
  margin-top: 2px;
}

.tool-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  padding: 11px 13px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border);
  background: var(--surface);
}
.tool-row__name {
  font-weight: 500;
  font-size: 13.5px;
}
.tool-row__note {
  font-size: 11.5px;
  color: var(--text-faint);
  margin-top: 1px;
}
.tool-row__badges {
  display: flex;
  gap: 6px;
  flex: none;
}
.badge {
  font-size: 11px;
  padding: 3px 8px;
  border-radius: 999px;
  border: 1px solid var(--border);
  color: var(--text-faint);
  background: var(--surface-2);
}
.badge--on {
  color: var(--accent-text);
  border-color: var(--accent);
  background: var(--accent-soft);
}
.badge--ok {
  color: var(--ok);
  border-color: var(--ok);
  background: transparent;
}
.cap-line {
  font-size: 12.5px;
  color: var(--text-muted);
}
.cap-line b {
  color: var(--text);
}

.switch {
  flex: none;
  cursor: pointer;
}
.switch input {
  position: absolute;
  opacity: 0;
  width: 0;
  height: 0;
}
.switch__track {
  display: inline-block;
  width: 38px;
  height: 22px;
  border-radius: 999px;
  background: var(--surface-2);
  border: 1px solid var(--border-strong);
  position: relative;
  transition: background 0.15s, border-color 0.15s;
}
.switch__thumb {
  position: absolute;
  top: 2px;
  left: 2px;
  width: 16px;
  height: 16px;
  border-radius: 50%;
  background: var(--text-muted);
  transition: transform 0.15s, background 0.15s;
}
.switch input:checked + .switch__track {
  background: var(--accent-soft);
  border-color: var(--accent);
}
.switch input:checked + .switch__track .switch__thumb {
  transform: translateX(16px);
  background: var(--accent);
}
.switch--busy {
  opacity: 0.5;
  cursor: progress;
}

.state {
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  overflow: hidden;
}
.state__row {
  display: flex;
  gap: 12px;
  padding: 9px 13px;
  font-size: 12.5px;
  border-bottom: 1px solid var(--border);
}
.state__row:last-child {
  border-bottom: none;
}
.state__k {
  flex: none;
  width: 100px;
  color: var(--text-faint);
}
.state__v {
  color: var(--text);
  word-break: break-word;
}

.fade-enter-active,
.fade-leave-active {
  transition: opacity 0.2s;
}
.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}
.slide-enter-active,
.slide-leave-active {
  transition: transform 0.22s ease;
}
.slide-enter-from,
.slide-leave-to {
  transform: translateX(100%);
}
</style>
