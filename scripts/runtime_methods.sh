#!/bin/bash
#
#   Locate the .NET Mono runtime checkout and check it against the commit
#   recorded in MONO_RUNTIME_COMMIT.  Used by build-mono.sh and
#   build-managed-assemblies.sh.
#
#   The runtime is a fork of dotnet/runtime kept outside this repository.  Its
#   location comes from MEADOW_RUNTIME_DIR, or ../runtime (relative to the
#   repository root) when MEADOW_RUNTIME_DIR is not set.
#
#   Requires $scriptdir (the repository root) and the colours and fail()
#   function of the calling script.
#
#   On return these are set:
#     RUNTIME_DIR      absolute path of the runtime checkout
#     RUNTIME_COMMIT   its HEAD commit
#     RUNTIME_TREE     its HEAD tree
#

locate_runtime_checkout() {
  RUNTIME_DIR="${MEADOW_RUNTIME_DIR:-$scriptdir/../runtime}"
  if [ ! -f "$RUNTIME_DIR/src/mono/build-nuttx.sh" ]; then
    fail "No runtime checkout at $RUNTIME_DIR.  Set MEADOW_RUNTIME_DIR to the WildernessLabs/runtime checkout."
  fi
  RUNTIME_DIR="$( cd "$RUNTIME_DIR" ; pwd -P )"

  local pin_file=$scriptdir/MONO_RUNTIME_COMMIT
  local pin_commit
  local pin_tree
  pin_commit=$(sed -n 's/^commit=//p' "$pin_file")
  pin_tree=$(sed -n 's/^tree=//p' "$pin_file")
  if [ -z "$pin_commit" ] || [ -z "$pin_tree" ]; then
    fail "Cannot read the commit and tree from $pin_file."
  fi

  RUNTIME_COMMIT=$(git -C "$RUNTIME_DIR" rev-parse HEAD 2> /dev/null)
  RUNTIME_TREE=$(git -C "$RUNTIME_DIR" rev-parse "HEAD^{tree}" 2> /dev/null)
  local runtime_changes
  runtime_changes=$(git -C "$RUNTIME_DIR" status --porcelain --untracked-files=no 2> /dev/null)

  printf "Runtime:   %s\n" "$RUNTIME_DIR"
  printf "Commit:    %s\n" "$RUNTIME_COMMIT"

  local pinned=true
  if [ "$RUNTIME_COMMIT" != "$pin_commit" ] || [ "$RUNTIME_TREE" != "$pin_tree" ]; then
    pinned=false
    printf "${red}The runtime checkout is not at the MONO_RUNTIME_COMMIT commit %s.${reset}\n" "$pin_commit"
  fi
  if [ -n "$runtime_changes" ]; then
    pinned=false
    printf "${red}The runtime checkout has local changes.${reset}\n"
  fi
  if ! $pinned; then
    if [ "${MEADOW_RUNTIME_ALLOW_UNPINNED:-0}" != "1" ]; then
      fail "Check out the pinned runtime commit, or set MEADOW_RUNTIME_ALLOW_UNPINNED=1 while developing the runtime."
    fi
    printf "${red}WARNING:${reset} building an unpinned runtime (MEADOW_RUNTIME_ALLOW_UNPINNED=1).\n"
  fi
}
