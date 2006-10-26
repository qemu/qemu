/****************************************************************************
*
*                   VBE 2.0 Linear Framebuffer Profiler
*                    By Kendall Bennett and Brian Hook
*
* Filename:     LFBPROF.C
* Language:     ANSI C
* Environment:  Watcom C/C++ 10.0a with DOS4GW
*
* Description:  Simple program to profile the speed of screen clearing
*               and full screen BitBlt operations using a VESA VBE 2.0
*               linear framebuffer from 32 bit protected mode.
*
*               For simplicity, this program only supports 256 color
*               SuperVGA video modes that support a linear framebuffer.
*
*
* 2002/02/18: Jeroen Janssen <japj at xs4all dot nl>
*               - fixed unsigned short for mode list (-1 != 0xffff otherwise)
*               - fixed LfbMapRealPointer macro mask problem (some modes were skipped)
*
****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include "lfbprof.h"

/*---------------------------- Global Variables ---------------------------*/

int     VESABuf_len = 1024;         /* Length of VESABuf                */
int     VESABuf_sel = 0;            /* Selector for VESABuf             */
int     VESABuf_rseg;               /* Real mode segment of VESABuf     */
unsigned short   modeList[50];      /* List of available VBE modes      */
float   clearsPerSec;               /* Number of clears per second      */
float   clearsMbPerSec;             /* Memory transfer for clears       */
float   bitBltsPerSec;              /* Number of BitBlt's per second    */
float   bitBltsMbPerSec;            /* Memory transfer for bitblt's     */
int     xres,yres;                  /* Video mode resolution            */
int     bytesperline;               /* Bytes per scanline for mode      */
long    imageSize;                  /* Length of the video image        */
char    *LFBPtr;                	/* Pointer to linear framebuffer    */

/*------------------------- DPMI interface routines -----------------------*/

void DPMI_allocRealSeg(int size,int *sel,int *r_seg)
/****************************************************************************
*
* Function:     DPMI_allocRealSeg
* Parameters:   size    - Size of memory block to allocate
*               sel     - Place to return protected mode selector
*               r_seg   - Place to return real mode segment
*
* Description:  Allocates a block of real mode memory using DPMI services.
*               This routine returns both a protected mode selector and
*               real mode segment for accessing the memory block.
*
****************************************************************************/
{
    union REGS      r;

    r.w.ax = 0x100;                 /* DPMI allocate DOS memory         */
    r.w.bx = (size + 0xF) >> 4;     /* number of paragraphs             */
    int386(0x31, &r, &r);
    if (r.w.cflag)
        FatalError("DPMI_allocRealSeg failed!");
    *sel = r.w.dx;                  /* Protected mode selector          */
    *r_seg = r.w.ax;                /* Real mode segment                */
}

void DPMI_freeRealSeg(unsigned sel)
/****************************************************************************
*
* Function:     DPMI_allocRealSeg
* Parameters:   sel - Protected mode selector of block to free
*
* Description:  Frees a block of real mode memory.
*
****************************************************************************/
{
    union REGS  r;

    r.w.ax = 0x101;                 /* DPMI free DOS memory             */
    r.w.dx = sel;                   /* DX := selector from 0x100        */
    int386(0x31, &r, &r);
}

typedef struct {
    long    edi;
    long    esi;
    long    ebp;
    long    reserved;
    long    ebx;
    long    edx;
    long    ecx;
    long    eax;
    short   flags;
    short   es,ds,fs,gs,ip,cs,sp,ss;
    } _RMREGS;

#define IN(reg)     rmregs.e##reg = in->x.reg
#define OUT(reg)    out->x.reg = rmregs.e##reg

