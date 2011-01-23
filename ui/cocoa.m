/*
 * QEMU Cocoa CG display driver
 *
 * Copyright (c) 2008 Mike Kronenberg
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#import <Cocoa/Cocoa.h>

#include "qemu-common.h"
#include "console.h"
#include "sysemu.h"

#ifndef MAC_OS_X_VERSION_10_4
#define MAC_OS_X_VERSION_10_4 1040
#endif
#ifndef MAC_OS_X_VERSION_10_5
#define MAC_OS_X_VERSION_10_5 1050
#endif


//#define DEBUG

#ifdef DEBUG
#define COCOA_DEBUG(...)  { (void) fprintf (stdout, __VA_ARGS__); }
#else
#define COCOA_DEBUG(...)  ((void) 0)
#endif

#define cgrect(nsrect) (*(CGRect *)&(nsrect))
#define COCOA_MOUSE_EVENT \
        if (isTabletEnabled) { \
            kbd_mouse_event((int)(p.x * 0x7FFF / (screen.width - 1)), (int)((screen.height - p.y) * 0x7FFF / (screen.height - 1)), 0, buttons); \
        } else if (isMouseGrabed) { \
            kbd_mouse_event((int)[event deltaX], (int)[event deltaY], 0, buttons); \
        } else { \
            [NSApp sendEvent:event]; \
        }

typedef struct {
    int width;
    int height;
    int bitsPerComponent;
    int bitsPerPixel;
} QEMUScreen;

int qemu_main(int argc, char **argv); // main defined in qemu/vl.c
NSWindow *normalWindow;
id cocoaView;
static DisplayChangeListener *dcl;

int gArgc;
char **gArgv;

// keymap conversion
int keymap[] =
{
//  SdlI    macI    macH    SdlH    104xtH  104xtC  sdl
    30, //  0       0x00    0x1e            A       QZ_a
    31, //  1       0x01    0x1f            S       QZ_s
    32, //  2       0x02    0x20            D       QZ_d
    33, //  3       0x03    0x21            F       QZ_f
    35, //  4       0x04    0x23            H       QZ_h
    34, //  5       0x05    0x22            G       QZ_g
    44, //  6       0x06    0x2c            Z       QZ_z
    45, //  7       0x07    0x2d            X       QZ_x
    46, //  8       0x08    0x2e            C       QZ_c
    47, //  9       0x09    0x2f            V       QZ_v
    0,  //  10      0x0A    Undefined
    48, //  11      0x0B    0x30            B       QZ_b
    16, //  12      0x0C    0x10            Q       QZ_q
    17, //  13      0x0D    0x11            W       QZ_w
    18, //  14      0x0E    0x12            E       QZ_e
    19, //  15      0x0F    0x13            R       QZ_r
    21, //  16      0x10    0x15            Y       QZ_y
    20, //  17      0x11    0x14            T       QZ_t
    2,  //  18      0x12    0x02            1       QZ_1
    3,  //  19      0x13    0x03            2       QZ_2
    4,  //  20      0x14    0x04            3       QZ_3
    5,  //  21      0x15    0x05            4       QZ_4
    7,  //  22      0x16    0x07            6       QZ_6
    6,  //  23      0x17    0x06            5       QZ_5
    13, //  24      0x18    0x0d            =       QZ_EQUALS
    10, //  25      0x19    0x0a            9       QZ_9
    8,  //  26      0x1A    0x08            7       QZ_7
    12, //  27      0x1B    0x0c            -       QZ_MINUS
    9,  //  28      0x1C    0x09            8       QZ_8
    11, //  29      0x1D    0x0b            0       QZ_0
    27, //  30      0x1E    0x1b            ]       QZ_RIGHTBRACKET
    24, //  31      0x1F    0x18            O       QZ_o
    22, //  32      0x20    0x16            U       QZ_u
    26, //  33      0x21    0x1a            [       QZ_LEFTBRACKET
    23, //  34      0x22    0x17            I       QZ_i
    25, //  35      0x23    0x19            P       QZ_p
    28, //  36      0x24    0x1c            ENTER   QZ_RETURN
    38, //  37      0x25    0x26            L       QZ_l
    36, //  38      0x26    0x24            J       QZ_j
    40, //  39      0x27    0x28            '       QZ_QUOTE
    37, //  40      0x28    0x25            K       QZ_k
    39, //  41      0x29    0x27            ;       QZ_SEMICOLON
    43, //  42      0x2A    0x2b            \       QZ_BACKSLASH
    51, //  43      0x2B    0x33            ,       QZ_COMMA
    53, //  44      0x2C    0x35            /       QZ_SLASH
    49, //  45      0x2D    0x31            N       QZ_n
    50, //  46      0x2E    0x32            M       QZ_m
    52, //  47      0x2F    0x34            .       QZ_PERIOD
    15, //  48      0x30    0x0f            TAB     QZ_TAB
    57, //  49      0x31    0x39            SPACE   QZ_SPACE
    41, //  50      0x32    0x29            `       QZ_BACKQUOTE
    14, //  51      0x33    0x0e            BKSP    QZ_BACKSPACE
    0,  //  52      0x34    Undefined
    1,  //  53      0x35    0x01            ESC     QZ_ESCAPE
    0,  //  54      0x36                            QZ_RMETA
    0,  //  55      0x37                            QZ_LMETA
    42, //  56      0x38    0x2a            L SHFT  QZ_LSHIFT
    58, //  57      0x39    0x3a            CAPS    QZ_CAPSLOCK
    56, //  58      0x3A    0x38            L ALT   QZ_LALT
    29, //  59      0x3B    0x1d            L CTRL  QZ_LCTRL
    54, //  60      0x3C    0x36            R SHFT  QZ_RSHIFT
    184,//  61      0x3D    0xb8    E0,38   R ALT   QZ_RALT
    157,//  62      0x3E    0x9d    E0,1D   R CTRL  QZ_RCTRL
    0,  //  63      0x3F    Undefined
    0,  //  64      0x40    Undefined
    0,  //  65      0x41    Undefined
    0,  //  66      0x42    Undefined
    55, //  67      0x43    0x37            KP *    QZ_KP_MULTIPLY
    0,  //  68      0x44    Undefined
    78, //  69      0x45    0x4e            KP +    QZ_KP_PLUS
    0,  //  70      0x46    Undefined
    69, //  71      0x47    0x45            NUM     QZ_NUMLOCK
    0,  //  72      0x48    Undefined
    0,  //  73      0x49    Undefined
    0,  //  74      0x4A    Undefined
    181,//  75      0x4B    0xb5    E0,35   KP /    QZ_KP_DIVIDE
    152,//  76      0x4C    0x9c    E0,1C   KP EN   QZ_KP_ENTER
    0,  //  77      0x4D    undefined
    74, //  78      0x4E    0x4a            KP -    QZ_KP_MINUS
    0,  //  79      0x4F    Undefined
    0,  //  80      0x50    Undefined
    0,  //  81      0x51                            QZ_KP_EQUALS
    82, //  82      0x52    0x52            KP 0    QZ_KP0
    79, //  83      0x53    0x4f            KP 1    QZ_KP1
    80, //  84      0x54    0x50            KP 2    QZ_KP2
    81, //  85      0x55    0x51            KP 3    QZ_KP3
    75, //  86      0x56    0x4b            KP 4    QZ_KP4
    76, //  87      0x57    0x4c            KP 5    QZ_KP5
    77, //  88      0x58    0x4d            KP 6    QZ_KP6
    71, //  89      0x59    0x47            KP 7    QZ_KP7
    0,  //  90      0x5A    Undefined
    72, //  91      0x5B    0x48            KP 8    QZ_KP8
    73, //  92      0x5C    0x49            KP 9    QZ_KP9
    0,  //  93      0x5D    Undefined
    0,  //  94      0x5E    Undefined
    0,  //  95      0x5F    Undefined
    63, //  96      0x60    0x3f            F5      QZ_F5
    64, //  97      0x61    0x40            F6      QZ_F6
    65, //  98      0x62    0x41            F7      QZ_F7
    61, //  99      0x63    0x3d            F3      QZ_F3
    66, //  100     0x64    0x42            F8      QZ_F8
    67, //  101     0x65    0x43            F9      QZ_F9
    0,  //  102     0x66    Undefined
    87, //  103     0x67    0x57            F11     QZ_F11
    0,  //  104     0x68    Undefined
    183,//  105     0x69    0xb7                    QZ_PRINT
    0,  //  106     0x6A    Undefined
    70, //  107     0x6B    0x46            SCROLL  QZ_SCROLLOCK
    0,  //  108     0x6C    Undefined
    68, //  109     0x6D    0x44            F10     QZ_F10
    0,  //  110     0x6E    Undefined
    88, //  111     0x6F    0x58            F12     QZ_F12
    0,  //  112     0x70    Undefined
    110,//  113     0x71    0x0                     QZ_PAUSE
    210,//  114     0x72    0xd2    E0,52   INSERT  QZ_INSERT
    199,//  115     0x73    0xc7    E0,47   HOME    QZ_HOME
    201,//  116     0x74    0xc9    E0,49   PG UP   QZ_PAGEUP
    211,//  117     0x75    0xd3    E0,53   DELETE  QZ_DELETE
    62, //  118     0x76    0x3e            F4      QZ_F4
    207,//  119     0x77    0xcf    E0,4f   END     QZ_END
    60, //  120     0x78    0x3c            F2      QZ_F2
    209,//  121     0x79    0xd1    E0,51   PG DN   QZ_PAGEDOWN
    59, //  122     0x7A    0x3b            F1      QZ_F1
    203,//  123     0x7B    0xcb    e0,4B   L ARROW QZ_LEFT
    205,//  124     0x7C    0xcd    e0,4D   R ARROW QZ_RIGHT
    208,//  125     0x7D    0xd0    E0,50   D ARROW QZ_DOWN
    200,//  126     0x7E    0xc8    E0,48   U ARROW QZ_UP
/* completed according to http://www.libsdl.org/cgi/cvsweb.cgi/SDL12/src/video/quartz/SDL_QuartzKeys.h?rev=1.6&content-type=text/x-cvsweb-markup */

