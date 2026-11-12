# CLion project configuration

The CLion project files in `.idea/` are committed, so they must not contain a path that
only exists on one machine. They name tools by indirection instead:

| What | Committed reference | Resolved on each machine by |
|---|---|---|
| Code model | `compile_commands.json` (compilation database project) | **Regenerate compile_commands.json** |
| Build / clean custom target `Meadow` | toolchain name `Meadow ARM`, External Tools in `.idea/tools` | CLion → Settings → Build → Toolchains |
| OpenOCD (debug server) | `$PROJECT_DIR$/scripts/clion/openocd` | `MEADOW_OPENOCD_DIR`, through `set_openocd_variables` as in `flash.sh` |
| GDB | `$PROJECT_DIR$/scripts/clion/gdb` | `arm-none-eabi-gdb` (macOS) or `gdb-multiarch` (Linux) on `PATH`, as in `debug.sh` |

## Run configurations

| Configuration | What it does |
|---|---|
| **Build**, **Build (full clean)**, **Build (wlclean)**, **Build (dual CDC, full clean)** | `build.sh` with the matching options |
| **Deploy (OS + runtime)** | `scripts/deploy.sh`: flashes the OS over SWD and writes the runtime with the `meadow` CLI |
| **Embedded Meadow (reset)** | OpenOCD with `debug.cfg` (reset, then halt), and GDB on `nuttx/nuttx.elf` |
| **Embedded Meadow (attach)** | OpenOCD with `debug-attach.cfg`: attaches to the running board without a reset |
| **Regenerate compile_commands.json** | `scripts/clion_compile_commands.sh` |

The debug configurations use an ST-Link on the board's SWD header; OpenOCD's GDB port is
4242, as in `debug.sh`.

## One-time setup per machine

1. Build the Meadow OpenOCD (`build-openocd.sh`) and set `MEADOW_OPENOCD_DIR` to its
   directory in the login shell, as `flash.sh`, `debug.sh` and `deploy.sh` already need.
2. Install the Arm GNU toolchain so that `arm-none-eabi-gcc` and `arm-none-eabi-gdb`
   (macOS) or `gdb-multiarch` (Linux) are on `PATH`. **Deploy** also needs the `meadow`
   CLI on `PATH`.
3. In CLion, add a toolchain named exactly **Meadow ARM**: System toolset, with the C and
   C++ compilers set to that machine's `arm-none-eabi-gcc` and `arm-none-eabi-g++`.
4. Open the project and run **Regenerate compile_commands.json** once.

CLion on macOS reads the login shell's environment at startup, so restart it after
changing `MEADOW_OPENOCD_DIR` or `PATH`.

## Overrides

`scripts/clion/tools.local.env` (not committed; see `tools.local.env.example`) can set
`MEADOW_OPENOCD_DIR`, or `MEADOW_GDB` to use a GDB other than the default above.

## Troubleshooting

**Debug session ends at once with "Debugger exited with code 137 (SIGKILL)".**
This is rarely a GDB problem. CLion kills GDB when `-target-select remote` times out,
which happens when OpenOCD failed to start. Check OpenOCD directly:

    scripts/clion/openocd -f debug-attach.cfg

A port-4242 conflict means another OpenOCD (a debug session, or `deploy.sh`) holds the
probe.
