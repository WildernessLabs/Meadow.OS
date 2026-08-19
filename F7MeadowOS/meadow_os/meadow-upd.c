/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <string.h>

#include <nuttx/config.h>

#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>
#include <arch/board/board.h>
#include <nuttx/mqueue.h>
#include <nuttx/signal.h>
#include <nuttx/timers/pwm.h>
#include <nuttx/spi/spi.h>

#include <stdbool.h>
#include <assert.h>
#include <debug.h>
#include <errno.h>

#include "chip.h"
#include "fcntl.h"
#include "stm32_pwm.h"
#include <nuttx/i2c/i2c_master.h>
#include "stm32_spi.h"
#include <meadow/meadow_interrupt.h>
#include <dirent.h>
#include "arm_internal.h"

#include <nuttx/timers/timer.h>
#include <nuttx/timers/watchdog.h>
#include <sys/ioctl.h>
#include "stm32_tim.h"
#include "stm32_i2c.h"
#include <meadow/meadow-upd.h>

#include <meadow/hcom_shared_common.h>
#include "stm32_uid.h" // stm32_get_uniqueid()
#include "hcom_nx/hcom_nx_common.h"

#include "espcp/espcp_common.h"
#include "espcp/espcp_encoders.h"
#include "hcom_nx/hcom_nx_config_manager.h"

#if defined (CONFIG_MEADOW_HCOM)
// Needed by the HCOM_NX_UPD_* commands merged in from hcom_nx_upd.c
#include <inttypes.h>
#include "stm32_gpio.h"
#include <meadow/hcom_upd_shared.h>
#include <meadow/hcom_bbreg_defn.h>
#include <meadow/meadow_hw_version.h>
#include <meadow/meadow_apps_core_share.h>
#include <meadow/hcom_dfu_mode_entry.h>
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct upd_register_value
{
  uint32_t address;
  uint32_t value;
};

struct upd_register_update
{
  uint32_t address;
  uint32_t clearBits;
  uint32_t setBits;
};

struct upd_i2c_cmd
{
  uint32_t address;
  uint32_t frequency;
  uint8_t* txBuffer; // in to driver (so tx)
  uint32_t txLength;
  uint8_t* rxBuffer; // back out to app, so rx
  uint32_t rxLength;
  uint32_t busNumber; // bus number is at the end to enable backward-compat
};

struct upd_spi_data_cmd
{
  uint8_t* txBuffer; // in to driver (so tx)
  uint8_t* rxBuffer; // back out to app, so rx
  uint32_t length;
  uint32_t busNumber;
};

struct upd_spi_speed_cmd
{
  uint32_t busNumber;
  uint64_t frequency;
};

struct upd_spi_mode_cmd
{
  uint32_t busNumber;
  uint32_t mode;
};

struct upd_spi_bits_cmd
{
  uint32_t busNumber;
  uint32_t bits;
};

struct upd_sleep_cmd
{
  uint32_t secondsToSleep;
};

struct upd_dir_enum_cmd
{
  char* root; // folder to enumerate
  char* result; // data back to app
  uint32_t resultLength; // length of data buffer
};


