// Force-included into host/main.cpp for the UWP build (see uwp/uwp.cmake).
// The native SDL path in main.cpp names sdl:: and psprecomp::hex32 without
// declaring them itself; GCC builds get them transitively, MSVC does not.
#pragma once

#include "psprecomp/common.hpp"

// main.cpp includes sdl_frontend.hpp itself; only the short name is missing.
namespace pspweb::sdl {}
namespace sdl = pspweb::sdl;
