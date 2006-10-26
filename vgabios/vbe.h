#ifndef vbe_h_included
#define vbe_h_included

#include "vgabios.h"

// DISPI helper function
void dispi_set_enable(enable);

/** VBE int10 API
 *
 *  See the function descriptions in vbe.c for more information
 */
Boolean vbe_has_vbe_display();
void vbe_biosfn_return_controller_information(AX, ES, DI);
void vbe_biosfn_return_mode_information(AX, CX, ES, DI);
void vbe_biosfn_set_mode(AX, BX, ES, DI);
void vbe_biosfn_save_restore_state(AX, CX, DX, ES, BX);
void vbe_biosfn_set_get_palette_data(AX);
void vbe_biosfn_return_protected_mode_interface(AX);

// The official VBE Information Block
typedef struct VbeInfoBlock
{ 
   Bit8u  VbeSignature[4];
   Bit16u VbeVersion;
   Bit16u OemStringPtr_Off;
   Bit16u OemStringPtr_Seg;
   Bit8u  Capabilities[4];
   Bit16u VideoModePtr_Off;
   Bit16u VideoModePtr_Seg;
   Bit16u TotalMemory;
   Bit16u OemSoftwareRev;
   Bit16u OemVendorNamePtr_Off;
   Bit16u OemVendorNamePtr_Seg;
   Bit16u OemProductNamePtr_Off;
   Bit16u OemProductNamePtr_Seg;
   Bit16u OemProductRevPtr_Off;
   Bit16u OemProductRevPtr_Seg;
   Bit16u  Reserved[111]; // used for dynamicly generated mode list
   Bit8u  OemData[256];
} VbeInfoBlock;


// This one is for compactly storing a static list of mode info blocks
// this saves us 189 bytes per block
typedef struct ModeInfoBlockCompact
{
// Mandatory information for all VBE revisions
   Bit16u ModeAttributes;
   Bit8u  WinAAttributes;
   Bit8u  WinBAttributes;
   Bit16u WinGranularity;
   Bit16u WinSize;
   Bit16u WinASegment;
   Bit16u WinBSegment;
   Bit32u WinFuncPtr;
   Bit16u BytesPerScanLine;
// Mandatory information for VBE 1.2 and above
   Bit16u XResolution;
   Bit16u YResolution;
   Bit8u  XCharSize;
   Bit8u  YCharSize;
   Bit8u  NumberOfPlanes;
   Bit8u  BitsPerPixel;
   Bit8u  NumberOfBanks;
   Bit8u  MemoryModel;
   Bit8u  BankSize;
   Bit8u  NumberOfImagePages;
   Bit8u  Reserved_page;
// Direct Color fields (required for direct/6 and YUV/7 memory models)
   Bit8u  RedMaskSize;
   Bit8u  RedFieldPosition;
   Bit8u  GreenMaskSize;
   Bit8u  GreenFieldPosition;
   Bit8u  BlueMaskSize;
   Bit8u  BlueFieldPosition;
   Bit8u  RsvdMaskSize;
   Bit8u  RsvdFieldPosition;
   Bit8u  DirectColorModeInfo;
// Mandatory information for VBE 2.0 and above
   Bit32u PhysBasePtr;
   Bit32u OffScreenMemOffset;
   Bit16u OffScreenMemSize;
// Mandatory information for VBE 3.0 and above
   Bit16u LinBytesPerScanLine;
   Bit8u  BnkNumberOfPages;
   Bit8u  LinNumberOfPages;
   Bit8u  LinRedMaskSize;
   Bit8u  LinRedFieldPosition;
   Bit8u  LinGreenMaskSize;
   Bit8u  LinGreenFieldPosition;
   Bit8u  LinBlueMaskSize;
   Bit8u  LinBlueFieldPosition;
   Bit8u  LinRsvdMaskSize;
   Bit8u  LinRsvdFieldPosition;
   Bit32u MaxPixelClock;
//   Bit8u  Reserved[189]; // DO NOT PUT THIS IN HERE because of Compact Mode Info storage in bios 
} ModeInfoBlockCompact;

