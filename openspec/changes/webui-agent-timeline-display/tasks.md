## 1. Timeline Data Model

- [x] 1.1 Extend `webui/src/types.ts` with `ChatSegment`, `ToolCallSegment`, tool stream output, artifact, and truncation metadata types.
- [x] 1.2 Add front-end helpers for appending or merging adjacent timeline segments while preserving cross-type order.
- [x] 1.3 Add tool event matching logic that associates `tool_output`, `tool_finished`, and `artifact_ready` with the most recent compatible unfinished tool segment.
- [x] 1.4 Add bounded retention for stdout/stderr and large structured payloads, including a visible truncation marker.
- [x] 1.5 Keep legacy `content`, `reasoning`, `toolSteps`, and `runtimeEvents` fields populated for history, copy, retry, and old session compatibility.

## 2. Streaming Ingestion

- [x] 2.1 Update `webui/src/api.ts` to surface typed stream parts for reasoning, content, EVA tool markers, EVA runtime events, stats, and final reconciliation.
- [x] 2.2 Update `webui/src/store.ts` to build assistant `segments` incrementally during streaming while continuing to update legacy aggregate fields.
- [x] 2.3 Stop dropping `tool_output` events; route them into the active tool segment with stream labels and bounded storage.
- [x] 2.4 Handle `eva_final` reconciliation without reordering already displayed tool and reasoning segments.
- [x] 2.5 Ensure aborted, failed, and empty responses produce coherent timeline error or status segments.

## 3. Timeline Rendering

- [x] 3.1 Update `MessageItem.vue` to render `message.segments` when present and fall back to the current legacy layout otherwise.
- [x] 3.2 Render thinking segments as independent collapsible rows with sensible streaming and completed defaults.
- [x] 3.3 Render answer segments with the existing markdown/codeblock pipeline at their timeline positions.
- [x] 3.4 Render tool call segments as expandable rows with compact summary, status, command/cwd, stdout/stderr, envelope, error, recovery hints, and artifacts.
- [x] 3.5 Render standalone runtime events, artifacts, and errors when they cannot be associated with a tool segment.
- [x] 3.6 Adjust `ChatThread.vue` scroll-follow behavior so updates inside timeline segments keep streaming near the bottom without jumping during manual inspection.
- [x] 3.7 Add responsive CSS for timeline rows, compact mobile display, long path wrapping, and large output panels.

## 4. ACP Event Completeness

- [x] 4.1 Audit current direct-runtime and bridge-mode `delta.eva_event` payloads for tool name, command, cwd, stream, exit code, envelope, artifacts, and errors.
- [x] 4.2 If required fields are missing for common tool calls, add minimal EVA-specific event payload fields in `src/acp_runtime.cpp` or runtime event producers without changing standard OpenAI fields.
- [x] 4.3 Prefer stable tool call ids if already available; otherwise document and implement latest-compatible-tool matching.

## 5. Validation

- [x] 5.1 Verify a simulated or real stream preserves the actual received order, including but not limited to `思考 -> 回复输出 -> 思考 -> 工具调用 -> 工具输出 -> 思考 -> 回复输出`.
- [x] 5.2 Verify failed tools expose error details, stderr, exit code or envelope error, and recovery hints when present.
- [x] 5.3 Verify long stdout/stderr is truncated in localStorage but remains understandable in the expanded UI.
- [x] 5.4 Verify old locally stored sessions without `segments` still render and can be retried/copied.
- [x] 5.5 Run `cd webui && npm run build`.
- [x] 5.6 Run relevant OpenSpec validation for `webui-agent-timeline-display`.
- [x] 5.7 After implementation, run `python scripts/update_feature_log.py` with a concise implementation summary.
