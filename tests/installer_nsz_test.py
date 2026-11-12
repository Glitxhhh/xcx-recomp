"""installer/nsz.py: NSZ files written here in both NCZ layouts (one zstd stream, and blocks with some stored raw) turn
back into the NSP they came from, byte for byte. Synthetic NCAs, no game data. Needs Python 3.14 (compression.zstd)."""
import hashlib
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools'), str(ROOT / 'tools' / 'vendor' / 'macnativegames')]

import compression.zstd as zstd  # noqa: E402
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes  # noqa: E402

from installer import nsz  # noqa: E402


def key_stream(key, counter, start, length):
    block = start & ~0xf
    iv = counter[:8] + struct.pack('>Q', block >> 4)
    return Cipher(algorithms.AES(key), modes.CTR(iv)).encryptor().update(bytes(start - block + length))[start - block:]


def fake_nca(size):
    """(NCA bytes, NCZ section table) with two encrypted sections and a plain gap between them."""
    nca = bytearray(os.urandom(size))
    nca[HEADER:HEADER + 0x2000] = bytes(0x2000) * 1   # compressible data
    cut = HEADER + (size - HEADER) // 2 + 5
    sections = [(HEADER, cut - HEADER, nsz.CRYPTO_CTR, os.urandom(16), os.urandom(8) + bytes(8)),
                (cut, 0x1000, nsz.CRYPTO_NONE, bytes(16), bytes(16)),
                (cut + 0x1000, size - cut - 0x1000, nsz.CRYPTO_BKTR, os.urandom(16), os.urandom(8) + bytes(8))]
    return bytes(nca), sections


HEADER = nsz.HEADER


def to_ncz(nca, sections, blocks=None):
    plain = bytearray(nca[HEADER:])
    for start, length, crypto, key, counter in sections:
        if crypto != nsz.CRYPTO_NONE:
            ks = key_stream(key, counter, start, length)
            for i in range(length):
                plain[start - HEADER + i] ^= ks[i]
    out = bytearray(nca[:HEADER]) + nsz.SECTION_MAGIC + struct.pack('<Q', len(sections))
    for start, length, crypto, key, counter in sections:
        out += struct.pack('<QQQQ', start, length, crypto, 0) + key + counter
    if blocks is None:
        return bytes(out + zstd.compress(bytes(plain)))
    exponent = blocks
    size = 1 << exponent
    pieces = [bytes(plain[i:i + size]) for i in range(0, len(plain), size)]
    stored = [p if i % 2 else zstd.compress(p) for i, p in enumerate(pieces)]   # odd blocks stored raw
    stored = [s if len(s) < len(p) or s is p else p for s, p in zip(stored, pieces)]
    out += nsz.BLOCK_MAGIC + struct.pack('<BBBBIQ', 2, 1, 0, exponent, len(pieces), len(plain))
    out += struct.pack(f'<{len(pieces)}I', *map(len, stored)) + b''.join(stored)
    return bytes(out)


def pfs0(files):
    head = nsz.pfs0_header([(n, len(d)) for n, d in files])
    return head + b''.join(d for _, d in files)


class NszTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.ncas = []
        for size in (0x9000 + 0x123, 0x21000):
            nca, sections = fake_nca(size)
            name = hashlib.sha256(nca).hexdigest()[:32] + '.nca'
            self.ncas.append((name, nca, sections))
        self.ticket = ('0100.tik', os.urandom(0x2c0))
        self.nsp = pfs0([(n, d) for n, d, _ in self.ncas] + [self.ticket])

    def make(self, blocks):
        files = [(n[:-4] + '.ncz', to_ncz(d, s, blocks)) for n, d, s in self.ncas] + [self.ticket]
        path = self.dir / f'game-{blocks}.nsz'
        path.write_bytes(pfs0(files))
        return path

    def test_stream_and_blocks_give_the_nsp_back(self):
        for blocks in (None, 14):
            with self.subTest(blocks=blocks):
                src = self.make(blocks)
                self.assertIsNotNone(nsz.compressed_entries(src))
                out = nsz.decompress(src, self.dir / f'out-{blocks}.nsp')
                self.assertEqual(out.read_bytes(), self.nsp)
                self.assertEqual(nsz.plain_size(src) - 0x10000, sum(len(d) for _, d, _ in self.ncas) + len(self.ticket[1]))

    def test_headers_read_in_place(self):
        src = self.make(None)
        with src.open('rb') as f:
            entries = [(n, s) for n, _, s in nsz.nca_entries(f, nsz.compressed_entries(src))]
        # named and sized as the NCAs: the header checks compare the size an NCA header records with its entry's
        self.assertEqual(entries, [(n, len(d)) for n, d, _ in self.ncas] + [(self.ticket[0], len(self.ticket[1]))])

    def test_damaged_ncz_is_refused(self):
        name, nca, sections = self.ncas[0]
        ncz = bytearray(to_ncz(nca, sections))
        ncz[-20] ^= 0xff   # inside the zstd stream: its checksum or the data no longer matches
        src = self.dir / 'bad.nsz'
        src.write_bytes(pfs0([(name[:-4] + '.ncz', bytes(ncz))]))
        with self.assertRaises(nsz.NszError):
            nsz.decompress(src, self.dir / 'bad.nsp')

    def test_plain_files_pass_through_and_rebuilds_are_reused(self):
        plain = self.dir / 'game.nsp'
        plain.write_bytes(self.nsp)
        self.assertEqual(nsz.plain_path(plain, self.dir / 'tmp'), plain)
        src = self.make(14)
        first = nsz.plain_path(src, self.dir / 'tmp')
        self.assertNotEqual(first, src)
        mtime = first.stat().st_mtime_ns
        self.assertEqual(nsz.plain_path(src, self.dir / 'tmp'), first)
        self.assertEqual(first.stat().st_mtime_ns, mtime)


if __name__ == '__main__':
    unittest.main()
