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
#include "vl.h"
#import <Cocoa/Cocoa.h>

NSWindow *window = NULL;
NSQuickDrawView *qd_view = NULL;


int gArgc;
char **gArgv;
DisplayState current_ds;

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
    
    current_ds = *ds;
}

/*
 ------------------------------------------------------
    keymap conversion
 ------------------------------------------------------
*/

static int keymap[] =
{
    30, //'a' 0x0
    31,  //'s'
    32,  //'d'
    33,  //'f'
    35,  //'h'
    34,  //'g'
    44,  //'z'
    45,  //'x'
    46,  //'c'
    47,  //'v'
    0,   // 0  0x0a
    48,  //'b'
    16,  //'q'
    17,  //'w'
    18,  //'e'
    19,  //'r' 
    21,  //'y' 0x10
    20,  //'t'
    2,  //'1'
    3,  //'2'
    4,  //'3'
    5,  //'4'
    7,  //'6'
    6,  //'5'
    0,  //'='
    10,  //'9'
    8,  //'7' 0x1A
    0,  //'-' 
    9,  //'8' 
    11,  //'0' 
    27,  //']' 
    24,  //'o' 
    22,  //'u' 0x20
    26,  //'['
    23,  //'i'
    25,  //'p'
    28,  //'\n'
    38,  //'l'
    36,  //'j'
    40,  //'"'
    37,  //'k'
    39,  //';'
    15,  //'\t' 0x30
    0,  //' '
    0,  //'`'
    14,  //'<backspace>'
    0,  //'' 0x34
    0,  //'<esc>'
    0,  //'<esc>'
    /* Not completed to finish see http://www.libsdl.org/cgi/cvsweb.cgi/SDL12/src/video/quartz/SDL_QuartzKeys.h?rev=1.6&content-type=text/x-cvsweb-markup */
};

static int cocoa_keycode_to_qemu(int keycode)
{
    if(sizeof(keymap) <= keycode)
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
    int grab = 1;
    
    pool = [ [ NSAutoreleasePool alloc ] init ];
    distantPast = [ NSDate distantPast ];
    
    if (is_active_console(vga_console)) 
        vga_update_display();
    do {
        event = [ NSApp nextEventMatchingMask:NSAnyEventMask untilDate:distantPast
                        inMode: NSDefaultRunLoopMode dequeue:YES ];
        if (event != nil) {
            switch ([event type]) {
                case NSKeyDown:
                    if(grab)
                    {
                        int keycode = cocoa_keycode_to_qemu([event keyCode]);
                        
                        if (keycode & 0x80)
                            kbd_put_keycode(0xe0);
                        kbd_put_keycode(keycode & 0x7f);
                    }
                    break;
                case NSKeyUp:
                    if(grab)
                    {
                        int keycode = cocoa_keycode_to_qemu([event keyCode]);

                        if (keycode & 0x80)
                            kbd_put_keycode(0xe0);
                        kbd_put_keycode(keycode | 0x80);
                    }
                    break;
                case NSScrollWheel:
                
                case NSLeftMouseDown:
                case NSLeftMouseUp:
                
                case NSOtherMouseDown:
                case NSRightMouseDown:
                
                case NSOtherMouseUp:
                case NSRightMouseUp:
                
                case NSMouseMoved:
                case NSOtherMouseDragged:
                case NSRightMouseDragged:
                case NSLeftMouseDragged:
                
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
    
    /* Do whatever we want here : set up a pc list... */
    {
        NSOpenPanel *op = [[NSOpenPanel alloc] init];
        
        cocoa_resize(&current_ds, 640, 400);
        
        [op setPrompt:@"Boot image"];
        
        [op setMessage:@"Select the disk image you want to boot.\n\nHit the \"Cancel\" button to quit"];
        
        [op beginSheetForDirectory:nil file:nil types:[NSArray arrayWithObjects:@"img",@"iso",nil]
              modalForWindow:window modalDelegate:self
              didEndSelector:@selector(openPanelDidEnd:returnCode:contextInfo:) contextInfo:NULL];
    }
    
    /* or Launch Qemu, with the global args */
    //[self startEmulationWithArgc:gArgc argv:gArgv];
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

static void CustomApplicationMain (argc, argv)
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
    
    CustomApplicationMain (argc, argv);
    
    return 0;
}
