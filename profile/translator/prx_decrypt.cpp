#include "prx_decrypt.hpp"

#include "crypto.hpp"
#include "psprecomp/deflate.hpp"
#include "psprecomp/guest_memory.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

namespace pspweb::translator {
namespace {

// File layout of a type 2 header (0x150 bytes before the payload).
constexpr std::size_t kHeaderSize = 0x150;
constexpr std::size_t kKirkHeaderSize = 0x90;
// The KIRK command 1 header is rebuilt so that the original first 0x80 bytes
// of the file follow it and the payload follows those, as in the file.
constexpr std::size_t kKirkOffset = kHeaderSize - kKirkHeaderSize - 0x80;

constexpr std::uint32_t kPspMagic = 0x5053507Eu; // "~PSP"

// KIRK key-vault slot 0x5D (command 7 key for game executables) and the
// command 1 header key. The neighbouring slots 0x38/0x39/0x63 also used by
// host/pgd.cpp come from the same published key vault.
constexpr Block kKeyVault5D{0x11, 0x5A, 0x5D, 0x20, 0xD5, 0x3A, 0x8D, 0xD3, 0x9C, 0xC5, 0xAF, 0x41, 0x0F, 0x0F, 0x18, 0x6F};
constexpr Block kKirk1Key{0x98, 0xC9, 0x40, 0x97, 0x5C, 0x1D, 0x10, 0xE8, 0x7F, 0xE6, 0x0E, 0xA3, 0xFD, 0x03, 0xA8, 0xBA};

struct TagSeed {
    std::uint32_t tag;
    Block seed;
};

// Per-tag seeds for retail UMD/PSN game executables (decrypt type 2, KIRK key 0x5D).
constexpr TagSeed kTagSeeds[] = {
    {0xD91605F0u, {0xB8, 0x8C, 0x45, 0x8B, 0xB6, 0xE7, 0x6E, 0xB8, 0x51, 0x59, 0xA6, 0x53, 0x7C, 0x5E, 0x86, 0x31}},
    {0xD91606F0u, {0xED, 0x10, 0xE0, 0x36, 0xC4, 0xFE, 0x83, 0xF3, 0x75, 0x70, 0x5E, 0xF6, 0xA4, 0x40, 0x05, 0xF7}},
    {0xD91608F0u, {0x5C, 0x77, 0x0C, 0xBB, 0xB4, 0xC2, 0x4F, 0xA2, 0x7E, 0x3B, 0x4E, 0xB4, 0xB4, 0xC8, 0x70, 0xAF}},
    {0xD91609F0u, {0xD0, 0x36, 0x12, 0x75, 0x80, 0x56, 0x20, 0x43, 0xC4, 0x30, 0x94, 0x3E, 0x1C, 0x75, 0xD1, 0xBF}},
    {0xD9160AF0u, {0x10, 0xA9, 0xAC, 0x16, 0xAE, 0x19, 0xC0, 0x7E, 0x3B, 0x60, 0x77, 0x86, 0x01, 0x6F, 0xF2, 0x63}},
    {0xD9160BF0u, {0x83, 0x83, 0xF1, 0x37, 0x53, 0xD0, 0xBE, 0xFC, 0x8D, 0xA7, 0x32, 0x52, 0x46, 0x0A, 0xC2, 0xC2}},
    {0xD91611F0u, {0x61, 0xB0, 0xC0, 0x58, 0x71, 0x57, 0xD9, 0xFA, 0x74, 0x67, 0x0E, 0x5C, 0x7E, 0x6E, 0x95, 0xB9}},
    {0xD91612F0u, {0x9E, 0x20, 0xE1, 0xCD, 0xD7, 0x88, 0xDE, 0xC0, 0x31, 0x9B, 0x10, 0xAF, 0xC5, 0xB8, 0x73, 0x23}},
    {0xD91613F0u, {0xEB, 0xFF, 0x40, 0xD8, 0xB4, 0x1A, 0xE1, 0x66, 0x91, 0x3B, 0x8F, 0x64, 0xB6, 0xFC, 0xB7, 0x12}},
    {0xD91614F0u, {0xFD, 0xF7, 0xB7, 0x3C, 0x9F, 0xD1, 0x33, 0x95, 0x11, 0xB8, 0xB5, 0xBB, 0x54, 0x23, 0x73, 0x85}},
    {0xD91615F0u, {0xC8, 0x03, 0xE3, 0x44, 0x50, 0xF1, 0xE7, 0x2A, 0x6A, 0x0D, 0xC3, 0x61, 0xB6, 0x8E, 0x5F, 0x51}},
    {0xD91616F0u, {0x53, 0x03, 0xB8, 0x6A, 0x10, 0x19, 0x98, 0x49, 0x1C, 0xAF, 0x30, 0xE4, 0x25, 0x1B, 0x6B, 0x28}},
    {0xD91617F0u, {0x02, 0xFA, 0x48, 0x73, 0x75, 0xAF, 0xAE, 0x0A, 0x67, 0x89, 0x2B, 0x95, 0x4B, 0x09, 0x87, 0xA3}},
    {0xD91618F0u, {0x96, 0x96, 0x7C, 0xC3, 0xF7, 0x12, 0xDA, 0x62, 0x1B, 0xF6, 0x9A, 0x9A, 0x44, 0x44, 0xBC, 0x48}},
    {0xD91619F0u, {0xE0, 0x32, 0xA7, 0x08, 0x6B, 0x2B, 0x29, 0x2C, 0xD1, 0x4D, 0x5B, 0xEE, 0xA8, 0xC8, 0xB4, 0xE9}},
    {0xD9161AF0u, {0x27, 0xE5, 0xA7, 0x49, 0x52, 0xE1, 0x94, 0x67, 0x35, 0x66, 0x91, 0x0C, 0xE8, 0x9A, 0x25, 0x24}},
    {0xD91620F0u, {0x52, 0x1C, 0xB4, 0x5F, 0x40, 0x3B, 0x9A, 0xDD, 0xAC, 0xFC, 0xEA, 0x92, 0xFD, 0xDD, 0xF5, 0x90}},
    {0xD91621F0u, {0xD1, 0x91, 0x2E, 0xA6, 0x21, 0x14, 0x29, 0x62, 0xF6, 0xED, 0xAE, 0xCB, 0xDD, 0xA3, 0xBA, 0xFE}},
    {0xD91622F0u, {0x59, 0x5D, 0x78, 0x4D, 0x21, 0xB2, 0x01, 0x17, 0x6C, 0x9A, 0xB5, 0x1B, 0xDA, 0xB7, 0xF9, 0xE6}},
    {0xD91623F0u, {0xAA, 0x45, 0xEB, 0x4F, 0x62, 0xFB, 0xD1, 0x0D, 0x71, 0xD5, 0x62, 0xD2, 0xF5, 0xBF, 0xA5, 0x2F}},
    {0xD91624F0u, {0x61, 0xB7, 0x26, 0xAF, 0x8B, 0xF1, 0x41, 0x58, 0x83, 0x6A, 0xC4, 0x92, 0x12, 0xCB, 0xB1, 0xE9}},
    {0xD91628F0u, {0x49, 0xA4, 0xFC, 0x66, 0xDC, 0xE7, 0x62, 0x21, 0xDB, 0x18, 0xA7, 0x50, 0xD6, 0xA8, 0xC1, 0xB6}},
};

std::uint32_t le32(const std::uint8_t *p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
void put32(std::uint8_t *p, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(value >> (8 * i));
}
std::size_t align16(std::size_t value) { return (value + 15u) & ~std::size_t{15}; }

const Aes128 &kirk7_aes() { static const Aes128 aes(kKeyVault5D); return aes; }
const Aes128 &kirk1_aes() { static const Aes128 aes(kKirk1Key); return aes; }

// KIRK command 1 on a header at the start of buffer: verifies both CMACs and
// returns the decrypted payload.
std::vector<std::uint8_t> kirk_cmd1(std::span<std::uint8_t> buffer) {
    if (buffer.size() < kKirkHeaderSize) throw PrxError("Truncated KIRK header.");
    std::uint8_t *header = buffer.data();
    if (le32(header + 0x60) != 1u) throw PrxError("Unsupported KIRK mode.");
    if (header[0x64] != 0u) throw PrxError("ECDSA-signed executables are not supported.");
    const std::uint32_t data_size = le32(header + 0x70), data_offset = le32(header + 0x74);
    if (data_size == 0u) throw PrxError("Empty encrypted payload.");
    const std::size_t padded = align16(data_size);
    if (static_cast<std::uint64_t>(kKirkHeaderSize) + data_offset + padded > buffer.size()) {
        throw PrxError("The encrypted payload extends past the end of the executable.");
    }
    std::array<std::uint8_t, 32> keys;
    std::memcpy(keys.data(), header, keys.size());
    kirk1_aes().cbc_decrypt(keys);
    Block aes_key, cmac_key;
    std::memcpy(aes_key.data(), keys.data(), 16);
    std::memcpy(cmac_key.data(), keys.data() + 16, 16);
    const Aes128 cmac(cmac_key);
    const Block header_mac = cmac.cmac({header + 0x60, 0x30});
    const Block data_mac = cmac.cmac({header + 0x60, 0x30 + padded + data_offset});
    if (std::memcmp(header_mac.data(), header + 0x20, 16) != 0) throw PrxError("KIRK header CMAC mismatch.");
    if (std::memcmp(data_mac.data(), header + 0x30, 16) != 0) throw PrxError("KIRK data CMAC mismatch.");
    std::vector<std::uint8_t> out(header + kKirkHeaderSize + data_offset, header + kKirkHeaderSize + data_offset + padded);
    Aes128(aes_key).cbc_decrypt(out);
    out.resize(data_size);
    return out;
}

const std::array<std::uint32_t, 256> &crc_table() {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1u) != 0u ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    return table;
}

} // namespace

namespace detail {

const std::uint8_t *tag_seed(std::uint32_t tag) {
    for (const auto &entry : kTagSeeds)
        if (entry.tag == tag) return entry.seed.data();
    return nullptr;
}

std::vector<std::uint8_t> expand_seed(const std::uint8_t *seed) {
    std::vector<std::uint8_t> out(0x90);
    for (std::size_t i = 0; i < out.size(); i += 16) {
        std::memcpy(out.data() + i, seed, 16);
        out[i] = static_cast<std::uint8_t>(i / 16);
    }
    kirk7_decrypt(out);
    return out;
}

void kirk7_decrypt(std::span<std::uint8_t> data) { kirk7_aes().cbc_decrypt(data); }
void kirk7_encrypt(std::span<std::uint8_t> data) { kirk7_aes().cbc_encrypt(data); }
void kirk1_key_decrypt(std::span<std::uint8_t> data) { kirk1_aes().cbc_decrypt(data); }
void kirk1_key_encrypt(std::span<std::uint8_t> data) { kirk1_aes().cbc_encrypt(data); }

std::uint32_t crc32(std::span<const std::uint8_t> data) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (const std::uint8_t byte : data) c = crc_table()[(c ^ byte) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

} // namespace detail

bool is_encrypted_prx(std::span<const std::uint8_t> file) {
    return file.size() >= 4 && le32(file.data()) == kPspMagic;
}

bool is_elf(std::span<const std::uint8_t> file) {
    return file.size() >= 4 && file[0] == 0x7F && file[1] == 'E' && file[2] == 'L' && file[3] == 'F';
}

DecryptedPrx decrypt_prx(std::span<const std::uint8_t> file) {
    if (file.size() < kHeaderSize || !is_encrypted_prx(file)) throw PrxError("Not an encrypted PSP executable.");
    const std::uint32_t psp_size = le32(file.data() + 0x2C);
    if (psp_size < kHeaderSize || psp_size > file.size()) throw PrxError("The executable is truncated.");
    const std::uint32_t tag = le32(file.data() + 0xD0);
    const std::uint8_t *seed = detail::tag_seed(tag);
    if (seed == nullptr) throw PrxError("Unsupported executable encryption tag.");
    if (std::any_of(file.data() + 0xD4, file.data() + 0x12C, [](std::uint8_t b) { return b != 0u; })) {
        throw PrxError("Unsupported executable header variant.");
    }
    const auto mask = detail::expand_seed(seed);

    // Reassemble the authenticated header: tag | 0x58 zero bytes | id | SHA-1 |
    // KIRK header (file 0x80..0xB0 + 0xC0..0xD0) | KIRK sizes | file 0x00..0x80.
    std::array<std::uint8_t, kHeaderSize> h{};
    std::memcpy(h.data() + 0x00, file.data() + 0xD0, 4);
    std::memcpy(h.data() + 0x5C, file.data() + 0x140, 0x10);
    std::memcpy(h.data() + 0x6C, file.data() + 0x12C, 0x14);
    std::memcpy(h.data() + 0x80, file.data() + 0x80, 0x30);
    std::memcpy(h.data() + 0xB0, file.data() + 0xC0, 0x10);
    std::memcpy(h.data() + 0xC0, file.data() + 0xB0, 0x10);
    std::memcpy(h.data() + 0xD0, file.data(), 0x80);
    // id, SHA-1 and most of the KIRK header form one command 7 block run.
    detail::kirk7_decrypt({h.data() + 0x5C, 0x60});

    Sha1 sha;
    sha.update({h.data(), 4});
    sha.update({mask.data(), 0x10});
    sha.update({h.data() + 0x04, 0x58});
    sha.update({h.data() + 0x5C, 0x10});
    sha.update({h.data() + 0x80, kHeaderSize - 0x80});
    const auto digest = sha.finish();
    if (std::memcmp(digest.data(), h.data() + 0x6C, digest.size()) != 0) {
        throw PrxError("Executable header check failed; the file is damaged or uses another format.");
    }

    // Rebuild the KIRK command 1 input in place of the file's header.
    std::vector<std::uint8_t> buffer(file.begin() + kKirkOffset, file.begin() + psp_size);
    std::uint8_t *kirk = buffer.data();
    std::memset(kirk, 0, kKirkHeaderSize);
    std::memcpy(kirk + 0x70, h.data() + 0xC0, 0x10);
    std::memcpy(kirk + kKirkHeaderSize, h.data() + 0xD0, 0x80);
    for (int i = 0; i < 0x40; ++i) kirk[i] = static_cast<std::uint8_t>(h[0x80 + i] ^ mask[0x10 + i]);
    detail::kirk7_decrypt({kirk, 0x40});
    for (int i = 0; i < 0x40; ++i) kirk[i] ^= mask[0x50 + i];
    put32(kirk + 0x60, 1u);

    DecryptedPrx result{kirk_cmd1(buffer), Compression::None, tag};
    const bool compressed = (file[6] & 1u) != 0u;
    const auto &data = result.data;
    if (compressed && data.size() >= 2 && data[0] == 0x1F && data[1] == 0x8B) {
        result.compression = Compression::Gzip;
    } else if (compressed && data.size() >= 4 && data[0] == 'K' && data[1] == 'L' && (data[2] == '4' || data[2] == '3') && data[3] == 'E') {
        result.compression = Compression::Kl4e;
    } else if (compressed) {
        throw PrxError("Unknown executable compression.");
    }
    return result;
}

std::vector<std::uint8_t> gunzip(std::span<const std::uint8_t> data, std::uint32_t max_output) {
    if (data.size() < 18 || data[0] != 0x1F || data[1] != 0x8B || data[2] != 8) throw PrxError("Invalid gzip stream.");
    const std::uint8_t flags = data[3];
    if ((flags & 0xE0u) != 0u) throw PrxError("Invalid gzip flags.");
    std::size_t at = 10;
    auto need = [&](std::size_t n) { if (at + n > data.size()) throw PrxError("Truncated gzip header."); };
    if ((flags & 4u) != 0u) { need(2); const std::size_t extra = data[at] | (data[at + 1] << 8); at += 2; need(extra); at += extra; }
    for (const std::uint8_t bit : {std::uint8_t{8}, std::uint8_t{16}}) {
        if ((flags & bit) != 0u) {
            while (true) { need(1); if (data[at++] == 0u) break; }
        }
    }
    if ((flags & 2u) != 0u) { need(2); at += 2; }
    const std::size_t compressed = data.size() - at;
    // The inflater works between guest-memory ranges; a PSP executable always
    // fits the 64 MiB layout together with its compressed form.
    constexpr std::uint32_t kMemory = 64u * 1024u * 1024u;
    if (align16(compressed) + std::uint64_t{max_output} > kMemory) throw PrxError("Decompressed executable is too large.");
    const auto memory = std::make_unique<psprecomp::GuestMemory>(kMemory);
    const std::uint32_t input = psprecomp::GuestMemory::kPhysicalBase;
    const std::uint32_t output = input + static_cast<std::uint32_t>(align16(compressed));
    memory->copy_in(input, data.subspan(at));
    const auto status = psprecomp::inflate_raw_deflate(*memory, output, max_output, input);
    if (status.status != psprecomp::RawDeflateStatus::Ok) throw PrxError("Corrupt or oversized gzip data.");
    const std::size_t trailer = at + status.input_consumed;
    if (trailer + 8u > data.size()) throw PrxError("Truncated gzip trailer.");
    std::vector<std::uint8_t> out(status.output_size);
    memory->copy_out(output, out);
    if (detail::crc32(out) != le32(data.data() + trailer) || le32(data.data() + trailer + 4) != out.size()) {
        throw PrxError("gzip CRC or length mismatch.");
    }
    return out;
}

std::vector<std::uint8_t> executable_to_elf(std::span<const std::uint8_t> file) {
    if (is_elf(file)) return {file.begin(), file.end()};
    auto prx = decrypt_prx(file);
    std::vector<std::uint8_t> elf;
    if (prx.compression == Compression::Gzip) {
        elf = gunzip(prx.data, std::max<std::uint32_t>(le32(file.data() + 0x28), 1u));
    } else if (prx.compression == Compression::Kl4e) {
        throw PrxError("KL4E/KL3E-compressed executables are not supported yet.");
    } else {
        elf = std::move(prx.data);
    }
    if (!is_elf(elf)) throw PrxError("Decryption did not produce an ELF executable.");
    return elf;
}

} // namespace pspweb::translator