/* Aditional 104 Key XP-Keyboard Scancodes from http://www.computer-engineering.org/ps2keyboard/scancodes1.html */
/*
    219 //          0xdb            e0,5b   L GUI
    220 //          0xdc            e0,5c   R GUI
    221 //          0xdd            e0,5d   APPS
        //              E0,2A,E0,37         PRNT SCRN
        //              E1,1D,45,E1,9D,C5   PAUSE
    83  //          0x53    0x53            KP .
// ACPI Scan Codes
    222 //          0xde            E0, 5E  Power
    223 //          0xdf            E0, 5F  Sleep
    227 //          0xe3            E0, 63  Wake
// Windows Multimedia Scan Codes
    153 //          0x99            E0, 19  Next Track
    144 //          0x90            E0, 10  Previous Track
    164 //          0xa4            E0, 24  Stop
    162 //          0xa2            E0, 22  Play/Pause
    160 //          0xa0            E0, 20  Mute
    176 //          0xb0            E0, 30  Volume Up
    174 //          0xae            E0, 2E  Volume Down
    237 //          0xed            E0, 6D  Media Select
    236 //          0xec            E0, 6C  E-Mail
    161 //          0xa1            E0, 21  Calculator
    235 //          0xeb            E0, 6B  My Computer
    229 //          0xe5            E0, 65  WWW Search
    178 //          0xb2            E0, 32  WWW Home
    234 //          0xea            E0, 6A  WWW Back
    233 //          0xe9            E0, 69  WWW Forward
    232 //          0xe8            E0, 68  WWW Stop
    231 //          0xe7            E0, 67  WWW Refresh
    230 //          0xe6            E0, 66  WWW Favorites
*/
};

