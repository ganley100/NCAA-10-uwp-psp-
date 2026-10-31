// pspweb_prx_decrypt <EBOOT.BIN> <EBOOT.ELF>: native form of the browser
// translator's executable step, usable as PSP_DECRYPT for scripts/decrypt.sh.
#include "convert.hpp"

#include <iostream>

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Usage: pspweb_prx_decrypt <EBOOT.BIN> <EBOOT.ELF>\n";
        return 2;
    }
    const auto error = pspweb::translator::convert_executable(argv[1], argv[2]);
    if (!error.empty()) {
        std::cerr << error << '\n';
        return 1;
    }
    return 0;
}
