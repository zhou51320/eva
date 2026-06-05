<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type { ChatMessage, ChatSegment, RuntimeArtifact, RuntimeEvent, ToolCallSegment } from '../types'
import { renderMarkdown } from '../markdown'

const props = defineProps<{ message: ChatMessage }>()
const emit = defineEmits<{ retry: [] }>()

const hasTimeline = computed(() => Boolean(props.message.segments && props.message.segments.length))

const visibleRuntimeEvents = computed(() => (props.message.runtimeEvents || []).filter((event) => {
  return ['task_started', 'plan_created', 'tool_started', 'tool_finished', 'recovering', 'skill_loading', 'skill_running', 'artifact_ready', 'task_completed', 'task_failed'].includes(event.type)
}).slice(-8))

const runtimeArtifacts = computed(() => {
  const artifacts: RuntimeArtifact[] = []
  for (const event of props.message.runtimeEvents || []) {
    if (event.type !== 'artifact_ready') continue
    const direct = event.payload?.artifacts
    if (Array.isArray(direct)) artifacts.push(...direct)
    const envelopeArtifacts = event.payload?.envelope?.artifacts
    if (Array.isArray(envelopeArtifacts)) artifacts.push(...(envelopeArtifacts as RuntimeArtifact[]))
  }
  return artifacts.slice(-6)
})

