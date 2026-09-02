---
name: review-files
description: Review a batch of existing source files (not a GitHub PR) for bugs, concurrency/deadlock issues, and performance problems. Launches several independent reviewer agents in parallel over the same files, then confidence-scores each finding to filter noise. Use this instead of /code-review when there is no PR to diff against -- e.g. "review these files for bugs," "audit this directory for deadlocks."
---

# Reviewing existing files (bugs / concurrency / performance)

## When to use this

Use when the user wants an existing set of source files audited for problems and there
is no pull request to diff against. This is the file-based counterpart to `/code-review`,
which only works against a GitHub PR diff.

## Inputs

The files or directories to review are passed as this skill's arguments. If a directory
is given, expand it to its source files.

Note that vendored/upstream NuttX code and Meadow's own code are interleaved under both
`nuttx/` and `apps/` -- e.g. `apps/system/hcom/` is Meadow's own code even though it
lives under `apps/`. Don't guess which is which from the path alone. If it's unclear
whether an expanded directory is vendored or Meadow-owned, ask the user rather than
silently including or excluding it.

If no arguments are given, ask the user which files or directory to review -- do not
guess.

Keep each invocation to a small batch (roughly 3-8 files). If the argument expands to
something much larger, tell the user how you'd split it and confirm before proceeding,
rather than reviewing everything in one pass.

## Steps

1. Read every file in the batch yourself first, so you can catch cross-file issues
   (e.g. a lock taken in one file and assumed-but-not-held in another) that a
   single-file-scoped agent would miss.

2. Gather relevant CLAUDE.md files: the repo root's, plus any in directories containing
   the batch's files. Note their paths -- don't summarize them yet.

3. Launch parallel agents (Agent tool, all in one message so they run concurrently, and
   wait for all of them since step 4 needs the combined results). Give every agent the
   full file list plus the CLAUDE.md paths from step 2, and this instruction: focus on
   real, concrete, non-speculative problems, and always name the file and the exact
   line number or line range for each finding (not just the function name -- the user
   wants to be able to jump straight to the code).

   - **Correctness agent**: logic errors, off-by-one, null/use-after-free, unchecked
     return values, resource/memory leaks.
   - **Concurrency agent**: deadlocks, races, missing/incorrect locking, missing
     `volatile`, ISR-vs-task context mismatches, interrupt-unsafe access to shared
     state.
   - **Performance agent**: unnecessary blocking calls in hot/ISR paths, wasteful
     copies/allocations, busy-waits, quantization issues.
   - **Convention/CLAUDE.md agent**: violations of the CLAUDE.md files gathered in
     step 2. Skip a rule if it's clearly inapplicable to this code.

   Each agent returns a list of findings: file, line/function, description, and why
   it's a problem.

