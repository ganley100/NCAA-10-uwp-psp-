#include "iso9660.hpp"

#include <cstdio>
#include <deque>
#include <fstream>
#include <memory>
#include <vector>

namespace pspweb::uwp {
namespace {

constexpr std::uint64_t kSector = 2048u;

std::uint32_t le32(const std::uint8_t *p) {
    return p[0] | (p[1] << 8u) | (p[2] << 16u) | (static_cast<std::uint32_t>(p[3]) << 24u);
}

struct FileCloser {
    void operator()(std::FILE *f) const { std::fclose(f); }
};
using File = std::unique_ptr<std::FILE, FileCloser>;

File open(const std::filesystem::path &path, const char *mode) {
#if defined(_WIN32)
    return File(_wfopen(path.c_str(), mode[0] == 'r' ? L"rb" : L"wb"));
#else
    return File(std::fopen(path.c_str(), mode));
#endif
}

bool seek(std::FILE *f, std::uint64_t offset) {
#if defined(_WIN32)
    return _fseeki64(f, static_cast<long long>(offset), SEEK_SET) == 0;
#else
    return fseeko(f, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

bool read_at(std::FILE *f, std::uint64_t offset, std::vector<std::uint8_t> &data) {
    return seek(f, offset) && std::fread(data.data(), 1, data.size(), f) == data.size();
}

struct Entry {
    std::string path;
    std::uint32_t lba;
    std::uint32_t size;
};

} // namespace

std::string extract_iso(const std::filesystem::path &iso, const std::filesystem::path &out,
                        ExtractProgress &progress) {
    namespace fs = std::filesystem;
    File in = open(iso, "rb");
    if (!in) return "cannot open the ISO";
    std::vector<std::uint8_t> pvd(kSector);
    if (!read_at(in.get(), 16u * kSector, pvd) || pvd[0] != 1u || std::string(pvd.begin() + 1, pvd.begin() + 6) != "CD001")
        return "not an ISO9660 disc image (ZIP, CSO and CHD are not supported)";

    std::vector<Entry> files;
    std::deque<Entry> dirs{{"", le32(&pvd[156 + 2]), le32(&pvd[156 + 10])}};
    std::size_t visited = 0;
    while (!dirs.empty()) {
        const Entry dir = dirs.front();
        dirs.pop_front();
        if (++visited > 100000u || dir.size > 64u * 1024u * 1024u) return "corrupt directory tree";
        std::vector<std::uint8_t> data(dir.size);
        if (!read_at(in.get(), dir.lba * kSector, data)) return "truncated directory";
        for (std::size_t i = 0; i < data.size();) {
            const std::size_t length = data[i];
            if (length == 0u) {
                i = (i / kSector + 1u) * kSector; // records never straddle sectors
                continue;
            }
            if (length < 34u || i + length > data.size()) return "corrupt directory record";
            const std::uint8_t *record = &data[i];
            i += length;
            const std::size_t name_length = record[32];
            if (33u + name_length > length) return "corrupt directory record";
            std::string name(reinterpret_cast<const char *>(record + 33), name_length);
            if (name.size() == 1u && (name[0] == '\0' || name[0] == '\1')) continue; // "." and ".."
            if (const auto semicolon = name.find(';'); semicolon != std::string::npos) name.resize(semicolon);
            if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\:") != std::string::npos)
                return "unsafe file name in the ISO";
            Entry entry{dir.path.empty() ? name : dir.path + "/" + name, le32(record + 2), le32(record + 10)};
            if (record[25] & 2u) dirs.push_back(entry);
            else files.push_back(entry);
        }
    }

    std::uint64_t total = 0;
    for (const auto &file : files) total += file.size;
    progress.total = total;
    progress.done = 0;
    std::vector<std::uint8_t> buffer(1u << 20u);
    std::error_code error;
    for (const auto &file : files) {
        const fs::path target = out / fs::path(file.path); // ISO9660 names are ASCII
        fs::create_directories(target.parent_path(), error);
        File dst = open(target, "wb");
        if (!dst) return "cannot write " + file.path;
        if (!seek(in.get(), file.lba * kSector)) return "truncated ISO at " + file.path;
        for (std::uint64_t left = file.size; left > 0u;) {
            const auto chunk = static_cast<std::size_t>(std::min<std::uint64_t>(left, buffer.size()));
            if (std::fread(buffer.data(), 1, chunk, in.get()) != chunk) return "truncated ISO at " + file.path;
            if (std::fwrite(buffer.data(), 1, chunk, dst.get()) != chunk) return "disk full while writing " + file.path;
            left -= chunk;
            progress.done += chunk;
        }
    }
    return {};
}

std::string read_disc_id(const std::filesystem::path &disc) {
    std::ifstream in(disc / "PSP_GAME" / "PARAM.SFO", std::ios::binary);
    std::vector<std::uint8_t> sfo((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (sfo.size() < 20u || le32(&sfo[0]) != 0x46535000u) return {};
    const std::uint32_t keys = le32(&sfo[8]), values = le32(&sfo[12]), count = le32(&sfo[16]);
    for (std::uint32_t i = 0; i < count && 20u + i * 16u + 16u <= sfo.size(); ++i) {
        const std::uint8_t *entry = &sfo[20u + i * 16u];
        const std::uint32_t key = keys + (entry[0] | (entry[1] << 8u));
        const std::uint32_t length = le32(entry + 4), offset = values + le32(entry + 12);
        if (key >= sfo.size() || offset + length > sfo.size()) continue;
        if (std::string(reinterpret_cast<const char *>(&sfo[key])) != "DISC_ID") continue;
        std::string id(reinterpret_cast<const char *>(&sfo[offset]), length);
        id.resize(id.find('\0') == std::string::npos ? id.size() : id.find('\0'));
        return id;
    }
    return {};
}

} // namespace pspweb::uwp
