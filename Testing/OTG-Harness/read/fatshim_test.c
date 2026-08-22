/* Host harness: builds the model from a simulated /meadow0 and writes the
 * whole synthesized volume to a file, so the layout can be checked with real
 * FAT implementations before any of it reaches hardware.
 */

#include "fatshim_host_stubs.h"
#include <stdarg.h>

/* ---- the simulated /meadow0 ------------------------------------------- */

struct tnode
{
  const char *path;     /* full path, no trailing slash */
  int         isdir;
  long        size;
};

static const struct tnode g_tree[] =
{
  { "/meadow0/Meadow.dll",                 0, 40000 },
  { "/meadow0/App.dll",                    0, 4096  },   /* exact cluster */
  { "/meadow0/README",                     0, 11    },
  { "/meadow0/EMPTY.TXT",                  0, 0     },   /* zero length */
  { "/meadow0/meadow.config.yaml",         0, 300   },
  { "/meadow0/wifi.config.yaml",           0, 180   },
  { "/meadow0/cell.config.yaml",           0, 170   },
  { "/meadow0/meadow.log",                 0, 9000  },
  { "/meadow0/TabDelimited200Names.txt",   0, 5000  },
  { "/meadow0/averyveryverylongfilename_one.dat",   0, 100 },
  { "/meadow0/averyveryverylongfilename_two.dat",   0, 100 },
  { "/meadow0/update",                     1, 0     },
  { "/meadow0/update/os",                  1, 0     },
  { "/meadow0/update/os/Meadow.OS.Update.bin", 0, 70000 },
  { "/meadow0/rollback",                   1, 0     },
  { "/meadow0/rollback/previous.bin",      0, 12345 },
  { "/meadow0/crash",                      1, 0     },
  { "/meadow0/crash/oscrash_report.txt",   0, 700   },
  { "/meadow0/system",                     1, 0     },
  { "/meadow0/system/os_persist.dat",      0, 64    },
};

#define TREE_N ((int)(sizeof(g_tree) / sizeof(g_tree[0])))

/* Deterministic content so reads can be checked byte for byte. */
static unsigned char content_byte(int idx, long off)
{
  return (unsigned char)((idx * 37 + off * 7 + (off >> 8) * 13) & 0xff);
}

/* ---- directory enumeration -------------------------------------------- */

struct fatshim_test_dir_s
{
  char base[256];
  int  next;
  struct fatshim_test_dirent_s de;
};

void fatshim_test_log(int prio, const char *fmt, ...)
{
  va_list ap;
  UNUSED(prio);
  fputs("  [log] ", stderr);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
}

fatshim_test_dir_s *fatshim_test_opendir(const char *path)
{
  fatshim_test_dir_s *d;
  int i;
  int found = (strcmp(path, "/meadow0") == 0);

  for (i = 0; !found && i < TREE_N; i++)
    {
      if (g_tree[i].isdir && strcmp(g_tree[i].path, path) == 0) found = 1;
    }

  if (!found) return NULL;

  d = calloc(1, sizeof(*d));
  snprintf(d->base, sizeof(d->base), "%s", path);
  d->next = 0;
  return d;
}

struct fatshim_test_dirent_s *fatshim_test_readdir(fatshim_test_dir_s *d)
{
  size_t blen = strlen(d->base);

  while (d->next < TREE_N)
    {
      const struct tnode *t = &g_tree[d->next++];

      if (strncmp(t->path, d->base, blen) != 0)      continue;
      if (t->path[blen] != '/')                      continue;
      if (strchr(&t->path[blen + 1], '/') != NULL)   continue;  /* deeper */

      snprintf(d->de.d_name, sizeof(d->de.d_name), "%s", &t->path[blen + 1]);
      d->de.d_type = t->isdir ? DTYPE_DIRECTORY : DTYPE_FILE;
      return &d->de;
    }

  return NULL;
}

int fatshim_test_closedir(fatshim_test_dir_s *d) { free(d); return 0; }

int nx_stat(const char *path, struct stat *buf, int resolve)
{
  int i;
  UNUSED(resolve);
  for (i = 0; i < TREE_N; i++)
    {
      if (strcmp(g_tree[i].path, path) == 0)
        {
          memset(buf, 0, sizeof(*buf));
          buf->st_size = g_tree[i].size;
          buf->st_mode = g_tree[i].isdir ? S_IFDIR : S_IFREG;
          return 0;
        }
    }
  return -ENOENT;
}

/* ---- file access ------------------------------------------------------ */

int file_open(struct file *f, const char *path, int oflags, ...)
{
  int i;
  UNUSED(oflags);
  for (i = 0; i < TREE_N; i++)
    {
      if (!g_tree[i].isdir && strcmp(g_tree[i].path, path) == 0)
        {
          f->index = i;
          f->pos = 0;
          return 0;
        }
    }
  return -ENOENT;
}

off_t file_seek(struct file *f, off_t offset, int whence)
{
  UNUSED(whence);
  f->pos = (long)offset;
  return offset;
}

ssize_t file_read(struct file *f, void *buf, size_t nbytes)
{
  unsigned char *out = buf;
  long size = g_tree[f->index].size;
  size_t n = 0;

  while (n < nbytes && f->pos < size)
    {
      out[n++] = content_byte(f->index, f->pos++);
    }
  return (ssize_t)n;
}

int file_close(struct file *f) { f->index = -1; return 0; }

/* ---- the code under test --------------------------------------------- */

#include "hcom_nx_fatshim.c"

/* ---- main ------------------------------------------------------------ */

int main(int argc, char **argv)
{
  FILE *out;
  unsigned char sector[FS_SECTORSIZE];
  uint32_t lba;
  int i;

  if (argc < 2) { fprintf(stderr, "usage: %s <image>\n", argv[0]); return 2; }

  printf("geometry: sectorsize=%d secperclus=%d clusters=%d\n",
         FS_SECTORSIZE, FS_SECPERCLUS, FS_DATACLUSTERS);
  printf("          fatsecs=%d rootdirsecs=%d datastart=%d total=%d (%d KB)\n",
         FS_SECPERFAT, FS_ROOTDIRSECS, FS_DATASTART, FS_TOTALSECTORS,
         (FS_TOTALSECTORS * FS_SECTORSIZE) / 1024);

  if (model_build() < 0) { fprintf(stderr, "model_build failed\n"); return 1; }

  printf("model: %d entries, %d name bytes, %u root slots, %u clusters\n",
         _entrycount, _namepoolused, (unsigned)_rootslots,
         (unsigned)(_nextcluster - FS_FIRSTCLUSTER));

  for (i = 0; i < _entrycount; i++)
    {
      struct fatshim_entry_s *e = &_entries[i];
      char sn[12];
      memcpy(sn, e->shortname, 11); sn[11] = 0;
      printf("  %2d %-42s parent=%3d %s slots=%u@%-4u clus=%u+%u size=%u"
             " short='%s'\n",
             i, entry_name(i), e->parent, e->isdir ? "DIR " : "file",
             e->nslots, e->dirslot, e->firstcluster, e->nclusters,
             e->size, sn);
    }

  out = fopen(argv[1], "wb");
  if (!out) { perror("fopen"); return 1; }

  for (lba = 0; lba < FS_TOTALSECTORS; lba++)
    {
      synth_sector(lba, sector);
      if (fwrite(sector, 1, FS_SECTORSIZE, out) != FS_SECTORSIZE)
        { perror("fwrite"); return 1; }
    }

  fclose(out);
  printf("wrote %s (%d bytes)\n", argv[1], FS_TOTALSECTORS * FS_SECTORSIZE);
  return 0;
}