static int cocoa_keycode_to_qemu(int keycode)
{
    if((sizeof(keymap)/sizeof(int)) <= keycode)
    {
        printf("(cocoa) warning unknow keycode 0x%x\n", keycode);
        return 0;
    }
    return keymap[keycode];
}



/*
 ------------------------------------------------------
    QemuCocoaView
 ------------------------------------------------------
*/
@interface QemuCocoaView : NSView
{
    QEMUScreen screen;
    NSWindow *fullScreenWindow;
    float cx,cy,cw,ch,cdx,cdy;
    CGDataProviderRef dataProviderRef;
    int modifiers_state[256];
    BOOL isMouseGrabed;
    BOOL isFullscreen;
    BOOL isAbsoluteEnabled;
    BOOL isTabletEnabled;
}
- (void) resizeContentToWidth:(int)w height:(int)h displayState:(DisplayState *)ds;
- (void) grabMouse;
- (void) ungrabMouse;
- (void) toggleFullScreen:(id)sender;
- (void) handleEvent:(NSEvent *)event;
- (void) setAbsoluteEnabled:(BOOL)tIsAbsoluteEnabled;
- (BOOL) isMouseGrabed;
- (BOOL) isAbsoluteEnabled;
- (float) cdx;
- (float) cdy;
- (QEMUScreen) gscreen;
@end

@implementation QemuCocoaView
- (id)initWithFrame:(NSRect)frameRect
{
    COCOA_DEBUG("QemuCocoaView: initWithFrame\n");

    self = [super initWithFrame:frameRect];
    if (self) {

        screen.bitsPerComponent = 8;
        screen.bitsPerPixel = 32;
        screen.width = frameRect.size.width;
        screen.height = frameRect.size.height;

    }
    return self;
}

- (void) dealloc
{
    COCOA_DEBUG("QemuCocoaView: dealloc\n");

    if (dataProviderRef)
        CGDataProviderRelease(dataProviderRef);

    [super dealloc];
}

- (BOOL) isOpaque
{
    return YES;
}

