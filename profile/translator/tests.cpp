// Known-answer and synthetic round-trip tests for the translator's executable
// step. The encryptor below is test-only and mirrors the decryptor, so it proves
// internal consistency; a real executable is verified by its published hash.
#undef NDEBUG // these checks must run in every build type
#include "crypto.hpp"
#include "prx_decrypt.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

using namespace pspweb::translator;

namespace {

std::vector<std::uint8_t> hex(const char *text) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; text[i] != '\0'; i += 2) out.push_back(static_cast<std::uint8_t>(std::stoul(std::string(text + i, 2), nullptr, 16)));
    return out;
}
template <typename T> bool same(const T &a, const std::vector<std::uint8_t> &b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), b.size()) == 0;
}
Block block(const char *text) { Block b; const auto v = hex(text); std::memcpy(b.data(), v.data(), 16); return b; }
std::vector<std::uint8_t> bytes(const std::string &text) { return {text.begin(), text.end()}; }
void put32(std::uint8_t *p, std::uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i)); }

void expect_error(const std::function<void()> &call, const char *fragment) {
    try {
        call();
    } catch (const PrxError &error) {
        if (std::string(error.what()).find(fragment) != std::string::npos) return;
        std::fprintf(stderr, "unexpected error: %s (wanted %s)\n", error.what(), fragment);
        assert(false);
    }
    std::fprintf(stderr, "missing error: %s\n", fragment);
    assert(false);
}

const std::vector<std::uint8_t> kGzipElf = {
    0x1f,0x8b,0x08,0x00,0x00,0x00,0x00,0x00,0x02,0xff,0xab,0x77,0xf5,0x71,0x63,0xe6,0x12,0x94,0x90,0x57,
    0xd3,0x35,0xb1,0x76,0xf2,0x0c,0x08,0x8f,0x4b,0xcd,0x29,0xae,0x6a,0xec,0xe8,0x9f,0x36,0x77,0xc9,0xea,
    0x4d,0x3b,0x0f,0x1c,0x3f,0x77,0xf5,0xce,0xe3,0x57,0x1f,0x7f,0xb0,0x70,0x0b,0x49,0x2a,0xa8,0xeb,0x99,
    0xda,0x38,0x7b,0x05,0x46,0xc4,0xa7,0xe5,0x96,0x54,0x37,0x75,0x4e,0x98,0x3e,0x6f,0xe9,0x9a,0xcd,0xbb,
    0x0e,0x9e,0x38,0x7f,0xed,0xee,0x93,0xd7,0x9f,0x7e,0xb2,0xf2,0x08,0x4b,0x29,0x6a,0xe8,0x9b,0xd9,0xba,
    0x78,0x07,0x45,0x26,0xa4,0xe7,0x95,0xd6,0x34,0x77,0x4d,0x9c,0x31,0x7f,0xd9,0xda,0x2d,0xbb,0x0f,0x9d,
    0xbc,0x70,0xfd,0xde,0xd3,0x37,0x9f,0x7f,0xb1,0xf1,0x8a,0x48,0x2b,0x69,0x1a,0x98,0xdb,0xb9,0xfa,0x04,
    0x47,0x25,0x66,0xe4,0x97,0xd5,0xb6,0x74,0x4f,0x9a,0xb9,0x60,0xf9,0xba,0xad,0x7b,0x0e,0x9f,0xba,0x78,
    0xe3,0xfe,0xb3,0xb7,0x5f,0x18,0xd8,0xf9,0x44,0x65,0x94,0xb5,0x0c,0x2d,0xec,0xdd,0x7c,0x43,0xa2,0x93,
    0x32,0x0b,0xca,0xeb,0x5a,0x7b,0x26,0xcf,0x5a,0xb8,0x62,0xfd,0xb6,0xbd,0x47,0x4e,0x5f,0xba,0xf9,0xe0,
    0xf9,0xbb,0xaf,0x8c,0x1c,0xfc,0x62,0xb2,0x2a,0xda,0x46,0x96,0x0e,0xee,0x7e,0xa1,0x31,0xc9,0x59,0x85,
    0x15,0xf5,0x6d,0xbd,0x53,0x66,0x2f,0x5a,0xb9,0x61,0xfb,0xbe,0xa3,0x67,0x2e,0xdf,0x7a,0xf8,0xe2,0xfd,
    0x37,0x26,0x4e,0x01,0x71,0x39,0x55,0x1d,0x63,0x2b,0x47,0x0f,0xff,0xb0,0xd8,0x94,0xec,0xa2,0xca,0x86,
    0xf6,0xbe,0xa9,0x73,0x16,0xaf,0xda,0xb8,0x63,0xff,0xb1,0xb3,0x57,0x6e,0x3f,0x7a,0xf9,0xe1,0xfb,0xa8,
    0xd7,0x47,0xbd,0x3e,0xea,0xf5,0x51,0xaf,0x8f,0x7a,0x7d,0xd4,0xeb,0xa3,0x5e,0x1f,0xf5,0xfa,0xa8,0xd7,
    0x47,0xbd,0x3e,0x64,0xbc,0x0e,0x00,0x47,0x31,0x8d,0x16,0xbc,0x0b,0x00,0x00
};

