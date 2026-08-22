/* Write-path harness.
 *
 *   emit  <root> <image>            build the view of <root> into <image>
 *   apply <root> <before> <after>   feed the sectors that differ between the
 *                                   two images through the write path, in
 *                                   shuffled order, then commit
 *
 * Between the two, Linux's own FAT driver is what modifies the image, so the
 * writes being replayed are those a real host produces rather than my guess at
 * them.
 */

#include "fatshim_host_stubs.h"
#include <stdarg.h>

char g_root[512];
static int g_quiet;

const char *fs_real(const char *vpath)
{
  static char buf[1024];
  const char *v = "/meadow0";
  size_t n = strlen(v);

  if (strncmp(vpath, v, n) == 0)
    {
      snprintf(buf, sizeof(buf), "%s%s", g_root, vpath + n);
    }
  else
    {
      snprintf(buf, sizeof(buf), "%s", vpath);
    }
  return buf;
}

void fatshim_log(int prio, const char *fmt, ...)
{
  va_list ap;
  UNUSED(prio);
  if (g_quiet) return;
  fputs("  [fatshim] ", stdout);
  va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
}

int fs_stat(const char *vpath, struct stat *buf) { return stat(fs_real(vpath), buf); }

int fs_rename(const char *a, const char *b)
{
  char from[1024];
  snprintf(from, sizeof(from), "%s", fs_real(a));
  /* Parenthesised so the "rename" macro does not expand into this function. */
  return (rename)(from, fs_real(b));
}

int nx_unlink(const char *p) { return unlink(p); }

int file_open(struct file *f, const char *vpath, int oflags, ...)
{
  int fd = open(fs_real(vpath), oflags, 0644);
  if (fd < 0) return -errno;
  f->fd = fd; return 0;
}
off_t   file_seek(struct file *f, off_t o, int w) { return lseek(f->fd, o, w); }
ssize_t file_read(struct file *f, void *b, size_t n) { return read(f->fd, b, n); }
ssize_t file_write(struct file *f, const void *b, size_t n) { return write(f->fd, b, n); }
int     file_close(struct file *f) { close(f->fd); f->fd = -1; return 0; }

#include "hcom_nx_fatshim.c"

static int emit(const char *image)
{
  unsigned char sec[FS_SECTORSIZE];
  FILE *out = fopen(image, "wb");
  uint32_t lba;

  if (!out) { perror("fopen"); return 1; }
  if (model_build() < 0) { fprintf(stderr, "model_build failed\n"); return 1; }
  for (lba = 0; lba < FS_TOTALSECTORS; lba++)
    {
      read_sector(lba, sec);
      fwrite(sec, 1, FS_SECTORSIZE, out);
    }
  fclose(out);
  printf("emitted %s: %d entries, %u clusters used\n", image, _entrycount,
         (unsigned)(_nextcluster - FS_FIRSTCLUSTER));
  return 0;
}

/* Replays the sectors that differ between two images, in shuffled order. */
static int replay(const char *before, const char *after)
{
  FILE *fa = fopen(before, "rb");
  FILE *fb = fopen(after, "rb");
  unsigned char a[FS_SECTORSIZE], b[FS_SECTORSIZE];
  static uint32_t changed[8192];
  int nchanged = 0;
  uint32_t lba;
  int i;

  if (!fa || !fb) { perror("fopen"); return 1; }

  for (lba = 0; lba < FS_TOTALSECTORS; lba++)
    {
      if (fread(a, 1, FS_SECTORSIZE, fa) != FS_SECTORSIZE) break;
      if (fread(b, 1, FS_SECTORSIZE, fb) != FS_SECTORSIZE) break;
      if (memcmp(a, b, FS_SECTORSIZE) != 0 &&
          nchanged < (int)(sizeof(changed)/sizeof(changed[0])))
        changed[nchanged++] = lba;
    }
  printf("  %d sectors differ\n", nchanged);

  for (i = nchanged - 1; i > 0; i--)
    { int j = (i * 7919 + 13) % (i + 1);
      uint32_t t = changed[i]; changed[i] = changed[j]; changed[j] = t; }

  for (i = 0; i < nchanged; i++)
    {
      fseek(fb, (long)changed[i] * FS_SECTORSIZE, SEEK_SET);
      if (fread(b, 1, FS_SECTORSIZE, fb) != FS_SECTORSIZE) break;
      if (write_sector(changed[i], b) < 0)
        { fprintf(stderr, "write_sector %u failed\n", changed[i]); return 1; }
    }

  fclose(fa); fclose(fb);
  return 0;
}

