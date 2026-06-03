## ADDED Requirements

### Requirement: WebUI can list installed Skills
The system SHALL provide ACP/WebUI with a list of installed Skills from the main EVA process when bridge mode is available.

#### Scenario: List Skills through bridge
- **WHEN** ACP/WebUI requests the Skills list while connected to the main EVA bridge
- **THEN** the response includes each installed Skill's id, enabled state, description, license, root path, and `SKILL.md` path

#### Scenario: Bridge unavailable for Skills list
- **WHEN** ACP/WebUI requests the Skills list without an available main EVA bridge
- **THEN** the response indicates that Skills management is unavailable instead of returning stale or direct-runtime-only state

### Requirement: WebUI can enable or disable an installed Skill
The system SHALL allow ACP/WebUI to enable or disable an installed Skill through the main EVA bridge without implicitly changing tool capability state.

#### Scenario: Enable installed Skill
- **WHEN** ACP/WebUI requests `set_enabled` for an installed Skill with `enabled` set to true
- **THEN** the main EVA process enables that Skill using `SkillManager` and returns the refreshed Skills list

#### Scenario: Disable installed Skill
- **WHEN** ACP/WebUI requests `set_enabled` for an installed Skill with `enabled` set to false
- **THEN** the main EVA process disables that Skill using `SkillManager` and returns the refreshed Skills list

#### Scenario: Skill toggle does not enable engineer
- **WHEN** ACP/WebUI enables a Skill while the system engineer capability is disabled
- **THEN** the Skill enabled state changes but the system engineer capability remains disabled

### Requirement: WebUI can remove an installed Skill
The system SHALL allow ACP/WebUI to remove an installed Skill through the main EVA bridge after the WebUI has requested removal.

#### Scenario: Remove installed Skill
- **WHEN** ACP/WebUI requests `remove` for an installed Skill
- **THEN** the main EVA process removes the Skill directory using `SkillManager` and returns the refreshed Skills list

#### Scenario: Remove missing Skill
- **WHEN** ACP/WebUI requests `remove` for a Skill id that is not installed
- **THEN** the system returns an error message and does not report the operation as accepted

### Requirement: WebUI can refresh Skills state
The system SHALL allow ACP/WebUI to request a Skills refresh from disk through the main EVA bridge.

#### Scenario: Refresh Skills list
- **WHEN** ACP/WebUI requests `refresh` for Skills
- **THEN** the main EVA process reloads Skills from disk and returns the current Skills list

### Requirement: Skills management UI is separate from tools UI
The WebUI SHALL present Skills management as a separate control surface from tool capability toggles.

#### Scenario: Display Skills separate from tools
- **WHEN** the user opens the ACP/WebUI control panel
- **THEN** tool capability toggles and installed Skills management are shown as distinct sections or tabs

#### Scenario: Engineer disabled hint
- **WHEN** the Skills surface is shown while the system engineer capability is disabled
- **THEN** the WebUI informs the user that enabled Skills require the system engineer capability to be injected and used

### Requirement: First version excludes Skill import upload
The system SHALL NOT expose browser zip upload or remote Skill installation as part of this change.

#### Scenario: No upload control in first version
- **WHEN** the user opens the Skills management surface
- **THEN** the WebUI does not present a zip upload or remote marketplace installation control
