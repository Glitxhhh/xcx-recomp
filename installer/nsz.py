"""NSZ and XCZ: compressed NSP and XCI files, turned back into a plain NSP before the setup reads them.

An NSZ is an NSP whose NCAs are stored as NCZ files, and an XCZ is an XCI whose secure partition holds them. An NCZ
keeps the first 0x4000 bytes of its NCA as they are (the encrypted header), then a section table, then the rest of the
NCA decrypted and compressed with zstd: one stream, or blocks that can each be stored raw. Turning it back means
decompressing and encrypting each section again with the key and counter the table gives. AES-CTR encrypts by XOR with a
key stream, so applying the same key stream the compressor used gives back the original bytes, whatever the section is.

The NSP written here holds the same files in the same order with every .ncz as its .nca. Each NCA's file name is the
first 16 bytes of its SHA-256 in hex, so every rebuilt NCA is checked against its own name; the setup's version and
RomFS hash checks then check the result again.

Format: the NSZ project's own description of it (github.com/nicoboss/nsz, "NCZ" in its README); this is our own code.
"""
import hashlib
import struct
from pathlib import Path

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes


HEADER = 0x4000                 # bytes of the NCA an NCZ keeps unchanged
SECTION_MAGIC = b'NCZSECTN'
BLOCK_MAGIC = b'NCZBLOCK'
CRYPTO_NONE, CRYPTO_CTR, CRYPTO_BKTR = 1, 3, 4
CHUNK = 1 << 22
PFS0_ALIGN = 0x20


class NszError(ValueError):
    pass


def require(ok, message):
    if not ok:
        raise NszError(message)


def compressed_entries(path):
    """The container's entries (name, offset, size) when it holds any .ncz, else None. ValueError: not an NSP/XCI."""
    from extract_exefs import container_entries   # tools/vendor/macnativegames, on sys.path once steps.py is imported
    with path.open('rb') as f:
        _, entries = container_entries(f, path.stat().st_size)
    return entries if any(name.lower().endswith('.ncz') for name, _, _ in entries) else None


def read_at(f, offset, size):
    f.seek(offset)
    data = f.read(size)
    require(len(data) == size, 'the NCZ file is cut short')
    return data


class Ncz:
    """One NCZ inside a container: its section table and where its compressed data starts."""

    def __init__(self, f, offset, size):
        self.f, self.offset, self.size = f, offset, size
        require(size > HEADER + 16, 'the NCZ file is cut short')
        require(read_at(f, offset + HEADER, 8) == SECTION_MAGIC, 'not an NCZ file (no section table)')
        count = struct.unpack('<Q', read_at(f, offset + HEADER + 8, 8))[0]
        require(0 < count < 64, f'NCZ section count {count}')
        table = read_at(f, offset + HEADER + 16, count * 0x40)
        self.sections = []
        for i in range(count):
            start, length, crypto, _ = struct.unpack_from('<QQQQ', table, i * 0x40)
            key, counter = table[i * 0x40 + 0x20:i * 0x40 + 0x30], table[i * 0x40 + 0x30:i * 0x40 + 0x40]
            require(crypto in (CRYPTO_NONE, CRYPTO_CTR, CRYPTO_BKTR), f'NCZ section crypto type {crypto}')
            self.sections.append((start, length, crypto, key, counter))
        self.nca_size = max(start + length for start, length, *_ in self.sections)
        require(self.nca_size > HEADER, 'NCZ sections end inside the header')
        self.data = offset + HEADER + 16 + count * 0x40   # the block header or the zstd stream

    def plain_chunks(self):
        """The decompressed NCA bytes from HEADER to the end, in pieces."""
        import compression.zstd as zstd   # Python 3.14; both release packages carry it
        f, end = self.f, self.offset + self.size
        f.seek(self.data)
        if read_at(f, self.data, 8) == BLOCK_MAGIC:
            version, kind, _, exponent, count = struct.unpack('<BBBBI', read_at(f, self.data + 8, 8))
            total = struct.unpack('<Q', read_at(f, self.data + 16, 8))[0]
            require(version == 2 and kind == 1 and 14 <= exponent <= 32 and count, 'unsupported NCZ block header')
            sizes = struct.unpack(f'<{count}I', read_at(f, self.data + 24, 4 * count))
            block, pos, left = 1 << exponent, self.data + 24 + 4 * count, total
            for stored in sizes:
                want = min(block, left)
                raw = read_at(f, pos, stored)
                pos += stored
                piece = raw if stored >= want else zstd.decompress(raw)
                require(len(piece) == want, 'an NCZ block decompressed to the wrong size')
                left -= want
                yield piece
            require(left == 0 and pos <= end, 'the NCZ blocks do not add up')
            return
        decomp, pos = zstd.ZstdDecompressor(), self.data
        while pos < end and not decomp.eof:
            raw = read_at(f, pos, min(CHUNK, end - pos))
            pos += len(raw)
            piece = decomp.decompress(raw)
            if piece:
                yield piece
        require(decomp.eof, 'the NCZ zstd stream is cut short')

    def cipher_xor(self, start, data):
        """data (NCA bytes at offset start) with every encrypted section's key stream applied."""
        out, end = bytearray(data), start + len(data)
        for s_start, length, crypto, key, counter in self.sections:
            if crypto == CRYPTO_NONE:
                continue
            lo, hi = max(start, s_start), min(end, s_start + length)
            if lo >= hi:
                continue
            block = lo & ~0xf
            iv = counter[:8] + struct.pack('>Q', block >> 4)
            stream = Cipher(algorithms.AES(key), modes.CTR(iv)).encryptor()
            pad = lo - block
            ks = stream.update(bytes(pad + hi - lo))[pad:]
            seg = memoryview(out)[lo - start:hi - start]
            seg[:] = bytes(a ^ b for a, b in zip(seg, ks)) if len(ks) < 64 else \
                (int.from_bytes(seg, 'little') ^ int.from_bytes(ks, 'little')).to_bytes(len(ks), 'little')
        return bytes(out)

    def write_nca(self, out, progress=None):
        """Write the NCA to the open file out; returns its SHA-256 hex."""
        h = hashlib.sha256()
        head = read_at(self.f, self.offset, HEADER)
        out.write(head)
        h.update(head)
        import compression.zstd as zstd
        pos = HEADER
        chunks = self.plain_chunks()
        while True:
            try:
                piece = next(chunks)
            except StopIteration:
                break
            except zstd.ZstdError as e:
                raise NszError(f'the NCZ data is damaged ({e})') from None
            for i in range(0, len(piece), CHUNK):
                part = self.cipher_xor(pos, piece[i:i + CHUNK])
                out.write(part)
                h.update(part)
                pos += len(part)
                if progress:
                    progress(len(part))
        require(pos == self.nca_size, f'the NCZ decompressed to {pos} bytes, its sections say {self.nca_size}')
        return h.hexdigest()