- (void) drawRect:(NSRect) rect
{
    COCOA_DEBUG("QemuCocoaView: drawRect\n");

    // get CoreGraphic context
    CGContextRef viewContextRef = [[NSGraphicsContext currentContext] graphicsPort];
    CGContextSetInterpolationQuality (viewContextRef, kCGInterpolationNone);
    CGContextSetShouldAntialias (viewContextRef, NO);

    // draw screen bitmap directly to Core Graphics context
    if (dataProviderRef) {
        CGImageRef imageRef = CGImageCreate(
            screen.width, //width
            screen.height, //height
            screen.bitsPerComponent, //bitsPerComponent
            screen.bitsPerPixel, //bitsPerPixel
            (screen.width * (screen.bitsPerComponent/2)), //bytesPerRow
#ifdef __LITTLE_ENDIAN__
            CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB), //colorspace for OS X >= 10.4
            kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst,
#else
            CGColorSpaceCreateDeviceRGB(), //colorspace for OS X < 10.4 (actually ppc)
            kCGImageAlphaNoneSkipFirst, //bitmapInfo
#endif
            dataProviderRef, //provider
            NULL, //decode
            0, //interpolate
            kCGRenderingIntentDefault //intent
        );
// test if host supports "CGImageCreateWithImageInRect" at compile time
#if (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_4)
        if (CGImageCreateWithImageInRect == NULL) { // test if "CGImageCreateWithImageInRect" is supported on host at runtime
#endif
            // compatibility drawing code (draws everything) (OS X < 10.4)
            CGContextDrawImage (viewContextRef, CGRectMake(0, 0, [self bounds].size.width, [self bounds].size.height), imageRef);
#if (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_4)
        } else {
            // selective drawing code (draws only dirty rectangles) (OS X >= 10.4)
            const NSRect *rectList;
#if (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_5)
            NSInteger rectCount;
#else
            int rectCount;
#endif
            int i;
            CGImageRef clipImageRef;
            CGRect clipRect;

            [self getRectsBeingDrawn:&rectList count:&rectCount];
            for (i = 0; i < rectCount; i++) {
                clipRect.origin.x = rectList[i].origin.x / cdx;
                clipRect.origin.y = (float)screen.height - (rectList[i].origin.y + rectList[i].size.height) / cdy;
                clipRect.size.width = rectList[i].size.width / cdx;
                clipRect.size.height = rectList[i].size.height / cdy;
                clipImageRef = CGImageCreateWithImageInRect(
                    imageRef,
                    clipRect
                );
                CGContextDrawImage (viewContextRef, cgrect(rectList[i]), clipImageRef);
                CGImageRelease (clipImageRef);
            }
        }
#endif
        CGImageRelease (imageRef);
    }
}

- (void) setContentDimensions
{
    COCOA_DEBUG("QemuCocoaView: setContentDimensions\n");

    if (isFullscreen) {
        cdx = [[NSScreen mainScreen] frame].size.width / (float)screen.width;
        cdy = [[NSScreen mainScreen] frame].size.height / (float)screen.height;
        cw = screen.width * cdx;
        ch = screen.height * cdy;
        cx = ([[NSScreen mainScreen] frame].size.width - cw) / 2.0;
        cy = ([[NSScreen mainScreen] frame].size.height - ch) / 2.0;
    } else {
        cx = 0;
        cy = 0;
        cw = screen.width;
        ch = screen.height;
        cdx = 1.0;
        cdy = 1.0;
    }
}

- (void) resizeContentToWidth:(int)w height:(int)h displayState:(DisplayState *)ds
{
    COCOA_DEBUG("QemuCocoaView: resizeContent\n");

    // update screenBuffer
    if (dataProviderRef)
        CGDataProviderRelease(dataProviderRef);

    //sync host window color space with guests
	screen.bitsPerPixel = ds_get_bits_per_pixel(ds);
	screen.bitsPerComponent = ds_get_bytes_per_pixel(ds) * 2;

    dataProviderRef = CGDataProviderCreateWithData(NULL, ds_get_data(ds), w * 4 * h, NULL);

    // update windows
    if (isFullscreen) {
        [[fullScreenWindow contentView] setFrame:[[NSScreen mainScreen] frame]];
        [normalWindow setFrame:NSMakeRect([normalWindow frame].origin.x, [normalWindow frame].origin.y - h + screen.height, w, h + [normalWindow frame].size.height - screen.height) display:NO animate:NO];
    } else {
        if (qemu_name)
            [normalWindow setTitle:[NSString stringWithFormat:@"QEMU %s", qemu_name]];
        [normalWindow setFrame:NSMakeRect([normalWindow frame].origin.x, [normalWindow frame].origin.y - h + screen.height, w, h + [normalWindow frame].size.height - screen.height) display:YES animate:NO];
    }
    screen.width = w;
    screen.height = h;
	[normalWindow center];
    [self setContentDimensions];
    [self setFrame:NSMakeRect(cx, cy, cw, ch)];
}

- (void) toggleFullScreen:(id)sender
{
    COCOA_DEBUG("QemuCocoaView: toggleFullScreen\n");

    if (isFullscreen) { // switch from fullscreen to desktop
        isFullscreen = FALSE;
        [self ungrabMouse];
        [self setContentDimensions];
// test if host supports "exitFullScreenModeWithOptions" at compile time
#if (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_5)
        if ([NSView respondsToSelector:@selector(exitFullScreenModeWithOptions:)]) { // test if "exitFullScreenModeWithOptions" is supported on host at runtime
            [self exitFullScreenModeWithOptions:nil];
        } else {
#endif
            [fullScreenWindow close];
            [normalWindow setContentView: self];
            [normalWindow makeKeyAndOrderFront: self];
            [NSMenu setMenuBarVisible:YES];
#if (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_5)
        }
#endif
    } else { // switch from desktop to fullscreen
        isFullscreen = TRUE;
        [self grabMouse];
        [self setContentDimensions];
// test if host supports "enterFullScreenMode:withOptions" at compile time
#if (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_5)
        if ([NSView respondsToSelector:@selector(enterFullScreenMode:withOptions:)]) { // test if "enterFullScreenMode:withOptions" is supported on host at runtime
            [self enterFullScreenMode:[NSScreen mainScreen] withOptions:[NSDictionary dictionaryWithObjectsAndKeys:
                [NSNumber numberWithBool:NO], NSFullScreenModeAllScreens,
                [NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithBool:NO], kCGDisplayModeIsStretched, nil], NSFullScreenModeSetting,
                 nil]];
        } else {
#endif
            [NSMenu setMenuBarVisible:NO];
            fullScreenWindow = [[NSWindow alloc] initWithContentRect:[[NSScreen mainScreen] frame]
                styleMask:NSBorderlessWindowMask
                backing:NSBackingStoreBuffered
                defer:NO];
            [fullScreenWindow setHasShadow:NO];
            [fullScreenWindow setContentView:self];
            [fullScreenWindow makeKeyAndOrderFront:self];
#if (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_5)
        }
#endif
    }
}

