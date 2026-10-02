/****************************************************************************
 * F7MeadowOS/misc/meadow_dac.c
 *
 *   Copyright (C) 2026 Wilderness Labs. All rights reserved.
 *   Author:  Wilderness Labs
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/* Output on the STM32F7 DAC: static levels (issue #158) and waveforms
 * (issue #658). See Documentation/MeadowDac.md for the design.
 *
 * Channel 1 is PA4 (A00) and channel 2 is PA5 (A01). Nothing here touches
 * the DAC or those pins until meadow_dac_open() is called, and the DAC
 * clock is only on while a channel is open. For a static level there is no
 * trigger, timer, DMA or interrupt: a value written to DHR12Rx reaches the
 * output one APB1 clock later.
 *
 * A waveform is paced by TIM6, which both channels share and which runs
 * only while a waveform plays. A buffer loop is fed by DMA1 (stream 5 for
 * channel 1, stream 6 for channel 2) in circular mode from the driver's
 * own copy of the samples; triangle and noise use the DAC's generators.
 *
 * A stream (issue #662) plays a ring buffer the same way, but refills it
 * while it plays: DMA interrupts at the half-way point and the end of the
 * ring, each freeing the half just played, which the handler resets to the
 * fill value so that a half the app does not refill in time plays that
 * value instead of old samples.
 * Feeding a stream is in meadow_dac_stream.c; the definitions both files
 * share are in meadow_dac_local.h.
 *
 * A one-shot (issue #664) plays a clip once: DMA runs in normal mode instead
 * of circular, and its transfer-complete interrupt turns the channel's
 * trigger off, leaving the output at the fill value. The DMA stream and the
 * clip's buffer are released by work on the low-priority work queue, or by
 * the next DAC call, whichever comes first. A clip asked for at a different
 * rate from a waveform on the other channel is resampled to that rate,
 * since both channels share TIM6.
 *
 * The NuttX DAC driver is not used; the design document explains why.
 * NuttX has no STM32F7 DAC register header, so the few registers needed
 * are defined here.
 *
 * All calls return 0 or a negative error code, like the meadow_adc calls.
 * Errors from lower-level calls are passed back unchanged.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#include <string.h>

#include <nuttx/cache.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/wqueue.h>

#include <arch/board/board.h>

#include "arm_internal.h"
#include "chip.h"
#include "stm32_dma.h"
#include "stm32_gpio.h"
#include "stm32_rcc.h"
#include "stm32_tim.h"

#include <meadow/meadow_syscall_support.h>

#include "meadow_dac_local.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

const struct meadow_dac_chan_s g_dac_chan[MEADOW_DAC_NCHANNELS] =
{
  {
    MEADOW_DAC_PINID_PA4, MEADOW_DAC_GPIO_PA4,
    MEADOW_DAC_DHR12R1, MEADOW_DAC_DOR1, 0,
    DMAMAP_DAC1, STM32_DMA1_S5CR
  },
  {
    MEADOW_DAC_PINID_PA5, MEADOW_DAC_GPIO_PA5,
    MEADOW_DAC_DHR12R2, MEADOW_DAC_DOR2, MEADOW_DAC_CR_CH_SHIFT,
    DMAMAP_DAC2, STM32_DMA1_S6CR
  }
};

/* Protects g_dac_open and DAC_CR, which both channels share */

mutex_t g_dac_lock = NXMUTEX_INITIALIZER;

/* Bit n set = channel n is open */

static uint8_t g_dac_open;

/* The waveform on each channel, if any */

struct meadow_dac_wave_state_s g_dac_wave[MEADOW_DAC_NCHANNELS];

/* TIM6's divider, (PSC + 1) * (ARR + 1), reserved while any channel has a
 * waveform (including a stream that is still priming); 0 when none has.
 * TIM6 itself runs only once a waveform needs pulses (g_dac_tim_running).
 */

static uint32_t g_dac_tim_div;
static uint32_t g_dac_tim_psc;
static uint32_t g_dac_tim_arr;
static bool     g_dac_tim_running;

/* Queued by the DMA handler when a one-shot ends, to release its resources */

static struct work_s g_dac_oneshot_work;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void meadow_dac_oneshot_reap_locked(void);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: meadow_dac_find_chan
 *
 * Description:
 *   Return the channel index for a pin identifier, or -EBADSLT if the pin
 *   has no DAC output.
 *
 ****************************************************************************/

int meadow_dac_find_chan(uint8_t pinid)
{
  int chan;

  for (chan = 0; chan < MEADOW_DAC_NCHANNELS; chan++)
    {
      if (g_dac_chan[chan].pinid == pinid)
        {
          return chan;
        }
    }

  return -EBADSLT;
}

