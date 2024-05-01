/*
 * Header file for commands-windows-ssh.c
 *
 * Copyright Schweitzer Engineering Laboratories. 2024
 *
 * Authors:
 *  Aidan Leuck <aidan_leuck@selinc.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib/gstrfuncs.h>
#include <stdbool.h>
typedef struct WindowsUserInfo {
    char *sshDirectory;
    char *authorizedKeyFile;
    char *username;
    char *SSID;
    bool isAdmin;
} WindowsUserInfo;

typedef WindowsUserInfo *PWindowsUserInfo;

void free_userInfo(PWindowsUserInfo info);
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(PWindowsUserInfo, free_userInfo, NULL);