- (void) handleEvent:(NSEvent *)event
{
    COCOA_DEBUG("QemuCocoaView: handleEvent\n");

    int buttons = 0;
    int keycode;
    NSPoint p = [event locationInWindow];

    switch ([event type]) {
        case NSFlagsChanged:
            keycode = cocoa_keycode_to_qemu([event keyCode]);
            if (keycode) {
                if (keycode == 58 || keycode == 69) { // emulate caps lock and num lock keydown and keyup
                    kbd_put_keycode(keycode);
                    kbd_put_keycode(keycode | 0x80);
                } else if (is_graphic_console()) {
                    if (keycode & 0x80)
                        kbd_put_keycode(0xe0);
                    if (modifiers_state[keycode] == 0) { // keydown
                        kbd_put_keycode(keycode & 0x7f);
                        modifiers_state[keycode] = 1;
                    } else { // keyup
                        kbd_put_keycode(keycode | 0x80);
                        modifiers_state[keycode] = 0;
                    }
                }
            }

            // release Mouse grab when pressing ctrl+alt
            if (!isFullscreen && ([event modifierFlags] & NSControlKeyMask) && ([event modifierFlags] & NSAlternateKeyMask)) {
                [self ungrabMouse];
            }
            break;
        case NSKeyDown:

            // forward command Key Combos
            if ([event modifierFlags] & NSCommandKeyMask) {
                [NSApp sendEvent:event];
                return;
            }

            // default
            keycode = cocoa_keycode_to_qemu([event keyCode]);

            // handle control + alt Key Combos (ctrl+alt is reserved for QEMU)
            if (([event modifierFlags] & NSControlKeyMask) && ([event modifierFlags] & NSAlternateKeyMask)) {
                switch (keycode) {

                    // enable graphic console
                    case 0x02 ... 0x0a: // '1' to '9' keys
                        console_select(keycode - 0x02);
                        break;
                }

            // handle keys for graphic console
            } else if (is_graphic_console()) {
                if (keycode & 0x80) //check bit for e0 in front
                    kbd_put_keycode(0xe0);
                kbd_put_keycode(keycode & 0x7f); //remove e0 bit in front

            // handlekeys for Monitor
            } else {
                int keysym = 0;
                switch([event keyCode]) {
                case 115:
                    keysym = QEMU_KEY_HOME;
                    break;
                case 117:
                    keysym = QEMU_KEY_DELETE;
                    break;
                case 119:
                    keysym = QEMU_KEY_END;
                    break;
                case 123:
                    keysym = QEMU_KEY_LEFT;
                    break;
                case 124:
                    keysym = QEMU_KEY_RIGHT;
                    break;
                case 125:
                    keysym = QEMU_KEY_DOWN;
                    break;
                case 126:
                    keysym = QEMU_KEY_UP;
                    break;
                default:
                    {
                        NSString *ks = [event characters];
                        if ([ks length] > 0)
                            keysym = [ks characterAtIndex:0];
                    }
                }
                if (keysym)
                    kbd_put_keysym(keysym);
            }
            break;
        case NSKeyUp:
            keycode = cocoa_keycode_to_qemu([event keyCode]);
            if (is_graphic_console()) {
                if (keycode & 0x80)
                    kbd_put_keycode(0xe0);
                kbd_put_keycode(keycode | 0x80); //add 128 to signal release of key
            }
            break;
        case NSMouseMoved:
            if (isAbsoluteEnabled) {
                if (p.x < 0 || p.x > screen.width || p.y < 0 || p.y > screen.height || ![[self window] isKeyWindow]) {
                    if (isTabletEnabled) { // if we leave the window, deactivate the tablet
                        [NSCursor unhide];
                        isTabletEnabled = FALSE;
                    }
                } else {
                    if (!isTabletEnabled) { // if we enter the window, activate the tablet
                        [NSCursor hide];
                        isTabletEnabled = TRUE;
                    }
                }
            }
            COCOA_MOUSE_EVENT
            break;
        case NSLeftMouseDown:
            if ([event modifierFlags] & NSCommandKeyMask) {
                buttons |= MOUSE_EVENT_RBUTTON;
            } else {
                buttons |= MOUSE_EVENT_LBUTTON;
            }
            COCOA_MOUSE_EVENT
            break;
        case NSRightMouseDown:
            buttons |= MOUSE_EVENT_RBUTTON;
            COCOA_MOUSE_EVENT
            break;
        case NSOtherMouseDown:
            buttons |= MOUSE_EVENT_MBUTTON;
            COCOA_MOUSE_EVENT
            break;
        case NSLeftMouseDragged:
            if ([event modifierFlags] & NSCommandKeyMask) {
                buttons |= MOUSE_EVENT_RBUTTON;
            } else {
                buttons |= MOUSE_EVENT_LBUTTON;
            }
            COCOA_MOUSE_EVENT
            break;
        case NSRightMouseDragged:
            buttons |= MOUSE_EVENT_RBUTTON;
            COCOA_MOUSE_EVENT
            break;
        case NSOtherMouseDragged:
            buttons |= MOUSE_EVENT_MBUTTON;
            COCOA_MOUSE_EVENT
            break;
        case NSLeftMouseUp:
            if (isTabletEnabled) {
                    COCOA_MOUSE_EVENT
            } else if (!isMouseGrabed) {
                if (p.x > -1 && p.x < screen.width && p.y > -1 && p.y < screen.height) {
                    [self grabMouse];
                } else {
                    [NSApp sendEvent:event];
                }
            } else {
                COCOA_MOUSE_EVENT
            }
            break;
        case NSRightMouseUp:
            COCOA_MOUSE_EVENT
            break;
        case NSOtherMouseUp:
            COCOA_MOUSE_EVENT
            break;
        case NSScrollWheel:
            if (isTabletEnabled || isMouseGrabed) {
                kbd_mouse_event(0, 0, -[event deltaY], 0);
            } else {
                [NSApp sendEvent:event];
            }
            break;
        default:
            [NSApp sendEvent:event];
    }
}

