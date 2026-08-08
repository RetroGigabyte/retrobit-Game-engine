#pragma once

// Selects the right OpenGL header per platform, so main.cpp/Shader.cpp don't
// need to know which one they're on. macOS exposes core-profile entry points
// directly (no loader needed). Other platforms need a loader (e.g. GLAD)
// vendored into third_party/ before this can compile there — the #error below
// marks that as a known, explicit gap rather than letting it fail confusingly
// deep in a header somewhere.
#if defined(__APPLE__)
    #include <OpenGL/gl3.h>
#elif defined(_WIN32) || defined(__linux__)
    #error "RETRObit's OpenGL loading isn't implemented for this platform yet. \
Vendor a loader (e.g. GLAD) into third_party/, wire it into CMakeLists.txt, and \
include it here instead of this #error."
#else
    #error "Unsupported platform"
#endif
