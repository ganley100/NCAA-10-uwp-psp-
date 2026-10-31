# SDL2 for UWP is provided as an imported target by uwp/uwp.cmake.
if(NOT TARGET SDL2::SDL2)
    set(SDL2_FOUND FALSE)
    set(SDL2_NOT_FOUND_MESSAGE "SDL2::SDL2 must be defined by uwp/uwp.cmake")
    return()
endif()
set(SDL2_FOUND TRUE)
set(SDL2_LIBRARIES SDL2::SDL2)