- (void) grabMouse
{
    COCOA_DEBUG("QemuCocoaView: grabMouse\n");

    if (!isFullscreen) {
        if (qemu_name)
            [normalWindow setTitle:[NSString stringWithFormat:@"QEMU %s - (Press ctrl + alt to release Mouse)", qemu_name]];
        else
            [normalWindow setTitle:@"QEMU - (Press ctrl + alt to release Mouse)"];
    }
    [NSCursor hide];
    CGAssociateMouseAndMouseCursorPosition(FALSE);
    isMouseGrabed = TRUE; // while isMouseGrabed = TRUE, QemuCocoaApp sends all events to [cocoaView handleEvent:]
}

- (void) ungrabMouse
{
    COCOA_DEBUG("QemuCocoaView: ungrabMouse\n");

    if (!isFullscreen) {
        if (qemu_name)
            [normalWindow setTitle:[NSString stringWithFormat:@"QEMU %s", qemu_name]];
        else
            [normalWindow setTitle:@"QEMU"];
    }
    [NSCursor unhide];
    CGAssociateMouseAndMouseCursorPosition(TRUE);
    isMouseGrabed = FALSE;
}

- (void) setAbsoluteEnabled:(BOOL)tIsAbsoluteEnabled {isAbsoluteEnabled = tIsAbsoluteEnabled;}
- (BOOL) isMouseGrabed {return isMouseGrabed;}
- (BOOL) isAbsoluteEnabled {return isAbsoluteEnabled;}
- (float) cdx {return cdx;}
- (float) cdy {return cdy;}
- (QEMUScreen) gscreen {return screen;}
@end



/*
 ------------------------------------------------------
    QemuCocoaAppController
 ------------------------------------------------------
*/
@interface QemuCocoaAppController : NSObject
{
}
- (void)startEmulationWithArgc:(int)argc argv:(char**)argv;
- (void)openPanelDidEnd:(NSOpenPanel *)sheet returnCode:(int)returnCode contextInfo:(void *)contextInfo;
- (void)toggleFullScreen:(id)sender;
- (void)showQEMUDoc:(id)sender;
- (void)showQEMUTec:(id)sender;
@end

@implementation QemuCocoaAppController
- (id) init
{
    COCOA_DEBUG("QemuCocoaAppController: init\n");

    self = [super init];
    if (self) {

        // create a view and add it to the window
        cocoaView = [[QemuCocoaView alloc] initWithFrame:NSMakeRect(0.0, 0.0, 640.0, 480.0)];
        if(!cocoaView) {
            fprintf(stderr, "(cocoa) can't create a view\n");
            exit(1);
        }

        // create a window
        normalWindow = [[NSWindow alloc] initWithContentRect:[cocoaView frame]
            styleMask:NSTitledWindowMask|NSMiniaturizableWindowMask|NSClosableWindowMask
            backing:NSBackingStoreBuffered defer:NO];
        if(!normalWindow) {
            fprintf(stderr, "(cocoa) can't create window\n");
            exit(1);
        }
        [normalWindow setAcceptsMouseMovedEvents:YES];
        [normalWindow setTitle:[NSString stringWithFormat:@"QEMU"]];
        [normalWindow setContentView:cocoaView];
        [normalWindow useOptimizedDrawing:YES];
        [normalWindow makeKeyAndOrderFront:self];
		[normalWindow center];

    }
    return self;
}

- (void) dealloc
{
    COCOA_DEBUG("QemuCocoaAppController: dealloc\n");

    if (cocoaView)
        [cocoaView release];
    [super dealloc];
}