int DPMI_int86(int intno, RMREGS *in, RMREGS *out)
/****************************************************************************
*
* Function:     DPMI_int86
* Parameters:   intno   - Interrupt number to issue
*               in      - Pointer to structure for input registers
*               out     - Pointer to structure for output registers
* Returns:      Value returned by interrupt in AX
*
* Description:  Issues a real mode interrupt using DPMI services.
*
****************************************************************************/
{
    _RMREGS         rmregs;
    union REGS      r;
    struct SREGS    sr;

    memset(&rmregs, 0, sizeof(rmregs));
    IN(ax); IN(bx); IN(cx); IN(dx); IN(si); IN(di);

    segread(&sr);
    r.w.ax = 0x300;                 /* DPMI issue real interrupt        */
    r.h.bl = intno;
    r.h.bh = 0;
    r.w.cx = 0;
    sr.es = sr.ds;
    r.x.edi = (unsigned)&rmregs;
    int386x(0x31, &r, &r, &sr);     /* Issue the interrupt              */

    OUT(ax); OUT(bx); OUT(cx); OUT(dx); OUT(si); OUT(di);
    out->x.cflag = rmregs.flags & 0x1;
    return out->x.ax;
}

int DPMI_int86x(int intno, RMREGS *in, RMREGS *out, RMSREGS *sregs)
/****************************************************************************
*
* Function:     DPMI_int86
* Parameters:   intno   - Interrupt number to issue
*               in      - Pointer to structure for input registers
*               out     - Pointer to structure for output registers
*               sregs   - Values to load into segment registers
* Returns:      Value returned by interrupt in AX
*
* Description:  Issues a real mode interrupt using DPMI services.
*
****************************************************************************/
{
    _RMREGS         rmregs;
    union REGS      r;
    struct SREGS    sr;

    memset(&rmregs, 0, sizeof(rmregs));
    IN(ax); IN(bx); IN(cx); IN(dx); IN(si); IN(di);
    rmregs.es = sregs->es;
    rmregs.ds = sregs->ds;

    segread(&sr);
    r.w.ax = 0x300;                 /* DPMI issue real interrupt        */
    r.h.bl = intno;
    r.h.bh = 0;
    r.w.cx = 0;
    sr.es = sr.ds;
    r.x.edi = (unsigned)&rmregs;
    int386x(0x31, &r, &r, &sr);     /* Issue the interrupt */

    OUT(ax); OUT(bx); OUT(cx); OUT(dx); OUT(si); OUT(di);
    sregs->es = rmregs.es;
    sregs->cs = rmregs.cs;
    sregs->ss = rmregs.ss;
    sregs->ds = rmregs.ds;
    out->x.cflag = rmregs.flags & 0x1;
    return out->x.ax;
}

int DPMI_allocSelector(void)
/****************************************************************************
*
* Function:     DPMI_allocSelector
* Returns:      Newly allocated protected mode selector
*
* Description:  Allocates a new protected mode selector using DPMI
*               services. This selector has a base address and limit of 0.
*
****************************************************************************/
{
    int         sel;
    union REGS  r;

    r.w.ax = 0;                     /* DPMI allocate selector           */
    r.w.cx = 1;                     /* Allocate a single selector       */
    int386(0x31, &r, &r);
    if (r.x.cflag)
        FatalError("DPMI_allocSelector() failed!");
    sel = r.w.ax;

    r.w.ax = 9;                     /* DPMI set access rights           */
    r.w.bx = sel;
    r.w.cx = 0x8092;                /* 32 bit page granular             */
    int386(0x31, &r, &r);
    return sel;
}

long DPMI_mapPhysicalToLinear(long physAddr,long limit)
/****************************************************************************
*
* Function:     DPMI_mapPhysicalToLinear
* Parameters:   physAddr    - Physical memory address to map
*               limit       - Length-1 of physical memory region to map
* Returns:      Starting linear address for mapped memory
*
* Description:  Maps a section of physical memory into the linear address
*               space of a process using DPMI calls. Note that this linear
*               address cannot be used directly, but must be used as the
*               base address for a selector.
*
****************************************************************************/
{
    union REGS  r;

    r.w.ax = 0x800;                 /* DPMI map physical to linear      */
    r.w.bx = physAddr >> 16;
    r.w.cx = physAddr & 0xFFFF;
    r.w.si = limit >> 16;
    r.w.di = limit & 0xFFFF;
    int386(0x31, &r, &r);
    if (r.x.cflag)
        FatalError("DPMI_mapPhysicalToLinear() failed!");
    return ((long)r.w.bx << 16) + r.w.cx;
}