def pfs0_header(names_sizes):
    """A PFS0 header for files of these (name, size), data right after it."""
    table = b''.join(n.encode() + b'\0' for n, _ in names_sizes)
    base = 16 + 0x18 * len(names_sizes)
    pad = -(base + len(table)) % PFS0_ALIGN
    table += b'\0' * pad
    head = struct.pack('<4sIII', b'PFS0', len(names_sizes), len(table), 0)
    data = name_at = 0
    for name, size in names_sizes:
        head += struct.pack('<QQII', data, size, name_at, 0)
        data += size
        name_at += len(name.encode()) + 1
    return head + table


def decompress(src, dst, progress=None):
    """Write src (an NSZ or XCZ) to dst as a plain NSP. NszError when the file is damaged or a rebuilt NCA does not
    match its name. progress(done, total) gets the bytes written."""
    entries = compressed_entries(src)
    require(entries is not None, f'{src.name} holds no NCZ file')
    with src.open('rb') as f:
        plan = []
        for name, offset, size in entries:
            if name.lower().endswith('.ncz'):
                ncz = Ncz(f, offset, size)
                plan.append((name[:-4] + '.nca', ncz.nca_size, ncz, None))
            else:
                plan.append((name, size, None, (offset, size)))
        total = sum(size for _, size, _, _ in plan)
        done = 0

        def step(n):
            nonlocal done
            done += n
            if progress:
                progress(done, total)

        dst.parent.mkdir(parents=True, exist_ok=True)
        part = dst.with_name(dst.name + '.part')
        with part.open('wb') as out:
            out.write(pfs0_header([(n, s) for n, s, _, _ in plan]))
            for name, size, ncz, raw in plan:
                if ncz:
                    digest = ncz.write_nca(out, step)
                    stem = name.split('.', 1)[0].lower()
                    require(len(stem) != 32 or digest[:32] == stem,
                            f'{name} rebuilt from the NCZ does not match its name (SHA-256 {digest[:16]})')
                    continue
                pos, left = raw
                while left:
                    piece = read_at(f, pos, min(CHUNK, left))
                    out.write(piece)
                    pos += len(piece)
                    left -= len(piece)
                    step(len(piece))
    part.replace(dst)
    return dst


def plain_path(path, temp, progress=None):
    """path itself when it is a plain NSP or XCI, else the plain NSP rebuilt from it in temp (kept and reused while the
    source's size and time stay the same)."""
    path = Path(path)
    try:
        if compressed_entries(path) is None:
            return path
    except ValueError:
        return path   # not a container: the setup's own checks say what is wrong with it
    st = path.stat()
    tag = hashlib.sha256(f'{path.resolve()}|{st.st_size}|{st.st_mtime_ns}'.encode()).hexdigest()[:12]
    dst = Path(temp) / 'nsz' / f'{path.stem}-{tag}.nsp'
    if dst.is_file():
        return dst
    for old in (Path(temp) / 'nsz').glob(f'{path.stem}-*.nsp'):
        old.unlink()
    return decompress(path, dst, progress)


def nca_entries(f, entries):
    """The entries of the opened container f with each .ncz named and sized as its .nca: an NCZ starts with its NCA's
    header unchanged, so the header checks (nca_inventory, program_nca, nca_ctr_key) read it in place, and they check
    the size the header records against the entry's, which is the NCA's, not the smaller NCZ's."""
    out = []
    for name, offset, size in entries:
        if name.lower().endswith('.ncz'):
            name, size = name[:-4] + '.nca', Ncz(f, offset, size).nca_size
        out.append((name, offset, size))
    return out


def plain_size(path):
    """The bytes of the plain NSP plain_path() writes for path, 0 when path needs none."""
    try:
        entries = compressed_entries(Path(path))
    except (ValueError, OSError):
        return 0
    if entries is None:
        return 0
    with Path(path).open('rb') as f:
        return sum(Ncz(f, o, s).nca_size if n.lower().endswith('.ncz') else s for n, o, s in entries) + 0x10000
