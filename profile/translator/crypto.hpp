// Small, dependency-free AES-128, AES-CMAC and SHA-1 for the browser translator.
// AES follows FIPS-197 (the same construction as host/pgd.cpp), CMAC follows
// RFC 4493 and SHA-1 follows FIPS 180-4. Known-answer tests live in
// tests/translator.cpp.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace pspweb::translator {

using Block = std::array<std::uint8_t, 16>;

class Aes128 {
public:
    explicit Aes128(const Block &key);
    void encrypt(Block &block) const;
    void decrypt(Block &block) const;
    // CBC with a zero IV, the mode KIRK uses; size must be a multiple of 16.
    void cbc_encrypt(std::span<std::uint8_t> data) const;
    void cbc_decrypt(std::span<std::uint8_t> data) const;
    [[nodiscard]] Block cmac(std::span<const std::uint8_t> data) const;

private:
    void add_round_key(Block &block, int round) const;
    std::array<std::uint8_t, 176> round_keys_{};
};

using Sha1Digest = std::array<std::uint8_t, 20>;

class Sha1 {
public:
    Sha1();
    void update(std::span<const std::uint8_t> data);
    [[nodiscard]] Sha1Digest finish();

private:
    void block(const std::uint8_t *data);
    std::array<std::uint32_t, 5> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_{};
    std::uint64_t length_{};
};

} // namespace pspweb::translator
