"""The mod manager: install, remove, turn on and turn off mods in an install folder's mods/.

    python -m installer.mods --dest DIR list                 installed mods and the library, as JSON
    python -m installer.mods --dest DIR install ID           from the library (github.com/jmars125/xcx-recomp-mods)
    python -m installer.mods --dest DIR add-zip FILE [--id ID]   a game-file mod zip downloaded by hand
    python -m installer.mods --dest DIR remove|enable|disable ID

Two kinds of mod, as the library's index.json lists them:
  lua    mods/<id>/ with mod.ini and main.lua, run by runtime/mods.c. On when listed in mods/enabled.txt; installed
         ones are also listed in mods/catalog.txt so the game's Mods page shows them while off. Read at start-up.
  romfs  mods/<id>/romfs/<game path>, files in place of the game's own (runtime/romfs_mods.c). On when listed in
         mods/romfs-enabled.txt, which the game reads again twice a second, so they switch while it runs. Earlier
         lines win when two mods have the same file; a mod turned on goes first.
A game-file mod comes from its author's page as a zip in the Switch layout: romfs/<path>, or romfs/mod/<path> for mods
made for the XCXDE mod loader, possibly inside one top folder. exefs/ (Switch code) is not used. The zip must match the
library's size and SHA-256 before anything is unpacked.

mods/library.json records what this manager installed (kind, version, name) so updates and removals know the kind.
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile
import time
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

LIBRARY = 'https://raw.githubusercontent.com/jmars125/xcx-recomp-mods/main/'
INDEX_FORMAT = 2
AGENT = 'XCX-Setup-mods'
UNPACK_LIMIT = 16 << 30          # bytes a game-file mod may unpack to
LUA_FILES = {'mod.ini', 'main.lua', 'info.ini', 'README.md', 'LICENSE', 'LICENSE.txt'}
CHUNK = 1 << 20


class ModError(Exception):
    """A failure to show the player; the text is English, the window maps the id to its catalog."""

    def __init__(self, id, **params):
        super().__init__(id)
        self.id, self.params = id, params


def valid_id(mid):
    return (isinstance(mid, str) and 0 < len(mid) <= 64 and 'a' <= mid[0] <= 'z'
            and all('a' <= ch <= 'z' or '0' <= ch <= '9' or ch in '._-' for ch in mid))


# ---- files ---------------------------------------------------------------------------------------------------------

def mods_dir(dest):
    return Path(dest) / 'mods'


def read_list(path):
    try:
        text = path.read_text(encoding='utf-8')
    except FileNotFoundError:
        return []
    return [line.strip() for line in text.splitlines() if line.strip() and not line.startswith('#')]


def write_text(path, text):
    """Writes through a temp file and a rename, so the game never reads half a list. The game may hold the file open
    for a moment while it reads it; the rename is tried again then."""
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + '.new')
    tmp.write_text(text, encoding='utf-8', newline='\n')
    for attempt in range(20):
        try:
            os.replace(tmp, path)
            return
        except PermissionError:
            if attempt == 19:
                raise
            time.sleep(0.05)


def write_list(path, ids):
    write_text(path, ''.join(i + '\n' for i in ids))


def load_record(root):
    try:
        data = json.loads((root / 'library.json').read_text(encoding='utf-8'))
        return data if isinstance(data, dict) else {}
    except (FileNotFoundError, ValueError):
        return {}


def save_record(root, record):
    write_text(root / 'library.json', json.dumps(record, indent=1, sort_keys=True) + '\n')


def kind_of(root, mid, record=None):
    rec = (record or load_record(root)).get(mid)
    if rec:
        return rec.get('kind')
    folder = root / mid
    if (folder / 'romfs').is_dir():
        return 'romfs'
    if (folder / 'mod.ini').is_file():
        return 'lua'
    return None


LISTS = {'romfs': 'romfs-enabled.txt', 'lua': 'enabled.txt'}


def installed(dest):
    """[{id, kind, version, name, enabled}] for every mod folder, the manager's or one put there by hand."""
    root = mods_dir(dest)
    if not root.is_dir():
        return []
    record = load_record(root)
    on = {k: set(read_list(root / f)) for k, f in LISTS.items()}
    out = []
    for folder in sorted(p for p in root.iterdir() if p.is_dir() and valid_id(p.name)):
        kind = kind_of(root, folder.name, record)
        if not kind:
            continue
        rec = dict(record.get(folder.name, {}), **read_info(folder))   # library.ini is the newer word
        out.append({'id': folder.name, 'kind': kind, 'version': rec.get('version', ''),
                    'name': rec.get('name') or folder.name, 'enabled': folder.name in on.get(kind, ())})
    return out


# ---- library -------------------------------------------------------------------------------------------------------

def opener(url):
    if not url.startswith('https://'):
        raise ModError('mods.bad_link', url=url)
    return urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': AGENT}), timeout=60)


