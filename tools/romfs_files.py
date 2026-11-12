"""Per-file hashes of a verified RomFS: the table the setup checks a folder of loose RomFS files against.

Reads <romfs>/romfs.bin and its index.json (a verified extraction, tools/extract.py) and writes
tools/data/romfs-files-xcx-<version>.json: for each file the SHA-256 of its path (so no file name of the game is
stored), its size and the SHA-256 of its bytes. installer/steps.py (Extract, loose RomFS folders) packs a player's
loose files into a new romfs.bin only when every file of this table is there with this size and hash.

    python tools/romfs_files.py --romfs <install>/romfs --version 2.0.0
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHUNK = 1 << 22


def path_key(path):
    """The table's key of a RomFS path: SHA-256 of its UTF-8 bytes, '/' separated, no leading '/'."""
    return hashlib.sha256(path.strip('/').encode()).hexdigest()


def file_hashes(image, files):
    out = {}
    with image.open('rb') as f:
        for n, e in enumerate(sorted(files, key=lambda e: e['offset'])):
            f.seek(e['offset'])
            h, left = hashlib.sha256(), e['size']
            while left:
                piece = f.read(min(CHUNK, left))
                if not piece:
                    sys.exit(f'romfs_files: romfs.bin ends inside entry {n}')
                h.update(piece)
                left -= len(piece)
            out[path_key(e['path'])] = [e['size'], h.hexdigest()]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--romfs', type=Path, required=True, help='folder with romfs.bin and index.json')
    ap.add_argument('--version', required=True, help='game version of the image (2.0.0)')
    a = ap.parse_args()
    report = json.loads((a.romfs / 'index.json').read_text(encoding='utf-8'))
    expected = json.loads((ROOT / 'tools' / 'data' / f'expected-xcx-{a.version}.json').read_text(encoding='utf-8'))
    if report.get('romfs_sha256') != expected['romfs']['sha256']:
        sys.exit('romfs_files: index.json does not record a verified extraction of that version')
    table = {'version': a.version, 'romfs_sha256': expected['romfs']['sha256'],
             'files': dict(sorted(file_hashes(a.romfs / 'romfs.bin', report['files']).items()))}
    out = ROOT / 'tools' / 'data' / f'romfs-files-xcx-{a.version}.json'
    out.write_text(json.dumps(table, indent=1) + '\n', encoding='utf-8')
    print(f'romfs_files: {out.relative_to(ROOT).as_posix()}: {len(table["files"])} files')


if __name__ == '__main__':
    main()
