import type {
  ArtifactSegment,
  ChatMessage,
  ChatSegment,
  ErrorSegment,
  RuntimeArtifact,
  RuntimeEvent,
  RuntimeEventSegment,
  TextChatSegment,
  ToolCallSegment,
  ToolCallStatus,
  ToolOutputTruncation,
} from './types'

const TOOL_OUTPUT_LIMIT_CHARS = 32 * 1024
const STRUCTURED_TEXT_LIMIT_CHARS = 24 * 1024
const STRUCTURED_ARRAY_LIMIT = 80
const STRUCTURED_DEPTH_LIMIT = 5

let segmentCounter = 0

function segmentId(prefix: string): string {
  segmentCounter += 1
  return `${prefix}-${Date.now().toString(36)}-${segmentCounter.toString(36)}`
}

function ensureSegments(message: ChatMessage): ChatSegment[] {
  if (!message.segments) message.segments = []
  return message.segments
}

function deactivateTail(segments: ChatSegment[]) {
  const tail = segments[segments.length - 1]
  if (tail) tail.active = false
}

function pushSegment(message: ChatMessage, segment: ChatSegment) {
  const segments = ensureSegments(message)
  deactivateTail(segments)
  segment.active = true
  segments.push(segment)
}

export function appendTextSegment(message: ChatMessage, kind: 'thinking' | 'answer', text: string) {
  if (!text) return
  const segments = ensureSegments(message)
  const tail = segments[segments.length - 1]
  if (tail?.kind === kind) {
    ;(tail as TextChatSegment).text += text
    tail.active = true
    return
  }
  pushSegment(message, {
    id: segmentId(kind),
    kind,
    text,
    createdAt: new Date().toISOString(),
  })
}

export function appendRuntimeStatusSegment(message: ChatMessage, event: RuntimeEvent) {
  pushSegment(message, runtimeSegmentFromEvent(event))
}

export function appendErrorSegment(message: ChatMessage, summary: string, details?: string, event?: RuntimeEvent) {
  const segment: ErrorSegment = {
    id: segmentId('error'),
    kind: 'error',
    summary,
    details,
    event: event ? compactEvent(event) : undefined,
    payload: boundedRecord(event?.payload),
    createdAt: new Date().toISOString(),
  }
  pushSegment(message, segment)
}

export function finalizeTimeline(message: ChatMessage) {
  for (const segment of message.segments || []) segment.active = false
}

export function reconcileFinalText(message: ChatMessage, content: string, reasoning: string) {
  const segments = ensureSegments(message)
  const answerSegments = segments.filter((segment): segment is TextChatSegment => segment.kind === 'answer')
  const thinkingSegments = segments.filter((segment): segment is TextChatSegment => segment.kind === 'thinking')
  const hasNonText = segments.some((segment) => segment.kind !== 'answer' && segment.kind !== 'thinking')

  if (content && answerSegments.length === 0) {
    appendTextSegment(message, 'answer', content)
  } else if (content && !hasNonText && answerSegments.length === 1 && answerSegments[0].text !== content) {
    answerSegments[0].text = content
  }

  if (reasoning && thinkingSegments.length === 0) {
    appendTextSegment(message, 'thinking', reasoning)
  } else if (reasoning && !hasNonText && thinkingSegments.length === 1 && thinkingSegments[0].text !== reasoning) {
    thinkingSegments[0].text = reasoning
  }
}

export function appendToolMarker(message: ChatMessage, toolName: string) {
  if (!toolName.trim()) return
  const existing = findRecentToolSegment(ensureSegments(message), toolName, '')
  if (existing && (existing.status === 'pending' || existing.status === 'running')) {
    existing.active = true
    return
  }
  pushSegment(message, createToolSegment(toolName.trim(), undefined, 'running'))
}

export function applyRuntimeEventToTimeline(message: ChatMessage, event: RuntimeEvent) {
  const type = event.type
  if (type === 'tool_started') {
    upsertToolStarted(message, event)
    return
  }
  if (type === 'tool_output') {
    appendToolOutput(message, event)
    return
  }
  if (type === 'tool_finished') {
    finishTool(message, event)
    return
  }
  if (type === 'artifact_ready') {
    attachArtifact(message, event)
    return
  }
  if (type === 'recovering') {
    attachRecovery(message, event)
    return
  }
  if (type === 'task_failed') {
    appendErrorSegment(message, eventSummary(event), event.error || jsonPreview(event.payload), event)
    return
  }
  appendRuntimeStatusSegment(message, event)
}

