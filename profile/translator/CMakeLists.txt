# Game-free browser translator: the unchanged PSPRecomp code generator plus
# executable decryption, built as one WebAssembly module with Emscripten, or
# natively as pspweb_prx_decrypt and its tests.
cmake_minimum_required(VERSION 3.20)
project(PspWebTranslator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PSPRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../PSPRecomp" CACHE PATH "Patched PSPRecomp checkout from scripts/setup.sh")
if(NOT EXISTS "${PSPRECOMP_DIR}/tools/codegen_main.cpp")
    message(FATAL_ERROR "PSPRecomp not found at ${PSPRECOMP_DIR}; run scripts/setup.sh first")
endif()
set(PSPRECOMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PSPRECOMP_PROFILE "" CACHE STRING "" FORCE)
add_subdirectory("${PSPRECOMP_DIR}" psprecomp EXCLUDE_FROM_ALL)

if(EMSCRIPTEN)
    set(PSPWEB_TRANSLATOR_EXCEPTIONS -fwasm-exceptions)
endif()

add_library(pspweb_translator_core STATIC crypto.cpp prx_decrypt.cpp convert.cpp)
target_include_directories(pspweb_translator_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pspweb_translator_core PUBLIC psprecomp_core)
target_compile_options(pspweb_translator_core PRIVATE -Wall -Wextra ${PSPWEB_TRANSLATOR_EXCEPTIONS})

if(EMSCRIPTEN)
    target_compile_options(psprecomp_core PRIVATE ${PSPWEB_TRANSLATOR_EXCEPTIONS})
    add_executable(pspweb_translator "${PSPRECOMP_DIR}/tools/codegen_main.cpp" wasm_exports.cpp)
    target_link_libraries(pspweb_translator PRIVATE pspweb_translator_core)
    target_compile_options(pspweb_translator PRIVATE ${PSPWEB_TRANSLATOR_EXCEPTIONS})
    target_link_options(pspweb_translator PRIVATE
        ${PSPWEB_TRANSLATOR_EXCEPTIONS}
        -sMODULARIZE=1 -sEXPORT_NAME=createPspTranslator
        -sENVIRONMENT=web,worker,node -sINVOKE_RUN=0 -sEXIT_RUNTIME=0
        -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=4GB -sSTACK_SIZE=16MB
        -sFORCE_FILESYSTEM=1
        -sEXPORTED_RUNTIME_METHODS=FS,callMain,ccall,UTF8ToString
        -sEXPORTED_FUNCTIONS=_main,_pspweb_convert_executable,_malloc,_free)
else()
    add_executable(pspweb_prx_decrypt prx_decrypt_main.cpp)
    target_link_libraries(pspweb_prx_decrypt PRIVATE pspweb_translator_core)
    add_executable(pspweb_translator_tests tests.cpp)
    target_link_libraries(pspweb_translator_tests PRIVATE pspweb_translator_core)
    enable_testing()
    add_test(NAME pspweb_translator_tests COMMAND pspweb_translator_tests)
endif()
