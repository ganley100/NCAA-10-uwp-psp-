// Retail PSP executable ("~PSP") decryption for the browser translator.
//
// UMD game executables use the tag family 0xD91605F0..0xD91628F0: a per-tag
// seed expanded with KIRK command 7 (AES-128-CBC, key-vault slot 0x5D) masks a
// KIRK command 1 header; a SHA-1 over the reassembled header authenticates it;
// command 1 then checks the AES-CMAC of header and data and AES-CBC decrypts
// the payload, which may be gzip-compressed. The algorithm and key data were
// taken from public documentation of the PSP's PRX loader and KIRK engine and
// implemented independently; no third-party decrypter code is included.
// Successful decryption is necessary but not sufficient: callers must still
// check the result against the verified executable hash.
#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace pspweb::translator {

class PrxError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class Compression : std::uint8_t { None, Gzip, Kl4e };

struct DecryptedPrx {
    std::vector<std::uint8_t> data;
    Compression compression{Compression::None};
    std::uint32_t tag{};
};

[[nodiscard]] bool is_encrypted_prx(std::span<const std::uint8_t> file);
[[nodiscard]] bool is_elf(std::span<const std::uint8_t> file);
[[nodiscard]] DecryptedPrx decrypt_prx(std::span<const std::uint8_t> file);
// RFC 1952 gzip with CRC-32 and length checks; at most max_output bytes.
[[nodiscard]] std::vector<std::uint8_t> gunzip(std::span<const std::uint8_t> data, std::uint32_t max_output);
// Encrypted, compressed or plain executable -> plain ELF, or throws PrxError.
[[nodiscard]] std::vector<std::uint8_t> executable_to_elf(std::span<const std::uint8_t> file);

namespace detail {
// Exposed for synthetic round-trip tests.
inline constexpr std::uint32_t kKirkSeedKey = 0x5Du;
[[nodiscard]] const std::uint8_t *tag_seed(std::uint32_t tag);
[[nodiscard]] std::vector<std::uint8_t> expand_seed(const std::uint8_t *seed);
void kirk7_decrypt(std::span<std::uint8_t> data);
void kirk7_encrypt(std::span<std::uint8_t> data);
void kirk1_key_decrypt(std::span<std::uint8_t> data);
void kirk1_key_encrypt(std::span<std::uint8_t> data);
std::uint32_t crc32(std::span<const std::uint8_t> data);
} // namespace detail

} // namespace pspweb::translator
