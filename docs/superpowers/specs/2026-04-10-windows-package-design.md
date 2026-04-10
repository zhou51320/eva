# EVA Windows Package Design

## Goal

Add a dedicated GitHub Actions workflow that builds a **Win7-targeted, MinGW-based, Qt 5.15 Windows package** and uploads a **download-and-run zip artifact**.

The package should match the existing user-facing distribution style as closely as possible:

- one main executable in the root directory
- `EVA_BACKEND/` included from repository contents
- `EVA_SKILLS/` included from repository contents
- `EVA_MODELS/` included only if it already exists in the repository
- Qt runtime files and plugin directories present as needed for direct execution after unzip

## Non-Goals

This work does **not** include:

- replacing the existing Linux unit-test workflow
- building an installer
- publishing GitHub Releases automatically
- downloading backend binaries from external sources during CI
- guaranteeing runtime validation on an actual Win7 machine inside CI

## User Requirements Captured

- Packaging target is **Windows 7 64-bit** usage.
- Toolchain preference is **MinGW + Qt 5.15**.
- The resulting artifact should be **directly usable after unzip**.
- The package structure should follow the original project’s distribution shape.
- The main executable should keep the existing versioned naming style, specifically `eva-b8149-x86.exe`.
- `EVA_BACKEND/` should be copied from repository contents if present.

## Recommended Approach

Create a **separate workflow** at:

- `.github/workflows/windows-package.yml`

This workflow should be independent from `.github/workflows/tests.yml`.

### Why this approach

Compared with extending the current Linux test workflow into a matrix build:

- logs stay easier to read
- Linux unit-test reliability is preserved
- Windows packaging can evolve independently
- packaging-specific retries or tuning do not affect test CI

## Workflow Design

### Trigger Model

Initial triggers:

- `workflow_dispatch`

Optional later expansion:

- push on selected branches
- release/tag-based packaging

For the first version, `workflow_dispatch` is preferred because Windows packaging will likely need iteration.

## Build Environment

The workflow should run on:

- `windows-latest`

The job should provision:

- MinGW toolchain
- CMake
- Ninja
- Qt 5.15 for MinGW

The workflow should explicitly avoid MSVC for this packaging path.

## Build Commands

The Windows package build should follow the project’s documented build style with CI-friendly explicitness.

Recommended configure/build flow:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBODY_PACK=OFF
cmake --build build --config Release --parallel
```

### BODY_PACK choice

Use `BODY_PACK=OFF` for the workflow.

Reasoning:

- the requested output is a manually curated unzip-and-run package
- this keeps packaging logic explicit and predictable in CI
- it avoids relying on the project’s Linux/Windows BODY_PACK behaviors for artifact shaping
- it reduces coupling between build logic and release layout logic

## Package Assembly Design

After the build completes, the workflow should create a release staging directory with a stable shape.

Recommended root directory name inside the zip:

- `eva-b8149-win7-x64/`

Inside that directory, place:

- renamed main executable: `eva-b8149-x86.exe`
- required Qt runtime DLLs
- required Qt plugin directories
- `EVA_BACKEND/` from repository root, if present
- `EVA_SKILLS/` from repository root, if present
- `EVA_MODELS/` from repository root, if present

### Expected package shape

```text
eva-b8149-win7-x64/
  eva-b8149-x86.exe
  Qt5Core.dll
  Qt5Gui.dll
  Qt5Widgets.dll
  ...
  platforms/
  imageformats/
  EVA_BACKEND/
  EVA_SKILLS/
  EVA_MODELS/   # only when present
