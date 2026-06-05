## ADDED Requirements

### Requirement: Runtime accounts for Windows and Win7 command compatibility
The system SHALL avoid assuming Unix-like shells or modern Windows shell features when executing commands on Windows/Win7.

#### Scenario: Command shell is selected
- **WHEN** the runtime executes a command on Windows or Win7
- **THEN** it MUST choose a compatible shell or fallback strategy and report unsupported shell features clearly

#### Scenario: Command output contains non-ASCII text
- **WHEN** command output includes Chinese paths, localized errors, or non-ASCII content
- **THEN** the runtime SHOULD preserve readable output or indicate encoding limitations instead of silently corrupting diagnostics

#### Scenario: Paths contain spaces or non-ASCII names
- **WHEN** files or directories contain spaces, Chinese names, drive letters, or backslashes
- **THEN** structured path operations and command execution MUST handle quoting and normalization safely

### Requirement: Runtime diagnoses dependency and portable runtime availability
The system SHALL provide clear diagnostics for common local runtimes and dependencies used by agent tasks and Skills.

#### Scenario: Node, Python, git, or package manager is required
- **WHEN** a task or Skill requires Node/npm, Python/pip, git, or similar tools
- **THEN** the runtime MUST check availability/version when failure suggests a missing dependency
- **AND** it MUST report whether the dependency is missing, unsupported on Win7, or blocked by configuration

#### Scenario: Dependency install needs network access
- **WHEN** dependency installation or package fetching fails due to network, TLS, certificate, or proxy issues
- **THEN** the runtime MUST surface actionable proxy/TLS/certificate diagnostics instead of a generic command failure

### Requirement: Win7 compatibility supports local-first execution
The system SHALL prefer workspace-local, run-local, or cached dependency locations for agent-managed execution.

#### Scenario: Skill or task installs dependencies
- **WHEN** EVA installs or prepares dependencies for a runtime-managed action
- **THEN** it SHOULD avoid mutating global system locations unless explicitly approved
- **AND** it SHOULD support local/cache reuse where practical

#### Scenario: Platform cannot support required runtime
- **WHEN** Win7 or the current Windows environment cannot support the required toolchain
- **THEN** the agent MUST report a clear unsupported_platform blocker and suggest feasible alternatives or prerequisites