/****************************************************************************
 * Name: meadow_dac_lock_open_chan
 *
 * Description:
 *   Find the channel for pinid, take the driver lock, and check that the
 *   channel is open. On success the lock is held and the channel index is
 *   returned. On failure the lock is not held. Any one-shot that has ended
 *   is cleaned up first, so the caller never sees one half-finished.
 *
 ****************************************************************************/

int meadow_dac_lock_open_chan(uint8_t pinid)
{
  int chan;
  int ret;

  chan = meadow_dac_find_chan(pinid);
  if (chan < 0)
    {
      return chan;
    }

  ret = nxmutex_lock(&g_dac_lock);
  if (ret < 0)
    {
      return ret;
    }

  meadow_dac_oneshot_reap_locked();

  if ((g_dac_open & (1 << chan)) == 0)
    {
      nxmutex_unlock(&g_dac_lock);
      return -EBADF;
    }

  return chan;
}

/****************************************************************************
 * Name: meadow_dac_clock
 *
 * Description:
 *   Turn the DAC interface clock on or off. Turning it on also pulses the
 *   DAC reset, so the block starts from its reset state. Only called while
 *   no channel is open.
 *
 ****************************************************************************/

static void meadow_dac_clock(bool on)
{
  if (on)
    {
      modifyreg32(STM32_RCC_APB1ENR, 0, RCC_APB1ENR_DACEN);
      modifyreg32(STM32_RCC_APB1RSTR, 0, RCC_APB1RSTR_DACRST);
      modifyreg32(STM32_RCC_APB1RSTR, RCC_APB1RSTR_DACRST, 0);
    }
  else
    {
      modifyreg32(STM32_RCC_APB1ENR, RCC_APB1ENR_DACEN, 0);
    }
}

/****************************************************************************
 * Name: meadow_dac_rate_to_div
 *
 * Description:
 *   Choose TIM6's prescaler and reload value for a sample rate. Returns the
 *   total divider, (PSC + 1) * (ARR + 1), closest to the timer clock divided
 *   by rate_hz. rate_hz must be 1 to MEADOW_DAC_WAVE_MAX_RATE.
 *
 ****************************************************************************/

static uint32_t meadow_dac_rate_to_div(uint32_t rate_hz, uint32_t *psc,
                                       uint32_t *arr)
{
  uint32_t div = (MEADOW_DAC_TIM_CLKIN + (rate_hz / 2)) / rate_hz;
  uint32_t pscdiv;
  uint32_t arrdiv;

  /* Smallest prescaler that lets the reload value fit in 16 bits */

  pscdiv = (div + 65535) / 65536;
  arrdiv = (div + (pscdiv / 2)) / pscdiv;

  *psc = pscdiv - 1;
  *arr = arrdiv - 1;
  return pscdiv * arrdiv;
}

/****************************************************************************
 * Name: meadow_dac_tim_start / meadow_dac_tim_stop
 *
 * Description:
 *   Start TIM6 pulsing its trigger output once per sample, or stop it and
 *   turn its clock off. Called with the driver lock held.
 *
 ****************************************************************************/

static void meadow_dac_tim_start(uint32_t psc, uint32_t arr)
{
  modifyreg32(STM32_RCC_APB1ENR, 0, RCC_APB1ENR_TIM6EN);

  putreg32(0, MEADOW_DAC_TIM_CR1);
  putreg32(psc, MEADOW_DAC_TIM_PSC);
  putreg32(arr, MEADOW_DAC_TIM_ARR);

  /* Load the prescaler before selecting the update event as TRGO, so the
   * forced update does not pulse the DAC.
   */

  putreg32(0, MEADOW_DAC_TIM_CR2);
  putreg32(BTIM_EGR_UG, MEADOW_DAC_TIM_EGR);
  putreg32(BTIM_CR2_UPDT, MEADOW_DAC_TIM_CR2);
  putreg32(BTIM_CR1_ARPE | BTIM_CR1_CEN, MEADOW_DAC_TIM_CR1);
}

static void meadow_dac_tim_stop(void)
{
  putreg32(0, MEADOW_DAC_TIM_CR1);
  putreg32(0, MEADOW_DAC_TIM_CR2);
  modifyreg32(STM32_RCC_APB1ENR, RCC_APB1ENR_TIM6EN, 0);
  g_dac_tim_div = 0;
  g_dac_tim_running = false;
}

/* Start TIM6 at the reserved rate, if it is not already running */

void meadow_dac_tim_run(void)
{
  if (!g_dac_tim_running)
    {
      meadow_dac_tim_start(g_dac_tim_psc, g_dac_tim_arr);
      g_dac_tim_running = true;
    }
}

/****************************************************************************
 * Name: meadow_dac_set_mode
 *
 * Description:
 *   Replace a channel's waveform bits in DAC_CR. The channel is disabled
 *   while they change and enabled again afterwards, so the trigger and wave
 *   settings never change under a running channel. Called with the driver
 *   lock held, on an open channel.
 *
 ****************************************************************************/