// Test-only inverse of decrypt_prx for one tag.
std::vector<std::uint8_t> encrypt_prx(const std::vector<std::uint8_t> &payload, std::uint32_t tag, bool compressed) {
    const std::size_t padded = (payload.size() + 15u) & ~std::size_t{15};
    std::vector<std::uint8_t> file(0x150 + padded, 0);
    put32(file.data(), 0x5053507Eu);
    file[6] = compressed ? 1u : 0u;
    put32(file.data() + 0x28, compressed ? 64u * 1024u * 1024u - 0x100000u : static_cast<std::uint32_t>(payload.size()));
    put32(file.data() + 0x2C, static_cast<std::uint32_t>(file.size()));
    std::memcpy(file.data() + 0x0A, "SyntheticModule", 15);

    // KIRK command 1 header followed by file 0x00..0x80 and the payload.
    std::vector<std::uint8_t> kirk(0x90 + 0x80 + padded, 0);
    const Block aes_key = block("00112233445566778899aabbccddeeff");
    const Block cmac_key = block("0f1e2d3c4b5a69788796a5b4c3d2e1f0");
    std::memcpy(kirk.data(), aes_key.data(), 16);
    std::memcpy(kirk.data() + 16, cmac_key.data(), 16);
    detail::kirk1_key_encrypt({kirk.data(), 32});
    put32(kirk.data() + 0x60, 1u);
    put32(kirk.data() + 0x70, static_cast<std::uint32_t>(payload.size()));
    put32(kirk.data() + 0x74, 0x80u);
    std::memcpy(kirk.data() + 0x90, file.data(), 0x80);
    std::memcpy(kirk.data() + 0x110, payload.data(), payload.size());
    Aes128(aes_key).cbc_encrypt({kirk.data() + 0x110, padded});
    const Aes128 cmac(cmac_key);
    const Block header_mac = cmac.cmac({kirk.data() + 0x60, 0x30});
    const Block data_mac = cmac.cmac({kirk.data() + 0x60, 0x30 + 0x80 + padded});
    std::memcpy(kirk.data() + 0x20, header_mac.data(), 16);
    std::memcpy(kirk.data() + 0x30, data_mac.data(), 16);
    std::memcpy(file.data() + 0x150, kirk.data() + 0x110, padded);

    const auto mask = detail::expand_seed(detail::tag_seed(tag));
    std::uint8_t masked[0x40];
    for (int i = 0; i < 0x40; ++i) masked[i] = static_cast<std::uint8_t>(kirk[i] ^ mask[0x50 + i]);
    detail::kirk7_encrypt({masked, 0x40});
    for (int i = 0; i < 0x40; ++i) masked[i] ^= mask[0x10 + i];

    std::uint8_t h[0x150] = {};
    put32(h, tag);
    for (int i = 0; i < 0x10; ++i) h[0x5C + i] = static_cast<std::uint8_t>(0xA0 + i);
    std::memcpy(h + 0x80, masked, 0x40);
    std::memcpy(h + 0xC0, kirk.data() + 0x70, 0x10);
    std::memcpy(h + 0xD0, file.data(), 0x80);
    Sha1 sha;
    sha.update({h, 4});
    sha.update({mask.data(), 0x10});
    sha.update({h + 4, 0x58});
    sha.update({h + 0x5C, 0x10});
    sha.update({h + 0x80, 0xD0});
    const auto digest = sha.finish();
    std::memcpy(h + 0x6C, digest.data(), 20);
    detail::kirk7_encrypt({h + 0x5C, 0x60});
    std::memcpy(file.data() + 0x140, h + 0x5C, 0x10);
    std::memcpy(file.data() + 0x12C, h + 0x6C, 0x14);
    std::memcpy(file.data() + 0x80, h + 0x80, 0x30);
    std::memcpy(file.data() + 0xC0, h + 0xB0, 0x10);
    std::memcpy(file.data() + 0xB0, h + 0xC0, 0x10);
    put32(file.data() + 0xD0, tag);
    return file;
}

} // namespace

