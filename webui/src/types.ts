// Shared types mirroring the eva_acp HTTP API.

export type ChatRole = 'user' | 'assistant' | 'system'

export interface ChatMessage {
  role: ChatRole
  content: string
  /** Attached image data URLs (user messages only). */
  images?: string[]
  reasoning?: string
  /** Short status line shown under the message (e.g. "完成", "流式输出", "错误"). */
  meta?: string
  /** True while the assistant message is still being streamed. */
  pending?: boolean
  error?: boolean
}

/** Generation/sampling settings, sent as standard OpenAI request fields. */
export interface GenerationSettings {
  temperature: number
  topP: number
  topK: number
  maxTokens: number
  systemPrompt: string
}

export const DEFAULT_SETTINGS: GenerationSettings = {
  temperature: 0.4,
  topP: 0.95,
  topK: 40,
  maxTokens: 0, // 0 / empty → let the runtime decide
  systemPrompt: '',
}

/** OpenAI multimodal content part. */
export type ContentPart =
  | { type: 'text'; text: string }
  | { type: 'image_url'; image_url: { url: string } }

export interface ApiMessage {
  role: ChatRole
  content: string | ContentPart[]
}

export interface Session {
  id: string
  title: string
  messages: ChatMessage[]
  createdAt: string
}

export interface ModelInfo {
  id: string
  source?: string
  path?: string
  endpoint?: string
  current?: boolean
}

export interface BackendCapabilities {
  full_eva_stack?: boolean
  conversation_owner?: string
  message_input_mode?: string
  chat?: boolean
  stream?: boolean
  stop?: boolean
  knowledge?: boolean
  mcp?: boolean
  enabled_tools?: string[]
  configured_tools_list?: string[]
  configured_tools?: Record<string, boolean>
  tools?: Record<string, boolean>
  tools_enabled?: boolean
  tool_execution_route?: string
  tts?: { model_configured?: boolean; program_available?: boolean }
  [key: string]: unknown
}

export interface BackendState {
  state?: string
  ready?: boolean
  mode?: string
  endpoint?: string
  api_endpoint?: string
  api_model?: string
  current_model?: string
  state_source?: string
  direct_runtime?: boolean
  bridge_available?: boolean
  chat_route?: string
  port?: string | number
  nthread?: string | number
  nctx?: string | number
  backend_choice?: string
  backend_resolved?: string
  last_error?: string
  capabilities?: BackendCapabilities
  [key: string]: unknown
}

export type RuntimeMode = 'local' | 'link'

export interface LoadPayload {
  mode: RuntimeMode
  port?: string
  nthread?: number
  nctx?: number
  backend?: string
  model_path?: string
  api_endpoint?: string
  api_key?: string
  api_model?: string
}
