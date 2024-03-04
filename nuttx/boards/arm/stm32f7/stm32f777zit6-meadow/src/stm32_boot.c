/****************************************************************************
 * boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_boot.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <assert.h>

#include <nuttx/board.h>
#include <arch/board/board.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/spi/qspi.h>

#include "mpu.h"
#include <nuttx/mtd/mtd.h>
#include <nuttx/spi/qspi.h>

#include "meadow_temp_rebase.h"
#include "stm32f777zit6-meadow.h"

#include <meadow/meadow_hw_version.h>

#ifdef CONFIG_STM32F7_QUADSPI
#  include <nuttx/mtd/mtd.h>
#  include "stm32_qspi.h"

#  ifdef CONFIG_FS_FAT
#    include <sys/mount.h>
#    include <nuttx/fs/fat.h>
#  endif
#endif    // #ifdef CONFIG_STM32F7_QUADSPI

/* MEADOW FIXME: header clash? */
// extern FAR struct qspi_dev_s *stm32f7_qspi_initialize(int intf);

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Function declarations
 ****************************************************************************/

// extern FAR struct qspi_dev_s *stm32f7_qspi_initialize(int intf);

/****************************************************************************
 * Global definitions
 ****************************************************************************/

#if defined(CONFIG_STM32F7_QUADSPI)
struct qspi_dev_s *g_qspi;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#if defined(CONFIG_MTD_S25FL)
static struct mtd_dev_s * board_init_mtd_s25fl(FAR struct qspi_dev_s *qspi)
{
  FAR struct mtd_dev_s *mtd;
  mtd = s25fl_initialize(qspi, true);
  if (!mtd)
  {
    syslog(LOG_ERR, "ERROR: S25FL Flash initialization failed\n");
    return 0;
  }

  return mtd;
}
#endif

#if defined(CONFIG_MTD_W25QXXXJV)
static struct mtd_dev_s * board_init_mtd_w25qxxxjv(FAR struct qspi_dev_s *qspi)
{
  FAR struct mtd_dev_s *mtd;
  mtd = w25qxxxjv_initialize(qspi, true);
  if (!mtd)
  {
    syslog(LOG_ERR, "ERROR: W25QxxxJV Flash initialization failed\n");
    return 0;
  }

