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
made for the XCXDE mod loader, possibly inside top folders (atmosphere/contents/<title id>/romfs). exefs/ (Switch code)
is not used. The zip must match the library's size and SHA-256 before anything is unpacked. A mod unpacked into mods/
by hand in any of those layouts is moved to mods/<id>/romfs the next time the mods are listed (normalise).

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


def slug(text):
    """A mod id made from a folder or file name: ASCII letters and digits lowercased, anything else a '-', 'mod-' in
    front when it does not start with a letter, at most 64 characters. runtime/mod_library.c, slug, is the same."""
    s = ''.join(ch if ch.isascii() and (ch.isalnum() or ch in '._') else '-' for ch in text.lower())
    while '--' in s:
        s = s.replace('--', '-')
    s = s.strip('-._')
    s = s if s and 'a' <= s[0] <= 'z' else f'mod-{s}'.rstrip('-')
    return s[:64].rstrip('-._')


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


# ---- mods put in mods/ by hand ---------------------------------------------------------------------------------------
# A player unpacks a mod's download into mods/ as it comes: a folder named after the mod ("Unburger Patch"), the
# Switch SD card layout (atmosphere/contents/<title id>/romfs, <title id>/romfs, contents/<title id>/romfs) or a bare
# romfs/. normalise moves each such folder to mods/<id>/romfs, the one layout the game reads, and names every folder
# it leaves alone with the reason. runtime/mod_library.c, normalise, does the same when the F1 Mods tab reads the
# folder; the two follow the same rules, so either one's result looks the same to the other.

TITLE_ID = '0100453019aa8000'     # Xenoblade Chronicles X: Definitive Edition
SEARCH_DEPTH = 4                  # folders below the top one in which a romfs folder is looked for
WRAPPERS = {'romfs', 'atmosphere', 'contents'}   # top folder names that say nothing about the mod


def is_title_id(name):
    return len(name) == 16 and all(ch in '0123456789abcdefABCDEF' for ch in name)


def find_romfs(folder, depth=SEARCH_DEPTH):
    """The romfs folders below folder (not inside one another), nearest first; at most two are looked for."""
    found, level = [], [folder]
    for _ in range(depth):
        nxt = []
        for d in level:
            try:
                subs = sorted((p for p in d.iterdir() if p.is_dir() and not p.is_symlink()), key=lambda p: p.name)
            except OSError:
                continue
            for p in subs:
                (found if p.name.lower() == 'romfs' else nxt).append(p)
        if found or not nxt:
            break
        level = nxt
    return found


def other_game(rel_parts):
    """The title id of another game in a path below mods/, else None."""
    return next((p for p in rel_parts if is_title_id(p) and p.lower() != TITLE_ID), None)


def free_id(root, base, folder=None):
    """base, or base-2, base-3 ...: the first name not taken in root. folder itself does not count, so on a file
    system that ignores case "Uncensor" becomes "uncensor"."""
    def taken(mid):
        p = root / mid
        if folder is not None and p.exists() and os.path.samefile(p, folder):
            return False
        return p.exists() or p.is_symlink()
    mid, n = base, 2
    while taken(mid):
        suffix = f'-{n}'
        mid, n = base[:64 - len(suffix)] + suffix, n + 1
    return mid


def is_loader(romfs):
    """Whether romfs holds just a folder named mod: the XCXDE mod loader's layout, romfs/mod/<game path>."""
    try:
        entries = list(romfs.iterdir())
    except OSError:
        return False
    return len(entries) == 1 and entries[0].is_dir() and entries[0].name.lower() == 'mod'


def normalise(root):
    """Moves the mods put in root by hand to root/<id>/romfs. Returns [(catalog id, params)]: a 'mods.moved' line for
    each folder moved, a 'mods.skip_*' line for each one left alone."""
    notes = []
    try:
        names = sorted(p.name for p in root.iterdir())
    except OSError:
        return notes
    for name in names:
        top = root / name
        if name.startswith('.') or top.is_symlink():
            continue
        if top.is_file():
            if name.lower().endswith('.zip'):
                notes.append(('mods.skip_zip', {'folder': name}))
            continue
        if not top.is_dir():
            continue
        if valid_id(name) and (top / 'mod.ini').is_file():
            continue
        if (top / 'mod.ini').is_file():
            notes.append(('mods.skip_lua_name', {'folder': name}))
            continue
        if name.lower() == 'romfs':
            romfs = top
        else:
            found = find_romfs(top)
            if not found:
                notes.append(('mods.skip_no_romfs', {'folder': name}))
                continue
            if len(found) > 1:
                notes.append(('mods.skip_two_romfs', {'folder': name}))
                continue
            romfs = found[0]
        rel = romfs.relative_to(root).parts
        title = other_game(rel)
        if title:
            notes.append(('mods.skip_other_game', {'folder': name, 'title': title}))
            continue
        if valid_id(name) and rel == (name, 'romfs') and not is_loader(romfs):
            continue   # already mods/<id>/romfs
        generic = name.lower() in WRAPPERS or is_title_id(name)
        mid = free_id(root, 'romfs-mod') if generic else name if valid_id(name) else free_id(root, slug(name), top)
        final = root / mid
        try:
            if name.lower() == 'romfs':          # the folder is the mod's romfs
                final.mkdir()
                os.replace(top, final / 'romfs')
            elif generic:                         # atmosphere/..., contents/..., <title id>/...: kept whole inside
                final.mkdir()
                os.replace(top, final / name)
                os.replace(final.joinpath(*rel), final / 'romfs')
            else:                                 # a folder named after the mod
                if mid != name:
                    os.replace(top, final)
                if rel[1:] != ('romfs',):
                    os.replace(final.joinpath(*rel[1:]), final / 'romfs')
            if is_loader(final / 'romfs'):
                tmp = final / '.romfs-loader'
                os.replace(final / 'romfs', tmp)
                os.replace(next(tmp.iterdir()), final / 'romfs')
                tmp.rmdir()
            if not (final / 'library.ini').exists():
                write_info(final, 'romfs', mid if generic or name.lower() == 'romfs' else name, source='folder')
        except OSError as e:
            notes.append(('mods.skip_move_failed', {'folder': name, 'detail': str(e)}))
            continue
        notes.append(('mods.moved', {'folder': name, 'id': mid}))
    return notes


def installed(dest, notes=None):
    """[{id, kind, version, name, enabled}] for every mod folder, the manager's or one put there by hand. Folders put
    there in another layout are moved first (normalise); notes, a list, gets its lines."""
    root = mods_dir(dest)
    if not root.is_dir():
        return []
    found = normalise(root)
    if notes is not None:
        notes.extend(found)
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
        title = other_game(parts[:len(low) - low[::-1].index('romfs')])
        if title:
            raise ModError('mods.zip_other_game', title=title)
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
        mid = slug(zip_path.stem)
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


def status(dest, index, notes=None):
    """The library entries joined with what is installed: [{...entry, installed, enabled, update}] plus mods that are
    installed but not in the library. notes, a list, gets the lines of normalise."""
    have = {m['id']: m for m in installed(dest, notes)}
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
            notes = []
            print(json.dumps(status(args.dest, fetch_index(args.index), notes), indent=1))
            for note, params in notes:
                print(f'{note} {json.dumps(params)}', file=sys.stderr)
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
