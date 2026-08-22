/* Host stubs that back /meadow0 with a real directory, so the commit path can
 * be run and its effects inspected directly.
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
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#define FAR
#define OK 0
#define UNUSED(x) ((void)(x))
#define locate_data(sect)
#define kumm_zalloc(n) calloc(1, n)
#define kumm_free(p)   free(p)
#define meadow_storage_mode_set_host_engaged() ((void)0)

#define MONO_MEADOW_FILE_SYSTEM_MOUNTED_NAME "/meadow0"

#define DTYPE_DIRECTORY DT_DIR
#define LOG_ERR 3
#define LOG_WARNING 4
#define LOG_NOTICE 5

extern char g_root[512];              /* real directory backing /meadow0 */
const char *fs_real(const char *vpath);   /* map a virtual path to a real one */

void fatshim_log(int prio, const char *fmt, ...);
#define syslog fatshim_log

/* Directory and stat go through the real filesystem, path-mapped. */
#define opendir(p)      opendir(fs_real(p))
#define nx_stat(p,b,r)  fs_stat(p,b)
int fs_stat(const char *vpath, struct stat *buf);

/* File access, path-mapped, over real descriptors. */
struct file { int fd; };
int     file_open(struct file *f, const char *vpath, int oflags, ...);
off_t   file_seek(struct file *f, off_t off, int whence);
ssize_t file_read(struct file *f, void *buf, size_t n);
ssize_t file_write(struct file *f, const void *buf, size_t n);
int     file_close(struct file *f);

/* Mutations the commit path performs. */
#define mkdir(p,m)   mkdir(fs_real(p), m)
#define rmdir(p)     rmdir(fs_real(p))
#define unlink(p)    unlink(fs_real(p))
int fs_rename(const char *a, const char *b);
#define rename(a,b)  fs_rename(a,b)
int nx_unlink(const char *p);

/* Work queue: the harness calls the worker directly. */
struct work_s { int dummy; };
#define LPWORK 1
#define MSEC2TICK(m) (m)
static inline int work_cancel(int q, struct work_s *w)
  { UNUSED(q); UNUSED(w); return 0; }
static inline int work_queue(int q, struct work_s *w, void (*f)(void *),
                             void *a, int d)
  { UNUSED(q); UNUSED(w); UNUSED(f); UNUSED(a); UNUSED(d); return 0; }

typedef int mutex_t;
static inline void nxmutex_init(mutex_t *m)   { *m = 0; }
static inline int  nxmutex_lock(mutex_t *m)   { UNUSED(m); return 0; }
static inline void nxmutex_unlock(mutex_t *m) { UNUSED(m); }

#endif
