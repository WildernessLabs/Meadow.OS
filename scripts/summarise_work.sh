#!/usr/bin/env bash
#
#   summarise_work.sh - produce a narrative summary of one author's work in this
#   repository over the last N days.
#
#   The script does the mechanical part itself (working out the date range,
#   finding the commits, grouping them under the pull requests that merged them,
#   collecting diffs) and then hands the collected material to the Claude CLI,
#   which writes the prose.  Use --dry-run to see the material without calling
#   Claude.
#
#   Written for bash 3.2 so that it runs on a stock macOS install; no mapfile,
#   no associative arrays.
#

#
#   Defaults.  DAYS counts back from and includes today, so "--days 1" is today
#   only and "--days 7" is today plus the six days before it.
#
DAYS=7
AUTHOR=""
MODEL=""
OUTPUT=""
DRY_RUN="no"
INCLUDE_DIFFS="yes"
MAX_DIFF_BYTES=400000
DIFF_CONTEXT=3

SCRIPT_NAME=$(basename "$0")

usage()
{
  cat <<EOF
Usage: ${SCRIPT_NAME} [options]

Summarise the work an author has committed to this repository over a recent
window of days.  Commits are grouped under the pull requests that merged them,
and the diffs are passed to the Claude CLI so that the summary describes what
the changes actually do, not just what the commit subjects say.

Options:
  --days N              Summarise the last N days, including today (default: ${DAYS}).
  --author NAME         Author to report on.  Matched as a regular expression
                        against both the author name and the author email, so a
                        name covers every identity that person commits under.
                        Default: the current 'git config user.name'.
  --model NAME          Model to pass to the Claude CLI (default: CLI default).
  --out FILE            Write the summary to FILE instead of standard output.
  --no-diffs            Send commit subjects and file statistics only.  Faster
                        and cheaper, but the summary cannot describe the
                        substance of a fix.
  --max-diff-bytes N    Cap the total volume of diff text sent (default: ${MAX_DIFF_BYTES}).
  --dry-run             Print the prompt that would be sent and exit without
                        calling Claude.
  -h, --help            Show this help.

Examples:
  ${SCRIPT_NAME} --days 7
  ${SCRIPT_NAME} --days 30 --author peter --out /tmp/peter-month.md
  ${SCRIPT_NAME} --days 14 --dry-run | less
EOF
}

#
#   Argument parsing.
#
while [ $# -gt 0 ]; do
  case "$1" in
    --days)
      DAYS="$2"
      shift 2
      ;;
    --days=*)
      DAYS="${1#*=}"
      shift
      ;;
    --author)
      AUTHOR="$2"
      shift 2
      ;;
    --author=*)
      AUTHOR="${1#*=}"
      shift
      ;;
    --model)
      MODEL="$2"
      shift 2
      ;;
    --model=*)
      MODEL="${1#*=}"
      shift
      ;;
    --out)
      OUTPUT="$2"
      shift 2
      ;;
    --out=*)
      OUTPUT="${1#*=}"
      shift
      ;;
    --no-diffs)
      INCLUDE_DIFFS="no"
      shift
      ;;
    --max-diff-bytes)
      MAX_DIFF_BYTES="$2"
      shift 2
      ;;
    --max-diff-bytes=*)
      MAX_DIFF_BYTES="${1#*=}"
      shift
      ;;
    --dry-run)
      DRY_RUN="yes"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "${SCRIPT_NAME}: unknown option '$1'" >&2
      usage >&2
      exit 1
      ;;
  esac
done

case "${DAYS}" in
  ''|*[!0-9]*)
    echo "${SCRIPT_NAME}: --days must be a positive whole number, got '${DAYS}'" >&2
    exit 1
    ;;
esac

if [ "${DAYS}" -lt 1 ]; then
  echo "${SCRIPT_NAME}: --days must be at least 1" >&2
  exit 1
fi

if ! git rev-parse --show-toplevel > /dev/null 2>&1; then
  echo "${SCRIPT_NAME}: not inside a git repository" >&2
  exit 1
fi

REPO_ROOT=$(git rev-parse --show-toplevel)
cd "${REPO_ROOT}" || exit 1