void meadow_dac_set_mode(const struct meadow_dac_chan_s *chan,
                         uint32_t bits)
{
  modifyreg32(MEADOW_DAC_CR, MEADOW_DAC_CR_EN << chan->shift, 0);
  modifyreg32(MEADOW_DAC_CR, MEADOW_DAC_CR_WAVE_BITS << chan->shift,
              bits << chan->shift);
  modifyreg32(MEADOW_DAC_CR, 0, MEADOW_DAC_CR_EN << chan->shift);
}

/****************************************************************************
 * Name: meadow_dac_wave_stop_locked
 *
 * Description:
 *   Stop the waveform on a channel, if one is playing, and return it to
 *   static mode with the output left at the last value it reached. Called
 *   with the driver lock held, on an open channel.
 *
 * Returned Value:
 *   0 if a waveform was stopped, -EINVAL if none was playing.
 *
 ****************************************************************************/

static int meadow_dac_wave_stop_locked(int index)
{
  const struct meadow_dac_chan_s *chan = &g_dac_chan[index];
  struct meadow_dac_wave_state_s *wave = &g_dac_wave[index];
  int other = index ^ 1;

  if (wave->type == 0)
    {
      return -EINVAL;
    }

  /* Stop the trigger first, unless the other channel still needs it */

  if (g_dac_wave[other].type == 0)
    {
      meadow_dac_tim_stop();
    }

  /* A one-shot that has ended was already returned to static mode by its
   * DMA handler; doing it again would briefly disable the output.
   */

  if (!(wave->type == MEADOW_DAC_WAVE_ONESHOT && wave->done))
    {
      meadow_dac_set_mode(chan, 0);
    }

  if (wave->dma != NULL)
    {
      irqstate_t flags = enter_critical_section();
      stm32_dmastop(wave->dma);
      wave->type = 0;
      leave_critical_section(flags);

      stm32_dmafree(wave->dma);
      wave->dma = NULL;
    }

  if (wave->buf != NULL)
    {
      kmm_free(wave->buf);
      wave->buf = NULL;
    }

  /* Clear any DMA underrun left behind (write 1 to clear) */

  putreg32(MEADOW_DAC_SR_DMAUDR << chan->shift, MEADOW_DAC_SR);

  wave->type   = 0;
  wave->sstate = MEADOW_DAC_STREAM_NONE;
  wave->done   = false;

  /* Wake a writer waiting for space; it sees the stream has gone */

  meadow_dac_space_post(index);
  return OK;
}

/****************************************************************************
 * Name: meadow_dac_oneshot_reap_locked
 *
 * Description:
 *   Release the resources of any one-shot that has played to the end, and
 *   stop TIM6 if nothing else needs it. Called with the driver lock held,
 *   by every call that takes it and by the work its DMA handler queues.
 *
 ****************************************************************************/

static void meadow_dac_oneshot_reap_locked(void)
{
  int index;

  for (index = 0; index < MEADOW_DAC_NCHANNELS; index++)
    {
      if (g_dac_wave[index].type == MEADOW_DAC_WAVE_ONESHOT &&
          g_dac_wave[index].done)
        {
          meadow_dac_wave_stop_locked(index);
        }
    }
}

/****************************************************************************
 * Name: meadow_dac_oneshot_work
 *
 * Description:
 *   Low-priority work queued when a one-shot ends: clean it up promptly
 *   rather than waiting for the next DAC call.
 *
 ****************************************************************************/

static void meadow_dac_oneshot_work(void *arg)
{
  UNUSED(arg);

  if (nxmutex_lock(&g_dac_lock) < 0)
    {
      return;   /* The next DAC call cleans up instead */
    }

  meadow_dac_oneshot_reap_locked();
  nxmutex_unlock(&g_dac_lock);
}

/****************************************************************************
 * Name: meadow_dac_oneshot_dma
 *
 * Description:
 *   DMA callback for a one-shot: the clip has been delivered (or DMA
 *   failed). Turn the channel's trigger and DMA requests off, which leaves
 *   the fill value on the output, mark the clip done and queue its
 *   clean-up. Interrupt context.
 *
 ****************************************************************************/

static void meadow_dac_oneshot_dma(DMA_HANDLE handle, uint8_t status,
                                   void *arg)
{
  int index = (int)(uintptr_t)arg;
  struct meadow_dac_wave_state_s *st = &g_dac_wave[index];

  UNUSED(handle);
  UNUSED(status);

  /* Ignore an interrupt for a clip that was stopped or replaced */

  if (st->type != MEADOW_DAC_WAVE_ONESHOT || st->done)
    {
      return;
    }

  meadow_dac_set_mode(&g_dac_chan[index], 0);
  st->done = true;

  work_queue(LPWORK, &g_dac_oneshot_work, meadow_dac_oneshot_work, NULL, 0);
}