- (void)applicationDidFinishLaunching: (NSNotification *) note
{
    COCOA_DEBUG("QemuCocoaAppController: applicationDidFinishLaunching\n");

    // Display an open dialog box if no argument were passed or
    // if qemu was launched from the finder ( the Finder passes "-psn" )
    if( gArgc <= 1 || strncmp ((char *)gArgv[1], "-psn", 4) == 0) {
        NSOpenPanel *op = [[NSOpenPanel alloc] init];
        [op setPrompt:@"Boot image"];
        [op setMessage:@"Select the disk image you want to boot.\n\nHit the \"Cancel\" button to quit"];
        [op beginSheetForDirectory:nil file:nil types:[NSArray arrayWithObjects:@"img",@"iso",@"dmg",@"qcow",@"cow",@"cloop",@"vmdk",nil]
              modalForWindow:normalWindow modalDelegate:self
              didEndSelector:@selector(openPanelDidEnd:returnCode:contextInfo:) contextInfo:NULL];
    } else {
        // or Launch Qemu, with the global args
        [self startEmulationWithArgc:gArgc argv:(char **)gArgv];
    }
}

- (void)applicationWillTerminate:(NSNotification *)aNotification
{
    COCOA_DEBUG("QemuCocoaAppController: applicationWillTerminate\n");

    qemu_system_shutdown_request();
    exit(0);
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)theApplication
{
    return YES;
}

- (void)startEmulationWithArgc:(int)argc argv:(char**)argv
{
    COCOA_DEBUG("QemuCocoaAppController: startEmulationWithArgc\n");

    int status;
    status = qemu_main(argc, argv);
    exit(status);
}

- (void)openPanelDidEnd:(NSOpenPanel *)sheet returnCode:(int)returnCode contextInfo:(void *)contextInfo
{
    COCOA_DEBUG("QemuCocoaAppController: openPanelDidEnd\n");

    if(returnCode == NSCancelButton) {
        exit(0);
    } else if(returnCode == NSOKButton) {
        const char *bin = "qemu";
        char *img = (char*)[ [ sheet filename ] cStringUsingEncoding:NSASCIIStringEncoding];

        char **argv = (char**)malloc( sizeof(char*)*3 );

        asprintf(&argv[0], "%s", bin);
        asprintf(&argv[1], "-hda");
        asprintf(&argv[2], "%s", img);

        printf("Using argc %d argv %s -hda %s\n", 3, bin, img);

        [self startEmulationWithArgc:3 argv:(char**)argv];
    }
}
- (void)toggleFullScreen:(id)sender
{
    COCOA_DEBUG("QemuCocoaAppController: toggleFullScreen\n");

    [cocoaView toggleFullScreen:sender];
}

- (void)showQEMUDoc:(id)sender
{
    COCOA_DEBUG("QemuCocoaAppController: showQEMUDoc\n");

    [[NSWorkspace sharedWorkspace] openFile:[NSString stringWithFormat:@"%@/../doc/qemu/qemu-doc.html",
        [[NSBundle mainBundle] resourcePath]] withApplication:@"Help Viewer"];
}

- (void)showQEMUTec:(id)sender
{
    COCOA_DEBUG("QemuCocoaAppController: showQEMUTec\n");

    [[NSWorkspace sharedWorkspace] openFile:[NSString stringWithFormat:@"%@/../doc/qemu/qemu-tech.html",
        [[NSBundle mainBundle] resourcePath]] withApplication:@"Help Viewer"];
}
@end



// Dock Connection
typedef struct CPSProcessSerNum
{
        UInt32                lo;
        UInt32                hi;
} CPSProcessSerNum;

OSErr CPSGetCurrentProcess( CPSProcessSerNum *psn);
OSErr CPSEnableForegroundOperation( CPSProcessSerNum *psn, UInt32 _arg2, UInt32 _arg3, UInt32 _arg4, UInt32 _arg5);
OSErr CPSSetFrontProcess( CPSProcessSerNum *psn);

