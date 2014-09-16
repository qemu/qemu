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
#include <crt_externs.h>

#include "qemu-common.h"
#include "ui/console.h"
#include "ui/input.h"
#include "sysemu/sysemu.h"

#ifndef MAC_OS_X_VERSION_10_4
#define MAC_OS_X_VERSION_10_4 1040
#endif
#ifndef MAC_OS_X_VERSION_10_5
#define MAC_OS_X_VERSION_10_5 1050
#endif
#ifndef MAC_OS_X_VERSION_10_6
#define MAC_OS_X_VERSION_10_6 1060
#endif


//#define DEBUG

#ifdef DEBUG
#define COCOA_DEBUG(...)  { (void) fprintf (stdout, __VA_ARGS__); }
#else
#define COCOA_DEBUG(...)  ((void) 0)
#endif

#define cgrect(nsrect) (*(CGRect *)&(nsrect))

typedef struct {
    int width;
    int height;
    int bitsPerComponent;
    int bitsPerPixel;
} QEMUScreen;

NSWindow *normalWindow;
static DisplayChangeListener *dcl;
static int last_buttons;

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
    220, // 54      0x36    0xdc    E0,5C   R GUI   QZ_RMETA
    219, // 55      0x37    0xdb    E0,5B   L GUI   QZ_LMETA
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

/* Additional 104 Key XP-Keyboard Scancodes from http://www.computer-engineering.org/ps2keyboard/scancodes1.html */
/*
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
    if (ARRAY_SIZE(keymap) <= keycode) {
        fprintf(stderr, "(cocoa) warning unknown keycode 0x%x\n", keycode);
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
    BOOL isMouseGrabbed;
    BOOL isFullscreen;
    BOOL isAbsoluteEnabled;
    BOOL isMouseDeassociated;
}
- (void) switchSurface:(DisplaySurface *)surface;
- (void) grabMouse;
- (void) ungrabMouse;
- (void) toggleFullScreen:(id)sender;
- (void) handleEvent:(NSEvent *)event;
- (void) setAbsoluteEnabled:(BOOL)tIsAbsoluteEnabled;
/* The state surrounding mouse grabbing is potentially confusing.
 * isAbsoluteEnabled tracks qemu_input_is_absolute() [ie "is the emulated
 *   pointing device an absolute-position one?"], but is only updated on
 *   next refresh.
 * isMouseGrabbed tracks whether GUI events are directed to the guest;
 *   it controls whether special keys like Cmd get sent to the guest,
 *   and whether we capture the mouse when in non-absolute mode.
 * isMouseDeassociated tracks whether we've told MacOSX to disassociate
 *   the mouse and mouse cursor position by calling
 *   CGAssociateMouseAndMouseCursorPosition(FALSE)
 *   (which basically happens if we grab in non-absolute mode).
 */
- (BOOL) isMouseGrabbed;
- (BOOL) isAbsoluteEnabled;
- (BOOL) isMouseDeassociated;
- (float) cdx;
- (float) cdy;
- (QEMUScreen) gscreen;
@end

QemuCocoaView *cocoaView;

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

- (BOOL) screenContainsPoint:(NSPoint) p
{
    return (p.x > -1 && p.x < screen.width && p.y > -1 && p.y < screen.height);
}

- (void) hideCursor
{
    if (!cursor_hide) {
        return;
    }
    [NSCursor hide];
}

- (void) unhideCursor
{
    if (!cursor_hide) {
        return;
    }
    [NSCursor unhide];
}