/****************************************************************************
 * Name: meadow_dac_mamp
 *
 * Description:
 *   Return the MAMP field for a triangle or noise amplitude, or -EINVAL if
 *   the amplitude is not one the hardware supports (2^n - 1, n = 1 to 12).
 *
 ****************************************************************************/

static int meadow_dac_mamp(uint32_t amplitude)
{
  int n;

  for (n = 1; n <= 12; n++)
    {
      if (amplitude == (1u << n) - 1)
        {
          return n - 1;
        }
    }

  return -EINVAL;
}

/****************************************************************************
 * Name: meadow_dac_copy_samples
 *
 * Description:
 *   Allocate the kernel's copy of a sample buffer, aligned to the data
 *   cache line, copy the caller's samples into it checking each one, and
 *   clean it from the data cache so DMA reads what was copied. The size is
 *   rounded up to whole cache lines, so the clean touches nothing else.
 *
 ****************************************************************************/

static int meadow_dac_copy_samples(const uint16_t *samples, uint32_t count,
                                   uint16_t **bufout)
{
  size_t    bytes;
  uint16_t *buf;
  uint32_t  i;

  bytes = count * sizeof(uint16_t);
  bytes = (bytes + MEADOW_DAC_WAVE_ALIGN - 1) &
          ~(size_t)(MEADOW_DAC_WAVE_ALIGN - 1);

  buf = kmm_memalign(MEADOW_DAC_WAVE_ALIGN, bytes);
  if (buf == NULL)
    {
      return -ENOMEM;
    }

  for (i = 0; i < count; i++)
    {
      if (samples[i] > MEADOW_DAC_MAX_VALUE)
        {
          kmm_free(buf);
          return -EINVAL;
        }

      buf[i] = samples[i];
    }

  up_clean_dcache((uintptr_t)buf, (uintptr_t)buf + bytes);

  *bufout = buf;
  return OK;
}

/****************************************************************************
 * Name: meadow_dac_copy_oneshot
 *
 * Description:
 *   Allocate and fill the kernel's copy of a one-shot clip: the samples,
 *   resampled if the channel plays at a different rate, followed by
 *   MEADOW_DAC_ONESHOT_TAIL fill values. Aligned and cleaned from the data
 *   cache as for meadow_dac_copy_samples(). Called with the driver lock
 *   held; the fill value has already been checked.
 *
 * Input Parameters:
 *   samples - The caller's clip.
 *   count   - Its length, 1 to MEADOW_DAC_WAVE_MAX_SAMPLES.
 *   rate_hz - The rate the clip was asked to play at.
 *   div     - TIM6's divider for rate_hz.
 *   outdiv  - TIM6's divider for the rate the channel will play at. If it
 *             equals div, the clip is copied as it is.
 *   fill    - The value the output is left at when the clip ends.
 *   bufout  - The kernel's copy.
 *   lenout  - Its length in samples, including the fill values.
 *
 * Returned Value:
 *   0 on success, -EINVAL for a sample above 4095 or a clip longer than
 *   MEADOW_DAC_WAVE_MAX_SAMPLES once resampled, or -ENOMEM.
 *
 ****************************************************************************/

