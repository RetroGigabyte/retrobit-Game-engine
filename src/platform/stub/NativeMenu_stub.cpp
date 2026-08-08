#include "NativeMenu.h"

// No native menu bar / file dialogs implemented for this platform yet — see
// src/platform/macos/NativeMenu.mm for the real (Cocoa) implementation.
//
// These are harmless no-ops rather than missing symbols specifically so
// call sites (main.cpp) don't need #ifdef guards: the platform-selection
// pattern is that CMakeLists.txt picks which src/platform/<platform>/ file
// to compile, and every platform provides the same functions, so the rest
// of the engine can call them unconditionally.
void SetupNativeFileMenu(void (*)(), void (*)(const char* path), void (*)(const char* path)) {}
void TriggerNew() {}
void TriggerSaveDialog() {}
bool TriggerOpenDialog() { return false; }
