# UWP / Xbox One build glue for the PSPRecomp "web" profile.
#
# Injected with -DCMAKE_PROJECT_INCLUDE=uwp/uwp.cmake (see scripts/build_uwp.ps1)
# so the upstream PSPRecomp and profile CMake files stay untouched.  It:
#   * provides EGL / GLESv2 (ANGLE for UWP) and SDL2::SDL2 (SDL2 WinRT) as
#     imported targets under the names the profile links against;
#   * renames the native main() to pspweb_main() and adds uwp/uwp_main.cpp,
#     whose wWinMain hands the CoreWindow to SDL and then runs pspweb_main.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    message(FATAL_ERROR "uwp.cmake expects -DCMAKE_SYSTEM_NAME=WindowsStore")
endif()

set(PSPWEB_UWP_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(PSPWEB_UWP_SDL2_INCLUDE "" CACHE PATH "SDL2 include directory")
set(PSPWEB_UWP_SDL2_LIB "" CACHE FILEPATH "SDL2 import library built for UWP")
set(PSPWEB_UWP_ANGLE_INCLUDE "" CACHE PATH "ANGLE include directory (EGL/, GLES2/, GLES3/, KHR/)")
set(PSPWEB_UWP_EGL_LIB "" CACHE FILEPATH "ANGLE libEGL import library for UWP")
set(PSPWEB_UWP_GLES_LIB "" CACHE FILEPATH "ANGLE libGLESv2 import library for UWP")
set(PSPWEB_UWP_CPPWINRT_INCLUDE "" CACHE PATH "Windows SDK cppwinrt include directory (optional)")
foreach(var PSPWEB_UWP_SDL2_INCLUDE PSPWEB_UWP_SDL2_LIB PSPWEB_UWP_ANGLE_INCLUDE PSPWEB_UWP_EGL_LIB PSPWEB_UWP_GLES_LIB)
    if(NOT EXISTS "${${var}}")
        message(FATAL_ERROR "${var} is not set or missing: '${${var}}'")
    endif()
endforeach()

# Generated AOT units are very large; UTF-8 sources throughout.
add_compile_options(/bigobj /utf-8)
add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN
    # ANGLE.WindowsStore's GLES 3 headers only declare entry points with this,
    # and its EGL headers predate EGL_KHR_no_config_context.
    GL_GLEXT_PROTOTYPES "EGL_NO_CONFIG_KHR=((EGLConfig)0)")

add_library(EGL UNKNOWN IMPORTED GLOBAL)
set_target_properties(EGL PROPERTIES IMPORTED_LOCATION "${PSPWEB_UWP_EGL_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${PSPWEB_UWP_ANGLE_INCLUDE}")
add_library(GLESv2 UNKNOWN IMPORTED GLOBAL)
set_target_properties(GLESv2 PROPERTIES IMPORTED_LOCATION "${PSPWEB_UWP_GLES_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${PSPWEB_UWP_ANGLE_INCLUDE}")
add_library(SDL2::SDL2 UNKNOWN IMPORTED GLOBAL)
set_target_properties(SDL2::SDL2 PROPERTIES IMPORTED_LOCATION "${PSPWEB_UWP_SDL2_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${PSPWEB_UWP_SDL2_INCLUDE}")
# find_package(SDL2 REQUIRED) in the profile resolves to uwp/cmake/SDL2Config.cmake.
set(SDL2_DIR "${PSPWEB_UWP_DIR}/cmake" CACHE PATH "" FORCE)

function(pspweb_uwp_finish)
    if(NOT TARGET pspweb)
        message(FATAL_ERROR "uwp.cmake: the profile did not define the pspweb target")
    endif()
    get_target_property(profile_dir pspweb SOURCE_DIR)
    # The profile's GCC-style warning flags are not valid cl options.
    get_target_property(options pspweb COMPILE_OPTIONS)
    if(options)
        list(REMOVE_ITEM options -Wall -Wextra)
        set_target_properties(pspweb PROPERTIES COMPILE_OPTIONS "${options}")
    endif()
    set_source_files_properties("${profile_dir}/host/main.cpp" TARGET_DIRECTORY pspweb
        PROPERTIES COMPILE_DEFINITIONS "main=pspweb_main"
                   COMPILE_OPTIONS "/FI${PSPWEB_UWP_DIR}/uwp_compat.hpp")
    target_sources(pspweb PRIVATE "${PSPWEB_UWP_DIR}/uwp_main.cpp" "${PSPWEB_UWP_DIR}/launcher.cpp"
        "${PSPWEB_UWP_DIR}/iso9660.cpp")
    # The launcher's file picker and USB scan use C++/WinRT from the Windows SDK.
    if(PSPWEB_UWP_CPPWINRT_INCLUDE)
        target_include_directories(pspweb PRIVATE "${PSPWEB_UWP_CPPWINRT_INCLUDE}")
    endif()
    target_compile_definitions(pspweb PRIVATE PSPWEB_UWP=1)
    # Plain ISO C++ (no C++/CX); the WinRT parts live inside SDL.
    set_target_properties(pspweb PROPERTIES VS_WINRT_COMPONENT OFF WIN32_EXECUTABLE ON)
    target_link_libraries(pspweb PRIVATE runtimeobject.lib)
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL pspweb_uwp_finish)