function upsertToolStarted(message: ChatMessage, event: RuntimeEvent) {
  const name = toolNameFromEvent(event)
  const callId = toolCallIdFromEvent(event)
  const segments = ensureSegments(message)
  let tool = findRecentToolSegment(segments, name, callId)
  if (!tool || (tool.status !== 'pending' && tool.status !== 'running')) {
    tool = createToolSegment(name, callId, 'running')
    pushSegment(message, tool)
  } else {
    deactivateTail(segments)
    tool.active = true
  }
  mergeToolEvent(tool, event)
  tool.status = 'running'
}

function appendToolOutput(message: ChatMessage, event: RuntimeEvent) {
  const tool = toolForEvent(message, event, 'running')
  mergeToolEvent(tool, event)
  const stream = stringFrom(event.payload?.stream) || 'output'
  const text = event.text || stringFrom(event.payload?.text) || ''
  appendOutputChunk(tool, stream, text)
}

function finishTool(message: ChatMessage, event: RuntimeEvent) {
  const tool = toolForEvent(message, event, 'completed')
  mergeToolEvent(tool, event)
  tool.status = statusFromFinishedEvent(event)
  tool.active = false
}

function attachArtifact(message: ChatMessage, event: RuntimeEvent) {
  const artifacts = artifactsFromEvent(event)
  if (artifacts.length === 0) {
    appendRuntimeStatusSegment(message, event)
    return
  }
  const tool = findRecentToolSegment(ensureSegments(message), toolNameFromEvent(event), toolCallIdFromEvent(event))
  if (tool) {
    tool.artifacts = mergeArtifacts(tool.artifacts || [], artifacts)
    tool.events = [...(tool.events || []), compactEvent(event)]
    tool.active = false
    return
  }
  const segment: ArtifactSegment = {
    id: segmentId('artifact'),
    kind: 'artifact',
    summary: eventSummary(event),
    artifacts,
    event: compactEvent(event),
    createdAt: new Date().toISOString(),
  }
  pushSegment(message, segment)
}

function attachRecovery(message: ChatMessage, event: RuntimeEvent) {
  const tool = findRecentToolSegment(ensureSegments(message), toolNameFromEvent(event), toolCallIdFromEvent(event))
  if (!tool) {
    appendRuntimeStatusSegment(message, event)
    return
  }
  mergeToolEvent(tool, event)
  tool.status = tool.status === 'failed' ? 'failed' : 'warning'
  tool.active = false
}

function toolForEvent(message: ChatMessage, event: RuntimeEvent, fallbackStatus: ToolCallStatus): ToolCallSegment {
  const name = toolNameFromEvent(event)
  const callId = toolCallIdFromEvent(event)
  let tool = findRecentToolSegment(ensureSegments(message), name, callId)
  if (!tool) {
    tool = createToolSegment(name, callId, fallbackStatus)
    pushSegment(message, tool)
  }
  return tool
}

function createToolSegment(toolName: string, callId: string | undefined, status: ToolCallStatus): ToolCallSegment {
  return {
    id: segmentId('tool'),
    kind: 'tool_call',
    toolName: toolName || 'tool',
    callId,
    status,
    outputs: [],
    createdAt: new Date().toISOString(),
  }
}

function findRecentToolSegment(segments: ChatSegment[], toolName: string, callId: string): ToolCallSegment | undefined {
  for (let i = segments.length - 1; i >= 0; --i) {
    const segment = segments[i]
    if (segment.kind !== 'tool_call') continue
    if (callId && segment.callId === callId) return segment
    if (!callId && sameToolName(segment.toolName, toolName) && (segment.status === 'pending' || segment.status === 'running')) return segment
  }
  for (let i = segments.length - 1; i >= 0; --i) {
    const segment = segments[i]
    if (segment.kind === 'tool_call' && sameToolName(segment.toolName, toolName)) return segment
  }
  return undefined
}

