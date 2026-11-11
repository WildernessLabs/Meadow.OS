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
| `scripts/openocd_server.sh` | Starts (`reset` or `attach`) or stops a background OpenOCD on port 4242 |
| `debug-attach.cfg` | OpenOCD configuration that attaches without resetting the board |
| `gdb/clion.gdbinit` | GDB start-up commands for CLion (symbols, Meadow and NuttX scripts) |
| `gdb/STM32F777.svd` | Register descriptions for the CLion Peripherals view |
| `tools/clion/workspace.gdbinit` | Copied to `.gdbinit` in the workspace root; runs `gdb/clion.gdbinit` |
| `tools/clion/runConfigurations/` | CLion run configurations; copied to `.idea/runConfigurations/` |
| `tools/clion/customTargets.xml` | The **Meadow** custom build target used by the Embedded configurations |
| `tools/clion/tools/External Tools.xml` | The build and clean tools the **Meadow** target runs |

`.idea/` is not committed (see `.gitignore`), so these files are kept in `tools/clion/` and copied
into place.

## One-time set-up

### 1. Copy the files into place

Close the project in CLion first (CLion rewrites `.idea/` files it has open), then from the
workspace root:

```bash
mkdir -p .idea/runConfigurations .idea/tools
cp tools/clion/runConfigurations/*.xml .idea/runConfigurations/
cp tools/clion/customTargets.xml .idea/customTargets.xml
cp "tools/clion/tools/External Tools.xml" ".idea/tools/External Tools.xml"
cp tools/clion/workspace.gdbinit .gdbinit
```

The two **Embedded Meadow** configurations hold absolute paths: the OpenOCD executable and its
`tcl` directory (from `MEADOW_OPENOCD_DIR`) and the workspace's `debug.cfg` / `debug-attach.cfg`.
Correct them in **Edit Configurations...** if your paths differ.  The other configurations use
`$PROJECT_DIR$` and `MEADOW_OPENOCD_DIR`.

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

The **Debug Meadow** configurations name the **Meadow ARM** toolchain; if CLion shows a different
toolchain in one, select **Meadow ARM** in **Edit Configurations...**.

The **Embedded Meadow** configurations do not use the debug profile selector: they take the
debugger from the toolchain of the **Meadow** build target, which is **Meadow ARM**.  CLion does
not let a general-purpose debug profile be selected for them; leave the selector as it is.

## Run configurations

| Name | Folder | What it does |
|------|--------|--------------|
| Build | Build | `./build.sh` |
| Build (full clean) | Build | `./build.sh --clean --force` |
| Build (wlclean) | Build | `./build.sh --wlclean` |
| Build (debug symbols, full clean) | Build | `./build.sh --debug --clean --force` |
| Regenerate compile_commands.json | Build | `scripts/clion_compile_commands.sh` |
| Deploy (OS + runtime) | Debug | `scripts/deploy.sh` |
| Debug Meadow (reset) | Debug | Starts OpenOCD with `debug.cfg` (resets and halts the board), then attaches GDB |
| Debug Meadow (attach) | Debug | Starts OpenOCD with `debug-attach.cfg` (no reset), then attaches GDB |
| Start OpenOCD (reset) / Start OpenOCD (attach, no reset) | Debug | The Before launch steps of the two above: `scripts/openocd_server.sh` |
| Stop OpenOCD | Debug | `scripts/openocd_server.sh stop` |
| Embedded Meadow (reset) | Embedded | Embedded GDB Server: CLion runs OpenOCD with `debug.cfg` itself; has the Peripherals view |
| Embedded Meadow (attach) | Embedded | The same with `debug-attach.cfg` (no reset) |

Run the shell-script configurations with **Run**, and the **Debug Meadow** and **Embedded
Meadow** configurations with **Debug**.

The two kinds of debug configuration:

