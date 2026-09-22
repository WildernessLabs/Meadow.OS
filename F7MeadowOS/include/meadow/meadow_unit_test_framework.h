/****************************************************************************
 * nuttx/include/meadow/meadow_unit_test_framework.h
 *
 *   Copyright (C) 2025 Wilderness Labs. All rights reserved.
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
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#ifndef MEADOW_UNIT_TEST_FRAMEWORK_H
#define MEADOW_UNIT_TEST_FRAMEWORK_H

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Public types.
 ****************************************************************************/

 /**
 * @brief Structure for unit tests configuration.
 */
struct unit_tests_configuration_s
{
    /**
     * @brief Test parameter 1.
     */
    char *parameter1;

    /**
     * @brief Test parameter 2.
     */
    char *parameter2;

    /**
     * @brief Test parameter 3.
     */
    char *parameter3;

    /**
     * @brief Test parameter 4.
     */
    char *parameter4;

    /**
     * @brief Test parameter 5.
     */
    char *parameter5;

    /**
     * @brief Test parameter 6.
     */
    char *parameter6;

    /**
     * @brief Test parameter 7.
     */
    char *parameter7;

    /**
     * @brief Test parameter 8.
     */
    char *parameter8;

    /**
     * @brief Test parameter 9.
     */
    char *parameter9;

    /**
     * @brief Test parameter 10.
     */
    char *parameter10;
};
typedef struct unit_tests_configuration_s unit_tests_configuration_t;

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

//---------------------------------------------------------------------------
//      Test IDs.
//---------------------------------------------------------------------------

/**
 * @brief snprintf test ID.
 */
#define MEADOW_TEST_SNPRINTF                        1

/**
 * @brief GPIO test ID.
 */
#define MEADOW_TEST_GPIO                            2

/**
 * @brief SQLite test ID.
 */
#define MEADOW_TEST_SQLITE                          3

/**
 * @brief MCU overload test ID.
 */
#define MEADOW_TEST_MCU_OVERLOAD                    4

/**
 * @brief Chat client test ID.
 */
#define MEADOW_TEST_CHAT_CLIENT                     5

/**
 * @brief Battery backed register test ID.
 */
#define MEADOW_TEST_BBR                             6

/**
 * @brief SD card test ID.
 */
#define MEADOW_TEST_SD_CARD                         7

/**
 * @brief Power management test ID.
 */
#define MEADOW_TEST_POWER_MANAGEMENT                8

/**
 * @brief ISO8601 test ID.
 */
#define MEADOW_TEST_ISO8601                         9

/**
 * @brief Miscellaneous (quick) test ID.
 */
#define MEADOW_TEST_MISC                            10

/**
 * @brief TensorFlow test ID.
 */
#define MEADOW_TEST_TENSORFLOW                      11

/**
 * @brief Directory management test ID.
 */
#define MEADOW_TEST_DIRECTORY_MANAGEMENT            13

/**
 * @brief ADC test ID.
 */
#define MEADOW_TEST_ADC                             14

/**
 * @brief DAC test ID.
 */
#define MEADOW_TEST_DAC                             15

/**
 * @brief Interrupt test ID.
 */
#define MEADOW_TEST_INTERRUPT                       16

/**
 * @brief SPI DMA test ID.
 */
#define MEADOW_TEST_SPI_DMA                         17

/**
 * @brief Rotary encoder test ID.
 */
#define MEADOW_TEST_ROTARY_ENCODER                  18

/**
 * @brief Frequency measurement test ID.
 */
#define MEADOW_TEST_FREQUENCY_MEASUREMENT           19

/**
 * @brief Cell tests ID.
 */
#define MEADOW_TEST_CELL_TESTS                      20

/**
 * @brief USRWORK tests.
 */
#define MEADOW_TEST_USRWORK_TESTS                   21

/**
 * @brief KU traverse channel tests.
 */
#define MEADOW_TEST_TRAVERSE_KU                     22

/**
 * @brief GPIO counter test ID.
 */
#define MEADOW_TEST_GPIO_COUNTER                    23

/**
 * @brief sotest dlopen/dlsym loadable module test ID.
 */
