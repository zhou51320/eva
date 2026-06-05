## 1. Prompt Protocol and Task Controller

- [x] 1.1 Locate system prompt construction for normal chat, engineer mode, tool usage, Skill injection, ACP bridge, and direct runtime paths.
- [x] 1.2 Define the EVA Agent Runtime 2.0 execution loop: understand → plan → inspect → act → observe → recover → verify → report.
- [x] 1.3 Add tool-use rules: prefer structured tools for routine file/path/artifact operations; shell is an escape hatch; every command needs cwd/timeout and clear purpose.
- [x] 1.4 Add completion rules: generation tasks require artifact confirmation; code tasks require build/test or explicit unverified blocker; failed/blocked tasks must not be reported as complete.
- [x] 1.5 Add recovery rules for repeated failures, path errors, dependency errors, syntax errors, missing artifacts, network/proxy failures, and unsupported platform cases.
- [x] 1.6 Add explicit Windows/Win7 guidance: avoid modern shell assumptions; handle spaces/non-ASCII paths; prefer portable/local runtimes; report TLS/proxy/runtime blockers clearly.
- [x] 1.7 Verify prompt changes are injected consistently in Qt Widget and ACP/WebUI bridge/direct runtime paths.

## 2. Tool Result Envelope v2

- [x] 2.1 Audit current tool schemas, tool result serialization, model observation formatting, and legacy parser/native tool-call paths.
- [x] 2.2 Define the canonical `ToolResultEnvelope` schema with `ok`, `summary`, `data`, `artifacts`, `warnings`, `error`, and `recovery_hints`.
- [x] 2.3 Add an adapter that wraps legacy tool results into envelope v2 without breaking existing callers.
- [x] 2.4 Add standardized error types for path, dependency, syntax, permission, timeout, network, artifact_missing, encoding, unsupported_platform, and unknown errors.
- [x] 2.5 Update command, file/path, Skill, and artifact-related tools to emit envelope v2 natively or through wrappers.
- [x] 2.6 Update model-visible tool descriptions so the model understands structured success/failure and recovery hints.
- [x] 2.7 Add regression coverage for envelope compatibility with old and new tool-call formats.

## 3. Command Runner / Execution Runtime v2

- [x] 3.1 Audit existing `execute_command`, PTC execution, cancellation, streaming output, and command UI event paths.
- [x] 3.2 Extend command request schema with cwd, env, timeout_ms, shell, expected_outputs, command label/summary, and cancellation identity.
- [x] 3.3 Return structured stdout, stderr, exit_code, duration, command id, truncated-output metadata, error type, and recovery hints.
- [x] 3.4 Implement repeated failed command detection so unchanged retries are flagged or discouraged.
- [x] 3.5 Add long-running command progress events without flooding chat content.
- [x] 3.6 Add Windows/Win7 command compatibility handling for cmd fallback, old PowerShell, encoding, quoting, spaces, and non-ASCII paths.
- [x] 3.7 Add proxy/network diagnostics for dependency commands and network failures.
- [x] 3.8 Verify command runner behavior for success, non-zero exit, timeout, cancel, missing executable, and encoding-heavy output.

## 4. Workspace and Artifact Manager

- [x] 4.1 Define allowed roots for workspace writes, installed Skill reads, temp run directories, dependency caches, and artifact outputs.
- [x] 4.2 Implement or expose path-safe stat/list/glob/search operations with normalized paths and Windows path support.
- [x] 4.3 Implement or expose path-safe copy/move operations for files/directories, including spaces and non-ASCII names.
- [x] 4.4 Implement artifact confirmation metadata: path, normalized path, size, type/extension, modified time, source tool, display label, and open/download hints.
- [x] 4.5 Add final answer gating for generation tasks so missing artifact confirmation prevents “done” claims.
- [x] 4.6 Add cleanup/retention policy for temp run directories, logs, and generated artifact metadata.
- [x] 4.7 Verify artifact confirmation with PPTX, image/report, and code-generated file scenarios.

