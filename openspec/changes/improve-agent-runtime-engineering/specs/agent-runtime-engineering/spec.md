## ADDED Requirements

### Requirement: Agent follows an explicit task execution protocol
The system SHALL guide agent tasks through a runtime protocol that favors observable progress, verified outcomes, and recovery over unconstrained text generation.

#### Scenario: Agent starts a non-trivial task
- **WHEN** a user asks EVA to perform a file, code, generation, or tool-using task
- **THEN** the agent MUST reason in terms of understanding the goal, forming a verifiable plan, inspecting required context, acting with tools, observing results, recovering from failures, verifying outputs, and reporting status
- **AND** the agent MUST NOT skip directly to a final completion claim without relevant observation when tool verification is possible

#### Scenario: Tool usage is available
- **WHEN** a task requires checking files, running commands, generating artifacts, or modifying code
- **THEN** the agent MUST prefer available structured tools over fragile shell commands for routine file, path, and artifact operations
- **AND** shell commands SHOULD be treated as an escape hatch for operations that require a CLI or script execution

### Requirement: Agent applies final answer gates
The system SHALL prevent or discourage final answers that claim completion without satisfying task-specific verification gates.

#### Scenario: Task generates a file artifact
- **WHEN** a task is expected to produce a file
- **THEN** the agent MUST confirm the artifact exists with metadata such as path and size before reporting it as complete
- **AND** if confirmation cannot be performed, the final answer MUST explicitly state that the artifact was not verified

#### Scenario: Task modifies code
- **WHEN** a task modifies code or configuration
- **THEN** the agent MUST run an appropriate build, test, lint, or targeted verification when available
- **AND** if verification is skipped or blocked, the final answer MUST state the reason and current risk

#### Scenario: Task is blocked
- **WHEN** a tool, dependency, permission, network, or platform blocker prevents completion
- **THEN** the agent MUST report the blocker and any partial work instead of presenting the task as done

### Requirement: Agent follows standardized recovery behavior
The system SHALL guide the agent to classify failures, change strategy after failure, and avoid repeating identical failed actions.

#### Scenario: A tool call fails
- **WHEN** a tool call or command returns a failure
- **THEN** the agent MUST inspect the structured error type and recovery hints if present
- **AND** the agent MUST choose a recovery action or explain why recovery is blocked

#### Scenario: An action fails repeatedly
- **WHEN** the same command or tool call has already failed without new evidence
- **THEN** the agent MUST NOT repeat it unchanged
- **AND** the agent MUST inspect context, change arguments, use a different tool, or ask for confirmation

#### Scenario: Windows or Win7 compatibility may affect execution
- **WHEN** a task uses shell commands, paths, runtimes, dependencies, networking, or file encodings on Windows/Win7
- **THEN** the agent MUST avoid assuming modern Unix shell or modern PowerShell behavior
- **AND** the agent MUST surface platform-specific blockers clearly