#define MEADOW_TEST_SOTEST                          24

/**
 * @brief I2C1 bus scan test ID.
 */
#define MEADOW_TEST_I2C1_SCAN                       25

/**
 * @brief I2C3 bus scan test ID.
 */
#define MEADOW_TEST_I2C3_SCAN                       26

//---------------------------------------------------------------------------
//      PWM tests 800 - 899. -v is the output frequency in kHz.
//---------------------------------------------------------------------------

/**
 * @brief PWM D02 20% duty test ID.
 */
#define MEADOW_TEST_PWM_D02_DUTY20                  800

/**
 * @brief PWM D02 40% duty test ID.
 */
#define MEADOW_TEST_PWM_D02_DUTY40                  801

/**
 * @brief PWM D02 60% duty test ID.
 */
#define MEADOW_TEST_PWM_D02_DUTY60                  802

/**
 * @brief PWM D02 80% duty test ID.
 */
#define MEADOW_TEST_PWM_D02_DUTY80                  803

/**
 * @brief PWM D03 20% duty test ID.
 */
#define MEADOW_TEST_PWM_D03_DUTY20                  804

/**
 * @brief PWM D03 40% duty test ID.
 */
#define MEADOW_TEST_PWM_D03_DUTY40                  805

/**
 * @brief PWM D03 60% duty test ID.
 */
#define MEADOW_TEST_PWM_D03_DUTY60                  806

/**
 * @brief PWM D03 80% duty test ID.
 */
#define MEADOW_TEST_PWM_D03_DUTY80                  807

/**
 * @brief PWM D04 20% duty test ID.
 */
#define MEADOW_TEST_PWM_D04_DUTY20                  808

/**
 * @brief PWM D04 40% duty test ID.
 */
#define MEADOW_TEST_PWM_D04_DUTY40                  809

/**
 * @brief PWM D04 60% duty test ID.
 */
#define MEADOW_TEST_PWM_D04_DUTY60                  810

/**
 * @brief PWM D04 80% duty test ID.
 */
#define MEADOW_TEST_PWM_D04_DUTY80                  811

/**
 * @brief PWM D05 20% duty test ID.
 */
#define MEADOW_TEST_PWM_D05_DUTY20                  812

/**
 * @brief PWM D05 40% duty test ID.
 */
#define MEADOW_TEST_PWM_D05_DUTY40                  813

/**
 * @brief PWM D05 60% duty test ID.
 */
#define MEADOW_TEST_PWM_D05_DUTY60                  814

/**
 * @brief PWM D05 80% duty test ID.
 */
#define MEADOW_TEST_PWM_D05_DUTY80                  815

/**
 * @brief PWM D06 20% duty test ID (F7v1 only).
 */
#define MEADOW_TEST_PWM_D06_DUTY20                  816

/**
 * @brief PWM D06 40% duty test ID (F7v1 only).
 */
#define MEADOW_TEST_PWM_D06_DUTY40                  817

/**
 * @brief PWM D06 60% duty test ID (F7v1 only).
 */
#define MEADOW_TEST_PWM_D06_DUTY60                  818

/**
 * @brief PWM D06 80% duty test ID (F7v1 only).
 */
#define MEADOW_TEST_PWM_D06_DUTY80                  819

/**
 * @brief PWM D07 20% duty test ID (shares a pin with I2C1_SDA).
 */
#define MEADOW_TEST_PWM_D07_DUTY20                  820

/**
 * @brief PWM D07 40% duty test ID (shares a pin with I2C1_SDA).
 */
#define MEADOW_TEST_PWM_D07_DUTY40                  821

/**
 * @brief PWM D07 60% duty test ID (shares a pin with I2C1_SDA).
 */
#define MEADOW_TEST_PWM_D07_DUTY60                  822

/**
 * @brief PWM D07 80% duty test ID (shares a pin with I2C1_SDA).
 */
#define MEADOW_TEST_PWM_D07_DUTY80                  823

/**
 * @brief PWM D08 20% duty test ID (shares a pin with I2C1_SCL).
 */
#define MEADOW_TEST_PWM_D08_DUTY20                  824

/**
 * @brief PWM D08 40% duty test ID (shares a pin with I2C1_SCL).
 */
