## ADDED Requirements

### Requirement: Timeline preserves assistant turn event order

The WebUI SHALL render each assistant turn as an ordered timeline of stream segments that preserves the arrival order of reasoning, assistant content, EVA runtime events, tool calls, tool outputs, artifacts, and errors.

#### Scenario: Mixed stream parts arrive in one turn

- **WHEN** the browser receives stream chunks in the order `reasoning`, `content`, `reasoning`, `tool_started`, `tool_output`, `tool_finished`, `reasoning`, `content`
- **THEN** the assistant message MUST display those segments in the same relative order.
- **AND** the UI MUST NOT move all reasoning to the top or all tool events above the final answer.

#### Scenario: A different valid stream order arrives

- **WHEN** the browser receives reasoning, content, tool, artifact, status, or error chunks in any other valid order
- **THEN** the assistant message MUST display the segments in that actual received order.
- **AND** the WebUI MUST NOT force the turn into a fixed reasoning-answer-tool-answer template.

#### Scenario: Adjacent chunks of the same visible type arrive

- **WHEN** two or more adjacent stream chunks have the same segment type and belong to the same active segment
- **THEN** the WebUI MAY merge them into one rendered segment.
- **AND** the merge MUST NOT cross over a different segment type such as a tool call or answer segment.

### Requirement: Timeline renders thinking segments as collapsible reasoning

The WebUI SHALL render reasoning content as `thinking` timeline segments that can be expanded and collapsed independently from answer text.

#### Scenario: Thinking is streaming before answer text

- **WHEN** an assistant message is pending and reasoning text is the active segment
- **THEN** the active thinking segment MUST remain visible enough for the user to see that reasoning is being produced.

#### Scenario: Thinking completes before or after answer text

- **WHEN** a thinking segment is no longer active
- **THEN** the UI MUST allow the user to collapse or expand that specific thinking segment without hiding answer or tool segments.

### Requirement: Timeline renders answer output in separate ordered segments

The WebUI SHALL render assistant answer content as markdown-capable `answer` timeline segments at the positions where the answer text was produced.

#### Scenario: Assistant produces answer text before a tool call

- **WHEN** answer content arrives before a later tool call in the same assistant turn
- **THEN** the answer content MUST remain above that tool call in the rendered timeline.

#### Scenario: Assistant resumes answer text after a tool call

- **WHEN** additional answer content arrives after a tool call finishes
- **THEN** the later answer content MUST render as a later answer segment after the tool call details.

### Requirement: Timeline renders tool calls as expandable detail rows

The WebUI SHALL render each tool invocation as an expandable `tool_call` segment with a compact summary and detailed execution information.

#### Scenario: Tool starts

- **WHEN** a `tool_started` runtime event or equivalent EVA tool marker arrives
- **THEN** the timeline MUST create or update a tool call segment showing the tool name and active status.

#### Scenario: Tool outputs stdout or stderr

- **WHEN** `tool_output` events arrive for a tool call
- **THEN** the WebUI MUST preserve the output stream type when available.
- **AND** the expanded tool details MUST show stdout and stderr content separately or label each chunk clearly.

#### Scenario: Tool finishes successfully

- **WHEN** a `tool_finished` event indicates successful completion or includes a successful envelope
- **THEN** the collapsed tool row MUST show a completed state and a concise summary.
- **AND** the expanded details MUST expose available result metadata such as exit code, envelope summary, returned artifacts, command, working directory, and structured payload.

#### Scenario: Tool fails or reports recovery hints

- **WHEN** a tool event includes failure, nonzero exit code, interrupted execution, error payload, or recovery hints
- **THEN** the collapsed tool row MUST show an error or warning state.
- **AND** the expanded details MUST expose the error and recovery information instead of hiding it from the conversation.

### Requirement: Timeline renders artifacts and runtime status without losing context

The WebUI SHALL display artifact and runtime status events in the same assistant turn timeline and associate them with the most relevant tool segment when possible.

#### Scenario: Artifact belongs to a recent tool

- **WHEN** an `artifact_ready` event references the same tool name as a recent tool call
- **THEN** the artifact MUST be shown inside or immediately after that tool call segment.

#### Scenario: Runtime event has no tool association

- **WHEN** a runtime event such as `plan_created`, `recovering`, `task_completed`, or `task_failed` cannot be associated with a tool call
- **THEN** the WebUI MUST render it as a standalone timeline status segment.

### Requirement: Timeline keeps OpenAI-compatible transport semantics

The WebUI and ACP streaming transport SHALL keep standard OpenAI-compatible fields usable while adding EVA-specific timeline information only through EVA-specific fields or front-end interpretation.

#### Scenario: Standard streaming content arrives

- **WHEN** a streaming response uses only standard `delta.content`, `delta.reasoning`, or `delta.reasoning_content`
- **THEN** the WebUI MUST still build answer and thinking timeline segments without requiring EVA-specific fields.

#### Scenario: EVA runtime event arrives

- **WHEN** a streaming response includes `delta.eva_event` or `delta.eva_tool`
- **THEN** the WebUI MUST use those fields to enrich the timeline.
- **AND** this MUST NOT change the meaning of standard OpenAI response fields.

### Requirement: Timeline is backward compatible and storage bounded

The WebUI SHALL preserve readability of existing locally stored sessions and MUST bound large tool output stored in browser localStorage.

#### Scenario: Existing message has no timeline segments

- **WHEN** a locally stored assistant message contains legacy `content`, `reasoning`, `toolSteps`, or `runtimeEvents` fields but no timeline segments
- **THEN** the WebUI MUST still render the message coherently.

#### Scenario: Tool output is large

- **WHEN** tool stdout, stderr, or structured payload exceeds the configured front-end retention limit
- **THEN** the WebUI MUST truncate or summarize the saved output.
- **AND** the expanded tool details MUST indicate that truncation occurred.
