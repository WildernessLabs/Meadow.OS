---
name: nuttx-add-syscall
description: Expose a kernel-space function (board driver, Meadow OS module, etc.) to user-space app code in this repo's NuttX PROTECTED build. Use whenever a link fails with "undefined reference" plus "Unknown destination type (ARM/Thumb)" / "dangerous relocation: unsupported relocation" for a function that IS defined somewhere in the tree, or when adding a new board-specific syscall from scratch.
---

# Adding a NuttX syscall (kernel <-> user boundary)

## When this applies

This board builds with `CONFIG_BUILD_PROTECTED=y`
(`nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/configs/meadow_os/defconfig`). Kernel-space
code (anything compiled into `CSRCS` in a board `src/Make.defs`, e.g. `F7MeadowOS/*`) and
user-space code (anything under `apps/`, linked into `nuttx_user.elf`/`libapps.a`) are
**separate address spaces**. A user-space file cannot call a kernel-space function by name
directly, even if both are compiled from the same source tree and the symbol resolves at
the C level -- the linker will fail with something like:

```text
arm-none-eabi-ld: ...: undefined reference to `some_kernel_function'
arm-none-eabi-ld: (some_kernel_function): Unknown destination type (ARM/Thumb) in ...
...: dangerous relocation: unsupported relocation
```

That specific pair of errors (undefined reference + "Unknown destination type
(ARM/Thumb)" + "dangerous relocation") is the signature of this exact problem. The fix is
to expose the kernel function as a syscall, not to fiddle with include paths or Make.defs
CSRCS -- the function needs a generated proxy (user-space stub that traps into the
kernel) and stub (kernel-space dispatcher), which only exist for functions registered as
syscalls.

If the board instead uses `CONFIG_BUILD_FLAT=y`, none of this applies -- kernel and user
code share one address space and a plain function call/prototype include is enough.
Check the defconfig first if unsure.

## The two files that must both change

Registering a new Meadow-specific syscall requires edits to **two separate files** --
missing either one produces a different failure, and neither failure looks like "you
forgot the other file" at first glance:

### 1. `nuttx/syscall/syscall.csv`

One row per function, comma-separated, quoted fields:

```csv
"function_name","header/that/declares/it.h","condition_or_empty_string","return_type","arg1_type","arg2_type",...
```

- `header/that/declares/it.h` -- the header containing the function's real prototype
  (e.g. `meadow/meadow_cloud.h`). The generated proxy/stub `.c` files `#include` this.
- `condition_or_empty_string` -- a `defined(CONFIG_XXX)` guard (or `!defined(...)`,
  or `""` for always-on POSIX calls), matching whatever `#if` already guards the real
  function definition and its declaration.
- Argument types should match the real prototype. `FAR` prefixes on pointer types are
  conventional in this file but not strictly required for correctness on ARM. Top-level
  `const` on a pointer parameter (`char ** const`) can be dropped in the CSV (`FAR
  char **`) -- per C, top-level parameter qualifiers don't affect function-type
  compatibility, so this is not a mismatch.

Example (from adding Meadow Cloud's three functions):

```csv
"meadow_cloud_provision","meadow/meadow_cloud.h","defined(CONFIG_MEADOW_CLOUD)","int","FAR const char *","int","FAR const char *","int","FAR void *"
"meadow_cloud_release_private_key","meadow/meadow_cloud.h","defined(CONFIG_MEADOW_CLOUD)","int","FAR char **"
"meadow_cloud_retrieve_private_key","meadow/meadow_cloud.h","defined(CONFIG_MEADOW_CLOUD)","int","FAR const char **","FAR int *"
```

Insert new rows near other Meadow-specific entries (search for `"meadow_` in the file) for
readability -- ordering has no functional effect.

**This file drives codegen.** `nuttx/syscall/Makefile` has a `.context: syscall.csv` rule
that reruns `tools/mksyscall` to regenerate every `proxies/PROXY_<name>.c` and
`stubs/STUB_<name>.c` whenever `syscall.csv`'s mtime changes. A normal `./build.sh` picks
this up automatically. If it doesn't (stale `.context` after some unusual build state),
force it: `make -C nuttx/syscall clean && make -C nuttx/syscall context`, or do a
`--clean --force` build.

### 2. `nuttx/include/sys/syscall_lookup.h`

**Hand-maintained, NOT generated from `syscall.csv`.** This file defines the `SYS_<name>`
enum values (via `sys/syscall.h`'s `enum { ... #include "syscall_lookup.h" ... }`) that
the generated proxy code calls into (`sys_callN((unsigned int)SYS_<name>, ...)`). Every
function added to `syscall.csv` needs a matching line here:

```c
SYSCALL_LOOKUP(function_name, argcount)
```

`argcount` is the number of argument types listed in the CSV row (not counting the
return type). Add it inside a `#if defined(CONFIG_XXX)` block matching the CSV row's
condition column -- reuse an existing block for that config if one already exists (e.g.
all `CONFIG_ARCH_BOARD_MEADOW_F7MICRO`-gated syscalls share one block), or add a new
`#if`/`#endif` block near the other Meadow-specific ones if the condition is new.

Example, continuing from above:

```c
#if defined(CONFIG_MEADOW_CLOUD)
SYSCALL_LOOKUP(meadow_cloud_provision, 5)
SYSCALL_LOOKUP(meadow_cloud_release_private_key, 1)
SYSCALL_LOOKUP(meadow_cloud_retrieve_private_key, 2)
#endif
```

## How to tell which file you're missing

| Symptom | Missing piece |
|---|---|
| Link error: `undefined reference to 'foo'` + "Unknown destination type (ARM/Thumb)" + "dangerous relocation" | `syscall.csv` row entirely missing, OR present but `.context` is stale (proxy/stub never regenerated) |
| Compile error in `proxies/PROXY_foo.c`: `'SYS_foo' undeclared (first use in this function)` | `syscall.csv` row exists (proxy generated fine) but the matching `SYSCALL_LOOKUP(foo, N)` line in `syscall_lookup.h` is missing |

Both files must be present and consistent (same function name, matching arg count)
before the build will link.

## Full checklist for a brand-new kernel-space function callable from user-space

1. Declare the function's prototype in a `meadow/*.h` header, guarded by the same
   `#if defined(CONFIG_XXX)` used everywhere else for this feature.
2. Implement it in a kernel-space `.c` file, added to `CSRCS` in the relevant board
   `src/Make.defs`, under the same `#if`/`#endif` guard.
3. Add a row to `nuttx/syscall/syscall.csv` (see above).
4. Add a matching `SYSCALL_LOOKUP(...)` line to `nuttx/include/sys/syscall_lookup.h`
   (see above), with the same argument count as the CSV row.
5. Rebuild. If the CSV change alone doesn't seem to regenerate proxies/stubs, force
   `nuttx/syscall`'s context step (see above).

## Background: prior art in this repo

This same kernel/user boundary issue has come up before for `up_coherent_dcache`
(needed for `dlopen` support -- see project memory `project_dynamic_loading_roadmap`)
and is already solved this way for a number of existing Meadow-specific syscalls you can
use as reference (`grep -n '"meadow_' nuttx/syscall/syscall.csv` and
`grep -n 'meadow_' nuttx/include/sys/syscall_lookup.h`), e.g. `meadow_os_reset_cycle_count`,
`meadow_adc_configure`, `stm32_configgpio`.
