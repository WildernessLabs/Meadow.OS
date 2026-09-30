/****************************************************************************
 * Meadow.OS/misc/meadow_dac.c
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
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>

#include <arch/board/board.h>

#include "arm_internal.h"
#include "chip.h"
#include "stm32_dma.h"
#include "stm32_gpio.h"
#include "stm32_rcc.h"
#include "stm32_tim.h"

#include <meadow/meadow_syscall_support.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEADOW_DAC_NCHANNELS      2

/* The largest value for 12-bit right-aligned data */

#define MEADOW_DAC_MAX_VALUE      4095

/* Pin identifiers use the meadow_adc_configure() encoding:
 * (port << 4) | pin, with port A = 0.
 */

#define MEADOW_DAC_PINID_PA4      0x04
#define MEADOW_DAC_PINID_PA5      0x05

#define MEADOW_DAC_GPIO_PA4       (GPIO_ANALOG | GPIO_FLOAT | GPIO_PORTA | \
                                   GPIO_PIN4)
#define MEADOW_DAC_GPIO_PA5       (GPIO_ANALOG | GPIO_FLOAT | GPIO_PORTA | \
                                   GPIO_PIN5)

/* DAC registers (RM0410, DAC chapter) */

#define MEADOW_DAC_CR             (STM32_DAC_BASE + 0x00)
#define MEADOW_DAC_DHR12R1        (STM32_DAC_BASE + 0x08)
#define MEADOW_DAC_DHR12R2        (STM32_DAC_BASE + 0x14)
#define MEADOW_DAC_DOR1           (STM32_DAC_BASE + 0x2c)
#define MEADOW_DAC_DOR2           (STM32_DAC_BASE + 0x30)
#define MEADOW_DAC_SR             (STM32_DAC_BASE + 0x34)

/* DAC_CR bits for channel 1. The channel 2 bits are the same, 16 bits up. */

#define MEADOW_DAC_CR_EN          (1 << 0)  /* Channel enable */
#define MEADOW_DAC_CR_BOFF        (1 << 1)  /* Output buffer disable */
#define MEADOW_DAC_CR_TEN         (1 << 2)  /* Trigger enable */
#define MEADOW_DAC_CR_TSEL_MASK   (7 << 3)  /* Trigger select; 0 = TIM6 */
#define MEADOW_DAC_CR_WAVE_NOISE  (1 << 6)  /* Noise generator */
#define MEADOW_DAC_CR_WAVE_TRI    (2 << 6)  /* Triangle generator */
#define MEADOW_DAC_CR_WAVE_MASK   (3 << 6)
#define MEADOW_DAC_CR_MAMP_SHIFT  8         /* Mask / amplitude selector */
#define MEADOW_DAC_CR_MAMP_MASK   (15 << 8)
#define MEADOW_DAC_CR_DMAEN       (1 << 12) /* DMA enable */
#define MEADOW_DAC_CR_CH_SHIFT    16

/* All the DAC_CR bits a waveform sets, for one channel */

#define MEADOW_DAC_CR_WAVE_BITS   (MEADOW_DAC_CR_TEN | MEADOW_DAC_CR_TSEL_MASK | \
                                   MEADOW_DAC_CR_WAVE_MASK | \
                                   MEADOW_DAC_CR_MAMP_MASK | \
                                   MEADOW_DAC_CR_DMAEN)

/* DAC_SR DMA underrun flag for channel 1; channel 2 is 16 bits up */

#define MEADOW_DAC_SR_DMAUDR      (1 << 13)

#define MEADOW_DAC_DATA_MASK      0x0fff

/* Waveform limits */

#define MEADOW_DAC_WAVE_MAX_SAMPLES  8192
#define MEADOW_DAC_WAVE_MAX_RATE     1000000   /* The DAC's maximum update rate */

/* The kernel's copy of a sample buffer is aligned to the data cache line */

#define MEADOW_DAC_WAVE_ALIGN     32

/* DMA set-up for a buffer loop: memory to peripheral, 16-bit on both sides,
 * memory increment, circular, high priority.
 */

#define MEADOW_DAC_DMA_SCR        (DMA_SCR_DIR_M2P | DMA_SCR_MINC | \
                                   DMA_SCR_PSIZE_16BITS | \
                                   DMA_SCR_MSIZE_16BITS | DMA_SCR_CIRC | \
                                   DMA_SCR_PRIHI)