function mergeToolEvent(tool: ToolCallSegment, event: RuntimeEvent) {
  tool.events = [...(tool.events || []), compactEvent(event)]
  tool.payload = { ...(tool.payload || {}), ...boundedRecord(event.payload) }
  tool.summary = eventSummary(event) || tool.summary
  const command = stringFrom(event.payload?.command) || (event.type === 'tool_started' ? event.text : '')
  if (command) tool.command = command
  const cwd = stringFrom(event.payload?.cwd)
  if (cwd) tool.cwd = cwd
  const envelope = objectFrom(event.payload?.envelope)
  if (envelope) {
    tool.envelope = boundedRecord(envelope)
    const envelopeArtifacts = arrayFrom(envelope.artifacts) as RuntimeArtifact[]
    if (envelopeArtifacts.length) tool.artifacts = mergeArtifacts(tool.artifacts || [], envelopeArtifacts)
    const recoveryHints = arrayFrom(envelope.recovery_hints)
    if (recoveryHints.length) tool.recoveryHints = recoveryHints
    const envelopeError = envelope.error
    if (envelopeError) tool.error = typeof envelopeError === 'string' ? envelopeError : jsonPreview(envelopeError)
  }
  const payloadError = event.error || event.payload?.error
  if (payloadError) tool.error = typeof payloadError === 'string' ? payloadError : jsonPreview(payloadError)
  const recoveryHints = arrayFrom(event.payload?.recovery_hints)
  if (recoveryHints.length) tool.recoveryHints = recoveryHints
}

function appendOutputChunk(tool: ToolCallSegment, stream: string, text: string) {
  if (!text) return
  const tail = tool.outputs[tool.outputs.length - 1]
  if (tail?.stream === stream) tail.text += text
  else tool.outputs.push({ stream, text })
  trimOutputStream(tool, stream)
}

function trimOutputStream(tool: ToolCallSegment, stream: string) {
  let total = tool.outputs.reduce((sum, chunk) => sum + (chunk.stream === stream ? chunk.text.length : 0), 0)
  if (total <= TOOL_OUTPUT_LIMIT_CHARS) return

  const truncation = ensureTruncation(tool, stream)
  for (let i = 0; i < tool.outputs.length && total > TOOL_OUTPUT_LIMIT_CHARS; ++i) {
    const chunk = tool.outputs[i]
    if (chunk.stream !== stream) continue
    const remove = Math.min(chunk.text.length, total - TOOL_OUTPUT_LIMIT_CHARS)
    chunk.text = chunk.text.slice(remove)
    total -= remove
    truncation.omittedChars += remove
  }
  tool.outputs = tool.outputs.filter((chunk) => chunk.text.length > 0)
}

function ensureTruncation(tool: ToolCallSegment, stream: string): ToolOutputTruncation {
  if (!tool.outputTruncation) tool.outputTruncation = {}
  if (!tool.outputTruncation[stream]) {
    tool.outputTruncation[stream] = {
      truncated: true,
      omittedChars: 0,
      limitChars: TOOL_OUTPUT_LIMIT_CHARS,
    }
  }
  tool.outputTruncation[stream].truncated = true
  return tool.outputTruncation[stream]
}

function statusFromFinishedEvent(event: RuntimeEvent): ToolCallStatus {
  const interrupted = Boolean(event.payload?.interrupted)
  if (interrupted) return 'interrupted'
  const exitCode = numberFrom(event.payload?.exit_code)
  if (exitCode !== undefined && exitCode !== 0) return 'failed'
  const envelope = objectFrom(event.payload?.envelope)
  if (envelope && envelope.ok === false) return 'failed'
  if (event.error || event.payload?.error) return 'failed'
  return 'completed'
}

function runtimeSegmentFromEvent(event: RuntimeEvent): RuntimeEventSegment {
  return {
    id: segmentId('event'),
    kind: 'runtime_event',
    eventType: event.type,
    label: eventLabel(event),
    summary: eventSummary(event),
    event: compactEvent(event),
    payload: boundedRecord(event.payload),
    createdAt: new Date().toISOString(),
  }
}

function artifactsFromEvent(event: RuntimeEvent): RuntimeArtifact[] {
  const direct = arrayFrom(event.payload?.artifacts) as RuntimeArtifact[]
  const envelope = objectFrom(event.payload?.envelope)
  const envelopeArtifacts = arrayFrom(envelope?.artifacts) as RuntimeArtifact[]
  return mergeArtifacts(direct, envelopeArtifacts)
}