#define MEADOW_TEST_PWM_D08_DUTY40                  825

/**
 * @brief PWM D08 60% duty test ID (shares a pin with I2C1_SCL).
 */
#define MEADOW_TEST_PWM_D08_DUTY60                  826

/**
 * @brief PWM D08 80% duty test ID (shares a pin with I2C1_SCL).
 */
#define MEADOW_TEST_PWM_D08_DUTY80                  827

/**
 * @brief PWM D09 20% duty test ID (not supported on CCM).
 */
#define MEADOW_TEST_PWM_D09_DUTY20                  828

/**
 * @brief PWM D09 40% duty test ID (not supported on CCM).
 */
#define MEADOW_TEST_PWM_D09_DUTY40                  829

/**
 * @brief PWM D09 60% duty test ID (not supported on CCM).
 */
#define MEADOW_TEST_PWM_D09_DUTY60                  830

/**
 * @brief PWM D09 80% duty test ID (not supported on CCM).
 */
#define MEADOW_TEST_PWM_D09_DUTY80                  831

/**
 * @brief PWM D10 20% duty test ID (not supported on CCM).
 */
#define MEADOW_TEST_PWM_D10_DUTY20                  832

/**
 * @brief PWM D10 40% duty test ID (not supported on CCM).
 */
#define MEADOW_TEST_PWM_D10_DUTY40                  833

/**
 * @brief PWM D10 60% duty test ID (not supported on CCM).
 */
#define MEADOW_TEST_PWM_D10_DUTY60                  834

/**
 * @brief PWM D10 80% duty test ID (not supported on CCM).
 */
#define MEADOW_TEST_PWM_D10_DUTY80                  835

/**
 * @brief PWM D11 20% duty test ID.
 */
#define MEADOW_TEST_PWM_D11_DUTY20                  836

/**
 * @brief PWM D11 40% duty test ID.
 */
#define MEADOW_TEST_PWM_D11_DUTY40                  837

/**
 * @brief PWM D11 60% duty test ID.
 */
#define MEADOW_TEST_PWM_D11_DUTY60                  838

/**
 * @brief PWM D11 80% duty test ID.
 */
#define MEADOW_TEST_PWM_D11_DUTY80                  839

/**
 * @brief PWM D12 20% duty test ID.
 */
#define MEADOW_TEST_PWM_D12_DUTY20                  840

/**
 * @brief PWM D12 40% duty test ID.
 */
#define MEADOW_TEST_PWM_D12_DUTY40                  841

/**
 * @brief PWM D12 60% duty test ID.
 */
#define MEADOW_TEST_PWM_D12_DUTY60                  842

/**
 * @brief PWM D12 80% duty test ID.
 */
#define MEADOW_TEST_PWM_D12_DUTY80                  843

/**
 * @brief PWM D13 20% duty test ID.
 */
#define MEADOW_TEST_PWM_D13_DUTY20                  844

/**
 * @brief PWM D13 40% duty test ID.
 */
#define MEADOW_TEST_PWM_D13_DUTY40                  845

/**
 * @brief PWM D13 60% duty test ID.
 */
#define MEADOW_TEST_PWM_D13_DUTY60                  846

/**
 * @brief PWM D13 80% duty test ID.
 */
#define MEADOW_TEST_PWM_D13_DUTY80                  847

/**
 * @brief PWM D15 20% duty test ID (CCM only).
 */
#define MEADOW_TEST_PWM_D15_DUTY20                  848

/**
 * @brief PWM D15 40% duty test ID (CCM only).
 */
#define MEADOW_TEST_PWM_D15_DUTY40                  849

/**
 * @brief PWM D15 60% duty test ID (CCM only).
 */
#define MEADOW_TEST_PWM_D15_DUTY60                  850

/**
 * @brief PWM D15 80% duty test ID (CCM only).
 */
#define MEADOW_TEST_PWM_D15_DUTY80                  851

/**
 * @brief PWM A01 20% duty test ID.
 */
#define MEADOW_TEST_PWM_A01_DUTY20                  852

/**
 * @brief PWM A01 40% duty test ID.
 */