struct upd_device_info
{
  char *infoBuf;
  int infoBufLen;
  int infoRetLen;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/
static int upd_ioctl(FAR struct file *filep, int cmd, unsigned long arg);
static int upd_open(struct file *filep);
static int upd_close(struct file *filep);
static int upd_read(FAR struct file *filep, FAR char *buffer, size_t buflen);

#if defined (CONFIG_MEADOW_HCOM)
static int upd_hcom_get_mcu_ser_numb(unsigned long arg);
static int upd_hcom_restore_uart_reconfig(unsigned long arg);
#endif

// static int upd_gpio_interrupt(int irq, void *context, void *arg);

static int upd_handle_i2c(int cmd, struct upd_i2c_cmd*);
static struct spi_dev_s * get_spi_bus(int busNumber);
static int upd_handle_spi_data(int cmd, struct upd_spi_data_cmd*);
static int upd_handle_spi_speed(int cmd, struct upd_spi_speed_cmd*);
static int upd_handle_spi_mode(int cmd, struct upd_spi_mode_cmd*);
static int upd_handle_spi_bits(int cmd, struct upd_spi_bits_cmd* data);
static int upd_handle_dir_enum(struct upd_dir_enum_cmd*);
static int upd_handle_sleep_command(struct upd_sleep_cmd* cmd);

// static int upd_handle_watchdog_set(unsigned long cmd);
// static int upd_handle_watchdog_pet(void);

static int upd_get_set_configuration_value(upd_get_set_configuration_value_t *);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations g_driver_operations =
{
  .open  = upd_open,
  .close = upd_close,
  .read  = upd_read,
  .ioctl = upd_ioctl
};

#define MEADOW_I2C_PORT1    1
#define MEADOW_I2C_PORT3    3
#define MEADOW_SPI_PORT5    5  // external on CCM
#define MEADOW_SPI_PORT3    3  // external
#define MEADOW_SPI_PORT2    2  // EXP32

static struct i2c_master_s *g_i2c1 = NULL;
static struct i2c_master_s *g_i2c3 = NULL;

static struct i2c_config_s g_i2c1_cfg;
static struct i2c_config_s g_i2c3_cfg;

static struct spi_dev_s *g_spi5 = NULL; // external
static struct spi_dev_s *g_spi3 = NULL; // external
static struct spi_dev_s *g_spi2 = NULL; // to ESP32

// static int s_wd_fd = -1;

// Note ioctl calls put any returned value into errno and the returned int
// is set to -1
static int upd_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  struct upd_register_value *register_val;
  struct upd_register_update *register_update;
  struct mint_gpio_int_config *interrupt_cfg;

#if defined (CONFIG_MEADOW_HCOM)
  int ret = -1;
  struct hcom_nx_upd_bbr_value *bbr_val;
  struct hcom_nx_upd_bbr_update *bbr_update;
  struct hcom_nx_upd_syslog_mask *syslog_mask;
  struct hcom_nx_cmd_data *cmdData;
  struct hcom_nx_upd_is_part_mounted *is_mounted;

  hcom_nx_upd_get_hw_ver_t *hardwareVer;

#if defined (CONFIG_MEADOW_PWR_MGMT_SUPPORT)
  hcom_nx_upd_rtc_set_time_t *rtcSetTime;
#endif
#if HCOM_INCLUDE_ISO8601_SUPPORT > 0
  hcom_nx_upd_rtc_wakeup_time_t *rtcWakeupTime;
#endif
#endif  // defined (CONFIG_MEADOW_HCOM)

