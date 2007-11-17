/*
 * QEMU Cocoa display driver
 *
 * Copyright (c) 2005 Pierre d'Herbemont
 *                    many code/inspiration from SDL 1.2 code (LGPL)
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
/*
    Todo :    x  miniaturize window
              x  center the window
              -  save window position
              -  handle keyboard event
              -  handle mouse event
              -  non 32 bpp support
              -  full screen
              -  mouse focus
              x  simple graphical prompt to demo
              -  better graphical prompt
*/

#import <Cocoa/Cocoa.h>

#include "qemu-common.h"
#include "console.h"
#include "sysemu.h"

NSWindow *window = NULL;
NSQuickDrawView *qd_view = NULL;


int gArgc;
char **gArgv;
DisplayState current_ds;

int grab = 0;
int modifiers_state[256];

/* main defined in qemu/vl.c */
int qemu_main(int argc, char **argv);

/* To deal with miniaturization */
@interface QemuWindow : NSWindow
{ }
@end


/*
 ------------------------------------------------------
    Qemu Video Driver
 ------------------------------------------------------
*/

/*
 ------------------------------------------------------
    cocoa_update
 ------------------------------------------------------
*/
static void cocoa_update(DisplayState *ds, int x, int y, int w, int h)
{
    //printf("updating x=%d y=%d w=%d h=%d\n", x, y, w, h);

    /* Use QDFlushPortBuffer() to flush content to display */
    RgnHandle dirty = NewRgn ();
    RgnHandle temp  = NewRgn ();

    SetEmptyRgn (dirty);

    /* Build the region of dirty rectangles */
    MacSetRectRgn (temp, x, y,
                        x + w, y + h);
    MacUnionRgn (dirty, temp, dirty);

    /* Flush the dirty region */
    QDFlushPortBuffer ( [ qd_view  qdPort ], dirty );
    DisposeRgn (dirty);
    DisposeRgn (temp);
}

/*
 ------------------------------------------------------
    cocoa_resize
 ------------------------------------------------------
*/
static void cocoa_resize(DisplayState *ds, int w, int h)
{
    const int device_bpp = 32;
    static void *screen_pixels;
    static int  screen_pitch;
    NSRect contentRect;

    //printf("resizing to %d %d\n", w, h);

    contentRect = NSMakeRect (0, 0, w, h);
    if(window)
    {
        [window close];
        [window release];
    }
    window = [ [ QemuWindow alloc ] initWithContentRect:contentRect
                                  styleMask:NSTitledWindowMask|NSMiniaturizableWindowMask|NSClosableWindowMask
                                  backing:NSBackingStoreBuffered defer:NO];
    if(!window)
    {
        fprintf(stderr, "(cocoa) can't create window\n");
        exit(1);
    }

    if(qd_view)
        [qd_view release];

    qd_view = [ [ NSQuickDrawView alloc ] initWithFrame:contentRect ];

    if(!qd_view)
    {
         fprintf(stderr, "(cocoa) can't create qd_view\n");
        exit(1);
    }

    [ window setAcceptsMouseMovedEvents:YES ];
    [ window setTitle:@"Qemu" ];
    [ window setReleasedWhenClosed:NO ];

    /* Set screen to black */
    [ window setBackgroundColor: [NSColor blackColor] ];

    /* set window position */
    [ window center ];

    [ qd_view setAutoresizingMask: NSViewWidthSizable | NSViewHeightSizable ];
    [ [ window contentView ] addSubview:qd_view ];
    [ qd_view release ];
    [ window makeKeyAndOrderFront:nil ];

    /* Careful here, the window seems to have to be onscreen to do that */
    LockPortBits ( [ qd_view qdPort ] );
    screen_pixels = GetPixBaseAddr ( GetPortPixMap ( [ qd_view qdPort ] ) );
    screen_pitch  = GetPixRowBytes ( GetPortPixMap ( [ qd_view qdPort ] ) );
    UnlockPortBits ( [ qd_view qdPort ] );
    {
            int vOffset = [ window frame ].size.height -
                [ qd_view frame ].size.height - [ qd_view frame ].origin.y;

            int hOffset = [ qd_view frame ].origin.x;

            screen_pixels += (vOffset * screen_pitch) + hOffset * (device_bpp/8);
    }
    ds->data = screen_pixels;
    ds->linesize = screen_pitch;
    ds->depth = device_bpp;
    ds->width = w;
    ds->height = h;
#ifdef __LITTLE_ENDIAN__
    ds->bgr = 1;
#else
    ds->bgr = 0;
#endif

    current_ds = *ds;
}

