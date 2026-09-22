#pragma once

// Product version for the native WinUI executable and its VERSIONINFO resource.
// Keep MAJOR/MINOR/PATCH synchronized with project(VelocityCopy VERSION ...)
// in the root CMakeLists.txt. The packaging workflow derives DISPLAY_VERSION
// from these macros so the installed-app version cannot silently diverge.
#define VELOCITYCOPY_VERSION_MAJOR 1
#define VELOCITYCOPY_VERSION_MINOR 0
#define VELOCITYCOPY_VERSION_PATCH 0
#define VELOCITYCOPY_VERSION_BUILD 0

#define VELOCITYCOPY_VERSION_COMMA \
    VELOCITYCOPY_VERSION_MAJOR,VELOCITYCOPY_VERSION_MINOR,VELOCITYCOPY_VERSION_PATCH,VELOCITYCOPY_VERSION_BUILD
#define VELOCITYCOPY_VERSION_STRING "1.0.0.0"