#define MEADOW_TEST_PWM_A01_DUTY40                  853

/**
 * @brief PWM A01 60% duty test ID.
 */
#define MEADOW_TEST_PWM_A01_DUTY60                  854

/**
 * @brief PWM A01 80% duty test ID.
 */
#define MEADOW_TEST_PWM_A01_DUTY80                  855

/**
 * @brief PWM A02 20% duty test ID.
 */
#define MEADOW_TEST_PWM_A02_DUTY20                  856

/**
 * @brief PWM A02 40% duty test ID.
 */
#define MEADOW_TEST_PWM_A02_DUTY40                  857

/**
 * @brief PWM A02 60% duty test ID.
 */
#define MEADOW_TEST_PWM_A02_DUTY60                  858

/**
 * @brief PWM A02 80% duty test ID.
 */
#define MEADOW_TEST_PWM_A02_DUTY80                  859

/**
 * @brief PWM A03 20% duty test ID (not supported on F7v1).
 */
#define MEADOW_TEST_PWM_A03_DUTY20                  860

/**
 * @brief PWM A03 40% duty test ID (not supported on F7v1).
 */
#define MEADOW_TEST_PWM_A03_DUTY40                  861

/**
 * @brief PWM A03 60% duty test ID (not supported on F7v1).
 */
#define MEADOW_TEST_PWM_A03_DUTY60                  862

/**
 * @brief PWM A03 80% duty test ID (not supported on F7v1).
 */
#define MEADOW_TEST_PWM_A03_DUTY80                  863

/**
 * @brief PWM A04 20% duty test ID (not supported on F7v1).
 */
#define MEADOW_TEST_PWM_A04_DUTY20                  864

/**
 * @brief PWM A04 40% duty test ID (not supported on F7v1).
 */
#define MEADOW_TEST_PWM_A04_DUTY40                  865

/**
 * @brief PWM A04 60% duty test ID (not supported on F7v1).
 */
#define MEADOW_TEST_PWM_A04_DUTY60                  866

/**
 * @brief PWM A04 80% duty test ID (not supported on F7v1).
 */
#define MEADOW_TEST_PWM_A04_DUTY80                  867

/**
 * @brief PWM all channels on test ID.
 */
#define MEADOW_TEST_PWM_ALL_ON                      868

/**
 * @brief PWM all channels off test ID.
 */
#define MEADOW_TEST_PWM_ALL_OFF                     869

/**
 * @brief PWM cycle through all channels test ID.
 */
#define MEADOW_TEST_PWM_CYCLE_ALL                   870

/**
 * @brief User space assert test ID.
 */
#define MEADOW_TEST_USER_SPACE_ASSERT               900

/**
 * @brief User space reset test ID.
 */
#define MEADOW_TEST_USER_SPACE_RESET                901

/**
 * @brief Kernel space assert test ID.
 */
#define MEADOW_TEST_KERNEL_ASSERT                   902

/**
 * @brief Battery backed domain test ID.
 */
#define MEADOW_TEST_BBD_REGISTER                    903

/**
 * @brief Battery backed domain write after reset test ID.
 */
#define MEADOW_TEST_BBD_WRITE_AFTER_RESET           904

/**
 * @brief Battery backed domain read after reset test ID.
 */
#define MEADOW_TEST_READ_AFTER_RESET                905

/**
 * @brief CLI timeout test ID.
 */
#define MEADOW_TEST_CLI_TIMEOUT                     906

/**
 * @brief Get test configuration test ID.
 */
#define MEADOW_TEST_GET_TEST_CONFIGURATION          907

/**
 * @brief ESP32 (all tests) test ID.
 */
#define MEADOW_TEST_ALL_ESP32                       1000

/**
 * @brief ESP32 start WiFi connection test ID.
 */
#define MEADOW_TEST_ESP_START_WIFI                  1001

/**
 * @brief ESP32 web page load test ID.
 */
#define MEADOW_TEST_ESP_WEB_PAGE_LOAD_TEST          1002

/**
 * @brief Bluetooth start service A test ID.
 */
#define MEADOW_TEST_BLUETOOTH_START_SERVICE_A       1100

