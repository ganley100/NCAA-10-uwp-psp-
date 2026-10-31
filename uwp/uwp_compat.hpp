// Force-included into host/main.cpp for the UWP build (see uwp/uwp.cmake).
// The native SDL path in main.cpp names sdl:: and psprecomp::hex32 without
// declaring them itself; GCC builds get them transitively, MSVC does not.
#pragma once

#include "psprecomp/common.hpp"
#include "../profile/host/sdl_frontend.hpp"

namespace sdl = pspweb::sdl;
