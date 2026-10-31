#include "disc_image.hpp"

#include <libchdr/chd.h>
#include <miniz.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <deque>

namespace pspweb::uwp {
namespace {

constexpr std::uint64_t kSector = 2048u;

std::uint32_t le32(const std::uint8_t *p) {
    return p[0] | (p[1] << 8u) | (p[2] << 16u) | (static_cast<std::uint32_t>(p[3]) << 24u);
}

std::uint64_t le64(const std::uint8_t *p) { return le32(p) | (static_cast<std::uint64_t>(le32(p + 4)) << 32u); }

bool seek(std::FILE *f, std::uint64_t offset) {
#if defined(_WIN32)
    return _fseeki64(f, static_cast<long long>(offset), SEEK_SET) == 0;
#else
    return fseeko(f, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

bool read_at(std::FILE *f, std::uint64_t offset, void *dst, std::size_t length) {
    return seek(f, offset) && std::fread(dst, 1, length, f) == length;
}

std::uint64_t file_size(std::FILE *f) {
#if defined(_WIN32)
    if (_fseeki64(f, 0, SEEK_END) != 0) return 0;
    return static_cast<std::uint64_t>(_ftelli64(f));
#else
    if (fseeko(f, 0, SEEK_END) != 0) return 0;
    return static_cast<std::uint64_t>(ftello(f));
#endif
}

struct FileCloser {
    void operator()(std::FILE *f) const {
        if (f) std::fclose(f);
    }
};
using File = std::unique_ptr<std::FILE, FileCloser>;

// Serves fixed-size blocks through a one-block cache.
class BlockImage : public DiscImage {
public:
    bool read(std::uint64_t offset, void *dst, std::size_t length) override {
        if (offset + length > size_) return false;
        auto *out = static_cast<std::uint8_t *>(dst);
        while (length > 0u) {
            const std::uint64_t block = offset / block_size_;
            if (block != cached_) {
                if (!load(block, cache_)) return false;
                cached_ = block;
            }
            const auto within = static_cast<std::size_t>(offset % block_size_);
            const std::size_t take = std::min<std::size_t>(length, block_size_ - within);
            std::memcpy(out, cache_.data() + within, take);
            out += take;
            offset += take;
            length -= take;
        }
        return true;
    }
    [[nodiscard]] std::uint64_t size() const override { return size_; }

protected:
    virtual bool load(std::uint64_t block, std::vector<std::uint8_t> &out) = 0;
    std::uint64_t size_{};
    std::size_t block_size_{};
    std::vector<std::uint8_t> cache_;
    std::uint64_t cached_{~0ull};
};

class IsoImage final : public DiscImage {
public:
    explicit IsoImage(File file) : file_(std::move(file)), size_(file_size(file_.get())) {}
    [[nodiscard]] std::uint64_t size() const override { return size_; }
    bool read(std::uint64_t offset, void *dst, std::size_t length) override {
        return offset + length <= size_ && read_at(file_.get(), offset, dst, length);
    }

private:
    File file_;
    std::uint64_t size_;
};

// CISO v1: 24-byte header, then one little-endian index word per block (plus
// one end marker).  Bit 31 marks a stored block; the rest, shifted by the
// header's alignment, is the block's file offset.  Others are raw deflate.
class CsoImage final : public BlockImage {
public:
    static std::unique_ptr<DiscImage> open(File file, std::string &error) {
        std::uint8_t header[24]{};
        if (!read_at(file.get(), 0, header, sizeof header) || std::memcmp(header, "CISO", 4) != 0) {
            error = std::memcmp(header, "ZISO", 4) == 0 ? "ZSO (LZ4) images are not supported; use ISO, CSO or CHD"
                                                         : "not a CSO image";
            return nullptr;
        }
        auto image = std::unique_ptr<CsoImage>(new CsoImage(std::move(file)));
        image->size_ = le64(header + 8);
        image->block_size_ = le32(header + 16);
        image->align_ = header[21];
        if (header[20] > 1u) {
            error = "CSO version 2 is not supported; recompress as CSO v1, ISO or CHD";
            return nullptr;
        }
        if (image->block_size_ < kSector || image->block_size_ > (1u << 20u) || image->size_ == 0u) {
            error = "corrupt CSO header";
            return nullptr;
        }
        const std::uint64_t blocks = (image->size_ + image->block_size_ - 1u) / image->block_size_;
        std::vector<std::uint8_t> raw((blocks + 1u) * 4u);
        if (!read_at(image->file_.get(), 24, raw.data(), raw.size())) {
            error = "truncated CSO index";
            return nullptr;
        }
        image->index_.resize(blocks + 1u);
        for (std::uint64_t i = 0; i <= blocks; ++i) image->index_[i] = le32(&raw[i * 4u]);
        image->cache_.resize(image->block_size_);
        return image;
    }

private:
    explicit CsoImage(File file) : file_(std::move(file)) {}

    bool load(std::uint64_t block, std::vector<std::uint8_t> &out) override {
        const std::uint32_t entry = index_[block], next = index_[block + 1u];
        const std::uint64_t start = static_cast<std::uint64_t>(entry & 0x7FFFFFFFu) << align_;
        const std::uint64_t end = static_cast<std::uint64_t>(next & 0x7FFFFFFFu) << align_;
        if (end < start) return false;
        if (entry & 0x80000000u) { // stored; the image's last block may be short
            const auto stored = static_cast<std::size_t>(std::min<std::uint64_t>(end - start, block_size_));
            std::fill(out.begin() + static_cast<std::ptrdiff_t>(stored), out.end(), 0u);
            return read_at(file_.get(), start, out.data(), stored);
        }
        packed_.resize(static_cast<std::size_t>(end - start));
        if (!read_at(file_.get(), start, packed_.data(), packed_.size())) return false;
        mz_stream stream{};
        if (mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) return false;
        stream.next_in = packed_.data();
        stream.avail_in = static_cast<unsigned>(packed_.size());
        stream.next_out = out.data();
        stream.avail_out = static_cast<unsigned>(block_size_);
        const int status = mz_inflate(&stream, MZ_FINISH);
        const bool complete = (status == MZ_STREAM_END || status == MZ_OK || status == MZ_BUF_ERROR) &&
                              stream.avail_out == 0u;
        mz_inflateEnd(&stream);
        // The last block of an image may be short.
        return complete || (block + 1u == index_.size() - 1u && status == MZ_STREAM_END);
    }

    File file_;
    std::vector<std::uint32_t> index_;
    std::vector<std::uint8_t> packed_;
    unsigned align_{};
};

// CHD: DVD images (unit = 2048 bytes) map hunks straight onto the ISO; CD
// images (unit = 2352 + 96) keep the 2048-byte user data at the frame start.
class ChdImage final : public BlockImage {
public:
    static std::unique_ptr<DiscImage> open(File file, std::string &error) {
        chd_file *chd = nullptr;
        const chd_error status = chd_open_file(file.get(), CHD_OPEN_READ, nullptr, &chd);
        if (status != CHDERR_NONE) {
            error = std::string("cannot open CHD: ") + chd_error_string(status);
            return nullptr;
        }
        auto image = std::unique_ptr<ChdImage>(new ChdImage(std::move(file), chd));
        const chd_header *header = chd_get_header(chd);
        image->hunk_bytes_ = header->hunkbytes;
        image->unit_bytes_ = header->unitbytes;
        image->hunk_.resize(header->hunkbytes);
        if (header->unitbytes == kSector) {
            image->size_ = header->logicalbytes;
        } else if (header->unitbytes == 2448u && header->hunkbytes % 2448u == 0u) {
            image->size_ = header->logicalbytes / 2448u * kSector;
        } else {
            error = "unsupported CHD layout; create it with: chdman createdvd -i game.iso -o game.chd";
            return nullptr;
        }
        image->block_size_ = kSector;
        image->cache_.resize(kSector);
        return image;
    }
    ~ChdImage() override { chd_close(chd_); } // before file_ closes

private:
    ChdImage(File file, chd_file *chd) : file_(std::move(file)), chd_(chd) {}

    bool load(std::uint64_t sector, std::vector<std::uint8_t> &out) override {
        const std::uint64_t byte = sector * unit_bytes_;
        const auto hunk = static_cast<std::uint32_t>(byte / hunk_bytes_);
        if (hunk != loaded_hunk_) {
            if (chd_read(chd_, hunk, hunk_.data()) != CHDERR_NONE) return false;
            loaded_hunk_ = hunk;
        }
        std::memcpy(out.data(), hunk_.data() + byte % hunk_bytes_, kSector);
        return true;
    }

    File file_;
    chd_file *chd_;
    std::vector<std::uint8_t> hunk_;
    std::uint32_t hunk_bytes_{}, unit_bytes_{};
    std::uint32_t loaded_hunk_{~0u};
};

std::string extension(const std::string &name) {
    const auto dot = name.rfind('.');
    std::string ext = dot == std::string::npos ? std::string() : name.substr(dot + 1u);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

} // namespace

std::unique_ptr<DiscImage> DiscImage::open(std::FILE *raw, const std::string &name, std::string &error) {
    File file(raw);
    if (!file) {
        error = "cannot open the file";
        return nullptr;
    }
    const std::string ext = extension(name);
    if (ext == "cso") return CsoImage::open(std::move(file), error);
    if (ext == "chd") return ChdImage::open(std::move(file), error);
    if (ext == "iso") return std::make_unique<IsoImage>(std::move(file));
    error = "unsupported file type ." + ext + " (use .iso, .cso or .chd)";
    return nullptr;
}

std::string list_iso(DiscImage &image, std::vector<IsoEntry> &entries) {
    std::uint8_t pvd[kSector];
    if (!image.read(16u * kSector, pvd, sizeof pvd) || pvd[0] != 1u || std::memcmp(pvd + 1, "CD001", 5) != 0)
        return "not an ISO9660 disc";
    struct Dir {
        std::string path;
        std::uint64_t lba, size;
    };
    std::deque<Dir> dirs{{"", le32(pvd + 156 + 2), le32(pvd + 156 + 10)}};
    std::size_t visited = 0;
    std::vector<std::uint8_t> data;
    while (!dirs.empty()) {
        const Dir dir = dirs.front();
        dirs.pop_front();
        if (++visited > 100000u || dir.size > 64u * 1024u * 1024u) return "corrupt directory tree";
        data.resize(static_cast<std::size_t>(dir.size));
        if (!image.read(dir.lba * kSector, data.data(), data.size())) return "truncated directory";
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
            if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\:\r\n") != std::string::npos)
                return "unsafe file name in the image";
            IsoEntry entry{dir.path.empty() ? name : dir.path + "/" + name, le32(record + 2) * kSector,
                           le32(record + 10), (record[25] & 2u) != 0u};
            entries.push_back(entry);
            if (entry.directory) dirs.push_back({entry.path, entry.offset / kSector, entry.size});
        }
    }
    return {};
}

std::string read_disc_id(DiscImage &image, const std::vector<IsoEntry> &entries) {
    const auto found = std::find_if(entries.begin(), entries.end(), [](const IsoEntry &e) {
        std::string lower = e.path;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lower == "psp_game/param.sfo";
    });
    if (found == entries.end() || found->size < 20u || found->size > 64u * 1024u) return {};
    std::vector<std::uint8_t> sfo(static_cast<std::size_t>(found->size));
    if (!image.read(found->offset, sfo.data(), sfo.size()) || le32(&sfo[0]) != 0x46535000u) return {};
    const std::uint32_t keys = le32(&sfo[8]), values = le32(&sfo[12]), count = le32(&sfo[16]);
    for (std::uint32_t i = 0; i < count && 20u + i * 16u + 16u <= sfo.size(); ++i) {
        const std::uint8_t *entry = &sfo[20u + i * 16u];
        const std::uint32_t key = keys + (entry[0] | (entry[1] << 8u));
        const std::uint32_t length = le32(entry + 4), offset = values + le32(entry + 12);
        if (key >= sfo.size() || offset + length > sfo.size()) continue;
        if (std::strncmp(reinterpret_cast<const char *>(&sfo[key]), "DISC_ID", sfo.size() - key) != 0) continue;
        std::string id(reinterpret_cast<const char *>(&sfo[offset]), length);
        id.resize(std::min(id.size(), id.find('\0')));
        return id;
    }
    return {};
}

} // namespace pspweb::uwp
