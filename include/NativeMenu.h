#pragma once

// Adds a "File" menu to the macOS menu bar with New (Cmd+N), Open (Cmd+O), and
// Save (Cmd+S) items. GLFW has no cross-platform menu-bar API, so this is
// Cocoa-specific (see NativeMenu.mm) and only built on Apple; call sites
// should guard with #ifdef __APPLE__ if this ever needs to compile elsewhere.
//
// onNew takes no arguments (there's no dialog for New). onSaveWithPath /
// onOpenWithPath are called with the path the user chose in the respective
// dialog.
void SetupNativeFileMenu(void (*onNew)(), void (*onSaveWithPath)(const char* path), void (*onOpenWithPath)(const char* path));

// Trigger the same actions the File menu items use, and invoke the callbacks
// registered via SetupNativeFileMenu. Lets Cmd+N/Cmd+S/Cmd+O (or the Ctrl
// equivalents, once a non-Apple build exists) trigger them directly via
// GLFW's keyCallback without going through the menu.
void TriggerNew();
void TriggerSaveDialog();

// Returns true if the user picked a file (and onOpenWithPath was called), false
// if they canceled the dialog. Doesn't reflect whether the file parsed
// successfully — just whether a file was chosen.
bool TriggerOpenDialog();