  switch(cmd)
  {
    case MUPD_SET_REGISTER:
        register_val = (struct upd_register_value *)arg;
        putreg32(register_val->value, register_val->address);
        return OK;
    case MUPD_GET_REGISTER:
        register_val = (struct upd_register_value *)arg;
        register_val->value = getreg32(register_val->address);
        return OK;
    case MUPD_UPDATE_REGISTER:
        // this does an atomic read/set/write of a register
        register_update = (struct upd_register_update *)arg;
        modifyreg32(register_update->address, register_update->clearBits, register_update->setBits);
        return OK;
    case MUPD_REGISTER_GPIO_IRQ:
        interrupt_cfg = (struct mint_gpio_int_config *)arg;
        return mint_config_interrupt(interrupt_cfg);

    case MUPD_PWM_SETUP:
    case MUPD_PWM_SHUTDOWN:
    case MUPD_PWM_START:
    case MUPD_PWM_STOP:
        return upd_handle_pwm(cmd, arg);

    case MUPD_I2C_SHUTDOWN:
    case MUPD_I2C_DATA:
        return upd_handle_i2c(cmd, (struct upd_i2c_cmd*)arg);
      
    case MUPD_SPI_DATA:
        return upd_handle_spi_data(cmd, (struct upd_spi_data_cmd*)arg);
    case MUPD_SPI_SPEED:
        return upd_handle_spi_speed(cmd, (struct upd_spi_speed_cmd*)arg);
    case MUPD_SPI_MODE:
        return upd_handle_spi_mode(cmd, (struct upd_spi_mode_cmd*)arg);
    case MUPD_SPI_BITS:
        return upd_handle_spi_bits(cmd, (struct upd_spi_bits_cmd*)arg);

    case MUPD_DIR_ENUM:
      return upd_handle_dir_enum((struct upd_dir_enum_cmd*)arg);
      break;
    case MUPD_GET_LAST_ERROR:
      *((int*)arg) = errno;
      return OK;

    case MUPD_ESP32_COMMAND:
      return upd_handle_esp32_command((upd_esp32_command_t *) arg);
    case MUPD_ESP32_GET_EVENT_RESULT:
      return upd_handle_esp32_get_event_result((espcp_event_data_payload_t *) arg);

    case MUPD_PWR_RESET:
      up_systemreset();
      break;

    case MUPD_GET_SET_CONFIGURATION_VALUE:
      return(upd_get_set_configuration_value((upd_get_set_configuration_value_t *) arg));
      break;

    case MUPD_PWR_SLEEP1:
    case MUPD_PWR_SLEEP2:
      return upd_handle_sleep_command((struct upd_sleep_cmd *)arg);
      return EINVAL;

#if defined (CONFIG_MEADOW_HCOM)
  //=========================================================================
  // HCOM command set. These were the ioctl commands of the separate HCOM UPD
  // driver (/dev/nxupd, hcom_nx/hcom_nx_upd.c) before the two drivers were
  // consolidated onto this one. They are numbered from HCOM_NX_UPD_BASE
  // (0x7000) so they cannot collide with the MUPD_* commands above.
  //=========================================================================

    // This only works with Battery Backed Registers via 0-31 id value
    case HCOM_NX_UPD_SET_BBR_VALUE:
      bbr_val = (struct hcom_nx_upd_bbr_value *)arg;
      putreg32(bbr_val->value, HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER);
      return OK;
    case HCOM_NX_UPD_GET_BBR_VALUE:
      bbr_val = (struct hcom_nx_upd_bbr_value *)arg;
      bbr_val->value = getreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER);
      return OK;
    case HCOM_NX_UPD_UPDATE_BBR_VALUE:
      bbr_update = (struct hcom_nx_upd_bbr_update *)arg;
      modifyreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER,
                bbr_update->clearBits, bbr_update->setBits);
      return OK;

    case HCOM_NX_UPD_CLI_COMMAND:
      cmdData = (struct hcom_nx_cmd_data *)arg;
      ret = hcom_nx_route_in_bound_cli_command(cmdData);
      return ret;

    case HCOM_NX_UPD_GET_MCU_ID:
      stm32_get_uniqueid((uint8_t *)arg);
      return OK;

    case HCOM_NX_UPD_GET_MCU_SER_NUMB:
      return upd_hcom_get_mcu_ser_numb(arg);

    case HCOM_NX_UPD_IS_PART_MOUNTED:
      is_mounted = (struct hcom_nx_upd_is_part_mounted *)arg;
      is_mounted->isMounted = hcom_nx_fs_is_mounted(is_mounted->partitionId);
      return OK;

    case HCOM_NX_UPD_MONO_HAS_STARTED:
#if defined (CONFIG_RAMLOG_SYSLOG)
      return hcom_nx_trace_msg_mono_started();
#else
      return OK;
#endif

    case HCOM_NX_UPD_RESTORE_UART_CONFIG:
      return upd_hcom_restore_uart_reconfig(arg);

    case HCOM_NX_UPD_HOST_RESTART_MEADOW_MCU:
      hcom_nx_common_utils_host_restart_meadow();
      return OK;

    case HCOM_NX_UPD_ONLY_RESTART_MEADOW_MCU:
      hcom_nx_common_utils_only_restart_meadow();
      return OK;

    case HCOM_NX_UPD_GET_HW_VERSION:
      hardwareVer = (hcom_nx_upd_get_hw_ver_t*)arg;
      hardwareVer->hwVer = meadow_hw_version_get();
      return OK;

    case HCOM_NX_UPD_ENTER_INTO_DFU_MODE:
      *((unsigned long *)MEADOW_ENTER_DFU_MODE_MEMORY_ADDR) =
                MEADOW_ENTER_DFU_MODE_MAGIC_NUMB;
      return OK;