if [ -z "${AUTHOR}" ]; then
  AUTHOR=$(git config user.name)
fi

if [ -z "${AUTHOR}" ]; then
  echo "${SCRIPT_NAME}: no author given and 'git config user.name' is not set" >&2
  exit 1
fi

if [ "${DRY_RUN}" = "no" ] && ! command -v claude > /dev/null 2>&1; then
  echo "${SCRIPT_NAME}: the 'claude' CLI is not on PATH (use --dry-run to skip it)" >&2
  exit 1
fi

#
#   Work out the first day of the window.  BSD date (macOS) and GNU date
#   (Linux) disagree about how to do relative dates, so try both.
#
OFFSET=$((DAYS - 1))
SINCE_DATE=$(date -v-${OFFSET}d "+%Y-%m-%d" 2> /dev/null)
if [ -z "${SINCE_DATE}" ]; then
  SINCE_DATE=$(date -d "${OFFSET} days ago" "+%Y-%m-%d" 2> /dev/null)
fi

if [ -z "${SINCE_DATE}" ]; then
  echo "${SCRIPT_NAME}: could not calculate the start date with this system's 'date'" >&2
  exit 1
fi

TODAY=$(date "+%Y-%m-%d")
SINCE="${SINCE_DATE} 00:00:00"

WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/summarise_work.XXXXXX") || exit 1
trap 'rm -rf "${WORK_DIR}"' EXIT

NON_MERGES="${WORK_DIR}/non_merges.txt"
MERGES="${WORK_DIR}/merges.txt"
CLAIMED="${WORK_DIR}/claimed.txt"
DATA="${WORK_DIR}/data.md"

#
#   --all is used so that work sitting on a feature branch that has not been
#   merged yet is still reported.
#
git log --all --since="${SINCE}" --author="${AUTHOR}" --no-merges --reverse \
        --pretty=%H > "${NON_MERGES}"

#
#   Only "Merge pull request" merges are used to group commits.  A routine
#   "Merge branch 'main' into <feature>" has main as its second parent, so its
#   ^1..^2 range would drag in every unrelated commit on main.
#
git log --all --since="${SINCE}" --author="${AUTHOR}" --merges --reverse \
        --pretty="%H %s" |
  grep -E ' Merge pull request #[0-9]+ ' |
  cut -d' ' -f1 > "${MERGES}"

COMMIT_COUNT=$(wc -l < "${NON_MERGES}" | tr -d ' ')
MERGE_COUNT=$(wc -l < "${MERGES}" | tr -d ' ')

if [ "${COMMIT_COUNT}" -eq 0 ] && [ "${MERGE_COUNT}" -eq 0 ]; then
  echo "No commits by '${AUTHOR}' since ${SINCE_DATE}."
  exit 0
fi

: > "${CLAIMED}"

#
#   Emit one commit: header line, then the file statistics.
#
emit_commit_stat()
{
  local sha="$1"

  git log -1 --date=short --pretty="- %h  %ad  %s" "${sha}"
  git show --stat --format= "${sha}" | sed 's/^/    /'
}