## 5. Recovery Engine MVP

- [x] 5.1 Implement failure classification helpers for path, dependency, syntax, permission, timeout, network/proxy, artifact_missing, encoding, unsupported_platform, and unknown cases.
- [x] 5.2 Generate recovery hints from classified failures, including suggested structured tools or safe next checks.
- [x] 5.3 Prevent or warn on repeated identical failed commands/tool calls unless new evidence changed.
- [x] 5.4 For path failures, require stat/list/normalize/copy checks before retrying command execution.
- [x] 5.5 For dependency failures, check runtime availability/version and choose local/run-local install or report a clear blocker.
- [x] 5.6 For artifact_missing, search approved output locations and inspect command output before reporting failure or success.
- [x] 5.7 Surface recovery attempts through logs and progress events.

## 6. Skill Runtime as Plugin Layer

- [x] 6.1 Audit current SkillManager, `skill_call`, Skill mount roots, ACP/WebUI Skills API, and prompt injection behavior.
- [x] 6.2 Define Skill metadata schema for optional manifest/frontmatter: entrypoints, args, dependencies, assets, outputs, platform constraints, and examples.
- [x] 6.3 Update `skill_call` to return root, instructions, tree, parsed metadata, entrypoint hints, dependency hints, and output hints while preserving legacy behavior.
- [x] 6.4 Implement isolated Skill run directory creation under the workspace or approved temp root.
- [x] 6.5 Implement Skill asset staging from read-only installed Skill roots into the run directory.
- [x] 6.6 Implement minimal `skill_run` or equivalent runtime path for declared command entrypoints.
- [x] 6.7 Return envelope v2 Skill execution status, logs, failure details, recovery hints, and artifacts.
- [x] 6.8 Add compatibility tests for legacy Skills, Chinese Skill names, Windows-style paths, script-heavy Skills, and disabled Skill runtime fallback.

## 7. Progress Event Bus and UI Feedback

- [x] 7.1 Define frontend-neutral event schema for task_started, plan_created, tool_started, tool_output, tool_finished, recovering, skill_loading, skill_running, artifact_ready, task_completed, and task_failed.
- [x] 7.2 Emit events from TaskController, command runner, file/artifact tools, Recovery Engine, Skill runtime, and final answer gate.
- [x] 7.3 Render concise progress in Qt Widget without overwhelming raw logs.
- [x] 7.4 Render tool/Skill progress and artifact-ready cards in ACP WebUI.
- [x] 7.5 Verify bridge mode and direct runtime mode preserve chat semantics while showing progress.

## 8. Win7 Compatibility Pack

- [x] 8.1 Define supported Windows/Win7 execution assumptions and fallback order for shell, path, encoding, and runtime tools.
- [x] 8.2 Add diagnostics for Node/npm, Python/pip, git, shell, TLS/proxy, certificates, and VC runtime availability.
- [x] 8.3 Prefer workspace-local/run-local dependency installs and clearly report network/proxy failures.
- [x] 8.4 Add path handling tests for spaces, Chinese names, drive letters, backslashes, relative paths, and long-path fallback.
- [x] 8.5 Decide whether portable Node/Python/helper binaries are bundled or only diagnosed in the first implementation.

## 9. End-to-End Verification

- [x] 9.1 Verify a PPTX-generation Skill completes through runtime-managed paths and confirms the `.pptx` artifact.
- [x] 9.2 Verify a known OpenCode-successful Skill can complete in EVA without repeated user intervention.
- [x] 9.3 Verify path failure recovery uses structured file tools and avoids unchanged command retries.
- [x] 9.4 Verify dependency-missing behavior installs locally when allowed or reports a clear blocker.
- [x] 9.5 Verify code modification tasks require build/test observation or explicitly report skipped verification.
- [x] 9.6 Build affected C++ targets and WebUI artifacts.
- [x] 9.7 Run OpenSpec status/validation for `improve-agent-runtime-engineering`.