/* Two rounds in one session: the second is a rename of a file the first
 * created. That is the case "has it ever been staged" got wrong, because the
 * clusters are still in the staging buffer from round one.
 */
static int apply2(const char *a, const char *b, const char *c)
{
  if (model_build() < 0) { fprintf(stderr, "model_build failed\n"); return 1; }

  printf("round 1: create\n");
  if (replay(a, b)) return 1;
  commit_apply();

  printf("round 2: rename what round 1 created\n");
  if (replay(b, c)) return 1;
  commit_apply();

  stage_release();
  return 0;
}

static int apply(const char *before, const char *after)
{
  FILE *fa = fopen(before, "rb");
  FILE *fb = fopen(after, "rb");
  unsigned char a[FS_SECTORSIZE], b[FS_SECTORSIZE];
  static uint32_t changed[8192];
  int nchanged = 0;
  uint32_t lba;
  int i;

  if (!fa || !fb) { perror("fopen"); return 1; }
  if (model_build() < 0) { fprintf(stderr, "model_build failed\n"); return 1; }

  for (lba = 0; lba < FS_TOTALSECTORS; lba++)
    {
      if (fread(a, 1, FS_SECTORSIZE, fa) != FS_SECTORSIZE) break;
      if (fread(b, 1, FS_SECTORSIZE, fb) != FS_SECTORSIZE) break;
      if (memcmp(a, b, FS_SECTORSIZE) != 0 &&
          nchanged < (int)(sizeof(changed)/sizeof(changed[0])))
        {
          changed[nchanged++] = lba;
        }
    }
  printf("host changed %d sectors\n", nchanged);

  /* Shuffle, so the replay order is deliberately not the order a host used.
   * The design is meant to be order independent; this is where that is tested.
   */
  for (i = nchanged - 1; i > 0; i--)
    {
      int j = (i * 7919 + 13) % (i + 1);
      uint32_t t = changed[i]; changed[i] = changed[j]; changed[j] = t;
    }

  for (i = 0; i < nchanged; i++)
    {
      fseek(fb, (long)changed[i] * FS_SECTORSIZE, SEEK_SET);
      if (fread(b, 1, FS_SECTORSIZE, fb) != FS_SECTORSIZE) break;
      if (write_sector(changed[i], b) < 0)
        { fprintf(stderr, "write_sector %u failed\n", changed[i]); return 1; }
    }

  fclose(fa); fclose(fb);
  printf("replayed in shuffled order, committing\n");
  commit_apply();

  /* Commit again with nothing new. A second pass must be a no-op; before the
   * model was updated after each change it re-applied everything, and for a
   * rename that meant writing a zero filled file.
   */
  printf("--- second commit, expecting 0 changes ---\n");
  commit_apply();

  stage_release();
  return 0;
}

int main(int argc, char **argv)
{
  if (argc < 3) { fprintf(stderr, "usage: emit|apply ...\n"); return 2; }
  snprintf(g_root, sizeof(g_root), "%s", argv[2]);

  if (strcmp(argv[1], "emit") == 0 && argc == 4)  return emit(argv[3]);
  if (strcmp(argv[1], "apply") == 0 && argc == 5) return apply(argv[3], argv[4]);
  if (strcmp(argv[1], "apply2") == 0 && argc == 6)
    {
      if (model_build() < 0) return 1;
      return apply2(argv[3], argv[4], argv[5]);
    }
  fprintf(stderr, "bad arguments\n");
  return 2;
}
