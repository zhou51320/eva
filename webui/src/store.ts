import { reactive } from 'vue'
import * as api from './api'
import { DEFAULT_SETTINGS } from './types'
import type {
  ApiMessage,
  BackendState,
  ChatMessage,
  ContentPart,
  GenerationSettings,
  LoadPayload,
  ModelInfo,
  RuntimeMode,
  Session,
} from './types'

const SESSIONS_KEY = 'eva-acp-webui-sessions'
const THEME_KEY = 'eva-acp-webui-theme'
const SETTINGS_KEY = 'eva-acp-webui-settings'

interface StoreState {
  sessions: Session[]
  activeSessionId: string | null
  backend: BackendState | null
  models: ModelInfo[]
  streaming: boolean
  drawerOpen: boolean
  theme: 'dark' | 'light'
  healthError: string | null
  loadFeedback: string
  loading: boolean
  settings: GenerationSettings
}

const state = reactive<StoreState>({
  sessions: [],
  activeSessionId: null,
  backend: null,
  models: [],
  streaming: false,
  drawerOpen: false,
  theme: 'dark',
  healthError: null,
  loadFeedback: '',
  loading: false,
  settings: { ...DEFAULT_SETTINGS },
})

let abortController: AbortController | null = null

function now(): string {
  return new Date().toISOString()
}

function newId(): string {
  return Date.now().toString(36) + Math.floor(Math.random() * 1e6).toString(36)
}

function persistSessions() {
  try {
    localStorage.setItem(SESSIONS_KEY, JSON.stringify(state.sessions.slice(0, 50)))
  } catch {
    /* storage may be full or disabled */
  }
}

function loadSessions() {
  try {
    const raw = localStorage.getItem(SESSIONS_KEY)
    if (!raw) return
    const parsed = JSON.parse(raw)
    if (Array.isArray(parsed)) {
      state.sessions = parsed
      state.activeSessionId = parsed[0]?.id ?? null
    }
  } catch {
    /* ignore corrupt storage */
  }
}

function persistSettings() {
  try {
    localStorage.setItem(SETTINGS_KEY, JSON.stringify(state.settings))
  } catch {
    /* ignore */
  }
}

function loadSettings() {
  try {
    const raw = localStorage.getItem(SETTINGS_KEY)
    if (raw) state.settings = { ...DEFAULT_SETTINGS, ...JSON.parse(raw) }
  } catch {
    /* ignore */
  }
}

function updateSettings(partial: Partial<GenerationSettings>) {
  Object.assign(state.settings, partial)
  persistSettings()
}

function resetSettings() {
  state.settings = { ...DEFAULT_SETTINGS }
  persistSettings()
}

/** Build OpenAI message content: multimodal array when images are present, else a string. */
function toApiContent(text: string, images?: string[]): string | ContentPart[] {
  if (!images || images.length === 0) return text
  const parts: ContentPart[] = []
  if (text) parts.push({ type: 'text', text })
  for (const url of images) parts.push({ type: 'image_url', image_url: { url } })
  return parts
}

function activeSession(): Session {
  let session = state.sessions.find((s) => s.id === state.activeSessionId)
  if (!session) {
    session = { id: newId(), title: '新对话', messages: [], createdAt: now() }
    state.sessions.unshift(session)
    state.activeSessionId = session.id
    persistSessions()
  }
  return session
}

async function refreshAll() {
  try {
    const [health, backend, models] = await Promise.all([
      api.fetchHealth(),
      api.fetchBackendState(),
      api.fetchModels(),
    ])
    state.backend = { ...health, ...backend }
    state.models = models
    state.healthError = null
  } catch (error) {
    state.healthError = (error as Error).message
  }
}

function createSession() {
  const session: Session = { id: newId(), title: '新对话', messages: [], createdAt: now() }
  state.sessions.unshift(session)
  state.activeSessionId = session.id
  persistSessions()
}

async function newChat() {
  try {
    await api.resetConversation()
  } catch {
    /* reset best-effort; still start a fresh local draft */
  }
  createSession()
  await refreshAll()
}

function selectSession(id: string) {
  state.activeSessionId = id
}

function deleteSession(id: string) {
  state.sessions = state.sessions.filter((s) => s.id !== id)
  if (state.activeSessionId === id) {
    state.activeSessionId = state.sessions[0]?.id ?? null
    if (!state.activeSessionId) createSession()
  }
  persistSessions()
}

function preferredModel(): string | undefined {
  const current = state.models.find((m) => m.current)
  if (current?.source === 'remote') return current.id
  if (state.backend?.api_model) return String(state.backend.api_model)
  return current?.id
}

