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

### 5. Create the toolchain

**Settings -> Build, Execution, Deployment -> Toolchains**: add a toolchain named **Meadow ARM**
with C compiler `arm-none-eabi-gcc`, C++ compiler `arm-none-eabi-g++` and **Debugger** set to
**Custom GDB executable**: the path of `arm-none-eabi-gdb` (`/opt/homebrew/bin/arm-none-eabi-gdb`
with Homebrew).

The **Meadow** build target (`.idea/customTargets.xml`) uses this toolchain, and the **Embedded
Meadow** configurations take their debugger from it.  They do not use CLion's debug profile
selector in the main toolbar; leave it as it is.  (Without a GDB toolchain CLion falls back to
its bundled LLDB, which fails with `unsupported connection URL: ''`.)

## Run configurations

| Name | Folder | What it does |
|------|--------|--------------|
| Build | Build | `./build.sh` (incremental) |
| Build (full clean) | Build | `./build.sh --clean --force` |
| Build (wlclean) | Build | `./build.sh --wlclean` (cleans only the Wilderness Labs and apps objects) |
| Build (dual CDC, full clean) | Build | `./build.sh --dualcdc --clean --force` (second USB CDC/ACM port for the syslog) |
| Regenerate compile_commands.json | Build | `scripts/clion_compile_commands.sh` |
| Deploy (OS + runtime) | Debug | `scripts/deploy.sh` |
| Embedded Meadow (reset) | Embedded | Starts OpenOCD with `debug.cfg` (resets the board) and debugs from boot |
| Embedded Meadow (attach) | Embedded | Starts OpenOCD with `debug-attach.cfg` and attaches to the running board |

Run the build and deploy configurations with **Run**, and the **Embedded Meadow** configurations
with **Debug**.

`--dualcdc` changes the NuttX configuration, so it is a full build; build with **Build (full
clean)** to return to the standard configuration.

The **Embedded Meadow** configurations are of CLion's Embedded GDB Server type: CLion starts
OpenOCD with the session, connects GDB to port 4242 and stops OpenOCD when the session ends.
They are set never to upload the executable or reset the device (`download-type="NONE"`,
`reset-type="NEVER"`); the reset is done by `debug.cfg`, and flashing by the deploy.  They need
the **Meadow** build target, but do not build before launching: build and deploy first.

## Deploying

Stop any debug session first (the deploy refuses to start while port 4242 is in use), then run
**Deploy (OS + runtime)**.  `scripts/deploy.sh`:

1. disables the runtime while the old OS is still running;
2. flashes the OS with `flash.sh`;
3. resets the board over SWD and waits for the Meadow USB port;
4. writes `nuttx/Meadow.OS.Runtime.bin` with `meadow firmware write runtime -f` (without `-f` the
   CLI writes the released runtime instead), retrying once after an erase time-out;
5. deletes the staged runtime file, enables the runtime and prints the device information.  The
   OS and Runtime versions it shows should be the same.

## Debugging

1. Debug **Embedded Meadow (attach)** to attach to a running board, or **Embedded Meadow
   (reset)** to debug from reset.  The reset variant halts the board at the first instruction,
   then CLion resumes it and it boots to the first breakpoint.
2. The Embedded sessions do not show the output of `gdb/clion.gdbinit` in the GDB console, but it
   runs (GDB starts in the workspace root, so the root `.gdbinit` loads it): the user-space
   symbols and the Meadow and NuttX GDB commands are available.
3. Pause if the program shows as running.  The threads pane lists the NuttX tasks with their call
   stacks, and the Meadow and NuttX GDB commands (`dmesg`, `nx_bt <pid>`, `ps`, ...) can be typed
   in the GDB console.
4. Stop the session.  `debug.cfg` and `debug-attach.cfg` resume the core when GDB disconnects
   (`-event gdb-detach { resume }`), so the board keeps running even when the session is stopped
   while paused.  Without this CLion's Stop leaves the core halted and the Meadow CLI then times
   out.  The same applies when GDB started by `./debug.sh` quits.

Code that runs once at start-up (for example the start of `hcom_main()`) has already run on a
board that has been attached to; use **Embedded Meadow (reset)** to stop there.

### Peripheral registers

Open the **Peripherals** tab of the Debug tool window (turn it on in the tool window's layout
settings if it is hidden), choose **Load .svd File** and select `gdb/STM32F777.svd`.  Select only
the peripherals needed: each one shown is read over the ST-Link at every stop.  Values can be
edited only while the board is paused, and reading some registers has side effects (a USART data
register, for example).

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
- The reset setting of the Embedded configurations must be **Never** (`reset-type="NEVER"`).  An
  unrecognised value made CLion send `monitor reset` at every start, so an attach rebooted the
  board.
- OpenOCD's `Error: No symbols for nuttx` and `Failed to read memory at 0x00000000` at the start
  of a reset session come from its NuttX task awareness while the core is held at reset; they are
  harmless.
- An attach shortly after the board rebooted once failed with `timed out while waiting for target
  halted`; a later attempt worked.