#if defined (CONFIG_MEADOW_PWR_MGMT_SUPPORT)
    case HCOM_NX_UPD_RTC_SET_TIME:
      // Set the time in the RTC hardware from mono
      rtcSetTime = (hcom_nx_upd_rtc_set_time_t*)arg;
      ret = pwrmgmt_mono_cmd_time_set_clock(rtcSetTime->hdrMsg,
                rtcSetTime->msgLen);
      return ret;
#endif  // defined (CONFIG_MEADOW_PWR_MGMT_SUPPORT)

#if HCOM_INCLUDE_ISO8601_SUPPORT > 0
    case HCOM_NX_UPD_RTC_WAKEUP_TIME:
      // Set the wakeup time in the RTC hardware
      rtcWakeupTime = (hcom_nx_upd_rtc_wakeup_time_t*)arg;
      ret = pwrmgmt_mono_cmd_time_wakeup_period(rtcWakeupTime->hdrMsg,
                rtcWakeupTime->msgLen);
      return ret;
#endif  // HCOM_INCLUDE_ISO8601_SUPPORT > 0

    case HCOM_NX_UPD_UPDATE_OS1:
      // Stage a updated OS bin
#if (MEADOW_INCLUDE_CODE_FOR_TESTING_5MB_OF_FLASH == 0)
      // Don't do this if testing
      ret = hcom_nx_exec_ex_flash_OS_update_flash1();
#endif
      return ret;

    case HCOM_NX_UPD_UPDATE_OS2:
      // Stage a updated external flash OS bin
      ret = hcom_nx_exec_ex_flash_OS_update_flash2();
      return ret;

    case HCOM_NX_UPD_COPY_RUNTIME_TO_RAM:
      {
        uint32_t block_size = hcom_nx_exec_ex_flash_get_block_size();
        uint32_t number_of_blocks =
                  HCOM_NX_FS_MONO_RAW_PARTITION_SIZE / block_size;
        return hcom_nx_exec_ex_flash_copy_blocks_to_memory(0,
                  (void *) CONFIG_HEAP2_BASE, number_of_blocks);
      }

#if defined (CONFIG_MEADOW_PWR_MGMT_SUPPORT)
    case HCOM_NX_UPD_REG_PWR_MGMT_CB:
      {
        pwr_mgmt_notify_callback *callback = (pwr_mgmt_notify_callback*) arg;
        return pwrmgmt_subscribe_for_low_pwr_notifications(*callback);
      }
#endif

    // Immediately applies a fully-formed syslog mask to kernel-space, so a
    // live CLI trace-level change (user-space's
    // hcom_diag_trace_priority_change_trace_level()) takes effect on
    // kernel-space syslog() calls right away instead of only on next reboot.
    case HCOM_NX_UPD_SET_SYSLOG_MASK:
      syslog_mask = (struct hcom_nx_upd_syslog_mask *)arg;
      hcom_nx_utils_set_syslog_mask_now(syslog_mask->value);
      return OK;

#endif  // defined (CONFIG_MEADOW_HCOM)

    default:
      syslog(LOG_ERR, "%s@%d-unknown upd command:%d\n",
                __FILE__, __LINE__, cmd);
      return ERROR;
  }
  return ERROR;
}

// Allow the CLI to initiate Meadow entering the stop mode for a time period.
static int upd_handle_sleep_command(struct upd_sleep_cmd* cmd)
{
  return pwrmgmt_enter_stm32f7_stop_mode(cmd->secondsToSleep);
}

static int upd_handle_dir_enum(struct upd_dir_enum_cmd* cmd)
{
  DIR *d;
  struct dirent *dir;
  int len = 0;

  d = opendir(cmd->root);
  if(!d) return ENOTDIR;
  while((dir = readdir(d)) != NULL)
  {
    if(len + strlen(dir->d_name) + 1 > cmd->resultLength)
    {
      // this isn't really safe, as the user could always send in fake length data, but for now we assume they are nice users
      break;
    }
    strcat(cmd->result, dir->d_name);
    strcat(cmd->result, "\n");
  }
  closedir(d);
  return OK;
}

