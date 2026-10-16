############################################################################
# apps/system/mono/Makefile
#
#   Copyright (C) 2026 Wilderness Labs. All rights reserved.
#
#   Host code for the Mono .NET runtime.  This directory builds no program
#   of its own: meadow_mono_main() is started by hcom (mono/hcom_mono_control.c)
#   as the "Mono" task.
#
############################################################################

include $(APPDIR)/Make.defs

# The System.Native PAL headers come from the runtime checkout, the same one
# build-mono.sh builds the archives from.

MEADOW_RUNTIME_DIR ?= $(APPDIR)/../../runtime
RUNTIME_NATIVE_DIR = $(MEADOW_RUNTIME_DIR)/src/native

CFLAGS += ${INCDIR_PREFIX}$(RUNTIME_NATIVE_DIR)/libs/System.Native
CFLAGS += ${INCDIR_PREFIX}$(RUNTIME_NATIVE_DIR)/libs/Common
CFLAGS += ${INCDIR_PREFIX}$(RUNTIME_NATIVE_DIR)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/database/sqlite
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/database/sqlite/build
CFLAGS += -DTARGET_UNIX=1
CFLAGS += -Werror

CSRCS = mono_main.c mono_nuttx_support.c mono_mbedtls.c pal_ssl_mbedtls.c

include $(APPDIR)/Application.mk
