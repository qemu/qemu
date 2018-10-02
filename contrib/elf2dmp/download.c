/*
 * Copyright (c) 2018 Virtuozzo International GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 *
 */

#include "qemu/osdep.h"
#include <curl/curl.h>
#include "download.h"

int download_url(const char *name, const char *url)
{
    int err = 0;
    FILE *file;
    CURL *curl = curl_easy_init();

    if (!curl) {
        return 1;
    }

    file = fopen(name, "wb");
    if (!file) {
        err = 1;
        goto out_curl;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);

    if (curl_easy_perform(curl) != CURLE_OK) {
        err = 1;
        fclose(file);
        unlink(name);
        goto out_curl;
    }

    err = fclose(file);

out_curl:
    curl_easy_cleanup(curl);

    return err;
}