int main (int argc, const char * argv[]) {

    gArgc = argc;
    gArgv = (char **)argv;
    CPSProcessSerNum PSN;
    int i;

    /* In case we don't need to display a window, let's not do that */
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-vnc") ||
            !strcmp(argv[i], "-nographic") ||
            !strcmp(argv[i], "-curses")) {
                return qemu_main(gArgc, gArgv);
        }
    }

    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];

    if (!CPSGetCurrentProcess(&PSN))
        if (!CPSEnableForegroundOperation(&PSN,0x03,0x3C,0x2C,0x1103))
            if (!CPSSetFrontProcess(&PSN))
                [NSApplication sharedApplication];

    // Add menus
    NSMenu      *menu;
    NSMenuItem  *menuItem;

    [NSApp setMainMenu:[[NSMenu alloc] init]];

    // Application menu
    menu = [[NSMenu alloc] initWithTitle:@""];
    [menu addItemWithTitle:@"About QEMU" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""]; // About QEMU
    [menu addItem:[NSMenuItem separatorItem]]; //Separator
    [menu addItemWithTitle:@"Hide QEMU" action:@selector(hide:) keyEquivalent:@"h"]; //Hide QEMU
    menuItem = (NSMenuItem *)[menu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"]; // Hide Others
    [menuItem setKeyEquivalentModifierMask:(NSAlternateKeyMask|NSCommandKeyMask)];
    [menu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""]; // Show All
    [menu addItem:[NSMenuItem separatorItem]]; //Separator
    [menu addItemWithTitle:@"Quit QEMU" action:@selector(terminate:) keyEquivalent:@"q"];
    menuItem = [[NSMenuItem alloc] initWithTitle:@"Apple" action:nil keyEquivalent:@""];
    [menuItem setSubmenu:menu];
    [[NSApp mainMenu] addItem:menuItem];
    [NSApp performSelector:@selector(setAppleMenu:) withObject:menu]; // Workaround (this method is private since 10.4+)

    // View menu
    menu = [[NSMenu alloc] initWithTitle:@"View"];
    [menu addItem: [[[NSMenuItem alloc] initWithTitle:@"Enter Fullscreen" action:@selector(toggleFullScreen:) keyEquivalent:@"f"] autorelease]]; // Fullscreen
    menuItem = [[[NSMenuItem alloc] initWithTitle:@"View" action:nil keyEquivalent:@""] autorelease];
    [menuItem setSubmenu:menu];
    [[NSApp mainMenu] addItem:menuItem];

    // Window menu
    menu = [[NSMenu alloc] initWithTitle:@"Window"];
    [menu addItem: [[[NSMenuItem alloc] initWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"] autorelease]]; // Miniaturize
    menuItem = [[[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""] autorelease];
    [menuItem setSubmenu:menu];
    [[NSApp mainMenu] addItem:menuItem];
    [NSApp setWindowsMenu:menu];

    // Help menu
    menu = [[NSMenu alloc] initWithTitle:@"Help"];
    [menu addItem: [[[NSMenuItem alloc] initWithTitle:@"QEMU Documentation" action:@selector(showQEMUDoc:) keyEquivalent:@"?"] autorelease]]; // QEMU Help
    [menu addItem: [[[NSMenuItem alloc] initWithTitle:@"QEMU Technology" action:@selector(showQEMUTec:) keyEquivalent:@""] autorelease]]; // QEMU Help
    menuItem = [[[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""] autorelease];
    [menuItem setSubmenu:menu];
    [[NSApp mainMenu] addItem:menuItem];

    // Create an Application controller
    QemuCocoaAppController *appController = [[QemuCocoaAppController alloc] init];
    [NSApp setDelegate:appController];

    // Start the main event loop
    [NSApp run];

    [appController release];
    [pool release];

    return 0;
}



#pragma mark qemu
static void cocoa_update(DisplayState *ds, int x, int y, int w, int h)
{
    COCOA_DEBUG("qemu_cocoa: cocoa_update\n");

    NSRect rect;
    if ([cocoaView cdx] == 1.0) {
        rect = NSMakeRect(x, [cocoaView gscreen].height - y - h, w, h);
    } else {
        rect = NSMakeRect(
            x * [cocoaView cdx],
            ([cocoaView gscreen].height - y - h) * [cocoaView cdy],
            w * [cocoaView cdx],
            h * [cocoaView cdy]);
    }
    [cocoaView setNeedsDisplayInRect:rect];
}

static void cocoa_resize(DisplayState *ds)
{
    COCOA_DEBUG("qemu_cocoa: cocoa_resize\n");

    [cocoaView resizeContentToWidth:(int)(ds_get_width(ds)) height:(int)(ds_get_height(ds)) displayState:ds];
}

static void cocoa_refresh(DisplayState *ds)
{
    COCOA_DEBUG("qemu_cocoa: cocoa_refresh\n");

    if (kbd_mouse_is_absolute()) {
        if (![cocoaView isAbsoluteEnabled]) {
            if ([cocoaView isMouseGrabed]) {
                [cocoaView ungrabMouse];
            }
        }
        [cocoaView setAbsoluteEnabled:YES];
    }

    NSDate *distantPast;
    NSEvent *event;
    distantPast = [NSDate distantPast];
    do {
        event = [NSApp nextEventMatchingMask:NSAnyEventMask untilDate:distantPast
                        inMode: NSDefaultRunLoopMode dequeue:YES];
        if (event != nil) {
            [cocoaView handleEvent:event];
        }
    } while(event != nil);
    vga_hw_update();
}

static void cocoa_cleanup(void)
{
    COCOA_DEBUG("qemu_cocoa: cocoa_cleanup\n");
	qemu_free(dcl);
}

void cocoa_display_init(DisplayState *ds, int full_screen)
{
    COCOA_DEBUG("qemu_cocoa: cocoa_display_init\n");

	dcl = qemu_mallocz(sizeof(DisplayChangeListener));
	
    // register vga output callbacks
    dcl->dpy_update = cocoa_update;
    dcl->dpy_resize = cocoa_resize;
    dcl->dpy_refresh = cocoa_refresh;

	register_displaychangelistener(ds, dcl);

    // register cleanup function
    atexit(cocoa_cleanup);
}
