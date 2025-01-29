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

//==========================================================================
// This function has been removed from Nuttx between 7.9 and 12. It's only
// used in espcp_common.c
int net_lockedwait(sem_t *sem);

//==========================================================================
// This function 
int hcom_nx_setup_mgr(FAR struct mtd_dev_s *mtd);
  