{
  echo "# Commit data for ${AUTHOR}"
  echo
  echo "- Repository: ${REPO_ROOT}"
  echo "- Current branch: $(git rev-parse --abbrev-ref HEAD)"
  echo "- Window: ${SINCE_DATE} to ${TODAY} inclusive (${DAYS} day(s))"
  echo "- Commits by this author: ${COMMIT_COUNT} non-merge, plus ${MERGE_COUNT} pull request merge(s)"
  echo

  echo "## Identities matched"
  echo
  git log --all --since="${SINCE}" --author="${AUTHOR}" --pretty="%an <%ae>" |
    sort | uniq -c | sed 's/^ */- /'
  echo

  echo "## Other authors active in the same window (excluded from the summary)"
  echo
  git log --all --since="${SINCE}" --author="${AUTHOR}" --no-merges --pretty="%an" |
    sort -u > "${WORK_DIR}/mine.txt"
  git log --all --since="${SINCE}" --no-merges --pretty="%an" |
    sort | uniq -c | sort -rn |
    awk -v mine="${WORK_DIR}/mine.txt" '
      BEGIN { while ((getline line < mine) > 0) { reported[line] = 1 } }
      {
        count = $1
        name = $0
        sub(/^[ \t]*[0-9]+[ \t]+/, "", name)
        if (!(name in reported)) { printf "- %s commit(s): %s\n", count, name; found = 1 }
      }
      END { if (!found) { print "- (None.)" } }'
  echo

  echo "## Aggregate change volume for this author (non-merge commits)"
  echo
  git log --all --since="${SINCE}" --author="${AUTHOR}" --no-merges \
          --numstat --pretty=%x00 |
    awk -F'\t' '
      NF == 3 && $1 != "-" { added += $1; deleted += $2 }
      NF == 3              { files[$3] = 1 }
      END {
        printf "- Insertions: %d\n- Deletions: %d\n- Distinct files touched: %d\n",
               added, deleted, length(files)
      }'
  echo

  echo "## Commits grouped by the pull request that merged them"
  echo

  if [ "${MERGE_COUNT}" -eq 0 ]; then
    echo "(No pull request merges by this author in the window.)"
    echo
  fi

  while read -r merge_sha; do
    [ -n "${merge_sha}" ] || continue

    merge_subject=$(git log -1 --date=short --pretty="%h  %ad  %s" "${merge_sha}")
    second_parent=$(git rev-parse --verify --quiet "${merge_sha}^2")

    echo "### ${merge_subject}"
    echo

    if [ -z "${second_parent}" ]; then
      echo "(Not a two-parent merge; no branch commits to list.)"
      echo
      continue
    fi

    #
    #   Commits on the merged branch, narrowed to the ones this author wrote
    #   inside the window so that a long-lived branch does not drag in history
    #   from outside the report.
    #
    git log --no-merges --pretty=%H "${merge_sha}^1..${merge_sha}^2" \
        > "${WORK_DIR}/branch.txt"
    grep -F -x -f "${NON_MERGES}" "${WORK_DIR}/branch.txt" \
        > "${WORK_DIR}/branch_in_window.txt" 2> /dev/null

    if [ ! -s "${WORK_DIR}/branch_in_window.txt" ]; then
      echo "(No commits by this author in the window on this branch.)"
      echo
      continue
    fi

    while read -r sha; do
      [ -n "${sha}" ] || continue
      emit_commit_stat "${sha}"
    done < "${WORK_DIR}/branch_in_window.txt"
    echo

    cat "${WORK_DIR}/branch_in_window.txt" >> "${CLAIMED}"
  done < "${MERGES}"

  echo "## Commits not reached by a pull request merge by this author"
  echo
  echo "(Merged by somebody else, pushed direct, or still sitting on a branch."
  echo "The branch list under each commit says which.)"
  echo

  grep -F -x -v -f "${CLAIMED}" "${NON_MERGES}" > "${WORK_DIR}/unclaimed.txt" 2> /dev/null
  if [ ! -s "${CLAIMED}" ]; then
    cp "${NON_MERGES}" "${WORK_DIR}/unclaimed.txt"
  fi

  if [ -s "${WORK_DIR}/unclaimed.txt" ]; then
    while read -r sha; do
      [ -n "${sha}" ] || continue
      emit_commit_stat "${sha}"
      branches=$(git branch -a --contains "${sha}" 2> /dev/null |
                 sed 's/^[* ]*//' | tr '\n' ',' | sed 's/,$//')
      echo "    branches: ${branches}"
    done < "${WORK_DIR}/unclaimed.txt"
  else
    echo "(None.)"
  fi
  echo

  if [ "${INCLUDE_DIFFS}" = "yes" ]; then
    echo "## Diffs"
    echo
    echo "Truncated where a commit is large; the file statistics above are complete."
    echo

    used=0
    truncated=0
    while read -r sha; do
      [ -n "${sha}" ] || continue

      git show --format= -U${DIFF_CONTEXT} "${sha}" > "${WORK_DIR}/diff.txt"
      size=$(wc -c < "${WORK_DIR}/diff.txt" | tr -d ' ')

      echo "### $(git log -1 --pretty='%h %s' "${sha}")"
      echo
      if [ $((used + size)) -gt "${MAX_DIFF_BYTES}" ]; then
        truncated=1
        echo '```'
        echo "(diff omitted: ${size} bytes, budget of ${MAX_DIFF_BYTES} bytes exhausted)"
        echo '```'
      else
        used=$((used + size))
        echo '```diff'
        cat "${WORK_DIR}/diff.txt"
        echo '```'
      fi
      echo
    done < "${NON_MERGES}"

    if [ "${truncated}" -eq 1 ]; then
      echo "Note: some diffs were omitted because of the size budget."
      echo
    fi
  else
    echo "## Diffs"
    echo
    echo "(Omitted: --no-diffs was given.  Describe the commits from their"
    echo "subjects and file statistics, and say nothing about the substance of a"
    echo "change that those do not support.)"
    echo
  fi
} > "${DATA}"

