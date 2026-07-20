/****************************************************************************
 * \apps\examples\hcom\tests\sotest_tests.c
 *
 *   Copyright (C) 2026 Wilderness Labs. All rights reserved.
 *   Author:  Wilderness Labs
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <nuttx/config.h>

#if defined(CONFIG_SOTEST_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)

#include <dlfcn.h>
#include <nuttx/symtab.h>

#include "../hcom_common.h"

/****************************************************************************
 * Symbols from Auto-Generated Code
 ****************************************************************************/

extern const struct symtab_s g_sot_exports[];
extern const int g_sot_nexports;

/****************************************************************************
 * Name: sotest_tests_open_module
 *
 * Description:
 *  Open one of the sotest loadable modules from CONFIG_EXAMPLES_SOTEST_BINDIR.
 *
 * Input Parameters:
 *  name - Name of the loadable module file to be opened
 *
 * Returned Value:
 *  Handle to the module if successful, NULL if the module cannot be opened.
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
static void *sotest_tests_open_module(const char *name)
{
    char path[128];

    snprintf_chk(path, sizeof(path), "%s/%s", CONFIG_EXAMPLES_SOTEST_BINDIR, name);
    syslog(LOG_MTEST, "    Opening %s\n", path);
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    syslog(LOG_MTEST, "    Handle: %p\n", handle);

    return(handle);
}

/****************************************************************************
 * Name: meadow_sotest_tests
 *
 * Description:
 *  Exercise NuttX's dlopen/dlsym/dlclose loadable module support by loading
 *  the upstream "modprint" and "sotest" example modules (apps/examples/sotest)
 *  and calling their exported test functions.
 *
 * Input Parameters:
 *  value - Developer value passed to CLI.
 *
 * Returned Value:
 *   None
 *
 * Assumptions/Limitations:
 *   The modprint and sotest loadable module files must already have been
 *   copied to CONFIG_EXAMPLES_SOTEST_BINDIR on the target file system.
 *
 ****************************************************************************/
void meadow_sotest_tests(uint32_t value)
{
    bool pass = true;
    int ret;
    void *handle1 = NULL;

    syslog(LOG_MTEST, "Executing sotest dlopen/dlsym test\n");

    ret = dlsymtab((struct symtab_s *)g_sot_exports, g_sot_nexports);
    if (ret < 0)
    {
        syslog(LOG_MTEST, "    dlsymtab failed: %d\n", ret);
        return;
    }

#if CONFIG_LIBC_ELF_MAXDEPEND > 0
    /* Install the first test module.  It only verifies that symbols
     * exported by one module can be used to resolve undefined symbols
     * in a second module.
     */

    handle1 = sotest_tests_open_module("modprint");
    if (handle1 == NULL)
    {
        syslog(LOG_MTEST, "    Cannot open modprint module\n");
        pass = false;
    }
#endif

    void *handle2 = sotest_tests_open_module("sotest");
    if (handle2 != NULL)
    {
        void (*testfunc)(const char *msg);
        const char *msg;

        testfunc = dlsym(handle2, "testfunc1");
        msg = dlsym(handle2, "g_msg1");
        if ((testfunc != NULL) && (msg != NULL))
        {
            testfunc(msg);
        }
        else
        {
            syslog(LOG_MTEST, "    Cannot locate testfunc1/g_msg1\n");
            pass = false;
        }

        testfunc = dlsym(handle2, "testfunc2");
        msg = dlsym(handle2, "g_msg2");
        if ((testfunc != NULL) && (msg != NULL))
        {
            testfunc(msg);
        }
        else
        {
            syslog(LOG_MTEST, "    Cannot locate testfunc2/g_msg2\n");
            pass = false;
        }

        testfunc = dlsym(handle2, "testfunc3");
        msg = dlsym(handle2, "g_msg3");
        if (testfunc != NULL && msg != NULL)
        {
            testfunc(msg);
        }
        else
        {
            syslog(LOG_MTEST, "    Cannot locate testfunc3/g_msg3\n");
            pass = false;
        }

#if CONFIG_LIBC_ELF_MAXDEPEND > 0
        /* sotest depends on modprint, so this close should fail while
         * handle2 (sotest) is still open.
         */

        if ((handle1 != NULL) && (dlclose(handle1) == 0))
        {
            syslog(LOG_MTEST, "    dlclose(modprint) unexpectedly succeeded with a dependency\n");
            pass = false;
        }
#endif

        if (dlclose(handle2) < 0)
        {
            syslog(LOG_MTEST, "    dlclose(sotest) failed\n");
            pass = false;
        }

#if CONFIG_LIBC_ELF_MAXDEPEND > 0
        if ((handle1 != NULL) && (dlclose(handle1) < 0))
        {
            syslog(LOG_MTEST, "    dlclose(modprint) failed\n");
            pass = false;
        }
#endif
    }
    else
    {
        syslog(LOG_MTEST, "    Cannot open sotest module\n");
        pass = false;
    }

    syslog(LOG_MTEST, "    sotest dlopen/dlsym test - %s.\n", pass ? "PASS" : "FAIL");
}

#endif // defined(CONFIG_SOTEST_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