typedef struct ModeInfoBlock
{
// Mandatory information for all VBE revisions
   Bit16u ModeAttributes;
   Bit8u  WinAAttributes;
   Bit8u  WinBAttributes;
   Bit16u WinGranularity;
   Bit16u WinSize;
   Bit16u WinASegment;
   Bit16u WinBSegment;
   Bit32u WinFuncPtr;
   Bit16u BytesPerScanLine;
// Mandatory information for VBE 1.2 and above
   Bit16u XResolution;
   Bit16u YResolution;
   Bit8u  XCharSize;
   Bit8u  YCharSize;
   Bit8u  NumberOfPlanes;
   Bit8u  BitsPerPixel;
   Bit8u  NumberOfBanks;
   Bit8u  MemoryModel;
   Bit8u  BankSize;
   Bit8u  NumberOfImagePages;
   Bit8u  Reserved_page;
// Direct Color fields (required for direct/6 and YUV/7 memory models)
   Bit8u  RedMaskSize;
   Bit8u  RedFieldPosition;
   Bit8u  GreenMaskSize;
   Bit8u  GreenFieldPosition;
   Bit8u  BlueMaskSize;
   Bit8u  BlueFieldPosition;
   Bit8u  RsvdMaskSize;
   Bit8u  RsvdFieldPosition;
   Bit8u  DirectColorModeInfo;
// Mandatory information for VBE 2.0 and above
   Bit32u PhysBasePtr;
   Bit32u OffScreenMemOffset;
   Bit16u OffScreenMemSize;
// Mandatory information for VBE 3.0 and above
   Bit16u LinBytesPerScanLine;
   Bit8u  BnkNumberOfPages;
   Bit8u  LinNumberOfPages;
   Bit8u  LinRedMaskSize;
   Bit8u  LinRedFieldPosition;
   Bit8u  LinGreenMaskSize;
   Bit8u  LinGreenFieldPosition;
   Bit8u  LinBlueMaskSize;
   Bit8u  LinBlueFieldPosition;
   Bit8u  LinRsvdMaskSize;
   Bit8u  LinRsvdFieldPosition;
   Bit32u MaxPixelClock;
   Bit8u  Reserved[189];
} ModeInfoBlock;

typedef struct ModeInfoListItem
{
  Bit16u                mode;
  ModeInfoBlockCompact  info;
} ModeInfoListItem;

// VBE Return Status Info
// AL
#define VBE_RETURN_STATUS_SUPPORTED                      0x4F
#define VBE_RETURN_STATUS_UNSUPPORTED                    0x00
// AH
#define VBE_RETURN_STATUS_SUCCESSFULL                    0x00
#define VBE_RETURN_STATUS_FAILED                         0x01
#define VBE_RETURN_STATUS_NOT_SUPPORTED                  0x02
#define VBE_RETURN_STATUS_INVALID                        0x03

// VBE Mode Numbers

#define VBE_MODE_VESA_DEFINED                            0x0100
#define VBE_MODE_REFRESH_RATE_USE_CRTC                   0x0800
#define VBE_MODE_LINEAR_FRAME_BUFFER                     0x4000
#define VBE_MODE_PRESERVE_DISPLAY_MEMORY                 0x8000

// VBE GFX Mode Number

#define VBE_VESA_MODE_640X400X8                          0x100
#define VBE_VESA_MODE_640X480X8                          0x101
#define VBE_VESA_MODE_800X600X4                          0x102
#define VBE_VESA_MODE_800X600X8                          0x103
#define VBE_VESA_MODE_1024X768X4                         0x104
#define VBE_VESA_MODE_1024X768X8                         0x105
#define VBE_VESA_MODE_1280X1024X4                        0x106
#define VBE_VESA_MODE_1280X1024X8                        0x107
#define VBE_VESA_MODE_320X200X1555                       0x10D
#define VBE_VESA_MODE_320X200X565                        0x10E
#define VBE_VESA_MODE_320X200X888                        0x10F
#define VBE_VESA_MODE_640X480X1555                       0x110
#define VBE_VESA_MODE_640X480X565                        0x111
#define VBE_VESA_MODE_640X480X888                        0x112
#define VBE_VESA_MODE_800X600X1555                       0x113
#define VBE_VESA_MODE_800X600X565                        0x114
#define VBE_VESA_MODE_800X600X888                        0x115
#define VBE_VESA_MODE_1024X768X1555                      0x116
#define VBE_VESA_MODE_1024X768X565                       0x117
#define VBE_VESA_MODE_1024X768X888                       0x118
#define VBE_VESA_MODE_1280X1024X1555                     0x119
#define VBE_VESA_MODE_1280X1024X565                      0x11A
#define VBE_VESA_MODE_1280X1024X888                      0x11B
#define VBE_VESA_MODE_1600X1200X8                        0x11C
#define VBE_VESA_MODE_1600X1200X1555                     0x11D
#define VBE_VESA_MODE_1600X1200X565                      0x11E
#define VBE_VESA_MODE_1600X1200X888                      0x11F

// BOCHS/PLEX86 'own' mode numbers
#define VBE_OWN_MODE_320X200X8888                        0x140
#define VBE_OWN_MODE_640X400X8888                        0x141
#define VBE_OWN_MODE_640X480X8888                        0x142
#define VBE_OWN_MODE_800X600X8888                        0x143
#define VBE_OWN_MODE_1024X768X8888                       0x144
#define VBE_OWN_MODE_1280X1024X8888                      0x145
#define VBE_OWN_MODE_320X200X8                           0x146
#define VBE_OWN_MODE_1600X1200X8888                      0x147
#define VBE_OWN_MODE_1152X864X8                          0x148
#define VBE_OWN_MODE_1152X864X1555                       0x149
#define VBE_OWN_MODE_1152X864X565                        0x14a
#define VBE_OWN_MODE_1152X864X888                        0x14b
#define VBE_OWN_MODE_1152X864X8888                       0x14c

#define VBE_VESA_MODE_END_OF_LIST                        0xFFFF

// Capabilities

