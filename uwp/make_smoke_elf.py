"""Write the synthetic PSP ELF used for the game-free UWP smoke package.

Same hand-assembled program as scripts/psp_elf_fixture.mjs; no game data.
"""
import struct
import sys

TEXT_BASE = 0x08804000
CODE = [
    0x27BDFFF0, 0xAFBF000C, 0x0E20100A, 0x24040003, 0x10400002, 0x00000000,
    0x24420001, 0x8FBF000C, 0x03E00008, 0x27BD0010, 0x03E00008, 0x00841021,
]


def psp_elf(words):
    text = struct.pack(f'<{len(words)}I', *words)
    names = b'\0.text\0.shstrtab\0'
    text_offset = 0x1000
    names_offset = text_offset + len(text)
    sh_offset = (names_offset + len(names) + 3) & ~3
    out = bytearray(sh_offset + 3 * 40)
    out[0:7] = bytes([0x7F, 0x45, 0x4C, 0x46, 1, 1, 1])
    struct.pack_into('<HHIIIII', out, 16, 2, 8, 1, TEXT_BASE, 52, sh_offset, 0x10A23001)
    struct.pack_into('<HHHHHH', out, 40, 52, 32, 1, 40, 3, 2)
    struct.pack_into('<8I', out, 52, 1, text_offset, TEXT_BASE, TEXT_BASE, len(text), len(text), 5, 16)
    out[text_offset:text_offset + len(text)] = text
    out[names_offset:names_offset + len(names)] = names
    for index, name, kind, flags, address, offset, size in (
            (1, 1, 1, 6, TEXT_BASE, text_offset, len(text)),
            (2, 7, 3, 0, 0, names_offset, len(names))):
        struct.pack_into('<6I', out, sh_offset + index * 40, name, kind, flags, address, offset, size)
        struct.pack_into('<I', out, sh_offset + index * 40 + 32, 4)
    return bytes(out)


if __name__ == '__main__':
    with open(sys.argv[1], 'wb') as f:
        f.write(psp_elf(CODE))