static struct spi_dev_s * get_spi_bus(int busNumber)
{
  switch (busNumber)
  {
    case 2:
      if(g_spi2 == NULL)
      {
        g_spi2 = stm32_spibus_initialize(MEADOW_SPI_PORT2);
      }
      return g_spi2;
    case 3:
      if(g_spi3 == NULL)
      {
        g_spi3 = stm32_spibus_initialize(MEADOW_SPI_PORT3);
      }
      return g_spi3;
    case 5:
      if(g_spi5 == NULL)
      {
        g_spi5 = stm32_spibus_initialize(MEADOW_SPI_PORT5);
      }
      return g_spi5;
  }

  return NULL;
}

static int upd_handle_spi_bits(int cmd, struct upd_spi_bits_cmd* data)
{
  struct spi_dev_s *target = get_spi_bus(data->busNumber);

  if(target == NULL)
  {
    return ENODEV;
  }

  SPI_SETBITS(target, data->bits);

  return OK;
}

static int upd_handle_spi_mode(int cmd, struct upd_spi_mode_cmd* data)
{
  struct spi_dev_s *target = get_spi_bus(data->busNumber);

  if(target == NULL)
  {
    return ENODEV;
  }

  SPI_SETMODE(target, data->mode);

  return OK;
}

static int upd_handle_spi_speed(int cmd, struct upd_spi_speed_cmd* data)
{
  struct spi_dev_s *target = get_spi_bus(data->busNumber);

  if(target == NULL)
  {
    return ENODEV;
  }

  SPI_SETFREQUENCY(target, data->frequency);

  return OK;
}

static int upd_handle_spi_data(int cmd, struct upd_spi_data_cmd* data)
{
  struct spi_dev_s *target = get_spi_bus(data->busNumber);

  if(target == NULL)
  {
    return ENODEV;
  }

#if defined (CONFIG_STM32F7_SPI_DMA)
  // The STM32F777 used in Meadow has a DMA transfer size limit of 65535
  // bytes. For more information see Ref Man section 8.3.6 and 8.3.16 the
  // last bullet,"...This means that a maximum of 65535 data items can be
  // managed by the DMA in a single transaction."
  if(data->txBuffer || data->rxBuffer)
  {
    if(data->length <= 0xffff)
    {
      SPI_EXCHANGE(target, data->txBuffer, data->rxBuffer, data->length);
    }
    else
    {
      uint32_t numbToSend = data->length;
      uint8_t *txTempBuf = data->txBuffer;
      uint8_t *rxTempBuf = data->rxBuffer;

      // There is also a DMA requirement that the number be mulitple of 4 or 2,
      // in some cases.
      while(numbToSend > 65532)
      {
        // syslog(LOG_MDIAG, "->Send Loop-to send %lu bytes, tx:%p->rx:%p\n",
        //           numbToSend, txTempBuf, rxTempBuf);
        SPI_EXCHANGE(target, txTempBuf, rxTempBuf, 65532);
        if(txTempBuf) txTempBuf += 65532;
        if(rxTempBuf) rxTempBuf += 65532;
        numbToSend -= 65532;
      }

      // syslog(LOG_MDIAG, "->Send Last-%lu bytes, tx:%p->rx:%p\n",
      //             numbToSend, txTempBuf, rxTempBuf);
      SPI_EXCHANGE(target, txTempBuf, rxTempBuf, numbToSend);
    }
  }
  else
  {
    // no read or write buffer
    return EINVAL;
  }
#else
  // if we have only outbuffer, it's a write
  if(data->txBuffer)
  {
    if(data->rxBuffer)
    {
      // writeread
      SPI_EXCHANGE(target, data->txBuffer, data->rxBuffer, data->length);
    }
    else
    {
      //write
      SPI_SNDBLOCK(target, data->txBuffer, data->length);
    }
  }
  else if(data->rxBuffer > 0)
  {
    // read
    SPI_RECVBLOCK(target, data->rxBuffer, data->length);
  }
  else
  {
    // no read or write buffer
    return EINVAL;
  }
#endif
  return OK;
}