#define VBE_CAPABILITY_8BIT_DAC                          0x0001
#define VBE_CAPABILITY_NOT_VGA_COMPATIBLE                0x0002
#define VBE_CAPABILITY_RAMDAC_USE_BLANK_BIT              0x0004
#define VBE_CAPABILITY_STEREOSCOPIC_SUPPORT              0x0008
#define VBE_CAPABILITY_STEREO_VIA_VESA_EVC               0x0010

// Mode Attributes

#define VBE_MODE_ATTRIBUTE_SUPPORTED                     0x0001
#define VBE_MODE_ATTRIBUTE_EXTENDED_INFORMATION_AVAILABLE  0x0002
#define VBE_MODE_ATTRIBUTE_TTY_BIOS_SUPPORT              0x0004
#define VBE_MODE_ATTRIBUTE_COLOR_MODE                    0x0008
#define VBE_MODE_ATTRIBUTE_GRAPHICS_MODE                 0x0010
#define VBE_MODE_ATTRIBUTE_NOT_VGA_COMPATIBLE            0x0020
#define VBE_MODE_ATTRIBUTE_NO_VGA_COMPATIBLE_WINDOW      0x0040
#define VBE_MODE_ATTRIBUTE_LINEAR_FRAME_BUFFER_MODE      0x0080
#define VBE_MODE_ATTRIBUTE_DOUBLE_SCAN_MODE              0x0100
#define VBE_MODE_ATTRIBUTE_INTERLACE_MODE                0x0200
#define VBE_MODE_ATTRIBUTE_HARDWARE_TRIPLE_BUFFER        0x0400
#define VBE_MODE_ATTRIBUTE_HARDWARE_STEREOSCOPIC_DISPLAY 0x0800
#define VBE_MODE_ATTRIBUTE_DUAL_DISPLAY_START_ADDRESS    0x1000

#define VBE_MODE_ATTTRIBUTE_LFB_ONLY                     ( VBE_MODE_ATTRIBUTE_NO_VGA_COMPATIBLE_WINDOW | VBE_MODE_ATTRIBUTE_LINEAR_FRAME_BUFFER_MODE )

// Window attributes

#define VBE_WINDOW_ATTRIBUTE_RELOCATABLE                 0x01
#define VBE_WINDOW_ATTRIBUTE_READABLE                    0x02
#define VBE_WINDOW_ATTRIBUTE_WRITEABLE                   0x04

// Memory model

#define VBE_MEMORYMODEL_TEXT_MODE                        0x00
#define VBE_MEMORYMODEL_CGA_GRAPHICS                     0x01
#define VBE_MEMORYMODEL_HERCULES_GRAPHICS                0x02
#define VBE_MEMORYMODEL_PLANAR                           0x03
#define VBE_MEMORYMODEL_PACKED_PIXEL                     0x04
#define VBE_MEMORYMODEL_NON_CHAIN_4_256                  0x05
#define VBE_MEMORYMODEL_DIRECT_COLOR                     0x06
#define VBE_MEMORYMODEL_YUV                              0x07

// DirectColorModeInfo

#define VBE_DIRECTCOLOR_COLOR_RAMP_PROGRAMMABLE          0x01
#define VBE_DIRECTCOLOR_RESERVED_BITS_AVAILABLE          0x02

// GUEST <-> HOST Communication API

// FIXME: either dynamicly ask host for this or put somewhere high in physical memory
//        like 0xE0000000


  #define VBE_DISPI_BANK_ADDRESS          0xA0000
  #define VBE_DISPI_BANK_SIZE_KB          64
  
  #define VBE_DISPI_MAX_XRES              1024
  #define VBE_DISPI_MAX_YRES              768
  
  #define VBE_DISPI_IOPORT_INDEX          0x01CE
  #define VBE_DISPI_IOPORT_DATA           0x01CF
  
  #define VBE_DISPI_INDEX_ID              0x0
  #define VBE_DISPI_INDEX_XRES            0x1
  #define VBE_DISPI_INDEX_YRES            0x2
  #define VBE_DISPI_INDEX_BPP             0x3
  #define VBE_DISPI_INDEX_ENABLE          0x4
  #define VBE_DISPI_INDEX_BANK            0x5
  #define VBE_DISPI_INDEX_VIRT_WIDTH      0x6
  #define VBE_DISPI_INDEX_VIRT_HEIGHT     0x7
  #define VBE_DISPI_INDEX_X_OFFSET        0x8
  #define VBE_DISPI_INDEX_Y_OFFSET        0x9
      
  #define VBE_DISPI_ID0                   0xB0C0
  #define VBE_DISPI_ID1                   0xB0C1
  #define VBE_DISPI_ID2                   0xB0C2
  #define VBE_DISPI_ID3                   0xB0C3
  #define VBE_DISPI_ID4                   0xB0C4
  
  #define VBE_DISPI_DISABLED              0x00
  #define VBE_DISPI_ENABLED               0x01
  #define VBE_DISPI_GETCAPS               0x02
  #define VBE_DISPI_8BIT_DAC              0x20
  #define VBE_DISPI_LFB_ENABLED           0x40
  #define VBE_DISPI_NOCLEARMEM            0x80
  
  #define VBE_DISPI_LFB_PHYSICAL_ADDRESS  0xa0000000

#endif
