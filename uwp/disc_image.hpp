// Random access to a PSP disc image: plain ISO, CSO (CISO v1, deflate) or
// CHD (chdman createdvd, or createcd with 2048-byte data sectors).
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace pspweb::uwp {

class DiscImage {
public:
    virtual ~DiscImage() = default;
    // Logical ISO9660 size in bytes.
    [[nodiscard]] virtual std::uint64_t size() const = 0;
    // Reads exactly `length` bytes at `offset` of the logical image.
    virtual bool read(std::uint64_t offset, void *dst, std::size_t length) = 0;

    // Takes ownership of `file` (closed on failure as well).  `name` picks
    // the format by extension; `error` explains a null result.
    static std::unique_ptr<DiscImage> open(std::FILE *file, const std::string &name, std::string &error);
};

struct IsoEntry {
    std::string path;        // relative to the disc root, '/' separated
    std::uint64_t offset{};  // byte offset in the image
    std::uint64_t size{};
    bool directory{};
};

// Walks the ISO9660 tree.  Returns an empty string on success.
std::string list_iso(DiscImage &image, std::vector<IsoEntry> &entries);

// DISC_ID from PSP_GAME/PARAM.SFO, or an empty string.
std::string read_disc_id(DiscImage &image, const std::vector<IsoEntry> &entries);

} // namespace pspweb::uwp
