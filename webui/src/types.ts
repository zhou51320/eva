// Shared types mirroring the eva_acp HTTP API.

export type ChatRole = 'user' | 'assistant' | 'system'

export interface RuntimeArtifact {
  path?: string
  normalized_path?: string
  label?: string
  type?: string
  extension?: string
  size?: string | number
  source_tool?: string
  hints?: Record<string, string>
  [key: string]: unknown
}

export interface RuntimeEvent {
  type: string
  role?: string
  text?: string
  name?: string
  error?: string
  payload?: {
    summary?: string
    artifacts?: RuntimeArtifact[]
    envelope?: Record<string, unknown>
    recovery_hints?: unknown[]
    error?: Record<string, unknown>
    [key: string]: unknown
  }
  [key: string]: unknown
}

export type ChatSegmentKind = 'thinking' | 'answer' | 'tool_call' | 'runtime_event' | 'artifact' | 'error'

export interface ChatSegmentBase {
  id: string
  kind: ChatSegmentKind
  active?: boolean
  createdAt?: string
}

export interface TextChatSegment extends ChatSegmentBase {
  kind: 'thinking' | 'answer'
  text: string
}

export type ToolCallStatus = 'pending' | 'running' | 'completed' | 'failed' | 'warning' | 'interrupted'

export interface ToolOutputTruncation {
  truncated: boolean
  omittedChars: number
  limitChars: number
}

export interface ToolOutputChunk {
  stream: string
  text: string
}

export interface ToolCallSegment extends ChatSegmentBase {
  kind: 'tool_call'
  toolName: string
  status: ToolCallStatus
  callId?: string
  summary?: string
  command?: string
  cwd?: string
  outputs: ToolOutputChunk[]
  outputTruncation?: Record<string, ToolOutputTruncation>
  envelope?: Record<string, unknown>
  result?: Record<string, unknown>
  payload?: Record<string, unknown>
  error?: string
  recoveryHints?: unknown[]
  artifacts?: RuntimeArtifact[]
  events?: RuntimeEvent[]
}

export interface RuntimeEventSegment extends ChatSegmentBase {
  kind: 'runtime_event'
  eventType: string
  label: string
  summary: string
  event?: RuntimeEvent
  payload?: Record<string, unknown>
}

export interface ArtifactSegment extends ChatSegmentBase {
  kind: 'artifact'
  summary?: string
  artifacts: RuntimeArtifact[]
  event?: RuntimeEvent
}

export interface ErrorSegment extends ChatSegmentBase {
  kind: 'error'
  summary: string
  details?: string
  event?: RuntimeEvent
  payload?: Record<string, unknown>
}

export type ChatSegment =
  | TextChatSegment
  | ToolCallSegment
  | RuntimeEventSegment
  | ArtifactSegment
  | ErrorSegment

export interface ChatMessage {
  role: ChatRole
  content: string
  /** Attached image data URLs (user messages only). */
  images?: string[]
  reasoning?: string
  /** Ordered assistant turn timeline, built from the actual stream arrival order. */
  segments?: ChatSegment[]
  /** Tool steps surfaced during the turn (e.g. tool names invoked by EVA). */
  toolSteps?: string[]
  runtimeEvents?: RuntimeEvent[]
  /** Short status line shown under the message (e.g. "完成", "流式输出", "错误"). */
  meta?: string
  /** Final per-turn statistics shown in the assistant footer. */
  stats?: ChatStats
  /** True while the assistant message is still being streamed. */
  pending?: boolean
  error?: boolean
}

export interface ChatStats {
  tokens?: number
  promptTokens?: number
  completionTokens?: number
  totalTokens?: number
  elapsedMs?: number
  tokensPerSecond?: number
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

export interface SkillRecord {
  id: string
  description?: string
  license?: string
  frontmatterBody?: string
  skillRootPath?: string
  skillFilePath?: string
  enabled: boolean
}

export interface SkillsState {
  ok?: boolean
  bridge?: boolean
  skillsRoot?: string
  engineerEnabled?: boolean
  error?: string
  accepted?: boolean
  skills: SkillRecord[]
}

export type SkillAction =
  | { op: 'refresh' }
  | { op: 'set_enabled'; id: string; enabled: boolean }
  | { op: 'remove'; id: string }

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
