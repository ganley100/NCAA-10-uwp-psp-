#include "crypto.hpp"

#include <cstring>
#include <stdexcept>

namespace pspweb::translator {
namespace {

std::uint8_t gf_mul(std::uint8_t a, std::uint8_t b) {
    std::uint8_t product = 0;
    while (b != 0u) {
        if ((b & 1u) != 0u) product ^= a;
        a = static_cast<std::uint8_t>((a << 1) ^ ((a & 0x80u) != 0u ? 0x1Bu : 0u));
        b >>= 1;
    }
    return product;
}

struct SBoxes {
    std::uint8_t forward[256];
    std::uint8_t inverse[256];
    SBoxes() {
        for (int x = 0; x < 256; ++x) {
            // Multiplicative inverse in GF(2^8) (x^254), then the affine transform.
            std::uint8_t inv = 1;
            for (int i = 0; i < 254; ++i) inv = gf_mul(inv, static_cast<std::uint8_t>(x));
            if (x == 0) inv = 0;
            std::uint8_t s = inv;
            for (int r = 1; r <= 4; ++r) s ^= static_cast<std::uint8_t>((inv << r) | (inv >> (8 - r)));
            s ^= 0x63u;
            forward[x] = s;
            inverse[s] = static_cast<std::uint8_t>(x);
        }
    }
};

const SBoxes &sboxes() {
    static const SBoxes boxes;
    return boxes;
}

// CMAC subkey derivation: shift left by one bit in GF(2^128).
Block doubled(Block b) {
    const bool carry = (b[0] & 0x80u) != 0u;
    for (int i = 0; i < 15; ++i) b[i] = static_cast<std::uint8_t>((b[i] << 1) | (b[i + 1] >> 7));
    b[15] = static_cast<std::uint8_t>((b[15] << 1) ^ (carry ? 0x87u : 0u));
    return b;
}

std::uint32_t rotl(std::uint32_t value, int bits) { return (value << bits) | (value >> (32 - bits)); }

} // namespace

Aes128::Aes128(const Block &key) {
    const auto &sbox = sboxes().forward;
    std::memcpy(round_keys_.data(), key.data(), 16);
    std::uint8_t rcon = 1;
    for (std::size_t i = 16; i < round_keys_.size(); i += 4) {
        std::uint8_t t[4] = {round_keys_[i - 4], round_keys_[i - 3], round_keys_[i - 2], round_keys_[i - 1]};
        if (i % 16 == 0) {
            const std::uint8_t first = t[0];
            t[0] = static_cast<std::uint8_t>(sbox[t[1]] ^ rcon);
            t[1] = sbox[t[2]];
            t[2] = sbox[t[3]];
            t[3] = sbox[first];
            rcon = gf_mul(rcon, 2);
        }
        for (int j = 0; j < 4; ++j) round_keys_[i + j] = static_cast<std::uint8_t>(round_keys_[i - 16 + j] ^ t[j]);
    }
}

void Aes128::add_round_key(Block &b, int round) const {
    for (int i = 0; i < 16; ++i) b[i] ^= round_keys_[16 * round + i];
}

void Aes128::encrypt(Block &b) const {
    const auto &sbox = sboxes().forward;
    add_round_key(b, 0);
    for (int round = 1; round <= 10; ++round) {
        Block s;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) s[r + 4 * c] = sbox[b[r + 4 * ((c + r) % 4)]];
        if (round != 10) {
            for (int c = 0; c < 4; ++c) {
                const std::uint8_t *a = &s[4 * c];
                b[4 * c + 0] = static_cast<std::uint8_t>(gf_mul(a[0], 2) ^ gf_mul(a[1], 3) ^ a[2] ^ a[3]);
                b[4 * c + 1] = static_cast<std::uint8_t>(a[0] ^ gf_mul(a[1], 2) ^ gf_mul(a[2], 3) ^ a[3]);
                b[4 * c + 2] = static_cast<std::uint8_t>(a[0] ^ a[1] ^ gf_mul(a[2], 2) ^ gf_mul(a[3], 3));
                b[4 * c + 3] = static_cast<std::uint8_t>(gf_mul(a[0], 3) ^ a[1] ^ a[2] ^ gf_mul(a[3], 2));
            }
        } else {
            b = s;
        }
        add_round_key(b, round);
    }
}

