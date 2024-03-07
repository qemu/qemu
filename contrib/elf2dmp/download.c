/*
 * Copyright (c) 2018 Virtuozzo International GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 *
 */

#include "qemu/osdep.h"
#include <curl/curl.h>
#include "download.h"

bool download_url(const char *name, const char *url)
{
    bool success = false;
    FILE *file;
    CURL *curl = curl_easy_init();

    if (!curl) {
        return false;
    }

    file = fopen(name, "wb");
    if (!file) {
        goto out_curl;
    }

    if (curl_easy_setopt(curl, CURLOPT_URL, url) != CURLE_OK
            || curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL) != CURLE_OK
            || curl_easy_setopt(curl, CURLOPT_WRITEDATA, file) != CURLE_OK
            || curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1) != CURLE_OK
            || curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0) != CURLE_OK
            || curl_easy_perform(curl) != CURLE_OK) {
        unlink(name);
        fclose(file);
    } else {
        success = !fclose(file);
    }

out_curl:
    curl_easy_cleanup(curl);

    return success;
}