def fetch_index(source=None):
    """The library's entries. source: a URL or a local index.json (tests); default the library on GitHub."""
    try:
        if source and not source.startswith('https://'):
            data = json.loads(Path(source).read_text(encoding='utf-8'))
        else:
            with opener(source or LIBRARY + 'index.json') as r:
                data = json.loads(r.read(8 << 20).decode('utf-8'))
    except (OSError, ValueError) as e:
        raise ModError('mods.index_failed', detail=str(e)) from e
    if not isinstance(data, dict) or data.get('format') != INDEX_FORMAT or not isinstance(data.get('mods'), list):
        raise ModError('mods.index_format')
    return [m for m in data['mods'] if isinstance(m, dict) and valid_id(m.get('id')) and m.get('kind') in LISTS]


def download(url, size, sha256, out, progress=None):
    """Downloads url to out, which must come to exactly size bytes with the given SHA-256."""
    h, got = hashlib.sha256(), 0
    with opener(url) as r, open(out, 'wb') as f:
        while True:
            block = r.read(CHUNK)
            if not block:
                break
            got += len(block)
            if got > size:
                raise ModError('mods.download_mismatch', url=url)
            h.update(block)
            f.write(block)
            if progress:
                progress(got, size)
    if got != size or h.hexdigest() != sha256:
        raise ModError('mods.download_mismatch', url=url)


# ---- unpacking -----------------------------------------------------------------------------------------------------

def romfs_members(z):
    """[(member, game path)] for the game files in a mod zip. The game path follows the last 'romfs' folder, without
    a leading 'mod/' (the XCXDE mod loader's layout)."""
    out = []
    for info in z.infolist():
        name = info.filename.replace('\\', '/')
        parts = PurePosixPath(name).parts
        if info.is_dir() or not parts:
            continue
        mode = (info.external_attr >> 16) & 0o170000
        if mode == 0o120000 or name.startswith('/') or ':' in parts[0] or '..' in parts:
            raise ModError('mods.zip_unsafe', name=name)
        low = [p.lower() for p in parts]
        if 'romfs' not in low:
            continue
        rest = parts[len(low) - low[::-1].index('romfs'):]
        if rest and rest[0].lower() == 'mod' and len(rest) > 1:
            rest = rest[1:]
        if rest:
            out.append((info, '/'.join(rest)))
    return out


def unpack_romfs(zip_path, folder):
    """Unpacks the zip's game files into folder/romfs/; folder must not exist yet."""
    try:
        z = zipfile.ZipFile(zip_path)
    except (OSError, zipfile.BadZipFile) as e:
        raise ModError('mods.zip_bad', detail=str(e)) from e
    with z:
        members = romfs_members(z)
        if not members:
            raise ModError('mods.zip_no_romfs')
        if sum(i.file_size for i, _ in members) > UNPACK_LIMIT:
            raise ModError('mods.zip_too_big')
        seen = set()
        for info, game_path in members:
            key = game_path.lower()
            if key in seen:
                raise ModError('mods.zip_duplicate', name=game_path)
            seen.add(key)
            target = folder / 'romfs' / game_path
            target.parent.mkdir(parents=True, exist_ok=True)
            with z.open(info) as src, open(target, 'wb') as dst:
                shutil.copyfileobj(src, dst, CHUNK)
    return len(members)


# ---- operations ----------------------------------------------------------------------------------------------------

def write_info(staged, kind, name, author='', version='', source=''):
    """<id>/library.ini: name, author, version and kind of an installed mod. The game's F1 menu writes the same file
    when it installs a mod (runtime/mod_library.c, write_library_ini), so each manager sees the other's installs."""
    clean = lambda v: ' '.join(str(v).split())[:90]
    text = ''.join(f'{k} = {clean(v)}\n' for k, v in (('name', name), ('author', author), ('version', version),
                                                       ('kind', kind), ('source', source)))
    (staged / 'library.ini').write_text(text, encoding='utf-8', newline='\n')


def read_info(folder):
    """The keys of <id>/library.ini; {} when there is none."""
    try:
        text = (folder / 'library.ini').read_text(encoding='utf-8')
    except (OSError, UnicodeDecodeError):
        return {}
    out = {}
    for line in text.splitlines():
        key, eq, value = line.partition('=')
        if eq:
            out[key.strip()] = value.strip()
    return out


def place(root, mid, staged):
    """Moves a staged mod folder to mods/<id>, in place of an older copy."""
    final = root / mid
    old = root / f'.old-{mid}'
    if old.exists():
        shutil.rmtree(old)
    if final.exists():
        os.replace(final, old)
    os.replace(staged, final)
    if old.exists():
        shutil.rmtree(old, ignore_errors=True)