static int upd_handle_i2c(int cmd, struct upd_i2c_cmd* data)
{
  if(cmd == MUPD_I2C_SHUTDOWN)
  {
    if(g_i2c1 != NULL)
    {
      stm32_i2cbus_uninitialize(g_i2c1);
      g_i2c1 = NULL;
    }
    return OK;
  }

  struct i2c_config_s *pCfg;
  struct i2c_master_s *pBus;

  if(data->busNumber == 0 || data->busNumber == 1)
  {
    if(g_i2c1 == NULL)
    {
      g_i2c1 = stm32_i2cbus_initialize(MEADOW_I2C_PORT1);
    }
    pBus = g_i2c1;
    pCfg = &g_i2c1_cfg;
  }
  else if(data->busNumber == 3)
  {
    if(g_i2c3 == NULL)
    {
      g_i2c3 = stm32_i2cbus_initialize(MEADOW_I2C_PORT3);
    }
    pBus = g_i2c3;
    pCfg = &g_i2c3_cfg;
  }
  else
  {
    return ENODEV;
  }

  pCfg->address = data->address;
  pCfg->addrlen = 7; // we currently are supporting only 7-bit address devices
  pCfg->frequency = data->frequency;

  int result = OK;

  // if we have only outbuffer, it's a write
  if(data->txLength > 0)
  {
    if(data->rxLength > 0)
    {
      // writeread
      result = i2c_writeread(pBus, pCfg, data->txBuffer, data->txLength, data->rxBuffer, data->rxLength);
    }
    else
    {
      //write
      result = i2c_write(pBus, pCfg, data->txBuffer, data->txLength);
    }
  }
  else if(data->rxLength > 0)
  {
    // read
    result = i2c_read(pBus, pCfg, data->rxBuffer, data->rxLength);
  }
  else
  {
    // no read or write buffer
    result = EINVAL;
  }
  
  return result;
}

int upd_handle_pwm(int cmd, unsigned long arg)
{
  struct upd_pwm_cmd *_upd_pwm_cmd = (struct upd_pwm_cmd *)arg;
  struct pwm_lowerhalf_s *pwm;

  /* Call stm32_pwminitialize() to get an instance of the PWM interface */
  pwm = stm32_pwminitialize(_upd_pwm_cmd->timer);
  if (!pwm)
  {    
    aerr("ERROR: Failed to get the STM32 PWM lower half\n");
    return -ENODEV;
  }

  switch(cmd)
  {
  case MUPD_PWM_SETUP:
  {
      pwm->ops->setup(pwm);
      return OK;
  }
  case MUPD_PWM_SHUTDOWN:
  {
      pwm->ops->shutdown(pwm);
      return OK;
  }
  case MUPD_PWM_START:
  {
      struct pwm_info_s info = {};
      info.frequency = _upd_pwm_cmd->frequency;
#ifdef CONFIG_PWM_MULTICHAN
      // pwm->ops->ioctl() has no implementation in the STM32 PWM lower
      // half (stm32_pwm.c's pwm_ioctl() unconditionally returns -ENOTTY
      // without touching its argument), so info.channels[] can't be
      // "read, then modify" - it has to be built from scratch here.
      // channel == -1 terminates the list for pwm_duty_channels_update().
      info.channels[0].channel = _upd_pwm_cmd->channel;
      info.channels[0].duty = _upd_pwm_cmd->duty;
      info.channels[1].channel = -1;
#else
      info.duty = _upd_pwm_cmd->duty;
#endif

      pwm->ops->start(pwm, &info);
      return OK;
  }
  case MUPD_PWM_STOP:
  {
      struct pwm_info_s info = {};
      info.frequency = _upd_pwm_cmd->frequency;
#ifdef CONFIG_PWM_MULTICHAN
      info.channels[0].channel = _upd_pwm_cmd->channel;
      info.channels[0].duty = 0;
      info.channels[1].channel = -1;
#else
      info.duty = 0;
#endif

      pwm->ops->start(pwm, &info);
      return OK;
  }
  }

  return OK;
}

