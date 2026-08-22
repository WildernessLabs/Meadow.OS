# Off-target tests for the /meadow0 FAT view

`hcom_nx_fatshim.c` synthesizes a FAT16 volume from the LittleFS contents of
`/meadow0`, and the part of it that computes sector contents touches no file
system at all. That part can therefore be compiled and run on a development
machine, which is what these harnesses do: `FATSHIM_HOST_TEST` swaps a stub
header in for the NuttX surface and nothing else in the driver changes.

This matters because the layout has to be byte-exact and a host operating
system is an unforgiving reader. Three of the defects found during development
were caught here rather than on hardware, including a rename that would have
written a zero-filled file over good data.

It is equally worth being clear about what these cannot see. They exercise the
synthesis and the commit logic; they do not exercise `usbmsc`, the composite
bind, `find_blockdriver`, or a real host. Every bug that reached hardware lived
in one of those, so a clean run here is necessary and nowhere near sufficient.

## read/ - the read-only view

Builds the view of a simulated `/meadow0` and writes the whole volume to an
image, which can then be checked with real FAT implementations.

    cd read
    cc -std=c11 -Wall -DFATSHIM_HOST_TEST -I. -I../.. -o fatshim_test fatshim_test.c
    ./fatshim_test /tmp/meadow0.img

    fsck_msdos -n /tmp/meadow0.img          # Apple's checker, stricter than a mount

    docker run --rm --privileged -v /tmp:/img alpine:latest sh -c '
      mkdir -p /mnt/t && mount -t vfat -o ro,loop /img/meadow0.img /mnt/t
      ls -la /mnt/t && find /mnt/t -type f | while read f; do md5sum "$f"; done
      umount /mnt/t'

The simulated tree in `fatshim_test.c` is chosen to hit the awkward cases: a
file spanning many clusters, one exactly a cluster long, a zero length file,
three levels of nesting, and two long names sharing a prefix so that the `~n`
short-name aliases have to disambiguate.

## write/ - the write and commit path

Rather than guessing how a host writes, this lets Linux be the host. The view
is emitted to an image, the image is mounted read-write so the `vfat` driver
modifies it, and the sectors that differ are then replayed through
`write_sector()` **in deliberately shuffled order** before the commit runs.

    cd write
    cc -std=c11 -g -fsanitize=address -DFATSHIM_HOST_TEST \
       -DCONFIG_MEADOW_USBMSC_FATSHIM_WRITE \
       -DCONFIG_MEADOW_USBMSC_FATSHIM_SETTLE_MS=2000 \
       -DCONFIG_MEADOW_USBMSC_FATSHIM_STAGE_KB=16384 \
       -I. -I../.. -o fatshim_wtest fatshim_wtest.c

    ./fatshim_wtest emit   <root> <image>              # build the view of <root>
    ./fatshim_wtest apply  <root> <before> <after>     # replay a diff, then commit twice
    ./fatshim_wtest apply2 <root> <a> <b> <c>          # two rounds in one session

`<root>` is a real directory standing in for `/meadow0`, and the commit writes
to it, so its contents afterwards are the result to inspect.

Two properties are worth keeping in the tests, because both correspond to bugs
that were actually shipped and then fixed:

* `apply` commits **twice**. The second must report zero changes. Before the
  model was updated after each change, a second commit re-applied everything,
  and for a rename that meant writing a zero filled file over the data.
* `apply2` runs two rounds in one session, the second renaming a file the first
  created. That is the case a "has this cluster ever been staged" test gets
  wrong, because the clusters are still in the staging buffer from round one.

Build with `-fsanitize=address`. It is free here and the driver indexes a good
deal of memory by cluster number.

## A trap

`<root>` is mutated by `apply`, so an image emitted from it beforehand no longer
matches afterwards. Re-running against a stale image reports changes to files
nobody touched, which looks alarming and is not real. Reset the directory
between runs.
