// C entry points for the browser translator module. The PSPRecomp code
// generator is linked unchanged and driven through callMain().
#include "convert.hpp"

#include <emscripten/emscripten.h>

#include <algorithm>
#include <cstring>

extern "C" EMSCRIPTEN_KEEPALIVE int pspweb_convert_executable(const char *input, const char *output, char *error, int error_size) {
    const auto message = pspweb::translator::convert_executable(input, output);
    if (message.empty()) return 0;
    if (error_size > 0) {
        const auto length = std::min<std::size_t>(message.size(), static_cast<std::size_t>(error_size - 1));
        std::memcpy(error, message.data(), length);
        error[length] = '\0';
    }
    return 1;
}
