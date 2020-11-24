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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <glib.h>
#include "util/nano_utils.h"

#pragma pack(push) 
#pragma pack(1)    

typedef struct tagBITMAPFILEHEADER { /* bmfh */
	unsigned short bfType; 
	unsigned int  bfSize;
	unsigned short bfReserved1;
	unsigned short bfReserved2;
	unsigned int  bfOffBits;
} BITMAPFILEHEADER;

typedef struct tagBITMAPINFOHEADER { /* bmih */
	unsigned int  biSize;
	unsigned int  biWidth;
	unsigned int  biHeight;
	unsigned short biPlanes;
	unsigned short biBitCount;
	unsigned int  biCompression;
	unsigned int  biSizeImage;
	unsigned int  biXPelsPerMeter;
	unsigned int  biYPelsPerMeter;
	unsigned int  biClrUsed;
	unsigned int  biClrImportant;
} BITMAPINFOHEADER;

#pragma pack(pop) 

static int isBMPFormat(PT_FileMap ptFileMap);
static int GetPixelDatasFrmBMP(PT_FileMap ptFileMap, PT_PixelDatas ptPixelDatas);
static int FreePixelDatasForBMP(PT_PixelDatas ptPixelDatas);
static int CopyRegionPixelDatasFrmRGB(PT_PixelDatas ptReginPixelDatas, PT_PixelDatas ptPixelDatas, int x, int y, int width, int height);

static T_PicFileParser g_tBMPParser = {
	.name                 = "bmp",
	.isSupport            = isBMPFormat,
	.GetPixelDatas        = GetPixelDatasFrmBMP,
	.FreePixelDatas       = FreePixelDatasForBMP,	
	.CopyRegionPixelDatas = CopyRegionPixelDatasFrmRGB,
};


static int isBMPFormat(PT_FileMap ptFileMap)
{
    unsigned char *aFileHead = ptFileMap->pucFileMapMem;
    
	if (aFileHead[0] != 0x42 || aFileHead[1] != 0x4d)
		return 0;
	else
		return 1;
}


static int CovertOneLine(int iWidth, int iSrcBpp, int iDstBpp, unsigned char *pudSrcDatas, unsigned char *pudDstDatas)
{
	unsigned int dwRed;
	unsigned int dwGreen;
	unsigned int dwBlue;
	unsigned int dwColor;

	unsigned short *pwDstDatas16bpp = (unsigned short *)pudDstDatas;
	unsigned int   *pwDstDatas32bpp = (unsigned int *)pudDstDatas;

	int i;
	int pos = 0;

	if (iSrcBpp != 24)
	{
		return -1;
	}

	if (iDstBpp == 24)
	{
		memcpy(pudDstDatas, pudSrcDatas, iWidth*3);
	}
	else
	{
		for (i = 0; i < iWidth; i++)
		{
			dwBlue  = pudSrcDatas[pos++];
			dwGreen = pudSrcDatas[pos++];
			dwRed   = pudSrcDatas[pos++];
			if (iDstBpp == 32)
			{
				dwColor = (dwRed << 16) | (dwGreen << 8) | dwBlue;
				*pwDstDatas32bpp = dwColor;
				pwDstDatas32bpp++;
			}
			else if (iDstBpp == 16)
			{
				/* 565 */
				dwRed   = dwRed >> 3;
				dwGreen = dwGreen >> 2;
				dwBlue  = dwBlue >> 3;
				dwColor = (dwRed << 11) | (dwGreen << 5) | (dwBlue);
				*pwDstDatas16bpp = dwColor;
				pwDstDatas16bpp++;
			}
		}
	}
	return 0;
}


