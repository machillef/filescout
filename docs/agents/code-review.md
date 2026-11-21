# AI code review: CodeRabbit and Greptile

Both are advisory: they never gate a merge, CI does. Verify every finding against the code before fixing it,
and skip the ones that are wrong.

## CodeRabbit: every PR

Every PR gets a full CodeRabbit review of its latest commit before it is merged. The repository is public,
so the review is free. `.coderabbit.yaml` turns on automatic reviews and never pauses them, but CodeRabbit
only reviews public repositories with fewer than 10 stars on request. So after opening a PR, and again after
the last push before merging, check that a review covers the head commit; if not, ask for one:

```
gh pr comment <n> --body "@coderabbitai review"
```

The walkthrough is a summary comment from `coderabbitai[bot]` (edited in place); findings are inline comments.
Read them with `gh api --paginate repos/machillef/filescout/issues/<n>/comments` and
`gh api --paginate repos/machillef/filescout/pulls/<n>/comments` (one page holds only 30 comments).

Before a PR, the CLI reviews a local diff (`coderabbit`, alias `cr`, logged in to `machillef`; 10 free reviews
per repository per rolling hour, never pass `--use-credits`; 5-15 minutes, run it in the background):

```
cr review --uncommitted --include-untracked --agent  # work not yet committed, new files too
cr review --committed --base master --agent          # the branch commits, as the PR will see them
cr review findings                                   # show the last review again
```

## Greptile: on request only

The Greptile GitHub App reviews a PR only when asked (automatic reviews are set to Never in the dashboard).
The free plan has 50 credits per month for every repository, so ask once per PR when it is ready, and only
when the user wants it:

```
gh pr comment <n> --body "@greptileai"                     # Base review, 1 credit
gh pr comment <n> --body "@greptileai review this at plus" # 3 credits, only when asked
```

The review arrives in a few minutes as a summary comment from `greptile-apps[bot]` plus inline comments.