void Aes128::decrypt(Block &b) const {
    const auto &inv = sboxes().inverse;
    add_round_key(b, 10);
    for (int round = 9; round >= 0; --round) {
        Block s;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) s[r + 4 * ((c + r) % 4)] = inv[b[r + 4 * c]];
        b = s;
        add_round_key(b, round);
        if (round == 0) break;
        for (int c = 0; c < 4; ++c) {
            const std::uint8_t a[4] = {b[4 * c], b[4 * c + 1], b[4 * c + 2], b[4 * c + 3]};
            b[4 * c + 0] = static_cast<std::uint8_t>(gf_mul(a[0], 14) ^ gf_mul(a[1], 11) ^ gf_mul(a[2], 13) ^ gf_mul(a[3], 9));
            b[4 * c + 1] = static_cast<std::uint8_t>(gf_mul(a[0], 9) ^ gf_mul(a[1], 14) ^ gf_mul(a[2], 11) ^ gf_mul(a[3], 13));
            b[4 * c + 2] = static_cast<std::uint8_t>(gf_mul(a[0], 13) ^ gf_mul(a[1], 9) ^ gf_mul(a[2], 14) ^ gf_mul(a[3], 11));
            b[4 * c + 3] = static_cast<std::uint8_t>(gf_mul(a[0], 11) ^ gf_mul(a[1], 13) ^ gf_mul(a[2], 9) ^ gf_mul(a[3], 14));
        }
    }
}

void Aes128::cbc_encrypt(std::span<std::uint8_t> data) const {
    if (data.size() % 16 != 0) throw std::invalid_argument("AES-CBC data is not block aligned");
    Block chain{};
    for (std::size_t i = 0; i < data.size(); i += 16) {
        Block b;
        for (int j = 0; j < 16; ++j) b[j] = static_cast<std::uint8_t>(data[i + j] ^ chain[j]);
        encrypt(b);
        std::memcpy(data.data() + i, b.data(), 16);
        chain = b;
    }
}

void Aes128::cbc_decrypt(std::span<std::uint8_t> data) const {
    if (data.size() % 16 != 0) throw std::invalid_argument("AES-CBC data is not block aligned");
    Block chain{};
    for (std::size_t i = 0; i < data.size(); i += 16) {
        Block cipher, b;
        std::memcpy(cipher.data(), data.data() + i, 16);
        b = cipher;
        decrypt(b);
        for (int j = 0; j < 16; ++j) data[i + j] = static_cast<std::uint8_t>(b[j] ^ chain[j]);
        chain = cipher;
    }
}

Block Aes128::cmac(std::span<const std::uint8_t> data) const {
    Block k1{};
    encrypt(k1);
    k1 = doubled(k1);
    const Block k2 = doubled(k1);
    const std::size_t blocks = data.empty() ? 1 : (data.size() + 15) / 16;
    const bool complete = !data.empty() && data.size() % 16 == 0;
    Block state{};
    for (std::size_t n = 0; n + 1 < blocks; ++n) {
        for (int j = 0; j < 16; ++j) state[j] ^= data[16 * n + j];
        encrypt(state);
    }
    Block last{};
    const std::size_t tail = data.size() - 16 * (blocks - 1);
    std::memcpy(last.data(), data.data() + 16 * (blocks - 1), tail);
    if (!complete) last[tail] = 0x80u;
    const Block &subkey = complete ? k1 : k2;
    for (int j = 0; j < 16; ++j) state[j] ^= static_cast<std::uint8_t>(last[j] ^ subkey[j]);
    encrypt(state);
    return state;
}

Sha1::Sha1() : state_{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u} {}

void Sha1::block(const std::uint8_t *p) {
    std::uint32_t w[80];
    for (int i = 0; i < 16; ++i)
        w[i] = (static_cast<std::uint32_t>(p[4 * i]) << 24) | (static_cast<std::uint32_t>(p[4 * i + 1]) << 16) |
               (static_cast<std::uint32_t>(p[4 * i + 2]) << 8) | p[4 * i + 3];
    for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4];
    for (int i = 0; i < 80; ++i) {
        std::uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
        const std::uint32_t t = rotl(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rotl(b, 30); b = a; a = t;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d; state_[4] += e;
}

void Sha1::update(std::span<const std::uint8_t> data) {
    length_ += data.size();
    for (const std::uint8_t byte : data) {
        buffer_[buffered_++] = byte;
        if (buffered_ == 64) { block(buffer_.data()); buffered_ = 0; }
    }
}

Sha1Digest Sha1::finish() {
    const std::uint64_t bits = length_ * 8u;
    const std::uint8_t pad = 0x80u;
    update({&pad, 1});
    const std::uint8_t zero = 0;
    while (buffered_ != 56) update({&zero, 1});
    for (int i = 7; i >= 0; --i) {
        const auto byte = static_cast<std::uint8_t>(bits >> (8 * i));
        buffer_[buffered_++] = byte;
    }
    block(buffer_.data());
    Sha1Digest out;
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j) out[4 * i + j] = static_cast<std::uint8_t>(state_[i] >> (24 - 8 * j));
    return out;
}

} // namespace pspweb::translator
