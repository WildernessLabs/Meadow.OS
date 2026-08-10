############################################################################
# boards/arm/stm32f7/stm32f777zit6-meadow/kernel/Makefile
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

include $(TOPDIR)/Make.defs
include $(TOPDIR)/$(patsubst "%",%,$(CONFIG_APPS_DIR))/system/hcom/Make.defs

# APPDIR is normally set up by the top-level apps build (nuttx/tools/Make.defs), which
# this board Makefile never includes - define it here so the mbedTLS object lookup
# below resolves correctly.
APPDIR := $(TOPDIR)/$(patsubst "%",%,$(CONFIG_APPS_DIR))

# Force the whole mbedTLS library (crypto + x509 + TLS, from apps/crypto/mbedtls) into
# the final link, even for symbols nothing in the statically-linked C/C++ code
# references directly (e.g. Mono/.NET code that calls into mbedTLS via reflection/
# interop, invisible to the linker).
#
# apps/Make.defs's archiving step (AROBJSRULES) copies every compiled object to a
# second, "_<N>"-suffixed name purely so ar can batch it into libapps.a without a
# member-name collision - both names hold identical content. Only keep the plain name,
# or the pair would be linked in directly (bypassing archive semantics) and give the
# linker two definitions of every mbedTLS symbol.
ifneq ($(CONFIG_CRYPTO_MBEDTLS),)
MBEDTLS_AOBJS := $(shell find $(APPDIR)/crypto/mbedtls/mbedtls/library -maxdepth 1 -name '*$(OBJEXT)' 2>/dev/null | grep -vE '_[0-9]+\.o$$')
endif

# The entry point name (if none is provided in the .config file)

CONFIG_INIT_ENTRYPOINT ?= user_start
ENTRYPT = $(patsubst "%",%,$(CONFIG_INIT_ENTRYPOINT))

# Get the paths to the libraries and the links script path in format that
# is appropriate for the host OS

USER_LIBPATHS = $(addprefix -L,$(call CONVERT_PATH,$(addprefix $(TOPDIR)$(DELIM),$(dir $(USERLIBS)))))
USER_LDSCRIPT = -T $(call CONVERT_PATH,$(BOARD_DIR)$(DELIM)scripts$(DELIM)memory.ld)
USER_LDSCRIPT += -T $(call CONVERT_PATH,$(BOARD_DIR)$(DELIM)scripts$(DELIM)user-space.ld)
USER_HEXFILE += $(call CONVERT_PATH,$(TOPDIR)$(DELIM)nuttx_user.hex)
USER_SRECFILE += $(call CONVERT_PATH,$(TOPDIR)$(DELIM)nuttx_user.srec)
USER_BINFILE += $(call CONVERT_PATH,$(TOPDIR)$(DELIM)nuttx_user.bin)

USER_LDFLAGS = --undefined=$(ENTRYPT) --entry=$(ENTRYPT) $(USER_LDSCRIPT) --undefined=sqlite3_libversion
USER_LDFLAGS += --undefined=get_errno --undefined=set_errno
USER_LIBGCC = "${shell "$(CC)" $(ARCHCPUFLAGS) -print-libgcc-file-name}"

# Force the whole math library into the final link, even for symbols
# nothing in the statically-linked code references directly. Code
# dlopen()'d at runtime (e.g. an AoT-compiled TensorFlow model) resolves its
# external references against nuttx_user.elf's own symbol table, exported
# via elf_globals.S - which only ever lists symbols that already made it
# into this link. Without this, a libm function such as fmodf that nothing
# in the static image happens to call would never be present to export,
# however many times elf_globals.S is regenerated.
#
# This build uses CONFIG_LIBM_TOOLCHAIN=y, so libm.a is never a NuttX-built
# staging/libm.a in USERLIBS - it is the ARM toolchain's own libm for the
# active multilib variant, discovered the same way USER_LIBGCC discovers
# libgcc.a below.
#
# libc.a is deliberately NOT whole-archived: it mixes user-safe code with
# kernel-only helpers (e.g. lib_fileinstream.c's file_read/file_open,
# lib_blkoutstream.c's open_blockdriver/close_blockdriver) that don't exist
# in the user-space link at all - whole-archiving it pulls those in and the
# link fails outright. get_errno/set_errno (also in libc.a, added above)
# are pulled in individually via --undefined= instead.
USER_LIBM = "${shell "$(CC)" $(ARCHCPUFLAGS) -print-file-name=libm$(LIBEXT)}"
USER_LDLIBS = $(patsubst lib%,-l%,$(basename $(notdir $(USERLIBS))))

# Source files

CSRCS = stm32_userspace.c
COBJS = $(CSRCS:.c=$(OBJEXT))
OBJS  = $(COBJS)

# Targets:

all: $(TOPDIR)$(DELIM)nuttx_user.elf $(TOPDIR)$(DELIM)User.map
.PHONY: nuttx_user.elf depend clean distclean

$(COBJS): %$(OBJEXT): %.c
	$(call COMPILE, $<, $@)

# Create the nuttx_user.elf file containing all of the user-mode code

nuttx_user.elf: $(OBJS)
	$(Q) $(LD) -o $@ $(USER_LDFLAGS) $(USER_LIBPATHS) $(OBJS) $(MBEDTLS_AOBJS) --start-group --whole-archive $(USER_LIBM) --no-whole-archive $(USER_LDLIBS) $(MONO_AOBJS) $(MONO_LIBM) --end-group $(USER_LIBGCC)


$(TOPDIR)$(DELIM)nuttx_user.elf: nuttx_user.elf
	@echo "LD: nuttx_user.elf"
	$(Q) cp -a nuttx_user.elf $(TOPDIR)$(DELIM)nuttx_user.elf
ifeq ($(CONFIG_INTELHEX_BINARY),y)
	@echo "CP: nuttx_user.hex"
	$(Q) $(OBJCOPY) $(OBJCOPYARGS) -O ihex nuttx_user.elf $(USER_HEXFILE)
endif
ifeq ($(CONFIG_MOTOROLA_SREC),y)
	@echo "CP: nuttx_user.srec"
	$(Q) $(OBJCOPY) $(OBJCOPYARGS) -O srec nuttx_user.elf $(USER_SRECFILE)
endif
ifeq ($(CONFIG_RAW_BINARY),y)
	@echo "CP: nuttx_user.bin"
	$(Q) $(OBJCOPY) $(OBJCOPYARGS) -O binary nuttx_user.elf $(USER_BINFILE)
endif

$(TOPDIR)$(DELIM)User.map: nuttx_user.elf
	@echo "MK: User.map"
	$(Q) $(NM) nuttx_user.elf >$(TOPDIR)$(DELIM)User.map
	$(Q) $(CROSSDEV)size nuttx_user.elf

.depend:

depend: .depend

clean:
	$(call DELFILE, nuttx_user.elf)
	$(call DELFILE, "$(TOPDIR)$(DELIM)nuttx_user.*")
	$(call DELFILE, "$(TOPDIR)$(DELIM)User.map")
	$(call CLEAN)

distclean: clean
