# Rebasing

How to move Meadow.OS (`nuttx/` and `apps/`) onto a newer upstream NuttX release.

These notes were rewritten after the 12.12 -> 13.1 rebase (`nuttx-13.1.1-RC0` / apps
`nuttx-13.1.0`). Earlier rebases (12.5.1 to 12.12) used a plain `git subtree pull`; that method is
described under [Why not a plain subtree pull](#why-not-a-plain-subtree-pull) because it silently
damaged the tree, and the damage was only found during the 13.1 rebase. Substitute the release
numbers for the rebase you are doing.

## Overview

1. Check out the new release in the upstream clones and make a worktree for the rebase.
2. Merge `apps/` (usually clean).
3. Merge `nuttx/` with an explicit merge base (the previous release tag) and rename detection off.
4. Prune what upstream added for architectures and boards we do not carry.
5. Resolve the remaining conflicts.
6. Adapt Meadow code to upstream breaking changes (renames, removed options, API changes).
7. Audit the configuration: removed symbols, and defaults that changed silently.
8. Check ABI changes that reach code not built from this tree (Meadow.Core).
9. Build, test in the emulator against a `main` baseline, then test on a board.
10. Look for debris: files in our tree that are in no upstream release.

## Directory structure

```
OS/
+- NuttXMainline/
|  +- nuttx/        clone of apache/nuttx
|  +- apps/         clone of apache/nuttx-apps
+- Rebase/          this repository (main checkout)
+- Rebase.worktrees/
|  +- MergingNN-N/  the rebase worktree
+- Meadow.OS.Emulator/
+- runtime/
```

The names can differ; adjust the commands.

## Prepare

In each upstream clone, check out the release branch and note the exact commit and tags:

```bash
cd ../NuttXMainline/nuttx
git checkout releases/13.1 && git pull
git describe --tags; git tag --points-at HEAD     # e.g. nuttx-13.1.1-RC0
git log -1 --format=%h nuttx-12.12.0              # previous release tag
```

Do the same in `apps`. Record four commits: the old and new tags for `nuttx` and for `apps`. The
old tags must be the ones the tree was last merged from. Check with the previous merge commit:

```bash
git log --oneline --grep="Subtree merge" | head
git log -1 --format=%P <previous nuttx merge commit>   # second parent = old nuttx tag commit
```

Create the worktree and fetch both upstreams (remotes are repository-wide, so this is only needed
once):

```bash
cd Rebase
git worktree add ../Rebase.worktrees/Merging13-1 -b Merging13-1 main
git remote add local-nuttx ../NuttXMainline/nuttx
git remote add local-nuttx-apps ../NuttXMainline/apps
git fetch --no-tags local-nuttx releases/13.1
git fetch --no-tags local-nuttx-apps releases/13.1
git fetch --no-tags local-nuttx tag nuttx-12.12.0     # only if not already present
```

Builds in a worktree need the runtime location (see `Documentation/Emulator.md` and the build
section below):

```bash
export MEADOW_RUNTIME_DIR=<OS>/runtime MEADOW_RUNTIME_ALLOW_UNPINNED=1
```

## Why not a plain subtree pull

`git subtree pull --prefix=nuttx/ local-nuttx <tag>` (what this document used to describe) has two
problems:

1. **Wrong merge base.** NuttX release branches are cut from master and receive backports; one
   release tag is not an ancestor of the next. Git therefore picks an old master commit as the
   merge base, not the previous release. Every file that a backport touched then looks changed on
   both sides. In the 13.1 rebase this produced dozens of false conflicts in `sched/` (our files
   were byte-identical to 12.12), and it can also merge silently but wrongly. Check before
   merging:

   ```bash
   git merge-base HEAD <new nuttx tag commit> | xargs git log -1 --oneline
   ```

   If that is not the old release tag, do not use a plain merge.

2. **Rename detection.** We delete most of `arch/` and `boards/`. Git then "finds" renames
   between the files upstream added and the files we deleted, and places upstream files at wrong
   paths: STM32 sources under `include/nuttx/`, imx9 sources under `tools/imx9/`, x86_64 code
   under `Documentation/`, ESP32 board files under `Documentation/_extensions/`. The 12.6 and 12.12
   merges committed about 90 such files and lost two real ones (`drivers/thermal/thermal_dummy.c`
   and `sched/event/event_getmask.c`, still listed in their Make.defs). The old "Anomalies"
   step that deleted `UA` files from the Meadow board directory was cleaning up the same effect.

## Merging apps

For `apps/` the previous apps tag has so far been an ancestor of the new release, so the normal
merge is correct. Confirm, then merge:

```bash
git merge-base HEAD <new apps commit>        # must print the previous apps tag commit
git merge -Xsubtree=apps/ --no-ff --no-commit <new apps commit>
git diff --cached --name-only | cut -d/ -f1 | sort | uniq -c   # everything under apps/
git commit -m "Subtree merge NuttX Apps 13.1

git-subtree-dir: apps
git-subtree-mainline: $(git rev-parse HEAD)
git-subtree-split: <new apps commit>"
```

If the merge base is wrong, use the `merge-tree` method below with `apps` in place of `nuttx`.
To double-check a finished apps merge, recompute it with the correct base and compare trees; they
must be identical.

## Merging nuttx

Compute the merge of our `nuttx/` subtree with the new release, giving the previous release as the
base and turning rename detection off (git 2.40 or later):

```bash
OURS=$(git commit-tree -m ours-nuttx HEAD:nuttx)
git -c merge.conflictStyle=diff3 merge-tree --write-tree \
    --merge-base=<old nuttx tag commit> -Xno-renames \
    $OURS <new nuttx commit> > merge-tree.out
head -1 merge-tree.out                                   # result tree id
grep "^CONFLICT" merge-tree.out | sed -E 's/^CONFLICT \(([^)]*)\).*/\1/' | sort | uniq -c
```

For 13.1 this gave 23 content conflicts (all in files we customise) and about 7000 modify/delete
conflicts (upstream changes to arch/boards files we deleted).

Record it as a merge whose `nuttx/` is that tree:

```bash
R=$(head -1 merge-tree.out)
git merge -s ours --no-ff --no-commit <new nuttx commit>   # records the second parent
git rm -r -q --cached nuttx
git read-tree --prefix=nuttx/ $R
git checkout-index -a -f -q
git clean -fdq nuttx
```

Conflicted files are now in the index with diff3 conflict markers, and modify/delete conflicts
leave the modified side in the tree; the next two sections deal with both. Never commit before
`grep -rl '^<<<<<<<' nuttx` comes back empty.

## Pruning

Work with three file lists: base (old tag), ours (`HEAD:nuttx`), theirs (new tag):

```bash
git ls-tree -r --name-only $OURS > ours.txt
git ls-tree -r --name-only <old nuttx tag commit> > base.txt
git ls-tree -r --name-only <new nuttx commit> > theirs.txt
```

Rules:

- **Deleted by us, still upstream** (in base, not in ours): remove. These are the modify/delete
  leftovers, nearly all in `arch/` and `boards/`. Before removing anything outside `arch/` and
  `boards/`, check its history (`git log --all -- nuttx/<path>`). If no commit ever deleted it and
  a Make.defs still lists it, it was lost in an earlier merge: keep upstream's copy instead.
- **Deleted upstream, modified by us**: review each one. In 13.1 there was one,
  `stm32_appinitialize.c` (upstream removed `board_app_initialize`).
- **New upstream file** (in theirs only) under `arch/` or `boards/`: keep it only when its
  directory already exists in our tree, or when it is under a directory the build now needs.
  Everything else (new chips, new boards) goes. For 13.1 the extra keeps were
  `arch/arm/src/common/stm32/` and `boards/arm/common/stm32/`, because STM32F7 moved onto the
  common STM32 code. 3734 files were dropped.
- Print anything dropped from an unexpected place (a new subdirectory of a directory we keep) and
  look at it by hand.

What we keep, as of 13.1:

| Area | Kept |
|------|------|
| `arch/` | `arm`, `dummy`, top-level files |
| `arch/arm/src/` | `arm`, `armv7-m`, `cmake`, `common` (incl. `common/stm32`), `stm32f7` |
| `arch/arm/include/` | `arm`, `armv7-a`, `armv7-m`, `stm32f7`, top-level files |
| `boards/` | `arm/stm32f7/stm32f777zit6-meadow`, `arm/stm32f7/common` (upstream's build stubs), `arm/common/stm32`, `dummy`, top-level files |

Upstream reorganises STM32 regularly (13.x split `arch/arm/src/stm32` into per-family directories
plus `common/stm32`, and removed `arch/arm/include/stm32`, `boards/arm/stm32` and most of
`boards/arm/stm32f7/common`). Read `arch/arm/src/stm32f7/Make.defs`, its `CMakeLists.txt` and the
`source` lines of the Kconfig files to see what STM32F7 depends on before deciding what to drop.

## Resolving conflicts

Get a summary per file first. `git checkout --conflict=diff3` does not work here (the conflicts
came from `merge-tree`), but the markers already include the base section.

### Kconfig files

`arch/Kconfig`, `arch/arm/Kconfig` and `boards/Kconfig` are pruned by hand: every chip, board and
`source` line for something we do not carry is removed, because Kconfig fails on a `source` of a
missing file. Our side of these files normally only deletes text. Check that with:

```bash
git diff -U0 <old nuttx tag commit>:arch/arm/Kconfig HEAD:nuttx/arch/arm/Kconfig | grep '^+[^+]'
```

Then resolve to ours and fix up whatever upstream restructured. Things to check afterwards:

- Every `source` line resolves:
  `for f in $(grep -o 'source "[^"]*"' nuttx/arch/arm/Kconfig nuttx/boards/Kconfig | cut -d'"' -f2); do [ -f nuttx/$f ] || echo MISSING $f; done`
- No entry from upstream's new chips/boards slipped in through an auto-merged region.
- `ARCH_CHIP` resolves to `stm32f7`. In 13.1 `ARCH_CHIP_STM32` became a hidden symbol that
  `ARCH_CHIP_STM32F7` selects; the old `default "stm32" if ARCH_CHIP_STM32` line had to go or it
  would have won.
- The choice defaults name symbols that still exist.
- No symbol is defined twice (`boards/Kconfig` had a duplicate `BOARDCTL_IRQ_AFFINITY`).

kconfiglib treats remaining warnings as fatal in `tools/Unix.mk`, so the build's configure step
catches most mistakes.

### nuttx/tools

`nuttx/tools` carries Wilderness Labs changes (`build-globals.sh`, `Config.mk`, `nxstyle.c`,
`pynuttx/nxgdb/*.py`, possibly more). Do not take upstream's version wholesale. Run
`git log --oneline <previous merge>..HEAD -- nuttx/tools` to see which files carry local changes.

### nuttx/Documentation

We do not change it. Take upstream's version for any conflict there.

### Defconfigs and board files

Keep ours, then apply the breaking-change renames (next section). Options that upstream deleted
(13.1: `NSH_ARCHINIT`, `SYSTEM_TIME64`, `LIBC_LONG_LONG`, `PWM_MULTICHAN`,
`STM32F7_USE_LEGACY_PINMAP`) are removed from the defconfigs.

### syscall.csv and syscall_lookup.h

Keep the Meadow syscalls (`stm32_configgpio` and friends, `up_coherent_dcache`) and take upstream's
changes to the rest (13.1 changed `up_fork` to take a `bool`). If upstream adds a syscall that
Meadow user code already defines, the user link fails with "multiple definition" (13.1:
`mprotect`, removed from `apps/system/mono/mono_nuttx_support.c`).

## Upstream breaking changes

Upstream marks breaking changes with a `!` at the start of the commit subject. List the ones
between the two releases, ignoring other architectures:

```bash
git -C ../NuttXMainline/nuttx log --format='%h %s' <old tag>..<new tag> | grep -E '^[0-9a-f]+ !'
```

Read each one that touches arm, armv7-m, STM32/STM32F7, sched, fs, net, libc, drivers we use, or
the build. The 13.1 list that mattered:

| Change | Effect on Meadow |
|--------|------------------|
| `CONFIG_STM32F7_*` -> `CONFIG_STM32_*` (and `stm32f7_*` functions, `STM32F7_*` macros -> `stm32_*` / `STM32_*`) | defconfigs, `configuration_tweaks.sh`, F7MeadowOS, hcom, board code, our docs |
| `CONFIG_STM32F7_CUSTOM_CLOCKCONFIG` -> `CONFIG_ARCH_BOARD_STM32_CUSTOM_CLOCKCONFIG`, `QUADSPI` -> `QSPI` | special cases of the above |
| `STM32_APBx_TIMn_CLKIN` -> `STM32_TIMn_CLKIN` | DAC, frequency measurement, LSI calibration |
| `PWM_MULTICHAN` removed | `meadow-upd.c` always uses `info.channels[]`; `CONFIG_PWM_NCHANNELS` > 1 is required |
| `HAVE_LONG_LONG`, `SYSTEM_TIME64` removed; `time_t` is `int64_t` | NTP client uses the 64-bit path only |
| `BOARDIOC_INIT`, `board_app_initialize`, `NSH_ARCHINIT` removed | `stm32_appinitialize.c` deleted; Meadow uses `BOARD_LATE_INITIALIZE` |
| PIC base register r10 -> r9 | board `ARCHPICFLAGS`/`CPICFLAGS` lines dropped (they only feed NXFLAT, which Meadow does not use) |
| `fcntl.h` `O_*` values aligned with Linux | ABI change, see below |
| `sock_intf_s.si_sendmsg` takes `const struct msghdr *` | espcp `sendmsg` signature |
| `userspace_s.signal_handler` only with `ENABLE_ALL_SIGNALS` | see the configuration audit |
| `compiler.h` defines a `memory_barrier()` macro | Mono build fails; `#undef` in the runtime's `nuttx-compat.h` |
| `string.h` wraps `memset`/`memcpy`/... in always-inline `builtin_function()` wrappers when `__OPTIMIZE__` is set (not a `!` commit) | Mono functions marked `MONO_NO_OPTIMIZATION` (`optimize("O0")`) got a `udf` trap in place of the wrapper's fallback call: every interpreter icall faulted (UsageFault UNDEFINSTR in `do_icall_wrapper`), JIT was unaffected. The runtime's `nuttx-compat.h` now includes `<nuttx/compiler.h>` first and `#undef CONFIG_HAVE_BUILTIN`, guarded by `__has_include` because CMake's compiler checks force-include it without the NuttX include path |

After a runtime rebuild, count `udf` instructions in `nuttx_user.elf`
(`arm-none-eabi-objdump -d nuttx_user.elf | grep -c udf`) and compare with a build of `main`: the
legitimate ones (`__builtin_trap()` in assert paths) are the same in both, so any increase is a
new compiler-generated trap. MiniTests must be run under both JIT and the interpreter; this one
only showed under the interpreter.

### Renaming identifiers mechanically

Build the map from the two upstream trees rather than by hand: every `STM32F7_*` / `stm32f7_*`
identifier that exists in the old release, no longer exists in the new one, and whose `STM32_` /
`stm32_` form does exist in the new one. Extract `arch/arm`, `boards/arm`, `include` and
`drivers` of both releases with `git archive`, then:

```python
import os, re
def ids(root):
    out = set()
    for dp, dn, fn in os.walk(root):
        for f in fn:
            out.update(re.findall(r'\b[A-Za-z_][A-Za-z0-9_]*\b',
                                  open(os.path.join(dp, f), errors='ignore').read()))
    return {x[7:] if x.startswith('CONFIG_') else x for x in out}
old, new = ids('old'), ids('new')
rename = {}
for x in sorted(old):
    if x.startswith(('STM32F7_', 'stm32f7_')) and x not in new:
        y = ('STM32_' if x[0] == 'S' else 'stm32_') + x[8:]
        rename[x] = y if y in new else None      # None: needs a human
```

Apply it with word boundaries (`\b(CONFIG_)?(STM32F7|stm32f7)_\w+\b`) to every tracked file
except `nuttx/Documentation/` (upstream's release notes and porting guides must keep the old
names) and historical records such as `Documentation/CodeReviewTracking.md`. In 13.1: 183
replacements in 35 files. Print identifiers that are in our code but in neither map; they are
either upstream names that still exist or Meadow's own names (`hcom_file_dnld_stm32f7_*` is
Meadow's and is not matched because of the word boundary).

The same approach works for any other family-wide rename upstream makes.

## Configuration audit

`build.sh --force` regenerates `nuttx/.config` from the defconfig, and Kconfig fills in every
default. Two checks are needed.

**Symbols that no longer exist.** Collect every `config`/`menuconfig` name from all Kconfig files
in `nuttx/`, `apps/` and `F7MeadowOS/`, and every symbol referenced by the board defconfigs and
`scripts/configuration_tweaks.sh`. Run the same check on `main` and compare: Meadow's own test
flags (`*_TESTS`) are expected in both lists, and anything only in the new list was removed
upstream. Kconfig drops unknown symbols without a word, and any `#ifdef CONFIG_<removed>` in
Meadow code silently flips to the other branch (13.1: `CONFIG_PWM_MULTICHAN` in `meadow-upd.c`,
`CONFIG_HAVE_LONG_LONG` in the NTP client).

**Defaults that changed.** This board sets `CONFIG_DEFAULT_SMALL=y`, and upstream keeps adding
options whose default depends on it. Compare the generated `.config` with the one from a `main`
build, mapping renamed symbols, and read every changed value:

```python
import re
def load(p):
    d = {}
    for l in open(p):
        m = re.match(r'CONFIG_(\w+)=(.*)', l) or re.match(r'# CONFIG_(\w+) is not set', l)
        if m: d[m.group(1)] = m.group(2) if m.lastindex == 2 else 'n'
    return d
old, new = load('main/nuttx/.config'), load('worktree/nuttx/.config')
for k, v in sorted(old.items()):
    k2 = rename.get(k) or k                    # map from the previous section
    if k2 in new and new[k2] != v: print('CHANGED', k, v, '->', new[k2])
    elif k2 not in new and v != 'n': print('GONE', k, v)
```

13.1 needed these pinned in the `meadow_os` defconfig:

| Option | New default | Problem | Pinned to |
|--------|-------------|---------|-----------|
| signal support choice | `ENABLE_PARTIAL_SIGNALS` (DEFAULT_SMALL) | no user-space signal trampoline; Mono needs full signals | `CONFIG_ENABLE_ALL_SIGNALS=y` |
| `TLS_ALIGNED` | `y` for protected builds | stacks capped at 2^`TLS_LOG2_MAXSTACK` = 8 KB; Meadow uses 64 KB | `# CONFIG_TLS_ALIGNED is not set` |
| `LIBC_DISABLE_HEXSTR_TO_FLOAT` | `y` (DEFAULT_SMALL) | `strtod` loses hex floats | `# ... is not set` |
| `LIBC_TEMPBUFFER` | `n` (DEFAULT_SMALL) | see below | `CONFIG_LIBC_TEMPBUFFER=y` |

`LIBC_TEMPBUFFER` deserves a note because it did not show up in the audit (its value did not
change) and broke file downloads. With it off, `lib_get_tempbuffer()` is `alloca()`. 13.1's
`fs/inode/fs_inodesearch.c` stores such a buffer in `desc->buffer` and uses it after the
allocating function has returned, so `relpath` points at dead stack. littlefs then received
corrupted names: deploys failed with ENOENT and left a file called `System.` plus a garbage byte.
This is an upstream bug (any `LIBC_TEMPBUFFER=n` configuration); enabling the option avoids it.
Lesson: when a test fails, also look at how code paths that changed upstream behave under our
configuration, not only at changed values.

Do not run `make savedefconfig`; it rewrites `.config` and pulls in extra features.

## ABI changes outside this tree

The Mono runtime is rebuilt by `build.sh --clean` against the new headers, so header changes reach
it automatically (but read the runtime's `src/mono/cmake/nuttx-compat.h` and
`src/native/libs/System.Native/pal_*_nuttx.h` for NuttX assumptions). Meadow.Core is not rebuilt:
its F7 interop (`source/implementations/f7/Meadow.F7/Interop/`) hard-codes NuttX constants and
struct layouts. Diff the public headers between the two releases and compare with that code:

```bash
git -C ../NuttXMainline/nuttx diff <old tag> <new tag> -- include/fcntl.h include/termios.h \
    include/signal.h include/dirent.h include/sys/stat.h include/sys/types.h \
    include/nuttx/serial/tioctl.h include/nuttx/fs/ioctl.h include/sys/socket.h
```

13.1 findings:

- `O_*` open flags now use the Linux values (`O_RDONLY` 1 -> 0, `O_WRONLY` 2 -> 1, `O_RDWR` 3 -> 2,
  `O_CREAT` 4 -> 0x40, ...). Meadow.Core's `Interop.Nuttx.DriverFlags` still has the old values.
  `F7SerialPort` passes 0x83, which 13.1 reads as access mode 3 (passes the read and write checks)
  plus `O_EXCL`; it works, but `O_SYNC` is lost. `UPD.cs` passes 0, now `O_RDONLY`, which is fine
  for an ioctl-only driver.
- `struct sigevent`: `sigev_notify` and `sigev_signo` became `int`. Meadow.Core's `SigEvent` uses
  bytes; nothing calls `mq_notify`, so it is unused.
- `struct dirent` gained `d_ino`. Fine for rebuilt code.
- `termios` (`NCCS` 12), `stat`, `timespec` and the ioctl numbers we use did not change.

Meadow code inside this tree that tests access modes must use `(flags & O_ACCMODE) == O_RDONLY`;
`flags & O_RDONLY` is always 0 now.

The ESP32 wire protocol does not carry `O_*` bits (espcp sends a separate non-blocking flag), but
check `F7MeadowOS/espcp/espcp_mapping.c` whenever errno, socket option or flag values change.

## Building

```bash
./build.sh --clean --force
```

Notes:

- The Mono runtime builds first; `build-mono.sh --verbose` shows its compiler errors (`build.sh`
  only prints "error").
- `build.sh` runs `git checkout HEAD` on `F7MeadowOS/include/meadow/hcom_nuttx_shared.h` when it
  finishes. During an uncommitted merge that is harmless only because the merge does not touch the
  file; do not leave unrelated uncommitted edits in it.
- A failure in `Board.mk` names the F7MeadowOS file through the `F7MeadowOSLink` symlink; edit the
  file under `F7MeadowOS/`.
- `-Werror` is on: new upstream prototypes (`const` added, changed callback types) show up as
  `incompatible-pointer-types` errors in espcp and board code.

## Testing

### Emulator first, against a baseline

The emulator (`Documentation/Emulator.md`) runs the full deploy and MiniTests without a board.
Always compare with a `main` build run the same way: a failure only means something if `main`
passes. Build `main` in a temporary detached worktree, not in the main checkout:

```bash
git worktree add ../Rebase.worktrees/Baseline --detach main
```

For each tree:

```bash
./build.sh --emulator --clean --force          # --clean is required after a normal build
cd ../../Meadow.OS.Emulator
MEADOW_EMULATOR_OS_DIR=<tree> ./build-meadow.os-emulated.sh --config configs/meadow-os.yaml
MEADOW_EMULATOR_OS_DIR=<tree> ./run.sh --config configs/meadow-os.yaml      # in the background
meadow device info -s socket://localhost:4242
MEADOW_CORE_DIR=<Meadow.Core checkout> meadow app run tests/managed/MiniTests -s socket://localhost:4242
```

- The console (syslog) is `/tmp/renode-uart-meadow.txt`; it may still hold the previous session
  until the new one starts, so check the version in the "powered-on" line.
- The hook extraction warns about missing symbols (`qspi_waitstatusflags.part.0`,
  `espcp_spi_interface_lock`, ...). The same warnings appear on `main`.
- `meadow app run` does not exit after the tests (the app stays alive). Wait for
  `TESTRUN END` *or* for the CLI process to exit, and read the CLI output: a deploy failure ends
  with lines such as `unknown error` or `Device failed to write`, not with `TESTRUN`.
- Expected: `TESTRUN END failures=1 cases=736`, the failure being
  `exceptions.test_0_ldflda_null_pointer`, and two skips. The whole run takes a few minutes.
- After emulator testing, rebuild with `./build.sh --clean --force` before flashing a board.

### Hardware

Flash and debug as described in the `embedded-debugger` skill and `Documentation/Debugging.md`,
write the OS and the runtime together, then:

```
meadow device info
```

The OS and runtime versions must match the `Build of version ...` line of the build. Then run
MiniTests and the kernel unit tests (`Documentation/RunningDeveloperTests.md`) that cover what
the rebase touched: file system, ESP32 networking, PWM, DAC, frequency measurement.

## Checking for debris

List the files in our `nuttx/` that are in no upstream release and are not Meadow additions:

```bash
git ls-files nuttx | sed 's#^nuttx/##' | sort > merged.txt
comm -23 merged.txt <(sort theirs.txt) | comm -23 - <(sort base.txt) \
    | grep -v '^boards/arm/stm32f7/stm32f777zit6-meadow/'
```

The deliberate Meadow additions outside the board directory are `include/nuttx/kstring.h`,
`mm/kmm_string/*` and `libs/libc/misc/lib_geterrno.c` (plus a few documentation pages). Anything
else is a candidate for removal; check that no Make.defs, CMakeLists.txt or Kconfig refers to it.

`alphaTooling/check_differences.py <NuttXMainline/nuttx>` compares the content of files present
in both trees and lists those that differ; every difference it reports should be a deliberate
Meadow change. `alphaTooling/prepare_nuttx_directory.py` prepares an upstream copy for that
comparison by deleting the architectures and boards we do not carry.

## Committing

Commit the apps merge and the nuttx merge as separate commits with the usual messages
(`Subtree merge NuttX Apps 13.1`, `Subtree merge NuttX 13.1 into nuttx`), the latter including
the `git-subtree-dir`, `git-subtree-mainline` and `git-subtree-split` lines. Adaptations of Meadow
code can go in the merge commit (the tree does not build without them) or in follow-up commits.
Record any runtime change separately (runtime repository, then bump `MONO_RUNTIME_COMMIT`).
