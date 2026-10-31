#include "convert.hpp"

#include "prx_decrypt.hpp"

#include <fstream>
#include <iterator>
#include <vector>

namespace pspweb::translator {

std::string convert_executable(const std::filesystem::path &input, const std::filesystem::path &output) {
    try {
        std::ifstream in(input, std::ios::binary);
        if (!in) return "Cannot open " + input.string();
        const std::vector<std::uint8_t> file{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        const auto elf = executable_to_elf(file);
        std::ofstream out(output, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(elf.data()), static_cast<std::streamsize>(elf.size()));
        if (!out) return "Cannot write " + output.string();
        return {};
    } catch (const std::exception &error) {
        return error.what();
    }
}

} // namespace pspweb::translator
