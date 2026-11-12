"""--game: one path (a game file or a folder) instead of --base, --update and --dlc.

A folder holding exefs/ and the RomFS of the updated game is an extracted game (installer/extracted.py) instead.

A launcher or a script that holds the player's game folder passes it as is. Each .nsp, .xci, .nsz and .xcz in the folder is opened
with the keys and sorted by what is in it, never by its name: a Program NCA that is a BKTR patch is the update, any
other Program NCA is the base game, and a package with only Meta NCAs is add-on content. A file given with --base,
--update or --dlc wins over what --game finds. Several files of one kind: the first by name.
"""
from pathlib import Path

from .runner import InstallError
from . import extracted, nsz
from .steps import container_entries, load_keys, nca_inventory
from package_select import is_bktr   # on sys.path since steps.py imported the tools

CONTAINERS = ('.nsp', '.xci', '.nsz', '.xcz')
PROGRAM, META = 0, 1   # NCA content types


def kind_of(path, keys):
    """'base', 'update' or 'dlc' for the game file at path, None for a file that is none of them or cannot be read."""
    try:
        with path.open('rb') as f:
            _, entries = container_entries(f, path.stat().st_size)
            records = nca_inventory(f, nsz.nca_entries(f, entries), keys)
    except (ValueError, OSError):
        return None
    programs = [header for record, header in records if record['content_type'] == PROGRAM]
    if any(not is_bktr(header) for header in programs):
        return 'base'
    if programs:
        return 'update'
    if any(record['content_type'] == META for record, _ in records):
        return 'dlc'
    return None


def candidates(game):
    if game.is_file():
        return [game]
    return sorted(p for p in game.iterdir() if p.is_file() and p.suffix.lower() in CONTAINERS)


def resolve(options):
    """Fill options.base, .update and .dlc from options.game where they are not given. InstallError when a file or
    the keys are missing or unreadable, or the folder holds no base game or no update."""
    game = Path(options.game)
    if not game.exists():
        raise InstallError('check.input_missing', input='game', path=str(game))
    found_extracted = extracted.find(game)
    if found_extracted and not (options.base or options.update):
        # An extracted game (installer/extracted.py): no base or update file and no keys needed; add-on content
        # still comes from --dlc
        options.extracted = found_extracted
        return
    if not options.keys or not Path(options.keys).is_file():
        raise InstallError('check.inputs_needed')
    try:
        keys = load_keys(Path(options.keys))
    except (ValueError, OSError, UnicodeDecodeError):
        raise InstallError('check.keys_unreadable') from None
    found = {'base': [], 'update': [], 'dlc': []}
    for path in candidates(game):
        kind = kind_of(path, keys)
        if kind:
            found[kind].append(path)
    if not options.base:
        if not found['base']:
            raise InstallError('check.game_no_base', path=str(game))
        options.base = found['base'][0]
    if not options.update:
        if not found['update']:
            raise InstallError('check.game_no_update', path=str(game))
        options.update = found['update'][0]
    if not options.dlc:
        options.dlc = found['dlc']