function eventLabel(event: RuntimeEvent): string {
  const labels: Record<string, string> = {
    task_started: '任务开始',
    plan_created: '计划',
    tool_started: '工具开始',
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

function eventSummary(event: RuntimeEvent): string {
  return event.text || event.payload?.summary || event.name || event.type
}

function artifactName(artifact: RuntimeArtifact): string {
  return artifact.label || artifact.path || artifact.normalized_path || 'artifact'
}

function artifactMeta(artifact: RuntimeArtifact): string {
  const parts = []
  const ext = artifact.extension || artifact.type
  if (ext) parts.push(String(ext))
  if (artifact.size !== undefined && artifact.size !== null && artifact.size !== '') parts.push(`${artifact.size} B`)
  return parts.join(' · ')
}

const reasoningOpen = ref(true)
const timelineOpen = ref<Record<string, boolean>>({})
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

function segmentKey(segment: ChatSegment, index: number): string {
  return segment.id || `${segment.kind}-${index}`
}

function defaultSegmentOpen(segment: ChatSegment): boolean {
  if (segment.kind === 'thinking') return Boolean(segment.active && props.message.pending)
  if (segment.kind === 'tool_call') return segment.status === 'failed' || segment.status === 'warning' || segment.status === 'interrupted'
  if (segment.kind === 'error') return true
  return false
}

function isSegmentOpen(segment: ChatSegment, index: number): boolean {
  const key = segmentKey(segment, index)
  return timelineOpen.value[key] ?? defaultSegmentOpen(segment)
}

function onSegmentToggle(segment: ChatSegment, index: number, event: Event) {
  timelineOpen.value[segmentKey(segment, index)] = (event.currentTarget as HTMLDetailsElement).open
}

function isThinking(segment: ChatSegment): boolean {
  return segment.kind === 'thinking'
}

function isAnswer(segment: ChatSegment): boolean {
  return segment.kind === 'answer'
}

function isTool(segment: ChatSegment): segment is ToolCallSegment {
  return segment.kind === 'tool_call'
}

function isRuntime(segment: ChatSegment): boolean {
  return segment.kind === 'runtime_event'
}

function isArtifact(segment: ChatSegment): boolean {
  return segment.kind === 'artifact'
}

function isError(segment: ChatSegment): boolean {
  return segment.kind === 'error'
}

function segmentText(segment: ChatSegment): string {
  return segment.kind === 'thinking' || segment.kind === 'answer' ? segment.text : ''
}

function toolStatusLabel(status: string): string {
  const labels: Record<string, string> = {
    pending: '等待',
    running: '运行中',
    completed: '完成',
    failed: '错误',
    warning: '注意',
    interrupted: '已中断',
  }
  return labels[status] || status
}

function toolSummary(segment: ChatSegment): string {
  if (!isTool(segment)) return ''
  if (segment.summary) return segment.summary
  const outputCount = segment.outputs?.length || 0
  if (outputCount > 0) return `${outputCount} 段输出`
  return segment.command || segment.toolName
}

function toolOutputs(segment: ChatSegment) {
  return isTool(segment) ? segment.outputs || [] : []
}

function toolArtifacts(segment: ChatSegment): RuntimeArtifact[] {
  return isTool(segment) ? segment.artifacts || [] : []
}

function segmentArtifacts(segment: ChatSegment): RuntimeArtifact[] {
  return segment.kind === 'artifact' ? segment.artifacts : []
}

function runtimeLabel(segment: ChatSegment): string {
  return segment.kind === 'runtime_event' ? segment.label : ''
}

function runtimeSummary(segment: ChatSegment): string {
  if (segment.kind === 'runtime_event') return segment.summary
  if (segment.kind === 'artifact') return segment.summary || '产物就绪'
  if (segment.kind === 'error') return segment.summary
  return ''
}

function runtimePayload(segment: ChatSegment): Record<string, unknown> | undefined {
  if (segment.kind === 'runtime_event') return segment.payload || segment.event?.payload
  if (segment.kind === 'error') return segment.payload || segment.event?.payload
  return undefined
}

function errorDetails(segment: ChatSegment): string {
  return segment.kind === 'error' ? segment.details || '' : ''
}

function truncationText(segment: ChatSegment): string[] {
  if (!isTool(segment) || !segment.outputTruncation) return []
  return Object.entries(segment.outputTruncation)
    .filter(([, meta]) => meta.truncated)
    .map(([stream, meta]) => `${stream} 已截断，省略 ${meta.omittedChars.toLocaleString()} 字符，当前保留 ${meta.limitChars.toLocaleString()} 字符。`)
}

function formatJson(value: unknown): string {
  try {
    return JSON.stringify(value, null, 2)
  } catch {
    return String(value)
  }
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
        <div v-if="hasTimeline" class="timeline">
          <template v-for="(segment, i) in message.segments" :key="segmentKey(segment, i)">
            <details
              v-if="isThinking(segment)"
              class="timeline-segment timeline-thinking"
              :open="isSegmentOpen(segment, i)"
              @toggle="(event) => onSegmentToggle(segment, i, event)"
            >
              <summary>
                <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 18h6M10 22h4M12 2a7 7 0 0 0-4 12.7V17h8v-2.3A7 7 0 0 0 12 2z" /></svg>
                思考
                <span v-if="segment.active" class="timeline-badge">生成中</span>
              </summary>
              <div class="timeline-thinking__body">{{ segmentText(segment) }}</div>
            </details>

            <div
              v-else-if="isAnswer(segment)"
              class="md body timeline-answer"
              :class="{ 'body--err': message.error }"
              v-html="renderMarkdown(segmentText(segment))"
              @click="onBodyClick"
            ></div>

            <details
              v-else-if="isTool(segment)"
              class="timeline-segment timeline-tool"
              :class="`timeline-tool--${segment.status}`"
              :open="isSegmentOpen(segment, i)"
              @toggle="(event) => onSegmentToggle(segment, i, event)"
            >
              <summary>
                <span class="tool-icon">
                  <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M14.7 6.3a4 4 0 0 0-5.4 5.4l-6 6a1.5 1.5 0 0 0 2 2l6-6a4 4 0 0 0 5.4-5.4l-2.3 2.3-2-2 2.3-2.3z" /></svg>
                </span>
                <span class="timeline-tool__name">{{ segment.toolName }}</span>
                <span class="timeline-tool__status">{{ toolStatusLabel(segment.status) }}</span>
                <span class="timeline-tool__summary">{{ toolSummary(segment) }}</span>
              </summary>
              <div class="timeline-tool__body">
                <div v-if="segment.command" class="tool-detail">
                  <div class="tool-detail__label">命令</div>
                  <pre class="tool-pre">{{ segment.command }}</pre>
                </div>
                <div v-if="segment.cwd" class="tool-detail">
                  <div class="tool-detail__label">工作目录</div>
                  <code class="tool-inline">{{ segment.cwd }}</code>
                </div>
                <div v-if="toolOutputs(segment).length" class="tool-detail">
                  <div class="tool-detail__label">输出</div>
                  <div class="tool-outputs">
                    <div v-for="(output, outputIndex) in toolOutputs(segment)" :key="outputIndex" class="tool-output">
                      <div class="tool-output__stream">{{ output.stream }}</div>
                      <pre class="tool-pre">{{ output.text }}</pre>
                    </div>
                  </div>
                  <div v-for="line in truncationText(segment)" :key="line" class="tool-truncation">{{ line }}</div>
                </div>
                <div v-if="segment.envelope" class="tool-detail">
                  <div class="tool-detail__label">结果 envelope</div>
                  <pre class="tool-pre">{{ formatJson(segment.envelope) }}</pre>
                </div>
                <div v-if="segment.error" class="tool-detail tool-detail--error">
                  <div class="tool-detail__label">错误</div>
                  <pre class="tool-pre">{{ segment.error }}</pre>
                </div>
                <div v-if="segment.recoveryHints && segment.recoveryHints.length" class="tool-detail">
                  <div class="tool-detail__label">恢复建议</div>
                  <pre class="tool-pre">{{ formatJson(segment.recoveryHints) }}</pre>
                </div>
                <div v-if="toolArtifacts(segment).length" class="artifact-cards artifact-cards--inline">
                  <div v-for="(artifact, artifactIndex) in toolArtifacts(segment)" :key="artifactIndex" class="artifact-card">
                    <div class="artifact-card__title">{{ artifactName(artifact) }}</div>
                    <div class="artifact-card__meta">{{ artifactMeta(artifact) }}</div>
                    <div v-if="artifact.normalized_path || artifact.path" class="artifact-card__path">{{ artifact.normalized_path || artifact.path }}</div>
                  </div>
                </div>
              </div>
            </details>

            <div
              v-else-if="isRuntime(segment)"
              class="timeline-segment timeline-event"
              :class="`timeline-event--${runtimeSummary(segment) ? 'with-summary' : 'empty'}`"
            >
              <span class="runtime-event__label">{{ runtimeLabel(segment) }}</span>
              <span class="runtime-event__summary">{{ runtimeSummary(segment) }}</span>
              <pre v-if="runtimePayload(segment)" class="timeline-json">{{ formatJson(runtimePayload(segment)) }}</pre>
            </div>

            <div v-else-if="isArtifact(segment)" class="artifact-cards">
              <div v-for="(artifact, artifactIndex) in segmentArtifacts(segment)" :key="artifactIndex" class="artifact-card">
                <div class="artifact-card__title">{{ artifactName(artifact) }}</div>
                <div class="artifact-card__meta">{{ artifactMeta(artifact) }}</div>
                <div v-if="artifact.normalized_path || artifact.path" class="artifact-card__path">{{ artifact.normalized_path || artifact.path }}</div>
              </div>
            </div>

            <details
              v-else-if="isError(segment)"
              class="timeline-segment timeline-error"
              :open="isSegmentOpen(segment, i)"
              @toggle="(event) => onSegmentToggle(segment, i, event)"
            >
              <summary>{{ runtimeSummary(segment) }}</summary>
              <pre v-if="errorDetails(segment)" class="tool-pre">{{ errorDetails(segment) }}</pre>
              <pre v-if="runtimePayload(segment)" class="tool-pre">{{ formatJson(runtimePayload(segment)) }}</pre>
            </details>
          </template>
          <div v-if="message.pending && (!message.segments || message.segments.length === 0)" class="body">
            <span class="cursor"></span>
          </div>
        </div>

        <template v-else>
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

          <div v-if="visibleRuntimeEvents.length" class="runtime-events">
            <div v-for="(event, i) in visibleRuntimeEvents" :key="i" class="runtime-event" :class="`runtime-event--${event.type}`">
              <span class="runtime-event__label">{{ eventLabel(event) }}</span>
              <span class="runtime-event__summary">{{ eventSummary(event) }}</span>
            </div>
          </div>

          <div v-if="runtimeArtifacts.length" class="artifact-cards">
            <div v-for="(artifact, i) in runtimeArtifacts" :key="i" class="artifact-card">
              <div class="artifact-card__title">{{ artifactName(artifact) }}</div>
              <div class="artifact-card__meta">{{ artifactMeta(artifact) }}</div>
              <div v-if="artifact.normalized_path || artifact.path" class="artifact-card__path">{{ artifact.normalized_path || artifact.path }}</div>
            </div>
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
        </template>

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

.timeline {
  display: grid;
  gap: 10px;
}

.timeline-segment {
  min-width: 0;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: var(--surface);
  overflow: hidden;
}

.timeline-segment summary {
  display: flex;
  align-items: center;
  gap: 8px;
  min-width: 0;
  padding: 8px 11px;
  cursor: pointer;
  user-select: none;
  color: var(--text-muted);
  font-size: 12.5px;
}

.timeline-segment summary::-webkit-details-marker {
  display: none;
}

.timeline-badge,
.timeline-tool__status {
  flex: none;
  border-radius: 999px;
  padding: 1px 7px;
  background: var(--surface-2);
  color: var(--text-faint);
  font-size: 11px;
  line-height: 18px;
}

.timeline-thinking {
  background: var(--surface);
}

.timeline-thinking__body {
  padding: 0 13px 11px;
  border-top: 1px solid var(--border);
  padding-top: 9px;
  color: var(--text-muted);
  font-size: 13px;
  line-height: 1.65;
  white-space: pre-wrap;
}

.timeline-answer {
  min-width: 0;
}

.timeline-tool {
  border-color: var(--border-strong);
}

.timeline-tool--completed {
  border-color: rgba(74, 222, 128, 0.35);
}

.timeline-tool--failed,
.timeline-tool--interrupted {
  border-color: rgba(255, 107, 107, 0.38);
}

.timeline-tool--warning {
  border-color: rgba(251, 191, 36, 0.4);
}

.tool-icon {
  flex: none;
  display: inline-flex;
  color: var(--accent-text);
}

.timeline-tool__name {
  flex: none;
  color: var(--text);
  font-weight: 600;
}

.timeline-tool__summary {
  min-width: 0;
  color: var(--text-muted);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.timeline-tool__body {
  display: grid;
  gap: 10px;
  padding: 10px 11px 12px;
  border-top: 1px solid var(--border);
}

.tool-detail {
  min-width: 0;
  display: grid;
  gap: 5px;
}

.tool-detail--error .tool-pre {
  color: var(--danger);
}

.tool-detail__label {
  color: var(--text-faint);
  font-size: 11.5px;
}

.tool-inline {
  display: block;
  width: 100%;
  padding: 6px 8px;
  border-radius: 6px;
  background: var(--surface-2);
  color: var(--text-muted);
  font-family: var(--font-mono);
  font-size: 12px;
  white-space: normal;
  overflow-wrap: anywhere;
}

.tool-pre,
.timeline-json {
  margin: 0;
  max-height: 260px;
  overflow: auto;
  padding: 9px 10px;
  border-radius: 6px;
  background: var(--surface-2);
  color: var(--text-muted);
  font-family: var(--font-mono);
  font-size: 12px;
  line-height: 1.5;
  white-space: pre-wrap;
  overflow-wrap: anywhere;
}

.tool-outputs {
  display: grid;
  gap: 8px;
}

.tool-output {
  min-width: 0;
  border: 1px solid var(--border);
  border-radius: 6px;
  overflow: hidden;
}

.tool-output__stream {
  padding: 5px 8px;
  border-bottom: 1px solid var(--border);
  background: var(--surface-2);
  color: var(--accent-text);
  font-family: var(--font-mono);
  font-size: 11px;
}

.tool-output .tool-pre {
  border-radius: 0;
  background: transparent;
}

.tool-truncation {
  color: var(--warn);
  font-size: 12px;
}

.timeline-event {
  display: grid;
  grid-template-columns: auto minmax(0, 1fr);
  gap: 6px 8px;
  padding: 8px 10px;
  font-size: 12.5px;
}

.timeline-event .timeline-json {
  grid-column: 1 / -1;
  margin-top: 2px;
}

.timeline-error {
  border-color: rgba(255, 107, 107, 0.38);
}

.timeline-error summary {
  color: var(--danger);
  font-weight: 600;
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

.runtime-events {
  display: grid;
  gap: 6px;
  margin-bottom: 10px;
}
.runtime-event {
  display: flex;
  gap: 8px;
  align-items: baseline;
  padding: 7px 10px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: var(--surface);
  font-size: 12.5px;
}
.runtime-event--recovering,
.runtime-event--task_failed {
  border-color: rgba(255, 107, 107, 0.35);
}
.runtime-event--artifact_ready,
.runtime-event--task_completed {
  border-color: rgba(74, 222, 128, 0.35);
}
.runtime-event__label {
  flex: none;
  color: var(--accent-text);
  font-weight: 600;
}
.runtime-event__summary {
  min-width: 0;
  color: var(--text-muted);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.artifact-cards {
  display: grid;
  gap: 8px;
  margin-bottom: 10px;
}
.artifact-cards--inline {
  margin-bottom: 0;
}
.artifact-card {
  padding: 10px 12px;
  border: 1px solid rgba(74, 222, 128, 0.35);
  border-radius: var(--radius-sm);
  background: var(--surface);
}
.artifact-card__title {
  color: var(--text);
  font-size: 13px;
  font-weight: 600;
}
.artifact-card__meta,
.artifact-card__path {
  margin-top: 4px;
  color: var(--text-faint);
  font-size: 12px;
  word-break: break-all;
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

@media (max-width: 640px) {
  .msg {
    gap: 8px;
  }
  .avatar {
    width: 26px;
    height: 26px;
    border-radius: 8px;
    font-size: 12px;
  }
  .user-col {
    max-width: 92%;
  }
  .timeline-segment summary {
    flex-wrap: wrap;
    gap: 6px;
  }
  .timeline-tool__summary {
    flex-basis: 100%;
    white-space: normal;
  }
  .timeline-event {
    grid-template-columns: 1fr;
  }
  .tool-pre,
  .timeline-json {
    max-height: 220px;
  }
}
</style>
