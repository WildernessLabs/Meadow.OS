# CLion

This document describes how to set up CLion (tested with 2026.3 on macOS) for editing, building,
deploying and debugging Meadow.OS.  CLion does not build the code itself: it reads the compiler
commands from a compilation database for code insight, and runs `build.sh`, `scripts/deploy.sh`
and OpenOCD through shell-script run configurations.

See `Debugging.md` for the debugging set-up these steps build on (OpenOCD, GDB and the Python
scripts in `gdb/`).

## Prerequisites

- The build environment described in `CLAUDE.md` (Arm toolchain on `PATH`, `MEADOW_OPENOCD_DIR`
  set to the Meadow-patched OpenOCD build) and a working `./build.sh`.
- `bear` for creating the compilation database: `brew install bear`.
- `arm-none-eabi-gdb` with Python support (the Homebrew Arm toolchain provides it).
- The Meadow CLI on `PATH`, for deploying.

## Files

| File | Purpose |
|------|---------|
| `scripts/clion_compile_commands.sh` | Full clean build under `bear`; writes `compile_commands.json` |
| `scripts/deploy.sh` | Deploys the current build: OS and Mono runtime together |
| `debug-attach.cfg` | OpenOCD configuration that attaches without resetting the board |
| `gdb/clion.gdbinit` | GDB start-up commands for CLion (symbols, Meadow and NuttX scripts) |
| `tools/clion/workspace.gdbinit` | Copied to `.gdbinit` in the workspace root; runs `gdb/clion.gdbinit` |
| `tools/clion/runConfigurations/` | CLion run configurations; copied to `.idea/runConfigurations/` |

`.idea/` is not committed (see `.gitignore`), so the run configurations are kept in
`tools/clion/` and copied into place.

## One-time set-up

### 1. Copy the files into place

From the workspace root:

```bash
mkdir -p .idea/runConfigurations
cp tools/clion/runConfigurations/*.xml .idea/runConfigurations/
cp tools/clion/workspace.gdbinit .gdbinit
```

Add these to `.git/info/exclude` so they do not show as untracked files:

```
/.gdbinit
compile_commands.json
compile_commands.raw.json
.bear/
```

### 2. Allow GDB to load the workspace `.gdbinit`

GDB only auto-loads a `.gdbinit` from the current directory when it is on the safe path.  Add this
line to `~/.gdbinit`, using the path of your workspace:

```
add-auto-load-safe-path /path/to/Rebase/.gdbinit
```

### 3. Create the compilation database

```bash
scripts/clion_compile_commands.sh
```

This runs `./build.sh --clean --force` under `bear` (options are passed on to `build.sh`, for
example `--unittests=esp`), then tidies the result:

- source paths are resolved through symbolic links, so `F7MeadowOS/` files (compiled through the
  `F7MeadowOSLink` symlink) and the NuttX `arch/arm/src/board` and `chip` links name the real
  files, in both the `file` field and the compiler arguments.  If the two do not match, CLion sees
  the source as a second input and reports "Compiler Info Errors" for those files;
- entries for configure and CMake try-compile tests are dropped.  Their directories have been
  deleted, and CLion rejects them with "Incorrect directory specified".

Run it again after any change to the build options, `defconfig` or Kconfig.  The
**Regenerate compile_commands.json** run configuration does the same from inside CLion.

### 4. Open the project

Open the workspace root in CLion, right-click `compile_commands.json` and choose **Load
Compilation Database Project**.  Use **File -> Reload All from Disk** if the run configurations do
not appear.

### 5. Create a toolchain and a debug profile

CLion 2026.3 takes the debugger for a remote debug configuration from a **debug profile**, not from
the run configuration.  Without one it starts its bundled LLDB, which fails with
`unsupported connection URL: ''`.

1. **Settings -> Build, Execution, Deployment -> Toolchains**: add a toolchain named
   **Meadow ARM** with C compiler `arm-none-eabi-gcc`, C++ compiler `arm-none-eabi-g++` and
   **Debugger** set to **Custom GDB executable**: the path of `arm-none-eabi-gdb`
   (`/opt/homebrew/bin/arm-none-eabi-gdb` with Homebrew).
