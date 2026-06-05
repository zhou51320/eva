## ADDED Requirements

### Requirement: Skills are runtime plugins with backward compatibility
The system SHALL treat Skills as reusable runtime plugins while preserving existing natural-language `SKILL.md` behavior.

#### Scenario: User explicitly requests a Skill
- **WHEN** the user asks EVA to use a named Skill
- **THEN** the agent MUST load that Skill through `skill_call` or equivalent Skill discovery before task-specific execution
- **AND** the agent MUST use the Skill workflow as primary unless it is unavailable or fails after recovery attempts

#### Scenario: Skill is unavailable
- **WHEN** a requested Skill cannot be found or mounted
- **THEN** the agent MUST report that it is unavailable and MUST NOT pretend to have used it

#### Scenario: Legacy Skill has only instructions
- **WHEN** an installed Skill only provides `SKILL.md` natural-language instructions
- **THEN** the system MUST continue to return those instructions and SHOULD provide best-effort hints without requiring migration

### Requirement: Skills can declare executable metadata
The system SHALL support optional Skill metadata for entrypoints, dependencies, assets, outputs, platform constraints, and examples.

#### Scenario: Skill declares entrypoints
- **WHEN** a Skill manifest or frontmatter declares entrypoints
- **THEN** `skill_call` or an equivalent API MUST expose action names, argument hints or schemas, dependency hints, asset requirements, output hints, and platform constraints

#### Scenario: Skill has script-heavy legacy structure
- **WHEN** a legacy Skill contains obvious scripts or templates but no manifest
- **THEN** the system SHOULD expose best-effort entrypoint hints without mutating the installed Skill

### Requirement: Skill execution uses isolated run directories
The system SHALL execute Skill actions without mutating installed Skill directories.

#### Scenario: Agent runs a Skill action
- **WHEN** the agent invokes a supported Skill action through Skill runtime
- **THEN** the system MUST create an isolated run directory, stage required assets, execute the declared entrypoint with provided arguments, and return structured status

#### Scenario: Skill action produces outputs
- **WHEN** a Skill action produces output files
- **THEN** the system MUST confirm artifacts and expose their final paths through the artifact manager

#### Scenario: Skill action fails
- **WHEN** Skill execution fails because of path, dependency, script, permission, timeout, network, or platform issues
- **THEN** the result MUST include a structured error and recovery hints
- **AND** the installed Skill directory MUST remain unmodified during normal execution
