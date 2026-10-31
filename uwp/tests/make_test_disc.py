"""Write a synthetic PSP-style disc (no game data) as ISO and CSO v1.

    python uwp/tests/make_test_disc.py <out_dir>

Produces test.iso (PSP_GAME/PARAM.SFO with DISC_ID ULUS10440, a multi-sector
directory, a large incompressible file and compressible files) and test.cso.
CHD versions are made from test.iso with chdman by the CI job.
"""
import io
import os
from pathlib import Path
import random
import struct
import sys
import zlib

import pycdlib


def sfo(entries):
    keys = vals = idx = b''
    for key, value in entries:
        data = value.encode() + b'\0'
        cap = (len(data) + 3) & ~3
        idx += struct.pack('<HHIII', len(keys), 0x204, len(data), cap, len(vals))
        keys += key.encode() + b'\0'
        vals += data.ljust(cap, b'\0')
    keys = keys.ljust((len(keys) + 3) & ~3, b'\0')
    key_offset = 20 + len(idx)
    return struct.pack('<5I', 0x46535000, 0x101, key_offset, key_offset + len(keys), len(entries)) + idx + keys + vals


def make_iso(path):
    rng = random.Random(10440)
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=3)
    for directory in ('/PSP_GAME', '/PSP_GAME/SYSDIR', '/PSP_GAME/USRDIR'):
        iso.add_directory(directory)
    files = {'/PSP_GAME/PARAM.SFO;1': sfo([('TITLE', 'TEST DISC'), ('DISC_ID', 'ULUS10440')]),
             '/PSP_GAME/SYSDIR/EBOOT.BIN;1': b'\x7fELF' + bytes(60),
             '/PSP_GAME/USRDIR/RANDOM.BIN;1': rng.randbytes(3 * 1024 * 1024 + 123),
             '/PSP_GAME/USRDIR/ZEROS.BIN;1': bytes(2 * 1024 * 1024)}
    for i in range(80):  # forces a multi-sector directory
        files[f'/PSP_GAME/USRDIR/F{i:05d}.DAT;1'] = (b'%05d' % i) * (i * 7)
    for name, data in files.items():
        iso.add_fp(io.BytesIO(data), len(data), name)
    iso.write(str(path))
    iso.close()


def make_cso(iso_path, cso_path, block=2048):
    data = Path(iso_path).read_bytes()
    blocks = (len(data) + block - 1) // block
    header_size = 24 + (blocks + 1) * 4
    index, body = [], bytearray()
    for i in range(blocks):
        raw = data[i * block:(i + 1) * block]
        packer = zlib.compressobj(9, zlib.DEFLATED, -15)
        packed = packer.compress(raw) + packer.flush()
        offset = header_size + len(body)
        if len(packed) >= len(raw):
            index.append(offset | 0x80000000)
            body += raw
        else:
            index.append(offset)
            body += packed
    index.append(header_size + len(body))
    header = b'CISO' + struct.pack('<IQIBB2x', 24, len(data), block, 1, 0)
    Path(cso_path).write_bytes(header + struct.pack(f'<{len(index)}I', *index) + body)


if __name__ == '__main__':
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    make_iso(out / 'test.iso')
    make_cso(out / 'test.iso', out / 'test.cso')
    print('wrote', out / 'test.iso', out / 'test.cso', os.path.getsize(out / 'test.cso'), 'bytes')
