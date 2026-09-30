/****************************************************************************
 * \apps\examples\hcom\tests\developer_tests.c
 *
 *   Copyright (C) 2020 Wilderness Labs. All rights reserved.
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

#include <stdint.h>

#include <meadow/meadow_unit_test_framework.h>
#include "../hcom_common.h"
#include <meadow/hcom_upd_shared.h>
#include <meadow/hcom_shared_common.h>
#include <meadow/meadow_kernel_tests.h>
#include <meadow/meadow_kt_dispatcher.h>

#include "../diag/hcom_diag_gpio.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

//
//  Default logging level for this file.
//
#define LOGGING_LEVEL   1  // 1 = LOG_ALERT

/****************************************************************************
 * Private types
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

static meadow_test_names_t _testNames[] =
{
    //
    //  Miscellaneous tests 1 - 999
    //
#if defined(CONFIG_SNPRINTF_TESTS) //|| defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_SNPRINTF, "sprintf_chk tests" },
#endif

#if defined(CONFIG_GPIO_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_GPIO, "GPIO tests" },
#endif

#if defined(CONFIG_EXAMPLES_SQLITE_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_SQLITE, "NuttX SQLite tests" },
#endif

#if defined(CONFIG_MCU_OVERLOAD_TESTS) //|| defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_MCU_OVERLOAD, "MCU Overload tests" },
#endif

#if defined(CONFIG_ETH_CHAT_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_CHAT_CLIENT, "Ethernet Chat client tests" },
#endif

#if defined(CONFIG_BBR_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_BBR, "Battery backed register tests" },
#endif

#if defined(CONFIG_SD_CARD_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_SD_CARD, "SD card tests" },
#endif

#if defined(CONFIG_POWER_MANAGEMENT_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_POWER_MANAGEMENT, "Power management tests" },
#endif

#if defined(CONFIG_ISO8601_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_ISO8601, "ISO8601 tests" },
#endif

#if defined(CONFIG_QUICK_MISC_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_MISC, "Quick and Misc tests" },
#endif

#if defined(CONFIG_TENSORFLOW_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_TENSORFLOW, "Tensorflow tests" },
#endif

#if defined(CONFIG_DIR_MGMT_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_DIRECTORY_MANAGEMENT, "Directory mgmt tests" },
#endif

#if defined(CONFIG_ADC_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_ADC, "Analog to Digital tests" },
#endif

#if defined(CONFIG_DAC_TESTS) //|| defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_DAC, "Digital to Analog tests (-v 1: API checks, 2-4: level steps, 5: hold A00, 6: close all, 7: waveform API checks, 8-12: waveforms, 13: streaming API checks, 14-18: streaming)" },
#endif

#if defined(CONFIG_MEADOW_INTERRUPT_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_INTERRUPT, "Meadow interrupt tests" },
#endif

#if defined(CONFIG_SPI_DMA_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_SPI_DMA, "SPI DMA tests" },
#endif

#if defined(CONFIG_ROTARY_ENCODER_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_ROTARY_ENCODER, "Rotary Encoder tests" },
#endif

#if defined(CONFIG_GPIO_COUNTER_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_GPIO_COUNTER, "GPIO Counter tests" },
#endif

#if defined(CONFIG_SOTEST_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_SOTEST, "sotest dlopen/dlsym tests" },
#endif

#if defined(CONFIG_MEASURE_FREQUENCY_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_FREQUENCY_MEASUREMENT, "Measure Frequency tests" },
#endif

#if defined(CONFIG_CELL_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_CELL_TESTS, "Cell tests" },
#endif

#if defined(CONFIG_USR_WORK_TESTS) //|| defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_USRWORK_TESTS, "USRWORK tests" },
#endif

#if defined(CONFIG_TRAVERSE_KU_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_TRAVERSE_KU, "KU traverse channel tests" },
#endif

#if defined(CONFIG_I2C_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_I2C1_SCAN, "I2C1 bus scan tests (-v 0: 0x08-0x77 default, -v 1: 0x00-0x7f full range)" },
    { MEADOW_TEST_I2C3_SCAN, "I2C3 bus scan tests (-v 0: 0x08-0x77 default, -v 1: 0x00-0x7f full range)" },
#endif

    //
    //  PWM tests (800-899). -v is the output frequency in kHz.
    //
#if defined(CONFIG_PWM_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_PWM_D02_DUTY20, "PWM D02 20% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D02_DUTY40, "PWM D02 40% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D02_DUTY60, "PWM D02 60% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D02_DUTY80, "PWM D02 80% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D03_DUTY20, "PWM D03 20% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D03_DUTY40, "PWM D03 40% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D03_DUTY60, "PWM D03 60% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D03_DUTY80, "PWM D03 80% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D04_DUTY20, "PWM D04 20% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D04_DUTY40, "PWM D04 40% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D04_DUTY60, "PWM D04 60% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D04_DUTY80, "PWM D04 80% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D05_DUTY20, "PWM D05 20% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D05_DUTY40, "PWM D05 40% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D05_DUTY60, "PWM D05 60% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D05_DUTY80, "PWM D05 80% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D06_DUTY20, "PWM D06 20% duty (-v freq in kHz) (F7v1 only)" },
    { MEADOW_TEST_PWM_D06_DUTY40, "PWM D06 40% duty (-v freq in kHz) (F7v1 only)" },
    { MEADOW_TEST_PWM_D06_DUTY60, "PWM D06 60% duty (-v freq in kHz) (F7v1 only)" },
    { MEADOW_TEST_PWM_D06_DUTY80, "PWM D06 80% duty (-v freq in kHz) (F7v1 only)" },
    { MEADOW_TEST_PWM_D07_DUTY20, "PWM D07 20% duty (-v freq in kHz) (shares pin with I2C1_SDA)" },
    { MEADOW_TEST_PWM_D07_DUTY40, "PWM D07 40% duty (-v freq in kHz) (shares pin with I2C1_SDA)" },
    { MEADOW_TEST_PWM_D07_DUTY60, "PWM D07 60% duty (-v freq in kHz) (shares pin with I2C1_SDA)" },
    { MEADOW_TEST_PWM_D07_DUTY80, "PWM D07 80% duty (-v freq in kHz) (shares pin with I2C1_SDA)" },
    { MEADOW_TEST_PWM_D08_DUTY20, "PWM D08 20% duty (-v freq in kHz) (shares pin with I2C1_SCL)" },
    { MEADOW_TEST_PWM_D08_DUTY40, "PWM D08 40% duty (-v freq in kHz) (shares pin with I2C1_SCL)" },
    { MEADOW_TEST_PWM_D08_DUTY60, "PWM D08 60% duty (-v freq in kHz) (shares pin with I2C1_SCL)" },
    { MEADOW_TEST_PWM_D08_DUTY80, "PWM D08 80% duty (-v freq in kHz) (shares pin with I2C1_SCL)" },
    { MEADOW_TEST_PWM_D09_DUTY20, "PWM D09 20% duty (-v freq in kHz) (not supported on CCM)" },
    { MEADOW_TEST_PWM_D09_DUTY40, "PWM D09 40% duty (-v freq in kHz) (not supported on CCM)" },
    { MEADOW_TEST_PWM_D09_DUTY60, "PWM D09 60% duty (-v freq in kHz) (not supported on CCM)" },
    { MEADOW_TEST_PWM_D09_DUTY80, "PWM D09 80% duty (-v freq in kHz) (not supported on CCM)" },
    { MEADOW_TEST_PWM_D10_DUTY20, "PWM D10 20% duty (-v freq in kHz) (not supported on CCM)" },
    { MEADOW_TEST_PWM_D10_DUTY40, "PWM D10 40% duty (-v freq in kHz) (not supported on CCM)" },
    { MEADOW_TEST_PWM_D10_DUTY60, "PWM D10 60% duty (-v freq in kHz) (not supported on CCM)" },
    { MEADOW_TEST_PWM_D10_DUTY80, "PWM D10 80% duty (-v freq in kHz) (not supported on CCM)" },
    { MEADOW_TEST_PWM_D11_DUTY20, "PWM D11 20% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D11_DUTY40, "PWM D11 40% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D11_DUTY60, "PWM D11 60% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D11_DUTY80, "PWM D11 80% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D12_DUTY20, "PWM D12 20% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D12_DUTY40, "PWM D12 40% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D12_DUTY60, "PWM D12 60% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D12_DUTY80, "PWM D12 80% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D13_DUTY20, "PWM D13 20% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D13_DUTY40, "PWM D13 40% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D13_DUTY60, "PWM D13 60% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D13_DUTY80, "PWM D13 80% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_D15_DUTY20, "PWM D15 20% duty (-v freq in kHz) (CCM only)" },
    { MEADOW_TEST_PWM_D15_DUTY40, "PWM D15 40% duty (-v freq in kHz) (CCM only)" },
    { MEADOW_TEST_PWM_D15_DUTY60, "PWM D15 60% duty (-v freq in kHz) (CCM only)" },
    { MEADOW_TEST_PWM_D15_DUTY80, "PWM D15 80% duty (-v freq in kHz) (CCM only)" },
    { MEADOW_TEST_PWM_A01_DUTY20, "PWM A01 20% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_A01_DUTY40, "PWM A01 40% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_A01_DUTY60, "PWM A01 60% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_A01_DUTY80, "PWM A01 80% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_A02_DUTY20, "PWM A02 20% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_A02_DUTY40, "PWM A02 40% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_A02_DUTY60, "PWM A02 60% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_A02_DUTY80, "PWM A02 80% duty (-v freq in kHz)" },
    { MEADOW_TEST_PWM_A03_DUTY20, "PWM A03 20% duty (-v freq in kHz) (not supported on F7v1)" },
    { MEADOW_TEST_PWM_A03_DUTY40, "PWM A03 40% duty (-v freq in kHz) (not supported on F7v1)" },
    { MEADOW_TEST_PWM_A03_DUTY60, "PWM A03 60% duty (-v freq in kHz) (not supported on F7v1)" },
    { MEADOW_TEST_PWM_A03_DUTY80, "PWM A03 80% duty (-v freq in kHz) (not supported on F7v1)" },
    { MEADOW_TEST_PWM_A04_DUTY20, "PWM A04 20% duty (-v freq in kHz) (not supported on F7v1)" },
    { MEADOW_TEST_PWM_A04_DUTY40, "PWM A04 40% duty (-v freq in kHz) (not supported on F7v1)" },
    { MEADOW_TEST_PWM_A04_DUTY60, "PWM A04 60% duty (-v freq in kHz) (not supported on F7v1)" },
    { MEADOW_TEST_PWM_A04_DUTY80, "PWM A04 80% duty (-v freq in kHz) (not supported on F7v1)" },
    { MEADOW_TEST_PWM_ALL_ON, "PWM all channels on, 10 kHz / 50% duty, stays active (-v ignored)" },
    { MEADOW_TEST_PWM_ALL_OFF, "PWM all channels off (-v ignored)" },
    { MEADOW_TEST_PWM_CYCLE_ALL, "PWM cycle all channels, 10 kHz / 30% duty, -v seconds per channel" },
#endif

    //
    //  Meadow OS tests (900-999)
    //
#if defined(CONFIG_MEADOW_OS_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_USER_SPACE_ASSERT, "User space assert" },
    { MEADOW_TEST_USER_SPACE_RESET, "User space meadow_os_reset_board" },
    { MEADOW_TEST_KERNEL_ASSERT, "Kernel space assert" },
    { MEADOW_TEST_BBD_REGISTER, "Battery Backed Domain register tests" },
    { MEADOW_TEST_BBD_WRITE_AFTER_RESET, "Battery Backed Domain write and reset test" },
    { MEADOW_TEST_READ_AFTER_RESET, "Battery Backed Domain read after reset test" },
    { MEADOW_TEST_GET_TEST_CONFIGURATION, "Get test configuration from the kernel" },
#endif

    //
    //  ESP tests 1000 - 1099
    //
#if defined(CONFIG_ESP_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_ALL_ESP32, "All ESP32 tests" },
    { MEADOW_TEST_ESP_START_WIFI, "ESP32 start WiFi connection" },
    { MEADOW_TEST_ESP_WEB_PAGE_LOAD_TEST, "ESP32 get web resource n times (use -v <count> to specify number of iterations, default = 1)" },
#endif

#if defined(CONFIG_BLUETOOTH_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_BLUETOOTH_START_SERVICE_A, "Bluetooth start service (MeadowA, Espressif)" },
    { MEADOW_TEST_BLUETOOTH_START_SERVICE_B, "Bluetooth start service (MeadowB, Baidu)" },
    { MEADOW_TEST_BLUETOOTH_START_SERVICE_C, "Bluetooth start service (MeadowC, no company identifier)" },
    { MEADOW_TEST_BLUETOOTH_START_SERVICE_D, "Notify characteristic, multiple descriptors (MeadowD, no company identifier)" },
    { MEADOW_TEST_BLUETOOTH_START_SERVICE_F7, "Bluetooth start service (MeadowF7, Espressif)" },
    { MEADOW_TEST_BLUETOOTH_STOP, "Bluetooth stop service" },
    // { MEADOW_TEST_BLUETOOTH_NOTIFY, "Bluetooth notify characteristic (interactive)" },
    // { MEADOW_TEST_BLUETOOTH_SOAK, "Bluetooth soak write characteristic (interactive)" },
#endif

    //
    //  Ethernet tests 1200 - 1399
    //
#if defined(CONFIG_ETHERNET_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_ETHERNET, "All ethernet tests" },
    { MEADOW_TEST_ETHERNET_WEB_PAGE_LOAD_TEST, "Ethernet get web resource n times (use -v <count> to specify number of iterations, default = 1)" },
#endif

    //
    //  BG77 tests 1400 - 1599
    //
#if defined(CONFIG_BG77_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_BG77, "All BG77 tests" },
#endif

    //
    //  QSPI flash tests 1600 - 1799
    //
#if defined(CONFIG_QSPI_FLASH_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_QSPI_FLASH_INIT, "QSPI flash init/erase test (see hcom_nx_qspi_flash_tests.c for -v sub-command values)" },
    { MEADOW_TEST_QSPI_FLASH_WRITE, "QSPI flash write test (see hcom_nx_qspi_flash_tests.c for -v sub-command values)" },
    { MEADOW_TEST_QSPI_FLASH_READ, "QSPI flash read/display test (see hcom_nx_qspi_flash_tests.c for -v sub-command values)" },
#endif
};

static meadow_test_methods_t _userspaceTests[] =
{
    //
    //  Miscellaneous tests 1 - 999
    //
#if defined(CONFIG_SNPRINTF_TESTS) //|| defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_SNPRINTF, diag_misc_tests_snprintf_on_nuttx },
#endif

#if defined(CONFIG_GPIO_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_GPIO, hcom_meadow_diag_gpio_tests },
#endif

#if defined(CONFIG_EXAMPLES_SQLITE_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_SQLITE, hcom_meadow_sqlite_tests },
#endif

#if defined(CONFIG_MCU_OVERLOAD_TESTS) //|| defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_MCU_OVERLOAD, diag_misc_tests_overload_mcu },
#endif

#if defined(CONFIG_ETH_CHAT_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_CHAT_CLIENT, diag_ethernet_chat_server },
#endif

#if defined(CONFIG_BBR_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_BBR, hcom_bbr_tests },
#endif

#if defined(CONFIG_TENSORFLOW_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_TENSORFLOW, tensorflow_tests_hello_world },
#endif

#if defined(CONFIG_SOTEST_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_SOTEST, meadow_sotest_tests },
#endif

#if defined(CONFIG_DIR_MGMT_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_DIRECTORY_MANAGEMENT, meadow_dir_mgmt_tests },
#endif

#if defined(CONFIG_USR_WORK_TESTS) //|| defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_USRWORK_TESTS, diag_misc_tests_verify_usrwork },
#endif

#if defined(CONFIG_DAC_TESTS) //|| defined(CONFIG_ALL_MEADOW_TESTS)
    { MEADOW_TEST_DAC, meadow_dac_tests },
#endif

#if defined(CONFIG_MEADOW_OS_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
    //
    //  Meadow OS tests (900-999)
    //
    { MEADOW_TEST_USER_SPACE_ASSERT, meadow_os_userspace_assert_test },
    { MEADOW_TEST_USER_SPACE_RESET, meadow_os_userspace_board_reset_test },
    { MEADOW_TEST_BBD_REGISTER, meadow_bbd_write_read_test },
    { MEADOW_TEST_BBD_WRITE_AFTER_RESET, meadow_bbd_write_and_reset_test },
    { MEADOW_TEST_READ_AFTER_RESET, meadow_bbd_read_after_reset_test },
    { MEADOW_TEST_CLI_TIMEOUT, meadow_os_cli_timeout_test },
    { MEADOW_TEST_GET_TEST_CONFIGURATION, meadow_os_userspace_get_test_configuration_test },
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: hcom_developer_tests_userspace_dispatcher
 *
 * Description:
 *  Locate the requested userspace test and execute the test passing the user
 *  data to the test method.
 *
 *  If the test is not compiled into the system then then nothing will be
 *  executed.
 *
 * Input Parameters:
 *  param - The param passed using the -p parameter.  This is used to
 *          determine which test should be executed.
 *  value - User data specified using the -v parameter.  This will be
 *          used by the test method.
 *
 * Returned Value:
 *  TEST_ERR_OK: Test found and executed.
 *  TEST_ERR_NOT_FOUND: Test not found.
 *  TEST_ERR_INVALID_CONFIG: Invalid configuration file.
 *
 * Assumptions/Limitations:
 *  None.
 *
 ****************************************************************************/
