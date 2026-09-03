/****************************************************************************
 * /Meadow.OS/misc/meadow_client_cert.c
 *
 *   Copyright (C) 2024 Wilderness Labs. All rights reserved.
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

#include <nuttx/config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include "espcp/espcp_file_system.h"
#include <meadow/meadow_client_cert.h>
#include <meadow/meadow_apps_core_share.h>

/****************************************************************************
 * Debug-only toggle: MEADOW_CLIENT_CERT_LOG_SECRET_CONTENT
 *
 * When defined, restores full-content logging of the client certificate,
 * private key, and passphrase to syslog -- the original behavior, before it
 * was found that the private key and passphrase were being logged in
 * plaintext and actively streamed off-device over UART by the always-on
 * diagnostic thread (hcom_nx_trace_msg_proc.c), not just written to a local
 * log (see Issue #401). This must NEVER be enabled outside of local,
 * temporary debugging. The #warning below is deliberate: this project
 * builds with -Werror, so leaving this defined turns into a hard build
 * failure instead of a silent leak.
 ****************************************************************************/
// #define MEADOW_CLIENT_CERT_LOG_SECRET_CONTENT

#ifdef MEADOW_CLIENT_CERT_LOG_SECRET_CONTENT
#warning "MEADOW_CLIENT_CERT_LOG_SECRET_CONTENT is enabled -- private key and passphrase will be logged in plaintext and streamed off-device. Local debugging only; must never be committed or shipped."
#endif

static int client_cert_buf_size = 0;
static int private_key_buf_size = 0;
static int private_key_pass_buf_size = 0;