void DPMI_setSelectorBase(int sel,long linAddr)
/****************************************************************************
*
* Function:     DPMI_setSelectorBase
* Parameters:   sel     - Selector to change base address for
*               linAddr - Linear address used for new base address
*
* Description:  Sets the base address for the specified selector.
*
****************************************************************************/
{
    union REGS  r;

    r.w.ax = 7;                     /* DPMI set selector base address   */
    r.w.bx = sel;
    r.w.cx = linAddr >> 16;
    r.w.dx = linAddr & 0xFFFF;
    int386(0x31, &r, &r);
    if (r.x.cflag)
        FatalError("DPMI_setSelectorBase() failed!");
}

void DPMI_setSelectorLimit(int sel,long limit)
/****************************************************************************
*
* Function:     DPMI_setSelectorLimit
* Parameters:   sel     - Selector to change limit for
*               limit   - Limit-1 for the selector
*
* Description:  Sets the memory limit for the specified selector.
*
****************************************************************************/
{
    union REGS  r;

    r.w.ax = 8;                     /* DPMI set selector limit          */
    r.w.bx = sel;
    r.w.cx = limit >> 16;
    r.w.dx = limit & 0xFFFF;
    int386(0x31, &r, &r);
    if (r.x.cflag)
        FatalError("DPMI_setSelectorLimit() failed!");
}

/*-------------------------- VBE Interface routines -----------------------*/

void FatalError(char *msg)
{
    fprintf(stderr,"%s\n", msg);
    exit(1);
}

static void ExitVBEBuf(void)
{
    DPMI_freeRealSeg(VESABuf_sel);
}

void VBE_initRMBuf(void)
/****************************************************************************
*
* Function:     VBE_initRMBuf
* Description:  Initialises the VBE transfer buffer in real mode memory.
*               This routine is called by the VESAVBE module every time
*               it needs to use the transfer buffer, so we simply allocate
*               it once and then return.
*
****************************************************************************/
{
    if (!VESABuf_sel) {
        DPMI_allocRealSeg(VESABuf_len, &VESABuf_sel, &VESABuf_rseg);
        atexit(ExitVBEBuf);
        }
}

void VBE_callESDI(RMREGS *regs, void *buffer, int size)
/****************************************************************************
*
* Function:     VBE_callESDI
* Parameters:   regs    - Registers to load when calling VBE
*               buffer  - Buffer to copy VBE info block to
*               size    - Size of buffer to fill
*
* Description:  Calls the VESA VBE and passes in a buffer for the VBE to
*               store information in, which is then copied into the users
*               buffer space. This works in protected mode as the buffer
*               passed to the VESA VBE is allocated in conventional
*               memory, and is then copied into the users memory block.
*
****************************************************************************/
{
    RMSREGS sregs;

    VBE_initRMBuf();
    sregs.es = VESABuf_rseg;
    regs->x.di = 0;
    _fmemcpy(MK_FP(VESABuf_sel,0),buffer,size);
    DPMI_int86x(0x10, regs, regs, &sregs);
    _fmemcpy(buffer,MK_FP(VESABuf_sel,0),size);
}

