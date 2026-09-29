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

/* Static-level output on the STM32F7 DAC (issue #158). See
 * Documentation/MeadowDac.md for the design.
 *
 * Channel 1 is PA4 (A00) and channel 2 is PA5 (A01). Nothing here touches
 * the DAC or those pins until meadow_dac_open() is called, and the DAC
 * clock is only on while a channel is open. There is no trigger, timer,
 * DMA or interrupt: a value written to DHR12Rx reaches the output one APB1
 * clock later.
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

#include <nuttx/mutex.h>

#include "arm_internal.h"
#include "chip.h"
#include "stm32_gpio.h"
#include "stm32_rcc.h"

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

/* DAC_CR bits for channel 1. The channel 2 bits are the same, 16 bits up. */

#define MEADOW_DAC_CR_EN          (1 << 0)  /* Channel enable */
#define MEADOW_DAC_CR_BOFF        (1 << 1)  /* Output buffer disable */
#define MEADOW_DAC_CR_TEN         (1 << 2)  /* Trigger enable */
#define MEADOW_DAC_CR_CH_SHIFT    16

#define MEADOW_DAC_DATA_MASK      0x0fff

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
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct meadow_dac_chan_s g_dac_chan[MEADOW_DAC_NCHANNELS] =
{
  {
    MEADOW_DAC_PINID_PA4, MEADOW_DAC_GPIO_PA4,
    MEADOW_DAC_DHR12R1, MEADOW_DAC_DOR1, 0
  },
  {
    MEADOW_DAC_PINID_PA5, MEADOW_DAC_GPIO_PA5,
    MEADOW_DAC_DHR12R2, MEADOW_DAC_DOR2, MEADOW_DAC_CR_CH_SHIFT
  }
};

/* Protects g_dac_open and DAC_CR, which both channels share */

static mutex_t g_dac_lock = NXMUTEX_INITIALIZER;

/* Bit n set = channel n is open */

static uint8_t g_dac_open;

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
      /* Already open: stop the channel before setting it up again. BOFF
       * must only change while the channel is disabled.
       */

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
 *   value above 4095, -EBADF if the channel is not open, or an error
 *   passed back from a lower-level call.
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
 *   Release an open channel. The channel is disabled, which also stops its
 *   current draw, and the pin is left in analog mode with no pull-up or
 *   pull-down, ready for use as an ADC input. The DAC clock is turned off
 *   when neither channel is open.
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

  modifyreg32(MEADOW_DAC_CR, MEADOW_DAC_CR_EN << g_dac_chan[index].shift, 0);

  g_dac_open &= ~(1 << index);
  if (g_dac_open == 0)
    {
      meadow_dac_clock(false);
    }

  nxmutex_unlock(&g_dac_lock);
  return OK;
}