/****************************************************************************
 * Name: meadow_client_cert_initialize
 *
 * Description:
 *  This function is responsible for loading the client certificate, client
 * private key, and the private key passphrase. It stores 
 * these values into ESP32, adding an extra security layer for the credentials.
 * After processing, the client credentials files are removed from the STM.
 * 
 * Input Parameters:
 *  None.
 *
 * Returned Value:
 *  Zero, if the function succeeds, otherwise returns a specific NuttX 
 * error code corresponding to the encountered issue.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
int meadow_client_cert_initialize(void)
{
    // Loading client certificate
    syslog(LOG_INFO, "Checking for a client certificate in STM storage.\n");
    int ret;
    char *client_cert = NULL;
    char *private_key = NULL;
    char *private_key_pass = NULL;
    long client_cert_size = 0;
    long private_key_size = 0;
    long private_key_pass_size = 0;

    FILE *client_cert_file = fopen(CLIENT_CERT_FILE_PATH, "r");
    if (client_cert_file == NULL)
    {
        syslog(LOG_INFO, "Client certificate not found. Failed to open file '%s'.\n", CLIENT_CERT_FILE_PATH);
    }
    else
    {
        syslog(LOG_INFO, "Client certificate file found.\n");
        // Add a null-terminator character at the end of the file
        //  since it's required by mbedTLS
        fseek(client_cert_file, 0, SEEK_END);
        client_cert_size = ftell(client_cert_file);
        if (client_cert_size < 0)
        {
            syslog(LOG_ERR, "Failed to determine client certificate file size.\n");
            fclose(client_cert_file);
            return -EIO;
        }
        rewind(client_cert_file);

        client_cert = (char *)malloc(client_cert_size + 1);
        if (client_cert == NULL)
        {
            syslog(LOG_INFO, "Memory allocation failed for client_cert.\n");
            fclose(client_cert_file);
            return -ENOMEM;
        }

        size_t client_cert_len = fread(client_cert, 1, client_cert_size, client_cert_file);
        client_cert[client_cert_len] = '\0';
        fclose(client_cert_file);

        // Storing client certificate
#ifdef MEADOW_CLIENT_CERT_LOG_SECRET_CONTENT
        syslog(LOG_INFO, "Storing client certificate length: %d\nContent: %s\n", client_cert_len, client_cert);
#else
        syslog(LOG_INFO, "Storing client certificate length: %d\n", client_cert_len);
#endif
        ret = espcp_file_system_write_file(CLIENT_CERT_FILE, (uint8_t *) client_cert, client_cert_len + 1);
        if (ret < 0)
        {
            syslog(LOG_ERR, "Failed to store client cert private key.\n");
            memset(client_cert, 0, client_cert_size + 1);
            free(client_cert);
            return ret;
        }

        // Remove file from STM storage
        remove(CLIENT_CERT_FILE_PATH);
    }

    // Loading client private key
    syslog(LOG_INFO, "Checking for a client cert private key in STM storage.\n");

    FILE *private_key_file = fopen(CLIENT_CERT_PRIVATE_KEY_FILE_PATH, "r");
    if (private_key_file == NULL)
    {
        syslog(LOG_INFO, "Client cert private key not found. Failed to open file '%s'.\n", CLIENT_CERT_PRIVATE_KEY_FILE_PATH);
    }
    else
    {
        syslog(LOG_INFO, "Client certificate private key file found.\n");
        // Add a null-terminator character at the end of the file
        //  since it's required by mbedTLS
        fseek(private_key_file, 0, SEEK_END);
        private_key_size = ftell(private_key_file);
        if (private_key_size < 0)
        {
            syslog(LOG_ERR, "Failed to determine private key file size.\n");
            fclose(private_key_file);
            if (client_cert != NULL)
            {
                memset(client_cert, 0, client_cert_size + 1);
                free(client_cert);
            }
            return -EIO;
        }
        rewind(private_key_file);

        private_key = (char *)malloc(private_key_size + 1);
        if (private_key == NULL)
        {
            syslog(LOG_ERR, "Memory allocation failed for private_key.\n");
            fclose(private_key_file);
            if (client_cert != NULL)
            {
                memset(client_cert, 0, client_cert_size + 1);
                free(client_cert);
            }
            return -ENOMEM;
        }

        size_t private_key_len = fread(private_key, 1, private_key_size, private_key_file);
        private_key[private_key_len] = '\0';
        fclose(private_key_file);

        // Storing client certificate private key
#ifdef MEADOW_CLIENT_CERT_LOG_SECRET_CONTENT
        syslog(LOG_INFO, "Storing private key length: %d\nContent: %s\n", private_key_len, private_key);
#else
        syslog(LOG_INFO, "Storing private key length: %d\n", private_key_len);
#endif
        ret = espcp_file_system_write_file(CLIENT_CERT_PRIVATE_KEY_FILE, (uint8_t *) private_key, private_key_len + 1);
        if (ret < 0)
        {
            syslog(LOG_ERR, "Failed to store client cert private key.\n");
            if (client_cert != NULL)
            {
                memset(client_cert, 0, client_cert_size + 1);
                free(client_cert);
            }
            memset(private_key, 0, private_key_size + 1);
            free(private_key);
            return ret;
        }

        // Remove file from STM storage
        remove(CLIENT_CERT_PRIVATE_KEY_FILE_PATH);
    }

    // Loading client private key passphrase
    syslog(LOG_INFO, "Checking for a client cert private key passphrase in STM storage.\n");

    size_t private_key_pass_len = 0;
    FILE *private_key_pass_file = fopen(CLIENT_CERT_PRIVATE_KEY_PASS_FILE_PATH, "r");
    if (private_key_pass_file == NULL)
    {
        syslog(LOG_INFO, "Client cert private key pass not found. Failed to open file '%s'.\n", CLIENT_CERT_PRIVATE_KEY_PASS_FILE_PATH);
    }
    else
    {
        syslog(LOG_INFO, "Client certificate private key passphrase file found.\n");
        // Add a null-terminator character at the end of the file
        //  since it's required by mbedTLS
        fseek(private_key_pass_file, 0, SEEK_END);
        private_key_pass_size = ftell(private_key_pass_file);
        if (private_key_pass_size < 0)
        {
            syslog(LOG_ERR, "Failed to determine private key passphrase file size.\n");
            fclose(private_key_pass_file);
            if (client_cert != NULL)
            {
                memset(client_cert, 0, client_cert_size + 1);
                free(client_cert);
            }
            if (private_key != NULL)
            {
                memset(private_key, 0, private_key_size + 1);
                free(private_key);
            }
            return -EIO;
        }
        rewind(private_key_pass_file);

        private_key_pass = (char *)malloc(private_key_pass_size + 1);
        if (private_key_pass == NULL)
        {
            syslog(LOG_ERR, "Memory allocation failed for private_key_pass.\n");
            fclose(private_key_pass_file);
            if (client_cert != NULL)
            {
                memset(client_cert, 0, client_cert_size + 1);
                free(client_cert);
            }
            if (private_key != NULL)
            {
                memset(private_key, 0, private_key_size + 1);
                free(private_key);
            }
            return -ENOMEM;
        }

        private_key_pass_len = fread(private_key_pass, 1, private_key_pass_size, private_key_pass_file);
        private_key_pass[private_key_pass_len] = '\0';
        fclose(private_key_pass_file);

        // Storing client certificate private key passphrase
#ifdef MEADOW_CLIENT_CERT_LOG_SECRET_CONTENT
        syslog(LOG_INFO, "Storing private key passphrase length: %d\nContent: %s\n", private_key_pass_len, private_key_pass);
#else
        syslog(LOG_INFO, "Storing private key passphrase length: %d\n", private_key_pass_len);
#endif
        ret = espcp_file_system_write_file(CLIENT_CERT_PRIVATE_KEY_PASS_FILE, (uint8_t *) private_key_pass, private_key_pass_len + 1);
        if (ret < 0)
        {
            syslog(LOG_ERR, "Failed to store client cert private key passphrase.\n");
            if (client_cert != NULL)
            {
                memset(client_cert, 0, client_cert_size + 1);
                free(client_cert);
            }
            if (private_key != NULL)
            {
                memset(private_key, 0, private_key_size + 1);
                free(private_key);
            }
            memset(private_key_pass, 0, private_key_pass_size + 1);
            free(private_key_pass);
            return ret;
        }

        // Remove file from STM storage
        remove(CLIENT_CERT_PRIVATE_KEY_PASS_FILE_PATH);
    }

    if (client_cert != NULL)
    {
        memset(client_cert, 0, client_cert_size + 1);
        free(client_cert);
    }
    if (private_key != NULL)
    {
        memset(private_key, 0, private_key_size + 1);
        free(private_key);
    }
    if (private_key_pass != NULL)
    {
        memset(private_key_pass, 0, private_key_pass_size + 1);
        free(private_key_pass);
    }

    return OK;
}

/****************************************************************************
 * Name: meadow_client_cert_retrieve_credentials
 *
 * Description:
 *  Reads the client certificate, private key, and private key passphrase
 * back from ESP32 storage, allocating a buffer for each and returning it
 * via the corresponding output pointer.
 *
 *  Currently unused: no caller exists yet anywhere in the repo. It's
 * kept, documented, alongside meadow_client_cert_initialize() (which writes
 * these same three values to ESP32 storage) as the read-side counterpart,
 * reserved for a future credential-retrieval use case (e.g. cert rotation)
 * once one is needed. Call meadow_client_cert_release_credentials() to free
 * the buffers this function allocates.
 *
 * Input Parameters:
 *  client_cert_buf_ptr      - Set to the allocated client certificate buffer.
 *  client_cert_len          - Set to the length of that buffer.
 *  private_key_buf_ptr      - Set to the allocated private key buffer.
 *  private_key_len          - Set to the length of that buffer.
 *  private_key_pass_buf_ptr - Set to the allocated passphrase buffer.
 *  private_key_pass_len     - Set to the length of that buffer.
 *
 * Returned Value:
 *  Zero, if the function succeeds, otherwise a negative error code.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
int meadow_client_cert_retrieve_credentials(const uint8_t **client_cert_buf_ptr,
                    int *client_cert_len, const uint8_t **private_key_buf_ptr,
                    int *private_key_len, const uint8_t **private_key_pass_buf_ptr,
                    int *private_key_pass_len)
{
    syslog(LOG_INFO, "Retrieving client cert credentials from ESP32...\n");

    int16_t client_cert_length;
    const uint8_t *client_cert_buf = (const uint8_t *) espcp_file_system_read_file(CLIENT_CERT_FILE, &client_cert_length);
    if (client_cert_buf == NULL)
    {
        syslog(LOG_ERR, "Fail to retrieve client cert\n");
        return -1;
    }

#ifdef MEADOW_CLIENT_CERT_LOG_SECRET_CONTENT
    syslog(LOG_INFO, "Retrieved client cert: %s len: %d\n", client_cert_buf, client_cert_length);
#else
    syslog(LOG_INFO, "Retrieved client cert len: %d\n", client_cert_length);
#endif
    *client_cert_buf_ptr = client_cert_buf;
    client_cert_buf_size = client_cert_length;
    *client_cert_len = client_cert_length;

    syslog(LOG_INFO, "Retrieving client cert private key from ESP32...\n");

    int16_t private_key_length;
    const uint8_t *private_key_buf = (const uint8_t *) espcp_file_system_read_file(CLIENT_CERT_PRIVATE_KEY_FILE, &private_key_length);
    if (private_key_buf == NULL)
    {
        syslog(LOG_ERR, "Fail to retrieve client cert private key\n");
        return -1;
    }

#ifdef MEADOW_CLIENT_CERT_LOG_SECRET_CONTENT
    syslog(LOG_INFO, "Retrieved client cert private key: %s len: %d\n", private_key_buf, private_key_length);
#else
    syslog(LOG_INFO, "Retrieved client cert private key len: %d\n", private_key_length);
#endif
    *private_key_buf_ptr = private_key_buf;
    private_key_buf_size = private_key_length;
    *private_key_len = private_key_length;

    syslog(LOG_INFO,"Retrieving client cert private key pass from ESP32...\n");

    int16_t private_key_pass_length;
    const uint8_t *private_key_pass_buf = (const uint8_t *) espcp_file_system_read_file(CLIENT_CERT_PRIVATE_KEY_PASS_FILE, &private_key_pass_length);
    if (private_key_pass_buf == NULL)
    {
        syslog(LOG_ERR, "Fail to retrieve client cert private key passphrase\n");
        return -1;
    }

#ifdef MEADOW_CLIENT_CERT_LOG_SECRET_CONTENT
    syslog(LOG_INFO, "Retrieved client cert private key pass: %s len: %d\n", private_key_pass_buf, private_key_pass_length);
#else
    syslog(LOG_INFO, "Retrieved client cert private key pass len: %d\n", private_key_pass_length);
#endif
    *private_key_pass_buf_ptr = private_key_pass_buf;
    private_key_pass_buf_size = private_key_pass_length;
    *private_key_pass_len = private_key_pass_length;

    return OK;
}

/****************************************************************************
 * Name: meadow_client_cert_release_credentials
 *
 * Description:
 *  Zeroes and frees the three buffers allocated by
 * meadow_client_cert_retrieve_credentials(), then sets each pointer to
 * NULL. Safe to call with any subset of the pointers already NULL.
 *
 *  Currently unused, since meadow_client_cert_retrieve_credentials() has
 * no caller yet -- see that function's doc comment.
 *
 * Input Parameters:
 *  client_cert_buf_ptr      - Pointer to the client certificate buffer.
 *  private_key_buf_ptr      - Pointer to the private key buffer.
 *  private_key_pass_buf_ptr - Pointer to the passphrase buffer.
 *
 * Returned Value:
 *  Zero (always succeeds).
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
int meadow_client_cert_release_credentials(char ** const client_cert_buf_ptr,
    char ** const private_key_buf_ptr,
    char ** const private_key_pass_buf_ptr)
{
    if (*client_cert_buf_ptr != NULL)
    {
        memset(*client_cert_buf_ptr, 0, client_cert_buf_size);
        free(*client_cert_buf_ptr);
        *client_cert_buf_ptr = NULL;
    }

    if (*private_key_buf_ptr != NULL)
    {
        memset(*private_key_buf_ptr, 0, private_key_buf_size);
        free(*private_key_buf_ptr);
        *private_key_buf_ptr = NULL;
    }

    if (*private_key_pass_buf_ptr != NULL)
    {
        memset(*private_key_pass_buf_ptr, 0, private_key_pass_buf_size);
        free(*private_key_pass_buf_ptr);
        *private_key_pass_buf_ptr = NULL;
    }

    return OK;
}