static int hcom_developer_tests_userspace_dispatcher(uint16_t param, uint32_t value)
{
    int result = TEST_ERR_NOT_FOUND;

    syslog(LOGGING_LEVEL, "Checking for userspace test param: %u - value: %lu\n", param, value);
    if (sizeof(_userspaceTests) > 0)
    {
        for (int index = 0; index < sizeof(_userspaceTests) / sizeof(meadow_test_methods_t); index++)
        {
            if (_userspaceTests[index].testId == param)
            {
                _userspaceTests[index].testMethod(value);
                result = TEST_ERR_OK;
                break;
            }
        }
    }

    return(result);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: hcom_developer_tests_developer
 *
 * Description:
 *  Locate the requested test and execute the test passing the user data to
 *  the test method.
 *
 *  If the test is not compiled into the system then then nothing will be
 *  executed and the user will be informed that the test(s) is/are not
 *  available.
 *
 *  If param is 0 then the test ID and description will be sent to CLI.
 *
 * Input Parameters:
 *  param - The param passed using the -p parameter.  This is used to
 *          determine which test should be executed.
 *  value - User data specified using the -v parameter.  This will be
 *          used by the test method.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *  None.
 *
 ****************************************************************************/
void hcom_developer_tests_developer(uint16_t param, uint32_t value)
{
    char *hostMsg = malloc(HCOM_LARGE_HOST_STRING_BUFF_LENGTH);

    if (hostMsg == NULL)
    {
        syslog(LOG_MTEST, "Developer tests: failed to allocate memory for hostMsg\n");
        return;
    }

    syslog(LOG_MTEST, "Developer test param: %u - value: %lu\n", param, value);

    if (param == 0)
    {
        hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_ERROR, 0, "Available tests:\n", __FILE__, __LINE__);
        if (sizeof(_testNames) > 0)
        {
            for (int index = 0; index < sizeof(_testNames) / sizeof(meadow_test_names_t); index++)
            {
                snprintf_chk(hostMsg, HCOM_LARGE_HOST_STRING_BUFF_LENGTH, "    %d - %s\n", _testNames[index].testId, _testNames[index].description);
                hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_ERROR, 0, hostMsg, __FILE__, __LINE__);
            }
        }
        else
        {
            hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_ERROR, 0, "    No tests built into the system.\n", __FILE__, __LINE__);
        }
    }
    else
    {
        for (int index = 0; index < sizeof(_testNames) / sizeof(meadow_test_names_t); index++)
        {
            if (_testNames[index].testId == param)
            {
                snprintf_chk(hostMsg, HCOM_LARGE_HOST_STRING_BUFF_LENGTH, "Executing %s\n", _testNames[index].description);
                hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_ERROR, 0, hostMsg, __FILE__, __LINE__);
                break;
            }
        }
        int result = hcom_developer_tests_userspace_dispatcher(param, value);

        #if defined(CONFIG_KERNEL_TESTS_SYSCALL)
        if (result == TEST_ERR_NOT_FOUND)
        {
            result = meadow_kt_dispatcher((uint32_t) param, value);
        }
        #endif

        switch (result)
        {
            case TEST_ERR_OK:
                snprintf_chk(hostMsg, HCOM_LARGE_HOST_STRING_BUFF_LENGTH, "Test %u completed successfully.\n", param);
                break;
            case TEST_ERR_NOT_FOUND:
                snprintf_chk(hostMsg, HCOM_LARGE_HOST_STRING_BUFF_LENGTH, "Test %u cannot be found.  Check that the test has been compiled into the system.\n", param);
                break;
            case TEST_ERR_INVALID_CONFIG:
                snprintf_chk(hostMsg, HCOM_LARGE_HOST_STRING_BUFF_LENGTH, "Invalid configuration file.\n");
                break;
            default:
                snprintf_chk(hostMsg, HCOM_LARGE_HOST_STRING_BUFF_LENGTH, "Unknown error.\n");
                break;
        }
        hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_ERROR, 0, hostMsg, __FILE__, __LINE__);
    }

    free(hostMsg);
}