- **Debug Meadow** (GDB Remote Debug) uses the debug profile, and leaves OpenOCD running in the
  background after the session (the next start replaces it; **Stop OpenOCD** or a deploy stops
  it).  CLion has no Peripherals view for this kind of session.
- **Embedded Meadow** (Embedded GDB Server) starts and stops OpenOCD with the session and has the
  Peripherals view.  It is set never to upload the executable or reset the device (the reset and
  the flashing are left to `debug.cfg` and the deploy).  Run **Stop OpenOCD** first if a **Debug
  Meadow** session left OpenOCD running, as only one OpenOCD can use the probe.

## Deploying

Stop any debug session first, then run **Deploy (OS + runtime)**.  `scripts/deploy.sh`:

1. stops a background OpenOCD started by `scripts/openocd_server.sh`, and refuses to continue if
   another program holds port 4242;
2. disables the runtime while the old OS is still running;
3. flashes the OS with `flash.sh`;
4. resets the board over SWD and waits for the Meadow USB port;
5. writes `nuttx/Meadow.OS.Runtime.bin` with `meadow firmware write runtime -f` (without `-f` the
   CLI writes the released runtime instead), retrying once after an erase time-out;
6. deletes the staged runtime file, enables the runtime and prints the device information.  The
   OS and Runtime versions it shows should be the same.

## Debugging

1. Debug **Debug Meadow (attach)** or **Embedded Meadow (attach)** to attach to a running board,
   or the **(reset)** variant to debug from reset.  The reset variants leave the board halted at
   the first instruction; press **Resume** to boot it.
2. When `gdb/clion.gdbinit` has run, the remote sessions show `Build version: "..."` and
   `Registering NuttX GDB commands ...` in the GDB console.  The Embedded sessions do not show
   this output, but load the same symbols and commands (`dmesg` works).
3. Pause if the program shows as running.  The threads pane lists the NuttX tasks with their call
   stacks, and the Meadow and NuttX GDB commands (`dmesg`, `nx_bt <pid>`, `ps`, ...) can be typed
   in the GDB console.
4. Resume before stopping the session, so that the board is not left halted (the Meadow CLI then
   times out).

Code that runs once at start-up (for example the start of `hcom_main()`) has already run on a
board that has been attached to; use a **(reset)** configuration to stop there.

### Peripheral registers

In an **Embedded Meadow** session, open the **Peripherals** tab of the Debug tool window (turn it
on in the tool window's layout settings if it is hidden), choose **Load .svd File** and select
`gdb/STM32F777.svd`.  Select only the peripherals needed: each one shown is read over the ST-Link
at every stop.  Values can be edited only while the board is paused, and reading some registers
has side effects (a USART data register, for example).

### Notes

- The board must be running the build that `nuttx/nuttx.elf` came from.  With an older build the
  threads, variables and OpenOCD's NuttX awareness are wrong (OpenOCD logs `Unknown g_tcbinfo
  layout`).  `compare-sections .text` in the GDB console checks it.
- Breakpoints in flash use the hardware breakpoint unit, which has 8 comparators.
- `gdb/clion.gdbinit` does not send `monitor` commands, because CLion connects after it runs.
  Enter `mon gdb breakpoint_override hard` or `monitor cortex_m vector_catch all` in the GDB
  console if needed.
- gdb-dashboard is turned off for GDB started in the workspace root.  `./debug.sh` starts GDB in
  `gdb/` and is unaffected.
- `scripts/openocd_server.sh` detaches OpenOCD from the script (its own session, re-parented to
  launchd).  CLion stops every process a Before launch script started when the script ends; a
  plain background OpenOCD was shut down before GDB could connect ("could not connect: Operation
  timed out").  Its log is `$TMPDIR/meadow-openocd.log`.
- In the Embedded configurations the reset setting must be **Never** (`reset-type="NEVER"`).  An
  unrecognised value made CLion send `monitor reset` at every start, so an attach rebooted the
  board.
- An attach shortly after the board rebooted once failed with `timed out while waiting for target
  halted`; a later attempt worked.
