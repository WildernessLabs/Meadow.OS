/****************************************************************************
 * Meadow.OS/Meadow.OS/meadow_temp_rebase.h
 *   Copyright (C) 2024 Wilderness Labs. All rights reserved.
 *   Author:  Wilderness Labs
*/
// This file is temporary while bringing up HCOM on Nuttx v12. It contains
// things required to get HCOM working. Other Meadow features maybe included
// here as stubs, proxies or actual code.
//
// Please add comments to all additions so they can be understood in the future
//

#include <nuttx/semaphore.h>
#include <nuttx/mtd/mtd.h>

// THIS #DEFINE IS ONLY USEFUL WHILE PORTING MEADOW FROM NUTTX 7.X TO 12.X
// it allows using legacy Meadow code or newer Nuttx v12 code where there are
// differences. Such as, function renamed, modified or removed.
#define MEADOW_REBASE_V12_USE_NEWEST_VERSIONS (1)

//==========================================================================
// This function has been removed from Nuttx between 7.9 and 12. It's only
// used in espcp_common.c
int net_lockedwait(sem_t *sem);

//==========================================================================
// Note: the stm32f7_qspi_hw_reinitialize() function was added to Legacy Meadow
// in stm32_qspi.h and stm32_qspi.c files.
// Its purpose is to update the stm32f7's internal register
// values to correct any flash size error originally introduced by the
// stm32f7_qspi_initialize() function previously called.

void stm32f7_qspi_hw_reinitialize(int flashSize);

//==========================================================================
// This function 
int hcom_nx_setup_mgr(FAR struct mtd_dev_s *mtd);
  