static int GetPixelDatasFrmBMP(PT_FileMap ptFileMap, PT_PixelDatas ptPixelDatas)
{
	BITMAPFILEHEADER *ptBITMAPFILEHEADER;
	BITMAPINFOHEADER *ptBITMAPINFOHEADER;

    unsigned char *aFileHead;

	int iWidth;
	int iHeight;
	int iBMPBpp;
	int y;

	unsigned char *pucSrc;
	unsigned char *pucDest;
	int iLineWidthAlign;
	int iLineWidthReal;

    aFileHead = ptFileMap->pucFileMapMem;

	ptBITMAPFILEHEADER = (BITMAPFILEHEADER *)aFileHead;
	ptBITMAPINFOHEADER = (BITMAPINFOHEADER *)(aFileHead + sizeof(BITMAPFILEHEADER));

	iWidth = ptBITMAPINFOHEADER->biWidth;
	iHeight = ptBITMAPINFOHEADER->biHeight;
	iBMPBpp = ptBITMAPINFOHEADER->biBitCount;

	if (iBMPBpp != 24)
	{
		DBG_PRINTF("iBMPBpp = %d\n", iBMPBpp);
		DBG_PRINTF("sizeof(BITMAPFILEHEADER) = %ld\n", sizeof(BITMAPFILEHEADER));
		return -1;
	}

	ptPixelDatas->iWidth  = iWidth;
	ptPixelDatas->iHeight = iHeight;
	//ptPixelDatas->iBpp    = iBpp;
	ptPixelDatas->iLineBytes    = iWidth * ptPixelDatas->iBpp / 8;
    ptPixelDatas->iTotalBytes   = ptPixelDatas->iHeight * ptPixelDatas->iLineBytes;
	ptPixelDatas->aucPixelDatas = malloc(ptPixelDatas->iTotalBytes);
	if (NULL == ptPixelDatas->aucPixelDatas)
	{
		return -1;
	}

	iLineWidthReal = iWidth * iBMPBpp / 8;
	iLineWidthAlign = (iLineWidthReal + 3) & ~0x3;   
		
	pucSrc = aFileHead + ptBITMAPFILEHEADER->bfOffBits;
	pucSrc = pucSrc + (iHeight - 1) * iLineWidthAlign;

	pucDest = ptPixelDatas->aucPixelDatas;
	
	for (y = 0; y < iHeight; y++)
	{		
		//memcpy(pucDest, pucSrc, iLineWidthReal);
		CovertOneLine(iWidth, iBMPBpp, ptPixelDatas->iBpp, pucSrc, pucDest);
		pucSrc  -= iLineWidthAlign;
		pucDest += ptPixelDatas->iLineBytes;
	}
	return 0;	
}


static int FreePixelDatasForBMP(PT_PixelDatas ptPixelDatas)
{
	free(ptPixelDatas->aucPixelDatas);
	return 0;
}

static int CopyRegionPixelDatasFrmRGB(PT_PixelDatas ptReginPixelDatas, PT_PixelDatas ptPixelDatas, int x, int y, int width, int height)
{
	unsigned char *src, *dest;
	
	ptReginPixelDatas->iWidth  = width;
	ptReginPixelDatas->iHeight = height;
	ptReginPixelDatas->iBpp    = ptPixelDatas->iBpp;
	ptReginPixelDatas->iLineBytes  = width * (ptReginPixelDatas->iBpp >> 3);
	ptReginPixelDatas->iTotalBytes = ptReginPixelDatas->iLineBytes * height;

	ptReginPixelDatas->aucPixelDatas = g_malloc0(ptReginPixelDatas->iTotalBytes);
	if (!ptReginPixelDatas->aucPixelDatas)
		return -1;

	src  = ptPixelDatas->aucPixelDatas + y*ptPixelDatas->iLineBytes + x*(ptPixelDatas->iBpp>>3);
	dest = ptReginPixelDatas->aucPixelDatas;
	while (height--)
	{
		memcpy(dest, src, ptReginPixelDatas->iLineBytes);
		src  += ptPixelDatas->iLineBytes;
		dest += ptReginPixelDatas->iLineBytes;
	}
	
	return 0;
}



PT_PicFileParser GetBMPParserInit(void)
{
	return &g_tBMPParser;
}


int MapFile(PT_FileMap ptFileMap)
{
	int iFd;
    	FILE *tFp;
	struct stat tStat;
	

	tFp = fopen(ptFileMap->strFileName, "r+");
	if (tFp == NULL)
	{
		DBG_PRINTF("can't open %s\n", ptFileMap->strFileName);
		return -1;
	}
	ptFileMap->tFp = tFp;
    	iFd = fileno(tFp);

	fstat(iFd, &tStat);
	ptFileMap->iFileSize = tStat.st_size;
	ptFileMap->pucFileMapMem = (unsigned char *)mmap(NULL , tStat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, iFd, 0);
	if (ptFileMap->pucFileMapMem == (unsigned char *)-1)
	{
		DBG_PRINTF("mmap error!\n");
		return -1;
	}
	return 0;
}


void UnMapFile(PT_FileMap ptFileMap)
{
	munmap(ptFileMap->pucFileMapMem, ptFileMap->iFileSize);
	fclose(ptFileMap->tFp);
}

 
static char cur_abs_dir[NANO_MAX_ABSOLUTE_PATH_LENGTH];

char *get_cur_app_abs_dir (void)
{
	int count;
	if (cur_abs_dir[0])
		return cur_abs_dir;
	count = readlink( "/proc/self/exe", cur_abs_dir, NANO_MAX_ABSOLUTE_PATH_LENGTH ); 
	if ( count < 0 || count >= NANO_MAX_ABSOLUTE_PATH_LENGTH )
	{ 
		cur_abs_dir[0]='.';
		cur_abs_dir[1]='/';
		return cur_abs_dir;
	}
	cur_abs_dir[count] = '\0'; 
	for (;count > 0; count--)
	{
		if (cur_abs_dir[count] != '/')
		{
			cur_abs_dir[count] = '\0';
		}
		else
		{
			return cur_abs_dir;			
		}
	}
	return cur_abs_dir;
}