def install(dest, entry, progress=None):
    """Installs or updates a library entry. A mod that was on stays on; a new one starts off."""
    mid, kind = entry['id'], entry['kind']
    root = mods_dir(dest)
    root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=root, prefix='.install-') as tmp:
        staged = Path(tmp) / mid
        staged.mkdir()
        if kind == 'romfs':
            zpath = Path(tmp) / 'mod.zip'
            download(entry['url'], int(entry['size']), entry['sha256'], zpath, progress)
            unpack_romfs(zpath, staged)
        else:
            files = entry.get('files') or []
            if not files or any(f.get('name') not in LUA_FILES for f in files):
                raise ModError('mods.index_format')
            for i, f in enumerate(files):
                download(LIBRARY + entry['path'] + '/' + f['name'], int(f['size']), f['sha256'], staged / f['name'])
                if progress:
                    progress(i + 1, len(files))
        write_info(staged, kind, entry.get('name', mid), entry.get('author', ''), entry.get('version', ''),
                   entry.get('homepage') or 'library')
        place(root, mid, staged)
    record = load_record(root)
    record[mid] = {'kind': kind, 'version': entry.get('version', ''), 'name': entry.get('name', mid),
                   'source': entry.get('homepage') or 'library'}
    save_record(root, record)
    if kind == 'lua':
        catalog = read_list(root / 'catalog.txt')
        if mid not in catalog:
            write_list(root / 'catalog.txt', catalog + [mid])


def add_zip(dest, zip_path, mid=None, name=None):
    """Installs a game-file mod zip the player downloaded; the id comes from the file name when not given."""
    zip_path = Path(zip_path)
    if not mid:
        stem = ''.join(ch if ch.isalnum() or ch in '._-' else '-' for ch in zip_path.stem.lower()).strip('-._')
        mid = stem if stem and 'a' <= stem[0] <= 'z' else f'mod-{stem}'
        mid = mid[:64]
    if not valid_id(mid):
        raise ModError('mods.bad_id', id=mid)
    root = mods_dir(dest)
    root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=root, prefix='.install-') as tmp:
        staged = Path(tmp) / mid
        staged.mkdir()
        unpack_romfs(zip_path, staged)
        write_info(staged, 'romfs', name or zip_path.stem, source='zip')
        place(root, mid, staged)
    record = load_record(root)
    record[mid] = {'kind': 'romfs', 'version': '', 'name': name or zip_path.stem, 'source': 'zip'}
    save_record(root, record)
    return mid


def set_enabled(dest, mid, on):
    root = mods_dir(dest)
    kind = kind_of(root, mid)
    if not kind or not (root / mid).is_dir():
        raise ModError('mods.not_installed', id=mid)
    path = root / LISTS[kind]
    ids = [i for i in read_list(path) if i != mid]
    write_list(path, ([mid] + ids) if on else ids)


def remove(dest, mid):
    root = mods_dir(dest)
    if not valid_id(mid) or not (root / mid).is_dir():
        raise ModError('mods.not_installed', id=mid)
    kind = kind_of(root, mid)
    if kind:
        set_enabled(dest, mid, False)
    catalog = read_list(root / 'catalog.txt')
    if mid in catalog:
        write_list(root / 'catalog.txt', [i for i in catalog if i != mid])
    record = load_record(root)
    if record.pop(mid, None) is not None:
        save_record(root, record)
    shutil.rmtree(root / mid)


def status(dest, index):
    """The library entries joined with what is installed: [{...entry, installed, enabled, update}] plus mods that are
    installed but not in the library."""
    have = {m['id']: m for m in installed(dest)}
    out = []
    for e in index:
        m = have.pop(e['id'], None)
        out.append(dict(e, installed=bool(m), enabled=bool(m and m['enabled']),
                        update=bool(m and m['version'] != e.get('version', ''))))
    for m in have.values():
        out.append(dict(m, installed=True, update=False, description='', author='', homepage='', notes=''))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(prog='python -m installer.mods', description=__doc__.splitlines()[0])
    ap.add_argument('--dest', type=Path, required=True, help='install folder (the one with xcx.exe)')
    ap.add_argument('--index', help='library index.json, a URL or a file (default: the library on GitHub)')
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('list')
    for c in ('install', 'remove', 'enable', 'disable'):
        sub.add_parser(c).add_argument('id')
    z = sub.add_parser('add-zip')
    z.add_argument('zip', type=Path)
    z.add_argument('--id')
    args = ap.parse_args(argv)
    try:
        if args.cmd == 'list':
            print(json.dumps(status(args.dest, fetch_index(args.index)), indent=1))
        elif args.cmd == 'install':
            entry = next((e for e in fetch_index(args.index) if e['id'] == args.id), None)
            if not entry:
                raise ModError('mods.not_in_library', id=args.id)
            install(args.dest, entry)
            print(f'installed {args.id}')
        elif args.cmd == 'add-zip':
            print(f'installed {add_zip(args.dest, args.zip, args.id)}')
        elif args.cmd == 'remove':
            remove(args.dest, args.id)
            print(f'removed {args.id}')
        else:
            set_enabled(args.dest, args.id, args.cmd == 'enable')
            print(f'{args.cmd}d {args.id}')
    except ModError as e:
        print(f'{e.id} {json.dumps(e.params)}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