async function sendMessage(text: string, images: string[], stream: boolean) {
  const input = text.trim()
  if ((!input && images.length === 0) || state.streaming) return
  const session = activeSession()

  // Build the OpenAI message list: optional system prompt, prior turns as text,
  // then the new user message (multimodal when it has images).
  const history: ApiMessage[] = []
  const hasSystem = session.messages.some((m) => m.role === 'system')
  if (state.settings.systemPrompt.trim() && !hasSystem) {
    history.push({ role: 'system', content: state.settings.systemPrompt.trim() })
  }
  for (const m of session.messages) {
    if (m.error || (m.role !== 'user' && m.role !== 'assistant' && m.role !== 'system')) continue
    history.push({ role: m.role, content: toApiContent(m.content, m.images) })
  }
  history.push({ role: 'user', content: toApiContent(input, images) })

  session.messages.push({
    role: 'user',
    content: input,
    images: images.length ? images : undefined,
    meta: new Date().toLocaleTimeString(),
  })
  if (session.title === '新对话') session.title = (input || '图片消息').slice(0, 24)
  const assistant: ChatMessage = { role: 'assistant', content: '', reasoning: '', pending: true, meta: '生成中…' }
  session.messages.push(assistant)
  persistSessions()

  state.streaming = true
  abortController = new AbortController()

  try {
    await api.sendChat(history, { stream, model: preferredModel(), settings: state.settings }, {
      signal: abortController.signal,
      onDelta: ({ content, reasoning }) => {
        assistant.content = content
        assistant.reasoning = reasoning
        assistant.meta = reasoning ? '流式输出 · 含思考' : '流式输出'
      },
    })
    assistant.pending = false
    assistant.meta = assistant.content ? '完成' : '完成 · 空响应'
    if (!assistant.content) assistant.content = '(空响应)'
  } catch (error) {
    const err = error as Error
    assistant.pending = false
    assistant.error = true
    if (err.name === 'AbortError') {
      assistant.meta = '已停止'
      if (!assistant.content) assistant.content = '_已停止_'
    } else {
      assistant.meta = '错误'
      assistant.content = `请求失败：${err.message}`
    }
  } finally {
    state.streaming = false
    abortController = null
    persistSessions()
    refreshAll()
  }
}

async function stop() {
  if (abortController) abortController.abort()
  try {
    await api.stopTurn()
  } catch {
    /* best effort */
  } finally {
    await refreshAll()
  }
}

async function applyLoad(payload: LoadPayload) {
  state.loadFeedback = '正在应用…'
  state.loading = true
  try {
    state.backend = await api.applyLoad(payload)
    state.loadFeedback = '已应用。'
    await refreshAll()
  } catch (error) {
    state.loadFeedback = (error as Error).message
  } finally {
    state.loading = false
  }
}

/** Toggle a main-EVA capability via the bridge, then refresh real state. */
async function setTool(key: string, value: boolean) {
  state.loadFeedback = ''
  state.loading = true
  try {
    await api.setTools({ [key]: value })
    await refreshAll()
  } catch (error) {
    state.loadFeedback = (error as Error).message
  } finally {
    state.loading = false
  }
}

function applyTheme() {
  document.documentElement.dataset.theme = state.theme
}

function toggleTheme() {
  state.theme = state.theme === 'dark' ? 'light' : 'dark'
  try {
    localStorage.setItem(THEME_KEY, state.theme)
  } catch {
    /* ignore */
  }
  applyTheme()
}

function loadTheme() {
  try {
    const saved = localStorage.getItem(THEME_KEY)
    if (saved === 'light' || saved === 'dark') {
      state.theme = saved
    } else if (window.matchMedia && window.matchMedia('(prefers-color-scheme: light)').matches) {
      state.theme = 'light'
    }
  } catch {
    /* ignore */
  }
  applyTheme()
}

function setDrawer(open: boolean) {
  state.drawerOpen = open
  if (open) refreshAll()
}

async function init() {
  loadTheme()
  loadSettings()
  loadSessions()
  if (state.sessions.length === 0) createSession()
  await refreshAll()
}

export const store = {
  state,
  init,
  refreshAll,
  newChat,
  selectSession,
  deleteSession,
  sendMessage,
  stop,
  applyLoad,
  setTool,
  toggleTheme,
  setDrawer,
  updateSettings,
  resetSettings,
  activeSession,
  preferredModel,
}

export type { BackendState, ChatMessage, ModelInfo, RuntimeMode, Session }
