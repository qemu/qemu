/* 
   This is a little turbo C program that executes
   several int10, and let you inspect the content
   of the vgabios area

   It is used to test the behavior of the vgabios
*/

#include <stdio.h>
#include <dos.h>
#include <conio.h>


typedef unsigned char  Bit8u;
typedef unsigned short Bit16u;

typedef struct
{Bit8u initial;
 Bit8u current;
 Bit16u nbcols;
 Bit16u regen;
 Bit16u start;
 Bit16u curpos[8];
 Bit8u curtyp;
 Bit8u curpage;
 Bit16u crtc;
 Bit16u msr;
 Bit16u cgapal;
 Bit8u nbrows;
 Bit16u cheight;
 Bit8u ctl;
 Bit8u switches;
 Bit8u modeset;
 Bit8u dcc;
 Bit16u vsseg;
 Bit16u vsoffset;
} BIOSAREA;

void int10ax0003(struct REGPACK *regs)
{
 regs->r_ax=0x0003;
 intr(0x10,regs);
}

void int10ax02(struct REGPACK *regs)
{
 regs->r_ax=0x0200;
 regs->r_bx=0x0000;
 regs->r_dx=0x1710;
 intr(0x10,regs);
 printf("We are now at 24/17");
}

void int10ax03(struct REGPACK *regs)
{
 regs->r_ax=0x0300;
 regs->r_bx=0x0000;
 intr(0x10,regs);
 printf("\nCursor is ax%04x cx%04x dx%04x\n",regs->r_ax,regs->r_cx,regs->r_dx);
}

void int10ax0501(struct REGPACK *regs)
{
 regs->r_ax=0x0501;
 intr(0x10,regs);
 regs->r_ax=0x0e61;
 regs->r_bx=0x0000;
 intr(0x10,regs);
 printf("We are now on page 2");
}

void int10ax0602(struct REGPACK *regs)
{
 regs->r_ax=0x0602;
 regs->r_bx=0x0700;
 regs->r_cx=0x0101;
 regs->r_dx=0x0a0a;
 intr(0x10,regs);
 printf("Scrolled 2 up");
}

void int10ax0702(struct REGPACK *regs)
{
 regs->r_ax=0x0702;
 regs->r_bx=0x0700;
 regs->r_cx=0x0101;
 regs->r_dx=0x0a0a;
 intr(0x10,regs);
 printf("Scrolled 2 down");
}

void int10ax08(struct REGPACK *regs)
{
 regs->r_ax=0x0800;
 regs->r_bx=0x0000;
 intr(0x10,regs);
}

void int10ax09(struct REGPACK *regs)
{
 char attr;
 regs->r_ax=0x0501;
 intr(0x10,regs);
 for(attr=0;attr<16;attr++)
  {printf("%02x ",attr);
   regs->r_ax=0x0961+attr;
   regs->r_bx=0x0100+attr;
   regs->r_cx=0x0016;
   intr(0x10,regs);
   printf("\n");
  }
}

void int10ax0a(struct REGPACK *regs)
{
 regs->r_ax=0x0501;
 intr(0x10,regs);
 regs->r_ax=0x0a62;
 regs->r_bx=0x0101;
 regs->r_cx=0x0016;
 intr(0x10,regs);
}

void int10ax0f(struct REGPACK *regs)
{
 regs->r_ax=0x0501;
 intr(0x10,regs);
 regs->r_ax=0x0f00;
 intr(0x10,regs);
}

void int10ax1b(struct REGPACK *regs)
{unsigned char table[64];
 unsigned char far *ptable;
 int  i;

 regs->r_ax=0x0501;
 intr(0x10,regs);
 regs->r_ax=0x1b00;
 regs->r_bx=0x0000;
 ptable=&table;
 regs->r_es=FP_SEG(ptable);
 regs->r_di=FP_OFF(ptable);
 printf("Read state info in %04x:%04x\n",regs->r_es,regs->r_di);
 intr(0x10,regs);

 for(i=0;i<64;i++)
  {if(i%16==0)printf("\n%02x ",i);
   printf("%02x ",table[i]);
  }
 printf("\n");
}

static unsigned char var[64];

void int10ax13(struct REGPACK *regs)
{unsigned char far *pvar;

 pvar=&var;

 regs->r_ax=0x1300;
 regs->r_bx=0x000b;
 regs->r_dx=0x1010;
 regs->r_cx=0x0002;
 regs->r_es=FP_SEG(pvar);
 regs->r_bp=FP_OFF(pvar);
 pokeb(regs->r_es,regs->r_bp,'t');
 pokeb(regs->r_es,regs->r_bp+1,'b');
 printf("Writing from %04x:%04x\n",regs->r_es,regs->r_bp);
 intr(0x10,regs);
 
}

void switch_50(struct REGPACK *regs)
{
 regs->r_ax=0x1202;
 regs->r_bx=0x3000;
 intr(0x10,regs);
 regs->r_ax=0x0003;
 intr(0x10,regs);
 regs->r_ax=0x1112;
 regs->r_bx=0x0000;
 intr(0x10,regs);
}