// Called from hcom_via_nx_upd_driver_open() in hcom_via_nx_upd_driver_open.c
static int upd_open(struct file *filep)
{
  return OK;
}

static int upd_close(struct file *filep)
{
  return OK;
}

// The driver is opened O_RDONLY by hcom (hcom_via_nx_upd_driver_open()).
// This stub keeps such opens valid independently of the ioctl method, which
// is what currently satisfies the VFS access check in fs/vfs/fs_open.c.
static int upd_read(FAR struct file *filep, FAR char *buffer, size_t buflen)
{
  return OK;
}

#if defined (CONFIG_MEADOW_HCOM)
// ====================================================================
// This is also called to add serial number to USB
static int upd_hcom_get_mcu_ser_numb(unsigned long arg)
{
  char strMcuSn[16];
  struct hcom_nx_upd_mcu_ser_numb_s *mcu_ser;
  mcu_ser = (struct hcom_nx_upd_mcu_ser_numb_s *)arg;

  hcom_nx_common_utils_calc_f7_sn(NULL, strMcuSn);
  strcpy(mcu_ser->ser_numb, strMcuSn);
  return OK;
}

// ====================================================================
// For a UART that has been configured to output etc. reconfig to be
// Tx or Rx as needed for the following UARTs
static int upd_hcom_restore_uart_reconfig(unsigned long arg)
{
  struct hcom_nx_upd_uart_reconfig_s *uartReconfig;
  uartReconfig = (struct hcom_nx_upd_uart_reconfig_s *)arg;

  switch (uartReconfig->uart_id)
  {
  case MEADOW_RECONFIG_MISCONFIGURED_UART1:
    stm32_configgpio(GPIO_USART1_TX); // PB14
    stm32_configgpio(GPIO_USART1_RX); // PH13
    break;

  case MEADOW_RECONFIG_MISCONFIGURED_UART4:
    stm32_configgpio(GPIO_UART4_TX); // PH13
    stm32_configgpio(GPIO_UART4_RX); // PI9
    break;

  case MEADOW_RECONFIG_MISCONFIGURED_UART5:
    // F7v2 and CCMv2 use the same user GPIO pins
    if(meadow_hw_version_get() == MEADOW_F7_HW_VERSION_NUMB_F7V1)
      stm32_configgpio(GPIO_UART5_TX_V1); // PB13
    else
      stm32_configgpio(GPIO_UART5_TX_V2); // PC12
    stm32_configgpio(GPIO_UART5_RX); // PD2
    break;

  case MEADOW_RECONFIG_MISCONFIGURED_UART6:
    stm32_configgpio(GPIO_USART6_TX); // PC6
    stm32_configgpio(GPIO_USART6_RX); // PC7
    break;

  default:
    syslog(LOG_ERR, "%s@%d-restore uart reconfig, invalid uart:%" PRId32 "\n",
           __FILE__, __LINE__, uartReconfig->uart_id);
    return -1;
  }
  return OK;
}
#endif  // defined (CONFIG_MEADOW_HCOM)