  return mtd;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: stm32_boardinitialize
 *
 * Description:
 *   All STM32 architectures must provide the following entry point. This
 *   entry point is called early in the initialization -- after all memory
 *   has been configured and mapped but before any devices have been
 *   initialized.
 * 
 * Note: This function is called very early in the Nuttx startup
 *
 ****************************************************************************/

void stm32_boardinitialize(void)
{
#ifdef CONFIG_DEBUG_FEATURES
// If debug features defined Nuttx has output 'A' and after this will output
// 'BCDE'
  syslog(1, "\n%s@%d-Entered stm32_boardinitialize\n", __FILE__, __LINE__);
#else
  syslog(1, "%s@%d-Entered stm32_boardinitialize\n", __FILE__, __LINE__);
#endif

#if defined(CONFIG_STM32F7_SPI1) || defined(CONFIG_STM32F7_SPI2) || \
    defined(CONFIG_STM32F7_SPI3) || defined(CONFIG_STM32F7_SPI4) || \
    defined(CONFIG_STM32F7_SPI5)
  /* Configure SPI chip selects if 1) SPI is not disabled, and 2) the weak
   * function stm32_spidev_initialize() has been brought into the link.
   */

  if (stm32_spidev_initialize)
    {
      stm32_spidev_initialize();
    }
#endif

#ifdef CONFIG_SPORADIC_INSTRUMENTATION
  /* This configuration has been used for evaluating the NuttX sporadic
   * scheduler.
   * The following call initializes the sporadic scheduler monitor.
   */

  arch_sporadic_initialize();
#endif

// (--) - Don't need this!
// #ifdef CONFIG_ARCH_LEDS
//   /* Configure on-board LEDs if LED support has been selected. */

//   board_autoled_initialize();
// #endif

// Rebase Note: Nuttx function name changed from stm32_enablefmc() to
// stm32_sdram_initialize(). The code is also slightly different.
#ifdef CONFIG_STM32F7_FMC
  stm32_sdram_initialize();
#endif

// (--) Expose USB after file system working
// #ifdef CONFIG_STM32F7_OTGFS
//   stm32_usbinitialize();
// #endif
}

/****************************************************************************
 * Name: board_late_initialize
 *
 * Description:
 *   If CONFIG_BOARD_LATE_INITIALIZE is selected, then an additional
 *   initialization call will be performed in the boot-up sequence to a
 *   function called board_late_initialize().
 *   board_late_initialize() will be called immediately after up_initialize()
 *   is called and just before the initial application is started.
 *   This additional initialization phase may be used, for example, to
 *   initialize board-specific device drivers.
 *
 ****************************************************************************/

// Rebase Note: This function is where Legacy Meaodw did a lot of initialization
// It's here too
#ifdef CONFIG_BOARD_LATE_INITIALIZE
void board_late_initialize(void)
{
  int ret;
#ifdef CONFIG_STM32F7_QUADSPI
  FAR struct qspi_dev_s *qspi;
  FAR struct mtd_dev_s *mtd;
  struct qspi_meminfo_s meminfo;

syslog(1, "%s@%d-Entered board_late_initialize()\n", __FILE__, __LINE__);

/* COPIED FROM LEGACY MEADOW. BUT, NOT READY FOR IT'S USE YET
#if defined (CONFIG_RAMLOG_SYSLOG)
  // Initialize the syslog message pump
  ret = hcom_nx_trace_msg_proc_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup trace msg pump %d\n", __FILE__, __LINE__, ret);
  }
#endif

#if defined(CONFIG_STM32F7_PWR)
  // Initialize the backup SRAM and the 32 registers
  stm32_pwr_initbkp(true);    // initialize as writable
#endif

#ifdef CONFIG_PWM
  // Initialize PWM and register the PWM device.
  ret = stm32_pwm_setup();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: stm32_pwm_setup() failed: %d\n", ret);
    }
#endif

#if defined(CONFIG_CDCACM)
  board_init_usbdev();
#endif

#ifdef CONFIG_BUILD_PROTECTED
 #if defined(CONFIG_ARM_MPU)
  // Map in the entire GPIO register range.
  // Due to MPU alignment requirements, size needs to be slightly larger
  // than the GPIO memory region, leaving the CRC, RCC and Flash interface
  // registers open to user code as well.
  size_t size = 1 << mpu_log2regionceil(STM32_GPIOK_BASE - STM32_GPIOA_BASE);
  mpu_user_peripheral(STM32_GPIOA_BASE, size);
 #endif
#endif

#ifdef CONFIG_EXAMPLES_MONO
  ret = meadow_upd_initialize();
  if (ret < 0)
  {
    syslog(LOG_ERR, "ERROR: meadow_upd_initialize() failed: %d\n", ret);
  }
#endif
*/

  // Part of every QSPI flash chip is an API with at a standard set of
  // functions. This means we can read the flash chip's vendor information
  // without knowing much about the chip. Thus using the Nuttx function
  // stm32f7_qspi_initialize(), which assumes the defconfig configuration
  // CONFIG_STM32F7_QSPI_FLASH_SIZE size, we can initialize any QSPI
  // flash chip used by Meadow. And we can find the vendor information to
  // determine the exact chip used. If the chip size is wrong we will fix
  // it later.
  qspi = stm32f7_qspi_initialize(0);
  if (!qspi)
    {
      syslog(LOG_ERR, "ERROR: sam_qspi_initialize muiled\n");
      return;
    }

  g_qspi = qspi;

  // Get the version of this board.
  uint32_t meadowHwVer = meadow_hw_version_find_device_ver(qspi);
  syslog(1, "%s@%d-meadowHwVer:%lu\n", __FILE__, __LINE__, meadowHwVer);

  if (meadowHwVer == MEADOW_F7_HW_VERSION_NUMB_UNKNOWN ||
      meadowHwVer == MEADOW_F7_HW_VERSION_NUMB_ERROR)
  {
    ferr("ERROR: Meadow version could not be determined\n");
    syslog(1, "%s@%d-ERROR: Meadow version could not be determined\n", __FILE__, __LINE__);
    return;
  }

  // Get the correct flash chip size based on the hardware version
  size_t flashSize = meadow_hw_version_flash_size();
  syslog(1, "%s@%d-flashSize:%u\n", __FILE__, __LINE__, flashSize);

  // Test against the defconfig value of QSPI flash size. If we initially used
  // the defconfig value that is not the correct flash size we must fix it.
  if(flashSize != CONFIG_STM32F7_QSPI_FLASH_SIZE)
  {
    // Note: the stm32f7_qspi_hw_reinitialize() function was added to an existing
    // Nuttx module for Meadow. It updates the stm32f7's internal register value
    // to correct any flash size error orginally introduced by the
    // stm32f7_qspi_initialize() function previously called.
    syslog(1, "%s@%d-flashSize:%u-Must reinitialize flash\n", __FILE__, __LINE__, flashSize);

    stm32f7_qspi_hw_reinitialize(flashSize);
  }

/* NOT READY FOR THE FOLLOWING YET
 #if defined(CONFIG_ARM_MPU)
  // Allow user-space access to the QSPI flash memory region.

  mpu_configure_region(STM32_FMC_BANK4, flashSize,
                           MPU_RASR_TEX_SO   | // Ordered            
                           MPU_RASR_C        | // Cacheable          
                                               // Not Bufferable     
                           MPU_RASR_S        | // Shareable          
                           MPU_RASR_AP_RWRW    // P:RW   U:RW        
                                               // Instruction access );
 #endif

  // Initialize the correct flash driver. Only one can be initialized even
  // if multiple built.
  switch(meadow_hw_version_get())
  {
 #if defined(CONFIG_MTD_S25FL)
    case MEADOW_F7_HW_VERSION_NUMB_F7V1:
    mtd = board_init_mtd_s25fl(qspi);
    break;
 #endif

 #if defined(CONFIG_MTD_W25QXXXJV)
    case MEADOW_F7_HW_VERSION_NUMB_F7V2:
    case MEADOW_F7_HW_VERSION_NUMB_CCMV2:
    mtd = board_init_mtd_w25qxxxjv(qspi);
    break;
 #endif

    default:

// PROBABLY NEVER NEED RAMMTD    
//  #if defined(CONFIG_RAMMTD)
//     mtd = board_init_mtd_ram(MEADOW_RAM_MTD_SIZE);
//     break;
//  #else
    ferr("ERROR: Unknown MTD:%d\n", meadowHwVer);
    return;
//  #endif
  }

 #if defined(CONFIG_MTD)
  if (mtd != NULL)
  {
    // Provides a Nuttx block driver wrapper around an MTD interface
    ret = ftl_initialize(0, mtd);
    if (ret < 0)
    {
      ferr("ERROR: Initialize the FTL layer. returned %d\n", ret);
      return;
    }
 #endif // #if defined(CONFIG_MTD)

 #if defined(CONFIG_MEADOW_HCOM)
    // Initialize Meadow HCOM nuttx
    ret = hcom_nx_setup_mgr(mtd);
    if(ret < 0)
    {
      syslog(LOG_EMERG, "ERROR: HCOM proxy initialization failed!\n");
      PANIC();
    }
  }
 #endif

#else // #if defined(CONFIG_STM32F7_QUADSPI)

  uint32_t meadowHwVer = meadow_hw_version_determine_ver(NULL);
  if (meadowHwVer == MEADOW_F7_HW_VERSION_NUMB_UNKNOWN)
  {
    ferr("ERROR: Meadow version could not be determined with QSPI configured\n");
    return;
  }

#endif // #if defined(CONFIG_STM32F7_QUADSPI)
*/

// EARLY REBASE CODE BELOW
//   mtd = w25qxxxjv_initialize(qspi, true);
//   if (!mtd)
//     {
//       syslog(LOG_ERR, "ERROR: w25qxxxjv_initialize failed\n");
//     }

//   ret = ftl_initialize(0, mtd);
//   if (ret < 0)
//     {
//       ferr("ERROR: Initialize the FTL layer\n");
//     }

//   meminfo.flags = QSPIMEM_READ | QSPIMEM_QUADIO;
//   meminfo.addrlen = 3;
//   meminfo.dummies = 6;
//   meminfo.cmd = 0xeb; /* S25FL1_FAST_READ_QUADIO; */
//   meminfo.addr = 0;
//   meminfo.buflen = 0;
//   meminfo.buffer = NULL;

//   stm32f7_qspi_enter_memorymapped(qspi, &meminfo, 80000000);

//   /* FIXME: stm32_mpu_uheap depends on PROTECTED && MPU
//    *
//    * stm32_mpu_uheap((uintptr_t)0x90000000, 0x4000000);
//    */

// #endif
// #if defined(MEADOW_OS)
//   /* Initialize Meadow HCOM nuttx */

//   ret = hcom_nx_setup_mgr(mtd);
//   if (ret < 0)
//     {
//       syslog(LOG_EMERG, "ERROR: HCOM proxy initialization failed!\n");
//       PANIC();
//     }

// #endif
// #if defined(CONFIG_NSH_LIBRARY) && !defined(CONFIG_BOARDCTL)
//   /* Perform NSH initialization here instead of from the NSH.  This
//    * alternative NSH initialization is necessary when NSH is ran in
//    * user-space but the initialization function must run in kernel space.
//    */

//   board_app_initialize();
#endif    // #ifdef CONFIG_STM32F7_QUADSPI
}
#endif    // #ifdef CONFIG_BOARD_LATE_INITIALIZE