4. For every finding from step 3, launch a parallel confidence-scoring agent. Give it
   the finding plus the relevant file(s), and this rubric verbatim:

   - 0: Not confident at all -- doesn't hold up to scrutiny.
   - 25: Somewhat confident -- might be real, agent couldn't verify it.
   - 50: Moderately confident -- verified real, but minor/nitpick.
   - 75: Highly confident -- verified real and will matter in practice.
   - 100: Absolutely certain -- confirmed by direct evidence, will happen frequently.

   Filter out only findings scoring below 25 (i.e. ones the agent actively concluded
   don't hold up to scrutiny) -- this is deliberately a low bar. The user reviews a large,
   one-shot codebase and wants visibility into everything plausible, including low-risk
   code-quality improvements, not just near-certain bugs; they do the prioritizing
   themselves afterward, factoring in severity, likelihood, and how large a fix is.

   Treat as outright false positives (score 0, filtered): pedantic style nitpicks a
   senior engineer wouldn't raise; anything a compiler/linter would already catch;
   issues explicitly and intentionally silenced in the code (e.g. a comment explaining
   why an apparent issue is safe). Unlike PR review, do NOT discard a finding just
   because it's "pre-existing" -- there's no diff here, everything under review is
   existing code, so pre-existing is not a disqualifier.

   Also have this agent write two short one-line notes for every finding that survives
   (these matter more here than in a PR review, since the user is triaging a large batch
   at once and needs to weigh cost against benefit): "Risk if unfixed" (what actually
   goes wrong, and how likely/severe) and "Cost/risk to fix" (how large and how risky the
   change itself would be -- e.g. "one-line, no behavior change elsewhere" vs "touches
   shared state across 3 files, needs hardware verification").

5. Report every surviving finding (score 25+) with the `ReportFindings` tool as a single
   full numbered list, ordered by score descending (highest first). Do not silently
   drop anything at or above 25 -- low-risk/high-return code-quality items matter as much
   as bugs for this review. For each finding, make sure the write-up includes: the score,
   the exact file:line or file:line-range, the one-line summary, and the "Risk if
   unfixed" / "Cost/risk to fix" notes from step 4 (fold these into `summary` and
   `failure_scenario` if the tool's fields don't have a dedicated slot for them -- don't
   drop them). If nothing survives even the 25 bar, call it with an empty list rather
   than announcing "no issues" in prose.

6. Update `Documentation/CodeReviewTracking.md` and GitHub:
   a. Group step 5's surviving findings (score 25+) by their **primary file** -- the
      first file listed in each finding's file:line range(s). For every file in this
      batch that is the primary file for 1+ findings, create one GitHub Issue for that
      file via `gh issue create` (title `Code review: <file path>`, label
      `code-review` -- create the label first with `gh label create` if it doesn't exist
      yet), with one checklist item per finding: `- [ ] (score NN) -- <one-line
      summary>` followed by the full "Risk if unfixed" / "Cost/risk to fix" writeup and
      the file:line range(s). Findings no longer get a global F0XX id (that scheme ended
      with the migration to Issues) -- a checklist item is identified by its issue number
      plus position/summary, which is enough since almost every issue holds only 1-2
      findings; a commit fixing one references `#NNN` (and only that issue's *last*
      finding uses `Fixes #NNN` in its commit, since that's what auto-closes the issue).
      One issue per file, not one per finding and not one shared issue -- this keeps
      each issue well under GitHub's body-size limit and independently
      searchable/closeable. If a finding spans multiple files and its primary file is in
      this batch but a secondary file was already reviewed in an earlier batch, add a
      short cross-reference note (`<short description> tracked in #NNN (<primary
      file>)`) to that earlier file's row instead of creating a second issue.
   b. For every file in this batch, change its row's Status to "Reviewed" and set Date to
      today. Set the `Issue` column to the new issue's number (`#NNN`) if this file is a
      primary file from step a; leave it blank otherwise. Set `Notes` to
      "No confirmed findings" for a file with zero findings, or to a cross-reference note
      (see step a) if this file is only a *secondary* location for someone else's
      finding -- never a comma-separated F0XX list (that content now lives in the
      issue). Each of the three per-directory tables has its own ID column (A/B/C +
      zero-padded row number, e.g. A07, B41, C169) -- never renumber or reuse an existing
      ID. If a batch file isn't already listed in that table (a new file, or the tracking
      doc doesn't exist yet), append a row for it at the end of the correct table with
      the next unused ID in that table's letter sequence, rather than skipping it
      silently.
   c. Update the `**Review timeline:**` line near the top of the doc so `last updated`
      reflects today's date.

## Notes

- `gh` **is** used, but only to create/update per-file Issues -- this still operates on
  the working tree, not a PR diff, and is unrelated to `/code-review`'s PR-review flow.
- Don't build or run the code as part of this; that's separate from the review.
- Don't fix anything as part of this skill -- it only finds and records findings. Fixing
  is a deliberate, one-at-a-time follow-up activity against a file's GitHub Issue, not
  something this skill does automatically.
- When applying a fix as that follow-up activity: **create a dedicated branch first,
  before editing anything** (e.g. `issue<NNN>-<short-description>`) -- check
  `git branch --show-current` if there's any doubt which branch is checked out. Never
  edit directly on `main` or on an unrelated in-progress branch; if a fix is started
  before this is caught, stop, create the branch, and confirm the (still-uncommitted)
  change now shows up on it before going further -- uncommitted changes move with you
  when you switch/create a branch, so this is always recoverable as long as nothing's
  been committed yet.
- Don't commit a fix immediately after making it. Leave the change (and the
  tracking-doc `last updated` bump) uncommitted so the user can review, build, and test
  first. Only commit if the user explicitly asks you to. Once a fix is committed,
  comment on its finding's Issue with the branch/PR link and tick that finding's
  checkbox; close the Issue once every checkbox in it is ticked (or let the merge
  commit's `Fixes #N` do it automatically if it's the last one).
- When a fix's branch gets its PR merged on GitHub, delete the local branch too
  (`git branch -d <branch>`) once you've confirmed the merge (e.g. `gh pr view` or
  `gh pr list --state merged`) -- GitHub deleting the remote branch on merge doesn't
  remove the local copy, and these accumulate across a long fix-by-fix review effort.
- To review the next batch, just invoke this skill again with the next set of files --
  each run is independent.
- `Documentation/CodeReviewTracking.md` is the source of truth for what's been reviewed
  and what hasn't; each file's `code-review`-labeled GitHub Issue (linked from its
  `Issue` column) is the source of truth for that file's findings and their status.
  Check the tracking doc before picking the next batch so files aren't reviewed twice or
  skipped.