char exec_function(struct REGPACK *regs)
{char c;

 printf("--- Functions --------------------\n");
 printf("a. int10 ax0003\t");
 printf("b. int10 ax02\t");
 printf("c. int10 ax03\t");
 printf("d. int10 ax0501\n");
 printf("e. int10 ax0602\t");
 printf("f. int10 ax0702\t");
 printf("g. int10 ax08\t");
 printf("h. int10 ax09\t");
 printf("i. int10 ax0a\n");
 printf("j. int10 ax0f\t");
 printf("k. int10 ax1b\t");
 printf("l. int10 ax13\n");
 printf("q. Quit\t");
 printf("r. switch to 50 lines\n");
 c=getche();
 
 switch(c)
  {case 'a':
    int10ax0003(regs);
    break;
   case 'b':
    int10ax02(regs);
    break;
   case 'c':
    int10ax03(regs);
    break;
   case 'd':
    int10ax0501(regs);
    break;
   case 'e':
    int10ax0602(regs);
    break;
   case 'f':
    int10ax0702(regs);
    break;
   case 'g':
    int10ax08(regs);
    break;
   case 'h':
    int10ax09(regs);
    break;
   case 'i':
    int10ax0a(regs);
    break;
   case 'j':
    int10ax0f(regs);
    break;
   case 'k':
    int10ax1b(regs);
    break;
   case 'l':
    int10ax13(regs);
    break;
   case 'q':
    break;
   case 'r':
    switch_50(regs);
    break;
   default:
    printf("No such function!\n");
  }

 if(c=='q')return 1;
 while(kbhit()==0);
 c=getch();
 
 return 0;
}

void read_bios_area(BIOSAREA *biosarea)
{
 biosarea->initial=peekb(0x40,0x10);
 biosarea->current=peekb(0x40,0x49);
 biosarea->nbcols=peek(0x40,0x4a);
 biosarea->regen=peek(0x40,0x4c);
 biosarea->start=peek(0x40,0x4e);
 biosarea->curpos[0]=peek(0x40,0x50);
 biosarea->curpos[1]=peek(0x40,0x52);
 biosarea->curpos[2]=peek(0x40,0x54);
 biosarea->curpos[3]=peek(0x40,0x56);
 biosarea->curpos[4]=peek(0x40,0x58);
 biosarea->curpos[5]=peek(0x40,0x5a);
 biosarea->curpos[6]=peek(0x40,0x5c);
 biosarea->curpos[7]=peek(0x40,0x5e);
 biosarea->curtyp=peek(0x40,0x60);
 biosarea->curpage=peekb(0x40,0x62);
 biosarea->crtc=peek(0x40,0x63);
 biosarea->msr=peekb(0x40,0x65);
 biosarea->cgapal=peekb(0x40,0x66);
 biosarea->nbrows=peekb(0x40,0x84);
 biosarea->cheight=peek(0x40,0x85);
 biosarea->ctl=peekb(0x40,0x87);
 biosarea->switches=peekb(0x40,0x88);
 biosarea->modeset=peekb(0x40,0x89);
 biosarea->dcc=peekb(0x40,0x8a);
 biosarea->vsseg=peek(0x40,0xa8);
 biosarea->vsoffset=peek(0x40,0xaa);
}

void show_bios_area(BIOSAREA *biosarea)
{
 printf("--- BIOS area --------------------\n");
 printf("initial : %02x\t",biosarea->initial);
 printf("current : %02x\t",biosarea->current);
 printf("nbcols  : %04x\t",biosarea->nbcols);
 printf("regen   : %04x\t",biosarea->regen);
 printf("start   : %04x\n",biosarea->start);
 printf("curpos  : %04x %04x %04x %04x %04x %04x %04x %04x\n",
   biosarea->curpos[0], biosarea->curpos[1], biosarea->curpos[2], biosarea->curpos[3],
   biosarea->curpos[4], biosarea->curpos[5], biosarea->curpos[6], biosarea->curpos[7]);
 printf("curtyp  : %04x\t",biosarea->curtyp);
 printf("curpage : %02x\t",biosarea->curpage);
 printf("crtc    : %04x\t",biosarea->crtc);
 printf("msr     : %04x\n",biosarea->msr);
 printf("cgapal  : %04x\t",biosarea->cgapal);
 printf("nbrows-1: %02x\t",biosarea->nbrows);
 printf("cheight : %04x\t",biosarea->cheight);
 printf("ctl     : %02x\n",biosarea->ctl);
 printf("switches: %02x\t",biosarea->switches);
 printf("modeset : %02x\t",biosarea->modeset);
 printf("dcc     : %02x\t",biosarea->dcc);
 printf("vs      : %04x:%04x\n",biosarea->vsseg,biosarea->vsoffset);
}

void show_regs(struct REGPACK *regs)
{
 printf("--- Registers --------------------\n");
 printf("ax %04x\t",regs->r_ax);
 printf("bx %04x\t",regs->r_bx);
 printf("cx %04x\t",regs->r_cx);
 printf("dx %04x\t",regs->r_dx);
 printf("ds %04x\t",regs->r_ds);
 printf("si %04x\t",regs->r_si);
 printf("es %04x\t",regs->r_es);
 printf("di %04x\n",regs->r_di);
}

void reset_videomode()
{
 struct REGPACK regs;

 regs.r_ax=0x0003;
 intr(0x10,&regs);
}

void main()
{

 BIOSAREA biosarea;
 struct REGPACK regs;

 directvideo=0;
 
 while(1)
  {
   read_bios_area(&biosarea);

   reset_videomode();
   show_bios_area(&biosarea);
   show_regs(&regs);

   if(exec_function(&regs)!=0)break;
  }
}
