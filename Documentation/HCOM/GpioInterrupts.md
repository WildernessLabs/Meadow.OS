# The GPIO Interrupt Service

## Overview

`meadow_interrupt.c` (`F7MeadowOS/meadow_os/`) lets managed code receive GPIO pin interrupts. It is not part of
HCOM, but it is part of what makes Meadow.OS work, and it uses the same cross-boundary patterns: a `/dev/upd`
ioctl in, and a message queue out. The kernel does the time-critical work in interrupt handlers (edge
detection, glitch filtering and debouncing) and posts a two-byte message for each interrupt that survives. The
Mono runtime reads those messages and raises the managed `IDigitalInterruptPort` events.

```
    user space (Mono runtime)                      kernel
   +-----------------------------+      +-----------------------------------------------+
   | configure a pin:            |      |  mint_config_interrupt()                      |
   |   MUPD_REGISTER_GPIO_IRQ  --+-ioctl-> allocate state, stm32_gpiosetevent()          |
   |   (struct mint_gpio_int_    |      |                                               |
   |    config)                  |      |  pin edge  -> EXTI interrupt                  |
   |                             |      |     no filter  -> mint_gpio_no_delay_isr()    |--+
   |                             |      |     filter     -> mint_gpio_need_delay_isr()  |  |
   |                             |      |                      | start TIM7 (100 us)    |  |
   |                             |      |                      v                        |  |
   |                             |      |        mint_isr_periodic() counts glitch /    |  |
   |                             |      |        debounce time, then forwards           |--+
   |  mq "/mdw_int" reader  <------------- file_mq_send(&_mint_mq, {pin, state})        |
   +-----------------------------+      +-----------------------------------------------+
```

## Configuring a Pin

The runtime calls `ioctl(/dev/upd, MUPD_REGISTER_GPIO_IRQ, &cfg)`, which `upd_ioctl()` passes to
`mint_config_interrupt()` ([UpdInterface.md](UpdInterface.md)). The structure
(`struct mint_gpio_int_config`, `meadow_interrupt.h`; it must match the managed `Interop.upd.cs`):

| Field | Meaning |
|-------|---------|
| `port` | 0 to 10, GPIO port A to K |
| `pin` | 0 to 15 |
| `configType` | 0 = remove, 1 = add, 2 = low-power wake-up |
| `risingEdge`, `fallingEdge` | 1 to react to that edge |
| `resistorMode` | 0 = float, 1 = pull-up, 2 = pull-down |
| `debounceDuration` | Debounce time in units of 100 microseconds (10 = 1 ms) |
| `glitchDuration` | Glitch-filter time in units of 100 microseconds |

A pin is identified by one byte: port in the upper four bits, pin in the lower four (PD9 is 0x39). Checks, all
returning `-EINVAL` with a syslog message: a null pointer; a port above 10 or a pin above 15; an add or wake-up with
neither edge selected; a **wake-up with a debounce or glitch time**, because the clocks that would time it are
stopped in Stop mode. After those checks:

- **Remove** a pin that is not configured: `-ENODATA`.
- **Add** a pin that is already configured: `-ENOTEMPTY`. Re-adding would orphan the earlier state.
- More than **32** pins (`MEADOW_INTERRUPT_MAX_SUPPORTED_GPIOS`): `-ENOSPC`.
- Allocation failure: `-ENOMEM`.
- A failure to set up the hardware interrupt undoes the registration, so the pin can be tried again.

One mutex, `_mintConfigLock`, serialises the whole function, so two callers cannot register the same pin or race
in the first-call set-up.

## The Four Modes

The glitch and debounce times select how a pin is handled. The code's own note: *debounce recognises the first
state change and then ignores everything for a period; glitch filtering ignores the first change, waits, and then
checks that the state is stable.*

| `glitchDuration` | `debounceDuration` | Mode | ISR | Notification |
|------------------|--------------------|------|-----|--------------|
| 0 | 0 | No delay (also used for wake-up) | `mint_gpio_no_delay_isr` | Immediately, with the pin state read in the interrupt |
| 0 | > 0 | Debounce | `mint_gpio_need_delay_isr` | At the first timer tick, then the pin is deaf for the debounce time |
| > 0 | 0 | Glitch | `mint_gpio_need_delay_isr` | When the state has stayed the same for the glitch time |
| > 0 | > 0 | Glitch then debounce | `mint_gpio_need_delay_isr` | When the glitch time ends, then the pin is deaf for the debounce time |

