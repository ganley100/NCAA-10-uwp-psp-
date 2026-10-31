// File-level executable conversion shared by the native CLI and the Wasm export.
#pragma once

#include <filesystem>
#include <string>

namespace pspweb::translator {

// Writes the plain ELF for an encrypted, compressed or already plain PSP
// executable. Returns an empty string on success, otherwise the error.
[[nodiscard]] std::string convert_executable(const std::filesystem::path &input, const std::filesystem::path &output);

} // namespace pspweb::translator