- (void) drawRect:(NSRect) rect
{
    COCOA_DEBUG("QemuCocoaView: drawRect\n");

    // get CoreGraphic context
    CGContextRef viewContextRef = [[NSGraphicsContext currentContext] graphicsPort];
    CGContextSetInterpolationQuality (viewContextRef, kCGInterpolationNone);
    CGContextSetShouldAntialias (viewContextRef, NO);

    // draw screen bitmap directly to Core Graphics context
    if (!dataProviderRef) {
        // Draw request before any guest device has set up a framebuffer:
        // just draw an opaque black rectangle
        CGContextSetRGBFillColor(viewContextRef, 0, 0, 0, 1.0);
        CGContextFillRect(viewContextRef, NSRectToCGRect(rect));
    } else {
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

- (void) switchSurface:(DisplaySurface *)surface
{
    COCOA_DEBUG("QemuCocoaView: switchSurface\n");

    int w = surface_width(surface);
    int h = surface_height(surface);
    /* cdx == 0 means this is our very first surface, in which case we need
     * to recalculate the content dimensions even if it happens to be the size
     * of the initial empty window.
     */
    bool isResize = (w != screen.width || h != screen.height || cdx == 0.0);

    int oldh = screen.height;
    if (isResize) {
        // Resize before we trigger the redraw, or we'll redraw at the wrong size
        COCOA_DEBUG("switchSurface: new size %d x %d\n", w, h);
        screen.width = w;
        screen.height = h;
        [self setContentDimensions];
        [self setFrame:NSMakeRect(cx, cy, cw, ch)];
    }

    // update screenBuffer
    if (dataProviderRef)
        CGDataProviderRelease(dataProviderRef);

    //sync host window color space with guests
    screen.bitsPerPixel = surface_bits_per_pixel(surface);
    screen.bitsPerComponent = surface_bytes_per_pixel(surface) * 2;

    dataProviderRef = CGDataProviderCreateWithData(NULL, surface_data(surface), w * 4 * h, NULL);

    // update windows
    if (isFullscreen) {
        [[fullScreenWindow contentView] setFrame:[[NSScreen mainScreen] frame]];
        [normalWindow setFrame:NSMakeRect([normalWindow frame].origin.x, [normalWindow frame].origin.y - h + oldh, w, h + [normalWindow frame].size.height - oldh) display:NO animate:NO];
    } else {
        if (qemu_name)
            [normalWindow setTitle:[NSString stringWithFormat:@"QEMU %s", qemu_name]];
        [normalWindow setFrame:NSMakeRect([normalWindow frame].origin.x, [normalWindow frame].origin.y - h + oldh, w, h + [normalWindow frame].size.height - oldh) display:YES animate:NO];
    }

    if (isResize) {
        [normalWindow center];
    }
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
    bool mouse_event = false;
    NSPoint p = [event locationInWindow];

    switch ([event type]) {
        case NSFlagsChanged:
            keycode = cocoa_keycode_to_qemu([event keyCode]);

            if ((keycode == 219 || keycode == 220) && !isMouseGrabbed) {
              /* Don't pass command key changes to guest unless mouse is grabbed */
              keycode = 0;
            }

            if (keycode) {
                if (keycode == 58 || keycode == 69) { // emulate caps lock and num lock keydown and keyup
                    qemu_input_event_send_key_number(dcl->con, keycode, true);
                    qemu_input_event_send_key_number(dcl->con, keycode, false);
                } else if (qemu_console_is_graphic(NULL)) {
                    if (modifiers_state[keycode] == 0) { // keydown
                        qemu_input_event_send_key_number(dcl->con, keycode, true);
                        modifiers_state[keycode] = 1;
                    } else { // keyup
                        qemu_input_event_send_key_number(dcl->con, keycode, false);
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
            keycode = cocoa_keycode_to_qemu([event keyCode]);

            // forward command key combos to the host UI unless the mouse is grabbed
            if (!isMouseGrabbed && ([event modifierFlags] & NSCommandKeyMask)) {
                [NSApp sendEvent:event];
                return;
            }

            // default

            // handle control + alt Key Combos (ctrl+alt is reserved for QEMU)
            if (([event modifierFlags] & NSControlKeyMask) && ([event modifierFlags] & NSAlternateKeyMask)) {
                switch (keycode) {

                    // enable graphic console
                    case 0x02 ... 0x0a: // '1' to '9' keys
                        console_select(keycode - 0x02);
                        break;
                }

            // handle keys for graphic console
            } else if (qemu_console_is_graphic(NULL)) {
                qemu_input_event_send_key_number(dcl->con, keycode, true);

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

            // don't pass the guest a spurious key-up if we treated this
            // command-key combo as a host UI action
            if (!isMouseGrabbed && ([event modifierFlags] & NSCommandKeyMask)) {
                return;
            }

            if (qemu_console_is_graphic(NULL)) {
                qemu_input_event_send_key_number(dcl->con, keycode, false);
            }
            break;
        case NSMouseMoved:
            if (isAbsoluteEnabled) {
                if (![self screenContainsPoint:p] || ![[self window] isKeyWindow]) {
                    if (isMouseGrabbed) {
                        [self ungrabMouse];
                    }
                } else {
                    if (!isMouseGrabbed) {
                        [self grabMouse];
                    }
                }
            }
            mouse_event = true;
            break;
        case NSLeftMouseDown:
            if ([event modifierFlags] & NSCommandKeyMask) {
                buttons |= MOUSE_EVENT_RBUTTON;
            } else {
                buttons |= MOUSE_EVENT_LBUTTON;
            }
            mouse_event = true;
            break;
        case NSRightMouseDown:
            buttons |= MOUSE_EVENT_RBUTTON;
            mouse_event = true;
            break;
        case NSOtherMouseDown:
            buttons |= MOUSE_EVENT_MBUTTON;
            mouse_event = true;
            break;
        case NSLeftMouseDragged:
            if ([event modifierFlags] & NSCommandKeyMask) {
                buttons |= MOUSE_EVENT_RBUTTON;
            } else {
                buttons |= MOUSE_EVENT_LBUTTON;
            }
            mouse_event = true;
            break;
        case NSRightMouseDragged:
            buttons |= MOUSE_EVENT_RBUTTON;
            mouse_event = true;
            break;
        case NSOtherMouseDragged:
            buttons |= MOUSE_EVENT_MBUTTON;
            mouse_event = true;
            break;
        case NSLeftMouseUp:
            mouse_event = true;
            if (!isMouseGrabbed && [self screenContainsPoint:p]) {
                [self grabMouse];
            }
            break;
        case NSRightMouseUp:
            mouse_event = true;
            break;
        case NSOtherMouseUp:
            mouse_event = true;
            break;
        case NSScrollWheel:
            if (isMouseGrabbed) {
                buttons |= ([event deltaY] < 0) ?
                    MOUSE_EVENT_WHEELUP : MOUSE_EVENT_WHEELDN;
            }
            mouse_event = true;
            break;
        default:
            [NSApp sendEvent:event];
    }

    if (mouse_event) {
        if (last_buttons != buttons) {
            static uint32_t bmap[INPUT_BUTTON_MAX] = {
                [INPUT_BUTTON_LEFT]       = MOUSE_EVENT_LBUTTON,
                [INPUT_BUTTON_MIDDLE]     = MOUSE_EVENT_MBUTTON,
                [INPUT_BUTTON_RIGHT]      = MOUSE_EVENT_RBUTTON,
                [INPUT_BUTTON_WHEEL_UP]   = MOUSE_EVENT_WHEELUP,
                [INPUT_BUTTON_WHEEL_DOWN] = MOUSE_EVENT_WHEELDN,
            };
            qemu_input_update_buttons(dcl->con, bmap, last_buttons, buttons);
            last_buttons = buttons;
        }
        if (isMouseGrabbed) {
            if (isAbsoluteEnabled) {
                /* Note that the origin for Cocoa mouse coords is bottom left, not top left.
                 * The check on screenContainsPoint is to avoid sending out of range values for
                 * clicks in the titlebar.
                 */
                if ([self screenContainsPoint:p]) {
                    qemu_input_queue_abs(dcl->con, INPUT_AXIS_X, p.x, screen.width);
                    qemu_input_queue_abs(dcl->con, INPUT_AXIS_Y, screen.height - p.y, screen.height);
                }
            } else {
                qemu_input_queue_rel(dcl->con, INPUT_AXIS_X, (int)[event deltaX]);
                qemu_input_queue_rel(dcl->con, INPUT_AXIS_Y, (int)[event deltaY]);
            }
        } else {
            [NSApp sendEvent:event];
        }
        qemu_input_event_sync();
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
    [self hideCursor];
    if (!isAbsoluteEnabled) {
        isMouseDeassociated = TRUE;
        CGAssociateMouseAndMouseCursorPosition(FALSE);
    }
    isMouseGrabbed = TRUE; // while isMouseGrabbed = TRUE, QemuCocoaApp sends all events to [cocoaView handleEvent:]
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
    [self unhideCursor];
    if (isMouseDeassociated) {
        CGAssociateMouseAndMouseCursorPosition(TRUE);
        isMouseDeassociated = FALSE;
    }
    isMouseGrabbed = FALSE;
}

- (void) setAbsoluteEnabled:(BOOL)tIsAbsoluteEnabled {isAbsoluteEnabled = tIsAbsoluteEnabled;}
- (BOOL) isMouseGrabbed {return isMouseGrabbed;}
- (BOOL) isAbsoluteEnabled {return isAbsoluteEnabled;}
- (BOOL) isMouseDeassociated {return isMouseDeassociated;}
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

    // Display an open dialog box if no arguments were passed or
    // if qemu was launched from the finder ( the Finder passes "-psn" )
    if( gArgc <= 1 || strncmp ((char *)gArgv[1], "-psn", 4) == 0) {
        NSOpenPanel *op = [[NSOpenPanel alloc] init];
        [op setPrompt:@"Boot image"];
        [op setMessage:@"Select the disk image you want to boot.\n\nHit the \"Cancel\" button to quit"];
        NSArray *filetypes = [NSArray arrayWithObjects:@"img", @"iso", @"dmg",
                                 @"qcow", @"qcow2", @"cloop", @"vmdk", nil];
#if (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_6)
        [op setAllowedFileTypes:filetypes];
        [op beginSheetModalForWindow:normalWindow
            completionHandler:^(NSInteger returnCode)
            { [self openPanelDidEnd:op
                  returnCode:returnCode contextInfo:NULL ]; } ];
#else
        // Compatibility code for pre-10.6, using deprecated method
        [op beginSheetForDirectory:nil file:nil types:filetypes
              modalForWindow:normalWindow modalDelegate:self
              didEndSelector:@selector(openPanelDidEnd:returnCode:contextInfo:) contextInfo:NULL];
#endif
    } else {
        // or launch QEMU, with the global args
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
    status = qemu_main(argc, argv, *_NSGetEnviron());
    exit(status);
}

- (void)openPanelDidEnd:(NSOpenPanel *)sheet returnCode:(int)returnCode contextInfo:(void *)contextInfo
{
    COCOA_DEBUG("QemuCocoaAppController: openPanelDidEnd\n");

    if(returnCode == NSCancelButton) {
        exit(0);
    } else if(returnCode == NSOKButton) {
        char *img = (char*)[ [ [ sheet URL ] path ] cStringUsingEncoding:NSASCIIStringEncoding];

        char **argv = g_new(char *, 4);

        [sheet close];

        argv[0] = g_strdup(gArgv[0]);
        argv[1] = g_strdup("-hda");
        argv[2] = g_strdup(img);
        argv[3] = NULL;

        // printf("Using argc %d argv %s -hda %s\n", 3, gArgv[0], img);

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



int main (int argc, const char * argv[]) {

    gArgc = argc;
    gArgv = (char **)argv;
    int i;

    /* In case we don't need to display a window, let's not do that */
    for (i = 1; i < argc; i++) {
        const char *opt = argv[i];

        if (opt[0] == '-') {
            /* Treat --foo the same as -foo.  */
            if (opt[1] == '-') {
                opt++;
            }
            if (!strcmp(opt, "-h") || !strcmp(opt, "-help") ||
                !strcmp(opt, "-vnc") ||
                !strcmp(opt, "-nographic") ||
                !strcmp(opt, "-version") ||
                !strcmp(opt, "-curses") ||
                !strcmp(opt, "-qtest")) {
                return qemu_main(gArgc, gArgv, *_NSGetEnviron());
            }
        }
    }

    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];

    // Pull this console process up to being a fully-fledged graphical
    // app with a menubar and Dock icon
    ProcessSerialNumber psn = { 0, kCurrentProcess };
    TransformProcessType(&psn, kProcessTransformToForegroundApplication);

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
static void cocoa_update(DisplayChangeListener *dcl,
                         int x, int y, int w, int h)
{
    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];

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

    [pool release];
}

static void cocoa_switch(DisplayChangeListener *dcl,
                         DisplaySurface *surface)
{
    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];

    COCOA_DEBUG("qemu_cocoa: cocoa_switch\n");
    [cocoaView switchSurface:surface];
    [pool release];
}

static void cocoa_refresh(DisplayChangeListener *dcl)
{
    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];

    COCOA_DEBUG("qemu_cocoa: cocoa_refresh\n");

    if (qemu_input_is_absolute()) {
        if (![cocoaView isAbsoluteEnabled]) {
            if ([cocoaView isMouseGrabbed]) {
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
    graphic_hw_update(NULL);
    [pool release];
}

static void cocoa_cleanup(void)
{
    COCOA_DEBUG("qemu_cocoa: cocoa_cleanup\n");
    g_free(dcl);
}

static const DisplayChangeListenerOps dcl_ops = {
    .dpy_name          = "cocoa",
    .dpy_gfx_update = cocoa_update,
    .dpy_gfx_switch = cocoa_switch,
    .dpy_refresh = cocoa_refresh,
};

void cocoa_display_init(DisplayState *ds, int full_screen)
{
    COCOA_DEBUG("qemu_cocoa: cocoa_display_init\n");

    dcl = g_malloc0(sizeof(DisplayChangeListener));

    // register vga output callbacks
    dcl->ops = &dcl_ops;
    register_displaychangelistener(dcl);

    // register cleanup function
    atexit(cocoa_cleanup);
}