function mergeArtifacts(existing: RuntimeArtifact[], next: RuntimeArtifact[]): RuntimeArtifact[] {
  const seen = new Set<string>()
  const merged: RuntimeArtifact[] = []
  for (const artifact of [...existing, ...next]) {
    const key = String(artifact.normalized_path || artifact.path || artifact.label || JSON.stringify(artifact))
    if (seen.has(key)) continue
    seen.add(key)
    merged.push(artifact)
  }
  return merged
}

function toolNameFromEvent(event: RuntimeEvent): string {
  return stringFrom(event.payload?.tool_name) || event.name || stringFrom(event.payload?.name) || 'tool'
}

function toolCallIdFromEvent(event: RuntimeEvent): string {
  const state = objectFrom(event.state)
  return stringFrom(event.payload?.tool_call_id) || stringFrom(event.payload?.call_id) || stringFrom(state?.pending_tool_call_id) || ''
}

function sameToolName(left: string, right: string): boolean {
  if (!left || !right) return true
  return left === right
}

function eventSummary(event: RuntimeEvent): string {
  return event.text || stringFrom(event.payload?.summary) || event.name || event.type
}

function eventLabel(event: RuntimeEvent): string {
  const labels: Record<string, string> = {
    task_started: '任务开始',
    plan_created: '计划',
    tool_started: '工具开始',
    tool_output: '工具输出',
    tool_finished: '工具完成',
    recovering: '恢复中',
    skill_loading: '加载 Skill',
    skill_running: '运行 Skill',
    artifact_ready: '产物就绪',
    task_completed: '任务完成',
    task_failed: '任务失败',
  }
  return labels[event.type] || event.type
}

function stringFrom(value: unknown): string {
  return typeof value === 'string' ? value : ''
}

function numberFrom(value: unknown): number | undefined {
  if (typeof value === 'number' && Number.isFinite(value)) return value
  if (typeof value === 'string' && value.trim()) {
    const parsed = Number(value)
    if (Number.isFinite(parsed)) return parsed
  }
  return undefined
}

function objectFrom(value: unknown): Record<string, unknown> | undefined {
  return value && typeof value === 'object' && !Array.isArray(value) ? (value as Record<string, unknown>) : undefined
}

function arrayFrom(value: unknown): unknown[] {
  return Array.isArray(value) ? value : []
}

function compactEvent(event: RuntimeEvent): RuntimeEvent {
  return {
    ...event,
    text: boundedString(event.text),
    error: boundedString(event.error),
    payload: boundedRecord(event.payload),
  }
}

function boundedRecord(value: unknown): Record<string, unknown> | undefined {
  const object = objectFrom(value)
  return object ? (boundedValue(object, 0) as Record<string, unknown>) : undefined
}

function boundedValue(value: unknown, depth: number): unknown {
  if (typeof value === 'string') return boundedString(value)
  if (typeof value !== 'object' || value === null) return value
  if (depth >= STRUCTURED_DEPTH_LIMIT) return '[truncated: depth limit]'
  if (Array.isArray(value)) {
    const items = value.slice(0, STRUCTURED_ARRAY_LIMIT).map((item) => boundedValue(item, depth + 1))
    if (value.length > STRUCTURED_ARRAY_LIMIT) items.push(`[truncated: ${value.length - STRUCTURED_ARRAY_LIMIT} more items]`)
    return items
  }
  const result: Record<string, unknown> = {}
  for (const [key, item] of Object.entries(value as Record<string, unknown>)) {
    result[key] = boundedValue(item, depth + 1)
  }
  return result
}

function boundedString(value: string | undefined): string {
  if (!value || value.length <= STRUCTURED_TEXT_LIMIT_CHARS) return value || ''
  const omitted = value.length - STRUCTURED_TEXT_LIMIT_CHARS
  return `${value.slice(0, STRUCTURED_TEXT_LIMIT_CHARS)}\n...[truncated ${omitted.toLocaleString()} chars]`
}

function jsonPreview(value: unknown): string {
  try {
    return JSON.stringify(value, null, 2)
  } catch {
    return String(value)
  }
}