```

`EVA_TEMP/` should **not** be pre-populated unless there is a strong packaging reason later. The application is expected to create it at runtime as documented.

## Executable Naming Policy

The user explicitly wants the original naming style retained.

Therefore:

- the packaged executable should be renamed to `eva-b8149-x86.exe`

Even though the intended runtime environment is Win7 64-bit, the file name should preserve the existing user-facing naming convention for compatibility with prior expectations.

The outer artifact name should still identify the package target clearly:

- `eva-b8149-win7-x64.zip`

This keeps the download distinguishable while preserving the familiar executable name.

## Qt Runtime Deployment

The workflow should use the Qt Windows deployment tool (`windeployqt`) to collect runtime DLLs and plugin folders for the packaged executable.

### Required outcome

The packaged directory must contain everything needed to launch the application after unzip on a suitable Windows machine, without requiring the user to separately install Qt.

### Acceptable packaging trade-off

It is acceptable if the package contains more Qt DLLs/plugins than the absolute minimum, as long as:

- the package remains directly runnable
- the layout remains understandable
- unnecessary CI complexity is avoided

## Repository Content Copy Rules

### EVA_BACKEND

- If `EVA_BACKEND/` exists in the repository root, copy it into the package.
- If it does not exist, fail the packaging job with a clear error.

Reason: the user explicitly wants repository contents used, and the final package is meant to be directly usable.

### EVA_SKILLS

- If `EVA_SKILLS/` exists, copy it.
- If missing, create an empty directory and emit a warning in logs.

Reason: missing skills should not necessarily block packaging if the app can still start.

### EVA_MODELS

- If `EVA_MODELS/` exists, copy it.
- If missing, skip it silently or with an informational log.

Reason: model payloads may be intentionally excluded due to size.

## Win7 Compatibility Position

This workflow is intended to produce a **Win7-oriented package**, but GitHub Actions cannot provide authoritative runtime verification on Windows 7.

So the workflow’s success criteria are:

1. the Windows package builds successfully
2. the package layout matches the intended unzip-and-run structure
3. runtime dependencies are bundled into the package
4. the artifact is downloadable as a zip

Final compatibility confirmation on real Win7 remains a manual validation step outside CI.

## Failure Handling

The workflow should fail clearly when:

- MinGW or Qt 5.15 setup fails
- the build fails
- the main executable is not found after build
- `EVA_BACKEND/` is required by policy but missing
- zip creation fails
- artifact upload fails

Each failure point should have a short, direct log message indicating what was missing or what command failed.

## Artifact Upload Policy

The workflow should upload exactly one primary package artifact for the first version.

Recommended artifact name:

- `eva-b8149-win7-x64`

Recommended file payload inside artifact:

- `eva-b8149-win7-x64.zip`

This keeps the Actions UI simple and avoids ambiguity about which download is the real deliverable.

## Testing Strategy

The Windows packaging workflow should validate:

- configure step succeeds
- build step succeeds
- package staging directory is created
- expected main executable exists after rename
- zip file exists before artifact upload

It should **not** run the full unit-test suite initially unless that proves cheap and stable. The Linux workflow already covers unit tests. The Windows package workflow should focus on producing the deliverable correctly.

## Future Extensions

Possible later additions, intentionally excluded from the first version:

- automatic GitHub Release upload
- installer generation
- multiple Windows package variants
- matrix builds for different Qt/toolchain variants
- real Win7 runtime smoke validation on external infrastructure

## Files Expected To Change

Primary new file:

- `.github/workflows/windows-package.yml`

Possible supporting documentation updates:

- `README.md`
- `README_en.md`
- build/package documentation if needed

## Acceptance Criteria

This design is complete when the implemented workflow can:

1. be triggered manually in GitHub Actions
2. build EVA on Windows using MinGW + Qt 5.15
3. assemble a package directory containing:
   - `eva-b8149-x86.exe`
   - Qt runtime files
   - `EVA_BACKEND/`
   - `EVA_SKILLS/`
   - optional `EVA_MODELS/`
4. compress that directory into `eva-b8149-win7-x64.zip`
5. upload the zip as a downloadable Actions artifact
6. produce a package that is intended for unzip-and-run use on Win7 64-bit environments