static int meadow_dac_copy_oneshot(const uint16_t *samples, uint32_t count,
                                   uint32_t rate_hz, uint32_t div,
                                   uint32_t outdiv, uint16_t fill,
                                   uint16_t **bufout, uint32_t *lenout)
{
  uint64_t  step = 0;
  uint64_t  pos;
  uint32_t  nout;
  uint32_t  i;
  uint32_t  j;
  size_t    bytes;
  uint16_t *buf;
  int32_t   a;
  int32_t   b;

  /* Check every sample first: resampling reads them out of order */

  for (i = 0; i < count; i++)
    {
      if (samples[i] > MEADOW_DAC_MAX_VALUE)
        {
          return -EINVAL;
        }
    }

  if (outdiv == div)
    {
      nout = count;
    }
  else
    {
      /* Input samples per output sample, in 16.16 fixed point: the clip's
       * rate divided by the channel's actual rate.
       */

      step = (((uint64_t)rate_hz * outdiv) << 16) / MEADOW_DAC_TIM_CLKIN;
      if (step == 0)
        {
          return -EINVAL;
        }

      pos = ((uint64_t)(count - 1) << 16) / step + 1;
      if (pos > MEADOW_DAC_WAVE_MAX_SAMPLES)
        {
          return -EINVAL;
        }

      nout = (uint32_t)pos;
    }

  bytes = (nout + MEADOW_DAC_ONESHOT_TAIL) * sizeof(uint16_t);
  bytes = (bytes + MEADOW_DAC_WAVE_ALIGN - 1) &
          ~(size_t)(MEADOW_DAC_WAVE_ALIGN - 1);

  buf = kmm_memalign(MEADOW_DAC_WAVE_ALIGN, bytes);
  if (buf == NULL)
    {
      return -ENOMEM;
    }

  if (outdiv == div)
    {
      memcpy(buf, samples, count * sizeof(uint16_t));
    }
  else
    {
      /* Linear interpolation between neighbouring input samples */

      for (j = 0; j < nout; j++)
        {
          pos = (uint64_t)j * step;
          i   = (uint32_t)(pos >> 16);
          a   = samples[i];
          b   = (i + 1 < count) ? samples[i + 1] : a;
          buf[j] = (uint16_t)(a + (((b - a) * (int32_t)(pos & 0xffff)) /
                                   65536));
        }
    }

  for (j = nout; j < nout + MEADOW_DAC_ONESHOT_TAIL; j++)
    {
      buf[j] = fill;
    }

  up_clean_dcache((uintptr_t)buf, (uintptr_t)buf + bytes);

  *bufout = buf;
  *lenout = nout + MEADOW_DAC_ONESHOT_TAIL;
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: meadow_dac_open
 *
 * Description:
 *   Claim a DAC channel and start driving its output at 0. Opening a
 *   channel that is already open restarts it: an app that stopped without
 *   closing must not lock the next one out. Meadow.Core already refuses
 *   two users of one pin, so that is not checked here.
 *
 * Input Parameters:
 *   pinid - Pin identifier, (port << 4) | pin. PA4 or PA5.
 *   flags - MEADOW_DAC_FLAG_* bits. 0 = output buffer on.
 *
 * Returned Value:
 *   0 on success, -EBADSLT for a pin with no DAC output, -EINVAL for
 *   unknown flags, or an error passed back from a lower-level call.
 *
 ****************************************************************************/

int meadow_dac_open(uint8_t pinid, uint32_t flags)
{
  const struct meadow_dac_chan_s *chan;
  uint32_t setbits;
  int index;
  int ret;

  index = meadow_dac_find_chan(pinid);
  if (index < 0)
    {
      return index;
    }

  if ((flags & ~MEADOW_DAC_FLAG_BUFFER_OFF) != 0)
    {
      return -EINVAL;
    }

  chan = &g_dac_chan[index];

  ret = nxmutex_lock(&g_dac_lock);
  if (ret < 0)
    {
      return ret;
    }

  meadow_dac_oneshot_reap_locked();

  if (g_dac_open == 0)
    {
      meadow_dac_clock(true);
    }
  else if ((g_dac_open & (1 << index)) != 0)
    {
      /* Already open: stop any waveform and the channel before setting it
       * up again. BOFF must only change while the channel is disabled.
       */

      meadow_dac_wave_stop_locked(index);
      modifyreg32(MEADOW_DAC_CR, MEADOW_DAC_CR_EN << chan->shift, 0);
      g_dac_open &= ~(1 << index);
    }

  /* The reference manual asks for the pin to be analog before the channel
   * is enabled, to avoid parasitic current.
   */

  ret = stm32_configgpio(chan->gpio);
  if (ret < 0)
    {
      if (g_dac_open == 0)
        {
          meadow_dac_clock(false);
        }

      nxmutex_unlock(&g_dac_lock);
      return ret;
    }

  setbits = 0;
  if ((flags & MEADOW_DAC_FLAG_BUFFER_OFF) != 0)
    {
      setbits |= MEADOW_DAC_CR_BOFF;
    }

  modifyreg32(MEADOW_DAC_CR,
              (MEADOW_DAC_CR_BOFF | MEADOW_DAC_CR_TEN) << chan->shift,
              setbits << chan->shift);
  putreg32(0, chan->dhr);
  modifyreg32(MEADOW_DAC_CR, 0, MEADOW_DAC_CR_EN << chan->shift);

  g_dac_open |= (1 << index);
  nxmutex_unlock(&g_dac_lock);
  return OK;
}

/****************************************************************************
 * Name: meadow_dac_write
 *
 * Description:
 *   Set the output level of an open channel. The output settles within
 *   about 6 us; this call does not wait for it.
 *
 * Input Parameters:
 *   pinid - Pin identifier, as for meadow_dac_open().
 *   value - 0 to 4095. Larger values are rejected, not clipped.
 *
 * Returned Value:
 *   0 on success, -EBADSLT for a pin with no DAC output, -EINVAL for a
 *   value above 4095, -EBADF if the channel is not open, -EBUSY while a
 *   waveform plays, or an error passed back from a lower-level call.
 *
 ****************************************************************************/

int meadow_dac_write(uint8_t pinid, uint32_t value)
{
  int index;

  index = meadow_dac_find_chan(pinid);
  if (index < 0)
    {
      return index;
    }

  if (value > MEADOW_DAC_MAX_VALUE)
    {
      return -EINVAL;
    }

  index = meadow_dac_lock_open_chan(pinid);
  if (index < 0)
    {
      return index;
    }

  /* During a waveform the output is fed by DMA or the DAC's generator */

  if (g_dac_wave[index].type != 0)
    {
      nxmutex_unlock(&g_dac_lock);
      return -EBUSY;
    }

  putreg32(value, g_dac_chan[index].dhr);

  nxmutex_unlock(&g_dac_lock);
  return OK;
}

/****************************************************************************
 * Name: meadow_dac_read
 *
 * Description:
 *   Return the value currently on the output of an open channel, from the
 *   data output register. This is the value the DAC was given, not a
 *   measurement of the output voltage.
 *
 * Input Parameters:
 *   pinid - Pin identifier, as for meadow_dac_open().
 *   value - Where to store the value (0 to 4095).
 *
 * Returned Value:
 *   0 on success, -EBADSLT for a pin with no DAC output, -EINVAL for a
 *   NULL value pointer, -EBADF if the channel is not open, or an error
 *   passed back from a lower-level call.
 *
 ****************************************************************************/

int meadow_dac_read(uint8_t pinid, uint32_t *value)
{
  int index;

  if (value == NULL)
    {
      return -EINVAL;
    }

  index = meadow_dac_lock_open_chan(pinid);
  if (index < 0)
    {
      return index;
    }

  *value = getreg32(g_dac_chan[index].dor) & MEADOW_DAC_DATA_MASK;

  nxmutex_unlock(&g_dac_lock);
  return OK;
}

/****************************************************************************
 * Name: meadow_dac_close
 *
 * Description:
 *   Release an open channel. Any waveform is stopped first. The channel is
 *   disabled, which also stops its current draw, and the pin is left in
 *   analog mode with no pull-up or pull-down, ready for use as an ADC
 *   input. The DAC clock is turned off when neither channel is open.
 *
 * Input Parameters:
 *   pinid - Pin identifier, as for meadow_dac_open().
 *
 * Returned Value:
 *   0 on success, -EBADSLT for a pin with no DAC output, -EBADF if the
 *   channel is not open, or an error passed back from a lower-level call.
 *
 ****************************************************************************/

int meadow_dac_close(uint8_t pinid)
{
  int index;

  index = meadow_dac_lock_open_chan(pinid);
  if (index < 0)
    {
      return index;
    }

  meadow_dac_wave_stop_locked(index);
  modifyreg32(MEADOW_DAC_CR, MEADOW_DAC_CR_EN << g_dac_chan[index].shift, 0);

  g_dac_open &= ~(1 << index);
  if (g_dac_open == 0)
    {
      meadow_dac_clock(false);
    }

  nxmutex_unlock(&g_dac_lock);
  return OK;
}

/****************************************************************************
 * Name: meadow_dac_wave_start
 *
 * Description:
 *   Start a waveform on an open channel: a buffer of samples played in a
 *   loop, a stream, a one-shot clip, or the DAC's triangle or noise
 *   generator, paced by TIM6. See struct meadow_dac_wave_s. The caller's
 *   samples are copied, so the caller may reuse its buffer as soon as this
 *   returns. A one-shot replaces a one-shot already playing on the channel,
 *   and is resampled if the other channel plays at a different rate.
 *
 * Input Parameters:
 *   pinid - Pin identifier, as for meadow_dac_open().
 *   wave  - The waveform. actual_hz is set to the rate actually used.
 *
 * Returned Value:
 *   0 on success, -EBADSLT for a pin with no DAC output, -EBADF if the
 *   channel is not open, -EINVAL for a bad argument, -EBUSY if a waveform
 *   (other than a one-shot being replaced) is already playing on this
 *   channel, or the other channel plays at a different rate (except for a
 *   one-shot), -ENOMEM if the samples cannot be copied, or an error passed
 *   back from a lower-level call.
 *
 ****************************************************************************/

int meadow_dac_wave_start(uint8_t pinid, struct meadow_dac_wave_s *wave)
{
  const struct meadow_dac_chan_s *chan;
  struct meadow_dac_wave_state_s *state;
  struct meadow_dac_wave_s w;
  uint16_t *buf = NULL;
  DMA_HANDLE dma = NULL;
  uint32_t bits;
  uint32_t div;
  uint32_t outdiv;
  uint32_t len;
  uint32_t psc;
  uint32_t arr;
  int mamp = 0;
  int index;
  int ret;

  index = meadow_dac_find_chan(pinid);
  if (index < 0)
    {
      return index;
    }

  /* Work on a copy, so the caller cannot change it while it is checked */

  if (wave == NULL || wave->size != sizeof(struct meadow_dac_wave_s))
    {
      return -EINVAL;
    }

  memcpy(&w, wave, sizeof(w));

  if (w.rate_hz == 0 || w.rate_hz > MEADOW_DAC_WAVE_MAX_RATE)
    {
      return -EINVAL;
    }

  switch (w.type)
    {
      case MEADOW_DAC_WAVE_BUFFER:
        if (w.count == 0 || w.count > MEADOW_DAC_WAVE_MAX_SAMPLES ||
            w.samples == NULL)
          {
            return -EINVAL;
          }

        bits = MEADOW_DAC_CR_TEN | MEADOW_DAC_CR_DMAEN;
        break;

      case MEADOW_DAC_WAVE_TRIANGLE:
      case MEADOW_DAC_WAVE_NOISE:
        mamp = meadow_dac_mamp(w.amplitude);
        if (mamp < 0)
          {
            return mamp;
          }

        bits = MEADOW_DAC_CR_TEN | (mamp << MEADOW_DAC_CR_MAMP_SHIFT) |
               (w.type == MEADOW_DAC_WAVE_TRIANGLE ?
                MEADOW_DAC_CR_WAVE_TRI : MEADOW_DAC_CR_WAVE_NOISE);
        break;

      case MEADOW_DAC_WAVE_STREAM:
        if (w.ring == 0)
          {
            w.ring = MEADOW_DAC_STREAM_DEF_RING;
          }

        if (w.ring < MEADOW_DAC_STREAM_MIN_RING ||
            w.ring > MEADOW_DAC_STREAM_MAX_RING || (w.ring & 1) != 0 ||
            w.fill > MEADOW_DAC_MAX_VALUE)
          {
            return -EINVAL;
          }

        bits = 0;   /* Set when the stream starts playing */
        break;

      case MEADOW_DAC_WAVE_ONESHOT:
        if (w.count == 0 || w.count > MEADOW_DAC_WAVE_MAX_SAMPLES ||
            w.samples == NULL || w.fill > MEADOW_DAC_MAX_VALUE)
          {
            return -EINVAL;
          }

        bits = MEADOW_DAC_CR_TEN | MEADOW_DAC_CR_DMAEN;
        break;

      default:
        return -EINVAL;
    }

  div = meadow_dac_rate_to_div(w.rate_hz, &psc, &arr);

  index = meadow_dac_lock_open_chan(pinid);
  if (index < 0)
    {
      return index;
    }

  chan  = &g_dac_chan[index];
  state = &g_dac_wave[index];

  /* A one-shot replaces a one-shot already playing on this channel */

  if (w.type == MEADOW_DAC_WAVE_ONESHOT &&
      state->type == MEADOW_DAC_WAVE_ONESHOT)
    {
      meadow_dac_wave_stop_locked(index);
    }

  /* One waveform per channel, and both channels share TIM6's rate. A
   * one-shot asked for at another rate is resampled to TIM6's instead.
   */

  outdiv = g_dac_tim_div != 0 ? g_dac_tim_div : div;

  if (state->type != 0 ||
      (outdiv != div && w.type != MEADOW_DAC_WAVE_ONESHOT))
    {
      ret = -EBUSY;
      goto errout;
    }

  if (w.type == MEADOW_DAC_WAVE_TRIANGLE &&
      (getreg32(chan->dhr) & MEADOW_DAC_DATA_MASK) + w.amplitude >
      MEADOW_DAC_MAX_VALUE)
    {
      ret = -EINVAL;
      goto errout;
    }

  if (w.type == MEADOW_DAC_WAVE_BUFFER)
    {
      ret = meadow_dac_copy_samples(w.samples, w.count, &buf);
      if (ret < 0)
        {
          goto errout;
        }

      /* Nothing else in Meadow uses this stream (see MeadowDac.md), so
       * this does not wait.
       */

      dma = stm32_dmachannel(chan->dmamap);
      if (dma == NULL)
        {
          kmm_free(buf);
          ret = -EBUSY;
          goto errout;
        }

      /* Start the stream here rather than with stm32_dmastart(), which in
       * circular mode always enables the transfer-complete interrupt: that
       * would fire once per loop of the buffer, for nothing. Only the
       * transfer-error interrupt is enabled.
       */

      stm32_dmasetup(dma, chan->dhr, (uint32_t)(uintptr_t)buf, w.count,
                     MEADOW_DAC_DMA_SCR);
      modifyreg32(chan->dmacr, 0, DMA_SCR_TEIE | DMA_SCR_EN);
    }

  if (w.type == MEADOW_DAC_WAVE_ONESHOT)
    {
      ret = meadow_dac_copy_oneshot(w.samples, w.count, w.rate_hz, div,
                                    outdiv, (uint16_t)w.fill, &buf, &len);
      if (ret < 0)
        {
          goto errout;
        }

      dma = stm32_dmachannel(chan->dmamap);
      if (dma == NULL)
        {
          kmm_free(buf);
          ret = -EBUSY;
          goto errout;
        }

      /* Played once, in normal mode; started below, once the channel's
       * state is set, since its handler checks it.
       */

      stm32_dmasetup(dma, chan->dhr, (uint32_t)(uintptr_t)buf, len,
                     MEADOW_DAC_DMA_SCR_ONCE);
      state->done = false;
    }

  if (w.type == MEADOW_DAC_WAVE_STREAM)
    {
      ret = meadow_dac_stream_prepare(index, w.ring, (uint16_t)w.fill,
                                      &buf, &dma);
      if (ret < 0)
        {
          goto errout;
        }
    }

  /* Reserve TIM6's rate for this channel */

  if (g_dac_tim_div == 0)
    {
      g_dac_tim_div = div;
      g_dac_tim_psc = psc;
      g_dac_tim_arr = arr;
    }

  state->dma  = dma;
  state->buf  = buf;
  state->type = w.type;

  if (w.type == MEADOW_DAC_WAVE_ONESHOT)
    {
      stm32_dmastart(dma, meadow_dac_oneshot_dma,
                     (void *)(uintptr_t)index, false);
    }

  if (w.type != MEADOW_DAC_WAVE_STREAM)
    {
      meadow_dac_set_mode(chan, bits);
      meadow_dac_tim_run();
    }

  wave->actual_hz = (MEADOW_DAC_TIM_CLKIN + (outdiv / 2)) / outdiv;

  nxmutex_unlock(&g_dac_lock);
  return OK;

errout:
  nxmutex_unlock(&g_dac_lock);
  return ret;
}

/****************************************************************************
 * Name: meadow_dac_wave_stop
 *
 * Description:
 *   Stop the waveform on a channel. The channel stays open, back in static
 *   mode, with the output left at the last value it reached. TIM6 stops
 *   when neither channel plays a waveform.
 *
 * Input Parameters:
 *   pinid - Pin identifier, as for meadow_dac_open().
 *
 * Returned Value:
 *   0 on success, -EBADSLT for a pin with no DAC output, -EBADF if the
 *   channel is not open, -EINVAL if no waveform is playing, or an error
 *   passed back from a lower-level call.
 *
 ****************************************************************************/

int meadow_dac_wave_stop(uint8_t pinid)
{
  int index;
  int ret;

  index = meadow_dac_lock_open_chan(pinid);
  if (index < 0)
    {
      return index;
    }

  ret = meadow_dac_wave_stop_locked(index);

  nxmutex_unlock(&g_dac_lock);
  return ret;
}

/****************************************************************************
 * Name: meadow_dac_wave_status
 *
 * Description:
 *   Report a channel's stream state: samples queued, space free and
 *   underruns. A channel playing a one-shot reports
 *   MEADOW_DAC_ONESHOT_PLAYING and the samples left to play. Any other
 *   channel reports MEADOW_DAC_STREAM_NONE and zeros.
 *
 * Input Parameters:
 *   pinid  - Pin identifier, as for meadow_dac_open().
 *   status - Filled in; its size field must be set by the caller.
 *
 * Returned Value:
 *   0 on success, -EBADSLT for a pin with no DAC output, -EBADF if the
 *   channel is not open, -EINVAL for a NULL status or wrong size, or an
 *   error passed back from a lower-level call.
 *
 ****************************************************************************/

int meadow_dac_wave_status(uint8_t pinid,
                           struct meadow_dac_wave_status_s *status)
{
  struct meadow_dac_wave_state_s *st;
  struct meadow_dac_wave_status_s out;
  uint32_t pos;
  int index;

  index = meadow_dac_find_chan(pinid);
  if (index < 0)
    {
      return index;
    }

  if (status == NULL ||
      status->size != sizeof(struct meadow_dac_wave_status_s))
    {
      return -EINVAL;
    }

  index = meadow_dac_lock_open_chan(pinid);
  if (index < 0)
    {
      return index;
    }

  st = &g_dac_wave[index];
  memset(&out, 0, sizeof(out));
  out.size = sizeof(out);

  if (st->type == MEADOW_DAC_WAVE_STREAM)
    {
      meadow_dac_stream_status(index, &out);
    }
  else if (st->type == MEADOW_DAC_WAVE_ONESHOT)
    {
      /* Not yet ended, or the lock would have cleaned it up. The count
       * left includes the fill values after the clip.
       */

      pos        = (uint32_t)stm32_dmaresidual(st->dma);
      out.state  = MEADOW_DAC_ONESHOT_PLAYING;
      out.queued = pos > MEADOW_DAC_ONESHOT_TAIL ?
                   pos - MEADOW_DAC_ONESHOT_TAIL : 0;
    }

  memcpy(status, &out, sizeof(out));

  nxmutex_unlock(&g_dac_lock);
  return OK;
}
