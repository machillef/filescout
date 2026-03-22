# Review Checklist

## Correctness

- Does the change preserve active-pane routing for commands and tabs?
- Could a shell-backed action target the wrong item or path?
- Are startup and bootstrap errors still surfaced clearly?

## Verification

- Was the native app rebuilt after the change?
- Was any manual UI verification needed but skipped?
- Does the change update stale docs when behavior or paths changed?

## Security and safety

- Could the change make destructive file operations easier to trigger accidentally?
- Are untrusted paths, shell items, or clipboard inputs validated at the boundary where practical?

## Performance and lifetime

- Any new COM, WinRT, or shell object lifetime risks?
- Any new work on UI-thread hot paths that should be questioned?

## Review output

- Findings should name the concrete risk, not just the code difference.
- If there are no findings, mention the residual manual-test risk plainly.
