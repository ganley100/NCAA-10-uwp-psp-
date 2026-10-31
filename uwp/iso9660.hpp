// Minimal ISO9660 extraction for importing a PSP disc image on the console.
// Mirrors scripts/extract_iso.py: plain ISO9660 names, ";1" versions dropped.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

namespace pspweb::uwp {

struct ExtractProgress {
    std::atomic<std::uint64_t> done{0};  // bytes written so far
    std::atomic<std::uint64_t> total{0}; // bytes to write (known after the directory walk)
};

// Extracts every file in `iso` below `out`.  Returns an empty string on
// success, otherwise a short description of what went wrong.
std::string extract_iso(const std::filesystem::path &iso, const std::filesystem::path &out,
                        ExtractProgress &progress);

// DISC_ID from <disc>/PSP_GAME/PARAM.SFO, or an empty string.
std::string read_disc_id(const std::filesystem::path &disc);

} // namespace pspweb::uwp
