#ifndef __INC_NUTTX_FUNCTIONS__
#define __INC_NUTTX_FUNCTIONS__

#include <errno.h>
#include <ifaddrs.h>
#include <malloc.h>
#include <mqueue.h>
#include <net/if.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/statfs.h>
#include <termios.h>

extern int meadow_gpio_write(int pin, bool state);
extern int mount(const char *source, const char *target, const char *filesystemtype, unsigned long mountflags, const void *data);
extern int umount2(const char *target, int flags);
extern int meadow_cloud_decrypt_buf(const char *encrypted_buf, int encrypted_len, const char *decrypted_buf);
extern int meadow_cloud_decrypt_buf_aes(const char *encrypted_buf, int encrypted_len, unsigned char key[16], unsigned char iv[16], const char *decrypted_buf);
#if defined(CONFIG_MEADOW_CELL_INCLUDE_IN_BUILD)
/* Implemented by the hcom cell driver (apps/system/hcom/cell/hcom_cell.c). */
extern void meadow_cell_change_state(int state);
extern bool meadow_cell_is_connected(void);
extern int meadow_get_cell_error(void);
extern int meadow_get_cell_at_cmds_output(unsigned char *buf);
#else
/* Cellular support is not part of this build.  Meadow.Core still binds these
 * entry points, so answer "not connected" / "not supported" instead of
 * failing the P/Invoke. */
static void meadow_cell_change_state(int state) { (void)state; }
static bool meadow_cell_is_connected(void) { return false; }
static int meadow_get_cell_error(void) { return -ENOSYS; }
static int meadow_get_cell_at_cmds_output(const char *buf) { (void)buf; return -ENOSYS; }
#endif
extern int meadow_idle_monitor_get_value(void);
extern int meadow_adc_configure(uint8_t gpioList[], uint32_t gpioCount, double *userDataBuf);
extern int meadow_adc_read_values(void);
extern int meadow_adc_read_temp_vbat(double *batteryVoltage, double *temperatureValue);
extern int meadow_read_file_total_free_flash_size(uint32_t *totalBytes,  uint32_t *freeBytes);
extern int statfs(FAR const char *path, FAR struct statfs *buf);
extern int pwrmgmt_most_recent_wakeup_reason(void);
extern uint32_t meadow_os_power_cycle_count(void);
extern uint32_t meadow_os_reset_cycle_count(void);
extern uint32_t meadow_os_reset_reason(void);
extern uint32_t meadow_os_hardware_version(void);
extern uint32_t meadow_os_native_protocol_version(void);
extern int meadow_os_get_gateway_address(char *buffer);
extern int meadow_measure_freq_configure(void* mdwCfgTimerChan);
extern int meadow_measure_freq_return_freq_info(void* mdwFreqReturnData);
extern int meadow_rotary_encoder_config(void* rotencCfg);
extern int meadow_rotary_encoder_set_count(uint32_t encoderNumb, int32_t encoderCount);
extern int stm32_configgpio(uint32_t cfgset);
extern void stm32_gpiowrite(uint32_t pinset, bool value);
extern int stm32_unconfiggpio(uint32_t cfgset);

const MonoDlMapping meadow_mappings[] = {
       { "open", open },
       { "close", close },
       { "ioctl", ioctl },
       { "read", read },
       { "write", write },
       { "clock_gettime", clock_gettime },
       { "clock_settime", clock_settime },
       { "malloc", malloc },
       { "free", free },
       { "nx_freeifaddrs", freeifaddrs },
       { "nx_getifaddrs", getifaddrs },
       { "nx_if_nametoindex", if_nametoindex },
       { "sigaction", sigaction },
       { "sigaddset", sigaddset },
       { "sigdelset", sigdelset },
       { "sigemptyset", sigemptyset },
       { "sigismember", sigismember },
       { "sigpending", sigpending },
       { "sigprocmask", sigprocmask },
       { "sigqueue", sigqueue },
       { "sigsuspend", sigsuspend },
       { "sigtimedwait", sigtimedwait },
       { "sigwaitinfo", sigwaitinfo },
       { "statfs", statfs },
       { "getpid", getpid},
       { "mq_close", mq_close },
       { "mq_getattr", mq_getattr },
       { "mq_notify", mq_notify },
       { "mq_open", mq_open },
       { "mq_receive", mq_receive },
       { "mq_send", mq_send },
       { "mq_setattr", mq_setattr },
       { "mq_timedreceive", mq_timedreceive },
       { "mq_timedsend", mq_timedsend },
       { "mq_unlink", mq_unlink },
       { "pipe", pipe },
       { "cfgetspeed", cfgetspeed },
       { "cfsetspeed", cfsetspeed },
       { "tcgetattr", tcgetattr },
       { "tcsetattr", tcsetattr },
       { "mount", mount },
       { "umount2", umount2 },
       { "mallinfo", mallinfo },
       { "meadow_cloud_decrypt_buf", meadow_cloud_decrypt_buf },
       { "meadow_cloud_decrypt_buf_aes", meadow_cloud_decrypt_buf_aes },
       { "meadow_cell_is_connected", meadow_cell_is_connected },
       { "meadow_get_cell_error", meadow_get_cell_error },
       { "meadow_idle_monitor_get_value", meadow_idle_monitor_get_value },
       { "meadow_get_cell_at_cmds_output", meadow_get_cell_at_cmds_output},
       { "meadow_cell_change_state", meadow_cell_change_state},
       { "meadow_adc_configure", meadow_adc_configure},
       { "meadow_adc_read_values", meadow_adc_read_values},
       { "meadow_adc_read_temp_vbat", meadow_adc_read_temp_vbat},
       { "meadow_read_file_total_free_flash_size", meadow_read_file_total_free_flash_size},
       { "pwrmgmt_most_recent_wakeup_reason", pwrmgmt_most_recent_wakeup_reason},
       { "meadow_os_power_cycle_count", meadow_os_power_cycle_count},
       { "meadow_os_reset_cycle_count", meadow_os_reset_cycle_count},
       { "meadow_os_reset_reason", meadow_os_reset_reason},
       { "meadow_os_hardware_version", meadow_os_hardware_version},
       { "meadow_os_native_protocol_version", meadow_os_native_protocol_version},
       { "meadow_os_get_gateway_address", meadow_os_get_gateway_address},
       { "meadow_os_coprocessor_deep_sleep", meadow_os_coprocessor_deep_sleep},
       { "meadow_measure_freq_configure", meadow_measure_freq_configure},
       { "meadow_measure_freq_return_freq_info", meadow_measure_freq_return_freq_info},
       { "meadow_rotary_encoder_config", meadow_rotary_encoder_config},
       { "meadow_rotary_encoder_set_count", meadow_rotary_encoder_set_count},
       { "stm32_configgpio", stm32_configgpio },
       { "stm32_gpiowrite", stm32_gpiowrite },
       { "stm32_unconfiggpio", stm32_unconfiggpio },
       { NULL, NULL }
};

#endif // __INC_NUTTX_FUNCTIONS__