/* TIM6, the waveform trigger timer */

#define MEADOW_DAC_TIM_CLKIN      STM32_APB1_TIM6_CLKIN
#define MEADOW_DAC_TIM_CR1        (STM32_TIM6_BASE + STM32_BTIM_CR1_OFFSET)
#define MEADOW_DAC_TIM_CR2        (STM32_TIM6_BASE + STM32_BTIM_CR2_OFFSET)
#define MEADOW_DAC_TIM_EGR        (STM32_TIM6_BASE + STM32_BTIM_EGR_OFFSET)
#define MEADOW_DAC_TIM_PSC        (STM32_TIM6_BASE + STM32_BTIM_PSC_OFFSET)
#define MEADOW_DAC_TIM_ARR        (STM32_TIM6_BASE + STM32_BTIM_ARR_OFFSET)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct meadow_dac_chan_s
{
  uint8_t   pinid;  /* meadow_adc_configure() style pin identifier */
  uint32_t  gpio;   /* stm32_configgpio() pin configuration */
  uint32_t  dhr;    /* 12-bit right-aligned data holding register */
  uint32_t  dor;    /* Data output register */
  uint8_t   shift;  /* Position of this channel's bits in DAC_CR */
  uint32_t  dmamap; /* stm32_dmachannel() stream and channel */
  uint32_t  dmacr;  /* That DMA stream's configuration register */
};

/* A waveform playing on one channel */

struct meadow_dac_wave_state_s
{
  uint32_t    type;  /* MEADOW_DAC_WAVE_*, or 0 when none is playing */
  DMA_HANDLE  dma;   /* Buffer loop only: the claimed DMA stream */
  uint16_t   *buf;   /* Buffer loop only: the kernel's copy */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct meadow_dac_chan_s g_dac_chan[MEADOW_DAC_NCHANNELS] =
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

static mutex_t g_dac_lock = NXMUTEX_INITIALIZER;

/* Bit n set = channel n is open */

static uint8_t g_dac_open;

/* The waveform on each channel, if any */

static struct meadow_dac_wave_state_s g_dac_wave[MEADOW_DAC_NCHANNELS];

/* TIM6's divider, (PSC + 1) * (ARR + 1), while it runs; 0 when stopped */

static uint32_t g_dac_tim_div;

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

static int meadow_dac_find_chan(uint8_t pinid)
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
 *   returned. On failure the lock is not held.
 *
 ****************************************************************************/

static int meadow_dac_lock_open_chan(uint8_t pinid)
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

static void meadow_dac_set_mode(const struct meadow_dac_chan_s *chan,
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

  meadow_dac_set_mode(chan, 0);

  if (wave->dma != NULL)
    {
      stm32_dmastop(wave->dma);
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

  wave->type = 0;
  return OK;
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
 *   loop, or the DAC's triangle or noise generator, paced by TIM6. See
 *   struct meadow_dac_wave_s. The caller's samples are copied, so the
 *   caller may reuse its buffer as soon as this returns.
 *
 * Input Parameters:
 *   pinid - Pin identifier, as for meadow_dac_open().
 *   wave  - The waveform. actual_hz is set to the rate actually used.
 *
 * Returned Value:
 *   0 on success, -EBADSLT for a pin with no DAC output, -EBADF if the
 *   channel is not open, -EINVAL for a bad argument, -EBUSY if a waveform
 *   is already playing on this channel or the other channel plays at a
 *   different rate, -ENOMEM if the samples cannot be copied, or an error
 *   passed back from a lower-level call.
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

  /* One waveform per channel, and both channels share TIM6's rate */

  if (state->type != 0 || (g_dac_tim_div != 0 && g_dac_tim_div != div))
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

  meadow_dac_set_mode(chan, bits);

  state->type = w.type;
  state->dma  = dma;
  state->buf  = buf;

  if (g_dac_tim_div == 0)
    {
      meadow_dac_tim_start(psc, arr);
      g_dac_tim_div = div;
    }

  wave->actual_hz = (MEADOW_DAC_TIM_CLKIN + (div / 2)) / div;

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
