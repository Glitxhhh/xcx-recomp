"""The last install the setup window started, in a per-user file, so the next release (unpacked into a new folder)
starts with the form filled in and an update is one click.

    {"format": 1, "dest": "<install folder>", "base": ..., "update": ..., "dlc": ..., "keys": ...}

Absolute paths only. "keys" is the path of the keys file, never its contents. The install folder's own
xcx-install.json records the same paths ("paths", written by installer/cli.py, recorded_paths), so a folder the player
picks by hand fills the form too; this file is the pointer to the last folder, and the fallback for paths a run that
stopped before its first save never recorded there.

Windows: %APPDATA%\\XCX Recomp\\setup.json; elsewhere $XDG_CONFIG_HOME/xcx-recomp/setup.json (default
~/.config/xcx-recomp/setup.json). XCX_SETUP_RECENT names another file (tests).

Adapted from TotK Recomp (github.com/valentderah/totk-pc-recomp, MIT), installer/recent.py and installer/gui.py
load_recent: the per-user file, the checks on load and the bounded wait for a path on a gone network share.
"""
import json
import os
import threading
from pathlib import Path

from . import state as statefile

FORMAT = 1
ENV = 'XCX_SETUP_RECENT'
SOURCES = ('base', 'update', 'dlc', 'keys')   # files; dlc is the one add-on content NSP of the window
FIELDS = ('dest',) + SOURCES
WAIT = 1.5   # seconds the window waits for load()


def path():
    override = os.environ.get(ENV)
    if override:
        return Path(override)
    if os.name == 'nt':
        base = os.environ.get('APPDATA') or Path.home() / 'AppData' / 'Roaming'
        return Path(base) / 'XCX Recomp' / 'setup.json'
    base = os.environ.get('XDG_CONFIG_HOME') or Path.home() / '.config'
    return Path(base) / 'xcx-recomp' / 'setup.json'


def usable(name, value):
    """value when it is a non-empty absolute path of the right kind for the field (dest: a folder holding
    xcx-install.json; the rest: a file), else None. Stat calls only; a path the OS rejects counts as gone."""
    if not isinstance(value, str) or not value.strip() or not os.path.isabs(value):
        return None
    try:
        p = Path(value)
        ok = (p / statefile.NAME).is_file() if name == 'dest' else p.is_file()
    except (OSError, ValueError):
        return None
    return value if ok else None


def first_file(value):
    """The dlc entry of a record: a path, or the first usable one of a list (the CLI takes several)."""
    for v in (value if isinstance(value, list) else [value]):
        if usable('dlc', v):
            return v
    return None


def read():
    try:
        data = json.loads(path().read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


def recorded_paths(dest):
    """The "paths" the CLI recorded in dest's xcx-install.json, or {}. Never raises."""
    try:
        state = statefile.load(Path(dest))
    except (statefile.StateError, OSError, ValueError):
        return {}
    paths = (state or {}).get('paths')
    return paths if isinstance(paths, dict) else {}


def source(name, value):
    """A recorded source path that still names a file, else None (dlc: the first such entry of a list)."""
    return first_file(value) if name == 'dlc' else usable(name, value)


def from_folder(dest):
    """The source paths dest's xcx-install.json recorded that still name files, by field. Never raises."""
    recorded = recorded_paths(dest)
    return {name: v for name in SOURCES if (v := source(name, recorded.get(name)))}


def load():
    """The form fields to prefill, by name: the recorded install folder while it still holds xcx-install.json, and
    each source path that still names a file, from that folder's own record first, then from this file. A field
    whose path moved or is gone is left out, so the window shows it empty. Never raises."""
    data = read()
    dest = usable('dest', data.get('dest'))
    out = from_folder(dest) if dest else {}
    for name in SOURCES:
        if name not in out and (v := source(name, data.get(name))):
            out[name] = v
    if dest:
        out['dest'] = dest
    return out


def load_bounded(wait=WAIT):
    """load() in a daemon thread, waited for at most `wait` seconds: a recorded path on a disconnected network share
    can block its stat call for a long time. Nothing is prefilled when it has not returned by then."""
    out = {}
    thread = threading.Thread(target=lambda: out.update(load()), daemon=True)
    thread.start()
    thread.join(wait)
    return {} if thread.is_alive() else dict(out)


def save(dest, sources):
    """Records dest and the non-empty entries of sources (base, update, dlc, keys) as absolute paths; an entry left
    empty keeps its saved value. Atomic; a failure to write is ignored, the window works without it."""
    target = path()
    data = {name: v for name, v in read().items() if name in FIELDS and isinstance(v, str)}
    data['dest'] = os.path.abspath(dest.strip())
    for name, value in sources.items():
        if name in SOURCES and isinstance(value, str) and value.strip():
            data[name] = os.path.abspath(value.strip())
    tmp = target.with_name(target.name + '.tmp')
    try:
        target.parent.mkdir(parents=True, exist_ok=True)
        tmp.write_text(json.dumps({'format': FORMAT, **data}, indent=1, ensure_ascii=False) + '\n', encoding='utf-8')
        os.replace(tmp, target)
    except OSError:
        pass


def merge_paths(previous, options):
    """The "paths" for xcx-install.json: previous with each source the command line named and that exists replaced
    by its absolute path. A run given only --dest keeps them all. base may be a folder (--game, an extracted game)."""
    out = {k: v for k, v in (previous or {}).items() if k in ('base', 'update', 'dlc', 'keys')}
    given = {'base': getattr(options, 'game', None) or getattr(options, 'base', None),
             'update': getattr(options, 'update', None), 'keys': getattr(options, 'keys', None)}
    for name, value in given.items():
        if value is not None and Path(value).exists():
            out[name] = os.path.abspath(value)
    dlc = [os.path.abspath(p) for p in (getattr(options, 'dlc', None) or []) if Path(p).is_file()]
    if dlc:
        out['dlc'] = dlc
    return out