int VBE_detect(void)
/****************************************************************************
*
* Function:     VBE_detect
* Parameters:   vgaInfo - Place to store the VGA information block
* Returns:      VBE version number, or 0 if not detected.
*
* Description:  Detects if a VESA VBE is out there and functioning
*               correctly. If we detect a VBE interface we return the
*               VGAInfoBlock returned by the VBE and the VBE version number.
*
****************************************************************************/
{
    RMREGS      regs;
    unsigned    short    *p1,*p2;
    VBE_vgaInfo vgaInfo;

    /* Put 'VBE2' into the signature area so that the VBE 2.0 BIOS knows
     * that we have passed a 512 byte extended block to it, and wish
     * the extended information to be filled in.
     */
    strncpy(vgaInfo.VESASignature,"VBE2",4);

    /* Get the SuperVGA Information block */
    regs.x.ax = 0x4F00;
    VBE_callESDI(&regs, &vgaInfo, sizeof(VBE_vgaInfo));
    if (regs.x.ax != 0x004F)
        return 0;
    if (strncmp(vgaInfo.VESASignature,"VESA",4) != 0)
        return 0;

    /* Now that we have detected a VBE interface, copy the list of available
     * video modes into our local buffer. We *must* copy this mode list,
     * since the VBE will build the mode list in the VBE_vgaInfo buffer
     * that we have passed, so the next call to the VBE will trash the
     * list of modes.
     */
    printf("videomodeptr %x\n",vgaInfo.VideoModePtr);
    p1 = LfbMapRealPointer(vgaInfo.VideoModePtr);
    p2 = modeList;
    while (*p1 != -1)
    {
        printf("found mode %x\n",*p1);
        *p2++ = *p1++;
    }
    *p2 = -1;
    return vgaInfo.VESAVersion;
}

int VBE_getModeInfo(int mode,VBE_modeInfo *modeInfo)
/****************************************************************************
*
* Function:     VBE_getModeInfo
* Parameters:   mode        - VBE mode to get information for
*               modeInfo    - Place to store VBE mode information
* Returns:      1 on success, 0 if function failed.
*
* Description:  Obtains information about a specific video mode from the
*               VBE. You should use this function to find the video mode
*               you wish to set, as the new VBE 2.0 mode numbers may be
*               completely arbitrary.
*
****************************************************************************/
{
    RMREGS  regs;

    regs.x.ax = 0x4F01;             /* Get mode information         */
    regs.x.cx = mode;
    VBE_callESDI(&regs, modeInfo, sizeof(VBE_modeInfo));
    if (regs.x.ax != 0x004F)
        return 0;
    if ((modeInfo->ModeAttributes & vbeMdAvailable) == 0)
        return 0;
    return 1;
}

void VBE_setVideoMode(int mode)
/****************************************************************************
*
* Function:     VBE_setVideoMode
* Parameters:   mode    - VBE mode number to initialise
*
****************************************************************************/
{
    RMREGS  regs;
    regs.x.ax = 0x4F02;
    regs.x.bx = mode;
    DPMI_int86(0x10,&regs,&regs);
}

/*-------------------- Application specific routines ----------------------*/

void *GetPtrToLFB(long physAddr)
/****************************************************************************
*
* Function:     GetPtrToLFB
* Parameters:   physAddr    - Physical memory address of linear framebuffer
* Returns:      Far pointer to the linear framebuffer memory
*
****************************************************************************/
{
    int     sel;
    long    linAddr,limit = (4096 * 1024) - 1;

//	sel = DPMI_allocSelector();
	linAddr = DPMI_mapPhysicalToLinear(physAddr,limit);
//	DPMI_setSelectorBase(sel,linAddr);
//	DPMI_setSelectorLimit(sel,limit);
//	return MK_FP(sel,0);
	return (void*)linAddr;
}

void AvailableModes(void)
/****************************************************************************
*
* Function:     AvailableModes
*
* Description:  Display a list of available LFB mode resolutions.
*
****************************************************************************/
{
    unsigned short           *p;
    VBE_modeInfo    modeInfo;

    printf("Usage: LFBPROF <xres> <yres>\n\n");
    printf("Available 256 color video modes:\n");
    for (p = modeList; *p != -1; p++) {
        if (VBE_getModeInfo(*p, &modeInfo)) {
            /* Filter out only 8 bit linear framebuffer modes */
            if ((modeInfo.ModeAttributes & vbeMdLinear) == 0)
                continue;
            if (modeInfo.MemoryModel != vbeMemPK
                || modeInfo.BitsPerPixel != 8
                || modeInfo.NumberOfPlanes != 1)
                continue;
            printf("    %4d x %4d %d bits per pixel\n",
                modeInfo.XResolution, modeInfo.YResolution,
                modeInfo.BitsPerPixel);
            }
        }
    exit(1);
}

