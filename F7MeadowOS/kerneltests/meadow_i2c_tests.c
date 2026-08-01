/****************************************************************************
 * F7MeadowOS\kerneltests\meadow_i2c_tests.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <inttypes.h>
#include <syslog.h>
#include <meadow/hcom_shared_common.h>
#include <meadow/meadow_kernel_tests.h>
#include <nuttx/i2c/i2c_master.h>
#include "stm32_i2c.h"

// Only build if configured
#if defined(CONFIG_I2C_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
#pragma message "(--) meadow_i2c_tests.c"

/************************************************************************************
 * Pre-processor Definitions
 ************************************************************************************/
// Fixed at I2C standard mode. Every device expected on either bus supports this
// speed, so there is no need to select it via the test's userData value.
#define I2C_SCAN_FREQUENCY (100000)

// I2C addresses 0x00-0x07 and 0x78-0x7f are reserved by the I2C spec.
#define I2C_SCAN_STANDARD_FIRST (0x08)
#define I2C_SCAN_STANDARD_LAST  (0x77)
#define I2C_SCAN_FULL_FIRST     (0x00)
#define I2C_SCAN_FULL_LAST      (0x7f)

/************************************************************************************
 * Private Function Prototypes
 ************************************************************************************/

/************************************************************************************
 * Private Functions
 ************************************************************************************/

/****************************************************************************
 * Name: i2c_scan_bus
 *
 * Description:
 *  Scan the given I2C port for connected devices and log the results to
 *  syslog. Each candidate address is probed with a single-byte I2C_M_READ
 *  transfer; a successful transfer means a device ACK'd and is present.
 *
 * Input Parameters:
 *  port - I2C port number to scan (1 for I2C1, 3 for I2C3).
 *  userData - Selects the address range to scan: 0 for the standard 7-bit
 *   range (0x08-0x77), 1 for the full range (0x00-0x7f), including reserved
 *   addresses.
 *
 * Returned Value:
 *   None
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
static void i2c_scan_bus(uint32_t port, uint32_t userData)
{
    FAR struct i2c_master_s *dev;
    struct i2c_msg_s msg;
    uint8_t scratch;
    uint8_t found[I2C_SCAN_FULL_LAST - I2C_SCAN_FULL_FIRST + 1];
    uint32_t foundCount = 0;
    uint32_t first = I2C_SCAN_STANDARD_FIRST;
    uint32_t last = I2C_SCAN_STANDARD_LAST;
    uint32_t addr;
    int ret;
    uint32_t i;

    if (userData == 1)
    {
        first = I2C_SCAN_FULL_FIRST;
        last = I2C_SCAN_FULL_LAST;
    }

    dev = stm32_i2cbus_initialize(port);
    if (dev == NULL)
    {
        syslog(LOG_MTEST, "%s@%d-I2C%" PRIu32 " failed to initialize\n", __FILE__, __LINE__, port);
        return;
    }

    syslog(LOG_MTEST, "I2C%" PRIu32 " - Scanning addresses 0x%02" PRIx32 "-0x%02" PRIx32 "\n", port, first, last);

    for (addr = first; addr <= last; addr++)
    {
        msg.addr = addr;
        msg.frequency = I2C_SCAN_FREQUENCY;
        msg.flags = I2C_M_READ;
        msg.buffer = &scratch;
        msg.length = 1;

        // A single NACK is the expected, normal result for an empty address, not
        // a fault - so there is no retry here, unlike a data-integrity test.
        ret = I2C_TRANSFER(dev, &msg, 1);
        if (ret == OK)
        {
            found[foundCount++] = addr;
        }
    }

    syslog(LOG_MTEST, "I2C%" PRIu32 " - Scan complete, %" PRIu32 " device(s) found\n", port, foundCount);
    if (foundCount != 0)
    {
        for (i = 0; i < foundCount; i++)
        {
            syslog(LOG_MTEST, "I2C%" PRIu32 " - Found device at 0x%02x (%d)\n", port, found[i], found[i]);
        }
    }
}

/************************************************************************************
 * Public Functions
 ************************************************************************************/

/****************************************************************************
 * Name: meadow_kt_i2c1_scan_tests
 *
 * Description:
 *  Scan the I2C1 bus (the Feather bus, present on both the Feather form
 *  factor and the CCM) for connected devices. developer -p 25 comes here.
 *
 * Input Parameters:
 *  userData - Selects the address range to scan: 0 for the standard 7-bit
 *   range (0x08-0x77), 1 for the full range (0x00-0x7f), including reserved
 *   addresses.
 *
 * Returned Value:
 *   None
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
void meadow_kt_i2c1_scan_tests(uint32_t userData)
{
    i2c_scan_bus(1, userData);
}

/****************************************************************************
 * Name: meadow_kt_i2c3_scan_tests
 *
 * Description:
 *  Scan the I2C3 bus (the additional bus present only on the CCM) for
 *  connected devices. developer -p 26 comes here.
 *
 * Input Parameters:
 *  userData - Selects the address range to scan: 0 for the standard 7-bit
 *   range (0x08-0x77), 1 for the full range (0x00-0x7f), including reserved
 *   addresses.
 *
 * Returned Value:
 *   None
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
void meadow_kt_i2c3_scan_tests(uint32_t userData)
{
    i2c_scan_bus(3, userData);
}

#endif  // #if defined(CONFIG_I2C_TESTS)