#
#   Build the prompt.  Everything the model needs is in the data block; it is
#   told not to go looking for more.
#
PROMPT="${WORK_DIR}/prompt.md"

{
  cat <<EOF
You are writing a work summary for a software engineer.  Everything you need is
in the COMMIT DATA block below, which was extracted from the git repository at
${REPO_ROOT}.  Do not use any tools and do not ask any questions; write the
summary directly from the data provided.

Summarise the work committed by ${AUTHOR} between ${SINCE_DATE} and ${TODAY}
inclusive.

Write GitHub-flavoured Markdown, in this shape:

1.  A level-2 heading naming the author and the date range.
2.  A short opening paragraph giving the commit count, the aggregate insertions,
    deletions and distinct file count, and one sentence naming the dominant
    theme of the work.  If other authors were active in the same window, say in
    one clause that their commits are excluded.
3.  One level-3 section per pull request, headed with the PR number and branch
    name, in the order the data presents them.  Under each, use bullets that
    say what the change does and why it matters, not what the commit subject
    says.  Read the diffs: when a commit fixes a real defect, name the function
    and file and describe the defect concretely (for example "the pin table was
    selected inside a success branch, so a failure left the wrong table in
    place").  Put the substantive bug fixes before the cosmetic ones.
4.  A final level-3 section for any commits that no pull request merge reached,
    headed with the branch name, saying whether they are merged (the branch list
    includes main) or still outstanding.

If the data contains no pull request merges at all, drop rule 3 and organise the
body into level-3 sections by theme instead, one per coherent area of work.

Style rules:

-  ASCII characters only.  No em dashes, no smart quotes, no arrows, no box
   drawing.  Use "->" and "--" if you need them.
-  Refer to files by their repository-relative path, and to functions by name
   with parentheses.
-  Be specific and factual.  Every claim must be supported by the data below.
   Do not speculate about intent, do not pad, and do not invent testing or
   review activity that the commits do not show.
-  Group trivial mechanical changes (formatting, renames, dead code removal)
   into a single bullet rather than listing each one.
-  Use they/them for any person whose pronouns are not stated.
-  Output only the summary.  No preamble, no sign off.

===== COMMIT DATA =====

EOF
  cat "${DATA}"
} > "${PROMPT}"

if [ "${DRY_RUN}" = "yes" ]; then
  cat "${PROMPT}"
  exit 0
fi

#
#   Hand it to Claude.  Tools are disabled because the prompt is self contained.
#
if [ -n "${MODEL}" ]; then
  set -- -p --allowed-tools "" --model "${MODEL}"
else
  set -- -p --allowed-tools ""
fi

echo "${SCRIPT_NAME}: summarising ${COMMIT_COUNT} commit(s) by ${AUTHOR} from ${SINCE_DATE} to ${TODAY}..." >&2

if [ -n "${OUTPUT}" ]; then
  if claude "$@" < "${PROMPT}" > "${OUTPUT}"; then
    echo "${SCRIPT_NAME}: written to ${OUTPUT}" >&2
  else
    echo "${SCRIPT_NAME}: the claude CLI failed" >&2
    exit 1
  fi
else
  if ! claude "$@" < "${PROMPT}"; then
    echo "${SCRIPT_NAME}: the claude CLI failed" >&2
    exit 1
  fi
fi