void InitGraphics(int x,int y)
/****************************************************************************
*
* Function:     InitGraphics
* Parameters:   x,y - Requested video mode resolution
*
* Description:  Initialise the specified video mode. We search through
*               the list of available video modes for one that matches
*               the resolution and color depth are are looking for.
*
****************************************************************************/
{
    unsigned short           *p;
    VBE_modeInfo    modeInfo;
    printf("InitGraphics\n");

    for (p = modeList; *p != -1; p++) {
        if (VBE_getModeInfo(*p, &modeInfo)) {
            /* Filter out only 8 bit linear framebuffer modes */
            if ((modeInfo.ModeAttributes & vbeMdLinear) == 0)
                continue;
            if (modeInfo.MemoryModel != vbeMemPK
                || modeInfo.BitsPerPixel != 8
                || modeInfo.NumberOfPlanes != 1)
                continue;
            if (modeInfo.XResolution != x || modeInfo.YResolution != y)
                continue;
            xres = x;
            yres = y;
            bytesperline = modeInfo.BytesPerScanLine;
            imageSize = bytesperline * yres;
            VBE_setVideoMode(*p | vbeUseLFB);
            LFBPtr = GetPtrToLFB(modeInfo.PhysBasePtr);
            return;
            }
        }
    printf("Valid video mode not found\n");
    exit(1);
}

void EndGraphics(void)
/****************************************************************************
*
* Function:     EndGraphics
*
* Description:  Restores text mode.
*
****************************************************************************/
{
    RMREGS  regs;
    printf("EndGraphics\n");
    regs.x.ax = 0x3;
    DPMI_int86(0x10, &regs, &regs);
}

void ProfileMode(void)
/****************************************************************************
*
* Function:     ProfileMode
*
* Description:  Profiles framebuffer performance for simple screen clearing
*               and for copying from system memory to video memory (BitBlt).
*               This routine thrashes the CPU cache by cycling through
*               enough system memory buffers to invalidate the entire
*               CPU external cache before re-using the first memory buffer
*               again.
*
****************************************************************************/
{
    int     i,numClears,numBlts,maxImages;
    long    startTicks,endTicks;
    void    *image[10],*dst;
    printf("ProfileMode\n");

    /* Profile screen clearing operation */
    startTicks = LfbGetTicks();
    numClears = 0;
    while ((LfbGetTicks() - startTicks) < 182)
		LfbMemset(LFBPtr,numClears++,imageSize);
	endTicks = LfbGetTicks();
	clearsPerSec = numClears / ((endTicks - startTicks) * 0.054925);
	clearsMbPerSec = (clearsPerSec * imageSize) / 1048576.0;

	/* Profile system memory to video memory copies */
	maxImages = ((512 * 1024U) / imageSize) + 2;
	for (i = 0; i < maxImages; i++) {
		image[i] = malloc(imageSize);
		if (image[i] == NULL)
			FatalError("Not enough memory to profile BitBlt!");
		memset(image[i],i+1,imageSize);
		}
	startTicks = LfbGetTicks();
	numBlts = 0;
	while ((LfbGetTicks() - startTicks) < 182)
		LfbMemcpy(LFBPtr,image[numBlts++ % maxImages],imageSize);
    endTicks = LfbGetTicks();
    bitBltsPerSec = numBlts / ((endTicks - startTicks) * 0.054925);
    bitBltsMbPerSec = (bitBltsPerSec * imageSize) / 1048576.0;
}

void main(int argc, char *argv[])
{
    if (VBE_detect() < 0x200)
        FatalError("This program requires VBE 2.0; Please install UniVBE 5.1.");
    if (argc != 3)
        AvailableModes();       /* Display available modes              */

    InitGraphics(atoi(argv[1]),atoi(argv[2]));  /* Start graphics       */
    ProfileMode();              /* Profile the video mode               */
    EndGraphics();              /* Restore text mode                    */

    printf("Profiling results for %dx%d 8 bits per pixel.\n",xres,yres);
    printf("%3.2f clears/s, %2.2f Mb/s\n", clearsPerSec, clearsMbPerSec);
    printf("%3.2f bitBlt/s, %2.2f Mb/s\n", bitBltsPerSec, bitBltsMbPerSec);
}
