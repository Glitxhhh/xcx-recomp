"""--game with an extracted game: a folder holding exefs/ and the RomFS of the updated game instead of NSP/XCI files.

The folder (the names are the ones hactool, emulators' dump menus and an earlier install of ours use):

    exefs/              main, main.npdm, rtld, sdk, subsdk* of the 2.0.0 update
    romfs.bin           the RomFS image of the base game merged with the update (hactool --section1 with --basenca),
      or romfs/romfs.bin
      or romfs/         the RomFS as loose files (an emulator's "Dump RomFS" with the update installed)

The ExeFS is checked against tools/data/expected-xcx-<version>.json module by module, as for a game file. An image is
copied (hard-linked when it is on the install folder's disk) and must match the expected RomFS hash. Loose files are
checked one by one against tools/data/romfs-files-xcx-<version>.json (tools/romfs_files.py) and packed into a new
romfs.bin with its own index.json: xcx.exe reads the RomFS only at the offsets of its index (runtime/romfs.c), so the
image need not be the cartridge's byte for byte. index.json then records the image's own hash and, as source_sha256,
the expected one whose files it holds (tools/extract.py romfs_verified).
"""
import hashlib
import json
import os
import shutil
from pathlib import Path

from .runner import InstallError

CHUNK = 1 << 22
ALIGN = 0x10   # file data offsets in the packed image, as in the cartridge's
LOOSE = 'loose files; each file matched romfs-files-xcx-{version}.json'


def find(folder):
    """{'exefs', 'image' or 'loose'} of an extracted game in folder, None when folder is not one."""
    folder = Path(folder)
    exefs = folder / 'exefs'
    if not folder.is_dir() or not (exefs / 'main').is_file():
        return None
    for image in (folder / 'romfs.bin', folder / 'romfs' / 'romfs.bin'):
        if image.is_file():
            return {'exefs': exefs, 'image': image}
    loose = folder / 'romfs'
    if loose.is_dir() and any(loose.iterdir()):
        return {'exefs': exefs, 'loose': loose}
    return None


def table_of(layout, version):
    path = layout.tools / 'data' / f'romfs-files-xcx-{version}.json'
    return json.loads(path.read_text(encoding='utf-8')) if path.is_file() else None


def path_key(path):
    """tools/romfs_files.py path_key."""
    return hashlib.sha256(path.strip('/').encode()).hexdigest()


def loose_files(root):
    """(RomFS path, file) of every file under root, in path order."""
    return sorted(((p.relative_to(root).as_posix(), p) for p in root.rglob('*') if p.is_file()),
                  key=lambda e: e[0].encode())


def check_loose(root, table, progress=None):
    """The loose files when every file of the table is there with its size and hash. InstallError naming how many
    differ or are missing; extra files (an emulator's own) are left out of the image."""
    files, want = loose_files(root), table['files']
    found, bad = {}, 0
    total = sum(size for size, _ in want.values())
    done = 0
    for path, p in files:
        key = path_key(path)
        if key not in want:
            continue
        size, digest = want[key]
        if p.stat().st_size != size:
            bad += 1
            continue
        h = hashlib.sha256()
        with p.open('rb') as f:
            while piece := f.read(CHUNK):
                h.update(piece)
                done += len(piece)
                if progress:
                    progress(done, total)
        if h.hexdigest() != digest:
            bad += 1
            continue
        found[key] = (path, p, size)
    missing = len(want) - len(found) - bad
    if bad or missing:
        raise InstallError('check.romfs_files_wrong', path=str(root), bad=bad, missing=missing, total=len(want),
                           version=table['version'])
    return sorted(found.values(), key=lambda e: e[0].encode())


def pack_loose(files, table, out, progress=None):
    """Write out/romfs.bin from the checked loose files and out/index.json describing it."""
    out.mkdir(parents=True, exist_ok=True)
    image, entries = out / 'romfs.bin', []
    part = out / 'romfs.bin.part'
    h, pos = hashlib.sha256(), 0
    total = sum(size for _, _, size in files)
    with part.open('wb') as w:
        for path, p, size in files:
            pad = -pos % ALIGN
            if pad:
                w.write(bytes(pad))
                h.update(bytes(pad))
                pos += pad
            entries.append({'path': path, 'offset': pos, 'size': size})
            with p.open('rb') as f:
                while piece := f.read(CHUNK):
                    w.write(piece)
                    h.update(piece)
                    pos += len(piece)
                    if progress:
                        progress(sum(e['size'] for e in entries[:-1]) + (pos - entries[-1]['offset']), total)
    part.replace(image)
    report = dict(status='pass', romfs_size=pos, romfs_sha256=h.hexdigest(), files=entries,
                  integrity=LOOSE.format(version=table['version']), source_sha256=table['romfs_sha256'])
    (out / 'index.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def take_image(src, out, index_romfs, progress=None):
    """out/romfs.bin as src (hard link on the same disk, else a copy) and out/index.json from its own file table.
    Returns the report; the caller compares its hash with the expected one."""
    out.mkdir(parents=True, exist_ok=True)
    image = out / 'romfs.bin'
    if image.exists():
        image.unlink()
    try:
        os.link(src, image)
    except OSError:
        part = out / 'romfs.bin.part'
        shutil.copyfile(src, part)
        part.replace(image)
    h, total, done = hashlib.sha256(), image.stat().st_size, 0
    with image.open('rb') as f:
        while piece := f.read(CHUNK):
            h.update(piece)
            done += len(piece)
            if progress:
                progress(done, total)
    report = dict(status='pass', romfs_size=total, romfs_sha256=h.hexdigest(), files=index_romfs(image),
                  integrity='image from an extracted game folder; SHA-256 matched')
    (out / 'index.json').write_text(json.dumps(report, indent=2) + '\n')
    return report
