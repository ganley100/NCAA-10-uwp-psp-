// Writes the plain ISO behind a .chd / .cso / .iso image, with the same
// reader the console uses (uwp/disc_image.cpp):
//   disc_to_iso <image.chd|cso|iso> <out.iso>
// The build needs an ISO to verify the disc and translate its audio programs.
#include "disc_image.hpp"

#include <cstdio>
#include <vector>

int main(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: disc_to_iso <image.chd|cso|iso> <out.iso>\n");
        return 2;
    }
    std::string error;
    auto image = pspweb::uwp::DiscImage::open(std::fopen(argv[1], "rb"), argv[1], error);
    if (!image) {
        std::fprintf(stderr, "%s: %s\n", argv[1], error.c_str());
        return 1;
    }
    // CHDs round up to whole hunks; the ISO9660 volume size is the real end.
    std::vector<unsigned char> pvd(2048);
    std::uint64_t size = image->size();
    if (image->read(16u * 2048u, pvd.data(), pvd.size())) {
        const std::uint64_t blocks = pvd[80] | (pvd[81] << 8u) | (pvd[82] << 16u) | (static_cast<std::uint64_t>(pvd[83]) << 24u);
        if (blocks > 16u && blocks * 2048u <= size) size = blocks * 2048u;
    }
    std::FILE *out = std::fopen(argv[2], "wb");
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    std::vector<unsigned char> buffer(1u << 20u);
    for (std::uint64_t offset = 0; offset < size;) {
        const auto chunk = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), size - offset));
        if (!image->read(offset, buffer.data(), chunk) || std::fwrite(buffer.data(), 1, chunk, out) != chunk) {
            std::fprintf(stderr, "read/write failed at %llu\n", static_cast<unsigned long long>(offset));
            std::fclose(out);
            return 1;
        }
        offset += chunk;
    }
    std::fclose(out);
    std::printf("wrote %s (%llu bytes)\n", argv[2], static_cast<unsigned long long>(size));
    return 0;
}
