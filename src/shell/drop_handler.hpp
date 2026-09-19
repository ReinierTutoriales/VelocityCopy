#pragma once
#include <windows.h>

// A new identity deliberately does not activate any of the retired verb classes.
inline constexpr CLSID CLSID_VelocityCopyDropHandler =
    {0x6bd80c35, 0x7ce8, 0x4a63, {0x92, 0xd4, 0x51, 0xaf, 0x4d, 0xac, 0xb8, 0x21}};