/****************************************************************************
 * Name: upd_handle_esp32_command
 *
 * Description:
 *  Take the command for the ESP32 and package this up into a message to
 *  send to the ESP32.
 *
 * Input Parameters:
 *  data - structure containing a pointer to the structure containing the
 *         message information.
 *
 * Returned Value:
 *  OK if the command was executed or ERROR if there was a problem.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
int upd_handle_esp32_command(struct upd_esp32_command *data)
{
  int result = ERROR;
  uint8_t *payload = NULL;
  espcp_message_t *message = NULL;

  data->status_code = espcp_status_codes_failure;
  
  espcp_configuration_t *config = espcp_get_configuration();
  if (config == NULL)
  {
    return ERROR;
  }
  if (config->esp_not_responding)
  {
    data->status_code = espcp_status_codes_coprocessor_not_responding;
    return(ERROR);
  }
  if (data->payload_length != 0)
  {
    /*
      *  Note that we must take a copy of the message payload here as
      *  the process of sending the message that will be created next
      *  will dispose of the message and the payload after the message
      *  has been dealt with.  We do not want to mess with the managed
      *  memory so we take copies.
      */
    payload = (uint8_t *) malloc(data->payload_length);
    if (payload == NULL)
    {
      return ERROR;
    }
    memcpy(payload, data->payload, data->payload_length);
  }
  else
  {
    payload = NULL;
  }

  message = espcp_create_message_on_heap(espcp_message_types_header,
    data->interface, data->function, 0, espcp_get_next_message_id(),
    payload, data->payload_length);
  if (message == NULL)
  {
    if (payload != NULL)
    {
      free(payload);
    }
    return ERROR;
  }

  if (espcp_queue_message(message, data->block != 0) == espcp_status_codes_completed_ok)
  {
    result = OK;
    if (data->block != 0)
    {
      data->status_code = message->status_code;
      if (message->payload_length > 0)
      {
        if (message->payload_length <= data->result_length)
        {
          memcpy(data->result, message->payload, message->payload_length);
          data->result_length = message->payload_length;
        }
        else
        {
          result = ERROR;
        }
      }
    }

    else
    {
      //
      //  Non-blocking message should appear to succeed immediately
      //  as success or failure is normally indicated by an event.
      //
      data->status_code = espcp_status_codes_completed_ok;
    }
  }
  if (data->block != 0)
  {
    //
    //  We must free blocking messages here as they have served their purpose,
    //  non-blocking messages will be deleted by the messaging system when
    //  they have been sent to the ESP32.
    //
    espcp_delete_message_and_payload(message);
  }
  return(result);
}

/****************************************************************************
 * Name: upd_handle_esp32_get_event_result
 * 
 * Description:
 *  Get any payload and status information for events returning more than
 *  a trivial amount of data.
 * 
 *  This method is necessary as message queues can only hold 22 bytes of data.
 *  After overheads are taken into considertion this only leaves 13 bytes.  It
 *  was decided to use the payload and payload_length concept used with
 *  messages to transfer any data other than interface, function and status
 *  code.
 *
 * Input Parameters:
 *  data - pointer to a structure to hold the event data.  The memory should
 *         be allocated in the managed code.
 *
 * Returned Value:
 *  OK if the command was executed or ERROR if there was a problem.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
int upd_handle_esp32_get_event_result(espcp_event_data_payload_t *data)
{
  int result = OK;

  espcp_message_t *message = espcp_get_event_data(data->message_id);
  if (message != NULL)
  {
      if (data->payload_length >= message->payload_length)
      {
        if (data->payload_length > 0)
        {
          memcpy(data->payload, message->payload, message->payload_length);
        }
        data->payload_length = message->payload_length;
      }
      else
      {
        data->payload_length = 0;
        result = ERROR;
      }

      espcp_delete_message_and_payload(message);
  }
  else
  {
      result = ERROR;
  }
  
  return(result);
}

/****************************************************************************
 * Name: upd_get_set_configuration_value
 * 
 * Description:
 *  Process the request to read or write a configuration value.
 * 
 * Input Parameters:
 *  data - pointer to a structure holding the request.
 *
 * Returned Value:
 *  OK if the command was executed or ERROR if there was a problem.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
int upd_get_set_configuration_value(upd_get_set_configuration_value_t *data)
{
  data->returned_data_length = hcom_nx_config_get_set_config_value(data->item, data->direction, data->buffer, data->buffer_length);
  return((data->returned_data_length >= 0) ? OK : ERROR);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: stm32_gpio_initialize
 *
 * Description:
 *   Initialize GPIO drivers for use with /apps/examples/gpio
 *
 ****************************************************************************/

int meadow_upd_initialize(void)
{
  syslog(LOG_INFO, "meadow_upd_initialize\n");
  
  // register the driver, passing in our entry points
  int ret = register_driver(MEADOW_UPD_DRIVER_NAME, &g_driver_operations,
              0666, NULL);
  if (ret)
  {
    return ERROR;
  }

  return OK;
}

