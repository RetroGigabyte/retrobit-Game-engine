#import <Cocoa/Cocoa.h>
#include "NativeMenu.h"

typedef void (*PathCallback)(const char* path);
typedef void (*VoidCallback)();
static VoidCallback g_newCallback = nullptr;
static PathCallback g_saveCallback = nullptr;
static PathCallback g_openCallback = nullptr;

void TriggerNew() {
    if (g_newCallback) g_newCallback();
}

void TriggerSaveDialog() {
    if (!g_saveCallback) return;

    NSSavePanel* panel = [NSSavePanel savePanel];
    [panel setNameFieldStringValue:@"level.retrobitl"];
    [panel setAllowsOtherFileTypes:YES];
    [panel setExtensionHidden:NO];

    NSModalResponse result = [panel runModal];
    if (result == NSModalResponseOK) {
        NSURL* url = [panel URL];
        if (url != nil) {
            g_saveCallback([[url path] UTF8String]);
        }
    }
}

bool TriggerOpenDialog() {
    if (!g_openCallback) return false;

    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];

    NSModalResponse result = [panel runModal];
    if (result == NSModalResponseOK) {
        NSURL* url = [[panel URLs] firstObject];
        if (url != nil) {
            g_openCallback([[url path] UTF8String]);
            return true;
        }
    }
    return false;
}

@interface RetrobitMenuHandler : NSObject
- (void)newAction:(id)sender;
- (void)saveAction:(id)sender;
- (void)openAction:(id)sender;
@end

@implementation RetrobitMenuHandler
- (void)newAction:(id)sender {
    TriggerNew();
}
- (void)saveAction:(id)sender {
    TriggerSaveDialog();
}
- (void)openAction:(id)sender {
    TriggerOpenDialog();
}
@end

// Kept alive for the process lifetime; NSMenuItem only holds a weak/unretained target.
static RetrobitMenuHandler* g_menuHandler = nil;

void SetupNativeFileMenu(void (*onNew)(), void (*onSaveWithPath)(const char* path), void (*onOpenWithPath)(const char* path)) {
    g_newCallback = onNew;
    g_saveCallback = onSaveWithPath;
    g_openCallback = onOpenWithPath;
    g_menuHandler = [[RetrobitMenuHandler alloc] init];

    NSMenu* mainMenu = [NSApp mainMenu];
    if (mainMenu == nil) {
        mainMenu = [[NSMenu alloc] init];
        [NSApp setMainMenu:mainMenu];
    }

    NSMenuItem* fileMenuItem = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
    NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    [fileMenuItem setSubmenu:fileMenu];

    NSMenuItem* newItem = [[NSMenuItem alloc] initWithTitle:@"New"
                                                      action:@selector(newAction:)
                                               keyEquivalent:@"n"];
    [newItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
    [newItem setTarget:g_menuHandler];
    [fileMenu addItem:newItem];

    NSMenuItem* openItem = [[NSMenuItem alloc] initWithTitle:@"Open…"
                                                       action:@selector(openAction:)
                                                keyEquivalent:@"o"];
    [openItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
    [openItem setTarget:g_menuHandler];
    [fileMenu addItem:openItem];

    NSMenuItem* saveItem = [[NSMenuItem alloc] initWithTitle:@"Save…"
                                                       action:@selector(saveAction:)
                                                keyEquivalent:@"s"];
    [saveItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
    [saveItem setTarget:g_menuHandler];
    [fileMenu addItem:saveItem];

    [mainMenu addItem:fileMenuItem];
}