Behaviour that follows from the design:

- **The state in the message is sometimes a guess.** The MCU does not tell the interrupt handler which level
  caused the interrupt, and the pin may have changed since. With no filter the pin is read at once. With debounce
  alone, a rising-only pin is reported as 1, a falling-only pin as 0, and a "both" pin as the opposite of the last
  known state. The code states that perfect debounce is not possible for this reason. Only the glitch filter reads
  the real level, after it has been stable.
- **Glitch mode always asks the hardware for both edges**, whatever the configuration says, so it can follow the
  level; the configured edges are applied when deciding whether to notify.
- **A glitch is dropped silently.** If the stable level equals the last known level, no message is sent.
- **Wake-up pins** are no-delay pins whose handler, instead of sending a message, calls
  `pwrmgmt_isr_gpio_wakeup_code()`, which restarts the clocks if the device is in Stop mode
  ([PlatformState.md](PlatformState.md) and the power-management documents). No message is sent for a wake-up,
  and a wake-up interrupt when the device is not in Stop mode does nothing.
- **Interrupts that arrive during filtering are ignored.** A pin in the glitch or debounce state
  is not "waiting for a change", so its handler returns at once.

### The state machine

Each pin has a small record (`struct interruptPinMap_s`) with a state:

```
   uncfg --configure--> wait_gpio_chg --edge ISR--> mon_glitch ----stable----> mon_debounce --timeout--+
                              ^   |                    ^  | state changed:                            |
                              |   |                    +--+ restart the count                         |
                              |   +--edge ISR (debounce only)--------> mon_debounce --timeout--+     |
                              +----------------------------------------------------------------+-----+
   no-delay pins stay in mon_no_delay for life
```

### The timer

The filters are timed by **TIM7**, a basic timer with no pins, set up once at the first configuration to
interrupt every 0.1 ms (10 kHz). It runs only while at least one pin is being filtered: the pin's ISR adds it to
a list of pins being timed (`_allGpiosBeingTimed`) and switches the timer on; `mint_isr_periodic()` visits
every pin on the list at each tick, and when the list is empty it switches the timer off.

- **Debounce** counts ticks down from `debounceDuration`.
- **Glitch**, for times up to 20 ms (`glitchDuration` 200 or less): counts ticks, re-reading the pin at every tick and
  restarting the count whenever the level changes. For longer times it reads the pin only each millisecond and
  measures elapsed time with the system clock, because a count of 100 microsecond ticks accumulates error over long
  periods.

TIM6, the other basic timer, once served the idle monitor and is now the DAC trigger timer.

## The Message Queue

The service posts to a POSIX message queue, `/mdw_int` (`MINT_MSG_QUEUE_NAME`), created by
`meadow_interrupt_setup()` at start-up (step 19, [KernelStartup.md](KernelStartup.md)) with room for 16
messages (`MINT_MSG_QUEUE_MAX_MSGS`). The runtime opens the other end. Each message is two bytes
(`mint_send_int_core_t`, packed):

| Byte | Meaning |
|------|---------|
| 0 | Pin id (port in the upper four bits, pin in the lower four) |
| 1 | Pin state, 0 or 1 |

A time-stamp field exists behind `MEADOW_INTERRUPT_INCLUDE_TIME_STAMP`, set to 0. The header explains why it is
off: the managed side, in another repository, would have to be updated first to expect the larger message.

### Sending from an interrupt

Messages are sent from interrupt handlers, so from whichever task happened to be running. That determined the
design:

- The queue is kept as a `struct file` and sent to with `file_mq_send()`. The ordinary `mq_send()` takes a queue
  descriptor, which NuttX looks up in the *calling task's* descriptor table. In an interrupt that task is
  unrelated to the one that opened the queue, and the lookup failed intermittently with `EBADF`. `file_mq_send()`
  uses the `struct file` directly and so works from any context. It also returns a negative error number instead
  of setting `errno`, which would be some unrelated thread's `errno` inside an interrupt. (The kernel-to-user
  channel in [TraceSyslog.md](TraceSyslog.md) uses the same technique.)
