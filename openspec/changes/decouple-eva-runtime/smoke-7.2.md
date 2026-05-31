# 7.2 Windowed EVA Smoke Checklist

This checklist is the target-environment evidence needed before marking
`tasks.md` item 7.2 complete. Run it on the machine where the windowed EVA
binary, backend bundle, models, Qt plugins, knowledge DB, and MCP services
are actually available.

Do not mark 7.2 complete from a build-only result. The item requires real
runtime behavior evidence for every row below.

## Required Setup

- Build and run the windowed `eva` binary.
- Run `eva_acp` against the windowed app bridge:
  - `EVA_ACP_RUNTIME_MODE=bridge eva_acp --host 127.0.0.1 --port 19070`
- Confirm `GET http://127.0.0.1:19070/api/backend/state` returns:
  - `state_source: "bridge"`
  - `bridge_available: true`
  - `chat_route: "eva_bridge"`
  - `capabilities.full_eva_stack: true`
  - `capabilities.conversation_owner: "widget"`
  - `capabilities.message_input_mode: "latest_user_text"`
  - `capabilities.stop: true`
  - `capabilities.stream: true`
  - `capabilities.reset: true`

You can use:

```bash
python3 scripts/smoke_acp_runtime.py --base-url http://127.0.0.1:19070 --expect-source bridge
```

## Evidence Matrix

| Item | Evidence required | Suggested check |
| --- | --- | --- |
| Local load | Windowed EVA loads a local GGUF through ACP bridge and reaches ready/running backend state. | `python3 scripts/smoke_acp_runtime.py --expect-source bridge --load-local-model /path/to/model.gguf --backend auto --wait-ready 90` |
| Link mode | Windowed EVA switches to remote OpenAI-compatible endpoint through ACP bridge. | `python3 scripts/smoke_acp_runtime.py --expect-source bridge --load-link-endpoint http://127.0.0.1:PORT --load-link-model MODEL` |
| Send | Non-streaming chat returns assistant content. | `python3 scripts/smoke_acp_runtime.py --message "ping"` |
| Stream | Streaming chat emits chunks and `[DONE]`. | `python3 scripts/smoke_acp_runtime.py --message "ping" --stream` |
| Stop | Active turn can be cancelled through ACP. | `python3 scripts/smoke_acp_runtime.py --message "write a long response" --stop-during-stream --stop-delay 2` |
| Reset | Conversation reset clears/starts a fresh runtime conversation. | `python3 scripts/smoke_acp_runtime.py --reset`, then state/message snapshot confirms reset. |
| Tools | A tool-enabled turn emits tool lifecycle/result evidence. | Enable calculator or engineer tool in the EVA convention window, ask for a tool call, confirm records/events/output. |
| Knowledge | Knowledge tool can query configured knowledge DB. | Enable knowledge, ask a query known to match indexed text, confirm tool response and final answer. |
| MCP | MCP tool list/call path works. | Enable MCP, refresh services, call a known MCP tool, confirm tool response and final answer. |

## Direct Runtime Regression

The same helper can also guard the no-window path that should continue working.
Direct runtime is currently a headless model runtime path; until `xTool`, MCP,
knowledge, and TTS are fully hosted by `EvaRuntime`, full tool/skill behavior
requires the windowed EVA bridge route above. Direct state may report
`configured_tools_list`, but `enabled_tools` should stay empty and
`capabilities.full_eva_stack` should be `false`. Its conversation owner is
`acp_runtime`, while bridge mode's conversation owner is `widget`.

```bash
python3 scripts/smoke_acp_runtime.py --base-url http://127.0.0.1:19070 --expect-source direct_runtime --message "ping" --stream
```

## Completion Rule

Only check `7.2` after all rows have direct evidence. If one row is blocked by
missing target resources, keep `7.2` unchecked and record the missing resource in
`docs/功能迭代.md`.
