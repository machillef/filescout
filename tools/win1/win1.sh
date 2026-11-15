#!/usr/bin/env bash
# Drive the win1 Windows test VM from ubuntu-containers: mirror a checkout, build, test, run GUI.
#
#   tools/win1/win1.sh sync  <name> [dir]          mirror git-visible files of dir (default: repo root)
#                                                   to C:\fsdev\wt\<name> (build outputs are kept)
#   tools/win1/win1.sh build <name> [Debug|Release] [--test [gtest-filter]]
#   tools/win1/win1.sh ps    '<pwsh command>'      run a command over ssh (session 0, Administrator)
#   tools/win1/win1.sh gui   '<pwsh command>' [timeout-s]
#                                                   run in runetest's console session 1 (GUI); prints
#                                                   its stdout/stderr. Holds C:\fsdev\gui.lock.
#   tools/win1/win1.sh guard                       fail if a \HermesRune-* task (other than
#                                                   DesktopGuard) or another GUI run is active
#   tools/win1/win1.sh fetch <remote-path> <local-path>
#
# GUI runs must happen in session 1 (never session 0): see ~/.claude/CLAUDE.md [windows-test-vm].
set -euo pipefail

HOST=${WIN1_HOST:-win1}
WT_ROOT='C:\fsdev\wt'
ssh_q() { ssh -o ConnectTimeout=15 -o ServerAliveInterval=30 -o LogLevel=ERROR "$HOST" "$@" 2> >(grep -v -e 'post-quantum' -e 'store now' -e 'may need to be upgraded' >&2); }

cmd=${1:-}; shift || true
case "$cmd" in
  sync)
    name=$1; dir=${2:-$(git rev-parse --show-toplevel)}
    stage="C:\\fsdev\\stage\\$name"; dest="$WT_ROOT\\$name"
    ssh_q "Remove-Item -Recurse -Force '$stage' -ea 0; New-Item -ItemType Directory -Force '$stage','$dest' | Out-Null"
    (cd "$dir" && git ls-files -co --exclude-standard -z | tar --null -cf - -T -) | \
      ssh_q "tar -xf - -C '$stage'"
    # Content-based mirror (Sync-Stage.ps1): changed files get "now" so MSBuild rebuilds them,
    # unchanged ones keep a stable time so the next sync does not copy them again.
    scp -q "$(dirname "$0")/Sync-Stage.ps1" "$HOST:C:/fsdev/tools/Sync-Stage.ps1"
    ssh_q "pwsh -NoProfile -File C:\\fsdev\\tools\\Sync-Stage.ps1 -Stage '$stage' -Dest '$dest'; if (\$LASTEXITCODE) { exit 1 }; Remove-Item -Recurse -Force '$stage'"
    # Branches cut before tools/win1 existed still build with the current helper scripts.
    ssh_q "New-Item -ItemType Directory -Force '$dest\\tools\\win1' | Out-Null"
    scp -q "$(dirname "$0")/build.ps1" "$HOST:$(echo "$dest" | tr '\\' '/')/tools/win1/build.ps1"
    ;;
  build)
    name=$1; shift; config=Debug; test=""; filter='*'
    if [[ ${1:-} == Debug || ${1:-} == Release ]]; then config=$1; shift; fi
    if [[ ${1:-} == --test ]]; then test="-Test"; shift; filter=${1:-*}; fi
    # Long builds over one ssh session get cut silently (OpenSSH kills the session's job), so run
    # them as a scheduled task (Invoke-Detached.ps1) and poll for the exit file.
    out="C:\\fsdev\\wt\\$name-run.log"
    inner="& '$WT_ROOT\\$name\\tools\\win1\\build.ps1' -Root '$WT_ROOT\\$name' -Configuration $config $test -TestFilter '$filter'"
    b64=$(printf '%s' "$inner" | iconv -t UTF-16LE | base64 -w0)
    ssh_q "pwsh -NoProfile -File C:\\fsdev\\tools\\Invoke-Detached.ps1 -EncodedCommand $b64 -LogPath '$out'" >/dev/null
    while ! ssh_q "if (Test-Path '$out.exit') { exit 0 } else { exit 1 }" 2>/dev/null; do sleep 10; done
    ssh_q "Get-Content '$out'; exit [int](Get-Content '$out.exit')"
    ;;
  ps)
    ssh_q "$1"
    ;;
  guard)
    ssh_q '$t = Get-ScheduledTask -TaskPath "\" | ? { $_.TaskName -like "HermesRune*" -and $_.TaskName -ne "HermesRune-DesktopGuard" -and $_.State -eq "Running" }; if ($t) { "busy: " + ($t.TaskName -join ","); exit 3 }; if (Test-Path C:\fsdev\gui.lock) { "busy: gui.lock " + (Get-Content C:\fsdev\gui.lock); exit 4 }; "free"'
    ;;
  gui)
    command=$1; timeout=${2:-600}
    b64=$(printf '%s' "$command" | iconv -t UTF-16LE | base64 -w0)
    ssh_q "pwsh -NoProfile -File C:\\fsdev\\tools\\Invoke-Session1.ps1 -EncodedCommand $b64 -TimeoutSeconds $timeout"
    ;;
  fetch)
    scp -q "$HOST:$1" "$2"
    ;;
  *)
    sed -n '2,16p' "$0"; exit 2 ;;
esac