int main(int argc, char **argv) {
    // --encrypt-fixture <in> <out> [gzip]: synthetic encrypted executable for
    // the Wasm cross-check in scripts/test_web_translator.mjs.
    if (argc >= 4 && std::string(argv[1]) == "--encrypt-fixture") {
        std::ifstream in(argv[2], std::ios::binary);
        const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        const auto file = encrypt_prx(payload, 0xD91611F0u, argc == 5);
        std::ofstream out(argv[3], std::ios::binary);
        out.write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
        return out ? 0 : 1;
    }
    // FIPS-197 appendix C.1.
    {
        const Aes128 aes(block("000102030405060708090a0b0c0d0e0f"));
        Block b = block("00112233445566778899aabbccddeeff");
        aes.encrypt(b);
        assert(same(b, hex("69c4e0d86a7b0430d8cdb78070b4c55a")));
        aes.decrypt(b);
        assert(same(b, hex("00112233445566778899aabbccddeeff")));
    }
    // RFC 4493 section 4 examples.
    {
        const Aes128 aes(block("2b7e151628aed2a6abf7158809cf4f3c"));
        const auto m = hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710");
        assert(same(aes.cmac({m.data(), 0}), hex("bb1d6929e95937287fa37d129b756746")));
        assert(same(aes.cmac({m.data(), 16}), hex("070a16b46b4d4144f79bdd9dd04a287c")));
        assert(same(aes.cmac({m.data(), 40}), hex("dfa66747de9ae63030ca32611497c827")));
        assert(same(aes.cmac({m.data(), 64}), hex("51f0bebf7e3b9d92fc49741779363cfe")));
        // NIST SP 800-38A F.2.1 CBC-AES128 (zero IV variant checked by round trip).
        std::vector<std::uint8_t> data = m;
        aes.cbc_encrypt(data);
        assert(!same(data, m));
        aes.cbc_decrypt(data);
        assert(same(data, m));
    }
    // FIPS 180-4 examples.
    {
        Sha1 empty;
        assert(same(empty.finish(), hex("da39a3ee5e6b4b0d3255bfef95601890afd80709")));
        Sha1 abc;
        abc.update(bytes("abc"));
        assert(same(abc.finish(), hex("a9993e364706816aba3e25717850c26c9cd0d89d")));
        Sha1 two;
        const auto text = bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
        two.update({text.data(), 10});
        two.update({text.data() + 10, text.size() - 10});
        assert(same(two.finish(), hex("84983e441c3bd26ebaae4aa1f95129e5e54670f1")));
    }
    assert(detail::crc32(bytes("123456789")) == 0xCBF43926u);

    // gzip, including CRC and length checks.
    std::vector<std::uint8_t> elf_payload{0x7F, 'E', 'L', 'F'};
    for (int i = 0; i < 3000; ++i) elf_payload.push_back(static_cast<std::uint8_t>((i * 7 + 3) % 251));
    assert(gunzip(kGzipElf, 3004u) == elf_payload);
    expect_error([] { (void)gunzip(kGzipElf, 3003u); }, "gzip");
    auto bad_crc = kGzipElf;
    bad_crc[bad_crc.size() - 8] ^= 1u;
    expect_error([&] { (void)gunzip(bad_crc, 3004u); }, "CRC");
    expect_error([] { (void)gunzip(bytes("not gzip at all, really"), 100u); }, "Invalid gzip");

    // Plain ELF passes through unchanged.
    assert(executable_to_elf(elf_payload) == elf_payload);

    // Synthetic type 2 round trips: plain and gzip payloads, first and last tags.
    for (const std::uint32_t tag : {0xD91605F0u, 0xD91611F0u, 0xD91628F0u}) {
        const auto plain = encrypt_prx(elf_payload, tag, false);
        assert(is_encrypted_prx(plain) && !is_elf(plain));
        const auto decrypted = decrypt_prx(plain);
        assert(decrypted.tag == tag && decrypted.compression == Compression::None && decrypted.data == elf_payload);
        assert(executable_to_elf(plain) == elf_payload);
        const auto packed = encrypt_prx(kGzipElf, tag, true);
        assert(decrypt_prx(packed).compression == Compression::Gzip);
        assert(executable_to_elf(packed) == elf_payload);
    }

    // Rejections.
    const auto good = encrypt_prx(elf_payload, 0xD91611F0u, false);
    auto tampered = good;
    tampered[0x150 + 40] ^= 1u;
    expect_error([&] { (void)decrypt_prx(tampered); }, "data CMAC");
    auto bad_header = good;
    bad_header[0x12C] ^= 1u;
    expect_error([&] { (void)decrypt_prx(bad_header); }, "header check");
    auto unknown = good;
    put32(unknown.data() + 0xD0, 0x12345678u);
    expect_error([&] { (void)decrypt_prx(unknown); }, "tag");
    auto variant = good;
    variant[0xE0] = 1u;
    expect_error([&] { (void)decrypt_prx(variant); }, "variant");
    expect_error([&] { (void)decrypt_prx(std::vector<std::uint8_t>(good.begin(), good.begin() + 0x200)); }, "truncated");
    expect_error([&] { (void)decrypt_prx(std::vector<std::uint8_t>(0x150, 0)); }, "Not an encrypted");
    std::vector<std::uint8_t> kl4e{'K', 'L', '4', 'E', 1, 2, 3, 4};
    expect_error([&] { (void)executable_to_elf(encrypt_prx(kl4e, 0xD91611F0u, true)); }, "KL4E");
    std::vector<std::uint8_t> not_elf(64, 0x42);
    expect_error([&] { (void)executable_to_elf(encrypt_prx(not_elf, 0xD91611F0u, false)); }, "did not produce an ELF");

    std::puts("PASS: AES/CMAC/SHA-1/CRC-32 known answers, gzip checks, synthetic type 2 PRX round trips and rejections");
    return 0;
}