/*
 ------------------------------------------------------
    keymap conversion
 ------------------------------------------------------
*/

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
    183,//  105     0x69    0xb7            QZ_PRINT
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

int cocoa_keycode_to_qemu(int keycode)
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
    cocoa_refresh
 ------------------------------------------------------
*/
static void cocoa_refresh(DisplayState *ds)
{
    //printf("cocoa_refresh \n");
    NSDate *distantPast;
    NSEvent *event;
    NSAutoreleasePool *pool;

    pool = [ [ NSAutoreleasePool alloc ] init ];
    distantPast = [ NSDate distantPast ];

    vga_hw_update();

    do {
        event = [ NSApp nextEventMatchingMask:NSAnyEventMask untilDate:distantPast
                        inMode: NSDefaultRunLoopMode dequeue:YES ];
        if (event != nil) {
            switch ([event type]) {
                case NSFlagsChanged:
                    {
                        int keycode = cocoa_keycode_to_qemu([event keyCode]);

                        if (keycode)
                        {
                            if (keycode == 58 || keycode == 69) {
                                /* emulate caps lock and num lock keydown and keyup */
                                kbd_put_keycode(keycode);
                                kbd_put_keycode(keycode | 0x80);
                            } else if (is_graphic_console()) {
                                if (keycode & 0x80)
                                    kbd_put_keycode(0xe0);
                                if (modifiers_state[keycode] == 0) {
                                    /* keydown */
                                    kbd_put_keycode(keycode & 0x7f);
                                    modifiers_state[keycode] = 1;
                                } else {
                                    /* keyup */
                                    kbd_put_keycode(keycode | 0x80);
                                    modifiers_state[keycode] = 0;
                                }
                            }
                        }

                        /* release Mouse grab when pressing ctrl+alt */
                        if (([event modifierFlags] & NSControlKeyMask) && ([event modifierFlags] & NSAlternateKeyMask))
                        {
                            [window setTitle: @"QEMU"];
                            [NSCursor unhide];
                            CGAssociateMouseAndMouseCursorPosition ( TRUE );
                            grab = 0;
                        }
                    }
                    break;

                case NSKeyDown:
                    {
                        int keycode = cocoa_keycode_to_qemu([event keyCode]);

                        /* handle command Key Combos */
                        if ([event modifierFlags] & NSCommandKeyMask) {
                            switch ([event keyCode]) {
                                /* quit */
                                case 12: /* q key */
                                    /* switch to windowed View */
                                    exit(0);
                                    return;
                            }
                        }

                        /* handle control + alt Key Combos */
                        if (([event modifierFlags] & NSControlKeyMask) && ([event modifierFlags] & NSAlternateKeyMask)) {
                            switch (keycode) {
                                /* toggle Monitor */
                                case 0x02 ... 0x0a: /* '1' to '9' keys */
                                    console_select(keycode - 0x02);
                                    break;
                            }
                        } else {
                            /* handle standard key events */
                            if (is_graphic_console()) {
                                if (keycode & 0x80) //check bit for e0 in front
                                    kbd_put_keycode(0xe0);
                                kbd_put_keycode(keycode & 0x7f); //remove e0 bit in front
                            /* handle monitor key events */
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
                        }
                    }
                    break;

                case NSKeyUp:
                    {
                        int keycode = cocoa_keycode_to_qemu([event keyCode]);
                        if (is_graphic_console()) {
                            if (keycode & 0x80)
                                kbd_put_keycode(0xe0);
                            kbd_put_keycode(keycode | 0x80); //add 128 to signal release of key
                        }
                    }
                    break;

                case NSMouseMoved:
                    if (grab) {
                        int dx = [event deltaX];
                        int dy = [event deltaY];
                        int dz = [event deltaZ];
                        int buttons = 0;
                        kbd_mouse_event(dx, dy, dz, buttons);
                    }
                    break;

                case NSLeftMouseDown:
                    if (grab) {
                        int buttons = 0;

                        /* leftclick+command simulates rightclick */
                        if ([event modifierFlags] & NSCommandKeyMask) {
                            buttons |= MOUSE_EVENT_RBUTTON;
                        } else {
                            buttons |= MOUSE_EVENT_LBUTTON;
                        }
                        kbd_mouse_event(0, 0, 0, buttons);
                    } else {
                        [NSApp sendEvent: event];
                    }
                    break;

                case NSLeftMouseDragged:
                    if (grab) {
                        int dx = [event deltaX];
                        int dy = [event deltaY];
                        int dz = [event deltaZ];
                        int buttons = 0;
                        if ([[NSApp currentEvent] modifierFlags] & NSCommandKeyMask) { //leftclick+command simulates rightclick
                            buttons |= MOUSE_EVENT_RBUTTON;
                        } else {
                            buttons |= MOUSE_EVENT_LBUTTON;
                        }
                        kbd_mouse_event(dx, dy, dz, buttons);
                    }
                    break;

                case NSLeftMouseUp:
                    if (grab) {
                        kbd_mouse_event(0, 0, 0, 0);
                    } else {
                        [window setTitle: @"QEMU (Press ctrl + alt to release Mouse)"];
                        [NSCursor hide];
                        CGAssociateMouseAndMouseCursorPosition ( FALSE );
                        grab = 1;
                        //[NSApp sendEvent: event];
                    }
                    break;

                case NSRightMouseDown:
                    if (grab) {
                        int buttons = 0;

                        buttons |= MOUSE_EVENT_RBUTTON;
                        kbd_mouse_event(0, 0, 0, buttons);
                    } else {
                        [NSApp sendEvent: event];
                    }
                    break;

                case NSRightMouseDragged:
                    if (grab) {
                        int dx = [event deltaX];
                        int dy = [event deltaY];
                        int dz = [event deltaZ];
                        int buttons = 0;
                        buttons |= MOUSE_EVENT_RBUTTON;
                        kbd_mouse_event(dx, dy, dz, buttons);
                    }
                    break;

                case NSRightMouseUp:
                    if (grab) {
                        kbd_mouse_event(0, 0, 0, 0);
                    } else {
                        [NSApp sendEvent: event];
                    }
                    break;

                case NSOtherMouseDragged:
                    if (grab) {
                        int dx = [event deltaX];
                        int dy = [event deltaY];
                        int dz = [event deltaZ];
                        int buttons = 0;
                        buttons |= MOUSE_EVENT_MBUTTON;
                        kbd_mouse_event(dx, dy, dz, buttons);
                    }
                    break;

                case NSOtherMouseDown:
                    if (grab) {
                        int buttons = 0;
                        buttons |= MOUSE_EVENT_MBUTTON;
                        kbd_mouse_event(0, 0, 0, buttons);
                    } else {
                        [NSApp sendEvent:event];
                    }
                    break;

                case NSOtherMouseUp:
                    if (grab) {
                        kbd_mouse_event(0, 0, 0, 0);
                    } else {
                        [NSApp sendEvent: event];
                    }
                    break;

                case NSScrollWheel:
                    if (grab) {
                        int dz = [event deltaY];
                        kbd_mouse_event(0, 0, -dz, 0);
                    }
                    break;

                default: [NSApp sendEvent:event];
            }
        }
    } while(event != nil);
}

/*
 ------------------------------------------------------
    cocoa_cleanup
 ------------------------------------------------------
*/

static void cocoa_cleanup(void)
{

}

/*
 ------------------------------------------------------
    cocoa_display_init
 ------------------------------------------------------
*/

void cocoa_display_init(DisplayState *ds, int full_screen)
{
    ds->dpy_update = cocoa_update;
    ds->dpy_resize = cocoa_resize;
    ds->dpy_refresh = cocoa_refresh;

    cocoa_resize(ds, 640, 400);

    atexit(cocoa_cleanup);
}

/*
 ------------------------------------------------------
    Interface with Cocoa
 ------------------------------------------------------
*/


/*
 ------------------------------------------------------
    QemuWindow
    Some trick from SDL to use miniwindow
 ------------------------------------------------------
*/
static void QZ_SetPortAlphaOpaque ()
{
    /* Assume 32 bit if( bpp == 32 )*/
    if ( 1 ) {

        uint32_t    *pixels = (uint32_t*) current_ds.data;
        uint32_t    rowPixels = current_ds.linesize / 4;
        uint32_t    i, j;

        for (i = 0; i < current_ds.height; i++)
            for (j = 0; j < current_ds.width; j++) {

                pixels[ (i * rowPixels) + j ] |= 0xFF000000;
            }
    }
}

@implementation QemuWindow
- (void)miniaturize:(id)sender
{

    /* make the alpha channel opaque so anim won't have holes in it */
    QZ_SetPortAlphaOpaque ();

    [ super miniaturize:sender ];

}
- (void)display
{
    /*
        This method fires just before the window deminaturizes from the Dock.

        We'll save the current visible surface, let the window manager redraw any
        UI elements, and restore the SDL surface. This way, no expose event
        is required, and the deminiaturize works perfectly.
    */

    /* make sure pixels are fully opaque */
    QZ_SetPortAlphaOpaque ();

    /* save current visible SDL surface */
    [ self cacheImageInRect:[ qd_view frame ] ];

    /* let the window manager redraw controls, border, etc */
    [ super display ];

    /* restore visible SDL surface */
    [ self restoreCachedImage ];
}

@end


/*
 ------------------------------------------------------
    QemuCocoaGUIController
    NSApp's delegate - indeed main object
 ------------------------------------------------------
*/

@interface QemuCocoaGUIController : NSObject
{
}
- (void)applicationDidFinishLaunching: (NSNotification *) note;
- (void)applicationWillTerminate:(NSNotification *)aNotification;

- (void)openPanelDidEnd:(NSOpenPanel *)sheet returnCode:(int)returnCode contextInfo:(void *)contextInfo;

- (void)startEmulationWithArgc:(int)argc argv:(char**)argv;
@end

@implementation QemuCocoaGUIController
/* Called when the internal event loop has just started running */
- (void)applicationDidFinishLaunching: (NSNotification *) note
{

    /* Display an open dialog box if no argument were passed or
       if qemu was launched from the finder ( the Finder passes "-psn" ) */

    if( gArgc <= 1 || strncmp (gArgv[1], "-psn", 4) == 0)
    {
        NSOpenPanel *op = [[NSOpenPanel alloc] init];

        cocoa_resize(&current_ds, 640, 400);

        [op setPrompt:@"Boot image"];

        [op setMessage:@"Select the disk image you want to boot.\n\nHit the \"Cancel\" button to quit"];

        [op beginSheetForDirectory:nil file:nil types:[NSArray arrayWithObjects:@"img",@"iso",@"dmg",@"qcow",@"cow",@"cloop",@"vmdk",nil]
              modalForWindow:window modalDelegate:self
              didEndSelector:@selector(openPanelDidEnd:returnCode:contextInfo:) contextInfo:NULL];
    }
    else
    {
        /* or Launch Qemu, with the global args */
        [self startEmulationWithArgc:gArgc argv:gArgv];
    }
}

- (void)applicationWillTerminate:(NSNotification *)aNotification
{
    printf("Application will terminate\n");
    qemu_system_shutdown_request();
    /* In order to avoid a crash */
    exit(0);
}

- (void)openPanelDidEnd:(NSOpenPanel *)sheet returnCode:(int)returnCode contextInfo:(void *)contextInfo
{
    if(returnCode == NSCancelButton)
    {
        exit(0);
    }

    if(returnCode == NSOKButton)
    {
        char *bin = "qemu";
        char *img = (char*)[ [ sheet filename ] cString];

        char **argv = (char**)malloc( sizeof(char*)*3 );

        asprintf(&argv[0], "%s", bin);
        asprintf(&argv[1], "-hda");
        asprintf(&argv[2], "%s", img);

        printf("Using argc %d argv %s -hda %s\n", 3, bin, img);

        [self startEmulationWithArgc:3 argv:(char**)argv];
    }
}

- (void)startEmulationWithArgc:(int)argc argv:(char**)argv
{
    int status;
    /* Launch Qemu */
    printf("starting qemu...\n");
    status = qemu_main (argc, argv);
    exit(status);
}
@end

/*
 ------------------------------------------------------
    Application Creation
 ------------------------------------------------------
*/

/* Dock Connection */
typedef struct CPSProcessSerNum
{
        UInt32                lo;
        UInt32                hi;
} CPSProcessSerNum;

extern OSErr    CPSGetCurrentProcess( CPSProcessSerNum *psn);
extern OSErr    CPSEnableForegroundOperation( CPSProcessSerNum *psn, UInt32 _arg2, UInt32 _arg3, UInt32 _arg4, UInt32 _arg5);
extern OSErr    CPSSetFrontProcess( CPSProcessSerNum *psn);

/* Menu Creation */
static void setApplicationMenu(void)
{
    /* warning: this code is very odd */
    NSMenu *appleMenu;
    NSMenuItem *menuItem;
    NSString *title;
    NSString *appName;

    appName = @"Qemu";
    appleMenu = [[NSMenu alloc] initWithTitle:@""];

    /* Add menu items */
    title = [@"About " stringByAppendingString:appName];
    [appleMenu addItemWithTitle:title action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];

    [appleMenu addItem:[NSMenuItem separatorItem]];

    title = [@"Hide " stringByAppendingString:appName];
    [appleMenu addItemWithTitle:title action:@selector(hide:) keyEquivalent:@"h"];

    menuItem = (NSMenuItem *)[appleMenu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    [menuItem setKeyEquivalentModifierMask:(NSAlternateKeyMask|NSCommandKeyMask)];

    [appleMenu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];

    [appleMenu addItem:[NSMenuItem separatorItem]];

    title = [@"Quit " stringByAppendingString:appName];
    [appleMenu addItemWithTitle:title action:@selector(terminate:) keyEquivalent:@"q"];


    /* Put menu into the menubar */
    menuItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    [menuItem setSubmenu:appleMenu];
    [[NSApp mainMenu] addItem:menuItem];

    /* Tell the application object that this is now the application menu */
    [NSApp setAppleMenu:appleMenu];

    /* Finally give up our references to the objects */
    [appleMenu release];
    [menuItem release];
}

/* Create a window menu */
static void setupWindowMenu(void)
{
    NSMenu      *windowMenu;
    NSMenuItem  *windowMenuItem;
    NSMenuItem  *menuItem;

    windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];

    /* "Minimize" item */
    menuItem = [[NSMenuItem alloc] initWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [windowMenu addItem:menuItem];
    [menuItem release];

    /* Put menu into the menubar */
    windowMenuItem = [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
    [windowMenuItem setSubmenu:windowMenu];
    [[NSApp mainMenu] addItem:windowMenuItem];

    /* Tell the application object that this is now the window menu */
    [NSApp setWindowsMenu:windowMenu];

    /* Finally give up our references to the objects */
    [windowMenu release];
    [windowMenuItem release];
}

static void CustomApplicationMain(void)
{
    NSAutoreleasePool   *pool = [[NSAutoreleasePool alloc] init];
    QemuCocoaGUIController *gui_controller;
    CPSProcessSerNum PSN;

    [NSApplication sharedApplication];

    if (!CPSGetCurrentProcess(&PSN))
        if (!CPSEnableForegroundOperation(&PSN,0x03,0x3C,0x2C,0x1103))
            if (!CPSSetFrontProcess(&PSN))
                [NSApplication sharedApplication];

    /* Set up the menubar */
    [NSApp setMainMenu:[[NSMenu alloc] init]];
    setApplicationMenu();
    setupWindowMenu();

    /* Create SDLMain and make it the app delegate */
    gui_controller = [[QemuCocoaGUIController alloc] init];
    [NSApp setDelegate:gui_controller];

    /* Start the main event loop */
    [NSApp run];

    [gui_controller release];
    [pool release];
}

/* Real main of qemu-cocoa */
int main(int argc, char **argv)
{
    gArgc = argc;
    gArgv = argv;

    CustomApplicationMain();

    return 0;
}