2. **Settings -> Build, Execution, Deployment -> Debugger -> Debug Profiles**: **Create Profile**,
   choose a general-purpose GDB debugger that uses the **Meadow ARM** toolchain, and name it
   **Meadow ARM GDB**.
3. Select **Meadow ARM GDB** in the debug profile selector in the main toolbar.

The attach run configuration names the **Meadow ARM** toolchain; if CLion shows a different
toolchain in it, select **Meadow ARM** in **Edit Configurations...**.

## Run configurations

| Name | What it runs |
|------|--------------|
| Build | `./build.sh` |
| Build (full clean) | `./build.sh --clean --force` |
| Build (wlclean) | `./build.sh --wlclean` |
| Build (debug symbols, full clean) | `./build.sh --debug --clean --force` |
| Regenerate compile_commands.json | `scripts/clion_compile_commands.sh` |
| Deploy (OS + runtime) | `scripts/deploy.sh` |
| OpenOCD server (attach, no reset) | OpenOCD with `debug-attach.cfg` |
| OpenOCD server (reset) | `./debug.sh --server` (OpenOCD with `debug.cfg`, resets the board) |
| Attach Meadow (OpenOCD :4242) | GDB Remote Debug: `arm-none-eabi-gdb`, `target remote localhost:4242`, symbols from `nuttx/nuttx.elf` |

Run the shell-script configurations with **Run**, and the attach configuration with **Debug**.

## Deploying

Stop any OpenOCD server first (the deploy refuses to start while port 4242 is in use), then run
**Deploy (OS + runtime)**.  `scripts/deploy.sh`:

1. disables the runtime while the old OS is still running;
2. flashes the OS with `flash.sh`;
3. resets the board over SWD and waits for the Meadow USB port;
4. writes `nuttx/Meadow.OS.Runtime.bin` with `meadow firmware write runtime -f` (without `-f` the
   CLI writes the released runtime instead), retrying once after an erase time-out;
5. deletes the staged runtime file, enables the runtime and prints the device information.  The
   OS and Runtime versions it shows should be the same.

## Debugging

1. Run **OpenOCD server (attach, no reset)** to attach to a running board, or **OpenOCD server
   (reset)** to debug from reset.  With `debug_level 1` OpenOCD only prints warnings and errors;
   it is ready when it shows the `Erratum 3092511` warning and no `Error:` lines.  If it reports
   `Examination failed`, run it again.
2. Debug **Attach Meadow (OpenOCD :4242)**.  The GDB console tab shows `Build version: "..."` and
   `Registering NuttX GDB commands ...` when `gdb/clion.gdbinit` has run.
3. Pause if the program shows as running.  The threads pane lists the NuttX tasks with their call
   stacks, and the Meadow and NuttX GDB commands (`dmesg`, `nx_bt <pid>`, `ps`, ...) can be typed
   in the GDB console.
4. Resume before stopping the session, so that the board is not left halted (the Meadow CLI then
   times out).

Notes:

- The board must be running the build that `nuttx/nuttx.elf` came from.  With an older build the
  threads, variables and OpenOCD's NuttX awareness are wrong (OpenOCD logs `Unknown g_tcbinfo
  layout`).  `compare-sections .text` in the GDB console checks it.
- Breakpoints in flash use the hardware breakpoint unit, which has 8 comparators.
- `gdb/clion.gdbinit` does not send `monitor` commands, because CLion connects after it runs.
  Enter `mon gdb breakpoint_override hard` or `monitor cortex_m vector_catch all` in the GDB
  console if needed.
- gdb-dashboard is turned off for GDB started in the workspace root.  `./debug.sh` starts GDB in
  `gdb/` and is unaffected.
- An attach shortly after the board rebooted once failed with `timed out while waiting for target
  halted`; a later attempt worked.
