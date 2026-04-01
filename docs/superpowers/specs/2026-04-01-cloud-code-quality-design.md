# Cloud Code Quality And CI Design

## Goal

Add a cloud-first GitHub Actions workflow for FileScout that restores, builds, and tests the native Windows solution, and integrates SonarQube Cloud for code-quality scanning on pushes and pull requests.

## Why This Work Exists

The repository currently has no GitHub Actions workflows and no automated cloud code-quality scanning. Local verification is strong, but there is no hosted CI gate to protect `master` and no recurring static-analysis surface for pull requests.

The user wants a free-tier solution if possible, detailed setup instructions, and a design that starts fully cloud-hosted while preserving a clean fallback path if hosted Windows runners or SonarQube Cloud prove insufficient.

## Constraints

- The repository is currently private.
- The preferred initial approach is fully cloud-hosted.
- The solution should be compatible with GitHub-hosted Windows runners first.
- The design should remain easy to adapt to:
  - a self-hosted Windows runner
  - a self-hosted SonarQube server
- The workflow should target the current native solution under `native/`.

## Recommendation

Use a two-part cloud-first setup:

1. A GitHub Actions workflow running on `windows-latest` that restores, builds, and runs the native test suite.
2. SonarQube Cloud analysis integrated into the same workflow, triggered on pushes to `master` and pull requests targeting `master`.

This is the lowest-operations path that still gives automated quality feedback in a private repository, while leaving the repo structure ready for future self-hosted fallback if needed.

## Architecture

### Files To Add

- `.github/workflows/ci-sonar.yml`
- `sonar-project.properties`

### Workflow Responsibilities

The GitHub Actions workflow should:

- trigger on push to `master`
- trigger on pull requests targeting `master`
- run on a GitHub-hosted Windows runner
- restore the solution with MSBuild
- build the solution in `Debug|x64`
- run `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no`
- run SonarQube Cloud analysis after build and tests complete

### Sonar Configuration Responsibilities

The root `sonar-project.properties` file should:

- define the Sonar organization and project key using repository secrets or variables where practical
- identify the codebase roots for analysis
- separate production code and test code
- avoid trying to over-model C++ build-wrapper coverage on the first pass

The first version should optimize for “scanner runs and produces useful feedback” rather than full advanced C/C++ compilation-database precision. If Sonar’s C++ analysis quality is insufficient without deeper build-wrapper support, that becomes the first follow-up task.

## Setup Instructions For The User

### GitHub Side

1. Open the repository on GitHub.
2. Go to `Settings` -> `Secrets and variables` -> `Actions`.
3. Add a repository secret:
   - `SONAR_TOKEN`
4. Add repository variables if we choose to keep project identity outside the repo:
   - `SONAR_PROJECT_KEY`
   - `SONAR_ORG`

If the project key and organization are safe to keep in `sonar-project.properties`, only `SONAR_TOKEN` needs to be secret.

### SonarQube Cloud Side

1. Sign in to SonarQube Cloud with GitHub.
2. Import the `machillef/filescout` repository.
3. Create the SonarQube Cloud project.
4. Copy the Sonar project key.
5. Copy the Sonar organization key.
6. Generate a user token for CI scanning.
7. Save that token into the GitHub repository secret `SONAR_TOKEN`.

### What To Expect On First Run

The first GitHub Actions run may fail for one of three reasons:

1. Windows App SDK or build-environment mismatch on the hosted runner.
2. SonarQube Cloud project key, org key, or token mismatch.
3. Scanner limitations for this native C++ solution without deeper Sonar CFamily setup.

The design should make each of these failure modes diagnosable without restructuring the repo.

## Workflow Design Details

### Trigger Strategy

- `push` on `master`
- `pull_request` on `master`

This keeps the workflow focused on merge protection and active review traffic without over-triggering on every branch by default.

### Job Shape

Use a single Windows job first:

- checkout
- prepare MSBuild environment
- restore
- build
- test
- Sonar scan

A single job is simpler for the initial rollout because the native outputs live in one Windows environment and Sonar results should correspond to the exact build that passed.

### Build Commands

Mirror the repository’s known-good local commands as closely as possible:

- restore with the Visual Studio developer environment and MSBuild
- build `native\FileScout.Native.sln` with `Configuration=Debug` and `Platform=x64`
- run `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no`

This reduces drift between local verification and CI verification.

## Sonar Scope

The initial scan should focus on:

- `native/FileScout.Native`
- `native/FileScout.Tests`

We may optionally include Markdown and workflow files later, but the first pass should prioritize the C++ codebase.

## Fallback Design

### If GitHub-Hosted Windows Runner Fails

Keep the workflow structure the same but change:

- `runs-on: windows-latest`

to a self-hosted Windows label, for example:

- `runs-on: [self-hosted, windows, x64]`

No repo structure change should be required beyond runner labels and any local path/tooling adjustments.

### If SonarQube Cloud Is Insufficient

Keep `sonar-project.properties`, but swap the scanner target and token source to a self-hosted SonarQube server. The GitHub workflow can continue to own restore/build/test and simply point scanning at the self-hosted instance.

### If Sonar Needs Deeper Native C++ Support

The follow-up should investigate Sonar CFamily build-wrapper integration or an alternative native-analysis path. This is a follow-up concern, not a blocker for the first cloud-first workflow attempt.

## Verification Plan

After implementation:

- confirm the workflow file is syntactically valid
- confirm the Sonar config file matches the repo layout
- push a branch and observe the GitHub Actions run
- verify:
  - restore completes
  - build completes
  - tests run
  - Sonar step authenticates and uploads analysis
- if the workflow fails, record whether the failure is:
  - GitHub runner/toolchain
  - build/test issue
  - Sonar auth/config issue
  - Sonar native-analysis limitation

## Risks

- GitHub-hosted Windows images may not match the local WinUI/App SDK environment closely enough.
- SonarQube Cloud’s free private-repo path may work commercially but still need careful C++ configuration.
- A first-pass workflow that mixes build/test and Sonar in one job may be slower, though it is simpler to debug.

## Non-Goals

- No self-hosted runner setup in the first implementation pass.
- No self-hosted SonarQube server setup in the first implementation pass.
- No advanced coverage publishing or badge work in the first implementation pass.
- No branch protection policy automation in the first implementation pass.

## Success Criteria

The first implementation is successful if:

- the repo gains a Windows GitHub Actions workflow
- the workflow attempts restore, build, and test using the known native commands
- SonarQube Cloud is wired into the workflow
- the setup instructions are sufficient for the user to configure the required GitHub and Sonar settings
- fallback paths are documented clearly if the fully cloud-hosted route needs adjustment
