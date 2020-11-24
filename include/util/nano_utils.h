/*
 * Copyright (c) 2020 Nanosonics
 *
 * Nanosonics IMX6UL emulation utilities.
 *
 *
 * This code is licensed under the GPL, version 2 or later.
 * See the file `COPYING' in the top level directory.
 *
 * It (partially) emulates nanosonics platform with a Freescale
 * i.MX6ul SoC
 */

#ifndef _NANO_UTILS_H
#define _NANO_UTILS_H

#include <stdio.h>

#ifndef DEBUG_NANO_BOARD
#define DEBUG_NANO_BOARD 1
#endif

#define DBG_PRINTF(fmt, args...) \
    do { \
        if (DEBUG_NANO_BOARD) { \
            fprintf(stderr, "[%s]: " fmt, __func__, ##args); \
        } \
    } while (0)

#define BIT(nr) (1UL << (nr))

#define DPRINTF(module, enabled, fmt, args...) \
    do { \
        if (enabled) { \
            fprintf(stdout, "[%s]%s: " fmt , module, __func__, ##args); \
        } \
    } while (0)

typedef struct FileMap {
	char strFileName[256];   
	// int iFd; 
	FILE * tFp;             
	int iFileSize;           
	unsigned char *pucFileMapMem;  
}T_FileMap, *PT_FileMap;


typedef enum {
	FILETYPE_DIR = 0, 
	FILETYPE_FILE,    
}E_FileType;


typedef struct DirContent {
	char strName[256];
	E_FileType eFileType; 
}T_DirContent, *PT_DirContent;

int MapFile(PT_FileMap ptFileMap);

void UnMapFile(PT_FileMap ptFileMap);

int GetDirContents(char *strDirName, PT_DirContent **pptDirContents, int *piNumber);

void FreeDirContents(PT_DirContent *aptDirContents, int iNumber);

int GetFilesIndir(char *strDirName, int *piStartNumberToRecord, int *piCurFileNumber, int *piFileCountHaveGet, int iFileCountTotal, char apstrFileNames[][256]);


typedef struct PixelDatas {
	int iWidth;  
	int iHeight; 
	int iBpp;    
	int iLineBytes;
	int iTotalBytes;
	unsigned char *aucPixelDatas; 
}T_PixelDatas, *PT_PixelDatas;


typedef struct PicFileParser {
	const char *name;             
	int (*isSupport)(PT_FileMap ptFileMap);
	int (*GetPixelDatas)(PT_FileMap ptFileMap, PT_PixelDatas ptPixelDatas); 
	int (*CopyRegionPixelDatas)(PT_PixelDatas ptReginPixelDatas, PT_PixelDatas ptPixelDatas, int x, int y, int width, int height);
	int (*FreePixelDatas)(PT_PixelDatas ptPixelDatas); 
    struct PicFileParser *ptNext;  
}T_PicFileParser, *PT_PicFileParser;



PT_PicFileParser GetBMPParserInit(void);

#define NANO_MAX_ABSOLUTE_PATH_LENGTH	1024

/**
 * get_cur_app_abs_dir: Get absolute path of current directory of the executable
 * @returns: Returns the absolute path of the current directory
 *
 */
char * get_cur_app_abs_dir (void);


#endif /* _NANO_UTILS_H */
