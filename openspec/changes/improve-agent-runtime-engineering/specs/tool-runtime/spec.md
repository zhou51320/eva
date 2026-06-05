## ADDED Requirements

### Requirement: Tools return structured result envelopes
The system SHALL expose important tool observations through a structured envelope containing success state, summary, data, artifacts, warnings, errors, and recovery hints.

#### Scenario: Tool succeeds
- **WHEN** a tool operation succeeds
- **THEN** the tool result MUST include `ok: true`, a concise summary, and any relevant structured data
- **AND** generated or discovered artifacts MUST be reported in an `artifacts` collection when applicable

#### Scenario: Tool fails
- **WHEN** a tool operation fails
- **THEN** the tool result MUST include `ok: false`, a concise summary, a structured error with type and message, and recovery hints when available
- **AND** the failure MUST preserve enough stdout, stderr, path, or diagnostic detail for the agent to choose a next action

#### Scenario: Legacy tool result is returned
- **WHEN** an existing tool cannot yet emit the new envelope natively
- **THEN** the runtime MUST wrap or adapt the legacy result into the envelope shape without breaking existing consumers

### Requirement: Command runner provides execution runtime semantics
The system SHALL treat command execution as a controlled runtime primitive rather than an opaque text shortcut.

#### Scenario: Agent runs a command
- **WHEN** the agent invokes command execution
- **THEN** the request SHOULD include cwd, timeout, environment overrides, shell selection when relevant, and a concise purpose
- **AND** the response MUST include exit code, stdout, stderr, duration, and failure classification when possible

#### Scenario: Command runs for a long time
- **WHEN** command execution is long-running
- **THEN** the runtime MUST support progress output and cancellation without requiring the user to wait for a silent operation

#### Scenario: Command fails due to platform or environment
- **WHEN** a command fails because an executable, dependency, shell feature, encoding, path syntax, permission, or network capability is unavailable
- **THEN** the command result MUST classify the failure when possible and provide recovery hints

### Requirement: Routine file and path actions use structured tools
The system SHALL provide structured operations for common file/path actions so agents do not depend on platform-specific shell syntax.

#### Scenario: Agent checks a path
- **WHEN** the agent needs to know whether a file or directory exists
- **THEN** the system MUST provide stat/list functionality returning normalized path, type, size, and relevant metadata

#### Scenario: Agent copies files or directories
- **WHEN** the agent needs to copy files, templates, scripts, or Skill assets
- **THEN** the system MUST provide path-safe copy functionality that handles spaces, non-ASCII names, Windows paths, and allowed-root constraints

#### Scenario: Agent searches for files
- **WHEN** the agent needs to locate files by pattern, name, or extension
- **THEN** the system MUST provide glob/search functionality scoped to allowed roots and returning normalized paths
