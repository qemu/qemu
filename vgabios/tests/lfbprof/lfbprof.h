/****************************************************************************
*
*                   VBE 2.0 Linear Framebuffer Profiler
*                    By Kendall Bennett and Brian Hook
*
* Filename:     LFBPROF.H
* Language:     ANSI C
* Environment:  Watcom C/C++ 10.0a with DOS4GW
*
* Description:  Header file for the LFBPROF.C progam.
*
****************************************************************************/

#ifndef __LFBPROF_H
#define __LFBPROF_H

/*---------------------- Macros and type definitions ----------------------*/

#pragma pack(1)

/* SuperVGA information block */

typedef struct {
    char    VESASignature[4];       /* 'VESA' 4 byte signature          */
    short   VESAVersion;            /* VBE version number               */
    long    OemStringPtr;           /* Pointer to OEM string            */
    long    Capabilities;           /* Capabilities of video card       */
    long    VideoModePtr;           /* Pointer to supported modes       */
    short   TotalMemory;            /* Number of 64kb memory blocks     */

    /* VBE 2.0 extensions */

    short   OemSoftwareRev;         /* OEM Software revision number     */
    long    OemVendorNamePtr;       /* Pointer to Vendor Name string    */
    long    OemProductNamePtr;      /* Pointer to Product Name string   */
    long    OemProductRevPtr;       /* Pointer to Product Revision str  */
    char    reserved[222];          /* Pad to 256 byte block size       */
    char    OemDATA[256];           /* Scratch pad for OEM data         */
    } VBE_vgaInfo;

/* SuperVGA mode information block */

typedef struct {
    short   ModeAttributes;         /* Mode attributes                  */
    char    WinAAttributes;         /* Window A attributes              */
    char    WinBAttributes;         /* Window B attributes              */
    short   WinGranularity;         /* Window granularity in k          */
    short   WinSize;                /* Window size in k                 */
    short   WinASegment;            /* Window A segment                 */
    short   WinBSegment;            /* Window B segment                 */
    long    WinFuncPtr;             /* Pointer to window function       */
    short   BytesPerScanLine;       /* Bytes per scanline               */
    short   XResolution;            /* Horizontal resolution            */
    short   YResolution;            /* Vertical resolution              */
    char    XCharSize;              /* Character cell width             */
    char    YCharSize;              /* Character cell height            */
    char    NumberOfPlanes;         /* Number of memory planes          */
    char    BitsPerPixel;           /* Bits per pixel                   */
    char    NumberOfBanks;          /* Number of CGA style banks        */
    char    MemoryModel;            /* Memory model type                */
    char    BankSize;               /* Size of CGA style banks          */
    char    NumberOfImagePages;     /* Number of images pages           */
    char    res1;                   /* Reserved                         */
    char    RedMaskSize;            /* Size of direct color red mask    */
    char    RedFieldPosition;       /* Bit posn of lsb of red mask      */
    char    GreenMaskSize;          /* Size of direct color green mask  */
    char    GreenFieldPosition;     /* Bit posn of lsb of green mask    */
    char    BlueMaskSize;           /* Size of direct color blue mask   */
    char    BlueFieldPosition;      /* Bit posn of lsb of blue mask     */
    char    RsvdMaskSize;           /* Size of direct color res mask    */
    char    RsvdFieldPosition;      /* Bit posn of lsb of res mask      */
    char    DirectColorModeInfo;    /* Direct color mode attributes     */

    /* VBE 2.0 extensions */

    long    PhysBasePtr;            /* Physical address for linear buf  */
    long    OffScreenMemOffset;     /* Pointer to start of offscreen mem*/
    short   OffScreenMemSize;       /* Amount of offscreen mem in 1K's  */
    char    res2[206];              /* Pad to 256 byte block size       */
    } VBE_modeInfo;

#define vbeMemPK        4           /* Packed Pixel memory model        */
#define vbeUseLFB       0x4000      /* Enable linear framebuffer mode   */

/* Flags for the mode attributes returned by VBE_getModeInfo. If
 * vbeMdNonBanked is set to 1 and vbeMdLinear is also set to 1, then only
 * the linear framebuffer mode is available.
 */

#define vbeMdAvailable  0x0001      /* Video mode is available          */
#define vbeMdColorMode  0x0008      /* Mode is a color video mode       */
#define vbeMdGraphMode  0x0010      /* Mode is a graphics mode          */
#define vbeMdNonBanked  0x0040      /* Banked mode is not supported     */
#define vbeMdLinear     0x0080      /* Linear mode supported            */

/* Structures for issuing real mode interrupts with DPMI */

struct _RMWORDREGS {
    unsigned short ax, bx, cx, dx, si, di, cflag;
    };

struct _RMBYTEREGS {
    unsigned char   al, ah, bl, bh, cl, ch, dl, dh;
    };

typedef union {
    struct  _RMWORDREGS x;
    struct  _RMBYTEREGS h;
    } RMREGS;

typedef struct {
    unsigned short  es;
    unsigned short  cs;
    unsigned short  ss;
    unsigned short  ds;
    } RMSREGS;

/* Inline assembler block fill/move routines */

void LfbMemset(void *p,int c,int n);
#pragma aux LfbMemset =             \
	"shr    ecx,2"                  \
	"xor    eax,eax"                \
	"mov    al,bl"                  \
	"shl    ebx,8"                  \
	"or     ax,bx"                  \
	"mov    ebx,eax"                \
	"shl    ebx,16"                 \
	"or     eax,ebx"                \
	"rep    stosd"                  \
	parm [edi] [ebx] [ecx];

void LfbMemcpy(void *dst,void *src,int n);
#pragma aux LfbMemcpy =             \
	"shr    ecx,2"                  \
	"rep    movsd"                  \
	parm [edi] [esi] [ecx];

/* Map a real mode pointer into address space */

#define LfbMapRealPointer(p)    (void*)(((unsigned)((p)  & 0xFFFF0000) >> 12) + ((p) & 0xFFFF))

/* Get the current timer tick count */

#define LfbGetTicks()       *((long*)0x46C)

#pragma pack()

#endif  /* __LFBPROF_H */
