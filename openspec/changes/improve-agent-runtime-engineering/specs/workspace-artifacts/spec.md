## ADDED Requirements

### Requirement: Runtime manages workspace roots and safe paths
The system SHALL define and enforce allowed roots for workspace writes, installed Skill reads, temporary runs, dependency caches, and artifact outputs.

#### Scenario: Agent writes a user file
- **WHEN** the agent writes, moves, or copies a file for the user's task
- **THEN** the target MUST be inside the current workspace, an approved artifact directory, or an explicitly approved path

#### Scenario: Agent reads installed Skill assets
- **WHEN** the agent reads files from mounted Skill roots
- **THEN** the system MUST allow read access while preventing accidental mutation during normal Skill execution

#### Scenario: Agent resolves a path
- **WHEN** a path is provided by the model, user, Skill, or tool output
- **THEN** the runtime MUST normalize it consistently and preserve enough information for Windows paths, relative paths, spaces, and non-ASCII names

### Requirement: Runtime confirms generated artifacts
The system SHALL track generated artifacts as first-class runtime objects.

#### Scenario: File artifact is generated
- **WHEN** a command, Skill, or file operation produces an output artifact
- **THEN** the runtime MUST be able to confirm its path, size, type or extension, and modified time
- **AND** the artifact SHOULD include source step and display label metadata when available

#### Scenario: Artifact is ready for the user
- **WHEN** an artifact has been confirmed
- **THEN** the runtime MUST emit an artifact-ready event that frontends can render as a path, open action, download action, or inspect action as appropriate

#### Scenario: Expected artifact is missing
- **WHEN** a generation step completes but the expected artifact cannot be confirmed
- **THEN** the runtime MUST report artifact_missing or equivalent status and SHOULD search approved output locations before allowing the task to be reported complete

### Requirement: Runtime manages temporary run directories
The system SHALL provide controlled temporary run directories for Skills, scripts, and generated intermediate files.

#### Scenario: Isolated run starts
- **WHEN** a runtime-managed action needs intermediate files
- **THEN** the system MUST create a run directory under an approved workspace or temp root

#### Scenario: Run completes or fails
- **WHEN** the action completes or fails
- **THEN** the system MUST retain enough logs and file paths for diagnosis and apply a documented cleanup or retention policy