- **The pool is shared.** Message bodies come from NuttX's preallocated pools, `CONFIG_PREALLOC_MQ_MSGS` (8) and
  `CONFIG_PREALLOC_MQ_IRQ_MSGS` (8), for the whole system, not per queue. If a burst of interrupts empties them,
  `file_mq_send()` fails with `-ENOMEM`, the message is **lost**, and "MQ message pool is empty" is logged. To
  allow deeper bursts, raise `CONFIG_PREALLOC_MQ_IRQ_MSGS`, not `MINT_MSG_QUEUE_MAX_MSGS`.
- A full queue or any other send error is logged and the message is lost; nothing retries.

## Concurrency

| Data | Protection |
|------|------------|
| The pin table and the first-call set-up | `_mintConfigLock` (a mutex), held for the whole of `mint_config_interrupt()` |
| `_allGpiosBeingTimed` (add and remove) | `enter_critical_section()` in both functions |
| `_allGpiosBeingTimed` (scan by TIM7's handler) | None, deliberately: the array is `volatile`, and a concurrent change can at worst make one entry run a tick early or late |
| A pin's state record | Written by the configuration code only before the interrupt is armed, and freed only after the interrupt is disarmed and the pin removed from the timed list |

Removing a pin first disarms the hardware interrupt (`stm32_gpiosetevent()` with no handler), then takes the pin off
the timed list, then frees its record.

## Diagnostics and Tests

- **Logic-analyser pins.** With `USE_MEADOW_DEBUG_HELPERS` defined, four debug pins show activity: A0 high inside
  the periodic handler, A1 inside the no-delay handler, A2 inside the filtered-pin handler, A3 while a pin is
  being filtered.
- **Syslog.** `MEADOW_INTERRUPT_INCLUDE_DIAGNOSTIC_SYSLOG` (0 normally) enables messages: 1 for configuration and
  notifications, 2 for everything. They are written from interrupt handlers, so switch them on only deliberately.
- **Test.** Developer test 16 (`MEADOW_TEST_INTERRUPT`, build with `--unittests=mint`) runs the service from the
  kernel ([DeveloperCommandsNsh.md](DeveloperCommandsNsh.md)); `meadow_interrupt_tests.c` calls
  `mint_config_interrupt()` directly and reads the queue.

## Things to Know

- **Interrupts can be lost** when the shared message pool is empty or the queue is full; the only trace is a syslog
  line.
- **Debounce-only and no-delay states can be wrong.** See the modes above.
- **Errors in the handlers use `syslog()`.** The handlers log errors with it. The "invalid state" paths wait 20 ms
  with `up_udelay()` first, then assert or panic, so a coding error here resets the board.
- **A failed timer set-up is not retried.** The first call sets `_firstTimeConfig` false before preparing TIM7. If
  that preparation fails, the call returns the error but later calls skip it, and filtered pins would never be
  timed.
- **An illegal `resistorMode`** is logged but the pin is configured as a floating input and the call succeeds.
- **Pointers are trusted.** `cfg` comes from the ioctl argument and is read directly ([UpdInterface.md](UpdInterface.md)).
- **Dead code.** `meadow_interrupt_shutdown()` has no callers, and `_totalGpiosCanBeTimed` is counted but never read.
- **Pins that the runtime never releases** stay configured until the device restarts; there is no "remove all".

## Source Map

| Concern | File |
|---------|------|
| The service | `F7MeadowOS/meadow_os/meadow_interrupt.c` |
| Configuration structure, message format, queue name | `F7MeadowOS/include/meadow/meadow_interrupt.h` |
| The ioctl entry | `F7MeadowOS/meadow_os/meadow-upd.c`, `F7MeadowOS/include/meadow/meadow-upd.h` (`MUPD_REGISTER_GPIO_IRQ`) |
| Stop-mode wake-up hook | `F7MeadowOS/pwrmgmt/pwrmgmt_enter_stop_mode.c` (`pwrmgmt_isr_gpio_wakeup_code()`) |
| Start-up call | `F7MeadowOS/hcom_nx/hcom_nx_startup_mgr.c` |
| Tests | `F7MeadowOS/kerneltests/meadow_interrupt_tests.c` |
| Pool sizes | `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/configs/meadow_os/defconfig` |

## Related Documents

[KernelStartup.md](KernelStartup.md), [UpdInterface.md](UpdInterface.md), [TraceSyslog.md](TraceSyslog.md),
[PlatformState.md](PlatformState.md), [DeveloperCommandsNsh.md](DeveloperCommandsNsh.md),
[RunningDeveloperTests.md](../RunningDeveloperTests.md).
