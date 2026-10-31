// Checks uwp/disc_image.cpp against a reference ISO:
//   disc_image_test <reference.iso> <image.iso|cso|chd>...
// Every image must read back byte-identical to the reference (in odd-sized,
// unaligned pieces), list the same ISO9660 tree and report DISC_ID ULUS10440.
#include "disc_image.hpp"

#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using pspweb::uwp::DiscImage;

namespace {

std::vector<std::uint8_t> slurp(const char *path) {
    std::vector<std::uint8_t> data;
    if (std::FILE *f = std::fopen(path, "rb")) {
        std::uint8_t buffer[1 << 16];
        for (std::size_t n; (n = std::fread(buffer, 1, sizeof buffer, f)) > 0;) data.insert(data.end(), buffer, buffer + n);
        std::fclose(f);
    }
    return data;
}

int check(const std::vector<std::uint8_t> &reference, const char *path) {
    std::string error;
    auto image = DiscImage::open(std::fopen(path, "rb"), path, error);
    if (!image) {
        std::printf("FAIL %s: %s\n", path, error.c_str());
        return 1;
    }
    if (image->size() < reference.size() ||
        image->size() > reference.size() + 2048u * 64u) { // CHD may pad to whole hunks
        std::printf("FAIL %s: size %llu, reference %zu\n", path, static_cast<unsigned long long>(image->size()),
                    reference.size());
        return 1;
    }
    std::mt19937 rng(10440);
    std::vector<std::uint8_t> piece;
    for (std::size_t offset = 0; offset < reference.size();) {
        const std::size_t length = std::min<std::size_t>(1u + rng() % 70000u, reference.size() - offset);
        piece.resize(length);
        if (!image->read(offset, piece.data(), length) || std::memcmp(piece.data(), &reference[offset], length) != 0) {
            std::printf("FAIL %s: bytes differ at %zu (+%zu)\n", path, offset, length);
            return 1;
        }
        offset += length;
    }
    std::uint8_t probe;
    if (image->read(image->size(), &probe, 1)) {
        std::printf("FAIL %s: read past the end succeeded\n", path);
        return 1;
    }
    std::vector<pspweb::uwp::IsoEntry> entries;
    error = pspweb::uwp::list_iso(*image, entries);
    const std::string id = pspweb::uwp::read_disc_id(*image, entries);
    if (!error.empty() || id != "ULUS10440" || entries.size() != 87u) {
        std::printf("FAIL %s: list '%s', %zu entries, disc id '%s'\n", path, error.c_str(), entries.size(), id.c_str());
        return 1;
    }
    std::printf("ok   %s: %llu bytes, %zu entries, %s\n", path, static_cast<unsigned long long>(image->size()),
                entries.size(), id.c_str());
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    const auto reference = slurp(argv[1]);
    int failures = 0;
    for (int i = 2; i < argc; ++i) failures += check(reference, argv[i]);
    // This executable is not a disc: listing it must fail cleanly.
    std::string error;
    std::vector<pspweb::uwp::IsoEntry> none;
    auto not_a_disc = DiscImage::open(std::fopen(argv[0], "rb"), "x.iso", error);
    if (!not_a_disc || pspweb::uwp::list_iso(*not_a_disc, none).empty()) {
        std::printf("FAIL: a non-ISO file listed as a disc\n");
        ++failures;
    }
    return failures == 0 ? 0 : 1;
}
