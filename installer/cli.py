"""Command line: python -m installer --base FILE --update FILE --keys FILE [--dlc FILE ...] --lang CODE --dest DIR
                                     [--root DIR] [--temp DIR] [--verify]

--game PATH replaces --base, --update and --dlc: a game file or a folder holding them, sorted by content.
A first install needs every option. On a folder with xcx-install.json (resume after a cancel or failure, or update
with a newer release) only --dest is needed: --lang defaults to the recorded language, and the game files are needed
only when the extraction has to run again. Prints JSON events on stdout (events.py). Exit code: 0 done, 1 failed,
2 bad command line, 3 cancelled (Ctrl+C or Ctrl+Break).
"""
import argparse
import hashlib
import signal
import sys
import time
from pathlib import Path

from . import game_files
from . import guilogic
from . import ini
from . import layout as layouts
from . import state as statefile
from .events import Reporter
from .runner import Context, InstallError, run_steps
from .steps import all_steps

EXIT = {'ok': 0, 'failed': 1, 'cancelled': 3}


class Parser(argparse.ArgumentParser):
    def error(self, message):
        Reporter().error('cli.usage', {'detail': message})
        sys.exit(2)


# The temp folder inside the install folder when the layout names none (the Linux release); the check step does not
# count it when it requires an empty install folder
TEMP_IN_DEST = '.setup-temp'


def temp_folder(base, dest):
    """One temp folder per install folder, so two installs never share generated files."""
    tag = hashlib.sha256(str(dest).lower().encode()).hexdigest()[:8]
    return base / f'{dest.name or "root"}-{tag}'


def auto_language(dest, state):
    """--lang auto: the language already in the folder's xcx.ini, else the recorded one, else the window's default.
    A launcher passes it, so a language the player changed in xcx.ini survives an update."""
    path = dest / 'xcx.ini'
    chosen = ini.get_language(path.read_text(encoding='utf-8-sig')) if path.is_file() else None
    recorded = (state or {}).get('lang')   # a fresh folder has no state
    return ini.playable(chosen or recorded or guilogic.default_game_language(guilogic.windows_ui_language()))


def cancel(signum, frame):
    raise KeyboardInterrupt


def main(argv=None):
    ap = Parser(prog='python -m installer', description=__doc__.splitlines()[0])
    ap.add_argument('--game', type=Path, help='a game file or a folder holding the base game, the update and any '
                                              'add-on content (XCI or NSP), found by content; --base, --update and '
                                              '--dlc win over it')
    ap.add_argument('--base', type=Path, help='base game, XCI or NSP')
    ap.add_argument('--update', type=Path, help='2.0.0 update NSP')
    ap.add_argument('--dlc', type=Path, nargs='*', help='add-on content NSPs (optional)')
    ap.add_argument('--keys', type=Path, help='prod.keys')
    ap.add_argument('--lang', help='game language, a Switch language code (en-US, en-GB, ja, de, ru, ...), or auto')
    ap.add_argument('--dest', type=Path, required=True, help='install folder')
    ap.add_argument('--root', type=Path, help='release folder or repository (default: the folder above installer/)')
    ap.add_argument('--temp', type=Path, help='temp folder (default: temp/ of the release on Windows, .setup-temp in '
                                              'the install folder on Linux, build/install-temp in a repository)')
    ap.add_argument('--verify', action='store_true', help='hash every kept file, also those this release made')
    ap.add_argument('--dlss', action='store_true', help="download NVIDIA's nvngx_dlss.dll (NVIDIA RTX SDK license) next "
                                                      'to the game, for upscaler = dlss')
    ap.add_argument('--dlss-file', type=Path, help="an nvngx_dlss.dll to copy next to the game instead of downloading "
                                                    "NVIDIA's (a launcher passes the one it fetched)")
    ap.add_argument('--progress-lines', action='store_true',
                    help='print "progress <0 to 1> <text>" lines instead of JSON events')
    args = ap.parse_args(argv)
    # A launcher fills optional inputs the player left empty with nothing (--dlc=, --update=, --dlss-file=), which Path
    # reads as the current folder: treat those as not given
    if args.dlc is not None:
        args.dlc = [p for p in args.dlc if str(p) not in ('', '.')] or None
    for name in ('base', 'update', 'keys', 'dlss_file'):
        if getattr(args, name) is not None and str(getattr(args, name)) in ('', '.'):
            setattr(args, name, None)
    # A folder as the base game (RecompLauncher's folder input): an extracted game, read as --game
    if args.base and Path(args.base).is_dir() and not args.game:
        args.game, args.base = args.base, None

    # The window cancels with Ctrl+Break to the installer's process group (CREATE_NEW_PROCESS_GROUP), which also reaches
    # the tools it runs; Python's default for it ends the process without recording anything.
    for name in ('SIGBREAK', 'SIGTERM'):
        if hasattr(signal, name):
            signal.signal(getattr(signal, name), cancel)
    layout = layouts.find(args.root)
    dest = args.dest.resolve()
    base = args.temp or layout.temp
    temp = temp_folder(base.resolve(), dest) if base else dest / TEMP_IN_DEST
    log = temp / 'logs' / time.strftime('install-%Y%m%d-%H%M%S.log')
    rep = Reporter(log_path=log, lines=args.progress_lines)
    t0 = time.monotonic()
    try:
        state = statefile.load(dest)
    except statefile.StateError as e:
        rep.error(e.id, {'path': str(e.path)})
        rep.emit('end', result='failed', seconds=0)
        return 1
    if args.lang == 'auto':
        args.lang = auto_language(dest, state)
    if args.game:
        try:
            game_files.resolve(args)
        except InstallError as e:
            rep.error(e.id, e.params)
            rep.emit('end', result='failed', seconds=0)
            return 1
    ctx = Context(layout, dest, temp, rep, args, state=state, verify=args.verify)
    steps = all_steps()
    rep.emit('start', release=layout.release, mode=ctx.mode, dev=layout.dev, dest=str(dest), log=str(log),
             steps=[s.id for s in steps])
    result = run_steps(steps, ctx)
    rep.emit('end', result=result, seconds=round(time.monotonic() - t0, 1))
    rep.close()
    return EXIT[result]