/**
 * @brief Bluetooth start service B test ID.
 */
#define MEADOW_TEST_BLUETOOTH_START_SERVICE_B       1101

/**
 * @brief Bluetooth start service C test ID.
 */
#define MEADOW_TEST_BLUETOOTH_START_SERVICE_C       1102

/**
 * @brief Bluetooth start service D test ID.
 */
#define MEADOW_TEST_BLUETOOTH_START_SERVICE_D       1103

/**
 * @brief Bluetooth start service F7 test ID.
 */
#define MEADOW_TEST_BLUETOOTH_START_SERVICE_F7      1104

/**
 * @brief Bluetooth stop service test ID.
 */
#define MEADOW_TEST_BLUETOOTH_STOP                  1105

/**
 * @brief Ethernet (all tests) test ID.
 */
#define MEADOW_TEST_ETHERNET                        1200

/**
 * @brief Ethernet web page load test ID.
 */
#define MEADOW_TEST_ETHERNET_WEB_PAGE_LOAD_TEST     1201

/**
 * @brief BG77 (all tests) test ID.
 */
#define MEADOW_TEST_BG77                            1400

/**
 * @brief QSPI flash init/erase test ID.
 */
#define MEADOW_TEST_QSPI_FLASH_INIT                  1600

/**
 * @brief QSPI flash write test ID.
 */
#define MEADOW_TEST_QSPI_FLASH_WRITE                 1601

/**
 * @brief QSPI flash read/display test ID.
 */
#define MEADOW_TEST_QSPI_FLASH_READ                  1602

//---------------------------------------------------------------------------
//      Test result codes.
//---------------------------------------------------------------------------

/**
 * @brief TEST_ERR_OK
 */
#define TEST_ERR_OK                                 0

/**
 * @brief TEST_ERR_NOT_FOUND
 */
#define TEST_ERR_NOT_FOUND                          1

/**
 * @brief TEST_ERR_INVALID_CONFIG
 */
#define TEST_ERR_INVALID_CONFIG                     2

/**
 * @brief TEST_ERR_FAILED
 */
#define TEST_ERR_FAILED                             3

//---------------------------------------------------------------------------
//      Network test parameter mappings.
//---------------------------------------------------------------------------

/**
 * @brief Unit test parameter holding the SSID of the WiFi network to connect to.
 */
#define UNIT_TESTS_CONFIG_SSID              unit_tests_configuration->parameter1

/**
 * @brief Unit test parameter holding the password of the WiFi network to connect to.
 */
#define UNIT_TESTS_CONFIG_PASSWORD          unit_tests_configuration->parameter2

/**
 * @brief Unit test parameter holding the IP address of the server to connect to.
 */
#define UNIT_TESTS_CONFIG_SERVER_IP         unit_tests_configuration->parameter3

/**
 * @brief Unit test parameter holding the port of the server to connect to.
 */
#define UNIT_TESTS_CONFIG_SERVER_PORT       unit_tests_configuration->parameter4

/**
 * @brief Unit test parameter holding the resource to request from the server.
 */
#define UNIT_TESTS_CONFIG_RESOURCE          unit_tests_configuration->parameter5

/****************************************************************************
 * Private types
 ****************************************************************************/

/*
 * @brief Prototype for the test methods.
 */
typedef void (*test_method_t)(uint32_t);

/*
 * @brief Structure to hold the test names and their IDs.
 */
struct meadow_test_names_s
{
    /*
    *  ID of a test.
    */
    uint16_t testId;

    /*
    *  Description of the test.
    */
    char *description;
};
typedef struct meadow_test_names_s meadow_test_names_t;

/**
 * @brief Structure to hold the methods associated with a particular test ID.
 */
struct meadow_test_methods_s
{
    /*
    *  ID of a test.
    */
    uint16_t testId;

    /*
    *  Method to be executed.
    */
    test_method_t testMethod;
};
typedef struct meadow_test_methods_s meadow_test_methods_t;

/****************************************************************************
 * Public Data
 ****************************************************************************/

 /****************************************************************************
 * Public Function prototypes
 ****************************************************************************/


#endif /* MEADOW_UNIT_TEST_FRAMEWORK_H */