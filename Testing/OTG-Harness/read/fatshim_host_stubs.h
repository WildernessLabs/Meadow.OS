/* Host-side stand-ins for the NuttX kernel surface the fatshim uses.  Only
 * enough to compile the model builder and the synthesis engine; the block
 * operations are compiled out under FATSHIM_HOST_TEST.
 */

#ifndef FATSHIM_HOST_STUBS_H
#define FATSHIM_HOST_STUBS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#define FAR
#define OK 0
#define UNUSED(x) ((void)(x))
#define locate_data(sect)
#define meadow_storage_mode_set_host_engaged() ((void)0)

#define MONO_MEADOW_FILE_SYSTEM_MOUNTED_NAME "/meadow0"

#define DTYPE_DIRECTORY 4
#define DTYPE_FILE      8

#define LOG_ERR     3
#define LOG_WARNING 4
#define LOG_NOTICE  5

/* Named to avoid colliding with the host's own declarations. */
#define syslog       fatshim_test_log
#define opendir      fatshim_test_opendir
#define readdir      fatshim_test_readdir
#define closedir     fatshim_test_closedir
#define DIR          fatshim_test_dir_s
#define dirent       fatshim_test_dirent_s

void fatshim_test_log(int prio, const char *fmt, ...);

struct fatshim_test_dirent_s
{
  char    d_name[128];
  uint8_t d_type;
};

typedef struct fatshim_test_dir_s fatshim_test_dir_s;

fatshim_test_dir_s *fatshim_test_opendir(const char *path);
struct fatshim_test_dirent_s *fatshim_test_readdir(fatshim_test_dir_s *d);
int fatshim_test_closedir(fatshim_test_dir_s *d);

int nx_stat(const char *path, struct stat *buf, int resolve);

struct file
{
  int index;
  long pos;
};

int     file_open(struct file *f, const char *path, int oflags, ...);
off_t   file_seek(struct file *f, off_t offset, int whence);
ssize_t file_read(struct file *f, void *buf, size_t nbytes);
int     file_close(struct file *f);

typedef int mutex_t;
static inline void nxmutex_init(mutex_t *m)   { *m = 0; }
static inline int  nxmutex_lock(mutex_t *m)   { UNUSED(m); return 0; }
static inline void nxmutex_unlock(mutex_t *m) { UNUSED(m); }

/* Clock and stall, for the instrumentation in the driver. The harness has no
 * ticks, so a tick is a millisecond and TICK2MSEC is the identity; the stall is
 * deliberately a no-op, since off-target there is no host to keep waiting.
 */
#include <time.h>
#define TICK2MSEC(t) (t)
static inline uint32_t clock_systime_ticks(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)((uint32_t)ts.tv_sec * 1000u +
                    (uint32_t)(ts.tv_nsec / 1000000));
}

static inline int nxsig_usleep(unsigned int us) { UNUSED(us); return 0; }

#endif /* FATSHIM_HOST_STUBS_H */
