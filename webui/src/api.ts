import type { ApiMessage, BackendState, GenerationSettings, LoadPayload, ModelInfo } from './types'

async function parseJson(response: Response): Promise<any> {
  const text = await response.text()
  let json: any = {}
  try {
    json = text ? JSON.parse(text) : {}
  } catch {
    json = { raw: text }
  }
  if (!response.ok) {
    throw new Error(json.error || json.details || text || `HTTP ${response.status}`)
  }
  return json
}

export async function fetchHealth(): Promise<BackendState> {
  return parseJson(await fetch('/health'))
}

export async function fetchBackendState(): Promise<BackendState> {
  return parseJson(await fetch('/api/backend/state'))
}

export async function fetchModels(): Promise<ModelInfo[]> {
  const json = await parseJson(await fetch('/v1/models'))
  return Array.isArray(json.data) ? json.data : []
}

export async function applyLoad(payload: LoadPayload): Promise<BackendState> {
  return parseJson(
    await fetch('/api/backend/load', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    }),
  )
}

export async function resetConversation(): Promise<BackendState> {
  return parseJson(await fetch('/api/runtime/reset', { method: 'POST' }))
}

/** Toggle main-EVA tool/knowledge/MCP capabilities (bridge mode only). */
export async function setTools(payload: Record<string, boolean>): Promise<unknown> {
  return parseJson(
    await fetch('/api/runtime/tools', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    }),
  )
}

export async function stopTurn(): Promise<BackendState> {
  return parseJson(await fetch('/api/runtime/stop', { method: 'POST' }))
}

export interface ChatDelta {
  content: string
  reasoning: string
}

export interface ChatStreamCallbacks {
  onDelta: (delta: ChatDelta) => void
  onToolStep?: (tool: string) => void
  signal?: AbortSignal
}

/**
 * Send an OpenAI-compatible chat completion. Handles both streaming (SSE) and
 * non-streaming responses, normalizing reasoning/reasoning_content into one field.
 * Sampling fields are forwarded as standard OpenAI request fields.
 * Returns the final { content, reasoning }.
 */
export async function sendChat(
  messages: ApiMessage[],
  options: { stream: boolean; model?: string; settings?: GenerationSettings },
  callbacks: ChatStreamCallbacks,
): Promise<ChatDelta> {
  const payload: Record<string, unknown> = {
    messages,
    stream: options.stream,
  }
  if (options.model) payload.model = options.model
  const s = options.settings
  if (s) {
    if (Number.isFinite(s.temperature)) payload.temperature = s.temperature
    if (Number.isFinite(s.topP)) payload.top_p = s.topP
    if (Number.isFinite(s.topK) && s.topK > 0) payload.top_k = s.topK
    if (Number.isFinite(s.maxTokens) && s.maxTokens > 0) payload.max_tokens = s.maxTokens
  }

  const response = await fetch('/v1/chat/completions', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
    signal: callbacks.signal,
  })

  if (!response.ok) {
    const text = await response.text()
    throw new Error(normalizeError(text, `HTTP ${response.status}`))
  }

  let content = ''
  let reasoning = ''

  if (!options.stream || !response.body) {
    const data = await response.json()
    content = data.choices?.[0]?.message?.content || ''
    reasoning = data.choices?.[0]?.message?.reasoning || ''
    callbacks.onDelta({ content, reasoning })
    return { content, reasoning }
  }

  const reader = response.body.getReader()
  const decoder = new TextDecoder()
  let buffer = ''

  while (true) {
    const { value, done } = await reader.read()
    if (done) break
    buffer += decoder.decode(value, { stream: true })
    const lines = buffer.split(/\r?\n/)
    buffer = lines.pop() || ''
    for (const line of lines) {
      if (!line.startsWith('data:')) continue
      const chunk = line.slice(5).trim()
      if (!chunk || chunk === '[DONE]') continue
      const data = JSON.parse(chunk)
      if (data.error) throw new Error(String(data.error))
      // Authoritative reconciliation: replace accumulated content/reasoning.
      if (data.eva_final) {
        if (typeof data.eva_final.content === 'string') content = data.eva_final.content
        if (typeof data.eva_final.reasoning === 'string') reasoning = data.eva_final.reasoning
        callbacks.onDelta({ content, reasoning })
        continue
      }
      const delta = data.choices?.[0]?.delta || {}
      if (typeof delta.eva_tool === 'string') {
        callbacks.onToolStep?.(delta.eva_tool)
        continue
      }
      if (typeof delta.content === 'string') content += delta.content
      if (typeof delta.reasoning === 'string') reasoning += delta.reasoning
      if (typeof delta.reasoning_content === 'string') reasoning += delta.reasoning_content
      if (!delta.content && data.choices?.[0]?.message?.content) {
        content += data.choices[0].message.content
      }
      callbacks.onDelta({ content, reasoning })
    }
  }

  return { content, reasoning }
}

export function normalizeError(raw: unknown, fallback: string): string {
  const text = String(raw || '').trim()
  if (!text) return fallback
  try {
    const parsed = JSON.parse(text)
    return parsed.error || parsed.details || parsed.message || text
  } catch {
    return text
  }
}
