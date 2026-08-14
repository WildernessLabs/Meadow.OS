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

6. Update `Documentation/CodeReviewTracking.md`:
   a. For every finding from step 5 (score 25+, not just the highest-confidence ones),
      add one row to the "Open Findings" table (near the top of the doc, above the
      per-directory tables) -- one row per finding, not per file, since a finding can
      span multiple files. Use the next sequential ID (F001, F002, ... -- check the
      existing table for the highest one used so far). Status starts as "Open"; leave
      Fixed Date and Branch/Commit blank. Include the score and file:line range(s) in the
      Files column, and fold the "Risk if unfixed" / "Cost/risk to fix" notes into the
      Summary column so the user can triage from this table alone. Don't touch existing
      rows in this table other than adding new ones.
   b. For every file in this batch, change its row's Status to "Reviewed" and set Date to
      today. Set Notes to the comma-separated list of finding IDs (from step a) whose
      Files column actually includes this specific file, e.g. "F001, F002" -- NOT a
      copy-pasted summary of the whole batch. Most files in a batch will have no findings
      at all; for those, set Notes to "No confirmed findings" rather than leaving it
      blank or reusing another file's text. Each of the three per-directory tables has
      its own ID column (A/B/C + zero-padded row number, e.g. A07, B41, C169) -- never
      renumber or reuse an existing ID. If a batch file isn't already listed in that
      table (a new file, or the tracking doc doesn't exist yet), append a row for it at
      the end of the correct table with the next unused ID in that table's letter
      sequence, rather than skipping it silently.

## Notes

- No `gh`/GitHub involvement -- this operates on the working tree, not a PR.
- Don't build or run the code as part of this; that's separate from the review.
- Don't fix anything as part of this skill -- it only finds and records findings. Fixing
  is a deliberate, one-at-a-time follow-up activity against the Open Findings table, not
  something this skill does automatically.
- To review the next batch, just invoke this skill again with the next set of files --
  each run is independent.
- `Documentation/CodeReviewTracking.md` is the source of truth for what's been reviewed,
  what hasn't, and which findings are still open. Check it before picking the next batch
  so files aren't reviewed twice or skipped.
